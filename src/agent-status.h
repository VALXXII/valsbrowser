#pragma once
// « Ton agent » — the ONE source of truth for the chat's agents (Claude Code, Codex) on this computer.
// Design: design/ton-agent-interface.md. Paper « Onboarding agents — Ton agent (maquette prête) ».
//
// Every screen that talks about an agent reads it here and asks for actions here: the tutorial's « Ton agent » step,
// the chat (first message, panel, comment bubble, model menu), Réglages › Agents, the annotation band's reminder.
// The tutorial must never say « connecté » when the chat says « Pas installé »: there is only this one status.
//
//   detection  the chat's own: claude.exe / codex.exe found (AgentRuntime::Detect: installers' folders, PATH re-read
//              from the registry, WinGet, npm), its version supported, its sign-in status (`claude auth status`,
//              `codex login status`: exit code only). Claude Desktop alone (no claude.exe) is « not installed »
//              (Reason::DesktopOnly): its own Claude Code is not usable by the browser.
//   install    the OFFICIAL installer (src/agent-install.h), per user, never elevated, only after the user's click
//              (Consent: the strong button « Installer Claude Code » / « Installer Codex » / « Réessayer »).
//   sign-in    the CLI's own (`claude auth login`, `codex login`) in a visible console that opens the web browser;
//              then a new detection.
//
// Threading: everything below is called on the UI thread. Detection, installs and sign-ins run off it through the
// Host's `run`; their results come back on the UI thread, then every listener is called.
// No interface text here: states, reasons and the official command / page only (the catalogue has the wording).
#include <functional>
#include <optional>
#include <string>

#include "agent-runtime.h"

namespace AgentStatus {

using Agent = AgentRuntime::Agent;
inline constexpr Agent AllAgents[2] = {Agent::Claude, Agent::Codex};

// ---------------------------------------------------------------- the status of one agent
enum class State {
    Unknown,       // not detected yet (the browser just started): show nothing definite, a detection is running
    NotInstalled,  // no usable CLI (reason: None, DesktopOnly, TooOld) → « Installer »
    Installing,    // the official installer runs (stage) → « Installation… »
    NeedsSignIn,   // installed, signed out → « Connecter » / « Se connecter à … »
    SigningIn,     // the sign-in console is open (the web browser) → keep « Se connecter » busy
    Ready,         // installed and signed in (or sign-in unknown: the session start tells) → « Prêt »
    Error,         // the last install or sign-in failed (reason) → « Erreur », « Réessayer », « Installer moi-même »
};
enum class Reason {
    None,
    // NotInstalled
    DesktopOnly,           // only Claude Desktop is here: its Claude Code is its own, not usable by the browser
    TooOld,                // a CLI older than the chat supports: installing again updates it
    // Error (install)
    Network,               // the installer (or its download) could not reach the official server: network, proxy
    FileInUse,             // « used by another process »: an antivirus or a running copy holds a file
    ExecutionPolicy,       // PowerShell refused to run the script (a policy set by an administrator)
    Timeout,               // the installer did not finish in time (15 min)
    InstallerFailed,       // the installer ended with an error we cannot name
    NotFoundAfterInstall,  // the installer said it succeeded but the CLI is still not found
    Disk,                  // could not write the script to the temporary folder
    // Error (sign-in)
    SignInFailed,          // the console closed and the CLI is still signed out
    SignInTimeout,         // no sign-in within 15 min
    // either
    Cancelled,             // the browser closed during it (never shown: the next start detects again), or the user cancelled the
                           // sign-in (« Annuler »): NeedsSignIn with this reason — not a failure, nothing to show
};
enum class Stage { None, Downloading, Running, Checking };   // Installing only: where the install is (no percentage exists)

struct Status {
    Agent agent = Agent::Claude;
    State state = State::Unknown;
    Reason reason = Reason::None;
    Stage stage = Stage::None;
    std::string version;        // "2.1.325" when a CLI was found (also TooOld)
    std::wstring path;          // the CLI used
    bool desktop = false;       // Claude Desktop is installed (Claude only; informative)
    bool runtimeReady = false;  // the chat's own runtime for this agent (private Node + adapter) is downloaded: the
                                // chat's first message prepares it otherwise (« Préparation… »); not part of « installed »
    unsigned generation = 0;    // +1 on every change of this agent (cheap « did it change »)
    std::string detail;         // developer detail for the logs (never shown)
    bool signedIn = false;      // the CLI's own status says signed in (Ready also covers « unknown »): #128 v2's model read

    bool Ready() const { return state == State::Ready; }
    bool Busy() const { return state == State::Installing || state == State::SigningIn; }
    bool Installed() const { return state == State::NeedsSignIn || state == State::SigningIn || state == State::Ready; }
};
const char* StateName(State state);    // "unknown" | "notInstalled" | "installing" | "needsSignIn" | "signingIn" | "ready" | "error"
const char* ReasonName(Reason reason); // stable identifiers ("desktopOnly", "network", …)

// What the board's « Installer moi-même ↗ » shows: the official command to paste in PowerShell, and the official page.
struct Manual { const wchar_t* command; const wchar_t* url; };
Manual ManualInstall(Agent agent);

// Pure: a detection (and whether Claude Desktop is installed) → the status. Unit-tested (the detection matrix).
Status FromDetection(const AgentRuntime::Detection& detection, bool desktopInstalled);
// Claude Desktop's own folders (install, MSIX package, configuration) under the environment's AppData folders.
bool DesktopInstalled(const AgentRuntime::Environment& env);

// ---------------------------------------------------------------- the hub (UI thread)
struct Consent { Agent agent = Agent::Claude; bool given = false; };
// The ONLY way to get a consent: the user clicked the strong button (« Installer Claude Code », « Installer Codex »,
// « Réessayer »). Nothing installs without it; nothing installs by itself.
inline Consent UserClicked(Agent agent) { return Consent{agent, true}; }

struct Host {
    std::wstring dataRoot;   // the browser's data folder (AgentRuntime::Environment::dataRoot)
    // Runs `work` off the UI thread, then `done` on the UI thread (main: Ann::Run). Tests may run both at once.
    std::function<void(std::function<void()> work, std::function<void()> done)> run;
    std::function<void(const std::wstring&)> log;   // the browser log (no secrets: states, codes, paths)
    // ---- injectable (tests, trial launchers with fake profiles); empty = the real thing
    std::function<AgentRuntime::Environment()> environment;                         // default Environment::FromProcess(dataRoot)
    std::function<AgentRuntime::Detection(const AgentRuntime::Environment&, Agent)> detect;   // default AgentRuntime::Detect
    // The sign-in console: returns once it closed (or after `timeoutMs`); true = it ran. Default: RunTerminalSignIn.
    std::function<bool(const AgentRuntime::Environment&, Agent, unsigned timeoutMs, std::string* error)> signIn;
};
void Init(Host host);   // once, at startup; starts a first detection
void Shutdown();        // the browser closes: cancels a running install, closes a sign-in console

const Status& Get(Agent agent);
bool AnyReady();
std::optional<Agent> FirstReady();   // Claude Code first

// Listeners: called on the UI thread after every change of an agent's status (the changed one is passed).
using Listener = std::function<void(const Status&)>;
int Subscribe(Listener listener);    // returns an id for Unsubscribe
void Unsubscribe(int id);

// Actions. Each returns false when it cannot start now (busy, already ready, no consent).
bool Install(const Consent& consent);        // NotInstalled / Error → Installing → (detect) NeedsSignIn / Ready / Error
bool SignIn(Agent agent);                    // NeedsSignIn / Error(sign-in) → SigningIn → (detect) Ready / Error
bool Retry(const Consent& consent);          // Error: the failed step again (install with the consent, or sign-in)
// « Annuler » while signing in (Designer's board): only when the agent is SigningIn (false otherwise). Closes its sign-in
// console (its process tree is killed, the wait ends at once), then the normal end: a new detection — still signed out →
// NeedsSignIn (reason Cancelled, not an error); signed in meanwhile → Ready. Listeners are called as for any change.
#define AGENT_STATUS_CANCEL_SIGN_IN 1
bool CancelSignIn(Agent agent);
// detect both again (window focus, settings opened, chat opened). An agent signing in is not detected again: its CLI is
// asked whether it is signed in already (finished in the web page while the console waits) — then the console closes
// and the sign-in's own end says Ready.
void Refresh();
void Refresh(Agent agent);
void Refresh(std::function<void()> done);    // both again, then `done` on the UI thread (after the listeners)

}  // namespace AgentStatus
