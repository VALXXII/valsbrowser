#pragma once
// Panneau latéral, variante A finale (#127; Paper « Browser Design », page « #127 — Panneau latéral (A finale validée) »
// p-X-0, planches « A finale 01 » à « 16 »; design/127-sidebar-plan.md). One column on the left replaces the tab row
// and the toolbar: profile, Back / Forward / Reload, the fold button, the address, the bookmarks grid, the tabs and
// the tools that open the real drawers. Two modes, complete (248) and compact (56, icons only).
// Pure C++17 (no Win32): the Paper values, the geometry both modes share with the hit test, the page box, the drawers'
// top, the fold animation and the hover-to-open timer. Unit-tested in tests/sidebar_test.cpp; drawn by Chrome
// (src/chrome.h, BuildSidebar / PaintSidebar), bound to the window in src/main.cpp (section « Panneau latéral »).
#include <algorithm>
#include <cmath>
#include <vector>

namespace NativeChrome { namespace Sidebar {

struct Box {
    float left = 0, top = 0, right = 0, bottom = 0;
    float Width() const { return right - left; }
    float Height() const { return bottom - top; }
    bool Empty() const { return right <= left || bottom <= top; }
    bool Contains(float x, float y) const { return x >= left && x < right && y >= top && y < bottom; }
};
inline Box Centered(float cx, float cy, float w, float h) { return {cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2}; }

enum class Mode { Complete, Compact };

// ---- Paper values (DIP) -------------------------------------------------------------------------------------
// Page zone (01 « Zone page » 1PZS-0): 8 on its right and bottom. « Barre fenêtre » 1Q00-0: 40 high, the window
// buttons 32 x 28 (1Q07-0 / 1Q04-0 / 1Q01-0), 4 apart, 2 from the zone's right edge, glyph 12 drawn with a 1.8 / 24
// stroke in white 70 % (#FFFFFFB3). « Page web » 1PZT-0: the page, its four corners 8.
namespace Zone {
inline constexpr float inset = 8, bar = 40, buttonW = 32, buttonH = 28, buttonGap = 4, buttonRight = 2;
inline constexpr float glyph = 12, glyphStroke = 1.8f * glyph / 24, glyphAlpha = 0xB3 / 255.f, pageRadius = 8;
}
// Complete column (01 « Panneau » 1Q0A-0): 248, padding 8, gap 10.
namespace Complete {
inline constexpr float width = 248, pad = 8, gap = 10;
// « Haut » 1Q2C-0: 32 high, gap 6: profile 32 (1V1O-0), the Back / Forward pair (1VCX-0), Reload (1VD5-0), a
// spacer, the fold button 28 (1Q2D-0, radius 6, glyph 16 white 70 %).
inline constexpr float row = 32, rowGap = 6, toggle = 28;
// « Adresse » 1Q26-0: 32 high, #FFFFFF14, radius 6, padding 8, gap 6: lock 14 (1Q2A-0), text 16/20 -0.01 em
// (1Q29-0), Copy 14 (1V38-0), star 14 (1Q27-0), glyphs white 50 %.
inline constexpr float address = 32, addressPad = 8, addressGap = 6, addressGlyph = 14, addressFont = 16;
// « Favoris — grille » 1Q1Q-0: tiles 40 high, radius 8, #FFFFFF0A (hover #FFFFFF14, 04 1QAY-0), gap 6, five per row,
// favicon 18. At most two rows (Designer, answer 3): the tenth tile becomes « » » when there are more.
inline constexpr float tile = 40, tileGap = 6;
inline constexpr int columns = 5, maxRows = 2;
// « Onglets » 1Q0R-0: a 1 DIP #FFFFFF0F line 4 in from each side and 8 above the rows (1Q1P-0); rows 32, radius 6,
// padding 8 (4 on the right with the close button), gap 8, favicon 16, title 14/18; 2 between rows.
inline constexpr float lineInset = 4, lineAfter = 8, tabRow = 32, tabGap = 2, tabPad = 8, tabIconGap = 8, favicon = 16;
inline constexpr float close = 24, closePadRight = 4, closeGlyph = 12;
// « Outils (tiroirs) » 1Q0B-0: a 1 DIP #FFFFFF0F top border, padding-top 8, buttons 32 high sharing the width, gap 2.
inline constexpr float toolsLead = 1 + 8, tool = 32, toolGap = 2;
}
// Compact column (02 « Panneau » 1Q3T-0): 56, padding 8, gap 12, items centred (40 wide).
namespace Compact {
inline constexpr float width = 56, pad = 8, gap = 12, item = 40;
// « Haut » 1Q5K-0: profile 32, gap 6, the fold button 28.
inline constexpr float profile = 32, topGap = 6, toggle = 28;
// « Favoris — grille » 1Q54-0: tiles 40 x 36, radius 8, #FFFFFF0F, gap 4; the first five, then « » » (answer 3).
inline constexpr float tile = 36, tileGap = 4;
inline constexpr int tiles = 5;
// « Onglets » 1Q4A-0: a 1 DIP #FFFFFF0F top border, padding-top 8, rows 40 x 36, radius 6, favicon 16 at 12, gap 2.
inline constexpr float tabsLead = 1 + 8, tabRow = 36, tabGap = 2;
// « Outils (tiroirs) » 1Q3U-0: a 1 DIP #FFFFFF0F top border, padding-top 8, buttons 40 x 32, gap 4.
inline constexpr float toolsLead = 1 + 8, tool = 32, toolGap = 4;
// « Barre fenêtre » 1Q35-0: padding-left 4. « Navigation + adresse » 1Q3F-0: gap 2 (pair, Reload), then the address
// 1Q3G-0: 360 x 28, margin-left 6, #FFFFFF14, radius 6, padding 10, gap 6, lock 12, text 14/18 -0.01 em.
inline constexpr float barPad = 4, navGap = 2, addressLead = 6, addressW = 360, addressH = 28, addressPad = 10, addressGap = 6, addressGlyph = 12, addressFont = 14;
}
// Back / Forward (1VCX-0): two 32 halves 3 from a 1 x 10 hairline (white 10 %); Reload (1VD5-0) 32.
namespace Nav {
inline constexpr float button = 32, pairGap = 3, hairline = 1, hairlineH = 10;
inline constexpr float pair = button * 2 + pairGap * 2 + hairline;   // 71
}
static_assert(Nav::pair == 71, "Paper 1VCX-0: the pair is 71 wide");
// The complete column over the page (03 « Panneau déplié par-dessus » 1QHV-0): + a 1 DIP right border white 12 %
// (#FFFFFF1F), right corners 12, the window's whole height.
namespace Overlay { inline constexpr float border = 1, borderAlpha = 0x1F / 255.f, radius = 12; }
// Rows and tiles (01, 02, 04, 05): fills and text opacities.
namespace Look {
inline constexpr float tabActive = 0x3E / 255.f;                    // --color-browser-pressed #3E3E3E (1Q1K-0)
inline constexpr float tabHover = 0x0A / 255.f;                     // white 4 % (1Q1C-0)
inline constexpr float titleActive = 1, titleHover = 0xE6 / 255.f, titleRest = 0xB3 / 255.f, newTab = 0x80 / 255.f;
inline constexpr float tileRest = 0x0A / 255.f, tileHover = 0x14 / 255.f, compactTile = 0x0F / 255.f;
inline constexpr float line = 0x0F / 255.f;                         // separators (1Q1P-0, 1Q0B-0 border)
inline constexpr float toolHover = 0x0A / 255.f, toolOpen = 0x1A / 255.f;   // 04 1Q9J-0, 07 1QMH-0
inline constexpr float glyph = 0xB3 / 255.f, glyphMuted = 0x80 / 255.f, settingsGlyph = .5f;   // 1Q0D-0 opacity .5
inline constexpr float field = 0x14 / 255.f, fieldEditing = 0x0A / 255.f, fieldRing = 0x80 / 255.f;   // 06 1QH8-0
inline constexpr float dragged = 0x29 / 255.f, draggedBorder = 0x26 / 255.f, origin = .3f, insertion = 2;   // 05
}
// Motion (16 « Valeurs »; 03 note): the width moves in 180 ms, titles fade first in 80 ms; the pointer rests 400 ms
// on the compact column (or the Focus edge) before the complete column opens over the page.
namespace Timing { inline constexpr double widthMs = 180, titlesMs = 80, hoverMs = 400; }
// Below this window width the column is compact by itself (Designer, answer 11): the page keeps about 560. The
// user's choice is never overwritten; above it, the column comes back to that choice.
inline constexpr float autoCompactBelow = 800;
inline Mode Effective(Mode chosen, float windowWidth) { return windowWidth < autoCompactBelow ? Mode::Compact : chosen; }
inline float Width(Mode m) { return m == Mode::Complete ? Complete::width : Compact::width; }

// Glyphs exported by Paper (viewBox 256, Phosphor Bold) where the column needs a size or an opacity the 16 DIP
// 50 % icon bitmaps do not have: the fold button 1Q2E-0, the tools 1Q0M-0 / 1Q0P-0 / 1Q0D-0, the tab close 1Q1E-0,
// the « + » 1Q0V-0, the address lock 1Q2A-0, Copy 1V38-0 and the star 1Q27-0 (14 DIP).
namespace Glyph {
inline constexpr const char* fold = "M216,36H40A20,20,0,0,0,20,56V200a20,20,0,0,0,20,20H216a20,20,0,0,0,20-20V56A20,20,0,0,0,216,36ZM44,60H84V196H44ZM212,196H108V60H212Z";
inline constexpr const char* history = "M140,80v41.21l34.17,20.5a12,12,0,1,1-12.34,20.58l-40-24A12,12,0,0,1,116,128V80a12,12,0,0,1,24,0ZM128,28A99.38,99.38,0,0,0,57.24,57.34c-4.69,4.74-9,9.37-13.24,14V64a12,12,0,0,0-24,0v40a12,12,0,0,0,12,12H72a12,12,0,0,0,0-24H57.77C63,86,68.37,80.22,74.26,74.26a76,76,0,1,1,1.58,109,12,12,0,0,0-16.48,17.46A100,100,0,1,0,128,28Z";
inline constexpr const char* downloads = "M228,144v64a12,12,0,0,1-12,12H40a12,12,0,0,1-12-12V144a12,12,0,0,1,24,0v52H204V144a12,12,0,0,1,24,0Zm-108.49,8.49a12,12,0,0,0,17,0l40-40a12,12,0,0,0-17-17L140,115V32a12,12,0,0,0-24,0v83L96.49,95.51a12,12,0,0,0-17,17Z";
inline constexpr const char* settings = "M128,76a52,52,0,1,0,52,52A52.06,52.06,0,0,0,128,76Zm0,80a28,28,0,1,1,28-28A28,28,0,0,1,128,156Zm113.86-49.57A12,12,0,0,0,236,98.34L208.21,82.49l-.11-31.31a12,12,0,0,0-4.25-9.12,116,116,0,0,0-38-21.41,12,12,0,0,0-9.68.89L128,37.27,99.83,21.53a12,12,0,0,0-9.7-.9,116.06,116.06,0,0,0-38,21.47,12,12,0,0,0-4.24,9.1l-.14,31.34L20,98.35a12,12,0,0,0-5.85,8.11,110.7,110.7,0,0,0,0,43.11A12,12,0,0,0,20,157.66l27.82,15.85.11,31.31a12,12,0,0,0,4.25,9.12,116,116,0,0,0,38,21.41,12,12,0,0,0,9.68-.89L128,218.73l28.14,15.74a12,12,0,0,0,9.7.9,116.06,116.06,0,0,0,38-21.47,12,12,0,0,0,4.24-9.1l.14-31.34,27.81-15.81a12,12,0,0,0,5.85-8.11A110.7,110.7,0,0,0,241.86,106.43Zm-22.63,33.18-26.88,15.28a11.94,11.94,0,0,0-4.55,4.59c-.54,1-1.11,1.93-1.7,2.88a12,12,0,0,0-1.83,6.31L184.13,199a91.83,91.83,0,0,1-21.07,11.87l-27.15-15.19a12,12,0,0,0-5.86-1.53h-.29c-1.14,0-2.3,0-3.44,0a12.08,12.08,0,0,0-6.14,1.51L93,210.82A92.27,92.27,0,0,1,71.88,199l-.11-30.24a12,12,0,0,0-1.83-6.32c-.58-.94-1.16-1.91-1.7-2.88A11.92,11.92,0,0,0,63.7,155L36.8,139.63a86.53,86.53,0,0,1,0-23.24l26.88-15.28a12,12,0,0,0,4.55-4.58c.54-1,1.11-1.94,1.7-2.89a12,12,0,0,0,1.83-6.31L71.87,57A91.83,91.83,0,0,1,92.94,45.17l27.15,15.19a11.92,11.92,0,0,0,6.15,1.52c1.14,0,2.3,0,3.44,0a12.08,12.08,0,0,0,6.14-1.51L163,45.18A92.27,92.27,0,0,1,184.12,57l.11,30.24a12,12,0,0,0,1.83,6.32c.58.94,1.16,1.91,1.7,2.88A11.92,11.92,0,0,0,192.3,101l26.9,15.33A86.53,86.53,0,0,1,219.23,139.61Z";
inline constexpr const char* close = "M208.49,191.51a12,12,0,0,1-17,17L128,145,64.49,208.49a12,12,0,0,1-17-17L111,128,47.51,64.49a12,12,0,0,1,17-17L128,111l63.51-63.52a12,12,0,0,1,17,17L145,128Z";
inline constexpr const char* plus = "M228,128a12,12,0,0,1-12,12H140v76a12,12,0,0,1-24,0V140H40a12,12,0,0,1,0-24h76V40a12,12,0,0,1,24,0v76h76A12,12,0,0,1,228,128Z";
inline constexpr const char* copy = "M117.18,188.74a12,12,0,0,1,0,17l-5.12,5.12A58.26,58.26,0,0,1,70.6,228h0A58.62,58.62,0,0,1,29.14,127.92L63.89,93.17a58.64,58.64,0,0,1,98.56,28.11,12,12,0,1,1-23.37,5.44,34.65,34.65,0,0,0-58.22-16.58L46.11,144.89A34.62,34.62,0,0,0,70.57,204h0a34.41,34.41,0,0,0,24.49-10.14l5.11-5.12A12,12,0,0,1,117.18,188.74ZM226.83,45.17a58.65,58.65,0,0,0-82.93,0l-5.11,5.11a12,12,0,0,0,17,17l5.12-5.12a34.63,34.63,0,1,1,49,49L175.1,145.86A34.39,34.39,0,0,1,150.61,156h0a34.63,34.63,0,0,1-33.69-26.72,12,12,0,0,0-23.38,5.44A58.64,58.64,0,0,0,150.56,180h.05a58.28,58.28,0,0,0,41.47-17.17l34.75-34.75a58.62,58.62,0,0,0,0-82.91Z";
inline constexpr const char* star = "M243,96a20.33,20.33,0,0,0-17.74-14l-56.59-4.57L146.83,24.62a20.36,20.36,0,0,0-37.66,0L87.35,77.44,30.76,82A20.45,20.45,0,0,0,19.1,117.88l43.18,37.24-13.2,55.7A20.37,20.37,0,0,0,79.57,233L128,203.19,176.43,233a20.39,20.39,0,0,0,30.49-22.15l-13.2-55.7,43.18-37.24A20.43,20.43,0,0,0,243,96Z";
}

// ---- Geometry ------------------------------------------------------------------------------------------------
struct Input {
    Mode mode = Mode::Complete;   // the column's layout (complete also for the column opened over the page)
    float zoneLeft = -1;          // left of the page zone; -1: the column's width (docked)
    float width = 1180, height = 740;
    int tiles = 0;                // grid tiles (bookmarks shown, « » » included); 0 with `grid` = the empty tile
    bool grid = false;            // the grid row exists
    int tabs = 0;                 // tab rows (the « + Nouvel onglet » row comes after them)
    int tools = 4;                // footer buttons
    float scroll = 0;             // tab list scroll (clamped)
    float band = 0;               // annotation band at the top of the page zone (answer 6)
};
struct Layout {
    Mode mode = Mode::Complete;
    float column = 0;             // the column's width
    Box profile, back, forward, hairline, reload, toggle;
    // The address field: in the column (complete) or in the window bar (compact). `copy` / `star` are empty in compact.
    bool addressInColumn = true;
    Box address, lock, text, copy, star;
    float addressFont = 16, addressGlyph = 14;
    Box grid;                     // the grid's rows (empty without a grid)
    std::vector<Box> tiles;
    Box line;                     // the line above the tabs (complete: inset; compact: the section's border)
    Box tabsView;                 // tab rows are clipped to it and scroll under it
    std::vector<Box> tabs;        // at the current scroll
    Box newTab;
    float content = 0, maxScroll = 0, scroll = 0;
    Box footerLine;
    std::vector<Box> tools;
    Box minimize, maximize, close;
    Box zone, band, page;
    float drawerTop = Zone::bar;  // the real drawers dock at the right from here (07: top 40)
};
// The close target in a complete tab row (1Q1D-0: 24 x 24, 4 from the row's right edge).
inline Box CloseIn(const Box& row) { const float r = row.right - Complete::closePadRight, cy = (row.top + row.bottom) / 2; return {r - Complete::close, cy - Complete::close / 2, r, cy + Complete::close / 2}; }
// Favicon slot of a row (complete: 8 in; compact: centred, 12 in).
inline Box FaviconIn(const Box& row, Mode m) {
    const float cy = (row.top + row.bottom) / 2, s = Complete::favicon;
    const float l = m == Mode::Complete ? row.left + Complete::tabPad : (row.left + row.right - s) / 2;
    return {l, cy - s / 2, l + s, cy + s / 2};
}
// Title slot of a complete row: after the favicon, to the padding (or the close button when it shows).
inline Box TitleIn(const Box& row, bool closeShown) {
    const float l = row.left + Complete::tabPad + Complete::favicon + Complete::tabIconGap;
    const float r = closeShown ? CloseIn(row).left - Complete::tabIconGap : row.right - Complete::tabPad;
    return {l, row.top, std::max(l, r), row.bottom};
}

inline Layout Compute(const Input& in) {
    Layout L; L.mode = in.mode; const bool full = in.mode == Mode::Complete;
    const float W = std::max(in.width, 1.f), H = std::max(in.height, 1.f);
    L.column = Width(in.mode);
    const float zoneLeft = in.zoneLeft >= 0 ? in.zoneLeft : L.column;
    // Page zone: window buttons right-aligned in the 40 DIP bar, the page under it (and under the annotation band).
    {
        namespace Z = Zone;
        const float right = W - Z::inset - Z::buttonRight, cy = Z::bar / 2;
        L.close = Centered(right - Z::buttonW / 2, cy, Z::buttonW, Z::buttonH);
        L.maximize = Centered(L.close.left - Z::buttonGap - Z::buttonW / 2, cy, Z::buttonW, Z::buttonH);
        L.minimize = Centered(L.maximize.left - Z::buttonGap - Z::buttonW / 2, cy, Z::buttonW, Z::buttonH);
        L.zone = {zoneLeft, 0, W, H};
        const float pageRight = std::max(zoneLeft, W - Z::inset), pageBottom = std::max(Z::bar, H - Z::inset);
        L.band = in.band > 0 ? Box{zoneLeft, Z::bar, pageRight, Z::bar + in.band} : Box{};
        L.page = {zoneLeft, Z::bar + in.band, pageRight, std::max(Z::bar + in.band, pageBottom)};
        L.drawerTop = Z::bar;
    }
    auto pairAt = [&](float x, float top) {
        L.back = {x, top, x + Nav::button, top + Nav::button};
        const float h = x + Nav::button + Nav::pairGap, cy = top + Nav::button / 2;
        L.hairline = {h, cy - Nav::hairlineH / 2, h + Nav::hairline, cy + Nav::hairlineH / 2};
        L.forward = {h + Nav::hairline + Nav::pairGap, top, h + Nav::hairline + Nav::pairGap + Nav::button, top + Nav::button};
    };
    if (full) {
        namespace C = Complete;
        const float l = C::pad, r = C::width - C::pad; float y = C::pad;
        // Top row.
        L.profile = {l, y, l + C::row, y + C::row};
        pairAt(L.profile.right + C::rowGap, y);
        L.reload = {L.forward.right + C::rowGap, y, L.forward.right + C::rowGap + Nav::button, y + Nav::button};
        L.toggle = Centered(r - C::toggle / 2, y + C::row / 2, C::toggle, C::toggle);
        y += C::row + C::gap;
        // Address.
        L.addressInColumn = true; L.addressFont = C::addressFont; L.addressGlyph = C::addressGlyph;
        L.address = {l, y, r, y + C::address};
        const float cy = y + C::address / 2, g = C::addressGlyph;
        L.lock = Centered(l + C::addressPad + g / 2, cy, g, g);
        L.star = Centered(r - C::addressPad - g / 2, cy, g, g);
        L.copy = Centered(L.star.left - C::addressGap - g / 2, cy, g, g);
        L.text = {L.lock.right + C::addressGap, y, L.copy.left - C::addressGap, y + C::address};
        y += C::address + C::gap;
        // Grid: five columns (fixed: a short row keeps the 5-column lane), at most two rows.
        if (in.grid) {
            const int n = std::max(0, std::min(in.tiles, C::columns * C::maxRows));
            const int rows = n == 0 ? 1 : (n + C::columns - 1) / C::columns;
            const float w = (r - l - C::tileGap * (C::columns - 1)) / C::columns;
            for (int i = 0; i < n; ++i) {
                const int c = i % C::columns, row = i / C::columns;
                const float x = l + c * (w + C::tileGap), t = y + row * (C::tile + C::tileGap);
                L.tiles.push_back({x, t, x + w, t + C::tile});
            }
            L.grid = {l, y, r, y + rows * C::tile + (rows - 1) * C::tileGap};
            y = L.grid.bottom + C::gap;
        }
        // Footer.
        const int tools = std::max(1, in.tools);
        const float footer = H - C::pad - (C::toolsLead + C::tool);
        L.footerLine = {l, footer, r, footer + 1};
        const float tw = (r - l - C::toolGap * (tools - 1)) / tools;
        for (int i = 0; i < tools; ++i) { const float x = l + i * (tw + C::toolGap); L.tools.push_back({x, footer + C::toolsLead, x + tw, footer + C::toolsLead + C::tool}); }
        // Tabs: the line, then the rows (2 apart, the first 2 under the line's margin), scrolled inside the view.
        L.line = {l + C::lineInset, y, r - C::lineInset, y + 1};
        L.tabsView = {l, y + 1 + C::lineAfter, r, std::max(y + 1 + C::lineAfter, footer - C::gap)};
        const float pitch = C::tabRow + C::tabGap, lead = C::tabGap;
        L.content = lead + (in.tabs + 1) * pitch - C::tabGap;
        L.maxScroll = std::max(0.f, L.content - L.tabsView.Height());
        L.scroll = std::clamp(in.scroll, 0.f, L.maxScroll);
        for (int i = 0; i <= in.tabs; ++i) {
            const float t = L.tabsView.top + lead + i * pitch - L.scroll;
            const Box row{l, t, r, t + C::tabRow};
            if (i < in.tabs) L.tabs.push_back(row); else L.newTab = row;
        }
    } else {
        namespace K = Compact;
        const float cx = K::width / 2, l = cx - K::item / 2, r = cx + K::item / 2; float y = K::pad;
        L.profile = Centered(cx, y + K::profile / 2, K::profile, K::profile);
        y += K::profile + K::topGap;
        L.toggle = Centered(cx, y + K::toggle / 2, K::toggle, K::toggle);
        y += K::toggle + K::gap;
        if (in.grid) {
            const int n = std::max(1, std::min(in.tiles, K::tiles + 1));
            for (int i = 0; i < n; ++i) { const float t = y + i * (K::tile + K::tileGap); L.tiles.push_back({l, t, r, t + K::tile}); }
            if (in.tiles <= 0) L.tiles.resize(1);
            L.grid = {l, y, r, L.tiles.back().bottom};
            y = L.grid.bottom + K::gap;
        }
        const int tools = std::max(1, in.tools);
        const float toolsH = K::toolsLead + tools * K::tool + (tools - 1) * K::toolGap, footer = H - K::pad - toolsH;
        L.footerLine = {l, footer, r, footer + 1};
        for (int i = 0; i < tools; ++i) { const float t = footer + K::toolsLead + i * (K::tool + K::toolGap); L.tools.push_back({l, t, r, t + K::tool}); }
        L.line = {l, y, r, y + 1};
        L.tabsView = {l, y + 1, r, std::max(y + 1, footer - K::gap)};
        const float pitch = K::tabRow + K::tabGap, lead = K::tabsLead - 1;
        L.content = lead + (in.tabs + 1) * pitch - K::tabGap;
        L.maxScroll = std::max(0.f, L.content - L.tabsView.Height());
        L.scroll = std::clamp(in.scroll, 0.f, L.maxScroll);
        for (int i = 0; i <= in.tabs; ++i) {
            const float t = L.tabsView.top + lead + i * pitch - L.scroll;
            const Box row{l, t, r, t + K::tabRow};
            if (i < in.tabs) L.tabs.push_back(row); else L.newTab = row;
        }
        // Window bar: the pair, Reload and the address after the zone's 4 DIP padding.
        const float top = (Zone::bar - Nav::button) / 2;
        pairAt(zoneLeft + K::barPad, top);
        L.reload = {L.forward.right + K::navGap, top, L.forward.right + K::navGap + Nav::button, top + Nav::button};
        L.addressInColumn = false; L.addressFont = K::addressFont; L.addressGlyph = K::addressGlyph;
        const float al = L.reload.right + K::addressLead, ar = std::max(al, std::min(al + K::addressW, L.minimize.left - Zone::buttonGap));
        L.address = Centered((al + ar) / 2, Zone::bar / 2, ar - al, K::addressH);
        const float acy = Zone::bar / 2, g = K::addressGlyph;
        L.lock = Centered(al + K::addressPad + g / 2, acy, g, g);
        L.text = {L.lock.right + K::addressGap, L.address.top, std::max(L.lock.right + K::addressGap, ar - K::addressPad), L.address.bottom};
    }
    return L;
}

// Row index under (x, y): a tab (0..tabs-1), `tabs` for the « + » row, -1 elsewhere (rows clipped by the view).
inline int RowAt(const Layout& L, float x, float y) {
    if (!L.tabsView.Contains(x, y)) return -1;
    for (int i = 0; i < static_cast<int>(L.tabs.size()); ++i) if (L.tabs[i].Contains(x, y)) return i;
    return L.newTab.Contains(x, y) ? static_cast<int>(L.tabs.size()) : -1;
}
// Drop slot while dragging a tab vertically: the row whose middle the pointer passed (0..tabs-1).
inline int DropSlot(const Layout& L, float y) {
    const int n = static_cast<int>(L.tabs.size());
    if (n <= 0) return 0;
    int slot = 0;
    for (int i = 0; i < n; ++i) if (y >= (L.tabs[i].top + L.tabs[i].bottom) / 2) slot = i;
    return slot;
}
// Scroll that brings row `i` fully into the view (unchanged when it already is).
inline float ScrollToShow(const Layout& L, int i) {
    if (i < 0 || i >= static_cast<int>(L.tabs.size())) return L.scroll;
    const Box& r = L.tabs[i]; float s = L.scroll;
    if (r.top < L.tabsView.top) s -= L.tabsView.top - r.top;
    else if (r.bottom > L.tabsView.bottom) s += r.bottom - L.tabsView.bottom;
    return std::clamp(s, 0.f, L.maxScroll);
}

// ---- Fold animation --------------------------------------------------------------------------------------------
// t = 1 complete, 0 compact. Folding, the titles fade out over the first 80 ms while the width eases over 180 ms;
// unfolding, the width eases over 180 ms and the titles come back over the last 80 ms. Reduced motion: at once.
inline double EaseOut(double x) { x = std::clamp(x, 0.0, 1.0); const double u = 1 - x; return 1 - u * u * u; }
struct Fold {
    Mode target = Mode::Complete; double startMs = -1; float from = 1;
    float Settled() const { return target == Mode::Complete ? 1.f : 0.f; }
    bool Moving(double now) const { return startMs >= 0 && now - startMs < Timing::widthMs; }
    // Width progress (0 compact .. 1 complete).
    float Progress(double now) const {
        if (startMs < 0 || now - startMs >= Timing::widthMs) return Settled();
        const float e = static_cast<float>(EaseOut((now - startMs) / Timing::widthMs));
        return from + (Settled() - from) * e;
    }
    float ColumnWidth(double now) const { const float p = Progress(now); return Compact::width + (Complete::width - Compact::width) * p; }
    // Opacity of the complete column's titles and labels.
    float Titles(double now) const {
        if (startMs < 0 || now - startMs >= Timing::widthMs) return target == Mode::Complete ? 1.f : 0.f;
        const double t = now - startMs;
        if (target == Mode::Compact) return static_cast<float>(1 - std::clamp(t / Timing::titlesMs, 0.0, 1.0)) * std::min(1.f, from * 4);
        return static_cast<float>(std::clamp((t - (Timing::widthMs - Timing::titlesMs)) / Timing::titlesMs, 0.0, 1.0));
    }
    void Set(Mode m, double now, bool animate) {
        if (m == target && startMs < 0) return;
        from = animate ? Progress(now) : (m == Mode::Complete ? 1.f : 0.f);
        target = m; startMs = animate ? now : -1;
    }
    void Settle(double now) { if (startMs >= 0 && now - startMs >= Timing::widthMs) startMs = -1; }
};

// ---- Hover to open (compact; Focus later) -------------------------------------------------------------------------
// The pointer resting 400 ms on the compact column opens the complete column over the page; it closes as soon as the
// pointer leaves the opened column. A press, a drag or a menu keeps it open while it lasts (`Hold`).
struct Hover {
    double enteredMs = -1; bool open = false; int holds = 0;
    void Enter(double now) { if (enteredMs < 0 && !open) enteredMs = now; }
    void Leave() { enteredMs = -1; if (holds <= 0) open = false; }
    bool Due(double now) const { return !open && enteredMs >= 0 && now - enteredMs >= Timing::hoverMs; }
    double Remaining(double now) const { return enteredMs < 0 || open ? -1 : std::max(0.0, Timing::hoverMs - (now - enteredMs)); }
    void Open() { open = true; enteredMs = -1; }
    void Close() { open = false; enteredMs = -1; holds = 0; }
};

}}  // namespace NativeChrome::Sidebar
