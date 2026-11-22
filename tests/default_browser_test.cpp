// Browser as a selectable default browser (src/default-browser.h) and the link / file a launch opens
// (src/launch-args.h). The registration is written ONLY under a throwaway key HKCU\Software\BrowserTests\<guid>,
// read back, removed, and that key deleted: the person's real registration and default browser are never read
// for the checks nor written. -Wall -Wextra -Werror (run_all.py).
#include <windows.h>
#include <objbase.h>
#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>
#include "../src/default-browser.h"
#include "../src/launch-args.h"
#include "../src/taskbar-identity.h"

static int failures = 0, checks = 0;
static void Check(bool ok, const char* what) { ++checks; if (!ok) ++failures; std::printf("%s %s\n", ok ? "ok  " : "FAIL", what); }

namespace DB = DefaultBrowser;

static bool KeyExists(const std::wstring& key) { HKEY k = nullptr; if (RegOpenKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, KEY_READ, &k) != ERROR_SUCCESS) return false; RegCloseKey(k); return true; }
static std::wstring Read(const std::wstring& key, const std::wstring& name) { std::wstring v; return DB::ReadString(HKEY_CURRENT_USER, key, name, v) ? v : L"<absent>"; }
static bool ValueExists(const std::wstring& key, const std::wstring& name) { return RegGetValueW(HKEY_CURRENT_USER, key.c_str(), name.c_str(), RRF_RT_ANY, nullptr, nullptr, nullptr) == ERROR_SUCCESS; }

int main() {
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) { std::printf("FAIL COM\n"); return 1; }
    GUID g{}; CoCreateGuid(&g); wchar_t gs[64]{}; StringFromGUID2(g, gs, 64);
    const std::wstring base = std::wstring(L"Software\\BrowserTests\\") + gs;   // throwaway, deleted at the end
    const std::wstring root = L"C:\\Users\\Ana\\AppData\\Local\\browser";
    const DB::Texts texts{L"Navigateur web rapide et sobre.", L"Document HTML Browser", L"Document PDF Browser", L"Lien web Browser"};

    // ---- the layout (pure) ------------------------------------------------------------------------
    const DB::Layout l = DB::For(root, texts, base);
    std::map<std::wstring, std::wstring> v; for (const auto& x : l.values) v[x.key + L"|" + x.name] = x.data;
    const std::wstring client = base + L"\\Software\\Clients\\StartMenuInternet\\ValXXII.browser", caps = client + L"\\Capabilities";
    const std::wstring cls = base + L"\\Software\\Classes\\";
    Check(v[client + L"|"] == L"Browser" && v[caps + L"|ApplicationName"] == L"Browser", "StartMenuInternet\\ValXXII.browser named « Browser » (capital B)");
    Check(v[caps + L"|ApplicationDescription"] == texts.description && v[caps + L"|ApplicationIcon"] == root + L"\\browser.ico,0", "capabilities: description and the stable icon");
    Check(v[client + L"\\DefaultIcon|"] == root + L"\\browser.ico,0" && v[client + L"\\shell\\open\\command|"] == L"\"" + root + L"\\browser.exe\"", "client: icon + open command = the stable launcher");
    Check(v[caps + L"\\URLAssociations|http"] == L"ValXXII.BrowserURL" && v[caps + L"\\URLAssociations|https"] == L"ValXXII.BrowserURL", "http and https -> ValXXII.BrowserURL");
    bool files = true; for (const wchar_t* e : {L".htm", L".html", L".shtml", L".xhtml", L".svg"}) files = files && v[caps + L"\\FileAssociations|" + e] == L"ValXXII.BrowserHTML";
    Check(files && v[caps + L"\\FileAssociations|.pdf"] == L"ValXXII.BrowserPDF", ".htm .html .shtml .xhtml .svg -> BrowserHTML, .pdf -> BrowserPDF");
    Check(v[caps + L"\\StartMenu|StartMenuInternet"] == L"ValXXII.browser", "StartMenu -> the client key");
    Check(v[base + L"\\Software\\RegisteredApplications|ValXXII.browser"] == caps, "RegisteredApplications\\ValXXII.browser -> the capabilities path");
    const std::wstring open = L"\"" + root + L"\\browser.exe\" --single-argument \"%1\"";
    bool progs = true;
    for (const wchar_t* p : {L"ValXXII.BrowserHTML", L"ValXXII.BrowserURL", L"ValXXII.BrowserPDF"}) {
        const std::wstring k = cls + p;
        progs = progs && v[k + L"\\shell\\open\\command|"] == open && v[k + L"\\DefaultIcon|"] == root + L"\\browser.ico,0" &&
                v[k + L"|AppUserModelID"] == L"ValXXII.browser" && v[k + L"\\Application|ApplicationName"] == L"Browser" && v[k + L"\\Application|ApplicationIcon"] == root + L"\\browser.ico,0";
    }
    Check(progs, "ProgIDs: open with the stable launcher --single-argument \"%1\", the stable icon, Browser's identity");
    Check(v.count(cls + L"ValXXII.BrowserURL|URL Protocol") == 1 && v.count(cls + L"ValXXII.BrowserHTML|URL Protocol") == 0, "BrowserURL is a URL protocol handler, BrowserHTML is not");
    Check(v[cls + L"ValXXII.BrowserHTML|"] == texts.htmlType && v[cls + L"ValXXII.BrowserPDF|"] == texts.pdfType && v[cls + L"ValXXII.BrowserURL|"] == texts.urlType, "ProgID names from the catalog texts");
    Check(v.count(cls + L".html\\OpenWithProgids|ValXXII.BrowserHTML") == 1 && v.count(cls + L".pdf\\OpenWithProgids|ValXXII.BrowserPDF") == 1, "« Ouvrir avec » lists Browser for .html and .pdf");
    bool versionFree = true; for (const auto& x : l.values) versionFree = versionFree && x.data.find(L"\\versions\\") == std::wstring::npos;
    Check(versionFree, "nothing points into a versions\\<v> folder (updates never break the registration)");
    Check(DB::AppUserModelId == std::wstring(TaskbarIdentity::AppId), "same AppUserModelID as the taskbar identity");
    Check(DB::SettingsUri() == L"ms-settings:defaultapps?registeredAppUser=ValXXII.browser", "Default apps page opened on Browser's registered name");
    const DB::Layout prod = DB::For(root, texts);
    Check(prod.values.front().key == L"Software\\Clients\\StartMenuInternet\\ValXXII.browser", "production layout: plain HKCU\\Software keys (prefix empty)");
    Check(std::find_if(prod.values.begin(), prod.values.end(), [](const DB::Value& x) { return x.key == L"Software\\RegisteredApplications"; })->data ==
          L"Software\\Clients\\StartMenuInternet\\ValXXII.browser\\Capabilities", "production RegisteredApplications value");

    // ---- written, read back, owned, removed (throwaway key only) ----------------------------------------
    Check(!DB::Registered(HKEY_CURRENT_USER, base) && !DB::Owns(HKEY_CURRENT_USER, l, base), "nothing registered under the fresh test key");
    Check(DB::Write(HKEY_CURRENT_USER, l), "registration written under the test key");
    bool back = true; for (const auto& x : l.values) back = back && Read(x.key, x.name) == x.data;
    Check(back, "every value reads back exactly");
    Check(DB::Registered(HKEY_CURRENT_USER, base) && DB::Owns(HKEY_CURRENT_USER, l, base), "registered, and owned by this root");
    Check(!DB::Owns(HKEY_CURRENT_USER, DB::For(L"D:\\other\\browser", texts, base), base), "another root does not own it");
    Check(DB::Write(HKEY_CURRENT_USER, l), "writing again (each update) is idempotent");
    // Another app's value in a shared key survives our removal.
    HKEY shared = nullptr; RegCreateKeyExW(HKEY_CURRENT_USER, (cls + L".html\\OpenWithProgids").c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &shared, nullptr);
    RegSetValueExW(shared, L"OtherApp.HTML", 0, REG_SZ, reinterpret_cast<const BYTE*>(L""), sizeof(wchar_t)); RegCloseKey(shared);
    HKEY other = nullptr; RegCreateKeyExW(HKEY_CURRENT_USER, (base + L"\\Software\\RegisteredApplications").c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &other, nullptr);
    RegSetValueExW(other, L"OtherApp", 0, REG_SZ, reinterpret_cast<const BYTE*>(L"x"), 2 * sizeof(wchar_t)); RegCloseKey(other);
    Check(DB::Remove(HKEY_CURRENT_USER, l), "registration removed");
    bool gone = true; for (const auto& k : l.ownedKeys) gone = gone && !KeyExists(k);
    for (const auto& kv : l.sharedValues) gone = gone && !ValueExists(kv.first, kv.second);
    Check(gone && !DB::Registered(HKEY_CURRENT_USER, base), "every owned key and every value of ours is gone");
    Check(ValueExists(cls + L".html\\OpenWithProgids", L"OtherApp.HTML") && ValueExists(base + L"\\Software\\RegisteredApplications", L"OtherApp"), "other apps' values in shared keys are kept");
    Check(DB::Remove(HKEY_CURRENT_USER, l), "removing again (nothing there) is not an error");
    RegDeleteTreeW(HKEY_CURRENT_USER, base.c_str()); RegDeleteKeyW(HKEY_CURRENT_USER, base.c_str());
    Check(!KeyExists(base), "throwaway test key deleted");
    RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\BrowserTests");   // only when empty (another run may be using it)

    // ---- default status (the system query is faked: the real default is never read here) ------------
    auto fake = [](std::wstring http, std::wstring https) { return DB::Query([=](const wchar_t* p) { return std::wstring(p) == L"http" ? http : https; }); };
    Check(DB::StatusFrom(fake(L"ValXXII.BrowserURL", L"ValXXII.BrowserURL")) == DB::Status::Default, "http and https both ours: « Défini »");
    Check(DB::StatusFrom(fake(L"valxxii.browserurl", L"VALXXII.BROWSERURL")) == DB::Status::Default, "ProgID compared without case");
    Check(DB::StatusFrom(fake(L"ValXXII.BrowserURL", L"MSEdgeHTM")) == DB::Status::NotDefault, "only http ours: not the default");
    Check(DB::StatusFrom(fake(L"ChromeHTML", L"ChromeHTML")) == DB::Status::NotDefault && DB::StatusFrom(fake(L"", L"")) == DB::Status::NotDefault, "another browser, or unknown: not the default");
    Check(DB::StatusFrom(DB::Query{}) == DB::Status::NotDefault, "no query: not the default");

    // ---- the command line: switches vs the one link or file to open -------------------------------------
    using LaunchArgs::Parse;
    auto p = Parse({L"https://127.0.0.1:8080/page"});
    Check(p.target == L"https://127.0.0.1:8080/page" && p.switches.empty() && !p.single, "browser.exe <url>: the url");
    p = Parse({L"C:\\Users\\Ana\\My Pages\\a.html"});
    Check(p.target == L"C:\\Users\\Ana\\My Pages\\a.html", "browser.exe <file with spaces>: the path, whole (one argv token)");
    p = Parse({L"--profile", L"travail", L"https://x.y/"});
    Check(p.target == L"https://x.y/" && p.switches == L"--profile travail", "--profile <id> <url>: the id is a switch value, never the target");
    p = Parse({L"--private", L"--restore-window", L"abc123", L"--webview2-recovered"});
    Check(p.target.empty() && p.switches == L"--private --restore-window abc123 --webview2-recovered", "switches only: no target, switches kept in order");
    p = Parse({L"--single-argument", L"https://site/?q=--private --smoke-test"});
    Check(p.single && p.target == L"https://site/?q=--private --smoke-test" && p.switches.empty(), "--single-argument: the rest is the link, its « --private » is never a switch");
    p = Parse({L"--profile", L"travail", L"--single-argument", L"C:\\a b\\c.html"});
    Check(p.single && p.target == L"C:\\a b\\c.html" && p.switches == L"--profile travail", "switches before --single-argument are kept");
    p = Parse({L"--single-argument", L"https://a/", L"b"});
    Check(p.target == L"https://a/ b", "unquoted %1 split by Windows: joined back");
    p = Parse({L"--profile", L"--private"});
    Check(p.target.empty() && p.switches == L"--profile --private", "a value-less switch never swallows the next one");
    p = Parse({L"one.html", L"two.html"});
    Check(p.target == L"one.html", "two plain arguments: the first opens");
    Check(LaunchArgs::Quoted(L"C:\\a b") == L"\"C:\\a b\"" && LaunchArgs::Quoted(L"x") == L"x", "values with spaces quoted in the switches line");
    bool roundTrip = true;   // what the portable -> installed hand-off passes is read back whole by the installed copy
    for (const wchar_t* s : {L"C:\\a b\\c.html", L"C:\\dir with space\\", L"https://x.y/?a=\"b\" c", L"\\\\nas\\share\\a b.html", L"x"}) {
        int n = 0; LPWSTR* v = CommandLineToArgvW((L"browser.exe --single-argument " + LaunchArgs::Quoted(s)).c_str(), &n);
        roundTrip = roundTrip && v && n == 3 && std::wstring(v[2]) == s; if (v) LocalFree(v);
    }
    Check(roundTrip, "Quoted: CommandLineToArgvW reads every target back exactly (spaces, quotes, trailing backslash)");

    CoUninitialize();
    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
