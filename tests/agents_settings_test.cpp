// « Ton agent » — Réglages › Agents and the band's reminder rule (worker C; Paper « Onboarding agents — Ton agent » ›
// « Réglages › Agents »). Headless: the drawer is created on a hidden owner far off screen and painted offscreen.
//  - the pure model (src/annotation-settings.h): every chat agent state → status line, dot, button, strong, « ✓ Prêt »;
//    the root summary; the band's reminder rule (no chat agent ready, both detected, the chat closed);
//  - the mapping from the agents' one status (src/agent-status.h → src/chat-agents-view.h), every State and Reason;
//  - the real drawer: the root « Agents » row (absent without the chat), the view's rows per status, the paid note,
//    « Avancé » › « Connecter un agent externe » (asks for the MCP window), the button's click only (Install / Retry /
//    Se connecter; nothing while it runs; « Prêt » is not a button), hit testing, Tab order, UIA tree, search, the
//    view's refresh, « Annotations » without the old MCP rows and switch; PNG captures (96 / 144 dpi) per state.
// Never a real installer, sign-in or detection: the statuses are given by the test.
// Usage: agents_settings_test.exe <repo root> <capture folder>
#include <windows.h>
#include <cstdio>
#include <algorithm>
#include <string>
#include <vector>
#include "../src/chrome.h"
#include "../src/settings-panel.h"
#include "../src/chat-agents-view.h"

using namespace NativeChrome;
namespace AS = AnnotationSettings;
using SettingsUI::Kind;
using AS::ChatState;
static int failures = 0, checks = 0;
static void Check(bool ok, const char* what) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", what); } else std::printf("ok   %s\n", what); }
static void Pump(int ms = 30) { const DWORD end = GetTickCount() + ms; MSG m; while (static_cast<int>(end - GetTickCount()) > 0) while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); } }
static std::string Utf8(const std::wstring& w) { const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr); std::string s(size_t(n), '\0'); WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr); return s; }
static const SettingsUI::Slot* Find(const std::vector<SettingsUI::Slot>& slots, Kind kind, const std::wstring& text) { for (auto& s : slots) if (s.kind == kind && s.text == text) return &s; return nullptr; }
static const Accessibility::Node* Node(const std::vector<Accessibility::Node>& nodes, int hit) { const long long key = SettingsUI::Drawer::UiaKey(hit); for (auto& n : nodes) if (n.key == key) return &n; return nullptr; }
static const Accessibility::Node* NodeNamed(const std::vector<Accessibility::Node>& nodes, const std::wstring& name) { for (auto& n : nodes) if (n.name == name) return &n; return nullptr; }
static std::wstring Join(const std::vector<std::wstring>& lines) { std::wstring s; for (auto& l : lines) s += (s.empty() ? L"" : L" ") + l; return s; }
static AS::ChatAgent A(ChatState st, std::wstring version = L"") { AS::ChatAgent a; a.state = st; a.version = std::move(version); return a; }

// ---------------------------------------------------------------- the pure model
static void TestModel() {
    struct Row { ChatState st; const wchar_t* version; const wchar_t* status; AS::ChatAction action; bool enabled, strong; AS::ChatDot dot; bool ready, error; };
    const Row rows[] = {
        {ChatState::Unknown, L"", L"Vérification…", AS::ChatAction::None, false, false, AS::ChatDot::None, false, false},
        {ChatState::NotInstalled, L"", L"Pas installé", AS::ChatAction::Install, true, true, AS::ChatDot::Idle, false, false},
        {ChatState::TooOld, L"1.9.4", L"Version 1.9 · trop ancienne", AS::ChatAction::Install, true, true, AS::ChatDot::Idle, false, false},
        {ChatState::Installing, L"", L"Installation…", AS::ChatAction::Install, false, true, AS::ChatDot::None, false, false},
        {ChatState::NeedsSignIn, L"0.166.0", L"Installé · pas connecté", AS::ChatAction::SignIn, true, false, AS::ChatDot::Pending, false, false},
        {ChatState::SigningIn, L"0.166.0", L"Connexion en cours…", AS::ChatAction::SignIn, false, false, AS::ChatDot::Pending, false, false},
        {ChatState::Ready, L"2.1.325", L"Version 2.1", AS::ChatAction::None, false, false, AS::ChatDot::None, true, false},
        {ChatState::InstallFailed, L"", L"L'installation n'a pas abouti", AS::ChatAction::Retry, true, true, AS::ChatDot::Danger, false, true},
        {ChatState::SignInFailed, L"2.1.325", L"La connexion n'a pas abouti", AS::ChatAction::Retry, true, false, AS::ChatDot::Danger, false, true},
    };
    for (const auto& r : rows) {
        const auto a = A(r.st, r.version);
        const std::string what = "model: " + Utf8(r.status);
        Check(AS::ChatStatusText(a) == r.status && AS::ChatActionFor(a) == r.action && AS::ChatActionEnabled(a) == r.enabled && AS::ChatActionStrong(a) == r.strong &&
              AS::ChatDotFor(a) == r.dot && AS::ChatReadyMark(a) == r.ready && AS::ChatStatusIsError(a) == r.error, what.c_str());
    }
    Check(AS::ChatStatusText(A(ChatState::Ready)) == L"Installé" && AS::ChatStatusText(A(ChatState::TooOld)) == L"Version trop ancienne", "model: no version known: « Installé », « Version trop ancienne »");
    Check(AS::ShortVersion(L"2.1.325") == L"2.1" && AS::ShortVersion(L"0.166.0") == L"0.166" && AS::ShortVersion(L"3") == L"3" && AS::ShortVersion(L"2.1") == L"2.1", "version: major.minor (« Version 2.1 », board)");
    Check(std::wstring(AS::ChatActionLabel(AS::ChatAction::Install)) == L"Installer" && std::wstring(AS::ChatActionLabel(AS::ChatAction::SignIn)) == L"Se connecter" &&
          std::wstring(AS::ChatActionLabel(AS::ChatAction::Retry)) == L"Réessayer", "buttons: Installer, Se connecter, Réessayer");
    AS::ChatAgents g;
    Check(AS::ChatSummary(g).empty(), "summary: nothing while the first detection runs");
    g.claude = A(ChatState::NotInstalled); Check(AS::ChatSummary(g).empty(), "summary: one still detecting: nothing yet");
    g.codex = A(ChatState::NeedsSignIn); Check(AS::ChatSummary(g) == L"Aucun agent prêt", "summary: « Aucun agent prêt »");
    g.codex = A(ChatState::Ready); Check(AS::ChatSummary(g) == L"Codex", "summary: « Codex »");
    g.claude = A(ChatState::Ready); Check(AS::ChatSummary(g) == L"Claude Code, Codex", "summary: « Claude Code, Codex »");
}

// ---------------------------------------------------------------- the band's reminder rule
static void TestReminder() {
    AS::ChatAgents g;
    Check(!AS::BandReminder(g, false), "reminder: hidden while the first detection runs (no flash at start)");
    g.claude = A(ChatState::NotInstalled); Check(!AS::BandReminder(g, false), "reminder: hidden while one agent is still unknown");
    g.codex = A(ChatState::NotInstalled); Check(AS::BandReminder(g, false), "reminder: shown — neither agent installed");
    Check(!AS::BandReminder(g, true), "reminder: steps back while the chat is open (its own block says it)");
    g.codex = A(ChatState::NeedsSignIn); Check(AS::BandReminder(g, false), "reminder: shown — Codex installed but signed out");
    g.claude = A(ChatState::Installing); Check(AS::BandReminder(g, false), "reminder: shown — an install running is not « ready »");
    g.claude = A(ChatState::InstallFailed); g.codex = A(ChatState::SignInFailed); Check(AS::BandReminder(g, false), "reminder: shown — both failed");
    g.codex = A(ChatState::Ready); Check(!AS::BandReminder(g, false), "reminder: hidden — Codex ready (any ready agent is enough)");
    g.claude = A(ChatState::Ready); g.codex = A(ChatState::Unknown); Check(!AS::BandReminder(g, false), "reminder: hidden — Claude Code ready, Codex still detecting");
}

// ---------------------------------------------------------------- the mapping from the one status (worker A)
static void TestMapping() {
    using S = AgentStatus::State; using R = AgentStatus::Reason;
    auto st = [](S state, R reason = R::None, const char* version = "") { AgentStatus::Status s; s.state = state; s.reason = reason; s.version = version; return s; };
    struct M { S state; R reason; ChatState want; const char* what; };
    const M table[] = {
        {S::Unknown, R::None, ChatState::Unknown, "map: Unknown"},
        {S::NotInstalled, R::None, ChatState::NotInstalled, "map: NotInstalled"},
        {S::NotInstalled, R::DesktopOnly, ChatState::NotInstalled, "map: Claude Desktop alone is « Pas installé »"},
        {S::NotInstalled, R::TooOld, ChatState::TooOld, "map: TooOld"},
        {S::Installing, R::None, ChatState::Installing, "map: Installing"},
        {S::NeedsSignIn, R::None, ChatState::NeedsSignIn, "map: NeedsSignIn"},
        {S::SigningIn, R::None, ChatState::SigningIn, "map: SigningIn"},
        {S::Ready, R::None, ChatState::Ready, "map: Ready"},
        {S::Error, R::Network, ChatState::InstallFailed, "map: Error(network) → install failed"},
        {S::Error, R::FileInUse, ChatState::InstallFailed, "map: Error(file in use) → install failed"},
        {S::Error, R::ExecutionPolicy, ChatState::InstallFailed, "map: Error(execution policy) → install failed"},
        {S::Error, R::Timeout, ChatState::InstallFailed, "map: Error(timeout) → install failed"},
        {S::Error, R::InstallerFailed, ChatState::InstallFailed, "map: Error(installer) → install failed"},
        {S::Error, R::NotFoundAfterInstall, ChatState::InstallFailed, "map: Error(not found after install) → install failed"},
        {S::Error, R::Disk, ChatState::InstallFailed, "map: Error(disk) → install failed"},
        {S::Error, R::SignInFailed, ChatState::SignInFailed, "map: Error(sign-in) → sign-in failed"},
        {S::Error, R::SignInTimeout, ChatState::SignInFailed, "map: Error(sign-in timeout) → sign-in failed"},
        {S::Error, R::Cancelled, ChatState::Unknown, "map: Error(cancelled) is never shown"},
    };
    for (const auto& m : table) Check(AS::FromStatus(st(m.state, m.reason)).state == m.want, m.what);
    Check(AS::FromStatus(st(S::Ready, R::None, "2.1.325")).version == L"2.1.325", "map: the version is carried");
    Check(AS::ToAgent(AS::ChatApp::ClaudeCode) == AgentStatus::Agent::Claude && AS::ToAgent(AS::ChatApp::Codex) == AgentStatus::Agent::Codex, "map: rows → agents");
    const auto both = AS::FromStatuses(st(S::Ready, R::None, "2.1.325"), st(S::NeedsSignIn));
    Check(both.claude.state == ChatState::Ready && both.codex.state == ChatState::NeedsSignIn, "map: both agents in their rows");
}

int wmain(int argc, wchar_t** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 2;
    INITCOMMONCONTROLSEX cc{sizeof cc, ICC_STANDARD_CLASSES}; InitCommonControlsEx(&cc);
    const std::wstring root = argc > 1 ? argv[1] : L".";
    const std::wstring shots = argc > 2 ? argv[2] : root + L"\\build\\ton-agent-qa\\settings";
    TestModel(); TestReminder(); TestMapping();

    // A hidden owner far off screen: the drawer docks to it, so nothing ever shows on the desktop.
    HINSTANCE hi = GetModuleHandleW(nullptr);
    WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = hi; wc.lpszClassName = L"AgentsSettingsOwner"; RegisterClassW(&wc);
    HWND owner = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"owner", WS_POPUP, -32000, -32000, 1100, 860, nullptr, nullptr, hi, nullptr);

    BrowserSettings::State st;
    AS::ChatAgents agents; agents.claude = A(ChatState::Ready, L"2.1.325"); agents.codex = A(ChatState::NeedsSignIn, L"0.166.0");   // the board's state
    std::vector<std::pair<AS::ChatApp, AS::ChatAction>> acted; int refreshed = 0, external = 0;
    SettingsUI::Drawer::Hooks h;
    h.annotations = [] { return true; };
    h.countAnnotations = [](std::function<void(std::size_t)> done) { done(0); };
    h.chatAgents = [&] { return agents; };
    h.refreshChatAgents = [&] { ++refreshed; };
    h.chatAgentAction = [&](AS::ChatApp app, AS::ChatAction action) { acted.push_back({app, action}); };
    h.openExternalAgents = [&] { ++external; };
    SettingsUI::Drawer dr; Check(dr.Create(hi, owner, root, &st, h), "drawer created");
    Check(!IsWindowVisible(owner), "headless: the owner is never shown");
    const float height = 760;
    auto shot = [&](const wchar_t* name) {
        dr.ScrollForTest(0); dr.SettleForTest(); Pump(20);
        const bool a = dr.RenderForTest(shots + L"\\" + name + L"-96.png", 96, height, root), b = dr.RenderForTest(shots + L"\\" + name + L"-144.png", 144, height, root);
        Check(a && b, (std::string("capture ") + Utf8(name) + " (96 and 144 dpi)").c_str());
    };
    auto set = [&](AS::ChatAgent claude, AS::ChatAgent codex) { agents.claude = claude; agents.codex = codex; dr.ChatAgentsChanged(); dr.SettleForTest(); };

    // ---- Root: « Agents » after « Annotations », the ready agents as its summary ----------------------
    dr.Show(); dr.SettleForTest();
    {
        const auto slots = dr.SlotsForTest(); const auto* row = Find(slots, Kind::Nav, L"Agents"); const auto* ann = Find(slots, Kind::Nav, L"Annotations");
        Check(row && row->hit == SettingsUI::CatAgents && row->path == SettingsUI::AgentsGlyph && row->value == L"Claude Code", "root: « Agents » (robot glyph), summary « Claude Code »");
        Check(row && ann && row->top > ann->top && ann->value.empty(), "root: after « Annotations » (which no longer sums up MCP agents)");
        const auto nodes = dr.AccessibleNodesForTest(); const auto* n = Node(nodes, SettingsUI::CatAgents);
        Check(n && n->name == L"Agents" && n->status == L"Claude Code" && n->automationId == L"SettingsAgents", "UIA root: « Agents », status « Claude Code » (SettingsAgents)");
    }
    {
        SettingsUI::Drawer plain; SettingsUI::Drawer::Hooks ph; BrowserSettings::State ps; ph.annotations = [] { return true; };
        Check(plain.Create(hi, owner, root, &ps, ph), "drawer without the chat");
        plain.Show(); plain.SettleForTest(); bool none = true; for (auto& s : plain.SlotsForTest()) none = none && s.hit != SettingsUI::CatAgents;
        Check(none, "chat compiled out: no « Agents » row");
        bool agentHit = false; for (const auto& r : SettingsUI::SearchSettings(L"codex", true, true, false)) agentHit = agentHit || SettingsUI::IsAgentsEntry(*r.entry);
        Check(!agentHit, "chat compiled out: search finds no « Agents » row");
        plain.Hide(false, false); plain.Dispose();
    }

    // ---- The view (the board's state: Claude Code ready, Codex signed out) ---------------------------
    refreshed = 0;
    dr.ActivateForTest(SettingsUI::CatAgents, false); dr.SettleForTest();
    Check(dr.CurrentViewForTest() == SettingsUI::View::Agents && refreshed == 1, "row opens « Agents » and asks for a new detection");
    shot(L"agents-board");
    {
        const auto slots = dr.SlotsForTest();
        std::vector<std::wstring> labels; for (auto& s : slots) if (s.kind == Kind::Label) labels.push_back(s.text);
        Check(labels == std::vector<std::wstring>{L"Agents du chat", L"Avancé"}, "groups: « Agents du chat », « Avancé » (board)");
        const auto* claude = Find(slots, Kind::Agent, L"Claude Code"); const auto* codex = Find(slots, Kind::Agent, L"Codex");
        Check(claude && claude->hit == 0 && claude->value.empty() && Join(claude->lines) == L"Version 2.1" && claude->height == 54, "Claude Code: « Version 2.1 », no button (« ✓ Prêt »), 54 high");
        Check(codex && codex->hit == SettingsUI::AgentHit(AS::ChatApp::Codex) && codex->value == L"Se connecter" && Join(codex->lines) == L"Installé · pas connecté" && !codex->disabled,
              "Codex: « Installé · pas connecté » + « Se connecter »");
        Check(claude && codex && codex->top - claude->top == 54 + 6, "rows 6 apart (board gap)");
        const auto* note = Find(slots, Kind::Note, L"Claude Code demande un compte Claude payant (Pro, Max, Team ou Enterprise).");
        Check(note && note->fine && !note->hit && codex && note->top > codex->top && Join(note->lines) == note->text, "the paid-account note under the agents (13/18)");
        const auto* ext = Find(slots, Kind::Nav, L"Connecter un agent externe");
        Check(ext && ext->hit == SettingsUI::ExternalAgents && Join(ext->lines) == L"Pour une app hors du navigateur (Claude Desktop, Codex) qui agit dans Browser." && ext->lines.size() == 2,
              "« Avancé » › « Connecter un agent externe », its description on two lines (board)");
        // Hit testing: only the button answers on an agent row; the described row answers as a whole.
        if (codex) { const auto b = dr.AgentActionForTest(*codex); Check(dr.HitForTest((b.left + b.right) / 2, (b.top + b.bottom) / 2) == codex->hit && dr.HitForTest(SettingsUI::Layout::agentText + 4, codex->top + 12) == 0, "hit: the button only"); }
        if (codex) { const auto b = dr.AgentActionForTest(*codex); Check(b.right == 312 && b.bottom - b.top == 28 && std::abs((b.top + b.bottom) / 2 - (codex->top + codex->height / 2)) < .01f, "button: 28 high, centred, ending at 312 (board)"); }
        if (ext) Check(dr.HitForTest(100, ext->top + ext->height / 2) == SettingsUI::ExternalAgents, "hit: « Connecter un agent externe » row");
        const auto order = dr.FocusOrderForTest(); Check(order.size() >= 4 && order[0] == SettingsUI::Back && order[1] == SettingsUI::Close && order[2] == SettingsUI::AgentHit(AS::ChatApp::Codex) && order[3] == SettingsUI::ExternalAgents, "Tab order: the rows in reading order");
    }
    {
        const auto nodes = dr.AccessibleNodesForTest();
        Check(NodeNamed(nodes, L"Claude Code : Version 2.1, Prêt") && !NodeNamed(nodes, L"Claude Code : Version 2.1, Prêt")->focusable, "UIA: « Claude Code : Version 2.1, Prêt » read as text");
        const auto* b = Node(nodes, SettingsUI::AgentHit(AS::ChatApp::Codex));
        Check(b && b->name == L"Se connecter Codex" && b->help == L"Installé · pas connecté" && b->enabled && b->automationId == L"SettingsAgentCodex", "UIA: button « Se connecter Codex », status as help");
        const auto* e = Node(nodes, SettingsUI::ExternalAgents);
        Check(e && e->name == L"Connecter un agent externe" && e->help == L"Pour une app hors du navigateur (Claude Desktop, Codex) qui agit dans Browser." && e->automationId == L"SettingsExternalAgents", "UIA: « Connecter un agent externe », described");
        Check(NodeNamed(nodes, L"Claude Code demande un compte Claude payant (Pro, Max, Team ou Enterprise).") != nullptr, "UIA: the paid note is readable");
    }

    // ---- Clicks: the button only, never by itself ---------------------------------------------------
    Check(acted.empty(), "nothing acted while showing (no install by itself)");
    dr.ActivateForTest(SettingsUI::AgentHit(AS::ChatApp::Codex), false);
    Check(acted.size() == 1 && acted[0].first == AS::ChatApp::Codex && acted[0].second == AS::ChatAction::SignIn, "« Se connecter » (click): SignIn Codex");
    dr.ActivateForTest(SettingsUI::AgentHit(AS::ChatApp::ClaudeCode), false);
    Check(acted.size() == 1, "« ✓ Prêt » is not a button");
    dr.ActivateForTest(SettingsUI::ExternalAgents, true);
    Check(external == 1, "« Connecter un agent externe » asks for the MCP « Connecter un agent » window");

    // ---- Every state, as the view shows it ----------------------------------------------------------
    acted.clear();
    set(A(ChatState::NotInstalled), A(ChatState::NotInstalled));
    {
        const auto slots = dr.SlotsForTest(); const auto* claude = Find(slots, Kind::Agent, L"Claude Code"); const auto* codex = Find(slots, Kind::Agent, L"Codex");
        Check(claude && claude->value == L"Installer" && !claude->disabled && Join(claude->lines) == L"Pas installé" && codex && codex->value == L"Installer", "not installed: « Pas installé » + « Installer » (both)");
        dr.ActivateForTest(SettingsUI::AgentHit(AS::ChatApp::ClaudeCode), true);
        Check(acted.size() == 1 && acted[0].first == AS::ChatApp::ClaudeCode && acted[0].second == AS::ChatAction::Install, "« Installer » (Entrée): Install Claude Code — the click is the consent");
    }
    shot(L"agents-not-installed");
    set(A(ChatState::Installing), A(ChatState::Unknown));
    {
        const auto slots = dr.SlotsForTest(); const auto* claude = Find(slots, Kind::Agent, L"Claude Code"); const auto* codex = Find(slots, Kind::Agent, L"Codex");
        Check(claude && claude->value == L"Installer" && claude->disabled && Join(claude->lines) == L"Installation…", "installing: « Installation… », « Installer » kept, disabled");
        Check(codex && codex->hit == 0 && codex->value.empty() && Join(codex->lines) == L"Vérification…", "detecting: « Vérification… », no button");
        const auto order = dr.FocusOrderForTest(); Check(std::find(order.begin(), order.end(), SettingsUI::AgentHit(AS::ChatApp::ClaudeCode)) != order.end(), "a busy button keeps its place in the Tab order");
        const size_t before = acted.size(); dr.ActivateForTest(SettingsUI::AgentHit(AS::ChatApp::ClaudeCode), false); Check(acted.size() == before, "installing: a second click does nothing");
        const auto nodes = dr.AccessibleNodesForTest(); const auto* b = Node(nodes, SettingsUI::AgentHit(AS::ChatApp::ClaudeCode)); Check(b && !b->enabled, "UIA: the running button is disabled");
    }
    shot(L"agents-installing");
    set(A(ChatState::InstallFailed), A(ChatState::SignInFailed, L"0.166.0"));
    {
        const auto slots = dr.SlotsForTest(); const auto* claude = Find(slots, Kind::Agent, L"Claude Code"); const auto* codex = Find(slots, Kind::Agent, L"Codex");
        Check(claude && claude->value == L"Réessayer" && claude->error && Join(claude->lines) == L"L'installation n'a pas abouti", "install failed: danger « L'installation n'a pas abouti » + « Réessayer »");
        Check(codex && codex->value == L"Réessayer" && codex->error && Join(codex->lines) == L"La connexion n'a pas abouti", "sign-in failed: danger « La connexion n'a pas abouti » + « Réessayer »");
        acted.clear(); dr.ActivateForTest(SettingsUI::AgentHit(AS::ChatApp::ClaudeCode), false); dr.ActivateForTest(SettingsUI::AgentHit(AS::ChatApp::Codex), false);
        Check(acted.size() == 2 && acted[0].second == AS::ChatAction::Retry && acted[1].second == AS::ChatAction::Retry, "« Réessayer »: Retry (the host takes the consent from this click)");
    }
    shot(L"agents-errors");
    set(A(ChatState::TooOld, L"1.9.4"), A(ChatState::SigningIn, L"0.166.0"));
    {
        const auto slots = dr.SlotsForTest(); const auto* claude = Find(slots, Kind::Agent, L"Claude Code"); const auto* codex = Find(slots, Kind::Agent, L"Codex");
        Check(claude && claude->value == L"Installer" && Join(claude->lines) == L"Version 1.9 · trop ancienne", "too old: « Version 1.9 · trop ancienne » + « Installer » (installing again updates)");
        Check(codex && codex->value == L"Se connecter" && codex->disabled && Join(codex->lines) == L"Connexion en cours…", "signing in: « Connexion en cours… », « Se connecter » disabled");
        bool fits = true; for (auto& s : slots) if (s.kind == Kind::Agent) fits = fits && s.lines.size() == 1;
        Check(fits, "every status fits on one line beside its button (French)");
    }
    shot(L"agents-too-old-signing-in");
    set(A(ChatState::Ready, L"2.1.325"), A(ChatState::Ready, L"0.166.0"));
    {
        const auto slots = dr.SlotsForTest(); const auto* codex = Find(slots, Kind::Agent, L"Codex");
        Check(codex && codex->hit == 0 && Join(codex->lines) == L"Version 0.166", "both ready: « Version 0.166 », « ✓ Prêt »");
    }
    shot(L"agents-ready");

    // ---- Search: the agents' rows, found only with the chat -----------------------------------------
    {
        const auto codex = SettingsUI::SearchSettings(L"codex", true, true, true);
        bool path = false; for (const auto& r : codex) if (r.entry->hit == SettingsUI::AgentHit(AS::ChatApp::Codex)) path = std::wstring(I18n::T(r.entry->path.id)) == L"Agents";
        Check(path, "search: « codex » finds the Codex row, in « Agents »");
        const auto ext = SettingsUI::SearchSettings(L"agent externe", true, true, true);
        Check(!ext.empty() && ext[0].entry->hit == SettingsUI::ExternalAgents, "search: « agent externe » finds « Connecter un agent externe »");
        bool old = false; for (const auto& r : SettingsUI::SearchSettings(L"proposer", true, true, true)) old = old || std::wstring(I18n::T(r.entry->title.id)) == L"Proposer de connecter un agent";
        Check(!old, "search: « Proposer de connecter un agent » is gone");
    }

    // ---- « Annotations » no longer lists agents -------------------------------------------------------
    dr.KeyForTest(VK_ESCAPE); dr.SettleForTest();
    dr.ActivateForTest(SettingsUI::CatAnnotations, false); dr.SettleForTest();
    {
        const auto slots = dr.SlotsForTest();
        bool agentRows = false; for (auto& s : slots) agentRows = agentRows || s.kind == Kind::Agent;
        Check(dr.CurrentViewForTest() == SettingsUI::View::Annotations && !agentRows && !Find(slots, Kind::Nav, L"Autre app ou réglage manuel") && !Find(slots, Kind::Switch, L"Proposer de connecter un agent"),
              "« Annotations »: no MCP agent rows, no « Autre app ou réglage manuel », no « Proposer de connecter un agent »");
        std::vector<std::wstring> labels; for (auto& s : slots) if (s.kind == Kind::Label) labels.push_back(s.text);
        Check(labels == std::vector<std::wstring>{L"Mode Annotation", L"Historique"}, "« Annotations »: groups « Mode Annotation », « Historique »");
    }
    shot(L"annotations");

    // ---- Opening straight on « Agents » refreshes too -------------------------------------------------
    dr.Hide(false, false); refreshed = 0; dr.ShowAgents(); dr.SettleForTest();
    Check(dr.CurrentViewForTest() == SettingsUI::View::Agents && refreshed == 1, "ShowAgents(): straight on « Agents », detected again");

    dr.Hide(false, false); dr.Dispose(); DestroyWindow(owner);
    std::printf("%d checks, %d failures\n", checks, failures);
    CoUninitialize();
    return failures ? 1 : 0;
}
