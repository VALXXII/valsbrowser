// The window title and icon never hide the page. Bug: a brief flash of the canvas in place of the page when
// clicking in a site. Every address, history, title and load event of the page runs Update(), which set the window
// title; the title was stored with the main window briefly marked invisible (to stop Windows painting a classic
// caption over the chrome), which hid the WebView2 page windows for that instant.
//
// Drives the real Update() and window procedure of src/main.cpp on a window with the browser window's styles and a
// child window standing for the page (the WebView2 host), and checks, through every title / icon change:
//   * the window never loses WS_VISIBLE and the page child is never invisible;
//   * the caption style is cleared while Windows stores the title or icon (no classic caption painted), then restored;
//   * a title that does not change sends nothing (no WM_SETTEXT for the same title on each page event);
//   * the title and icon Windows holds (taskbar, Alt+Tab, screen readers) are the new ones.
// Screen-free: the window is cloaked by DWM before it is shown (never drawn on screen, takes no input), never
// activated, at the bottom of the z-order and owned by a hidden window (no taskbar button). Without DWM cloaking the
// checks that need a shown window are skipped (SKIP printed) and the window is never shown.
#include "../../src/main.cpp"
#include <cassert>
#include <cstdio>

static HWND pageChild = nullptr;
static bool forwardToBrowser = false, windowShown = false;
static int setTexts = 0, setIcons = 0, visibilityDrops = 0, pageHiddenSeen = 0, captionCleared = 0, captionRestored = 0;

static LRESULT CALLBACK HostProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_STYLECHANGED && w == (WPARAM)GWL_STYLE) {
        const auto* s = reinterpret_cast<const STYLESTRUCT*>(l);
        if ((s->styleOld & WS_VISIBLE) && !(s->styleNew & WS_VISIBLE)) ++visibilityDrops;
        if ((s->styleOld & WS_CAPTION) == WS_CAPTION && (s->styleNew & WS_CAPTION) != WS_CAPTION) ++captionCleared;
        if ((s->styleOld & WS_CAPTION) != WS_CAPTION && (s->styleNew & WS_CAPTION) == WS_CAPTION) ++captionRestored;
        if (windowShown && !IsWindowVisible(pageChild)) ++pageHiddenSeen;   // what DWM would compose at this instant
    }
    if (m == WM_SETTEXT) ++setTexts;
    if (m == WM_SETICON) ++setIcons;
    // Only the messages under test reach the browser's window procedure (the rest of the chrome is not set up here).
    if (forwardToBrowser && (m == WM_SETTEXT || m == WM_SETICON || m == 0x00AE || m == 0x00AF)) return WndProc(h, m, w, l);
    return DefWindowProcW(h, m, w, l);
}

[[maybe_unused]] static std::wstring Text(HWND h) { wchar_t b[512] = {}; GetWindowTextW(h, b, 512); return b; }
[[maybe_unused]] static std::wstring SystemText(HWND h) { wchar_t b[512] = {}; InternalGetWindowText(h, b, 512); return b; }
// First, after every step: nothing was hidden, not even for an instant.
static void NeverHidden(const char* step) {
    printf("%-46s WM_SETTEXT %2d  WM_SETICON %d  window made invisible %2d times  page invisible %2d times\n", step, setTexts, setIcons, visibilityDrops, pageHiddenSeen);
    assert(visibilityDrops == 0 && pageHiddenSeen == 0);
    if (windowShown) assert(IsWindowVisible(window) && IsWindowVisible(pageChild));
}

int main() {
    HINSTANCE hi = GetModuleHandleW(nullptr);
    WNDCLASSW wc{}; wc.lpfnWndProc = HostProc; wc.hInstance = hi; wc.lpszClassName = L"WindowTitleTestHost"; wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    const ATOM registered = RegisterClassW(&wc); assert(registered); (void)registered;
    HWND owner = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, hi, nullptr);   // never shown
    // The browser window's own styles (wWinMain).
    const LONG_PTR style = WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU | WS_CLIPCHILDREN;
    window = CreateWindowExW(WS_EX_NOACTIVATE, wc.lpszClassName, L"Browser", (DWORD)style, 40, 40, 1180, 815, owner, nullptr, hi, nullptr);
    assert(owner && window);
    const LONG_PTR created = GetWindowLongPtrW(window, GWL_STYLE);
    assert((created & style) == style);
    pageChild = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 81, 1180, 734, window, nullptr, hi, nullptr);
    assert(pageChild);
    chrome.width = 1180; chrome.height = 815;
    const BOOL cloak = TRUE;
    windowShown = SUCCEEDED(DwmSetWindowAttribute(window, 13 /*DWMWA_CLOAK*/, &cloak, sizeof(cloak)));
    if (windowShown) SetWindowPos(window, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    else printf("SKIP shown-window checks: DWM cloaking unavailable, the window is never shown\n");
    if (windowShown) assert(IsWindowVisible(window) && IsWindowVisible(pageChild) && GetForegroundWindow() != window);
    forwardToBrowser = true;

    auto tab = std::make_shared<Tab>(); tab->home = false; tab->revealed = true; tab->uri = L"http://127.0.0.1/a"; tab->title = L"Alpha";
    pages = {tab}; current = 0;

    // 1. A new page title reaches Windows once.
    Update(); NeverHidden("1. new page title");
    assert(setTexts == 1 && Text(window) == TRF(window_title, L"Alpha") && SystemText(window) == TRF(window_title, L"Alpha"));
    assert(captionCleared == 1 && captionRestored == 1);

    // 2. The same title on the page's other events (address, history, load end, focus...): nothing sent.
    for (int i = 0; i < 6; ++i) Update();
    NeverHidden("2. same title, six more page events");
    assert(setTexts == 1 && captionCleared == 1);

    // 3. A title change (a click that navigates, a single-page app changing document.title).
    tab->title = L"Beta"; tab->uri = L"http://127.0.0.1/b"; Update(); Update();
    privateWindow = true; Update(); privateWindow = false;
    NeverHidden("3. title changes (normal, then private)");
    assert(setTexts == 3 && Text(window) == TRF(window_title_private, L"Beta") && SystemText(window) == TRF(window_title_private, L"Beta"));

    // 4. Icon changes go through the same path.
    HICON a = LoadIcon(nullptr, IDI_APPLICATION), b = LoadIcon(nullptr, IDI_INFORMATION);
    SendMessageW(window, WM_SETICON, ICON_SMALL, (LPARAM)a); SendMessageW(window, WM_SETICON, ICON_BIG, (LPARAM)b);
    NeverHidden("4. icon changes");
    assert(setIcons == 2 && (HICON)SendMessageW(window, WM_GETICON, ICON_SMALL, 0) == a && (HICON)SendMessageW(window, WM_GETICON, ICON_BIG, 0) == b);

    // 5. Many quick changes (a page animating its title).
    for (int i = 0; i < 50; ++i) { tab->title = L"Tick " + std::to_wstring(i); Update(); }
    NeverHidden("5. fifty quick title changes");
    assert(setTexts == 53 && Text(window) == TRF(window_title, L"Tick 49"));

    // The caption style was cleared around every store and always came back; nothing else in the style moved.
    assert(captionCleared == captionRestored && captionCleared == setTexts + setIcons);
    assert((GetWindowLongPtrW(window, GWL_STYLE) & ~static_cast<LONG_PTR>(WS_VISIBLE)) == (created & ~static_cast<LONG_PTR>(WS_VISIBLE)));

    forwardToBrowser = false; DestroyWindow(window); window = nullptr; DestroyWindow(owner);
    printf("window title test: OK%s\n", windowShown ? "" : " (shown-window checks skipped)");
    return 0;
}
