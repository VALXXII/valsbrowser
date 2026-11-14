# Download success: arrow → check morph

Production assets (generated, do not hand-edit):
- `assets/lottie/download-success.json`: arrow channel.
- `assets/lottie/download-success-cutout.json`: cut channel.

Generator: `download-success.py`. Review renders: `render-review.cpp` → `review/`.

## Contract for the integration (chrome / download-icon)
- **Format:** 256×256, 60 fps, `op` 25 (frames 0..24 = **400 ms**), one layer. Paths, strokes and group opacity only. Each file is about 9 KB, well under the player's 120-frame and 256 KB limits.
- **Arrow only.** The ring is not in these files. Compose exactly as today (`DownloadIcon::Compose`): ring track + fill **held at the completed frame `FillEnd` = 54**, fill tinted by the window accent (profile tint); then `download-success-cutout.json` in the cut slot and `download-success.json` in the arrow slot.
  - The circle therefore stays where it is: the established download ring, R 88, stroke 24. This is an intentional adaptation; the official CheckCircle ring is R 96.
- **Channels.** White, opaque strokes, like the existing arrow/cut files. `Compose()` uses alpha only (arrow × `Ink`, cut as destination-out).
  - The arrow is stroke 24, round caps and joins.
  - The cut is stroke 24 + 2 × 11 (the same knockout band as `download-progress.py`), following the same morph. Its notch in the ring at frame 0 closes as the arrow leaves the ring's edge.
- **Frame 0 = the existing arrow at a loop boundary.** It equals `download-arrow.json` / `download-arrow-cutout.json` at frame 14 (= `Morph`, also reached at `LoopEnd` = 92), the rest pose `dy = 30`:
  - stem (128,62)-(128,170);
  - head (88,130)-(128,170)-(168,130).

  Start the success morph when the arrow is at a loop boundary, the same rule as the existing « done » segment. `render-review` checks that frame 0 composites **pixel-identical** to the existing icon at 16/24/32 px.
- **Frame 24 = the official check.** Phosphor Bold `check-circle` (deps/phosphor-core/package/assets/bold/check-circle-bold.svg, verified byte for byte by the generator). Check centreline (88,136)-(112,160)-(168,104): the centres of the r = 12 cap/join arcs of the official outline. Measured from the actual source; the regular weight's coordinates differ.
  - `render-review` checks that the last frame is **pixel-identical** to that check drawn alone at 16/24/32/64 px.
  - Hold frame 24 for the 3 s success state; the hold starts once the 400 ms morph has settled.
- **Not in the asset:** reduced motion (show frame 24 statically), interruption by a new download, and the hold timing are the integration's.

## The morph (true vertex morph, no swap, no cross-fade)
- **Head:** 3 vertices on the shared ease (SETTLE, `cubic-bezier(.45,0,.15,1)`).
  - left tip → short end of the check;
  - corner → check corner;
  - right tip → long end of the check.
- **Stem:** 2 vertices.
  - Its bottom stays on the corner at every frame, so the joint never opens.
  - It first **shortens**: by 30 % of the eased time it is already only the length of the check's first fifth.
  - It then **rotates onto the long arm**, aligned by 55 %, and ends inside the long arm's stroke.
  - It never sweeps a long stem across the arm, never meets the arm tip to tip (no doubled tip), and its round tip never sits half-merged into the arm outside the corner join (no nub). These rules are asserted by the generator, which samples 240 points of the morph.
- **One key per frame**, so each vertex follows its own schedule (linear between frames).
- **Hand-over at frame 9.** ThorVG adds the coverage of overlapping strokes of one shape, so a stem hidden inside the arm would darken a few anti-aliased edge pixels. At the first frame where the stem is fully hidden (frame 9), the stem + head group hands over to a head-only group on the same head keys. The step happens mid-motion (16 px: 115/255 vs 67/255 and 40/255 for its neighbours). The morph ends settled: frames 22→23 and 23→24 change at most 6/255.

## Review evidence (`review/`)
- `toolbar-{16,24,32}px-{blue,green}.png`: production player + `Compose`. Columns:
  1. the existing icon at the loop boundary;
  2. frame 0 (identical to column 1);
  3. frames 6, 12, 18 and 24 (quarter, half, three-quarters, end).
- `sheet-256-green.png`: all 25 frames composited at 256 px with a profile tint.
- `arrow-channel-256.png`: the arrow channel alone (white), all 25 frames.
- `frame-{0,6,12,18,24}-256.png`: key frames.
- `render-review.log`: the pixel checks and frame-to-frame steps.

## Regenerate and verify
```
python -I design/lottie/download-success/download-success.py
g++ -std=c++17 -O1 -Wall -Wextra -static -municode -DTVG_STATIC -Ideps/thorvg-1.1.3/inc design/lottie/download-success/render-review.cpp src/native-lottie.cpp build/lottie-backend/src/libthorvg-1.a -o build/download-success/render-review.exe -lole32 -lwindowscodecs -luuid
build\download-success\render-review.exe design\lottie\download-success\review
```
`render-review` exits non-zero if frame 0 differs from the existing icon or frame 24 from the official check. Its `Compose` is a mirror of `src/download-icon.h` (with the accent tint), so the tool does not depend on files being edited by the integration.
