# Native motion polish

Implemented on `feat/design-system-polish` in `src/motion.h`,
`src/motion-presentation.h`, and `tests/motion/polish_test.cpp`. The existing
public API and default timing remain intact: the original 54 checks still pass.
This document supersedes the older integration document only when the owner
explicitly enables the polished profile. No renderer or main-loop files were
edited by the motion worker.

## Profile and sampled values

Call `motion.EnablePolish()` **once before first interaction**. Its default is
off for compatibility. Profile configuration is not an animation event; do not
toggle it during a running transition or from paint.

| Property | Polished behavior |
| --- | --- |
| Hover fill | 120 ms CSS ease-out `(0,0,.58,1)` on enter and leave |
| Press | 150 ms same ease-out on press and release, centered scale `1 -> .96` |
| Copy check reveal | 300 ms `cubic-bezier(.2,0,0,1)` opacity/scale |
| Copy dwell | 1500 ms, one sleeping deadline, no frame paints |
| Copy disappearance | 150 ms same icon curve, shorter than reveal |
| Repeated copy | Start from current opacity, renew full dwell, never blink invisible |
| Tab selection surface | 120 ms ease-out; no delayed focus, content, glyph or text state |
| Reduced motion | Instant hover/selection, no scale; copy uses 150 ms opacity fades |

Every tween retargets from its sampled current value, including a release or a
reversal between timer frames. Same-target pointer updates do not restart the
clock. Retargeting guarantees position/opacity continuity, not spring velocity
continuity. Easing is bounded, deterministic, with exact endpoints and no bounce.
The icon curve uses a fixed 20-step inverse evaluation; no allocation, compositor
effect, blur pass, background thread, or library is required.

Reduced motion is still queried through `SPI_GETCLIENTAREAANIMATION`; query
failure conservatively reduces motion. The existing `WM_SETTINGCHANGE` path is
correct. Enabling reduction settles interaction and selection targets immediately
and suppresses copy transform without jumping its opacity or restarting its
deadline. Turning it back off does not resurrect expired effects. The static
check icon remains the success cue; this is low-stakes clipboard confirmation,
with no action or error hidden behind its finite dwell.

## Exact renderer integration (`src/chrome.h`, owned by component worker)

1. Include `motion-presentation.h` alongside/replacing the existing motion
   include. In `Chrome::Paint`, replace toolbar `size *= 1-.08f*interaction.press`
   and close-glyph `16*(1-.1f*press)` with `Motion::PressScale(...)`:

   ```cpp
   const bool reduced = motion && motion->ReducedMotion();
   size *= Motion::PressScale(interaction.press, reduced);
   // Close slot: 16 * Motion::PressScale(press, reduced).
   // For an unavailable control pass enabled=false; for opt-out isStatic=true.
   ```

   Do not scale the hit region, tab rectangle, focus ring, or reserved close slot.
   Native disabled states must block activation and avoid setting press targets.

2. Replace the copy/check paint branch with sampled complementary layers:

   ```cpp
   auto layers = Motion::CopyPresentation(motion->copy.Visual(now), reduced);
   Icon(L"copy", box, size * layers.copy.scale, layers.copy.opacity);
   Icon(L"check", box, size * layers.check.scale, layers.check.opacity);
   ```

   Both glyphs remain the existing Phosphor **Bold** assets, centered in the same
   slot. The helper keeps opacity sum at one, so the control does not disappear
   during a swap. Skip a draw when its opacity is zero. No persistent blur effect
   is introduced: the native lightweight implementation uses scale/opacity only.
   No app icon, product name, font, or icon-library changes are needed.

3. Optional selection: `motion->selection.Visual(n+1, now)` is the surface
   emphasis for tab `n` with index IDs. Blend the **one existing tab fill** toward
   its selected value. Continue using `selected==n` synchronously for title,
   icon, accessibility/focus and active page. Do not draw a second tab surface,
   overlay a black backing rectangle, scale a whole tab, or translate its layout.
   Tab icons and hover-revealed close glyphs stay immediate or <=120 ms opacity.

## Exact main-loop integration (`src/main.cpp`, owned by component worker)

- `wWinMain`: call `motion.EnablePolish()` before `chrome.SetMotion(&motion)`
  and before first input/selection. Existing `State` construction still reads the
  OS preference. Do not replace it with a hardcoded reduced-motion setting.
- `Select(int n)`: immediately after assigning `current=n`, capture `now`, call
  `motion.SelectTab(n+1, now)`, then `InvalidateChrome(); ScheduleMotion(now);`.
  Initial selection is already settled, avoiding a startup entrance. ID zero is
  reserved for no selection; do not pass raw zero-based tab indices.
- `NewTab` before `pages.push_back`, and `CloseAt` before `pages.erase`: index
  reuse must not transfer a departing tab's visual state to its neighbor. Call
  `motion.selection.Reset(); motion.ClearInteractions();`, then the normal
  `Select(...)` path. If a path retains `current` without calling `Select`, call
  `motion.SelectTab(current+1, now)` and reschedule explicitly. When no tabs
  remain, use `SelectTab(0,now)`. Resetting is unnecessary with stable nonzero tab
  IDs, but then render and select must use those same stable IDs consistently.
- Add/remove remains synchronous: existing plus/close hover and .96 press give
  feedback; removed tabs disappear immediately. There is no delayed close,
  ghost hit region, layout slide, fake retained model item, or stagger on routine
  tab actions. `ForgetInteraction(id)` supports a stable-ID removal; clearing
  all interactions is safer for current index-based regions.
- Existing copy-success hook (only after successful `SetClipboardData`) is
  correct; no caller changes beyond enabling polish are required.
- Preserve current capture cancellation, deactivation handling, timer dispatch,
  `WM_SETTINGCHANGE`, minimize/halt/destroy cancellation and chrome invalidation.
  `State::NextWake` now includes optional selection transitions automatically.

The existing `ScheduleMotion` hints remain valid: kill a recurring Win32 timer
on dispatch, invalidate chrome once including the final frame, then reschedule
from fresh steady time. No hint means no timer. At most one dwell wake follows
copy reveal. The polished finite sequence takes 29 frame wakes plus one dwell
wake at the current 16 ms cadence. Delayed dispatch samples current time rather
than replaying missing frames. Keep invalidation inside `0..clientWidth x
0..S(84)` with erase false; do not resize/paint web content on timer events.
Paint must clip its existing target to dirty chrome and restore draw state.
The model and helpers create no surfaces and make no invalidation calls, so
double-surface or black-flash regressions depend on renderer integration.

## Download hold

No download-engine hookup or progress animation was added. `Download` retains
real-event-only byte fractions, unknown totals without fabricated fill, static
paused state, and completion only on explicit `Completed`. Enabling polish
does not change its defaults or awaken it. No browser, WebView2, smoke test,
download-dialog diagnostic, runtime repair, system setting change, profile,
shortcut, or application build output was run or modified for this task.

## Evidence (2026-11-06)

```powershell
& tools/w64devkit/bin/g++.exe -std=c++17 -Wall -Wextra -Werror -pedantic -static tests/motion/motion_test.cpp -o tests/motion/motion_test.exe -luser32
& tests/motion/motion_test.exe
# PASS: 54 deterministic native motion checks; no browser/WebView2 used.
& tools/w64devkit/bin/g++.exe -std=c++17 -Wall -Wextra -Werror -pedantic -static tests/motion/polish_test.cpp -o tests/motion/polish_test.exe -luser32
& tests/motion/polish_test.exe
# PASS: 701 deterministic polish checks; native only, no WebView2.
```

The portable compiler builds both fixtures with warnings as errors. Coverage
includes independent cubic-control-point samples, monotonic bounded curves,
endpoints, rapid hover/press reversals, interruption on 200 successive inputs,
copy repetition across reveal/dwell/exit/idle, reduced-motion opacity continuity,
complementary glyph layers, preference changes, tab reversal/index reuse, removal
cancellation, startup settlement, sleeping dwell, final repaint and indefinite
idle, and dormant downloads. Existing tests retain real progress/unknown total,
duplicate completion, late timer, and legacy behavior checks. `git diff --check`
passed. Not verified: final rendered appearance, integrated renderer hooks,
actual OS preference toggling, live keyboard/screen-reader interaction, and
browser/download operation; these require owner integration and the WebView2
hold must remain in force.
