#pragma once
// « Ton agent » — the official installers of Claude Code and Codex, per user, without administrator rights.
// Design: design/ton-agent-interface.md. The UI never calls this directly: AgentStatus::Install does (src/agent-status.h).
//
//   Claude Code  https://claude.ai/install.ps1 (the official « irm … | iex »): installs into %USERPROFILE%\.local\bin,
//                which it may NOT add to the PATH → EnsureOnUserPath adds it to the USER PATH (HKCU\Environment, no
//                administrator rights) and tells running programs (WM_SETTINGCHANGE « Environment »); the browser's
//                detection already re-reads the PATH from the registry, so the CLI is found at once.
//   Codex        https://chatgpt.com/codex/install.ps1: installs into %LOCALAPPDATA%\Programs\OpenAI\Codex\bin and adds
//                itself to the user PATH.
// How: the script is downloaded over HTTPS (system proxy settings) into a fresh folder under %TEMP%, then run with
// `powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File <script>` in a hidden window, in a job (killed
// with the browser), its output kept in a log beside it to name the error. Never elevated (no « runas », no manifest
// asking for it). The script gives no progress: the UI shows its animated « Installation… » state.
// Blocking: call Run off the UI thread (AgentStatus does).
#include <atomic>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "agent-runtime.h"

namespace AgentInstall {

using Agent = AgentRuntime::Agent;

// i18n-exempt-begin: official addresses and folders
inline const wchar_t* OfficialScriptUrl(Agent agent) {
    return agent == Agent::Claude ? L"https://claude.ai/install.ps1" : L"https://chatgpt.com/codex/install.ps1";
}
// i18n-exempt-end

enum class Stage { Downloading, Running, Checking };
enum class Error {
    None,
    Cancelled,        // the browser closed (the process tree is killed)
    Network,          // the script could not be downloaded, or the script could not reach its server (network, proxy, TLS)
    FileInUse,        // « being used by another process » / access denied while replacing a file (antivirus, a running CLI)
    ExecutionPolicy,  // PowerShell refused to run scripts (a policy set by an administrator)
    Timeout,          // still running after `timeoutMs`: killed
    ScriptFailed,     // non-zero exit we cannot name
    Disk,             // could not create the work folder or write the script
    PathUpdate,       // installed, but the user PATH could not be updated (the CLI is still found by its folder)
};
const char* ErrorName(Error error);   // stable identifiers

// Where the user PATH lives. Tests and the fake-profile trials point it at a key of their own (never the real one).
struct PathTarget {
    // i18n-exempt-begin: registry names
    std::wstring key = L"Environment";   // under HKEY_CURRENT_USER
    std::wstring value = L"Path";
    // i18n-exempt-end
    bool broadcast = true;               // WM_SETTINGCHANGE « Environment » after a change
};

struct Options {
    Agent agent = Agent::Claude;
    std::wstring scriptUrl;     // "" = OfficialScriptUrl(agent); https only
    std::wstring scriptFile;    // a local .ps1 run instead of downloading (tests / fake-profile trials; refused in release builds)
    std::wstring powershell;    // "" = %SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe
    std::wstring workDir;       // "" = %TEMP%\browser-agent-install\<agent>-<time>
    unsigned timeoutMs = 15 * 60 * 1000;
    std::vector<std::pair<std::wstring, std::wstring>> environment;   // extra variables for the script (tests: the fake's target)
    std::wstring userBinDir;    // a folder to put on the user PATH after success ("" = none); Claude: %USERPROFILE%\.local\bin
    PathTarget path;
};
// The official installer for this agent in this environment (Claude: userBinDir = <userProfile>\.local\bin). Test hooks,
// non-release builds under BROWSER_ANNOTATIONS_TEST only: BROWSER_AGENT_INSTALL_SCRIPT=<.ps1> replaces the download,
// BROWSER_AGENT_PATH_KEY=<key under HKCU> replaces HKCU\Environment.
Options DefaultOptions(Agent agent, const AgentRuntime::Environment& env);

struct Result {
    Error error = Error::None;
    int exitCode = 0;
    bool pathAdded = false;     // userBinDir was added to the user PATH
    std::string detail;         // developer detail (logs): Windows error, the output's telling line
    std::wstring log;           // the script's output, kept for the log (under the work folder)
    bool Ok() const { return error == Error::None || error == Error::PathUpdate; }
};
// Downloads (or takes scriptFile) and runs the installer; then adds userBinDir to the user PATH. `stage` is called on
// the calling thread. `cancel` may be set from any thread: the installer's process tree is killed.
Result Run(const Options& options, const std::function<void(Stage)>& stage = nullptr, const std::atomic<bool>* cancel = nullptr);

// ---------------------------------------------------------------- pure helpers (unit-tested)
// The error a finished installer stands for: its exit code and its output (stdout + stderr).
Error Classify(int exitCode, std::string_view output);
// Whether a PATH value (« a;b;%USERPROFILE%\x ») already holds `dir` (case-insensitive, trailing slashes and
// environment variables expanded with `expand`).
bool PathContains(std::wstring_view pathValue, std::wstring_view dir, const std::function<std::wstring(const std::wstring&)>& expand = nullptr);
// Adds `dir` at the end of the user PATH of `target` when missing (REG_EXPAND_SZ kept); `added` tells whether it did.
bool EnsureOnUserPath(const std::wstring& dir, const PathTarget& target, bool* added, std::string* error);

}  // namespace AgentInstall
