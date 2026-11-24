#pragma once
// « Ton agent »: an agent's one status (src/agent-status.h) as Réglages › Agents and the annotation band's reminder read
// it (AnnotationSettings::ChatAgent, src/annotation-settings.h): no chat type reaches the drawer. Pure, header-only
// (tests/agents_settings_test.cpp checks every state). Worker C; the status itself is worker A's.
#include <string>

#include "agent-status.h"
#include "annotation-settings.h"

namespace AnnotationSettings {

inline AgentStatus::Agent ToAgent(ChatApp a) { return a == ChatApp::Codex ? AgentStatus::Agent::Codex : AgentStatus::Agent::Claude; }

inline ChatAgent FromStatus(const AgentStatus::Status& s) {
    using S = AgentStatus::State; using R = AgentStatus::Reason;
    ChatAgent a; a.version.assign(s.version.begin(), s.version.end());   // « 2.1.325 »: ASCII digits and dots
    switch (s.state) {
    case S::NotInstalled:   // DesktopOnly: « Pas installé », and the row says the Claude app is not the chat's Claude Code
        a.state = s.reason == R::TooOld ? ChatState::TooOld : ChatState::NotInstalled; a.desktopOnly = s.reason == R::DesktopOnly; break;
    case S::Installing: a.state = ChatState::Installing; break;
    case S::NeedsSignIn: a.state = ChatState::NeedsSignIn; break;
    case S::SigningIn:
        a.state = ChatState::SigningIn;
#ifdef AGENT_STATUS_CANCEL_SIGN_IN   // worker A's AgentStatus::CancelSignIn: « Annuler » works; without it, it shows dimmed
        a.cancellable = true;
#endif
        break;
    case S::Ready: a.state = ChatState::Ready; break;
    case S::Error:
        // Cancelled (the browser closed during it) is never shown: the next detection says what is there.
        a.state = s.reason == R::Cancelled ? ChatState::Unknown
                : s.reason == R::SignInFailed || s.reason == R::SignInTimeout ? ChatState::SignInFailed : ChatState::InstallFailed;
        break;
    default: a.state = ChatState::Unknown; break;
    }
    return a;
}
inline ChatAgents FromStatuses(const AgentStatus::Status& claude, const AgentStatus::Status& codex) {
    ChatAgents out; out.claude = FromStatus(claude); out.codex = FromStatus(codex); return out;
}

}  // namespace AnnotationSettings
