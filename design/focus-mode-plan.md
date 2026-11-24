# Mode Focus — plan

User request (2026-11-23): build the « Mode Focus » designed in Paper (file `01M6Q7YT14ZKMGET54ASPNGQEC`,
page **Mode Focus** `p-U-0`), « la même chose que Zen Browser (compact mode), juste à notre manière ».
Follow-up from the user: the bar **slides in** from the top when the pointer reaches the top edge.

Base: `49b003b` (branch `VALXXII/Mode-Focus`). Native Win32 + Direct2D chrome, windowed WebView2 pages.

## User decisions (2026-11-23, answered in chat)

1. **Activation**: shortcut **Ctrl+Maj+F** (toggle) **and** a switch « Mode Focus » in Réglages › Affichage
   (same switch row component as « Réduire les animations »). Persisted in `settings` (per profile, like the
   other Display settings). Default **off**.
2. **Margins**: in focus mode use the Paper values exactly — page inset **4 DIP on all four sides**, page
   corners **6 DIP on all four corners**. Normal mode is **not** changed (stays inset 2 / bottom radius 8).
3. **Bookmarks bar**: if the bar is on, it is part of the floating panel too (panel 40 DIP taller).

## Paper — exact values (from get_jsx; DIP)

Artboards are 1180×705 with a 1 px `#FFFFFF14` frame (the window frame: ignore it, the client starts inside).
Client = 1178×703 in the artboards.

**Mode Focus – Désactivé** (`1J7V-0`): today's normal chrome. Not to be changed (see decision 2).

**Mode Focus – Activé** (`1J9P-0`): no chrome at all. « Zone de page » padding **4** on all sides
(`paddingTop: 4, paddingInline: 4, paddingBottom: 4`), the page (`#0E0E0E` placeholder = the site) has
radius **6** on all four corners. Window background = canvas `#171717`.

**Mode Focus – Activé – Hover en haut (Hitbox)** (`1J9W-0`): the hit box is a rectangle at client
(0, 0), width = client width (1178), **height 14**. (Drawn `#009DFF` at 10 % only to show it: invisible in product.)

**Mode Focus – Activé – Hover en haut** (`1JBQ-0`): the page does **not** move or resize (same 4 DIP inset,
same content position as « Activé »); a floating panel covers its top:

| Panel property | Value |
|---|---|
| position | absolute, `top: 4`, horizontally centred, **width = client − 8** (1170 for 1178) → x = 4 … W−4 |
| background | `#171717` (canvas token) |
| border | **1 px solid `#FFFFFF14`** (white, alpha 0x14 = 20/255) |
| radius | **4** |
| padding | top **2**, bottom **6**, left/right 0 (rows bring their own 6 DIP side padding, as in normal chrome) |
| layout | column, gap **6**: tab row 36 high, then toolbar row 32 high |
| height | 1 + 2 + 36 + 6 + 32 + 6 + 1 = **84** (with bookmarks bar: +40 = 124, decision 3) |
| shadow | **none** |

The two rows inside are identical to the normal chrome (profile plate, tab, +, window buttons 32×32 with
`paddingRight 6`, back/forward pair, reload, address field, history/downloads/private/separator/settings).
Inner width = client − 10 (1168 for 1178).

### Coordinate mapping (derived)

Normal chrome lays out in its own space (chrome-local) with the tab row at y 2…38, toolbar at 44…76,
bottom padding 4 then the 1 DIP hairline at 80 (`Design::Vertical`). Inside the panel:

- chrome-local (0,0) ↦ client **(5, panelTop + 1)** (panel x 4 + border 1; panel top + border 1 − 0, since the
  panel's padding-top 2 equals the normal row offset 2);
- chrome layout width = **client width − 10**;
- the panel occupies chrome-local {−1, −1, layoutWidth + 1, 83} (or 123 with the bookmarks bar);
- the normal 1 DIP bottom hairline is **not** drawn in the panel (the border replaces it).

At rest (fully shown) `panelTop = 4` → chrome content origin = client (5, 5).

## Behaviour

States: **Off** (normal) · **Focus, bar hidden** · **Focus, bar sliding/shown**. Fullscreen (F11 or page
fullscreen) always wins: content-only as today, no panel, no hit box; focus mode resumes after.

- **Toggle** (Ctrl+Maj+F, the switch, or settings sync): instant relayout like the bookmarks-bar toggle; the
  bar starts hidden (unless the pointer is already in the hit box). Annotation mode: since 2026-11-24 (owner's rule, mode-focus-2) it no longer
  suspends focus mode: see « With the annotation mode » in design/focus-mode-report.md.
- **Reveal**: pointer inside the hit box (client y < 14, inside the client horizontally), window is the
  foreground window and not minimised, no mouse button held (no reveal during a text-selection drag in the
  page). Over the page the main window gets no mouse messages (windowed WebView2 = another process), so use a
  light **poll** (GetCursorPos, ~30 ms timer, only while focus mode is on and the window is active; stop it
  otherwise) plus the main window's own WM_MOUSEMOVE.
- **Keep shown** while any of: pointer inside the panel rect (or the hit box); address field editing or the
  suggestions popup visible; any toolbar surface open (profile menu, tab/band context menus, bookmark popover,
  audio panel, downloads popover, settings/history/bookmarks/downloads drawers — `chrome.latched`,
  `chrome.bandOpen`, the `.Open()/.Visible()` of each surface); keyboard focus in the chrome (F6/Tab cycling);
  a tab drag or any mouse capture by the main window; band rename editing.
- **Hide**: none of the keep conditions for **300 ms** (grace, so a slightly-too-low pointer does not drop it).
- **Keyboard**: Ctrl+L / Alt+D / F6 / Ctrl+T (focus the address) reveal the bar and keep it while editing;
  when focus goes back to the page and the pointer is outside, it hides after the grace.
- **Window drag**: with the bar shown, empty tab-row space is the caption (HTCAPTION, double-click maximises),
  like today. With the bar hidden only the resize edges remain (restored window).
- **Maximised**: same 4 DIP inset; page corners follow the existing rule (square when maximised).
- Drawers opened from the floating bar open **below the panel**: top = panel bottom + 4 (92, or 132 with the
  bookmarks bar). Not in Paper — flag it in the report.
- Drawers in focus mode (user request 2026-11-23, « un petit corner radius en haut du tiroir, ça fait plus polish »):
  the docked drawers (Réglages, Historique, Favoris, Téléchargements) round their **top-left and top-right corners,
  6 DIP** (the focus-mode page radius), **antialiased** (the drawers are plain popup windows: their region drops the
  partly covered corner pixels and a per-pixel-alpha layered window owned by the drawer lays them back at their
  coverage, `src/drawer-corners.h`). Bottom corners unchanged. Their right edge lines up with the floating panel's
  (client **W − 4**) so panel and drawer read as one frame. Normal mode (focus off) unchanged (81, flush right, square).

## Motion (user: « il y a un slide-in évidemment »)

- Show: the whole panel slides down from fully above the window (panelTop = 4 − 88, i.e. bottom at y 0) to
  panelTop = 4. **200 ms**, strong ease-out `cubic-bezier(0.23, 1, 0.32, 1)` (emil-design-eng: enter = ease-out,
  under 300 ms, fast because it is used many times a day).
- Hide: slides back up, **160 ms**, same ease-out curve. Reversible mid-flight from the current position (no jump).
  Since the fidelity fix (D1) the move starts 50 ms after the hide decision. During those 50 ms the sliding picture
  replaces the live panel at rest, and nothing visible changes.
- Reduced motion (app setting or Windows): no slide, instant show/hide.
- Decided 2026-11-23 (coordinator, at the worker's question): only the **pointer** at the top edge slides the panel
  in (200 ms). A reveal from the **keyboard or an opened surface** (Ctrl+L, Alt+D, F6, Ctrl+T, a drawer opened by
  shortcut, the downloads popover opening on a download) shows it **at once**, at rest (emil-design-eng: never
  animate keyboard-initiated actions; the address field and the popovers are placed where the panel rests). Hiding
  always slides (160 ms after the 300 ms grace).
- Driven by the existing motion loop (`ScheduleMotion`/frame clock), not a new timer per frame.

## Architecture (decided; probe evidence below)

The main window has `WS_CLIPCHILDREN` and each page is a windowed WebView2 (`Chrome_WidgetWin_*` child of
`window`, found by `PageHostWindow(Tab&)`), so the main window cannot draw over a page. **Decision: the main
window keeps drawing the chrome itself, and while the panel is visible every visible page window gets a window
region (`SetWindowRgn`) that excludes the panel's visible rectangle.** The main window then paints the panel in
that hole with the normal `Chrome::Paint` (translated, see mapping), and mouse messages over the panel reach the
main window directly (WindowFromPoint honours regions).

- Probe (2026-11-23, build 0.3.688 / 49b003b, `design/focus-mode-evidence/probe/` (script + before/after captures)): `SetWindowRgn` on the
  cross-process `Chrome_WidgetWin_0` returned TRUE and its GPU content was clipped — the parent's canvas showed in
  the hole at once, and after a forced redraw. Chromium may reset the region on its own resize: re-apply it in
  `ApplyPages()` and on every panel frame; remove it (`SetWindowRgn(h, nullptr, TRUE)`) when the panel is hidden.
- Hole = panel visible rect ∩ page rect, in pixels, updated every animation frame. The panel's **bottom**
  corners (radius 4) follow the arc per scanline (a pixel stays page when its centre is outside the arc), so a
  white site shows no dark notch; the top corners may stay square in the hole (they sit over the page's own
  rounded top corners).
- **Revised 2026-11-23 after the fidelity audit** (`design/focus-mode-fidelity-audit.md`):
  - **D1.** The page windows' new region reaches the screen one frame after the main window's paint, so a hole moved
    on every frame lagged the painted panel: a cut toolbar in the slide-in, a stale toolbar strip in the slide-out.
    Now the hole exists **only at rest**.
    - While the panel slides, it is a **picture** of the panel at rest (`Chrome::RenderPanel`, premultiplied, its
      rounded corners transparent).
    - The picture sits in a per-pixel-alpha **layered child window** above the page windows, moved with
      SetWindowPos. Its position and content reach the screen together.
    - The page windows keep their whole surface during the slide.
    - At rest the panel goes live (hole + the window's own paint). The picture stays three more frames over it, until
      the hole is on screen.
    - A slide-out first puts the picture at rest and takes the hole away, then moves after a **50 ms lead**
      (invisible: the picture is the panel at rest).
  - **D2.** The hole now takes only the pixels the panel covers **completely**. Along the two bottom arcs, the partly
    covered pixels stay the page's, and a tiny per-pixel-alpha click-through layered window per corner lays the
    panel's colour (border along the arc, fill inside) back at its coverage: antialiased over any site
    (`FocusMode::BottomCorner`). Those windows show only while the panel is live.
- Hide the page **top** corner masks while the panel is shown (they are HWND_TOP layered children and would cut
  the panel's corners); show them again when hidden.
- Other layered children over the page (split grips, scrollbar handles, annotation layers, chat panel host,
  status bubble) must not draw over the panel: hide or clip those that intersect it while it is shown.
- `Chrome` gets an origin (client offset) + layout width for the floating mode. **Every consumer that maps a
  chrome rect/point to client or screen coordinates must apply it** (known list from the audit, check each):
  `chrome.Hit` callers (WndProc mouse, WM_NCHITTEST `y<44` caption test), `RegionScreen`,
  `ProfileButtonScreen`, address EDIT placement in `Layout()` (`chrome.edit`, `MeasureText(L"Ag",{…44…76})`),
  suggestions anchor under `chrome.bar`, downloads anchor `S(48)..S(76)` (main.cpp ~1591), audio anchor
  (`audio-integration.h:247`), bookmark popover `RegionScreen('K')`, chrome tooltips (`ChromeTipHover`,
  `elements-integration.h`), tab drag (`ContentTop()`, `SV::liftY=84`, `reattachY=64`), tab menu / wheel `S(44)`,
  `Vertical::toolbarBottom` uses, `InvalidateChrome()` and the dirty rects (`DownloadIconDirty`, `LoadingDirty`),
  UIA `AccessibleNodes`, band (bookmarks bar) hit/drag/menus/rename, drawer tops (`DrawerLayout::top`).
- `chrome.ContentTop()` users that mean « where the page starts » must get the focus-mode value (4); users that
  mean « bottom of the toolbar » must get the panel bottom. Audit each of the 14 call sites.
- Layout in focus mode: `ContentBox()` = {4, 4, W − 4 − ChatInsetDip(), H − 4}; `PageCorners()` returns top
  **and** bottom corners for boxes touching the content top/bottom, radius **6** (focus mode only); `RenderCorner`
  / `CornerLook` get a top/bottom flag; split ring corners (`DrawSplit` / `CornerBox`) and the scrollbar
  track corner reservation follow radius 6 and the top corners in focus mode.
- Pure logic in a new header `src/focus-mode.h` (no Win32 beyond basic types): panel rect for a progress,
  chrome origin, hit-box test, keep/hide decision with timestamps, easing, hole scanline spans. Unit tested.

## Settings, shortcut, strings

- `settings.h`: `bool focusMode=false;` (`focus-mode=` key), `Change::FocusMode`, round trip like `reduceMotion`.
- `settings-panel.h`, View::Display, right after « Réduire les animations »: a `twoLine(FocusMode, …)` switch with
  a note line; search keywords.
- `Key()`: Ctrl+Maj+F → `FocusModeCommand` (free on main and on the pending shortcuts branch).
- Shortcuts panel (`shortcuts-panel.h`), section Page, next to « Plein écran »: « Mode Focus » — Ctrl+Maj+F.
- `src/i18n-catalog.inc` (FR, EN):
  - `focus_mode_title` « Mode Focus » / « Focus mode »
  - `settings_focus_mode` « Mode Focus » / « Focus mode »
  - `settings_focus_mode_note` « Masque les onglets et la barre d'adresse. Approchez le pointeur du haut de la
    fenêtre pour les afficher. » / « Hides the tabs and the address bar. Move the pointer to the top of the window
    to show them. »
  - `search_kw_focus_mode` « focus, zen, compact, masquer, barre, onglets, plein écran » / « focus, zen, compact,
    hide, toolbar, tabs, distraction »
  - The i18n gate (`tools/i18n/check_strings.py`) must pass.

## Tests

- `tests/settings_test.cpp`: default off, round trip, unknown/garbage value.
- `tests/shortcuts_test.cpp`: Ctrl+Shift+F → focus command; toggles state.
- New `tests/focus_mode_test.cpp` (pure, STRICT flags): panel rect at progress 0/0.5/1 for 560 and 1180 DIP,
  with/without bookmarks bar; chrome origin; hit box edges (y 13.9 in, 14 out); keep/hide timing (grace 300 ms,
  reversal mid-slide keeps position); easing endpoints; hole spans at 100/125/150/175 % DPI (bottom-corner arc).
  Add it to `tests/update-integration/run_all.py` (CI).
- `tests/ui/split_ui_test.cpp`: focus mode `ContentBox` = {4,4,W−4,H−4}; `PageCorners` top+bottom at radius 6;
  fullscreen still wins; split pages get top corners.
- `tests/ui/ui_test.cpp` (offscreen chrome): floating paint at 96 and 144 DPI — panel fill `#171717`, border
  pixel `#FFFFFF14` over canvas, tab plate / address field at the translated positions, no hairline.
- Regression: `test-ui.ps1`, `test-split-view.ps1`, `test-shortcuts.ps1`, `test-design-system.ps1`, settings,
  i18n gate, then the CI runner `python tests/update-integration/run_all.py` and `test.ps1` smoke.
- **Real-window evidence** in `design/focus-mode-evidence/`: fresh `--out` build folder; captures of the four
  Paper states at 1180×705 client (Off, On hidden, hit box hover → shown, shown), a frame strip of the slide-in
  and slide-out, a white site under the panel (no dark corner notch), maximised, 125 % and 150 % DPI, bookmarks
  bar on, a drawer opened from the floating bar, address editing keeps the bar.
