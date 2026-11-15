# Address selection follows the active profile

User request (2026-11-15, screenshot): « est-ce qu'on peut modifier la couleur du surlignage à la couleur du
profil ? » — the URL selected in the native address field was the Windows blue while the window's profile is
teal.

## Result

The selected run in the address field is now painted in the **window's profile fill** with **white text**:

| Window | Selection fill | Measured selected text vs fill (rendered pixels) |
|---|---|---|
| Teal profile (screenshot, hue 190 / .12) | `#00837E` | 4.62:1 |
| Warm profile (hue 50 / .16) | `#BD5800` | 4.61:1 |
| Extreme intensity (hue 100 / .16 max) | `#857500` | 4.62:1 |
| Faint (hue 300 / .02) | `#777380` | 4.62:1 |
| Neutral « Personnel » | `#747474` (its own grey, **not** the reference blue) | 4.67:1 |
| Private window | `#3E67FF` (its existing reference identity) | 4.58:1 (real app) |
| 120 ms profile fade, steps 25/50/75 % teal→grey | `#1D7F7C` / `#3A7C79` / `#577877` | 4.80 / 4.84 / 4.82:1 |

Same values at 96, 144 and 192 DPI (`design/profile-address-selection-evidence/address_selection_test-after.txt`).
Contrast = WCAG ratio between the **brightest non-fill pixel actually rendered inside the run** (glyph cores)
and the fill pixel, read back from the field — not computed from constants.

High contrast (forced colours) keeps the native system highlight untouched.

## Approach and why

### Native probe (before any code)

A plain single-line `EDIT` has no per-control selection colour: it paints the selected run with the system
`COLOR_HIGHLIGHT` / `COLOR_HIGHLIGHTTEXT`. `WM_CTLCOLOREDIT` only sets the ordinary text/background. A probe
(an `EDIT` built like the browser's, parent logging `WM_CTLCOLOREDIT`) measured:

- the EDIT paints the selection **outside `WM_PAINT`** too (direct `GetDC` draws on `EM_SETSEL`, mouse drag,
  double-click);
- it sends **`WM_CTLCOLOREDIT` before every draw**, with the very HDC it then paints into (clip type
  `COMPLEXREGION`, client box);
- its highlight cell spans from `EM_POSFROMCHAR(start)` to `EM_POSFROMCHAR(end)` **inclusive** — one pixel
  past the run's text extent where the font's rounding differs (`:/` in the URL: extent 9 px, highlight 10 px).

### Narrow rendering change (`NativeUI::EditSelection`, `src/native-ui.h`)

1. **Clip** — from the parent's `WM_CTLCOLOREDIT`, `ExcludeClipRect` the selected run's cell on that HDC:
   the system highlight never reaches the screen, in any path.
2. **Paint** — once the EDIT's draw is over, paint that cell ourselves: profile fill, the selected substring in
   white, the field's font, at `EM_POSFROMCHAR(start)`, one line high (`tmHeight`), clipped to `EM_GETRECT`.
   - `WM_PAINT`: our own `BeginPaint`, the EDIT paints into that HDC (`wParam`, verified: the unselected text
     is still drawn by the EDIT), DC saved/restored around it, then the run — one frame, caret hidden by
     `BeginPaint`/`EndPaint`.
   - Any other message that made the EDIT draw (detected by `Clip` being hit at nesting depth 0): `HideCaret`,
     `GetDC`, run, `ShowCaret` — synchronously before the message returns.
   - `WM_PRINT`/`WM_PRINTCLIENT`: painted into the given HDC.
3. **Colour** — `Accent::Selection()` (`src/design-system.h`), a new role next to fill/ink, read per paint:
   the profile's own fill (`ProfileEditor::FillOf`: `AccentFill` or `NeutralFill`). It follows the existing
   120 ms fade in `SetAccents`/`ProfileAnimTick` (no new timer) and is clamped by the existing `Reach` so white
   stays ≥ 4.5:1 at every fade step. Private windows never call `SetAccents`: they keep the reference blue.
   `AddressSelectionChanged()` repaints only the selected run when the colour moves.

Nothing else changes: the selection range, caret (incl. the custom `AccentCaret`), text, IME, undo, clipboard,
`EM_SETSEL` callers, context menu, UIA value/focus are all the EDIT's own. No hooks/detours, no
`SetSysColors`/registry, no windowless rich edit, no replacement of the EDIT, no screen-colour substitution
(we never read pixels back in the product).

Rejected: windowed RichEdit (no selection-colour message; windowless `ITextHost::TxGetSysColor` would mean a
text-host framework for one colour), `SetSysColors` (global, all apps), owner-drawing the whole field (text
engine replacement), post-hoc repaint without the clip (one-frame blue flash on direct draws).

## Behaviour covered (native harness, real EDIT, rendered pixels)

`tests/address_selection_test.cpp` builds the field exactly like `main.cpp` (`ES_AUTOHSCROLL`, limit 8192,
zero margins, `Inter Display` −16 DIP `ANTIALIASED_QUALITY`, the same `WM_CTLCOLOREDIT` colours) at **96, 144
and 192 DPI**, drives it with window messages only (thread keyboard state via `SetKeyboardState`; no
`SendInput`, no clipboard), and reads back what Windows composed. Per case: no system-highlight pixel anywhere,
the run is the fill, no solid fill patch outside it, unselected text still drawn, selection/text unchanged by
the painting, glyph vs fill ≥ 4.5:1.

- select all (`FocusAddress`/Ctrl+L/Ctrl+A path = `EM_SETSEL 0,-1`) and partial, for 5 colours;
- `WM_PAINT` repaint; Shift+→×2, Shift+End, Ctrl+Shift+←, Home (collapse);
- mouse drag, double-click;
- typing over a selection, delete, undo ×2;
- long URL (412 chars) scrolled to its end, select-all + Shift+←, Unicode run (é, 日本語, Arabic);
- live profile switch teal → orange (no stale teal pixel), 3 fade steps teal → grey;
- focus loss (no highlight of any kind), focus regained;
- forced colours via injectable policy: native highlight present, no fill; back off again;
- system `COLOR_HIGHLIGHT` identical before/after the run (nothing global touched);
- two fields: only the focused one paints.

**First-failure proof**: the same harness with the helper neutralised (`Active()` → false, i.e. today's
native EDIT) fails 165 checks, starting with « teal select all @96dpi: no system-highlight pixel (3632 …) »
(`address_selection_test-before-helper-disabled.txt`).

## Evidence (`design/profile-address-selection-evidence/`)

- `before-native-highlight-96.png` — native EDIT (system blue), same field.
- `after-full-teal-96.png`, `after-partial-teal-96.png`, `after-long-scrolled-96.png` — harness, teal.
- `app-after-*.png` — real `browser.exe` (isolated E2E, below).
- `address_selection_test-after.txt`, `address_selection_test-before-helper-disabled.txt`, `run_all-ci.txt`,
  `app-e2e.txt`.

## Tests run

- `tests/address_selection_test.cpp` (new; added to `test-ui.ps1` and `tests/update-integration/run_all.py`
  with the strict flags `-Wall -Wextra -Werror`).
- `python tests/update-integration/run_all.py --ci` (all existing native fixtures incl. shortcuts,
  accessibility, navigation errors, split view, host integration compiling the production `main.cpp`).
- `tests/e2e/address_selection_e2e.ps1` (new): a fresh copy of the built app in a temporary root, disposable
  profiles (teal, warm, neutral) and a private window; address focused through UI Automation, window captured
  with `PrintWindow(PW_RENDERFULLCONTENT)`. Result (`app-e2e.txt`): teal `#00837E` 4.62:1, warm `#BD5800`
  4.61:1, neutral `#747474` 4.67:1, private `#3E67FF` 4.58:1; 0 system-highlight pixels in each; the temporary
  root and its processes removed afterwards. The fixture app was built from this working tree
  (`tools/release/build.py --out build/address-selection-app`, not committed).

## Limits

- Single-line field only (the address bar); other native EDITs are unchanged and still use the system
  highlight.
- The run is laid out like the EDIT lays it out (start = `EM_POSFROMCHAR`, extent of the run in the field font,
  native inclusive end). Bidirectional text mixed inside one selected run would be drawn in logical order by
  `ExtTextOutW`, like the EDIT itself does for this LTR field; not a URL case.
- The custom `AccentCaret` XOR bitmap is solved for the field background; over the fill (only at a run's
  edge) it shows a different tint, as it did over the system blue.
- High contrast is read at paint time (`SPI_GETHIGHCONTRAST`); switching it while a selection is visible
  takes effect at the next draw of the field.
- Webpage `::selection` is not changed (out of scope).

## Process note

During the very first probe a `SendInput` step targeted the foreground window, not the probe (the probe was
not foreground): four synthetic keystrokes (Shift+→, `A`, Ctrl+A, Home) went to whatever window was in the
foreground at that moment. All later probes and tests use only thread-local keyboard state and posted window
messages to our own windows.

## Integration hunks (for manual integration after the i18n checkpoint)

- `src/native-ui.h`: new `NativeUI::EditSelection` (+ `#include <algorithm>`).
- `src/design-system.h`: `Accent::SelectionCurrent()` / `Accent::Selection()`.
- `src/profiles-integration.h`: `SetAccents(fill, ink, selection, animate)`, tween of the selection role,
  `AddressSelectionChanged()` next to `AnnotationAccentChanged()`, `ApplyProfileChrome` passes `FillOf(*p)`.
- `src/main.cpp`: 4 lines — `addressSelection` + `AddressSelectionColors()` + `AddressSelectionChanged()` after
  the globals, `addressSelection.Attach(...)` in `WM_CREATE`, `addressSelection.Clip((HWND)l,dc)` in
  `WM_CTLCOLOREDIT`. No user-visible string changed.
