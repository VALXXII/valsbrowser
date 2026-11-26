// Réglages › the root footer's layout switch and the « BETA » chip (src/settings-panel.h; Val's « Atelier » boards
// « 01 · Réglages — racine »: 26YE-0 at rest, 271F-0 « Panneau latéral » hovered, 274F-0 « Compact » hovered,
// 26V2-0 « Classique » hovered with the side panel selected).
//  - geometry: the control 4 DIP inside the column (16..316), 2 + 32 + 4 + 6 above the version line, segments 28 high;
//    the footer grows by 44 on the root only, and by 10 more while a label shows (the hairline rises);
//  - the label: which one, where (a 109 DIP box at the column's start / across it / at its end), the hover lane (120 ms),
//    reduced motion (at once), the keyboard focus shows it too;
//  - one source of truth: a segment sets « Position des onglets » (state + Change::TabsPosition, once), Réglages ›
//    Onglets' own control and the switch show the same selection;
//  - pointer: each segment, the gaps (nearest segment), outside; keyboard: Tab order (after the card, before the version
//    line), arrows move the focus inside the switch only, Entrée / Espace select; UIA: three named buttons, the selected
//    one marked, English names;
//  - « BETA »: the chip before the version on the root and on a sub-view; pictures at 100 / 150 % (rest, the three
//    hovers, keyboard focus), with pixel probes of the boards' colours.
// Windowless: the drawer is created hidden and never shown nor activated; pictures are drawn off screen.
// Usage: settings_layout_switch_test.exe <repo root> <capture folder>
#include <windows.h>
#include <wincodec.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "../src/chrome.h"
#include "../src/settings-panel.h"

using namespace NativeChrome;
static int failures = 0, checks = 0;
static void Check(bool ok, const char* what) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", what); } else std::printf("ok   %s\n", what); }
static bool Near(float a, float b, float e = .01f) { return a > b - e && a < b + e; }
static const Accessibility::Node* Node(const std::vector<Accessibility::Node>& nodes, int hit) { const long long key = SettingsUI::Drawer::UiaKey(hit); for (auto& n : nodes) if (n.key == key) return &n; return nullptr; }
struct Image { UINT w = 0, h = 0; std::vector<BYTE> px; int At(float xDip, float yDip, float scale) const { const UINT x = static_cast<UINT>(xDip * scale), y = static_cast<UINT>(yDip * scale); return x < w && y < h ? px[(static_cast<size_t>(y) * w + x) * 4 + 1] : -1; } };
static Image Load(const std::wstring& png) {
    Image img; IWICImagingFactory* f = nullptr; IWICBitmapDecoder* d = nullptr; IWICBitmapFrameDecode* fr = nullptr; IWICFormatConverter* cv = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f))) && SUCCEEDED(f->CreateDecoderFromFilename(png.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &d))
        && SUCCEEDED(d->GetFrame(0, &fr)) && SUCCEEDED(f->CreateFormatConverter(&cv)) && SUCCEEDED(cv->Initialize(fr, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))
        && SUCCEEDED(cv->GetSize(&img.w, &img.h))) { img.px.resize(static_cast<size_t>(img.w) * img.h * 4); if (FAILED(cv->CopyPixels(nullptr, img.w * 4, static_cast<UINT>(img.px.size()), img.px.data()))) img.px.clear(); }
    if (cv) cv->Release(); if (fr) fr->Release(); if (d) d->Release(); if (f) f->Release();
    return img;
}
static int Peak(const Image& img, float scale, Design::Box b) {
    int peak = 0; for (float y = b.top; y < b.bottom; y += 1 / scale) for (float x = b.left; x < b.right; x += 1 / scale) peak = std::max(peak, img.At(x, y, scale)); return peak;
}
static int Over(int base, float alpha) { return static_cast<int>(std::lround(base + (255 - base) * alpha)); }

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) { std::printf("usage: settings_layout_switch_test <repo root> <capture folder>\n"); return 2; }
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) { std::printf("FAIL native COM initialization\n"); return 1; }
    const std::wstring root = argv[1], shots = argv[2]; CreateDirectoryW(shots.c_str(), nullptr);
    HINSTANCE hi = GetModuleHandleW(nullptr);
    WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = hi; wc.lpszClassName = L"LayoutSwitchOwner"; RegisterClassW(&wc);
    HWND owner = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"owner", WS_POPUP, -32000, -32000, 1100, 860, nullptr, nullptr, hi, nullptr);

    BrowserSettings::State st; int changes = 0, positionChanges = 0; bool reduced = false;
    auto hooks = [&] {
        SettingsUI::Drawer::Hooks h;
        h.reducedMotion = [&] { return reduced; };
        h.version = [] { SettingsUI::Drawer::Version v; v.text = L"Browser 0.3.79"; v.canCheck = true; return v; };   // i18n-exempt: test data
        h.about = [](bool) {};
        h.changed = [&](BrowserSettings::Change c) { ++changes; if (c == BrowserSettings::Change::TabsPosition) ++positionChanges; };
        return h;
    };
    const float H = 731;   // the boards' height
    using SettingsUI::LayoutSwitchBase; using SettingsUI::Drawer;
    const int classic = LayoutSwitchBase, side = LayoutSwitchBase + 1, compact = LayoutSwitchBase + 2;

    {   // ---- Geometry, pointer, one source of truth, keyboard, UIA
        Drawer dr; Check(dr.Create(hi, owner, root, &st, hooks()), "drawer created (hidden)");
        Check(!IsWindowVisible(dr.Window()), "the drawer window is never shown by the test");
        dr.OpenWithoutWindowForTest();
        const float h = dr.HeightForTest(), version = h - 16 - 24;
        const auto c = dr.SwitchControlForTest();
        Check(Near(c.left, 16) && Near(c.right, 316) && Near(c.bottom, version - 6 - 4) && Near(c.bottom - c.top, 32), "control (Paper 270P-0): 16..316, 32 high, its 4 DIP margin + 6 above the version line");
        const auto s0 = dr.SwitchSegmentForTest(0), s1 = dr.SwitchSegmentForTest(1), s2 = dr.SwitchSegmentForTest(2);
        Check(Near(s0.left, 18) && Near(s2.right, 314) && Near(s0.bottom - s0.top, 28) && Near(s1.left - s0.right, 2) && Near(s2.left - s1.right, 2) && Near(s0.right - s0.left, s1.right - s1.left),
              "segments: padding 2, gap 2, three equal segments 28 high");
        Check(Near(dr.FooterHeightForTest(), 53 + 44), "root footer: 44 DIP more (2 + 32 + 4 + 6), hairline 12 above the control's margin");
        // Pointer.
        Check(dr.HitForTest((s0.left + s0.right) / 2, (s0.top + s0.bottom) / 2) == classic && dr.HitForTest((s1.left + s1.right) / 2, s1.top + 2) == side && dr.HitForTest(s2.right - 1, s2.bottom - 1) == compact, "hit test: each segment");
        Check(dr.HitForTest(s0.right + .5f, s0.top + 10) == classic && dr.HitForTest(s1.right + 1.5f, s1.top + 10) == compact && dr.HitForTest(c.left + .5f, c.top + .5f) == classic, "hit test: the gaps and padding go to the nearest segment (the label never blinks)");
        Check(dr.HitForTest(c.left - 1, s0.top + 10) != classic && dr.HitForTest(100, c.top - 1) < LayoutSwitchBase && dr.HitForTest(100, c.bottom + 1) < LayoutSwitchBase, "hit test: outside the control, not the switch");
        // One source of truth.
        dr.ActivateForTest(side, false);
        Check(st.tabsPosition == BrowserSettings::TabsPosition::Side && positionChanges == 1, "a click on « Panneau latéral »: the « Position des onglets » setting, Change::TabsPosition once");
        dr.ActivateForTest(side, false); Check(positionChanges == 1, "the selected one again: nothing changes");
        auto nodes = dr.AccessibleNodesForTest();
        Check(Node(nodes, side) && Node(nodes, side)->selected && !Node(nodes, classic)->selected && !Node(nodes, compact)->selected, "UIA: the selected segment follows the setting");
        dr.ActivateForTest(SettingsUI::TabsLayoutBase + 2, false);   // Réglages › Onglets' own control
        nodes = dr.AccessibleNodesForTest();
        Check(st.tabsPosition == BrowserSettings::TabsPosition::Compact && Node(nodes, compact)->selected && positionChanges == 2, "Réglages › Onglets sets it: the switch shows « Compact » (the same setting)");
        // UIA names (the labels), help, automation ids.
        const auto* a = Node(nodes, classic); const auto* b = Node(nodes, side); const auto* k = Node(nodes, compact);
        Check(a && b && k && a->role == Accessibility::Role::Button && a->name == L"Classique" && b->name == L"Panneau latéral" && k->name == L"Compact" && a->help == L"Position des onglets" && a->focusable,
              "UIA: three buttons « Classique », « Panneau latéral », « Compact », help « Position des onglets »");
        Check(a->automationId == L"SettingsLayoutClassic" && b->automationId == L"SettingsLayoutSide" && k->automationId == L"SettingsLayoutCompact", "UIA: automation ids");
        // Keyboard.
        const auto order = dr.FocusOrderForTest(); auto at = [&](int hit) { for (size_t i = 0; i < order.size(); ++i) if (order[i] == hit) return static_cast<int>(i); return -1; };
        Check(at(classic) >= 0 && at(side) == at(classic) + 1 && at(compact) == at(side) + 1 && at(SettingsUI::AboutVersion) == at(compact) + 1, "Tab order: …, Classique, Panneau latéral, Compact, the version line, the update button");
        for (int i = 0; i < 80 && dr.FocusForTest() != classic; ++i) dr.KeyForTest(VK_TAB);
        Check(dr.FocusForTest() == classic && dr.SwitchLabelForTest() == 0, "Tab reaches « Classique »: its label shows (keyboard focus)");
        dr.KeyForTest(VK_RIGHT); Check(dr.FocusForTest() == side && dr.SwitchLabelForTest() == 1, "→ : « Panneau latéral »");
        dr.KeyForTest(VK_RIGHT); dr.KeyForTest(VK_RIGHT); Check(dr.FocusForTest() == compact, "→ stops at « Compact » (never leaves the switch)");
        dr.KeyForTest(VK_LEFT); dr.KeyForTest(VK_LEFT); dr.KeyForTest(VK_LEFT); Check(dr.FocusForTest() == classic, "← stops at « Classique »");
        Check(st.tabsPosition == BrowserSettings::TabsPosition::Compact, "the arrows move the focus only");
        dr.KeyForTest(VK_RETURN); Check(st.tabsPosition == BrowserSettings::TabsPosition::Top && positionChanges == 3, "Entrée selects « Classique » (the top bar)");
        dr.KeyForTest(VK_RIGHT); dr.KeyForTest(VK_SPACE); Check(st.tabsPosition == BrowserSettings::TabsPosition::Side && positionChanges == 4, "Espace selects « Panneau latéral »");
        dr.KeyForTest(VK_TAB); Check(dr.FocusForTest() == compact, "Tab moves on to « Compact »"); dr.KeyForTest(VK_TAB); Check(dr.FocusForTest() == SettingsUI::AboutVersion && dr.SwitchLabelForTest() == -1, "then the version line: no label");
        // The label and the hairline: hover lane (120 ms), back at rest.
        dr.HoverForTest(side); Check(dr.SwitchLabelForTest() == 1, "hover « Panneau latéral »: its label");
        Sleep(200); Check(Near(dr.SwitchRevealForTest(), 1) && Near(dr.FooterHeightForTest(), 53 + 44 + 10), "hover: the hairline rises 10 DIP (Paper 273O-0: padding 4 + label 16 + 2), within 120 ms");
        dr.HoverForTest(compact); Check(dr.SwitchLabelForTest() == 2 && Near(dr.SwitchRevealForTest(), 1), "from one segment to the next: the hairline stays up");
        dr.HoverForTest(0); Sleep(200); Check(dr.SwitchLabelForTest() == -1 && Near(dr.FooterHeightForTest(), 97), "pointer away: back at rest");
        reduced = true; dr.SetReducedMotion(); dr.HoverForTest(classic); Check(Near(dr.SwitchRevealForTest(), 1), "reduced motion: at once"); dr.HoverForTest(0); Check(Near(dr.SwitchRevealForTest(), 0), "and back at once"); reduced = false; dr.SetReducedMotion();
        // Sub-views keep their footer (no switch), with the chip.
        dr.ActivateForTest(SettingsUI::CatTabs, false); dr.SettleForTest();
        Check(dr.CurrentViewForTest() == SettingsUI::View::Tabs && Near(dr.FooterHeightForTest(), 53), "Réglages › Onglets: no switch in its footer (root only)");
        nodes = dr.AccessibleNodesForTest(); Check(!Node(nodes, classic), "Réglages › Onglets: no switch in the UIA tree");
        const auto chip = dr.BetaForTest(); std::printf("     BETA chip %.2f..%.2f x %.2f..%.2f (drawer %.1f high)\n", chip.left, chip.right, chip.top, chip.bottom, dr.HeightForTest());
        Check(Near(chip.left, 20) && Near(chip.bottom - chip.top, 14) && Near((chip.top + chip.bottom) / 2, dr.HeightForTest() - 16 - 12) && chip.right - chip.left > 33 && chip.right - chip.left < 36,
              "« BETA » (Paper 2716-0): at the text's left edge (20), 14 high, centred on the version's slot, text + 4 + 4 (the board's plate measures 35)");
        Check(Near(SettingsUI::Layout::betaGap, 6) && Near(SettingsUI::Layout::betaRadius, 4), "« BETA »: radius 4, 6 DIP before the version");
        dr.Dispose();
    }
    {   // ---- Pictures (the boards' four states) and pixel probes
        st.tabsPosition = BrowserSettings::TabsPosition::Top;
        Drawer dr; dr.Create(hi, owner, root, &st, hooks()); dr.OpenWithoutWindowForTest();
        struct Shot { const wchar_t* name; int hover; BrowserSettings::TabsPosition selected; };
        const Shot shots_[] = {{L"rest", 0, BrowserSettings::TabsPosition::Top}, {L"hover-side", side, BrowserSettings::TabsPosition::Top},
                               {L"hover-compact", compact, BrowserSettings::TabsPosition::Top}, {L"hover-classic", classic, BrowserSettings::TabsPosition::Side}};
        for (const auto& s : shots_) {
            st.tabsPosition = s.selected; dr.HoverForTest(s.hover); Sleep(250); dr.SettleSwitchForTest();
            for (UINT dpi : {96u, 144u}) {
                const std::wstring file = shots + L"\\switch-" + s.name + L"-" + std::to_wstring(dpi * 100 / 96) + L".png";
                Check(dr.RenderForTest(file, dpi, H, root), "capture rendered");
                const auto img = Load(file); const float k = dpi / 96.f;
                const float version = H - 16 - 24, ctop = version - 10 - 32, hairline = H - 97 - (s.hover ? 10 : 0);
                const int canvas = 0x17, control = Over(canvas, 0x0A / 255.f), selected = Over(control, 0x1F / 255.f), hovered = Over(control, 0x0A / 255.f);
                const int sel = static_cast<int>(s.selected), hv = s.hover ? s.hover - LayoutSwitchBase : -1;
                auto segX = [&](int i) { return 18 + i * (292.f / 3 + 2) + 6; };   // inside segment i, clear of its icon
                if (dpi == 144) std::printf("     %ls: control %d (exp %d), selected %d (exp %d), hovered %d (exp %d), hairline %d\n", s.name, img.At(18 + 292.f / 3 + 1, ctop + 16, k), control, img.At(segX(sel), ctop + 8, k), selected,
                                            hv >= 0 && hv != sel ? img.At(segX(hv), ctop + 8, k) : -1, hovered, img.At(100, hairline + .3f, k));
                Check(std::abs(img.At(18 + 292.f / 3 + 1, ctop + 16, k) - control) <= 1 && std::abs(img.At(segX(sel), ctop + 8, k) - selected) <= 1, "picture: the control white 4 %, the selected segment white 12 % (#FFFFFF1F)");
                if (hv >= 0 && hv != sel) Check(std::abs(img.At(segX(hv), ctop + 8, k) - hovered) <= 1, "picture: the hovered segment white 4 % (#FFFFFF0A)");
                Check(img.At(100, hairline + .3f, k) > canvas && img.At(100, hairline + 1.6f, k) == canvas, "picture: the footer's hairline where the board draws it");
                const float labelTop = ctop - 2 - 2 - 16; const Design::Box labelBox{12, labelTop, 320, labelTop + 16};
                const int peak = std::max(Peak(img, k, {labelBox.left, labelBox.top, labelBox.right, H - 97 - 1}), Peak(img, k, {labelBox.left, H - 97 + 2, labelBox.right, labelBox.bottom}));   // the rest hairline (H - 97) crosses the box
                if (s.hover) Check(peak > canvas + 60 && peak <= Over(canvas, .5f) + 3, "picture: the label at white 50 % (#FFFFFF80)");
                else Check(peak <= canvas + 1, "picture: no label at rest");
                const auto chip = dr.BetaForTest();
                Check(std::abs(img.At(chip.left + 1.5f, version + 5 + 1.5f, k) - control) <= 1, "picture: the « BETA » plate white 4 %");
            }
        }
        // Where each label sits: its ink inside its 109 DIP box (start / across / end).
        {   const auto side96 = Load(shots + L"\\switch-hover-side-100.png"), compact96 = Load(shots + L"\\switch-hover-compact-100.png"), classic96 = Load(shots + L"\\switch-hover-classic-100.png");
            const float top = H - 40 - 10 - 32 - 4 - 16;
            auto ink = [&](const Image& img, float& l, float& r) { l = 1e9f; r = -1; for (float x = 0; x < 332; x += 1) for (float y = top; y < top + 16; y += 1) if (img.At(x, y, 1) > 0x17 + 40) { l = std::min(l, x); r = std::max(r, x); } };
            float l, r; ink(side96, l, r); Check(std::fabs((l + r) / 2 - 166) <= 2, "« Panneau latéral »: centred over the column (Paper 273R-0)");
            ink(compact96, l, r); Check(std::fabs((l + r) / 2 - (320 - 54.5f)) <= 2 && r <= 320, "« Compact »: centred in a 109 DIP box at the column's end (Paper 276R-0)");
            ink(classic96, l, r); Check(std::fabs((l + r) / 2 - (12 + 54.5f)) <= 2 && l >= 12, "« Classique »: centred in a 109 DIP box at the column's start (Paper 271E-0)");
        }
        // Keyboard focus picture: the ring on the focused segment and its label.
        st.tabsPosition = BrowserSettings::TabsPosition::Top; dr.HoverForTest(0); Sleep(250);
        for (int i = 0; i < 80 && dr.FocusForTest() != compact; ++i) dr.KeyForTest(VK_TAB);
        dr.SettleSwitchForTest();
        Check(dr.RenderForTest(shots + L"\\switch-focus-compact-150.png", 144, H, root), "keyboard focus capture rendered");
        {   const auto img = Load(shots + L"\\switch-focus-compact-150.png"); const auto s2 = dr.SwitchSegmentForTest(2);
            Check(img.At(s2.left + .4f, H - 40 - 10 - 16, 1.5f) > 0x50, "keyboard focus: the drawer's ring on « Compact »"); }
        dr.Dispose();
    }
    {   // English labels
        I18n::SetForProcess(I18n::Locale::En);
        Drawer dr; dr.Create(hi, owner, root, &st, hooks()); dr.OpenWithoutWindowForTest();
        const auto nodes = dr.AccessibleNodesForTest();
        Check(Node(nodes, classic)->name == L"Classic" && Node(nodes, side)->name == L"Side panel" && Node(nodes, compact)->name == L"Compact" && Node(nodes, side)->help == L"Tab position", "English: « Classic », « Side panel », « Compact »");
        dr.HoverForTest(classic); Sleep(250); dr.SettleSwitchForTest();
        Check(dr.RenderForTest(shots + L"\\switch-hover-classic-en-100.png", 96, H, root), "English capture rendered");
        dr.Dispose(); I18n::SetForProcess(I18n::Locale::Fr);
    }
    DestroyWindow(owner);
    CoUninitialize();
    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
