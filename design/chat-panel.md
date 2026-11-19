# Panneau de chat — page interne et protocole avec l'hôte (contrat B)

**En bref.** Le panneau de chat est une page web à nous (`assets/chat-panel/`), affichée dans une WebView2 interne à droite de la page. Il ne parle qu'à l'hôte (le navigateur, C4), par messages JSON. Il ne lit ni n'écrit aucun fichier, n'ouvre aucune connexion réseau, et n'exécute aucun texte reçu : tout ce qui vient de l'agent est affiché comme du texte.

- Propriétaire : C2. Consommateur : C4 (`src/chat-host.*`).
- Version du protocole : **1.9** (`"v": 1`). Ce document est la référence ; une évolution compatible (nouveaux messages ou champs facultatifs) garde `"v": 1` et prend un numéro de révision ici ; une rupture incrémente `v`. Historique en fin de document (§11).

---

## 1. Chargement

| Élément | Valeur |
|---|---|
| Page | `assets/chat-panel/index.html` (+ `panel.css`, `panel.js`, `markdown.js`, `icons.js`) |
| Origine conseillée | `https://chat.browser.internal/chat-panel/index.html`, servie par `WebResourceRequested`, toute autre requête refusée |
| Polices | La CSS charge `../fonts/InterDisplay-Regular.ttf`, `../fonts/InterDisplay-Medium.ttf`, `../fonts/JetBrainsMono-Regular.ttf` : l'hôte sert `assets/fonts/` sous `https://chat.browser.internal/fonts/` (mêmes fichiers que le reste de l'app) |
| CSP (balise `<meta>` dans la page, à doubler en en-tête par l'hôte) | `default-src 'none'; script-src 'self'; style-src 'self'; font-src 'self'; img-src data:; connect-src 'none'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'` |
| Exclu du build | `assets/chat-panel/dev-host.js` et `assets/chat-panel/dev.html` (faux hôte de QA). La page de production ne les référence pas. |
| Fond | `#171717` opaque (`--color-browser-canvas`, planches 6.x). `DefaultBackgroundColor` de la WebView : même couleur, pour éviter un flash blanc. |
| Taille | Conçu pour 380 DIP de large (planches), fonctionne de 320 à 560. Hauteur libre. En bulle *(1.8)* : 305 DIP de large, hauteur demandée par `bubble.size` (§19). |

Pas d'`eval`, pas de `new Function`, pas de `innerHTML` avec du contenu reçu : le Markdown de l'agent est analysé par `markdown.js` et construit avec `createElement` / `textContent`. Les liens ne sont jamais suivis par la page : un clic envoie `link.open` à l'hôte.

## 2. Transport

- Panneau → hôte : `window.chrome.webview.postMessage(objet)` (objet JSON, pas une chaîne).
- Hôte → panneau : `PostWebMessageAsJson(json)` ; le panneau écoute `window.chrome.webview.addEventListener('message', e => e.data)`.
- Chaque message est un objet `{ "v": 1, "type": "<type>", ...champs }`.
- Un message de `type` inconnu est **ignoré** des deux côtés (et journalisé côté hôte). Un champ inconnu est ignoré. Un champ requis manquant ou du mauvais type fait ignorer le message entier ; le panneau envoie alors `panel.log` niveau `warn`.
- `v` différent de 1 : le panneau affiche l'erreur « Le panneau n'est pas à jour » et n'envoie plus que `close`.

### Bornes (validées par l'hôte avec `strict-json.h`)

| Quoi | Borne |
|---|---|
| Message panneau → hôte (sauf `attachImage`) | 256 Kio |
| `attachImage` (image collée, base64) | 12 Mio de JSON ; PNG/JPEG/WebP/GIF seulement |
| Texte d'un `send` / `message.edit` | 100 000 caractères |
| Identifiants (`id`, `turnId`, `requestId`…) | 1 à 128 caractères `[A-Za-z0-9._:-]` |
| Message hôte → panneau | 4 Mio (une `thread.reset` d'historique long peut s'approcher : l'hôte découpe en `thread.reset` + `thread.append`) |
| Pièces jointes par message | 10 éléments visés + 10 images |
| Miniatures (`thumb`) | `data:image/png|jpeg|webp;base64,…`, 256 Kio ; toute autre URL est refusée par le panneau |

### Poignée de main

1. Le panneau charge et envoie `ready`.
2. L'hôte répond `hello`, puis `state` (complet), puis `thread.reset` si une conversation est ouverte.
3. Ensuite, flux libre. Le panneau ne suppose rien avant `state` : il affiche un squelette vide.

---

## 3. Types partagés

```ts
type Id = string;                      // [A-Za-z0-9._:-]{1,128}
type Iso = string;                     // date ISO 8601, ex. "2026-11-17T15:34:00+01:00"

type AgentStatus =
  | "ready"          // prêt
  | "starting"       // démarrage (Node / adaptateur / session)
  | "preparing"      // téléchargement de Node ou de l'adaptateur, avec progress
  | "notInstalled"   // CLI introuvable
  | "notConnected"   // CLI présente, pas connectée (authMethods)
  | "error";         // message clair dans detail

interface Agent {
  id: Id;                  // "claude-code" | "codex" | autre
  name: string;            // "Claude Code"
  icon: "claude" | "codex" | "generic";
  status: AgentStatus;
  detail?: string;         // "Installé", "Pas connecté", "Téléchargement 42 %"…
  progress?: number;       // 0..1 si preparing
  authMethods?: { id: Id; label: string }[];   // si notConnected
  models?: Option[];       // vide/absent si inconnu tant que l'agent n'a pas démarré
}

interface Option { id: Id; label: string; description?: string; isDefault?: boolean; group?: string; }

interface Mode extends Option {
  kind: "edits" | "auto" | "ask" | "plan" | "other";   // (1.9) double chevron / éclair / bouclier / liste ; "other" n'est jamais montré (§20.4)
}

interface ElementRef {           // « puce » d'élément visé
  id: Id;
  label: string;                 // "button.btn-primary"
  kind: ElementKind;             // icône (planche 6.13)
  selector?: string;
  source?: string;               // "Checkout.tsx:42"
  text?: string;                 // texte visible, ≤ 120 car.
  hint?: string;                 // à droite dans le menu @ : "Payer 48,00 €"
  thumb?: string;                // data:image/… miniature
}
type ElementKind = "button" | "link" | "text" | "heading" | "image" | "video" | "icon"
                 | "input" | "select" | "checkbox" | "form" | "list" | "table" | "block";

interface Attachment { id: Id; kind: "image"; name: string; thumb: string; }   // thumb en data:

interface FileChange {
  path: string;                 // relatif au dossier du projet
  status: "modified" | "created" | "deleted";
  added: number; removed: number;
  diff?: Diff;                  // facultatif (l'hôte le met sur les premiers fichiers) : diff sous la barre dépliée quand un seul
                                // fichier a changé (6.6), et contenu immédiat de la vue « Voir les changements » avant changes.diff
}

interface Diff {                // aperçu d'un changement
  path: string;
  added: number; removed: number;
  lines: { op: "+" | "-" | " " | "@"; text: string }[];   // ≤ 400 lignes, l'hôte tronque
  truncated?: boolean;
}

interface Action { id: Id; label: string; primary?: boolean; }
```

---

## 4. Hôte → panneau

| `type` | Champs | Effet dans le panneau |
|---|---|---|
| `layout` *(1.8)* | `mode` (`"panel"` / `"bubble"`), `maxWidth?`, `maxHeight?` (px CSS), `side?`, `element?`, `open?`, `inset?` *(1.9)* `{top, right, bottom, left}` | Disposition de la page : panneau à droite (défaut) ou **bulle compacte** du mode Commentaire (§19). Envoyé à l'ouverture et à chaque bascule. En bulle, le panneau répond `bubble.size`. |
| `hello` | `v`, `locale` (`"fr"`), `platform` (`"windows"`), `reducedMotion?` (bool, si Windows le demande) | Initialise. `reducedMotion` s'ajoute à `prefers-reduced-motion`. |
| `state` | voir §4.1 | Fusion **par clé de premier niveau** : une clé absente garde sa valeur, `null` l'efface. |
| `thread.reset` | `conversationId`, `items: Item[]` (§4.2) | Remplace tout le fil (ouverture d'une conversation, nouveau chat = `items: []`). |
| `thread.append` | `items: Item[]` | Ajoute des éléments déjà terminés (suite d'un historique découpé). |
| `message.user` | `id`, `text`, `elements?: ElementRef[]`, `attachments?: Attachment[]`, `at: Iso`, `editable?` (déf. vrai), `annotations?` *(1.4)* `{ batchId, items: AnnotationItem[], instruction? }` | Bulle de l'utilisateur, puces dans le texte, date centrée si nouveau jour ou pause > 30 min. |
| `turn.start` | `turnId`, `model` (libellé), `effort?` (libellé), `agentId`, `at: Iso` | Ouvre un tour : « Travaille… 0 s » démarre, chronomètre local. Si modèle/effort diffère du tour précédent : repère « Passé à Opus 5.5 · Élevé ». |
| `message.agentChunk` | `turnId`, `messageId`, `text` (morceau à ajouter, Markdown) | Texte en flux. Rendu par paragraphe, sans animation par mot ; annoncé aux lecteurs d'écran par phrase terminée. |
| `thought` | `turnId`, `id`, `text` (morceau à ajouter), `done?`, `durationMs?` | Ligne repliée « A réfléchi 6 s ›» (pendant : « Réfléchit… »). Clic : détail en texte atténué. |
| `toolCall` | `turnId`, `id`, `title`, `kind` (`read`/`edit`/`delete`/`move`/`search`/`execute`/`think`/`fetch`/`other`), `status` (`pending`/`in_progress`/`completed`/`failed`), `locations?: {path,line?}[]`, `diff?: Diff`, `summary?` *(1.3)* `{ verb, object, after?, afterMany?, count?, one?, many? }` | Étape. Les étapes consécutives terminées se regroupent en une ligne repliée (règles §14 : « A lu 2 annotations et landing.css, a marqué l'annotation « Bouton » comme terminée › »). L'étape en cours s'affiche seule avec un indicateur. Échec : « (échec) » en rouge dans la ligne. |
| `toolCallUpdate` | `id`, et n'importe lequel de `title`, `status`, `locations`, `diff`, `summary` *(1.3)* | Met à jour l'étape. |
| `plan` | `turnId`, `id`, `entries: {content, status: "pending"|"in_progress"|"completed"}[]`, `proposed?` | `proposed: true` (mode Planifier, attend le feu vert) : carte « Plan proposé · N étapes » avec **Lancer le plan** / **Modifier**. Sinon liste de suivi mise à jour en place. |
| `permission.request` | `requestId`, `toolCallId?`, `title` (« Claude veut modifier checkout.css » : **phrase humaine**, verbe d'abord, ce qui va se passer et sur quoi ; jamais un nom technique), `kind`, `diff?: Diff`, `command?` (pour `execute`), `tool?` *(1.2)* `{ name, label?, server?, serverLabel? }` (outil appelé : `name` brut, ex. `mcp__browser-annotations-preview__browser_annotations_list` ; `label` « Lister les annotations » et `serverLabel` « Annotations du navigateur » si l'hôte les connaît), `input?` *(1.2)* (arguments bruts de l'outil, texte JSON ≤ 4 000 caractères), `options: {optionId, name, kind: "allow_once"|"allow_always"|"reject_once"|"reject_always"}[]` | **La carte remplace la saisie.** Focus sur « Autoriser ». Annonce assertive. Plusieurs demandes : file, une à la fois, « 1 sur 2 ». |
| `permission.cancel` | `requestId` | Retire la carte (tour annulé par l'agent). |
| `turn.end` | `turnId`, `stopReason` (`end_turn`/`cancelled`/`refusal`/`max_tokens`/`error`), `durationMs`, `changes?: FileChange[]`, `checkpointId?`, `undoable?`, `copyText?` | Ferme le tour : infos du tour (« Sonnet · Élevé · 18 s »), barre « ✓ 1 fichier modifié +6 −2 ⌄ » avec Tout annuler, point de reprise, actions au survol (Copier, Relancer). `cancelled` : ligne « Arrêté ». `refusal` / `max_tokens` : ligne d'explication. |
| `turn.undone` | `turnId`, `ok`, `conflicts?: string[]`, `message?` | Après `undo.turn` ou `checkpoint.restore` : la barre des changements passe à « Annulé », ou affiche le conflit nommé (rien n'a été écrasé). |
| `error` | `id`, `scope` (`thread`/`composer`), `title`, `message`, `detail?`, `actions?: Action[]`, `turnId?` | `thread` : carte rouge dans le fil (6.7 D), la demande est gardée. Bouton « Voir le détail » local si `detail`. Les actions renvoient `error.action`. |
| `strip` | `id`, `tone` (`danger`/`neutral`/`info`), `text`, `action?: Action` ; ou `id` + `clear: true` | Bandeau collé au-dessus de la saisie (6.5 états 7 et 8 : déconnecté, limite atteinte, mémoire pleine…). `danger` désactive la saisie. |
| `queue` | `items: {id, text, after?}[]` | Suites en attente pendant un tour : « En file · Et fais pareil pour le titre · Envoyer maintenant · × ». *(1.8)* `after` : libellé de l'élément sur lequel l'agent travaille déjà (file d'attente entre éléments, §19.6). |
| `history` | `items: HistoryItem[]`, `query?` | Liste de l'historique (6.7 B). `HistoryItem = {id, title, subtitle?, siteLabel, folderName?, thisSite: bool, agentIcon, updatedAt: Iso, running?: bool, current?: bool}`. *(1.8)* `folderName` (nom du dossier du projet, « boutique ») s'affiche à la place de `siteLabel` quand il est fourni (§19.8). |
| `element.attached` | `element: ElementRef` | Ajoute la puce dans la saisie (clic dans la page, panneau ouvert, ou `@`). *(1.5)* L'hôte envoie ensuite `focus` : le curseur de texte va dans la saisie. |
| `attachment.added` | `attachment: Attachment` | Ajoute la miniature dans la saisie. |
| `pick.state` | `active: bool` | Bouton viseur enfoncé ou non. *(1.5)* Toujours vrai tant que le panneau est ouvert sur un site local, tour en cours compris : un clic dans la page va au chat. |
| `mentions` | `query`, `elements: ElementRef[]`, `files: {path, dir}[]` | Réponse à `mention.query` (menu « @ »). |
| `commands` | `items: {name, description}[]` | Menu « / » (vient de l'agent ; liste vide = menu masqué). |
| `edit.confirm` | `messageId`, `files: string[]` | « Des fichiers ont changé depuis ce message » : **Restaurer et renvoyer** / **Renvoyer sans restaurer** / Annuler. |
| `focus` | `target: "composer"` | Met le focus dans la saisie (ouverture du panneau). |
| `annotations.batch` *(1.4)* | `batchId`, `siteLabel`, `items: AnnotationItem[]` (≤ 50 ; `items: []` retire le bloc) | Lot « N annotations à traiter » posé dans la saisie (toutes cochées). Arrive de la suggestion du chat vide (`suggestion.run {id:"annotations"}`), de la commande `/annotations` (`annotations.request`) ou de « Traiter avec l'agent » dans l'historique (côté hôte). Remplace un lot précédent. |
| `annotations.status` *(1.4)* | `batchId`, `id`, `status` (`pending`/`working`/`done`/`skipped`), `summary?` (une phrase de l'agent, si `done`), `reason?` (si `skipped`) | Met à jour une ligne de la carte « Traiter N annotations » du fil (et l'en-tête « 1 sur N » / « ✓ N terminées »). Annonce polie. |
| `copy.done` | `ok` | Le bouton Copier affiche « Copié » 1,5 s (ou rien si `ok` faux). |
| `usage` *(1.2)* | `used`, `size` (jetons de contexte utilisés / taille de la fenêtre, depuis `usage_update` ACP) | Jauge de contexte à droite du titre : anneau rempli à `used/size`, neutre, ambre au-delà de 80 % ; infobulle « 62 % de la mémoire du chat utilisée · se résume automatiquement vers 100 % ». `size: 0` (ou nouvelle conversation, `thread.reset`) la masque ; l'hôte renvoie `usage` après un `thread.reset` s'il le connaît. |
| `annotation.kept` *(1.6)* | `id`, `text`, `elements: ElementRef[]`, `annotations: Id[]` (ids du magasin), `at: Iso`, `partial?` | Ligne discrète dans le fil, côté utilisateur : les mots, « Gardé en annotation » + les puces, « Pour vos agents connectés · rien n'est envoyé au chat ». Aussi un `Item` (rejouée avec la conversation). |
| `pointer.leave` *(1.5)* | — | Le pointeur est sorti du panneau (vu par l'hôte, voir `hover`) : l'infobulle ouverte se ferme. |
| `changes.diff` *(1.1)* | `turnId`, `diffs: Diff[]` (tous les fichiers du tour, ≤ 4 000 lignes chacun), `files?: FileChange[]` | Réponse à `checkpoint.viewChanges`. Remplit la vue « Voir les changements » si elle est ouverte sur ce tour (sinon gardé pour la prochaine ouverture). Un fichier sans entrée dans `diffs` affiche « Pas d'aperçu pour ce fichier ». |

### 4.1 `state`

```ts
interface State {
  site?: { origin: string; label: string; local: boolean };      // label : "127.0.0.1:5173"
  folder?: { path: string; display: string; name: string } | null;   // null : première fois (6.2)
  folderSuggestion?: { path: string; display: string; name: string; reason: "auto" } | null;
  askFolder?: boolean;             // (1.4) true : afficher « Où est le code de ce site ? » (détection incertaine).
                                   // Absent (hôte < 1.4) : l'écran s'affiche quand folder === null, comme avant.
  agents?: Agent[];                // tous les agents connus (6.2, menu modèle)
  agentId?: Id;                    // agent de la conversation
  model?: Id | null;  efforts?: Option[];  effort?: Id | null;
  modes?: Mode[];     mode?: Id | null;
  conversation?: { id: Id; title: string; source?: string; running?: boolean } | null;
  running?: boolean;               // un tour est en cours (bouton Arrêter, placeholder « Ajouter une suite… »)
  pendingAnnotations?: number;     // suggestion « Traiter les 2 annotations en attente »
  suggestions?: { id: Id; label: string; badge?: string }[];
  capabilities?: { image: boolean; steering: boolean; commands: boolean };
  disabledReason?: string | null;  // site non local : saisie désactivée avec cette explication
}
```

Les modèles affichés sont ceux de l'agent courant (`agents[agentId].models`) puis ceux des autres agents, séparés par un trait. Au-delà de 4 modèles par agent : « Autres modèles › ». Rien n'est écrit en dur dans le panneau.

### 4.2 `Item` (rejeu d'une conversation)

Un `Item` est un des messages hôte → panneau ci-dessus, déjà terminé : `message.user`, `turn.start`, `message.agentChunk` (texte complet), `thought` (`done: true`), `toolCall`, `plan`, `turn.end`, `error`. Le panneau les applique dans l'ordre sans animation.

---

## 5. Panneau → hôte

| `type` | Champs | Quand |
|---|---|---|
| `ready` | `v`, `build` | Page chargée. |
| `send` | `text`, `elements: Id[]`, `attachments: Id[]`, `annotations?` *(1.4)* `{ batchId, ids: Id[] }` (lignes cochées, dans l'ordre ; `text` est alors la précision, peut être vide) | Entrée dans la saisie. Pendant un tour : l'hôte met en file (`queue`) ou envoie en pilotage si l'agent le permet. **`text` contient le libellé de chaque puce à sa place** (« … comparé à button.btn-primary dans le canvas ») : c'est voulu, l'hôte l'utilise tel quel comme texte de l'utilisateur et décrit les éléments à part (contrat D) ; `elements` les liste dans l'ordre d'apparition. Le panneau, à l'écho `message.user`, replace la puce à la première occurrence du libellé. |
| `stop` | — | Bouton Arrêter ou Échap pendant un tour. |
| `queue.sendNow` | `id` | « Envoyer maintenant ». |
| `queue.remove` | `id` | « × » sur une suite en file. |
| `permission.respond` | `requestId`, `optionId`, `message?` | Autoriser (↵) / Refuser (Échap) / « Toujours autoriser » coché = l'option `allow_always`. `message` : texte de « Dire à Claude quoi faire à la place… » (avec l'option `reject_once`, l'hôte envoie ensuite le texte comme suite). |
| `setModel` | `id`, `agentId` | Choix d'un modèle de l'agent courant dans le menu modèle (`agentId` = agent courant ; une valeur différente reste acceptée : l'hôte démarre alors une nouvelle conversation). |
| `setEffort` | `id` | Sous-menu effort. |
| `setMode` | `id` | Menu mode ou Maj+Tab. |
| `setAgent` | `id` | Première fois (6.2), ou « Autres modèles › Autres agents » dans le menu modèle *(1.2)* : la ligne dit déjà « Ouvre un nouveau chat » ; après la première fois, l'hôte démarre une nouvelle conversation avec cet agent (l'ancienne reste dans l'historique). |
| `agent.connect` | `agentId`, `methodId?` | « Se connecter » (bandeau ou carte). |
| `agent.prepare` | `agentId` | « Installer » / « Réessayer » quand l'agent n'est pas prêt. |
| `pickElement` | `active: bool` | Bouton viseur. L'hôte répond `pick.state`. *(1.7)* C'est l'interrupteur de la sélection dans la page : `false` la coupe (plus de curseur viseur, de survol ni de cadre ; la page reçoit de nouveau ses clics), le panneau et sa conversation restent ; `true` la rallume. |
| `element.remove` | `id` | « × » sur une puce. |
| `attachImage` | `source: "picker"` ou `source: "paste"` + `name`, `mime`, `data` (base64) | « + › Joindre une image » ou collage. L'hôte répond `attachment.added`. |
| `attachment.remove` | `id` | « × » sur une miniature. |
| `mention.query` | `query` | Frappe après « @ » (débit limité à 1 / 80 ms). |
| `folder.use` | `path` | « Utiliser » sur le dossier détecté. |
| `folder.choose` | — | « Choisir un autre dossier… » (écran du dossier) ou « + › Choisir le dossier… » — ce dernier seulement quand le navigateur n'est pas sûr du dossier (`askFolder` vrai ou pas de dossier) *(1.9)* ; sélecteur Windows côté hôte. La puce dossier de la 1.4 n'existe plus *(1.5)*. |
| `annotations.request` *(1.4)* | — | Commande `/annotations` choisie dans le menu « / » : l'hôte répond `annotations.batch` (ou une `strip` « Aucune annotation en attente »). |
| `annotation.show` *(1.4)* | `id` | Clic sur une ligne de la carte « Traiter N annotations » : montrer l'annotation sur la page. |
| `undo.turn` | `turnId` | « Tout annuler » dans la barre des changements. |
| `checkpoint.restore` | `checkpointId` | « Revenir » sur un point de reprise. |
| `checkpoint.viewChanges` | `turnId`, `path?` | « Voir les changements » (barre dépliée) ou clic sur un fichier (`path`). Le panneau ouvre aussitôt sa vue avec les `FileChange.diff` déjà reçus, puis attend `changes.diff` (1.1). |
| `plan.run` | `planId` | « Lancer le plan ». |
| `plan.edit` | `planId` | « Modifier » (le panneau met aussi le focus dans la saisie, placeholder « Ajouter une précision au plan… »). |
| `history.open` | — | Ouverture de l'historique (bouton Historique de l'en-tête ; le titre ne l'ouvre plus *(1.9)*). L'hôte répond `history`. |
| `conversation.rename` *(1.9)* | `id`, `title` (1 à 120 caractères, une ligne) | Double-clic (ou F2) sur le titre de l'en-tête, Entrée (§20.5). L'hôte ramène le titre sur une ligne, l'enregistre, puis renvoie `state` (`conversation.title`) et `history` si l'historique est ouvert ; hors bornes : ignoré. |
| `history.search` | `query` | Champ « Rechercher une conversation » (débit limité). |
| `history.select` | `id` | Clic sur une conversation. L'hôte répond `state` + `thread.reset`. |
| `newChat` | — | Bouton « + » de l'en-tête ou « Nouveau » de l'historique. |
| `message.edit` | `messageId`, `text`, `restore?: bool` | Modifier un message. Sans `restore` : l'hôte peut répondre `edit.confirm`. |
| `retry` | `turnId` | Relancer (survol) ou « Réessayer » d'une erreur. |
| `error.action` | `errorId`, `actionId` | Bouton d'une carte d'erreur. |
| `strip.action` | `stripId`, `actionId` | Bouton d'un bandeau (« Se connecter », « Passer à Codex »). |
| `suggestion.run` | `id` | Suggestion du chat vide. |
| `copy` | `text` | Copier (code, réponse, message). L'hôte écrit le presse-papiers et répond `copy.done`. |
| `link.open` | `url` | Lien dans une réponse. L'hôte vérifie (http/https/file local) et ouvre un onglet. |
| `file.open` | `path`, `line?` | Clic sur « Checkout.tsx:42 » ou un nom de fichier. Facultatif pour l'hôte. |
| `close` | — | Bouton fermer, ou Échap quand rien d'autre n'est ouvert. *(1.8)* En bulle : Échap ferme la bulle **sans arrêter l'agent** et sans répondre à une demande d'autorisation (§19.4). |
| `bubble.size` *(1.8)* | `width`, `height` (px CSS, entiers), `overflowTop?`, `overflowRight?`, `overflowBottom?`, `overflowLeft?` *(1.9, entiers 0 à 400)* | En disposition bulle seulement : la taille que veut la bulle, bornée par `layout.maxWidth` / `maxHeight`. Envoyé après chaque `layout`, puis à chaque changement de contenu (jamais deux fois de suite la même valeur). L'hôte redimensionne la fenêtre. |
| `panel.log` | `level` (`info`/`warn`/`error`), `message` | Diagnostic (message invalide, exception). Borné à 2 Kio. |
| `annotate` *(1.6)* | `text`, `elements: Id[]` | `Ctrl+Entrée` dans la saisie : les puces et les mots deviennent des annotations **ouvertes** du magasin (lues par les agents MCP), rien n'est envoyé à l'agent du chat. Il faut au moins une puce et quelques mots (sinon un bandeau bref dit ce qui manque). L'hôte répond `annotation.kept`, ou une `error` `composer` si l'écriture échoue. |
| `hover` *(1.5)* | `active: bool` | Une infobulle de survol est ouverte (`true`) ou fermée (`false`). Tant qu'elle l'est, l'hôte compare le vrai pointeur au rectangle du panneau (toutes les 80 ms) et répond `pointer.leave` dès qu'il en sort. Rien ne tourne sinon. |

---

## 6. Règles de comportement liées au protocole

- **Défilement.** Le fil suit le texte tant que l'utilisateur est en bas (à 24 px près). Dès qu'il remonte, il ne bouge plus et le bouton « ↓ N nouvelles étapes » apparaît (N = étapes et messages arrivés depuis).
- **Autorisation.** Pendant qu'une carte est affichée, la saisie est remplacée ; Échap = Refuser (option `reject_once`), Entrée = Autoriser (`allow_always` si la case est cochée, sinon `allow_once`). Le champ « Dire à Claude quoi faire à la place… » envoie l'option `reject_once` avec `message`.
- **Échap**, dans l'ordre : ferme le menu ouvert → quitte la modification d'un message → Refuser la demande d'autorisation → `stop` si un tour est en cours → `close`. En bulle *(1.8)* : menu → diff en grand → `close`, rien d'autre (§19.4).
- **Clavier** : Entrée envoie, Maj+Entrée nouvelle ligne, Ctrl+/ ouvre le menu modèle, Maj+Tab change de mode (dans la saisie), flèches + Entrée dans les menus, `@` et `/` en début de mot ouvrent leurs menus.
- **Rien n'est déduit en dur** : modèles, efforts, modes, commandes et agents viennent de `state` / `commands`.
- **Accessibilité** : fil `role="log"` (`aria-live="polite"`, annonces par phrase), carte d'autorisation `role="alertdialog"` (annonce assertive), menus `role="menu"`/`listbox`, tous les boutons nommés, ordre Tab : en-tête → fil → bandeau → saisie.

## 7. Faux hôte (QA)

`assets/chat-panel/dev.html` charge la page de production plus `dev-host.js`, qui imite `window.chrome.webview` et rejoue des scénarios (`?scenario=6.1-autorisation`, `6.2-premiere-fois`, … liste dans `dev-host.js`, mode d'emploi dans `tests/chat-panel/README.md`). Ni `dev.html` ni `dev-host.js` ne sont copiés dans le build.

**À embarquer par C4** (et rien d'autre) : `assets/chat-panel/index.html`, `panel.css`, `panel.js`, `markdown.js`, `icons.js`, plus `assets/fonts/InterDisplay-Regular.ttf`, `InterDisplay-Medium.ttf`, `JetBrainsMono-Regular.ttf` (servis sous `/fonts/`).

## 8. Ce que fait le panneau (résumé pour l'intégration)

| Zone | Comportement |
|---|---|
| En-tête | Logo de l'agent, titre (double-clic ou F2 : le renommer en place, §20.5 ; il n'ouvre plus l'historique), source `Checkout.tsx:42` au-dessus si `conversation.source` (en-tête 52 px avec filet), indicateur « en cours », Nouveau / Historique / Fermer. |
| Fil | Date centrée (nouveau jour ou pause > 30 min) ; bulle de l'utilisateur avec puces d'élément à l'endroit de leur libellé, les autres puces et images sous la bulle ; crayon au survol (Modifier) ; « Revenir » au survol quand le tour a un point de reprise. Tour : infos (modèle · effort · durée), réflexion repliée, texte en flux (Markdown sûr, curseur bleu), étapes (une par ligne pendant le tour, résumé repliable ensuite, « (échec) » en rouge), plan, attente d'accord, « Travaille… N s », barre des changements (+/−, détail, Tout annuler), Copier / Relancer au survol ou au clavier, repère « Passé à … ». |
| Bas | Bandeau collé à la saisie (hôte ou état de l'agent : pas connecté, pas installé, préparation avec progression, erreur, site non local), suites en file, saisie (puces, images, 6 lignes puis défilement, raccourcis affichés en multi-ligne) ; la carte d'autorisation remplace le tout. |
| Menus | Modèle (agent courant puis autres agents ; agent pas prêt avec son action ; confirmation « Passer à Codex ouvre un nouveau chat »), Effort, Mode, « + » (image, dossier), « @ » (éléments et fichiers), « / » (commandes de l'agent). |
| Vues | Première fois (dossier détecté, autre dossier, choix de l'agent), chat vide (suggestions), historique (recherche, ce site / autres sites, en cours). |

Mouvement : entrées de 200 ms (`translateY(6px)` + opacité, courbe ease-out forte), menus 150 ms depuis leur bouton, pression 0,96 ; tout passe en simple fondu, sans reflet animé, sous `prefers-reduced-motion` ou `hello.reducedMotion`. Les éléments rejoués (`thread.reset`) n'animent pas.

Performance : un fil long garde `content-visibility: auto` sur chaque élément ; le texte en flux est regroupé par image (`requestAnimationFrame`) et seul le dernier bloc Markdown est reconstruit.

## 9. Écarts assumés par rapport aux planches

1. **Graisse 600** : l'app n'embarque qu'Inter Display Regular et Medium ; les titres en 600 des planches utilisent Medium (comme les surfaces natives).
2. **Barre des changements** : les planches mettent « Copier » dans la barre ; la consigne demande « Tout annuler ». La barre porte « Tout annuler » ; Copier reste sur chaque bloc de code et dans les actions de fin de tour. Dépliée, elle liste les fichiers (et le diff si l'hôte fournit `FileChange.diff`).
3. **Raccourcis visibles** sur la carte d'autorisation (« Refuser Échap », « Autoriser ↵ »), demandés par la consigne, absents de la planche 6.1.
4. **Puce dans la bulle envoyée** : cadre bleu + icône « viseur » (planches 6.10, 6.11, la plus récente série) ; dans la saisie, icône du type d'élément sans cadre (6.5, 6.9, 6.13). Les planches 6.1/6.3/6.6 montrent l'autre variante.
5. **Date et infos du tour** affichées dès le premier message (règle de 6.6) ; les planches 6.1 et 6.3 ne les montrent pas.
6. **Étape en cours** : suivie de 6.10 (ligne de l'étape avec indicateur, « Travaille… N s » en dessous) plutôt que 6.6 (indicateur sur « Travaille »).
7. **Bord de la saisie** : 1 px `#FFFFFF1F` partout (6.5, planche des états) ; les écrans 6.10 à 6.12 n'en ont pas.
8. **Historique** : s'ouvre sous l'en-tête, qui reste visible (titre ⌄ ↔ ⌃) ; la planche 6.7 B montre la liste seule.
9. **Sous-menu Effort** : s'ouvre à la place du menu modèle (même position que le bouton « Élevé ⌄ ») ; à 380 px, côte à côte comme sur 6.4 ne tient pas.
10. **« @butt »** n'est pas coloré en bleu dans la saisie (texte brut) ; la puce, elle, l'est une fois l'élément choisi.
11. **9ᵉ état de la saisie** (6.5 n'en dessine que 8) : site non local, bandeau d'explication et saisie désactivée (décision produit 2).
12. **Menu « + »** : absent des planches, au style des menus 6.4 : « Joindre une image », et « Choisir le dossier… » seulement quand le navigateur n'est pas sûr du dossier (1.9, §20.6).

Écart moyen mesuré (canal, sur 255) entre planche et panneau : 6.2 : 3,6 · 6.7 A : 7,0 · 6.8 : 7,3 · 6.9 : 8,4 · 6.7 B : 9,8 · 6.10 : 11,6 · 6.1 : 13,1 · 6.3 : 14,6 · 6.11 : 15,8 · 6.12 : 17,9 · 6.6 : 18,5. Les plus forts viennent des écarts 4 et 5 (contenu décalé de la hauteur d'une ligne), pas du dessin des composants.

## 10. Tests

`python -I tests/chat-panel/run.py` — 526 contrôles (statique, scénarios du panneau et 33 du mode Commentaire, clavier, sécurité, polices, performance, fidélité valeur par valeur). Détails : `tests/chat-panel/README.md`. Sortie : `build/chat-qa/panel/` (panneau) et `build/chat-qa/comment-v2/panel/` (bulle du mode Commentaire, comparée aux planches `tests/chat-panel/reference/comment/`, tableau d'écarts `ecarts.md`).

## 11. Historique du contrat

- **1.9** (18 nov. 2026, compatible, `"v": 1`) : retours du premier test réel du Mode Commentaire (§20). `bubble.size` gagne `overflowTop/Right/Bottom/Left` et `layout` gagne `inset` (menus et infobulles hors de la bulle) ; accusé de réception seulement pour un `layout` d'ouverture ; `Mode.kind` `"edits"` et les 4 modes exacts de Paper ; `conversation.rename` ; plus de dossier dans « + » quand il est sûr ; pas de puce sous le message en bulle ; l'étape en cours à la place du message tant que l'agent n'a rien écrit ; logo du fournisseur dans l'en-tête des réponses ; une demande d'autorisation renvoyée après `thread.reset` réapparaît (une seule fois).
- **1.8** (18 nov. 2026, compatible, `"v": 1`) : Mode Commentaire. `layout {mode, maxWidth, maxHeight}` (bulle compacte ou panneau) et `bubble.size {width, height}` ; Échap en bulle = `close` sans arrêter l'agent ; `queue.items[].after` (file d'attente entre éléments) ; `HistoryItem.folderName` (nom du dossier plutôt que l'adresse) (§19).
- **1.7** (18 nov. 2026, compatible, `"v": 1`) : `pickElement` / `pick.state` = interrupteur de la sélection, le chat restant ouvert ; noms de modes en français fournis par l'hôte (§18).
- **1.6** (18 nov. 2026, compatible, `"v": 1`) : `annotate` / `annotation.kept` (Ctrl+Entrée garde en annotation) ; infobulle du bouton envoyer (§17).
- **1.5** (18 nov. 2026, compatible, `"v": 1`) : retours du test réel (§16). `hover` / `pointer.leave` (infobulle de la jauge qui se ferme toujours) ; plus de puce dossier dans la saisie ; menu modèle par fournisseur connecté, efforts sans « Default » ; `pick.state` vrai pendant un tour (un clic va toujours au chat quand il est ouvert).
- **1.4** (18 nov. 2026, compatible, `"v": 1`) : `state.askFolder` (l'écran du dossier ne s'affiche que si l'hôte le demande ; puce dossier dans la saisie, clic = `folder.choose`) ; lot d'annotations (`annotations.batch`, `annotations.status`, `message.user.annotations`, `send.annotations`, `annotations.request`, `annotation.show`) (§15).
- **1.3** (18 nov. 2026, compatible, `"v": 1`) : champ facultatif `summary` sur `toolCall` / `toolCallUpdate` pour le résumé replié des étapes (§14) ; le panneau ne dit plus « fichier » que pour de vrais chemins (`locations`).
- **1.2** (18 nov. 2026, compatible, `"v": 1`) : message `usage {used, size}` (jauge de contexte, plus d'indicateur de chargement dans l'en-tête) ; `permission.request` gagne `tool` et `input` (détails repliables) et son `title` doit être une phrase humaine — sans titre lisible, le panneau en compose un à partir de `tool` ou du nom brut ; changement d'agent par `setAgent` depuis « Autres modèles » (le menu modèle n'envoie plus `setModel` pour un autre agent).
- **1.1** (17 nov. 2026, compatible, `"v": 1`) : message hôte → panneau `changes.diff` en réponse à `checkpoint.viewChanges` ; `FileChange.diff` sert aussi à ouvrir la vue tout de suite ; précision sur `send.text` (libellés des puces en ligne, voulu).
- **1** (17 nov. 2026) : première version.

## 12. Vue « Voir les changements » (1.1)

Ouverte depuis la barre des changements dépliée (« Voir les changements », ou un fichier : la vue se place sur lui). Elle couvre le fil et la saisie, sous l'en-tête du chat ; c'est un `role="dialog"` titré « Changements ».

- **En-tête collant** : « ‹ » Retour au fil (focus à l'ouverture, Échap fait pareil et rend le focus au bouton d'origine), « Changements », « 3 fichiers · +13 −3 », Tout annuler si le tour peut l'être.
- **Liste des fichiers** (si plus d'un) : chemin, « nouveau » / « supprimé », +/− ; un clic fait défiler jusqu'au fichier.
- **Un bloc par fichier** : en-tête repliable (`aria-expanded`, titre de niveau 3) avec chemin, +/− et Copier (le diff en texte) ; diff unifié en JetBrains Mono 12/18, colonnes ancien / nouveau numéro (tirés des en-têtes `@@ -a,b +c,d @@`), signe, texte qui revient à la ligne. Lignes ajoutées et retirées dans les couleurs de la carte d'autorisation (fond `#6FD0871A` texte `#A6E3B8`, fond `#FF7B6E1A` texte `#FFB3AB`), en-têtes `@@` atténués.
- **Repli** : dans une suite de lignes inchangées, on garde 3 lignes de chaque côté d'un changement ; dès que 4 lignes ou plus restent, elles deviennent « ↕ N lignes inchangées », qui se déplie sur place (le focus reste dans ce diff).
- **Lecteurs d'écran** : chaque diff est une région nommée « Différences dans <fichier> » ; les lignes ajoutées et retirées commencent par « ajouté : » / « retiré : » (texte masqué à l'écran), les numéros et signes sont ignorés.

## 13. Révision 1.2 dans le panneau

- **Jauge de contexte** (en-tête) : anneau de 14 px à droite du titre, piste `#FFFFFF26`, remplissage blanc à 70 % puis ambre `#E6AC3D` au-delà de 80 % (le pourcentage est aussi dans l'infobulle et le nom accessible : la couleur n'est jamais seule). Focusable au clavier, infobulle au survol et au focus, annonce « Mémoire du chat presque pleine » une seule fois en passant 80 %. L'en-tête n'a plus d'indicateur de chargement : « Travaille… » reste dans le fil.
- **Menu modèle** : toute la puce « modèle · effort » est un seul bouton (Ctrl+/) qui ouvre un seul menu : modèles de l'agent courant (4 au plus), « Autres modèles › », « Effort · Élevé › ». Les deux sous-menus s'ouvrent dans le même menu, avec une ligne « ‹ Retour » (ou flèche gauche). « Autres modèles » : les modèles restants, puis la section « Autres agents » (logo, nom, statut, « Ouvre un nouveau chat », ou l'action « Se connecter » / « Installer » si l'agent n'est pas prêt).
- **Carte d'autorisation** : titre de l'hôte s'il est humain ; sinon « Claude veut utiliser l'outil « Lister les annotations » (Annotations du navigateur) », composé à partir de `tool` ou du nom brut `mcp__<serveur>__<outil>` (verbe anglais traduit quand il est connu, tirets et soulignés en espaces, majuscule). Les étapes du fil sont réécrites de la même façon. « Détails » (repliable) montre le nom brut et les arguments.

## 14. Résumé replié des étapes (1.3)

Champ facultatif **`summary`** sur `toolCall` (et `toolCallUpdate`) : comment l'étape se dit dans une phrase de résumé.

```ts
interface StepSummary {
  verb: string;        // passé composé sans sujet, en minuscules : "a lu", "a marqué"
  object: string;      // ce sur quoi : "2 annotations", "l'annotation « Bouton de paiement »"
  after?: string;      // suite après l'objet : "comme terminée"
  afterMany?: string;  // la même au pluriel, quand le groupe compte plusieurs éléments : "comme terminées"
  count?: number;      // avec one / many : le panneau additionne ("2 annotations" + "1 annotation" → "3 annotations")
  one?: string;        // "annotation"
  many?: string;       // "annotations"
}
```

Exemples pour l'hôte : lecture d'annotations `{ "verb": "a lu", "object": "2 annotations", "count": 2, "one": "annotation", "many": "annotations" }` ; résolution `{ "verb": "a marqué", "object": "l'annotation « Bouton de paiement »", "after": "comme terminée", "afterMany": "comme terminées" }`.

Règles du panneau, dans l'ordre :
1. **Un groupe d'une seule étape** affiche le titre de cette étape tel quel.
2. Sinon chaque étape apporte : son `summary` ; à défaut ses **vrais chemins** (`read` / `edit` / `delete` / `move` avec `locations`), seuls appelés « fichier » ; à défaut un compte sûr (`search` « fois », `execute` « commande », `fetch` « page ») ; à défaut son titre.
3. Les morceaux de même `verb` + `after` fusionnent : les comptes de même `many` s'additionnent, les objets s'énumèrent (« a, b et c »), les fichiers gardent leur nom jusqu'à 2 puis deviennent « N fichiers ». Un groupe fait seulement de fichiers garde la forme des planches : « A lu 2 fichiers, en a modifié 1 ».
4. Les verbes différents se suivent, séparés par des virgules, dans l'ordre de leur première étape ; les titres seuls viennent ensuite (au plus 2, sinon « N autres étapes »).

## 15. Dossier et « Traiter les annotations » (1.4)

```ts
interface AnnotationItem {
  id: Id;
  note: string;          // la note écrite par l'utilisateur
  element?: string;      // "p.hero-text"
  source?: string;       // "landing.html:23"
  thumb?: string;        // data:image/… (capture de l'élément), sinon un aplat
  status?: "pending" | "working" | "done" | "skipped";   // dans message.user (rejeu) ; absent = pending
  summary?: string;      // résumé d'une phrase de l'agent (done)
  reason?: string;       // pourquoi non traitée (skipped)
}
```

- **Puce dossier** *(retirée en 1.5 : un seul projet par chat ; en 1.9, « + » ne propose le dossier que si le navigateur n'en est pas sûr)*. L'écran « Où est le code de ce site ? » ne s'affiche que si `askFolder` vaut vrai ; il ne montre plus la liste des agents quand l'agent courant est prêt.
- **Avant l'envoi** (Paper 186S-0) : bloc dans la saisie, en-tête « 2 annotations à traiter » + site ; une ligne par annotation (case cochée, miniature 44 × 32, note, « élément · fichier:ligne » en JetBrains Mono) ; décocher = garder pour plus tard ; au-delà de 5, « + N autres » ; champ « Ajouter une précision (facultatif)… ». Envoi possible avec au moins une case cochée, même sans texte.
- **Après l'envoi** (Paper 188L-0, 18B2-0) : `message.user.annotations` remplace la bulle par une carte : en-tête « Traiter N annotations » et « 1 sur N » (« ✓ N terminées » à la fin) ; lignes En attente → « Claude s'en occupe » (anneau bleu, ligne surlignée) → Terminée (coche verte + résumé) ; non traitée = ambre avec sa raison ; clic sur une ligne = `annotation.show` ; « Voir la consigne envoyée à Claude » (le texte exact, `instruction`) replié. Les noms techniques n'apparaissent pas ailleurs.

Bas de la saisie sur une seule ligne *(1.5)* : quand la place manque, le panneau retire d'abord le chevron du mode, puis coupe le nom du modèle (points de suspension) ; jamais l'effort ni le logo du fournisseur.

Écarts assumés (1.4) : la puce dossier n'est pas dessinée dans Paper (icône dossier + nom, gris 50 %, comme les autres outils) ; le bloc de la saisie a une croix « Retirer ces annotations de la saisie » en plus ; la ligne de fin de tour « Point de reprise · Voir les changements · Restaurer » de la planche 18B2-0 n'est pas reprise ici (la barre des changements existante reste).

## 16. Révision 1.5 — retours du test réel (V1.2)

**Menu modèle** (Paper `17US-0`, 300 px, s'ouvre vers le haut). Un groupe par fournisseur **connecté** (`Agent.status` `ready`, `starting` ou `preparing`), dans l'ordre de `state.agents`, séparés par un trait (1 px `#FFFFFF14`, marges 4/10). Un fournisseur pas installé ou pas connecté n'apparaît pas du tout (ni ligne grisée, ni « Se connecter » dans ce menu). Chaque groupe montre ses 3 premiers modèles (`Agent.models`, listes de l'agent) ; `isDefault` porte le badge « Par défaut ». Un fournisseur connecté dont les modèles ne sont pas encore connus (son agent n'a jamais démarré ici) tient sur une ligne à son nom. Puis un trait, « Autres modèles › » (seulement si un groupe a plus de 3 modèles ; le sous-menu les range par fournisseur) et « Effort  <valeur> › ». Ligne choisie : fond `#FFFFFF0F`, coche `#8FA9FF`. Lignes 6/10, rayon 8, 13/18 500.
- Modèle du même agent : `setModel {id, agentId}`, appliqué au message suivant.
- Modèle d'un autre fournisseur : le menu dit d'abord « Passer à Codex ouvre un nouveau chat. Celui-ci reste dans l'historique. » (Annuler / Passer à Codex), puis `setModel {id, agentId: <l'autre>}` : l'hôte démarre une nouvelle conversation avec cet agent et ce modèle. Fournisseur sans modèle connu : `setAgent`.
- Aucun fournisseur connecté : le menu montre l'état de connexion des agents, avec leur action (Se connecter, Installer).

**Effort** (Paper `17VU-0`, sous-menu de 214 px). Les niveaux annoncés par l'agent, en français : `low` Faible, `medium` Moyen, `high` Élevé, `xhigh` X-Élevé, `max` Max (`minimal`, `ultra`, `none` : Minimal, Ultra, Aucun ; un autre nom reste tel quel). L'entrée « Default » d'un agent n'est jamais montrée : l'hôte la retire de `state.efforts`, et le panneau l'ignore aussi par sécurité (`id` `default` ou libellé « Default… »). L'en-tête d'un tour dit « Opus · Moyen · 4 s », jamais « Default ».

**Bas de la saisie** (Paper `14P9-0`) : viser, +, mode ; à droite le bouton modèle avec le logo du fournisseur (14 px), le nom (12/16 500 `#FFFFFFE6`), l'effort (`#FFFFFF80`) et le chevron ; puis envoyer. Plus de puce dossier.

**Infobulle de la jauge.** Elle n'est plus pilotée par `:hover` (un état de survol que la WebView ne quittait jamais la laissait à l'écran) : elle apparaît 300 ms après l'entrée du pointeur, disparaît à sa sortie de la jauge, à la sortie du document, quand la fenêtre perd le focus, ou à `pointer.leave` de l'hôte. Au clavier : `:focus-visible` (un clic ne la garde pas ouverte).

## 17. Révision 1.6 — un clic va toujours au chat (V1.3)

Décision de l'utilisateur après son test : en mode Annotation, sur un site local, un clic sur un élément ouvre **toujours**
le chat (s'il est fermé), l'élément en puce dans la saisie et le curseur de texte dans la saisie. La petite boîte de note
ne s'affiche plus sur un site local ; elle reste là où le chat ne peut pas s'ouvrir (site non local, fenêtre privée).

- **Entrée** : demander à l'agent (inchangé). **Ctrl+Entrée** : garder en annotation (`annotate`) — l'ancien « Ajouter »
  de la boîte de note. Les images jointes restent dans la saisie (une annotation n'emporte que la capture de l'élément).
- **Infobulle du bouton envoyer** (`#send-tip`, `role="tooltip"`, référencée par `aria-describedby`, `aria-keyshortcuts=
  "Enter Control+Enter"`) : deux lignes « Demander à l'agent ↵ » et « Garder en annotation Ctrl ↵ », au-dessus du bouton,
  alignée à droite dans le panneau. 300 ms la première fois, immédiate si une autre bulle vient d'être ouverte, 125 ms
  depuis son bouton (`transform-origin` en bas à droite, échelle 0,98 → 1) ; part à la sortie du pointeur, à l'appui, et
  sur `pointer.leave`. Au clavier : `:focus-visible`. Pendant un tour sans contenu (bouton Arrêter) : pas de bulle.
- **Ligne « Gardé en annotation »** : à droite comme les messages de l'utilisateur, mais plus discrète qu'une bulle
  (bord pointillé `#FFFFFF1F`, pas de fond) ; coche verte, entrée standard du fil (200 ms, 6 px), annoncée poliment.
- La ligne d'aide multi-ligne de la saisie gagne « Ctrl ↵ Annotation ».

## 18. Révision 1.7 — interrupteur de la sélection, modes en français (V1.4)

**Bouton viser** (en bas à gauche de la saisie) : l'interrupteur de la sélection dans la page (curseur viseur, survol, clic
qui vise un élément). `aria-pressed="true"` = active ; enfoncé à la couleur d'accent (teinte 16 % et liseré intérieur
28 %, icône `#8FA9FF`, comme le bouton « Annoter » de la barre), changement en 150 ms, appui 0,96. Un clic envoie
`pickElement {active}` (l'état change tout de suite dans le panneau, l'hôte confirme par `pick.state`). Coupée : la page
redevient normale, le bandeau « Mode Annotation » remonte, le bouton « Annoter » de la barre s'éteint ; le panneau garde
sa conversation, ses puces et les cadres du chat. `Ctrl+Maj+A` (et le bouton « Annoter ») fait la même chose tant que le
chat est ouvert ; chat fermé, il allume ou quitte le mode comme avant. Fermer le chat pendant que la sélection est coupée
quitte le mode. Nom accessible « Sélection d'éléments sur la page », `aria-keyshortcuts="Control+Shift+A"`.

**Modes** *(remplacé en 1.9, §20.4)* : le menu reprend à la lettre les 4 lignes de Paper `14RK-0` (Accept edits, Auto,
Demander, Planifier) ; « Autonome » n'existe plus.

## 19. Révision 1.8 — Mode Commentaire : la bulle compacte

Même page, mêmes fichiers, même fil et mêmes messages que le panneau. Seule la **disposition** change : une bulle de
305 px collée à l'élément, sans en-tête ni liste de conversations (Paper `18NS-0`, `1B30-0`, `1AIE-0`, `1AE7-0`, `1ANU-0`,
`19ON-0`, `1AAE-0`, `1A6L-0`, `1AWI-0`, `19IO-0`).

### 19.1 `layout` (hôte → panneau)

```ts
interface Layout {
  mode: "panel" | "bubble";    // "panel" : le chat à droite, comme avant. Sans `layout`, la page est en "panel".
  maxWidth?: number;           // px CSS. La bulle fait 305 px, ou moins si maxWidth est plus petit (jamais sous 240).
  maxHeight?: number;          // px CSS (déf. 520). La bulle grandit avec son contenu jusqu'à maxHeight ; au-delà, le fil défile.
  side?: "right" | "left" | "below" | "above" | "inside";   // où est la bulle par rapport à l'élément (déf. "right")
  element?: string;            // libellé de l'élément (« button.btn-primary ») : nom de la boîte de dialogue pour les lecteurs d'écran
  open?: boolean;              // déf. vrai : ce `layout` ouvre la bulle (animation d'ouverture). Faux : seulement de nouvelles bornes.
}
```

- **Quand** : à chaque ouverture de la bulle et à chaque bascule Commentaire ↔ Panneau. Ordre conseillé à l'ouverture :
  `layout`, `state`, `thread.reset` (la conversation de cet élément, ou `items: []`), `focus`. Une bascule de `mode` est
  toujours suivie d'un `thread.reset` : le fil est redessiné dans la nouvelle disposition.
- **Premier rendu sans flash** (facultatif) : charger la page avec `index.html?layout=bubble` la met en bulle avant le
  premier `layout`.
- `side` sert à deux choses : l'origine de l'animation d'ouverture (du côté de l'élément) et le bord que l'hôte garde
  fixe quand la hauteur change (`"above"` : le bas ; sinon le haut). La page aligne son contenu sur ce bord : pendant
  qu'une nouvelle taille est en route, rien ne saute.
- **Fond** : en bulle, `html` et `body` sont transparents ; la surface (`#171717`, rayon 16) remplit la fenêtre. L'hôte
  met le fond de la WebView en transparent, ou arrondit la fenêtre (région de rayon 16).

### 19.2 `bubble.size` (panneau → hôte)

`{ "type": "bubble.size", "width": 305, "height": 224 }` — entiers, px CSS. `width` = min(305, `maxWidth`).
`height` = la hauteur du contenu, bornée par `maxHeight` (l'hôte envoie min(520, place libre)). Envoyé après chaque
`layout` en bulle (même si la taille n'a pas changé : c'est l'accusé de réception), puis dès que le contenu change
(jamais deux fois de suite la même valeur). La mesure ne dépend pas de la taille actuelle de la fenêtre : l'hôte peut
la demander fenêtre masquée. Quand le contenu dépasse `maxHeight`, le fil défile et son haut s'estompe (fondu de 96 px).
Le **diff en grand** prend toute la hauteur disponible : `height` = `maxHeight` (une commande ou les arguments d'un
outil, ouverts de la même façon, gardent leur propre hauteur, jamais moins de 224 px).

Hauteurs des planches : saisie seule 104 · tour en cours 224 · arrêté 265 · erreur 248 (détails 279) · terminé 279 ·
autorisation 303 et diff en grand 303 (planches dessinées avec 303 px de place : le haut du fil défile sous le fondu).

Menus (modèle, mode, « + », @, /) et infobulles *(remplacé en 1.9, §20.1)* : ils s'ouvrent **au-dessus** de leur bouton
et peuvent sortir de la bulle ; la hauteur de la bulle ne change pas, la fenêtre transparente grandit autour d'elle.

Séquence type (planche 1 → 3) : `layout` → 84 (saisie vide) ; l'utilisateur écrit deux lignes → 104 ; `message.user` +
`state.running` → 96, puis le tour grandit ligne à ligne jusqu'à 224 ; `turn.end` → 279.

### 19.3 Ce que la bulle montre

| État | Dans la bulle |
|---|---|
| Départ | La saisie seule : « + », le mode avec son nom (Demander, Auto, Planifier), le modèle · effort, envoyer. Pas de bouton viser : l'élément est celui de la bulle. Le bouton envoyer reste allumé saisie vide (planches) : il met alors le curseur dans la saisie ; il ne s'éteint que si l'envoi est impossible (pas de dossier, agent pas prêt, site non local). Sans dossier (`folder: null`) : une ligne « Choisissez le dossier du projet pour commencer. » avec « Choisir le dossier… » (`folder.choose`). |
| Tour en cours | Le message (bulle bleue, sans puce d'élément *(1.9)*), puis « logo modèle · effort » et l'étape en cours à droite (« Lit checkout.css… »), le texte de l'agent. Tant que l'agent n'a rien écrit, l'étape en cours est à la place du texte, sous « modèle · effort » *(1.9, §20.3)*. En bas : le mode et **Arrêter** (`stop`). Pas de saisie pendant le tour. |
| Survol de l'étape | Infobulle avec les étapes déjà faites (« A lu index.html », « A utilisé 1 outil MCP » : les appels MCP bruts sont comptés ensemble ; 6 lignes au plus, les plus récentes), 2 px au-dessus de l'étape, bords gauches alignés, 178 px, hors de la bulle s'il le faut *(1.9)* ; l'étape survolée prend un fond blanc 10 % et des coins 4/4/10/10. Clavier : le focus sur l'étape l'ouvre ; un clic ou Entrée l'épingle. |
| Arrêté (`turn.end` `cancelled`) | « S'est arrêté » à droite du modèle ; la saisie revient (« Que voulez-vous ajouter ? »). |
| Autorisation (`permission.request`) | Dans la bulle, à la place de la saisie : la ligne « Claude veut modifier checkout.css +6 −2 › », « Dire à Claude quoi faire à la place… », l'interrupteur Auto (= option `allow_always`), Refuser / Autoriser. Le haut du fil s'estompe. |
| Diff en grand | Clic sur la ligne de fichier (autorisation) ou sur la barre des changements (terminé) : le diff prend la bulle, avec fermer ; sous un tour terminé, « Copier ». La page envoie `checkpoint.viewChanges` et complète avec `changes.diff` quand le diff n'était pas dans `turn.end`. |
| Erreur (`error` `thread`) | Le texte de l'agent en rouge, « Une erreur est apparue. », « Plus d'infos › » (déplie `detail` en caractères fixes, puis `title` et `message` en clair), **Annuler** (local : la saisie revient) et **Réessayer** (`retry`, ou la première `actions` de l'erreur → `error.action`). |
| Terminé | « C'est fait… », la barre « 1 fichier modifié +6 −2 › » avec **Copier** (`copy` : le diff des fichiers, ou la réponse s'il n'y a pas de diff), « Historique » à droite du modèle (infobulle : lectures, modifications, outils MCP, commandes de ce tour), la saisie (« Que voulez-vous ajouter de plus ? »). |

Ce qui reste comme dans le panneau, en plus serré : plan (`plan`), bandeaux (`strip`), lot d'annotations, ligne « Gardé
en annotation », Markdown. Ce qui n'existe pas en bulle : en-tête, historique, jauge, « Voir les changements » du
panneau, modifier un message, « Tout annuler », dates. Ces messages sont acceptés et ignorés à l'affichage.

### 19.4 Envoi, Échap, focus

- **L'élément de la bulle est implicite.** `send.elements` et `annotate.elements` ne listent que les puces ajoutées à la
  main (menu @) ; ils sont vides le plus souvent. L'hôte ajoute lui-même l'élément auquel la bulle est ancrée.
  `Ctrl+Entrée` (`annotate`) ne demande que quelques mots en bulle. L'hôte n'envoie pas `element.attached` pour
  l'élément ancré.
- **`state.running`** décrit la conversation montrée dans la bulle, pas l'agent en général.
- **Échap**, dans l'ordre : ferme le menu ouvert → ferme l'infobulle épinglée → ferme le diff en grand → `close`.
  Jamais `stop`, jamais de refus d'autorisation : la bulle se ferme, l'agent continue, la demande d'autorisation
  attend la réouverture. `close` part tout de suite (aucune attente côté hôte) ; la page efface sa surface en 120 ms
  et la garde invisible jusqu'au prochain `layout` (ou `focus`) : la réouverture ne montre jamais l'ancien état.
- **`focus`** : le curseur va dans la saisie ; sur « Autoriser » si une autorisation attend ; sur la bulle elle-même
  pendant un tour (il n'y a pas de saisie).
- **Infobulles** : `hover` / `pointer.leave` comme en 1.5.

### 19.5 Mouvement et accessibilité

Ouverture : 180 ms, opacité + échelle 0,96 → 1 depuis le côté de l'élément (`side`). Fermeture : 120 ms. Mouvement
réduit (`prefers-reduced-motion` ou `hello.reducedMotion`) : fondu seul. La bulle est une boîte de dialogue non modale
(`role="dialog"`, `aria-modal="false"`, nommée « Commentaire sur <element> ») ; le fil reste un `role="log"`.

### 19.6 File d'attente entre éléments

Un seul tour d'agent à la fois. Si l'utilisateur écrit dans la bulle d'un autre élément pendant un tour, l'hôte garde
la demande et envoie `queue { items: [{ id, text, after: "button.btn-primary" }] }` à cette bulle. Elle montre le
message en attente (bulle estompée) et, dessous, le texte discret « Commencera après le travail en cours sur
button.btn-primary » avec « × » (`queue.remove`). Sans `after` : « En file ». Quand le tour démarre, l'hôte envoie
`queue { items: [] }`, puis `message.user` et `turn.start` comme d'habitude. Un bandeau `strip` d'identifiant `wait`
qui dirait la même phrase n'est pas affiché en bulle tant qu'un message en attente porte `after` (pas de doublon).

### 19.7 Écarts assumés (bulle)

1. **Infobulles** *(1.9)* : elles sortent de la bulle comme sur `1AIE-0` (7 px en haut, 36 à 38 px à droite) ; la page se
   cale sur l'étape telle que la place `1B30-0` (2 px plus à gauche que sur `1AIE-0`, qui fige aussi sa largeur à 128 px).
2. **« Ask » → « Demander »** (noms de modes en français, §18).
3. **Menus** : non dessinés dans les planches du mode ; mêmes menus que le panneau (6.4), au-dessus de leur bouton, hors de
   la bulle *(1.9)*. Effort et Autres modèles sont de vrais sous-menus à côté du menu modèle (§20.1) ; le panneau, trop étroit
   pour deux menus côte à côte, garde la vue dans le menu (« ‹ Effort », §9.9).
4. **Variantes entre planches**, une seule retenue : bulle du message de 210 px au plus (`1AE7-0` la dessine à 220) et
   de rayon 12 (`1AWI-0` : 10) ; étape en cours avec l'anneau de 14 px et son retrait de `1B30-0` (`1ANU-0` : anneau de
   12 px collé au bord droit).
5. **Texte** : mêmes polices que l'app (Inter Display Regular / Medium, JetBrains Mono) ; Paper dessine les lettres un
   peu plus grasses et plus serrées. C'est tout l'écart moyen mesuré, comme pour le panneau (§9).
6. **Ce que les planches ne dessinent pas** et que la bulle sait montrer : plan proposé, file d'attente entre éléments,
   plusieurs fichiers dans le diff en grand, commande à autoriser, pas de dossier, mode Auto (nom en couleur d'accent).

Écart moyen mesuré (canal, sur 255) entre la bulle de la planche et la bulle rendue, à taille égale au pixel :
saisie 7,1 · tour en cours 9,1 · arrêté 9,9 · autorisation 7,5 · autorisation, diff ouvert 3,2 · erreur 7,2 · erreur,
détails 8,1 · terminé 8,2 · terminé, diff ouvert 3,4 (peu de texte : l'écart tombe). Captures et comparaisons :
`build/chat-qa/comment/` et `build/chat-qa/comment/compare/`.

### 19.8 Nom du dossier plutôt que le chemin ou l'adresse (retour Paper `156L-0`)

Là où le panneau montrait un chemin ou une adresse pour dire de quel projet il s'agit, il montre le **nom du dossier** :
écran du dossier (le chemin complet est donné aux lecteurs d'écran, `aria-description`, 1.9), menu « + › Choisir le dossier… » (1.9 : seulement si le dossier n'est pas sûr), titre « Ce site ·
boutique » de l'historique, et lignes « Autres sites » (`HistoryItem.folderName`, sinon `siteLabel`).

## 20. Révision 1.9 — retours du premier test réel du Mode Commentaire

Source : `chat-comment-v2-retours.md` (R1 à R12) ; valeurs : relevé Paper `design/chat-comment-paper-values.md`.

### 20.1 Menus et infobulles hors de la bulle (R3)

La bulle est une fenêtre transparente : la page peint elle-même la surface (`#171717`, rayon 16, **ni ombre ni bordure**,
Paper `18TO-0` … `1B19-0`) et peut dessiner autour d'elle, dans la marge que l'hôte lui accorde.

- **Page → hôte** : `bubble.size { width, height, overflowTop?, overflowRight?, overflowBottom?, overflowLeft? }`. `width` /
  `height` restent la taille de la **surface** ; chaque `overflow*` (entier 0 à 400, absent = 0) est la place voulue de ce
  côté de la surface pour ce qui flotte (menu ouvert, infobulle), ombre comprise. Au repos, rien n'est demandé.
- **Hôte → page** : l'hôte répond à **chaque** `bubble.size` par `layout { mode: "bubble", open: false, inset: { top, right,
  bottom, left } }` : les marges réellement accordées (moins que demandé au bord de l'écran). La surface reste à sa place à
  l'écran ; la page la dessine à (`inset.left`, `inset.top`) dans la fenêtre.
- **Accusé de réception** : la page répond toujours à un `layout` d'**ouverture** (`open` absent ou vrai) par `bubble.size`,
  même inchangé. Après un `layout` `open: false`, elle n'envoie `bubble.size` que si ce qu'elle veut a changé : pas de boucle.
- **Marge refusée** : si un côté accordé est plus petit que demandé, la page replace ce qui flotte — au-dessus de son
  bouton, sinon dessous, sinon dans la surface (la liste défile) — et redemande ; les limites ne font que baisser tant que
  quelque chose flotte, donc cela se termine (deux allers-retours au plus en pratique).
- **Clics** : au repos, les clics hors de la surface vont à la page Web dessous. Tant qu'un `overflow` est demandé, toute la
  fenêtre prend les clics ; un clic dans la zone transparente ferme le menu ou l'infobulle (il n'agit pas dessous).
- **Placement** (Paper 6.4 `17US-0`, `17VU-0`, `14RK-0` ; `1AIE-0`) : menus 6 px au-dessus de leur bouton, alignés sur
  son bord (modèle : à droite ; mode, « + », @ : à gauche) ; menu modèle 300 px sans ombre, lignes 1 px l'une de l'autre ;
  menu des modes 290 px avec l'ombre `#00000080 0 16px 48px`, lignes 2 px l'une de l'autre ; infobulle des étapes 2 px
  au-dessus de l'étape, bords gauches alignés, 178 px, coins 10/10/10/4 (le coin de 4 px côté étape), sans ombre ;
  infobulle du bouton envoyer 6 px au-dessus, bords droits alignés.
- **Sous-menus** (Paper 6.4 : `17VU-0` à côté de `17US-0`) : dans la bulle, « Effort › » (214 px) et « Autres modèles › »
  (300 px) ouvrent un vrai sous-menu à côté du menu modèle, sans ligne « ‹ Retour » : à sa droite, 4 px plus loin, hauts
  alignés (`overflowRight`) ; à sa gauche si l'hôte ne peut pas donner la place à droite (`overflowLeft`) ; par-dessus sa
  partie droite si aucun côté n'a la place. Ouverture : survol de la ligne (120 ms), clic, Entrée ou flèche droite (le focus
  va sur le niveau choisi) ; fermeture : flèche gauche ou Échap (le menu modèle reste, focus sur sa ligne), survol d'une
  autre ligne (250 ms) ; choisir ferme les deux. La ligne porte `aria-haspopup="menu"` et `aria-expanded`.

### 20.2 Pas de puce sous le message en bulle (R7)

L'élément est celui de la bulle : le message de l'utilisateur n'affiche aucune puce d'élément (ni dans le texte, ni dessous),
même si `message.user.elements` en porte. Le panneau, lui, ne change pas.

### 20.3 L'étape en cours à la place du message (R8, Paper `1B30-0`)

Tant qu'aucun `message.agentChunk` n'est arrivé pour le tour, l'étape en cours (« Lit checkout.css… », « Réfléchit… »,
« Travaille… ») s'affiche dans le bloc du texte de l'agent, sous « modèle · effort », comme un message (13/18, blanc 80 %,
anneau de 14 px). Le premier texte prend sa place ; l'étape repasse à droite du modèle. Faire écrire à l'agent une première
phrase qui dit ce qu'il va faire relève de l'hôte (consigne envoyée).

### 20.4 Modes : les 4 lignes de Paper (R4, `14RK-0` mis à jour par l'utilisateur)

`Mode.kind` gagne `"edits"`. Le panneau et la bulle montrent au plus ces 4 lignes, dans cet ordre, avec ces mots (quel que
soit `label`) : « Accept edits » / « Automatically accept all file edits » (`edits` : Claude `acceptEdits`, Codex
`workspace-write` ; double chevron 16 px `#3E67FF`) ; « Auto » / « Claude handles permission decisions » (`auto` : Claude
`auto`, Codex `agent` ; éclair 16 px `#3E67FF` ; « Codex » à la place de « Claude » pour Codex) ; « Demander » / « Propose
chaque modification. » (`ask`, bouclier 15 px blanc 85 %) ; « Planifier » / « Propose un plan, ne touche à aucun
fichier. » (`plan`, liste 15 px). La ligne choisie : fond blanc 6 %, coche `#8FA9FF` 2 px plus bas. Un mode absent de
l'agent n'a pas de ligne ; un mode `other` (dont celui qui contourne toutes les autorisations) n'est jamais montré.
Maj+Tab passe au suivant de ces lignes. Compatibilité : un `kind: "auto"` dont l'`id` dit acceptEdits / edits / workspace
est lu comme `edits`. Dans la saisie, `edits` et `auto` portent l'accent (icône et nom).

### 20.5 Renommer la conversation (R11)

Le titre de l'en-tête n'ouvre plus l'historique (le bouton Historique est juste à côté). Double-clic (ou F2 au clavier) :
un champ en place, le nom sélectionné ; **Entrée** envoie `conversation.rename { id, title }` et garde le nom, **Échap** ou la
sortie du champ remettent l'ancien (rien n'est envoyé, Échap ne ferme pas le panneau). Une nouvelle conversation (pas
d'`id`) ne se renomme pas et son titre n'est pas un arrêt de Tab.

### 20.6 Dossier (R5)

Quand le dossier est trouvé avec certitude (`folder` présent, `askFolder` non vrai), il n'apparaît nulle part dans la saisie
et ne se change pas depuis elle : « + » ne garde que « Joindre une image ». « Choisir le dossier… » n'y revient que si le
navigateur n'est pas sûr ; l'écran « Où est le code de ce site ? » reste le chemin normal dans ce cas.

### 20.7 Logo du fournisseur dans l'en-tête des réponses (R12)

À gauche du nom du modèle, comme dans la saisie : 14 px dans la bulle (« Sonnet 5.5 Élevé », Paper `1B66-0`), 12 px sur la
ligne de 14 px du panneau (« Sonnet · Élevé · 18 s »).

### 20.8 Demande d'autorisation après `thread.reset` (R9, côté page)

Chaque ouverture de la bulle envoie `thread.reset`, qui efface les cartes vivantes ; l'hôte renvoie ensuite chaque
`permission.request` encore en attente. La page la réaffiche, focus sur « Autoriser » ; la même `requestId` reçue deux fois
ne fait jamais deux cartes.

### 20.9 Fidélité

`tests/chat-panel/run.py` mesure dans la page chaque valeur du relevé Paper pour les 10 états de la bulle et les 3 menus
(boîtes relatives à la bulle ou au menu, styles calculés ; `tests/chat-panel/fidelity.json` relie chaque nœud Paper à son
élément) et écrit le tableau `build/chat-qa/comment-v2/panel/ecarts.md` : 0 écart non justifié. Écarts justifiés : le nom
français du mode (« Demander » au lieu de « Ask ») ; les variantes entre planches de §19.7.4 (bulle de 220 px de `1AE7-0`,
rayon 10 de `1AWI-0`, étape de `1ANU-0`, étape survolée de `1AIE-0`) ;
trois marges portées par l'enfant plutôt que par le cadre, au même rendu (barre « 1 fichier modifié », zone du diff).

### 20.10 Libellé des étapes (audit D4, Paper `1B30-0`, `1AIE-0`, `1ANU-0`)

Une étape n'est jamais montrée avec le titre brut de l'agent (« Edit styles\landing.css »), ni en bulle ni dans le panneau.
Le panneau la dit en français, avec le **nom du fichier seul**, au présent suivi de « … » tant qu'elle tourne et au passé
une fois finie : d'abord `summary` de l'hôte (« a lu » + « 2 annotations » → « Lit 2 annotations… » / « A lu 2
annotations ») ; sinon `kind` + le nom de `locations[0].path` (à défaut, le fichier nommé dans le titre) : read « Lit X… » /
« A lu X », edit « Modifie X… » / « A modifié X », delete, move, search, fetch de même ; execute « Lance <commande>… » /
« A lancé <commande> » (la commande du titre si elle tient en 40 caractères, sinon « une commande ») ; think « Réfléchit… » ;
un appel MCP brut « Utilise l'outil « … »… » / « A utilisé 1 outil MCP ». Le titre (rendu lisible) ne sert que si `kind`
ne dit rien.

### 20.11 Aucune infobulle système (audit D7)

Aucun élément de la page ne porte d'attribut `title` (bulle, panneau, liens du Markdown) : une infobulle système apparaissait
sous le « + » de la bulle, absente de Paper. Les noms sont dans `aria-label` (et `aria-description` pour un détail : chemin
du dossier, adresse d'un lien, « Double-clic ou F2 pour renommer ») ; les seules infobulles visibles sont celles que Paper
dessine (étapes, envoyer, jauge). Le DOM le garantit (`h()` ignore `title`) et chaque scénario de test le vérifie.

### 20.12 Second `turn.start` du même tour

L'hôte peut renvoyer `turn.start { turnId, model, effort, agentId, at }` avec le **même** `turnId` quand le modèle et l'effort
deviennent connus après le début du tour. La page le prend comme une mise à jour de l'en-tête de ce tour seulement :
« logo modèle · effort » (bulle) ou « Opus 5.5 · Élevé » (panneau ; avec la durée si le tour est fini). Un champ absent garde
sa valeur. Rien d'autre ne bouge : pas de nouveau tour, pas de repère « Passé à … », le chronomètre, le texte et les étapes
restent. Avant 1.9 ce second message était ignoré.

