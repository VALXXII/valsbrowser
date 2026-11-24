// « Ton agent » — the official installers (src/agent-install.h) and the one status hub (src/agent-status.h).
// Design: design/ton-agent-interface.md.
#ifndef UNICODE
#define UNICODE
#endif
#include "agent-install.h"
#include "agent-status.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <map>
#include <memory>

namespace {

bool DirExists(const std::wstring& p) { const DWORD a = GetFileAttributesW(p.c_str()); return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY); }
std::wstring Lower(std::wstring s) { for (auto& c : s) c = static_cast<wchar_t>(std::towlower(c)); return s; }
std::wstring StripSlashes(std::wstring s) { while (s.size() > 3 && (s.back() == L'\\' || s.back() == L'/')) s.pop_back(); return s; }
std::wstring Env(const wchar_t* name) {
    const DWORD n = GetEnvironmentVariableW(name, nullptr, 0);
    if (!n) return L"";
    std::wstring v(n, L'\0'); const DWORD got = GetEnvironmentVariableW(name, v.data(), n); v.resize(got); return v;
}
bool TestHooks() {
#ifdef BROWSER_CHANNEL_RELEASE
    return false;
#else
    wchar_t probe[8]; SetLastError(ERROR_SUCCESS);
    return GetEnvironmentVariableW(L"BROWSER_ANNOTATIONS_TEST", probe, 8) != 0 || GetLastError() != ERROR_ENVVAR_NOT_FOUND;
#endif
}

}  // namespace

// ================================================================ installer
namespace AgentInstall {

const char* ErrorName(Error error) {
    switch (error) {
    case Error::None: return "none";
    case Error::Cancelled: return "cancelled";
    case Error::Network: return "network";
    case Error::FileInUse: return "fileInUse";
    case Error::ExecutionPolicy: return "executionPolicy";
    case Error::Timeout: return "timeout";
    case Error::ScriptFailed: return "scriptFailed";
    case Error::Disk: return "disk";
    case Error::PathUpdate: return "pathUpdate";
    }
    return "?";
}

Options DefaultOptions(Agent agent, const AgentRuntime::Environment& env) {
    Options o;
    o.agent = agent;
    o.scriptUrl = OfficialScriptUrl(agent);
    if (agent == Agent::Claude && !env.userProfile.empty()) o.userBinDir = StripSlashes(env.userProfile) + L"\\.local\\bin";
    if (TestHooks()) {
        if (const std::wstring s = Env(L"BROWSER_AGENT_INSTALL_SCRIPT"); !s.empty()) o.scriptFile = s;
        if (const std::wstring k = Env(L"BROWSER_AGENT_PATH_KEY"); !k.empty()) { o.path.key = k; o.path.broadcast = false; }
    }
    return o;
}

Error Classify(int exitCode, std::string_view output) {
    if (exitCode == 0) return Error::None;
    std::string low(output); for (auto& c : low) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    auto has = [&](const char* s) { return low.find(s) != std::string::npos; };
    if (has("execution polic") || has("running scripts is disabled") || has("about_execution_policies") || has("psecuritye")) return Error::ExecutionPolicy;
    if (has("being used by another process") || has("used by another process") || has("access to the path") || has("access is denied") || has("virus") ||
        has("unauthorizedaccess"))
        return Error::FileInUse;
    if (has("unable to connect") || has("could not be resolved") || has("remote name could not") || has("proxy") || has("(407)") || has("timed out") ||
        has("ssl") || has("tls") || has("trust relationship") || has("no such host") || has("network") || has("webexception") ||
        has("unable to download") || has("failed to download"))
        return Error::Network;
    return Error::ScriptFailed;
}

bool PathContains(std::wstring_view pathValue, std::wstring_view dir, const std::function<std::wstring(const std::wstring&)>& expand) {
    auto norm = [&](std::wstring s) {
        if (expand) s = expand(s);
        while (!s.empty() && (s.back() == L' ' || s.back() == L'"')) s.pop_back();
        while (!s.empty() && (s.front() == L' ' || s.front() == L'"')) s.erase(s.begin());
        return Lower(StripSlashes(s));
    };
    const std::wstring want = norm(std::wstring(dir));
    if (want.empty()) return false;
    std::size_t start = 0;
    while (start <= pathValue.size()) {
        const std::size_t semi = pathValue.find(L';', start);
        const std::wstring part(pathValue.substr(start, semi == std::wstring_view::npos ? std::wstring_view::npos : semi - start));
        if (!part.empty() && norm(part) == want) return true;
        if (semi == std::wstring_view::npos) break;
        start = semi + 1;
    }
    return false;
}

bool EnsureOnUserPath(const std::wstring& dir, const PathTarget& target, bool* added, std::string* error) {
    if (added) *added = false;
    HKEY key = nullptr;
    LONG r = RegCreateKeyExW(HKEY_CURRENT_USER, target.key.c_str(), 0, nullptr, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &key, nullptr);
    if (r != ERROR_SUCCESS) { if (error) *error = "open " + std::to_string(r); return false; }
    DWORD type = REG_EXPAND_SZ, bytes = 0;
    std::wstring value;
    r = RegQueryValueExW(key, target.value.c_str(), nullptr, &type, nullptr, &bytes);
    if (r == ERROR_SUCCESS && bytes) {
        value.resize(bytes / sizeof(wchar_t) + 1);
        r = RegQueryValueExW(key, target.value.c_str(), nullptr, &type, reinterpret_cast<BYTE*>(value.data()), &bytes);
        if (r != ERROR_SUCCESS) { RegCloseKey(key); if (error) *error = "read " + std::to_string(r); return false; }
        value.resize(bytes / sizeof(wchar_t));
        while (!value.empty() && value.back() == L'\0') value.pop_back();
    } else if (r != ERROR_FILE_NOT_FOUND && r != ERROR_SUCCESS) { RegCloseKey(key); if (error) *error = "query " + std::to_string(r); return false; }
    if (type != REG_SZ && type != REG_EXPAND_SZ) type = REG_EXPAND_SZ;
    const auto expand = [](const std::wstring& s) {
        const DWORD n = ExpandEnvironmentStringsW(s.c_str(), nullptr, 0);
        if (!n) return s;
        std::wstring out(n, L'\0'); ExpandEnvironmentStringsW(s.c_str(), out.data(), n); out.resize(n - 1); return out;
    };
    if (PathContains(value, dir, expand)) { RegCloseKey(key); return true; }
    std::wstring next = value;
    if (!next.empty() && next.back() != L';') next += L';';
    next += dir;
    r = RegSetValueExW(key, target.value.c_str(), 0, type == REG_SZ ? REG_SZ : REG_EXPAND_SZ, reinterpret_cast<const BYTE*>(next.c_str()),
                       static_cast<DWORD>((next.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    if (r != ERROR_SUCCESS) { if (error) *error = "write " + std::to_string(r); return false; }
    if (added) *added = true;
    if (target.broadcast) {
        DWORD_PTR result = 0;
        SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(L"Environment"), SMTO_ABORTIFHUNG, 2000, &result);
    }
    return true;
}

// Interface published first (design/ton-agent-interface.md): the run itself lands right after, behind this signature.
Result Run(const Options& options, const std::function<void(Stage)>& stage, const std::atomic<bool>* cancel) {
    (void)options; (void)stage; (void)cancel;
    Result r; r.error = Error::ScriptFailed; r.detail = "installer not implemented yet"; return r;
}

}  // namespace AgentInstall

// ================================================================ status hub
namespace AgentStatus {

const char* StateName(State state) {
    switch (state) {
    case State::Unknown: return "unknown";
    case State::NotInstalled: return "notInstalled";
    case State::Installing: return "installing";
    case State::NeedsSignIn: return "needsSignIn";
    case State::SigningIn: return "signingIn";
    case State::Ready: return "ready";
    case State::Error: return "error";
    }
    return "?";
}
const char* ReasonName(Reason reason) {
    switch (reason) {
    case Reason::None: return "none";
    case Reason::DesktopOnly: return "desktopOnly";
    case Reason::TooOld: return "tooOld";
    case Reason::Network: return "network";
    case Reason::FileInUse: return "fileInUse";
    case Reason::ExecutionPolicy: return "executionPolicy";
    case Reason::Timeout: return "timeout";
    case Reason::InstallerFailed: return "installerFailed";
    case Reason::NotFoundAfterInstall: return "notFoundAfterInstall";
    case Reason::Disk: return "disk";
    case Reason::SignInFailed: return "signInFailed";
    case Reason::SignInTimeout: return "signInTimeout";
    case Reason::Cancelled: return "cancelled";
    }
    return "?";
}

Manual ManualInstall(Agent agent) {
    // i18n-exempt-begin: the official command lines and pages, shown as they are for the user to copy
    if (agent == Agent::Claude) return {L"irm https://claude.ai/install.ps1 | iex", L"https://code.claude.com/docs/en/setup"};
    return {L"powershell -ExecutionPolicy ByPass -c \"irm https://chatgpt.com/codex/install.ps1 | iex\"", L"https://github.com/openai/codex"};
    // i18n-exempt-end
}

Status FromDetection(const AgentRuntime::Detection& d, bool desktopInstalled) {
    Status s;
    s.agent = d.agent;
    s.version = d.cli.version;
    s.path = d.cli.path;
    s.desktop = d.agent == Agent::Claude && desktopInstalled;
    switch (d.cli.state) {
    case AgentRuntime::CliState::Missing: s.state = State::NotInstalled; s.reason = s.desktop ? Reason::DesktopOnly : Reason::None; break;
    case AgentRuntime::CliState::TooOld: s.state = State::NotInstalled; s.reason = Reason::TooOld; break;
    default: s.state = d.login == AgentRuntime::Login::SignedOut ? State::NeedsSignIn : State::Ready; break;
    }
    return s;
}

bool DesktopInstalled(const AgentRuntime::Environment& env) {
    if (!env.localAppData.empty()) {
        const std::wstring local = StripSlashes(env.localAppData);
        if (DirExists(local + L"\\AnthropicClaude")) return true;   // the Claude Desktop installer
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((local + L"\\Packages\\Claude_*").c_str(), &fd);   // the Microsoft Store package
        if (h != INVALID_HANDLE_VALUE) { const bool dir = fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY; FindClose(h); if (dir) return true; }
    }
    return !env.appData.empty() && DirExists(StripSlashes(env.appData) + L"\\Claude");   // its configuration folder
}

namespace {
struct Hub {
    Host host;
    bool initialised = false, shuttingDown = false;
    Status status[2];
    std::map<int, Listener> listeners;
    int nextId = 1;
    unsigned detecting[2] = {0, 0};   // detections in flight (a newer one wins)
    std::shared_ptr<std::atomic<bool>> cancel = std::make_shared<std::atomic<bool>>(false);
};
Hub& H() {
    static Hub hub;
    static bool seeded = false;
    if (!seeded) { seeded = true; hub.status[0].agent = Agent::Claude; hub.status[1].agent = Agent::Codex; }
    return hub;
}
int Index(Agent a) { return a == Agent::Claude ? 0 : 1; }
std::wstring W(const std::string& s) { return std::wstring(s.begin(), s.end()); }
void Log(const std::wstring& s) { if (H().host.log) H().host.log(L"AGENTS " + s); }
const wchar_t* Name(Agent a) { return a == Agent::Claude ? L"claude" : L"codex"; }
AgentRuntime::Environment Environment() { return H().host.environment ? H().host.environment() : AgentRuntime::Environment::FromProcess(H().host.dataRoot); }
AgentRuntime::Detection DoDetect(const AgentRuntime::Environment& env, Agent a) { return H().host.detect ? H().host.detect(env, a) : AgentRuntime::Detect(env, a); }
void RunOff(std::function<void()> work, std::function<void()> done) {
    if (H().host.run) H().host.run(std::move(work), std::move(done));
    else { work(); done(); }
}
void Set(Status next) {
    auto& cur = H().status[Index(next.agent)];
    const bool changed = cur.state != next.state || cur.reason != next.reason || cur.stage != next.stage || cur.version != next.version ||
                         cur.path != next.path || cur.desktop != next.desktop;
    next.generation = cur.generation + (changed ? 1u : 0u);
    cur = std::move(next);
    if (!changed) return;
    Log(std::wstring(Name(cur.agent)) + L": " + W(StateName(cur.state)) + (cur.reason == Reason::None ? L"" : L" (" + W(ReasonName(cur.reason)) + L")") +
        (cur.version.empty() ? L"" : L" " + W(cur.version)) + (cur.detail.empty() ? L"" : L" — " + W(cur.detail)));
    const auto copy = H().listeners;   // a listener may (un)subscribe
    for (const auto& l : copy) if (l.second) l.second(cur);
}
}  // namespace

void Init(Host host) {
    auto& h = H();
    h.host = std::move(host); h.initialised = true; h.shuttingDown = false; h.cancel->store(false);
    Refresh();
}
void Shutdown() { auto& h = H(); h.shuttingDown = true; h.cancel->store(true); }

const Status& Get(Agent agent) { return H().status[Index(agent)]; }
bool AnyReady() { return Get(Agent::Claude).Ready() || Get(Agent::Codex).Ready(); }
std::optional<Agent> FirstReady() {
    for (Agent a : AllAgents) if (Get(a).Ready()) return a;
    return std::nullopt;
}

int Subscribe(Listener listener) { const int id = H().nextId++; H().listeners[id] = std::move(listener); return id; }
void Unsubscribe(int id) { H().listeners.erase(id); }

void Refresh(Agent agent) {
    auto& h = H();
    if (!h.initialised || h.shuttingDown || Get(agent).Busy()) return;
    const unsigned ticket = ++h.detecting[Index(agent)];
    struct Out { AgentRuntime::Detection d; bool desktop = false; };
    auto out = std::make_shared<Out>();
    RunOff([out, agent] { const auto env = Environment(); out->d = DoDetect(env, agent); out->desktop = DesktopInstalled(env); },
           [out, agent, ticket] {
               auto& hub = H();
               if (hub.shuttingDown || ticket != hub.detecting[Index(agent)] || Get(agent).Busy()) return;   // a newer detection, or an install began
               Set(FromDetection(out->d, out->desktop));
           });
}
void Refresh() { for (Agent a : AllAgents) Refresh(a); }

namespace {
Reason ReasonOf(AgentInstall::Error e) {
    switch (e) {
    case AgentInstall::Error::Network: return Reason::Network;
    case AgentInstall::Error::FileInUse: return Reason::FileInUse;
    case AgentInstall::Error::ExecutionPolicy: return Reason::ExecutionPolicy;
    case AgentInstall::Error::Timeout: return Reason::Timeout;
    case AgentInstall::Error::Disk: return Reason::Disk;
    case AgentInstall::Error::Cancelled: return Reason::Cancelled;
    default: return Reason::InstallerFailed;
    }
}
}  // namespace

bool Install(const Consent& consent) {
    auto& h = H();
    const Agent agent = consent.agent;
    const Status& cur = Get(agent);
    if (!consent.given || !h.initialised || h.shuttingDown || cur.Busy() || cur.Installed()) return false;   // never without the click; nothing to install
    Status s = cur; s.state = State::Installing; s.reason = Reason::None; s.stage = Stage::Running; s.detail.clear(); Set(s);
    ++h.detecting[Index(agent)];   // a detection in flight must not overwrite « Installing »
    struct Out { AgentInstall::Result result; AgentRuntime::Detection d; bool desktop = false; };
    auto out = std::make_shared<Out>(); auto cancel = h.cancel;
    RunOff([out, agent, cancel] {
               const auto env = Environment();
               out->result = AgentInstall::Run(AgentInstall::DefaultOptions(agent, env), nullptr, cancel.get());
               if (out->result.Ok()) { const auto again = Environment(); out->d = DoDetect(again, agent); out->desktop = DesktopInstalled(again); }
           },
           [out, agent] {
               Status s = Get(agent); s.stage = Stage::None;
               s.detail = std::string(AgentInstall::ErrorName(out->result.error)) + (out->result.detail.empty() ? "" : " " + out->result.detail);
               if (!out->result.Ok()) { s.state = State::Error; s.reason = ReasonOf(out->result.error); Set(s); return; }
               Status found = FromDetection(out->d, out->desktop); found.detail = s.detail;
               if (found.state == State::NotInstalled) { found.state = State::Error; found.reason = Reason::NotFoundAfterInstall; }
               Set(found);
           });
    return true;
}

bool SignIn(Agent agent) {
    auto& h = H();
    const Status& cur = Get(agent);
    const bool signInError = cur.state == State::Error && (cur.reason == Reason::SignInFailed || cur.reason == Reason::SignInTimeout);
    if (!h.initialised || h.shuttingDown || cur.Busy() || !(cur.state == State::NeedsSignIn || signInError)) return false;
    Status s = cur; s.state = State::SigningIn; s.reason = Reason::None; s.detail.clear(); Set(s);
    ++h.detecting[Index(agent)];
    struct Out { bool ran = false, timedOut = false; std::string error; AgentRuntime::Detection d; bool desktop = false; };
    auto out = std::make_shared<Out>();
    RunOff([out, agent] {
               const auto env = Environment();
               constexpr unsigned timeout = 15 * 60 * 1000;
               if (H().host.signIn) out->ran = H().host.signIn(env, agent, timeout, &out->error);
               else {
                   auto console = AgentRuntime::RunTerminalSignIn(env, agent, AgentRuntime::DefaultTerminalSignIn(agent), &out->error);
                   out->ran = console.Started();
                   if (out->ran) { out->timedOut = !console.Wait(timeout); console.Close(); }
               }
               const auto again = Environment(); out->d = DoDetect(again, agent); out->desktop = DesktopInstalled(again);
           },
           [out, agent] {
               Status found = FromDetection(out->d, out->desktop); found.detail = out->error;
               if (found.state == State::NeedsSignIn) { found.state = State::Error; found.reason = out->timedOut ? Reason::SignInTimeout : Reason::SignInFailed; }
               Set(found);
           });
    return true;
}

bool Retry(const Consent& consent) {
    const Status& cur = Get(consent.agent);
    if (cur.state != State::Error || !consent.given) return false;
    if (cur.reason == Reason::SignInFailed || cur.reason == Reason::SignInTimeout) return SignIn(consent.agent);
    Status s = cur; s.state = State::NotInstalled; s.reason = Reason::None; Set(s);   // back to « not installed », then install
    return Install(consent);
}

}  // namespace AgentStatus
