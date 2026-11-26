#pragma once
// Split view hover frames (Val's page « Atelier de Val - à ne pas toucher », boards « Fenêtre »: 27E0-0, the frames on
// the right pane, and 2793-0, on the left pane; Val's own design). When the pointer comes near the top of a split-view
// pane, two small frames arrive on THAT pane: top-left the site (favicon, name, address); top-right « réduire » (the
// page leaves the split view and stays a tab: the split view's own « back to a tab ») and « fermer » (the page closes,
// as its tab's close button).
// This header is pure C++ (no drawing, no window): the geometry, the show / hide state machine and the morph's poses.
// The painter and the layered windows: src/split-frames-ui.h. Tests: tests/split-view/split_frames_test.cpp.
//
// Board values (Paper get_jsx 27GD-0, 27GI-0, 27DC-0, 27DU-0): each frame is the canvas (--color-browser-canvas
// #171717) flush with the pane's top edge and its own side, 24 high (--spacing-browser-24), padding 8 left / 10 right,
// gap 8 (--spacing-browser-8); only its inner bottom corner is rounded, 6 (--radius-browser-6). Site frame: the favicon
// 12 x 12 (radius 2), 6 (--spacing-browser-6), the name 12/14 Inter Display Regular white 90 % (#FFFFFFE6), 8, the
// address 12/14 white 50 % (--opacity-browser-secondary); both with cv01-cv04, cv13 and tnum. Actions frame: the
// minimize glyph 16 x 16 (Phosphor minus, the app's « minimize », 0.2 DIP higher: the board's view box starts at -12.8),
// 8, the close glyph 14 x 14 centred in a 16 x 16 box (the app's « close » drawn at 14); both #FFFFFF82.
//
// Motion (the boards draw none): the morph rides the app's morph spring in (Downloads::DrawerMotion::morphSpring,
// critically damped, response 0.32 s) and the drawers' exit spring out (exitSpring, 0.25 s). Each frame arrives into its
// own corner: it slides in diagonally from behind the pane's top edge and its own side (one frame height, 24 DIP, each
// way; the pane clips it, so nothing appears from nothing) while its content fades in over the morph's last 75 %
// (DrawerMotion::morphLead), and leaves the same way. Reduced motion (« Réduire les animations », or Windows' setting):
// no travel, a plain 120 ms fade (the hover fade).
// Not in the boards (provisional, listed in the hand-off): the trigger zone (the pane's top 32 DIP), the delays (shown
// 150 ms after the pointer settles in the zone, hidden 400 ms after it leaves), a move to another pane's top (the frames
// follow at once and arrive again), the narrow pane (the address trims first, gone under 24 DIP, then the name; with no
// room even for the favicon the site frame steps aside), the glyphs' hover (white 90 %) and press (white 70 %), a click
// on the site frame (it activates that pane, as a click on the page would).
#include <algorithm>
#include <cmath>
#include <vector>
#include "split-view.h"
#include "downloads.h"   // Lane, Spring, DrawerMotion (the app's motion constants)

namespace NativeChrome { namespace SplitFrames {

namespace Layout {
inline constexpr float height = 24, padLeft = 8, padRight = 10, gap = 8, iconGap = 6, favicon = 12, faviconRadius = 2;
inline constexpr float font = 12, line = 14, radius = 6;
inline constexpr float glyphBox = 16, minimizeGlyph = 16, closeGlyph = 14;
inline constexpr float nameAlpha = 0xE6 / 255.f, addressAlpha = .5f, glyphAlpha = 0x82 / 255.f, glyphHover = .9f, glyphPress = .7f;
inline constexpr float actionsWidth = padLeft + glyphBox + gap + glyphBox + padRight;   // 58
// Provisional (not drawn): see the header.
inline constexpr float zone = 32, sideGap = 8, minAddress = 24;
// The board's minimize view box is « 0 -12.8 256 256 » around the app's minus (centre line y 128 in 256 units): drawn
// at 16 DIP, the bar's centre sits at (111.999 + 12.8) / 256 x 16 = 7.8 DIP, the app's at 8: 0.2 DIP higher.
inline constexpr float MinimizeOffset() { return (111.999f + 12.8f) * 16 / 256 - 128.f * 16 / 256; }
}
static_assert(Layout::actionsWidth == 58, "Paper 27GI-0: 8 + 16 + 8 + 16 + 10");

namespace Timing {
inline constexpr Motion::Milliseconds showDelay{150}, hideDelay{400};   // provisional (not drawn)
inline constexpr Downloads::Spring enterSpring = Downloads::DrawerMotion::morphSpring, exitSpring = Downloads::DrawerMotion::exitSpring;
inline constexpr Motion::Milliseconds enter = Downloads::DrawerMotion::morph, exit = Downloads::DrawerMotion::exit, fade{120};
inline constexpr float contentLead = Downloads::DrawerMotion::morphLead;   // content fades in over the morph's last 75 %
}

using Box = Split::Box;

// ---- Geometry (owner client DIP) ---------------------------------------------------------------------------------
struct SiteLayout {
    bool shown = false, address = false;
    Box frame, icon, name, addressBox;
};
// `nameW` / `addressW`: the texts' natural widths. The frame hugs its content and never comes within `sideGap` of the
// actions frame: the address trims first (and goes under 24 DIP), then the name.
inline SiteLayout LayoutSite(const Box& pane, float nameW, float addressW) {
    using namespace Layout;
    SiteLayout l; const float fixed = padLeft + favicon + iconGap + padRight;
    const float room = pane.W() - actionsWidth - sideGap;
    if (room < fixed) return l;   // the site frame steps aside
    float name = std::max(0.f, nameW), address = std::max(0.f, addressW); const float text = room - fixed;
    if (name + (address > 0 ? gap + address : 0) > text) {
        address = std::max(0.f, text - name - gap);
        if (address < minAddress) address = 0;
        if (address == 0) name = std::min(name, text);
    }
    l.shown = true; l.address = address > 0;
    const float w = fixed + name + (l.address ? gap + address : 0), top = pane.top, cy = top + height / 2;
    l.frame = {pane.left, top, pane.left + w, top + height};
    l.icon = {pane.left + padLeft, cy - favicon / 2, pane.left + padLeft + favicon, cy + favicon / 2};
    l.name = {l.icon.right + iconGap, top, l.icon.right + iconGap + name, top + height};
    l.addressBox = l.address ? Box{l.name.right + gap, top, l.name.right + gap + address, top + height} : Box{l.name.right, top, l.name.right, top + height};
    return l;
}
struct ActionsLayout {
    bool shown = false;
    Box frame, minimize, close, minimizeHit, closeHit;   // glyph boxes (16 x 16) and their pointer targets (the frame split between them)
};
inline ActionsLayout LayoutActions(const Box& pane) {
    using namespace Layout;
    ActionsLayout a; if (pane.W() < actionsWidth || pane.H() < height) return a;
    const float top = pane.top, cy = top + height / 2;
    a.shown = true; a.frame = {pane.right - actionsWidth, top, pane.right, top + height};
    a.minimize = {a.frame.left + padLeft, cy - glyphBox / 2, a.frame.left + padLeft + glyphBox, cy + glyphBox / 2};
    a.close = {a.minimize.right + gap, a.minimize.top, a.minimize.right + gap + glyphBox, a.minimize.bottom};
    const float mid = (a.minimize.right + a.close.left) / 2;
    a.minimizeHit = {a.frame.left, top, mid, a.frame.bottom}; a.closeHit = {mid, top, a.frame.right, a.frame.bottom};
    return a;
}
enum Action : int { NoAction = 0, Minimize = 1, Close = 2 };
inline int ActionAt(const ActionsLayout& a, float x, float y) {
    if (!a.shown) return NoAction;
    return a.minimizeHit.Contains(x, y) ? Minimize : a.closeHit.Contains(x, y) ? Close : NoAction;
}

// ---- Morph ---------------------------------------------------------------------------------------------------------
// Where a frame's picture sits inside its own (final) box at morph progress `p`, and its opacities. `actions`: the
// top-right frame (it arrives from the right), else the site frame (from the left).
struct Pose { float dx = 0, dy = 0, surface = 1, content = 1; };
inline Pose PoseAt(float p, bool reduced, bool actions) {
    p = std::clamp(p, 0.f, 1.f);
    if (reduced) return {0, 0, p, p};
    const float travel = Layout::height * (1 - p);
    return {actions ? travel : -travel, -travel, 1, std::clamp((p - Timing::contentLead) / (1 - Timing::contentLead), 0.f, 1.f)};
}

// ---- Show / hide ---------------------------------------------------------------------------------------------------
struct Input {
    bool active = false;          // a split view on screen, the window in front, nothing over the pages' tops (drag, resize, Mode Focus…)
    bool pointer = false;         // the pointer is over the window; x, y in client DIP
    float x = 0, y = 0;
    bool buttons = false;         // a mouse button is held (a selection drag in a page): never starts the frames
    std::vector<Box> panes;       // the visible panes (rails excluded), panel order
};
// The pane whose top zone holds (x, y), or -1.
inline int ZoneAt(const std::vector<Box>& panes, float x, float y) {
    for (int i = 0; i < static_cast<int>(panes.size()); ++i) {
        const auto& b = panes[i];
        if (x >= b.left && x < b.right && y >= b.top && y < std::min(b.bottom, b.top + Layout::zone)) return i;
    }
    return -1;
}
class Machine {
public:
    void SetReduced(bool reduced) noexcept { reduced_ = reduced; }
    bool Reduced() const noexcept { return reduced_; }
    // One evaluation (every ~30 ms while a split view shows, and on every layout change). True when the frames' pane or
    // direction changed (the host re-lays out its windows).
    bool Update(const Input& in, Motion::Time now) {
        const int before = pane_; const float target = reveal_.Target();
        if (pane_ >= static_cast<int>(in.panes.size())) { pane_ = -1; reveal_.Snap(0); leavingSince_ = false; }   // its pane is gone (closed, reduced, layout)
        const bool shown = pane_ >= 0 && reveal_.Target() > 0, leaving = pane_ >= 0 && reveal_.Target() == 0;
        const int zone = in.active && in.pointer ? ZoneAt(in.panes, in.x, in.y) : -1;
        if (!in.active) { candidate_ = -1; leavingSince_ = false; if (pane_ >= 0) Hide(now); }
        else if (shown && zone == pane_) { candidate_ = -1; leavingSince_ = false; }
        else if (zone >= 0 && !in.buttons && (shown || leaving) && pane_ >= 0) {
            // Frames on screen (or still leaving): they follow the pointer at once, no second wait.
            if (zone != pane_) { pane_ = zone; reveal_.Snap(0); }
            Show(now); candidate_ = -1; leavingSince_ = false;
        } else if (zone >= 0 && !in.buttons) {
            if (candidate_ != zone) { candidate_ = zone; since_ = now; }
            else if (now - since_ >= Timing::showDelay) { pane_ = zone; reveal_.Snap(0); Show(now); candidate_ = -1; }
        } else {
            candidate_ = -1;
            if (shown) {
                if (!leavingSince_) { leavingSince_ = true; left_ = now; }
                else if (now - left_ >= Timing::hideDelay) { Hide(now); leavingSince_ = false; }
            }
        }
        if (pane_ >= 0 && reveal_.Target() == 0 && !reveal_.Moving(now)) pane_ = -1;   // gone
        return pane_ != before || reveal_.Target() != target;
    }
    int Pane() const noexcept { return pane_; }                        // the pane the frames are on (also while they leave)
    bool Showing() const noexcept { return pane_ >= 0 && reveal_.Target() > 0; }   // arriving or in place: the buttons answer
    float Progress(Motion::Time now) const noexcept { return pane_ >= 0 ? reveal_.Value(now) : 0.f; }
    bool Moving(Motion::Time now) const noexcept { return reveal_.Moving(now); }
    int Candidate() const noexcept { return candidate_; }
    Motion::Wake NextWake(Motion::Time now) const {
        Motion::Wake wake; if (reveal_.Moving(now)) wake = Motion::Frame();
        if (candidate_ >= 0) wake = Motion::Earlier(wake, Motion::Deadline(now, since_ + Timing::showDelay));
        if (leavingSince_) wake = Motion::Earlier(wake, Motion::Deadline(now, left_ + Timing::hideDelay));
        return wake;
    }
    // At once, without motion (a click on « réduire » / « fermer »: the pane goes away under the frames).
    void Dismiss() noexcept { pane_ = -1; candidate_ = -1; leavingSince_ = false; reveal_.Snap(0); }
private:
    int pane_ = -1, candidate_ = -1; bool reduced_ = false, leavingSince_ = false;
    Motion::Time since_{}, left_{};
    Downloads::Lane reveal_;
    void Show(Motion::Time now) {
        if (reduced_) reveal_.Set(1, now, Motion::Milliseconds(0), Timing::fade, Downloads::Curves::emphasized);
        else reveal_.Set(1, now, Motion::Milliseconds(0), Timing::enter, Timing::enterSpring);
    }
    void Hide(Motion::Time now) {
        if (reduced_) reveal_.Set(0, now, Motion::Milliseconds(0), Timing::fade, Downloads::Curves::emphasized);
        else reveal_.Set(0, now, Motion::Milliseconds(0), Timing::exit, Timing::exitSpring);
    }
};

}}  // namespace NativeChrome::SplitFrames
