#pragma once
// Réglages › « Annotations » — the pure part (no window, no disk, no store): what each agent row
// says and offers, the confirmation texts of « Effacer toutes les annotations… », the shortcut reminder and
// the agent logos. The drawer (src/settings-panel.h) paints it; the host (main.cpp, through
// src/annotation-entry.h) feeds it the agents' state and does the work behind every callback.
#include <cstddef>
#include <string>

namespace AnnotationSettings {

enum class App { Claude = 0, Codex = 1 };
inline constexpr int AppCount = 2;
inline App AppAt(int i) { return i == 1 ? App::Codex : App::Claude; }
inline const wchar_t* AppName(App a) { return a == App::Claude ? L"Claude Desktop" : L"Codex"; }   // the row
inline const wchar_t* AppShort(App a) { return a == App::Claude ? L"Claude" : L"Codex"; }          // summaries

// What the drawer knows about one app. The host fills the detection part (same meaning as
// AnnotationAgentState in src/annotation-entry.h); the drawer owns `activity` and the last result.
enum class Activity { Idle, Connecting, Disconnecting };
struct Agent {
    bool known = false;        // the host answered at least once (until then: « Vérification… »)
    bool detected = false;     // the app's configuration folder exists
    bool configured = false;   // our entry is in its configuration (Claude: or the .mcpb extension installed)
    bool connected = false;    // configured, or it read the store recently
    bool stalePath = false;    // configured for another bridge / store path
    bool restart = false;      // configured, not read since: the app must be restarted to load the server
    App app = App::Claude;     // which app (the restart line names it)
    long long lastReadAt = 0;  // UTC seconds, 0 = never read
    Activity activity = Activity::Idle;
    bool failed = false;       // the last connect / disconnect failed: `message` says why
    static Agent For(App a) { Agent x; x.app = a; return x; }
    std::wstring message;      // last result (French, from agent-connect): an error, or a step left (« Cliquez sur Installer dans Claude »)
};
struct Agents {
    Agent claude, codex{Agent::For(App::Codex)};
    Agent& operator[](App a) { return a == App::Claude ? claude : codex; }
    const Agent& operator[](App a) const { return a == App::Claude ? claude : codex; }
};

enum class Action { None, Connect, Disconnect, Reconnect };
inline const wchar_t* ActionLabel(Action a) {
    switch (a) { case Action::Connect: return L"Connecter"; case Action::Disconnect: return L"Déconnecter"; case Action::Reconnect: return L"Reconnecter"; default: return L""; }
}
// The row's button. None: no button (not installed, connected through a setup we did not write, unknown yet).
// While a connection runs, the button stays where it is (same label) and is disabled.
inline Action ActionFor(const Agent& a) {
    if (a.activity == Activity::Connecting) return a.stalePath ? Action::Reconnect : Action::Connect;
    if (a.activity == Activity::Disconnecting) return Action::Disconnect;
    if (!a.known) return Action::None;
    if (a.stalePath) return Action::Reconnect;
    if (a.configured) return Action::Disconnect;
    if (a.connected || !a.detected) return Action::None;
    return Action::Connect;
}
inline bool ActionEnabled(const Agent& a) { return a.activity == Activity::Idle && ActionFor(a) != Action::None; }

// « à l'instant », « il y a 5 min », « il y a 2 h », « hier », « il y a 3 jours ».
inline std::wstring Ago(long long seconds) {
    if (seconds < 60) return L"à l'instant";
    if (seconds < 3600) return L"il y a " + std::to_wstring(seconds / 60) + L" min";
    if (seconds < 86400) return L"il y a " + std::to_wstring(seconds / 3600) + L" h";
    if (seconds < 2 * 86400) return L"hier";
    return L"il y a " + std::to_wstring(seconds / 86400) + L" jours";
}
// After a connection, until the app reads the store: same words as the connect modal (1.3).
inline std::wstring RestartText(App app) { return std::wstring(L"Redémarrez ") + AppShort(app) + L" pour l'activer"; }
// The status line under the app's name.
inline std::wstring StatusText(const Agent& a, long long now) {
    if (a.activity == Activity::Connecting) return L"Connexion…";
    if (a.activity == Activity::Disconnecting) return L"Déconnexion…";
    if (!a.known) return L"Vérification…";
    if (a.failed) return a.message.empty() ? std::wstring(L"La connexion a échoué. Réessayez.") : a.message;
    if (a.stalePath) return L"Connecté à un ancien emplacement du navigateur";
    if (a.connected && a.restart) return std::wstring(L"Connecté · redémarrez ") + AppShort(a.app) + L" pour l'activer";
    if (a.connected && a.lastReadAt > 0) return L"Connecté · a lu vos annotations " + Ago(now - a.lastReadAt);
    if (a.connected) return L"Connecté";
    if (!a.message.empty()) return a.message;   // e.g. the .mcpb is open: « Cliquez sur Installer dans Claude »
    if (!a.detected) return L"Non installé";
    return L"Non connecté";
}
inline bool StatusIsError(const Agent& a) { return a.activity == Activity::Idle && a.known && a.failed; }
// The root row's summary: the connected apps, « Aucun agent », or nothing until known.
inline std::wstring Summary(const Agents& agents, bool known) {
    if (!known) return L"";
    std::wstring s;
    for (int i = 0; i < AppCount; ++i) if (agents[AppAt(i)].connected) s += (s.empty() ? L"" : L", ") + std::wstring(AppShort(AppAt(i)));
    return s.empty() ? std::wstring(L"Aucun agent") : s;
}

// « Effacer toutes les annotations… »: an irreversible mass action. The confirmation names the count.
inline std::wstring ClearTitle(std::size_t n) { return n == 1 ? std::wstring(L"Effacer l'annotation ?") : L"Effacer les " + std::to_wstring(n) + L" annotations ?"; }
inline std::wstring ClearBody(std::size_t n) {
    return n == 1 ? std::wstring(L"Sa note et sa capture seront supprimées de cet ordinateur, même si un agent la traite. Impossible d'annuler.")
                  : std::wstring(L"Leurs notes et captures seront supprimées de cet ordinateur, même celles qu'un agent traite. Impossible d'annuler.");
}
inline std::wstring ClearAction(std::size_t n) { return L"Effacer " + std::to_wstring(n) + (n == 1 ? L" annotation" : L" annotations"); }

inline constexpr const wchar_t* ShortcutsNote = L"Ctrl+Maj+A lance le mode Annotation, Ctrl+Maj+H ouvre l'historique.";

// Retention menu (« Conserver les terminées »): one line each, the recommended one marked.
inline const wchar_t* RetentionNote(int days) {
    return days == 7 ? L"Elles quittent vite l'historique." : days == 30 ? L"Recommandé." : L"Pour garder un long historique.";
}

// Agent logos and the annotation mark: the exact Paper paths of src/annotation-kit.h (Glyphs::claude,
// Glyphs::openai, Glyphs::mark; Simple Icons 13.21.0, CC0). tests/annotation_settings_test.cpp checks they
// stay identical. Claude #D97757, OpenAI white.
struct Glyph { const char* d; float x, y, w, h; };
inline constexpr unsigned ClaudeRgb = 0xD97757;
inline constexpr Glyph ClaudeLogo{"M4.452 15.736l4.455-2.5 0.075-0.218-0.075-0.12h-0.218l-0.745-0.046-2.547-0.069-2.207-0.092-2.139-0.114-0.539-0.115-0.504-0.665 0.051-0.333 0.454-0.304 0.648 0.058 1.433 0.097 2.151 0.15 1.559 0.091 2.311 0.241h0.367l0.052-0.149-0.126-0.092-0.098-0.091L6.586 9.956l-2.409-1.594-1.262-0.918-0.682-0.464-0.344-0.436-0.149-0.952 0.619-0.682 0.831 0.573 0.213 0.573 0.843 0.648 1.8 1.393 2.351 1.732 0.344 0.287 0.138-0.098 0.017-0.069-0.155-0.258-1.279-2.311-1.365-2.35-0.607-0.975-0.161-0.585c-0.057-0.241-0.097-0.441-0.097-0.688L5.938 0.793 6.328 0.667l0.94 0.126 0.396 0.344 0.585 1.336 0.946 2.105 1.468 2.861 0.43 0.849 0.229 0.786 0.086 0.241h0.149v-0.138l0.121-1.611 0.223-1.979 0.218-2.546 0.075-0.717 0.355-0.86 0.705-0.464 0.551 0.264 0.453 0.648-0.631 0.418-0.269 1.749-0.528 2.741-0.344 1.835h0.201l0.229-0.23 0.929-1.232 1.559-1.949 0.688-0.774 0.803-0.855 0.516-0.407h0.974l0.717 1.066-0.321 1.102-1.004 1.273-0.831 1.077-1.193 1.606-0.746 1.284 0.069 0.104 0.178-0.018 2.694-0.573 1.457-0.264 1.737-0.298 0.786 0.367 0.86 0.373-0.31 0.762-1.858 0.459-2.178 0.436-3.246 0.768-0.401 0.029 0.459 0.057 1.462 0.138 0.625 0.034h1.531l2.851 0.212 0.745 0.493 0.447 0.603-0.746 0.459-1.146 0.585-1.548-0.367-3.613-0.861-1.238-0.309h-0.172v0.103l1.032 1.01 1.892 1.708 2.368 2.202 0.12 0.545-0.304 0.429-0.321-0.046-2.081-1.565-0.803-0.705-1.818-1.531h-0.121v0.161l0.419 0.614 2.214 3.325 0.114 1.021-0.16 0.332-0.574 0.201-0.63-0.115-1.296-1.818L13.581 17.628l-1.078-1.835-0.131 0.074-0.637 6.852-0.298 0.35-0.688 0.263-0.574-0.435-0.304-0.706 0.305-1.393 0.367-1.818 0.298-1.445 0.269-1.794 0.16-0.597-0.011-0.04-0.132 0.017-1.353 1.858-2.059 2.781-1.628 1.744-0.39 0.155-0.677-0.35 0.063-0.625 0.379-0.556 2.254-2.868 1.359-1.777 0.877-1.027-0.006-0.149h-0.051l-5.986 3.887-1.067 0.138-0.458-0.43 0.057-0.705 0.219-0.23 1.8-1.238Z", 0.667f, 1.6f, 21.333f, 21.333f};
inline constexpr Glyph OpenAiLogo{"M19.806 10.063a5.32 5.32 0 0 0-0.458-4.365 5.374 5.374 0 0 0-5.787-2.578A5.391 5.391 0 0 0 4.428 5.051a5.32 5.32 0 0 0-3.554 2.577 5.374 5.374 0 0 0 0.66 6.309 5.316 5.316 0 0 0 0.454 4.365 5.379 5.379 0 0 0 5.791 2.578A5.32 5.32 0 0 0 11.787 22.667a5.383 5.383 0 0 0 5.13-3.739 5.324 5.324 0 0 0 3.554-2.578 5.383 5.383 0 0 0-0.664-6.287z m-8.019 11.207a3.979 3.979 0 0 1-2.557-0.925l0.126-0.072 4.248-2.451a0.707 0.706 90 0 0 0.349-0.606v-5.988l1.795 1.039a0.063 0.063 0 0 1 0.034 0.046v4.963a4.004 4.004 0 0 1-3.996 3.995z m-8.588-3.666a3.974 3.974 0 0 1-0.475-2.68l0.126 0.076 4.252 2.452a0.686 0.685 0 0 0 0.694 0l5.193-2.995v2.073a0.715 0.071 0 0 1-0.295 0.055L8.658 19.067a3.999 3.999 0 0 1-5.459-1.463zM2.081 8.352a3.987 3.987 0 0 1 2.102-1.754V11.644a0.681 0.681 0 0 0 0.345 0.602l5.168 2.982-1.796 1.039a0.673 0.068 0 0 1-0.063 0l-4.293-2.478A4.004 4.004 0 0 1 2.081 8.331z m14.752 3.428L11.648 8.768 13.439 7.733a0.673 0.068 0 0 1 0.063 0l4.294 2.481a3.995 3.995 0 0 1-0.602 7.204v-5.046a0.702 0.702 0 0 0-0.361-0.593z m1.788-2.688l-0.127-0.075-4.243-2.473a0.69 0.69 90 0 0-0.698 0L8.364 9.538V7.464a0.588 0.059 0 0 1 0.025-0.055l4.293-2.477a3.999 3.999 0 0 1 5.938 4.142zM7.383 12.767l-1.795-1.035a0.715 0.071 0 0 1-0.034-0.05V6.732a3.999 3.999 0 0 1 6.556-3.07l-0.126 0.072L7.737 6.186a0.707 0.706 90 0 0-0.349 0.605z m0.976-2.102l2.313-1.333 2.317 1.333v2.666l-2.308 1.333-2.317-1.333Z", 0, 1.6f, 21.333f, 21.333f};
inline constexpr Glyph Mark{"M46.008 256.187L46.082 241.973C46.085 241.297 46.668 240.757 47.376 240.774L63.371 240.829C66.248 240.899 67.033 244.643 64.407 245.765L55.985 248.654C55.284 248.954 54.691 249.439 54.276 250.053 54.225 250.127 54.185 250.207 54.149 250.288L51.029 257.525C49.617 259.778 45.994 258.813 46.008 256.187Z", 46.0f, 240.756f, 20, 18.862f};
inline const Glyph& Logo(App a) { return a == App::Claude ? ClaudeLogo : OpenAiLogo; }

}  // namespace AnnotationSettings
