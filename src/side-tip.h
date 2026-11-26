#pragma once
// #143 · Infobulle B of the side panel's tabs and favourites (owner's go 2026-11-26). Paper page « #127 — Panneau
// latéral », board 26O9-0 « #143 · Infobulle des onglets à droite · B (maquette prête) », box 26QB-0; Designer's motion
// values and answers (2026-11-26). Pure: the box's values, its place beside the hovered row / tile, when it shows (the
// delay, the chaining, a click) and its motion (appear, disappear, glide from one item to the next: interruptible, from
// what is on screen, reduced motion). The window drawing it: src/side-tip-window.h. UI thread only.
#include <algorithm>
#include <chrono>
#include <cmath>
#include "motion.h"

namespace NativeChrome { namespace SideTip {
// Paper 26QB-0 (get_jsx): #171717, 1 px #2B2B2B, radius 6, padding 6 (top / bottom) and 8 (sides), 2 between the lines,
// max width 300. Line 1: the title alone, 14/20 #E8E8E8, flush left. Line 2: the site icon 14 x 14, 6, the address,
// 14/20 #8B8B8B. No arrow.
namespace Look {
inline constexpr float gap = 8;   // from the hovered plate / row / tile's outer edge (26QB-0 at 44 beside a 32 plate ending at 36; Designer (a))
inline constexpr float border = 1, padX = 8, padY = 6, lineGap = 2, line = 20, text = 14, icon = 14, iconGap = 6, radius = 6, maxWidth = 300;
inline constexpr unsigned surfaceRgb = 0x171717, borderRgb = 0x2B2B2B, titleRgb = 0xE8E8E8, addressRgb = 0x8B8B8B;
// 34 high with the title only (no address: a new tab, a folder), 56 with both lines.
inline constexpr float Height(bool address) { return 2 * border + 2 * padY + line + (address ? lineGap + line : 0); }
// From the lines' measured text widths (DIP): the longer one, capped at 300 (beyond: an ellipsis).
inline float Width(float title, float address, bool hasAddress) {
    const float inner = std::max(title, hasAddress ? icon + iconGap + address : 0.f);
    return std::min(maxWidth, std::ceil(2 * border + 2 * padX + inner));
}
}
// Designer (2026-11-26): appear = opacity 0 -> 1, 4 px out from the icon (-4 -> 0) and 0.98 -> 1 from the edge on the
// icon's side, one spring with no bounce (damping 1) of 0.18 s; disappear = a 100 ms fade, no slide; from one item to the
// next the box never goes away: it glides there (and takes its width) on a 0.2 s spring, the text cross-fades in 80 ms;
// reduced motion: 100 ms fades only, the box jumps between items with the 80 ms cross-fade. Delay: 500 ms in the complete
// column, none in the compact one (owner, 2026-11-26).
namespace Timing {
inline constexpr double delayMs = 500, compactDelayMs = 0;
inline constexpr float appearResponse = .18f, glideResponse = .20f;
inline constexpr double fadeOutMs = 100, crossMs = 80, reducedFadeMs = 100;
inline constexpr float slide = 4, scaleFrom = .98f;
}
struct Box { float left = 0, top = 0, right = 0, bottom = 0; float Width() const { return right - left; } float Height() const { return bottom - top; } };
// Beside `anchor` (the hovered plate / row / tile, window DIP): `Look::gap` out from its outer edge (its right edge with
// the panel on the left, its left edge with the panel on the right), centred on it vertically, kept whole inside the
// window [0, W] x [0, H] (never clipped by its edge).
inline Box Place(const Box& anchor, float w, float h, float W, float H, bool panelLeft) {
    float x = panelLeft ? anchor.right + Look::gap : anchor.left - Look::gap - w;
    float y = (anchor.top + anchor.bottom) / 2 - h / 2;
    x = std::clamp(x, 0.f, std::max(0.f, W - w)); y = std::clamp(y, 0.f, std::max(0.f, H - h));
    return {x, y, x + w, y + h};
}

// When the hovered item's tip shows: after `delay` of rest on it (none in the compact column), at once while a tip is
// on screen (sliding from one item to the next), and never again for an item clicked (or whose menu opened) until the
// pointer reaches another item or leaves the column (Reset).
class Gate {
    int item_ = 0, dismissed_ = 0; double since_ = -1;
public:
    // `item` under the pointer (0: none, a gap: the tip on screen stays). Returns the item to show now, 0 otherwise.
    int Hover(int item, double nowMs, double delayMs, bool showing) {
        if (!item) return 0;
        if (item != dismissed_) dismissed_ = 0; else return 0;
        if (item != item_) { item_ = item; since_ = nowMs; }
        return showing || delayMs <= 0 || nowMs - since_ >= delayMs ? item : 0;
    }
    // How long until the rested-on item is due (ms), -1 when nothing waits.
    double Remaining(double nowMs, double delayMs) const { return item_ && item_ != dismissed_ && since_ >= 0 ? std::max(0.0, delayMs - (nowMs - since_)) : -1; }
    void Dismiss() { dismissed_ = item_; }
    // That item's menu opened (its tip goes and stays away until another item), whatever the pointer was on before.
    void Dismiss(int item) { if (item) { item_ = dismissed_ = item; since_ = -1; } else Dismiss(); }
    void Reset() { item_ = dismissed_ = 0; since_ = -1; }
    int Item() const { return item_; }
    int Dismissed() const { return dismissed_; }
};

// What one frame shows: the box (window DIP), its opacity, its slide out from the icon (DIP, signed) and scale (about the
// icon's side edge, vertically centred), the cross-fade (the item's text at `cross`, the previous one's at 1 - cross).
struct Frame { bool visible = false; Box box; float opacity = 0, dx = 0, scale = 1, cross = 1; int item = 0, previous = 0; bool panelLeft = true; };

class Motion {
    using Time = NativeChrome::Motion::Time;
    NativeChrome::Motion::Spring x_, y_, w_, h_, p_, o_;   // the box (glide), the appear progress (slide + scale), the opacity
    NativeChrome::Motion::Tween cross_;
    bool fading_ = false; float fadeFrom_ = 0, fadeTo_ = 0; double fadeMs_ = 0; Time fadeAt_{};   // a linear fade (out, or reduced motion)
    bool shown_ = false, panelLeft_ = true; int item_ = 0, previous_ = 0;
    static double Ms(Time a, Time b) { return std::chrono::duration<double, std::milli>(b - a).count(); }
    void Fade(float from, float to, double ms, Time now) { fading_ = true; fadeFrom_ = from; fadeTo_ = to; fadeMs_ = std::max(1.0, ms * std::fabs(to - from)); fadeAt_ = now; o_.Snap(to); }
    void Glide(const Box& b, Time now, bool reduced) {
        const float r = Timing::glideResponse;
        x_.Set(b.left, now, r, reduced); y_.Set(b.top, now, r, reduced); w_.Set(b.Width(), now, r, reduced); h_.Set(b.Height(), now, r, reduced);
    }
public:
    float Opacity(Time now) const {
        if (!fading_) return std::clamp(o_.Value(now), 0.f, 1.f);
        const double t = std::clamp(Ms(fadeAt_, now) / fadeMs_, 0.0, 1.0); return std::clamp(static_cast<float>(fadeFrom_ + (fadeTo_ - fadeFrom_) * t), 0.f, 1.f);
    }
    // Shows `item` at `box` (window DIP). Nothing on screen: it appears there. On screen (or still fading out): it glides
    // there from where it is, the text cross-fading when the item changed, its opacity going back up from what is shown.
    void Show(int item, const Box& box, bool panelLeft, Time now, bool reduced) {
        const float o = Opacity(now); const bool onScreen = shown_ || o > 0;
        panelLeft_ = panelLeft;
        if (!onScreen) {
            x_.Snap(box.left); y_.Snap(box.top); w_.Snap(box.Width()); h_.Snap(box.Height());
            item_ = item; previous_ = 0; cross_.Snap(1);
            if (reduced) { p_.Snap(1); Fade(0, 1, Timing::reducedFadeMs, now); }
            else { fading_ = false; p_.Snap(0); p_.Set(1, now, Timing::appearResponse); o_.Snap(0); o_.Set(1, now, Timing::appearResponse); }
            shown_ = true; return;
        }
        if (item != item_) { previous_ = item_; item_ = item; cross_.Snap(0); cross_.Set(1, now, NativeChrome::Motion::Milliseconds(static_cast<int>(Timing::crossMs)), false, NativeChrome::Motion::Curve::SmoothStep); }
        Glide(box, now, reduced);
        if (!shown_ || fading_) {   // coming back while it fades out: from the opacity on screen
            if (reduced) Fade(o, 1, Timing::reducedFadeMs, now);
            else { fading_ = false; o_.Snap(o); o_.Set(1, now, Timing::appearResponse); p_.Set(1, now, Timing::appearResponse); }
        }
        shown_ = true;
    }
    // Goes: a 100 ms fade from the opacity on screen, where it is (no slide: the box and the appear motion stop there).
    void Hide(Time now) {
        if (!shown_) return;
        shown_ = false; const float o = Opacity(now);
        x_.Snap(x_.Value(now)); y_.Snap(y_.Value(now)); w_.Snap(w_.Value(now)); h_.Snap(h_.Value(now)); p_.Snap(p_.Value(now));
        Fade(o, 0, Timing::fadeOutMs, now);
    }
    // At once (the layout changed under it, the window hid).
    void Clear() { shown_ = fading_ = false; o_.Snap(0); p_.Snap(0); item_ = previous_ = 0; cross_.Snap(1); }
    Frame At(Time now) const {
        Frame f; f.opacity = Opacity(now); f.visible = shown_ || f.opacity > 0; f.panelLeft = panelLeft_;
        const float x = x_.Value(now), y = y_.Value(now), w = std::max(0.f, w_.Value(now)), h = std::max(0.f, h_.Value(now));
        f.box = {x, y, x + w, y + h};
        const float p = std::clamp(p_.Value(now), 0.f, 1.f);
        f.dx = (panelLeft_ ? -1.f : 1.f) * Timing::slide * (1 - p); f.scale = Timing::scaleFrom + (1 - Timing::scaleFrom) * p;
        f.cross = cross_.Value(now); f.item = item_; f.previous = f.cross < 1 ? previous_ : 0;
        return f;
    }
    bool Moving(Time now) const {
        const bool fade = fading_ && Ms(fadeAt_, now) < fadeMs_;
        return fade || x_.Moving(now) || y_.Moving(now) || w_.Moving(now) || h_.Moving(now) || p_.Moving(now) || o_.Moving(now) || cross_.Moving(now);
    }
    bool Shown() const { return shown_; }   // its target: on screen (false while it fades out)
    int Item() const { return item_; }
};

}}  // namespace NativeChrome::SideTip
