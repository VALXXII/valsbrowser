// Onboarding logic (src/onboarding-flow.h): the flow state machine, when the window shows (first run, the
// --onboarding switch, automation), skip / Escape paths, import bookkeeping (stale results, retry of what is
// missing) and the motion policy (reduced motion, software device, hidden window). Pure C++: no window.
#include <cstdio>
#include <cmath>
#include "../../src/onboarding-flow.h"

static int failures = 0, checks = 0;
static void Expect(bool ok, const char* what) { ++checks; std::printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++failures; }

using namespace Onboarding;

static Flow AtImport(bool annotation = true) { Flow f(Flow::Config{annotation}); f.Start(); f.ProfileAccepted(); return f; }

int main() {
    // ---- when the window shows ---------------------------------------------------------------------
    {
        Launch fresh; fresh.installed = true;
        Expect(ShouldShow(fresh) == Reason::FirstRun, "fresh install (installed layout, no registry, no data): shown");
        Launch updated = fresh; updated.hasRegistry = true;
        Expect(ShouldShow(updated) == Reason::None, "a profile registry exists: never shown again");
        Launch legacy = fresh; legacy.hasData = true;
        Expect(ShouldShow(legacy) == Reason::None, "data from before the profiles exist (settings / session / engine folder): not a first run");
        Launch portable; portable.installed = false;
        Expect(ShouldShow(portable) == Reason::None, "portable folder (dev builds, test fixtures): not shown by itself");
        Launch flag = portable; flag.requested = true;
        Expect(ShouldShow(flag) == Reason::Requested, "--onboarding: shown, portable or not");
        Launch flagOld = updated; flagOld.requested = true;
        Expect(ShouldShow(flagOld) == Reason::Requested, "--onboarding on an existing installation: shown (preview)");
        for (int i = 0; i < 3; ++i) {
            Launch l = fresh; l.requested = true; (i == 0 ? l.smoke : i == 1 ? l.privateWindow : l.motionLab) = true;
            Expect(ShouldShow(l) == Reason::None, i == 0 ? "smoke test: never" : i == 1 ? "private window: never" : "motion lab: never");
        }
        Launch optOut = fresh; optOut.optOut = true;
        Expect(ShouldShow(optOut) == Reason::None, "BROWSER_NO_ONBOARDING (automated installs): first run not shown");
        optOut.requested = true;
        Expect(ShouldShow(optOut) == Reason::Requested, "the explicit switch wins over the opt-out");
        Expect(HasSwitch(L"--onboarding") && HasSwitch(L"--profile default --onboarding --restore-window w1") && HasSwitch(L"\"--onboarding\""), "--onboarding read anywhere on the command line");
        Expect(!HasSwitch(L"") && !HasSwitch(L"--onboarding-later") && !HasSwitch(L"https://example.org/--onboarding/x") && !HasSwitch(L"--no-onboarding"), "only the exact switch counts");
    }
    // ---- the happy path ----------------------------------------------------------------------------
    {
        Flow f(Flow::Config{true});
        Expect(f.screen() == Screen::Welcome && f.ending() == Ending::None, "starts on the welcome screen");
        Expect(!f.Back() && f.screen() == Screen::Welcome, "nothing before the welcome screen");
        Expect(f.Start() && f.screen() == Screen::Profile, "Commencer -> create the profile");
        Expect(!f.Start(), "Commencer only exists on the welcome screen");
        Expect(f.Back() && f.screen() == Screen::Welcome && f.Start(), "Retour -> welcome, and forward again");
        Expect(!f.profileApplied(), "nothing is written before Continuer");
        Expect(f.ProfileAccepted() && f.screen() == Screen::ImportPick && f.profileApplied(), "Continuer (profile saved by the host) -> import");
        Expect(f.Back() && f.screen() == Screen::Profile && f.profileApplied(), "Retour from the import edits the same profile");
        f.ProfileAccepted();
        f.SetSourceAvailable(true); f.SetCategories(true, true);
        Expect(f.CanImport(), "a source and something checked: Importer enabled");
        const unsigned t = f.BeginImport();
        Expect(t != 0 && f.screen() == Screen::ImportRunning && f.askedBookmarks() && f.askedHistory(), "Importer mes donnees -> import running, both categories asked");
        Expect(!f.Back() && !f.Continue() && f.BeginImport() == 0, "no Retour / Continuer / second import while it runs");
        Expect(f.ImportFinished(t, true, true) && f.screen() == Screen::ImportDone, "both saved -> done");
        Expect(f.Continue() && f.screen() == Screen::Annotation, "Continuer -> annotation mode intro");
        Expect(f.AnswerAnnotation(true) && f.screen() == Screen::Outro && f.annotate(), "Activer -> closing logo, annotation mode asked");
        Expect(f.ending() == Ending::None, "the browser does not open before the closing screen ends");
        Expect(f.OutroDone() && f.screen() == Screen::Closed && f.ending() == Ending::Completed, "closing screen over -> browser (completed)");
        Expect(!f.OutroDone() && !f.Skip() && !f.Start(), "a closed flow ignores everything");
    }
    // ---- skipping ----------------------------------------------------------------------------------
    {
        Flow f(Flow::Config{true});
        Expect(f.Skip() && f.screen() == Screen::Closed && f.ending() == Ending::Skipped && !f.profileApplied() && !f.annotate(), "Passer et naviguer: straight to the browser, nothing written");
        const Screen all[] = {Screen::Welcome, Screen::Profile, Screen::ImportPick, Screen::ImportRunning, Screen::ImportDone, Screen::ImportError, Screen::Annotation};
        for (Screen s : all) {
            Flow g(Flow::Config{true});
            if (s != Screen::Welcome) g.Start();
            if (s != Screen::Welcome && s != Screen::Profile) g.ProfileAccepted();
            unsigned t = 0;
            if (s == Screen::ImportRunning || s == Screen::ImportDone || s == Screen::ImportError) { g.SetSourceAvailable(true); g.SetCategories(true, true); t = g.BeginImport(); }
            if (s == Screen::ImportDone) g.ImportFinished(t, true, true);
            if (s == Screen::ImportError) g.ImportFinished(t, true, false);
            if (s == Screen::Annotation) g.SkipImport();
            const bool reached = g.screen() == s;
            const bool closed = g.Skip() && g.screen() == Screen::Closed && g.ending() == Ending::Skipped;
            Expect(reached && closed, "Escape / close from any screen: straight to the browser");
            if (s == Screen::ImportRunning) Expect(!g.ImportFinished(t, true, true) && g.screen() == Screen::Closed, "a result arriving after the window closed is ignored");
        }
        Flow o(Flow::Config{false}); o.Start(); o.ProfileAccepted(); o.SkipImport();
        Expect(o.screen() == Screen::Outro, "no annotation mode in this build: the intro is not shown");
        Expect(o.Skip() && o.ending() == Ending::Completed, "Escape on the closing logo only shortens it: the flow was completed");
    }
    // ---- import bookkeeping ------------------------------------------------------------------------
    {
        Flow f = AtImport();
        Expect(!f.CanImport() && f.BeginImport() == 0, "no source: Importer disabled");
        f.SetSourceAvailable(true); f.SetCategories(false, false);
        Expect(!f.CanImport() && f.BeginImport() == 0, "nothing checked: Importer disabled");
        Expect(f.SkipImport() && f.screen() == Screen::Annotation, "Passer on the import -> next step, nothing imported");
    }
    {
        Flow f = AtImport(); f.SetSourceAvailable(true); f.SetCategories(true, false);
        const unsigned t = f.BeginImport();
        Expect(f.askedBookmarks() && !f.askedHistory(), "only what is checked is asked");
        Expect(f.CancelImport() && f.screen() == Screen::ImportPick, "Annuler -> back to the import settings");
        Expect(!f.ImportFinished(t, true, true) && f.screen() == Screen::ImportPick, "the cancelled import's result is ignored");
        const unsigned t2 = f.BeginImport();
        Expect(t2 != 0 && t2 != t && !f.ImportFinished(t, true, true) && f.screen() == Screen::ImportRunning, "a new import has a new token; the old one cannot finish it");
        Expect(f.ImportFinished(t2, true, false) && f.screen() == Screen::ImportDone, "an unasked category cannot fail the import");
    }
    {
        Flow f = AtImport(); f.SetSourceAvailable(true); f.SetCategories(true, true);
        const unsigned t = f.BeginImport();
        Expect(f.ImportFinished(t, true, false) && f.screen() == Screen::ImportError && !f.failedBookmarks() && f.failedHistory(), "history failed (browser open): error state, bookmarks kept");
        Expect(!f.Continue(), "no Continuer on the error state");
        const unsigned r = f.Retry();
        Expect(r != 0 && r != t && f.screen() == Screen::ImportRunning && !f.askedBookmarks() && f.askedHistory(), "Reessayer asks only for what is missing");
        Expect(f.ImportFinished(r, false, false) && f.screen() == Screen::ImportError && f.failedHistory() && !f.failedBookmarks(), "still failing: error state again, bookmarks not reported as failed");
        const unsigned r2 = f.Retry();
        Expect(f.ImportFinished(r2, false, true) && f.screen() == Screen::ImportDone, "retry succeeds -> done");
        Flow g = AtImport(); g.SetSourceAvailable(true); g.SetCategories(true, true);
        g.ImportFinished(g.BeginImport(), false, false);
        Expect(g.screen() == Screen::ImportError && g.failedBookmarks() && g.failedHistory(), "both failed");
        Expect(g.SkipImport() && g.screen() == Screen::Annotation, "Passer on the error -> next step");
        Flow h = AtImport(); h.SetSourceAvailable(true); h.SetCategories(true, true);
        h.ImportFinished(h.BeginImport(), true, false); h.Retry();
        Expect(h.CancelImport() && h.screen() == Screen::ImportError, "Annuler during a retry -> back to the error state it came from");
    }
    {
        Flow f = AtImport(); f.SkipImport();
        Expect(f.AnswerAnnotation(false) && f.screen() == Screen::Outro && !f.annotate(), "Pas interesse(e) -> closing logo, annotation mode not asked");
        Expect(!f.AnswerAnnotation(true) && !f.annotate(), "the answer is given once");
    }
    // ---- motion policy -----------------------------------------------------------------------------
    {
        Expect(ShaderLoops(false, true, true, false), "normal: shaders animate");
        Expect(!ShaderLoops(true, true, true, false), "reduced motion: still frame");
        Expect(!ShaderLoops(false, false, true, false), "software device: still frame");
        Expect(!ShaderLoops(false, true, false, false) && !ShaderLoops(false, true, true, true), "hidden or minimised window: paused");
        Expect(LoopPhase(0, 4) == 0 && std::fabs(LoopPhase(1, 4) - .25) < 1e-9 && std::fabs(LoopPhase(4, 4)) < 1e-9 && std::fabs(LoopPhase(4.5 + 4000, 4) - .125) < 1e-6, "loop phase wraps: the end of a turn is its start");
        Expect(LoopPhase(-1, 4) >= 0 && LoopPhase(3, 0) == 0, "loop phase: clock going back or no period never leaves 0..1");
        Expect(TransitionMs(false) > 0 && TransitionMs(false) <= 300 && TransitionMs(true) <= TransitionMs(false), "screen changes stay short");
        Expect(Moves(false) && !Moves(true), "reduced motion: opacity only, nothing travels");
        Expect(OutroMs(true) > 0 && OutroMs(true) < OutroMs(false) && OutroMs(false) <= 2000, "closing logo: brief, shorter with reduced motion");
        Expect(ProgressBars(0, 19) == 0 && ProgressBars(1, 19) == 19 && ProgressBars(.5, 19) >= 9 && ProgressBars(.5, 19) <= 10 && ProgressBars(7, 19) == 19 && ProgressBars(-1, 19) == 0, "progress ticks follow the fraction, bounded");
    }
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
