# Mode Commentaire v2 — valeurs exactes de Paper

Relevé du 18 novembre 2026 (worker F). Source : fichier Paper `01M6Q7YT14ZKMGET54ASPNGQEC` (« Browser Design »), hash des tokens `b37eaa4f`. Toutes les valeurs viennent de `get_computed_styles`, `get_jsx` et `get_node_info` (positions mesurées par Paper), **jamais d'une capture**. Les tableaux ont été générés directement depuis les réponses de Paper (aucune recopie à la main).

Captures de référence (export Paper 1x, PNG) : `build/chat-qa/comment-v2/paper/` — une par planche (`1-element-vise-commentaire.png` … `4-deselectionne.png`), une par bulle (`bulle-*.png`, recadrée sur la bulle), l'infobulle des étapes, la marque de la planche 4, les trois menus 6.4 et le bandeau (`bandeau-masque-selecteur.png`, `bandeau-masque.png`, `bandeau-selecteur.png`).

## Lire les tableaux

- **L×H** : taille mesurée par Paper (px CSS à 1x). Les tailles « fit-content » sont la taille rendue.
- **x,y / bulle** : position du coin haut gauche par rapport au coin haut gauche de la bulle. **x,y / élément** : par rapport au coin haut gauche de l'élément visé (le bouton « Payer », sans le cadre de sélection).
- **Styles** : propriétés CSS de Paper, tokens remplacés par leur valeur. Abréviations : `bg` fond, `radius` rayon, `size` taille de police, `lh` interligne, `weight` graisse, `font` famille, `dir` direction flex, `align`/`justify` alignements, `ws` white-space, `shrink` flex-shrink. Une propriété absente = valeur par défaut CSS (pas de bordure, pas d'ombre, opacité 1).
- `color-mix(#FFFFFF 50%, transparent)` = blanc à 50 % ; `#FFFFFFE6` = blanc à 90 % ; `#FFFFFF80` = 50 % ; `#FFFFFFCC` = 80 %.
- Police : le token `--font-browser-inter` vaut `"Inter Display"` (avec ses repli). `fontSynthesis: none`, lissage antialiased sur tout le contenu.

## Tokens utilisés (valeurs)

| Token | Valeur |
|---|---|
| `--color-browser-canvas` | `#171717` (fond de la bulle) |
| `--color-browser-raised` | `#292929` |
| `--color-browser-hover` | `#333333` |
| `--color-browser-pressed` | `#3E3E3E` |
| `--color-browser-focus` | `rgb(255 255 255 / 10%)` |
| `--color-browser-accent` | `#3E67FF` |
| `--color-browser-danger` | `#FF7B6E` |
| `--opacity-browser-focus` | `10%` |
| `--opacity-browser-secondary` | `50%` |
| `--spacing-browser-4 / 6 / 8 / 24` | `4px / 6px / 8px / 24px` |
| `--radius-browser-4 / 12 / 16` | `4px / 12px / 16px` |
| `--font-weight-browser-500` | `500` |

## Synthèse (à appliquer partout)

1. **Bulle** (toutes les planches) : largeur **305 px**, padding **12 px**, gap vertical **12 px**, rayon **16 px**, fond **#171717**. **Aucune bordure, aucune ombre** (ni `boxShadow` ni `border` dans Paper sur les 10 bulles). Hauteur = contenu (104 → 303 px selon l'état).
2. **Position de la bulle** (9 planches sur 10) : bord gauche = bord droit de l'élément **+ 12 px** (donc 8 px après le cadre de sélection, qui déborde de 4 px) ; haut = haut de l'élément **− 4 px** (aligné sur le haut du cadre de sélection). Exception : planche 3 `1AZR-0` → +10 px et haut = haut de l'élément (voir « Incohérences de Paper »).
3. **Élément visé** : cadre de sélection 2 px `#3E67FF`, fond `#3E67FF14`, débord 4 px de chaque côté, sans rayon ; étiquettes 20 px de haut, padding horizontal 6 px, coins hauts 3 px, fond `#3E67FF`, texte 13/18 `#FFFFFFE6` ; étiquette de taille collée à droite (right −4).
4. **Planche 4 (désélectionné)** : le cadre reste (2 px `#3E67FF`, **sans fond**) + icône 24×24 fond `#3E67FF`, coin bas droit 6 px, placée à (−1,4 ; −1,4) dans le cadre (collée au coin haut gauche, par-dessus la bordure). Plus d'étiquettes.
5. **Menus 6.4** : surface `#242424`, bordure 1 px `#FFFFFF1A`, rayon 12, padding 6, lignes 30 px (padding 6/10, rayon 8, survol `#FFFFFF0F`), texte 13/18 500 blanc, descriptions 12/16 `#FFFFFF80`. Modèle 300 px « s'ouvre vers le haut », effort 214 px, mode 290 px avec ombre `#00000080 0 16px 48px`.
6. **Bandeau planche 3** : masque `1AYJ-0` = calque absolu 1180×32 plein cadre, opacité 0,85, `linear-gradient(in oklab 90deg, #171717 à 0 % jusqu'à 60,66 %, #171717 plein à 82,29 %)`, posé **au-dessus** de la trame (opacité 0,85) et sous le contenu. Sélecteur `1AYP-0` : 58 px, padding 4, gap 2, rayon 10, deux boutons 24×24 rayon 6 ; actif = fond `#FFFFFF` à 10 %, icône opacité 0,8 ; inactif = sans fond, icône opacité 50 % (icône panneau tournée de 180°). Placé avant « Quitter », gap 4.

## Points tranchés par le coordinateur (18 novembre 2026)

- **Menu des modes `14RK-0` — 4 lignes exactes, textes de Paper à la lettre** (R4 corrigé : l'utilisateur a mis à jour la planche volontairement), dans cet ordre :
  1. « Accept edits » / « Automatically accept all file edits »
  2. « Auto » / « Claude handles permission decisions »
  3. « Demander » / « Propose chaque modification. » (ligne sélectionnée dans Paper : fond `#FFFFFF0F` + coche à droite)
  4. « Planifier » / « Propose un plan, ne touche à aucun fichier. »
  Les lignes 1 et 2 ont une icône 16×16 dans un cadre 16×18 (padding haut 2) ; les lignes 3 et 4 une icône 15×15 (margin-top 2, opacité 0,85). Valeurs complètes dans « Menu 6.4 · modes » en fin de document.
- **Position de la bulle : +12 px / −4 px partout.** La planche 3 `1AZR-0` (+10 px, haut aligné sur l'élément, x = 5565,001) est une imprécision de la maquette, pas une règle.
- **Étiquette de taille** : Paper affiche « 240x64 » alors que le bouton mesure 520×44 dans les planches 2 à 4 (240×44 en planche 1). Le code affiche la **vraie taille**.

## Autres remarques sur Paper
- **Libellé du mode** : « Ask » dans les bulles ; le brief impose « Demander » (règle 7).
- Les menus 6.4 ont été dessinés pour le panneau (6.4 `14P2-0`) ; aucune planche ne les montre ouverts sur la bulle. Seule règle de placement dans Paper : « Modèle · s'ouvre vers le haut ». R3 : menus au-dessus de la bulle, pouvant en sortir.

## Placement de la bulle par planche

| Planche | Bulle | Bulle L×H | Élément L×H | Bulle − élément (x, y) | Bord droit élément → bulle | Haut bulle − bas élément |
|---|---|---|---|---|---|---|
| 1 · élément visé + Commentaire | `18TO-0` | 305×104 | 240×44 | 252, -4 | 12 | -48 |
| 2 · Prompt envoyé | `1B60-0` | 305×224 | 520×44 | 532, -4 | 12 | -48 |
| 2.1 · Prompt envoyé, Hover details | `1ALE-0` | 305×224 | 520×44 | 532, -4 | 12 | -48 |
| 2.1 · Prompt stoppé | `1AH7-0` | 305×265 | 520×44 | 532, -4 | 12 | -48 |
| 2 · Prompt envoyé (autorisation) | `1AQU-0` | 305×303 | 520×44 | 532, -4 | 12 | -48 |
| 2 · Prompt envoyé (diff ouvert) | `19RN-0` | 305×303 | 520×44 | 532, -4 | 12 | -48 |
| 2.2b · Prompt Erreur | `1ADE-0` | 305×248 | 520×44 | 532, -4 | 12 | -48 |
| 2.2b · Prompt Erreur (détails) | `1A9L-0` | 305×279 | 520×44 | 532, -4 | 12 | -48 |
| 3 · Prompt terminé | `1AZR-0` | 305×279 | 520×44 | 530, 0 | 10 | -44 |
| 3 · Prompt terminé (code ouvert) | `1B19-0` | 305×303 | 520×44 | 532, -4 | 12 | -48 |
# Planches

## 1 · élément visé + Commentaire — `18NS-0` (1180×815)

- Bulle `18TO-0` 305×104 ; élément visé `18QH-0` Bouton Payer (site) 240×44. Bulle − élément : x 252, y -4 ; écart horizontal bord droit élément → bord gauche bulle : 12 px ; haut bulle − bas élément : -48 px ; bulle dans le contenu web : (316, 346).

### Élément visé et marques

- Repère « élément » : `18QH-0` Bouton Payer (site) — 240×44 à (-1196, 14126) monde

| Élément | Node | L×H | x,y / élément | Styles Paper (tokens résolus) |
|---|---|---|---|---|
| Payer — sélectionné | `18QG-0` | 240×68 | 0, -24 | width: 240px; paddingTop: 24px; position: relative |
|   └ Bouton Payer (site) | `18QH-0` | 240×44 | 0, 0 | align: center; justify: center; width: 240px; height: 44px; radius: 8px; shrink: 0; bg: #1C1C1C |
|     └ Payer 48,00 € « Payer 48,00 € » | `18QI-0` | 96×20 | 72, 12 | size: 15px; lh: 20px; font: "Inter Display"; weight: 600; ws: pre-wrap; color: #FFFFFF |
|   └ Cadre de sélection | `18QJ-0` | 248×52 | -4, -4 | position: absolute; left: -4px; top: 20px; width: 248px; height: 52px; bg: #3E67FF14; borderWidth: 2px; borderStyle: solid; borderColor: #3E67FF |
|   └ Étiquette | `18QK-0` | 118×20 | -4, -24 | position: absolute; left: -4px; top: 0px; align: center; height: 20px; paddingInline: 6px; borderTopLeftRadius: 3px; borderTopRightRadius: 3px; borderBottomRightRadius: 0px; borderBottomLeftRadius: 0px; bg: #3E67FF |
|     └ button.btn-primary « button.btn-primary » | `18QL-0` | 106×18 | 2, -23 | size: 13px; lh: 18px; ws: pre; font: "Inter Display"; weight: 400; color: #FFFFFFE6 |
|   └ Curseur viseur | `18QM-0` | 32×32 | 196, 6 | SVG position: absolute; left: 196px; top: 30px |
|   └ Étiquette — taille | `18QQ-0` | 58×20 | 186, -24 | position: absolute; top: 0; align: center; height: 20px; paddingInline: 6px; width: max-content; borderTopLeftRadius: 3px; borderTopRightRadius: 3px; right: -4; bg: #3E67FF |
|     └ 240x64 « 240x64 » | `18QR-0` | 46×18 | 192, -23 | size: 13px; lh: 18px; ws: pre; font: "Inter Display"; weight: 400; color: #FFFFFFE6 |

### Bulle

- Repère « bulle » : `18TO-0` Frame — 305×104 à (-944, 14122) monde
- Repère « élément » : `18QH-0` Bouton Payer (site) — 240×44 à (-1196, 14126) monde

| Élément | Node | L×H | x,y / bulle | x,y / élément | Styles Paper (tokens résolus) |
|---|---|---|---|---|---|
| Frame | `18TO-0` | 305×104 | 0, 0 | 252, -4 | width: 305px; dir: column; padding: 12px; radius: 16px; gap: 12px; position: absolute; left: 316; top: 290; bg: #171717 |
|   └ Frame | `18TP-0` | 281×40 | 12, 12 | 264, 8 | dir: column; align: start; width: fit-content; gap: 2 |
|     └ Fais en sorte que le… « Fais en sorte que le bouton prenne toute la largeur du form » | `18TQ-0` | 281×40 | 12, 12 | 264, 8 | font: "Inter Display"; size: 14px; lh: 20px; alignSelf: stretch; weight: 400; ws: pre-wrap; color: #FFFFFFE6 |
|   └ Pied | `18TR-0` | 281×28 | 12, 64 | 264, 60 | align: center; gap: 4px; justify: space-between |
|     └ Frame | `18TS-0` | 77×24 | 12, 66 | 264, 62 | align: start |
|       └ Ajouter | `18TW-0` | 24×24 | 12, 66 | 264, 62 | width: 24px; height: 24px; align: center; justify: center; radius: 6px; shrink: 0 |
|         └ SVG | `18TX-0` | 14×14 | 17, 71 | 269, 67 | SVG opacity: 50%; shrink: 0 |
|       └ Mode | `18TZ-0` | 53×24 | 36, 66 | 288, 62 | align: center; height: 24px; paddingInline: 6px; radius: 6px; gap: 5px |
|         └ SVG | `18U0-0` | 13×13 | 42, 71.5 | 294, 67.5 | SVG opacity: 0.9; shrink: 0 |
|         └ Ask « Ask » | `18U2-0` | 23×20 | 60, 68 | 312, 64 | font: "Inter Display"; size: 14px; lh: 20px; width: max-content; shrink: 0; weight: 400; ws: pre-wrap; color: color-mix(#FFFFFF 50%, transparent) |
|     └ Frame | `18U3-0` | 152×28 | 141, 64 | 393, 60 | align: center; gap: 4 |
|       └ Modèle | `18U4-0` | 120×24 | 141, 66 | 393, 62 | align: center; height: 24px; paddingInline: 6px; radius: 6px; gap: 4 |
|         └ Frame | `18U5-0` | 76×16 | 147, 70 | 399, 66 | align: center; gap: 4px |
|           └ Logo Claude | `18U6-0` | 14×14 | 147, 71 | 399, 67 | SVG shrink: 0 |
|           └ Sonnet 5.5 « Sonnet 5.5 » | `18U8-0` | 58×16 | 165, 70 | 417, 66 | font: "Inter Display"; size: 12px; lh: 16px; ws: pre; weight: 500; color: #FFFFFFE6 |
|         └ Frame | `18U9-0` | 28×16 | 227, 70 | 479, 66 | align: center; gap: 4px |
|           └ Élevé « Élevé » | `18UA-0` | 28×16 | 227, 70 | 479, 66 | font: "Inter Display"; size: 12px; lh: 16px; ws: pre; weight: 400; color: #FFFFFF80 |
|       └ Envoyer | `18UB-0` | 28×28 | 265, 64 | 517, 60 | width: 28px; height: 28px; align: center; justify: center; radius: 8px; shrink: 0; bg: #3E67FF |
|         └ SVG | `18UC-0` | 14×14 | 272, 71 | 524, 67 | SVG shrink: 0 |

## 2 · Prompt envoyé — `1B30-0` (1180×815)

- Bulle `1B60-0` 305×224 ; élément visé `1B5P-0` Bouton Payer (site) 520×44. Bulle − élément : x 532, y -4 ; écart horizontal bord droit élément → bord gauche bulle : 12 px ; haut bulle − bas élément : -48 px ; bulle dans le contenu web : (596, 346).

### Élément visé et marques

- Repère « élément » : `1B5P-0` Bouton Payer (site) — 520×44 à (64, 14126) monde

| Élément | Node | L×H | x,y / élément | Styles Paper (tokens résolus) |
|---|---|---|---|---|
| Payer — sélectionné | `1B5O-0` | 520×68 | 0, -24 | width: 520px; paddingTop: 24px; position: relative |
|   └ Bouton Payer (site) | `1B5P-0` | 520×44 | 0, 0 | align: center; justify: center; height: 44px; radius: 8px; flex: 1; bg: #1C1C1C |
|     └ Payer 48,00 € « Payer 48,00 € » | `1B5Q-0` | 96×20 | 212, 12 | size: 15px; lh: 20px; font: "Inter Display"; weight: 600; ws: pre-wrap; color: #FFFFFF |
|   └ Cadre de sélection | `1B5R-0` | 528×52 | -4, -4 | position: absolute; left: -4; top: 20; height: 52px; width: 528px; bg: #3E67FF14; borderWidth: 2px; borderStyle: solid; borderColor: #3E67FF |
|   └ Étiquette | `1B5S-0` | 118×20 | -4, -24 | position: absolute; left: -4px; top: 0px; align: center; height: 20px; paddingInline: 6px; borderTopLeftRadius: 3px; borderTopRightRadius: 3px; borderBottomRightRadius: 0px; borderBottomLeftRadius: 0px; bg: #3E67FF |
|     └ button.btn-primary « button.btn-primary » | `1B5T-0` | 106×18 | 2, -23 | size: 13px; lh: 18px; ws: pre; font: "Inter Display"; weight: 400; color: #FFFFFFE6 |
|   └ Étiquette — taille | `1B5U-0` | 58×20 | 466, -24 | position: absolute; top: 0; align: center; height: 20px; paddingInline: 6px; width: max-content; borderTopLeftRadius: 3px; borderTopRightRadius: 3px; right: -4; bg: #3E67FF |
|     └ 240x64 « 240x64 » | `1B5V-0` | 46×18 | 472, -23 | size: 13px; lh: 18px; ws: pre; font: "Inter Display"; weight: 400; color: #FFFFFFE6 |

### Bulle

- Repère « bulle » : `1B60-0` Frame — 305×224 à (596, 14122) monde
- Repère « élément » : `1B5P-0` Bouton Payer (site) — 520×44 à (64, 14126) monde

| Élément | Node | L×H | x,y / bulle | x,y / élément | Styles Paper (tokens résolus) |
|---|---|---|---|---|---|
| Frame | `1B60-0` | 305×224 | 0, 0 | 532, -4 | width: 305px; dir: column; padding: 12px; radius: 16px; gap: 12px; position: absolute; left: 596; top: 346; bg: #171717 |
|   └ Frame | `1B61-0` | 281×52 | 12, 12 | 544, 8 | dir: column; align: end; gap: 2; justify: center; alignSelf: stretch |
|     └ Frame | `1B62-0` | 210×52 | 83, 12 | 615, 8 | dir: column; align: start; paddingInline: 10px; paddingBlock: 6px; borderTopLeftRadius: 12px; borderTopRightRadius: 12px; borderBottomLeftRadius: 12px; borderBottomRightRadius: 4px; bg: #3E67FF |
|       └ Fais en sorte que le… « Fais en sorte que le bouton prenne toute la largeur du form » | `1B63-0` | 190×40 | 93, 18 | 625, 14 | font: "Inter Display"; size: 13px; lh: 20px; width: 190px; weight: 500; ws: pre-wrap; color: #FFFFFFE6 |
|   └ Frame | `1B64-0` | 281×96 | 12, 76 | 544, 72 | dir: column; align: start; width: fit-content; gap: 2 |
|     └ Frame | `1B65-0` | 281×24 | 12, 76 | 544, 72 | align: center; justify: space-between; paddingRight: 2px; alignSelf: stretch |
|       └ Modèle | `1B66-0` | 122×24 | 12, 76 | 544, 72 | align: center; height: 24px; gap: 4; paddingLeft: 6px; paddingRight: 8px; borderTopLeftRadius: 12px; borderTopRightRadius: 12px; borderBottomLeftRadius: 4px; borderBottomRightRadius: 12px; bg: #292929 |
|         └ Frame | `1B67-0` | 76×16 | 18, 80 | 550, 76 | align: center; gap: 4px |
|           └ Logo Claude | `1B68-0` | 14×14 | 18, 81 | 550, 77 | SVG shrink: 0 |
|           └ Sonnet 5.5 « Sonnet 5.5 » | `1B6A-0` | 58×16 | 36, 80 | 568, 76 | font: "Inter Display"; size: 12px; lh: 16px; ws: pre; weight: 500; color: #FFFFFFE6 |
|         └ Frame | `1B6B-0` | 28×16 | 98, 80 | 630, 76 | align: center; gap: 4px |
|           └ Élevé « Élevé » | `1B6C-0` | 28×16 | 98, 80 | 630, 76 | font: "Inter Display"; size: 12px; lh: 16px; ws: pre; weight: 400; color: #FFFFFF80 |
|       └ Frame | `1B6D-0` | 128×22 | 163, 77 | 695, 73 | align: center; gap: 4; justify: end; radius: calc(infinity * 1px); paddingLeft: 4px; paddingRight: 6px; paddingBlock: 2px |
|         └ SVG | `1B6E-0` | 14×14 | 167, 81 | 699, 77 | SVG shrink: 0 |
|         └ Lit checkout.css… « Lit checkout.css… » | `1B6H-0` | 100×18 | 185, 79 | 717, 75 | font: "Inter Display"; size: 13px; lh: 18px; ws: pre; weight: 400; color: #FFFFFFCC |
|     └ Frame | `1B6I-0` | 281×70 | 12, 102 | 544, 98 | dir: column; align: start; borderTopLeftRadius: 4px; borderTopRightRadius: 12px; borderBottomLeftRadius: 12px; borderBottomRightRadius: 16px; paddingTop: 4px; paddingBottom: 6px; alignSelf: stretch; paddingInline: 8px; bg: #292929 |
|       └ Le bouton a une larg… « Le bouton a une largeur fixe de 240 px. Je le passe à droite sur grand écran et en pleine largeur sous 640 px. » | `1B6J-0` | 265×60 | 20, 106 | 552, 102 | font: "Inter Display"; size: 13px; lh: 20px; alignSelf: stretch; weight: 400; ws: pre-wrap; color: #FFFFFFE6 |
|   └ Pied | `1B6K-0` | 281×28 | 12, 184 | 544, 180 | align: center; gap: 4px; justify: space-between |
|     └ Frame | `1B6L-0` | 53×24 | 12, 186 | 544, 182 | align: start |
|       └ Mode | `1B6M-0` | 53×24 | 12, 186 | 544, 182 | align: center; height: 24px; paddingInline: 6px; radius: 6px; gap: 5px |
|         └ SVG | `1B6N-0` | 13×13 | 18, 191.5 | 550, 187.5 | SVG opacity: 0.9; shrink: 0 |
|         └ Ask « Ask » | `1B6P-0` | 23×20 | 36, 188 | 568, 184 | font: "Inter Display"; size: 14px; lh: 20px; width: max-content; shrink: 0; weight: 400; ws: pre-wrap; color: color-mix(#FFFFFF 50%, transparent) |
|     └ Frame | `1B6Q-0` | 28×28 | 265, 184 | 797, 180 | align: center; gap: 4 |
|       └ Envoyer | `1B6R-0` | 28×28 | 265, 184 | 797, 180 | width: 28px; height: 28px; align: center; justify: center; radius: 8px; shrink: 0; bg: color-mix(#FF7B6E 10%, transparent) |
|         └ SVG | `1B6S-0` | 14×14 | 272, 191 | 804, 187 | SVG shrink: 0 |

## 2.1 · Prompt envoyé, Hover details — `1AIE-0` (1180×815)

- Bulle `1ALE-0` 305×224 ; élément visé `1AL3-0` Bouton Payer (site) 520×44. Bulle − élément : x 532, y -4 ; écart horizontal bord droit élément → bord gauche bulle : 12 px ; haut bulle − bas élément : -48 px ; bulle dans le contenu web : (596, 346).

### Élément visé et marques

- Repère « élément » : `1AL3-0` Bouton Payer (site) — 520×44 à (64, 15014) monde

| Élément | Node | L×H | x,y / élément | Styles Paper (tokens résolus) |
|---|---|---|---|---|
| Payer — sélectionné | `1AL2-0` | 520×68 | 0, -24 | width: 520px; paddingTop: 24px; position: relative |
|   └ Bouton Payer (site) | `1AL3-0` | 520×44 | 0, 0 | align: center; justify: center; height: 44px; radius: 8px; flex: 1; bg: #1C1C1C |
|     └ Payer 48,00 € « Payer 48,00 € » | `1AL4-0` | 96×20 | 212, 12 | size: 15px; lh: 20px; font: "Inter Display"; weight: 600; ws: pre-wrap; color: #FFFFFF |
|   └ Cadre de sélection | `1AL5-0` | 528×52 | -4, -4 | position: absolute; left: -4; top: 20; height: 52px; width: 528px; bg: #3E67FF14; borderWidth: 2px; borderStyle: solid; borderColor: #3E67FF |
|   └ Étiquette | `1AL6-0` | 118×20 | -4, -24 | position: absolute; left: -4px; top: 0px; align: center; height: 20px; paddingInline: 6px; borderTopLeftRadius: 3px; borderTopRightRadius: 3px; borderBottomRightRadius: 0px; borderBottomLeftRadius: 0px; bg: #3E67FF |
|     └ button.btn-primary « button.btn-primary » | `1AL7-0` | 106×18 | 2, -23 | size: 13px; lh: 18px; ws: pre; font: "Inter Display"; weight: 400; color: #FFFFFFE6 |
|   └ Étiquette — taille | `1AL8-0` | 58×20 | 466, -24 | position: absolute; top: 0; align: center; height: 20px; paddingInline: 6px; width: max-content; borderTopLeftRadius: 3px; borderTopRightRadius: 3px; right: -4; bg: #3E67FF |
|     └ 240x64 « 240x64 » | `1AL9-0` | 46×18 | 472, -23 | size: 13px; lh: 18px; ws: pre; font: "Inter Display"; weight: 400; color: #FFFFFFE6 |

### Bulle

- Repère « bulle » : `1ALE-0` Frame — 305×224 à (596, 15010) monde
- Repère « élément » : `1AL3-0` Bouton Payer (site) — 520×44 à (64, 15014) monde

| Élément | Node | L×H | x,y / bulle | x,y / élément | Styles Paper (tokens résolus) |
|---|---|---|---|---|---|
| Frame | `1ALE-0` | 305×224 | 0, 0 | 532, -4 | width: 305px; dir: column; padding: 12px; radius: 16px; gap: 12px; position: absolute; left: 596; top: 346; bg: #171717 |
|   └ Frame | `1ALF-0` | 281×52 | 12, 12 | 544, 8 | dir: column; align: end; gap: 2; justify: center; alignSelf: stretch |
|     └ Frame | `1ALG-0` | 210×52 | 83, 12 | 615, 8 | dir: column; align: start; paddingInline: 10px; paddingBlock: 6px; borderTopLeftRadius: 12px; borderTopRightRadius: 12px; borderBottomLeftRadius: 12px; borderBottomRightRadius: 4px; bg: #3E67FF |
|       └ Fais en sorte que le… « Fais en sorte que le bouton prenne toute la largeur du form » | `1ALH-0` | 190×40 | 93, 18 | 625, 14 | font: "Inter Display"; size: 13px; lh: 20px; width: 190px; weight: 500; ws: pre-wrap; color: #FFFFFFE6 |
|   └ Frame | `1ALI-0` | 281×96 | 12, 76 | 544, 72 | dir: column; align: start; width: fit-content; gap: 2 |
|     └ Frame | `1ALJ-0` | 281×24 | 12, 76 | 544, 72 | align: center; justify: space-between; alignSelf: stretch |
|       └ Modèle | `1ALK-0` | 122×24 | 12, 76 | 544, 72 | align: center; height: 24px; gap: 4; paddingLeft: 6px; paddingRight: 8px; borderTopLeftRadius: 12px; borderTopRightRadius: 12px; borderBottomLeftRadius: 4px; borderBottomRightRadius: 12px; bg: #292929 |
|         └ Frame | `1ALL-0` | 76×16 | 18, 80 | 550, 76 | align: center; gap: 4px |
|           └ Logo Claude | `1ALM-0` | 14×14 | 18, 81 | 550, 77 | SVG shrink: 0 |
|           └ Sonnet 5.5 « Sonnet 5.5 » | `1ALO-0` | 58×16 | 36, 80 | 568, 76 | font: "Inter Display"; size: 12px; lh: 16px; ws: pre; weight: 500; color: #FFFFFFE6 |
|         └ Frame | `1ALP-0` | 28×16 | 98, 80 | 630, 76 | align: center; gap: 4px |
|           └ Élevé « Élevé » | `1ALQ-0` | 28×16 | 98, 80 | 630, 76 | font: "Inter Display"; size: 12px; lh: 16px; ws: pre; weight: 400; color: #FFFFFF80 |
|       └ Frame | `1B6U-0` | 128×22 | 165, 77 | 697, 73 | align: center; gap: 4; justify: end; paddingLeft: 4px; paddingRight: 6px; width: 128px; borderTopLeftRadius: 4px; borderTopRightRadius: 4px; borderBottomLeftRadius: 10px; borderBottomRightRadius: 10px; shrink: 0; paddingBlock: 2px; bg: color-mix(#FFFFFF 10%, transparent) |
|         └ SVG | `1B6V-0` | 14×14 | 169, 81 | 701, 77 | SVG shrink: 0 |
|         └ Lit checkout.css… « Lit checkout.css… » | `1B6Y-0` | 100×18 | 187, 79 | 719, 75 | font: "Inter Display"; size: 13px; lh: 18px; ws: pre; weight: 400; color: #FFFFFFCC |
|     └ Frame | `1ALW-0` | 281×70 | 12, 102 | 544, 98 | dir: column; align: start; borderTopLeftRadius: 4px; borderTopRightRadius: 12px; borderBottomLeftRadius: 12px; borderBottomRightRadius: 16px; paddingTop: 4px; paddingBottom: 6px; alignSelf: stretch; paddingInline: 8px; bg: #292929 |
|       └ Le bouton a une larg… « Le bouton a une largeur fixe de 240 px. Je le passe à droite sur grand écran et en pleine largeur sous 640 px. » | `1ALX-0` | 265×60 | 20, 106 | 552, 102 | font: "Inter Display"; size: 13px; lh: 20px; alignSelf: stretch; weight: 400; ws: pre-wrap; color: #FFFFFFE6 |
|   └ Pied | `1ALY-0` | 281×28 | 12, 184 | 544, 180 | align: center; gap: 4px; justify: space-between |
|     └ Frame | `1ALZ-0` | 53×24 | 12, 186 | 544, 182 | align: start |
|       └ Mode | `1AM0-0` | 53×24 | 12, 186 | 544, 182 | align: center; height: 24px; paddingInline: 6px; radius: 6px; gap: 5px |
|         └ SVG | `1AM1-0` | 13×13 | 18, 191.5 | 550, 187.5 | SVG opacity: 0.9; shrink: 0 |
|         └ Ask « Ask » | `1AM3-0` | 23×20 | 36, 188 | 568, 184 | font: "Inter Display"; size: 14px; lh: 20px; width: max-content; shrink: 0; weight: 400; ws: pre-wrap; color: color-mix(#FFFFFF 50%, transparent) |
|     └ Frame | `1AM4-0` | 28×28 | 265, 184 | 797, 180 | align: center; gap: 4 |
|       └ Envoyer | `1AM5-0` | 28×28 | 265, 184 | 797, 180 | width: 28px; height: 28px; align: center; justify: center; radius: 8px; shrink: 0; bg: color-mix(#FF7B6E 10%, transparent) |
|         └ SVG | `1AM6-0` | 14×14 | 272, 191 | 804, 187 | SVG shrink: 0 |
|   └ Curseur viseur | `1ALA-0` | 32×32 | 267, 76 | 799, 72 | SVG position: absolute; left: 267; top: 76 |

### Calque hors contenu : Étapes

- Repère « bulle » : `1ALE-0` Frame — 305×224 à (596, 15010) monde
- Repère « élément » : `1AL3-0` Bouton Payer (site) — 520×44 à (64, 15014) monde

| Élément | Node | L×H | x,y / bulle | x,y / élément | Styles Paper (tokens résolus) |
|---|---|---|---|---|---|
| Étapes | `1B7H-0` | 178×82 | 165, -7 | 697, -11 | dir: column; gap: 6px; width: 178px; borderTopLeftRadius: 10px; borderTopRightRadius: 10px; borderBottomLeftRadius: 4px; borderBottomRightRadius: 10px; paddingLeft: 10px; paddingRight: 8px; position: absolute; left: 761; bottom: 277; paddingBlock: 8px; bg: #2F2F2F |
|   └ Frame | `1B7I-0` | 160×18 | 175, 1 | 707, -3 | align: center; gap: 8px |
|     └ SVG | `1B7J-0` | 12×12 | 175, 4 | 707, 0 | SVG opacity: 0.5; shrink: 0 |
|     └ A lu index.html « A lu index.html » | `1B7L-0` | 83×18 | 195, 1 | 727, -3 | font: "Inter Display"; size: 13px; lh: 18px; ws: pre; weight: 400; color: #FFFFFFCC |
|   └ Frame | `1B7M-0` | 160×18 | 175, 25 | 707, 21 | align: center; gap: 8px |
|     └ SVG | `1B7N-0` | 12×12 | 175, 28 | 707, 24 | SVG opacity: 0.5; shrink: 0 |
|     └ A lu Checkout.tsx « A lu Checkout.tsx » | `1B7P-0` | 100×18 | 195, 25 | 727, 21 | font: "Inter Display"; size: 13px; lh: 18px; ws: pre; weight: 400; color: #FFFFFFCC |
|   └ Frame | `1B7Q-0` | 160×18 | 175, 49 | 707, 45 | align: center; gap: 8px |
|     └ SVG | `1B7U-0` | 14×14 | 175, 51 | 707, 47 | SVG opacity: 50%; shrink: 0 |
|     └ A utilisé 1 outil MC… « A utilisé 1 outil MCP » | `1B7R-0` | 111×18 | 197, 49 | 729, 45 | font: "Inter Display"; size: 13px; lh: 18px; ws: pre; weight: 400; color: #FFFFFFCC |

## 2.1 · Prompt stoppé — `1AE7-0` (1180×815)

- Bulle `1AH7-0` 305×265 ; élément visé `1AGW-0` Bouton Payer (site) 520×44. Bulle − élément : x 532, y -4 ; écart horizontal bord droit élément → bord gauche bulle : 12 px ; haut bulle − bas élément : -48 px ; bulle dans le contenu web : (596, 346).

### Élément visé et marques

- Repère « élément » : `1AGW-0` Bouton Payer (site) — 520×44 à (1324, 14126) monde

| Élément | Node | L×H | x,y / élément | Styles Paper (tokens résolus) |
|---|---|---|---|---|
| Payer — sélectionné | `1AGV-0` | 520×68 | 0, -24 | width: 520px; paddingTop: 24px; position: relative |
|   └ Bouton Payer (site) | `1AGW-0` | 520×44 | 0, 0 | align: center; justify: center; height: 44px; radius: 8px; flex: 1; bg: #1C1C1C |
|     └ Payer 48,00 € « Payer 48,00 € » | `1AGX-0` | 96×20 | 212, 12 | size: 15px; lh: 20px; font: "Inter Display"; weight: 600; ws: pre-wrap; color: #FFFFFF |
|   └ Cadre de sélection | `1AGY-0` | 528×52 | -4, -4 | position: absolute; left: -4; top: 20; height: 52px; width: 528px; bg: #3E67FF14; borderWidth: 2px; borderStyle: solid; borderColor: #3E67FF |
|   └ Étiquette | `1AGZ-0` | 118×20 | -4, -24 | position: absolute; left: -4px; top: 0px; align: center; height: 20px; paddingInline: 6px; borderTopLeftRadius: 3px; borderTopRightRadius: 3px; borderBottomRightRadius: 0px; borderBottomLeftRadius: 0px; bg: #3E67FF |
|     └ button.btn-primary « button.btn-primary » | `1AH0-0` | 106×18 | 2, -23 | size: 13px; lh: 18px; ws: pre; font: "Inter Display"; weight: 400; color: #FFFFFFE6 |
|   └ Étiquette — taille | `1AH1-0` | 58×20 | 466, -24 | position: absolute; top: 0; align: center; height: 20px; paddingInline: 6px; width: max-content; borderTopLeftRadius: 3px; borderTopRightRadius: 3px; right: -4; bg: #3E67FF |
|     └ 240x64 « 240x64 » | `1AH2-0` | 46×18 | 472, -23 | size: 13px; lh: 18px; ws: pre; font: "Inter Display"; weight: 400; color: #FFFFFFE6 |

### Bulle

- Repère « bulle » : `1AH7-0` Frame — 305×265 à (1856, 14122) monde
- Repère « élément » : `1AGW-0` Bouton Payer (site) — 520×44 à (1324, 14126) monde

| Élément | Node | L×H | x,y / bulle | x,y / élément | Styles Paper (tokens résolus) |
|---|---|---|---|---|---|
| Frame | `1AH7-0` | 305×265 | 0, 0 | 532, -4 | width: 305px; dir: column; padding: 12px; radius: 16px; gap: 12px; position: absolute; left: 596; top: 346; bg: #171717 |
|   └ Frame | `1AH8-0` | 281×52 | 12, 12 | 544, 8 | dir: column; align: end; gap: 2; justify: center; alignSelf: stretch |
|     └ Frame | `1AH9-0` | 220×52 | 73, 12 | 605, 8 | dir: column; align: start; paddingInline: 10px; paddingBlock: 6px; borderTopLeftRadius: 12px; borderTopRightRadius: 12px; borderBottomLeftRadius: 12px; borderBottomRightRadius: 4px; bg: #3E67FF |
|       └ Fais en sorte que le… « Fais en sorte que le bouton prenne toute la largeur du form » | `1AHA-0` | 200×40 | 83, 18 | 615, 14 | font: "Inter Display"; size: 13px; lh: 20px; width: 200px; weight: 500; ws: pre-wrap; color: #FFFFFFE6 |
|   └ Frame | `1AHB-0` | 281×96 | 12, 76 | 544, 72 | dir: column; align: start; width: fit-content; gap: 2 |
|     └ Frame | `1AHC-0` | 281×24 | 12, 76 | 544, 72 | align: center; justify: space-between; alignSelf: stretch |
|       └ Modèle | `1AHD-0` | 122×24 | 12, 76 | 544, 72 | align: center; height: 24px; gap: 4; paddingLeft: 6px; paddingRight: 8px; borderTopLeftRadius: 12px; borderTopRightRadius: 12px; borderBottomLeftRadius: 4px; borderBottomRightRadius: 12px; bg: #FFFFFF14 |
|         └ Frame | `1AHE-0` | 76×16 | 18, 80 | 550, 76 | align: center; gap: 4px |
|           └ Logo Claude | `1AHF-0` | 14×14 | 18, 81 | 550, 77 | SVG shrink: 0 |
|           └ Sonnet 5.5 « Sonnet 5.5 » | `1AHH-0` | 58×16 | 36, 80 | 568, 76 | font: "Inter Display"; size: 12px; lh: 16px; ws: pre; weight: 500; color: #FFFFFFE6 |
|         └ Frame | `1AHI-0` | 28×16 | 98, 80 | 630, 76 | align: center; gap: 4px |
|           └ Élevé « Élevé » | `1AHJ-0` | 28×16 | 98, 80 | 630, 76 | font: "Inter Display"; size: 12px; lh: 16px; ws: pre; weight: 400; color: #FFFFFF80 |
|       └ Frame | `1AHK-0` | 159×18 | 134, 79 | 666, 75 | align: center; gap: 4; flex: 1; justify: end |
|         └ SVG | `1AHL-0` | 14×14 | 210, 81 | 742, 77 | SVG opacity: 50%; shrink: 0 |
|         └ S'est arrêté « S'est arrêté » | `1AHN-0` | 65×18 | 228, 79 | 760, 75 | font: "Inter Display"; size: 13px; lh: 18px; ws: pre; weight: 400; color: color-mix(#FFFFFF 50%, transparent) |
|     └ Frame | `1AHO-0` | 281×70 | 12, 102 | 544, 98 | dir: column; align: start; borderTopLeftRadius: 4px; borderTopRightRadius: 12px; borderBottomLeftRadius: 12px; borderBottomRightRadius: 16px; paddingTop: 4px; paddingBottom: 6px; alignSelf: stretch; paddingInline: 8px; bg: #FFFFFF14 |
|       └ Le bouton a une larg… « Le bouton a une largeur fixe de 240 px. Je le passe à droite sur grand écran et en pleine largeur sous 640 px. » | `1AHP-0` | 265×60 | 20, 106 | 552, 102 | font: "Inter Display"; size: 13px; lh: 20px; alignSelf: stretch; weight: 400; ws: pre-wrap; color: #FFFFFFE6 |
|   └ Rectangle | `1AHQ-0` | 281×1 | 12, 184 | 544, 180 | height: 1px; radius: calc(infinity * 1px); alignSelf: stretch; shrink: 0; bg: color-mix(#DDDDDD 10%, transparent) |
|   └ Frame | `1AHR-0` | 281×56 | 12, 197 | 544, 193 | dir: column; align: start; gap: 8; alignSelf: stretch |
|     └ Frame | `1AHS-0` | 281×20 | 12, 197 | 544, 193 | dir: column; align: start; paddingInline: 2px; alignSelf: stretch |
|       └ Que voulez-vous ajou… « Que voulez-vous ajouter ? » | `1AHT-0` | 277×20 | 14, 197 | 546, 193 | font: "Inter Display"; size: 14px; lh: 20px; height: 20px; alignSelf: stretch; shrink: 0; weight: 400; ws: pre-wrap; color: color-mix(#FFFFFF 50%, transparent) |
|     └ Pied | `1AHU-0` | 281×28 | 12, 225 | 544, 221 | align: center; gap: 4px; justify: space-between; alignSelf: stretch |
|       └ Frame | `1AHV-0` | 77×24 | 12, 227 | 544, 223 | align: start |
|         └ Ajouter | `1AHW-0` | 24×24 | 12, 227 | 544, 223 | width: 24px; height: 24px; align: center; justify: center; radius: 6px; shrink: 0 |
|           └ SVG | `1AHX-0` | 14×14 | 17, 232 | 549, 228 | SVG opacity: 50%; shrink: 0 |
|         └ Mode | `1AHZ-0` | 53×24 | 36, 227 | 568, 223 | align: center; height: 24px; paddingInline: 6px; radius: 6px; gap: 5px |
|           └ SVG | `1AI0-0` | 13×13 | 42, 232.5 | 574, 228.5 | SVG opacity: 0.9; shrink: 0 |
|           └ Ask « Ask » | `1AI2-0` | 23×20 | 60, 229 | 592, 225 | font: "Inter Display"; size: 14px; lh: 20px; width: max-content; shrink: 0; weight: 400; ws: pre-wrap; color: color-mix(#FFFFFF 50%, transparent) |
|       └ Frame | `1AI3-0` | 152×28 | 141, 225 | 673, 221 | align: center; gap: 4 |
|         └ Modèle | `1AI4-0` | 120×24 | 141, 227 | 673, 223 | align: center; height: 24px; paddingInline: 6px; radius: 6px; gap: 4 |
|           └ Frame | `1AI5-0` | 76×16 | 147, 231 | 679, 227 | align: center; gap: 4px |
|             └ Logo Claude | `1AI6-0` | 14×14 | 147, 232 | 679, 228 | SVG shrink: 0 |
|             └ Sonnet 5.5 « Sonnet 5.5 » | `1AI8-0` | 58×16 | 165, 231 | 697, 227 | font: "Inter Display"; size: 12px; lh: 16px; ws: pre; weight: 500; color: #FFFFFFE6 |
|           └ Frame | `1AI9-0` | 28×16 | 227, 231 | 759, 227 | align: center; gap: 4px |
|             └ Élevé « Élevé » | `1AIA-0` | 28×16 | 227, 231 | 759, 227 | font: "Inter Display"; size: 12px; lh: 16px; ws: pre; weight: 400; color: #FFFFFF80 |
|         └ Envoyer | `1AIB-0` | 28×28 | 265, 225 | 797, 221 | width: 28px; height: 28px; align: center; justify: center; radius: 8px; shrink: 0; bg: #3E67FF |
|           └ SVG | `1AIC-0` | 14×14 | 272, 232 | 804, 228 | SVG shrink: 0 |

## 2 · Prompt envoyé (autorisation) — `1ANU-0` (1180×815)

- **Planche redessinée le 24 nov. 2026 (Designer)** : la carte d'autorisation est une vraie carte, nœud `26HS-0` (#292929, bordure 1 px #FFFFFF14, rayon 12, marges et écarts de 10 px ; ligne du fichier 13 / 18 sans fond, champ #1F1F1F rayon 8 haut de 30, « Auto. » dessiné éteint #FFFFFF26, « Refuser » #FFFFFF0F texte #FFFFFFCC, boutons à 10 px de marge), la même que « App réelle · Mode Commentaire 4 » (`1V51-0`). Elle fait 118 px de haut au lieu de 104 : tout ce qui est au-dessus est 14 px plus haut. Le tableau ci-dessous est le relevé d'origine (carte `1ARF-0`, qui n'existe plus) ; les valeurs en vigueur sont dans `tests/chat-panel/fidelity.json` (`c-2-autorisation`, clé `paper`). La planche du diff en grand (`19ON-0`, carte `1AMG-0`) n'a pas été redessinée.
- Bulle `1AQU-0` 305×303 ; élément visé `1AQJ-0` Bouton Payer (site) 520×44. Bulle − élément : x 532, y -4 ; écart horizontal bord droit élément → bord gauche bulle : 12 px ; haut bulle − bas élément : -48 px ; bulle dans le contenu web : (596, 346).

### Élément visé et marques

- Repère « élément » : `1AQJ-0` Bouton Payer (site) — 520×44 à (2561, 14126) monde

| Élément | Node | L×H | x,y / élément | Styles Paper (tokens résolus) |
|---|---|---|---|---|
| Payer — sélectionné | `1AQI-0` | 520×68 | 0, -24 | width: 520px; paddingTop: 24px; position: relative |
|   └ Bouton Payer (site) | `1AQJ-0` | 520×44 | 0, 0 | align: center; justify: center; height: 44px; radius: 8px; flex: 1; bg: #1C1C1C |
|     └ Payer 48,00 € « Payer 48,00 € » | `1AQK-0` | 96×20 | 212, 12 | size: 15px; lh: 20px; font: "Inter Display"; weight: 600; ws: pre-wrap; color: #FFFFFF |
|   └ Cadre de sélection | `1AQL-0` | 528×52 | -4, -4 | position: absolute; left: -4; top: 20; height: 52px; width: 528px; bg: #3E67FF14; borderWidth: 2px; borderStyle: solid; borderColor: #3E67FF |
|   └ Étiquette | `1AQM-0` | 118×20 | -4, -24 | position: absolute; left: -4px; top: 0px; align: center; height: 20px; paddingInline: 6px; borderTopLeftRadius: 3px; borderTopRightRadius: 3px; borderBottomRightRadius: 0px; borderBottomLeftRadius: 0px; bg: #3E67FF |
|     └ button.btn-primary « button.btn-primary » | `1AQN-0` | 106×18 | 2, -23 | size: 13px; lh: 18px; ws: pre; font: "Inter Display"; weight: 400; color: #FFFFFFE6 |
|   └ Étiquette — taille | `1AQO-0` | 58×20 | 466, -24 | position: absolute; top: 0; align: center; height: 20px; paddingInline: 6px; width: max-content; borderTopLeftRadius: 3px; borderTopRightRadius: 3px; right: -4; bg: #3E67FF |
|     └ 240x64 « 240x64 » | `1AQP-0` | 46×18 | 472, -23 | size: 13px; lh: 18px; ws: pre; font: "Inter Display"; weight: 400; color: #FFFFFFE6 |

### Bulle

- Repère « bulle » : `1AQU-0` Frame — 305×303 à (3093, 14122) monde
- Repère « élément » : `1AQJ-0` Bouton Payer (site) — 520×44 à (2561, 14126) monde

| Élément | Node | L×H | x,y / bulle | x,y / élément | Styles Paper (tokens résolus) |
|---|---|---|---|---|---|
| Frame | `1AQU-0` | 305×303 | 0, 0 | 532, -4 | width: 305px; dir: column; padding: 12px; radius: 16px; gap: 12px; position: absolute; left: 596; top: 346; height: 303px; align: center; justify: end; overflow: clip; bg: #171717 |
|   └ Frame | `1AQV-0` | 281×52 | 12, 2 | 544, -2 | dir: column; align: end; gap: 2; justify: center; alignSelf: stretch |
|     └ Frame | `1AQW-0` | 210×52 | 83, 2 | 615, -2 | dir: column; align: start; paddingInline: 10px; paddingBlock: 6px; borderTopLeftRadius: 12px; borderTopRightRadius: 12px; borderBottomLeftRadius: 12px; borderBottomRightRadius: 4px; bg: #3E67FF |
|       └ Fais en sorte que le… « Fais en sorte que le bouton prenne toute la largeur du form » | `1AQX-0` | 190×40 | 93, 8 | 625, 4 | font: "Inter Display"; size: 13px; lh: 20px; width: 190px; weight: 500; ws: pre-wrap; color: #FFFFFFE6 |
|   └ Frame | `1AQY-0` | 281×96 | 12, 66 | 544, 62 | dir: column; align: start; gap: 2 |
|     └ Frame | `1AQZ-0` | 281×24 | 12, 66 | 544, 62 | align: center; justify: space-between; alignSelf: stretch |
|       └ Modèle | `1AR0-0` | 122×24 | 12, 66 | 544, 62 | align: center; height: 24px; gap: 4; paddingLeft: 6px; paddingRight: 8px; borderTopLeftRadius: 12px; borderTopRightRadius: 12px; borderBottomLeftRadius: 4px; borderBottomRightRadius: 12px; bg: #FFFFFF14 |
|         └ Frame | `1AR1-0` | 76×16 | 18, 70 | 550, 66 | align: center; gap: 4px |
|           └ Logo Claude | `1AR2-0` | 14×14 | 18, 71 | 550, 67 | SVG shrink: 0 |
|           └ Sonnet 5.5 « Sonnet 5.5 » | `1AR4-0` | 58×16 | 36, 70 | 568, 66 | font: "Inter Display"; size: 12px; lh: 16px; ws: pre; weight: 500; color: #FFFFFFE6 |
|         └ Frame | `1AR5-0` | 28×16 | 98, 70 | 630, 66 | align: center; gap: 4px |
|           └ Élevé « Élevé » | `1AR6-0` | 28×16 | 98, 70 | 630, 66 | font: "Inter Display"; size: 12px; lh: 16px; ws: pre; weight: 400; color: #FFFFFF80 |
|       └ Frame | `1AR7-0` | 159×18 | 134, 69 | 666, 65 | align: center; gap: 8px; flex: 1; justify: end |
|         └ SVG | `1AR8-0` | 12×12 | 173, 72 | 705, 68 | SVG shrink: 0 |
|         └ Lit checkout.css… « Lit checkout.css… » | `1ARB-0` | 100×18 | 193, 69 | 725, 65 | font: "Inter Display"; size: 13px; lh: 18px; ws: pre; weight: 400; color: #FFFFFFCC |
|     └ Frame | `1ARC-0` | 281×70 | 12, 92 | 544, 88 | dir: column; align: start; borderTopLeftRadius: 4px; borderTopRightRadius: 12px; borderBottomLeftRadius: 12px; borderBottomRightRadius: 16px; paddingTop: 4px; paddingBottom: 6px; alignSelf: stretch; paddingInline: 8px; bg: #FFFFFF14 |
|       └ Le bouton a une larg… « Le bouton a une largeur fixe de 240 px. Je le passe à droite sur grand écran et en pleine largeur sous 640 px. » | `1ARD-0` | 265×60 | 20, 96 | 552, 92 | font: "Inter Display"; size: 13px; lh: 20px; alignSelf: stretch; weight: 400; ws: pre-wrap; color: #FFFFFFE6 |
|   └ Rectangle | `1ARE-0` | 281×1 | 12, 174 | 544, 170 | height: 1px; radius: calc(infinity * 1px); alignSelf: stretch; shrink: 0; bg: color-mix(#DDDDDD 10%, transparent) |
|   └ Frame | `1ARF-0` | 281×104 | 12, 187 | 544, 183 | dir: column; radius: 16px; gap: 6px; alignSelf: stretch; bg: #1F1F1F |
|     └ Frame | `1ARG-0` | 281×70 | 12, 187 | 544, 183 | dir: column; align: start; gap: 6px; alignSelf: stretch |
|       └ Barre des changements | `1ARH-0` | 281×32 | 12, 187 | 544, 183 | align: center; height: 32px; paddingRight: 8px; paddingLeft: 8px; gap: 8px; justify: space-between; alignSelf: stretch; shrink: 0; bg: #FFFFFF14; radius: 10px |
|         └ Claude veut modifier… « Claude veut modifier checkout.css » | `1ARI-0` | 200×16 | 20, 195 | 552, 191 | font: "Inter Display"; size: 13px; lh: 16px; ws: pre; textOverflow: ellipsis; flex: 1; weight: 500; WebkitBoxOrient: vertical; WebkitLineClamp: 1; overflow: hidden; color: #FFFFFFE6 |
|         └ +6 « +6 » | `1ARJ-0` | 15×16 | 228, 195 | 760, 191 | size: 12px; lh: 16px; ws: pre; width: max-content; shrink: 0; font: "JetBrains Mono"; weight: 400; color: #7FD39A |
|         └ −2 « −2 » | `1ARK-0` | 15×16 | 251, 195 | 783, 191 | size: 12px; lh: 16px; ws: pre; width: max-content; shrink: 0; font: "JetBrains Mono"; weight: 400; color: #FF8A80 |
|         └ SVG | `1ARL-0` | 11×11 | 274, 197.5 | 806, 193.5 | SVG opacity: 0.45; shrink: 0; rotate: 270deg; transformOrigin: 50% 50% |
|       └ Rediriger | `1ARN-0` | 281×32 | 12, 225 | 544, 221 | align: center; height: 32px; paddingLeft: 8px; paddingRight: 8px; gap: 6; alignSelf: stretch; shrink: 0; bg: #292929; radius: 10px |
|         └ SVG | `1ARO-0` | 14×14 | 20, 234 | 552, 230 | SVG opacity: 50%; shrink: 0 |
|         └ Dire à Claude quoi f… « Dire à Claude quoi faire à la place… » | `1ARQ-0` | 197×16 | 40, 233 | 572, 229 | font: "Inter Display"; size: 13px; lh: 16px; weight: 400; ws: pre-wrap; color: color-mix(#FFFFFF 50%, transparent) |
|     └ Actions | `1ARR-0` | 281×28 | 12, 263 | 544, 259 | align: center; gap: 8px; paddingLeft: 4px |
|       └ Toujours autoriser | `1ARS-0` | 131×16 | 16, 269 | 548, 265 | align: center; gap: 6px; flexBasis: 0%; flexGrow: 1 |
|         └ Interrupteur — activé | `1ART-0` | 28×16 | 16, 269 | 548, 265 | align: center; justify: end; width: 28px; height: 16px; shrink: 0; padding: 2px; radius: 8px; bg: #3E67FF |
|           └ Rectangle | `1ARU-0` | 12×12 | 30, 271 | 562, 267 | width: 12px; height: 12px; radius: 6px; shrink: 0; bg: #FFFFFF |
|         └ Auto. « Auto. » | `1ARV-0` | 28×16 | 50, 269 | 582, 265 | font: "Inter Display"; size: 12px; lh: 16px; ws: pre; weight: 400; color: #FFFFFFB3 |
|       └ Frame | `1ARW-0` | 61×28 | 155, 263 | 687, 259 | align: center; height: 28px; paddingRight: 8px; paddingLeft: 8px; radius: 6px; gap: 6px; bg: #292929 |
|         └ Refuser « Refuser » | `1ARX-0` | 45×16 | 163, 269 | 695, 265 | font: "Inter Display"; size: 13px; lh: 16px; weight: 500; ws: pre-wrap; color: #FFFFFF |
|       └ Frame | `1ARY-0` | 69×28 | 224, 263 | 756, 259 | align: center; height: 28px; paddingRight: 8px; paddingLeft: 8px; radius: 6px; gap: 6px; bg: #3E67FF |
|         └ Autoriser « Autoriser » | `1ARZ-0` | 53×16 | 232, 269 | 764, 265 | font: "Inter Display"; size: 13px; lh: 16px; weight: 500; ws: pre-wrap; color: #FFFFFF |
|   └ Masque haut | `1AS0-0` | 305×96 | 0, 0 | 532, -4 | height: 96px; position: absolute; left: 0; top: 0; right: 0; backgroundImage: linear-gradient(in oklab 180.49deg, #292929 10.75%, color-mix(#292929 0%, transparent) 98.04%) |

## 2 · Prompt envoyé (diff ouvert) — `19ON-0` (1180×815)

- Bulle `19RN-0` 305×303 ; élément visé `19RC-0` Bouton Payer (site) 520×44. Bulle − élément : x 532, y -4 ; écart horizontal bord droit élément → bord gauche bulle : 12 px ; haut bulle − bas élément : -48 px ; bulle dans le contenu web : (596, 346).

### Élément visé et marques

- Repère « élément » : `19RC-0` Bouton Payer (site) — 520×44 à (2561, 15014) monde

| Élément | Node | L×H | x,y / élément | Styles Paper (tokens résolus) |
|---|---|---|---|---|
| Payer — sélectionné | `19RB-0` | 520×68 | 0, -24 | width: 520px; paddingTop: 24px; position: relative |
|   └ Bouton Payer (site) | `19RC-0` | 520×44 | 0, 0 | align: center; justify: center; height: 44px; radius: 8px; flex: 1; bg: #1C1C1C |
|     └ Payer 48,00 € « Payer 48,00 € » | `19RD-0` | 96×20 | 212, 12 | size: 15px; lh: 20px; font: "Inter Display"; weight: 600; ws: pre-wrap; color: #FFFFFF |
|   └ Cadre de sélection | `19RE-0` | 528×52 | -4, -4 | position: absolute; left: -4; top: 20; height: 52px; width: 528px; bg: #3E67FF14; borderWidth: 2px; borderStyle: solid; borderColor: #3E67FF |
|   └ Étiquette | `19RF-0` | 118×20 | -4, -24 | position: absolute; left: -4px; top: 0px; align: center; height: 20px; paddingInline: 6px; borderTopLeftRadius: 3px; borderTopRightRadius: 3px; borderBottomRightRadius: 0px; borderBottomLeftRadius: 0px; bg: #3E67FF |
|     └ button.btn-primary « button.btn-primary » | `19RG-0` | 106×18 | 2, -23 | size: 13px; lh: 18px; ws: pre; font: "Inter Display"; weight: 400; color: #FFFFFFE6 |
|   └ Étiquette — taille | `19RH-0` | 58×20 | 466, -24 | position: absolute; top: 0; align: center; height: 20px; paddingInline: 6px; width: max-content; borderTopLeftRadius: 3px; borderTopRightRadius: 3px; right: -4; bg: #3E67FF |
|     └ 240x64 « 240x64 » | `19RI-0` | 46×18 | 472, -23 | size: 13px; lh: 18px; ws: pre; font: "Inter Display"; weight: 400; color: #FFFFFFE6 |

### Bulle

- Repère « bulle » : `19RN-0` Frame — 305×303 à (3093, 15010) monde
- Repère « élément » : `19RC-0` Bouton Payer (site) — 520×44 à (2561, 15014) monde

| Élément | Node | L×H | x,y / bulle | x,y / élément | Styles Paper (tokens résolus) |
|---|---|---|---|---|---|
| Frame | `19RN-0` | 305×303 | 0, 0 | 532, -4 | width: 305px; dir: column; radius: 16px; gap: 12px; position: absolute; left: 596; top: 346; height: 303px; align: center; justify: end; overflow: clip; paddingBlock: 6px; paddingInline: 6px; bg: #171717 |
|   └ Barre des changements | `1ANA-0` | 293×207 | 6, 6 | 538, 2 | align: start; paddingRight: 8px; paddingLeft: 8px; gap: 8px; justify: space-between; flex: 1; paddingTop: 8px; paddingBottom: 8px; dir: column; alignSelf: stretch; bg: #FFFFFF14; radius: 10px |
|     └ Frame | `1AS8-0` | 277×20 | 14, 14 | 546, 10 | align: center; gap: 8; paddingInline: 2px; alignSelf: stretch |
|       └ Claude veut modifier… « Claude veut modifier checkout.css » | `1AND-0` | 205×16 | 16, 16 | 548, 12 | font: "Inter Display"; size: 13px; lh: 16px; ws: pre; textOverflow: ellipsis; flex: 1; weight: 500; WebkitBoxOrient: vertical; WebkitLineClamp: 1; overflow: hidden; color: #FFFFFFE6 |
|       └ Frame | `1ASL-0` | 32×16 | 229, 16 | 761, 12 | align: start; gap: 2 |
|         └ +6 « +6 » | `1ANE-0` | 15×16 | 229, 16 | 761, 12 | size: 12px; lh: 16px; ws: pre; width: max-content; shrink: 0; font: "JetBrains Mono"; weight: 400; color: #7FD39A |
|         └ −2 « −2 » | `1ANF-0` | 15×16 | 246, 16 | 778, 12 | size: 12px; lh: 16px; ws: pre; width: max-content; shrink: 0; font: "JetBrains Mono"; weight: 400; color: #FF8A80 |
|       └ Frame | `1ASI-0` | 20×20 | 269, 14 | 801, 10 | align: center; width: 20px; height: 20px; justify: center; radius: 6px; shrink: 0 |
|         └ SVG | `1ASJ-0` | 14×14 | 272, 17 | 804, 13 | SVG shrink: 0 |
|     └ Aperçu | `1AS9-0` | 277×163 | 14, 42 | 546, 38 | dir: column; paddingBlock: 6px; radius: 8px; flex: 1; alignSelf: stretch; bg: #171717 |
|       └ Frame | `1ASA-0` | 277×18 | 14, 48 | 546, 44 | paddingInline: 10px; ws: nowrap; bg: #FF7B6E1A |
|         └ - width: 240px; « -  width: 240px; » | `1ASB-0` | 257×18 | 24, 48 | 556, 44 | size: 12px; lh: 18px; ws: pre; font: "JetBrains Mono"; weight: 400; color: #FF8D80 |
|       └ Frame | `1ASC-0` | 277×18 | 14, 66 | 546, 62 | paddingInline: 10px; ws: nowrap; bg: #6FD0871A |
|         └ +  margin-left: auto… « +  margin-left: auto; » | `1ASD-0` | 257×18 | 24, 66 | 556, 62 | size: 12px; lh: 18px; ws: pre; font: "JetBrains Mono"; weight: 400; color: #7AFFAB |
|       └ Frame | `1ASE-0` | 277×18 | 14, 84 | 546, 80 | paddingInline: 10px; ws: nowrap; bg: #6FD0871A |
|         └ +  width: 100%;  /* … « +  width: 100%;  /* sous 640 px */ » | `1ASF-0` | 257×18 | 24, 84 | 556, 80 | size: 12px; lh: 18px; ws: pre; font: "JetBrains Mono"; weight: 400; color: #7AFFAB |
|   └ Frame | `1AMG-0` | 293×72 | 6, 225 | 538, 221 | dir: column; radius: 16px; gap: 6px; paddingBottom: 6px; alignSelf: stretch; paddingInline: 6px; bg: #1F1F1F |
|     └ Frame | `1ANN-0` | 281×32 | 12, 225 | 544, 221 | dir: column; align: start; gap: 6px; alignSelf: stretch |
|       └ Rediriger | `1AMS-0` | 281×32 | 12, 225 | 544, 221 | align: center; height: 32px; paddingLeft: 8px; paddingRight: 8px; gap: 6; alignSelf: stretch; shrink: 0; bg: #292929; radius: 10px |
|         └ SVG | `1ANS-0` | 14×14 | 20, 234 | 552, 230 | SVG opacity: 50%; shrink: 0 |
|         └ Dire à Claude quoi f… « Dire à Claude quoi faire à la place… » | `1AMT-0` | 197×16 | 40, 233 | 572, 229 | font: "Inter Display"; size: 13px; lh: 16px; weight: 400; ws: pre-wrap; color: color-mix(#FFFFFF 50%, transparent) |
|     └ Actions | `1AMU-0` | 281×28 | 12, 263 | 544, 259 | align: center; gap: 8px; paddingLeft: 4px |
|       └ Toujours autoriser | `1AMV-0` | 131×16 | 16, 269 | 548, 265 | align: center; gap: 6px; flexBasis: 0%; flexGrow: 1 |
|         └ Interrupteur — activé | `1AMW-0` | 28×16 | 16, 269 | 548, 265 | align: center; justify: end; width: 28px; height: 16px; shrink: 0; padding: 2px; radius: 8px; bg: #3E67FF |
|           └ Rectangle | `1AMX-0` | 12×12 | 30, 271 | 562, 267 | width: 12px; height: 12px; radius: 6px; shrink: 0; bg: #FFFFFF |
|         └ Auto. « Auto. » | `1AMY-0` | 28×16 | 50, 269 | 582, 265 | font: "Inter Display"; size: 12px; lh: 16px; ws: pre; weight: 400; color: #FFFFFFB3 |
|       └ Frame | `1AMZ-0` | 61×28 | 155, 263 | 687, 259 | align: center; height: 28px; paddingRight: 8px; paddingLeft: 8px; radius: 6px; gap: 6px; bg: #292929 |
|         └ Refuser « Refuser » | `1AN0-0` | 45×16 | 163, 269 | 695, 265 | font: "Inter Display"; size: 13px; lh: 16px; weight: 500; ws: pre-wrap; color: #FFFFFF |
|       └ Frame | `1AN1-0` | 69×28 | 224, 263 | 756, 259 | align: center; height: 28px; paddingRight: 8px; paddingLeft: 8px; radius: 6px; gap: 6px; bg: #3E67FF |
|         └ Autoriser « Autoriser » | `1AN2-0` | 53×16 | 232, 269 | 764, 265 | font: "Inter Display"; size: 13px; lh: 16px; weight: 500; ws: pre-wrap; color: #FFFFFF |

## 2.2b · Prompt Erreur — `1AAE-0` (1180×815)

- Bulle `1ADE-0` 305×248 ; élément visé `1AD3-0` Bouton Payer (site) 520×44. Bulle − élément : x 532, y -4 ; écart horizontal bord droit élément → bord gauche bulle : 12 px ; haut bulle − bas élément : -48 px ; bulle dans le contenu web : (596, 346).

### Élément visé et marques

- Repère « élément » : `1AD3-0` Bouton Payer (site) — 520×44 à (3798, 14126) monde

| Élément | Node | L×H | x,y / élément | Styles Paper (tokens résolus) |
|---|---|---|---|---|
| Payer — sélectionné | `1AD2-0` | 520×68 | 0, -24 | width: 520px; paddingTop: 24px; position: relative |
|   └ Bouton Payer (site) | `1AD3-0` | 520×44 | 0, 0 | align: center; justify: center; height: 44px; radius: 8px; flex: 1; bg: #1C1C1C |
|     └ Payer 48,00 € « Payer 48,00 € » | `1AD4-0` | 96×20 | 212, 12 | size: 15px; lh: 20px; font: "Inter Display"; weight: 600; ws: pre-wrap; color: #FFFFFF |
|   └ Cadre de sélection | `1AD5-0` | 528×52 | -4, -4 | position: absolute; left: -4; top: 20; height: 52px; width: 528px; bg: #3E67FF14; borderWidth: 2px; borderStyle: solid; borderColor: #3E67FF |
|   └ Étiquette | `1AD6-0` | 118×20 | -4, -24 | position: absolute; left: -4px; top: 0px; align: center; height: 20px; paddingInline: 6px; borderTopLeftRadius: 3px; borderTopRightRadius: 3px; borderBottomRightRadius: 0px; borderBottomLeftRadius: 0px; bg: #3E67FF |
|     └ button.btn-primary « button.btn-primary » | `1AD7-0` | 106×18 | 2, -23 | size: 13px; lh: 18px; ws: pre; font: "Inter Display"; weight: 400; color: #FFFFFFE6 |
|   └ Étiquette — taille | `1AD8-0` | 58×20 | 466, -24 | position: absolute; top: 0; align: center; height: 20px; paddingInline: 6px; width: max-content; borderTopLeftRadius: 3px; borderTopRightRadius: 3px; right: -4; bg: #3E67FF |
|     └ 240x64 « 240x64 » | `1AD9-0` | 46×18 | 472, -23 | size: 13px; lh: 18px; ws: pre; font: "Inter Display"; weight: 400; color: #FFFFFFE6 |

### Bulle

- Repère « bulle » : `1ADE-0` Frame — 305×248 à (4330, 14122) monde
- Repère « élément » : `1AD3-0` Bouton Payer (site) — 520×44 à (3798, 14126) monde

| Élément | Node | L×H | x,y / bulle | x,y / élément | Styles Paper (tokens résolus) |
|---|---|---|---|---|---|
| Frame | `1ADE-0` | 305×248 | 0, 0 | 532, -4 | width: 305px; dir: column; padding: 12px; radius: 16px; gap: 12px; position: absolute; left: 596; top: 346; bg: #171717 |
|   └ Frame | `1ADF-0` | 281×52 | 12, 12 | 544, 8 | dir: column; align: end; gap: 2; justify: center; alignSelf: stretch |
|     └ Frame | `1ADG-0` | 210×52 | 83, 12 | 615, 8 | dir: column; align: start; paddingInline: 10px; paddingBlock: 6px; borderTopLeftRadius: 12px; borderTopRightRadius: 12px; borderBottomLeftRadius: 12px; borderBottomRightRadius: 4px; bg: #3E67FF |
|       └ Fais en sorte que le… « Fais en sorte que le bouton prenne toute la largeur du form » | `1ADH-0` | 190×40 | 93, 18 | 625, 14 | font: "Inter Display"; size: 13px; lh: 20px; width: 190px; weight: 500; ws: pre-wrap; color: #FFFFFFE6 |
|   └ Frame | `1ADI-0` | 281×120 | 12, 76 | 544, 72 | dir: column; align: start; gap: 2; alignSelf: stretch |
|     └ Frame | `1ADJ-0` | 281×24 | 12, 76 | 544, 72 | align: center; justify: space-between; alignSelf: stretch |
|       └ Modèle | `1ADK-0` | 122×24 | 12, 76 | 544, 72 | align: center; height: 24px; gap: 4; paddingLeft: 6px; paddingRight: 8px; borderTopLeftRadius: 12px; borderTopRightRadius: 12px; borderBottomLeftRadius: 4px; borderBottomRightRadius: 12px; bg: #FFFFFF14 |
|         └ Frame | `1ADL-0` | 76×16 | 18, 80 | 550, 76 | align: center; gap: 4px |
|           └ Logo Claude | `1ADM-0` | 14×14 | 18, 81 | 550, 77 | SVG shrink: 0 |
|           └ Sonnet 5.5 « Sonnet 5.5 » | `1ADO-0` | 58×16 | 36, 80 | 568, 76 | font: "Inter Display"; size: 12px; lh: 16px; ws: pre; weight: 500; color: #FFFFFFE6 |
|         └ Frame | `1ADP-0` | 28×16 | 98, 80 | 630, 76 | align: center; gap: 4px |
|           └ Élevé « Élevé » | `1ADQ-0` | 28×16 | 98, 80 | 630, 76 | font: "Inter Display"; size: 12px; lh: 16px; ws: pre; weight: 400; color: color-mix(#FFFFFF 50%, transparent) |
|     └ Frame | `1ADR-0` | 281×70 | 12, 102 | 544, 98 | dir: column; align: start; borderTopLeftRadius: 4px; borderTopRightRadius: 12px; borderBottomLeftRadius: 12px; borderBottomRightRadius: 16px; paddingTop: 4px; paddingBottom: 6px; gap: 4px; overflow: clip; alignSelf: stretch; paddingInline: 8px; backgroundImage: linear-gradient(in oklab 180deg, #FFFFFF14 0%, color-mix(#FF7B6E 10%, transparent) 100%) |
|       └ Le bouton a une larg… « Le bouton a une largeur fixe de 240 px. Je le passe à droite sur grand écran et en pleine largeur sous 640 px. » | `1ADS-0` | 265×60 | 20, 106 | 552, 102 | font: "Inter Display"; size: 13px; lh: 20px; alignSelf: stretch; weight: 400; ws: pre-wrap; color: #FF7B6E |
|     └ Frame | `1ADT-0` | 281×22 | 12, 174 | 544, 170 | align: center; paddingTop: 2px; alignSelf: stretch; paddingInline: 2px |
|       └ Frame | `1ADU-0` | 199×20 | 14, 176 | 546, 172 | align: center; flex: 1; gap: 4px |
|         └ SVG | `1ADV-0` | 14×14 | 14, 179 | 546, 175 | SVG shrink: 0 |
|         └ Une erreur est appar… « Une erreur est apparue. » | `1ADX-0` | 181×20 | 32, 176 | 564, 172 | font: "Inter Display"; size: 13px; lh: 20px; width: 100%; weight: 400; ws: pre-wrap; color: #FF7B6E |
|       └ Frame | `1ADY-0` | 78×20 | 213, 176 | 745, 172 | align: center; gap: 2; paddingLeft: 2px |
|         └ Plus d'infos « Plus d'infos » | `1ADZ-0` | 62×20 | 215, 176 | 747, 172 | font: "Inter Display"; size: 12px; lh: 20px; width: max-content; weight: 500; shrink: 0; ws: pre-wrap; color: #FF7B6E |
|         └ SVG | `1AE0-0` | 12×12 | 279, 180 | 811, 176 | SVG shrink: 0 |
|   └ Frame | `1AE2-0` | 281×28 | 12, 208 | 544, 204 | align: start; gap: 12; justify: start; alignSelf: stretch |
|     └ Frame | `1AE3-0` | 134.5×28 | 12, 208 | 544, 204 | align: center; height: 28px; paddingRight: 8px; paddingLeft: 8px; radius: 6px; gap: 6px; flex: 1; justify: center; bg: #292929 |
|       └ Annuler « Annuler » | `1AE4-0` | 45×16 | 56.75, 214 | 588.75, 210 | font: "Inter Display"; size: 13px; lh: 16px; weight: 500; ws: pre-wrap; color: #FFFFFF |
|     └ Frame | `1AE5-0` | 134.5×28 | 158.5, 208 | 690.5, 204 | align: center; height: 28px; paddingRight: 8px; paddingLeft: 8px; radius: 6px; gap: 6px; flex: 1; justify: center; bg: color-mix(#FF7B6E 10%, transparent) |
|       └ Réessayer « Réessayer » | `1AE6-0` | 60×16 | 195.75, 214 | 727.75, 210 | font: "Inter Display"; size: 13px; lh: 16px; weight: 500; ws: pre-wrap; color: #FF7B6E |

## 2.2b · Prompt Erreur (détails) — `1A6L-0` (1180×815)

- Bulle `1A9L-0` 305×279 ; élément visé `1A9A-0` Bouton Payer (site) 520×44. Bulle − élément : x 532, y -4 ; écart horizontal bord droit élément → bord gauche bulle : 12 px ; haut bulle − bas élément : -48 px ; bulle dans le contenu web : (596, 346).

### Élément visé et marques

- Repère « élément » : `1A9A-0` Bouton Payer (site) — 520×44 à (3798, 15014) monde

| Élément | Node | L×H | x,y / élément | Styles Paper (tokens résolus) |
|---|---|---|---|---|
| Payer — sélectionné | `1A99-0` | 520×68 | 0, -24 | width: 520px; paddingTop: 24px; position: relative |
|   └ Bouton Payer (site) | `1A9A-0` | 520×44 | 0, 0 | align: center; justify: center; height: 44px; radius: 8px; flex: 1; bg: #1C1C1C |
|     └ Payer 48,00 € « Payer 48,00 € » | `1A9B-0` | 96×20 | 212, 12 | size: 15px; lh: 20px; font: "Inter Display"; weight: 600; ws: pre-wrap; color: #FFFFFF |
|   └ Cadre de sélection | `1A9C-0` | 528×52 | -4, -4 | position: absolute; left: -4; top: 20; height: 52px; width: 528px; bg: #3E67FF14; borderWidth: 2px; borderStyle: solid; borderColor: #3E67FF |
|   └ Étiquette | `1A9D-0` | 118×20 | -4, -24 | position: absolute; left: -4px; top: 0px; align: center; height: 20px; paddingInline: 6px; borderTopLeftRadius: 3px; borderTopRightRadius: 3px; borderBottomRightRadius: 0px; borderBottomLeftRadius: 0px; bg: #3E67FF |
|     └ button.btn-primary « button.btn-primary » | `1A9E-0` | 106×18 | 2, -23 | size: 13px; lh: 18px; ws: pre; font: "Inter Display"; weight: 400; color: #FFFFFFE6 |
|   └ Étiquette — taille | `1A9F-0` | 58×20 | 466, -24 | position: absolute; top: 0; align: center; height: 20px; paddingInline: 6px; width: max-content; borderTopLeftRadius: 3px; borderTopRightRadius: 3px; right: -4; bg: #3E67FF |
|     └ 240x64 « 240x64 » | `1A9G-0` | 46×18 | 472, -23 | size: 13px; lh: 18px; ws: pre; font: "Inter Display"; weight: 400; color: #FFFFFFE6 |

### Bulle

- Repère « bulle » : `1A9L-0` Frame — 305×279 à (4330, 15010) monde
- Repère « élément » : `1A9A-0` Bouton Payer (site) — 520×44 à (3798, 15014) monde

| Élément | Node | L×H | x,y / bulle | x,y / élément | Styles Paper (tokens résolus) |
|---|---|---|---|---|---|
| Frame | `1A9L-0` | 305×279 | 0, 0 | 532, -4 | width: 305px; dir: column; padding: 12px; radius: 16px; gap: 12px; position: absolute; left: 596; top: 346; bg: #171717 |
|   └ Frame | `1A9M-0` | 281×52 | 12, 12 | 544, 8 | dir: column; align: end; gap: 2; justify: center; alignSelf: stretch |
|     └ Frame | `1A9N-0` | 210×52 | 83, 12 | 615, 8 | dir: column; align: start; paddingInline: 10px; paddingBlock: 6px; borderTopLeftRadius: 12px; borderTopRightRadius: 12px; borderBottomLeftRadius: 12px; borderBottomRightRadius: 4px; bg: #3E67FF |
|       └ Fais en sorte que le… « Fais en sorte que le bouton prenne toute la largeur du form » | `1A9O-0` | 190×40 | 93, 18 | 625, 14 | font: "Inter Display"; size: 13px; lh: 20px; width: 190px; weight: 500; ws: pre-wrap; color: #FFFFFFE6 |
|   └ Frame | `1A9P-0` | 281×151 | 12, 76 | 544, 72 | dir: column; align: start; gap: 2; alignSelf: stretch |
|     └ Frame | `1A9Q-0` | 281×24 | 12, 76 | 544, 72 | align: center; justify: space-between; alignSelf: stretch |
|       └ Modèle | `1A9R-0` | 122×24 | 12, 76 | 544, 72 | align: center; height: 24px; gap: 4; paddingLeft: 6px; paddingRight: 8px; borderTopLeftRadius: 12px; borderTopRightRadius: 12px; borderBottomLeftRadius: 4px; borderBottomRightRadius: 12px; bg: #FFFFFF14 |
|         └ Frame | `1A9S-0` | 76×16 | 18, 80 | 550, 76 | align: center; gap: 4px |
|           └ Logo Claude | `1A9T-0` | 14×14 | 18, 81 | 550, 77 | SVG shrink: 0 |
|           └ Sonnet 5.5 « Sonnet 5.5 » | `1A9V-0` | 58×16 | 36, 80 | 568, 76 | font: "Inter Display"; size: 12px; lh: 16px; ws: pre; weight: 500; color: #FFFFFFE6 |
|         └ Frame | `1A9W-0` | 28×16 | 98, 80 | 630, 76 | align: center; gap: 4px |
|           └ Élevé « Élevé » | `1A9X-0` | 28×16 | 98, 80 | 630, 76 | font: "Inter Display"; size: 12px; lh: 16px; ws: pre; weight: 400; color: color-mix(#FFFFFF 50%, transparent) |
|     └ Frame | `1A9Y-0` | 281×22 | 12, 102 | 544, 98 | align: center; paddingTop: 2px; height: 22px; alignSelf: stretch; shrink: 0; paddingInline: 2px |
|       └ Frame | `1A9Z-0` | 199×20 | 14, 104 | 546, 100 | align: center; flex: 1; gap: 4px |
|         └ SVG | `1AA0-0` | 14×14 | 14, 107 | 546, 103 | SVG shrink: 0 |
|         └ Une erreur est appar… « Une erreur est apparue. » | `1AA2-0` | 181×20 | 32, 104 | 564, 100 | font: "Inter Display"; size: 13px; lh: 20px; width: 100%; weight: 400; ws: pre-wrap; color: #FF7B6E |
|       └ Frame | `1AA3-0` | 78×20 | 213, 104 | 745, 100 | align: center; gap: 2; paddingLeft: 2px |
|         └ Plus d'infos « Plus d'infos » | `1AA4-0` | 62×20 | 215, 104 | 747, 100 | font: "Inter Display"; size: 12px; lh: 20px; width: max-content; weight: 500; shrink: 0; ws: pre-wrap; color: #FF7B6E |
|         └ SVG | `1AA5-0` | 12×12 | 279, 108 | 811, 104 | SVG rotate: 90deg; shrink: 0; transformOrigin: 50% 50% |
|     └ Frame | `1AA7-0` | 281×101 | 12, 126 | 544, 122 | dir: column; align: start; paddingInline: 4px; alignSelf: stretch |
|       └ Erreur 529 · overloa… « Erreur 529 · overloaded_error Les serveurs de Claude étaient surchargés. La réponse s'est arrêtée au bout de 18 s, avant toute modification : aucun fichier n'a été changé. « Réessayer » relance la même demande. » | `1AA8-0` | 273×101 | 16, 126 | 548, 122 | font: "Inter Display"; size: 13px; lh: 20px; width: 273px; weight: 400; ws: pre-wrap; color: color-mix(#FF7B6E 80%, transparent) |
|   └ Frame | `1AA9-0` | 281×28 | 12, 239 | 544, 235 | align: start; gap: 12; justify: start; alignSelf: stretch |
|     └ Frame | `1AAA-0` | 134.5×28 | 12, 239 | 544, 235 | align: center; height: 28px; paddingRight: 8px; paddingLeft: 8px; radius: 6px; gap: 6px; flex: 1; justify: center; bg: #292929 |
|       └ Annuler « Annuler » | `1AAB-0` | 45×16 | 56.75, 245 | 588.75, 241 | font: "Inter Display"; size: 13px; lh: 16px; weight: 500; ws: pre-wrap; color: #FFFFFF |
|     └ Frame | `1AAC-0` | 134.5×28 | 158.5, 239 | 690.5, 235 | align: center; height: 28px; paddingRight: 8px; paddingLeft: 8px; radius: 6px; gap: 6px; flex: 1; justify: center; bg: color-mix(#FF7B6E 10%, transparent) |
|       └ Réessayer « Réessayer » | `1AAD-0` | 60×16 | 195.75, 245 | 727.75, 241 | font: "Inter Display"; size: 13px; lh: 16px; weight: 500; ws: pre-wrap; color: #FF7B6E |

## 3 · Prompt terminé — `1AWI-0` (1180×815)

- Bulle `1AZR-0` 305×279 ; élément visé `1AZG-0` Bouton Payer (site) 520×44. Bulle − élément : x 530.0010000000002, y 0 ; écart horizontal bord droit élément → bord gauche bulle : 10.001000000000204 px ; haut bulle − bas élément : -44 px ; bulle dans le contenu web : (594.0010000000002, 350).

### Élément visé et marques

- Repère « élément » : `1AZG-0` Bouton Payer (site) — 520×44 à (5035, 14126) monde

| Élément | Node | L×H | x,y / élément | Styles Paper (tokens résolus) |
|---|---|---|---|---|
| Payer — sélectionné | `1AZF-0` | 520×68 | 0, -24 | width: 520px; paddingTop: 24px; position: relative |
|   └ Bouton Payer (site) | `1AZG-0` | 520×44 | 0, 0 | align: center; justify: center; height: 44px; radius: 8px; flex: 1; bg: #1C1C1C |
|     └ Payer 48,00 € « Payer 48,00 € » | `1AZH-0` | 96×20 | 212, 12 | size: 15px; lh: 20px; font: "Inter Display"; weight: 600; ws: pre-wrap; color: #FFFFFF |
|   └ Cadre de sélection | `1AZI-0` | 528×52 | -4, -4 | position: absolute; left: -4; top: 20; height: 52px; width: 528px; bg: #3E67FF14; borderWidth: 2px; borderStyle: solid; borderColor: #3E67FF |
|   └ Étiquette | `1AZJ-0` | 118×20 | -4, -24 | position: absolute; left: -4px; top: 0px; align: center; height: 20px; paddingInline: 6px; borderTopLeftRadius: 3px; borderTopRightRadius: 3px; borderBottomRightRadius: 0px; borderBottomLeftRadius: 0px; bg: #3E67FF |
|     └ button.btn-primary « button.btn-primary » | `1AZK-0` | 106×18 | 2.0, -23 | size: 13px; lh: 18px; ws: pre; font: "Inter Display"; weight: 400; color: #FFFFFFE6 |
|   └ Étiquette — taille | `1AZL-0` | 58×20 | 466, -24 | position: absolute; top: 0; align: center; height: 20px; paddingInline: 6px; width: max-content; borderTopLeftRadius: 3px; borderTopRightRadius: 3px; right: -4; bg: #3E67FF |
|     └ 240x64 « 240x64 » | `1AZM-0` | 46×18 | 472, -23 | size: 13px; lh: 18px; ws: pre; font: "Inter Display"; weight: 400; color: #FFFFFFE6 |

### Bulle

- Repère « bulle » : `1AZR-0` Frame — 305×279 à (5565.001, 14126) monde
- Repère « élément » : `1AZG-0` Bouton Payer (site) — 520×44 à (5035, 14126) monde

| Élément | Node | L×H | x,y / bulle | x,y / élément | Styles Paper (tokens résolus) |
|---|---|---|---|---|---|
| Frame | `1AZR-0` | 305×279 | 0.0, 0 | 530.0, 0 | width: 305px; dir: column; padding: 12px; gap: 12px; position: absolute; left: 594; top: 350; radius: 16px; bg: #171717 |
|   └ Frame | `1AZS-0` | 281×52 | 12.0, 12 | 542, 12 | dir: column; align: end; gap: 2; justify: center; alignSelf: stretch |
|     └ Frame | `1AZT-0` | 210×52 | 83.0, 12 | 613, 12 | dir: column; align: start; paddingInline: 10px; paddingBlock: 6px; borderTopLeftRadius: 10px; borderTopRightRadius: 10px; borderBottomLeftRadius: 10px; borderBottomRightRadius: 4px; bg: #3E67FF |
|       └ Fais en sorte que le… « Fais en sorte que le bouton prenne toute la largeur du form » | `1AZU-0` | 190×40 | 93.0, 18 | 623.0, 18 | font: "Inter Display"; size: 13px; lh: 20px; width: 190px; weight: 500; ws: pre-wrap; color: #FFFFFFE6 |
|   └ Frame | `1AZV-0` | 281×110 | 12.0, 76 | 542, 76 | dir: column; align: start; width: fit-content; gap: 2 |
|     └ Frame | `1AZW-0` | 281×24 | 12.0, 76 | 542, 76 | align: center; justify: space-between; paddingRight: 4px; alignSelf: stretch |
|       └ Modèle | `1AZX-0` | 122×24 | 12.0, 76 | 542, 76 | align: center; height: 24px; gap: 4; paddingLeft: 6px; paddingRight: 8px; borderTopLeftRadius: 12px; borderTopRightRadius: 12px; borderBottomLeftRadius: 4px; borderBottomRightRadius: 12px; bg: #FFFFFF14 |
|         └ Frame | `1AZY-0` | 76×16 | 18.0, 80 | 548.0, 80 | align: center; gap: 4px |
|           └ Logo Claude | `1AZZ-0` | 14×14 | 18.0, 81 | 548.0, 81 | SVG shrink: 0 |
|           └ Sonnet 5.5 « Sonnet 5.5 » | `1B01-0` | 58×16 | 36.0, 80 | 566.0, 80 | font: "Inter Display"; size: 12px; lh: 16px; ws: pre; weight: 500; color: #FFFFFFE6 |
|         └ Frame | `1B02-0` | 28×16 | 98.0, 80 | 628, 80 | align: center; gap: 4px |
|           └ Élevé « Élevé » | `1B03-0` | 28×16 | 98.0, 80 | 628, 80 | font: "Inter Display"; size: 12px; lh: 16px; ws: pre; weight: 400; color: #FFFFFF80 |
|       └ Frame | `1B7X-0` | 76×20 | 213.0, 78 | 743, 78 | align: center; gap: 4px |
|         └ SVG | `1B7Y-0` | 14×14 | 213.0, 81 | 743, 81 | SVG opacity: 0.75; shrink: 0 |
|         └ Historique « Historique » | `1B7W-0` | 58×20 | 231.0, 78 | 761, 78 | font: "Inter Display"; size: 13px; lh: 20px; width: max-content; shrink: 0; opacity: 0.75; weight: 400; ws: pre-wrap; color: #FFFFFF |
|     └ Frame | `1B06-0` | 281×50 | 12.0, 102 | 542, 102 | dir: column; align: start; borderTopLeftRadius: 4px; borderTopRightRadius: 10px; borderBottomLeftRadius: 4px; borderBottomRightRadius: 4px; paddingTop: 4px; paddingBottom: 6px; alignSelf: stretch; paddingInline: 8px; bg: #FFFFFF14 |
|       └ C'est fait. La page … « C'est fait. La page s'est rechargée ; l'annotation est marquée « terminée ». » | `1B07-0` | 265×40 | 20.0, 106 | 550, 106 | font: "Inter Display"; size: 13px; lh: 20px; alignSelf: stretch; weight: 400; ws: pre-wrap; color: #FFFFFFE6 |
|     └ Barre des changements | `1B08-0` | 281×32 | 12.0, 154 | 542, 154 | align: center; height: 32px; paddingRight: 2px; paddingLeft: 10px; gap: 8px; borderTopLeftRadius: 4px; borderTopRightRadius: 4px; borderBottomLeftRadius: 10px; borderBottomRightRadius: 10px; alignSelf: stretch; shrink: 0; bg: #FFFFFF14 |
|       └ SVG | `1B09-0` | 14×14 | 22.0, 163 | 552, 163 | SVG opacity: 50%; shrink: 0 |
|       └ 1 fichier modifié « 1 fichier modifié » | `1B0B-0` | 90×16 | 44.0, 162 | 574, 162 | font: "Inter Display"; size: 13px; lh: 16px; ws: pre; weight: 500; color: #FFFFFFE6 |
|       └ +6 « +6 » | `1B0C-0` | 15×16 | 142.0, 162 | 672, 162 | size: 12px; lh: 16px; shrink: 0; ws: pre; font: "JetBrains Mono"; weight: 400; color: #7FD39A |
|       └ −2 « −2 » | `1B0D-0` | 15×16 | 165.0, 162 | 695.0, 162 | size: 12px; lh: 16px; shrink: 0; ws: pre; font: "JetBrains Mono"; weight: 400; color: #FF8A80 |
|       └ SVG | `1B0E-0` | 11×11 | 188.0, 164.5 | 718, 164.5 | SVG opacity: 0.45; shrink: 0; rotate: 270deg; transformOrigin: 50% 50% |
|       └ Rectangle | `1B0G-0` | 8×None | 207.0, 170 | 737, 170 | flexGrow: 1; flexBasis: 0% |
|       └ Frame | `1B0H-0` | 68×26 | 223.0, 157 | 753, 157 | align: center; height: 26px; paddingInline: 8px; radius: 8px; gap: 4px; bg: #1F1F1F |
|         └ SVG | `1B0I-0` | 12×12 | 231.0, 164 | 761, 164 | SVG shrink: 0 |
|         └ Copier « Copier » | `1B0K-0` | 36×16 | 247.0, 162 | 777, 162 | font: "Inter Display"; size: 12px; lh: 16px; ws: pre; weight: 500; color: #FFFFFF |
|   └ Rectangle | `1B0L-0` | 281×1 | 12.0, 198 | 542, 198 | height: 1px; radius: calc(infinity * 1px); alignSelf: stretch; shrink: 0; bg: color-mix(#DDDDDD 10%, transparent) |
|   └ Frame | `1B0M-0` | 281×56 | 12.0, 211 | 542, 211 | dir: column; align: start; gap: 8; alignSelf: stretch |
|     └ Frame | `1B0N-0` | 281×20 | 12.0, 211 | 542, 211 | dir: column; align: start; paddingInline: 2px; alignSelf: stretch |
|       └ Que voulez-vous ajou… « Que voulez-vous ajouter de plus ? » | `1B0O-0` | 277×20 | 14.0, 211 | 544, 211 | font: "Inter Display"; size: 14px; lh: 20px; height: 20px; alignSelf: stretch; shrink: 0; weight: 400; ws: pre-wrap; color: color-mix(#FFFFFF 50%, transparent) |
|     └ Pied | `1B0P-0` | 281×28 | 12.0, 239 | 542, 239 | align: center; gap: 4px; justify: space-between; alignSelf: stretch |
|       └ Frame | `1B0Q-0` | 77×24 | 12.0, 241 | 542, 241 | align: start |
|         └ Ajouter | `1B0R-0` | 24×24 | 12.0, 241 | 542, 241 | width: 24px; height: 24px; align: center; justify: center; radius: 6px; shrink: 0 |
|           └ SVG | `1B0S-0` | 14×14 | 17.0, 246 | 547.0, 246 | SVG opacity: 50%; shrink: 0 |
|         └ Mode | `1B0U-0` | 53×24 | 36.0, 241 | 566.0, 241 | align: center; height: 24px; paddingInline: 6px; radius: 6px; gap: 5px |
|           └ SVG | `1B0V-0` | 13×13 | 42.0, 246.5 | 572, 246.5 | SVG opacity: 0.9; shrink: 0 |
|           └ Ask « Ask » | `1B0X-0` | 23×20 | 60.0, 243 | 590, 243 | font: "Inter Display"; size: 14px; lh: 20px; width: max-content; shrink: 0; weight: 400; ws: pre-wrap; color: color-mix(#FFFFFF 50%, transparent) |
|       └ Frame | `1B0Y-0` | 152×28 | 141.0, 239 | 671, 239 | align: center; gap: 4 |
|         └ Modèle | `1B0Z-0` | 120×24 | 141.0, 241 | 671, 241 | align: center; height: 24px; paddingInline: 6px; radius: 6px; gap: 4 |
|           └ Frame | `1B10-0` | 76×16 | 147.0, 245 | 677.0, 245 | align: center; gap: 4px |
|             └ Logo Claude | `1B11-0` | 14×14 | 147.0, 246 | 677.0, 246 | SVG shrink: 0 |
|             └ Sonnet 5.5 « Sonnet 5.5 » | `1B13-0` | 58×16 | 165.0, 245 | 695.0, 245 | font: "Inter Display"; size: 12px; lh: 16px; ws: pre; weight: 500; color: #FFFFFFE6 |
|           └ Frame | `1B14-0` | 28×16 | 227.0, 245 | 757, 245 | align: center; gap: 4px |
|             └ Élevé « Élevé » | `1B15-0` | 28×16 | 227.0, 245 | 757, 245 | font: "Inter Display"; size: 12px; lh: 16px; ws: pre; weight: 400; color: #FFFFFF80 |
|         └ Envoyer | `1B16-0` | 28×28 | 265.0, 239 | 795, 239 | width: 28px; height: 28px; align: center; justify: center; radius: 8px; shrink: 0; bg: #3E67FF |
|           └ SVG | `1B17-0` | 14×14 | 272.0, 246 | 802, 246 | SVG shrink: 0 |

## 3 · Prompt terminé (code ouvert) — `19IO-0` (1180×815)

- Bulle `1B19-0` 305×303 ; élément visé `19LD-0` Bouton Payer (site) 520×44. Bulle − élément : x 532, y -4 ; écart horizontal bord droit élément → bord gauche bulle : 12 px ; haut bulle − bas élément : -48 px ; bulle dans le contenu web : (596, 346).

### Élément visé et marques

- Repère « élément » : `19LD-0` Bouton Payer (site) — 520×44 à (5035, 15014) monde

| Élément | Node | L×H | x,y / élément | Styles Paper (tokens résolus) |
|---|---|---|---|---|
| Payer — sélectionné | `19LC-0` | 520×68 | 0, -24 | width: 520px; paddingTop: 24px; position: relative |
|   └ Bouton Payer (site) | `19LD-0` | 520×44 | 0, 0 | align: center; justify: center; height: 44px; radius: 8px; flex: 1; bg: #1C1C1C |
|     └ Payer 48,00 € « Payer 48,00 € » | `19LE-0` | 96×20 | 212, 12 | size: 15px; lh: 20px; font: "Inter Display"; weight: 600; ws: pre-wrap; color: #FFFFFF |
|   └ Cadre de sélection | `19LF-0` | 528×52 | -4, -4 | position: absolute; left: -4; top: 20; height: 52px; width: 528px; bg: #3E67FF14; borderWidth: 2px; borderStyle: solid; borderColor: #3E67FF |
|   └ Étiquette | `19LG-0` | 118×20 | -4, -24 | position: absolute; left: -4px; top: 0px; align: center; height: 20px; paddingInline: 6px; borderTopLeftRadius: 3px; borderTopRightRadius: 3px; borderBottomRightRadius: 0px; borderBottomLeftRadius: 0px; bg: #3E67FF |
|     └ button.btn-primary « button.btn-primary » | `19LH-0` | 106×18 | 2.0, -23 | size: 13px; lh: 18px; ws: pre; font: "Inter Display"; weight: 400; color: #FFFFFFE6 |
|   └ Étiquette — taille | `19LI-0` | 58×20 | 466, -24 | position: absolute; top: 0; align: center; height: 20px; paddingInline: 6px; width: max-content; borderTopLeftRadius: 3px; borderTopRightRadius: 3px; right: -4; bg: #3E67FF |
|     └ 240x64 « 240x64 » | `19LJ-0` | 46×18 | 472, -23 | size: 13px; lh: 18px; ws: pre; font: "Inter Display"; weight: 400; color: #FFFFFFE6 |

### Bulle

- Repère « bulle » : `1B19-0` Frame — 305×303 à (5567, 15010) monde
- Repère « élément » : `19LD-0` Bouton Payer (site) — 520×44 à (5035, 15014) monde

| Élément | Node | L×H | x,y / bulle | x,y / élément | Styles Paper (tokens résolus) |
|---|---|---|---|---|---|
| Frame | `1B19-0` | 305×303 | 0, 0 | 532, -4 | width: 305px; dir: column; radius: 16px; gap: 12px; position: absolute; left: 596; top: 346; height: 303px; align: center; justify: end; overflow: clip; paddingBlock: 6px; paddingInline: 6px; bg: #171717 |
|   └ Barre des changements | `1B1A-0` | 293×204 | 6, 6 | 538, 2 | align: start; paddingRight: 8px; paddingLeft: 8px; gap: 8px; justify: space-between; flex: 1; paddingTop: 8px; paddingBottom: 8px; dir: column; alignSelf: stretch; bg: #FFFFFF14; radius: 10px |
|     └ Frame | `1B1B-0` | 277×20 | 14, 14 | 546, 10 | align: center; gap: 8; paddingInline: 2px; alignSelf: stretch |
|       └ SVG | `1B25-0` | 14×14 | 16.0, 17 | 548.0, 13 | SVG opacity: 50%; shrink: 0 |
|       └ checkout.css « checkout.css » | `1B1C-0` | 183×16 | 38, 16 | 570, 12 | font: "Inter Display"; size: 13px; lh: 16px; ws: pre; textOverflow: ellipsis; flex: 1; weight: 500; WebkitBoxOrient: vertical; WebkitLineClamp: 1; overflow: hidden; color: #FFFFFFE6 |
|       └ Frame | `1B1D-0` | 32×16 | 229, 16 | 761, 12 | align: start; gap: 2 |
|         └ +6 « +6 » | `1B1E-0` | 15×16 | 229, 16 | 761, 12 | size: 12px; lh: 16px; ws: pre; width: max-content; shrink: 0; font: "JetBrains Mono"; weight: 400; color: #7FD39A |
|         └ −2 « −2 » | `1B1F-0` | 15×16 | 246.0, 16 | 778.0, 12 | size: 12px; lh: 16px; ws: pre; width: max-content; shrink: 0; font: "JetBrains Mono"; weight: 400; color: #FF8A80 |
|       └ Frame | `1B1G-0` | 20×20 | 269, 14 | 801, 10 | align: center; width: 20px; height: 20px; justify: center; radius: 6px; shrink: 0 |
|         └ SVG | `1B1H-0` | 14×14 | 272.0, 17 | 804.0, 13 | SVG shrink: 0 |
|     └ Aperçu | `1B1J-0` | 277×160 | 14, 42 | 546, 38 | dir: column; paddingBlock: 6px; radius: 8px; flex: 1; position: relative; alignSelf: stretch; bg: #171717 |
|       └ Frame | `1B1K-0` | 277×18 | 14, 48 | 546, 44 | paddingInline: 10px; ws: nowrap; bg: #FF7B6E1A |
|         └ - width: 240px; « -  width: 240px; » | `1B1L-0` | 257×18 | 24, 48 | 556, 44 | size: 12px; lh: 18px; ws: pre; font: "JetBrains Mono"; weight: 400; color: #FF8D80 |
|       └ Frame | `1B1M-0` | 277×18 | 14, 66 | 546, 62 | paddingInline: 10px; ws: nowrap; bg: #6FD0871A |
|         └ +  margin-left: auto… « +  margin-left: auto; » | `1B1N-0` | 257×18 | 24, 66 | 556, 62 | size: 12px; lh: 18px; ws: pre; font: "JetBrains Mono"; weight: 400; color: #7AFFAB |
|       └ Frame | `1B1O-0` | 277×18 | 14, 84 | 546, 80 | paddingInline: 10px; ws: nowrap; bg: #6FD0871A |
|         └ +  width: 100%;  /* … « +  width: 100%;  /* sous 640 px */ » | `1B1P-0` | 257×18 | 24, 84 | 556, 80 | size: 12px; lh: 18px; ws: pre; font: "JetBrains Mono"; weight: 400; color: #7AFFAB |
|       └ Frame | `1B2W-0` | 68×26 | 223, 176 | 755, 172 | align: center; height: 26px; paddingInline: 8px; gap: 4px; width: max-content; position: absolute; right: 0; bottom: 0; borderTopLeftRadius: 8px; borderBottomRightRadius: 8px; bg: #1F1F1F |
|         └ SVG | `1B2X-0` | 12×12 | 231, 183 | 763, 179 | SVG shrink: 0 |
|         └ Copier « Copier » | `1B2Z-0` | 36×16 | 247, 181 | 779, 177 | font: "Inter Display"; size: 12px; lh: 16px; ws: pre; weight: 500; color: #FFFFFF |
|   └ Frame | `1B2V-0` | 293×75 | 6, 222 | 538, 218 | dir: column; align: start; gap: 12; paddingBottom: 6px; alignSelf: stretch; paddingInline: 6px |
|     └ Rectangle | `1B27-0` | 281×1 | 12, 222 | 544, 218 | height: 1px; radius: calc(infinity * 1px); alignSelf: stretch; shrink: 0; bg: color-mix(#DDDDDD 10%, transparent) |
|     └ Frame | `1B28-0` | 281×56 | 12, 235 | 544, 231 | dir: column; align: start; gap: 8; alignSelf: stretch |
|       └ Frame | `1B29-0` | 281×20 | 12, 235 | 544, 231 | dir: column; align: start; paddingInline: 2px; alignSelf: stretch |
|         └ Que voulez-vous ajou… « Que voulez-vous ajouter de plus ? » | `1B2A-0` | 277×20 | 14, 235 | 546, 231 | font: "Inter Display"; size: 14px; lh: 20px; height: 20px; alignSelf: stretch; shrink: 0; weight: 400; ws: pre-wrap; color: color-mix(#FFFFFF 50%, transparent) |
|       └ Pied | `1B2B-0` | 281×28 | 12, 263 | 544, 259 | align: center; gap: 4px; justify: space-between; alignSelf: stretch |
|         └ Frame | `1B2C-0` | 77×24 | 12, 265 | 544, 261 | align: start |
|           └ Ajouter | `1B2D-0` | 24×24 | 12, 265 | 544, 261 | width: 24px; height: 24px; align: center; justify: center; radius: 6px; shrink: 0 |
|             └ SVG | `1B2E-0` | 14×14 | 17, 270 | 549, 266 | SVG opacity: 50%; shrink: 0 |
|           └ Mode | `1B2G-0` | 53×24 | 36, 265 | 568, 261 | align: center; height: 24px; paddingInline: 6px; radius: 6px; gap: 5px |
|             └ SVG | `1B2H-0` | 13×13 | 42, 270.5 | 574, 266.5 | SVG opacity: 0.9; shrink: 0 |
|             └ Ask « Ask » | `1B2J-0` | 23×20 | 60, 267 | 592, 263 | font: "Inter Display"; size: 14px; lh: 20px; width: max-content; shrink: 0; weight: 400; ws: pre-wrap; color: color-mix(#FFFFFF 50%, transparent) |
|         └ Frame | `1B2K-0` | 152×28 | 141, 263 | 673, 259 | align: center; gap: 4 |
|           └ Modèle | `1B2L-0` | 120×24 | 141, 265 | 673, 261 | align: center; height: 24px; paddingInline: 6px; radius: 6px; gap: 4 |
|             └ Frame | `1B2M-0` | 76×16 | 147, 269 | 679, 265 | align: center; gap: 4px |
|               └ Logo Claude | `1B2N-0` | 14×14 | 147, 270 | 679, 266 | SVG shrink: 0 |
|               └ Sonnet 5.5 « Sonnet 5.5 » | `1B2P-0` | 58×16 | 165, 269 | 697, 265 | font: "Inter Display"; size: 12px; lh: 16px; ws: pre; weight: 500; color: #FFFFFFE6 |
|             └ Frame | `1B2Q-0` | 28×16 | 227, 269 | 759, 265 | align: center; gap: 4px |
|               └ Élevé « Élevé » | `1B2R-0` | 28×16 | 227, 269 | 759, 265 | font: "Inter Display"; size: 12px; lh: 16px; ws: pre; weight: 400; color: #FFFFFF80 |
|           └ Envoyer | `1B2S-0` | 28×28 | 265, 263 | 797, 259 | width: 28px; height: 28px; align: center; justify: center; radius: 8px; shrink: 0; bg: #3E67FF |
|             └ SVG | `1B2T-0` | 14×14 | 272.0, 270 | 804.0, 266 | SVG shrink: 0 |

## 4 · Prompt terminé - Désélectionné — `18IJ-0` (1180×815)


### Élément visé et marques

- Repère « élément » : `18L8-0` Bouton Payer (site) — 520×44 à (6284, 14126) monde

| Élément | Node | L×H | x,y / élément | Styles Paper (tokens résolus) |
|---|---|---|---|---|
| Payer — sélectionné | `18L7-0` | 520×68 | 0, -24 | width: 520px; paddingTop: 24px; position: relative |
|   └ Bouton Payer (site) | `18L8-0` | 520×44 | 0, 0 | align: center; justify: center; height: 44px; radius: 8px; flex: 1; bg: #1C1C1C |
|     └ Payer 48,00 € « Payer 48,00 € » | `18L9-0` | 96×20 | 212, 12 | size: 15px; lh: 20px; font: "Inter Display"; weight: 600; ws: pre-wrap; color: #FFFFFF |
|   └ Cadre de sélection | `18LA-0` | 528×52 | -4, -4 | position: absolute; left: -4; top: 20; height: 52px; width: 528px; borderWidth: 2px; borderStyle: solid; borderColor: #3E67FF |
|     └ Annoter (actif) | `19NA-0` | 24×24 | -3.8, -3.8 | align: center; justify: center; width: 24px; height: 24px; borderBottomRightRadius: 6px; position: absolute; left: -1.4; top: -1.4; bg: #3E67FF |
|       └ SVG | `19NC-0` | 13×12.25 | 1.7, 2.08 | SVG shrink: 0 |

# Menus 6.4 et bandeau

### Bandeau avec masque et sélecteur (planche 3)

- Repère « racine » : `1AYH-0` Bandeau — Mode Annotation — 1180×32 à (4971, 13744) monde

| Élément | Node | L×H | x,y / racine | Styles Paper (tokens résolus) |
|---|---|---|---|---|
| Bandeau — Mode Annotation | `1AYH-0` | 1180×32 | 0, 0 | align: center; height: 32px; shrink: 0; paddingRight: 8px; paddingLeft: 12px; gap: 12px; position: relative; alignSelf: stretch; backgroundImage: linear-gradient(in oklab 180deg, #3E67FF00 0%, color-mix(#233FD6 32%, transparent) 100%); backgroundOrigin: border-box; borderBottomWidth: 1px; borderBottomStyle: solid; borderBottomColor: #3E67FF3D |
|   └ Vague tramée | `1AYI-0` | 1180×32 | 0, 0 | position: absolute; left: 0px; top: 0px; width: 1180px; height: 32px; opacity: 0.85; backgroundImage: url(https://app.paper.design/file-assets/01M6Q7YT14ZKMGET54ASPNGQEC/374AEVG4WY76A1Z6JN205KDMBC.png); backgroundSize: 100% 100%; backgroundPosition: 50%; backgroundRepeat: no-repeat |
|   └ Masque | `1AYJ-0` | 1180×32 | 0, 0 | position: absolute; left: 0px; top: 0px; width: 1180px; height: 32px; opacity: 0.85; backgroundImage: linear-gradient(in oklab 90deg, color-mix(#171717 0%, transparent) 60.66%, #171717 82.29%) |
|   └ Frame | `1AYK-0` | 145×29 | 12, 1.1 | align: center; gap: 6px; width: fit-content; position: relative |
|     └ Heatmap | `1AYL-0` | 31×29 | 12, 1.1 | width: 31px; height: 29px; shrink: 0; bg: #00000000 |
|     └ Mode Annotation « Mode Annotation » | `1AYM-0` | 108×20 | 49, 5.6 | size: 14px; lh: 20px; ws: pre; font: "Inter Display"; weight: 500; color: #5B84FF |
|   └ Rappel — aucun agent | `1AYN-0` | 828×None | 169, 15.6 | justify: end; align: center; flexGrow: 1; flexBasis: 0%; paddingRight: 8px; position: relative; height: 0px |
|   └ Frame | `1AYO-0` | 163×31.1875 | 1009, 0 | align: center; gap: 4; position: relative; alignSelf: stretch |
|     └ Frame | `1AYP-0` | 58×31.1875 | 1009, 0 | align: center; paddingBlock: 4px; paddingInline: 4px; justify: center; gap: 2; radius: 10px; alignSelf: stretch |
|       └ Frame | `1AYQ-0` | 24×24 | 1013, 3.6 | align: center; height: 24px; paddingInline: 8px; gap: 4px; width: 24px; justify: center; borderTopLeftRadius: 6px; borderTopRightRadius: 6px; borderBottomLeftRadius: 6px; borderBottomRightRadius: 6px; shrink: 0; bg: color-mix(#FFFFFF 10%, transparent) |
|         └ SVG | `1AYR-0` | 14×14 | 1018, 8.6 | SVG opacity: 0.8; shrink: 0 |
|       └ Frame | `1AYT-0` | 24×24 | 1039, 3.6 | align: center; height: 24px; paddingInline: 8px; gap: 4px; width: 24px; justify: center; radius: 6px; shrink: 0 |
|         └ SVG | `1AYU-0` | 14×14 | 1044.0, 8.6 | SVG rotate: 180deg; opacity: 50%; shrink: 0; transformOrigin: 50% 50% |
|     └ Quitter | `1AYW-0` | 101×24 | 1071, 3.6 | align: center; height: 24px; paddingRight: 4px; paddingLeft: 8px; radius: 3px; gap: 8px |
|       └ Quitter « Quitter » | `1AYX-0` | 40×16 | 1079, 7.6 | size: 13px; lh: 16px; font: "Inter Display"; weight: 500; ws: pre-wrap; color: #FFFFFFE6 |
|       └ Frame | `1AYY-0` | 41×18 | 1127.0, 6.6 | align: center; height: 18px; paddingInline: 5px; radius: 3px; bg: #FFFFFF14 |
|         └ Échap « Échap » | `1AYZ-0` | 31×14 | 1132, 8.6 | size: 11px; lh: 14px; font: "Inter Display"; weight: 400; ws: pre-wrap; color: #FFFFFFB3 |

### Menu 6.4 · modèle

- Repère « racine » : `17US-0` Dropover Models — 300×280.59375 à (40, 10511.6) monde

| Élément | Node | L×H | x,y / racine | Styles Paper (tokens résolus) |
|---|---|---|---|---|
| Dropover Models | `17US-0` | 300×280.59375 | 0, 0.0 | width: 300px; dir: column; radius: 12px; padding: 6px; gap: 1; bg: #242424; borderWidth: 1px; borderStyle: solid; borderColor: #FFFFFF1A |
|   └ Frame | `17UT-0` | 286.390625×30 | 6.8, 6.8 | align: center; radius: 8px; gap: 10px; paddingInline: 10px; paddingBlock: 6px |
|     └ Frame | `17UU-0` | 266.390625×18 | 16.8, 12.8 | dir: column; flexGrow: 1; flexBasis: 0%; gap: 1px |
|       └ Opus 5.5 « Opus 5.5 » | `17UV-0` | 266.390625×18 | 16.8, 12.8 | font: "Inter Display"; size: 13px; lh: 18px; weight: 500; ws: pre-wrap; color: #FFFFFF |
|   └ Frame | `17UW-0` | 286.390625×30 | 6.8, 37.8 | align: center; radius: 8px; gap: 10px; paddingInline: 10px; paddingBlock: 6px; bg: #FFFFFF0F |
|     └ Frame | `17UX-0` | 242.390625×18 | 16.8, 43.8 | dir: column; flexGrow: 1; flexBasis: 0%; gap: 1px |
|       └ Frame | `17UY-0` | 242.390625×18 | 16.8, 43.8 | align: center; gap: 6px |
|         └ Sonnet 5.5 « Sonnet 5.5 » | `17UZ-0` | 63×18 | 16.8, 43.8 | font: "Inter Display"; size: 13px; lh: 18px; weight: 500; ws: pre-wrap; color: #FFFFFF |
|         └ Frame | `17V0-0` | 56×16 | 85.8, 44.8 | align: center; height: 16px; paddingInline: 5px; radius: 4px; bg: #FFFFFF14 |
|           └ Par défaut « Par défaut » | `17V1-0` | 46×12 | 90.8, 46.8 | font: "Inter Display"; size: 10px; lh: 12px; weight: 500; ws: pre-wrap; color: #FFFFFFB3 |
|     └ SVG | `17V2-0` | 14×14 | 269.2, 45.8 | SVG shrink: 0 |
|   └ Frame | `17V4-0` | 286.390625×30 | 6.8, 68.8 | align: center; radius: 8px; gap: 10px; paddingInline: 10px; paddingBlock: 6px |
|     └ Frame | `17V5-0` | 266.390625×18 | 16.8, 74.8 | dir: column; flexGrow: 1; flexBasis: 0%; gap: 1px |
|       └ Haiku 5.5 « Haiku 5.5 » | `17V6-0` | 266.390625×18 | 16.8, 74.8 | font: "Inter Display"; size: 13px; lh: 18px; weight: 500; ws: pre-wrap; color: #FFFFFF |
|   └ Rectangle | `17V7-0` | 266.390625×1 | 16.8, 103.8 | height: 1px; marginTop: 4px; marginRight: 10px; marginBottom: 4px; marginLeft: 10px; shrink: 0; bg: #FFFFFF14 |
|   └ Frame | `17V8-0` | 286.390625×30 | 6.8, 109.8 | align: center; radius: 8px; gap: 10px; paddingInline: 10px; paddingBlock: 6px |
|     └ Frame | `17V9-0` | 266.390625×18 | 16.8, 115.8 | dir: column; flexGrow: 1; flexBasis: 0%; gap: 1px |
|       └ 6.1 Sol « 6.1 Sol » | `17VA-0` | 266.390625×18 | 16.8, 115.8 | font: "Inter Display"; size: 13px; lh: 18px; weight: 500; ws: pre-wrap; color: #FFFFFF |
|   └ Frame | `17VB-0` | 286.390625×30 | 6.8, 140.8 | align: center; radius: 8px; gap: 10px; paddingInline: 10px; paddingBlock: 6px |
|     └ Frame | `17VC-0` | 266.390625×18 | 16.8, 146.8 | dir: column; flexGrow: 1; flexBasis: 0%; gap: 1px |
|       └ Frame | `17VD-0` | 266.390625×18 | 16.8, 146.8 | align: center; gap: 6px |
|         └ 6.1 Terra « 6.1 Terra » | `17VE-0` | 47×18 | 16.8, 146.8 | font: "Inter Display"; size: 13px; lh: 18px; weight: 500; ws: pre-wrap; color: #FFFFFF |
|   └ Frame | `17VF-0` | 286.390625×30 | 6.8, 171.8 | align: center; radius: 8px; gap: 10px; paddingInline: 10px; paddingBlock: 6px |
|     └ Frame | `17VG-0` | 266.390625×18 | 16.8, 177.8 | dir: column; flexGrow: 1; flexBasis: 0%; gap: 1px |
|       └ Frame | `17VH-0` | 266.390625×18 | 16.8, 177.8 | align: center; gap: 6px |
|         └ 6.1 Luna « 6.1 Luna » | `17VI-0` | 46×18 | 16.8, 177.8 | font: "Inter Display"; size: 13px; lh: 18px; weight: 500; ws: pre-wrap; color: #FFFFFF |
|   └ Rectangle | `17VJ-0` | 266.390625×1 | 16.8, 206.8 | height: 1px; marginTop: 4px; marginRight: 10px; marginBottom: 4px; marginLeft: 10px; shrink: 0; bg: #FFFFFF14 |
|   └ Effort | `17VK-0` | 286.390625×30 | 6.8, 212.8 | align: center; radius: 8px; gap: 8px; paddingInline: 10px; paddingBlock: 6px |
|     └ Autres modèles « Autres modèles » | `17VL-0` | 247.390625×18 | 16.8, 218.8 | font: "Inter Display"; size: 13px; lh: 18px; flexGrow: 1; flexBasis: 0%; weight: 500; ws: pre-wrap; color: #FFFFFF |
|     └ SVG | `17VM-0` | 11×11 | 272.2, 222.3 | SVG opacity: 0.5; rotate: -90deg; shrink: 0; transformOrigin: 50% 50% |
|   └ Effort | `17VO-0` | 286.390625×30 | 6.8, 243.8 | align: center; radius: 8px; gap: 8px; paddingInline: 10px; paddingBlock: 6px |
|     └ Effort « Effort » | `17VP-0` | 211.390625×18 | 16.8, 249.8 | font: "Inter Display"; size: 13px; lh: 18px; flexGrow: 1; flexBasis: 0%; weight: 500; ws: pre-wrap; color: #FFFFFF |
|     └ Élevé « Élevé » | `17VQ-0` | 28×16 | 236.2, 250.8 | font: "Inter Display"; size: 12px; lh: 16px; weight: 400; ws: pre-wrap; color: #FFFFFF99 |
|     └ SVG | `17VR-0` | 11×11 | 272.2, 253.3 | SVG opacity: 0.5; rotate: -90deg; shrink: 0; transformOrigin: 50% 50% |

### Menu 6.4 · effort

- Repère « racine » : `17VU-0` Frame — 214×167.59375 à (364, 10511.6) monde

| Élément | Node | L×H | x,y / racine | Styles Paper (tokens résolus) |
|---|---|---|---|---|
| Frame | `17VU-0` | 214×167.59375 | 0, 0.0 | width: 214px; dir: column; padding: 6px; radius: 12px; gap: 1; bg: #242424; borderWidth: 1px; borderStyle: solid; borderColor: #FFFFFF1A |
|   └ Frame | `17VV-0` | 200.390625×30 | 6.8, 6.8 | align: center; paddingBlock: 6px; paddingInline: 10px; radius: 8px; gap: 10px |
|     └ Frame | `17VW-0` | 180.390625×18 | 16.8, 12.8 | dir: column; flexGrow: 1; flexBasis: 0%; gap: 1px |
|       └ Faible « Faible » | `17VX-0` | 180.390625×18 | 16.8, 12.8 | font: "Inter Display"; size: 13px; lh: 18px; weight: 500; ws: pre-wrap; color: #FFFFFF |
|   └ Frame | `17VY-0` | 200.390625×30 | 6.8, 37.8 | align: center; paddingBlock: 6px; paddingInline: 10px; radius: 8px; gap: 10px |
|     └ Frame | `17VZ-0` | 180.390625×18 | 16.8, 43.8 | dir: column; flexGrow: 1; flexBasis: 0%; gap: 1px |
|       └ Moyen « Moyen » | `17W0-0` | 180.390625×18 | 16.8, 43.8 | font: "Inter Display"; size: 13px; lh: 18px; weight: 500; ws: pre-wrap; color: #FFFFFF |
|   └ Frame | `17W1-0` | 200.390625×30 | 6.8, 68.8 | align: center; paddingBlock: 6px; paddingInline: 10px; radius: 8px; gap: 10px; bg: #FFFFFF0F |
|     └ Frame | `17W2-0` | 156.390625×18 | 16.8, 74.8 | dir: column; flexGrow: 1; flexBasis: 0%; gap: 1px |
|       └ Élevé « Élevé » | `17W3-0` | 156.390625×18 | 16.8, 74.8 | font: "Inter Display"; size: 13px; lh: 18px; weight: 500; ws: pre-wrap; color: #FFFFFF |
|     └ SVG | `17W4-0` | 14×14 | 183.2, 76.8 | SVG shrink: 0 |
|   └ Frame | `17W6-0` | 200.390625×30 | 6.8, 99.8 | align: center; paddingBlock: 6px; paddingInline: 10px; radius: 8px; gap: 10px |
|     └ Frame | `17W7-0` | 180.390625×18 | 16.8, 105.8 | dir: column; flexGrow: 1; flexBasis: 0%; gap: 1px |
|       └ X-Élevé « X-Élevé » | `17W8-0` | 180.390625×18 | 16.8, 105.8 | font: "Inter Display"; size: 13px; lh: 18px; weight: 500; ws: pre-wrap; color: #FFFFFF |
|   └ Frame | `17W9-0` | 200.390625×30 | 6.8, 130.8 | align: center; paddingBlock: 6px; paddingInline: 10px; radius: 8px; gap: 10px |
|     └ Frame | `17WA-0` | 180.390625×18 | 16.8, 136.8 | dir: column; flexGrow: 1; flexBasis: 0%; gap: 1px |
|       └ Max « Max » | `17WB-0` | 180.390625×18 | 16.8, 136.8 | font: "Inter Display"; size: 13px; lh: 18px; weight: 500; ws: pre-wrap; color: #FFFFFF |

### Menu 6.4 · modes

- Repère « racine » : `14RK-0` Frame — 290×223.59375 à (602, 10511.6) monde

| Élément | Node | L×H | x,y / racine | Styles Paper (tokens résolus) |
|---|---|---|---|---|
| Frame | `14RK-0` | 290×223.59375 | 0, 0.0 | width: 290px; dir: column; padding: 6px; radius: 12px; gap: 2px; boxShadow: #00000080 0px 16px 48px; bg: #242424; borderWidth: 1px; borderStyle: solid; borderColor: #FFFFFF1A |
|   └ Frame | `14RT-0` | 276.390625×51 | 6.8, 6.8 | align: start; paddingBlock: 8px; paddingInline: 10px; radius: 8px; gap: 10px |
|     └ Frame | `17XF-0` | 16×18 | 16.8, 14.8 | align: center; paddingTop: 2px |
|       └ SVG | `17XD-0` | 16×16 | 16.8, 16.8 | SVG height: 16px; shrink: 0 |
|     └ Frame | `14RW-0` | 230.390625×35 | 42.8, 14.8 | dir: column; flexGrow: 1; flexBasis: 0%; gap: 1px |
|       └ Accept edits « Accept edits » | `14RX-0` | 230.390625×18 | 42.8, 14.8 | font: "Inter Display"; size: 13px; lh: 18px; weight: 500; ws: pre-wrap; color: #FFFFFF |
|       └ Automatically accept… « Automatically accept all file edits » | `14RY-0` | 230.390625×16 | 42.8, 33.8 | font: "Inter Display"; size: 12px; lh: 16px; weight: 400; ws: pre-wrap; color: #FFFFFF80 |
|   └ Frame | `1B81-0` | 276.390625×51 | 6.8, 59.8 | align: start; paddingBlock: 8px; paddingInline: 10px; radius: 8px; gap: 10px |
|     └ Frame | `1B8A-0` | 16×18 | 16.8, 67.8 | align: center; paddingTop: 2px |
|       └ SVG | `1B88-0` | 16×16 | 16.8, 69.8 | SVG height: 16px; shrink: 0 |
|     └ Frame | `1B82-0` | 230.390625×35 | 42.8, 67.8 | dir: column; flexGrow: 1; flexBasis: 0%; gap: 1px |
|       └ Auto « Auto » | `1B84-0` | 230.390625×18 | 42.8, 67.8 | font: "Inter Display"; size: 13px; lh: 18px; weight: 500; ws: pre-wrap; color: #FFFFFF |
|       └ Claude handles permi… « Claude handles permission decisions » | `1B83-0` | 230.390625×16 | 42.8, 86.8 | font: "Inter Display"; size: 12px; lh: 16px; weight: 400; ws: pre-wrap; color: #FFFFFF80 |
|   └ Frame | `14RL-0` | 276.390625×51 | 6.8, 112.8 | align: start; paddingBlock: 8px; paddingInline: 10px; radius: 8px; gap: 10px; bg: #FFFFFF0F |
|     └ SVG | `14RM-0` | 15×15 | 16.8, 122.8 | SVG shrink: 0; marginTop: 2px; opacity: 0.85 |
|     └ Frame | `14RO-0` | 207.390625×35 | 41.8, 120.8 | dir: column; flexGrow: 1; flexBasis: 0%; gap: 1px |
|       └ Demander « Demander » | `14RP-0` | 207.390625×18 | 41.8, 120.8 | font: "Inter Display"; size: 13px; lh: 18px; weight: 500; ws: pre-wrap; color: #FFFFFF |
|       └ Propose chaque modif… « Propose chaque modification. » | `14RQ-0` | 207.390625×16 | 41.8, 139.8 | font: "Inter Display"; size: 12px; lh: 16px; weight: 400; ws: pre-wrap; color: #FFFFFF80 |
|     └ SVG | `14RR-0` | 14×14 | 259.2, 122.8 | SVG shrink: 0; marginTop: 2px |
|   └ Frame | `14RZ-0` | 276.390625×51 | 6.8, 165.8 | align: start; paddingBlock: 8px; paddingInline: 10px; radius: 8px; gap: 10px |
|     └ SVG | `14S0-0` | 15×15 | 16.8, 175.8 | SVG shrink: 0; marginTop: 2px; opacity: 0.85 |
|     └ Frame | `14S2-0` | 231.390625×35 | 41.8, 173.8 | dir: column; flexGrow: 1; flexBasis: 0%; gap: 1px |
|       └ Planifier « Planifier » | `14S3-0` | 231.390625×18 | 41.8, 173.8 | font: "Inter Display"; size: 13px; lh: 18px; weight: 500; ws: pre-wrap; color: #FFFFFF |
|       └ Propose un plan, ne … « Propose un plan, ne touche à aucun fichier. » | `14S4-0` | 231.390625×16 | 41.8, 192.8 | font: "Inter Display"; size: 12px; lh: 16px; weight: 400; ws: pre-wrap; color: #FFFFFF80 |