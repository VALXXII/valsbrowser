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
// V3-B: the spacing inspector (Paper 5.3) lives with the frame, so it takes the same road: the page sends its padding
// ring, gap rectangles and value labels with the frame's geometry, this copy draws them at the same pixels, the
// « handoff » hides the page's — the shot never contains them either. As in the page: the ring is ONE geometry (padding
// box with its radius minus the content box, even-odd) and the gaps ONE union, each hatched by a CSS-like
// repeating-linear-gradient (135° / 45°, 1 px every 5 px) whose box is the viewport — one phase for everything, no
// seam where the sides meet; order: hatches, the frame's edges, its label, the value labels on top.
#include <dwmapi.h>
#include "annotation-kit.h"

namespace NativeChrome { namespace Annotations {
// The page's frame, CSS px of the page's viewport (AnnotationHost::Mark) + where that viewport is.
struct FrameMark {
    RECT page{};             // the WebView in the parent's client pixels
    double dpr = 1;          // the page's devicePixelRatio (system scale x zoom)
    double x = 0, y = 0, w = 0, h = 0;          // edges box
    double lx = 0, ly = 0, lw = 0, lh = 0;      // label box (R14: the name, « button.primary »)
    std::wstring text;                          // label text
    double rx = 0, ry = 0, rw = 0, rh = 0;      // R14: the size label at the right (« 240x44 »), rw 0 = none
    std::wstring rtext;
    std::uint32_t accent = 0x3E67FF;            // the page's accent (the ints sent to the page script)
    // Final pass 1, defect 3 (R45): the labels as the page drew them, taken from the screen just before the handoff
    // (parent client px, premultiplied BGRA, opaque): the copy shows these very pixels instead of drawing the text again
    // with another engine. Empty: the text is drawn (Paint's fallback).
    struct Pixels { RECT at{}; std::vector<std::uint32_t> bits; bool Ok() const { return at.right > at.left && at.bottom > at.top && bits.size() == size_t(at.right - at.left) * size_t(at.bottom - at.top); } };
    Pixels leftPx, rightPx;
    Pixels whole;   // final pass 1: the whole mark's box (edges, labels, hatches, value labels) as on screen; shown as is
    struct Gap { double x = 0, y = 0, w = 0, h = 0; };
    struct Label { int kind = 0; double x = 0, y = 0, w = 0, h = 0; std::wstring text; };   // 0 padding, 1 gap
    // V3-B inspector: pad = [outer x, y, w, h, inner x, y, w, h] (CSS px), radii of the outer / inner rounded rects,
    // vw / vh = the viewport (the hatches' gradient box); empty: none.
    bool hasPad = false; double pad[8]{}; double radius = 0, innerRadius = 0, vw = 0, vh = 0;
    std::vector<Gap> gaps; std::vector<Label> labels;
};
// Page CSS constants of the inspector (annotation-script.h): hatch 1 px every 5 px, label 14 px high, radius 3,
// 400 10/14 Inter Display (D8), padding 0 4; gap pink rgb(224 71 158), gap label #D63E92.
namespace SpaceSpec {
inline constexpr double pitch = 5, line = 1, labelH = 14, labelR = 3, labelPad = 4, font = 10;
inline constexpr float padLine = .4f, padFill = .08f, gapLine = .45f, gapFill = .12f;
inline constexpr std::uint32_t gap = 0xE0479E, gapLabel = 0xD63E92;
}
// Page CSS constants of the frame (annotation-script.h; R14 = Paper 18QK-0 / 18QQ-0): edges 2 px, labels 20 px high,
// padding 1 / 6, top corners 3, Inter Display 400 13/18, text #FFFFFF (Paper: #FFFFFFE6 = 4.03:1 on #3E67FF, under the 4.5:1 a 13 px text needs: lifted to white, as the band's BETA).
namespace FrameSpec { inline constexpr double edge = 2, padX = 6, padY = 1, radius = 3, font = 13, line = 18; inline constexpr float textAlpha = 1.f; }
// R27: the page draws every frame (hover, selection, conversation, bubble) as one box whose border is the 2 px edge
// rounded to whole device pixels (edgePx in annotation-script.h): 2 at 100 %, 3 at 125 % and 150 % (2.4 / 2 CSS px).
inline double EdgeCss(double dpr) { if (!(dpr > 0)) dpr = 1; return std::max(1.0, std::round(FrameSpec::edge * dpr)) / dpr; }

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
        if (m.rw > 0 && m.rh > 0) { x0 = std::min(x0, m.rx); y0 = std::min(y0, m.ry); x1 = std::max(x1, m.rx + m.rw); y1 = std::max(y1, m.ry + m.rh); }   // R14
        if (m.hasPad) { x0 = std::min(x0, m.pad[0]); y0 = std::min(y0, m.pad[1]); x1 = std::max(x1, m.pad[0] + m.pad[2]); y1 = std::max(y1, m.pad[1] + m.pad[3]); }
        for (const auto& b : m.gaps) { x0 = std::min(x0, b.x); y0 = std::min(y0, b.y); x1 = std::max(x1, b.x + b.w); y1 = std::max(y1, b.y + b.h); }
        for (const auto& l : m.labels) { x0 = std::min(x0, l.x); y0 = std::min(y0, l.y); x1 = std::max(x1, l.x + l.w); y1 = std::max(y1, l.y + l.h); }
        RECT r{LONG(std::floor(X(x0))) - 1, LONG(std::floor(Y(y0))) - 1, LONG(std::ceil(X(x1))) + 1, LONG(std::ceil(Y(y1))) + 1};
        RECT in{}; if (!IntersectRect(&in, &r, &m.page)) return RECT{};
        return in;
    }
    // V3-B: one hatched geometry exactly as the page draws it. CSS repeating-linear-gradient(angle, c 0 1px, transparent
    // 1px 5px) on the VIEWPORT box (vw x vh): the gradient line goes through its centre in the direction of `angle`,
    // its 0 where the perpendicular through the corner meets it (length |vw sin a| + |vh cos a|); a point is on a line
    // when its position along that line modulo 5 is under 1. Filled, then 1 px strokes across the geometry's bounds,
    // all inside one layer masked by the geometry (anti-aliased on its contours only: no inner edge, no seam).
    static void PaintHatched(Ink& ink, ID2D1Geometry* geo, double cssX0, double cssY0, double cssX1, double cssY1, double vw, double vh, double angleDeg,
                             const std::function<float(double)>& X, const std::function<float(double)>& Y, float k, D2D1_COLOR_F line, D2D1_COLOR_F fill) {
        auto* t = ink.Target(); if (!geo) return;
        const D2D1_RECT_F bounds{X(cssX0) - 1, Y(cssY0) - 1, X(cssX1) + 1, Y(cssY1) + 1};
        ID2D1Layer* layer = nullptr; if (FAILED(t->CreateLayer(nullptr, &layer))) return;
        t->PushLayer(D2D1::LayerParameters(bounds, geo, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE), layer);
        ink.Fill(bounds, 0, fill);
        const double a = angleDeg * 3.14159265358979 / 180, gx = std::sin(a), gy = -std::cos(a);
        const double len = std::fabs(vw * gx) + std::fabs(vh * gy), sx = vw / 2 - gx * len / 2, sy = vh / 2 - gy * len / 2;
        double pmin = 1e18, pmax = -1e18;
        for (double cx : {cssX0, cssX1}) for (double cy : {cssY0, cssY1}) { const double p = (cx - sx) * gx + (cy - sy) * gy; pmin = std::min(pmin, p); pmax = std::max(pmax, p); }
        const double reach = (cssX1 - cssX0) + (cssY1 - cssY0) + 4, cx = (cssX0 + cssX1) / 2, cy = (cssY0 + cssY1) / 2;
        ID2D1SolidColorBrush* brush = nullptr;
        if (SUCCEEDED(t->CreateSolidColorBrush(line, &brush))) {
            for (double n = std::floor((pmin - 1) / SpaceSpec::pitch); n * SpaceSpec::pitch <= pmax + 1; n += 1) {
                const double p = n * SpaceSpec::pitch + SpaceSpec::line / 2;
                // the line's point nearest the bounds' centre, then +- reach along the line's direction (-gy, gx)
                const double t0 = (cx - sx) * -gy + (cy - sy) * gx, px = sx + gx * p + -gy * t0, py = sy + gy * p + gx * t0;
                t->DrawLine({X(px + gy * reach), Y(py - gx * reach)}, {X(px - gy * reach), Y(py + gx * reach)}, brush, float(SpaceSpec::line) * k);
            }
            brush->Release();
        }
        t->PopLayer(); Release(layer);
    }
    static ID2D1Geometry* RoundRect(ID2D1Factory* f, D2D1_RECT_F r, float radius) {
        ID2D1RoundedRectangleGeometry* g = nullptr; radius = std::max(0.f, std::min({radius, (r.right - r.left) / 2, (r.bottom - r.top) / 2}));
        f->CreateRoundedRectangleGeometry(D2D1::RoundedRect(r, radius, radius), &g); return g;
    }
    static void PaintSpace(Ink& ink, const FrameMark& m, const std::function<float(double)>& X, const std::function<float(double)>& Y, float k) {
        const D2D1_COLOR_F accent{((m.accent >> 16) & 255) / 255.f, ((m.accent >> 8) & 255) / 255.f, (m.accent & 255) / 255.f, 1};
        const D2D1_COLOR_F pink{((SpaceSpec::gap >> 16) & 255) / 255.f, ((SpaceSpec::gap >> 8) & 255) / 255.f, (SpaceSpec::gap & 255) / 255.f, 1};
        auto with = [](D2D1_COLOR_F c, float a) { c.a = a; return c; };
        auto* f = ink.Factory(); const double vw = m.vw > 0 ? m.vw : (m.page.right - m.page.left) / m.dpr, vh = m.vh > 0 ? m.vh : (m.page.bottom - m.page.top) / m.dpr;
        if (m.hasPad) {   // the ring: padding box (outer radius) minus the content box (inner radius), even-odd
            const auto* p = m.pad; ID2D1Geometry* parts[2] = {RoundRect(f, {X(p[0]), Y(p[1]), X(p[0] + p[2]), Y(p[1] + p[3])}, float(m.radius) * k), nullptr};
            UINT n = 1; if (p[6] > 0 && p[7] > 0) { parts[1] = RoundRect(f, {X(p[4]), Y(p[5]), X(p[4] + p[6]), Y(p[5] + p[7])}, float(m.innerRadius) * k); n = parts[1] ? 2 : 1; }
            ID2D1GeometryGroup* ring = nullptr;
            if (parts[0] && SUCCEEDED(f->CreateGeometryGroup(D2D1_FILL_MODE_ALTERNATE, parts, n, &ring)))
                PaintHatched(ink, ring, p[0], p[1], p[0] + p[2], p[1] + p[3], vw, vh, 135, X, Y, k, with(accent, SpaceSpec::padLine), with(accent, SpaceSpec::padFill));
            Release(ring); Release(parts[0]); Release(parts[1]);
        }
        if (!m.gaps.empty()) {   // the gaps: one union (non-zero)
            std::vector<ID2D1Geometry*> rects; double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;
            for (const auto& g : m.gaps) {
                if (auto* r = RoundRect(f, {X(g.x), Y(g.y), X(g.x + g.w), Y(g.y + g.h)}, 0)) rects.push_back(r);
                x0 = std::min(x0, g.x); y0 = std::min(y0, g.y); x1 = std::max(x1, g.x + g.w); y1 = std::max(y1, g.y + g.h);
            }
            ID2D1GeometryGroup* group = nullptr;
            if (!rects.empty() && SUCCEEDED(f->CreateGeometryGroup(D2D1_FILL_MODE_WINDING, rects.data(), UINT(rects.size()), &group)))
                PaintHatched(ink, group, x0, y0, x1, y1, vw, vh, 45, X, Y, k, with(pink, SpaceSpec::gapLine), with(pink, SpaceSpec::gapFill));
            Release(group); for (auto* r : rects) r->Release();
        }
    }
    // The value labels, above everything (the page placed them: whole, inward on thin sides, off the element label).
    static void PaintValueLabels(Ink& ink, const FrameMark& m, const std::function<float(double)>& X, const std::function<float(double)>& Y, float k) {
        const D2D1_COLOR_F accent{((m.accent >> 16) & 255) / 255.f, ((m.accent >> 8) & 255) / 255.f, (m.accent & 255) / 255.f, 1};
        for (const auto& l : m.labels) {
            const D2D1_RECT_F box{X(l.x), Y(l.y), X(l.x + l.w), Y(l.y + l.h)};
            ink.Fill(box, float(SpaceSpec::labelR) * k, l.kind == 0 ? accent : D2D1_COLOR_F{((SpaceSpec::gapLabel >> 16) & 255) / 255.f, ((SpaceSpec::gapLabel >> 8) & 255) / 255.f, (SpaceSpec::gapLabel & 255) / 255.f, 1});
            Font f = F(float(SpaceSpec::font) * k, float(SpaceSpec::labelH) * k, 400);   // D8: Inter Display, as the page
            ink.Text(l.text, f, box, {1, 1, 1, 1}, false, Align::Center, true);
        }
    }
    // Paints the mark into a surface whose top-left is `origin` (parent client px), at `dipScale` px per DIP.
    // Pixels taken from the screen, drawn 1:1 on the device pixels they came from (nearest neighbour: no resampling).
    static bool DrawPixels(Ink& ink, const FrameMark::Pixels& px, POINT origin, float dipScale) {
        if (!px.Ok()) return false;
        ID2D1Bitmap* bm = nullptr; auto* t = ink.Target();
        const UINT bw = UINT(px.at.right - px.at.left), bh = UINT(px.at.bottom - px.at.top);
        if (FAILED(t->CreateBitmap(D2D1::SizeU(bw, bh), px.bits.data(), bw * 4, D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)), &bm)) || !bm) return false;
        const D2D1_RECT_F d{float((px.at.left - origin.x) / dipScale), float((px.at.top - origin.y) / dipScale), float((px.at.right - origin.x) / dipScale), float((px.at.bottom - origin.y) / dipScale)};
        t->DrawBitmap(bm, d, 1, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR); bm->Release(); return true;
    }
    static void Paint(Ink& ink, const FrameMark& m, POINT origin, float dipScale) {
        if (DrawPixels(ink, m.whole, origin, dipScale)) return;   // final pass 1: the box exactly as it was on screen
        const float k = float(m.dpr / dipScale);   // DIP per CSS px
        auto X = [&](double css) { return float((m.page.left - origin.x + css * m.dpr) / dipScale); };
        auto Y = [&](double css) { return float((m.page.top - origin.y + css * m.dpr) / dipScale); };
        const D2D1_COLOR_F accent{((m.accent >> 16) & 255) / 255.f, ((m.accent >> 8) & 255) / 255.f, (m.accent & 255) / 255.f, 1};
        if (m.hasPad || !m.gaps.empty()) PaintSpace(ink, m, X, Y, k);   // the hatches: under the edges and the label, as in the page
        const double e = EdgeCss(m.dpr), w = std::max(m.w, 1.0), h = std::max(m.h, 1.0);   // R27: the page's border, whole device pixels
        ink.Fill({X(m.x), Y(m.y), X(m.x + w), Y(m.y + e)}, 0, accent);               // top
        ink.Fill({X(m.x), Y(m.y + h - e), X(m.x + w), Y(m.y + h)}, 0, accent);       // bottom
        ink.Fill({X(m.x), Y(m.y), X(m.x + e), Y(m.y + h)}, 0, accent);               // left
        ink.Fill({X(m.x + w - e), Y(m.y), X(m.x + w), Y(m.y + h)}, 0, accent);       // right
        auto tag = [&](double lx, double ly, double lw, double lh, const std::wstring& s, const FrameMark::Pixels& px) {   // R14: Paper's label (top corners 3, Inter 13/18)
            if (lw <= 0 || lh <= 0) return;
            if (DrawPixels(ink, px, origin, dipScale)) return;   // R45: the page's own pixels
            const D2D1_RECT_F box{X(lx), Y(ly), X(lx + lw), Y(ly + lh)}; const float r = float(FrameSpec::radius) * k;
            ink.Fill(box, r, accent); ink.Fill({box.left, std::max(box.top, box.bottom - r), box.right, box.bottom}, 0, accent);   // square bottom corners
            Font f = F(float(FrameSpec::font) * k, float(FrameSpec::line) * k, 400);
            const D2D1_RECT_F text{box.left + float(FrameSpec::padX) * k, box.top + float(FrameSpec::padY) * k, box.right - float(FrameSpec::padX) * k + .5f, box.top + float(FrameSpec::padY + FrameSpec::line) * k};
            ink.Target()->PushAxisAlignedClip(box, D2D1_ANTIALIAS_MODE_ALIASED);
            ink.Text(s, f, text, {1, 1, 1, FrameSpec::textAlpha});
            ink.Target()->PopAxisAlignedClip();
        };
        tag(m.lx, m.ly, m.lw, m.lh, m.text, m.leftPx);
        tag(m.rx, m.ry, m.rw, m.rh, m.rtext, m.rightPx);
        if (!m.labels.empty()) PaintValueLabels(ink, m, X, Y, k);   // V3-B: on top of the edges and the element label
    }
    // R45: the page's labels as they are on screen now (before the handoff hides them), in parent client px. Empty when
    // the parent is not on screen or the copy fails: the copy then draws the text itself.
    // The screen's pixels of `r` (parent client px). Empty on any failure.
    static void GrabScreen(HWND parent, RECT r, FrameMark::Pixels& out, int maxW, int maxH) {
            out = {}; if (!parent || IsRectEmpty(&r)) return;
            const int w = r.right - r.left, h = r.bottom - r.top; if (w > maxW || h > maxH) return;
            POINT o{r.left, r.top}; ClientToScreen(parent, &o);
            BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
            HDC screen = GetDC(nullptr); if (!screen) return;
            void* bits = nullptr; HDC mem = CreateCompatibleDC(screen); HBITMAP dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
            if (mem && dib && bits) {
                HGDIOBJ old = SelectObject(mem, dib);
                if (BitBlt(mem, 0, 0, w, h, screen, o.x, o.y, SRCCOPY | CAPTUREBLT)) {
                    GdiFlush(); out.at = r; out.bits.assign(static_cast<const std::uint32_t*>(bits), static_cast<const std::uint32_t*>(bits) + size_t(w) * h);
                    for (auto& p : out.bits) p |= 0xFF000000u;   // opaque labels
                }
                SelectObject(mem, old);
            }
            if (dib) DeleteObject(dib); if (mem) DeleteDC(mem); ReleaseDC(nullptr, screen);
    }
    static void GrabLabels(HWND parent, FrameMark& m) {
        auto grab = [&](double lx, double ly, double lw, double lh, FrameMark::Pixels& out) {
            out = {}; if (!parent || lw <= 0 || lh <= 0) return;
            RECT r{LONG(std::lround(m.page.left + lx * m.dpr)), LONG(std::lround(m.page.top + ly * m.dpr)), LONG(std::lround(m.page.left + (lx + lw) * m.dpr)), LONG(std::lround(m.page.top + (ly + lh) * m.dpr))};
            RECT in{}; if (!IntersectRect(&in, &r, &m.page) || !EqualRect(&in, &r)) return;   // whole labels only
            GrabScreen(parent, r, out, 2000, 200);
        };
        grab(m.lx, m.ly, m.lw, m.lh, m.leftPx);
        grab(m.rx, m.ry, m.rw, m.rh, m.rightPx);
    }
    // Draws the mark and returns once the compositor has shown it (or false: no window, nothing visible).
    bool Show(const FrameMark& shown) {
        FrameMark m = shown;
        // R45 / final pass 1: the whole box as the page shows it now (before the handoff hides its edges, labels, inspector):
        // the copy is those very pixels — no second rendering of text, edges or hatches. The labels alone, else the drawing.
        if (!m.whole.Ok()) GrabScreen(parent_, Bounds(m), m.whole, 8192, 8192);
        if (!m.whole.Ok() && !m.leftPx.Ok() && !m.rightPx.Ok()) GrabLabels(parent_, m);
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
