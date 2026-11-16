# « Nouveautés » — modale native des notes de version

Branche `VALXXII/release-notes-modal`, worktree `release-notes-modal`, base `4e370344c82eef0583d0e33937fbe57e55f94821` (main, PR #85).
Commits locaux, rien de poussé ni fusionné :

| Commit | Contenu |
| --- | --- |
| `b1fb6e8` | source (src, asset Lottie, `package.py`) |
| `9480192` | tests (natif, e2e réel, outils de release) |
| commit suivant | ce rapport et `design/release-notes-modal-evidence/` |

Source de vérité : Paper `01M6Q7YT14ZKMGET54ASPNGQEC`, page `p-L-0`, planches `142E-0` « Nouveautés log (Mini) » et `144O-0` « Nouveautés log (Full opened) ». Lecture seule : aucun changement Paper, tokens (`b37eaa4f` avant/après), commentaires ou pages. Les anciennes planches `12VC/12WI` et `13YN` ne sont pas implémentées.

## 1. Contrat (validé par le coordinateur au checkpoint)

**Ce qui déclenche la mini.** Elle apparaît une seule fois, après une mise à jour réussie. Toutes ces conditions doivent être vraies :

- `UpdateCore::NoteStartup` a reconnu l'enregistrement `pending` du helper. Le moteur rapporte alors `Installed` dès `Engine::Start`. Cela n'arrive qu'une fois par installation, dans le processus que le helper a lancé (ack `CREATE_NEW` par nonce).
- `<installRoot>\previous` indique `installed X`, avec X au format M.m.B différent de la version courante, ou `portable <chemin>`.
- L'état de lecture n'a pas encore vu `ReleaseNotes::NotesId`.

L'état n'est écrit qu'au moment où la modale est **réellement** à l'écran (`Spec::onShown`, appelé par `Dialog::Show` après `ShowWindow`), jamais à la mise en file.

**Clé de lecture : `NotesId`, pas le numéro de build.** Une release qui oublierait de mettre à jour les notes ne remontre donc pas les mêmes notes comme « nouvelles ».

**Cas qui ne reçoivent jamais la mini.** Tous vérifiés en test natif et/ou dans le vrai navigateur :

- première installation (pas de `previous`) : on enregistre seulement une base, sans modale ;
- réparation de la même version ;
- build dev (version « dev ») ;
- lancement ordinaire, autre fenêtre ou autre processus (pas d'`Installed`) ;
- notes déjà vues ;
- état illisible, endommagé ou au format futur : ni affiché ni réécrit ;
- `previous` invalide ;
- fenêtre privée et `--smoke-test`.

**Ouverture manuelle.** Dans « À propos », le lien existant « Notes de version » (`AboutNotesLink`) ouvre la vue complète. Il n'y a ni nouveau bouton ni nouvelle mise en page. Cette ouverture n'écrit jamais l'état.

> **Changement visible :** ce lien ouvrait jusqu'ici la page GitHub de la release dans un onglet. Cet accès GitHub n'existait nulle part ailleurs dans l'interface. `ReleaseNotesUrl()` est conservée (et son test) si root préfère rétablir un lien.

**Où vit l'état de lecture.** Fichier `<racine de données globale>\release-notes` (`profileRoot`, sinon `dataDir`), indépendant du profil actif et jamais celui d'une fenêtre privée :

```
browser-release-notes 1
seen=<NotesId>
```

L'écriture est atomique (`ProfileStore::WriteAtomic`). Elle n'a lieu que si le fichier est absent ou déjà dans ce format.

**Packaging et migration.**

- `release-notes` et `release-notes.tmp*` sont ajoutés à `RUNTIME_DATA` de `tools/release/package.py` : jamais empaquetés.
- La migration portable → installé de `update-helper` n'est **pas** modifiée (pas de changement du protocole helper/installer). Après une telle migration, `previous` vaut `portable …` et l'état est absent côté installé : la mini s'affiche une fois, ce qui est l'effet voulu.

**Comportement de la file d'attente.** La mini passe par `AskModal`, donc par la file existante : elle ne préempte jamais une modale active.

Elle est lancée par `Defer`, dont le minuteur de thread n'est servi que quand la file de messages est vide. En pratique, dans le vrai navigateur, elle apparaît après la première navigation du démarrage : elle n'interrompt pas le lancement.

Le contenu est compilé dans le build (catalogue FR/EN) : aucun fetch.

## 2. Fichiers et hunks partagés (pour la résolution par root)

L'auteur actif sur `browser-data-import` / `VALXXII/import-bar-roots-dnd` touche aussi `main.cpp`, `modal.h`, le catalogue et `elements-integration.h`. Rien n'a été modifié dans ses fichiers d'import, de détection ou de leurs tests.

| Fichier | Hunks |
| --- | --- |
| `src/release-notes.h` | nouveau : modèle, état de lecture, `previous`, décision |
| `src/modal.h` | voir le détail ci-dessous |
| `src/elements-integration.h` | `#include "release-notes.h"` ; gestionnaire du lien « Notes de version » (1 ligne) ; `ShowReleaseNotes` et `ReleaseNotesAtStartup` après `ShowAboutDialog` |
| `src/main.cpp` | 2 lignes à la fin de `StartUpdates` |
| `src/i18n-catalog.inc` | 12 lignes après `about_engine_footnote` |
| `src/native-lottie.{h,cpp}` | `Lottie::Limits` optionnel ; défauts inchangés |
| `tools/release/package.py` | `RUNTIME_DATA` et préfixe `.tmp` |
| `tests/update-integration/test_release_tools.py` | 3 lignes |
| `assets/lottie/logo-spin.json` | copie octet pour octet |

Détail des hunks de `src/modal.h` :

- `Kit::DrawGlyph` : nouveau paramètre `viewMinY`, avec défaut ;
- `Kind::ReleaseNotes` ;
- `NoteSection`, `Notes` et `NotesPaper` déclarés avant `Spec` ;
- `Spec::onShown` et `Spec::notes` ;
- dans `Dialog` :
  - identifiants `NotesExpandHit` / `NoteTextKey` ;
  - blocs `NoteHead`, `NoteRule`, `NoteText` ;
  - membres du logo ;
  - `BuildNotes` / `ExpandNotes` ;
  - branche en tête de `Build`, `Fit`, `Order`, `InitialStop`, `Activate` ;
  - `ReleaseLogo` dans `FinishClosing`, `Dispose` et `Show` ;
  - `PaintNotes` et sa branche dans `Paint` ;
  - `NotesNodes` pour l'UIA ;
  - `onShown` dans `Show`, `NextWake` ;
  - accesseurs de test ;
- préréglage `ReleaseNotesSpec`.

## 3. Design : Paper face au rendu natif

Valeurs lues dans `get_jsx` (sans identifiants de nœuds) et `get_computed_styles`. Mesures natives prises sur `HeightForTest` / `BoxForTest`, sur les PNG offscreen et sur les captures du vrai navigateur.

| Élément | Paper | Natif | Statut |
| --- | --- | --- | --- |
| Mini | 343 × 218, `overflow: clip` | 343 × 218 DIP (natif) ; 343,2 × 218,4 dans le vrai navigateur à 120 DPI (arrondi en pixels) | conforme |
| Complète | 343, hauteur au contenu | FR 390 DIP (Paper 389,5 à 2x) ; EN 330 DIP | conforme |
| Surface, bordure, rayon | #171717 ; #FFFFFF0D 1 px ; 10 | #171717 ; #FFFFFF0D ; région de rayon 10 | bordure : voir écart 1 |
| Padding / gap | 16 dans une bordure de 1 (border-box) → contenu à 17 ; gap 16 | contenu à 17, gap 16 ; séparateurs à 53 et 416 DIP à 2x, aux mêmes lignes de pixels que Paper | conforme |
| En-tête | logo 20 × 20, gap 6, titre 16 Medium, -0,01em, #FFFFFFE6 × opacité 0,9 | idem ; blanc 81 % ; titre à x = 43 | conforme |
| Fermer | 24 × 24, top 10,8 / right 10,4 (boîte de padding) ; glyphe 16 #FFFFFF82 | boîte 307,6 / 11,8, 24 × 24 ; glyphe 16 à 0x82 ; survol, pression et focus de la modale existante | conforme |
| Séparateurs | 1 px, #DDDDDD à 10 % | idem (vert 43 des deux côtés) | conforme |
| Titres de section | icône 20 (viewBox 1,6 49,704 160 et 0 0 160), 16/20 Medium, +0,01em, #FFFFFFE6 | idem, origines de viewBox exactes | conforme |
| Puces / items | puce 16 (padding 2), gap 6 ; texte 16/20 Regular, +0,01em, blanc 50 % × 0,9, `text-wrap: balance`, cv01-04/cv13/tnum | idem ; équilibrage natif (largeur minimale gardant le nombre de lignes) | conforme, retours à la ligne identiques en FR (2/2, puis 1/2/2) |
| Pied de la mini | overlay 128 de haut, padding 16, dégradé #171717 de 0 à 55,98 %, bouton raised #292929, h 32, rayon 6, 14 Medium #FFFFFFCC | idem ; bouton de 17 à 326 sur les lignes 170 à 202 (mêmes lignes de pixels que Paper à 2x) | conforme |
| Animation du logo | « Icône (animée avec le fichier .json) » | `logo-spin.json` via ThorVG, blanc 50 % | voir § 4 |

**Écarts (aucun n'est une refonte) :**

1. **Bordure.** Elle est tracée en 1 pixel physique, convention de `Kit::Ring` partagée par toutes les modales. Paper trace 1 DIP, donc 2 px à 2x.
2. **Texte.** Il tombe environ 1 DIP plus bas que la capture Paper, uniformément. Les colonnes concordent à ±0,5 DIP et les icônes, séparateurs et bouton sont aux mêmes lignes de pixels. L'écart vient de la boîte de ligne de `Kit::Text`, commune à toutes les modales.
3. **Anneau de focus.** Après « Voir tout » au clavier, le focus passe sur Fermer et son anneau s'affiche. C'est le comportement des modales existantes ; Paper ne dessine aucun état de focus.
4. **Fenêtre trop basse pour la vue complète.** Le corps défile (molette, ↑/↓, Pg↑/Pg↓) avec la marque de position discrète déjà utilisée pour les longs messages. Paper ne dessine pas ce cas.
5. **Scrim.** Comme « À propos » (`Scope::Window`), la modale ajoute le voile noir 40 % des modales navigateur. Paper ne montre pas le contexte.

## 4. Logo Lottie

**Provenance.** `C:/Users/<user>/Downloads/logo-spin.json` est copié tel quel dans `assets/lottie/logo-spin.json` :

- 1 143 955 octets ;
- SHA-256 `84c8966245ff58d8eb55c32b50cf2fa5ec8e9caf76913fa28f52266a6d06e4d1` ;
- identique dans le blob git de `b1fb6e8` et dans l'original, qui n'a pas été modifié (date 9 nov.) ;
- `test-release-notes.ps1` contrôle ce hash et l'absence d'image externe.

**Contenu.**

- Lottie 5.7.4, 512 × 512, 60 fps, ip 0 / op 255 (4,25 s) ;
- un précomp `turn` (forme `contour` de 200 sommets, 154 images clés sur 360 images) ;
- un calque avec remappage temporel facilité (0 → 6 s, bezier 0,54 / 0,4) : un tour par boucle ;
- remplissage #B2B2B8 ; aucun asset image.

**Rendu.**

- Moteur de production : `NativeChrome::Lottie::Asset`, ThorVG 1.1.3, sans dépendance web.
- Les défauts (256 Kio, 120 images) refusent cet asset et protègent toujours les autres. Seule la modale passe des `Limits` optionnelles : 2 Mio, 256 images, 10 s, `cacheFrames=false`, c'est-à-dire une seule image en mémoire, jamais 255.
- ThorVG rend tout le canevas 512 dans l'emplacement de 20 DIP, en pixels physiques (20, 30 ou 40 px à 96, 144 ou 192 DPI).
- Les marges internes donnent environ 13 DIP d'encre, comme l'image fixe Paper `146I-0` (13 × 12,25). Mesures : boîte 12 à 14 px sur 20 px, 17 à 20 px sur 30 px, 23 à 27 px sur 40 px ; encre visible à tous les instants testés.
- Seule la couverture est conservée, peinte en blanc 50 % (`--color-browser-text` à `--opacity-browser-secondary`).

**Horloge.**

- `NextWake` ne demande des images que si la modale est ouverte, visible, que la fenêtre propriétaire n'est pas réduite et que le mouvement n'est pas réduit.
- À la fermeture ou à `Dispose` : lecteur et pixels libérés, plus aucun réveil (vérifié).
- Changement de DPI : l'emplacement se re-rastérise à la nouvelle taille.

**Mouvement réduit.** Il suit le réglage Windows et le réglage existant `reduce-motion`. Affichage de l'image 0, la pose de repos, vérifiée visible à 96, 144 et 192 DPI et dans le vrai navigateur (deux captures à 450 ms d'intervalle identiques).

**Repli.** Si l'asset est absent ou illisible, le glyphe fixe Paper est dessiné, sans horloge (testé).

**Limites réelles.**

- 64 px maximum par côté : au-delà de 320 % d'échelle, rien n'est dessiné en Lottie et c'est le repli fixe qui s'affiche.
- Chaque image est rendue en logiciel à la demande. Le coût CPU par image n'a pas été mesuré.
- Le parsing JSON de 1,1 Mo a lieu à chaque ouverture de la modale ; durée non mesurée.

## 5. Tests, commandes et résultats

Journaux dans `design/release-notes-modal-evidence/`, en `.txt`.

| Commande | Résultat |
| --- | --- |
| `powershell -File ./test-release-notes.ps1` | en-têtes propres (seuls et après `chrome.h`) ; hash du logo OK ; **227 vérifications, 0 échec** → `test-release-notes.txt` |
| `python -I tests/e2e/release_notes_e2e.py build/release-notes-e2e` (**vrai navigateur**) | **0 échec, aucune capture indisponible**, 81 vérifications → `release_notes_e2e-run3.txt` ; les runs 1 et 2 sont gardés : attente trop courte, puis sélecteur UIA du test |
| `python -I tools/i18n/check_strings.py` | 0 finding → `i18n-gate.txt` |
| `test-popups.ps1 -SkipPixels` (`modal_test`) | premier passage : 278 vérifications, **3 échecs** sur la liste « Période » (menu natif) → `test-popups-skippixels.txt` ; ensuite 3 relances branche et 3 relances base `4e37034` à 278/0 → `modal_test-reruns.txt`, `modal_test-base-4e37034.txt`. Échec intermittent, non reproduit, présent avant ce lot (code non touché) |
| `test-elements-integration.ps1` | PASS → `test-elements-integration.txt` |
| fixtures Lottie existantes, avec `-Werror` (native-lottie, ui, loading, frame-clock, design-system) | toutes PASS → `lottie-fixtures.txt` |
| `python -I tests/update-integration/test_release_tools.py` | 59 tests OK (1 ignoré) → `test_release_tools.txt` ; contre-épreuve rouge sans la modification de `package.py` : en échec → `test_release_tools-red-without-package-change.txt` |
| `python tools/release/build.py --out build/release-notes-dev --channel dev` | Build OK 0.3.381 (`b1fb6e8`), 0 avertissement → `build-dev.txt` |
| idem `--update-repo qa-offline/release-notes-none` → `build/release-notes-e2e` | Build OK → `build-e2e.txt` (binaire du test e2e) |
| `--channel release` | **refusé par la politique du dépôt** (commit hors `main`) → `build-release-refused.txt` |

`test-lottie.py` n'a **pas** été lancé tel quel. Il exécute `build-lottie.py`, qui reconfigurerait ThorVG dans `build/lottie-backend`, une jonction vers le dépôt principal. Ses fixtures ont été compilées et exécutées directement avec les mêmes options. `run_all` n'a pas été lancé, conformément à la consigne.

**Ce que vérifie le test natif** (vraie `MD::Dialog`, rendu offscreen D2D) :

- matrice de décision, positifs et négatifs ciblés ;
- format et écriture sur disque (pas d'écrasement d'un fichier futur ou endommagé, cible illisible, dossier absent) ;
- logo : refusé par les défauts, chargé avec les limites optionnelles, 255 images / 4,25 s, encre visible à 6 instants × 3 tailles, images réellement différentes, mémoire d'une seule image ;
- modale :
  - géométrie Paper et retours à la ligne FR identiques ;
  - focus initial sur « Voir tout », Tab qui boucle, Entrée qui déplie sur place ;
  - Échap, X et `Abort` répondent une seule fois (-1), focus rendu au contrôle du propriétaire ;
  - `onShown` une seule fois, jamais sur un `Show` refusé ;
  - UIA : groupe nommé, description complète, 7 nœuds texte, éléments sous le fondu signalés hors écran ;
  - horloge seulement à l'écran, mouvement réduit sans horloge avec affiche visible ;
  - fenêtres courtes (défilement jusqu'au dernier item, mini réduite avec bouton visible) ;
  - logo absent → repli ;
  - FR/EN aux trois DPI.

**Ce que vérifie l'e2e réel** (racines synthétiques sous `build/e2e-release-notes/<scénario>/lad/browser`, lancement avec `LOCALAPPDATA` pointé dessus) :

- Le vrai `NoteStartup` écrit l'ack attendu par le helper.
- **update-fr / update-en / reduced** :
  - mini 343,2 × 218,4 DIP à 120 DPI ;
  - noms UIA FR/EN et items cachés atteignables ;
  - état écrit seulement après l'affichage ;
  - logo qui tourne (ou figé en mouvement réduit) ;
  - Tab (`WM_KEYDOWN` posté à notre fenêtre) → focus sur Fermer ;
  - « Voir tout » par UIA Invoke → vue complète 390,4 DIP en FR et 330,4 en EN ;
  - Échap posté → fermeture ;
  - affichée une seule fois.
- **restart, first-install (base), already-seen, future-state** : aucune modale pendant 25 s, état inchangé ou base écrite.
- **about-full** :
  - commande `'A'` postée à notre fenêtre, puis UIA Invoke sur « Notes de version » → vue complète sans « Voir tout » ;
  - X par UIA → fermée ;
  - aucun état écrit.

## 6. Captures

Toutes les captures du vrai navigateur ont passé le contrôle de surface (#171717 au centre) : aucune n'est noire ni indisponible.

| Dossier | Contenu |
| --- | --- |
| `evidence/offscreen/` | **rendu natif offscreen** (fixture `RenderForTest`, pas le navigateur) : `mini-{fr,en}-{96,144,192}.png`, les instants du logo `-t1..t3`, `full-{fr,en}-{96,144,192}.png`, `mini-reduced-*.png`, `mini-no-lottie-144.png`, `full-short-scrolled-144.png` |
| `evidence/real-app/` | **vrai navigateur**, PrintWindow de notre propre fenêtre de dialogue, à 120 DPI seulement (le DPI de l'écran de cette session) : `update-fr-mini-120`, `update-fr-full-120`, `update-en-*`, `reduced-*`, `about-full-full-120` |
| `evidence/paper/` | captures Paper 2x de `142E-0` / `144O-0` ; comparaison par lignes et colonnes de pixels dans `paper-vs-native-192.txt` |

Les 96, 144 et 192 DPI ne sont prouvés qu'en offscreen : le DPI de l'écran n'a pas été modifié.

## 7. Builds ouvrables

- `build/release-notes-dev/browser.exe` : dev 0.3.381, commit `b1fb6e8`, flux de mise à jour par défaut. Ouverture manuelle : « À propos » › « Notes de version ».
- `build/release-notes-e2e/browser.exe` : même source, flux pointé vers un dépôt inexistant, pour l'e2e.

La vérification de mise à jour automatique au démarrage contacte github.com, comme tout build dev ; elle répond 404 et reste silencieuse. Rien n'est téléchargé.

## 8. Limites et points non vérifiés

- Le vrai navigateur n'a été observé qu'à 120 DPI.
- Le focus clavier dans l'e2e dépend de la fenêtre au premier plan ; il a été observé ici. Le script le déclare UNAVAILABLE s'il ne l'est pas.
- La restauration du focus vers la page après fermeture n'est prouvée qu'en natif (contrôle `EDIT` du propriétaire), pas dans le vrai navigateur.
- Une vraie mise à jour bout en bout (helper qui télécharge et bascule) n'a pas été rejouée. L'e2e reproduit exactement l'état que le helper laisse (`current`, `previous`, `updates/pending`) et passe par le vrai `NoteStartup`.
- Une réparation par le setup garde l'ancien `previous` : si les notes n'ont jamais été vues, la mini peut s'afficher une fois après une réparation.
- Le build `--channel release` est impossible hors `main`. La CI le produira après fusion.
- Aucune publication, aucun push, aucune PR.
- Copie : contenu Paper actuel (5 points déjà livrés), sans promesse sur les paiements ni sur de futurs travaux. L'anglais est un équivalent concis, à relire par l'utilisatrice ou l'utilisateur.
