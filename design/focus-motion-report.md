# Mode Focus — entry / exit motion (branch `VALXXII/focus-transition`)

Owner's report (2026-11-25, after 0.3.840): Ctrl+Maj+F « jumps » — the UI and the page switch from one state to the other at
once. Designer's motion spec the same night: Paper page « Mode Focus - En cours », board « Mode Focus — mouvement d'entrée
et de sortie » (read with the Paper tools; quoted below as « the spec »).

## The cause, measured

The switch was a relayout in one frame. On Ctrl+Maj+F, `SettingChanged(FocusMode)` ran `Layout()`: the chrome became the
floating panel (the bar gone in that frame) and `ApplyPages()` resized and moved the page window (another process's
WebView2 host) to the Focus box in the same frame.

Proof without the screen: `tests/focus/transition_qa.py` runs a copy of the build on a hidden Windows desktop (CreateDesktop:
never displayed), presses the window's own Ctrl+Maj+F command and copies the window with PrintWindow (PW_RENDERFULLCONTENT)
as fast as it can. On the 20:00 release build (0.3.894): the page's top edge goes from 80.8 to 4.0 DIP between two consecutive
captures (a 76.8 DIP step), the bar gone in the same capture; no frame in between.

## What the motion does now (the spec, applied)

- One block, one spring: no bounce (damping 1.0), response 0.3 s, critically damped and analytic (`src/focus-motion.h`), the
  same position for every surface. Only position, size and opacity move.
- Entry: the top bar slides up 0 → −100 % of its height and fades over the first 60 % of the path; the page's rectangle grows
  on the same curve, its top edge on the bar's bottom edge plus the inset growing 0 → 4 (the canvas shows only in that inset).
  Inset and radii arrive at the same time.
- Exit: the same path backwards; the bar's opacity 0 → 1 over the first 40 % of the path.
- Interruptible: a second press retargets the spring from the position and the velocity on screen (never from the target),
  the bar's opacity from the opacity on screen. No input is blocked.
- How, on Windows (the spec's ⚠️ case): the page is another process's window, so it is not resized every frame. At the press,
  a frozen picture of the page (PrintWindow of our own WebView hosts, as `src/content-transition.h` does) and a picture of
  the normal bar (`Chrome::RenderBar`) are put in DirectComposition visuals above the window's children (the topmost target
  shared with the tab-switch transition); the live page keeps its box while the pictures move; when the motion lands the
  live page is resized ONCE under the pictures at rest, and once it is drawn at its new size (CapturePreview reports it, plus
  one frame) the pictures leave in a 120 ms cross-fade.
- The open drawers follow the frame (their top, right edge, bottom and top corners); the chat card (mode Panneau) follows the
  page's top and bottom; the annotation banner's picture stays glued to the page's top and goes up with it.
- Reduced motion (Windows' or the browser's setting): no slide, no spring: the picture of before over the new layout, then a
  120 ms cross-fade (the bar fades in or out without moving).
- The composition device is made at idle 2 s after the window first shows (and one throwaway page capture), so the first
  Ctrl+Maj+F does not pay for it (it cost ~230 ms on the first press).

## Measured after (hidden desktop, `transition_qa.py`, flat blue page, 125 %)

- The app's own trace (the page's top edge at every frame it drew, logged when the motion lands): 2 motions, 125 frames, the
  largest step between two frames 6.3 DIP (it was 76.8 in one frame), each motion landing exactly on 4.0 / 81.0.
- Captures: the page's edge seen at 30+ positions between the two layouts; no capture without the page; no white; no hole
  inside the page. The bar's own text is white: the white check covers the page column below the bar.
- Interrupted mid-way: back exactly where it started, no step above the capture's own resolution.
- Reduced motion: page on every frame, no white, no hole.
- Limit of the method: on a hidden desktop, PrintWindow does not render the window's own Direct2D surface (the bar and the
  canvas come out black at rest), and a capture can stall ~200 ms while the app keeps drawing; that is why the app's own
  per-frame trace is checked too. What a person sees is the night's eye check.

## Decisions and deviations

1. The live page is resized when the motion lands, for the entry too (the spec suggests at its start). Resizing it at the start
   held the composition and the captures ~250 ms during the slide on this machine; at the end it happens under the pictures
   at rest and is invisible.
2. End values are today's layout, unchanged: the top bar's Focus page has its corners at 6 (Paper p-U-0, validated); the spec's
   « rayon 0 → 8 » is the side panel's value (#127 boards) and applies there when #127 is in main.
3. A drawer open during the entry ends where Focus places drawers today (92, or 124 with the annotation banner), not at 4: the
   spec's « → 4 » would put it over the revealed panel's place.

## Not done yet

- Side panel / compact (#127, not in main tonight): the column must slide left 0 → −100 % on the same spring, the page's left
  edge glued to it. The model takes it as one more edge; the QA already passes `--tab-layout` (`--layout side|compact`).
- The chat card moves 4 DIP sideways at the switch (normal: flush right; Focus: 4 from the edge): its horizontal place is in
  `src/chat-host-ui.h` (lot R's file); to lerp after lot R merges.
- The comment bubble (mode Commentaire) does not follow its element during the slide: the live page is still at its old box,
  the bubble follows the live page.
- Window resized or moved during the motion: the motion ends at once on its target (no picture stretched).
- First press latency: on the hidden desktop the first page picture of a session costs ~210 ms (the next ones ~20 ms), even
  after a throwaway capture at idle; the screen shows the unchanged layout meanwhile (no glitch, a delay). To measure on the
  real screen at night: the log says « FOCUS motion in (… page picture N ms …) ».

## Tests

- `tests/focus_motion_test.cpp` (pure, in `run_all.py`): spring values and curve at 60 / 150 Hz, no bounce, settling,
  interruption keeping position and velocity, the fades' 60 % / 40 % and their continuity on reversal, the glued layout
  (canvas only in the inset, never a hole), clamping, the annotation banner's room.
- `tests/focus/transition_qa.py` (hidden desktop, daytime-safe): scenarios enter-exit, interrupt, `--reduced`,
  `--start-focus`; `--layout side|compact` once #127 is in main.
- Existing: `focus_mode_test`, `split_ui_test` (instant switch in fixtures: the motion is skipped there), `ui_test`.
