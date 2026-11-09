// Lot B: sandbox bridge (allowlist, token, malformed / oversized JSON, ResultScript escaping,
// catalogue completeness) and a native harness for the B components (find bar, context toast,
// tooltip, full-screen hint, shortcuts panel): real windows on an off-screen test owner, real
// hooks, shadowless styles, callbacks exactly once, Échap / Entrée paths, Paper geometry.
// Built and run by test-sandbox.ps1 (no WebView2, no browser).
#include "../src/debug-sandbox.h"
#include "../src/find-bar.h"
#include "../src/context-toast.h"
#include "../src/fullscreen-hint.h"
#include "../src/shortcuts-panel.h"
#include <cstdio>
#include <cstdlib>
#include <set>

using namespace NativeChrome;
static int checks = 0;
static void Require(bool ok, const char* what) { ++checks; if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); std::exit(1); } }
static bool Has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }
static size_t Count(const std::string& s, const std::string& part) { size_t n = 0; for (size_t i = s.find(part); i != std::string::npos; i = s.find(part, i + 1)) ++n; return n; }
static std::wstring Cmd(const std::wstring& token, const std::wstring& name, const std::wstring& variant) { return L"{\"token\":\"" + token + L"\",\"command\":\"" + name + L"\",\"variant\":\"" + variant + L"\"}"; }
// Decodes the JS string literal ResultScript produced (\\ \" \' \uXXXX only): must round-trip.
static bool Unescape(const std::wstring& js, std::wstring& out) {
    const std::wstring head = L"window.__sandboxLog&&window.__sandboxLog(\"", tail = L"\",\"natif\");";
    if (js.compare(0, head.size(), head) != 0 || js.size() < head.size() + tail.size() || js.compare(js.size() - tail.size(), tail.size(), tail) != 0) return false;
    const std::wstring body = js.substr(head.size(), js.size() - head.size() - tail.size()); out.clear();
    for (size_t i = 0; i < body.size(); ++i) {
        const wchar_t c = body[i];
        if (c == L'"' || c == L'\n' || c == L'\r' || c == 0x2028 || c == 0x2029 || c == L'<' || c == L'>') return false; // must never appear raw
        if (c != L'\\') { out += c; continue; }
        if (++i >= body.size()) return false;
        if (body[i] == L'\\' || body[i] == L'"' || body[i] == L'\'') { out += body[i]; continue; }
        if (body[i] != L'u' || i + 4 >= body.size()) return false;
        out += static_cast<wchar_t>(std::wcstoul(body.substr(i + 1, 4).c_str(), nullptr, 16)); i += 4;
    }
    return true;
}

static void BridgeTests() {
    const std::wstring token = L"{2F0C1D6E-7A3B-4C59-8E21-90AB12CD34EF}";
    // Allowlist: unique names, every catalogue trigger allowed, and nothing destructive by name.
    std::set<std::wstring> names(DebugSandbox::AllowedCommands().begin(), DebugSandbox::AllowedCommands().end());
    Require(names.size() == DebugSandbox::AllowedCommands().size() && names.size() >= 30, "unique command names");
    for (auto& e : DebugSandbox::Catalogue()) for (auto& t : e.triggers) if (t.command) Require(DebugSandbox::Allowed(t.command, t.variant ? t.variant : L""), "every catalogue trigger is allow-listed");
    for (auto* n : {L"modal.clearData", L"modal.closeWindow", L"find.open", L"error.offline", L"fullscreen.hint", L"prompt.passwordFill", L"toast.tabLimit", L"prompt.permission", L"prompt.basicAuth", L"shortcuts.open"}) Require(names.count(n) == 1, "expected command present");
    for (auto& n : names) Require(n.find(L"eval") == std::wstring::npos && n.find(L"navigate") == std::wstring::npos && n.find(L"script") == std::wstring::npos, "no generic eval/navigate command");

    auto ok = DebugSandbox::ParseCommand(Cmd(token, L"modal.clearData", L"busy"), token);
    Require(ok && ok->name == L"modal.clearData" && ok->variant == L"busy", "valid command parsed");
    Require(DebugSandbox::ParseCommand(L"{\"token\":\"" + token + L"\",\"command\":\"sandbox.ping\"}", token).has_value(), "variant-less command");
    {   // Input modality of the page button: "keyboard" | "pointer" only, never anything else.
        auto viaKey = DebugSandbox::ParseCommand(L"{\"token\":\"" + token + L"\",\"command\":\"prompt.permission\",\"variant\":\"camera\",\"via\":\"keyboard\"}", token);
        auto viaPtr = DebugSandbox::ParseCommand(L"{\"token\":\"" + token + L"\",\"command\":\"prompt.permission\",\"variant\":\"camera\",\"via\":\"pointer\"}", token);
        Require(viaKey && viaKey->keyboard && viaPtr && !viaPtr->keyboard, "via: keyboard / pointer accepted");
        Require(!DebugSandbox::ParseCommand(L"{\"token\":\"" + token + L"\",\"command\":\"prompt.permission\",\"variant\":\"camera\",\"via\":\"touch\"}", token), "via: other values rejected");
        Require(!DebugSandbox::ParseCommand(L"{\"token\":\"" + token + L"\",\"command\":\"prompt.permission\",\"variant\":\"camera\",\"via\":\"pointer\",\"x\":\"1\"}", token), "a fifth key is still rejected");
    }
    Require(DebugSandbox::ParseCommand(L"\"{\\\"token\\\":\\\"" + token + L"\\\",\\\"command\\\":\\\"find.open\\\",\\\"variant\\\":\\\"seed\\\"}\"", token).has_value(), "JSON form of the posted string");
    Require(!DebugSandbox::ParseCommand(Cmd(L"{00000000-0000-0000-0000-000000000000}", L"find.open", L"default"), token), "wrong token");
    Require(!DebugSandbox::ParseCommand(L"{\"command\":\"find.open\",\"variant\":\"default\"}", token), "absent token");
    Require(!DebugSandbox::ParseCommand(Cmd(L"", L"find.open", L"default"), L""), "empty token never matches");
    Require(!DebugSandbox::ParseCommand(Cmd(token.substr(0, token.size() - 1), L"find.open", L"default"), token), "token prefix rejected");
    Require(!DebugSandbox::ParseCommand(Cmd(token, L"modal.format", L"default"), token), "unknown command");
    Require(!DebugSandbox::ParseCommand(Cmd(token, L"modal.clearData", L"now"), token), "variant outside its allowlist");
    Require(!DebugSandbox::ParseCommand(L"{\"token\":\"" + token + L"\",\"command\":\"modal.clearData\"}", token), "required variant missing");
    Require(!DebugSandbox::ParseCommand(Cmd(token, L"sandbox.ping", L"x"), token), "variant on a variant-less command");
    Require(!DebugSandbox::ParseCommand(L"{\"token\":\"" + token + L"\",\"command\":\"find.open\",\"variant\":\"default\",\"origin\":\"https://evil.test\"}", token), "extra member");
    Require(!DebugSandbox::ParseCommand(L"{\"token\":\"" + token + L"\",\"command\":[\"find.open\"]}", token), "array value");
    Require(!DebugSandbox::ParseCommand(L"{\"token\":\"" + token + L"\",\"command\":\"find.open\"", token), "unterminated");
    Require(!DebugSandbox::ParseCommand(L"{'token':'" + token + L"','command':'find.open'}", token), "single quotes are not JSON");
    Require(!DebugSandbox::ParseCommand(L"{\"token\":\"" + token + L"\",\"command\":\"find.open\\u0000\",\"variant\":\"default\"}", token), "NUL escape does not alias a command");
    Require(!DebugSandbox::ParseCommand(Cmd(token, L"find.open", L"default") + std::wstring(DebugSandbox::MaxMessage, L' '), token), "oversized message");
    Require(!DebugSandbox::ParseCommand(L"", token) && !DebugSandbox::ParseCommand(L"find.open", token), "non-JSON");

    // ResultScript: any text round-trips and can never close the string or the script.
    for (const std::wstring& line : {std::wstring(L"Pont actif"), std::wstring(L"\"); alert(1); (\""), std::wstring(L"</script><script>alert(1)</script>"),
                                    std::wstring(L"a\\\"b'c\nd\re f g"), std::wstring(L"Échap « démo » \x01\x1F"), std::wstring(L"x\\")}) {
        std::wstring back; Require(Unescape(DebugSandbox::ResultScript(line), back) && back == line, "ResultScript round-trips safely");
    }
    std::wstring back; Require(Unescape(DebugSandbox::ResultScript(std::wstring(5000, L'x')), back) && back.size() == 2001, "log line bounded");

    // Page: every artboard and status, the token once, the escape helpers, no decorative shadow.
    const std::string page = DebugSandbox::Html(token);
    Require(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, page.data(), static_cast<int>(page.size()), nullptr, 0) > 0, "valid UTF-8");
    for (const char* id : {"C0", "A1", "A2", "A3", "A4", "A5", "A6", "A7", "A8", "A9", "B01", "B02", "B03", "B04", "B05", "B06", "B07", "B08", "B09", "C1", "C2", "C3", "C4", "C5", "C6", "C7", "C8", "C9"})
        Require(Has(page, std::string("id=\"") + id + "\""), "every Paper artboard has its card");
    Require(DebugSandbox::Catalogue().size() == 28, "28 artboards");
    for (const char* label : {"Intégré", "Démonstration", "Natif conservé", "Sélecteur de fichiers", "Windows Hello", "Impression"}) Require(Has(page, label), "statuses and native-kept windows listed");
    Require(Has(page, "browser://debug") && Has(page, "Ctrl+Maj+F12") && Has(page, "--debug-sandbox"), "access documented in the page");
    Require(Has(page, "window.alert(") && Has(page, "window.confirm(") && Has(page, "window.prompt(") && Has(page, "beforeunload") && Has(page, "getUserMedia") && Has(page, "requestFullscreen") && Has(page, "keyboard.lock") && Has(page, "mailto:camille.demo@exemple.test") && Has(page, ".invalid"), "real web paths present");
    Require(Has(page, "oxyde de zinc") && Has(page, "ÉTÉ"), "find test content (accents, case)");
    Require(Count(page, "{2F0C1D6E-7A3B-4C59-8E21-90AB12CD34EF}") == 1, "token injected once");
    Require(Has(page, "id=\"reset\"") && Has(page, "aria-live=\"polite\""), "visible log with reset");
    Require(!Has(page, "drop-shadow") && !Has(page, "text-shadow") && Count(page, "box-shadow") == Count(page, "box-shadow:inset"), "no decorative shadow in the sandbox");
    const std::string evil = DebugSandbox::Html(L"\"</script><script>alert(1)//");
    Require(Count(evil, "</script>") == 1 && !Has(evil, "<script>alert"), "hostile token cannot break out");
    Require(Count(page, "@exemple.test") == Count(page, "camille.demo@exemple.test"), "only the fictitious account");
}

// ---- Native harness -----------------------------------------------------------------------
static LRESULT CALLBACK OwnerProc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }
static bool Styled(HWND h, DWORD ex) { return (static_cast<DWORD>(GetWindowLongPtrW(h, GWL_EXSTYLE)) & ex) == ex; }
static void Pump() { MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); } }

static void NativeTests(HINSTANCE instance, const std::wstring& root) {
    WNDCLASSW wc{}; wc.lpfnWndProc = OwnerProc; wc.hInstance = instance; wc.lpszClassName = L"LisiereBTestOwner"; RegisterClassW(&wc);
    HWND owner = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"owner", WS_POPUP, -6000, -6000, 1200, 800, nullptr, nullptr, instance, nullptr);
    Require(owner != nullptr, "test owner window");
    ShowWindow(owner, SW_SHOWNOACTIVATE);
    const float scale = PopupKit::Dpi(owner) / 96.f;
    RECT page{-6000, -6000 + static_cast<LONG>(81 * scale), -6000 + 1200, -6000 + 800};
    HWND foreground = GetForegroundWindow();

    // Exact Paper glyphs parse and stay inside the 256 view box.
    for (const char* path : {PopupKit::Glyphs::search, PopupKit::Glyphs::caretUp, PopupKit::Glyphs::caretDown, PopupKit::Glyphs::close, PopupKit::Glyphs::info, PopupKit::Glyphs::checkCircle, PopupKit::Glyphs::warningCircle, PopupKit::Glyphs::link, PopupKit::Glyphs::cornersIn}) {
        ID2D1Factory* f = nullptr; D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &f); Require(f != nullptr, "d2d factory");
        auto* g = PopupKit::ParsePath(f, path); Require(g != nullptr, "glyph parses");
        D2D1_RECT_F b{}; g->GetBounds(nullptr, &b); Require(b.left >= 8 && b.top >= 8 && b.right <= 248 && b.bottom <= 248 && b.right - b.left > 100, "glyph bounds in the Phosphor box");
        UINT32 figures = 0; g->GetFigureCount(&figures); Require(figures >= 1, "glyph has figures");
        g->Release(); f->Release();
    }

    // --- Find bar (C1/C2).
    {
        std::vector<std::wstring> queries; FindBar::Options last; int next = 0, previous = 0, closed = 0;
        FindBar::Hooks hooks; hooks.query = [&](const std::wstring& q, const FindBar::Options& o) { queries.push_back(q); last = o; };
        hooks.next = [&] { ++next; }; hooks.previous = [&] { ++previous; }; hooks.close = [&] { ++closed; };
        FindBar::Bar bar; Require(bar.Create(instance, owner, root, hooks), "find bar created");
        Require(NativeUI::IsShadowless(bar.Window()), "find bar shadowless");
        Require(!(GetClassLongPtrW(bar.Window(), GCL_STYLE) & CS_DROPSHADOW), "no CS_DROPSHADOW");
        bar.Show(page, L"oxyde"); Pump();
        Require(bar.Visible() && !queries.empty() && queries.back() == L"oxyde", "seed queries once shown");
        RECT r{}; GetWindowRect(bar.Window(), &r);
        Require(r.right == page.right - static_cast<LONG>(std::lround(8 * scale)), "anchored 8 DIP inside the page's right edge");
        Require(r.right - r.left == static_cast<LONG>(std::lround(420 * scale)) && r.bottom - r.top == static_cast<LONG>(std::lround(40 * scale)), "420x40 DIP");
        bar.Frame(Motion::Now() + Motion::Milliseconds(400)); GetWindowRect(bar.Window(), &r);
        Require(r.top == page.top + static_cast<LONG>(std::lround(8 * scale)), "settles 8 DIP under the page top");
        Require(bar.CounterLabel().empty(), "no counter while searching");
        bar.SetResult(2, 7, FindBar::State::Found); Require(bar.CounterLabel() == L"2 sur 7", "counter n sur m");
        Require(FindBar::CounterText(1, 1500, FindBar::State::Found) == L"1 sur 999+", "999+ beyond 999");
        Require(bar.KeyForTest(VK_RETURN) && next == 1, "Entrée = next");
        Require(bar.KeyForTest(VK_F3) && next == 2, "F3 = next");
        const size_t before = queries.size();
        Require(bar.KeyForTest('C', true) && last.matchCase && queries.size() == before + 1, "Alt+C toggles match case and re-queries");
        Require(bar.KeyForTest('W', true) && last.wholeWord, "Alt+W toggles whole word");
        bar.ActivateForTest(FindBar::Bar::PrevHit); Require(previous == 0, "an option change re-queries: arrows wait for the result");
        bar.SetResult(3, 7, FindBar::State::Found); bar.ActivateForTest(FindBar::Bar::PrevHit); Require(previous == 1, "previous button");
        bar.SetResult(0, 0, FindBar::State::NoMatch); Require(bar.CounterLabel() == L"Aucun résultat", "no match copy");
        bar.KeyForTest(VK_RETURN); bar.ActivateForTest(FindBar::Bar::NextHit); Require(next == 2, "arrows inert without matches");
        bar.SetResult(0, 0, FindBar::State::Unavailable); Require(bar.CounterLabel() == L"Indisponible", "unavailable state");
        bar.SetText(L""); bar.SetResult(1, 3, FindBar::State::Found); Require(bar.CounterLabel().empty(), "empty field shows no stale counter");
        const size_t q = queries.size(); bar.SetText(L"cuivre"); Require(queries.size() == q, "SetText restores without querying");
        bar.PaintForTest(); bar.SetResult(2, 7, FindBar::State::Found);
        Require(bar.RenderForTest(root, (root + L"/build/sandbox/render_find_bar.png").c_str(), 192), "find bar renders offscreen (PNG)");
        Require(bar.KeyForTest(VK_ESCAPE) && closed == 1 && !bar.Visible(), "Échap closes once and hides");
        bar.Hide(); Require(closed == 1, "Hide() never notifies");
        Require(GetForegroundWindow() == foreground || GetForegroundWindow() == bar.Window() || GetForegroundWindow() == owner, "focus handed back");
        bar.Dispose();
    }
    // --- Context toast (A7 / C8 / C9): callback exactly once on every path.
    {
        ContextToast::Toast toast; Require(toast.Create(instance, owner, root), "toast created");
        Require(NativeUI::IsShadowless(toast.Window()) && Styled(toast.Window(), WS_EX_NOACTIVATE), "toast shadowless, non-activating");
        int calls = 0; bool result = false; auto cb = [&](bool a) { ++calls; result = a; };
        toast.Show(ContextToast::TabLimitSpec(128), page, cb); toast.PaintForTest();
        Require(toast.RenderForTest(root, (root + L"/build/sandbox/render_toast_tab_limit.png").c_str(), 192), "A7 renders (PNG)");
        Require(toast.Visible() && toast.SizeDip().cx == 269 && toast.SizeDip().cy == 178, "A7 card 269x178 (Paper FD6-0)");
        RECT r{}; toast.Frame(Motion::Now() + Motion::Milliseconds(500)); GetWindowRect(toast.Window(), &r);
        Require(r.left == page.left + static_cast<LONG>(std::lround(14 * scale)) && r.bottom == page.bottom - static_cast<LONG>(std::lround(14 * scale)), "anchored 14 DIP bottom-left");
        toast.ActivateForTest(ContextToast::Toast::ActionHit); Require(calls == 1 && result, "action → done(true) once");
        toast.ActivateForTest(ContextToast::Toast::ActionHit); toast.Hide(); toast.Frame(Motion::Now() + Motion::Milliseconds(400)); Require(calls == 1 && !toast.Visible(), "nothing after done");
        calls = 0; int second = 0;
        toast.Show(ContextToast::PageSavedSpec(L"Cuivre — Wikipédia.html"), page, cb); Require(toast.SizeDip().cx == 360 && toast.SizeDip().cy == 58, "C8 status 360x58");
        toast.Show(ContextToast::BriefSpec(L"Lien copié", ContextToast::Icon::Link), page, [&](bool) { ++second; });
        Require(calls == 1 && !result && toast.SizeDip().cy == 40, "replacement: previous done(false), brief 40 DIP");
        toast.ExpireForTest(); Require(second == 1, "time-out → done(false) once");
        int third = 0; toast.Show(ContextToast::SaveFailedSpec(L"Le disque C: est plein"), page, [&](bool a) { ++third; result = a; });
        toast.Frame(Motion::Now() + Motion::Milliseconds(60000)); Require(toast.Visible() && third == 0, "failure stays until acted on");
        toast.KeyForTest(VK_ESCAPE); Require(third == 1 && !result, "Échap closes the toast once");
        int fourth = 0; toast.Show(ContextToast::SavingSpec(.42f), page, [&](bool) { ++fourth; });
        Require(toast.Current().detail == L"Page complète · 42 %", "in-progress copy");
        Require(toast.RenderForTest(root, (root + L"/build/sandbox/render_toast_progress.png").c_str(), 192), "C8 renders (PNG)");
        toast.Update(ContextToast::SavingSpec(.9f)); Require(fourth == 0, "update keeps the callback");
        toast.ActivateForTest(ContextToast::Toast::CloseHit); Require(fourth == 0, "in-progress toast has no close");
        toast.Dispose(); Require(fourth == 1, "Dispose → done(false) once");
    }
    // --- Tooltip (C5): never activates, never takes clicks, delay then neighbour instant.
    {
        Tooltip::Tip tip; Require(tip.Create(instance, owner, root), "tooltip created");
        // User request 2026-11-09: C5 surfaces take the canvas token (--color-browser-canvas #171717).
        Require(Tooltip::Layout::surface == 0x171717 && ShortcutsPanel::Layout::Surface == 0x17 && std::lround(Design::Surface::canvas * 255) == 0x17, "C5 tooltip + shortcuts panel on the canvas surface");
        Require(NativeUI::IsShadowless(tip.Window()) && Styled(tip.Window(), WS_EX_NOACTIVATE | WS_EX_TRANSPARENT), "tooltip shadowless + click-through");
        RECT target{page.left + 40, page.top - 40, page.left + 72, page.top - 8};
        tip.Hover({L"Recharger", L"Ctrl+R", L""}, target); Require(!tip.Visible() && tip.Pending(), "600 ms hover delay");
        auto wake = tip.NextWake(Motion::Now()); Require(wake.delay && wake.delay->count() > 500 && wake.delay->count() <= 600, "deadline wake, no 60 Hz polling");
        tip.Frame(Motion::Now() + Motion::Milliseconds(650)); Require(tip.Visible(), "appears after the delay");
        Require(tip.SizeDip().cy == 28, "single line 28 DIP");
        RECT r{}; GetWindowRect(tip.Window(), &r);
        const POINT expect = PopupKit::PlaceBelow(target, {r.right - r.left, r.bottom - r.top}, PopupKit::WorkArea(target), static_cast<int>(std::lround(6 * scale)));
        Require(r.left == expect.x && r.top == expect.y, "placed by PlaceBelow in the monitor work area");
        const RECT area{0, 0, 1000, 700};
        POINT p = PopupKit::PlaceBelow({100, 100, 132, 132}, {200, 28}, area, 6); Require(p.x == 100 && p.y == 138, "6 px under, left aligned");
        p = PopupKit::PlaceBelow({100, 660, 132, 692}, {200, 28}, area, 6); Require(p.y == 660 - 6 - 28, "flips above when no room below");
        p = PopupKit::PlaceBelow({950, 100, 982, 132}, {200, 28}, area, 6); Require(p.x == 800, "aligns to the edge when short of room");
        tip.Hover({L"Copier le lien", L"", L""}, target); Require(tip.Visible(), "neighbour: instant");
        tip.Hide(); tip.Frame(Motion::Now() + Motion::Milliseconds(200)); Require(!tip.Visible(), "fades out");
        tip.Hover({L"Précédent", L"Alt+←", L"Clic droit ou maintien pour l'historique"}, target); Require(tip.Visible(), "within 300 ms of the last: instant");
        Require(tip.SizeDip().cy == 2 + 12 + 20 + 4 + 20, "header + detail (Paper EST-0: 58)");
        Require(tip.RenderForTest(root, (root + L"/build/sandbox/render_tooltip_detail.png").c_str(), 192), "tooltip renders (PNG)");
        tip.HideNow(); tip.Show(L"Respecter la casse", L"Alt+C", target); Require(tip.Visible(), "keyboard focus: immediate");
        Require(GetForegroundWindow() != tip.Window(), "never activates");
        tip.Dispose();
    }
    // --- Full-screen hint (C6/C7).
    {
        FullscreenHint::Hint hint; Require(hint.Create(instance, owner, root), "hint created");
        Require(NativeUI::IsShadowless(hint.Window()) && Styled(hint.Window(), WS_EX_NOACTIVATE | WS_EX_TRANSPARENT), "hint shadowless + click-through");
        RECT monitor{-6000, -6000, -6000 + 1180, -6000 + 815};
        hint.Show(monitor, true, L"youtube.com"); Require(hint.Visible() && hint.CurrentCopy().strong == L"youtube.com" && hint.CurrentCopy().key == L"Échap", "page variant names the origin");
        hint.Frame(Motion::Now() + Motion::Milliseconds(400)); RECT r{}; GetWindowRect(hint.Window(), &r);
        Require(r.top == monitor.top + static_cast<LONG>(std::lround(24 * scale)) && std::abs((r.left + r.right) / 2 - (monitor.left + monitor.right) / 2) <= 1, "centred 24 DIP from the top");
        hint.Frame(Motion::Now() + Motion::Milliseconds(3300)); hint.Frame(Motion::Now() + Motion::Milliseconds(3600)); Require(!hint.Visible(), "leaves after 3 s");
        hint.Show(monitor, false); Require(hint.CurrentCopy().key == L"F11", "F11 variant");
        hint.Show(monitor, true, L"jeu.exemple.fr", true); Require(hint.CurrentCopy().prefix == L"Maintenez", "locked keyboard: hold Échap");
        hint.SetHoldProgress(.6f); hint.PaintForTest(); Require(hint.RenderForTest(root, (root + L"/build/sandbox/render_fullscreen_locked.png").c_str(), 192), "hint renders (PNG)"); hint.Frame(Motion::Now() + Motion::Milliseconds(5000)); Require(hint.Visible(), "stays while Échap is held");
        hint.Dispose();
    }
    // --- Shortcuts panel (C5 Ctrl+/).
    {
        ShortcutsPanel::Panel panel; Require(panel.Create(instance, owner, root), "panel created");
        Require(NativeUI::IsShadowless(panel.Window()), "panel shadowless");
        int closed = 0; panel.SetClosed([&] { ++closed; });
        panel.Show(page); const int all = panel.VisibleRowCount(); Require(panel.Visible() && all >= 25, "real shortcuts listed");
        SetWindowTextW(panel.FilterEdit(), L"echap"); Require(panel.VisibleRowCount() == 0, "filter narrows (no « Échap » shortcut row)");
        SetWindowTextW(panel.FilterEdit(), L"ONGLET"); Require(panel.VisibleRowCount() > 0 && panel.VisibleRowCount() < all, "case-insensitive filter");
        SetWindowTextW(panel.FilterEdit(), L"precedent"); Require(panel.VisibleRowCount() >= 1, "accent-insensitive filter");
        panel.PaintForTest(); SetWindowTextW(panel.FilterEdit(), L"");
        Require(panel.RenderForTest(root, (root + L"/build/sandbox/render_shortcuts.png").c_str(), 192), "shortcuts panel renders (PNG)");
        Require(panel.KeyForTest(VK_ESCAPE) && closed == 1 && !panel.Visible(), "Échap closes once");
        panel.Hide(); Require(closed == 1, "Hide() never notifies");
        panel.Dispose();
    }
    DestroyWindow(owner);
}

int main() {
    BridgeTests();
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    InitCommonControls();
    wchar_t cwd[MAX_PATH]{}; GetCurrentDirectoryW(MAX_PATH, cwd);
    for (auto* name : FontFiles) AddFontResourceExW((std::wstring(cwd) + L"\\assets\\fonts\\" + name).c_str(), FR_PRIVATE, nullptr);
    NativeTests(GetModuleHandleW(nullptr), cwd);
    std::printf("PASS debug sandbox + B components: %d checks (allowlist, token, JSON, escaping, catalogue, find bar, toasts, tooltip, full-screen hint, shortcuts panel)\n", checks);
    CoUninitialize();
    return 0;
}
