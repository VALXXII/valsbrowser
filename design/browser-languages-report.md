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
  - L'argument moteur suit la même langue : `--lang=fr` ou `--lang=en-US` (`I18n::WithEngineLang`). Avant le suivi, `--lang=fr` restait écrit en dur à côté de `Language=en-US` (relevé par la revue indépendante de 80171f9). Un `--lang=` passé par la personne est conservé.
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

- **Catalogue** `src/i18n-catalog.inc` : 1 339 messages `I18N(id, L"fr", L"en")`.
  - Identifiants sémantiques, emplacements `{0}`… dans des phrases entières.
  - 29 paires de pluriels `_one` / `_other`, avec la règle française (0 et 1 au singulier) et la règle anglaise.
  - 47 entrées identiques dans les deux langues, chacune marquée `// same: <raison>` (marques, « Annotations », « Notifications », couleurs « orange », « cyan »…).
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
   - Seuls les **arguments** d'un appel de diagnostic (`Log(...)`, `Fail(...)`…, jusqu'à la parenthèse fermante, même sur plusieurs lignes) sont exemptés : un `Log(...)` sur une ligne ne cache plus un `Announce(...)` ou un autre texte visible à côté. Un mot isolé accentué (« déconnecté ») compte comme du texte.
   - L'outil vérifie d'abord ces deux règles sur ses propres exemples (`self_test`).
   - Résultat actuel : **0 constat**.

**Correction après revue (commit de suivi sur 80171f9).** La première version de l'outil sautait toute ligne contenant un appel de journal, et traitait un mot accentué seul comme un identifiant. Une fois l'outil corrigé, il a trouvé **30 textes réellement oubliés**, tous traités :
- traduits : annonces UIA du mode Annotation (« … déconnecté », « Annotation rouverte », « Impossible de rouvrir », « Impossible d'activer le mode Annotation… »), « … connecté » (connexion d'agent), le nombre de résultats des favoris, le résumé d'un dossier de favoris, « Barre de favoris affichée / masquée », le nom et l'annonce du séparateur de panneaux, la raison « Download unavailable » d'un téléchargement, le dialogue d'arrêt WebView2 (titre et texte), les trois messages de suppression de profil, les noms de couleur de profil (lus et affichés), les touches de la barre de recherche (« Maj+Entrée », « Entrée », « Échap ») ;
- exemptés avec une raison : les identifiants d'activité UIA (`Annotation`, `Bookmarks`), le résumé d'événement « libérée » écrit par le magasin (donnée comparée, jamais affichée telle quelle), le marqueur de journal `NAVIGATION OK`, le journal de la sandbox QA (`SandboxLog`).

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
- Le texte d'appel de la barre d'adresse est traduit : « Rechercher ou saisir une adresse » en français ; en anglais, il reste la copie Paper « Search, browse, whatever ». La première version gardait la copie anglaise dans les deux langues ; c'est corrigé, car la demande est une interface entièrement française.

## Tests et preuves

| Vérification | Résultat | Journal / preuves |
|---|---|---|
| `tests/i18n_test.cpp` (strict `-Werror`) | 66 vérifications (dont texte d'appel, `--lang` dans les deux langues, couleurs, touches, pluriels des favoris) | `build/browser-languages/run-all-3.txt` |
| `tests/language_settings_test.cpp` (tiroir réel FR et EN) | 30 vérifications | captures `build/browser-languages/captures/settings/{fr-root,fr-pending-en,fr-save-failed,en-pending-fr,en-root}-{96,144}.png` |
| Contrôle de couverture `tools/i18n/check_strings.py` | 0 constat | idem |
| E2E vrai processus `tests/e2e/language_e2e.py`, version de suivi (copie jetable `build/e2e-language`) | 72 vérifications, 0 échec | `build/browser-languages/language-e2e-2.txt`, captures `build/browser-languages/captures/e2e-printwindow/*.png` |
| E2E, première version (avant revue) | 24 vérifications | `language-e2e-1.txt`, captures `captures/e2e/*.png` ; voir la note ci-dessous |
| Migration portable → installée et paquet | `e2e_install.py`, `test_release_tools.py` | `run-all-*.txt` |
| Build dev complet (`tools/release/build.py`) | OK, 0 avertissement | `build/browser-languages/build-dev-6.txt` |
| `test-annotation-settings.ps1` (renommage) | 116 vérifications, 7 échecs « Connecter Claude / Codex » | mêmes 7 échecs sur d060366 intact (`baseline-d060366-annotation-settings.txt`) : préexistants dans cet environnement, pas masqués |

Détail du test de bout en bout (version de suivi, `language-e2e-2.txt`) :

- **Partie A, fichier écrit par le harnais** (le lecteur de démarrage seul) :
  - sans fichier : la fenêtre est en français (« Réglages », « Historique », « Nouvel onglet ») ;
  - `language=en` : « Settings », « History », « New tab » ;
  - retour à `fr` : français ;
  - version de fichier inconnue : français, avec le statut `unknown-version` journalisé.
- **Partie B, le vrai chemin des Réglages** (le harnais n'écrit aucun fichier) :
  - dans un processus français : Réglages › Langue › « English », choisi par UI Automation (Invoke sur le bouton Réglages, Expand sur la rangée, Invoke sur l'élément du menu) ;
  - le fichier contient alors `language=en` ; la rangée affiche « English » et « Au prochain démarrage. » ; la fenêtre ouverte reste en français ;
  - un processus neuf démarre en anglais à partir de ce fichier, sans y toucher ;
  - même chose en sens inverse depuis l'anglais (« Français », « Applies at next start. »), puis un processus neuf en français.
- **Isolement** : aucune fenêtre n'est mise au premier plan par le harnais, aucune touche ni aucun clic n'est envoyé. Les captures passent par `PrintWindow` de notre propre fenêtre seulement, sans lecture de l'écran.
  - Windows peut tout de même activer une fenêtre que le harnais vient de lancer, comme pour tout programme démarré.
  - Le tiroir des Réglages est une fenêtre séparée : il n'apparaît pas sur les captures de la fenêtre principale. Son état est vérifié par UI Automation.

**Note sur la première version (`language-e2e-1.txt`).** Elle mettait la fenêtre au premier plan (`SetForegroundWindow`) et capturait l'écran (`CopyFromScreen`) ; ce n'est plus le cas. Elle écrivait le fichier de préférence elle-même et ne vérifiait que le chrome d'un processus neuf : elle **n'exerçait pas** l'enregistrement par le menu des Réglages. Ce journal reste tel quel, comme trace de ce premier passage.

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
- **Deux processus de langues différentes sur un même profil** : le bail d'environnement partagé du profil ne contient pas la langue. Une revue indépendante vérifie si un processus anglais ouvert à côté d'un processus français encore actif sur le même profil se heurte à une incompatibilité d'options. Aucune politique de profil ou d'environnement n'a été changée en attendant sa reproduction.
- **Préférence et profils** : la préférence est lue depuis la racine des données avant le choix du profil, donc elle est globale par construction. Aucun E2E dédié avec un second profil n'a été lancé.
