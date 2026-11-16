// browser-<version>-win-x64-setup.exe logic (no UI). See src/setup-core.h and design/windows-setup-plan.md.
#include "taskbar-identity.h"
#include "setup-core.h"

#include <bcrypt.h>
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <memory>
#include <vector>

#include "install-transaction.h"
#include "setup-uninstall-key.h"
#include "update-core.h"
#include "update-package.h"

namespace UP = UpdatePackage;

namespace SetupCore {
namespace {

constexpr DWORD RenameMs = 15 * 1000;              // same transient-hold budget as the updater (UP::MoveDirectory)
constexpr DWORD HelperWaitMs = 12 * 60 * 1000;     // helper's own limits: 60 s profile unlock + 120 s ack + renames

std::wstring Lower(std::wstring s) {
    CharLowerBuffW(s.data(), DWORD(s.size()));
    return s;
}
std::wstring W(const std::string& s) { return UP::Widen(s); }

std::string RandomHex(std::size_t bytes) {
    std::vector<std::uint8_t> b(bytes);
    if (BCryptGenRandom(nullptr, b.data(), ULONG(b.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        auto t = GetTickCount64() ^ (std::uint64_t(GetCurrentProcessId()) << 32);
        for (std::size_t i = 0; i < bytes; ++i) b[i] = std::uint8_t(t >> ((i % 8) * 8));
    }
    static const char* d = "0123456789abcdef";
    std::string s;
    for (auto x : b) {
        s += d[x >> 4];
        s += d[x & 15];
    }
    return s;
}

std::wstring KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR path = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &path)) && path) out = path;
    CoTaskMemFree(path);
    return out;
}

std::string Trim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s;
}

std::string Field(const std::string& text, const std::string& key) {
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        if (line.size() > key.size() && line.compare(0, key.size(), key) == 0 && line[key.size()] == ' ') return line.substr(key.size() + 1);
        start = end + 1;
    }
    return "";
}

// Same rule as the stable launcher (src/update-helper.cpp LaunchableCurrent).
std::string LaunchableCurrent(const std::wstring& root) {
    std::string current;
    UP::Version v;
    if (!UP::ReadSmallFile(root + L"\\current", 64, current)) return "";
    current = Trim(current);
    if (!UP::ParseVersion(current, v) || UP::IsZero(v) || UP::IsReparsePoint(root + L"\\versions")) return "";
    const std::wstring exe = root + L"\\versions\\" + W(current) + L"\\browser.exe";
    const DWORD a = GetFileAttributesW(exe.c_str());
    if (a == INVALID_FILE_ATTRIBUTES || (a & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) return "";
    if (UP::IsReparsePoint(root + L"\\versions\\" + W(current))) return "";
    return current;
}

// The install root (existing or to be created) is reached without any junction/symlink: its nearest existing
// ancestor resolves to exactly its lexical path. Destructive and creating steps only ever run under a root that
// passed this AND is pinned by the held InstallTxn::Lock (no ancestor can be swapped while it is held).
bool AncestryPlain(const std::wstring& root) {
    std::wstring p = InstallTxn::Absolute(root);
    while (!p.empty() && GetFileAttributesW(p.c_str()) == INVALID_FILE_ATTRIBUTES) {
        const auto pos = p.find_last_of(L'\\');
        if (pos == std::wstring::npos || pos < 3) return false;
        p = p.substr(0, pos);
    }
    return !p.empty() && InstallTxn::ResolvesToItself(p, true);
}

bool EnsurePlainDir(const std::wstring& dir) {
    if (!CreateDirectoryW(dir.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
    return UP::IsDirectory(dir) && !UP::IsReparsePoint(dir);
}

bool LockHeld(const std::wstring& lock) {
    if (!UP::PathExists(lock)) return false;
    HANDLE f = CreateFileW(lock.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        CloseHandle(f);
        return false;
    }
    const DWORD e = GetLastError();
    return e == ERROR_SHARING_VIOLATION || e == ERROR_LOCK_VIOLATION || e == ERROR_ACCESS_DENIED;
}

// A mapped image refuses write access: the exe is running (or held open for writing).
bool ImageInUse(const std::wstring& exe) {
    if (!UP::PathExists(exe)) return false;
    HANDLE f = CreateFileW(exe.c_str(), FILE_WRITE_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                           FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        CloseHandle(f);
        return false;
    }
    return GetLastError() == ERROR_SHARING_VIOLATION;
}

bool SameFile(const std::wstring& a, const std::wstring& b) {
    UP::Digest da{}, db{};
    std::uint64_t sa = 0, sb = 0;
    return UP::Sha256File(a, da, sa) && UP::Sha256File(b, db, sb) && sa == sb && da == db;
}

// Copy `from` over `to` through a temp file + replace (never a partial `to`).
bool ReplaceFile(const std::wstring& from, const std::wstring& to, const std::string& nonce) {
    const std::wstring temp = to + L".new-" + W(nonce);
    DeleteFileW(temp.c_str());
    if (!CopyFileW(from.c_str(), temp.c_str(), TRUE)) return false;
    if (!MoveFileExW(temp.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temp.c_str());
        return false;
    }
    return SameFile(from, to);
}

#ifdef SETUP_TESTING
// Fault points for tests/setup only (BROWSER_SETUP_FAULT=<point>); release stubs compile none.
bool Fault(const char* point) {
    wchar_t v[64];
    DWORD n = GetEnvironmentVariableW(L"BROWSER_SETUP_FAULT", v, 64);
    return n && n < 64 && UP::Narrow(std::wstring(v, n)) == point;
}
#else
constexpr bool Fault(const char*) { return false; }
#endif

struct ComInit {
    HRESULT hr;
    ComInit() : hr(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
    ~ComInit() { if (SUCCEEDED(hr)) CoUninitialize(); }
};

bool WriteShortcut(const std::wstring& lnk, const std::wstring& root) {
    ComInit com;
    IShellLinkW* link = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) return false;
    std::unique_ptr<IShellLinkW, void (*)(IShellLinkW*)> holder(link, [](IShellLinkW* p) { p->Release(); });
    const std::wstring launcher = root + L"\\browser.exe";
    link->SetPath(launcher.c_str());
    link->SetWorkingDirectory(root.c_str());
    link->SetIconLocation((root + L"\\browser.ico").c_str(), 0);
    link->SetDescription(L"browser");
    {   // same taskbar identity as the installed windows (src/taskbar-identity.h): a pin from this link groups with them
        IPropertyStore* store = nullptr;
        if (FAILED(link->QueryInterface(IID_PPV_ARGS(&store)))) return false;
        const bool set = TaskbarIdentity::SetString(store, PKEY_AppUserModel_ID, TaskbarIdentity::AppId) && SUCCEEDED(store->Commit());
        store->Release();
        if (!set) return false;
    }
    IPersistFile* file = nullptr;
    if (FAILED(link->QueryInterface(IID_PPV_ARGS(&file)))) return false;
    const HRESULT saved = file->Save(lnk.c_str(), TRUE);
    file->Release();
    return SUCCEEDED(saved) && Lower(ShortcutTarget(lnk)) == Lower(launcher);
}

bool Ours(const std::wstring& lnk, const std::wstring& root) {
    return UP::PathExists(lnk) && Lower(ShortcutTarget(lnk)) == Lower(root + L"\\browser.exe");
}

void MarkReported(const std::wstring& updates) {
    std::string text;
    if (!UP::ReadSmallFile(updates + L"\\last-result", 8192, text) || text.find("\nreported 0\n") == std::string::npos) return;
    text.replace(text.find("\nreported 0\n"), 12, "\nreported 1\n");  // the setup showed the failure itself
    UP::AtomicWriteFile(updates + L"\\last-result", text);
}

}  // namespace

Env DefaultEnv() {
    Env e;
    e.root = UpdateCore::DefaultInstallRoot();
    e.uninstallKey = SetupKey::UninstallKey();
    const std::wstring test = SetupKey::TestRoot();
    if (!test.empty()) {
        e.programs = test + L"\\StartMenu\\Programs";
        e.desktop = test + L"\\Desktop";
        e.temp = test + L"\\Temp";
        return e;
    }
    e.programs = KnownFolder(FOLDERID_Programs);
    e.desktop = KnownFolder(FOLDERID_Desktop);
    wchar_t temp[MAX_PATH + 1];
    DWORD n = GetTempPathW(MAX_PATH + 1, temp);
    e.temp = n && n <= MAX_PATH ? std::wstring(temp, n) : L"";
    while (e.temp.size() > 3 && e.temp.back() == L'\\') e.temp.pop_back();
    return e;
}

bool ReparseFree(const std::wstring& path) { return !UP::PathExists(path) || !UP::IsReparsePoint(path); }

std::wstring ShortcutTarget(const std::wstring& lnk) {
    if (!UP::PathExists(lnk)) return L"";
    ComInit com;
    IShellLinkW* link = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) return L"";
    std::wstring out;
    IPersistFile* file = nullptr;
    if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&file)))) {
        if (SUCCEEDED(file->Load(lnk.c_str(), STGM_READ))) {
            wchar_t path[MAX_PATH * 4];
            if (SUCCEEDED(link->GetPath(path, MAX_PATH * 4, nullptr, SLGP_RAWPATH))) out = path;
        }
        file->Release();
    }
    link->Release();
    return out;
}

std::string LastResultField(const Env& env, const std::string& key) {
    std::string text;
    if (!UP::ReadSmallFile(env.root + L"\\updates\\last-result", 8192, text)) return "";
    return Field(text, key);
}

Inspection Inspect(const Env& env, const std::string& version) {
    Inspection r;
    if (env.root.size() < 4 || !AncestryPlain(env.root)) return r.kind = Kind::Unsafe, r;
    for (const wchar_t* sub : {L"", L"\\versions", L"\\data", L"\\updates", L"\\bin"})
        if (!ReparseFree(env.root + sub)) return r.kind = Kind::Unsafe, r;
    if (UP::PathExists(env.root) && !UP::IsDirectory(env.root)) return r.kind = Kind::Unsafe, r;
    r.installed = LaunchableCurrent(env.root);
    if (r.installed.empty()) return r.kind = Kind::Fresh, r;
    UP::Version a, b;
    UP::ParseVersion(version, a);
    UP::ParseVersion(r.installed, b);
    const int order = UP::CompareVersion(a, b);
    r.kind = order > 0 ? Kind::Update : order == 0 ? Kind::Repair : Kind::Newer;
    return r;
}

bool BrowserRunning(const Env& env) {
    const std::wstring data = env.root + L"\\data";
    if (LockHeld(data + L"\\profile\\EBWebView\\lockfile") || LockHeld(data + L"\\profile\\lockfile")) return true;
    // Other profiles: data\profiles\<id>\profile (src/profiles.h); bounded listing of one directory level.
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileExW((data + L"\\profiles\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    bool busy = false;
    int seen = 0;
    if (find != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring name = fd.cFileName;
            if (name == L"." || name == L".." || !(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
            const std::wstring p = data + L"\\profiles\\" + name + L"\\profile";
            busy = LockHeld(p + L"\\EBWebView\\lockfile") || LockHeld(p + L"\\lockfile");
        } while (!busy && ++seen < 256 && FindNextFileW(find, &fd));
        FindClose(find);
    }
    if (busy) return true;
    const std::string current = LaunchableCurrent(env.root);
    return !current.empty() && ImageInUse(env.root + L"\\versions\\" + W(current) + L"\\browser.exe");
}

Result Install(const Env& env, const std::wstring& self, const SetupPayload::Layout& layout, const Options& options,
               const std::atomic<bool>* cancel, const Progress& progress) {
    Result r;
    auto fail = [&](Error e, const std::string& detail) {
        r.error = e;
        r.detail = detail;
        return r;
    };
    auto report = [&](Step s, double f) { if (progress) progress(s, f); };
    const std::string& version = layout.version;
    if (Inspect(env, version).kind == Kind::Unsafe) return fail(Error::Unsafe, "install root or one of its folders is not a plain directory");
    const std::wstring root = env.root, updates = root + L"\\updates", downloads = updates + L"\\downloads", staging = updates + L"\\staging";
    // One install/removal transaction per root (src/install-transaction.h), inheritable: handed to the helper.
    if (Fault("mkdir") || !EnsurePlainDir(root)) return fail(Error::Disk, "cannot create " + UP::Narrow(root));
    InstallTxn::Lock txn;
    if (!txn.Acquire(root, 0, true))
        return txn.Busy() ? fail(Error::Busy, "another install or uninstall of this root is running")
                          : fail(Error::Unsafe, "install root is not a plain directory reached without links");
    if (InstallTxn::RecoverRepair(root) == InstallTxn::RepairOutcome::Kept)
        return fail(Error::Helper, "an interrupted repair could not be finished (its original copy is kept)");
    const Inspection inspection = Inspect(env, version);  // under the lock: what the helper will see
    if (inspection.kind == Kind::Unsafe) return fail(Error::Unsafe, "install root or one of its folders is not a plain directory");
    if (inspection.kind == Kind::Newer) return fail(Error::Newer, "a newer version is installed: " + inspection.installed);
    if (BrowserRunning(env)) return fail(Error::Running, "browser is running");
    for (const std::wstring& dir : {root, updates, downloads, staging})
        if (Fault("mkdir") || !EnsurePlainDir(dir)) return fail(Error::Disk, "cannot create " + UP::Narrow(dir));
    const std::string nonce = RandomHex(16);

    // 1. Embedded package -> updates\downloads (hash checked against the trailer).
    report(Step::Prepare, 0);
    const std::wstring package = downloads + L"\\setup-" + W(nonce) + L".lsrupd";
    std::string error;
    if (!SetupPayload::CopyPackage(self, layout, package, cancel,
                                   [&](std::uint64_t done, std::uint64_t total) { report(Step::Prepare, 0.5 * double(done) / double(total)); }, error)) {
        if (error == "cancelled") return fail(Error::Cancelled, error);
        return fail(error.find("hash") != std::string::npos || error.find("read") != std::string::npos ? Error::Damaged : Error::Disk, error);
    }
    if (Fault("corrupt-copied-package")) {  // a byte flipped after the trailer check: the package validator must refuse it
        HANDLE f = CreateFileW(package.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        std::uint8_t b = 0;
        DWORD n = 0;
        SetFilePointer(f, -40, nullptr, FILE_END);
        ReadFile(f, &b, 1, &n, nullptr);
        b ^= 0x55;
        SetFilePointer(f, -40, nullptr, FILE_END);
        WriteFile(f, &b, 1, &n, nullptr);
        CloseHandle(f);
    }
    // 2. The updater's own validator: structure, allowlist, paths, sizes, per-entry SHA-256, CREATE_NEW extraction.
    UP::PackageIndex index;
    const std::wstring work = staging + L"\\" + W(version) + L".partial-" + W(nonce);
    bool ok = UP::ReadPackageIndex(package, index, error) && index.version == version;
    if (ok) ok = !Fault("extract") && UP::ExtractPackage(package, version, work, cancel, error);
    DeleteFileW(package.c_str());
    if (!ok) {
        UP::RemoveTree(work);
        if (cancel && cancel->load()) return fail(Error::Cancelled, "cancelled");
        const bool disk = error.find("write") != std::string::npos || error.find("cannot create") != std::string::npos || Fault("extract");
        return fail(disk ? Error::Disk : Error::Damaged, "package refused: " + error);
    }
    report(Step::Prepare, 0.9);
    const std::wstring stage = staging + L"\\" + W(version);
    if (UP::PathExists(stage)) {  // an earlier staging of the same version: replaced by this setup's own payload
        const std::wstring old = staging + L"\\.setup-old-" + W(nonce);
        if (!UP::MoveDirectory(stage, old, RenameMs)) {
            UP::RemoveTree(work);
            return fail(Error::Disk, "previous staging in use");
        }
        UP::RemoveTree(old);
    }
    if (!UP::MoveDirectory(work, stage, RenameMs) || !UP::VerifyStagedTree(stage, version, error)) {
        UP::RemoveTree(work);
        return fail(Error::Disk, "staging failed " + error);
    }
    if (cancel && cancel->load()) {
        UP::RemoveTree(stage);
        return fail(Error::Cancelled, "cancelled");
    }
    report(Step::Prepare, 1);
    if (Fault("pause-after-stage")) Sleep(4000);  // fixtures: a competing uninstall must get Busy here

    // 3. The payload's own helper performs the install (same code path as an update).
    if (BrowserRunning(env)) return fail(Error::Running, "browser started meanwhile");
    report(Step::Install, 0);
    const std::wstring helperDir = updates + L"\\helper", helper = helperDir + L"\\update-helper-" + W(nonce) + L".exe";
    if (!EnsurePlainDir(helperDir) || !CopyFileW((stage + L"\\update-helper.exe").c_str(), helper.c_str(), TRUE))
        return fail(Error::Disk, "cannot prepare the install helper");
    std::string plan = "browser-install 1\nnonce " + nonce + "\nversion " + version + "\npid 0\ncreated 0\norigin setup\nlaunch " +
                       (options.launch ? "1" : "0") + "\nlock-handle " +
                       std::to_string(static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(txn.Handle()))) + "\n";
    if (inspection.kind == Kind::Fresh) {
        plan += "previous-kind none\n";
    } else {
        plan += std::string("previous-kind ") + (inspection.kind == Kind::Repair ? "repair" : "installed") + "\nprevious-version " +
                inspection.installed + "\nprevious-exe " + UP::Narrow(root) + "\\versions\\" + inspection.installed + "\\browser.exe\n";
    }
    const std::wstring planPath = updates + L"\\install-" + W(nonce);
    if (!UP::AtomicWriteFile(planPath, plan)) {
        DeleteFileW(helper.c_str());
        return fail(Error::Disk, "cannot write the install plan");
    }
    std::wstring commandLine = L"\"" + helper + L"\" --install " + W(nonce);
    // The helper inherits exactly one handle, the lock: exclusion lasts until it exits even if this setup dies.
    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
    std::vector<std::uint8_t> attrBuffer(attrSize);
    auto attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuffer.data());
    HANDLE inherit = txn.Handle();
    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    bool started = InitializeProcThreadAttributeList(attrs, 1, 0, &attrSize) &&
                   UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &inherit, sizeof(inherit), nullptr, nullptr);
    si.lpAttributeList = attrs;
    started = started && CreateProcessW(helper.c_str(), commandLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT,
                                        nullptr, helperDir.c_str(), &si.StartupInfo, &pi);
    DeleteProcThreadAttributeList(attrs);
    if (!started) {
        DeleteFileW(planPath.c_str());
        DeleteFileW(helper.c_str());
        return fail(Error::Helper, "cannot start the install helper (error " + std::to_string(GetLastError()) + ")");
    }
    CloseHandle(pi.hThread);
    const DWORD waited = WaitForSingleObject(pi.hProcess, HelperWaitMs);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    DeleteFileW(helper.c_str());  // best effort: it has exited
    if (waited != WAIT_OBJECT_0) return fail(Error::Helper, "install helper still running");
    r.status = LastResultField(env, "status");
    const std::string reason = LastResultField(env, "reason"), resultVersion = LastResultField(env, "version");
    // Success needs the helper's own exit code too: a helper killed mid-way leaves an older last-result behind.
    const bool consistent = LastResultField(env, "nonce") == nonce &&
                            ((r.status == "installed" && code == 0) || (r.status == "unconfirmed" && code == 31));
    if (resultVersion != version || !consistent) {
        // Only a result stamped with THIS run's nonce describes this run (a killed helper leaves an older one).
        const bool reported = LastResultField(env, "nonce") == nonce && resultVersion == version;
        r.status = "failed";
        MarkReported(updates);
        UP::RemoveTree(stage);  // a helper that aborted before its move leaves this setup's staging: not an engine offer
        if (!reported) return fail(Error::Helper, "helper exit " + std::to_string(code) + " without a result");
        if (reason == "install-busy") return fail(Error::Busy, "helper: " + reason);
        if (reason == "no-startup-ack") return fail(Error::StartFailed, "the installed browser exited before confirming its start");
        if (reason == "profile-locked" || reason == "target-in-use" || reason.find("busy") != std::string::npos || reason.find("locked") != std::string::npos)
            return fail(Error::Running, "helper: " + reason);
        return fail(Error::Helper, "helper exit " + std::to_string(code) + ": " + (reason.empty() ? std::string("no result") : reason));
    }
    report(Step::Install, 1);

    // 4. Shortcuts, uninstaller, uninstall entry.
    report(Step::Integrate, 0);
    Result integrated = Integrate(env, self, layout, options);
    integrated.status = r.status;
    report(Step::Integrate, 1);
    return integrated;
}

Result Integrate(const Env& env, const std::wstring& self, const SetupPayload::Layout& layout, const Options& options) {
    Result r;
    const std::wstring root = env.root;
    const std::string current = LaunchableCurrent(root);
    if (current != layout.version) return r.error = Error::Integration, r.detail = "current is not " + layout.version, r;
    const std::string nonce = RandomHex(8);
    // Stable launcher: the helper refreshes it best effort; shortcuts need it, so it must be this version's helper.
    const std::wstring launcher = root + L"\\browser.exe", helper = root + L"\\versions\\" + W(current) + L"\\update-helper.exe";
    if (!SameFile(launcher, helper) && !ReplaceFile(helper, launcher, nonce))
        return r.error = Error::Integration, r.detail = "stable launcher could not be written", r;
    const std::wstring icon = root + L"\\browser.ico", iconSource = root + L"\\versions\\" + W(current) + L"\\assets\\browser.ico";
    if (UP::PathExists(iconSource) && !SameFile(icon, iconSource)) ReplaceFile(iconSource, icon, nonce);
    // Uninstaller = this setup without its payload.
    const std::wstring uninstaller = root + L"\\uninstall.exe", fresh = uninstaller + L".new-" + W(nonce);
    std::string error;
    DeleteFileW(fresh.c_str());
    if (Fault("uninstaller") || !SetupPayload::WriteStub(self, layout, fresh, error) ||
        !MoveFileExW(fresh.c_str(), uninstaller.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(fresh.c_str());
        return r.error = Error::Integration, r.detail = "uninstaller could not be written " + error, r;
    }
    // Shortcuts: always to <root>\browser.exe (never a version directory). A same-named link to another copy (e.g. a
    // portable folder) is not ours: left untouched and reported.
    if (env.programs.empty() || !(UP::IsDirectory(env.programs) || SHCreateDirectoryExW(nullptr, env.programs.c_str(), nullptr) == ERROR_SUCCESS))
        return r.error = Error::Integration, r.detail = "Start Menu folder unavailable", r;
    const std::wstring start = env.programs + L"\\browser.lnk", desk = env.desktop + L"\\browser.lnk";
    if (UP::PathExists(start) && !Ours(start, root)) {
        r.startMenuForeign = true;
    } else if (!WriteShortcut(start, root)) {
        return r.error = Error::Integration, r.detail = "Start Menu shortcut could not be written", r;
    }
    if (options.desktop) {
        if ((!UP::PathExists(desk) || Ours(desk, root)) && !WriteShortcut(desk, root))
            return r.error = Error::Integration, r.detail = "desktop shortcut could not be written", r;
    } else if (Ours(desk, root)) {
        DeleteFileW(desk.c_str());  // option changed: only our own link is removed
    }
    // Per-user uninstall entry (one app-owned HKCU key).
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, env.uninstallKey.c_str(), 0, nullptr, 0, KEY_SET_VALUE | KEY_QUERY_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return r.error = Error::Integration, r.detail = "uninstall entry could not be written", r;
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t date[16];
    swprintf(date, 16, L"%04u%02u%02u", t.wYear, t.wMonth, t.wDay);
    std::wstring installDate;
    bool ok = SetupKey::WriteString(key, L"DisplayName", L"browser") && SetupKey::WriteString(key, L"DisplayVersion", W(current)) &&
              SetupKey::WriteString(key, L"Publisher", L"VALXXII") && SetupKey::WriteString(key, L"DisplayIcon", icon) &&   // i18n-exempt: registry value names
              SetupKey::WriteString(key, L"InstallLocation", root) &&
              SetupKey::WriteString(key, L"UninstallString", L"\"" + uninstaller + L"\"");
    if (ok && !SetupKey::ReadString(key, L"InstallDate", installDate)) ok = SetupKey::WriteString(key, L"InstallDate", date);
    const DWORD one = 1, sizeKb = DWORD(layout.packageSize / 1024);
    ok = ok && RegSetValueExW(key, L"NoModify", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&one), sizeof(one)) == ERROR_SUCCESS &&
         RegSetValueExW(key, L"NoRepair", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&one), sizeof(one)) == ERROR_SUCCESS &&
         RegSetValueExW(key, L"EstimatedSize", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&sizeKb), sizeof(sizeKb)) == ERROR_SUCCESS;
    RegCloseKey(key);
    if (!ok) return r.error = Error::Integration, r.detail = "uninstall entry could not be written", r;
    return r;
}

UninstallReport Uninstall(const Env& env, bool removeData) {
    UninstallReport r;
    const std::wstring root = env.root;
    auto fail = [&](Error e, const std::string& d) {
        r.error = e;
        r.detail = d;
        return r;
    };
    if (root.size() < 4 || !AncestryPlain(root)) return fail(Error::Unsafe, "the install root is reached through a link; nothing removed");
    for (const wchar_t* sub : {L"", L"\\versions", L"\\data", L"\\updates", L"\\bin"})
        if (!ReparseFree(root + sub)) return fail(Error::Unsafe, "a folder of the install is a reparse point; nothing removed");
    InstallTxn::Lock txn;  // same exclusion as installs; pins root and its ancestors while held
    if (UP::IsDirectory(root) && !txn.Acquire(root, 0))
        return txn.Busy() ? fail(Error::Busy, "another install or uninstall of this root is running")
                          : fail(Error::Unsafe, "the install root is reached through a link; nothing removed");
    if (Fault("pause-uninstall")) Sleep(4000);
    if (txn.Held() && InstallTxn::RecoverRepair(root) == InstallTxn::RepairOutcome::Kept)
        return fail(Error::Helper, "an interrupted repair could not be finished; nothing removed");
    if (BrowserRunning(env)) return fail(Error::Running, "browser is running");
    const std::string nonce = RandomHex(8);
    const std::wstring trash = root + L"\\.uninstall-" + W(nonce);
    if (UP::IsDirectory(root)) {
        if (!CreateDirectoryW(trash.c_str(), nullptr)) return fail(Error::Disk, "cannot prepare removal");
        // Program folders first, as whole directories: a rename fails while anything inside runs, so nothing is half
        // removed. versions + updates go together or not at all.
        const std::wstring versions = root + L"\\versions", updates = root + L"\\updates";
        if (UP::PathExists(versions) && (Fault("uninstall-versions") || !UP::MoveDirectory(versions, trash + L"\\versions", RenameMs))) {
            RemoveDirectoryW(trash.c_str());
            return fail(Error::Running, "program files are in use");
        }
        if (UP::PathExists(updates) && !UP::MoveDirectory(updates, trash + L"\\updates", RenameMs)) {
            UP::MoveDirectory(trash + L"\\versions", versions, RenameMs);
            RemoveDirectoryW(trash.c_str());
            return fail(Error::Running, "update files are in use");
        }
        // bin: a bridge run by an agent right now cannot move; it stays (reported), never killed.
        if (UP::PathExists(root + L"\\bin") && !UP::MoveDirectory(root + L"\\bin", trash + L"\\bin", 2000)) r.leftovers = true;
        for (const wchar_t* name : {L"\\current", L"\\previous", L"\\browser.ico", L"\\browser.exe", L"\\uninstall.exe"}) {
            const std::wstring f = root + name;
            if (UP::PathExists(f) && !MoveFileExW(f.c_str(), (trash + name).c_str(), 0)) r.leftovers = true;
        }
        if (!UP::RemoveTree(trash)) r.leftovers = true;
    }
    for (const std::wstring& lnk : {env.programs + L"\\browser.lnk", env.desktop + L"\\browser.lnk"})
        if (Ours(lnk, root)) DeleteFileW(lnk.c_str());
    RegDeleteKeyW(HKEY_CURRENT_USER, env.uninstallKey.c_str());
    if (removeData && UP::PathExists(root + L"\\data")) {
        // Only <root>\data itself, a plain directory (checked above); RemoveTree never follows a reparse point inside.
        const std::wstring doomed = root + L"\\.data-removed-" + W(nonce);
        if (!InstallTxn::ResolvesToItself(root + L"\\data", true) || !UP::MoveDirectory(root + L"\\data", doomed, RenameMs)) {
            r.detail = "data in use: kept";
        } else {
            r.dataKept = false;
            if (!UP::RemoveTree(doomed)) r.leftovers = true;
        }
    }
    if (txn.Held()) {
        txn.Release();
        DeleteFileW((root + L"\\" + InstallTxn::LockName).c_str());
    }
    RemoveDirectoryW(root.c_str());  // only when empty
    return r;
}

}  // namespace SetupCore
