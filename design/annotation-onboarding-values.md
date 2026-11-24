# Annotation mode onboarding (#125, variant A) — Paper values

Source: Paper « Browser Design » (`01M6Q7YT14ZKMGET54ASPNGQEC`) › page « MCP - Design UI UX » (`p-H-0`), row 1 under
« #125 · Onboarding du mode Annotation — 3 propositions ». Read with `get_jsx` (CSS of every node) — never from a
screenshot. Code: `src/annotation-onboarding.h` (window), `src/annotation-onboarding-flow.h` (flow, first-time rule),
`src/onboarding-parts.h` (the pieces shared with the first-run onboarding, `src/onboarding.h`), host
`src/annotation-mode.h`. Tests: `tests/onboarding/annotation_onboarding_test.cpp`, audit `tests/onboarding/annotation_compare.py`.

| Board | Node | Title |
| --- | --- | --- |
| A1 | `1MLX-0` | « Proposition — Onboarding annotation · A1 À quoi ça sert » |
| A2 | `1MNM-0` | « · A2 Où ça marche » |
| A3 | `1MPB-0` | « · A3 Connecter » |
| A4 | `1MR0-0` | « · A4 Prêt » |

Variants B (`1MUR-0`, `1MWL-0`) and C (`1N0F-0` …) are not coded.

## Common to the four boards (= board 6 of the first-run onboarding)

| Part | Value |
| --- | --- |
| window | 720 × 580, radius 12, `#000000`, no border |
| GrainGradient (`1MNL-0`, `1MPA-0`, `1MQZ-0`, `1MSO-0`) | 725 × 407 at (−2, −2), background `#DEFDFF`; speed 1, scale 1, softness 0.7, intensity 0.15, noise 0, shape wave, colors `#0AF4FF` `#0099A0` `#01292C`, back transparent — same node as board 6 `1CJ9-0`; board 6's dither on top (its tuning defaults) |
| veil (`1MNK-0` …) | bottom 1, height 330, left −2, right −7 → y 249 … 579: `linear-gradient(in oklab 179.87deg, #00000000 0.3%, #000000 47.2%)` |
| header (`1MNB-0`) | 48 high, padding 10 / 12: « Français » 14/20 500 `#474747` + chevron 16 (`m7 10 5 5 5-5`, stroke 1.8) in a 28-high box, padding-inline 6, gap 4, radius 6, at (12, 10); close 28 × 28 radius 6, cross 16 (`m7 7 10 10m0-10L7 17`) at (680, 10) |
| text block (`1MN8-0`) | column, gap 5, its bottom at y 498 (16 above the footer): title 32/38 500, letter-spacing −0.035 em, white, centred; body 14/20 400 `#B3B3B3` centred, one line (A1) or two (`<br>`: A2 – A4) → title at y 435 or 415 |
| footer (`1MMZ-0`) | 66 high at y 514, padding-inline 19: secondary at x 19, primary right-aligned at x 701, y 531 |
| buttons | h 32, radius 6, padding-inline 12, 14/20 500 white; secondary `--color-browser-raised` `#292929`, primary `#0099A0` |
| progress (`1MSP-0`) | absolute, centred on (360, 547): 4 pills h 6 radius 3, gap 6; the current one 16 wide `#FFFFFF`, the others 6 wide `#FFFFFF40` |

Labels: A1 « Plus tard » / « Suivant »; A2 « Retour » / « Suivant »; A3 « Retour » / « Continuer »; A4 « Retour » / « Annoter ma page ».

## A1 « À quoi ça sert »

Title « Bienvenue sur le mode Annotation », body « Cliquez sur un élément de votre projet et décrivez le changement. »
(Val's final text). Illustration `1MLY-0` 557 × 124 at (81, 155): exactly board 6's `1CHL-0` (button 240 × 44 `#1C1C1C`
radius 8 « Payer 48,00 € » 15/20 600; frame 248 × 52 at (−4, 20) border 2 `#0099A0` fill `#3E67FF14`; tags h 20
padding-inline 6 `#0099A0` top radius 3, 13/18 `#FFFFFFE6` « button.btn-primary » / « 240x64 »; crosshair 32 at (196, 30);
note 305 wide at (252, 20) `#171717` radius 16 padding 12 gap 12, 14/20 `#FFFFFFE6`, footer row: plus 14 at 50 %, shield 13,
Claude 14 `#D97757` + « Opus 5.5 » 12/16 500 `#FFFFFFE6` + « Moyen » 12/16 `#FFFFFF80`, send 28 × 28 `#0099A0` radius 8).
Drawn by the same function (`OnboardingParts::AnnotationDemo`).

## A2 « Où ça marche »

Title « Sur vos projets en cours »; body « Le mode Annotation marche sur les sites qui tournent sur votre ordinateur, /
comme localhost:5173. Sur les autres sites, il reste éteint. »

| Node | Value |
| --- | --- |
| `1MTD-0` | column at (120, 104), 480 wide, gap 14 |
| `1MTE-0` local row | h 48, `#171717`, radius 12, padding-left 16, right 8, gap 10 |
| `1MTF-0` | 16/20 400, −0.01 em: « localhost » `#FFFFFF` + « :5173 » `#FFFFFF80`; grows |
| `1MTG-0` button | h 32, radius 8, padding-inline 10, gap 6, `#0099A0`: crosshair 14 (32 view box, stroke 3 white, round caps) + « Annoter » 13/16 500 white |
| `1MTK-0` web row | same, `#171717E6`: « lemonde.fr » `#FFFFFF99` + « /planete » `#FFFFFF4D`; button `#FFFFFF0F`, glyph and label `#FFFFFF52` |
| `1MTQ-0` tooltip | align end (right at x 600), `#171717`, border 1 `#FFFFFF14`, radius 8, padding 6 / 10, gap 2, max-width 280: 13/18 `#FFFFFFE6` « Seulement sur vos projets locaux », 13/18 `#FFFFFF80` « Lancez votre projet, puis ouvrez localhost. » |

## A3 « Connecter »

Title « Connectez votre agent »; body « Browser règle Claude ou Codex pour vous, en un clic. / Vos notes et captures
restent sur cet ordinateur. »

| Node | Value |
| --- | --- |
| `1MTU-0` card | at (180, 112), 360 wide, `#171717`, radius 16, padding 12, column gap 8 |
| `1MTV-0` connected | h 40, border 1 `#FFFFFF14`, radius 8, centred, gap 6: Claude logo 16 (view box 0.667 1.6 21.333, `#D97757`) + « Claude est connecté » 14/18 500 white + check 14 (256 view box) `#2FD1D9` |
| `1MU1-0` to connect | h 40, `#292929`, radius 8, gap 6: « Connecter avec Codex » 14/18 500 white + OpenAI logo 16 (view box 0 1.6 21.333) white |
| `1MU5-0` | padding-top 4, centred: « Autre app ou réglage manuel » 12/16 `#FFFFFF80` |

States Paper does not draw take the « Connecter un agent » modal's (`src/annotation-connect.h`, Paper 1.2 – 1.4) at this
row's size: en cours `#FFFFFF0A` + border `#FFFFFF14`, turning ring 14 `#FFFFFFB3` + label `#FFFFFFCC`; échec raised +
border `#FF7B6E66`, « Réessayer avec … », message 12/16 `#FF7B6E` under the row (warning-circle 13); connected with a note
(« Redémarrez Claude pour l'activer ») 12/16 `#FFFFFF99` under the row. Hover of a row: `#292929` → `#333333`, pressed
`#3E3E3E` (the footer's buttons). See « Open points » in the PR.

## A4 « Prêt »

Title « Tout est prêt »; body « Sur votre projet, appuyez sur Ctrl + Maj + A, cliquez un élément / et écrivez ce qui
doit changer. Claude s'occupe du reste. » (the connected agent's name).

| Node | Value |
| --- | --- |
| `1MUD-0` | column, left 0 right 0, top 120, centred, gap 20 |
| `1MUE-0` keys | row, gap 10, centred |
| `1MUF-0` / `1MUI-0` keys | h 56, `#171717`, border-bottom 3 `#0B0B0B`, radius 12 (inner bottom corners 12 × 9), padding-inline 18, 20/24 500 white « Ctrl », « Maj » |
| `1MUL-0` key « A » | 56 × 56, same |
| `1MUH-0` / `1MUK-0` | « + » 18/24 500 `#0B3B3D` |
| `1MUN-0` status | h 32, `#171717`, radius 16, padding-inline 12, gap 8: dot 8 × 8 radius 4 `#2FD1D9` + « Claude est connecté » 13/16 500 `#FFFFFFE6` |

## Measuring against Paper

`get_jsx` gives the CSS; the exports (`design/annotation-onboarding-evidence/paper/A*@96.png`, `@120.png` = 1x and 1.25x)
are tagged « Display P3 Gamut with sRGB Transfer » (`#0099A0` is stored as 67, 151, 158): `annotation_compare.py` converts
them to sRGB first. They are also rasterised about half a pixel below and right of the CSS boxes (every edge of an opaque
box is a half-covered row); the code keeps the CSS boxes on whole pixels. Measured adjustment: the key caps' labels sit one
row lower than a line box at 134.5 (see `DrawKey`). Text weight: the first-run onboarding's DirectWrite parameters
(`OnboardingParts::TextLook`).
