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
