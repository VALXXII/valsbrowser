#pragma once
#include "i18n.h"
// "Son des sites": popover under the toolbar's audio button (chrome.h 'V') and, from its
// "Voir tout (N)" button, a drawer docked on the window's right edge under the chrome.
// Paper « Browser Design », page « Son des sites — volume par site » (p-G-0): popover
// TJ2-0 / TGK-0 (panel TH9-0), tooltip TJ0-0 (T9Q-0), drawer U40-0 (U5V-0).
//
// Popover: 320 DIP, 1 DIP #FFFFFF0F border, radius 8 (window region, no shadow), padding 16,
// no title (the window's accessible name carries it), close 24 at right 11 / top 10 (14 DIP
// glyph, 50 %). At most two rows; with more sites, a 32 DIP « Voir tout (N) » button
// (#292929, radius 6, 14 medium at 50 %) opens the drawer. Rows are separated by a 1 DIP
// #DDDDDD 10 % rule with 16 DIP on each side.
// Row: 40 DIP tile (Media Session artwork cropped to fill, else the favicon on #292929),
// title 14 medium, then the artist (music) or « Sons et musiques » + an info glyph whose
// tooltip explains it (native Tooltip::Tip, Paper copy). Controls (24 DIP high): mute 24,
// sixteen 2 x 12 DIP ticks spread evenly, value 36 DIP (13 medium, 50 %, centred).
// Drawer: 320 DIP from the right edge, full height under the chrome, #FFFFFF05 left rule,
// padding 16, every row (wheel scrolls), close 24 at right 14 / top 14 (16 DIP glyph).
//
// The ticks are only a picture of the volume. The value is continuous (1 % steps, any of
// 0..100 %), set 1:1 by the pointer anywhere on the tick lane, by the keyboard (arrows 1 %,
// Page 10 %, Home/End) or the wheel (5 %), and it is the value applied to the pages and shown
// as a percentage. The picture lights the tick nearest to the value (72 % -> tick 11 of 0..15)
// and never feeds back into it. Lit ticks are the accent; the current one is 16 DIP tall with
// a soft accent glow. The lit position glides to its new tick (~40 ms time constant) so a
// drag reads as one continuous sweep; reduced motion snaps it.
// UI Automation: a group, per row an « Afficher l'onglet » button, a mute toggle, a Slider
// (RangeValue 0..100, small change 1, large 10) and, for short-sound sites, an info button
// whose help text is the tooltip; then « Voir tout » and « Fermer ».
#include <windows.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "chrome.h"
#include "native-ui.h"
#include "accessibility.h"
#include "tooltip.h"
#include "audio-mixer.h"

namespace NativeChrome {
namespace AudioPanel {
// Phosphor Bold 2.1.1 (MIT), 256-unit paths drawn through Chrome::PathGlyph.
namespace Glyph {
inline constexpr char high[] = "M157.27,21.22a12,12,0,0,0-12.64,1.31L75.88,76H32A20,20,0,0,0,12,96v64a20,20,0,0,0,20,20H75.88l68.75,53.47A12,12,0,0,0,164,224V32A12,12,0,0,0,157.27,21.22ZM36,100H68v56H36Zm104,99.46L92,162.13V93.87l48-37.33ZM212,128a44,44,0,0,1-11,29.11,12,12,0,1,1-18-15.88,20,20,0,0,0,0-26.43,12,12,0,0,1,18-15.86A43.94,43.94,0,0,1,212,128Zm40,0a83.87,83.87,0,0,1-21.39,56,12,12,0,0,1-17.89-16,60,60,0,0,0,0-80,12,12,0,1,1,17.88-16A83.87,83.87,0,0,1,252,128Z";
inline constexpr char low[] = "M157.27,21.22a12,12,0,0,0-12.64,1.31L75.88,76H32A20,20,0,0,0,12,96v64a20,20,0,0,0,20,20H75.88l68.75,53.47A12,12,0,0,0,164,224V32A12,12,0,0,0,157.27,21.22ZM36,100H68v56H36Zm104,99.46L92,162.13V93.87l48-37.33ZM212,128a44,44,0,0,1-11,29.11,12,12,0,0,1-18-15.88,20,20,0,0,0,0-26.44,12,12,0,0,1,18-15.85A43.94,43.94,0,0,1,212,128Z";
inline constexpr char off[] = "M157.27,21.22a12,12,0,0,0-12.64,1.31L75.88,76H32A20,20,0,0,0,12,96v64a20,20,0,0,0,20,20H75.88l68.75,53.47A12,12,0,0,0,164,224V32A12,12,0,0,0,157.27,21.22ZM36,100H68v56H36Zm104,99.47L92,162.12V93.88l48-37.34Zm108.49-55.95a12,12,0,0,1-17,17L216,145l-15.51,15.52a12,12,0,0,1-17-17L199,128l-15.52-15.51a12,12,0,0,1,17-17L216,111l15.51-15.51a12,12,0,0,1,17,17L233,128Z";
// Toolbar statics: speaker-simple-high (music, reduced motion) and -low (short sounds, idle).
inline constexpr char simpleHigh[] = "M165.27,21.22a12,12,0,0,0-12.64,1.31L83.88,76H40A20,20,0,0,0,20,96v64a20,20,0,0,0,20,20H83.88l68.75,53.47A12,12,0,0,0,172,224V32A12,12,0,0,0,165.27,21.22ZM148,199.47,95.37,158.53A12,12,0,0,0,88,156H44V100H88a12,12,0,0,0,7.37-2.53L148,56.54ZM212,104v48a12,12,0,0,1-24,0V104a12,12,0,0,1,24,0Zm36-16v80a12,12,0,0,1-24,0V88a12,12,0,0,1,24,0Z";
inline constexpr char simpleLow[] = "M165.27,21.22a12,12,0,0,0-12.64,1.31L83.88,76H40A20,20,0,0,0,20,96v64a20,20,0,0,0,20,20H83.88l68.75,53.47A12,12,0,0,0,172,224V32A12,12,0,0,0,165.27,21.22ZM148,199.46,95.37,158.53A12,12,0,0,0,88,156H44V100H88a12,12,0,0,0,7.37-2.53L148,56.54ZM212,104v48a12,12,0,0,1-24,0V104a12,12,0,0,1,24,0Z";
inline constexpr char close[] = "M208.49,191.51a12,12,0,0,1-17,17L128,145,64.49,208.49a12,12,0,0,1-17-17L111,128,47.51,64.49a12,12,0,0,1,17-17L128,111l63.51-63.52a12,12,0,0,1,17,17L145,128Z";
inline constexpr char info[] = "M108,84a16,16,0,1,1,16,16A16,16,0,0,1,108,84Zm128,44A108,108,0,1,1,128,20,108.12,108.12,0,0,1,236,128Zm-24,0a84,84,0,1,0-84,84A84.09,84.09,0,0,0,212,128Zm-72,36.68V132a20,20,0,0,0-20-20,12,12,0,0,0-4,23.32V168a20,20,0,0,0,20,20,12,12,0,0,0,4-23.32Z";
inline const char* ForPercent(int percent) { return percent <= 0 ? off : percent < 50 ? low : high; }
}
// Paper copy (TJ1-0) for « Sons et musiques ».
inline const wchar_t* SoundsLabel() { return TR(audio_sounds_label); }
inline const wchar_t* SoundsTip() { return TR(audio_sounds_tip); }

namespace Layout {
inline constexpr float width = 320, border = 1, padding = 16, inset = border + padding, right = width - inset; // content 17..303
inline constexpr float radius = NativeUI::nativeCornerRadius;
inline constexpr float tile = 40, tileGap = 12, textLeft = inset + tile + tileGap;          // 69
inline constexpr float control = Design::Size::compact, controlTop = tile + Design::Space::md; // 48..72
inline constexpr float rowHeight = controlTop + control, ruleGap = 16, pitch = rowHeight + ruleGap + 1 + ruleGap; // 72, 105
inline constexpr float valueWidth = 36, valueLeft = right - valueWidth;                       // 267..303
inline constexpr float muteRight = inset + control;                                            // 41
inline constexpr int ticks = 16;
inline constexpr float tickWidth = 2, tickHeight = 12, tickCurrent = 16;
// justify-content: space-between over mute, 16 ticks and the value (Paper THG-0).
inline constexpr float tickGap = (right - inset - control - valueWidth - ticks * tickWidth) / (ticks + 1); // 11.41
inline constexpr float TickLeft(int i) { return muteRight + tickGap + i * (tickWidth + tickGap); }
inline constexpr float TickCenter(int i) { return TickLeft(i) + tickWidth / 2; }
inline constexpr int popoverRows = 2;
inline constexpr float viewAll = 32, viewAllGap = 16;
inline constexpr float anchorGap = 8, enterMs = 180, enterSlide = 6, drawerEnterMs = 220, drawerExitMs = 150, drawerSlide = 12;
inline constexpr float popoverTop = inset, drawerTop = padding;
inline constexpr float RowTop(int i, bool drawer = false, float scroll = 0) { return (drawer ? drawerTop : popoverTop) + i * pitch - scroll; }
inline constexpr float RowsHeight(int n) { return n < 1 ? 0 : n * rowHeight + (n - 1) * (pitch - rowHeight); }
inline constexpr float EmptyHeight = 20;
inline constexpr float PopoverHeight(int n) {
    const int shown = n > popoverRows ? popoverRows : n;
    return popoverTop + (shown < 1 ? EmptyHeight : RowsHeight(shown)) + (n > popoverRows ? viewAllGap + viewAll : 0) + inset;
}
inline constexpr float ViewAllTop(int n) { return popoverTop + RowsHeight(n > popoverRows ? popoverRows : n) + viewAllGap; }
inline constexpr float DrawerContent(int n) { return drawerTop + (n < 1 ? EmptyHeight : RowsHeight(n)) + padding; }
inline constexpr Design::Box Close(bool drawer) { return drawer ? Design::Box{width - 14 - control, 14, width - 14, 14 + control} : Design::Box{width - border - 11 - control, border + 10, width - border - 11, border + 10 + control}; }
inline constexpr Design::Box Tile(float top) { return {inset, top, inset + tile, top + tile}; }
inline constexpr Design::Box RowArea(float top) { return {inset - 4, top - 4, right + 4, top + tile + 4}; }
inline constexpr Design::Box Mute(float top) { return {inset, top + controlTop, muteRight, top + rowHeight}; }
inline constexpr Design::Box Lane(float top) { return {muteRight + 2, top + controlTop, valueLeft - 2, top + rowHeight}; }      // pointer target
inline constexpr Design::Box LaneRing(float top) { return {TickLeft(0) - 6, top + controlTop, TickLeft(ticks - 1) + tickWidth + 6, top + rowHeight}; }
inline constexpr Design::Box Value(float top) { return {valueLeft, top + controlTop, right, top + rowHeight}; }
inline constexpr Design::Box ViewAllBox(int n) { return {inset, ViewAllTop(n), right, ViewAllTop(n) + viewAll}; }
// Continuous value under the pointer: tick 0's centre is 0 %, tick 15's centre is 100 %.
inline double PercentAt(float x) { return std::clamp((x - TickCenter(0)) / (TickCenter(ticks - 1) - TickCenter(0)) * 100.0, 0.0, 100.0); }
inline float XAt(double percent) { return TickCenter(0) + static_cast<float>(std::clamp(percent, 0.0, 100.0) / 100.0) * (TickCenter(ticks - 1) - TickCenter(0)); }
// The tick that pictures a value: the nearest one. A picture only: never written back.
inline int NearestTick(double percent) { return static_cast<int>(std::lround(std::clamp(percent, 0.0, 100.0) * (ticks - 1) / 100.0)); }
}
static_assert(Layout::textLeft == 69 && Layout::pitch == 105 && Layout::PopoverHeight(2) == 211 && Layout::PopoverHeight(7) == 259, "Paper TJ2-0 (211) and TGK-0 (259) panel heights");

// Hit identifiers.
constexpr int CloseHit = 1, ViewAllHit = 2, RowBase = 100;
inline int RowHit(int i) { return RowBase + i * 4; }
inline int MuteHit(int i) { return RowBase + i * 4 + 1; }
inline int SliderHit(int i) { return RowBase + i * 4 + 2; }
inline int InfoHit(int i) { return RowBase + i * 4 + 3; }
inline int HitRow(int hit) { return hit >= RowBase ? (hit - RowBase) / 4 : -1; }
inline int HitPart(int hit) { return hit >= RowBase ? (hit - RowBase) % 4 : -1; }

struct Item {
    Audio::Row row;
    std::shared_ptr<const FaviconImage> favicon, artwork;
};
// Music rows read their artist (or site); every other row says « Sons et musiques » + info.
inline bool ShowsArtist(const Audio::Row& r) { return r.kind == Audio::Kind::Media || r.playingMedia || !r.artist.empty(); }
struct Scene {
    const std::vector<Item>* items = nullptr;
    const Motion::State* motion = nullptr;
    const std::vector<float>* ticks = nullptr; // presented tick position per row (fractional while gliding)
    int focus = 0, dragging = -1; bool keyboard = false, focused = true, drawer = false;
    float entrance = 1, scroll = 0, height = 0;
    Motion::Time now{};
};

class View {
    Chrome& chrome_;
    static Motion::InteractionVisual Interaction(const Scene& s, int hit) { return s.motion ? s.motion->Visual(hit, s.now) : Motion::InteractionVisual{}; }
public:
    explicit View(Chrome& chrome) : chrome_(chrome) {}
    static D2D1_RECT_F R(Design::Box b) { return {b.left, b.top, b.right, b.bottom}; }
    float SoundsWidth() { return chrome_.TextWidth(SoundsLabel(), 13, false, false, -.08f); }
    D2D1_RECT_F InfoBox(float top) { const float x = Layout::textLeft + SoundsWidth() + Design::Space::xs, y = top + 30; return {x, y - 7, x + 14, y + 7}; }
    void Tile(const Item& item, float top) {
        const auto box = R(Layout::Tile(top));
        if (item.artwork && chrome_.DrawCover(item.artwork, box, Design::Radius::control)) return;
        chrome_.Round(box, Design::Radius::control, Design::Surface::raised);
        const float c = (box.left + box.right) / 2, m = (box.top + box.bottom) / 2;
        const D2D1_RECT_F slot{c - 8, m - 8, c + 8, m + 8};
        if (!chrome_.DrawSiteImage(item.favicon, slot)) chrome_.Icon(L"favicon", slot, Design::Size::icon, 1);
    }
    // Sixteen ticks; `presented` is the (gliding) nearest-tick position, never the value itself.
    void Ticks(int index, double percent, float presented, float top, const Scene& s) {
        auto* t = chrome_.RenderTarget(); auto* b = chrome_.SolidBrush();
        const auto accent = Design::Accent::Get();
        const bool silent = percent <= 0;
        const float cy = top + Layout::controlTop + Layout::control / 2;
        const D2D1_COLOR_F lit = silent ? D2D1::ColorF(1, 1, 1, .32f) : D2D1::ColorF(accent.r, accent.g, accent.b, 1), dim = D2D1::ColorF(0xDD / 255.f, 0xDD / 255.f, 0xDD / 255.f, .10f);
        auto mix = [](D2D1_COLOR_F a, D2D1_COLOR_F c, float u) { return D2D1::ColorF(a.r + (c.r - a.r) * u, a.g + (c.g - a.g) * u, a.b + (c.b - a.b) * u, a.a + (c.a - a.a) * u); };
        for (int i = 0; i < Layout::ticks; ++i) {
            const float current = std::max(0.f, 1 - std::abs(i - presented));               // 1 on the pictured tick
            const float on = silent ? current : std::max(current, Motion::Clamp(presented - i)); // ticks before it are lit
            const float h = Layout::tickHeight + (Layout::tickCurrent - Layout::tickHeight) * current, x = Layout::TickLeft(i);
            const D2D1_RECT_F r{x, cy - h / 2, x + Layout::tickWidth, cy + h / 2};
            if (current > .01f && !silent) // glow: accent at 50 %, 6 DIP blur, 2 DIP spread (three soft passes)
                for (int k = 3; k >= 1; --k) { const float g = 2.f * k; b->SetColor(D2D1::ColorF(accent.r, accent.g, accent.b, .5f * current * (.12f + .1f * (3 - k)))); t->FillRoundedRectangle(D2D1::RoundedRect({r.left - g, r.top - g, r.right + g, r.bottom + g}, 1 + g, 1 + g), b); }
            b->SetColor(mix(dim, lit, on)); t->FillRoundedRectangle(D2D1::RoundedRect(r, 1, 1), b);
        }
        if (s.focus == SliderHit(index) && s.focused) chrome_.FocusRing(R(Layout::LaneRing(top)), Design::Radius::control);
    }
    void Row(const Item& item, int index, float top, const Scene& s) {
        const auto& row = item.row;
        // The row (tile, title, detail) still brings its tab forward on click/Enter, but shows no
        // hover or press fill (not in Paper; user feedback): only the keyboard focus ring.
        const auto area = R(Layout::RowArea(top));
        if (s.focus == RowHit(index) && s.focused) chrome_.FocusRing(area, Design::Radius::control + 2);
        Tile(item, top);
        const float closeLeft = Layout::Close(s.drawer).left, titleRight = top < Layout::Close(s.drawer).bottom && top + 20 > Layout::Close(s.drawer).top ? closeLeft - Design::Space::xs : Layout::right;
        chrome_.Text(row.title, {Layout::textLeft, top, titleRight, top + 20}, 14, Design::Text::primary, false, true, -.14f);
        if (ShowsArtist(row)) chrome_.Text(row.artist.empty() ? row.site : row.artist, {Layout::textLeft, top + 20, Layout::right, top + Layout::tile}, 13, Design::Text::secondary, false, false, -.08f);
        else {
            chrome_.Text(SoundsLabel(), {Layout::textLeft, top + 20, Layout::right, top + Layout::tile}, 13, Design::Text::secondary, false, false, -.08f);
            const auto iv = Interaction(s, InfoHit(index)); const auto box = InfoBox(top);
            chrome_.PathGlyph(Glyph::info, box, 14, D2D1::ColorF(1, 1, 1, .5f + .4f * iv.hover));
            if (s.focus == InfoHit(index) && s.focused) chrome_.FocusRing({box.left - 3, box.top - 3, box.right + 3, box.bottom + 3}, Design::Radius::inset + 2);
        }
        // Mute: ghost 24 DIP button, glyph follows the level (off / low / high).
        const auto mv = Interaction(s, MuteHit(index)); const auto mute = R(Layout::Mute(top));
        auto style = Design::Resolve(Design::ButtonVariant::Ghost, {mv.hover, mv.press, false, s.focus == MuteHit(index) && s.focused});
        // At 100 % (accent glyph) the hover/press fill is the accent tint (--color-browser-split-tint,
        // like the toolbar button), not the grey ghost surface.
        if (row.percent >= 100 && (mv.hover > 0 || mv.press > 0)) { const auto a = Design::Accent::Get(); chrome_.FillBox(mute, style.radius, D2D1::ColorF(a.r, a.g, a.b, .16f * mv.hover + .08f * mv.press)); }
        else if (style.fill) chrome_.Round(mute, style.radius, style.surface);
        if (style.focus) chrome_.FocusRing(mute, style.radius);
        const float press = Motion::PressScale(mv.press, s.motion && s.motion->ReducedMotion());
        // Ink by state: 100 % = the accent; muted / 0 % = the speaker-x at the usual secondary ink
        // (never brighter); anything else = the usual 50 % white. Muting sets the value to 0 (the
        // level to restore is only remembered), so a muted row is never shown at 100 %.
        const auto accent = Design::Accent::Get();
        const D2D1_COLOR_F ink = row.percent >= 100 ? D2D1::ColorF(accent.r, accent.g, accent.b, 1) : D2D1::ColorF(1, 1, 1, Design::Text::secondary);
        chrome_.PathGlyph(Glyph::ForPercent(row.percent), mute, Design::Size::icon * press, ink);
        const float presented = s.ticks && index < static_cast<int>(s.ticks->size()) ? (*s.ticks)[index] : static_cast<float>(Layout::NearestTick(row.percent));
        Ticks(index, row.percent, presented, top, s);
        const auto value = R(Layout::Value(top)); const auto text = Audio::PercentText(row.percent);
        const float w = chrome_.TextWidth(text, 13, true, true);
        const float x = std::min((value.left + value.right - w) / 2, value.right - w);   // « 100 % » is wider than the column: it ends at the padding
        chrome_.Text(text, {x, value.top, x + w + 1, value.bottom}, 13, Design::Text::secondary, true, true);
    }
    void CloseButton(const Scene& s) {
        const auto cv = Interaction(s, CloseHit); const auto box = R(Layout::Close(s.drawer));
        auto style = Design::Resolve(Design::ButtonVariant::Ghost, {cv.hover, cv.press, false, s.focus == CloseHit && s.focused});
        if (style.fill) chrome_.Round(box, style.radius, style.surface);
        if (style.focus) chrome_.FocusRing(box, style.radius);
        const float size = (s.drawer ? 16.f : 14.f) * Motion::PressScale(cv.press, s.motion && s.motion->ReducedMotion());
        chrome_.PathGlyph(Glyph::close, box, size, D2D1::ColorF(1, 1, 1, s.drawer ? 130 / 255.f : .5f));
    }
    void Paint(const Scene& s) {
        auto* t = chrome_.RenderTarget(); if (!t || !chrome_.SolidBrush() || !s.items) return;
        const float c = Design::Surface::canvas; t->Clear(D2D1::ColorF(c, c, c, 1));
        const float h = s.height > 0 ? s.height : t->GetSize().height;
        const float e = Motion::Clamp(s.entrance);
        D2D1_MATRIX_3X2_F base; t->GetTransform(&base);
        t->SetTransform((s.drawer ? D2D1::Matrix3x2F::Translation((1 - e) * Layout::drawerSlide, 0) : D2D1::Matrix3x2F::Translation(0, -(1 - e) * Layout::enterSlide)) * base);
        const auto& items = *s.items; const int n = static_cast<int>(items.size());
        const int shown = s.drawer ? n : std::min(n, Layout::popoverRows);
        if (s.drawer) t->PushAxisAlignedClip(D2D1::RectF(0, 0, Layout::width, h), D2D1_ANTIALIAS_MODE_ALIASED);
        if (n == 0) chrome_.Text(TR(audio_empty), {Layout::inset, Layout::RowTop(0, s.drawer), Layout::right - Layout::control, Layout::RowTop(0, s.drawer) + Layout::EmptyHeight}, 14, Design::Text::muted, false, false, -.14f);
        for (int i = 0; i < shown; ++i) {
            const float top = Layout::RowTop(i, s.drawer, s.scroll);
            if (top > h || top + Layout::pitch < 0) continue;
            Row(items[i], i, top, s);
            if (i + 1 < shown) { auto* b = chrome_.SolidBrush(); b->SetColor(D2D1::ColorF(0xDD / 255.f, 0xDD / 255.f, 0xDD / 255.f, .10f)); const float y = top + Layout::rowHeight + Layout::ruleGap; t->FillRectangle(D2D1::RectF(Layout::inset, y, Layout::right, y + 1), b); }
        }
        if (s.drawer) t->PopAxisAlignedClip();
        if (!s.drawer && n > Layout::popoverRows) {
            const auto box = R(Layout::ViewAllBox(n)); const auto v = Interaction(s, ViewAllHit);
            chrome_.Round(box, Design::Radius::control, Design::Surface::raised + (Design::Surface::hover - Design::Surface::raised) * v.hover + (Design::Surface::pressed - Design::Surface::hover) * v.press);
            if (s.focus == ViewAllHit && s.focused) chrome_.FocusRing(box, Design::Radius::control);
            const auto label = TRF(audio_view_all, std::to_wstring(n)); const float w = chrome_.TextWidth(label, 14, true, true, -.14f);
            const float x = (box.left + box.right - w) / 2;
            chrome_.Text(label, {x, box.top, x + w + 1, box.bottom}, 14, Design::Text::secondary, true, true, -.14f);
        }
        CloseButton(s);
        t->SetTransform(base);
        auto* b = chrome_.SolidBrush();
        const float px = t->GetSize().width / std::max(1.f, static_cast<float>(t->GetPixelSize().width));
        if (s.drawer) { b->SetColor(D2D1::ColorF(1, 1, 1, 5 / 255.f)); t->FillRectangle(D2D1::RectF(0, 0, px, h), b); }
        else { b->SetColor(D2D1::ColorF(1, 1, 1, 15 / 255.f)); t->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(px / 2, px / 2, Layout::width - px / 2, h - px / 2), Layout::radius, Layout::radius), b, px); }
    }
    int Hit(const std::vector<Item>& items, float x, float y, bool drawer, float scroll, float height) {
        auto in = [&](Design::Box b) { return x >= b.left && x < b.right && y >= b.top && y < b.bottom; };
        auto inR = [&](D2D1_RECT_F b) { return x >= b.left - 3 && x < b.right + 3 && y >= b.top - 3 && y < b.bottom + 3; };
        if (in(Layout::Close(drawer))) return CloseHit;
        const int n = static_cast<int>(items.size()), shown = drawer ? n : std::min(n, Layout::popoverRows);
        if (!drawer && n > Layout::popoverRows && in(Layout::ViewAllBox(n))) return ViewAllHit;
        if (drawer && (y < 0 || y >= height)) return 0;
        for (int i = 0; i < shown; ++i) {
            const float top = Layout::RowTop(i, drawer, scroll);
            if (in(Layout::Mute(top))) return MuteHit(i);
            if (in(Layout::Lane(top))) return SliderHit(i);
            if (!ShowsArtist(items[i].row) && inR(InfoBox(top))) return InfoHit(i);
            if (in(Layout::RowArea(top))) return RowHit(i);
        }
        return 0;
    }
};

struct Hooks {
    std::function<void(const std::wstring& site, int percent, bool commit)> volume; // every change; commit = gesture ended (persist)
    std::function<void(int tab)> show;                                                // bring that tab forward
    std::function<void(bool keyboard)> closed;
    std::function<RECT()> drawerArea;                                                 // screen px: the strip right of the page, under the chrome
    std::function<void()> drawerOpening;                                              // the host closes the other docked drawers
};

class Popover {
    HWND hwnd_ = nullptr, owner_ = nullptr;
    Chrome chrome_; View view_{chrome_}; Motion::State motion_; Hooks hooks_;
    Accessibility::Bridge uia_; Tooltip::Tip tip_;
    std::vector<Item> items_;
    std::vector<float> ticks_; std::map<std::wstring, float> tickBySite_; // presented tick position (glides to the nearest tick)
    std::map<std::wstring, int> remembered_;                               // last audible level per site, for unmute
    bool visible_ = false, keyboard_ = false, entering_ = false, exiting_ = false, drawer_ = false;
    int hover_ = 0, pressed_ = 0, focus_ = 0, dragging_ = -1, tipFor_ = 0; float grab_ = 0, scroll_ = 0;
    POINT final_{}; Motion::Time opened_{}, lastStep_{};
    static constexpr UINT_PTR FrameTimer = 1; bool frameArmed_ = false;
    static constexpr long long GroupKey = 10;
    float Dpi() const { return static_cast<float>(GetDpiForWindow(hwnd_ ? hwnd_ : owner_)); }
    int Px(float dip) const { return static_cast<int>(std::lround(dip * Dpi() / 96.f)); }
    int Count() const { return static_cast<int>(items_.size()); }
    int Shown() const { return drawer_ ? Count() : std::min(Count(), Layout::popoverRows); }
    float HeightDip() const { RECT r{}; GetClientRect(hwnd_, &r); return r.bottom * 96.f / Dpi(); }
    float MaxScroll() const { return drawer_ ? std::max(0.f, Layout::DrawerContent(Count()) - HeightDip()) : 0.f; }
    std::vector<int> Order() const {
        std::vector<int> o;
        for (int i = 0; i < Shown(); ++i) { o.push_back(RowHit(i)); if (!ShowsArtist(items_[i].row)) o.push_back(InfoHit(i)); o.push_back(MuteHit(i)); o.push_back(SliderHit(i)); }
        if (!drawer_ && Count() > Layout::popoverRows) o.push_back(ViewAllHit);
        o.push_back(CloseHit); return o;
    }
    float Entrance(Motion::Time now) const {
        if (!entering_ && !exiting_) return 1.f;
        const float ms = drawer_ ? (exiting_ ? Layout::drawerExitMs : Layout::drawerEnterMs) : Layout::enterMs;
        const float u = Motion::Eased(static_cast<float>(std::chrono::duration<double, std::milli>(now - opened_).count() / ms), Motion::Curve::Icon);
        return exiting_ ? 1 - u : u;
    }
    Scene MakeScene(Motion::Time now) const {
        Scene s; s.items = &items_; s.motion = &motion_; s.ticks = &ticks_; s.focus = focus_; s.keyboard = keyboard_; s.dragging = dragging_; s.now = now; s.focused = GetFocus() == hwnd_;
        s.drawer = drawer_; s.scroll = scroll_; s.height = HeightDip(); s.entrance = motion_.ReducedMotion() && drawer_ ? 1.f : Entrance(now);
        return s;
    }
    void Paint() {
        if (!chrome_.RenderTarget()) chrome_.Resize();
        auto* t = chrome_.RenderTarget(); if (!t) return;
        chrome_.keyboardModality = keyboard_;
        t->BeginDraw(); view_.Paint(MakeScene(Motion::Now()));
        if (t->EndDraw() == D2DERR_RECREATE_TARGET) chrome_.RecreateTarget();
    }
    void Invalidate() { if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE); }
    // Arms the frame timer once: SetTimer on an armed timer restarts its countdown, so re-arming on every pointer
    // move (a drag sends one every 1-2 ms) kept WM_TIMER from ever firing and the lit tick froze until the pointer paused.
    void Wake() { if (hwnd_ && !frameArmed_) frameArmed_ = SetTimer(hwnd_, FrameTimer, 10, nullptr) != 0; }
    void Rest() { if (hwnd_) KillTimer(hwnd_, FrameTimer); frameArmed_ = false; lastStep_ = {}; }
    void Interact() { const auto now = Motion::Now(); motion_.SetInteraction(hover_, pressed_ == hover_ ? pressed_ : (dragging_ >= 0 ? SliderHit(dragging_) : 0), now); Invalidate(); if (motion_.NextWake(now).delay) Wake(); SyncTip(false); }
    // Presented ticks glide to the nearest tick of each row's value (exponential, 40 ms).
    bool StepTicks(Motion::Time now) {
        const float dt = lastStep_ == Motion::Time{} ? 0.f : static_cast<float>(std::chrono::duration<double>(now - lastStep_).count()); lastStep_ = now;
        const float k = motion_.ReducedMotion() ? 1.f : 1 - std::exp(-std::min(dt, .1f) / .04f);
        bool moving = false;
        for (int i = 0; i < Count() && i < static_cast<int>(ticks_.size()); ++i) {
            const float target = static_cast<float>(Layout::NearestTick(items_[i].row.percent)); float& p = ticks_[i];
            if (std::abs(target - p) < .01f) p = target; else { p += (target - p) * k; moving = true; }
            tickBySite_[items_[i].row.site] = p;
        }
        return moving;
    }
    void Step() {
        const auto now = Motion::Now();
        const bool ticks = StepTicks(now);
        if (entering_ || exiting_) {
            const float e = MakeScene(now).entrance;
            SetLayeredWindowAttributes(hwnd_, 0, static_cast<BYTE>(std::lround(255 * e)), LWA_ALPHA);
            const float ms = drawer_ ? (exiting_ ? Layout::drawerExitMs : Layout::drawerEnterMs) : Layout::enterMs;
            if (std::chrono::duration<double, std::milli>(now - opened_).count() >= ms) { if (exiting_) { exiting_ = false; Hidden(false, false); return; } entering_ = false; SetLayeredWindowAttributes(hwnd_, 0, 255, LWA_ALPHA); }
        }
        Invalidate();
        if (!entering_ && !exiting_ && !ticks && !motion_.NextWake(now).delay) Rest();
    }
    void Place() {
        if (drawer_ && hooks_.drawerArea) {
            const RECT a = hooks_.drawerArea(); const int w = Px(Layout::width);
            final_ = {a.right - w, a.top};
            SetWindowPos(hwnd_, HWND_TOP, final_.x, final_.y, w, std::max<LONG>(1, a.bottom - a.top), SWP_NOACTIVATE);
        } else {
            RECT r{}; GetWindowRect(hwnd_, &r);
            const int w = Px(Layout::width), h = Px(Layout::PopoverHeight(Count()));
            if (r.right - r.left != w || r.bottom - r.top != h || r.left != final_.x || r.top != final_.y) SetWindowPos(hwnd_, nullptr, final_.x, final_.y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        chrome_.Resize(); scroll_ = std::clamp(scroll_, 0.f, MaxScroll());
    }
    // Changes the site's level: model + pages (through the host) + announcement. Never snapped to a tick.
    void SetPercent(int index, int percent, bool commit) {
        if (index < 0 || index >= Count()) return;
        auto& row = items_[index].row; const int before = row.percent; percent = Audio::ClampPercent(percent);
        if (percent > 0) remembered_[row.site] = percent; else if (before > 0) remembered_[row.site] = before;
        row.percent = percent; Invalidate(); Wake();
        if (uia_.Listening() && before != percent) uia_.RangeChanged(SliderHit(index), before, percent);
        if (hooks_.volume && (before != percent || commit)) hooks_.volume(row.site, percent, commit);
    }
    void ToggleMute(int index) {
        if (index < 0 || index >= Count()) return;
        const auto& row = items_[index].row; auto it = remembered_.find(row.site);
        const int next = Audio::ToggleMute(row.percent, it == remembered_.end() ? 0 : it->second);
        SetPercent(index, next, true);
        if (uia_.Listening()) uia_.Notify(MuteHit(index), next == 0 ? TRF(audio_announce_muted, row.site).c_str() : TRF(audio_announce_unmuted, row.site, I18n::Percent(next)).c_str(), L"AudioMute");
    }
    void Activate(int hit, bool keyboard) {
        if (hit == CloseHit) { Close(keyboard); return; }
        if (hit == ViewAllHit) { OpenDrawer(keyboard); return; }
        const int i = HitRow(hit); if (i < 0 || i >= Count()) return;
        switch (HitPart(hit)) {
        case 0: { const int tab = items_[i].row.tab; if (!drawer_) Close(false); if (hooks_.show && tab >= 0) hooks_.show(tab); break; }
        case 1: ToggleMute(i); break;
        case 3: ShowTip(i, true); break;
        default: break;
        }
    }
    RECT ScreenBox(D2D1_RECT_F b) const { RECT p{Px(b.left), Px(b.top), Px(b.right), Px(b.bottom)}; MapWindowPoints(hwnd_, nullptr, reinterpret_cast<POINT*>(&p), 2); return p; }
    void ShowTip(int i, bool now) {
        if (i < 0 || i >= Count() || !tip_.Window()) return;
        const RECT target = ScreenBox(view_.InfoBox(Layout::RowTop(i, drawer_, scroll_)));
        tipFor_ = InfoHit(i);
        // Kept inside the browser window (Paper TJ0-0 sits within it), like the toolbar's tips.
        const RECT bounds = TooltipPlacement::VisibleRect(owner_);
        if (now) tip_.Show(Tooltip::Content{SoundsTip(), L"", L""}, target, &bounds); else tip_.Hover(Tooltip::Content{SoundsTip(), L"", L""}, target, &bounds);
    }
    // The tip follows the info glyph under the pointer, or the keyboard focus on it.
    void SyncTip(bool keyboardMoved) {
        const int want = HitPart(hover_) == 3 ? hover_ : (keyboard_ && HitPart(focus_) == 3 ? focus_ : 0);
        if (want) { if (want != tipFor_ || keyboardMoved || !tip_.Visible()) ShowTip(HitRow(want), keyboard_ && want == focus_); }
        else if (tipFor_) { tipFor_ = 0; tip_.Hide(); }
    }
    void EnsureVisible(int hit) {
        if (!drawer_) return; const int i = HitRow(hit); if (i < 0) return;
        const float top = Layout::RowTop(i, true, 0), h = HeightDip();
        if (top - scroll_ < Layout::drawerTop) scroll_ = std::max(0.f, top - Layout::drawerTop);
        else if (top + Layout::rowHeight - scroll_ > h - Layout::padding) scroll_ = std::min(MaxScroll(), top + Layout::rowHeight - h + Layout::padding);
    }
    void FocusStop(int hit) { focus_ = hit; keyboard_ = true; EnsureVisible(hit); Invalidate(); if (uia_.Listening() && hit) uia_.Raise(hit, UIA_AutomationFocusChangedEventId); SyncTip(true); }
    void Cycle(bool back) {
        const auto o = Order(); auto it = std::find(o.begin(), o.end(), focus_);
        int i = it == o.end() ? (back ? static_cast<int>(o.size()) - 1 : 0) : static_cast<int>(it - o.begin()) + (back ? -1 : 1);
        i = (i + static_cast<int>(o.size())) % static_cast<int>(o.size()); FocusStop(o[i]);
    }
    bool Key(WPARAM key) {
        if (key == VK_ESCAPE) { if (tip_.Visible()) { tip_.Hide(); tipFor_ = 0; return true; } Close(true); return true; }
        if (key == VK_TAB) { Cycle(GetKeyState(VK_SHIFT) < 0); return true; }
        // Nothing focused yet (opened with the mouse): the first slider takes the arrows.
        int focus = focus_; if (!focus && Count() && (key == VK_LEFT || key == VK_RIGHT || key == VK_UP || key == VK_DOWN)) { FocusStop(SliderHit(0)); focus = focus_; }
        const int i = HitRow(focus);
        if (HitPart(focus) == 2 && i < Count()) {
            if (key == VK_LEFT || key == VK_RIGHT || key == VK_UP || key == VK_DOWN || key == VK_PRIOR || key == VK_NEXT || key == VK_HOME || key == VK_END) { keyboard_ = true; SetPercent(i, Audio::KeyStep(items_[i].row.percent, static_cast<int>(key)), true); return true; }
        }
        if ((key == VK_RETURN || key == VK_SPACE) && focus) { keyboard_ = true; Activate(focus, true); return true; }
        return false;
    }
    void Drag(float x) { if (dragging_ < 0) return; SetPercent(dragging_, static_cast<int>(std::lround(Layout::PercentAt(x - grab_))), false); }
    void OpenDrawer(bool keyboard) {
        if (!hooks_.drawerArea) return;
        if (hooks_.drawerOpening) hooks_.drawerOpening();
        tip_.HideNow(); tipFor_ = 0;
        drawer_ = true; scroll_ = 0; keyboard_ = keyboard; hover_ = pressed_ = 0; motion_.ClearInteractions();
        NativeUI::ConfigureShadowlessPopup(hwnd_, 0);
        opened_ = Motion::Now(); entering_ = !motion_.ReducedMotion(); exiting_ = false;
        SetLayeredWindowAttributes(hwnd_, 0, entering_ ? 0 : 255, LWA_ALPHA);
        Place(); focus_ = keyboard && Count() ? SliderHit(0) : 0;
        Paint(); ValidateRect(hwnd_, nullptr); SetForegroundWindow(hwnd_); SetFocus(hwnd_);
        if (entering_) Wake();
        if (uia_.Listening()) { uia_.ChildrenChanged(GroupKey); if (focus_) uia_.Raise(focus_, UIA_AutomationFocusChangedEventId); }
    }
    void Hidden(bool keyboard, bool notify = true) {
        ShowWindow(hwnd_, SW_HIDE); Rest(); drawer_ = false;
        if (notify && hooks_.closed) hooks_.closed(keyboard);
    }
    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<Popover*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (msg == WM_NCCREATE) { self = static_cast<Popover*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); self->hwnd_ = h; }
        return self ? self->Handle(h, msg, w, l) : DefWindowProcW(h, msg, w, l);
    }
    LRESULT Handle(HWND h, UINT msg, WPARAM w, LPARAM l) {
        const float scale = 96.f / Dpi();
        auto hit = [&](float x, float y) { return view_.Hit(items_, x, y, drawer_, scroll_, HeightDip()); };
        switch (msg) {
        case WM_GETOBJECT: { LRESULT result; if (uia_.OnGetObject(h, w, l, result)) return result; break; }
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); Paint(); EndPaint(h, &ps); return 0; }
        // Light dismiss: the popover only. The drawer stays docked until closed.
        case WM_ACTIVATE: if (LOWORD(w) == WA_INACTIVE && visible_ && !drawer_) PostMessageW(h, WM_APP + 1, 0, 0); if (LOWORD(w) == WA_INACTIVE) { tip_.Hide(); tipFor_ = 0; } return 0;
        case WM_APP + 1: if (visible_ && !drawer_ && GetForegroundWindow() != hwnd_ && dragging_ < 0) Close(false); return 0;
        case WM_APP + 2: keyboard_ = true; Activate(static_cast<int>(w), true); return 0;
        case WM_APP + 3: { const int i = HitRow(static_cast<int>(w)); SetPercent(i, static_cast<int>(l), true); return 0; }
        case WM_MOUSEACTIVATE: return MA_ACTIVATE;
        // The volume lane shows the horizontal drag cursor (two arrows); it stays during a drag (capture).
        case WM_SETCURSOR: if (LOWORD(l) == HTCLIENT) { POINT p; GetCursorPos(&p); ScreenToClient(h, &p); if (dragging_ >= 0 || HitPart(hit(p.x * scale, p.y * scale)) == 2) { SetCursor(LoadCursor(nullptr, IDC_SIZEWE)); return TRUE; } } break;
        case WM_TIMER: if (w == FrameTimer) Step(); return 0;
        case WM_MOUSEMOVE: {
            const float x = GET_X_LPARAM(l) * scale, y = GET_Y_LPARAM(l) * scale;
            if (dragging_ >= 0) { Drag(x); return 0; }
            TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, h, 0}; TrackMouseEvent(&t);
            const int at = hit(x, y); if (at != hover_) { hover_ = at; Interact(); } return 0; }
        case WM_MOUSELEAVE: if (dragging_ < 0) { hover_ = 0; Interact(); } return 0;
        case WM_LBUTTONDOWN: {
            const float x = GET_X_LPARAM(l) * scale, y = GET_Y_LPARAM(l) * scale; keyboard_ = false;
            const int at = hit(x, y); pressed_ = at; SetCapture(h); tip_.Hide(); tipFor_ = 0;
            if (HitPart(at) == 2) {
                // On the value's spot: keep where it was grabbed. Elsewhere on the lane: the value jumps there.
                const int i = HitRow(at); const float here = Layout::XAt(items_[i].row.percent);
                grab_ = std::abs(x - here) <= 5 ? x - here : 0;
                dragging_ = i; focus_ = at; Drag(x);
            }
            Interact(); return 0; }
        case WM_LBUTTONUP: case WM_CAPTURECHANGED: {
            if (msg == WM_CAPTURECHANGED && reinterpret_cast<HWND>(l) == h) return 0;
            const int was = pressed_, drag = dragging_; pressed_ = 0; dragging_ = -1;
            if (msg == WM_LBUTTONUP && GetCapture() == h) ReleaseCapture();
            if (drag >= 0 && drag < Count()) SetPercent(drag, items_[drag].row.percent, true);
            else if (msg == WM_LBUTTONUP) { const int at = hit(GET_X_LPARAM(l) * scale, GET_Y_LPARAM(l) * scale); if (was && was == at) Activate(at, false); }
            if (msg == WM_LBUTTONUP) hover_ = hit(GET_X_LPARAM(l) * scale, GET_Y_LPARAM(l) * scale);
            Interact(); return 0; }
        case WM_MOUSEWHEEL: {
            POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)}; ScreenToClient(h, &p);
            int at = hit(p.x * scale, p.y * scale); if (HitPart(at) != 2 && HitPart(focus_) == 2 && !drawer_) at = focus_;
            const int notches = GET_WHEEL_DELTA_WPARAM(w) / WHEEL_DELTA;
            if (HitPart(at) == 2) { const int i = HitRow(at); SetPercent(i, items_[i].row.percent + notches * Audio::WheelStep, true); }
            else if (drawer_) { scroll_ = std::clamp(scroll_ - notches * 48.f, 0.f, MaxScroll()); tip_.HideNow(); tipFor_ = 0; hover_ = hit(p.x * scale, p.y * scale); Interact(); }
            return 0; }
        case WM_KEYDOWN: if (Key(w)) return 0; break;
        case WM_GETDLGCODE: return DLGC_WANTALLKEYS;
        case WM_SETFOCUS: case WM_KILLFOCUS: Invalidate(); return 0;
        case WM_DPICHANGED: if (visible_) { Place(); Invalidate(); } return 0;
        }
        return DefWindowProcW(h, msg, w, l);
    }
    std::vector<Accessibility::Node> Nodes() {
        std::vector<Accessibility::Node> nodes; if (!visible_) return nodes;
        auto screen = [&](Design::Box b) { return ScreenBox(View::R(b)); };
        Accessibility::Node g; g.key = GroupKey; g.role = Accessibility::Role::Group; g.name = TR(chrome_tip_audio); g.automationId = drawer_ ? L"AudioDrawer" : L"AudioPanel"; g.focusable = false; RECT wr{}; GetWindowRect(hwnd_, &wr); g.bounds = wr; nodes.push_back(g);
        for (int i = 0; i < Shown(); ++i) {
            const auto& r = items_[i].row; const float top = Layout::RowTop(i, drawer_, scroll_);
            const bool off = drawer_ && (top + Layout::rowHeight < 0 || top > HeightDip());
            Accessibility::Node row; row.key = RowHit(i); row.parent = GroupKey; row.name = TRF(audio_row_show_tab, r.title); row.help = ShowsArtist(r) ? (r.artist.empty() ? r.site : r.artist + L", " + r.site) : std::wstring(SoundsLabel()) + L", " + r.site; row.automationId = L"AudioRow" + std::to_wstring(i); row.bounds = screen(Layout::RowArea(top)); row.position = i + 1; row.count = Count(); row.offscreen = off; nodes.push_back(row);
            if (!ShowsArtist(r)) { Accessibility::Node info; info.key = InfoHit(i); info.parent = GroupKey; info.name = TRF(audio_about, SoundsLabel()); info.help = SoundsTip(); info.automationId = L"AudioInfo" + std::to_wstring(i); info.bounds = ScreenBox(view_.InfoBox(top)); info.offscreen = off; nodes.push_back(info); }
            Accessibility::Node mute; mute.key = MuteHit(i); mute.parent = GroupKey; mute.name = TRF(audio_mute_site, r.site); mute.toggle = r.percent == 0 ? 2 : 1; mute.automationId = L"AudioMute" + std::to_wstring(i); mute.bounds = screen(Layout::Mute(top)); mute.offscreen = off; nodes.push_back(mute);
            Accessibility::Node slider; slider.key = SliderHit(i); slider.parent = GroupKey; slider.role = Accessibility::Role::Slider; slider.name = TRF(audio_volume_site, r.site); slider.help = TR(audio_volume_help);
            slider.rangeValue = r.percent; slider.rangeMin = 0; slider.rangeMax = 100; slider.smallChange = Audio::Step; slider.largeChange = Audio::PageStep; slider.automationId = L"AudioVolume" + std::to_wstring(i); slider.bounds = screen(Layout::Lane(top)); slider.offscreen = off; nodes.push_back(slider);
        }
        if (!drawer_ && Count() > Layout::popoverRows) { Accessibility::Node all; all.key = ViewAllHit; all.parent = GroupKey; all.name = TRF(audio_view_all, std::to_wstring(Count())); all.help = TR(audio_view_all_help); all.automationId = L"AudioViewAll"; all.bounds = screen(Layout::ViewAllBox(Count())); nodes.push_back(all); }
        Accessibility::Node close; close.key = CloseHit; close.parent = GroupKey; close.name = TR(common_close); close.accelerator = TR(key_escape); close.automationId = L"AudioClose"; close.bounds = screen(Layout::Close(drawer_)); nodes.push_back(close);
        return nodes;
    }
public:
    ~Popover() { Dispose(); }
    void Dispose() { tip_.Dispose(); uia_.Detach(); chrome_.Dispose(); hwnd_ = nullptr; visible_ = false; }
    bool Create(HINSTANCE instance, HWND owner, const std::wstring& root, Hooks hooks) {
        owner_ = owner; hooks_ = std::move(hooks); motion_.EnablePolish(); chrome_.SetMotion(&motion_);
        WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = instance; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.lpszClassName = L"LisiereAudioPanel";
        RegisterClassW(&wc);
        CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED, wc.lpszClassName, TR(chrome_tip_audio), WS_POPUP, 0, 0, 1, 1, owner, nullptr, instance, this);
        if (!hwnd_) return false;
        SetLayeredWindowAttributes(hwnd_, 0, 255, LWA_ALPHA);
        NativeUI::ConfigureShadowlessPopup(hwnd_, Layout::radius);
        tip_.Create(instance, hwnd_, root);
        Accessibility::Host host; host.window = hwnd_;
        host.nodes = [this] { return Nodes(); };
        host.focused = [this]() -> long long { return GetFocus() == hwnd_ ? focus_ : 0; };
        host.invoke = [this](long long key) { PostMessageW(hwnd_, WM_APP + 2, static_cast<WPARAM>(key), 0); };
        host.toggle = host.invoke;
        host.focus = [this](long long key) { FocusStop(static_cast<int>(key)); };
        host.setRange = [this](long long key, double value) { PostMessageW(hwnd_, WM_APP + 3, static_cast<WPARAM>(key), static_cast<LPARAM>(std::lround(value))); };
        uia_.Attach(std::move(host));
        return SUCCEEDED(chrome_.Init(hwnd_, root));
    }
    HWND Window() const { return hwnd_; }
    bool Visible() const { return visible_; }
    bool Docked() const { return visible_ && drawer_; }
    bool Dragging() const { return dragging_ >= 0; }
    ULONGLONG hiddenAt = 0;
    const std::vector<Item>& Items() const { return items_; }
    const std::vector<float>& PresentedTicks() const { return ticks_; }
    // New snapshot from the host. Keeps the keyboard focus on the same site and part, the value
    // of a slider being dragged (the pointer owns it until release) and each row's gliding ticks.
    void SetItems(std::vector<Item> items) {
        const int fi = HitRow(focus_), fp = HitPart(focus_);
        const std::wstring focusSite = fi >= 0 && fi < Count() ? items_[fi].row.site : L"";
        const std::wstring dragSite = dragging_ >= 0 && dragging_ < Count() ? items_[dragging_].row.site : L"";
        const int dragValue = dragging_ >= 0 && dragging_ < Count() ? items_[dragging_].row.percent : 0;
        items_ = std::move(items);
        for (auto& it : items_) if (it.row.percent > 0) remembered_[it.row.site] = it.row.percent;
        dragging_ = -1; ticks_.clear();
        for (int i = 0; i < Count(); ++i) {
            if (!dragSite.empty() && items_[i].row.site == dragSite) { dragging_ = i; items_[i].row.percent = dragValue; }
            if (!focusSite.empty() && items_[i].row.site == focusSite) focus_ = RowBase + i * 4 + (fp == 3 && ShowsArtist(items_[i].row) ? 0 : fp);
            auto t = tickBySite_.find(items_[i].row.site);
            ticks_.push_back(t != tickBySite_.end() && visible_ ? t->second : static_cast<float>(Layout::NearestTick(items_[i].row.percent)));
        }
        if (fi >= 0 && (focusSite.empty() || HitRow(focus_) >= Count() || items_[HitRow(focus_)].row.site != focusSite)) focus_ = Count() ? SliderHit(0) : CloseHit;
        if (!hwnd_ || !visible_) return;
        Place(); Invalidate(); Wake();
        if (uia_.Listening()) uia_.ChildrenChanged(GroupKey);
    }
    // anchor: toolbar button, screen pixels. Right edges aligned, 8 DIP below; kept inside `bounds`.
    // `at` (#127, from the column): the popover's bottom-left corner in screen pixels instead of under the button.
    void Show(RECT anchor, RECT bounds, bool keyboard, const POINT* at = nullptr) {
        if (!hwnd_) return;
        const bool reduced = Motion::PreferredReducedMotion();
        drawer_ = false; exiting_ = false; scroll_ = 0;
        NativeUI::ConfigureShadowlessPopup(hwnd_, Layout::radius);
        keyboard_ = keyboard; hover_ = pressed_ = 0; dragging_ = -1; motion_.ClearInteractions(); motion_.SetReducedMotion(reduced);
        for (int i = 0; i < Count() && i < static_cast<int>(ticks_.size()); ++i) ticks_[i] = static_cast<float>(Layout::NearestTick(items_[i].row.percent));
        focus_ = keyboard ? (Count() ? SliderHit(0) : CloseHit) : 0;
        const int w = Px(Layout::width), h = Px(Layout::PopoverHeight(Count())), margin = Px(Design::Space::sm);
        int x = anchor.right - w, y = anchor.bottom + Px(Layout::anchorGap);
        if (at) { x = at->x; y = at->y - h; }
        x = std::max<int>(bounds.left + margin, std::min<int>(x, bounds.right - margin - w));
        final_ = {x, y}; visible_ = true; opened_ = Motion::Now(); entering_ = !reduced;
        SetWindowPos(hwnd_, HWND_TOP, x, y, w, h, SWP_NOACTIVATE);
        SetLayeredWindowAttributes(hwnd_, 0, entering_ ? 0 : 255, LWA_ALPHA);
        chrome_.Resize();
        ShowWindow(hwnd_, SW_SHOW); Paint(); ValidateRect(hwnd_, nullptr);
        SetForegroundWindow(hwnd_); SetFocus(hwnd_);
        if (entering_) Wake();
        if (uia_.Listening()) { uia_.ChildrenChanged(GroupKey); uia_.Raise(GroupKey, UIA_Window_WindowOpenedEventId); if (focus_) uia_.Raise(focus_, UIA_AutomationFocusChangedEventId); }
    }
    // The owner moved or resized: the docked drawer follows its strip.
    void OwnerChanged() { if (visible_ && drawer_) { Place(); Invalidate(); } }
    void Close(bool keyboard) {
        if (!visible_) return;
        visible_ = false; hiddenAt = GetTickCount64(); dragging_ = -1; tip_.HideNow(); tipFor_ = 0;
        if (GetCapture() == hwnd_) ReleaseCapture();
        if (uia_.Listening()) uia_.Raise(GroupKey, UIA_Window_WindowClosedEventId);
        // The drawer leaves the way it came (fade + 12 DIP to the right); the popover vanishes.
        if (drawer_ && !motion_.ReducedMotion() && !keyboard) { entering_ = false; exiting_ = true; opened_ = Motion::Now(); Wake(); if (hooks_.closed) hooks_.closed(keyboard); return; }
        Hidden(keyboard);
    }
    // Test hooks (smoke run and fixtures).
    bool KeyForTest(WPARAM key) { return Key(key); }
    int FocusForTest() const { return focus_; }
    void PaintForTest() { Paint(); }
    void OpenDrawerForTest() { OpenDrawer(false); }
};
} // namespace AudioPanel
} // namespace NativeChrome
