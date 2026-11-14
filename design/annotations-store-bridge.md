# Annotations — magasin, bridge MCP et connexion des agents (V1 + V2-A)

Contrat : `design/annotations-v1-brief.md` (worktree du plan). Ce document décrit ce qui est livré côté magasin et bridge, pour l'intégration dans le navigateur.

## Fichiers

| Fichier | Rôle |
| --- | --- |
| `src/strict-json.h` | Parseur/sérialiseur JSON strict, en-tête seul (namespace `StrictJson`). |
| `src/annotations.h`, `src/annotations.cpp` | Magasin (namespace `BrowserAnnotations`) : modèle, bornes, ids, chemins, baux, rétention, lecteurs, export Markdown. Win32 (kernel32, bcrypt) + C++17, sans UI. |
| `src/mcp-bridge.cpp` | `browser-annotations-mcp.exe` : MCP stdio, console, statique. |
| `tools/release/build.py` | `annotations.cpp` dans les sources du navigateur ; bridge construit à côté de `browser.exe` (`--annotations-bridge auto|on|off`, `auto` = tout sauf `--channel release`). |
| `tests/annotations/` | `run_tests.py` (runner), `store_test.cpp`, `json_fuzz.cpp`, `claim_race.cpp`, `bridge_fixture.cpp`, `transcripts/*.json`, `golden/tools-list.json`. |

## Choix du parseur JSON

Le dépôt n'a pas de parseur JSON à lui : `main.cpp`/`elements-integration.h` n'extraient que des champs isolés. RapidJSON n'existe que dans les sources internes de ThorVG (`deps/thorvg-1.1.3/src/loaders/lottie/rapidjson`, dossier ignoré par git, matérialisé par `build-lottie.py`) : s'y lier coupleraient le magasin et le bridge à un détail interne d'une autre dépendance, et son mode par défaut est récursif sans borne de profondeur, tolère les clés dupliquées et n'impose pas l'UTF-8 valide. Choix retenu : **parseur strict maison** (~400 lignes, en-tête seul), aucune nouvelle dépendance ni licence :

- RFC 8259 strict : pas de commentaires, virgules finales, NaN/Infinity, zéros de tête, contrôles bruts ; UTF-8 strict (surlongs, substituts, > U+10FFFF refusés) ; `\u` appariés ; **clés dupliquées refusées** ; BOM refusé sauf pour les fichiers du magasin (Bloc-notes).
- Bornes : taille du document, profondeur (vérifiée avant de descendre : pas de débordement de pile), taille de chaîne, nombre total d'éléments. Magasin : 256 Kio / profondeur 16 ; stdin du bridge : 8 Mio par ligne / profondeur 64.
- Sortie sur une seule ligne (contrôles, U+2028/2029 échappés ; UTF-8 invalide → U+FFFD) : directement utilisable comme trame stdio MCP. Forme canonique stable (`serialize(parse(serialize(x))) == serialize(x)`).
- Tests : vecteurs fixes, bornes (bombe de 200 000 `[`), **300 000 mutations** déterministes, 20 000 allers-retours d'arbres aléatoires, et **test différentiel** contre le module `json` de Python sur 12 009 documents générés/mutés (0 désaccord). Le fuzz a trouvé un vrai défaut de forme canonique (`-0.0`), corrigé.

## API (`annotations.h`)

Toutes les méthodes de `Store` font des E/S synchrones et **bloquent** l'appelant ; `Claim`/`Release`/`Resolve`/`ForceRelease`/`Reopen` peuvent en plus attendre jusqu'à 2 s le verrou court. Le navigateur doit les appeler hors du fil UI. Les fonctions de texte, d'URL, de temps, de (dé)sérialisation et `ToMarkdown` sont pures. Plusieurs `Store` (threads ou processus) peuvent viser la même racine.

Points d'intégration :

- Racine : `BrowserAnnotations::StoreRootForDataDir(paths.dataDir)` avec le `dataDir` déjà résolu par `UpdateCore::ResolveDataPaths`. Le bridge sans `--store` utilise `DefaultStoreRoot(exe)`, testé égal à `ResolveDataPaths(<même dossier>\browser.exe).dataDir + \annotations` (et `<root>\bin\` → `<root>\data\annotations`).
- Écriture : `Store::WriteAnnotation(a, &png, draft)` (id généré si vide, `Sanitize` appliqué, `annotation.json` écrit en dernier). `PublishDraft`, `Delete`, `DeleteProject`, `Reopen`, `ForceRelease` pour le tiroir.
- Lecture du tiroir : `List({project, status, drafts, newestFirst, limit, cursor})`, `Read`, `ReadShot`, `ListReaders`, `LoadProjects`/`SaveProjects`.
- Rétention : `Purge()` à l'ouverture du tiroir et au lancement (jamais sur minuterie).
- Listes blanches pour le script de capture : `AllowedStyleProperties()` (32 propriétés), `AllowedAttributes()`.
- « Copier en Markdown » : `ToMarkdown(entry, store.ShotPath(...))`.

## Baux

`claim` = `CreateFileW(CREATE_NEW)` (exclusivité NTFS). Toute lecture-modification de `state.json` et tout remplacement de bail se fait sous `claim.lock`, créé en `CREATE_NEW | FILE_FLAG_DELETE_ON_CLOSE` : le système le supprime à la fermeture du handle, y compris si le processus meurt, donc aucun verrou orphelin. Bail expiré : supprimé sous verrou seulement si son contenu n'a pas changé, puis nouvelle tentative `CREATE_NEW`. Le fichier `claim` fait foi : un bail expiré que personne n'a repris permet encore à son détenteur de résoudre ; une fois repris, l'ancien jeton est refusé. Un `claim` illisible est considéré « en cours d'écriture » pendant 60 s (temps réel), puis expiré.

## Bridge MCP

- stdio ligne par ligne, stdout strictement JSON-RPC (testé), journaux sur stderr (`[browser-annotations-mcp] …`, dont la révision demandée par le client). Bloque sur `ReadFile(stdin)` : aucune boucle de polling, aucun thread, sortie à la fermeture de stdin.
- Dual-era : `initialize` (2025-11-25 ; 2025-06-18/2025-03-26/2024-11-05 conservées, sinon 2025-11-25) ; requêtes avec `_meta["io.modelcontextprotocol/protocolVersion"] = "2026-07-28"` servies sans état (`resultType`, `serverInfo` dans `_meta`), `server/discover`, `-32022` pour une version inconnue, `-32602` si `_meta` incomplet ou ni `_meta` ni `initialize`.
- Outils : `browser_annotations_list`, `browser_annotation_get` (Markdown + `structuredContent` + `image/png` base64), `browser_annotation_claim` (bail 30 min, renouvelable avec `claim_token`), `browser_annotation_resolve`, `browser_annotation_release`. `inputSchema` stricts (`additionalProperties:false`), `outputSchema`, annotations `readOnlyHint`/`destructiveHint:false`. Erreurs d'arguments/métier → `isError:true` ; outil inconnu, requête mal formée → erreur JSON-RPC.
- `readers\<client>.json` écrit à `initialize`/`server/discover`/appel d'outil, au plus une fois toutes les 10 s par client. `agent_label` par défaut : `clientInfo.title`, sinon `clientInfo.name`.

Configuration d'un client (exemple, preview portable) :

```json
{"mcpServers": {"browser-annotations-preview": {"command": "C:\\…\\build\\annotations-preview\\browser-annotations-mcp.exe"}}}
```

```toml
[mcp_servers.browser-annotations-preview]
command = 'C:\…\build\annotations-preview\browser-annotations-mcp.exe'
```

## Limites connues (pour l'intégrateur)

- Le paquet de release (`tools/release/package.py`, `update-package`) ne connaît pas encore le bridge : il n'est donc pas construit en `--channel release` (sinon `package.py` refuserait le fichier). Un dossier `annotations\` à côté d'un `browser.exe` portable serait aussi refusé par `package.py` (données d'exécution non listées dans `RUNTIME_DATA`) : à ajouter lors de l'intégration.
- Le contrôle du plafond de 200 Mio ne parcourt le magasin que lorsque le nombre d'entrées pourrait l'atteindre (≥ ~110 entrées, chaque entrée étant bornée à ~1,8 Mio par nos écritures).
- Les liens symboliques de fichiers ne sont testés que si le mode développeur Windows est actif (sinon le cas est signalé « skipped ») ; jonctions et liens physiques sont toujours testés.

---

# V2-A — regroupement par site, historique, connexion des agents (14 novembre 2026)

Contrat : `plan-annotations-mcp-local/design/annotations-v2-brief.md` §1, §2, §5. Les sections V1 ci-dessus restent vraies sauf ce qui suit.

## Magasin par site

- **Clé = origine** `http(s)://hôte[:port]` (`SiteOrigin(url)` : minuscules, port par défaut et identifiants retirés, même règle que `AnnotationHost::OriginOf` ; tout autre schéma → `""`, affiché « Autres pages »).
- **Dossier** `SiteKey(origine)` = partie lisible `hôte[-port]` (a-z0-9 et `-`, ≤ 40) + `-` + 8 hexa d'un FNV-1a 64 bits replié de l'origine, ex. `localhost-5173-1f2e3d4c`. Déterministe entre processus et versions ; `run_tests.py` en a une copie Python indépendante qui retrouve le `shot.png` écrit par le C++ (vérification croisée). Le hachage distingue `http`/`https` et `a.b`/`a-b`. `IsValidSiteKey` : `[a-z0-9][a-z0-9-]{0,63}`, ni `drafts`/`readers`/`trash`, ni nom de périphérique.
- **Schéma** `browser-annotation/2` : `"site": "<origine>"`, plus de `project`. `projects.json` n'est plus lu ni écrit.
- **Lecture tolérante V1** (pas de migration) : un dossier de projet V1 (`boutique\`) est un dossier comme un autre ; ses entrées `browser-annotation/1` sont lues avec `site` = origine de leur URL et `siteKey` = nom du dossier. Filtre par origine = dossier V2 de l'origine + dossiers V1 (entrées filtrées par `site`). Une entrée V2 rangée dans un dossier qui n'est pas le sien est ignorée (comptée `skipped`). Les agents peuvent réserver/résoudre une entrée V1 ; son fichier reste en V1.
- Toute méthode `Store` qui prend `site` accepte une origine (ou n'importe quelle URL du site) **ou** une clé de dossier (`Entry.annotation.siteKey`).
- `MaxAnnotationsPerSite` = 500 (l'ancien nom reste un alias).

## API ajoutée pour le panneau Historique (`annotations.h`)

```cpp
std::string SiteOrigin(std::string_view urlOrOrigin);   // pures
std::string SiteKey(std::string_view origin);
std::string SiteLabel(std::string_view origin);         // "example.com", "localhost:5173", "Autres pages"
bool IsSiteOrigin(std::string_view s);
bool IsValidSiteKey(std::string_view key);
enum class Progress { Any, InProgress, Done };          // En cours = open+claimed ; Terminées = resolved+needs_info+dismissed
bool IsDone(Status s);
struct ListQuery { std::string site; std::optional<Status> status; Progress progress = Progress::Any;
                   bool drafts; bool newestFirst; std::size_t limit; std::string cursor; };
struct SiteGroup { std::string site, siteKey, label; std::vector<Entry> entries; };
std::vector<SiteGroup> GroupBySite(std::vector<Entry> entries);  // ordre de 1re apparition (site le plus récent d'abord)
struct Annotation { std::string id, site, siteKey; std::string project /* compat V1 = siteKey */; ... };
struct Reader { std::string client, label, site; std::string project /* compat, toujours vide */; std::int64_t lastReadAt; };
struct PurgeReport { std::size_t drafts, closed, tempFiles, trash; };
constexpr std::int64_t TrashRetentionSeconds = 120;
// Store (E/S bloquantes, hors fil UI)
Outcome Trash(const std::string& site, const std::string& id) const;         // « Supprimer »
Outcome Restore(const std::string& site, const std::string& id) const;       // « Annuler » (toast 6 s)
Outcome DiscardTrash(const std::string& site, const std::string& id) const;  // toast fermé
std::size_t CountClaimed() const;                                            // compteur du lanceur
Outcome DeleteSite(const std::string& site) const;                           // DeleteProject = alias
PurgeReport Purge(std::int64_t closedRetentionSeconds = 0) const;   // + terminées (needs_info inclus) > 30 j (ou le réglage 7/30/90 j), corbeille > 120 s
```

Liste paginée tous sites, récente d'abord : `ListQuery q; q.progress = Progress::Done; q.newestFirst = true; q.limit = 30;` puis `q.cursor = page.nextCursor` ; regrouper chaque page avec `GroupBySite`.

**Suppression + « Annuler » : corbeille temporaire** (et non suppression différée). `Trash` écrit `trashed.json` (heure de suppression) puis renomme `<siteKey>\<id>` en `trash\<siteKey>\<id>` (un seul `MoveFileExW`, même volume). Raisons : (1) l'annotation disparaît **immédiatement** pour les agents (`list`/`get`/`claim` → introuvable), alors qu'une suppression différée la laisserait servie pendant 6 s ; (2) « Annuler » rend l'entrée intacte (état, bail en cours, capture) par le renommage inverse ; (3) un plantage pendant les 6 s ne laisse rien durablement : `Purge()` vide la corbeille après 120 s (y compris un élément sans marqueur). Si un agent a un fichier de l'entrée ouvert, le renommage échoue → `Busy` après ~1 s de nouvelles tentatives (entrée intacte, l'UI réessaie).

**Compatibilité source V1** : retirée par V2-C (intégration) — `Project`, `LoadProjects()`, `SaveProjects()`, `MaxProjects`, `MaxOriginsPerProject`, `Annotation::project` et `Reader::project` n'existent plus ; `IsValidProjectId` reste (lecture tolérante des dossiers V1).

## Bridge

- `--site <origine>` (ou une URL du site) restreint le serveur à ce site. `--project` : **déprécié** ; une valeur qui est une origine agit comme `--site`, sinon elle est **ignorée** avec un avertissement sur stderr (un identifiant de projet V1 ne désigne pas un site de façon fiable : mieux vaut ne rien filtrer que filtrer faux).
- `browser_annotations_list` accepte `site` (origine ou URL, `maxLength` 2048) ; `project` n'existe plus (« Argument inconnu : project. »). Les sorties portent `site` au lieu de `project`. `readers\<client>.json` porte `site` (origine de `--site` ou `null`).
- Transcriptions : `legacy-2025-11-25` (filtre `site` par URL, entrée V1 servie par site, erreurs), `modern-2026-07-28`, `site-restricted` (`--site` donné en URL majuscules), `project-deprecated`. Golden `tools-list.json` régénéré (diff : `project` → `site`).

## Connexion des agents (`src/agent-connect.h/.cpp`, sans UI)

API (namespace `AgentConnect`) :

```cpp
struct Environment { std::wstring appData, localAppData, userProfile, codexHome, path;
                     std::function<unsigned(const std::wstring&)> driveType; std::function<std::int64_t()> now;
                     std::function<bool()> mcpbAssociated; std::function<bool(const std::wstring&)> openFile;
                     static Environment FromProcess(); };
struct Target { std::wstring bridgeExe, storeRoot; std::string serverName = "browser-annotations"; std::string version; };
enum class Client { Claude, Codex };
enum class Result { Connected, AlreadyConnected, PendingInstall, Disconnected, NotConnected, ManualStep, NoClient, InvalidConfig, Failed };
struct Report { Result result; std::string message; std::wstring file, backup; bool usedCli; bool ok() const; };
struct Detection { bool claude, codex; std::wstring claudeConfig, codexConfig, codexCli; bool mcpbAssociated; };
struct ConfiguredEntry { bool present; std::wstring command; std::vector<std::wstring> args; };
struct Status { bool configured, extensionInstalled, stalePath, recentlyRead; std::int64_t lastReadAt;
                std::string readerLabel; bool removableDrive; bool connected() const; };

Detection Detect(const Environment&);
std::wstring ClaudeConfigDir(const Environment&), ClaudeConfigPath(...), CodexHome(...), CodexConfigPath(...), FindCodexCli(...);
std::wstring StableBridgePath(const std::wstring& browserExe, const std::wstring& installRoot = L"");
bool IsOnRemovableDrive(const Environment&, const std::wstring& path);
std::string ClaudeJsonSnippet(const Target&); std::string CodexTomlSnippet(const Target&); std::wstring CodexAddCommandLine(const Target&);
Report ConnectClaude(const Environment&, const Target&, const std::wstring& workDir);  // .mcpb ouvert, sinon fusion JSON
Report DisconnectClaude(const Environment&, const Target&);
Report ConnectCodex(const Environment&, const Target&);
Report DisconnectCodex(const Environment&, const Target&);
Status ClientStatus(const Environment&, Client, const Target&, const std::vector<BrowserAnnotations::Reader>&,
                    std::int64_t recentSeconds = 7 * 86400);
// Niveau fichier (tests, « Reconnecter ») : MergeClaudeConfig, RemoveClaudeConfig, ReadClaudeEntry, ClaudeExtensionInstalled,
// MergeCodexToml, RemoveCodexToml, ReadCodexEntry, McpbManifest, BuildMcpb. Briques pures : ZipStored, Crc32, QuoteArg.
```

Intégration attendue : `Target{StableBridgePath(exe du navigateur), StoreRootForDataDir(dataDir), "browser-annotations", version}` ; appels **hors fil UI** (E/S + processus Codex jusqu'à 20 s) ; `Report.message` est en français, prêt pour l'UI. Correspondance planches : `PendingInstall` = 1.2 (« Cliquez sur Installer dans Claude ») ; `Connected` = 1.3 (« Redémarrez … ») ; `InvalidConfig`/`Failed` = 1.4 (rien n'a été écrit) ; snippets = 1.5 (Copier JSON/TOML) ; `Status.removableDrive` = 1.6 (« Reconnecter » = rappeler `Connect*` avec le chemin courant ; `stalePath` signale une entrée qui pointe ailleurs).

Choix et vérifications :

- **Détection par dossiers de configuration uniquement** : Claude = `%APPDATA%\Claude`, ou la copie MSIX `%LOCALAPPDATA%\Packages\Claude_*\LocalCache\Roaming\Claude` si elle existe (celle que lit Claude installé depuis le Store) ; Codex = `%CODEX_HOME%`, sinon `%USERPROFILE%\.codex`. La détection ne crée aucun dossier.
- **`.mcpb`** : zip avec `manifest.json` (`manifest_version` `"0.3"` = version courante du MANIFEST.md officiel, relu le 14/11/2026 ; requis `name`, `version`, `description`, `author.name`, `server` ; `server.type: "binary"`, `entry_point: "server/browser-annotations-mcp.exe"`, `mcp_config.command: "${__dirname}/server/browser-annotations-mcp"` **sans** `.exe` car le MANIFEST dit « For binaries, apps will automatically append .exe on Windows » ; `args: ["--store", <chemin réel>]`, `tools`, `compatibility.platforms: ["win32"]`). Ouvert par `ShellExecuteExW("open")` si `.mcpb` a une association (`AssocQueryStringW` exécutable / commande / DelegateExecute) ; sinon, ou si l'ouverture échoue, **fusion** dans `claude_desktop_config.json`.
- **Zip interne** (méthode 0 « stored », CRC-32, noms UTF-8, ~60 lignes) : Win32 n'a pas d'API publique d'écriture de zip (le dossier compressé du Shell est COM, asynchrone, avec UI possible ; `cabinet.dll` fait du CAB/MSZIP, pas un zip) ; la méthode 0 est lue par tout lecteur zip, et compresser un exe de 1,2 Mo destiné à un fichier local éphémère n'apporte rien. Vérifié par `zipfile` de Python (CRC, entrées, manifest) dans `run_tests.py`.
- **Fusion JSON** : le texte existant est **découpé, jamais re-sérialisé** (parseur strict pour valider + scanner de positions) : autres clés, ordre, nombres (`12345678901234567890`, `1.50`, `1E+2`), échappements, BOM, CRLF et indentation conservés octet pour octet ; connecter puis déconnecter redonne le fichier d'origine (testé). JSON invalide (virgule finale, commentaire, clé dupliquée, tronqué, racine non objet) ou `mcpServers` non objet → `InvalidConfig`, aucune écriture, aucune sauvegarde. Le résultat est re-parsé avant écriture (filet). Sauvegarde `<fichier>.browser-annotations-AAAAMMJJ-HHMMSS[-n].bak` (UTC) avant toute écriture, puis temporaire + `MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH)` ; si l'écriture échoue, la sauvegarde devenue inutile est retirée. Déjà connecté à l'identique → rien n'est écrit.
- **Codex — CLI** : syntaxe **vérifiée** sur la CLI présente (`codex-cli 0.166.0`, `codex mcp add --help` : `codex mcp add [OPTIONS] <NAME> (--url <URL> | -- <COMMAND>...)`, `codex mcp remove <NAME>`) et essayée à la main dans un `CODEX_HOME` temporaire : elle conserve commentaires et autres tables et remplace une entrée existante. Utilisée seulement si **`codex.exe`** est dans une entrée **absolue** du `PATH` ; lancée cachée avec `CODEX_HOME` explicite (même valeur que la détection), stdio sur NUL, 20 s max ; son résultat est **relu et vérifié** dans `config.toml`, sinon repli sur l'édition directe (une seule sauvegarde, prise avant la CLI). Un `codex.cmd`/`.ps1` (shim npm) n'est **pas** utilisé : il passe par cmd.exe/PowerShell, dont l'interprétation de `% ^ & !` dans un chemin ne peut pas être neutralisée de façon sûre.
- **Codex — édition directe** : scanner TOML (4 sortes de chaînes, commentaires, tableaux et tables en ligne multi-lignes) qui ne réécrit que **nos** blocs `[mcp_servers.browser-annotations]` (+ sous-tables) ; tout le reste est intact octet pour octet (aller-retour testé, y compris un faux en-tête dans une chaîne multi-ligne). Notre entrée sous une autre forme (table en ligne `mcp_servers = {…}`, clés pointées, `[mcp_servers]` + `browser-annotations = {…}`, `[[mcp_servers]]`) ou fichier non analysable → `InvalidConfig`, rien n'est écrit. Valeurs en chaînes de base échappées (chemins avec `'`, espaces, accents).
- **Déconnecter** retire seulement notre entrée (JSON : notre membre, `mcpServers` gardé ; TOML : nos blocs). Extension installée depuis le `.mcpb` (`Claude Extensions\*\manifest.json` dont `name` est le nôtre, lecture seule) → `ManualStep` (« Désinstallez … dans Claude › Réglages › Extensions ») : on ne modifie jamais les fichiers internes de Claude.
- **État connecté** : entrée de configuration présente (ou extension installée) **ou** `readers\<client>.json` de moins de 7 jours (client contenant `claude` / `codex`) ; `stalePath` si l'entrée pointe vers un autre exe ou magasin.
- **Disque amovible** : `GetDriveTypeW(racine)` = `DRIVE_REMOVABLE` pour l'exe du bridge ou le magasin (UNC : non).
- **Chemin stable du bridge** : navigateur installé (`<installRoot>\versions\<M.m.B>\browser.exe`) → `<installRoot>\bin\browser-annotations-mcp.exe` s'il existe (survit aux mises à jour), sinon à côté de `browser.exe` (portable, preview).

Tests (`tests/annotations/agent_connect_test.cpp` + `fake_codex.cpp`, 187 vérifications) : le processus redirige `APPDATA`, `LOCALAPPDATA`, `USERPROFILE`, `CODEX_HOME` et `PATH` vers `build\annotations-test\tmp\agent-connect\…` et **s'arrête** si l'une pointe ailleurs ; `run_tests.py` relève l'empreinte (taille, mtime, SHA-256) des vrais `claude_desktop_config.json` et `.codex\config.toml`, la liste de `Claude Extensions` et l'absence de nos `.bak`/`.tmp-` avant et après tout le run.

## Limites / non vérifié

- **Installation réelle du `.mcpb` dans Claude Desktop** : non faite (interdit de toucher Claude) ; sur ce poste `.mcpb` n'a **pas** d'association (`assoc .mcpb` : aucune), donc le parcours réel y serait la fusion JSON. Le bundle est validé contre le MANIFEST (champs, types) et par `zipfile`, pas par `mcpb validate` (CLI Node non installée).
- L'emplacement où Claude décompresse les extensions (`Claude Extensions\<id>`) est déduit : la détection lit `manifest.json` dans **tous** les sous-dossiers ; si Claude change ce format, `extensionInstalled` reste faux et l'état « connecté » repose sur `readers`.
- La vraie `codex mcp add` n'est pas exécutée par les tests (faux `codex.exe` dans un `PATH` temporaire) ; elle a été essayée une fois à la main dans un `CODEX_HOME` temporaire, jamais sur `C:\Users\<user>\.codex`.
- Le build preview a été fait pendant que l'autre worker modifiait les en-têtes `annotation-*.h` dans le même worktree : il prouve que `agent-connect.cpp` et le magasin V2 compilent avec l'UI de ce moment, pas avec sa version finale.


---

# V3-A — l'agent prend en charge et termine (14 novembre 2026)

Retours §5 : Claude avait lu l'annotation sans jamais appeler `claim` / `resolve`.

- **Instructions** (`initialize` et `server/discover`) directives : lire (`list`, `get`), **avant** de modifier `browser_annotation_claim`, **après** `browser_annotation_resolve` avec `outcome` et un `summary` d'une phrase montré à l'utilisateur, et s'il ne peut pas la traiter (pas d'accès au code, fichier introuvable…) `browser_annotation_release` avec `reason`. Descriptions des cinq outils alignées (avant / après / sinon).
- **`browser_annotation_release` accepte `reason`** (facultatif, ≤ 280 caractères) : `Store::Release(…, reason)` l'écrit comme résumé de l'événement `claimed → open` ; le navigateur en fait « … n'a pas pu la traiter » avec la raison citée.
- **Prompt MCP « Traiter mes annotations »** (`traiter_mes_annotations`, argument `site` facultatif) : capacité `prompts` (`listChanged: false`) annoncée dans les deux ères ; `prompts/list` et `prompts/get` servis comme `tools/*` (avec `_meta` 2026-07-28, ou après `initialize`) ; prompt ou argument inconnu → `-32602`.
- **Lectures par annotation** : `readers\<client>.json` porte `seen` = `{id: date}` des annotations que ce client a vraiment lues (`list` qui les lui montre, `get`), 40 au plus (les plus récentes), écrit aussitôt quand un id est nouveau (sinon toujours ≤ 1 écriture / 10 s). `lastReadAt` seul ne suffisait pas (mis à jour aussi par `initialize` / `tools/list`). Lecture tolérante (fichier V2 sans `seen`, membres invalides ignorés). `Reader::seen`, `Reader::SeenAt(id)`.
- Transcriptions mises à jour (`legacy-2025-11-25` : instructions, capacités, `release` avec `reason`, `reason` non chaîne, `prompts/list`, `prompts/get` avec et sans `site`, prompt et argument inconnus, `seen` vérifié dans `readers\claude-ai.json` ; `modern-2026-07-28` : instructions, capacités, `prompts/*` avec `_meta`, `prompts/list` sans `_meta` refusé). Golden `tools-list.json` régénéré (descriptions + `reason`).
