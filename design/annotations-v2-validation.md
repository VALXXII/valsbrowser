# Annotations V2-C — intégration navigateur, build preview, sandbox : validation

Branche `VALXXII/annotations-v1` (après V2-A 4fff084/5486ff1, V2-B 99e8514 et sa révision 1.1 cf9e7c3). Contrat : `plan-annotations-mcp-local/design/annotations-v2-brief.md` + planches « Spéc » de Paper (fichier `01M6Q7YT14ZKMGET54ASPNGQEC`, page `p-H-0`, relues le 14 novembre 2026 en lecture seule : `10X1-0` Lancement, `ZJQ-0` Connexion, `10IT-0` Annotations + capture rapide, `1167-0` Historique). Preuves du 14 novembre 2026, Windows 11 26200, affichage 125 % (DPI 120), runtime WebView2 157.

Légende : **Vérifié** = observé ici, preuve citée · **Non vérifié** = pas démontré, raison donnée.

## 1. Livré

| Fichier | Rôle |
| --- | --- |
| `src/annotation-mode.h` | Intégration réécrite pour V2 : modale de connexion au lancement (règle des 3 « Plus tard »), connexion/déconnexion via `agent-connect` hors fil UI, capture CDP au clic (repli `CapturePreview`), écriture immédiate + image qui rejoint, lanceur permanent, cartes 3.1 → 3.3 en direct, historique (lecture à l'ouverture, surveillance pendant l'ouverture), corbeille + « Annuler », Markdown complet, points d'entrée pour les Réglages. |
| `src/annotation-entry.h` (nouveau) | Points d'entrée pour la section Réglages « Annotations et agents » (worker Réglages) : `AnnotationOpenConnect(manual)`, `AnnotationConnectAgent(app, connect, done)`, `AnnotationAgentsStatus(done)`, `AnnotationOpenFolder()`, `AnnotationCount(done)`, `AnnotationClearAll(done)`, `AnnotationSettingsChanged()`. Stubs vides quand le mode est compilé hors (release). |
| `src/annotation-host.h` | Logique pure : `PlanClip` (zone CDP = élément + 24 px, coordonnées du document, limitée à la zone visible, échelle ≤ 1 600 px), `ClipParams`, `ScreenshotData`/`Base64Decode`, `Selection::seq/at`, `ElementLabel` (« Bouton « Payer 48,00 € » »), `AgeLabel`, `AgentDisplay`, `SinceLower`. Aides de projets V1 retirées. |
| `src/annotation-capture.h` | `ProcessClip` (PNG du moteur gardé tel quel s'il respecte 1 600 px / 1,5 Mio, sinon réduit), `Process` (repli viewport + recadrage), `Thumbnail` (vignettes de l'historique, « cover »). Thread pool uniquement. |
| `src/annotation-script.h` | Le cadre est masqué dans la tâche même du clic, avant le message ; `seq` et l'heure du clic voyagent avec la sélection. |
| `src/main.cpp` | Accroches : GUID du handler CDP, `AnnotateHistoryCommand` + Ctrl+Maj+H dans `Key()`, `#include "annotation-entry.h"`. (6 lignes au total avec V1 inchangé.) |
| `src/annotations.h/.cpp` | Shims V1 retirés (`Project`, `LoadProjects`, `SaveProjects`, `MaxProjects`, `MaxOriginsPerProject`, `Annotation::project`, `Reader::project`) ; `Purge(closedRetentionSeconds)` pour le réglage 7/30/90 jours. |
| `src/annotation-connect.h` (V2-B) | Ajouts d'intégration : « Déconnecter » sur une ligne connectée (`setup.disconnectable`, `hooks.disconnect`, UIA `AnnotationDisconnectClaude/Codex`), ouverture sur le repli manuel (`setup.manual`), **correction UIA** (clés 501+ : `DialogKey = 1` = `CHeader = 1` faisait un arbre UIA récursif sans aucun bouton). |
| `src/annotation-history.h`, `src/annotation-launcher.h` (V2-B) | `ShowDetail(id)` / `ShowHistoryDetail(id)` (carte « Voir l'annotation »), `copyEntry` (Markdown complet du magasin), **même correction UIA** (`PanelKey = 1` = `HClose = 1`). `annotation-onboarding.h` supprimé, `annotation-ui.h` ne l'inclut plus. |
| `tests/annotations/host_test.cpp` | 77 vérifications (zone CDP, base64, `seq`/`at`, libellés, âges). `store_test.cpp`, `claim_race.cpp`, `agent_connect_test.cpp` adaptés au retrait des shims ; rétention configurable testée. |
| `tests/annotations/sandbox_qa_v2.py`, `uia.ps1`, `idle_measure.py`, `idle_probe.py`, `own_warnings.py` | Parcours réel automatisé, aide UI Automation, mesure de repos V2, sonde par thread, preuve « 0 avertissement » de nos sources. |
| `tests/update-integration/e2e_install.py` | Le fixture hôte (main.cpp estampillé dev) lie aussi `src/agent-connect.cpp`. |
| `sandbox/annotations/` | `run-sandbox.py` (une commande, agents redirigés par défaut), `try-mcp.py` (`--site`, list/get/claim/resolve/release/wait/demo), `README.md` réécrit. |

### Choix et écarts

- **Capture au clic, sans attendre deux images.** Le script masque le cadre dans la tâche du clic puis poste la sélection ; l'hôte demande `Page.captureScreenshot` dès réception, **avant** de construire le panneau (le moteur travaille pendant la mise en page native). Vérifié : un changement de style fait dans la tâche JS précédente est absent de la capture suivante 10/10, et aucun pixel de l'accent du cadre (`rgb(62,103,255)`) dans les `shot.png` du parcours. Un script qui n'envoie pas `seq` retombe sur l'ancien chemin (« hide » → deux images → « hidden », minuterie 800 ms). Mesuré avec `--remote-debugging-port` : aucun événement `resize` dans la page pendant les captures (pas de clignotement par émulation).
- **Compteur du lanceur** : lu **une fois** à son premier affichage (un `CountClaimed` hors fil UI, seulement si le dossier du magasin existe), puis à chaque lecture déjà faite (mode, historique, écriture). Sinon le magasin n'est lu qu'à l'ouverture de l'historique, comme demandé.
- **Surveillance du magasin** (`ReadDirectoryChangesW`, un thread bloqué) seulement pendant : mode actif, historique ouvert, modale ouverte, ou un agent au travail (compteur > 0 : l'anneau tourne de toute façon). C'est elle qui fait passer les cartes 3.2 → 3.3 en direct.
- **Rétention** : purge à la première ouverture de l'historique ou à la première activation de la session (jamais au lancement ni sur minuterie : « rien au repos » l'emporte sur « à l'ouverture suivante du navigateur » de la planche Historique). Durée = réglage `annotationRetentionDays` s'il existe, sinon 30 jours.
- **Nom du serveur** : `browser-annotations-preview` (builds dev/preview ; le mode n'existe pas en release), pour ne jamais écraser l'entrée d'une installation.
- **Libellé « en cours » de Claude** : « Connexion à Claude… » ; « Cliquez sur Installer dans Claude » seulement si un `.mcpb` a vraiment été ouvert (sur ce poste : jamais, pas d'association).
- **Cible hors écran** (sélection défilée) : le panneau se place comme pour le centre de la page, pour que la pastille « Capture impossible » reste visible.
- **Réglages** : le périmètre « section Réglages » a été confié à un autre worker en cours de tâche. J'avais commencé dans `src/settings.h`, `src/settings-panel.h`, `tests/settings_test.cpp` : **ces changements ont été retirés** (`git checkout` de ces trois fichiers à l'annonce). Le mode lit `annotationLauncher`, `annotationConnectPrompt`, `annotationConnectDeclines`, `annotationRetentionDays` par **détection à la compilation** (valeurs par défaut si une clé manque) : pas de dépendance de source dans un sens ni dans l'autre.

## 2. Build

```
python tools/release/build.py --out build/annotations-preview --channel dev --preview
```
**Vérifié** : `Build OK: …\build\annotations-preview\browser.exe (0.3.248, 4e167a07a73d)`, canal `dev` (icône preview), `--preview` (moteur de mise à jour coupé, profil/réglages/magasin propres au dossier), `browser-annotations-mcp.exe` à côté (1 172 865 o), `browser.exe` 10 546 493 o (référence `--no-annotations` : 9 542 437 o).

Avertissements : le build de l'arbre de travail partagé affiche **3 `-Wswitch`** dans `SettingChanged` (`Change::AnnotationLauncher/ConnectPrompt/Retention`), valeurs ajoutées par V2-D (4e167a0) dont le branchement dans `main.cpp` est prévu après V2-C. **Nos sources : 0 avertissement** — `python tests/annotations/own_warnings.py` compile `main.cpp` (mode compilé, `-O2 -c`, drapeaux exacts de `build.py`) avec `settings.h`/`settings-panel.h` d'avant V2-D : `exit 0, 0 warnings, 0 errors` (`build/annotations-v2-qa/own-warnings.log`).

## 3. Tests

| Commande | Résultat |
| --- | --- |
| `python -I tests/annotations/run_tests.py` | **Vérifié** : exit 0 « annotations: all builds and tests passed » — store_test 2 530, json_fuzz 156 715, **host_test 77**, **agent_connect_test 199** (dont `TestGuard`), claim_race 15 (2 × 1 000 + 8 threads), transcriptions MCP, vrais fichiers Claude/Codex inchangés (empreintes du runner). `build/v2c-run_tests.log`. |
| `python tests/annotations-ui/run.py` (harnais V2-B) | **Vérifié** : « 440 checks, 0 failures; 79 captures » (après mes ajouts dans `annotation-connect/history/launcher.h`). `build/annotations-v2-qa/ui-harness.log`. |
| `python tests/update-integration/run_all.py --app build/annotations-preview` | **Vérifié** : exit 0, 42 étapes « ok » (dont `shortcuts_test`, `accessibility_test`, `navigation_errors_test`, `context_menu_hover_test`, `tab_drag_test`, `split_ui_test`, `bookmarks_ui_test`, update-core, update-toast, reprise WebView2, outils de release, **intégration hôte `main.cpp` estampillé dev (mode compilé, avec `agent-connect.cpp`) + installation simulée**, contrôles statiques 0.3.248). `build/annotations-v2-qa/run_all.log`. |
| `test.ps1` (smoke du vrai navigateur, build `build/smoke-v2` canal dev, donc mode et lanceur compilés) | **Vérifié** : exit 0, `SMOKE PASS: blank #171717 home, bundled font families, 11 exported SVG icons, 2 tabs, HTTPS, …` (+ tests session/settings/address, favoris). `build/annotations-v2-qa/test-ps1.log`. |
| `test-elements-integration.ps1` | **Vérifié** : exit 0, `PASS elements integration: …` (routeur `PageMessage`). |
| Accessibilité / raccourcis | **Vérifié** via `run_all` (`accessibility_test`, `shortcuts_test`) ; UIA réel des surfaces du mode dans le parcours §4 (tous les boutons de la modale, de la note, des cartes, de l'historique et du toast invoqués par UI Automation hors processus). |
| `python tests/annotations/sandbox_qa_v2.py run` | **Vérifié** : **46 étapes, 0 échec** (§4). |

## 4. Parcours réel sur la sandbox (preview)

Pilote : `python tests/annotations/sandbox_qa_v2.py run` sur une **copie** du preview (`build/annotations-qa-app`, profil/réglages/magasin propres), port de débogage WebView2 en loopback pour cette copie seulement (entrée dans la page par `Input.dispatchMouseEvent`), UI Automation et messages postés à **nos** fenêtres pour le chrome natif, captures `PrintWindow`. Connexion des agents redirigée vers `build/annotations-v2-qa/agent-configs` (`BROWSER_ANNOTATIONS_CONNECT_ROOT`). Rapport brut : `build/annotations-v2-qa/report.json`, journal `build/annotations-qa-app/prototype.log`.

| Étape (planche) | Preuve | Statut |
| --- | --- | --- |
| Lanceur visible au démarrage, magasin non touché, aucune ligne `ANNOTATE` avant usage | UIA `AnnotationLauncher` ; pas de dossier `annotations\` ; `0.1-launcher-at-rest.png` | Vérifié |
| 1.1 Activation sans agent → modale aussitôt, mode actif derrière | journal `ANNOTATE connect modal opened (launch, writes redirected…)` + `mode on` ; `1.1-launch-modal.png` (en-tête révisé par V2-B cf9e7c3 : croix = « Plus tard ») | Vérifié |
| 1.8 « Plus tard » → rappel dans le bandeau, compteur | UIA `AnnotationConnectReminder` ; journal `« Plus tard » (1/3)` ; fichier `settings` : `annotation-connect-declines=1` (clé V2-D lue par détection) ; `1.8-band-reminder.png` | Vérifié |
| Même session : la modale ne revient pas | mode quitté puis relancé : aucun `connect modal opened`, rappel présent | Vérifié |
| Réouverture aux lancements suivants jusqu'à 3 refus | au relancement de la copie QA avec `declines=1`, la modale s'est rouverte (observé pendant la mise au point, capture de contrôle non conservée) ; le seuil 3 et `annotationConnectPrompt=0` : logique `Prefs::Declines < 3 && Prefs::Prompt` | Vérifié (1→rouverte) ; seuil 3 **Non vérifié** en réel (couvert par la logique et les tests V2-D) |
| 2.1 Annotation d'un élément : capture au clic | `360x116 cdp`, image en mémoire **56 ms** après le clic, affichée 60 ms ; `2.1-note-panel.png` | Vérifié |
| 2.2 Pastille « Ce que l'agent verra » | capture, Page, Élément « Bouton « Payer 48,00 € » », Style ; `2.2-pill-expanded.png` | Vérifié |
| « Ajouter » écrit aussitôt avec l'image | `written … with shot in 17 ms` ; `annotation.json` `browser-annotation/2`, `site` = `http://127.0.0.1:5173`, `shot.png` 360×116 = (240+48)×(44+48)×1,25 | Vérifié |
| 2.4 « Ajouter » avant que l'image soit prête | texte + Entrée postés < 30 ms après le clic : `written … (shot joins when ready) in 33 ms` puis `capture joined … ok`, `shot.png` présent | Vérifié |
| Annuler (Échap) = image jetée, rien d'écrit | aucune écriture, une seule annotation dans le magasin | Vérifié |
| 2.3 Capture impossible | sélection défilée hors écran entre le clic droit et « Annoter la sélection » : `capture unavailable (plan): outside the viewport`, UIA `AnnotationCaptureUnavailable`, l'annotation part `without shot` ; `2.3-capture-unavailable.png` | Vérifié |
| Le cadre n'apparaît jamais dans l'image | 0 pixel de l'accent `rgb(62,103,255)` dans les 3 `shot.png` du parcours (la page n'utilise pas cette couleur) | Vérifié |
| Mot de passe / e-mail jamais dans le magasin | `MDP-SANDBOX` et `client@exemple` : 0 occurrence dans tous les fichiers du magasin | Vérifié |
| 1.4 Échec (JSON invalide) | `connect claude: invalid_config`, fichier identique, aucun `.bak` ; `1.4-connect-failed.png` | Vérifié |
| 1.3 Connexion Claude (dossier de test) | `connected`, entrée `mcpServers.browser-annotations-preview` ajoutée, `autre-serveur` et `globalShortcut` conservés, 1 `.bak` ; `1.3-connect-claude-connected.png` | Vérifié |
| 1.3 Connexion Codex (dossier de test, édition TOML : pas de CLI dans l'environnement redirigé) | `connected`, table ajoutée, début du fichier identique ; `1.3b-connect-both-connected.png` | Vérifié |
| « Déconnecter » | `disconnected`, fichier redevenu identique à l'original (octet pour octet) ; `1.3c-after-disconnect-claude.png` | Vérifié |
| « Terminé » ; agent connecté → plus de rappel | modale fermée, voile retiré, plus de `AnnotationConnectReminder` | Vérifié |
| 3.0 Carte « Annotation ajoutée » (agent connecté) | `3.0-card-added-agent-connected.png` | Vérifié |
| 3.1 Carte sans agent | UIA « Connecter un agent » ; `3.1-card-no-agent.png` | Vérifié |
| 3.2 « … s'en occupe » en direct + compteur | `try-mcp.py claim` → carte « try-mcp s'en occupe », lanceur « Annotations · 1 en cours » sans action ; `3.2-card-agent-working.png` | Vérifié |
| 3.3 « … a terminé » en direct + « Recharger la page » | `try-mcp.py resolve` → « try-mcp (sandbox) a terminé : Libellé changé dans Checkout.tsx » ; `3.3-card-agent-done.png` | Vérifié |
| 4.1 Historique (Ctrl+Maj+H) | `history opened (4 annotations)` (lecture à l'ouverture) ; `4.1-history.png` | Vérifié |
| 4.2 Détail + « Copier en Markdown » | presse-papiers = Markdown complet du magasin (`## Annotation ann_…`, site, statut, chemin de la capture) ; `4.2-history-detail.png` ; presse-papiers de l'utilisateur restauré après | Vérifié |
| 4.4 Supprimer (immédiat) | `history trash … ok`, `try-mcp.py list` ne la voit plus, dossier dans `trash\` ; `4.5-history-toast-undo.png` | Vérifié |
| 4.5 Annuler (toast 6 s) | `history restore … ok`, de nouveau listée par l'agent ; `4.6-history-after-undo.png` | Vérifié |
| Surveillance seulement pendant l'ouverture | `store watch stopped` à la fermeture (mode quitté, aucun agent au travail) | Vérifié |
| Lanceur masqué en plein écran | F11 : plus d'`AnnotationLauncher` ; F11 : de retour | Vérifié |
| Lanceur sans vol de clic hors de son rectangle | fenêtre enfant à alpha par pixel (V2-B, `WindowFromPoint` vérifié par leur harnais) | Vérifié par V2-B ; pas re-mesuré ici |
| **Vraies configurations Claude / Codex** | empreintes (taille, mtime, SHA-256) de `%APPDATA%\Claude\claude_desktop_config.json`, `%USERPROFILE%\.codex\config.toml`, fichiers `*browser-annotations*`/`.tmp-*`, `Claude Extensions` : identiques avant/après le parcours | Vérifié (parcours) — voir l'incident §6 |

Défauts trouvés et corrigés pendant le parcours : arbre UIA récursif de la modale et de l'historique (clés en collision, V2-B), « Annuler » sans effet après un rafraîchissement de l'historique (entrée oubliée : corbeille gardée à part), panneau et pastille hors fenêtre pour une cible hors écran, « Capture impossible » écrasé par l'ouverture du panneau, libellé « Cliquez sur Installer dans Claude » affiché pendant une simple fusion de configuration, carte « Aucun agent ne la lit encore » laissée après une connexion réussie.

## 5. Mesures

### Temps de capture (clic → image)

12 clics réels (CDP `Input.dispatchMouseEvent`) sur 6 éléments, horloge système des deux côtés (`utc-ms` du journal vs heure d'envoi du relâchement) :

| | médiane | max | < 150 ms |
| --- | ---: | ---: | ---: |
| Image en mémoire (PNG du moteur décodé, vignette prête) | **50 ms** | 65 ms | 12/12 |
| Image affichée dans la pastille | 56 ms | 81 ms | 12/12 |

Décomposition type (journal) : clic → hôte ≈ 5–10 ms ; moteur (`Page.captureScreenshot`, zone 360×116) 40–70 ms pendant que le panneau se construit (≈ 30 ms au repos, mesuré seul) ; décodage + vignette 0–1 ms ; remise au fil UI 2–25 ms. Avant correction (attente de deux images + capture après le panneau) : 150–226 ms. « Ajouter » : écriture en 14–33 ms, **0 ms d'attente de l'image** (2.4).

### Repos (protocole de l'audit, `python tests/annotations/idle_measure.py --runs 3`)

`browser.exe` seul, copie neuve par run, page sandbox, 15 s de stabilisation, fenêtre de 30 s. `baseline` = `build/annotations-baseline` (`--no-annotations` : ni mode ni lanceur), `never` = preview, lanceur affiché, mode jamais activé, `used` = preview, mode activé puis quitté et historique ouvert puis fermé avant la fenêtre. Brut : `build/annotations-v2-qa/idle.log`. **2 passes complètes** (la 3ᵉ s'est arrêtée : `build/annotations-idle-never/browser-annotations-mcp.exe` est verrouillé par un processus lancé à 14:15 par **Codex de l'utilisateur** depuis la configuration de l'incident (§6) ; la référence a ses 3 runs).

| variante | CPU hôte (% d'un cœur) par run | écritures (ops / octets) | threads | handles | USER | GDI | dossier `annotations\` | surveillance à la fin |
| --- | --- | --- | ---: | ---: | ---: | ---: | --- | --- |
| baseline | 1,875 · 1,302 · 1,979 | 0 · 52/1 716 · 104/3 398 | 428 | 2 715 | 107 | 23 | non | — |
| never (lanceur au repos) | 1,771 · 1,458 | 0 · 6/236 | 448 | 2 826–2 833 | 112–114 | 25 | non ×2 | non |
| used | 1,667 · 0,625 | 26/858 · 0 | 449 | 2 859 | 118–119 | 29 | oui | non (arrêtée) |

- **Vérifié** : lanceur au repos = aucune lecture du magasin (pas de dossier créé, aucune ligne `ANNOTATE`), aucune surveillance, CPU dans le bruit de la référence (les deux variantes varient de 1,3 à 2,0 % d'un run à l'autre ; les écritures de la référence viennent du moteur). Sonde par thread (`idle_probe.py`, 20 s, variante `never`) : 218 ms au total sur 448 threads, aucun thread au-delà d'un tick d'ordonnanceur (31 ms max) : pas de thread chaud, pas de minuterie du lanceur.
- **Coût en ressources du lanceur toujours visible** (assumé, il est affiché) : +20 threads (Direct2D/DirectWrite/WIC et le pool du compositeur, bloqués), +≈ 110 handles, +5 USER, +2 GDI par rapport à la référence. Après usage : +1 thread, +6 USER, +4 GDI (bandeau, panneau, modale gardés cachés pour être réutilisés).
- **Non vérifié** (précision) : une différence de CPU inférieure au bruit (≈ ±0,5 % d'un cœur) entre `never` et `baseline` n'est pas mesurable sur ce poste partagé.

### Build

`browser.exe` 10 546 493 o (référence `--no-annotations` : 9 542 437 o) ; `browser-annotations-mcp.exe` 1 172 865 o à côté.

## 6. Non vérifié / limites

- **Incident (14 novembre 2026, 13:01:10–12)** : une entrée `browser-annotations-preview` pointant vers `build\annotations-idle-never` a été écrite dans les **vraies** configurations (`%APPDATA%\Claude\claude_desktop_config.json` par fusion, `%USERPROFILE%\.codex\config.toml` par `codex mcp add`). Cause : vers 12:48 j'ai lancé à la main une copie du preview (`build/annotations-idle-never/browser.exe`) pour une sonde CPU, **sans redirection** de la connexion ; ma session a été suspendue (limite) avant la commande qui la fermait, et elle est restée ouverte jusqu'à ~14:10. À 13:01, quelqu'un ou quelque chose a cliqué « Annoter » puis « Connecter avec Claude » / « Connecter avec Codex » dans cette fenêtre (aucun de mes scripts ne tournait). Le coordinateur a restauré les deux fichiers depuis leurs sauvegardes (`*.browser-annotations-20261114-120110.bak` / `-120112.bak`, conservées, non touchées par moi). **Garde-fou ajouté** : `AgentConnect::Environment::testMode` (variable `BROWSER_ANNOTATIONS_TEST`, ou un port de débogage distant WebView2) → `WriteAllowed()` refuse toute écriture, sauvegarde, ouverture de `.mcpb` et appel de la CLI codex sous les vrais dossiers, résolus par les dossiers connus Windows (`Roaming\Claude`, `Local\Packages`, `<profil>\.codex`) et non par les variables qu'un test redirige ; testé sur dossiers **simulés** (`agent_connect_test` `TestGuard` : refus, fichiers identiques, aucun `.bak`, aucun `.mcpb` ouvert ; insensible à la casse ; voisin non bloqué ; racines inconnues = refus). Tous les lanceurs de test (`sandbox_qa_v2.py`, `idle_measure.py`, `run-sandbox.py --agents sandbox`) posent `BROWSER_ANNOTATIONS_TEST=1` **et** une redirection, et le pilote QA refuse de lancer sinon ; plus aucune copie lancée hors d'un script qui la ferme. Les empreintes « inchangées » du §4 et de `run_tests.py` datent d'après la restauration.
- **Processus restant de l'incident** : `browser-annotations-mcp.exe --store build\annotations-idle-never\annotations` (pid 36360) tourne encore, **enfant de `codex.exe`** (session Codex de l'utilisateur ouverte à 14:15, avant la restauration de `config.toml`). Pas lancé par moi, pas arrêté par moi (processus d'une autre app) ; il s'arrêtera avec cette session Codex, les nouvelles sessions ne le relanceront pas.
- **Installation réelle du `.mcpb` dans Claude Desktop** : non faite (interdit de toucher Claude ; pas d'association `.mcpb` sur ce poste). L'UI la gère : sans association, repli sur la fusion de configuration, et le bouton dit « Connexion à Claude… » (pas « Cliquez sur Installer »).
- **Codex par la vraie CLI** (`codex mcp add`) : non exécutée par moi dans un dossier redirigé (la sandbox et la QA désactivent la CLI : édition TOML). Vérifiée par V2-A dans un `CODEX_HOME` temporaire.
- **Connexion réelle de Claude Desktop / Codex comme clients MCP** : non faite ; le client est `try-mcp.py` (même protocole stdio).
- **Disque amovible (1.6, « Reconnecter »)** : branché (`IsOnRemovableDrive` → `setup.removableDrive`, état `Reconnect`), non exercé (pas de clé USB).
- **Seuil des 3 « Plus tard »** et réglage « Proposer de connecter… » désactivé : logique branchée sur les clés V2-D, non rejoués en réel ici.
- **Rétention 7/30/90 jours** : `Store::Purge(seconds)` testé (store_test) ; la purge réelle par le navigateur n'a pas été observée sur des données de plus de 7 jours.
- **Clignotement pendant la capture** : aucun `resize` dans la page pendant 7 captures `Page.captureScreenshot` à zone ; rendu à l'écran non filmé (pas d'enregistrement vidéo).
- **Entrée clavier physique** (SendInput) : non utilisée (la machine était utilisée en même temps) ; clics dans la page par CDP, chrome natif par UI Automation et messages postés à nos fenêtres. Un raccourci posté peut être perdu si quelqu'un tape au même moment (vu une fois : le pilote a alors un repli par le lanceur, non utilisé dans la passe finale).
- **Avertissements du build partagé** : `build.py` sur l'arbre de travail actuel affiche 3 `-Wswitch` (`Change::AnnotationLauncher/ConnectPrompt/Retention` non traités dans `SettingChanged`) : valeurs ajoutées par V2-D (4e167a0), dont le branchement dans `main.cpp` est prévu après V2-C. Nos sources seules : 0 avertissement (`own_warnings.py`).
- **Réglages « Annotations et agents »** : livrés par V2-D ; leur branchement à `annotation-entry.h` dans `main.cpp` n'est pas encore fait, donc non vérifié de bout en bout ici.
- Vue partagée, plusieurs fenêtres, mise en veille de l'onglet annoté : non exercés (comme en V1).


---

# V3-A — corrections du premier test réel : validation (14 novembre 2026)

Branche `VALXXII/annotations-v1`, commits `e09b799` → (ce commit), sur `c62426c`. Retours : `plan-annotations-mcp-local/design/annotations-v2-retours.md` (points 1, 2, 3, 5, 6 + glissé du bandeau du 9, et 11 ajouté en cours de tâche). Références Paper relues en lecture seule (section « 5 · Retours du test » : 5.2, 5.5, 5.6, 5.7, 5.8 ; valeurs de mouvement de 5.2). Détails de conception : `design/annotations-ui.md` § V3-A, `design/annotations-store-bridge.md` § V3-A, `design/annotations-settings.md` § V3-A. Le build et la sandbox de l'utilisateur (`build/annotations-preview`, `run-sandbox.py --agents real`) n'ont été ni touchés ni lancés ; tout s'est fait sur `build/annotations-v3-preview` et ses copies QA (`build/annotations-v3-qa-app`). `sandbox/annotations/site/style.css`, modifié pendant le test de l'utilisateur, est laissé tel quel, non commité.

Légende : **Vérifié** = observé ici, preuve citée · **Non vérifié** = pas démontré, raison donnée.

## 1. Livré

| Point | Ce qui change | Statut |
| --- | --- | --- |
| §1 Redémarrer | Modale : « Redémarrez Claude pour l'activer » tant que l'app n'a pas lu le magasin depuis la connexion (même composant que Codex) ; une seule ligne « Redémarrez Claude et Codex pour les activer » si les deux. Réglages : « Connecté · redémarrez Claude pour l'activer ». README de la sandbox. | Vérifié (QA réelle, harnais, test des réglages) |
| §2 Capture sans flash | Copie native des bords + étiquette (`SelectionFrame`) puis « handoff » ; voile retiré par calcul ; prise de la vue entière recadrée par l'hôte ; panneau construit après la demande au moteur. | Vérifié (sondes d'écran, image comparée à la prise du moteur mode éteint) |
| §3 Petits flashs | 9 causes trouvées et corrigées (tableau dans `annotations-ui.md` § V3-A, résumé §4 ci-dessous). | Vérifié pour 1, 2, 7, 8 à l'écran ; 3–6, 9 par construction + harnais |
| §5 Prise en charge | Instructions et descriptions directives, `release` avec `reason`, prompt MCP « Traiter mes annotations », lectures par annotation (`readers\…\seen`) ; historique (ligne, détail « Suivi », aide) et cartes « Lue par … » / « … n'a pas pu la traiter ». | Vérifié (transcriptions, QA réelle avec `try-mcp`) |
| §6 Lanceur en mode seulement | Apparaît 120 ms après le bandeau (0,9 → 1, 200 ms), repart à la sortie, fenêtres détruites au repos ; historique hors mode par Ctrl+Maj+H, ancré à 16 DIP du bas ; réglage « Afficher le lanceur » retiré (lecture tolérante de l'ancienne clé). | Vérifié |
| Glissé du bandeau | −32 → 0 + fondu, 220 ms (0,32, 0,72, 0, 1) ; sortie 160 ms ; réduit : fondu 150 ms ; interruptible ; point d'accroche `SetBackdrop` pour le fond animé à venir. | Vérifié (modèle + écran) |
| §11 Aperçu rogné | `contain` partout ; disposition verticale au-delà de 2,2:1 ; même règle pour le détail de l'historique. | Vérifié (harnais, captures) |

## 2. Build

`python tools/release/build.py --out build/annotations-v3-preview --channel dev --preview` → `Build OK … browser.exe (0.3.256, 035f20445d41)`, **0 avertissement, 0 erreur** dans le journal complet (`build/v3a-build.log` : aucune ligne « warning »). `browser.exe` 10 704 720 o, `browser-annotations-mcp.exe` 1 231 866 o.

## 3. Tests

| Commande | Résultat |
| --- | --- |
| `python -I tests/annotations/run_tests.py --bridge build/annotations-v3-preview/browser-annotations-mcp.exe` | **Vérifié** : exit 0 « annotations: all builds and tests passed » — store_test 2 535 (dont `seen` des lecteurs : borne 40, plus récents gardés, lecture tolérante), json_fuzz 156 715, host_test 90 (dont `mark`, `shown`, plan d'origine du clip, retrait du voile ≤ 1 niveau sur 64 × 40 avec bords fractionnaires), agent_connect_test 199, claim_race 15 ; transcriptions sur le bridge de test **et** sur celui du build V3 (legacy 53 étapes, modern 19, project-deprecated 3, site-restricted 9), `tools/list` égal au golden régénéré ; vrais fichiers Claude/Codex inchangés (empreintes du runner). `build/v3a-run_tests.log`. |
| `python tests/annotations-ui/run.py` | **Vérifié** : « 528 checks, 0 failures; 92 captures » (`build/annotations-ui-qa/v2/`). V3 : temps et courbe du bandeau (−32 px, 220 / 160 / 150 ms, reprise depuis la valeur présentée), lanceur (120 ms, 0,9, 200 / 150 ms), « Lue par » / « n'a pas pu la traiter » (lignes, détail « Suivi » + aide, Markdown, cartes), contrastes (ambre 12/16 sur `#171717`), ligne de redémarrage unique, pastille verticale + `contain` (pixels), copie native du cadre (pixels des bords, rien à l'intérieur), fenêtres réelles : **0 fenêtre du lanceur hors mode**, historique seul ancré à 16 DIP, **0 réveil en 1,5 s hors mode** (`Idle 1500 ms (v3 outside the mode): frames 0 … WM_TIMER 0`), bandeau réel en cours de glissé, copie native cliquable-à-travers. Captures `v3-*.png`. |
| `test-annotation-settings.ps1` | **Vérifié** : exit 0, 111 lignes « ok » (plus de ligne « lanceur », un seul interrupteur, ordre Tab à 8 arrêts, « Connecté · redémarrez Claude / Codex pour l'activer », `annotation-launcher=` lu et jamais réécrit) ; client UIA réel : « no launcher switch », bascule de « Proposer de connecter un agent » appliquée ; « real configurations unchanged ». |
| `python tests/annotations/sandbox_qa_v2.py run` (adapté V3) | **Vérifié** : **64 étapes, 0 échec** (§4), `build/annotations-v3-qa/report.json` + captures. |
| Vraies configurations | Empreintes SHA-256 de `%APPDATA%\Claude\claude_desktop_config.json` et `%USERPROFILE%\.codex\config.toml` identiques avant/après la dernière série (`build/v3a-fp-before.txt`) ; chaque script les vérifie aussi. (`config.toml` a changé entre deux de mes séries, hors de mes tests : modification de l'utilisateur ; jamais pendant un test.) |

## 4. Parcours réel (copie QA du build V3, sandbox, agents redirigés)

Pilote `tests/annotations/sandbox_qa_v2.py run` : copie `build/annotations-v3-qa-app`, `BROWSER_ANNOTATIONS_TEST=1`, `BROWSER_ANNOTATIONS_CONNECT_ROOT` sous `build/`, port de débogage pour cette copie seulement ; navigateur et serveur fermés dans un `finally` (aucun processus restant, vérifié par `tasklist`). Étapes V3 ajoutées :

| Étape | Preuve | Statut |
| --- | --- | --- |
| Aucun lanceur au lancement | pas de nœud UIA `AnnotationLauncher`, **aucune fenêtre** de lanceur (énumération des fenêtres enfants) | Vérifié |
| Bandeau animé à l'entrée | 165 images d'écran de la zone du bandeau, ≥ 4 niveaux distincts (pas un saut) | Vérifié |
| Lanceur avec le mode, absent après | UIA présent en mode ; mode quitté : 0 nœud, 0 fenêtre | Vérifié |
| §2 aucun changement visible pendant la prise (« Payer », h1, h2) | 72 images d'écran par clic (une toutes les ~8–11 ms, BitBlt + CAPTUREBLT, fenêtres en couches comprises, de 150 ms avant le clic à 500 ms après) : **0 image où le cadre perd plus de 30 % de ses pixels d'accent, 0 image où la page perd son texte** | Vérifié |
| Contrôle négatif (chemin V2 : la page masque son cadre) | la même sonde voit le cadre disparaître sur 5 à 9 images | Vérifié (la sonde détecte bien un flash) |
| Voile retiré exactement | image enregistrée vs prise du moteur de la vue, mode éteint, même survol, même recadrage : **pire écart 1 niveau, 0 pixel au-delà de 2** (585 × 117) | Vérifié |
| Aucun pixel du cadre dans les images | 0 pixel d'accent dans tous les `shot.png` | Vérifié |
| §1 Redémarrer | « Redémarrez Claude pour l'activer » après la connexion ; « Redémarrez Claude et Codex pour les activer » (aide UIA du bouton Codex) | Vérifié |
| §5 « Lue par … » | `try-mcp get` → `readers\…\seen` contient l'id → carte « Lue par try-mcp (sandbox) » en direct | Vérifié |
| §5 « … n'a pas pu la traiter » | `try-mcp claim` puis `release <id> Pas d'accès au code source de ce site.` → carte en direct ; historique : ligne « try-mcp (sandbox) n'a pas pu la traiter », détail « Ajoutée · Lue par · Prise en charge · n'a pas pu la traiter « Pas d'accès… » » + aide | Vérifié |
| Historique hors mode | Ctrl+Maj+H : panneau seul, aucun bouton lanceur | Vérifié |
| Repos hors mode après usage | 0 fenêtre de lanceur ; seule reste la fenêtre (masquée) du bandeau | Vérifié |
| Étapes V2 conservées (modale, « Plus tard », connexions redirigées, capture impossible, Ajouter avant l'image, cartes 3.x, historique, corbeille / Annuler, Markdown, mots de passe jamais stockés, plein écran) | toutes passent | Vérifié |

## 5. Mesures

### Temps de capture (clic → image)

| Série | médiane | max | < 150 ms |
| --- | ---: | ---: | ---: |
| QA, 12 clics réels (CDP) — image en mémoire | **103 ms** | 127 ms | 12/12 |
| QA — image affichée dans la pastille | 124 ms | 153 ms | — |
| `capture_timing.py`, 10 clics | 103 ms | 126 ms | 10/10 |

Décomposition type (journal) : copie native 11–29 ms (dont `DwmFlush` ≤ 16 ms), « handoff » de la page 1–11 ms, moteur 58–88 ms (vue entière), recadrage + voile + encodage + vignette 2–7 ms, panneau construit pendant que le moteur travaille (32–60 ms). Avant de construire le panneau après la demande : « handoff » 30–85 ms (le panneau bloquait le fil UI), médiane 120 ms. **« Ajouter » : écriture en 16–17 ms, 0 ms d'attente de l'image** (étape 2.4 : la note part, l'image la rejoint).

### Flash à la capture (preuve)

Sondes d'écran ci-dessus (§4). Expériences A/B pendant la tâche (même session, mêmes éléments, 3 clics chacune) : prise **avec** clip → le texte de la page disparaît sur 4 images (24–110 ms après le clic) à chaque clic, aussi en chemin V2 ; sans prise → 0 ; prise de la vue entière → 0 ; `CapturePreview` → 0 mais 141–155 ms. Choix : vue entière + recadrage (0 flash, même coût moteur). Les interrupteurs d'expérience ont été retirés ; restent pour la QA `BROWSER_ANNOTATIONS_QA_NO_NATIVE_FRAME` (contrôle négatif) et `BROWSER_ANNOTATIONS_QA_RAW_DIR` (image brute du moteur), lus seulement s'ils sont définis.

### Repos hors mode

QA, après usage complet puis sortie du mode : **0 fenêtre du lanceur**, CPU du processus navigateur 0,47 % d'un cœur sur 10 s (bruit du poste partagé : la référence V2 variait de 1,3 à 2,0 %). Harnais : 0 image, 0 réveil d'horloge, 0 `WM_TIMER` en 1,5 s hors mode. La surveillance du magasin ne tourne plus pour l'anneau du lanceur hors mode (le lanceur n'existe plus).

## 6. Non vérifié / limites

- **Vrais agents** : Claude Desktop / Codex non lancés contre ce build (interdit d'y toucher) ; le client est `try-mcp.py` (même protocole). Que Claude appelle désormais `claim` / `resolve` n'est **pas** démontré : les instructions, descriptions et le prompt le demandent explicitement, l'usage réel reste à observer.
- **Étiquette native vs étiquette de la page** : même police (première installée de JetBrains Mono, Cascadia Mono, Consolas), même boîte (mesurée par la page), mais rendu DirectWrite contre Skia : l'anticrénelage du texte de l'étiquette peut différer d'un sous-pixel pendant ~100 ms (la sonde compte jusqu'à ~20 % de pixels d'accent en moins sur h1, dans le texte de l'étiquette, sans trou). Non perceptible sur les captures, non prouvé imperceptible.
- **`DwmFlush`** sur le fil UI au clic (≤ 16 ms) : garantit que la copie native est à l'écran avant que la page masque la sienne ; coût inclus dans les mesures.
- **Planche 5.5** : « Rouvrir » du détail non ajouté (l'annotation est ouverte) ; titres des cartes « Lue par … » / « … n'a pas pu la traiter » en version propre avec les composants existants, à ajuster au design final. Dégradé du viewport, vague tramée, curseur dédié, inspecteur d'espacements : non faits (en design, tâche suivante). Point 7 (Claude Code en un clic) : hors périmètre.
- **Défilement pendant la prise** : si la page défile dans les ~100 ms où la copie native est affichée, les bords natifs ne suivent pas (le voile, lui, suit) ; non observé, non corrigé.
- Sondes d'écran sur un bureau partagé avec d'autres agents : la fenêtre QA est mise au premier plan (topmost) le temps des sondes, sans entrée clavier synthétique ; une fenêtre d'un autre processus peut fausser une sonde (vu une fois au lancement : l'invocation UIA du bouton « Annoter » n'était pas arrivée ; le pilote réessaie).
- Le décalage de 32 DIP de la page à l'entrée / sortie du mode (redimensionnement du WebView) reste un saut (inhérent).

# V3-B — entrée animée, bandeau vivant, curseur dédié, espacements au survol : validation (14 novembre 2026)

Branche `VALXXII/annotations-v1`, sur V3-A (`12d5ff8`). Source : Paper `01M6Q7YT14ZKMGET54ASPNGQEC` / `p-H-0`, planches 5.1 à 5.5 (lecture seule, valeurs par `get_computed_styles` / `get_jsx`) ; trames : formules de `gen_dither.py`. Conception : `design/annotations-ui.md` § V3-B, `design/annotations-store-bridge.md` § V3-B. Build sous test : `build/annotations-v3-preview` et sa copie QA `build/annotations-v3-qa-app` ; référence V3-A pour le repos : `build/annotations-v3a-ref` (même commande de build sur `12d5ff8`, arbre de travail temporaire supprimé ensuite). `build/annotations-preview` et la sandbox de l'utilisateur n'ont été ni modifiés ni lancés ; `sandbox/annotations/site/style.css` (modifié par l'utilisateur) reste tel quel, non commité. Windows 11 26200, 125 % (DPI 120), WebView2 157.

Légende : **Vérifié** = observé ici, preuve citée · **Non vérifié** = pas démontré, raison donnée.

## 1. Livré

| Point | Ce qui change | Statut |
| --- | --- | --- |
| 5.2 Bandeau vivant | Trame de base + vague tramée (Bayer, cellules 2 DIP) en boucle linéaire de 4 s, ≤ 30 images/s, départ à 220 ms (montée 400 ms) ; nulle sous « Mode Annotation » (+ BETA), « Quitter / Échap » et le rappel (zones issues de `LayoutBanner`, smoothstep 120 DIP) ; horloge arrêtée fenêtre réduite / masquée / en arrière-plan et hors du mode ; réduit : trame fixe. Images de vague : intervalle réécrit en pixels + mise à jour partielle de la fenêtre en couches. | Vérifié |
| 5.1 Dégradé d'entrée | `EntryVeil` : fenêtre en couches cliquable-au-travers, non activable, au-dessus de la page ; lavis 160 DIP + liseré 48 px sur trois bords + trame qui ondule (cellules 3 DIP) ; 140 / 80 / jusqu'à 500 ms puis **détruite** ; interruptible (160 ms depuis la valeur affichée) ; réduit : 50 % sans trame, 150 + 150 ms ; rien à la sortie. Lanceur à 120 ms et glissé du bandeau inchangés (V3-A). | Vérifié |
| 5.4 Curseur « flèche + note » | Feuille adoptée (document + racines d'ombre ouvertes) et boucliers des iframes ; SVG Paper exact, point chaud (5, 3) ; 32 / 48 / 64 px selon DPI × taille de pointeur ; résolution = échelle de l'écran (indépendante du zoom) ; retiré à la sortie. | Vérifié (curseur Windows réel) |
| 5.3 Espacements au survol | Padding (135°, bleu) et gap (45°, rose, flex et grid, wrap) sur l'élément survolé, étiquettes par valeur distincte et par axe, < 4 px sans étiquette, fondu 120 ms avec le cadre, rien pendant un mouvement rapide, zoom pris en compte (valeurs en px CSS) ; copie native à la sélection, donc jamais dans la capture ; `target.spacing` + ligne « Style » (pastille, Markdown, bridge). | Vérifié |
| 5.5 « Rouvrir » | Bouton du détail d'une annotation « … n'a pas pu la traiter » ; `Store::Reopen` (ouverte, « Rouverte » horodaté), historique et compteur relus. | Vérifié (unitaire + harnais + capture) ; non cliqué dans la QA réelle |

## 2. Build

`python tools/release/build.py --out build/annotations-v3-preview --channel dev --preview` : `Build OK … browser.exe (0.3.257, 12d5ff8ae426)`, **0 avertissement, 0 erreur** (`build/v3b-build.log`). `browser.exe` 10 840 605 o, `browser-annotations-mcp.exe` 1 249 898 o.

## 3. Tests

| Commande | Résultat |
| --- | --- |
| `python -I tests/annotations/run_tests.py --bridge build/annotations-v3-preview/browser-annotations-mcp.exe` | **Vérifié** : « annotations: all builds and tests passed » — store_test 2 562 (dont `TestSpacingStyle` : écriture/lecture de `target.spacing`, raccourcis, Sanitize, 6 lectures tolérantes ; `TestReopenUnhandled`), json_fuzz 156 715, host_test 18 181 (dont `V3B` : `mark.sp` et `target.spacing` venus de la page, rejet doux, `StyleSummary`, tailles du curseur, zones de vague nulles sous les libellés, boucle sans couture, `VeilTimeline` naturelle / interrompue / réduite, rasters), agent_connect_test 199, claim_race 15 ; transcriptions sur les deux bridges (legacy 53 étapes, avec la ligne « - Style (px CSS) : `padding 10 24 · gap 0 8 (flex)` » et `structuredContent.target.spacing` exact), `tools/list` égal au golden **régénéré** (seule différence : la description de `browser_annotation_get` mentionne les espacements) ; vrais fichiers inchangés. `build/v3b-run_tests.log`. |
| `python tests/annotations-ui/run.py` | **Vérifié** : « 945 checks, 0 failures; 108 captures » (`build/annotations-ui-qa/v2/`, captures `v3b-*`). Voir §4. |
| `test-annotation-settings.ps1` | **Vérifié** : exit 0, 102 checks + 111 lignes « ok », client UIA ok, vraies configurations inchangées (`build/v3b-settings.log`). |
| `python tests/annotations/sandbox_qa_v3b.py run` (nouveau) | **Vérifié** : **49 étapes, 0 échec** (§5), `build/annotations-v3b-qa/report.json` + captures. |
| Vraies configurations | Empreintes SHA-256 de `%APPDATA%\Claude\claude_desktop_config.json` (`d5c653f1…`) et `%USERPROFILE%\.codex\config.toml` (`f5c61dea…`) identiques avant (`build/v3b-fp-before.txt`) et après toute la série ; chaque script les vérifie aussi. |

## 4. Harnais (planches 5.1 à 5.5)

| Preuve | Mesure | Statut |
| --- | --- | --- |
| 5.2 trois instants (0 / 1,3 / 2,7 s), variante rappel, trame fixe (réduit) | `v3b-5.2-band-wave-0-1.3-2.7s.png`, `…-reminder.png`, `…-reduced-still-texture.png` | Vérifié |
| **Vague nulle sous les libellés** | rendu vague vs trame seule, pixel à pixel : **0 pixel différent dans les zones de texte**, 283 520 entre elles, sur 96 rendus (12 instants × rappel oui/non × BETA oui/non × 96 et 120 DPI) ; fenêtre réelle, deux instants à 500 ms : 0 pixel changé sous les libellés, 1 324 entre eux | Vérifié |
| Chemin rapide de la vague = rendu Direct2D | intervalle réécrit en pixels vs image Direct2D complète, 4 DPI × 2 × 3 instants : pire écart **1 niveau** | Vérifié |
| Cadence et coût du bandeau (fenêtre réelle) | **27,5 images/s** (≤ 30) ; **0,93 ms** par image ; horloge d'affichage **30 réveils/s** pendant la vague seule (≈ 120 avant le minuteur) | Vérifié |
| Arrêt de l'horloge | 1,5 s en arrière-plan, fenêtre réduite, hors du mode après la vague, mouvement réduit, après le voile : **0 image, 0 réveil d'horloge, 0 `WM_TIMER`** ; le temps de vague ne court pas en pause | Vérifié |
| 5.1 pic (~180 ms), variante réduite, bande d'entrée 0 / 60 / 140 / 220 / 360 / 500 ms | `v3b-5.1-entry-peak-180ms.png`, `v3b-5.1-entry-reduced-150ms.png`, `v3b-5.2-entry-strip-…png` ; bleu en haut de page, rien au milieu ; réduit : aucun point | Vérifié |
| Voile réel | fenêtre `WS_EX_LAYERED`, `WS_EX_TRANSPARENT`, `WS_EX_NOACTIVATE`, `WindowFromPoint` la traverse ; **détruit 503–505 ms** après le départ, pic 1,0, 59 images, 1,8 ms par image (calcul 1,1 ms) | Vérifié |
| Interruption | interrompu à ~60 ms (valeur 0,85–0,88) : éteint depuis cette valeur, détruit ~240 ms après le départ, pic < 1 ; strip `v3b-5.2-entry-interrupted-…png` | Vérifié |
| Réduit | pic 0,50, pas de trame, détruit ~300 ms | Vérifié |
| 5.3 inspecteur (copie native) | `v3b-5.3-spacing-inspector.png` (carte Paper) : hachures bleues / roses, pastilles accent et #D63E92 échantillonnées | Vérifié |
| 5.4 curseur | `v3b-5.4-cursor-32-48-64.png` (rendu de référence des tracés, clair / sombre) | Vérifié |
| 5.5 « Rouvrir » | `v3b-5.5-history-detail-reopen.png`, `…-reopened.png`, fenêtre réelle `v3b-5.5-window-history-reopen.png` ; frappe (HReopen), crochet appelé avec l'id, nœud UIA « Rouvrir », contraste mesuré | Vérifié |

## 5. Parcours réel (copie QA du build V3, sandbox, agents redirigés)

`tests/annotations/sandbox_qa_v3b.py run` : copie `build/annotations-v3-qa-app`, `BROWSER_ANNOTATIONS_TEST=1`, dossiers d'agents sous `build/`, port de débogage pour cette copie seulement, navigateur fermé et serveur arrêté dans des `finally`. Le pointeur est déplacé par `SetCursorPos` sur notre fenêtre seulement (au premier plan le temps des sondes) et remis en place. Curseur réel lu par `GetCursorInfo` + `GetIconInfo` (taille, point chaud, pixels bleus du « + »).

| Étape | Preuve | Statut |
| --- | --- | --- |
| Entrée : voile à l'écran puis parti | images d'écran du haut de la page : bleu visible ~300 ms au-dessus du seuil (pic `v3b-5.1-real-entry-peak.png`), rien après ; journal « entry veil destroyed after **501–508 ms** (28–42 images, pire image 5–9 ms) » ; aucune fenêtre cliquable-au-travers restante | Vérifié |
| Curseur réel sur texte (h1), lien, champ, canvas, iframe même origine, **iframe d'une autre origine** | curseur personnalisé **48×48, point chaud (7, 4)**, 165 pixels bleus du « + » (DPI 120) ; style calculé `image-set(...)` sur ces éléments et sur les boucliers des deux iframes | Vérifié |
| Notre UI | flèche sur le bandeau ; panneau de note : I-beam dans le champ, flèche sur le panneau | Vérifié |
| Pas de sablier pendant la capture | 37–40 échantillons sur 600 ms après le clic : uniquement notre curseur | Vérifié |
| Sortie | curseurs de la page revenus (I-beam sur le texte, main sur le lien) ; aucune règle de curseur, aucune feuille à nous (la page garde la feuille du navigateur `:root{scrollbar-width:none}`), aucune couche | Vérifié |
| Zoom 150 % | même curseur réel 48×48 / (7, 4) ; résolution `1.25dppx` (échelle écran, pas 1,875) ; inspecteur visible, étiquette « 6 » (px CSS) | Vérifié |
| Inspecteur sur `form.checkout` (flex colonne, gap 6), `.row` (flex-wrap, gap 8), `.frames` (grid, gap 16), `.summary` (padding 18 / 20) | lu dans la racine fermée via CDP (opacité 1, bandes, étiquettes `g6`, `g8 g8`, `g16`, `p18 p20`) **et** à l'écran (pixels de hachures bleus / roses) | Vérifié |
| Rien pendant un mouvement rapide | 16 conteneurs en ~530 ms : opacité 0 après chaque mouvement, 1 au repos | Vérifié |
| Capture flex et grid | journal « inspector at the selection: 8 / 2 gap bands, labels g6 / g16 » ; 67–76 images d'écran pendant la prise : **aucune** où les hachures perdent plus de 30 % (copie native) | Vérifié |
| **Image enregistrée sans cadre, hachures, étiquettes ni curseur** | comparée à la prise du moteur, mode éteint, même défilement, même survol, même recadrage : **pire écart 1–2 niveaux, 0 pixel au-delà de 2** (585 × 644 et 1 070 × 259) | Vérifié |
| `target.spacing` + « Style » | magasin : gap 6 flex / 16 grid ; `try-mcp get` : « - Style (px CSS) : `gap 6 (flex)` » / « `gap 16 (grid)` » | Vérifié |
| Interruption | Échap ~300 ms après l'entrée : voile détruit après **209–232 ms** | Vérifié |
| Réduit (réglage « reduce-motion ») | voile détruit après **300–304 ms** | Vérifié |

## 6. Mesures

### Repos hors du mode après un usage (même protocole, 3 exécutions × 20 s, copie fraîche à chaque fois)

| Build | CPU (% d'un cœur) | Changements de contexte / s (tous les fils de `browser.exe`) |
| --- | --- | --- |
| V3-A (`build/annotations-v3a-ref`) | 1,64 · 1,48 · 0,78 | 1 429,5 · 1 428,2 · 1 453,9 |
| V3-B | 0,47 · 0,47 · 2,34 | 1 422,5 · 1 422,7 · 1 421,5 |

Égal à V3-A : le CPU varie de 0,5 à 2,3 % selon la charge du poste partagé, dans les deux sens ; les réveils sont identiques à 2 % près. Harnais : 0 réveil hors du mode (§4). `build/annotations-v3b-qa/idle.json`.

### Pendant le mode (fenêtre au premier plan, 15 s)

| Situation | CPU (% d'un cœur) | Changements de contexte / s |
| --- | --- | --- |
| Mode, vague en cours | 6,0 · 7,1 (deux séries) | ~3 600 |
| Mode, mouvement réduit (pas de vague) | 1,4 · 2,1 | ~1 425 |

**Budget annoncé : le mode avec sa vague coûte au plus 8 % d'un cœur au processus navigateur** (mesuré 6,0–7,1 %, soit ~4,5–5,5 % pour la vague seule). Dans le harnais, une image de vague coûte 0,93 ms (≈ 2,6 % à 27,5 images/s) ; l'écart avec le navigateur réel (et les ~2 200 changements de contexte/s supplémentaires) vient de la recomposition de la fenêtre en couches au-dessus du WebView, non décomposée plus finement. Optimisations faites pendant la tâche : 5 ms → 0,93 ms par image (intervalle en pixels sans Direct2D au lieu d'un rendu Direct2D complet avec relecture GPU), ~120 → 30 réveils d'horloge/s (minuteur entre deux images), plus de `SetWindowPos` par image, mise en page des libellés mise en cache.

### Durée réelle de l'entrée

Voile : détruit **501–508 ms** après son départ (5 entrées réelles), 28–42 images, pire image 5–9 ms ; à l'écran, bleu au-dessus du seuil pendant ~300 ms (montée et fin du fondu sous le seuil). Bandeau 220 ms, lanceur 120 → 320 ms (V3-A, inchangés). Interrompue : 209–232 ms. Réduite : 300–304 ms.

## 7. Non vérifié / limites

- **Bureau verrouillé pendant une partie de la tâche** : la session n'avait plus de bureau d'entrée (capture d'écran et `GetCursorInfo` refusés) ; le pilote QA sait alors sauter les sondes d'écran (marquées « SKIP »). La série finale (49/49) a tourné bureau disponible, toutes sondes faites.
- **Taille de pointeur Windows ≠ 32** (accessibilité) : calcul testé en unitaire (`SizeFor(96, 48) == 48` …), pas essayé avec le réglage changé (réglage global de l'utilisateur, non modifié).
- **Point chaud** : mesuré (7, 4) sur 48 px à 125 % pour une pointe en (7,5 ; 4,5) — arrondi de Chromium, < 1 px.
- **Iframes imbriquées dans une iframe d'une autre origine, racines d'ombre fermées de la page** : couvertes par le bouclier / l'héritage du curseur ; un élément d'une racine fermée qui force son propre `cursor` garderait le sien (non observé dans la sandbox).
- **Coût réel de la vague** : voir §6, non ramené au coût du harnais.
- **« Rouvrir »** : non cliqué dans le parcours réel (unitaire magasin + fenêtre réelle du harnais).
- **Hachures de la copie native** : rendu Direct2D vs Skia, même géométrie et même phase ; l'anticrénelage des traits peut différer d'un sous-pixel pendant ~100 ms ; la sonde d'écran ne voit aucune image avec plus de 30 % de hachures perdues.
- **Inspecteur dans une iframe même origine** : la cible y est trouvée (V1), l'inspecteur suit ses rectangles décalés ; non mesuré à part dans la QA.
- Les écarts assumés avec `gen_dither.py` (enveloppe enroulée pour une boucle sans couture, montée d'amplitude de 400 ms) sont décrits dans `design/annotations-ui.md` § V3-B.
