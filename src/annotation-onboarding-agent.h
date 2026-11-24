#pragma once
// « Ton agent » (agents onboarding, validated by Val) — what the screen shows, decided from the agents' ONE status
// (src/agent-status.h, design/ton-agent-interface.md). Pure C++17, no Win32: unit-tested in
// tests/onboarding/annotation_onboarding_test.cpp. Drawn by src/annotation-onboarding.h: the tutorial's sixth step (in
// the place of « Connecter ») and the same screen alone (the chat's first message, the band's reminder: OpenTonAgent,
// src/ton-agent-entry.h). The chat's own block (assets/chat-panel/) reads the same status through src/chat-ton-agent.h.
//
// Paper « Browser Design » › « Onboarding agents — Ton agent (maquette prête) »: Claude Code 1 Choisir (survol = Installer)
// 22ZR-0, 2 Installation 2312-0, 3 Prêt 232D-0, 4 Erreur 233O-0, 5 Connexion échouée 25OI-0; Codex 1 Choisir 24VS-0,
// 2 Installation 24X8-0, 3 À connecter (survol = Se connecter) 24YO-0, 3b Connexion dans un autre onglet 263M-0, 4 Prêt
// 2504-0, 5 Erreur 251H-0; the notes « Installation… » 253G-0 and « La tuile lance l'action » 265A-0 (Val's decision).
// Values: design/ton-agent-screen.md.
//
//  * Two tiles, Claude Code then Codex. Under each name its mark: ⤓ Installer · 📶 Connecter · ⤓ Installation… ·
//    📶 Connexion… · ✓ Prêt · ☹ Erreur / Connexion échouée.
//  * A click on a tile RUNS its action at once (Val): Installer, Mettre à jour, Se connecter, Réessayer. That click IS the
//    consent of an install (AgentStatus::UserClicked): nothing installs before it. No selection, no action button.
//  * A tile with an action: at rest #1F1F1F inset 1 #2A2A2A, its mark #A6A6A6; hovered: the hand, #262626 inset 1 #3A3A3A,
//    the mark white. Installation… / Connexion… / Prêt: no hover, no click. A failed tile (danger): hovered, its word
//    becomes « Réessayer » (#FF7B6E) and a click retries. While one agent installs or signs in, the other tile waits
//    (32 %, inert: Designer, note 26AL-0).
//  * The tile acted on is the target (the text above the footer follows it); busy tiles are outlined cyan wherever they
//    are, a ready or failed one when it is the target.
//  * The footer: « Plus tard » on the left; « Continuer » on the right only once an agent is ready.
//  * Under the tiles nothing, except after a failure (two lines and, for an install, « Installer moi-même ↗ ») and
//    while signing in (3b: two lines and, when the sign-in tab was closed, « Rouvrir la page de connexion ↗ »).
#include <algorithm>
#include <optional>
#include "agent-status.h"

namespace TonAgent {

using Agent = AgentStatus::Agent;
using State = AgentStatus::State;
using Reason = AgentStatus::Reason;
using Status = AgentStatus::Status;

inline int Index(Agent a) { return a == Agent::Claude ? 0 : 1; }
inline Agent Other(Agent a) { return a == Agent::Claude ? Agent::Codex : Agent::Claude; }
inline Agent AllAgentsAt(int i) { return i == 0 ? Agent::Claude : Agent::Codex; }

inline bool SignInReason(Reason r) { return r == Reason::SignInFailed || r == Reason::SignInTimeout; }

// ---- a tile ----------------------------------------------------------------------------------------------
// The mark under the name (icon + word). Unknown: the first detection runs, nothing definite (no mark). Update (« Mettre à
// jour », a CLI too old: Designer, note 26AL-0): the download icon with its word; the click installs again.
enum class Mark { Unknown, Install, Update, Installing, Connect, SigningIn, Ready, Error, SignInFailed };
inline Mark MarkOf(const Status& s) {
    switch (s.state) {
    case State::NotInstalled: return s.reason == Reason::TooOld ? Mark::Update : Mark::Install;
    case State::Installing: return Mark::Installing;
    case State::NeedsSignIn: return Mark::Connect;
    case State::SigningIn: return Mark::SigningIn;                                        // 3b « Connexion… »
    case State::Ready: return Mark::Ready;
    case State::Error: return SignInReason(s.reason) ? Mark::SignInFailed : Mark::Error;   // 5 « Connexion échouée »
    default: return Mark::Unknown;
    }
}
// The tile's frame: at rest (#1F1F1F, inset 1 #2A2A2A; hovered #262626, inset 1 #3A3A3A), current (#192A2A, inset 1.5
// #2FD1D9), failed (danger 10 %, inset 1.5 danger). Busy tiles are current wherever they are (2 Installation, 3b); a
// ready or failed tile when it is the target; a tile waiting for its click stays at rest (3 À connecter).
enum class Look { Rest, Current, Danger };
inline Look LookOf(const Status& s, bool target) {
    switch (s.state) {
    case State::Installing: case State::SigningIn: return Look::Current;
    case State::Ready: return target ? Look::Current : Look::Rest;
    case State::Error: return target ? Look::Danger : Look::Rest;
    default: return Look::Rest;
    }
}
// The mark's colour: the profile's light ink (#2FD1D9) on a current tile, danger for a failure, grey (#A6A6A6) else
// (white while a tile with an action is hovered: the window draws it).
enum class Ink { Grey, Accent, Danger };
inline Ink InkOf(const Status& s, bool target) {
    if (s.state == State::Error) return Ink::Danger;
    return LookOf(s, target) == Look::Current ? Ink::Accent : Ink::Grey;
}

// ---- what a click on a tile does (Val: the tile IS the button) -----------------------------------------------
enum class Action { None, Install, Update, SignIn, Retry };
inline Action ActionOf(const Status& s) {
    switch (s.state) {
    case State::NotInstalled: return s.reason == Reason::TooOld ? Action::Update : Action::Install;
    case State::NeedsSignIn: return Action::SignIn;
    case State::Error: return Action::Retry;
    default: return Action::None;   // the first detection, Installation…, Connexion…, Prêt: no hover, no click
    }
}
// While one agent installs or signs in, the other tile waits: 32 %, no hover, no click (Val; Designer, boards 2 and 3b).
inline bool Waits(const Status& other) { return other.state == State::Installing || other.state == State::SigningIn; }
inline bool Clickable(const Status& s, const Status& other) { return ActionOf(s) != Action::None && !Waits(other); }
// « Continuer » (the footer's right): only once an agent is ready.
inline bool CanContinue(const Status& claude, const Status& codex) { return claude.Ready() || codex.Ready(); }

// ---- under the tiles ---------------------------------------------------------------------------------------
// After a failure (two lines; an install: « Installer moi-même ↗ »), while signing in (3b: two lines; « Rouvrir la page
// de connexion ↗ » when the sign-in page was seen and its tab closed).
enum class Trouble { None, Install, SignIn, SigningIn };
inline Trouble TroubleOf(const Status& s) {
    if (s.state == State::SigningIn) return Trouble::SigningIn;
    if (s.state != State::Error) return Trouble::None;
    return SignInReason(s.reason) ? Trouble::SignIn : Trouble::Install;
}
// « Installer moi-même ↗ »: after an install that did not succeed (not after a sign-in).
inline bool ManualOffered(const Status& s) { return TroubleOf(s) == Trouble::Install; }

// ---- the text above the footer ---------------------------------------------------------------------------
// Choose: « Choisissez votre agent » (at the start and after an install failure); Installing: « Installation de X »;
// SignIn: « Connectez X » (also after a sign-in failure, 25OI-0; Claude Code: its paid plan is said here); SigningIn:
// « Connexion en cours » (3b); Ready: « X est prêt ».
enum class Heading { Choose, Installing, SignIn, SigningIn, Ready };
inline Heading HeadingOf(const Status& s) {
    switch (s.state) {
    case State::Installing: return Heading::Installing;
    case State::NeedsSignIn: return Heading::SignIn;
    case State::SigningIn: return Heading::SigningIn;
    case State::Ready: return Heading::Ready;
    case State::Error: return SignInReason(s.reason) ? Heading::SignIn : Heading::Choose;
    default: return Heading::Choose;
    }
}

// ---- the target -------------------------------------------------------------------------------------------
// Before any click: the agent that is busy, else ready, else failed (Claude Code first), else Claude Code.
inline Agent DefaultTarget(const Status& claude, const Status& codex) {
    if (claude.Busy()) return Agent::Claude;
    if (codex.Busy()) return Agent::Codex;
    if (claude.Ready()) return Agent::Claude;
    if (codex.Ready()) return Agent::Codex;
    if (claude.state == State::Error) return Agent::Claude;
    if (codex.state == State::Error) return Agent::Codex;
    return Agent::Claude;
}

// ---- the card's place (Paper: the card glides between them) ------------------------------------------------
namespace Card {
inline constexpr float left = 160, width = 400, pad = 12, gap = 10, radius = 16, tileRadius = 12;
inline constexpr float tilePadTop = 26, tilePadBottom = 18, tilePadX = 12, logo = 34, nameH = 21, markIcon = 14, markGap = 4;
inline constexpr float markTop = tilePadTop + logo + 10 + nameH + 10;   // 101: the mark's row, from the tile's top
inline constexpr float centre = 220;                                   // Choisir, À connecter: « top: calc(50% - 70px) », centred
inline constexpr float topInstalling = 123, topReady = 140, topError = 72;
inline constexpr float troubleGap = 12, troubleLine = 16, troubleLinkGap = 6, troublePadBottom = 2, troublePadX = 16;
// The mark's row: 14 (icon 14, text 13/13) or 17 (« Erreur », « Connexion échouée » 13/17, « ✓ Prêt » 13/17).
inline float MarkHeight(Mark m) { return m == Mark::Ready || m == Mark::Error || m == Mark::SignInFailed ? 17.f : 14.f; }
inline float TileHeight(Mark claude, Mark codex) { return markTop + std::max(MarkHeight(claude), MarkHeight(codex)) + tilePadBottom; }
// The link under the two lines: « Installer moi-même ↗ » after an install failure; « Rouvrir la page de connexion ↗ »
// while signing in, when the window has a page to reopen (the caller says).
inline bool LinkOf(Trouble t) { return t == Trouble::Install; }
inline float TroubleHeight(Trouble t, bool link) { return t == Trouble::None ? 0.f : troubleGap + 2 * troubleLine + (link ? troubleLinkGap + troubleLine : 0.f) + troublePadBottom; }
inline float TroubleHeight(Trouble t) { return TroubleHeight(t, LinkOf(t)); }
inline float Height(Mark claude, Mark codex, Trouble t, bool link) { return 2 * pad + TileHeight(claude, codex) + TroubleHeight(t, link); }
inline float Height(Mark claude, Mark codex, Trouble t) { return Height(claude, codex, t, LinkOf(t)); }
inline float Top(const Status& target, float height) {
    switch (target.state) {
    case State::Installing: return topInstalling;
    case State::Ready: return topReady;
    case State::Error: case State::SigningIn: return topError;   // 4 Erreur, 5 Connexion échouée, 3b: at 72
    default: return centre - height / 2;
    }
}
}  // namespace Card

// ---- the screen's decisions for two statuses and the person's pick -----------------------------------------------
class Screen {
    std::optional<Agent> picked_;
public:
    // A tile acted on (its click, Entrée / Espace on it): that agent stays the target.
    void Pick(Agent a) { picked_ = a; }
    void Reset() { picked_.reset(); }
    bool Picked() const { return picked_.has_value(); }
    Agent Target(const Status& claude, const Status& codex) const { return picked_ ? *picked_ : DefaultTarget(claude, codex); }
};

}  // namespace TonAgent
