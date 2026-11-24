// « Ton agent » (design/ton-agent-interface.md): the one status of the chat's agents and the official installers, tested
// WITHOUT the real installers, the real CLIs or the user's real PATH. Everything outside runs on fakes:
//   - detections are injected (the detection matrix), or real detections over fake claude.exe / codex.exe (fake-cli.exe
//     copied into temporary folders standing for %USERPROFILE% / %LOCALAPPDATA%);
//   - installers are fake PowerShell scripts in a temporary folder (success, failure kinds, timeout);
//   - the user PATH is a test key under HKCU\Software\BrowserTests (never HKCU\Environment).
// Usage: agent_status_test.exe <fake-cli.exe> <work dir> <sandbox\agents\fake-install.ps1>
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <psapi.h>

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "agent-install.h"
#include "agent-status.h"
#include "chat-agent-status.h"

namespace AS = AgentStatus; namespace AI = AgentInstall; namespace RT = AgentRuntime;

static int failures = 0, checks = 0;
static void Expect(bool ok, const char* what) { ++checks; std::printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++failures; }

static RT::Detection Det(RT::Agent a, RT::CliState cli, RT::Login login, const char* version = "2.1.325") {
    RT::Detection d; d.agent = a; d.cli.state = cli; d.login = login;
    if (cli != RT::CliState::Missing) { d.cli.version = version; d.cli.path = L"C:\\fake\\cli.exe"; }
    return d;
}

static void DetectionMatrix() {
    using S = AS::State; using R = AS::Reason;
    auto st = [](const RT::Detection& d, bool desktop) { return AS::FromDetection(d, desktop); };
    Expect(st(Det(RT::Agent::Claude, RT::CliState::Missing, RT::Login::Unknown), false).state == S::NotInstalled, "matrix: Claude Code absent → not installed");
    {
        const auto s = st(Det(RT::Agent::Claude, RT::CliState::Missing, RT::Login::Unknown), true);
        Expect(s.state == S::NotInstalled && s.reason == R::DesktopOnly && s.desktop, "matrix: Claude Desktop only → not installed (desktopOnly)");
    }
    Expect(st(Det(RT::Agent::Codex, RT::CliState::Missing, RT::Login::Unknown), true).reason == R::None, "matrix: Codex absent: Claude Desktop says nothing about it");
    {
        const auto s = st(Det(RT::Agent::Claude, RT::CliState::TooOld, RT::Login::SignedIn, "2.0.1"), false);
        Expect(s.state == S::NotInstalled && s.reason == R::TooOld && s.version == "2.0.1", "matrix: too old → not installed (tooOld), the version kept");
    }
    Expect(st(Det(RT::Agent::Claude, RT::CliState::Found, RT::Login::SignedOut), false).state == S::NeedsSignIn, "matrix: installed, signed out → needs sign-in");
    Expect(st(Det(RT::Agent::Claude, RT::CliState::Found, RT::Login::SignedIn), true).state == S::Ready, "matrix: installed, signed in → ready (Claude Desktop too: fine)");
    Expect(st(Det(RT::Agent::Codex, RT::CliState::Untested, RT::Login::SignedIn), false).state == S::Ready, "matrix: a newer release line → ready");
    Expect(st(Det(RT::Agent::Codex, RT::CliState::Found, RT::Login::Unknown), false).state == S::Ready, "matrix: sign-in unknown → ready (the session start tells)");
    Expect(std::string(AS::StateName(S::NeedsSignIn)) == "needsSignIn" && std::string(AS::ReasonName(R::DesktopOnly)) == "desktopOnly", "names: stable identifiers");
    Expect(std::wstring(AS::ManualInstall(RT::Agent::Claude).command) == L"irm https://claude.ai/install.ps1 | iex", "manual: Claude Code's official command");
    Expect(std::wstring(AS::ManualInstall(RT::Agent::Codex).command).find(L"https://chatgpt.com/codex/install.ps1") != std::wstring::npos, "manual: Codex's official command");
}

// The chat shows the one status (src/chat-agent-status.h): never « Installé » while the CLI is missing.
static void ChatView() {
    using S = AS::State; using R = AS::Reason; using T = ChatAgentStatus::Text;
    auto v = [](S state, R reason = R::None, bool runtime = false) { AS::Status s; s.state = state; s.reason = reason; s.runtimeReady = runtime; return ChatAgentStatus::Of(s); };
    Expect(std::string(v(S::NotInstalled).status) == "notInstalled" && !v(S::NotInstalled).cli, "chat: not installed → « Pas installé », no CLI");
    Expect(std::string(v(S::NotInstalled, R::DesktopOnly).status) == "notInstalled", "chat: Claude Desktop only → « Pas installé »");
    Expect(std::string(v(S::NotInstalled, R::TooOld).status) == "error" && v(S::NotInstalled, R::TooOld).text == T::TooOld, "chat: too old → « Version trop ancienne »");
    Expect(std::string(v(S::NeedsSignIn).status) == "notConnected" && v(S::NeedsSignIn).cli, "chat: installed, signed out → « Pas connecté »");
    Expect(std::string(v(S::Ready, R::None, true).status) == "ready" && v(S::Ready, R::None, true).text == T::Installed, "chat: ready, runtime downloaded → « Installé »");
    Expect(v(S::Ready).text == T::FirstUse, "chat: ready, runtime still to download → « first use »");
    Expect(std::string(v(S::Installing).status) == "preparing" && v(S::Installing).text == T::Installing && !v(S::Installing).cli, "chat: installing → « Installation… »");
    Expect(std::string(v(S::Error, R::NotFoundAfterInstall).status) == "notInstalled" && !v(S::Error, R::NotFoundAfterInstall).cli, "chat (the « Installé » bug): an install that left no CLI is « Pas installé », never « Installé »");
    Expect(std::string(v(S::Error, R::SignInFailed).status) == "notConnected" && v(S::Error, R::SignInFailed).cli, "chat: a failed sign-in → still to sign in");
    Expect(!v(S::Unknown).known && std::string(v(S::Unknown).status) == "starting", "chat: before the first detection → « starting », unknown");
    Expect(std::string(v(S::SigningIn).status) == "preparing" && v(S::SigningIn).text == T::SigningIn && v(S::SigningIn).cli, "chat: signing in → « Connexion… », not sendable (never « starting »)");
    // « Query closed before response received »: the turn's card says the real cause once the agent is checked again
    using E = Acp::ErrorCode;
    auto st = [](S state, R reason = R::None) { AS::Status s; s.state = state; s.reason = reason; return s; };
    Expect(ChatAgentStatus::Rechecked(E::ProcessExited) && ChatAgentStatus::Rechecked(E::AuthRequired) && !ChatAgentStatus::Rechecked(E::RateLimited), "turn failure: a stopped agent or a refused sign-in is checked again first");
    Expect(ChatAgentStatus::FailureCause(st(S::NeedsSignIn), E::ProcessExited) == E::AuthRequired, "turn failure: CLI closed, signed out meanwhile → « pas connecté » + Se connecter");
    Expect(ChatAgentStatus::FailureCause(st(S::NotInstalled), E::ProcessExited) == E::NotInstalled, "turn failure: CLI gone → « pas installé » + Installer (the screen)");
    Expect(ChatAgentStatus::FailureCause(st(S::NotInstalled, R::TooOld), E::ProcessExited) == E::CliTooOld, "turn failure: CLI too old → « trop ancienne » + Installer");
    Expect(ChatAgentStatus::FailureCause(st(S::Ready), E::ProcessExited) == E::ProcessExited, "turn failure: still ready → « X s'est arrêté » + Réessayer (never the generic card)");
    Expect(ChatAgentStatus::FailureCause(st(S::Error, R::SignInFailed), E::AuthRequired) == E::AuthRequired, "turn failure: a failed sign-in → « pas connecté »");
}

static void Classification() {
    using E = AI::Error;
    Expect(AI::Classify(0, "anything") == E::None, "classify: exit 0 → none");
    Expect(AI::Classify(1, "File C:\\x\\install.ps1 cannot be loaded because running scripts is disabled on this system.") == E::ExecutionPolicy, "classify: scripts disabled → execution policy");
    Expect(AI::Classify(1, "The process cannot access the file 'claude.exe' because it is being used by another process.") == E::FileInUse, "classify: used by another process → file in use (antivirus)");
    Expect(AI::Classify(1, "Invoke-WebRequest : Unable to connect to the remote server") == E::Network, "classify: unable to connect → network");
    Expect(AI::Classify(1, "The remote server returned an error: (407) Proxy Authentication Required.") == E::Network, "classify: proxy 407 → network");
    Expect(AI::Classify(3, "Something else went wrong") == E::ScriptFailed, "classify: anything else → script failed");
}

static void PathHelpers() {
    Expect(AI::PathContains(L"C:\\a;C:\\Users\\me\\.local\\bin\\;D:\\b", L"c:\\users\\ME\\.local\\bin"), "path: found case-insensitively, trailing slash ignored");
    Expect(!AI::PathContains(L"C:\\a;C:\\Users\\me\\.local\\binx", L"C:\\Users\\me\\.local\\bin"), "path: a longer folder is not the folder");
    Expect(AI::PathContains(L"%USERPROFILE%\\.local\\bin", L"C:\\Users\\me\\.local\\bin", [](const std::wstring& s) { std::wstring r = s; const auto at = r.find(L"%USERPROFILE%"); if (at != std::wstring::npos) r.replace(at, 13, L"C:\\Users\\me"); return r; }),
           "path: variables expanded before comparing");
    Expect(!AI::PathContains(L"", L"C:\\x"), "path: an empty PATH holds nothing");
    // The real registry, on a test key (never HKCU\Environment).
    AI::PathTarget t; t.key = L"Software\\BrowserTests\\AgentInstall-" + std::to_wstring(GetCurrentProcessId()); t.broadcast = false;
    HKEY k = nullptr; RegCreateKeyExW(HKEY_CURRENT_USER, t.key.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr);
    const std::wstring before = L"C:\\Windows;%USERPROFILE%\\Tools";
    RegSetValueExW(k, L"Path", 0, REG_EXPAND_SZ, reinterpret_cast<const BYTE*>(before.c_str()), DWORD((before.size() + 1) * 2)); RegCloseKey(k);
    bool added = false; std::string err;
    Expect(AI::EnsureOnUserPath(L"C:\\Users\\Fake\\.local\\bin", t, &added, &err) && added, "user PATH: the folder added when missing");
    wchar_t buf[512]; DWORD size = sizeof buf, type = 0;
    RegGetValueW(HKEY_CURRENT_USER, t.key.c_str(), L"Path", RRF_RT_ANY | RRF_NOEXPAND, &type, buf, &size);
    Expect(type == REG_EXPAND_SZ && std::wstring(buf) == before + L";C:\\Users\\Fake\\.local\\bin", "user PATH: appended, REG_EXPAND_SZ and the old entries kept");
    Expect(AI::EnsureOnUserPath(L"c:\\users\\fake\\.local\\bin\\", t, &added, &err) && !added, "user PATH: not added twice");
    RegDeleteTreeW(HKEY_CURRENT_USER, t.key.c_str());
}

static void Hub() {
    // A synchronous host with injected detections: what the browser would see.
    RT::Detection claude = Det(RT::Agent::Claude, RT::CliState::Missing, RT::Login::Unknown), codex = Det(RT::Agent::Codex, RT::CliState::Found, RT::Login::SignedOut);
    int installsRun = 0, signIns = 0, notes = 0;
    AS::Host h;
    h.environment = [] { RT::Environment e; e.dataRoot = L"C:\\nowhere"; return e; };
    h.detect = [&](const RT::Environment&, RT::Agent a) { return a == RT::Agent::Claude ? claude : codex; };
    h.signIn = [&](const RT::Environment&, RT::Agent, unsigned, std::string*) { ++signIns; codex.login = RT::Login::SignedIn; return true; };
    AS::Init(h);
    const int id = AS::Subscribe([&](const AS::Status&) { ++notes; });
    Expect(AS::Get(RT::Agent::Claude).state == AS::State::NotInstalled && AS::Get(RT::Agent::Codex).state == AS::State::NeedsSignIn, "hub: the first detection gives both statuses");
    Expect(!AS::AnyReady() && !AS::FirstReady(), "hub: nothing ready");
    AS::Consent none; none.agent = RT::Agent::Claude;
    Expect(!AS::Install(none) && AS::Get(RT::Agent::Claude).state == AS::State::NotInstalled && installsRun == 0, "consent: no install without the click");
    Expect(!AS::Install(AS::UserClicked(RT::Agent::Codex)), "install: refused for an installed agent");
    Expect(AS::SignIn(RT::Agent::Codex) && signIns == 1 && AS::Get(RT::Agent::Codex).Ready(), "sign-in: console, then a new detection → ready");
    Expect(AS::AnyReady() && AS::FirstReady() == RT::Agent::Codex && notes >= 2, "hub: listeners told of every change");
    AS::Unsubscribe(id);
    AS::Shutdown();
}

// ---------------------------------------------------------------- the installer, on the FAKE script (sandbox/agents/fake-install.ps1)
static std::wstring Work, FakeCli, FakeScript;
static bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
static void MakeDir(const std::wstring& d) { std::wstring acc; for (size_t i = 0; i <= d.size(); ++i) { if (i == d.size() || d[i] == L'\\') { if (acc.size() > 2) CreateDirectoryW(acc.c_str(), nullptr); } if (i < d.size()) acc += d[i]; } }
static void WriteText(const std::wstring& p, const char* text) { HANDLE f = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr); DWORD put = 0; WriteFile(f, text, DWORD(strlen(text)), &put, nullptr); CloseHandle(f); }
static AI::PathTarget TestPath() { AI::PathTarget t; t.key = L"Software\\BrowserTests\\AgentPath-" + std::to_wstring(GetCurrentProcessId()); t.broadcast = false; return t; }
static std::wstring PathValue(const AI::PathTarget& t) { wchar_t buf[2048] = {}; DWORD size = sizeof buf; RegGetValueW(HKEY_CURRENT_USER, t.key.c_str(), t.value.c_str(), RRF_RT_ANY | RRF_NOEXPAND, nullptr, buf, &size); return buf; }

static AI::Result RunFake(const std::wstring& name, const wchar_t* mode, RT::Agent agent, unsigned timeoutMs, const std::wstring& home, const std::wstring& local,
                          const std::wstring& userBin = L"", const std::atomic<bool>* cancel = nullptr, std::vector<AI::Stage>* stages = nullptr) {
    AI::Options o; o.agent = agent; o.scriptFile = FakeScript; o.timeoutMs = timeoutMs; o.workDir = Work + L"\\runs\\" + name; o.userBinDir = userBin; o.path = TestPath();
    o.environment = {{L"BROWSER_AGENT_INSTALL_AGENT", agent == RT::Agent::Claude ? L"claude" : L"codex"}, {L"BROWSER_FAKE_INSTALL_MODE", mode},
                     {L"BROWSER_FAKE_CLI", FakeCli}, {L"FAKE_HOME", home}, {L"FAKE_LOCAL", local}, {L"BROWSER_FAKE_INSTALL_DELAY_MS", L""}};
    return AI::Run(o, [stages](AI::Stage s) { if (stages) stages->push_back(s); }, cancel);
}

static void Installer() {
    const std::wstring home = Work + L"\\inst\\home", local = Work + L"\\inst\\local"; MakeDir(home); MakeDir(local);
    std::vector<AI::Stage> stages;
    auto r = RunFake(L"claude-ok", L"ok", RT::Agent::Claude, 60000, home, local, home + L"\\.local\\bin", nullptr, &stages);
    Expect(r.Ok() && r.exitCode == 0 && Exists(home + L"\\.local\\bin\\claude.exe"), "install: the fake Claude Code installer ran (hidden PowerShell), its CLI in .local\\bin");
    Expect(r.pathAdded && AI::PathContains(PathValue(TestPath()), home + L"\\.local\\bin"), "install: .local\\bin added to the (test) user PATH");
    Expect(stages.size() == 2 && stages[0] == AI::Stage::Running && stages[1] == AI::Stage::Checking, "install: a local script is not downloaded (running, checking)");
    Expect(r.log.find(L"Installed.") != std::wstring::npos, "install: the script's output kept for the log");
    r = RunFake(L"codex-ok", L"ok", RT::Agent::Codex, 60000, home, local);
    Expect(r.Ok() && !r.pathAdded && Exists(local + L"\\Programs\\OpenAI\\Codex\\bin\\codex.exe"), "install: the fake Codex installer (it adds itself to the PATH: nothing for us to add)");
    Expect(RunFake(L"policy", L"policy", RT::Agent::Claude, 60000, home, local).error == AI::Error::ExecutionPolicy, "install error: PowerShell scripts disabled → execution policy");
    Expect(RunFake(L"inuse", L"inuse", RT::Agent::Claude, 60000, home, local).error == AI::Error::FileInUse, "install error: file used by another process → antivirus / file in use");
    Expect(RunFake(L"network", L"network", RT::Agent::Codex, 60000, home, local).error == AI::Error::Network, "install error: unable to connect → network");
    r = RunFake(L"fail", L"fail", RT::Agent::Codex, 60000, home, local);
    Expect(r.error == AI::Error::ScriptFailed && r.exitCode == 7 && r.detail.find("exit 7") != std::string::npos && r.detail.find("unexpected") != std::string::npos, "install error: anything else → failed, the telling line kept");
    const ULONGLONG t0 = GetTickCount64();
    r = RunFake(L"hang", L"hang", RT::Agent::Claude, 1500, home, local);
    Expect(r.error == AI::Error::Timeout && GetTickCount64() - t0 < 15000, "install error: still running after the timeout → killed (timeout)");
    std::atomic<bool> cancel{false};
    HANDLE th = CreateThread(nullptr, 0, [](void* p) -> DWORD { Sleep(800); static_cast<std::atomic<bool>*>(p)->store(true); return 0; }, &cancel, 0, nullptr);
    r = RunFake(L"cancel", L"hang", RT::Agent::Claude, 60000, home, local, L"", &cancel);
    WaitForSingleObject(th, INFINITE); CloseHandle(th);
    Expect(r.error == AI::Error::Cancelled, "install: the browser closing kills the installer (cancelled)");
    RegDeleteTreeW(HKEY_CURRENT_USER, TestPath().key.c_str());
}

// ---------------------------------------------------------------- the hub over fake profiles (real detection, fake CLIs)
struct FakeProfile { std::wstring home, local, roaming; };
static FakeProfile Profile(const std::wstring& name) { FakeProfile p{Work + L"\\profiles\\" + name + L"\\home", Work + L"\\profiles\\" + name + L"\\local", Work + L"\\profiles\\" + name + L"\\roaming"}; MakeDir(p.home); MakeDir(p.local); MakeDir(p.roaming); return p; }
static void PutCli(const std::wstring& dir, const wchar_t* exe, const char* version, const char* login) {
    MakeDir(dir); CopyFileW(FakeCli.c_str(), (dir + L"\\" + exe).c_str(), FALSE);
    WriteText(dir + L"\\" + exe + L".version", version); WriteText(dir + L"\\" + exe + L".login", login);
}
static void UseProfile(const FakeProfile& p) {
    AS::Host h;
    h.dataRoot = Work + L"\\data";
    h.environment = [p] { RT::Environment e; e.dataRoot = Work + L"\\data"; e.userProfile = p.home; e.localAppData = p.local; e.appData = p.roaming; e.path = L""; e.freshUserEnvironment = false; return e; };
    h.signIn = [](const RT::Environment& env, RT::Agent a, unsigned, std::string*) {   // the fake CLI's own sign-in, without a console
        const std::wstring exe = a == RT::Agent::Claude ? env.userProfile + L"\\.local\\bin\\claude.exe" : env.localAppData + L"\\Programs\\OpenAI\\Codex\\bin\\codex.exe";
        if (Exists(exe)) WriteText(exe + L".login", "0");
        return true;
    };
    AS::Init(h);   // a first detection, at once (no `run`: synchronous)
    SetEnvironmentVariableW(L"FAKE_HOME", p.home.c_str()); SetEnvironmentVariableW(L"FAKE_LOCAL", p.local.c_str());
}

static void Profiles() {
    // The browser's own test hooks (non-release, BROWSER_ANNOTATIONS_TEST): the fake script instead of the official one, the PATH on a test key.
    SetEnvironmentVariableW(L"BROWSER_ANNOTATIONS_TEST", L"1");
    SetEnvironmentVariableW(L"BROWSER_AGENT_INSTALL_SCRIPT", FakeScript.c_str());
    SetEnvironmentVariableW(L"BROWSER_AGENT_PATH_KEY", TestPath().key.c_str());
    SetEnvironmentVariableW(L"BROWSER_FAKE_CLI", FakeCli.c_str());
    using S = AS::State; using R = AS::Reason; const auto C = RT::Agent::Claude, X = RT::Agent::Codex;
    {   // Claude Code absent, nothing else
        UseProfile(Profile(L"absent"));
        Expect(AS::Get(C).state == S::NotInstalled && AS::Get(C).reason == R::None && AS::Get(X).state == S::NotInstalled, "profile « absent »: both not installed");
    }
    {   // Claude Desktop only → not installed (desktopOnly); install → installed, signed out; sign in → ready
        const auto p = Profile(L"desktop"); MakeDir(p.local + L"\\AnthropicClaude"); UseProfile(p);
        Expect(AS::Get(C).state == S::NotInstalled && AS::Get(C).reason == R::DesktopOnly, "profile « Claude Desktop only »: Claude Code not installed (desktopOnly)");
        SetEnvironmentVariableW(L"BROWSER_FAKE_INSTALL_MODE", L"ok");
        Expect(AS::Install(AS::UserClicked(C)) && AS::Get(C).state == S::NeedsSignIn && AS::Get(C).version == "2.1.325", "profile « Claude Desktop only »: the click installs Claude Code → installed, to sign in");
        Expect(AI::PathContains(PathValue(TestPath()), p.home + L"\\.local\\bin"), "profile « Claude Desktop only »: .local\\bin on the (test) user PATH");
        Expect(AS::SignIn(C) && AS::Get(C).Ready() && AS::FirstReady() == C, "profile « Claude Desktop only »: signed in → ready");
    }
    {   // Claude Code present and signed in, Codex present and signed out
        const auto p = Profile(L"present");
        PutCli(p.home + L"\\.local\\bin", L"claude.exe", "2.1.325 (Claude Code)", "0"); PutCli(p.local + L"\\Programs\\OpenAI\\Codex\\bin", L"codex.exe", "codex-cli 0.166.0", "1");
        UseProfile(p);
        Expect(AS::Get(C).Ready() && AS::Get(X).state == S::NeedsSignIn, "profile « present »: Claude Code ready, Codex to sign in");
        Expect(!AS::Install(AS::UserClicked(C)) && !AS::Install(AS::UserClicked(X)), "profile « present »: nothing to install");
    }
    {   // too old → install again updates it
        const auto p = Profile(L"old"); PutCli(p.home + L"\\.local\\bin", L"claude.exe", "2.0.1 (Claude Code)", "0"); UseProfile(p);
        Expect(AS::Get(C).state == S::NotInstalled && AS::Get(C).reason == R::TooOld && AS::Get(C).version == "2.0.1", "profile « too old »: not installed (tooOld)");
        SetEnvironmentVariableW(L"BROWSER_FAKE_INSTALL_MODE", L"ok");
        Expect(AS::Install(AS::UserClicked(C)) && AS::Get(C).state == S::NeedsSignIn && AS::Get(C).version == "2.1.325", "profile « too old »: installing updates it");
    }
    {   // the installer « succeeds » but no CLI → error (not « Installé »: the bug)
        UseProfile(Profile(L"nocli"));
        SetEnvironmentVariableW(L"BROWSER_FAKE_INSTALL_MODE", L"nocli");
        Expect(AS::Install(AS::UserClicked(X)) && AS::Get(X).state == S::Error && AS::Get(X).reason == R::NotFoundAfterInstall, "install without the CLI afterwards → error (never « installed »)");
        SetEnvironmentVariableW(L"BROWSER_FAKE_INSTALL_MODE", L"network");
        Expect(AS::Retry(AS::UserClicked(X)) && AS::Get(X).state == S::Error && AS::Get(X).reason == R::Network, "retry: the network fails → error (network)");
        SetEnvironmentVariableW(L"BROWSER_FAKE_INSTALL_MODE", L"ok");
        Expect(AS::Retry(AS::UserClicked(X)) && AS::Get(X).state == S::NeedsSignIn, "retry: then it works → installed, to sign in");
    }
    AS::Shutdown();
    RegDeleteTreeW(HKEY_CURRENT_USER, TestPath().key.c_str());
    SetEnvironmentVariableW(L"BROWSER_AGENT_INSTALL_SCRIPT", nullptr); SetEnvironmentVariableW(L"BROWSER_AGENT_PATH_KEY", nullptr);
}

// ---------------------------------------------------------------- « Annuler » while signing in (CancelSignIn)
// An asynchronous host, as the browser's: the work on another thread, `done` queued for this (« UI ») thread, which pumps.
struct DoneQueue { CRITICAL_SECTION cs; std::vector<std::function<void()>> items; DoneQueue() { InitializeCriticalSection(&cs); } };
static DoneQueue& Q() { static DoneQueue q; return q; }
static void AsyncRun(std::function<void()> work, std::function<void()> done) {
    struct Job { std::function<void()> work, done; };
    auto* job = new Job{std::move(work), std::move(done)};
    HANDLE t = CreateThread(nullptr, 0, [](void* p) -> DWORD {
        Job* j = static_cast<Job*>(p); j->work();
        EnterCriticalSection(&Q().cs); Q().items.push_back(std::move(j->done)); LeaveCriticalSection(&Q().cs);
        delete j; return 0; }, job, 0, nullptr);
    if (t) CloseHandle(t);
}
static void Pump(unsigned ms, const std::function<bool()>& until) {
    const ULONGLONG end = GetTickCount64() + ms;
    for (;;) {
        std::vector<std::function<void()>> take;
        EnterCriticalSection(&Q().cs); take.swap(Q().items); LeaveCriticalSection(&Q().cs);
        for (auto& f : take) f();
        if ((until && until()) || GetTickCount64() >= end) return;
        Sleep(10);
    }
}
// Whether a process runs from this exact file (the fake CLI's sign-in console).
static bool RunningFrom(const std::wstring& exe) {
    bool found = false;
    DWORD ids[4096], bytes = 0;
    if (!EnumProcesses(ids, sizeof ids, &bytes)) return false;
    for (DWORD i = 0; i < bytes / sizeof(DWORD) && !found; ++i) {
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, ids[i]); if (!h) continue;
        wchar_t path[MAX_PATH * 2]; DWORD n = MAX_PATH * 2;
        if (QueryFullProcessImageNameW(h, 0, path, &n) && CompareStringOrdinal(path, int(n), exe.c_str(), -1, TRUE) == CSTR_EQUAL) found = true;
        CloseHandle(h);
    }
    return found;
}
static void CancelSignInCase() {
    using S = AS::State; using R = AS::Reason; const auto C = RT::Agent::Claude;
    const auto p = Profile(L"signin");
    const std::wstring exe = p.home + L"\\.local\\bin\\claude.exe";
    PutCli(p.home + L"\\.local\\bin", L"claude.exe", "2.1.325 (Claude Code)", "1");
    WriteText(exe + L".signin", "hang");   // the person never finishes in the web page
    AS::Host h;
    h.dataRoot = Work + L"\\data"; h.run = AsyncRun;
    // no h.signIn: the browser's own sign-in (RunTerminalSignIn) on the fake CLI, without a window (headless test)
    h.environment = [p] { RT::Environment e; e.dataRoot = Work + L"\\data"; e.userProfile = p.home; e.localAppData = p.local; e.appData = p.roaming; e.path = L""; e.freshUserEnvironment = false; e.signInConsoleHidden = true; return e; };
    AS::Init(h);
    Pump(15000, [&] { return AS::Get(C).state == S::NeedsSignIn; });
    Expect(AS::Get(C).state == S::NeedsSignIn, "cancel: Claude Code installed, signed out");
    Expect(!AS::CancelSignIn(C), "cancel: refused when the agent is not signing in");
    Expect(AS::SignIn(C) && AS::Get(C).state == S::SigningIn, "cancel: « Se connecter » opens the sign-in (the fake CLI never finishes)");
    Pump(5000, [&] { return RunningFrom(exe); });
    Expect(RunningFrom(exe), "cancel: the sign-in console is running");
    const ULONGLONG t0 = GetTickCount64();
    Expect(AS::CancelSignIn(C), "cancel: « Annuler » while signing in");
    Pump(15000, [&] { return AS::Get(C).state != S::SigningIn; });
    const ULONGLONG took = GetTickCount64() - t0;
    Expect(AS::Get(C).state == S::NeedsSignIn && AS::Get(C).reason == R::Cancelled && took < 5000,
           "cancel: back to « Se connecter » (reason cancelled, not an error), at once — not after the 15 min timeout");
    Expect(!RunningFrom(exe), "cancel: the sign-in console's process is gone");
    Expect(!AS::CancelSignIn(C), "cancel: nothing left to cancel");
    // « Browser voit tout seul quand c'est fait » (3b): signed in in the web page while the console still waits — a
    // detection asked meanwhile (focus back in Browser, the tutorial back over its tab) sees it: the console closes, « Prêt ».
    Expect(AS::SignIn(C) && AS::Get(C).state == S::SigningIn, "signed in meanwhile: a sign-in running (its console waits)");
    Pump(5000, [&] { return RunningFrom(exe); });
    bool asked = false; AS::Refresh([&] { asked = true; }); Pump(15000, [&] { return asked; });
    Expect(asked && AS::Get(C).state == S::SigningIn && RunningFrom(exe), "signed in meanwhile: still signed out → the detection changes nothing, the console keeps waiting");
    WriteText(exe + L".login", "0");   // the person finished in the web page
    const ULONGLONG t2 = GetTickCount64(); AS::Refresh();
    Pump(15000, [&] { return AS::Get(C).state != S::SigningIn; });
    Expect(AS::Get(C).Ready() && GetTickCount64() - t2 < 5000, "signed in meanwhile: the detection sees it → « Prêt » at once");
    Pump(5000, [&] { return !RunningFrom(exe); });
    Expect(!RunningFrom(exe), "signed in meanwhile: the waiting sign-in console is closed");
    WriteText(exe + L".login", "1"); AS::Refresh(); Pump(15000, [&] { return AS::Get(C).state == S::NeedsSignIn; });   // signed out again
    // The browser closing still ends a sign-in.
    Expect(AS::SignIn(C), "shutdown: a sign-in running");
    Pump(5000, [&] { return RunningFrom(exe); });
    const ULONGLONG t1 = GetTickCount64();
    AS::Shutdown();
    Pump(15000, [&] { return AS::Get(C).state != S::SigningIn; });
    Expect(AS::Get(C).state != S::SigningIn && GetTickCount64() - t1 < 5000 && !RunningFrom(exe), "shutdown: the browser closing kills the sign-in console at once");
}

// A fresh work folder every run (the fake profiles must start empty). Only ever under the folder the runner gives.
static void RemoveTree(const std::wstring& dir) {
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring name = fd.cFileName; if (name == L"." || name == L"..") continue;
            const std::wstring p = dir + L"\\" + name;
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) RemoveTree(p);
            else { SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL); if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) RemoveDirectoryW(p.c_str()); else DeleteFileW(p.c_str()); }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir.c_str());
}
static std::wstring RealUserPath() { wchar_t buf[32768] = {}; DWORD size = sizeof buf; RegGetValueW(HKEY_CURRENT_USER, L"Environment", L"Path", RRF_RT_ANY | RRF_NOEXPAND, nullptr, buf, &size); return buf; }

int wmain(int argc, wchar_t** argv) {
    const std::wstring realPath = RealUserPath();   // the user's real PATH: must be exactly the same at the end
    DetectionMatrix();
    ChatView();
    Classification();
    PathHelpers();
    Hub();
    if (argc >= 4) {
        FakeCli = argv[1]; Work = argv[2]; FakeScript = argv[3];
        RemoveTree(Work); MakeDir(Work); Installer(); Profiles(); CancelSignInCase();
    } else std::printf("(no fake CLI / work dir / fake script given: installer and profile checks skipped)\n");
    Expect(RealUserPath() == realPath, "safety: the user's real PATH (HKCU\\Environment) untouched");
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
