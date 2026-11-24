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
    m.Set("hint", Str(TR(ton_agent_chat_send_hint)));   // under the input while the agent cannot take what was typed
    SJ::Value rows = SJ::Value::Arr(); for (auto a : AgentStatus::AllAgents) rows.Push(TonAgentRow(a)); m.Set("rows", std::move(rows));
    Send("agents.setup", std::move(m));
}
// ---- « Connexion dans un autre onglet » (Paper 263M-0, note 265A-0) ----
// Each CLI's sign-in (`claude auth login`, `codex login`) opens its page with the system's default browser. When that is
// Browser, the page arrives as a link handed over by another launch (main.cpp OpenHandedTarget → TonAgentPageOpened) and
// opens in a tab: it is kept while that agent signs in, and offered again (« Rouvrir la page de connexion ↗ ») once its
// tab is closed. Another default browser: nothing arrives here, no link (the CLI's console still shows the address).
struct TonSignInPage { std::wstring url; std::weak_ptr<Tab> tab; bool seen = false; };
TonSignInPage tonSignInPages[2];
std::weak_ptr<Tab> tonAgentTab;   // the tab the screen alone was opened over: set aside for a sign-in, it comes back there
int tonSignInListener = 0;
// The sign-in page's hosts: Claude Code's OAuth page (claude.ai, anthropic.com, claude.com), Codex's (auth.openai.com,
// chatgpt.com). https only, no user part.
bool TonSignInHost(AgentStatus::Agent a, const std::wstring& url) {
    std::wstring u = url; for (auto& ch : u) ch = static_cast<wchar_t>(towlower(ch));
    if (u.rfind(L"https://", 0) != 0) return false;
    const auto end = u.find_first_of(L"/?#", 8); std::wstring host = u.substr(8, end == std::wstring::npos ? std::wstring::npos : end - 8);
    if (host.find(L'@') != std::wstring::npos) return false;
    if (const auto colon = host.find(L':'); colon != std::wstring::npos) host = host.substr(0, colon);
    auto under = [&host](const std::wstring& d) { return host == d || (host.size() > d.size() && host.compare(host.size() - d.size(), d.size(), d) == 0 && host[host.size() - d.size() - 1] == L'.'); };
    return a == AgentStatus::Agent::Codex ? under(L"openai.com") || under(L"chatgpt.com") : under(L"claude.ai") || under(L"anthropic.com") || under(L"claude.com");
}
void ListenTonSignIn() {
    if (tonSignInListener) return;
    tonSignInListener = AgentStatus::Subscribe([](const AgentStatus::Status& s) { if (s.state != AgentStatus::State::SigningIn) tonSignInPages[TonAgent::Index(s.agent)] = TonSignInPage{}; });   // the sign-in ended: forgotten
}
// 3b: whether the page opened in a tab of Browser (else another browser has it: its own lines, no link) and, once that
// tab is closed, the page to open again (while it is open the person goes back to it themselves).
NativeChrome::AnnotationOnboardingUI::SignInPage TonSignInPageFor(AgentStatus::Agent a) {
    const auto& p = tonSignInPages[TonAgent::Index(a)];
    if (!p.seen || AgentStatus::Get(a).state != AgentStatus::State::SigningIn) return {};
    return {true, p.tab.expired() ? p.url : std::wstring()};
}
// « Rouvrir la page de connexion ↗ »: that page in a new tab (the screen steps aside: the tab changed).
void ReopenSignInPage(AgentStatus::Agent a) {
    auto& p = tonSignInPages[TonAgent::Index(a)];
    if (!p.seen || AgentStatus::Get(a).state != AgentStatus::State::SigningIn || !TonSignInHost(a, p.url)) return;
    const std::wstring url = p.url;
    ::NewTab(url); p.tab = ::Active(); SetForegroundWindow(::window);
    Diag(std::wstring(L"Ton agent: the sign-in page of ") + (a == AgentStatus::Agent::Codex ? L"Codex" : L"Claude Code") + L" opened again in a tab");
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
    f.signInPage = [](AgentStatus::Agent a) { return TonSignInPageFor(a); };
    f.reopenSignIn = [](AgentStatus::Agent a) { ReopenSignInPage(a); };
    return true;
}();

}  // namespace Chat

// « Ton agent » alone (src/ton-agent-entry.h): the band's reminder, the chat, Réglages. UI thread, mode on or off.
void OpenTonAgent(TonAgentFrom from) {
    namespace OUI = NativeChrome::AnnotationOnboardingUI; namespace AO = AnnotationOnboarding;
    const wchar_t* who = from == TonAgentFrom::Band ? L"the band's reminder" : from == TonAgentFrom::Settings ? L"Réglages" : L"the chat";   // i18n-exempt: log detail
    Chat::EnsureAgentStatus();
    if (Ann::OnboardingOpen()) { if (Ann::onboarding->Aside()) Ann::onboardingTab = ::Active(); Ann::onboarding->Raise(Ann::ClientScreen()); Log(std::wstring(L"TON AGENT asked by ") + who + L": the tutorial is open (its « Ton agent » step), brought to the front"); return; }
    if (Chat::tonAgentWindow && Chat::tonAgentWindow->Visible()) { if (Chat::tonAgentWindow->Aside()) Chat::tonAgentTab = ::Active(); Chat::tonAgentWindow->Raise(Ann::ClientScreen()); return; }
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
    Chat::tonAgentTab = ::Active();
    Log(std::wstring(L"TON AGENT opened by ") + who);
}
// AnnotationTabChanged (src/annotation-mode.h): the screen alone steps aside while a sign-in's page is in front and comes
// back over the tab it was opened over (that tab closed meanwhile: it goes, nothing answered).
void TonAgentTabChanged() {
    auto& w = Chat::tonAgentWindow; if (!w || !w->Visible()) return;
    const auto start = Chat::tonAgentTab.lock();
    if (!start) { if (w->Aside()) { w->Dismiss(); Log(L"TON AGENT closed: the tab it was opened over is gone"); } return; }
    const bool was = w->Aside();
    w->TabInFront(start == ::Active(), Ann::ClientScreen());
    if (w->Aside() != was) Log(was ? L"TON AGENT back over its tab (the agents detected again)" : L"TON AGENT set aside: a sign-in page is in front");
}
// main.cpp OpenHandedTarget: a link handed over by another launch, opened in the tab in front. While an agent signs in and
// the link is its sign-in page (Browser is the default browser), it is kept for « Rouvrir la page de connexion ↗ ».
// Never logged (the address carries the sign-in's one-time state).
void TonAgentPageOpened(const std::wstring& url) {
    for (auto a : AgentStatus::AllAgents) {
        if (AgentStatus::Get(a).state != AgentStatus::State::SigningIn || !Chat::TonSignInHost(a, url)) continue;
        Chat::ListenTonSignIn(); Chat::tonSignInPages[TonAgent::Index(a)] = Chat::TonSignInPage{url, ::Active(), true};
        Log(std::wstring(L"TON AGENT the sign-in page of ") + (a == AgentStatus::Agent::Codex ? L"Codex" : L"Claude Code") + L" opened in a tab (kept to open it again)");
        return;
    }
}
