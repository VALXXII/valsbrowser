#pragma once
// Mode Focus entry / exit motion (Designer's spec 2026-11-25, Paper page « Mode Focus - En cours », board « Mode Focus —
// mouvement d'entrée et de sortie »). Pure C++17 (no Win32): the spring, the bar's fade along the path, and the
// in-between layout of every surface that moves. Bound to the window in src/main.cpp (« Mode Focus transition »);
// unit-tested in tests/focus_motion_test.cpp.
//
//  * Everything moves together, one block, the same spring and the same start: a spring without bounce (damping 1.0),
//    response 0.3 s. Only position, size (transform) and opacity are animated.
//  * Entry: the top bar slides up 0 -> -100 % of its height and fades (opacity 1 -> 0 over the first 60 % of the path);
//    the page's rectangle grows on the same curve, its top edge glued to the bar's bottom edge (the gap between them,
//    the inset, grows with it: the canvas shows only in that inset); inset and radii arrive at the same time.
//  * Exit: the same path backwards; the bar's opacity 0 -> 1 over the first 40 % of the path.
//  * Interruptible: a second press restarts from the position on screen, never from the target, keeping the velocity.
//  * Reduced motion: no slide, no spring: a 120 ms cross-fade from a picture of before to the new screen.
//  * Windows: the page (another process's window) is resized once; meanwhile a frozen picture of it moves, then the live
//    page replaces it in a 120 ms cross-fade.
#include <algorithm>
#include <cmath>

namespace NativeChrome { namespace FocusMotion {

// ---- The spec's values ------------------------------------------------------------------------------------------
inline constexpr double responseS = 0.3;       // « réponse 0,3 s »
inline constexpr double dampingRatio = 1.0;    // « amortissement 1,0 » (no bounce)
inline constexpr double enterFadeShare = 0.6;  // the bar's opacity 1 -> 0 over the first 60 % of the entry's path
inline constexpr double exitFadeShare = 0.4;   // 0 -> 1 over the first 40 % of the exit's path
inline constexpr double crossFadeMs = 120;     // frozen picture -> live page; reduced motion: the whole switch
inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double omega = 2 * kPi / responseS;   // the undamped angular frequency of a « response » period

// ---- The spring (critically damped, analytic) ---------------------------------------------------------------------
// Position 0 = the normal layout, 1 = Mode Focus. Retarget keeps the position and the velocity on screen.
struct Spring {
    double x = 0, v = 0, target = 0;
    void Snap(double value) { x = target = value; v = 0; }
    void Retarget(double t) { target = t; }
    // Advances by dt seconds: x(t) = T + (d + (v + w d) t) e^(-w t), v(t) = (v - w (v + w d) t) e^(-w t), d = x - T.
    void Step(double dt) {
        if (dt <= 0) return;
        const double d = x - target, e = std::exp(-omega * dt), c = v + omega * d;
        x = target + (d + c * dt) * e;
        v = (v - omega * c * dt) * e;
        if (Settled()) { x = target; v = 0; }
    }
    bool Settled() const { return std::fabs(x - target) < 5e-4 && std::fabs(v) < 5e-3; }
};

// ---- The bar's opacity along the path -----------------------------------------------------------------------------
// From where the move (re)started (its position and the opacity on screen then) toward the target's opacity, over the
// share of the path the spec gives: from rest it is exactly the spec's curve; a reversal mid-way starts from what is on
// screen (no jump in opacity either).
struct Fade {
    double fromX = 0, toX = 1, fromOpacity = 1, toOpacity = 0, share = enterFadeShare;
    static Fade Toward(double target, double x, double opacityNow) {
        Fade f; f.fromX = x; f.toX = target; f.fromOpacity = opacityNow; f.toOpacity = target >= .5 ? 0 : 1; f.share = target >= .5 ? enterFadeShare : exitFadeShare; return f;
    }
    double At(double x) const {
        const double span = std::fabs(toX - fromX);
        const double path = span > 1e-9 ? std::clamp(std::fabs(x - fromX) / span, 0.0, 1.0) : 1.0;
        return fromOpacity + (toOpacity - fromOpacity) * std::clamp(path / share, 0.0, 1.0);
    }
};

// ---- The in-between layout ----------------------------------------------------------------------------------------
struct Rect { float left = 0, top = 0, right = 0, bottom = 0; };
inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
inline Rect Lerp(const Rect& a, const Rect& b, float t) { return {Lerp(a.left, b.left, t), Lerp(a.top, b.top, t), Lerp(a.right, b.right, t), Lerp(a.bottom, b.bottom, t)}; }
// One layout's places (client DIP): the bar (tabs, toolbar, bookmarks band: 0 .. barHeight), the page (its corner radii),
// and the surfaces that follow it.
struct Layout {
    float barHeight = 0;               // the top bar's height (0 in Mode Focus: hidden)
    Rect page;                         // the page's box (the split view's whole area)
    float topRadius = 0, bottomRadius = 0;
    Rect chat;                         // the chat card / panel (empty: none)
    float drawerTop = 0;               // the docked drawers' top
};
// The frame at spring position x (0 normal .. 1 focus; clamped: the spring never overshoots from rest, a reversal may
// carry a little): every edge on the same curve, so the page's top stays on the bar's bottom plus the growing inset.
struct Frame {
    float barOffset = 0, barOpacity = 1;   // the bar's translation (DIP, <= 0) and opacity
    Rect page; float topRadius = 0, bottomRadius = 0;
    Rect chat; float drawerTop = 0;
};
inline Frame At(const Layout& normal, const Layout& focus, double x, double barOpacity) {
    const float t = static_cast<float>(std::clamp(x, 0.0, 1.0));
    Frame f;
    f.barOffset = -normal.barHeight * t;
    f.barOpacity = static_cast<float>(std::clamp(barOpacity, 0.0, 1.0));
    f.page = Lerp(normal.page, focus.page, t);
    f.topRadius = Lerp(normal.topRadius, focus.topRadius, t); f.bottomRadius = Lerp(normal.bottomRadius, focus.bottomRadius, t);
    f.chat = Lerp(normal.chat, focus.chat, t);
    f.drawerTop = Lerp(normal.drawerTop, focus.drawerTop, t);
    return f;
}
// The gap between the bar's bottom edge and the page's top edge at that frame (the canvas inset: >= 0, never a hole
// wider than the inset the page is heading to).
inline float Gap(const Layout& normal, const Frame& f) { return f.page.top - (normal.barHeight + f.barOffset); }

}}
