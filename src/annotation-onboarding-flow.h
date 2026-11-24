#pragma once
// Annotation mode onboarding (#125, variant A) — the logic. Paper « Browser Design » › « MCP - Design UI UX », row 1
// under « #125 · Onboarding du mode Annotation — 3 propositions »: « Proposition — Onboarding annotation · A1 À quoi
// ça sert », « · A2 Où ça marche », « · A3 Connecter », « · A4 Prêt ». Pure C++17, no Win32: unit-tested in
// tests/annotations/annotation_onboarding_test.cpp. Window: src/annotation-onboarding.h; host: src/annotation-mode.h.
//
//   A1 What ──Suivant──▶ A2 Where ──Suivant──▶ A3 Connect ──Continuer──▶ A4 Ready ──Annoter ma page──▶ closed (Finished)
//      │                    ◀──Retour──             ◀──Retour──              ◀──Retour──
//      └ Plus tard ─▶ closed (Skipped)        the close button and Échap: closed (Skipped), from any step
//
//  * When: the first time the person turns the mode on (it opens over the page, the mode already on behind it, in
//    the place of the « Connecter un agent » modal). Once Finished or Skipped it never comes back (setting
//    annotation-onboarded); a window closed by the mode itself (the page went away) does not count.
//  * A3 connects through the host's real code (src/agent-connect.h); A4 shows which agent will read the notes.
#include <string>

namespace AnnotationOnboarding {

enum class Step { What = 0, Where = 1, Connect = 2, Ready = 3, Closed = 4 };
enum class Ending { None, Finished, Skipped };
inline constexpr int StepCount = 4;
inline int Index(Step s) { return s == Step::Closed ? -1 : static_cast<int>(s); }

class Flow {
    Step step_ = Step::What; Ending ending_ = Ending::None;
public:
    Step step() const { return step_; }
    Ending ending() const { return ending_; }
    bool open() const { return step_ != Step::Closed; }
    // The primary button: « Suivant » (A1, A2), « Continuer » (A3), « Annoter ma page » (A4: the end).
    bool Next() {
        switch (step_) {
        case Step::What: step_ = Step::Where; return true;
        case Step::Where: step_ = Step::Connect; return true;
        case Step::Connect: step_ = Step::Ready; return true;
        case Step::Ready: step_ = Step::Closed; ending_ = Ending::Finished; return true;
        default: return false;
        }
    }
    // « Retour » (A2 … A4).
    bool Back() {
        switch (step_) {
        case Step::Where: step_ = Step::What; return true;
        case Step::Connect: step_ = Step::Where; return true;
        case Step::Ready: step_ = Step::Connect; return true;
        default: return false;
        }
    }
    // « Plus tard » (A1), the close button, Échap.
    bool Skip() { if (step_ == Step::Closed) return false; step_ = Step::Closed; ending_ = Ending::Skipped; return true; }
};

// ---- when the mode opens it -------------------------------------------------------------------------------
struct Launch {
    bool onboarded = false;   // setting annotation-onboarded: finished or skipped once already
    bool forced = false;      // « --annotation-onboarding » (QA, design preview): every time the mode is turned on
    bool direct = false;      // the mode was started on an element from the page menu: the note comes first
    bool chatOpen = false;    // the chat was asked for: its agent is the chat's, no modal
    bool busy = false;        // another modal or the note panel is already open
};
inline bool ShouldShow(const Launch& l) {
    if (l.direct || l.chatOpen || l.busy) return false;
    return l.forced || !l.onboarded;
}
inline constexpr const wchar_t* Switch = L"--annotation-onboarding";

// ---- what A3 and A4 show about the agents ------------------------------------------------------------------
enum class Agent { Claude = 0, Codex = 1 };
enum class Link { Idle, Working, Connected, Failed, Reconnect };
// A4's status pill and its sentence: the agent that will read the notes (Claude first when both are connected),
// none when no agent is connected yet.
enum class Reader { None, Claude, Codex };
inline Reader ReaderOf(Link claude, Link codex) {
    if (claude == Link::Connected) return Reader::Claude;
    if (codex == Link::Connected) return Reader::Codex;
    return Reader::None;
}
inline const wchar_t* ReaderName(Reader r) { return r == Reader::Codex ? L"Codex" : L"Claude"; }   // i18n-exempt: product names
// A3's row: a click connects only from a resting state (as in the « Connecter un agent » modal).
inline bool Connectable(Link l) { return l == Link::Idle || l == Link::Failed || l == Link::Reconnect; }

} // namespace AnnotationOnboarding
