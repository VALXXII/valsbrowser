#pragma once
// The browser window's resize border stays grabbable over everything the window holds (owner's report, trial 0.3.952,
// also in 0.3.840: with the chat open in « Panneau » the window could no longer be resized from its right edge, its
// bottom edge or its bottom-right corner; the same over an open drawer).
//
// The window has no system frame (WM_NCCALCSIZE 0): its border is a band inside its own pixels, answered by its
// WM_NCHITTEST (src/main.cpp: 4 DIP on the left, right and bottom, 3 DIP at the top, rounded as S() rounds; no band when
// maximized or in fullscreen). Windows sends WM_NCHITTEST only to the window under the pointer, the deepest child or a
// popup above, and two kinds of windows lay over the band and answered « client » there:
//   * the chat panel's WebView, flush with the window's right and bottom edges in « Panneau »: a window of the WebView2
//     process (windowed hosting, the default), whose hit test our code never sees;
//   * the docked drawers (Réglages, Historique, Favoris, Téléchargements): our own popups, flush with the window's right
//     edge and bottom outside Mode Focus.
// Two answers, both giving the main window's own sizing code (its WM_NCHITTEST decides: maximized, fullscreen):
//   * Claim: a window of ours over the band (a drawer) calls it first in its window procedure. On the band it answers the
//     main window's sizing code, shows the sizing cursor and hands the press to the main window, whose DefWindowProc
//     runs the system's own size loop, as on its uncovered border. Anywhere else it does nothing.
//   * Grips: for the children of another process (the chat's WebView; the pages, 2 DIP inside the window, over the
//     band's inner half), four invisible layered child windows on the band (alpha 1/255 everywhere: never seen, yet
//     hit-tested, like the split view's separator grips), above every other child, each claiming its points. Their
//     owner keeps them on top after each layout and each placement of the chat: a child created later lands below
//     them, but one given to the window later (SetParent, WebView2's put_ParentWindow) lands above, as does one
//     raised (HWND_TOP) by its own code.
// The other windows of ours on the window's edges claim too: the annotation layers (Mode Focus: the banner on the top
// edge) and the pages' overlay scrollbars (on the pages' right and bottom edges).
// Mode Focus and the side layouts (#127) keep the page, the chat card and the drawers 4 DIP inside the window: the band is
// free there by geometry, the grips and the claim cover it anyway.
#include <windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace NativeChrome { namespace ResizeBorder {

// src/main.cpp WM_NCHITTEST: edge=S(4) (left, right, bottom) and S(3) (top). tests/resize_border_test.cpp holds the two
// in step.
inline constexpr float sideDip = 4, topDip = 3;
inline int Px(float dip, unsigned dpi) { return static_cast<int>(std::lround(dip * (dpi ? dpi : 96) / 96.f)); }   // = main.cpp S()

// The sizing code (HTLEFT .. HTBOTTOMRIGHT) at client pixel (x, y) of a width x height client at `dpi`, HTNOWHERE off
// the band. `framed` false: maximized or in fullscreen, no band. The order of src/main.cpp: corners first.
inline int Hit(int x, int y, int width, int height, unsigned dpi, bool framed = true) {
    if (!framed || x < 0 || y < 0 || x >= width || y >= height) return HTNOWHERE;
    const int edge = Px(sideDip, dpi), top = Px(topDip, dpi);
    const bool l = x < edge, r = x >= width - edge, t = y < top, b = y >= height - edge;
    if (t && l) return HTTOPLEFT;
    if (t && r) return HTTOPRIGHT;
    if (b && l) return HTBOTTOMLEFT;
    if (b && r) return HTBOTTOMRIGHT;
    if (l) return HTLEFT;
    if (r) return HTRIGHT;
    if (t) return HTTOP;
    if (b) return HTBOTTOM;
    return HTNOWHERE;
}
inline bool Sizing(LRESULT code) { return code >= HTLEFT && code <= HTBOTTOMRIGHT; }
// The system cursor of a sizing code (IDC_SIZEWE, IDC_SIZENS, IDC_SIZENWSE, IDC_SIZENESW; IDC_ARROW otherwise).
inline int CursorId(LRESULT code) {
    switch (code) {
    case HTLEFT: case HTRIGHT: return 32644;
    case HTTOP: case HTBOTTOM: return 32645;
    case HTTOPLEFT: case HTBOTTOMRIGHT: return 32642;
    case HTTOPRIGHT: case HTBOTTOMLEFT: return 32643;
    }
    return 32512;
}
// The band as four strips in client pixels: 0 top and 1 bottom across the whole width (the corners), 2 left and 3 right
// between them. Together they hold every band pixel once and nothing else.
inline std::array<RECT, 4> Strips(int width, int height, unsigned dpi) {
    width = std::max(0, width); height = std::max(0, height);
    const int edge = Px(sideDip, dpi), t = std::min(Px(topDip, dpi), height), b = std::max(t, height - edge);
    const int l = std::min(edge, width), r = std::max(l, width - edge);
    return {{{0, 0, width, t}, {0, b, width, height}, {0, t, l, b}, {r, t, width, b}}};
}

// ---- Claim: a window of ours lying over the band ----
// The window whose border it is: the root of a child, the root owner of a popup (a drawer). None for a window alone.
inline HWND Frame(HWND h) { HWND top = h ? GetAncestor(h, GA_ROOTOWNER) : nullptr; return top && top != h ? top : nullptr; }
// `top`'s sizing code at the screen point packed in `screen` (WM_NCHITTEST's lParam), HTNOWHERE elsewhere. The band's
// geometry first (most calls: a pointer moving anywhere over the window), then `top`'s own WM_NCHITTEST, which knows
// whether it is maximized or in fullscreen.
inline LRESULT FrameHit(HWND top, LPARAM screen) {
    if (!top || IsIconic(top)) return HTNOWHERE;
    POINT p{static_cast<short>(LOWORD(screen)), static_cast<short>(HIWORD(screen))}; RECT c{};
    if (!GetClientRect(top, &c) || !ScreenToClient(top, &p)) return HTNOWHERE;
    if (!Hit(p.x, p.y, c.right, c.bottom, GetDpiForWindow(top))) return HTNOWHERE;
    const LRESULT code = SendMessageW(top, WM_NCHITTEST, 0, screen);
    return Sizing(code) ? code : HTNOWHERE;
}
// First thing in the window procedure: `if (Claim(h, msg, w, l, r)) return r;`. True when it took the message.
inline bool Claim(HWND h, UINT msg, WPARAM w, LPARAM l, LRESULT& result) {
    switch (msg) {
    case WM_NCHITTEST: { const LRESULT code = FrameHit(Frame(h), l); if (!code) return false; result = code; return true; }
    case WM_SETCURSOR:
        if (reinterpret_cast<HWND>(w) != h || !Sizing(LOWORD(l))) return false;
        SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(CursorId(LOWORD(l))))); result = TRUE; return true;
    // The presses on the band go to the main window as if they were on its own border: the left button starts the size
    // loop (a double click on the top edge stretches it to the screen's height), the others do what they do there.
    case WM_NCLBUTTONDOWN: case WM_NCLBUTTONUP: case WM_NCLBUTTONDBLCLK:
    case WM_NCRBUTTONDOWN: case WM_NCRBUTTONUP: case WM_NCRBUTTONDBLCLK:
    case WM_NCMBUTTONDOWN: case WM_NCMBUTTONUP: case WM_NCMBUTTONDBLCLK:
    case WM_NCXBUTTONDOWN: case WM_NCXBUTTONUP: case WM_NCXBUTTONDBLCLK: {
        HWND top = Frame(h); if (!top || !Sizing(LOWORD(w))) return false;
        result = SendMessageW(top, msg, w, l); return true;
    }
    }
    return false;
}

// ---- Grips: the band over the children of another process ----
class Grips {
    HWND windows_[4]{}; RECT placed_[4]{}; SIZE drawn_[4]{}; bool shown_[4]{};
    static LRESULT CALLBACK Proc(HWND g, UINT msg, WPARAM w, LPARAM l) {
        LRESULT r = 0; if (Claim(g, msg, w, l, r)) return r;
        switch (msg) {
        case WM_NCHITTEST: return HTTRANSPARENT;   // not a sizing point (the band is changing): left to what lies below
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(g, &ps); EndPaint(g, &ps); return 0; }
        }
        return DefWindowProcW(g, msg, w, l);   // WM_MOUSEACTIVATE: the parent's answer, as on its own border
    }
    // The grip's pixels: black at alpha 1/255 everywhere (premultiplied). A layered window is hit-tested only where its
    // alpha is not 0, and only once it has content.
    bool Render(int i, int w, int h) {
        BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
        HDC screen = GetDC(nullptr); HDC mem = CreateCompatibleDC(screen); void* bits = nullptr; HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        bool ok = false;
        if (bmp && bits) {
            std::fill_n(static_cast<std::uint32_t*>(bits), static_cast<size_t>(w) * h, 0x01000000u);
            HGDIOBJ old = SelectObject(mem, bmp); POINT src{0, 0}; SIZE size{w, h}; BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
            ok = UpdateLayeredWindow(windows_[i], screen, nullptr, &size, mem, &src, 0, &blend, ULW_ALPHA) != FALSE;
            SelectObject(mem, old);
        }
        if (bmp) DeleteObject(bmp);
        DeleteDC(mem); ReleaseDC(nullptr, screen);
        return ok;
    }
    bool Ensure(HWND parent) {
        if (windows_[0] && GetParent(windows_[0]) == parent) return true;
        Destroy();
        HINSTANCE inst = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent, GWLP_HINSTANCE));
        WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = inst; wc.lpszClassName = L"BrowserResizeGrip"; RegisterClassW(&wc);   // fails harmlessly once registered
        for (auto& g : windows_) {
            g = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE, L"BrowserResizeGrip", L"", WS_CHILD, 0, 0, 1, 1, parent, nullptr, inst, nullptr);
            if (!g) { Destroy(); return false; }
        }
        return true;
    }
public:
    Grips() = default; Grips(const Grips&) = delete; Grips& operator=(const Grips&) = delete;
    ~Grips() { Destroy(); }
    void Destroy() { for (int i = 0; i < 4; ++i) { if (windows_[i] && IsWindow(windows_[i])) DestroyWindow(windows_[i]); windows_[i] = nullptr; placed_[i] = {}; drawn_[i] = {}; shown_[i] = false; } }
    void Hide() { for (int i = 0; i < 4; ++i) if (windows_[i] && shown_[i]) { ShowWindow(windows_[i], SW_HIDE); shown_[i] = false; } }
    bool Ours(HWND h) const { for (HWND g : windows_) if (g && g == h) return true; return false; }
    // The four grips are the topmost children of `parent` (hidden children above them would show over them).
    bool OnTop(HWND parent) const {
        int n = 0; for (HWND c = GetWindow(parent, GW_CHILD); c && n < 4; c = GetWindow(c, GW_HWNDNEXT), ++n) if (!Ours(c)) return false;
        return n == 4;
    }
    // On `parent`'s band, above all its other children; hidden while `on` is false (maximized, fullscreen, minimized: no
    // band). Cheap when nothing changed: it runs on every layout and every placement of the chat.
    void Sync(HWND parent, bool on) {
        RECT c{}; if (!parent || !GetClientRect(parent, &c) || !on || c.right <= 0 || c.bottom <= 0) { Hide(); return; }
        if (!Ensure(parent)) return;
        const auto strips = Strips(c.right, c.bottom, GetDpiForWindow(parent)); const bool top = OnTop(parent);
        for (int i = 0; i < 4; ++i) {
            const RECT& s = strips[i]; const int w = s.right - s.left, h = s.bottom - s.top;
            if (w <= 0 || h <= 0) { if (shown_[i]) { ShowWindow(windows_[i], SW_HIDE); shown_[i] = false; } continue; }
            if (!top || !shown_[i] || !EqualRect(&s, &placed_[i])) SetWindowPos(windows_[i], HWND_TOP, s.left, s.top, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
            if (drawn_[i].cx != w || drawn_[i].cy != h) drawn_[i] = Render(i, w, h) ? SIZE{w, h} : SIZE{};
            placed_[i] = s; shown_[i] = true;
        }
    }
    HWND Window(int i) const { return i >= 0 && i < 4 ? windows_[i] : nullptr; }
    bool Shown(int i) const { return i >= 0 && i < 4 && shown_[i]; }
};

}}  // namespace NativeChrome::ResizeBorder
