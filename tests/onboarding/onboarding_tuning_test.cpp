// Onboarding motion as numbers (src/onboarding-tuning.h): every board-to-board transition and the hand-off to
// the browser — defaults (the designer's settings), the text round trip of the review panel's file, bounds, and
// the timelines the window plays, with and without reduced motion.
#include <cmath>
#include <cstdio>
#include "../../src/onboarding-tuning.h"

static int failures = 0, checks = 0;
static void Expect(bool ok, const char* what) { ++checks; std::printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++failures; }
namespace T = Onboarding::Tuning;
using Onboarding::Screen;
static bool Near(float a, float b, float e = 1e-3f) { return std::fabs(a - b) <= e; }

int main() {
    const T::Values d = T::Defaults();
    // ---- defaults: the designer's pace on every transition ----
    bool pace = true;
    for (T::Section s : {T::T12, T::T23, T::T34, T::T45, T::T56, T::T67}) {
        const T::Row& r = d[s];
        pace = pace && Near(r[T::Duration], 380) && Near(r[T::OutShare], .23f) && Near(r[T::ContentRise], 0) && Near(r[T::MockDelay], 0) && Near(r[T::MockDuration], 740) &&
               Near(r[T::CurveX1], .33f) && Near(r[T::CurveY1], .99f) && Near(r[T::CurveX2], .42f) && Near(r[T::CurveY2], .96f) && Near(r[T::MockDim], 0);
    }
    Expect(pace, "every board-to-board transition: content 380 ms (out share 0.23), main movement 740 ms on cubic-bezier(0.33, 0.99, 0.42, 0.96)");
    const T::Row& e = d[T::Exit];
    Expect(Near(e[T::Hold], 500) && Near(e[T::LogoDuration], 740) && Near(e[T::LogoScale], .92f) && Near(e[T::WindowOverlap], 200) && Near(e[T::WindowDuration], 740) && Near(e[T::WindowScale], .92f) &&
           Near(e[T::BrowserOverlap], 300) && Near(e[T::BrowserDuration], 740) && Near(e[T::BrowserScale], .96f) && Near(e[T::ExitX1], .33f) && Near(e[T::ExitY1], .99f) && Near(e[T::ExitX2], .42f) && Near(e[T::ExitY2], .96f),
           "hand-off: logo out 740 ms to 0.92, window out 740 ms to 0.92, browser in 740 ms from 0.96, same curve");
    const T::Row& tr = d[T::Trame];
    Expect(Near(tr[T::TrameOpacity], .22f) && Near(tr[T::TramePitch], 2) && Near(tr[T::TramePeriod], 8) && Near(tr[T::TrameBase], .18f) && Near(tr[T::TrameWave], 1) && T::ParamCount(T::Trame) == T::TrameCount,
           "board 6's dither: the band's pitch (2), base (0.18) and wave (1), opacity 0.22, one pass in 8 s");
    Expect(T::Write(d).find("\ntrame.opacity=0.22\n") != std::string::npos && Near(T::Read("browser-onboarding-tuning 1\ntrame.opacity=0.4\ntrame.pitch=9\n")[T::Trame][T::TrameOpacity], .4f) && Near(T::Read("browser-onboarding-tuning 1\ntrame.pitch=9\n")[T::Trame][T::TramePitch], 6), "the dither's values are in the file too, bounded like the others");
    bool sane = true;
    for (int s = 0; s < T::Sections; ++s) for (int i = 0; i < T::ParamCount(static_cast<T::Section>(s)); ++i) { const auto& p = T::Params(static_cast<T::Section>(s))[i]; sane = sane && p.min < p.max && p.def >= p.min && p.def <= p.max && p.step > 0 && p.label && *p.label && p.key && *p.key; }
    Expect(sane && T::ParamCount(T::T23) == T::Count && T::ParamCount(T::Exit) == T::ExitCount && T::ExitCount <= T::MaxParams, "every parameter of every section has a French label, a key, bounds around its default and a step");
    // ---- the file ----
    Expect(T::Read(T::Write(d)) == d && T::Write(d) == T::Write(T::Read(T::Write(d))), "defaults survive the round trip");
    T::Values v = d; float seed = 0;
    for (int s = 0; s < T::Sections; ++s) for (int i = 0; i < T::ParamCount(static_cast<T::Section>(s)); ++i) { const auto& p = T::Params(static_cast<T::Section>(s))[i]; seed += .137f; v[static_cast<T::Section>(s)][static_cast<std::size_t>(i)] = T::Step(static_cast<T::Section>(s), i, p.min + (p.max - p.min) * (seed - std::floor(seed))); }
    Expect(v != d && T::Read(T::Write(v)) == v, "every parameter of every section survives the round trip");
    const std::string text = T::Write(d);
    Expect(text.rfind("browser-onboarding-tuning 1\n", 0) == 0 && text.find("\ntransition12.duration=380\n") != std::string::npos && text.find("\ntransition23.mockDuration=740\n") != std::string::npos && text.find("\ntransition67.curveY2=0.96\n") != std::string::npos && text.find("\nexit.browserScale=0.96\n") != std::string::npos,
           "the file: a header, then one <transition>.<parameter>=<value> per line");
    Expect(T::Read("") == d && T::Read("something else\ntransition23.duration=900\n") == d, "an empty or foreign file gives the defaults");
    const T::Values user = T::Read("browser-onboarding-tuning 1\ntransition23.duration=380\ntransition23.outShare=0.23\ntransition23.contentRise=0\ntransition23.mockDelay=0\ntransition23.mockDuration=740\ntransition23.curveX1=0.33\ntransition23.curveY1=0.99\ntransition23.curveX2=0.42\ntransition23.curveY2=0.96\ntransition23.mockDim=0.00\n");
    Expect(user == d, "the file the designer sent (2 -> 3 only, the first panel's format) reads as the defaults");
    const T::Values partial = T::Read("browser-onboarding-tuning 1\r\ntransition34.duration=300\r\nunknown.key=5\ntransition34.outShare=abc\nexit.hold=120\nnodot=1\n");
    Expect(Near(partial[T::T34][T::Duration], 300) && Near(partial[T::T34][T::OutShare], .23f) && Near(partial[T::T23][T::Duration], 380) && Near(partial[T::Exit][T::Hold], 120), "a line only changes its own transition; missing, unknown and unreadable lines keep their defaults");
    const T::Values wild = T::Read("browser-onboarding-tuning 1\ntransition12.duration=99999\ntransition12.outShare=-3\ntransition12.curveY1=nan\nexit.logoScale=0\nexit.browserScale=7\n");
    Expect(Near(wild[T::T12][T::Duration], 1500) && Near(wild[T::T12][T::OutShare], 0) && Near(wild[T::T12][T::CurveY1], .99f) && Near(wild[T::Exit][T::LogoScale], .5f) && Near(wild[T::Exit][T::BrowserScale], 1), "values are bounded; not-a-number is refused");
    Expect(Near(T::Step(T::T23, T::Duration, 233.4f), 235) && Near(T::Step(T::T23, T::OutShare, .437f), .44f) && Near(T::Step(T::Exit, T::LogoScale, .913f), .91f), "slider values snap to the parameter's step");
    // ---- which section a change of board plays ----
    Expect(T::Between(Screen::Welcome, Screen::Profile) == T::T12 && T::Between(Screen::Profile, Screen::Welcome) == T::T12, "1 <-> 2");
    Expect(T::Between(Screen::Profile, Screen::ImportPick) == T::T23 && T::Between(Screen::ImportPick, Screen::Profile) == T::T23, "2 <-> 3");
    Expect(T::Between(Screen::ImportPick, Screen::ImportRunning) == T::T34 && T::Between(Screen::ImportRunning, Screen::ImportPick) == T::T34 && T::Between(Screen::ImportError, Screen::ImportRunning) == T::T34, "3 <-> 4, and « Réessayer » from 5.1");
    Expect(T::Between(Screen::ImportRunning, Screen::ImportDone) == T::T45 && T::Between(Screen::ImportRunning, Screen::ImportError) == T::T45, "4 -> 5 and 4 -> 5.1");
    Expect(T::Between(Screen::ImportDone, Screen::Annotation) == T::T56 && T::Between(Screen::ImportPick, Screen::Annotation) == T::T56 && T::Between(Screen::ImportError, Screen::Annotation) == T::T56, "5 -> 6, and the « Passer » shortcuts to 6");
    Expect(T::Between(Screen::Annotation, Screen::Outro) == T::T67 && T::Between(Screen::ImportDone, Screen::Outro) == T::T67, "6 -> 7, and straight to 7 when the annotation board is absent");
    // ---- the curve and a transition's timeline ----
    Expect(Near(T::Bezier(0, 0, 1, 1, .3f), .3f, .01f) && T::Bezier(.33f, .99f, .42f, .96f, 0) == 0 && Near(T::Bezier(.33f, .99f, .42f, .96f, 1), 1), "cubic bezier: linear when the handles sit on the diagonal, fixed ends");
    Expect(T::Bezier(.33f, .99f, .42f, .96f, .3f) > .6f && T::Bezier(.33f, .99f, .42f, .96f, .3f) < T::Bezier(.33f, .99f, .42f, .96f, .6f), "the designer's curve: fast first, always forward");
    const T::Row& r = d[T::T23];
    Expect(Near(T::TotalMs(r), 740), "a transition lasts until its last part ends: the 740 ms movement");
    Expect(T::At(r, -50).mock == 0 && T::At(r, 0).oldAlpha == 1 && T::At(r, 0).newAlpha == 0 && !T::At(r, 0).swapped, "before the start: the old board, untouched");
    const T::Frame early = T::At(r, 43.7f);
    Expect(!early.swapped && Near(early.oldAlpha, .5f, .01f) && early.newAlpha == 0 && early.mock > 0 && early.mock < 1, "first share: the old content fades out while the movement has already begun");
    const T::Frame later = T::At(r, 380 * (.23f + .77f / 2));
    Expect(later.swapped && later.oldAlpha == 0 && Near(later.newAlpha, .5f, .01f), "after the share: the new content fades in");
    const T::Frame content = T::At(r, 380);
    Expect(content.swapped && content.newAlpha == 1 && content.mock < 1 && !T::Done(r, 380), "the content is in at 380 ms; the movement is still settling");
    const T::Frame end = T::At(r, 740);
    Expect(end.newAlpha == 1 && Near(end.mock, 1) && end.rise == 0 && end.dim == 0 && T::Done(r, 740) && !T::Done(r, 739), "at 740 ms: the new board, settled");
    T::Row w = r; w[T::MockDelay] = 60; w[T::ContentRise] = 12; w[T::MockDim] = .3f;
    Expect(T::At(w, 30).mock == 0 && Near(T::TotalMs(w), 800) && T::At(w, 200).rise > 0 && T::At(w, 200).rise <= 12 && T::At(w, 380).rise == 0 && T::At(w, 430).dim > 0 && T::At(w, 430).dim <= .3f && T::At(w, 800).dim == 0, "delay, rise and dimming do what their sliders say");
    // ---- the hand-off ----
    const T::ExitPlan p = T::ExitPlanFor(e, false);
    Expect(Near(p.logoStart, 500) && Near(p.windowStart, 500 + 740 - 200) && Near(p.browserStart, 1040 + 740 - 300) && Near(p.End(), 1480 + 740), "hand-off order: pause, logo, window (200 ms before the logo is gone), browser (300 ms before the window is gone)");
    const T::ExitFrame hold = T::ExitAt(p, 300);
    Expect(hold.logoScale == 1 && hold.logoAlpha == 1 && hold.windowScale == 1 && hold.windowAlpha == 1 && hold.browserAlpha == 0 && !hold.windowLeaving && !hold.browserEntering && !hold.done, "during the pause nothing moves");
    const T::ExitFrame logo = T::ExitAt(p, 700);
    Expect(logo.logoScale < 1 && logo.logoScale >= .92f && logo.logoAlpha < 1 && logo.logoAlpha > 0 && !logo.windowLeaving && logo.windowAlpha == 1, "step 1: the logo shrinks and fades; the window is still whole");
    const T::ExitFrame window = T::ExitAt(p, 1300);
    Expect(Near(window.logoAlpha, 0, .02f) && Near(window.logoScale, .92f, .01f) && window.windowLeaving && window.windowScale < 1 && window.windowScale >= .92f && window.windowAlpha < 1 && !window.browserEntering && window.browserAlpha == 0, "step 2: the logo is gone, the window shrinks and fades");
    const T::ExitFrame both = T::ExitAt(p, 1600);
    Expect(both.browserEntering && both.browserAlpha > 0 && both.browserScale >= .96f && both.browserScale < 1 && both.windowAlpha > 0 && both.windowAlpha < .2f, "steps 2 and 3 overlap: the browser is coming in before the window is fully gone (no empty gap)");
    const T::ExitFrame last = T::ExitAt(p, p.End());
    Expect(last.done && Near(last.browserScale, 1) && Near(last.browserAlpha, 1) && Near(last.windowAlpha, 0) && Near(last.windowScale, .92f, .001f), "the end: the browser at its size, fully there");
    T::Row odd = e; odd[T::WindowOverlap] = 1000; odd[T::BrowserOverlap] = 1000;
    const T::ExitPlan o = T::ExitPlanFor(odd, false);
    Expect(o.windowStart >= o.logoStart && o.browserStart >= o.windowStart, "an overlap longer than the step before it never starts a step early");
    const T::ExitPlan calm = T::ExitPlanFor(e, true);
    Expect(calm.logoScale == 1 && calm.windowScale == 1 && calm.browserScale == 1 && calm.logoMs <= 150 && calm.windowMs <= 150 && calm.browserMs <= 150 && calm.windowStart >= calm.logoStart + calm.logoMs && calm.browserStart >= calm.windowStart + calm.windowMs, "reduced motion: short fades one after the other, nothing scales");
    const T::ExitFrame calmMid = T::ExitAt(calm, calm.windowStart + 60);
    Expect(calmMid.windowScale == 1 && calmMid.logoScale == 1 && calmMid.windowAlpha < 1 && calmMid.windowAlpha > 0, "reduced motion: opacity only");
    // ---- the switch ----
    Expect(Onboarding::HasSwitch(L"--onboarding --onboarding-debug", Onboarding::DebugSwitch) && Onboarding::HasSwitch(L"--onboarding --onboarding-debug") && !Onboarding::HasSwitch(L"--onboarding", Onboarding::DebugSwitch), "--onboarding-debug is its own switch");
    Expect(!Onboarding::HasSwitch(L"--onboarding-debug") && Onboarding::Asked(L"--onboarding-debug") && Onboarding::Asked(L"--onboarding") && !Onboarding::Asked(L"--profile default"), "--onboarding-debug alone also asks for the window");
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
