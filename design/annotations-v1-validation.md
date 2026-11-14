# Annotations V1 — intégration navigateur, build preview, sandbox : validation (T3)

Branche `VALXXII/annotations-v1` (base `origin/main` f665943, après T1 73c8d1c/a3a61e2 et T2 2ccacce). Contrat : `plan-annotations-mcp-local/design/annotations-v1-brief.md`. Date des preuves : 14 novembre 2026, Windows 11 26200, affichage 125 % (DPI 120), runtime WebView2 157.0.4341.46.

Légende : **Vérifié** = observé ici avec la preuve citée · **Non vérifié** = pas démontré, la raison est donnée.

## 1. Ce qui est livré

| Fichier | Rôle |
| --- | --- |
| `src/annotation-mode.h` | Intégration (seul point de contact avec `main.cpp`) : activation/sortie, routeur de messages, capture, écriture hors fil UI, projets, carte, onboarding, surveillance de `readers\`, menu de page, Markdown. |
| `src/annotation-script.h` | Script de page injecté par `ExecuteScript` à l'activation (racine shadow fermée, cadre, extraction bornée, démontage). |
| `src/annotation-host.h` | Logique pure testée : validation des messages (enveloppe, nonce, `navStamp`, 64 Kio, schéma), plan de recadrage, origines/projets, résumé d'état des agents. |
| `src/annotation-capture.h` | Recadrage + réduction + encodage PNG (WIC) + miniature, sur le thread pool. |
| `src/main.cpp` | ~25 lignes d'accroche : garde de compilation, déclarations, `Key()` (Ctrl+Maj+A, Échap), `ExtraCommand`, `Update()`, `Select()`/`ActivatePanel()`, `ApplyPages()`, `NavigationStarting`/`SourceChanged`, menu de page, mise en veille, UIA du bouton. |
| `src/chrome.h` | Bouton barre « Annoter » (`'N'`, peint par `PaintToolbarButton` de T2) et bande de 32 DIP (`ContentTop()`). |
| `src/elements-integration.h` | `PageMessage` : une ligne, le routeur d'annotation avant le plafond de 4 096 caractères (l'interstitiel HTTPS reste traité en premier). |
| `src/annotation-launcher.h`, `src/annotation-onboarding.h`, `src/annotation-kit.h` (T2) | Ajouts d'intégration : carte `Resolved` (« Résolue par / Question de / Écartée par »), onboarding sans `.mcpb` (configuration manuelle ouverte, « extension bientôt »), nom de serveur `browser-annotations-preview`, blocs JSON complets, JetBrains Mono embarquée. |
| `assets/fonts/JetBrainsMono-Regular.ttf` + `JetBrainsMono-OFL.txt` | JetBrains Mono **2.304** (zip officiel `JetBrainsMono-2.304.zip` SHA-256 `6f6376c6ed2960ea8a963cd7387ec9d76e3f629125bc33d1fdcd7eb7012f7bbf` ; TTF SHA-256 `a0bf60ef0f83c5ed4d7a75d45838548b1f6873372dfac88f71804491898d138f` ; OFL SHA-256 `30f0c136e3c88e422d0791acd97238870f9054a9729bc34cf2ff0d4ed8cac4ad`). Même modèle que les fontes existantes (licence à côté, copiée avec `assets/`). |
| `tools/release/build.py` | `--no-annotations` (référence de mesure : `-DBROWSER_NO_ANNOTATIONS`, pas de bridge). |
| `tools/release/package.py` | `annotations` ajouté à `RUNTIME_DATA` (dossier d'un portable : signalé, jamais empaqueté). |
| `tests/update-integration/e2e_install.py` | Le fixture hôte compile `main.cpp` avec une identité **dev** : il lie maintenant `src/annotations.cpp`. |
| `tests/annotations/host_test.cpp` (+ `run_tests.py`) | 53 vérifications de la logique hôte. |
| `tests/annotations/sandbox_qa.py`, `idle_measure.py` | Pilote QA de la copie preview, mesure de repos. |
| `sandbox/annotations/` | `site/` (Boutique), `cross/` (iframe d'une autre origine), `serve.py`, `try-mcp.py`, `run-sandbox.py`, `README.md`. |

Compilation : le mode n'existe que dans les builds **estampillés non-release** (`BROWSER_HAS_BUILD_INFO` et pas `BROWSER_CHANNEL_RELEASE`). `--channel release` et les fixtures non estampillés qui incluent `main.cpp` gardent exactement le comportement précédent (stubs vides) ; le bridge n'est toujours pas construit en release (T1, inchangé).

### Comportement retenu (écarts et choix)

- **Purge de rétention** à la première activation de la session, pas au lancement : le budget « mode jamais activé = aucune E/S » l'emporte sur « purge au lancement » du brief.
- **`browser.exe` n'accepte pas d'URL** en argument : `run-sandbox.py` écrit la session du preview seulement si elle n'existe pas, sinon affiche l'adresse à ouvrir.
- **Build preview** construit avec `--channel dev --preview` (et non `--channel dev` seul) : `--preview` coupe le moteur de mise à jour et la remise à une copie installée, pour qu'aucun profil installé ne puisse être touché.
- **Pas de tiroir ni de brouillons** (T2 ne les livre pas) : la carte du lanceur sert de suivi. Lanceur cliqué → carte « Prise par … » si une annotation est réservée, sinon « Résolue par … » (résolution des dernières 24 h), sinon onboarding. Une résolution pas encore vue dans la session s'affiche une fois à l'activation. « Ouvrir » montre le dossier de l'annotation.
- **« Copier en Markdown »** : menu de page « Copier la dernière annotation (Markdown) » (le popover T2 n'a pas ce bouton).
- **Projets** : règle d'origine existante, sinon projet proposé nommé d'après le titre (« Paiement — Boutique » → `boutique` / « Boutique »), écrit dans `projects.json` avec la première annotation ; « Nouveau projet pour cette origine… » en crée un autre nommé d'après l'hôte.
- **Iframes** : un bouclier transparent de la racine fermée recouvre chaque iframe visible pendant le mode ; le clic n'atteint jamais l'iframe ; même origine → élément intérieur, autre origine → boîte.
- Le script ne consomme que les événements **de confiance** (`isTrusted`) : les `el.click()` de la page passent.
- `ClaudeConfig` produit un document JSON complet `{"mcpServers": {...}}` (l'ancien membre isolé n'était pas du JSON valide) ; le test T2 est mis à jour.
- Graisse 600 : rendue Medium (pas d'Inter Display SemiBold dans `assets/fonts`) — écart T2 accepté tel quel.

## 2. Build

```
python tools/release/build.py --out build/annotations-preview --channel dev --preview
```
**Vérifié** : `Build OK: …\build\annotations-preview\browser.exe (0.3.242, 2ccacce4b402)`, 0 avertissement (`-Wall -Wextra`), `version.json` canal `dev` (icône preview), `browser-annotations-mcp.exe` à côté (1 156 392 o), `browser.exe` 9 981 459 o (référence sans la fonctionnalité : 9 286 766 o, +0,69 Mo dont JetBrains Mono hors exe). Profil, session et magasin propres au dossier (`profile\`, `session`, `annotations\`).

## 3. Tests

| Commande | Résultat |
| --- | --- |
| `python -I tests/annotations/run_tests.py` | **Vérifié** : exit 0, « annotations: all builds and tests passed » — store_test 2 380 vérifications, json_fuzz 156 715 (300 000 mutations), différentiel Python 12 009 documents 0 désaccord, **host_test 53 vérifications 0 échec**, claim_race 15 (2 × 1 000 + 8 threads), transcripts legacy/modern/project, bridge au repos 0,0 ms CPU. Journal : `build/annotations-v1-qa/run_tests.log`. |
| `python tests/annotations-ui/run.py` (harnais T2), 3 passes | **Vérifié** : 3/3 « 292 checks, 0 failures; 48 captures », police mono résolue **JetBrains Mono** (journaux `t2-harness-run{1,2,3}.log`). Contexte : l'économiseur d'écran détenait le bureau d'entrée, donc aucune saisie utilisateur ne pouvait interférer ; les 3 échecs intermittents observés par T2 « poste utilisé » ne sont **ni reproduits ni infirmés**. |
| `python tests/update-integration/run_all.py --app build/annotations-preview` (suite de la CI : fixtures existants dont `shortcuts_test`, `accessibility_test`, `navigation_errors_test`, `elements`/menus, update-core/toast/webview2, intégration hôte + installation simulée, contrôles statiques) | **Vérifié** : « ALL PASSED » (41 étapes : `shortcuts_test`, `accessibility_test`, `navigation_errors_test`, `context_menu_hover_test`, `tab_drag_test`, `split_ui_test`, `bookmarks_ui_test`, update-core 92 s, update-toast, webview2 recovery, release tooling, **intégration hôte `main.cpp` estampillé dev (mode compilé) + installation simulée**, contrôles statiques de `build/annotations-preview`). Journal : `run_all.log`. Les fixtures non estampillés (`shortcuts_test`, `accessibility_test`…) compilent `main.cpp` **sans** le mode (stubs) : ils prouvent la non-régression, pas le mode lui-même. |
| `test.ps1` (smoke `--smoke-test` du navigateur réel, build `build/smoke-v2`) | **Vérifié** : exit 0, `SMOKE PASS` (navigateur réel, build `build/smoke-v2` canal dev, donc **avec** le mode compilé) + `SMOKE bookmarks` ; inclut `test-bookmarks.ps1`, session/settings/address tests. Journal : `test-ps1.log`. |
| `test-elements-integration.ps1` (routeur `PageMessage`, interstitiel, menus) | **Vérifié** : exit 0, `PASS elements integration: deferrals once, deny by default, sandbox guard and bridge, schemes, labels, B01 request chip lifecycle`. |
| `python sandbox/annotations/run-sandbox.py` (commande unique) | **Vérifié** : serveurs démarrés, preview ouvert sur la page (`NAVIGATION OK http://127.0.0.1:5173/`), configs et commandes affichées ; à la fermeture du navigateur : « Serveurs du sandbox arrêtés. », plus aucun port 5173/5174 à l'écoute. Journal : `run-sandbox.log`. |
| `package.py` : `stage()` sur un payload propre + dossier `annotations\` | **Vérifié** : « Not packaged (runtime data): annotations », rien d'empaqueté sous `annotations`. |

## 4. Parcours réel sur la sandbox (build preview)

Pilotage : une **copie** du preview (`build/annotations-qa-app`, mêmes binaires, profil et magasin propres) lancée sur `http://127.0.0.1:5173/` avec le port de débogage WebView2 en loopback (variable d'environnement de ce seul processus) pour les mouvements/clics **dans la page** (`Input.dispatchMouseEvent`), UI Automation et messages postés **à nos propres fenêtres** pour le chrome natif. L'économiseur d'écran détenait le bureau d'entrée (SendInput et captures d'écran impossibles) : captures par `PrintWindow`. Aucune autre application pilotée.

| Étape | Preuve | Statut |
| --- | --- | --- |
| Activation par le bouton « Annoter » (clic sur la barre) | journal `ANNOTATE mode on (62 ms …)`, `innerHeight` 734 → 702 (bande 32 DIP), poignée et hôte présents ; `04-card-no-agent.png` (bouton actif, bandeau) | Vérifié |
| Activation/sortie par **Ctrl+Maj+A** (`Key()`), sortie par **Échap** | journal `mode on` / `mode off (toggle)` / `mode off (escape)` ; après sortie : `window[Symbol.for('browser.annotation')]` indéfini, aucun `browser-annotation-layer`, `innerHeight` revenu à 734 | Vérifié |
| Survol : cadre + étiquette (`button.btn-primary · 240×44`) | `01-hover-pay-button.png`, `01b-hover-frame-zoom.png` (cadre aligné au pixel à DPR 1,25 après correction du défaut « boîte de 1 px arrondie au pixel physique », trouvé au 1er passage) | Vérifié |
| Clic consommé | bouton « Payer » non soumis (texte inchangé) ; témoins de clic des deux iframes à 0/inchangés | Vérifié |
| Popover : contexte, projet « Boutique » auto, miniature, « Ajouter » | `03-popover-note-typed.png`, `16-popover-selection.png` ; écriture `ANNOTATE written boutique/ann_… with shot` | Vérifié |
| Capture recadrée 100 % (DPR 1,25) | `09-mcp-get-shot-100pct.png` : 360×116 = (240+48)×(44+48)×1,25, cadre masqué, bouton à 30 px du bord ; 62–109 ms capture→PNG (thread pool) | Vérifié |
| Capture recadrée **150 %** (zoom 150 %, DPR 1,875) | `18-shot-zoom150.png` : 451×174 = (192+48)×(44+48)×1,875 ; `viewport.zoom` 1,5 | Vérifié |
| Sélection de texte (menu « Annoter la sélection ») | `15-page-menu-selection.png` (menu : « Annoter l'élément Ctrl+Maj+A », « Annoter la sélection », « Copier la dernière annotation (Markdown) »), `16-popover-selection.png`, `target.kind = selection`, `selectionText` « Stylo plume » | Vérifié |
| Iframe même origine / autre origine / shadow ouvert / shadow fermé / canvas | magasin : `frame.kind` `same-origin` + `div[data-testid="ship-standard"] > button.choose` ; `cross-origin` + `figure:nth-of-type(2) > iframe` ; `shadowPath ['promo-badge']` + `span[data-testid="promo-code"]` ; `secure-field` seul (texte vide) ; `canvas[data-testid="sales-chart"]` ; `12-…`, `13-…`, `14-…png` (cadres) | Vérifié |
| Re-rendu SPA (mode actif conservé), liste remplacée | « Génération 2 », 3 articles, mode toujours actif, survol des nouveaux nœuds | Vérifié |
| Sortie à la navigation (`pushState` → `SourceChanged`) popover ouvert | journal `La page a changé, sélection annulée` + `mode off (navigation)`, popover fermé, script démonté | Vérifié |
| Sortie au changement d'onglet (Ctrl+T) | `mode off (tab change)` | Vérifié |
| Sortie à la mise en veille de l'onglet | branchée (`AnnotationTabSleeping`) ; l'onglet actif n'est jamais mis en veille, donc le chemin n'est atteignable qu'après un changement d'onglet (qui sort déjà) | **Non vérifié** (non déclenchable en pratique) |
| Carte « Aucun agent » → onboarding (Claude, configuration manuelle, `.mcpb` absent) → « Copier » → Vérifier | `04-card-no-agent.png`, `05-onboarding-choose.png`, `06-onboarding-claude-manual.png` (chemin réel du bridge, serveur `browser-annotations-preview`), presse-papiers = JSON valide (`ConvertFrom-Json`), `07-onboarding-verify-waiting.png` | Vérifié |
| Statut agent lu depuis `readers\` en direct (ReadDirectoryChangesW seulement carte/onboarding ouverts) | `try-mcp.py list` → `readers\try-mcp.json` → onboarding « try-mcp (sandbox) a lu vos annotations » sans action (`08-onboarding-verify-connected.png`) ; journal `watching the store` à l'ouverture, `store watch stopped` à la fermeture | Vérifié |
| `try-mcp.py list / get / claim / resolve` | sorties dans `mcp-get-all.txt` (9 annotations, 9 images PNG) ; `claim` → carte « Prise par try-mcp » + badge 1 (`10-card-working.png`) ; `resolve` pendant que la carte est ouverte → « Résolue par try-mcp (sandbox) » + résumé, mise à jour seule (`11-card-resolved-live.png`, puis texte court corrigé `11b-card-resolved-on-activation.png`) ; `state.json` : open → claimed → resolved | Vérifié |
| « Copier la dernière annotation (Markdown) » | presse-papiers = Markdown de l'annotation (contenu de page balisé comme donnée) | Vérifié |
| **Aucune fuite** du mot de passe pré-rempli `MDP-SANDBOX-7f3a9c`, de l'e-mail saisi, du numéro de carte | `grep -r` sur `build/annotations-qa-app/annotations` (JSON + PNG) et sur toutes les sorties MCP `get` : 0 occurrence | Vérifié |
| UIA : bouton « Annoter » (toggle, Ctrl+Maj+A), bandeau, popover, lanceur, carte, onboarding | arbre UIA lu par `System.Windows.Automation` (nœuds `AnnotateButton`, `AnnotationBanner`/`AnnotationQuit`, `AnnotationNote*`, `AnnotationLauncher`, carte, onboarding) ; « Ajouter », « Annuler », lanceur, « Connecter un agent », « Continuer », « Copier la configuration », « C'est ajouté », « Terminé » actionnés par `Invoke` | Vérifié |

Défauts trouvés et corrigés pendant ce parcours : cadre 20 % trop court à DPR 1,25 (boîtes de 1 px) ; clics dans une iframe (même ou autre origine) non consommés quand le pointeur y arrivait directement ; `el.click()` de la page intercepté ; échelle verticale du recadrage faussée quand le mode démarre depuis le menu (viewport mesuré avant la bande) ; débordement « il y a quelques secondes » dans la carte ; note avec retour à la ligne final ; texte « Extension en un clic » mensonger sans `.mcpb`.

Limites de preuve : après un moment, l'écran s'est éteint ; `PrintWindow` ne restitue alors plus les surfaces Direct2D (chrome, popover) — les captures 12/13/14/19/11b n'en montrent que la page et les fenêtres en couches. Les surfaces natives sont montrées par les captures prises plus tôt (03, 04–08, 10, 11, 15, 16) et par le harnais T2.

## 5. Coût « mode jamais activé » (protocole de l'audit de performances)

`python tests/annotations/idle_measure.py --runs 3` — `browser.exe` seul, copie neuve par run, page sandbox, 15 s de stabilisation puis fenêtre de 30 s : CPU = Δ`GetProcessTimes`, écritures = Δ`GetProcessIoCounters`, puis threads/handles/objets USER-GDI. `baseline` = `build/annotations-baseline` (`--no-annotations`), `never` = preview jamais activé, `used` = preview activé puis quitté une fois avant la fenêtre. Brut : `build/annotations-v1-qa/idle.json`.

| variante | CPU hôte (% d'un cœur) médiane [min–max] | écritures (ops / octets) | threads | handles | USER | GDI | dossier `annotations` créé |
| --- | --- | --- | --- | --- | --- | --- | --- |
| baseline | 0,677 [0,260–1,250] | 0 / 0 | 414 | 2 579 | 107 | 23 | non ×3 |
| never | 0,573 [0,521–1,719] | 0 / 0 | 428 | 2 715 | 107 | 23 | non ×3 |
| used | 1,302 [0,781–2,240] | 0 / 0 | 448 | 2 829 | 115 | 27 | non ×3 |

- **Vérifié** : jamais activé = aucune écriture disque, aucun dossier `annotations\`, aucune ligne `ANNOTATE` au journal ; dans un même run, `never` et `baseline` ont exactement les mêmes threads (413/413), handles (2 569/2 569), objets USER (105/105) et GDI (23/23) — aucune fenêtre, aucun thread, aucune ressource créés tant que le mode n'est pas ouvert. (Les écarts 414/428 entre runs viennent du moteur, ils touchent les deux variantes.)
- Après une utilisation : +8 USER / +4 GDI (bandeau, lanceur, fenêtre de messages, gardés cachés pour réutilisation, comme prévu par T2), +≈ 20 threads (thread pool Windows, horloge de frames bloquée). Aucun thread chaud : sonde thread par thread sur 60 s après la sortie, aucun thread au-dessus de 63 ms / 10 s, identifiants tournants.
- **Non vérifié** (précision) : la différence de CPU hôte entre variantes n'est pas mesurable proprement ici — le bruit de fond de la machine (autres agents, économiseur d'écran) donne 31 à 578 ms par fenêtre de 10 s **avant même** l'activation ; la médiane `used` plus haute n'est pas attribuable au mode avec ces données.

## 6. Non vérifié / limites connues

- **Connexion réelle de Claude Desktop ou Codex** : non faite (interdit de modifier leur configuration) ; le client MCP est `try-mcp.py` (même protocole stdio, `initialize` 2025-11-25). Le rendu effectif de l'image d'outil chez ces clients reste à démontrer (plan §3.4).
- **Ombres, mouvement et rendu des surfaces natives à l'écran** pendant ce parcours : les animations n'ont pas pu être observées (écran éteint / bureau de l'économiseur) ; elles sont couvertes par le harnais T2 (modèle pur + fenêtres réelles) uniquement.
- **Entrée clavier réelle** (SendInput) : remplacée par des messages postés et UIA ; la validation du popover par la touche Entrée postée n'a pas déclenché l'envoi pendant le premier passage (l'action a été faite par UIA « Ajouter ») — à revérifier au clavier physique.
- **Mise en veille de l'onglet annoté** (voir tableau), **plusieurs fenêtres**, **vue partagée** (le mode suit l'onglet actif ; le lanceur se place sur son panneau) : non exercés.
- **Purge de rétention** : appelée à la première activation (testée unitairement par T1), pas observée sur des données de plus de 7/30 jours.
- **iframes d'autres origines** : boîte seulement (V1) ; iframes dans une racine shadow ouverte : pas de bouclier (le clic n'y serait pas consommé).
- Le titre de l'onboarding vérifié dit « Claude Desktop est connecté » même si le lecteur est un autre client (texte T2 fondé sur l'app choisie).
- La note ajoutée avant la correction « retour à la ligne final » garde ce caractère dans le magasin QA.
- Le presse-papiers de la session a été utilisé par le parcours (copie de configuration et de Markdown).
