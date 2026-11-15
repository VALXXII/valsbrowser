// Import from a browser (src/browser-import.h): discovery, readers, rules and commits on SYNTHETIC roots only.
// Never reads %LOCALAPPDATA% / %APPDATA%: every root is a folder this test creates under its work folder.
// Usage: browser_import_test.exe <work folder>   (deleted and recreated; nothing outside it is touched)
#include "browser-import.h"
#include <sddl.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #x); std::exit(1); } } while (0)

using namespace BrowserImport;
namespace BM = Bookmarks;

static std::wstring work;
static int checks = 0;
#define OK(x) do { CHECK(x); ++checks; } while (0)

static void MakeDirs(const std::wstring& path) {
    for (std::size_t i = 3; i <= path.size(); ++i) if (i == path.size() || path[i] == L'\\') CreateDirectoryW(path.substr(0, i).c_str(), nullptr);
}
static void Put(const std::wstring& path, const std::string& bytes) {
    MakeDirs(path.substr(0, path.rfind(L'\\')));
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(f != INVALID_HANDLE_VALUE); DWORD put = 0; CHECK(WriteFile(f, bytes.data(), static_cast<DWORD>(bytes.size()), &put, nullptr) && put == bytes.size()); CloseHandle(f);
}
static void RemoveTree(const std::wstring& dir) {
    WIN32_FIND_DATAW fd{}; HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring n = fd.cFileName; if (n == L"." || n == L"..") continue;
            const std::wstring p = dir + L"\\" + n;
            SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) RemoveTree(p);
            else if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) RemoveDirectoryW(p.c_str());   // a junction: never followed
            else DeleteFileW(p.c_str());
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir.c_str());
}
// Every file (name → bytes) and every folder under `dir`: proves a source is left exactly as it was,
// sidecars (-wal, -shm, -journal) included.
static std::map<std::wstring, std::string> Tree(const std::wstring& dir, const std::wstring& rel = L"") {
    std::map<std::wstring, std::string> out; WIN32_FIND_DATAW fd{}; HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        const std::wstring n = fd.cFileName; if (n == L"." || n == L"..") continue;
        const std::wstring p = dir + L"\\" + n, r = rel + L"\\" + n;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { out[r + L"\\"] = ""; auto sub = Tree(p, r); out.insert(sub.begin(), sub.end()); }
        else { std::string b; HANDLE f = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr); if (f != INVALID_HANDLE_VALUE) { LARGE_INTEGER s{}; GetFileSizeEx(f, &s); b.resize(static_cast<std::size_t>(s.QuadPart)); DWORD got = 0; if (!b.empty()) ReadFile(f, b.data(), static_cast<DWORD>(b.size()), &got, nullptr); CloseHandle(f); } out[r] = b; }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return out;
}
static bool Empty(const std::wstring& dir) { return Tree(dir).empty(); }
// A directory junction `link` → `target` (mklink /J: no privilege needed), synthetic folders only.
static bool Junction(const std::wstring& link, const std::wstring& target) {
    std::wstring cmd = L"cmd.exe /c mklink /J \"" + link + L"\" \"" + target + L"\" >nul";
    STARTUPINFOW si{sizeof si}; PROCESS_INFORMATION pi{}; std::vector<wchar_t> line(cmd.begin(), cmd.end()); line.push_back(0);
    if (!CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return false;
    WaitForSingleObject(pi.hProcess, 10000); CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return BrowserImport::IsReparse(link);
}
// Exact UTF-8 (keeps line breaks, unlike the store's ToUtf8 which flattens them).
static std::string U8(const std::wstring& w) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n), '\0'); WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr); return s;
}

// ---- SQLite fixtures (winsqlite3, synthetic databases) ----------------------------------------------------
struct Db {
    Sql::sqlite3* db = nullptr;
    explicit Db(const std::wstring& path) { MakeDirs(path.substr(0, path.rfind(L'\\'))); CHECK(Sql::Get().Ready()); CHECK(Sql::Get().open_v2(Sql::FileUri(path, "").c_str(), &db, Sql::OPEN_READWRITE | 0x4 /* CREATE */ | Sql::OPEN_URI, nullptr) == Sql::OK); }
    ~Db() { if (db) Sql::Get().close(db); }
    void Exec(const std::string& sql) { char* err = nullptr; const int rc = Sql::Get().exec(db, sql.c_str(), nullptr, nullptr, &err); if (rc != Sql::OK) { std::fprintf(stderr, "SQL %d: %s\n", rc, sql.c_str()); std::exit(1); } }
};
// Chromium time (µs since 1601) for a Unix ms value.
static std::string Chrome(std::int64_t unixMs) { return std::to_string((unixMs + WindowsToUnixMs) * 1000); }
static void ChromiumHistory(const std::wstring& path, bool hiddenColumn = true) {
    Db d(path);
    d.Exec(std::string("CREATE TABLE urls(id INTEGER PRIMARY KEY, url LONGVARCHAR, title LONGVARCHAR, visit_count INTEGER DEFAULT 0 NOT NULL, typed_count INTEGER DEFAULT 0 NOT NULL, last_visit_time INTEGER NOT NULL") + (hiddenColumn ? ", hidden INTEGER DEFAULT 0 NOT NULL" : "") + ")");
    const char* h = hiddenColumn ? ", hidden" : "";
    auto row = [&](int id, const std::string& url, const std::string& title, int visits, const std::string& last, int hidden) {
        d.Exec("INSERT INTO urls(id, url, title, visit_count, last_visit_time" + std::string(h) + ") VALUES(" + std::to_string(id) + ", '" + url + "', '" + title + "', " + std::to_string(visits) + ", " + last + (hiddenColumn ? ", " + std::to_string(hidden) : "") + ")");
    };
    row(1, "https://example.com/a", U8(L"Accueil é 漢字 🙂"), 7, Chrome(1700000000000), 0);
    row(2, "https://example.com/b", "Page B", 2, Chrome(1700000500000), 0);
    row(3, "javascript:alert(1)", "bad", 1, Chrome(1700000600000), 0);
    row(4, "https://hidden.example/", "hidden", 1, Chrome(1700000700000), 1);
    row(5, "chrome://settings/", "internal", 3, Chrome(1700000800000), 0);
    row(6, "https://never.example/", "never", 0, "0", 0);
    row(7, "file:///C:/notes/x.html", "local", 1, Chrome(1600000000000), 0);
    row(8, "https://www.google.com/search?q=x", "search", 1, Chrome(1700000900000), 0);
}
static void Places(const std::wstring& path, bool wal) {
    Db d(path);
    if (wal) d.Exec("PRAGMA journal_mode=WAL");
    d.Exec("CREATE TABLE moz_places(id INTEGER PRIMARY KEY, url LONGVARCHAR, title LONGVARCHAR, visit_count INTEGER DEFAULT 0, hidden INTEGER DEFAULT 0 NOT NULL, last_visit_date INTEGER)");
    d.Exec("CREATE TABLE moz_bookmarks(id INTEGER PRIMARY KEY, type INTEGER, fk INTEGER DEFAULT NULL, parent INTEGER, position INTEGER, title LONGVARCHAR, dateAdded INTEGER, guid TEXT)");
    d.Exec("INSERT INTO moz_places VALUES(1, 'https://mozilla.example/', 'Moz', 4, 0, 1700000000000000)");
    d.Exec(std::string("INSERT INTO moz_places VALUES(2, 'https://unicode.example/é', '") + U8(L"Été ☀ 🙂") + "', 1, 0, 1700000100000000)");
    d.Exec("INSERT INTO moz_places VALUES(3, 'place:sort=8', 'smart', 0, 0, NULL)");
    d.Exec("INSERT INTO moz_places VALUES(4, 'https://hidden.example/', 'h', 1, 1, 1700000200000000)");
    d.Exec("INSERT INTO moz_bookmarks VALUES(1, 2, NULL, 0, 0, '', 1, 'root________')");
    d.Exec("INSERT INTO moz_bookmarks VALUES(2, 2, NULL, 1, 0, 'menu', 1, 'menu________')");
    d.Exec("INSERT INTO moz_bookmarks VALUES(3, 2, NULL, 1, 1, 'toolbar', 1, 'toolbar_____')");
    d.Exec("INSERT INTO moz_bookmarks VALUES(4, 2, NULL, 1, 2, 'tags', 1, 'tags________')");
    d.Exec("INSERT INTO moz_bookmarks VALUES(5, 2, NULL, 1, 3, 'unfiled', 1, 'unfiled_____')");
    // Toolbar: positions inserted out of order; a separator; a folder with a Unicode title.
    d.Exec("INSERT INTO moz_bookmarks VALUES(10, 1, 2, 3, 2, 'second', 1700000000000000, 'a1')");
    d.Exec("INSERT INTO moz_bookmarks VALUES(11, 1, 1, 3, 0, 'first', 1690000000000000, 'a2')");
    d.Exec("INSERT INTO moz_bookmarks VALUES(12, 3, NULL, 3, 1, '', 1, 'a3')");
    d.Exec(std::string("INSERT INTO moz_bookmarks VALUES(13, 2, NULL, 3, 3, '") + U8(L"Dossier 日本") + "', 1, 'a4')");
    d.Exec("INSERT INTO moz_bookmarks VALUES(14, 1, 1, 13, 0, 'inside', 1, 'a5')");
    d.Exec("INSERT INTO moz_bookmarks VALUES(15, 1, 3, 2, 0, 'smart', 1, 'a6')");          // menu: place: URL
    d.Exec("INSERT INTO moz_bookmarks VALUES(16, 1, 1, 4, 0, 'tagged', 1, 'a7')");         // under tags: never imported
    d.Exec("INSERT INTO moz_bookmarks VALUES(17, 1, NULL, 5, 0, 'no url', 1, 'a8')");       // damaged: no place
}
static std::vector<std::wstring> Titles(const BM::Store& s, BM::Id folder) { std::vector<std::wstring> t; for (BM::Id id : s.Find(folder)->children) t.push_back(s.Find(id)->title); return t; }
static BM::Id Child(const BM::Store& s, BM::Id folder, const std::wstring& title) { for (BM::Id id : s.Find(folder)->children) if (s.Find(id)->title == title) return id; return BM::NoId; }

static std::string Json(const std::wstring& w) { std::string o = "\""; for (char c : U8(w)) { if (c == '"' || c == '\\') o += '\\'; o += c; } return o + "\""; }
static std::string ChromeNode(const std::wstring& name, const std::string& url, std::int64_t addedMs = 1700000000000) {
    return "{\"type\":\"url\",\"name\":" + Json(name) + ",\"url\":\"" + url + "\",\"date_added\":\"" + Chrome(addedMs) + "\",\"guid\":\"x\",\"id\":\"1\"}";
}
static std::string ChromeFolder(const std::wstring& name, const std::vector<std::string>& kids) {
    std::string k; for (const auto& c : kids) { if (!k.empty()) k += ","; k += c; }
    return "{\"type\":\"folder\",\"name\":" + Json(name) + ",\"children\":[" + k + "],\"date_added\":\"" + Chrome(1690000000000) + "\"}";
}
static std::string ChromeBookmarks() {
    std::string deep = ChromeNode(L"au fond", "https://deep.example/");
    for (int i = 11; i >= 1; --i) deep = ChromeFolder(L"N" + std::to_wstring(i), {deep});
    const std::string bar = ChromeFolder(L"Bookmarks bar", {
        ChromeNode(L"Un", "https://one.example/", 1600000000000),
        ChromeFolder(L"Travail 🛠 é", {ChromeNode(L"Doc", "https://docs.example/"), ChromeNode(L"Doc again", "https://docs.example/"), ChromeNode(L"JS", "javascript:void(0)")}),
        ChromeFolder(L"Perso", {ChromeNode(L"Doc", "https://docs.example/")}),
        ChromeNode(L"Deux", "https://two.example/"),
    });
    const std::string other = ChromeFolder(L"Other bookmarks", {ChromeNode(L"Trois", "https://three.example/"), deep});
    const std::string synced = ChromeFolder(L"Mobile bookmarks", {});
    return "{\"checksum\":\"0\",\"roots\":{\"bookmark_bar\":" + bar + ",\"other\":" + other + ",\"synced\":" + synced + "},\"version\":1}";
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: browser_import_test <work folder>\n"); return 2; }
    { std::wstring w; NativeChrome::Downloads::FromUtf8(argv[1], w); for (auto& c : w) if (c == L'/') c = L'\\'; work = w; }
    // Spaces, accents, CJK, '#' and '%' in every path below: URI escaping and path handling are exercised.
    work += L"\\run é #% 漢";
    RemoveTree(work); MakeDirs(work);
    const std::wstring temp = work + L"\\temp #1 %20"; MakeDirs(temp);
    CHECK(Sql::Get().Ready());   // winsqlite3.dll from System32

    // ---- Time conversions ----------------------------------------------------------------------------------
    OK(ChromiumTime((1700000000000LL + WindowsToUnixMs) * 1000) == 1700000000000LL);
    OK(ChromiumTime(0) == 0 && ChromiumTime(-5) == 0);
    OK(FirefoxTime(1700000000000000LL) == 1700000000000LL && FirefoxTime(0) == 0);
    OK(HtmlTime(1700000000) == 1700000000000LL && HtmlTime(1700000000000LL) == 1700000000000LL && HtmlTime(1700000000000000LL) == 1700000000000LL && HtmlTime(-1) == 0);
    OK(Clamp(5, 3) == 3 && Clamp(-1, 3) == 0);

    // ---- URI escaping ---------------------------------------------------------------------------------------
    OK(Sql::FileUri(L"C:\\a b\\é#?%\\db", "mode=ro") == "file:///C:/a%20b/%C3%A9%23%3F%25/db?mode=ro");

    // ---- Discovery (metadata only, injected roots) ----------------------------------------------------------
    const Roots roots{work + L"\\Local", work + L"\\Roaming"};
    const std::wstring chrome = roots.local + L"\\Google\\Chrome\\User Data", edge = roots.local + L"\\Microsoft\\Edge\\User Data", firefox = roots.roaming + L"\\Mozilla\\Firefox";
    Put(chrome + L"\\Default\\Bookmarks", ChromeBookmarks());
    ChromiumHistory(chrome + L"\\Default\\History");
    ChromiumHistory(chrome + L"\\Profile 2\\History", false);           // older schema: no `hidden`
    MakeDirs(chrome + L"\\System Profile"); Put(chrome + L"\\System Profile\\Bookmarks", "{}");   // not a person's profile
    MakeDirs(chrome + L"\\Profile 9");                                    // no data: not listed
    Put(chrome + L"\\Local State", "{\"never\":\"read\"}");
    Put(edge + L"\\Default\\Bookmarks", "{\"roots\":{}}");
    // A junction planted as a profile: never followed.
    { MakeDirs(work + L"\\elsewhere\\Profile 3"); Put(work + L"\\elsewhere\\Profile 3\\Bookmarks", ChromeBookmarks());
      std::wstring cmd = L"cmd.exe /c mklink /J \"" + chrome + L"\\Profile 3\" \"" + work + L"\\elsewhere\\Profile 3\" >nul";
      STARTUPINFOW si{sizeof si}; PROCESS_INFORMATION pi{}; std::vector<wchar_t> line(cmd.begin(), cmd.end()); line.push_back(0);
      CHECK(CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi));
      WaitForSingleObject(pi.hProcess, 10000); CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
      CHECK(IsReparse(chrome + L"\\Profile 3")); }
    const std::wstring ffProfile = firefox + L"\\Profiles\\ab cd é#%.default-release";
    Places(ffProfile + L"\\places.sqlite", false);
    Put(firefox + L"\\profiles.ini", U8(L"\xFEFF[General]\r\nStartWithLastProfile=1\r\n\r\n[Profile0]\r\nName=default-release\r\nIsRelative=1\r\nPath=Profiles/ab cd é#%.default-release\r\nDefault=1\r\n\r\n"
        L"[Profile1]\r\nName=escape\r\nIsRelative=1\r\nPath=../../outside\r\n\r\n[Profile2]\r\nName=absolute\r\nIsRelative=0\r\nPath=C:\\Windows\r\n\r\n[Profile3]\r\nName=drive\r\nIsRelative=1\r\nPath=C:/x\r\n"));
    {
        // The data files are held open WITHOUT any sharing during discovery: it never opens them.
        HANDLE lock = CreateFileW((chrome + L"\\Default\\Bookmarks").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        CHECK(lock != INVALID_HANDLE_VALUE);
        const auto before = Tree(work);
        const auto found = Discover(roots);
        OK(Tree(work) == before);
        CloseHandle(lock);
        OK(found.size() == 4);
        OK(found[0].browser == Browser::Chrome && found[0].profile == L"Default" && found[0].isDefault && found[0].bookmarks && found[0].history && !found[0].soleProfile);
        OK(found[1].browser == Browser::Chrome && found[1].profile == L"Profile 2" && !found[1].bookmarks && found[1].history);
        OK(found[2].browser == Browser::Edge && found[2].soleProfile && found[2].bookmarks && !found[2].history);
        OK(found[3].browser == Browser::Firefox && found[3].profile == L"default-release" && found[3].isDefault && found[3].dir == ffProfile && found[3].bookmarks && found[3].history);
        OK(Label(found[0]) == L"Chrome — profil principal" && Label(found[1]) == L"Chrome — Profile 2" && Label(found[2]) == L"Edge" && Label(found[3]) == L"Firefox");
        OK(FolderTitle(found[2]) == L"Importés de Edge" && FolderTitle(HtmlSource(L"x")) == L"Importés d'un fichier HTML");
        OK(Discover(Roots{work + L"\\nothing", L""}).empty());
    }

    const std::int64_t now = 1800000000000LL;
    // ---- Chromium bookmarks ----------------------------------------------------------------------------------
    const Source chromeDefault = DiscoverChromium(roots, Browser::Chrome)[0];
    Options both; both.bookmarks = both.history = true; both.tempRoot = temp;
    const auto chromeTreeBefore = Tree(chrome);
    const ReadResult cr = Read(chromeDefault, both);
    OK(Tree(chrome) == chromeTreeBefore);          // source untouched, no sidecar created
    OK(Empty(temp));                               // private snapshot deleted
    OK(cr.bookmarks.failure == Failure::None && cr.bookmarks.roots.size() == 2);   // empty « synced » skipped
    OK(cr.bookmarks.roots[0].title == L"Barre de favoris" && cr.bookmarks.roots[1].title == L"Autres favoris");
    OK(cr.bookmarks.roots[0].children[1].title == L"Travail 🛠 é" && cr.bookmarks.roots[0].children[0].added == 1600000000000LL);
    {
        BM::Store s; s.Add(BM::BarRoot, BM::End, L"existing", L"https://mine.example/", 1);
        const BM::Store original = s;
        const Counts c = ApplyBookmarks(s, L"Importés de Chrome", cr.bookmarks.roots, now);
        // 7 links + deep; javascript: unsupported; deep link is under 11 nested folders.
        OK(c.imported == 6 && c.unsupported == 1 && c.present == 0 && c.deep == 1 && c.full == 0);
        const BM::Id box = Child(s, BM::OtherRoot, L"Importés de Chrome"); OK(box != BM::NoId);
        OK((Titles(s, box) == std::vector<std::wstring>{L"Barre de favoris", L"Autres favoris"}));
        const BM::Id bar = Child(s, box, L"Barre de favoris");
        OK((Titles(s, bar) == std::vector<std::wstring>{L"Un", L"Travail 🛠 é", L"Perso", L"Deux"}));   // order kept
        const BM::Id work1 = Child(s, bar, L"Travail 🛠 é");
        OK((Titles(s, work1) == std::vector<std::wstring>{L"Doc", L"Doc again"}));   // intentional repeat in one folder kept
        OK(s.Find(Child(s, Child(s, bar, L"Perso"), L"Doc")) != nullptr);          // same URL in another folder kept
        OK(s.Find(Child(s, bar, L"Un"))->added == 1600000000000LL);
        // Depth: box (2), Autres favoris (3), N1..N5 (4..8); N6 would be 9 > MaxDepth: its subtree is counted, never flattened.
        BM::Id f = Child(s, Child(s, box, L"Autres favoris"), L"N1"); int levels = 0; while (f && levels < 20) { ++levels; const BM::Id next = Child(s, f, L"N" + std::to_wstring(levels + 1)); if (!next) break; f = next; }
        OK(levels == 5 && s.Find(f)->children.empty());
        OK(s.Find(Child(s, BM::BarRoot, L"existing")) != nullptr);   // nothing replaced
        // Repeat import: nothing new.
        const BM::Store once = s;
        const Counts again = ApplyBookmarks(s, L"Importés de Chrome", cr.bookmarks.roots, now);
        OK(s == once && again.imported == 0 && again.present == 6 && again.unsupported == 1 && again.deep == 1 && again.folders == 0);
        // A source that gained one bookmark: only that one is added, at the end of its folder.
        auto more = cr.bookmarks.roots; Item extra; extra.title = L"Nouveau"; extra.url = L"https://new.example/"; more[0].children.push_back(extra);
        const Counts third = ApplyBookmarks(s, L"Importés de Chrome", more, now);
        OK(third.imported == 1 && third.present == 6 && Titles(s, bar).back() == L"Nouveau");
        (void)original;
    }
    {   // Capacity: 4 free nodes → the rest is reported, nothing evicted.
        BM::Store s; for (std::size_t i = 0; s.Size() < BM::Store::Limit - 4; ++i) CHECK(s.Add(BM::OtherRoot, BM::End, L"x", L"https://fill.example/" + std::to_wstring(i), 1) != BM::NoId);
        const Counts c = ApplyBookmarks(s, L"Importés de Chrome", cr.bookmarks.roots, now);
        OK(s.Size() == BM::Store::Limit);
        OK(c.imported + c.folders == 4 && c.imported + c.full + c.unsupported + c.deep >= 7);
        OK(c.full > 0);
    }
    // Malformed and oversized sources.
    OK(ParseChromiumBookmarks("{\"roots\": [").failure == Failure::Malformed);
    OK(ParseChromiumBookmarks("{\"no_roots\":1}").failure == Failure::Malformed);
    OK(ParseChromiumBookmarks("{\"roots\":{\"bookmark_bar\":{\"type\":\"folder\",\"children\":[{\"type\":\"url\",\"name\":1}]}}}").roots.empty());
    {
        const std::wstring big = work + L"\\big\\Default"; Put(big + L"\\Bookmarks", std::string(static_cast<std::size_t>(Bound::jsonBytes) + 1, ' '));
        Source s = chromeDefault; s.dir = big; s.root = Parent(big); Options o; o.bookmarks = true; o.tempRoot = temp;
        OK(Read(s, o).bookmarks.failure == Failure::TooLarge);
        Source gone = chromeDefault; gone.dir = work + L"\\gone"; gone.root = work; OK(Read(gone, o).bookmarks.failure == Failure::Missing);
        // A JSON nested deeper than the parser allows is refused, never a stack overflow.
        std::string nest = ChromeNode(L"x", "https://x.example/"); for (int i = 0; i < 400; ++i) nest = ChromeFolder(L"f", {nest});
        OK(ParseChromiumBookmarks("{\"roots\":{\"bookmark_bar\":" + nest + "}}").failure == Failure::TooDeep);
    }
    // ---- Reader nesting bound (review 934ef4b finding 4): never a silent cut ------------------------------------
    {
        // Chromium: the bar root is depth 1; `folders` nested folders under it put the link at depth folders + 2.
        auto chain = [](int folders) { std::string n = ChromeNode(L"au fond", "https://bottom.example/"); for (int i = 0; i < folders; ++i) n = ChromeFolder(L"f", {n}); return "{\"roots\":{\"bookmark_bar\":" + ChromeFolder(L"bar", {n}) + "}}"; };
        auto deepest = [](const std::vector<Item>& roots) { std::size_t d = 0; std::wstring url; std::function<void(const Item&, std::size_t)> walk = [&](const Item& it, std::size_t k) { if (!it.folder && k > d) { d = k; url = it.url; } for (const auto& c : it.children) walk(c, k + 1); }; for (const auto& r : roots) walk(r, 1); return std::make_pair(d, url); };
        const auto at = ParseChromiumBookmarks(chain(static_cast<int>(Bound::nesting) - 2));
        OK(at.failure == Failure::None && deepest(at.roots) == std::make_pair(Bound::nesting, std::wstring(L"https://bottom.example/")));   // link exactly at the bound: kept
        BM::Store s; const Counts c = ApplyBookmarks(s, L"Importés de Chrome", at.roots, now);
        OK(c.deep == 1 && c.imported == 0);   // and the destination's own depth 8 reports it, never hides it
        OK(ParseChromiumBookmarks(chain(static_cast<int>(Bound::nesting) - 1)).failure == Failure::TooDeep);   // one level more: refused whole
        // The reviewer's case: 257 nested folders and one link → refused, nothing to commit (no false « terminée »).
        const auto review = ParseChromiumBookmarks(chain(257));
        OK(review.failure == Failure::TooDeep && review.roots.empty());
        // Firefox: toolbar children are depth 2 (as Chromium); a link at depth 256 kept, at 257 refused.
        auto places = [&](const std::wstring& dir, int folders) {
            Db d(dir + L"\\places.sqlite");
            d.Exec("CREATE TABLE moz_places(id INTEGER PRIMARY KEY, url LONGVARCHAR, title LONGVARCHAR, visit_count INTEGER DEFAULT 0, hidden INTEGER DEFAULT 0 NOT NULL, last_visit_date INTEGER)");
            d.Exec("CREATE TABLE moz_bookmarks(id INTEGER PRIMARY KEY, type INTEGER, fk INTEGER DEFAULT NULL, parent INTEGER, position INTEGER, title LONGVARCHAR, dateAdded INTEGER, guid TEXT)");
            d.Exec("INSERT INTO moz_places VALUES(1, 'https://bottom.example/', 'b', 1, 0, 1700000000000000)");
            d.Exec("INSERT INTO moz_bookmarks VALUES(1, 2, NULL, 0, 0, '', 1, 'root________')");
            d.Exec("INSERT INTO moz_bookmarks VALUES(3, 2, NULL, 1, 0, 'toolbar', 1, 'toolbar_____')");
            d.Exec("BEGIN");
            for (int i = 0; i < folders; ++i) d.Exec("INSERT INTO moz_bookmarks VALUES(" + std::to_string(100 + i) + ", 2, NULL, " + std::to_string(i ? 99 + i : 3) + ", 0, 'f', 1, 'g" + std::to_string(i) + "')");
            d.Exec("INSERT INTO moz_bookmarks VALUES(50, 1, 1, " + std::to_string(folders ? 99 + folders : 3) + ", 0, 'au fond', 1, 'link')");
            d.Exec("COMMIT");
        };
        Options o; o.bookmarks = true; o.tempRoot = temp;
        Source ff; ff.browser = Browser::Firefox; ff.bookmarks = true;
        ff.root = work + L"\\ffdeep"; ff.dir = work + L"\\ffdeep\\at"; places(ff.dir, static_cast<int>(Bound::nesting) - 2);
        const auto fat = Read(ff, o).bookmarks;
        OK(fat.failure == Failure::None && deepest(fat.roots) == std::make_pair(Bound::nesting, std::wstring(L"https://bottom.example/")));
        ff.dir = work + L"\\ffdeep\\over"; places(ff.dir, static_cast<int>(Bound::nesting) - 1);
        const auto fover = Read(ff, o).bookmarks;
        OK(fover.failure == Failure::TooDeep && fover.roots.empty());
        // HTML: top-level items are depth 1; `folders` nested folders put the link at depth folders + 1.
        auto html = [](int folders) { std::string h = "<!DOCTYPE NETSCAPE-Bookmark-file-1>\n<DL>"; for (int i = 0; i < folders; ++i) h += "<DT><H3>f</H3><DL>"; h += "<DT><A HREF=\"https://bottom.example/\">x</A>"; for (int i = 0; i <= folders; ++i) h += "</DL>"; return h; };
        const auto hat = ParseHtmlBookmarks(html(static_cast<int>(Bound::nesting) - 1));
        OK(hat.failure == Failure::None && deepest(hat.roots) == std::make_pair(Bound::nesting, std::wstring(L"https://bottom.example/")));
        OK(ParseHtmlBookmarks(html(static_cast<int>(Bound::nesting))).failure == Failure::TooDeep);
        OK(FailureText(Failure::TooDeep, chromeDefault) == L"dossiers imbriqués trop profondément, rien n'a été importé.");
        // Damaged entries are counted and shown, not only logged.
        Counts u; u.imported = 2; u.unreadable = 1; OK(CountsText(u) == L"2 importés · 1 entrée illisible" && u.Left());
        OK(Empty(temp));
    }

    // ---- Chromium history -----------------------------------------------------------------------------------
    OK(cr.history.failure == Failure::None);
    OK(cr.history.visits.size() == 6);   // hidden and never-visited rows excluded by the query
    OK(cr.history.visits[0].url == L"https://www.google.com/search?q=x" && cr.history.visits.back().url == L"file:///C:/notes/x.html");
    {
        const Visit* a = nullptr; for (const auto& v : cr.history.visits) if (v.url == L"https://example.com/a") a = &v;
        OK(a && a->title == L"Accueil é 漢字 🙂" && a->visits == 7 && a->last == 1700000000000LL);
        AddressBar::History h; h.Record(L"https://example.com/b", L"mine", 1750000000000LL);
        auto search = [](const std::wstring& u) { return u.rfind(L"https://www.google.com/search", 0) == 0; };
        const Counts c = ApplyHistory(h, cr.history.visits, cr.history.beyond, now, search);
        OK(c.imported == 2 && c.present == 1 && c.unsupported == 3 && c.full == 0);   // search, javascript:, chrome:
        bool kept = false; for (const auto& v : h.Items()) if (v.url == L"https://example.com/b") kept = v.title == L"mine" && v.last == 1750000000000LL && v.visits == 2;
        OK(kept);   // existing page: title and later visit kept, count = max
        const AddressBar::History once = h;
        const Counts again = ApplyHistory(h, cr.history.visits, 0, now, search);
        OK(h == once && again.imported == 0 && again.present == 3);   // idempotent
    }
    {   // Full history: nothing evicted, the rest reported.
        AddressBar::History h; for (std::size_t i = 0; i < AddressBar::History::Limit; ++i) h.Record(L"https://full.example/" + std::to_wstring(i), L"", 5);
        const auto before = h; const Counts c = ApplyHistory(h, cr.history.visits, 12, now);
        OK(h == before && c.imported == 0 && c.full == 12 + 4);
    }
    {   // Schema without `hidden` (older/forked Chromium): fallback query.
        Source p2 = DiscoverChromium(roots, Browser::Chrome)[1]; Options o; o.history = true; o.tempRoot = temp;
        const auto r = Read(p2, o); OK(r.history.failure == Failure::None && r.history.visits.size() == 7);
        // Unknown schema → unsupported, never a crash.
        const std::wstring odd = work + L"\\odd\\Default"; { Db d(odd + L"\\History"); d.Exec("CREATE TABLE something(x)"); }
        Source s = p2; s.dir = odd; s.root = Parent(odd); OK(Read(s, o).history.failure == Failure::Unsupported);
        // Not a database at all.
        const std::wstring junk = work + L"\\junk\\Default"; Put(junk + L"\\History", std::string(4096, 'Z'));
        s.dir = junk; s.root = Parent(junk); OK(Read(s, o).history.failure == Failure::Malformed);
        OK(Empty(temp));
    }
    {   // Source in use by the browser: a writer handle (as a running browser holds) → Busy, source unchanged.
        Source s = chromeDefault; Options o; o.history = true; o.tempRoot = temp;
        const auto before = Tree(chrome);
        { Db holder(s.dir + L"\\History"); holder.Exec("SELECT count(*) FROM urls"); OK(Read(s, o).history.failure == Failure::Busy); }
        { Db holder(s.dir + L"\\History"); holder.Exec("PRAGMA locking_mode=EXCLUSIVE"); holder.Exec("BEGIN EXCLUSIVE"); OK(Read(s, o).history.failure == Failure::Busy); holder.Exec("COMMIT"); }
        OK(Tree(chrome) == before && Empty(temp));
        // Read-only source files: still read, still untouched.
        SetFileAttributesW((s.dir + L"\\History").c_str(), FILE_ATTRIBUTE_READONLY);
        OK(Read(s, o).history.visits.size() == 6);
        SetFileAttributesW((s.dir + L"\\History").c_str(), FILE_ATTRIBUTE_NORMAL);
        OK(Tree(chrome) == before && Empty(temp));
    }

    // ---- Firefox (rollback journal, then WAL with committed rows only in the -wal) ------------------------------
    {
        const Source ff = DiscoverFirefox(roots)[0];
        const auto before = Tree(firefox);
        const auto r = Read(ff, both);
        OK(Tree(firefox) == before && Empty(temp));
        OK(r.bookmarks.failure == Failure::None && r.history.failure == Failure::None);
        OK(r.bookmarks.roots.size() == 2);   // toolbar, menu; tags never; unfiled only had a damaged row
        OK(r.bookmarks.roots[0].title == L"Barre de favoris" && r.bookmarks.roots[1].title == L"Menu des marque-pages");
        const auto& tb = r.bookmarks.roots[0].children;
        OK(tb.size() == 3 && tb[0].title == L"first" && tb[1].title == L"second" && tb[2].folder && tb[2].title == L"Dossier 日本" && tb[2].children[0].title == L"inside");
        OK(tb[1].url == L"https://unicode.example/é" && tb[0].added == 1690000000000LL);
        OK(r.bookmarks.ignored == 1);
        OK(r.history.visits.size() == 2 && r.history.visits[0].title == L"Été ☀ 🙂" && r.history.visits[0].last == 1700000100000LL);
        BM::Store s; const Counts c = ApplyBookmarks(s, FolderTitle(ff), r.bookmarks.roots, now);
        OK(c.imported == 3 && c.unsupported == 1);   // place: URL

        // WAL: the database file alone lacks the rows; they are committed in the -wal of a crashed session.
        const std::wstring live = work + L"\\ffwal\\live", crashed = work + L"\\ffwal\\Profiles\\crashed";
        MakeDirs(crashed);
        {
            Places(live + L"\\places.sqlite", true);
            Db w(live + L"\\places.sqlite"); w.Exec("PRAGMA wal_autocheckpoint=0");
            w.Exec("INSERT INTO moz_places VALUES(9, 'https://only-in-wal.example/', 'wal', 1, 0, 1700000300000000)");
            // Copy database + -wal while the writer is open: the state a crash leaves behind (no -shm copied).
            CHECK(CopyFileW((live + L"\\places.sqlite").c_str(), (crashed + L"\\places.sqlite").c_str(), FALSE));
            CHECK(CopyFileW((live + L"\\places.sqlite-wal").c_str(), (crashed + L"\\places.sqlite-wal").c_str(), FALSE));
            // While the writer (a running Firefox) is open: Busy, and its -shm/-wal are left alone.
            Source running = ff; running.dir = live; running.root = Parent(live); const auto liveBefore = Tree(live + L"\\..");
            OK(Read(running, both).history.failure == Failure::Busy);
            OK(Tree(live + L"\\..") == liveBefore);
        }
        Source s2 = ff; s2.dir = crashed; s2.root = Parent(crashed);
        const auto crashedBefore = Tree(crashed);
        OK(crashedBefore.count(L"\\places.sqlite-wal") && !crashedBefore.count(L"\\places.sqlite-shm"));
        // The source folder refuses new files (deny add-file / add-subfolder ACE): the read needs none.
        PSECURITY_DESCRIPTOR deny = nullptr, open = nullptr;
        CHECK(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(D;;0x6;;;WD)(A;OICI;FA;;;WD)", SDDL_REVISION_1, &deny, nullptr));
        CHECK(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;OICI;FA;;;WD)", SDDL_REVISION_1, &open, nullptr));
        CHECK(SetFileSecurityW(crashed.c_str(), DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, deny));
        CHECK(CreateFileW((crashed + L"\\probe").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, 0, nullptr) == INVALID_HANDLE_VALUE);
        const auto rw = Read(s2, both);
        CHECK(SetFileSecurityW(crashed.c_str(), DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, open));
        LocalFree(deny); LocalFree(open);
        OK(Tree(crashed) == crashedBefore);    // no -shm created, the -wal not checkpointed into the source
        OK(Empty(temp));
        OK(rw.history.failure == Failure::None && rw.history.visits.size() == 3 && rw.history.visits[0].url == L"https://only-in-wal.example/");
    }

    // ---- HTML bookmark file ---------------------------------------------------------------------------------
    {
        const std::string html =
            "<!DOCTYPE NETSCAPE-Bookmark-file-1>\n<!-- This is an automatically generated file. <A HREF=\"https://comment.example/\">no</A> -->\n"
            "<META HTTP-EQUIV=\"Content-Type\" CONTENT=\"text/html; charset=UTF-8\">\n<TITLE>Bookmarks</TITLE>\n<H1>Bookmarks</H1>\n<DL><p>\n"
            "    <DT><H3 ADD_DATE=\"1600000000\" PERSONAL_TOOLBAR_FOLDER=\"true\">Barre &amp; outils</H3>\n    <DL><p>\n"
            "        <DT><A HREF=\"https://a.example/?x=1&amp;y=2\" ADD_DATE=\"1700000000\" ICON=\"data:image/png;base64,AAAA\">A &lt;b&gt; <b>gras</b> &#233;t&#xE9; &#128512;</A>\n"
            "        <DT><H3>Vide</H3>\n        <DL><p>\n        </DL><p>\n"
            "        <DT><A HREF=\"javascript:alert(1)\">js</A>\n"
            "    </DL><p>\n"
            "    <script>document.write('<DT><A HREF=\"https://script.example/\">no</A>')</script>\n"
            "    <DT><a href='https://lower.example/' add_date=1700000001000>lower</a>\n"
            "    <DT><A>no href</A>\n"
            "</DL><p>\n";
        const std::wstring file = work + L"\\exports\\favoris é#%.html"; Put(file, html);
        const auto before = Tree(work + L"\\exports");
        Options o; o.bookmarks = true; o.history = true;
        const auto r = Read(HtmlSource(file), o);
        OK(Tree(work + L"\\exports") == before);
        OK(r.bookmarks.failure == Failure::None && r.history.visits.empty() && r.bookmarks.ignored == 1);
        OK(r.bookmarks.roots.size() == 2);
        const auto& bar = r.bookmarks.roots[0];
        OK(bar.folder && bar.title == L"Barre & outils" && bar.added == 1600000000000LL && bar.children.size() == 3);
        OK(bar.children[0].url == L"https://a.example/?x=1&y=2" && bar.children[0].title == L"A <b> gras été 😀" && bar.children[0].added == 1700000000000LL);
        OK(bar.children[1].folder && bar.children[1].title == L"Vide" && bar.children[1].children.empty());
        OK(r.bookmarks.roots[1].url == L"https://lower.example/" && r.bookmarks.roots[1].added == 1700000001000LL);
        BM::Store s; const Counts c = ApplyBookmarks(s, FolderTitle(HtmlSource(file)), r.bookmarks.roots, now);
        OK(c.imported == 2 && c.unsupported == 1 && c.folders == 3);
        // Windows-1252 legacy export.
        const auto legacy = ParseHtmlBookmarks("<DL><DT><A HREF=\"https://e.example/\">caf\xE9</A></DL>");
        OK(legacy.failure == Failure::None && legacy.roots.size() == 1 && legacy.roots[0].title == L"café");
        OK(ParseHtmlBookmarks("just text, no list").failure == Failure::Malformed);
        std::string deep = "<DL>"; for (int i = 0; i < 300; ++i) deep += "<DT><H3>f</H3><DL>"; deep += "<DT><A HREF=\"https://x.example/\">x</A>";
        OK(ParseHtmlBookmarks(deep).failure == Failure::TooDeep);
        Put(work + L"\\exports\\big.html", std::string(static_cast<std::size_t>(Bound::htmlBytes) + 1, 'x'));
        OK(Read(HtmlSource(work + L"\\exports\\big.html"), o).bookmarks.failure == Failure::TooLarge);
        // A reparse point instead of a file is refused.
        const std::wstring link = work + L"\\exports\\link.html";
        if (CreateSymbolicLinkW(link.c_str(), file.c_str(), 0x2 /* ALLOW_UNPRIVILEGED_CREATE */)) OK(Read(HtmlSource(link), o).bookmarks.failure == Failure::Unreadable);
        else std::printf("note: symbolic links unavailable without developer mode; junction case covered by discovery\n");
    }

    // ---- Private copies left by a process that ended mid-read are swept by the next snapshot --------------------
    {
        STARTUPINFOW si{sizeof si}; PROCESS_INFORMATION pi{}; wchar_t line[] = L"cmd.exe /c exit 0";
        CHECK(CreateProcessW(nullptr, line, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi));
        WaitForSingleObject(pi.hProcess, 10000); const DWORD dead = pi.dwProcessId; CloseHandle(pi.hThread); CloseHandle(pi.hProcess);   // handle closed: the id is free
        const std::wstring stale = temp + L"\\browser-import-" + std::to_wstring(dead) + L"-1a2b", mine = temp + L"\\browser-import-" + std::to_wstring(GetCurrentProcessId()) + L"-ff", other = temp + L"\\browser-import-notapid-1", extra = temp + L"\\browser-import-" + std::to_wstring(dead) + L"-77";
        Put(stale + L"\\db", "x"); Put(stale + L"\\db-wal", "y"); Put(mine + L"\\db", "x"); Put(other + L"\\db", "x"); Put(extra + L"\\db", "x"); Put(extra + L"\\keep.txt", "not ours");
        Source s = chromeDefault; Options o; o.history = true; o.tempRoot = temp;
        OK(Read(s, o).history.failure == Failure::None);
        OK(!IsDirectory(stale) && IsDirectory(mine) && IsDirectory(other) && IsDirectory(extra) && IsFile(extra + L"\\keep.txt"));   // only a dead process's own copy files
        RemoveTree(mine); RemoveTree(other); RemoveTree(extra);
        OK(Empty(temp));
    }

    // ---- Stable destination folders (review 934ef4b finding 2) --------------------------------------------------
    {
        using I18n::Locale;
        const std::string json = ChromeBookmarks();
        auto parseIn = [&](Locale l) { I18n::SetForProcess(l); auto r = ParseChromiumBookmarks(json); I18n::SetForProcess(Locale::Fr); return r.roots; };
        const auto fr = parseIn(Locale::Fr), en = parseIn(Locale::En);
        OK(fr[0].key == "bookmark_bar" && en[0].key == "bookmark_bar" && fr[0].title != en[0].title);   // displayed names differ, identity does not
        Source chromeD = chromeDefault; chromeD.soleProfile = true;
        auto importIn = [&](BM::Store& s, FolderMap& m, Locale l, const Source& src, const std::vector<Item>& roots) { I18n::SetForProcess(l); const Counts c = ApplyBookmarks(s, src, roots, now, &m); I18n::SetForProcess(Locale::Fr); return c; };
        {   // FR → EN → FR: one container, one folder per root, nothing added after the first import.
            BM::Store s; FolderMap m;
            const Counts a = importIn(s, m, Locale::Fr, chromeD, fr);
            const BM::Store once = s;
            const Counts b = importIn(s, m, Locale::En, chromeD, en);
            const Counts c = importIn(s, m, Locale::Fr, chromeD, fr);
            OK(a.imported == 6 && b.imported == 0 && b.present == 6 && b.folders == 0 && c.imported == 0 && c.present == 6 && s == once);
            OK(s.Find(BM::OtherRoot)->children.size() == 1 && m.entries.size() == 3);   // container + bar + other
            // The reviewer's probe, in this form: FR import then an EN repeat → present, one group.
            BM::Store p; FolderMap pm; importIn(p, pm, Locale::Fr, chromeD, fr); const Counts pe = importIn(p, pm, Locale::En, chromeD, en);
            OK(pe.imported == 0 && pe.present == 6 && p.Find(BM::OtherRoot)->children.size() == 1);
        }
        {   // Map lost (or never written): the titles of both languages still find the folders.
            BM::Store s; FolderMap m1, m2;
            importIn(s, m1, Locale::Fr, chromeD, fr);
            const Counts b = importIn(s, m2, Locale::En, chromeD, en);
            OK(b.imported == 0 && b.present == 6 && b.folders == 0 && s.Find(BM::OtherRoot)->children.size() == 1);
        }
        {   // The source gains another profile (its label changes), the person renamed and moved the folders.
            BM::Store s; FolderMap m;
            importIn(s, m, Locale::Fr, chromeD, fr);
            const BM::Id box = s.Find(BM::OtherRoot)->children[0], bar = s.Find(box)->children[0];
            CHECK(s.Rename(box, L"Mes imports") && s.Rename(bar, L"Ma barre") && s.Move(box, BM::BarRoot, BM::End));
            Source multi = chromeD; multi.soleProfile = false;   // « Chrome — profil principal » now
            const BM::Store before = s;
            const Counts c = importIn(s, m, Locale::En, multi, en);
            OK(c.imported == 0 && c.present == 6 && c.folders == 0 && s == before);
            OK(s.Find(box)->title == L"Mes imports" && s.Find(bar)->title == L"Ma barre" && s.Find(box)->parent == BM::BarRoot);   // the person's names and place kept
            // Another profile of the same browser never adopts that folder, even with the sole-profile title free.
            Source p2 = multi; p2.profile = L"Profile 2"; p2.isDefault = false; p2.dir = chromeD.root + L"\\Profile 2";
            const Counts d = importIn(s, m, Locale::Fr, p2, fr);
            OK(d.imported == 6 && Child(s, BM::OtherRoot, L"Importés de Chrome — Profile 2") != BM::NoId && m.entries.size() == 6);
            // Deleted by the person: the next import creates it again (and maps the new one).
            CHECK(s.Remove(box));
            const Counts e = importIn(s, m, Locale::Fr, multi, fr);
            OK(e.imported == 6 && Child(s, BM::OtherRoot, L"Importés de Chrome — profil principal") != BM::NoId);
        }
        {   // Folders repeated on purpose (same name, same parent) stay distinct across imports and languages.
            const std::string dup = "{\"roots\":{\"bookmark_bar\":" + ChromeFolder(L"bar", {ChromeFolder(L"Dup", {ChromeNode(L"a", "https://a.example/")}), ChromeFolder(L"Dup", {ChromeNode(L"b", "https://b.example/")}), ChromeNode(L"x", "https://x.example/"), ChromeNode(L"x", "https://x.example/")}) + "}}";
            I18n::SetForProcess(Locale::Fr); const auto dfr = ParseChromiumBookmarks(dup).roots; I18n::SetForProcess(Locale::En); const auto den = ParseChromiumBookmarks(dup).roots; I18n::SetForProcess(Locale::Fr);
            BM::Store s; FolderMap m;
            const Counts a = importIn(s, m, Locale::Fr, chromeD, dfr), b = importIn(s, m, Locale::En, chromeD, den);
            const BM::Id barF = s.Find(s.Find(BM::OtherRoot)->children[0])->children[0];
            OK(a.imported == 4 && b.imported == 0 && b.present == 4 && (Titles(s, barF) == std::vector<std::wstring>{L"Dup", L"Dup", L"x", L"x"}));
        }
        {   // Firefox roots by GUID; HTML (no roots) keeps one container in either language.
            Item tb; tb.folder = true; tb.key = "toolbar_____"; tb.title = L"Barre de favoris"; Item l; l.url = L"https://f.example/"; l.title = L"f"; tb.children.push_back(l);
            Item tbEn = tb; tbEn.title = L"Bookmarks bar";
            Source ffs; ffs.browser = Browser::Firefox; ffs.root = L"C:\\x\\Firefox"; ffs.dir = L"C:\\x\\Firefox\\Profiles\\a.default"; ffs.profile = L"default"; ffs.soleProfile = true;
            BM::Store s; FolderMap m;
            importIn(s, m, Locale::Fr, ffs, {tb}); const Counts b = importIn(s, m, Locale::En, ffs, {tbEn});
            OK(b.present == 1 && b.imported == 0 && m.entries.count("firefox:Firefox/Profiles/a.default|toolbar_____") == 1);
            Item h; h.url = L"https://h.example/"; h.title = L"h";
            importIn(s, m, Locale::Fr, HtmlSource(L"C:\\a.html"), {h}); const Counts hb = importIn(s, m, Locale::En, HtmlSource(L"C:\\b.html"), {h});
            OK(hb.present == 1 && s.Find(BM::OtherRoot)->children.size() == 2);
        }
        {   // On disk: the map is written in the same transaction scope, validated, never trusted blindly.
            const std::wstring data = work + L"\\mapped"; MakeDirs(data);
            const std::wstring bm = data + L"\\bookmarks", map = data + L"\\bookmark-imports";
            BM::Store base, mine; bool saved = false;
            I18n::SetForProcess(Locale::Fr);
            const auto c1 = CommitBookmarks(data, bm, map, base, mine, chromeD, fr, now, &saved);
            std::string mapBytes, bmBytes; ProfileStore::ReadAll(map, mapBytes); ProfileStore::ReadAll(bm, bmBytes);
            OK(c1.Saved() && saved && c1.counts.imported == 6 && mapBytes.rfind("browser-bookmark-imports 1\n", 0) == 0);
            { std::istringstream in(bmBytes); bool damaged = true; BM::Store::Read(in, &damaged); OK(!damaged && bmBytes.rfind("browser-bookmarks 1\n", 0) == 0); }   // bookmarks format unchanged
            I18n::SetForProcess(Locale::En);
            const auto c2 = CommitBookmarks(data, bm, map, base, mine, chromeD, en, now, &saved);
            I18n::SetForProcess(Locale::Fr);
            std::string bm2; ProfileStore::ReadAll(bm, bm2);
            OK(c2.result == ProfileStore::Result::Unchanged && c2.counts.present == 6 && bm2 == bmBytes && saved);
            // Map unwritable (a folder in its place): identities first, so NOTHING is imported and the category fails.
            DeleteFileW(map.c_str()); CreateDirectoryW(map.c_str(), nullptr);
            Item extra; extra.url = L"https://new.example/"; extra.title = L"n"; auto more = fr; more[0].children.push_back(extra);
            const BM::Store b3 = base, m3 = mine;
            const auto c3 = CommitBookmarks(data, bm, map, base, mine, chromeD, more, now, &saved);
            std::string bm3; ProfileStore::ReadAll(bm, bm3);
            OK(!c3.Saved() && c3.identity == Identity::Unwritable && !saved && c3.counts == Counts{} && bm3 == bmBytes && base == b3 && mine == m3);
            RemoveDirectoryW(map.c_str());
            // Map lost (deleted outside the browser): titles of either language still find everything; the map comes back.
            I18n::SetForProcess(Locale::En);
            const auto c4 = CommitBookmarks(data, bm, map, base, mine, chromeD, en, now, &saved);
            I18n::SetForProcess(Locale::Fr);
            OK(c4.Saved() && saved && c4.counts.imported == 0 && c4.counts.present == 6 && mine.Find(BM::OtherRoot)->children.size() == 1);
            // A map of another version is neither used nor rewritten, and nothing is imported (no identity to keep).
            std::string bm4; ProfileStore::ReadAll(bm, bm4);
            const std::string future = "browser-bookmark-imports 9\nwhatever\n"; CHECK(ProfileStore::WriteAtomic(map, future));
            const auto c5 = CommitBookmarks(data, bm, map, base, mine, chromeD, more, now, &saved);
            std::string still, bm5; ProfileStore::ReadAll(map, still); ProfileStore::ReadAll(bm, bm5);
            OK(!c5.Saved() && c5.identity == Identity::Newer && !saved && still == future && bm5 == bm4);
            // A stale entry (no such folder) is never trusted and is dropped.
            CHECK(ProfileStore::WriteAtomic(map, "browser-bookmark-imports 1\nchromium:Chrome/Default\t99999\t5\nbroken line\n"));
            const auto c6 = CommitBookmarks(data, bm, map, base, mine, chromeD, fr, now, &saved);
            std::string rewritten; ProfileStore::ReadAll(map, rewritten);
            OK(c6.Saved() && saved && c6.counts.present == 6 && mine.Find(BM::OtherRoot)->children.size() == 1 && rewritten.find("99999") == std::string::npos);
        }
        {   // Review 99959e3: map target locked (deny-delete reader, as an indexer) → refused; then rename + other
            // language + profile label change → still one group. Fresh folder, synthetic data only.
            const std::wstring data = work + L"\\durable"; MakeDirs(data);
            const std::wstring bm = data + L"\\bookmarks", map = data + L"\\bookmark-imports";
            const std::string json = "{\"roots\":{\"bookmark_bar\":{\"type\":\"folder\",\"name\":\"bar\",\"children\":[{\"type\":\"url\",\"name\":\"s\",\"url\":\"https://fresh.example/one\"}]}}}";
            I18n::SetForProcess(Locale::Fr); const auto frr = ParseChromiumBookmarks(json).roots;
            I18n::SetForProcess(Locale::En); const auto enr = ParseChromiumBookmarks(json).roots; I18n::SetForProcess(Locale::Fr);
            Source src = chromeD; BM::Store base, mine; bool saved = false;
            CHECK(ProfileStore::WriteAtomic(map, std::string(FolderMap::Header) + "\n"));
            HANDLE held = CreateFileW(map.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
            CHECK(held != INVALID_HANDLE_VALUE);
            const auto a = CommitBookmarks(data, bm, map, base, mine, src, frr, now, &saved);
            CloseHandle(held);
            OK(!a.Saved() && a.identity == Identity::Unwritable && !saved && !IsFile(bm) && mine.Size() == 0);   // refused, nothing written
            const auto a2 = CommitBookmarks(data, bm, map, base, mine, src, frr, now, &saved);   // retry once the lock is gone
            OK(a2.Saved() && saved && a2.counts.imported == 1);
            const BM::Id box = mine.Find(BM::OtherRoot)->children[0], bar = mine.Find(box)->children[0];
            CHECK(mine.Rename(box, L"My custom container") && mine.Rename(bar, L"My custom root"));
            { std::ostringstream o; mine.Write(o); CHECK(ProfileStore::WriteAtomic(bm, o.str())); base = mine; }
            I18n::SetForProcess(Locale::En); src.soleProfile = false;
            const auto b = CommitBookmarks(data, bm, map, base, mine, src, enr, now + 100, &saved);
            I18n::SetForProcess(Locale::Fr);
            OK(b.Saved() && b.counts.imported == 0 && b.counts.present == 1 && mine.Find(BM::OtherRoot)->children.size() == 1 && mine.Find(box)->title == L"My custom container" && mine.Find(bar)->title == L"My custom root");

            // Interrupted at the durable boundary: identities written, bookmarks NOT (target read-only = a failed write,
            // the state a crash between the two writes leaves). Nothing is claimed; the dangling entries are never
            // trusted, not even when another window later creates a folder with the same id; the retry is exact.
            const std::wstring data2 = work + L"\\durable2"; MakeDirs(data2);
            const std::wstring bm2 = data2 + L"\\bookmarks", map2 = data2 + L"\\bookmark-imports";
            BM::Store base2, mine2; Source src2 = chromeD;
            { BM::Store seed; seed.Add(BM::BarRoot, BM::End, L"mine", L"https://mine.example/", 1); std::ostringstream o; seed.Write(o); CHECK(ProfileStore::WriteAtomic(bm2, o.str())); }
            std::string seedBytes; ProfileStore::ReadAll(bm2, seedBytes);
            SetFileAttributesW(bm2.c_str(), FILE_ATTRIBUTE_READONLY);
            const auto i1 = CommitBookmarks(data2, bm2, map2, base2, mine2, src2, frr, now, &saved);
            SetFileAttributesW(bm2.c_str(), FILE_ATTRIBUTE_NORMAL);
            std::string afterFail, mapAfterFail; ProfileStore::ReadAll(bm2, afterFail); ProfileStore::ReadAll(map2, mapAfterFail);
            OK(!i1.Saved() && i1.counts == Counts{} && afterFail == seedBytes && mapAfterFail.find("chromium:Chrome/Default\t") != std::string::npos);   // map ahead, bookmarks untouched
            // Another window (same profile, under the same lock discipline) adds a folder that takes the dangling id.
            const FolderMap dangling = FolderMap::Read(mapAfterFail, nullptr);
            const BM::Id danglingId = dangling.entries.at("chromium:Chrome/Default").front().id;
            {
                BM::Store other; std::istringstream in(afterFail); other = BM::Store::Read(in);
                BM::Id got = BM::NoId; while ((got = other.AddFolder(BM::OtherRoot, BM::End, L"Other window", now + 999)) != BM::NoId && got < danglingId) {}
                CHECK(got == danglingId && other.Find(danglingId)->added != dangling.entries.at("chromium:Chrome/Default").front().added);
                std::ostringstream o; other.Write(o); CHECK(ProfileStore::WriteAtomic(bm2, o.str()));
            }
            const auto i2 = CommitBookmarks(data2, bm2, map2, base2, mine2, src2, frr, now + 1, &saved);
            OK(i2.Saved() && saved && i2.counts.imported == 1 && mine2.Find(danglingId)->title == L"Other window" && mine2.Find(danglingId)->children.empty());   // never adopted
            OK(Child(mine2, BM::OtherRoot, L"Importés de Chrome") != BM::NoId && Child(mine2, BM::BarRoot, L"mine") != BM::NoId);
            // Then the person renames the imported folders; another language and label: still exactly one group.
            const BM::Id box2 = Child(mine2, BM::OtherRoot, L"Importés de Chrome");
            CHECK(mine2.Rename(box2, L"Renamed after recovery"));
            { std::ostringstream o; mine2.Write(o); CHECK(ProfileStore::WriteAtomic(bm2, o.str())); base2 = mine2; }
            I18n::SetForProcess(Locale::En); src2.soleProfile = false;
            const auto i3 = CommitBookmarks(data2, bm2, map2, base2, mine2, src2, enr, now + 2, &saved);
            I18n::SetForProcess(Locale::Fr);
            std::size_t groups = 0; for (BM::Id id : mine2.Find(BM::OtherRoot)->children) if (mine2.Find(id)->kind == BM::Kind::Folder && mine2.Find(id)->title != L"Other window") ++groups;
            OK(i3.Saved() && i3.counts.imported == 0 && i3.counts.present == 1 && groups == 1 && mine2.Find(box2)->title == L"Renamed after recovery");
            // Another window renames it again between two imports (its own commit under the profile lock): still found.
            { BM::Store other; std::string cur; ProfileStore::ReadAll(bm2, cur); std::istringstream in(cur); other = BM::Store::Read(in); CHECK(other.Rename(box2, L"Renamed elsewhere")); std::ostringstream o; other.Write(o); ProfileStore::Lock l(data2); CHECK(ProfileStore::WriteAtomic(bm2, o.str())); }
            const auto i4 = CommitBookmarks(data2, bm2, map2, base2, mine2, src2, frr, now + 3, &saved);
            OK(i4.Saved() && i4.counts.present == 1 && i4.counts.imported == 0 && mine2.Find(box2)->title == L"Renamed elsewhere");
            { std::string f; ProfileStore::ReadAll(bm2, f); std::istringstream in(f); bool damaged = true; BM::Store::Read(in, &damaged); OK(!damaged && f.rfind("browser-bookmarks 1\n", 0) == 0); }   // format unchanged
        }
    }

    // ---- Existing mapped folder + pending deletion at the map-ahead boundary (review ff1bcbe) ------------------
    {
        using I18n::Locale;
        const std::string json = "{\"roots\":{\"bookmark_bar\":{\"type\":\"folder\",\"name\":\"bar\",\"children\":[{\"type\":\"url\",\"name\":\"s\",\"url\":\"https://fresh.example/one\"}]}}}";
        I18n::SetForProcess(Locale::Fr); const auto frr = ParseChromiumBookmarks(json).roots;
        I18n::SetForProcess(Locale::En); const auto enr = ParseChromiumBookmarks(json).roots; I18n::SetForProcess(Locale::Fr);
        Source chromeD = chromeDefault; chromeD.soleProfile = true;
        auto groupsOf = [](const BM::Store& s) { std::size_t g = 0; for (BM::Id id : s.Find(BM::OtherRoot)->children) if (s.Find(id)->kind == BM::Kind::Folder) ++g; return g; };
        auto reload = [](const std::wstring& file, BM::Store& base, BM::Store& mine) { std::string b; ProfileStore::ReadAll(file, b); std::istringstream in(b); mine = BM::Store::Read(in); base = mine; };
        // Imported, mapped, renamed by the person, then this window deletes the container but that save fails.
        auto prepare = [&](const std::wstring& data, BM::Store& base, BM::Store& mine, Source& src, BM::Id& box, BM::Id& bar) {
            MakeDirs(data); bool saved = false;
            const auto first = CommitBookmarks(data, data + L"\\bookmarks", data + L"\\bookmark-imports", base, mine, src, frr, now, &saved);
            CHECK(first.Saved() && saved && first.counts.imported == 1);
            box = mine.Find(BM::OtherRoot)->children[0]; bar = mine.Find(box)->children[0];
            CHECK(mine.Rename(box, L"Custom container") && mine.Rename(bar, L"Custom root"));
            { std::ostringstream o; mine.Write(o); CHECK(ProfileStore::WriteAtomic(data + L"\\bookmarks", o.str())); base = mine; }
            CHECK(mine.Remove(box));   // pending deletion, only in this window's memory
        };
        {   // (1) The bookmarks write fails after the map was written.
            const std::wstring data = work + L"\\boundary-fail"; Source src = chromeD; BM::Store base, mine; BM::Id box = 0, bar = 0;
            prepare(data, base, mine, src, box, bar);
            std::string diskBefore; ProfileStore::ReadAll(data + L"\\bookmarks", diskBefore);
            SetFileAttributesW((data + L"\\bookmarks").c_str(), FILE_ATTRIBUTE_READONLY);
            bool saved = true; I18n::SetForProcess(Locale::En); src.soleProfile = false;
            const auto failed = CommitBookmarks(data, data + L"\\bookmarks", data + L"\\bookmark-imports", base, mine, src, enr, now + 1, &saved);
            SetFileAttributesW((data + L"\\bookmarks").c_str(), FILE_ATTRIBUTE_NORMAL);
            std::string diskAfter, mapAfter; ProfileStore::ReadAll(data + L"\\bookmarks", diskAfter); ProfileStore::ReadAll(data + L"\\bookmark-imports", mapAfter);
            const FolderMap m = FolderMap::Read(mapAfter, nullptr);
            OK(!failed.Saved() && !saved && failed.counts == Counts{} && diskAfter == diskBefore);
            OK(m.entries.at("chromium:Chrome/Default").size() == 2 && m.entries.at("chromium:Chrome/Default").back().id == box);   // new ahead, old kept
            // Restart: this window's unsaved deletion is gone, the disk still has the renamed folders.
            reload(data + L"\\bookmarks", base, mine);
            const auto again = CommitBookmarks(data, data + L"\\bookmarks", data + L"\\bookmark-imports", base, mine, src, enr, now + 2, &saved);
            I18n::SetForProcess(Locale::Fr);
            OK(again.Saved() && saved && again.counts.imported == 0 && again.counts.present == 1 && groupsOf(mine) == 1 && mine.Find(box)->title == L"Custom container" && mine.Find(bar)->title == L"Custom root");
            std::string mapNow; ProfileStore::ReadAll(data + L"\\bookmark-imports", mapNow);
            OK(FolderMap::Read(mapNow, nullptr).entries.at("chromium:Chrome/Default").size() == 1);   // the dangling candidate pruned
            // And French again, sole label: still that one group.
            const auto fr2 = CommitBookmarks(data, data + L"\\bookmarks", data + L"\\bookmark-imports", base, mine, chromeD, frr, now + 3, &saved);
            OK(fr2.Saved() && fr2.counts.imported == 0 && fr2.counts.present == 1 && groupsOf(mine) == 1);
        }
        {   // (2) Interrupted exactly at the boundary: the committing thread stops right after the map write (the
            //     profile mutex is abandoned, nothing after the map write runs: the state a crash leaves).
            const std::wstring data = work + L"\\boundary-crash"; Source src = chromeD; BM::Store base, mine; BM::Id box = 0, bar = 0;
            prepare(data, base, mine, src, box, bar);
            std::string diskBefore; ProfileStore::ReadAll(data + L"\\bookmarks", diskBefore);
            struct Ctx { std::wstring data; BM::Store* base; BM::Store* mine; Source src; const std::vector<Item>* roots; std::int64_t now; } ctx{data, &base, &mine, src, &enr, now + 1};
            ctx.src.soleProfile = false;
            MapWrittenForTest() = [] { ExitThread(0); };
            I18n::SetForProcess(Locale::En);
            HANDLE t = CreateThread(nullptr, 1u << 20, [](LPVOID p) -> DWORD { auto* c = static_cast<Ctx*>(p); bool saved = false; CommitBookmarks(c->data, c->data + L"\\bookmarks", c->data + L"\\bookmark-imports", *c->base, *c->mine, c->src, *c->roots, c->now, &saved); return 7; }, &ctx, 0, nullptr);
            WaitForSingleObject(t, 20000); DWORD code = 0; GetExitCodeThread(t, &code); CloseHandle(t);
            MapWrittenForTest() = nullptr;
            std::string diskAfter, mapAfter; ProfileStore::ReadAll(data + L"\\bookmarks", diskAfter); ProfileStore::ReadAll(data + L"\\bookmark-imports", mapAfter);
            OK(code == 0 && diskAfter == diskBefore && FolderMap::Read(mapAfter, nullptr).entries.at("chromium:Chrome/Default").size() == 2);   // stopped between the writes
            // Restart (fresh stores from disk; the abandoned profile mutex is taken over), English, multi-profile label.
            BM::Store base2, mine2; reload(data + L"\\bookmarks", base2, mine2);
            Source src2 = chromeD; src2.soleProfile = false; bool saved = false;
            const auto again = CommitBookmarks(data, data + L"\\bookmarks", data + L"\\bookmark-imports", base2, mine2, src2, enr, now + 2, &saved);
            I18n::SetForProcess(Locale::Fr);
            OK(again.Saved() && saved && again.counts.imported == 0 && again.counts.present == 1 && groupsOf(mine2) == 1 && mine2.Find(box)->title == L"Custom container" && mine2.Find(bar)->title == L"Custom root");
            // The person's deletion, saved for real this time, then an import: a new group, and the old candidate goes.
            CHECK(mine2.Remove(box));
            const auto del = CommitBookmarks(data, data + L"\\bookmarks", data + L"\\bookmark-imports", base2, mine2, src2, frr, now + 3, &saved);
            OK(del.Saved() && del.counts.imported == 1 && groupsOf(mine2) == 1 && !mine2.Find(box));
            const auto next = CommitBookmarks(data, data + L"\\bookmarks", data + L"\\bookmark-imports", base2, mine2, src2, frr, now + 4, &saved);
            std::string mapEnd; ProfileStore::ReadAll(data + L"\\bookmark-imports", mapEnd);
            OK(next.Saved() && next.counts.present == 1 && next.counts.imported == 0 && FolderMap::Read(mapEnd, nullptr).entries.at("chromium:Chrome/Default").size() == 1);
        }
        {   // Map format: candidates in order, duplicates and excess dropped, unknown version untouched (other tests).
            const FolderMap m = FolderMap::Read("browser-bookmark-imports 1\nk\t5\t1\nk\t6\t2\nk\t5\t1\nk\t7\t3\nk\t8\t4\nk\t9\t5\n", nullptr);
            OK(m.entries.at("k").size() == FolderMap::Candidates && m.entries.at("k").front().id == 5 && m.Write() == "browser-bookmark-imports 1\nk\t5\t1\nk\t6\t2\nk\t7\t3\nk\t8\t4\n");
        }
    }

    // ---- Read-time substitution (review 934ef4b finding 3): folders pinned again when « Importer » is pressed ------
    {
        const std::wstring sub = work + L"\\subst", local = sub + L"\\Local", ud = local + L"\\Google\\Chrome\\User Data", foreign = sub + L"\\foreign";
        Put(ud + L"\\Default\\Bookmarks", ChromeBookmarks()); ChromiumHistory(ud + L"\\Default\\History");
        Put(foreign + L"\\Bookmarks", ChromeBookmarks()); ChromiumHistory(foreign + L"\\History");
        const auto found = DiscoverChromium(Roots{local, L""}, Browser::Chrome);
        CHECK(found.size() == 1);
        Options o; o.bookmarks = o.history = true; o.tempRoot = temp;
        OK(Read(found[0], o).bookmarks.failure == Failure::None);   // the real profile still reads
        {   // While pinned, the profile folder (and its parents) can be neither renamed nor replaced.
            Pin pin; OK(pin.Open(found[0].root, found[0].dir) == Failure::None);
            OK(!MoveFileW(found[0].dir.c_str(), (ud + L"\\moved").c_str()) && !MoveFileW(ud.c_str(), (local + L"\\Google\\Chrome\\moved").c_str()));
        }
        // The reviewer's case: after discovery the profile folder becomes a junction to a foreign tree.
        const auto foreignBefore = Tree(foreign);
        CHECK(MoveFileW(found[0].dir.c_str(), (ud + L"\\saved").c_str()) && Junction(found[0].dir, foreign));
        const auto r = Read(found[0], o);
        OK(r.bookmarks.failure == Failure::Unreadable && r.bookmarks.roots.empty() && r.history.failure == Failure::Unreadable && r.history.visits.empty());
        OK(Tree(foreign) == foreignBefore && Empty(temp));
        RemoveDirectoryW(found[0].dir.c_str()); CHECK(MoveFileW((ud + L"\\saved").c_str(), found[0].dir.c_str()));
        // The browser root itself replaced by a junction.
        CHECK(MoveFileW(ud.c_str(), (local + L"\\Google\\Chrome\\real").c_str()) && Junction(ud, local + L"\\Google\\Chrome\\real"));
        OK(Read(found[0], o).bookmarks.failure == Failure::Unreadable);
        RemoveDirectoryW(ud.c_str()); CHECK(MoveFileW((local + L"\\Google\\Chrome\\real").c_str(), ud.c_str()));
        OK(Read(found[0], o).bookmarks.failure == Failure::None);   // restored: reads again
        // Firefox: an intermediate folder (« Profiles ») replaced by a junction; database + sidecars never copied.
        const std::wstring ffroot = sub + L"\\Roaming\\Mozilla\\Firefox";
        Places(ffroot + L"\\Profiles\\x.default\\places.sqlite", false);
        Put(ffroot + L"\\profiles.ini", "[Profile0]\nName=x\nIsRelative=1\nPath=Profiles/x.default\n");
        const auto ff = DiscoverFirefox(Roots{L"", sub + L"\\Roaming"});
        CHECK(ff.size() == 1);
        Places(foreign + L"\\elsewhere\\x.default\\places.sqlite", false);
        CHECK(MoveFileW((ffroot + L"\\Profiles").c_str(), (ffroot + L"\\Profiles.saved").c_str()) && Junction(ffroot + L"\\Profiles", foreign + L"\\elsewhere"));
        const auto fr = Read(ff[0], o);
        OK(fr.bookmarks.failure == Failure::Unreadable && fr.history.failure == Failure::Unreadable && Empty(temp));
        RemoveDirectoryW((ffroot + L"\\Profiles").c_str()); CHECK(MoveFileW((ffroot + L"\\Profiles.saved").c_str(), (ffroot + L"\\Profiles").c_str()));
        OK(Read(ff[0], o).bookmarks.failure == Failure::None);
        // HTML: the chosen file's folder is a junction (the file is reached through it).
        Put(foreign + L"\\html\\favoris.html", "<DL><DT><A HREF=\"https://x.example/\">x</A></DL>");
        CHECK(Junction(sub + L"\\via", foreign + L"\\html"));
        Options h; h.bookmarks = true;
        OK(Read(HtmlSource(sub + L"\\via\\favoris.html"), h).bookmarks.failure == Failure::Unreadable);
        OK(Read(HtmlSource(foreign + L"\\html\\favoris.html"), h).bookmarks.failure == Failure::None);
        RemoveDirectoryW((sub + L"\\via").c_str());
    }

    // ---- Commits through the profile store (destination) --------------------------------------------------------
    {
        const std::wstring data = work + L"\\profile"; MakeDirs(data);
        const std::wstring bmFile = data + L"\\bookmarks", hFile = data + L"\\history";
        // Another window already wrote a bookmark and a page this window has not seen.
        { BM::Store other; other.Add(BM::BarRoot, BM::End, L"other window", L"https://other.example/", 5); std::ostringstream o; other.Write(o); CHECK(ProfileStore::WriteAtomic(bmFile, o.str())); }
        { AddressBar::History other; other.Record(L"https://other.example/", L"o", 5); std::ostringstream o; other.Write(o); CHECK(ProfileStore::WriteAtomic(hFile, o.str())); }
        BM::Store base, mine; AddressBar::History hbase, hmine;
        auto c1 = CommitBookmarks(data, bmFile, base, mine, L"Importés de Chrome", cr.bookmarks.roots, now);
        OK(c1.Saved() && c1.counts.imported == 6);
        OK(Child(mine, BM::BarRoot, L"other window") != BM::NoId && Child(mine, BM::OtherRoot, L"Importés de Chrome") != BM::NoId && base == mine);
        auto h1 = CommitHistory(data, hFile, hbase, hmine, cr.history.visits, 0, now);
        OK(h1.Saved() && h1.counts.imported == 4 && hmine.Items().size() == 5);   // other window's page kept
        // Repeat: nothing new, the file stays identical.
        std::string bmBytes, hBytes; ProfileStore::ReadAll(bmFile, bmBytes); ProfileStore::ReadAll(hFile, hBytes);
        auto c2 = CommitBookmarks(data, bmFile, base, mine, L"Importés de Chrome", cr.bookmarks.roots, now);
        auto h2 = CommitHistory(data, hFile, hbase, hmine, cr.history.visits, 0, now);
        OK(c2.result == ProfileStore::Result::Unchanged && c2.counts.imported == 0 && c2.counts.present == 6);
        OK(h2.result == ProfileStore::Result::Unchanged && h2.counts.present == 4);
        std::string bm2, h2b; ProfileStore::ReadAll(bmFile, bm2); ProfileStore::ReadAll(hFile, h2b);
        OK(bm2 == bmBytes && h2b == hBytes);
        // Destination save failure: bookmarks file of an unknown (future) format → nothing written, nothing
        // claimed; history still commits on its own (partial import, reported per category).
        const std::string future = "browser-bookmarks 2\nwhatever\n"; CHECK(ProfileStore::WriteAtomic(bmFile, future));
        const BM::Store bBefore = base, mBefore = mine;
        Item extra; extra.title = L"new"; extra.url = L"https://new.example/"; std::vector<Item> roots{extra};
        auto c3 = CommitBookmarks(data, bmFile, base, mine, L"Importés de Chrome", roots, now);
        std::string stillFuture; ProfileStore::ReadAll(bmFile, stillFuture);
        OK(c3.result == ProfileStore::Result::UnknownFormat && !c3.Saved() && c3.counts == Counts{} && stillFuture == future && base == bBefore && mine == mBefore);
        Visit v; v.url = L"https://partial.example/"; v.last = 1700000000000LL;
        auto h3 = CommitHistory(data, hFile, hbase, hmine, {v}, 0, now);
        OK(h3.Saved() && h3.counts.imported == 1);
        // Damaged history file: backed up, never rewritten, import reported as failed.
        std::string damaged = "browser-history 1\nnot a record\n"; CHECK(ProfileStore::WriteAtomic(hFile, damaged));
        const auto hb = hbase; auto h4 = CommitHistory(data, hFile, hbase, hmine, {v}, 0, now);
        std::string after; ProfileStore::ReadAll(hFile, after);
        OK(!h4.Saved() && after == damaged && hbase == hb);
        // Profile busy (another window holds the lock past the timeout): nothing written.
        {
            CHECK(ProfileStore::WriteAtomic(bmFile, bmBytes));
            HANDLE m = CreateMutexW(nullptr, TRUE, ProfileStore::MutexName(data).c_str()); CHECK(m);
            std::string held; ProfileStore::ReadAll(bmFile, held);
            // The mutex is recursive for this thread: take it from another thread instead.
            ReleaseMutex(m);
            HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr), done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            struct Ctx { std::wstring name; HANDLE ready, done; } ctx{ProfileStore::MutexName(data), ready, done};
            HANDLE t = CreateThread(nullptr, 0, [](LPVOID p) -> DWORD { auto* c = static_cast<Ctx*>(p); HANDLE mm = CreateMutexW(nullptr, FALSE, c->name.c_str()); WaitForSingleObject(mm, INFINITE); SetEvent(c->ready); WaitForSingleObject(c->done, INFINITE); ReleaseMutex(mm); CloseHandle(mm); return 0; }, &ctx, 0, nullptr);
            WaitForSingleObject(ready, 5000);
            BM::Store b2, m2; auto busy = CommitBookmarks(data, bmFile, b2, m2, L"x", roots, now, nullptr, 50);
            SetEvent(done); WaitForSingleObject(t, 5000); CloseHandle(t); CloseHandle(ready); CloseHandle(done); CloseHandle(m);
            std::string unchanged; ProfileStore::ReadAll(bmFile, unchanged);
            OK(busy.result == ProfileStore::Result::Busy && unchanged == held);
        }
    }

    // ---- Report text ------------------------------------------------------------------------------------------
    {
        Counts c; c.imported = 12; c.present = 1; c.full = 2;
        OK(CountsText(c) == L"12 importés · 1 déjà présent · 2 non importés (limite atteinte)");
        OK(FailureText(Failure::Busy, chromeDefault) == L"fermez Chrome, puis réessayez.");
        I18n::SetForProcess(I18n::Locale::En);
        OK(CountsText(c) == L"12 imported · 1 already there · 2 not imported (limit reached)");
        OK(FailureText(Failure::Busy, chromeDefault) == L"close Chrome, then try again." && Label(DiscoverChromium(roots, Browser::Chrome)[0]) == L"Chrome – main profile");
        I18n::SetForProcess(I18n::Locale::Fr);
    }

    RemoveTree(work);
    std::printf("browser import: %d checks passed\n", checks);
    return 0;
}
