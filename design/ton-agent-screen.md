# « Ton agent » — the screen and the chat's block (worker B)

Paper « Browser Design » › « Onboarding agents — Ton agent (maquette prête) » (file 01M6Q7YT14ZKMGET54ASPNGQEC, page p-12-0).
Branch `VALXXII/ton-agent-ui`, on A's `VALXXII/ton-agent` (the agents' one status: `src/agent-status.h`,
`design/ton-agent-interface.md`). Everything here reads that status and nothing else; nothing installs without a click
(`AgentStatus::UserClicked`): on the screen, the tile's own click (Val, 2026-11-24: « la tuile lance l'action », Paper note
265A-0); in the chat's block and Réglages › Agents, the row's button.

## 1. The screen

One window, two uses:

- **the tutorial's sixth step** (#125, `src/annotation-onboarding.h`, in the place of « Connecter »): progress dots, « Plus
  tard » goes on to A7, « Continuer » (once an agent is ready) too;
- **alone** (`Options::alone`): the band's reminder, the chat, Réglages — `OpenTonAgent` below. No progress; « Plus
  tard », the close button and Échap close it; « Continuer » closes it and opens the chat with the ready agent.

The footer never holds an action: « Plus tard » on the left, « Continuer » (accent) on the right only once an agent is
ready (every board, « Prêt » included).

What it shows is decided in `src/annotation-onboarding-agent.h` (pure, tested):

| target's state | title / body (catalogue) | its tile (the click) | under the tiles | card top |
| --- | --- | --- | --- | --- |
| first detection | Choisissez votre agent | no mark, no click | — | centred on 220 |
| not installed (also Claude Desktop only) | Choisissez votre agent | ⤓ Installer, at rest (click: install) | — | centred on 220 (141.5 → 141) |
| too old (Designer, note 26AL-0) | Choisissez votre agent | ⤓ Mettre à jour, at rest (click: install again) | — | centred |
| installing | Installation de X / Ça peut prendre… | ⤓ Installation... animated, cyan; the other tile waits (32 %, no click) | — | 123 |
| needs sign-in | Connectez X (Claude Code: its paid plan; Codex: its ChatGPT account) | 📶 Connecter, at rest (click: sign in) | — | centred |
| signing in (3b 263M-0) | Connexion en cours / Terminez la connexion dans l'onglet ouvert (another browser: dans votre navigateur), puis revenez ici. | 📶 Connexion…, cyan, no click; the other tile waits | two lines (« …dans un autre onglet » / « …dans votre navigateur web ») + « Rouvrir la page de connexion ↗ » when its Browser tab was closed | 72 |
| ready | X est prêt / Il travaille sur vos projets… | ✓ Prêt, cyan, no click | — | 140 |
| install failed | Choisissez votre agent | ☹ Erreur, danger (hover: « Réessayer »; click: retry) | two lines + « Installer moi-même ↗ » | 72 |
| sign-in failed (5 25OI-0) | Connectez X / Connectez-vous une fois avec votre compte Claude, … | ☹ Connexion échouée, danger (hover: « Réessayer ») | two lines (Claude Code: its paid account; Codex: its ChatGPT account) | 72 |

- **The tile is the button** (265A-0): its click runs its action at once and is the consent. Hovered, a tile with an
  action shows the hand, #262626 with inset 1 #3A3A3A and its mark white (at rest #1F1F1F, #2A2A2A, #A6A6A6); a failed
  one keeps its danger frame and says « Réessayer » in #FF7B6E. Installation… / Connexion… / Prêt: no hover, no click.
  While one agent installs or signs in, the other tile waits: 32 %, no click (Val; boards 2 and 3b). Keyboard focus on a
  failed tile says « Réessayer » too (Designer).
- **Target**: the tile acted on (the text above the footer follows it); before any click, the agent that is busy, else
  ready, else failed (Claude Code first), else Claude Code. Busy tiles are cyan wherever they are; a ready or failed tile
  when it is the target.
- **Connexion dans un autre onglet** (263M-0, note 265A-0): the CLI's sign-in opens its page with the default browser.
  When that is Browser, the page opens in a tab (a link handed over by the CLI's launch: `TonAgentPageOpened`): the
  window steps aside while another tab is in front (hidden with its veil, the browser usable), and comes back over the
  tab it was opened over — still signing in: 3b; signed in meanwhile: Prêt (the hub's sign-in check:
  `AgentStatus::Refresh` while the console waits). The page's address is kept for « Rouvrir la page de connexion ↗ »
  once its tab is closed; another default browser hands nothing over: no link. Back in Browser from another app while a
  sign-in runs: the agents are asked again (`Ann::AgentsFocusBack`).
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
- Keyboard: Tab through the tiles with an action, the link, the footer, language, close; ← → between those tiles;
  Espace / Entrée on a tile runs it; Entrée elsewhere = « Continuer »; Échap = close. UIA: a tile with an action is a
  button named by it (« Installer Claude Code », « Se connecter à Codex », « Réessayer l'installation de … »), its mark
  as status (waiting: disabled, « Disponible après l'installation en cours »); without one, a text (name, mark); the
  lines under the tiles are text.

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

**Nothing is sent to an agent that is not ready** (Val, 24/11): while the chat's agent is not installed, signed out,
installing or signing in (the hub's SigningIn is « preparing » for the page, never « starting »), « Envoyer » is off and
Entrée does nothing; what was typed stays, and « Connectez un agent pour envoyer » (the catalogue's
`ton_agent_chat_send_hint`, sent with `agents.setup`) shows under the input — panel and bubble; no board draws that line
yet: the keys row's look (12/16 #FFFFFF80), provisional. An agent ready again: « Envoyer » works, the line goes, the text
is still there. A turn that fails because its agent stopped shows ONE card (the cause checked again: FailureCause); the
messages waiting behind it are held (not sent to fail the same way) until the agent is ready again or « Envoyer
maintenant ».

« Installer » of a missing agent elsewhere in the chat (the dock's strip « X n'est pas installé… », the model menu's
header, the folder screen's agent rows) now opens the « Ton agent » screen (`agents.setup.open` → `OpenTonAgent(Chat)`)
instead of the old runtime download (Val's decision via Fix, 24/11: the chat's « Installer » uses « Ton agent »; never an
install without the click on the screen's strong button). The other buttons keep their messages: « Réessayer » of a
too old CLI or of a failed runtime download sends `agent.prepare`, « Se connecter » `agent.connect` — A routes them on top
of lots R and 2 (`PrepareAgent`: CLI missing / too old / Claude Desktop only → `OpenTonAgent(Chat)`; `Connect()` →
`AgentStatus::SignIn`, so `CancelSignIn` applies).

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
