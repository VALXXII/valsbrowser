#pragma once
// Agent runtime for the integrated chat: what it takes to launch Claude Code or Codex as an ACP agent on Windows.
// Contract "A" of design/chat-acp.md (C1 -> C4). The protocol side is src/acp-client.h.
//
//  - Finds the user's own CLI (claude.exe / codex.exe): PATH, the installers' usual folders, the native binary
//    behind an npm shim. A .cmd / .ps1 shim is never executed.
//  - Tells whether the CLI is signed in WITHOUT reading any secret: it runs the CLI's own documented status command
//    (`claude auth status`, `codex login status`) with all output discarded and reads the exit code only.
//  - Downloads on demand, into <data>\agents\ only: the official Node.js for Windows x64 (pinned LTS, pinned
//    SHA-256, node.exe extracted from the zip then moved into place atomically) and the adapters' npm packages
//    (every tarball pinned by its sha512 integrity, extracted by this code, moved into place atomically). npm is
//    never run and no package script can run: the dependency tree is resolved at build time
//    (tools/agents/pin-adapters.py -> src/agent-runtime-pins.inc), one set of pins per browser version.
//  - Builds the launch of the adapter on that Node: CLAUDE_CODE_EXECUTABLE / CODEX_PATH pointing at the user's CLI,
//    BROWSER=none except while signing in.
//  - Signing in: ACP "terminal" methods run the CLI's own login in a visible console.
//
// Nothing here writes outside <data>\agents\ (plus Windows' temporary files of WinHTTP). The user's Claude / Codex
// configuration and credentials are never read nor written by this code.
// Threading: Detect, Prepare and ResolveLaunch block (process, disk, network): call them off the UI thread.
// No interface text: states and codes only (design/chat-acp.md has the wording table).
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "acp-client.h"

namespace AgentRuntime {

enum class Agent { Claude, Codex };
const char* AgentId(Agent agent);                      // "claude" | "codex" (stable: settings, messages to the panel)
bool ParseAgentId(std::string_view id, Agent& agent);
const wchar_t* AgentDisplayName(Agent agent);          // product names: "Claude Code", "Codex"

// ---------------------------------------------------------------- pins (one set per browser version)
struct PackagePin {
    const char* path;       // where it goes under the adapter folder: "node_modules/zod"
    const char* name;       // "zod", "@scope/name"
    const char* version;
    const char* integrity;  // "sha512-<base64>" of the registry tarball
};
struct AdapterPin {
    const char* package;        // "@agentclientprotocol/claude-agent-acp"
    const char* version;
    const char* entry;          // script to run, relative to the adapter folder
    const char* cliLine;        // the CLI version the adapter was built against ("2.1.325")
    const char* minCliVersion;  // oldest CLI accepted
    const PackagePin* packages;
    std::size_t packageCount;
    const char* treeDigest;     // short digest of the whole table: part of the folder name
};
struct NodePin {
    const char* version;        // "24.22.3"
    const char* zipName;        // "node-v24.22.3-win-x64.zip"
    const char* zipSha256;      // lowercase hex, from nodejs.org SHASUMS256.txt
    std::uint64_t zipBytes;
    const char* exeSha256;      // node.exe inside the zip (SHASUMS256.txt "win-x64/node.exe")
    std::uint64_t exeBytes;
};
struct Pins {
    NodePin node;
    AdapterPin claude, codex;
    const AdapterPin& Adapter(Agent a) const { return a == Agent::Claude ? claude : codex; }
};
const Pins& BuiltInPins();

// ---------------------------------------------------------------- environment (every lookup of the outside world)
struct Environment {
    std::wstring dataRoot;  // the browser's data folder; everything lives in <dataRoot>\agents
    // Where CLIs are looked for. FromProcess() reads the user's current environment from the registry, so a CLI
    // installed while the browser runs is found.
    std::wstring path, userProfile, appData, localAppData;
    std::wstring nodeDist = L"https://nodejs.org/dist";         // official downloads
    std::wstring registry = L"https://registry.npmjs.org";      // official npm registry
    bool freshUserEnvironment = true;  // the agent gets the user's environment rebuilt from the registry (tests: false)
    // Test hook, filled by FromProcess only when BROWSER_ANNOTATIONS_TEST is set and the build is not a release:
    // BROWSER_CHAT_FAKE_AGENT=<fake-acp-agent.exe> [args...] replaces both agents (no CLI, no download).
    std::wstring fakeAgentExe;
    std::vector<std::wstring> fakeAgentArgs;
    const Pins* pins = nullptr;  // nullptr = BuiltInPins() (tests pass altered pins)

    static Environment FromProcess(const std::wstring& dataRoot);
    const Pins& ThePins() const { return pins ? *pins : BuiltInPins(); }
};

// Paths (no I/O).
std::wstring AgentsRoot(const Environment& env);                    // <dataRoot>\agents
std::wstring NodeDir(const Environment& env);                       // <agents>\node\<version>
std::wstring NodeExe(const Environment& env);                       // <NodeDir>\node.exe
std::wstring AdapterDir(const Environment& env, Agent agent);       // <agents>\adapters\<id>\<version>-<digest>
std::wstring AdapterEntry(const Environment& env, Agent agent);     // the script given to node.exe

// ---------------------------------------------------------------- detection
enum class CliState {
    Missing,   // not found: offer InstallHelp()
    Found,     // usable
    TooOld,    // found, but older than the adapter supports: offer InstallHelp().update
    Untested,  // usable, but from a newer release line than the adapter was built against (allowed, may misbehave)
};
enum class Login { Unknown, SignedIn, SignedOut };
struct CliInfo {
    CliState state = CliState::Missing;
    std::wstring path;      // the executable used
    std::string version;    // "2.1.325"; "" when it could not be read
    std::string source;     // "path" | "installer" | "npm" | "winget" | "test"
};
struct Detection {
    Agent agent = Agent::Claude;
    CliInfo cli;
    Login login = Login::Unknown;  // a hint: the session start is what tells for sure (StartState::AuthRequired)
    bool nodeReady = false;        // the private Node is in place
    bool adapterReady = false;     // the adapter and its dependencies are in place
    bool Usable() const { return cli.state == CliState::Found || cli.state == CliState::Untested; }
    bool NeedsPrepare() const { return !nodeReady || !adapterReady; }
};
struct DetectOptions {
    bool checkLogin = true;     // runs the CLI's status command (about 0.3 s)
    unsigned timeoutMs = 10000;  // per CLI command
};
// Blocking (runs `<cli> --version`, and the status command when asked). Never starts the agent, never downloads.
Detection Detect(const Environment& env, Agent agent, const DetectOptions& options = DetectOptions());

// What to show when the CLI is missing or too old: official commands for the user to copy, and the official page.
struct InstallHelp {
    const wchar_t* install;      // PowerShell command line
    const wchar_t* installAlt;   // another official way, or L""
    const wchar_t* update;       // command that updates an existing install
    const wchar_t* url;          // official setup page
};
InstallHelp GetInstallHelp(Agent agent);

// ---------------------------------------------------------------- preparation (downloads)
struct Progress {
    enum class Stage { Checking, Node, Adapter, Done };
    Stage stage = Stage::Checking;
    std::uint64_t received = 0, total = 0;  // bytes of the current stage (total 0 = unknown)
    std::size_t item = 0, itemCount = 0;    // Adapter: packages done / to do
};
enum class PrepareError {
    None,
    Cancelled,
    Network,     // could not reach or read the official server
    Integrity,   // a download did not match its pinned fingerprint: nothing was installed
    Disk,        // could not write under <data>\agents
    Archive,     // an archive we could not read, or with an entry that leaves its folder
};
const char* PrepareErrorName(PrepareError error);  // stable identifier
struct PrepareResult {
    PrepareError error = PrepareError::None;
    std::string detail;  // developer detail (which file, which Windows error)
    bool Ok() const { return error == PrepareError::None; }
};
// Makes the private Node and the agent's adapter ready (no-op when they already are). Blocking; `progress` is
// called on the calling thread, at most about ten times a second. `cancel` may be set from any thread.
PrepareResult Prepare(const Environment& env, Agent agent, const std::function<void(const Progress&)>& progress = nullptr,
                      const std::atomic<bool>* cancel = nullptr);
// Removes what earlier browser versions left in <data>\agents (other Node versions, other adapter folders, interrupted
// downloads). Safe at any time no agent is running.
void RemoveStale(const Environment& env);

// ---------------------------------------------------------------- launch
// The Resolver for Acp::StartOptions: looks for the CLI (no login check), checks that Node and the adapter are in
// place, and builds the command. Errors: NotInstalled, CliTooOld (detail: the version found), NeedsPrepare.
Acp::Resolution ResolveLaunch(const Environment& env, Agent agent, const Acp::ResolveContext& context);
Acp::Resolver MakeResolver(Environment env, Agent agent);

// ---------------------------------------------------------------- the agent's own permission modes
// Which of the agent's modes stand for the product's three choices. The session's announced list still decides
// what is offered: an id the agent does not announce is skipped, in order. The session starts in the mode the
// user's own agent configuration picks (seen: Claude "auto", Codex "agent"), so the integration sets the mode it
// wants right after Started. Checked against the real adapters (design/chat-acp.md section 9).
struct ModeMap {
    std::vector<std::string> ask;        // "Demander": every change waits for a permission
    std::vector<std::string> automatic;  // "Auto", best first
    std::string planMode;                // "Planifier" as a session mode (Session::SetMode), or ""
    std::string planOption, planOn, planOff;  // or as a config option (Session::SetConfigOption), or ""
    std::vector<std::string> never;      // modes that ask for nothing at all: not offered
};
ModeMap GetModeMap(Agent agent);

// ---------------------------------------------------------------- terminal sign-in
// Runs the CLI's own sign-in for an ACP "terminal" method (AuthMethod::terminal) in a visible console window: the
// CLI opens the user's web browser and exits once signed in. The adapter's leading "--cli" argument is dropped.
class SignIn {
public:
    SignIn() = default;
    ~SignIn();
    SignIn(SignIn&& other) noexcept;
    SignIn& operator=(SignIn&& other) noexcept;
    bool Started() const { return process_ != nullptr; }
    // true once the console has closed; exitCode 0 = the CLI reports success. Then start the session again.
    bool Wait(unsigned timeoutMs, int* exitCode = nullptr);
    void Close();  // closes the console if it is still open (the browser is quitting)

private:
    friend SignIn RunTerminalSignIn(const Environment&, Agent, const Acp::AuthMethod&, std::string*);
    void* process_ = nullptr;
    void* job_ = nullptr;
};
SignIn RunTerminalSignIn(const Environment& env, Agent agent, const Acp::AuthMethod& method, std::string* error = nullptr);
// The CLI's own sign-in as a terminal method, for when the agent cannot be asked (the adapter is not downloaded yet,
// or Detection::login says SignedOut before any session): `claude auth login --claudeai`, `codex login`.
Acp::AuthMethod DefaultTerminalSignIn(Agent agent);
// The sign-in method to offer first, or nullptr: a subscription login before an API-key one.
const Acp::AuthMethod* PreferredSignIn(const std::vector<Acp::AuthMethod>& methods);

// ---------------------------------------------------------------- internals exposed for tests
namespace detail {
bool ParseVersion(std::string_view text, int out[3]);  // first "a.b.c" in the text
CliState Compatible(Agent agent, std::string_view found, const AdapterPin& pin);
// Candidate executables for an agent, best first (existing files only).
std::vector<std::pair<std::wstring, std::string>> CliCandidates(const Environment& env, Agent agent);
std::string Sha256Hex(const void* data, std::size_t size);
std::string Sha512Base64(const void* data, std::size_t size);
bool HashFile(const std::wstring& path, bool sha512, std::string& out);  // sha256 hex or sha512 base64
// Raw DEFLATE (RFC 1951). false on corrupt input or when the output would exceed maxOut.
bool Inflate(const unsigned char* in, std::size_t inSize, std::string& out, std::size_t maxOut);
std::uint32_t Crc32(const void* data, std::size_t size, std::uint32_t crc = 0);
// Extracts the named files of a zip ("node-v24.22.3-win-x64/node.exe" -> destDir\node.exe). Stored and deflated
// entries, no zip64. Every wanted name must be present.
bool ExtractZipFiles(const std::wstring& zip, const std::vector<std::pair<std::string, std::wstring>>& wanted, std::string& error);
// Extracts an npm tarball (.tgz) into destDir, dropping the first path component ("package/"). Refuses any entry
// that is not a plain file or directory (links, devices) or whose path is absolute, has "..", ':' or a reserved name.
bool ExtractNpmTarball(const std::string& tgz, const std::wstring& destDir, std::string& error);
// HTTPS download to a file (system proxy settings), hashing while writing. https only, no redirect to http.
PrepareResult Download(const std::wstring& url, const std::wstring& file, std::uint64_t maxBytes, bool sha512, std::string& digest,
                       const std::function<void(std::uint64_t received, std::uint64_t total)>& progress, const std::atomic<bool>* cancel);
}  // namespace detail

}  // namespace AgentRuntime
