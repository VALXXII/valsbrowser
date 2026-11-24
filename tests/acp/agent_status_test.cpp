// « Ton agent » (design/ton-agent-interface.md): the one status of the chat's agents and the official installers, tested
// WITHOUT the real installers, the real CLIs or the user's real PATH. Everything outside runs on fakes:
//   - detections are injected (the detection matrix), or real detections over fake claude.exe / codex.exe (fake-cli.exe
//     copied into temporary folders standing for %USERPROFILE% / %LOCALAPPDATA%);
//   - installers are fake PowerShell scripts in a temporary folder (success, failure kinds, timeout);
//   - the user PATH is a test key under HKCU\Software\BrowserTests (never HKCU\Environment).
// Usage: agent_status_test.exe <fake-cli.exe> <work dir>
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

#include "agent-install.h"
#include "agent-status.h"

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

int wmain(int argc, wchar_t** argv) {
    (void)argc; (void)argv;
    DetectionMatrix();
    Classification();
    PathHelpers();
    Hub();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
