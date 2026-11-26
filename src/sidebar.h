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
// Page zone (01 « Zone page » 1PZS-0, retouched): 4 on its right and bottom (Val: the page's inset is 4 everywhere, and
// 4 before an open drawer). « Barre fenêtre » 1Q00-0: 40 high in the complete layout, 32 in the compact one (« Favoris ·
// fenêtre étroite » 1WBH-0); the window buttons 32 x 28 (1Q07-0 / 1Q04-0 / 1Q01-0), 4 apart, 2 inside the zone's 4,
// glyph 12 drawn with a 1.8 / 24 stroke in white 70 % (#FFFFFFB3). « Page web » 1PZT-0: the page, its four corners 8.
namespace Zone {
inline constexpr float inset = 4, bar = 40, compactBar = 32, buttonW = 32, buttonH = 28, buttonGap = 4, buttonRight = inset + 2;
inline constexpr float glyph = 12, glyphStroke = 1.8f * glyph / 24, glyphAlpha = 0xB3 / 255.f, pageRadius = 8;
// The top bar's page card (Designer, 24 nov.: 4 DIP inset, its four corners 6; the side panel's are 8).
inline constexpr float topBarPageRadius = 6;
// Docked drawers start at the page's level, right under the window bar: 40 in the complete layout (07 « Tiroir » 1TNA-0),
// 32 in the compact one (Val, 24 nov.: Paper 11's 25 put « Historique » against the bar; the drawer keeps its own padding).
inline constexpr float drawerTop = 40, compactDrawerTop = compactBar;
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
// Bookmarks grid, variant B (Paper « Favoris · 10 / 20 · B / 40 · B replié / déplié / panneau 300 / 360 »): tiles 40
// high, radius 8, #FFFFFF0A (hover #FFFFFF14), gap 6, as many columns as keep a tile about 42 wide (5 in 232, 6 in 284,
// 7 in 344); favicon 18, a folder's glyph 16 white 70 %. Folded: two rows, then the 24 DIP row « Tous les favoris (N) »
// with a caret (1WKW-0: padding 6, gap 6, 13/16 white 50 %, caret 12 white 50 %) when there are more. Unfolded: up to 5
// rows (224) in a viewport that scrolls (a 32 DIP canvas fade at its bottom, a 3 DIP #FFFFFF33 bar radius 2 at its
// right, 4 from its top: 1WRB-0 / 1WRC-0), then the row « Réduire » (caret up); the tabs keep at least half the height.
inline constexpr float tile = 40, tileGap = 6, tileNominal = 41.6f, toggleRow = 24, togglePad = 6, toggleFont = 13, toggleGlyph = 12;
inline constexpr int foldedRows = 2, openRows = 5;
inline constexpr float fade = 32, barWidth = 3, barInset = 4, barAlpha = 0x33 / 255.f;
// « Gérer les favoris  Ctrl+Maj+O » under the unfolded grid (Paper « Favoris · panneau · Gérer les favoris (tiroir) »
// 1Z8Y-0): 6 under « Réduire », 28 high, #242424, radius 6, padding 6, gap 6: folder 14 #B3B3B3, label 13/16 #E6E6E6,
// the shortcut 12/16 #808080 on the right. It opens the Bookmarks drawer (Ctrl+Maj+O).
inline constexpr float manage = 28, managePad = 6, manageGap = 6, manageIcon = 14, manageFont = 13, manageKeyFont = 12;
// « Onglets » 1Q0R-0: a 1 DIP #FFFFFF0F line 4 in from each side and 8 above the rows (1Q1P-0); rows 32, radius 6,
// padding 8 (4 on the right with the close button), gap 8, favicon 16, title 14/18; 2 between rows.
inline constexpr float lineInset = 4, lineAfter = 8, tabRow = 32, tabGap = 2, tabPad = 8, tabIconGap = 8, favicon = 16;
inline constexpr float close = 24, closePadRight = 4, closeGlyph = 12;
// « Outils (tiroirs) » 1Q0B-0: a 1 DIP #FFFFFF0F top border, padding-top 8, buttons 32 high sharing the width, gap 2.
inline constexpr float toolsLead = 1 + 8, tool = 32, toolGap = 2;
}
// Compact column (reference « Favoris · fenêtre étroite » 1WC7-0, Val's retouch, pasted on 02, 03, 10, 11, 12,
// « compact, 40 »): 40 wide, padding 4, gap 12; items 40 wide (they reach the column's edges).
namespace Compact {
inline constexpr float width = 40, pad = 4, gap = 12, item = 40;
// « Haut » 1WDY-0: the profile without its plate, 32 x 26; gap 6; the fold button 28.
inline constexpr float profileW = 32, profileH = 26, topGap = 6, toggle = 28;
// « Favoris — grille » 1WDI-0: tiles 40 x 36, radius 8, #FFFFFF0F, gap 4; all of them up to 5, else 4 then « » »
// (#FFFFFF0A, caret-double-right 16 white 70 %; open: #FFFFFF1F, white): all the bookmarks in a popover.
inline constexpr float tile = 36, tileGap = 4;
inline constexpr int tiles = 5;
// « Onglets » 1WCO-0: a 1 DIP #FFFFFF0F top border, padding-top 8, rows 40 x 36, radius 6, favicon 16 centred, gap 2.
inline constexpr float tabsLead = 1 + 8, tabRow = 36, tabGap = 2;
// « Outils (tiroirs) » 1WC8-0: a 1 DIP #FFFFFF0F top border, padding-top 8, buttons 40 x 32, gap 4.
inline constexpr float toolsLead = 1 + 8, tool = 32, toolGap = 4;
// « Barre fenêtre » 1WBH-0 (32 high): padding-left 4. « Navigation + adresse » 1WBR-0: gap 6, the pair and Reload
// without plates, the address without background, the row's full height (360 x 32), padding 10, gap 6, lock 12,
// text 14/18 -0.01 em.
inline constexpr float barPad = 4, navGap = 6, addressLead = 0, addressW = 360, addressH = 32, addressPad = 10, addressGap = 6, addressGlyph = 12, addressFont = 14;
}
// « Popover — tous les favoris » (« compact, 40 » 1WSC-0): 248 wide, padding 8, radius 8, #171717, border 1 #FFFFFF0F,
// 4 right of the column, its top on the « » » tile's; inside, the unfolded grid (5 rows then scrolling).
// The compact « » » popover; its foot (« Favoris · compact · Gérer les favoris (tiroir) » 1Z2Q-0): 8 under the grid, a
// 1 DIP #262626 divider, 6, then the row (28, #242424, radius 6, padding 6 / 4, gap 8: folder 16 #B3B3B3, label 13/20
// white, the shortcut 12/16 #808080).
namespace Popover { inline constexpr float width = 248, pad = 8, radius = 8, border = 1, borderAlpha = 0x0F / 255.f, gapX = 4;
inline constexpr float footGap = 8, divider = 1, dividerGap = 6, manage = 28, managePadL = 6, managePadR = 4, manageGap = 8, manageIcon = 16;
inline constexpr int columns = 5; }   // five per row in its 230 (tiles 41.2), as Paper draws it
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
inline constexpr float tileRest = 0x0A / 255.f, tileHover = 0x14 / 255.f, compactTile = 0x0F / 255.f, moreRest = 0x0A / 255.f, moreOpen = 0x1F / 255.f, toggleText = .5f;
inline constexpr float line = 0x0F / 255.f;                         // separators (1Q1P-0, 1Q0B-0 border)
inline constexpr float toolHover = 0x0A / 255.f, toolOpen = 0x1A / 255.f;   // 04 1Q9J-0, 07 1QMH-0
inline constexpr float glyph = 0xB3 / 255.f, glyphMuted = 0x80 / 255.f, settingsGlyph = .5f;
inline constexpr float foldGlyph = .5f;   // « Basculer compact » at rest: white 50 % like Précédent / Suivant / Actualiser (Val #6, Paper updated)   // 1Q0D-0 opacity .5
inline constexpr float field = 0x14 / 255.f, fieldEditing = 0x0A / 255.f;   // 06 1QH8-0 (its ring: Chrome::AddressRing, 5 %)
inline constexpr float dragged = 0x2A / 255.f, draggedBorder = 0x26 / 255.f, origin = .3f, insertion = 2;   // 05; motion spec §1: lifted on #2A2A2A
inline constexpr float lifted = 0x3A / 255.f;   // a row dragged onto the page: its empty place's 1 DIP outline #3A3A3A (24BH-0, 24CB-0)
}
// Motion (16 « Valeurs »; 03 note): the width moves in 180 ms, titles fade first in 80 ms; the pointer rests 400 ms
// on the compact column (or the Focus edge) before the complete column opens over the page.
// The fold: a spring of .35 s (a thrown one .3 s, damping .85); titles out in 80 ms, in over 120 ms (motion spec §2).
namespace Timing { inline constexpr double foldResponse = .35, throwResponse = .30, titlesOutMs = 80, titlesInMs = 120, hoverMs = 400; }
// Below this window width the column is compact by itself (Designer, answer 11): the page keeps about 560. The
// user's choice is never overwritten; above it, the column comes back to that choice.
inline constexpr float autoCompactBelow = 800;
// Under this window width (DIP) the chat card goes over the page instead of pushing it (src/chat-host.h OverPage):
// its window then stays above the page windows (src/main.cpp KeepChatAbovePages).
inline constexpr float chatOverlayBelow = 900;
inline Mode Effective(Mode chosen, float windowWidth) { return windowWidth < autoCompactBelow ? Mode::Compact : chosen; }
// `panel`: the complete column's width, chosen by dragging its edge (Resize below; 248 by default).
inline float Width(Mode m, float panel = Complete::width) { return m == Mode::Complete ? panel : Compact::width; }

// ---- Resize (Paper « Redimensionner · 1 bord survolé / 2 pendant le glisser / 3 largeur max 360 / 4 sous le
// minimum (validées) » 1XTL-0 / 1XWR-0 / 1Y0R-0 / 1Y4X-0) --------------------------------------------------------
// The column's right edge is a handle (6 DIP inside the column, along the page): ↔ cursor; a 2 DIP line hugging the
// page's left edge just outside it and following its rounded corners (an arc concentric with the page corner,
// radius 8 + 1 = 9, 1Y92-0 « M10 1A9 9 0 0 0 1 10V688A9 9 0 0 0 10 697 »), round caps, faded out at both ends
// (opacity 0 at each end, 100 % 12 DIP from it, measured vertically); #6B6B6B when hovered, the accent #3E67FF while
// dragging. Width 220 .. 360 (a hard stop, the pointer may go on); under 160 the column snaps to compact (40) and a
// release keeps it (the setting becomes « Panneau compact »); back past 160 it opens again at 220. Double-click: 248
// (from compact too). The width is remembered per profile (« side-width » in the profile's settings file).
namespace Resize {
inline constexpr float minimum = 220, maximum = 360, snap = 160, handle = 6, line = 2, arc = 9, fade = 12;
inline constexpr unsigned hover = 0x6B6B6B, drag = 0x3E67FF;
inline float Clamp(float w) { return std::clamp(w, minimum, maximum); }
struct State { Mode mode; float width; };
// The column under a pointer whose edge would be at `edge` (DIP from the window's left): compact under 160, else the
// width held between 220 and 360. Stateless: going back past 160 opens it at 220 again.
inline State At(float edge) { return edge < snap ? State{Mode::Compact, Clamp(edge)} : State{Mode::Complete, Clamp(edge)}; }
// Motion spec §5 (Designer): the width follows the pointer 1:1 between 220 and 360 and resists past either bound
// (rubber band: the further past, the less it follows); the compact decision is taken at the release on the PROJECTED
// width, position + momentum (Apple's projection, deceleration .99: (v / 1000) × .99 / .01): under 160 the column folds
// with the gesture's speed (damping .85, .3 s), so a quick flick left folds it even released at 230; otherwise it stays
// where it was released, a rubber-banded width springing back to its bound (damping 1, .3 s).
inline constexpr float rubberDimension = 120, rubberConstant = .55f, deceleration = .99f, settleResponse = .30f, throwDamping = .85f;
inline float Rubber(float over) { return over * rubberDimension * rubberConstant / (rubberDimension + rubberConstant * std::fabs(over)); }
inline float Shown(float raw) { return raw > maximum ? maximum + Rubber(raw - maximum) : raw < minimum ? minimum - Rubber(minimum - raw) : raw; }
inline float Project(float velocity) { return velocity / 1000 * deceleration / (1 - deceleration); }
// While dragging: a column that started compact stays compact until the pointer passes 160; then (and for a complete
// one) the complete column at the rubber-banded width.
inline State Dragging(float raw, bool startedCompact) { return startedCompact && raw < snap ? State{Mode::Compact, minimum} : State{Mode::Complete, Shown(raw)}; }
struct Release { Mode mode; float width; bool springBack; };
inline Release Released(float raw, float velocity) {
    if (raw + Project(velocity) < snap) return {Mode::Compact, Clamp(raw), false};
    return {Mode::Complete, Clamp(raw), raw < minimum || raw > maximum};
}
// The handle: `handle` DIP inside the column's right edge, along the page.
inline Box Handle(float column, const Box& page) { return {column - handle, page.top, column, page.bottom}; }
}

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
// Annoter (01 « Annoter » 1TLR-0): the mode's mark, viewBox 47.5 239.5 14 13.4 in a 15 DIP box, white 70 %.
inline constexpr const char* annotate = "M48.001 250.363L48.049 240.981C48.051 240.535 48.43 240.178 48.89 240.19L59.287 240.226C61.157 240.272 61.667 242.743 59.96 243.484L54.486 245.391C54.03 245.589 53.645 245.909 53.375 246.314 53.342 246.363 53.316 246.416 53.293 246.47L51.265 251.246C50.347 252.733 47.992 252.096 48.001 250.363Z";
inline constexpr float annotateX = 47.5f, annotateY = 239.5f, annotateW = 14, annotateH = 13.4f, annotateBox = 15;
// Grid B: a folder tile (1WOV-0, Phosphor folder fill, white 70 %), the toggle row carets (1WKY-0 down, 1WRF-0 up),
// the compact « » » (1WV9-0, caret-double-right).
inline constexpr const char* folder = "M216,72H131.31L104,44.69A15.86,15.86,0,0,0,92.69,40H40A16,16,0,0,0,24,56V200.62A15.4,15.4,0,0,0,39.38,216H216.89A15.13,15.13,0,0,0,232,200.89V88A16,16,0,0,0,216,72ZM40,56H92.69l16,16H40Z";
inline constexpr const char* caretDown = "M213.66,101.66l-80,80a8,8,0,0,1-11.32,0l-80-80A8,8,0,0,1,53.66,90.34L128,164.69l74.34-74.35a8,8,0,0,1,11.32,11.32Z";
inline constexpr const char* caretUp = "M213.66,165.66a8,8,0,0,1-11.32,0L128,91.31,53.66,165.66a8,8,0,0,1-11.32-11.32l80-80a8,8,0,0,1,11.32,0l80,80A8,8,0,0,1,213.66,165.66Z";
inline constexpr const char* more = "M141.66,133.66l-80,80a8,8,0,0,1-11.32-11.32L124.69,128,50.34,53.66A8,8,0,0,1,61.66,42.34l80,80A8,8,0,0,1,141.66,133.66Zm80-11.32-80-80a8,8,0,0,0-11.32,11.32L204.69,128l-74.35,74.34a8,8,0,0,0,11.32,11.32l80-80A8,8,0,0,0,221.66,122.34Z";
}

// ---- Vue partagée dans la colonne (Paper « Proposition — Vue partagée dans la colonne (panneau + compact) » 2473-0) ----
// 1 « Repos » (247H-0 panel, 248B-0 compact): tabs together in a split view stick together under one shared background
// #262626, radius 8, padding 2, no gap between them: a 1 DIP row holds a 10 x 1 #545454 line centred on the favicons
// (247N-0: 2 + 11 from the background's left; the favicon at 2 + 8: both centred 18 in). The displayed tab keeps the
// active row (#3E3E3E, title 500 white), the others stay normal (#B3B3B3). Compact: the same background around the icons,
// the line centred (the board draws 32 DIP cells; here the column's 40 x 36 rows, 2 inside the background, which keeps
// the column's 40). The block moves as one when a member is dragged (« la paire se déplace d'un bloc »).
// 2 « Survol du menu » (2492-0 panel, 24AJ-0 compact): hovering a row of the split menu outlines the tabs it designates
// (the right-clicked tab and its target) with a 1 DIP inner #8C8C8C stroke on their row.
// Membership is the top bar's (Chrome::tabGroups; Chrome::DrawTabs): tab n joins tab n + 1 when both have the same non-zero
// group; a run of joined tabs is one block (a member away from the others is a block of one).
namespace Split {
inline constexpr float pad = 2, radius = 8, joint = 1, lineW = 10, lineH = 1, outlineW = 1;
inline constexpr float compactMember = 34;   // a compact member tab: 36 x 34 (radius 6) inside the 40-wide block
inline constexpr float fill = 0x26 / 255.f, line = 0x54 / 255.f, outline = 0x8C / 255.f;   // #262626, #545454, #8C8C8C
// Frozen (Val's retouch, 2473-0 « Repos » / « Sélectionné »): the block is a 1 DIP #262626 outline without fill around its
// rows (radius 8, padding 2); when the pair is displayed both rows take #262626, corners 6 outside and 3 inside (like
// Précédent / Suivant), titles white 500; the displayed tab has no #3E3E3E of its own.
inline constexpr float frame = 0x26 / 255.f, frameW = 1, outer = 6, inner = 3;
}
enum class SplitPos { None, Single, First, Middle, Last };
struct SplitRow {
    int group = 0; SplitPos pos = SplitPos::None;
    bool Member() const { return group != 0; }
    bool JoinsPrevious() const { return pos == SplitPos::Middle || pos == SplitPos::Last; }
    bool JoinsNext() const { return pos == SplitPos::First || pos == SplitPos::Middle; }
};
// Per tab: its group (0 = none) and its place in its block, from the top bar's `tabGroups` (missing entries: no group).
inline std::vector<SplitRow> SplitRows(const std::vector<int>& groups, int count) {
    std::vector<SplitRow> rows(static_cast<size_t>(std::max(0, count)));
    const auto g = [&](int i) { return i >= 0 && i < count && i < static_cast<int>(groups.size()) ? groups[i] : 0; };
    for (int i = 0; i < count; ++i) {
        const int k = g(i); if (!k) continue;
        const bool prev = g(i - 1) == k, next = g(i + 1) == k;
        rows[i] = {k, prev ? (next ? SplitPos::Middle : SplitPos::Last) : (next ? SplitPos::First : SplitPos::Single)};
    }
    return rows;
}
// The block holding row `i`: [first, first + count) (a row outside any group is a block of one).
struct Block { int first = -1, count = 0; };
inline Block BlockOf(const std::vector<SplitRow>& rows, int i) {
    const int n = static_cast<int>(rows.size()); if (i < 0 || i >= n) return {};
    int a = i, b = i; while (a > 0 && rows[a].JoinsPrevious()) --a; while (b + 1 < n && rows[b].JoinsNext()) ++b;
    return {a, b - a + 1};
}
// Extra height before each row (and, at [n], before the « + » row) that the blocks add to the plain list (rows `gap`
// apart): the padding above a block, 1 instead of `gap` between joined rows, the padding under it.
inline std::vector<float> SplitShift(const std::vector<SplitRow>& rows, float gap) {
    std::vector<float> s(rows.size() + 1, 0.f); float y = 0;
    for (size_t i = 0; i <= rows.size(); ++i) {
        if (i > 0 && rows[i - 1].Member() && !rows[i - 1].JoinsNext()) y += Split::pad;
        if (i < rows.size()) { if (rows[i].JoinsPrevious()) y += Split::joint - gap; else if (rows[i].Member()) y += Split::pad; }
        s[i] = y;
    }
    return s;
}
// Reorder with blocks: dragging row `from` to DropSlot's `slot` moves its whole block, and never lands inside another block
// (it snaps to that block's nearer edge; halfway, the edge it comes from). `dest`: where the block's first row lands in the
// new order. `insert`: the row the drop line is drawn against (Chrome::sidebarInsert: below it when it is after `from`,
// above it otherwise; `from` itself when nothing moves). BlockMove(rows, from, m.insert) gives the same move again.
struct Move { int first = -1, count = 0, dest = -1, insert = -1; bool Moves() const { return count > 0 && dest >= 0 && dest != first; } };
inline Move BlockMove(const std::vector<SplitRow>& rows, int from, int slot) {
    const int n = static_cast<int>(rows.size()); Move m; if (from < 0 || from >= n) return m;
    const Block b = BlockOf(rows, from); m.first = b.first; m.count = b.count; m.dest = b.first; m.insert = from;
    if (slot < 0 || slot >= n || (slot >= b.first && slot < b.first + b.count)) return m;
    const bool down = slot >= b.first + b.count;
    int gap = down ? slot + 1 : slot;   // the boundary the block goes to: before old row `gap`
    if (gap > 0 && gap < n && rows[gap].JoinsPrevious()) {
        const Block o = BlockOf(rows, gap); const int top = o.first, bottom = o.first + o.count;
        const int toTop = gap - top, toBottom = bottom - gap;
        gap = toTop < toBottom ? top : toBottom < toTop ? bottom : (down ? top : bottom);
    }
    const int dest = gap <= b.first ? gap : gap - b.count;
    if (dest == b.first) return m;
    m.dest = dest; m.insert = down ? gap - 1 : gap;
    return m;
}
// Moves [first, first + count) so that it starts at `dest` in the new order (the host's tabs; Move::dest).
template <class T> void MoveRange(std::vector<T>& v, int first, int count, int dest) {
    const int n = static_cast<int>(v.size());
    if (first < 0 || count <= 0 || first + count > n || dest < 0 || dest + count > n || dest == first) return;
    if (dest < first) std::rotate(v.begin() + dest, v.begin() + first, v.begin() + first + count);
    else std::rotate(v.begin() + first, v.begin() + first + count, v.begin() + dest + count);
}
// The rows a highlighted split-menu row designates (Chrome::menuTargets) plus the right-clicked tab (`source`, -1 none):
// both are outlined while a target is designated; sorted, in range, once each.
inline std::vector<int> Designated(const std::vector<int>& targets, int source, int count) {
    std::vector<int> d; if (targets.empty()) return d;
    for (int i : targets) if (i >= 0 && i < count) d.push_back(i);
    if (d.empty()) return d;
    if (source >= 0 && source < count) d.push_back(source);
    std::sort(d.begin(), d.end()); d.erase(std::unique(d.begin(), d.end()), d.end());
    return d;
}

// ---- Geometry ------------------------------------------------------------------------------------------------
struct Input {
    Mode mode = Mode::Complete;   // the column's layout (complete also for the column opened over the page)
    float zoneLeft = -1;          // left of the page zone; -1: the column's width (docked)
    Mode zoneMode = Mode::Complete; // the window bar's layout (compact under the column opened over the page)
    bool zoneSet = false;         // zoneMode given (else the column's)
    float width = 1180, height = 740;
    bool grid = false;            // the grid exists (bookmarks bar « Toujours », or « Automatique » with bookmarks)
    int bookmarks = 0;            // bar bookmarks (0 with `grid`: the empty tile)
    bool gridOpen = false;        // complete: « Tous les favoris » unfolded
    float gridScroll = 0;         // complete, unfolded: the grid's own scroll
    int tabs = 0;                 // tab rows (the « + Nouvel onglet » row comes after them)
    int tools = 4;                // footer buttons
    float scroll = 0;             // tab list scroll (clamped)
    float band = 0;               // annotation band at the top of the page zone (answer 6)
    float reserve = 0;            // an open drawer's column: the page stops `inset` before it
    bool reserveLeft = true;      // #143 (owner 2026-11-25): the drawer docks on the panel's side, against the column: the page starts `inset` after it
    float dockAt = -1;            // #143: the host's dock (DIP) when DrawerLayout::Side's other choices move it off the column's edge; -1 none
    float panel = Complete::width; // the complete column's width (Resize: 220 .. 360)
    std::vector<int> groups;      // per tab, its split view group (0 = none): the top bar's Chrome::tabGroups
    bool hidden = false;          // Mode Focus: the column hides, the page takes the window (FocusSide)
    float top = 0;                // hidden: where the column starts when it comes back (under the annotation banner)
};
// A grid of tiles in a scrolling viewport (the complete column's, the compact popover's).
struct Grid {
    Box view;                     // the viewport (tiles are clipped to it)
    std::vector<Box> tiles;       // every bookmark's tile at the current scroll
    int columns = 5, rows = 0;
    float content = 0, maxScroll = 0, scroll = 0;
    Box bar;                      // scroll bar (empty without scrolling)
    bool Scrolls() const { return maxScroll > .5f; }
};
inline int Columns(float width) { return std::max(1, static_cast<int>(std::floor((width + Complete::tileGap) / (Complete::tileNominal + Complete::tileGap) + 1e-3f))); }
// `n` tiles in rows of Columns(w) inside [left, left + w], from `top`, `visibleRows` rows tall at most (then scrolling),
// `maxHeight` at most (the viewport may cut a row: the fade covers it).
// `columns` > 0: that many per row (the popover's five in 230, Paper 1WSC-0 / 1YX2-0), else Columns(w).
inline Grid MakeGrid(float left, float top, float w, int n, int visibleRows, float scroll, float maxHeight = 1e9f, int columns = 0) {
    namespace C = Complete; Grid g; g.columns = columns > 0 ? columns : Columns(w);
    g.rows = n <= 0 ? 0 : (n + g.columns - 1) / g.columns;
    const auto height = [](int r) { return r <= 0 ? 0.f : r * C::tile + (r - 1) * C::tileGap; };
    const float viewH = std::min(height(std::min(g.rows, visibleRows)), std::max(C::tile, maxHeight));
    g.view = {left, top, left + w, top + viewH};
    g.content = height(g.rows); g.maxScroll = std::max(0.f, g.content - viewH); g.scroll = std::clamp(scroll, 0.f, g.maxScroll);
    const float tw = (w - C::tileGap * (g.columns - 1)) / g.columns;
    for (int i = 0; i < n; ++i) { const int c = i % g.columns, r = i / g.columns; const float x = left + c * (tw + C::tileGap), y = top + r * (C::tile + C::tileGap) - g.scroll; g.tiles.push_back({x, y, x + tw, y + C::tile}); }
    if (g.Scrolls()) {
        const float track = viewH - 2 * C::barInset, h = std::max(16.f, track * viewH / g.content), y = g.view.top + C::barInset + (track - h) * (g.scroll / g.maxScroll);
        g.bar = {g.view.right - C::barWidth, y, g.view.right, y + h};
    }
    return g;
}
struct Layout {
    Mode mode = Mode::Complete;
    float column = 0;             // the column's width
    float bar = Zone::bar;        // the window bar's height
    Box profile, back, forward, hairline, reload, toggle;
    bool profilePlate = true;     // the compact profile has no plate (Val's retouch)
    bool navPlates = true;        // the compact window bar's Back / Forward / Reload have no plates (Val's retouch)
    // The address field: in the column (complete) or in the window bar (compact). `copy` / `star` are empty in compact.
    bool addressInColumn = true, addressPlate = true;
    Box address, lock, text, copy, star;
    float addressFont = 16, addressGlyph = 14;
    // Bookmarks. Complete: `grid` (all bookmarks, folded to two rows or unfolded) and `gridToggle` (« Tous les favoris (N)
    // » / « Réduire », empty when everything fits). Compact: `tiles` (up to 5; with `more`, the 5th is « » »).
    Box gridBlock;                // the whole block (tiles + toggle row): empty without a grid
    Grid grid; Box gridToggle; bool gridOpen = false;
    Box manage;                   // complete, unfolded: « Gérer les favoris » (empty otherwise)
    bool emptyGrid = false;       // « Toujours » with no bookmark: one tile (complete: the full row)
    std::vector<Box> tiles; bool more = false;
    Box line;                     // the line above the tabs (complete: inset; compact: the section's border)
    Box tabsView;                 // tab rows are clipped to it and scroll under it
    std::vector<Box> tabs;        // at the current scroll
    std::vector<SplitRow> split;  // per tab: its split view group and place in its block (Split)
    std::vector<Box> blocks, joints;   // split view: each block's shared background, the 10 x 1 lines between joined rows
    Box newTab;
    float content = 0, maxScroll = 0, scroll = 0;
    Box footerLine;
    std::vector<Box> tools;
    Box minimize, maximize, close;
    Box zone, band, page;
    float drawerTop = Zone::drawerTop;   // the real drawers dock from here
    float drawerDock = 0;                // #143: their left edge (DIP from the window's): the column's right edge (in Focus, after its border)
    bool hidden = false;          // Mode Focus: the column is not docked (it comes back over the page, FocusSide)
    float top = 0;                // the column's top (hidden: under the annotation banner, else 0)
};
// The close target in a complete tab row (1Q1D-0: 24 x 24, 4 from the row's right edge).
inline Box CloseIn(const Box& row) { const float r = row.right - Complete::closePadRight, cy = (row.top + row.bottom) / 2; return {r - Complete::close, cy - Complete::close / 2, r, cy + Complete::close / 2}; }
// Favicon slot of a row (complete: 8 in; compact: centred).
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
// Split view blocks of a laid-out list: each block's shared background (its rows + `pad`), and a 10 x 1 line under each
// row joined to the next, centred on the favicon (Split; Paper 247N-0 / 248F-0).
inline void PlaceSplit(Layout& L) {
    L.blocks.clear(); L.joints.clear();
    const int n = std::min(static_cast<int>(L.tabs.size()), static_cast<int>(L.split.size()));
    for (int i = 0; i < n; ++i) {
        const SplitRow& s = L.split[i]; if (!s.Member()) continue;
        if (!s.JoinsPrevious()) { const Block b = BlockOf(L.split, i); const Box& a = L.tabs[i]; const Box& z = L.tabs[std::min(n - 1, b.first + b.count - 1)];
            L.blocks.push_back({a.left - Split::pad, a.top - Split::pad, z.right + Split::pad, z.bottom + Split::pad}); }
        if (s.JoinsNext() && i + 1 < n) { const Box f = FaviconIn(L.tabs[i], L.mode); const float cx = (f.left + f.right) / 2, y = L.tabs[i].bottom;
            L.joints.push_back({cx - Split::lineW / 2, y, cx + Split::lineW / 2, y + Split::lineH}); }
    }
}

inline Layout ComputeHidden(const Input& in);
inline Layout Compute(const Input& in) {
    if (in.hidden) return ComputeHidden(in);
    Layout L; L.mode = in.mode; const bool full = in.mode == Mode::Complete;
    const Mode zoneMode = in.zoneSet ? in.zoneMode : in.mode;
    const float W = std::max(in.width, 1.f), H = std::max(in.height, 1.f);
    L.column = Width(in.mode, Resize::Clamp(in.panel));
    const float zoneLeft = in.zoneLeft >= 0 ? in.zoneLeft : L.column;
    L.bar = zoneMode == Mode::Complete ? Zone::bar : Zone::compactBar;
    // Page zone: window buttons right-aligned in the bar, the page under it (and under the annotation band).
    {
        namespace Z = Zone;
        const float right = W - Z::buttonRight, cy = L.bar / 2;
        L.close = Centered(right - Z::buttonW / 2, cy, Z::buttonW, Z::buttonH);
        L.maximize = Centered(L.close.left - Z::buttonGap - Z::buttonW / 2, cy, Z::buttonW, Z::buttonH);
        L.minimize = Centered(L.maximize.left - Z::buttonGap - Z::buttonW / 2, cy, Z::buttonW, Z::buttonH);
        L.zone = {zoneLeft, 0, W, H};
        float pageRight = std::max(zoneLeft, W - Z::inset), pageLeft = zoneLeft;
        L.drawerDock = in.dockAt >= 0 ? in.dockAt : zoneLeft;   // #143: against the docked column
        if (in.reserve > 0) { if (in.reserveLeft) pageLeft = std::min(pageRight, std::max(zoneLeft, L.drawerDock + in.reserve + Z::inset)); else pageRight = std::max(zoneLeft, std::min(pageRight, W - in.reserve - Z::inset)); }
        const float pageBottom = std::max(L.bar, H - Z::inset);
        L.band = in.band > 0 ? Box{pageLeft, L.bar, pageRight, L.bar + in.band} : Box{};
        L.page = {pageLeft, L.bar + in.band, pageRight, std::max(L.bar + in.band, pageBottom)};
        L.drawerTop = zoneMode == Mode::Complete ? Z::drawerTop : Z::compactDrawerTop;
    }
    auto pairAt = [&](float x, float top) {
        L.back = {x, top, x + Nav::button, top + Nav::button};
        const float h = x + Nav::button + Nav::pairGap, cy = top + Nav::button / 2;
        L.hairline = {h, cy - Nav::hairlineH / 2, h + Nav::hairline, cy + Nav::hairlineH / 2};
        L.forward = {h + Nav::hairline + Nav::pairGap, top, h + Nav::hairline + Nav::pairGap + Nav::button, top + Nav::button};
    };
    if (full) {
        namespace C = Complete;
        const float l = C::pad, r = L.column - C::pad; float y = C::pad;
        L.profile = {l, y, l + C::row, y + C::row};
        pairAt(L.profile.right + C::rowGap, y);
        L.reload = {L.forward.right + C::rowGap, y, L.forward.right + C::rowGap + Nav::button, y + Nav::button};
        L.toggle = Centered(r - C::toggle / 2, y + C::row / 2, C::toggle, C::toggle);
        y += C::row + C::gap;
        L.addressInColumn = true; L.addressFont = C::addressFont; L.addressGlyph = C::addressGlyph;
        L.address = {l, y, r, y + C::address};
        const float cy = y + C::address / 2, g = C::addressGlyph;
        L.lock = Centered(l + C::addressPad + g / 2, cy, g, g);
        L.star = Centered(r - C::addressPad - g / 2, cy, g, g);
        L.copy = Centered(L.star.left - C::addressGap - g / 2, cy, g, g);
        L.text = {L.lock.right + C::addressGap, y, L.copy.left - C::addressGap, y + C::address};
        y += C::address + C::gap;
        const int tools = std::max(1, in.tools);
        const float footer = H - C::pad - (C::toolsLead + C::tool);
        L.footerLine = {l, footer, r, footer + 1};
        const float tw = (r - l - C::toolGap * (tools - 1)) / tools;
        for (int i = 0; i < tools; ++i) { const float x = l + i * (tw + C::toolGap); L.tools.push_back({x, footer + C::toolsLead, x + tw, footer + C::toolsLead + C::tool}); }
        // Grid B: two rows folded; unfolded up to five then scrolling; the toggle row only when it does not all fit.
        if (in.grid) {
            if (in.bookmarks <= 0) { L.emptyGrid = true; L.gridBlock = {l, y, r, y + C::tile}; L.tiles.push_back(L.gridBlock); y = L.gridBlock.bottom + C::gap; }
            else {
                const int n = in.bookmarks, cols = Columns(r - l);
                const bool folds = n > cols * C::foldedRows;
                L.gridOpen = folds && in.gridOpen;
                if (!L.gridOpen) L.grid = MakeGrid(l, y, r - l, std::min(n, cols * C::foldedRows), C::foldedRows, 0);
                else {
                    // The tabs keep at least half of what the grid block and the tabs share.
                    const float shared = (footer - C::gap) - y, maxGrid = shared / 2 - C::tileGap - C::toggleRow - C::tileGap - C::manage;
                    L.grid = MakeGrid(l, y, r - l, n, C::openRows, in.gridScroll, maxGrid);
                }
                float bottom = L.grid.view.bottom;
                if (folds) { L.gridToggle = {l, bottom + C::tileGap, r, bottom + C::tileGap + C::toggleRow}; bottom = L.gridToggle.bottom; }
                if (L.gridOpen) { L.manage = {l, bottom + C::tileGap, r, bottom + C::tileGap + C::manage}; bottom = L.manage.bottom; }
                L.gridBlock = {l, y, r, bottom}; y = bottom + C::gap;
            }
        }
        L.line = {l + C::lineInset, y, r - C::lineInset, y + 1};
        L.tabsView = {l, y + 1 + C::lineAfter, r, std::max(y + 1 + C::lineAfter, footer - C::gap)};
        const float pitch = C::tabRow + C::tabGap, lead = C::tabGap;
        L.split = SplitRows(in.groups, in.tabs); const auto shift = SplitShift(L.split, C::tabGap);   // split view blocks
        L.content = lead + (in.tabs + 1) * pitch - C::tabGap + shift.back();
        L.maxScroll = std::max(0.f, L.content - L.tabsView.Height());
        L.scroll = std::clamp(in.scroll, 0.f, L.maxScroll);
        for (int i = 0; i <= in.tabs; ++i) {
            const float t = L.tabsView.top + lead + i * pitch + shift[i] - L.scroll, x = i < in.tabs && L.split[i].Member() ? Split::pad : 0;
            const Box row{l + x, t, r - x, t + C::tabRow};
            if (i < in.tabs) L.tabs.push_back(row); else L.newTab = row;
        }
        PlaceSplit(L);
    } else {
        namespace K = Compact;
        const float cx = K::width / 2, l = cx - K::item / 2, r = cx + K::item / 2; float y = K::pad;
        L.profilePlate = false;
        L.profile = Centered(cx, y + K::profileH / 2, K::profileW, K::profileH);
        y += K::profileH + K::topGap;
        L.toggle = Centered(cx, y + K::toggle / 2, K::toggle, K::toggle);
        y += K::toggle + K::gap;
        if (in.grid) {
            const int n = in.bookmarks; L.emptyGrid = n <= 0; L.more = n > K::tiles;
            const int count = L.emptyGrid ? 1 : std::min(n, K::tiles);
            for (int i = 0; i < count; ++i) { const float t = y + i * (K::tile + K::tileGap); L.tiles.push_back({l, t, r, t + K::tile}); }
            L.gridBlock = {l, y, r, L.tiles.back().bottom};
            y = L.gridBlock.bottom + K::gap;
        }
        const int tools = std::max(1, in.tools);
        const float toolsH = K::toolsLead + tools * K::tool + (tools - 1) * K::toolGap, footer = H - K::pad - toolsH;
        L.footerLine = {l, footer, r, footer + 1};
        for (int i = 0; i < tools; ++i) { const float t = footer + K::toolsLead + i * (K::tool + K::toolGap); L.tools.push_back({l, t, r, t + K::tool}); }
        L.line = {l, y, r, y + 1};
        L.tabsView = {l, y + 1, r, std::max(y + 1, footer - K::gap)};
        const float pitch = K::tabRow + K::tabGap, lead = K::tabsLead - 1;
        L.split = SplitRows(in.groups, in.tabs); auto shift = SplitShift(L.split, K::tabGap);   // split view blocks
        // Compact members are 36 x 34 inside their 40-wide block (Designer, 24 nov.): each member row is 2 shorter.
        { float trim = 0; for (int i = 0; i <= in.tabs; ++i) { shift[i] += trim; if (i < in.tabs && L.split[i].Member()) trim -= K::tabRow - Split::compactMember; } }
        L.content = lead + (in.tabs + 1) * pitch - K::tabGap + shift.back();
        L.maxScroll = std::max(0.f, L.content - L.tabsView.Height());
        L.scroll = std::clamp(in.scroll, 0.f, L.maxScroll);
        for (int i = 0; i <= in.tabs; ++i) {
            const bool member = i < in.tabs && L.split[i].Member();
            const float t = L.tabsView.top + lead + i * pitch + shift[i] - L.scroll, x = member ? Split::pad : 0;
            const Box row{l + x, t, r - x, t + (member ? Split::compactMember : K::tabRow)};
            if (i < in.tabs) L.tabs.push_back(row); else L.newTab = row;
        }
        PlaceSplit(L);
    }
    if (zoneMode == Mode::Compact && !full) {
        // Window bar: the pair, Reload and the address after the zone's 4 DIP padding, 6 apart, no plates.
        namespace K = Compact;
        const float top = (L.bar - Nav::button) / 2;
        pairAt(zoneLeft + K::barPad, top);
        L.reload = {L.forward.right + K::navGap, top, L.forward.right + K::navGap + Nav::button, top + Nav::button};
        L.navPlates = false;
        L.addressInColumn = false; L.addressPlate = false; L.addressFont = K::addressFont; L.addressGlyph = K::addressGlyph;
        const float al = L.reload.right + K::navGap + K::addressLead, ar = std::max(al, std::min(al + K::addressW, L.minimize.left - Zone::buttonGap));
        L.address = Centered((al + ar) / 2, L.bar / 2, ar - al, K::addressH);
        const float acy = L.bar / 2, g = K::addressGlyph;
        L.lock = Centered(al + K::addressPad + g / 2, acy, g, g);
        L.text = {L.lock.right + K::addressGap, L.address.top, std::max(L.lock.right + K::addressGap, ar - K::addressPad), L.address.bottom};
    }
    return L;
}
// ---- Mode Focus in the side layouts (Designer 24 nov.: Paper « Annotation + Focus · panneau (bord survolé) » 2091-0,
// « … · compact » 20EJ-0; the older boards 13 / 14 / 15 follow this code) -------------------------------------------------
// The column hides: the page takes the window minus 4 DIP on every side (radius 8), no window bar, no window buttons; with
// the annotation mode its banner spans the top (x 4 .. W - 4, y 0 .. 32) and the page starts under it. An 8 DIP band on
// the window's left edge recalls the column: after 400 ms (Timing::hoverMs) it slides over the page from the left, under
// the banner, square, a 1 DIP #262626 right border (outside its 248 / 40); it leaves to the left as soon as the pointer
// leaves it, unless something holds it (a drawer opened from it, a menu, a drag, the address being typed in). Ctrl+Maj+,
// slides it in (it stays: the shortcut again or Échap). Drawers start at the page's top, anchored right, the page stops
// 4 DIP before them. Motion (the drawers' spec): in on a critically damped 0.3 s spring, out 0.2 s, always from the
// displayed position; reduced motion: a 120 ms fade.
namespace FocusSide {
inline constexpr float edge = 8;                              // the left band that recalls the column (board 13)
inline constexpr float border = 1;                            // its right border, outside the column
inline constexpr unsigned borderRgb = 0x262626;
// #143 Q3 (Designer, 2026-11-25): a drawer docks just after that border, never over it (false: flush with the column).
inline constexpr bool drawerAfterBorder = true;
inline constexpr float enterResponse = .30f, exitResponse = .20f;
inline constexpr double reducedFadeMs = 120;
}
// Moves the column's boxes down by `dy` (the window bar's, compact, move too: the hidden layout has none).
inline void ShiftColumn(Layout& L, float dy) {
    if (dy == 0) return;
    auto mv = [dy](Box& b) { b.top += dy; b.bottom += dy; };
    auto all = [&](std::vector<Box>& v) { for (auto& b : v) mv(b); };
    for (Box* b : {&L.profile, &L.back, &L.forward, &L.hairline, &L.reload, &L.toggle, &L.address, &L.lock, &L.text, &L.copy,
                   &L.star, &L.gridBlock, &L.grid.view, &L.grid.bar, &L.gridToggle, &L.manage, &L.line, &L.tabsView, &L.newTab,
                   &L.footerLine}) mv(*b);
    all(L.grid.tiles); all(L.tiles); all(L.tabs); all(L.blocks); all(L.joints); all(L.tools);
}
// The column laid out from `in.top` to the window's bottom (where it comes back), the page zone as above.
inline Layout ComputeHidden(const Input& in) {
    namespace Z = Zone;
    Input c = in; c.hidden = false; c.top = 0; c.zoneLeft = -1; c.zoneSet = false; c.height = std::max(1.f, in.height - in.top);
    Layout L = Compute(c); ShiftColumn(L, in.top);
    L.hidden = true; L.top = in.top;
    const float W = std::max(in.width, 1.f), H = std::max(in.height, 1.f);
    const float top = std::max(in.band, Z::inset);            // under the banner, else the 4 DIP inset
    L.bar = std::max(0.f, Z::inset - in.band);                // the chrome's ContentTop is bar + band (continuous while the banner slides)
    L.minimize = L.maximize = L.close = Box{};
    float pageRight = std::max(Z::inset, W - Z::inset), pageLeft = Z::inset;
    // #143: a drawer docks against the column shown over the page, just after its border (FocusSide::drawerAfterBorder).
    L.drawerDock = in.dockAt >= 0 ? in.dockAt : L.column + (FocusSide::drawerAfterBorder ? FocusSide::border : 0.f);
    if (in.reserve > 0) { if (in.reserveLeft) pageLeft = std::min(pageRight, std::max(Z::inset, L.drawerDock + in.reserve + Z::inset)); else pageRight = std::max(Z::inset, std::min(pageRight, W - in.reserve - Z::inset)); }
    L.zone = {0, 0, W, H};
    L.band = in.band > 0 ? Box{Z::inset, 0, std::max(Z::inset, W - Z::inset), in.band} : Box{};
    L.page = {pageLeft, top, pageRight, std::max(top, H - Z::inset)};
    L.drawerTop = top;
    return L;
}
// The column over the page in Focus: shown or not, and how. Pure (the host samples the pointer every ~30 ms).
struct FocusReveal {
    bool open = false;
    bool pinned = false;          // opened by Ctrl+Maj+, (or for the address): stays until the shortcut again or Échap
    bool wide = false;            // opened for the address in the compact layout: the complete column (it has the field)
    double edgeSinceMs = -1;      // the pointer in the left band since (-1: not there)
    // One pointer sample: `inBand` the left band (window in front, no button down), `overColumn` the shown column,
    // `hold` something keeps it. True when `open` changed.
    bool Pointer(double now, bool inBand, bool overColumn, bool hold) {
        if (!open) {
            if (!inBand) { edgeSinceMs = -1; return false; }
            if (edgeSinceMs < 0) edgeSinceMs = now;
            if (now - edgeSinceMs < Timing::hoverMs) return false;
            open = true; pinned = wide = false; edgeSinceMs = -1; return true;
        }
        edgeSinceMs = -1;
        if (pinned || overColumn || inBand || hold) return false;
        open = wide = false; return true;
    }
    double Remaining(double now) const { return open || edgeSinceMs < 0 ? -1 : std::max(0.0, Timing::hoverMs - (now - edgeSinceMs)); }
    // The shortcut: in (pinned) / out.
    void Toggle() { if (open) Close(); else { open = pinned = true; wide = false; edgeSinceMs = -1; } }
    // The address needs the field: in, pinned; `compact`: the complete column.
    void ForAddress(bool compact) { if (!open) { open = pinned = true; edgeSinceMs = -1; } else pinned = true; wide = wide || compact; }
    // #143: a drawer docks against it: in, not pinned (the host holds it while the drawer is open, then it goes as usual).
    void ForDrawer() { if (!open) { open = true; pinned = wide = false; edgeSinceMs = -1; } }
    void Close() { open = pinned = wide = false; edgeSinceMs = -1; }
};
// The compact « » » popover (« compact, 40 » 1WSC-0): all the bookmarks, 4 right of the column, top on the « » » tile
// (moved up to stay in the window), the unfolded grid inside (5 rows, then scrolling).
struct PopoverLayout { Box box; Grid grid; Box divider, manage; };
inline PopoverLayout ComputePopover(float column, const Box& anchor, int bookmarks, float scroll, float windowHeight) {
    namespace P = Popover; PopoverLayout p;
    const float inner = P::width - 2 * (P::border + P::pad);
    const Grid probe = MakeGrid(0, 0, inner, bookmarks, Complete::openRows, 0, 1e9f, P::columns);
    const float foot = P::footGap + P::divider + P::dividerGap + P::manage;
    const float h = 2 * (P::border + P::pad) + probe.view.Height() + foot;
    const float top = std::max(Zone::inset, std::min(anchor.top, windowHeight - Zone::inset - h));
    p.box = {column + P::gapX, top, column + P::gapX + P::width, top + h};
    p.grid = MakeGrid(p.box.left + P::border + P::pad, top + P::border + P::pad, inner, bookmarks, Complete::openRows, scroll, 1e9f, P::columns);
    const float l = p.box.left + P::border + P::pad, dy = p.grid.view.bottom + P::footGap;
    p.divider = {l, dy, l + inner, dy + P::divider};
    p.manage = {l, p.divider.bottom + P::dividerGap, l + inner, p.divider.bottom + P::dividerGap + P::manage};
    return p;
}
// Tile index under (x, y) in a grid (clipped by its view), -1 elsewhere.
inline int TileAt(const Grid& g, float x, float y) {
    if (!g.view.Contains(x, y)) return -1;
    for (int i = 0; i < static_cast<int>(g.tiles.size()); ++i) if (g.tiles[i].Contains(x, y)) return i;
    return -1;
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

// ---- Row motion (#127, parity point 3) ---------------------------------------------------------------------------
// The tab strip's open / close model (Motion::TabStripMotion, src/tab-motion.h) runs unchanged on the vertical axis: the
// row height stands for the tab width, the row gap for the tab gap. Each row is presented at its cumulative slot, so an
// opening row grows to its full height and pushes the rows under it (and « + Nouvel onglet »); a closing row leaves a
// ghost at full height in its old place that blurs out while its slot collapses and the rows under it glide up. Same
// springs (.30 s open, .22 s close), 150 ms exit, reduced motion as the strip. Hit regions stay on the functional layout.
struct RowMetrics { float lead = 0, row = 0, gap = 0; };
inline RowMetrics Rows(Mode m) { return m == Mode::Complete ? RowMetrics{Complete::tabGap, Complete::tabRow, Complete::tabGap} : RowMetrics{Compact::tabsLead - 1, Compact::tabRow, Compact::tabGap}; }
// A row at content-space `slot` (cumulative, before the list's scroll), `presence` of its height (1 at rest).
inline Box ShownRow(const Layout& L, float slot, float presence, float scroll) {
    const auto m = Rows(L.mode); const float t = L.tabsView.top + m.lead + slot - scroll;
    return {L.tabsView.left, t, L.tabsView.right, t + m.row * std::clamp(presence, 0.f, 1.f)};
}

// ---- Fold animation (#127 motion spec §2, Designer) -----------------------------------------------------------------
// t = 1 complete, 0 compact, on a spring (damping 1, response .35 s; Apple's model, as Motion::Spring): interruptible, a
// new target starts from the presented value and velocity, so a reversal never jumps. The page follows on the same
// frame (the host lays it out from ColumnWidth). Folding: the titles fade out over the first 80 ms, the column keeps the
// complete layout for those 80 ms, then the compact one (its tiles and buttons glide to their places: Chrome's morph).
// Unfolding: the complete layout at once, the titles fading in over 120 ms once the width passes 70 %. A gesture can
// throw it (the edge released under 160: damping .85 with the pointer's velocity). Reduced motion: at once.
inline double EaseOut(double x) { x = std::clamp(x, 0.0, 1.0); const double u = 1 - x; return 1 - u * u * u; }
struct Fold {
    Mode target = Mode::Complete; double startMs = -1; float from = 1, velocity = 0, damping = 1, titlesAtStart = 1;
    double response = Timing::foldResponse, titlesFrom = -1; float panel = Complete::width;   // the complete width
    float Settled() const { return target == Mode::Complete ? 1.f : 0.f; }
    void Eval(double now, float& x, float& v) const {
        if (startMs < 0) { x = Settled(); v = 0; return; }
        const double t = std::max(0.0, (now - startMs) / 1000), w = 6.283185307179586 / response, c1 = from - Settled();
        double px, pv;
        if (damping >= .9999f) { const double c2 = velocity + w * c1, e = std::exp(-w * t); px = (c1 + c2 * t) * e; pv = (c2 - w * (c1 + c2 * t)) * e; }
        else { const double z = damping, wd = w * std::sqrt(1 - z * z), c2 = (velocity + z * w * c1) / wd, e = std::exp(-z * w * t), cs = std::cos(wd * t), sn = std::sin(wd * t);
            px = e * (c1 * cs + c2 * sn); pv = e * ((c2 * wd - z * w * c1) * cs - (c1 * wd + z * w * c2) * sn); }
        if (std::fabs(px) < 1e-3 && std::fabs(pv) < 1e-3 * w) { x = Settled(); v = 0; return; }
        x = static_cast<float>(Settled() + px); v = static_cast<float>(pv);
    }
    bool Moving(double now) const { if (startMs < 0) return false; float x, v; Eval(now, x, v); return x != Settled() || v != 0 || TitlesMoving(now); }
    // Width progress (0 compact .. 1 complete); a thrown fold may overshoot past the complete width a little, never under
    // the compact one.
    float Progress(double now) const { float x, v; Eval(now, x, v); return std::clamp(x, 0.f, 1.05f); }
    float ColumnWidth(double now) const { const float p = Progress(now); return Compact::width + (panel - Compact::width) * p; }
    // Which layout to draw: folding keeps the complete one for the titles' 80 ms, unfolding switches at once.
    bool CompleteLayout(double now) const { return target == Mode::Complete || (startMs >= 0 && now - startMs < Timing::titlesOutMs); }
    // Opacity of the complete column's titles and labels.
    bool TitlesMoving(double now) const {
        if (startMs < 0) return false;
        if (target == Mode::Compact) return now - startMs < Timing::titlesOutMs;
        return titlesFrom < 0 || now - titlesFrom < Timing::titlesInMs;
    }
    float Titles(double now) const {
        if (startMs < 0) return target == Mode::Complete ? 1.f : 0.f;
        if (target == Mode::Compact) return titlesAtStart * static_cast<float>(1 - std::clamp((now - startMs) / Timing::titlesOutMs, 0.0, 1.0));
        if (titlesFrom < 0) return 0.f;   // under 70 % of the width: no titles yet
        return static_cast<float>(std::clamp((now - titlesFrom) / Timing::titlesInMs, 0.0, 1.0));
    }
    // Every frame: the titles' fade-in starts when the width passes 70 %.
    void Update(double now) { if (target == Mode::Complete && startMs >= 0 && titlesFrom < 0 && Progress(now) >= .7f) titlesFrom = now; }
    // `velocity` (progress per second) and `damping`: a thrown fold (the edge released with momentum); 0 / 1 otherwise.
    void Set(Mode m, double now, bool animate, float throwVelocity = 0, float throwDamping = 1) {
        if (m == target && startMs < 0) return;
        float x, v; Eval(now, x, v);
        titlesAtStart = Titles(now);
        target = m;
        if (!animate) { startMs = -1; from = Settled(); velocity = 0; titlesFrom = -1; return; }
        from = x; velocity = throwVelocity != 0 ? throwVelocity : v; damping = throwDamping; response = throwDamping < 1 ? Timing::throwResponse : Timing::foldResponse;
        startMs = now; titlesFrom = -1;
    }
    void Settle(double now) { if (startMs >= 0 && !Moving(now)) startMs = -1; }
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
