#pragma once
// Split view model (design/split-view.md, Paper "Split View — flow" 00-23).
// Pure C++17, no Win32: geometry in DIPs, decisions unit-tested in tests/split-view.
//
// Units. A site sees CSS px = DIP / page zoom (WebView2 ZoomFactor; the
// rasterization scale follows the monitor, so physical px = DIP * DPI / 96 never
// changes the responsive width). Minimums are therefore stated in CSS px and
// converted with the zoom before any DIP comparison: MinExtent().
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <vector>

namespace NativeChrome { namespace Split {

struct Box {
    float left = 0, top = 0, right = 0, bottom = 0;
    float W() const { return right - left; }
    float H() const { return bottom - top; }
    bool Contains(float x, float y) const { return x >= left && x < right && y >= top && y < bottom; }
};
enum class Orientation { Row, Column };  // Row: side by side (vertical separators). Column: stacked.

inline constexpr float gap = 4;            // Paper 08 (--spacing-browser-4): a thin neutral line at rest; the handle shows on hover (was 8, then 6)
inline constexpr float inset = 4;          // content inset, unchanged from the single page
inline constexpr float rail = 32;          // collapsed site, Design::Size::tabMin
inline constexpr float minWidthCss = 360;  // narrowest common phone width, above WCAG 1.4.10's 320
inline constexpr float minHeightCss = 256; // WCAG 1.4.10 reflow height
inline constexpr float hysteresis = 24;    // zone boundaries
inline constexpr float liftY = 84;         // pointer below the address row: lift the tab
inline constexpr float reattachY = 64;     // back above this: reorder again (20 DIP hysteresis)
inline constexpr float pageRadius = 6;     // outer bottom page corners (Paper 08: 6, concentric with the window's 10 over the 4 DIP inset)
inline constexpr float handleTarget = 16;  // separator hit target: the 4 DIP gap + 6 DIP over each page edge

inline float Extent(const Box& b, Orientation o) { return o == Orientation::Row ? b.W() : b.H(); }
inline float Start(const Box& b, Orientation o) { return o == Orientation::Row ? b.left : b.top; }
inline float Along(float x, float y, Orientation o) { return o == Orientation::Row ? x : y; }
// Minimum site extent in DIPs for the page zoom (1.25 at 125 %: 360 CSS px = 450 DIP).
inline float MinExtent(Orientation o, float zoom) {
    return (o == Orientation::Row ? minWidthCss : minHeightCss) * std::max(.25f, zoom);
}
inline bool Fits(float extent, float minExtent, int n) { return n >= 1 && n * minExtent + (n - 1) * gap <= extent + 1e-3f; }
// No fixed cap: as many sites as the space allows at the minimum.
inline int Capacity(float extent, float minExtent) {
    return std::max(1, (int)std::floor((extent + gap) / (minExtent + gap) + 1e-4f));
}
// Content rectangle below the toolbar, same as the single page.
inline Box Content(float width, float height) { return {inset, 84, width - inset, std::max(84.f, height - inset)}; }

struct Slot { int member = 0; bool rail = false; Box box; };
struct Arrangement {
    Orientation orientation = Orientation::Row;
    Box content;
    std::vector<Slot> slots;  // panel order (= tab order)
    int visible = 0;
    // Separator s sits between slots[s] and slots[s + 1] when both are visible.
    std::vector<int> separators;
    Box SeparatorBox(int s) const {
        const Box& a = slots[s].box; const Box& b = slots[s + 1].box;
        return orientation == Orientation::Row ? Box{a.right, content.top, b.left, content.bottom}
                                               : Box{content.left, a.bottom, content.right, b.top};
    }
    // Hit area: the visible gap widened to the target size, inside the content.
    Box SeparatorHit(int s) const {
        Box g = SeparatorBox(s); const float grow = std::max(0.f, (handleTarget - Extent(g, orientation)) / 2);
        if (orientation == Orientation::Row) { g.left -= grow; g.right += grow; } else { g.top -= grow; g.bottom += grow; }
        return g;
    }
    int SlotOf(int member) const { for (int i = 0; i < (int)slots.size(); ++i) if (slots[i].member == member) return i; return -1; }
};

// Split `avail` between visible sites by weight, none below `minExtent`.
inline std::vector<float> Distribute(float avail, const std::vector<float>& weights, float minExtent) {
    const int n = (int)weights.size(); std::vector<float> out(n, 0); std::vector<bool> fixed(n, false);
    if (n == 0) return out;
    if (avail < n * minExtent) { for (auto& e : out) e = avail / n; return out; }
    for (int pass = 0; pass <= n; ++pass) {
        float room = avail, total = 0;
        for (int i = 0; i < n; ++i) { if (fixed[i]) room -= minExtent; else total += std::max(1e-3f, weights[i]); }
        bool changed = false;
        for (int i = 0; i < n; ++i) if (!fixed[i]) {
            out[i] = room * std::max(1e-3f, weights[i]) / total;
            if (out[i] < minExtent - 1e-3f) { fixed[i] = true; out[i] = minExtent; changed = true; }
        }
        if (!changed) break;
    }
    return out;
}

// Lay out a group. The active site is always visible; when the space runs out, the
// least recently used sites collapse into rails (nothing is closed).
inline Arrangement Arrange(Box content, Orientation o, const std::vector<float>& weights,
                           const std::vector<std::uint64_t>& recency, int active, float minExtent) {
    Arrangement a; a.orientation = o; a.content = content;
    const int n = (int)weights.size(); if (n == 0) return a;
    active = std::clamp(active, 0, n - 1);
    const float extent = Extent(content, o);
    int k = n;
    while (k > 1 && k * minExtent + (n - k) * rail + (n - 1) * gap > extent + 1e-3f) --k;
    std::vector<int> order(n); std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](int x, int y) {
        if (x == active || y == active) return x == active && y != active;
        const auto rx = x < (int)recency.size() ? recency[x] : 0, ry = y < (int)recency.size() ? recency[y] : 0;
        return rx > ry;
    });
    std::vector<bool> shown(n, false); for (int i = 0; i < k; ++i) shown[order[i]] = true;
    std::vector<float> vw; for (int i = 0; i < n; ++i) if (shown[i]) vw.push_back(weights[i]);
    const float avail = extent - (n - k) * rail - (n - 1) * gap;
    auto sizes = Distribute(avail, vw, minExtent);
    float pos = Start(content, o), acc = 0; int vi = 0;
    for (int i = 0; i < n; ++i) {
        Slot s; s.member = i; s.rail = !shown[i];
        float size = s.rail ? rail : sizes[vi++];
        // Whole DIPs, the remainder accumulated so the last edge lands exactly on the content edge.
        acc += size; float end = (i == n - 1) ? Start(content, o) + extent : std::round(Start(content, o) + acc + i * gap);
        if (o == Orientation::Row) s.box = {pos, content.top, end, content.bottom};
        else s.box = {content.left, pos, content.right, end};
        pos = end + gap; a.slots.push_back(s);
    }
    a.visible = k;
    for (int i = 0; i + 1 < n; ++i) if (!a.slots[i].rail && !a.slots[i + 1].rail) a.separators.push_back(i);
    return a;
}

// New site gets 1/(n+1) of the space; the others keep their proportions.
inline std::vector<float> InsertWeights(std::vector<float> w, int index) {
    const int n = (int)w.size(); float total = 0; for (float x : w) total += x;
    const float add = n ? total / n : 1;
    index = std::clamp(index, 0, n); w.insert(w.begin() + index, add); return w;
}
inline float Rubber(float over, float dimension, float c = .55f) {
    return over * dimension * c / (dimension + c * std::fabs(over));
}
// Apple's momentum projection (decelerationRate .998): where a release is heading.
inline float Project(float velocityPerSecond, float rate = .998f) { return velocityPerSecond / 1000 * rate / (1 - rate); }

// Separator s (between slots s and s+1) dragged by `delta` DIPs from the extents at grab
// time: only its two neighbours move; past a minimum the edge resists (rubber band).
inline std::vector<float> Resize(const Arrangement& a, const std::vector<float>& grabExtents, int s, float delta,
                                 float minExtent, bool rubber = true) {
    std::vector<float> e = grabExtents;
    if (s < 0 || s + 1 >= (int)e.size()) return e;
    const float total = e[s] + e[s + 1];
    float first = e[s] + delta;
    if (first < minExtent) first = minExtent - (rubber ? Rubber(minExtent - first, total) : 0);
    if (first > total - minExtent) first = total - minExtent + (rubber ? Rubber(first - (total - minExtent), total) : 0);
    e[s] = first; e[s + 1] = total - first; (void)a; return e;
}
inline std::vector<float> Extents(const Arrangement& a) {
    std::vector<float> e; for (auto& s : a.slots) e.push_back(Extent(s.box, a.orientation)); return e;
}
// Release: two sites snap to 1/3, 1/2 or 2/3 of their shared room, chosen on the
// projected position; more sites settle where released (clamped to the minimums).
inline float SnapFirst(float first, float total, float velocity, float minExtent, int visible) {
    if (visible == 2) {
        const float projected = first + Project(velocity);
        float best = total / 2, d = 1e9f;
        for (float f : {1.f / 3, .5f, 2.f / 3}) {
            const float c = std::clamp(total * f, minExtent, total - minExtent);
            if (std::fabs(c - projected) < d) { d = std::fabs(c - projected); best = c; }
        }
        return best;
    }
    return std::clamp(first, minExtent, total - minExtent);
}

// ---- Drop targets -------------------------------------------------------------
enum class Side { Left, Right, Top, Bottom };
enum class Kind { None, Split, Insert, Replace, Swap, Move, Refuse };
struct Target {
    Kind kind = Kind::None;
    Side side = Side::Right;
    Orientation orientation = Orientation::Row;
    int index = -1;   // Insert/Move: insertion index (member order); Replace/Swap: member
    int needed = 0;   // Refuse: how many sites the drop would need
    Box preview;      // exact future rectangle of the dropped site
    bool operator==(const Target& o) const { return kind == o.kind && side == o.side && index == o.index && orientation == o.orientation; }
};
inline Orientation OrientationOf(Side s) { return s == Side::Left || s == Side::Right ? Orientation::Row : Orientation::Column; }

// Single page: the diagonals of the content split it into four triangles, each
// aiming at its edge. A previous side is kept until the pointer is 24 DIP past
// the diagonal.
inline Side NearestSide(const Box& c, float x, float y, const Target* previous = nullptr) {
    const float w = std::max(1.f, c.W()), h = std::max(1.f, c.H());
    const float u = (x - c.left) / w, v = (y - c.top) / h;
    const float d[4] = {u, 1 - u, v, 1 - v};  // Left, Right, Top, Bottom (normalised)
    int best = 0; for (int i = 1; i < 4; ++i) if (d[i] < d[best]) best = i;
    if (previous && previous->kind != Kind::None) {
        const int p = (int)previous->side;
        if (p != best && (d[p] - d[best]) * std::min(w, h) < hysteresis) best = p;
    }
    return (Side)best;
}
inline Target SingleTarget(const Box& c, float x, float y, float minW, float minH, const Target* previous = nullptr) {
    Target t; t.side = NearestSide(c, x, y, previous); t.orientation = OrientationOf(t.side);
    const float min = t.orientation == Orientation::Row ? minW : minH;
    if (!Fits(Extent(c, t.orientation), min, 2)) { t.kind = Kind::Refuse; t.needed = 2; return t; }
    t.kind = Kind::Split;
    const float half = (Extent(c, t.orientation) - gap) / 2;
    switch (t.side) {
    case Side::Left: t.preview = {c.left, c.top, std::round(c.left + half), c.bottom}; break;
    case Side::Right: t.preview = {std::round(c.right - half), c.top, c.right, c.bottom}; break;
    case Side::Top: t.preview = {c.left, c.top, c.right, std::round(c.top + half)}; break;
    case Side::Bottom: t.preview = {c.left, std::round(c.bottom - half), c.right, c.bottom}; break;
    }
    return t;
}
// Existing group: a site's middle half replaces it, its outer quarters insert at that
// boundary. `dragged` is the dragged tab's member index, or -1 for an outside tab.
inline Target GroupTarget(const Arrangement& a, const std::vector<float>& weights, int dragged, float x, float y,
                          float minExtent, const Target* previous = nullptr) {
    Target t; t.orientation = a.orientation;
    const int n = (int)a.slots.size(); if (!n || !a.content.Contains(x, y)) return t;
    const float p = Along(x, y, a.orientation);
    int slot = -1;
    for (int i = 0; i < n; ++i) { if (a.slots[i].rail) continue;
        const Box& b = a.slots[i].box; const float s = Start(b, a.orientation), e = s + Extent(b, a.orientation);
        if (p >= s - gap && p < e + gap) { slot = i; break; } }
    if (slot < 0) return t;  // over a rail: nothing to do there
    const Box& b = a.slots[slot].box; const float s = Start(b, a.orientation), ext = Extent(b, a.orientation);
    float q1 = s + ext * .25f, q3 = s + ext * .75f;
    if (previous && previous->kind != Kind::None) {  // keep the previous zone within 24 DIP of its boundary
        const bool prevBefore = (previous->kind == Kind::Insert || previous->kind == Kind::Move) && previous->index == slot;
        const bool prevAfter = (previous->kind == Kind::Insert || previous->kind == Kind::Move) && previous->index == slot + 1;
        const bool prevMiddle = (previous->kind == Kind::Replace || previous->kind == Kind::Swap) && previous->index == slot;
        if (prevBefore) q1 += hysteresis;
        if (prevAfter) q3 -= hysteresis;
        if (prevMiddle) { q1 -= hysteresis; q3 += hysteresis; }
    }
    const int member = a.slots[slot].member;
    int insertAt = p < q1 ? member : p >= q3 ? member + 1 : -1;
    if (dragged >= 0) {  // a member of this group: swap or move inside the group
        if (insertAt < 0) { if (member == dragged) return t; t.kind = Kind::Swap; t.index = member; t.preview = b; return t; }
        if (insertAt == dragged || insertAt == dragged + 1) return t;
        t.kind = Kind::Move; t.index = insertAt;
        std::vector<float> w = weights; const float mine = w[dragged]; w.erase(w.begin() + dragged);
        const int at = insertAt > dragged ? insertAt - 1 : insertAt; w.insert(w.begin() + at, mine);
        std::vector<std::uint64_t> rec(n, 1); auto moved = Arrange(a.content, a.orientation, w, rec, at, minExtent);
        t.preview = moved.slots[at].box; return t;
    }
    if (insertAt < 0) { t.kind = Kind::Replace; t.index = member; t.preview = b; return t; }
    if (!Fits(Extent(a.content, a.orientation), minExtent, n + 1)) { t.kind = Kind::Refuse; t.needed = n + 1; t.index = insertAt; return t; }
    t.kind = Kind::Insert; t.index = insertAt;
    auto w = InsertWeights(weights, insertAt); std::vector<std::uint64_t> rec(n + 1, 1);
    auto grown = Arrange(a.content, a.orientation, w, rec, insertAt, minExtent);
    t.preview = grown.slots[insertAt].box; return t;
}

// ---- Motion -----------------------------------------------------------------------
// Critically damped spring (Apple damping 1.0) that keeps its velocity when the
// target changes, so a re-targeted preview never jumps or brakes.
struct Spring {
    float x = 0, v = 0, target = 0; bool ready = false;
    void Snap(float t) { x = target = t; v = 0; ready = true; }
    void Set(float t) { if (!ready) Snap(t); else target = t; }
    void Step(float dt, float response) {
        if (!ready || dt <= 0) return;
        const float w = 6.2831853f / std::max(.05f, response), e0 = x - target, c = v + w * e0, ex = std::exp(-w * dt);
        x = target + (e0 + c * dt) * ex; v = (v - w * c * dt) * ex;
        if (Settled()) { x = target; v = 0; }
    }
    bool Settled() const { return std::fabs(x - target) < .25f && std::fabs(v) < 4; }
};
struct BoxSpring {
    Spring l, t, r, b;
    void Snap(const Box& x) { l.Snap(x.left); t.Snap(x.top); r.Snap(x.right); b.Snap(x.bottom); }
    void Set(const Box& x) { l.Set(x.left); t.Set(x.top); r.Set(x.right); b.Set(x.bottom); }
    void Step(float dt, float response) { l.Step(dt, response); t.Step(dt, response); r.Step(dt, response); b.Step(dt, response); }
    bool Settled() const { return l.Settled() && t.Settled() && r.Settled() && b.Settled(); }
    Box Value() const { return {l.x, t.x, r.x, b.x}; }
};
}}
