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
Contrast = WCAG ratio between the **brightest selected-glyph pixel actually rendered** (glyph cores)
and the fill pixel, read back from the field — not computed from constants.

High contrast (forced colours) keeps the native system highlight untouched.

## Approach and why

### Native probe (before any code)

A plain single-line `EDIT` has no per-control selection colour: it paints the selected run with the system
`COLOR_HIGHLIGHT` / `COLOR_HIGHLIGHTTEXT`. `WM_CTLCOLOREDIT` only sets the ordinary text/background. A probe
(an `EDIT` built like the browser's, parent logging `WM_CTLCOLOREDIT`) measured:

- the EDIT paints the selection **outside `WM_PAINT`** too (direct `GetDC` draws on `EM_SETSEL`, mouse drag,
  double-click);
- it sends **`WM_CTLCOLOREDIT` before every draw**, with the very HDC it then paints into;
- it renders the selection itself on `WM_PRINTCLIENT` (documented), identical to the screen except ±1 GDI
  antialiasing rounding between a DIB and the device surface (`545454` vs `555555`).

### Independent review (2026-11-15) and second probe

The first version (`681e8f4`) recomputed the highlight cell itself: start = `EM_POSFROMCHAR(start)`, width =
the run's extent, one LTR rectangle. The independent review found two P2 regressions, reproduced here:

1. **Arabic IDN URL** `https://مثال.إختبار/مسار`, logical 8..12: the EDIT reorders the RTL run visually (the
   selected word is drawn at the right end of the run), so 265 px of system blue remained at 96 DPI and the
   fill covered unselected text.
2. **7 920-character URL** (< 8 192 limit) scrolled to its end then select-all: `EM_POSFROMCHAR` returns
   16-bit signed coordinates and wraps (run computed at x 12 785..131 433), so the whole visible selection
   stayed blue (4 512 px at 96 DPI).

A second probe compared the EDIT's native highlight columns with a Uniscribe (`ScriptStringAnalyse` /
`ScriptStringCPtoX`) recomputation anchored on an on-screen character (which does avoid the 16-bit wrap). The
visual order was right, but the positions were off by 1–6 px (font fallback, the EDIT's own origin and its
inclusive end column), e.g. Arabic 8..12: native `[101,119)`, Uniscribe `[104,121)`; mixed-script 27..33:
native `[207,237)`, Uniscribe `[201,230)`. **Recomputing the geometry outside the EDIT cannot be exact**, so
the fix stops recomputing it.

### Narrow rendering change (`NativeUI::EditSelection`, `src/native-ui.h`)

The EDIT draws its own selection; only its colours change.

1. **Clip** — from the parent's `WM_CTLCOLOREDIT`, while the field is focused with a non-empty selection,
   `ExcludeClipRect` the whole client area on that HDC: nothing the EDIT draws there reaches the screen.
2. **Compose** — once the EDIT's draw is over, ask the EDIT to draw itself again with `WM_PRINTCLIENT`
   (`PRF_CLIENT | PRF_ERASEBKGND`) into a memory DIB the size of the client. That image is exactly what the EDIT
   would have shown: its own Uniscribe shaping, bidi reordering, fallback fonts, scroll offset, loading
   background and highlight geometry. Then **remap only the highlight pixels** and copy the image to the
   screen DC:
   - a highlight pixel is the exact cell colour `H = COLOR_HIGHLIGHT`, or an antialiased glyph pixel
     `p = H + a·(T − H)` (`T = COLOR_HIGHLIGHTTEXT`, same coverage `a` on every channel, ±2.5 rounding);
   - only in columns that contain the exact cell colour (or a contiguous run of such blend columns touching
     one), so unselected text and the field are never touched;
   - it becomes `fill + a·(white − fill)`: the same coverage, our colours.
   - `WM_PAINT`: our own `BeginPaint` (the EDIT still processes its paint into that DC, clipped), then the
     composed field: one frame, caret hidden by `BeginPaint`/`EndPaint`.
   - Any other message that made the EDIT draw (`Clip` hit at nesting depth 0): `HideCaret`, `GetDC`,
     composed field, `ShowCaret`, synchronously before the message returns.
   - Whenever a draw was clipped, the field is always recomposed, even if the selection disappeared during
     that draw (Ctrl+Shift+← collapsing it: found by the new oracle harness). If the DIB cannot be allocated,
     the next paint is left to the EDIT (native blue once) rather than a stale frame.
3. **Colour** — `Accent::Selection()` (`src/design-system.h`), a new role next to fill/ink, read per paint:
   the profile's own fill (`ProfileEditor::FillOf`: `AccentFill` or `NeutralFill`). It follows the existing
   120 ms fade in `SetAccents`/`ProfileAnimTick` (no new timer) and is clamped by the existing `Reach` so white
   stays ≥ 4.5:1 at every fade step. Private windows never call `SetAccents`: they keep the reference blue.
   `AddressSelectionChanged()` repaints the field when the colour moves.
4. **Separability gate** (re-review of `9a85a0a`, conditional P2) — the pixel classification above is only
   sound if no ordinary pixel of the field can look like a highlight pixel. With a legal, non-forced
   black / white system pair, every grey of the field (`#292929`), its ink (`#949494`) and their antialiased
   blends lie on the H → T line: the reviewer's exact-production reproduction recoloured 6/6 unselected pixels
   (`#292929 → #299793`, `#949494 → #94CBC9`). Now `Clip()` records the field's **actual ordinary palette** from
   the parent's DC (`GetTextColor`, `GetBkColor`) plus the other surface its brush paints (the loading fill,
   passed by `main.cpp`). `Separable(pair, palette)` is false when H/T differ by < 96 on every channel, when the
   palette is unknown, or when the ink, a surface, or any of 65 blends of the ink over each surface lies within
   6 levels (wider than the 2.5 classification tolerance) of the H → T segment. Not separable → `Active()` is
   false and the EDIT keeps its **native highlight, untouched**; `Remap()` itself also refuses and changes
   nothing. The system pair is read through an injectable source (`PairSource`, default `GetSysColor`), so tests
   use synthetic pairs without touching any Windows setting.

Nothing else changes: the selection range, caret (incl. the custom `AccentCaret`), text, IME, undo, clipboard,
`EM_SETSEL` callers, context menu, UIA value/focus are all the EDIT's own; without a selection the EDIT paints
exactly as before. No hooks/detours, no `SetSysColors`/registry, no windowless rich edit, no replacement of the
EDIT, no new dependency, **nothing read from the screen**: the only pixels read are the EDIT's own off-screen
`WM_PRINTCLIENT` image of itself.

Rejected: recomputing the run (v1, review findings above), Uniscribe recomputation (inexact, probe above),
windowed RichEdit (no selection-colour message; windowless `ITextHost::TxGetSysColor` would mean a text-host
framework for one colour), `SetSysColors` (global, all apps), owner-drawing the whole field (text engine
replacement), post-hoc repaint without the clip (one-frame blue flash on direct draws).

## Behaviour covered (native harness, real EDIT, rendered pixels)

`tests/address_selection_test.cpp` builds the field exactly like `main.cpp` (`ES_AUTOHSCROLL`, limit 8192,
zero margins, `Inter Display` −16 DIP `ANTIALIASED_QUALITY`, the same `WM_CTLCOLOREDIT` colours) at **96, 144
and 192 DPI**, drives it with window messages only (thread keyboard state via `SetKeyboardState`; no
`SendInput`, no clipboard, nothing activated), and reads back what Windows composed on the field's own window.

**Oracle (geometry never computed by the test):** for every case the test also captures the EDIT's own native
rendering of the same state (forced-colours policy → helper inactive, repainted through `WM_PAINT`). Per case:

- no system-highlight pixel anywhere in our field;
- every native highlight-cell pixel is exactly the profile fill;
- every native selected-glyph pixel keeps its native coverage in white (±3);
- **nothing outside the native highlight changes** (±1 GDI rounding);
- selection range and text unchanged by the painting; brightest selected-text pixel vs fill ≥ 4.5:1.

Cases: select all (`FocusAddress`/Ctrl+L/Ctrl+A = `EM_SETSEL 0,-1`) and partial for 5 colours; `WM_PAINT`
repaint; Shift+→×2, Shift+End, Ctrl+Shift+←, Home; mouse drag, double-click; typing over a selection, delete,
undo ×2; long URL scrolled to its end, select-all + Shift+←, Unicode run; **the reviewer's Arabic IDN 8..12
(verbatim) plus 6..10, 9..14, 13..19, 0..23 and Shift+→×11 across the bidi boundary; the reviewer's 7 920-char
URL select-all after end scroll (verbatim), Shift+← then Ctrl+Shift+←, partial near the end; mixed Latin /
accented / CJK / Arabic / Hebrew URL, 4 ranges (one logical range = 3 visual segments)**; live profile switch
(no stale teal pixel); 3 fade steps; focus loss (no highlight of any kind) and regain; forced colours (native
highlight kept, no fill) and back; system `COLOR_HIGHLIGHT` unchanged; two fields, only the focused paints.

**Colour classifier (synthetic, injected pairs, no OS setting):** the reviewer's 12-pixel image verbatim with
H = black / T = white against the grey palette → remap refused, **0/12 pixels changed** (red on `9a85a0a`: 6/6
unselected changed, root `color-ambiguity-9a.txt`); 6 ambiguous pairs (black/white, white/black, `#808080`/white,
field grey/white, dark/light grey, two close blues) → not separable; the Windows default `#0078D7`/white, this
machine's pair and two coloured pairs stay separable, and on a synthetic row their cells become the fill while
field, ink and an AA ink pixel stay byte-identical. **Real EDIT with an injected black/white pair** (the real
EDIT still draws the system blue): helper inactive, field identical to the native rendering (0 changed pixels,
no fill, native highlight present) at 96/144/192 DPI (`fallback-ambiguous-pair-96.png`), then the real pair
again passes the full oracle.

| Measured (rendered pixels) | 96 DPI | 144 DPI | 192 DPI |
|---|---|---|---|
| Arabic IDN 8..12: cell px → fill / glyph px | 281 / 61 | 783 / 203 | 1515 / 318 |
| 7 920-char select-all: cell px → fill / glyph px | 4512 / 2328 | 11177 / 4483 | 20424 / 7656 |
| Mixed bidi 30..40: cell px → fill / glyph px | 1079 / 289 | 2714 / 621 | 4812 / 1038 |
| system-highlight px left, any case | 0 | 0 | 0 |

**Red / green:**

- New harness against the pinned `681e8f4` helper: **319 failures**, among them exactly the reviewer's
  « Arabic IDN domain selection @96dpi: no system-highlight pixel (265 …) » and « 7919-char URL … (4512 …) »
  at all 3 DPI, plus the v1 glyphs not matching the native coverage (`address_selection_test-red-681e8f4.txt`).
- Same harness against the fix: **0 failures** (`address_selection_test-after.txt`).
- The reviewer's own harness (`review_selection_test.cpp`, unchanged) against the fix: **0 system-highlight
  failures**. Its 18 remaining failures (« run is the profile fill », « no fill outside the run ») come from its
  expected-run oracle, which is `EM_POSFROMCHAR` + LTR extent — the geometry the review proved wrong for these
  two inputs (`review_selection_test-on-fix.txt`).
- Earlier first-failure proof (helper disabled = today's native EDIT) kept:
  `address_selection_test-before-helper-disabled.txt`.

## Evidence (`design/profile-address-selection-evidence/`)

- `before-native-highlight-96.png` — native EDIT (system blue), same field.
- `fallback-ambiguous-pair-96.png` — injected black/white pair: the field left exactly native.
- `after-full-teal-96.png`, `after-partial-teal-96.png`, `after-long-scrolled-96.png`,
  `after-arabic-idn-96.png`, `after-max-url-96.png`, `after-mixed-bidi-96.png` — harness, teal.
- `app-after-*.png` — real `browser.exe` (isolated E2E, below).
- `address_selection_test-after.txt`, `address_selection_test-red-681e8f4.txt`,
  `review_selection_test-on-fix.txt`, `address_selection_test-before-helper-disabled.txt`, `run_all-ci.txt`,
  `app-e2e.txt`.

## Tests run

- `tests/address_selection_test.cpp` (in `test-ui.ps1` and `tests/update-integration/run_all.py`, strict flags
  `-Wall -Wextra -Werror`).
- `python tests/update-integration/run_all.py --ci` (all existing native fixtures incl. shortcuts,
  accessibility, navigation errors, split view, host integration compiling the production `main.cpp`).
- `tests/e2e/address_selection_e2e.ps1`: a fresh copy of the built app in a temporary root, disposable profiles
  (teal, warm, neutral) and a private window; address focused through UI Automation, the test browser's window
  captured with `PrintWindow(PW_RENDERFULLCONTENT)`; the temporary root and its processes removed afterwards.
  Result after the fix (`app-e2e.txt`, 0 system-highlight pixels in each): teal `#00837E` 4.62:1, warm
  `#BD5800` 4.61:1, neutral `#747474` 4.67:1, private `#3E67FF` 4.58:1, **Arabic IDN 8..12** (teal, fill at
  the visual position of the selected word, `app-after-arabic-idn.png`) 4.62:1, **7 920-char URL** select-all
  after end scroll 4.62:1.
- `run_all --ci` after the fix: first run stopped at `bookmarks_ui_test` (keyboard-focus checks then
  0xC0000005, `run_all-ci-fix-run1-bookmarks-focus-failure.txt`); the same binary passed 3/3 alone and the full
  re-run passed (`run_all-ci.txt`, exit 0). **Cause unresolved**: a repeat pass is not a repair, and no link to
  the address path was established (the bookmarks fixture does not select address text); both logs kept.
- `run_all --ci` after the separability gate (2 runs, **neither fully green**): run 1 stopped at
  `bookmarks_ui_test` (UIA menu exposure / context-menu order checks,
  `run_all-ci-color-run1-bookmarks-uia-failure.txt`); run 2 passed bookmarks and `address_selection_test`,
  then stopped at the separate `update-toast` peer suite on `GetForegroundWindow()` / `GetFocus()` assertions
  (`run_all-ci-color-run2-update-toast-focus-failure.txt`; that suite only compiles `native-ui.h`, it does not
  use `EditSelection`). Solo A/B alternating base `d060366` and this fix: bookmarks 4/4 vs 4/4 pass,
  update-toast 5/5 vs 5/5 pass; earlier fix-only solo update-toast runs failed 2/3 while the base comparison
  was not runnable (`focus-fixtures-ab.txt`). **Unresolved**: no causal link to this change shown, none
  excluded; not claimed repaired by repetition. `run_all-ci.txt` is the green run of the previous commit
  (`b6df929`).
  The fixture app is built from this working tree (`tools/release/build.py --out build/address-selection-app`,
  not committed).

## Limits

- Single-line field only (the address bar); other native EDITs keep the system highlight.
- The remap needs `COLOR_HIGHLIGHT` and `COLOR_HIGHLIGHTTEXT` to differ by ≥ 96 on one channel and their blend
  line to stay clear of the field's ordinary palette (separability gate); otherwise — custom pairs such as
  black/white or greys, and high contrast — the native highlight is kept rather than risk recolouring
  unselected pixels. The palette is the one the parent declares on the DC (text, background, loading fill); a
  colour the parent paints without declaring it is outside the guarantee.
- Glyph coverage is one value for all channels (grayscale antialiasing: the field font is
  `ANTIALIASED_QUALITY`; the probe found no off-line pixel, including fallback Arabic/CJK/Hebrew fonts). A
  sub-pixel (ClearType) glyph pixel would fail the blend test and keep its native colour; the oracle counts
  such pixels as « no system-highlight pixel » failures, none seen.
- Each draw of a field with a selection re-renders the field once into a client-sized DIB (≈ 11 k px at
  96 DPI, 46 k px at 192 DPI); without a selection nothing extra happens.
- The custom `AccentCaret` XOR bitmap is solved for the field background; over the fill it shows a different
  tint, as it did over the system blue.
- High contrast is read at paint time (`SPI_GETHIGHCONTRAST`); switching it while a selection is visible takes
  effect at the next draw of the field.
- Webpage `::selection` is not changed (out of scope).

## Process note

During the very first probe a `SendInput` step targeted the foreground window, not the probe (the probe was
not foreground): four synthetic keystrokes (Shift+→, `A`, Ctrl+A, Home) went to whatever window was in the
foreground at that moment. All later probes and tests use only thread-local keyboard state and posted window
messages to our own windows.

## Integration hunks (for manual integration after the i18n checkpoint)

- `src/native-ui.h`: new `NativeUI::EditSelection` (+ `<algorithm>`, `<cmath>`, `<cstdlib>`, `<vector>`).
- `src/design-system.h`: `Accent::SelectionCurrent()` / `Accent::Selection()`.
- `src/profiles-integration.h`: `SetAccents(fill, ink, selection, animate)`, tween of the selection role,
  `AddressSelectionChanged()` next to `AnnotationAccentChanged()`, `ApplyProfileChrome` passes `FillOf(*p)`.
- `src/main.cpp`: 4 lines — `addressSelection` + `AddressSelectionColors()` + `AddressSelectionChanged()` after
  the globals, `addressSelection.Attach(...)` in `WM_CREATE`, `addressSelection.Clip((HWND)l,dc,<loading fill>)` (after the text/background colours) in
  `WM_CTLCOLOREDIT`. No user-visible string changed.
