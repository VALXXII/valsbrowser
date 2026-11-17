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

`python -I tests/acp/run_tests.py` construit et lance tout (voir §11).
