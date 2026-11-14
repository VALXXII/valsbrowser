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
// V3-B: the spacing inspector (Paper 5.3) lives with the frame, so it takes the same road: the page sends its bands
// and value labels with the frame's geometry, this copy draws them at the same pixels (hatches in the page's phase:
// a CSS repeating-linear-gradient at 135° / 45°, 1 px every 5 px, anchored on each band's box), the « handoff » hides
// the page's — the shot never contains them either.
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
    struct Band { int kind = 0; double x = 0, y = 0, w = 0, h = 0; };                       // V3-B: 0 padding, 1 gap
    struct Label { int kind = 0; double x = 0, y = 0, w = 0, h = 0; std::wstring text; };
    std::vector<Band> bands; std::vector<Label> labels; double radius = 0;     // V3-B inspector (empty: none)
};
// Page CSS constants of the inspector (annotation-script.h): hatch 1 px every 5 px, label 14 px high, radius 3,
// 400 10/14 mono, padding 0 4; gap pink rgb(224 71 158), gap label #D63E92.
namespace SpaceSpec {
inline constexpr double pitch = 5, line = 1, labelH = 14, labelR = 3, labelPad = 4, font = 10;
inline constexpr float padLine = .4f, padFill = .08f, gapLine = .45f, gapFill = .12f;
inline constexpr std::uint32_t gap = 0xE0479E, gapLabel = 0xD63E92;
}
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
        double x0 = std::min(m.x, m.lw > 0 ? m.lx : m.x), y0 = std::min(m.y, m.lh > 0 ? m.ly : m.y);
        double x1 = std::max(m.x + m.w, m.lw > 0 ? m.lx + m.lw : m.x + m.w), y1 = std::max(m.y + m.h, m.lh > 0 ? m.ly + m.lh : m.y + m.h);
        for (const auto& b : m.bands) { x0 = std::min(x0, b.x); y0 = std::min(y0, b.y); x1 = std::max(x1, b.x + b.w); y1 = std::max(y1, b.y + b.h); }
        for (const auto& l : m.labels) { x0 = std::min(x0, l.x); y0 = std::min(y0, l.y); x1 = std::max(x1, l.x + l.w); y1 = std::max(y1, l.y + l.h); }
        RECT r{LONG(std::floor(X(x0))) - 1, LONG(std::floor(Y(y0))) - 1, LONG(std::ceil(X(x1))) + 1, LONG(std::ceil(Y(y1))) + 1};
        RECT in{}; if (!IntersectRect(&in, &r, &m.page)) return RECT{};
        return in;
    }
    // V3-B: a band's hatches exactly as the page draws them. CSS repeating-linear-gradient(angle, c 0 1px, transparent
    // 1px 5px) on a box w x h: the gradient line goes through the box's centre in the direction of `angle`, its 0 is
    // where the perpendicular through the corner meets it (length |w sin a| + |h cos a|); a pixel is on a line when its
    // position along the gradient line modulo 5 is under 1. Drawn as 1 px wide strokes across the box, clipped to it.
    static void PaintHatch(Ink& ink, D2D1_RECT_F box, double cssW, double cssH, float k, double angleDeg, D2D1_COLOR_F lineColor, D2D1_COLOR_F fillColor, float radius, int roundTop) {
        auto* t = ink.Target(); if (box.right <= box.left || box.bottom <= box.top) return;
        ID2D1RoundedRectangleGeometry* round = nullptr; ID2D1Layer* layer = nullptr; bool pushed = false;
        if (radius > 0 && roundTop) {   // the top band's top corners / the bottom band's bottom corners (inner radius)
            const float r = radius; D2D1_RECT_F ext = box; if (roundTop > 0) ext.bottom += r; else ext.top -= r;
            if (SUCCEEDED(ink.Factory()->CreateRoundedRectangleGeometry(D2D1::RoundedRect(ext, r, r), &round)) && SUCCEEDED(t->CreateLayer(nullptr, &layer))) {
                t->PushLayer(D2D1::LayerParameters(box, round), layer); pushed = true;
            }
        }
        if (!pushed) t->PushAxisAlignedClip(box, D2D1_ANTIALIAS_MODE_ALIASED);
        ink.Fill(box, 0, fillColor);
        const double a = angleDeg * 3.14159265358979 / 180, gx = std::sin(a), gy = -std::cos(a);   // gradient direction (CSS: 0deg = up)
        const double len = std::fabs(cssW * gx) + std::fabs(cssH * gy);
        const double cx = cssW / 2, cy = cssH / 2, sx = cx - gx * len / 2, sy = cy - gy * len / 2;   // gradient start (CSS px, box space)
        // A line at position p along the gradient is the set {q : (q - s)·g = p}; direction along the line: (-gy, gx).
        const double reach = std::hypot(cssW, cssH) + 2;
        ID2D1SolidColorBrush* brush = nullptr;
        if (SUCCEEDED(t->CreateSolidColorBrush(lineColor, &brush))) {
            for (double p = SpaceSpec::line / 2; p < len + SpaceSpec::pitch; p += SpaceSpec::pitch) {
                const double px = sx + gx * p, py = sy + gy * p;
                const D2D1_POINT_2F a0{box.left + float((px + gy * reach) * k), box.top + float((py - gx * reach) * k)};
                const D2D1_POINT_2F a1{box.left + float((px - gy * reach) * k), box.top + float((py + gx * reach) * k)};
                t->DrawLine(a0, a1, brush, float(SpaceSpec::line) * k);
            }
            brush->Release();
        }
        if (pushed) t->PopLayer(); else t->PopAxisAlignedClip();
        Release(layer); Release(round);
    }
    static void PaintSpace(Ink& ink, const FrameMark& m, const std::function<float(double)>& X, const std::function<float(double)>& Y, float k) {
        const D2D1_COLOR_F accent{((m.accent >> 16) & 255) / 255.f, ((m.accent >> 8) & 255) / 255.f, (m.accent & 255) / 255.f, 1};
        const D2D1_COLOR_F pink{((SpaceSpec::gap >> 16) & 255) / 255.f, ((SpaceSpec::gap >> 8) & 255) / 255.f, (SpaceSpec::gap & 255) / 255.f, 1};
        auto with = [](D2D1_COLOR_F c, float a) { c.a = a; return c; };
        for (size_t i = 0; i < m.bands.size(); ++i) {
            const auto& b = m.bands[i]; const D2D1_RECT_F box{X(b.x), Y(b.y), X(b.x + b.w), Y(b.y + b.h)};
            const bool pad = b.kind == 0;
            const int roundTop = pad && m.radius > 0 ? (i == 0 ? 1 : i == 1 ? -1 : 0) : 0;   // the page rounds bands 0 (top) and 1 (bottom)
            const float r = float(std::min({m.radius, b.h, b.w})) * k;
            PaintHatch(ink, box, b.w, b.h, k, pad ? 135 : 45, with(pad ? accent : pink, pad ? SpaceSpec::padLine : SpaceSpec::gapLine), with(pad ? accent : pink, pad ? SpaceSpec::padFill : SpaceSpec::gapFill), r, roundTop);
        }
        for (const auto& l : m.labels) {
            const D2D1_RECT_F box{X(l.x), Y(l.y), X(l.x + l.w), Y(l.y + l.h)};
            ink.Fill(box, float(SpaceSpec::labelR) * k, l.kind == 0 ? accent : D2D1_COLOR_F{((SpaceSpec::gapLabel >> 16) & 255) / 255.f, ((SpaceSpec::gapLabel >> 8) & 255) / 255.f, (SpaceSpec::gapLabel & 255) / 255.f, 1});
            Font f = Mono(float(SpaceSpec::font) * k, float(SpaceSpec::labelH) * k); f.system = true; f.weight = 400;
            ink.Text(l.text, f, {box.left, box.top, box.right, box.bottom}, {1, 1, 1, 1}, false, Align::Center, true);
        }
    }
    // Paints the mark into a surface whose top-left is `origin` (parent client px), at `dipScale` px per DIP.
    static void Paint(Ink& ink, const FrameMark& m, POINT origin, float dipScale) {
        const float k = float(m.dpr / dipScale);   // DIP per CSS px
        auto X = [&](double css) { return float((m.page.left - origin.x + css * m.dpr) / dipScale); };
        auto Y = [&](double css) { return float((m.page.top - origin.y + css * m.dpr) / dipScale); };
        const D2D1_COLOR_F accent{((m.accent >> 16) & 255) / 255.f, ((m.accent >> 8) & 255) / 255.f, (m.accent & 255) / 255.f, 1};
        if (!m.bands.empty()) PaintSpace(ink, m, X, Y, k);   // under the edges and label, as in the page
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
