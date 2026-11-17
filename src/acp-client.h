#pragma once
// ACP v1 client (Agent Client Protocol, schema v1.32): one agent process, one session. No UI, no window.
// Contract "A" of design/chat-acp.md (C1 -> C4). Runtime side (which exe to launch): src/agent-runtime.h.
//
// Transport: JSON-RPC 2.0, one JSON message per line on the agent's stdin/stdout (src/strict-json.h, 32 MiB per
// line). The agent runs in a Job Object (kill-on-close): stopping the session kills everything the agent started.
//
// Threading: every method is non-blocking and may be called from the UI thread. Reading, writing and process
// creation happen on the session's own threads. Results come back as Events in a thread-safe queue: the wake
// callback (SetWake) is called from a background thread when the queue goes from empty to non-empty (post yourself
// a window message there), then Poll() on your thread drains everything at once.
//
// What the client declares to the agent (initialize): protocol 1, fs.readTextFile/writeTextFile false, terminal
// false (the agent reads, writes and runs commands itself), auth.terminal true (the agent may offer terminal
// sign-in methods, which the client runs itself: AgentRuntime::RunTerminalSignIn), elicitation.form when
// StartOptions::questions.
//
// No interface text here: errors are codes (ErrorCodeName) plus the agent's own words (Error::detail, untrusted,
// bounded). The integration maps codes to catalog text (table in design/chat-acp.md).
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Acp {

constexpr int ProtocolVersion = 1;
constexpr std::size_t MaxLineBytes = 32u << 20;    // one inbound JSON line
constexpr std::size_t MaxTextBytes = 256u << 10;   // one text field kept in an event (chunks, tool text, details)
constexpr std::size_t MaxDiffBytes = 2u << 20;     // one side of a diff kept in an event
constexpr std::size_t MaxDetailBytes = 4000;       // Error::detail, stderr tail

enum class ErrorCode {
    None,
    // Before the process exists (from the Resolver, see AgentRuntime::MakeResolver).
    NotInstalled,   // the user's CLI (claude / codex) was not found
    CliTooOld,      // found, but older than the adapter supports
    NeedsPrepare,   // the private Node or the adapter is not downloaded yet: AgentRuntime::Prepare
    // Process and protocol.
    SpawnFailed,    // CreateProcess failed (detail: Windows error)
    ProcessExited,  // the agent died (detail: last lines of its stderr)
    Timeout,        // no answer in time
    Protocol,       // unsupported protocol version or an answer we cannot use
    AuthRequired,   // JSON-RPC -32000, or sign-in wording in the agent's error
    AgentError,     // any other JSON-RPC error from the agent (rpcCode, detail)
    NotReady,       // call made in a state that cannot take it
    // A failed turn whose cause is recognised in the agent's own text (checked in this order).
    ContextTooLong, UsageLimit, Overloaded, RateLimited, ServerError, Network,
};
const char* ErrorCodeName(ErrorCode code);  // stable identifier ("auth_required"), for logs and catalog keys

struct Error {
    ErrorCode code = ErrorCode::None;
    int rpcCode = 0;      // JSON-RPC error code when the agent sent one
    std::string detail;   // the agent's own words or an OS detail: untrusted, UTF-8, at most MaxDetailBytes
    bool Ok() const { return code == ErrorCode::None; }
};

// ---------------------------------------------------------------- launch
struct LaunchSpec {
    std::wstring exe;                // absolute path
    std::vector<std::wstring> args;
    std::wstring cwd;                // process directory; empty = StartOptions::cwd
    // Variables set over the base environment (replacing any existing one, names compared without case).
    std::vector<std::pair<std::wstring, std::wstring>> env;
    std::vector<std::wstring> unsetEnv;
    // true: base = the user's environment rebuilt from the registry (CreateEnvironmentBlock), so a PATH changed
    // after the browser started is seen. false: base = this process's environment (tests).
    bool freshUserEnvironment = true;
};
struct ResolveContext {
    bool signIn = false;  // the session is started to sign in: the agent may open the user's web browser
};
struct Resolution {
    Error error;        // NotInstalled / CliTooOld / NeedsPrepare, or None
    LaunchSpec launch;  // valid when error.Ok()
};
// Runs on the session's thread before the process is created; may block (CLI detection).
using Resolver = std::function<Resolution(const ResolveContext&)>;

// ---------------------------------------------------------------- session description
struct McpServer {  // stdio MCP server handed to the agent in session/new (UTF-8)
    std::string name, command;
    std::vector<std::string> args;
    std::vector<std::pair<std::string, std::string>> env;
};
struct AuthMethod {
    std::string id, name, description;
    // true: ACP forbids passing this method to `authenticate`; the client runs the agent's CLI itself with
    // terminalArgs (AgentRuntime::RunTerminalSignIn). false: Session::Authenticate(id).
    bool terminal = false;
    std::vector<std::string> terminalArgs;
    std::vector<std::pair<std::string, std::string>> terminalEnv;
};
struct Mode {
    std::string id, name, description;
};
struct ModeState {
    std::string currentId;
    std::vector<Mode> available;
};
struct ConfigChoice {
    std::string value, name, description;
    std::string group;  // name of the group the choice came in ("" when the list is flat)
};
struct ConfigOption {
    std::string id, name, description;
    std::string category;      // "model", "thought_level" (effort), "mode", "model_config", other, or ""
    std::string type;          // "select" or "boolean"
    std::string currentValue;  // select: a choice value; boolean: "true" / "false"
    std::vector<ConfigChoice> choices;
};
// The option the agent uses for each role, or nullptr. Nothing is hard-coded per agent: the role comes from the
// option's category (model: the option whose id is "model" wins when several share the category).
const ConfigOption* FindModelOption(const std::vector<ConfigOption>& options);
const ConfigOption* FindEffortOption(const std::vector<ConfigOption>& options);
const ConfigOption* FindModeOption(const std::vector<ConfigOption>& options);

struct Capabilities {
    bool image = false, audio = false, embeddedContext = false;  // promptCapabilities
    bool loadSession = false, resume = false;                    // session/load, session/resume
    bool httpMcp = false, sseMcp = false;                        // mcpCapabilities
};
struct Command {
    std::string name, description, inputHint;
};

enum class StartState { Ready, NotInstalled, NeedsPrepare, AuthRequired, Failed };
struct StartResult {
    StartState state = StartState::Failed;
    Error error;                          // set unless Ready
    std::string sessionId;
    bool resumed = false;                 // an earlier session was picked up (session/resume or session/load)
    int protocolVersion = 0;
    std::string agentName, agentTitle, agentVersion;  // initialize.agentInfo
    Capabilities capabilities;
    std::vector<AuthMethod> authMethods;  // as advertised at initialize (also filled when AuthRequired)
    ModeState modes;
    std::vector<ConfigOption> configOptions;
    std::vector<Command> commands;        // slash commands known when the session opened (may still be empty)
};

// ---------------------------------------------------------------- prompt
struct ContentBlock {
    enum class Kind { Text, Image, ResourceLink };
    Kind kind = Kind::Text;
    std::string text;             // Text
    std::string mimeType, data;   // Image: e.g. "image/png" + base64 (sent only when Capabilities::image)
    std::string uri, name, title; // ResourceLink (mimeType optional)
    static ContentBlock Text(std::string t) { ContentBlock b; b.text = std::move(t); return b; }
    static ContentBlock Image(std::string mime, std::string base64) {
        ContentBlock b; b.kind = Kind::Image; b.mimeType = std::move(mime); b.data = std::move(base64); return b;
    }
    static ContentBlock ResourceLink(std::string uri, std::string name, std::string title = {}, std::string mime = {}) {
        ContentBlock b; b.kind = Kind::ResourceLink; b.uri = std::move(uri); b.name = std::move(name);
        b.title = std::move(title); b.mimeType = std::move(mime); return b;
    }
};

// ---------------------------------------------------------------- events
struct ToolLocation {
    std::string path;
    std::int64_t line = -1;  // -1 when the agent gave none
};
struct Diff {
    std::string path;
    bool hasOldText = false;  // false: the file is new
    std::string oldText, newText;
    bool truncated = false;   // a side was cut at MaxDiffBytes
};
// A tool call or an update to one. In an update only the has* fields changed; the rest keeps its last value.
struct ToolCall {
    std::string id;
    bool hasTitle = false, hasKind = false, hasStatus = false, hasContent = false, hasLocations = false;
    std::string title;
    std::string kind;    // read | edit | delete | move | search | execute | think | fetch | switch_mode | other
    std::string status;  // pending | in_progress | completed | failed
    std::string text;    // the text content blocks, joined by blank lines (at most MaxTextBytes)
    std::vector<Diff> diffs;
    std::vector<ToolLocation> locations;
    std::string rawInput;  // the tool's raw input as compact JSON ("" when absent or larger than 64 KiB)
};
struct PlanEntry {
    std::string content, priority, status;  // high|medium|low ; pending|in_progress|completed
};
struct PermissionOption {
    std::string id, name;
    std::string kind;         // allow_once | allow_always | reject_once | reject_always ; "answer" for a question
    std::string description;  // questions only: what the choice means
};
struct PermissionRequest {
    std::string requestId;  // pass back to Session::RespondPermission
    ToolCall toolCall;      // what is asked for (title, kind, locations, diffs)
    std::vector<PermissionOption> options;
    // true: not a tool permission but a question of the agent (elicitation form, one pick from a list). The
    // question is toolCall.title, `message` what the form says beside it; the first option is always "skip".
    bool question = false;
    std::string message;
};
struct Usage {
    std::int64_t used = 0, size = 0;  // context tokens used / context window
    bool hasCost = false;
    double costAmount = 0;
    std::string costCurrency;
};

struct Event {
    enum class Type {
        Started,               // started
        MessageChunk,          // text, messageId
        ThoughtChunk,          // text, messageId
        ToolCall,              // tool (complete)
        ToolCallUpdate,        // tool (delta)
        Plan,                  // plan (replaces the previous one)
        PermissionRequest,     // permission
        PermissionDismissed,   // permission.requestId: the request no longer waits (turn cancelled, agent gone)
        ModesChanged,          // modes
        ConfigOptionsChanged,  // configOptions (the whole list)
        CommandsChanged,       // commands
        Usage,                 // usage
        SessionTitle,          // text
        Notice,                // text, severity
        TurnEnded,             // stopReason, or error
        RequestFailed,         // request ("set_mode" | "set_config_option"), error
        Warning,               // text: a line that was not valid JSON-RPC was ignored (developer detail)
        Exited,                // the process ended while the session was live: exitCode, error (ProcessExited)
    };
    Type type = Type::Warning;
    StartResult started;
    std::string text, messageId, severity;
    ToolCall tool;
    std::vector<PlanEntry> plan;
    PermissionRequest permission;
    ModeState modes;
    std::vector<ConfigOption> configOptions;
    std::vector<Command> commands;
    Usage usage;
    std::string stopReason;  // end_turn | max_tokens | max_turn_requests | refusal | cancelled ; "" with an error
    Error error;
    std::string request;
    int exitCode = 0;
};

struct Timeouts {
    unsigned initializeMs = 45000;
    unsigned sessionMs = 60000;        // session/new, session/resume, session/load
    unsigned authenticateMs = 300000;  // waits for the user in their web browser
    unsigned settingMs = 30000;        // session/set_mode, session/set_config_option
    unsigned exitGraceMs = 2000;       // after stdin is closed, before the whole job is killed
};

struct StartOptions {
    Resolver resolve;                    // required
    std::string cwd;                     // absolute project folder, UTF-8 (ACP requires an absolute cwd)
    std::vector<McpServer> mcpServers;
    std::string resumeSessionId;         // try session/resume, then session/load, then a new session
    std::string authenticateMethodId;    // send `authenticate` first (set by Session::Authenticate)
    std::string clientName = "browser", clientVersion = "0";
    bool questions = true;               // surface the agent's single-choice questions as PermissionRequest events
    Timeouts timeouts;
};

enum class Phase { Idle, Starting, Ready, Prompting, Stopped };

struct SessionImpl;

class Session {
public:
    Session();
    ~Session();  // Dispose()
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // Called from a background thread when events become available. Set before Start; keep it cheap.
    void SetWake(std::function<void()> wake);

    // Launches the agent: resolve -> process -> initialize -> [authenticate] -> session/resume | session/new.
    // Always answered by exactly one Started event. false: a session is already starting or live.
    bool Start(StartOptions options);
    // Signs in with a non-terminal method, then opens the session: restarts the agent with the user's web browser
    // allowed and sends `authenticate` (up to Timeouts::authenticateMs). Answered by a Started event.
    // false: no earlier Start, or the method is a terminal one (AgentRuntime::RunTerminalSignIn).
    bool Authenticate(const std::string& methodId);

    // One turn at a time. Image blocks are dropped when the agent does not take images. Answered by TurnEnded.
    // false: not Ready (no session, or a turn is already running).
    bool Prompt(std::vector<ContentBlock> blocks);
    // Stops the running turn (session/cancel). Pending permission requests are answered "cancelled" and reported by
    // PermissionDismissed; the turn then ends with TurnEnded{stopReason "cancelled"}. No-op without a turn.
    void Cancel();
    bool SetMode(const std::string& modeId);                                        // -> ModesChanged | RequestFailed
    bool SetConfigOption(const std::string& configId, const std::string& value);    // -> ConfigOptionsChanged | RequestFailed
    // optionId empty = cancelled. false: unknown or already answered request.
    bool RespondPermission(const std::string& requestId, const std::string& optionId);

    // Stops the agent and everything it started: closes its stdin, waits Timeouts::exitGraceMs, then kills the job.
    // Non-blocking; no event is delivered afterwards. The object can be started again.
    void Dispose();
    // true once the last started process is gone and its threads have ended (tests, browser shutdown).
    bool WaitStopped(unsigned timeoutMs);

    std::size_t Poll(std::vector<Event>& out);  // appends the queued events; returns how many
    bool WaitForEvents(unsigned timeoutMs);     // for callers without a message loop (tests, probes)

    Phase phase() const;
    unsigned long processId() const;  // 0 when no process (tests: orphan checks)

private:
    std::shared_ptr<SessionImpl> impl_;
};

// ---------------------------------------------------------------- pure helpers (exposed for tests)
// Classifies an agent error: AuthRequired (code -32000 or sign-in wording), a known turn failure, else AgentError.
Error ClassifyAgentError(int rpcCode, const std::string& message, const std::string& dataDetail);
bool LooksLikeAuthError(std::string_view text);
ErrorCode PlainFailure(std::string_view text);  // None when the text is not a failure we know
// Environment block for CreateProcessW (UTF-16, sorted, double-NUL terminated).
std::wstring BuildEnvironmentBlock(const LaunchSpec& spec);
// Windows command-line quoting of one argument (CommandLineToArgvW rules).
std::wstring QuoteArgument(const std::wstring& arg);

}  // namespace Acp
