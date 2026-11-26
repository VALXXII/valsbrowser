// Annotation mode, iframes case 2 (src/annotation-frame-card.h; Paper « Iframes · cas 2 · iframe d'un autre site · v2 carte
// + En savoir plus » 26DE-0): the card under the iframe of another site and its « En savoir plus » panel, by the production
// classes. Never on the user's screen: the renders are offscreen (software Direct2D into WIC, saved as PNG for the
// side-by-side audit with the board), and the real windows are made on a desktop of this process's own (CreateDesktopW,
// never shown): the card's buttons clicked through its window procedure, the panel docked, opened and closed (×, Échap,
// reduced motion) on an owner window there. The card's placement (under / above / edges / inside) is checked in
// tests/annotations/host_test.cpp (AnnotationHost::PlaceFrameCard), the page script's messages in
// tests/chat/final/page_frames.py (case 2).
// Usage: frame_card_test.exe <repo root> <output dir>
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <string>
#include "../../src/annotation-frame-card.h"

static int failures = 0, checks = 0;
static void Expect(bool ok, const char* what) { ++checks; std::printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++failures; }
static bool Near(float a, float b, float eps = .51f) { return std::fabs(a - b) <= eps; }
static void Pump(int ms) {
    const ULONGLONG end = GetTickCount64() + static_cast<ULONGLONG>(ms);
    while (GetTickCount64() < end) { MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); } Sleep(4); }
}
namespace FC = NativeChrome::AnnotationFrameCard;

static LRESULT CALLBACK OwnerProc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }

int wmain(int argc, wchar_t** argv) {
    // A desktop of our own before any window: nothing reaches the user's screen, mouse, keyboard or foreground.
    HDESK desk = CreateDesktopW((L"frame-card-test-" + std::to_wstring(GetCurrentProcessId())).c_str(), nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    const bool hidden = desk && SetThreadDesktop(desk);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (argc < 3) { std::printf("usage: frame_card_test <repo root> <output dir>\n"); return 2; }
    Expect(hidden, "setup: a desktop of our own (never on the user's screen)");
    if (!hidden) return 1;   // never a window on the user's desktop
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const std::wstring root = argv[1], out = argv[2];
    I18n::SetForProcess(I18n::Locale::Fr);
    NativeChrome::Chrome measure; HWND scratch = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, -32000, -32000, 10, 10, nullptr, nullptr, nullptr, nullptr);
    const bool measured = scratch && SUCCEEDED(measure.Init(scratch, root));
    Expect(measured, "setup: a text measurer");

    // ---- the card, offscreen (Paper 26GX-0)
    {
        FC::Card card; int annotate = 0, more = 0; FC::CardHooks h; h.annotate = [&] { ++annotate; }; h.more = [&] { ++more; };
        card.SetupForTest(root, 0x1A8641, h);   // the board's « travail » profile accent
        if (measured) {
            const float height = card.HeightForTest(measure);
            const auto a = card.ButtonForTest(FC::Card::Annotate, measure), m = card.ButtonForTest(FC::Card::More, measure);
            const float wa = NativeChrome::OnboardingKit::Width(measure, TR(annotation_frame_card_annotate), FC::CardSpec::Button()), wm = NativeChrome::OnboardingKit::Width(measure, TR(annotation_frame_card_more), FC::CardSpec::Button());
            char line[200]; std::snprintf(line, sizeof line, "geometry: card 300 x %.0f (border 1 + padding 12; title 18 + 8 + two 2-line texts 34 + 8 + 4 + buttons 28): 300 x 168", height); Expect(Near(height, 168), line);
            Expect(Near(a.left, 13) && Near(a.top, 127) && Near(a.bottom - a.top, 28) && Near(a.right - a.left, wa + 20), "geometry: « Annoter le cadre » at (13, 127), 28 high, its text + 10 + 10");
            Expect(Near(m.left, a.right + 8) && Near(m.top, a.top) && Near(m.right - m.left, wm + 20), "geometry: « En savoir plus » 8 after it, same row, its text + 10 + 10");
        }
        bool ok = true;
        for (UINT dpi : {96u, 120u, 144u}) { const std::wstring p = out + L"\\card-fr@" + std::to_wstring(dpi) + L".png"; ok = card.RenderForTest(dpi, p.c_str(), measure) && ok; }
        Expect(ok, "render: the card at 100 / 125 / 150 % (card-fr@96/120/144.png)");
        card.HoverForTest(FC::Card::Annotate); Expect(card.RenderForTest(96, (out + L"\\card-fr-hover-annotate@96.png").c_str(), measure), "render: « Annoter le cadre » under the pointer (the accent's hover)");
        card.HoverForTest(FC::Card::More); Expect(card.RenderForTest(96, (out + L"\\card-fr-hover-more@96.png").c_str(), measure), "render: « En savoir plus » under the pointer (#3E3E3E)");
        card.HoverForTest(FC::Card::None);
        card.SetupForTest(root, 0x3E67FF, h); Expect(card.RenderForTest(96, (out + L"\\card-fr-personnel@96.png").c_str(), measure), "render: the card in « Personnel »'s blue");
        card.ActivateForTest(FC::Card::Annotate); card.ActivateForTest(FC::Card::More);
        Expect(annotate == 1 && more == 1, "card: « Annoter le cadre » and « En savoir plus » reach the host");
    }
    // ---- the panel, offscreen (Paper 26HA-0) at the drawers' width
    {
        FC::Help help;
        if (measured) {
            const auto g = help.GlyphBoxForTest(measure), b = help.CloseBoxForTest(measure);
            Expect(Near(g.right, 332 - 16) && Near(g.top, 18) && Near(g.bottom - g.top, 16), "geometry: « × » 16/16 at the header's right (332 − 16), centred on its 20 px line");
            Expect(Near(b.right - b.left, 24) && Near(b.bottom - b.top, 24) && Near((b.left + b.right) / 2, (g.left + g.right) / 2) && Near((b.top + b.bottom) / 2, 26), "geometry: its 24 px button centred on the glyph");
            const float bottom = help.BodyBottomForTest(measure);
            char line[200]; std::snprintf(line, sizeof line, "geometry: the body from 52 (16 + 20 + 16) to %.0f: four sections under the intro, all in a 740 px window's drawer (659)", bottom);
            Expect(bottom > 400 && bottom + 16 <= 659, line);
        }
        bool ok = true;
        for (UINT dpi : {96u, 120u, 144u}) { const std::wstring p = out + L"\\panel-fr@" + std::to_wstring(dpi) + L".png"; ok = help.RenderForTest(root, dpi, 659, 0, false, p.c_str(), measure) && ok; }
        Expect(ok, "render: the panel 332 × 659 (a 740 px window under its 81 px chrome) at 100 / 125 / 150 %");
        Expect(help.RenderForTest(root, 96, 659, 0, true, (out + L"\\panel-fr-close-hover@96.png").c_str(), measure), "render: « × » under the pointer (the ghost button's plate)");
        Expect(help.RenderForTest(root, 96, 360, 9999, false, (out + L"\\panel-fr-scrolled@96.png").c_str(), measure), "render: a short window, scrolled to the end (the header stays, the fade under it)");
    }
    // ---- English
    I18n::SetForProcess(I18n::Locale::En);
    {
        FC::Card card; card.SetupForTest(root, 0x1A8641, {});
        Expect(card.RenderForTest(96, (out + L"\\card-en@96.png").c_str(), measure), "render: the card in English");
        FC::Help help; Expect(help.RenderForTest(root, 96, 659, 0, false, (out + L"\\panel-en@96.png").c_str(), measure), "render: the panel in English");
    }
    I18n::SetForProcess(I18n::Locale::Fr);

    // ---- real windows on our desktop: an owner 1180 × 740, the card's buttons, the panel docked / opened / closed
    WNDCLASSW wc{}; wc.lpfnWndProc = OwnerProc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"FrameCardTestOwner"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); RegisterClassW(&wc);
    HWND owner = CreateWindowExW(0, wc.lpszClassName, L"owner", WS_OVERLAPPEDWINDOW, 40, 40, 1180, 740, nullptr, nullptr, wc.hInstance, nullptr);
    Expect(owner != nullptr, "setup: an owner window (on our desktop)");
    if (owner) {
        ShowWindow(owner, SW_SHOW); Pump(50);
        const float k = GetDpiForWindow(owner) / 96.f;
        {
            FC::Card card; int annotate = 0, more = 0, entered = 0, left = 0;
            FC::CardHooks h; h.annotate = [&] { ++annotate; }; h.more = [&] { ++more; }; h.entered = [&] { ++entered; }; h.left = [&] { ++left; };
            Expect(card.Ensure(wc.hInstance, owner, root), "card window: made (hidden until shown)");
            Expect(card.Window() && !IsWindowVisible(card.Window()), "card window: not shown before the hover asks");
            const SIZE sz = card.SizePx(); Expect(sz.cx == std::lround(300 * k), "card window: 300 DIP wide");
            Expect(card.Show({200, 300}, 0x1A8641, h) && card.Visible() && IsWindowVisible(card.Window()), "card window: shown at the place given");
            RECT r{}; GetWindowRect(card.Window(), &r); Expect(r.left == 200 && r.top == 300 && r.right - r.left == sz.cx && r.bottom - r.top == sz.cy, "card window: its top-left corner where PlaceFrameCard put it");
            const LONG_PTR ex = GetWindowLongPtrW(card.Window(), GWL_EXSTYLE); Expect((ex & WS_EX_NOACTIVATE) != 0, "card window: never takes the keyboard (WS_EX_NOACTIVATE: the page keeps it)");
            Expect(SendMessageW(card.Window(), WM_MOUSEACTIVATE, WPARAM(owner), MAKELPARAM(HTCLIENT, WM_LBUTTONDOWN)) == MA_NOACTIVATE, "card window: a click does not activate it");
            // the pointer through the window procedure (client px): onto « Annoter le cadre », press, release
            if (measured) {
                FC::Card probe; probe.SetupForTest(root, 0, {}); const auto a = probe.ButtonForTest(FC::Card::Annotate, measure), m = probe.ButtonForTest(FC::Card::More, measure);
                auto at = [&](D2D1_RECT_F b) { return MAKELPARAM(std::lround((b.left + b.right) / 2 * k), std::lround((b.top + b.bottom) / 2 * k)); };
                SendMessageW(card.Window(), WM_MOUSEMOVE, 0, at(a)); Expect(entered == 1, "card: the pointer onto it tells the host (it stays: no grace end)");
                SendMessageW(card.Window(), WM_LBUTTONDOWN, MK_LBUTTON, at(a)); SendMessageW(card.Window(), WM_LBUTTONUP, 0, at(a));
                Expect(annotate == 1 && more == 0, "card: a click on « Annoter le cadre » (window procedure)");
                SendMessageW(card.Window(), WM_LBUTTONDOWN, MK_LBUTTON, at(m)); SendMessageW(card.Window(), WM_LBUTTONUP, 0, at(m));
                Expect(more == 1 && annotate == 1, "card: a click on « En savoir plus » (window procedure)");
                SendMessageW(card.Window(), WM_LBUTTONDOWN, MK_LBUTTON, at(a)); SendMessageW(card.Window(), WM_LBUTTONUP, 0, at(m));
                Expect(annotate == 1 && more == 1, "card: pressed on one button, released on the other: nothing");
                SendMessageW(card.Window(), WM_MOUSELEAVE, 0, 0); Expect(left == 1, "card: the pointer off it tells the host (its 300 ms grace)");
            }
            card.Close(); Expect(!card.Visible(), "card: Close starts the fade out");
            Pump(400); Expect(!IsWindowVisible(card.Window()), "card: hidden once faded out (120 ms)");
            Expect(card.Show({220, 320}, 0x1A8641, h) && IsWindowVisible(card.Window()), "card: shown again (the same window)");
            card.Hide(); Expect(!card.Visible() && !IsWindowVisible(card.Window()), "card: hidden at once (mode's end, window minimised)");
            card.Destroy(); Expect(card.Window() == nullptr, "card: destroyed with the mode");
        }
        {
            namespace DL = NativeChrome::Downloads::DrawerLayout;
            FC::Help help; int closed = 0, closedKeyboard = 0;
            FC::HelpHooks h; h.closed = [&](bool keyboard) { ++closed; if (keyboard) ++closedKeyboard; };
            Expect(help.Create(wc.hInstance, owner, root), "panel window: made");
            help.Show(false, h); help.SettleForTest();
            RECT c{}; GetClientRect(owner, &c); MapWindowPoints(owner, nullptr, reinterpret_cast<POINT*>(&c), 2);
            const RECT w = help.WindowRectForTest();
            char line[240]; std::snprintf(line, sizeof line, "panel window: docked at the drawers' place — right edge %ld (owner %ld), %ld wide (332 DIP = %ld), from %ld (toolbar %ld)",
                                          w.right, c.right, w.right - w.left, std::lround(332 * k), w.top, c.top + std::lround(DL::liveTop * k));
            Expect(help.Open() && IsWindowVisible(help.Window()) && w.right == c.right && w.right - w.left == std::lround(332 * k) && w.top == c.top + std::lround(DL::liveTop * k) && w.bottom == c.bottom, line);
            Expect(Near(help.SlideForTest(), 1, .001f), "panel: slid in (the drawers' enter spring, settled)");
            help.KeyForTest(VK_ESCAPE); Expect(help.Closing() && closed == 1 && closedKeyboard == 1, "panel: Échap closes it (keyboard: the page gets the keyboard back)");
            help.SettleForTest(); Expect(!help.Visible() && !IsWindowVisible(help.Window()), "panel: gone once slid out");
            help.Show(false, h); help.SettleForTest(); help.ClickCloseForTest();
            help.SettleForTest(); Expect(!help.Visible() && closed == 2 && closedKeyboard == 1, "panel: « × » closes it");
            help.Show(true, h); Expect(help.Open() && Near(help.SlideForTest(), 1, .001f), "panel: reduced motion — in place at once (no slide)");
            help.Hide(false, true); Expect(!help.Visible(), "panel: reduced motion — gone at once");
            help.Show(false, h); help.SettleForTest();
            help.KeyForTest(VK_TAB); help.KeyForTest(VK_RETURN); help.SettleForTest(); Expect(!help.Visible() && closed == 4 && closedKeyboard == 2, "panel: Tab to « × », Entrée closes it");
            help.Show(false, h); help.SettleForTest(); help.Hide(false, false); Expect(!help.Visible(), "panel: hidden at once (the window minimised)");
            help.Destroy();
        }
        DestroyWindow(owner);
    }
    if (scratch) DestroyWindow(scratch);
    std::printf("frame_card_test: %d checks, %d failures\n", checks, failures);
    CoUninitialize();
    if (desk) CloseDesktop(desk);
    return failures ? 1 : 0;
}
