// The window's resize border over the chat panel and the drawers (src/resize-border.h; owner's report, trial 0.3.952: with
// the chat open in « Panneau » the window could no longer be resized from its right edge, its bottom edge or its
// bottom-right corner, nor over an open drawer). Windowless: every window here is created hidden and never shown.
//   1. the band (src/main.cpp WM_NCHITTEST) at 100 / 125 / 150 / 175 %: each edge, each corner, its inner limit;
//   2. the grips' four strips: every band pixel once, nothing else;
//   3. the layouts: « Panneau » chat and docked drawers lie over the band (the bug), Mode Focus keeps everything 4 DIP in,
//      the pages 2 DIP in;
//   4. real (hidden) windows: a stand-in for the chat's WebView (a child that answers « client », created after the grips,
//      so above them) and a docked drawer (an owned popup): every band point of the window still answers the window's
//      own sizing code, the cursor is the sizing one, the press reaches the window (its size loop); nothing changes
//      inside the band's limit; maximized / fullscreen: no band;
//   5. the wiring in the browser's sources (main.cpp band and Layout, the chat's placement, the four drawers, the
//      annotation layers, the pages' overlay scrollbars).
#include "../src/resize-border.h"
#include "../src/focus-mode.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

namespace RB = NativeChrome::ResizeBorder;
namespace FM = NativeChrome::FocusMode;
static int checks = 0;
static void Require(bool ok, const std::string& what) { ++checks; if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what.c_str()); std::exit(1); } }
static std::string At(unsigned dpi) { return " at " + std::to_string(dpi * 100 / 96) + " %"; }
static bool In(const RECT& r, int x, int y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }
static int S(float dip, unsigned dpi) { return RB::Px(dip, dpi); }

// ---- the stand-in browser window: its WM_NCHITTEST is src/main.cpp's band, the presses it receives are recorded ----
static bool zoomed = false, fullscreen = false;
static UINT lastMsg = 0; static WPARAM lastW = 0; static LPARAM lastL = 0;
static constexpr LRESULT SizeLoop = 0x5151;   // what the stand-in answers instead of running the system's size loop
static LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_NCHITTEST) {
        if (fullscreen) return HTCLIENT;
        POINT p{static_cast<short>(LOWORD(l)), static_cast<short>(HIWORD(l))}; ScreenToClient(h, &p); RECT c{}; GetClientRect(h, &c);
        const int code = RB::Hit(p.x, p.y, c.right, c.bottom, GetDpiForWindow(h), !zoomed);
        return code ? code : HTCLIENT;
    }
    if (m >= WM_NCLBUTTONDOWN && m <= WM_NCXBUTTONDBLCLK) { lastMsg = m; lastW = w; lastL = l; return SizeLoop; }
    return DefWindowProcW(h, m, w, l);
}
// The chat's WebView as the browser sees it: a window that answers « client » everywhere (its hit test is the WebView2
// process's, never ours).
static LRESULT CALLBACK ForeignProc(HWND h, UINT m, WPARAM w, LPARAM l) { if (m == WM_NCHITTEST) return HTCLIENT; return DefWindowProcW(h, m, w, l); }
// A docked drawer: the claim first, as in src/settings-panel.h / history-panel.h / bookmarks-panel.h / download-panel.h.
static LRESULT CALLBACK DrawerProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    { LRESULT border = 0; if (RB::Claim(h, m, w, l, border)) return border; }
    if (m == WM_NCHITTEST) return HTCLIENT;
    if (m >= WM_NCLBUTTONDOWN && m <= WM_NCXBUTTONDBLCLK) return 0x7777;   // the drawer's own handling
    return DefWindowProcW(h, m, w, l);
}
static LPARAM Screen(HWND main, int x, int y) { POINT p{x, y}; ClientToScreen(main, &p); return MAKELPARAM(static_cast<WORD>(p.x), static_cast<WORD>(p.y)); }

static std::string Read(const char* path) { std::ifstream f(path, std::ios::binary); std::stringstream s; s << f.rdbuf(); return s.str(); }
static std::string Line(const std::string& text, const std::string& needle) { const auto at = text.find(needle); if (at == std::string::npos) return {}; const auto b = text.rfind('\n', at), e = text.find('\n', at); return text.substr(b == std::string::npos ? 0 : b + 1, (e == std::string::npos ? text.size() : e) - (b == std::string::npos ? 0 : b + 1)); }
static std::string After(const std::string& text, const std::string& start, size_t n) { const auto at = text.find(start); return at == std::string::npos ? std::string() : text.substr(at, n); }
static std::string Block(const std::string& text, const std::string& start) { const auto at = text.find(start); if (at == std::string::npos) return {}; const auto end = text.find("\n}", at); return text.substr(at, end == std::string::npos ? std::string::npos : end - at); }

int wmain() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const unsigned dpis[] = {96, 120, 144, 168};
    const int W = 1280, H = 800;

    // ---- 1. the band ----
    for (unsigned dpi : dpis) {
        const int e = S(4, dpi), t = S(3, dpi);
        Require(e == (dpi == 96 ? 4 : dpi == 120 ? 5 : dpi == 144 ? 6 : 7) && t == (dpi == 96 ? 3 : dpi == 120 ? 4 : 5), "band: 4 DIP sides, 3 DIP top, rounded as main.cpp's S()" + At(dpi));
        Require(RB::Hit(0, 0, W, H, dpi) == HTTOPLEFT && RB::Hit(W - 1, 0, W, H, dpi) == HTTOPRIGHT && RB::Hit(0, H - 1, W, H, dpi) == HTBOTTOMLEFT && RB::Hit(W - 1, H - 1, W, H, dpi) == HTBOTTOMRIGHT, "band: the four corners" + At(dpi));
        Require(RB::Hit(W - e, H / 2, W, H, dpi) == HTRIGHT && RB::Hit(W - e - 1, H / 2, W, H, dpi) == HTNOWHERE, "band: the right edge is " + std::to_string(e) + " px deep" + At(dpi));
        Require(RB::Hit(W / 2, H - e, W, H, dpi) == HTBOTTOM && RB::Hit(W / 2, H - e - 1, W, H, dpi) == HTNOWHERE, "band: the bottom edge" + At(dpi));
        Require(RB::Hit(e - 1, H / 2, W, H, dpi) == HTLEFT && RB::Hit(e, H / 2, W, H, dpi) == HTNOWHERE, "band: the left edge" + At(dpi));
        Require(RB::Hit(W / 2, t - 1, W, H, dpi) == HTTOP && RB::Hit(W / 2, t, W, H, dpi) == HTNOWHERE, "band: the top edge" + At(dpi));
        Require(RB::Hit(W - e, H - e, W, H, dpi) == HTBOTTOMRIGHT && RB::Hit(W - 1, H - e - 1, W, H, dpi) == HTRIGHT && RB::Hit(W - e - 1, H - 1, W, H, dpi) == HTBOTTOM, "band: the bottom-right corner is e x e" + At(dpi));
        Require(RB::Hit(W - 1, H - 1, W, H, dpi, false) == HTNOWHERE && RB::Hit(0, 0, W, H, dpi, false) == HTNOWHERE, "band: none maximized or in fullscreen" + At(dpi));
        Require(RB::Hit(W, H / 2, W, H, dpi) == HTNOWHERE && RB::Hit(-1, H / 2, W, H, dpi) == HTNOWHERE, "band: nothing outside the client" + At(dpi));
    }
    Require(RB::Sizing(HTLEFT) && RB::Sizing(HTBOTTOMRIGHT) && !RB::Sizing(HTCLIENT) && !RB::Sizing(HTCAPTION) && !RB::Sizing(HTTRANSPARENT) && !RB::Sizing(HTNOWHERE), "sizing codes: HTLEFT .. HTBOTTOMRIGHT only");
    Require(RB::CursorId(HTRIGHT) == 32644 && RB::CursorId(HTBOTTOM) == 32645 && RB::CursorId(HTBOTTOMRIGHT) == 32642 && RB::CursorId(HTTOPRIGHT) == 32643 && RB::CursorId(HTCLIENT) == 32512, "cursors: IDC_SIZEWE / NS / NWSE / NESW");

    // ---- 2. the strips ----
    for (unsigned dpi : dpis) for (const SIZE sz : {SIZE{W, H}, SIZE{560, 300}, SIZE{901, 657}}) {
        const auto strips = RB::Strips(sz.cx, sz.cy, dpi); bool once = true, only = true;
        for (int y = 0; y < sz.cy; ++y) for (int x = 0; x < sz.cx; ++x) {
            int n = 0; for (const auto& s : strips) n += In(s, x, y);
            const bool band = RB::Hit(x, y, sz.cx, sz.cy, dpi) != HTNOWHERE;
            if (band && n != 1) once = false;
            if (!band && n != 0) only = false;
        }
        Require(once && only, "strips: every band pixel in exactly one strip, nothing else (" + std::to_string(sz.cx) + " x " + std::to_string(sz.cy) + ")" + At(dpi));
    }

    // ---- 3. the layouts (client pixels, as the browser places them) ----
    for (unsigned dpi : dpis) {
        const auto overBand = [&](const RECT& r, int& n) { n = 0; for (int y = r.top; y < r.bottom; ++y) for (int x = r.left; x < r.right; ++x) n += RB::Hit(x, y, W, H, dpi) != HTNOWHERE; return n > 0; };
        int n = 0;
        // « Panneau » (src/chat-host-ui.h PanelRect, at rest): 380 DIP wide, under the chrome (81 DIP), to the window's right and bottom edges.
        const RECT chat{W - S(380, dpi), S(81, dpi), W, H};
        Require(overBand(chat, n) && RB::Hit(chat.right - 1, chat.bottom - 1, W, H, dpi) == HTBOTTOMRIGHT, "Panneau: the chat lies over the right and bottom band and the corner (the reported bug)" + At(dpi));
        const auto strips = RB::Strips(W, H, dpi); bool covered = true;
        for (int y = chat.top; y < chat.bottom; ++y) for (int x = chat.left; x < chat.right; ++x) if (RB::Hit(x, y, W, H, dpi)) { bool any = false; for (const auto& s : strips) any = any || In(s, x, y); covered = covered && any; }
        Require(covered, "Panneau: a grip lies on each of those " + std::to_string(n) + " pixels" + At(dpi));
        // A docked drawer outside Mode Focus (src/history-panel.h ApplyGeometry: liveRight 0, liveBottom 0).
        const RECT drawer{W - S(360, dpi), S(81, dpi), W, H};
        Require(overBand(drawer, n), "drawer: flush with the right edge and the bottom, over the band (claimed)" + At(dpi));
        // Mode Focus: page, chat card and drawers 4 DIP inside the window, as S() rounds (src/focus-mode.h).
        const RECT page{S(FM::pageInset, dpi), S(FM::pageInset, dpi), W - S(FM::pageInset, dpi), H - S(FM::pageInset, dpi)};
        const RECT card{W - S(FM::chatInset, dpi) - S(380, dpi), S(FM::pageInset, dpi), W - S(FM::chatInset, dpi), H - S(FM::chatInset, dpi)};
        const RECT focusDrawer{W - S(FM::panelInset, dpi) - S(360, dpi), S(60, dpi), W - S(FM::panelInset, dpi), H - S(FM::drawerBottom, dpi)};
        Require(!overBand(page, n) && !overBand(card, n) && !overBand(focusDrawer, n), "Mode Focus: page, chat card and drawers leave the whole band free" + At(dpi));
        // The pages outside Mode Focus: 2 DIP inside (src/design-system.h Viewport::inset): the band's outer part is the window's.
        const RECT normal{S(2, dpi), S(81, dpi), W - S(2, dpi), H - S(2, dpi)};
        Require(overBand(normal, n) && RB::Hit(W - 1, H / 2, W, H, dpi) == HTRIGHT && !In(normal, W - 1, H / 2) && S(4, dpi) - S(2, dpi) >= 2, "pages: 2 DIP in, the band's outer half stays the window's (the grips cover the rest)" + At(dpi));
    }

    // ---- 4. real windows, never shown ----
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSW wc{}; wc.hInstance = inst;
    wc.lpfnWndProc = MainProc; wc.lpszClassName = L"ResizeBorderTestMain"; RegisterClassW(&wc);
    wc.lpfnWndProc = ForeignProc; wc.lpszClassName = L"ResizeBorderTestForeign"; RegisterClassW(&wc);
    wc.lpfnWndProc = DrawerProc; wc.lpszClassName = L"ResizeBorderTestDrawer"; RegisterClassW(&wc);
    // Left of the primary monitor: negative screen coordinates in every packed point.
    HWND main = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"ResizeBorderTestMain", L"", WS_POPUP | WS_CLIPCHILDREN, -1500, 120, 1000, 700, nullptr, nullptr, inst, nullptr);
    Require(main && !IsWindowVisible(main), "a hidden stand-in browser window");
    const unsigned dpi = GetDpiForWindow(main); RECT client{}; GetClientRect(main, &client); const int cw = client.right, ch = client.bottom;
    const int e = S(4, dpi), t = S(3, dpi);
    RB::Grips grips; grips.Sync(main, true);
    Require(grips.OnTop(main), "grips: created on top of the window's children");
    {   const auto strips = RB::Strips(cw, ch, dpi); bool placed = true;
        for (int i = 0; i < 4; ++i) { RECT r{}; GetWindowRect(grips.Window(i), &r); MapWindowPoints(nullptr, main, reinterpret_cast<POINT*>(&r), 2); placed = placed && EqualRect(&r, &strips[i]) && grips.Shown(i) && (GetWindowLongW(grips.Window(i), GWL_EXSTYLE) & WS_EX_LAYERED); }
        Require(placed, "grips: four layered children on the band's strips" + At(dpi)); }
    // The chat's WebView, flush with the right and bottom edges, given to the window after the grips: put_ParentWindow
    // (the chat back from the bubble, src/chat-panel-view.h Reparent) is a SetParent, which lands it on top. (A child
    // created by CreateWindowEx lands at the bottom: measured, see below.)
    const RECT chat{cw - S(380, dpi), S(81, dpi), cw, ch};
    HWND holder = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"ResizeBorderTestMain", L"", WS_POPUP, -1500, 120, 1000, 700, nullptr, nullptr, inst, nullptr);
    HWND foreign = CreateWindowExW(0, L"ResizeBorderTestForeign", L"", WS_CHILD | WS_VISIBLE, chat.left, chat.top, chat.right - chat.left, chat.bottom - chat.top, holder, nullptr, inst, nullptr);
    {   HWND below = CreateWindowExW(0, L"ResizeBorderTestForeign", L"", WS_CHILD | WS_VISIBLE, chat.left, chat.top, 10, 10, main, nullptr, inst, nullptr);
        Require(below && grips.OnTop(main), "a child created after the grips lands below them");
        DestroyWindow(below); }
    SetParent(foreign, main);
    Require(foreign && GetParent(foreign) == main && !grips.OnTop(main), "the chat's WebView reparented to the window lands above the grips (what hid the band)");
    {   POINT p{cw - 1, ch / 2}; Require(ChildWindowFromPointEx(main, p, CWP_ALL) == foreign, "before the fix's sync: the right edge over the chat is the WebView's"); }
    grips.Sync(main, true);   // the chat's placement (src/chat-host-ui.h SyncCardFrame -> SyncResizeGrips)
    Require(grips.OnTop(main), "grips: back on top after the chat's placement");
    struct Probe { int x, y; LRESULT code; const char* name; };
    const Probe probes[] = {
        {cw - 1, ch / 2, HTRIGHT, "right edge, outer pixel"}, {cw - e, ch / 2, HTRIGHT, "right edge, inner pixel"},
        {cw - e, chat.top + 1, HTRIGHT, "right edge, top of the chat"}, {cw - S(200, dpi), ch - 1, HTBOTTOM, "bottom edge under the chat"},
        {cw - S(200, dpi), ch - e, HTBOTTOM, "bottom edge, inner pixel"}, {cw - 1, ch - 1, HTBOTTOMRIGHT, "bottom-right corner"},
        {cw - e, ch - e, HTBOTTOMRIGHT, "bottom-right corner, inner pixel"}, {0, ch - 1, HTBOTTOMLEFT, "bottom-left corner"},
        {0, 0, HTTOPLEFT, "top-left corner"}, {cw - 1, 0, HTTOPRIGHT, "top-right corner"}, {cw / 2, t - 1, HTTOP, "top edge"}, {e - 1, ch / 2, HTLEFT, "left edge"},
    };
    for (const auto& pr : probes) {
        POINT p{pr.x, pr.y}; HWND under = ChildWindowFromPointEx(main, p, CWP_ALL);
        Require(grips.Ours(under), std::string("chat open: a grip is the window under the ") + pr.name);
        Require(SendMessageW(under, WM_NCHITTEST, 0, Screen(main, pr.x, pr.y)) == pr.code, std::string("chat open: the window's own code at the ") + pr.name + At(dpi));
    }
    {   POINT p{cw - e - 1, ch / 2}; Require(ChildWindowFromPointEx(main, p, CWP_ALL) == foreign, "chat open: one pixel inside the band's limit is the chat's");
        p = {cw - e - 1, ch - e - 1}; Require(ChildWindowFromPointEx(main, p, CWP_ALL) == foreign, "chat open: the chat's own bottom-right pixel (inside the band) is the chat's"); }
    {   HWND right = ChildWindowFromPointEx(main, POINT{cw - 1, ch / 2}, CWP_ALL);
        Require(SendMessageW(right, WM_SETCURSOR, reinterpret_cast<WPARAM>(right), MAKELPARAM(HTRIGHT, WM_MOUSEMOVE)) == TRUE, "chat open: the sizing cursor on the right edge");
        lastMsg = 0; const LPARAM at = Screen(main, cw - 1, ch / 2);
        Require(SendMessageW(right, WM_NCLBUTTONDOWN, HTRIGHT, at) == SizeLoop && lastMsg == WM_NCLBUTTONDOWN && lastW == HTRIGHT && lastL == at, "chat open: a press on the right edge reaches the window (its size loop)");
        HWND corner = ChildWindowFromPointEx(main, POINT{cw - 1, ch - 1}, CWP_ALL); lastMsg = 0;
        Require(SendMessageW(corner, WM_NCLBUTTONDOWN, HTBOTTOMRIGHT, Screen(main, cw - 1, ch - 1)) == SizeLoop && lastW == HTBOTTOMRIGHT, "chat open: a press on the bottom-right corner reaches the window");
        lastMsg = 0; Require(SendMessageW(right, WM_NCLBUTTONDBLCLK, HTRIGHT, at) == SizeLoop && lastMsg == WM_NCLBUTTONDBLCLK, "chat open: a double click on the edge is the window's");
        lastMsg = 0; SendMessageW(right, WM_NCLBUTTONDOWN, HTCLIENT, at); Require(lastMsg == 0, "grips: nothing but sizing codes is handed on"); }
    // Maximized / fullscreen: the window has no band; the grips step away and answer nothing.
    zoomed = true;
    {   HWND right = ChildWindowFromPointEx(main, POINT{cw - 1, ch / 2}, CWP_ALL);
        Require(SendMessageW(right, WM_NCHITTEST, 0, Screen(main, cw - 1, ch / 2)) == HTTRANSPARENT, "maximized: a grip left in place claims nothing");
        grips.Sync(main, false); bool hidden = true; for (int i = 0; i < 4; ++i) hidden = hidden && !(GetWindowLongW(grips.Window(i), GWL_STYLE) & WS_VISIBLE) && !grips.Shown(i);
        Require(hidden, "maximized: the grips are hidden"); }
    zoomed = false; fullscreen = true; grips.Sync(main, true);
    {   HWND right = ChildWindowFromPointEx(main, POINT{cw - 1, ch / 2}, CWP_ALL);
        Require(grips.Ours(right) && SendMessageW(right, WM_NCHITTEST, 0, Screen(main, cw - 1, ch / 2)) == HTTRANSPARENT, "fullscreen: a grip claims nothing (and SyncResizeGrips hides them)"); }
    fullscreen = false; grips.Sync(main, true);
    Require(grips.OnTop(main) && grips.Shown(0), "restored: the grips are back on top");
    // A new size: the strips follow (the size loop's WM_SIZE -> Layout -> SyncResizeGrips).
    SetWindowPos(main, nullptr, 0, 0, 1180, 760, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE); GetClientRect(main, &client);
    grips.Sync(main, true);
    {   const auto strips = RB::Strips(client.right, client.bottom, dpi); bool placed = true;
        for (int i = 0; i < 4; ++i) { RECT r{}; GetWindowRect(grips.Window(i), &r); MapWindowPoints(nullptr, main, reinterpret_cast<POINT*>(&r), 2); placed = placed && EqualRect(&r, &strips[i]); }
        Require(placed, "resized: the grips follow the band"); }
    SetWindowPos(main, nullptr, 0, 0, 1000, 700, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE); grips.Sync(main, true);
    // A docked drawer: an owned popup (above all the window's children), flush with the right edge and the bottom.
    RECT drawer{cw - S(360, dpi), S(81, dpi), cw, ch}; MapWindowPoints(main, nullptr, reinterpret_cast<POINT*>(&drawer), 2);
    HWND dr = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"ResizeBorderTestDrawer", L"", WS_POPUP, drawer.left, drawer.top, drawer.right - drawer.left, drawer.bottom - drawer.top, main, nullptr, inst, nullptr);
    Require(dr && RB::Frame(dr) == main && !IsWindowVisible(dr), "drawer: a hidden popup owned by the window");
    for (const auto& pr : probes) {
        RECT d{}; GetWindowRect(dr, &d); POINT s{pr.x, pr.y}; ClientToScreen(main, &s); if (!PtInRect(&d, s)) continue;
        Require(SendMessageW(dr, WM_NCHITTEST, 0, Screen(main, pr.x, pr.y)) == pr.code, std::string("drawer open: the window's own code at the ") + pr.name + At(dpi));
    }
    Require(SendMessageW(dr, WM_NCHITTEST, 0, Screen(main, cw - e - 1, ch / 2)) == HTCLIENT && SendMessageW(dr, WM_NCHITTEST, 0, Screen(main, cw - S(200, dpi), ch - e - 1)) == HTCLIENT, "drawer open: inside the band's limit, the drawer's own");
    Require(SendMessageW(dr, WM_SETCURSOR, reinterpret_cast<WPARAM>(dr), MAKELPARAM(HTBOTTOMRIGHT, WM_MOUSEMOVE)) == TRUE, "drawer open: the sizing cursor on the corner");
    lastMsg = 0;
    Require(SendMessageW(dr, WM_NCLBUTTONDOWN, HTBOTTOM, Screen(main, cw - S(100, dpi), ch - 1)) == SizeLoop && lastMsg == WM_NCLBUTTONDOWN && lastW == HTBOTTOM, "drawer open: a press on the bottom edge reaches the window (its size loop)");
    lastMsg = 0; Require(SendMessageW(dr, WM_NCRBUTTONUP, HTRIGHT, Screen(main, cw - 1, ch / 2)) == SizeLoop && lastMsg == WM_NCRBUTTONUP, "drawer open: a right click on the edge is the window's, not the drawer's");
    lastMsg = 0; Require(SendMessageW(dr, WM_NCLBUTTONDOWN, HTCAPTION, Screen(main, cw - 1, ch / 2)) == 0x7777 && lastMsg == 0, "drawer: its own non-sizing presses stay its own");
    zoomed = true; Require(SendMessageW(dr, WM_NCHITTEST, 0, Screen(main, cw - 1, ch - 1)) == HTCLIENT, "drawer, maximized window: no band, the drawer's own"); zoomed = false;
    fullscreen = true; Require(SendMessageW(dr, WM_NCHITTEST, 0, Screen(main, cw - 1, ch / 2)) == HTCLIENT, "drawer, fullscreen: no band"); fullscreen = false;
    // A window alone (no owner, no parent): nothing to claim for.
    HWND lone = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"ResizeBorderTestDrawer", L"", WS_POPUP, drawer.left, drawer.top, 200, 200, nullptr, nullptr, inst, nullptr);
    Require(lone && RB::Frame(lone) == nullptr && SendMessageW(lone, WM_NCHITTEST, 0, Screen(main, cw - 1, ch / 2)) == HTCLIENT, "a window alone claims nothing");
    // A layer-like child over the top edge (Mode Focus: the annotation banner at y 0, x 4 .. W - 4), its claim first.
    HWND banner = CreateWindowExW(0, L"ResizeBorderTestDrawer", L"", WS_CHILD | WS_VISIBLE, S(4, dpi), 0, cw - 2 * S(4, dpi), S(32, dpi), main, nullptr, inst, nullptr);
    Require(SendMessageW(banner, WM_NCHITTEST, 0, Screen(main, cw / 2, 0)) == HTTOP && SendMessageW(banner, WM_NCHITTEST, 0, Screen(main, cw / 2, t)) == HTCLIENT, "Mode Focus banner on the top edge: the top band stays the window's");
    DestroyWindow(banner); DestroyWindow(lone); DestroyWindow(dr); DestroyWindow(foreign); DestroyWindow(holder); grips.Destroy(); DestroyWindow(main);
    Require(!IsWindow(grips.Window(0)), "grips: destroyed with their owner");

    // ---- 5. the browser's wiring ----
    const std::string mainCpp = Read("src/main.cpp"), chatUi = Read("src/chat-host-ui.h"), kit = Read("src/annotation-kit.h");
    Require(!mainCpp.empty() && !chatUi.empty() && !kit.empty(), "sources readable (run from the repository root)");
    const std::string hit = Line(mainCpp, "case WM_NCHITTEST:{if(InFullscreen())return HTCLIENT;");
    Require(hit.find("int edge=S(4);") != std::string::npos && hit.find("top=p.y<S(3)") != std::string::npos && hit.find("if(!IsZoomed(h))") != std::string::npos, "main.cpp WM_NCHITTEST: the band is still S(4) / S(3), none maximized (RB::sideDip / topDip)");
    Require(mainCpp.find("int S(float value){return (int)std::lround(value*GetDpiForWindow(window)/96.f);}") != std::string::npos, "main.cpp S(): the rounding RB::Px repeats");
    Require(Block(mainCpp, "void Layout(){").find("ApplyPages();SyncResizeGrips();") != std::string::npos, "main.cpp Layout: the grips back on top after the pages");
    Require(Line(mainCpp, "void SyncResizeGrips(){").find("resizeGrips.Sync(window,!IsZoomed(window)&&!IsIconic(window)&&!InFullscreen())") != std::string::npos, "main.cpp: no grips maximized, minimized or in fullscreen");
    Require(Block(chatUi, "void SyncCardFrame(RECT r) {").find("::SyncResizeGrips();") != std::string::npos, "chat-host-ui.h SyncCardFrame: the grips back above the chat's WebView at each placement");
    for (const char* f : {"src/settings-panel.h", "src/history-panel.h", "src/bookmarks-panel.h", "src/download-panel.h"}) {
        const std::string s = Read(f), proc = After(s, "static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM w, LPARAM l) {", 700);   // its first lines
        Require(proc.find("NativeChrome::ResizeBorder::Claim(h, msg, w, l, border)") != std::string::npos, std::string(f) + ": the drawer's window procedure claims the band first");
    }
    const auto layer = kit.find("auto* self = reinterpret_cast<Layer*>(GetWindowLongPtrW(h, GWLP_USERDATA));");
    Require(layer != std::string::npos && kit.substr(layer, 600).find("NativeChrome::ResizeBorder::Claim(h, m, w, l, border)") != std::string::npos,
            "annotation-kit.h Layer: the annotation layers (Mode Focus banner on the top edge) claim the band first");
    {   const std::string bars = Read("src/viewport-scrollbar-host.h"); const auto proc = bars.find("LRESULT CALLBACK ScrollbarProc(HWND h,UINT msg,WPARAM w,LPARAM l){");
        Require(proc != std::string::npos && bars.substr(proc, 300).find("NativeChrome::ResizeBorder::Claim(h,msg,w,l,border)") != std::string::npos, "viewport-scrollbar-host.h: the pages' overlay scrollbars claim the band first"); }

    std::printf("resize_border_test: %d checks passed (window DPI %u)\n", checks, dpi);
    return 0;
}
