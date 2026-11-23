// Site icons of imported bookmarks (bug: bookmarks imported from another browser showed no icon on the bar).
// src/browser-import.h « Site icons » (the source's own icons, read with the bookmarks) and src/import-icons.h (saved
// into the profile's favicons folder; the missing ones fetched from the site). Windowless, on SYNTHETIC data only:
// fake Chromium / Firefox profiles and an HTML export created under the work folder, and small HTTP servers on the
// loopback (127.0.0.1, ephemeral ports) for the fetch. Never reads %LOCALAPPDATA% / %APPDATA%, never asks a real site.
// Usage: import_icons_test.exe <work folder>   (deleted and recreated; nothing outside it is touched)
#include <winsock2.h>
#include <ws2tcpip.h>
#include "import-icons.h"
#include <cstdio>
#include <cstdlib>
#include <mutex>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #x); std::exit(1); } } while (0)
static int checks = 0;
#define OK(x) do { CHECK(x); ++checks; } while (0)

using namespace BrowserImport;
namespace II = ImportIcons;

static std::wstring work;

static void MakeDirs(const std::wstring& path) {
    for (std::size_t i = 3; i <= path.size(); ++i) if (i == path.size() || path[i] == L'\\') CreateDirectoryW(path.substr(0, i).c_str(), nullptr);
}
static void Put(const std::wstring& path, const std::string& bytes) {
    MakeDirs(path.substr(0, path.rfind(L'\\')));
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(f != INVALID_HANDLE_VALUE); DWORD put = 0; CHECK(WriteFile(f, bytes.data(), static_cast<DWORD>(bytes.size()), &put, nullptr) && put == bytes.size()); CloseHandle(f);
}
static std::string Get(const std::wstring& path) {
    std::string b; HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return b;
    LARGE_INTEGER s{}; GetFileSizeEx(f, &s); b.resize(static_cast<std::size_t>(s.QuadPart)); DWORD got = 0;
    if (!b.empty()) ReadFile(f, b.data(), static_cast<DWORD>(b.size()), &got, nullptr);
    CloseHandle(f); return b;
}
static void RemoveTree(const std::wstring& dir) {
    WIN32_FIND_DATAW fd{}; HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring n = fd.cFileName; if (n == L"." || n == L"..") continue;
            const std::wstring p = dir + L"\\" + n;
            SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) RemoveTree(p);
            else if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) RemoveDirectoryW(p.c_str());
            else DeleteFileW(p.c_str());
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir.c_str());
}
// Every file (name → bytes) under `dir`: proves a source is left exactly as it was.
static std::map<std::wstring, std::string> Tree(const std::wstring& dir, const std::wstring& rel = L"") {
    std::map<std::wstring, std::string> out; WIN32_FIND_DATAW fd{}; HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        const std::wstring n = fd.cFileName; if (n == L"." || n == L"..") continue;
        const std::wstring p = dir + L"\\" + n, r = rel + L"\\" + n;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { out[r + L"\\"] = ""; auto sub = Tree(p, r); out.insert(sub.begin(), sub.end()); }
        else out[r] = Get(p);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return out;
}
static std::size_t Leftovers(const std::wstring& temp) { std::size_t n = 0; for (const auto& [k, v] : Tree(temp)) if (k.find(L"browser-import-") != std::wstring::npos) ++n; return n; }

// ---- Image fixtures: right signatures, a readable tag after them (the reader never decodes) ----------------------
static std::string Png(const std::string& tag) { return std::string("\x89PNG\r\n\x1a\n", 8) + "IHDR-" + tag; }
static std::string Ico(const std::string& tag) { return std::string("\0\0\1\0\1\0", 6) + "ICO-" + tag; }
static const std::string Svg = "<svg xmlns=\"http://www.w3.org/2000/svg\"></svg>";
static std::string Base64(const std::string& b) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o; std::size_t i = 0;
    for (; i + 2 < b.size(); i += 3) { const unsigned v = (unsigned char)b[i] << 16 | (unsigned char)b[i + 1] << 8 | (unsigned char)b[i + 2]; o += t[v >> 18]; o += t[(v >> 12) & 63]; o += t[(v >> 6) & 63]; o += t[v & 63]; }
    if (b.size() - i == 1) { const unsigned v = (unsigned char)b[i] << 16; o += t[v >> 18]; o += t[(v >> 12) & 63]; o += "=="; }
    else if (b.size() - i == 2) { const unsigned v = (unsigned char)b[i] << 16 | (unsigned char)b[i + 1] << 8; o += t[v >> 18]; o += t[(v >> 12) & 63]; o += t[(v >> 6) & 63]; o += '='; }
    return o;
}
static std::string Hex(const std::string& b) { std::string o = "X'"; static const char* h = "0123456789ABCDEF"; for (unsigned char c : b) { o += h[c >> 4]; o += h[c & 15]; } return o + "'"; }

// ---- SQLite fixtures (winsqlite3) ---------------------------------------------------------------------------------
struct Db {
    Sql::sqlite3* db = nullptr;
    explicit Db(const std::wstring& path) { MakeDirs(path.substr(0, path.rfind(L'\\'))); CHECK(Sql::Get().Ready()); CHECK(Sql::Get().open_v2(Sql::FileUri(path, "").c_str(), &db, Sql::OPEN_READWRITE | 0x4 /* CREATE */ | Sql::OPEN_URI, nullptr) == Sql::OK); }
    ~Db() { if (db) Sql::Get().close(db); }
    void Exec(const std::string& sql) { char* err = nullptr; if (Sql::Get().exec(db, sql.c_str(), nullptr, nullptr, &err) != Sql::OK) { std::fprintf(stderr, "SQL: %s\n", sql.c_str()); std::exit(1); } }
};
static std::string CLink(const std::string& name, const std::string& url) { return "{\"type\":\"url\",\"name\":\"" + name + "\",\"url\":\"" + url + "\",\"date_added\":\"13300000000000000\",\"guid\":\"x\",\"id\":\"1\"}"; }
static std::string CFolder(const std::string& name, const std::vector<std::string>& kids, const std::string& guid = "") {
    std::string k; for (const auto& c : kids) { if (!k.empty()) k += ","; k += c; }
    return "{\"type\":\"folder\",\"name\":\"" + name + "\",\"children\":[" + k + "],\"date_added\":\"13300000000000000\"" + (guid.empty() ? "" : ",\"guid\":\"" + guid + "\"") + "}";
}
// A Chromium profile: Bookmarks + Favicons. Expected icons: a32, b48, c0, d24, e32 (see the comments).
static void ChromiumProfile(const std::wstring& dir) {
    const std::string bar = CFolder("Bookmarks bar", {CLink("A", "https://a.example/page"), CLink("B", "https://b.example/"),
        CFolder("Dossier", {CLink("C", "https://c.example/x")}, "g-1"), CLink("D", "https://www.d.example/"), CLink("JS", "javascript:void(0)")});
    const std::string other = CFolder("Other bookmarks", {CLink("E", "https://e.example/"), CLink("F", "https://f.example/"), CLink("File", "file:///C:/notes.html")});
    Put(dir + L"\\Bookmarks", "{\"roots\":{\"bookmark_bar\":" + bar + ",\"other\":" + other + ",\"synced\":" + CFolder("Mobile bookmarks", {}) + "},\"version\":1}");
    Db d(dir + L"\\Favicons");
    d.Exec("CREATE TABLE favicons(id INTEGER PRIMARY KEY, url LONGVARCHAR NOT NULL, icon_type INTEGER DEFAULT 1)");
    d.Exec("CREATE TABLE favicon_bitmaps(id INTEGER PRIMARY KEY, icon_id INTEGER NOT NULL, last_updated INTEGER DEFAULT 0, image_data BLOB, width INTEGER DEFAULT 0, height INTEGER DEFAULT 0, last_requested INTEGER DEFAULT 0)");
    d.Exec("CREATE INDEX favicon_bitmaps_icon_id ON favicon_bitmaps(icon_id)");
    d.Exec("CREATE TABLE icon_mapping(id INTEGER PRIMARY KEY, page_url LONGVARCHAR NOT NULL, icon_id INTEGER)");
    d.Exec("INSERT INTO favicons VALUES(1, 'https://a.example/favicon.ico', 1), (2, 'https://a.example/touch.png', 2), (3, 'https://b.example/favicon.ico', 1), (4, 'https://c.example/i.png', 1),"
           " (5, 'https://d.example/i.png', 1), (6, 'https://e.example/broken', 1), (7, 'https://e.example/favicon.ico', 1)");
    // a: the favicon type of its bookmarked page beats the touch icon of the same page; 32 px beats 16 and 64.
    d.Exec("INSERT INTO icon_mapping(page_url, icon_id) VALUES('https://a.example/page', 2), ('https://a.example/page', 1)");
    // b: only another page of the site has an icon; 48 px beats 16. c: no width known. d: « www. » is the same site.
    d.Exec("INSERT INTO icon_mapping(page_url, icon_id) VALUES('https://b.example/other', 3), ('https://c.example/x', 4), ('https://www.d.example/', 5)");
    // e: its own page's icon is not an image: the site's other icon is taken. zzz: not bookmarked, never read out.
    d.Exec("INSERT INTO icon_mapping(page_url, icon_id) VALUES('https://e.example/', 6), ('https://e.example/z', 7), ('https://zzz.example/', 3)");
    auto bitmap = [&](int icon, const std::string& data, int width) { d.Exec("INSERT INTO favicon_bitmaps(icon_id, image_data, width, height) VALUES(" + std::to_string(icon) + ", " + data + ", " + std::to_string(width) + ", " + std::to_string(width) + ")"); };
    bitmap(1, Hex(Png("a16")), 16); bitmap(1, Hex(Png("a64")), 64); bitmap(1, Hex(Png("a32")), 32);
    bitmap(2, Hex(Png("a-touch")), 180);
    bitmap(3, Hex(Png("b16")), 16); bitmap(3, Hex(Png("b48")), 48);
    bitmap(4, "NULL", 32); bitmap(4, Hex(Png("c0")), 0);
    bitmap(5, Hex(Png("d24")), 24);
    bitmap(6, Hex(Svg), 32);
    bitmap(7, Hex(Png("e32")), 32);
}
// A Firefox profile: places.sqlite (toolbar g, h, i) + favicons.sqlite. Expected: g32, h's root ICO; i none (SVG only).
static void FirefoxProfile(const std::wstring& dir) {
    {
        Db d(dir + L"\\places.sqlite");
        d.Exec("CREATE TABLE moz_places(id INTEGER PRIMARY KEY, url LONGVARCHAR, title LONGVARCHAR, visit_count INTEGER DEFAULT 0, hidden INTEGER DEFAULT 0 NOT NULL, last_visit_date INTEGER)");
        d.Exec("CREATE TABLE moz_bookmarks(id INTEGER PRIMARY KEY, type INTEGER, fk INTEGER DEFAULT NULL, parent INTEGER, position INTEGER, title LONGVARCHAR, dateAdded INTEGER, guid TEXT)");
        d.Exec("INSERT INTO moz_places VALUES(1, 'https://g.example/a', 'G', 1, 0, 1700000000000000), (2, 'https://h.example/', 'H', 1, 0, 1700000000000000), (3, 'https://i.example/', 'I', 1, 0, 1700000000000000)");
        d.Exec("INSERT INTO moz_bookmarks VALUES(1, 2, NULL, 0, 0, '', 1, 'root________'), (2, 2, NULL, 1, 0, 'toolbar', 1, 'toolbar_____'),"
               " (10, 1, 1, 2, 0, 'G', 1, 'b1'), (11, 1, 2, 2, 1, 'H', 1, 'b2'), (12, 1, 3, 2, 2, 'I', 1, 'b3')");
    }
    Db d(dir + L"\\favicons.sqlite");
    d.Exec("CREATE TABLE moz_icons(id INTEGER PRIMARY KEY, icon_url TEXT NOT NULL, fixed_icon_url_hash INTEGER NOT NULL, width INTEGER NOT NULL DEFAULT 0, root INTEGER NOT NULL DEFAULT 0, color INTEGER, expire_ms INTEGER NOT NULL DEFAULT 0, data BLOB)");
    d.Exec("CREATE TABLE moz_pages_w_icons(id INTEGER PRIMARY KEY, page_url TEXT NOT NULL, page_url_hash INTEGER NOT NULL)");
    d.Exec("CREATE TABLE moz_icons_to_pages(page_id INTEGER NOT NULL, icon_id INTEGER NOT NULL, expire_ms INTEGER NOT NULL DEFAULT 0, PRIMARY KEY(page_id, icon_id)) WITHOUT ROWID");
    d.Exec("INSERT INTO moz_icons(id, icon_url, fixed_icon_url_hash, width, root, data) VALUES(1, 'https://g.example/i.ico', 1, 16, 0, " + Hex(Png("g16")) + "), (2, 'https://g.example/i.ico', 1, 32, 0, " + Hex(Png("g32")) + "),"
           " (3, 'https://g.example/i.svg', 2, 65535, 0, " + Hex(Svg) + "), (4, 'https://h.example/favicon.ico', 3, 16, 1, " + Hex(Ico("h16")) + "), (5, 'https://i.example/i.svg', 4, 65535, 0, " + Hex(Svg) + ")");
    d.Exec("INSERT INTO moz_pages_w_icons VALUES(1, 'https://g.example/a', 1), (2, 'https://i.example/', 2)");
    d.Exec("INSERT INTO moz_icons_to_pages(page_id, icon_id) VALUES(1, 1), (1, 2), (1, 3), (2, 5)");
}

// ---- A tiny HTTP/1.1 server on the loopback (one connection at a time, « Connection: close ») -----------------------
struct Route { int status = 200; std::string body, headers; };
struct Server {
    SOCKET listener = INVALID_SOCKET; int port = 0; HANDLE thread = nullptr;
    std::map<std::string, Route> routes; std::mutex lock; std::vector<std::string> requests;   // request heads, in order
    explicit Server(std::map<std::string, Route> r) : routes(std::move(r)) {
        listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); CHECK(listener != INVALID_SOCKET);
        sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = 0;
        CHECK(bind(listener, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0 && listen(listener, 16) == 0);
        int len = sizeof a; CHECK(getsockname(listener, reinterpret_cast<sockaddr*>(&a), &len) == 0); port = ntohs(a.sin_port);
        thread = CreateThread(nullptr, 0, [](LPVOID self) -> DWORD { static_cast<Server*>(self)->Loop(); return 0; }, this, 0, nullptr); CHECK(thread);
    }
    ~Server() { Stop(); }
    void Stop() { if (listener != INVALID_SOCKET) { closesocket(listener); listener = INVALID_SOCKET; } if (thread) { WaitForSingleObject(thread, 10000); CloseHandle(thread); thread = nullptr; } }
    std::wstring Origin() const { return L"http://127.0.0.1:" + std::to_wstring(port); }
    std::vector<std::string> Requests() { std::lock_guard<std::mutex> g(lock); return requests; }
    void Loop() {
        for (;;) {
            SOCKET c = accept(listener, nullptr, nullptr);
            if (c == INVALID_SOCKET) return;
            DWORD timeout = 5000; setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof timeout);
            std::string head; char buffer[4096];
            while (head.find("\r\n\r\n") == std::string::npos && head.size() < 65536) { const int n = recv(c, buffer, sizeof buffer, 0); if (n <= 0) break; head.append(buffer, static_cast<std::size_t>(n)); }
            const auto sp1 = head.find(' '), sp2 = head.find(' ', sp1 + 1);
            const std::string path = sp1 == std::string::npos || sp2 == std::string::npos ? "" : head.substr(sp1 + 1, sp2 - sp1 - 1);
            Route route; route.status = 404;
            { std::lock_guard<std::mutex> g(lock); requests.push_back(head); auto f = routes.find(path); if (f != routes.end()) route = f->second; }
            std::string out = "HTTP/1.1 " + std::to_string(route.status) + " X\r\nContent-Length: " + std::to_string(route.body.size()) + "\r\nConnection: close\r\n" + route.headers + "\r\n" + route.body;
            for (std::size_t sent = 0; sent < out.size();) { const int n = send(c, out.data() + sent, static_cast<int>(std::min<std::size_t>(out.size() - sent, 65536)), 0); if (n <= 0) break; sent += static_cast<std::size_t>(n); }
            shutdown(c, SD_SEND); recv(c, buffer, sizeof buffer, 0); closesocket(c);
        }
    }
};
static bool Has(const std::vector<std::string>& heads, const std::string& path) { for (const auto& h : heads) if (h.rfind("GET " + path + " ", 0) == 0) return true; return false; }
static std::string Narrow(const std::wstring& w) { std::string s; for (wchar_t c : w) s += static_cast<char>(c); return s; }

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: import_icons_test <work folder>\n"); return 2; }
    { std::string a = argv[1]; work.assign(a.begin(), a.end()); }
    RemoveTree(work); MakeDirs(work);
    const std::wstring temp = work + L"\\tmp"; MakeDirs(temp);
    WSADATA wsa{}; CHECK(WSAStartup(MAKEWORD(2, 2), &wsa) == 0);

    // ---- Signatures and data addresses ----
    OK(Icon::Raster(Png("x")) && Icon::Raster(Ico("x")) && Icon::Raster(std::string("GIF89a..")) && Icon::Raster(std::string("\xFF\xD8\xFF\xE0....", 8)) && Icon::Raster(std::string("BM......")));
    OK(!Icon::Raster(Svg) && !Icon::Raster("<html><body>not found</body></html>") && !Icon::Raster(std::string("\0\0\1\0\0\0....", 10)) && !Icon::Raster(std::string("\x89PNG", 4)));
    OK(!Icon::Raster(Png(std::string(Icon::bytes, 'x'))));   // over one icon's bound
    { const std::string b = Base64(Png("inline")); OK(Icon::DataImage(L"data:image/png;base64," + std::wstring(b.begin(), b.end())) == Png("inline")); }
    { const std::string b = Base64(Ico("x")); std::wstring w(b.begin(), b.end()); w.insert(8, L"\r\n "); OK(Icon::DataImage(L"DATA:image/x-icon;BASE64," + w) == Ico("x")); }   // case, line breaks
    { const std::string b = Base64(Svg); OK(Icon::DataImage(L"data:image/svg+xml;base64," + std::wstring(b.begin(), b.end())).empty()); }   // not raster
    OK(Icon::DataImage(L"data:image/png,%89PNG").empty() && Icon::DataImage(L"data:text/html;base64,PGh0bWw+").empty() && Icon::DataImage(L"https://a.example/i.png").empty() && Icon::DataImage(L"data:image/png;base64,iVBO$$$$").empty());
    OK(Icon::Score(32) == 0 && Icon::Score(48) < Icon::Score(16) && Icon::Score(16) < Icon::Score(0) && Icon::Score(65535) == Icon::Score(0));

    // ---- Chromium: the source's icons come with its bookmarks ----
    const std::wstring local = work + L"\\Local", chromeRoot = local + L"\\Google\\Chrome\\User Data", chrome = chromeRoot + L"\\Default";
    ChromiumProfile(chrome);
    auto sources = Discover(Roots{local, work + L"\\Roaming"});
    OK(sources.size() == 1 && sources[0].browser == Browser::Chrome);
    const Source cs = sources[0];
    const auto before = Tree(chrome);
    Options o; o.bookmarks = true; o.tempRoot = temp;
    ReadResult r = Read(cs, o);
    OK(r.bookmarks.failure == Failure::None && !r.bookmarks.roots.empty());
    const Icons& icons = r.bookmarks.icons;
    OK(icons.size() == 5);
    OK(icons.count(L"a.example") && icons.at(L"a.example") == Png("a32"));   // its own page's favicon type, 32 px
    OK(icons.count(L"b.example") && icons.at(L"b.example") == Png("b48"));   // another page of the site, 48 before 16
    OK(icons.count(L"c.example") && icons.at(L"c.example") == Png("c0"));    // a NULL bitmap is passed over
    OK(icons.count(L"d.example") && icons.at(L"d.example") == Png("d24"));   // « www. » is the same site
    OK(icons.count(L"e.example") && icons.at(L"e.example") == Png("e32"));   // SVG bytes are not an icon: the next candidate
    OK(!icons.count(L"f.example") && !icons.count(L"zzz.example"));          // none in the source / not bookmarked
    OK(Tree(chrome) == before && Leftovers(temp) == 0);                       // the source untouched, the private copy gone
    { Options n = o; n.icons = false; OK(Read(cs, n).bookmarks.icons.empty()); }
    { Options h; h.history = true; h.tempRoot = temp; OK(Read(cs, h).bookmarks.icons.empty()); }   // history only: no icon read
    // The order the sites are handled in: what sits directly on the bar first, then every other link.
    {
        const Icon::Sites s = SitesOf(r.bookmarks.roots);
        OK((s.order == std::vector<std::wstring>{L"a.example", L"b.example", L"d.example", L"c.example", L"e.example", L"f.example"}));
        OK(s.links.at(L"a.example").count(L"https://a.example/page") == 1 && !s.links.count(L""));
    }
    // A running Chrome holds Favicons for writing: the bookmarks still come, without the source's icons.
    {
        HANDLE holder = CreateFileW((chrome + L"\\Favicons").c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        CHECK(holder != INVALID_HANDLE_VALUE);
        const ReadResult busy = Read(cs, o);
        CloseHandle(holder);
        OK(busy.bookmarks.failure == Failure::None && !busy.bookmarks.roots.empty() && busy.bookmarks.icons.empty());
    }
    // No Favicons, a damaged one, an unknown schema, a view in place of a table: bookmarks fine, no icon, nothing left behind.
    {
        const std::wstring saved = chrome + L"\\Favicons.saved"; CHECK(MoveFileW((chrome + L"\\Favicons").c_str(), saved.c_str()));
        ReadResult x = Read(cs, o); OK(x.bookmarks.failure == Failure::None && x.bookmarks.icons.empty());
        Put(chrome + L"\\Favicons", std::string(4096, 'z')); x = Read(cs, o); OK(x.bookmarks.failure == Failure::None && x.bookmarks.icons.empty());
        DeleteFileW((chrome + L"\\Favicons").c_str());
        { Db d(chrome + L"\\Favicons"); d.Exec("CREATE TABLE something(x)"); }
        x = Read(cs, o); OK(x.bookmarks.failure == Failure::None && x.bookmarks.icons.empty());
        DeleteFileW((chrome + L"\\Favicons").c_str());
        { Db d(chrome + L"\\Favicons"); d.Exec("CREATE TABLE favicons(id INTEGER PRIMARY KEY, url, icon_type)"); d.Exec("CREATE TABLE favicon_bitmaps(id INTEGER PRIMARY KEY, icon_id, image_data, width)");
          d.Exec("CREATE TABLE t(page_url, icon_id)"); d.Exec("CREATE VIEW icon_mapping AS SELECT * FROM t"); }
        x = Read(cs, o); OK(x.bookmarks.failure == Failure::None && x.bookmarks.icons.empty());
        DeleteFileW((chrome + L"\\Favicons").c_str()); CHECK(MoveFileW(saved.c_str(), (chrome + L"\\Favicons").c_str()));
        OK(Leftovers(temp) == 0);
    }

    // ---- Firefox: favicons.sqlite beside places.sqlite ----
    {
        const std::wstring ffRoot = work + L"\\Roaming\\Mozilla\\Firefox", profile = ffRoot + L"\\Profiles\\t.default";
        Put(ffRoot + L"\\profiles.ini", "[Profile0]\nName=default\nIsRelative=1\nPath=Profiles/t.default\nDefault=1\n");
        FirefoxProfile(profile);
        const auto all = Discover(Roots{local, work + L"\\Roaming"});
        const Source* fs = nullptr; for (const auto& s : all) if (s.browser == Browser::Firefox) fs = &s;
        CHECK(fs);
        const auto ffBefore = Tree(profile);
        const ReadResult f = Read(*fs, o);
        OK(f.bookmarks.failure == Failure::None && f.bookmarks.icons.size() == 2);
        OK(f.bookmarks.icons.count(L"g.example") && f.bookmarks.icons.at(L"g.example") == Png("g32"));   // 32 among 16 / 32 / SVG
        OK(f.bookmarks.icons.count(L"h.example") && f.bookmarks.icons.at(L"h.example") == Ico("h16"));   // the site's root icon
        OK(!f.bookmarks.icons.count(L"i.example"));                                                      // SVG only: none
        OK(Tree(profile) == ffBefore && Leftovers(temp) == 0);
    }

    // ---- HTML export: ICON="data:…" (Firefox's export), the first per site ----
    {
        const std::string j1 = Base64(Png("j1")), j2 = Base64(Png("j2")), svg = Base64(Svg);
        const std::wstring file = work + L"\\Html\\export.html";
        Put(file, "<!DOCTYPE NETSCAPE-Bookmark-file-1>\n<DL><p>\n"
                  "<DT><A HREF=\"https://j.example/1\" ICON_URI=\"https://j.example/favicon.ico\" ICON=\"data:image/png;base64," + j1 + "\">J1</A>\n"
                  "<DT><A HREF=\"https://j.example/2\" ICON=\"data:image/png;base64," + j2 + "\">J2</A>\n"
                  "<DT><A HREF=\"https://k.example/\" ICON=\"data:image/svg+xml;base64," + svg + "\">K</A>\n"
                  "<DT><A HREF=\"https://l.example/\" ICON_URI=\"https://l.example/favicon.ico\">L</A>\n</DL>\n");
        const ReadResult h = Read(HtmlSource(file), o);
        OK(h.bookmarks.failure == Failure::None && h.bookmarks.icons.size() == 1 && h.bookmarks.icons.count(L"j.example") && h.bookmarks.icons.at(L"j.example") == Png("j1"));
        Options n = o; n.icons = false; OK(Read(HtmlSource(file), n).bookmarks.icons.empty());
    }

    // ---- Saved into the profile: never over a file already there ----
    const std::wstring data = work + L"\\Profile";
    MakeDirs(data);
    Put(II::FileOf(data, L"b.example"), Png("visited"));   // a visit's icon is newer than the import's
    {
        Icons in = icons; in[L"bad.example"] = Svg;
        const auto written = II::Save(data, in);
        OK((written == std::vector<std::wstring>{L"a.example", L"c.example", L"d.example", L"e.example"}));
        OK(Get(II::FileOf(data, L"a.example")) == Png("a32") && Get(II::FileOf(data, L"b.example")) == Png("visited") && !II::Exists(II::FileOf(data, L"bad.example")));
        OK(II::FileOf(data, L"xn--caf-dma.example") == data + L"\\favicons\\xn--caf-dma.example.png" && II::FileOf(data, L"a:b") == data + L"\\favicons\\a_b.png");
        OK(II::Save(data, in).empty());   // again: everything is already there
        bool stray = false; for (const auto& [k, v] : Tree(data + L"\\favicons")) if (k.find(L".import") != std::wstring::npos) stray = true;
        OK(!stray);
        OK(II::Save(work + L"\\Gone", icons).empty() && !II::Exists(work + L"\\Gone"));   // a deleted profile is never recreated
    }
    // What is still missing: f.example only (and nothing when the browser already has one for it).
    {
        const auto want = II::Missing(data, r.bookmarks.roots, nullptr);
        OK(want.size() == 1 && want[0].site == L"f.example" && want[0].origin == L"https://f.example");
        OK(II::Missing(data, r.bookmarks.roots, [](const std::wstring& s) { return s == L"f.example"; }).empty());
        OK(II::Missing(data, r.bookmarks.roots, nullptr, 0).empty());
        OK(II::OriginOf(L"https://user:pw@Host.Example:8443/x?y") == L"https://host.example:8443" && II::OriginOf(L"javascript:x").empty() && II::OriginOf(L"file:///C:/x").empty() && II::OriginOf(L"ftp://a.example/").empty());
        Item http, https; http.url = L"http://m.example/a"; https.url = L"https://www.m.example/b";
        const auto both = II::Missing(data, {http, https}, nullptr);
        OK(both.size() == 1 && both[0].site == L"m.example" && both[0].origin == L"https://www.m.example");   // https first
    }

    // ---- A page's declared icons ----
    {
        const std::wstring base = L"https://site.example/dir/page.html";
        OK(II::Resolve(base, L"/a.png") == L"https://site.example/a.png" && II::Resolve(base, L"b.png") == L"https://site.example/dir/b.png");
        OK(II::Resolve(base, L"//cdn.example/c.ico") == L"https://cdn.example/c.ico" && II::Resolve(base, L"HTTP://x.example/d.png") == L"HTTP://x.example/d.png");
        OK(II::Resolve(base, L" e f.png#frag ") == L"https://site.example/dir/e%20f.png" && II::Resolve(L"https://site.example", L"g.png") == L"https://site.example/g.png");
        OK(II::Resolve(base, L"javascript:alert(1)").empty() && II::Resolve(base, L"data:image/png;base64,xx").empty() && II::Resolve(base, L"").empty() && II::Resolve(base, L"/caf\u00e9.png").empty());
        const std::string page = "<html><head><link rel=\"stylesheet\" href=\"/s.css\"><link rel=\"mask-icon\" href=\"/m.svg\">"
                                 "<link rel=\"icon\" type=\"image/svg+xml\" href=\"/i.svg\"><link rel=\"icon\" href=\"/v.svg?x=1\">"
                                 "<link rel=\"apple-touch-icon\" href=\"/touch.png\"><link rel=\"icon\" sizes=\"16x16\" href=\"/i16.png\">\n"
                                 "<LINK REL=\"Shortcut Icon\" HREF=\"/i32.ico?v=2&amp;x=1\" SIZES=\"32x32\"><link rel='icon' href='data:image/png;base64," + Base64(Png("in")) + "' sizes='64x64'></head>";
        const auto links = II::IconLinks(page, base);
        OK(links.size() == 4 && links[0] == L"https://site.example/i32.ico?v=2&x=1" && links[1].rfind(L"data:image/png;base64,", 0) == 0 && links[2] == L"https://site.example/i16.png" && links[3] == L"https://site.example/touch.png");
        OK(II::IconLinks("<html><head><title>x</title></head><body>no icon</body></html>", base).empty() && II::IconLinks("", base).empty());
    }

    // ---- Fetched from the site (loopback servers; a direct session, no proxy) ----
    HINTERNET session = II::OpenSession(true);
    CHECK(session);
    {   // 1. /favicon.ico, asked as an image, without cookie or credentials
        Server s({{"/favicon.ico", {200, Ico("fetched"), "Content-Type: image/x-icon\r\n"}}});
        OK(II::Fetch(session, s.Origin()) == Ico("fetched"));
        const auto heads = s.Requests();
        OK(heads.size() == 1 && Has(heads, "/favicon.ico") && heads[0].find("Accept: image/png") != std::string::npos && heads[0].find("Cookie:") == std::string::npos && heads[0].find("Authorization:") == std::string::npos);
    }
    {   // 2. no /favicon.ico: the home page's declared icon (the page is read only up to its bound; a cookie it sets is never sent back)
        const std::string html = "<html><head><link rel=\"icon\" href=\"/static/i.png\"></head><body>" + std::string(700000, 'x') + "</body></html>";
        Server s({{"/", {200, html, "Content-Type: text/html\r\nSet-Cookie: id=secret\r\n"}}, {"/static/i.png", {200, Png("declared"), ""}}});
        OK(II::Fetch(session, s.Origin()) == Png("declared"));
        const auto heads = s.Requests();
        OK(heads.size() == 3 && Has(heads, "/favicon.ico") && Has(heads, "/") && Has(heads, "/static/i.png"));
        bool cookie = false; for (const auto& h : heads) if (h.find("Cookie:") != std::string::npos) cookie = true;
        OK(!cookie);
    }
    {   // 3. a « soft 404 » page at /favicon.ico and no declared icon: nothing (the glyph stays)
        Server s({{"/favicon.ico", {200, "<html>not found</html>", "Content-Type: text/html\r\n"}}, {"/", {200, "<html><head></head></html>", ""}}});
        OK(II::Fetch(session, s.Origin()).empty());
    }
    {   // 4. redirects: to another path, then to another origin
        Server other({{"/x.ico", {200, Ico("other"), ""}}});
        Server s({{"/favicon.ico", {302, "", "Location: /moved.ico\r\n"}}, {"/moved.ico", {301, "", "Location: " + Narrow(other.Origin()) + "/x.ico\r\n"}}});
        OK(II::Fetch(session, s.Origin()) == Ico("other"));
    }
    {   // 5. a redirect loop, an icon over the bound, an inline icon
        Server loop({{"/favicon.ico", {302, "", "Location: /favicon.ico\r\n"}}});
        OK(II::Fetch(session, loop.Origin()).empty());
        OK(loop.Requests().size() <= static_cast<std::size_t>(II::Bound::hops) + 2);
        Server big({{"/favicon.ico", {200, Png(std::string(Icon::bytes, 'x')), ""}}});
        OK(II::Fetch(session, big.Origin()).empty());
        Server inline_({{"/", {200, "<link rel=icon href=\"data:image/png;base64," + Base64(Png("inline")) + "\">", ""}}});
        OK(II::Fetch(session, inline_.Origin()) == Png("inline"));
    }
    {   // 6. a site asking for credentials gets none and gives nothing
        Server s({{"/favicon.ico", {401, "", "WWW-Authenticate: NTLM\r\nWWW-Authenticate: Negotiate\r\n"}}, {"/", {401, "", "WWW-Authenticate: NTLM\r\n"}}});
        OK(II::Fetch(session, s.Origin()).empty());
        const auto heads = s.Requests();
        OK(heads.size() == 2);
        for (const auto& h : heads) OK(h.find("Authorization:") == std::string::npos);
    }
    {   // 7. nothing listening: quick, empty
        int port = 0; { Server s({}); port = s.port; }
        const ULONGLONG t0 = GetTickCount64();
        OK(II::Fetch(session, L"http://127.0.0.1:" + std::to_wstring(port)).empty() && GetTickCount64() - t0 < 15000);
    }
    // ---- The host's loop in small: what is missing is fetched and saved, the rest left alone ----
    {
        Server n({{"/", {200, "<link rel=\"shortcut icon\" href=\"/n.gif\">", ""}}, {"/n.gif", {200, "GIF89a-n", ""}}});
        Item page, again, already;
        page.url = n.Origin() + L"/page"; again.url = n.Origin() + L"/other"; already.url = L"https://a.example/again";
        const auto want = II::Missing(data, {page, again, already}, nullptr);
        OK(want.size() == 1 && want[0].site == L"127.0.0.1" && want[0].origin == n.Origin());   // one site (a port is not part of it), asked once
        std::size_t saved = 0; for (const auto& w : want) if (II::SaveOne(data, w.site, II::Fetch(session, w.origin))) ++saved;
        OK(saved == 1 && Get(II::FileOf(data, L"127.0.0.1")) == "GIF89a-n");
        OK(II::Missing(data, {page, again, already}, nullptr).empty());
        OK(!II::SaveOne(data, L"127.0.0.1", Png("later")) && Get(II::FileOf(data, L"127.0.0.1")) == "GIF89a-n");   // never replaced
    }
    II::CloseSession(session);
    WSACleanup();
    std::printf("import icons: %d checks passed\n", checks);
    return 0;
}
