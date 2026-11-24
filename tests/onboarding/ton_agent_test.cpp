// « Ton agent » (worker B): the screen's state machine and its renders, headless. Paper « Browser Design » › « Onboarding
// agents — Ton agent (maquette prête) ». Never the real installers, CLIs, sign-ins, PATH or browser profile:
//   - the pure decisions (src/annotation-onboarding-agent.h) and the tutorial's flow around A6 / alone;
//   - the production window (src/annotation-onboarding.h) on the REAL hub (src/agent-status.h) over A's fake profiles:
//     temporary folders for %USERPROFILE% / %LOCALAPPDATA% / %APPDATA%, the fake CLI (tests/acp/fake_cli.cpp), the fake
//     official installer (sandbox/agents/fake-install.ps1, BROWSER_FAKE_INSTALL_MODE), the user PATH on a test key under
//     HKCU\Software\BrowserTests, the CLI's own sign-in faked (no console). The hub's work is queued and pumped by the
//     test, so « Installation… » can be seen and drawn;
//   - Claude Code present / absent / Claude Desktop only, Codex signed in / out, install success / failure, retry, a
//     sign-in that fails (5), « Installer moi-même », « Plus tard », « Continuer », alone (the chat, the band's reminder);
//   - the tile IS the button (Val, Paper note 265A-0): a click runs its action, hover only on a tile with one, the other
//     tile waiting during an install, the names screen readers say, Tab / Entrée / Espace on the tiles;
//   - « Connexion dans un autre onglet » (3b 263M-0): the window steps aside while another tab is in front during a
//     sign-in and comes back over its own tab (still signing in: 3b; signed in meanwhile: Prêt), « Rouvrir la page de
//     connexion ↗ » when the host has the page;
//   - every artboard drawn off screen at 96 and 120 DPI into <out> (PNG), for tests/onboarding/ton_agent_compare.py.
// Usage: ton_agent_test.exe <repo root> <out dir> <fake-cli.exe> <work dir>
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>
#include "../../src/settings.h"
#include "../../src/annotation-onboarding.h"

static int failures = 0, checks = 0;
static void Expect(bool ok, const char* what) { ++checks; std::printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++failures; }
static bool Near(float a, float b, float eps = .51f) { return std::fabs(a - b) <= eps; }

namespace UI = NativeChrome::AnnotationOnboardingUI; namespace AO = AnnotationOnboarding; namespace AS = AgentStatus; namespace TA = TonAgent; namespace RT = AgentRuntime;
using AO::Step; using S = AS::State; using R = AS::Reason;
const auto C = RT::Agent::Claude, X = RT::Agent::Codex;

// ---------------------------------------------------------------- the pure decisions
static AS::Status St(RT::Agent a, S state, R reason = R::None) { AS::Status s; s.agent = a; s.state = state; s.reason = reason; return s; }
static void Model() {
    Expect(TA::MarkOf(St(C, S::NotInstalled)) == TA::Mark::Install && TA::MarkOf(St(C, S::NotInstalled, R::DesktopOnly)) == TA::Mark::Install && TA::MarkOf(St(C, S::NotInstalled, R::TooOld)) == TA::Mark::Update,
           "mark: not installed (also Claude Desktop only) → « Installer »; too old → « Mettre à jour »");
    Expect(TA::MarkOf(St(C, S::Installing)) == TA::Mark::Installing && TA::MarkOf(St(X, S::NeedsSignIn)) == TA::Mark::Connect && TA::MarkOf(St(X, S::SigningIn)) == TA::Mark::SigningIn, "mark: « Installation… », « Connecter », « Connexion… » (3b)");
    Expect(TA::MarkOf(St(C, S::Ready)) == TA::Mark::Ready && TA::MarkOf(St(C, S::Error, R::Network)) == TA::Mark::Error && TA::MarkOf(St(C, S::Error, R::SignInFailed)) == TA::Mark::SignInFailed && TA::MarkOf(St(C, S::Unknown)) == TA::Mark::Unknown,
           "mark: « ✓ Prêt », « Erreur », « Connexion échouée » (5), nothing while the first detection runs");
    Expect(TA::LookOf(St(C, S::NotInstalled), true) == TA::Look::Rest && TA::LookOf(St(X, S::NeedsSignIn), true) == TA::Look::Rest, "look: a tile waiting for its click stays at rest (1 Choisir, 3 À connecter)");
    Expect(TA::LookOf(St(C, S::Installing), false) == TA::Look::Current && TA::LookOf(St(X, S::SigningIn), false) == TA::Look::Current && TA::LookOf(St(C, S::Ready), true) == TA::Look::Current && TA::LookOf(St(C, S::Ready), false) == TA::Look::Rest,
           "look: cyan while busy (wherever it is: 2 Installation, 3b), ready when the target");
    Expect(TA::LookOf(St(C, S::Error, R::Network), true) == TA::Look::Danger && TA::LookOf(St(C, S::Error, R::Network), false) == TA::Look::Rest, "look: danger outline on the failed target only");
    Expect(TA::InkOf(St(X, S::NeedsSignIn), true) == TA::Ink::Grey && TA::InkOf(St(X, S::SigningIn), false) == TA::Ink::Accent && TA::InkOf(St(C, S::Error, R::Disk), false) == TA::Ink::Danger, "ink: grey at rest, the light accent while busy, danger for a failure");
    auto act = [](S st, R r = R::None) { return TA::ActionOf(St(C, st, r)); };
    Expect(act(S::NotInstalled) == TA::Action::Install && act(S::NotInstalled, R::TooOld) == TA::Action::Update && act(S::NeedsSignIn) == TA::Action::SignIn && act(S::Error, R::Network) == TA::Action::Retry && act(S::Error, R::SignInFailed) == TA::Action::Retry,
           "the tile's click: Installer, Mettre à jour, Se connecter, Réessayer (Val: the tile is the button)");
    Expect(act(S::Installing) == TA::Action::None && act(S::SigningIn) == TA::Action::None && act(S::Ready) == TA::Action::None && act(S::Unknown) == TA::Action::None, "the tile's click: none while busy, ready or detecting (no hover, no click)");
    Expect(TA::Waits(St(C, S::Installing)) && TA::Waits(St(C, S::SigningIn)) && !TA::Clickable(St(X, S::NotInstalled), St(C, S::Installing)) && TA::Clickable(St(X, S::NotInstalled), St(C, S::NeedsSignIn)) && !TA::Waits(St(C, S::Error, R::Network)),
           "while one agent installs or signs in, the other tile waits (Designer: 32 %, no click)");
    Expect(!TA::CanContinue(St(C, S::NotInstalled), St(X, S::NeedsSignIn)) && TA::CanContinue(St(C, S::NotInstalled), St(X, S::Ready)), "« Continuer » only once an agent is ready");
    Expect(TA::HeadingOf(St(C, S::NotInstalled)) == TA::Heading::Choose && TA::HeadingOf(St(C, S::Error, R::Network)) == TA::Heading::Choose && TA::HeadingOf(St(C, S::Installing)) == TA::Heading::Installing &&
           TA::HeadingOf(St(X, S::NeedsSignIn)) == TA::Heading::SignIn && TA::HeadingOf(St(X, S::SigningIn)) == TA::Heading::SigningIn && TA::HeadingOf(St(C, S::Error, R::SignInTimeout)) == TA::Heading::SignIn && TA::HeadingOf(St(X, S::Ready)) == TA::Heading::Ready,
           "heading: Choisissez (also after an install failure), Installation de, Connectez (also after a sign-in failure), Connexion en cours, est prêt");
    Expect(TA::TroubleOf(St(C, S::Error, R::Network)) == TA::Trouble::Install && TA::ManualOffered(St(C, S::Error, R::NotFoundAfterInstall)) && TA::TroubleOf(St(C, S::Error, R::SignInFailed)) == TA::Trouble::SignIn &&
           !TA::ManualOffered(St(C, S::Error, R::SignInTimeout)) && TA::TroubleOf(St(C, S::Ready)) == TA::Trouble::None && TA::TroubleOf(St(X, S::SigningIn)) == TA::Trouble::SigningIn,
           "under the tiles: an install failure offers « Installer moi-même », a sign-in failure does not; 3b while signing in");
    Expect(TA::DefaultTarget(St(C, S::NotInstalled), St(X, S::NeedsSignIn)) == C, "target: Claude Code first, even when Codex only needs a sign-in (Claude Code 1 Choisir)");
    Expect(TA::DefaultTarget(St(C, S::NotInstalled), St(X, S::Ready)) == X && TA::DefaultTarget(St(C, S::Ready), St(X, S::Ready)) == C, "target: a ready agent (Claude Code first)");
    Expect(TA::DefaultTarget(St(C, S::Error, R::Network), St(X, S::Installing)) == X && TA::DefaultTarget(St(C, S::NotInstalled), St(X, S::Error, R::Disk)) == X, "target: busy first, then ready, then failed");
    TA::Screen sc; Expect(!sc.Picked() && sc.Target(St(C, S::NotInstalled), St(X, S::NotInstalled)) == C, "target: no tile picked at the start");
    sc.Pick(X); Expect(sc.Target(St(C, S::Ready), St(X, S::NotInstalled)) == X, "target: a click on a tile makes it the target (Codex 1 Choisir)");
    namespace K = TA::Card;
    Expect(Near(K::Height(TA::Mark::Install, TA::Mark::Connect, TA::Trouble::None), 157) && Near(K::Top(St(C, S::NotInstalled), 157), 141.5f), "card: 157 high, centred on y 220 (Choisir: « top: calc(50% - 70px) »)");
    Expect(Near(K::Height(TA::Mark::Ready, TA::Mark::Connect, TA::Trouble::None), 160) && Near(K::Top(St(C, S::Ready), 160), 140) && Near(K::Top(St(C, S::Installing), 157), 123), "card: Prêt at 140 (the « ✓ Prêt » row 17), Installation at 123");
    Expect(Near(K::Height(TA::Mark::Error, TA::Mark::Connect, TA::Trouble::Install), 228) && Near(K::Top(St(C, S::Error, R::Network), 228), 72), "card: Erreur 228 high at 72 (the two lines and the link)");
    Expect(Near(K::Height(TA::Mark::Install, TA::Mark::SigningIn, TA::Trouble::SigningIn, true), 225) && Near(K::Height(TA::Mark::Install, TA::Mark::SigningIn, TA::Trouble::SigningIn, false), 203) && Near(K::Top(St(X, S::SigningIn), 225), 72),
           "card: 3b at 72, 225 high with « Rouvrir la page de connexion ↗ », 203 without");
}

static void FlowAround() {
    AO::Flow f(Step::Connect);
    Expect(f.Later() && f.step() == Step::Ready && f.ending() == AO::Ending::None, "flow: A6 « Plus tard » → A7 (no agent now)");
    Expect(f.Back() && f.step() == Step::Connect, "flow: A7 « Retour » → A6");
    Expect(f.Back() && f.step() == Step::Where && f.Next() && f.step() == Step::Connect, "flow: A6 « Retour » (once ready) → A5");
    Expect(f.OpenChat() && !f.open() && f.ending() == AO::Ending::Finished && f.chat(), "flow: A6 « Ouvrir le chat » ends it, finished, the chat opens");
    AO::Flow a(Step::What, true);
    Expect(a.alone() && a.step() == Step::Connect, "alone: only « Ton agent »");
    Expect(a.Later() && a.ending() == AO::Ending::Skipped, "alone: « Plus tard » closes it (skipped)");
    AO::Flow b(Step::What, true); Expect(b.Back() && b.ending() == AO::Ending::Skipped && !b.chat(), "alone: no step to go back to (closes)");
    AO::Flow c(Step::What, true); Expect(c.OpenChat() && c.ending() == AO::Ending::Finished && c.chat(), "alone: « Ouvrir le chat » (finished)");
    Expect(AO::ReaderOf(true, false) == AO::Reader::Claude && AO::ReaderOf(false, true) == AO::Reader::Codex && AO::ReaderOf(false, false) == AO::Reader::None && std::wstring(AO::ReaderName(AO::Reader::Claude)) == L"Claude Code",
           "A7: the READY agent (Claude Code first), none otherwise");
}

// ---------------------------------------------------------------- A's fake profiles under the real hub
static std::wstring Work, FakeCli;
static bool signInFails = false;   // the fake sign-in leaves the CLI signed out (5 Connexion échouée)
static std::deque<std::pair<std::function<void()>, std::function<void()>>> queued;
static int queuedRuns = 0;
static void Pump() { while (!queued.empty()) { auto job = std::move(queued.front()); queued.pop_front(); if (job.first) job.first(); if (job.second) job.second(); } }
static bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
static void MakeDir(const std::wstring& d) { std::wstring acc; for (size_t i = 0; i <= d.size(); ++i) { if (i == d.size() || d[i] == L'\\') { if (acc.size() > 2) CreateDirectoryW(acc.c_str(), nullptr); } if (i < d.size()) acc += d[i]; } }
static void WriteText(const std::wstring& p, const char* text) { HANDLE f = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr); DWORD put = 0; WriteFile(f, text, DWORD(strlen(text)), &put, nullptr); CloseHandle(f); }
static const wchar_t* PathKey() { static const std::wstring k = L"Software\\BrowserTests\\TonAgentScreen-" + std::to_wstring(GetCurrentProcessId()); return k.c_str(); }
struct FakeProfile { std::wstring home, local, roaming; };
static FakeProfile Profile(const std::wstring& name) { FakeProfile p{Work + L"\\profiles\\" + name + L"\\home", Work + L"\\profiles\\" + name + L"\\local", Work + L"\\profiles\\" + name + L"\\roaming"}; MakeDir(p.home); MakeDir(p.local); MakeDir(p.roaming); return p; }
static std::wstring ClaudeExe(const FakeProfile& p) { return p.home + L"\\.local\\bin\\claude.exe"; }
static std::wstring CodexExe(const FakeProfile& p) { return p.local + L"\\Programs\\OpenAI\\Codex\\bin\\codex.exe"; }
static void PutCli(const std::wstring& exe, const char* version, const char* login) {
    MakeDir(exe.substr(0, exe.rfind(L'\\'))); CopyFileW(FakeCli.c_str(), exe.c_str(), FALSE);
    WriteText(exe + L".version", version); WriteText(exe + L".login", login);
}
static void UseProfile(const FakeProfile& p) {
    AS::Host h;
    h.dataRoot = Work + L"\\data";
    h.run = [](std::function<void()> work, std::function<void()> done) { ++queuedRuns; queued.emplace_back(std::move(work), std::move(done)); };   // the test pumps it
    h.environment = [p] { RT::Environment e; e.dataRoot = Work + L"\\data"; e.userProfile = p.home; e.localAppData = p.local; e.appData = p.roaming; e.path = L""; e.freshUserEnvironment = false; return e; };
    h.signIn = [](const RT::Environment& env, RT::Agent a, unsigned, std::string*) {   // the CLI's own sign-in, faked: no console, no web page
        const std::wstring exe = a == RT::Agent::Claude ? env.userProfile + L"\\.local\\bin\\claude.exe" : env.localAppData + L"\\Programs\\OpenAI\\Codex\\bin\\codex.exe";
        if (Exists(exe) && !signInFails) WriteText(exe + L".login", "0");
        return true;
    };
    SetEnvironmentVariableW(L"FAKE_HOME", p.home.c_str()); SetEnvironmentVariableW(L"FAKE_LOCAL", p.local.c_str());
    AS::Init(h); Pump();
}
static void Mode(const wchar_t* m) { SetEnvironmentVariableW(L"BROWSER_FAKE_INSTALL_MODE", m); }

struct Fixture {
    UI::Window window; std::wstring root, out;
    int closed = 0, chats = 0, pages = 0, order = 0, closedAt = 0, chatAt = 0, reopens = 0; AO::Ending ending = AO::Ending::None; RT::Agent chatAgent = C; std::wstring page;
    bool signInTab = false; std::wstring signInUrl;   // the host's sign-in page: in a tab of Browser, and the one to open again ("" = none)
    void Setup(Step start = Step::Connect, bool alone = false) {
        UI::Options o; o.reduced = true; o.start = start; o.alone = alone;
        o.hooks.closed = [this](AO::Ending e, bool) { ++closed; ending = e; closedAt = ++order; };
        o.hooks.openChat = [this](RT::Agent a) { ++chats; chatAgent = a; chatAt = ++order; };
        o.hooks.openPage = [this](const std::wstring& url) { ++pages; page = url; };
        o.hooks.signInPage = [this](RT::Agent) { return UI::SignInPage{signInTab, signInUrl}; };
        o.hooks.reopenSignIn = [this](RT::Agent) { ++reopens; };
        window.SetupForTest(root, o); window.SetShaderSecondsForTest(0);
        closed = chats = pages = order = closedAt = chatAt = reopens = 0; ending = AO::Ending::None; signInTab = false; signInUrl.clear();
    }
    bool Shot(const std::wstring& name) {
        bool ok = true;
        for (UINT dpi : {96u, 120u}) { const std::wstring path = out + L"\\" + name + (dpi == 96 ? L"@96.png" : L"@120.png"); ok = window.RenderForTest(dpi, path.c_str()) && ok; }
        return ok;
    }
    void Click(int id) { window.ActivateForTest(id); }
};

static void Screens(Fixture& f) {
    using W = UI::Window;
    // ---- Claude Code: absent; Codex: installed, signed out (Paper « Claude Code » row) ----
    {
        const auto p = Profile(L"claude-row"); PutCli(CodexExe(p), "codex-cli 0.166.0", "1"); UseProfile(p);
        Expect(AS::Get(C).state == S::NotInstalled && AS::Get(X).state == S::NeedsSignIn, "profile: Claude Code absent, Codex installed and signed out");
        f.Setup(); Pump();
        Expect(f.window.TitleForTest() == L"Choisissez votre agent" && f.window.SecondaryForTest() == L"Plus tard" && !f.window.PrimaryEnabledForTest() && f.window.TargetForTest() == C,
               "1 Choisir: « Choisissez votre agent », « Plus tard », no action button (Val: the tile is the button)");
        Expect(f.window.TileEnabledForTest(C) && f.window.TileEnabledForTest(X) && f.window.TileNameForTest(C) == L"Installer Claude Code" && f.window.TileNameForTest(X) == L"Se connecter à Codex",
               "1 Choisir: each tile is a button named by its action (« Installer Claude Code », « Se connecter à Codex »)");
        Expect(f.window.MarkForTest(C) == L"Installer" && f.window.MarkForTest(X) == L"Connecter", "1 Choisir: the marks « Installer », « Connecter »");
        Expect(f.window.BodyForTest() == L"Le chat de Browser utilise Claude Code ou Codex,\ninstallés sur cet ordinateur.", "1 Choisir: the board's two lines");
        Expect(Near(f.window.CardTopForTest(), 141.5f) && !f.window.ManualOfferedForTest() && f.window.LinkForTest().empty() && f.window.DotsForTest(), "1 Choisir: the card centred (top 141.5), no link, the tutorial's progress");
        Expect(!Exists(ClaudeExe(p)) && queued.empty() && AS::Get(C).state == S::NotInstalled, "consent: the screen shown installs nothing by itself");
        f.window.HoverForTest(W::ClaudeRow);
        Expect(Near(f.window.TileLiftForTest(C), 1) && f.Shot(L"claude-1-choisir"), "render: Claude Code 1 Choisir, the tile hovered (22ZR-0: #262626, #3A3A3A, « Installer » white)");
        f.window.HoverForTest(W::None); Expect(f.Shot(L"claude-1-choisir-repos"), "render: Claude Code 1 Choisir at rest");
        Mode(L"ok"); f.Click(W::ClaudeRow);
        Expect(AS::Get(C).state == S::Installing && !Exists(ClaudeExe(p)), "2 Installation: the tile's click is the consent — installing (the fake installer queued)");
        Expect(f.window.TitleForTest() == L"Installation de Claude Code" && !f.window.PrimaryEnabledForTest() && f.window.SecondaryForTest() == L"Plus tard", "2 Installation: « Installation de Claude Code », only « Plus tard »");
        Expect(!f.window.TileEnabledForTest(C) && !f.window.TileEnabledForTest(X) && f.window.TileNameForTest(C).empty(), "2 Installation: no click on the installing tile; the other tile waits (dimmed)");
        f.window.HoverForTest(W::CodexRow); Expect(Near(f.window.TileLiftForTest(X), 0), "2 Installation: the waiting tile takes no hover");
        f.Click(W::CodexRow); Expect(AS::Get(X).state == S::NeedsSignIn, "2 Installation: a click on the waiting tile does nothing");
        f.window.HoverForTest(W::None);
        Expect(f.window.BodyForTest() == L"Ça peut prendre quelques minutes.\nBrowser vérifie tout seul quand c'est fini." && Near(f.window.CardTopForTest(), 123), "2 Installation: the board's lines, the card at 123");
        Expect(f.Shot(L"claude-2-installation"), "render: Claude Code 2 Installation (2312-0; Codex dimmed while it waits)");
        f.Click(W::ClaudeRow); Expect(AS::Get(C).state == S::Installing, "2 Installation: the installing tile's click does nothing");
        Pump();
        Expect(Exists(ClaudeExe(p)) && AS::Get(C).state == S::NeedsSignIn, "install success: the fake official installer put Claude Code there → to connect");
        Expect(f.window.TitleForTest() == L"Connectez Claude Code" && f.window.TileNameForTest(C) == L"Se connecter à Claude Code" && f.window.TileEnabledForTest(X), "Claude Code to connect: « Connectez Claude Code », its tile signs in, Codex clickable again");
        Expect(f.window.BodyForTest().find(L"compte Claude payant") != std::wstring::npos && f.window.BodyForTest().find(L"Pro, Max, Team ou Enterprise") != std::wstring::npos, "Claude Code to connect: its paid plan said at the sign-in");
        Expect(f.Shot(L"claude-to-connect"), "render: Claude Code to connect (the Codex « À connecter » board for Claude Code)");
        // the sign-in (3b): the window steps aside while another tab is in front, comes back over its tab
        f.Click(W::ClaudeRow);
        Expect(AS::Get(C).state == S::SigningIn && f.window.TitleForTest() == L"Connexion en cours" && f.window.BodyForTest() == L"Terminez la connexion dans votre navigateur,\npuis revenez ici." &&
               f.window.TroubleForTest() == L"La page de connexion est ouverte dans votre navigateur web.\nBrowser voit tout seul quand c'est fait." && f.window.LinkForTest().empty(),
               "3b, the page in another browser (none seen in a tab): « Terminez la connexion dans votre navigateur », its lines, no « Rouvrir »");
        Expect(f.Shot(L"claude-3b-autre-navigateur"), "render: 3b, the sign-in page in another browser (Designer's note 26AL-0)");
        f.signInTab = true;
        Expect(f.window.BodyForTest() == L"Terminez la connexion dans l'onglet ouvert,\npuis revenez ici." && f.window.LinkForTest().empty(),
               "3b, the page in a tab of Browser (still open): « Terminez la connexion dans l'onglet ouvert, puis revenez ici. », no link yet");
        Expect(f.window.MarkForTest(C) == L"Connexion…" && !f.window.TileEnabledForTest(C) && !f.window.TileEnabledForTest(X) && f.window.TroubleForTest() == L"La page de connexion est ouverte dans un autre onglet.\nBrowser voit tout seul quand c'est fait." && Near(f.window.CardTopForTest(), 72),
               "3b: the tile « Connexion… » (no click), the other tile waits, the card's two lines, the card at 72");
        f.window.TabInFrontForTest(true); Expect(!f.window.AsideForTest(), "3b: its own tab in front → stays");
        f.window.TabInFrontForTest(false); Expect(f.window.AsideForTest() && f.window.OpenForTest(), "3b: the sign-in page's tab in front → the window steps aside (still open)");
        f.window.TabInFrontForTest(false); Expect(f.window.AsideForTest(), "3b: another tab → still aside");
        f.window.TabInFrontForTest(true);
        Expect(!f.window.AsideForTest() && AS::Get(C).state == S::SigningIn && f.window.TitleForTest() == L"Connexion en cours", "3b: back on its tab, not signed in yet → « Connexion en cours » again");
        f.window.TabInFrontForTest(false); Expect(f.window.AsideForTest(), "3b: aside again");
        Pump();   // the sign-in finishes while the window is aside
        Expect(AS::Get(C).Ready() && f.window.AsideForTest(), "3b: signed in meanwhile, the window stays aside until its tab is in front");
        f.window.TabInFrontForTest(true); Pump();
        Expect(!f.window.AsideForTest() && f.window.TitleForTest() == L"Claude Code est prêt" && f.window.PrimaryForTest() == L"Continuer" && f.window.PrimaryEnabledForTest() && f.window.SecondaryForTest() == L"Plus tard",
               "3 Prêt at the return: « Claude Code est prêt », « Plus tard », « Continuer »");
        f.window.TabInFrontForTest(false); Expect(!f.window.AsideForTest(), "no sign-in running: a tab change does not set it aside");
        Expect(!f.window.TileEnabledForTest(C) && f.window.TileNameForTest(C).empty() && f.window.MarkForTest(C) == L"Prêt", "3 Prêt: the ready tile has no hover and no click");
        Expect(f.window.BodyForTest() == L"Il travaille sur vos projets,\ndirectement depuis le chat de Browser." && Near(f.window.CardTopForTest(), 140), "3 Prêt: the board's lines, the card at 140");
        Expect(f.Shot(L"claude-3-pret"), "render: Claude Code 3 Prêt (232D-0)");
        f.Click(W::Primary); Expect(f.window.Flow().step() == Step::Ready && f.closed == 0 && f.chats == 0, "3 Prêt « Continuer » → A7 (the tutorial goes on)");
        f.Click(W::Secondary); Expect(f.window.Flow().step() == Step::Connect, "A7 « Retour » → A6");
        f.Click(W::Secondary); Expect(f.window.Flow().step() == Step::Ready, "3 Prêt « Plus tard » → A7 too");
        f.Click(W::Primary); Expect(f.closed == 1 && f.ending == AO::Ending::Finished && f.chats == 0, "A7 « Terminer »: the tutorial ends (finished)");
        // Codex now: its tile signs in at once
        f.Setup(); Expect(f.window.TileNameForTest(X) == L"Se connecter à Codex", "Codex: its tile says « Se connecter à Codex »");
        f.Click(W::CodexRow);
        Expect(AS::Get(X).state == S::SigningIn && f.window.TargetForTest() == X && f.window.TitleForTest() == L"Connexion en cours", "a click on the Codex tile: signing in, Codex the target");
        Pump(); Expect(AS::Get(X).Ready(), "Codex signed in");
    }
    // ---- Codex: a sign-in that fails (like Claude Code's 5, Designer: its ChatGPT account) ----
    {
        const auto p = Profile(L"codex-signin-error"); PutCli(CodexExe(p), "codex-cli 0.166.0", "1"); UseProfile(p);
        f.Setup(); signInFails = true; f.Click(W::CodexRow); Pump(); signInFails = false;
        Expect(AS::Get(X).state == S::Error && AS::Get(X).reason == R::SignInFailed && f.window.TitleForTest() == L"Connectez Codex" && f.window.MarkForTest(X) == L"Connexion échouée",
               "Codex sign-in failed: « Connectez Codex », « Connexion échouée »");
        Expect(f.window.TroubleForTest() == L"La connexion n'a pas abouti.\nVérifiez votre compte ChatGPT, puis réessayez." && f.window.TileNameForTest(X) == L"Réessayer la connexion à Codex", "Codex sign-in failed: its ChatGPT account, its tile retries");
        Expect(f.Shot(L"codex-5-connexion-echouee"), "render: Codex, sign-in failed");
        Pump();
    }
    // ---- Claude Code: the install fails, retry, « Installer moi-même »; then a sign-in that fails (5) ----
    {
        const auto p = Profile(L"claude-error"); PutCli(CodexExe(p), "codex-cli 0.166.0", "1"); UseProfile(p);
        f.Setup(); Mode(L"network"); f.Click(W::ClaudeRow); Pump();
        Expect(AS::Get(C).state == S::Error && AS::Get(C).reason == R::Network, "install failure (network, the fake installer) → error");
        Expect(f.window.TitleForTest() == L"Choisissez votre agent" && !f.window.PrimaryEnabledForTest() && f.window.SecondaryForTest() == L"Plus tard" && f.window.ManualOfferedForTest(), "4 Erreur: « Choisissez votre agent », only « Plus tard », « Installer moi-même ↗ »");
        Expect(f.window.MarkForTest(C) == L"Erreur" && f.window.MarkForTest(C, true) == L"Réessayer" && f.window.TileNameForTest(C) == L"Réessayer l'installation de Claude Code" && f.window.TileEnabledForTest(C),
               "4 Erreur: « Erreur »; hovered « Réessayer », its click retries (« Réessayer l'installation de Claude Code »)");
        Expect(f.window.TroubleForTest() == L"L'installation n'a pas abouti.\nUn antivirus, un proxy d'entreprise ou PowerShell a pu la bloquer." && Near(f.window.CardTopForTest(), 72), "4 Erreur: the board's two lines, the card at 72");
        Expect(f.Shot(L"claude-4-erreur"), "render: Claude Code 4 Erreur (233O-0)");
        f.window.HoverForTest(W::ClaudeRow); Expect(f.Shot(L"claude-4-erreur-survol"), "render: 4 Erreur, the failed tile hovered (« Réessayer » #FF7B6E)"); f.window.HoverForTest(W::None);
        f.Click(W::Manual);
        Expect(f.pages == 1 && f.page == AS::ManualInstall(C).url && f.window.ManualClicksForTest() == 1 && AS::Get(C).state == S::Error, "« Installer moi-même ↗ »: the official page (the command copied), nothing installed by us");
        Expect(f.closed == 1 && f.ending == AO::Ending::Skipped && f.chats == 0, "« Installer moi-même ↗ »: the screen goes first (the page would be hidden behind it)");
        f.Setup(); Expect(f.window.TargetForTest() == C && f.window.TileNameForTest(C) == L"Réessayer l'installation de Claude Code", "opened again: the failure is still the target, its tile retries");
        Mode(L"policy"); f.Click(W::ClaudeRow); Expect(AS::Get(C).state == S::Installing, "the failed tile's click (consent): the install again"); Pump();
        Expect(AS::Get(C).state == S::Error && AS::Get(C).reason == R::ExecutionPolicy, "retry: PowerShell's policy → error again");
        Mode(L"ok"); f.Click(W::ClaudeRow); Pump();
        Expect(AS::Get(C).state == S::NeedsSignIn && Exists(ClaudeExe(p)), "retry: then it works → installed, to connect");
        signInFails = true; f.Click(W::ClaudeRow); Pump(); signInFails = false;
        Expect(AS::Get(C).state == S::Error && AS::Get(C).reason == R::SignInFailed, "a sign-in that does not finish → error (sign-in)");
        Expect(f.window.TitleForTest() == L"Connectez Claude Code" && f.window.BodyForTest() == L"Connectez-vous une fois avec votre compte Claude,\nla page s'ouvre dans votre navigateur web." && f.window.MarkForTest(C) == L"Connexion échouée",
               "5 Connexion échouée: « Connectez Claude Code », the board's lines, the mark « Connexion échouée »");
        Expect(f.window.TroubleForTest() == L"La connexion n'a pas abouti.\nVérifiez votre compte Claude payant, puis réessayez." && !f.window.ManualOfferedForTest() && Near(f.window.CardTopForTest(), 72) && !f.window.PrimaryEnabledForTest(),
               "5 Connexion échouée: the card's two lines (the paid account), no link, the card at 72, no action button");
        Expect(f.window.TileNameForTest(C) == L"Réessayer la connexion à Claude Code" && f.window.MarkForTest(C, true) == L"Réessayer", "5 Connexion échouée: its tile retries the sign-in (« Réessayer » hovered)");
        Expect(f.Shot(L"claude-5-connexion-echouee"), "render: Claude Code 5 Connexion échouée (25OI-0)");
        f.Click(W::ClaudeRow); Expect(AS::Get(C).state == S::SigningIn, "5: the tile's click signs in again"); Pump();
        Expect(AS::Get(C).Ready(), "then signed in → ready");
    }
    // ---- Codex row (both absent) ----
    {
        const auto p = Profile(L"codex-row"); UseProfile(p);
        f.Setup(); f.window.HoverForTest(W::CodexRow);
        Expect(f.window.TileNameForTest(X) == L"Installer Codex" && f.window.TitleForTest() == L"Choisissez votre agent" && f.Shot(L"codex-1-choisir"), "render: Codex 1 Choisir, its tile hovered (24VS-0)");
        Mode(L"ok"); f.Click(W::CodexRow);
        Expect(AS::Get(X).state == S::Installing && f.window.TargetForTest() == X && f.window.TitleForTest() == L"Installation de Codex" && !f.window.TileEnabledForTest(C), "Codex 2 Installation (Claude Code waits)");
        Expect(f.Shot(L"codex-2-installation"), "render: Codex 2 Installation (24X8-0)");
        // « Installation… » (Paper 253G-0): the Downloads button's morph, drawn off screen at chosen instants
        f.window.MorphForTest(X, 120); Expect(f.Shot(L"morph-installing-start"), "render: the morph starting (the tray curls into a ring, 120 ms)");
        f.window.MorphForTest(X, 900); Expect(f.Shot(L"morph-installing-loop"), "render: the morph looping (the arrow bobbing, 900 ms)");
        Expect(f.window.MorphDrawsForTest() >= 4, "« Installation… »: drawn from the Downloads icon's Lottie layers (assets/lottie/download-*.json)");
        Pump();
        Expect(f.window.EndKindForTest(X) == 1, "« Installation… » ended well: the success morph (into the check) plays");
        const int before = f.window.MorphDrawsForTest();
        f.window.MorphForTest(X, 200); Expect(f.Shot(L"morph-success") && f.window.MorphDrawsForTest() > before, "render: the arrow morphing into the check (200 ms after the end)");
        f.window.MorphForTest(X, 5000); Expect(f.Shot(L"morph-after") && f.window.MorphDrawsForTest() == before + 2, "after the morph: the still « Connecter » mark again");
        Expect(AS::Get(X).state == S::NeedsSignIn && f.window.TitleForTest() == L"Connectez Codex" && Near(f.window.CardTopForTest(), 141.5f), "Codex 3 À connecter: centred again");
        f.window.HoverForTest(W::CodexRow); Expect(f.Shot(L"codex-3-a-connecter"), "render: Codex 3 À connecter, its tile hovered (24YO-0: « Connecter » white)"); f.window.HoverForTest(W::None);
        f.Click(W::CodexRow); f.signInTab = true; f.signInUrl = L"https://auth.openai.com/oauth/authorize?client_id=test";
        Expect(AS::Get(X).state == S::SigningIn && f.window.LinkForTest() == L"Rouvrir la page de connexion ↗" && Near(f.window.CardTopForTest(), 72), "3b: the sign-in page known (its tab closed) → « Rouvrir la page de connexion ↗ »");
        const auto order = f.window.TabOrderForTest();
        Expect(std::find(order.begin(), order.end(), static_cast<int>(W::Reopen)) != order.end() && std::find(order.begin(), order.end(), static_cast<int>(W::CodexRow)) == order.end(), "3b keys: the link in the tab order, not the busy tile");
        Expect(f.Shot(L"codex-3b-connexion"), "render: Codex 3b Connexion dans un autre onglet (263M-0)");
        f.Click(W::Reopen); Expect(f.reopens == 1 && f.window.OpenForTest(), "« Rouvrir la page de connexion ↗ »: the host opens the page again (the window stays open)");
        f.window.TabInFrontForTest(false); Expect(f.window.AsideForTest(), "3b: the reopened page's tab in front → aside");
        Pump(); f.window.TabInFrontForTest(true); Pump();
        Expect(AS::Get(X).Ready() && !f.window.AsideForTest() && f.window.TitleForTest() == L"Codex est prêt" && f.window.PrimaryForTest() == L"Continuer", "Codex 4 Prêt at the return");
        Expect(f.Shot(L"codex-4-pret"), "render: Codex 4 Prêt (2504-0)");
        // A7 reads the same status: Codex is ready, Claude Code is not installed
        f.Setup(Step::Ready);
        Expect(f.window.BodyForTest().find(L"Codex s’occupe du reste.") != std::wstring::npos, "A7: the sentence names the READY agent (Codex)");
        Expect(f.Shot(L"A7-codex-ready"), "render: A7 with Codex ready");
    }
    {
        const auto p = Profile(L"codex-error"); UseProfile(p);
        f.Setup(); Mode(L"inuse"); f.Click(W::CodexRow); Pump();
        Expect(AS::Get(X).state == S::Error && AS::Get(X).reason == R::FileInUse && f.window.MarkForTest(X) == L"Erreur" && f.window.TileNameForTest(X) == L"Réessayer l'installation de Codex", "Codex 5 Erreur (a file in use: the antivirus)");
        Expect(f.Shot(L"codex-5-erreur"), "render: Codex 5 Erreur (251H-0)");
        // A7: nothing ready → never « connecté », never « prêt »
        f.Setup(Step::Ready);
        Expect(f.window.BodyForTest().find(L"Votre agent s’occupe du reste.") != std::wstring::npos, "A7 with no agent ready: « Votre agent »");
        Expect(f.Shot(L"A7-none-ready"), "render: A7 with no agent ready (« Aucun agent prêt »)");
        f.Setup(); f.Click(W::Secondary);
        Expect(f.window.Flow().step() == Step::Ready && f.closed == 0, "A6 « Plus tard » (nothing ready) → A7");
    }
    // ---- Claude Desktop only ----
    {
        const auto p = Profile(L"desktop"); MakeDir(p.local + L"\\AnthropicClaude"); UseProfile(p);
        Expect(AS::Get(C).state == S::NotInstalled && AS::Get(C).reason == R::DesktopOnly, "profile: Claude Desktop only → Claude Code not installed");
        f.Setup();
        Expect(f.window.TileNameForTest(C) == L"Installer Claude Code" && TA::MarkOf(AS::Get(C)) == TA::Mark::Install, "Claude Desktop only: « Installer » (its Claude Code is not the browser's)");
        Mode(L"ok"); f.Click(W::ClaudeRow); Pump();
        Expect(AS::Get(C).state == S::NeedsSignIn, "Claude Desktop only: installing Claude Code works → to connect");
    }
    // ---- Claude Code present and signed in ----
    {
        const auto p = Profile(L"present"); PutCli(ClaudeExe(p), "2.1.325 (Claude Code)", "0"); PutCli(CodexExe(p), "codex-cli 0.166.0", "0"); UseProfile(p);
        f.Setup();
        Expect(f.window.TargetForTest() == C && f.window.TitleForTest() == L"Claude Code est prêt" && f.window.PrimaryForTest() == L"Continuer" && f.window.PrimaryEnabledForTest(), "both ready: Claude Code first, « Continuer » at once");
        Expect(!f.window.TileEnabledForTest(C) && !f.window.TileEnabledForTest(X), "both ready: no tile to click");
        // alone: the chat's first message / the band's reminder
        f.Setup(Step::What, true);
        Expect(f.window.Flow().step() == Step::Connect && !f.window.DotsForTest() && f.window.SecondaryForTest() == L"Plus tard" && f.window.PrimaryForTest() == L"Continuer", "alone: « Ton agent » only, no progress, « Plus tard », « Continuer »");
        Expect(f.Shot(L"alone-claude-ready"), "render: alone, Claude Code ready");
        f.Click(W::Secondary); Expect(f.closed == 1 && f.ending == AO::Ending::Skipped && f.chats == 0, "alone « Plus tard »: closed (skipped), no chat");
        f.Setup(Step::What, true); f.Click(W::Primary);
        Expect(f.closed == 1 && f.ending == AO::Ending::Finished && f.chats == 1 && f.chatAgent == C && f.closedAt < f.chatAt, "alone « Continuer »: closed (finished), then the chat opens with Claude Code");
        f.Setup(Step::What, true); f.window.KeyForTest(VK_RETURN);
        Expect(f.closed == 1 && f.ending == AO::Ending::Finished && f.chats == 1 && f.chatAgent == C, "alone, Entrée: « Continuer »");
        f.Setup(Step::What, true); f.window.KeyForTest(VK_ESCAPE); Expect(f.closed == 1 && f.ending == AO::Ending::Skipped, "alone, Échap: closed (skipped)");
    }
    // ---- keyboard: the tiles, the link ----
    {
        const auto p = Profile(L"keys"); UseProfile(p); Mode(L"fail");
        f.Setup(); f.window.KeyForTest(VK_TAB); Expect(f.window.FocusForTest() == W::ClaudeRow, "keys: Tab reaches the Claude Code tile first");
        f.window.KeyForTest(VK_RIGHT); Expect(f.window.FocusForTest() == W::CodexRow, "keys: → to the Codex tile");
        f.window.KeyForTest(VK_SPACE); Expect(AS::Get(X).state == S::Installing && f.window.TargetForTest() == X, "keys: Espace on the Codex tile installs it (the key is the consent)");
        f.window.KeyForTest(VK_LEFT); Expect(f.window.FocusForTest() == W::CodexRow, "keys: ← does not reach the waiting Claude Code tile");
        f.window.KeyForTest(VK_TAB); Expect(f.window.FocusForTest() == W::Secondary, "keys: while it installs no tile to reach: « Plus tard » next");
        Pump();
        Expect(AS::Get(X).state == S::Error && f.window.ManualOfferedForTest(), "keys: the install failed here: the link is offered");
        for (int i = 0; i < 5; ++i) f.window.KeyForTest(VK_TAB);   // language → close → Claude Code → Codex → the link
        Expect(f.window.FocusForTest() == W::Manual, "keys: the link comes after the tiles");
        f.window.KeyForTest(VK_RETURN); Expect(f.pages == 1 && f.closed == 1, "keys: Entrée on the link: the screen goes, the official page opens");
        f.Setup(); f.window.KeyForTest(VK_TAB); Mode(L"ok"); f.window.KeyForTest(VK_RETURN);
        Expect(AS::Get(C).state == S::Installing, "keys: Entrée on the Claude Code tile installs it"); Pump();
    }
    queued.clear();
}

int wmain(int argc, wchar_t** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (argc < 5) { std::printf("usage: ton_agent_test <repo root> <out dir> <fake-cli.exe> <work dir>\n"); return 2; }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    I18n::SetForProcess(I18n::Locale::Fr);
    Model(); FlowAround();
    Fixture f; f.root = argv[1]; f.out = argv[2]; FakeCli = argv[3]; Work = argv[4];
    CreateDirectoryW(f.out.c_str(), nullptr); MakeDir(Work);
    // A's test hooks (non-release builds, BROWSER_ANNOTATIONS_TEST): the fake installer, the PATH on a test key.
    SetEnvironmentVariableW(L"BROWSER_ANNOTATIONS_TEST", L"1");
    const std::wstring script = std::wstring(argv[1]) + L"\\sandbox\\agents\\fake-install.ps1";
    SetEnvironmentVariableW(L"BROWSER_AGENT_INSTALL_SCRIPT", script.c_str());
    SetEnvironmentVariableW(L"BROWSER_AGENT_PATH_KEY", PathKey());
    SetEnvironmentVariableW(L"BROWSER_FAKE_CLI", FakeCli.c_str());
    Expect(Exists(script) && Exists(FakeCli), "setup: the fake installer and the fake CLI");
    Screens(f);
    // English
    I18n::SetForProcess(I18n::Locale::En);
    {
        const auto p = Profile(L"english"); PutCli(CodexExe(p), "codex-cli 0.166.0", "1"); UseProfile(p);
        f.Setup(); Expect(f.window.TitleForTest() == L"Choose your agent" && f.window.TileNameForTest(C) == L"Install Claude Code" && f.window.SecondaryForTest() == L"Later", "English: « Choose your agent », the tile « Install Claude Code », « Later »");
        Expect(f.Shot(L"en-choose"), "render: English");
    }
    I18n::SetForProcess(I18n::Locale::Fr);
    AS::Shutdown();
    RegDeleteTreeW(HKEY_CURRENT_USER, PathKey());
    std::printf("%d checks, %d failures\n", checks, failures);
    CoUninitialize();
    return failures ? 1 : 0;
}
