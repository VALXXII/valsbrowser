// Zero drop shadow (user decision 2026-11-09): every popup window of ours is shadowless.
// Two kinds of evidence:
//  1. structure — NativeUI::IsShadowless on the REAL component windows (class styles,
//     frame styles, DWM non-client rendering and corner preference), and the helper's
//     re-assertion after WM_NCACTIVATE / WM_DPICHANGED / show, region kept in sync;
//  2. pixels — the desktop is captured (GetPixel on the screen DC, i.e. what DWM composed,
//     shadows included; PrintWindow would miss them) around a popup placed over a white
//     backdrop. A DWM-rounded control window must darken the backdrop (proves the capture
//     sees shadows); every helper-configured window must leave it untouched.
// Usage: popup_shadow_test.exe <repo root>. Needs an interactive, unlocked desktop for (2);
// without one the pixel checks report NOT VERIFIED and the run fails.
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>
#include "../src/chrome.h"
#include "../src/native-ui.h"
#include "../src/modal.h"
#include "../src/origin-prompt.h"
#include "../src/password-demo.h"
#include "../src/bookmark-toast.h"
#include "../src/bookmark-popover.h"
#include "../src/address-popup.h"
#include "../src/download-panel.h"
#include "../src/history-panel.h"
#include "../src/settings-panel.h"
#include "../src/bookmarks-panel.h"
#include "../src/status-bubble.h"
#include "../src/split-overlay.h"

using namespace NativeChrome;
static int failures = 0, checks = 0;
static void Check(bool ok, const char* what) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", what); } else std::printf("ok   %s\n", what); }
static void Pump(int ms) { const DWORD end = GetTickCount() + ms; MSG m; while (static_cast<int>(end - GetTickCount()) > 0) { while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); } DwmFlush(); } }
static COLORREF Screen(int x, int y) { HDC s = GetDC(nullptr); const COLORREF c = GetPixel(s, x, y); ReleaseDC(nullptr, s); return c; }
static int Darkest(const RECT& r) {
    // Probe a ring just outside the window: 2, 5 and 10 px beyond each edge.
    int darkest = 255;
    for (int d : {2, 5, 10}) {
        const int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
        for (POINT p : {POINT{cx, r.bottom - 1 + d}, POINT{r.right - 1 + d, cy}, POINT{cx, r.top - d}, POINT{r.left - d, cy}, POINT{r.right - 1 + d, r.bottom - 1 + d}}) {
            const COLORREF c = Screen(p.x, p.y); if (c == CLR_INVALID) continue;
            darkest = std::min({darkest, static_cast<int>(GetRValue(c)), static_cast<int>(GetGValue(c)), static_cast<int>(GetBValue(c))});
        }
    }
    return darkest;
}
static LRESULT CALLBACK Fill(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_PAINT) { PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); HBRUSH b = CreateSolidBrush(static_cast<COLORREF>(GetWindowLongPtrW(h, GWLP_USERDATA))); FillRect(dc, &ps.rcPaint, b); DeleteObject(b); EndPaint(h, &ps); return 0; }
    if (m == WM_ERASEBKGND) return 1;
    return DefWindowProcW(h, m, w, l);
}
static HWND backdrop = nullptr; static bool desktop = true;
static const RECT probe{260, 220, 560, 420};
// Place `w` over the white backdrop and return the darkest backdrop pixel around it.
static int Measure(HWND w, bool keepSize = false) {
    RECT r = probe;
    if (keepSize) { RECT c{}; GetWindowRect(w, &c); r.right = r.left + std::max<LONG>(40, std::min<LONG>(c.right - c.left, 380)); r.bottom = r.top + std::max<LONG>(24, std::min<LONG>(c.bottom - c.top, 300)); }
    SetWindowPos(w, HWND_TOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    LONG_PTR ex = GetWindowLongPtrW(w, GWL_EXSTYLE);
    BYTE alpha = 255; DWORD flags = 0; COLORREF key = 0;
    if ((ex & WS_EX_LAYERED) && GetLayeredWindowAttributes(w, &key, &alpha, &flags) && (flags & LWA_ALPHA)) SetLayeredWindowAttributes(w, 0, 255, LWA_ALPHA);
    Pump(350);
    return Darkest(r);
}
static void PixelCheck(HWND w, const char* name, bool keepSize = false) {
    if (!desktop) { std::printf("NOT VERIFIED (no composed desktop) pixels: %s\n", name); ++failures; return; }
    const int d = Measure(w, keepSize);
    char text[256]; std::snprintf(text, sizeof text, "%s: backdrop around it stays white (darkest %d >= 246)", name, d);
    Check(d >= 246, text);
    ShowWindow(w, SW_HIDE); Pump(60);
}
static HWND Plain(const wchar_t* cls, DWORD style = WS_POPUP, DWORD ex = WS_EX_TOOLWINDOW | WS_EX_TOPMOST) {
    HWND h = CreateWindowExW(ex, cls, L"", style, probe.left, probe.top, 300, 200, backdrop, nullptr, GetModuleHandleW(nullptr), nullptr);
    SetWindowLongPtrW(h, GWLP_USERDATA, RGB(23, 23, 23)); return h;
}
static int Corner(HWND h) { int v = -1; DwmGetWindowAttribute(h, 33, &v, sizeof v); return v; }

int wmain(int argc, wchar_t** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 2;
    INITCOMMONCONTROLSEX cc{sizeof cc, ICC_STANDARD_CLASSES | ICC_BAR_CLASSES | ICC_TAB_CLASSES}; InitCommonControlsEx(&cc);
    std::wstring root = argc > 1 ? argv[1] : L".";
    HINSTANCE hi = GetModuleHandleW(nullptr);
    WNDCLASSW plain{}; plain.lpfnWndProc = Fill; plain.hInstance = hi; plain.lpszClassName = L"ShadowTestPlain"; RegisterClassW(&plain);
    WNDCLASSW shadow = plain; shadow.lpszClassName = L"ShadowTestDropShadow"; shadow.style = CS_DROPSHADOW; RegisterClassW(&shadow);
    backdrop = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"ShadowTestPlain", L"", WS_POPUP, 120, 120, 600, 420, nullptr, nullptr, hi, nullptr);
    SetWindowLongPtrW(backdrop, GWLP_USERDATA, RGB(255, 255, 255)); ShowWindow(backdrop, SW_SHOWNOACTIVATE); Pump(300);
    BOOL composed = FALSE; DwmIsCompositionEnabled(&composed);
    const COLORREF seen = Screen(140, 140); std::printf("backdrop pixel %06lx (CLR_INVALID = desktop locked or not interactive), composed %d\n", static_cast<unsigned long>(seen), composed);
    desktop = composed && seen == RGB(255, 255, 255);
    std::printf("desktop capture %s\n", desktop ? "available" : "UNAVAILABLE");

    // 1. Control: DWM rounding casts a shadow, and the capture sees it.
    {
        HWND w = Plain(L"ShadowTestPlain"); const int round = 2; DwmSetWindowAttribute(w, 33, &round, sizeof round);
        Check(!NativeUI::IsShadowless(w), "control: DWMWCP_ROUND window is reported as shadowed");
        if (desktop) { const int d = Measure(w); char t[160]; std::snprintf(t, sizeof t, "control: the capture sees DWM's shadow (darkest %d < 240)", d); Check(d < 240, t); ShowWindow(w, SW_HIDE); }
        DestroyWindow(w);
        HWND c = Plain(L"ShadowTestDropShadow");
        Check(!NativeUI::IsShadowless(c), "control: CS_DROPSHADOW class is reported as shadowed");
        DestroyWindow(c);
    }
    // 2. Helper on a plain popup: rounded region at the Paper radius, no shadow.
    {
        HWND w = Plain(L"ShadowTestPlain");
        NativeUI::ConfigureShadowlessPopup(w, 10);
        NativeUI::ConfigureShadowlessPopup(w, 10);   // idempotent
        Check(NativeUI::IsShadowless(w), "helper: plain popup is shadowless");
        Check(Corner(w) == 1, "helper: DWMWCP_DONOTROUND");
        HRGN rgn = CreateRectRgn(0, 0, 0, 0); const int kind = GetWindowRgn(w, rgn);
        Check(kind == COMPLEXREGION, "helper: rounded window region present");
        Check(!PtInRegion(rgn, 0, 0) && PtInRegion(rgn, 150, 100) && PtInRegion(rgn, 0, 100), "helper: corner cut, edges and centre kept");
        PixelCheck(w, "helper: plain popup radius 10");
        // Resize: the region follows.
        SetWindowPos(w, nullptr, 0, 0, 200, 120, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        GetWindowRgn(w, rgn); RECT box{}; GetRgnBox(rgn, &box);
        Check(box.right == 200 && box.bottom == 120, "helper: region resized with the window");
        // Re-assertion: something re-enables DWM rounding, the next activation/DPI/show removes it.
        const int round = 2;
        DwmSetWindowAttribute(w, 33, &round, sizeof round); SendMessageW(w, WM_NCACTIVATE, TRUE, 0);
        Check(Corner(w) == 1, "helper: WM_NCACTIVATE re-asserts DONOTROUND");
        DwmSetWindowAttribute(w, 33, &round, sizeof round); RECT wr{}; GetWindowRect(w, &wr); SendMessageW(w, WM_DPICHANGED, MAKEWPARAM(96, 96), reinterpret_cast<LPARAM>(&wr));
        Check(Corner(w) == 1, "helper: WM_DPICHANGED re-asserts DONOTROUND");
        DwmSetWindowAttribute(w, 33, &round, sizeof round); ShowWindow(w, SW_HIDE); ShowWindow(w, SW_SHOWNOACTIVATE);
        Check(Corner(w) == 1, "helper: WM_SHOWWINDOW re-asserts DONOTROUND");
        NativeUI::ConfigureShadowlessPopup(w, 0);
        Check(GetWindowRgn(w, rgn) == ERROR, "helper: radius 0 removes the region (square)");
        DeleteObject(rgn); DestroyWindow(w);
    }
    // 3. Helper strips CS_DROPSHADOW (this process' class only) and caption/thick frames.
    {
        HWND w = Plain(L"ShadowTestDropShadow", WS_POPUP | WS_CAPTION | WS_THICKFRAME);
        NativeUI::ConfigureShadowlessPopup(w, 8);
        Check(!(GetClassLongPtrW(w, GCL_STYLE) & CS_DROPSHADOW), "helper: CS_DROPSHADOW removed from the class");
        Check(!(GetWindowLongPtrW(w, GWL_STYLE) & (WS_DLGFRAME | WS_THICKFRAME)), "helper: caption/thick frame removed");
        Check(NativeUI::IsShadowless(w), "helper: drop-shadow class + framed window is shadowless");
        PixelCheck(w, "helper: former CS_DROPSHADOW + WS_THICKFRAME window");
        DestroyWindow(w);
    }
    // 4. Layered (fade) popup keeps working with the region.
    {
        HWND w = Plain(L"ShadowTestPlain", WS_POPUP, WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED);
        SetLayeredWindowAttributes(w, 0, 255, LWA_ALPHA); NativeUI::ConfigureShadowlessPopup(w, 8);
        Check(NativeUI::IsShadowless(w), "helper: layered popup is shadowless");
        PixelCheck(w, "helper: layered popup radius 8");
        DestroyWindow(w);
    }
    // 5. comctl tooltip.
    {
        HWND tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0, 0, 0, backdrop, nullptr, hi, nullptr);
        SetClassLongPtrW(tip, GCL_STYLE, GetClassLongPtrW(tip, GCL_STYLE) | CS_DROPSHADOW);   // as comctl v6 registers it
        Check(!NativeUI::IsShadowless(tip), "tooltip: CS_DROPSHADOW class detected");
        NativeUI::StripTooltipShadow(tip);
        Check(NativeUI::IsShadowless(tip), "tooltip: StripTooltipShadow makes it shadowless");
        TOOLINFOW ti{}; ti.cbSize = TTTOOLINFOW_V2_SIZE; ti.uFlags = TTF_TRACK | TTF_ABSOLUTE; ti.hwnd = backdrop; ti.lpszText = const_cast<LPWSTR>(L"Info-bulle de test");
        SendMessageW(tip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti)); SendMessageW(tip, TTM_TRACKPOSITION, 0, MAKELPARAM(probe.left, probe.top)); SendMessageW(tip, TTM_TRACKACTIVATE, TRUE, reinterpret_cast<LPARAM>(&ti));
        Pump(300);
        if (desktop) { RECT r{}; GetWindowRect(tip, &r); const int d = Darkest(r); char t[160]; std::snprintf(t, sizeof t, "tooltip: backdrop around it stays white (darkest %d >= 246)", d); Check(r.right > r.left && d >= 246, t); }
        else { std::printf("NOT VERIFIED (no composed desktop) pixels: tooltip\n"); ++failures; }
        DestroyWindow(tip);
    }
    // 6. Real components.
    struct Named { const char* name; HWND hwnd; bool pixels; };
    std::vector<Named> windows;
    Modal::Dialog dialog; dialog.Create(hi, backdrop, root, {});
    windows.push_back({"Modal::Dialog panel", dialog.Window(), false}); windows.push_back({"Modal::Dialog scrim", dialog.Scrim(), false});
    OriginPrompt::Prompt prompt; prompt.Create(hi, backdrop, root, {}); windows.push_back({"OriginPrompt::Prompt", prompt.Window(), false});
    PasswordDemo::Window demo; demo.Create(hi, backdrop, root, {}); windows.push_back({"PasswordDemo::Window", demo.Window(), false});
    BookmarkToast::Toast toast; toast.Create(hi, backdrop, root); windows.push_back({"BookmarkToast::Toast", toast.Window(), true});
    BookmarkPopover::Popover popover; popover.Create(hi, backdrop, root, {}); windows.push_back({"BookmarkPopover::Popover", popover.Window(), true});
    AddressUI::Suggestions suggestions; suggestions.Create(hi, backdrop, root, {}); windows.push_back({"AddressUI::Suggestions", suggestions.Window(), true});
    std::vector<Downloads::Item> items;
    DownloadsUI::Popover downloads; downloads.Create(hi, backdrop, root, &items, {}); windows.push_back({"DownloadsUI::Popover", downloads.Window(), true});
    DownloadsUI::Drawer downloadsDrawer; downloadsDrawer.Create(hi, backdrop, root, &items, {}); windows.push_back({"DownloadsUI::Drawer", downloadsDrawer.Window(), false});
    AddressBar::History history; HistoryUI::Drawer historyDrawer; historyDrawer.Create(hi, backdrop, root, &history, {}); windows.push_back({"HistoryUI::Drawer", historyDrawer.Window(), false});
    BrowserSettings::State settings; SettingsUI::Drawer settingsDrawer; settingsDrawer.Create(hi, backdrop, root, &settings, {}); windows.push_back({"SettingsUI::Drawer", settingsDrawer.Window(), false});
    Bookmarks::Store store; BookmarksUI::Drawer bookmarksDrawer; bookmarksDrawer.Create(hi, backdrop, root, &store, {}); windows.push_back({"BookmarksUI::Drawer", bookmarksDrawer.Window(), false});
    Chrome factories; factories.InitOffscreen(root, 96, 16, 16);
    StatusBubble::Bubble bubble; bubble.Init(hi, backdrop, factories.Factory(), factories.WriteFactory(), factories.FontCollection()); windows.push_back({"StatusBubble::Bubble", bubble.Window(), false});
    SplitUI::Overlay overlay; overlay.Init(hi, backdrop, factories.Factory(), factories.WriteFactory(), factories.FontCollection()); windows.push_back({"SplitUI::Overlay", overlay.Window(), false});
    for (auto& w : windows) {
        char t[200]; std::snprintf(t, sizeof t, "%s: window created and shadowless", w.name);
        Check(w.hwnd && NativeUI::IsShadowless(w.hwnd), t);
        if (w.hwnd && w.pixels) PixelCheck(w.hwnd, w.name, true);
    }
    // The modal as shown (panel over its scrim) and an anchored origin prompt.
    {
        RECT area{probe.left - 100, probe.top - 60, probe.right + 100, probe.bottom + 60};
        bool done = false;
        dialog.Show(Modal::ConfirmSpec(L"exemple.fr", L"Continuer ?"), area, [&](const Modal::Result&) { done = true; });
        dialog.FinishAnimationsForTest(); Pump(350);
        Check(NativeUI::IsShadowless(dialog.Window()) && NativeUI::IsShadowless(dialog.Scrim()), "Modal::Dialog shown: panel and scrim shadowless");
        if (desktop) { RECT r{}; GetWindowRect(dialog.Window(), &r); ShowWindow(dialog.Scrim(), SW_HIDE); Pump(200); const int d = Darkest(r); char t[160]; std::snprintf(t, sizeof t, "Modal::Dialog shown: backdrop around the panel stays white (darkest %d >= 246)", d); Check(d >= 246, t); }
        dialog.Abort(); dialog.FinishAnimationsForTest();
        Check(done, "Modal::Dialog: Abort answered done");
        OriginPrompt::Spec ps; ps.kind = OriginPrompt::Kind::Permission; ps.origin = L"meet.exemple.fr"; ps.permissions = {OriginPrompt::Permission::Camera, OriginPrompt::Permission::Microphone};
        bool answered = false; RECT anchor{probe.left + 4, probe.top - 40, probe.left + 28, probe.top - 16};
        prompt.Show(ps, anchor, [&](OriginPrompt::Result&) { answered = true; });
        prompt.Surface().FinishAnimationsForTest(); Pump(350);
        Check(NativeUI::IsShadowless(prompt.Window()), "OriginPrompt anchored: shadowless");
        if (desktop) { RECT r{}; GetWindowRect(prompt.Window(), &r); const int d = Darkest(r); char t[160]; std::snprintf(t, sizeof t, "OriginPrompt anchored: backdrop around it stays white (darkest %d >= 246)", d); Check(d >= 246, t); }
        prompt.Abort(); prompt.Surface().FinishAnimationsForTest();
        Check(answered, "OriginPrompt: Abort answered done");
    }
    dialog.Dispose(); prompt.Dispose(); demo.Dispose(); toast.Dispose(); popover.Dispose(); suggestions.Dispose(); downloads.Dispose(); downloadsDrawer.Dispose();
    historyDrawer.Dispose(); settingsDrawer.Dispose(); bookmarksDrawer.Dispose(); bubble.Dispose(); overlay.Dispose(); factories.Dispose();
    DestroyWindow(backdrop);
    std::printf("%d checks, %d failures\n", checks, failures);
    CoUninitialize();
    return failures ? 1 : 0;
}
