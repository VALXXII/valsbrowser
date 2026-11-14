#pragma once
// Agent connection (annotations V2-A): "Connecter avec Claude" / "Connecter avec Codex" in one click, without UI.
// Contract: plan-annotations-mcp-local/design/annotations-v2-brief.md §2; notes in design/annotations-store-bridge.md.
//
// Rules (all enforced here, tested on redirected folders only):
//  - Called only on an explicit user click. Never touches anything else than OUR entry ("browser-annotations").
//  - Detection by configuration folders, never by application folders: Claude Desktop = %APPDATA%\Claude
//    (or the MSIX copy %LOCALAPPDATA%\Packages\Claude_*\LocalCache\Roaming\Claude when it exists); Codex =
//    %CODEX_HOME% or %USERPROFILE%\.codex.
//  - Every write: timestamped backup of the previous file (<file>.browser-annotations-YYYYMMDD-HHMMSS.bak), then
//    temporary file + MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH). Existing content is spliced, never
//    re-serialized: every other key, comment, number, order, BOM and line ending stays byte-for-byte.
//  - An existing file we cannot understand (invalid JSON, mcpServers not an object, our TOML entry written in a
//    form we do not edit) => refused, nothing written, Result::InvalidConfig.
//  - Claude: a .mcpb bundle (zip, manifest.json per MCPB MANIFEST.md 0.3, server.type "binary", the bridge exe,
//    args ["--store", <real store path>]) opened through the Windows file association (Claude's install dialog).
//    No association => fallback: merge mcpServers.browser-annotations into claude_desktop_config.json.
//  - Codex: `codex mcp add <name> -- <exe> --store <path>` when codex.exe is on PATH (syntax checked against
//    `codex mcp add --help`, codex-cli 0.166.0), run with CODEX_HOME set explicitly; its result is verified in
//    config.toml, else (or without the CLI) the [mcp_servers.<name>] table is edited directly.
//
// Threading: synchronous file I/O (and, for Codex, a child process up to CliTimeoutMs): call off the UI thread.
// Every OS lookup goes through Environment, so tests redirect all of them to temporary folders.
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "annotations.h"

namespace AgentConnect {

constexpr const char* DefaultServerName = "browser-annotations";
constexpr const char* McpbManifestVersion = "0.3";
constexpr unsigned CliTimeoutMs = 20000;
constexpr std::int64_t RecentReadSeconds = 7 * 86400;  // a readers\<client>.json newer than this = "connected"
constexpr std::uint64_t MaxConfigBytes = 8u << 20;      // bigger config files are refused, never truncated

enum class Client { Claude, Codex };

// Every lookup of the outside world. FromProcess() reads the real environment; tests fill it with temp folders.
struct Environment {
    std::wstring appData, localAppData, userProfile, codexHome, path;  // the environment variables ("" = unset)
    std::function<unsigned(const std::wstring& rootPath)> driveType;    // GetDriveTypeW
    std::function<std::int64_t()> now;                                   // UTC Unix seconds
    std::function<bool()> mcpbAssociated;                                // .mcpb has an "open" handler
    std::function<bool(const std::wstring& file)> openFile;              // ShellExecuteExW "open"
    // Test guard (incident of 14 Nov 2026: a test copy of the preview, left open without redirection, wrote into the
    // user's real configurations). testMode: set by FromProcess when BROWSER_ANNOTATIONS_TEST is set or a WebView2
    // remote-debugging port is requested (automation). In test mode every write, backup, .mcpb opening and codex CLI
    // run whose target lies under one of protectedRoots() is refused (Result::Failed, nothing touched).
    // protectedRoots: the REAL folders, resolved from the Windows known folders (never from the environment
    // variables a test redirects): Roaming\Claude, Local\Packages (Claude_* MSIX copies), <profile>\.codex.
    bool testMode = false;
    std::function<std::vector<std::wstring>()> protectedRoots;
    static Environment FromProcess();
};
// Pure guard used by Connect*/Disconnect* (exposed for tests): false + why when env.testMode and `path` is one of the
// protected roots or inside one.
bool WriteAllowed(const Environment& env, const std::wstring& path, std::string* why = nullptr);

// What the bridge is and where the store is.
struct Target {
    std::wstring bridgeExe;  // absolute path of browser-annotations-mcp.exe (see StableBridgePath)
    std::wstring storeRoot;  // absolute real store root, passed as --store
    std::string serverName = DefaultServerName;
    std::string version = "0.0.0";  // semver, manifest "version"
};

// ---------------------------------------------------------------- locations (no writes)
std::wstring ClaudeConfigDir(const Environment& env);   // "" when no Claude Desktop configuration folder exists
std::wstring ClaudeConfigPath(const Environment& env);  // <ClaudeConfigDir>\claude_desktop_config.json ("" if none)
std::wstring CodexHome(const Environment& env);         // CODEX_HOME, else %USERPROFILE%\.codex ("" if neither set)
std::wstring CodexConfigPath(const Environment& env);   // <CodexHome>\config.toml
// codex.exe on PATH (absolute PATH entries only). A codex.cmd/.bat/.ps1 shim is NOT used: running it needs
// cmd.exe/PowerShell, whose parsing of % ^ & ! in a path cannot be made safe => direct TOML edit instead.
std::wstring FindCodexCli(const Environment& env);

struct Detection {
    bool claude = false, codex = false;  // configuration folder present
    std::wstring claudeConfig, codexConfig, codexCli;
    bool mcpbAssociated = false;
};
Detection Detect(const Environment& env);

// Where the bridge exe should be referenced from: for an installed browser
// (<installRoot>\versions\<M.m.B>\browser.exe) the stable copy <installRoot>\bin\browser-annotations-mcp.exe when it exists (it survives
// updates), otherwise the bridge next to browser.exe (portable / preview / worktree). installRoot defaults to
// %LOCALAPPDATA%\browser (same rule as BrowserAnnotations::DefaultStoreRoot).
std::wstring StableBridgePath(const std::wstring& browserExe, const std::wstring& installRoot = L"");
// True when `path` is on a removable drive (GetDriveType = DRIVE_REMOVABLE): the UI warns and offers
// "Reconnecter" (a config pointing to a drive letter that changes or disappears).
bool IsOnRemovableDrive(const Environment& env, const std::wstring& path);

// ---------------------------------------------------------------- manual setup ("Réglage manuel", Copier)
std::string ClaudeJsonSnippet(const Target& t);      // complete {"mcpServers": {...}} document, pretty
std::string CodexTomlSnippet(const Target& t);       // [mcp_servers.<name>] table
std::wstring CodexAddCommandLine(const Target& t);   // codex mcp add <name> -- "<exe>" --store "<store>"

// ---------------------------------------------------------------- results
enum class Result {
    Connected,         // entry written (restart the client to load it)
    AlreadyConnected,  // identical entry already present: nothing written
    PendingInstall,    // .mcpb opened: the user clicks "Installer" in Claude
    Disconnected,      // our entry removed
    NotConnected,      // nothing to remove
    ManualStep,        // Claude extension installed from the .mcpb: uninstall it in Claude › Réglages › Extensions
    NoClient,          // no configuration folder for this client
    InvalidConfig,     // existing file not understood: refused, nothing written
    Failed,            // I/O error (nothing half-written: the original file is intact)
};
const char* ResultName(Result r);

struct Report {
    Result result = Result::Failed;
    std::string message;   // French, for the UI
    std::wstring file;     // configuration file (or .mcpb) concerned
    std::wstring backup;   // backup written before the change ("" if none)
    bool usedCli = false;  // Codex: `codex mcp add/remove` was run
    bool ok() const {
        return result == Result::Connected || result == Result::AlreadyConnected || result == Result::PendingInstall ||
               result == Result::Disconnected || result == Result::NotConnected;
    }
};

// ---------------------------------------------------------------- Claude Desktop
// The manifest.json text of the bundle (pretty JSON).
std::string McpbManifest(const Target& t);
// Writes the .mcpb (zip: manifest.json + server/browser-annotations-mcp.exe, method "stored", CRC-32) to
// outPath atomically. False + error when the bridge cannot be read or the file cannot be written.
bool BuildMcpb(const Target& t, const std::wstring& outPath, std::string* error = nullptr);
// One click: .mcpb associated => build <workDir>\<name>.mcpb and open it (PendingInstall); otherwise (or if
// opening fails) merge the config entry. NoClient when Claude Desktop has no configuration folder.
Report ConnectClaude(const Environment& env, const Target& t, const std::wstring& workDir);
// Removes our mcpServers entry. If the extension was installed from the .mcpb: ManualStep (we never edit
// Claude's extension files).
Report DisconnectClaude(const Environment& env, const Target& t);
// File-level operations (used by the above; exposed for tests and the "Reconnecter" path).
Report MergeClaudeConfig(const std::wstring& configPath, const Target& t, std::int64_t now);
Report RemoveClaudeConfig(const std::wstring& configPath, const std::string& serverName, std::int64_t now);
// Our entry as found in the file: present + command + args ("" / empty when absent or unreadable).
struct ConfiguredEntry {
    bool present = false;
    std::wstring command;
    std::vector<std::wstring> args;
};
ConfiguredEntry ReadClaudeEntry(const std::wstring& configPath, const std::string& serverName);
// The extension installed from our .mcpb (<ClaudeConfigDir>\Claude Extensions\*\manifest.json named serverName).
bool ClaudeExtensionInstalled(const Environment& env, const std::string& serverName);

// ---------------------------------------------------------------- Codex
Report ConnectCodex(const Environment& env, const Target& t);
Report DisconnectCodex(const Environment& env, const Target& t);
Report MergeCodexToml(const std::wstring& configPath, const Target& t, std::int64_t now);
Report RemoveCodexToml(const std::wstring& configPath, const std::string& serverName, std::int64_t now);
ConfiguredEntry ReadCodexEntry(const std::wstring& configPath, const std::string& serverName);

// ---------------------------------------------------------------- state
// "Connecté" = our entry is configured (or the Claude extension is installed) or the client read the store
// recently (readers\<client>.json, written by the bridge: client slug containing "claude" / "codex").
struct Status {
    bool configured = false;          // config entry present (Claude: or extension installed)
    bool extensionInstalled = false;  // Claude only
    bool stalePath = false;           // configured, but command/--store differ from the target: "Reconnecter"
    bool recentlyRead = false;
    std::int64_t lastReadAt = 0;
    std::string readerLabel;          // "Claude Desktop", "Codex"... (readers folder)
    bool removableDrive = false;      // the bridge or the store is on a removable drive
    bool connected() const { return configured || recentlyRead; }
};
Status ClientStatus(const Environment& env, Client c, const Target& t, const std::vector<BrowserAnnotations::Reader>& readers,
                    std::int64_t recentSeconds = RecentReadSeconds);

// ---------------------------------------------------------------- building blocks (pure, tested)
// Minimal zip writer (method 0 "stored", CRC-32, UTF-8 names). Valid for every zip reader; no deflate is
// needed for a 1 MB executable and no external tool or COM shell zip folder is involved.
struct ZipEntry {
    std::string name;  // forward slashes
    std::string data;
};
std::string ZipStored(const std::vector<ZipEntry>& entries, std::int64_t unixTime);
std::uint32_t Crc32(const std::string& data);
// Windows argv quoting (CommandLineToArgvW / MSVC CRT rules) of one argument.
std::wstring QuoteArg(const std::wstring& arg);

}  // namespace AgentConnect
