#pragma once
// Chat intégré — the agent's model and effort lists as the chat shows them (design/chat-integration.md, « Modèle et
// effort »). Pure: no Win32, no agent; tests/chat/chat_units_test.cpp.
//
// The agents announce their lists (ACP configOptions); nothing is hard-coded per agent. Two rules of V1.2:
//   - an entry that only says « use the agent's default » (Claude: effort "default" / "Default", model "default" /
//     "Default (recommended)") is not a level nor a model: it is never shown. The model it stands for (named in its
//     description: "Opus 5.5") carries the « Par défaut » badge instead;
//   - a new session gets the effort the user chose last for this site, else Medium, set by the browser right after the
//     session starts (like the « Demander » mode), when the agent offers it.
#include <cctype>
#include <string>
#include <vector>

namespace ChatOptions {
inline std::string Lower(std::string s) { for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c))); return s; }
inline std::string Trim(const std::string& s) {
    std::size_t a = 0, b = s.size(); while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a; while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}
inline constexpr const char* MediumEffort = "medium";   // the effort of a session the user never tuned (decision V1.2)

// « Use my default »: the entry an agent puts in front of its real choices.
inline bool IsAlias(const std::string& value, const std::string& name) {
    const std::string v = Lower(Trim(value)), n = Lower(Trim(name));
    return v == "default" || n == "default" || n.rfind("default (", 0) == 0;
}
template <class Choice> bool IsAlias(const Choice& c) { return IsAlias(c.value, c.name); }
template <class Choice> const Choice* Find(const std::vector<Choice>& choices, const std::string& value) {
    for (const auto& c : choices) if (c.value == value) return &c;
    return nullptr;
}
// The model the agent uses when told nothing: the real model its alias names (by name, or by value), else `fallback`
// (the model the agent had chosen itself when its first session started).
template <class Choice> std::string DefaultModel(const std::vector<Choice>& choices, const std::string& fallback) {
    for (const auto& a : choices) {
        if (!IsAlias(a)) continue;
        const std::string want = Lower(Trim(a.description));
        if (want.empty()) continue;
        for (const auto& c : choices) if (!IsAlias(c) && (Lower(Trim(c.name)) == want || Lower(c.value) == want)) return c.value;
    }
    if (const auto* f = Find(choices, fallback); f && !IsAlias(*f)) return f->value;
    return "";
}
// What the chat shows as the current choice: an alias is shown as the model it stands for (efforts: nothing).
template <class Choice> std::string Shown(const std::vector<Choice>& choices, const std::string& current, const std::string& fallback = "") {
    const auto* c = Find(choices, current);
    if (!c) return current;
    if (!IsAlias(*c)) return current;
    return DefaultModel(choices, fallback);
}
// The effort to set when a session starts: the site's remembered choice when the agent offers it, else Medium when
// offered; "" when nothing has to change (already set, or the agent offers neither).
template <class Choice> std::string EffortToApply(const std::vector<Choice>& choices, const std::string& current, const std::string& remembered) {
    std::string want;
    if (const auto* r = Find(choices, remembered); r && !IsAlias(*r)) want = r->value;
    else if (const auto* m = Find(choices, MediumEffort)) want = m->value;
    return want == current ? std::string() : want;
}
} // namespace ChatOptions
