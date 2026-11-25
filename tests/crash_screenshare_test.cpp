// #121 « Onglet planté · A » and « Partage d'écran, confirmation · A » (Paper p-V-0): the crashed-tab page
// (src/crash-page.h) and the screen-share confirmation popover (src/origin-prompt.h Kind::ScreenCapture), as pure
// functions: no window, no WebView2. Built and run by tests/update-integration/run_all.py.
//   crash_screenshare_test.exe <repo root>
// The engine side (ProcessFailed kinds, history, Reload/GoBack, ScreenCaptureStarting Cancel -> NotAllowedError, the
// NavigateToString size limit) is measured on the real engine with no window shown by tests/crash-screenshare/engine_probe.py.
#include "../src/crash-page.h"
#include "../src/origin-prompt.h"
#include "../src/screen-share-blocks.h"
#include "../src/settings.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>

using namespace NativeChrome;
static int checks = 0, failures = 0;
static void Check(bool ok, const char* what) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", what); } }
static bool Has(const std::wstring& s, const std::wstring& part) { return s.find(part) != std::wstring::npos; }
static std::wstring Msg(const std::wstring& token, const std::wstring& action) { return L"{\"token\":\"" + token + L"\",\"action\":\"" + action + L"\"}"; }
static std::wstring DataUri(const std::wstring& path) {
    std::ifstream in(path.c_str(), std::ios::binary); if (!in) return L"";
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    static const char* digits = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::wstring r = L"data:font/woff2;base64,";
    for (size_t i = 0; i < bytes.size(); i += 3) {
        const unsigned n = ((unsigned char)bytes[i] << 16) | (i + 1 < bytes.size() ? (unsigned char)bytes[i + 1] << 8 : 0) | (i + 2 < bytes.size() ? (unsigned char)bytes[i + 2] : 0);
        r += (wchar_t)digits[(n >> 18) & 63]; r += (wchar_t)digits[(n >> 12) & 63]; r += i + 1 < bytes.size() ? (wchar_t)digits[(n >> 6) & 63] : L'='; r += i + 2 < bytes.size() ? (wchar_t)digits[n & 63] : L'=';
    }
    return r;
}

int wmain(int argc, wchar_t** argv) {
    const std::wstring root = argc > 1 ? argv[1] : L".";
    // ---- crashed tab page ---------------------------------------------------------------------------------------
    const std::wstring token = L"{5E1C6A3F-0000-4000-8000-123456789ABC}";
    const std::wstring h = CrashPage::Html(L"https://www.lemonde.fr/planete/", token);
    Check(Has(h, L"<title>lemonde.fr</title>") && Has(h, L"<h1>Cette page a cessé de fonctionner</h1>"), "crash page: Paper title; the tab keeps the site name (1LM8-0)");
    Check(Has(h, L"<p><b>lemonde.fr</b> s&#39;est arrêté d&#39;un coup. Rechargez pour la rouvrir ; si ça recommence, fermer quelques onglets libère de la mémoire.</p>"), "crash page: Paper text, host without www. in Medium");
    Check(Has(h, L"class=\"primary\" data-action=\"reload\" autofocus>Recharger</button>") && Has(h, L"class=\"danger\" data-action=\"close\">Fermer l&#39;onglet</button>"), "crash page: « Recharger » first (focus par défaut), « Fermer l'onglet » destructive");
    Check(h.find(L"data-action=\"reload\"") < h.find(L"data-action=\"close\""), "crash page: button order as the board");
    // Paper 1LK7-0 / 1MAZ-0 / 1LKB-0 / 1LKE-0 / 1LKC-0 values.
    Check(Has(h, L"max-width:420px") && Has(h, L"gap:16px;text-align:center") && Has(h, L".text{display:flex;flex-direction:column;align-items:center;gap:8px}"), "crash page: 420 block, 16 between icon / text / buttons, 8 between title and text");
    Check(Has(h, L"h1{margin:0;font-size:20px;line-height:28px;font-weight:600;color:#FFFFFFE6;text-wrap:balance}"), "crash page: title 20/28 SemiBold #FFFFFFE6 balanced");
    Check(Has(h, L"p{margin:0;font-size:14px;line-height:20px;font-weight:400;color:#FFFFFF99;text-wrap:balance}") && Has(h, L"p b{font-weight:500;color:#FFFFFFE6}"), "crash page: text 14/20 #FFFFFF99, host Medium #FFFFFFE6");
    Check(Has(h, L"button{height:32px;padding:0 14px;border:0;border-radius:8px;font:inherit;font-size:14px;line-height:32px;font-weight:500"), "crash page: buttons h 32, padding 14, radius 8, 14/32 Medium");
    Check(Has(h, L".primary{background:#FFFFFFE6;color:#171717}") && Has(h, L".danger{background:rgba(255,123,110,.1);color:#FF7B6E}"), "crash page: primary #FFFFFFE6 on #171717, destructive #FF7B6E on 10 %");
    Check(Has(h, L".primary:hover{background:#FFFFFF}.primary:active{background:#FFFFFFBF}"), "crash page states: Recharger hover #FFFFFF, pressed #FFFFFFBF (Paper 1O2K-0 / 1O2N-0)");
    Check(Has(h, L".danger:hover{background:rgba(255,123,110,.16)}.danger:active{background:rgba(255,123,110,.22)}") && !Has(h, L"#FF9489"), "crash page states: Fermer l'onglet 16 % / 22 %, text unchanged (Paper 1O2Y-0 / 1O31-0)");
    Check(Has(h, L"button:focus-visible{outline:none;box-shadow:inset 0 0 0 1px #FFFFFF80}") && !Has(h, L"transform:scale") && !Has(h, L"transform: scale"),"crash page states: A1 inset focus ring, no press scale (owner decision)");
    Check(Has(h, L"background:#171717") && Has(h, L"\"cv01\",\"cv02\",\"cv03\",\"cv04\",\"cv13\""), "crash page: canvas #171717, Inter Display features");
    Check(Has(h, L"width=\"48\" height=\"48\" viewBox=\"0 0 24 24\"") && Has(h, L"stop-color=\"#FF7B6E\" stop-opacity=\"0.85\"") && Has(h, L"stop-color=\"#E0483A\" stop-opacity=\"0.75\"") && Has(h, CrashPage::Icon), "crash page: the warning icon (paid Jiggicons, glass; a free substitute in the public copy), danger tint, 48");
    Check(Has(h, L"default-src 'none'") && Has(h, L"font-src data:") && !Has(h, L"src=\"http") && !Has(h, L"url(http") && !Has(h, L"https://www.lemonde"), "crash page: strict CSP, no network, the address itself never in the page");
    Check(Has(h, L"const T=\"" + token + L"\""), "crash page: token in the script");
    // A hostile address cannot inject markup or script.
    const std::wstring evil = CrashPage::Html(L"https://a<script>alert(1)</script>.example/\"x", token);
    Check(!Has(evil, L"<script>alert") && Has(evil, L"&lt;script&gt;"), "crash page: host escaped");
    // Messages: exactly this page's token and an allow-listed action.
    Check(CrashPage::ParseMessage(Msg(token, L"reload"), token) == CrashPage::Action::Reload, "crash page: reload message");
    Check(CrashPage::ParseMessage(Msg(token, L"close"), token) == CrashPage::Action::CloseTab, "crash page: close message");
    Check(CrashPage::ParseMessage(L"\"" + std::wstring(L"{\\\"token\\\":\\\"") + token + L"\\\",\\\"action\\\":\\\"reload\\\"}\"", token) == CrashPage::Action::Reload, "crash page: JSON-string form (get_WebMessageAsJson)");
    Check(!CrashPage::ParseMessage(Msg(L"{other}", L"reload"), token), "crash page: wrong token refused");
    Check(!CrashPage::ParseMessage(Msg(token, L"retry"), token) && !CrashPage::ParseMessage(Msg(token, L"back"), token), "crash page: error-page actions refused");
    Check(!CrashPage::ParseMessage(L"{\"token\":\"" + token + L"\",\"action\":\"reload\",\"x\":\"1\"}", token), "crash page: extra member refused");
    Check(!CrashPage::ParseMessage(Msg(L"", L"reload"), L""), "crash page: empty token never matches");
    // Which engine failures get the page.
    Check(CrashPage::Shows(COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED), "crash page: the tab's renderer exited");
    for (int k : {COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED, COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_UNRESPONSIVE, COREWEBVIEW2_PROCESS_FAILED_KIND_FRAME_RENDER_PROCESS_EXITED,
                  COREWEBVIEW2_PROCESS_FAILED_KIND_UTILITY_PROCESS_EXITED, COREWEBVIEW2_PROCESS_FAILED_KIND_SANDBOX_HELPER_PROCESS_EXITED, COREWEBVIEW2_PROCESS_FAILED_KIND_GPU_PROCESS_EXITED,
                  COREWEBVIEW2_PROCESS_FAILED_KIND_PPAPI_PLUGIN_PROCESS_EXITED, COREWEBVIEW2_PROCESS_FAILED_KIND_PPAPI_BROKER_PROCESS_EXITED, COREWEBVIEW2_PROCESS_FAILED_KIND_UNKNOWN_PROCESS_EXITED})
        Check(!CrashPage::Shows(k), "crash page: not for unresponsive, frame, browser or helper processes");
    Check(CrashPage::PageKind > static_cast<int>(ErrorPages::Kind::Certificate), "crash page: its kind never collides with an error page's");
    // With the real fonts the page stays under what NavigateToString accepts (measured: 1 571 998 characters).
    CrashPage::Extras x;
    x.fontRegular = DataUri(root + L"\\assets\\fonts\\InterDisplay-Regular.woff2"); x.fontMedium = DataUri(root + L"\\assets\\fonts\\InterDisplay-Medium.woff2"); x.fontSemiBold = DataUri(root + L"\\assets\\fonts\\InterDisplay-SemiBold.woff2");
    Check(!x.fontRegular.empty() && !x.fontMedium.empty() && !x.fontSemiBold.empty(), "crash page: the three WOFF2 weights ship in assets/fonts");
    const std::wstring full = CrashPage::Html(L"https://www.lemonde.fr/planete/", token, x);
    Check(full.size() < 1000000 && Has(full, L"format(\"woff2\");font-weight:600"), "crash page: three weights, well under NavigateToString's limit");
    std::printf("crash page with fonts: %zu characters\n", full.size());
    // Back from our page: the entry two places back, read from DevTools history (strings may hold anything).
    const std::wstring history = LR"({"currentIndex":2,"entries":[{"id":5,"title":"A \"id\":99","transitionType":"typed","url":"http://a/","userTypedURL":""},{"id":6,"title":"B [1,2]","transitionType":"typed","url":"http://b/?x={\"id\":7}","userTypedURL":""},{"id":8,"title":"ours","transitionType":"link","url":"data:text/html;base64,AAAA","userTypedURL":""}]})";
    Check(CrashPage::HistoryEntryBefore(history, 2) == 5 && CrashPage::HistoryEntryBefore(history, 1) == 6 && CrashPage::HistoryEntryBefore(history, 0) == 8, "crash page: history entries by position, ids inside strings ignored");
    Check(CrashPage::HistoryEntryBefore(history, 3) == -1 && CrashPage::HistoryEntryBefore(L"{}", 2) == -1 && CrashPage::HistoryEntryBefore(L"not json", 1) == -1, "crash page: no such entry -> -1");
    // Renderer exits (night 24/11: a background tab already on our page crashed again and came back as the SITE).
    using CrashPage::OnExit; using CrashPage::OnNavigation;
    Check(CrashPage::ExitResponse(false, false, false) == OnExit::ShowPage, "exit: a site's renderer died -> our page");
    Check(CrashPage::ExitResponse(false, true, false) == OnExit::ReshowPage, "exit: OUR page's renderer died -> our page again, never the site behind it");
    Check(CrashPage::ExitResponse(false, true, true) == OnExit::ReshowPage && CrashPage::ExitResponse(false, false, true) == OnExit::ReloadDocument, "exit: home / sandbox reload their own document");
    Check(CrashPage::ExitResponse(true, true, false) == OnExit::ReloadAfterHang && CrashPage::ExitResponse(true, false, false) == OnExit::ReloadAfterHang, "exit: the frozen page's « Recharger » reloads the page");
    // Navigations while our page is on screen. The page shown again reloads its own entry (pending): it must pass.
    const int reload = COREWEBVIEW2_NAVIGATION_KIND_RELOAD, history_ = COREWEBVIEW2_NAVIGATION_KIND_BACK_OR_FORWARD, fresh = COREWEBVIEW2_NAVIGATION_KIND_NEW_DOCUMENT;
    Check(CrashPage::NavigationWhileShown(true, true, reload, false, false, false) == OnNavigation::Allow, "navigation: our page shown again after its own crash is let through (not sent back to the site)");
    Check(CrashPage::NavigationWhileShown(false, true, reload, false, false, false) == OnNavigation::ToCrashedEntry, "navigation: F5 / Actualiser on our page -> back to the crashed entry");
    Check(CrashPage::NavigationWhileShown(false, true, history_, false, false, false) == OnNavigation::ToEntryBefore, "navigation: Back from our page -> the entry before the crashed one");
    Check(CrashPage::NavigationWhileShown(false, true, history_, true, false, false) == OnNavigation::Allow && CrashPage::NavigationWhileShown(false, false, history_, false, true, false) == OnNavigation::Allow, "navigation: « Recharger » and the resolved Back pass");
    Check(CrashPage::NavigationWhileShown(false, true, history_, false, false, true) == OnNavigation::Allow, "navigation: over one of our own pages, Back is the engine's");
    Check(CrashPage::NavigationWhileShown(false, false, fresh, false, false, false) == OnNavigation::Allow && CrashPage::NavigationWhileShown(false, false, reload, false, false, false) == OnNavigation::Allow, "navigation: a typed address or a site's reload passes");
    // Frozen page: the engine reports a freeze once (20:00 run 24/11: after « Attendre » the window never came back).
    using CrashPage::HangNext;
    Check(CrashPage::AfterWait(false, true, false, true, true, false) == HangNext::Ask && CrashPage::HangAskAgainMs == 15000, "frozen: « Attendre », still frozen 15 s later, on screen: the window comes back");
    Check(CrashPage::AfterWait(false, true, false, true, false, false) == HangNext::Later, "frozen: still frozen in a hidden tab: asked once it is shown");
    Check(CrashPage::AfterWait(false, false, false, true, true, false) == HangNext::Stop && CrashPage::AfterWait(true, true, false, true, true, false) == HangNext::Stop,
          "frozen: the page answered again, or the tab closed: nothing more");
    Check(CrashPage::AfterWait(false, true, true, true, true, false) == HangNext::Stop && CrashPage::AfterWait(false, true, false, false, true, false) == HangNext::Stop
          && CrashPage::AfterWait(false, true, false, true, true, true) == HangNext::Stop, "frozen: reloading / crashed, another document, or the window already open: no second window");
    // English process.
    I18n::LocaleSlot() = static_cast<int>(I18n::Locale::En);
    const std::wstring en = CrashPage::Html(L"https://www.lemonde.fr/planete/", token);
    Check(Has(en, L"<html lang=\"en\"") && Has(en, L"<h1>This page stopped working</h1>") && Has(en, L">Reload</button>") && Has(en, L">Close tab</button>"), "crash page: English");
    Check(Has(en, L"<b>lemonde.fr</b> stopped suddenly. Reload to open it again; if it keeps happening, closing a few tabs frees up memory."), "crash page: English text of the board (1O6H-0)");
    I18n::LocaleSlot() = static_cast<int>(I18n::Locale::Fr);

    // ---- screen-share confirmation (Paper 1LMF-0) ----------------------------------------------------------------
    OriginPrompt::Spec s; s.kind = OriginPrompt::Kind::ScreenCapture; s.origin = L"meet.google.com";
    const auto m = OriginPrompt::Build(s, true);
    Check(m.scope == Modal::Scope::Anchor && m.width == 340 && m.closeButton && m.padding == 17 && m.gap == 0, "share: 340 popover under the origin indicator, padding 16 inside the 1 px border, gap 16, close button");
    Check(m.title == L"meet.google.com veut voir votre écran" && m.subtitle.empty() && !m.headGlyph, "share: Paper title, no subtitle, no head glyph");
    Check(m.rows.size() == 1 && m.rows[0].glyph == DialogKit::Glyph::monitor && m.rows[0].label == L"Partager votre écran" && m.rows[0].toggle == -1, "share: one request row, monitor glyph");
    Check(m.rows[0].note == L"Vous choisirez ensuite un onglet, une fenêtre ou l’écran entier.", "share: the row's note (typographic apostrophe as the board)");
    Check(m.checkLabel == L"Toujours bloquer ce site" && m.originCheck && !m.checkValue && m.fields.empty() && m.notices.empty() && !m.demo, "share: « Toujours bloquer ce site » unchecked, in the popovers' lane (Paper 1O60-0)");
    Check(m.buttons.size() == 2 && m.buttons[0].id == OriginPrompt::Id::Cancel && m.buttons[0].label == L"Bloquer" && m.buttons[0].role == Modal::Role::Secondary
          && m.buttons[1].id == OriginPrompt::Id::Accept && m.buttons[1].label == L"Choisir…" && m.buttons[1].role == Modal::Role::Primary && m.buttons[1].isDefault, "share: Bloquer / Choisir… (default)");
    Check(m.stretchButtons && m.softButtons, "share: stretched buttons as B01");
    Check(m.armMs == 500, "share: SEC-22 armed 500 ms");
    Check(std::string(DialogKit::Glyph::monitor).rfind("M208,36H48A28,28,0,0,0,20,64V176", 0) == 0 && std::string(DialogKit::Glyph::monitor).find("Zm-40,52a12,12,0,0,1-12,12H96") != std::string::npos, "share: monitor glyph = Paper 1LPL-0");
    auto map = [&](int button) { Modal::Result r; r.button = button; r.checked = true; return OriginPrompt::Map(s, r); };
    Check(map(OriginPrompt::Id::Accept).action == OriginPrompt::Action::Choose, "share: Choisir… = the engine's picker");
    Check(map(OriginPrompt::Id::Cancel).action == OriginPrompt::Action::Block, "share: Bloquer = refused");
    Check(map(-1).action == OriginPrompt::Action::Dismiss && !map(-1).remember && !map(OriginPrompt::Id::Accept).remember, "share: close / Échap = Dismiss; « Choisir… » is never remembered");
    Check(map(OriginPrompt::Id::Cancel).remember, "share: « Bloquer » with the box checked is remembered");
    { Modal::Result r; r.button = OriginPrompt::Id::Cancel; r.checked = false; Check(!OriginPrompt::Map(s, r).remember, "share: « Bloquer » with the box clear asks again next time"); }
    I18n::LocaleSlot() = static_cast<int>(I18n::Locale::En);
    const auto me = OriginPrompt::Build(s, true);
    Check(me.title == L"meet.google.com wants to see your screen" && me.buttons[0].label == L"Block" && me.buttons[1].label == L"Choose…" && me.rows[0].label == L"Share your screen", "share: English");
    Check(me.rows[0].note == L"Next, you’ll pick a tab, a window or your entire screen." && me.checkLabel == L"Always block this site", "share: English note and box (board 1O6W-0 / 1O6Z-0)");
    I18n::LocaleSlot() = static_cast<int>(I18n::Locale::Fr);

    // ---- frozen page (Paper 1O1M-0, veil 1NZI-0) ---------------------------------------------------------------------
    OriginPrompt::Spec u; u.kind = OriginPrompt::Kind::Unresponsive; u.origin = L"lemonde.fr";
    const auto f = OriginPrompt::Build(u, true);
    Check(f.scope == Modal::Scope::Page && f.centerInOwner && f.width == 400 && f.padding == 21 && f.gap == 0 && !f.closeButton, "frozen: 400 window centred in the browser window, padding 20 inside the 1 px border, gap 16");
    Check(f.scrimAlpha == 0x80 / 255.f && f.scrimBottomRadius == 6, "frozen: the page veiled black 50 %, its bottom corners rounded 6");
    Check(f.hero.size() == 3 && f.heroSize == 32 && f.heroView == 24 && f.hero[0].d == OriginPrompt::Hourglass::body && f.hero[0].top == 0x3A3A3D && f.hero[0].bottom == 0x3A3A3D
          && f.hero[1].top == 0xB5B5BA && f.hero[1].bottom == 0x7A7A80 && f.hero[1].y1 == 2 && f.hero[1].y2 == 22 && f.hero[2].d == OriginPrompt::Hourglass::caps, "frozen: Jiggicons hourglass, glass, grey (Paper 1O1O-0)");
    {   // The paths themselves are the paid icon's (tools/public-mirror/paid-icons: never checked here, the public copy has others).
        bool paths = true; for (const char* d : {OriginPrompt::Hourglass::body, OriginPrompt::Hourglass::sand, OriginPrompt::Hourglass::caps}) { const std::string p = d; paths = paths && p.size() >= 5 && p[0] == 'M' && p.back() == 'Z'; }
        Check(paths, "frozen: hourglass layers are closed path data"); }
    Check(f.title == L"lemonde.fr ne répond plus" && f.body == L"La page est bloquée. Vous pouvez attendre qu’elle reprenne, ou la recharger." && f.subtitle.empty(), "frozen: Paper title and text");
    Check(f.buttons.size() == 2 && f.buttons[0].id == OriginPrompt::Id::Cancel && f.buttons[0].label == L"Attendre" && f.buttons[0].role == Modal::Role::Secondary && f.buttons[0].isCancel
          && f.buttons[1].id == OriginPrompt::Id::Accept && f.buttons[1].label == L"Recharger" && f.buttons[1].role == Modal::Role::Primary && f.buttons[1].isDefault
          && !f.stretchButtons && !f.softButtons && f.armMs == 500, "frozen: Attendre (Échap) / Recharger (Entrée, armed), right-aligned A1 buttons");
    auto mapU = [&](int button) { Modal::Result r; r.button = button; return OriginPrompt::Map(u, r); };
    Check(mapU(OriginPrompt::Id::Accept).action == OriginPrompt::Action::Reload && mapU(OriginPrompt::Id::Cancel).action == OriginPrompt::Action::Wait && mapU(-1).action == OriginPrompt::Action::Dismiss, "frozen: Recharger / Attendre / closed by the host");
    I18n::LocaleSlot() = static_cast<int>(I18n::Locale::En);
    const auto fe = OriginPrompt::Build(u, true);
    Check(fe.title == L"lemonde.fr isn’t responding" && fe.body == L"The page is stuck. You can wait for it to recover, or reload it." && fe.buttons[0].label == L"Wait" && fe.buttons[1].label == L"Reload", "frozen: English (board 1O6N-0 / 1O6Q-0)");
    I18n::LocaleSlot() = static_cast<int>(I18n::Locale::Fr);

    // ---- « Toujours bloquer ce site »: Browser's own list (src/screen-share-blocks.h) ----------------------------------
    namespace SB = ScreenShareBlocks;
    Check(SB::Normalize(L"HTTPS://Meet.Google.com/") == L"https://meet.google.com" && SB::Normalize(L"http://localhost:5207") == L"http://localhost:5207", "blocks: origins normalised");
    Check(SB::Normalize(L"file:///C:/x").empty() && SB::Normalize(L"https://a b").empty() && SB::Normalize(L"https://x/path").empty() && SB::Normalize(L"meet.google.com").empty(), "blocks: only http(s) origins");
    std::vector<std::wstring> list;
    Check(SB::Set(list, L"https://meet.google.com/", true) && !SB::Set(list, L"https://MEET.google.com", true) && SB::Set(list, L"https://a.example", true) && list.size() == 2, "blocks: added once");
    Check(SB::Contains(list, L"https://meet.google.com") && !SB::Contains(list, L"https://other.example"), "blocks: lookup");
    const std::string file = SB::Serialize(list);
    Check(file == "browser-screen-share-blocked 1\nhttps://a.example\nhttps://meet.google.com\n" && SB::Parse(file) == list, "blocks: file round trip");
    Check(SB::Parse("something else\nhttps://meet.google.com\n").empty() && SB::Parse("browser-screen-share-blocked 1\r\nhttps://x.example\r\njunk\r\nhttps://x.example\r\n") == std::vector<std::wstring>{L"https://x.example"}, "blocks: header required, junk and duplicates dropped");
    Check(SB::Set(list, L"https://meet.google.com", false) && !SB::Contains(list, L"https://meet.google.com") && !SB::Set(list, L"https://meet.google.com", false), "blocks: removed");
    Check(BrowserSettings::PermissionName(BrowserSettings::Permission::ScreenShare) == std::wstring(L"Partage d'écran") && BrowserSettings::PermissionValue(BrowserSettings::Permission::ScreenShare, BrowserSettings::PermissionState::Block) == L"Bloqué", "blocks: « Partage d'écran · Bloqué » in Réglages › Autorisations");
    {   // Réglages › Autorisations des sites (main.cpp LoadSitePermissions): the engine's sites with the list merged in.
        using namespace BrowserSettings;
        const SitePermission share{Permission::ScreenShare, PermissionState::Block};
        std::vector<Site> sites; Site cam; cam.origin = L"http://127.0.0.1:5207/"; cam.permissions.push_back({Permission::Camera, PermissionState::Allow}); sites.push_back(cam);
        Site other; other.origin = L"https://zeta.example"; other.permissions.push_back({Permission::Notifications, PermissionState::Block}); sites.push_back(other);
        std::vector<std::wstring> blocked; blocked.push_back(L"http://127.0.0.1:5207"); blocked.push_back(L"https://meet.google.com");
        SB::MergeInto(sites, blocked, share);
        Check(sites.size() == 3 && sites[0].origin == L"http://127.0.0.1:5207/" && sites[0].permissions.size() == 2 && sites[0].permissions[1].kind == Permission::ScreenShare
              && sites[0].permissions[1].state == PermissionState::Block, "blocks: a blocked site the engine also lists gets the « Partage d'écran · Bloqué » row (engine spelling with /)");
        Check(sites[1].origin == L"https://meet.google.com" && sites[1].permissions.size() == 1 && sites[1].permissions[0].kind == Permission::ScreenShare && sites[2].origin == L"https://zeta.example",
              "blocks: a blocked site the engine does not list is shown on its own, the list sorted by origin");
        Check(SiteSummary(sites[1]) == L"Partage d'écran bloqué", "blocks: the sites list's second line « Partage d'écran bloqué »");
        std::vector<Site> none; std::vector<std::wstring> only; only.push_back(L"http://127.0.0.1:5207"); SB::MergeInto(none, only, share);
        Check(none.size() == 1 && none[0].origin == L"http://127.0.0.1:5207" && none[0].permissions.size() == 1, "blocks: no engine permission at all: the site still listed (the night QA's case)");
    }

    std::printf("crash_screenshare_test: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
