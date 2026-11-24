#pragma once
// « Ton agent » (agents onboarding, validated by Val) — what the screen shows, decided from the agents' ONE status
// (src/agent-status.h, design/ton-agent-interface.md). Pure C++17, no Win32: unit-tested in
// tests/onboarding/annotation_onboarding_test.cpp. Drawn by src/annotation-onboarding.h: the tutorial's sixth step (in
// the place of « Connecter ») and the same screen alone (the chat's first message, the band's reminder: OpenTonAgent,
// src/ton-agent-entry.h). The chat's own block (assets/chat-panel/) reads the same status through src/chat-ton-agent.h.
//
// Paper « Browser Design » › « Onboarding agents — Ton agent (maquette prête) »: Claude Code 1 Choisir 22ZR-0,
// 2 Installation 2312-0, 3 Prêt 232D-0, 4 Erreur 233O-0; Codex 1 Choisir 24VS-0, 2 Installation 24X8-0, 3 À connecter
// 24YO-0, 4 Prêt 2504-0, 5 Erreur 251H-0; the note « Installation… » 253G-0. Values: design/ton-agent-screen.md.
//
//  * Two tiles, Claude Code then Codex. Under each name its mark: ⤓ Installer · 📶 Connecter · ⤓ Installation… · ✓ Prêt ·
//    ☹ Erreur. At the start NO tile is current; the target of the strong button is Claude Code unless an agent is busy,
//    ready or failed (that one, Claude first); a click on a tile makes it the target (Codex 1 Choisir).
//  * The target tile is outlined once it is acted on (cyan: installing, to connect, ready; danger: failed); not while it
//    only waits for « Installer ».
//  * ONE strong button: « Installer X », « Installation… » (dimmed), « Se connecter à X », « Ouvrir le chat »,
//    « Réessayer ». Its click IS the consent (AgentStatus::UserClicked): nothing installs before it.
//  * Under the tiles nothing, except after a failure: two lines and, for an install, « Installer moi-même ↗ ».
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

// ---- a tile ----------------------------------------------------------------------------------------------
// The mark under the name (icon + word). Unknown: the first detection runs, nothing definite (no mark).
enum class Mark { Unknown, Install, Installing, Connect, Ready, Error };
inline Mark MarkOf(const Status& s) {
    switch (s.state) {
    case State::NotInstalled: return Mark::Install;
    case State::Installing: return Mark::Installing;
    case State::NeedsSignIn: case State::SigningIn: return Mark::Connect;
    case State::Ready: return Mark::Ready;
    case State::Error: return Mark::Error;
    default: return Mark::Unknown;
    }
}
// The tile's frame: at rest (#1F1F1F, inset 1 #2A2A2A), current (#192A2A, inset 1.5 #2FD1D9), failed (danger 10 %,
// inset 1.5 danger). Only the target is ever outlined, and only once acted on.
enum class Look { Rest, Current, Danger };
inline Look LookOf(const Status& s, bool target) {
    if (!target) return Look::Rest;
    switch (s.state) {
    case State::Error: return Look::Danger;
    case State::Installing: case State::NeedsSignIn: case State::SigningIn: case State::Ready: return Look::Current;
    default: return Look::Rest;
    }
}
// The mark's colour: the profile's light ink (#2FD1D9) on the current tile, danger for an error, grey (#A6A6A6) else.
enum class Ink { Grey, Accent, Danger };
inline Ink InkOf(const Status& s, bool target) {
    if (s.state == State::Error) return Ink::Danger;
    if (s.state == State::Installing) return Ink::Accent;   // live: the morph runs in the accent wherever it is
    return LookOf(s, target) == Look::Current ? Ink::Accent : Ink::Grey;
}

// ---- the failure under the tiles -------------------------------------------------------------------------
enum class Trouble { None, Install, SignIn };
inline bool SignInReason(Reason r) { return r == Reason::SignInFailed || r == Reason::SignInTimeout; }
inline Trouble TroubleOf(const Status& s) {
    if (s.state != State::Error) return Trouble::None;
    return SignInReason(s.reason) ? Trouble::SignIn : Trouble::Install;
}
// « Installer moi-même ↗ »: after an install that did not succeed (not after a sign-in).
inline bool ManualOffered(const Status& s) { return TroubleOf(s) == Trouble::Install; }

// ---- the strong button -----------------------------------------------------------------------------------
enum class Label { Install, Installing, SignIn, OpenChat, Retry };
enum class Action { None, Install, SignIn, OpenChat, Retry };
struct Button { Label label = Label::Install; Action action = Action::None; bool dimmed = false; };
inline Button ButtonOf(const Status& s) {
    switch (s.state) {
    case State::NotInstalled: return {Label::Install, Action::Install, false};
    case State::Installing: return {Label::Installing, Action::None, true};
    case State::NeedsSignIn: return {Label::SignIn, Action::SignIn, false};
    case State::SigningIn: return {Label::SignIn, Action::None, true};      // the console is open: wait for it
    case State::Ready: return {Label::OpenChat, Action::OpenChat, false};
    case State::Error: return {Label::Retry, Action::Retry, false};
    default: return {Label::Install, Action::None, true};                   // the first detection: nothing to click yet
    }
}

// ---- the text above the footer ---------------------------------------------------------------------------
// Choose: « Choisissez votre agent » (at the start and after a failure); Installing: « Installation de X »; SignIn:
// « Connectez X » (Claude Code: its paid plan is said here); Ready: « X est prêt ».
enum class Heading { Choose, Installing, SignIn, Ready };
inline Heading HeadingOf(const Status& s) {
    switch (s.state) {
    case State::Installing: return Heading::Installing;
    case State::NeedsSignIn: case State::SigningIn: return Heading::SignIn;
    case State::Ready: return Heading::Ready;
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
// The mark's row: 14 (icon 14, text 13/13) or 17 (« Erreur » 13/17, « ✓ Prêt » 13/17).
inline float MarkHeight(Mark m) { return m == Mark::Ready || m == Mark::Error ? 17.f : 14.f; }
inline float TileHeight(Mark claude, Mark codex) { return markTop + std::max(MarkHeight(claude), MarkHeight(codex)) + tilePadBottom; }
inline float TroubleHeight(Trouble t) { return t == Trouble::None ? 0.f : troubleGap + 2 * troubleLine + (t == Trouble::Install ? troubleLinkGap + troubleLine : 0.f) + troublePadBottom; }
inline float Height(Mark claude, Mark codex, Trouble t) { return 2 * pad + TileHeight(claude, codex) + TroubleHeight(t); }
inline float Top(const Status& target, float height) {
    switch (target.state) {
    case State::Installing: return topInstalling;
    case State::Ready: return topReady;
    case State::Error: return topError;
    default: return centre - height / 2;
    }
}
}  // namespace Card

// ---- the screen's decisions for two statuses and the person's pick -----------------------------------------------
class Screen {
    std::optional<Agent> picked_;
public:
    // A click on a tile (or the strong button acting on the target): that agent stays the target.
    void Pick(Agent a) { picked_ = a; }
    void Reset() { picked_.reset(); }
    bool Picked() const { return picked_.has_value(); }
    Agent Target(const Status& claude, const Status& codex) const { return picked_ ? *picked_ : DefaultTarget(claude, codex); }
};

}  // namespace TonAgent
