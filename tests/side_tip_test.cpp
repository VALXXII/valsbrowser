// #143 · Infobulle B of the side column's tabs and favourites (src/side-tip.h): Paper 26QB-0's box, its place beside the
// hovered plate / row / tile (panel on the left and on the right, the compact and the complete column, never clipped by
// the window), when it shows (500 ms in the complete column, at once in the compact one, at once from one item to the
// next, a click keeps it away until another item) and its motion on Designer's values (appear on a 0.18 s spring with no
// bounce: opacity, 4 DIP out from the icon, 0.98 -> 1; a 100 ms fade out with no slide; a 0.2 s glide between items with
// an 80 ms cross-fade and no frame without the tip; interruptions from what is on screen; reduced motion; mirrored).
// Pure: no window. The scale (DPI) never enters the model (DIP); src/side-tip-window.h turns it into pixels.
#include "../src/side-tip.h"
#include <cstdio>
#include <string>

namespace ST = NativeChrome::SideTip; namespace M = NativeChrome::Motion;
static int checks = 0, failures = 0;
static void Check(bool ok, const std::string& what) { ++checks; if (!ok) { std::printf("FAIL %s\n", what.c_str()); ++failures; } }
static bool Near(float a, float b, float e = .01f) { return std::fabs(a - b) <= e; }
static M::Time At(M::Time t0, double ms) { return t0 + std::chrono::duration_cast<M::Clock::duration>(std::chrono::duration<double, std::milli>(ms)); }

int main() {
    // ---- Paper 26QB-0 ----
    Check(Near(ST::Look::Height(true), 56) && Near(ST::Look::Height(false), 34), "box: 56 high with the address line, 34 with the title alone (1 + 6 + 20 + 2 + 20 + 6 + 1)");
    Check(Near(ST::Look::Width(150, 80, true), 168) && Near(ST::Look::Width(40, 120, true), 158), "box: 2 + 16 + the longer line (the address line: 14 + 6 + its text)");
    Check(Near(ST::Look::Width(900, 0, false), 300), "box: 300 at most (an ellipsis beyond)");
    Check(ST::Look::surfaceRgb == 0x171717 && ST::Look::borderRgb == 0x2B2B2B && ST::Look::titleRgb == 0xE8E8E8 && ST::Look::addressRgb == 0x8B8B8B && Near(ST::Look::radius, 6), "box: #171717, 1 px #2B2B2B, radius 6, title #E8E8E8, address #8B8B8B");
    // ---- place ----
    {   const ST::Box plate{4, 110, 36, 142};   // the compact column's 32 plate (Paper 26PL-0)
        auto b = ST::Place(plate, 168, 56, 1180, 740, true);
        Check(Near(b.left, 44) && Near(b.top, 126 - 28) && Near(b.Width(), 168), "compact, panel on the left: 8 from the plate (x 44 as in Paper), centred on it");
        b = ST::Place({1144, 110, 1176, 142}, 168, 56, 1180, 740, false);
        Check(Near(b.right, 1136) && Near(b.top, 98), "panel on the right: 8 left of the plate, mirrored");
        b = ST::Place({8, 103, 240, 135}, 200, 56, 1180, 740, true);
        Check(Near(b.left, 248) && Near(b.top, 91), "complete column: 8 from the row's right edge (240 + 8)");
        b = ST::Place({4, 4, 36, 36}, 168, 56, 1180, 740, true);
        Check(Near(b.top, 0), "an item at the window's top: the box stays inside it");
        b = ST::Place({4, 700, 36, 732}, 168, 56, 1180, 740, true);
        Check(Near(b.bottom, 740), "an item at the window's bottom: the box stays inside it");
        b = ST::Place({4, 300, 36, 332}, 280, 56, 300, 740, true);
        Check(Near(b.right, 300) && b.left >= 0, "a window too narrow: the box slides back inside it (never clipped)");
    }
    // ---- when it shows ----
    {   ST::Gate g;
        Check(g.Hover(1, 0, ST::Timing::delayMs, false) == 0 && g.Hover(1, 499, ST::Timing::delayMs, false) == 0 && g.Hover(1, 500, ST::Timing::delayMs, false) == 1, "complete column: after 500 ms of rest");
        Check(Near((float)g.Remaining(200, ST::Timing::delayMs), 300), "complete column: the wait left (300 ms after 200)");
        Check(g.Hover(2, 510, ST::Timing::delayMs, true) == 2, "complete column: the next item at once while a tip is on screen");
        Check(g.Hover(0, 520, ST::Timing::delayMs, true) == 0 && g.Item() == 2, "a gap between items: nothing new, the tip stays");
        g.Reset();
        Check(g.Hover(3, 1000, ST::Timing::compactDelayMs, false) == 3, "compact column: at once (owner, 2026-11-26)");
        g.Dismiss();
        Check(g.Hover(3, 1010, 0, false) == 0 && g.Hover(0, 1020, 0, false) == 0 && g.Hover(3, 1030, 0, false) == 0, "a click: not again on that item, a gap does not bring it back");
        Check(g.Hover(4, 1040, 0, false) == 4 && g.Hover(3, 1050, 0, true) == 3, "a click: another item brings it back, then the clicked one too");
        g.Dismiss(); g.Reset(); Check(g.Hover(4, 1060, 0, false) == 4, "left the column: everything fresh");
    }
    // ---- appear ----
    const auto t0 = M::Now();
    for (bool left : {true, false}) {
        ST::Motion m; const ST::Box box{44, 98, 212, 154};
        m.Show(1, box, left, t0, false);
        auto f = m.At(t0);
        Check(f.visible && Near(f.opacity, 0) && Near(f.dx, left ? -4.f : 4.f) && Near(f.scale, .98f) && Near(f.box.left, 44) && Near(f.box.top, 98), std::string(left ? "left" : "right") + ": appears from 0, 4 DIP toward the icon, 0.98, in its place");
        float last = -1; bool monotonic = true, outward = true;
        for (int ms = 0; ms <= 600; ms += 8) { const auto g = m.At(At(t0, ms)); if (g.opacity < last - 1e-4f) monotonic = false; last = g.opacity; if (left ? g.dx > 1e-4f : g.dx < -1e-4f) outward = false; }
        Check(monotonic && outward, std::string(left ? "left" : "right") + ": no bounce (damping 1): opacity only rises, the slide never passes its place");
        f = m.At(At(t0, 90));
        Check(f.opacity > .6f && f.opacity < .95f && std::fabs(f.dx) < 1.6f && f.scale > .99f, "half the response (90 ms): mostly in (" + std::to_string(f.opacity) + ")");
        f = m.At(At(t0, 600));
        Check(Near(f.opacity, 1, .002f) && Near(f.dx, 0, .01f) && Near(f.scale, 1, .001f) && !m.Moving(At(t0, 2000)), "settled at rest: opacity 1, in place, 1.0");
    }
    // ---- disappear: a 100 ms fade, no slide ----
    {   ST::Motion m; m.Show(1, {44, 98, 212, 154}, true, t0, false);
        const auto t1 = At(t0, 1000); m.Hide(t1);
        auto f = m.At(At(t1, 50));
        Check(Near(f.opacity, .5f, .02f) && Near(f.dx, 0) && Near(f.scale, 1) && Near(f.box.left, 44), "disappear: half gone after 50 ms, where it was, no slide");
        f = m.At(At(t1, 101));
        Check(!f.visible && Near(f.opacity, 0), "disappear: gone after 100 ms");
    }
    // ---- from one item to the next: a glide, never a frame without the tip ----
    {   ST::Motion m; m.Show(1, {44, 98, 212, 154}, true, t0, false);
        const auto t1 = At(t0, 1000); const ST::Box next{44, 134, 260, 190};
        m.Show(2, next, true, t1, false);
        float minOpacity = 1; bool between = true;
        for (int ms = 0; ms <= 500; ms += 4) { const auto f = m.At(At(t1, ms)); minOpacity = std::min(minOpacity, f.opacity); if (f.box.top < 98 - .01f || f.box.top > 134 + .01f) between = false; }
        Check(Near(minOpacity, 1, .001f), "next item: the tip never goes away (no flicker)");
        Check(between, "next item: the box glides from the old place to the new one, never past it");
        auto f = m.At(At(t1, 100));
        Check(f.box.top > 98 && f.box.top < 134 && f.box.Width() > 168 && f.box.Width() < 216, "next item: half-way at 100 ms, top and width together");
        f = m.At(At(t1, 40)); Check(f.item == 2 && f.previous == 1 && f.cross > 0 && f.cross < 1, "next item: the text cross-fades (both drawn at 40 ms)");
        f = m.At(At(t1, 81)); Check(Near(f.cross, 1) && f.previous == 0, "next item: the cross-fade done in 80 ms");
        f = m.At(At(t1, 800)); Check(Near(f.box.top, 134, .01f) && Near(f.box.Width(), 216, .01f), "next item: settled on it");
    }
    // ---- interruptions: from what is on screen ----
    {   ST::Motion m; m.Show(1, {44, 98, 212, 154}, true, t0, false);
        const auto t1 = At(t0, 60); const float shown = m.At(t1).opacity;
        m.Hide(t1);
        Check(Near(m.At(t1).opacity, shown, .001f), "hidden mid-appear: the fade starts from the opacity on screen");
        const auto t2 = At(t1, 30); const float during = m.At(t2).opacity, dx = m.At(t2).dx;
        m.Show(1, {44, 98, 212, 154}, true, t2, false);
        Check(Near(m.At(t2).opacity, during, .001f) && Near(m.At(t2).dx, dx, .001f), "back mid-fade: from the opacity and slide on screen, no jump");
        Check(m.At(At(t2, 600)).opacity > .99f, "back mid-fade: then all the way in");
        // Mid-glide to a third item: from the box on screen.
        ST::Motion g; g.Show(1, {44, 98, 212, 154}, true, t0, false); const auto u1 = At(t0, 1000);
        g.Show(2, {44, 200, 212, 256}, true, u1, false); const auto u2 = At(u1, 60); const float top = g.At(u2).box.top;
        g.Show(3, {44, 300, 212, 356}, true, u2, false);
        Check(Near(g.At(u2).box.top, top, .001f) && g.At(At(u2, 8)).box.top > top, "a third item mid-glide: from where the box is, on toward the new one");
    }
    // ---- reduced motion: fades only ----
    {   ST::Motion m; m.Show(1, {44, 98, 212, 154}, true, t0, true);
        auto f = m.At(At(t0, 50));
        Check(Near(f.dx, 0) && Near(f.scale, 1) && Near(f.opacity, .5f, .02f), "reduced: appear = a 100 ms fade, no slide, no scale");
        Check(Near(m.At(At(t0, 101)).opacity, 1), "reduced: in after 100 ms");
        const auto t1 = At(t0, 1000); m.Show(2, {44, 200, 212, 256}, true, t1, true);
        f = m.At(t1);
        Check(Near(f.box.top, 200) && Near(f.opacity, 1) && f.previous == 1 && Near(f.cross, 0), "reduced, next item: the box jumps there, the text cross-fades");
        Check(Near(m.At(At(t1, 81)).cross, 1), "reduced, next item: the cross-fade done in 80 ms");
        const auto t2 = At(t1, 1000); m.Hide(t2);
        Check(Near(m.At(At(t2, 50)).opacity, .5f, .02f) && !m.At(At(t2, 101)).visible, "reduced: disappear = the same 100 ms fade");
    }
    std::printf("%s: %d checks, %d failures\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
