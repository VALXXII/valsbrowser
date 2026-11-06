# Tab spacing, close slot, focus ring and authored Lottie icons

Author: Claude Opus 5.5 (`claude-opus-5-5`), task `task_a4609f5acd98`, baseline `4cd42ce`.
Scope: `src/design-system.h` tokens, `assets/lottie/*`, `tests/design-system/geometry_test.cpp`,
`tests/lottie-assets/*`. The renderer (`src/chrome.h`, `src/main.cpp`, `src/native-lottie.*`)
belongs to the integration peer. This document is the adapter contract and the Paper sync source.

## 1. Spacing, derived from the code (not from the screenshot)

| Measure | Before (4cd42ce) | After |
| --- | --- | --- |
| Toolbar gap, back `{6,38}` to forward `{44,76}` | 6 DIP | 6 DIP (the reference) |
| Visible tab-to-tab gap = `tabPitch - tab` | 140 - 128 = **12 DIP** | 134 - 128 = **6 DIP** |
| Last tab to new-tab button | 6 DIP | 6 DIP, now through `NewTabLeft` |
| Tab hit regions | contiguous 140 | contiguous 134, no dead zone between tabs |
| Overflow capacity | `(w-118-51-32)/140` | `TabStrip::Capacity(w) = (w-118-51-6-32)/134` |

### Changed and new tokens (`src/design-system.h`)

| Token | Value | Note |
| --- | --- | --- |
| `Space::controlGap` | 6 (`= Space::sm`) | new, the one gap between chrome controls |
| `Space::tabGap` | 6 (`= controlGap`) | new |
| `Size::tabPitch` | **134** (`= tab + tabGap`), was 140 | changed |
| `Size::closeInset` | 4 (`= (control - closeSlot)/2`) | new |
| `Size::focusStroke` | **1**, was 2 | changed |
| `Focus::color` / `Focus::opacity` / `Focus::stroke` | 1 (white) / **0.10** / 1 | new, the user's own choice |
| `Surface::focus` | 190/255 | **legacy**, delete once `chrome.h` uses `Focus::` |
| `Box`, `TabStrip::{left=51, regionTop=2, regionBottom=40, top=6, bottom=38, trailingReserve=118}` | | new |
| `TabStrip::Region / Surface / CloseSlot / FaviconSlot / TitleSlot / NewTabLeft / Capacity` | | new pure helpers |

Static asserts lock the gap to 6, the close slot to vertical centring and the focus stroke to 1.

### Component geometry (DIP, slot `i`, first tab `i = 0`)

```
Region(i)    = {51 + 134i, 2, 51 + 134(i+1), 40}          hit area, contiguous
Surface(i)   = {57 + 134i, 6, 185 + 134i, 38}             128 x 32 visible tab, radius 6
FaviconSlot  = {tab.left + 8, 14, tab.left + 24, 30}       16 x 16
TitleSlot    = {tab.left + 32, 6, tab.right - 32, 38}      constant, never shifts with hover
CloseSlot    = {tab.right - 28, 10, tab.right - 4, 34}     24 x 24, was {right-24,10,right,34}
NewTabLeft(n)= 51 + 134n + 6                               32 x 32 button at y 6..38
```

Tab close: the slot is inset 4 DIP from the trailing, top and bottom edges of the tab, so its padding is
equal on every side. The 16 DIP Phosphor Bold `x` is centred in it, and its glyph box sits 8 DIP from the
tab's trailing edge and 8 DIP from its top and bottom. That mirrors the favicon's 8 DIP leading inset. The
Phosphor `x` ink spans 44 to 212 of 256 in both axes, so its padding is visually equal too. `TabClose`
keeps `fill = false`, so the close hover adds no second surface and the tab stays the only fill. The title
slot always reserves the close slot, so text never shifts. The title loses 4 DIP of width, from
`right-28` to `right-32`.

Keyboard focus: a 1 DIP stroke in white at 10% alpha, inset 0.5 DIP so it lands on whole device pixels at
100%. It is the only focus indicator, with no extra high-contrast ring. The user chose this over the
skill's default recommendation. Keyboard order and access are unchanged: tab body, then its close button.

### Peer adapter (`chrome.h`, owned by the integration peer)

1. In `Build()`, set `capacity = Design::TabStrip::Capacity(width)`. For each shown tab `i`, set the
   tab region to `Region(i)` and the close region to `CloseSlot(Surface(i))`. Set the new-tab x to
   `NewTabLeft(shown)`. Without that last change, the new-tab button touches the last tab with a 0 gap.
2. In `Paint()`, use `tab = Surface(i)`. The favicon goes in `FaviconSlot(tab)` and the title in
   `TitleSlot(tab)`. The close icon, the close press scale and the close focus ring all use `CloseSlot(tab)`.
3. In `FocusRing()`, call `Color(Design::Focus::color, Design::Focus::opacity)` with stroke
   `Design::Focus::stroke` and inset `stroke / 2`. Then delete `Surface::focus`.
4. In `tests/design-system/design_system_test.cpp`, the tab probe pixels now come from
   `TabStrip::Surface(n).right - 2`. That replaces the hard-coded 183 and 323. Pixel 323 would fall in
   the gap at pitch 134.

## 2. Lottie assets

| File | Comp | fps | Frames | Markers |
| --- | --- | --- | --- | --- |
| `assets/lottie/copy-link.json` | 256 x 256 | 30 | ip 0, op 20 (667 ms) | `toCheck` 0 to 10, `toLink` 10 to 20 |
| `assets/lottie/refresh.json` | 256 x 256 | 30 | ip 0, op 15 (500 ms) | `sweep` 0 to 15 |

Each file is Bodymovin 5.7 schema with one shape layer (`ty 4`). It uses only `gr / sh / st / tm / tr`.
There are no masks, mattes, effects, blur, expressions, images or fonts. The files are 3.3 kB and 2.1 kB.
The 256 grid is the Phosphor grid, so path coordinates equal the icon source. The host scales to 16 DIP × DPI.

**Ink contract:** the strokes are opaque `#FFFFFF`. The host must composite the rendered bitmap at
`0.5 × ButtonStyle.iconOpacity`, which is the 0.5 baked into the shipped Phosphor PNGs. Opacity is not
baked into the asset, because overlapping strokes would double their alpha where the link halves cross
and where the arrow head meets the arc.

### Source geometry (Phosphor Bold 2.1.1, MIT)

Phosphor Bold ships filled outlines of a 24-unit stroke with round caps and joins. I recovered the
centerlines from those outlines: cap centres sit at the midpoints of the 12 r cap arcs, and the arc radius
is the mean of the outer and inner radii. `tests/lottie-assets/generate.py` emits them, and the reference
SVGs are in `assets/lottie/source/`:

- `link.svg` has two halves related by 180° rotation about (128,128). Half A has 7 anchors:
  `M108.68,197.25 L103.57,202.37`, then a semicircle with r 46.63 about (70.6,169.4) to `37.63,136.43`,
  then `L72.4,101.68`, then an arc with r 46.63 about (105.37,134.65) to `150.77,124.0`. That last arc
  is 2 cubics.
- `check-7pt.svg` is the Phosphor check `40,144 → 96,200 → 224,72`, resampled to the same **7-anchor
  open topology** as half A. That makes the morph a pure per-vertex interpolation.
- `refresh.svg` is `arrow-counter-clockwise`. It has an r 88 arc about (128,128) from the tail cap
  `67.61,192.01`, running counter-clockwise on screen through the right side to `65.7,65.85`, then
  `L24,104`. That is 3 cubics plus a line. The head is `M72,104 L24,104 L24,56`.

Every stroke is 24 units with `lc 2` and `lj 2` (round), so endpoints and weight match Phosphor Bold
exactly. The rest frames match the shipped Phosphor PNGs at 16, 32 and 64 px, with a mean ink difference
of 0.012 to 0.041 against a threshold of 0.08.

### Motion timelines

Easings come from the generator: `standard = cubic-bezier(0.2, 0, 0, 1)` (better-ui) and
`inOut = cubic-bezier(0.45, 0, 0.2, 1)`.

**copy-link**: link to check after a successful copy, then back.

| Frames | ms | Property | Values | Ease |
| --- | --- | --- | --- | --- |
| 0 → 9 | 0 → 300 | Half A `sh` morph | link-A → check (7 anchors) | standard |
| 0 → 6 | 0 → 200 | Half B trim end | 100 → 0 (retracts into its cap) | standard |
| 2 → 5 | 67 → 167 | Half B group opacity | 100 → 0 (no zero-length cap dot) | standard |
| 9 → 10 | | hold | check (rest of `toCheck`) | |
| 10 → 19 | 333 → 633 | Half A morph | check → link-A | standard |
| 12 → 19 | | Half B trim end | 0 → 100 | standard |
| 14 → 17 | | Half B opacity | 0 → 100 | standard |
| 19, 20 | | rest | identical to frame 0 (verified pixel-exact) | |

**refresh**: one sweep in the arrow's own direction (counter-clockwise) that lands back on rest.

| Frames | ms | Property | Values | Ease |
| --- | --- | --- | --- | --- |
| 0 → 15 | 0 → 500 | Group rotation about (128,128) | 0 → −360° | inOut |
| 0 → 7 → 15 | 0 → 233 → 500 | Arc trim start (tail) | 0 → 38% → 0 | inOut |

The tail retracts toward the head during the fast middle of the sweep, then grows back, so the path
changes and the motion is not a rigid rotation alone. Frame 15 equals frame 0, so the sweep loops without
a seam.

### Runtime contract for the peer (`lottie-playback.h` / `chrome.h`)

1. **Copy needs segments.** `CopyProgress` currently plays 0 → 1 across the whole file, which goes link →
   check → link in 667 ms with no dwell. Map the phases from the existing `Motion::Success` instead:
   - Reveal (300 ms) plays `toCheck`, frames 0 → 10.
   - Dwell (1500 ms) holds frame 10.
   - Disappear plays `toLink`, frames 10 → 19, ideally over 300 ms. At the current 150 it runs at 2× speed.
   - A re-trigger during Disappear at frame f resumes `toCheck` at frame `19 − f`.
2. **Ink:** draw at `0.5 * style.iconOpacity`. `DrawLottie` currently passes `style.iconOpacity`, which
   makes the animated icon twice as bright as its static PNG.
3. **Refresh** loops `sweep` only while a navigation is actually loading. When `RefreshEnd` is called,
   finish the current cycle up to frame 15, which is rest, instead of cutting off mid-sweep. If the load
   finishes within one cycle, play it once.
4. **Reduced motion** shows a static final frame and runs no Lottie. Copy shows frame 10 (check) for the
   dwell, then frame 0 (link), or the existing opacity-only swap. Refresh stays at rest (frame 0).
5. **ThorVG `Asset::Sample` side change, found by the harness.** Once one side has been sampled, a
   different side fails. Sampling 16 then 32 returns null. This will happen when the window's DPI
   changes. The harness works around it with one Asset per side. The fix belongs in
   `src/native-lottie.cpp`.
6. **Frame counting:** ThorVG reports `totalFrame = op − ip`, so 20 and 15, and `Sample(progress)` reaches
   index `count−1`. Refresh frame 14 is about 0.04 ink off rest. Return to the static PNG or frame 0 at
   the end, as `RefreshActive` already does.

## 3. Validation

Run `powershell -File tests/lottie-assets/run.ps1`. Everything runs natively with no WebView2 and no
browser launch. Output goes to `build/opus-lottie-qa/`.

- **rlottie 1.3.8** (`rlottie-python` from PyPI, isolated venv): both files parse, with 21 and 16
  frames inclusive. Feature allow-list: `gr/sh/st/tm/tr` only, with no masks, mattes, effects, images or
  expressions. Rest and check frames match Phosphor at 16, 32 and 64 px. The last frame equals the rest
  frame pixel for pixel. The refresh mid-sweep differs from rest by 0.48. Morph keyframes share the same
  topology. Contact sheets: `frames/*-contact-{16,64}.png`. Per-frame 32 px PNGs are in `frames/`.
- **ThorVG 1.1.3, production path** (`src/native-lottie.cpp` + `build/lottie-backend`): both files load,
  with 20 and 15 frames and 0.667 s / 0.500 s. Every frame samples at 16, 32 and 64 px. Rest and check
  match Phosphor with a difference of at most 0.038. Copy returns to link with a difference of 0.000.
  Contact sheets: `thorvg/thorvg-*-contact-64.png`.
- **`tests/design-system/geometry_test.cpp`** checks: a 6 DIP gap for tab ↔ tab and last tab ↔ new tab;
  contiguous hit regions; overflow capacity that is maximal and never overlaps the window controls;
  close insets that are equal at 4 DIP; favicon and close glyph insets that mirror each other at 8 DIP;
  a fixed title slot; and the 1 DIP white 10% focus ring.

## 4. Paper sync (coordinator applies after integration)

- Tab strip: tabs are 128 × 32 with radius 6 and a **6 px gap**, with the first tab at x 57. Use a
  6 px gap between the last tab and the 32 × 32 new-tab button.
- Tab content: favicon 16 at inset 8. Title from x+32 to right−32. Close is a 24 × 24 slot at right−28
  with top inset 4, holding a centred 16 px Phosphor Bold `x`. It has no fill of its own.
- Focus: 1 px inside stroke in `#FFFFFF` at 10% opacity. It replaces the 2 px #BEBEBE ring everywhere.
- Motion frames: use the copy-link contact sheet frames 0, 3, 6 and 10, and the refresh frames 0, 4, 7,
  11 and 15. Note the durations of 300 ms reveal / 1500 ms dwell / 300 ms return, and the 500 ms sweep.

## 5. Limits

- I validated the assets in two native engines, rlottie and ThorVG. The in-app animation was not seen
  running, because that needs the peer's integration and a window launch, which this task did not allow.
- Copy frames 1 to 3 and 11 to 13 are transitional curve shapes, each on screen for about 33 ms.
  They are a real path morph, not a cross-fade.
- I did not edit `design/design-system.md`, to avoid concurrent edits. This file is the change record.
