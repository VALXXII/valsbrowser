// update-helper.exe — two roles, no WebView2, no network:
//   1. "--install <nonce>" when running from <root>\updates\helper\: waits for the exact browser process recorded in
//      <root>\updates\install-<nonce> to exit (never kills), moves the verified staged payload to <root>\versions\<v>,
//      migrates portable data once (copy only), switches <root>\current atomically, launches the new version and waits for
//      its startup ack. Without ack (process exited) it restores the previous version and relaunches it.
//   2. Launcher (any other invocation, used as <root>\browser.exe): starts <root>\versions\<current>\browser.exe.
// The root is derived only from this executable's own location.
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "annotations-migration.h"
#include "install-transaction.h"  // root lock + durable repair record (shared with the setup)
#include "setup-uninstall-key.h"  // DisplayVersion of the setup's per-user uninstall entry, if present
#include "update-package.h"
#include "ui-language.h"   // the launcher's only message, in the browser-wide interface language

namespace UP = UpdatePackage;
namespace AM = AnnotationsMigration;

namespace {

constexpr DWORD WaitBrowserMs = 10 * 60 * 1000;
constexpr DWORD ProfileUnlockMs = 60 * 1000;
constexpr DWORD AckWaitMs = 120 * 1000;
// Directory renames retried while a scanner/indexer still holds a file just written under them (UP::MoveDirectory).
constexpr DWORD TransientRenameMs = 15 * 1000;

std::wstring g_log;
std::string g_nonce;  // this install's nonce, stamped on last-result (a setup trusts only its own result)

std::wstring Lower(std::wstring s) {
    CharLowerBuffW(s.data(), DWORD(s.size()));
    return s;
}
std::wstring Parent(const std::wstring& p) {
    auto pos = p.find_last_of(L'\\');
    return pos == std::wstring::npos ? L"" : p.substr(0, pos);
}
std::wstring Leaf(const std::wstring& p) {
    auto pos = p.find_last_of(L'\\');
    return pos == std::wstring::npos ? p : p.substr(pos + 1);
}
std::wstring FullPath(const std::wstring& p) {
    wchar_t buffer[4096];
    DWORD n = GetFullPathNameW(p.c_str(), 4096, buffer, nullptr);
    if (!n || n >= 4096) return L"";
    std::wstring s(buffer, n);
    while (s.size() > 3 && s.back() == L'\\') s.pop_back();
    return s;
}
std::wstring SelfPath() {
    std::wstring buffer(32768, L'\0');
    DWORD n = GetModuleFileNameW(nullptr, buffer.data(), DWORD(buffer.size()));
    buffer.resize(n);
    return FullPath(buffer);
}
std::vector<std::string> Lines(const std::string& text) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start < text.size()) {
        auto end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        out.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return out;
}
std::string Field(const std::vector<std::string>& lines, const std::string& key) {
    for (const auto& l : lines)
        if (l.size() > key.size() && l.compare(0, key.size(), key) == 0 && l[key.size()] == ' ') return l.substr(key.size() + 1);
    return "";
}
void Log(const std::string& message) {
    if (g_log.empty()) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    char stamp[64];
    std::snprintf(stamp, sizeof(stamp), "%04u-%02u-%02u %02u:%02u:%02u ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    std::string line = stamp + message + "\r\n";
    HANDLE f = CreateFileW(g_log.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD put = 0;
    WriteFile(f, line.data(), DWORD(line.size()), &put, nullptr);
    CloseHandle(f);
}

// Quote one argument for CommandLineToArgvW / CRT parsing.
std::wstring Quote(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) return arg;
    std::wstring out = L"\"";
    for (std::size_t i = 0;; ++i) {
        std::size_t slashes = 0;
        while (i < arg.size() && arg[i] == L'\\') {
            ++i;
            ++slashes;
        }
        if (i == arg.size()) {
            out.append(slashes * 2, L'\\');
            break;
        }
        if (arg[i] == L'"')
            out.append(slashes * 2 + 1, L'\\');
        else
            out.append(slashes, L'\\');
        out += arg[i];
    }
    return out + L"\"";
}

bool Launch(const std::wstring& exe, const std::vector<std::wstring>& args, PROCESS_INFORMATION& pi) {
    std::wstring cmd = Quote(exe);
    for (const auto& a : args) cmd += L" " + Quote(a);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    std::wstring dir = Parent(exe);
    return CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi) != 0;
}

bool ValidExeAt(const std::wstring& exe) {
    DWORD a = GetFileAttributesW(exe.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
           !UP::IsReparsePoint(Parent(exe));
}

// "<name>.exe" with 1..64 chars of [A-Za-z0-9._-] before ".exe": no spaces, quotes, ADS ':' or separators.
bool PortableExeName(const std::wstring& leaf) {
    if (leaf.size() < 5 || leaf.size() > 68 || Lower(leaf.substr(leaf.size() - 4)) != L".exe") return false;
    std::wstring stem = leaf.substr(0, leaf.size() - 4);
    if (stem.front() == L'.' || stem.back() == L'.') return false;
    for (wchar_t c : stem)
        if (!((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || c == L'-' || c == L'_' || c == L'.'))
            return false;
    return Lower(leaf) != L"update-helper.exe";
}

// Absolute "X:\..." on a local drive letter: no UNC, no \\?\ device paths, no ADS colon after the drive.
bool LocalDrivePath(const std::wstring& path) {
    if (path.size() < 4 || path[1] != L':' || path[2] != L'\\') return false;
    wchar_t d = path[0];
    if (!((d >= L'A' && d <= L'Z') || (d >= L'a' && d <= L'z'))) return false;
    if (path.find(L':', 2) != std::wstring::npos || path.find(L'/') != std::wstring::npos) return false;
    wchar_t drive[4] = {d, L':', L'\\', 0};
    UINT type = GetDriveTypeW(drive);
    return type == DRIVE_FIXED || type == DRIVE_REMOVABLE;
}

// ---------------------------------------------------------------- test faults
// Fault injection for the install fixture (tests/update-core) ONLY: compiled in with -DUPDATE_HELPER_FAULTS,
// never by tools/release/build.py. BROWSER_HELPER_FAULT=<point> makes that step fail ("fail") or kills the
// helper on the spot (points starting with "kill-"), to prove recovery from every intermediate state.
#ifdef UPDATE_HELPER_FAULTS
bool Fault(const char* point) {
    wchar_t v[64];
    DWORD n = GetEnvironmentVariableW(L"BROWSER_HELPER_FAULT", v, 64);
    if (!n || n >= 64 || UP::Narrow(std::wstring(v, n)) != point) return false;
    if (std::string(point).rfind("kill-", 0) == 0) TerminateProcess(GetCurrentProcess(), 77);
    return true;
}
#else
constexpr bool Fault(const char*) { return false; }
#endif

// Engine-started installs wait this long for a running setup/uninstall to finish before failing cleanly.
DWORD LockWaitMs() {
#ifdef UPDATE_HELPER_FAULTS
    wchar_t v[16];
    if (GetEnvironmentVariableW(L"BROWSER_HELPER_LOCK_WAIT_MS", v, 16)) return DWORD(_wtoi(v));
#endif
    return 60 * 1000;
}

// ---------------------------------------------------------------- stable bridge
// Agents (Claude, Codex) are configured once with <root>\bin\browser-annotations-mcp.exe (src/agent-connect.cpp):
// version directories are pruned, so the versioned copy (assets\bin\ of the package, the only place existing
// clients accept) must be published there. Invariant restored by ReconcileBridge(root, v): the bin file is
// byte-identical to versions\<v>\assets\bin\<bridge>, or absent when v ships none (an older current never keeps a
// newer, possibly incompatible bridge). A running bridge is never killed or written: its file is renamed aside
// (allowed while the image is mapped; replacing it in place is not) and deleted later once nothing runs it. The
// only moment the final name is absent is between that rename and the next one (two renames in a row); a crash
// there is repaired by the next ReconcileBridge (install start, rollback, every launcher start).
constexpr const wchar_t* BridgeName = L"browser-annotations-mcp.exe";

class BinLock {  // exclusive <root>\bin\.publish-lock: one reconciler at a time (helper install vs launcher)
public:
    BinLock(const std::wstring& bin, DWORD waitMs) {
        ULONGLONG end = GetTickCount64() + waitMs;
        for (;;) {
            h_ = CreateFileW((bin + L"\\.publish-lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                             FILE_ATTRIBUTE_HIDDEN | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (h_ != INVALID_HANDLE_VALUE || GetTickCount64() >= end) return;
            Sleep(100);
        }
    }
    ~BinLock() { if (h_ != INVALID_HANDLE_VALUE) CloseHandle(h_); }
    BinLock(const BinLock&) = delete;
    BinLock& operator=(const BinLock&) = delete;
    bool Held() const { return h_ != INVALID_HANDLE_VALUE; }
private:
    HANDLE h_ = INVALID_HANDLE_VALUE;
};

bool SameBytes(const std::wstring& a, const std::wstring& b) {
    UP::Digest da{}, db{};
    std::uint64_t sa = 0, sb = 0;
    return UP::Sha256File(a, da, sa) && UP::Sha256File(b, db, sb) && sa == sb && da == db;
}

// Removes leftovers of earlier publications; files still executed by an agent stay until a later pass.
void CleanBridgeLeftovers(const std::wstring& bin) {
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileExW((bin + L"\\.*-*.exe").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        std::wstring name = fd.cFileName;
        if (fd.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) continue;
        if (name.rfind(L".old-", 0) == 0 || name.rfind(L".new-", 0) == 0) DeleteFileW((bin + L"\\" + name).c_str());
    } while (FindNextFileW(find, &fd));
    FindClose(find);
}

// Caller holds the BinLock when bin exists. `version` empty: no current version (portable previous).
// Final path of an open handle, lower-case, without the \\?\ prefix ("" on failure).
std::wstring FinalPath(HANDLE h) {
    std::wstring buffer(32768, L'\0');
    DWORD n = GetFinalPathNameByHandleW(h, buffer.data(), DWORD(buffer.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!n || n >= buffer.size()) return L"";
    buffer.resize(n);
    if (buffer.rfind(L"\\\\?\\UNC\\", 0) == 0) buffer = L"\\\\" + buffer.substr(8);
    else if (buffer.rfind(L"\\\\?\\", 0) == 0) buffer = buffer.substr(4);
    return Lower(buffer);
}

// Opens `rel` under `root` without following a final reparse point and checks that the object actually opened is
// exactly <final path of root>\rel: a junction or symlink ANYWHERE between root and the object (assets, bin,
// versions\<v>, ...) changes the final path and is refused. Root itself may be reached through a link above it
// (the user's profile path), never below it.
class RootedHandle {
public:
    RootedHandle(const std::wstring& root, const std::wstring& rel, bool directory, DWORD access = GENERIC_READ) {
        HANDLE r = CreateFileW(root.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                               OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (r == INVALID_HANDLE_VALUE) return;
        const std::wstring rootFinal = FinalPath(r);
        CloseHandle(r);
        if (rootFinal.empty()) return;
        h_ = CreateFileW((root + L"\\" + rel).c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                         OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | (directory ? FILE_FLAG_BACKUP_SEMANTICS : 0), nullptr);
        if (h_ == INVALID_HANDLE_VALUE) return;
        FILE_ATTRIBUTE_TAG_INFO tag{};
        const bool kindOk = GetFileInformationByHandleEx(h_, FileAttributeTagInfo, &tag, sizeof(tag)) &&
                            !(tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
                            bool(tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) == directory;
        if (!kindOk || FinalPath(h_) != rootFinal + L"\\" + Lower(rel)) {
            CloseHandle(h_);
            h_ = INVALID_HANDLE_VALUE;
        }
    }
    ~RootedHandle() { if (h_ != INVALID_HANDLE_VALUE) CloseHandle(h_); }
    RootedHandle(const RootedHandle&) = delete;
    RootedHandle& operator=(const RootedHandle&) = delete;
    bool Ok() const { return h_ != INVALID_HANDLE_VALUE; }
    HANDLE Get() const { return h_; }
private:
    HANDLE h_ = INVALID_HANDLE_VALUE;
};

bool ReadAll(HANDLE h, std::string& out, std::uint64_t max) {
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(h, &size) || size.QuadPart < 0 || std::uint64_t(size.QuadPart) > max) return false;
    out.assign(std::size_t(size.QuadPart), '\0');
    std::size_t done = 0;
    while (done < out.size()) {
        DWORD got = 0;
        const DWORD want = DWORD(std::min<std::size_t>(out.size() - done, 1 << 20));
        if (!ReadFile(h, out.data() + done, want, &got, nullptr) || got == 0) return false;
        done += got;
    }
    return true;
}

// Caller holds the BinLock. `version` empty: no current version (portable previous). Every object read or written is
// opened through RootedHandle (no reparse point anywhere under root); the bytes published are the bytes read from the
// verified source handle, and the published file is re-read through a verified handle and compared.
bool ReconcileBridge(const std::wstring& root, const std::string& version, const std::string& tag, std::string& error) {
    const std::wstring bin = root + L"\\bin", final_ = bin + L"\\" + BridgeName;
    const std::wstring srcRel = version.empty() ? L"" : L"versions\\" + UP::Widen(version) + L"\\assets\\bin\\" + BridgeName;
    const bool want = !srcRel.empty() && UP::PathExists(root + L"\\" + srcRel);
    std::string bytes;
    if (want) {
        RootedHandle source(root, srcRel, false);
        if (!source.Ok() || !ReadAll(source.Get(), bytes, 256ull << 20)) return error = "bridge source invalid", false;
    }
    if (UP::PathExists(bin) && !RootedHandle(root, L"bin", true).Ok()) return error = "bin is not a plain directory", false;
    // Byte-for-byte (PERF-19): the launcher runs this before every browser start; a size mismatch needs no read, and
    // equal bytes need no SHA-256 of either copy.
    auto publishedIs = [&](const std::wstring& rel) {
        RootedHandle h(root, rel, false);
        LARGE_INTEGER size{};
        if (!h.Ok() || !GetFileSizeEx(h.Get(), &size) || std::uint64_t(size.QuadPart) != bytes.size()) return false;
        std::string got;
        return ReadAll(h.Get(), got, 256ull << 20) && got == bytes;
    };
    const bool present = UP::PathExists(final_);
    if (want && present && publishedIs(L"bin\\" + std::wstring(BridgeName))) {
        CleanBridgeLeftovers(bin);
        return true;  // already published
    }
    if (!want && !present) return true;
    const std::wstring asideName = L".old-" + UP::Widen(tag) + L".exe", freshName = L".new-" + UP::Widen(tag) + L".exe";
    const std::wstring aside = bin + L"\\" + asideName, fresh = bin + L"\\" + freshName;
    if (want) {
        if (!CreateDirectoryW(bin.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return error = "bin create failed", false;
        if (!RootedHandle(root, L"bin", true).Ok()) return error = "bin is not a plain directory", false;
        DeleteFileW(fresh.c_str());
        bool written = false;
        if (!Fault("bin-copy")) {
            HANDLE out = CreateFileW(fresh.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (out != INVALID_HANDLE_VALUE) {
                DWORD put = 0;
                written = bytes.empty() || (WriteFile(out, bytes.data(), DWORD(bytes.size()), &put, nullptr) && put == bytes.size());
                written = written && FlushFileBuffers(out);
                CloseHandle(out);
            }
        }
        if (!written || !publishedIs(L"bin\\" + freshName)) {
            DeleteFileW(fresh.c_str());
            return error = "bridge copy failed", false;
        }
    }
    if (present && (Fault("bin-aside") || !MoveFileExW(final_.c_str(), aside.c_str(), MOVEFILE_WRITE_THROUGH))) {
        DeleteFileW(fresh.c_str());
        return error = "bridge in place could not be moved aside", false;
    }
    Fault("kill-after-aside");
    if (want && (Fault("bin-final") || !MoveFileExW(fresh.c_str(), final_.c_str(), MOVEFILE_WRITE_THROUGH) ||
                 !publishedIs(L"bin\\" + std::wstring(BridgeName)))) {
        if (UP::PathExists(final_) && !UP::PathExists(fresh)) MoveFileExW(final_.c_str(), fresh.c_str(), 0);
        if (present) MoveFileExW(aside.c_str(), final_.c_str(), MOVEFILE_WRITE_THROUGH);  // previous file back in place
        DeleteFileW(fresh.c_str());
        return error = "bridge publish failed", false;
    }
    CleanBridgeLeftovers(bin);
    return true;
}

// ---------------------------------------------------------------- launcher
void RecoverAnnotationsMigration(const std::wstring& root, const std::string& tag);  // install section
bool WriteSourcePointer(const std::wstring& root, const std::wstring& src);           // install section


// Reads <root>\current: the version if valid and its browser.exe is launchable, else "".
std::string LaunchableCurrent(const std::wstring& root) {
    std::string current;
    UP::Version v;
    if (!UP::ReadSmallFile(root + L"\\current", 64, current)) return "";
    while (!current.empty() && (current.back() == '\n' || current.back() == '\r')) current.pop_back();
    if (!UP::ParseVersion(current, v) || UP::IsReparsePoint(root + L"\\versions")) return "";
    return ValidExeAt(root + L"\\versions\\" + UP::Widen(current) + L"\\browser.exe") ? current : "";
}

int RunLauncher(int argc, wchar_t** argv) {
    const std::wstring root = Parent(SelfPath()), bin = root + L"\\bin";
    RecoverAnnotationsMigration(root, "launch-" + std::to_string(GetCurrentProcessId()));
    if (UP::PathExists(InstallTxn::RepairRecord(root))) {  // an interrupted repair: finish it if no install runs now
        InstallTxn::Lock txn;
        if (txn.Acquire(root, 0)) InstallTxn::RecoverRepair(root);
    }
    // The stable bridge is reconciled and the browser launched for the SAME `current`, read under the bin lock: an
    // install switching `current` holds that lock from its publication to its switch, so this launcher either sees
    // the old state before it, or the new one after it — never a stale value. Busy lock (install in progress past
    // the timeout): launch without touching bin; the install reconciles it.
    std::string current = LaunchableCurrent(root);
    std::unique_ptr<BinLock> lock;
    if (!current.empty() && (UP::PathExists(bin) || UP::PathExists(root + L"\\versions\\" + UP::Widen(current) + L"\\assets\\bin\\" + BridgeName))) {
        if ((UP::PathExists(bin) || CreateDirectoryW(bin.c_str(), nullptr)) && RootedHandle(root, L"bin", true).Ok()) {
            lock = std::make_unique<BinLock>(bin, 15000);
            if (lock->Held()) {
                current = LaunchableCurrent(root);  // re-read under the lock
                std::string error;
                if (!current.empty()) ReconcileBridge(root, current, "launch-" + std::to_string(GetCurrentProcessId()), error);
                if (Fault("pause-launcher-locked")) Sleep(4000);
            }
        }
    }
    if (!current.empty()) {
        std::vector<std::wstring> args(argv + 1, argv + argc);
        PROCESS_INFORMATION pi{};
        if (Launch(root + L"\\versions\\" + UP::Widen(current) + L"\\browser.exe", args, pi)) {  // still under the lock
            AllowSetForegroundWindow(pi.dwProcessId);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return 0;
        }
    }
    I18n::SetForProcess(UiLanguage::Load(root + L"\\data").locale);   // installed layout: <root>\data (French when absent)
    MessageBoxW(nullptr, TR(launcher_install_broken), L"Browser", MB_OK | MB_ICONERROR);
    return 2;
}

// ---------------------------------------------------------------- install

// After every install (setup or update): the Apps entry follows the version and the « Browser » name, and Browser is
// registered with Windows as a web browser for this root (src/default-browser.h), in the person's language. Existing
// installs get the registration at their first update by this helper.
void RefreshIntegration(const std::wstring& root, const std::string& version) {
    SetupKey::RefreshDisplayVersion(root, version);
    I18n::SetForProcess(UiLanguage::Load(root + L"\\data").locale);
    if (!SetupKey::RegisterDefaultBrowser(root)) Log("default-browser registration not written");
}

struct Plan {
    std::string nonce, version, previousVersion, previousKind;
    std::wstring previousExe;
    DWORD pid = 0;
    std::uint64_t created = 0;
    // Written by browser-<v>-win-x64-setup.exe (src/setup-core.cpp) for the helper of the SAME version only; engine
    // plans have no « origin » line and are parsed exactly as before. A setup plan: previous-kind none (first install),
    // installed (pid 0 = nothing to wait for) or repair (reinstall of `current` itself); never portable; nothing is
    // relaunched on failure (the setup reports it); `launch 0` installs without starting the browser.
    bool setup = false, launch = true;
    // Setup plans: the setup's inheritable handle to <root>\install.lock, verified by final path (InstallTxn::Lock).
    std::uint64_t lockHandle = 0;
};

bool ParsePlan(const std::string& text, const std::wstring& root, const std::string& nonce, Plan& p, std::string& error) {
    auto lines = Lines(text);
    if (lines.empty() || lines[0] != "browser-install 1") return error = "plan header", false;
    p.nonce = Field(lines, "nonce");
    p.version = Field(lines, "version");
    p.previousVersion = Field(lines, "previous-version");
    p.previousKind = Field(lines, "previous-kind");
    p.previousExe = FullPath(UP::Widen(Field(lines, "previous-exe")));
    const std::string origin = Field(lines, "origin"), launch = Field(lines, "launch");
    if (!origin.empty() && origin != "setup") return error = "plan origin", false;
    p.setup = origin == "setup";
    if (!launch.empty() && (!p.setup || (launch != "0" && launch != "1"))) return error = "plan launch", false;
    p.launch = launch != "0";
    if (p.nonce != nonce) return error = "plan nonce", false;
    UP::Version v, pv;
    if (!UP::ParseVersion(p.version, v) || UP::IsZero(v)) return error = "plan version", false;
    unsigned long long pid = std::strtoull(Field(lines, "pid").c_str(), nullptr, 10);
    p.created = std::strtoull(Field(lines, "created").c_str(), nullptr, 10);
    if (p.setup) {
        if (Field(lines, "pid") != "0" || Field(lines, "created") != "0") return error = "plan pid", false;  // nothing to wait for
        const std::string lock = Field(lines, "lock-handle");
        p.lockHandle = std::strtoull(lock.c_str(), nullptr, 10);
        if (lock.empty() || lock.find_first_not_of("0123456789") != std::string::npos || !p.lockHandle) return error = "plan lock", false;
        if (p.previousKind == "none") {
            if (!p.previousVersion.empty() || !Field(lines, "previous-exe").empty()) return error = "plan previous", false;
            return true;
        }
        if (!UP::ParseVersion(p.previousVersion, pv)) return error = "plan version", false;
        const int order = UP::CompareVersion(v, pv);
        if (p.previousKind == "repair" ? order != 0 : (p.previousKind != "installed" || order <= 0)) return error = "plan kind", false;
        if (Lower(p.previousExe) != Lower(root + L"\\versions\\" + UP::Widen(p.previousVersion) + L"\\browser.exe"))
            return error = "installed previous exe outside root", false;
        return true;
    }
    if (!UP::ParseVersion(p.previousVersion, pv) || UP::CompareVersion(v, pv) <= 0) return error = "plan version", false;
    if (!pid || pid > 0xffffffffull || !p.created) return error = "plan pid", false;
    p.pid = DWORD(pid);
    if (p.previousExe.empty()) return error = "plan previous exe", false;
    if (p.previousKind == "installed") {
        if (Lower(p.previousExe) != Lower(root + L"\\versions\\" + UP::Widen(p.previousVersion) + L"\\browser.exe"))
            return error = "installed previous exe outside root", false;
    } else if (p.previousKind == "portable") {
        // Portable/dev builds keep their own exe names (browser.exe, browser-main-78f5340.exe, browser-url-ease.exe ...).
        // Accept a plain local "X:\...\<name>.exe" outside the root; the exact pid + creation time + image path check
        // in WaitForBrowser still binds the plan to the process that wrote it.
        if (!PortableExeName(Leaf(p.previousExe)) || !LocalDrivePath(p.previousExe) || UP::IsUnder(p.previousExe, root) ||
            Lower(p.previousExe) == Lower(root) || !ValidExeAt(p.previousExe))
            return error = "portable exe invalid", false;
    } else {
        return error = "plan kind", false;
    }
    return true;
}

// Waits for the exact browser process (pid + creation time + image path) to exit. Never terminates it.
bool WaitForBrowser(const Plan& p) {
    HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, p.pid);
    if (!h) {
        Log("browser pid already gone");
        return true;
    }
    FILETIME c{}, e{}, k{}, u{};
    bool same = false;
    if (GetProcessTimes(h, &c, &e, &k, &u)) {
        std::uint64_t created = (std::uint64_t(c.dwHighDateTime) << 32) | c.dwLowDateTime;
        wchar_t image[4096];
        DWORD n = 4096;
        same = created == p.created && QueryFullProcessImageNameW(h, 0, image, &n) &&
               Lower(FullPath(std::wstring(image, n))) == Lower(p.previousExe);
    }
    if (!same) {
        CloseHandle(h);
        Log("pid reused by another process; original browser already exited");
        return true;
    }
    DWORD r = WaitForSingleObject(h, WaitBrowserMs);
    CloseHandle(h);
    if (r != WAIT_OBJECT_0) {
        Log("browser did not exit in time; aborting without touching it");
        return false;
    }
    Log("browser exited");
    return true;
}

// WebView2 browser processes can hold the profile after the host exits: wait for the lockfiles to be free.
bool WaitProfileUnlocked(const std::wstring& profile, DWORD timeoutMs) {
    const std::wstring locks[] = {profile + L"\\EBWebView\\lockfile", profile + L"\\lockfile"};
    ULONGLONG deadline = GetTickCount64() + timeoutMs;
    for (;;) {
        bool busy = false;
        for (const auto& lock : locks) {
            if (!UP::PathExists(lock)) continue;
            HANDLE f = CreateFileW(lock.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (f == INVALID_HANDLE_VALUE) {
                DWORD e = GetLastError();
                if (e == ERROR_SHARING_VIOLATION || e == ERROR_LOCK_VIOLATION || e == ERROR_ACCESS_DENIED) busy = true;
            } else {
                CloseHandle(f);
            }
        }
        if (!busy) return true;
        if (GetTickCount64() >= deadline) return false;
        Sleep(500);
    }
}

bool CopyWithRetry(const std::wstring& src, const std::wstring& dst, ULONGLONG deadline) {
    for (;;) {
        if (CopyFileExW(src.c_str(), dst.c_str(), nullptr, nullptr, nullptr, COPY_FILE_FAIL_IF_EXISTS)) return true;
        DWORD e = GetLastError();
        if ((e != ERROR_SHARING_VIOLATION && e != ERROR_LOCK_VIOLATION) || GetTickCount64() >= deadline) return false;
        Sleep(250);
    }
}

// Copies a tree without following reparse points (they are skipped and logged). Never modifies the source.
bool CopyTree(const std::wstring& src, const std::wstring& dst, ULONGLONG deadline, int depth = 0) {
    if (depth > 64 || !CreateDirectoryW(dst.c_str(), nullptr)) return false;
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileExW((src + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (find == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND;
    bool ok = true;
    do {
        std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            Log("migration skipped reparse point: " + UP::Narrow(src + L"\\" + name));
            continue;
        }
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            ok = CopyTree(src + L"\\" + name, dst + L"\\" + name, deadline, depth + 1);
        else
            ok = CopyWithRetry(src + L"\\" + name, dst + L"\\" + name, deadline);
        if (!ok) Log("migration copy failed: " + UP::Narrow(src + L"\\" + name) + " (error " + std::to_string(GetLastError()) + ")");
    } while (ok && FindNextFileW(find, &fd));
    FindClose(find);
    return ok;
}

// Logs the failing migration step with its Win32 error. Always false, so it can end an `ok = step || ...` chain.
bool MigrationStepFailed(const std::string& step) {
    DWORD e = GetLastError();
    Log("migration step failed: " + step + " (error " + std::to_string(e) + ")");
    return false;
}

// One-time copy of portable <exeDir>\profile + session into <root>\data, plus the profile registry
// (profiles.txt and its recovery copies) and the other profiles' folders (profiles\, trash included).
// Existing data is never overwritten; the source is never modified.
// The caller holds the source's annotation gate EXCLUSIVELY (no writer can touch <src>\annotations meanwhile).
bool MigratePortable(const std::wstring& root, const Plan& p, const std::string& nonce, bool& migrated) {
    migrated = false;
    std::wstring data = root + L"\\data";
    if (UP::PathExists(data)) {
        Log("data already present; portable data not migrated");
        return true;
    }
    std::wstring src = Parent(p.previousExe);
    std::wstring profile = src + L"\\profile";
    if (UP::IsReparsePoint(profile)) {
        Log("portable profile is a reparse point; refusing migration");
        return false;
    }
    if (!WaitProfileUnlocked(profile, ProfileUnlockMs)) {
        Log("portable profile still locked");
        return false;
    }
    // Other profiles (src/profiles.h): <exeDir>\profiles\<id>\ each holds its own WebView2 folder and
    // data; profiles\.trash\ holds deletions inside their undo window (journals relative to the data
    // root, so they stay valid once copied). Same rules as the default profile: no reparse point,
    // every WebView2 folder released before anything is copied.
    const std::wstring profiles = src + L"\\profiles";
    if (UP::IsReparsePoint(profiles)) {
        Log("portable profiles folder is a reparse point; refusing migration");
        return false;
    }
    if (UP::IsDirectory(profiles)) {
        WIN32_FIND_DATAW pd;
        HANDLE pf = FindFirstFileExW((profiles + L"\\*").c_str(), FindExInfoBasic, &pd, FindExSearchNameMatch, nullptr, 0);
        bool locked = false, reparse = false;
        if (pf != INVALID_HANDLE_VALUE) {
            do {
                const std::wstring name = pd.cFileName;
                if (name == L"." || name == L".." || name == L".trash" || !(pd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                const std::wstring webview = profiles + L"\\" + name + L"\\profile";
                if ((pd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) || UP::IsReparsePoint(webview)) { reparse = true; break; }
                if (!WaitProfileUnlocked(webview, ProfileUnlockMs)) { locked = true; break; }
            } while (FindNextFileW(pf, &pd));
            FindClose(pf);
        }
        if (reparse) { Log("a portable profile folder is a reparse point; refusing migration"); return false; }
        if (locked) { Log("a portable profile is still locked"); return false; }
    }
    std::wstring work = root + L"\\data.migrating-" + UP::Widen(nonce);
    UP::RemoveTree(work);
    if (!CreateDirectoryW(work.c_str(), nullptr)) return MigrationStepFailed("create work dir");
    ULONGLONG deadline = GetTickCount64() + 30000;
    bool ok = true;
    if (UP::IsDirectory(profile)) ok = CopyTree(profile, work + L"\\profile", deadline) || MigrationStepFailed("copy profile");
    // Plain data files next to the profile (session, download history, the browser-wide interface language
    // preference « ui-language », src/ui-language.h, and « bookmark-imports », the stable identities of imported
    // bookmark folders, src/browser-import.h FolderMap: it travels with « bookmarks » in this same all-or-nothing
    // copy, so a renamed import folder is still recognised after the move). Transient *.tmp files are not copied.
    for (const wchar_t* name : {L"session", L"downloads", L"bookmarks", L"bookmarks.bak", L"bookmark-imports", L"ui-language"}) {
        std::wstring file = src + L"\\" + name;
        if (ok && UP::PathExists(file) && !UP::IsDirectory(file) && !UP::IsReparsePoint(file))
            ok = CopyWithRetry(file, work + L"\\" + name, deadline) || MigrationStepFailed("copy " + UP::Narrow(name));
    }
    // Profile registry and its recovery copies (« profiles.txt.damaged-<stamp> », « .damaged-last »);
    // the atomic-write temp files (« profiles.txt.tmp<pid> ») are transient and not copied.
    if (ok) {
        WIN32_FIND_DATAW rd;
        HANDLE rf = FindFirstFileExW((src + L"\\profiles.txt*").c_str(), FindExInfoBasic, &rd, FindExSearchNameMatch, nullptr, 0);
        if (rf != INVALID_HANDLE_VALUE) {
            do {
                const std::wstring name = rd.cFileName;
                if (name != L"profiles.txt" && name.rfind(L"profiles.txt.damaged-", 0) != 0) continue;
                if (rd.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) continue;
                ok = CopyWithRetry(src + L"\\" + name, work + L"\\" + name, deadline) || MigrationStepFailed("copy " + UP::Narrow(name));
            } while (ok && FindNextFileW(rf, &rd));
            FindClose(rf);
        }
    }
    if (ok && UP::IsDirectory(profiles)) ok = CopyTree(profiles, work + L"\\profiles", deadline) || MigrationStepFailed("copy profiles");
    // Annotation store (src/annotations-migration.h): copied frozen, recorded « copied »; « committed » only after ack.
    const std::wstring annotations = src + L"\\annotations";
    if (ok && UP::IsReparsePoint(annotations)) ok = MigrationStepFailed("annotations store is a reparse point");
    if (ok && UP::IsDirectory(annotations))
        ok = CopyTree(annotations, work + L"\\annotations", GetTickCount64() + 60000) || MigrationStepFailed("copy annotations");
    if (ok) {
        AM::Record record;
        record.state = AM::State::Copied;
        record.from = UP::Narrow(src);
        record.nonce = nonce;
        ok = UP::AtomicWriteFile(work + L"\\" + AM::RecordName, AM::Format(record)) || MigrationStepFailed("write annotations record");
    }
    if (ok) {  // source pointer before anything can be committed (freezes nothing by itself)
        ok = WriteSourcePointer(root, src) || MigrationStepFailed("write annotations source pointer");
    }
    if (ok) ok = UP::AtomicWriteFile(work + L"\\migrated-from.txt", UP::Narrow(src) + "\n") || MigrationStepFailed("write migrated-from.txt");
    int attempts = 0;
    if (ok) ok = UP::MoveDirectory(work, data, TransientRenameMs, &attempts) || MigrationStepFailed("publish data");
    if (ok && attempts > 1) Log("portable data published after " + std::to_string(attempts) + " rename attempts (file briefly held)");
    if (!ok) {
        UP::RemoveTree(work);
        Log("migration failed; source untouched");
        return false;
    }
    Log("portable data migrated from " + UP::Narrow(src));
    migrated = true;
    return true;
}

void WriteResult(const std::wstring& updates, const std::string& status, const std::string& version, const std::string& reason) {
    UP::AtomicWriteFile(updates + L"\\last-result", "browser-result 1\nstatus " + status + "\nversion " + version +
                                                       "\nreason " + reason + "\nreported " +
                                                       (status == "failed" ? "0" : "1") + "\n" +
                                                       (g_nonce.empty() ? std::string() : "nonce " + g_nonce + "\n"));
}

void Relaunch(const std::wstring& exe) {
    if (!ValidExeAt(exe)) {
        Log("previous exe missing, not relaunched");
        return;
    }
    PROCESS_INFORMATION pi{};
    if (Launch(exe, {}, pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        Log("previous version relaunched");
    }
}

// Removes versions other than keep1/keep2 only if the directory can be renamed (i.e. nothing inside is in use).
void Prune(const std::wstring& root, const std::string& keep1, const std::string& keep2, const std::string& nonce) {
    std::wstring versions = root + L"\\versions";
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileExW((versions + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (find == INVALID_HANDLE_VALUE) return;
    std::vector<std::wstring> victims;
    do {
        std::string name = UP::Narrow(fd.cFileName);
        UP::Version v;
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && UP::ParseVersion(name, v) && name != keep1 && name != keep2)
            victims.push_back(fd.cFileName);
    } while (FindNextFileW(find, &fd));
    FindClose(find);
    int i = 0;
    for (const auto& name : victims) {
        std::wstring trash = versions + L"\\.trash-" + UP::Widen(nonce) + L"-" + std::to_wstring(i++);
        if (MoveFileExW((versions + L"\\" + name).c_str(), trash.c_str(), 0)) UP::RemoveTree(trash);
    }
}

// Durable commit of the annotation migration: the record first (lets a portable bridge redirect), then the mirror in
// the source folder (a portable browser still running there stops writing). Only after the new version's ack.
// The source pointer (<src>\annotations.migrated -> root). Written during the copy, before any commit can exist; the
// source store is frozen and a bridge redirects only when it AND a committed record agree (src/annotations.cpp
// MovedToInstalled), so the commit below is one write that moves both sides at once.
bool WriteSourcePointer(const std::wstring& root, const std::wstring& src) {
    if (!UP::IsDirectory(src) || UP::IsReparsePoint(src)) return false;
    const std::wstring pointer = src + L"\\annotations" + AM::PointerSuffix;
    std::string text;
    if (UP::ReadSmallFile(pointer, 8192, text) && AM::ParsePointer(text) == UP::Narrow(root)) return true;
    return !Fault("pointer-write") && UP::AtomicWriteFile(pointer, AM::FormatPointer(UP::Narrow(root)));
}

// Durable commit: caller holds the source gate EXCLUSIVELY and has checked that the installed version is usable.
bool CommitAnnotations(const std::wstring& root, const std::wstring& src, const std::string& nonce) {
    if (!WriteSourcePointer(root, src)) return false;  // never a committed record without its pointer
    AM::Record record;
    record.state = AM::State::Committed;
    record.from = UP::Narrow(src);
    record.nonce = nonce;
    return !Fault("commit-write") && UP::AtomicWriteFile(root + L"\\data\\" + AM::RecordName, AM::Format(record));
}

// The installed side is a durable, usable outcome: valid launchable `current` whose install did not fail.
bool InstallUsable(const std::wstring& root) {
    const std::string current = LaunchableCurrent(root);
    if (current.empty()) return false;
    std::string result;
    if (UP::ReadSmallFile(root + L"\\updates\\last-result", 4096, result)) {
        const auto lines = Lines(result);
        if (!lines.empty() && lines[0] == "browser-result 1" && Field(lines, "status") == "failed" && Field(lines, "version") == current)
            return false;
    }
    return true;
}

// Bounded recovery (every launcher start, install start and after an ack): a migration whose helper died before the
// commit (record « copied ») is finished from the source ONLY when the installed version is usable (InstallUsable);
// otherwise nothing changes and the source stays authoritative and writable (the pointer alone freezes nothing). The
// installed copy refused mutations meanwhile, so it is replaced by a fresh copy taken under the HELD exclusive gate,
// then committed. A committed record whose source pointer is missing gets its pointer back. A migration made before
// the record existed is committed when nothing was moved yet, else marked « skipped » (no redirect).
void RecoverAnnotationsMigration(const std::wstring& root, const std::string& tag) {
    const std::wstring data = root + L"\\data", recordPath = data + L"\\" + AM::RecordName;
    std::string from, text;
    if (UP::IsReparsePoint(data) || !UP::ReadSmallFile(data + L"\\migrated-from.txt", 4096, from)) return;
    while (!from.empty() && (from.back() == '\n' || from.back() == '\r')) from.pop_back();
    const AM::Record record = UP::ReadSmallFile(recordPath, 8192, text) ? AM::Parse(text) : AM::Record{};
    if (record.state == AM::State::Skipped || from.empty()) return;
    const std::wstring src = UP::Widen(from), store = src + L"\\annotations", installed = data + L"\\annotations";
    if (record.state == AM::State::Committed) {  // pointer repair only (never a state change)
        AM::Gate gate(AM::GatePath(store), true);
        if (gate.Held() && WriteSourcePointer(root, src)) return;
        return;  // next start
    }
    if (record.state == AM::State::None && UP::PathExists(installed)) {
        AM::Record skipped;
        skipped.state = AM::State::Skipped;
        skipped.from = from;
        skipped.nonce = tag;
        UP::AtomicWriteFile(recordPath, AM::Format(skipped));
        return;
    }
    if (!InstallUsable(root)) return;  // no usable installed outcome: the source stays authoritative and writable
    AM::Gate gate(AM::GatePath(store), true);
    if (!gate.Held() || UP::IsReparsePoint(store) || UP::IsReparsePoint(installed)) return;  // next start
    if (UP::IsDirectory(store)) {
        const std::wstring fresh = data + L"\\annotations.recover-" + UP::Widen(tag), old = data + L"\\annotations.old-" + UP::Widen(tag);
        UP::RemoveTree(fresh);
        if (!CopyTree(store, fresh, GetTickCount64() + 60000)) {
            UP::RemoveTree(fresh);
            return;
        }
        if (UP::PathExists(installed) && !MoveFileExW(installed.c_str(), old.c_str(), 0)) {
            UP::RemoveTree(fresh);
            return;
        }
        if (!MoveFileExW(fresh.c_str(), installed.c_str(), 0)) {
            if (UP::PathExists(old)) MoveFileExW(old.c_str(), installed.c_str(), 0);
            UP::RemoveTree(fresh);
            return;
        }
        UP::RemoveTree(old);
    }
    if (CommitAnnotations(root, src, tag)) Log("annotations migration recovered and committed from " + from);
}

bool InstallLauncher(const std::wstring& root, const std::wstring& target, const std::string& nonce) {
    const std::wstring launcher = root + L"\\browser.exe", temp = root + L"\\browser.exe.new-" + UP::Widen(nonce);
    const std::wstring helper = target + L"\\update-helper.exe";
    if (UP::PathExists(launcher) && SameBytes(launcher, helper)) return true;
    if (!CopyFileW(helper.c_str(), temp.c_str(), FALSE)) return false;
    if (Fault("launcher-busy") ||
        !MoveFileExW(temp.c_str(), launcher.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temp.c_str());
        return false;
    }
    return SameBytes(launcher, helper);
}

int RunInstall(const std::string& nonce) {
    std::wstring self = SelfPath(), helperDir = Parent(self), updates = Parent(helperDir), root = Parent(updates);
    if (Lower(Leaf(helperDir)) != L"helper" || Lower(Leaf(updates)) != L"updates" || root.size() < 4) return 10;
    if (UP::IsReparsePoint(root) || UP::IsReparsePoint(updates) || UP::IsReparsePoint(helperDir)) return 11;
    g_log = updates + L"\\helper.log";
    g_nonce = nonce;
    Log("install " + nonce + " start");
    std::wstring planPath = updates + L"\\install-" + UP::Widen(nonce);
    std::string text, error;
    Plan p;
    if (!UP::ReadSmallFile(planPath, 16384, text) || !ParsePlan(text, root, nonce, p, error)) {
        Log("plan rejected: " + error);
        return 12;
    }
    DeleteFileW(planPath.c_str());  // single use
    std::wstring stage = updates + L"\\staging\\" + UP::Widen(p.version);
    std::wstring versions = root + L"\\versions", target = versions + L"\\" + UP::Widen(p.version);
    std::wstring targetExe = target + L"\\browser.exe";

    if (p.setup) Log("setup install, previous-kind " + p.previousKind + (p.launch ? "" : ", no launch"));
    if (!p.setup && !WaitForBrowser(p)) {
        WriteResult(updates, "failed", p.version, "browser-did-not-exit");
        return 20;
    }
    // One install/removal transaction per root (src/install-transaction.h), held until this helper exits: the setup
    // hands over its own lock; an engine-started helper takes it (a running setup/uninstall makes this update fail
    // cleanly and the previous browser restart).
    InstallTxn::Lock txn;
    if (p.setup ? !txn.Adopt(reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(p.lockHandle)), root)
                : !txn.Acquire(root, LockWaitMs())) {
        Log(std::string("install lock ") + (p.setup ? "handle invalid" : txn.Busy() ? "busy" : "unusable"));
        WriteResult(updates, "failed", p.version, txn.Busy() ? "install-busy" : "install-lock-unusable");
        if (!p.setup) Relaunch(p.previousExe);
        return 13;
    }
    if (Fault("pause-after-lock")) Sleep(4000);
    if (InstallTxn::RecoverRepair(root) == InstallTxn::RepairOutcome::Kept) {
        Log("an interrupted repair could not be finished; nothing changed");
        WriteResult(updates, "failed", p.version, "repair-recovery-pending");
        if (!p.setup) Relaunch(p.previousExe);
        return 14;
    }
    RecoverAnnotationsMigration(root, nonce + "-pre");
    // Repair (setup only): a durable record is written BEFORE the live versions\<v> is renamed aside. Every failure
    // rolls back (replacement out, original back); only a deliberate no-launch success or the new process's ack
    // commits (deletes the original). A kill anywhere leaves the record for InstallTxn::RecoverRepair.
    const InstallTxn::Repair repair{nonce, p.version};
    bool repairOpen = false;
    auto rollbackRepair = [&] {
        if (!repairOpen) return;
        if (InstallTxn::RollbackRepair(root, repair) == InstallTxn::RepairOutcome::Kept)
            Log("repair: original copy kept aside (restore failed); recovered at the next start");
        else
            Log("repair: original copy restored");
        repairOpen = false;
    };
    auto commitRepair = [&] {
        if (!repairOpen) return;
        if (InstallTxn::CommitRepair(root, repair) == InstallTxn::RepairOutcome::Kept) Log("repair: original copy still in use; removed later");
        repairOpen = false;
    };
    auto abortBeforeSwitch = [&](const std::string& reason, int code) {
        Log("aborted: " + reason);
        rollbackRepair();
        WriteResult(updates, "failed", p.version, reason);
        if (!p.setup) Relaunch(p.previousExe);  // a setup reports the failure itself
        return code;
    };
    if (!UP::VerifyStagedTree(stage, p.version, error)) return abortBeforeSwitch("stage-invalid " + error, 21);
    if (!CreateDirectoryW(versions.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
        return abortBeforeSwitch("versions-dir", 22);
    if (UP::IsReparsePoint(versions)) return abortBeforeSwitch("versions-reparse", 22);
    std::string oldCurrent;
    bool hadCurrent = UP::ReadSmallFile(root + L"\\current", 64, oldCurrent);
    while (!oldCurrent.empty() && (oldCurrent.back() == '\n' || oldCurrent.back() == '\r')) oldCurrent.pop_back();
    if (p.previousKind == "repair") {
        if (!hadCurrent || oldCurrent != p.version) return abortBeforeSwitch("repair-not-current", 23);
        if (UP::PathExists(target)) {
            if (!InstallTxn::WriteRepair(root, repair)) return abortBeforeSwitch("repair-record", 23);
            repairOpen = true;
            if (Fault("repair-aside") || !UP::MoveDirectory(target, InstallTxn::RepairAside(root, nonce), TransientRenameMs))
                return abortBeforeSwitch("target-in-use", 23);
            Fault("kill-after-repair-aside");
            if (Fault("pause-after-repair-aside")) Sleep(4000);
        }
    } else if (UP::PathExists(target)) {
        if (hadCurrent && oldCurrent == p.version) return abortBeforeSwitch("target-is-current", 23);
        std::wstring trash = versions + L"\\.trash-" + UP::Widen(nonce);
        if (!UP::MoveDirectory(target, trash, TransientRenameMs)) return abortBeforeSwitch("target-in-use", 23);
        UP::RemoveTree(trash);
    }
    if (Fault("move") || !UP::MoveDirectory(stage, target, TransientRenameMs)) return abortBeforeSwitch("move-failed", 24);
    Fault("kill-after-repair-move");
    if (!UP::VerifyStagedTree(target, p.version, error)) {
        UP::MoveDirectory(target, stage, TransientRenameMs);
        return abortBeforeSwitch("installed-tree-invalid " + error, 25);
    }
    // Data: never start the new version on a live or half-copied profile.
    bool migrated = false;
    // Held from before the copy until this function returns (ack + commit, or rollback): a portable bridge or
    // browser writing the source store waits or gets Busy; if this helper dies, the handle goes with it and the
    // record stays « copied » (recovered later, never redirected to).
    AM::Gate annotationsGate;
    const std::wstring portableDir = Parent(p.previousExe);
    if (p.previousKind == "portable" && !UP::PathExists(root + L"\\data")) {
        const AM::GateState g = annotationsGate.Open(AM::GatePath(portableDir + L"\\annotations"), true, 30000);
        if (g != AM::GateState::Held)
            return abortBeforeSwitch(g == AM::GateState::Busy ? "annotations-busy" : "annotations-gate-unusable", 26);
    }
    if (p.previousKind == "portable") {
        if (!MigratePortable(root, p, nonce, migrated)) return abortBeforeSwitch("migration-failed", 26);
        Fault("kill-after-migration");
        if (Fault("pause-after-migration")) Sleep(4000);  // gate held: agents' requests must get Busy
    } else if (!WaitProfileUnlocked(root + L"\\data\\profile", ProfileUnlockMs)) {
        return abortBeforeSwitch("profile-locked", 26);
    }
    // Stable bridge for agents (<root>\bin, see ReconcileBridge), published BEFORE the new version can start
    // (src/agent-connect.cpp never falls back to a versioned copy) and kept locked until `current` names the
    // version it matches, so a concurrent launcher can never revert it in between.
    const std::wstring bin = root + L"\\bin";
    const std::string previousBridge = hadCurrent ? oldCurrent : "";
    const bool bridgeInvolved = UP::PathExists(target + L"\\assets\\bin\\" + BridgeName) || UP::PathExists(bin);
    auto restoreBridge = [&] {
        if (!bridgeInvolved) return;
        BinLock lock(bin, 10000);
        std::string e;
        if (!lock.Held() || !ReconcileBridge(root, previousBridge, nonce + "-restore", e))
            Log("bridge restore failed (" + (lock.Held() ? e : std::string("bin locked")) + "); repaired by the next launcher start");
    };
    {
        std::unique_ptr<BinLock> lock;
        if (bridgeInvolved) {
            if (!CreateDirectoryW(bin.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
                return abortBeforeSwitch("bridge-publish bin create failed", 32);
            if (UP::IsReparsePoint(bin)) return abortBeforeSwitch("bridge-publish bin is not a plain directory", 32);
            lock = std::make_unique<BinLock>(bin, 10000);
            if (!lock->Held()) return abortBeforeSwitch("bridge-publish bin locked", 32);
            // The stable launcher is what repairs an interrupted publication (RunLauncher). An install from a version
            // whose launcher predates the bridge would leave a launcher that cannot: it is replaced FIRST, and
            // strictly (a busy launcher aborts this install, retried later). It stays compatible with the previous
            // `current` if this install aborts.
            if (!InstallLauncher(root, target, nonce)) {
                lock.reset();
                return abortBeforeSwitch("bridge-publish launcher busy", 32);
            }
            Fault("kill-after-launcher");
            std::string e;
            if (!ReconcileBridge(root, p.version, nonce, e)) {
                lock.reset();
                restoreBridge();
                return abortBeforeSwitch("bridge-publish " + e, 32);
            }
            Log("bridge published to bin for " + p.version);
            Fault("kill-after-publish");
            if (Fault("pause-after-publish")) Sleep(4000);  // lock held: a concurrent launcher must wait
        }
        // Switch.
        if (Fault("current-write") || !UP::AtomicWriteFile(root + L"\\current", p.version + "\n")) {
            lock.reset();
            restoreBridge();
            return abortBeforeSwitch("current-write", 27);
        }
    }
    Fault("kill-after-switch");
    if (p.previousKind == "installed" || p.previousKind == "portable")  // a setup first install / repair keeps it
        UP::AtomicWriteFile(root + L"\\previous",
                            p.previousKind == "installed" ? "installed " + p.previousVersion + "\n"
                                                          : "portable " + UP::Narrow(p.previousExe) + "\n");
    auto restore = [&] {
        if (hadCurrent)
            UP::AtomicWriteFile(root + L"\\current", oldCurrent + "\n");
        else
            DeleteFileW((root + L"\\current").c_str());
        restoreBridge();  // after `current`: a launcher reconciling in between follows the restored current
    };
    // Stable launcher (best effort; a launcher being executed right now keeps the old copy).
    std::wstring launcherTemp = root + L"\\browser.exe.new-" + UP::Widen(nonce);
    if (CopyFileW((target + L"\\update-helper.exe").c_str(), launcherTemp.c_str(), FALSE) &&
        !MoveFileExW(launcherTemp.c_str(), (root + L"\\browser.exe").c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        DeleteFileW(launcherTemp.c_str());
    // Stable app icon file (best effort) for shortcuts whose IconLocation is <root>\browser.ico, then tell the shell
    // that exactly these two items changed so it re-reads their icons (no global icon-cache purge, no Explorer restart).
    std::wstring iconTemp = root + L"\\browser.ico.new-" + UP::Widen(nonce);
    if (CopyFileW((target + L"\\assets\\browser.ico").c_str(), iconTemp.c_str(), FALSE) &&
        !MoveFileExW(iconTemp.c_str(), (root + L"\\browser.ico").c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        DeleteFileW(iconTemp.c_str());
    for (const wchar_t* name : {L"\\browser.exe", L"\\browser.ico"})
        SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW | SHCNF_FLUSHNOWAIT, (root + name).c_str(), nullptr);
    // Versions kept by Prune: current + the previous installed version (a repair keeps the one recorded before).
    std::string keepPrevious = p.previousKind == "installed" ? p.previousVersion : "";
    if (p.previousKind == "repair") {
        std::string prev;
        if (UP::ReadSmallFile(root + L"\\previous", 4096, prev) && prev.rfind("installed ", 0) == 0) {
            keepPrevious = prev.substr(10);
            while (!keepPrevious.empty() && (keepPrevious.back() == '\n' || keepPrevious.back() == '\r')) keepPrevious.pop_back();
        }
    }
    if (p.setup && !p.launch) {  // installed without starting it (setup option); the setup integrates and reports
        WriteResult(updates, "installed", p.version, "setup-no-launch");
        commitRepair();
        RecoverAnnotationsMigration(root, nonce + "-post");
        Prune(root, p.version, keepPrevious, nonce);
        RefreshIntegration(root, p.version);
        Log("installed " + p.version + " (not launched)");
        return 0;
    }
    std::wstring pending = updates + L"\\pending", ack = updates + L"\\ack-" + UP::Widen(nonce);
    DeleteFileW(ack.c_str());
    if (!UP::AtomicWriteFile(pending, "browser-pending 1\nnonce " + nonce + "\nversion " + p.version + "\n")) {
        restore();
        return abortBeforeSwitch("pending-write", 28);
    }
    PROCESS_INFORMATION pi{};
    if (!Launch(targetExe, {}, pi)) {
        DeleteFileW(pending.c_str());
        restore();
        return abortBeforeSwitch("launch-failed", 29);
    }
    CloseHandle(pi.hThread);
    Log("new version launched pid " + std::to_string(pi.dwProcessId));
    if (Fault("pause-in-ack-wait")) Sleep(4000);
    ULONGLONG deadline = GetTickCount64() + AckWaitMs;
    bool acked = false, exited = false;
    for (;;) {
        std::string ackText;
        if (UP::ReadSmallFile(ack, 4096, ackText)) {
            auto lines = Lines(ackText);
            if (!lines.empty() && lines[0] == "browser-ack 1" && Field(lines, "version") == p.version &&
                Field(lines, "pid") == std::to_string(pi.dwProcessId)) {
                acked = true;
                break;
            }
        }
        if (WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0) {
            // Re-check once: the ack may have been written just before a fast exit.
            if (UP::ReadSmallFile(ack, 4096, ackText) && ackText.find("pid " + std::to_string(pi.dwProcessId) + "\n") != std::string::npos &&
                ackText.find("version " + p.version + "\n") != std::string::npos) {
                acked = true;
            } else {
                exited = true;
            }
            break;
        }
        if (GetTickCount64() >= deadline) break;
        Sleep(200);
    }
    CloseHandle(pi.hProcess);
    if (acked) {
        DeleteFileW(pending.c_str());
        DeleteFileW(ack.c_str());
        // The ack is recorded first: it is what makes the installed side usable (InstallUsable reads last-result).
        WriteResult(updates, "installed", p.version, "ack");
        if (migrated) {
            Fault("kill-before-commit");
            if (!CommitAnnotations(root, portableDir, nonce)) Log("annotations commit failed; recovered at the next launcher start");
        } else {
            RecoverAnnotationsMigration(root, nonce + "-post");  // an earlier interrupted migration, now that the install is usable
        }
        commitRepair();
        Prune(root, p.version, keepPrevious, nonce);
        RefreshIntegration(root, p.version);
        Log("installed " + p.version);
        return 0;
    }
    if (exited) {
        Log("new version exited without startup ack; rolling back");
        DeleteFileW(pending.c_str());
        restore();
        if (repairOpen) {
            rollbackRepair();  // repair: the original copy of the same version returns
        } else {
            std::wstring failed = versions + L"\\.failed-" + UP::Widen(nonce);
            if (UP::MoveDirectory(target, failed, TransientRenameMs)) UP::RemoveTree(failed);
        }
        if (migrated && WaitProfileUnlocked(root + L"\\data\\profile", ProfileUnlockMs)) {
            // The copy made by this run is discarded so a later retry re-migrates the (still authoritative) portable data.
            std::wstring dropped = root + L"\\data.rolledback-" + UP::Widen(nonce);
            if (UP::MoveDirectory(root + L"\\data", dropped, TransientRenameMs)) UP::RemoveTree(dropped);
        }
        WriteResult(updates, "failed", p.version, "no-startup-ack");
        if (!p.setup) Relaunch(p.previousExe);
        return 30;
    }
    // Still running but silent: do not kill it. The browser acknowledges itself later via the pending record.
    if (repairOpen) Log("repair left open: committed on the new version's ack, else rolled back by the next recovery");
    WriteResult(updates, "unconfirmed", p.version, "ack-timeout");
    RefreshIntegration(root, p.version);
    Log("ack timeout; new version left running");
    return 31;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return 1;
    int result;
    if (argc == 3 && std::wstring(argv[1]) == L"--install") {
        std::wstring nonce = argv[2];
        bool ok = nonce.size() == 32 && nonce.find_first_not_of(L"0123456789abcdef") == std::wstring::npos;
        result = ok ? RunInstall(UP::Narrow(nonce)) : 9;
    } else {
        result = RunLauncher(argc, argv);
    }
    LocalFree(argv);
    return result;
}
