# Plan B — raccordement C++ des annotations au canal release (côté « writer couleurs »)

Branche `VALXXII/annotations-release-wiring`, empilée sur `e204fd6` (PR72, accents). Contrat : plan B du worker
packaging (`public-download-plan-b.md`, items 5 et 7) et réponse root du 2026-11-14. Fichiers : `src/agent-connect.*`,
`src/annotation-mode.h`, `src/main.cpp` + tests. Rien dans `tools/release`, `update-helper`, `mcp-bridge`,
`annotations.cpp/.h` (packaging), ni Paper, ni configs agents réelles.

**Le gate n'est pas activé** : une build `--channel release` reste sans annotations tant que la chaîne de build ne
passe pas `-DBROWSER_RELEASE_ANNOTATIONS`, ce que `build.py` (packaging) ne fera que si le pont est dans
`assets/bin`.

## Changements

| Point | Avant | Après |
|---|---|---|
| Gate `src/main.cpp:62` | `HAS_BUILD_INFO && !CHANNEL_RELEASE && !NO_ANNOTATIONS` | `HAS_BUILD_INFO && !NO_ANNOTATIONS && (!CHANNEL_RELEASE \|\| RELEASE_ANNOTATIONS)` |
| Pont, navigateur installé (`<root>\versions\<v>\browser.exe`) | `<root>\bin\…` s'il existe, **sinon la copie versionnée** (élaguée 2 mises à jour plus tard) | **toujours** `<root>\bin\browser-annotations-mcp.exe`, présent ou non |
| Pont, portable | voisin de `browser.exe` | release : `<exe>\assets\bin\…` ; dev/preview : voisin (inchangé) |
| Avant d'écrire une config Claude/Codex | aucun contrôle (le `.mcpb` vérifiait « MZ ») | `BridgeUsable` : chemin disque local absolu, dossier parent non-reparse, fichier ordinaire ouvert sans suivre de reparse point, PE32+ x64 sous-système console ; sinon `Failed` « introuvable … rien n'a été modifié », avant tout `.mcpb`, CLI codex, sauvegarde ou écriture |
| Nom de serveur MCP | `browser-annotations-preview` en dur | release `browser-annotations`, dev/preview `browser-annotations-preview` |
| `source.channel` des annotations | `"preview"` en dur | `BROWSER_CHANNEL_TEXT` ; `"preview"` conservé pour `BROWSER_PREVIEW` et une build non estampillée |

Pas de réécriture spontanée de config : une config existante qui pointe ailleurs reste signalée `stalePath`
(« Reconnecter »), comme avant. La connexion échouée se réessaie depuis la modale (bouton existant). L'annotation
locale (mode, note, capture, magasin) n'en dépend pas.

## Tests exécutés

- `python -I tests/annotations/run_tests.py --quick` : toutes les builds et tests passent ; `agent_connect_test`
  249 vérifications, 0 échec (`02-…`). Nouveaux cas : installé + copie versionnée présente + `bin` absent → `bin`
  (pas de repli), portable `assets\bin` / voisin, autre racine `versions` ; `BridgeUsable` refuse texte « MZ », vide,
  sous-système GUI, x86, non-PE, absent, dossier, chemin relatif, UNC, `..`, dossier jonction (créée réellement) ;
  `ConnectClaude`/`ConnectCodex` avec pont absent : `Failed`, config, journal du faux codex et `.mcpb` inchangés,
  puis même cible acceptée une fois le pont publié.
- Gate au préprocesseur sur `main.cpp` (`03-…`) : dev → défini ; release sans opt-in → non ; release + opt-in →
  défini ; release + opt-in + `BROWSER_NO_ANNOTATIONS` → non.
- `main.cpp` compilé (`-fsyntax-only -Wall -Wextra`) en release + opt-in : 0 avertissement (`04-…`).
- Build dev complète : OK (`05-…`).
- E2E application réelle couleurs (`tests/annotations/profile_color_e2e.py`) sur la build B : 0 échec (`06b-…`).

### Premiers échecs conservés

- `01-…` : `agent_connect_test.cpp:554` — mon test exigeait l'absence de `browser-annotations.mcpb`, laissé par le
  test de flux Claude précédent ; corrigé dans le test (fichier supprimé avant, absence exigée après).
- `06a-…` : l'E2E pilotait le mode par Ctrl+Maj+A posté avec un état clavier partagé ; une autre application étant au
  premier plan, la touche n'arrivait plus (même échec sur la build PR72 qui passait plus tôt : environnement, pas
  régression). L'E2E clique désormais le bouton « Annoter » par messages postés à SA fenêtre, sans prendre le premier
  plan ni toucher la session de l'utilisateur. (Correctif de test seul ; reportable sur PR72 si root le souhaite.)

## Limites / dépendances

- Activation réelle : uniquement quand `build.py` passe `-DBROWSER_RELEASE_ANNOTATIONS` (packaging, items 1–6 :
  pont dans `assets/bin`, publication `<root>\bin` par le helper avant lancement, migration du magasin portable,
  validateur v0.3.239). Test du binaire release exact à faire sur le paquet complet.
- Marqueur pour `run_all --app` : chaînes UTF-16 `ANNOTATE …` (présentes seulement si le mode est compilé).
- Non testé en E2E : connexion refusée sur une vraie installation sans `<root>\bin` (couvert en unitaire avec une
  arborescence `versions`/`bin` réelle) ; la modale affiche le message d'échec existant.
- La section « Réglage manuel » de la modale montre toujours le chemin stable attendu, même si le pont est absent.

## Correctif de revue PR72 répercuté (merge c7bc8c2)

Le commit ac6a5ef (plancher BETA/libellé sur toutes les lignes de fond) est fusionné dans cette branche sans réécriture. Re-vérifié sur la tête fusionnée : test pur 31/31, harnais natif 1376/0, suite annotations --quick OK (agent_connect 249/0), build dev OK, E2E couleurs réel 0 échec (`07a…07e`). Le gate reste inactif.

## Revue indépendante de 65adadd : deux P2 corrigés

1. **Débordement du décalage PE** : `pe + 24 + 70` était calculé sur 32 bits ; un fichier local de 128 octets « MZ » avec
   `e_lfanew = 0xFFFFFFF0` faisait planter le contrôle (0xC0000005). Correction : décalage en `size_t`, bornes vérifiées
   par soustraction après `pe <= taille lue` (`pe < 0x40 || pe > size || size - pe < 94`), avant tout accès.
2. **Lien au-dessus de `bin`** : seuls le dossier parent et le fichier étaient contrôlés ; un chemin
   `alias\bin\browser-annotations-mcp.exe` (jonction `alias -> real`) était accepté. Correction : orthographe unique
   (pas de `.`, `..`, composant vide, `/`, séparateur final), **chaque dossier** du chemin depuis le lecteur est contrôlé
   (existe, dossier, pas un point d'analyse), et le chemin final du handle ouvert (`GetFinalPathNameByHandle`) doit égaler
   la forme longue du chemin (contre un échange de dossier après le contrôle, un lecteur `subst` ou un alias).

Rouge/vert (sondes en processus séparés, `tests/annotations/bridge_probe.py`, équivalentes à celles de la revue) :
`08a` sur 65adadd — 4 échecs (2 jonctions acceptées, 2 plantages 0xC0000005) ; `08b` après correction — 0 échec (23 cas :
jonction à 1 et 2 niveaux au-dessus de `bin`, dossier immédiat lien, décalages extrêmes/limites, fichiers tronqués).
`agent_connect_test` : `08c` sur l'ancien `agent-connect.cpp` — échecs puis segfault (exit 139) ; `08d` après — 288
vérifications, 0 échec, dont connexion Claude/Codex refusée via jonction ancêtre sans config, sauvegarde, CLI ni `.mcpb`.

Limites : contrôle d'en-tête seulement (pas de preuve de chargeabilité ni d'identité du fichier au démarrage du client) ;
un dossier utilisateur redirigé par jonction, un lecteur `subst` ou un dossier OneDrive « à la demande » (points
d'analyse) font désormais refuser la connexion — restriction voulue, message « passe par un lien ». `main.cpp` non
modifié par ce correctif (gate inchangé). Build/E2E application non refaits pour ce correctif ciblé.
