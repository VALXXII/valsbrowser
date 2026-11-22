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
#include <utility>
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
// The effort a new session gets: the user's choice when the agent offers it, else Medium when offered ("" = the agent's own).
template <class Choice> std::string EffortForNewSession(const std::vector<Choice>& choices, const std::string& chosen) {
    if (const auto* r = Find(choices, chosen); r && !IsAlias(*r)) return r->value;
    if (const auto* m = Find(choices, MediumEffort)) return m->value;
    return "";
}
template <class Choice> std::string EffortToApply(const std::vector<Choice>& choices, const std::string& current, const std::string& remembered) {
    const std::string want = EffortForNewSession(choices, remembered);
    return want == current ? std::string() : want;
}
// The model a new session gets: the user's choice when the agent still offers it ("" = the agent's own default).
template <class Choice> std::string ModelForNewSession(const std::vector<Choice>& choices, const std::string& chosen) {
    const auto* r = Find(choices, chosen); return r && !IsAlias(*r) ? r->value : std::string();
}

// D6: a turn's header (« Sonnet 5.5 · Moyen ») is kept with the turn. A new conversation starts its session WITH its first
// turn: the model and the effort are not known yet when the turn starts. They are filled in once known (first text, end
// of the turn); a value already kept is never replaced (the turn was run with it).
inline bool FillTurnLabel(std::string& kept, const std::string& now) { if (!kept.empty() || now.empty()) return false; kept = now; return true; }
// …and while the turn runs, the session's own announcement wins over the composer's guess (coordinator, 19 Nov 2026).
inline bool CorrectTurnLabel(std::string& kept, const std::string& now) { if (now.empty() || now == kept) return false; kept = now; return true; }

// R28: until the browser has applied its choices to a new session (model, effort, mode), the chat shows what will be
// applied — the composer is right and stable from its first image; the session's own announcement shows afterwards.
inline std::string Intended(const std::string& current, const std::string& wanted, bool applied) { return applied || wanted.empty() ? current : wanted; }
// R46 (test réel: pasting text, the mode button showed « Auto » an instant): a value asked to the session (the browser's
// mode right after the start, or the user's choice) is shown until the agent confirms it — never the session's starting
// value in between. Patience: an agent that never confirms (refused) shows its real value again after `patienceMs`.
struct Asked { std::string value; double at = 0; };
// E2 / M9 (coordinator, review R): the model, the effort and the mode the user chose are remembered per site and given to
// every new session — a new element included; « Demander » is only the start while nothing was chosen.
inline std::string ChoiceForNewSession(const std::string& justChosen, const std::string& remembered) { return justChosen.empty() ? remembered : justChosen; }
// M (Q's monkey 0.3.561, seed 1 #78-#81: « Auto » chosen in the panel while another element's turn waited, then every bubble
// and the panel showed « Demander »): what the composer shows for the mode, the model and the effort.
//   own     the conversation shown is the one the live session works for (false: shown « aside » — another element's turn runs)
//   owed    a choice made aside waits for the end of that session's turn (E3: a running turn is never touched)
//   session what that session says (R28 / R46 applied); wanted: what a new session gets (the user's choice, else the start value)
// A conversation shown aside has no session yet: it shows what ITS session will get, never the other element's session; and
// the session a choice is owed to shows that choice (it gets it when its turn ends) — nothing changes without the user.
inline std::string ShownSetting(bool own, bool owed, const std::string& session, const std::string& wanted) {
    return (own && !owed) || wanted.empty() ? session : wanted;
}
inline std::string ShownWhileAsked(const std::string& current, Asked& asked, double now, double patienceMs = 3000) {
    if (asked.value.empty()) return current;
    if (current == asked.value || now - asked.at > patienceMs) { asked = Asked{}; return current; }
    return asked.value;
}

// R4 (Paper 14RK-0, updated by the user): the mode menu has exactly four lines, in this order — « Accept edits » (the
// agent's file-edits mode: Claude acceptEdits, Codex workspace-write), « Auto » (the mode where the agent decides its
// permissions itself: Claude auto, Codex agent), « Demander », « Planifier ». The mode that bypasses every permission is
// never offered (the session's ModeMap::never already drops it); any other mode the adapter adds is not shown either.
// A line the adapter does not have is simply absent. `kind`: the host's classification ("ask", "auto", "plan", …).
enum class ModeSlot { Edits = 0, Auto = 1, Ask = 2, Plan = 3, Hidden = 4 };
inline ModeSlot SlotOf(const std::string& kind, const std::string& id) {
    if (kind == "ask") return ModeSlot::Ask;
    if (kind == "plan") return ModeSlot::Plan;
    if (kind == "auto") return id == "auto" || id == "agent" ? ModeSlot::Auto : ModeSlot::Edits;
    return ModeSlot::Hidden;
}
// The kind the page receives (contract B §20): "edits", "auto", "ask", "plan".
inline const char* SlotKind(ModeSlot s) { switch (s) { case ModeSlot::Edits: return "edits"; case ModeSlot::Auto: return "auto"; case ModeSlot::Ask: return "ask"; case ModeSlot::Plan: return "plan"; default: return "other"; } }
// The indices of `modes` (pairs kind, id) to show, in Paper's order, one per line (the first of a line wins).
inline std::vector<std::size_t> PaperModes(const std::vector<std::pair<std::string, std::string>>& modes) {
    std::vector<std::size_t> out; bool taken[4] = {false, false, false, false};
    for (int slot = 0; slot < 4; ++slot)
        for (std::size_t i = 0; i < modes.size(); ++i) {
            const auto s = SlotOf(modes[i].first, modes[i].second);
            if (int(s) == slot && !taken[slot]) { taken[slot] = true; out.push_back(i); }
        }
    return out;
}
} // namespace ChatOptions
