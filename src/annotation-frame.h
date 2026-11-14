#pragma once
// Annotation mode — the selection frame's native copy (V3, retours §2 « aucun flash à la capture »).
//
// At a selection the page's frame (src/annotation-script.h: 8 % accent fill, 2 px accent edges, accent label) must
// stay on screen, unchanged, while Page.captureScreenshot takes an image that never contains it. The capture is a
// frame rendered by the page itself, so whatever the page draws is in it; a native window above the WebView is
// not. The swap is ordered so that the screen never misses a pixel:
//   1. the page posts the selection with the frame's exact geometry (CSS px: edges, label box, label text);
//   2. this layer draws the same edges and label at the same device pixels, above the WebView, and waits for the
//      compositor (DwmFlush) so it is on screen;
//   3. the page hides ITS edges and label (« handoff ») — covered by this copy, nothing changes on screen;
//   4. the engine renders the shot: no edges, no label; the fill, which never moved, is removed from the image
//      arithmetically (src/annotation-capture.h, RemoveFill);
//   5. « show »: the page puts its edges back, says « shown » two frames later; only then is this layer hidden.
// Each step can only add an identical copy over the other, never leave a gap. The fill is never swapped: two
// translucent copies would darken while both are up.
// A click-through child layered window (WS_EX_TRANSPARENT, fully transparent pixels): it never takes input. It is
// created at the first selection and destroyed with the mode's other windows when the mode ends.
#include <dwmapi.h>
#include "annotation-kit.h"

namespace NativeChrome { namespace Annotations {
// The page's frame, CSS px of the page's viewport (AnnotationHost::Mark) + where that viewport is.
struct FrameMark {
    RECT page{};             // the WebView in the parent's client pixels
    double dpr = 1;          // the page's devicePixelRatio (system scale x zoom)
    double x = 0, y = 0, w = 0, h = 0;          // edges box
    double lx = 0, ly = 0, lw = 0, lh = 0;      // label box
    std::wstring text;                          // label text (« button.primary · 240×44 »)
    std::uint32_t accent = 0x3E67FF;            // the page's accent (the ints sent to the page script)
};
// Page CSS constants of the frame (annotation-script.h): edges 2 px, label padding 2 / 6, radius 4, font 500 11/16 mono.
namespace FrameSpec { inline constexpr double edge = 2, padX = 6, padY = 2, radius = 4, font = 11, line = 16; }

class SelectionFrame {
    Layer layer_; HINSTANCE instance_ = nullptr; HWND parent_ = nullptr; std::wstring root_;
    unsigned long long shows_ = 0; double lastShowMs_ = 0; RECT rect_{};
public:
    void Init(HINSTANCE instance, HWND parent, std::wstring root) { instance_ = instance; parent_ = parent; root_ = std::move(root); }
    // The window rect (parent client px) of a mark: edges + label, clipped to the page. Pure (tests).
    static RECT Bounds(const FrameMark& m) {
        auto X = [&](double css) { return m.page.left + css * m.dpr; }; auto Y = [&](double css) { return m.page.top + css * m.dpr; };
        const double x0 = std::min(m.x, m.lw > 0 ? m.lx : m.x), y0 = std::min(m.y, m.lh > 0 ? m.ly : m.y);
        const double x1 = std::max(m.x + m.w, m.lw > 0 ? m.lx + m.lw : m.x + m.w), y1 = std::max(m.y + m.h, m.lh > 0 ? m.ly + m.lh : m.y + m.h);
        RECT r{LONG(std::floor(X(x0))) - 1, LONG(std::floor(Y(y0))) - 1, LONG(std::ceil(X(x1))) + 1, LONG(std::ceil(Y(y1))) + 1};
        RECT in{}; if (!IntersectRect(&in, &r, &m.page)) return RECT{};
        return in;
    }
    // Paints the mark into a surface whose top-left is `origin` (parent client px), at `dipScale` px per DIP.
    static void Paint(Ink& ink, const FrameMark& m, POINT origin, float dipScale) {
        const float k = float(m.dpr / dipScale);   // DIP per CSS px
        auto X = [&](double css) { return float((m.page.left - origin.x + css * m.dpr) / dipScale); };
        auto Y = [&](double css) { return float((m.page.top - origin.y + css * m.dpr) / dipScale); };
        const D2D1_COLOR_F accent{((m.accent >> 16) & 255) / 255.f, ((m.accent >> 8) & 255) / 255.f, (m.accent & 255) / 255.f, 1};
        const double e = FrameSpec::edge, w = std::max(m.w, 1.0), h = std::max(m.h, 1.0);
        ink.Fill({X(m.x), Y(m.y), X(m.x + w), Y(m.y + e)}, 0, accent);               // top
        ink.Fill({X(m.x), Y(m.y + h - e), X(m.x + w), Y(m.y + h)}, 0, accent);       // bottom
        ink.Fill({X(m.x), Y(m.y), X(m.x + e), Y(m.y + h)}, 0, accent);               // left
        ink.Fill({X(m.x + w - e), Y(m.y), X(m.x + w), Y(m.y + h)}, 0, accent);       // right
        if (m.lw > 0 && m.lh > 0) {
            const D2D1_RECT_F box{X(m.lx), Y(m.ly), X(m.lx + m.lw), Y(m.ly + m.lh)};
            ink.Fill(box, float(FrameSpec::radius) * k, accent);
            Font f = Mono(float(FrameSpec::font) * k, float(FrameSpec::line) * k); f.system = true; f.weight = 500;
            const D2D1_RECT_F text{box.left + float(FrameSpec::padX) * k, box.top + float(FrameSpec::padY) * k, box.right - float(FrameSpec::padX) * k + .5f, box.top + float(FrameSpec::padY + FrameSpec::line) * k};
            ink.Target()->PushAxisAlignedClip(box, D2D1_ANTIALIAS_MODE_ALIASED);
            ink.Text(m.text, f, text, {1, 1, 1, 1});
            ink.Target()->PopAxisAlignedClip();
        }
    }
    // Draws the mark and returns once the compositor has shown it (or false: no window, nothing visible).
    bool Show(const FrameMark& m) {
        if (!Ink::Shared(root_)) return false;
        if (!layer_.Window()) {
            Layer::Options o; o.child = true; o.clickThrough = true; o.title = L"";
            if (!layer_.Create(instance_, parent_, o)) return false;
        }
        const RECT r = Bounds(m); if (IsRectEmpty(&r)) return false;
        const POINT origin{r.left, r.top}; const float scale = layer_.Scale();
        layer_.paint = [m, origin, scale](Ink& ink, float, float) { Paint(ink, m, origin, scale); };
        if (!layer_.Present(r, true)) return false;
        rect_ = r; ++shows_;
        DwmFlush();   // on screen before the page hides its own copy
        return true;
    }
    void Hide() { layer_.Hide(); }
    void Destroy() { layer_.Destroy(); }
    bool Visible() const { return layer_.Shown(); }
    HWND Window() const { return layer_.Window(); }
    RECT Rect() const { return rect_; }
    unsigned long long Shows() const { return shows_; }
    Layer& LayerForTest() { return layer_; }
};
}} // namespace NativeChrome::Annotations
