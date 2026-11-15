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
};
struct Visit { std::wstring url, title; int visits = 1; std::int64_t last = 0; };   // last: Unix ms

enum class Failure {
    None,
    Missing,       // the file is not there (any more)
    Busy,          // the source browser has it open: close it and retry
    Unreadable,    // access denied, I/O error, reparse point, not a regular file
    Malformed,     // not the expected format
    TooLarge,      // over the bounds
    Unsupported,   // a schema this build does not know
    NoDatabase,    // winsqlite3.dll unavailable on this Windows
};
struct Source {
    Browser browser = Browser::HtmlFile;
    std::wstring dir;               // profile folder (Chromium, Firefox) or the HTML file
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
    bool operator==(const Counts& o) const { return imported == o.imported && present == o.present && unsupported == o.unsupported && full == o.full && deep == o.deep && folders == o.folders; }
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
// Whole file, read-only with full sharing (a JSON/HTML file is replaced by rename, never edited in place).
inline Failure ReadFileBounded(const std::wstring& path, std::uint64_t limit, std::string& out) {
    out.clear();
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (f == INVALID_HANDLE_VALUE) return OpenFailure(GetLastError());
    Failure r = Failure::None; LARGE_INTEGER size{};
    if (!PlainHandle(f) || !GetFileSizeEx(f, &size)) r = Failure::Unreadable;
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
    Failure Take(const std::wstring& database, const std::wstring& tempRoot = L"") {
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
            if (!PlainHandle(p.h) || !GetFileSizeEx(p.h, &size)) { r = Failure::Unreadable; break; }
            total += static_cast<std::uint64_t>(size.QuadPart);
            if (total > Bound::databaseBytes) { r = Failure::TooLarge; break; }
        }
        if (r == Failure::None && !MakeFolder(tempRoot)) r = Failure::Unreadable;
        for (auto& p : parts) {
            if (r == Failure::None && p.h != INVALID_HANDLE_VALUE && !CopyHandle(p.h, folder_ + L"\\db" + p.suffix)) r = Failure::Unreadable;
        }
        for (auto& p : parts) if (p.h != INVALID_HANDLE_VALUE) CloseHandle(p.h);
        if (r != Failure::None) Clean();
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
        for (int attempt = 0; attempt < 16; ++attempt) {
            LARGE_INTEGER c{}; QueryPerformanceCounter(&c);
            wchar_t name[64]; swprintf(name, 64, L"\\browser-import-%lu-%llx", GetCurrentProcessId(), static_cast<unsigned long long>(c.QuadPart) + attempt);
            const std::wstring f = root + name;
            if (CreateDirectoryW(f.c_str(), nullptr)) { folder_ = f; return true; }
            if (GetLastError() != ERROR_ALREADY_EXISTS) return false;
        }
        return false;
    }
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
        Source s; s.browser = b; s.dir = root + L"\\" + name; s.profile = name; s.isDefault = n < 0;
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
        Source s; s.browser = Browser::Firefox; s.dir = root + L"\\" + rel; s.profile = e.name.empty() ? rel : e.name; s.isDefault = e.isDefault;
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
inline std::wstring Label(const Source& s) {
    if (s.browser == Browser::HtmlFile) return TR(import_source_html);
    if (s.soleProfile) return BrandName(s.browser);
    const std::wstring profile = Chromium(s.browser) && s.isDefault ? std::wstring(TR(import_profile_default)) : s.profile;
    return TRF(import_source_profile, BrandName(s.browser), profile);
}
inline std::wstring FolderTitle(const Source& s) {
    if (s.browser == Browser::HtmlFile) return TR(import_folder_html);
    return TRF(import_folder, Label(s));
}

// ---- Chromium « Bookmarks » (JSON) ------------------------------------------------------------------------
inline void ChromiumNode(const StrictJson::Value& v, std::vector<Item>& into, std::size_t depth, std::size_t& count, std::size_t& ignored) {
    if (!v.IsObject() || ++count > Bound::items) { ++ignored; return; }
    const auto* type = v.Get("type");
    Item it;
    if (const auto* n = v.Get("name"); n && n->IsString()) it.title = Widen(n->string);
    if (const auto* d = v.Get("date_added"); d && d->IsString() && !d->string.empty() && d->string.size() < 20 && d->string.find_first_not_of("0123456789") == std::string::npos)
        it.added = ChromiumTime(static_cast<std::int64_t>(std::stoull(d->string)));
    if (type && type->IsString() && type->string == "url") {
        const auto* u = v.Get("url");
        if (!u || !u->IsString()) { ++ignored; return; }
        it.url = Widen(u->string);
        into.push_back(std::move(it));
    } else if (type && type->IsString() && type->string == "folder") {
        it.folder = true;
        if (const auto* c = v.Get("children"); c && c->IsArray() && depth < Bound::nesting)
            for (const auto& child : c->items) ChromiumNode(child, it.children, depth + 1, count, ignored);
        into.push_back(std::move(it));
    } else ++ignored;
}
inline Bookmarks_ ParseChromiumBookmarks(const std::string& bytes) {
    Bookmarks_ r;
    StrictJson::Limits limits; limits.maxBytes = Bound::jsonBytes; limits.maxDepth = Bound::nesting * 2 + 8; limits.maxStringBytes = 8u << 20; limits.maxItems = Bound::items * 4; limits.allowBom = true;
    StrictJson::Value doc;
    if (!StrictJson::Parse(bytes, doc, nullptr, limits) || !doc.IsObject()) { r.failure = bytes.size() > Bound::jsonBytes ? Failure::TooLarge : Failure::Malformed; return r; }
    const auto* roots = doc.Get("roots");
    if (!roots || !roots->IsObject()) { r.failure = Failure::Malformed; return r; }
    struct RootName { const char* key; std::wstring title; };
    const RootName names[] = {{"bookmark_bar", TR(bookmarks_bar_root)}, {"other", TR(bookmarks_other_root)}, {"synced", TR(import_root_mobile)}};
    std::size_t count = 0;
    for (const auto& n : names) {
        const auto* node = roots->Get(n.key);
        if (!node || !node->IsObject()) continue;
        std::vector<Item> one; ChromiumNode(*node, one, 1, count, r.ignored);
        if (one.empty() || !one[0].folder || one[0].children.empty()) continue;
        one[0].title = n.title;   // the browser's own root names are replaced by ours, in the interface language
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
            if (stack.size() > Bound::nesting) { r.failure = Failure::TooLarge; return r; }
            Item* parent = stack.back();
            if (pendingFolder) { parent->children.push_back(std::move(*pendingFolder)); pendingFolder.reset(); }
            else { Item anonymous; anonymous.folder = true; parent->children.push_back(std::move(anonymous)); }
            stack.push_back(&parent->children.back());
        } else if (name == L"/dl") {
            if (pendingFolder) { stack.back()->children.push_back(std::move(*pendingFolder)); pendingFolder.reset(); }
            if (stack.size() > 1) stack.pop_back();
        } else if (name == L"h3") {
            if (pendingFolder) { stack.back()->children.push_back(std::move(*pendingFolder)); pendingFolder.reset(); }   // an empty folder without <DL>
            if (++count > Bound::items) { r.failure = Failure::TooLarge; return r; }
            pendingFolder = std::make_unique<Item>(); pendingFolder->folder = true;
            pendingFolder->added = HtmlTime(Html::Number(Html::Attribute(inner, L"add_date")));
            pendingFolder->title = textUntil(L"</h3");
        } else if (name == L"a") {
            if (pendingFolder) { stack.back()->children.push_back(std::move(*pendingFolder)); pendingFolder.reset(); }
            if (++count > Bound::items) { r.failure = Failure::TooLarge; return r; }
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
    std::set<std::int64_t> seen;
    std::function<void(std::int64_t, Item&, std::size_t)> fill = [&](std::int64_t folder, Item& into, std::size_t depth) {
        const auto k = kids.find(folder); if (k == kids.end()) return;
        for (std::int64_t id : k->second) {
            if (!seen.insert(id).second) { ++r.ignored; continue; }   // a cycle in a damaged database
            const Row& row = rows.at(id);
            if (row.type == 1) { if (row.url.empty()) { ++r.ignored; continue; } Item it; it.title = row.title; it.url = Clip(row.url); it.added = row.added; into.children.push_back(std::move(it)); }
            else if (row.type == 2) { Item it; it.folder = true; it.title = row.title; it.added = row.added; if (depth < Bound::nesting) fill(id, it, depth + 1); into.children.push_back(std::move(it)); }
            // type 3: separator, not a bookmark
        }
    };
    struct RootName { const wchar_t* guid; std::wstring title; };
    const RootName names[] = {{L"toolbar_____", TR(bookmarks_bar_root)}, {L"menu________", TR(import_root_menu)}, {L"unfiled_____", TR(bookmarks_other_root)}, {L"mobile______", TR(import_root_mobile)}};
    for (const auto& n : names) {
        const auto g = byGuid.find(n.guid); if (g == byGuid.end()) continue;
        Item root; root.folder = true; root.title = n.title; seen.insert(g->second); fill(g->second, root, 1);
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
    if (!o.bookmarks) r.bookmarks.failure = Failure::None;
    if (s.browser == Browser::HtmlFile) {
        if (o.bookmarks) {
            std::string bytes; const Failure f = ReadFileBounded(s.dir, Bound::htmlBytes, bytes);
            if (f != Failure::None) r.bookmarks.failure = f; else r.bookmarks = ParseHtmlBookmarks(bytes);
            SecureZeroMemory(bytes.data(), bytes.size());
        }
        return r;
    }
    if (Chromium(s.browser)) {
        if (o.bookmarks) {
            std::string bytes; const Failure f = ReadFileBounded(s.dir + L"\\Bookmarks", Bound::jsonBytes, bytes);
            if (f != Failure::None) r.bookmarks.failure = f; else r.bookmarks = ParseChromiumBookmarks(bytes);
        }
        if (o.history) {
            Snapshot snap; const Failure f = snap.Take(s.dir + L"\\History", o.tempRoot);
            if (f != Failure::None) r.history.failure = f;
            else { Database db(snap.Path()); if (db.failure() != Failure::None) r.history.failure = db.failure(); else r.history = ReadHistory(db.handle(), s.browser); }
        }
        return r;
    }
    // Firefox: one database for both categories, one snapshot, one transaction.
    if (o.bookmarks || o.history) {
        Snapshot snap; const Failure f = snap.Take(s.dir + L"\\places.sqlite", o.tempRoot);
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
inline void Into(Bookmarks::Store& s, Bookmarks::Id dest, const std::vector<Item>& items, std::int64_t now, Counts& c) {
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
            const auto f = folders.find(it.title);
            if (f != folders.end() && k < f->second.size()) target = f->second[k];
            else {
                if (static_cast<std::size_t>(s.Depth(dest)) + 1 > BM::Store::MaxDepth) { c.deep += Links(it); continue; }
                target = s.AddFolder(dest, BM::End, it.title, it.added > 0 ? Clamp(it.added, now) : now);
                if (target == BM::NoId) { c.full += Links(it); continue; }
                ++c.folders;
            }
            Into(s, target, it.children, now, c);
            continue;
        }
        if (!BM::Admissible(it.url)) { ++c.unsupported; continue; }
        const std::wstring key = BM::UrlKey(it.url);
        if (++linkSeen[key] <= links[key]) { ++c.present; continue; }
        if (s.Add(dest, BM::End, it.title, it.url, it.added > 0 ? Clamp(it.added, now) : now) == BM::NoId) ++c.full; else ++c.imported;
    }
}
} // namespace detail
// Bookmarks go to « Autres favoris › <folderTitle> » (created when missing, reused when present).
inline Counts ApplyBookmarks(Bookmarks::Store& s, const std::wstring& folderTitle, const std::vector<Item>& roots, std::int64_t now) {
    namespace BM = Bookmarks;
    Counts c; std::size_t total = 0; for (const auto& r : roots) total += detail::Links(r);
    if (roots.empty()) return c;
    BM::Id container = BM::NoId;
    for (BM::Id id : s.Find(BM::OtherRoot)->children) { const BM::Node* n = s.Find(id); if (n->kind == BM::Kind::Folder && n->title == folderTitle) { container = id; break; } }
    if (container == BM::NoId) {
        container = s.AddFolder(BM::OtherRoot, BM::End, folderTitle, now);
        if (container == BM::NoId) { c.full = total; return c; }
        ++c.folders;
    }
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
struct Committed { ProfileStore::Result result = ProfileStore::Result::Failed; Counts counts; bool Saved() const { return result == ProfileStore::Result::Written || result == ProfileStore::Result::Unchanged; } };
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
    std::wstring out;
    for (const auto& p : parts) { if (!out.empty()) out += L" · "; out += p; }
    return out;
}

} // namespace BrowserImport
