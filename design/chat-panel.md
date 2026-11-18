# Panneau de chat — page interne et protocole avec l'hôte (contrat B)

**En bref.** Le panneau de chat est une page web à nous (`assets/chat-panel/`), affichée dans une WebView2 interne à droite de la page. Il ne parle qu'à l'hôte (le navigateur, C4), par messages JSON. Il ne lit ni n'écrit aucun fichier, n'ouvre aucune connexion réseau, et n'exécute aucun texte reçu : tout ce qui vient de l'agent est affiché comme du texte.

- Propriétaire : C2. Consommateur : C4 (`src/chat-host.*`).
- Version du protocole : **1.2** (`"v": 1`). Ce document est la référence ; une évolution compatible (nouveaux messages ou champs facultatifs) garde `"v": 1` et prend un numéro de révision ici ; une rupture incrémente `v`. Historique en fin de document (§11).

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
| Taille | Conçu pour 380 DIP de large (planches), fonctionne de 320 à 560. Hauteur libre. |

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
  kind: "ask" | "auto" | "plan" | "other";   // pilote l'icône : bouclier / double chevron / liste
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
| `hello` | `v`, `locale` (`"fr"`), `platform` (`"windows"`), `reducedMotion?` (bool, si Windows le demande) | Initialise. `reducedMotion` s'ajoute à `prefers-reduced-motion`. |
| `state` | voir §4.1 | Fusion **par clé de premier niveau** : une clé absente garde sa valeur, `null` l'efface. |
| `thread.reset` | `conversationId`, `items: Item[]` (§4.2) | Remplace tout le fil (ouverture d'une conversation, nouveau chat = `items: []`). |
| `thread.append` | `items: Item[]` | Ajoute des éléments déjà terminés (suite d'un historique découpé). |
| `message.user` | `id`, `text`, `elements?: ElementRef[]`, `attachments?: Attachment[]`, `at: Iso`, `editable?` (déf. vrai) | Bulle de l'utilisateur, puces dans le texte, date centrée si nouveau jour ou pause > 30 min. |
| `turn.start` | `turnId`, `model` (libellé), `effort?` (libellé), `agentId`, `at: Iso` | Ouvre un tour : « Travaille… 0 s » démarre, chronomètre local. Si modèle/effort diffère du tour précédent : repère « Passé à Opus 5.5 · Élevé ». |
| `message.agentChunk` | `turnId`, `messageId`, `text` (morceau à ajouter, Markdown) | Texte en flux. Rendu par paragraphe, sans animation par mot ; annoncé aux lecteurs d'écran par phrase terminée. |
| `thought` | `turnId`, `id`, `text` (morceau à ajouter), `done?`, `durationMs?` | Ligne repliée « A réfléchi 6 s ›» (pendant : « Réfléchit… »). Clic : détail en texte atténué. |
| `toolCall` | `turnId`, `id`, `title`, `kind` (`read`/`edit`/`delete`/`move`/`search`/`execute`/`think`/`fetch`/`other`), `status` (`pending`/`in_progress`/`completed`/`failed`), `locations?: {path,line?}[]`, `diff?: Diff` | Étape. Les étapes consécutives terminées se regroupent en une ligne « A lu 2 fichiers, en a modifié 1 › ». L'étape en cours s'affiche seule avec un indicateur. Échec : « (échec) » en rouge dans la ligne. |
| `toolCallUpdate` | `id`, et n'importe lequel de `title`, `status`, `locations`, `diff` | Met à jour l'étape. |
| `plan` | `turnId`, `id`, `entries: {content, status: "pending"|"in_progress"|"completed"}[]`, `proposed?` | `proposed: true` (mode Planifier, attend le feu vert) : carte « Plan proposé · N étapes » avec **Lancer le plan** / **Modifier**. Sinon liste de suivi mise à jour en place. |
| `permission.request` | `requestId`, `toolCallId?`, `title` (« Claude veut modifier checkout.css » : **phrase humaine**, verbe d'abord, ce qui va se passer et sur quoi ; jamais un nom technique), `kind`, `diff?: Diff`, `command?` (pour `execute`), `tool?` *(1.2)* `{ name, label?, server?, serverLabel? }` (outil appelé : `name` brut, ex. `mcp__browser-annotations-preview__browser_annotations_list` ; `label` « Lister les annotations » et `serverLabel` « Annotations du navigateur » si l'hôte les connaît), `input?` *(1.2)* (arguments bruts de l'outil, texte JSON ≤ 4 000 caractères), `options: {optionId, name, kind: "allow_once"|"allow_always"|"reject_once"|"reject_always"}[]` | **La carte remplace la saisie.** Focus sur « Autoriser ». Annonce assertive. Plusieurs demandes : file, une à la fois, « 1 sur 2 ». |
| `permission.cancel` | `requestId` | Retire la carte (tour annulé par l'agent). |
| `turn.end` | `turnId`, `stopReason` (`end_turn`/`cancelled`/`refusal`/`max_tokens`/`error`), `durationMs`, `changes?: FileChange[]`, `checkpointId?`, `undoable?`, `copyText?` | Ferme le tour : infos du tour (« Sonnet · Élevé · 18 s »), barre « ✓ 1 fichier modifié +6 −2 ⌄ » avec Tout annuler, point de reprise, actions au survol (Copier, Relancer). `cancelled` : ligne « Arrêté ». `refusal` / `max_tokens` : ligne d'explication. |
| `turn.undone` | `turnId`, `ok`, `conflicts?: string[]`, `message?` | Après `undo.turn` ou `checkpoint.restore` : la barre des changements passe à « Annulé », ou affiche le conflit nommé (rien n'a été écrasé). |
| `error` | `id`, `scope` (`thread`/`composer`), `title`, `message`, `detail?`, `actions?: Action[]`, `turnId?` | `thread` : carte rouge dans le fil (6.7 D), la demande est gardée. Bouton « Voir le détail » local si `detail`. Les actions renvoient `error.action`. |
| `strip` | `id`, `tone` (`danger`/`neutral`/`info`), `text`, `action?: Action` ; ou `id` + `clear: true` | Bandeau collé au-dessus de la saisie (6.5 états 7 et 8 : déconnecté, limite atteinte, mémoire pleine…). `danger` désactive la saisie. |
| `queue` | `items: {id, text}[]` | Suites en attente pendant un tour : « En file · Et fais pareil pour le titre · Envoyer maintenant · × ». |
| `history` | `items: HistoryItem[]`, `query?` | Liste de l'historique (6.7 B). `HistoryItem = {id, title, subtitle?, siteLabel, thisSite: bool, agentIcon, updatedAt: Iso, running?: bool, current?: bool}`. |
| `element.attached` | `element: ElementRef` | Ajoute la puce dans la saisie (viseur ou `@`). Met fin au mode viseur. |
| `attachment.added` | `attachment: Attachment` | Ajoute la miniature dans la saisie. |
| `pick.state` | `active: bool` | Bouton viseur enfoncé ou non. |
| `mentions` | `query`, `elements: ElementRef[]`, `files: {path, dir}[]` | Réponse à `mention.query` (menu « @ »). |
| `commands` | `items: {name, description}[]` | Menu « / » (vient de l'agent ; liste vide = menu masqué). |
| `edit.confirm` | `messageId`, `files: string[]` | « Des fichiers ont changé depuis ce message » : **Restaurer et renvoyer** / **Renvoyer sans restaurer** / Annuler. |
| `focus` | `target: "composer"` | Met le focus dans la saisie (ouverture du panneau). |
| `copy.done` | `ok` | Le bouton Copier affiche « Copié » 1,5 s (ou rien si `ok` faux). |
| `usage` *(1.2)* | `used`, `size` (jetons de contexte utilisés / taille de la fenêtre, depuis `usage_update` ACP) | Jauge de contexte à droite du titre : anneau rempli à `used/size`, neutre, ambre au-delà de 80 % ; infobulle « 62 % de la mémoire du chat utilisée · se résume automatiquement vers 100 % ». `size: 0` (ou nouvelle conversation, `thread.reset`) la masque ; l'hôte renvoie `usage` après un `thread.reset` s'il le connaît. |
| `changes.diff` *(1.1)* | `turnId`, `diffs: Diff[]` (tous les fichiers du tour, ≤ 4 000 lignes chacun), `files?: FileChange[]` | Réponse à `checkpoint.viewChanges`. Remplit la vue « Voir les changements » si elle est ouverte sur ce tour (sinon gardé pour la prochaine ouverture). Un fichier sans entrée dans `diffs` affiche « Pas d'aperçu pour ce fichier ». |

### 4.1 `state`

```ts
interface State {
  site?: { origin: string; label: string; local: boolean };      // label : "127.0.0.1:5173"
  folder?: { path: string; display: string; name: string } | null;   // null : première fois (6.2)
  folderSuggestion?: { path: string; display: string; name: string; reason: "auto" } | null;
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
| `send` | `text`, `elements: Id[]`, `attachments: Id[]` | Entrée dans la saisie. Pendant un tour : l'hôte met en file (`queue`) ou envoie en pilotage si l'agent le permet. **`text` contient le libellé de chaque puce à sa place** (« … comparé à button.btn-primary dans le canvas ») : c'est voulu, l'hôte l'utilise tel quel comme texte de l'utilisateur et décrit les éléments à part (contrat D) ; `elements` les liste dans l'ordre d'apparition. Le panneau, à l'écho `message.user`, replace la puce à la première occurrence du libellé. |
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
| `pickElement` | `active: bool` | Bouton viseur. L'hôte répond `pick.state`. |
| `element.remove` | `id` | « × » sur une puce. |
| `attachImage` | `source: "picker"` ou `source: "paste"` + `name`, `mime`, `data` (base64) | « + › Joindre une image » ou collage. L'hôte répond `attachment.added`. |
| `attachment.remove` | `id` | « × » sur une miniature. |
| `mention.query` | `query` | Frappe après « @ » (débit limité à 1 / 80 ms). |
| `folder.use` | `path` | « Utiliser » sur le dossier détecté. |
| `folder.choose` | — | « Choisir un autre dossier… » (sélecteur Windows côté hôte). |
| `undo.turn` | `turnId` | « Tout annuler » dans la barre des changements. |
| `checkpoint.restore` | `checkpointId` | « Revenir » sur un point de reprise. |
| `checkpoint.viewChanges` | `turnId`, `path?` | « Voir les changements » (barre dépliée) ou clic sur un fichier (`path`). Le panneau ouvre aussitôt sa vue avec les `FileChange.diff` déjà reçus, puis attend `changes.diff` (1.1). |
| `plan.run` | `planId` | « Lancer le plan ». |
| `plan.edit` | `planId` | « Modifier » (le panneau met aussi le focus dans la saisie, placeholder « Ajouter une précision au plan… »). |
| `history.open` | — | Ouverture de l'historique (titre ⌄ ou icône). L'hôte répond `history`. |
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
| `close` | — | Bouton fermer, ou Échap quand rien d'autre n'est ouvert. |
| `panel.log` | `level` (`info`/`warn`/`error`), `message` | Diagnostic (message invalide, exception). Borné à 2 Kio. |

---

## 6. Règles de comportement liées au protocole

- **Défilement.** Le fil suit le texte tant que l'utilisateur est en bas (à 24 px près). Dès qu'il remonte, il ne bouge plus et le bouton « ↓ N nouvelles étapes » apparaît (N = étapes et messages arrivés depuis).
- **Autorisation.** Pendant qu'une carte est affichée, la saisie est remplacée ; Échap = Refuser (option `reject_once`), Entrée = Autoriser (`allow_always` si la case est cochée, sinon `allow_once`). Le champ « Dire à Claude quoi faire à la place… » envoie l'option `reject_once` avec `message`.
- **Échap**, dans l'ordre : ferme le menu ouvert → quitte la modification d'un message → Refuser la demande d'autorisation → `stop` si un tour est en cours → `close`.
- **Clavier** : Entrée envoie, Maj+Entrée nouvelle ligne, Ctrl+/ ouvre le menu modèle, Maj+Tab change de mode (dans la saisie), flèches + Entrée dans les menus, `@` et `/` en début de mot ouvrent leurs menus.
- **Rien n'est déduit en dur** : modèles, efforts, modes, commandes et agents viennent de `state` / `commands`.
- **Accessibilité** : fil `role="log"` (`aria-live="polite"`, annonces par phrase), carte d'autorisation `role="alertdialog"` (annonce assertive), menus `role="menu"`/`listbox`, tous les boutons nommés, ordre Tab : en-tête → fil → bandeau → saisie.

## 7. Faux hôte (QA)

`assets/chat-panel/dev.html` charge la page de production plus `dev-host.js`, qui imite `window.chrome.webview` et rejoue des scénarios (`?scenario=6.1-autorisation`, `6.2-premiere-fois`, … liste dans `dev-host.js`, mode d'emploi dans `tests/chat-panel/README.md`). Ni `dev.html` ni `dev-host.js` ne sont copiés dans le build.

**À embarquer par C4** (et rien d'autre) : `assets/chat-panel/index.html`, `panel.css`, `panel.js`, `markdown.js`, `icons.js`, plus `assets/fonts/InterDisplay-Regular.ttf`, `InterDisplay-Medium.ttf`, `JetBrainsMono-Regular.ttf` (servis sous `/fonts/`).

## 8. Ce que fait le panneau (résumé pour l'intégration)

| Zone | Comportement |
|---|---|
| En-tête | Logo de l'agent, titre (bouton : ouvre l'historique, chevron qui pivote), source `Checkout.tsx:42` au-dessus si `conversation.source` (en-tête 52 px avec filet), indicateur « en cours », Nouveau / Historique / Fermer. |
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
12. **Menu « + »** : absent des planches, deux lignes (Joindre une image, Choisir un autre dossier…) au style des menus 6.4.

Écart moyen mesuré (canal, sur 255) entre planche et panneau : 6.2 : 3,6 · 6.7 A : 7,0 · 6.8 : 7,3 · 6.9 : 8,4 · 6.7 B : 9,8 · 6.10 : 11,6 · 6.1 : 13,1 · 6.3 : 14,6 · 6.11 : 15,8 · 6.12 : 17,9 · 6.6 : 18,5. Les plus forts viennent des écarts 4 et 5 (contenu décalé de la hauteur d'une ligne), pas du dessin des composants.

## 10. Tests

`python -I tests/chat-panel/run.py` — 206 contrôles (statique, 47 scénarios, clavier, sécurité, polices, performance). Détails : `tests/chat-panel/README.md`. Sortie : `build/chat-qa/panel/`.

## 11. Historique du contrat

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

