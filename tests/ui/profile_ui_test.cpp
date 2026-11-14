// Profiles UI (Paper p-I-0): profile menu rows, tab preview, editor block — rendered off screen
// (software Direct2D into WIC, no window, no WebView2) and checked against the Paper values.
// Usage: profile_ui_test.exe <repo root> [output dir for PNG renders]
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <dwmapi.h>
#include <wincodec.h>
#include <vector>
#include <cstdio>
#include <string>
#include <cmath>
#include <algorithm>
#include "../../src/profile-menu.h"
#include "../../src/profile-editor.h"
#include "../../src/profile-dialog.h"
#include "../../src/profile-delete-dialog.h"
#include "../../src/context-toast.h"
#include "../../src/profile-args.h"
#include "../../src/workspace-failover.h"
#include "../../src/modal.h"
#include "../../src/content-transition.h"

static int failures = 0;
static void Expect(bool ok, const char* what) { std::printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++failures; }

int wmain(int argc, wchar_t** argv) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const std::wstring root = argc > 1 ? argv[1] : L".";
    const std::wstring out = argc > 2 ? argv[2] : L".";
    namespace PM = NativeChrome::ProfileMenu; namespace PE = NativeChrome::ProfileEditor;
    // ---- relaunch arguments (re-review R2): --delete-profile is one-shot, --profile stays ----
    Expect(ProfileArgs::ForRelaunch(L"--profile default --delete-profile p5bc0518c213d") == L"--profile default", "relaunch drops --delete-profile <id>, keeps --profile");
    Expect(ProfileArgs::ForRelaunch(L"--delete-profile pa --profile pb --restore-window w1") == L"--profile pb --restore-window w1", "leading --delete-profile stripped, the rest in order");
    Expect(ProfileArgs::ForRelaunch(L"--profile pb") == L"--profile pb" && ProfileArgs::ForRelaunch(L"") == L"", "nothing to strip: unchanged");
    Expect(ProfileArgs::Value(ProfileArgs::ForRelaunch(L"--profile pb --delete-profile pa"), L"--delete-profile").empty() && ProfileArgs::Value(L"--profile pb --delete-profile pa", L"--profile") == L"pb", "after stripping no take-over can run again; the profile is still read");
    Expect(ProfileArgs::ForRelaunch(L"--delete-profile --profile pb") == L"--profile pb", "value-less --delete-profile does not swallow the next flag");
    Expect(ProfileArgs::ForRelaunch(L"--profile pb --delete-profile") == L"--profile pb", "trailing value-less --delete-profile stripped");
    Expect(ProfileArgs::ForRelaunch(L"--profile pb --delete-profile   pa --private") == L"--profile pb --private", "extra spaces: value consumed, later flags kept");
    Expect(ProfileArgs::Value(L"--delete-profile --profile pb", L"--delete-profile").empty() && ProfileArgs::Value(L"--delete-profile --profile pb", L"--profile") == L"pb", "malformed value: none read, the profile still read");
    // ---- take-over race (final review): reserve before answering, refuse anything else meanwhile ----
    {
        ProfileTakeOver::Gate g;
        Expect(g.Reserve(L"pfamille", false, false) && g.Owns(L"pfamille"), "host accepts a take-over: reserved synchronously");
        Expect(g.Busy(), "a local delete arriving before the deferred take-over runs is refused (no undo can start in between)");
        Expect(!g.Reserve(L"ptravail", false, false), "a second take-over request is refused while one is reserved");
        g.Clear(); Expect(!g.Busy() && g.Reserve(L"ptravail", false, false), "cleared when the delete ends (done / failed / timed out): next request accepted");
        ProfileTakeOver::Gate busy; Expect(!busy.Reserve(L"pfamille", true, false) && !busy.Busy(), "busy host (its own undo pending) refuses and reserves nothing");
        ProfileTakeOver::Gate priv; Expect(!priv.Reserve(L"pfamille", false, true), "private window / own profile / bad id never takes a delete over");
    }
    // ---- model helpers ----
    PM::Model model; model.tabCount = 2;
    auto ink = [](ProfileColor::Rgb8 c) { return PE::Color(c); };
    PM::Profile travail; travail.id = L"travail"; travail.name = L"Travail"; travail.icon = L"briefcase"; travail.ink = ink(ProfileColor::Ink(150, .14f)); travail.windows = 1; travail.current = true;
    PM::Profile perso; perso.id = L"default"; perso.name = L"Personnel"; perso.icon = L"house"; perso.ink = ink(ProfileColor::Neutral()); perso.windows = 3;
    for (int i = 0; i < 9; ++i) { PM::Tab t; t.title = L"Onglet " + std::to_wstring(i + 1); t.site = i % 2 ? L"example.org" : L"marmiton.org"; t.session = i < 4 ? L"a" : L"b"; t.index = i; perso.tabs.push_back(t); }
    PM::Profile famille; famille.id = L"famille"; famille.name = L"Famille"; famille.icon = L"users-three"; famille.ink = ink(ProfileColor::Ink(80, .14f));
    model.profiles = {travail, perso, famille};

    // ---- pure helpers ----
    Expect(PM::RowName(travail) == L"Travail, cette fenêtre, 1 fenêtre ouverte", "row name reads « Travail, cette fenêtre, 1 fenêtre ouverte »");
    Expect(PM::RowName(perso) == L"Personnel, 3 fenêtres ouvertes", "plural window count");
    Expect(PM::OverflowLine(perso, 6) == L"+ 3 autres onglets · 1 autre fenêtre", "overflow line counts hidden tabs and windows");
    Expect(PM::OverflowLine(famille, 0).empty(), "no overflow without tabs");
    PM::Tab blank; blank.site = L"www.intranet.example"; Expect(PM::Initial(blank) == L"I", "initial tile letter skips www.");

    // ---- menu layout ----
    PM::Menu menu;
    const bool rendered = menu.RenderForTest(model, root, 3, 3, true, (out + L"/profile-menu.png").c_str(), (out + L"/profile-preview.png").c_str());
    Expect(rendered, "menu and preview render off screen");
    const auto& rows = menu.Rows();
    Expect(rows.size() == 9, "rows: tab list, separator, 3 profiles, separator, create, private window, manage");
    if (rows.size() == 9) {
        Expect(rows[7].kind == PM::Menu::Kind::Private, "« Nouvelle fenêtre privée » sits after « Nouveau profil… », apart from the profiles");
        Expect(rows[0].kind == PM::Menu::Kind::TabList && rows[0].top == 5 && rows[0].height == 32, "first row « Liste des onglets » at 4 + 1, 32 high");
        Expect(rows[1].height == 9, "separator 9");
        Expect(rows[2].top == 46, "first profile row under the separator");
        Expect(rows[8].kind == PM::Menu::Kind::Manage, "last row « Gérer les profils… »");
    }
    // ---- profile row context action (same-window model): right-click / Maj+F10 / Menu never switch ----
    {
        PM::Menu rm; const auto c = rm.TestRowMenu(model, 3);
        Expect(c.action == PM::Action::RowMenu && c.profile == L"default", "context on a profile row: its own menu (« Ouvrir dans une nouvelle fenêtre »), not the switch");
        const auto none = rm.TestRowMenu(model, 0);
        Expect(none.action == PM::Action::None, "no context menu on non-profile rows");
        // UI Automation: the context action is a real Invoke target (a child button of each profile row).
        const auto nodes = rm.TestMenuNodes(model); int actions = 0; bool parented = true, rowInvokable = true;
        for (const auto& n : nodes) {
            if (n.automationId == L"ProfileMenuNewWindow") { ++actions; parented = parented && n.parent == PM::Menu::RowKey(static_cast<int>(n.key - PM::Menu::NewWindowBase)) && n.role == Accessibility::Role::Button && n.name == L"Ouvrir dans une nouvelle fenêtre"; }
            if (n.automationId == L"ProfileMenuProfile" && n.expand != 0) rowInvokable = false;
        }
        int profiles = 0; for (const auto& n : nodes) if (n.automationId == L"ProfileMenuProfile") ++profiles;
        Expect(actions == profiles && actions > 0 && parented, "each profile row exposes « Ouvrir dans une nouvelle fenêtre » as a child Button (UIA Invoke)");
        Expect(rowInvokable, "profile rows keep Invoke (no ExpandCollapse): the row still switches");
        const auto nw = rm.TestNewWindow(model, 3);
        Expect(nw.action == PM::Action::NewWindow && nw.profile == L"default", "invoking the child action asks a new window for that profile, never a switch");
        Expect(rm.TestNewWindow(model, 0).action == PM::Action::None, "no new-window action on non-profile rows");
    }
    // ---- engine failure of a profile: local ONLY with a demonstrably working fallback (Profiles::WorkspaceFailover) ----
    {
        namespace WF = Profiles::WorkspaceFailover;
        auto healthy = [](const wchar_t* id, unsigned long long used) { WF::Candidate c; c.id = id; c.lastUsed = used; c.started = true; c.engine = true; c.controller = true; return c; };
        const unsigned long long now = 1000000;
        WF::Candidate failing = healthy(L"A", 9);
        WF::Candidate slot; slot.id = L"S"; slot.lastUsed = 8; slot.cold = true;                       // restored, never started
        WF::Candidate cold = healthy(L"C", 7); cold.cold = true; cold.engine = false; cold.controller = false;
        WF::Candidate pending = healthy(L"P", 6); pending.pending = true; pending.controller = false;
        WF::Candidate dead = healthy(L"D", 5); dead.dead = true;
        WF::Candidate noController = healthy(L"N", 4); noController.controller = false;               // engine but no usable tab
        WF::Candidate failed = healthy(L"F", 3); failed.failedAt = now - 1000;                           // failed a second ago
        std::vector<WF::Candidate> none{failing, slot, cold, pending, dead, noController, failed};
        Expect(WF::Target(none, L"A", now).empty(), "failover: cold / unstarted slot / pending / dead / no controller / recently failed are never fallbacks -> normal recovery");
        auto one = none; one.push_back(healthy(L"H", 1));
        Expect(WF::Target(one, L"A", now) == L"H", "failover: the one healthy profile is the fallback, even if least recently used");
        auto two = one; two.push_back(healthy(L"H2", 2));
        Expect(WF::Target(two, L"A", now) == L"H2", "failover: the most recently used healthy profile wins");
        Expect(WF::Target({healthy(L"A", 1)}, L"A", now).empty(), "failover: the failing profile never falls back to itself");
        WF::Candidate old = failed; old.failedAt = now - WF::RecentFailureMs;
        Expect(WF::Target({failing, old}, L"A", now) == L"F", "failover: an old failure (>= 60 s) no longer disqualifies a working profile");
        // Systemic failure, no loop: A fails -> B (healthy). B then fails 1 s later -> A failed recently: no fallback,
        // normal recovery once; nothing is switched or started again.
        WF::Candidate a = healthy(L"A", 2), b = healthy(L"B", 1);
        Expect(WF::Target({a, b}, L"A", now) == L"B", "systemic: the first failure falls back to the working B");
        a.failedAt = now; a.cold = true; a.engine = a.controller = false; b.lastUsed = 3;
        Expect(WF::Target({a, b}, L"B", now + 1000).empty(), "systemic: B failing next has no fallback (A failed, cold) -> normal recovery, no ping-pong");
        a.cold = false; a.engine = a.controller = true;   // even if A had come back live, its recent failure keeps it out
        Expect(WF::Target({a, b}, L"B", now + 1000).empty(), "systemic: a recently failed profile is never the fallback within 60 s");
    }
    // ---- private window menu: tab list + « Nouvelle fenêtre privée », no profile rows ----
    {
        PM::Model pm; pm.privateWindow = true; pm.tabCount = 2; PM::Menu priv;
        Expect(priv.RenderForTest(pm, root, 2, -1, true, (out + L"/profile-menu-private.png").c_str(), nullptr), "private menu renders");
        const auto& pr = priv.Rows();
        Expect(pr.size() == 3 && pr[0].kind == PM::Menu::Kind::TabList && pr[2].kind == PM::Menu::Kind::Private, "private menu: tab list, separator, new private window — nothing from the profiles");
    }
    // ---- menu motion (P05): in 180 ms + 6 DIP, out the same path 150 ms; reduced: opacity 100 ms ----
    {
        PM::Menu mm;
        Expect(mm.TestEnterPresence(0, false) < .01f && mm.TestEnterPresence(90, false) > .5f && mm.TestEnterPresence(181, false) == 1, "menu enters in 180 ms (ease-out)");
        Expect(mm.TestLeavePresence(75, false) > .3f && mm.TestLeavePresence(75, false) < 1 && mm.TestLeavePresence(151, false) == 0, "menu leaves in 150 ms (same path, reversed)");
        Expect(mm.TestEnterPresence(101, true) == 1 && mm.TestLeavePresence(101, true) == 0, "reduced motion: opacity only, 100 ms each way");
        Expect(std::abs(mm.TestLeavePresence(0, false, .4f) - .4f) < .01f, "leaving mid-entrance starts from what is on screen");
    }
    // ---- profile button (P05): icon cross-fade 300 ms, colour-only 120 ms, interruptible ----
    {
        NativeChrome::Chrome c;
        if (SUCCEEDED(c.InitOffscreen(root, 192, 44, 44))) {
            const auto t0 = NativeChrome::Motion::Now(); auto at = [&](int ms) { return t0 + std::chrono::milliseconds(ms); };
            const auto green = PE::Color(ProfileColor::Ink(150, .14f)), orange = PE::Color(ProfileColor::Ink(55, .14f));
            c.SetProfileButton(L"Travail", ProfileIcons::Fill(L"briefcase"), green, false, t0);
            Expect(!c.profileButton.animating, "first look: no animation");
            c.SetProfileButton(L"Travail", ProfileIcons::Fill(L"heart"), green, true, t0);
            Expect(c.profileButton.animating && c.profileButton.iconChange && std::abs(c.ProfileButtonProgress(at(150)) - .5f) < .02f && c.ProfileButtonProgress(at(300)) == 1, "icon change: 300 ms cross-fade");
            for (int ms : {0, 75, 150, 225, 300}) {
                c.RenderTarget()->BeginDraw(); c.RenderTarget()->Clear(D2D1::ColorF(0x17 / 255.f, 0x17 / 255.f, 0x17 / 255.f)); c.DrawProfileButton(0, 0, at(ms)); c.RenderTarget()->EndDraw();
                c.SavePng((out + L"/button-swap-" + std::to_wstring(ms) + L".png").c_str());
            }
            Expect(!c.ProfileButtonAnimating(at(301)), "swap settles");
            c.SetProfileButton(L"Travail", ProfileIcons::Fill(L"heart"), orange, true, at(400));
            Expect(c.profileButton.animating && !c.profileButton.iconChange && std::abs(c.ProfileButtonProgress(at(460)) - .5f) < .02f && c.ProfileButtonProgress(at(520)) == 1, "colour-only change: 120 ms");
            const auto mid = c.ProfileInkAt(at(460)); c.SetProfileButton(L"Travail", ProfileIcons::Fill(L"heart"), green, true, at(460));
            Expect(std::abs(c.profileButton.fromInk.r - mid.r) < .001f, "interrupted colour change starts from the ink on screen");
        }
        c.Dispose();
    }
    // ---- editor geometry ----
    PE::Editor editor; editor.SetWidth(368);
    Profiles::Profile p; p.id = L"loisirs"; p.name = L"Loisirs"; p.icon = L"heart"; p.hue = 200; p.intensity = .14f; editor.SetValue(p);
    Expect(editor.Cell(5).right == 212 && editor.Cell(6).top == editor.Cell(0).top + 36, "icon grid 6 × 2, 32 + 4");
    Expect(editor.Track(PE::Hue).bottom - editor.Track(PE::Hue).top == 12, "slider track 12 high");
    Expect(editor.Lane(PE::Hue).right == 368 - 36 - 8, "value lane 36 after an 8 gap");
    Expect(PE::IntensityPercent(p) == 88, "intensity .14 reads 88 %");
    Expect(editor.Key(VK_RIGHT) == false, "keys do nothing without a focused stop");
    editor.TestFocus(PE::Hue); editor.Key(VK_END); Expect(editor.Value().hue == 359, "Fin: 359°");
    editor.Key(VK_RIGHT); Expect(editor.Value().hue == 0, "hue wraps 359 → 0 from the keyboard");
    editor.TestFocus(PE::Intensity); editor.Key(VK_HOME); Expect(editor.Value().neutral, "intensity 0 is the neutral grey");
    editor.TestFocus(PE::Icons); editor.Key(VK_DOWN); Expect(editor.Value().icon == L"leaf", "↓ moves one row in the icon grid (heart → leaf)");
    {
        NativeChrome::Chrome c; const float h = editor.Height();
        if (SUCCEEDED(c.InitOffscreen(root, 96, 368, h))) { c.RenderTarget()->BeginDraw(); c.RenderTarget()->Clear(D2D1::ColorF(0x17 / 255.f, 0x17 / 255.f, 0x17 / 255.f)); editor.Paint(c, {0, 0}); c.RenderTarget()->EndDraw(); Expect(SUCCEEDED(c.SavePng((out + L"/profile-editor.png").c_str())), "editor renders off screen"); }
        c.Dispose();
    }
    // ---- delete dialog (Paper P04 current: default, in progress, failure) ----
    {
        namespace PD = NativeChrome::ProfileDeleteDialog; PD::Dialog d;
        PD::Spec spec; spec.name = L"Famille"; spec.glyph = ProfileIcons::Fill(L"users-three"); spec.ink = PE::Color(ProfileColor::Ink(80, .14f));
        Expect(d.RenderForTest(root, spec, 0, (out + L"/profile-delete-default.png").c_str()), "delete dialog: default renders");
        const float h0 = d.HeightForTest();
        Expect(d.RenderForTest(root, spec, 1, (out + L"/profile-delete-busy.png").c_str()) && d.HeightForTest() == h0, "delete dialog: in progress keeps the height");
        Expect(d.RenderForTest(root, spec, 2, (out + L"/profile-delete-failed.png").c_str()) && d.HeightForTest() > h0, "delete dialog: failure adds the error line");
        spec.note = L"Les téléchargements en cours dans ces fenêtres seront annulés.";
        Expect(d.RenderForTest(root, spec, 0, (out + L"/profile-delete-open-windows.png").c_str()) && (d.HeightForTest() == h0 + 20 + 16 || d.HeightForTest() == h0 + 40 + 16), "delete dialog with open windows: the downloads note adds its wrapped lines + gap, nothing clipped");
    }
    // ---- P04 notices (deleted with inline Annuler, restored with Rouvrir) ----
    {
        WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"ProfileUiOwner"; RegisterClassW(&wc);
        HWND owner = CreateWindowExW(0, wc.lpszClassName, L"", WS_POPUP, 0, 0, 800, 600, nullptr, nullptr, wc.hInstance, nullptr);
        NativeChrome::ContextToast::Toast toast;
        if (owner && toast.Create(wc.hInstance, owner, root)) {
            const RECT page{0, 0, 800, 600};
            NativeChrome::ContextToast::Spec d; d.kind = NativeChrome::ContextToast::Kind::Card; d.profileCard = true; d.inlineAction = true; d.durationMs = 0; d.closable = false;
            d.message = L"Profil « Travail » supprimé"; d.detail = L"Effacement définitif dans 7 s."; d.action = L"Annuler";
            toast.Show(d, page, [](bool) {}); toast.PaintForTest();
            Expect(toast.SizeDip().cx == 320 && toast.SizeDip().cy == 2 + 32 + 20 + 16 + 32, "deleted notice: 320 wide, header + inline row (Paper Y8Y-0)");
            Expect(toast.RenderForTest(root, (out + L"/profile-toast-deleted.png").c_str()), "deleted notice renders");
            NativeChrome::ContextToast::Spec r = d; r.inlineAction = false; r.message = L"Profil « Travail » rétabli";
            r.detail = L"Ses données et les onglets de ses 2 fenêtres sont intacts ; elles ne se rouvrent pas seules."; r.action = L"Rouvrir ses fenêtres";
            toast.Update(r); toast.PaintForTest();
            Expect(toast.SizeDip().cx == 320, "restored notice: 320 wide (Paper Y9A-0)");
            Expect(toast.RenderForTest(root, (out + L"/profile-toast-restored.png").c_str()), "restored notice renders");
            // Success check of a status toast: the window's profile INK (was the fixed legacy #7D98FF).
            auto tally = [](const std::wstring& png, int& green, int& blue) {
                green = blue = 0; IWICImagingFactory* f = nullptr; IWICBitmapDecoder* d = nullptr; IWICBitmapFrameDecode* fr = nullptr; IWICFormatConverter* cv = nullptr;
                if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f)))) return;
                UINT w = 0, h = 0; std::vector<BYTE> px;
                if (SUCCEEDED(f->CreateDecoderFromFilename(png.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &d)) && SUCCEEDED(d->GetFrame(0, &fr)) && SUCCEEDED(f->CreateFormatConverter(&cv))
                    && SUCCEEDED(cv->Initialize(fr, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)) && SUCCEEDED(cv->GetSize(&w, &h))) {
                    px.resize(size_t(w) * h * 4); if (SUCCEEDED(cv->CopyPixels(nullptr, w * 4, static_cast<UINT>(px.size()), px.data())))
                        for (size_t i = 0; i < px.size(); i += 4) { const int b = px[i], g = px[i + 1], r = px[i + 2]; if (g > r + 40 && g > b + 40) ++green; if (b > r + 60 && b > g + 30) ++blue; }
                }
                if (cv) cv->Release(); if (fr) fr->Release(); if (d) d->Release(); f->Release();
            };
            NativeChrome::Design::Accent::SetInk(0x6FD087);
            toast.Update(NativeChrome::ContextToast::PageSavedSpec(L"page.html")); toast.PaintForTest();
            const std::wstring saved = out + L"/toast-success-green.png"; toast.RenderForTest(root, saved.c_str());
            int green = 0, blue = 0; tally(saved, green, blue);
            Expect(green > 10 && blue == 0, "success toast: the check is the profile ink (green), no legacy blue pixel");
            NativeChrome::Design::Accent::SetInk(0x3E67FF);
            toast.HideNow(); toast.Dispose();
        }
        if (owner) DestroyWindow(owner);
    }
    // ---- tab / profile switch transition: blur-out AND blur-in (two one-shot snapshots of our own webview windows) ----
    {
        namespace CT = ContentTransition;
        const auto tp = CT::TimingOf(CT::Kind::Profile, false), tt = CT::TimingOf(CT::Kind::Tab, false), rp = CT::TimingOf(CT::Kind::Profile, true);
        Expect(tt.durationMs <= 150 && tp.durationMs >= 180 && tp.durationMs <= 240 && tt.blurDip >= 2 && tp.blurDip <= 4, "timing: tabs <= 150 ms, profiles 180-240 ms, blur 2-4 DIP");
        Expect(rp.blurDip == 0 && rp.durationMs <= tp.durationMs, "reduced motion: opacity only (no blur), never longer");
        const float d = tp.durationMs / 1000.f;
        Expect(CT::BlurAt(0, d, 0, 4) == 0 && std::fabs(CT::BlurAt(.6f * d, d, 0, 4) - 4) < 1e-4f && CT::BlurAt(d, d, 0, 4) == 4, "outgoing blur: 0 -> peak over 60 %, then held");
        bool sharpMono = true; float last = 99; for (int k = 0; k <= 20; ++k) { const float b = CT::SharpenAt(d * k / 20, d, 5); if (b > last + 1e-5f) sharpMono = false; last = b; }
        Expect(CT::SharpenAt(0, d, 5) == 5 && CT::SharpenAt(d, d, 5) == 0 && sharpMono, "incoming blur: peak -> sharp (0), monotonic");
        Expect(CT::OldFadeAt(0, d, 1) == 1 && CT::OldFadeAt(.4f * d, d, 1) == 0 && CT::DissolveAt(.69f * d, d, 1) == 1 && CT::DissolveAt(d, d, 1) == 0, "incoming: outgoing layer gone by 40 %, overlay holds until 70 % then dissolves into the live page");
        const float t = .3f * d, b = CT::BlurAt(t, d, 0, 4);
        Expect(std::fabs(CT::BlurAt(0, d, b, 4) - b) < 1e-5f, "retarget: the new segment starts at the blur on screen");
        Expect(CT::BudgetFor(15, 1920LL * 1080) == CT::Budget::Full && CT::BudgetFor(40, 1920LL * 1080) == CT::Budget::OutgoingOnly && CT::BudgetFor(90, 1920LL * 1080) == CT::Budget::None && CT::BudgetFor(5, 7680LL * 4320) == CT::Budget::None,
               "capture budget: slow captures skip the incoming capture, very slow / huge viewports switch instantly");
        // Real overlay: a parent window with one fake WebView child (class Chrome_WidgetWin_*), off screen, never activated.
        static COLORREF childColor = RGB(220, 40, 40);
        WNDCLASSW pc{}; pc.lpfnWndProc = DefWindowProcW; pc.hInstance = GetModuleHandleW(nullptr); pc.lpszClassName = L"TransitionTestWindow"; pc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)); RegisterClassW(&pc);
        WNDCLASSW cc{}; cc.hInstance = pc.hInstance; cc.lpszClassName = L"Chrome_WidgetWin_TransitionTest";
        cc.lpfnWndProc = [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
            if (m == WM_ERASEBKGND || m == WM_PRINTCLIENT) { RECT r; GetClientRect(h, &r); HBRUSH br = CreateSolidBrush(childColor); FillRect(reinterpret_cast<HDC>(w), &r, br); DeleteObject(br); return 1; }
            return DefWindowProcW(h, m, w, l); };
        RegisterClassW(&cc);
        HWND w = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, pc.lpszClassName, L"", WS_POPUP | WS_CLIPCHILDREN, -12000, -12000, 400, 300, nullptr, nullptr, pc.hInstance, nullptr);
        HWND page = CreateWindowExW(0, cc.lpszClassName, L"", WS_CHILD | WS_VISIBLE, 0, 40, 400, 260, w, nullptr, pc.hInstance, nullptr);
        ShowWindow(w, SW_SHOWNOACTIVATE); UpdateWindow(w);
        CT::Overlay ov; const RECT vp{0, 40, 400, 300};
        ov.SetPrintFlagsForTest(0);   // GDI test windows: WM_PRINT (deterministic); the product uses PW_RENDERFULLCONTENT
        if (!ov.Begin(w, vp, CT::Kind::Profile, false, 1.25f, {page})) std::printf("note: DirectComposition Device3 unavailable here: the switch is instant (fallback)\n");
        else {
            Expect(ov.Holding() && ov.Surfaces() == 1 && ov.Captures() == 1 && ov.Blurring() && ov.Now().outer == 1, "outgoing: snapshot 1 of our webview, GPU blur growing, opacity held while the switch runs");
            const unsigned g = ov.Placed();
            Expect(ov.Waiting() && !ov.Incoming(w, g + 7, {page}), "waiting: a stale readiness callback (older generation) does nothing");
            childColor = RGB(40, 80, 220); InvalidateRect(page, nullptr, TRUE); UpdateWindow(page);   // the incoming page is placed
            Expect(ov.Incoming(w, g, {page}) && ov.CurrentPhase() == CT::Phase::Incoming && ov.Surfaces() == 2 && ov.Captures() == 2 && ov.IncomingBlurring() && ov.Now().newBlur > 0,
                   "incoming: snapshot 2 of the placed page, blurred at the blur on screen, sharpening (blur-in), at most two surfaces");
            Sleep(30); const auto mid = ov.Now();
            Expect(mid.newBlur < ov.Now().newBlur + 1 && mid.oldOpacity < 1 && mid.outer == 1, "incoming mid-phase: the outgoing layer fading, the incoming one sharpening, the overlay still covering");
            const unsigned before = ov.Generation();
            Expect(ov.Begin(w, vp, CT::Kind::Tab, false, 1.25f, {page}) && ov.Holding() && ov.Retargets() == 1 && ov.Captures() == 2 && ov.Surfaces() == 1 && ov.Generation() != before,
                   "retarget during blur-in: the incoming snapshot on screen becomes the outgoing layer (no recapture, one surface)");
            Expect(!ov.Incoming(w, before, {page}), "the previous generation's readiness can never resurface its capture");
            const unsigned g2 = ov.Placed();
            Expect(ov.FadeOut(g2) && ov.CurrentPhase() == CT::Phase::FadingOut && ov.Surfaces() == 1, "timeout / nothing to capture: the outgoing snapshot fades over the live page");
            ov.Finish(g2 + 1); Expect(ov.Active(), "a stale end timer is ignored");
            ov.Finish(g2); Expect(!ov.Active() && ov.Surfaces() == 0, "end: every snapshot released at once");
            // Reduced motion: no blur effect, no incoming capture (fade only).
            Expect(ov.Begin(w, vp, CT::Kind::Profile, true, 1.25f, {page}) && !ov.Blurring(), "reduced motion: no blur effect");
            const unsigned g3 = ov.Placed(); ov.Incoming(w, g3, {page});
            Expect(ov.CurrentPhase() == CT::Phase::FadingOut && ov.Captures() == 1, "reduced motion: no incoming capture, a plain fade");
            ov.Cancel();
            // Budget: a slow previous capture skips the incoming capture; a very slow one makes the switch instant.
            ov.SetLastCaptureMsForTest(50); ov.Begin(w, vp, CT::Kind::Tab, false, 1.f, {page}); const unsigned g4 = ov.Placed(); ov.Incoming(w, g4, {page});
            Expect(ov.CurrentPhase() == CT::Phase::FadingOut && ov.Captures() == 1, "budget: after a slow capture, outgoing blur only (no second capture)");
            ov.Cancel(); ov.SetLastCaptureMsForTest(100);
            Expect(!ov.Begin(w, vp, CT::Kind::Tab, false, 1.f, {page}) && !ov.Active(), "budget: after a very slow capture the next switch is instant");
            ov.SetLastCaptureMsForTest(0);
            // Viewport shift inside the window (bookmarks bar): re-anchored, never stretched; cancel removes everything.
            ov.Begin(w, vp, CT::Kind::Tab, false, 1.f, {page}); const RECT moved{0, 80, 400, 300}; ov.Reanchor(moved);
            Expect(ov.Active() && ov.Rect().top == 80 && ov.Surfaces() == 1, "re-anchor: the overlay follows the new viewport (offset + clip)");
            ov.Cancel(); Expect(!ov.Active() && ov.Surfaces() == 0, "cancel (resize / move / DPI / lost foreground / close): removed and released at once");
            // ---- review: input during the opaque hold, wait cap, capture allowlist ----
            Expect(CT::WaitCapMs <= 150, "the opaque hold after the switch is capped (<= 150 ms, cold profiles included), then a fade");
            ShowWindow(page, SW_SHOW);
            ov.Begin(w, vp, CT::Kind::Profile, false, 1.25f, {page}); const unsigned gi = ov.Placed();
            ov.Cancel();   // what the first click / key in a page does (ContentTransitionInput), before the page handles it
            Expect(!ov.Active() && ov.Surfaces() == 0 && !ov.Incoming(w, gi, {page}) && !ov.Active(), "input while waiting: the overlay is gone at once and the late incoming capture can never bring it back");
            ov.Begin(w, vp, CT::Kind::Tab, false, 1.f, {page}); const unsigned gc = ov.Placed();
            Expect(ov.Incoming(w, gc, {page}) && ov.IncomingBlurring(), "no input: the cached blur-in runs as before");
            ov.Cancel(); ov.Begin(w, vp, CT::Kind::Tab, true, 1.f, {page}); ov.Cancel();
            Expect(!ov.Active() && ov.Surfaces() == 0, "rapid cancel then a reduced-motion transition: nothing left");
            // Two overlapping WebView-like children at the same bounds: the previous tab's (red, on top in z-order)
            // and the current one (blue): only the allowlisted current one is captured, whatever the order.
            childColor = RGB(30, 60, 230); InvalidateRect(page, nullptr, TRUE); UpdateWindow(page);
            static COLORREF otherColor = RGB(230, 30, 30);
            WNDCLASSW oc{}; oc.hInstance = pc.hInstance; oc.lpszClassName = L"Chrome_WidgetWin_PreviousTab";
            oc.lpfnWndProc = [](HWND h, UINT m, WPARAM wp, LPARAM lp) -> LRESULT { if (m == WM_ERASEBKGND || m == WM_PRINTCLIENT) { RECT r; GetClientRect(h, &r); HBRUSH br = CreateSolidBrush(otherColor); FillRect(reinterpret_cast<HDC>(wp), &r, br); DeleteObject(br); return 1; } return DefWindowProcW(h, m, wp, lp); };
            RegisterClassW(&oc);
            HWND previous = CreateWindowExW(0, oc.lpszClassName, L"", WS_CHILD | WS_VISIBLE, 0, 40, 400, 260, w, nullptr, pc.hInstance, nullptr);
            SetWindowPos(previous, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE); UpdateWindow(previous); DwmFlush(); Sleep(50);
            auto blue = [](UINT32 px) { return (px & 255) > 180 && ((px >> 16) & 255) < 90; };
            auto red = [](UINT32 px) { return ((px >> 16) & 255) > 180 && (px & 255) < 90; };
            ov.SetLastCaptureMsForTest(0);
            Expect(!ov.Begin(w, vp, CT::Kind::Tab, false, 1.f, {page}) && ov.LastCaptureOccluded() && !ov.Active(),
                   "allowlist: the previous tab's window still visible ABOVE the current page -> refused (fade), never its pixels");
            ShowWindow(previous, SW_HIDE); RedrawWindow(w, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN); DwmFlush(); DwmFlush(); Sleep(100);   // the previous tab hidden (as the switch does)
            ov.SetLastCaptureMsForTest(0);
            Expect(ov.Begin(w, vp, CT::Kind::Tab, false, 1.f, {page}) && blue(ov.LastSampleForTest()) && !ov.LastCaptureOccluded(), "allowlist: once the previous tab is hidden, the current page is captured");
            ov.Cancel();
            ShowWindow(page, SW_HIDE); ShowWindow(previous, SW_SHOWNOACTIVATE); RedrawWindow(w, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN); DwmFlush(); DwmFlush(); Sleep(100); ov.SetLastCaptureMsForTest(0);
            Expect(ov.Begin(w, vp, CT::Kind::Tab, false, 1.f, {previous}) && red(ov.LastSampleForTest()), "allowlist: exactly the given window (control: the other one alone)");
            ov.Cancel(); ShowWindow(page, SW_SHOW);
            Expect(!ov.Begin(w, vp, CT::Kind::Tab, false, 1.f, {}) && !ov.Begin(w, vp, CT::Kind::Tab, false, 1.f, {GetDesktopWindow()}), "allowlist: nothing allowed, or a window that is not ours, is never captured");
            DestroyWindow(previous);
            ShowWindow(page, SW_HIDE);
            Expect(!ov.Begin(w, vp, CT::Kind::Tab, false, 1.f, {page}), "nothing of ours to capture (native canvas only): no overlay");
            ov.Destroy(); Expect(!ov.Available(), "destroy: every DComp resource released");
        }
        DestroyWindow(w);
    }
    // ---- primary buttons keep the PROFILE hue at rest / hover / press (user report: hover went back to the old blue) ----
    // The real DialogKit::Kit::Button (modals, origin prompts, password, site requests), rendered off screen and sampled.
    {
        NativeChrome::Chrome c;
        if (SUCCEEDED(c.InitOffscreen(root, 96, 200, 60))) {
            NativeChrome::DialogKit::Kit kit; kit.Bind(&c);
            namespace A = NativeChrome::Design::Accent;
            auto paint = [&](float hover, float press, const wchar_t* png) {
                c.RenderTarget()->BeginDraw(); c.RenderTarget()->Clear(D2D1::ColorF(0x17 / 255.f, 0x17 / 255.f, 0x17 / 255.f));
                kit.Button({20, 14, 180, 46}, L"OK", NativeChrome::DialogKit::Kit::Tone::Primary, hover, press, false, false);
                c.RenderTarget()->EndDraw(); if (png) c.SavePng((out + png).c_str());
                const UINT32 px = c.SurfacePixel(26, 30); return NativeChrome::Design::Rgb{((px >> 16) & 255) / 255.f, ((px >> 8) & 255) / 255.f, (px & 255) / 255.f};
            };
            auto sameRgb = [](NativeChrome::Design::Rgb x, NativeChrome::Design::Rgb y) { return std::fabs(x.r - y.r) < 3 / 255.f && std::fabs(x.g - y.g) < 3 / 255.f && std::fabs(x.b - y.b) < 3 / 255.f; };
            const NativeChrome::Design::Rgb legacyHover{0x57 / 255.f, 0x79 / 255.f, 1}, legacyPressed{0x35 / 255.f, 0x57 / 255.f, 0xD6 / 255.f};
            struct P { const wchar_t* name; int hue; } vivid[] = {{L"green", 145}, {L"orange", 55}, {L"purple", 305}};
            for (const auto& v : vivid) {
                const auto f8 = ProfileColor::AccentFill(v.hue, .14f); A::Set((f8.r << 16) | (f8.g << 8) | f8.b); const auto fill = A::Get();
                const std::wstring n = v.name; bool ok = true, legacy = false;
                const float states[][2] = {{0, 0}, {.5f, 0}, {1, 0}, {1, .5f}, {1, 1}, {0, 1}};
                for (const auto& st : states) {
                    const auto got = paint(st[0], st[1], st[0] == 0 && st[1] == 0 ? (L"/primary-" + n + L"-rest.png").c_str() : st[0] == 1 && st[1] == 0 ? (L"/primary-" + n + L"-hover.png").c_str() : st[0] == 1 && st[1] == 1 ? (L"/primary-" + n + L"-press.png").c_str() : nullptr);
                    ok = ok && sameRgb(got, A::StateColor(fill, st[0], st[1])) && A::WhiteContrast(got) >= 4.45f;
                    legacy = legacy || sameRgb(got, legacyHover) || sameRgb(got, legacyPressed);
                }
                const auto rest = paint(0, 0, nullptr), hov = paint(1, 0, nullptr), pre = paint(1, 1, nullptr);
                Expect(ok && !legacy && sameRgb(rest, fill), (std::string("primary button ") + (v.hue == 145 ? "green" : v.hue == 55 ? "orange" : "purple") + ": rest = profile fill, hover / mid / press = the same hue deepened (white >= 4.5:1), never the legacy blue").c_str());
                Expect(A::Luminance(rest) > A::Luminance(hov) && A::Luminance(hov) > A::Luminance(pre), "  visible feedback: rest > hover > pressed in lightness");
            }
            // Profile switch DURING a hover (accent swapped between two paints, as SwitchWorkspace / ApplyProfileChrome do):
            // the next paint at the same hover is entirely the new profile's, nothing cached.
            { const auto g = ProfileColor::AccentFill(145, .14f), o = ProfileColor::AccentFill(55, .14f);
              A::Set((g.r << 16) | (g.g << 8) | g.b); const auto gh = paint(.6f, 0, nullptr); const auto gfill = A::Get();
              A::Set((o.r << 16) | (o.g << 8) | o.b); const auto oh = paint(.6f, 0, L"/primary-switch-mid-hover.png");
              Expect(sameRgb(gh, A::StateColor(gfill, .6f, 0)) && sameRgb(oh, A::StateColor(A::Get(), .6f, 0)), "switch mid-hover: the next paint is the new profile's hover colour (no cached brush, no stale hue)");
              // The 120 ms accent tween (ProfileAnimTick mixes rest fills): every intermediate hover stays between the two hues' states.
              A::Current() = A::Mix({g.r / 255.f, g.g / 255.f, g.b / 255.f}, {o.r / 255.f, o.g / 255.f, o.b / 255.f}, .5f);
              const auto mid = paint(1, 0, nullptr);
              Expect(sameRgb(mid, A::StateColor(A::Get(), 1, 0)) && !sameRgb(mid, legacyHover), "accent tween: a mid-fade hover derives from the tweening fill, never the legacy blue"); }
            // Neutral profile (intentional blue fill): rest is exactly the reference #3E67FF; hover / press deepen it.
            A::Set(A::defaultRgb);
            const auto nr = paint(0, 0, L"/primary-neutral-rest.png"), nh = paint(1, 0, L"/primary-neutral-hover.png");
            Expect(sameRgb(nr, {0x3E / 255.f, 0x67 / 255.f, 1}) && nh.b > nh.r && nh.b > nh.g && A::WhiteContrast(nh) >= 4.45f, "neutral profile: rest #3E67FF exactly (intended), hover a deeper blue with white >= 4.5:1");
        }
        c.Dispose();
    }
    // ---- download ring follows the window's accent ink, also when it changes mid-download ----
    {
        NativeChrome::Chrome c;
        if (SUCCEEDED(c.InitOffscreen(root, 96, 64, 64))) {
            NativeChrome::DownloadIcon::Pose pose; pose.ring = NativeChrome::DownloadIcon::FillEnd; pose.arrow = NativeChrome::DownloadIcon::Morph;  // full ring
            auto dominant = [&](const wchar_t* png) {
                c.RenderTarget()->BeginDraw(); c.RenderTarget()->Clear(D2D1::ColorF(0x17 / 255.f, 0x17 / 255.f, 0x17 / 255.f));
                const bool ok = c.DrawDownloadIcon(pose, {0, 0, 64, 64}, 48, 1); c.RenderTarget()->EndDraw(); c.SavePng((out + png).c_str()); return ok;
            };
            NativeChrome::Design::Accent::SetInk(0x3E67FF); const bool a = dominant(L"/download-ring-blue.png");
            NativeChrome::Design::Accent::SetInk(0x6FD087); const bool b = dominant(L"/download-ring-green.png");
            Expect(a && b, "download ring renders with the reference blue, then with a profile ink (same pose, re-tinted)");
            // Success morph (B's assets): start, quarter, mid, three quarters, final — rendered on the full ring in the profile ink.
            const int frames[] = {0, 6, 12, 18, NativeChrome::DownloadIcon::SuccessMorph}; bool all = true;
            for (int f : frames) { pose.success = f; all = dominant((L"/download-success-" + std::to_wstring(f) + L".png").c_str()) && all; }
            Expect(all, "success morph frames render (0, 6, 12, 18, 24) with the profile ink");
            NativeChrome::Design::Accent::SetInk(0xFB954A); pose.success = NativeChrome::DownloadIcon::SuccessMorph;   // profile colour changed during the hold
            Expect(dominant(L"/download-success-24-orange.png"), "held check re-tinted when the profile colour changes (orange)");

            // ---- palette regression: the ACTIVE progress (half-filled ring) reads the profile ink in its pixels ----
            // Coloured pixels = un-premultiplied, opaque enough, max-min > 40; counts per dominant hue.
            struct Tally { int coloured = 0, green = 0, orange = 0, blue = 0; };
            auto tally = [&] {
                Tally t;
                for (uint32_t px : c.dlPixels) {
                    const int a = px >> 24; if (a < 128) continue;
                    const int b = (px & 255) * 255 / a, g = ((px >> 8) & 255) * 255 / a, r = ((px >> 16) & 255) * 255 / a;
                    if (std::max({r, g, b}) - std::min({r, g, b}) <= 40) continue;
                    ++t.coloured;
                    if (g > r + 30 && g > b + 30) ++t.green;
                    if (r > g + 30 && g > b + 30) ++t.orange;
                    if (b > r + 40 && b > g + 30) ++t.blue;
                }
                return t;
            };
            pose = {}; pose.ring = NativeChrome::DownloadIcon::Morph + NativeChrome::DownloadIcon::Steps / 2; pose.arrow = NativeChrome::DownloadIcon::Morph;  // 50 % downloaded
            NativeChrome::Design::Accent::SetInk(0x3E67FF); dominant(L"/download-progress-blue.png"); const Tally blue = tally();
            NativeChrome::Design::Accent::SetInk(0x6FD087); dominant(L"/download-progress-green.png"); const Tally green = tally();
            Expect(blue.blue > 20 && blue.green == 0, "reference: the 50 % ring is blue with the reference ink");
            Expect(green.coloured > 20 && green.green * 10 >= green.coloured * 9 && green.blue == 0, "50 % ring: green profile ink, no blue pixel left");
            // Profile colour edited mid-download: same pose (progress untouched), new colour only.
            NativeChrome::Design::Accent::SetInk(0xFB954A); dominant(L"/download-progress-orange.png"); const Tally orange = tally();
            Expect(c.dlRing == pose.ring && orange.coloured > 20 && orange.orange * 10 >= orange.coloured * 9 && orange.blue == 0 && orange.green == 0,
                   "colour edit mid-download: ring stays at the same frame, re-tinted orange, no blue/green left");
            Expect(std::abs(orange.coloured - green.coloured) <= green.coloured / 20 + 2, "re-tint keeps the filled arc's coverage (progress not reset)");
            // Neutral « Personnel » (the user's report): its ink is its own grey, so the ring holds no blue at all.
            Profiles::Profile neutral; neutral.neutral = true; neutral.intensity = 0;
            const auto grey = NativeChrome::ProfileEditor::InkOf(neutral);
            Expect(grey.r == 0xB3 && grey.g == 0xB3 && grey.b == 0xB3, "neutral profile ink is #B3B3B3");
            NativeChrome::Design::Accent::SetInk((grey.r << 16) | (grey.g << 8) | grey.b); dominant(L"/download-progress-neutral.png"); const Tally none = tally();
            Expect(none.blue == 0 && none.coloured == 0, "neutral profile: the active ring has no blue (no coloured pixel at all)");
        }
        c.Dispose();
    }
    CoUninitialize();
    std::printf(failures ? "%d FAILED\n" : "all profile UI checks passed\n", failures);
    return failures ? 1 : 0;
}
