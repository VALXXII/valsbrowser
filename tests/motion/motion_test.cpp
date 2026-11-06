#include "../../src/motion.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>

using namespace NativeChrome::Motion;
namespace {
int checks = 0;
void Check(bool condition, const char* message) {
    ++checks;
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
bool Near(float a, float b) { return std::fabs(a-b) < .00001f; }
Time T(int ms) { return Time{}+Milliseconds(ms); }
void CopyTests() {
    Success s;
    Check(s.Visual(T(0)).phase == SuccessPhase::Idle, "copy initially idle");
    Check(!s.NextWake(T(0)).delay, "idle copy needs no timer");
    s.Trigger(T(0));
    Check(s.Visual(T(0)).opacity == 0, "reveal starts at zero");
    Check(Near(s.Visual(T(75)).opacity, .5f), "reveal eased midpoint");
    Check(s.NextWake(T(75)).frames, "reveal requests frames");
    Check(s.Visual(T(150)).phase == SuccessPhase::Dwell, "reveal ends at 150ms");
    Check(s.Visual(T(150)).opacity == 1, "reveal exact endpoint");
    const auto dwell = s.NextWake(T(150));
    Check(dwell.delay && dwell.delay->count() == 1500 && !dwell.frames, "dwell sleeps for 1500ms");
    Check(s.Visual(T(1649)).opacity == 1, "full 1500ms dwell");
    Check(s.Visual(T(1650)).phase == SuccessPhase::Disappear, "disappear starts at 1650ms");
    Check(Near(s.Visual(T(1725)).opacity, .5f), "disappear eased midpoint");
    Check(s.Visual(T(1800)).opacity == 0, "copy exact final endpoint");
    Check(!s.NextWake(T(1800)).delay && !s.NextWake(T(999999)).delay, "copy never ticks forever");
    s.Trigger(T(2000)); s.Trigger(T(2100));
    Check(s.Visual(T(2100)).opacity == 0 && s.Visual(T(3900)).phase == SuccessPhase::Idle,
          "a second successful copy starts a fresh finite sequence");
    s.Trigger(T(4000), true);
    Check(s.Visual(T(4000)).opacity == 1 && s.Visual(T(4000)).scale == 1, "reduced copy appears immediately");
    Check(!s.NextWake(T(4000)).frames && s.NextWake(T(4000)).delay->count() == 1500,
          "reduced copy schedules only expiry");
    Check(s.Visual(T(5500)).opacity == 0 && !s.NextWake(T(5500)).delay, "reduced copy expires immediately");
    s.Trigger(T(6000)); s.ReduceMotion();
    Check(s.Visual(T(6010)).opacity == 1 && !s.NextWake(T(6010)).frames, "runtime reduced preference stops reveal");
    Check(!s.NextWake(T(7650)).delay, "runtime preference preserves finite expiry");
    s.Trigger(T(8000)); s.ReduceMotion(); s.Clear();
    Check(!s.NextWake(T(8010)).delay, "clear cancels pending sequence");
}
void InteractionTests() {
    Interaction i;
    i.Set(true, true, T(0));
    Check(i.Visual(T(0)).hover == 0 && i.Visual(T(0)).press == 0, "interaction initial endpoints");
    Check(Near(i.Visual(T(40)).press, .5f), "press midpoint 40ms");
    Check(Near(i.Visual(T(20)).press, .15625f), "quarter press uses eased rather than linear interpolation");
    Check(Near(i.Visual(T(60)).hover, .5f), "hover midpoint 60ms");
    i.Set(true, true, T(60));
    Check(i.Visual(T(80)).press == 1 && i.Visual(T(120)).hover == 1, "same target does not restart tween");
    Check(!i.NextWake(T(120)).delay, "hovered steady state has no timer");
    i.Set(false, false, T(120));
    const auto before = i.Visual(T(150));
    i.Set(true, false, T(150));
    Check(Near(i.Visual(T(150)).hover, before.hover), "midflight reversal is continuous");
    Check(i.Visual(T(270)).hover == 1 && i.Visual(T(270)).press == 0, "reversal reaches exact endpoints");
    i.Set(false, true, T(300), true);
    Check(i.Visual(T(300)).hover == 0 && i.Visual(T(300)).press == 1 && !i.NextWake(T(300)).delay,
          "reduced interaction snaps without ticks");
}
void DownloadTests() {
    Download d;
    Check(!d.Visual(T(0)).visible && !d.NextWake(T(0)).delay, "unhooked download stays invisible and idle");
    d.Update({DownloadStatus::InProgress, 25, 100}, T(0));
    Check(d.Visual(T(0)).visible && d.Visual(T(0)).determinate && Near(d.Visual(T(0)).progress, .25f),
          "real byte progress is represented");
    Check(Near(d.Visual(T(100000)).progress, .25f) && !d.NextWake(T(100000)).delay,
          "time alone never advances download or schedules animation");
    d.Update({DownloadStatus::InProgress, 100, 100}, T(100001));
    Check(d.Visual(T(100001)).completion.phase == SuccessPhase::Idle, "bytes at total do not fake completion");
    d.Update({DownloadStatus::InProgress, 150, 100}, T(100002));
    Check(d.Visual(T(100002)).progress == 1, "overreported byte count clamps");
    d.Update({DownloadStatus::Paused, 25, 100}, T(100003));
    Check(d.Visual(T(100003)).visible && !d.NextWake(T(100003)).delay, "paused is static");
    d.Update({DownloadStatus::InProgress, 25, std::nullopt}, T(100004));
    Check(!d.Visual(T(100004)).determinate && d.Visual(T(100004)).progress == 0,
          "unknown total has no fictional fill");
    d.Update({DownloadStatus::InProgress, 25, 0}, T(100005));
    Check(!d.Visual(T(100005)).determinate, "zero total remains unknown");
    d.Update({DownloadStatus::Interrupted, 25, 100}, T(100006));
    Check(!d.Visual(T(100006)).visible && !d.NextWake(T(100006)).delay, "interruption has no completion animation");
    d.Update({DownloadStatus::Completed, 100, 100}, T(110000));
    Check(d.Visual(T(110075)).visible && Near(d.Visual(T(110075)).completion.opacity, .5f), "real completed event triggers reveal");
    d.Update({DownloadStatus::Completed, 100, 100}, T(110100));
    Check(!d.Visual(T(111800)).visible && !d.NextWake(T(111800)).delay, "duplicate completed event cannot restart completion");
    d.Clear();
    d.Update({DownloadStatus::Completed, 100, 100}, T(120000), true);
    Check(d.Visual(T(120000)).completion.opacity == 1 && !d.NextWake(T(120000)).frames,
          "reduced download completion is static");
    d.Update({DownloadStatus::Cancelled, 100, 100}, T(120100));
    Check(!d.Visual(T(120100)).visible && !d.NextWake(T(120100)).delay, "cancel clears pending completion");
}
void StateTests() {
    State state(false);
    Check(!state.NextWake(T(0)).delay, "state starts timer-free");
    state.CopySucceeded(T(0));
    state.SetInteraction(2000, 0, T(200));
    Check(state.NextWake(T(200)).frames, "hover frame outranks copy dwell deadline");
    Check(!state.NextWake(T(320)).frames && state.NextWake(T(320)).delay->count() == 1330,
          "settled hover returns to one dwell deadline");
    state.SetInteraction(0, 0, T(400));
    Check(state.Visual(2000, T(520)).hover == 0, "close ID is treated opaquely");
    state.SetInteraction(1000, 1000, T(600));
    state.SetReducedMotion(true);
    Check(state.Visual(1000, T(600)).hover == 1 && state.Visual(1000, T(600)).press == 1,
          "runtime reduced preference settles hover and press");
    state.SetReducedMotion(false);
    Check(!state.NextWake(T(100000)).delay, "turning motion back on never resurrects expired motion");
    state.SetInteraction(0, 0, T(100001));
    Check(!state.NextWake(T(100121)).delay, "release and leave finish with no timer");
    Check(Deadline(T(0), T(0)+std::chrono::microseconds(1)).delay->count() == 1,
          "submillisecond deadline rounds up for Win32 timers");
    State finite(false);
    finite.CopySucceeded(T(0));
    auto now = T(0);
    int frameWakes = 0, deadlineWakes = 0;
    for (int n = 0; n < 100; ++n) {
        const auto wake = finite.NextWake(now);
        if (!wake.delay) break;
        if (wake.frames) ++frameWakes; else ++deadlineWakes;
        now += *wake.delay;
    }
    Check(!finite.NextWake(now).delay && frameWakes == 20 && deadlineWakes == 1,
          "full scheduler simulation ends with 20 frame wakes and only one dwell wake");
    Check(finite.copy.Visual(now).opacity == 0, "last scheduled paint clears success");
    State delayed(false);
    delayed.CopySucceeded(T(0));
    Check(!delayed.NextWake(T(10000)).delay && delayed.copy.Visual(T(10000)).opacity == 0,
          "late or suspended timer goes directly to idle");
    State reduced(true);
    reduced.CopySucceeded(T(0));
    const auto expiry = reduced.NextWake(T(0));
    Check(expiry.delay->count() == 1500 && !expiry.frames && !reduced.NextWake(T(1500)).delay,
          "reduced scheduler has exactly one deadline wake");
}
}
int main() {
    CopyTests(); InteractionTests(); DownloadTests(); StateTests();
    std::printf("PASS: %d deterministic native motion checks; no browser/WebView2 used.\n", checks);
}
