// Réglages › the version line opens « À propos de Browser » (src/settings-panel.h; Paper « Browser — référence unifiée »,
// « À propos : point d'entrée » variant A: plate 1V0L-0, tooltip 1V0O-0). The line is plain text at rest; hovered it gets
// the white 4 % plate (274 x 40, radius 6) and the 12 DIP chevron at white 50 %; a click, Entrée or Espace calls the host
// (which closes the drawer and opens the A8 window); Tab reaches it before the update button; UIA reads it as a button.
// Windowless: the drawer is created hidden and never shown nor activated (no focus taken); pictures are drawn off screen
// (Direct2D + WIC). Synthetic hooks: nothing on the machine is read or written.
// Usage: settings_about_test.exe <repo root> <capture folder>
#include <windows.h>
#include <wincodec.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "../src/chrome.h"
#include "../src/settings-panel.h"

using namespace NativeChrome;
static int failures = 0, checks = 0;
static void Check(bool ok, const char* what) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", what); } else std::printf("ok   %s\n", what); }
static bool Near(float a, float b, float e = .01f) { return a > b - e && a < b + e; }
static std::string Bytes(const std::wstring& path) { std::ifstream f(path.c_str(), std::ios::binary); return std::string(std::istreambuf_iterator<char>(f), {}); }
static const Accessibility::Node* Node(const std::vector<Accessibility::Node>& nodes, int hit) { const long long key = SettingsUI::Drawer::UiaKey(hit); for (auto& n : nodes) if (n.key == key) return &n; return nullptr; }

// PNG pixels (BGRA) through WIC.
struct Image { UINT w = 0, h = 0; std::vector<BYTE> px; int At(float xDip, float yDip, float scale) const { const UINT x = static_cast<UINT>(xDip * scale), y = static_cast<UINT>(yDip * scale); return x < w && y < h ? px[(static_cast<size_t>(y) * w + x) * 4 + 1] : -1; } };
static Image Load(const std::wstring& png) {
    Image img; IWICImagingFactory* f = nullptr; IWICBitmapDecoder* d = nullptr; IWICBitmapFrameDecode* fr = nullptr; IWICFormatConverter* cv = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f))) && SUCCEEDED(f->CreateDecoderFromFilename(png.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &d))
        && SUCCEEDED(d->GetFrame(0, &fr)) && SUCCEEDED(f->CreateFormatConverter(&cv)) && SUCCEEDED(cv->Initialize(fr, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))
        && SUCCEEDED(cv->GetSize(&img.w, &img.h))) { img.px.resize(static_cast<size_t>(img.w) * img.h * 4); if (FAILED(cv->CopyPixels(nullptr, img.w * 4, static_cast<UINT>(img.px.size()), img.px.data()))) img.px.clear(); }
    if (cv) cv->Release(); if (fr) fr->Release(); if (d) d->Release(); if (f) f->Release();
    return img;
}
// Brightest green channel in a DIP box (the canvas is 0x17).
static int Peak(const Image& img, float scale, Design::Box b) {
    int peak = 0;
    for (float y = b.top; y < b.bottom; y += 1 / scale) for (float x = b.left; x < b.right; x += 1 / scale) peak = std::max(peak, img.At(x, y, scale));
    return peak;
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) { std::printf("usage: settings_about_test <repo root> <capture folder>\n"); return 2; }
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) { std::printf("FAIL native COM initialization\n"); return 1; }
    const std::wstring root = argv[1], shots = argv[2];
    CreateDirectoryW(shots.c_str(), nullptr);
    HINSTANCE hi = GetModuleHandleW(nullptr);
    WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = hi; wc.lpszClassName = L"SettingsAboutOwner"; RegisterClassW(&wc);
    // Hidden owner (never shown); the drawer is created hidden too and never shown: nothing on screen, no focus taken.
    HWND owner = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"owner", WS_POPUP, -32000, -32000, 1100, 860, nullptr, nullptr, hi, nullptr);

    int opened = 0, openedByKeyboard = 0;
    const std::wstring versionText = L"Browser 0.3.412 · À jour";
    auto hooks = [&](bool about) {
        SettingsUI::Drawer::Hooks h;
        h.reducedMotion = [] { return false; };
        h.version = [versionText] { SettingsUI::Drawer::Version v; v.text = versionText; v.canCheck = true; return v; };
        if (about) h.about = [&](bool keyboard) { ++opened; if (keyboard) ++openedByKeyboard; };
        return h;
    };
    BrowserSettings::State st;
    const float H = 731;   // the captures' page height (DIP), as tests/default_browser_settings_test.cpp
    const float scale96 = 1, scale144 = 1.5f;

    {   // Geometry (Paper 1V0L-0) at the drawer's own height.
        SettingsUI::Drawer dr; Check(dr.Create(hi, owner, root, &st, hooks(true)), "drawer created (hidden)");
        Check(!IsWindowVisible(dr.Window()), "the drawer window is never shown by the test");
        const float h = dr.HeightForTest(), y = h - 16 - 24;
        const auto row = dr.AboutForTest(), chevron = dr.AboutChevronForTest();
        Check(Near(row.left, 8) && Near(row.right, 282) && Near(row.top, y - 8) && Near(row.bottom, y + 32), "plate: x 8, 274 x 40, 8 DIP around the version's 24 DIP slot");
        Check(Near(row.bottom, h - 8), "plate: 8 DIP above the drawer's bottom (Paper: 652 + 40 in 700)");
        Check(Near(chevron.right - chevron.left, 12) && Near(chevron.bottom - chevron.top, 12) && Near(chevron.right, row.right - 10) && Near((chevron.top + chevron.bottom) / 2, (row.top + row.bottom) / 2),
              "chevron: 12 x 12, 10 DIP from the plate's right edge, centred");
        const auto update = SettingsUI::Layout::Trailing(y, SettingsUI::Layout::slot);
        Check(Near(update.left - row.right, 10), "the update button stays outside, 10 DIP to the right of the plate");
        // Pointer: the real hit test.
        Check(dr.HitForTest((row.left + row.right) / 2, (row.top + row.bottom) / 2) == SettingsUI::AboutVersion, "hit test: the middle of the line opens À propos");
        Check(dr.HitForTest(row.left + 1, row.top + 1) == SettingsUI::AboutVersion && dr.HitForTest(row.right - 1, row.bottom - 1) == SettingsUI::AboutVersion, "hit test: the plate's corners");
        Check(dr.HitForTest((update.left + update.right) / 2, (update.top + update.bottom) / 2) == SettingsUI::CheckUpdates, "hit test: the update button is still its own");
        Check(dr.HitForTest(4, (row.top + row.bottom) / 2) != SettingsUI::AboutVersion && dr.HitForTest(100, row.top - 2) != SettingsUI::AboutVersion, "hit test: outside the plate, nothing");
        // Keyboard: Tab order and activation.
        const auto order = dr.FocusOrderForTest(); auto at = [&](int hit) { for (size_t i = 0; i < order.size(); ++i) if (order[i] == hit) return static_cast<int>(i); return -1; };
        Check(at(SettingsUI::AboutVersion) >= 0 && at(SettingsUI::CheckUpdates) == at(SettingsUI::AboutVersion) + 1, "Tab order: … the version line, then the update button");
        for (int i = 0; i < 60 && dr.FocusForTest() != SettingsUI::AboutVersion; ++i) dr.KeyForTest(VK_TAB);
        Check(dr.FocusForTest() == SettingsUI::AboutVersion, "Tab reaches the version line");
        dr.KeyForTest(VK_RETURN);
        Check(opened == 1 && openedByKeyboard == 1, "Entrée opens À propos (the host is told it was the keyboard)");
        dr.FocusForTest(SettingsUI::AboutVersion, true); dr.KeyForTest(VK_SPACE);
        Check(opened == 2 && openedByKeyboard == 2, "Espace opens it too");
        dr.ActivateForTest(SettingsUI::AboutVersion, false);
        Check(opened == 3 && openedByKeyboard == 2, "a click opens it (pointer)");
        // Screen readers: one button, named, the version as help.
        dr.OpenWithoutWindowForTest();
        const auto nodes = dr.AccessibleNodesForTest(); const auto* n = Node(nodes, SettingsUI::AboutVersion);
        Check(n && n->role == Accessibility::Role::Button && n->name == L"À propos de Browser" && n->help == versionText && n->automationId == L"SettingsAbout" && n->focusable,
              "UIA: a button « À propos de Browser », the version and its state as help, automation id SettingsAbout");
        bool loose = false; for (const auto& k : nodes) loose = loose || (k.name == versionText && k.key != SettingsUI::Drawer::UiaKey(SettingsUI::AboutVersion));
        Check(!loose, "UIA: the version is not read twice (no separate text node)");
        // Tooltip (Paper 1V0O-0).
        const auto tip = dr.TipForTest(SettingsUI::AboutVersion);
        const float px = dr.RenderDpiForTest() / 96.f;
        Check(tip.second == L"À propos de Browser", "tooltip: « À propos de Browser »");
        Check(std::abs((tip.first.right - tip.first.left) - 274 * px) <= 1 && std::abs((tip.first.bottom - tip.first.top) - 40 * px) <= 1, "tooltip: over the whole plate");
        dr.Dispose();
    }
    {   // Pictures at Paper's sizes: at rest nothing new, hover = plate + chevron, keyboard focus = ring + chevron.
        SettingsUI::Drawer with; with.Create(hi, owner, root, &st, hooks(true));
        SettingsUI::Drawer without; without.Create(hi, owner, root, &st, hooks(false));
        without.OpenWithoutWindowForTest();
        Check(without.HitForTest(100, without.HeightForTest() - 28) != SettingsUI::AboutVersion, "no host hook: the line is plain text (no hit)");
        auto order = without.FocusOrderForTest(); Check(std::find(order.begin(), order.end(), SettingsUI::AboutVersion) == order.end(), "no host hook: not a Tab stop");
        const auto nodes = without.AccessibleNodesForTest(); bool text = false; for (const auto& k : nodes) text = text || (k.name == versionText && k.role == Accessibility::Role::Text);
        Check(!Node(nodes, SettingsUI::AboutVersion) && text, "no host hook: UIA reads the version as text, no button");
        for (UINT dpi : {96u, 144u}) {
            const std::wstring tag = std::to_wstring(dpi), rest = shots + L"\\about-rest-" + tag + L".png", plain = shots + L"\\about-plain-" + tag + L".png";
            Check(with.RenderForTest(rest, dpi, H, root) && without.RenderForTest(plain, dpi, H, root), "captures rendered");
            Check(!Bytes(rest).empty() && Bytes(rest) == Bytes(plain), "at rest: the same picture as before the change (nothing new on screen)");
        }
        const float y = H - 16 - 24; const Design::Box row{8, y - 8, 282, y + 32}, chevron{260, y + 6, 272, y + 18};
        const int canvas = 0x17, plate = static_cast<int>(std::lround((Design::Surface::canvas + (1 - Design::Surface::canvas) * .04f) * 255));
        {   const auto img = Load(shots + L"\\about-rest-144.png");
            Check(img.At(12, y + 30, scale144) == canvas && Peak(img, scale144, chevron) <= canvas + 1, "at rest: no plate, no chevron"); }
        with.HoverForTest(SettingsUI::AboutVersion); Sleep(400);   // the 120 ms hover fade, settled
        for (UINT dpi : {96u, 144u}) {
            const std::wstring tag = std::to_wstring(dpi), file = shots + L"\\about-hover-" + tag + L".png";
            Check(with.RenderForTest(file, dpi, H, root), "hover capture rendered");
            const auto img = Load(file); const float s = dpi == 96 ? scale96 : scale144;
            std::printf("     %u dpi: plate %d (expected %d, white 4 %% over #171717), chevron peak %d (white 50 %% = %d)\n", dpi, img.At(12, y + 30, s), plate, Peak(img, s, chevron), canvas + (255 - canvas) / 2);
            Check(std::abs(img.At(12, y + 30, s) - plate) <= 1 && std::abs(img.At(row.right - 3, row.top + 3, s) - plate) <= 1, "hover: the white 4 % plate");
            Check(img.At(row.left - 2, y + 12, s) == canvas && img.At(row.right + 4, y + 30, s) == canvas, "hover: nothing outside the 274 x 40 plate");
            const int peak = Peak(img, s, chevron);
            Check(peak > canvas + 80 && peak <= plate + (255 - plate) / 2 + 2, "hover: the chevron at white 50 % (never brighter)");
            Check(Peak(img, s, {chevron.right + 1, chevron.top, row.right, chevron.bottom}) <= plate + 1, "hover: the chevron ends 10 DIP inside the plate");
        }
        with.HoverForTest(0); Sleep(400);
        with.FocusForTest(SettingsUI::AboutVersion, true);
        Check(with.RenderForTest(shots + L"\\about-focus-144.png", 144, H, root), "keyboard focus capture rendered");
        {   const auto img = Load(shots + L"\\about-focus-144.png");
            Check(img.At(row.left + .4f, y + 12, scale144) > 0x60 && img.At(row.right - .4f, y + 12, scale144) > 0x60, "keyboard focus: the drawer's 1 DIP ring around the plate");
            Check(Peak(img, scale144, chevron) > canvas + 80, "keyboard focus: the chevron shows (where Entrée leads)"); }
        with.Dispose(); without.Dispose();
    }
    {   // English: the name and the tooltip follow the interface language.
        I18n::SetForProcess(I18n::Locale::En);
        SettingsUI::Drawer dr; dr.Create(hi, owner, root, &st, hooks(true)); dr.OpenWithoutWindowForTest();
        const auto nodes = dr.AccessibleNodesForTest(); const auto* n = Node(nodes, SettingsUI::AboutVersion);
        Check(n && n->name == L"About Browser" && dr.TipForTest(SettingsUI::AboutVersion).second == L"About Browser", "English: « About Browser » (UIA name and tooltip)");
        dr.Dispose(); I18n::SetForProcess(I18n::Locale::Fr);
    }
    DestroyWindow(owner);
    CoUninitialize();
    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
