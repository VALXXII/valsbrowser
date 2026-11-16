#pragma once
#include "i18n.h"
// Import from another browser (Réglages › Confidentialité › « Importer depuis un navigateur… »):
// bookmarks and history from Chrome, Edge, Brave and Firefox profiles, and bookmarks from an HTML
// bookmark file. Plan, availability matrix and rules: design/browser-data-import-plan.md.
//
// Three steps, kept apart so each can be tested on synthetic roots:
//   1. Discover(roots): directory metadata and Firefox's profiles.ini only, never a data file.
//   2. Read(source, …): runs after the person pressed « Importer » (worker thread, no globals). Files are
//      read-only, bounded, refused when they are reparse points. SQLite databases are never opened in place:
//      the database and its sidecars are copied into a private temporary folder while the source is opened
//      WITHOUT write sharing (no process can be writing it during the copy; a running browser makes that
//      open fail → Failure::Busy, « close the browser and retry »). The copy is opened and queried in one
//      read transaction, then deleted on every exit. Nothing is ever created or changed beside the source.
//   3. ApplyBookmarks / ApplyHistory: pure, run inside the profile store's merge (src/profile-store.h) on
//      the file's current state, so duplicates are judged against what is really on disk. Append only.
// Cookies are not read from any source (design/browser-data-import-report.md, « Cookies »).
// Nothing here logs a URL, a title or a path of the person's data.
#include <windows.h>
#include <shlobj.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "address.h"
#include "bookmarks.h"
#include "profile-store.h"
#include "strict-json.h"

namespace BrowserImport {

enum class Browser { Chrome, Edge, Brave, Firefox, HtmlFile };
inline const wchar_t* BrandName(Browser b) {
    switch (b) {
    case Browser::Chrome: return L"Chrome";     // i18n-exempt: product name
    case Browser::Edge: return L"Edge";         // i18n-exempt: product name
    case Browser::Brave: return L"Brave";       // i18n-exempt: product name
    case Browser::Firefox: return L"Firefox";   // i18n-exempt: product name
    default: return L"";
    }
}
inline bool Chromium(Browser b) { return b == Browser::Chrome || b == Browser::Edge || b == Browser::Brave; }

// ---- Bounds (untrusted input) -----------------------------------------------------------------------
namespace Bound {
inline constexpr std::uint64_t jsonBytes = 32ull << 20, htmlBytes = 32ull << 20, iniBytes = 64ull << 10;
inline constexpr std::uint64_t databaseBytes = 1024ull << 20;   // copied snapshot (database + sidecars)
inline constexpr std::size_t nesting = 256;                      // folder levels read from a source
inline constexpr std::size_t bookmarkRows = 200000, historyRows = 50000, items = 1000000;
inline constexpr std::size_t text = 65536;                       // one title/URL read from a source (UTF-16 units)
inline constexpr int busyMs = 1500;
}

// ---- Model -------------------------------------------------------------------------------------------
struct Item {                       // one bookmark or folder, in source order
    bool folder = false;
    std::wstring title, url;
    std::int64_t added = 0;         // Unix ms, 0 = unknown
    std::vector<Item> children;
    std::string key;                // a source root's stable identity (« bookmark_bar », « toolbar_____ »); empty otherwise
};
struct Visit { std::wstring url, title; int visits = 1; std::int64_t last = 0; };   // last: Unix ms

enum class Failure {
    None,
    Missing,       // the file is not there (any more)
    Busy,          // the source browser has it open: close it and retry
    Unreadable,    // access denied, I/O error, reparse point, not a regular file
    Malformed,     // not the expected format
    TooLarge,      // over the size / count bounds
    TooDeep,       // folders nested deeper than Bound::nesting: refused whole, never cut
    Unsupported,   // a schema this build does not know
    NoDatabase,    // winsqlite3.dll unavailable on this Windows
};
struct Source {
    Browser browser = Browser::HtmlFile;
    std::wstring dir;               // profile folder (Chromium, Firefox) or the HTML file
    std::wstring root;              // the browser's folder (« User Data », « Firefox »); HTML: empty (the file's folder)
                                    // every folder from it down to `dir` is pinned and checked again at read time
    std::wstring profile;           // folder name (Chromium) or profiles.ini Name (Firefox)
    bool isDefault = false;         // Chromium « Default », Firefox Default=1
    bool bookmarks = false, history = false;   // the data file exists (metadata only)
    bool soleProfile = true;        // the only profile of its browser: its label is just the brand
};
struct Roots { std::wstring local, roaming; };   // %LOCALAPPDATA%, %APPDATA% (tests: synthetic folders)

struct Bookmarks_ { Failure failure = Failure::None; std::vector<Item> roots; std::size_t ignored = 0; };
struct History_ { Failure failure = Failure::None; std::vector<Visit> visits; std::size_t beyond = 0, ignored = 0; };
struct ReadResult { Bookmarks_ bookmarks; History_ history; };

// Per category, what happened to the source's entries. `present` = already in the destination.
struct Counts {
    std::size_t imported = 0, present = 0, unsupported = 0, full = 0, deep = 0, folders = 0;
    std::size_t unreadable = 0;   // source entries the reader could not use (damaged node, link without address): reported, never hidden
    std::size_t moved = 0;        // entries of an earlier import moved out of its old « Importés de … » group (same ids)
    bool operator==(const Counts& o) const { return imported == o.imported && present == o.present && unsupported == o.unsupported && full == o.full && deep == o.deep && folders == o.folders && unreadable == o.unreadable && moved == o.moved; }
    bool Left() const { return full || deep || unreadable; }   // something of the source did not arrive
};

// ---- Time --------------------------------------------------------------------------------------------
inline constexpr std::int64_t WindowsToUnixMs = 11644473600000LL;   // 1601-01-01 → 1970-01-01
inline std::int64_t ChromiumTime(std::int64_t microsSince1601) { return microsSince1601 <= 0 ? 0 : microsSince1601 / 1000 - WindowsToUnixMs; }
inline std::int64_t FirefoxTime(std::int64_t prtime) { return prtime <= 0 ? 0 : prtime / 1000; }
// Netscape ADD_DATE: seconds; some exporters write ms or µs.
inline std::int64_t HtmlTime(std::int64_t v) { return v <= 0 ? 0 : v >= 100000000000000LL ? v / 1000 : v >= 100000000000LL ? v : v * 1000; }
inline std::int64_t Clamp(std::int64_t t, std::int64_t now) { return t > now ? now : t < 0 ? 0 : t; }

// ---- Text --------------------------------------------------------------------------------------------
inline std::wstring Clip(std::wstring s) { if (s.size() > Bound::text) s.resize(Bound::text); return s; }
inline std::wstring Widen(const std::string& utf8) { std::wstring w; if (!NativeChrome::Downloads::FromUtf8(utf8, w)) return L""; return Clip(std::move(w)); }

// ---- Files (read-only, no reparse surprises) ----------------------------------------------------------
inline bool IsReparse(const std::wstring& path) {
    const DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT);
}
inline bool IsDirectory(const std::wstring& path) {
    const DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) && !(a & FILE_ATTRIBUTE_REPARSE_POINT);
}
inline bool IsFile(const std::wstring& path) {
    const DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DEVICE));
}
// Every folder from `root` (included) down to `path` (included) is a real directory, not a junction or
// symbolic link: a planted link cannot redirect a source into another tree.
inline bool PlainChain(const std::wstring& root, const std::wstring& path) {
    if (path.size() < root.size() || _wcsnicmp(path.c_str(), root.c_str(), root.size()) != 0) return false;
    if (!IsDirectory(root)) return false;
    for (std::size_t i = root.size(); i <= path.size(); ++i) {
        if (i < path.size() && path[i] != L'\\') continue;
        if (i == root.size()) continue;
        if (!IsDirectory(path.substr(0, i))) return false;
    }
    return true;
}
// An open handle that is a plain disk file (not a reparse point, device or directory).
inline bool PlainHandle(HANDLE f) {
    BY_HANDLE_FILE_INFORMATION info{};
    return GetFileType(f) == FILE_TYPE_DISK && GetFileInformationByHandle(f, &info) && !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT));
}
inline Failure OpenFailure(DWORD e) {
    if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) return Failure::Missing;
    if (e == ERROR_SHARING_VIOLATION || e == ERROR_LOCK_VIOLATION) return Failure::Busy;
    return Failure::Unreadable;
}
// ---- Read-time pinning (review 934ef4b, finding 3) -----------------------------------------------------------
// Discovery's checks are not enough: a folder can be replaced by a junction between discovery and « Importer ».
// At read time every folder from the browser's root down to the profile is opened WITHOUT delete sharing (it can
// be neither renamed nor replaced while held: the handles carry data access, so sharing applies), each handle must be a real directory (not a reparse point) whose
// final path is its parent's final path plus its own name, and every source file handle is accepted only if its
// own final path is the pinned folder's final path plus the expected file name. Whatever a path walk resolved,
// only a handle that really sits in the pinned folder is read.
inline std::wstring FinalPath(HANDLE h) {
    wchar_t buffer[2048]; const DWORD n = GetFinalPathNameByHandleW(h, buffer, 2048, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    return n > 0 && n < 2048 ? std::wstring(buffer, n) : std::wstring();
}
inline bool SamePath(const std::wstring& a, const std::wstring& b) { return !a.empty() && a.size() == b.size() && CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL; }
class Pin {
public:
    Pin() = default; Pin(const Pin&) = delete; Pin& operator=(const Pin&) = delete;
    ~Pin() { for (HANDLE h : held_) CloseHandle(h); }
    // `dir` must be `root` or below it.
    Failure Open(std::wstring root, std::wstring dir) {
        auto trim = [](std::wstring& p) { for (auto& c : p) if (c == L'/') c = L'\\'; while (p.size() > 3 && p.back() == L'\\') p.pop_back(); };
        trim(root); trim(dir);
        if (root.empty() || dir.size() < root.size() || _wcsnicmp(dir.c_str(), root.c_str(), root.size()) != 0 || (dir.size() > root.size() && dir[root.size()] != L'\\')) return Failure::Unreadable;
        std::wstring path = root, expected;
        std::size_t at = root.size();
        for (;;) {
            HANDLE h = CreateFileW(path.c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (h == INVALID_HANDLE_VALUE) { const Failure f = OpenFailure(GetLastError()); return f == Failure::Missing ? f : Failure::Unreadable; }
            held_.push_back(h);
            BY_HANDLE_FILE_INFORMATION info{};
            if (!GetFileInformationByHandle(h, &info) || !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) return Failure::Unreadable;
            const std::wstring final = FinalPath(h);
            if (final.empty() || (!expected.empty() && !SamePath(final, expected))) return Failure::Unreadable;   // a component resolved elsewhere
            final_ = final;
            if (at >= dir.size()) return Failure::None;
            const std::size_t next = dir.find(L'\\', at + 1);
            const std::wstring name = dir.substr(at + 1, (next == std::wstring::npos ? dir.size() : next) - at - 1);
            if (name.empty() || name == L"." || name == L"..") return Failure::Unreadable;
            expected = final_ + L"\\" + name;
            path = dir.substr(0, next == std::wstring::npos ? dir.size() : next);
            at = path.size();
        }
    }
    const std::wstring& Final() const { return final_; }
    // The open file `h` is the file `name` directly inside the pinned folder.
    bool Contains(HANDLE h, const std::wstring& name) const { return !final_.empty() && SamePath(FinalPath(h), final_ + L"\\" + name); }
private:
    std::vector<HANDLE> held_; std::wstring final_;
};
inline std::wstring FileName(const std::wstring& path) { const auto k = path.find_last_of(L"\\/"); return k == std::wstring::npos ? path : path.substr(k + 1); }
inline std::wstring Parent(const std::wstring& path) { const auto k = path.find_last_of(L"\\/"); return k == std::wstring::npos ? L"" : path.substr(0, k); }
// Whole file, read-only with full sharing (a JSON/HTML file is replaced by rename, never edited in place).
// With `pin`: the opened handle must be `path`'s file name directly inside the pinned folder.
inline Failure ReadFileBounded(const std::wstring& path, std::uint64_t limit, std::string& out, const Pin* pin = nullptr) {
    out.clear();
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (f == INVALID_HANDLE_VALUE) return OpenFailure(GetLastError());
    Failure r = Failure::None; LARGE_INTEGER size{};
    if (!PlainHandle(f) || !GetFileSizeEx(f, &size) || (pin && !pin->Contains(f, FileName(path)))) r = Failure::Unreadable;
    else if (static_cast<std::uint64_t>(size.QuadPart) > limit) r = Failure::TooLarge;
    else {
        out.resize(static_cast<std::size_t>(size.QuadPart)); DWORD got = 0;
        if (!out.empty() && (!ReadFile(f, out.data(), static_cast<DWORD>(out.size()), &got, nullptr) || got != out.size())) { out.clear(); r = Failure::Unreadable; }
    }
    CloseHandle(f);
    return r;
}

// ---- SQLite (Windows' own winsqlite3.dll, stable documented C API) -------------------------------------
namespace Sql {
struct sqlite3; struct sqlite3_stmt;
inline constexpr int OK = 0, ROW = 100, DONE = 101, BUSY = 5, LOCKED = 6, NOMEM = 7, READONLY = 8, CORRUPT = 11, CANTOPEN = 14, NOTADB = 26;
inline constexpr int OPEN_READONLY = 0x1, OPEN_READWRITE = 0x2, OPEN_URI = 0x40, OPEN_NOMUTEX = 0x8000, OPEN_PRIVATECACHE = 0x40000, OPEN_NOFOLLOW = 0x01000000;
inline constexpr int NULLTYPE = 5;
struct Api {
    HMODULE dll = nullptr;
    int(__stdcall* open_v2)(const char*, sqlite3**, int, const char*) = nullptr;
    int(__stdcall* close)(sqlite3*) = nullptr;
    int(__stdcall* prepare_v2)(sqlite3*, const char*, int, sqlite3_stmt**, const char**) = nullptr;
    int(__stdcall* step)(sqlite3_stmt*) = nullptr;
    int(__stdcall* finalize)(sqlite3_stmt*) = nullptr;
    const void*(__stdcall* column_text16)(sqlite3_stmt*, int) = nullptr;
    int(__stdcall* column_bytes16)(sqlite3_stmt*, int) = nullptr;
    long long(__stdcall* column_int64)(sqlite3_stmt*, int) = nullptr;
    int(__stdcall* column_type)(sqlite3_stmt*, int) = nullptr;
    int(__stdcall* exec)(sqlite3*, const char*, void*, void*, char**) = nullptr;
    int(__stdcall* busy_timeout)(sqlite3*, int) = nullptr;
    bool Ready() const { return dll && open_v2 && close && prepare_v2 && step && finalize && column_text16 && column_bytes16 && column_int64 && column_type && exec && busy_timeout; }
};
// Loaded once from System32 only (never from the application or current folder).
inline const Api& Get() {
    static Api api = [] {
        Api a; a.dll = LoadLibraryExW(L"winsqlite3.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!a.dll) return a;
        auto get = [&](auto& fn, const char* name) { fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(reinterpret_cast<void*>(GetProcAddress(a.dll, name))); };
        get(a.open_v2, "sqlite3_open_v2"); get(a.close, "sqlite3_close"); get(a.prepare_v2, "sqlite3_prepare_v2"); get(a.step, "sqlite3_step");
        get(a.finalize, "sqlite3_finalize"); get(a.column_text16, "sqlite3_column_text16"); get(a.column_bytes16, "sqlite3_column_bytes16");
        get(a.column_int64, "sqlite3_column_int64"); get(a.column_type, "sqlite3_column_type"); get(a.exec, "sqlite3_exec"); get(a.busy_timeout, "sqlite3_busy_timeout");
        return a;
    }();
    return api;
}
inline Failure CodeFailure(int rc) {
    switch (rc & 0xFF) {
    case BUSY: case LOCKED: return Failure::Busy;
    case CORRUPT: case NOTADB: return Failure::Malformed;
    case CANTOPEN: return Failure::Unreadable;
    default: return Failure::Unreadable;
    }
}
// "file:" URI of an absolute Windows path, every byte outside the unreserved set percent-encoded
// (spaces, %, #, ?, non-ASCII), so no path can be read as a query or fragment.
inline std::string FileUri(const std::wstring& path, const char* query) {
    std::string utf8 = NativeChrome::Downloads::ToUtf8(path), uri = "file:///";
    static const char* hex = "0123456789ABCDEF";
    for (unsigned char c : utf8) {
        if (c == '\\') c = '/';
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '~' || c == '/' || c == ':') uri += static_cast<char>(c);
        else { uri += '%'; uri += hex[c >> 4]; uri += hex[c & 15]; }
    }
    if (query && *query) { uri += '?'; uri += query; }
    return uri;
}
class Statement {
public:
    Statement(sqlite3* db, const char* sql) { const auto& a = Get(); rc_ = a.prepare_v2(db, sql, -1, &s_, nullptr); }
    ~Statement() { if (s_) Get().finalize(s_); }
    Statement(const Statement&) = delete; Statement& operator=(const Statement&) = delete;
    bool Prepared() const { return rc_ == OK && s_; }
    int Step() { return rc_ = Get().step(s_); }
    int Code() const { return rc_; }
    std::wstring Text(int col) const {
        const auto& a = Get(); const auto* p = static_cast<const wchar_t*>(a.column_text16(s_, col));
        if (!p) return L"";
        const int n = a.column_bytes16(s_, col) / 2;
        return std::wstring(p, static_cast<std::size_t>(std::min(n, static_cast<int>(Bound::text))));
    }
    std::int64_t Int(int col) const { return Get().column_int64(s_, col); }
    bool Null(int col) const { return Get().column_type(s_, col) == NULLTYPE; }
private:
    sqlite3_stmt* s_ = nullptr; int rc_ = 0;
};
} // namespace Sql

// Test seam (dev builds' E2E only, see browser-import-integration.h): runs once a private snapshot exists.
inline std::function<void()>& SnapshotTakenForTest() { static std::function<void()> hook; return hook; }
// A private copy of a SQLite database and its sidecars (-wal, -journal; never -shm, which is only an index
// rebuilt from the WAL). Every source file is opened with FILE_SHARE_READ only, all of them before the first
// byte is copied, and held until the last one is copied: if any process has one open for writing the open
// fails (Busy), and no process can start writing while we copy. The copy therefore is one consistent state,
// and SQLite recovers a crashed WAL/journal on the copy, never on the source.
class Snapshot {
public:
    ~Snapshot() { Clean(); }
    Snapshot() = default; Snapshot(const Snapshot&) = delete; Snapshot& operator=(const Snapshot&) = delete;
    // `tempRoot` (tests) or the user's temporary folder.
    Failure Take(const std::wstring& database, const std::wstring& tempRoot = L"", const Pin* pin = nullptr) {
        Clean();
        struct Part { const wchar_t* suffix; HANDLE h = INVALID_HANDLE_VALUE; };
        Part parts[] = {{L""}, {L"-wal"}, {L"-journal"}};
        Failure r = Failure::None; std::uint64_t total = 0;
        for (auto& p : parts) {
            const std::wstring path = database + p.suffix;
            p.h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
            if (p.h == INVALID_HANDLE_VALUE) {
                const Failure f = OpenFailure(GetLastError());
                if (f == Failure::Missing && *p.suffix) continue;   // no sidecar: fine
                r = f; break;
            }
            LARGE_INTEGER size{};
            if (!PlainHandle(p.h) || !GetFileSizeEx(p.h, &size) || (pin && !pin->Contains(p.h, FileName(path)))) { r = Failure::Unreadable; break; }
            total += static_cast<std::uint64_t>(size.QuadPart);
            if (total > Bound::databaseBytes) { r = Failure::TooLarge; break; }
        }
        if (r == Failure::None && !MakeFolder(tempRoot)) r = Failure::Unreadable;
        for (auto& p : parts) {
            if (r == Failure::None && p.h != INVALID_HANDLE_VALUE && !CopyHandle(p.h, folder_ + L"\\db" + p.suffix)) r = Failure::Unreadable;
        }
        for (auto& p : parts) if (p.h != INVALID_HANDLE_VALUE) CloseHandle(p.h);
        if (r != Failure::None) Clean();
        else if (SnapshotTakenForTest()) SnapshotTakenForTest()();
        return r;
    }
    std::wstring Path() const { return folder_.empty() ? L"" : folder_ + L"\\db"; }
    const std::wstring& Folder() const { return folder_; }
    void Clean() {
        if (folder_.empty()) return;
        for (const wchar_t* s : {L"", L"-wal", L"-journal", L"-shm"}) DeleteFileW((folder_ + L"\\db" + s).c_str());
        RemoveDirectoryW(folder_.c_str());
        folder_.clear();
    }
private:
    std::wstring folder_;
    bool MakeFolder(const std::wstring& tempRoot) {
        std::wstring root = tempRoot;
        if (root.empty()) { wchar_t t[MAX_PATH + 1]{}; const DWORD n = GetTempPathW(MAX_PATH + 1, t); if (!n || n > MAX_PATH) return false; root = t; }
        while (!root.empty() && (root.back() == L'\\' || root.back() == L'/')) root.pop_back();
        SweepStale(root);
        for (int attempt = 0; attempt < 16; ++attempt) {
            LARGE_INTEGER c{}; QueryPerformanceCounter(&c);
            wchar_t name[64]; swprintf(name, 64, L"\\browser-import-%lu-%llx", GetCurrentProcessId(), static_cast<unsigned long long>(c.QuadPart) + attempt);
            const std::wstring f = root + name;
            if (CreateDirectoryW(f.c_str(), nullptr)) { folder_ = f; return true; }
            if (GetLastError() != ERROR_ALREADY_EXISTS) return false;
        }
        return false;
    }
public:
    // A process that ended during a read (window closed, crash) could not run Clean(): its private copy is removed
    // here, by the next snapshot. Only « browser-import-<pid>-<hex> » folders of processes no longer running, only
    // their own four files, never through a reparse point. A live process (or one we cannot query) is left alone.
    static void SweepStale(const std::wstring& root) {
        WIN32_FIND_DATAW fd{}; HANDLE find = FindFirstFileExW((root + L"\\browser-import-*").c_str(), FindExInfoBasic, &fd, FindExSearchLimitToDirectories, nullptr, 0);
        if (find == INVALID_HANDLE_VALUE) return;
        do {
            const std::wstring name = fd.cFileName;
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
            const std::size_t dash = name.find(L'-', 15);
            if (name.rfind(L"browser-import-", 0) != 0 || dash == std::wstring::npos || dash == 15) continue;
            const std::wstring pid = name.substr(15, dash - 15), tag = name.substr(dash + 1);
            if (pid.size() > 10 || pid.find_first_not_of(L"0123456789") != std::wstring::npos || tag.empty() || tag.find_first_not_of(L"0123456789abcdef") != std::wstring::npos) continue;
            const DWORD id = static_cast<DWORD>(std::wcstoul(pid.c_str(), nullptr, 10));
            if (id == GetCurrentProcessId()) continue;
            HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, id);
            if (process) { const bool alive = WaitForSingleObject(process, 0) == WAIT_TIMEOUT; CloseHandle(process); if (alive) continue; }
            else if (GetLastError() != ERROR_INVALID_PARAMETER) continue;   // exists but not ours to query: leave it
            const std::wstring dir = root + L"\\" + name;
            for (const wchar_t* f : {L"", L"-wal", L"-journal", L"-shm"}) DeleteFileW((dir + L"\\db" + f).c_str());
            RemoveDirectoryW(dir.c_str());   // fails (kept) if anything else is inside
        } while (FindNextFileW(find, &fd));
        FindClose(find);
    }
private:
    static bool CopyHandle(HANDLE from, const std::wstring& to) {
        HANDLE out = CreateFileW(to.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, nullptr);
        if (out == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER zero{}; bool ok = SetFilePointerEx(from, zero, nullptr, FILE_BEGIN);
        std::vector<char> buffer(1 << 20);
        while (ok) {
            DWORD got = 0, put = 0;
            if (!ReadFile(from, buffer.data(), static_cast<DWORD>(buffer.size()), &got, nullptr)) { ok = false; break; }
            if (got == 0) break;
            if (!WriteFile(out, buffer.data(), got, &put, nullptr) || put != got) ok = false;
        }
        CloseHandle(out);
        return ok;
    }
};
// Opens the private copy (read-write: SQLite may need to replay the copied WAL/journal) and starts one read
// transaction. Closed by the destructor.
class Database {
public:
    explicit Database(const std::wstring& copy) {
        const auto& a = Sql::Get();
        if (!a.Ready()) { failure_ = Failure::NoDatabase; return; }
        const int rc = a.open_v2(Sql::FileUri(copy, "").c_str(), &db_, Sql::OPEN_READWRITE | Sql::OPEN_URI | Sql::OPEN_NOMUTEX | Sql::OPEN_PRIVATECACHE, nullptr);
        if (rc != Sql::OK) { failure_ = Sql::CodeFailure(rc); return; }
        a.busy_timeout(db_, Bound::busyMs);
        const int begin = a.exec(db_, "BEGIN", nullptr, nullptr, nullptr);
        if (begin != Sql::OK) { failure_ = Sql::CodeFailure(begin); return; }
        // Touch the schema now: a file that is not a database fails here, not in the middle of a query.
        Sql::Statement probe(db_, "SELECT count(*) FROM sqlite_master");
        if (!probe.Prepared()) { failure_ = Sql::CodeFailure(probe.Code()); return; }
        const int rc2 = probe.Step();
        if (rc2 != Sql::ROW) failure_ = Sql::CodeFailure(rc2);
    }
    ~Database() { if (db_) { const auto& a = Sql::Get(); a.exec(db_, "COMMIT", nullptr, nullptr, nullptr); a.close(db_); } }
    Database(const Database&) = delete; Database& operator=(const Database&) = delete;
    Failure failure() const { return failure_; }
    Sql::sqlite3* handle() const { return db_; }
private:
    Sql::sqlite3* db_ = nullptr; Failure failure_ = Failure::None;
};

// ---- Discovery (metadata only) ---------------------------------------------------------------------------
inline std::wstring ChromiumRoot(const Roots& roots, Browser b) {
    if (roots.local.empty()) return L"";
    switch (b) {
    case Browser::Chrome: return roots.local + L"\\Google\\Chrome\\User Data";
    case Browser::Edge: return roots.local + L"\\Microsoft\\Edge\\User Data";
    case Browser::Brave: return roots.local + L"\\BraveSoftware\\Brave-Browser\\User Data";
    default: return L"";
    }
}
inline std::wstring FirefoxRoot(const Roots& roots) { return roots.roaming.empty() ? L"" : roots.roaming + L"\\Mozilla\\Firefox"; }
inline Roots DefaultRoots() {
    Roots r; PWSTR p = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DONT_VERIFY, nullptr, &p)) && p) r.local = p;
    CoTaskMemFree(p); p = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_DONT_VERIFY, nullptr, &p)) && p) r.roaming = p;
    CoTaskMemFree(p);
    return r;
}
// Chromium profile folders: « Default » first, then « Profile N » by number.
inline std::vector<Source> DiscoverChromium(const Roots& roots, Browser b) {
    std::vector<Source> out; const std::wstring root = ChromiumRoot(roots, b);
    if (root.empty() || !IsDirectory(root)) return out;
    std::vector<std::pair<long, std::wstring>> names;
    WIN32_FIND_DATAW fd{}; HANDLE find = FindFirstFileExW((root + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchLimitToDirectories, nullptr, 0);
    if (find == INVALID_HANDLE_VALUE) return out;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
        const std::wstring name = fd.cFileName;
        if (name == L"Default") names.push_back({-1, name}); // i18n-exempt: Chromium profile folder name
        else if (name.rfind(L"Profile ", 0) == 0 && name.size() > 8 && name.size() < 16 && name.find_first_not_of(L"0123456789", 8) == std::wstring::npos) names.push_back({std::wcstol(name.c_str() + 8, nullptr, 10), name}); // i18n-exempt: Chromium profile folder prefix
    } while (FindNextFileW(find, &fd) && names.size() < 256);
    FindClose(find);
    std::sort(names.begin(), names.end());
    for (const auto& [n, name] : names) {
        Source s; s.browser = b; s.dir = root + L"\\" + name; s.root = root; s.profile = name; s.isDefault = n < 0;
        if (!PlainChain(root, s.dir)) continue;
        s.bookmarks = IsFile(s.dir + L"\\Bookmarks"); s.history = IsFile(s.dir + L"\\History");
        if (s.bookmarks || s.history) out.push_back(std::move(s));
    }
    for (auto& s : out) s.soleProfile = out.size() == 1;
    return out;
}
// profiles.ini (metadata: names and folders). Only relative profiles that stay inside the Firefox folder.
inline std::vector<Source> DiscoverFirefox(const Roots& roots) {
    std::vector<Source> out; const std::wstring root = FirefoxRoot(roots);
    if (root.empty() || !IsDirectory(root)) return out;
    std::string bytes;
    if (ReadFileBounded(root + L"\\profiles.ini", Bound::iniBytes, bytes) != Failure::None) return out;
    std::wstring text = Widen(bytes);
    if (text.empty()) return out;
    if (text[0] == 0xFEFF) text.erase(0, 1);
    struct Entry { std::wstring name, path; bool relative = false, isDefault = false; };
    std::vector<Entry> entries; bool inProfile = false;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find(L'\n', start); if (end == std::wstring::npos) end = text.size();
        std::wstring line = AddressBar::Trim(text.substr(start, end - start)); start = end + 1;
        if (line.empty() || line[0] == L';' || line[0] == L'#') continue;
        if (line[0] == L'[') { inProfile = line.rfind(L"[Profile", 0) == 0; if (inProfile && entries.size() < 64) entries.push_back({}); else inProfile = false; continue; }
        if (!inProfile || entries.empty()) continue;
        const auto eq = line.find(L'='); if (eq == std::wstring::npos) continue;
        const std::wstring k = line.substr(0, eq), v = line.substr(eq + 1);
        auto& e = entries.back();
        if (k == L"Name") e.name = v.substr(0, 128); else if (k == L"Path") e.path = v; else if (k == L"IsRelative") e.relative = v == L"1"; else if (k == L"Default") e.isDefault = v == L"1"; // i18n-exempt: profiles.ini keys
    }
    for (const auto& e : entries) {
        if (!e.relative || e.path.empty() || e.path.size() > 260) continue;
        std::wstring rel = e.path; for (auto& c : rel) if (c == L'/') c = L'\\';
        if (rel[0] == L'\\' || rel.find(L':') != std::wstring::npos || rel.find(L"..") != std::wstring::npos) continue;
        Source s; s.browser = Browser::Firefox; s.dir = root + L"\\" + rel; s.root = root; s.profile = e.name.empty() ? rel : e.name; s.isDefault = e.isDefault;
        if (!PlainChain(root, s.dir)) continue;
        s.bookmarks = s.history = IsFile(s.dir + L"\\places.sqlite");
        if (s.bookmarks) out.push_back(std::move(s));
    }
    for (auto& s : out) s.soleProfile = out.size() == 1;
    return out;
}
inline std::vector<Source> Discover(const Roots& roots) {
    std::vector<Source> out;
    for (Browser b : {Browser::Chrome, Browser::Edge, Browser::Brave}) { auto v = DiscoverChromium(roots, b); out.insert(out.end(), v.begin(), v.end()); }
    auto f = DiscoverFirefox(roots); out.insert(out.end(), f.begin(), f.end());
    return out;
}
inline Source HtmlSource(const std::wstring& file) { Source s; s.browser = Browser::HtmlFile; s.dir = file; s.bookmarks = true; return s; }

// What the dialog shows for a source, and the folder its bookmarks land in.
inline std::wstring LabelIn(I18n::Locale l, const Source& s, bool sole) {
    using I18n::Id;
    if (s.browser == Browser::HtmlFile) return I18n::In(l, Id::import_source_html);
    if (sole) return BrandName(s.browser);
    const std::wstring profile = Chromium(s.browser) && s.isDefault ? std::wstring(I18n::In(l, Id::import_profile_default)) : s.profile;
    return I18n::FormatText(I18n::In(l, Id::import_source_profile), {BrandName(s.browser), profile});
}
inline std::wstring FolderTitleIn(I18n::Locale l, const Source& s, bool sole) {
    if (s.browser == Browser::HtmlFile) return I18n::In(l, I18n::Id::import_folder_html);
    return I18n::FormatText(I18n::In(l, I18n::Id::import_folder), {LabelIn(l, s, sole)});
}
inline std::wstring Label(const Source& s) { return LabelIn(I18n::Current(), s, s.soleProfile); }
inline std::wstring FolderTitle(const Source& s) { return FolderTitleIn(I18n::Current(), s, s.soleProfile); }
// Every title the source's folder may carry from an earlier import (both languages, sole profile or not): only a
// fallback when the stable mapping (FolderMap) has no valid entry.
inline std::vector<std::wstring> FolderTitles(const Source& s) {
    std::vector<std::wstring> t;
    for (I18n::Locale l : {I18n::Locale::Fr, I18n::Locale::En}) for (bool sole : {true, false}) { auto x = FolderTitleIn(l, s, sole); if (std::find(t.begin(), t.end(), x) == t.end()) t.push_back(std::move(x)); }
    return t;
}
// Stable identity of a source (never its displayed label): browser + profile folder (Chromium « Default »,
// « Profile 2 »; Firefox the profiles.ini relative path); all HTML files share one.
inline std::string SourceKey(const Source& s) {
    if (s.browser == Browser::HtmlFile) return "html";
    std::wstring id = s.profile;
    if (s.browser == Browser::Firefox && s.dir.size() > s.root.size() + 1 && !s.root.empty()) id = s.dir.substr(s.root.size() + 1);
    for (auto& c : id) if (c == L'\\' ) c = L'/';
    std::string key = std::string(Chromium(s.browser) ? "chromium:" : "firefox:") + NativeChrome::Downloads::ToUtf8(std::wstring(BrandName(s.browser)) + L"/" + id);
    for (auto& c : key) if (c == '|' || c == '\t') c = '_';
    return key;
}
// Displayed title of a source root, in both languages (fallback matching).
// What a source root IS (never guessed from a title): the browser's own bookmarks bar, its « other » / menu roots, or
// its mobile root. HTML exports name the bar and « other » roots with PERSONAL_TOOLBAR_FOLDER / UNFILED_BOOKMARKS_FOLDER
// on a top-level folder (Chrome, Edge, Brave, Firefox exports); the parser turns those into keys « html:toolbar » /
// « html:unfiled ».
enum class RootRole { None, Bar, Other, Mobile };
inline RootRole RoleOf(const std::string& key) {
    if (key == "bookmark_bar" || key == "toolbar_____" || key == "html:toolbar") return RootRole::Bar;
    if (key == "other" || key == "unfiled_____" || key == "menu________" || key == "html:unfiled") return RootRole::Other;
    if (key == "synced" || key == "mobile______") return RootRole::Mobile;
    return RootRole::None;
}
inline std::vector<std::wstring> RootTitles(const std::string& key) {
    using I18n::Id; Id id;
    if (key == "bookmark_bar" || key == "toolbar_____") id = Id::bookmarks_bar_root;
    else if (key == "other" || key == "unfiled_____") id = Id::bookmarks_other_root;
    else if (key == "synced" || key == "mobile______") id = Id::import_root_mobile;
    else if (key == "menu________") id = Id::import_root_menu;
    else return {};
    return {I18n::In(I18n::Locale::Fr, id), I18n::In(I18n::Locale::En, id)};
}

// ---- Stable destination folders (review 934ef4b, finding 2) ----------------------------------------------------
// `<data>\\bookmark-imports`: which destination folder holds a source (key « chromium:Chrome/Default ») and each of
// its roots (« chromium:Chrome/Default|bookmark_bar »), by folder id + its `added` time (the store's own node
// identity). Displayed titles never identify anything: a later import in the other language, a source that gained
// a profile (its label changes) or a folder the person renamed or moved still lands in the same place. The entry is
// trusted only if that folder still exists with the same id, kind and time; otherwise matching falls back to the
// titles this source could have had (both languages), then creates. The bookmarks file format is unchanged.
// Each key holds an ORDERED list of candidate folders (preferred first), so a change of identity is never destructive
// (review ff1bcbe): when an import maps a key to a new folder, the previous folder stays listed after it as long as
// it still exists in the bookmarks file as read. The map is written before the bookmarks (CommitBookmarks); if the
// bookmarks write then fails or the process stops in between, the file on disk still holds the old folder and the
// next import finds it through the older candidate (the new one points at nothing and is skipped). Both states live
// in the same atomically replaced file: no separate journal. A candidate is dropped only once its folder is gone from
// both the file as read and the result being written; a stale candidate therefore lasts at most one more commit.
struct FolderMap {
    struct Entry { Bookmarks::Id id = 0; std::int64_t added = 0; bool operator==(const Entry& o) const { return id == o.id && added == o.added; } };
    std::map<std::string, std::vector<Entry>> entries;
    static constexpr const char* Header = "browser-bookmark-imports 1";
    // Capacity (review 2ed598c). Every record names a distinct folder that exists in the bookmarks file as read or in
    // the result being written (Prune), and a folder belongs to one key only (Claimed), so valid records can never
    // exceed twice the bookmark store's own node limit: Limit is that bound. CommitBookmarks still checks, before
    // writing anything, that the map it would write fits it and reads back identically (Fits), and refuses otherwise.
    static constexpr std::size_t Limit = 2 * Bookmarks::Store::Limit, Candidates = 4;
    bool operator==(const FolderMap& o) const { return entries == o.entries; }
    bool operator!=(const FolderMap& o) const { return !(*this == o); }
    std::size_t Records() const { std::size_t n = 0; for (const auto& [k, list] : entries) n += list.size(); return n; }
    // `known`: false for a file of another version (left untouched, never rewritten). `complete`: false when the file
    // holds anything this reader would have to drop (more than Limit records, more than Candidates for a key, a
    // damaged line): such a map is never used nor rewritten, the import is refused instead of half-remembered.
    // Lines of one key, in file order, are its candidates in preference order; an exact repeated line is harmless.
    static FolderMap Read(const std::string& bytes, bool* known, bool* complete = nullptr) {
        FolderMap m; if (known) *known = true; if (complete) *complete = true;
        auto partial = [&] { if (complete) *complete = false; };
        if (bytes.empty()) return m;
        std::size_t start = 0, line = 0, kept = 0;
        while (start < bytes.size()) {
            std::size_t end = bytes.find('\n', start); if (end == std::string::npos) end = bytes.size();
            std::string l = bytes.substr(start, end - start); start = end + 1;
            if (!l.empty() && l.back() == '\r') l.pop_back();
            if (line++ == 0) { if (l != Header) { if (known) *known = false; return FolderMap{}; } continue; }
            if (l.empty()) continue;
            const auto a = l.find('\t'), b = a == std::string::npos ? a : l.find('\t', a + 1);
            if (b == std::string::npos || a == 0) { partial(); continue; }
            Entry e;
            try { std::size_t used = 0; const std::string id = l.substr(a + 1, b - a - 1), at = l.substr(b + 1);
                  const unsigned long v = std::stoul(id, &used); if (used != id.size()) throw 0; e.id = static_cast<Bookmarks::Id>(v);
                  e.added = std::stoll(at, &used); if (used != at.size()) throw 0; } catch (...) { partial(); continue; }
            auto& list = m.entries[l.substr(0, a)];
            if (std::find(list.begin(), list.end(), e) != list.end()) continue;
            if (list.size() >= Candidates || kept >= Limit) { partial(); continue; }
            list.push_back(e); ++kept;
        }
        return m;
    }
    std::string Write() const {
        std::string out = std::string(Header) + "\n";
        for (const auto& [k, list] : entries) for (const auto& e : list) out += k + "\t" + std::to_string(e.id) + "\t" + std::to_string(e.added) + "\n";
        return out;
    }
    // What would be written is read back whole and identical (keys, candidates, order) within the reader's bounds.
    bool Fits() const {
        if (Records() > Limit) return false;
        for (const auto& [k, list] : entries) if (k.empty() || list.empty() || list.size() > Candidates || k.find_first_of("\t\r\n") != std::string::npos) return false;
        bool known = false, complete = false; const FolderMap back = Read(Write(), &known, &complete);
        return known && complete && back == *this;
    }
    static const Bookmarks::Node* Holds(const Bookmarks::Store& s, const Entry& e) {
        const Bookmarks::Node* n = s.Find(e.id);
        return n && n->kind == Bookmarks::Kind::Folder && !Bookmarks::Store::IsRoot(n->id) && n->added == e.added ? n : nullptr;
    }
    // The first candidate of `key` whose folder exists in `s` (same id, kind and time).
    const Bookmarks::Node* Valid(const Bookmarks::Store& s, const std::string& key) const {
        const auto it = entries.find(key); if (it == entries.end()) return nullptr;
        for (const auto& e : it->second) if (const Bookmarks::Node* n = Holds(s, e)) return n;
        return nullptr;
    }
    bool Claimed(Bookmarks::Id id, const std::string& except) const { for (const auto& [k, list] : entries) if (k != except) for (const auto& e : list) if (e.id == id) return true; return false; }
    // `e` becomes the preferred candidate of `key`; the others stay after it (bounded).
    void Prefer(const std::string& key, const Entry& e) {
        auto& list = entries[key];
        list.erase(std::remove(list.begin(), list.end(), e), list.end());
        list.insert(list.begin(), e);
        if (list.size() > Candidates) list.resize(Candidates);
    }
    // Keeps a candidate while its folder exists in either store (the file as read, the result being written).
    void Prune(const Bookmarks::Store& a, const Bookmarks::Store& b) {
        for (auto it = entries.begin(); it != entries.end();) {
            auto& list = it->second;
            list.erase(std::remove_if(list.begin(), list.end(), [&](const Entry& e) { return !Holds(a, e) && !Holds(b, e); }), list.end());
            it = list.empty() ? entries.erase(it) : std::next(it);
        }
    }
};

// ---- Chromium « Bookmarks » (JSON) ------------------------------------------------------------------------
// Reader bounds are never a silent cut (review 934ef4b, finding 4): a node deeper than Bound::nesting (root = 1)
// or past Bound::items makes the whole category TooLarge instead of dropping the rest.
inline void ChromiumNode(const StrictJson::Value& v, std::vector<Item>& into, std::size_t depth, std::size_t& count, std::size_t& ignored, Failure& overflow) {
    if (overflow != Failure::None) return;
    if (depth > Bound::nesting) { overflow = Failure::TooDeep; return; }
    if (++count > Bound::items) { overflow = Failure::TooLarge; return; }
    if (!v.IsObject()) { ++ignored; return; }
    const auto* type = v.Get("type");
    Item it;
    if (const auto* n = v.Get("name"); n && n->IsString()) it.title = Widen(n->string);
    if (const auto* d = v.Get("date_added"); d && d->IsString() && !d->string.empty() && d->string.size() < 20 && d->string.find_first_not_of("0123456789") == std::string::npos)
        it.added = ChromiumTime(static_cast<std::int64_t>(std::stoull(d->string)));
    if (const auto* g = v.Get("guid"); g && g->IsString() && !g->string.empty() && g->string.size() <= 64 && g->string.find_first_of("\t\r\n|") == std::string::npos) it.key = "g:" + g->string;
    if (type && type->IsString() && type->string == "url") {
        it.key.clear();
        const auto* u = v.Get("url");
        if (!u || !u->IsString()) { ++ignored; return; }
        it.url = Widen(u->string);
        into.push_back(std::move(it));
    } else if (type && type->IsString() && type->string == "folder") {
        it.folder = true;
        if (const auto* c = v.Get("children"); c && c->IsArray())
            for (const auto& child : c->items) ChromiumNode(child, it.children, depth + 1, count, ignored, overflow);
        into.push_back(std::move(it));
    } else ++ignored;
}
inline Bookmarks_ ParseChromiumBookmarks(const std::string& bytes) {
    Bookmarks_ r;
    StrictJson::Limits limits; limits.maxBytes = Bound::jsonBytes; limits.maxStringBytes = 8u << 20; limits.maxItems = Bound::items * 4; limits.allowBom = true;
    // Parser depth: a node at depth d sits at JSON depth 2d + 1, so the reader's own bound is always reached first;
    // a parser limit hit (deeper, too many items, too long) is still « too large », not « malformed ».
    limits.maxDepth = Bound::nesting * 2 + 64;
    StrictJson::Value doc; std::string error;
    if (!StrictJson::Parse(bytes, doc, &error, limits) || !doc.IsObject()) {
        const bool bound = bytes.size() > Bound::jsonBytes || error.find("too many") != std::string::npos || error.find("too long") != std::string::npos || error.find("too large") != std::string::npos;
        r.failure = error.find("too deep") != std::string::npos ? Failure::TooDeep : bound ? Failure::TooLarge : Failure::Malformed; return r;
    }
    const auto* roots = doc.Get("roots");
    if (!roots || !roots->IsObject()) { r.failure = Failure::Malformed; return r; }
    struct RootName { const char* key; std::wstring title; };
    const RootName names[] = {{"bookmark_bar", TR(bookmarks_bar_root)}, {"other", TR(bookmarks_other_root)}, {"synced", TR(import_root_mobile)}};
    std::size_t count = 0; Failure overflow = Failure::None;
    for (const auto& n : names) {
        const auto* node = roots->Get(n.key);
        if (!node || !node->IsObject()) continue;
        std::vector<Item> one; ChromiumNode(*node, one, 1, count, r.ignored, overflow);
        if (overflow != Failure::None) { r = Bookmarks_{}; r.failure = overflow; return r; }
        if (one.empty() || !one[0].folder || one[0].children.empty()) continue;
        one[0].title = n.title;   // the browser's own root names are replaced by ours, in the interface language
        one[0].key = n.key;       // stable identity of the root (never its displayed name)
        r.roots.push_back(std::move(one[0]));
    }
    return r;
}

// ---- HTML bookmark file (Netscape format) ----------------------------------------------------------------
// A text tokenizer: tags and entities only, nothing is ever rendered or executed. Unknown tags are ignored.
namespace Html {
inline std::wstring Decode(const std::wstring& s) {
    std::wstring out; out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != L'&') { out += s[i]; continue; }
        const auto semi = s.find(L';', i);
        if (semi == std::wstring::npos || semi - i > 10) { out += s[i]; continue; }
        const std::wstring e = s.substr(i + 1, semi - i - 1);
        std::uint32_t cp = 0;
        if (e == L"amp") cp = '&'; else if (e == L"lt") cp = '<'; else if (e == L"gt") cp = '>'; else if (e == L"quot") cp = '"'; else if (e == L"apos") cp = '\''; else if (e == L"nbsp") cp = 0xA0;
        else if (e.size() > 1 && e[0] == L'#') {
            const bool hexa = e[1] == L'x' || e[1] == L'X'; const std::wstring digits = e.substr(hexa ? 2 : 1);
            if (digits.empty() || digits.size() > 7 || digits.find_first_not_of(hexa ? L"0123456789abcdefABCDEF" : L"0123456789") != std::wstring::npos) { out += s[i]; continue; }
            cp = static_cast<std::uint32_t>(std::wcstoul(digits.c_str(), nullptr, hexa ? 16 : 10));
            if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
        } else { out += s[i]; continue; }
        if (cp >= 0x10000) { cp -= 0x10000; out += static_cast<wchar_t>(0xD800 + (cp >> 10)); out += static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)); }
        else out += static_cast<wchar_t>(cp);
        i = semi;
    }
    return out;
}
inline std::wstring Collapse(const std::wstring& s) {   // HTML text: runs of whitespace are one space
    std::wstring out; bool space = false;
    for (wchar_t c : s) { if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n') { space = !out.empty(); continue; } if (space) out += L' '; space = false; out += c; }
    return out;
}
inline std::wstring Lower(std::wstring s) { for (auto& c : s) if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c + 32); return s; }
// Attribute `name` of a tag's inner text (`<A HREF="x" ADD_DATE=1>` → `HREF="x" ADD_DATE=1`).
inline std::wstring Attribute(const std::wstring& tag, const wchar_t* name) {
    const std::wstring low = Lower(tag), key = name;
    for (std::size_t at = 0; (at = low.find(key, at)) != std::wstring::npos; at += key.size()) {
        if (at > 0 && !iswspace(low[at - 1])) continue;
        std::size_t i = at + key.size(); while (i < tag.size() && iswspace(tag[i])) ++i;
        if (i >= tag.size() || tag[i] != L'=') continue;
        ++i; while (i < tag.size() && iswspace(tag[i])) ++i;
        if (i >= tag.size()) return L"";
        if (tag[i] == L'"' || tag[i] == L'\'') { const wchar_t q = tag[i]; const auto e = tag.find(q, i + 1); return Decode(tag.substr(i + 1, (e == std::wstring::npos ? tag.size() : e) - i - 1)); }
        std::size_t e = i; while (e < tag.size() && !iswspace(tag[e]) && tag[e] != L'>') ++e;
        return Decode(tag.substr(i, e - i));
    }
    return L"";
}
inline std::int64_t Number(const std::wstring& s) {
    if (s.empty() || s.size() > 18 || s.find_first_not_of(L"0123456789") != std::wstring::npos) return 0;
    return static_cast<std::int64_t>(std::wcstoll(s.c_str(), nullptr, 10));
}
} // namespace Html
inline std::wstring DecodeHtmlBytes(const std::string& bytes) {
    std::string b = bytes;
    if (b.size() >= 3 && static_cast<unsigned char>(b[0]) == 0xEF && static_cast<unsigned char>(b[1]) == 0xBB && static_cast<unsigned char>(b[2]) == 0xBF) b.erase(0, 3);
    std::wstring w;
    if (NativeChrome::Downloads::FromUtf8(b, w)) return w;
    // Not UTF-8: the legacy Netscape exports are Windows-1252.
    const int n = MultiByteToWideChar(1252, 0, b.data(), static_cast<int>(b.size()), nullptr, 0);
    w.assign(static_cast<std::size_t>(std::max(0, n)), L'\0');
    if (n > 0) MultiByteToWideChar(1252, 0, b.data(), static_cast<int>(b.size()), w.data(), n);
    return w;
}
inline Bookmarks_ ParseHtmlBookmarks(const std::string& bytes) {
    Bookmarks_ r;
    if (bytes.size() > Bound::htmlBytes) { r.failure = Failure::TooLarge; return r; }
    const std::wstring text = DecodeHtmlBytes(bytes);
    if (Html::Lower(text.substr(0, 4096)).find(L"<!doctype netscape-bookmark-file") == std::wstring::npos && Html::Lower(text).find(L"<dl") == std::wstring::npos) { r.failure = Failure::Malformed; return r; }
    Item top; top.folder = true;
    std::vector<Item*> stack{&top};     // open <DL> containers
    std::unique_ptr<Item> pendingFolder; // <H3> seen, its <DL> not yet
    bool sawDl = false; std::size_t count = 0;
    std::size_t i = 0;
    // One lower-cased copy for tag scanning (titles keep their case through `text`).
    const std::wstring lower = Html::Lower(text);
    auto textUntil = [&](const wchar_t* closing) {   // inner text up to </closing ...>, inner tags dropped
        const std::size_t end = lower.find(closing, i);
        const std::wstring raw = text.substr(i, (end == std::wstring::npos ? text.size() : end) - i);
        std::wstring plain; bool inTag = false;
        for (wchar_t c : raw) { if (c == L'<') inTag = true; else if (c == L'>') inTag = false; else if (!inTag) plain += c; }
        if (end == std::wstring::npos) i = text.size();
        else { const std::size_t gt = text.find(L'>', end); i = gt == std::wstring::npos ? text.size() : gt + 1; }
        return Clip(Html::Collapse(Html::Decode(plain)));
    };
    while (i < text.size()) {
        const std::size_t lt = text.find(L'<', i);
        if (lt == std::wstring::npos) break;
        if (lower.compare(lt, 4, L"<!--") == 0) { const auto e = lower.find(L"-->", lt + 4); i = e == std::wstring::npos ? text.size() : e + 3; continue; }
        const std::size_t gt = text.find(L'>', lt);
        if (gt == std::wstring::npos) break;
        const std::wstring inner = text.substr(lt + 1, gt - lt - 1);
        std::size_t nameEnd = 0; while (nameEnd < inner.size() && !iswspace(inner[nameEnd]) && inner[nameEnd] != L'/') ++nameEnd;
        if (nameEnd == 0 && !inner.empty() && inner[0] == L'/') { nameEnd = 1; while (nameEnd < inner.size() && !iswspace(inner[nameEnd])) ++nameEnd; }
        const std::wstring name = Html::Lower(inner.substr(0, nameEnd));
        i = gt + 1;
        if (name == L"script" || name == L"style") { const auto e = lower.find(name == L"script" ? L"</script" : L"</style", i); i = e == std::wstring::npos ? text.size() : e; continue; }
        if (name == L"dl") {
            if (!sawDl && !pendingFolder) { sawDl = true; continue; }   // the outer list is the file itself
            sawDl = true;
            if (stack.size() > Bound::nesting) { r = Bookmarks_{}; r.failure = Failure::TooDeep; return r; }   // its items would sit deeper than the bound
            Item* parent = stack.back();
            if (pendingFolder) { parent->children.push_back(std::move(*pendingFolder)); pendingFolder.reset(); }
            else { Item anonymous; anonymous.folder = true; parent->children.push_back(std::move(anonymous)); }
            stack.push_back(&parent->children.back());
        } else if (name == L"/dl") {
            if (pendingFolder) { stack.back()->children.push_back(std::move(*pendingFolder)); pendingFolder.reset(); }
            if (stack.size() > 1) stack.pop_back();
        } else if (name == L"h3") {
            if (pendingFolder) { stack.back()->children.push_back(std::move(*pendingFolder)); pendingFolder.reset(); }   // an empty folder without <DL>
            if (++count > Bound::items || stack.size() > Bound::nesting) { r = Bookmarks_{}; r.failure = stack.size() > Bound::nesting ? Failure::TooDeep : Failure::TooLarge; return r; }   // depth of an item = stack size
            pendingFolder = std::make_unique<Item>(); pendingFolder->folder = true;
            pendingFolder->added = HtmlTime(Html::Number(Html::Attribute(inner, L"add_date")));
            // The browser's own roots, as its export marks them (any case, any quoting); only at the file's top level, so a
            // folder deeper in the tree is never taken for a root whatever its title or attributes.
            if (stack.size() == 1) {
                if (Html::Lower(Html::Attribute(inner, L"personal_toolbar_folder")) == L"true") pendingFolder->key = "html:toolbar";
                else if (Html::Lower(Html::Attribute(inner, L"unfiled_bookmarks_folder")) == L"true") pendingFolder->key = "html:unfiled";
            }
            pendingFolder->title = textUntil(L"</h3");
        } else if (name == L"a") {
            if (pendingFolder) { stack.back()->children.push_back(std::move(*pendingFolder)); pendingFolder.reset(); }
            if (++count > Bound::items || stack.size() > Bound::nesting) { r = Bookmarks_{}; r.failure = stack.size() > Bound::nesting ? Failure::TooDeep : Failure::TooLarge; return r; }
            Item link; link.url = Clip(AddressBar::Trim(Html::Attribute(inner, L"href")));
            link.added = HtmlTime(Html::Number(Html::Attribute(inner, L"add_date")));
            link.title = textUntil(L"</a");
            if (link.url.empty()) { ++r.ignored; continue; }
            stack.back()->children.push_back(std::move(link));
        }
    }
    if (pendingFolder) stack.back()->children.push_back(std::move(*pendingFolder));
    if (!sawDl) { r.failure = Failure::Malformed; return r; }
    r.roots = std::move(top.children);
    return r;
}

// ---- SQLite sources ---------------------------------------------------------------------------------------
// Firefox places.sqlite: bookmarks under the four user roots (tags and separators are not bookmarks).
inline Bookmarks_ ReadFirefoxBookmarks(Sql::sqlite3* db) {
    Bookmarks_ r;
    struct Row { std::int64_t id, type, parent, position, added; std::wstring title, url, guid; };
    std::map<std::int64_t, Row> rows; std::map<std::int64_t, std::vector<std::int64_t>> kids;
    Sql::Statement q(db, "SELECT b.id, b.type, b.parent, b.position, b.title, b.dateAdded, p.url, b.guid FROM moz_bookmarks b LEFT JOIN moz_places p ON b.fk = p.id ORDER BY b.parent, b.position, b.id LIMIT 200001");
    if (!q.Prepared()) { r.failure = q.Code() == Sql::OK ? Failure::Unsupported : (Sql::CodeFailure(q.Code()) == Failure::Busy ? Failure::Busy : Failure::Unsupported); return r; }
    int rc;
    while ((rc = q.Step()) == Sql::ROW) {
        if (rows.size() >= Bound::bookmarkRows) { r.failure = Failure::TooLarge; return r; }
        Row row{q.Int(0), q.Int(1), q.Int(2), q.Int(3), FirefoxTime(q.Int(5)), Clip(q.Text(4)), q.Null(6) ? L"" : q.Text(6), q.Text(7)};
        kids[row.parent].push_back(row.id);
        rows.emplace(row.id, std::move(row));
    }
    if (rc != Sql::DONE) { r.failure = Sql::CodeFailure(rc); return r; }
    std::map<std::wstring, std::int64_t> byGuid; for (const auto& [id, row] : rows) byGuid[row.guid] = id;
    std::set<std::int64_t> seen; bool overflow = false;
    // `depth`: of the children being filled (the user roots are 1). Past Bound::nesting: TooLarge, never a cut.
    std::function<void(std::int64_t, Item&, std::size_t)> fill = [&](std::int64_t folder, Item& into, std::size_t depth) {
        const auto k = kids.find(folder); if (k == kids.end() || overflow) return;
        if (depth > Bound::nesting) { overflow = true; return; }   // TooDeep
        for (std::int64_t id : k->second) {
            if (!seen.insert(id).second) { ++r.ignored; continue; }   // a cycle in a damaged database
            const Row& row = rows.at(id);
            if (row.type == 1) { if (row.url.empty()) { ++r.ignored; continue; } Item it; it.title = row.title; it.url = Clip(row.url); it.added = row.added; into.children.push_back(std::move(it)); }
            else if (row.type == 2) { Item it; it.folder = true; it.title = row.title; it.added = row.added; if (!row.guid.empty() && row.guid.size() <= 64 && row.guid.find_first_of(L"\t\r\n|") == std::wstring::npos) it.key = "g:" + NativeChrome::Downloads::ToUtf8(row.guid); fill(id, it, depth + 1); into.children.push_back(std::move(it)); }
            // type 3: separator, not a bookmark
        }
    };
    struct RootName { const wchar_t* guid; std::wstring title; };
    const RootName names[] = {{L"toolbar_____", TR(bookmarks_bar_root)}, {L"menu________", TR(import_root_menu)}, {L"unfiled_____", TR(bookmarks_other_root)}, {L"mobile______", TR(import_root_mobile)}};
    for (const auto& n : names) {
        const auto g = byGuid.find(n.guid); if (g == byGuid.end()) continue;
        Item root; root.folder = true; root.title = n.title; root.key = NativeChrome::Downloads::ToUtf8(n.guid); seen.insert(g->second); fill(g->second, root, 2);
        if (overflow) { r = Bookmarks_{}; r.failure = Failure::TooDeep; return r; }
        if (!root.children.empty()) r.roots.push_back(std::move(root));
    }
    return r;
}
// History rows, newest first. Chromium « urls », Firefox « moz_places ». Each query has a variant without
// the `hidden` column (older or forked schemas); any other mismatch is Unsupported.
inline History_ ReadHistory(Sql::sqlite3* db, Browser b) {
    History_ r;
    const bool firefox = b == Browser::Firefox;
    const char* withHidden = firefox
        ? "SELECT url, title, visit_count, last_visit_date FROM moz_places WHERE hidden = 0 AND visit_count > 0 AND last_visit_date > 0 ORDER BY last_visit_date DESC, id LIMIT 50000"
        : "SELECT url, title, visit_count, last_visit_time FROM urls WHERE hidden = 0 AND last_visit_time > 0 ORDER BY last_visit_time DESC, id LIMIT 50000";
    const char* plain = firefox
        ? "SELECT url, title, visit_count, last_visit_date FROM moz_places WHERE visit_count > 0 AND last_visit_date > 0 ORDER BY last_visit_date DESC, id LIMIT 50000"
        : "SELECT url, title, visit_count, last_visit_time FROM urls WHERE last_visit_time > 0 ORDER BY last_visit_time DESC, id LIMIT 50000";
    const char* countWith = firefox ? "SELECT count(*) FROM moz_places WHERE hidden = 0 AND visit_count > 0 AND last_visit_date > 0" : "SELECT count(*) FROM urls WHERE hidden = 0 AND last_visit_time > 0";
    const char* countPlain = firefox ? "SELECT count(*) FROM moz_places WHERE visit_count > 0 AND last_visit_date > 0" : "SELECT count(*) FROM urls WHERE last_visit_time > 0";
    bool hidden = true;
    auto q = std::make_unique<Sql::Statement>(db, withHidden);
    if (!q->Prepared()) {
        if (Sql::CodeFailure(q->Code()) == Failure::Busy) { r.failure = Failure::Busy; return r; }
        hidden = false; q = std::make_unique<Sql::Statement>(db, plain);
        if (!q->Prepared()) { r.failure = Sql::CodeFailure(q->Code()) == Failure::Busy ? Failure::Busy : Failure::Unsupported; return r; }
    }
    int rc;
    while ((rc = q->Step()) == Sql::ROW) {
        Visit v; v.url = q->Text(0); v.title = Clip(q->Text(1));
        const std::int64_t count = q->Int(2); v.visits = static_cast<int>(std::clamp<std::int64_t>(count, 1, 1000000));
        v.last = firefox ? FirefoxTime(q->Int(3)) : ChromiumTime(q->Int(3));
        if (v.last <= 0) { ++r.ignored; continue; }
        r.visits.push_back(std::move(v));
    }
    if (rc != Sql::DONE) { r.failure = Sql::CodeFailure(rc); r.visits.clear(); return r; }
    Sql::Statement total(db, hidden ? countWith : countPlain);
    if (total.Prepared() && total.Step() == Sql::ROW) { const std::int64_t n = total.Int(0); if (n > static_cast<std::int64_t>(r.visits.size() + r.ignored)) r.beyond = static_cast<std::size_t>(n) - r.visits.size() - r.ignored; }
    return r;
}

// ---- Reading a source (after the explicit « Importer ») --------------------------------------------------
struct Options { bool bookmarks = false, history = false; std::wstring tempRoot; };
inline ReadResult Read(const Source& s, const Options& o) {
    ReadResult r;
    // Pin the source's folders for the whole read (HTML: the chosen file's own folder).
    Pin pin;
    const Failure pinned = s.browser == Browser::HtmlFile ? pin.Open(Parent(s.dir), Parent(s.dir)) : pin.Open(s.root, s.dir);
    if (pinned != Failure::None) { if (o.bookmarks) r.bookmarks.failure = pinned; if (o.history) r.history.failure = pinned; return r; }
    if (s.browser == Browser::HtmlFile) {
        if (o.bookmarks) {
            std::string bytes; const Failure f = ReadFileBounded(s.dir, Bound::htmlBytes, bytes, &pin);
            if (f != Failure::None) r.bookmarks.failure = f; else r.bookmarks = ParseHtmlBookmarks(bytes);
            SecureZeroMemory(bytes.data(), bytes.size());
        }
        return r;
    }
    if (Chromium(s.browser)) {
        if (o.bookmarks) {
            std::string bytes; const Failure f = ReadFileBounded(s.dir + L"\\Bookmarks", Bound::jsonBytes, bytes, &pin);
            if (f != Failure::None) r.bookmarks.failure = f; else r.bookmarks = ParseChromiumBookmarks(bytes);
        }
        if (o.history) {
            Snapshot snap; const Failure f = snap.Take(s.dir + L"\\History", o.tempRoot, &pin);
            if (f != Failure::None) r.history.failure = f;
            else { Database db(snap.Path()); if (db.failure() != Failure::None) r.history.failure = db.failure(); else r.history = ReadHistory(db.handle(), s.browser); }
        }
        return r;
    }
    // Firefox: one database for both categories, one snapshot, one transaction.
    if (o.bookmarks || o.history) {
        Snapshot snap; const Failure f = snap.Take(s.dir + L"\\places.sqlite", o.tempRoot, &pin);
        if (f != Failure::None) { if (o.bookmarks) r.bookmarks.failure = f; if (o.history) r.history.failure = f; return r; }
        Database db(snap.Path());
        if (db.failure() != Failure::None) { if (o.bookmarks) r.bookmarks.failure = db.failure(); if (o.history) r.history.failure = db.failure(); return r; }
        if (o.bookmarks) r.bookmarks = ReadFirefoxBookmarks(db.handle());
        if (o.history) r.history = ReadHistory(db.handle(), s.browser);
    }
    return r;
}

// ---- Applying to the destination stores (pure; run inside the profile store's merge) ---------------------
namespace detail {
inline std::size_t Links(const Item& it) { if (!it.folder) return 1; std::size_t n = 0; for (const auto& c : it.children) n += Links(c); return n; }
inline Bookmarks::Id Resolve(Bookmarks::Store& s, FolderMap* map, const std::string& key, Bookmarks::Id parent, const std::vector<std::wstring>& titles,
                             const std::wstring& create, std::int64_t now, Counts& c);
// `map` + `source`: a source folder with a stable id (« g:<guid> ») is found again by identity (renamed or moved by the
// person, it is still the same folder); folders without one are matched by title, in order, as before.
inline void Into(Bookmarks::Store& s, Bookmarks::Id dest, const std::vector<Item>& items, std::int64_t now, Counts& c, FolderMap* map = nullptr, const std::string& source = "") {
    namespace BM = Bookmarks;
    // The destination folder as it is before this pass: same-titled folders in order, link counts per URL key.
    std::map<std::wstring, std::vector<BM::Id>> folders; std::map<std::wstring, std::size_t> links;
    for (BM::Id id : s.Find(dest)->children) {
        const BM::Node* n = s.Find(id);
        if (n->kind == BM::Kind::Folder) folders[n->title].push_back(id); else ++links[BM::UrlKey(n->url)];
    }
    std::map<std::wstring, std::size_t> folderSeen, linkSeen;
    for (const Item& it : items) {
        if (it.folder) {
            const std::size_t k = folderSeen[it.title]++;
            BM::Id target = BM::NoId;
            if (map && !it.key.empty()) {
                const std::size_t before = c.folders;
                target = Resolve(s, map, source + "|" + it.key, dest, {it.title}, it.title, it.added > 0 ? Clamp(it.added, now) : now, c);
                if (target == BM::NoId) { (static_cast<std::size_t>(s.Depth(dest)) + 1 > BM::Store::MaxDepth ? c.deep : c.full) += Links(it); continue; }
                (void)before;
                Into(s, target, it.children, now, c, map, source);
                continue;
            }
            const auto f = folders.find(it.title);
            if (f != folders.end() && k < f->second.size()) target = f->second[k];
            else {
                if (static_cast<std::size_t>(s.Depth(dest)) + 1 > BM::Store::MaxDepth) { c.deep += Links(it); continue; }
                target = s.AddFolder(dest, BM::End, it.title, it.added > 0 ? Clamp(it.added, now) : now);
                if (target == BM::NoId) { c.full += Links(it); continue; }
                ++c.folders;
            }
            Into(s, target, it.children, now, c, map, source);
            continue;
        }
        if (!BM::Admissible(it.url)) { ++c.unsupported; continue; }
        const std::wstring key = BM::UrlKey(it.url);
        if (++linkSeen[key] <= links[key]) { ++c.present; continue; }
        if (s.Add(dest, BM::End, it.title, it.url, it.added > 0 ? Clamp(it.added, now) : now) == BM::NoId) ++c.full; else ++c.imported;
    }
}
} // namespace detail
namespace detail {
// The folder for `key`: the mapped one if still valid (wherever the person moved or however they renamed it), else
// an unclaimed folder of `parent` titled one of `titles`, else a new one titled `create`. NoId: no room (counted).
inline Bookmarks::Id Resolve(Bookmarks::Store& s, FolderMap* map, const std::string& key, Bookmarks::Id parent, const std::vector<std::wstring>& titles,
                             const std::wstring& create, std::int64_t now, Counts& c) {
    namespace BM = Bookmarks;
    if (map) if (const BM::Node* n = map->Valid(s, key)) { map->Prefer(key, FolderMap::Entry{n->id, n->added}); return n->id; }
    BM::Id id = BM::NoId;
    for (BM::Id child : s.Find(parent)->children) {
        const BM::Node* n = s.Find(child);
        if (n->kind != BM::Kind::Folder || std::find(titles.begin(), titles.end(), n->title) == titles.end() || (map && map->Claimed(child, key))) continue;
        id = child; break;
    }
    if (id == BM::NoId) {
        if (static_cast<std::size_t>(s.Depth(parent)) + 1 > BM::Store::MaxDepth) return BM::NoId;
        id = s.AddFolder(parent, BM::End, create, now);
        if (id == BM::NoId) return BM::NoId;
        ++c.folders;
    }
    if (map) map->Prefer(key, FolderMap::Entry{id, s.Find(id)->added});   // the previous candidate stays after it
    return id;
}
} // namespace detail
namespace detail {
inline std::size_t Subtree(const Bookmarks::Store& s, Bookmarks::Id id) { std::size_t n = 0; for (Bookmarks::Id x : s.Subtree(id)) if (s.Find(x)->kind == Bookmarks::Kind::Link) ++n; return n; }
// Moves every child of `from` (same ids, same order) to the end of `to`; counts the links moved.
inline void MoveChildren(Bookmarks::Store& s, Bookmarks::Id from, Bookmarks::Id to, Counts& c) {
    const std::vector<Bookmarks::Id> kids = s.Find(from)->children;
    for (Bookmarks::Id k : kids) { const std::size_t links = Subtree(s, k); if (s.Move(k, to, Bookmarks::End)) c.moved += links; }
}
// A folder the import created and mapped (`key`), now empty: removed. Never a root, never non-empty. Its map entry stays
// until a bookmarks write without it has been confirmed, so a failed write cannot lose the old group's identity.
inline void DropIfEmpty(Bookmarks::Store& s, FolderMap& map, const std::string& key, Bookmarks::Id id) {
    const Bookmarks::Node* n = s.Find(id);
    if (!n || Bookmarks::Store::IsRoot(id) || n->kind != Bookmarks::Kind::Folder || !n->children.empty()) return;
    s.Remove(id); (void)map; (void)key;   // its map entry is pruned only once the bookmarks write confirmed it gone (FolderMap::Prune)
}
// v0.3.376 put every import under « Autres favoris › Importés de <source> › <source root> ». When the same source is
// imported again, its old group is recognised ONLY by provenance — the folder map's validated identity of that source
// (and, for a native root, of that root) — and its content is moved (same ids, same order) where it now belongs before
// the new pass dedupes against it. The old group and root folders are removed only if they are mapped and now empty;
// anything the person added there stays. HTML exports carried no root keys then: inside the mapped HTML group, the
// folder titled exactly like THIS file's toolbar folder is its old bar, and the group's direct entries that match this
// file's other top-level entries (links by address and count, folders by title in order) move to « Autres favoris ».
inline void MigrateLegacy(Bookmarks::Store& s, FolderMap& map, const std::string& source, const std::vector<Item>& roots, Counts& c) {
    namespace BM = Bookmarks;
    const BM::Node* group = map.Valid(s, source);
    if (!group || group->parent != BM::OtherRoot) return;   // only the group v0.3.376 created, where it put it
    const BM::Id box = group->id;
    for (const Item& r : roots) {
        if (r.key.empty()) continue;
        const RootRole role = RoleOf(r.key);
        if (role == RootRole::None) continue;
        const BM::Id target = role == RootRole::Bar ? BM::BarRoot : BM::OtherRoot;
        if (r.key.rfind("html:", 0) == 0) {   // HTML: the old bar folder is the group's folder titled like this file's
            if (role != RootRole::Bar) continue;
            for (BM::Id k : std::vector<BM::Id>(s.Find(box)->children)) {
                const BM::Node* n = s.Find(k);
                if (n->kind == BM::Kind::Folder && n->title == r.title) { MoveChildren(s, k, BM::BarRoot, c); if (s.Find(k)->children.empty()) s.Remove(k); break; }
            }
            continue;
        }
        const std::string key = source + "|" + r.key;
        const BM::Node* old = map.Valid(s, key);
        if (!old || old->parent != box) continue;
        if (role == RootRole::Mobile) { s.Move(old->id, BM::OtherRoot, BM::End); c.moved += Subtree(s, old->id); continue; }   // keeps its own folder
        const BM::Id oldId = old->id;
        MoveChildren(s, oldId, target, c);
        DropIfEmpty(s, map, key, oldId);
    }
    if (source == "html") {   // the old group's direct entries that are this file's other top-level entries
        bool marked = false; for (const Item& r : roots) if (RoleOf(r.key) != RootRole::None) marked = true;
        const BM::Id destination = marked ? BM::OtherRoot : BM::BarRoot;   // where this file's top level now goes
        std::map<std::wstring, std::size_t> wantLinks, wantFolders;
        for (const Item& r : roots) {
            if (RoleOf(r.key) == RootRole::Bar) continue;
            const auto& level = RoleOf(r.key) == RootRole::Other ? r.children : std::vector<Item>{r};
            for (const Item& it : level) { if (it.folder) ++wantFolders[it.title]; else if (BM::Admissible(it.url)) ++wantLinks[BM::UrlKey(it.url)]; }
        }
        for (BM::Id k : std::vector<BM::Id>(s.Find(box)->children)) {
            const BM::Node* n = s.Find(k);
            auto& want = n->kind == BM::Kind::Folder ? wantFolders[n->title] : wantLinks[BM::UrlKey(n->url)];
            if (want == 0) continue;
            --want; const std::size_t links = Subtree(s, k);
            if (s.Move(k, destination, BM::End)) c.moved += links;
        }
    }
    DropIfEmpty(s, map, source, box);
}
} // namespace detail
// Where imported bookmarks go (v0.3.377): by what each source root IS. The browser's bookmarks bar → straight onto
// « Barre de favoris »; its « other » / menu roots → straight into « Autres favoris »; its mobile root → one folder
// « Favoris sur mobile » in « Autres favoris ». The fixed roots are never folders of the map: nothing can delete them.
// An HTML file whose export marks no root (no PERSONAL_TOOLBAR_FOLDER / UNFILED_BOOKMARKS_FOLDER): its top-level
// entries go onto the bar as they are, folders staying folders (no « Importés d'un fichier HTML » group, no title
// guessed as a technical root).
// With `map`, every folder that has a stable identity (mobile folder, guid-keyed source folders, the HTML group) is
// found again by it, and an earlier v0.3.376 group of the same source is corrected first (detail::MigrateLegacy).
inline Counts ApplyBookmarks(Bookmarks::Store& s, const Source& src, const std::vector<Item>& roots, std::int64_t now, FolderMap* map) {
    namespace BM = Bookmarks;
    Counts c;
    if (roots.empty()) return c;
    const std::string key = SourceKey(src);
    bool semantic = false; for (const Item& r : roots) if (RoleOf(r.key) != RootRole::None) semantic = true;
    if (map) detail::MigrateLegacy(s, *map, key, roots, c);
    if (!semantic) { detail::Into(s, BM::BarRoot, roots, now, c, map, key); return c; }   // HTML without root marks
    for (const Item& r : roots) {
        switch (RoleOf(r.key)) {
        case RootRole::Bar: detail::Into(s, BM::BarRoot, r.children, now, c, map, key); break;
        case RootRole::Other: detail::Into(s, BM::OtherRoot, r.children, now, c, map, key); break;
        case RootRole::Mobile: {
            std::vector<std::wstring> rootTitles = RootTitles(r.key);
            if (std::find(rootTitles.begin(), rootTitles.end(), r.title) == rootTitles.end()) rootTitles.insert(rootTitles.begin(), r.title);
            const BM::Id folder = detail::Resolve(s, map, key + "|" + r.key, BM::OtherRoot, rootTitles, r.title, r.added > 0 ? Clamp(r.added, now) : now, c);
            if (folder == BM::NoId) { c.full += detail::Links(r); break; }
            detail::Into(s, folder, r.children, now, c, map, key);
            break;
        }
        default: detail::Into(s, BM::OtherRoot, {r}, now, c, map, key); break;   // an HTML top-level entry beside marked roots
        }
    }
    return c;
}
// Title-only form (no mapping): « Autres favoris › <folderTitle> », matched by that exact title.
inline Counts ApplyBookmarks(Bookmarks::Store& s, const std::wstring& folderTitle, const std::vector<Item>& roots, std::int64_t now) {
    namespace BM = Bookmarks;
    Counts c; std::size_t total = 0; for (const auto& r : roots) total += detail::Links(r);
    if (roots.empty()) return c;
    const BM::Id container = detail::Resolve(s, nullptr, "", BM::OtherRoot, {folderTitle}, folderTitle, now, c);
    if (container == BM::NoId) { c.full = total; return c; }
    detail::Into(s, container, roots, now, c);
    return c;
}
// History rows newest first; `excluded` (search result pages) counts as unsupported.
inline Counts ApplyHistory(AddressBar::History& h, const std::vector<Visit>& visits, std::size_t beyond, std::int64_t now, const std::function<bool(const std::wstring&)>& excluded = {}) {
    Counts c; c.full = beyond;
    for (const auto& v : visits) {
        if (excluded && excluded(v.url)) { ++c.unsupported; continue; }
        switch (h.Import(v.url, v.title, v.visits, Clamp(v.last, now))) {
        case AddressBar::History::Imported::Added: ++c.imported; break;
        case AddressBar::History::Imported::Present: ++c.present; break;
        case AddressBar::History::Imported::Full: ++c.full; break;
        default: ++c.unsupported; break;
        }
    }
    return c;
}

// ---- Commits (one profile-store transaction per category) ---------------------------------------------
// The import is applied INSIDE the store's merge, on the file as it is now under the profile lock, so other
// windows' changes are kept and duplicates are judged against what is really on disk. Each category is its
// own transaction: bookmarks can be saved while history fails (and the reverse); the caller reports each
// one. A failed commit leaves the file, `base` and `mine` untouched, and a retry is idempotent.
// Test seam (backend tests only): runs right after the map is written and before the bookmarks are, so a test can
// stop the thread exactly at that boundary (the state a crash leaves).
inline std::function<void()>& MapWrittenForTest() { static std::function<void()> hook; return hook; }
// `identity`: why the bookmarks category was refused although the bookmarks file itself was fine (see CommitBookmarks).
enum class Identity { Ok, Unwritable, Newer, Damaged, Full };
struct Committed { ProfileStore::Result result = ProfileStore::Result::Failed; Counts counts; Identity identity = Identity::Ok; bool Saved() const { return result == ProfileStore::Result::Written || result == ProfileStore::Result::Unchanged; } };
// With the stable folder map (`mapFile`, normally <data>\\bookmark-imports), one transaction under the profile lock
// (review 99959e3): the map holding every new or changed source identity is written FIRST, after the merge has fixed
// the folder ids and before the bookmarks file is written (ProfileStore's pre-write `acceptable` step). If the map
// cannot be written (locked, unreadable) or is of a newer version (never rewritten), the bookmarks are NOT written and
// the category fails: an import never succeeds without the identity that keeps later renames and languages
// idempotent. The order makes an interruption recoverable: map written, bookmarks not (crash, write failure) leaves
// only entries for folders that do not exist; an entry is trusted only if its folder exists with the same id, kind
// and `added` time, so such an entry is ignored and replaced by the next import. Entries are pruned only when their
// folder is gone both from the file as read and from the result. `mapSaved`: the map now holds this import's identities.
inline Committed CommitBookmarks(const std::wstring& dataDir, const std::wstring& file, const std::wstring& mapFile, Bookmarks::Store& base, Bookmarks::Store& mine,
                                 const Source& src, const std::vector<Item>& roots, std::int64_t now, bool* mapSaved = nullptr,
                                 std::map<Bookmarks::Id, Bookmarks::Id>* remap = nullptr, DWORD timeoutMs = 5000) {
    namespace BM = Bookmarks;
    Committed c; std::map<BM::Id, BM::Id> ids;
    if (mapSaved) *mapSaved = false;
    ProfileStore::Lock lock(dataDir, timeoutMs);   // recursive for this thread: the bookmarks commit below re-enters it
    if (!lock.Held()) { c.result = ProfileStore::Result::Busy; return c; }
    std::string bytes; bool known = true;
    if (!ProfileStore::ReadAll(mapFile, bytes)) { c.identity = Identity::Unwritable; return c; }   // unreadable: never rewritten blind
    bool complete = true;
    const FolderMap before = FolderMap::Read(bytes, &known, &complete);
    if (!known) { c.identity = Identity::Newer; return c; }   // another version's map: left as is, nothing imported
    if (!complete) { c.identity = Identity::Damaged; return c; }   // never used half-read, never rewritten
    FolderMap map = before; BM::Store theirs;
    bool mapOk = true, mapFull = false;
    c.result = ProfileStore::Commit<BM::Store>(dataDir, file, base, mine, [&](const BM::Store& b, const BM::Store& m, const BM::Store& t) {
        BM::Store r = BM::Store::Merge(b, m, t, &ids);
        map = before; theirs = t;   // the merge may run again: start from the file's map each time
        c.counts = ApplyBookmarks(r, src, roots, now, &map);
        return r;
    }, timeoutMs, {"browser-bookmarks 1"}, true, [&](const BM::Store& merged) {
        // Pre-write step: the identities first. A failure here writes nothing at all.
        map.Prune(merged, theirs);
        if (!map.Fits()) { mapFull = true; return false; }   // over the reader's bounds: nothing written at all
        mapOk = map == before || ProfileStore::WriteAtomic(mapFile, map.Write());
        if (mapOk && MapWrittenForTest()) MapWrittenForTest()();
        return mapOk;
    });
    if (remap) *remap = ids;
    if (mapFull) c.identity = Identity::Full; else if (!mapOk) c.identity = Identity::Unwritable;
    if (!c.Saved()) { c.counts = Counts{}; return c; }
    if (mapSaved) *mapSaved = true;
    return c;
}
inline Committed CommitBookmarks(const std::wstring& dataDir, const std::wstring& file, Bookmarks::Store& base, Bookmarks::Store& mine,
                                 const std::wstring& folderTitle, const std::vector<Item>& roots, std::int64_t now,
                                 std::map<Bookmarks::Id, Bookmarks::Id>* remap = nullptr, DWORD timeoutMs = 5000) {
    namespace BM = Bookmarks;
    Committed c; std::map<BM::Id, BM::Id> ids;
    c.result = ProfileStore::Commit<BM::Store>(dataDir, file, base, mine, [&](const BM::Store& b, const BM::Store& m, const BM::Store& t) {
        BM::Store r = BM::Store::Merge(b, m, t, &ids);
        c.counts = ApplyBookmarks(r, folderTitle, roots, now);
        return r;
    }, timeoutMs, {"browser-bookmarks 1"});
    if (remap) *remap = ids;
    if (!c.Saved()) c.counts = Counts{};
    return c;
}
inline Committed CommitHistory(const std::wstring& dataDir, const std::wstring& file, AddressBar::History& base, AddressBar::History& mine,
                               const std::vector<Visit>& visits, std::size_t beyond, std::int64_t now,
                               const std::function<bool(const std::wstring&)>& excluded = {}, DWORD timeoutMs = 5000) {
    using H = AddressBar::History;
    Committed c;
    // Same reader, writer and damage rule as the browser's own history commit (main.cpp SaveHistory):
    // a damaged history file is backed up and never rewritten.
    c.result = ProfileStore::CommitWith<H>(dataDir, file, base, mine,
        [](std::istream& in, bool* damaged) { std::string b((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()); std::istringstream i2(b); auto h = H::Read(i2); if (damaged) *damaged = !b.empty() && ProfileStore::RecordLines(b) != h.Items().size(); return h; },
        [](std::ostream& out, const H& h) { h.Write(out); },
        [&](const H& b, const H& m, const H& t) { H r = H::Merge(b, m, t); c.counts = ApplyHistory(r, visits, beyond, now, excluded); return r; },
        timeoutMs, {"browser-history 1"}, false);
    if (!c.Saved()) c.counts = Counts{};
    return c;
}

// ---- Report text ----------------------------------------------------------------------------------------
inline std::wstring FailureText(Failure f, const Source& s) {
    switch (f) {
    case Failure::Missing: return TR(import_failure_missing);
    case Failure::Busy: return s.browser == Browser::HtmlFile ? std::wstring(TR(import_failure_file_busy)) : TRF(import_failure_busy, BrandName(s.browser));
    case Failure::Malformed: return TR(import_failure_malformed);
    case Failure::TooLarge: return TR(import_failure_too_large);
    case Failure::TooDeep: return TR(import_failure_too_deep);
    case Failure::Unsupported: return TR(import_failure_unsupported);
    case Failure::NoDatabase: return TR(import_failure_no_database);
    case Failure::Unreadable: return TR(import_failure_unreadable);
    default: return L"";
    }
}
// « 12 importés · 3 déjà présents · 1 non pris en charge · 2 non importés (plein) »
inline std::wstring CountsText(const Counts& c) {
    std::vector<std::wstring> parts;
    parts.push_back(TRP(import_count_imported, c.imported));
    if (c.present) parts.push_back(TRP(import_count_present, c.present));
    if (c.unsupported) parts.push_back(TRP(import_count_unsupported, c.unsupported));
    if (c.full) parts.push_back(TRP(import_count_full, c.full));
    if (c.deep) parts.push_back(TRP(import_count_deep, c.deep));
    if (c.unreadable) parts.push_back(TRP(import_count_unreadable, c.unreadable));
    if (c.moved) parts.push_back(TRP(import_count_moved, c.moved));
    std::wstring out;
    for (const auto& p : parts) { if (!out.empty()) out += L" · "; out += p; }
    return out;
}

} // namespace BrowserImport
