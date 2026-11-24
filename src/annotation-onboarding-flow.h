#pragma once
// Annotation mode tutorial (#125 v2, validated by Val) — the logic. Paper « Browser Design » › « #125 v2 — Tutoriel
// annotation (validé) », row 1: A1 Bienvenue, A2 Essai : choisir, A3 Essai : écrire, A4 Essai : résultat, A5 Où ça marche,
// A6 « Ton agent » (Paper « Onboarding agents — Ton agent (maquette prête) », in the place of « Connecter »), A7 Prêt (row 2:
// the trial steps in another profile colour). Pure C++17, no Win32: unit-tested in
// tests/onboarding/annotation_onboarding_test.cpp. Window: src/annotation-onboarding.h; host: src/annotation-mode.h.
//
//   A1 ──Suivant──▶ A2 ──(click « Payer »)──▶ A3 ──(Envoyer)──▶ A4 ──Suivant──▶ A5 ──Suivant──▶ A6 ──Plus tard──▶ A7 ──Terminer──▶ Finished
//                    └──── Passer l'essai ────┴──────────────────────────────▶ A5          A4 « Recommencer » ▶ A2 (button back to 160)
//   A6 « Ton agent »: its strong button acts on the agents (install, sign in, retry: src/annotation-onboarding-agent.h);
//   once an agent is ready it is « Ouvrir le chat », which ends the tutorial (Finished) and opens the chat; its
//   secondary is « Plus tard » (to A7) until then, « Retour » (to A5) once ready.
//   « Retour » one step back (A5 goes back to A4 once the trial was done, to A2 otherwise); « Plus tard » (A1), the close
//   button and Échap end it (Skipped) from any step.
//   Alone (the chat's first message, the band's reminder: OpenTonAgent): only A6, no progress; « Plus tard », the close
//   button and Échap close it (Skipped), « Ouvrir le chat » too (Finished).
//  * The trial is a fake page drawn by the window (no web page, no agent): hovering an element frames it with its tag,
//    clicking « Payer » picks it; A3's note arrives written and « Envoyer » makes the button wider (any text: the demo
//    does not read it).
//  * When: the first time the person turns the mode on (in the place of « Connecter un agent »); from the card of a live
//    site (« Découvrir en 1 minute », E1b); from the welcome window's step 6 (« Essayer en 1 minute », E2), which starts
//    at A2. Finished or skipped, it never opens by itself again (setting annotation-onboarded); E1b can always reopen it.
#include <string>

namespace AnnotationOnboarding {

enum class Step { What = 0, Pick = 1, Write = 2, Result = 3, Where = 4, Connect = 5, Ready = 6, Closed = 7 };   // Connect: « Ton agent »
enum class Ending { None, Finished, Skipped };
inline constexpr int StepCount = 7;
inline int Index(Step s) { return s == Step::Closed ? -1 : static_cast<int>(s); }
inline bool Trial(Step s) { return s == Step::Pick || s == Step::Write || s == Step::Result; }

// The fake page's elements (A2): what the pointer frames, with the tag it shows.
enum class Part { None, Header, Title, Email, Card, Pay };
inline const char* TagOf(Part p) {
    switch (p) { case Part::Header: return "header"; case Part::Title: return "h2"; case Part::Email: case Part::Card: return "input"; case Part::Pay: return "button"; default: return ""; }
}

class Flow {
    Step step_ = Step::What; Ending ending_ = Ending::None; bool tried_ = false, alone_ = false, chat_ = false; std::wstring text_, suggestion_;
public:
    Flow() = default;
    explicit Flow(Step start) : step_(start) {}
    // `alone`: the « Ton agent » screen by itself (it always starts there).
    Flow(Step start, bool alone) : step_(alone ? Step::Connect : start), alone_(alone) {}
    Step step() const { return step_; }
    Ending ending() const { return ending_; }
    bool open() const { return step_ != Step::Closed; }
    bool alone() const { return alone_; }
    bool tried() const { return tried_; }                 // the button was made wider (A4 reached by « Envoyer »)
    bool chat() const { return chat_; }                   // it ended on « Ouvrir le chat »: the host opens the chat
    const std::wstring& text() const { return text_; }    // what the person typed in A3's note
    bool CanSend() const { return step_ == Step::Write && text_.find_first_not_of(L" \t\r\n") != std::wstring::npos; }

    // The main button: « Suivant » (A1, A4, A5), « Terminer » (A7: the end). A2 and A3 have « Passer l'essai » in its
    // place (Skip trial). A6's strong button acts on the agents (the window); A6 → A7 is Later().
    bool Next() {
        switch (step_) {
        case Step::What: step_ = Step::Pick; return true;
        case Step::Result: case Step::Pick: case Step::Write: step_ = Step::Where; return true;   // A2 / A3: « Passer l'essai »
        case Step::Where: step_ = Step::Connect; return true;
        case Step::Connect: return Later();
        case Step::Ready: step_ = Step::Closed; ending_ = Ending::Finished; return true;
        default: return false;
        }
    }
    // The secondary button: « Plus tard » (A1), « Retour » (A2, A3, A5, A6 once ready, A7), « Recommencer » (A4).
    bool Back() {
        if (alone_) return Skip();
        switch (step_) {
        case Step::Pick: step_ = Step::What; return true;
        case Step::Write: step_ = Step::Pick; return true;
        case Step::Result: Restart(); return true;
        case Step::Where: step_ = tried_ ? Step::Result : Step::Pick; return true;
        case Step::Connect: step_ = Step::Where; return true;
        case Step::Ready: step_ = Step::Connect; return true;
        default: return false;
        }
    }
    // A6 « Plus tard »: no agent now, the tutorial goes on to A7 (alone: the screen closes).
    bool Later() {
        if (step_ != Step::Connect) return false;
        if (alone_) return Skip();
        step_ = Step::Ready; return true;
    }
    // A6 « Ouvrir le chat » (an agent is ready): the end, and the host opens the chat.
    bool OpenChat() { if (step_ != Step::Connect) return false; step_ = Step::Closed; ending_ = Ending::Finished; chat_ = true; return true; }
    bool Skip() { if (step_ == Step::Closed) return false; step_ = Step::Closed; ending_ = Ending::Skipped; return true; }
    // A3's note arrives written (Val, 2026-11-24: « Envoyez la demande »): picking « Payer » fills it with the suggestion
    // (the window gives the catalogue's « Rends ce bouton plus large »), so « Envoyer » is on at once. It stays editable
    // (any text widens the button; emptied, the hint shows and « Envoyer » dims again). A new language replaces the
    // suggestion when the person has not changed it.
    void Suggest(std::wstring s) { if (step_ == Step::Write && text_ == suggestion_) text_ = s; suggestion_ = std::move(s); }
    const std::wstring& suggestion() const { return suggestion_; }
    // A2: a click on the fake page. Only « Payer » picks.
    bool Pick(Part p) { if (step_ != Step::Pick || p != Part::Pay) return false; step_ = Step::Write; text_ = suggestion_; return true; }
    // A3: the note, as written or edited.
    void SetText(std::wstring t) { if (step_ == Step::Write) text_ = std::move(t); }
    // A3: « Envoyer » (button or Entrée).
    bool Send() { if (!CanSend()) return false; tried_ = true; step_ = Step::Result; return true; }
    // A4: « Recommencer »: the trial again, from the page as it was (the next pick writes the note again).
    void Restart() { step_ = Step::Pick; tried_ = false; text_.clear(); }
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

// ---- what A7 says about the agents (the agents' one status, src/agent-status.h) --------------------------------
// A7's status pill and its sentence: the agent that will read the notes — a READY one (installed and signed in, the
// chat's own test: never « prêt » when the chat says « Pas installé »), Claude Code first; none otherwise.
enum class Reader { None, Claude, Codex };
inline Reader ReaderOf(bool claudeReady, bool codexReady) {
    if (claudeReady) return Reader::Claude;
    if (codexReady) return Reader::Codex;
    return Reader::None;
}
inline const wchar_t* ReaderName(Reader r) { return r == Reader::Codex ? L"Codex" : L"Claude Code"; }   // i18n-exempt: product names

// ---- A4: the button grows (160 → 260 DIP in Paper) --------------------------------------------------------
inline constexpr float PayFrom = 160, PayTo = 260;
inline constexpr int GrowMs = 400, ShowDoneMs = 200;   // Val's default #5: 400 ms, then « Fait » and the measure fade in

} // namespace AnnotationOnboarding
