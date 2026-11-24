# Mode Focus — Paper fidelity audit

Independent audit of branch `VALXXII/Mode-Focus` (HEAD 6e6a3f7, fresh build `build/fid-head`, 0.3.700) against the
Paper file `01M6Q7YT14ZKMGET54ASPNGQEC`, page « Mode Focus ». The implementation's report and QA script were not
reused for measuring: the captures come from a separate script, `design/focus-mode-fidelity/tools/fid_run.py`. It
drives a real window at a 1180 × 705 DIP client, on a dark page (#0E0E0E, Paper's placeholder) or a white page, and
copies the composed screen. `tools/fid_measure.py` and `tools/fid_rows.py` then measure the captures.

- Normal-mode reference: build of `49b003b` (`build/focus-base`, 0.3.688), the base of the branch. The two later
  main PRs merged into the branch (#108 i18n gate, #109 settings profiles row) do not touch the chrome.
- Runs: 125 % (primary monitor) and 100 % (secondary), each with a dark page, a white page, and a dark page with the
  bookmarks bar. Raw captures are in `design/focus-mode-fidelity/raw/<run>/`, all numbers in
  `design/focus-mode-fidelity/measurements.json` and `rows-*.json`, crops in `design/focus-mode-fidelity/crops/`.
- Paper values come from `get_computed_styles` (not screenshots). The artboard client is 1178 × 703, inside a 1 px
  frame; the captures use a 1180 × 705 client, so widths differ by 2 DIP. Every value below is relative to the edges.
- Ideal corners are computed analytically: the Paper geometry, supersampled 16 × 16 per device pixel, composited
  over the measured background. « max err » is the largest channel difference (0–255) between the capture and that
  ideal.

Verdicts: **exact** (identical pixels, or ≤ 1/255 from the analytic ideal) · **≤ 0.5 px** (a sub-pixel difference
from rounding to whole device pixels) · **DEFECT**.

## 1. Activé (bar hidden) — 1J9P-0

| Property | Paper | Measured 100 % | Measured 125 % | Verdict |
|---|---|---|---|---|
| Page inset left / top / right / bottom | 4 / 4 / 4 / 4 (`padding 4`) | 4 / 4 / 4 / 4 DIP (4 px) | 4 / 4 / 4 / 4 DIP (5 px) | exact |
| Page corners, all four | radius 6 | antialiased; max err 1 (dark page), 7 (white page) | max err 1 (dark), 8 (white) | exact (the white-page error of 7–8 is the antialiasing of the edge colour, not the shape) |
| Window background | `--color-browser-canvas` #171717 | #171717 | #171717 | exact |
| Anything else drawn | nothing | only #171717 around the page¹ | only #171717¹ | exact |

¹ Other colours in the 4 DIP band appear only at the window's own rounded corners: DWM shows the desktop behind them.
The same pixels differ in normal mode on main too.
On the 125 % white-page run, a page scrollbar thumb shows at the top right of the page just after switching on (the
page's own overlay scrollbar, which fades a moment later). It is not drawn by the chrome.

## 2. Hit box — 1J9W-0

Paper: the « Hitbox » rectangle is 1178 × 14 at (0.2, 0.2), so it covers 0.2 … 14.2 DIP. With device pixels, the
pixel rows whose centre is inside are the rows under 14 DIP: rows 0–13 at 100 %, rows 0–17 at 125 %.

| Pointer (client px) | 100 %: x 0 / centre / W−1 | 125 %: x 0 / centre / W−1 | Verdict |
|---|---|---|---|
| last row inside (y 13 at 100 %, y 17 = 13.6 DIP at 125 %) | reveal / reveal / reveal | reveal / reveal / reveal | exact |
| first row outside (y 14 at 100 %, y 18 = 14.4 DIP at 125 %) | none / none / none | none / none / none | exact |

## 3. Hover en haut (panel at rest) — 1JBQ-0

| Property | Paper | Measured 100 % | Measured 125 % | Verdict |
|---|---|---|---|---|
| Panel x | 4 … W−4 (`width client−8`, centred) | 4 … 1176 DIP | 4 … 1176 DIP | exact |
| Panel top | 4.2 (`top: 4.2`) | 4 DIP | 4 DIP (px 5; 4.2 DIP would be 5.25 px) | ≤ 0.5 px (the 0.2 is a sub-pixel nudge in Paper) |
| Height | 84 (1+2+36+6+32+6+1) | 84 DIP (bottom border row 87, page from 88) | 84 DIP (border px 109, page from px 110) | exact |
| Fill | #171717 | #171717 | #171717 | exact |
| Border | 1 px #FFFFFF14 → #292929 over #171717 | #292929 on all four sides, 1 px | #292929 rows + the next row at #1C1C1C (28): 1.25 px of coverage, as in Paper at 1.25× | exact |
| Border over the page (bottom) / over the canvas (top corners) | #292929 / arc blended into #171717 | #292929 / max err 1 | #292929 / max err 1 | exact |
| Radius, top corners | 4 | antialiased, max err 1 vs ideal | max err 1 | exact |
| Radius, bottom corners over a dark page | 4 | max err 4 | max err 9 | exact to within the ±9/255 dark-page noise |
| Radius, bottom corners over a **white** page | 4, antialiased over the page | **stair-stepped: 4 pixels off, max err 69** | **5 pixels off, max err 104** | **DEFECT D2** |
| Shadow | none | none (rows outside the border = page / canvas) | none | exact |
| Hairline under the toolbar | none (border only) | rows 77–82: 23 23 23 23 23 \| border 41 | 23 23 23 23 28 41 | exact |
| Page moves / resizes when shown | no | rows below the panel identical to the hidden state (only a clock of another app differs) | 0 px differ | exact |
| Tab row + toolbar row vs normal chrome translated by (5, 5)² | identical | **every element identical** at offset (5, 5) px: profile plate, tab, +, window buttons³, back/forward, reload, address field, link/star, right icons, separator, gear | offset (6, 6) px = 4.8 DIP. Profile plate, +, back/forward, reload and the gaps are identical. Text in the tab and the address field differs by a sub-pixel phase (max 46–92/255 on glyph edges). The window buttons, the link/star, the right icons and the gear land on a half pixel: the layout width is 1170 DIP = 1462.5 px (max 54–60 on icon edges) | 100 %: exact · 125 %: ≤ 0.5 px (deviation 3 of the implementation report, declared) |

² The reference is normal mode on main's build at a W−10 DIP client (1170 DIP: what the panel lays out on), compared
element by element (`rows-*.json`).
³ 7 px (max 13/255) differ in the window-button box at 100 %. They are the panel's own top-right corner arc, which
enters the box at chrome-local y 2–3. Expected.

## 4. Panel corners over a white page

- **Top corners**: the panel's radius-4 arc and its 1 px border blend over the canvas exactly as computed (max err 1)
  at 100 % and 125 %. The page's own top corners (radius 6) are entirely under the panel, so nothing of the page shows
  there. Correct.
- **Bottom corners**: **no dark notch** (no canvas pixel shows outside the arc). The arc is **not antialiased against
  the page**: each edge pixel is either pure page white or panel blended over the canvas.
  - 125 %: pixels the ideal arc would render at 187 / 93 / 48 are 255 / 36 / 39.
  - 100 %: pixels the ideal arc would render at 220 / 105 are 255 / 36.
  - See D2.

## 5. Désactivé — 1J7V-0 (normal mode must not change)

| Comparison (same 1180 × 705 client, same page, same URL) | 100 % | 125 % | 125 % + bookmarks bar | Verdict |
|---|---|---|---|---|
| Branch (focus off) vs main `49b003b`, chrome (top 81 / 121 DIP + 2) | 0 px differ | 62 px, only in the 10 × 10 px window corners (DWM rounding shows the desktop behind) | 62 px, same corners | exact |
| Branch: off → on → off again vs first off | 0 px differ | 0 px | 0 px | exact |

## 6. Slide

Each run captured every distinct screen frame (every 5–10 ms) and measured where the page starts below the panel
(the hole), and where the painted panel content sits (best vertical match against the panel at rest).

| Property | Paper / plan | Measured 100 % | Measured 125 % | Verdict |
|---|---|---|---|---|
| Direction | from above (whole panel translated) | the panel content is the rest panel translated up (residual 0 on matched frames) | same | exact |
| Slide-in curve (fit of the bottom edge to cubic-bezier(0.23, 1, 0.32, 1)) | 200 ms | 208 / 216 / 220 ms (white / band / dark; 8–10 moving frames) | 200 / 196 / 208 ms (11–15 frames) | exact within the frame jitter |
| Slide-out | 160 ms after the 300 ms grace | starts at 304–322 ms; fit 108–168 ms (4–5 frames) | starts at 304–322 ms; fit 160–172 ms (7–9 frames) | exact |
| Keyboard reveal (Ctrl+L) | at once (decision) | first frame after the command already at rest (88 / 128) | same | exact |
| Canvas showing over the page below the panel | never | 0 px in every frame | 0 px | exact |
| **Hole and painted panel in step** | the page edge follows the panel's bottom edge | **one frame out of step**: slide-in, the page covers the panel's lower 8–27 px (toolbar cut, no bottom border); slide-out, a 6–20 px strip under the panel shows a stale copy of the toolbar | slide-in 5–33 px cut; slide-out 16–23 px stale toolbar strip | **DEFECT D1** |
| Reversal (pointer back at the top during the slide-out) | from where it is, no jump | positions go down then up with no jump. The pointer came back about when the slide-out ended, so a true mid-flight reversal was not captured | same; the closest case reverses at 5.6 DIP: 5.6 → 20 → 60 → … | no jump seen; mid-flight not proven |

## 7. Drawers in focus mode (user request, not in Paper)

| Property | Request | Measured 100 % | Measured 125 % | Verdict |
|---|---|---|---|---|
| Réglages top (from the panel's gear) | panel bottom + 4 = 92 | 92 DIP | 92 DIP (115 px) | exact |
| Right edge | W − 4 | W − 4 DIP | W − 4 DIP (5 px) | exact |
| Top corners | radius 6, antialiased | both antialiased; vs a solid r6 ideal: max err 5–6 (TL), 1–3 (TR) | max err 5 (TL), 1–3 (TR) | exact (errors ≤ 6/255, invisible) |
| Bottom | unchanged | reaches the window bottom (gap 0) | gap 0 | not compared with a normal-mode drawer capture |
| With the bookmarks bar | top 132 | 132 DIP | 132 DIP | exact |

## 8. Bookmarks bar on

| Property | Plan | Measured 100 % | Measured 125 % | Verdict |
|---|---|---|---|---|
| Panel height | 124 | 124 DIP (border row 127, page from 128) | 124 DIP | exact |
| Band row vs normal mode translated | identical | 0 px differ | 788 px, max 50: text sub-pixel phase, plates identical | 100 %: exact · 125 %: ≤ 0.5 px |
| Tab and toolbar rows | as in §3 | identical | as in §3 | as in §3 |

## 150 % offscreen renders (`design/focus-mode-evidence/offscreen-150/`)

- `focus-panel-144.png`, `focus-panel-band-144.png`: panel, border, rows and icons look right. Nothing visibly off.
- The drawer-corner composites over white and over dark are smoothly antialiased.
- These renders have no page under the panel, so they cannot show D1 or D2.

## Defects, ranked by visibility

**D1 — During the slide, the page hole lags the painted panel by one frame** (visible as a flicker of the toolbar at
each reveal and hide).
- Slide-out: for one or two frames, a strip 6–23 px tall under the moving panel shows a stale copy of the toolbar
  (address field, back/forward), as if the toolbar were doubled.
- Slide-in: for one or two frames, the page still covers the bottom 5–33 px of the panel. The toolbar is cut, with no
  border and no bottom padding.
- Seen in every slide at 100 % and 125 %, on dark and white pages (`content shift implies panel bottom` vs `hole
  bottom` in the analysis; up to 33 px apart).
- Crops:
  - `design/focus-mode-fidelity/crops/DEFECT-slideout-ghost-125-expected-left-captured-right-x2.png`
  - `design/focus-mode-fidelity/crops/DEFECT-slideout-ghost-100-expected-left-captured-right-x2.png`
  - `design/focus-mode-fidelity/crops/DEFECT-slidein-cut-100-expected-left-captured-right-x2.png`
- Repro:
  1. Switch focus mode on (Ctrl+Maj+F) on a white page.
  2. Put the pointer at the top edge, then move it away.
  3. Record the screen at ≥ 100 fps, or run `fid_run.py --page white`. Frames `raw/head-125-white/slideout-02-*` and
     `raw/head-100-white/slideout-02-*` show the doubled toolbar; `raw/head-100-white/slidein-03-*` shows the cut
     toolbar.

**D2 — The panel's bottom corners are aliased over a light page.**
- The radius-4 arc is a hard staircase: each edge pixel is pure page white or dark. Paper's corner is antialiased.
- At 125 %, 5 pixels per corner are off by more than 24/255, up to 104/255. At 100 %, 4 pixels, up to 69/255.
- No dark notch. Over a dark page the error stays below 10/255, which is invisible.
- Cause: the window-region hole keeps each pixel either page or panel, by its centre (the plan's architecture).
  Partly covered pixels cannot blend with the page.
- Crops: `design/focus-mode-fidelity/crops/DEFECT-panel-bottom-left-corner-white-125-ideal-left-build-right-x16.png`
  and `…-100-…` (ideal on the left, build on the right, 16×).
- Repro:
  1. Switch focus mode on with a white site open.
  2. Reveal the panel from the top edge.
  3. Zoom on the panel's bottom-left or bottom-right corner (`raw/head-125-white/on-shown.png`, around px (5, 110)
     and (1469, 110)).

Not defects (deliberate decisions, or within rounding):
- The chrome origin snapped to 6 px at 125 % (4.8 DIP instead of 5). Because of it, the text and the right-hand icons
  are at a sub-pixel phase vs normal mode (deviation 3 of the implementation report).
- Panel top 4 vs Paper's 4.2.
- Keyboard reveals at once (plan decision).
- Drawer geometry (user request).
