// Panneau latéral (#127): geometry, fold and hover timers of src/sidebar.h against the Paper values (page « #127 — Panneau
// latéral (A finale validée) »: A finale 01-07, Val's compact retouch « Favoris · fenêtre étroite », the bookmarks grid
// B « Favoris · 10 / 20 / 40 replié / déplié / panneau 300 / 360 / compact, 40 ») and Val's 4 DIP page inset. The client
// is the artboard's inside (1180 x 740 with its 1 DIP border: 1178 x 738).
#include "../src/sidebar.h"
#include <cstdio>
#include <cmath>

namespace S = NativeChrome::Sidebar;
static int failures = 0;
static void Check(bool ok, const char* what) { if (!ok) { std::printf("FAIL %s\n", what); ++failures; } }
static bool Near(float a, float b, float e = .01f) { return std::fabs(a - b) <= e; }
static bool Same(const S::Box& b, float l, float t, float r, float bo) { return Near(b.left, l) && Near(b.top, t) && Near(b.right, r) && Near(b.bottom, bo); }
static S::Input Complete(int bookmarks, int tabs = 5) { S::Input in; in.mode = S::Mode::Complete; in.width = 1178; in.height = 738; in.grid = bookmarks >= 0; in.bookmarks = bookmarks < 0 ? 0 : bookmarks; in.tabs = tabs; in.tools = 4; return in; }
static S::Input Compact(int bookmarks, int tabs = 5) { S::Input in = Complete(bookmarks, tabs); in.mode = S::Mode::Compact; return in; }

int main() {
    // ---- 01 Complet: 5 bookmarks, 5 tabs, 4 tools; the page 4 from the right and bottom (Val).
    {
        const auto L = S::Compute(Complete(5));
        Check(Near(L.column, 248) && Near(L.bar, 40), "complete column 248, window bar 40 (1Q0A-0, 1Q00-0)");
        Check(Same(L.profile, 8, 8, 40, 40) && L.profilePlate, "profile 32 at 8,8 with its plate (1V1O-0)");
        Check(Same(L.back, 46, 8, 78, 40) && Same(L.forward, 85, 8, 117, 40) && Same(L.hairline, 81, 19, 82, 29), "Back / Forward pair (1VCX-0)");
        Check(Same(L.reload, 123, 8, 155, 40) && Same(L.toggle, 212, 10, 240, 38), "Reload and the fold button (1VD5-0, 1Q2D-0)");
        Check(L.addressInColumn && Same(L.address, 8, 50, 240, 82) && Same(L.lock, 16, 59, 30, 73) && Same(L.copy, 198, 59, 212, 73) && Same(L.star, 218, 59, 232, 73), "address in the column (1Q26-0)");
        Check(L.grid.tiles.size() == 5 && Same(L.grid.tiles[0], 8, 92, 49.6f, 132) && Near(L.grid.tiles[4].right, 240) && L.gridToggle.Empty() && Same(L.gridBlock, 8, 92, 240, 132), "5 bookmarks: one row, nothing to fold (1Q1Q-0)");
        Check(Same(L.line, 12, 142, 236, 143) && Same(L.tabs[0], 8, 153, 240, 185) && Near(L.tabs[1].top, 187) && Same(L.newTab, 8, 323, 240, 355), "tabs under the grid (1Q0R-0)");
        Check(Same(L.footerLine, 8, 689, 240, 690) && L.tools.size() == 4 && Same(L.tools[0], 8, 698, 64.5f, 730), "tools (1Q0B-0)");
        Check(Same(L.page, 248, 40, 1174, 734), "page 4 from the right and bottom (Val's inset)");
        Check(Same(L.close, 1140, 6, 1172, 34) && Same(L.maximize, 1104, 6, 1136, 34) && Same(L.minimize, 1068, 6, 1100, 34), "window buttons 2 inside the zone's 4");
        Check(Near(L.drawerTop, 40), "drawers from y 40 (07 1TNA-0)");
        Check(Same(S::CloseIn(L.tabs[1]), 212, 191, 236, 215) && Same(S::FaviconIn(L.tabs[0], S::Mode::Complete), 16, 161, 32, 177), "row close and favicon");
        Check(S::RowAt(L, 20, 160) == 0 && S::RowAt(L, 20, 330) == 5 && S::RowAt(L, 20, 186) == -1, "rows hit-test");
    }
    // An open drawer (Val, B): the page stops 4 before the drawer's column.
    {
        auto in = Complete(5); in.reserve = 268; const auto L = S::Compute(in);
        Check(Same(L.page, 248, 40, 906, 734), "drawer open: the page ends 4 before it (07 1QLM-0)");
    }
    // ---- Grid B.
    {   // 10: two rows of 5, nothing hidden, no toggle row (« Favoris · 10 »).
        const auto L = S::Compute(Complete(10));
        Check(L.grid.tiles.size() == 10 && Same(L.grid.tiles[5], 8, 138, 49.6f, 178) && L.gridToggle.Empty() && Near(L.line.top, 188), "10: two rows, no toggle (1VIT-0)");
    }
    {   // 20 folded: two rows + « Tous les favoris (20) » (24 high, 6 under the rows).
        const auto L = S::Compute(Complete(20));
        Check(L.grid.tiles.size() == 10 && !L.gridOpen && Same(L.gridToggle, 8, 184, 240, 208) && Same(L.gridBlock, 8, 92, 240, 208) && Near(L.line.top, 218), "20 folded: 10 tiles + the toggle row (1WG3-0, 1WKW-0)");
        Check(!L.grid.Scrolls() && L.grid.bar.Empty(), "folded: no scroll");
    }
    {   // 40 folded: the same as 20.
        const auto L = S::Compute(Complete(40));
        Check(L.grid.tiles.size() == 10 && Same(L.gridToggle, 8, 184, 240, 208), "40 folded like 20 (1VVH-0)");
    }
    {   // 40 unfolded: 5 rows (224) then scrolling, the « Réduire » row 6 under, the tabs follow.
        auto in = Complete(40); in.gridOpen = true; const auto L = S::Compute(in);
        Check(L.gridOpen && L.grid.tiles.size() == 40 && Same(L.grid.view, 8, 92, 240, 316) && Same(L.gridToggle, 8, 322, 240, 346) && Near(L.gridBlock.Height(), 254), "40 unfolded: 224 viewport + Réduire, 254 (1WOK-0)");
        Check(Near(L.grid.content, 8 * 40 + 7 * 6) && Near(L.grid.maxScroll, 362 - 224) && L.grid.Scrolls(), "8 rows scroll under the viewport");
        Check(Same(L.grid.bar, 237, 96, 240, 96 + 216 * 224 / 362.f) , "scroll bar 3 wide, 4 from the top, proportional (1WRC-0)");
        Check(Near(L.line.top, 356), "tabs under the unfolded grid");
        in.gridScroll = 1e6f; const auto E = S::Compute(in);
        Check(Near(E.grid.scroll, E.grid.maxScroll) && Near(E.grid.tiles.back().bottom, E.grid.view.bottom) && Near(E.grid.bar.bottom, E.grid.view.bottom - 4), "scrolled to the end");
        Check(S::TileAt(L.grid, 20, 100) == 0 && S::TileAt(L.grid, 20, 320) == -1 && S::TileAt(E.grid, 20, 100) == 15, "tiles hit-test inside the viewport only, scrolled");
    }
    {   // A short window: the tabs keep at least half of the shared height.
        auto in = Complete(40); in.gridOpen = true; in.height = 500; const auto L = S::Compute(in);
        const float shared = (L.footerLine.top - 10) - 92;
        Check(L.grid.view.Height() <= shared / 2 - 30 + .01f && L.grid.view.Height() < 224 && L.grid.Scrolls(), "short window: the grid gives way to the tabs (1VYN-0 note)");
    }
    {   // 300 and 360 wide columns: 6 then 7 per row (« panneau 300 / 360 »).
        Check(S::Columns(232) == 5 && S::Columns(284) == 6 && S::Columns(344) == 7, "about 42 DIP tiles: 5 / 6 / 7 per row (1W1T-0, 1W4Z-0)");
    }
    {   // « Toujours » with no bookmark: one full row tile.
        const auto L = S::Compute(Complete(0));
        Check(L.emptyGrid && L.tiles.size() == 1 && Same(L.tiles[0], 8, 92, 240, 132), "empty grid: one full-width tile");
        const auto N = S::Compute(Complete(-1, 1));
        Check(N.gridBlock.Empty() && Near(N.line.top, 92), "no grid: the tabs come up");
    }
    {   // Many tabs scroll inside the view (answer 4).
        auto in = Complete(5, 40); in.scroll = 1e6f; const auto L = S::Compute(in);
        Check(L.maxScroll > 0 && Near(L.scroll, L.maxScroll) && Near(L.newTab.bottom, L.tabsView.bottom), "tabs scroll, clamped");
        in.scroll = 0; const auto T = S::Compute(in);
        Check(Near(S::ScrollToShow(T, 0), 0) && S::ScrollToShow(T, 39) > 0, "scroll to a tab");
    }
    // ---- Compact (Val's retouch, reference « Favoris · fenêtre étroite » 1WC7-0).
    {
        const auto L = S::Compute(Compact(5));
        Check(Near(L.column, 40) && Near(L.bar, 32), "compact column 40, window bar 32 (1WC7-0, 1WBH-0)");
        Check(Same(L.profile, 4, 4, 36, 30) && !L.profilePlate && Same(L.toggle, 6, 36, 34, 64), "profile 32 x 26 without its plate, the fold button 28 (1WDY-0)");
        Check(L.tiles.size() == 5 && !L.more && Same(L.tiles[0], 0, 76, 40, 112) && Near(L.tiles[4].bottom, 272), "five 40 x 36 tiles, gap 4 (1WDI-0)");
        Check(Same(L.line, 0, 284, 40, 285) && Same(L.tabs[0], 0, 293, 40, 329) && Near(L.tabs[1].top, 331), "tabs 12 under the grid (1WCO-0)");
        Check(L.tools.size() == 4 && Same(L.footerLine, 0, 585, 40, 586) && Same(L.tools[0], 0, 594, 40, 626) && Same(L.tools[3], 0, 702, 40, 734), "tools 40 x 32, gap 4 (1WC8-0)");
        Check(!L.navPlates && Same(L.back, 44, 0, 76, 32) && Same(L.hairline, 79, 11, 80, 21) && Same(L.forward, 83, 0, 115, 32) && Same(L.reload, 121, 0, 153, 32), "window bar: the pair and Reload, 6 apart, no plates (1WBR-0)");
        Check(!L.addressInColumn && !L.addressPlate && Same(L.address, 159, 0, 519, 32) && Same(L.lock, 169, 10, 181, 22) && Near(L.text.left, 187) && L.copy.Empty(), "address 360 x 32 without background (1WOG-0)");
        Check(Same(L.page, 40, 32, 1174, 734) && Same(L.close, 1140, 2, 1172, 30), "compact page under the 32 DIP bar, 4 in (1WBA-0)");
        Check(Near(L.drawerTop, 25), "compact drawers from 25 (11 1TSJ-0)");
        Check(Near(S::FaviconIn(L.tabs[0], S::Mode::Compact).left, 12), "favicon centred");
    }
    {   // More than five: four, then « » » (« compact, 40 »), and its popover with all of them.
        const auto L = S::Compute(Compact(40));
        Check(L.tiles.size() == 5 && L.more, "compact 40: 4 tiles + « » » (1WAI-0)");
        const auto P = S::ComputePopover(L.column, L.tiles[4], 40, 0, 738);
        Check(Same(P.box, 44, 236, 292, 236 + 2 + 16 + 224) && P.grid.tiles.size() == 40 && Near(P.grid.view.Width(), 230) && Near(P.grid.view.Height(), 224) && P.grid.Scrolls(), "popover 248, 4 right of the column, top on « » », 5 rows then scroll (1WSC-0)");
        const auto low = S::ComputePopover(L.column, S::Box{0, 700, 40, 736}, 40, 0, 738);
        Check(Near(low.box.bottom, 734), "popover kept inside the window");
        Check(S::Compute(Compact(6)).more && !S::Compute(Compact(5)).more && S::Compute(Compact(3)).tiles.size() == 3, "« » » only beyond five");
    }
    {   // Narrow compact address stops before the window buttons.
        auto in = Compact(5); in.width = 560; in.height = 400; const auto L = S::Compute(in);
        Check(Near(L.address.right, L.minimize.left - 4) && L.address.Width() < 360, "narrow window: the address shrinks");
    }
    // ---- 03: the complete column over the compact page: the zone keeps its compact bar and left.
    {
        auto in = Complete(5, 3); in.zoneLeft = 40; in.zoneSet = true; in.zoneMode = S::Mode::Compact; const auto L = S::Compute(in);
        Check(Near(L.column, 248) && Same(L.page, 40, 32, 1174, 734) && Near(L.bar, 32), "over the page: the page does not move (1QHV-0)");
    }
    // Annotation band (answer 6): at the top of the page zone, the page 32 lower.
    {
        auto in = Complete(5, 1); in.band = 32; const auto L = S::Compute(in);
        Check(Same(L.band, 248, 40, 1174, 72) && Near(L.page.top, 72), "annotation band over the page's width");
    }
    // Five tools with « Son des sites » (answer 7).
    {
        auto in = Complete(5); in.tools = 5; const auto L = S::Compute(in);
        Check(L.tools.size() == 5 && Near(L.tools[4].right, 240) && Near(L.tools[0].Width(), (232 - 8) / 5.f), "five tools share the width");
    }
    // Drag: drop slot follows row middles.
    {
        const auto L = S::Compute(Complete(5));
        Check(S::DropSlot(L, 150) == 0 && S::DropSlot(L, 170) == 0 && S::DropSlot(L, 205) == 1 && S::DropSlot(L, 900) == 4, "drop slot by row middles");
    }
    // ---- Auto compact (answer 11): the complete column (248) still leaves the page about 560 at 800; the compact
    // column's new width (40) does not move the threshold.
    Check(S::Effective(S::Mode::Complete, 799) == S::Mode::Compact && S::Effective(S::Mode::Complete, 800) == S::Mode::Complete && S::Effective(S::Mode::Compact, 1600) == S::Mode::Compact, "compact under 800, the user's choice above");
    // ---- Fold timing (16): 180 ms width, titles 80 ms first.
    {
        S::Fold f; f.Set(S::Mode::Compact, 1000, true);
        Check(f.Moving(1000) && Near(f.ColumnWidth(1000), 248), "fold starts at 248");
        Check(Near(f.Titles(1040), .5f) && Near(f.Titles(1080), 0), "titles fade out in 80 ms");
        Check(!f.Moving(1180) && Near(f.ColumnWidth(1180), 40), "width settled at 40 after 180 ms");
        f.Set(S::Mode::Complete, 2000, true);
        Check(Near(f.Titles(2050), 0) && Near(f.Titles(2140), .5f) && Near(f.Titles(2180), 1), "unfolding: titles come back over the last 80 ms");
        S::Fold g; g.Set(S::Mode::Compact, 0, false);
        Check(!g.Moving(0) && Near(g.ColumnWidth(0), 40) && Near(g.Titles(0), 0), "reduced motion: at once");
    }
    // ---- Hover (400 ms).
    {
        S::Hover v; v.Enter(0);
        Check(!v.Due(399) && v.Due(400), "opens after 400 ms of rest");
        v.Open(); v.Leave(); Check(!v.open, "closes when the pointer leaves");
        S::Hover x; x.Open(); x.holds = 1; x.Leave(); Check(x.open, "a held column stays open");
    }
    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("sidebar_test: ok\n");
    return 0;
}
