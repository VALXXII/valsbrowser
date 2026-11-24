# Chat intégré — client ACP et runtime des agents (contrat A, C1 → C4)

Fichiers : `src/acp-client.{h,cpp}`, `src/agent-runtime.{h,cpp}`, `src/agent-runtime-pins.inc` (généré),
`tools/agents/pin-adapters.py`, `tests/acp/`. Les en-têtes sont la référence ; ce document dit comment s'en servir.

## 1. En deux phrases

`AgentRuntime` trouve la CLI de l'utilisateur (`claude.exe` / `codex.exe`), télécharge à la demande un Node privé et
l'adaptateur ACP figés, et fabrique la commande de lancement. `Acp::Session` lance cette commande dans un Job Object,
parle ACP v1 sur stdin/stdout hors du fil UI, et rend tout sous forme d'événements dans une file.

Aucun texte d'interface dans ces deux modules (la barrière `tools/i18n/check_strings.py` passe) : ils rendent des
**états** et des **codes** ; C4 choisit les phrases du catalogue (§6).

## 2. Usage type (C4)

```cpp
#include "agent-runtime.h"   // inclut acp-client.h

// Une fois, à la première ouverture du panneau (jamais au lancement du navigateur) :
AgentRuntime::Environment env = AgentRuntime::Environment::FromProcess(dataRoot);

// Hors fil UI (bloquant : lance `claude --version`, puis `claude auth status`, ~0,5 s) :
AgentRuntime::Detection d = AgentRuntime::Detect(env, AgentRuntime::Agent::Claude);
//   d.cli.state == Missing  -> « pas installé » : AgentRuntime::GetInstallHelp(agent).install (commande à copier)
//   d.cli.state == TooOld   -> « à mettre à jour » : GetInstallHelp(agent).update
//   d.login == SignedOut    -> « pas connecté » (indice ; la vérité vient du Start, voir plus bas)
//   d.NeedsPrepare()        -> il faut télécharger : Prepare (hors fil UI, progression, annulable)
std::atomic<bool> cancel{false};
AgentRuntime::PrepareResult p = AgentRuntime::Prepare(env, agent, [&](const AgentRuntime::Progress& pr) { /* PostMessage */ }, &cancel);

// Session (toutes les méthodes sont non bloquantes, appelables du fil UI) :
Acp::Session session;
session.SetWake([hwnd] { PostMessageW(hwnd, WM_APP_ACP, 0, 0); });   // appelé d'un fil de fond, une fois par lot
Acp::StartOptions o;
o.resolve = AgentRuntime::MakeResolver(env, agent);
o.cwd = projectDirUtf8;                                   // dossier absolu : sans dossier, pas de session
o.mcpServers = {{"browser-annotations", bridgeExeUtf8, {"--store", storeUtf8, "--site", origin}, {}}};
o.resumeSessionId = savedSessionId;                       // "" pour une nouvelle session
o.clientName = "browser"; o.clientVersion = BROWSER_VERSION_TEXT;
session.Start(o);                                         // -> exactement un événement Started

// Sur WM_APP_ACP (fil UI) :
std::vector<Acp::Event> events;
session.Poll(events);
for (const Acp::Event& e : events) { switch (e.type) { /* §4 */ } }

session.Prompt({Acp::ContentBlock::Text(preambule), Acp::ContentBlock::Text(element),
                Acp::ContentBlock::Image("image/png", base64), Acp::ContentBlock::Text(message)});
session.Cancel();                                 // bouton Stop / Échap
session.RespondPermission(requestId, optionId);   // "" = annulé
session.SetMode(modeId); session.SetConfigOption(optionId, valeur);
session.Dispose();                                // arrêt : tue l'agent et tout ce qu'il a lancé (non bloquant)
session.WaitStopped(3000);                        // à la fermeture du navigateur seulement
```

`Start` répond toujours par **un** `Started`, dont `started.state` vaut :

| `StartState` | Sens | Suite |
| --- | --- | --- |
| `Ready` | session ouverte (`sessionId`, `resumed`, capacités, `modes`, `configOptions`, `commands`) | `Prompt` |
| `NotInstalled` | CLI absente (`error.code == NotInstalled`) ou trop ancienne (`CliTooOld`, `error.detail` = version trouvée) | carte « Installer » / « Mettre à jour » avec `GetInstallHelp` |
| `NeedsPrepare` | Node privé ou adaptateur pas encore téléchargé | `Prepare`, puis `Start` à nouveau |
| `AuthRequired` | pas connecté ; `started.authMethods` liste les méthodes | §5 |
| `Failed` | `error.code` : `SpawnFailed`, `ProcessExited` (+ fin de stderr), `Timeout`, `Protocol`, `AgentError`… | carte d'erreur + « Réessayer » (= `Start`) |

Après `NotInstalled` / `NeedsPrepare` / `AuthRequired` / `Failed`, aucun processus ne tourne : `Start` peut être rappelé
sur le même objet.

## 3. États (`Session::phase()`)

`Idle` → `Starting` → `Ready` ⇄ `Prompting` → `Stopped` (après un échec de démarrage, la mort de l'agent ou `Dispose`).
Un seul tour à la fois : `Prompt` renvoie `false` hors de `Ready` (C4 garde sa file d'attente de messages).
`SetMode` / `SetConfigOption` passent en `Ready` et en `Prompting`.

## 4. Événements (`Acp::Event`)

| Type | Champs | Remarques |
| --- | --- | --- |
| `Started` | `started` | voir §2 |
| `MessageChunk`, `ThoughtChunk` | `text`, `messageId` | à concaténer ; texte de l'agent = non fiable, à rendre inerte |
| `ToolCall` | `tool` complet | `id`, `title`, `kind` (`read`/`edit`/`delete`/`move`/`search`/`execute`/`think`/`fetch`/`switch_mode`/`other`), `status`, `locations` (chemin + ligne ou −1), `diffs` (chemin, `oldText` si le fichier existait, `newText`), `text`, `rawInput` (JSON compact ≤ 64 Kio) |
| `ToolCallUpdate` | `tool` partiel | seuls les champs `has*` ont changé (`hasTitle`, `hasKind`, `hasStatus`, `hasContent` ⇒ `text` + `diffs`, `hasLocations`) |
| `Plan` | `plan` | remplace le plan précédent (contenu, priorité, statut) |
| `PermissionRequest` | `permission` | `requestId`, `toolCall` (titre, chemins, diff), `options` (`id`, `name`, `kind` = `allow_once`/`allow_always`/`reject_once`/`reject_always`). Si `permission.question` : question de l'agent (un choix dans une liste) — titre = la question, `options[0].id == "skip"` (libellé à prendre au catalogue : `name` est vide), les suivantes ont `kind == "answer"` et une `description` |
| `PermissionDismissed` | `permission.requestId` | la carte n'attend plus (tour annulé, agent mort) |
| `ModesChanged` | `modes` | `currentId` + `available` (id, nom, description) |
| `ConfigOptionsChanged` | `configOptions` (liste entière) | voir ci-dessous |
| `CommandsChanged` | `commands` | commandes `/` (nom, description, indice de saisie) |
| `Usage` | `usage` | jetons de contexte utilisés / taille de la fenêtre, coût si l'agent le donne |
| `SessionTitle` | `text` | titre de session proposé par l'agent |
| `Notice` | `text`, `severity` | ligne d'info de l'agent |
| `AuthStatus` | `text` | le compte que l'agent utilise lui-même, quand il le dit (les deux adaptateurs le font, peu après `initialize`) : `"none"` = **pas connecté**, sinon `account`, `api_key`, `gateway`, `external`. Seul ce mot est gardé (jamais l'e-mail) |
| `TurnEnded` | `stopReason` ou `error` | `end_turn`, `max_tokens`, `max_turn_requests`, `refusal`, `cancelled` ; sinon `error.code` (§6) |
| `RequestFailed` | `request`, `error` | `set_mode` ou `set_config_option` refusé / sans réponse |
| `Warning` | `text` | ligne illisible ignorée (`invalid_json: …`, `line_too_long`, `unexpected_message`) : journal seulement, 20 au plus par session |
| `Exited` | `exitCode`, `error` | l'agent est mort pendant une session ouverte (après un éventuel `TurnEnded` en erreur) |

**Modèles, efforts, modes : rien n'est écrit en dur.** `Acp::FindModelOption(options)`, `FindEffortOption`,
`FindModeOption` rendent l'option que l'agent range dans la catégorie `model`, `thought_level`, `mode` (ou `nullptr`).
Chaque option : `id`, `name`, `currentValue`, `choices` (`value`, `name`, `description`, `group` si l'agent groupe ses
modèles). Changer : `SetConfigOption(option.id, choice.value)` → `ConfigOptionsChanged` avec la liste **entière**
(changer de modèle peut changer les efforts proposés). Mode : `SetMode(mode.id)` → `ModesChanged`. Quand l'agent
expose le mode des deux façons, les deux restent synchronisés dans les événements.

**Annulation** : `Cancel()` répond « annulé » à toute demande d'autorisation en attente (un `PermissionDismissed`
chacune), envoie `session/cancel`, puis le tour finit par `TurnEnded{stopReason: "cancelled"}`. Des morceaux peuvent
encore arriver entre les deux.

**Bornes** : ligne JSON ≤ 32 Mio ; texte d'un événement ≤ 256 Kio ; un côté de diff ≤ 2 Mio (`diff.truncated`) ;
`Error::detail` ≤ 4 000 octets. Les images ne sont envoyées que si `capabilities.image` (sinon retirées du prompt).

## 5. Connexion

- `started.authMethods[i].terminal == false` → `session.Authenticate(id)` : relance l'agent avec le navigateur web
  autorisé (pas de `BROWSER=none`), envoie `authenticate` (5 min au plus), puis ouvre la session → nouveau `Started`.
- `terminal == true` (cas de Claude Code, voir §9) → `AgentRuntime::RunTerminalSignIn(env, agent, method)` ouvre une
  console visible qui lance la CLI de connexion de l'utilisateur (`claude auth login --claudeai`). `SignIn::Wait`
  (hors fil UI) rend la main quand la console se ferme ; ensuite `session.Start(o)` à nouveau.
- `AgentRuntime::PreferredSignIn(methods)` : la méthode à proposer en premier (compte/abonnement avant clé d'API).
- **Claude Code ne dit pas « pas connecté » à l'ouverture de session** (constaté, §9) : `Started` vaut `Ready` même
  déconnecté. Les trois signaux à traiter, du plus tôt au plus tard : `Detect().login == SignedOut` (avant tout
  lancement), l'événement `AuthStatus` avec `text == "none"` (1 à 2 s après `Start`), et `TurnEnded` avec
  `error.code == AuthRequired` (au premier message). Pour les deux premiers, il n'y a pas forcément de liste de
  méthodes : `AgentRuntime::DefaultTerminalSignIn(agent)` donne la connexion de la CLI elle-même
  (`claude auth login --claudeai`, `codex login`), à passer à `RunTerminalSignIn`. Codex, lui, répond bien
  `AuthRequired` à l'ouverture.
- Un `TurnEnded` avec `error.code == AuthRequired` = connexion expirée en cours de session : même parcours.
- Nous ne lisons ni ne stockons aucun jeton. `Detection::login` vient du code de sortie de `claude auth status` /
  `codex login status` (commandes officielles ; sortie jetée sans être lue).

## 6. Codes d'erreur → phrases (à mettre au catalogue par C4)

`Acp::ErrorCodeName(code)` donne l'identifiant stable. `error.detail` = les mots de l'agent (anglais, non fiables) : à
montrer en « détail », jamais comme titre.

| Code | Identifiant | Phrase suggérée (FR) | Action |
| --- | --- | --- | --- |
| `NotInstalled` | `not_installed` | « Claude Code n'est pas installé sur cet ordinateur. » | Copier la commande d'installation |
| `CliTooOld` | `cli_too_old` | « Votre version de Claude Code est trop ancienne pour le chat. » | Copier la commande de mise à jour |
| `NeedsPrepare` | `needs_prepare` | « Le chat a besoin de télécharger ses composants (une seule fois). » | Télécharger |
| `AuthRequired` | `auth_required` | « Vous n'êtes pas connecté à Claude Code. » | Se connecter |
| `SpawnFailed` | `spawn_failed` | « Impossible de démarrer l'agent. » | Réessayer |
| `ProcessExited` | `process_exited` | « L'agent s'est arrêté de façon inattendue. » | Réessayer (détail : fin de stderr) |
| `Timeout` | `timeout` | « L'agent ne répond pas. » | Réessayer |
| `Protocol` | `protocol` | « Cette version de l'agent n'est pas compatible avec le navigateur. » | Mettre à jour le navigateur |
| `AgentError` | `agent_error` | « L'agent a signalé une erreur. » | Réessayer (détail) |
| `ContextTooLong` | `context_too_long` | « Cette conversation est trop longue pour l'agent. » | Nouvelle conversation |
| `UsageLimit` | `usage_limit` | « Votre compte a atteint sa limite d'utilisation ou n'a plus de crédit. » | — |
| `Overloaded` | `overloaded` | « Les serveurs de l'agent sont occupés. Réessayez dans une minute. » | Réessayer |
| `RateLimited` | `rate_limited` | « Trop de demandes à la suite. Attendez une minute. » | Réessayer |
| `ServerError` | `server_error` | « Les serveurs de l'agent ont eu un problème. » | Réessayer |
| `Network` | `network` | « L'agent n'arrive pas à joindre ses serveurs. Vérifiez votre connexion. » | Réessayer |

`AgentRuntime::PrepareErrorName` : `cancelled`, `network` (« Téléchargement impossible. Vérifiez votre connexion. »),
`integrity` (« Le téléchargement ne correspond pas à l'empreinte attendue : rien n'a été installé. »), `disk`
(« Impossible d'écrire dans le dossier du navigateur. »), `archive`.

## 7. Processus et arrêt

- `CreateProcessW` suspendu, mis dans un **Job Object** `KILL_ON_JOB_CLOSE`, puis relancé : rien de ce que l'agent
  démarre (CLI, shells, serveurs MCP) n'échappe à l'arrêt. Seuls les trois tuyaux sont hérités (liste explicite).
  Console cachée (`CREATE_NO_WINDOW`).
- Environnement : celui de l'utilisateur **reconstruit depuis le registre** (`CreateEnvironmentBlock`), donc un `PATH`
  modifié après le lancement du navigateur est vu ; plus `CLAUDE_CODE_EXECUTABLE` / `CODEX_PATH`,
  `CLAUDE_AGENT_ACP_PROTOCOL=v1`, `BROWSER=none` (sauf pendant une connexion).
- `Dispose()` : ferme stdin (un agent bien élevé sort seul), attend 2 s, puis `TerminateJobObject`. Non bloquant ; plus
  aucun événement ensuite. Si le navigateur meurt, la fermeture du handle du job tue tout.
- Fils (vivants seulement pendant une session) : lecteur stdout, lecteur stderr (4 000 derniers octets), écrivain +
  délais. Un prompt de 3 Mio ne bloque jamais l'appelant (mesuré : `Prompt()` rend la main en ~16 ms pendant que
  l'agent ne lit pas).
- Délais (`StartOptions::timeouts`) : `initialize` 45 s, ouverture de session 60 s, `authenticate` 5 min, réglages
  30 s. Un tour n'a pas de délai.
- **Zéro coût au repos** : aucun fil, aucun processus, aucun téléchargement tant que `Start` / `Detect` / `Prepare`
  ne sont pas appelés. Arrêt après inactivité = `Dispose()` par C4, reprise = `Start` avec `resumeSessionId`.

## 8. Faux agent et tests

`tests/acp/fake_agent.cpp` → `fake-acp-agent.exe` (construit par `tools/release/build.py` dans `<out>-test/`, jamais
dans le paquet). Scénarios choisis par un mot dans le message : `reply` (défaut), `think`, `tool`, `permission`,
`plan`, `mode`, `slow`, `crash`, `question`, `refusal`, `max_tokens`, `error-rate`, `error-auth`, `big`, `echo`,
`garbage`, `commands`, `title`. Options : `--auth-file`, `--crash-at`, `--silent`, `--protocol`, `--no-images`,
`--resume`, `--garbage`, `--child`, `--read-delay`, `--log` (détail en tête du fichier).

**Dans le navigateur (bac à sable de C4)** : avec `BROWSER_ANNOTATIONS_TEST=1` et
`BROWSER_CHAT_FAKE_AGENT=<chemin de fake-acp-agent.exe> [options]`, `Environment::FromProcess` remplace les deux
agents par le faux (aucune CLI, aucun téléchargement). Ignoré dans une build `--channel release`.

**Le mot du scénario est cherché dans le DERNIER bloc de texte du prompt seulement** (le message de l'utilisateur) :
un préambule ou une description d'élément placés avant ne déclenchent jamais un scénario.

**Scénario `apply` : une vraie modification de fichier** (pour voir la page changer, « 1 fichier modifié », « Tout
annuler »). Le faux agent remplace réellement un texte dans un fichier du dossier de la session, comme l'outil Edit
d'un agent :

- réglage : `FAKE_AGENT_EDIT=chemin|chercher|remplacer` dans l'environnement du navigateur, ou l'option
  `--edit "chemin|chercher|remplacer"` dans `BROWSER_CHAT_FAKE_AGENT`, ou directement dans le message :
  `apply chemin|chercher|remplacer`. Le chemin est **relatif au dossier de la session** ;
- avec un réglage, **tout message qui ne nomme pas un autre scénario applique la modification** (on peut écrire une
  demande normale) ; le mot `apply` la déclenche aussi ; `reply` garde la réponse simple ;
- ce qui est émis : `tool_call` (`kind: edit`, `status: pending`, `locations` = chemin absolu + ligne du texte
  trouvé, `content` = diff ancien fichier / nouveau fichier, `rawInput` = `file_path`, `old_string`, `new_string`),
  puis en mode `default` (« Demander ») une `session/request_permission` avec le même diff et les options
  `allow` / `always` / `reject` ; après accord : `tool_call_update` `in_progress`, **écriture du fichier**,
  `tool_call_update` `completed` (titre « Modifié … », diff, ligne), un message, `usage_update`, `end_turn` ;
- modes : `acceptEdits` (« Auto ») écrit sans demander ; `always` fait passer la session en `acceptEdits` ; `plan`
  n'écrit rien (un `plan` et un message) ; refus → rien n'est écrit ; `Cancel` pendant la carte → `cancelled` ;
- **refusé si le chemin sort du dossier de la session** (`..`, chemin absolu, lettre de lecteur) : `tool_call`
  `failed` + message « apply: refused… », aucun fichier touché. Texte ou fichier introuvable : `failed` aussi.

Test : `apply: a real edit in the project` dans `tests/acp/acp_client_test.cpp` (contenu du fichier vérifié sur disque
avant/après, chemins hostiles, trois modes, variable d'environnement).

## 9. Ce que les vrais adaptateurs annoncent sous Windows (essai du 17/11/2026, sans aucun `session/prompt`)

Essai : `python -I tests/acp/run_tests.py --real-only` → `initialize` + `session/new` dans un dossier temporaire vide,
avec les CLI réelles de la machine, une fois « tel que l'utilisateur est connecté » et une fois avec le dossier de
configuration de la CLI redirigé vers un dossier vide (`CLAUDE_CONFIG_DIR` / `CODEX_HOME`, pour ce processus
seulement). **0 prompt envoyé** (`promptsSent: 0` dans le rapport `build/acp-test/real-adapters.json`).

| | Claude Code | Codex |
| --- | --- | --- |
| CLI trouvée | `%USERPROFILE%\.local\bin\claude.exe` 2.1.326 (PATH) | `%LOCALAPPDATA%\Programs\OpenAI\Codex\bin\codex.exe` 0.166.0 (PATH) |
| Adaptateur | `@agentclientprotocol/claude-agent-acp` 0.107.3, « Claude Agent » | `@agentclientprotocol/codex-acp` 2.6.0, « Codex » |
| Protocole | 1 | 1 |
| `Started` (connecté) | `Ready` en 1,9–2,7 s | `Ready` en 3,6 s |
| Capacités | images oui, audio non, contexte embarqué oui, `session/resume` **oui**, `session/load` oui, MCP http + sse | images oui, audio non, contexte embarqué oui, `session/resume` **oui**, `session/load` oui, MCP http (pas sse) |
| Méthodes de connexion | `claude-ai-login` « Claude Subscription » et `console-login` « Anthropic Console » : toutes deux **`terminal`** (`--cli auth login --claudeai` / `--console`) | `api-key` « API Key » et `chat-gpt` « ChatGPT » : **non terminal** (→ `Session::Authenticate("chat-gpt")`) |
| Déconnecté | `Started` = **`Ready` quand même** ; `AuthStatus` = `none` ~1 s après ; l'erreur n'arriverait qu'au premier message | `Started` = `AuthRequired` en 0,5 s, code −32000, « Authentication required » |
| Modes (`modes`) | `default` « Manual », `acceptEdits` « Accept edits », `plan` « Plan », `auto` « Auto », `bypassPermissions` « Bypass permissions ». **Mode de départ : `auto`** (réglage de l'utilisateur) | `read-only` « Read-only », `workspace-write` « Workspace access », `agent` « Auto review », `agent-full-access` « Full access ». **Mode de départ : `agent`** |
| Options (`configOptions`) | `mode` (catégorie `mode`), `model` (`model`) : Default, Opus 5.5, Fable 5.2, Sonnet 5.5, Haiku 5.5 + modèles du compte ; `effort` (`thought_level`) : Default, Low, Medium, High, Xhigh, Max ; `fast` et `ultracode` (`model_config`, On/Off) ; `agent` (sans catégorie) | `mode` (`mode`), `collaboration_mode` : Default / **Plan** ; `model` (`model`) : 6.2 Sol, 6.2 Astra, 6.1 Terra… ; `reasoning_effort` (`thought_level`) : Low … Ultra ; `fast-mode` (`model_config`) |
| Commandes `/` | 76 (dont les skills et plugins de l'utilisateur) | 135 |
| `AuthStatus` | `account` (connecté) / `none` | `account` |

Conséquences pour l'intégration :

1. **Le mode de départ n'est pas « Demander »** : c'est celui de la configuration de l'utilisateur. Juste après
   `Started`, C4 applique le mode voulu. `AgentRuntime::GetModeMap(agent)` donne la correspondance vérifiée ici :
   Claude — Demander `default`, Auto `auto` puis `acceptEdits`, Planifier = mode `plan`, jamais `bypassPermissions` ;
   Codex — Demander `read-only`, Auto `agent` puis `workspace-write`, Planifier = option `collaboration_mode` à
   `plan` (retour `default`), jamais `agent-full-access`. On ne propose que ce que la session annonce.
   **Codex en « Demander » (essai réel du lot 5, 24/11/2026, Codex 0.166.0, codex-acp 2.6.0).** Lu dans le code de
   l'adaptateur (`src/AgentMode.ts`) : `read-only` = bac à sable en lecture seule + approbation « à la demande »
   (« Requires approval to edit files ») ; `workspace-write` et `agent` = écriture dans le dossier **sans demander** ;
   `agent-full-access` = aucune limite. `read-only` est donc le seul mode où une modification attend l'accord de
   l'utilisateur : la correspondance « Demander » → `read-only` est la bonne et ne change pas. À l'essai réel, Codex n'a
   pourtant rien demandé : il a répondu que le projet était en lecture seule (aucune demande d'autorisation, aucun
   fichier modifié). Deux corrections : (a) le navigateur dit à l'agent qu'une modification qui attend un accord se
   demande, ne se refuse pas (`ChatPrompt::ApprovalRule`), en instructions ajoutées à celles de l'agent quand il les
   accepte (`systemPrompt`, mode `append`, annoncé par codex-acp dans `sessionCapabilities.systemPrompt` ; pour Codex
   ce sont ses « developer instructions ») et dans le préambule (version 4) ; (b) une annotation n'est « résolue » que si
   le tour a modifié un fichier (`ChatOptions::AnnotationOutcome`), sinon elle est rendue avec sa raison. Les demandes
   de Codex arrivent par `session/request_permission` : « Edit files » (`allow_once` « Yes, proceed »,
   `allow_for_session`, `cancel`), « Run command? », et « Additional sandbox permissions » (`allow_permissions_turn`,
   `allow_permissions_session`, `reject_permissions`). Le faux agent les imite avec `--codex-like`
   (`tests/acp/acp_client_test.cpp`, « Codex in « Demander » »). **Limite** : que le vrai Codex demande bien, une fois
   prévenu, ne se prouve que par un nouvel essai réel.
2. Modèles et efforts : `FindModelOption` / `FindEffortOption` trouvent bien `model` + `effort` (Claude) et `model` +
   `reasoning_effort` (Codex). Les options `model_config` (`fast`, `ultracode`, `fast-mode`) et `agent` existent ; les
   afficher est un choix de C4/C2.
3. `session/resume` est annoncé par les deux : la reprise après arrêt pour inactivité est possible (non essayée avec
   les vrais agents, pour ne rien consommer ; essayée sur le faux agent).
4. Les CLI écrivent leur propre état pendant l'essai (ce n'est pas nous) : `~/.claude/projects` et `plugins` ;
   `~/.codex` (journaux, caches, bases `sqlite`, `tmp`). Les trois fichiers de configuration surveillés sont
   inchangés (empreintes avant/après dans la sortie des tests).
5. La CLI Claude s'est mise à jour toute seule pendant le travail (2.1.325 → 2.1.326) : sans effet, la règle de
   version accepte les correctifs suivants de la même ligne.

## 10. Chemins, versions figées, mise à jour

Tout vit dans `<données du navigateur>\agents\` ; rien n'est écrit ailleurs, rien n'est installé dans le système.

```
<données>\agents\node\24.22.3\node.exe (+ LICENSE)                      90 Mo sur disque (38 Mo téléchargés)
<données>\agents\adapters\claude\0.107.3-470fc136\node_modules\…        5 paquets, 28 Mo, 1 635 fichiers
<données>\agents\adapters\codex\2.6.0-2a30ad61\node_modules\…           24 paquets, 21 Mo
<données>\agents\…\.download-<aléa>.zip, .unpack-<aléa>\                 temporaires (supprimés ; restes > 1 h nettoyés)
```

| Figé | Valeur | Vérification |
| --- | --- | --- |
| Node.js | 24.22.3 (LTS « Krypton »), `node-v24.22.3-win-x64.zip` de nodejs.org, 37,8 Mo | SHA-256 du zip, puis SHA-256 et taille du `node.exe` extrait (les deux viennent du `SHASUMS256.txt` officiel) |
| Adaptateur Claude | `@agentclientprotocol/claude-agent-acp` 0.107.3 + `@agentclientprotocol/sdk` 2.7.0, `@anthropic-ai/claude-agent-sdk` 0.3.325, `diff` 9.0.0, `zod` 4.6.7 | `sha512` de chaque archive (intégrité npm), puis nom et version du `package.json` extrait |
| Adaptateur Codex | `@agentclientprotocol/codex-acp` 2.6.0 + 23 dépendances | idem |
| CLI attendue | Claude : ligne 2.1 avec correctif ≥ 325 (version contre laquelle le SDK est construit) ; Codex ≥ 0.166.0 | `--version` ; plus ancien → `CliTooOld` ; ligne plus récente → accepté (`CliState::Untested`) |

**Méthode retenue pour les dépendances : archives pré-résolues, jamais npm.** L'arbre est résolu une fois, à la
fabrication (`tools/agents/pin-adapters.py`, qui lance `npm install --package-lock-only --ignore-scripts` dans un
dossier temporaire puis filtre le verrou) ; le navigateur télécharge exactement ces archives depuis
`registry.npmjs.org` en HTTPS (WinHTTP, proxy système, TLS ≥ 1.2, 3 essais sur erreur réseau), vérifie chaque
`sha512` et les décompresse lui-même. Pourquoi c'est plus sûr que « npm du Node privé + `--ignore-scripts` » :

- aucun programme tiers ne s'exécute pendant l'installation : aucun script de paquet possible, par construction ;
- npm lirait le `.npmrc` de l'utilisateur (autre registre, proxy, jetons) et écrirait dans son cache
  (`%LOCALAPPDATA%\npm-cache`) : contraire à « rien ailleurs, rien de modifié » ;
- chaque octet est comparé à une empreinte compilée dans `browser.exe` ; npm ferait confiance au verrou sur disque ;
- le décompresseur refuse tout ce qui n'est pas un fichier ou un dossier ordinaire sous la destination (liens,
  périphériques, `..`, chemins absolus, `:`, noms réservés Windows) : 25 archives hostiles dans les tests ;
- on ne garde de Node que `node.exe` : pas de npm ni de ses milliers de fichiers sur le disque.

Sont exclus de l'arbre : les paquets **optionnels** (les CLI de plateforme de 370 à 500 Mo, remplacées par la CLI de
l'utilisateur via `CLAUDE_CODE_EXECUTABLE` / `CODEX_PATH`) et les paquets **uniquement « peer »** (pour Claude :
`@anthropic-ai/sdk` et `@modelcontextprotocol/sdk`, une centaine de paquets dont express, que ni l'adaptateur ni le
SDK n'importent — vérifié en lisant leurs imports, et prouvé par l'essai réel du §9).

Installation atomique : tout est préparé dans `.unpack-<aléa>` à côté, vérifié, puis **renommé** vers le dossier
final. Un arrêt en plein milieu ne laisse jamais un dossier final incomplet ; une empreinte fausse ne laisse rien.

**Mettre à jour les versions** (une fois par version du navigateur qui en a besoin) :

1. changer `NODE_VERSION`, `CLAUDE_ACP`, `CODEX_ACP` en tête de `tools/agents/pin-adapters.py` ;
2. `python -I tools/agents/pin-adapters.py` → régénère `src/agent-runtime-pins.inc` (`--check` compare sans écrire) ;
3. `python -I tests/acp/run_tests.py --real` → téléchargements réels vérifiés + essai sans prompt des vrais
   adaptateurs ; relire le rapport et mettre à jour le §9 et, si besoin, `GetModeMap` ;
4. committer le `.inc` avec la version du navigateur. Les anciens dossiers sont retirés par
   `AgentRuntime::RemoveStale(env)` (à appeler quand aucun agent ne tourne).

Choix des versions d'aujourd'hui : l'adaptateur Claude 0.107.3 est le dernier correctif de la série 0.107 et son SDK
vise exactement la CLI 2.1.325 ; la 0.108.0, publiée quelques heures avant ce travail, a été écartée (trop récente
pour être figée). Codex 2.6.0 est la version qu'utilise Gui (2.7.0 avait un jour).

## 11. Tests et preuves

`python -I tests/acp/run_tests.py` (ajouter `--quick` pour sauter les ~120 Mo de téléchargements, `--real` pour
l'essai des vrais adaptateurs) :

- `acp_client_test` — 415 vérifications : classement des erreurs, bloc d'environnement, guillemets ; puis de bout en
  bout sur le faux agent : démarrage et capacités, morceaux, réflexion, outils avec `locations` et diff, plan,
  autorisation (accord, refus, annulation dans le bon ordre), questions, modes et options (Opus/Sonnet/Haiku,
  efforts qui changent avec le modèle, option booléenne), connexion (−32000, `authenticate`, méthode terminal
  refusée), crash au démarrage / en plein tour / pendant une carte, lignes illisibles, délai, version de protocole
  inconnue, réponse lente puis annulation, `Dispose` en plein tour, **Job Object** (l'enfant lancé par l'agent meurt,
  agent têtu tué après le délai de grâce), prompt de 3 Mio non bloquant, reprise de session, dossier avec accent et
  espace, scénario `apply` ;
- `agent_runtime_test` — ~5 600 vérifications : versions et table des empreintes, SHA-256 / SHA-512 / CRC-32,
  décompression (12 flux, coupures et altérations), zip, archives npm (3 dialectes tar + 25 hostiles ou cassées),
  détection des CLI sur de faux dossiers (PATH, installeurs, WinGet, npm sans jamais lancer le `.cmd`, CLI cassée,
  trop ancienne, plus récente, bavarde, bloquée), commande de lancement (`CLAUDE_CODE_EXECUTABLE` / `CODEX_PATH`,
  `BROWSER=none` sauf connexion), nettoyage, accroche du faux agent et connexion « terminal » ; puis
  **téléchargements réels** dans un dossier temporaire : empreintes, cache (2ᵉ appel : 0 ms), Codex réutilise le
  Node, empreintes altérées refusées sans rien laisser, annulation, serveur absent, `http://` refusé.

Chaque lancement finit par : aucun processus restant sous `build/acp-test`, et les empreintes SHA-256 inchangées de
`%APPDATA%\Claude\claude_desktop_config.json`, `~/.codex/config.toml`, `~/.claude/settings.json`.

## 12. Limites connues, à savoir

- **x64 seulement** (Node `win-x64`) ; sur Windows ARM64 il tourne en émulation. Pas de `win-arm64` figé.
- Proxy d'entreprise : les téléchargements passent par le proxy **système** (WinHTTP). L'agent lui-même (Node et la
  CLI) ne lit que `HTTPS_PROXY`, comme dans un terminal.
- `Detection::login` est un indice : avec une clé d'API en variable d'environnement ou un fournisseur cloud
  (Bedrock, Vertex), la commande de statut peut dire « déconnecté » alors que l'agent fonctionne. Ne jamais bloquer
  l'utilisateur sur ce seul indice : proposer la connexion, laisser « Essayer quand même ».
- CLI installée par npm : le binaire natif est pris derrière le shim (`…\@anthropic-ai\claude-code\bin\claude.exe`,
  `…\@openai\codex-win32-x64\vendor\x86_64-pc-windows-msvc\bin\codex.exe`) ; chemins déduits des paquets publiés et
  testés sur de faux dossiers, pas sur une vraie installation npm (cette machine a les installeurs natifs).
- Non essayé avec les vrais agents, par règle (aucun appel au modèle) : un tour complet, une autorisation réelle,
  `session/resume` réel, la connexion réelle dans une console. L'essai réel court revient à C4.
- Extensions hors ACP v1 stable non utilisées : messages ajoutés en cours de tour (`_session/steering`),
  sous-agents, `fs/*` et `terminal/*` (désactivés, comme Gui).
- `tools/release/build.py` : mes fichiers et le faux agent y ont été branchés par C4 (`CHAT_SOURCES`, `CHAT_LIBS`,
  `<out>-test/fake-acp-agent.exe`) ; j'y ai seulement ajouté `Gui-LICENSE.txt` / `Gui-NOTICE.txt` (code adapté de
  Gui, Apache-2.0 : en-têtes de `src/acp-client.cpp` et `src/agent-runtime.cpp`, `third_party/gui/`).
