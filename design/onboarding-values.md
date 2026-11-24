# Onboarding — values extracted from Paper

Source: Paper file « Browser Design » (`01M6Q7YT14ZKMGET54ASPNGQEC`), page `p-R-0` « Onboarding — fenêtre de lancement »,
boards « 1 » `1DNP-0`, « 2 » `1C5H-0`, « 3 » `1C8X-0`, « 4 » `1CBM-0`, « 5 » `1CD5-0`, « 5.1 » `1BKK-0`,
« 6 - Mode Annotation » `1BZ5-0`, « 7 » `1CEX-0`. Read with `get_jsx`, `get_computed_styles`, `get_tree_summary`
(never from screenshots). Code: `src/onboarding.h` (window), `src/onboarding-kit.h` (colours, type, glyphs),
`src/onboarding-shaders.h` (shaders). Coordinates below are DIP in the window, origin top-left; the boards'
content box starts at (1, 1) because of the 1 px border (board 6 has none: its content is one DIP up and left).

## Frame

| | value |
|---|---|
| size | 720 × 580, radius 12 (`--radius-browser-12`) |
| fill | `--color-browser-canvas` #171717; board 6: #000000 |
| border | 1 px #FFFFFF1A, inside; none on board 6 |
| tokens used | raised #292929, hover #333333, pressed #3E3E3E, text #FFFFFF, secondary #B3B3B3 (`--color-profile-gris`), danger #FF7B6E, accent #3E67FF, disabled 32 %, focus 10 % |

## Type (Inter Display)

| use | size / line | weight | tracking | colour |
|---|---|---|---|---|
| big title (boards 1, 6) | 32 / 38 | 500 | −0.035 em | #FFFFFF, centred |
| body under it | 14 / 20 | 400 | 0 | #B3B3B3, centred |
| header title (2 – 5.1) | 16 / 16 | 600 | −0.035 em | #FFFFFF, icon 16 at 50 % white, gap 4, centred on x 360, top 16.4 + 1 |
| labels, values, selects, checks, language | 14 / 20 | 500 | 0 | #B3B3B3 (labels) / #FFFFFF |
| buttons, boards 1 and 6 | 14 / 20 | 500 | 0 | #FFFFFF |
| buttons, boards 2 – 5.1 | 14 / 20 | 600 | 0 | #FFFFFF |
| error detail, « plus tard » note | 12 / 16 | 400 | 0 | #FF7B6E / #FFFFFF99 |
| board 6 illustration | 15/20 600 (« Payer »), 13/18 400 #FFFFFFE6 (tags), 14/20 400 #FFFFFFE6 (note), 12/16 500 / 400 (model, effort) | | | |

Text widths: every text node of the boards has an integer width (« Passer et naviguer » 115, « Commencer » 78,
« Retour » 43, « Continuer » 64, « Importer mes données » 143…): the code rounds measured widths up, and the
hugging boxes follow (checked to 0.01 DIP in `tests/onboarding/onboarding_render_test.cpp`).

## Header (all boards but 7)

| part | Paper | at |
|---|---|---|
| row | 48 high, padding 10 / 12 | controls at y 11 (board 6: 10) |
| language button | h 28, padding-inline 6, gap 4, radius 6: « Français » 53 × 20 + chevron 16 | x 13 (board 6: 12), width 85 |
| close button | 28 × 28, radius 6, cross 16 | x 679 (board 6: 680) |
| glyphs | 24 view box, stroke 1.8 round: chevron `m7 10 5 5 5-5`, cross `m7 7 10 10m0-10L7 17` | #B3B3B3; board 6: #474747 |

## Buttons bar

66 high, padding-inline 19, row 32 high at y 530 (board 6: 531), from x 20 to 700 (board 6: 19 to 701).
Button: h 32, radius 6, padding-inline 12. Secondary #292929; primary #3E67FF on board 1, #0099A0 after.
Gap 8 between « Retour » and « Passer »; board 5: one centred « Continuer »; board 4: right button opacity 0.

## Board 1 — Bienvenue

| part | value |
|---|---|
| HalftoneDots `1DNQ-0` | 721 × 508 at left −2, top 0 → (−1, 1); background #171717 |
| params | contrast 0.58, originalColors false, inverted, grid square, radius 1.19, size 0.67, scale 0.46, grainSize 0.39, type gooey, fit cover, grainOverlay 0.17, grainMixer 0.12, colorFront #FFFFFF, colorBack #00000000, image `01M6Z96QMKEC39VR0XYRQZ4MZB.png` (1080²) |
| veil `1DNR-0` | bottom 0, height 267 (y 312 → 579): linear-gradient(in oklab 179.87deg, canvas 0 % alpha at 0.3 %, canvas at 69.82 %) |
| text block | 153 high at the bottom: title y 426, body y 469 (gap 5), then 24, then the bar |
| copy | « Bienvenue dans Browser », « Personnalisez votre profil et retrouvez vos favoris. », « Passer et naviguer », « Commencer » |

## Board 2 — Créer votre profil

| part | value |
|---|---|
| mock `1C5I-0` | image `01M7ZGSD41…` (4720 × 1760 = 1180 × 440 @4x), 2521 × 940 at left 71, top 98 → (72, 99) |
| veil | height 276 (y 303 → 579), stops 0.29 % → 41.25 % |
| row `1C5U-0` | 288 high, bottom at y 497, align end, gap 12, padding-inline 31; content is 673 wide in 656 → starts at x 23.5 |
| Nom | label y 441; field 83 × 32 at (23.5, 465), #292929, radius 6, padding-inline 10 |
| dividers | 1 × 56, white 10 %, at x 118.5 and 283.5 |
| Icône | label at x 131.5; cells 32 × 32, radius 8, gap 4, glyph 16 #B3B3B3 (Phosphor Fill); selected: fill #14353A, border 1 px #00D1DA, glyph #00D1DA |
| strip fade `1C6H-0` | 53 wide on the right: linear-gradient(in oklab 89.68deg, #17171700 19.92 %, #171717 80.09 %) |
| sliders | rows 24 high at y 445 and 473; label lane 66 at x 296.5; track 282 × 10, radius 5, at x 370.5; value lane 36 at x 660.5, right-aligned |
| Teinte track | linear-gradient(in oklab 90deg, #FF8FB4, #F8A052 12.5 %, #DAB33A 25 %, #6FD087 37.5 %, #0FD4BB 50 %, #61BFFF 62.5 %, #A59AFF 75 %, #E995E2 87.5 %, #FF8FB4) |
| Intensité track | linear-gradient(in oklab 90deg, #B7B7B7, #00E1EA) |
| thumb | 4 × 20, radius 2, #FFFFFF, box-shadow 0 0 0 1 px #101010; at round(55.1 %) of the track for 200° |
| sample values | « Alex », heart, 200°, 88 % |
| copy | « Créer votre profil », « Nom », « Icône », « Teinte », « Intensité », « Retour », « Continuer » |

## Board 3 — Importer vos données

| part | value |
|---|---|
| mock | same image, 1383 × 516 at left 17, top 47 → (18, 48) |
| veil | height 252 (y 327 → 579), stops 0.29 % → 41.25 % |
| selects | labels y 417, boxes y 441; « Navigateur » 228 × 32 at x 160, « Profil Google Chrome » 160 × 32 at x 400 (gap 12, centred); #292929, radius 6, padding-inline 10; logo 14, gap 6; chevron 16 #B3B3B3 |
| checks | row y 489, h 24, centred, gap 20: box 16 × 16 radius 3 #0099A0, check 14 (stroke 1.8 white, `m5 12 4 4L19 6`), gap 7 |
| copy | « Importer vos données », « Navigateur », « Profil Google Chrome », « Favoris et dossiers », « Historique », « Retour », « Passer », « Importer mes données » |

## Boards 4, 5, 5.1 — import running, done, failed

| part | value |
|---|---|
| mock | image `01M7ZJ5TN1…` (with the bookmarks bar, 5532 × 2064), same box as board 3 |
| status | 14 / 20 500 #B3B3B3 centred at y 436: « Import en cours... », « Import terminé ! » |
| row `1CC4-0` | 387 × 57 at (166.5, 456), gap 12: logo box 51 (image 42), 19 ticks 2 × 16 radius full, GemSmoke 58 × 57 |
| ticks, board 4 | 7 lit #04D1DA, 12 at #DDDDDD 10 % |
| ticks, boards 5 / 5.1 | all #04D1DA / #FF7B6E with filter blur(4px), opacity 0.2 |
| mark | 32 × 32 centred on the row: Phosphor check bold #04D1DA / warning bold #FF7B6E |
| GemSmoke `1CCQ-0` | speed 1, size 0.8, outerDistortion 0.6, innerDistortion 0.8, outerGlow 0, innerGlow 1, offset −0.58, scale 0.65, angle 0, shape diamond, colors #333333 #E7E6DF, colorInner #FAFAF5, colorBack transparent, image `01M6Z8XPRA….svg`, CSS filter contrast(108%) |
| board 5.1 text | title y 418 « Historique non importé » 14/20 500 #FF7B6E; gap 2; « Google Chrome est ouvert. Fermez-le, puis réessayez. » 12/16 #FF7B6E |
| board 5.1 note | chip after « Passer » (gap 4): canvas fill, radius 8, padding 6 / 8, gap 4, info glyph 14 at 60 %, « Vous pouvez l'importer plus tard dans les Réglages. » 12/16 #FFFFFF99 |
| buttons | 4: « Annuler »; 5: « Continuer » (centred); 5.1: « Passer », « Réessayer » |

## Board 6 — Mode Annotation

| part | value |
|---|---|
| GrainGradient `1CJ9-0` | 725 × 407 centred at (50 % + 0.5, 50 % − 88.5) → (−2, −2); background #DEFDFF; speed 1, scale 1, softness 0.7, intensity 0.15, noise 0, shape wave, colors #0AF4FF #0099A0 #01292C, colorBack transparent |
| veil `1BZ7-0` | bottom 1, height 330 (y 249 → 579): linear-gradient(in oklab 179.87deg, #00000000 0.3 %, #000000 47.2 %) |
| text | title y 415 « Mode Annotation »; body y 458, two lines: « Cliquez sur un élément de votre projet et décrivez le changement. » / « Votre agent modifie le code, et la page se met à jour aussitôt sans quitter Browser. » |
| illustration `1CHL-0` | 557 × 124 at (81, 155): button 240 × 44 #1C1C1C radius 8 at +24; selection frame 248 × 52 at (−4, +20), border 2 px #0099A0, fill #3E67FF14; tags h 20, padding-inline 6, #0099A0, top radius 3 (« button.btn-primary », « 240x64 »); crosshair 32 at (196, 30); note card 305 wide at (252, 20), #171717, radius 16, padding 12, gap 12; send button 28 × 28 #0099A0 radius 8 |
| copy | « Pas intéressé(e) », « Activer », « Payer 48,00 € », « Agrandis légèrement le bouton au survol et enfonce-le un peu au clic », « Opus 5.5 », « Moyen » |

## Board 7

HalftoneDots `1CEY-0`, same params as board 1, 721 × 579 at (−1, 1); nothing else.

## Colours taken from the profile

The boards are drawn for the sample profile (200°, 88 %). The window derives them so that this profile gives the
boards' literal values and any other profile gives its own:

| board value | rule (`src/onboarding-kit.h`) |
|---|---|
| #0099A0 (buttons, checks, annotation frame) | OKLCH L 0.62 at the profile's hue and chroma |
| #00D1DA / #04D1DA (selected icon, ticks, check) | the profile ink `ProfileColor::Ink` (gives #00D0D9) |
| #14353A (selected icon plate) | ink at 16 % over the canvas (gives #133536) |
| #00E1EA (Intensité track end) | OKLCH L 0.827 at the hue's largest chroma |
| #0AF4FF #0099A0 #01292C #DEFDFF (board 6) | same lightness and chroma, hue turned by (profile hue − 200) |

## Where the code cannot be the board, and why

| difference | why |
|---|---|
| Text reads lighter than on the boards (ink mass ≈ 0.8 – 0.95 of Paper's; same font files, sizes, weights, widths and baselines) | The boards are rasterised by a web engine, the window by DirectWrite. Gamma 2.2 is the closest setting (`tests/onboarding/compare.py`). |
| Ink #00D0D9 for #00D1DA / #04D1DA; selected plate #133536 for #14353A | Derived from the profile model so they follow any hue; one to four steps of 255 away at the sample profile. |
| Profile chip of the browser mock redrawn (ink #00D0D9, plate ink 15 % over canvas) instead of the image's #5FCED8 on #223334 | The mock is a picture; the chip has to show the icon and colour being chosen. |
| Source logo: the installed browser's own icon, not the Paper image | Any browser can be the source; no third-party logo is bundled. |
| Fourth icon of the strip is the graduation cap as on the board; the registry order (users-three third) is kept after the first four | Board order wins for what the board shows. |
| Animated shaders of boards 4 – 6 never match a Paper screenshot pixel for pixel | Paper captures them at an arbitrary instant; the still frame (reduced motion, tests) is aligned on the captures kept in `design/onboarding-evidence/paper/`. |
| Hairline: 1 device pixel at every scale (Paper: 1 CSS px) | Convention of the browser's other popups (`PopupKit::Outline`). |
| Fractional positions (23.5, 131.5, 370.5…) start on a device pixel | As the engine that drew the boards does (23.5 → 24 at 100 %). |

Evidence: `design/onboarding-evidence/` — `paper/` (Paper screenshots), `renders/` (off-screen renders at 96 and
120 DPI), `pairs/` (Paper | render | difference × 4), `compare-96.txt` / `compare-120.txt` (numbers), `shaders/`
(the three shaders alone), `real/` (real window at 125 %, `tests/onboarding/qa.py`).
