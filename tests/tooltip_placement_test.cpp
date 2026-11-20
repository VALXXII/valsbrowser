// TooltipPlacement::Place (src/tooltip-placement.h): every hover tooltip of the browser is
// centred on its anchor, slid only to stay inside the window / screen with a margin, and its
// arrow position keeps pointing at the anchor. Pure arithmetic: no window is created.
// Built and run by test-sandbox.ps1.
#include "../src/tooltip-placement.h"
#include <cstdio>
#include <cstdlib>

using namespace NativeChrome;
static int checks = 0;
static void Require(bool ok, const char* what) { ++checks; if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); std::exit(1); } }
static LONG Center(RECT r) { return TooltipPlacement::FloorHalf(r.left + r.right); }

int main() {
    using TooltipPlacement::Place;
    const RECT area{0, 0, 1000, 700};
    // Anchor in the open: centred, 6 px under, arrow on the anchor centre.
    {
        const RECT a{400, 100, 432, 132}; const SIZE s{200, 28};
        const auto r = Place(a, s, area, 6, 8);
        Require(r.at.x == 316 && r.at.y == 138 && !r.above, "centred 6 px under the anchor");
        Require(r.at.x + s.cx / 2 == Center(a), "tip centre = anchor centre");
        Require(r.arrowX == 100, "arrow in the middle");
    }
    // Odd difference: half a pixel rounds left, never drifts more than 1 px.
    {
        const RECT a{400, 100, 433, 132}; const auto r = Place(a, {200, 28}, area, 6, 8);
        Require(r.at.x == 316, "odd widths round left");
    }
    // Media panel case: a 16 px info glyph with a wide sentence tip (the reported bug).
    {
        const RECT a{600, 300, 616, 316}; const SIZE s{320, 48};
        const auto r = Place(a, s, area, 6, 8);
        Require(r.at.x == 608 - 160 && r.arrowX == 160, "wide tip centred under a small glyph");
    }
    // Near the left edge: slid right just enough to keep the margin; arrow still on the anchor.
    {
        const RECT a{10, 10, 42, 42}; const SIZE s{200, 28};
        const auto r = Place(a, s, area, 6, 8);
        Require(r.at.x == 8, "left edge: stops 8 px inside");
        Require(r.at.x + r.arrowX == Center(a), "left edge: arrow points at the anchor");
    }
    // Near the right edge.
    {
        const RECT a{950, 10, 982, 42}; const SIZE s{200, 28};
        const auto r = Place(a, s, area, 6, 8);
        Require(r.at.x == 1000 - 8 - 200, "right edge: stops 8 px inside");
        Require(r.at.x + r.arrowX == Center(a), "right edge: arrow points at the anchor");
    }
    // Anchor partly outside the area: the arrow clamps to the tip, never outside it.
    {
        const auto r = Place({990, 10, 1030, 42}, {200, 28}, area, 6, 8);
        Require(r.arrowX >= 0 && r.arrowX <= 200, "arrow stays on the tip");
    }
    // Tip wider than the room: starts at the left margin.
    {
        const auto r = Place({400, 10, 432, 42}, {990, 28}, area, 6, 8);
        Require(r.at.x == 8, "wider than the room: from the left margin");
    }
    // No room below: flips above; no room either way: clamped inside.
    {
        auto r = Place({400, 650, 432, 682}, {200, 28}, area, 6, 8);
        Require(r.above && r.at.y == 650 - 6 - 28, "flips above");
        Require(r.at.x == 316, "flipped tip stays centred");
        r = Place({400, 0, 432, 700}, {200, 28}, area, 6, 8);
        Require(!r.above && r.at.y == 700 - 8 - 28, "no room either way: clamped inside");
    }
    // Secondary screens (negative / offset virtual-screen coordinates).
    {
        const RECT left{-1920, 0, 0, 1040};
        auto r = Place({-1000, 40, -968, 72}, {200, 28}, left, 6, 8);
        Require(r.at.x == -1084 && r.at.y == 78, "screen on the left: centred (negative coordinates)");
        r = Place({-1915, 40, -1883, 72}, {200, 28}, left, 6, 8);
        Require(r.at.x == -1912, "screen on the left: its own left edge");
        r = Place({-30, 40, -2, 72}, {200, 28}, left, 6, 8);
        Require(r.at.x == -8 - 200, "screen on the left: stops before the primary screen");
        const RECT right{1920, -200, 4480, 1240};
        r = Place({4460, -190, 4476, -174}, {200, 28}, right, 6, 8);
        Require(r.at.x == 4480 - 8 - 200 && r.at.y == -168, "screen on the right, above the primary");
        // Window spanning two monitors: Bounds keeps the anchor's monitor.
        const RECT window{1500, 100, 2600, 900};
        const RECT b = TooltipPlacement::Bounds(right, &window);
        Require(b.left == 1920 && b.right == 2600 && b.top == 100 && b.bottom == 900, "window ∩ the anchor's work area");
        const RECT apart{0, 0, 800, 600};
        const RECT c = TooltipPlacement::Bounds(right, &apart);
        Require(c.left == right.left && c.right == right.right, "no overlap: the work area alone");
        Require(TooltipPlacement::Bounds(right, nullptr).left == right.left, "no window: the work area");
    }
    // Display scaling 100 / 125 / 150 / 200 %: gap and margin scale, centring holds.
    for (int dpi : {96, 120, 144, 192}) {
        const int gap = TooltipPlacement::Px(dpi, TooltipPlacement::gapDip), margin = TooltipPlacement::Px(dpi, TooltipPlacement::marginDip);
        const auto px = [&](int dip) { return static_cast<LONG>(dip * dpi / 96); };
        const RECT screen{0, 0, px(1280), px(800)};
        const SIZE s{px(260), px(28)};
        const RECT mid{px(600), px(40), px(624), px(64)};
        auto r = Place(mid, s, screen, gap, margin);
        Require(std::labs(r.at.x + s.cx / 2 - Center(mid)) <= 1, "scaled: centred within 1 px");
        Require(r.at.y == mid.bottom + gap, "scaled: gap follows the DPI");
        const RECT edge{px(4), px(40), px(28), px(64)};
        r = Place(edge, s, screen, gap, margin);
        Require(r.at.x == margin && margin == (dpi == 96 ? 8 : dpi == 120 ? 10 : dpi == 144 ? 12 : 16), "scaled: 8 DIP edge margin");
        Require(r.at.x + r.arrowX == Center(edge), "scaled: arrow on the anchor");
        const RECT rightmost{px(1270), px(40), px(1280), px(64)};
        r = Place(rightmost, s, screen, gap, margin);
        Require(r.at.x + s.cx == screen.right - margin, "scaled: right edge margin");
    }
    std::printf("tooltip placement: %d checks passed\n", checks);
    return 0;
}
