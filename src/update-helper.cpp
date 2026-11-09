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

#include <cstdio>
#include <string>
#include <vector>

#include "update-package.h"

namespace UP = UpdatePackage;

namespace {

constexpr DWORD WaitBrowserMs = 10 * 60 * 1000;
constexpr DWORD ProfileUnlockMs = 60 * 1000;
constexpr DWORD AckWaitMs = 120 * 1000;
// Directory renames retried while a scanner/indexer still holds a file just written under them (UP::MoveDirectory).
constexpr DWORD TransientRenameMs = 15 * 1000;

std::wstring g_log;

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

// ---------------------------------------------------------------- launcher

int RunLauncher(int argc, wchar_t** argv) {
    std::wstring root = Parent(SelfPath());
    std::string current;
    UP::Version v;
    if (UP::ReadSmallFile(root + L"\\current", 64, current)) {
        while (!current.empty() && (current.back() == '\n' || current.back() == '\r')) current.pop_back();
        if (UP::ParseVersion(current, v)) {
            std::wstring exe = root + L"\\versions\\" + UP::Widen(current) + L"\\browser.exe";
            if (ValidExeAt(exe) && !UP::IsReparsePoint(root + L"\\versions")) {
                std::vector<std::wstring> args(argv + 1, argv + argc);
                PROCESS_INFORMATION pi{};
                if (Launch(exe, args, pi)) {
                    AllowSetForegroundWindow(pi.dwProcessId);
                    CloseHandle(pi.hThread);
                    CloseHandle(pi.hProcess);
                    return 0;
                }
            }
        }
    }
    MessageBoxW(nullptr, L"Installation introuvable ou endommagée.", L"browser", MB_OK | MB_ICONERROR);
    return 2;
}

// ---------------------------------------------------------------- install

struct Plan {
    std::string nonce, version, previousVersion, previousKind;
    std::wstring previousExe;
    DWORD pid = 0;
    std::uint64_t created = 0;
};

bool ParsePlan(const std::string& text, const std::wstring& root, const std::string& nonce, Plan& p, std::string& error) {
    auto lines = Lines(text);
    if (lines.empty() || lines[0] != "browser-install 1") return error = "plan header", false;
    p.nonce = Field(lines, "nonce");
    p.version = Field(lines, "version");
    p.previousVersion = Field(lines, "previous-version");
    p.previousKind = Field(lines, "previous-kind");
    p.previousExe = FullPath(UP::Widen(Field(lines, "previous-exe")));
    UP::Version v, pv;
    if (p.nonce != nonce) return error = "plan nonce", false;
    if (!UP::ParseVersion(p.version, v) || !UP::ParseVersion(p.previousVersion, pv) || UP::CompareVersion(v, pv) <= 0)
        return error = "plan version", false;
    unsigned long long pid = std::strtoull(Field(lines, "pid").c_str(), nullptr, 10);
    p.created = std::strtoull(Field(lines, "created").c_str(), nullptr, 10);
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

// One-time copy of portable <exeDir>\profile + session into <root>\data. Existing data is never overwritten.
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
    std::wstring work = root + L"\\data.migrating-" + UP::Widen(nonce);
    UP::RemoveTree(work);
    if (!CreateDirectoryW(work.c_str(), nullptr)) return MigrationStepFailed("create work dir");
    ULONGLONG deadline = GetTickCount64() + 30000;
    bool ok = true;
    if (UP::IsDirectory(profile)) ok = CopyTree(profile, work + L"\\profile", deadline) || MigrationStepFailed("copy profile");
    // Plain data files next to the profile (session, download history). Transient *.tmp files are not copied.
    for (const wchar_t* name : {L"session", L"downloads", L"bookmarks", L"bookmarks.bak"}) {
        std::wstring file = src + L"\\" + name;
        if (ok && UP::PathExists(file) && !UP::IsDirectory(file) && !UP::IsReparsePoint(file))
            ok = CopyWithRetry(file, work + L"\\" + name, deadline) || MigrationStepFailed("copy " + UP::Narrow(name));
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
                                                       (status == "failed" ? "0" : "1") + "\n");
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

int RunInstall(const std::string& nonce) {
    std::wstring self = SelfPath(), helperDir = Parent(self), updates = Parent(helperDir), root = Parent(updates);
    if (Lower(Leaf(helperDir)) != L"helper" || Lower(Leaf(updates)) != L"updates" || root.size() < 4) return 10;
    if (UP::IsReparsePoint(root) || UP::IsReparsePoint(updates) || UP::IsReparsePoint(helperDir)) return 11;
    g_log = updates + L"\\helper.log";
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

    if (!WaitForBrowser(p)) {
        WriteResult(updates, "failed", p.version, "browser-did-not-exit");
        return 20;
    }
    auto abortBeforeSwitch = [&](const std::string& reason, int code) {
        Log("aborted: " + reason);
        WriteResult(updates, "failed", p.version, reason);
        Relaunch(p.previousExe);
        return code;
    };
    if (!UP::VerifyStagedTree(stage, p.version, error)) return abortBeforeSwitch("stage-invalid " + error, 21);
    if (!CreateDirectoryW(versions.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
        return abortBeforeSwitch("versions-dir", 22);
    if (UP::IsReparsePoint(versions)) return abortBeforeSwitch("versions-reparse", 22);
    std::string oldCurrent;
    bool hadCurrent = UP::ReadSmallFile(root + L"\\current", 64, oldCurrent);
    while (!oldCurrent.empty() && (oldCurrent.back() == '\n' || oldCurrent.back() == '\r')) oldCurrent.pop_back();
    if (UP::PathExists(target)) {
        if (hadCurrent && oldCurrent == p.version) return abortBeforeSwitch("target-is-current", 23);
        std::wstring trash = versions + L"\\.trash-" + UP::Widen(nonce);
        if (!UP::MoveDirectory(target, trash, TransientRenameMs)) return abortBeforeSwitch("target-in-use", 23);
        UP::RemoveTree(trash);
    }
    if (!UP::MoveDirectory(stage, target, TransientRenameMs)) return abortBeforeSwitch("move-failed", 24);
    if (!UP::VerifyStagedTree(target, p.version, error)) {
        UP::MoveDirectory(target, stage, TransientRenameMs);
        return abortBeforeSwitch("installed-tree-invalid " + error, 25);
    }
    // Data: never start the new version on a live or half-copied profile.
    bool migrated = false;
    if (p.previousKind == "portable") {
        if (!MigratePortable(root, p, nonce, migrated)) return abortBeforeSwitch("migration-failed", 26);
    } else if (!WaitProfileUnlocked(root + L"\\data\\profile", ProfileUnlockMs)) {
        return abortBeforeSwitch("profile-locked", 26);
    }
    // Switch.
    if (!UP::AtomicWriteFile(root + L"\\current", p.version + "\n")) return abortBeforeSwitch("current-write", 27);
    UP::AtomicWriteFile(root + L"\\previous",
                        p.previousKind == "installed" ? "installed " + p.previousVersion + "\n"
                                                      : "portable " + UP::Narrow(p.previousExe) + "\n");
    auto restore = [&] {
        if (hadCurrent)
            UP::AtomicWriteFile(root + L"\\current", oldCurrent + "\n");
        else
            DeleteFileW((root + L"\\current").c_str());
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
        WriteResult(updates, "installed", p.version, "ack");
        Prune(root, p.version, p.previousKind == "installed" ? p.previousVersion : "", nonce);
        Log("installed " + p.version);
        return 0;
    }
    if (exited) {
        Log("new version exited without startup ack; rolling back");
        DeleteFileW(pending.c_str());
        restore();
        std::wstring failed = versions + L"\\.failed-" + UP::Widen(nonce);
        if (UP::MoveDirectory(target, failed, TransientRenameMs)) UP::RemoveTree(failed);
        if (migrated && WaitProfileUnlocked(root + L"\\data\\profile", ProfileUnlockMs)) {
            // The copy made by this run is discarded so a later retry re-migrates the (still authoritative) portable data.
            std::wstring dropped = root + L"\\data.rolledback-" + UP::Widen(nonce);
            if (UP::MoveDirectory(root + L"\\data", dropped, TransientRenameMs)) UP::RemoveTree(dropped);
        }
        WriteResult(updates, "failed", p.version, "no-startup-ack");
        Relaunch(p.previousExe);
        return 30;
    }
    // Still running but silent: do not kill it. The browser acknowledges itself later via the pending record.
    WriteResult(updates, "unconfirmed", p.version, "ack-timeout");
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
