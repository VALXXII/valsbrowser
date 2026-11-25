#pragma once
#include "i18n.h"
// Réglages › « Annotations » and « Agents » — the pure part (no window, no disk, no store): what each chat agent row
// says and offers (« Ton agent », Paper « Onboarding agents — Ton agent » › « Réglages › Agents »), the band's reminder
// rule, the confirmation texts of « Effacer toutes les annotations… », the shortcut reminder and the agent logos. The
// drawer (src/settings-panel.h) paints it; the host (main.cpp, through src/annotation-entry.h) feeds it the agents'
// status and does the work behind every callback.
#include <cstddef>
#include <string>

namespace AnnotationSettings {

// ---------------------------------------------------------------- Réglages › Agents: the chat's agents
// One source of truth: src/agent-status.h (design/ton-agent-interface.md). The host flattens each AgentStatus::Status
// into a ChatAgent (src/chat-agents-view.h), so the drawer needs no chat type. « Connecter un agent » (external MCP
// apps: Claude Desktop, Codex) is no longer a row here: it is the advanced option « Connecter un agent externe ».
enum class ChatApp { ClaudeCode = 0, Codex = 1 };
inline constexpr int ChatAppCount = 2;
inline ChatApp ChatAppAt(int i) { return i == 1 ? ChatApp::Codex : ChatApp::ClaudeCode; }
inline const wchar_t* ChatAppName(ChatApp a) { return a == ChatApp::ClaudeCode ? TR(agents_brand_claude_code) : TR(brand_codex); }

enum class ChatState {
    Unknown,        // first detection running: « Vérification… », no icon, no button
    NotInstalled,   // no usable CLI: ⤓ « Pas installé » + Installer (Claude Desktop alone: + the note why)
    TooOld,         // a CLI older than the chat supports: ⤓ « Version trop ancienne » + Mettre à jour (an install)
    Installing,     // the official installer runs: ⤓ (the Downloads morph) « Installation… » in the accent, no button
    NeedsSignIn,    // installed, signed out: wifi « Pas connecté » + Se connecter
    SigningIn,      // the sign-in console is open: wifi « Connexion… » in the accent + Annuler (neutral)
    Ready,          // ✓ « Prêt · version 2.1 » in the accent, no button
    InstallFailed,  // ☹ « Échec de l'installation » (danger) + Réessayer (neutral) + « Installer moi-même ↗ »
    SignInFailed,   // ☹ « Échec de la connexion » (danger) + Réessayer (neutral)
};
struct ChatAgent {
    ChatState state = ChatState::Unknown;
    std::wstring version;       // as the CLI said it (« 2.1.325 »)
    bool desktopOnly = false;   // NotInstalled because only the Claude app is there: the row says why
    bool cancellable = false;   // SigningIn and the host can stop it (AgentStatus::CancelSignIn): « Annuler » enabled
};
struct ChatAgents {
    ChatAgent claude, codex;
    ChatAgent& operator[](ChatApp a) { return a == ChatApp::ClaudeCode ? claude : codex; }
    const ChatAgent& operator[](ChatApp a) const { return a == ChatApp::ClaudeCode ? claude : codex; }
};

// The row's button (Designer, « Ton agent » V1): ONE strong button per row, on the missing action, in the profile
// accent (Installer, Mettre à jour, Se connecter: their click is the consent); Annuler and Réessayer are neutral; no
// button while an install runs, when ready, or while detecting. ManualInstall is the error row's link, not a button.
enum class ChatAction { None, Install, Update, SignIn, Cancel, Retry, ManualInstall };
inline ChatAction ChatActionFor(const ChatAgent& a) {
    switch (a.state) {
    case ChatState::NotInstalled: return ChatAction::Install;
    case ChatState::TooOld: return ChatAction::Update;
    case ChatState::NeedsSignIn: return ChatAction::SignIn;
    case ChatState::SigningIn: return ChatAction::Cancel;
    case ChatState::InstallFailed: case ChatState::SignInFailed: return ChatAction::Retry;
    default: return ChatAction::None;
    }
}
inline bool ChatActionEnabled(const ChatAgent& a) { const auto x = ChatActionFor(a); return x != ChatAction::None && (x != ChatAction::Cancel || a.cancellable); }
inline bool ChatActionStrong(const ChatAgent& a) { const auto x = ChatActionFor(a); return x == ChatAction::Install || x == ChatAction::Update || x == ChatAction::SignIn; }
inline const wchar_t* ChatActionLabel(ChatAction x) {
    switch (x) {
    case ChatAction::Install: return TR(agents_action_install); case ChatAction::Update: return TR(agents_action_update);
    case ChatAction::SignIn: return TR(agents_action_sign_in); case ChatAction::Cancel: return TR(agents_action_cancel);
    case ChatAction::Retry: return TR(agents_action_retry); case ChatAction::ManualInstall: return TR(agents_manual_install);
    default: return L"";
    }
}
// The state under the name (the V1 tiles' icons, Phosphor Bold): ⤓ download, wifi, ✓ check, ☹ smiley-x-eyes.
enum class ChatIcon { None, Download, Wifi, Check, Error };
inline ChatIcon ChatIconFor(const ChatAgent& a) {
    switch (a.state) {
    case ChatState::NotInstalled: case ChatState::TooOld: case ChatState::Installing: return ChatIcon::Download;
    case ChatState::NeedsSignIn: case ChatState::SigningIn: return ChatIcon::Wifi;
    case ChatState::Ready: return ChatIcon::Check;
    case ChatState::InstallFailed: case ChatState::SignInFailed: return ChatIcon::Error;
    default: return ChatIcon::None;
    }
}
// Installing: the download icon moves with the Downloads button's own morph (reduced motion: still).
inline bool ChatIconAnimated(const ChatAgent& a) { return a.state == ChatState::Installing; }
// Its ink: what is under way or done in the profile accent, a failure in danger, the rest #A6A6A6.
enum class ChatTone { Muted, Accent, Danger };
inline ChatTone ChatToneFor(const ChatAgent& a) {
    switch (a.state) {
    case ChatState::Installing: case ChatState::SigningIn: case ChatState::Ready: return ChatTone::Accent;
    case ChatState::InstallFailed: case ChatState::SignInFailed: return ChatTone::Danger;
    default: return ChatTone::Muted;
    }
}
// « 2.1.325 » → « 2.1 »: major.minor, what a person recognises.
inline std::wstring ShortVersion(const std::wstring& v) {
    const auto first = v.find(L'.'); if (first == std::wstring::npos) return v;
    const auto second = v.find(L'.', first + 1); return second == std::wstring::npos ? v : v.substr(0, second);
}
// The state's words.
inline std::wstring ChatStatusText(const ChatAgent& a) {
    switch (a.state) {
    case ChatState::NotInstalled: return TR(agents_status_not_installed);
    case ChatState::TooOld: return TR(agents_status_too_old);
    case ChatState::Installing: return TR(agents_status_installing);
    case ChatState::NeedsSignIn: return TR(agents_status_signed_out);
    case ChatState::SigningIn: return TR(agents_status_signing_in);
    case ChatState::Ready: return a.version.empty() ? std::wstring(TR(agents_ready)) : TRF(agents_status_ready_version, ShortVersion(a.version));
    case ChatState::InstallFailed: return TR(agents_status_install_failed);
    case ChatState::SignInFailed: return TR(agents_status_sign_in_failed);
    default: return TR(agents_status_checking);
    }
}
// Under the state (13 px #8C8C8C): only when the Claude app alone is installed — its Claude Code is not the chat's.
inline std::wstring ChatNote(const ChatAgent& a) { return a.state == ChatState::NotInstalled && a.desktopOnly ? std::wstring(TR(agents_status_desktop_only)) : std::wstring(); }
// The failed install's way out: « Installer moi-même ↗ » (the official page), under the state.
inline bool ChatManualLink(const ChatAgent& a) { return a.state == ChatState::InstallFailed; }
inline bool ChatStatusIsError(const ChatAgent& a) { return ChatToneFor(a) == ChatTone::Danger; }
inline bool ChatKnown(const ChatAgents& agents) { return agents.claude.state != ChatState::Unknown && agents.codex.state != ChatState::Unknown; }
// The root row's summary: the ready agents (« Claude Code », « Claude Code, Codex »), « Aucun agent prêt » once both are
// known, nothing while the first detection runs.
inline std::wstring ChatSummary(const ChatAgents& agents) {
    std::wstring s;
    for (int i = 0; i < ChatAppCount; ++i) if (agents[ChatAppAt(i)].state == ChatState::Ready) s += (s.empty() ? L"" : L", ") + std::wstring(ChatAppName(ChatAppAt(i)));
    if (!s.empty()) return s;
    return ChatKnown(agents) ? std::wstring(TR(agents_settings_none_ready)) : std::wstring();
}
// The annotation band's reminder (« Aucun agent connecté · Connecter », Paper 1.8) is the CHAT's since « Ton agent »: it
// shows while no chat agent is ready — once both are detected (no flash while the first detection runs) — and steps back
// while the chat panel is open (its own « Choisissez votre agent » block says it). Its click opens « Ton agent ».
inline bool BandReminder(const ChatAgents& agents, bool chatOpen) {
    if (chatOpen) return false;
    for (int i = 0; i < ChatAppCount; ++i) if (agents[ChatAppAt(i)].state == ChatState::Ready) return false;
    return ChatKnown(agents);
}
// Paper « Réglages › Agents » and Designer's answer (2026-11-24): the exact colours. The accent is the profile's
// (Design::Accent, read per paint).
namespace ChatInk {
inline constexpr unsigned name = 0xFFFFFF, status = 0xA6A6A6, note = 0x8C8C8C, danger = 0xFF7B6E, link = 0xE6E6E6;
inline constexpr unsigned neutral = 0x2A2A2A, tile = 0x262626;
inline constexpr float nameAlpha = .70f, noteAlpha = .50f;   // #FFFFFFB3, #FFFFFF80
}
// The state icons (Phosphor Bold 2.1.1, 256 units; the V1 tiles draw the same paths in a 112 box).
namespace ChatGlyphs {
inline constexpr const char* download = "M228,144v64a12,12,0,0,1-12,12H40a12,12,0,0,1-12-12V144a12,12,0,0,1,24,0v52H204V144a12,12,0,0,1,24,0Zm-108.49,8.49a12,12,0,0,0,17,0l40-40a12,12,0,0,0-17-17L140,115V32a12,12,0,0,0-24,0v83L96.49,95.51a12,12,0,0,0-17,17Z";
inline constexpr const char* wifi = "M144,204a16,16,0,1,1-16-16A16,16,0,0,1,144,204ZM239.61,83.91a176,176,0,0,0-223.22,0,12,12,0,1,0,15.23,18.55,152,152,0,0,1,192.76,0,12,12,0,1,0,15.23-18.55Zm-32.16,35.73a128,128,0,0,0-158.9,0,12,12,0,0,0,14.9,18.81,104,104,0,0,1,129.1,0,12,12,0,0,0,14.9-18.81ZM175.07,155.3a80.05,80.05,0,0,0-94.14,0,12,12,0,0,0,14.14,19.4,56,56,0,0,1,65.86,0,12,12,0,1,0,14.14-19.4Z";
inline constexpr const char* error = "M128,20A108,108,0,1,0,236,128,108.12,108.12,0,0,0,128,20Zm0,192a84,84,0,1,1,84-84A84.09,84.09,0,0,1,128,212Zm56.49-92.49a12,12,0,0,1-17,17L162,131l-5.51,5.52a12,12,0,0,1-17-17L145,114l-5.52-5.51a12,12,0,0,1,17-17L162,97l5.51-5.52a12,12,0,0,1,17,17L179,114Zm-68,17a12,12,0,0,1-17,0L94,131l-5.51,5.52a12,12,0,0,1-17-17L77,114l-5.52-5.51a12,12,0,0,1,17-17L94,97l5.51-5.52a12,12,0,0,1,17,17L111,114l5.52,5.51A12,12,0,0,1,116.49,136.49ZM144,180a16,16,0,1,1-16-16A16,16,0,0,1,144,180Z";
inline constexpr const char* check = "M232.49,80.49l-128,128a12,12,0,0,1-17,0l-56-56a12,12,0,1,1,17-17L96,183,215.51,63.51a12,12,0,0,1,17,17Z";
inline const char* For(ChatIcon icon) { return icon == ChatIcon::Download ? download : icon == ChatIcon::Wifi ? wifi : icon == ChatIcon::Check ? check : icon == ChatIcon::Error ? error : nullptr; }
}

// « Effacer toutes les annotations… »: an irreversible mass action. The confirmation names the count.
inline std::wstring ClearTitle(std::size_t n) { return n == 1 ? std::wstring(TR(annotations_clear_title_single)) : TRF(annotations_clear_title_many, std::to_wstring(n)); }
inline std::wstring ClearBody(std::size_t n) {
    return n == 1 ? std::wstring(TR(annotations_clear_body_single)) : std::wstring(TR(annotations_clear_body_many));
}
inline std::wstring ClearAction(std::size_t n) { return TRP(annotations_clear_action, n); }

inline const wchar_t* ShortcutsNote() { return TR(annotations_shortcuts_note); }


// Agent logos and the annotation mark: the exact Paper paths of src/annotation-kit.h (Glyphs::claude,
// Glyphs::openai, Glyphs::mark; Simple Icons 13.21.0, CC0). tests/annotation_settings_test.cpp checks they
// stay identical. Claude #D97757, OpenAI white.
struct Glyph { const char* d; float x, y, w, h; };
inline constexpr unsigned ClaudeRgb = 0xD97757;
inline constexpr Glyph ClaudeLogo{"M4.452 15.736l4.455-2.5 0.075-0.218-0.075-0.12h-0.218l-0.745-0.046-2.547-0.069-2.207-0.092-2.139-0.114-0.539-0.115-0.504-0.665 0.051-0.333 0.454-0.304 0.648 0.058 1.433 0.097 2.151 0.15 1.559 0.091 2.311 0.241h0.367l0.052-0.149-0.126-0.092-0.098-0.091L6.586 9.956l-2.409-1.594-1.262-0.918-0.682-0.464-0.344-0.436-0.149-0.952 0.619-0.682 0.831 0.573 0.213 0.573 0.843 0.648 1.8 1.393 2.351 1.732 0.344 0.287 0.138-0.098 0.017-0.069-0.155-0.258-1.279-2.311-1.365-2.35-0.607-0.975-0.161-0.585c-0.057-0.241-0.097-0.441-0.097-0.688L5.938 0.793 6.328 0.667l0.94 0.126 0.396 0.344 0.585 1.336 0.946 2.105 1.468 2.861 0.43 0.849 0.229 0.786 0.086 0.241h0.149v-0.138l0.121-1.611 0.223-1.979 0.218-2.546 0.075-0.717 0.355-0.86 0.705-0.464 0.551 0.264 0.453 0.648-0.631 0.418-0.269 1.749-0.528 2.741-0.344 1.835h0.201l0.229-0.23 0.929-1.232 1.559-1.949 0.688-0.774 0.803-0.855 0.516-0.407h0.974l0.717 1.066-0.321 1.102-1.004 1.273-0.831 1.077-1.193 1.606-0.746 1.284 0.069 0.104 0.178-0.018 2.694-0.573 1.457-0.264 1.737-0.298 0.786 0.367 0.86 0.373-0.31 0.762-1.858 0.459-2.178 0.436-3.246 0.768-0.401 0.029 0.459 0.057 1.462 0.138 0.625 0.034h1.531l2.851 0.212 0.745 0.493 0.447 0.603-0.746 0.459-1.146 0.585-1.548-0.367-3.613-0.861-1.238-0.309h-0.172v0.103l1.032 1.01 1.892 1.708 2.368 2.202 0.12 0.545-0.304 0.429-0.321-0.046-2.081-1.565-0.803-0.705-1.818-1.531h-0.121v0.161l0.419 0.614 2.214 3.325 0.114 1.021-0.16 0.332-0.574 0.201-0.63-0.115-1.296-1.818L13.581 17.628l-1.078-1.835-0.131 0.074-0.637 6.852-0.298 0.35-0.688 0.263-0.574-0.435-0.304-0.706 0.305-1.393 0.367-1.818 0.298-1.445 0.269-1.794 0.16-0.597-0.011-0.04-0.132 0.017-1.353 1.858-2.059 2.781-1.628 1.744-0.39 0.155-0.677-0.35 0.063-0.625 0.379-0.556 2.254-2.868 1.359-1.777 0.877-1.027-0.006-0.149h-0.051l-5.986 3.887-1.067 0.138-0.458-0.43 0.057-0.705 0.219-0.23 1.8-1.238Z", 0.667f, 1.6f, 21.333f, 21.333f};
inline constexpr Glyph OpenAiLogo{"M19.806 10.063a5.32 5.32 0 0 0-0.458-4.365 5.374 5.374 0 0 0-5.787-2.578A5.391 5.391 0 0 0 4.428 5.051a5.32 5.32 0 0 0-3.554 2.577 5.374 5.374 0 0 0 0.66 6.309 5.316 5.316 0 0 0 0.454 4.365 5.379 5.379 0 0 0 5.791 2.578A5.32 5.32 0 0 0 11.787 22.667a5.383 5.383 0 0 0 5.13-3.739 5.324 5.324 0 0 0 3.554-2.578 5.383 5.383 0 0 0-0.664-6.287z m-8.019 11.207a3.979 3.979 0 0 1-2.557-0.925l0.126-0.072 4.248-2.451a0.707 0.706 90 0 0 0.349-0.606v-5.988l1.795 1.039a0.063 0.063 0 0 1 0.034 0.046v4.963a4.004 4.004 0 0 1-3.996 3.995z m-8.588-3.666a3.974 3.974 0 0 1-0.475-2.68l0.126 0.076 4.252 2.452a0.686 0.685 0 0 0 0.694 0l5.193-2.995v2.073a0.715 0.071 0 0 1-0.295 0.055L8.658 19.067a3.999 3.999 0 0 1-5.459-1.463zM2.081 8.352a3.987 3.987 0 0 1 2.102-1.754V11.644a0.681 0.681 0 0 0 0.345 0.602l5.168 2.982-1.796 1.039a0.673 0.068 0 0 1-0.063 0l-4.293-2.478A4.004 4.004 0 0 1 2.081 8.331z m14.752 3.428L11.648 8.768 13.439 7.733a0.673 0.068 0 0 1 0.063 0l4.294 2.481a3.995 3.995 0 0 1-0.602 7.204v-5.046a0.702 0.702 0 0 0-0.361-0.593z m1.788-2.688l-0.127-0.075-4.243-2.473a0.69 0.69 90 0 0-0.698 0L8.364 9.538V7.464a0.588 0.059 0 0 1 0.025-0.055l4.293-2.477a3.999 3.999 0 0 1 5.938 4.142zM7.383 12.767l-1.795-1.035a0.715 0.071 0 0 1-0.034-0.05V6.732a3.999 3.999 0 0 1 6.556-3.07l-0.126 0.072L7.737 6.186a0.707 0.706 90 0 0-0.349 0.605z m0.976-2.102l2.313-1.333 2.317 1.333v2.666l-2.308 1.333-2.317-1.333Z", 0, 1.6f, 21.333f, 21.333f};
inline constexpr Glyph Mark{"M46.008 256.187L46.082 241.973C46.085 241.297 46.668 240.757 47.376 240.774L63.371 240.829C66.248 240.899 67.033 244.643 64.407 245.765L55.985 248.654C55.284 248.954 54.691 249.439 54.276 250.053 54.225 250.127 54.185 250.207 54.149 250.288L51.029 257.525C49.617 259.778 45.994 258.813 46.008 256.187Z", 46.0f, 240.756f, 20, 18.862f};
inline const Glyph& Logo(ChatApp a) { return a == ChatApp::ClaudeCode ? ClaudeLogo : OpenAiLogo; }   // Claude Code: the Claude mark; Codex: OpenAI's

}  // namespace AnnotationSettings
