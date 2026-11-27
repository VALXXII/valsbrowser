// « À propos de Browser » B (src/modal.h Kind::About + src/about-icons.h; Paper « Browser — référence unifiée » 257B-0
// « À propos — B (validée) »): layout values, the six update states of the card, tile actions (Entrée, Espace, a real
// click message), the Tab order, the focus styles (keyboard 50 %, mouse click 5 %), French and English texts, the
// profile colours of the glass icons and of the button for two profiles, the accessibility tree.
// Screen-free: the dialog is laid out and painted with ShowHiddenForTest (never shown, activated or brought forward; its
// owner is hidden too); pictures are drawn off screen (Direct2D + WIC). Nothing on the machine is read but the repo.
// Usage: about_window_test.exe <repo root> <capture folder>
#include <windows.h>
#include <wincodec.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
#include "../src/chrome.h"
#include "../src/modal.h"

using namespace NativeChrome;
using D = Modal::Dialog;
namespace A = Modal::AboutPaper;
static int failures = 0, checks = 0;
static void Check(bool ok, const char* what) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", what); } else std::printf("ok   %s\n", what); }
static bool Near(float a, float b, float e = .01f) { return a > b - e && a < b + e; }
static bool Box(D2D1_RECT_F r, float l, float t, float rr, float b) { return Near(r.left, l) && Near(r.top, t) && Near(r.right, rr) && Near(r.bottom, b); }
static void PrintBox(const char* name, D2D1_RECT_F r) { std::printf("     %s: %.2f, %.2f, %.2f, %.2f (%.2f x %.2f)\n", name, r.left, r.top, r.right, r.bottom, r.right - r.left, r.bottom - r.top); }

// PNG pixels through WIC (BGRA).
struct Image {
    UINT w = 0, h = 0; std::vector<BYTE> px;
    bool At(float xDip, float yDip, float scale, int& r, int& g, int& b) const {
        const long x = std::lround(std::floor(xDip * scale)), y = std::lround(std::floor(yDip * scale));
        if (x < 0 || y < 0 || x >= static_cast<long>(w) || y >= static_cast<long>(h)) return false;
        const size_t i = (static_cast<size_t>(y) * w + static_cast<size_t>(x)) * 4; b = px[i]; g = px[i + 1]; r = px[i + 2]; return true;
    }
    int Green(float x, float y, float s) const { int r = 0, g = -1, b = 0; At(x, y, s, r, g, b); return g; }
};
static Image Load(const std::wstring& png) {
    Image img; IWICImagingFactory* f = nullptr; IWICBitmapDecoder* d = nullptr; IWICBitmapFrameDecode* fr = nullptr; IWICFormatConverter* cv = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f))) && SUCCEEDED(f->CreateDecoderFromFilename(png.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &d))
        && SUCCEEDED(d->GetFrame(0, &fr)) && SUCCEEDED(f->CreateFormatConverter(&cv)) && SUCCEEDED(cv->Initialize(fr, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))
        && SUCCEEDED(cv->GetSize(&img.w, &img.h))) { img.px.resize(static_cast<size_t>(img.w) * img.h * 4); if (FAILED(cv->CopyPixels(nullptr, img.w * 4, static_cast<UINT>(img.px.size()), img.px.data()))) img.px.clear(); }
    if (cv) cv->Release(); if (fr) fr->Release(); if (d) d->Release(); if (f) f->Release();
    return img;
}
// The most saturated pixel of a DIP box (OKLCH chroma), as its OKLCH.
static AboutIcons::Oklch Vivid(const Image& img, float s, D2D1_RECT_F b) {
    AboutIcons::Oklch best{};
    for (float y = b.top; y < b.bottom; y += 1 / s) for (float x = b.left; x < b.right; x += 1 / s) {
        int r, g, bl; if (!img.At(x, y, s, r, g, bl)) continue;
        const auto k = AboutIcons::ToOklch(r / 255.f, g / 255.f, bl / 255.f); if (k.c > best.c) best = k;
    }
    return best;
}
static float HueGap(double a, double b) { double d = std::fabs(a - b); return static_cast<float>(d > 180 ? 360 - d : d); }
static bool Hex(const D2D1_COLOR_F& c, unsigned rgb, float alpha) {
    auto v = [](float f) { return static_cast<unsigned>(std::lround(f * 255)); };
    return ((v(c.r) << 16) | (v(c.g) << 8) | v(c.b)) == rgb && std::fabs(c.a - alpha) < .001f;
}
static const Accessibility::Node* Node(const std::vector<Accessibility::Node>& nodes, long long key) { for (auto& n : nodes) if (n.key == key) return &n; return nullptr; }
static void Profile(unsigned fill, unsigned ink) { Design::Accent::Set(fill); Design::Accent::SetInk(ink); }

// The card's states, built exactly as the host builds them from the update engine (src/main.cpp AboutStatusNow).
static Modal::Status State(int which) {
    namespace MD = Modal;
    switch (which) {
    case 0: return MD::AboutStatus(MD::AboutUpToDate, TR(updates_last_check_ok));                                     // à jour
    case 1: return MD::AboutStatus(MD::AboutChecking);                                                                   // recherche
    case 2: { auto st = MD::AboutStatus(MD::AboutUpToDate); st.icon = MD::StatusIcon::Download; st.title = TRF(updates_version_available, L"0.3.133"); st.detail = TR(updates_download_on_request);   // disponible
        st.button = MD::Make(MD::AboutDownloadButton, TR(updates_download), MD::Role::Primary, false, false, false, L"AboutDownload"); st.button.closes = false; return st; }
    case 3: { auto st = MD::AboutStatus(MD::AboutChecking); st.title = TRF(updates_downloading_version, L"0.3.133"); st.detail = L"42 %";   // téléchargement
        st.button = MD::Make(MD::AboutCancelButton, TR(common_cancel), MD::Role::Quiet, false, false, false, L"AboutCancel"); st.button.closes = false; st.hasButton = true; return st; }
    case 4: { auto st = MD::AboutStatus(MD::AboutReady, L"", L"0.3.133"); st.button.closes = false; return st; }        // prête (Paper's card)
    default: return MD::AboutStatus(MD::AboutOffline, TR(updates_check_failed));                                         // erreur
    }
}
static const char* StateName(int i) { static const char* n[] = {"up-to-date", "checking", "available", "downloading", "ready", "error"}; return n[i]; }
static std::wstring WState(int i) { const char* n = StateName(i); return std::wstring(n, n + std::strlen(n)); }

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) { std::printf("usage: about_window_test <repo root> <capture folder>\n"); return 2; }
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) { std::printf("FAIL COM\n"); return 1; }
    const std::wstring root = argv[1], shots = argv[2];
    CreateDirectoryW(shots.c_str(), nullptr);
    I18n::SetForProcess(I18n::Locale::Fr);
    HINSTANCE hi = GetModuleHandleW(nullptr);
    WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = hi; wc.lpszClassName = L"AboutWindowOwner"; RegisterClassW(&wc);
    HWND owner = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"owner", WS_POPUP, -32000, -32000, 1200, 900, nullptr, nullptr, hi, nullptr);   // never shown
    const HWND foreground = GetForegroundWindow();
    D d; Check(d.Create(hi, owner, root, {}), "dialog created (hidden)");
    const std::wstring logo = root + L"\\src\\icons\\browser-live.ico";   // Paper's header shows the live icon
    const RECT area{0, 0, 1200, 900};
    Profile(0x1A8641, 0x6FD087);   // Paper's « Travail » (fill / ink)

    auto open = [&](int state, std::function<void(const Modal::Result&)> done = nullptr, std::function<void(int)> onButton = nullptr) {
        auto spec = Modal::AboutSpec(L"0.3.132", Modal::AboutUpToDate); spec.status = State(state); spec.about.logo = logo;
        spec.onButton = onButton ? onButton : [](int) {};
        const bool ok = d.ShowHiddenForTest(spec, area, done ? done : [](const Modal::Result&) {});
        d.FinishAnimationsForTest(); d.AssumeFocusForTest(false); return ok;
    };
    auto close = [&] { d.Abort(); d.FinishAnimationsForTest(); };

    {   // Texts (Paper 257B-0, French).
        const auto s = Modal::AboutSpec(L"0.3.132", Modal::AboutReady, L"", L"0.3.133");
        Check(s.kind == Modal::Kind::About && s.about.shown && s.width == 420 && s.title == L"Browser" && s.about.version == L"Version 0.3.132 · Windows 64 bits", "header: « Browser », « Version 0.3.132 · Windows 64 bits »");
        Check(s.status.title == L"Version 0.3.133 disponible" && s.status.detail == L"S'installe au redémarrage" && s.status.button.label == L"Redémarrer" && s.status.button.role == Modal::Role::Primary && s.status.icon == Modal::StatusIcon::Download,
              "card (Paper 257J-0): « Version 0.3.133 disponible », « S'installe au redémarrage », « Redémarrer » in the profile's fill, download icon");
        const auto up = Modal::AboutStatus(Modal::AboutUpToDate);
        Check(up.title == L"Browser est à jour" && up.button.label == L"Rechercher" && up.button.role != Modal::Role::Primary && up.icon == Modal::StatusIcon::Check, "up to date: check, « Browser est à jour », secondary « Rechercher »");
        Check(s.links.size() == 5 && s.links[0].label == L"Nouveautés" && s.links[1].label == L"Code source" && s.links[2].label == L"Site web" && s.links[3].label == L"Signaler un problème" && s.links[4].label == L"Licences des composants tiers",
              "tiles and footer link: Nouveautés, Code source, Site web, Signaler un problème, Licences des composants tiers");
        Check(s.links[0].id == Modal::AboutNotesLink && s.links[1].id == Modal::AboutSourceLink && s.links[2].id == Modal::AboutWebsiteLink && s.links[3].id == Modal::AboutReportLink && s.links[4].id == Modal::AboutLicensesLink, "each tile answers its own id");
        Check(s.about.tiles.size() == 4 && s.about.tiles[0].subtitle == L"Notes de la 0.3.132" && s.about.tiles[1].subtitle == L"Logiciel libre · GitHub ↗" && s.about.tiles[2].subtitle == L"valsbrowser.com ↗" && s.about.tiles[3].subtitle == L"Sur GitHub ↗",
              "subtitles: « Notes de la 0.3.132 », « Logiciel libre · GitHub ↗ », « valsbrowser.com ↗ », « Sur GitHub ↗ »");
        Check(s.about.tiles[0].icon == &AboutIcons::gift && s.about.tiles[1].icon == &AboutIcons::terminal && s.about.tiles[2].icon == &AboutIcons::compass && s.about.tiles[3].icon == &AboutIcons::bug, "icons: gift, terminal, compass, bug (glass)");
        Check(s.about.copyright == L"© 2026 Val · WebView2", "footer: « © 2026 Val · WebView2 »");
        Check(s.about.tiles[1].spoken == L"Logiciel libre · GitHub" && s.about.tiles[0].spoken == L"Notes de la 0.3.132", "screen readers: the subtitle without its ↗");
    }
    {   // Layout (Paper 257B-0, every value in DIP).
        Check(open(4), "opened hidden (ready)");
        Check(!IsWindowVisible(d.Window()) && !IsWindowVisible(d.Scrim()) && GetForegroundWindow() == foreground, "nothing shown, the foreground untouched");
        PrintBox("window", {0, 0, d.WidthForTest(), d.HeightForTest()});
        Check(Near(d.WidthForTest(), 420) && Near(d.HeightForTest(), 424), "window: 420 x 424 (1 px border, header 91, card 55, tiles 2 x 96, footer)");
        const auto head = d.AboutBlockForTest(0), card = d.AboutBlockForTest(1), foot = d.AboutBlockForTest(2);
        PrintBox("header", head); PrintBox("card", card); PrintBox("footer", foot);
        Check(Box(head, 1, 1, 419, 91), "header: inside the 1 px border, 20 + 52 + 18 high, hairline under it");
        Check(Box(d.BoxForTest(D::CloseHit), 383, 17, 407, 41), "×: 16 DIP glyph at 387,21 (padding 20 top / 16 right, top-aligned), 24 DIP target");
        Check(Box(card, 17, 106, 403, 161), "card: 14 under the header, 16 from the sides, 10 + 35 + 10 high");
        const auto btn = d.BoxForTest(D::StatusHit); PrintBox("button", btn);
        Check(Near(btn.right, 393) && Near(btn.bottom - btn.top, 28) && Near(btn.top, 119.5f) && btn.right - btn.left > 60, "button: 28 high, padding 10 from the card's right, centred");
        const auto t0 = d.BoxForTest(D::LinkHit(0)), t1 = d.BoxForTest(D::LinkHit(1)), t2 = d.BoxForTest(D::LinkHit(2)), t3 = d.BoxForTest(D::LinkHit(3));
        PrintBox("tile 1", t0); PrintBox("tile 4", t3);
        Check(Box(t0, 17, 175, 206, 271) && Box(t1, 214, 175, 403, 271) && Box(t2, 17, 279, 206, 375) && Box(t3, 214, 279, 403, 375), "tiles: 2 x 2, gap 8, padding 14 / 16 / 6, 189 x 96 (12 + 26 + 6 + 18 + 6 + 16 + 12)");
        Check(Box(foot, 21, 391, 399, 407), "footer: padding 10 / 20 / 16, one 16 line");
        const auto lic = d.BoxForTest(D::LinkHit(4)); PrintBox("licences", lic);
        Check(Near(lic.left, 21) && Near(lic.top, 391) && Near(lic.bottom, 407) && lic.right > 150 && lic.right < 260, "licences link: left of the footer, its own width");
        for (UINT dpi : {96u, 120u, 144u, 192u}) Check(d.RenderForTest(shots + L"\\about-ready-" + std::to_wstring(dpi) + L".png", dpi), "render ready (Paper's state)");
        Check(d.IconsFrostedForTest(), "glass icons: the frosted layer is a real Gaussian blur (Direct2D effect)");
        Check(d.AboutLogoForTest(), "header logo: the application icon is drawn");
        const auto img = Load(shots + L"\\about-ready-96.png");
        Check(img.w == 420 && img.h == 424, "render 96: 420 x 424 pixels");
        int r, g, b;
        Check(img.At(200, 80, 1, r, g, b) && r == 0x1C && g == 0x1C && b == 0x1C, "header: #1C1C1C");
        Check(img.At(200, 91, 1, r, g, b) && r == 0x26 && g == 0x26 && b == 0x26, "header hairline: #262626");
        Check(img.At(200, 95, 1, r, g, b) && r == 0x17 && g == 0x17 && b == 0x17, "window background: #171717");
        Check(img.At(20, 140, 1, r, g, b) && r == 0x20 && g == 0x20 && b == 0x20, "card: #202020");
        Check(img.At(190, 266, 1, r, g, b) && r == 0x20 && g == 0x20 && b == 0x20, "tile: #202020");
        Check(img.At(200, 0, 1, r, g, b) && r == 0x26 && g == 0x26 && b == 0x26 && img.At(0, 200, 1, r, g, b) && r == 0x26, "border: 1 px #262626");
        Check(img.At(btn.left + 3, btn.top + 4, 1, r, g, b) && std::abs(r - 0x1A) <= 1 && std::abs(g - 0x86) <= 1 && std::abs(b - 0x41) <= 1, "Redémarrer: Travail's fill #1A8641 (Paper 257P-0)");
        Check(img.At(21 + 26, 21 + 4, 1, r, g, b) && r > 0xC0 && g > 0xC0, "logo: the live icon's light tile");
        const auto icon = Vivid(img, 1, {29, 125, 45, 141}), ink = AboutIcons::ToOklch(0x6F / 255.f, 0xD0 / 255.f, 0x87 / 255.f);
        Check(HueGap(icon.h, ink.h) < 6 && icon.l > .6, "card icon: the profile's ink #6FD087");
        close();
    }
    {   // Glass colours: Paper gives Travail's exactly; any profile keeps the same relation to its ink; grey stays grey.
        const auto p = AboutIcons::PaletteFor(0x6F / 255.f, 0xD0 / 255.f, 0x87 / 255.f);
        Check(Hex(p.body[0], 0x3E8A52, 1) && Hex(p.body[1], 0x173D23, 1), "Travail body: #3E8A52 -> #173D23 (Paper 25KZ-0)");
        Check(Hex(p.glass[0], 0xB9EEC6, .7f) && Hex(p.glass[1], 0x6FD087, .7f), "Travail glass: #B9EEC6 -> #6FD087 at 70 %");
        Check(Hex(p.light[0], 0xD8F6DF, .96f) && Hex(p.light[1], 0x9FE3B1, .96f), "Travail light parts (gift ribbon): #D8F6DF -> #9FE3B1 at 96 %");
        int hue = 0; float chroma = 0; ProfileColor::FromHex(L"#B49BFF", hue, chroma);
        const auto vi = ProfileColor::Ink(hue, chroma); const auto v = AboutIcons::PaletteFor(vi.r / 255.f, vi.g / 255.f, vi.b / 255.f);
        const auto vk = AboutIcons::ToOklch(vi.r / 255.f, vi.g / 255.f, vi.b / 255.f), vb = AboutIcons::ToOklch(v.body[0].r, v.body[0].g, v.body[0].b), vg = AboutIcons::ToOklch(v.glass[0].r, v.glass[0].g, v.glass[0].b);
        Check(HueGap(vb.h, vk.h) < 4 && HueGap(vg.h, vk.h) < 4 && vb.l < vk.l && vg.l > vk.l, "Violet: body darker and glass lighter than its ink, same hue");
        const auto n = ProfileColor::Neutral(); const auto gp = AboutIcons::PaletteFor(n.r / 255.f, n.g / 255.f, n.b / 255.f);
        Check(Near(gp.body[0].r, gp.body[0].g, .005f) && Near(gp.body[0].g, gp.body[0].b, .005f) && Near(gp.glass[0].r, gp.glass[0].b, .005f), "Personnel (grey): grey glass");
    }
    {   // The six states of the card (A8's states, same engine), each laid out and rendered.
        float rest = 0;
        for (int i = 0; i < 6; ++i) {
            Check(open(i), "open state");
            const auto st = State(i); const auto card = d.AboutBlockForTest(1), t0 = d.BoxForTest(D::LinkHit(0));
            const auto order = d.OrderForTest(); const bool stop = std::find(order.begin(), order.end(), D::StatusHit) != order.end();
            std::printf("     %s: « %ls » / « %ls » / button « %ls »%s, card %.1f high\n", StateName(i), st.title.c_str(), st.detail.c_str(), st.hasButton ? st.button.label.c_str() : L"-", st.button.disabled ? " (disabled)" : "", card.bottom - card.top);
            Check(Near(t0.top, card.bottom + 14) && Near(d.HeightForTest(), card.bottom + 14 + 96 * 2 + 8 + 6 + 10 + 16 + 16 + 1), "tiles and footer follow the card");
            Check(stop == (st.hasButton && !st.button.disabled), "the card's button is a Tab stop only when it acts");
            const auto nodes = d.NodesForTest(); const auto* text = Node(nodes, 7700);
            Check(text && text->name.find(st.title) == 0 && text->role == Accessibility::Role::Text, "UIA: the state is read as text");
            if (i == 1) Check(Near(card.bottom - card.top, 48) && d.FocusForTest() == D::CloseHit, "checking: one line, « Rechercher » disabled, focus on ×");
            if (i == 4) rest = card.bottom - card.top;
            if (i == 5) Check(card.bottom - card.top > 55, "error: the detail wraps, the card grows");
            for (UINT dpi : {96u, 144u}) Check(d.RenderForTest(shots + L"\\about-state-" + WState(i) + L"-" + std::to_wstring(dpi) + L".png", dpi), "render state");
            const auto img = Load(shots + L"\\about-state-" + WState(i) + L"-96.png");
            if (st.hasButton) {
                const auto btn = d.BoxForTest(D::StatusHit); int r, g, b; img.At(btn.left + 3, btn.top + 4, 1, r, g, b);
                if (st.button.role == Modal::Role::Primary) Check(std::abs(r - 0x1A) <= 1 && std::abs(g - 0x86) <= 1 && std::abs(b - 0x41) <= 1, "primary: the profile's fill");
                else if (!st.button.disabled) Check(r == 0x29 && g == 0x29 && b == 0x29, "secondary: #292929 (the app's secondary button)");
                else Check(r < 0x29 && r > 0x20, "disabled: the secondary at 32 %");
            }
            close();
        }
        Check(Near(rest, 55), "ready: 55 high like Paper");
    }
    {   // Tile actions: Entrée, Espace, a real click (WM_LBUTTONDOWN / UP on the window), each closes with its id.
        const int ids[5] = {Modal::AboutNotesLink, Modal::AboutSourceLink, Modal::AboutWebsiteLink, Modal::AboutReportLink, Modal::AboutLicensesLink};
        for (int i = 0; i < 5; ++i) {
            int got = -9, calls = 0;
            open(0, [&](const Modal::Result& r) { ++calls; got = r.button; });
            const UINT dpi = GetDpiForWindow(d.Window()); const auto r = d.BoxForTest(D::LinkHit(static_cast<size_t>(i)));
            const LPARAM at = MAKELPARAM(std::lround((r.left + r.right) / 2 * dpi / 96.f), std::lround((r.top + r.bottom) / 2 * dpi / 96.f));
            SendMessageW(d.Window(), WM_MOUSEMOVE, 0, at); SendMessageW(d.Window(), WM_LBUTTONDOWN, MK_LBUTTON, at);
            Check(d.FocusForTest() == D::LinkHit(static_cast<size_t>(i)) && d.ClickedForTest() == D::LinkHit(static_cast<size_t>(i)), "a press focuses the tile (mouse focus)");
            SendMessageW(d.Window(), WM_LBUTTONUP, 0, at);
            Check(calls == 1 && got == ids[i] && !d.Visible(), "a click on the tile closes with its id (the host opens the page / Nouveautés)");
            d.FinishAnimationsForTest();
        }
        int got = -9;
        open(4, [&](const Modal::Result& r) { got = r.button; });
        d.KeyForTest(VK_TAB); d.KeyForTest(VK_TAB);   // Redémarrer -> Nouveautés -> Code source
        d.KeyForTest(VK_RETURN); Check(got == Modal::AboutSourceLink, "Entrée on « Code source »");
        d.FinishAnimationsForTest();
        open(4, [&](const Modal::Result& r) { got = r.button; });
        d.KeyForTest(VK_TAB); d.KeyForTest(VK_SPACE); Check(got == Modal::AboutNotesLink, "Espace on « Nouveautés »");
        d.FinishAnimationsForTest();
        open(4, [&](const Modal::Result& r) { got = r.button; });
        d.KeyForTest(VK_ESCAPE); Check(got == -1 && !d.Visible(), "Échap closes");
        d.FinishAnimationsForTest();
        int pressed = 0, calls = 0;
        open(0, [&](const Modal::Result&) { ++calls; }, [&](int id) { pressed = id; });
        d.ActivateForTest(D::StatusHit); Check(pressed == Modal::AboutCheckButton && calls == 0 && d.Visible(), "« Rechercher » asks the engine and the window stays");
        close();
        open(4, nullptr, [&](int id) { pressed = id; }); d.ActivateForTest(D::StatusHit); Check(pressed == Modal::AboutRestartButton, "« Redémarrer » hands over to the engine (#60 restart)");
        close();
        open(0, [&](const Modal::Result& r) { got = r.button; }); d.ActivateForTest(D::CloseHit); Check(got == -1, "× closes");
        d.FinishAnimationsForTest();
    }
    {   // Keyboard: × -> the card's button -> the four tiles -> the footer link (brief), focus on the card's action.
        open(4);
        const std::vector<int> want{D::CloseHit, D::StatusHit, D::LinkHit(0), D::LinkHit(1), D::LinkHit(2), D::LinkHit(3), D::LinkHit(4)};
        Check(d.OrderForTest() == want, "Tab order: ×, Redémarrer, Nouveautés, Code source, Site web, Signaler un problème, Licences");
        Check(d.FocusForTest() == D::StatusHit, "focus starts on the card's action (A8)");
        std::vector<int> seen; for (int i = 0; i < 7; ++i) { d.KeyForTest(VK_TAB); seen.push_back(d.FocusForTest()); }
        Check(seen == std::vector<int>({D::LinkHit(0), D::LinkHit(1), D::LinkHit(2), D::LinkHit(3), D::LinkHit(4), D::CloseHit, D::StatusHit}), "Tab walks the order and wraps");
        close();
    }
    {   // Focus styles (Paper 25KB-0): keyboard 1 px white 50 % outside the control; a mouse click 1 px white 5 %.
        const float s = 1; int r, g, b;
        open(0); d.AssumeFocusForTest(true);
        d.KeyForTest(VK_TAB); d.KeyForTest(VK_TAB);   // Rechercher -> Nouveautés -> Code source
        Check(d.FocusForTest() == D::LinkHit(1) && d.FocusVisibleForTest(), "keyboard focus on « Code source »");
        Check(d.RenderForTest(shots + L"\\about-focus-keyboard-96.png", 96) && d.RenderForTest(shots + L"\\about-focus-keyboard-144.png", 144), "render keyboard focus");
        {   const auto img = Load(shots + L"\\about-focus-keyboard-96.png"); const auto t = d.BoxForTest(D::LinkHit(1));
            img.At(t.left - .5f, (t.top + t.bottom) / 2, s, r, g, b); std::printf("     keyboard ring: %d (white 50 %% over #171717 = %d)\n", g, 0x17 + (255 - 0x17) / 2);
            Check(std::abs(g - (0x17 + (255 - 0x17) / 2)) <= 3, "keyboard: 1 px white 50 % just outside the tile");
            Check(img.Green(t.left - 2.5f, (t.top + t.bottom) / 2, s) == 0x17 && img.Green(206 + .5f, 200, s) == 0x17, "keyboard: only that ring, 1 px");
        }
        const UINT dpi = GetDpiForWindow(d.Window()); const auto btn = d.BoxForTest(D::StatusHit);
        const LPARAM at = MAKELPARAM(std::lround((btn.left + btn.right) / 2 * dpi / 96.f), std::lround((btn.top + btn.bottom) / 2 * dpi / 96.f));
        SendMessageW(d.Window(), WM_LBUTTONDOWN, MK_LBUTTON, at); SendMessageW(d.Window(), WM_LBUTTONUP, 0, at);
        Check(d.Visible() && d.FocusForTest() == D::StatusHit && d.ClickedForTest() == D::StatusHit && !d.FocusVisibleForTest(), "a click on « Rechercher » focuses it (mouse)");
        d.PointerStateForTest(0, 0); Sleep(300);   // the hover fade, settled
        Check(d.RenderForTest(shots + L"\\about-focus-mouse-96.png", 96) && d.RenderForTest(shots + L"\\about-focus-mouse-144.png", 144), "render mouse focus");
        {   const auto img = Load(shots + L"\\about-focus-mouse-96.png");
            img.At(btn.left - .5f, (btn.top + btn.bottom) / 2, s, r, g, b); const int want = static_cast<int>(std::lround(0x20 + (255 - 0x20) * A::mouseRing));
            std::printf("     mouse ring: %d (white 5 %% over the card #202020 = %d)\n", g, want);
            Check(std::abs(g - want) <= 2, "mouse: 1 px white 5 % around the clicked button");
        }
        d.KeyForTest(VK_TAB); Check(d.ClickedForTest() == 0 && d.FocusVisibleForTest(), "Tab after a click: the keyboard ring takes over");
        close();
    }
    {   // Hover (not in Paper: the app's surface steps) renders, for the gap list.
        open(4); d.PointerStateForTest(D::LinkHit(2), 0); Sleep(300);
        Check(d.RenderForTest(shots + L"\\about-hover-tile-96.png", 96), "render tile hover");
        {   const auto img = Load(shots + L"\\about-hover-tile-96.png"); const auto t = d.BoxForTest(D::LinkHit(2)); int r, g, b; img.At(t.right - 4, t.bottom - 4, 1, r, g, b);
            Check(r == 0x33 && g == 0x33, "tile hover: #333333 (Design::Surface::hover)"); }
        close();
    }
    {   // Two profiles: Travail (green) and Violet: icons and button follow the window's profile.
        int hue = 0; float chroma = 0; ProfileColor::FromHex(L"#B49BFF", hue, chroma);
        const auto ink = ProfileColor::Ink(hue, chroma), fill = ProfileColor::AccentFill(hue, chroma);
        Profile((fill.r << 16) | (fill.g << 8) | fill.b, (ink.r << 16) | (ink.g << 8) | ink.b);
        open(4);
        for (UINT dpi : {96u, 144u}) Check(d.RenderForTest(shots + L"\\about-violet-" + std::to_wstring(dpi) + L".png", dpi), "render Violet");
        const auto img = Load(shots + L"\\about-violet-96.png"); const auto btn = d.BoxForTest(D::StatusHit); int r, g, b;
        img.At(btn.left + 3, btn.top + 4, 1, r, g, b);
        Check(std::abs(r - fill.r) <= 1 && std::abs(g - fill.g) <= 1 && std::abs(b - fill.b) <= 1, "Violet: the button in Violet's fill");
        const auto k = AboutIcons::ToOklch(ink.r / 255.f, ink.g / 255.f, ink.b / 255.f);
        bool all = true;
        for (size_t i = 0; i < 4; ++i) { const auto t = d.BoxForTest(D::LinkHit(i)); const auto v = Vivid(img, 1, {t.left + 12, t.top + 12, t.left + 38, t.top + 38}); std::printf("     tile %zu icon: hue %.0f (ink %.0f), chroma %.3f\n", i + 1, v.h, k.h, v.c); all = all && HueGap(v.h, k.h) < 8 && v.c > .04; }
        Check(all, "Violet: the four glass icons in Violet's hue");
        close();
        Profile(0x1A8641, 0x6FD087);
        open(4); Check(d.RenderForTest(shots + L"\\about-travail-96.png", 96), "render Travail");
        const auto tr = Load(shots + L"\\about-travail-96.png"); const auto tk = AboutIcons::ToOklch(0x6F / 255.f, 0xD0 / 255.f, 0x87 / 255.f);
        all = true;
        for (size_t i = 0; i < 4; ++i) { const auto t = d.BoxForTest(D::LinkHit(i)); const auto v = Vivid(tr, 1, {t.left + 12, t.top + 12, t.left + 38, t.top + 38}); all = all && HueGap(v.h, tk.h) < 8 && v.c > .04; }
        Check(all, "Travail: the four glass icons in Travail's green");
        close();
    }
    {   // Accessibility: every control named, the tiles' subtitles as help, automation ids.
        open(4);
        const auto nodes = d.NodesForTest();
        const auto* g = Node(nodes, 1);
        Check(g && g->name == L"À propos de Browser" && g->help == L"Version 0.3.132 · Windows 64 bits" && g->automationId == L"AboutBrowser", "UIA: the window « À propos de Browser », the version as help");
        const auto* x = Node(nodes, D::CloseHit); Check(x && x->name == L"Fermer" && x->focusable, "UIA: × « Fermer »");
        const auto* s = Node(nodes, D::StatusHit); Check(s && s->name == L"Redémarrer" && s->help == L"Version 0.3.133 disponible · S'installe au redémarrage" && s->automationId == L"AboutRestart", "UIA: « Redémarrer », the state as help");
        const wchar_t* names[5] = {L"Nouveautés", L"Code source", L"Site web", L"Signaler un problème", L"Licences des composants tiers"};
        const wchar_t* ids[5] = {L"AboutWhatsNew", L"AboutSourceCode", L"AboutWebsite", L"AboutReport", L"AboutLicenses"};
        bool ok = true; for (size_t i = 0; i < 5; ++i) { const auto* n = Node(nodes, D::LinkHit(i)); ok = ok && n && n->name == names[i] && n->automationId == ids[i] && n->role == Accessibility::Role::Button && n->focusable; }
        Check(ok, "UIA: the four tiles and the licences link are named buttons");
        const auto* src = Node(nodes, D::LinkHit(1)); Check(src && src->help == L"Logiciel libre · GitHub", "UIA: a tile's subtitle as help");
        const auto* copy = Node(nodes, 7701); Check(copy && copy->name == L"© 2026 Val · WebView2" && !copy->focusable, "UIA: the credit line as text");
        close();
    }
    {   // English.
        I18n::SetForProcess(I18n::Locale::En);
        const auto s = Modal::AboutSpec(L"0.3.132", Modal::AboutReady, L"", L"0.3.133");
        Check(s.about.version == L"Version 0.3.132 · Windows 64-bit" && s.status.title == L"Version 0.3.133 available" && s.status.detail == L"Installs when you restart" && s.status.button.label == L"Restart", "English: header and card");
        Check(s.links[0].label == L"What’s new" && s.links[1].label == L"Source code" && s.links[2].label == L"Website" && s.links[3].label == L"Report a problem" && s.links[4].label == L"Third-party licenses", "English: tiles and link");
        Check(s.about.tiles[0].subtitle == L"Notes for 0.3.132" && s.about.tiles[1].subtitle == L"Open source · GitHub ↗" && s.about.tiles[2].subtitle == L"valsbrowser.com ↗" && s.about.tiles[3].subtitle == L"On GitHub ↗", "English: subtitles");
        open(4);
        const auto nodes = d.NodesForTest(); const auto* g = Node(nodes, 1);
        Check(g && g->name == L"About Browser", "English: the window « About Browser »");
        for (UINT dpi : {96u, 144u}) Check(d.RenderForTest(shots + L"\\about-en-" + std::to_wstring(dpi) + L".png", dpi), "render English");
        const auto lic = d.BoxForTest(D::LinkHit(4)), foot = d.AboutBlockForTest(2);
        Check(lic.right < foot.right - 100, "English: the licences link stays clear of the credit line");
        close();
        I18n::SetForProcess(I18n::Locale::Fr);
    }
    Check(!IsWindowVisible(d.Window()) && GetForegroundWindow() == foreground, "end: nothing was shown, the foreground never moved");
    d.Dispose(); DestroyWindow(owner);
    CoUninitialize();
    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
