// Réglages › the « navigateur par défaut » card (src/settings-panel.h, Paper « 01 · Réglages — racine ») on the real
// drawer, with SYNTHETIC hooks and an injected clock: the card shows at the bottom of the root (above the version) only
// while this copy is registered with Windows and is not the default; « Browser par défaut » opens Windows' Default apps
// page (the hook) and never changes anything itself; « Plus tard » hides the card for 30 days (stored in the settings
// state, back after 30 days if still not the default); a change made in Windows is followed when the window comes back.
// The root list is Paper's (no « Navigateur par défaut » row any more). The card's halftone is the onboarding's shader:
// it loops only while the drawer is open on the root, motion allowed and a hardware device; reduced motion or a
// closed drawer asks the host for no frame. Captures (96 / 144 dpi, FR and EN, two loop instants) go to <capture
// folder>. Nothing on the machine is read or written (the hooks are fakes); the owner window sits off-screen.
// Usage: default_browser_settings_test.exe <repo root> <capture folder>
#include <windows.h>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <iterator>
#include <string>
#include <vector>
#include "../src/chrome.h"
#include "../src/settings-panel.h"

using namespace NativeChrome;
using SettingsUI::Kind;
static int failures = 0, checks = 0;
static void Check(bool ok, const char* what) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", what); } else std::printf("ok   %s\n", what); }
static const SettingsUI::Slot* Hit(const std::vector<SettingsUI::Slot>& slots, int hit) { for (auto& s : slots) if (s.hit == hit) return &s; return nullptr; }
static const Accessibility::Node* Node(const std::vector<Accessibility::Node>& nodes, int hit) { const long long key = SettingsUI::Drawer::UiaKey(hit); for (auto& n : nodes) if (n.key == key) return &n; return nullptr; }
static std::string Bytes(const std::wstring& path) { std::ifstream f(path.c_str(), std::ios::binary); return std::string(std::istreambuf_iterator<char>(f), {}); }
static bool Near(float a, float b) { return a > b - .01f && a < b + .01f; }

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) { std::printf("usage: default_browser_settings_test <repo root> <capture folder>\n"); return 2; }
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) { std::printf("FAIL native COM initialization\n"); return 1; }
    const std::wstring root = argv[1], shots = argv[2];
    CreateDirectoryW(shots.c_str(), nullptr);
    HINSTANCE hi = GetModuleHandleW(nullptr);
    WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = hi; wc.lpszClassName = L"DefaultBrowserSettingsOwner"; RegisterClassW(&wc);
    // Off-screen owner (the drawer docks inside it): nothing appears on the desktop.
    HWND owner = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"owner", WS_POPUP | WS_VISIBLE, -32000, -32000, 1100, 860, nullptr, nullptr, hi, nullptr);

    int status = 0, opened = 0, laterSaves = 0; bool reduced = false;
    long long clock = 1'800'000'000;   // the injected UTC clock (seconds)
    auto hooks = [&] {
        SettingsUI::Drawer::Hooks h;
        h.savedLanguage = [] { return I18n::Current(); };
        h.saveLanguage = [](I18n::Locale) { return true; };
        h.defaultBrowser = [&] { return status; };
        h.setDefaultBrowser = [&] { ++opened; };
        h.now = [&] { return clock; };
        h.reducedMotion = [&] { return reduced; };
        h.changed = [&](BrowserSettings::Change c) { if (c == BrowserSettings::Change::DefaultBrowserLater) ++laterSaves; };
        h.version = [] { SettingsUI::Drawer::Version v; v.text = L"Browser 0.3.79 · À jour"; v.canCheck = true; return v; };
        return h;
    };
    auto open = [&](SettingsUI::Drawer& dr) { dr.Show(); dr.SettleForTest(); };
    auto shot = [&](SettingsUI::Drawer& dr, const std::wstring& name) {
        dr.ScrollForTest(0); dr.SettleForTest();
        const bool a = dr.RenderForTest(shots + L"\\" + name + L"-96.png", 96, 731, root), b = dr.RenderForTest(shots + L"\\" + name + L"-144.png", 144, 731, root);
        Check(a && b, "capture rendered (96 and 144 dpi)");
    };
    auto centre = [](Design::Box b) { return std::pair<float, float>{(b.left + b.right) / 2, (b.top + b.bottom) / 2}; };

    BrowserSettings::State st;
    {   // Not the default: the card, Paper's root list.
        status = 0; SettingsUI::Drawer dr; Check(dr.Create(hi, owner, root, &st, hooks()), "drawer created"); open(dr);
        Check(dr.CardShownForTest(), "not the default: the card shows on the root");
        const auto slots = dr.SlotsForTest();
        Check(!Hit(slots, SettingsUI::DefaultLater) && !Hit(slots, SettingsUI::DefaultMake), "no « Navigateur par défaut » row in the list (the card replaces it)");
        std::vector<int> list; for (const auto& s : slots) if (s.hit && s.hit != SettingsUI::SearchField && s.hit != SettingsUI::LanguageHit) list.push_back(s.hit);
        Check(list == std::vector<int>{SettingsUI::CatEngine, SettingsUI::HomePageHit, SettingsUI::CatTabs, SettingsUI::CatPrivacy, SettingsUI::CatDownloads, SettingsUI::CatDisplay},
              "root list as Paper: Moteur de recherche, Page d'accueil, Onglets, Confidentialité, Téléchargements, Affichage et son");
        // Geometry (Paper 1DQM-0): 203 high, art 110, text 2 px up into it, buttons 32 high 10 apart, centred; version 12 under.
        const auto c = dr.CardForTest();
        Check(dr.CardForTest().text.bottom - dr.CardForTest().text.top > 39, "French: the question on two balanced lines");
        Check(Near(c.card.bottom - c.card.top, 203) && Near(c.art.bottom - c.art.top, 110) && Near(c.text.top - c.card.top, 108) && Near(c.later.top - c.card.top, 158),
              "card: 203 high, halftone 110, question at 108, buttons at 158");
        Check(Near(c.later.bottom - c.later.top, 32) && Near(c.make.left - c.later.right, 10) && Near((c.later.left + c.make.right) / 2, 166),
              "buttons: 32 high, 10 apart, centred on the drawer's lane");
        std::printf("     buttons %.2f x 32 and %.2f x 32 (Paper 79 and 143)\n", c.later.right - c.later.left, c.make.right - c.make.left);
        Check(c.later.right - c.later.left > 70 && c.later.right - c.later.left < 90 && c.make.right - c.make.left > 130 && c.make.right - c.make.left < 156, "button widths: label + 2 x 12");
        // Pointer: the real hit test answers on each button.
        auto [lx, ly] = centre(c.later); auto [mx, my] = centre(c.make);
        Check(dr.HitForTest(lx, ly) == SettingsUI::DefaultLater && dr.HitForTest(mx, my) == SettingsUI::DefaultMake, "hit test: « Plus tard » and « Browser par défaut »");
        // Keyboard and UIA.
        const auto order = dr.FocusOrderForTest(); auto at = [&](int hit) { for (size_t i = 0; i < order.size(); ++i) if (order[i] == hit) return static_cast<int>(i); return -1; };
        Check(at(SettingsUI::DefaultLater) >= 0 && at(SettingsUI::DefaultMake) == at(SettingsUI::DefaultLater) + 1 && at(SettingsUI::CheckUpdates) == at(SettingsUI::DefaultMake) + 1,
              "Tab order: … rows, « Plus tard », « Browser par défaut », then the version's button");
        const auto nodes = dr.AccessibleNodesForTest(); const auto* later = Node(nodes, SettingsUI::DefaultLater); const auto* make = Node(nodes, SettingsUI::DefaultMake);
        Check(later && later->name == L"Plus tard" && later->automationId == L"SettingsDefaultBrowserLater" && later->help == L"Ne plus proposer pendant 30 jours" && later->focusable,
              "UIA « Plus tard »: name, automation id, help");
        Check(make && make->name == L"Browser par défaut" && make->automationId == L"SettingsDefaultBrowser" && make->help == L"Ouvre les Paramètres Windows, où vous choisissez Browser comme navigateur par défaut",
              "UIA « Browser par défaut »: name, automation id, help says where it leads");
        bool question = false; for (const auto& n : nodes) question = question || n.name == L"Voulez-vous avoir Browser comme navigateur par défaut ?";
        Check(question, "UIA: the question is read");
        // Focus ring on a card button (keyboard), then captures of the still frame and two loop instants.
        dr.CardSecondsForTest(0); shot(dr, L"fr-card");
        Check(dr.CardShaderReadyForTest(), "the onboarding's halftone shader rendered the card (Direct3D 11)");
        dr.CardSecondsForTest(.8); dr.RenderForTest(shots + L"\\fr-card-loop-0.8s-144.png", 144, 731, root);
        dr.CardSecondsForTest(1.6); dr.RenderForTest(shots + L"\\fr-card-loop-1.6s-144.png", 144, 731, root);
        const auto f0 = Bytes(shots + L"\\fr-card-144.png"), f1 = Bytes(shots + L"\\fr-card-loop-0.8s-144.png"), f2 = Bytes(shots + L"\\fr-card-loop-1.6s-144.png");
        Check(!f0.empty() && f0 != f1 && f1 != f2 && f0 != f2, "the halftone moves: three loop instants, three different pictures");
        dr.CardSecondsForTest(3.2); dr.RenderForTest(shots + L"\\fr-card-loop-3.2s-144.png", 144, 731, root);
        Check(Bytes(shots + L"\\fr-card-loop-3.2s-144.png") == f0, "one full turn (3.2 s) gives back Paper's frame: a seamless loop");
        dr.CardSecondsForTest(-1);
        for (int i = 0; i < 40 && dr.FocusForTest() != SettingsUI::DefaultMake; ++i) dr.KeyForTest(VK_TAB);
        Check(dr.FocusForTest() == SettingsUI::DefaultMake, "Tab reaches « Browser par défaut »");
        dr.CardSecondsForTest(0); dr.RenderForTest(shots + L"\\fr-card-focus-144.png", 144, 731, root); dr.CardSecondsForTest(-1);
        // Actions.
        dr.ActivateForTest(SettingsUI::DefaultMake, false); dr.SettleForTest();
        Check(opened == 1 && dr.CardShownForTest(), "click « Browser par défaut »: Windows' Default apps page asked for, once; nothing changed by the drawer");
        dr.ActivateForTest(SettingsUI::DefaultMake, true); Check(opened == 2, "keyboard (Entrée / Espace / UIA): same action");
        // The person chose Browser in Windows, then came back: the card goes.
        status = 1; dr.RefreshDefaultBrowser(); dr.SettleForTest();
        Check(!dr.CardShownForTest() && dr.DefaultBrowserForTest() == 1, "back from Windows as the default: the card is gone");
        Check(dr.FocusForTest() != SettingsUI::DefaultMake, "the focus left the vanished button");
        Check(!dr.NextWakeForTest().delay || !dr.CardLoopsForTest(), "no card: its loop asks for no frame");
        dr.Hide(false, false); dr.Dispose();
    }
    {   // Already the default: no card, the footer is the version alone.
        status = 1; SettingsUI::Drawer dr; dr.Create(hi, owner, root, &st, hooks()); open(dr);
        Check(!dr.CardShownForTest(), "the default: no card");
        shot(dr, L"fr-default");
        dr.Hide(false, false); dr.Dispose();
    }
    {   // A copy Windows does not know (portable, dev): no card; no hook: no card.
        status = -1; SettingsUI::Drawer dr; dr.Create(hi, owner, root, &st, hooks()); open(dr);
        Check(!dr.CardShownForTest(), "not registered with Windows: no card");
        dr.Hide(false, false); dr.Dispose();
        SettingsUI::Drawer::Hooks h = hooks(); h.defaultBrowser = nullptr; SettingsUI::Drawer plain; plain.Create(hi, owner, root, &st, h); open(plain);
        Check(!plain.CardShownForTest(), "no hook: no card"); plain.Hide(false, false); plain.Dispose();
    }
    {   // « Plus tard »: 30 days away, stored in the settings; back after 30 days if still not the default.
        status = 0; laterSaves = 0; SettingsUI::Drawer dr; dr.Create(hi, owner, root, &st, hooks()); open(dr);
        for (int i = 0; i < 40 && dr.FocusForTest() != SettingsUI::DefaultLater; ++i) dr.KeyForTest(VK_TAB);
        Check(dr.FocusForTest() == SettingsUI::DefaultLater, "Tab reaches « Plus tard »");
        dr.ActivateForTest(SettingsUI::DefaultLater, true); dr.SettleForTest();
        Check(st.defaultBrowserLaterAt == clock && laterSaves == 1, "« Plus tard »: the moment is stored in the settings (saved by the host)");
        Check(!dr.CardShownForTest(), "« Plus tard »: the card goes at once");
        Check(dr.FocusForTest() == SettingsUI::CheckUpdates, "keyboard « Plus tard »: the focus goes on to the version's button");
        std::stringstream saved; BrowserSettings::Write(saved, st); const auto back = BrowserSettings::Read(saved);
        Check(back.defaultBrowserLaterAt == clock, "the answer survives a save and a read (default-browser-later=)");
        dr.Hide(false, false);
        clock += 29LL * 24 * 60 * 60; open(dr); Check(!dr.CardShownForTest(), "29 days later: still away"); dr.Hide(false, false);
        clock += 24LL * 60 * 60; open(dr); Check(dr.CardShownForTest(), "30 days later and still not the default: back");
        dr.Hide(false, false);
        st.defaultBrowserLaterAt = clock + 3600; open(dr); Check(!dr.CardShownForTest(), "a clock set back before the answer: still away"); dr.Hide(false, false);
        st.defaultBrowserLaterAt = 0; dr.Dispose();
    }
    {   // Motion: the halftone loops only while seen; reduced motion is Paper's still frame.
        status = 0; reduced = true; SettingsUI::Drawer dr; dr.Create(hi, owner, root, &st, hooks()); open(dr);
        dr.RenderForTest(shots + L"\\reduced-144.png", 144, 731, root);
        Check(!dr.CardLoopsForTest() && !dr.NextWakeForTest().delay, "reduced motion: no loop, no frame asked of the host");
        Check(Bytes(shots + L"\\reduced-144.png") == Bytes(shots + L"\\fr-card-144.png"), "reduced motion: Paper's still frame");
        dr.Hide(false, false); reduced = false; dr.Dispose();
        SettingsUI::Drawer live; live.Create(hi, owner, root, &st, hooks()); open(live);
        live.RenderForTest(shots + L"\\live-144.png", 144, 731, root);   // initialises the device
        if (live.CardLoopsForTest()) Check(live.NextWakeForTest().delay.has_value(), "open on the root, hardware device: the loop asks the host for frames");
        else std::printf("     (no hardware Direct3D device: one still frame, no loop)\n");
        live.Hide(false, false);
        Check(!live.CardLoopsForTest() && !live.NextWakeForTest().delay, "closed: no loop, no frame (no timer left running)");
        live.Dispose();
        SettingsUI::Drawer soft; soft.ForceSoftwareShaderForTest(); soft.Create(hi, owner, root, &st, hooks()); open(soft);
        soft.RenderForTest(shots + L"\\software-144.png", 144, 731, root);
        Check(soft.CardShaderReadyForTest() && !soft.CardLoopsForTest(), "software device (WARP): the card renders one still frame, never loops");
        soft.Hide(false, false); soft.Dispose();
    }
    I18n::SetForProcess(I18n::Locale::En);
    {
        status = 0; SettingsUI::Drawer dr; dr.Create(hi, owner, root, &st, hooks()); open(dr);
        const auto nodes = dr.AccessibleNodesForTest(); const auto* later = Node(nodes, SettingsUI::DefaultLater); const auto* make = Node(nodes, SettingsUI::DefaultMake);
        bool question = false; for (const auto& n : nodes) question = question || n.name == L"Make Browser your default browser?";
        Check(question && later && later->name == L"Later" && make && make->name == L"Make default", "English: « Make Browser your default browser? », « Later », « Make default »");
        Check(Near(dr.CardForTest().card.bottom - dr.CardForTest().card.top, 183), "English: one line, the card 20 shorter (no empty line)");
        dr.CardSecondsForTest(0); shot(dr, L"en-card");
        dr.Hide(false, false); dr.Dispose();
    }
    I18n::SetForProcess(I18n::Locale::Fr);
    DestroyWindow(owner);
    CoUninitialize();
    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
