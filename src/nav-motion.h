#pragma once
// Toolbar navigation motion: Back/Forward glyph travel and the Reload <-> Stop morph.
//
// Pure models (no HWND, drawing or timers), sampled like download-icon.h: the host
// feeds real engine state, Chrome paints the pose, ScheduleMotion asks NextWake.
// Nothing here ever asks for frames while at rest, and nothing locks input.
#include <algorithm>
#include <chrono>
#include <cmath>
#include "motion.h"
#include "motion-presentation.h"

namespace NativeChrome { namespace NavMotion {

// Back/Forward: after a successful GoBack/GoForward the arrow glyph travels a few DIP
// in its own direction and settles back on a critically damped spring (damping 1,
// response 0.4 s, Apple's "move" spring). The press itself is the existing pointer-down
// surface + .96 glyph scale, so feedback is immediate; the travel only confirms the
// action. A new activation adds velocity to the live state (never restarts from a
// target), scaled by the room left before the peak, so rapid clicks stay continuous and a Back right after a Forward reverses
// smoothly. Displacement is visual only: hit testing and focus never move.
namespace Travel {
inline constexpr double response = .4;                       // seconds
inline constexpr double omega = 2 * 3.14159265358979 / response;
inline constexpr float peak = 3;                             // DIP, reached ~1/omega (64 ms) after a click from rest
inline constexpr float restPosition = .02f, restSpeed = .5f; // below both: at rest, no more frames
}
// Tunable (motion lab): peak travel in DIP, response in seconds, damping ratio (1 = no
// overshoot; below 1 the glyph overshoots rest, Apple's momentum feel). Defaults above.
struct SpringParams { double peak = Travel::peak, response = Travel::response, damping = 1; };
// Peak displacement reached from rest per unit of initial velocity.
inline double UnitPeak(double omega, double zeta) {
    if (zeta >= 1) return 1 / (omega * 2.718281828459045);
    const double wd = omega * std::sqrt(1 - zeta * zeta), t = std::atan2(wd, zeta * omega) / wd;
    return std::exp(-zeta * omega * t) * std::sin(wd * t) / wd;
}
class Spring {
    double x0_ = 0, v0_ = 0, w_ = Travel::omega, z_ = 1; Motion::Time t0_{}; bool moving_ = false;
    double Tau(Motion::Time now) const { return std::max(0., std::chrono::duration<double>(now - t0_).count()); }
public:
    // Critically damped: x(t) = (x0 + (v0 + w x0) t) e^(-w t); underdamped: the
    // standard e^(-zwt)(x0 cos wd t + (v0 + zw x0)/wd sin wd t).
    float Position(Motion::Time now) const {
        if (!moving_) return 0;
        const double t = Tau(now), w = w_;
        if (z_ >= 1) return static_cast<float>((x0_ + (v0_ + w * x0_) * t) * std::exp(-w * t));
        const double wd = w * std::sqrt(1 - z_ * z_);
        return static_cast<float>(std::exp(-z_ * w * t) * (x0_ * std::cos(wd * t) + (v0_ + z_ * w * x0_) / wd * std::sin(wd * t)));
    }
    float Velocity(Motion::Time now) const {
        if (!moving_) return 0;
        const double t = Tau(now), w = w_;
        if (z_ >= 1) return static_cast<float>((v0_ - w * (v0_ + w * x0_) * t) * std::exp(-w * t));
        const double wd = w * std::sqrt(1 - z_ * z_);
        return static_cast<float>(std::exp(-z_ * w * t) * (v0_ * std::cos(wd * t) - (z_ * w * v0_ + w * w * x0_) / wd * std::sin(wd * t)));
    }
    // direction: -1 left (Back), +1 right (Forward). Reduced motion: no travel at all.
    // Parameters are captured per kick, so a change never jumps a glyph mid-flight
    // (the live position and velocity carry over into the new spring).
    void Kick(int direction, Motion::Time now, bool reduced, SpringParams p = {}) {
        if (reduced || p.peak <= 0) { Stop(); return; }
        const double x = Position(now), v0 = Velocity(now);
        w_ = 2 * 3.14159265358979 / std::max(.05, p.response); z_ = std::clamp(p.damping, .3, 1.);
        const double impulse = p.peak / UnitPeak(w_, z_);
        // The impulse shrinks with the travel already done in that direction, so rapid
        // clicks keep the glyph near its peak instead of throwing it further.
        const double headroom = std::clamp(1 - direction * x / p.peak, 0., 1.);
        const double v = v0 + direction * impulse * headroom;
        x0_ = x; v0_ = std::clamp(v, -impulse * 1.25, impulse * 1.25); t0_ = now; moving_ = true;
    }
    bool Moving(Motion::Time now) const {
        return moving_ && (std::fabs(Position(now)) >= Travel::restPosition || std::fabs(Velocity(now)) >= Travel::restSpeed);
    }
    void Settle(Motion::Time now) { if (moving_ && !Moving(now)) moving_ = false; }
    void Stop() { moving_ = false; x0_ = v0_ = 0; }
    Motion::Wake NextWake(Motion::Time now) const { return Moving(now) ? Motion::Frame() : Motion::Wake{}; }
};

// Reload <-> Stop. Real states only, no fake delay:
//   - Reload clicked: the authored Lottie turn (one CCW revolution, 500 ms) plays.
//   - The tab is loading once the turn has ended (or immediately, for a navigation
//     that did not come from Reload): the reload glyph keeps turning CCW a quarter
//     while shrinking/fading out and the X turns in the same direction, growing in
//     (Curve::Icon, 240 ms; same layer scales as the Copy -> check swap).
//   - Loading ends (completed, failed or stopped): the X morphs back to Reload.
//   - A load that ends during the turn never shows the X (nothing left to stop).
// The morph re-targets from its current value, so a stop/start mid-morph reverses
// without a jump. Reduced motion swaps the glyphs instantly.
inline constexpr Motion::Milliseconds MorphDuration{240};
inline constexpr float MorphTurn = 90; // degrees, counter-clockwise like the reload arrow
// The morph's tunables (motion lab); defaults above.
struct StopLayers { Motion::IconLayer reload, stop; float reloadAngle = 0, stopAngle = 0; };
class StopMorph {
    Motion::Tween value_;
    bool loading_ = false, reduced_ = false;
    Motion::Time hold_{};    // end of the Reload turn: the X waits for it
    Motion::Time preview_{}; // motion lab replay: shows the loading cycle without navigating
    Motion::Milliseconds duration_ = MorphDuration; float turn_ = MorphTurn;
    void Apply(Motion::Time now) {
        const bool loading = loading_ || now < preview_;
        const float target = loading && (reduced_ || now >= hold_) ? 1.f : 0.f; // reduced: no turn to wait for
        value_.Set(target, now, duration_, reduced_, Motion::Curve::Icon);
    }
public:
    void Configure(Motion::Milliseconds duration, float turn) { duration_ = duration; turn_ = turn; }
    // Visual-only loading until `until` (motion lab replay). Real loading still wins.
    void Preview(Motion::Time until, Motion::Time turnEnd, Motion::Time now, bool reduced) { preview_ = until; hold_ = std::max(hold_, turnEnd); reduced_ = reduced; Apply(now); }
    // loading: the active tab's navigation is in flight. turnEnd: when the Reload turn
    // ends (any past time when no turn is playing).
    void Sync(bool loading, Motion::Time turnEnd, Motion::Time now, bool reduced) {
        loading_ = loading; hold_ = turnEnd; reduced_ = reduced; Apply(now);
    }
    // Time-driven gate (turn end): call on every motion tick.
    void Settle(Motion::Time now) { Apply(now); }
    // Tab switch / reduced motion: land on the state without animating.
    void Snap(bool loading) { loading_ = loading; hold_ = preview_ = {}; value_.Snap(loading ? 1.f : 0.f); }
    float Value(Motion::Time now) const { return value_.Value(now); }
    bool Moving(Motion::Time now) const { return value_.Moving(now); }
    StopLayers Layers(Motion::Time now) const {
        const float a = Value(now);
        const auto layers = Motion::CopyPresentation({Motion::SuccessPhase::Idle, a, 1}, reduced_);
        StopLayers out{layers.copy, layers.check};
        if (!reduced_) { out.reloadAngle = -turn_ * a; out.stopAngle = turn_ * (1 - a); }
        return out;
    }
    Motion::Wake NextWake(Motion::Time now) const {
        if (value_.Moving(now)) return Motion::Frame();
        auto wake = (loading_ || now < preview_) && now < hold_ && !reduced_ ? Motion::Deadline(now, hold_) : Motion::Wake{};
        if (now < preview_) wake = Motion::Earlier(wake, Motion::Deadline(now, preview_));
        return wake;
    }
};

struct State {
    Spring back, forward;
    StopMorph stop;
    void Settle(Motion::Time now) { back.Settle(now); forward.Settle(now); stop.Settle(now); }
    void Reduce(bool loading) { back.Stop(); forward.Stop(); stop.Snap(loading); }
    Motion::Wake NextWake(Motion::Time now) const {
        return Motion::Earlier(Motion::Earlier(back.NextWake(now), forward.NextWake(now)), stop.NextWake(now));
    }
};
}} // namespace NativeChrome::NavMotion
