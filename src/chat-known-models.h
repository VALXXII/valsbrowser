#pragma once
// Chat intégré — #128 v2 (Val, 24 Nov 2026: « jamais Modèle par défaut »): the models an agent is known to offer, shown
// while the agent has not said its own list yet (a fresh profile, the first seconds of the background read).
//
// Order of trust, for each agent: what the agent itself said (a session of it, or the background read of
// chat-model-probe.h) > the list cached from its last session or read (R13) > this built-in list. The built-in one is only
// a placeholder: an id picked from it is the real adapter's own id (it goes to the session when it starts, as any choice
// made before a session: `pendingModel` → ChatOptions::ModelForNewSession, which drops an id the live list does not
// have), and the agent's own list replaces it the moment it is known.
//
// Source — never typed from memory: the options the REAL adapters answered at session/new (tests/acp/run_tests.py --real,
// real_adapter_probe, no prompt), 17 Nov 2026, with the versions pinned in src/agent-runtime-pins.inc: claude-agent-acp
// 0.107.3 (Claude Code CLI 2.1.326) and codex-acp 2.6.0 (@openai/codex 0.166.0). Kept verbatim in
// tests/chat/fixtures/real-adapter-models-2026-11-17.json; tests/acp/model_probe_test.cpp checks this file against it.
// Update both when the pins move (the cache and the live read correct a stale entry on their own meanwhile).
#include "acp-client.h"
#include <string>
#include <vector>

namespace ChatKnownModels {

inline Acp::ConfigOption Make(std::string currentValue, std::vector<Acp::ConfigChoice> choices) {
    Acp::ConfigOption o; o.id = "model"; o.name = "Model"; o.category = "model"; o.type = "select";
    o.currentValue = std::move(currentValue); o.choices = std::move(choices); return o;
}
// The agent's id (src/chat-host.h `Agents`) → its last known model option; nullptr for an agent with none.
inline const Acp::ConfigOption* For(const std::string& agentId) {
    static const Acp::ConfigOption claude = Make("default", {   // claude-agent-acp 0.107.3: « default » is its alias for the recommended model
        {"default", "Default (recommended)", "Opus 5.5", ""},
        {"opus", "Opus 5.5", "For complex work and everyday tasks", ""},
        {"fable", "Fable 5.2", "For your toughest challenges", ""},
        {"sonnet", "Sonnet 5.5", "Most efficient for simpler tasks", ""},
        {"haiku", "Haiku 5.5", "Fastest for quick answers", ""},
        {"claude-fable-5-1", "Fable 5.1", "Most capable for your hardest and longest-running tasks", ""},
        {"claude-haiku-4-5-20251001", "Haiku 4.5", "Fastest for quick answers", ""},
        {"claude-sonnet-5", "Sonnet 5", "Efficient for routine tasks", ""},
        {"claude-opus-5", "Opus 5", "Best for everyday, complex tasks", ""},
        {"claude-fable-5", "Fable 5", "Most capable for your hardest and longest-running tasks", ""},
        {"claude-opus-4-8", "Opus 4.8", "Best for everyday, complex tasks", ""},
        {"claude-opus-4-7", "Opus 4.7", "Best for everyday, complex tasks", ""},
        {"claude-opus-4-6", "Opus 4.6", "Best for everyday, complex tasks", ""},
        {"claude-sonnet-4-6", "Sonnet 4.6", "Efficient for routine tasks", ""},
    });
    static const Acp::ConfigOption codex = Make("gpt-6.2-sol", {   // codex-acp 2.6.0 (model/list of @openai/codex 0.166.0): its recommended model first
        {"gpt-6.2-sol", "6.2 Sol", "Fast, go-to model for coding and everyday work.", ""},
        {"gpt-6.2-astra", "6.2 Astra", "Our most capable model for complex coding and professional work.", ""},
        {"gpt-6.1-terra", "6.1 Terra", "Efficient model that excels at visual understanding and computer use.", ""},
        {"gpt-6.1-sol", "6.1 Sol", "Previous generation model for coding and everyday work.", ""},
        {"gpt-6-astra", "6 Astra", "Previous generation model for complex coding and professional work.", ""},
        {"gpt-6-sol", "6 Sol", "Legacy model for coding and everyday work.", ""},
        {"gpt-6-luna", "6 Luna", "Fast and affordable model for easier tasks.", ""},
        {"gpt-5.6-sol", "5.6 Sol", "Legacy model for coding and everyday work.", ""},
        {"gpt-5.6-terra", "5.6 Terra", "Legacy balanced model for agentic coding.", ""},
        {"gpt-5.6-luna", "5.6 Luna", "Legacy fast model for simpler tasks.", ""},
    });
    if (agentId == "claude") return &claude;
    if (agentId == "codex") return &codex;
    return nullptr;
}
inline bool IsBuiltIn(const std::string& agentId, const Acp::ConfigOption* o) { return o && o == For(agentId); }
// The model option to show for an agent: its own (a session, the background read, or the R13 cache — all in `options`),
// else the built-in one while the agent is installed or not detected yet (never for an agent that is not installed: its
// group then shows its header only).
inline const Acp::ConfigOption* Shown(const std::string& agentId, const std::vector<Acp::ConfigOption>& options, bool detected, bool installed) {
    if (const auto* own = Acp::FindModelOption(options)) return own;
    return !detected || installed ? For(agentId) : nullptr;
}

}  // namespace ChatKnownModels
