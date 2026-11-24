#pragma once
// Mode Focus (design/focus-mode-plan.md; user request 2026-11-23, « un petit corner radius en haut du tiroir »): the
// docked drawers (Réglages, Historique, Favoris, Téléchargements) round their two TOP corners, 6 DIP, antialiased, while
// focus mode places them under the floating panel. Their bottom corners do not change.
// The drawers are keyed popup windows (NativeUI::KeyedDrawer, PR #117): the window never moves while it slides, each slide
// step is one present of the content drawn `lead` pixels right of its place with the strip it has not entered painted in
// the key colour (transparent, click-through). The corners follow the same model: the drawer's own paint keys out every
// corner pixel the rounded shape does not cover completely (CutRegion, with the slide, in the same present), and a tiny
// per-pixel-alpha layered window per corner, owned by the drawer (always right above it, click-through), lays the partly
// covered pixels back in the drawer's surface colour at their coverage: over whatever lies under them (the page), the arc
// is antialiased. The trailing (right) corner sits on the window's fixed right edge: its window never moves. The leading
// (left) corner moves with the content while the drawer slides; a moved window reaches the screen a composition after the
// present, so its window shows only at rest (the slide keeps the keyed, aliased arc for those few frames).
// Coverage: 8 x 8 samples per pixel. UI thread only.
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace NativeChrome { namespace DrawerCorners {

// Coverage of the top-left corner square (n x n pixels, n = ceil(radius)) by a rectangle whose top-left corner is
// rounded with `radius` pixels: row-major, y down, x right. Mirrored for the top-right corner.
struct Mask { int size = 0; float radius = 0; std::vector<float> coverage; };
inline Mask TopLeft(float radius) {
    Mask m; m.radius = radius; m.size = radius > 0 ? static_cast<int>(std::ceil(radius)) : 0; m.coverage.assign(static_cast<size_t>(m.size) * m.size, 1.f);
    constexpr int k = 8;
    for (int y = 0; y < m.size; ++y) for (int x = 0; x < m.size; ++x) {
        int in = 0;
        for (int j = 0; j < k; ++j) for (int i = 0; i < k; ++i) {
            const float sx = x + (i + .5f) / k, sy = y + (j + .5f) / k;
            if (sx >= radius || sy >= radius) { ++in; continue; }
            const float dx = radius - sx, dy = radius - sy; if (dx * dx + dy * dy <= radius * radius) ++in;
        }
        m.coverage[static_cast<size_t>(y) * m.size + x] = in / float(k * k);
    }
    return m;
}
// Pixels of a corner the drawer itself must not draw (not fully covered), as a region in the drawer window's pixels.
// `left`: the visible column's left edge; `right`: its right edge (exclusive). Empty for a radius of 0 or a column
// narrower than its two corners.
inline HRGN Cut(const Mask& m, int left, int right) {
    HRGN cut = CreateRectRgn(0, 0, 0, 0);
    if (m.size <= 0 || right - left < 2 * m.size) return cut;
    for (int y = 0; y < m.size; ++y) {
        int run = 0; while (run < m.size && m.coverage[static_cast<size_t>(y) * m.size + run] < 1.f) ++run;   // partial pixels from the edge inward
        if (run <= 0) continue;
        HRGN a = CreateRectRgn(left, y, left + run, y + 1), b = CreateRectRgn(right - run, y, right, y + 1);
        CombineRgn(cut, cut, a, RGN_OR); CombineRgn(cut, cut, b, RGN_OR); DeleteObject(a); DeleteObject(b);
    }
    return cut;
}

// The two antialiased corners of one drawer window: 0 the leading (left) one, 1 the trailing (right) one.
class Pair {
    HWND windows_[2]{}; Mask mask_; POINT placed_[2]{{-99999, -99999}, {-99999, -99999}}; bool shown_[2]{}; std::uint32_t colours_[2]{};
    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
        if (msg == WM_NCHITTEST) return HTTRANSPARENT;
        if (msg == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
        return DefWindowProcW(h, msg, w, l);
    }
    // `edge`: the colour of the drawer's own outer pixel column (its 1 px leading hairline on the left), `surface` inside.
    bool Render(int i, const POINT& at, float edge, float surface) {
        const int n = mask_.size; if (n <= 0) return false;
        BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth = n; bi.bmiHeader.biHeight = -n; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
        HDC screen = GetDC(nullptr); HDC mem = CreateCompatibleDC(screen); void* bits = nullptr; HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        bool ok = false;
        if (bmp && bits) {
            auto* px = static_cast<std::uint32_t*>(bits); const bool right = i == 1;
            for (int y = 0; y < n; ++y) for (int x = 0; x < n; ++x) {
                const int mx = right ? n - 1 - x : x;   // mirrored on the right
                const float c = mask_.coverage[static_cast<size_t>(y) * n + mx], a = c < 1.f ? c : 0.f;   // full pixels belong to the drawer itself
                const float grey = (!right && mx == 0) ? edge : surface;
                const auto A = static_cast<std::uint32_t>(std::lround(a * 255)), C = static_cast<std::uint32_t>(std::lround(grey * 255 * a));
                px[y * n + x] = (A << 24) | (C << 16) | (C << 8) | C;
            }
            HGDIOBJ old = SelectObject(mem, bmp); POINT src{0, 0}; SIZE size{n, n}; POINT dst = at; BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
            ok = UpdateLayeredWindow(windows_[i], screen, &dst, &size, mem, &src, 0, &blend, ULW_ALPHA) != FALSE;
            SelectObject(mem, old);
        }
        if (bmp) DeleteObject(bmp);
        DeleteDC(mem); ReleaseDC(nullptr, screen);
        return ok;
    }
    void HideOne(int i) { if (windows_[i] && shown_[i]) ShowWindow(windows_[i], SW_HIDE); shown_[i] = false; }
public:
    ~Pair() { Destroy(); }
    void Destroy() { for (int i = 0; i < 2; ++i) { if (windows_[i] && IsWindow(windows_[i])) DestroyWindow(windows_[i]); windows_[i] = nullptr; shown_[i] = false; } }
    void Hide() { HideOne(0); HideOne(1); }
    // The leading corner steps away before a present that moves the content (a slide step).
    void HideLeading() { HideOne(0); }
    // The corners' radius in pixels (0: none: everything hidden, nothing keyed). A new radius makes a new mask.
    void SetRadius(float radius) {
        if (mask_.radius != radius) { mask_ = TopLeft(radius); placed_[0] = placed_[1] = {-99999, -99999}; colours_[0] = colours_[1] = 0; }
        if (radius <= 0) Hide();
    }
    float Radius() const { return mask_.radius; }
    int Size() const { return mask_.size; }
    // What the drawer's paint keys out (window pixels): the two top corners' partly covered pixels of its content column
    // [left, right). Empty without corners. Delete it after use.
    HRGN CutRegion(int left, int right) const { return Cut(mask_, left, right); }
    // Lays those pixels back at their coverage: the drawer `owner` at `window` (screen pixels), its content column
    // [left, right) in window pixels. `leading`: the left corner too (false while the drawer slides, see above).
    void Place(HWND owner, const RECT& window, int left, int right, float edge, float surface, bool leading) {
        if (mask_.size <= 0 || !owner || right - left < 2 * mask_.size) { Hide(); return; }
        static bool registered = false; HINSTANCE inst = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(owner, GWLP_HINSTANCE));
        if (!registered) { WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = inst; wc.lpszClassName = L"BrowserDrawerCorner"; RegisterClassW(&wc); registered = true; }
        const POINT at[2] = {{window.left + left, window.top}, {window.left + right - mask_.size, window.top}};
        const std::uint32_t key = (static_cast<std::uint32_t>(std::lround(edge * 255)) << 8) | static_cast<std::uint32_t>(std::lround(surface * 255));
        for (int i = 0; i < 2; ++i) {
            if (i == 0 && !leading) { HideOne(0); continue; }
            if (!windows_[i]) windows_[i] = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"BrowserDrawerCorner", L"", WS_POPUP, 0, 0, 1, 1, owner, nullptr, inst, nullptr);
            if (!windows_[i]) continue;
            if (placed_[i].x != at[i].x || placed_[i].y != at[i].y || colours_[i] != key) { if (Render(i, at[i], edge, surface)) { placed_[i] = at[i]; colours_[i] = key; } }
            if (!shown_[i]) { SetWindowPos(windows_[i], nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER | SWP_SHOWWINDOW); shown_[i] = true; }
        }
    }
    bool Shown(int i) const { return i >= 0 && i < 2 && shown_[i]; }
    HWND Window(int i) const { return i >= 0 && i < 2 ? windows_[i] : nullptr; }
};

}}
