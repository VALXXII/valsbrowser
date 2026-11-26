#pragma once
// Animated downloads toolbar icon (design/lottie/download-progress.py).
//
// Four Lottie layers, rendered by the native ThorVG player and composed here:
//   track (grey ink) + fill (#3E67FF) - arrow cut-out (destination-out) + arrow (grey ink)
// The fill follows the download (frame = progress) while the arrow moves on its
// own clock, so the two are separate timelines:
//   ring  0..14 start (tray curls into a ring) · 14..54 progress · 54..81 done
//   arrow 0..14 start (drops in) · 14..92 bob loop · 92..119 done (rises back)
// "Done" starts at a loop boundary (arrow at rest) so nothing jumps, and a blue
// ripple (stroke that grows, blurs and fades) marks a completed download.
//
// Success (user request 2026-11-14): a batch that ends with a completed download does not leave
// at once. From the loop boundary the arrow MORPHS into the Phosphor check-circle check
// (download-success.json + its cut-out, 25 frames, the ring held full at FillEnd), the check holds
// 3 s, morphs back into the arrow, then the usual "done" plays (ring unrolls, arrow rises). A new
// download interrupts: the check morphs back from where it is, then progress resumes. A cancelled
// or failed batch never shows it. Reduced motion: the final check, static, for 3 s.
// Grey parts use the same ink as every other toolbar icon (white at 50%), so the
// animated icon matches the static one on any button surface.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include "motion.h"

namespace NativeChrome { namespace DownloadIcon {
// Frame layout, mirrored from the generator (frame counts are checked on load).
constexpr int Morph = 14, Steps = 40, Out = 27, Loop = 78;
constexpr int FillEnd = Morph + Steps, RingDone = FillEnd + Out, RingFrames = RingDone + 1;    // 82
constexpr int LoopEnd = Morph + Loop, ArrowDone = LoopEnd + Out, ArrowFrames = ArrowDone + 1;  // 120
constexpr double Fps = 60;
constexpr int SuccessMorph = 24, SuccessFrames = SuccessMorph + 1;                            // 400 ms, op 25
constexpr Motion::Milliseconds SuccessHold{3000};
constexpr float Ink = .5f; // toolbar icon ink: white at 50%
// Ripple, in the Lottie's 256-unit space: ring radius 88 grows to 4x, 20-unit
// stroke, blurs to 10 units (scaled with the ring, like the preview) and fades
// over 600 ms on cubic-bezier(.2, 0, 0, 1).
namespace Ripple {
constexpr float radius = 88, grow = 4, stroke = 20, blur = 10, opacity = .9f;
// Furthest the ripple reaches from the icon centre, in 256 units: the grown
// ring, half its scaled stroke and 3 sigma of its blur, which grows with it.
constexpr float reach = radius * grow + stroke * grow / 2 + 3 * blur * grow;
constexpr Motion::Milliseconds duration{600};
}
constexpr float FillColor[3] = {0x3E / 255.f, 0x67 / 255.f, 0xFF / 255.f};

struct Pose {
    int ring = 0, arrow = 0;    // Lottie frames
    int success = -1;           // >= 0: the arrow layer is the success morph at this frame (0 arrow .. 24 check)
    float ripple = -1;          // 0..1 while the ripple plays, else -1
    bool idle = true;           // static icon (no layer needed)
    // Same pose, same pixels: the host repaints the button only when this changes.
    bool operator==(const Pose& o) const { return ring == o.ring && arrow == o.arrow && success == o.success && ripple == o.ripple && idle == o.idle; }
    bool operator!=(const Pose& o) const { return !(*this == o); }
};

// Pure state model: feed it the downloads' state, sample a pose at any time.
// Settle() applies time-driven transitions; At() and NextWake() only read.
class Model {
    bool running_ = false, finishing_ = false, completed_ = false, rippling_ = false, queued_ = false, reduced_ = false;
    Motion::Time start_{}, finish_{}, ripple_{}, restart_{};
    // Success interrupted by a new download: the morph runs back from `interruptFrame_` at `interrupt_`.
    bool interrupted_ = false; Motion::Time interrupt_{}; int interruptFrame_ = 0;
    float fraction_ = 0;
    // Motion lab tunables: playback speed (x Fps) captured per run, ripple length per ripple.
    double speed_ = 1, tunedSpeed_ = 1;
    Motion::Clock::duration rippleLength_ = Ripple::duration, tunedRipple_ = Ripple::duration;
    double Frames(Motion::Time from, Motion::Time to) const { return std::chrono::duration<double>(to - from).count() * Fps * speed_; }
    Motion::Time After(Motion::Time t, double frames) const { return t + std::chrono::duration_cast<Motion::Clock::duration>(std::chrono::duration<double>(frames / (Fps * speed_))); }
    Motion::Time LoopOrigin() const { return reduced_ ? start_ : After(start_, Morph); }
    // Finishing waits for the arrow to be back at rest (a loop boundary).
    Motion::Time DoneStart() const {
        if (reduced_) return finish_;
        const auto origin = LoopOrigin();
        if (finish_ <= origin) return origin;
        return After(origin, std::ceil(Frames(origin, finish_) / Loop) * Loop);
    }
    // Success timeline (completed batch): morph in, hold, morph out, then the usual done.
    Motion::Time HoldStart() const { return reduced_ ? finish_ : After(DoneStart(), SuccessMorph); }
    Motion::Time HoldEnd() const { return HoldStart() + std::chrono::duration_cast<Motion::Clock::duration>(SuccessHold); }
    Motion::Time BackEnd() const { return reduced_ ? HoldEnd() : After(HoldEnd(), SuccessMorph); }
    Motion::Time DoneEnd() const { if (reduced_) return completed_ ? HoldEnd() : finish_; return completed_ ? After(BackEnd(), Out) : After(DoneStart(), Morph); }
    Motion::Time InterruptEnd() const { return reduced_ ? interrupt_ : After(interrupt_, interruptFrame_); }
    // Success frame shown at `now` (-1: none) while the success part of the timeline runs.
    int SuccessFrame(Motion::Time now) const {
        if (!finishing_ || !completed_) return -1;
        if (interrupted_) { if (now >= InterruptEnd()) return -1; return reduced_ ? -1 : std::max(0, interruptFrame_ - static_cast<int>(Frames(interrupt_, now))); }
        if (now < DoneStart() || now >= BackEnd()) return -1;
        if (reduced_) return SuccessMorph;
        if (now < HoldStart()) return std::min(SuccessMorph, static_cast<int>(Frames(DoneStart(), now)));
        if (now < HoldEnd()) return SuccessMorph;
        return std::max(0, SuccessMorph - static_cast<int>(Frames(HoldEnd(), now)));
    }
    bool Rippling(Motion::Time now) const { return rippling_ && now < ripple_ + rippleLength_; }
    void Begin(Motion::Time at, float fraction) { speed_ = tunedSpeed_; running_ = true; start_ = at; finishing_ = completed_ = queued_ = interrupted_ = false; fraction_ = Motion::Clamp(fraction); }
    // Back to progress straight from the arrow at rest (no start morph: the ring is already a ring).
    void Resume(Motion::Time at, float fraction) { Begin(reduced_ ? at : After(at, -Morph), fraction); }
public:
    // speed: 0.25..4 x the authored 60 fps; ripple: its fade length. Applied at the next run / ripple.
    void Tune(double speed, Motion::Milliseconds ripple) { tunedSpeed_ = std::clamp(speed, .25, 4.); tunedRipple_ = std::max(ripple, Motion::Milliseconds(50)); }
    void Settle(Motion::Time now) {
        if (!Rippling(now)) rippling_ = false;
        if (running_ && finishing_ && interrupted_ && now >= InterruptEnd()) { Resume(InterruptEnd(), fraction_); return; }
        if (running_ && finishing_ && now >= DoneEnd()) {
            if (queued_) Begin(restart_, 0);    // a download that started while the icon was leaving
            else running_ = false;
        }
    }
    // active: a download is in progress or paused; fraction: their combined
    // progress (0 = no bytes yet / unknown size); completed: one finished just now.
    void Update(bool active, float fraction, bool completed, Motion::Time now, bool reduced) {
        reduced_ = reduced; Settle(now);
        if (!running_) { if (active) Begin(now, fraction); return; }
        if (completed) completed_ = true;
        if (active) {
            if (finishing_ && now < DoneStart()) finishing_ = false;            // resumed before leaving
            else if (finishing_ && completed_ && !interrupted_ && now < BackEnd()) {
                // A new download during the success: the check morphs back from where it is, then progress.
                interruptFrame_ = std::max(0, SuccessFrame(now)); interrupt_ = now; interrupted_ = true; fraction_ = Motion::Clamp(fraction);
                if (reduced_) Resume(now, fraction_);
                return;
            }
            else if (finishing_ && interrupted_) { fraction_ = Motion::Clamp(fraction); return; }
            else if (finishing_) { queued_ = true; restart_ = DoneEnd(); return; }
            fraction_ = Motion::Clamp(fraction);
            return;
        }
        if (!finishing_) {
            finishing_ = true; finish_ = now;
            if (completed_) { fraction_ = 1; if (!reduced) { rippling_ = true; ripple_ = now; rippleLength_ = tunedRipple_; } }
        }
    }
    Pose At(Motion::Time now) const {
        Pose pose;
        if (Rippling(now)) pose.ripple = static_cast<float>(std::chrono::duration<double>(now - ripple_).count() / std::chrono::duration<double>(rippleLength_).count());
        if (!running_) return pose;
        if (finishing_ && now >= DoneStart()) {
            if (interrupted_) { pose.idle = false; pose.ring = FillEnd; pose.arrow = Morph; pose.success = SuccessFrame(now); if (pose.success < 0) pose.success = 0; return pose; }
            if (now >= DoneEnd()) return pose;
            pose.idle = false;
            if (completed_) {
                const int sf = SuccessFrame(now);
                if (sf >= 0 || now < BackEnd()) { pose.ring = FillEnd; pose.arrow = Morph; pose.success = std::max(0, sf); return pose; }
                const int g = static_cast<int>(Frames(BackEnd(), now));
                pose.ring = std::min(FillEnd + g, RingDone); pose.arrow = std::min(LoopEnd + g, ArrowDone); return pose;
            }
            const int g = static_cast<int>(Frames(DoneStart(), now));
            pose.ring = pose.arrow = std::max(Morph - g, 0);                   // cancelled: the start, reversed
            return pose;
        }
        pose.idle = false;
        const double f = Frames(start_, now);
        if (!reduced_ && f < Morph) { pose.ring = pose.arrow = std::max(0, static_cast<int>(f)); return pose; }
        pose.ring = Morph + static_cast<int>(std::lround(fraction_ * Steps));
        pose.arrow = reduced_ ? Morph : Morph + static_cast<int>(std::fmod(Frames(LoopOrigin(), now), Loop));
        return pose;
    }
    bool Active() const { return running_; }
    // Read-only timeline points for a host that follows the icon (the update card, src/update-toast-motion.h):
    // when the arrow is back at rest and the end starts, and when a completed batch's check is fully drawn.
    bool Ending() const { return running_ && finishing_; }
    bool Succeeded() const { return completed_; }
    Motion::Time EndStart() const { return DoneStart(); }
    Motion::Time CheckDrawn() const { return HoldStart(); }
    Motion::Wake NextWake(Motion::Time now) const {
        if (Rippling(now)) return Motion::Frame();
        if (running_ && reduced_ && finishing_ && completed_) {             // reduced: the static check, then idle
            const auto end = interrupted_ ? InterruptEnd() : DoneEnd();
            return now < end ? Motion::Deadline(now, end) : Motion::Frame();
        }
        if (running_ && finishing_ && completed_ && !interrupted_ && now >= HoldStart() && now < HoldEnd()) return Motion::Deadline(now, HoldEnd());   // holding: one wake at its end
        if (!running_ || (reduced_ && !finishing_)) return {};              // reduced: static ring, repainted on progress
        if (finishing_ && now >= DoneEnd()) return queued_ ? Motion::Frame() : Motion::Wake{};
        return Motion::Frame();
    }
};

// Premultiplied BGRA composition of one frame: inks the grey layers like the
// toolbar icons, puts the fill over the track, cuts the arrow's band out of
// both, then draws the arrow. All inputs are side*side ARGB (premultiplied).
// `tint` (RGB 0..1, optional): the window's accent ink (Paper p-I-0 P07, the profile colour). The
// fill asset is a single flat #3E67FF, so it is re-coloured from its own coverage: shape, timing and
// antialiasing stay the asset's; the grey track, cut-out and arrow are never tinted.
// `arrowMix` (0..1, with a tint): the arrow layer leaves the grey ink for the tint, opaque — the success
// check is drawn in the profile ink (user request), blended in over the first frames of its morph.
inline void Compose(const uint32_t* track, const uint32_t* fill, const uint32_t* cut, const uint32_t* arrow, size_t count, std::vector<uint32_t>& out, const float* tint = nullptr, float arrowMix = 0) {
    out.resize(count); arrowMix = tint ? std::clamp(arrowMix, 0.f, 1.f) : 0.f;
    for (size_t i = 0; i < count; ++i) {
        const float cover = (arrow[i] >> 24) / 255.f;
        const float ta = (track[i] >> 24) / 255.f * Ink, aa = cover * (Ink + (1 - Ink) * arrowMix), keep = 1 - (cut[i] >> 24) / 255.f;
        const float fa = (fill[i] >> 24) / 255.f;
        float c[3], a;
        for (int ch = 0; ch < 3; ++ch) c[ch] = (tint ? tint[2 - ch] * fa : ((fill[i] >> (8 * ch)) & 255) / 255.f) + ta * (1 - fa);  // fill over white-ink track (BGRA: ch 0 = blue)
        a = fa + ta * (1 - fa);
        for (int ch = 0; ch < 3; ++ch) {                                                                 // cut, then arrow over
            const float ink = cover * Ink * (1 - arrowMix) + (arrowMix > 0 ? tint[2 - ch] * cover * arrowMix : 0.f);   // premultiplied grey -> tint
            c[ch] = ink + c[ch] * keep * (1 - aa);
        }
        a = aa + a * keep * (1 - aa);
        uint32_t px = static_cast<uint32_t>(std::lround(std::clamp(a, 0.f, 1.f) * 255)) << 24;
        for (int ch = 0; ch < 3; ++ch) px |= static_cast<uint32_t>(std::lround(std::clamp(c[ch], 0.f, 1.f) * 255)) << (8 * ch);
        out[i] = px;
    }
}
}} // namespace NativeChrome::DownloadIcon
