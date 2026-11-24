// Onboarding import wiring (src/onboarding-import.h) on synthetic sources only: how the catalog's rows become
// the two selects of board 3, and what « Importer mes données » writes into a profile folder — through the same
// readers and transactions as Réglages › « Importer depuis un navigateur… ».
// Usage: onboarding_import_test.exe <synthetic sources made by tests/browser-import/make_sources.py> <work dir>
// No personal browser folder is listed or read; every address is under *.example.
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include "../../src/onboarding-import.h"

static int failures = 0, checks = 0;
static void Expect(bool ok, const char* what) { ++checks; std::printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++failures; }
namespace BI = BrowserImport; namespace OI = OnboardingImport; namespace fs = std::filesystem;

static std::string Slurp(const std::wstring& path) { std::ifstream in(fs::path(path), std::ios::binary); std::ostringstream s; s << in.rdbuf(); return s.str(); }
static std::size_t Count(const std::string& hay, const std::string& needle) { std::size_t n = 0; for (std::size_t at = hay.find(needle); at != std::string::npos; at = hay.find(needle, at + 1)) ++n; return n; }

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) { std::printf("usage: onboarding_import_test <sources> <work>\n"); return 2; }
    const std::wstring sources = argv[1], work = argv[2];
    I18n::SetForProcess(I18n::Locale::Fr);
    const BI::Roots roots{sources + L"\\Local", sources + L"\\Roaming"};
    // A second Chrome profile, as a person with two profiles has.
    { std::error_code ec; fs::create_directories(fs::path(roots.local) / L"Google/Chrome/User Data/Profile 1", ec); fs::copy_file(fs::path(roots.local) / L"Google/Chrome/User Data/Default/Bookmarks", fs::path(roots.local) / L"Google/Chrome/User Data/Profile 1/Bookmarks", fs::copy_options::overwrite_existing, ec); }
    const auto rows = BI::Catalog(BI::ReadRegisteredFile(sources + L"\\startmenuinternet.txt"), BI::Discover(roots), L"C:\\none\\browser.exe");
    int logos = 0;
    const OI::Listing list = OI::List(rows, [&](const BI::Row&) { ++logos; return std::shared_ptr<const NativeChrome::FaviconImage>(); });

    // ---- board 3: the two selects ----
    auto find = [&](const std::wstring& name) { for (std::size_t i = 0; i < list.shown.size(); ++i) if (list.shown[i].name == name) return static_cast<int>(i); return -1; };
    const int chrome = find(L"Google Chrome"), edge = find(L"Microsoft Edge"), firefox = find(L"Firefox");
    Expect(chrome >= 0 && edge >= 0 && firefox >= 0, "browsers are listed under the names people know (Google Chrome, Microsoft Edge, Firefox)");
    Expect(list.shown.size() == list.sources.size() && list.shown.size() == list.names.size(), "one source list and one name per shown browser");
    if (chrome < 0 || edge < 0) { std::printf("%d checks, %d failures\n", checks, failures + 1); return 1; }
    const auto& c = list.shown[static_cast<std::size_t>(chrome)];
    Expect(c.available && !c.file && c.profiles.size() == 2, "Chrome: one entry, its two profiles under it (the profile select appears)");
    Expect(c.profiles.size() == 2 && c.profiles[0].label == L"Profil principal" && c.profiles[1].label == L"Profil 1", "Chrome profiles: « Profil principal » (Default) and « Profil 1 » (Profile 1)");
    Expect(c.profiles.size() == 2 && c.profiles[0].bookmarks && c.profiles[0].history && c.profiles[1].bookmarks && !c.profiles[1].history, "each profile says what it holds (the second has no history)");
    Expect(list.shown[static_cast<std::size_t>(edge)].profiles.size() == 1, "Edge: a single profile (no profile select)");
    Expect(!list.shown.empty() && list.shown.back().file && list.shown.back().available && list.shown.back().name == L"Fichier de favoris HTML…", "the HTML file comes last");
    bool unavailable = false; for (const auto& s : list.shown) if (!s.available) unavailable = unavailable || (!s.detail.empty() && s.profiles.empty());
    Expect(unavailable, "browsers that cannot be imported are listed, greyed, with the reason");
    Expect(logos > 0, "logos are asked from the host (it reads them from the installed browser)");
    I18n::SetForProcess(I18n::Locale::En);
    const OI::Listing english = OI::List(rows, nullptr);
    Expect(english.shown[static_cast<std::size_t>(chrome)].profiles[0].label == L"Main profile" && english.shown[static_cast<std::size_t>(chrome)].profiles[1].label == L"Profile 1" && english.shown.back().name == L"HTML bookmarks file…", "the labels follow the language");
    I18n::SetForProcess(I18n::Locale::Fr);
    Expect(OI::Pick(list, chrome, 5) == nullptr && OI::Pick(list, 99, 0) == nullptr && OI::Pick(list, chrome, 1) != nullptr, "a pick outside the lists resolves to nothing");

    // ---- « Importer mes données »: read (worker's part) then commit (UI thread's part) into a profile folder ----
    const std::wstring profile = work + L"\\onboarding-import-profile"; { std::error_code ec; fs::remove_all(profile, ec); fs::create_directories(profile, ec); }
    const OI::Files files{profile, profile + L"\\bookmarks", profile + L"\\history", profile + L"\\bookmark-imports"};
    const BI::Source* source = OI::Pick(list, chrome, 0);
    const std::wstring name = list.names[static_cast<std::size_t>(chrome)];
    auto run = [&](bool bookmarks, bool history) {
        BI::Options o; o.bookmarks = bookmarks; o.history = history;
        const BI::ReadResult read = BI::Read(*source, o);
        return OI::Commit(files, *source, name, o, read, 1763500000000LL, [](const std::wstring&) { return false; }, nullptr);
    };
    {
        // Chrome is « open »: it holds History for writing, as the running browser does.
        const std::wstring held = source->dir + L"\\History";
        HANDLE h = CreateFileW(held.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        Expect(h != INVALID_HANDLE_VALUE, "fixture: the source History is held");
        const auto first = run(true, true);
        Expect(first.bookmarksSaved && !first.historySaved, "source in use: the bookmarks are saved, the history is not");
        Expect(first.detail == L"Google Chrome est ouvert. Fermez-le, puis réessayez.", "the reason is the board's sentence, with the browser's name");
        const std::string b = Slurp(files.bookmarks);
        Expect(Count(b, "home.example") >= 1 && Count(b, "docs.example") >= 1 && b.find("javascript:") == std::string::npos, "the bookmarks are in the profile file (script addresses left out)");
        Expect(!fs::exists(files.history) || Slurp(files.history).find("news.example") == std::string::npos, "no history was written");
        CloseHandle(h);
        // The bar's icons come with the bookmarks: Chrome's own (its Favicons is free), in the profile's favicons folder.
        const fs::path icons = fs::path(profile) / L"favicons";
        const std::string home = Slurp((icons / L"home.example.png").wstring()), docs = Slurp((icons / L"docs.example.png").wstring());
        Expect(home.rfind("\x89PNG\r\n\x1a\n", 0) == 0 && docs.rfind("\x89PNG\r\n\x1a\n", 0) == 0, "the source's icons of the imported sites are saved for the bar (home, docs)");
        Expect(!fs::exists(icons / L"three.example.png") && !fs::exists(icons / L"localhost.png"), "a site the source has no icon for gets none from it (the browser fetches it)");
    }
    {
        // The bookmarks bar boards 4 – 5.1 show in the browser mock: the source's own bar, in order.
        BI::Options o; o.bookmarks = true;
        const BI::ReadResult read = BI::Read(*source, o);
        const auto bar = OI::BarItems(read.bookmarks.roots);
        Expect(bar.size() == 4, "bar items: the four things on the source's bookmarks bar (nothing from « Other bookmarks »)");
        Expect(bar.size() == 4 && bar[0].title == L"Accueil — été ☀" && !bar[0].folder && bar[1].title == L"Travail 🛠 日本" && bar[1].folder && bar[2].title == L"Perso" && bar[2].folder && bar[3].title == L"Local" && !bar[3].folder, "bar items: titles, folders and links, in the source's order");
        const auto saved = OI::Commit(files, *source, name, o, read, 1763500000000LL, nullptr, nullptr);
        Expect(saved.bookmarksSaved && saved.bar.size() == 4 && saved.bar[1].folder, "a saved import hands the bar items to the window");
        std::vector<BI::Item> roots = read.bookmarks.roots;
        for (auto& r : roots) if (BI::RoleOf(r.key) == BI::RootRole::Bar) { BI::Item js; js.title = L"Script"; js.url = L"javascript:void(0)"; r.children.insert(r.children.begin(), js); BI::Item bare; bare.url = L"https://bare.example/"; r.children.push_back(bare); }
        const auto filtered = OI::BarItems(roots);
        Expect(filtered.size() == 5 && filtered[0].title == L"Accueil — été ☀" && filtered[4].title == L"https://bare.example/", "bar items: an address the browser does not keep is left out; a link without a title shows its address");
        Expect(OI::BarItems(roots, 2).size() == 2 && OI::BarItems({}).empty(), "bar items: bounded, and empty when the source has no bar");
        BI::Options history; history.history = true;
        Expect(OI::Commit(files, *source, name, history, BI::Read(*source, history), 1763500000000LL, nullptr, nullptr).bar.empty(), "no bookmarks asked: no bar items");
    }
    const std::string afterFirst = Slurp(files.bookmarks);
    {
        const auto retry = run(false, true);   // « Réessayer »: only what is missing
        Expect(!retry.bookmarksSaved && retry.historySaved && retry.detail.empty(), "retry once Chrome let go: the history is saved");
        const std::string h = Slurp(files.history);
        Expect(h.find("news.example") != std::string::npos && h.find("hidden.example") == std::string::npos && h.find("chrome://") == std::string::npos, "the history is in the profile file (hidden and internal pages left out)");
        Expect(Slurp(files.bookmarks) == afterFirst, "the retry did not touch the bookmarks");
    }
    {
        const std::wstring visited = (fs::path(profile) / L"favicons" / L"home.example.png").wstring();
        { std::ofstream out{fs::path(visited), std::ios::binary | std::ios::trunc}; out << "visited"; }   // the icon a visit left meanwhile
        const auto again = run(true, true);
        Expect(again.bookmarksSaved && again.historySaved, "importing the same source again succeeds");
        Expect(Count(Slurp(files.bookmarks), "home.example") == Count(afterFirst, "home.example"), "and adds no duplicate");
        Expect(Slurp(visited) == "visited", "an icon already in the profile is never replaced by the source's");
    }
    {
        const auto none = run(false, false);
        Expect(!none.bookmarksSaved && !none.historySaved && none.detail.empty(), "nothing asked: nothing saved, nothing reported");
        BI::Source gone = *source; gone.dir += L"-gone"; BI::Options o; o.bookmarks = true;
        const auto missing = OI::Commit(files, gone, name, o, BI::Read(gone, o), 1763500000000LL, nullptr, nullptr);
        Expect(!missing.bookmarksSaved && !missing.detail.empty() && iswupper(missing.detail[0]), "a source that disappeared: not saved, with a sentence saying why");
        I18n::SetForProcess(I18n::Locale::En);
        HANDLE h = CreateFileW((source->dir + L"\\History").c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        const auto busy = run(false, true); CloseHandle(h);
        Expect(busy.detail == L"Google Chrome is open. Close it, then try again.", "the reason in English");
        I18n::SetForProcess(I18n::Locale::Fr);
    }
    {
        int lines = 0; std::wstring all;
        BI::Options o; o.bookmarks = true; o.history = true;
        OI::Commit(files, *source, name, o, BI::Read(*source, o), 1763500000000LL, nullptr, [&](const std::wstring& l) { ++lines; all += l + L"\n"; });
        Expect(lines == 2 && all.find(L"example") == std::wstring::npos && all.find(L"imported=") != std::wstring::npos, "log lines carry counts only: no address, no title");
    }
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
