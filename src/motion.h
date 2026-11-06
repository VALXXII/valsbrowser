#pragma once

// Native-only, header-only C++17 motion model. All calls belong to the UI thread.
// Pass the same steady-clock timestamp to model updates, sampling and scheduling.
#include <chrono>
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

inline float Clamp(float value) noexcept { return value < 0 ? 0 : (value > 1 ? 1 : value); }
inline float Ease(float value) noexcept { const float t = Clamp(value); return t*t*(3-2*t); }
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
public:
    float Value(Time now) const noexcept {
        if (now >= end_) return target_;
        return from_ + (target_-from_)*Ease(Fraction(now, begin_, end_));
    }
    bool Moving(Time now) const noexcept { return from_ != target_ && now < end_; }
    void Set(float value, Time now, Milliseconds duration, bool reduced = false) noexcept {
        value = Clamp(value);
        if (reduced || duration.count() <= 0) { Snap(value); return; }
        if (value == target_) return; // Repeated input/paint must not restart the clock.
        from_ = Value(now); target_ = value; begin_ = now; end_ = now+duration;
    }
    void Snap(float value) noexcept { from_ = target_ = Clamp(value); begin_ = end_ = Time{}; }
    void Settle() noexcept { Snap(target_); }
};

struct InteractionVisual { float hover = 0, press = 0; };
class Interaction {
    Tween hover_, press_;
public:
    void Set(bool hover, bool press, Time now, bool reduced = false) noexcept {
        hover_.Set(hover ? 1.f : 0.f, now, Milliseconds(120), reduced);
        press_.Set(press ? 1.f : 0.f, now, Milliseconds(80), reduced);
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
    Time begin_{}, revealEnd_{}, dwellEnd_{}, end_{};
public:
    void Trigger(Time now, bool reduced = false) noexcept {
        triggered_ = true; reduced_ = reduced; begin_ = now;
        revealEnd_ = now+Milliseconds(reduced ? 0 : 150);
        dwellEnd_ = revealEnd_+Milliseconds(1500);
        end_ = dwellEnd_+Milliseconds(reduced ? 0 : 150);
    }
    void Clear() noexcept { triggered_ = false; }
    void ReduceMotion() noexcept {
        // Preserve the original dwell deadline; do not restart or resurrect it.
        reduced_ = true; revealEnd_ = begin_; end_ = dwellEnd_;
    }
    SuccessVisual Visual(Time now) const noexcept {
        if (!triggered_ || now >= end_) return {};
        if (now < revealEnd_) {
            const auto a = Ease(Fraction(now, begin_, revealEnd_));
            return {SuccessPhase::Reveal, a, .88f+.12f*a};
        }
        if (now < dwellEnd_) return {SuccessPhase::Dwell, 1, 1};
        const auto a = 1-Ease(Fraction(now, dwellEnd_, end_));
        return {SuccessPhase::Disappear, a, reduced_ ? 1.f : .88f+.12f*a};
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

// Optional convenience owner. IDs are opaque: tab/close/control namespaces are
// chosen by the renderer. No HWNDs, drawing, invalidation or timers live here.
class State {
    bool reduced_ = true;
    std::map<int, Interaction> interactions_;
public:
    Success copy;
    Download download;
    explicit State(bool reduced = SystemReducedMotion()) noexcept : reduced_(reduced) {}
    bool ReducedMotion() const noexcept { return reduced_; }
    void SetReducedMotion(bool reduced) noexcept {
        reduced_ = reduced;
        if (reduced) {
            for (auto& item : interactions_) item.second.Settle();
            copy.ReduceMotion(); download.ReduceMotion();
        }
    }
    void CopySucceeded(Time now) noexcept { copy.Trigger(now, reduced_); }
    void DownloadChanged(DownloadInput input, Time now) noexcept { download.Update(input, now, reduced_); }
    void SetInteraction(int hovered, int pressed, Time now) {
        if (hovered) interactions_.try_emplace(hovered);
        if (pressed) interactions_.try_emplace(pressed);
        for (auto it = interactions_.begin(); it != interactions_.end();) {
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
        for (const auto& item : interactions_) wake = Earlier(wake, item.second.NextWake(now));
        return wake;
    }
};
}} // namespace NativeChrome::Motion
