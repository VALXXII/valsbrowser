# Fullscreen: the page takes the whole monitor

User report: in fullscreen (F11, or a video / page using `requestFullscreen`) the browser's top bar stays visible; the
user wants true fullscreen content.

Branch `VALXXII/fix-fullscreen-content`, based on 12cac24 (PR #76 head, untouched). Local only: no push, no PR, no
merge until root review. The source has no i18n (no new user text, no catalog entry needed).

## Root cause (reproduced, then traced)

`ApplyFullscreen` (`src/elements-integration.h`) already resized the window to its monitor and the tab-strip /
toolbar paint was meant to be hidden. Three defects remained:

1. **Page bounds ignored fullscreen.** `ApplyPages()` (`src/main.cpp`) placed a standalone tab at its own rectangle,
   from `chrome.ContentTop()` and the 2 DIP viewport inset. `ContentBox()`, which returns the whole window in
   fullscreen and is used by split view, was not used for a single page.
   - Measured on 12cac24, 3840×2160 monitor at 125 %: the page host sat at (3, 101)–(3838, 2158) and the page saw
     3068×1646 CSS px instead of 3072×1728.
   - The 81 DIP chrome stayed painted above it: rows y 6 / y 60 were chrome colours (23, 23, 23) / (41, 41, 41).
2. **The chrome stayed alive behind the page.**
   - `Chrome::Build` still produced every region, so the hit-test, tooltips, F6 focus order and UI Automation all
     kept working on invisible controls. In the real app, UIA exposed Address, Tabs, Toolbar, Settings and the caption
     buttons in fullscreen.
   - A click where the tab strip used to be opened a new tab.
3. **Keyboard paths into the hidden chrome.**
   - Ctrl+L opened the native address field over the page.
   - F6 moved focus to invisible toolbar controls.
   - Entering F11 while editing the address left the field and its focus behind.

Window bounds, the maximized state and the chrome's return on exit were already correct on 12cac24: those checks pass
on both builds.

## Fix (narrow)

`src/chrome.h`
- New `Chrome::contentOnly`: `Build()` keeps no region and no tooltip (hit, tooltips, `FocusOrder` and UIA all read
  `regions`).
- `Paint()` clears the canvas and draws nothing else. It still handles `D2DERR_RECREATE_TARGET` and `EndDraw` the same way.

`src/main.cpp`
- `BuildChrome()` sets `chrome.contentOnly=InFullscreen()`.
- `ApplyPages()` places a single page at `PixelRect(ContentBox())`. Outside fullscreen this is the same rectangle as
  before; split view already used `ContentBox()`.
- `AccessibleNodes()` returns no chrome node in fullscreen.
- `FocusAddress()` does nothing in fullscreen.
- `ToggleChromeFocus()` (F6) gives focus back to the page in fullscreen.

`src/elements-integration.h` — on entering fullscreen, the hover, pressed state and tooltip are cleared. If focus was
in the chrome (address being edited, or keyboard focus on a toolbar control), it goes to the page. A page that asked
for fullscreen already has focus and is not touched.

Unchanged:
- the fullscreen hint (origin pill / F11 text) and its timing;
- Échap leaving a page's fullscreen, and F11 toggling browser fullscreen (Échap does not leave F11, as before);
- the engine's user-gesture rule;
- monitor choice (`MonitorFromWindow`, nearest).

No new setting, string, asset or hover-reveal.

## Tests

### Real app — `tests/e2e/fullscreen_e2e.py`

This runs on a throwaway copy of each build, with a local HTTP fixture: blue `/blue` and green `/green` pages, each
with a « Plein écran » button. No credentials, no external site, no permission granted.

How it drives the app:
- F11, Échap's command, Ctrl+1 / Ctrl+2, Ctrl+T, Ctrl+L, F6 and Agrandir are the app's own shortcut messages, posted to
  our window.
- The page fullscreen is a real click on the fixture button, posted to our page's own `Chrome_RenderWidgetHostHWND`.
  The engine accepts it as a user activation (`FULLSCREEN page on`). A CDP-dispatched click was refused by WebView2
  (« TypeError: not granted »), even with `userGesture`, so the engine's gesture policy is never relaxed.
- CDP (loopback, this copy only) only reads page metrics: `innerWidth`, `innerHeight`, `devicePixelRatio`,
  `fullscreenElement`, `hasFocus()`.
- UIA through the process's own tree.
- Pixels by `PrintWindow` of our window.
- No SendInput, no foreground change, no clipboard, no screen grab.

What each state checks:

| | |
| --- | --- |
| fullscreen | the window equals the monitor; the page host equals the client area; viewport × DPR equals the client size (±2 px); the page colour at y 6 and y 60; no chrome automation id in UIA; no visible address field |
| restored | exact previous window rect; maximized state; page back under the chrome; Toolbar, Tabs and Settings back in UIA; no fullscreen element |

States covered:
- F11 on / off three times, windowed;
- page fullscreen on, focus kept, Échap command off, windowed;
- F11 and page fullscreen from maximized, back to maximized;
- page fullscreen then a tab change: the page leaves fullscreen and the window comes back;
- F11 on the green tab, then Ctrl+1: the blue tab fills the screen;
- a click where the tab strip was opens no tab;
- Ctrl+L and F6 inside F11: no field, the page keeps focus, no chrome in UIA;
- F11 entered while editing a new home tab's address: the field closes, only the canvas shows, no UIA chrome, a click
  opens nothing, F11 off brings the chrome back maximized.

Results (`design/fullscreen-content-fix-evidence/`):
- `01e` — **12cac24: 39 failures** of 105. All fall in the classes above (bounds, viewport, top-row pixels, UIA chrome,
  hidden hit, Ctrl+L, F6, home-tab field).
- `02b` — **fix: 105 / 105**.

Initial runs are kept:
- `01a` — harness crash: PIL is unavailable under `python -I`, replaced by a ctypes PrintWindow read.
- `01b` / `01c` — the UIA query was broken (`powershell -Command` does not bind `$args`), so its fullscreen checks
  passed vacuously and its "chrome back" checks failed. `01b` also used the CDP click, so its page-fullscreen steps
  never entered fullscreen.
- `01d` — the corrected test before the last section was added.
- `02a` — 95 / 95 before the last section.

### Native fixture — `tests/ui/ui_test.cpp` (`test-ui.ps1`, no WebView2)

`contentOnly` at 96 and 144 DPI, 560 and 1180 DIP, normal and private window (audio and annotation buttons shown):
- no region and an empty focus order;
- no hit anywhere in the former chrome rows;
- only canvas pixels (#171717), where the tab and field pixels differ in normal mode;
- turning it off brings back the same region count, hit and pixels.

Results:
- `03b` and the PNGs `fullscreen-content-only*.png` (144 DPI): PASS.
- `03a`: first run, a test-only PNG path mistake (fixed); all functional checks had passed.
- On 12cac24 this block does not compile (no `contentOnly`). The real-app RED above is the behavioural baseline.

### Regression fixtures run

- `04` — accessibility (real UIA client on the production provider): PASS.
- `05` — shortcuts: PASS.
- `06` — split view: model, host fixture and `--split-test` native WebView2 pass. `ApplyPages` now reads
  `ContentBox()` for the single-page case.

## Limits (not exercised)

- **Single monitor at 125 %.** Multi-monitor placement is source only (`MonitorFromWindow` nearest, unchanged). Other
  DPIs are covered by the native fixture (96 / 144), not by the real app.
- **Not tested at runtime:**
  - a profile switch while in fullscreen (the flag is global, read at every `BuildChrome`);
  - a private window (source plus the native fixture's private variant);
  - split view while in fullscreen (it already used `ContentBox()`; source only);
  - annotation mode in fullscreen (already refused by `Ann::Available`).
- **Drawers in fullscreen.** History, downloads and settings opened by shortcut are separate popups placed by their own
  anchors; behaviour unchanged, not reviewed here.
- **Ctrl+L in F11** now does nothing (no address field over the page). Revealing the chrome on demand would be a new
  product feature and was out of scope.
