// ACP client tests: pure helpers, then end to end against the fake agent (tests/acp/fake_agent.cpp).
//   acp_client_test.exe <fake-acp-agent.exe> <temporary folder>
// No real agent, no network. Every process started here is stopped before the test ends (checked).
#include <atomic>
#include <cstring>

#include "../../src/acp-client.h"
#include "../../src/strict-json.h"
#include "acp_test_util.h"

using namespace AcpTest;
using Acp::Event;
namespace SJ = StrictJson;

namespace {

std::wstring g_agent, g_tmp;
int g_case = 0;

struct Harness {
    Acp::Session session;
    std::vector<Event> events;
    std::size_t cursor = 0;
    std::wstring log;
    std::atomic<int> wakes{0};
    std::atomic<int> signInSeen{0};

    Harness() {
        log = g_tmp + L"\\agent-" + std::to_wstring(++g_case) + L".log";
        DeleteFileW(log.c_str());
        session.SetWake([this] { ++wakes; });
    }
    ~Harness() {
        session.Dispose();
        CHECK(session.WaitStopped(10000));
    }
    Acp::StartOptions Options(std::vector<std::wstring> args = {}, bool withLog = true) {
        if (withLog) {
            args.push_back(L"--log");
            args.push_back(log);
        }
        Acp::StartOptions o;
        o.cwd = Narrow(g_tmp);
        o.clientName = "browser-test";
        o.clientVersion = "9.9";
        o.resolve = [this, args](const Acp::ResolveContext& ctx) {
            if (ctx.signIn) ++signInSeen;
            Acp::Resolution r;
            r.launch.exe = g_agent;
            r.launch.args = args;
            r.launch.freshUserEnvironment = false;
            return r;
        };
        return o;
    }
    void Drain(unsigned waitMs) {
        session.WaitForEvents(waitMs);
        session.Poll(events);
    }
    // The next event of `type` after the cursor (earlier ones of other types are skipped, not lost).
    bool Wait(Event::Type type, Event& out, unsigned timeoutMs = 15000) {
        const DWORD until = GetTickCount() + timeoutMs;
        for (;;) {
            for (std::size_t i = cursor; i < events.size(); ++i)
                if (events[i].type == type) {
                    out = events[i];
                    cursor = i + 1;
                    return true;
                }
            const int left = int(until - GetTickCount());
            if (left <= 0) return false;
            Drain(unsigned(left < 200 ? left : 200));
        }
    }
    bool Started(Event& out, unsigned timeoutMs = 15000) { return Wait(Event::Type::Started, out, timeoutMs); }
    // Sends a prompt and waits for the end of the turn; returns the events of the turn.
    std::vector<Event> Turn(const std::string& text, Event& end, unsigned timeoutMs = 15000) {
        const std::size_t from = events.size();
        cursor = from;
        CHECK(session.Prompt({Acp::ContentBlock::Text(text)}));
        CHECK(Wait(Event::Type::TurnEnded, end, timeoutMs));
        return std::vector<Event>(events.begin() + std::ptrdiff_t(from), events.begin() + std::ptrdiff_t(cursor));
    }
    std::string LogText() { return ReadFileText(log); }
};

std::string Joined(const std::vector<Event>& events, Event::Type type) {
    std::string s;
    for (const auto& e : events)
        if (e.type == type) s += e.text;
    return s;
}
int Count(const std::vector<Event>& events, Event::Type type) {
    int n = 0;
    for (const auto& e : events) n += e.type == type;
    return n;
}
const Event* First(const std::vector<Event>& events, Event::Type type) {
    for (const auto& e : events)
        if (e.type == type) return &e;
    return nullptr;
}

// ---------------------------------------------------------------- pure helpers
void TestClassification() {
    using Acp::ErrorCode;
    CHECK(Acp::LooksLikeAuthError("Authentication required"));
    CHECK(Acp::LooksLikeAuthError("Auth is required to continue"));
    CHECK(Acp::LooksLikeAuthError("You are not logged in. Please run `claude auth login`."));
    CHECK(Acp::LooksLikeAuthError("please sign in first"));
    CHECK(Acp::LooksLikeAuthError("Error: 401 Unauthorized"));
    CHECK(Acp::LooksLikeAuthError("expired token"));
    CHECK(Acp::LooksLikeAuthError("Invalid API key provided"));
    CHECK(Acp::LooksLikeAuthError("run codex login"));
    CHECK(Acp::LooksLikeAuthError("Please run setup first"));
    CHECK(!Acp::LooksLikeAuthError("The author of this file is unknown"));
    CHECK(!Acp::LooksLikeAuthError("OAuth scopes look fine"));
    CHECK(!Acp::LooksLikeAuthError("file 4010 not found"));
    CHECK(!Acp::LooksLikeAuthError("rerun the build"));
    CHECK(!Acp::LooksLikeAuthError(""));

    CHECK(Acp::PlainFailure("prompt is too long: 250000 tokens") == ErrorCode::ContextTooLong);
    CHECK(Acp::PlainFailure("exceeds the context window") == ErrorCode::ContextTooLong);
    CHECK(Acp::PlainFailure("Your credit balance is too low") == ErrorCode::UsageLimit);
    CHECK(Acp::PlainFailure("429 quota exceeded") == ErrorCode::UsageLimit);  // the account's limit wins over the rate limit
    CHECK(Acp::PlainFailure("API Error: 529 overloaded_error") == ErrorCode::Overloaded);
    CHECK(Acp::PlainFailure("API Error: 429 rate_limit_error") == ErrorCode::RateLimited);
    CHECK(Acp::PlainFailure("Too Many Requests") == ErrorCode::RateLimited);
    CHECK(Acp::PlainFailure("API Error: 503 upstream") == ErrorCode::ServerError);
    CHECK(Acp::PlainFailure("status code 502") == ErrorCode::ServerError);
    CHECK(Acp::PlainFailure("Internal Server Error") == ErrorCode::ServerError);
    CHECK(Acp::PlainFailure("connect ECONNREFUSED 127.0.0.1:443") == ErrorCode::Network);
    CHECK(Acp::PlainFailure("fetch failed") == ErrorCode::Network);
    CHECK(Acp::PlainFailure("request timed out") == ErrorCode::Network);
    CHECK(Acp::PlainFailure("status 200 ok") == ErrorCode::None);
    CHECK(Acp::PlainFailure("line 5290 of the file") == ErrorCode::None);
    CHECK(Acp::PlainFailure("the file was not found") == ErrorCode::None);

    Acp::Error e = Acp::ClassifyAgentError(-32000, "Authentication required", "");
    CHECK(e.code == ErrorCode::AuthRequired && e.rpcCode == -32000 && e.detail == "Authentication required");
    e = Acp::ClassifyAgentError(-32603, "Internal error", "  API Error: 529 overloaded ");
    CHECK(e.code == ErrorCode::Overloaded && e.detail == "API Error: 529 overloaded");
    e = Acp::ClassifyAgentError(-32603, "Internal error", "");
    CHECK(e.code == ErrorCode::AgentError && e.detail == "Internal error");
    e = Acp::ClassifyAgentError(-32603, std::string(10000, 'x'), "");
    CHECK(e.detail.size() == Acp::MaxDetailBytes);
    e = Acp::ClassifyAgentError(1, "bad \xff byte", "");
    CHECK(SJ::ValidUtf8(e.detail));
    CHECK(std::strcmp(Acp::ErrorCodeName(ErrorCode::AuthRequired), "auth_required") == 0);
    CHECK(std::strcmp(Acp::ErrorCodeName(ErrorCode::NeedsPrepare), "needs_prepare") == 0);
}

void TestOptions() {
    std::vector<Acp::ConfigOption> options(4);
    options[0].id = "provider"; options[0].category = "model"; options[0].type = "select";
    options[1].id = "model"; options[1].category = "model"; options[1].type = "select";
    options[2].id = "reasoning"; options[2].category = "thought_level"; options[2].type = "select";
    options[3].id = "fast"; options[3].category = "model_config"; options[3].type = "boolean";
    CHECK(Acp::FindModelOption(options) == &options[1]);  // the option named "model" wins inside the category
    CHECK(Acp::FindEffortOption(options) == &options[2]);
    CHECK(Acp::FindModeOption(options) == nullptr);
    options.erase(options.begin() + 1);
    CHECK(Acp::FindModelOption(options) == &options[0]);
    CHECK(Acp::FindModelOption({}) == nullptr);
}

std::vector<std::wstring> BlockEntries(const std::wstring& block) {
    std::vector<std::wstring> out;
    for (const wchar_t* p = block.c_str(); *p; p += wcslen(p) + 1) out.emplace_back(p);
    return out;
}
void TestEnvironmentAndQuoting() {
    SetEnvironmentVariableW(L"ACP_TEST_KEEP", L"kept");
    SetEnvironmentVariableW(L"ACP_TEST_DROP", L"dropped");
    SetEnvironmentVariableW(L"Acp_Test_Replace", L"old");
    Acp::LaunchSpec spec;
    spec.freshUserEnvironment = false;
    spec.env = {{L"ACP_TEST_REPLACE", L"new"}, {L"ACP_TEST_ADDED", L"é space"}, {L"BAD=NAME", L"x"}, {L"", L"x"}};
    spec.unsetEnv = {L"acp_test_drop"};
    const std::wstring block = Acp::BuildEnvironmentBlock(spec);
    CHECK(block.size() >= 2 && block[block.size() - 1] == L'\0' && block[block.size() - 2] == L'\0');
    const auto entries = BlockEntries(block);
    int keep = 0, drop = 0, replace = 0, added = 0, bad = 0;
    for (const auto& e : entries) {
        keep += e == L"ACP_TEST_KEEP=kept";
        drop += e.rfind(L"ACP_TEST_DROP=", 0) == 0;
        replace += e == L"Acp_Test_Replace=new";  // replaced in place, name kept as it was
        added += e == L"ACP_TEST_ADDED=é space";
        bad += e.rfind(L"BAD=", 0) == 0 || e[0] == L'=' ? (e.rfind(L"BAD=", 0) == 0) : 0;
    }
    CHECK(keep == 1 && drop == 0 && replace == 1 && added == 1 && bad == 0);
    bool sorted = true;
    for (std::size_t i = 1; i < entries.size(); ++i) {
        const std::wstring a = entries[i - 1].substr(0, entries[i - 1].find(L'=', 1)), b = entries[i].substr(0, entries[i].find(L'=', 1));
        if (CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_GREATER_THAN) sorted = false;
    }
    CHECK(sorted);
    // A fresh user environment is rebuilt from the registry: variables set only in this process are not in it.
    Acp::LaunchSpec fresh;
    const auto freshEntries = BlockEntries(Acp::BuildEnvironmentBlock(fresh));
    bool leaked = false, hasPath = false, hasRoot = false;
    for (const auto& e : freshEntries) {
        leaked |= e.rfind(L"ACP_TEST_KEEP=", 0) == 0;
        hasPath |= _wcsnicmp(e.c_str(), L"PATH=", 5) == 0;
        hasRoot |= _wcsnicmp(e.c_str(), L"SystemRoot=", 11) == 0;
    }
    CHECK(!leaked && hasPath && hasRoot);

    CHECK(Acp::QuoteArgument(L"plain") == L"plain");
    CHECK(Acp::QuoteArgument(L"") == L"\"\"");
    CHECK(Acp::QuoteArgument(L"a b") == L"\"a b\"");
    CHECK(Acp::QuoteArgument(L"C:\\dossier é\\x y\\") == L"\"C:\\dossier é\\x y\\\\\"");
    CHECK(Acp::QuoteArgument(L"say \"hi\"") == L"\"say \\\"hi\\\"\"");
    CHECK(Acp::QuoteArgument(L"back\\\"q") == L"\"back\\\\\\\"q\"");
}

// ---------------------------------------------------------------- end to end
void TestStartAndReply() {
    Harness h;
    Acp::StartOptions o = h.Options();
    o.mcpServers.push_back({"browser-annotations", "C:\\bin\\bridge é.exe", {"--store", "C:\\data"}, {{"K", "v"}}});
    CHECK(h.session.phase() == Acp::Phase::Idle);
    CHECK(h.session.Start(o));
    CHECK(!h.session.Start(o));  // already starting
    Event started;
    CHECK(h.Started(started));
    const Acp::StartResult& s = started.started;
    CHECK(s.state == Acp::StartState::Ready && s.error.Ok());
    CHECK(!s.sessionId.empty() && !s.resumed && s.protocolVersion == 1);
    CHECK(s.agentName == "fake-agent" && s.agentTitle == "Fake Agent" && s.agentVersion == "1.2.3");
    CHECK(s.capabilities.image && s.capabilities.embeddedContext && s.capabilities.httpMcp && !s.capabilities.sseMcp);
    CHECK(!s.capabilities.resume && !s.capabilities.loadSession);
    CHECK(s.authMethods.size() == 2 && s.authMethods[0].id == "fake-login" && !s.authMethods[0].terminal);
    CHECK(s.authMethods.size() == 2 && s.authMethods[1].terminal && s.authMethods[1].terminalArgs.size() == 2 &&
          s.authMethods[1].terminalArgs[1] == "login" && s.authMethods[1].terminalEnv.size() == 1);
    CHECK(s.modes.currentId == "default" && s.modes.available.size() == 3 && s.modes.available[2].id == "plan" &&
          s.modes.available[0].description == "Demande avant chaque modification");
    // Models, efforts and modes come from the agent, grouped lists are flattened.
    const Acp::ConfigOption* model = Acp::FindModelOption(s.configOptions);
    CHECK(model && model->currentValue == "sonnet" && model->choices.size() == 3 && model->choices[0].name == "Opus 5.5" &&
          model->choices[0].group == "Claude" && model->choices[0].description == "Le plus capable" && model->choices[2].value == "haiku");
    const Acp::ConfigOption* effort = Acp::FindEffortOption(s.configOptions);
    CHECK(effort && effort->currentValue == "medium" && effort->choices.size() == 3);
    CHECK(Acp::FindModeOption(s.configOptions) && s.configOptions.size() == 4 && s.configOptions[3].type == "boolean" &&
          s.configOptions[3].currentValue == "false");
    CHECK(h.session.phase() == Acp::Phase::Ready && h.session.processId() != 0);
    const Event* login = First(h.events, Event::Type::AuthStatus);
    CHECK(login && login->text == "account");  // the kind only: the account's e-mail is never kept

    // What the agent received: capabilities as Gui declares them, the absolute cwd, the stdio MCP server.
    const std::string log = h.LogText();
    SJ::Value init;
    CHECK(SJ::Parse(log.substr(0, log.find('\n')), init));
    const SJ::Value* params = init.Get("params");
    const SJ::Value* caps = params ? params->Get("clientCapabilities") : nullptr;
    CHECK(init.Get("method") && init.Get("method")->string == "initialize" && params && params->Get("protocolVersion")->integer == 1);
    CHECK(caps && !caps->Get("fs")->Get("readTextFile")->boolean && !caps->Get("fs")->Get("writeTextFile")->boolean &&
          !caps->Get("terminal")->boolean && caps->Get("auth")->Get("terminal")->boolean && caps->Get("elicitation")->Get("form"));
    CHECK(params && params->Get("clientInfo")->Get("name")->string == "browser-test");
    CHECK(Contains(log, "\"method\":\"session/new\"") && Contains(log, "\"name\":\"browser-annotations\"") &&
          Contains(log, "\"command\":\"C:\\\\bin\\\\bridge é.exe\"") && Contains(log, "\"args\":[\"--store\",\"C:\\\\data\"]") &&
          Contains(log, "\"env\":[{\"name\":\"K\",\"value\":\"v\"}]"));
    CHECK(Contains(log, "cwd " + Narrow(g_tmp)));

    // The commands sent right after session/new arrive as an event.
    Event commands;
    CHECK(h.Wait(Event::Type::CommandsChanged, commands) && commands.commands.size() == 1 && commands.commands[0].name == "init");

    Event end;
    const auto turn = h.Turn("reply please", end);
    CHECK(end.stopReason == "end_turn" && end.error.Ok());
    CHECK(Joined(turn, Event::Type::MessageChunk) == "Bonjour — voici la réponse.");
    CHECK(Count(turn, Event::Type::MessageChunk) == 3 && First(turn, Event::Type::MessageChunk)->messageId == "m1");
    const Event* usage = First(turn, Event::Type::Usage);
    CHECK(usage && usage->usage.used == 1234 && usage->usage.size == 200000 && usage->usage.hasCost && usage->usage.costCurrency == "USD" &&
          usage->usage.costAmount > 0.012 && usage->usage.costAmount < 0.013);
    CHECK(h.session.phase() == Acp::Phase::Ready);
    CHECK(h.wakes > 0 && h.wakes <= int(h.events.size()));

    // Thoughts, a second turn on the same session.
    const auto think = h.Turn("think about it", end);
    CHECK(Joined(think, Event::Type::ThoughtChunk) == "Je regarde le composant et ses styles." && end.stopReason == "end_turn");
    // Stop reasons are passed through.
    h.Turn("refusal", end);
    CHECK(end.stopReason == "refusal");
    h.Turn("max_tokens", end);
    CHECK(end.stopReason == "max_tokens");
    // Text, resource link and image blocks reach the agent.
    h.cursor = h.events.size();
    CHECK(h.session.Prompt({Acp::ContentBlock::Text("big"), Acp::ContentBlock::Image("image/png", std::string(1000, 'A')),
                            Acp::ContentBlock::ResourceLink("file:///C:/projet/src/Hero.tsx", "Hero.tsx", "Hero", "text/plain")}));
    CHECK(!h.session.Prompt({Acp::ContentBlock::Text("second")}));  // one turn at a time
    CHECK(h.session.phase() == Acp::Phase::Prompting);
    Event chunk;
    CHECK(h.Wait(Event::Type::MessageChunk, chunk) && chunk.text == "received 1000 image bytes, 1 links");
    CHECK(h.Wait(Event::Type::TurnEnded, end));
    CHECK(Contains(h.LogText(), "\"type\":\"resource_link\",\"uri\":\"file:///C:/projet/src/Hero.tsx\",\"name\":\"Hero.tsx\",\"title\":\"Hero\",\"mimeType\":\"text/plain\""));
    // Commands and title updates.
    const auto misc = h.Turn("commands title", end);
    const Event* cmd = First(misc, Event::Type::CommandsChanged);
    CHECK(cmd && cmd->commands.size() == 1 && cmd->commands[0].name == "review" && cmd->commands[0].inputHint == "fichier");
    const Event* title = First(misc, Event::Type::SessionTitle);
    CHECK(title && title->text == "Bouton du héros");
}

void TestToolsPlanPermission() {
    Harness h;
    CHECK(h.session.Start(h.Options()));
    Event started, end;
    CHECK(h.Started(started) && started.started.state == Acp::StartState::Ready);

    const auto tool = h.Turn("tool", end);
    const Event* call = First(tool, Event::Type::ToolCall);
    CHECK(call && call->tool.id == "call-1" && call->tool.title == "Modifier src/Hero.tsx" && call->tool.kind == "edit" && call->tool.status == "pending");
    CHECK(call && call->tool.locations.size() == 1 && call->tool.locations[0].path == "C:\\projet\\src\\Hero.tsx" && call->tool.locations[0].line == 12);
    CHECK(call && call->tool.rawInput == "{\"file_path\":\"C:\\\\projet\\\\src\\\\Hero.tsx\"}");
    CHECK(Count(tool, Event::Type::ToolCallUpdate) == 2);
    const Event* last = nullptr;
    for (const auto& e : tool)
        if (e.type == Event::Type::ToolCallUpdate) {
            if (!last) CHECK(e.tool.hasStatus && e.tool.status == "in_progress" && !e.tool.hasTitle && !e.tool.hasContent && !e.tool.hasLocations);
            last = &e;
        }
    CHECK(last && last->tool.status == "completed" && last->tool.hasTitle && last->tool.title == "Modifié src/Hero.tsx" && last->tool.hasContent);
    CHECK(last && last->tool.diffs.size() == 1 && last->tool.diffs[0].path == "C:\\projet\\src\\Hero.tsx" && last->tool.diffs[0].hasOldText &&
          last->tool.diffs[0].oldText == "<button className=\"p-2\">\n" && last->tool.diffs[0].newText == "<button className=\"p-4\">\n" &&
          !last->tool.diffs[0].truncated && last->tool.text == "1 ligne modifiée");

    const auto plan = h.Turn("plan", end);
    CHECK(Count(plan, Event::Type::Plan) == 2);
    const Event* p = First(plan, Event::Type::Plan);
    CHECK(p && p->plan.size() == 3 && p->plan[0].content == "Lire Hero.tsx" && p->plan[0].status == "in_progress" && p->plan[1].priority == "medium");

    // Permission: answered by option.
    h.cursor = h.events.size();
    CHECK(h.session.Prompt({Acp::ContentBlock::Text("permission")}));
    Event ask;
    CHECK(h.Wait(Event::Type::PermissionRequest, ask));
    CHECK(!ask.permission.question && ask.permission.options.size() == 3 && ask.permission.options[0].id == "allow" &&
          ask.permission.options[0].kind == "allow_once" && ask.permission.options[1].kind == "allow_always" &&
          ask.permission.options[2].name == "Refuser");
    CHECK(ask.permission.toolCall.id == "call-1" && ask.permission.toolCall.kind == "edit" && ask.permission.toolCall.diffs.size() == 1 &&
          ask.permission.toolCall.locations.size() == 1 && ask.permission.toolCall.locations[0].line == -1);
    CHECK(!h.session.RespondPermission("nope", "allow"));
    CHECK(h.session.RespondPermission(ask.permission.requestId, "allow"));
    CHECK(!h.session.RespondPermission(ask.permission.requestId, "allow"));  // answered once
    CHECK(h.Wait(Event::Type::TurnEnded, end) && end.stopReason == "end_turn");
    CHECK(Contains(h.LogText(), "permission-outcome selected allow"));

    h.cursor = h.events.size();
    CHECK(h.session.Prompt({Acp::ContentBlock::Text("permission")}));
    CHECK(h.Wait(Event::Type::PermissionRequest, ask));
    CHECK(h.session.RespondPermission(ask.permission.requestId, "reject"));
    Event chunk;
    CHECK(h.Wait(Event::Type::MessageChunk, chunk) && chunk.text == "refused");
    CHECK(h.Wait(Event::Type::TurnEnded, end) && end.stopReason == "end_turn");

    // Cancel while a permission waits: the request is answered "cancelled" before session/cancel, and dismissed.
    h.cursor = h.events.size();
    const std::size_t mark = h.LogText().size();
    CHECK(h.session.Prompt({Acp::ContentBlock::Text("permission")}));
    CHECK(h.Wait(Event::Type::PermissionRequest, ask));
    h.session.Cancel();
    h.session.Cancel();  // idempotent
    Event dismissed;
    CHECK(h.Wait(Event::Type::PermissionDismissed, dismissed) && dismissed.permission.requestId == ask.permission.requestId);
    CHECK(h.Wait(Event::Type::TurnEnded, end) && end.stopReason == "cancelled" && end.error.Ok());
    CHECK(!h.session.RespondPermission(ask.permission.requestId, "allow"));
    const std::string after = h.LogText().substr(mark);
    const std::size_t outcome = after.find("\"outcome\":{\"outcome\":\"cancelled\"}"), cancel = after.find("\"method\":\"session/cancel\"");
    CHECK(outcome != std::string::npos && cancel != std::string::npos && outcome < cancel);
    CHECK(h.session.phase() == Acp::Phase::Ready);
    h.session.Cancel();  // no turn: nothing is sent
    Sleep(100);
    CHECK(h.LogText().substr(mark).find("session/cancel", cancel + 30) == std::string::npos);

    // Questions of the agent (elicitation form): one card per single-choice question, Skip first.
    h.cursor = h.events.size();
    CHECK(h.session.Prompt({Acp::ContentBlock::Text("question")}));
    CHECK(h.Wait(Event::Type::PermissionRequest, ask));
    CHECK(ask.permission.question && ask.permission.toolCall.kind == "question" && ask.permission.toolCall.title == "Quelle taille pour le bouton ?");
    CHECK(ask.permission.options.size() == 3 && ask.permission.options[0].id == "skip" && ask.permission.options[0].kind == "reject_once" &&
          ask.permission.options[1].id == "0" && ask.permission.options[1].name == "Petit" && ask.permission.options[1].kind == "answer" &&
          ask.permission.options[2].description == "48 px" && ask.permission.message.empty());
    CHECK(h.session.RespondPermission(ask.permission.requestId, "1"));
    Event second;
    CHECK(h.Wait(Event::Type::PermissionRequest, second));
    CHECK(second.permission.question && second.permission.requestId != ask.permission.requestId &&
          second.permission.toolCall.title == "Deux questions avant de continuer" && second.permission.options.size() == 3 &&
          second.permission.options[2].name == "vert");
    CHECK(!h.session.RespondPermission(ask.permission.requestId, "0"));  // the first question is no longer on screen
    CHECK(h.session.RespondPermission(second.permission.requestId, "0"));
    CHECK(h.Wait(Event::Type::MessageChunk, chunk) && chunk.text == "question:accept:l:bleu");
    CHECK(h.Wait(Event::Type::TurnEnded, end) && end.stopReason == "end_turn");
    // Skip: the agent carries on without the answer.
    h.cursor = h.events.size();
    CHECK(h.session.Prompt({Acp::ContentBlock::Text("question")}));
    CHECK(h.Wait(Event::Type::PermissionRequest, ask));
    CHECK(h.session.RespondPermission(ask.permission.requestId, "skip"));
    CHECK(h.Wait(Event::Type::MessageChunk, chunk) && chunk.text == "question:decline::");
    CHECK(h.Wait(Event::Type::TurnEnded, end));
    // Cancel while a question waits.
    h.cursor = h.events.size();
    CHECK(h.session.Prompt({Acp::ContentBlock::Text("question")}));
    CHECK(h.Wait(Event::Type::PermissionRequest, ask));
    h.session.Cancel();
    CHECK(h.Wait(Event::Type::PermissionDismissed, dismissed) && dismissed.permission.requestId == ask.permission.requestId);
    CHECK(h.Wait(Event::Type::TurnEnded, end) && end.stopReason == "cancelled");
}

void TestModesAndOptions() {
    Harness h;
    CHECK(h.session.Start(h.Options()));
    Event started, e, end;
    CHECK(h.Started(started));
    h.cursor = h.events.size();
    CHECK(h.session.SetMode("plan"));
    CHECK(h.Wait(Event::Type::ModesChanged, e) && e.modes.currentId == "plan" && e.modes.available.size() == 3);
    CHECK(h.Wait(Event::Type::ConfigOptionsChanged, e));  // the "mode" option follows the mode
    CHECK(Acp::FindModeOption(e.configOptions) && Acp::FindModeOption(e.configOptions)->currentValue == "plan");
    CHECK(Contains(h.LogText(), "\"method\":\"session/set_mode\",\"params\":{\"sessionId\":\"" + started.started.sessionId + "\",\"modeId\":\"plan\"}"));

    h.cursor = h.events.size();
    CHECK(h.session.SetMode("nonsense"));
    CHECK(h.Wait(Event::Type::RequestFailed, e) && e.request == "set_mode" && e.error.code == Acp::ErrorCode::AgentError &&
          e.error.rpcCode == -32602 && e.error.detail == "unknown mode nonsense");

    // Choosing Haiku changes what the agent offers for the effort: the whole list comes back.
    h.cursor = h.events.size();
    CHECK(h.session.SetConfigOption("effort", "high"));
    CHECK(h.Wait(Event::Type::ConfigOptionsChanged, e) && Acp::FindEffortOption(e.configOptions)->currentValue == "high");
    CHECK(h.session.SetConfigOption("model", "haiku"));
    CHECK(h.Wait(Event::Type::ConfigOptionsChanged, e));
    CHECK(Acp::FindModelOption(e.configOptions)->currentValue == "haiku" && Acp::FindEffortOption(e.configOptions)->choices.size() == 2 &&
          Acp::FindEffortOption(e.configOptions)->currentValue == "medium");
    CHECK(Contains(h.LogText(), "\"configId\":\"model\",\"value\":\"haiku\""));
    h.cursor = h.events.size();
    CHECK(h.session.SetConfigOption("effort", "high"));
    CHECK(h.Wait(Event::Type::RequestFailed, e) && e.request == "set_config_option" && Contains(e.error.detail, "unknown value high"));
    // A boolean option is sent as {type: "boolean", value: true}.
    CHECK(h.session.SetConfigOption("fast", "true"));
    CHECK(h.Wait(Event::Type::ConfigOptionsChanged, e));
    CHECK(Contains(h.LogText(), "\"configId\":\"fast\",\"type\":\"boolean\",\"value\":true"));
    // The mode set through its option keeps the session mode in step.
    h.cursor = h.events.size();
    CHECK(h.session.SetConfigOption("mode", "acceptEdits"));
    CHECK(h.Wait(Event::Type::ModesChanged, e) && e.modes.currentId == "acceptEdits");
    // The agent changes the mode itself.
    const auto turn = h.Turn("mode", end);
    const Event* changed = First(turn, Event::Type::ModesChanged);
    CHECK(changed && changed->modes.currentId == "plan");
    const Event* options = First(turn, Event::Type::ConfigOptionsChanged);
    CHECK(options && Acp::FindModeOption(options->configOptions)->currentValue == "plan");
    CHECK(!h.session.SetMode("") && !h.session.SetConfigOption("", "x"));
}

void TestAuth() {
    Harness h;
    const std::wstring authFile = g_tmp + L"\\auth-" + std::to_wstring(g_case) + L".marker";
    DeleteFileW(authFile.c_str());
    CHECK(!h.session.Authenticate("fake-login"));  // nothing was started yet
    CHECK(h.session.Start(h.Options({L"--auth-file", authFile})));
    Event started, end;
    CHECK(h.Started(started));
    CHECK(started.started.state == Acp::StartState::AuthRequired && started.started.error.code == Acp::ErrorCode::AuthRequired &&
          started.started.error.rpcCode == -32000 && started.started.error.detail == "Please run `fake login` first");
    CHECK(started.started.authMethods.size() == 2 && started.started.sessionId.empty());
    const Event* login = First(h.events, Event::Type::AuthStatus);
    CHECK(login && login->text == "none");  // signed out, said by the agent itself
    CHECK(h.session.phase() == Acp::Phase::Stopped && h.session.WaitStopped(10000));
    CHECK(!h.session.Prompt({Acp::ContentBlock::Text("x")}));
    CHECK(!h.session.Authenticate("fake-terminal"));  // a terminal method is run by the client itself
    CHECK(h.signInSeen == 0);
    CHECK(h.session.Authenticate("fake-login"));
    CHECK(h.Started(started));
    CHECK(started.started.state == Acp::StartState::Ready && h.signInSeen == 1 && Exists(authFile));
    CHECK(Contains(h.LogText(), "\"method\":\"authenticate\",\"params\":{\"methodId\":\"fake-login\"}"));
    h.Turn("reply", end);
    CHECK(end.stopReason == "end_turn");
    // A login that expires in the middle of a session.
    h.Turn("error-auth", end);
    CHECK(end.stopReason.empty() && end.error.code == Acp::ErrorCode::AuthRequired && end.error.rpcCode == -32000);
    h.Turn("error-rate", end);
    CHECK(end.error.code == Acp::ErrorCode::RateLimited && Contains(end.error.detail, "API Error: 429"));
    CHECK(h.session.phase() == Acp::Phase::Ready);
    // A failing sign-in leaves the session signed out, with the reason.
    DeleteFileW(authFile.c_str());
    CHECK(h.session.Authenticate("unknown-method"));
    CHECK(h.Started(started));
    CHECK(started.started.state == Acp::StartState::AuthRequired && started.started.error.code == Acp::ErrorCode::AgentError &&
          started.started.error.detail == "unknown auth method");
    DeleteFileW(authFile.c_str());
}

void TestFailures() {
    {  // The resolver says the CLI is missing: no process at all.
        Harness h;
        Acp::StartOptions o = h.Options();
        o.resolve = [](const Acp::ResolveContext&) {
            Acp::Resolution r;
            r.error.code = Acp::ErrorCode::NotInstalled;
            return r;
        };
        CHECK(h.session.Start(o));
        Event started;
        CHECK(h.Started(started) && started.started.state == Acp::StartState::NotInstalled && started.started.error.code == Acp::ErrorCode::NotInstalled);
        CHECK(h.session.processId() == 0 && h.session.WaitStopped(5000) && h.session.phase() == Acp::Phase::Stopped);
        o.resolve = [](const Acp::ResolveContext&) {
            Acp::Resolution r;
            r.error.code = Acp::ErrorCode::NeedsPrepare;
            return r;
        };
        CHECK(h.session.Start(o));
        CHECK(h.Started(started) && started.started.state == Acp::StartState::NeedsPrepare);
        o.resolve = [](const Acp::ResolveContext&) {
            Acp::Resolution r;
            r.error.code = Acp::ErrorCode::CliTooOld;
            r.error.detail = "2.0.1";
            return r;
        };
        CHECK(h.session.Start(o));
        CHECK(h.Started(started) && started.started.state == Acp::StartState::NotInstalled && started.started.error.code == Acp::ErrorCode::CliTooOld &&
              started.started.error.detail == "2.0.1");
        // An executable that does not exist.
        o.resolve = [](const Acp::ResolveContext&) {
            Acp::Resolution r;
            r.launch.exe = g_tmp + L"\\missing.exe";
            r.launch.freshUserEnvironment = false;
            return r;
        };
        CHECK(h.session.Start(o));
        CHECK(h.Started(started) && started.started.state == Acp::StartState::Failed && started.started.error.code == Acp::ErrorCode::SpawnFailed &&
              started.started.error.detail == "CreateProcess 2");
    }
    {  // The process dies before answering: its stderr says why.
        Harness h;
        CHECK(h.session.Start(h.Options({L"--crash-at", L"initialize"})));
        Event started;
        CHECK(h.Started(started));
        CHECK(started.started.state == Acp::StartState::Failed && started.started.error.code == Acp::ErrorCode::ProcessExited &&
              started.started.error.detail == "fake-agent: crash at initialize: cannot find the CLI");
        CHECK(h.session.WaitStopped(5000) && h.session.phase() == Acp::Phase::Stopped);
        Event none;
        CHECK(!h.Wait(Event::Type::Exited, none, 200));  // the failed start is the only report
        CHECK(h.session.Start(h.Options({L"--crash-at", L"new"})));
        CHECK(h.Started(started) && started.started.error.code == Acp::ErrorCode::ProcessExited && started.started.agentName == "fake-agent");
    }
    {  // It dies in the middle of a turn: the turn ends with the error, then Exited.
        Harness h;
        CHECK(h.session.Start(h.Options()));
        Event started, end, exited, chunk;
        CHECK(h.Started(started));
        const unsigned long pid = h.session.processId();
        h.cursor = h.events.size();
        CHECK(h.session.Prompt({Acp::ContentBlock::Text("crash")}));
        CHECK(h.Wait(Event::Type::MessageChunk, chunk) && chunk.text == "Je commence");
        CHECK(h.Wait(Event::Type::TurnEnded, end));
        CHECK(end.stopReason.empty() && end.error.code == Acp::ErrorCode::ProcessExited && Contains(end.error.detail, "boom: the model backend went away"));
        CHECK(h.Wait(Event::Type::Exited, exited) && exited.exitCode == 7 && exited.error.code == Acp::ErrorCode::ProcessExited);
        CHECK(h.session.phase() == Acp::Phase::Stopped && !h.session.Prompt({Acp::ContentBlock::Text("x")}) && !h.session.SetMode("plan"));
        CHECK(h.session.WaitStopped(5000) && !ProcessAlive(pid));
        // The same object starts again.
        CHECK(h.session.Start(h.Options()));
        CHECK(h.Started(started) && started.started.state == Acp::StartState::Ready);
        h.Turn("reply", end);
        CHECK(end.stopReason == "end_turn");
    }
    {  // It dies while a permission waits: the card is dismissed.
        Harness h;
        CHECK(h.session.Start(h.Options({L"--crash-at", L"none"})));
        Event started, ask, dismissed, exited;
        CHECK(h.Started(started));
        CHECK(h.session.Prompt({Acp::ContentBlock::Text("permission")}));
        CHECK(h.Wait(Event::Type::PermissionRequest, ask));
        const unsigned long pid = h.session.processId();
        HANDLE p = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        CHECK(p && TerminateProcess(p, 9));
        if (p) CloseHandle(p);
        CHECK(h.Wait(Event::Type::PermissionDismissed, dismissed) && dismissed.permission.requestId == ask.permission.requestId);
        CHECK(h.Wait(Event::Type::Exited, exited) && exited.exitCode == 9);
    }
    {  // No answer to initialize: time out, and the silent process is killed with its job.
        Harness h;
        Acp::StartOptions o = h.Options({L"--sleep"}, false);
        o.timeouts.initializeMs = 400;
        o.timeouts.exitGraceMs = 300;
        const DWORD t0 = GetTickCount();
        CHECK(h.session.Start(o));
        Event started;
        CHECK(h.Started(started));
        CHECK(started.started.state == Acp::StartState::Failed && started.started.error.code == Acp::ErrorCode::Timeout &&
              started.started.error.detail == "initialize");
        CHECK(GetTickCount() - t0 >= 350 && GetTickCount() - t0 < 5000);
        CHECK(h.session.WaitStopped(5000) && h.session.processId() == 0);
    }
    {  // A protocol version we do not speak.
        Harness h;
        CHECK(h.session.Start(h.Options({L"--protocol", L"2"})));
        Event started;
        CHECK(h.Started(started) && started.started.state == Acp::StartState::Failed && started.started.error.code == Acp::ErrorCode::Protocol &&
              started.started.error.detail == "protocolVersion 2" && started.started.protocolVersion == 2);
    }
    {  // Lines that are not JSON-RPC are reported and ignored; the session goes on.
        Harness h;
        CHECK(h.session.Start(h.Options({L"--garbage"})));
        Event started, end, warning;
        CHECK(h.Started(started) && started.started.state == Acp::StartState::Ready);
        h.cursor = 0;  // the banner came before the answer to initialize
        CHECK(h.Wait(Event::Type::Warning, warning) && Contains(warning.text, "invalid_json"));
        CHECK(Count(h.events, Event::Type::Warning) == 2);  // the banner and the broken line; the empty line is skipped
        const auto turn = h.Turn("garbage reply", end);
        CHECK(end.stopReason == "end_turn" && Joined(turn, Event::Type::MessageChunk) == "Bonjour — voici la réponse.");
        CHECK(Count(turn, Event::Type::Warning) == 4);  // not JSON, truncated, an array, a duplicate key
    }
}

void TestCancelAndDispose() {
    Harness h;
    CHECK(h.session.Start(h.Options()));
    Event started, end, chunk;
    CHECK(h.Started(started));
    h.cursor = h.events.size();
    CHECK(h.session.Prompt({Acp::ContentBlock::Text("slow")}));
    CHECK(h.Wait(Event::Type::MessageChunk, chunk));
    const DWORD t0 = GetTickCount();
    h.session.Cancel();
    CHECK(h.Wait(Event::Type::TurnEnded, end) && end.stopReason == "cancelled" && end.error.Ok());
    CHECK(GetTickCount() - t0 < 2000);
    h.Turn("reply", end);  // the session is still usable
    CHECK(end.stopReason == "end_turn");

    // Dispose in the middle of a turn: nothing is delivered afterwards, the agent is gone, and the object restarts.
    CHECK(h.session.Prompt({Acp::ContentBlock::Text("slow")}));
    CHECK(h.Wait(Event::Type::MessageChunk, chunk));
    const unsigned long pid = h.session.processId();
    CHECK(pid && ProcessAlive(pid));
    const DWORD t1 = GetTickCount();
    h.session.Dispose();
    CHECK(GetTickCount() - t1 < 200);  // non-blocking
    CHECK(h.session.WaitStopped(10000) && !ProcessAlive(pid));
    CHECK(GetTickCount() - t1 < 1500);  // the fake agent exits when its stdin closes: no need for the grace period
    std::vector<Event> leftover;
    Sleep(150);
    CHECK(h.session.Poll(leftover) == 0);
    CHECK(h.session.phase() == Acp::Phase::Stopped && !h.session.Prompt({Acp::ContentBlock::Text("x")}));
    h.session.Dispose();  // twice is fine
    h.events.clear();
    h.cursor = 0;
    CHECK(h.session.Start(h.Options()));
    CHECK(h.Started(started) && started.started.state == Acp::StartState::Ready);
}

void TestJobObject() {
    Harness h;
    CHECK(h.session.Start(h.Options({L"--child"})));
    Event started;
    CHECK(h.Started(started) && started.started.state == Acp::StartState::Ready);
    const std::string log = h.LogText();
    const std::size_t at = log.find("child ");
    CHECK(at != std::string::npos);
    const unsigned long child = at == std::string::npos ? 0 : std::strtoul(log.c_str() + at + 6, nullptr, 10);
    const unsigned long agent = h.session.processId();
    CHECK(child && ProcessAlive(child) && agent && ProcessAlive(agent));
    h.session.Dispose();
    CHECK(h.session.WaitStopped(10000));
    bool gone = false;
    for (int i = 0; i < 40 && !gone; ++i) {
        gone = !ProcessAlive(child) && !ProcessAlive(agent);
        if (!gone) Sleep(50);
    }
    CHECK(gone);  // what the agent started dies with it

    // An agent that ignores the end of its input is killed after the grace period.
    Harness stubborn;
    Acp::StartOptions o = stubborn.Options({L"--sleep"}, false);
    o.timeouts.exitGraceMs = 300;
    CHECK(stubborn.session.Start(o));
    unsigned long pid = 0;
    for (int i = 0; i < 100 && !pid; ++i) {
        pid = stubborn.session.processId();
        if (!pid) Sleep(20);
    }
    CHECK(pid && ProcessAlive(pid));
    const DWORD t0 = GetTickCount();
    stubborn.session.Dispose();
    CHECK(stubborn.session.WaitStopped(5000) && !ProcessAlive(pid));
    CHECK(GetTickCount() - t0 >= 250);
}

void TestBigPromptDoesNotBlock() {
    Harness h;
    CHECK(h.session.Start(h.Options({L"--read-delay", L"1200"}, false)));
    Event started, chunk, end;
    CHECK(h.Started(started));
    std::string image(3u << 20, 'A');
    const DWORD t0 = GetTickCount();
    CHECK(h.session.Prompt({Acp::ContentBlock::Text("big"), Acp::ContentBlock::Image("image/png", std::move(image))}));
    const DWORD sent = GetTickCount() - t0;
    CHECK(sent < 300);  // the agent is not reading: the write waits on the session's thread, not here
    CHECK(h.Wait(Event::Type::MessageChunk, chunk) && chunk.text == "received 3145728 image bytes, 0 links");
    CHECK(h.Wait(Event::Type::TurnEnded, end) && end.stopReason == "end_turn");
    CHECK(GetTickCount() - t0 >= 1000);
    std::printf("  3 MiB prompt: Prompt() returned in %lu ms, turn ended after %lu ms\n", sent, GetTickCount() - t0);

    // An agent without image support never receives the image.
    Harness plain;
    CHECK(plain.session.Start(plain.Options({L"--no-images"})));
    CHECK(plain.Started(started) && !started.started.capabilities.image);
    plain.cursor = plain.events.size();
    CHECK(plain.session.Prompt({Acp::ContentBlock::Text("big"), Acp::ContentBlock::Image("image/png", "AAAA")}));
    CHECK(plain.Wait(Event::Type::MessageChunk, chunk) && chunk.text == "received 0 image bytes, 0 links");
    CHECK(!Contains(plain.LogText(), "\"type\":\"image\""));
}

void TestResume() {
    Harness h;
    Acp::StartOptions o = h.Options({L"--resume"});
    o.resumeSessionId = "sess-known";
    CHECK(h.session.Start(o));
    Event started, end;
    CHECK(h.Started(started));
    CHECK(started.started.state == Acp::StartState::Ready && started.started.resumed && started.started.sessionId == "sess-known" &&
          started.started.capabilities.resume && started.started.capabilities.loadSession && started.started.modes.available.size() == 3);
    CHECK(Contains(h.LogText(), "\"method\":\"session/resume\"") && !Contains(h.LogText(), "\"method\":\"session/new\""));
    const auto turn = h.Turn("reply", end);
    CHECK(Joined(turn, Event::Type::MessageChunk) == "Bonjour — voici la réponse.");
    h.session.Dispose();
    CHECK(h.session.WaitStopped(10000));

    // A session the agent no longer has: resume, then load, then a new session.
    h.events.clear();
    h.cursor = 0;
    DeleteFileW(h.log.c_str());
    o.resumeSessionId = "sess-gone";
    CHECK(h.session.Start(o));
    CHECK(h.Started(started));
    CHECK(started.started.state == Acp::StartState::Ready && !started.started.resumed && started.started.sessionId != "sess-gone");
    const std::string log = h.LogText();
    const std::size_t resume = log.find("\"method\":\"session/resume\""), load = log.find("\"method\":\"session/load\""), fresh = log.find("\"method\":\"session/new\"");
    CHECK(resume != std::string::npos && load != std::string::npos && fresh != std::string::npos && resume < load && load < fresh);
    h.session.Dispose();
    CHECK(h.session.WaitStopped(10000));

    // An agent that cannot resume gets a new session without being asked.
    h.events.clear();
    h.cursor = 0;
    DeleteFileW(h.log.c_str());
    Acp::StartOptions plain = h.Options();
    plain.resumeSessionId = "sess-known";
    CHECK(h.session.Start(plain));
    CHECK(h.Started(started) && !started.started.resumed && !Contains(h.LogText(), "session/resume") && !Contains(h.LogText(), "session/load"));
}

void TestEnvironmentAndCwd() {
    Harness h;
    const std::wstring cwd = g_tmp + L"\\projet é espace";
    CHECK(MakeDirs(cwd));
    const std::wstring dump = g_tmp + L"\\env-dump.txt";
    DeleteFileW(dump.c_str());
    SetEnvironmentVariableW(L"FAKE_REMOVED", L"1");
    Acp::StartOptions o;
    o.cwd = Narrow(cwd);
    o.resolve = [&](const Acp::ResolveContext&) {
        Acp::Resolution r;
        r.launch.exe = g_agent;
        r.launch.args = {L"--env-dump", dump, L"--log", h.log};
        r.launch.freshUserEnvironment = false;
        r.launch.env = {{L"FAKE_EXTRA", L"valeur é"}, {L"BROWSER", L"none"}};
        r.launch.unsetEnv = {L"FAKE_REMOVED"};
        return r;
    };
    CHECK(h.session.Start(o));
    Event started;
    CHECK(h.Started(started) && started.started.state == Acp::StartState::Ready);
    const std::string text = ReadFileText(dump);
    CHECK(Contains(text, "FAKE_EXTRA=valeur é\n") && Contains(text, "BROWSER=none\n") && Contains(text, "FAKE_REMOVED=<unset>\n"));
    CHECK(Contains(text, "cwd=" + Narrow(cwd) + "\n"));  // the process starts in the project folder
    CHECK(Contains(h.LogText(), "cwd " + Narrow(cwd)));  // and session/new carries it, accents included
}

// The "apply" scenario: the fake agent really edits a file of the session folder (for the browser's sandbox).
void TestApplyEdit() {
    const std::wstring project = g_tmp + L"\\apply projet";
    RemoveTree(project);
    CHECK(MakeDirs(project + L"\\src"));
    const std::wstring file = project + L"\\src\\Hero.css", outside = g_tmp + L"\\outside.txt";
    CHECK(WriteFileText(file, ".hero {\n  color: red;\n  padding: 8px;\n}\n") && WriteFileText(outside, "padding: 8px;\n"));
    Harness h;
    Acp::StartOptions o = h.Options({L"--edit", L"src/Hero.css|padding: 8px|padding: 16px"});
    o.cwd = Narrow(project);
    CHECK(h.session.Start(o));
    Event started, ask, end;
    CHECK(h.Started(started) && started.started.state == Acp::StartState::Ready);

    // Mode "default": the edit waits for the permission; refused, nothing is written.
    h.cursor = h.events.size();
    CHECK(h.session.Prompt({Acp::ContentBlock::Text("Préambule : apply plan tool mode"), Acp::ContentBlock::Text("Agrandis le bouton")}));
    CHECK(h.Wait(Event::Type::PermissionRequest, ask));
    CHECK(ask.permission.toolCall.kind == "edit" && ask.permission.toolCall.title == "Modifier src/Hero.css" && ask.permission.options.size() == 3);
    CHECK(ask.permission.toolCall.locations.size() == 1 && ask.permission.toolCall.locations[0].path == Narrow(file) && ask.permission.toolCall.locations[0].line == 3);
    CHECK(ask.permission.toolCall.diffs.size() == 1 && ask.permission.toolCall.diffs[0].path == Narrow(file) &&
          ask.permission.toolCall.diffs[0].oldText == ".hero {\n  color: red;\n  padding: 8px;\n}\n" &&
          ask.permission.toolCall.diffs[0].newText == ".hero {\n  color: red;\n  padding: 16px;\n}\n");
    CHECK(ReadFileText(file) == ".hero {\n  color: red;\n  padding: 8px;\n}\n");  // not yet
    CHECK(h.session.RespondPermission(ask.permission.requestId, "reject"));
    CHECK(h.Wait(Event::Type::TurnEnded, end) && end.stopReason == "end_turn");
    CHECK(ReadFileText(file) == ".hero {\n  color: red;\n  padding: 8px;\n}\n");

    // Allowed: the file changes on disk, and the tool call says where and what.
    const std::size_t from = h.events.size();
    h.cursor = from;
    CHECK(h.session.Prompt({Acp::ContentBlock::Text("apply")}));
    CHECK(h.Wait(Event::Type::PermissionRequest, ask));
    CHECK(h.session.RespondPermission(ask.permission.requestId, "allow"));
    CHECK(h.Wait(Event::Type::TurnEnded, end) && end.stopReason == "end_turn");
    CHECK(ReadFileText(file) == ".hero {\n  color: red;\n  padding: 16px;\n}\n");
    const std::vector<Event> turn(h.events.begin() + std::ptrdiff_t(from), h.events.end());
    const Event* call = First(turn, Event::Type::ToolCall);
    CHECK(call && call->tool.kind == "edit" && call->tool.status == "pending" && call->tool.locations.size() == 1 && call->tool.diffs.size() == 1 &&
          Contains(call->tool.rawInput, "\"old_string\":\"padding: 8px\""));
    const Event* done = nullptr;
    for (const auto& e : turn)
        if (e.type == Event::Type::ToolCallUpdate && e.tool.status == "completed") done = &e;
    CHECK(done && done->tool.id == call->tool.id && done->tool.title == "Modifié src/Hero.css" && done->tool.diffs.size() == 1 &&
          done->tool.locations.size() == 1 && done->tool.locations[0].line == 3);
    CHECK(Contains(Joined(turn, Event::Type::MessageChunk), "C'est fait") && Contains(h.LogText(), "applied src/Hero.css"));

    // The edit given in the message; in "acceptEdits" nothing is asked.
    CHECK(h.session.SetMode("acceptEdits"));
    Event mode;
    CHECK(h.Wait(Event::Type::ModesChanged, mode));
    const auto direct = h.Turn("apply src/Hero.css|color: red|color: blue", end);
    CHECK(Count(direct, Event::Type::PermissionRequest) == 0 && end.stopReason == "end_turn");
    CHECK(ReadFileText(file) == ".hero {\n  color: blue;\n  padding: 16px;\n}\n");
    // In "plan" nothing is written.
    CHECK(h.session.SetMode("plan"));
    CHECK(h.Wait(Event::Type::ModesChanged, mode));
    const auto planned = h.Turn("apply src/Hero.css|color: blue|color: green", end);
    CHECK(Count(planned, Event::Type::Plan) == 1 && Count(planned, Event::Type::ToolCall) == 0 && ReadFileText(file) == ".hero {\n  color: blue;\n  padding: 16px;\n}\n");
    CHECK(h.session.SetMode("acceptEdits"));
    CHECK(h.Wait(Event::Type::ModesChanged, mode));

    // A path that leaves the session folder is refused, whatever its form; a missing text or file fails cleanly.
    for (const char* hostile : {"apply ../outside.txt|padding: 8px|x", "apply src/../../outside.txt|padding: 8px|x", "apply ..\\outside.txt|padding: 8px|x"}) {
        const auto refused = h.Turn(hostile, end);
        const Event* failed = First(refused, Event::Type::ToolCall);
        CHECK(failed && failed->tool.status == "failed" && Contains(Joined(refused, Event::Type::MessageChunk), "refused") && end.stopReason == "end_turn");
    }
    const auto absolute = h.Turn("apply " + Narrow(outside) + "|padding: 8px|x", end);
    CHECK(Contains(Joined(absolute, Event::Type::MessageChunk), "refused"));
    CHECK(ReadFileText(outside) == "padding: 8px;\n");
    const auto missingText = h.Turn("apply src/Hero.css|no such text|x", end);
    CHECK(Contains(Joined(missingText, Event::Type::MessageChunk), "text not found") && First(missingText, Event::Type::ToolCall)->tool.status == "failed");
    const auto missingFile = h.Turn("apply src/Absent.css|a|b", end);
    CHECK(Contains(Joined(missingFile, Event::Type::MessageChunk), "cannot read"));
    CHECK(ReadFileText(file) == ".hero {\n  color: blue;\n  padding: 16px;\n}\n");
    // Other scenarios stay reachable by their word, and "reply" still answers without touching anything.
    const auto reply = h.Turn("reply", end);
    CHECK(Joined(reply, Event::Type::MessageChunk) == "Bonjour — voici la réponse." && Count(reply, Event::Type::ToolCall) == 0);
    h.session.Dispose();
    CHECK(h.session.WaitStopped(10000));

    // The same edit configured by the environment variable; "always allow" stops the questions.
    h.events.clear();
    h.cursor = 0;
    Acp::StartOptions viaEnv;
    viaEnv.cwd = Narrow(project);
    viaEnv.resolve = [](const Acp::ResolveContext&) {
        Acp::Resolution r;
        r.launch.exe = g_agent;
        r.launch.freshUserEnvironment = false;
        r.launch.env = {{L"FAKE_AGENT_EDIT", L"src/Hero.css|16px|24px"}};
        return r;
    };
    CHECK(h.session.Start(viaEnv));
    CHECK(h.Started(started) && started.started.state == Acp::StartState::Ready);
    h.cursor = h.events.size();
    CHECK(h.session.Prompt({Acp::ContentBlock::Text("Plus grand encore")}));
    CHECK(h.Wait(Event::Type::PermissionRequest, ask));
    CHECK(h.session.RespondPermission(ask.permission.requestId, "always"));
    CHECK(h.Wait(Event::Type::TurnEnded, end));
    CHECK(ReadFileText(file) == ".hero {\n  color: blue;\n  padding: 24px;\n}\n");
    const auto again = h.Turn("apply src/Hero.css|24px|32px", end);
    CHECK(Count(again, Event::Type::PermissionRequest) == 0 && ReadFileText(file) == ".hero {\n  color: blue;\n  padding: 32px;\n}\n");
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) {
        std::printf("usage: acp_client_test <fake-acp-agent.exe> <tmp dir>\n");
        return 2;
    }
    SetConsoleOutputCP(CP_UTF8);
    g_agent = FullPath(argv[1]);
    g_tmp = FullPath(argv[2]);
    RemoveTree(g_tmp);
    if (!MakeDirs(g_tmp) || !Exists(g_agent)) {
        std::printf("cannot prepare %s\n", Narrow(g_tmp).c_str());
        return 2;
    }
    struct {
        const char* name;
        void (*run)();
    } tests[] = {
        {"classification", TestClassification},
        {"options", TestOptions},
        {"environment + quoting", TestEnvironmentAndQuoting},
        {"start + reply", TestStartAndReply},
        {"tools, plan, permission, questions", TestToolsPlanPermission},
        {"modes + options", TestModesAndOptions},
        {"auth", TestAuth},
        {"failures", TestFailures},
        {"cancel + dispose", TestCancelAndDispose},
        {"job object", TestJobObject},
        {"big prompt", TestBigPromptDoesNotBlock},
        {"resume", TestResume},
        {"environment + cwd", TestEnvironmentAndCwd},
        {"apply: a real edit in the project", TestApplyEdit},
    };
    for (const auto& t : tests) {
        const int before = Failures();
        const DWORD t0 = GetTickCount();
        t.run();
        std::printf("%s %s (%lu ms)\n", Failures() == before ? "ok  " : "FAIL", t.name, GetTickCount() - t0);
        std::fflush(stdout);
    }
    std::printf("acp_client_test: %d checks, %d failures\n", Checks(), Failures());
    return Failures() ? 1 : 0;
}
