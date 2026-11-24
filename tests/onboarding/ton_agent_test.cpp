// « Ton agent » (worker B): the screen's state machine and its renders, headless. Paper « Browser Design » › « Onboarding
// agents — Ton agent (maquette prête) ». Never the real installers, CLIs, sign-ins, PATH or browser profile:
//   - the pure decisions (src/annotation-onboarding-agent.h) and the tutorial's flow around A6 / alone;
//   - the production window (src/annotation-onboarding.h) on the REAL hub (src/agent-status.h) over A's fake profiles:
//     temporary folders for %USERPROFILE% / %LOCALAPPDATA% / %APPDATA%, the fake CLI (tests/acp/fake_cli.cpp), the fake
//     official installer (sandbox/agents/fake-install.ps1, BROWSER_FAKE_INSTALL_MODE), the user PATH on a test key under
//     HKCU\Software\BrowserTests, the CLI's own sign-in faked (no console). The hub's work is queued and pumped by the
//     test, so « Installation… » can be seen and drawn;
//   - Claude Code present / absent / Claude Desktop only, Codex signed in / out, install success / failure, retry,
//     « Installer moi-même », « Plus tard », « Retour », « Ouvrir le chat », alone (the chat, the band's reminder);
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
    Expect(TA::MarkOf(St(C, S::NotInstalled)) == TA::Mark::Install && TA::MarkOf(St(C, S::NotInstalled, R::DesktopOnly)) == TA::Mark::Install && TA::MarkOf(St(C, S::NotInstalled, R::TooOld)) == TA::Mark::Install,
           "mark: not installed (also Claude Desktop only, too old) → « Installer »");
    Expect(TA::MarkOf(St(C, S::Installing)) == TA::Mark::Installing && TA::MarkOf(St(X, S::NeedsSignIn)) == TA::Mark::Connect && TA::MarkOf(St(X, S::SigningIn)) == TA::Mark::Connect, "mark: « Installation… », « Connecter » (also while signing in)");
    Expect(TA::MarkOf(St(C, S::Ready)) == TA::Mark::Ready && TA::MarkOf(St(C, S::Error, R::Network)) == TA::Mark::Error && TA::MarkOf(St(C, S::Unknown)) == TA::Mark::Unknown, "mark: « ✓ Prêt », « Erreur », nothing while the first detection runs");
    Expect(TA::LookOf(St(C, S::NotInstalled), true) == TA::Look::Rest, "look: the target only waiting for « Installer » is not outlined (1 Choisir)");
    Expect(TA::LookOf(St(C, S::Installing), true) == TA::Look::Current && TA::LookOf(St(X, S::NeedsSignIn), true) == TA::Look::Current && TA::LookOf(St(C, S::Ready), true) == TA::Look::Current, "look: cyan outline once acted on (installing, to connect, ready)");
    Expect(TA::LookOf(St(C, S::Error, R::Network), true) == TA::Look::Danger && TA::LookOf(St(C, S::Error, R::Network), false) == TA::Look::Rest && TA::LookOf(St(X, S::NeedsSignIn), false) == TA::Look::Rest, "look: danger outline on the failed target; never on the other tile");
    Expect(TA::InkOf(St(X, S::NeedsSignIn), false) == TA::Ink::Grey && TA::InkOf(St(X, S::NeedsSignIn), true) == TA::Ink::Accent && TA::InkOf(St(C, S::Error, R::Disk), false) == TA::Ink::Danger, "ink: grey at rest, the light accent when current, danger for « Erreur »");
    auto b = [](S st) { return TA::ButtonOf(St(C, st)); };
    Expect(b(S::NotInstalled).action == TA::Action::Install && !b(S::NotInstalled).dimmed && b(S::Installing).dimmed && b(S::Installing).action == TA::Action::None && b(S::Installing).label == TA::Label::Installing,
           "button: « Installer X » acts; « Installation… » dimmed, inert");
    Expect(b(S::NeedsSignIn).action == TA::Action::SignIn && b(S::SigningIn).dimmed && b(S::Ready).action == TA::Action::OpenChat && b(S::Error).action == TA::Action::Retry && b(S::Unknown).dimmed,
           "button: « Se connecter à X », busy while the console is open, « Ouvrir le chat », « Réessayer », nothing during the first detection");
    Expect(TA::HeadingOf(St(C, S::NotInstalled)) == TA::Heading::Choose && TA::HeadingOf(St(C, S::Error, R::Network)) == TA::Heading::Choose && TA::HeadingOf(St(C, S::Installing)) == TA::Heading::Installing &&
           TA::HeadingOf(St(X, S::NeedsSignIn)) == TA::Heading::SignIn && TA::HeadingOf(St(X, S::Ready)) == TA::Heading::Ready, "heading: Choisissez (also after a failure), Installation de, Connectez, est prêt");
    Expect(TA::TroubleOf(St(C, S::Error, R::Network)) == TA::Trouble::Install && TA::ManualOffered(St(C, S::Error, R::NotFoundAfterInstall)) && TA::TroubleOf(St(C, S::Error, R::SignInFailed)) == TA::Trouble::SignIn &&
           !TA::ManualOffered(St(C, S::Error, R::SignInTimeout)) && TA::TroubleOf(St(C, S::Ready)) == TA::Trouble::None, "trouble: an install failure offers « Installer moi-même », a sign-in failure does not");
    Expect(TA::DefaultTarget(St(C, S::NotInstalled), St(X, S::NeedsSignIn)) == C, "target: Claude Code first, even when Codex only needs a sign-in (Claude Code 1 Choisir)");
    Expect(TA::DefaultTarget(St(C, S::NotInstalled), St(X, S::Ready)) == X && TA::DefaultTarget(St(C, S::Ready), St(X, S::Ready)) == C, "target: a ready agent (Claude Code first)");
    Expect(TA::DefaultTarget(St(C, S::Error, R::Network), St(X, S::Installing)) == X && TA::DefaultTarget(St(C, S::NotInstalled), St(X, S::Error, R::Disk)) == X, "target: busy first, then ready, then failed");
    TA::Screen sc; Expect(!sc.Picked() && sc.Target(St(C, S::NotInstalled), St(X, S::NotInstalled)) == C, "target: no tile picked at the start");
    sc.Pick(X); Expect(sc.Target(St(C, S::Ready), St(X, S::NotInstalled)) == X, "target: a click on a tile makes it the target (Codex 1 Choisir)");
    namespace K = TA::Card;
    Expect(Near(K::Height(TA::Mark::Install, TA::Mark::Connect, TA::Trouble::None), 157) && Near(K::Top(St(C, S::NotInstalled), 157), 141.5f), "card: 157 high, centred on y 220 (Choisir: « top: calc(50% - 70px) »)");
    Expect(Near(K::Height(TA::Mark::Ready, TA::Mark::Connect, TA::Trouble::None), 160) && Near(K::Top(St(C, S::Ready), 160), 140) && Near(K::Top(St(C, S::Installing), 157), 123), "card: Prêt at 140 (the « ✓ Prêt » row 17), Installation at 123");
    Expect(Near(K::Height(TA::Mark::Error, TA::Mark::Connect, TA::Trouble::Install), 228) && Near(K::Top(St(C, S::Error, R::Network), 228), 72), "card: Erreur 228 high at 72 (the two lines and the link)");
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
        if (Exists(exe)) WriteText(exe + L".login", "0");
        return true;
    };
    SetEnvironmentVariableW(L"FAKE_HOME", p.home.c_str()); SetEnvironmentVariableW(L"FAKE_LOCAL", p.local.c_str());
    AS::Init(h); Pump();
}
static void Mode(const wchar_t* m) { SetEnvironmentVariableW(L"BROWSER_FAKE_INSTALL_MODE", m); }

struct Fixture {
    UI::Window window; std::wstring root, out;
    int closed = 0, chats = 0, pages = 0, order = 0, closedAt = 0, chatAt = 0; AO::Ending ending = AO::Ending::None; RT::Agent chatAgent = C; std::wstring page;
    void Setup(Step start = Step::Connect, bool alone = false) {
        UI::Options o; o.reduced = true; o.start = start; o.alone = alone;
        o.hooks.closed = [this](AO::Ending e, bool) { ++closed; ending = e; closedAt = ++order; };
        o.hooks.openChat = [this](RT::Agent a) { ++chats; chatAgent = a; chatAt = ++order; };
        o.hooks.openPage = [this](const std::wstring& url) { ++pages; page = url; };
        window.SetupForTest(root, o); window.SetShaderSecondsForTest(0);
        closed = chats = pages = order = closedAt = chatAt = 0; ending = AO::Ending::None;
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
        Expect(f.window.TitleForTest() == L"Choisissez votre agent" && f.window.PrimaryForTest() == L"Installer Claude Code" && f.window.SecondaryForTest() == L"Plus tard" && f.window.TargetForTest() == C,
               "1 Choisir: « Choisissez votre agent », « Installer Claude Code », « Plus tard »");
        Expect(f.window.BodyForTest() == L"Le chat de Browser utilise Claude Code ou Codex,\ninstallés sur cet ordinateur.", "1 Choisir: the board's two lines");
        Expect(Near(f.window.CardTopForTest(), 141.5f) && !f.window.ManualOfferedForTest() && f.window.DotsForTest(), "1 Choisir: the card centred (top 141.5), no link, the tutorial's progress");
        Expect(!Exists(ClaudeExe(p)) && queued.empty() && AS::Get(C).state == S::NotInstalled, "consent: the screen shown installs nothing by itself");
        Expect(f.Shot(L"claude-1-choisir"), "render: Claude Code 1 Choisir (22ZR-0)");
        Mode(L"ok"); f.Click(W::Primary);
        Expect(AS::Get(C).state == S::Installing && !Exists(ClaudeExe(p)), "2 Installation: the click is the consent — installing (the fake installer queued)");
        Expect(f.window.TitleForTest() == L"Installation de Claude Code" && f.window.PrimaryForTest() == L"Installation…" && !f.window.PrimaryEnabledForTest(), "2 Installation: « Installation de Claude Code », « Installation… » dimmed");
        Expect(f.window.BodyForTest() == L"Ça peut prendre quelques minutes.\nBrowser vérifie tout seul quand c'est fini." && Near(f.window.CardTopForTest(), 123), "2 Installation: the board's lines, the card at 123");
        Expect(f.Shot(L"claude-2-installation"), "render: Claude Code 2 Installation (2312-0)");
        f.Click(W::Primary); Expect(AS::Get(C).state == S::Installing, "2 Installation: the dimmed button does nothing");
        Pump();
        Expect(Exists(ClaudeExe(p)) && AS::Get(C).state == S::NeedsSignIn, "install success: the fake official installer put Claude Code there → to connect");
        Expect(f.window.TitleForTest() == L"Connectez Claude Code" && f.window.PrimaryForTest() == L"Se connecter à Claude Code", "Claude Code to connect: « Connectez Claude Code », « Se connecter à Claude Code »");
        Expect(f.window.BodyForTest().find(L"compte Claude payant") != std::wstring::npos && f.window.BodyForTest().find(L"Pro, Max, Team ou Enterprise") != std::wstring::npos, "Claude Code to connect: its paid plan said at the sign-in");
        Expect(f.Shot(L"claude-to-connect"), "render: Claude Code to connect (no board: the Codex « À connecter » one for Claude Code)");
        f.Click(W::Primary); Expect(AS::Get(C).state == S::SigningIn && !f.window.PrimaryEnabledForTest(), "sign-in: the CLI's own sign-in runs, the button waits");
        Pump();
        Expect(AS::Get(C).Ready() && f.window.TitleForTest() == L"Claude Code est prêt" && f.window.PrimaryForTest() == L"Ouvrir le chat" && f.window.SecondaryForTest() == L"Retour", "3 Prêt: « Claude Code est prêt », « Ouvrir le chat », « Retour »");
        Expect(f.window.BodyForTest() == L"Il travaille sur vos projets,\ndirectement depuis le chat de Browser." && Near(f.window.CardTopForTest(), 140), "3 Prêt: the board's lines, the card at 140");
        Expect(f.Shot(L"claude-3-pret"), "render: Claude Code 3 Prêt (232D-0)");
        f.Click(W::Secondary); Expect(f.window.Flow().step() == Step::Where, "3 Prêt « Retour » → A5");
        f.Click(W::Primary); Expect(f.window.Flow().step() == Step::Connect && f.window.TargetForTest() == C, "A5 « Suivant » → A6, Claude Code still the target");
        f.Click(W::Primary);
        Expect(f.closed == 1 && f.ending == AO::Ending::Finished && f.chats == 1 && f.chatAgent == C && f.closedAt < f.chatAt, "« Ouvrir le chat »: the tutorial ends (finished), then the chat opens with Claude Code");
        // Codex now: sign in from the screen (the tile picked)
        f.Setup(); f.Click(W::CodexRow);
        Expect(f.window.TargetForTest() == X && f.window.TitleForTest() == L"Connectez Codex" && f.window.PrimaryForTest() == L"Se connecter à Codex", "a click on the Codex tile: « Connectez Codex », « Se connecter à Codex »");
        Expect(f.window.BodyForTest() == L"Connectez-vous une fois avec votre compte ChatGPT,\nla page s'ouvre dans votre navigateur web." && f.window.BodyForTest().find(L"Plus") == std::wstring::npos, "Codex: its account, nothing about plans");
    }
    // ---- Claude Code: the install fails, retry, « Installer moi-même » ----
    {
        const auto p = Profile(L"claude-error"); PutCli(CodexExe(p), "codex-cli 0.166.0", "1"); UseProfile(p);
        f.Setup(); Mode(L"network"); f.Click(UI::Window::Primary); Pump();
        Expect(AS::Get(C).state == S::Error && AS::Get(C).reason == R::Network, "install failure (network, the fake installer) → error");
        Expect(f.window.TitleForTest() == L"Choisissez votre agent" && f.window.PrimaryForTest() == L"Réessayer" && f.window.SecondaryForTest() == L"Plus tard" && f.window.ManualOfferedForTest(), "4 Erreur: « Choisissez votre agent », « Réessayer », « Installer moi-même ↗ »");
        Expect(f.window.TroubleForTest() == L"L'installation n'a pas abouti.\nUn antivirus, un proxy d'entreprise ou PowerShell a pu la bloquer." && Near(f.window.CardTopForTest(), 72), "4 Erreur: the board's two lines, the card at 72");
        Expect(f.Shot(L"claude-4-erreur"), "render: Claude Code 4 Erreur (233O-0)");
        f.Click(UI::Window::Manual);
        Expect(f.pages == 1 && f.page == AS::ManualInstall(C).url && f.window.ManualClicksForTest() == 1 && AS::Get(C).state == S::Error, "« Installer moi-même ↗ »: the official page (the command copied), nothing installed by us");
        Expect(f.closed == 1 && f.ending == AO::Ending::Skipped && f.chats == 0, "« Installer moi-même ↗ »: the screen goes first (the page would be hidden behind it)");
        f.Setup(); Expect(f.window.TargetForTest() == C && f.window.PrimaryForTest() == L"Réessayer", "opened again: the failure is still the target, « Réessayer »");
        Mode(L"policy"); f.Click(UI::Window::Primary); Expect(AS::Get(C).state == S::Installing, "« Réessayer » (consent): the install again"); Pump();
        Expect(AS::Get(C).state == S::Error && AS::Get(C).reason == R::ExecutionPolicy, "retry: PowerShell's policy → error again");
        Mode(L"ok"); f.Click(UI::Window::Primary); Pump();
        Expect(AS::Get(C).state == S::NeedsSignIn && Exists(ClaudeExe(p)), "retry: then it works → installed, to connect");
    }
    // ---- Codex row (both absent; the Codex tile picked) ----
    {
        const auto p = Profile(L"codex-row"); UseProfile(p);
        f.Setup(); f.Click(UI::Window::CodexRow);
        Expect(f.window.TargetForTest() == X && f.window.PrimaryForTest() == L"Installer Codex" && f.window.TitleForTest() == L"Choisissez votre agent", "Codex 1 Choisir: the tile picked → « Installer Codex », no outline yet");
        Expect(f.Shot(L"codex-1-choisir"), "render: Codex 1 Choisir (24VS-0)");
        Mode(L"ok"); f.Click(UI::Window::Primary);
        Expect(AS::Get(X).state == S::Installing && f.window.TitleForTest() == L"Installation de Codex", "Codex 2 Installation");
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
        Expect(f.Shot(L"codex-3-a-connecter"), "render: Codex 3 À connecter (24YO-0)");
        f.Click(UI::Window::Primary); Pump();
        Expect(AS::Get(X).Ready() && f.window.TitleForTest() == L"Codex est prêt" && f.window.PrimaryForTest() == L"Ouvrir le chat", "Codex 4 Prêt");
        Expect(f.Shot(L"codex-4-pret"), "render: Codex 4 Prêt (2504-0)");
        // A7 reads the same status: Codex is ready, Claude Code is not installed
        f.Setup(Step::Ready);
        Expect(f.window.BodyForTest().find(L"Codex s’occupe du reste.") != std::wstring::npos, "A7: the sentence names the READY agent (Codex)");
        Expect(f.Shot(L"A7-codex-ready"), "render: A7 with Codex ready");
    }
    {
        const auto p = Profile(L"codex-error"); UseProfile(p);
        f.Setup(); f.Click(UI::Window::CodexRow); Mode(L"inuse"); f.Click(UI::Window::Primary); Pump();
        Expect(AS::Get(X).state == S::Error && AS::Get(X).reason == R::FileInUse && f.window.PrimaryForTest() == L"Réessayer", "Codex 5 Erreur (a file in use: the antivirus)");
        Expect(f.Shot(L"codex-5-erreur"), "render: Codex 5 Erreur (251H-0)");
        // A7: nothing ready → never « connecté », never « prêt »
        f.Setup(Step::Ready);
        Expect(f.window.BodyForTest().find(L"Votre agent s’occupe du reste.") != std::wstring::npos, "A7 with no agent ready: « Votre agent »");
        Expect(f.Shot(L"A7-none-ready"), "render: A7 with no agent ready (« Aucun agent prêt »)");
        f.Setup(); f.Click(UI::Window::Secondary);
        Expect(f.window.Flow().step() == Step::Ready && f.closed == 0, "A6 « Plus tard » (nothing ready) → A7");
    }
    // ---- Claude Desktop only ----
    {
        const auto p = Profile(L"desktop"); MakeDir(p.local + L"\\AnthropicClaude"); UseProfile(p);
        Expect(AS::Get(C).state == S::NotInstalled && AS::Get(C).reason == R::DesktopOnly, "profile: Claude Desktop only → Claude Code not installed");
        f.Setup();
        Expect(f.window.PrimaryForTest() == L"Installer Claude Code" && TA::MarkOf(AS::Get(C)) == TA::Mark::Install, "Claude Desktop only: « Installer » (its Claude Code is not the browser's)");
        Mode(L"ok"); f.Click(UI::Window::Primary); Pump();
        Expect(AS::Get(C).state == S::NeedsSignIn, "Claude Desktop only: installing Claude Code works → to connect");
    }
    // ---- Claude Code present and signed in ----
    {
        const auto p = Profile(L"present"); PutCli(ClaudeExe(p), "2.1.325 (Claude Code)", "0"); PutCli(CodexExe(p), "codex-cli 0.166.0", "0"); UseProfile(p);
        f.Setup();
        Expect(f.window.TargetForTest() == C && f.window.TitleForTest() == L"Claude Code est prêt" && f.window.PrimaryForTest() == L"Ouvrir le chat", "both ready: Claude Code first, « Ouvrir le chat » at once");
        f.Click(UI::Window::CodexRow); f.Click(UI::Window::Primary);
        Expect(f.chats == 1 && f.chatAgent == X, "the Codex tile, then « Ouvrir le chat »: the chat opens with Codex");
        // alone: the chat's first message / the band's reminder
        f.Setup(Step::What, true);
        Expect(f.window.Flow().step() == Step::Connect && !f.window.DotsForTest() && f.window.SecondaryForTest() == L"Plus tard", "alone: « Ton agent » only, no progress, « Plus tard » even when ready");
        Expect(f.Shot(L"alone-claude-ready"), "render: alone, Claude Code ready");
        f.Click(UI::Window::Secondary); Expect(f.closed == 1 && f.ending == AO::Ending::Skipped && f.chats == 0, "alone « Plus tard »: closed (skipped), no chat");
        f.Setup(Step::What, true); f.window.KeyForTest(VK_RETURN);
        Expect(f.closed == 1 && f.ending == AO::Ending::Finished && f.chats == 1 && f.chatAgent == C, "alone, Entrée: « Ouvrir le chat » (finished, the chat with Claude Code)");
        f.Setup(Step::What, true); f.window.KeyForTest(VK_ESCAPE); Expect(f.closed == 1 && f.ending == AO::Ending::Skipped, "alone, Échap: closed (skipped)");
    }
    // ---- keyboard: the tiles, the link ----
    {
        const auto p = Profile(L"keys"); UseProfile(p); Mode(L"fail");
        f.Setup(); f.window.KeyForTest(VK_TAB); Expect(f.window.FocusForTest() == UI::Window::ClaudeRow, "keys: Tab reaches the Claude Code tile first");
        f.window.KeyForTest(VK_RIGHT); Expect(f.window.FocusForTest() == UI::Window::CodexRow, "keys: → to the Codex tile");
        f.window.KeyForTest(VK_SPACE); Expect(f.window.TargetForTest() == X && f.window.PrimaryForTest() == L"Installer Codex", "keys: Espace picks it");
        f.window.KeyForTest(VK_TAB); Expect(f.window.FocusForTest() == UI::Window::Secondary, "keys: no link before a failure: « Plus tard » next");
        f.window.KeyForTest(VK_TAB); f.window.KeyForTest(VK_RETURN); Pump();
        Expect(AS::Get(X).state == S::Error && f.window.ManualOfferedForTest(), "keys: Entrée on « Installer Codex » installs (fails here): the link is offered");
        for (int i = 0; i < 5; ++i) f.window.KeyForTest(VK_TAB);   // « Réessayer » → language → close → Claude Code → Codex → the link
        Expect(f.window.FocusForTest() == UI::Window::Manual, "keys: the link comes after the tiles");
        f.window.KeyForTest(VK_RETURN); Expect(f.pages == 1 && f.closed == 1, "keys: Entrée on the link: the screen goes, the official page opens");
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
        f.Setup(); Expect(f.window.TitleForTest() == L"Choose your agent" && f.window.PrimaryForTest() == L"Install Claude Code", "English: « Choose your agent », « Install Claude Code »");
        Expect(f.Shot(L"en-choose"), "render: English");
    }
    I18n::SetForProcess(I18n::Locale::Fr);
    AS::Shutdown();
    RegDeleteTreeW(HKEY_CURRENT_USER, PathKey());
    std::printf("%d checks, %d failures\n", checks, failures);
    CoUninitialize();
    return failures ? 1 : 0;
}
