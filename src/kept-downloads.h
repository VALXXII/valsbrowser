#pragma once
// « Conserver » (src/main.cpp KeepHeldDownload): our own name for the kept bytes lives in <dataDir>\kept-downloads as
// "<pid>-<download id>-<file name>", with a sidecar "<that name>.path" holding the download's final path (UTF-8). In a
// normal run both go when the keep is settled. A browser killed in the middle leaves them, and our name may then be the
// only copy of the user's file: at the next start, Sweep claims every leftover of a process that is gone, gives the file
// back where it is missing and hands it to the browser to watch (AdoptedLook). Nothing is ever deleted before its bytes
// are at a user-visible place and safe from the engine's own clean-up; a live window's keep is never touched.
// No HWND, no WebView2, no global of the browser: tests/downloads/kept_downloads_test.cpp runs it windowless.
#include <windows.h>
#include <cwchar>
#include <cstdint>
#include <cwctype>
#include <functional>
#include <string>
#include <vector>

namespace KeptDownloads {
inline std::wstring SidecarOf(const std::wstring& backup) { return backup + L".path"; }
inline bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
inline std::wstring FolderOf(const std::wstring& p) { const auto s = p.find_last_of(L"\\/"); return s == std::wstring::npos ? L"" : p.substr(0, s); }
inline std::wstring NameOf(const std::wstring& p) { const auto s = p.find_last_of(L"\\/"); return s == std::wstring::npos ? p : p.substr(s + 1); }
// "<pid>-<id>-<file>": the pid and the file name; false for any other name (never ours to touch).
inline bool ParseName(const std::wstring& n, unsigned long& pid, std::wstring& file) {
    size_t i = 0; pid = 0; unsigned long id = 0;
    while (i < n.size() && iswdigit(n[i])) pid = pid * 10 + (n[i++] - L'0');
    if (!i || i >= n.size() || n[i++] != L'-') return false;
    const size_t j = i; while (i < n.size() && iswdigit(n[i])) id = id * 10 + (n[i++] - L'0');
    if (i == j || i >= n.size() || n[i++] != L'-' || i >= n.size()) return false;
    file = n.substr(i); return file.size() < 5 || file.compare(file.size() - 5, 5, L".path") != 0;
}
inline bool WriteSidecar(const std::wstring& backup, const std::wstring& finalPath) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, finalPath.c_str(), (int)finalPath.size(), nullptr, 0, nullptr, nullptr);
    std::string utf8(n, '\0'); WideCharToMultiByte(CP_UTF8, 0, finalPath.c_str(), (int)finalPath.size(), utf8.data(), n, nullptr, nullptr);
    HANDLE h = CreateFileW(SidecarOf(backup).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0; const bool ok = WriteFile(h, utf8.data(), (DWORD)utf8.size(), &written, nullptr) && written == utf8.size(); CloseHandle(h); return ok;
}
inline bool ReadSidecar(const std::wstring& backup, std::wstring& finalPath) {
    HANDLE h = CreateFileW(SidecarOf(backup).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    char buf[4096]; DWORD got = 0; const bool ok = ReadFile(h, buf, sizeof buf, &got, nullptr) && got > 0 && got < sizeof buf; CloseHandle(h);
    if (!ok) return false;
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, buf, (int)got, nullptr, 0); if (n <= 0) return false;
    finalPath.assign(n, L'\0'); MultiByteToWideChar(CP_UTF8, 0, buf, (int)got, finalPath.data(), n);
    return finalPath.size() > 3 && finalPath[1] == L':';   // an absolute path, nothing else
}
// Our name and its sidecar go (the bytes are known to be at a user-visible place).
inline void Drop(const std::wstring& backup) { DeleteFileW(backup.c_str()); DeleteFileW(SidecarOf(backup).c_str()); }
inline bool Info(const std::wstring& p, BY_HANDLE_FILE_INFORMATION& i) {
    HANDLE h = CreateFileW(p.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    const bool ok = GetFileInformationByHandle(h, &i) != FALSE; CloseHandle(h); return ok;
}
inline long long SizeOf(const std::wstring& p) { WIN32_FILE_ATTRIBUTE_DATA d{}; return GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &d) ? ((long long)d.nFileSizeHigh << 32 | d.nFileSizeLow) : -1; }
inline bool SameFileId(const std::wstring& a, const std::wstring& b) {
    BY_HANDLE_FILE_INFORMATION x{}, y{}; if (!Info(a, x) || !Info(b, y)) return false;
    return x.dwVolumeSerialNumber == y.dwVolumeSerialNumber && x.nFileIndexHigh == y.nFileIndexHigh && x.nFileIndexLow == y.nFileIndexLow;
}
// The file at `path` holds our bytes: the same file (a hard link), or a file of the same, non-zero size (a copy).
inline bool HoldsOurBytes(const std::wstring& path, const std::wstring& backup) {
    BY_HANDLE_FILE_INFORMATION x{}, y{}; if (!Info(path, x) || !Info(backup, y)) return false;
    const bool same = x.dwVolumeSerialNumber == y.dwVolumeSerialNumber && x.nFileIndexHigh == y.nFileIndexHigh && x.nFileIndexLow == y.nFileIndexLow;
    return same || (x.nFileSizeHigh == y.nFileSizeHigh && x.nFileSizeLow == y.nFileSizeLow && (x.nFileSizeHigh || x.nFileSizeLow));
}
// The next free name in `folder` for `name`: "name.ext", else "name (1).ext", "name (2).ext"…
inline std::wstring NextFreeName(const std::wstring& folder, const std::wstring& name) {
    const auto dot = name.find_last_of(L'.'); const std::wstring stem = dot == std::wstring::npos || dot == 0 ? name : name.substr(0, dot), ext = dot == std::wstring::npos || dot == 0 ? L"" : name.substr(dot);
    std::wstring target = folder + L"\\" + name;
    for (int n = 1; n < 1000 && Exists(target); ++n) target = folder + L"\\" + stem + L" (" + std::to_wstring(n) + L")" + ext;
    return target;
}
enum class Outcome { Untouched, Adopted, Restored, RestoredElsewhere, ToDownloads, TakenElsewhere, Failed };
// leftover: the name found; claimed: our own name for it now (this process's pid); to: where the file is (Adopted,
// Restored: its recorded path, watched from now on; RestoredElsewhere / ToDownloads: a name the engine never knew).
struct Result { std::wstring leftover, claimed, to; Outcome outcome; };
// A browser killed in the middle of a keep: when the next start finds the file at its place, that does not make it safe.
// Measured (night run, held_download_e2e keep-then-kill): the restarted engine removed the file by name ~8 s after the
// kill, after an earlier sweep had judged it in place and dropped our name. So a leftover is never dropped on the spot: it
// is claimed (renamed to this process, atomically: two processes sweeping at once never both take it), given back where
// the file is missing, and WATCHED like a keep (src/main.cpp WatchKept): every removal undone from our name until the
// guard, counted from this profile's engine being up, is over; only then, with the file at its path proven ours, does
// our name go. A window closing before that leaves it for the next start.
inline constexpr std::int64_t AdoptedGuardMs = 60000;
enum class Watch { Wait, GiveBack, Settle };
// engineUp: this profile's engine has started (its own clean-up of an unfinished download comes after its start);
// sinceMs: since the later of the engine's start and the last give-back; there: the file is at its recorded path.
inline Watch AdoptedLook(bool engineUp, std::int64_t sinceMs, bool there, std::int64_t guardMs = AdoptedGuardMs) {
    if (!there) return Watch::GiveBack;
    return engineUp && sinceMs >= guardMs ? Watch::Settle : Watch::Wait;
}
// live: is that pid a running browser of ours (its keep is under way: untouched). ownPid / nextId: this process and the
// ids its claimed names take. downloadFolder: where a leftover without a readable sidecar goes. Log: one line per leftover.
inline std::vector<Result> Sweep(const std::wstring& folder, const std::wstring& downloadFolder, const std::function<bool(unsigned long)>& live, const std::function<void(const std::wstring&)>& Log,
                                 unsigned long ownPid = GetCurrentProcessId(), unsigned long nextId = 900000000) {
    std::vector<Result> out; std::vector<std::wstring> names;
    WIN32_FIND_DATAW f{}; HANDLE h = FindFirstFileW((folder + L"\\*").c_str(), &f);
    if (h == INVALID_HANDLE_VALUE) return out;
    do { if (!(f.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) names.push_back(f.cFileName); } while (FindNextFileW(h, &f));
    FindClose(h);
    for (const auto& n : names) {
        unsigned long pid = 0; std::wstring file; const std::wstring leftover = folder + L"\\" + n;
        if (!ParseName(n, pid, file)) {   // a sidecar alone (its file already went): dropped once its process is gone
            unsigned long p2 = 0; std::wstring f2;
            if (n.size() > 5 && n.compare(n.size() - 5, 5, L".path") == 0 && ParseName(n.substr(0, n.size() - 5), p2, f2) && p2 != ownPid && !live(p2) && !Exists(leftover.substr(0, leftover.size() - 5))) DeleteFileW(leftover.c_str());
            continue;
        }
        if (pid == ownPid || live(pid)) { out.push_back({leftover, L"", L"", Outcome::Untouched}); continue; }
        // Claimed by a rename: the one process whose rename succeeds owns it (another one sweeping at the same start).
        const std::wstring claimed = folder + L"\\" + std::to_wstring(ownPid) + L"-" + std::to_wstring(nextId++) + L"-" + file;
        if (!MoveFileExW(leftover.c_str(), claimed.c_str(), 0)) { out.push_back({leftover, L"", L"", Outcome::TakenElsewhere}); continue; }
        MoveFileExW(SidecarOf(leftover).c_str(), SidecarOf(claimed).c_str(), 0);
        std::wstring finalPath, to; Outcome outcome;
        if (!ReadSidecar(claimed, finalPath)) {   // no note of where it belongs: the download folder, under its own name
            to = NextFreeName(downloadFolder, file); outcome = Outcome::ToDownloads;
        } else if (Exists(finalPath) && HoldsOurBytes(finalPath, claimed)) {   // at its place: watched, never dropped on the spot
            out.push_back({leftover, claimed, finalPath, Outcome::Adopted}); Log(L"DOWNLOAD kept leftover adopted: the file is at its place, watched before our name goes: " + NameOf(finalPath)); continue;
        } else if (!Exists(finalPath) || (SizeOf(finalPath) == 0 && SizeOf(claimed) > 0 && DeleteFileW(finalPath.c_str()))) {   // missing (or the engine's empty leftover): given back, then watched
            if (CreateHardLinkW(finalPath.c_str(), claimed.c_str(), nullptr) || CopyFileW(claimed.c_str(), finalPath.c_str(), TRUE)) {
                out.push_back({leftover, claimed, finalPath, Outcome::Restored}); Log(L"DOWNLOAD kept leftover given back, watched before our name goes: " + finalPath); continue;
            }
            Log(L"DOWNLOAD kept leftover could not be given back (" + std::to_wstring(GetLastError()) + L"), it stays: " + claimed); out.push_back({leftover, claimed, finalPath, Outcome::Failed}); continue;
        } else { to = NextFreeName(FolderOf(finalPath), NameOf(finalPath)); outcome = Outcome::RestoredElsewhere; }   // another file took the path
        // A name the engine never knew: no clean-up of the engine can reach it, nothing to watch.
        if (!MoveFileExW(claimed.c_str(), to.c_str(), MOVEFILE_COPY_ALLOWED)) {
            Log(L"DOWNLOAD kept leftover could not be given back (" + std::to_wstring(GetLastError()) + L"), it stays: " + claimed); out.push_back({leftover, claimed, to, Outcome::Failed}); continue;
        }
        DeleteFileW(SidecarOf(claimed).c_str());
        Log(L"DOWNLOAD kept leftover given back under a name of its own: " + to); out.push_back({leftover, claimed, to, outcome});
    }
    return out;
}
}
