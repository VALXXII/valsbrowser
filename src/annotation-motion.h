#pragma once
// Annotation mode — pure motion model (no HWND, no Direct2D, no clock reads).
// Contract: plan-annotations-mcp-local/design/annotations-v1-brief.md, « Mouvement ».
//  - Critically damped springs (Motion::Spring, damping 1.0): enter response 0.35 s,
//    exit response 0.16 s (98 % of the way after 150 ms: the exit is the entry path,
//    shorter). Retargeting samples the PRESENTED value and velocity, so a close during
//    an opening (or the reverse) continues without a jump.
//  - Entrance pose: opacity 0 -> 1, scale 0.96 -> 1 about the surface's origin (the
//    target for the note popover, the launcher for the card, the centre for the modal),
//    plus a lift of 8 DIP (popover, toward the target) or 10 DIP (panels, toward the
//    launcher). Reduced motion keeps the same spring for OPACITY only: no scale, no lift.
//  - Press: 0.96 over 150 ms ease-out (Motion::Interaction polish + Motion::PressScale),
//    never on a disabled control.
//  - Onboarding steps cross-fade (200 ms ease-out, no movement); the modal height follows
//    a 0.35 s critical spring.
//  - Running ring: one turn per 1.2 s while visible; fixed under reduced motion.
// Every function takes the caller's `now`, so tests sample exact instants.
#include "motion.h"
#include "motion-presentation.h"
#include <cmath>

namespace NativeChrome { namespace Annotations {
namespace Spec {
inline constexpr float enterResponse = .35f, exitResponse = .16f;
inline constexpr float enterScale = .96f;              // apple-design / better-ui: never below 0.96
inline constexpr float popoverLift = 8, panelLift = 10; // DIP
inline constexpr float veilOpacity = 140 / 255.f;       // Paper « Voile » #0000008C
inline constexpr int stepFadeMs = 200;
inline constexpr float stepHeightResponse = .35f;
inline constexpr double spinnerPeriod = 1.2;            // s per turn
inline constexpr int addedDwellMs = 4000;               // « Annotation ajoutée » (no action): one-shot
inline constexpr int verifyHelpMs = 120000;             // « Ça ne marche pas ? » after 2 min: one-shot
}

struct Pose { float opacity = 0, scale = 1, dx = 0, dy = 0; };

// One surface's presence (shown / hidden) as a critically damped spring on [0, 1].
class Presence {
    Motion::Spring spring_{.0005f};
    bool open_ = false;
public:
    bool IsOpen() const noexcept { return open_; }
    // animate=false snaps (first layout, a test, or a surface that must appear at once).
    void Show(Motion::Time now, bool animate = true) noexcept { open_ = true; spring_.Set(1, now, Spec::enterResponse, !animate); }
    void Hide(Motion::Time now, bool animate = true) noexcept { open_ = false; spring_.Set(0, now, Spec::exitResponse, !animate); }
    float Raw(Motion::Time now) const noexcept { return spring_.Value(now); }
    float Progress(Motion::Time now) const noexcept { return Motion::Clamp(spring_.Value(now)); }
    float Velocity(Motion::Time now) const noexcept { return spring_.Velocity(now); }
    bool Moving(Motion::Time now) const noexcept { return spring_.Moving(now); }
    bool Visible(Motion::Time now) const noexcept { return open_ || (Moving(now) && Progress(now) > 0); }
    // lift: the offset (DIP) the surface starts from, pointing AWAY from where it lands.
    Pose Sample(Motion::Time now, bool reduced, float liftX = 0, float liftY = 0) const noexcept {
        const float p = Progress(now);
        Pose s; s.opacity = p;
        if (!reduced) { s.scale = Spec::enterScale + (1 - Spec::enterScale) * p; s.dx = (1 - p) * liftX; s.dy = (1 - p) * liftY; }
        return s;
    }
};

// Onboarding step cross-fade. Interrupting a fade starts the next one from the alpha the
// outgoing view is presented at (never from 1), so nothing flashes back in.
class Crossfade {
    Motion::Tween tween_;
    int from_ = -1, to_ = 0;
    float fromAlpha_ = 1;
public:
    int Current() const noexcept { return to_; }
    int Previous() const noexcept { return from_; }
    void Reset(int step) noexcept { from_ = -1; to_ = step; fromAlpha_ = 1; tween_.Snap(1); }
    void Go(int step, Motion::Time now) noexcept {
        if (step == to_) return;
        fromAlpha_ = Incoming(now);
        from_ = to_; to_ = step;
        tween_.Snap(0); tween_.Set(1, now, Motion::Milliseconds(Spec::stepFadeMs), false, Motion::Curve::EaseOut);
    }
    float Incoming(Motion::Time now) const noexcept { return tween_.Value(now); }
    float Outgoing(Motion::Time now) const noexcept { return from_ < 0 ? 0.f : fromAlpha_ * (1 - tween_.Value(now)); }
    bool Moving(Motion::Time now) const noexcept { return tween_.Moving(now); }
};

// Rotation of the « en cours » ring, degrees. Continuous across frames (absolute clock).
inline float SpinnerAngle(Motion::Time now, bool reduced) noexcept {
    if (reduced) return 0;
    const double s = std::chrono::duration<double>(now.time_since_epoch()).count();
    return static_cast<float>(std::fmod(s / Spec::spinnerPeriod, 1.0) * 360.0);
}

// Press feedback of a drawn control: 0.96 at full press, never when disabled or reduced.
inline float PressScale(const Motion::InteractionVisual& v, bool enabled, bool reduced) noexcept {
    return Motion::PressScale(v.press, reduced, enabled);
}
}} // namespace NativeChrome::Annotations
