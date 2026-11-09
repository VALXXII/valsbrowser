// Contracts of the A/B dialog family (src/modal.h, src/origin-prompt.h, src/password-demo.h):
// `done` exactly once (button, Échap, Abort, owner destroyed, Dispose), Échap = cancel button,
// Entrée = default but never a destructive one, validation blocks the default until fixed,
// busy locks the controls, secrets are wiped, focus is restored, presets carry Paper's text.
// Usage: modal_test.exe <repo root>
#include <windows.h>
#include <cstdio>
#include <string>
#include "../src/chrome.h"
#include "../src/modal.h"
#include "../src/origin-prompt.h"
#include "../src/password-demo.h"

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
    {   // B04 save: identifier required; « Jamais pour ce site ».
        OriginPrompt::Spec s; s.kind = OriginPrompt::Kind::PasswordSave; s.origin = L"github.com"; s.user = L"";
        int calls = 0; OriginPrompt::Result got;
        p.Show(s, {200, 150, 224, 174}, [&](OriginPrompt::Result& r) { ++calls; got = r; });
        p.Surface().ActivateForTest(Modal::Dialog::ButtonHit(2));
        Check(calls == 0 && !p.Surface().FieldErrorForTest().empty(), "password save: missing identifier is invalid (B04)");
        p.Surface().ActivateForTest(Modal::Dialog::ButtonHit(0));
        Check(calls == 1 && got.action == OriginPrompt::Action::Never, "password save: Jamais pour ce site");
        p.Surface().FinishAnimationsForTest();
    }
    {   // B07 dangerous download: Supprimer / Plus d'options -> Keep (no menu wired).
        OriginPrompt::Spec s; s.kind = OriginPrompt::Kind::DangerousDownload; s.origin = L"pilotes.exemple"; s.detail = L"HP_M110_Setup_v3.2.1.exe";
        int calls = 0; OriginPrompt::Result got;
        p.Show(s, {900, 150, 924, 174}, [&](OriginPrompt::Result& r) { ++calls; got = r; });
        p.Surface().ActivateForTest(Modal::Dialog::ButtonHit(1));
        Check(calls == 1 && got.action == OriginPrompt::Action::Keep, "dangerous download: Plus d'options -> Keep");
        p.Surface().FinishAnimationsForTest();
    }
    p.Dispose();
    // ---- B05 password demo -----------------------------------------------------------------------
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
