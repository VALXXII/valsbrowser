// Contracts of the A/B dialog family (src/modal.h, src/origin-prompt.h, src/password-demo.h):
// `done` exactly once (button, Échap, Abort, owner destroyed, Dispose), Échap = cancel button,
// Entrée = default but never a destructive one, validation blocks the default until fixed,
// busy locks the controls, secrets are wiped, focus is restored, presets carry Paper's text.
// Usage: modal_test.exe <repo root>
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <wincodec.h>
#include <algorithm>
#include <vector>
#include "../src/chrome.h"
#include "../src/modal.h"
#include "../src/origin-prompt.h"
#include "../src/password-demo.h"
#include "../src/settings-panel.h"

using namespace NativeChrome;
static int failures = 0, checks = 0;
static void Check(bool ok, const char* what) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", what); } else std::printf("ok   %s\n", what); }
static void Pump(int ms = 30) { const DWORD end = GetTickCount() + ms; MSG m; while (static_cast<int>(end - GetTickCount()) > 0) while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); } }
static RECT Area() { return {100, 100, 1100, 800}; }

int wmain(int argc, wchar_t** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 2;
    INITCOMMONCONTROLSEX cc{sizeof cc, ICC_STANDARD_CLASSES}; InitCommonControlsEx(&cc);
    const std::wstring root = argc > 1 ? argv[1] : L".";
    HINSTANCE hi = GetModuleHandleW(nullptr);
    WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = hi; wc.lpszClassName = L"ModalTestOwner"; RegisterClassW(&wc);
    HWND owner = CreateWindowExW(0, wc.lpszClassName, L"owner", WS_POPUP | WS_VISIBLE, 100, 100, 1000, 700, nullptr, nullptr, hi, nullptr);
    HWND ownerEdit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE, 10, 10, 200, 24, owner, nullptr, hi, nullptr);
    Modal::Dialog d; Check(d.Create(hi, owner, root, {}), "Dialog::Create");
    using D = Modal::Dialog;

    {   // A2 alert: Entrée answers OK once; Échap afterwards does nothing.
        int calls = 0, button = -9;
        SetFocus(ownerEdit);
        Check(d.Show(Modal::AlertSpec(L"exemple.fr", L"Bonjour"), Area(), [&](const Modal::Result& r) { ++calls; button = r.button; }), "alert: Show");
        Check(!d.Show(Modal::AlertSpec(L"x", L"y"), Area(), [](const Modal::Result&) {}), "alert: second Show refused while open");
        Check(d.FocusForTest() == D::ButtonHit(0), "alert: initial focus on OK");
        d.KeyForTest(VK_RETURN); d.KeyForTest(VK_ESCAPE); d.KeyForTest(VK_RETURN);
        Check(calls == 1 && button == Modal::AcceptButton, "alert: Entrée -> AcceptButton, exactly once");
        Check(!d.Visible(), "alert: closed");
        Check(GetFocus() == ownerEdit, "alert: focus restored to the owner's control");
        d.FinishAnimationsForTest();
    }
    {   // A2 alert, repeated: blocking check returned in Result.checked; Échap = OK (cancel button).
        int calls = 0; Modal::Result got;
        d.Show(Modal::AlertSpec(L"exemple.fr", L"Encore", true), Area(), [&](const Modal::Result& r) { ++calls; got = r; });
        d.ActivateForTest(D::SimpleCheckHit); d.KeyForTest(VK_ESCAPE);
        Check(calls == 1 && got.checked && got.button == Modal::AcceptButton, "alert repeated: check returned, Échap answers OK");
        d.FinishAnimationsForTest();
    }
    {   // A2 confirm: Échap = Annuler.
        int calls = 0, button = -9;
        d.Show(Modal::ConfirmSpec(L"exemple.fr", L"Sûr ?"), Area(), [&](const Modal::Result& r) { ++calls; button = r.button; });
        Check(d.FocusForTest() == D::ButtonHit(1), "confirm: focus on OK (default)");
        d.KeyForTest(VK_TAB); Check(d.FocusForTest() == D::ButtonHit(0), "confirm: Tab reaches Annuler");
        d.KeyForTest(VK_ESCAPE);
        Check(calls == 1 && button == Modal::CancelButton, "confirm: Échap -> CancelButton once");
        d.FinishAnimationsForTest();
    }
    {   // A3 prompt: value typed, Entrée from the field, text returned verbatim (no validation).
        int calls = 0; std::wstring value = L"?";
        d.Show(Modal::PromptSpec(L"trello.com", L"Nom de la nouvelle liste :", L"À faire cette semaine"), Area(), [&](const Modal::Result& r) { ++calls; if (!r.values.empty()) value = r.values[0]; });
        Check(d.FieldForTest(0) == L"À faire cette semaine", "prompt: default value");
        Check(d.FocusForTest() == D::FieldStop(0), "prompt: focus in the field");
        d.SetFieldForTest(0, L""); d.KeyForTest(VK_RETURN);
        Check(calls == 1 && value.empty(), "prompt: empty answer accepted (site receives an empty string)");
        d.FinishAnimationsForTest();
    }
    {   // A3 browser variant: validated at first submit then at every keystroke.
        int calls = 0; std::wstring value;
        auto spec = Modal::InputSpec(L"Modifier l'adresse du favori", L"Wiki", L"", L"Enregistrer", [](const std::vector<std::wstring>& v) { return v.empty() || v[0].find(L'.') == std::wstring::npos ? std::wstring(L"Saisissez une adresse web, par ex. notion.so/atelier") : std::wstring(); });
        d.Show(spec, Area(), [&](const Modal::Result& r) { ++calls; if (!r.values.empty()) value = r.values[0]; });
        d.SetFieldForTest(0, L"notion atelier lumen"); d.KeyForTest(VK_RETURN);
        Check(calls == 0 && !d.FieldErrorForTest().empty(), "input: invalid submit shows the inline error, dialog stays");
        d.KeyForTest(VK_RETURN); Check(calls == 0, "input: Entrée does nothing while invalid");
        d.SetFieldForTest(0, L"notion.so/atelier");
        Check(d.FieldErrorForTest().empty(), "input: error cleared on the next keystroke");
        d.KeyForTest(VK_RETURN);
        Check(calls == 1 && value == L"notion.so/atelier", "input: valid submit answers once");
        d.FinishAnimationsForTest();
    }
    {   // A4 beforeunload: focus on « Rester », Entrée there stays; destructive action needs explicit focus.
        int calls = 0, button = -9;
        d.Show(Modal::BeforeUnloadSpec(L"docs.atelier-lumen.fr", false), Area(), [&](const Modal::Result& r) { ++calls; button = r.button; });
        Check(d.FocusForTest() == D::ButtonHit(0), "beforeunload: focus on « Rester sur la page »");
        d.KeyForTest(VK_RETURN);
        Check(calls == 1 && button == Modal::CancelButton, "beforeunload: Entrée stays on the page");
        d.FinishAnimationsForTest();
        Check(Modal::BeforeUnloadSpec(L"a.fr", true).title == L"Actualiser ce site ?", "beforeunload: reload title");
    }
    {   // A5: Entrée never clears; hold-then-busy flow; Échap ignored while busy; Snapshot.
        int calls = 0, button = -9; Modal::Result snap; bool held = false;
        auto spec = Modal::ClearDataSpec();
        spec.buttons[1].closes = false;
        spec.onButton = [&](int id) { held = id == Modal::AcceptButton; snap = d.Snapshot(); d.SetBusy(true, L"Effacement…"); };
        d.Show(spec, Area(), [&](const Modal::Result& r) { ++calls; button = r.button; });
        Check(d.FocusForTest() == D::ButtonHit(0), "clear data: focus on Annuler");
        d.KeyForTest(VK_TAB | 0); // Tab from Annuler -> Effacer; back to a check with Shift is covered below
        d.ActivateForTest(D::CheckHit(4));   // pointer toggles « Autorisations des sites »
        // Focus a check, then Entrée: the destructive default must not fire.
        d.KeyForTest(VK_TAB);
        while (d.FocusForTest() != D::CheckHit(0)) d.KeyForTest(VK_TAB);
        d.KeyForTest(VK_RETURN);
        Check(calls == 0 && !held, "clear data: Entrée never triggers the destructive action");
        d.ActivateForTest(D::ButtonHit(1));
        Check(held && d.BusyForTest() && calls == 0, "clear data: hold -> busy, dialog open");
        Check(snap.checks.size() == 5 && snap.checks[0] && !snap.checks[3] && snap.checks[4] && snap.select == 1, "clear data: Snapshot reports categories and period");
        d.KeyForTest(VK_ESCAPE); Check(calls == 0 && d.Visible(), "clear data: Échap ignored while busy");
        d.SetBusy(false); d.SetError(L"Impossible d'effacer les données.");
        Check(!d.BusyForTest() && d.Visible(), "clear data: failure re-enables the controls");
        d.SetBusy(true, L"Effacement…"); d.Finish(Modal::AcceptButton);
        Check(calls == 1 && button == Modal::AcceptButton, "clear data: Finish answers once");
        d.FinishAnimationsForTest();
    }
    {   // A5 Période: a real drop-down list (shadowless native menu under the field), never a cycle.
        // The keys are queued before the list opens; its modal loop reads them like a user's.
        int calls = 0; bool held = false;
        auto spec = Modal::ClearDataSpec();
        spec.buttons[1].closes = false; spec.onButton = [&](int) { held = true; };
        d.Show(spec, Area(), [&](const Modal::Result&) { ++calls; });
        while (d.FocusForTest() != D::SelectHit) d.KeyForTest(VK_TAB);
        auto post = [&](std::initializer_list<WPARAM> keys) { for (auto k : keys) PostMessageW(owner, WM_KEYDOWN, k, 0); };
        auto period = [&] { return d.Snapshot().select; };
        // While open: the list is a shadowless popup dropped under the field, the current row checked.
        static Modal::Dialog* probeDialog; static bool probed, shadowless, below, current; static int ticks; probeDialog = &d; probed = shadowless = below = current = false; ticks = 0;
        auto probe = [](HWND, UINT, UINT_PTR id, DWORD) {   // waits for the list to be open and settled (180 ms entrance)
            auto* m = probeDialog->MenuForTest();
            if (!m || !m->Open() || m->TestEntering(0)) { if (++ticks > 100) { KillTimer(nullptr, id); PostMessageW(probeDialog->Window(), WM_KEYDOWN, VK_ESCAPE, 0); } return; }
            KillTimer(nullptr, id);
            HWND w = m->LevelWindow(0); RECT r{}; GetWindowRect(w, &r);
            const auto f = probeDialog->BoxForTest(D::SelectHit); RECT dr{}; GetWindowRect(probeDialog->Window(), &dr);
            const float k = GetDpiForWindow(w) / 96.f;
            probed = true; shadowless = NativeUI::IsShadowless(w) && !(GetClassLongPtrW(w, GCL_STYLE) & CS_DROPSHADOW);
            below = std::abs(r.top - (dr.top + static_cast<LONG>(std::lround(f.bottom * k)))) <= 1 && std::abs(r.right - (dr.left + static_cast<LONG>(std::lround(f.right * k)))) <= 1;
            const auto nodes = m->AccessibleNodes(0); int checked = -1, i = -1; for (auto& n : nodes) if (n.role == Accessibility::Role::MenuItem) { ++i; if (n.status == L"Sélectionné") checked = i; }
            current = checked == 1 && nodes.size() == 6;
            std::printf("     list %ld,%ld-%ld,%ld under field bottom %ld right %ld\n", r.left, r.top, r.right, r.bottom, dr.top + static_cast<LONG>(std::lround(f.bottom * k)), dr.left + static_cast<LONG>(std::lround(f.right * k)));
            PostMessageW(probeDialog->Window(), WM_KEYDOWN, VK_ESCAPE, 0);
        };
        const UINT_PTR probeTimer = SetTimer(nullptr, 0, 20, probe); d.KeyForTest(VK_RETURN); KillTimer(nullptr, probeTimer);
        Check(probed && shadowless, "période: the list is a shadowless popup (no DWM rounding, no CS_DROPSHADOW)");
        Check(below, "période: the list drops under the field, right edges aligned");
        Check(current, "période: five options, the current one marked « Sélectionné »");
        Check(period() == 1 && d.Visible() && calls == 0 && !held, "période: Échap closes the list, value unchanged, nothing cleared");
        Check(d.FocusForTest() == D::SelectHit && GetFocus() == d.Window(), "période: focus back on the field after Échap");
        // Entrée opens on the checked row: ↓ then Entrée picks the next one.
        post({VK_DOWN, VK_RETURN}); d.KeyForTest(VK_RETURN);
        Check(period() == 2 && !held && calls == 0 && d.Visible(), "période: Entrée opens, ↓ + Entrée choose « 7 derniers jours », no clearing");
        Check(d.FocusForTest() == D::SelectHit && GetFocus() == d.Window(), "période: focus back on the field after a choice");
        // Every option, from Alt+↓, Espace, ↓, F4 and the pointer.
        const wchar_t* openers[] = {L"alt", L"space", L"down", L"f4", L"pointer"};
        bool all = true;
        for (int i = 0; i < 5; ++i) {
            std::vector<WPARAM> keys{VK_HOME}; for (int j = 0; j < i; ++j) keys.push_back(VK_DOWN); keys.push_back(VK_RETURN);
            for (auto k : keys) PostMessageW(owner, WM_KEYDOWN, k, 0);
            const std::wstring how = openers[i];
            if (how == L"alt") d.AltKeyForTest(VK_DOWN); else if (how == L"space") d.KeyForTest(VK_SPACE); else if (how == L"down") d.KeyForTest(VK_DOWN); else if (how == L"f4") d.KeyForTest(VK_F4); else d.ActivateForTest(D::SelectHit);
            const bool ok = period() == i && d.Visible() && !held && calls == 0 && !d.ChoosingForTest(); all = all && ok;
            std::printf("     option %d via %ls -> %d (%ls)\n", i, openers[i], period(), Modal::ClearDataPeriods()[static_cast<size_t>(period())].c_str());
        }
        Check(all, "période: each of the 5 options can be chosen (Alt+↓, Espace, ↓, F4, pointer), dialog stays open");
        // ↑/↓ on the closed field never change the value silently: Échap keeps it.
        post({VK_ESCAPE}); d.KeyForTest(VK_UP);
        Check(period() == 4, "période: ↑ opens the list instead of cycling; Échap keeps « Depuis toujours »");
        // A click outside the list (on the owner) dismisses it without choosing.
        post({VK_HOME}); PostMessageW(owner, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(5, 5)); d.ActivateForTest(D::SelectHit);
        Check(period() == 4 && d.Visible() && calls == 0 && !held, "période: click outside cancels, value unchanged");
        // The dialog closes while the list is open (tab closed, Abort): the late answer is dropped.
        static Modal::Dialog* abortDialog; abortDialog = &d;
        SetTimer(nullptr, 0, 60, [](HWND, UINT, UINT_PTR id, DWORD) { KillTimer(nullptr, id); abortDialog->Abort(); });
        post({VK_HOME}); d.KeyForTest(VK_RETURN);
        auto* m = d.MenuForTest(); Pump(60);
        Check(calls == 1 && !d.Visible() && !held && m && !m->Open(), "période: Abort while the list is open closes both, done once, nothing cleared");
        d.FinishAnimationsForTest();
    }
    {   // Host hook (browser): the shared menu through ChooseFromMenu; keyboard flag and anchor passed.
        ContextMenu::Menu shared; shared.Init(hi, owner, root);
        int hookCalls = 0; bool sawKeyboard = false; int sawCurrent = -1; RECT sawAnchor{};
        Modal::Dialog::Hooks h; h.dismiss = [&] { shared.Cancel(); }; h.choose = [&](const std::vector<std::wstring>& o, int cur, RECT a, bool kb) { ++hookCalls; sawKeyboard = kb; sawCurrent = cur; sawAnchor = a; return Modal::ChooseFromMenu(shared, o, cur, a, kb, true); };
        Modal::Dialog hd; Check(hd.Create(hi, owner, root, std::move(h)), "host hook: Create");
        int calls = 0; hd.Show(Modal::ClearDataSpec(), Area(), [&](const Modal::Result&) { ++calls; });
        while (hd.FocusForTest() != D::SelectHit) hd.KeyForTest(VK_TAB);
        PostMessageW(owner, WM_KEYDOWN, VK_UP, 0); PostMessageW(owner, WM_KEYDOWN, VK_RETURN, 0); hd.AltKeyForTest(VK_DOWN);
        Check(hookCalls == 1 && sawKeyboard && sawCurrent == 1 && sawAnchor.bottom > sawAnchor.top, "host hook: called once with keyboard, current and the field's screen rect");
        Check(hd.Snapshot().select == 0 && calls == 0 && hd.Visible() && !hd.MenuForTest(), "host hook: ↑ + Entrée from the checked row -> « Dernière heure », no own menu created");
        hd.Abort(); hd.FinishAnimationsForTest(); hd.Dispose(); shared.Dispose();
    }
    {   // Select::checks (« Importer depuis un navigateur… »): choosing a source rebuilds the checks with their
        // availability; a disabled check is never toggled nor in the Tab order; the error line is cleared.
        int chosen = 1;
        Modal::Dialog::Hooks h; h.choose = [&](const std::vector<std::wstring>&, int, RECT, bool) { return chosen; };
        Modal::Dialog hd; Check(hd.Create(hi, owner, root, std::move(h)), "select checks: Create");
        auto checksFor = [](int i) {
            Modal::Check a, b, c; a.label = L"Favoris"; b.label = L"Historique"; c.label = L"Cookies et connexions";
            a.value = true; b.value = i == 0; b.disabled = i != 0; b.detail = i ? L"Un fichier HTML ne contient que des favoris." : L"";
            c.disabled = true; c.detail = L"Non disponible";
            return std::vector<Modal::Check>{a, b, c};
        };
        Modal::Spec spec; spec.title = L"Importer depuis un navigateur"; spec.hasSelect = true; spec.select.options = {L"Chrome", L"Fichier de favoris HTML…"};
        spec.select.checks = checksFor; spec.checks = checksFor(0);
        spec.buttons = {Modal::Make(Modal::CancelButton, L"Annuler", Modal::Role::Secondary, false, true, false), Modal::Make(Modal::AcceptButton, L"Importer", Modal::Role::Primary, true, false, true)};
        spec.buttons[1].closes = false; spec.onButton = [&](int) { hd.SetError(L"Choisissez au moins une donnée à importer."); };
        int calls = 0; hd.Show(spec, Area(), [&](const Modal::Result&) { ++calls; });
        hd.ActivateForTest(D::CheckHit(2));
        Check(!hd.Snapshot().checks[2], "select checks: a disabled check (cookies) cannot be ticked");
        hd.ActivateForTest(D::ButtonHit(1));
        hd.ActivateForTest(D::SelectHit);
        const auto snap = hd.Snapshot();
        Check(snap.select == 1 && snap.checks.size() == 3 && snap.checks[0] && !snap.checks[1] && !snap.checks[2], "select checks: the HTML source has no history");
        Check(hd.CurrentSpec().checks[1].disabled && hd.CurrentSpec().checks[1].detail == L"Un fichier HTML ne contient que des favoris.", "select checks: availability and its reason follow the source");
        bool reached = false; for (int k = 0; k < 12; ++k) { hd.KeyForTest(VK_TAB); if (hd.FocusForTest() == D::CheckHit(1) || hd.FocusForTest() == D::CheckHit(2)) reached = true; }
        Check(!reached, "select checks: disabled checks are not in the Tab order");
        chosen = 0; hd.ActivateForTest(D::SelectHit);
        Check(hd.Snapshot().checks[1] && !hd.CurrentSpec().checks[1].disabled, "select checks: back to the browser, history available again");
        hd.Abort(); hd.FinishAnimationsForTest(); hd.Dispose();
        Check(calls == 1, "select checks: done once");
    }
    {   // Abort / Dispose / owner destroyed: done({-1}) once.
        int calls = 0, button = -9;
        d.Show(Modal::CloseWindowSpec(12), Area(), [&](const Modal::Result& r) { ++calls; button = r.button; });
        d.Abort(); d.Abort();
        Check(calls == 1 && button == -1, "abort: done(-1) once");
        d.FinishAnimationsForTest();
        Modal::Dialog e; HWND owner2 = CreateWindowExW(0, wc.lpszClassName, L"owner2", WS_POPUP, 0, 0, 800, 600, nullptr, nullptr, hi, nullptr);
        e.Create(hi, owner2, root, {}); int calls2 = 0;
        e.Show(Modal::AboutSpec(L"0.3.113", Modal::AboutUpToDate), {0, 0, 800, 600}, [&](const Modal::Result& r) { ++calls2; button = r.button; });
        DestroyWindow(owner2); Pump();
        Check(calls2 == 1 && button == -1, "owner destroyed: done(-1) once");
        e.Dispose(); Check(calls2 == 1, "owner destroyed: Dispose does not answer again");
        Modal::Dialog f; f.Create(hi, owner, root, {}); int calls3 = 0;
        f.Show(Modal::PrivateSpec(), Area(), [&](const Modal::Result&) { ++calls3; }); f.Dispose();
        Check(calls3 == 1, "dispose while open: done once");
    }
    {   // A6 presets (Paper text) and the download variant turns destructive.
        auto a = Modal::CloseWindowSpec(12);
        Check(a.title == L"Fermer 12 onglets ?" && a.checkLabel == L"Ne plus demander" && a.buttons[1].role == Modal::Role::Primary, "close window: Paper text");
        auto b = Modal::CloseWindowSpec(3, L"orca-windows-setup.exe", L"50.4/200.0MB");
        Check(b.checkLabel.empty() && b.buttons[1].role == Modal::Role::Danger && b.infos.size() == 1, "close window + download: destructive, no check");
        // A6 420 (Paper EY9-0, comment c-npdt3g): width, caps after the body, check on the actions row.
        Check(a.width == 420 && b.width == 420 && Modal::ClearDataSpec().width == 0, "close window: 420 DIP for this confirmation only");
        Check(a.body == L"Rouvrez-les depuis l'historique ou avec" && a.bodyKeys == std::vector<std::wstring>{L"Ctrl", L"Maj", L"T"} && a.checkInActions, "close window: Ctrl Maj T as key caps");
        {
            int calls = 0; Modal::Result got;
            d.Show(a, Area(), [&](const Modal::Result& r) { ++calls; got = r; });
            const auto box = d.BoxForTest(D::SimpleCheckHit), cancel = d.BoxForTest(D::ButtonHit(0)), close = d.BoxForTest(D::ButtonHit(1));
            Check(d.WidthForTest() == 420, "close window: panel 420 DIP");
            Check(box.top == cancel.top && box.bottom == cancel.bottom && box.bottom - box.top == 32 && box.left == 16 && box.right + 8 <= cancel.left && close.right == 420 - 16, "close window: check left on the buttons' row (EYG-0), buttons right");
            Check(d.HeightForTest() <= 126 + 0.5f && d.HeightForTest() >= 124 - 0.5f, "close window: 420x126 like EY9-0 (one body line, caps inline)");
            Check(d.FocusForTest() == D::ButtonHit(0), "close window: focus on Annuler");
            for (UINT dpi : {96u, 144u, 192u}) Check(d.RenderForTest(root + L"/build/popups/a6-close-420-" + std::to_wstring(dpi) + L".png", dpi), "close window 420 render sample");
            d.ActivateForTest(D::SimpleCheckHit); d.PaintForTest();
            Check(d.Snapshot().checked && calls == 0, "close window: the check toggles in place");
            d.ActivateForTest(D::ButtonHit(1));
            Check(calls == 1 && got.button == Modal::AcceptButton && got.checked, "close window: « Fermer la fenêtre » with « Ne plus demander »");
            d.FinishAnimationsForTest();
            const std::wstring longName = L"un-fichier-au-nom-vraiment-tres-long-pour-verifier-le-retour-a-la-ligne-et-la-coupure-2026-11-10.exe";
            d.Show(Modal::CloseWindowSpec(3, longName, L"50.4/200.0MB"), Area(), [](const Modal::Result&) {});
            const auto keep = d.BoxForTest(D::ButtonHit(0)), destroy = d.BoxForTest(D::ButtonHit(1));
            Check(d.WidthForTest() == 420 && keep.left >= 16 && destroy.right <= 420 - 16 && keep.right <= destroy.left, "close window + long file name: buttons fit, no clipping");
            Check(d.RenderForTest(root + L"/build/popups/a6-close-420-download-long-144.png", 144), "close window + download render sample");
            d.PaintForTest(); d.Abort(); d.FinishAnimationsForTest();
            Modal::Spec one = Modal::CloseWindowSpec(2); one.checkLabel = L"Ne plus demander pour cette fenêtre ni pour les autres fenêtres de ce profil";
            d.Show(one, Area(), [](const Modal::Result&) {});
            const auto row = d.BoxForTest(D::SimpleCheckHit), btn = d.BoxForTest(D::ButtonHit(0));
            Check(row.bottom <= btn.top, "close window: a check too long for the row falls back above the buttons");
            d.PaintForTest(); d.Abort(); d.FinishAnimationsForTest();
        }
        auto c = Modal::OpenManyTabsSpec(61, L"Archives 2025", 40, 88);
        Check(c.title == L"Ouvrir 40 des 61 favoris ?" && c.buttons[1].label == L"Ouvrir 40 onglets", "open many tabs: limit wording");
        for (auto& s : {Modal::AlertSpec(L"a", L"b"), Modal::ConfirmSpec(L"a", L"b"), Modal::PromptSpec(L"a", L"b", L"c"), Modal::BeforeUnloadSpec(L"a", false), Modal::ClearDataSpec(), a, b, c, Modal::DeleteBookmarksSpec(2), Modal::OpenManyTabsSpec(34, L"Veille design")}) {
            bool accept = false; for (auto& btn : s.buttons) accept = accept || btn.id == Modal::AcceptButton; Check(accept, "preset: accepting action is AcceptButton");
        }
    }
    {   // A8 About: status button stays open and calls onButton; link closes with its id.
        int calls = 0, button = -9, pressed = -1;
        auto spec = Modal::AboutSpec(L"0.3.113", Modal::AboutUpToDate, L"Vérifié aujourd'hui à 9h42");
        spec.onButton = [&](int id) { pressed = id; d.SetStatus(Modal::AboutStatus(Modal::AboutChecking)); };
        Check(spec.body == L"Version 0.3.113 · Windows 64 bits", "about: version line");
        d.Show(spec, Area(), [&](const Modal::Result& r) { ++calls; button = r.button; });
        d.ActivateForTest(D::StatusHit);
        Check(pressed == Modal::AboutCheckButton && d.Visible() && calls == 0, "about: Rechercher keeps the dialog open");
        d.ActivateForTest(D::LinkHit(1));
        Check(calls == 1 && button == Modal::AboutLicensesLink, "about: link closes with its id");
        d.FinishAnimationsForTest();
    }
    {   // A8 status row: 48 minimum, grows (padding 6) for a 2-line detail; button and icon stay centred.
        auto spec = Modal::AboutSpec(L"0.3.132", Modal::AboutUpToDate, L"Dernière vérification réussie");
        d.Show(spec, Area(), [](const Modal::Result&) {});
        const float h1 = d.HeightForTest(); const auto b1 = d.BoxForTest(D::StatusHit);
        auto two = Modal::AboutStatus(Modal::AboutOffline, L"La recherche de mises à jour a échoué. Réessayez.");
        d.SetStatus(two); d.PaintForTest();
        const float h2 = d.HeightForTest(); const auto b2 = d.BoxForTest(D::StatusHit);
        Check(h2 > h1 && std::fabs((h2 - h1) - 20) < .5f, "A8: a 2-line detail grows the row from 48 to 68 (20 + 36 + 2x6)");
        Check(std::fabs((b2.top - b1.top) - 10) < .5f && b2.bottom - b2.top == 32, "A8: the status button stays centred in the taller row");
        for (UINT dpi : {96u, 144u, 192u}) Check(d.RenderForTest(root + L"/build/popups/a8-status-2lines-" + std::to_wstring(dpi) + L".png", dpi), "A8 2-line render sample");
        auto one = Modal::AboutStatus(Modal::AboutReady, L"", L"0.3.133"); d.SetStatus(one);
        Check(std::fabs(d.HeightForTest() - h1) < .5f, "A8: a one-line detail keeps the 48 row");
        auto none = Modal::AboutStatus(Modal::AboutChecking); none.title = L"Installation…"; none.hasButton = false; d.SetStatus(none); d.PaintForTest();        Check(std::fabs(d.HeightForTest() - h1) < .5f, "A8: no detail, no button: 48 row");
        auto lng = Modal::AboutStatus(Modal::AboutChecking); lng.title = L"Téléchargement de la version 0.3.133-beta.2026.11.10+build.4567…"; lng.detail = L"42 %";
        lng.button = Modal::Make(Modal::AboutCancelButton, L"Annuler", Modal::Role::Quiet, false, false, false, L"AboutCancel"); lng.button.closes = false; d.SetStatus(lng); d.PaintForTest();
        Check(std::fabs(d.HeightForTest() - h1) < .5f && d.BoxForTest(D::StatusHit).right <= d.WidthForTest() - 16, "A8: long version title stays one line (ellipsis), Annuler inside");
        Check(d.RenderForTest(root + L"/build/popups/a8-status-long-144.png", 144), "A8 long render sample");
        d.Abort(); d.FinishAnimationsForTest();
    }
    {   // Long JS message (several KB): the panel fits the area, message scrolls.
        std::wstring longText; for (int i = 0; i < 400; ++i) longText += L"Ligne de texte brut <b>sans</b> balisage & sans échappement. ";
        int calls = 0;
        d.Show(Modal::AlertSpec(L"exemple.fr", longText), {100, 100, 1100, 500}, [&](const Modal::Result&) { ++calls; });
        Check(d.HeightForTest() <= 400 - 16 - 32 + 1 && d.HeightForTest() > 100, "long message: panel fits the page area");
        d.PaintForTest(); d.Abort(); d.FinishAnimationsForTest();
    }
    {   // Every preset paints (smoke).
        for (auto s : {Modal::ClearDataSpec(), Modal::AboutSpec(L"0.3.113", Modal::AboutReady, L"", L"0.3.114"), Modal::AboutSpec(L"0.3.113", Modal::AboutOffline), Modal::PrivateSpec(true), Modal::CloseWindowSpec(3, L"f.exe", L"1/2MB")}) {
            d.Show(s, Area(), [](const Modal::Result&) {}); d.PaintForTest(); d.Abort(); d.FinishAnimationsForTest();
        }
        Check(true, "presets paint without error");
    }
    // ---- origin prompts --------------------------------------------------------------------------
    OriginPrompt::Prompt p; Check(p.Create(hi, owner, root, {}), "Prompt::Create");
    {
        OriginPrompt::Spec s; s.kind = OriginPrompt::Kind::Permission; s.origin = L"rendez-vous.doctolib.fr";
        s.permissions = {OriginPrompt::Permission::Camera, OriginPrompt::Permission::Microphone, OriginPrompt::Permission::Geolocation};
        auto m = OriginPrompt::Build(s, true);
        Check(m.title == L"rendez-vous.doctolib.fr veut utiliser" && m.rows.size() == 3 && m.rows[2].trailing == L"Approximative" && m.width == 340, "permission: grouped request (B02)");
        int calls = 0; OriginPrompt::Result got;
        RECT anchor{200, 150, 224, 174};
        p.Show(s, anchor, [&](OriginPrompt::Result& r) { ++calls; got = r; });
        p.Surface().FinishAnimationsForTest(); RECT w{}; GetWindowRect(p.Window(), &w);
        Check(w.left == anchor.left - MulDiv(4, GetDpiForWindow(p.Window()), 96) && w.top == anchor.bottom + MulDiv(16, GetDpiForWindow(p.Window()), 96), "permission: 4 DIP left of the indicator, 16 DIP under it (B01)");
        p.Surface().ActivateForTest(Modal::Dialog::SimpleCheckHit); p.Surface().ActivateForTest(Modal::Dialog::ButtonHit(1));
        Check(calls == 1 && got.action == OriginPrompt::Action::Allow && got.remember, "permission: Autoriser + Mémoriser");
        p.Surface().FinishAnimationsForTest();
        s.permissions = {OriginPrompt::Permission::Notifications};
        p.Show(s, anchor, [&](OriginPrompt::Result& r) { ++calls; got = r; });
        p.Surface().KeyForTest(VK_ESCAPE);
        Check(calls == 2 && got.action == OriginPrompt::Action::Dismiss && !got.remember, "permission: Échap = Dismiss, nothing remembered");
        p.Surface().FinishAnimationsForTest();
        p.Show(s, anchor, [&](OriginPrompt::Result& r) { ++calls; got = r; });
        p.Surface().ActivateForTest(Modal::Dialog::ButtonHit(0));
        Check(calls == 3 && got.action == OriginPrompt::Action::Block && got.remember, "permission: notifications choice always remembered (B02)");
        p.Surface().FinishAnimationsForTest();
    }
    {   // Permission popovers as drawn in Paper B02 (user edit 2026-11-12): head (glyph, title, subtitle), the
        // remember check by kind, Bloquer / Autoriser, Fermer. No collapsible help, no extra note.
        using OriginPrompt::Permission::Notifications; namespace OPP = OriginPrompt::Permission; using D = Modal::Dialog;
        auto spec = [](std::vector<int> kinds, std::wstring origin = L"x.com") {
            OriginPrompt::Spec s; s.kind = OriginPrompt::Kind::Permission; s.origin = origin; s.permissions = std::move(kinds); return s; };
        auto has = [](const std::wstring& text, const wchar_t* part) { return text.find(part) != std::wstring::npos; };
        // Kind 13 is the SDK's COREWEBVIEW2_PERMISSION_KIND_PERSISTENT_STORAGE (the x.com capture), never « notifications ».
        Check(OPP::PersistentStorage == 13 && SiteRequest::Kind::PersistentStorage == 13, "permission kinds: 13 = persistent storage (SDK 1.0.4314.35)");
        auto st = OriginPrompt::Build(spec({OPP::PersistentStorage}), true);
        Check(st.title == L"x.com veut protéger son stockage local" && st.headGlyph == DialogKit::Glyph::storage && SiteRequest::GlyphOf(13) == SiteRequest::Path::storage, "permission kinds: 13 has its own title and storage glyph");
        Check(st.subtitle == L"Protège les données locales du site quand le navigateur libère de l'espace. Les mots de passe sont gérés séparément." && !has(st.title, L"notification") && !has(st.subtitle, L"otification"), "permission kinds: 13 never described as notifications");
        Check(st.checkLabel == L"Mémoriser pour ce site" && st.notices.empty(), "permission kinds: 13 keeps the remember check (policy unchanged)");
        auto unknown = OriginPrompt::Build(spec({OPP::Unknown}, L"exemple.fr"), true);
        Check(unknown.title == L"exemple.fr demande une autorisation" && unknown.subtitle == L"Le moteur ne précise pas de quoi il s'agit." && unknown.headGlyph == DialogKit::Glyph::info, "permission kinds: 0 says the engine gave no type");
        auto future = OriginPrompt::Build(spec({14}), true);
        Check(future.title == L"x.com demande une autorisation" && future.subtitle == L"Type non reconnu par ce navigateur (code moteur 14).", "permission kinds: a future kind is named by its engine code, not guessed");
        auto notif = OriginPrompt::Build(spec({Notifications}, L"lemonde.fr"), true);
        Check(notif.title == L"lemonde.fr veut vous envoyer des notifications" && notif.subtitle == L"Le site pourra afficher des alertes en dehors de la page." && !has(notif.subtitle, L"onglet fermé"), "notifications: Paper title and subtitle, no unproven promise");
        Check(notif.notices.empty() && notif.checkLabel.empty(), "notifications: no note and no check (Paper B02)");
        for (int k = 0; k <= 14; ++k) { if (OriginPrompt::Build(spec({k}), true).subtitle.empty()) { Check(false, "permission kinds: every kind has a subtitle"); break; } if (k == 14) Check(true, "permission kinds: every kind has a subtitle"); }

        // What the window exposes: no « Plus d'infos » button, no text block, nothing that scrolls.
        RECT anchor{200, 150, 224, 174}; int calls = 0; OriginPrompt::Result got; auto& S = p.Surface();
        auto exposed = [&](std::vector<std::wstring>& names, bool& extra) {
            names.clear(); extra = false;
            for (auto& n : S.NodesForTest()) { if (n.key == 1) continue; names.push_back(n.name); if (n.role == Accessibility::Role::Text || n.expand != 0 || has(n.name, L"Plus d'infos") || has(n.automationId, L"More")) extra = true; }
        };
        std::vector<std::wstring> names; bool extra = false;
        p.Show(spec({OPP::PersistentStorage}), anchor, [&](OriginPrompt::Result& r) { ++calls; got = r; }); S.FinishAnimationsForTest();
        exposed(names, extra);
        Check(!extra && names == std::vector<std::wstring>{L"Mémoriser pour ce site", L"Bloquer", L"Autoriser", L"Fermer"}, "storage popover: check, Bloquer, Autoriser, Fermer; no collapsible help");
        Check(S.FocusForTest() == D::ButtonHit(0), "storage popover: initial focus on Bloquer (unchanged)");
        S.KeyForTest(VK_TAB); Check(S.FocusForTest() == D::ButtonHit(1), "storage popover: Tab → Autoriser");
        S.KeyForTest(VK_TAB); Check(S.FocusForTest() == D::CloseHit, "storage popover: Tab → Fermer");
        S.KeyForTest(VK_TAB); Check(S.FocusForTest() == D::SimpleCheckHit, "storage popover: Tab → Mémoriser (wraps)");
        { const float before = S.HeightForTest(); SendMessageW(p.Window(), WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)), 0); S.KeyForTest(VK_DOWN); Check(S.HeightForTest() == before && calls == 0, "storage popover: wheel / ↓ change nothing, nothing answered"); }
        for (UINT dpi : {96u, 144u, 192u}) Check(S.RenderForTest(root + L"/build/popups/perm-storage-" + std::to_wstring(dpi) + L".png", dpi), "storage popover render");
        S.KeyForTest(VK_ESCAPE); Check(calls == 1 && got.action == OriginPrompt::Action::Dismiss && !got.remember, "storage popover: Échap = Dismiss once, nothing remembered");
        S.KeyForTest(VK_ESCAPE); S.FinishAnimationsForTest(); Check(calls == 1, "storage popover: no second answer");

        p.Show(spec({Notifications}, L"lemonde.fr"), anchor, [&](OriginPrompt::Result& r) { ++calls; got = r; }); S.FinishAnimationsForTest();
        exposed(names, extra);
        Check(!extra && names == std::vector<std::wstring>{L"Bloquer", L"Autoriser", L"Fermer"}, "notifications popover: title + subtitle + buttons only (Paper B02)");
        {   std::wstring help; for (auto& n : S.NodesForTest()) if (n.key == 1) help = n.help;
            Check(help == L"Le site pourra afficher des alertes en dehors de la page.", "notifications popover: UIA group help is the subtitle"); }
        { const auto block = S.BoxForTest(D::ButtonHit(0)); Check(S.HeightForTest() - block.bottom == 16, "notifications popover: buttons end the popover"); }
        for (UINT dpi : {96u, 144u, 192u}) Check(S.RenderForTest(root + L"/build/popups/perm-notifications-" + std::to_wstring(dpi) + L".png", dpi), "notifications popover render");
        S.ActivateForTest(D::ButtonHit(0)); Check(calls == 2 && got.action == OriginPrompt::Action::Block && got.remember, "notifications popover: Bloquer still always remembered");
        S.FinishAnimationsForTest();

        p.Show(spec({OPP::Camera, OPP::Microphone, OPP::Geolocation}, L"rendez-vous.doctolib.fr"), anchor, [&](OriginPrompt::Result& r) { ++calls; got = r; }); S.FinishAnimationsForTest();
        exposed(names, extra);
        Check(!extra && names == std::vector<std::wstring>{L"Mémoriser pour ce site", L"Bloquer", L"Autoriser", L"Fermer"}, "grouped popover: kind rows, check, buttons; no collapsible help");
        Check(S.RenderForTest(root + L"/build/popups/perm-grouped-144.png", 144), "grouped popover render");
        p.Abort(); S.FinishAnimationsForTest(); Check(calls == 3 && got.action == OriginPrompt::Action::Dismiss, "grouped popover: Abort answers once");

        p.Show(spec({OPP::PersistentStorage}, L"un-sous-domaine-vraiment-tres-long.exemple-de-domaine-public-pour-tester.fr"), anchor, [&](OriginPrompt::Result& r) { ++calls; got = r; }); S.FinishAnimationsForTest();
        Check(S.RenderForTest(root + L"/build/popups/perm-storage-long-192.png", 192), "long origin render (title wraps, nothing cut)");
        p.Abort(); S.FinishAnimationsForTest();
        p.Show(spec({OPP::Unknown}, L"exemple.fr"), anchor, [&](OriginPrompt::Result& r) { ++calls; got = r; }); S.FinishAnimationsForTest();
        Check(S.RenderForTest(root + L"/build/popups/perm-unknown-144.png", 144), "unknown kind render");
        p.Abort(); S.FinishAnimationsForTest(); Check(calls == 5, "every request answered exactly once");
    }
    {   // B06 HTTP auth: Submit returns the credentials once; the EDIT buffer is wiped; refusal.
        OriginPrompt::Spec s; s.kind = OriginPrompt::Kind::BasicAuth; s.origin = L"imprimante-bureau"; s.user = L"admin"; s.insecure = true;
        int calls = 0; std::wstring user, password; bool wipedAfter = false; OriginPrompt::Result* seen = nullptr;
        p.SetPageArea(Area());
        p.Show(s, {}, [&](OriginPrompt::Result& r) { ++calls; user = r.user; password = r.password; seen = &r; });
        Check(p.Surface().FocusForTest() == Modal::Dialog::FieldStop(1), "auth: focus on the password (identifier pre-filled)");
        Check(GetWindowLongPtrW(p.Surface().EditForTest(1), GWL_STYLE) & ES_PASSWORD, "auth: password field is ES_PASSWORD");
        p.AuthRefused(); Check(p.Visible(), "auth: refusal keeps the window with the error");
        p.Surface().SetFieldForTest(1, L"s3cret");
        HWND edit = p.Surface().EditForTest(1);
        p.Surface().KeyForTest(VK_RETURN);
        wipedAfter = GetWindowTextLengthW(edit) == 0;
        Check(calls == 1 && user == L"admin" && password == L"s3cret", "auth: Submit with identifier and password");
        Check(wipedAfter, "auth: password EDIT wiped on close");
        (void)seen;
        p.Surface().FinishAnimationsForTest();
    }
    {   // B06 external app from a third-party frame: Annuler is the default, no remembering.
        OriginPrompt::Spec s; s.kind = OriginPrompt::Kind::ExternalApp; s.origin = L"forum-velo.fr"; s.frame = L"pub-media.net"; s.detail = L"ms-settings:privacy";
        auto m = OriginPrompt::Build(s, true);
        Check(m.checkLabel.empty() && m.buttons[0].isDefault && m.buttons[0].label == L"Annuler", "external app (third party): Annuler default, no remember");
        s.frame.clear(); s.app = L"Zoom Workplace"; s.detail = L"zoommtg://zoom.us/join";
        m = OriginPrompt::Build(s, true);
        Check(m.title == L"Ouvrir Zoom Workplace ?" && m.checkLabel == L"Toujours autoriser ce site à ouvrir les liens zoommtg" && m.buttons[1].label == L"Ouvrir Zoom", "external app (known): Paper text");
        int calls = 0; OriginPrompt::Result got;
        p.Show(s, {}, [&](OriginPrompt::Result& r) { ++calls; got = r; });
        p.Surface().KeyForTest(VK_RETURN);
        Check(calls == 1 && got.action == OriginPrompt::Action::Open, "external app: Entrée opens");
        p.Surface().FinishAnimationsForTest();
    }
    {   // B03 fill (fictitious accounts): ↓ then Entrée picks the 2nd account.
        OriginPrompt::Spec s; s.kind = OriginPrompt::Kind::PasswordFill; s.origin = L"github.com"; s.demo = true;
        s.accounts = {{L"valxxii", L"exemple-un"}, {L"atelier-lumen-bot", L"exemple-deux"}};
        int calls = 0; OriginPrompt::Result got;
        p.Show(s, {200, 150, 224, 174}, [&](OriginPrompt::Result& r) { ++calls; got = r; });
        p.Surface().KeyForTest(VK_DOWN); p.Surface().KeyForTest(VK_RETURN);
        Check(calls == 1 && got.action == OriginPrompt::Action::Fill && got.choice == 1 && got.user == L"atelier-lumen-bot" && got.password.empty(), "password fill: ↓ Entrée, no secret returned");
        p.Surface().FinishAnimationsForTest();
        Check(OriginPrompt::Build(s, true).demo, "password fill: Démonstration badge");
    }
    {   // B04 REAL path (vault inactive until review): the submitted secret pre-filled, no « Jamais » without a real list,
        // and D1: several accounts and no identifier => explicit choice, « Mettre à jour » disabled until then.
        OriginPrompt::Spec r; r.kind = OriginPrompt::Kind::PasswordUpdate; r.origin = L"compte.exemple.test"; r.user = L""; r.secret = L"Fictif-nouveau-2026";
        r.accounts = {{L"camille@exemple.test", L""}, {L"travail@exemple.test", L""}};
        const auto m = OriginPrompt::Build(r, true);
        bool never = false; for (auto& b0 : m.buttons) never = never || b0.label == L"Jamais pour ce site";
        Check(!m.demo && !never && m.accountsSelect && m.accounts.size() == 2 && m.buttons.back().disabled && m.fields.size() == 1 && m.fields[0].value == L"Fictif-nouveau-2026", "B04 real: D1 choice required, secret pre-filled, no inert « Jamais »");
        Modal::Result pick; pick.button = OriginPrompt::Id::Accept; pick.select = 1; pick.values = {L"Fictif-nouveau-2026"};
        const auto out = OriginPrompt::Map(r, pick);
        Check(out.action == OriginPrompt::Action::Save && out.user == L"travail@exemple.test" && out.password == L"Fictif-nouveau-2026", "B04 real: the chosen account and the (possibly edited) password come back for the vault");
        // Owner decision B: a password the ENGINE had saved (autofilled) is offered as a migration, same B04 component.
        { OriginPrompt::Spec mg; mg.kind = OriginPrompt::Kind::PasswordSave; mg.migration = true; mg.origin = L"compte.exemple.test"; mg.user = L"camille@exemple.test"; mg.secret = L"Fictif-moteur-2026";
          const auto mm = OriginPrompt::Build(mg, true);
          Check(mm.automationId == L"PasswordKeep" && mm.title == L"Garder ce mot de passe dans browser ?" && mm.buttons.back().label == L"Garder" && mm.buttons.back().automationId == L"PasswordSaveConfirm" && mm.armMs == 500 && mm.fields.size() == 2 && mm.fields[1].value == L"Fictif-moteur-2026",
                "B04 migration: « Garder ce mot de passe dans browser ? », « Garder », armed, the engine's password pre-filled");
          Modal::Result keep; keep.button = OriginPrompt::Id::Accept; keep.values = {L"camille@exemple.test", L"Fictif-moteur-2026"};
          const auto ko = OriginPrompt::Map(mg, keep); Check(ko.action == OriginPrompt::Action::Save && ko.user == L"camille@exemple.test" && ko.password == L"Fictif-moteur-2026", "B04 migration: « Garder » saves like « Enregistrer »");
          I18n::SetForProcess(I18n::Locale::En); const auto me = OriginPrompt::Build(mg, true); I18n::SetForProcess(I18n::Locale::Fr);
          Check(me.title == L"Keep this password in browser?" && me.buttons.back().label == L"Keep", "B04 migration in English: « Keep this password in browser? », « Keep »"); }
        // Review F3/V5: credential prompts are armed (500 ms, foreground) and an update has no default Entrée.
        Check(m.armMs == 500 && !m.buttons.back().isDefault, "B04 real update: armed 500 ms, no default Entrée");
        { OriginPrompt::Spec sv = r; sv.kind = OriginPrompt::Kind::PasswordSave; sv.user = L"camille@exemple.test"; sv.accounts.clear();
          const auto ms = OriginPrompt::Build(sv, true); Modal::Dialog dd; dd.Create(hi, owner, root, {}); int closed = 0;
          dd.Show(ms, Area(), [&](const Modal::Result&) { ++closed; });
          dd.ActivateForTest(Modal::Dialog::ButtonHit(1));
          Check(!dd.ArmedForTest() && closed == 0 && dd.Visible(), "B04 real: a click right after the prompt appears does nothing (armed after 500 ms)");
          dd.ArmNowForTest(true); dd.FieldKeyMessageForTest(1, VK_RETURN, true);
          Check(closed == 0 && dd.Visible(), "B04 real: a held Entrée in a field (auto-repeat) never saves (review R4)");
          dd.Abort(); dd.FinishAnimationsForTest(); dd.Dispose(); }
        { // Review N2: « Mettre à jour » (not the default button) still validates: an emptied identifier is refused.
          OriginPrompt::Spec su = r; su.user = L"camille@exemple.test"; su.accounts = {{L"camille@exemple.test", L""}};
          const auto mu = OriginPrompt::Build(su, true); Modal::Dialog dd; dd.Create(hi, owner, root, {}); int closed = 0;
          dd.Show(mu, Area(), [&](const Modal::Result&) { ++closed; }); dd.ArmNowForTest(true);
          dd.SetFieldForTest(0, L""); dd.ActivateForTest(Modal::Dialog::ButtonHit(1));
          Check(dd.ArmedForTest() && closed == 0 && dd.Visible(), "B04 real update: an emptied identifier is refused by « Mettre à jour » (no entry without identifier)");
          dd.Abort(); dd.FinishAnimationsForTest(); dd.Dispose(); }
        { // Review N1: the B03 rows and « Gérer » answer only once armed; a held Entrée never picks an account.
          OriginPrompt::Spec f; f.kind = OriginPrompt::Kind::PasswordFill; f.origin = L"compte.exemple.test"; f.accounts = {{L"camille@exemple.test", L"x"}, {L"travail@exemple.test", L"x"}};
          const auto mf = OriginPrompt::Build(f, true); Modal::Dialog dd; dd.Create(hi, owner, root, {}); int closed = 0;
          dd.Show(mf, Area(), [&](const Modal::Result&) { ++closed; });
          dd.ActivateForTest(Modal::Dialog::AccountHit(0)); dd.ActivateForTest(Modal::Dialog::ManageHit);
          Check(mf.armMs == 500 && closed == 0 && dd.Visible(), "B03 real: choosing a row or « Gérer » right after the popover appears does nothing");
          dd.ArmNowForTest(true); dd.KeyMessageForTest(VK_RETURN, true);
          Check(closed == 0 && dd.Visible(), "B03 real: an auto-repeated Entrée (key held from the trigger) never picks an account");
          dd.ActivateForTest(Modal::Dialog::AccountHit(1));
          Check(closed == 1, "B03 real: once armed, an explicit choice answers");
          dd.FinishAnimationsForTest(); dd.Dispose(); }
        Modal::Result none; none.button = OriginPrompt::Id::Accept; none.select = -1; none.values = {L"x"};
        Check(OriginPrompt::Map(r, none).action != OriginPrompt::Action::Save, "B04 real: no account chosen => nothing saved (never the last one silently)");
    }
    {   // B05 real mode (E8): no decrypted secret, no reveal, real empty state, honest removal failure; back to demo for the sandbox.
        NativeChrome::PasswordDemo::Window w; Check(w.Create(hi, owner, root), "B05 real: surface created");
        int removeCalls = 0; std::wstring said; NativeChrome::PasswordDemo::Window w2; NativeChrome::PasswordDemo::Demo::Hooks hk; hk.announce = [&](const std::wstring& t0) { said = t0; };
        Check(w2.Create(hi, owner, root, hk), "B05 real: surface with announce");
        w2.SetReal({{L"compte.exemple.test", L"camille@exemple.test", L"NE-DOIT-PAS-APPARAITRE", L"", L"Modifié le 12 novembre 2026", 7}}, [&](std::uint64_t) { ++removeCalls; return false; });
        Check(w2.Real() && w2.Accounts().size() == 1 && w2.Accounts()[0].password.empty(), "B05 real: accounts carry no secret");
        Check(w2.Show({0, 0, 900, 700}), "B05 real: shown");
        Check(!w2.Surface().CurrentSpec().demo && w2.Surface().CurrentSpec().subtitle.find(L"restent proposés par le moteur") != std::wstring::npos, "B05 real: no demo badge, cohabitation note");
        w2.Surface().ActivateForTest(Modal::Dialog::AccountHit(0));
        bool reveal = false; std::wstring shown; for (auto& f : w2.Surface().CurrentSpec().fields) if (f.password) { reveal = f.reveal; shown = f.value; }
        Check(!reveal && shown.find(L"NE-DOIT") == std::wstring::npos, "B05 real: detail never shows or reveals a secret");
        w2.Surface().ActivateForTest(Modal::Dialog::ButtonHit(0)); w2.Surface().ActivateForTest(Modal::Dialog::ButtonHit(1));
        Check(removeCalls == 1 && w2.Accounts().size() == 1 && said.find(L"n'a pas pu être supprimé") != std::wstring::npos, "B05 real: a removal the vault did not write is said, the row stays");
        w2.Hide(); w2.Surface().FinishAnimationsForTest();
        w2.SetReal({}, nullptr); Check(w2.Show({0, 0, 900, 700}) && w2.Surface().CurrentSpec().accountsTitle == L"Aucun mot de passe enregistré par browser", "B05 real: empty state"); w2.Hide(); w2.Surface().FinishAnimationsForTest();
        {   // Owner decision « option A »: the B05 list offers deleting the engine's old passwords; the button asks the host;
            // after the deletion the list says so and the offer is gone. Renders: build/popups/vault-engine-*.png.
            int asked = 0; w2.SetReal({{L"compte.exemple.test", L"camille@exemple.test", L"", L"", L"Modifié le 12 novembre 2026", 7}}, [](std::uint64_t) { return true; });
            w2.SetEngineOffer(true, [&] { ++asked; }); Check(w2.Show({0, 0, 900, 700}), "option A: B05 list with the offer shown");
            const auto& ls = w2.Surface().CurrentSpec(); int offerAt = -1; for (size_t i = 0; i < ls.buttons.size(); ++i) if (ls.buttons[i].automationId == L"VaultEngineOffer") offerAt = static_cast<int>(i);
            Check(offerAt >= 0 && ls.buttons[static_cast<size_t>(offerAt)].label == L"Supprimer les anciens mots de passe…" && ls.buttons[static_cast<size_t>(offerAt)].role == Modal::Role::Danger && !ls.notices.empty() && ls.notices[0].text.find(L"Edge WebView2") != std::wstring::npos,
                  "option A: B05 list: the info line and « Supprimer les anciens mots de passe… » (destructive, leading)");
            w2.Surface().FinishAnimationsForTest(); Check(w2.Surface().RenderForTest(root + L"/build/popups/vault-engine-offer-fr.png"), "option A: render B05 list with the offer (FR)");
            if (offerAt >= 0) { w2.Surface().ArmNowForTest(); w2.Surface().ActivateForTest(Modal::Dialog::ButtonHit(offerAt)); }
            Check(asked == 1 && w2.Visible(), "option A: the button asks the host (the confirmation), the list stays");
            w2.EngineDeleted(); const auto& ds = w2.Surface().CurrentSpec(); bool still = false; for (auto& b : ds.buttons) still = still || b.automationId == L"VaultEngineOffer";
            Check(!still && !ds.notices.empty() && ds.notices[0].text == L"Anciens mots de passe supprimés." && ds.subtitle.find(L"moteur") == std::wstring::npos, "option A: after the deletion the list says so, the offer and the engine note are gone");
            w2.Surface().FinishAnimationsForTest(); Check(w2.Surface().RenderForTest(root + L"/build/popups/vault-engine-deleted-fr.png"), "option A: render B05 list after the deletion (FR)");
            w2.Hide(); w2.Surface().FinishAnimationsForTest();
            I18n::SetForProcess(I18n::Locale::En); w2.SetEngineOffer(true, [] {}); w2.Show({0, 0, 900, 700}); w2.Surface().FinishAnimationsForTest();
            Check(w2.Surface().RenderForTest(root + L"/build/popups/vault-engine-offer-en.png"), "option A: render B05 list with the offer (EN)"); w2.Hide(); w2.Surface().FinishAnimationsForTest(); I18n::SetForProcess(I18n::Locale::Fr);
            // The confirmation: A5 vocabulary (ECR-0 without period and categories), « Annuler » first and focused.
            const auto cs = Modal::DeleteEnginePasswordsSpec();
            Check(cs.title == L"Supprimer les anciens mots de passe" && cs.buttons.size() == 2 && cs.buttons[0].label == L"Annuler" && cs.buttons[0].initialFocus && cs.buttons[1].role == Modal::Role::Danger && cs.buttons[1].label == cs.title && !cs.buttons[1].isDefault && cs.body.find(L"irréversible") != std::wstring::npos,
                  "option A: confirmation « Supprimer les anciens mots de passe », Annuler focused, destructive action never the default Entrée");
            Modal::Dialog cd; cd.Create(hi, owner, root, {}); auto spec = cs; spec.buttons[1].closes = false; int closed = 0;
            cd.Show(spec, Area(), [&](const Modal::Result&) { ++closed; }); cd.FinishAnimationsForTest();
            Check(cd.RenderForTest(root + L"/build/popups/vault-engine-confirm-fr.png"), "option A: render the confirmation (FR)");
            cd.SetBusy(true, L"Suppression…"); Check(cd.RenderForTest(root + L"/build/popups/vault-engine-confirm-busy-fr.png"), "option A: render the confirmation while deleting");
            cd.SetBusy(false); cd.SetError(L"La suppression n'a pas abouti (0x80004005). Réessayez."); Check(cd.RenderForTest(root + L"/build/popups/vault-engine-confirm-failed-fr.png") && closed == 0, "option A: render the failure (the dialog stays)");
            cd.Abort(); cd.FinishAnimationsForTest(); cd.Dispose();
            I18n::SetForProcess(I18n::Locale::En); const auto ce = Modal::DeleteEnginePasswordsSpec(); Modal::Dialog ed; ed.Create(hi, owner, root, {}); ed.Show(ce, Area(), [](const Modal::Result&) {}); ed.FinishAnimationsForTest();
            Check(ce.title == L"Delete old passwords" && ed.RenderForTest(root + L"/build/popups/vault-engine-confirm-en.png"), "option A: confirmation in English, rendered"); ed.Abort(); ed.FinishAnimationsForTest(); ed.Dispose(); I18n::SetForProcess(I18n::Locale::Fr);
        }
        {   // Review F1: removals act on each row's own vault id, whatever the rows' order after earlier removals.
            std::vector<std::uint64_t> removed; w2.SetReal({{L"a.test", L"A", L"", L"", L"", 11}, {L"b.test", L"B", L"", L"", L"", 22}, {L"c.test", L"C", L"", L"", L"", 33}}, [&](std::uint64_t id) { removed.push_back(id); return true; });
            w2.Show({0, 0, 900, 700}); w2.Surface().ArmNowForTest();
            auto removeRow = [&](int row) { w2.Surface().ActivateForTest(Modal::Dialog::AccountHit(row)); w2.Surface().ActivateForTest(Modal::Dialog::ButtonHit(0)); w2.Surface().ActivateForTest(Modal::Dialog::ButtonHit(1)); };
            removeRow(0); removeRow(1);   // A, then the row now second: C
            Check(removed == std::vector<std::uint64_t>{11, 33} && w2.Accounts().size() == 1 && w2.Accounts()[0].user == L"B", "F1: removing A then C deletes exactly A and C (ids), B stays");
            w2.Hide(); w2.Surface().FinishAnimationsForTest();
        }
        w2.SetDemo(); Check(!w2.Real() && w2.Accounts().size() == NativeChrome::PasswordDemo::SampleAccounts().size(), "B05: back to the fictitious demonstration for the sandbox");
        w.Dispose(); w2.Dispose();
    }
    {   // B04 save (DEMONSTRATION surface): identifier required; « Jamais pour ce site ».
        OriginPrompt::Spec s; s.kind = OriginPrompt::Kind::PasswordSave; s.origin = L"github.com"; s.user = L""; s.demo = true;
        int calls = 0; OriginPrompt::Result got;
        p.Show(s, {200, 150, 224, 174}, [&](OriginPrompt::Result& r) { ++calls; got = r; });
        p.Surface().ActivateForTest(Modal::Dialog::ButtonHit(2));
        Check(calls == 0 && !p.Surface().FieldErrorForTest().empty(), "password save: missing identifier is invalid (B04)");
        p.Surface().ActivateForTest(Modal::Dialog::ButtonHit(0));
        Check(calls == 1 && got.action == OriginPrompt::Action::Never, "password save: Jamais pour ce site");
        p.Surface().FinishAnimationsForTest();
    }
    {   // B07 dangerous download: « Plus d'options » never answers by itself (regression: it used to keep the file).
        OriginPrompt::Spec s; s.kind = OriginPrompt::Kind::DangerousDownload; s.origin = L"pilotes.exemple"; s.detail = L"HP_M110_Setup_v3.2.1.exe";
        Modal::Result more; more.button = OriginPrompt::Id::More;
        Check(OriginPrompt::Map(s, more).action == OriginPrompt::Action::Dismiss, "dangerous download: More alone maps to no decision");
    }
    {   // Certificate issuer line is plain text (tracking 0); password dots keep Paper's 0.08em.
        OriginPrompt::Spec c; c.kind = OriginPrompt::Kind::ClientCertificate; c.origin = L"banque.exemple.test"; c.accounts = {{L"Camille Démo", L"Émis par Autorité Démo · expire en 2027"}};
        const auto mc = OriginPrompt::Build(c, true);
        Check(mc.accounts.size() == 1 && mc.accounts[0].plain && Modal::AccountHintStyle(mc.accounts[0]).trackingEm == 0, "certificate: issuer line not letter-spaced");
        OriginPrompt::Spec f; f.kind = OriginPrompt::Kind::PasswordFill; f.origin = L"compte.exemple.test"; f.accounts = {{L"camille.demo@exemple.test", L"Utilisé il y a 2 jours"}};
        const auto mf = OriginPrompt::Build(f, true);
        Check(mf.accounts.size() == 1 && !mf.accounts[0].plain && Modal::AccountHintStyle(mf.accounts[0]).trackingEm > .07f, "password fill: dots keep 0.08em");
    }
    p.Dispose();
    // ---- B07 « Plus d'options » with a scripted host menu --------------------------------------------
    {
        OriginPrompt::Prompt q; int nextMenu = 0, menus = 0, dismissed = 0, copied = 0, reported = 0; std::vector<ContextMenu::Item> seen; RECT menuAnchor{};
        std::function<void()> duringMenu;
        OriginPrompt::Prompt::Hooks h;
        h.menu = [&](const std::vector<ContextMenu::Item>& items, RECT anchor, bool) { ++menus; seen = items; menuAnchor = anchor; if (duringMenu) duringMenu(); return nextMenu; };
        h.dismiss = [&] { ++dismissed; };
        h.download = [&](OriginPrompt::Prompt::Command c, const OriginPrompt::Spec&) { if (c == OriginPrompt::Prompt::CopyLink) ++copied; else if (c == OriginPrompt::Prompt::Report) ++reported; };
        Check(q.Create(hi, owner, root, std::move(h)), "B07 prompt with host menu");
        OriginPrompt::Spec s; s.kind = OriginPrompt::Kind::DangerousDownload; s.origin = L"telechargements.exemple.test"; s.detail = L"facture-2026.pdf.exe"; s.demo = true;
        int calls = 0; OriginPrompt::Result got;
        auto show = [&] { calls = 0; got = {}; q.Show(s, {900, 150, 924, 174}, [&](OriginPrompt::Result& r) { ++calls; got = r; }); q.Surface().FinishAnimationsForTest(); };
        show();
        nextMenu = 0; q.Surface().ActivateForTest(Modal::Dialog::ButtonHit(1));
        Check(menus == 1 && seen.size() == 4 && seen[2].separator && seen[3].danger && seen[0].label == L"Copier le lien du fichier" && seen[3].label == L"Conserver quand même…", "B07: More opens the Paper FYS-0 menu (2 rows, separator, danger row)");
        const auto more = q.Surface().BoxForTest(Modal::Dialog::ButtonHit(1)); (void)more;
        Check(menuAnchor.right > menuAnchor.left && menuAnchor.bottom > menuAnchor.top, "B07: menu anchored to the « Plus d'options » button");
        Check(calls == 0 && q.Visible() && q.Current().kind == OriginPrompt::Kind::DangerousDownload, "B07: Échap / click outside the menu keeps the popover, no answer");
        nextMenu = OriginPrompt::Prompt::CopyLink; q.Surface().ActivateForTest(Modal::Dialog::ButtonHit(1));
        nextMenu = OriginPrompt::Prompt::Report; q.Surface().ActivateForTest(Modal::Dialog::ButtonHit(1));
        Check(copied == 1 && reported == 1 && calls == 0 && q.Visible(), "B07: Copier le lien / Signaler run and keep the popover open");
        nextMenu = OriginPrompt::Prompt::KeepAnyway; q.Surface().ActivateForTest(Modal::Dialog::ButtonHit(1));
        Check(calls == 0 && q.ConfirmingForTest() && q.Surface().CurrentSpec().automationId == L"KeepDangerousFile" && q.Surface().CurrentSpec().scope == Modal::Scope::Anchor, "B08: Conserver quand même… opens the confirmation in the popover");
        Check(q.Surface().FocusForTest() == Modal::Dialog::ButtonHit(0), "B08: « Annuler » is the default focus");
        q.Surface().ActivateForTest(Modal::Dialog::ButtonHit(0));
        Check(calls == 0 && !q.ConfirmingForTest() && q.Current().kind == OriginPrompt::Kind::DangerousDownload && q.Visible(), "B08: Annuler returns to the blocked download, nothing kept");
        nextMenu = OriginPrompt::Prompt::KeepAnyway; q.Surface().ActivateForTest(Modal::Dialog::ButtonHit(1));
        q.Surface().KeyForTest(VK_ESCAPE);
        Check(calls == 1 && got.action != OriginPrompt::Action::Keep, "B08: Échap in the confirmation never keeps the file");
        q.Surface().FinishAnimationsForTest();
        show(); nextMenu = OriginPrompt::Prompt::KeepAnyway; q.Surface().ActivateForTest(Modal::Dialog::ButtonHit(1));
        q.Surface().ActivateForTest(Modal::Dialog::ButtonHit(1));
        Check(calls == 1 && got.action == OriginPrompt::Action::Keep, "B08: only « Conserver » keeps (simulated result)");
        q.Surface().FinishAnimationsForTest();
        show(); const int before = dismissed; nextMenu = OriginPrompt::Prompt::KeepAnyway; duringMenu = [&] { q.Abort(); };
        q.Surface().ActivateForTest(Modal::Dialog::ButtonHit(1)); duringMenu = nullptr;
        Check(calls == 1 && got.action == OriginPrompt::Action::Dismiss && dismissed == before + 1 && !q.ConfirmingForTest(), "B07: popover aborted while its menu is open: menu dismissed, the late choice dropped");
        q.Surface().FinishAnimationsForTest();
        q.Dispose();
    }
    // ---- B08 screen share picker (demo thumbnails) ---------------------------------------------------
    {
        OriginPrompt::Prompt q; Check(q.Create(hi, owner, root, {}), "B08 share prompt");
        OriginPrompt::Spec s; s.kind = OriginPrompt::Kind::ScreenShare; s.origin = L"meet.exemple.test"; s.demo = true; s.sources = {L"Onglets", L"Fenêtres", L"Écrans"};
        s.shareSources = {{L"Ordre du jour", 0, 0}, {L"Visio", 0, 5}, {L"Budget", 1, 3}, {L"Notes", 1, 1}, {L"Navigateur", 1, 4}, {L"Écran 1", 2, 2}};
        int calls = 0; OriginPrompt::Result got;
        q.SetPageArea({100, 100, 1300, 900});
        auto show = [&] { calls = 0; got = {}; q.Show(s, {}, [&](OriginPrompt::Result& r) { ++calls; got = r; }); q.Surface().FinishAnimationsForTest(); };
        show(); auto& d = q.Surface();
        Check(d.WidthForTest() == 640 && d.ColumnsForTest() == 3, "share: larger picker (640 DIP, 3 columns) on a wide page");
        Check(!d.EnabledForTest(Modal::Dialog::ButtonHit(1)) && d.SourcesForTest().selected < 0, "share: « Partager » disabled until a source is selected");
        Check(d.FocusForTest() == Modal::Dialog::SourceHit(0), "share: focus starts on the first thumbnail");
        const auto t0 = d.BoxForTest(Modal::Dialog::SourceHit(0)), t1 = d.BoxForTest(Modal::Dialog::SourceHit(1));
        Check(t0.right <= t1.left - 8 + .5f && t0.top == t1.top && t0.bottom - t0.top > 80, "share: thumbnails side by side, 8 apart");
        d.KeyForTest(VK_RETURN);
        Check(d.SourcesForTest().selected == 0 && d.EnabledForTest(Modal::Dialog::ButtonHit(1)) && calls == 0, "share: Entrée selects (does not share yet)");
        d.KeyForTest(VK_RIGHT);
        Check(d.SourcesForTest().selected == 1 && d.FocusForTest() == Modal::Dialog::SourceHit(1), "share: → moves the selection");
        d.ActivateForTest(Modal::Dialog::CategoryHit(1));
        Check(d.SourcesForTest().category == 1 && d.SourcesForTest().selected < 0 && !d.EnabledForTest(Modal::Dialog::ButtonHit(1)), "share: another category clears the choice");
        Check(d.BoxForTest(Modal::Dialog::SourceHit(2)).right > 0 && d.BoxForTest(Modal::Dialog::SourceHit(0)).right == 0, "share: only the category's sources are shown");
        d.ActivateForTest(Modal::Dialog::SourceHit(3));
        d.PaintForTest();
        d.ActivateForTest(Modal::Dialog::ButtonHit(1));
        Check(calls == 1 && got.action == OriginPrompt::Action::Choose && got.choice == 3, "share: Partager returns the selected source (demo)");
        d.FinishAnimationsForTest();
        show(); q.Surface().KeyForTest(VK_ESCAPE);
        Check(calls == 1 && got.action == OriginPrompt::Action::Cancel, "share: Échap cancels");
        q.Surface().FinishAnimationsForTest();
        auto none = s; none.shareSources.clear();
        calls = 0; q.Show(none, {}, [&](OriginPrompt::Result& r) { ++calls; got = r; }); q.Surface().FinishAnimationsForTest();
        Check(!q.Surface().EnabledForTest(Modal::Dialog::ButtonHit(1)) && q.Surface().FocusForTest() == Modal::Dialog::CategoryHit(0), "share: no source: Partager disabled, focus on the categories");
        q.Surface().PaintForTest(); q.Surface().ActivateForTest(Modal::Dialog::ButtonHit(1));
        Check(calls == 0, "share: disabled Partager does nothing");
        q.Abort(); q.Surface().FinishAnimationsForTest();
        q.SetPageArea({100, 100, 560, 520});
        show();
        Check(q.Surface().WidthForTest() <= 460 - 32 + .5f && q.Surface().ColumnsForTest() == 2 && q.Surface().HeightForTest() <= 420 - 16 - 32 + 1, "share: narrow/short page: 2 columns, smaller thumbnails, fits");
        q.Abort(); q.Surface().FinishAnimationsForTest();
        q.Dispose();
    }
    // ---- B01 address-bar request chip model ----------------------------------------------------------
    {
        using namespace SiteRequest;
        auto g = Glyphs({Kind::Microphone, Kind::Camera});
        Check(g.size() == 2 && g[0] == Path::camera && g[1] == Path::microphone, "chip: camera before microphone (B01)");
        Check(Glyphs({Kind::Geolocation}) == std::vector<const char*>{Path::position} && Glyphs({Kind::Notifications})[0] == Path::bell && Glyphs({Kind::ClipboardRead})[0] == Path::clipboard, "chip: position / notifications / clipboard glyphs");
        Check(Glyphs({Kind::Camera, Kind::Camera}).size() == 1 && Glyphs({1, 2, 3, 4, 6}).size() == maxGlyphs, "chip: one glyph per kind, at most 3");
        Check(ChipWidth(2) == 5 + 14 + 4 + 14 + 5 && TextShift(0) == 0 && TextShift(1) == 2 + 24 + 4, "chip: Paper DJ8-0 width, URL shift");
        Check(DialogKit::Glyph::camera == Path::camera && OriginPrompt::Text::Glyph(OriginPrompt::Permission::Geolocation) == GlyphOf(Kind::Geolocation), "chip: same glyphs as the popover");
        Grants gr; Record(gr, {Kind::Camera, Kind::Microphone}, true);
        Check(Resolve(gr).state == State::Granted && Resolve(gr).kinds.size() == 2, "state: granted after Autoriser, not in use");
        Check(!SetActive(gr, Kind::Geolocation, true) && gr.active.empty(), "state: in use only for a granted kind");
        Check(SetActive(gr, Kind::Camera, true) && Resolve(gr).state == State::Active, "state: verified use = Active");
        SetActive(gr, Kind::Camera, false);
        Check(Resolve(gr).state == State::Granted, "state: use stopped, grant kept");
        Record(gr, {Kind::Camera}, false);
        Check(!Has(gr.granted, Kind::Camera) && Has(gr.blocked, Kind::Camera) && Resolve(gr).state == State::Granted && Resolve(gr).kinds.size() == 1, "state: a later block replaces the grant");
        Revoke(gr, Kind::Microphone);
        Check(Resolve(gr).state == State::Blocked, "state: only refusals left = Blocked");
        Check(ChipWidth(2, State::Active) == 5 + 6 + 6 + 14 + 6 + 14 + 5 && ChipWidth(1, State::Granted) == ChipWidth(1), "chip: Paper E4Q-0 active width (dot + 6 gaps)");
    }
    {   // B02 status popover (EAC-0): one-way toggles, link to the site settings, honest subtitle.
        OriginPrompt::Spec s; s.kind = OriginPrompt::Kind::SiteStatus; s.origin = L"meet.exemple.test";
        s.granted = {OriginPrompt::Permission::Camera, OriginPrompt::Permission::Microphone}; s.blocked = {OriginPrompt::Permission::Geolocation};
        auto m = OriginPrompt::Build(s, true);
        Check(m.title == L"meet.exemple.test peut utiliser" && m.rows.size() == 3 && m.rows[0].toggle == 1 && m.rows[2].toggle == -1 && m.rows[2].trailing == L"Bloqué", "status: granted rows with toggles, blocked row without");
        Check(m.subtitle.find(L"ne signale pas") != std::wstring::npos && !m.links.empty(), "status (real): says in-use is not reported; site settings link");
        s.demo = true; s.active = {OriginPrompt::Permission::Camera}; m = OriginPrompt::Build(s, true);
        Check(m.title == L"meet.exemple.test utilise" && m.rows[0].trailing == L"En cours" && m.demo, "status (demo): in use, badge Démonstration");
        Modal::Result r; r.button = OriginPrompt::Id::Manage;
        Check(OriginPrompt::Map(s, r).action == OriginPrompt::Action::Manage && OriginPrompt::Map(s, Modal::Result{}).action == OriginPrompt::Action::Dismiss, "status: link = Manage, Échap = nothing");
        OriginPrompt::Prompt q; std::vector<int> revoked; OriginPrompt::Prompt::Hooks h; h.revoke = [&](int k, const OriginPrompt::Spec&) { revoked.push_back(k); };
        Check(q.Create(hi, owner, root, std::move(h)), "status prompt");
        int calls = 0; s.demo = false; s.active.clear();
        q.Show(s, {200, 150, 224, 174}, [&](OriginPrompt::Result&) { ++calls; }); q.Surface().FinishAnimationsForTest();
        Check(q.Surface().FocusForTest() == Modal::Dialog::RowHit(0), "status: focus on the first toggle");
        q.Surface().KeyForTest(VK_SPACE);
        Check(revoked == std::vector<int>{OriginPrompt::Permission::Camera} && calls == 0 && q.Visible(), "status: Espace switches the toggle off = revoke, popover stays");
        const auto& rows = q.Surface().CurrentSpec().rows;
        Check(rows.size() == 3 && rows[0].toggle == 1 && rows[0].label == L"Micro" && rows[1].toggle == 0 && rows[1].trailing == L"Retirée", "status: revoked row off and inert");
        Check(!q.Surface().EnabledForTest(Modal::Dialog::RowHit(1)), "status: an off toggle cannot re-grant");
        q.Surface().PaintForTest();
        q.Surface().KeyForTest(VK_ESCAPE);
        Check(calls == 1 && revoked.size() == 1, "status: Échap closes, nothing else revoked");
        q.Surface().FinishAnimationsForTest(); q.Dispose();
    }
    // ---- B05 password demo -----------------------------------------------------------------------
    // ---- Focus-visible contract: real focus always on the safe initial control, ring by input ---------
    {
        bool hostKeyboard = false; Modal::Dialog::Hooks h; h.keyboard = [&] { return hostKeyboard; };
        Modal::Dialog f; Check(f.Create(hi, owner, root, std::move(h)), "modality dialog");
        auto spec = Modal::ConfirmSpec(L"exemple.fr", L"Continuer ?"); spec.modality = Modal::Spec::Modality::Pointer;
        int calls = 0; Modal::Result got;
        f.Show(spec, Area(), [&](const Modal::Result& r) { ++calls; got = r; }); f.FinishAnimationsForTest();
        const int initial = f.FocusForTest();
        Check(initial != 0 && !f.FocusVisibleForTest(), "pointer-opened: focus on the initial control, no ring");
        f.KeyForTest(VK_TAB);
        Check(f.FocusVisibleForTest() && f.FocusForTest() != 0, "Tab: ring shown on the focused control");
        f.ActivateForTest(0);   // a pointer action (no-op hit) goes back to pointer modality
        Check(!f.FocusVisibleForTest(), "pointer again: ring hidden, focus kept");
        f.KeyForTest(VK_ESCAPE); Check(calls == 1 && got.button != Modal::AcceptButton, "Échap still cancels with the ring hidden");
        f.FinishAnimationsForTest();
        spec.modality = Modal::Spec::Modality::Keyboard; f.Show(spec, Area(), [&](const Modal::Result&) {}); f.FinishAnimationsForTest();
        Check(f.FocusVisibleForTest(), "keyboard-opened (Entrée / Espace / UIA): ring at once");
        f.Abort(); f.FinishAnimationsForTest();
        spec.modality = Modal::Spec::Modality::Host; hostKeyboard = false; f.Show(spec, Area(), [&](const Modal::Result&) {}); f.FinishAnimationsForTest();
        Check(!f.FocusVisibleForTest(), "host modality pointer: no ring");
        f.Abort(); f.FinishAnimationsForTest(); hostKeyboard = true;
        f.Show(spec, Area(), [&](const Modal::Result&) {}); f.FinishAnimationsForTest();
        Check(f.FocusVisibleForTest(), "host modality keyboard (shortcut): ring");
        f.Abort(); f.FinishAnimationsForTest(); f.Dispose();
        // Origin prompts default to pointer (page input is not visible to the browser).
        OriginPrompt::Prompt q; Check(q.Create(hi, owner, root, {}), "modality prompt");
        OriginPrompt::Spec ps; ps.kind = OriginPrompt::Kind::Permission; ps.origin = L"meet.exemple.test"; ps.permissions = {OriginPrompt::Permission::Camera};
        q.Show(ps, {200, 150, 224, 174}, [](OriginPrompt::Result&) {}); q.Surface().FinishAnimationsForTest();
        Check(q.Surface().FocusForTest() != 0 && !q.Surface().FocusVisibleForTest(), "permission after a click: Bloquer focused, no ring");
        q.Surface().KeyForTest(VK_TAB); Check(q.Surface().FocusVisibleForTest(), "permission: Tab shows the ring");
        q.Abort(); q.Surface().FinishAnimationsForTest();
        ps.modality = Modal::Spec::Modality::Keyboard; q.Show(ps, {200, 150, 224, 174}, [](OriginPrompt::Result&) {}); q.Surface().FinishAnimationsForTest();
        Check(q.Surface().FocusVisibleForTest(), "permission opened from the keyboard: ring");
        q.Abort(); q.Surface().FinishAnimationsForTest();
        // B06 (user override): 16/20 title on the tile's line, check row in the head lanes.
        OriginPrompt::Spec e; e.kind = OriginPrompt::Kind::ExternalApp; e.origin = L"meet.exemple.test"; e.detail = L"mailto:camille.demo@exemple.test";
        const auto m = OriginPrompt::Build(e, true);
        Check(!m.largeTitle && m.checkInHeadLane && m.tileGlyph, "B06: regular title, lane check row");
        q.SetPageArea({100, 100, 1100, 800});
        for (UINT dpiCase = 0; dpiCase < 1; ++dpiCase) {
            q.Show(e, {}, [](OriginPrompt::Result&) {}); q.Surface().FinishAnimationsForTest(); auto& d = q.Surface();
            const auto box = d.BoxForTest(Modal::Dialog::SimpleCheckHit);
            Check(d.TitleDropForTest() == 6 && d.HeadLeftForTest() == 16 + 44, "B06: title line centred on the 32 DIP tile, text lane at 44");
            Check(box.left == 16 && box.right == d.WidthForTest() - 16 && box.bottom - box.top >= 18, "B06: whole check row clickable (box lane + label lane)");
            for (UINT dpi : {96u, 144u, 192u}) Check(d.RenderForTest(root + L"/build/popups/b06-external-" + std::to_wstring(dpi) + L".png", dpi), "B06 render sample");
            d.ActivateForTest(Modal::Dialog::SimpleCheckHit); d.PaintForTest();
            Check(d.RenderForTest(root + L"/build/popups/b06-external-checked-144.png", 144), "B06 checked sample");
            d.Abort(); d.FinishAnimationsForTest();
        }
        e.detail = L"un-schema-tres-long-pour-tester-le-retour-a-la-ligne-propre:x"; e.origin = L"un-sous-domaine-vraiment-tres-long.organisation-exemple.test";
        q.Show(e, {}, [](OriginPrompt::Result&) {}); q.Surface().FinishAnimationsForTest();
        Check(q.Surface().BoxForTest(Modal::Dialog::SimpleCheckHit).bottom - q.Surface().BoxForTest(Modal::Dialog::SimpleCheckHit).top >= 36, "B06: long scheme wraps (two lines), still one hit row");
        Check(q.Surface().RenderForTest(root + L"/build/popups/b06-external-long-144.png", 144), "B06 long sample");
        q.Surface().PaintForTest(); q.Abort(); q.Surface().FinishAnimationsForTest();
        e.frame = L"publicite.exemple.test"; const auto mf = OriginPrompt::Build(e, true);
        Check(mf.checkLabel.empty() && !mf.checkInHeadLane && !mf.notices.empty(), "B06 iframe: no remembering, notice kept");
        q.Show(e, {}, [](OriginPrompt::Result&) {}); q.Surface().FinishAnimationsForTest(); Check(q.Surface().RenderForTest(root + L"/build/popups/b06-external-iframe-144.png", 144), "B06 iframe sample"); q.Abort(); q.Surface().FinishAnimationsForTest();
        ID2D1Factory* fac = nullptr; D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &fac);
        auto* g = DialogKit::SvgGeometry(fac, DialogKit::Glyph::external); D2D1_RECT_F gb{}; if (g) g->GetBounds(nullptr, &gb);
        Check(g && std::fabs((gb.left + gb.right) / 2 - 128) < 1 && std::fabs((gb.top + gb.bottom) / 2 - 128) < 1, "B06: external glyph ink is centred in its 256 box (so in the tile)");
        if (g) g->Release(); if (fac) fac->Release();
        q.Dispose();
    }
    // ---- Popover lanes & roles (B06 rule generalised): every origin popover, rendered at 3 DPI ---------
    {
        OriginPrompt::Prompt q; Check(q.Create(hi, owner, root, {}), "alignment prompt"); q.SetPageArea({100, 100, 1300, 900});
        auto show = [&](const OriginPrompt::Spec& sp) { q.Show(sp, {200, 150, 224, 174}, [](OriginPrompt::Result&) {}); q.Surface().FinishAnimationsForTest(); return &q.Surface(); };
        auto shots = [&](const char* name) { for (UINT dpi : {96u, 144u, 192u}) { std::wstring n(name, name + std::strlen(name)); q.Surface().RenderForTest(root + L"/build/qa-alignment/" + n + L"-" + std::to_wstring(dpi) + L".png", dpi); } };
        using OP = OriginPrompt::Spec; namespace Pm = OriginPrompt::Permission;
        OP single; single.kind = OriginPrompt::Kind::Permission; single.origin = L"meteofrance.com"; single.permissions = {Pm::Geolocation};
        auto* d = show(single);
        Check(d->LaneCheckForTest() && d->LaneBoxLeftForTest() == 4 && d->LaneTextForTest() == 32 && d->HeadLeftForTest() == 16 + 32, "permission (single): check box centred in the 24 DIP glyph lane, label on the title/description column (32)");
        Check(d->TitleStyleForTest().size == 16 && d->TitleStyleForTest().line == 20 && d->TitleStyleForTest().medium, "permission: title 16/20 Medium");
        d->KeyForTest(VK_TAB); shots("permission-single"); q.Abort(); q.Surface().FinishAnimationsForTest();
        OP grouped = single; grouped.origin = L"un-sous-domaine-vraiment-tres-long.visio-exemple.test"; grouped.permissions = {Pm::Camera, Pm::Microphone, Pm::Geolocation};
        d = show(grouped);
        Check(d->LaneBoxLeftForTest() == 8 && d->LaneTextForTest() == 36, "permission (grouped): box in the rows' glyph lane (4 + 24), label on the rows' text column (36)");
        shots("permission-grouped"); q.Abort(); q.Surface().FinishAnimationsForTest();
        OP bell = single; bell.permissions = {Pm::Notifications}; d = show(bell);
        Check(!d->LaneCheckForTest() || q.Surface().CurrentSpec().checkLabel.empty(), "notifications: notice instead of a check");
        Check(d->LaneTextForTest() == 32, "notifications: notice text on the description column");
        shots("permission-notifications"); q.Abort(); q.Surface().FinishAnimationsForTest();
        OP auth; auth.kind = OriginPrompt::Kind::BasicAuth; auth.origin = L"intranet.exemple.test"; auth.detail = L"Zone protégée"; d = show(auth);
        Check(d->TitleStyleForTest().size == 16 && !d->LaneCheckForTest(), "Basic auth: 16/20 title (was 20/24), form check unchanged");
        shots("basic-auth"); q.Abort(); q.Surface().FinishAnimationsForTest();
        auth.user = L"camille.demo"; d = show(auth); q.AuthRefused(); shots("basic-auth-error"); q.Abort(); q.Surface().FinishAnimationsForTest();
        {   // Head / lane glyphs: ink centred in their box within 0.5 DIP at 16 DIP (geometry, not screenshots).
            ID2D1Factory* fac = nullptr; D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &fac); bool centred = true;
            for (const char* g : {DialogKit::Glyph::camera, DialogKit::Glyph::microphone, DialogKit::Glyph::position, DialogKit::Glyph::bell, DialogKit::Glyph::clipboard, DialogKit::Glyph::info, DialogKit::Glyph::external, DialogKit::Glyph::file}) {
                auto* geo = DialogKit::SvgGeometry(fac, g); D2D1_RECT_F b{}; if (geo) { geo->GetBounds(nullptr, &b); geo->Release(); } else centred = false;
                const float dx = ((b.left + b.right) / 2 - 128) * 16 / 256, dy = ((b.top + b.bottom) / 2 - 128) * 16 / 256;
                if (std::fabs(dx) > .5f || std::fabs(dy) > .5f || b.left < 0 || b.top < 0 || b.right > 256.5f || b.bottom > 256.5f) { centred = false; std::printf("glyph off-centre %.2f %.2f\n", dx, dy); }
            }
            Check(centred, "popover glyphs: ink centred (<= 0.5 DIP) and inside the 256 box (never clipped)");
            if (fac) fac->Release();
        }
        OP cert; cert.kind = OriginPrompt::Kind::ClientCertificate; cert.origin = L"banque.exemple.test"; cert.accounts = {{L"Camille Démo", L"Émis par Autorité Démo · expire en 2027"}, {L"Poste de travail démo", L"Émis par CA Interne Démo"}};
        d = show(cert); Check(d->TitleStyleForTest().size == 16, "certificate: 16/20 title (was 20/24)"); shots("certificate"); q.Abort(); q.Surface().FinishAnimationsForTest();
        OP share; share.kind = OriginPrompt::Kind::ScreenShare; share.origin = L"meet.exemple.test"; share.demo = true; share.shareSources = {{L"Ordre du jour", 0, 0}, {L"Visio", 0, 5}};
        d = show(share); Check(d->TitleStyleForTest().size == 16, "screen share: 16/20 title (was 20/24)"); shots("screen-share"); q.Abort(); q.Surface().FinishAnimationsForTest();
        OP ext; ext.kind = OriginPrompt::Kind::ExternalApp; ext.origin = L"meet.exemple.test"; ext.detail = L"mailto:x"; ext.frame = L"publicite.exemple.test";
        d = show(ext); Check(d->LaneTextForTest() == 44, "B06 iframe: notice text on the description column under the tile (44, was 32)");
        shots("external-iframe"); q.Abort(); q.Surface().FinishAnimationsForTest();
        ext.frame.clear(); d = show(ext); Check(d->LaneBoxLeftForTest() == 8 && d->LaneTextForTest() == 44, "B06: unchanged (box 8, text 44)"); shots("external"); q.Abort(); q.Surface().FinishAnimationsForTest();
        OP save; save.kind = OriginPrompt::Kind::PasswordSave; save.origin = L"compte.exemple.test"; save.user = L"camille.demo@exemple.test"; save.demo = true;
        d = show(save); Check(d->TitleStyleForTest().size == 16 && !d->LaneCheckForTest(), "password save: 16/20, form layout kept"); shots("password-save"); q.Abort(); q.Surface().FinishAnimationsForTest();
        OP fill; fill.kind = OriginPrompt::Kind::PasswordFill; fill.origin = L"compte.exemple.test"; fill.accounts = {{L"camille.demo@exemple.test", L"Utilisé il y a 2 jours"}};
        show(fill); shots("password-fill"); q.Abort(); q.Surface().FinishAnimationsForTest();
        OP danger; danger.kind = OriginPrompt::Kind::DangerousDownload; danger.origin = L"telechargements.exemple.test"; danger.detail = L"facture-2026.pdf.exe";
        show(danger); shots("dangerous-download"); q.Abort(); q.Surface().FinishAnimationsForTest();
        OP status; status.kind = OriginPrompt::Kind::SiteStatus; status.origin = L"meet.exemple.test"; status.granted = {Pm::Camera, Pm::Microphone}; status.blocked = {Pm::Geolocation};
        show(status); shots("site-status"); q.Abort(); q.Surface().FinishAnimationsForTest();
        q.Dispose();
    }
    // « Ouvrir tout » (> 20 favoris) from a menu: the confirmation's ring follows how the item was picked.
    {
        ContextMenu::Menu menu; menu.Init(hi, owner, root);
        std::vector<ContextMenu::Item> items{ContextMenu::Command(7, L"Ouvrir tout"), ContextMenu::Command(8, L"Autre")};
        PostMessageW(owner, WM_KEYDOWN, VK_RETURN, 0);
        const int id = menu.Track(items, {300, 300}, nullptr, true, true);
        Check(id == 7 && menu.LastChoiceKeyboard(), "menu: Entrée picks the item, choice marked keyboard");
        PostMessageW(owner, WM_KEYDOWN, VK_ESCAPE, 0);
        Check(menu.Track(items, {300, 300}, nullptr, true, true) == 0 && !menu.LastChoiceKeyboard(), "menu: dismissed, no keyboard choice");
        menu.Dispose();
    }
    // ---- Settings drawer: « Autorisations du site » opens ON its view (no root page under it) --------
    {
        BrowserSettings::State st; SettingsUI::Drawer::Hooks dh; std::function<void(std::vector<BrowserSettings::Site>)> pending;
        // « Effacer les données… » from the drawer: the confirmation's ring follows the drawer's own input,
        // not the chrome's last one (host = keyboardModality, synced by the hook as main does).
        bool hostKeyboard = true; bool ring = false; int opened = 0;
        Modal::Dialog::Hooks ch; ch.keyboard = [&] { return hostKeyboard; };
        Modal::Dialog confirm; Check(confirm.Create(hi, owner, root, std::move(ch)), "clear-data dialog");
        dh.clearData = [&](bool keyboard) {
            hostKeyboard = keyboard; ++opened;
            confirm.Show(Modal::ClearDataSpec(), Area(), [](const Modal::Result&) {}); confirm.FinishAnimationsForTest();
            ring = confirm.FocusVisibleForTest() && confirm.FocusForTest() != 0;
            confirm.Abort(); confirm.FinishAnimationsForTest();
        };
        dh.loadSites = [&](std::function<void(std::vector<BrowserSettings::Site>)> done) { pending = std::move(done); };
        SettingsUI::Drawer dr; Check(dr.Create(hi, owner, root, &st, std::move(dh)), "drawer created");
        using V = SettingsUI::View; const auto t0 = Motion::Now();
        dr.ShowSites();
        bool only = true; for (int ms = 0; ms <= 400; ms += 8) { auto v = dr.PaintedViewsForTest(t0 + Motion::Milliseconds(ms)); only = only && v.size() == 1 && v[0] == V::Sites; }
        Check(dr.CurrentViewForTest() == V::Sites && dr.DepthForTest() == 1 && only, "cold open: every entrance frame paints Sites only (no Main page)");
        Check(!dr.PageMovingForTest(t0) && dr.SlideForTest(t0) < 1, "cold open: the drawer slides, the page does not");
        Check(!dr.SitesNoteForTest(t0) && dr.SitesNoteForTest(t0 + Motion::Milliseconds(600)), "slow load: no « Chargement… » within 250 ms, shown after");
        pending({}); pending = nullptr;
        Check(!dr.SitesNoteForTest(Motion::Now() + Motion::Milliseconds(300)), "loaded: list / empty state, no note");
        dr.ShowSites(); Check(dr.CurrentViewForTest() == V::Sites && !dr.PageMovingForTest(Motion::Now()), "already on Sites: nothing moves");
        dr.Hide(false, false); dr.Show();
        Check(dr.CurrentViewForTest() == V::Main, "plain open: Main (Retour path intact)");
        const auto t1 = Motion::Now(); dr.ShowSites();
        Check(dr.CurrentViewForTest() == V::Sites && dr.DepthForTest() == 1 && dr.PageMovingForTest(t1 + Motion::Milliseconds(5)), "open on Main: a normal push to Sites");
        dr.Hide(false, true); dr.ShowSites();   // re-asked while leaving
        bool only2 = true; const auto t2 = Motion::Now(); for (int ms = 0; ms <= 400; ms += 8) { auto v = dr.PaintedViewsForTest(t2 + Motion::Milliseconds(ms)); only2 = only2 && v.size() == 1 && v[0] == V::Sites; }
        Check(only2 && dr.Visible(), "re-opened while closing: straight onto Sites, no ghost page");
        {   // Scroll fades: opaque canvas at the edge, reach follows what is hidden (continuous, never a dimmed overlay).
            using D = SettingsUI::Drawer; const auto top = D::FadeReachForTest(0, 120), mid = D::FadeReachForTest(60, 120), almost = D::FadeReachForTest(117, 120), end = D::FadeReachForTest(120, 120);
            Check(top.first == 0 && top.second == 48 && mid.first == 16 && mid.second == 48, "fades: top none at rest, bottom full while content remains");
            Check(almost.second == 3 && end.second == 0 && end.first == 16, "fades: the bottom fade shrinks to nothing as the end is reached (no half-dimmed band)");
            bool continuous = true; float last = D::FadeReachForTest(0, 120).second;
            for (float sc = 0; sc <= 120; sc += .5f) { const float v = D::FadeReachForTest(sc, 120).second; continuous = continuous && std::fabs(v - last) <= .5f + 1e-3f; last = v; }
            Check(continuous, "fades: reach changes continuously with scroll");
        }
        {   // Keyboard in the chrome, then a CLICK on the drawer row: no ring (focus still on the safe control).
            hostKeyboard = true; dr.ActivateForTest(SettingsUI::ClearData, false);
            Check(opened == 1 && !ring, "chrome keyboard then click « Effacer les données… »: no focus ring");
            dr.ActivateForTest(SettingsUI::ClearData, true);
            Check(opened == 2 && ring, "same row from the keyboard (Entrée / Espace / UIA): ring");
            dr.ActivateForTest(SettingsUI::ClearData, false);
            Check(opened == 3 && !ring, "pointer again: no ring");
        }
        dr.Hide(false, false); dr.Dispose(); confirm.Dispose();
    }
    {
        PasswordDemo::Window demo; Check(demo.Create(hi, owner, root, {}), "PasswordDemo::Create");
        int closed = 0;
        demo.Show(Area(), [&] { ++closed; });
        Check(demo.Accounts().size() == 5 && demo.CurrentView() == PasswordDemo::Window::View::List, "demo: 5 fictitious accounts, list view");
        demo.Surface().ActivateForTest(Modal::Dialog::AccountHit(0));
        Check(demo.CurrentView() == PasswordDemo::Window::View::Detail && demo.Visible(), "demo: row opens the detail");
        Check(demo.Surface().FieldForTest(1) == L"Lumen-atelier-2026!", "demo: fictitious password in a masked field");
        demo.Surface().ActivateForTest(Modal::Dialog::ButtonHit(0));
        Check(demo.CurrentView() == PasswordDemo::Window::View::Confirm, "demo: inline delete confirmation");
        demo.Surface().ActivateForTest(Modal::Dialog::ButtonHit(1));
        Check(demo.Accounts().size() == 4 && demo.CurrentView() == PasswordDemo::Window::View::List, "demo: deleted in memory, back to the list");
        demo.Surface().KeyForTest(VK_ESCAPE);
        Check(closed == 1 && !demo.Visible(), "demo: Échap closes, closed() once");
        demo.Dispose(); Check(closed == 1, "demo: Dispose does not call closed() again");
    }
    d.Dispose(); DestroyWindow(owner);
    std::printf("%d checks, %d failures\n", checks, failures);
    CoUninitialize();
    return failures ? 1 : 0;
}
