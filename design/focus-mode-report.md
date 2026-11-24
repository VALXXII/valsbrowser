# Mode Focus — report

User request (2026-11-23): the « Mode Focus » designed in Paper (file `01M6Q7YT14ZKMGET54ASPNGQEC`, page « Mode Focus »
`p-U-0`), « la même chose que Zen Browser (compact mode), juste à notre manière », with a slide-in when the pointer
reaches the top edge. Added the same day: rounded top corners on the drawers in focus mode.

Plan: `design/focus-mode-plan.md` (contract; updated twice, see « Decisions taken during the work »). Branch
`VALXXII/Mode-Focus`, base 49b003b, with origin/main merged in (#108, #109). Local only: no push, no PR.

## Port to main (branch `VALXXII/mode-focus-2`, 2026-11-24)

The first branch (`VALXXII/Mode-Focus`, 494144f, local only, untouched) could not be merged: PR #117 (0.3.757) changed how
the docked drawers slide. The port takes the whole feature onto origin/main (329f2ea and later) with the same design and
the same Paper values; what changed is listed here.

**Drawers in the new model (PR #117, NativeUI::KeyedDrawer).** A drawer window no longer moves while it slides: each
step is one present of the content drawn `lead` pixels right of its place, the strip it has not entered painted in the
key colour (transparent, click-through). Mode Focus followed the old model (window moved every frame, top corners cut by
the window region, corner windows following the window). Now:
- `DrawerLayout::liveTop / liveRight / liveBottom / liveRadius` (src/downloads.h) place the window once: 4 DIP under the
  panel, its right edge on W - 4, its bottom on H - 4 (new, see below). A change of those counts as a move in
  `ApplyGeometry` (the drawers' `Reposition()` is no longer forced).
- The two top corners are keyed out by the drawer's own paint (`KeyCorners`: the pixels the 6 DIP arc does not cover
  completely, following the content's left edge while it slides), in the same present as the content: no region change,
  no lag.
- `src/drawer-corners.h` lays the partly covered pixels back at their coverage (antialiased) with two layered windows
  owned by the drawer. The right one sits on the window's fixed right edge and never moves; the left one moves with the
  content, so it shows only at rest (a moved window reaches the screen a composition after the present). During the
  ~200 ms slide the left corner is the keyed, aliased arc.
- The downloads drawer keeps its morph from the popover (no corners while morphing, as before) and the morph-flash fix
  (0.3.815): the corners are keyed only once the morph has landed.
- In focus mode the drawer's bottom-right corner is no longer the window's: its region is square there.

**Drawer bottom 4 DIP above the window's (new).** The validated board « Annotation + Focus · barre du haut · tiroir
ouvert » (21OK-0) draws the drawer 612 high from 124 in a 740 window: its bottom on H - 4, square bottom corners. The first
branch went to the window's bottom edge (never drawn). One rule now for every top-bar Focus case:
`FocusMode::drawerBottom = 4` (one constant). `focus_qa.py --full` checks it (one check more: 32 per run).

## With the annotation mode (owner's rule 2026-11-24)

« If Annotation mode is running and you enter Mode Focus (or the reverse), the Annotation banner and the chat (Panneau
mode) stay visible and usable; Focus hides the rest of the UI, not those two. Same for the bubble in Commentaire mode. »
Paper page « #127 — Panneau latéral », validated boards: « Annotation + Focus · barre du haut » (203J-0, at rest),
« … · panneau révélé » (20MM-0), « … · tiroir ouvert » (21OK-0). Values read as the window's (the boards' 1180 x 740):
- Banner: the very top, y 0 .. 32 (its animated height while it slides in), x 4 .. W - 4 (`FocusMode::Banner`,
  `Ann::BandRect`).
- Page: x 4, top 32 (right under the banner), 4 DIP before the chat card, bottom H - 4, its four corners 6 DIP
  (`FocusMode::Content` with `ChatReserve`). R25 kept: while the banner slides, the page only moves.
- Chat (mode Panneau): a card 380 wide, 4 DIP from the window's right and bottom edges, top 32, radius 6, a 1 DIP
  #262626 border (`FocusMode::ChatCard`; src/chat-host-ui.h `PanelRect`, `CardView`, `SyncCardFrame`, four layered corner
  masks, the ring painted by `Chrome::DrawChatFrame`). Outside focus mode the chat is unchanged (flush right).
- The floating panel: under the banner, top 36 (4 under it), over the page's width only (788 in the board), 84 / 124 DIP
  high as before; hidden, it waits behind the banner (its picture slides out from under it: the banner is kept above the
  sliding picture). The hover strip is the 14 DIP right under the banner (32 .. 46), left of the chat card.
- Drawers: top 124 (4 under the panel), right edge W - 4 over the chat (`FocusMode::drawerOverChat`, as #127's
  `DrawerOverChat`; the chat is back when the drawer closes), bottom H - 4, top corners 6.
- Mode Commentaire: no card; the page goes to W - 4 under the banner; the bubble is a popup above the window, so it
  stays visible and usable (it is never under the panel).
- The selection paused (banner up, chat open): the page and the card go back to the 4 DIP top inset.
- Not changed: fullscreen still wins over everything.

Tests: `tests/focus_mode_test.cpp` (the three boards' values, 226 checks), `tests/ui/split_ui_test.cpp` section 17 (the
host: page, panel under the banner, drawers, chat card place, off again), real window:
`tests/focus/annotation_focus_journey.py` (night).

## #127 (side panel / compact layouts): what to recheck when it lands

#127 (`VALXXII/127-sidebar`) is not merged. Its plan (§ 4) already reserves `Sidebar::State::focus` and the `Overlay`
presentation for Mode Focus. Its boards « Annotation + Focus · panneau (bord survolé) » (2091-0) and « … · compact »
(20EJ-0) put the banner, the page (radius 8) and the chat card (radius 8) at the same places as the top bar, and slide the
column (248, or 40 compact) over the page from the left edge after 400 ms, under the banner (top 32); the chat does not
move. To do when both are in main:
- `FocusWanted()` / `SyncFocusMode()`: in a side layout, Focus sets `Sidebar::State::focus` (column `Hidden`) instead
  of the floating top panel; the hover zone is the left edge (#127's overlay), not the top strip.
- One chat card: #127's `PanelRect` / `CardView` / `SyncCardFrame` / `chatMasks` and this branch's are the same
  mechanism; keep one, with `CardOn() = chrome.floating || chrome.sidebar` and the radius 6 (top bar Focus) or 8 (side
  layouts), `CardInset` = `FocusMode::chatInset` (both 4).
- `Ann::BandRect`: #127's side-panel case (x from the column) and this branch's Focus case (x 4 .. W - 4, y 0) both go in.
- Drawers: #127's `DrawerOverChat` and `FocusMode::drawerOverChat` are the same rule (keep one constant).
- `focus_journey.py --tab-layout side|compact`: wire `reveal_side()` to #127's overlay, then add the two manifest entries.

## What was built

**Setting and shortcut**
- Réglages › Affichage › « Mode Focus » (right after « Réduire les animations »): a switch with its note line, found
  by the settings search. Saved per profile as `focus-mode=` (`src/settings.h`), off by default.
- **Ctrl+Maj+F** switches it on and off (`FocusModeCommand`). Ctrl+F stays the find bar. The keyboard-shortcuts panel
  lists it in « Page », next to « Plein écran ». The Réglages switch follows the shortcut when the drawer is open.
- FR / EN strings: `focus_mode_title`, `settings_focus_mode`, `settings_focus_mode_note`, `search_kw_focus_mode`.
  The i18n gate passes (0 finding) on the merged tree.

**Focus mode on** (Paper « Mode Focus – Activé », 1J9P-0)
- No chrome. The page is 4 DIP inside the window on all four sides, with its four corners rounded at 6 DIP.
- Normal mode is unchanged: inset 2, 81 / 121 DIP chrome, bottom corners 8.
- Maximised: same 4 DIP inset; the page corners are square, as in normal mode.
- Fullscreen (F11 or a page element) wins: the page alone, no panel and no hit box. Focus mode comes back after.
- ~~The annotation mode suspends focus mode while it is on.~~ Replaced on 2026-11-24 by the owner's rule: see « With the
  annotation mode » below.

**The floating panel** (« Hover en haut », 1JBQ-0)
- Position: top 4, from x 4 to W−4. Fill #171717, 1 px #FFFFFF14 border, radius 4, no shadow.
- Height: 84 DIP, or 124 with the bookmarks bar.
- Inside is the normal chrome, laid out on the client width − 10 and translated by (5, 5). It has no lower hairline.
- The page does not move or resize when the panel shows.

**Behaviour**
- The hit box is the client's top 14 DIP, full width (1J9W-0). Over the page, the main window gets no mouse
  messages, so the pointer is polled every 30 ms. The poll runs only while the window is in front or the panel is on
  screen.
- A reveal needs three things: the window (or one of its own popups) in front, the pointer in the hit box, and no
  mouse button held (no reveal during a text selection in the page).
- The panel stays while any of these is true:
  - the pointer is over the panel or in the hit box;
  - the address field is being edited, or its suggestions are open;
  - keyboard focus is in the chrome (F6 / Tab);
  - a toolbar surface is open: Réglages, Historique, Téléchargements, Son, Favoris, the bookmark popover, a band menu,
    the request chip / origin prompt;
  - a menu is open (tab, band or profile menu);
  - a drag or a mouse capture is in progress;
  - a band folder is being renamed in place.
- When none of these is true, the panel leaves after 300 ms.
- Window drag: with the panel shown, the tab row's empty space is the caption (drag, double-click maximises).

**Motion**
- The pointer's reveal slides the whole panel down from above the window: 200 ms, cubic-bezier(0.23, 1, 0.32, 1).
- Hiding slides it back up: 160 ms, same curve.
- A slide can be reversed in mid-flight from where the panel is, with no jump.
- Reduced motion (the app's setting or Windows'): no slide.
- The slide is driven by the existing frame clock (`ScheduleMotion` / `InvalidateMotion`).

**How the panel appears over the page** (the plan's architecture, revised by fix round 1; see « Fix round 1 » below)
- **At rest** (« live »):
  - every page window under the panel gets a window region without the pixels the panel covers completely;
  - the main window paints the panel in that hole and receives its clicks there;
  - two per-pixel-alpha corner windows lay the partly covered pixels of the panel's two bottom arcs over the page.
  - The region is re-applied after `ApplyPages()` and checked on every 30 ms tick (put back if the engine drops it;
    logged).
- **While it slides:** a picture of the panel at rest, in a layered child window above the page windows, moved
  frame by frame; the page windows keep their whole surface.
- Things that step away while the panel covers them:
  - the page's top corner masks;
  - split grips;
  - scrollbar handles.
- The find bar moves below the panel while the panel is shown. The tab / profile transition overlay stays under it.

**Coordinates**
- Every place that maps a chrome rectangle to the window or the screen now goes through `ChromeClient` / `ChromePx`:
  - tooltips (both kinds), UI Automation nodes, the address field, the suggestions anchor;
  - downloads, audio, profile menu, origin prompt and keyboard-menu anchors, `RegionScreen`, band rename box;
  - dirty rectangles (loading, downloads icon, audio icon).
- Pointer handlers use client DIP through `Chrome::Hit` / `Chrome::Local`: caption, tab menu, wheel, band context
  menu, band drop target, tab drag grab and drop slot.
- The tab drag lifts into a split below the panel's bottom (88 / 128) and reattaches above 64 + 5.
- The drawers start 4 DIP under the panel (92 / 132).

**Drawers in focus mode** (user request 2026-11-23)
- Réglages, Historique, Favoris and Téléchargements are affected.
- Top 4 DIP under the panel; right edge on the panel's right edge (W − 4), so panel and drawer read as one frame.
- Their two top corners are rounded at 6 DIP, antialiased (`src/drawer-corners.h`). How: the drawers are ordinary
  popup windows, so their region drops the partly covered corner pixels, and a tiny per-pixel-alpha layered window
  per corner, owned by the drawer and click-through, lays those pixels back at their coverage.
- Bottom corners unchanged. Normal mode unchanged.
- The « Voir tout » audio drawer follows the same top and right edge, but its corners are not rounded: it was not in
  the requested list.

## Decisions taken during the work, and deviations

1. **Keyboard / surface reveals appear at once** (no slide): Ctrl+L, Alt+D, F6, Ctrl+T, a drawer opened by shortcut,
   the downloads popover opening on a download, a press on the panel while it slides.
   - Only the pointer's reveal slides; hiding always slides.
   - Asked before deciding; confirmed by the coordinator. Recorded in the plan (Motion).
   - Reasons: emil-design-eng « never animate keyboard-initiated actions », and the address field and the popovers
     are placed where the panel rests.
2. **Drawers under the panel, aligned right, top corners rounded** (plan + user request). Not in Paper. Drawers stay
   rounded when the window is maximised: they are floating surfaces like the panel, not the page edge.
3. **Origin snapped to whole device pixels.** The panel's edges and the chrome's origin are rounded to whole device
   pixels, the same rounding as the page windows.
   - The hole and the painted panel meet exactly, and the chrome inside renders like the normal one.
   - Cost: at 125 / 150 / 175 % the chrome sits up to 0.5 px from the exact DIP value (for example 4.8 DIP instead of
     5 at 125 %).
   - At 144 DPI the origin (8 px = 5.333 DIP) is not exact in float. Text antialiasing then differs by a sub-pixel
     phase from the normal chrome: 2 375 of 192 807 pixels, blurred maximum 29/255. Plates and icons are identical.
     `ui_test` checks this tolerance at 144 DPI and exact equality at 96 DPI.
4. **The surfaces anchored to the address field reveal the panel.** These are the request chip and the origin
   (permission) prompt. The plan's list of toolbar surfaces did not name them; they are anchored to the field.
5. **The find bar keeps clear of the panel.** It hangs 8 DIP inside the page's top-right corner and moves down with
   the panel while the panel shows. Not in the plan; the alternative was the find bar covering the toolbar's buttons.
6. **Window size of the captures.** Captures use a 1180 × 705 DIP client, as the plan's evidence section asks. Paper's
   artboards are 1180 × 705 including a 1 px frame, so Paper's client is 1178 × 703. The panel is therefore 1172 wide
   in the captures and 1170 in Paper; the values (4, 84, radius 4…) are the same.

## Fix round 1 (fidelity audit D1 / D2, code review)

The independent audit (`design/focus-mode-fidelity-audit.md`, commit 6f345ed) found two defects; a code review found
seven more points. All are fixed. Final build `build/focus-r8` (0.3.701 + the fixes).

**D1: the hole lagged the painted panel by one frame during the slide.**
- Cause: the page windows' new region (SetWindowRgn on another process's window) reaches the screen one composition
  after the main window's own paint.
  - Slide-in: the page still covered the panel's lower part (a cut toolbar).
  - Slide-out: the hole was still large, and the window's surface there still held the panel's previous frame
    (WS_CLIPCHILDREN never repaints under a page). The result was a doubled toolbar strip.
  - Re-ordering the calls does not help, since the delay is in the composition. Neither does UpdateWindow / DwmFlush.
- Fix: no moving hole.
  - While the panel slides it is a picture (`Chrome::RenderPanel`: the chrome drawn on a compatible render target
    and read back through Direct2D 1.1).
  - The picture sits in a per-pixel-alpha layered child window above the page windows (the same kind as the
    existing split grips and page-corner masks), moved with SetWindowPos.
  - At rest the panel goes live (hole + the window's own paint). The picture stays 60 ms over it, until the hole is
    composed.
  - A slide-out shows the picture at rest first (DwmFlush), takes the hole away, and moves after a 50 ms lead.
  - A press on the moving picture lands the panel at rest at once.
  - If Direct2D 1.1 or the layered window is unavailable, the panel slides live as before (logged).
- Measured with `design/focus-mode-fidelity/tools/fid_sync.py`: every slide frame on a white page is compared with the
  panel at rest translated to where the hole (the page's top row) is.
  - Before: 20 of 30 frames disagree at 100 %, 29 of 45 at 125 % (the audit's own captures).
  - After: **0 of 42 at 100 %, 0 of 66 at 125 %**. In every compared frame the visible panel is the rest panel,
    translated to the hole, pixel for pixel.
  - The audit's reversal (pointer back mid-slide) is now captured mid-flight: 110 → 96 → 73 → 55 → 53 → 81 → … px,
    no jump.

**D2: the panel's bottom corners were aliased over a light page.**
- The hole now takes only the fully covered pixels (`FocusMode::FullyInside`).
- Two click-through per-pixel-alpha corner windows lay the partly covered pixels back over the page at their coverage
  (`FocusMode::BottomCorner`, 8 × 8 samples per pixel, border grey along the arc, fill inside). They show only while
  the panel is live.
- The sliding picture carries its own antialiased corners.
- Max error against the audit's analytic ideal: **before 69 / 255 at 100 %, 104 at 125 % → after 2 at 100 %, 4 at 125 %**.
  No pixel is above 24.

**Code review**
1. The loading fill behind the address text while editing was about 5 DIP short in focus mode. It now uses the field
   in client DIP (`ChromeClient(chrome.bar)`).
2. Keyboard-opened chrome surfaces now bring the panel at rest first (`FocusAnchor`):
   - the profile menu (Ctrl+Maj+M);
   - every menu or popover anchored to a chrome region (`RegionScreen`, `ShowMenu` with an anchor);
   - downloads, audio, settings, history and bookmarks drawers;
   - the permission prompt.
   - Regression check: `split_ui_test`.
3. When the panel's covering changes (an instant reveal included), the scrollbar handles are placed again at once
   (`ScrollbarRefresh` in `FocusApply`).
4. A press while the panel slides lands it at rest first; press and release then hit the same control. The tab
   drag's grab and grabY use `ChromeOrigin()`. Regression check: `split_ui_test`, a press mid-slide gives a click on
   the tab now under the pointer.
5. The find bar no longer swallows Ctrl+Maj+F: `find-bar.h` handles Ctrl+F only without Shift, and
   `ElementSurfaceKey` routes Ctrl+Maj+F to the Mode Focus command from the find field.
6. `search_kw_focus_mode` keywords are separated by spaces, like every other keyword string.
7. The loading-indicator invalidation is skipped while the window paints no panel (hidden or sliding).

Also found while testing:
- `FocusHold` treated a null focus as « the address field » when the field did not exist (fixtures only); now
  guarded.
- When the ease-out reached exactly 1 a frame before its end, the panel could stay as its picture at rest (not live,
  no hole). The frame after the end now settles it, and a fresh picture is made at each reveal.
- Reveal log lines now name their cause (`FOCUS bar shown at once (…)`, `FOCUS live`, `FOCUS sliding`).

**Tests in fix round 1** (final tree)
- `focus_mode_test`: 210 checks, pass.
- `drawer_corners_test`: 42, pass.
- `ui_test`: pass. The panel's picture laid over the canvas equals the painted panel: 0 differing pixels at 96 and
  144 DPI, with and without the band.
- `split_ui_test`, `settings_test`: pass.
- `test-ui.ps1`, `test-split-view.ps1`, `test-shortcuts.ps1`, `test-design-system.ps1`,
  `test-elements-integration.ps1`: all exit 0.
- i18n gate: 0 finding.
- `test.ps1` smoke: the first run stopped on its download assertion (the downloads popover closes when the window
  loses the foreground; the PC was in use). Rerun alone: exit 0, « SMOKE PASS ».
- Real-window QA (`focus_qa.py --full`), 31 / 31 in each of three runs: 125 %, 100 %, and 125 % with the band.
  - Some band runs failed while the window lost the foreground mid-run, because the PC was in use: the log showed
    `front 0` and a window position off by 60 / 120 px.
  - The final runs pass.
- `fid_run.py` + `fid_sync.py` / `fid_measure.py` (white page): 0 disagreeing frames, corners ≤ 4 / 255.
- `run_all.py` was not rerun this round; the task did not list it.

**Slide timings with the new mechanism** (the audit's curve fit, `fid_measure.py` on the final captures)

| | 100 % | 125 % |
|---|---|---|
| Slide-in fit (cubic-bezier(0.23, 1, 0.32, 1)) | 224 ms | 204 ms |
| Slide-out fit | 164 ms | 156 ms |
| Slide-out start after the pointer leaves | 370 ms | 358 ms |

- The fits were 196–220 ms in and 108–172 ms out before the fix: same curve, same frame jitter.
- The slide-out now starts about 50 ms later (the lead). That is 300 ms of grace + 50 ms + up to 30 ms of poll.
- Evidence: `design/focus-mode-fidelity/after/`.
  - `sync-before.json`, `sync-after.json`, `measurements-after.json`;
  - corner crops, ideal next to the build;
  - slide frame strips at 100 % and 125 %.

## Measured slide timings (real window)

Before fix round 1. The QA script's own numbers with the final build are in `design/focus-mode-evidence/qa/results-*.json`:
- pointer → first moving frame: 40–42 ms;
- first frame → rest: 136–150 ms;
- slide-out start: 359–371 ms;
- slide-out → off the page: 59–84 ms.

Measured by copying the screen as fast as GDI allows (one frame every 5–10 ms) and reading the panel's bottom row in
each frame:

| | 125 % | 125 %, bookmarks bar | 100 % |
|---|---|---|---|
| pointer at the top edge → first moving frame | 19 ms | 23 ms | 52 ms |
| first moving frame → panel at rest (last pixel row) | 143 ms | 159 ms | 127 ms |
| distinct positions on screen during the slide-in | 15 | 9 | 7 |
| pointer leaves → slide-out starts | 318 ms | 319 ms | 310 ms |
| slide-out → the panel off the page (bottom ≤ 4 DIP) | 76 ms | 82 ms | 90 ms |

The 100 % run's first try gave 30 / 155 / 8 / 332 / 72 ms. The 100 % screen is 88 px tall to cover, so it shows fewer
distinct positions.

The 125 % runs were on a 3840 × 2160 monitor, the 100 % run on a 1440 × 2560 one. Raw numbers:
`design/focus-mode-evidence/qa/results-*.json`.

How to read these numbers:
- The poll adds 0–30 ms before a reveal starts, and the screen shows a frame up to one vsync later.
- With a strong ease-out, the last pixel row of a 200 ms slide is reached at about 72 % of the time: 110 px to cover
  at 125 %, and ease-out(0.72) = 0.994. So the measured « first frame → rest » of 127–159 ms is the 200 ms curve.
- The same goes for the 160 ms slide-out: the panel stops covering the page when ease-out ≥ 0.955, at about half the
  curve, about 80 ms. That matches the measured 76–90 ms.
- The 300 ms grace starts at the poll tick that sees the pointer gone: 310–319 ms measured.

## Verified / unverified

Verified in a real window at 125 % and 100 %:
- the four Paper states;
- the slide-in / slide-out frame strips;
- the hole: a complex region while shown, none while hidden;
- a white site under the panel: no notch at its corners;
- maximised;
- the bookmarks bar (panel 124, drawer at 132);
- a drawer opened from the panel (top 92, right edge W−4, rounded antialiased top corners, kept while open);
- address editing keeps the panel; Escape lets it go;
- focus mode off again: the normal chrome.

Unverified:
- **150 % in a real window.** This PC has no 150 % monitor; Windows scaling was not changed (coordinator decision).
  Covered offscreen instead: the panel at 144 DPI (`ui_test`) and the drawer corners composited at 150 %
  (`drawer_corners_test`).
- **Real input.** Not exercised:
  - typed keys: the QA posts the window's own Shortcut messages;
  - physical mouse clicks: clicks are posted on the main window;
  - a real pointer drag of a tab or the window from the panel.
  The pointer itself is real (SetCursorPos, read by the app's own poll).
- **Unexplained transient:** in the first QA run only, a light grey rectangle was briefly composed over the page after
  the slide-out. It was not reproduced in the four following runs; the window list showed no extra window of the app.
  Reported, not explained.

## Tests

All runs below are on the merged tree (b0e2a43 + the evidence commit) unless noted.

**New**
- `tests/focus_mode_test.cpp` (strict, wired into `run_all.py`): 179 checks.
  - Paper values, panel at 0 / 0.5 / 1 for 560 and 1180 DIP with and without the band, origin.
  - Hit box: 13.9 in, 14 out.
  - Ease-out against CSS cubic-bezier.
  - Show / keep / hide: grace, reversal keeps position, keyboard hold at once, reduced motion.
  - Hole spans checked pixel by pixel at 100 / 125 / 150 / 175 %.
- `tests/drawer_corners_test.cpp` (strict, wired into `run_all.py`): 42 checks. Coverage mask, region cut, the layered
  corner windows (owned, click-through, follow the slide, hidden with the drawer), composites at 4 DPI.

**Additions to existing tests**
- `tests/settings_test.cpp`: default off, round trip, garbage values, CRLF, old files.
- `tests/shortcuts_test.cpp`: Ctrl+Maj+F gives the focus command, Ctrl+F stays the find bar, the command switches
  the setting both ways.
- `tests/ui/split_ui_test.cpp`, section 16:
  - ContentBox {4, 4, W−4, H−4}; four 6 DIP corners; split pages get their top corners and the ring follows;
  - hidden: nothing hit, no caption; revealed: hit through the panel, caption in the tab row's empty space;
  - fullscreen wins; off again: the normal chrome exactly.
- `tests/ui/ui_test.cpp` (offscreen chrome), 96 / 144 DPI, with and without the band:
  - fill, border, radius, no shadow, no hairline;
  - the chrome is the normal one translated: exact at 96 DPI, sub-pixel text phase only at 144 DPI;
  - hit, halfway, hidden.

**Regression runs**
- `test-ui.ps1`, `test-split-view.ps1`, `test-shortcuts.ps1`, `test-design-system.ps1`: all exit 0.
- `settings_test`: pass. i18n gate: 0 finding (after merging origin/main; on the untouched base 49b003b it had 6
  CheckTab findings, fixed by #108).
- `test.ps1` smoke: exit 0, « SMOKE PASS ».
- Real-window QA (`qa/focus_qa.py --full`): 31 / 31 at 125 %, 31 / 31 at 100 %, 31 / 31 at 125 % with the bookmarks
  bar.

**CI runner `python tests/update-integration/run_all.py`**
- First full run: every « existing » fixture passed, `focus_mode_test` and `drawer_corners_test` included.
  - It then stopped on `install_test` (update-core peer tests): 2 of 542 checks failed in a timed helper-process
    migration step. The test touches nothing changed by this branch.
  - Rerun alone, `tests/update-core/run_tests.py` passed: update_core_test 319 / 0, install_test 542 / 0.
- Second full run: stalled in `context_menu_hover_test` (a native menu test that passed in the first run) until the
  2 h background limit stopped it. Rerun alone at once, it passed.
- The two parts the full runs never reached were then run with the runner's own switches. Both exit 0, « ALL PASSED »:
  - `run_all.py --only peers`: update-core, update-toast, webview2 startup recovery;
  - `run_all.py --only integration`: release tooling, old validator, host integration + mocked end-to-end install,
    Windows setup suite with 468 checks and 0 failures.
- Every step of `run_all.py` has therefore passed on this tree, but **not in one single uninterrupted run**. The two
  interruptions are a timing-dependent install test and a stalled native menu test, both outside this branch's code,
  and both pass when rerun.

## Evidence (`design/focus-mode-evidence/`)

**How the captures were made**
- Real window, a fresh copy of the build (`build/focus-r3`, merged tree, 0.3.699), a local white page, a 1180 × 705 DIP
  client.
- Script: `qa/focus_qa.py` (`--full`, at `--monitor primary` = 125 %, `secondary` = 100 %, `--band`).
- The suffix names the run: `-125`, `-100`, `-125-band`. Assembled by `qa/assemble.py`.

**The captures**
- `paper/`: the four Paper artboards (get_screenshot).
- `paper-vs-build-1..4.png`: Paper next to the build at 100 %. The states are Désactivé, Activé, Hitbox and Hover en haut.
- `01-off-*`: focus mode off (the normal chrome).
- `02-on-hidden-*`: on, bar hidden; the page 4 DIP inside the window, four 6 DIP corners.
- `03-hitbox-*`: the same, with Paper's 14 DIP hit box drawn over it (#009DFF 10 %, an annotation on the capture).
- `04-shown-*`: the pointer at the top edge, the panel at rest.
- `05-white-site-corners-*`: 8× zoom of the panel's bottom corners over the white page (no dark notch).
- `06-maximised-hidden-*`, `06-maximised-shown-*`: maximised.
- `08-drawer-from-panel-*`, `08-drawer-top-corners-zoom-*`: Réglages opened from the panel's gear. Top 92 (132 with
  the bar), right edge W−4, antialiased 6 DIP top corners (8× zoom).
- `09-address-editing-*`: Ctrl+L, the pointer far away 1.2 s later: the panel stays.
- `10-slide-in-strip-*`, `11-slide-out-strip-*`: every distinct screen frame of the slides, timestamped.
- `12-off-again-*`: focus mode off again.
- `*-125-band`: the bookmarks bar on (panel 124 DIP).
- `offscreen-150/`, standing in for 150 %, which has no real window here:
  - `focus-panel-144.png` and `focus-panel-band-144.png`: the panel rendered offscreen at 144 DPI;
  - `focus-reference-144.png`: the normal chrome it is compared with;
  - `drawer-top-corners-composite-150-white|dark-x8.png`: the drawer's corner pixels (region cut + layered
    coverage) composited at 150 % over a white and a dark page, 8× zoom.
- `probe/`: the coordinator's region probe (before the work).
- `qa/results-*.json`: checks and slide timings of each run.

**Not the browser:** a round chat bubble with a « 2 » badge, another application's floating widget on the 100 %
monitor, shows in some 100 % captures. It is composed above every window, and the captures copy the screen. A first
100 % run also caught a floating timer; the window was moved lower and the run was redone.

The « Mise à jour disponible » card at the bottom left is the browser's own update toast: this dev build sees a newer
release.
