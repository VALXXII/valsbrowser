#pragma once
// « Ton agent » in the chat (worker B) — included at the end of src/chat-host-ui.h (namespace Chat, after the panel).
//
//  1. The block of the chat's first message when no agent is ready (Paper « Onboarding agents — Ton agent (maquette
//     prête) » › 22N3-0 « Chat — premier message, rien n'est prêt »; its rows' states as the Designer set them for Réglages ›
//     Agents: an icon and the words, no coloured dot), in the panel and in the comment bubble (the same
//     page, assets/chat-panel/panel.js): host → page `agents.setup`, page → host `agents.setup.act` / `.manual` /
//     `.open`. Its words come from the catalogue (block « agents: onboarding/chat »); its state is the agents' ONE
//     status (src/agent-status.h): every change reaches the page. A row's strong button IS the consent of its install
//     (AgentStatus::UserClicked), never anything else.
//  2. OpenTonAgent (src/ton-agent-entry.h, declared by worker C): the « Ton agent » screen alone — the tutorial's window
//     (src/annotation-onboarding.h, Options::alone) — for the band's reminder (C), the chat, Réglages. If the tutorial is
//     open, it is brought to the front instead (its sixth step is this screen).
//  3. What « Ouvrir le chat » and « Installer moi-même ↗ » do for the tutorial opened by src/annotation-mode.h (no hook
//     there): AnnotationOnboardingUI::Fallbacks(), set once here.
#include <memory>
#if __has_include("ton-agent-entry.h")
#include "ton-agent-entry.h"
#else
enum class TonAgentFrom { Band, Chat, Settings };   // worker C's declaration (src/ton-agent-entry.h), until it is merged
void OpenTonAgent(TonAgentFrom from);
#endif

namespace Chat {

std::unique_ptr<NativeChrome::AnnotationOnboardingUI::Window> tonAgentWindow;   // the screen alone
int tonAgentListener = 0;

const char* TonAgentId(AgentStatus::Agent a) { return a == AgentStatus::Agent::Codex ? "codex" : "claude"; }
bool TonAgentOf(const std::string& id, AgentStatus::Agent& a) { if (id == "claude") { a = AgentStatus::Agent::Claude; return true; } if (id == "codex") { a = AgentStatus::Agent::Codex; return true; } return false; }

// The profile's accent, as the tutorial draws it (« Personnel »: the mode's #3E67FF): the block's strong buttons.
std::string TonAgentAccent() {
    NativeChrome::AnnotationOnboardingUI::Options o; Ann::TintTutorial(o);
    const auto a = o.accent ? NativeChrome::OnboardingKit::FromHex(o.accent) : NativeChrome::OnboardingKit::Accent(o.tint);
    char b[8]; snprintf(b, sizeof b, "#%02X%02X%02X", a.r, a.g, a.b); return b;
}
// One row of the block: logo, name, the state (an icon and its words — the Designer's rule for Réglages › Agents, kept the
// same here: no coloured dot), ONE strong button on the missing action (the profile's accent), anything else neutral.
//   not installed  ⤓ « Pas installé » grey · strong « Installer » (Claude Desktop only: + its 13 px note)
//   too old        ⤓ « Version trop ancienne » grey · strong « Mettre à jour »
//   installing     ⤓ animated (the Downloads morph) « Installation… » accent · no button
//   needs sign-in  📶 « Pas connecté » grey · strong « Se connecter »
//   signing in     📶 « Connexion… » accent · neutral « Annuler » (AgentStatus::CancelSignIn: closes the sign-in console)
//   failed         ☹ « Échec de l'installation » / « Échec de la connexion » danger · neutral « Réessayer » (+ « Installer moi-même ↗ »)
//   ready          ✓ « Prêt · version 2.1 » accent (the block is gone by then: kept for completeness)
SJ::Value TonAgentRow(AgentStatus::Agent a) {
    using S = AgentStatus::State; using R = AgentStatus::Reason; const auto& s = AgentStatus::Get(a);
    SJ::Value r = SJ::Value::Obj(); r.Set("id", Str(std::string(TonAgentId(a)))); r.Set("name", Str(std::string(a == AgentStatus::Agent::Codex ? "Codex" : "Claude Code")));
    std::string icon, ink = "grey", act, label; std::wstring status, note; bool strong = true, animate = false, manual = false;
    switch (s.state) {
    case S::NotInstalled:
        icon = "download";
        if (s.reason == R::TooOld) { status = TR(ton_agent_row_too_old); label = T8(I18n::Id::ton_agent_row_update); }
        else { status = TR(ton_agent_row_not_installed); label = T8(I18n::Id::ton_agent_row_install); if (s.reason == R::DesktopOnly) note = TR(ton_agent_row_desktop_note); }
        act = "install"; break;
    case S::Installing: icon = "download"; ink = "accent"; animate = true; status = TR(ton_agent_row_installing); break;
    case S::NeedsSignIn: icon = "wifi"; status = TR(ton_agent_row_needs_signin); act = "signIn"; label = T8(I18n::Id::ton_agent_row_signin); break;
    case S::SigningIn: icon = "wifi"; ink = "accent"; status = TR(ton_agent_row_signing_in); act = "cancel"; label = T8(I18n::Id::ton_agent_row_cancel); strong = false; break;
    case S::Ready: icon = "check"; ink = "accent"; status = s.version.empty() ? std::wstring(TR(ton_agent_row_ready)) : TRF(ton_agent_row_ready_version, W(s.version)); break;
    case S::Error: {
        const bool signIn = TonAgent::SignInReason(s.reason);
        icon = "error"; ink = "danger"; status = signIn ? TR(ton_agent_row_signin_error) : TR(ton_agent_row_install_error);
        act = "retry"; label = T8(I18n::Id::ton_agent_button_retry); strong = false; manual = !signIn; break;
    }
    default: status = TR(ton_agent_row_checking); break;   // the first detection: no icon, no button
    }
    if (!icon.empty()) r.Set("icon", Str(icon));
    r.Set("ink", Str(ink)); r.Set("status", Str(status)); if (animate) r.Set("animate", SJ::Value::Bool(true));
    if (!note.empty()) r.Set("note", Str(note));
    if (manual) r.Set("manual", Str(TR(ton_agent_manual)));
    if (!act.empty()) { SJ::Value b = SJ::Value::Obj(); b.Set("id", Str(act)); b.Set("label", Str(label)); b.Set("strong", SJ::Value::Bool(strong)); r.Set("action", std::move(b)); }
    return r;
}
// Shown while no agent is ready (and once both were detected: never a flash of « Pas installé » at start).
bool TonAgentShows() {
    using S = AgentStatus::State;
    return !AgentStatus::AnyReady() && AgentStatus::Get(AgentStatus::Agent::Claude).state != S::Unknown && AgentStatus::Get(AgentStatus::Agent::Codex).state != S::Unknown;
}
void SendTonAgent();
void ListenTonAgent() {
    if (tonAgentListener) return;
    tonAgentListener = AgentStatus::Subscribe([](const AgentStatus::Status&) { SendTonAgent(); });
}
// host → page: the whole block (the page shows it on an empty chat while `show`).
void SendTonAgent() {
    ListenTonAgent();
    if (!panelReady) return;
    SJ::Value m = SJ::Value::Obj(); m.Set("show", SJ::Value::Bool(TonAgentShows()));
    m.Set("title", Str(TR(ton_agent_choose_title))); m.Set("body", Str(TR(ton_agent_chat_body))); m.Set("note", Str(TR(ton_agent_chat_paid)));
    m.Set("accent", Str(TonAgentAccent()));
    SJ::Value rows = SJ::Value::Arr(); for (auto a : AgentStatus::AllAgents) rows.Push(TonAgentRow(a)); m.Set("rows", std::move(rows));
    Send("agents.setup", std::move(m));
}
// « Installer moi-même ↗ »: the official command to the clipboard, the official page in a new tab.
void OpenOfficialPage(const std::wstring& url) { if (url.rfind(L"https://", 0) == 0) { ::NewTab(url); Diag(L"Ton agent: the official install page opened in a new tab"); } }
void InstallMyself(AgentStatus::Agent a) {
    const auto m = AgentStatus::ManualInstall(a);
    const bool copied = ::CopyToClipboard(m.command);   // main.cpp
    Ann::Announce(TR(ton_agent_manual_done)); Diag(std::wstring(L"Ton agent: « Installer moi-même » for ") + (a == AgentStatus::Agent::Codex ? L"Codex" : L"Claude Code") + (copied ? L" (command copied)" : L" (clipboard refused)"));
    OpenOfficialPage(m.url);
}
// « Ouvrir le chat » (the screen, an agent ready): that agent, in the chat — opened, or focused when it already is.
void OpenChatFor(AgentStatus::Agent a) {
    const std::string id = TonAgentId(a);
    if (AgentOf(id) && id != agentId) SwitchAgent(id, "");
    if (Open() || BubbleOpen()) { if (panelReady) { SJ::Value f = SJ::Value::Obj(); f.Set("target", Str(std::string("composer"))); Send("focus", std::move(f)); view.Focus(); } return; }
    Toggle(true);   // Ctrl+Maj+D's way: the mode first if needed, then the panel
    Diag(L"Ton agent: « Ouvrir le chat » with " + W(id));
}
// page → host. True: it was one of the block's messages.
bool TonAgentMessage(const std::string& type, const SJ::Value& v) {
    if (type.rfind("agents.setup", 0) != 0) return false;
    const Msg m{v}; AgentStatus::Agent a;
    if (type == "agents.setup.open") { OpenTonAgent(TonAgentFrom::Chat); return true; }
    if (!TonAgentOf(m.Text("agentId", 16), a)) return true;
    if (type == "agents.setup.manual") { if (TonAgent::ManualOffered(AgentStatus::Get(a))) InstallMyself(a); return true; }
    if (type == "agents.setup.act") {
        const std::string act = m.Text("action", 16); const auto& s = AgentStatus::Get(a); bool started = false;
        // the row's button: its click is the consent (install, update, retry) — the state must still ask for it
        if (act == "install" && s.state == AgentStatus::State::NotInstalled) started = AgentStatus::Install(AgentStatus::UserClicked(a));
        else if (act == "signIn" && s.state == AgentStatus::State::NeedsSignIn) started = AgentStatus::SignIn(a);
        else if (act == "retry" && s.state == AgentStatus::State::Error) started = AgentStatus::Retry(AgentStatus::UserClicked(a));
        else if (act == "cancel" && s.state == AgentStatus::State::SigningIn) started = AgentStatus::CancelSignIn(a);   // the sign-in console closes
        Diag(L"Ton agent (chat): " + W(act) + L" " + W(TonAgentId(a)) + (started ? L" started" : L" refused (state " + W(AgentStatus::StateName(s.state)) + L")"));
        SendTonAgent();
        return true;
    }
    return true;
}

// The tutorial opened by the annotation mode has no « Ouvrir le chat » / « Installer moi-même » hooks of its own: these.
const bool tonAgentFallbacks = [] {
    auto& f = NativeChrome::AnnotationOnboardingUI::Fallbacks();
    f.openChat = [](AgentStatus::Agent a) { OpenChatFor(a); };
    f.openPage = [](const std::wstring& url) { OpenOfficialPage(url); };
    return true;
}();

}  // namespace Chat

// « Ton agent » alone (src/ton-agent-entry.h): the band's reminder, the chat, Réglages. UI thread, mode on or off.
void OpenTonAgent(TonAgentFrom from) {
    namespace OUI = NativeChrome::AnnotationOnboardingUI; namespace AO = AnnotationOnboarding;
    const wchar_t* who = from == TonAgentFrom::Band ? L"the band's reminder" : from == TonAgentFrom::Settings ? L"Réglages" : L"the chat";   // i18n-exempt: log detail
    Chat::EnsureAgentStatus();
    if (Ann::OnboardingOpen()) { Ann::onboarding->Raise(); Log(std::wstring(L"TON AGENT asked by ") + who + L": the tutorial is open (its « Ton agent » step), brought to the front"); return; }
    if (Chat::tonAgentWindow && Chat::tonAgentWindow->Visible()) { Chat::tonAgentWindow->Raise(); return; }
    OUI::Options o; o.reduced = Ann::Reduced(); o.alone = true; Ann::TintTutorial(o);
    o.hooks.closed = [](AO::Ending end, bool) {
        Log(std::wstring(L"TON AGENT closed (") + (end == AO::Ending::Finished ? L"« Ouvrir le chat »" : L"later") + L")");
        if (Ann::s.active && !Chat::Open() && !Chat::BubbleOpen()) ::FocusPage();
    };
    o.hooks.openChat = [](AgentStatus::Agent a) { Chat::OpenChatFor(a); };
    o.hooks.openPage = [](const std::wstring& url) { Chat::OpenOfficialPage(url); };
    o.hooks.language = [](I18n::Locale l) { UiLanguage::Save(Ann::DataRoot(), l); };
    o.hooks.announce = [](const std::wstring& t) { Ann::Announce(t); };
    Chat::tonAgentWindow = std::make_unique<OUI::Window>();
    if (!Chat::tonAgentWindow->Open(instance, window, base, Ann::ClientScreen(), std::move(o))) { Chat::tonAgentWindow.reset(); Log(std::wstring(L"TON AGENT could not open (asked by ") + who + L")"); return; }
    Log(std::wstring(L"TON AGENT opened by ") + who);
}
