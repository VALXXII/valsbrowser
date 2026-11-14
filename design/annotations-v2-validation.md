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
