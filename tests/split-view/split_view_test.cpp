// Pure split-view model: capacity, minimums, zones, insertion, refusal, rails,
// resize bounds and springs (design/split-view.md, Paper boards 05-23).
#include "../../src/split-view.h"
#include <cassert>
#include <cstdio>
#include <cmath>
using namespace NativeChrome::Split;

static bool Near(float a, float b, float e = .51f) { return std::fabs(a - b) <= e; }

int main() {
    // ---- units: 360 CSS px wide / 256 CSS px high, scaled by the page zoom only
    assert(MinExtent(Orientation::Row, 1) == 360 && MinExtent(Orientation::Column, 1) == 256);
    assert(MinExtent(Orientation::Row, 1.25f) == 450 && MinExtent(Orientation::Column, .5f) == 128);

    // ---- capacity: no fixed cap; floor((content + 4) / (min + 4)), content = window - 8, gap 4 (Paper 08)
    const int widths[] = {732, 1180, 1460, 1920, 2560, 3440, 5120}, expected[] = {2, 3, 4, 5, 7, 9, 14};
    for (int i = 0; i < 7; ++i) assert(Capacity(widths[i] - 8.f, 360) == expected[i]);
    assert(Capacity(731 - 8.f, 360) == 1);              // below 732 DIP: two sites no longer fit
    assert(Capacity(1459 - 8.f, 360) == 3);             // a fourth needs 1 460 DIP
    assert(Capacity(2560 - 8.f, 450) == 5);             // zoom 125 %: wider minimum, fewer sites
    assert(Capacity(815 - 89.f, 256) == 2 && Capacity(1080 - 89.f, 256) == 3);  // stacked heights
    assert(Fits(1172, 360, 3) && !Fits(1172, 360, 4));

    // ---- arrangement: equal thirds at 1180, exact edges, 4 DIP gaps, all visible
    const Box c = Content(1180, 815);
    assert(c.left == 4 && c.top == 84 && c.right == 1176 && c.bottom == 811);
    auto a = Arrange(c, Orientation::Row, {1, 1, 1}, {3, 2, 1}, 0, 360);
    assert(a.visible == 3 && a.slots.size() == 3 && a.separators.size() == 2);
    assert(a.slots[0].box.left == 4 && a.slots[2].box.right == 1176);
    for (int i = 0; i < 3; ++i) assert(!a.slots[i].rail && Near(a.slots[i].box.W(), 388, 1));
    assert(Near(a.slots[1].box.left - a.slots[0].box.right, 4, .01f));
    // proportions follow weights, no site under the minimum
    auto skew = Arrange(c, Orientation::Row, {9, 1}, {1, 1}, 0, 360);
    assert(Near(skew.slots[1].box.W(), 360) && Near(skew.slots[0].box.W(), 1172 - 4 - 360));

    // ---- window shrinks: least recent collapses to a 32 DIP rail, active never, nothing dropped
    auto shrink = Arrange(c, Orientation::Row, {1, 1, 1, 1}, {4, 3, 9, 1}, 2, 360);
    assert(shrink.slots.size() == 4 && shrink.visible == 3);
    assert(shrink.slots[3].rail && Near(shrink.slots[3].box.W(), 32, 1) && !shrink.slots[2].rail);
    for (int i = 0; i < 3; ++i) assert(shrink.slots[i].box.W() >= 360 - .5f);
    assert(shrink.separators.size() == 2);  // no separator next to the rail
    auto tiny = Arrange(Content(600, 815), Orientation::Row, {1, 1, 1}, {1, 2, 3}, 0, 360);
    assert(tiny.visible == 1 && !tiny.slots[0].rail && tiny.slots[1].rail && tiny.slots[2].rail);
    // stacked: rails are horizontal
    auto low = Arrange(Content(1180, 520), Orientation::Column, {1, 1}, {1, 2}, 1, 256);
    assert(low.visible == 1 && low.slots[0].rail && Near(low.slots[0].box.H(), 32, 1));

    // ---- single page zones: the diagonals pick the nearest edge
    Target t = SingleTarget(c, 1100, 450, 360, 256);
    assert(t.kind == Kind::Split && t.side == Side::Right && t.orientation == Orientation::Row);
    assert(Near(t.preview.left, 592) && t.preview.right == 1176);
    t = SingleTarget(c, 590, 780, 360, 256);
    assert(t.side == Side::Bottom && t.orientation == Orientation::Column && Near(t.preview.top, 449.5f));
    t = SingleTarget(c, 100, 450, 360, 256); assert(t.side == Side::Left && Near(t.preview.right, 588));
    t = SingleTarget(c, 590, 120, 360, 256); assert(t.side == Side::Top);
    // hysteresis: just past the diagonal, the previous side holds; 24 DIP past it, it flips
    Target right = SingleTarget(c, 900, 450, 360, 256);
    const float dx = 4;  // diagonal from (4,84) to (1176,811)
    const float diagX = 4 + (700.f - 84) / 727 * 1172;
    Target held = SingleTarget(c, diagX - dx, 700, 360, 256, &right);
    assert(held.side == Side::Right);
    Target flipped = SingleTarget(c, diagX - 80, 700, 360, 256, &right);
    assert(flipped.side == Side::Bottom);
    // refusal: too narrow for two 360 px sites side by side, too low to stack
    t = SingleTarget(Content(700, 815), 650, 450, 360, 256); assert(t.kind == Kind::Refuse && t.needed == 2);
    t = SingleTarget(Content(1180, 500), 590, 400, 360, 256); assert(t.kind == Kind::Refuse);

    // ---- existing group: middle replaces, outer quarters insert, preview = final rect
    auto two = Arrange(c, Orientation::Row, {1, 1}, {1, 1}, 1, 360);
    Target ins = GroupTarget(two, {1, 1}, -1, 560, 400, 360);
    assert(ins.kind == Kind::Insert && ins.index == 1);
    assert(Near(ins.preview.left, 396, 1) && Near(ins.preview.W(), 388, 1));
    Target rep = GroupTarget(two, {1, 1}, -1, 300, 400, 360);
    assert(rep.kind == Kind::Replace && rep.index == 0 && rep.preview.left == 4);
    // a member of the group: swap with the other site, nothing on itself
    Target sw = GroupTarget(two, {1, 1}, 1, 300, 400, 360); assert(sw.kind == Kind::Swap && sw.index == 0);
    Target own = GroupTarget(two, {1, 1}, 1, 880, 400, 360); assert(own.kind == Kind::None);
    Target mv = GroupTarget(two, {1, 1}, 1, 20, 400, 360); assert(mv.kind == Kind::Move && mv.index == 0 && mv.preview.left == 4);
    // a fourth site would fall under 360 px at 1180: refused, but replacing still works
    auto three = Arrange(c, Orientation::Row, {1, 1, 1}, {1, 1, 1}, 0, 360);
    Target full = GroupTarget(three, {1, 1, 1}, -1, three.slots[1].box.right - 10, 400, 360);
    assert(full.kind == Kind::Refuse && full.needed == 4);
    Target mid = GroupTarget(three, {1, 1, 1}, -1, three.slots[1].box.left + 190, 400, 360);
    assert(mid.kind == Kind::Replace && mid.index == 1);
    // ultra-wide: four, six, then seven sites side by side
    auto wide4 = Arrange(Content(2560, 1080), Orientation::Row, {1, 1, 1, 1}, {1, 1, 1, 1}, 0, 360);
    assert(wide4.visible == 4 && Near(wide4.slots[0].box.W(), 635, 1));
    auto wide6 = Arrange(Content(2560, 1080), Orientation::Row, {1, 1, 1, 1, 1, 1}, {1, 1, 1, 1, 1, 1}, 0, 360);
    assert(wide6.visible == 6 && Near(wide6.slots[5].box.W(), 422, 1));
    Target seventh = GroupTarget(wide6, {1, 1, 1, 1, 1, 1}, -1, wide6.slots[2].box.right - 5, 500, 360);
    assert(seventh.kind == Kind::Insert);
    auto wide7 = Arrange(Content(2560, 1080), Orientation::Row, {1, 1, 1, 1, 1, 1, 1}, {1, 1, 1, 1, 1, 1, 1}, 0, 360);
    assert(wide7.visible == 7 && Near(wide7.slots[6].box.W(), 361.14f, 1));
    Target eighth = GroupTarget(wide7, {1, 1, 1, 1, 1, 1, 1}, -1, wide7.slots[2].box.right - 5, 500, 360);
    assert(eighth.kind == Kind::Refuse && eighth.needed == 8);
    // insertion weights: the newcomer gets 1/(n+1), the others keep their ratio
    auto w = InsertWeights({3, 1}, 1); assert(w.size() == 3 && Near(w[1] / (w[0] + w[1] + w[2]), 1.f / 3, .01f) && w[0] / w[2] == 3);

    // ---- resize: only the two neighbours move; the minimum resists, then snaps back
    auto ex = Extents(three);
    auto moved = Resize(three, ex, 0, 20, 360, false);  // thirds of 1180 sit 28 DIP above the minimum
    assert(Near(moved[0], ex[0] + 20) && Near(moved[1], ex[1] - 20) && moved[2] == ex[2]);
    auto clamped = Resize(two, Extents(two), 0, -400, 360, false); assert(Near(clamped[0], 360));
    auto rubber = Resize(two, Extents(two), 0, -400, 360, true); assert(rubber[0] < 360 && rubber[0] > 250);
    assert(Near(SnapFirst(380, 1166, 0, 360, 2), std::max(360.f, 1166 / 3.f)));
    assert(Near(SnapFirst(600, 1166, 0, 360, 2), 583));
    assert(Near(SnapFirst(600, 1166, 3000, 360, 2), 1166 * 2 / 3.f));  // a flick projects toward 2/3
    assert(Near(SnapFirst(300, 1200, 0, 360, 3), 360));

    // ---- springs: critically damped, no overshoot, velocity kept on re-target
    Spring s; s.Snap(0); s.Set(100); float peak = 0;
    for (int i = 0; i < 120; ++i) { s.Step(1.f / 120, .3f); peak = std::max(peak, s.x); }
    assert(s.Settled() && s.x == 100 && peak <= 100.001f);
    Spring r; r.Snap(0); r.Set(100); for (int i = 0; i < 6; ++i) r.Step(1.f / 120, .3f);
    const float before = r.x, vel = r.v; r.Set(-100); r.Step(1.f / 120, .3f);
    assert(vel > 0 && r.x > before);  // no jump back: it keeps moving forward, then turns
    std::puts("PASS: units, capacity without cap, arrangement, rails on shrink, diagonal zones + hysteresis, refusal, insert/replace/swap/move, resize bounds, snapping, springs");
}
