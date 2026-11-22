// Onboarding debug tuning (src/onboarding-tuning.h): the parameters of the board 2 -> 3 transition, their
// text round trip (the file the debug panel keeps and the text it copies), bounds, and the timeline they drive.
#include <cmath>
#include <cstdio>
#include "../../src/onboarding-tuning.h"
#include "../../src/onboarding-flow.h"

static int failures = 0, checks = 0;
static void Expect(bool ok, const char* what) { ++checks; std::printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++failures; }
namespace T = Onboarding::Tuning;
static bool Near(float a, float b, float e = 1e-3f) { return std::fabs(a - b) <= e; }

int main() {
    const T::Values d = T::Defaults();
    Expect(Near(d[T::Duration], 220) && Near(d[T::OutShare], .4f) && Near(d[T::MockDelay], 0) && Near(d[T::MockDuration], 220) && Near(d[T::ContentRise], 0) && Near(d[T::MockDim], 0), "defaults are the shipped transition (220 ms, old content out over the first 40 %, the mock travels the whole time)");
    Expect(T::Write(d) == T::Write(T::Read(T::Write(d))) && T::Read(T::Write(d)) == d, "defaults survive the round trip");
    T::Values v = d; v[T::Duration] = 480; v[T::OutShare] = .25f; v[T::MockDelay] = 60; v[T::MockDuration] = 700; v[T::CurveX1] = .32f; v[T::CurveY1] = .72f; v[T::CurveX2] = 0; v[T::CurveY2] = 1; v[T::ContentRise] = 12; v[T::MockDim] = .3f;
    const std::string text = T::Write(v);
    Expect(T::Read(text) == v, "every parameter survives the round trip");
    Expect(text.rfind("browser-onboarding-tuning 1\n", 0) == 0 && text.find("transition23.duration=480") != std::string::npos, "the file is readable text: a header, then one key=value per line");
    Expect(T::Read("") == d && T::Read("something else\ntransition23.duration=900\n") == d, "an empty or foreign file gives the defaults");
    const T::Values partial = T::Read("browser-onboarding-tuning 1\ntransition23.duration=300\nunknown.key=5\ntransition23.outShare=abc\n");
    Expect(Near(partial[T::Duration], 300) && Near(partial[T::OutShare], .4f) && Near(partial[T::MockDuration], 220), "missing, unknown and unreadable lines keep their defaults");
    const T::Values wild = T::Read("browser-onboarding-tuning 1\ntransition23.duration=99999\ntransition23.outShare=-3\ntransition23.curveY1=nan\n");
    Expect(Near(wild[T::Duration], T::Params()[T::Duration].max) && Near(wild[T::OutShare], T::Params()[T::OutShare].min) && Near(wild[T::CurveY1], d[T::CurveY1]), "values are bounded; not-a-number is refused");
    for (int i = 0; i < T::Count; ++i) { const auto& p = T::Params()[i]; if (!(p.min < p.max && p.def >= p.min && p.def <= p.max && p.step > 0 && p.label && *p.label)) { Expect(false, "every parameter has a French label, bounds around its default and a step"); break; } if (i == T::Count - 1) Expect(true, "every parameter has a French label, bounds around its default and a step"); }
    Expect(Near(T::Step(T::Duration, 233.4f), 235) && Near(T::Step(T::OutShare, .437f), .44f), "slider values snap to the parameter's step");
    // ---- the curve and the timeline ----
    Expect(Near(T::Bezier(0, 0, 1, 1, .3f), .3f, .01f) && T::Bezier(.23f, 1, .32f, 1, 0) == 0 && Near(T::Bezier(.23f, 1, .32f, 1, 1), 1), "cubic bezier: linear when the handles sit on the diagonal, fixed ends");
    Expect(T::Bezier(.23f, 1, .32f, 1, .3f) > .6f && T::Bezier(.23f, 1, .32f, 1, .3f) < T::Bezier(.23f, 1, .32f, 1, .6f), "ease-out: fast first, always forward");
    Expect(Near(T::TotalMs(d), 220) && Near(T::TotalMs(v), 760), "the transition lasts until its last part ends (content, or delay + travel of the mock)");
    Expect(T::At(d, -50).mock == 0 && T::At(d, 0).oldAlpha == 1 && T::At(d, 0).newAlpha == 0 && !T::At(d, 0).swapped, "before the start: the old board, untouched");
    const T::Frame mid = T::At(d, 44);
    Expect(!mid.swapped && Near(mid.oldAlpha, .5f, .01f) && mid.newAlpha == 0 && mid.mock > 0 && mid.mock < 1, "first share: the old content fades out while the mock already travels");
    const T::Frame later = T::At(d, 154);
    Expect(later.swapped && later.oldAlpha == 0 && Near(later.newAlpha, .5f, .01f), "after the share: the new content fades in");
    const T::Frame end = T::At(d, 220);
    Expect(end.swapped && end.newAlpha == 1 && Near(end.mock, 1) && end.rise == 0 && end.dim == 0 && T::Done(d, 220) && !T::Done(d, 219), "at the end: the new board, settled");
    Expect(T::At(v, 30).mock == 0 && T::At(v, 400).mock > 0 && T::At(v, 400).mock < 1 && Near(T::At(v, 760).mock, 1) && !T::Done(v, 500) && T::Done(v, 760), "a delayed, longer travel of the mock goes on after the content is in");
    Expect(T::At(v, 130).rise > 0 && T::At(v, 130).rise <= 12 && T::At(v, 480).rise == 0, "the new content rises into place by the chosen distance");
    Expect(T::At(v, 410).dim > 0 && T::At(v, 410).dim <= .3f && T::At(v, 760).dim == 0, "the mock dims mid-way by the chosen amount and comes back");
    // ---- the switch ----
    Expect(Onboarding::HasSwitch(L"--onboarding --onboarding-debug", Onboarding::DebugSwitch) && Onboarding::HasSwitch(L"--onboarding --onboarding-debug") && !Onboarding::HasSwitch(L"--onboarding", Onboarding::DebugSwitch), "--onboarding-debug is its own switch");
    Expect(!Onboarding::HasSwitch(L"--onboarding-debug") && Onboarding::Asked(L"--onboarding-debug") && Onboarding::Asked(L"--onboarding") && !Onboarding::Asked(L"--profile default"), "--onboarding-debug alone also asks for the window");
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
