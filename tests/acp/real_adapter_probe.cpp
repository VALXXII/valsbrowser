// Probe of the REAL adapters with the user's own CLI, without any call to a model:
//   real_adapter_probe.exe <data root> <claude|codex> <empty project folder> <report.json> [--signed-out <empty folder>]
// It detects the CLI, prepares the private Node and the adapter under <data root>, starts the adapter, sends
// `initialize` and `session/new`, records what the agent announces (capabilities, sign-in methods, modes, config
// options, commands) and stops. `session/prompt` is never sent: nothing is consumed on the user's account.
// --signed-out: the CLI's configuration folder is redirected to an empty folder (CLAUDE_CONFIG_DIR / CODEX_HOME)
// for this process only, to record what "not signed in" looks like, without touching the user's login.
#include <cstring>

#include "../../src/agent-runtime.h"
#include "../../src/strict-json.h"
#include "acp_test_util.h"

using namespace AcpTest;
namespace AR = AgentRuntime;
namespace SJ = StrictJson;

namespace {
SJ::Value S(const std::string& s) { return SJ::Value::Str(s); }
SJ::Value Strings(const std::vector<std::string>& list) {
    SJ::Value a = SJ::Value::Arr();
    for (const auto& s : list) a.Push(S(s));
    return a;
}
// Paths are reported without the user's profile folder.
std::string Anonymous(const std::wstring& path, const AR::Environment& env) {
    if (!env.userProfile.empty() && _wcsnicmp(path.c_str(), env.userProfile.c_str(), env.userProfile.size()) == 0)
        return "%USERPROFILE%" + Narrow(path.substr(env.userProfile.size()));
    return Narrow(path);
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 5) {
        std::printf("usage: real_adapter_probe <data root> <claude|codex> <project folder> <report.json> [--signed-out <folder>]\n");
        return 2;
    }
    SetConsoleOutputCP(CP_UTF8);
    const std::wstring dataRoot = FullPath(argv[1]), cwd = FullPath(argv[3]), reportPath = FullPath(argv[4]);
    AR::Agent agent;
    if (!AR::ParseAgentId(Narrow(argv[2]), agent)) return 2;
    std::wstring signedOutDir;
    if (argc > 6 && std::wstring(argv[5]) == L"--signed-out") signedOutDir = FullPath(argv[6]);
    if (!MakeDirs(dataRoot) || !MakeDirs(cwd) || (!signedOutDir.empty() && !MakeDirs(signedOutDir))) return 2;

    AR::Environment env = AR::Environment::FromProcess(dataRoot);
    env.fakeAgentExe.clear();  // this probe is about the real adapters
    SJ::Value report = SJ::Value::Obj();
    report.Set("agent", S(AR::AgentId(agent)));
    report.Set("signedOutRun", SJ::Value::Bool(!signedOutDir.empty()));
    const AR::Pins& pins = env.ThePins();
    report.Set("node", S(pins.node.version));
    report.Set("adapter", S(std::string(pins.Adapter(agent).package) + "@" + pins.Adapter(agent).version));

    DWORD t0 = GetTickCount();
    const AR::Detection d = AR::Detect(env, agent);
    SJ::Value cli = SJ::Value::Obj();
    static const char* states[] = {"missing", "found", "too_old", "untested"};
    static const char* logins[] = {"unknown", "signed_in", "signed_out"};
    cli.Set("state", S(states[int(d.cli.state)]));
    cli.Set("version", S(d.cli.version));
    cli.Set("source", S(d.cli.source));
    cli.Set("path", S(Anonymous(d.cli.path, env)));
    cli.Set("login", S(logins[int(d.login)]));
    cli.Set("detectMs", SJ::Value::Int(GetTickCount() - t0));
    report.Set("cli", std::move(cli));
    std::printf("%s: CLI %s %s (%s), login %s, detection %lu ms\n", AR::AgentId(agent), states[int(d.cli.state)], d.cli.version.c_str(), d.cli.source.c_str(),
                logins[int(d.login)], GetTickCount() - t0);

    int exitCode = 0;
    if (!d.Usable()) {
        report.Set("result", S("cli_not_usable"));
        std::printf("%s: the CLI is not installed (or too old): nothing started\n", AR::AgentId(agent));
    } else {
        t0 = GetTickCount();
        std::uint64_t lastBytes = 0;
        const AR::PrepareResult prepared = AR::Prepare(env, agent, [&](const AR::Progress& p) { lastBytes = p.received; });
        (void)lastBytes;
        SJ::Value prepare = SJ::Value::Obj();
        prepare.Set("error", S(AR::PrepareErrorName(prepared.error)));
        prepare.Set("detail", S(prepared.detail));
        prepare.Set("ms", SJ::Value::Int(GetTickCount() - t0));
        report.Set("prepare", std::move(prepare));
        std::printf("%s: prepare %s %s (%lu ms)\n", AR::AgentId(agent), AR::PrepareErrorName(prepared.error), prepared.detail.c_str(), GetTickCount() - t0);
        if (!prepared.Ok()) {
            report.Set("result", S("prepare_failed"));
            exitCode = 1;
        } else {
            Acp::Session session;
            Acp::StartOptions o;
            o.cwd = Narrow(cwd);
            o.clientName = "browser-probe";
            o.clientVersion = "0";
            const Acp::Resolver base = AR::MakeResolver(env, agent);
            o.resolve = [&](const Acp::ResolveContext& context) {
                Acp::Resolution r = base(context);
                if (!signedOutDir.empty()) {
                    r.launch.env.emplace_back(agent == AR::Agent::Claude ? L"CLAUDE_CONFIG_DIR" : L"CODEX_HOME", signedOutDir);
                    for (const wchar_t* key : {L"ANTHROPIC_API_KEY", L"ANTHROPIC_AUTH_TOKEN", L"CLAUDE_CODE_OAUTH_TOKEN", L"OPENAI_API_KEY", L"CODEX_API_KEY"})
                        r.launch.unsetEnv.emplace_back(key);
                }
                return r;
            };
            t0 = GetTickCount();
            session.Start(o);
            std::vector<Acp::Event> events;
            Acp::StartResult started;
            bool answered = false;
            std::vector<Acp::Command> commands;
            std::vector<std::string> warnings, others;
            std::string authStatus;
            DWORD answeredAt = 0;
            // Until the answer, then three more seconds for what agents send right after (commands, options).
            while (GetTickCount() - t0 < 150000 && (!answered || GetTickCount() - answeredAt < 3000)) {
                session.WaitForEvents(200);
                events.clear();
                session.Poll(events);
                for (const auto& e : events) {
                    if (e.type == Acp::Event::Type::Started) {
                        started = e.started;
                        answered = true;
                        answeredAt = GetTickCount();
                        commands = e.started.commands;
                    } else if (e.type == Acp::Event::Type::CommandsChanged) {
                        commands = e.commands;
                    } else if (e.type == Acp::Event::Type::ConfigOptionsChanged) {
                        started.configOptions = e.configOptions;
                        others.push_back("config_option_update");
                    } else if (e.type == Acp::Event::Type::ModesChanged) {
                        started.modes = e.modes;
                        others.push_back("current_mode_update");
                    } else if (e.type == Acp::Event::Type::AuthStatus) {
                        authStatus = e.text;
                    } else if (e.type == Acp::Event::Type::Warning) {
                        warnings.push_back(e.text);
                    } else if (e.type == Acp::Event::Type::Exited) {
                        others.push_back("exited " + std::to_string(e.exitCode) + " " + e.error.detail);
                    } else {
                        others.push_back("event " + std::to_string(int(e.type)));
                    }
                }
            }
            const DWORD startMs = answered ? answeredAt - t0 : GetTickCount() - t0;
            static const char* startStates[] = {"ready", "not_installed", "needs_prepare", "auth_required", "failed"};
            SJ::Value start = SJ::Value::Obj();
            start.Set("answered", SJ::Value::Bool(answered));
            start.Set("ms", SJ::Value::Int(startMs));
            start.Set("state", S(answered ? startStates[int(started.state)] : "no_answer"));
            start.Set("errorCode", S(Acp::ErrorCodeName(started.error.code)));
            start.Set("rpcCode", SJ::Value::Int(started.error.rpcCode));
            start.Set("errorDetail", S(started.error.detail));
            start.Set("protocolVersion", SJ::Value::Int(started.protocolVersion));
            start.Set("agentName", S(started.agentName));
            start.Set("agentTitle", S(started.agentTitle));
            start.Set("agentVersion", S(started.agentVersion));
            start.Set("hasSessionId", SJ::Value::Bool(!started.sessionId.empty()));
            SJ::Value caps = SJ::Value::Obj();
            caps.Set("image", SJ::Value::Bool(started.capabilities.image));
            caps.Set("audio", SJ::Value::Bool(started.capabilities.audio));
            caps.Set("embeddedContext", SJ::Value::Bool(started.capabilities.embeddedContext));
            caps.Set("loadSession", SJ::Value::Bool(started.capabilities.loadSession));
            caps.Set("resume", SJ::Value::Bool(started.capabilities.resume));
            caps.Set("httpMcp", SJ::Value::Bool(started.capabilities.httpMcp));
            caps.Set("sseMcp", SJ::Value::Bool(started.capabilities.sseMcp));
            start.Set("capabilities", std::move(caps));
            SJ::Value methods = SJ::Value::Arr();
            for (const auto& m : started.authMethods) {
                SJ::Value method = SJ::Value::Obj();
                method.Set("id", S(m.id));
                method.Set("name", S(m.name));
                method.Set("description", S(m.description));
                method.Set("terminal", SJ::Value::Bool(m.terminal));
                method.Set("args", Strings(m.terminalArgs));
                methods.Push(std::move(method));
            }
            start.Set("authMethods", std::move(methods));
            const Acp::AuthMethod* preferred = AR::PreferredSignIn(started.authMethods);
            start.Set("preferredSignIn", S(preferred ? preferred->id : ""));
            SJ::Value modes = SJ::Value::Obj();
            modes.Set("current", S(started.modes.currentId));
            SJ::Value available = SJ::Value::Arr();
            for (const auto& m : started.modes.available) {
                SJ::Value mode = SJ::Value::Obj();
                mode.Set("id", S(m.id));
                mode.Set("name", S(m.name));
                mode.Set("description", S(m.description));
                available.Push(std::move(mode));
            }
            modes.Set("available", std::move(available));
            start.Set("modes", std::move(modes));
            SJ::Value options = SJ::Value::Arr();
            for (const auto& option : started.configOptions) {
                SJ::Value o2 = SJ::Value::Obj();
                o2.Set("id", S(option.id));
                o2.Set("name", S(option.name));
                o2.Set("category", S(option.category));
                o2.Set("type", S(option.type));
                o2.Set("currentValue", S(option.currentValue));
                SJ::Value choices = SJ::Value::Arr();
                for (const auto& c : option.choices) {
                    SJ::Value choice = SJ::Value::Obj();
                    choice.Set("value", S(c.value));
                    choice.Set("name", S(c.name));
                    if (!c.description.empty()) choice.Set("description", S(c.description));
                    if (!c.group.empty()) choice.Set("group", S(c.group));
                    choices.Push(std::move(choice));
                }
                o2.Set("choices", std::move(choices));
                options.Push(std::move(o2));
            }
            start.Set("configOptions", std::move(options));
            const Acp::ConfigOption* model = Acp::FindModelOption(started.configOptions);
            const Acp::ConfigOption* effort = Acp::FindEffortOption(started.configOptions);
            const Acp::ConfigOption* modeOption = Acp::FindModeOption(started.configOptions);
            start.Set("modelOption", S(model ? model->id : ""));
            start.Set("effortOption", S(effort ? effort->id : ""));
            start.Set("modeOption", S(modeOption ? modeOption->id : ""));
            std::vector<std::string> names;
            for (const auto& c : commands) names.push_back(c.name);
            start.Set("commandCount", SJ::Value::Int(std::int64_t(names.size())));
            if (names.size() > 40) names.resize(40);
            start.Set("commands", Strings(names));
            start.Set("authStatusEvent", S(authStatus));
            start.Set("warnings", Strings(warnings));
            start.Set("otherEvents", Strings(others));
            report.Set("start", std::move(start));
            std::printf("%s: start -> %s in %lu ms (error %s %s)\n", AR::AgentId(agent), answered ? startStates[int(started.state)] : "no_answer", startMs,
                        Acp::ErrorCodeName(started.error.code), started.error.detail.c_str());
            // No prompt is ever sent. Stop the adapter and everything it started.
            const unsigned long pid = session.processId();
            session.Dispose();
            const bool stopped = session.WaitStopped(15000);
            report.Set("stoppedCleanly", SJ::Value::Bool(stopped && (!pid || !ProcessAlive(pid))));
            report.Set("promptsSent", SJ::Value::Int(0));
            report.Set("result", S(answered ? "answered" : "no_answer"));
            if (!answered || !stopped) exitCode = 1;
        }
    }
    WriteFileText(reportPath, SJ::Serialize(report));
    return exitCode;
}
