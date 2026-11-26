#pragma once
#include "i18n.h"
// #138 A favorites folder's look: a colour and an emblem (Paper « Browser Design », page « #138 — Dossiers de
// favoris », board « Dossiers de favoris · C finale (validée) » 21XK-0, variant C validated by Val: emblems in a
// darker shade of the folder's colour). Pure C++17 (no HWND, Direct2D or WebView2): the palette, the 24 icons,
// how a stored look resolves into colours, the sizes each surface draws, the picker's geometry and its keyboard
// model. tests/folder_style_test.cpp checks it. Drawn by Chrome::FolderLook / Chrome::PaintFolderPicker.
//  - The folder is Phosphor Fill 2.1.1 `folder` (MIT) filled with the colour; the emblem, another Phosphor Fill
//    glyph, sits inside at translate(78,92) scale(0.39) of the 256 box, in the darker shade (colour x 0.42).
//  - Colours: the profiles' palette (Paper tokens --color-profile-*), or none. An icon without a colour stays
//    grey (#B9B9B9, emblem #4E4E4E, board 240X-0); neither colour nor icon is the grey folder of today.
//  - Stored as keys (src/bookmarks.h, file version 2). A key this build does not know draws as none.
#include <cstdint>
#include <string>

namespace FolderStyle {

struct Color { const char* key; I18n::Text label; std::uint32_t folder, emblem; };   // 0xRRGGBB
inline constexpr Color colors[] = {
    {"gris", {I18n::Id::folder_color_grey}, 0xB3B3B3, 0x4B4B4B},
    {"vert", {I18n::Id::folder_color_green}, 0x6FD087, 0x2F5739},
    {"cyan", {I18n::Id::folder_color_cyan}, 0x4FC3D9, 0x21525B},
    {"violet", {I18n::Id::folder_color_violet}, 0xB49BFF, 0x4C416B},
    {"rose", {I18n::Id::folder_color_pink}, 0xF08BC4, 0x653A52},
    {"ambre", {I18n::Id::folder_color_amber}, 0xE6AC3D, 0x61481A},
};
inline constexpr int colorCount = static_cast<int>(sizeof(colors) / sizeof(colors[0]));
inline constexpr std::uint32_t noneFolder = 0xB9B9B9, noneEmblem = 0x4E4E4E;   // « Sans couleur » (board 23XJ-0, 240X-0)

// The 24 choices in the picker's order (8 x 3). The first, the plain folder, has no emblem.
struct Icon { const char* key; I18n::Text label; const char* emblem; };
inline constexpr Icon icons[] = {
    {"folder", {I18n::Id::folder_icon_folder}, nullptr},
    {"briefcase", {I18n::Id::folder_icon_briefcase}, "M152,112a8,8,0,0,1-8,8H112a8,8,0,0,1,0-16h32A8,8,0,0,1,152,112Zm80-40V200a16,16,0,0,1-16,16H40a16,16,0,0,1-16-16V72A16,16,0,0,1,40,56H80V48a24,24,0,0,1,24-24h48a24,24,0,0,1,24,24v8h40A16,16,0,0,1,232,72ZM96,56h64V48a8,8,0,0,0-8-8H104a8,8,0,0,0-8,8Zm120,57.61V72H40v41.61A184,184,0,0,0,128,136,184,184,0,0,0,216,113.61Z"},
    {"house", {I18n::Id::folder_icon_house}, "M224,120v96a8,8,0,0,1-8,8H160a8,8,0,0,1-8-8V164a4,4,0,0,0-4-4H108a4,4,0,0,0-4,4v52a8,8,0,0,1-8,8H40a8,8,0,0,1-8-8V120a16,16,0,0,1,4.69-11.31l80-80a16,16,0,0,1,22.62,0l80,80A16,16,0,0,1,224,120Z"},
    {"star", {I18n::Id::folder_icon_star}, "M234.29,114.85l-45,38.83L203,211.75a16.4,16.4,0,0,1-24.5,17.82L128,198.49,77.47,229.57A16.4,16.4,0,0,1,53,211.75l13.76-58.07-45-38.83A16.46,16.46,0,0,1,31.08,86l59-4.76,22.76-55.08a16.36,16.36,0,0,1,30.27,0l22.75,55.08,59,4.76a16.46,16.46,0,0,1,9.37,28.86Z"},
    {"heart", {I18n::Id::folder_icon_heart}, "M240,102c0,70-103.79,126.66-108.21,129a8,8,0,0,1-7.58,0C119.79,228.66,16,172,16,102A62.07,62.07,0,0,1,78,40c20.65,0,38.73,8.88,50,23.89C139.27,48.88,157.35,40,178,40A62.07,62.07,0,0,1,240,102Z"},
    {"code", {I18n::Id::folder_icon_code}, "M216,40H40A16,16,0,0,0,24,56V200a16,16,0,0,0,16,16H216a16,16,0,0,0,16-16V56A16,16,0,0,0,216,40ZM92.8,145.6a8,8,0,1,1-9.6,12.8l-32-24a8,8,0,0,1,0-12.8l32-24a8,8,0,0,1,9.6,12.8L69.33,128Zm58.89-71.4-32,112a8,8,0,1,1-15.38-4.4l32-112a8,8,0,0,1,15.38,4.4Zm53.11,60.2-32,24a8,8,0,0,1-9.6-12.8L186.67,128,163.2,110.4a8,8,0,1,1,9.6-12.8l32,24a8,8,0,0,1,0,12.8Z"},
    {"book-open", {I18n::Id::folder_icon_book_open}, "M240,56V200a8,8,0,0,1-8,8H160a24,24,0,0,0-24,23.94,7.9,7.9,0,0,1-5.12,7.55A8,8,0,0,1,120,232a24,24,0,0,0-24-24H24a8,8,0,0,1-8-8V56a8,8,0,0,1,8-8H88a32,32,0,0,1,32,32v87.73a8.17,8.17,0,0,0,7.47,8.25,8,8,0,0,0,8.53-8V80a32,32,0,0,1,32-32h64A8,8,0,0,1,240,56Z"},
    {"music-notes", {I18n::Id::folder_icon_music_notes}, "M212.92,17.71a7.89,7.89,0,0,0-6.86-1.46l-128,32A8,8,0,0,0,72,56V166.1A36,36,0,1,0,88,196V102.25l112-28V134.1A36,36,0,1,0,216,164V24A8,8,0,0,0,212.92,17.71Z"},
    {"shopping-cart", {I18n::Id::folder_icon_shopping_cart}, "M230.14,58.87A8,8,0,0,0,224,56H62.68L56.6,22.57A8,8,0,0,0,48.73,16H24a8,8,0,0,0,0,16h18L67.56,172.29a24,24,0,0,0,5.33,11.27,28,28,0,1,0,44.4,8.44h45.42A27.75,27.75,0,0,0,160,204a28,28,0,1,0,28-28H91.17a8,8,0,0,1-7.87-6.57L80.13,152h116a24,24,0,0,0,23.61-19.71l12.16-66.86A8,8,0,0,0,230.14,58.87ZM104,204a12,12,0,1,1-12-12A12,12,0,0,1,104,204Zm96,0a12,12,0,1,1-12-12A12,12,0,0,1,200,204Z"},
    {"airplane", {I18n::Id::folder_icon_airplane}, "M240,136v32a8,8,0,0,1-8,8,7.61,7.61,0,0,1-1.57-.16L156,161v23.73l17.66,17.65A8,8,0,0,1,176,208v24a8,8,0,0,1-11,7.43l-37-14.81L91,239.43A8,8,0,0,1,80,232V208a8,8,0,0,1,2.34-5.66L100,184.69V161L25.57,175.84A7.61,7.61,0,0,1,24,176a8,8,0,0,1-8-8V136a8,8,0,0,1,4.42-7.16L100,89.06V44a28,28,0,0,1,56,0V89.06l79.58,39.78A8,8,0,0,1,240,136Z"},
    {"game-controller", {I18n::Id::folder_icon_game_controller}, "M247.44,173.75a.68.68,0,0,0,0-.14L231.05,89.44c0-.06,0-.12,0-.18A60.08,60.08,0,0,0,172,40H83.89a59.88,59.88,0,0,0-59,49.52L8.58,173.61a.68.68,0,0,0,0,.14,36,36,0,0,0,60.9,31.71l.35-.37L109.52,160h37l39.71,45.09c.11.13.23.25.35.37A36.08,36.08,0,0,0,212,216a36,36,0,0,0,35.43-42.25ZM104,112H96v8a8,8,0,0,1-16,0v-8H72a8,8,0,0,1,0-16h8V88a8,8,0,0,1,16,0v8h8a8,8,0,0,1,0,16Zm40-8a8,8,0,0,1,8-8h24a8,8,0,0,1,0,16H152A8,8,0,0,1,144,104Zm84.37,87.47a19.84,19.84,0,0,1-12.9,8.23A20.09,20.09,0,0,1,198,194.31L167.8,160H172a60,60,0,0,0,51-28.38l8.74,45A19.82,19.82,0,0,1,228.37,191.47Z"},
    {"graduation-cap", {I18n::Id::folder_icon_graduation_cap}, "M176,207.24a119,119,0,0,0,16-7.73V240a8,8,0,0,1-16,0Zm11.76-88.43-56-29.87a8,8,0,0,0-7.52,14.12L171,128l17-9.06Zm64-29.87-120-64a8,8,0,0,0-7.52,0l-120,64a8,8,0,0,0,0,14.12L32,117.87v48.42a15.91,15.91,0,0,0,4.06,10.65C49.16,191.53,78.51,216,128,216a130,130,0,0,0,48-8.76V130.67L171,128l-43,22.93L43.83,106l0,0L25,96,128,41.07,231,96l-18.78,10-.06,0L188,118.94a8,8,0,0,1,4,6.93v73.64a115.63,115.63,0,0,0,27.94-22.57A15.91,15.91,0,0,0,224,166.29V117.87l27.76-14.81a8,8,0,0,0,0-14.12Z"},
    {"camera", {I18n::Id::folder_icon_camera}, "M208,56H180.28L166.65,35.56A8,8,0,0,0,160,32H96a8,8,0,0,0-6.65,3.56L75.71,56H48A24,24,0,0,0,24,80V192a24,24,0,0,0,24,24H208a24,24,0,0,0,24-24V80A24,24,0,0,0,208,56Zm-44,76a36,36,0,1,1-36-36A36,36,0,0,1,164,132Z"},
    {"palette", {I18n::Id::folder_icon_palette}, "M200.77,53.89A103.27,103.27,0,0,0,128,24h-1.07A104,104,0,0,0,24,128c0,43,26.58,79.06,69.36,94.17A32,32,0,0,0,136,192a16,16,0,0,1,16-16h46.21a31.81,31.81,0,0,0,31.2-24.88,104.43,104.43,0,0,0,2.59-24A103.28,103.28,0,0,0,200.77,53.89ZM84,168a12,12,0,1,1,12-12A12,12,0,0,1,84,168Zm0-56a12,12,0,1,1,12-12A12,12,0,0,1,84,112Zm44-24a12,12,0,1,1,12-12A12,12,0,0,1,128,88Zm44,24a12,12,0,1,1,12-12A12,12,0,0,1,172,112Z"},
    {"film-strip", {I18n::Id::folder_icon_film_strip}, "M216,40H40A16,16,0,0,0,24,56V200a16,16,0,0,0,16,16H216a16,16,0,0,0,16-16V56A16,16,0,0,0,216,40ZM184,56h32V72H184ZM72,200H40V184H72ZM72,72H40V56H72Zm48,128H88V184h32Zm0-128H88V56h32Zm48,128H136V184h32Zm0-128H136V56h32Zm48,128H184V184h32v16Z"},
    {"newspaper", {I18n::Id::folder_icon_newspaper}, "M216,48H56A16,16,0,0,0,40,64V184a8,8,0,0,1-16,0V88A8,8,0,0,0,8,88v96.11A24,24,0,0,0,32,208H208a24,24,0,0,0,24-24V64A16,16,0,0,0,216,48ZM176,152H96a8,8,0,0,1,0-16h80a8,8,0,0,1,0,16Zm0-32H96a8,8,0,0,1,0-16h80a8,8,0,0,1,0,16Z"},
    {"chat-circle", {I18n::Id::folder_icon_chat_circle}, "M232,128A104,104,0,0,1,79.12,219.82L45.07,231.17a16,16,0,0,1-20.24-20.24l11.35-34.05A104,104,0,1,1,232,128Z"},
    {"coffee", {I18n::Id::folder_icon_coffee}, "M208,80H32a8,8,0,0,0-8,8v48a96.3,96.3,0,0,0,32.54,72H32a8,8,0,0,0,0,16H208a8,8,0,0,0,0-16H183.46a96.59,96.59,0,0,0,27-40.09A40,40,0,0,0,248,128v-8A40,40,0,0,0,208,80Zm24,48a24,24,0,0,1-17.2,23,95.78,95.78,0,0,0,1.2-15V97.38A24,24,0,0,1,232,120ZM112,56V24a8,8,0,0,1,16,0V56a8,8,0,0,1-16,0Zm32,0V24a8,8,0,0,1,16,0V56a8,8,0,0,1-16,0ZM80,56V24a8,8,0,0,1,16,0V56a8,8,0,0,1-16,0Z"},
    {"lightbulb", {I18n::Id::folder_icon_lightbulb}, "M176,232a8,8,0,0,1-8,8H88a8,8,0,0,1,0-16h80A8,8,0,0,1,176,232Zm40-128a87.55,87.55,0,0,1-33.64,69.21A16.24,16.24,0,0,0,176,186v6a16,16,0,0,1-16,16H96a16,16,0,0,1-16-16v-6a16,16,0,0,0-6.23-12.66A87.59,87.59,0,0,1,40,104.49C39.74,56.83,78.26,17.14,125.88,16A88,88,0,0,1,216,104Zm-32.11-9.34a57.6,57.6,0,0,0-46.56-46.55,8,8,0,0,0-2.66,15.78c16.57,2.79,30.63,16.85,33.44,33.45A8,8,0,0,0,176,104a9,9,0,0,0,1.35-.11A8,8,0,0,0,183.89,94.66Z"},
    {"rocket", {I18n::Id::folder_icon_rocket}, "M152,224a8,8,0,0,1-8,8H112a8,8,0,0,1,0-16h32A8,8,0,0,1,152,224Zm71.62-68.17-12.36,55.63a16,16,0,0,1-25.51,9.11L158.51,200h-61L70.25,220.57a16,16,0,0,1-25.51-9.11L32.38,155.83a16.09,16.09,0,0,1,3.32-13.71l28.56-34.26a123.07,123.07,0,0,1,8.57-36.67c12.9-32.34,36-52.63,45.37-59.85a16,16,0,0,1,19.6,0c9.34,7.22,32.47,27.51,45.37,59.85a123.07,123.07,0,0,1,8.57,36.67l28.56,34.26A16.09,16.09,0,0,1,223.62,155.83Zm-139.23,34Q68.28,160.5,64.83,132.16L48,152.36,60.36,208l.18-.13ZM140,100a12,12,0,1,0-12,12A12,12,0,0,0,140,100Zm68,52.36-16.83-20.2q-3.42,28.28-19.56,57.69l23.85,18,.18.13Z"},
    {"globe", {I18n::Id::folder_icon_globe}, "M128,24h0A104,104,0,1,0,232,128,104.12,104.12,0,0,0,128,24Zm78.36,64H170.71a135.28,135.28,0,0,0-22.3-45.6A88.29,88.29,0,0,1,206.37,88ZM216,128a87.61,87.61,0,0,1-3.33,24H174.16a157.44,157.44,0,0,0,0-48h38.51A87.61,87.61,0,0,1,216,128ZM128,43a115.27,115.27,0,0,1,26,45H102A115.11,115.11,0,0,1,128,43ZM102,168H154a115.11,115.11,0,0,1-26,45A115.27,115.27,0,0,1,102,168Zm-3.9-16a140.84,140.84,0,0,1,0-48h59.88a140.84,140.84,0,0,1,0,48Zm50.35,61.6a135.28,135.28,0,0,0,22.3-45.6h35.66A88.29,88.29,0,0,1,148.41,213.6Z"},
    {"leaf", {I18n::Id::folder_icon_leaf}, "M223.45,40.07a8,8,0,0,0-7.52-7.52C139.8,28.08,78.82,51,52.82,94a87.09,87.09,0,0,0-12.76,49A101.72,101.72,0,0,0,46.7,175.2a4,4,0,0,0,6.61,1.43l85-86.3a8,8,0,0,1,11.32,11.32L56.74,195.94,42.55,210.13a8.2,8.2,0,0,0-.6,11.1,8,8,0,0,0,11.71.43l16.79-16.79c14.14,6.84,28.41,10.57,42.56,11.07q1.67.06,3.33.06A86.93,86.93,0,0,0,162,203.18C205,177.18,227.93,116.21,223.45,40.07Z"},
    {"paw-print", {I18n::Id::folder_icon_paw_print}, "M240,108a28,28,0,1,1-28-28A28,28,0,0,1,240,108ZM72,108a28,28,0,1,0-28,28A28,28,0,0,0,72,108ZM92,88A28,28,0,1,0,64,60,28,28,0,0,0,92,88Zm72,0a28,28,0,1,0-28-28A28,28,0,0,0,164,88Zm23.12,60.86a35.3,35.3,0,0,1-16.87-21.14,44,44,0,0,0-84.5,0A35.25,35.25,0,0,1,69,148.82,40,40,0,0,0,88,224a39.48,39.48,0,0,0,15.52-3.13,64.09,64.09,0,0,1,48.87,0,40,40,0,0,0,34.73-72Z"},
    {"lightning", {I18n::Id::folder_icon_lightning}, "M213.85,125.46l-112,120a8,8,0,0,1-13.69-7l14.66-73.33L45.19,143.49a8,8,0,0,1-3-13l112-120a8,8,0,0,1,13.69,7L153.18,90.9l57.63,21.61a8,8,0,0,1,3,12.95Z"},
};
inline constexpr int iconCount = static_cast<int>(sizeof(icons) / sizeof(icons[0]));
// Phosphor Fill 2.1.1 `folder`, exactly as the board draws it (P0, viewBox 256).
inline constexpr const char* folderPath = "M216,72H131.31L104,44.69A15.88,15.88,0,0,0,92.69,40H40A16,16,0,0,0,24,56V200.62A15.41,15.41,0,0,0,39.39,216h177.5A15.13,15.13,0,0,0,232,200.89V88A16,16,0,0,0,216,72ZM40,56H92.69l16,16H40Z";
inline constexpr float emblemX = 78, emblemY = 92, emblemScale = .39f;   // board: <g transform="translate(78,92) scale(0.39)">

inline int ColorIndex(const std::string& key) { for (int i = 0; i < colorCount; ++i) if (key == colors[i].key) return i; return -1; }   // -1: none
inline int IconIndex(const std::string& key) { for (int i = 1; i < iconCount; ++i) if (key == icons[i].key) return i; return 0; }      // 0: the plain folder
inline const char* ColorKey(int index) { return index >= 0 && index < colorCount ? colors[index].key : ""; }
inline const char* IconKey(int index) { return index > 0 && index < iconCount ? icons[index].key : ""; }
inline const char* PalettePath() { return icons[IconIndex("palette")].emblem; }   // the drawer's « Personnaliser » button (board 23DE-0)

// What a stored look draws. `styled` false: the surface keeps its own grey folder (no colour, no icon).
struct Look { bool styled = false; std::uint32_t folder = 0, emblem = 0; const char* emblemPath = nullptr; };
inline Look Resolve(int color, int icon) {
    Look l; if (color < 0 && icon <= 0) return l;
    l.styled = true; l.folder = color >= 0 ? colors[color].folder : noneFolder; l.emblem = color >= 0 ? colors[color].emblem : noneEmblem;
    l.emblemPath = icon > 0 && icon < iconCount ? icons[icon].emblem : nullptr;
    return l;
}
inline Look Resolve(const std::string& icon, const std::string& color) { return Resolve(ColorIndex(color), IconIndex(icon)); }

// Glyph sizes (DIP) per surface, board section 6 « Le dossier partout » and 5. `withEmblem`: the folder carries an
// icon; a folder without one is drawn smaller, like the board's plain folders (the picker's first cell 23MC-0 16,
// the drawer's « Divers » 23DT-0 16, the bar's 244L-0 14, the menu's 246Z-0 16).
namespace Size {
inline constexpr float bar = 16, barPlain = 14;        // top favorites bar (243X-0)
inline constexpr float tile = 20, tilePlain = 16;      // side panel grid and compact column tiles (2437-0, 244U-0)
inline constexpr float drawer = 18, drawerPlain = 16;  // Favoris drawer tile (245I-0)
inline constexpr float menu = 18, menuPlain = 16;      // menus « Déplacer vers » and folder menus (246B-0)
inline constexpr float cell = 18, cellPlain = 16;      // picker cells (23MF-0 / 23MC-0) and the popover's header (23SG-0)
inline float For(float withEmblem, float plain, const Look& l) { return l.emblemPath ? withEmblem : plain; }
}

// Long names (board section 5): cut under a 24 DIP fade into the surface, never « … ».
inline constexpr float nameFade = 24;

// ---- The picker (board 23F4-0 in the drawer, 23SD-0 as a popover) ------------------------------------------
namespace Picker {
inline constexpr int swatches = 1 + colorCount;        // « Sans couleur » first (23F6-0), then the palette
inline constexpr int cells = iconCount, columns = 8, rows = (cells + columns - 1) / columns;
inline constexpr int count = swatches + cells;         // focus stops: swatches 0..6, cells 7..30
inline constexpr float padding = 10, gap = 10, popoverGap = 8;
inline constexpr float swatch = 24, swatchDot = 18, swatchGap = 8, selectedRing = 1.5f;   // 23FK-0: #FFFFFF 1.5 inset
inline constexpr float cell = 26, cellGap = 4, cellRadius = 6, cellRing = 1, focusRing = 2;   // 23OC-0, focus 23OW-0
inline constexpr float hintSize = 12, hintLine = 16;
inline constexpr std::uint32_t surface = 0x1E1E1E, restGlyph = 0x8C8C8C, restEmblem = 0x3B3B3B, focusColor = 0xE6E6E6, hint = 0x8C8C8C;
inline constexpr std::uint32_t noneDot = 0x2A2A2A, noneRing = 0x3A3A3A, noneSlash = 0x8C8C8C;   // 23F7-0 / 23F8-0
inline constexpr float selectedFill = .20f;            // a chosen cell: its folder colour at 20 % over #1E1E1E (#463A24, #3D3D3D)
// The popover (23SD-0): 288 wide, #1E1E1E, a 1 DIP #2E2E2E border, radius 8; a header row (32 tile #202020 radius 6,
// the folder 18, its name 14/18 semibold white, 8 apart) over the swatches and the grid, 8 apart.
inline constexpr float popoverWidth = 288, popoverRadius = 8, border = 1, headerTile = 32, headerGap = 8, nameSize = 14, nameLine = 18;
inline constexpr std::uint32_t popoverBorder = 0x2E2E2E, headerTileFill = 0x202020;
inline constexpr float gridWidth = columns * cell + (columns - 1) * cellGap;     // 236
inline constexpr float gridHeight = rows * cell + (rows - 1) * cellGap;          // 86
inline constexpr float swatchesWidth = swatches * swatch + (swatches - 1) * swatchGap;
inline constexpr float PopoverHeight() { return border + padding + headerTile + popoverGap + swatch + popoverGap + gridHeight + padding + border; }
// Inside the drawer: swatches, grid and hint under each other, 10 apart, 10 of padding.
inline constexpr float DrawerHeight(float hintHeight) { return padding + swatch + gap + gridHeight + gap + hintHeight + padding; }

struct Box { float left = 0, top = 0, right = 0, bottom = 0; };
// Focus stop `index` relative to the swatches' row top-left `x`, `y` and the grid's top `gridTop`.
inline Box Stop(int index, float x, float y, float gridTop) {
    if (index < swatches) { const float l = x + index * (swatch + swatchGap); return {l, y, l + swatch, y + swatch}; }
    const int c = index - swatches, col = c % columns, row = c / columns;
    const float l = x + col * (cell + cellGap), t = gridTop + row * (cell + cellGap);
    return {l, t, l + cell, t + cell};
}
inline int HitStop(float px, float py, float x, float y, float gridTop) {
    for (int i = 0; i < count; ++i) { const Box b = Stop(i, x, y, gridTop); if (px >= b.left && px < b.right && py >= b.top && py < b.bottom) return i; }
    return -1;
}

// Keyboard model (board 2: « Tab et flèches pour se déplacer, Entrée pour choisir, Échap pour fermer. »). Two groups,
// one stop each in the Tab order: the swatches and the grid. Tab / Maj+Tab go to the other group, on its chosen item;
// arrows move inside a group (Left/Right one step in reading order, Up/Down a grid row; Down from the swatches enters
// the grid's first row, Up from it returns to the swatch above); Home/End its first/last; Enter or Space chooses,
// which applies at once (the picker stays open); Escape closes.
enum class Key { Tab, BackTab, Left, Right, Up, Down, Home, End, Choose, Close };
enum class Outcome { None, Moved, Chosen, Unchanged, Close };
struct Model {
    int color = -1, icon = 0;   // the folder's look: colour index (-1 none), icon index (0 the plain folder)
    int focus = -1;             // focus stop, -1 none (pointer use)
    static constexpr int SwatchOf(int color) { return color + 1; }
    static constexpr int CellOf(int icon) { return swatches + icon; }
    bool InSwatches() const { return focus >= 0 && focus < swatches; }
    void FocusChosen() { focus = SwatchOf(color); }
    // Chooses stop `index` (click, Enter): Chosen when the look changed.
    Outcome Choose(int index) {
        if (index < 0 || index >= count) return Outcome::None;
        focus = index;
        if (index < swatches) { const int c = index - 1; if (c == color) return Outcome::Unchanged; color = c; return Outcome::Chosen; }
        const int i = index - swatches; if (i == icon) return Outcome::Unchanged; icon = i; return Outcome::Chosen;
    }
    Outcome Press(Key k) {
        if (k == Key::Close) return Outcome::Close;
        if (focus < 0 || focus >= count) { FocusChosen(); return Outcome::Moved; }   // the first key from the pointer: the chosen colour
        const int from = focus;
        if (k == Key::Choose) return Choose(focus);
        if (k == Key::Tab || k == Key::BackTab) focus = InSwatches() ? CellOf(icon) : SwatchOf(color);
        else if (InSwatches()) {
            if (k == Key::Left) focus = focus > 0 ? focus - 1 : 0;
            else if (k == Key::Right) focus = focus < swatches - 1 ? focus + 1 : swatches - 1;
            else if (k == Key::Home) focus = 0;
            else if (k == Key::End) focus = swatches - 1;
            else if (k == Key::Down) focus = CellOf(focus);   // the cell under it (row 0, same column)
        } else {
            const int c = focus - swatches;
            if (k == Key::Left) focus = swatches + (c > 0 ? c - 1 : 0);
            else if (k == Key::Right) focus = swatches + (c < cells - 1 ? c + 1 : cells - 1);
            else if (k == Key::Home) focus = swatches;
            else if (k == Key::End) focus = swatches + cells - 1;
            else if (k == Key::Down) focus = c + columns < cells ? focus + columns : focus;
            else if (k == Key::Up) focus = c >= columns ? focus - columns : (c < swatches ? c : swatches - 1);   // row 0: the swatch above
        }
        return focus != from ? Outcome::Moved : Outcome::None;
    }
};
}

} // namespace FolderStyle
