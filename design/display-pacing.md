# Display-paced native animation

Status: implemented in the working tree (uncommitted). Native chrome only: no browser/WebView2 was launched or loaded for any of this work.

## Root cause (URL loading fill looked ~20 FPS on a 240 Hz panel)

1. **Timer pacing.** `Motion::Frame()` asked for 16 ms and `ScheduleMotion` used `SetTimer`/`WM_TIMER`. USER timers are rounded to the system tick (~15.6 ms by default), so a 16 ms request fires after about 2 ticks (~31 ms). `WM_TIMER` is also synthesised only when the queue has no other messages, and the timer was killed and re-armed every frame. Measured old path: median **28.6–30.6 ms** between paints (~33–35 paints/s) on both available displays, independent of the refresh rate.
2. **Blocking present.** The `ID2D1HwndRenderTarget` used `D2D1_PRESENT_OPTIONS_NONE`, which per the docs "waits until the display refreshes to present". Measured synchronous `Chrome::Paint` cost: **8.3 ms median** with NONE (UI thread blocked on vblank) against **0.38 ms** with `IMMEDIATELY`.
3. **Per-frame render work.** Each frame invalidated the whole 84-DIP header and recreated every DirectWrite text format, layout and typography (tab titles + URL): 1,554 text layouts in 6 s of loading animation.

## Implementation

- **`src/frame-clock.h` (new)**: an on-demand frame clock driven by a requestAnimationFrame-style request. One worker thread (64 KB stack reservation) blocks `INFINITE` on an event while nothing animates. On `Request()` it waits for one compositor tick, then posts one `WM_APP+8` to the chrome window.
  - Source 1: `DCompositionWaitForCompositorClock` (dcomp.dll, Windows build 22000+, resolved with `GetProcAddress`). It follows the compositor heartbeat, including refresh-rate and dynamic-refresh-rate changes. The stop event is passed in as a wait handle, so shutdown interrupts it.
  - Source 2 (fallback): `DwmGetCompositionTimingInfo` (re-read every frame, so monitor/rate changes apply) plus a `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION` waitable timer aimed at the next estimated vblank. This does **not** change the global timer resolution. There is a hard 1 ms floor. If timing info is invalid it assumes 60 Hz.
  - The clock demotes itself to source 2 after 8 consecutive non-tick results. Per the docs it returns immediately when the display is off, so every failed wait is paced by source 2 and can never spin.
  - **Bounded notifications**: `pending` is an atomic flag. At most one frame message is ever outstanding, and ticks during a pending frame are counted as `coalesced`, never queued. `Cancel()` covers idle, deadline-only dwells, minimise and halt. `Stop()` signals stop and joins with a 2 s cap. The worker owns a `shared_ptr` to its state, so a timeout can never free handles it still uses. If `PostMessage` fails (window gone) the worker exits and posts `ClockLost`; the UI then uses the SetTimer fallback.
  - Fallback when the worker cannot start: the previous SetTimer path. It is kept, safe and documented as **not** display-synchronised.
- **`src/main.cpp`**:
  - `ScheduleMotion` routes frame wakes, and deadlines due within one nominal frame, to `frameClock.Request()`. Longer deadline-only dwells (copy check dwell) keep one one-shot `SetTimer` with the worker dormant.
  - Minimised, halted (`Fail`), or no wake at all means `Cancel` + `KillTimer`.
  - `WM_DESTROY` stops the clock before tabs are torn down.
  - Frame handler: `Acknowledge`, then `InvalidateMotion`, then reschedule. Painting stays in normal-priority `WM_PAINT`, after input.
- **`src/chrome.h`**:
  - `presentOptions = RETAIN_CONTENTS | IMMEDIATELY`. There is no vblank block on the UI thread, and partial invalidation keeps the retained back buffer (docs: RETAIN_CONTENTS "does not discard the frame upon presenting"). DWM composes, so there is no tearing in windowed mode.
  - Bounded text-layout cache: 96 entries, cleared in full when full and on `Dispose`. It holds DirectWrite layouts, which are device-independent, keyed by text, slot size, font and tracking.
  - Paint culls controls wholly outside the dirty rectangle (4-DIP margin for focus rings).
  - `LoadingDirty()` gives the URL-surface repaint rectangle.
  - `LoadingSignature()` is the quantised on-screen identity of the fill. Loading-only frames whose signature matches the last painted one are skipped, since the pixels would be identical. Edge buckets are quarter-pixel and aligned to both integer and half-pixel coverage boundaries, with a 1/256 px guard band near boundaries. Opacity is not quantised, so every fade frame repaints.
- **Invalidation policy**: frames with only the loading fill moving repaint just the URL surface (bar plus embedded reload/copy buttons). Any other motion, and the frame right after it ends so its endpoint is painted, repaints the whole header as before.

Unchanged: loading semantics (15/65/100 % milestones, redirect retention, stale-completion guard, per-tab state), `.08` fill, 360 ms CSS ease-out, 200 ms fade, reduced motion behaviour, icons/branding/tabs/focus, downloads code and assets.

## Supported / fallback behaviour

| Situation | Behaviour |
|---|---|
| Windows 11 (build ≥ 22000) | compositor clock; one paint per compositor tick while something moves |
| dcomp export missing / repeated failures | DWM vblank estimate + high-resolution waitable timer (measured equal cadence here) |
| Worker cannot start / window lost | legacy SetTimer path (~30 ms cadence; not display-synchronised) |
| Idle, or waiting for the next navigation milestone | worker blocked on an event; no timer, no ticks |
| Copy dwell (deadline only) | one one-shot SetTimer; worker dormant |
| Minimised | requests cancelled; no paints; resumes on restore (`WM_SIZE`) |
| Occluded / other virtual desktop | not special-cased: only active interpolation requests frames; repeated navigation events can extend activity |
| Display off | compositor wait returns immediately per docs, so the frame is paced by DWM timing, then the clock demotes |
| Reduced motion | unchanged: width jumps; only the 200 ms opacity fade requests frames |
| Close | `WM_DESTROY` stops and joins the worker (fixture: < 1 ms) |

## Verified preview and scope of measurements

The user approved the interactive native preview and requested no further performance investigation. The follow-up worker was explicitly stopped; no new benchmarks are required to finish this release.

Measured in the standalone native harness: paint-completion interval median8.34ms /p95 8.66ms on the active120Hz display, versus median29.33ms /p95 33.60ms on the old timer/presentation path. These are CPU-side paint-completion timestamps, not measured scan-out or photons. Old median intervals and notification-count rates use different denominators and must not be called the same FPS metric.

Idle/minimised validation: zero ongoing frame wakes and paints after the transition. Existing scheduler/render fixtures pass. The interactive preview uses the production Chrome, Loading and FrameClock implementations, loads no WebView2 and animates only on replay.

The original CPU percentages and240Hz CPU projection are WITHDRAWN: converting QueryProcessCycleTime counts using an rdtsc/reference-clock rate does not establish CPU time under frequency scaling. Short GetProcessTimes samples were inconclusive. The native harness now reports OS-accounted user+kernel time and raw cycle counts separately with API error checks; the longer follow-up measurements were cancelled at the user's request. No reliable total CPU/GPU percentage or240Hz cost is claimed. dwm.exe cost was not measured.

The panel was in120Hz mode during the verified runs.240Hz remains unmeasured; display settings were unchanged. Animations stop when their interpolation ends, but repeated navigation/redirect events can extend activity; there is no unconditional per-navigation time cap.

## Known limits

- With several monitors, the compositor clock ticks at the system compositor cadence (120 Hz here). A window on the 100 Hz display received 120 notifications/s, so about 1 in 6 paints there is never scanned out. Per-target statistics (`DCompositionGetTargetStatistics`) could refine this later.
- The harness measures UI-thread notification and paint-completion times, not photon or scan-out times.
- The live browser was not run (runtime hold). The scheduler path in `main.cpp` matches the harness logic, but end-to-end WebView2 navigation pacing is unverified.

## Final local installation

Installed build/v2/browser-display-paced.exe, SHA2569D7F892EEFF834679CAF2E100A98C6A21DEDC57473C739E251614598938F0C01. Start Menu browser.lnk target and working directory verified; all168 payload files match the isolated package. Existing build/v2/profile and previous executables retained. No browser/WebView2 launch or user process termination. The follow-up assignment was confirmed stopped at the user request; no further benchmark work remains pending. The corrected native harness compiles; it was not rerun.
