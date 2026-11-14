# Annotations V1 — magasin et bridge MCP

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
