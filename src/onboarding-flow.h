#pragma once
// First-run onboarding — the logic (Paper p-R-0 « Onboarding — fenêtre de lancement », boards 1 … 7).
// Pure C++17, no Win32: unit-tested in tests/onboarding/onboarding_flow_test.cpp. The window is
// src/onboarding.h; the host wiring (profile, import, language, annotation mode) is src/onboarding-integration.h.
//
//   1 Welcome ──Commencer──▶ 2 Profile ──Continuer──▶ 3 ImportPick ──Importer──▶ 4 ImportRunning
//      │                        ◀──Retour──               ◀──Retour / Annuler──        │
//      │ Passer et naviguer                                │ Passer                    ├─ all saved ─▶ 5 ImportDone ──Continuer─┐
//      ▼                                                   ▼                           └─ a failure ─▶ 5.1 ImportError ─Passer──┤
//   browser (Skipped)                                 6 Annotation ◀───────────────────────────────────  Réessayer ▶ 4 ───────┘
//                                                          │ Activer / Pas intéressé(e)
//                                                          ▼
//                                                     7 Outro (closing logo) ──▶ browser (Completed)
//
//  * Échap and the close button leave from any screen, straight to the browser (Skipped); what was already
//    saved (the profile after « Continuer », an import that finished) stays. On the closing logo they only
//    shorten it (the flow was completed).
//  * « Passer » on the import and on its error state skips that step, not the rest of the flow.
//  * An import is identified by a token: a result that comes back after « Annuler », after the window closed
//    or after another import started is ignored. « Réessayer » asks only for what failed.
//  * Screen 6 only exists when the annotation mode is compiled in.
#include <cmath>
#include <string>

namespace Onboarding {

enum class Screen { Welcome, Profile, ImportPick, ImportRunning, ImportDone, ImportError, Annotation, Outro, Closed };
enum class Ending { None, Skipped, Completed };

// ---- when the window shows -----------------------------------------------------------------------------
struct Launch {
    bool requested = false;       // --onboarding (QA, design preview)
    bool installed = false;       // <root>\versions\<v>\browser.exe with its data in <root>\data (not a portable folder)
    bool hasRegistry = false;     // <data>\profiles.txt exists: a profile was already set up
    bool hasData = false;         // settings, session or engine folder from a build older than the profiles
    bool optOut = false;          // BROWSER_NO_ONBOARDING=1 (automated installs and tests)
    bool smoke = false, privateWindow = false, motionLab = false;
};
enum class Reason { None, FirstRun, Requested };
inline Reason ShouldShow(const Launch& l) {
    if (l.smoke || l.privateWindow || l.motionLab) return Reason::None;
    if (l.requested) return Reason::Requested;
    if (l.optOut) return Reason::None;
    // A portable folder is a developer or test copy: it asks with the switch. First run = a fresh install.
    return l.installed && !l.hasRegistry && !l.hasData ? Reason::FirstRun : Reason::None;
}
inline constexpr const wchar_t* Switch = L"--onboarding";
// The exact switch as one argument of the command line (quotes allowed around it).
inline bool HasSwitch(const wchar_t* args) {
    if (!args) return false;
    const std::wstring s = args, key = Switch;
    for (std::size_t at = s.find(key); at != std::wstring::npos; at = s.find(key, at + 1)) {
        const wchar_t before = at ? s[at - 1] : L' ', after = at + key.size() < s.size() ? s[at + key.size()] : L' ';
        if ((before == L' ' || before == L'"' || before == L'\t') && (after == L' ' || after == L'"' || after == L'\t')) return true;
    }
    return false;
}

// ---- the flow ----------------------------------------------------------------------------------------
class Flow {
public:
    struct Config { bool annotation = true; };
private:
    Config config_; Screen screen_ = Screen::Welcome; Ending ending_ = Ending::None;
    bool profileApplied_ = false, annotate_ = false;
    bool source_ = false, wantBookmarks_ = true, wantHistory_ = true;      // the import settings (screen 3)
    bool askedBookmarks_ = false, askedHistory_ = false;                    // what the running import reads
    bool failedBookmarks_ = false, failedHistory_ = false;                  // what the error state reports
    bool retrying_ = false; unsigned token_ = 0, running_ = 0;

    Screen AfterImport() const { return config_.annotation ? Screen::Annotation : Screen::Outro; }
    unsigned Run(bool bookmarks, bool history, bool retry) {
        askedBookmarks_ = bookmarks; askedHistory_ = history; retrying_ = retry;
        running_ = ++token_; if (!running_) running_ = ++token_;
        screen_ = Screen::ImportRunning; return running_;
    }
public:
    Flow() = default;
    explicit Flow(Config config) : config_(config) {}
    Screen screen() const { return screen_; }
    Ending ending() const { return ending_; }
    bool profileApplied() const { return profileApplied_; }
    bool annotate() const { return annotate_; }
    bool askedBookmarks() const { return askedBookmarks_; }
    bool askedHistory() const { return askedHistory_; }
    bool failedBookmarks() const { return failedBookmarks_; }
    bool failedHistory() const { return failedHistory_; }
    bool wantBookmarks() const { return wantBookmarks_; }
    bool wantHistory() const { return wantHistory_; }
    bool retrying() const { return retrying_; }

    // Screen 1 « Commencer ».
    bool Start() { if (screen_ != Screen::Welcome) return false; screen_ = Screen::Profile; return true; }
    // « Passer et naviguer », Échap, the close button.
    bool Skip() {
        if (screen_ == Screen::Closed) return false;
        ending_ = screen_ == Screen::Outro ? Ending::Completed : Ending::Skipped;
        running_ = 0; screen_ = Screen::Closed; return true;
    }
    // « Retour »: the drafts are kept by the window.
    bool Back() {
        if (screen_ == Screen::Profile) { screen_ = Screen::Welcome; return true; }
        if (screen_ == Screen::ImportPick) { screen_ = Screen::Profile; return true; }
        return false;
    }
    // Screen 2 « Continuer », once the host saved the profile.
    bool ProfileAccepted() { if (screen_ != Screen::Profile) return false; profileApplied_ = true; screen_ = Screen::ImportPick; return true; }

    void SetSourceAvailable(bool available) { source_ = available; }
    void SetCategories(bool bookmarks, bool history) { wantBookmarks_ = bookmarks; wantHistory_ = history; }
    bool CanImport() const { return screen_ == Screen::ImportPick && source_ && (wantBookmarks_ || wantHistory_); }
    // Screen 3 « Importer mes données »: the token of this import, 0 when refused.
    unsigned BeginImport() { return CanImport() ? Run(wantBookmarks_, wantHistory_, false) : 0; }
    // Screen 4 « Annuler »: back to where the import was started from.
    bool CancelImport() {
        if (screen_ != Screen::ImportRunning) return false;
        running_ = 0; screen_ = retrying_ ? Screen::ImportError : Screen::ImportPick; return true;
    }
    // The worker's answer. A category that was not asked cannot fail.
    bool ImportFinished(unsigned token, bool bookmarksSaved, bool historySaved) {
        if (screen_ != Screen::ImportRunning || !token || token != running_) return false;
        running_ = 0;
        failedBookmarks_ = askedBookmarks_ && !bookmarksSaved; failedHistory_ = askedHistory_ && !historySaved;
        screen_ = failedBookmarks_ || failedHistory_ ? Screen::ImportError : Screen::ImportDone;
        return true;
    }
    // Screen 5.1 « Réessayer »: only what is missing.
    unsigned Retry() { return screen_ == Screen::ImportError && (failedBookmarks_ || failedHistory_) ? Run(failedBookmarks_, failedHistory_, true) : 0; }
    // Screens 3 and 5.1 « Passer ».
    bool SkipImport() { if (screen_ != Screen::ImportPick && screen_ != Screen::ImportError) return false; screen_ = AfterImport(); return true; }
    // Screen 5 « Continuer ».
    bool Continue() { if (screen_ != Screen::ImportDone) return false; screen_ = AfterImport(); return true; }
    // Screen 6 « Activer » / « Pas intéressé(e) ».
    bool AnswerAnnotation(bool activate) { if (screen_ != Screen::Annotation) return false; annotate_ = activate; screen_ = Screen::Outro; return true; }
    // Screen 7 is over.
    bool OutroDone() { if (screen_ != Screen::Outro) return false; ending_ = Ending::Completed; screen_ = Screen::Closed; return true; }
};

// ---- motion policy (Paper is silent: design skills — short ease-out changes, reduced motion keeps opacity) ----
// The shaders run only when someone can see them move.
inline bool ShaderLoops(bool reducedMotion, bool hardwareDevice, bool visible, bool minimised) { return !reducedMotion && hardwareDevice && visible && !minimised; }
// 0..1 along a loop of `period` seconds; the end of a turn is its start.
inline double LoopPhase(double seconds, double period) {
    if (!(period > 0) || !std::isfinite(seconds)) return 0;
    const double turn = seconds / period, phase = turn - std::floor(turn);
    return phase >= 1 || phase < 0 ? 0 : phase;
}
inline constexpr double HalftoneLoopSeconds = 3.2;     // the dot lattice slides one step (≈ 4 px) per turn
inline int TransitionMs(bool reducedMotion) { return reducedMotion ? 120 : 220; }   // screen to screen
inline bool Moves(bool reducedMotion) { return !reducedMotion; }                    // zoom / slide allowed
inline int OutroMs(bool reducedMotion) { return reducedMotion ? 600 : 1400; }       // closing logo, before the browser
inline constexpr int OutroFadeMs = 180;
// Lit ticks of the import progress (Paper screen 4: 19 ticks, 7 lit).
inline int ProgressBars(double fraction, int count) {
    if (!(fraction > 0) || count <= 0) return 0;
    if (fraction >= 1) return count;
    return static_cast<int>(std::floor(fraction * count + .5));
}

} // namespace Onboarding
