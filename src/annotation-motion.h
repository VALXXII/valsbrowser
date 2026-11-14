#pragma once
// Annotation mode — pure motion model (no HWND, no Direct2D, no clock reads).
// V2 values: Paper « MCP - Design UI UX », planches « Spéc » (Lancement 10X1-0, Connexion des
// agents ZJQ-0, Annotations 10IT-0, Historique 1167-0) + apple-design / better-ui / emil:
//  - Modal: opacity + scale 0.96 -> 1 about its centre, 200 ms cubic-bezier(0.23, 1, 0.32, 1);
//    exit by the same path, shorter: 150 ms, scale -> 0.98, opacity -> 0. Veil 0 -> 1 in 200 ms.
//    The « Reste sur cet ordinateur » pill follows 60 ms later (opacity + 4 DIP).
//  - Note panel: 180 ms, same curve, from the target side; its pill follows 60 ms later.
//  - Launcher cards and the history panel are born from the launcher (origin bottom-right),
//    200 ms in, 150 ms out by the same path. History detail: slides from the right (cross-fade).
//  - Button states (En cours -> Connecté) cross-fade on a 0.3 s critical spring (no bounce).
//  - Manual fold: height + opacity 200 ms cubic-bezier(0.77, 0, 0.175, 1); keyboard = instant.
//  - Press 0.96 over 150 ms ease-out (120 ms on the connect buttons), never when disabled.
//  - Every ramp retargets from the PRESENTED value: an interruption never jumps.
//  - Reduced motion: opacity fades only (modal 150 ms in / 120 ms out), no scale, no lift;
//    the « en cours » ring stops turning (the badge and the static arc stay as cues).
// Every function takes the caller's `now`, so tests sample exact instants.
#include "motion.h"
#include "motion-presentation.h"
#include <cmath>

namespace NativeChrome { namespace Annotations {
// CSS cubic-bezier(x1, y1, x2, y2): y for a given x (progress in time), by bisection on x.
struct Bezier {
    float x1, y1, x2, y2;
    float operator()(float x) const noexcept {
        x = Motion::Clamp(x); if (x == 0 || x == 1) return x;
        float lo = 0, hi = 1;
        for (int n = 0; n < 24; ++n) {
            const float t = (lo + hi) * .5f, u = 1 - t;
            const float s = 3 * u * u * t * x1 + 3 * u * t * t * x2 + t * t * t;
            if (s < x) lo = t; else hi = t;
        }
        const float t = (lo + hi) * .5f, u = 1 - t;
        return 3 * u * u * t * y1 + 3 * u * t * t * y2 + t * t * t;
    }
};
namespace Curves {
inline constexpr Bezier out{.23f, 1, .32f, 1};       // panels, modal, cards, pills
inline constexpr Bezier inOut{.77f, 0, .175f, 1};    // the manual fold
inline constexpr Bezier easeOut{0, 0, .58f, 1};      // press (CSS ease-out)
inline constexpr Bezier band{.32f, .72f, 0, 1};       // V3 « Spéc · Entrée du mode » (Paper 5.2): the band's slide
}

namespace Spec {
// V2 timings (ms).
inline constexpr int modalIn = 200, modalOut = 150, modalReducedIn = 150, modalReducedOut = 120;
inline constexpr int noteIn = 180, noteOut = 150, pillDelay = 60, pillUnfold = 200;
inline constexpr int panelIn = 200, panelOut = 150, fold = 200, bannerFade = 120, modalAfterBanner = 80;
inline constexpr int localIntent = 150, localDescDelay = 60, localOut = 120, toastMs = 6000, failFade = 150;
inline constexpr float enterScale = .96f, exitScale = .98f, pillLift = 4, stateResponse = .3f;
inline constexpr float popoverLift = 8, panelLift = 10;  // V1 (onboarding, kept for the deprecated surface)
inline constexpr float veilOpacity = 140 / 255.f;       // Paper « Voile » #0000008C
inline constexpr int stepFadeMs = 200;
inline constexpr float stepHeightResponse = .35f, enterResponse = .35f, exitResponse = .16f;
inline constexpr double spinnerPeriod = 1.2;            // s per turn
inline constexpr int addedDwellMs = 4000;               // « Annotation ajoutée » (agent connected, no action): one-shot
inline constexpr int verifyHelpMs = 120000;             // V1 onboarding only
}

// A value moved along a cubic-bezier over a fixed duration, optionally after a delay.
// Set() starts from the value presented at `now`; the same target never restarts the clock.
class Ramp {
    float from_ = 0, to_ = 0; Motion::Time begin_{}, end_{}; Bezier curve_ = Curves::out;
public:
    float Value(Motion::Time now) const noexcept {
        if (now >= end_) return to_;
        if (now <= begin_) return from_;
        const double f = std::chrono::duration<double>(now - begin_).count() / std::chrono::duration<double>(end_ - begin_).count();
        return from_ + (to_ - from_) * curve_(float(f));
    }
    float Target() const noexcept { return to_; }
    bool Moving(Motion::Time now) const noexcept { return from_ != to_ && now < end_; }
    void Set(float target, Motion::Time now, int ms, Bezier curve = Curves::out, int delayMs = 0) noexcept {
        if (ms <= 0) { Snap(target); return; }
        if (target == to_) return;
        from_ = Value(now); to_ = target; curve_ = curve;
        begin_ = now + std::chrono::milliseconds(delayMs); end_ = begin_ + std::chrono::milliseconds(ms);
    }
    void Snap(float v) noexcept { from_ = to_ = v; begin_ = end_ = Motion::Time{}; }
};

struct Pose { float opacity = 0, scale = 1, dx = 0, dy = 0; };
struct Timing {
    int enter = Spec::panelIn, exit = Spec::panelOut, reducedEnter = Spec::modalReducedIn, reducedExit = Spec::modalReducedOut, delay = 0;
    float from = Spec::enterScale, to = Spec::exitScale;
    Bezier curve = Curves::out;
};
inline Timing ModalTiming() { return {}; }
inline Timing NoteTiming() { Timing t; t.enter = Spec::noteIn; t.exit = Spec::noteOut; return t; }
inline Timing PillTiming() { Timing t; t.enter = Spec::pillUnfold; t.exit = Spec::noteOut; t.delay = Spec::pillDelay; t.from = 1; t.to = 1; return t; }

// One surface's presence: opacity and scale ramps (enter 0.96 -> 1, exit -> 0.98), both
// retargeted from what is on screen. animate=false snaps (first layout, tests).
class Presence {
    Ramp opacity_, scale_; bool open_ = false, reduced_ = false; Timing t_;
public:
    Presence() { scale_.Snap(t_.from); }
    explicit Presence(Timing t) : t_(t) { scale_.Snap(t_.from); }
    void Configure(Timing t) noexcept { t_ = t; if (!open_ && !opacity_.Moving(Motion::Time{})) scale_.Snap(t_.from); }
    const Timing& Times() const noexcept { return t_; }
    bool IsOpen() const noexcept { return open_; }
    void Show(Motion::Time now, bool animate = true, bool reduced = false) noexcept {
        open_ = true; reduced_ = reduced;
        if (!animate) { opacity_.Snap(1); scale_.Snap(1); return; }
        if (opacity_.Value(now) <= 0 && !opacity_.Moving(now)) scale_.Snap(t_.from);
        opacity_.Set(1, now, reduced ? t_.reducedEnter : t_.enter, t_.curve, t_.delay);
        scale_.Set(1, now, reduced ? 0 : t_.enter, t_.curve, t_.delay);
    }
    void Hide(Motion::Time now, bool animate = true, bool reduced = false) noexcept {
        open_ = false; reduced_ = reduced;
        if (!animate) { opacity_.Snap(0); scale_.Snap(t_.from); return; }
        opacity_.Set(0, now, reduced ? t_.reducedExit : t_.exit, t_.curve);
        scale_.Set(t_.to, now, reduced ? 0 : t_.exit, t_.curve);
    }
    float Progress(Motion::Time now) const noexcept { return Motion::Clamp(opacity_.Value(now)); }
    float Raw(Motion::Time now) const noexcept { return opacity_.Value(now); }
    float Scale(Motion::Time now) const noexcept { return scale_.Value(now); }
    bool Moving(Motion::Time now) const noexcept { return opacity_.Moving(now) || scale_.Moving(now); }
    bool Visible(Motion::Time now) const noexcept { return open_ || Progress(now) > 0 || opacity_.Moving(now); }
    // lift: the offset (DIP) the surface starts from (pointing away from where it lands).
    Pose Sample(Motion::Time now, bool reduced, float liftX = 0, float liftY = 0) const noexcept {
        Pose s; s.opacity = Progress(now);
        if (!reduced) { s.scale = scale_.Value(now); s.dx = (1 - s.opacity) * liftX; s.dy = (1 - s.opacity) * liftY; }
        return s;
    }
};

// State cross-fade (button contents, card contents, steps): the incoming view rises on a
// 0.3 s critical spring; an interruption starts from the alpha the outgoing view shows.
class Crossfade {
    Motion::Spring spring_{.002f};
    int from_ = -1, to_ = 0; float fromAlpha_ = 1;
public:
    int Current() const noexcept { return to_; }
    int Previous() const noexcept { return from_; }
    void Reset(int state) noexcept { from_ = -1; to_ = state; fromAlpha_ = 1; spring_.Snap(1); }
    void Go(int state, Motion::Time now, bool animate = true) noexcept {
        if (state == to_) return;
        fromAlpha_ = Incoming(now); from_ = to_; to_ = state;
        spring_.Snap(0); if (animate) spring_.Set(1, now, Spec::stateResponse); else spring_.Snap(1);
    }
    float Incoming(Motion::Time now) const noexcept { return Motion::Clamp(spring_.Value(now)); }
    float Outgoing(Motion::Time now) const noexcept { return from_ < 0 ? 0.f : fromAlpha_ * (1 - Incoming(now)); }
    bool Moving(Motion::Time now) const noexcept { return spring_.Moving(now); }
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
inline float Lerp(float a, float b, float t) noexcept { return a + (b - a) * t; }
}} // namespace NativeChrome::Annotations
