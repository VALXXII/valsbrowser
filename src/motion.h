#pragma once

// Native-only, header-only C++17 motion model. All calls belong to the UI thread.
// Pass the same steady-clock timestamp to model updates, sampling and scheduling.
#include <chrono>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#ifdef _WIN32
#include <windows.h>
#endif

namespace NativeChrome { namespace Motion {
using Clock = std::chrono::steady_clock;
using Time = Clock::time_point;
using Milliseconds = std::chrono::milliseconds;
inline Time Now() noexcept { return Clock::now(); }

// Conservative fallback: if Windows cannot report the setting, avoid motion.
inline bool SystemReducedMotion() noexcept {
#ifdef _WIN32
    BOOL enabled = FALSE;
    return !SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0) || !enabled;
#else
    return true;
#endif
}
// The app's own "Réduire les animations": either switch quiets every surface.
// Process-wide so popups created later (and fixtures, which never set it) agree.
inline bool appReducedMotion = false;
inline void SetAppReducedMotion(bool reduced) noexcept { appReducedMotion = reduced; }
inline bool PreferredReducedMotion() noexcept { return appReducedMotion || SystemReducedMotion(); }

inline float Clamp(float value) noexcept { return value < 0 ? 0 : (value > 1 ? 1 : value); }
inline float Ease(float value) noexcept { const float t = Clamp(value); return t*t*(3-2*t); }
enum class Curve { SmoothStep, EaseOut, Icon };
// Native equivalents of CSS ease-out and cubic-bezier(.2,0,0,1).
// Invert x by bounded bisection: fixed cost, no overshoot or platform dependency.
inline float Eased(float value, Curve curve) noexcept {
    if (curve == Curve::SmoothStep) return Ease(value);
    const float x = Clamp(value);
    if (x == 0 || x == 1) return x;
    const float x1 = curve == Curve::Icon ? .2f : 0.f;
    const float x2 = curve == Curve::Icon ? 0.f : .58f;
    float lo = 0, hi = 1;
    for (int n = 0; n < 20; ++n) {
        const float t = (lo+hi)*.5f, u = 1-t;
        const float sample = 3*u*u*t*x1+3*u*t*t*x2+t*t*t;
        if (sample < x) lo = t; else hi = t;
    }
    const float t = (lo+hi)*.5f;
    return t*t*(3-2*t);
}
inline float Fraction(Time now, Time begin, Time end) noexcept {
    if (now <= begin) return 0;
    if (now >= end) return 1;
    return static_cast<float>(std::chrono::duration<double>(now-begin).count() /
                              std::chrono::duration<double>(end-begin).count());
}

// nullopt means KillTimer. Deadline hints allow an idle dwell without 60 Hz ticks.
// SetTimer is recurring: kill it on dispatch, then re-arm from a fresh hint.
struct Wake {
    std::optional<Milliseconds> delay;
    bool frames = false;
};
inline Wake Frame() noexcept { return {Milliseconds(16), true}; }
inline Wake Deadline(Time now, Time when) noexcept {
    if (when <= now) return {};
    auto delay = std::chrono::ceil<Milliseconds>(when-now);
    return {delay, false};
}
inline Wake Earlier(Wake a, Wake b) noexcept {
    if (!a.delay) return b;
    if (!b.delay) return a;
    if (*a.delay == *b.delay) return {a.delay, a.frames || b.frames};
    return *a.delay < *b.delay ? a : b;
}

class Tween {
    float from_ = 0, target_ = 0;
    Time begin_{}, end_{};
    Curve curve_ = Curve::SmoothStep;
public:
    float Value(Time now) const noexcept {
        if (now >= end_) return target_;
        return from_ + (target_-from_)*Eased(Fraction(now, begin_, end_), curve_);
    }
    bool Moving(Time now) const noexcept { return from_ != target_ && now < end_; }
    void Set(float value, Time now, Milliseconds duration, bool reduced = false,
             Curve curve = Curve::SmoothStep) noexcept {
        value = Clamp(value);
        if (reduced || duration.count() <= 0) { Snap(value); return; }
        if (value == target_) return; // Repeated input/paint must not restart the clock.
        from_ = Value(now); target_ = value; begin_ = now; end_ = now+duration;
        curve_ = curve;
    }
    void Snap(float value) noexcept { from_ = target_ = Clamp(value); begin_ = end_ = Time{}; }
    void Settle() noexcept { Snap(target_); }
};

// Critically damped spring (damping ratio 1: no overshoot from rest, no bounce).
// `response` is Apple's designer parameter in seconds (period of the undamped
// oscillator), not a duration. Retargeting samples the presented value AND
// velocity, so a reversal or a rapid repeat continues without a jump or a
// velocity "brick wall". Unclamped: callers clamp where a range matters.
// Settled values report the exact target, so a static layout is bit-exact.
class Spring {
    float target_ = 0, offset_ = 0, velocity_ = 0, omega_ = 0, epsilon_;
    Time begin_{};
    bool active_ = false;
    void Evaluate(Time now, float& value, float& velocity) const noexcept {
        if (!active_) { value = target_; velocity = 0; return; }
        double t = std::chrono::duration<double>(now-begin_).count();
        if (t < 0) t = 0;
        const double c1 = offset_, c2 = velocity_+omega_*c1, e = std::exp(-omega_*t);
        const double x = (c1+c2*t)*e, v = (c2-omega_*(c1+c2*t))*e;
        if (std::fabs(x) < epsilon_ && std::fabs(v) < epsilon_*omega_) { value = target_; velocity = 0; return; }
        value = static_cast<float>(target_+x); velocity = static_cast<float>(v);
    }
public:
    explicit Spring(float epsilon = .001f) noexcept : epsilon_(epsilon) {}
    float Target() const noexcept { return target_; }
    float Value(Time now) const noexcept { float x, v; Evaluate(now, x, v); return x; }
    float Velocity(Time now) const noexcept { float x, v; Evaluate(now, x, v); return v; }
    bool Moving(Time now) const noexcept {
        if (!active_) return false;
        float x, v; Evaluate(now, x, v); return x != target_ || v != 0;
    }
    void Snap(float value) noexcept { target_ = value; offset_ = velocity_ = 0; active_ = false; }
    void Settle() noexcept { Snap(target_); }
    // Same target never restarts the clock (repaints and repeated input are free).
    void Set(float target, Time now, float response, bool reduced = false) noexcept {
        if (reduced || response <= 0) { Snap(target); return; }
        if (target == target_) return;
        float x, v; Evaluate(now, x, v);
        target_ = target; offset_ = x-target; velocity_ = v; begin_ = now;
        omega_ = static_cast<float>(6.283185307179586/response);
        active_ = offset_ != 0 || velocity_ != 0;
    }
};

struct InteractionVisual { float hover = 0, press = 0; };
class Interaction {
    Tween hover_, press_;
    bool polished_ = false;
public:
    void EnablePolish(bool enabled = true) noexcept { polished_ = enabled; }
    void Set(bool hover, bool press, Time now, bool reduced = false) noexcept {
        const auto curve = polished_ ? Curve::EaseOut : Curve::SmoothStep;
        hover_.Set(hover ? 1.f : 0.f, now, Milliseconds(120), reduced, curve);
        press_.Set(press ? 1.f : 0.f, now, Milliseconds(polished_ ? 150 : 80), reduced, curve);
    }
    InteractionVisual Visual(Time now) const noexcept { return {hover_.Value(now), press_.Value(now)}; }
    Wake NextWake(Time now) const noexcept { return hover_.Moving(now) || press_.Moving(now) ? Frame() : Wake{}; }
    void Settle() noexcept { hover_.Settle(); press_.Settle(); }
};

enum class SuccessPhase { Idle, Reveal, Dwell, Disappear };
struct SuccessVisual {
    SuccessPhase phase = SuccessPhase::Idle;
    float opacity = 0; // opacity for check-bold; copy icon opacity is 1-opacity.
    float scale = 1;   // Optional centered glyph scale; never affects hit geometry.
};
class Success {
    bool triggered_ = false, reduced_ = false;
    bool polished_ = false;
    float revealFrom_ = 0;
    Time begin_{}, revealEnd_{}, dwellEnd_{}, end_{};
public:
    // Configure before first use. Existing users retain their timing and visuals.
    void EnablePolish(bool enabled = true) noexcept { polished_ = enabled; }
    void Trigger(Time now, bool reduced = false) noexcept {
        revealFrom_ = polished_ ? Visual(now).opacity : 0;
        triggered_ = true; reduced_ = reduced; begin_ = now;
        // A repeat extends dwell without blinking, including during disappearance.
        revealEnd_ = now+Milliseconds(polished_ ? (revealFrom_ == 1 ? 0 : (reduced ? 150 : 300)) : (reduced ? 0 : 150));
        dwellEnd_ = revealEnd_+Milliseconds(1500);
        end_ = dwellEnd_+Milliseconds(polished_ ? 150 : (reduced ? 0 : 150));
    }
    void Clear() noexcept { triggered_ = false; }
    void ReduceMotion() noexcept {
        // Preserve the original dwell deadline; do not restart or resurrect it.
        reduced_ = true;
        if (!polished_) { revealEnd_ = begin_; end_ = dwellEnd_; }
    }
    SuccessVisual Visual(Time now) const noexcept {
        if (!triggered_ || now >= end_) return {};
        if (now < revealEnd_) {
            const auto t = Fraction(now, begin_, revealEnd_);
            const auto a = polished_ ? revealFrom_+(1-revealFrom_)*Eased(t, Curve::Icon) : Ease(t);
            return {SuccessPhase::Reveal, a, reduced_ ? 1.f : (polished_ ? .25f+.75f*a : .88f+.12f*a)};
        }
        if (now < dwellEnd_) return {SuccessPhase::Dwell, 1, 1};
        const auto t = Fraction(now, dwellEnd_, end_);
        const auto a = 1-(polished_ ? Eased(t, Curve::Icon) : Ease(t));
        return {SuccessPhase::Disappear, a, reduced_ ? 1.f : (polished_ ? .25f+.75f*a : .88f+.12f*a)};
    }
    Wake NextWake(Time now) const noexcept {
        switch (Visual(now).phase) {
        case SuccessPhase::Reveal: case SuccessPhase::Disappear: return Frame();
        case SuccessPhase::Dwell: return Deadline(now, dwellEnd_);
        default: return {};
        }
    }
};

enum class DownloadStatus { None, InProgress, Paused, Completed, Interrupted, Cancelled };
struct DownloadInput {
    DownloadStatus status = DownloadStatus::None;
    std::uint64_t received = 0;
    std::optional<std::uint64_t> total; // Missing/zero total means unknown, never simulated.
};
struct DownloadVisual {
    DownloadStatus status = DownloadStatus::None;
    bool visible = false, determinate = false;
    float progress = 0;
    SuccessVisual completion;
};
class Download {
    DownloadInput input_;
    Success completion_;
public:
    // One operation (or an externally computed aggregate) per model. Feed real
    // engine events only. A byte count reaching total does NOT imply completion.
    void Update(DownloadInput input, Time now, bool reduced = false) noexcept {
        if (input.status == DownloadStatus::Completed && input_.status != DownloadStatus::Completed)
            completion_.Trigger(now, reduced);
        else if (input.status != DownloadStatus::Completed) completion_.Clear();
        input_ = input;
    }
    void Clear() noexcept { input_ = {}; completion_.Clear(); }
    void ReduceMotion() noexcept { completion_.ReduceMotion(); }
    DownloadVisual Visual(Time now) const noexcept {
        DownloadVisual v; v.status = input_.status; v.completion = completion_.Visual(now);
        v.visible = input_.status == DownloadStatus::InProgress || input_.status == DownloadStatus::Paused ||
                    v.completion.phase != SuccessPhase::Idle;
        v.determinate = v.visible && input_.total && *input_.total > 0;
        if (v.determinate)
            v.progress = Clamp(static_cast<float>(static_cast<double>(input_.received) / *input_.total));
        return v;
    }
    Wake NextWake(Time now) const noexcept { return completion_.NextWake(now); }
};

// Selection affects surface emphasis only. Focus, active content and hit geometry
// must change synchronously. Use stable tab IDs, or Reset() before index reuse.
class TabSelection {
    std::map<int, Tween> values_;
    bool initialized_ = false;
public:
    void Reset() noexcept { values_.clear(); initialized_ = false; }
    void Set(int selected, Time now, bool reduced = false) {
        if (selected) values_.try_emplace(selected);
        for (auto it = values_.begin(); it != values_.end();) {
            auto& tween = it->second;
            const float value = it->first == selected ? 1.f : 0.f;
            if (!initialized_) tween.Snap(value);
            else tween.Set(value, now, Milliseconds(120), reduced, Curve::EaseOut);
            if (!tween.Moving(now) && tween.Value(now) == 0) it = values_.erase(it);
            else ++it;
        }
        initialized_ = true;
    }
    float Visual(int id, Time now) const noexcept {
        const auto it = values_.find(id);
        return it == values_.end() ? 0.f : it->second.Value(now);
    }
    void Settle() noexcept { for (auto& item : values_) item.second.Settle(); }
    Wake NextWake(Time now) const noexcept {
        for (const auto& item : values_) if (item.second.Moving(now)) return Frame();
        return {};
    }
};

// Optional convenience owner. IDs are opaque: tab/close/control namespaces are
// chosen by the renderer. No HWNDs, drawing, invalidation or timers live here.
class State {
    bool reduced_ = true;
    bool polished_ = false;
    std::map<int, Interaction> interactions_;
public:
    Success copy;
    Download download;
    TabSelection selection;
    explicit State(bool reduced = PreferredReducedMotion()) noexcept : reduced_(reduced) {}
    bool ReducedMotion() const noexcept { return reduced_; }
    // Call once before first interaction. No changes to existing defaults.
    void EnablePolish(bool enabled = true) noexcept {
        polished_ = enabled; copy.EnablePolish(enabled);
        for (auto& item : interactions_) item.second.EnablePolish(enabled);
    }
    void ClearInteractions() noexcept { interactions_.clear(); }
    void ForgetInteraction(int id) noexcept { interactions_.erase(id); }
    void SelectTab(int id, Time now) { selection.Set(id, now, reduced_); }
    void SetReducedMotion(bool reduced) noexcept {
        reduced_ = reduced;
        if (reduced) {
            for (auto& item : interactions_) item.second.Settle();
            selection.Settle();
            copy.ReduceMotion(); download.ReduceMotion();
        }
    }
    void CopySucceeded(Time now) noexcept { copy.Trigger(now, reduced_); }
    void DownloadChanged(DownloadInput input, Time now) noexcept { download.Update(input, now, reduced_); }
    void SetInteraction(int hovered, int pressed, Time now) {
        if (hovered) interactions_.try_emplace(hovered);
        if (pressed) interactions_.try_emplace(pressed);
        for (auto it = interactions_.begin(); it != interactions_.end();) {
            it->second.EnablePolish(polished_);
            it->second.Set(it->first == hovered, it->first == pressed, now, reduced_);
            const auto v = it->second.Visual(now);
            if (!it->second.NextWake(now).delay && v.hover == 0 && v.press == 0)
                it = interactions_.erase(it);
            else ++it;
        }
    }
    InteractionVisual Visual(int id, Time now) const noexcept {
        auto it = interactions_.find(id);
        return it == interactions_.end() ? InteractionVisual{} : it->second.Visual(now);
    }
    Wake NextWake(Time now) const noexcept {
        auto wake = Earlier(copy.NextWake(now), download.NextWake(now));
        wake = Earlier(wake, selection.NextWake(now));
        for (const auto& item : interactions_) wake = Earlier(wake, item.second.NextWake(now));
        return wake;
    }
};
}} // namespace NativeChrome::Motion
