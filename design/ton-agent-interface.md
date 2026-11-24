# « Ton agent » — the agents' interface (worker A)

Paper « Browser Design » › « Onboarding agents — Ton agent (maquette prête) ». Branch `VALXXII/ton-agent`.
Workers: **A** mechanics (this interface: `src/agent-status.h`, `src/agent-install.h/.cpp`, `src/agent-runtime.*`,
`src/chat-host-turn.h`), **B** the tutorial screen + the chat's first-message block, **C** Réglages › Agents + the band's
reminder + no more MCP auto-open. Merge order A → B → C, all after Fix's lot R.

## One source of truth

`AgentStatus` (src/agent-status.h) is the only place that says whether Claude Code / Codex is usable. The tutorial, its
A7 status, the chat (panel, comment bubble, model menu), Réglages › Agents and the annotation band all read
`AgentStatus::Get(agent)` and subscribe to its changes. Nothing else detects agents (the MCP « Connecter un agent »
window keeps its own MCP-config status: it is about external apps, not the chat's agents).

| `State` | meaning | tile (Paper) | strong button |
| --- | --- | --- | --- |
| `Unknown` | first detection running | nothing definite | — |
| `NotInstalled` | no usable CLI (`reason`: `None`, `DesktopOnly`, `TooOld`) | ⤓ Installer | « Installer Claude Code / Codex » |
| `Installing` | official installer running (`stage`) | ⤓ Installation… (animated) | « Installation… » (dimmed) |
| `NeedsSignIn` | installed, signed out | 📶 Connecter | « Se connecter à … » |
| `SigningIn` | the sign-in console is open | 📶 Connecter (busy) | « Se connecter à … » (busy) |
| `Ready` | installed and signed in | ✓ Prêt | « Ouvrir le chat » |
| `Error` | last install / sign-in failed (`reason`) | ☹ Erreur | « Réessayer » + « Installer moi-même ↗ » (`ManualInstall`) |

`Reason::DesktopOnly`: only Claude Desktop is installed — its Claude Code is not usable by the browser: « not installed ».
`Status::version` / `path` are filled whenever a CLI was found (« Version 2.1 » in Réglages).

## Who calls what (all on the UI thread)

| call | who | effect |
| --- | --- | --- |
| `AgentStatus::Init(host)` | main (startup, after the data folder is known) | first detection of both agents |
| `AgentStatus::Shutdown()` | main (WM_DESTROY) | cancels an install (kills its process tree), closes a sign-in console |
| `Get(agent)`, `AnyReady()`, `FirstReady()` | everyone | the current status (cheap) |
| `Subscribe(listener)` / `Unsubscribe(id)` | B, C, the chat | listener(status) after every change of that agent |
| `Refresh()` / `Refresh(agent)` | B (screen shown), C (settings opened), chat (panel opened), window focus | detects again (off thread) |
| `Install(UserClicked(agent))` | B / C / chat: the strong button's click ONLY | NotInstalled / Error → Installing → NeedsSignIn / Ready / Error |
| `SignIn(agent)` | « Se connecter à … » | NeedsSignIn → SigningIn → Ready / Error(signInFailed) |
| `Retry(UserClicked(agent))` | « Réessayer » | the failed step again |
| `ManualInstall(agent)` | « Installer moi-même ↗ » | `{command, url}`: show the command to copy, open the page |

**Consent**: `Install` and `Retry` need an `AgentStatus::Consent` made by `UserClicked(agent)` — call it only in the click
handler of the strong button. Nothing installs by itself (tested).

**Paid plan**: Claude Code needs a paid Claude plan (Pro, Max, Team, Enterprise) or a Console account: say it at the moment
of signing in to Claude Code (B / C wording). Nothing about Codex plans.

## Threading and events

- Detection (`AgentRuntime::Detect`: runs `claude --version`, `claude auth status`…), the install (download + PowerShell)
  and the sign-in console run OFF the UI thread through `Host::run` (main passes `Ann::Run`). Results come back on the UI
  thread; then `Status::generation` grows and every listener is called with the changed agent's status.
- An install takes minutes and reports no percentage: `stage` is `Running` for the whole run (`Checking` is reserved);
  show the animated « Installation… » (the Downloads icon's morph).
- A detection that finishes after an install began never overwrites `Installing` / `SigningIn`.

## The installer (src/agent-install.h, called by AgentStatus only)

Official scripts, per user, never elevated: Claude Code `https://claude.ai/install.ps1` (then `%USERPROFILE%\.local\bin`
is added to the USER PATH — HKCU\Environment, REG_EXPAND_SZ kept, WM_SETTINGCHANGE), Codex
`https://chatgpt.com/codex/install.ps1` (adds itself to the user PATH). The script is downloaded over HTTPS into a fresh
folder under %TEMP% and run with `powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File …`, hidden, in a
job, 15 min timeout. Errors: `Network` (network, proxy, TLS), `FileInUse` (« used by another process »: antivirus),
`ExecutionPolicy`, `Timeout`, `ScriptFailed`, `Disk`; then `NotFoundAfterInstall` when it « succeeded » but no CLI is found.

## Test hooks (never the real installers, CLIs or PATH in tests)

- **Unit tests** (tests/acp/agent_status_test.cpp): `Host::detect` / `Host::environment` / `Host::signIn` injected,
  `Host::run` synchronous; fake installer scripts (`Options::scriptFile`); PATH on a test key (`PathTarget::key` under
  HKCU\Software\BrowserTests).
- **A browser with a fake profile** (trial launchers, B / C real-window checks), non-release builds with
  `BROWSER_ANNOTATIONS_TEST=1`:
  - `USERPROFILE`, `LOCALAPPDATA`, `APPDATA`, `PATH` of the browser process → temporary folders: the detection looks there
    (fake `claude.exe` / `codex.exe` = `tests/acp/fake_cli.cpp` with `<exe>.version` and `<exe>.login` beside them;
    Claude Desktop « only » = an empty `%LOCALAPPDATA%\AnthropicClaude` folder);
  - `BROWSER_AGENT_INSTALL_SCRIPT=<fake .ps1>` replaces the official script (it copies the fake CLI into the fake folders);
  - `BROWSER_AGENT_PATH_KEY=<key under HKCU>` replaces HKCU\Environment;
  - the sign-in stays the CLI's own: the fake CLI's `auth login` / `login` writes `0` into its `.login` file.

## Strings (src/i18n-catalog.inc)

The interface has no text. Each worker adds its strings in its own block: A « agents: status/install » (log-free: none
needed so far), B « agents: onboarding/chat », C « agents: settings/band ».
