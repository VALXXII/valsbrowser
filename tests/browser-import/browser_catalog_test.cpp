// Detected browsers with their logos (src/browser-catalog.h): synthetic registrations and synthetic « User Data »
// folders only — the real registry and the person's profiles are never read here. The only real files touched are
// read as icon RESOURCES: this test's own executable (no icon: fallback) and %SystemRoot%\explorer.exe (a logo).
// Usage: browser_catalog_test.exe <work folder>
#include "browser-catalog.h"
#include <cstdio>
#include <cstdlib>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #x); std::exit(1); } } while (0)
static int checks = 0;
#define OK(x) do { CHECK(x); ++checks; } while (0)
using namespace BrowserImport;
using I18n::Locale;

static void Dirs(const std::wstring& path) { std::wstring at; for (std::size_t i = 0; i <= path.size(); ++i) { if (i == path.size() || path[i] == L'\\') { if (at.size() > 3) CreateDirectoryW(at.c_str(), nullptr); } if (i < path.size()) at += path[i]; } }
static void Put(const std::wstring& path, const std::string& bytes) { Dirs(path.substr(0, path.find_last_of(L'\\'))); FILE* f = _wfopen(path.c_str(), L"wb"); CHECK(f); std::fwrite(bytes.data(), 1, bytes.size(), f); std::fclose(f); }
static const std::string kBookmarks = R"({"roots":{"bookmark_bar":{"type":"folder","name":"bar","children":[{"type":"url","name":"c","url":"https://comet.example/"}]}}})";

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) return 2;
    I18n::SetForProcess(Locale::Fr);
    const std::wstring work = std::wstring(argv[1]) + L"\\catalog-run";
    wchar_t selfPath[MAX_PATH * 2]{}; GetModuleFileNameW(nullptr, selfPath, MAX_PATH * 2); const std::wstring self = selfPath;

    // ---- Parsing the registration's strings -------------------------------------------------------------------
    OK(CommandExe(L"\"C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe\" --single-argument %1") == L"C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe");
    OK(CommandExe(L"C:\\Program Files\\Opera\\launcher.exe -- %1") == L"C:\\Program Files\\Opera\\launcher.exe");
    OK(CommandExe(L"  ") == L"" && CommandExe(L"\"unterminated") == L"");
    OK(Identify(L"C:\\x\\Google\\Chrome\\Application\\chrome.exe") == Browser::Chrome && Identify(L"C:\\x\\Chromium\\Application\\chrome.exe") == Browser::Chromium);
    OK(Identify(L"C:\\x\\Perplexity\\Comet\\Application\\Comet.exe") == Browser::Comet && Identify(L"C:\\x\\msedge.exe") == Browser::Edge && Identify(L"C:\\x\\brave.exe") == Browser::Brave && Identify(L"C:\\x\\FIREFOX.EXE") == Browser::Firefox);
    OK(!Identify(L"C:\\x\\vivaldi.exe") && !Identify(L"C:\\x\\opera.exe") && !Identify(L""));
    OK(IconSpec(L"C:\\a b\\app.exe,0") == std::make_pair(std::wstring(L"C:\\a b\\app.exe"), 0) && IconSpec(L"\"C:\\a\\app.exe\",-101") == std::make_pair(std::wstring(L"C:\\a\\app.exe"), -101));
    OK(IconSpec(L"C:\\a\\app.ico") == std::make_pair(std::wstring(L"C:\\a\\app.ico"), 0) && IconSpec(L"C:\\a,b\\app.exe").first == L"C:\\a,b\\app.exe");
    OK(CleanName(L"Opera\x202E GX\r\n") == L"Opera GX" && CleanName(std::wstring(200, L'x')).size() == 64);

    // ---- Where a logo may come from: local fixed drive, regular file, known type --------------------------------
    OK(!IconPathAllowed(L"\\\\server\\share\\browser.exe") && !IconPathAllowed(L"\\\\?\\C:\\Windows\\explorer.exe") && !IconPathAllowed(L"\\\\.\\C:\\x.exe"));
    OK(!IconPathAllowed(L"browser.exe") && !IconPathAllowed(L"C:browser.exe") && !IconPathAllowed(L"C:\\Windows\\..\\Windows\\explorer.exe") && !IconPathAllowed(L"C:/Windows/explorer.exe"));
    OK(!IconPathAllowed(work + L"\\missing.exe") && !IconPathAllowed(L"C:\\Windows\\win.ini") && !IconPathAllowed(L"C:\\Windows"));
    OK(IconPathAllowed(self));
    // Network and bad paths never reach the loader: null, i.e. the neutral glyph.
    OK(!LoadLogo(L"\\\\server\\share\\browser.exe", 0) && !LoadLogo(L"", 0) && !LoadLogo(work + L"\\missing.exe", 0));
    OK(!LoadLogo(self, 0));   // a real local executable without any icon resource: fallback
    {   // A real logo, read as a resource (data only): 32 x 32 premultiplied BGRA.
        wchar_t windows[MAX_PATH]{}; GetWindowsDirectoryW(windows, MAX_PATH);
        const auto logo = LoadLogo(std::wstring(windows) + L"\\explorer.exe", 0);
        OK(logo && logo->width == 32 && logo->height == 32 && logo->stride == 128 && logo->pixels.size() == 32u * 32u * 4u);
        bool premultiplied = true, visible = false;
        for (std::size_t i = 0; i < logo->pixels.size(); i += 4) { const unsigned a = logo->pixels[i + 3]; if (logo->pixels[i] > a || logo->pixels[i + 1] > a || logo->pixels[i + 2] > a) premultiplied = false; if (a) visible = true; }
        OK(premultiplied && visible);
        OK(!LoadLogo(std::wstring(windows) + L"\\explorer.exe", 9999));   // no such icon: fallback
    }

    // ---- Logo provenance (review 40dc694): every folder and the file held, no reparse point at any level ------------
    {
        wchar_t windows[MAX_PATH]{}; GetWindowsDirectoryW(windows, MAX_PATH);
        const std::wstring box = work + L"\\logo-box", real = box + L"\\real\\deep";
        Dirs(real); CHECK(CopyFileW((std::wstring(windows) + L"\\explorer.exe").c_str(), (real + L"\\logo.exe").c_str(), FALSE));
        CHECK(CopyFileW(self.c_str(), (box + L"\\plain.exe").c_str(), FALSE));
        // A minimal 16 x 16 32-bit .ico (one entry, opaque blue): the .ico path through the same checks.
        std::string ico; auto u16 = [&](unsigned v) { ico += char(v & 255); ico += char(v >> 8 & 255); }; auto u32 = [&](unsigned v) { u16(v & 0xFFFF); u16(v >> 16); };
        u16(0); u16(1); u16(1); ico += char(16); ico += char(16); ico += char(0); ico += char(0); u16(1); u16(32); u32(40 + 16 * 16 * 4 + 16 * 4); u32(22);
        u32(40); u32(16); u32(32); u16(1); u16(32); u32(0); u32(16 * 16 * 4 + 16 * 4); u32(0); u32(0); u32(0); u32(0);
        for (int i = 0; i < 16 * 16; ++i) { ico += char(255); ico += char(0); ico += char(0); ico += char(255); }
        ico += std::string(16 * 4, '\0');
        Put(real + L"\\logo.ico", ico);
        const DWORD handlesBefore = [] { DWORD n = 0; GetProcessHandleCount(GetCurrentProcess(), &n); return n; }();
        OK(IconPathAllowed(real + L"\\logo.exe") && LoadLogo(real + L"\\logo.exe", 0) && LoadLogo(real + L"\\logo.ico", 0));   // positive controls
        for (int i = 0; i < 20; ++i) { LoadLogo(real + L"\\logo.exe", 0); LoadLogo(real + L"\\logo.ico", 0); LoadLogo(box + L"\\plain.exe", 0); IconPathAllowed(box + L"\\missing.exe"); }
        DWORD handlesAfter = 0; GetProcessHandleCount(GetCurrentProcess(), &handlesAfter);
        OK(handlesAfter <= handlesBefore + 2);   // no handle left open by the checks or the loader
        // Junction one level above the file, then two levels above: refused before any loader call.
        auto junction = [](const std::wstring& link, const std::wstring& target) { const std::wstring c = L"cmd.exe /c mklink /J \"" + link + L"\" \"" + target + L"\" >nul"; return _wsystem(c.c_str()) == 0; };
        int loaderCalls = 0; LogoHeldForTest() = [&](const std::wstring&) { ++loaderCalls; };
        CHECK(junction(box + L"\\one", real) && junction(box + L"\\two", box + L"\\real"));
        OK(IsReparse(box + L"\\one") && !IconPathAllowed(box + L"\\one\\logo.exe") && !LoadLogo(box + L"\\one\\logo.exe", 0) && !LoadLogo(box + L"\\one\\logo.ico", 0));
        OK(IsReparse(box + L"\\two") && !IconPathAllowed(box + L"\\two\\deep\\logo.exe") && !LoadLogo(box + L"\\two\\deep\\logo.exe", 0));
        OK(loaderCalls == 0);   // the loader was never reached through a reparse point
        RemoveDirectoryW((box + L"\\one").c_str()); RemoveDirectoryW((box + L"\\two").c_str());
        // Deterministic swap between the checks and the loader: while held, neither the file nor any folder of its path
        // can be renamed, replaced or turned into a junction; the logo read is the checked one.
        bool swapped = true;
        LogoHeldForTest() = [&](const std::wstring& path) {
            ++loaderCalls;
            const bool file = MoveFileExW(path.c_str(), (path + L".old").c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
            const bool over = CopyFileW((box + L"\\plain.exe").c_str(), path.c_str(), FALSE) != 0;
            const bool folder = MoveFileExW(real.c_str(), (real + L"-moved").c_str(), 0) != 0;
            const bool ancestor = MoveFileExW((box + L"\\real").c_str(), (box + L"\\real-moved").c_str(), 0) != 0;
            swapped = file || over || folder || ancestor;
        };
        const auto held = LoadLogo(real + L"\\logo.exe", 0);
        OK(loaderCalls == 1 && !swapped && held && held->width == 32);
        LogoHeldForTest() = nullptr;
        // Control: once released, the same swap succeeds (the hold is what prevented it), and the replacement — an
        // executable without icon — gives the neutral glyph.
        OK(MoveFileExW((real + L"\\logo.exe").c_str(), (real + L"\\logo.exe.old").c_str(), 0) && CopyFileW((box + L"\\plain.exe").c_str(), (real + L"\\logo.exe").c_str(), FALSE));
        OK(!LoadLogo(real + L"\\logo.exe", 0));
        OK(!IconPathAllowed(L"C:\\logo.exe") && !IconPathAllowed(real + L"\\logo.exe:stream") && !IconPathAllowed(box + L"\\\\real\\deep\\logo.ico"));
    }

    // ---- Synthetic roots: Comet with two profiles in its PRIMARY folder (a different, stale secondary copy beside it),
    // Brave data without registration ------------------------------------------------------------------------------
    static const std::string kSecondary = R"({"roots":{"bookmark_bar":{"type":"folder","name":"bar","children":[{"type":"url","name":"s","url":"https://secondary.example/"}]}}})";
    const std::wstring local = work + L"\\Local", roaming = work + L"\\Roaming";
    Put(local + L"\\Comet\\User Data\\Default\\Bookmarks", kBookmarks);
    Put(local + L"\\Comet\\User Data\\Profile 2\\History", "not read here");
    Put(local + L"\\Comet\\User Data\\Profile 3\\Preferences", "{}");   // no data: not a source
    Put(local + L"\\Perplexity\\Comet\\User Data\\Default\\Bookmarks", kSecondary);   // secondary: not used while the primary has profiles
    Put(local + L"\\BraveSoftware\\Brave-Browser\\User Data\\Default\\Bookmarks", kBookmarks);
    const Roots roots{local, roaming};
    const auto found = Discover(roots);
    std::vector<Source> comet; for (const auto& s : found) if (s.browser == Browser::Comet) comet.push_back(s);
    OK(comet.size() == 2 && comet[0].profile == L"Default" && comet[0].isDefault && comet[1].profile == L"Profile 2" && !comet[0].soleProfile);
    OK(comet[0].root == local + L"\\Comet\\User Data" && comet[1].root == local + L"\\Comet\\User Data" && comet[0].bookmarks && !comet[1].bookmarks && comet[1].history);
    OK(found[0].browser == Browser::Comet && SourceKey(comet[0]) == "chromium:Comet/Default");   // the same key whichever folder it comes from
    {   // Primary alone: found there. Secondary alone: the fallback, its own pinned root.
        const std::wstring one = work + L"\\PrimaryOnly", two = work + L"\\SecondaryOnly";
        Put(one + L"\\Comet\\User Data\\Default\\Bookmarks", kBookmarks);
        Put(two + L"\\Perplexity\\Comet\\User Data\\Default\\Bookmarks", kSecondary);
        const auto v = DiscoverChromium(Roots{one, L""}, Browser::Comet), w = DiscoverChromium(Roots{two, L""}, Browser::Comet);
        OK(v.size() == 1 && v[0].root == one + L"\\Comet\\User Data" && v[0].soleProfile);
        OK(w.size() == 1 && w[0].root == two + L"\\Perplexity\\Comet\\User Data" && w[0].soleProfile && SourceKey(w[0]) == "chromium:Comet/Default");
        // Primary present but without any profile data: the secondary is used.
        const std::wstring three = work + L"\\EmptyPrimary";
        Put(three + L"\\Comet\\User Data\\Default\\Preferences", "{}");
        Put(three + L"\\Perplexity\\Comet\\User Data\\Default\\Bookmarks", kSecondary);
        const auto x = DiscoverChromium(Roots{three, L""}, Browser::Comet);
        OK(x.size() == 1 && x[0].root == three + L"\\Perplexity\\Comet\\User Data");
    }
    {   // Read as any Chromium profile: bookmarks bar → roots, nothing guessed.
        Options o; o.bookmarks = true; o.tempRoot = work + L"\\temp"; Dirs(o.tempRoot);
        const ReadResult r = Read(comet[0], o);
        OK(r.bookmarks.failure == Failure::None && r.bookmarks.roots.size() == 1 && r.bookmarks.roots[0].key == "bookmark_bar" && r.bookmarks.roots[0].children.size() == 1 && r.bookmarks.roots[0].children[0].url == L"https://comet.example/");   // the primary's content, not the secondary's
    }

    // ---- The rows -------------------------------------------------------------------------------------------------
    wchar_t windows[MAX_PATH]{}; GetWindowsDirectoryW(windows, MAX_PATH); const std::wstring explorer = std::wstring(windows) + L"\\explorer.exe";
    const std::vector<Registered> reg = {
        {L"Opera", L"Opera Stable", L"\"C:\\Program Files\\Opera\\opera.exe\" %1", L"C:\\Program Files\\Opera\\opera.exe,0"},
        {L"Comet", L"Comet", L"\"C:\\Users\\x\\AppData\\Local\\Perplexity\\Comet\\Application\\comet.exe\" --single-argument %1", explorer + L",0"},
        {L"Microsoft Edge", L"Microsoft Edge", L"\"C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe\" --single-argument %1", L"\\\\server\\icons\\edge.ico"},
        {L"Vivaldi", L"Vivaldi", L"\"C:\\Users\\x\\AppData\\Local\\Vivaldi\\Application\\vivaldi.exe\" -- \"%1\"", L""},
        {L"Vivaldi.ABC", L"vivaldi", L"\"D:\\Vivaldi\\vivaldi.exe\"", L""},                     // same name twice: one row
        {L"Self", L"Notre navigateur", L"\"" + self + L"\" %1", L""},                           // this browser: never a source
        {L"Broken", L"", L"", L""},                                                            // no command: ignored
        {L"Arc", L"Arc\x202E", L"\"C:\\Arc\\Arc.exe\"", L""},
    };
    const auto rows = Catalog(reg, found, self);
    std::vector<std::wstring> fr, en, detailFr, detailEn; std::vector<RowState> states;
    for (const auto& r : rows) { fr.push_back(RowLabelIn(Locale::Fr, r)); en.push_back(RowLabelIn(Locale::En, r)); detailFr.push_back(RowDetailIn(Locale::Fr, r)); detailEn.push_back(RowDetailIn(Locale::En, r)); states.push_back(r.state); }
    // Comet first (both profiles, installed: its logo reference), Edge installed without data, Brave data whose browser is
    // not registered (recoverable), then the browsers this build cannot read (sorted, disabled), then the HTML file.
    OK((fr == std::vector<std::wstring>{L"Comet — profil principal", L"Comet — Profile 2", L"Edge", L"Brave", L"Arc", L"Opera Stable", L"Vivaldi", L"Fichier de favoris HTML…"}));
    OK((en == std::vector<std::wstring>{L"Comet – main profile", L"Comet – Profile 2", L"Edge", L"Brave", L"Arc", L"Opera Stable", L"Vivaldi", L"HTML bookmarks file…"}));
    OK((states == std::vector<RowState>{RowState::Ready, RowState::Ready, RowState::NoData, RowState::Recoverable, RowState::Unsupported, RowState::Unsupported, RowState::Unsupported, RowState::Html}));
    OK(detailFr[0].empty() && detailFr[2] == L"Installé, aucun favori ni historique trouvé" && detailFr[3] == L"Navigateur introuvable : données récupérables" && detailFr[4] == L"Installé, pas encore pris en charge" && detailFr[7].empty());
    OK(detailEn[2] == L"Installed, no bookmarks or history found" && detailEn[3] == L"Browser not found: data can be recovered" && detailEn[6] == L"Installed, not supported yet");
    OK(rows[0].Available() && rows[3].Available() && rows[7].Available() && !rows[2].Available() && !rows[4].Available() && !rows[6].Available() && FirstAvailable(rows) == 0);
    OK(!rows[2].source.bookmarks && !rows[2].source.history && !rows[5].source.bookmarks && !rows[5].source.history);   // nothing to import from a disabled row
    // Logos: Comet's registered reference → a real logo; Edge's UNC reference and Brave (unregistered) → the glyph.
    OK(rows[0].icon == explorer && LoadLogo(rows[0].icon, rows[0].iconIndex) != nullptr);
    OK(rows[2].icon == L"\\\\server\\icons\\edge.ico" && !LoadLogo(rows[2].icon, rows[2].iconIndex) && rows[3].icon.empty());
    OK(rows[6].icon == L"C:\\Users\\x\\AppData\\Local\\Vivaldi\\Application\\vivaldi.exe" && !LoadLogo(rows[6].icon, 0));   // no DefaultIcon: its exe (absent here: glyph)
    {   // Nothing registered, nothing found: only the HTML file, selected.
        const auto none = Catalog({}, {}, self);
        OK(none.size() == 1 && none[0].state == RowState::Html && FirstAvailable(none) == 0);
        // Only unavailable browsers: the first available row is the HTML file.
        const auto off = Catalog({reg[0]}, {}, self);
        OK(off.size() == 2 && !off[0].Available() && FirstAvailable(off) == 1);
    }
    {   // The test seam's file: « key|name|command|icon » per line.
        Put(work + L"\\startmenuinternet.txt", "Comet|Comet|\"C:\\\\x\\\\comet.exe\" %1|C:\\\\x\\\\comet.exe,0\r\nbad line\nOpera|Opera|opera.exe|\n");
        const auto r = ReadRegisteredFile(work + L"\\startmenuinternet.txt");
        OK(r.size() == 2 && r[0].key == L"Comet" && r[0].command == L"\"C:\\\\x\\\\comet.exe\" %1" && r[0].icon == L"C:\\\\x\\\\comet.exe,0" && r[1].icon.empty());
        OK(ReadRegisteredFile(work + L"\\absent.txt").empty());
    }
    std::printf("browser catalog: %d checks passed\n", checks);
    return 0;
}
