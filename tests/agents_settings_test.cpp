// « Ton agent » — Réglages › Agents and the band's reminder rule (worker C; Paper « Onboarding agents — Ton agent » ›
// « Réglages › Agents » + Designer's answer of 2026-11-24, aligned with the V1 tiles). Headless: the drawer is created on
// a hidden owner far off screen and painted offscreen.
//  - the pure model (src/annotation-settings.h): every chat agent state → its icon, words, tone (muted / accent / danger),
//    the row's one action (strong = the missing one; Annuler / Réessayer neutral), the Claude Desktop note, the manual
//    link; the root summary; the band's reminder rule (no chat agent ready, both detected, the chat closed);
//  - the mapping from the agents' one status (src/agent-status.h → src/chat-agents-view.h), every State and Reason,
//    Claude Desktop alone, « Annuler » live with worker A's CancelSignIn;
//  - the real drawer: the root « Agents » row (robot glyph; absent without the chat), the rows per status, the paid note,
//    « Avancé » › « Connecter un agent externe », the button's click only (Installer / Mettre à jour / Se connecter /
//    Annuler / Réessayer, nothing while an install runs), « Installer moi-même ↗ » (hit, Tab, UIA, click), the Downloads
//    morph while installing (frames) and still under reduced motion, hit testing, Tab order, UIA tree, search, the view's
//    refresh, « Annotations » without the old MCP rows and switch; PNG captures (96 / 144 dpi) per state, one in a
//    coloured profile.
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
static AS::ChatAgent A(ChatState st, std::wstring version = L"", bool desktopOnly = false, bool cancellable = false) {
    AS::ChatAgent a; a.state = st; a.version = std::move(version); a.desktopOnly = desktopOnly; a.cancellable = cancellable; return a;
}

// ---------------------------------------------------------------- the pure model
static void TestModel() {
    using X = AS::ChatAction; using I = AS::ChatIcon; using T = AS::ChatTone;
    struct Row { AS::ChatAgent a; const wchar_t* status; X action; bool enabled, strong; I icon; T tone; bool manual; const char* what; };
    const Row rows[] = {
        {A(ChatState::Unknown), L"Vérification…", X::None, false, false, I::None, T::Muted, false, "model: detecting — « Vérification… », nothing else"},
        {A(ChatState::NotInstalled), L"Pas installé", X::Install, true, true, I::Download, T::Muted, false, "model: not installed — ⤓ « Pas installé » #A6A6A6 + strong « Installer »"},
        {A(ChatState::TooOld, L"1.9.4"), L"Version trop ancienne", X::Update, true, true, I::Download, T::Muted, false, "model: too old — ⤓ « Version trop ancienne » + strong « Mettre à jour »"},
        {A(ChatState::Installing), L"Installation…", X::None, false, false, I::Download, T::Accent, false, "model: installing — ⤓ (animated) « Installation… » in the accent, no button"},
        {A(ChatState::NeedsSignIn, L"0.166.0"), L"Pas connecté", X::SignIn, true, true, I::Wifi, T::Muted, false, "model: signed out — wifi « Pas connecté » + strong « Se connecter »"},
        {A(ChatState::SigningIn, L"0.166.0", false, true), L"Connexion…", X::Cancel, true, false, I::Wifi, T::Accent, false, "model: signing in — wifi « Connexion… » in the accent + neutral « Annuler »"},
        {A(ChatState::SigningIn, L"0.166.0"), L"Connexion…", X::Cancel, false, false, I::Wifi, T::Accent, false, "model: signing in, no way to stop it — « Annuler » dimmed"},
        {A(ChatState::Ready, L"2.1.325"), L"Prêt · version 2.1", X::None, false, false, I::Check, T::Accent, false, "model: ready — ✓ « Prêt · version 2.1 » in the accent, no button"},
        {A(ChatState::InstallFailed), L"Échec de l'installation", X::Retry, true, false, I::Error, T::Danger, true, "model: install failed — ☹ « Échec de l'installation » #FF7B6E + neutral « Réessayer » + « Installer moi-même ↗ »"},
        {A(ChatState::SignInFailed, L"2.1.325"), L"Échec de la connexion", X::Retry, true, false, I::Error, T::Danger, false, "model: sign-in failed — ☹ « Échec de la connexion » + neutral « Réessayer »"},
    };
    for (const auto& r : rows)
        Check(AS::ChatStatusText(r.a) == r.status && AS::ChatActionFor(r.a) == r.action && AS::ChatActionEnabled(r.a) == r.enabled && AS::ChatActionStrong(r.a) == r.strong &&
              AS::ChatIconFor(r.a) == r.icon && AS::ChatToneFor(r.a) == r.tone && AS::ChatManualLink(r.a) == r.manual && AS::ChatStatusIsError(r.a) == (r.tone == T::Danger), r.what);
    Check(AS::ChatIconAnimated(A(ChatState::Installing)) && !AS::ChatIconAnimated(A(ChatState::NotInstalled)), "model: only « Installation… » animates its icon");
    Check(AS::ChatStatusText(A(ChatState::Ready)) == L"Prêt", "model: ready without a known version — « Prêt »");
    Check(AS::ChatNote(A(ChatState::NotInstalled, L"", true)) == L"L'appli Claude est installée, mais le chat a besoin de Claude Code (même compte)." &&
          AS::ChatNote(A(ChatState::NotInstalled)).empty() && AS::ChatNote(A(ChatState::Ready, L"2.1", true)).empty(), "model: the Claude Desktop note only when it is the only Claude there");
    int strong = 0; for (const auto& r : rows) strong += r.strong; Check(strong == 3, "model: one strong button per row, only on the missing action (Installer, Mettre à jour, Se connecter)");
    Check(AS::ShortVersion(L"2.1.325") == L"2.1" && AS::ShortVersion(L"0.166.0") == L"0.166" && AS::ShortVersion(L"3") == L"3" && AS::ShortVersion(L"2.1") == L"2.1", "version: major.minor");
    Check(std::wstring(AS::ChatActionLabel(X::Install)) == L"Installer" && std::wstring(AS::ChatActionLabel(X::Update)) == L"Mettre à jour" && std::wstring(AS::ChatActionLabel(X::SignIn)) == L"Se connecter" &&
          std::wstring(AS::ChatActionLabel(X::Cancel)) == L"Annuler" && std::wstring(AS::ChatActionLabel(X::Retry)) == L"Réessayer" && std::wstring(AS::ChatActionLabel(X::ManualInstall)) == L"Installer moi-même ↗",
          "labels: Installer, Mettre à jour, Se connecter, Annuler, Réessayer, Installer moi-même ↗");
    Check(std::string(AS::ChatGlyphs::For(I::Download)) == AS::ChatGlyphs::download && std::string(AS::ChatGlyphs::For(I::Check)) == AS::ChatGlyphs::check && !AS::ChatGlyphs::For(I::None), "icons: one Phosphor path per state icon");
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
        {S::NeedsSignIn, R::Cancelled, ChatState::NeedsSignIn, "map: a cancelled sign-in is « Pas connecté » again"},
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
    Check(AS::FromStatus(st(S::NotInstalled, R::DesktopOnly)).desktopOnly && !AS::FromStatus(st(S::NotInstalled)).desktopOnly, "map: Claude Desktop alone carries its note");
#ifdef AGENT_STATUS_CANCEL_SIGN_IN
    Check(AS::FromStatus(st(S::SigningIn)).cancellable && AS::ChatActionEnabled(AS::FromStatus(st(S::SigningIn))), "map: « Annuler » live (worker A's AgentStatus::CancelSignIn is there)");
#else
    Check(false, "map: worker A's AgentStatus::CancelSignIn (AGENT_STATUS_CANCEL_SIGN_IN) is missing");
#endif
    Check(!AS::FromStatus(st(S::NeedsSignIn)).cancellable, "map: only a running sign-in can be cancelled");
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
    std::vector<std::pair<AS::ChatApp, AS::ChatAction>> acted; int refreshed = 0, external = 0; bool reduced = false;
    SettingsUI::Drawer::Hooks h;
    h.annotations = [] { return true; };
    h.reducedMotion = [&] { return reduced; };
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
    auto row = [&](const wchar_t* name) { static std::vector<SettingsUI::Slot> keep; keep = dr.SlotsForTest(); const auto* s = Find(keep, Kind::Agent, name); return s ? *s : SettingsUI::Slot{}; };

    // ---- Root: « Agents » after « Annotations », the ready agents as its summary ----------------------
    dr.Show(); dr.SettleForTest();
    {
        const auto slots = dr.SlotsForTest(); const auto* r = Find(slots, Kind::Nav, L"Agents"); const auto* ann = Find(slots, Kind::Nav, L"Annotations");
        Check(r && r->hit == SettingsUI::CatAgents && r->path == SettingsUI::AgentsGlyph && r->value == L"Claude Code", "root: « Agents » (Phosphor robot), summary « Claude Code »");
        Check(r && ann && r->top > ann->top && ann->value.empty(), "root: after « Annotations » (which no longer sums up MCP agents)");
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
        Check(claude && claude->hit == 0 && claude->value.empty() && Join(claude->lines) == L"Prêt · version 2.1" && claude->height == 54 && !claude->manual && claude->notes.empty(), "Claude Code: ✓ « Prêt · version 2.1 », no button, 54 high");
        Check(codex && codex->hit == SettingsUI::AgentHit(AS::ChatApp::Codex) && codex->value == L"Se connecter" && Join(codex->lines) == L"Pas connecté" && !codex->disabled,
              "Codex: wifi « Pas connecté » + « Se connecter »");
        Check(claude && codex && codex->top - claude->top == 54 + 6, "rows 6 apart (board gap)");
        const auto* note = Find(slots, Kind::Note, L"Claude Code demande un compte Claude payant (Pro, Max, Team ou Enterprise).");
        Check(note && note->fine && !note->hit && codex && note->top > codex->top && Join(note->lines) == note->text, "the paid-account note under the agents (13/18)");
        const auto* ext = Find(slots, Kind::Nav, L"Connecter un agent externe");
        Check(ext && ext->hit == SettingsUI::ExternalAgents && Join(ext->lines) == L"Pour une app hors du navigateur (Claude Desktop, Codex) qui agit dans Browser." && ext->lines.size() == 2,
              "« Avancé » › « Connecter un agent externe », its description on two lines (board)");
        if (codex) { const auto b = dr.AgentActionForTest(*codex); Check(dr.HitForTest((b.left + b.right) / 2, (b.top + b.bottom) / 2) == codex->hit && dr.HitForTest(SettingsUI::Layout::agentText + 4, codex->top + 12) == 0, "hit: the button only"); }
        if (codex) { const auto b = dr.AgentActionForTest(*codex); Check(b.right == 312 && b.bottom - b.top == 28 && std::abs((b.top + b.bottom) / 2 - (codex->top + codex->height / 2)) < .01f, "button: 28 high, centred, ending at 312 (board)"); }
        if (ext) Check(dr.HitForTest(100, ext->top + ext->height / 2) == SettingsUI::ExternalAgents, "hit: « Connecter un agent externe » row");
        const auto order = dr.FocusOrderForTest(); Check(order.size() >= 4 && order[0] == SettingsUI::Back && order[1] == SettingsUI::Close && order[2] == SettingsUI::AgentHit(AS::ChatApp::Codex) && order[3] == SettingsUI::ExternalAgents, "Tab order: Retour, Fermer, Se connecter Codex, Connecter un agent externe");
    }
    {
        const auto nodes = dr.AccessibleNodesForTest();
        Check(NodeNamed(nodes, L"Claude Code : Prêt · version 2.1") && !NodeNamed(nodes, L"Claude Code : Prêt · version 2.1")->focusable, "UIA: « Claude Code : Prêt · version 2.1 » read as text");
        const auto* b = Node(nodes, SettingsUI::AgentHit(AS::ChatApp::Codex));
        Check(b && b->name == L"Se connecter Codex" && b->help == L"Pas connecté" && b->enabled && b->automationId == L"SettingsAgentCodex", "UIA: button « Se connecter Codex », state as help");
        const auto* e = Node(nodes, SettingsUI::ExternalAgents);
        Check(e && e->name == L"Connecter un agent externe" && e->help == L"Pour une app hors du navigateur (Claude Desktop, Codex) qui agit dans Browser." && e->automationId == L"SettingsExternalAgents", "UIA: « Connecter un agent externe », described");
        Check(NodeNamed(nodes, L"Claude Code demande un compte Claude payant (Pro, Max, Team ou Enterprise).") != nullptr, "UIA: the paid note is readable");
    }

    // ---- Clicks: the button only, never by itself ---------------------------------------------------
    Check(acted.empty(), "nothing acted while showing (no install by itself)");
    dr.ActivateForTest(SettingsUI::AgentHit(AS::ChatApp::Codex), false);
    Check(acted.size() == 1 && acted[0].first == AS::ChatApp::Codex && acted[0].second == AS::ChatAction::SignIn, "« Se connecter » (click): SignIn Codex");
    dr.ActivateForTest(SettingsUI::AgentHit(AS::ChatApp::ClaudeCode), false);
    Check(acted.size() == 1, "ready: no button to press");
    dr.ActivateForTest(SettingsUI::ExternalAgents, true);
    Check(external == 1, "« Connecter un agent externe » asks for the MCP « Connecter un agent » window");

    // ---- In a coloured profile: the accent is the profile's ---------------------------------------------
    {
        const auto keepFill = Design::Accent::Get(); const auto keepInk = Design::Accent::Ink();
        Design::Accent::Set(0x1A8641); Design::Accent::SetInk(0x6FD087);   // Paper's « Travail » sample (fill / ink)
        shot(L"agents-board-green-profile");
        Design::Accent::Current() = keepFill; Design::Accent::SettledCurrent() = keepFill; Design::Accent::InkCurrent() = keepInk;
    }

    // ---- Every state, as the view shows it ----------------------------------------------------------
    acted.clear();
    set(A(ChatState::NotInstalled, L"", true), A(ChatState::NotInstalled));
    {
        const auto claude = row(L"Claude Code"); const auto codex = row(L"Codex");
        Check(claude.value == L"Installer" && !claude.disabled && Join(claude.lines) == L"Pas installé" && Join(claude.notes) == L"L'appli Claude est installée, mais le chat a besoin de Claude Code (même compte)." && claude.height > 54,
              "Claude Desktop alone: ⤓ « Pas installé » + « Installer », and the note under it (a taller row)");
        Check(codex.value == L"Installer" && codex.notes.empty() && codex.height == 54, "Codex not installed: ⤓ « Pas installé » + « Installer », no note");
        dr.ActivateForTest(SettingsUI::AgentHit(AS::ChatApp::ClaudeCode), true);
        Check(acted.size() == 1 && acted[0].first == AS::ChatApp::ClaudeCode && acted[0].second == AS::ChatAction::Install, "« Installer » (Entrée): Install Claude Code — the click is the consent");
        const auto nodes = dr.AccessibleNodesForTest();
        Check(NodeNamed(nodes, L"Claude Code : Pas installé. L'appli Claude est installée, mais le chat a besoin de Claude Code (même compte).") != nullptr, "UIA: the note is read with the state");
    }
    shot(L"agents-not-installed");
    set(A(ChatState::Installing), A(ChatState::Unknown));
    {
        const auto claude = row(L"Claude Code"); const auto codex = row(L"Codex");
        Check(claude.hit == 0 && claude.value.empty() && Join(claude.lines) == L"Installation…", "installing: ⤓ « Installation… », no button");
        Check(codex.hit == 0 && codex.value.empty() && Join(codex.lines) == L"Vérification…", "detecting: « Vérification… », no button");
        const size_t before = acted.size(); dr.ActivateForTest(SettingsUI::AgentHit(AS::ChatApp::ClaudeCode), false); Check(acted.size() == before, "installing: nothing to press");
        Check(dr.NextWakeForTest().delay.has_value(), "installing: the Downloads morph asks for frames");
        reduced = true; dr.SetReducedMotion(); dr.SettleForTest();
        Check(!dr.NextWakeForTest().delay.has_value(), "installing, reduced motion: a still icon, no frames");
        reduced = false; dr.SetReducedMotion();
    }
    shot(L"agents-installing");
    set(A(ChatState::InstallFailed), A(ChatState::SignInFailed, L"0.166.0"));
    {
        const auto claude = row(L"Claude Code"); const auto codex = row(L"Codex");
        Check(claude.value == L"Réessayer" && claude.error && claude.manual && Join(claude.lines) == L"Échec de l'installation", "install failed: ☹ « Échec de l'installation » + neutral « Réessayer » + the link");
        Check(codex.value == L"Réessayer" && codex.error && !codex.manual && Join(codex.lines) == L"Échec de la connexion", "sign-in failed: ☹ « Échec de la connexion » + « Réessayer », no link");
        const int link = SettingsUI::ManualHit(AS::ChatApp::ClaudeCode);
        const auto order = dr.FocusOrderForTest(); const auto at = std::find(order.begin(), order.end(), SettingsUI::AgentHit(AS::ChatApp::ClaudeCode));
        Check(at != order.end() && at + 1 != order.end() && *(at + 1) == link, "Tab order: « Réessayer », then « Installer moi-même ↗ »");
        const auto nodes = dr.AccessibleNodesForTest(); const auto* n = Node(nodes, link);
        Check(n && n->name == L"Installer moi-même Claude Code" && n->automationId == L"SettingsAgentManualClaudeCode" && n->bounds.right > n->bounds.left, "UIA: link « Installer moi-même Claude Code »");
        acted.clear(); dr.ActivateForTest(link, false);
        Check(acted.size() == 1 && acted[0].first == AS::ChatApp::ClaudeCode && acted[0].second == AS::ChatAction::ManualInstall, "« Installer moi-même ↗ »: asks the host for the official page");
        acted.clear(); dr.ActivateForTest(SettingsUI::AgentHit(AS::ChatApp::ClaudeCode), false); dr.ActivateForTest(SettingsUI::AgentHit(AS::ChatApp::Codex), false);
        Check(acted.size() == 2 && acted[0].second == AS::ChatAction::Retry && acted[1].second == AS::ChatAction::Retry, "« Réessayer »: Retry (the host takes the consent from this click)");
        acted.clear(); dr.ActivateForTest(SettingsUI::ManualHit(AS::ChatApp::Codex), false); Check(acted.empty(), "no link, no action (sign-in failure)");
    }
    shot(L"agents-errors");
    set(A(ChatState::TooOld, L"1.9.4"), A(ChatState::SigningIn, L"0.166.0", false, true));
    {
        const auto claude = row(L"Claude Code"); const auto codex = row(L"Codex");
        Check(claude.value == L"Mettre à jour" && Join(claude.lines) == L"Version trop ancienne", "too old: ⤓ « Version trop ancienne » + strong « Mettre à jour »");
        Check(codex.value == L"Annuler" && !codex.disabled && Join(codex.lines) == L"Connexion…", "signing in: wifi « Connexion… » + neutral « Annuler »");
        acted.clear(); dr.ActivateForTest(SettingsUI::AgentHit(AS::ChatApp::ClaudeCode), false); dr.ActivateForTest(SettingsUI::AgentHit(AS::ChatApp::Codex), false);
        Check(acted.size() == 2 && acted[0].second == AS::ChatAction::Update && acted[1].second == AS::ChatAction::Cancel, "« Mettre à jour »: Update; « Annuler »: Cancel");
        bool fits = true; for (auto& s : dr.SlotsForTest()) if (s.kind == Kind::Agent) fits = fits && s.lines.size() == 1;
        Check(fits, "every state fits on one line beside its button (French)");
    }
    shot(L"agents-too-old-signing-in");
    set(A(ChatState::Ready, L"2.1.325"), A(ChatState::SigningIn, L"0.166.0"));
    {
        const auto codex = row(L"Codex"); Check(codex.value == L"Annuler" && codex.disabled, "signing in without CancelSignIn: « Annuler » dimmed");
        acted.clear(); dr.ActivateForTest(SettingsUI::AgentHit(AS::ChatApp::Codex), false); Check(acted.empty(), "a dimmed « Annuler » does nothing");
    }
    set(A(ChatState::Ready, L"2.1.325"), A(ChatState::Ready, L"0.166.0"));
    {
        const auto codex = row(L"Codex"); Check(codex.hit == 0 && Join(codex.lines) == L"Prêt · version 0.166", "both ready: ✓ « Prêt · version 0.166 »");
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
