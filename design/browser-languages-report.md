# Langues de l'interface (français / anglais) — rapport

Mandat : `build/browser-languages/task.md`. Design validé par l'utilisateur : Paper p-K-0 (`design/browser-languages-paper-report.md`). Architecture et plan : `design/browser-languages-plan.md`.

## Ce qui change pour la personne

- **Réglages › Langue / Language**, sous Profils :
  - le menu natif propose deux choix, écrits dans leur propre langue (Français, English) ;
  - la coche est sur la langue enregistrée.
- **Un seul choix pour tout le navigateur** : il est enregistré dans `<racine des données>\ui-language`.
  - Copie portable : à côté de `browser.exe`. Copie installée : dans `<root>\data`.
  - Il vaut pour tous les profils ; aucun réglage de profil n'est modifié.
- **Le français reste la langue par défaut** : sans fichier, avec un fichier illisible, d'une autre version ou avec une valeur inconnue.
- **Le changement s'applique au prochain démarrage.**
  - La note « Au prochain démarrage. » / « Applies at next start. » reste visible sous la rangée (à x 44) tant que la langue enregistrée diffère de celle de la fenêtre.
  - Une fenêtre ouverte ne bascule jamais à moitié.
  - Chaque fenêtre est son propre processus : une fenêtre ouverte après le choix démarre dans la nouvelle langue.
- **Si l'enregistrement échoue** :
  - la valeur reste l'ancienne ;
  - « Impossible d'enregistrer la langue. » / « Couldn't save the language. » s'affiche en encre danger.
  - Le texte se suffit à lui-même, sans compter sur la couleur.
- **WebView2** reçoit `fr-FR` ou `en-US` (`EngineOptions::put_Language`) à la création de chaque environnement et pour la sonde de version.
- **Section « Annotations »** : elle s'appelle désormais ainsi dans les deux langues (commit 7e7ad3b). Les fonctions d'agents sont inchangées.

## Ce qui ne change pas

Les données ne sont jamais traduites ni renommées :

- noms de profils (« Personnel » reste « Personnel » dans une fenêtre anglaise) ;
- favoris ;
- annotations écrites ;
- titres de pages ;
- URL, fichiers.

Seul un profil **créé** après le choix reçoit le nom par défaut de la langue courante (« Personal »).

## Architecture (résumé)

- **Catalogue** `src/i18n-catalog.inc` : 1 309 messages `I18N(id, L"fr", L"en")`.
  - Identifiants sémantiques, emplacements `{0}`… dans des phrases entières.
  - 27 paires de pluriels `_one` / `_other`, avec la règle française (0 et 1 au singulier) et la règle anglaise.
  - 44 entrées identiques dans les deux langues, chacune marquée `// same: <raison>` (marques, « Annotations », « Notifications »…).
- **Moteur** `src/i18n.h` :
  - `TR` / `TRF` / `TRP` ;
  - `I18n::Text` pour les tables, qui garde un identifiant et se résout à chaque usage, sans jamais figer de texte au démarrage ;
  - conversion UTF-8 ;
  - formats décimal et pourcentage par langue, sans `setlocale`.
  - La langue est fixée une fois par processus (lecture atomique, sûre depuis tout thread).
- **Préférence** `src/ui-language.h` :
  - analyse stricte, avec les statuts `ok`, `missing`, `unreadable`, `unknown-version` et `invalid`, journalisés `UI LANGUAGE <fr|en> (<statut>)` ;
  - écriture atomique `ProfileStore::WriteAtomic` ; un échec renvoie `false`, jamais un faux succès.
- **Démarrage** (`wWinMain`) : la langue est lue juste après `ResolveBrowserPaths()`, avant toute fenêtre, dialogue ou WebView2.
  - Le lanceur `update-helper.exe` lit aussi `<root>\data\ui-language` pour son unique message d'erreur.
- **Migration portable → installée** : le helper copie `ui-language` comme `session` ou `downloads`.
  - Le paquet ne l'embarque jamais (`tools/release/package.py`, `RUNTIME_DATA` et `ui-language.tmp*`).

## Inventaire et exceptions (vérifiés par l'outil)

`tools/i18n/check_strings.py` est lancé par `tests/update-integration/run_all.py`.

1. **Le catalogue** :
   - identifiants uniques ;
   - FR et EN non vides ;
   - mêmes emplacements `{n}` ;
   - paires de pluriels complètes ;
   - EN identique au FR seulement si la raison est écrite ;
   - pas d'accent français dans l'anglais.
2. **Toutes les sources `src/*`** : tout littéral qui ressemble à du texte d'interface (accents, mots, libellé capitalisé, texte dans du HTML brut) est une erreur s'il n'est pas classé.
   - Classement accepté : journal, identifiant, format de fichier, marque, ligne ou région `i18n-exempt` avec une raison écrite, ou fichier entier classé avec sa raison.
   - Résultat actuel : **0 constat**.

Fichiers classés hors interface, avec leur raison (dans l'outil) :

| Fichier | Raison |
|---|---|
| `debug-sandbox.h` | page QA interne `browser://debug` en français ; les composants de production qu'elle ouvre sont traduits |
| `motion-lab.h`, `motion-tuning.h` | labo d'animation (`--motion-lab`), outil de revue |
| `password-demo.h` | surface de démonstration B05, comptes fictifs, ouverte seulement depuis la sandbox |
| `e2e-fixture-pin.h` | crochet de test |
| `mcp-bridge.cpp`, `strict-json.h` | protocole MCP et JSON pour les agents (texte développeur) |
| `annotations.cpp` | magasin d'annotations : documents Markdown/MCP pour les agents (schéma stable en français), résumés d'événements stockés comme données, détails d'erreur que l'interface enveloppe toujours dans une phrase traduite |
| `annotation-script.h`, `audio-script.h` | scripts injectés : messages de protocole et CSS |
| `svg-path.h`, `update-package.cpp`, `annotations-migration.h`, `trace-erase.h`, `accessibility.h` | données de tracé, format de paquet, journaux, plomberie UIA |

Exceptions repérées dans le code (`i18n-exempt` / régions) :

- harnais `--smoke-test` / `--split-test` / bookmarks smoke ;
- noms d'état du spécimen de composants ;
- codes moteur `WebView2 : CERTIFICATE_*` (code technique affiché tel quel) ;
- marques de sites (`site-name.h`) ;
- lignes de commande ;
- journaux sur plusieurs lignes ;
- identifiants d'activité UIA ;
- manifeste `.mcpb` écrit pour Claude Desktop (métadonnées du paquet) ;
- messages du mode test de connexion.

## Changements visibles en français (honnêteté)

- **Téléchargements et historique** : leurs libellés étaient en anglais même dans l'interface française (Paper les avait dessinés en anglais : « Show in folder », « Today », « 50.4/200MB », « No history yet »). En français, ils sont maintenant en français (« Afficher dans le dossier », « Aujourd'hui », « 50,4/200 Mo », « Aucun historique pour l'instant »). En anglais, ils restent exactement la copie Paper.
  - Les fixtures de ces deux tiroirs sont maintenant lancées en anglais, de façon explicite ; le français est vérifié dans `tests/i18n_test.cpp`.
- **Toast de mise à jour** : il était aussi en anglais (« New update available », « 50.4/200.0MB »). En français : « Nouvelle mise à jour disponible », « 50,4/200,0 Mo ». En anglais, il reste exactement la copie Paper.
  - `tests/update-toast/toast_tests.cpp` vérifie d'abord le modèle en français, puis se place explicitement en anglais pour les textes Paper et les captures.
  - Premier passage de la suite complète (`run-all-1.txt`) : la seule section en échec était celle-ci, car ses attentes anglaises tournaient sous la langue par défaut. Elle est corrigée sans affaiblir le test (`update-toast-1.txt` : 562 vérifications, 0 échec).
- Le texte d'appel de la barre d'adresse « Search, browse, whatever » (copie Paper) est conservé tel quel dans les deux langues.

## Tests et preuves

| Vérification | Résultat | Journal / preuves |
|---|---|---|
| `tests/i18n_test.cpp` (strict `-Werror`) | 54 vérifications | `build/browser-languages/run-all-*.txt` |
| `tests/language_settings_test.cpp` (tiroir réel FR et EN) | 30 vérifications | captures `build/browser-languages/captures/settings/{fr-root,fr-pending-en,fr-save-failed,en-pending-fr,en-root}-{96,144}.png` |
| Contrôle de couverture `tools/i18n/check_strings.py` | 0 constat | idem |
| E2E vrai processus `tests/e2e/language_e2e.py` (copie jetable `build/e2e-language`) | 24 vérifications | `build/browser-languages/language-e2e-1.txt`, captures `build/browser-languages/captures/e2e/*.png` |
| Migration portable → installée et paquet | `e2e_install.py`, `test_release_tools.py` | `run-all-*.txt` |
| Build dev complet (`tools/release/build.py`) | OK, 0 avertissement | `build/browser-languages/build-dev-6.txt` |
| `test-annotation-settings.ps1` (renommage) | 116 vérifications, 7 échecs « Connecter Claude / Codex » | mêmes 7 échecs sur d060366 intact (`baseline-d060366-annotation-settings.txt`) : préexistants dans cet environnement, pas masqués |

Détail du test de bout en bout :

- sans fichier : la fenêtre est en français (« Réglages », « Historique », « Nouvel onglet ») ;
- `language=en` : « Settings », « History », « New tab » ;
- retour à `fr` : français ;
- version de fichier inconnue : français, avec le statut `unknown-version` journalisé.

## Limites connues

- **Écrans contrôlés visuellement en anglais** : les Réglages (captures du tiroir, 96 et 144 dpi) et le chrome principal (captures E2E).
  - Les autres boîtes de dialogue anglaises n'ont pas été capturées une par une. Les messages anglais sont en général plus courts que les français et les mises en page enveloppent déjà le texte, mais aucune mesure n'a été faite écran par écran.
- **Interface propre au moteur WebView2** (menus contextuels du moteur, pages internes) : elle reçoit `fr-FR` ou `en-US`, mais ce n'est pas vérifié par capture ici.
- **Boîtes de dialogue Windows** (choix de fichier, certificats) : elles suivent la langue de Windows, comme prévu.
- **Données en français dans une fenêtre anglaise** :
  - le Markdown exporté et les messages du magasin d'annotations destinés aux agents ;
  - les résumés d'événements déjà écrits ;
  - les pages QA internes.
- **Contrôle de couverture** : il est heuristique. Il attrape les accents, les mots et les libellés capitalisés, mais un mot anglais isolé en minuscules dans un littéral resterait invisible. Les fichiers classés le sont avec une raison relue.
- **Préférence et profils** : la préférence est lue depuis la racine des données avant le choix du profil, donc elle est globale par construction. Aucun E2E dédié avec un second profil n'a été lancé.
