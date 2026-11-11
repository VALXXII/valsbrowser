#pragma once
// One session file per normal window (decision 2): <data>\sessions\<window id>. Each window
// process is the only writer of its own file and holds, for its whole life, a named mutex
// Local\browser-window-<profile hash>-<id> that says « this window is open ».
//  - A session whose window is not open is an orphan: the window crashed, or it was the last
//    one when the browser closed. At start, under the profile lock, the new process adopts the
//    most recent orphan as its own window and relaunches one process per other orphan
//    (--restore-window <id>), so every window comes back with its own tabs.
//  - Closing one window while others stay open removes its file (that window is closed, not
//    the browser); the last window keeps it for the next start.
//  - « Nouvelle fenêtre » (tab limit, partial restore) hands its URLs over as a new session file
//    and starts a process that adopts it; if the launch fails the file stays and comes back as an
//    orphan at the next start: nothing is lost silently.
// The pre-sessions file <data>\session is migrated once into the folder. Pure Win32 + C++17.
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>
#include "profile-store.h"

namespace WindowSessions {

inline std::wstring Dir(const std::wstring& dataDir) { return dataDir + L"\\sessions"; }
inline std::wstring File(const std::wstring& dataDir, const std::wstring& id) { return Dir(dataDir) + L"\\" + id; }
inline std::wstring LiveName(const std::wstring& dataDir, const std::wstring& id) {
    std::wstring profile = ProfileStore::MutexName(dataDir); profile = profile.substr(profile.rfind(L'-') + 1);
    return L"Local\\browser-window-" + profile + L"-" + id;
}
inline bool ValidId(const std::wstring& id) {
    if (id.empty() || id.size() > 40) return false;
    for (wchar_t c : id) if (!(iswalnum(c) || c == L'-')) return false;
    return true;
}
inline std::wstring NewId() {
    FILETIME ft; GetSystemTimeAsFileTime(&ft); static unsigned counter = 0;
    wchar_t text[48]; swprintf(text, 48, L"w%08lx%08lx-%lx-%x", ft.dwHighDateTime, ft.dwLowDateTime, GetCurrentProcessId(), ++counter);
    return text;
}
inline bool Live(const std::wstring& dataDir, const std::wstring& id) {
    HANDLE h = OpenMutexW(SYNCHRONIZE, FALSE, LiveName(dataDir, id).c_str());
    if (!h) return false;
    CloseHandle(h); return true;
}
// Marks `id` open for this process's lifetime. Null when another process already has it.
inline HANDLE Claim(const std::wstring& dataDir, const std::wstring& id) {
    HANDLE h = CreateMutexW(nullptr, FALSE, LiveName(dataDir, id).c_str());
    if (h && GetLastError() == ERROR_ALREADY_EXISTS) { CloseHandle(h); return nullptr; }
    return h;
}
struct Entry { std::wstring id; std::filesystem::file_time_type written; };
// Session files of the folder (temp files skipped), most recently written first.
inline std::vector<Entry> List(const std::wstring& dataDir) {
    std::vector<Entry> out; std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(Dir(dataDir), ec)) {
        const std::wstring id = e.path().filename().wstring();
        if (!e.is_regular_file(ec) || !ValidId(id) || id.rfind(L"closed-", 0) == 0) continue;   // closed windows are not sessions
        out.push_back({id, e.last_write_time(ec)});
    }
    std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) { return a.written > b.written; });
    return out;
}
// A session being relaunched by another starting process: reserved by that process (held for its
// lifetime) until the relaunched window claims it, so no simultaneous start can take it twice.
inline std::wstring ReserveName(const std::wstring& dataDir, const std::wstring& id) { return LiveName(dataDir, id) + L"-reserved"; }
inline bool Reserved(const std::wstring& dataDir, const std::wstring& id) {
    HANDLE h = OpenMutexW(SYNCHRONIZE, FALSE, ReserveName(dataDir, id).c_str()); if (!h) return false; CloseHandle(h); return true;
}
// Review S2: a window closing among others does not delete its session at once: it is renamed
// « closed-<id> ». If the other windows close right after (taskbar « Fermer toutes les fenêtres »,
// sign-out), the whole group comes back at the next start; a window closed long before the last one
// stays closed (deleted then).
constexpr int CloseTogetherSeconds = 10;
inline void ReviveClosedTogether(const std::wstring& dataDir) {
    std::error_code ec; std::filesystem::file_time_type last{}; bool any = false;
    for (const auto& e : std::filesystem::directory_iterator(Dir(dataDir), ec)) {
        const std::wstring id = e.path().filename().wstring();
        if (id.rfind(L"closed-", 0) == 0 || !e.is_regular_file(ec)) continue;
        if (!Live(dataDir, id)) { const auto t = e.last_write_time(ec); if (!any || t > last) last = t; any = true; }
    }
    if (!any)   // every window was closed « among others » (they all closed at once): the newest closing is the reference
        for (const auto& e : std::filesystem::directory_iterator(Dir(dataDir), ec))
            if (e.path().filename().wstring().rfind(L"closed-", 0) == 0) { const auto t = e.last_write_time(ec); if (!any || t > last) last = t; any = true; }
    for (const auto& e : std::filesystem::directory_iterator(Dir(dataDir), ec)) {
        const std::wstring name = e.path().filename().wstring();
        if (name.rfind(L"closed-", 0) != 0) continue;
        const auto t = e.last_write_time(ec);
        if (any && t + std::chrono::seconds(CloseTogetherSeconds) >= last) MoveFileExW(e.path().c_str(), File(dataDir, name.substr(7)).c_str(), MOVEFILE_WRITE_THROUGH);
        else DeleteFileW(e.path().c_str());
    }
}
inline std::vector<std::wstring> Orphans(const std::wstring& dataDir) {
    std::vector<std::wstring> out; for (const auto& e : List(dataDir)) if (!Live(dataDir, e.id) && !Reserved(dataDir, e.id)) out.push_back(e.id);
    return out;
}
inline bool OthersOpen(const std::wstring& dataDir, const std::wstring& self) {
    for (const auto& e : List(dataDir)) if (e.id != self && Live(dataDir, e.id)) return true;
    return false;
}
// Moves the single pre-sessions file into the folder (call under the profile lock).
inline void MigrateLegacy(const std::wstring& dataDir, const std::wstring& legacyFile) {
    std::error_code ec; std::filesystem::create_directories(Dir(dataDir), ec);
    if (legacyFile.empty() || !std::filesystem::exists(legacyFile, ec)) return;
    MoveFileExW(legacyFile.c_str(), File(dataDir, L"legacy-" + NewId()).c_str(), MOVEFILE_WRITE_THROUGH);
}

// Start of a normal window: decides, atomically under the profile lock, which session this
// process owns. `requested` (--restore-window): that handed-over or relaunched session, adopted
// only if no other process has it (else a fresh window: never two windows on one file). Otherwise
// the most recent orphan is adopted and the other orphans are returned to be relaunched; with
// restoration off they are not relaunched, they are dropped (the adopted id keeps the last
// session for when restoration is turned back on).
struct Adoption { std::wstring id; HANDLE live = nullptr; bool requested = false, adopted = false; std::vector<std::wstring> relaunch; std::vector<HANDLE> reservations; };
inline Adoption Adopt(const std::wstring& dataDir, const std::wstring& legacyFile, const std::wstring& requested, bool restoreTabs) {
    Adoption a; ProfileStore::Lock lock(dataDir, 10000);
    if (!lock.Held()) { while (!a.live) { a.id = NewId(); a.live = Claim(dataDir, a.id); } return a; }   // review S4: never adopt without the lock
    MigrateLegacy(dataDir, legacyFile);
    ReviveClosedTogether(dataDir);
    if (!requested.empty()) {
        if (ValidId(requested) && (a.live = Claim(dataDir, requested))) { a.id = requested; a.requested = true; }
    } else {
        for (const auto& id : Orphans(dataDir)) {
            if (!a.live) { if ((a.live = Claim(dataDir, id))) { a.id = id; a.adopted = true; } continue; }
            if (!restoreTabs) { DeleteFileW(File(dataDir, id).c_str()); continue; }
            if (HANDLE r = CreateMutexW(nullptr, FALSE, ReserveName(dataDir, id).c_str())) { a.reservations.push_back(r); a.relaunch.push_back(id); }
        }
    }
    while (!a.live) { a.id = NewId(); a.live = Claim(dataDir, a.id); }
    return a;
}
// Hands URLs to a new window: written durably before any launch, so a failed or crashed launch
// leaves them as an orphan session (restored at the next start), never lost.
inline bool WriteHandover(const std::wstring& dataDir, const std::wstring& id, const std::string& sessionText) {
    ProfileStore::Lock lock(dataDir, 10000); if (!lock.Held()) return false;
    std::error_code ec; std::filesystem::create_directories(Dir(dataDir), ec);
    return ProfileStore::WriteAtomic(File(dataDir, id), sessionText);
}
// A window closing: among other open windows its session goes (closed for good); the last one keeps it.
inline bool Release(const std::wstring& dataDir, const std::wstring& id, bool keep) {
    ProfileStore::Lock lock(dataDir, 10000);
    if (keep || !lock.Held() || !OthersOpen(dataDir, id)) return false;
    const std::wstring closed = File(dataDir, L"closed-" + id);
    if (!MoveFileExW(File(dataDir, id).c_str(), closed.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return false;
    HANDLE f = CreateFileW(closed.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f != INVALID_HANDLE_VALUE) { FILETIME now; GetSystemTimeAsFileTime(&now); SetFileTime(f, nullptr, nullptr, &now); CloseHandle(f); }
    return true;
}

} // namespace WindowSessions
