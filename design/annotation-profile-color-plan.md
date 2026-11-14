# Mode Annotation × couleur du profil — audit et plan

Base épinglée : `main` = `6c57d8847030ac37871c24edc492c3f08bef9102` (PR70 annotations, contient Profiles PR69 `acd90a0`).
Vérifié au démarrage : `origin/main` = HEAD du worktree, aucun delta.

## 1. Contrat source (relu, non modifié)

- `ProfileColor::AccentFill` : remplissage sous texte blanc, L résolu pour blanc ≥ 4,6:1 (luminance relative ≈ 0,175 pour toutes les teintes).
- `ProfileColor::Ink` : encre L 0,78 (premier plan sur chrome sombre).
- `ApplyProfileChrome` → `SetAccents(fill, ink, animate)` → `Design::Accent::Current/InkCurrent` ; `ProfileAnimTick` (timer 15 ms existant) fait le fondu 120 ms ; mouvement réduit = direct.
- P07 : profil neutre « Personnel » → remplissage = bleu de référence `#3E67FF`, encre = gris `#B3B3B3`.
- Fenêtre privée : mode Annotation interdit (`Available()` : « indisponible en navigation privée ») → hors périmètre de test.
- Le bleu de référence `#3E67FF` a la même luminance (Y 0,179, blanc 4,58:1) que les remplissages profil (Y 0,173–0,177) : toutes les nuances Paper du mode sont des dérivées du **remplissage**.

## 2. Audit de l'existant (main fusionné)

| Surface | Lecture actuelle | Verdict |
|---|---|---|
| Bouton « Annoter » barre d'outils (plaque 16 %, marque, barre) | `Accent::Get()` à chaque peinture du chrome, repeint par `ProfileAnimTick` | **Déjà raccordé** (même motif que le bouton audio). Conserver, tester. |
| Bandeau : puce BETA | `Accent(.16)` par peinture | dynamique, mais la fenêtre du bandeau n'est pas repeinte au changement (voir D2) |
| Bandeau : dégradé `#233FD6`, bordure `#3E67FF3D`, libellé `#5B84FF`, BETA `#6A8FFF`, rampe de la marque `#D1DFFF/#3296FF/#2F63E7` | **constantes bleues** | D1 |
| Trame du bandeau (vague) `#3E67FF96 / #7FA0FFDC` | constantes | D1 |
| Voile d'entrée (lavis, liseré, trame) `#3E67FF` | constantes + LUT | D1 |
| Curseur « viseur » : point `#3E67FF` | SVG constant | D1 |
| Lanceur : contour `#3E67FF99/66` | constantes | D1 |
| Note : bordure du champ focalisé `#3E67FF99` | constante | D1 |
| Boutons primaires « Ajouter », « Terminé », « Annoter cette page » | `Accent()` + survol/pression = **voile blanc** 8 %/6 % | D3 (contraste) |
| Coches, anneau de progression, disque du lanceur, puce du compteur | `Accent()` par peinture | dynamiques ; repeinte manquante (D2) |
| Script de page (remplissage 8 %, arêtes 2 px, étiquette, hachures padding) | `P.accent` **figé à l'injection**, repli `62,103,255` | D4 |
| Cadre natif de capture, retrait arithmétique du remplissage | `AccentRgb()` = `Accent::Get()` **au moment de l'appel** | D4/D5 |
| Gap (rose `#E0479E/#D63E92`), danger `#FF7B6E`, textes neutres | rôles propres | **inchangés** |
| Historique : profil d'origine (`source.profile`) | non affiché ; donnée du magasin global | **inchangé** (aucune coloration par profil courant) |

Défauts reproductibles identifiés (après fusion) :

- **D1** — profil coloré (ex. vert `#1A8641`) : bandeau, trame, voile, curseur, lanceur, bordure de note restent bleus ; seuls BETA/boutons/coches prennent la teinte → identité incohérente.
- **D2** — changement de teinte/intensité pendant le mode (réglages, ou autre fenêtre du même profil via `BrowserProfilesChanged`) : `ProfileAnimTick` n'invalide que le chrome ; bandeau, note, lanceur, historique, modale gardent l'ancienne couleur jusqu'à la prochaine interaction.
- **D3** — survol/pression des boutons primaires éclaircissent le remplissage (voile blanc) : blanc < 4,5:1 sur tout remplissage résolu à 4,6:1 (règle du projet `Design::Accent::StatesOf` : approfondir, jamais éclaircir).
- **D4** — le script de page garde la couleur de l'injection : changement en cours de mode → cadre/étiquette/hachures/curseur de l'ancienne couleur. Pire : entrer pendant le fondu de 120 ms installe une **teinte intermédiaire** comme valeur définitive (`Accent::Get()` est la valeur animée).
- **D5** — capture : le retrait du remplissage 8 % utilise `Accent::Get()` au moment du cliché, pas la couleur réellement peinte par la page → résidu teinté si la couleur change entre injection et cliché.

## 3. Décisions de rôle

Principe : rester dans le rôle **remplissage** (`Accent::Get`, celui de toutes les nuances Paper du mode) ; aucune palette séparée, aucun changement du modèle de couleur des profils.

1. **Tons relatifs** (`src/annotation-accent.h`, pur) : chaque constante Paper dérivée de `#3E67FF` devient une transformation OKLCH relative du remplissage courant : ΔL, rapport de chroma, Δh mesurés entre la constante Paper et `#3E67FF`, gamut sRGB ajusté en chroma (teinte conservée). Remplissage = `#3E67FF` (Personnel neutre) → **valeur Paper exacte** (court-circuit), donc aucun changement visuel pour le profil par défaut.
2. **Textes en ton accent** (« Mode Annotation », « BETA ») : plancher de contraste minimal — L relevée par pas jusqu'à 4,5:1 contre le fond composé réel (dégradé du bandeau au bas du libellé ; puce 16 % pour BETA). Pas d'encre `L .78` : elle rendrait le libellé gris pour Personnel (P07) alors que tout le bandeau est en remplissage.
3. **Alphas** (bordure du bandeau, contour lanceur, bordure note focalisée, lavis/liseré/trame du voile, trame de la vague) : remplissage × alpha Paper.
4. **Boutons primaires** : `Design::Accent::StateColor(fill, hover, press)` (approfondir, blanc ≥ 4,5:1 à chaque état), comme `modal.h` et `profile-dialog.h`.
5. **Valeur réglée vs valeur affichée** : `Design::Accent::Settled()` = cible du fondu (stockée) ; `Get()` reste la valeur affichée. Surfaces natives = valeur affichée (elles suivent le fondu existant). Page / cadre natif / retrait du remplissage = valeur réglée envoyée à la page (`pageAccent`), jamais une teinte intermédiaire.
6. **Propagation** : un seul point d'intégration `AnnotationAccentChanged()` appelé par `SetAccents` (instantané) et `ProfileAnimTick` (chaque pas du fondu existant). Il repeint les surfaces visibles et, si la cible a changé, envoie `accent` au script de la page du mode (si l'onglet du mode est à l'écran). Pendant une capture (cadre natif / cliché en cours) l'envoi est différé jusqu'à `ShotDone` pour que page, cadre et retrait restent sur la même couleur. Aucun nouveau timer.
7. **Changement d'espace/profil dans la fenêtre** : comportement existant conservé (`Select` → `AnnotationTabChanged` → `Exit("tab change")`, brouillon effacé seulement si la page change) ; rien n'est envoyé au DOM de l'ancien profil, la couleur B s'applique à la prochaine entrée.
8. **Inchangé** : gap rose, danger, textes neutres, focus blanc, magasin global/originProfile, MCP, coffre.

## 4. Plan TDD

1. Tests purs (`tests/annotations-ui` + test unitaire) : tons relatifs (Personnel = Paper exact, teintes réelles, intensité 0/limite), planchers de contraste mesurés, `Settled` vs `Get` pendant le fondu, `StateColor` blanc ≥ 4,5 sur survol/pression.
2. Rendus raster (harnais natif existant) : bandeau, note, lanceur, historique, modale, voile, curseur pour Personnel + 4 teintes ; échantillonnage des pixels = ton attendu ; contrastes mesurés sur rendu.
3. Hôte simulé (fenêtres vivantes du harnais) : `AccentChanged()` repeint sans interaction, zéro réveil au repos ensuite.
4. Script de page : op `accent` (CSS + curseur) testé sous navigateur sans tête si disponible, sinon par l'E2E sandbox existant.
5. Build strict (`-Wall -Wextra -Werror`) de l'application + régressions ciblées (annotations-ui, profile_color, design-system).
