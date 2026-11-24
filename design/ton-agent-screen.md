# « Ton agent » — the screen and the chat's block (worker B)

Paper « Browser Design » › « Onboarding agents — Ton agent (maquette prête) » (file 01M6Q7YT14ZKMGET54ASPNGQEC, page p-12-0).
Branch `VALXXII/ton-agent-ui`, on A's `VALXXII/ton-agent` (the agents' one status: `src/agent-status.h`,
`design/ton-agent-interface.md`). Everything here reads that status and nothing else; nothing installs without the click
of a strong button (`AgentStatus::UserClicked`).

## 1. The screen

One window, two uses:

- **the tutorial's sixth step** (#125, `src/annotation-onboarding.h`, in the place of « Connecter »): progress dots, « Plus
  tard » goes on to A7, « Retour » (once an agent is ready) to A5, « Ouvrir le chat » ends the tutorial (finished) and
  opens the chat with that agent;
- **alone** (`Options::alone`): the band's reminder, the chat, Réglages — `OpenTonAgent` below. No progress; « Plus
  tard », the close button and Échap close it; « Ouvrir le chat » closes it and opens the chat.

What it shows is decided in `src/annotation-onboarding-agent.h` (pure, tested):

| target's state | title / body (catalogue) | target tile | strong button | card top |
| --- | --- | --- | --- | --- |
| first detection | Choisissez votre agent | no mark | « Installer X » dimmed | centred on 220 |
| not installed (also Claude Desktop only, too old) | Choisissez votre agent | ⤓ Installer, at rest | « Installer X » | centred on 220 (141.5 → 141) |
| installing | Installation de X / Ça peut prendre… | ⤓ Installation... animated, cyan outline | « Installation… » dimmed (#1F4A4C, #FFFFFF99) | 123 |
| needs sign-in / signing in | Connectez X (Claude Code: its paid plan; Codex: its ChatGPT account) | 📶 Connecter, cyan outline | « Se connecter à X » (dimmed while the console is open) | centred |
| ready | X est prêt / Il travaille sur vos projets… | ✓ Prêt, cyan outline | « Ouvrir le chat » (+ « Retour » in the tutorial) | 140 |
| failed | Choisissez votre agent + the two lines under the tiles | ☹ Erreur, danger outline and 10 % | « Réessayer » (+ « Installer moi-même ↗ » after an install) | 72 |

- **Target**: before any click, the agent that is busy, else ready, else failed (Claude Code first), else Claude Code
  (Claude Code 1 Choisir); a click on a tile makes it the target (Codex 1 Choisir: « Installer Codex », no outline yet).
  Only the target is outlined, and only once acted on.
- **« Installation… »**: the Downloads button's Lottie layers (`src/download-icon.h`: track, fill, arrow, cut-out; the
  same frames, start morph and 1.3 s loop) composed in one ink, the profile's light accent (#2FD1D9 on the boards). At
  the end the arrow morphs into the check (`download-success.json`, 400 ms; held 600 ms before « Connecter » when a
  sign-in follows), or the icon fades into the error icon (150 ms). Reduced motion: still icons, the words change.
- **The card glides** between its places in 220 ms (ease-out), as the trial's page does; reduced motion: no glide.
- **« Installer moi-même ↗ »**: the official command (`AgentStatus::ManualInstall`) goes to the clipboard, the screen
  closes (skipped) and the official page opens in a new tab (a page opened behind a modal would be hidden).
- **A7** reads the same status: « Claude Code est prêt » / « Codex est prêt » (READY = installed and signed in, the
  chat's own test) or « Aucun agent prêt »; its sentence names that agent or « Votre agent ». Never « connecté » when the
  chat would say « Pas installé » (the MCP status the tutorial used to show is ignored).
- Refresh: when the screen is shown and when its window is activated again (back from the web page or PowerShell).
- Keyboard: Tab through the tiles, « Installer moi-même », the footer, language, close; ← → between the tiles; Espace /
  Entrée picks a tile; Entrée elsewhere = the strong button; Échap = close. UIA: the tiles are buttons named by the
  agent, their mark as status; the failure lines are text.

Values (Paper, exact): card 400 × (157 | 160 | 228) at x 160, #171717, radius 16, padding 12, gap 10; tile radius 12,
padding 26 / 12 / 18, at rest #1F1F1F with inset 1 #2A2A2A, current #192A2A inset 1.5 #2FD1D9, failed danger 10 % inset 1.5
danger; logo 34 (clipped to its box like the board's svg), name 16/21 600 white, 10 under it the mark: icon 14, gap 4,
13/13 500 #A6A6A6 (light accent when current; « Erreur » 13/17 danger; « ✓ Prêt » one text 13/17); failure lines 12/16
#A6A6A6 centred, 12 under the tiles, link 12/16 500 #E6E6E6 underlined, 6 under them. Colours follow the profile (Rehue)
like the rest of the tutorial.

## 2. The chat's first-message block

Paper 22N3-0 « Chat — premier message, rien n'est prêt », in the panel and in the comment bubble (the same page,
`assets/chat-panel/`). Shown on an empty chat while no agent is ready (both detected), in the place of « Que voulez-vous
changer ? »; never over a thread; gone once an agent is ready. The rows follow the **Designer's states for Réglages ›
Agents** (24/11, « keep the tutorial and chat consistent »): no coloured dot — an icon and the words; ONE strong button
on the missing action (the profile's accent), anything else neutral #2A2A2A:

| state | row | button |
| --- | --- | --- |
| not installed | ⤓ « Pas installé » #A6A6A6 (Claude Desktop only: + 13 px #8C8C8C « L'appli Claude est installée, mais le chat a besoin de Claude Code (même compte). ») | strong « Installer » |
| too old | ⤓ « Version trop ancienne » | strong « Mettre à jour » |
| installing | ⤓ animated « Installation… » accent | — |
| needs sign-in | 📶 « Pas connecté » | strong « Se connecter » |
| signing in | 📶 « Connexion… » accent | neutral « Annuler » (`AgentStatus::CancelSignIn`) |
| failed install / sign-in | ☹ « Échec de l'installation » / « Échec de la connexion » #FF7B6E (+ « Installer moi-même ↗ » 13 px underlined) | neutral « Réessayer » |
| ready | ✓ « Prêt · version 2.1 » accent | — (the block is gone by then) |

The paid Claude account line stays under the card. Paper values (measured in the page by the test): padding 16 / 12,
gap 12; title 16/22 600; line 12/17 #A6A6A6; card #1C1C1C border 1 #262626 radius 12 padding 4 gap 2; row padding 10
radius 10 gap 10 (56 high); logo box 32 #262626 radius 8, logo 16; name 14/18 500; state 12/16, icon 14, gap 4; button 28,
padding 10, radius 6, 13/16 500; note 12/17 #8C8C8C. In the web page the animated « Installation… » is the same morph
drawn in SVG from `design/lottie/download-progress.py`'s data (tray → ring in 233 ms with its easing, the arrow dropping
then bobbing 12 units every 1.3 s, a band cut around the arrow); reduced motion: the still icon.

« Installer » of a missing agent elsewhere in the chat (the dock's strip « X n'est pas installé… », the model menu's
header, the folder screen's agent rows) now opens the « Ton agent » screen (`agents.setup.open`) instead of the old
runtime download.

Protocol (v1, `src/chat-ton-agent.h` ↔ `assets/chat-panel/panel.js`):

- host → page `agents.setup` `{show, title, body, note, accent, rows:[{id: "claude"|"codex", name, icon:
  "download"|"wifi"|"error"|"check", ink: "grey"|"accent"|"danger", status, animate?, note?, manual?, action?: {id:
  "install"|"signIn"|"retry"|"cancel", label, strong}}]}` — sent when the page is ready and after every change of the
  status (one listener); every word from the catalogue block « agents: onboarding/chat ».
- page → host `agents.setup.act {agentId, action}` (the host checks the state still asks for it: install with the click's
  consent, sign in, retry with the consent, cancel), `agents.setup.manual {agentId}` (command copied, page in a new tab),
  `agents.setup.open` (the screen alone).

## 3. The entry point (for worker C and the chat)

```cpp
// src/ton-agent-entry.h (declared by C)        // defined in src/chat-ton-agent.h (B), end of src/annotation-mode.h
enum class TonAgentFrom { Band, Chat, Settings };
void OpenTonAgent(TonAgentFrom from);
```

- `src/annotation-onboarding.h` defines `BROWSER_TON_AGENT_SCREEN 1` (included at the top of `src/annotation-mode.h`):
  C's stand-in under `#ifndef BROWSER_TON_AGENT_SCREEN` drops out by itself once B is merged.
- UI thread, mode on or off. If the tutorial is open, it is brought to the front (its sixth step is this screen);
  if the screen is already open alone, it is brought to the front; else it opens alone over the browser (modal, the
  profile's colours, reduced motion followed). Its « Ouvrir le chat » selects that agent in the chat and opens the
  panel (Ctrl+Maj+D's path: the mode first if needed) or focuses it; logs « TON AGENT … ».
- The tutorial opened by `src/annotation-mode.h` keeps working without new hooks there: « Ouvrir le chat » and the
  official page use `AnnotationOnboardingUI::Fallbacks()`, set by the chat at start (`src/chat-ton-agent.h`). C may set
  `Options::hooks.openChat / openPage` itself instead.
- What C can now drop in `src/annotation-mode.h` (left untouched by B, C's file): the tutorial's `o.claude` / `o.codex` /
  `hooks.connect` / `onboarding->SetStatus(...)` (MCP rows, ignored now), and the reminder's `agents.any()` (MCP) in
  `OnboardingClosed` — the reminder should read `AgentStatus::AnyReady()`.

## 4. Tests

- `python -I tests/onboarding/run_ton_agent.py [--toolchain <checkout with tools/ and deps/>]`: `ton_agent_test` (the
  decisions, the flow, the window on the real hub over A's fake profiles — fake CLI, `sandbox/agents/fake-install.ps1`
  modes, the PATH on a test key; present / absent / Claude Desktop only, Codex signed in / out, install success /
  failure, retry, « Installer moi-même », « Plus tard », « Retour », « Ouvrir le chat », alone, keys, English) and
  `annotation_onboarding_test` (A1 – A7 with A6 « Ton agent »). Renders: `build/ton-agent-render/`, compared with the
  Paper exports by `python tests/onboarding/ton_agent_compare.py --pairs build/ton-agent-pairs`.
- `python tests/chat-panel/ton_agent_block.py`: the block in the panel and the bubble (headless EdgeCore, fake host
  scenarios `ta-*`), every state, the messages, the Paper values, the morph's timings, renders
  `build/chat-qa/ton-agent/`. `tests/chat-panel/model_menu_128.py` and `run.py` still pass.
- Paper exports: `design/ton-agent-evidence/paper/` (the nine boards and the chat's frame, 1x).
