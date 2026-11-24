#pragma once
// « Ton agent »: what the chat shows for an agent (state.agents of the panel, the model menu, the comment bubble), read
// from the agents' one status (src/agent-status.h) — never from a detection of its own. Pure: unit-tested
// (tests/acp/agent_status_test.cpp). The texts are the chat's catalogue entries, named here by `Text`.
#include "agent-status.h"

namespace ChatAgentStatus {

enum class Text { None, NotInstalled, TooOld, NotConnected, Starting, Installing, Installed, FirstUse };
struct View {
    const char* status;   // the panel's word: "starting" | "notInstalled" | "notConnected" | "preparing" | "error" | "ready"
    Text text;
    bool cli;             // a usable CLI is installed (the chat may pick this agent by itself)
    bool known;           // a detection has answered
};

inline View Of(const AgentStatus::Status& s) {
    using S = AgentStatus::State; using R = AgentStatus::Reason;
    switch (s.state) {
    case S::Unknown: return {"starting", Text::None, false, false};
    case S::NotInstalled: return s.reason == R::TooOld ? View{"error", Text::TooOld, false, true} : View{"notInstalled", Text::NotInstalled, false, true};
    case S::Installing: return {"preparing", Text::Installing, false, true};
    case S::NeedsSignIn: return {"notConnected", Text::NotConnected, true, true};
    case S::SigningIn: return {"starting", Text::Starting, true, true};
    case S::Ready: return {"ready", s.runtimeReady ? Text::Installed : Text::FirstUse, true, true};
    case S::Error:
        // a failed sign-in: still installed, to sign in again; a failed install: still not installed (never « Installé »)
        if (s.reason == R::SignInFailed || s.reason == R::SignInTimeout) return {"notConnected", Text::NotConnected, true, true};
        return {"notInstalled", Text::NotInstalled, false, true};
    }
    return {"starting", Text::None, false, false};
}

}  // namespace ChatAgentStatus
