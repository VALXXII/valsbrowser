// Panneau latéral (#127, variante A finale): geometry, fold and hover timers of src/sidebar.h against the Paper
// values (page « #127 — Panneau latéral (A finale validée) », planches 01, 02, 03, 04, 05, 07). The client is the
// artboard's inside (1180 x 740 with its 1 DIP border: 1178 x 738).
#include "../src/sidebar.h"
#include <cstdio>
#include <cmath>

namespace S = NativeChrome::Sidebar;
static int failures = 0;
static void Check(bool ok, const char* what) { if (!ok) { std::printf("FAIL %s\n", what); ++failures; } }
static bool Near(float a, float b, float e = .01f) { return std::fabs(a - b) <= e; }
static bool Same(const S::Box& b, float l, float t, float r, float bo) { return Near(b.left, l) && Near(b.top, t) && Near(b.right, r) && Near(b.bottom, bo); }

int main() {
    const float W = 1178, H = 738;
    // ---- 01 Complet: 5 bookmarks, 5 tabs, 4 tools.
    {
        S::Input in; in.mode = S::Mode::Complete; in.width = W; in.height = H; in.grid = true; in.tiles = 5; in.tabs = 5; in.tools = 4;
        const auto L = S::Compute(in);
        Check(Near(L.column, 248), "complete column 248 (1Q0A-0)");
        Check(Same(L.profile, 8, 8, 40, 40), "profile 32 at 8,8 (1V1O-0)");
        Check(Same(L.back, 46, 8, 78, 40) && Same(L.forward, 85, 8, 117, 40), "Back / Forward pair 6 after the profile, 3 + 1 + 3 apart (1VCX-0)");
        Check(Same(L.hairline, 81, 19, 82, 29), "pair hairline 1 x 10 centred (1VD1-0)");
        Check(Same(L.reload, 123, 8, 155, 40), "Reload 6 after the pair (1VD5-0)");
        Check(Same(L.toggle, 212, 10, 240, 38), "fold button 28 at the row's right (1Q2D-0)");
        Check(L.addressInColumn && Same(L.address, 8, 50, 240, 82), "address 232 x 32 at y 50 (1Q26-0)");
        Check(Same(L.lock, 16, 59, 30, 73) && Same(L.star, 218, 59, 232, 73) && Same(L.copy, 198, 59, 212, 73), "address glyphs 14, padding 8, gap 6");
        Check(Near(L.text.left, 36) && Near(L.text.right, 192), "address text between the lock and Copy (1Q29-0)");
        Check(L.tiles.size() == 5 && Same(L.tiles[0], 8, 92, 49.6f, 132) && Near(L.tiles[1].left, 55.6f) && Near(L.tiles[4].right, 240), "grid: five 41.6 x 40 tiles, gap 6 (1Q1Q-0)");
        Check(Same(L.line, 12, 142, 236, 143), "tabs line 1 DIP, 4 in (1Q1P-0)");
        Check(L.tabs.size() == 5 && Same(L.tabs[0], 8, 153, 240, 185) && Near(L.tabs[1].top, 187), "tab rows 32, 2 apart, first at 153 (1Q1K-0)");
        Check(Same(L.newTab, 8, 323, 240, 355), "« + Nouvel onglet » after the tabs (1Q0S-0)");
        Check(Same(L.footerLine, 8, 689, 240, 690), "tools border 49 above the bottom (1Q0B-0)");
        Check(L.tools.size() == 4 && Same(L.tools[0], 8, 698, 64.5f, 730) && Near(L.tools[1].left, 66.5f) && Near(L.tools[3].right, 240), "four tools sharing 232, gap 2 (1TLQ-0 … 1Q0C-0)");
        Check(Same(L.page, 248, 40, 1170, 730), "page 922 x 690 under the 40 DIP bar, 8 from the right and bottom (1PZT-0)");
        Check(Same(L.close, 1136, 6, 1168, 34) && Same(L.maximize, 1100, 6, 1132, 34) && Same(L.minimize, 1064, 6, 1096, 34), "window buttons 32 x 28, 4 apart, 2 in (1Q00-0)");
        Check(Near(L.drawerTop, 40), "drawers dock from y 40 (07 1TNA-0)");
        Check(L.maxScroll == 0 && Near(L.scroll, 0), "five tabs fit: no scroll");
        auto close = S::CloseIn(L.tabs[1]);
        Check(Same(close, 212, 191, 236, 215), "close 24 x 24, 4 from the row's right (1Q1D-0)");
        auto fav = S::FaviconIn(L.tabs[0], S::Mode::Complete);
        Check(Same(fav, 16, 161, 32, 177), "favicon 16, padding 8 (1Q1N-0)");
        auto title = S::TitleIn(L.tabs[0], false);
        Check(Near(title.left, 40) && Near(title.right, 232), "title after the favicon + 8, to the padding");
        Check(S::RowAt(L, 20, 160) == 0 && S::RowAt(L, 20, 330) == 5 && S::RowAt(L, 20, 186) == -1, "rows hit-test; the 2 DIP gap is no row");
    }
    // Two rows of bookmarks (answer 3) push the tabs 46 down; more than 10 keeps 10 tiles (the last is « » »).
    {
        S::Input in; in.width = W; in.height = H; in.grid = true; in.tiles = 14; in.tabs = 2;
        const auto L = S::Compute(in);
        Check(L.tiles.size() == 10 && Same(L.tiles[5], 8, 138, 49.6f, 178), "second grid row 6 under the first");
        Check(Near(L.line.top, 188), "tabs line 10 under two grid rows");
    }
    // No grid (bar « Jamais », or « Automatique » with no bookmark): the tabs come up.
    {
        S::Input in; in.width = W; in.height = H; in.grid = false; in.tabs = 1;
        const auto L = S::Compute(in);
        Check(L.tiles.empty() && Near(L.line.top, 92), "no grid: the line 10 under the address");
    }
    // Many tabs scroll inside the view (answer 4), clamped.
    {
        S::Input in; in.width = W; in.height = H; in.grid = true; in.tiles = 5; in.tabs = 40; in.scroll = 1e6f;
        const auto L = S::Compute(in);
        Check(L.maxScroll > 0 && Near(L.scroll, L.maxScroll), "scroll clamped to the content");
        Check(Near(L.newTab.bottom, L.tabsView.bottom), "at the end the « + » row meets the view's bottom");
        in.scroll = 0; const auto T = S::Compute(in);
        Check(Near(S::ScrollToShow(T, 39), T.maxScroll - (T.newTab.bottom - T.tabs[39].bottom)), "scrolling to the last tab shows it at the bottom");
        Check(Near(S::ScrollToShow(T, 0), 0), "the first tab is already shown");
    }
    // ---- 02 Compact.
    {
        S::Input in; in.mode = S::Mode::Compact; in.width = W; in.height = H; in.grid = true; in.tiles = 5; in.tabs = 5; in.tools = 4;
        const auto L = S::Compute(in);
        Check(Near(L.column, 56), "compact column 56 (1Q3T-0)");
        Check(Same(L.profile, 12, 8, 44, 40) && Same(L.toggle, 14, 46, 42, 74), "profile then the fold button, 6 apart (1Q5K-0)");
        Check(L.tiles.size() == 5 && Same(L.tiles[0], 8, 86, 48, 122) && Near(L.tiles[4].bottom, 282), "five 40 x 36 tiles, gap 4 (1Q54-0)");
        Check(Same(L.line, 8, 294, 48, 295), "tabs border 12 under the grid (1Q4A-0)");
        Check(L.tabs.size() == 5 && Same(L.tabs[0], 8, 303, 48, 339) && Near(L.tabs[1].top, 341), "tab rows 40 x 36, 2 apart, 8 under the border");
        Check(S::FaviconIn(L.tabs[0], S::Mode::Compact).left == 20, "compact favicon centred, 12 in (1Q52-0)");
        Check(L.tools.size() == 4 && Same(L.tools[3], 8, 698, 48, 730) && Near(L.tools[0].top, 590) && Same(L.footerLine, 8, 581, 48, 582), "tools 40 x 32, gap 4, at the bottom (1Q3U-0)");
        Check(!L.addressInColumn && Same(L.back, 60, 4, 92, 36) && Same(L.forward, 99, 4, 131, 36) && Same(L.reload, 133, 4, 165, 36), "compact: navigation in the window bar, 4 in, gap 2 (1Q3F-0)");
        Check(Same(L.address, 171, 6, 531, 34) && Same(L.lock, 181, 14, 193, 26) && Near(L.text.left, 199), "compact address 360 x 28, margin 6, padding 10, lock 12 (1Q3G-0)");
        Check(L.copy.Empty() && L.star.Empty(), "compact address: no Copy, no star (1Q3G-0)");
        Check(Same(L.page, 56, 40, 1170, 730), "compact page 1114 x 690 (1Q2Y-0)");
    }
    // Compact with more than five bookmarks: five, then « » » (six tiles).
    {
        S::Input in; in.mode = S::Mode::Compact; in.width = W; in.height = H; in.grid = true; in.tiles = 6; in.tabs = 1;
        Check(S::Compute(in).tiles.size() == 6, "compact: five bookmarks then « » »");
    }
    // Compact address in a narrow window stops 4 before the window buttons.
    {
        S::Input in; in.mode = S::Mode::Compact; in.width = 560; in.height = 400; in.tabs = 1;
        const auto L = S::Compute(in);
        Check(Near(L.address.right, L.minimize.left - 4) && L.address.Width() < 360, "narrow window: the address shrinks before the window buttons");
    }
    // ---- 03 The complete column over the page: the zone keeps its compact left.
    {
        S::Input in; in.mode = S::Mode::Complete; in.zoneLeft = 56; in.width = W; in.height = H; in.tabs = 3;
        const auto L = S::Compute(in);
        Check(Near(L.column, 248) && Same(L.page, 56, 40, 1170, 730), "over the page: the page does not move (1QHV-0)");
    }
    // Annotation band (answer 6): at the top of the page zone, the page 32 lower.
    {
        S::Input in; in.width = W; in.height = H; in.band = 32; in.tabs = 1;
        const auto L = S::Compute(in);
        Check(Same(L.band, 248, 40, 1170, 72) && Near(L.page.top, 72), "annotation band over the page's width, page 32 lower");
        Check(Near(L.drawerTop, 40), "drawers keep their top");
    }
    // Five tools with « Son des sites » (answer 7).
    {
        S::Input in; in.width = W; in.height = H; in.tools = 5;
        const auto L = S::Compute(in);
        Check(L.tools.size() == 5 && Near(L.tools[4].right, 240) && Near(L.tools[0].Width(), (232 - 8) / 5.f), "five tools share the width");
    }
    // Drag: drop slot follows row middles.
    {
        S::Input in; in.width = W; in.height = H; in.grid = true; in.tiles = 5; in.tabs = 5;
        const auto L = S::Compute(in);
        Check(S::DropSlot(L, 150) == 0 && S::DropSlot(L, 170) == 0 && S::DropSlot(L, 205) == 1 && S::DropSlot(L, 900) == 4, "drop slot by row middles");
    }
    // ---- Auto compact (answer 11).
    Check(S::Effective(S::Mode::Complete, 799) == S::Mode::Compact && S::Effective(S::Mode::Complete, 800) == S::Mode::Complete && S::Effective(S::Mode::Compact, 1600) == S::Mode::Compact, "compact under 800, the user's choice above");
    // ---- Fold timing (16): 180 ms width, titles 80 ms first.
    {
        S::Fold f; f.Set(S::Mode::Compact, 1000, true);
        Check(f.Moving(1000) && Near(f.ColumnWidth(1000), 248), "fold starts at 248");
        Check(Near(f.Titles(1040), .5f) && Near(f.Titles(1080), 0), "titles fade out in 80 ms");
        Check(f.ColumnWidth(1090) < 248 && f.ColumnWidth(1090) > 56, "width moving");
        Check(!f.Moving(1180) && Near(f.ColumnWidth(1180), 56), "width settled at 56 after 180 ms");
        f.Set(S::Mode::Complete, 2000, true);
        Check(Near(f.Titles(2050), 0) && Near(f.Titles(2140), .5f) && Near(f.Titles(2180), 1), "unfolding: titles come back over the last 80 ms");
        S::Fold g; g.Set(S::Mode::Compact, 0, false);
        Check(!g.Moving(0) && Near(g.ColumnWidth(0), 56) && Near(g.Titles(0), 0), "reduced motion: at once");
        S::Fold h; h.Set(S::Mode::Compact, 0, true); h.Set(S::Mode::Complete, 90, true);
        Check(h.ColumnWidth(90) < 248 && h.ColumnWidth(90) > 56 && Near(h.ColumnWidth(270), 248), "reversed mid-way: continues from where it is");
    }
    // ---- Hover (400 ms).
    {
        S::Hover v; v.Enter(0);
        Check(!v.Due(399) && v.Due(400) && Near(static_cast<float>(v.Remaining(100)), 300), "opens after 400 ms of rest");
        v.Open(); Check(v.open && !v.Due(1000), "open");
        v.Leave(); Check(!v.open, "closes when the pointer leaves");
        S::Hover w; w.Enter(0); w.Leave(); Check(!w.Due(1000), "leaving before 400 ms cancels");
        S::Hover x; x.Open(); x.holds = 1; x.Leave(); Check(x.open, "a held column (menu, drag) stays open");
    }
    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("sidebar_test: ok\n");
    return 0;
}
