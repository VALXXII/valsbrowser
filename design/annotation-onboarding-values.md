# Annotation mode tutorial (#125 v2) — Paper values

Source: Paper « Browser Design » (`01M6Q7YT14ZKMGET54ASPNGQEC`) › page « #125 v2 — Tutoriel annotation (validé) » (`p-Y-0`),
validated by Val on 2026-11-24. Read with `get_jsx` / `get_computed_styles` (CSS of every node) — never from a screenshot.

Code: `src/annotation-onboarding.h` (window), `src/annotation-onboarding-flow.h` (flow, trial, first-time rule),
`src/annotation-local-card.h` (E1b), `src/onboarding-parts.h` (pieces shared with the first-run onboarding, `src/onboarding.h`,
whose board 6 is E2), `src/annotation-sites.h` (where « Annoter » is on), host `src/annotation-mode.h`.
Tests: `tests/onboarding/annotation_onboarding_test.cpp` (offscreen renders + behaviour), `tests/onboarding/onboarding_render_test.cpp`
(E2 = board 6), `tests/annotations/annotation_sites_test.cpp`, `tests/annotations/annotation_private_test.cpp`;
audit `tests/onboarding/annotation_compare.py` (Paper exports in `design/annotation-onboarding-evidence/paper-v2/`).

| Board | Node | | Board | Node |
| --- | --- | --- | --- | --- |
| A1 Bienvenue | `1RTD-0` | | A5 Où ça marche | `1S3J-0` |
| A2 Essai : choisir | `1RVB-0` (violet `1SAY-0`) | | A6 Connecter | `1S4U-0` |
| A3 Essai : écrire | `1RZ5-0` (violet `1S7Y-0`) | | A7 Prêt | `1S62-0` |
| A4 Essai : résultat | `1RX8-0` (violet `1SDI-0`) | | E1a / E1b / E2 | `1TX1-0` / `1STD-0` / `1SWL-0` |

## Common to the seven steps (= board 6 of the first-run onboarding)

| Part | Value |
| --- | --- |
| window | 720 × 580, radius 12, `#000000`, no border |
| GrainGradient | 725 × 407 at (−2, −2), background `#DEFDFF`; speed 1, scale 1, softness 0.7, intensity 0.15, noise 0, shape wave, colors `#0AF4FF` `#0099A0` `#01292C`; board 6's dither on top (Val kept it) |
| veil | y 249 … 579: `linear-gradient(in oklab 179.87deg, #00000000 0.3%, #000000 47.2%)` |
| header | « Français » 14/20 500 `#474747` + chevron 16 at (12, 10), close 28 at (680, 10) |
| text block | bottom at y 498: title 32/38 500 −0.035 em white, gap 5, body 14/20 `#B3B3B3`, centred (one or two lines) |
| footer | 66 high at y 514, padding-inline 19; buttons h 32 radius 6 padding-inline 12, 14/20 500 white, y 531: secondary `#292929` at x 19, primary accent right-aligned at 701; A2 and A3: « Passer l'essai » text button `#FFFFFFB3` (no fill; white on hover — Val's default #7) |
| progress | 7 pills h 6 radius 3, gap 6, centred on (360, 547): current 16 wide `#FFFFFF`, others 6 `#FFFFFF40` |

Labels: A1 « Plus tard » / « Suivant »; A2, A3 « Retour » / « Passer l'essai »; A4 « Recommencer » / « Suivant »;
A5 « Retour » / « Suivant »; A6 « Retour » / « Continuer »; A7 « Retour » / « Terminer ».

Colours follow the profile like the annotation mode (Val): the boards are drawn for the sample profile (200° / 88 %); the
gradient and the inks (`#2FD1D9`, `#0AF4FF`, `#007D83`) move with `OnboardingKit::Rehue`, the fills (`#0099A0`) with
`OnboardingKit::Accent` — board 6's own formulas. « Personnel » (no colour): the mode's blue `#3E67FF` for the fills, the
gradient turned to its hue (Val, answer 8).

## A2 – A4: the trial on a fake page

| Node | Value |
| --- | --- |
| page `1S12-0` | 420 × 236 at (150, 64) — (110, 64) on A3 —, `#F4F3F1`, radius 10, clipped |
| header `1S13-0` | 36, `#FFFFFF`, border-bottom 1 `#00000012`, padding 16: « Boutique » 13/16 600 `#1C1C1C`, « Site d'exemple » 11/14 `#00000073` |
| body `1S16-0` | padding 16, gap 10: « Paiement » 18/22 600 −0.02 em `#1C1C1C`; two fields h 30 `#FFFFFF`, border 1 `#00000014`, radius 6, padding 10, 12/16 `#0000004D`; « Payer 48,00 € » 160 × 36 `#1C1C1C` radius 8, 13/16 600 white |
| A2 frame `1S1F-0` | 168 × 44 at (162, 224) = the button 4 out, border 2 accent, fill accent 8 %, radius 2; tag `1S1G-0` h 18 above it, padding 6, accent, top radius 3, « button » 12/16 `#FFFFFFE6` |
| A2 pointer `1S1I-0` | the mode's crosshair 32 (white 4, `#1C1C1C` 1.6, accent dot r 2.6 ringed 1.2 white) |
| A2 coach `1S1M-0` | at (350, 228): h 36, radius 18, `#171717`, padding 8 / 12, gap 8: disc 20 accent « 1 » 11/12 600 white, « Cliquez sur « Payer » » 13/16 500 white |
| A3 frame `1S2H-0` | the button framed (no radius) + « button.btn-primary » |
| A3 note `1S2K-0` | board 6's note at (304, 196), 305 wide: « Rends ce bouton plus large » 14/20 `#FFFFFFE6` + caret « \| » `#0AF4FF` |
| A4 button | 260 wide; measure `1S2…` at (166, 270) 260 × 16: lines 1.5 accent, « 160 → 260 » 11/14 600 `#007D83`, gap 6; « Fait » pill at (438, 228) (disc with check 12) |

Behaviour (Val's defaults 3 – 6, confirmed): every element of the page is framed with its tag under the pointer (header,
h2, input, button) and the Windows pointer becomes the drawn crosshair over the page; only « Payer » picks; A3's note is
typed for real (hint « Rends ce bouton plus large » `#FFFFFF99`, « Envoyer » dimmed to 32 % until something is typed, any
text works); A4: 160 → 260 in 400 ms, then « Fait » and the measure fade in over 200 ms; « Recommencer » back to A2; between
trial steps the page stays and glides (A3 sits 40 to the left).

## A5 – A7

A5 `1S3K-0` at (120, 84), 480 wide, gap 14: rows h 48 radius 12 padding 16 / 8 (« localhost:5173 », « Fichier  …/boutique/index.html »
with « Fichier » and « …/boutique/ » at 50 %, « lemonde.fr/planete » off at `#171717E6`); buttons h 32 radius 8 accent or
`#FFFFFF0F`; tooltip « Seulement sur vos projets et fichiers locaux » / « Ouvrez votre projet en local ou un fichier. ».
A6 = v1 A3 (card at (180, 112), rows 40, states of the « Connecter un agent » modal). A7 = v1 A4 (keys, status pill), « Terminer ».

## E1a / E1b / E2

* E1a `1TX2-0`: the toolbar button greyed (glyph at 32 %, no hover), tooltip « Mode Annotation - seulement sur vos projets et
  fichiers locaux » (Val's answer 1).
* E1b `1STF-0`: 316 × 178, `#171717`, border 1 `#FFFFFF14`, radius 12, padding 16, gap 12; title 16/20 600 −0.01 em white; gap 4;
  body 14/20 `#B3B3B3` with the host 500 white; « Compris » raised / « Découvrir en 1 minute » accent, h 28, radius 6,
  padding 12, 13/16 500. At (744, 80) for the button 1053 … 1077 × 48 … 72: 8 below the button, right edge 5 left of its centre.
* E2 `1SWL-0`: board 6 with « Mode Annotation », « Pour les sites que vous construisez : cliquez sur un élément et décrivez le
  changement. » / « Marche sur vos projets en local. Essayez sur une page d'exemple, ça prend une minute. », « Plus tard »
  (79 wide) and « Essayer en 1 minute » (146 wide), no progress.

## Measuring against Paper

The exports are « Display P3 Gamut with sRGB Transfer » (`#0099A0` is stored as 67, 151, 158): `annotation_compare.py`
converts them to sRGB. They are rasterised about half a pixel below and right of the CSS boxes; the code keeps the CSS
boxes on whole pixels. Measured adjustment: the key caps' labels one row lower than a line box at 134.5 (`DrawKey`).
Text weight: the first-run onboarding's DirectWrite parameters. The violet boards use a « hue » blend over the cyan
gradient and a fill `#7A5CE0` (OKLCH chroma 0.192, above any profile's 0.16): the code applies the onboarding's formulas to
a violet profile instead, so the violet renders are close but not identical (see `compare-96.txt`).
