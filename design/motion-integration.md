# Native motion integration

Implemented in `src/motion.h`, C++17, header only, namespace `NativeChrome::Motion`. No rendering, engine calls, threads, timers, HWND ownership, HTML, or extra WebViews. The state model uses `std::chrono::steady_clock`; use one `Motion::Now()` value per event/frame. Timestamps supplied to one model must be monotonic. Keep the model on the UI thread and marshal future real download events there.

## API

- `State motion;` reads `SPI_GETCLIENTAREAANIMATION` once. Failure conservatively selects reduced motion. For deterministic tests use `State(false)` or `State(true)`.
- `motion.SetInteraction(hoverId, pressedId, now)` retargets hover (120 ms) and press (80 ms), continuously from their current values. ID zero means none. `motion.Visual(id, now)` returns normalized `hover` and `press`; opaque IDs support `TabBase=1000`, `CloseBase=2000`. Repeated equal inputs never restart a tween. Settled zero interactions are pruned on subsequent input updates.
- `motion.CopySucceeded(now)` starts a finite 150 ms eased reveal, 1500 ms full dwell, 150 ms eased disappearance. `motion.copy.Visual(now)` exposes `phase`, `opacity`, optional centered `scale`. Crossfade copy at `1-opacity` and Phosphor **check-bold** at `opacity`; there is no hand-drawn replacement glyph. A second successful copy explicitly restarts the sequence.
- `motion.DownloadChanged({status, received, total}, now)` accepts real engine input. `motion.download.Visual(now)` exposes visibility, determinate state, byte fraction, status, and completion envelope. No input means no activity. Progress never changes just because time passes; byte count reaching total never implies success. Unknown/zero total is static, without fabricated fill or perpetual spinner. Paused progress stays static; cancelled/interrupted activity disappears without success. Only an explicit `Completed` transition triggers the finite completion envelope; duplicate completed events do not restart it. Call `download.Clear()` between distinct operations if there is no intervening non-completed event. Use one model per operation, or a truthful external aggregate, for concurrent downloads.
- `motion.NextWake(now)` returns `Wake{optional delay, frames}`. No delay means kill the motion timer. `frames=true` requests another sample in 16 ms; `frames=false` requests only a future dwell expiry. A static in-progress download and a settled hovered button require no timer.
- On `WM_SETTINGCHANGE`, re-query `SystemReducedMotion()` and call `SetReducedMotion(...)`, invalidate the chrome, and recompute scheduling. Enabling reduced motion immediately settles interaction targets, removes fades/scaling, and preserves the existing dwell deadline; an active disappearing success immediately ends. A new reduced-motion success appears immediately, stays 1500 ms, then disappears immediately. Disabling reduced motion does not restart existing motion.

## Integration points for the owning UI worker

`main.cpp` / `ExtraCommand('C')`: call `CopySucceeded` only after non-null `SetClipboardData(CF_UNICODETEXT, mem)` succeeds. `OpenClipboard`, allocation, locking, or an attempted copy alone do not justify success feedback. Keep clipboard ownership rules intact. Invalidate chrome and schedule after the successful event.

`WM_MOUSEMOVE` / `WM_MOUSELEAVE`: after hit-testing, update interaction targets. For press, retain a pressed ID on `WM_LBUTTONDOWN`, use capture, and clear it on `WM_LBUTTONUP`, `WM_CAPTURECHANGED`, `WM_CANCELMODE` and deactivation. Recompute whether a captured pointer is inside its original region; do not show press on a different control. Keep keyboard focus indication immediate and independent. IDs and geometry stay owned by the UI worker.

`chrome.h`: keep the existing `Paint(titles, selected, uri, editing, hover, focus)` signature if desired; store a pointer/reference to shared `Motion::State` in Chrome or expose an owner-managed sampling bridge. Read values once at the start of paint. Do not change targets from paint. Replace the existing hover fill with an interpolation using the sampled value; do not append a second overlay fill. In particular, the close region must not introduce a second tab background. Preserve the left favicon, stable title ellipsis, and reserved 24 DIP right close slot; animation must not alter hit geometry. Close-glyph opacity may interpolate; the tab has only one hover background. All glyphs remain Phosphor Bold; the asset worker supplies `check-bold` at the required DPI variants and the icon loader naming mapping. `Icon` needs an opacity argument/DrawBitmap opacity to crossfade; optional scale is centered within the existing glyph rectangle. Restore transforms/brush state after drawing.

For real download events, draw a slim D2D progress track within the existing downloads control/chrome strip, filling `trackWidth*visual.progress` only when determinate. Completed may draw `check-bold` using `visual.completion`. When no operation events are hooked, the ordinary downloads button remains, with no progress track, success check, or simulated activity. The model is not an event hookup and does not establish that downloads work.

## Timer and invalidation contract

Reserve a motion timer ID distinct from smoke timer `1`. Existing `WM_TIMER` routes all timers into smoke failure when `smoke` is set: dispatch the new ID before that branch, and restrict the timeout path to ID `1`. Keep local optional state for the armed hint so that unrelated paints do not repeatedly reset a pending timer. Recompute after input/model/preference changes and after each motion timer dispatch.

```cpp
// Called after an event, using the same timestamp as its model updates.
auto wake = motion.NextWake(now);
KillTimer(window, MotionTimerId);
if (wake.delay) SetTimer(window, MotionTimerId,
    static_cast<UINT>(wake.delay->count()), nullptr);
```

Win32 timers are recurring: on `WM_TIMER(MotionTimerId)`, kill first, invalidate the chrome strip once (including the final expiry frame), then recompute from a fresh monotonic timestamp and arm only if needed. A delayed timer samples current time directly, so there is no accumulated frame drift. `NextWake` is pure; there is no `Tick` required to retire state. At idle/terminal endpoints it reports no timer even after arbitrarily long elapsed time. Dwell deadlines do not need repeated paints. If `SetTimer` fails, snap/clear transient visuals and invalidate once rather than leave an unfinished transition.

Use a physical-pixel RECT limited to `0..clientWidth, 0..S(84)` (confirm actual chrome height in current `Layout`) with `InvalidateRect(window, &chromeRect, FALSE)`, never the full client for motion timer events. Existing native paint must clip the Direct2D target to that strip for these paints; `target->Clear` alone ignores Win32 invalidation clipping. Do not invalidate or resize web content on animation frames. Kill the motion timer on `WM_DESTROY`, before engine stop/halt, and while minimized; on restore sample fresh time, invalidate chrome once, and reschedule only if still active. Input-triggered repaint is still required in reduced-motion mode, even when `NextWake` is empty.

## Native-only verification

Run from the repository root:

```powershell
& tools/w64devkit/bin/g++.exe -std=c++17 -Wall -Wextra -Werror -pedantic -static tests/motion/motion_test.cpp -o tests/motion/motion_test.exe -luser32
& tests/motion/motion_test.exe
```

Tests supply deterministic timestamps and verify phase boundaries, easing midpoints, exact settled endpoints, repeat target/retrigger behavior, reduced-motion policy changes, real progress/unknown totals, duplicate completion events, and no perpetual ticking. They contain no WebView2 includes or browser launch. Browser/download-dialog validation remains paused because the required stop-on-WebView2-problem condition was already reached; this module does not retry or diagnose it.

Verification on 2026-11-06: the command above compiled cleanly with warnings treated as errors and reported `PASS: 54 deterministic native motion checks; no browser/WebView2 used.` The full scheduler simulation needed 20 frame wakes plus one dwell wake, then stopped; reduced motion needed one deadline wake. The generated test executable is a disposable local build output, not a required source artifact.
