#pragma once
// Mode Focus (Paper « Mode Focus », page p-U-0; design/focus-mode-plan.md). No chrome around the page: the page
// takes the window minus a 4 DIP inset on all four sides, with 6 DIP corners. The tab row and the toolbar come back
// in a floating panel that slides down over the top of the page when the pointer reaches the window's top edge.
// Pure C++17 (no Win32): the Paper values, the panel's geometry for a slide position, the hit box, the show / keep /
// hide decision with its timestamps, the easing and the panel's pixel spans (the hole made in the page windows).
// Unit-tested in tests/focus_mode_test.cpp; bound to the window in src/main.cpp (section « Mode Focus »).
#include <algorithm>
#include <cmath>
#include <vector>

namespace NativeChrome { namespace FocusMode {

// ---- Paper values (DIP) -----------------------------------------------------------------------------------
// « Mode Focus – Activé » (1J9P-0): « Zone de page » padding 4 on all sides, the page's four corners 6.
inline constexpr float pageInset = 4, pageRadius = 6;
// « Hover en haut (Hitbox) » (1J9W-0): client (0, 0), the client's width, 14 high.
inline constexpr float hitHeight = 14;
// « Hover en haut » (1JBQ-0): the panel. top 4, x 4 .. W-4, #171717, 1 px #FFFFFF14 border, radius 4, padding
// 2 / 6 (rows bring their own 6 DIP sides), column gap 6: the 36 DIP tab row then the 32 DIP toolbar. No shadow.
inline constexpr float panelInset = 4, panelRadius = 4, border = 1, padTop = 2, padBottom = 6;
inline constexpr float tabRow = 36, rowGap = 6, toolbarRow = 32;
inline constexpr float bandExtra = 40;                       // the bookmarks band inside the panel (decision 3)
inline constexpr float borderAlpha = 20 / 255.f;             // #FFFFFF14
inline constexpr float PanelHeight(bool band) { return border + padTop + tabRow + rowGap + toolbarRow + padBottom + border + (band ? bandExtra : 0); }
static_assert(PanelHeight(false) == 84 && PanelHeight(true) == 124, "Paper 1JBQ-0: the panel is 84 DIP high, 124 with the bookmarks band");
// The chrome lays out in its own space, the normal chrome's: chrome-local (0, 0) is the panel's inner top-left
// (panel x + border, panel top + border; the panel's 2 DIP padding-top is the normal chrome's own row offset),
// and its layout width is the client's minus 2 x (inset + border).
inline constexpr float layoutInset = 2 * (panelInset + border);   // 10: 1168 for a 1178 client
// `banner`: the annotation banner's height above the panel (0 without it; see « With the annotation mode » below).
inline constexpr float RestBottom(bool band, float banner = 0) { return banner + panelInset + PanelHeight(band); }   // 88 / 128
// Not in Paper (flagged in design/focus-mode-report.md): a drawer opened from the panel starts 4 DIP below it.
inline constexpr float drawerGap = 4;
inline constexpr float DrawerTop(bool band, float banner = 0) { return RestBottom(band, banner) + drawerGap; }     // 92 / 132
// Their bottom stops 4 DIP above the window's (Paper « Annotation + Focus · barre du haut · tiroir ouvert » 21OK-0: 612 high
// from 124 in a 740 window), square there; their right edge on the panel's, W - 4.
inline constexpr float drawerBottom = pageInset;
static_assert(DrawerTop(false) == 92 && DrawerTop(true) == 132, "drawers open 4 DIP under the panel");

// ---- With the annotation mode (owner's rule 2026-11-24; Paper page « #127 — Panneau latéral »: « Annotation + Focus ·
// barre du haut » 203J-0 at rest, « … · panneau révélé » 20MM-0) ---------------------------------------------------
// Focus hides the tabs and the toolbar, never the annotation banner nor the chat (mode Panneau): both stay visible and
// usable. The banner sits at the very top: y 0 .. its height (32, animated while it slides in), x 4 .. W - 4. The chat is
// a card 4 DIP from the window's right and bottom edges, from the banner's bottom down, radius 6, a 1 DIP #262626 border.
// The page keeps the 4 DIP inset and its 6 DIP corners under the banner and stops 4 DIP before the card. The panel floats
// under the banner (top banner + 4) over the page's width only, and its hover strip is the 14 DIP right under the
// banner: the banner and the chat stay whole while it shows.
inline constexpr float bannerInset = pageInset;                       // 205D-0: x 4, width W - 8
inline constexpr float chatInset = 4, chatGap = 4, chatRadius = 6, chatBorder = 1;
inline constexpr unsigned chatBorderRgb = 0x262626;
// Where the page starts: under the banner, or the 4 DIP inset.
inline constexpr float PageTop(float banner) { return banner > pageInset ? banner : pageInset; }
// What a chat card `chatWidth` wide takes from the page's right (the card, its 4 DIP gap; the inset is the page's own).
inline constexpr float ChatReserve(float chatWidth) { return chatWidth > 0 ? chatWidth + chatGap + chatInset - pageInset : 0; }
// The panel's surroundings: the banner above it and the strip kept on its right (ChatReserve), 0 / 0 without them.
struct Above { float banner = 0, reserve = 0; };
// A drawer opened while the chat card is shown (provisional, Designer 2026-11-24, as #127's DrawerOverChat): its right
// edge on the window's (W - 4), over the chat (back when the drawer closes), at its full width. false: on the panel's.
inline constexpr bool drawerOverChat = true;

// ---- Motion -------------------------------------------------------------------------------------------------
// Show 200 ms, hide 160 ms, both with the strong ease-out cubic-bezier(0.23, 1, 0.32, 1) (emil-design-eng: entering
// and exiting UI eases out, under 300 ms). The hide waits 300 ms with nothing holding the bar (a pointer slightly too
// low does not drop it). The pointer is polled every ~30 ms: over a page the window gets no mouse messages.
inline constexpr double showMs = 200, hideMs = 160, graceMs = 300, pollMs = 30;
inline constexpr double easeX1 = .23, easeY1 = 1, easeX2 = .32, easeY2 = 1;
// CSS cubic-bezier(x1, y1, x2, y2) at progress x in [0, 1]: x(t) solved by Newton steps with a bisection fallback.
inline double CubicBezier(double x, double x1, double y1, double x2, double y2) {
    if (x <= 0) return 0;
    if (x >= 1) return 1;
    auto bx = [&](double t) { const double u = 1 - t; return 3 * u * u * t * x1 + 3 * u * t * t * x2 + t * t * t; };
    auto by = [&](double t) { const double u = 1 - t; return 3 * u * u * t * y1 + 3 * u * t * t * y2 + t * t * t; };
    auto dx = [&](double t) { const double u = 1 - t; return 3 * u * u * x1 + 6 * u * t * (x2 - x1) + 3 * t * t * (1 - x2); };
    double t = x;
    for (int i = 0; i < 8; ++i) { const double e = bx(t) - x; if (std::fabs(e) < 1e-7) return by(t); const double d = dx(t); if (std::fabs(d) < 1e-6) break; t -= e / d; }
    double lo = 0, hi = 1; t = x;
    for (int i = 0; i < 60; ++i) { const double v = bx(t); if (std::fabs(v - x) < 1e-7) break; if (v < x) lo = t; else hi = t; t = (lo + hi) / 2; }
    return by(t);
}
inline double EaseOut(double x) { return CubicBezier(x, easeX1, easeY1, easeX2, easeY2); }

// ---- Geometry (client DIP) ---------------------------------------------------------------------------------
struct Rect {
    float left = 0, top = 0, right = 0, bottom = 0;
    bool Empty() const { return right <= left || bottom <= top; }
    bool Contains(float x, float y) const { return x >= left && x < right && y >= top && y < bottom; }
};
// The panel at slide position `position` (0 = hidden: its bottom on the window's top edge, or the banner's, 1 = at
// rest, top 4 under it). With the chat card it spans the page's width only.
inline Rect Panel(float clientWidth, bool band, float position, Above above = {}) {
    const float h = PanelHeight(band), top = above.banner - h + (h + panelInset) * std::clamp(position, 0.f, 1.f);
    return {panelInset, top, std::max(panelInset, clientWidth - panelInset - above.reserve), top + h};
}
// Where chrome-local (0, 0) lands for that panel.
struct Point { float x = 0, y = 0; };
inline Point Origin(const Rect& panel) { return {panel.left + border, panel.top + border}; }
// The part of the panel inside the window (nothing above the client's top edge).
inline Rect Visible(const Rect& panel) { Rect r = panel; r.top = std::max(r.top, 0.f); return r.Empty() ? Rect{} : r; }
// The hit box: the client's whole width, its top 14 DIP (y 13.9 is in, 14 is out). With the annotation banner: the
// 14 DIP right under it (32 .. 46), left of the chat card's strip.
inline bool InHitBox(float x, float y, float clientWidth, Above above = {}) {
    return x >= 0 && x < clientWidth - above.reserve && y >= above.banner && y < above.banner + hitHeight;
}
// The page box in focus mode (the chat card, when one is shown, keeps its strip on the right: ChatReserve), under the
// annotation banner when it is there.
inline Rect Content(float clientWidth, float clientHeight, float rightReserve = 0, float banner = 0) {
    Rect c{pageInset, PageTop(banner), std::max(pageInset, clientWidth - pageInset - rightReserve), std::max(PageTop(banner), clientHeight - pageInset)};
    return c;
}
// The chat card (client DIP), `chatWidth` wide, at rest.
inline Rect ChatCard(float clientWidth, float clientHeight, float chatWidth, float banner = 0) {
    return {clientWidth - chatInset - chatWidth, PageTop(banner), clientWidth - chatInset, std::max(PageTop(banner), clientHeight - chatInset)};
}
// The annotation banner (client DIP) at its height `banner`.
inline Rect Banner(float clientWidth, float banner) { return {bannerInset, 0, std::max(bannerInset, clientWidth - bannerInset), banner}; }
// A DIP value on whole device pixels (the panel's edges match the page windows', placed with the same rounding).
inline float Snap(float dip, float scale) { return scale > 0 ? std::round(dip * scale) / scale : dip; }

// ---- The panel's bottom corners (device pixels) ---------------------------------------------------------------
// Corner coordinates: column i from the panel's left edge (mirrored on the right), row j from its bottom edge upward,
// the arc's centre at (r, r). A pixel square [i, i+1] x [j, j+1] is fully inside the rounded shape when its point
// nearest the corner, (i, j), is inside the arc (or the square is out of the arc's zone).
inline bool FullyInside(int i, int j, float r) { return i >= r || j >= r || std::hypot(r - i, r - j) <= r; }
// A corner pixel as the panel colours it: coverage by the panel's shape (8 x 8 samples) and the panel's grey there
// (its 1 px border along the edge and the arc, its fill inside). Premultiplied by the host: grey x alpha.
struct CornerPixel { float alpha = 0, grey = 0; };
inline int CornerSize(float r) { return r > 0 ? static_cast<int>(std::ceil(r)) : 0; }
// Row-major, row 0 = the panel's bottom pixel row, column 0 = its outer column.
inline std::vector<CornerPixel> BottomCorner(float r, float borderPx, float fill, float border) {
    const int n = CornerSize(r); std::vector<CornerPixel> out(static_cast<size_t>(n) * n);
    constexpr int k = 8; const float inner = std::max(0.f, r - borderPx);
    for (int j = 0; j < n; ++j) for (int i = 0; i < n; ++i) {
        int nf = 0, nb = 0;
        for (int b = 0; b < k; ++b) for (int a = 0; a < k; ++a) {
            const float x = i + (a + .5f) / k, y = j + (b + .5f) / k;
            const bool arc = x < r && y < r; const float d = arc ? std::hypot(r - x, r - y) : 0.f;
            if (arc && d > r) continue;                                       // outside the panel: the page
            const bool fillZone = x >= borderPx && y >= borderPx && (!arc || d <= inner);
            if (fillZone) ++nf; else ++nb;
        }
        auto& p = out[static_cast<size_t>(j) * n + i]; const int in = nf + nb;
        p.alpha = in / float(k * k); p.grey = in ? (nf * fill + nb * border) / in : 0.f;
    }
    return out;
}

// ---- The hole (device pixels) --------------------------------------------------------------------------------
// While the panel is at rest every page window under it gets a window region without the panel's pixels, so the
// main window shows (and receives the pointer) there. The hole takes only the pixels the panel covers completely:
// along its two bottom arcs the partly covered pixels stay the page's, and a tiny per-pixel-alpha window per corner
// lays the panel's colour back over them at its coverage (antialiased over any site, no dark notch). The top
// corners stay square (they sit over the page's own rounded corners, on the canvas).
struct Pixels {
    int left = 0, top = 0, right = 0, bottom = 0;
    bool Empty() const { return right <= left || bottom <= top; }
    bool operator==(const Pixels& o) const { return left == o.left && top == o.top && right == o.right && bottom == o.bottom; }
};
inline std::vector<Pixels> HoleSpans(Pixels panel, float radiusPx) {
    std::vector<Pixels> out;if (panel.Empty()) return out;
    const float r = std::max(0.f, std::min(radiusPx, std::min((panel.right - panel.left) / 2.f, (panel.bottom - panel.top) / 2.f)));
    auto push = [&](int l, int t, int rr, int b) {
        if (rr <= l || b <= t) return;
        if (!out.empty() && out.back().left == l && out.back().right == rr && out.back().bottom == t) { out.back().bottom = b; return; }
        out.push_back({l, t, rr, b});
    };
    const int n = CornerSize(r), arcTop = std::max(panel.top, panel.bottom - n);
    push(panel.left, panel.top, panel.right, arcTop);
    for (int y = arcTop; y < panel.bottom; ++y) {
        const int j = panel.bottom - 1 - y; int cut = 0; while (cut < n && !FullyInside(cut, j, r)) ++cut;
        push(panel.left + cut, y, panel.right - cut, y + 1);
    }
    return out;
}
// Spans limited to one page window's rectangle and expressed in that window's own coordinates.
inline std::vector<Pixels> ClipTo(const std::vector<Pixels>& spans, Pixels page) {
    std::vector<Pixels> out;
    for (const auto& s : spans) {
        Pixels c{std::max(s.left, page.left), std::max(s.top, page.top), std::min(s.right, page.right), std::min(s.bottom, page.bottom)};
        if (!c.Empty()) out.push_back({c.left - page.left, c.top - page.top, c.right - page.left, c.bottom - page.top});
    }
    return out;
}

// ---- Show / keep / hide -------------------------------------------------------------------------------------
// The slide position over time: reversible mid-flight from where it is (no jump). A move covers its distance in
// the full duration's share of it (half the way back takes half the time). Milliseconds on any monotonic clock.
class Slide {
    double from_ = 0, to_ = 0, start_ = 0, duration_ = 0;
public:
    double At(double now) const {
        if (duration_ <= 0 || now >= start_ + duration_) return to_;
        if (now <= start_) return from_;
        return from_ + (to_ - from_) * EaseOut((now - start_) / duration_);
    }
    double Target() const { return to_; }
    bool Moving(double now) const { return duration_ > 0 && now < start_ + duration_ && from_ != to_; }
    double End() const { return start_ + duration_; }
    void Snap(double value, double now) { from_ = to_ = value; start_ = now; duration_ = 0; }
    void Go(double target, double now, double fullMs) {
        const double at = At(now);
        if (target == to_ && (Moving(now) || at == target)) return;
        from_ = at; to_ = target; start_ = now; duration_ = fullMs * std::fabs(target - at);
        if (duration_ <= 0) Snap(target, now);
    }
};
// What one evaluation (a poll tick, a mouse move, a key) sees.
struct Input {
    bool reveal = false;   // the pointer asks for the bar: in the hit box, the window in front and not minimised, no button held
    bool pointer = false;  // the pointer is in the hit box or over the panel's visible part
    bool hold = false;     // the chrome is in use from the keyboard or holds an open surface (editing, focus, a toolbar
                           // surface): shows the bar at once and keeps it
    bool keep = false;     // keeps a shown bar without revealing a hidden one (a menu, a drag, the mouse captured)
};
class Bar {
    Slide slide_; bool target_ = false; double keptAt_ = -1e18;
public:
    // Before a slide-out moves, the host swaps the panel for its sliding picture and gives the page windows their whole
    // surface back; the page's region change reaches the screen a frame after the window's own paint, so the slide
    // starts this long after the decision (2-3 frames, invisible: the picture is the panel at rest).
    double hideLeadMs = 0;
    bool Shown() const { return target_; }                    // where the bar is going
    double Position(double now) const { return slide_.At(now); }   // 0 hidden .. 1 at rest
    bool Visible(double now) const { return Position(now) > 0; }
    bool Moving(double now) const { return slide_.Moving(now); }
    double SlideEnd() const { return slide_.End(); }
    // When an unheld shown bar starts to hide (the grace's end); meaningful while Shown().
    double HideAt() const { return keptAt_ + graceMs; }
    void Reset(bool shown, double now) { target_ = shown; slide_.Snap(shown ? 1 : 0, now); keptAt_ = now; }
    // Returns true when the bar's target or position changed.
    bool Update(double now, const Input& in, bool reduced) {
        const bool kept = in.pointer || in.hold || in.keep;
        if (kept) keptAt_ = now;
        if (in.hold) {   // keyboard / surface: at once, never animated (and the address field sits where it will stay)
            const bool changed = !target_ || Position(now) != 1 || Moving(now);
            target_ = true; if (changed) slide_.Snap(1, now); return changed;
        }
        if (!target_ && in.reveal) { target_ = true; slide_.Go(1, now, reduced ? 0 : showMs); return true; }
        if (target_ && !kept && now - keptAt_ >= graceMs) { target_ = false; slide_.Go(0, reduced ? now : now + hideLeadMs, reduced ? 0 : hideMs); return true; }
        return false;
    }
};

}}
