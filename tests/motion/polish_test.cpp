#include "../../src/motion-presentation.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
using namespace NativeChrome::Motion;
namespace {
int checks = 0;
Time T(int ms) { return Time{}+Milliseconds(ms); }
bool Near(float a, float b) { return std::fabs(a-b) < .00001f; }
void Check(bool ok, const char* why) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}
void CurvesAndPress() {
    for (auto curve : {Curve::SmoothStep, Curve::EaseOut, Curve::Icon}) {
        Check(Eased(0, curve) == 0 && Eased(1, curve) == 1, "exact easing endpoints");
        float previous = 0;
        for (int n = 1; n <= 100; ++n) {
            auto v = Eased(n/100.f, curve);
            Check(v >= previous && v <= 1, "bounded monotonic easing"); previous = v;
        }
    }
    // Independently known cubic samples: x(.5)=.3425 for ease-out,
    // x(.5)=.2 for the icon curve, and y(.5)=.5 in both cases.
    Check(Near(Eased(.3425f, Curve::EaseOut), .5f), "CSS ease-out control points");
    Check(Near(Eased(.2f, Curve::Icon), .5f), "exact icon curve control points");
    Interaction i; i.EnablePolish(); i.Set(true, true, T(0));
    Check(i.Visual(T(0)).press == 0, "no initial press jump");
    Check(i.Visual(T(120)).hover == 1 && i.Visual(T(149)).press < 1, "hover and press durations");
    Check(i.Visual(T(150)).press == 1 && !i.NextWake(T(150)).delay, "press reaches idle endpoint");
    Check(Near(PressScale(1), .96f), "exact press scale");
    Check(PressScale(1, true) == 1 && PressScale(1, false, false) == 1 &&
          PressScale(1, false, true, true) == 1, "reduced disabled and static scales");
    Check(PressScale(-1) == 1 && Near(PressScale(2), .96f), "clamped press scale");
    for (int n = 151; n < 351; ++n) {
        const auto before = i.Visual(T(n));
        i.Set(n%2, n%3, T(n));
        Check(Near(i.Visual(T(n)).hover, before.hover) && Near(i.Visual(T(n)).press, before.press),
              "rapid interrupted press and hover remain continuous");
    }
    i.Set(false, false, T(351));
    Check(i.Visual(T(501)).press == 0 && i.Visual(T(501)).hover == 0 && !i.NextWake(T(501)).delay,
          "rapid reversal settles exactly");
}
void Copy() {
    Success s; s.EnablePolish(); s.Trigger(T(0));
    Check(s.Visual(T(0)).opacity == 0 && s.Visual(T(300)).opacity == 1, "polished reveal endpoints");
    Check(s.Visual(T(300)).phase == SuccessPhase::Dwell && s.NextWake(T(300)).delay->count() == 1500,
          "polished dwell sleeps once");
    Check(s.Visual(T(1800)).phase == SuccessPhase::Disappear && !s.NextWake(T(1950)).delay,
          "shorter exit and finite sequence");
    for (int repeat : {100, 300, 1800, 1875, 1949, 2000}) {
        s.Clear(); s.Trigger(T(0)); const auto before = s.Visual(T(repeat)).opacity;
        s.Trigger(T(repeat));
        Check(Near(s.Visual(T(repeat)).opacity, before), "repeat never blanks the check");
        Check(s.Visual(T(repeat+300)).opacity == 1, "repeat reaches visible check");
        Check(!s.NextWake(T(repeat+1950)).delay, "repeat eventually idles");
    }
    s.Clear(); s.Trigger(T(0)); s.Trigger(T(400));
    Check(s.Visual(T(400)).phase == SuccessPhase::Dwell && !s.NextWake(T(400)).frames &&
          s.NextWake(T(400)).delay->count() == 1500, "repeat during dwell renews only deadline");
    s.Clear(); s.Trigger(T(0), true);
    Check(s.Visual(T(0)).opacity == 0 && s.Visual(T(75)).opacity > 0 &&
          s.Visual(T(150)).opacity == 1 && s.Visual(T(75)).scale == 1, "reduced mode keeps short opacity reveal");
    Check(s.Visual(T(1650)).opacity == 1 && s.Visual(T(1725)).opacity < 1 &&
          !s.NextWake(T(1800)).delay, "reduced mode fades out finitely");
    s.Clear(); s.Trigger(T(0)); const auto before = s.Visual(T(100)).opacity;
    s.ReduceMotion();
    Check(Near(s.Visual(T(100)).opacity, before) && s.Visual(T(100)).scale == 1,
          "runtime reduction removes transform without opacity jump");
    Check(!s.NextWake(T(1950)).delay, "runtime reduction preserves deadline");
    for (int n = 0; n <= 1950; n += 13) {
        const auto layers = CopyPresentation(s.Visual(T(n)), true);
        Check(Near(layers.copy.opacity+layers.check.opacity, 1) &&
              layers.copy.scale == 1 && layers.check.scale == 1, "reduced layers conserve opacity and disable scale");
    }
    auto layers = CopyPresentation({SuccessPhase::Dwell, 1, 1});
    Check(layers.copy.opacity == 0 && layers.check.opacity == 1 && layers.check.scale == 1,
          "check layer endpoint");
    s.Clear(); Check(!s.NextWake(T(0)).delay, "clear stops all copy work");
}
void SelectionAndScheduling() {
    State s(false); s.EnablePolish(); s.SelectTab(1, T(0));
    Check(s.selection.Visual(1, T(0)) == 1 && !s.NextWake(T(0)).delay, "initial selected tab is immediate");
    s.SelectTab(2, T(0));
    Check(s.selection.Visual(1, T(60)) > 0 && s.selection.Visual(2, T(60)) > 0, "selection surfaces crossfade");
    const auto before = s.selection.Visual(1, T(60)); s.SelectTab(1, T(60));
    Check(Near(s.selection.Visual(1, T(60)), before), "selection reversal continuous");
    Check(s.selection.Visual(1, T(180)) == 1 && s.selection.Visual(2, T(180)) == 0 &&
          !s.NextWake(T(180)).delay, "selection reversals finish");
    s.selection.Reset(); s.ClearInteractions(); s.SelectTab(2, T(200));
    Check(s.selection.Visual(1, T(200)) == 0 && s.selection.Visual(2, T(200)) == 1,
          "tab model mutation does not transfer stale state to reused IDs");
    s.SetInteraction(10, 10, T(200)); s.ForgetInteraction(10);
    Check(!s.NextWake(T(200)).delay, "removed control does not retain a timer");
    s.SelectTab(3, T(201)); s.SetReducedMotion(true);
    Check(s.selection.Visual(3, T(201)) == 1 && !s.NextWake(T(201)).delay, "preference change settles selection");
    s.SetReducedMotion(false); s.CopySucceeded(T(1000));
    auto now = T(1000); int frames = 0, deadlines = 0;
    for (int n = 0; n < 100; ++n) {
        const auto wake = s.NextWake(now); if (!wake.delay) break;
        if (wake.frames) ++frames; else ++deadlines;
        now += *wake.delay;
    }
    Check(frames == 29 && deadlines == 1 && !s.NextWake(now).delay,
          "polished scheduler uses finite frame work and one dwell deadline");
    Check(s.copy.Visual(now).opacity == 0 && !s.NextWake(T(1000000)).delay, "final repaint returns to idle");
    State dormant(false); dormant.EnablePolish();
    Check(!dormant.download.Visual(T(100000)).visible && !dormant.NextWake(T(100000)).delay,
          "polish never activates download model");
}
}
int main() {
    CurvesAndPress(); Copy(); SelectionAndScheduling();
    std::printf("PASS: %d deterministic polish checks; native only, no WebView2.\n", checks);
}
