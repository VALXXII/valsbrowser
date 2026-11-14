# Mode Annotation × couleur du profil — rapport

Base : `main` `6c57d8847030ac37871c24edc492c3f08bef9102` (PR70, contient Profiles PR69). Plan et audit : `design/annotation-profile-color-plan.md`. Preuves : `design/annotation-profile-color-evidence/` (fichiers `.txt` et PNG suivis).

## Résultat

Le mode Annotation prend le **remplissage du profil actif** (`Design::Accent`, le rôle de toutes les nuances Paper du mode) sur toutes ses surfaces d'accent : bouton « Annoter » (déjà raccordé, conservé), bandeau (dégradé, bordure, libellé, BETA, marque, vague tramée), voile d'entrée, curseur « viseur », cadre/étiquette/hachures padding de la page et sa copie native, lanceur, note (« Ajouter », bordure focalisée, coches, anneau, caret), historique (« Annoter cette page »), modale (« Terminé »).

- **Personnel (neutre, P07)** : remplissage = `#3E67FF` → valeurs Paper **exactes** pour toutes les nuances non textuelles et pour le libellé « Mode Annotation » ; **seul « BETA » passe de `#6A8FFF` à `#6E93FF`** (même teinte, L +0,02) depuis le correctif de revue ci-dessous, car `#6A8FFF` ne tient pas 4,5:1 sur toute la hauteur de sa puce.
- **Autres profils** : chaque constante Paper est la même transformation OKLCH appliquée au remplissage (ΔL, rapport de chroma, Δh), gamut sRGB ajusté en chroma ; textes avec plancher de contraste minimal (L relevée) sur le fond composé réel.
- **Inchangés** : rose gap `#E0479E/#D63E92`, danger, textes neutres, anneau de focus blanc, magasin global, `source.profile`, MCP, coffre, Paper.

## Correctifs (défauts D1–D5 de l'audit)

| | Avant | Après |
|---|---|---|
| D1 | bandeau, vague, voile, curseur, lanceur, bordure de note en bleu fixe | tons du remplissage (`src/annotation-accent.h`) |
| D2 | changement de couleur pendant le mode : surfaces natives figées | `AnnotationAccentChanged()` depuis `SetAccents` et chaque pas de `ProfileAnimTick` (timer existant, aucun nouveau) → `AccentChanged()` de chaque surface visible |
| D3 | survol/pression des boutons primaires = voile blanc (blanc jusqu'à 3,55:1) | `Design::Accent::StateColor` (approfondi), blanc ≥ 4,58:1 dans tous les états |
| D4 | script de page : accent figé à l'injection, teinte intermédiaire possible si entrée pendant le fondu | valeur **réglée** (`Design::Accent::Settled`) à l'injection ; commande `accent` (règles CSS + point du curseur) envoyée une fois par changement, jamais un pas du fondu |
| D5 | retrait du remplissage de capture lisait l'accent courant | lit `pageAccent` (ce que la page peint) ; envoi différé pendant une capture (`ShotDone`) |

Changement d'espace/profil dans la fenêtre : comportement existant conservé (le mode se termine avec la page de l'ancien profil, rien n'est envoyé à son DOM) ; la nouvelle entrée prend la couleur du nouveau profil. Mouvement réduit : `SetAccents` direct → un seul appel.

## Défaut trouvé en revue indépendante (e204fd6) et correction

La revue indépendante de `e204fd6` a reproduit un **P2** : « BETA » était plafonné au contraste mesuré sur la **ligne du
milieu** de sa puce (y 16), alors que le dégradé du bandeau s'éclaircit vers le bas. Profil teinte 288 / intensité 0,10
(remplissage `#756DAF`) : BETA `#948EC1` mesuré **4,32:1** sur la ligne 38 px (y 19) au fond `#302E42` (rendu natif, texture
coupée). Le rapport de `e204fd6` affirmait à tort « tous ≥ 4,5:1 » : mon harnais comparait le meilleur pixel du texte à un
seul fond pris au milieu, et le calcul pur utilisait ce même fond.

Ampleur réelle (nouvelle vérification ligne par ligne appliquée aux tons de `e204fd6`, `07b-…`) : 7 profils sur 8
échouaient pour BETA (4,33 à 4,47:1) ; le libellé n'était pas touché (il était déjà plafonné sur sa ligne du bas).

Correction (commit distinct) : `AccentTone::ReadableOnAll` plafonne chaque texte contre **toutes** les lignes de fond
composé de sa boîte, au pas de 0,25 DIP (libellé 6..26, puce BETA 7..25 : borne conservatrice, toute la puce et pas
seulement les glyphes) ; plus aucun raccourci Personnel ne contourne le seuil. Texture/points tramés inchangés.

Choix assumé : pour Personnel, `#6A8FFF` tenait 4,52:1 sur ses lignes de glyphes réelles (rendu natif), mais seulement
4,35:1 sur la ligne basse de la puce ; avec la borne conservatrice il devient `#6E93FF` (4,72:1). Une borne limitée aux
glyphes aurait gardé la valeur Paper avec 0,02 de marge, à la merci du lissage, du DPI et du zoom.

Tests ajoutés : `MeasureRows` (harnais natif) mesure chaque ligne de pixels, chaque pixel opaque de glyphe contre le fond
rendu de **cette** ligne ; profil de la revue ajouté. Rouge sur les tons de `e204fd6` (8 échecs, dont 4,325:1 pour le
reproducteur, identique à la revue) → vert après correction. Test pur exhaustif sur toutes les lignes (360 teintes × 9
intensités + Personnel) et cas reproducteur explicite.

## Contrastes mesurés (rendus 2×, harnais natif ; 8 profils réels, après correction)

| Paire | Seuil | Pire mesure |
|---|---|---|
| « Mode Annotation », pire ligne de glyphe | 4,5:1 (1.4.3) | 4,70:1 (vert, reproducteur) ; Personnel 4,74:1 |
| « BETA », pire ligne de glyphe | 4,5:1 | 4,63:1 (intensité 0) ; reproducteur 4,72:1 ; Personnel 4,72:1 |
| Blanc sur « Ajouter » repos / survol | 4,5:1 | 4,58 / 5,25:1 (Personnel) |
| Marque « Annoter » active sur sa plaque | 3:1 (1.4.11) | 3,24:1 (intensité 0) |
| Arête 2 px du cadre sur page `#FFFFFF` / `#F4F4F3` / `#000000` / `#1C1C1C` | 3:1 | 4,58 / 4,16 / 4,49 / 3,65:1 |
| Étiquette du cadre (blanc sur remplissage) | 4,5:1 | 4,58:1 |

Calcul exhaustif (`tests/annotation_accent_test.cpp`, 360 teintes × 9 intensités 0 à max, toutes les lignes de fond) : libellé ≥ 4,50:1, BETA ≥ 4,50:1, boutons ≥ 4,58:1 (`07a-…`, 31/31). Le modèle pur (dégradé linéaire) donne 4,37:1 là où le rendu natif mesure 4,32:1 pour l'ancienne valeur : c'est la vérification native ligne par ligne qui fait foi, le modèle sert au choix des tons.

## Tests exécutés (niveaux distincts)

1. **Unitaire pur** `tests/annotation_accent_test.cpp` (`-Wall -Wextra -Werror -pedantic`) : 23/23 — `03-annotation-accent-test.txt`.
2. **Raster + fenêtres hôte simulé** `python tests/annotations-ui/run.py` : 1290 vérifications, 0 échec — `04b-…final.txt`, PNG `pc-*`. Section `ProfileAccent` : bandeau/note/lanceur/bouton/cadre (pages claires et sombres)/voile × 7 profils ; bandeau vivant repeint par `AccentChanged()` seul, sans image d'animateur, puis 1 500 ms au repos : 0 image, 0 réveil, 0 WM_TIMER.
3. **E2E application réelle** `python -I tests/annotations/profile_color_e2e.py` (copie jetable de la build dev, profils jetables `vert`/`ambre`, CDP loopback) : 0 échec — `06b-…`, journal `06c-…`, PNG `e2e-*`. Vérifie : entrée → la page peint `rgb(27,134,65)` (remplissage `vert`) et le point du curseur ; édition de couleur depuis « une autre fenêtre » (registre + `BrowserProfilesChanged`) → **un seul** envoi `181,93,6` (valeur réglée, aucun pas du fondu), bandeau repeint ; `qa switch ambre` en mode → « mode off (tab change) », aucun envoi au DOM de `vert`, nouvelle entrée en `ambre` avec `181,93,6` ; aucune annotation écrite.
4. **Régressions** : `test-design-system.ps1` OK, `test-profiles.ps1` OK, suite annotations `run_tests.py --quick` : toutes les builds et tests passent, configurations Claude/Codex réelles inchangées (`05c-…`). Build application `tools/release/build.py --channel dev` : exit 0, aucun avertissement (`02-…`).

### Premiers échecs conservés

- `01-…baseline` : à la base `6c57d88`, le harnais UI échoue déjà sur 3 vérifications d'historique (focus clavier, Markdown copié, défilement) ; non reproduites lors des deux exécutions suivantes (instables, sans lien avec la couleur).
- `04a-…` : premier passage après patch, 35 échecs = 5 échantillons mal placés × 7 profils (texture tramée sous la puce BETA, coin arrondi et barre du bouton manqués). Corrigé dans le test, pas dans le produit.
- E2E : 1er passage interrompu (`PIL` absent pour la capture) ; 2e passage `06a-…` 6 échecs : capture d'écran noire (pas de bureau interactif → `PrintWindow`), recherche par décalage dans plusieurs journaux de profil (→ comptages), page `ambre` ouverte via CDP alors que le port reste au moteur de `vert` (→ barre d'adresse + journal).

## Tests du correctif de revue

- `07a-…` : test pur 31/31. `07b-…` : nouvelle vérification ligne par ligne exécutée sur les tons de `e204fd6` — 8 échecs
  (rouge attendu). `07c-…` : harnais natif complet avec fenêtres après correction — 1376 vérifications, 0 échec.
  PNG `pc-band-solid-*` : rendus mesurés (texture coupée).
- Non refait pour ce correctif : build application et E2E réel (changement limité au choix des tons du bandeau, couvert
  par le rendu natif ; l'E2E de ce lot reste celui de `e204fd6`).

## Limites ouvertes

- **Canal release** : `src/main.cpp:64` compile le mode Annotation hors des builds `--channel release` ; une release publique actuelle n'a ni le mode ni ces accents (signalé à root ; plan « B » en cours, hors de ce lot).
- Arête du cadre sur une page de luminance proche du remplissage (gris moyen ~`#777`) : < 3:1, inhérent à un cadre monochrome, identique avant le patch.
- Points de la trame de base sous le libellé : contraste local ~2,5:1 avec le libellé (motif Paper validé, existant, indépendant du profil).
- Envoi différé pendant une capture : vérifié par lecture de code, pas par un E2E (fenêtre de ~60 ms difficile à viser).
- `PrintWindow` ne restitue pas la bande du chrome D2D (noire dans `e2e-*.png`) ; le bandeau, la page et le lanceur y sont.
- Fenêtre privée : mode interdit, non testé.
