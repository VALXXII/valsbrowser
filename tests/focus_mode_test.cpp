// Mode Focus model (src/focus-mode.h, design/focus-mode-plan.md, Paper « Mode Focus » p-U-0): the Paper values, the
// floating panel's geometry at a slide position, the hit box, the show / keep / hide timing (grace, reversal mid-slide,
// keyboard hold, reduced motion), the easing and the hole's pixel spans at 100 / 125 / 150 / 175 % DPI.
// Pure: no Win32, no window. Built with the strict flags (tests/update-integration/run_all.py).
#include "../src/focus-mode.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace FM = NativeChrome::FocusMode;
static int checks = 0;
static void Require(bool ok, const char* what) { ++checks; if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); std::exit(1); } }
static bool Near(double a, double b, double e = 1e-4) { return std::fabs(a - b) <= e; }
static bool Same(const FM::Rect& r, float l, float t, float rr, float b) { return Near(r.left, l) && Near(r.top, t) && Near(r.right, rr) && Near(r.bottom, b); }

// A pixel belongs to the hole when the panel's shape covers it completely (bottom corners on the arc, top square):
// every point of the pixel square is inside, i.e. its point nearest the corner, measured analytically here.
static bool FullyCovered(int x, int y, FM::Pixels p, float r) {
    if (x < p.left || x >= p.right || y < p.top || y >= p.bottom) return false;
    const double cy = p.bottom - r, py = std::max<double>(y + 1, cy);   // the pixel's bottom edge, nearest the arc's zone
    if (y + 1 <= cy) return true;
    if (x < p.left + r) { const double px = x, d = std::hypot(p.left + r - px, py - cy); return d <= r + 1e-9; }
    if (x + 1 > p.right - r) { const double px = x + 1, d = std::hypot(px - (p.right - r), py - cy); return d <= r + 1e-9; }
    return true;
}
static bool InSpans(const std::vector<FM::Pixels>& spans, int x, int y) {
    for (const auto& s : spans) if (x >= s.left && x < s.right && y >= s.top && y < s.bottom) return true;
    return false;
}

int main() {
    // ---- Paper values -------------------------------------------------------------------------------------------
    Require(FM::pageInset == 4 && FM::pageRadius == 6, "Activé (1J9P-0): page inset 4 on all sides, corners 6");
    Require(FM::hitHeight == 14, "Hitbox (1J9W-0): 14 DIP high");
    Require(FM::PanelHeight(false) == 84 && FM::PanelHeight(true) == 124, "panel 84 DIP, 124 with the bookmarks band (decision 3)");
    Require(FM::panelInset == 4 && FM::panelRadius == 4 && FM::border == 1 && FM::padTop == 2 && FM::padBottom == 6, "panel: top 4, radius 4, 1 px border, padding 2 / 6");
    Require(std::lround(FM::borderAlpha * 255) == 0x14, "border #FFFFFF14");
    Require(FM::layoutInset == 10 && 1178 - FM::layoutInset == 1168, "chrome layout width: client - 10 (1168 in the 1178 artboard)");
    Require(FM::RestBottom(false) == 88 && FM::RestBottom(true) == 128 && FM::DrawerTop(false) == 92 && FM::DrawerTop(true) == 132, "rest bottom 88 / 128, drawers 4 DIP under it");
    Require(FM::showMs == 200 && FM::hideMs == 160 && FM::graceMs == 300, "slide in 200 ms, out 160 ms, 300 ms grace");

    // ---- Panel geometry -----------------------------------------------------------------------------------------
    // The artboard's client (1178 x 703): the panel 1170 wide at x 4 .. 1174, top 4, bottom 88 at rest.
    Require(Same(FM::Panel(1178, false, 1), 4, 4, 1174, 88), "Paper 1JBQ-0: panel {4, 4, 1174, 88} in a 1178 client");
    Require(Near(FM::Panel(1178, false, 1).right - FM::Panel(1178, false, 1).left, 1170), "panel width = client - 8 (1170)");
    for (float w : {560.f, 1180.f}) {
        Require(Same(FM::Panel(w, false, 0), 4, -84, w - 4, 0), "hidden: the whole panel above the window (bottom on y 0)");
        Require(Same(FM::Panel(w, false, .5f), 4, -40, w - 4, 44), "halfway: top -40 (= -84 + 88 / 2)");
        Require(Same(FM::Panel(w, false, 1), 4, 4, w - 4, 88), "at rest: top 4, bottom 88");
        Require(Same(FM::Panel(w, true, 0), 4, -124, w - 4, 0), "band: hidden bottom on y 0");
        Require(Same(FM::Panel(w, true, .5f), 4, -60, w - 4, 64), "band: halfway top -60 (= -124 + 128 / 2)");
        Require(Same(FM::Panel(w, true, 1), 4, 4, w - 4, 128), "band: at rest 4 .. 128");
        Require(Same(FM::Panel(w, false, -1), 4, -84, w - 4, 0) && Same(FM::Panel(w, false, 2), 4, 4, w - 4, 88), "position clamped to 0 .. 1");
    }
    {   const auto o = FM::Origin(FM::Panel(1180, false, 1)); Require(Near(o.x, 5) && Near(o.y, 5), "chrome-local (0, 0) at client (5, 5) at rest");
        const auto h = FM::Origin(FM::Panel(1180, false, 0)); Require(Near(h.x, 5) && Near(h.y, -83), "hidden: origin above the window");
        // The normal chrome's rows land where Paper draws them: tab row 2 .. 38 -> 7 .. 43, toolbar 44 .. 76 -> 49 .. 81,
        // 6 DIP of padding then the 1 DIP border: 81 + 6 + 1 = 88.
        Require(Near(o.y + 2, 7) && Near(o.y + 76 + FM::padBottom + FM::border, 88), "rows inside the panel"); }
    Require(FM::Visible(FM::Panel(1180, false, 0)).Empty(), "hidden: nothing on screen");
    Require(Same(FM::Visible(FM::Panel(1180, false, .5f)), 4, 0, 1176, 44), "visible part clipped at the client's top edge");
    Require(Same(FM::Content(1178, 703), 4, 4, 1174, 699), "page box {4, 4, W - 4, H - 4}");
    Require(Same(FM::Content(1178, 703, 300), 4, 4, 874, 699), "a chat strip on the right is kept out of the page");
    Require(Near(FM::Snap(5, 1.25f), 4.8) && Near(FM::Snap(4, 1.25f), 4) && Near(FM::Snap(4, 1.5f), 4) && Near(FM::Snap(5, 1), 5), "snap to whole device pixels");

    // ---- Hit box ------------------------------------------------------------------------------------------------
    Require(FM::InHitBox(0, 0, 1178) && FM::InHitBox(1177.9f, 13.9f, 1178), "hit box: (0, 0) .. (W, 14) exclusive");
    Require(!FM::InHitBox(600, 14, 1178) && !FM::InHitBox(1178, 5, 1178) && !FM::InHitBox(-.1f, 5, 1178) && !FM::InHitBox(600, -.1f, 1178), "y 14, x = W, left of or above the client: out");

    // ---- With the annotation mode (owner's rule 2026-11-24): the banner and the chat stay ------------------------------
    // Paper page « #127 — Panneau latéral »: « Annotation + Focus · barre du haut » (203J-0, at rest), « … panneau révélé »
    // (20MM-0), « … tiroir ouvert » (21OK-0). The boards are 1180 x 740 with the window's 1 px frame; their values are read
    // as the window's (1180 x 740), the way the designer laid them out (page 788 = 1180 - 4 - 4 - 380 - 4).
    {   const float W = 1180, H = 740, banner = 32, chat = 380; const FM::Above above{banner, FM::ChatReserve(chat)};
        Require(FM::bannerInset == 4 && FM::chatInset == 4 && FM::chatGap == 4 && FM::chatRadius == 6 && FM::chatBorder == 1 && FM::chatBorderRgb == 0x262626,
                "203J-0: banner x 4, chat card 4 from the edges, 4 from the page, radius 6, 1 px #262626");
        Require(Same(FM::Banner(W, banner), 4, 0, 1176, 32), "203J-0: the banner at the very top, x 4 .. W - 4, 32 high");
        Require(Same(FM::Content(W, H, FM::ChatReserve(chat), banner), 4, 32, 792, 736), "203J-0: the page under the banner, x 4, 788 wide, 4 above the bottom");
        Require(Same(FM::ChatCard(W, H, chat, banner), 796, 32, 1176, 736), "203J-0: the chat card x 796, top 32, 380 x 704");
        Require(Same(FM::Content(W, H, 0, banner), 4, 32, 1176, 736), "Mode Commentaire (no chat card): the page to W - 4 under the banner");
        Require(FM::PageTop(0) == 4 && FM::PageTop(2) == 4 && FM::PageTop(32) == 32 && FM::PageTop(16) == 16, "page top: the inset, or under the banner (sliding in)");
        Require(Same(FM::Panel(W, false, 1, above), 4, 36, 792, 120), "20MM-0: the revealed panel x 4, top 36 (4 under the banner), 788 wide (the page's)");
        Require(Same(FM::Panel(W, false, 0, above), 4, 32 - 84, 792, 32), "hidden: the panel's bottom on the banner's (it slides out from behind it)");
        Require(Same(FM::Panel(W, true, 1, above), 4, 36, 792, 160), "bookmarks band: 124 high under the banner");
        Require(Same(FM::Visible(FM::Panel(W, false, 1, above)), 4, 36, 792, 120), "at rest, wholly under the banner");
        Require(FM::InHitBox(0, 32, W, above) && FM::InHitBox(795.9f, 45.9f, W, above), "20MM-0: hover strip 32 .. 46 under the banner, up to the chat card (x 796)");
        Require(!FM::InHitBox(400, 31.9f, W, above) && !FM::InHitBox(400, 46, W, above) && !FM::InHitBox(800, 40, W, above), "not over the banner, not below 46, not over the chat");
        Require(FM::RestBottom(false, banner) == 120 && FM::DrawerTop(false, banner) == 124 && FM::DrawerTop(true, banner) == 164, "21OK-0: a drawer 4 under the revealed panel: top 124");
        Require(FM::drawerOverChat && FM::drawerBottom == 4, "21OK-0: the drawer over the chat (right edge W - 4), its bottom 4 above the window's (612 from 124 in 740)");
        // The chrome inside the revealed panel: the panel's width minus its 1 px border on each side.
        const auto r = FM::Panel(W, false, 1, above); Require(Near(r.right - r.left - 2 * FM::border, W - FM::layoutInset - above.reserve), "chrome layout width = panel - 2 (786 in 20MM-0)");
        Require(Same(FM::Panel(W, false, 1, {}), 4, 4, 1176, 88) && FM::InHitBox(1179.9f, 0, W) && FM::ChatReserve(0) == 0, "without the banner and the chat: Paper 1JBQ-0 unchanged");
    }
    // ---- Easing -------------------------------------------------------------------------------------------------
    Require(FM::EaseOut(0) == 0 && FM::EaseOut(1) == 1 && FM::EaseOut(-1) == 0 && FM::EaseOut(2) == 1, "ease-out endpoints");
    {   double last = 0; bool monotonic = true; for (int i = 1; i <= 1000; ++i) { const double v = FM::EaseOut(i / 1000.0); if (v < last - 1e-12) monotonic = false; last = v; }
        Require(monotonic, "ease-out is monotonic");
        // Reference: bisection on the curve's own parameter (cubic-bezier(0.23, 1, 0.32, 1)).
        for (double x : {.05, .1, .25, .5, .75, .9}) {
            double lo = 0, hi = 1; for (int i = 0; i < 200; ++i) { const double t = (lo + hi) / 2, u = 1 - t, bx = 3 * u * u * t * .23 + 3 * u * t * t * .32 + t * t * t; (bx < x ? lo : hi) = t; }
            const double t = (lo + hi) / 2, u = 1 - t, by = 3 * u * u * t * 1 + 3 * u * t * t * 1 + t * t * t;
            Require(Near(FM::EaseOut(x), by, 1e-5), "matches CSS cubic-bezier(0.23, 1, 0.32, 1)"); }
        Require(FM::EaseOut(.25) > .7 && FM::EaseOut(.5) > .9, "strong ease-out: most of the way early"); }

    // ---- Show / keep / hide -------------------------------------------------------------------------------------
    FM::Input none, reveal, pointer, hold, keep; reveal.reveal = reveal.pointer = true; pointer.pointer = true; hold.hold = true; keep.keep = true;
    {   FM::Bar bar; bar.Reset(false, 0);
        Require(!bar.Shown() && bar.Position(0) == 0 && !bar.Visible(0), "starts hidden");
        Require(!bar.Update(0, keep, false) && !bar.Shown(), "a menu or a drag never brings a hidden bar");
        Require(bar.Update(10, reveal, false) && bar.Shown() && bar.Moving(10), "the hit box reveals: the slide starts");
        Require(bar.Position(10) == 0 && bar.Position(110) > .9 && bar.Position(110) < 1 && bar.Moving(209), "200 ms ease-out slide (most of the way in the first half)");
        Require(bar.Position(210) == 1 && !bar.Moving(210), "at rest after 200 ms");
        Require(Near(bar.Position(60), FM::EaseOut(.25)), "position = ease-out(t / 200 ms)");
        bar.Update(250, pointer, false);   // last moment the pointer was over the panel
        Require(!bar.Update(300, none, false) && bar.Shown(), "nothing holds it: the grace starts");
        Require(!bar.Update(549, none, false) && bar.Shown() && bar.Position(549) == 1, "still shown 299 ms later");
        Require(bar.Update(550, none, false) && !bar.Shown() && bar.Position(550) == 1, "300 ms without anything: the slide out starts where it is");
        Require(Near(bar.Position(630), 1 - FM::EaseOut(.5)) && bar.Position(710) == 0 && !bar.Visible(710), "160 ms ease-out slide up");
        // Reversal mid-slide: from the current position, no jump; the way back takes its share of the 200 ms.
        bar.Update(550, pointer, false); bar.Update(551, none, false);   // (still hiding: the pointer over the panel does not bring it back)
        Require(!bar.Shown(), "only the hit box brings a hiding bar back");
        const double at = bar.Position(630);
        Require(bar.Update(630, reveal, false) && bar.Shown() && Near(bar.Position(630), at), "reversed at once, from where it is");
        const double end = 630 + FM::showMs * (1 - at);
        Require(bar.Position(end - 1) < 1 && bar.Position(end) == 1 && Near(bar.SlideEnd(), end), "the way back covers its distance in its share of 200 ms");
        Require(bar.Position(631) > at, "and moves on at once (ease-out)"); }
    {   // Grace: a pointer slightly too low comes back in time and the bar stays.
        FM::Bar bar; bar.Reset(true, 0);
        Require(!bar.Update(100, none, false) && !bar.Update(250, none, false) && !bar.Update(299, pointer, false) && !bar.Update(598, none, false) && bar.Shown(), "back within 300 ms: stays");
        Require(bar.Update(599, none, false) && !bar.Shown(), "then hides 300 ms after the last hold"); }
    {   // The keyboard (or an open surface): at once, never animated, and it keeps the bar.
        FM::Bar bar; bar.Reset(false, 0);
        Require(bar.Update(5, hold, false) && bar.Shown() && bar.Position(5) == 1 && !bar.Moving(5), "Ctrl+L / F6 / a drawer: shown at once");
        Require(!bar.Update(1000, hold, false) && bar.Shown(), "kept while held");
        Require(!bar.Update(1200, none, false) && bar.Update(1300, none, false), "released: hides after the grace");
        // A hold during a pointer slide lands it at rest at once (the field / popover sits where the panel stays).
        bar.Reset(false, 2000); bar.Update(2000, reveal, false); Require(bar.Moving(2050), "sliding in");
        Require(bar.Update(2050, hold, false) && bar.Position(2050) == 1 && !bar.Moving(2050), "a click on the sliding panel: at rest at once");
        // A held, shown bar with the pointer away never hides.
        Require(!bar.Update(5000, hold, false) && bar.Shown(), "held: no grace runs out"); }
    {   // The host's lead before a slide-out moves (the sliding picture goes in place first): the panel waits at rest,
        // then takes the whole 160 ms; a reveal during the lead lands at rest at once (nothing moved).
        FM::Bar bar; bar.hideLeadMs = 40; bar.Reset(true, 0);
        Require(bar.Update(300, none, false) && !bar.Shown() && bar.Moving(300), "hide decided: moving (the lead counts)");
        Require(bar.Position(339) == 1 && bar.Position(340) == 1 && bar.Position(341) < 1 && bar.Position(500) == 0 && !bar.Moving(500), "waits 40 ms at rest, then the 160 ms slide");
        Require(bar.Update(320, reveal, false) && bar.Shown() && bar.Position(320) == 1 && !bar.Moving(320), "back during the lead: at rest, no slide");
        FM::Bar fast; fast.hideLeadMs = 40; fast.Reset(true, 0); fast.Update(300, none, true); Require(fast.Position(300) == 0, "reduced motion: no lead, hidden at once"); }
    {   // Reduced motion (the app's switch or Windows'): no slide.
        FM::Bar bar; bar.Reset(false, 0);
        Require(bar.Update(0, reveal, true) && bar.Position(0) == 1 && !bar.Moving(0), "reduced: shown at once");
        Require(bar.Update(400, none, true) && bar.Position(400) == 0 && !bar.Visible(400), "reduced: hidden at once after the grace"); }
    {   FM::Slide s; s.Snap(0, 0); s.Go(1, 0, 200); s.Go(1, 50, 200);
        Require(Near(s.End(), 200), "the same target again does not restart the slide"); }

    // ---- The hole: pixel spans at 100 / 125 / 150 / 175 % ---------------------------------------------------------
    for (float scale : {1.f, 1.25f, 1.5f, 1.75f}) {
        for (float position : {1.f, .5f, .1f}) {
            const auto r = FM::Panel(1180, false, position);
            const FM::Pixels px{(int)std::lround(FM::Snap(r.left, scale) * scale), (int)std::lround(FM::Snap(r.top, scale) * scale),
                                (int)std::lround(FM::Snap(r.right, scale) * scale), (int)std::lround((FM::Snap(r.top, scale) + FM::Snap(FM::PanelHeight(false), scale)) * scale)};
            const float radius = FM::panelRadius * scale;
            const auto spans = FM::HoleSpans(px, radius);
            Require(!spans.empty() && spans.front().top == px.top && spans.front().left == px.left && spans.front().right == px.right, "top rows: the panel's full width (square top corners)");
            Require((int)spans.size() <= 1 + (int)std::ceil(radius), "one rectangle for the straight part, at most one per arc scanline");
            int mismatches = 0;
            for (int y = px.top - 1; y <= px.bottom; ++y) for (int x = px.left - 1; x <= px.right; ++x)
                if (FullyCovered(x, y, px, radius) != InSpans(spans, x, y)) ++mismatches;
            Require(mismatches == 0, "a pixel is in the hole exactly when the panel's shape covers it completely");
            // The corner pixels left out are exactly the ones the corner window paints (coverage > 0 and < 1 or a sliver).
            const auto corner = FM::BottomCorner(radius, scale, 23 / 255.f, 41 / 255.f); const int n = FM::CornerSize(radius);
            int lost = 0;
            for (int j = 0; j < n; ++j) for (int i = 0; i < n; ++i) {
                const bool hole = InSpans(spans, px.left + i, px.bottom - 1 - j), full = FM::FullyInside(i, j, radius);
                if (hole != full) ++lost;
                if (full && corner[j * n + i].alpha < 1.f - 1e-6f) ++lost;   // a hole pixel is one the shape covers wholly
            }
            Require(lost == 0, "hole and corner window share the corner pixels");
            // The bottom corners: the last scanline loses pixels on both sides (the page shows outside the arc).
            const auto& last = spans.back(); Require(last.bottom == px.bottom && last.left > px.left && last.right < px.right, "bottom corners follow the arc");
            Require(last.left - px.left == px.right - last.right, "both bottom corners alike");
            // Clipped to a page (its window coordinates).
            const FM::Pixels page{(int)std::lround(4 * scale), (int)std::lround(4 * scale), (int)std::lround(1176 * scale), (int)std::lround(700 * scale)};
            const auto mine = FM::ClipTo(spans, page);
            for (const auto& s : mine) Require(s.left >= 0 && s.top >= 0 && s.right <= page.right - page.left && s.bottom <= page.bottom - page.top, "page window coordinates");
            if (position == 1.f) Require(!mine.empty() && mine.front().top == 0 && mine.front().left == 0, "at rest: the hole starts at the page's own top-left");
        }
    }
    {   // 100 %: the 4 px arc, the fully covered pixels scanline by scanline from the bottom: insets 4, 2, 1, 1.
        const auto spans = FM::HoleSpans({4, 4, 1176, 88}, 4);
        Require(spans.size() == 4 && spans[0].bottom == 84 && spans[1].left == 5 && spans[1].top == 84 && spans[1].bottom == 86 && spans[2].left == 6 && spans[2].top == 86
                && spans[3].left == 8 && spans[3].right == 1172 && spans[3].top == 87, "100 %: corner insets 1, 1, 2, 4");
        // The corner's own pixels: the outermost one barely covered, the border's grey along the edge, the fill inside.
        const auto c = FM::BottomCorner(4, 1, 23 / 255.f, 41 / 255.f);
        Require(c.size() == 16 && c[0].alpha == 0 && c[1].alpha > 0 && c[1].alpha < 1 && std::fabs(c[1].grey - 41 / 255.f) < 1e-6f && c[3 * 4 + 3].alpha == 1.f && c[3 * 4 + 3].grey < 24 / 255.f,
                "corner pixels: the outermost one outside the arc, partial ones in the border's grey, the fill inside");
        Require(c[0 * 4 + 3].alpha > .9f && c[0 * 4 + 3].alpha < 1 && std::fabs(c[0 * 4 + 3].grey - 41 / 255.f) < 1e-6f, "the bottom row near the arc's end: almost covered, the border's grey");
        double area = 0; for (auto& p : c) area += 1 - p.alpha; Require(std::fabs(area - 16 * (1 - 3.14159265 / 4)) < .15, "cut area = r^2 (1 - pi/4)");
        Require(FM::HoleSpans({0, 0, 0, 10}, 4).empty() && FM::ClipTo(FM::HoleSpans({4, -40, 1176, 44}, 4), {4, 4, 1176, 700}).front().top == 0, "empty panel: no hole; partly above the window: clipped to the page"); }
    std::printf("PASS focus mode model: %d checks (Paper values, geometry, hit box, annotation banner + chat card, easing, timing, hole spans at 4 DPI)\n", checks);
    return 0;
}
