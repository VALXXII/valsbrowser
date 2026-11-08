# Update toast — native design spec

Source of truth: Paper file *Browser Design*, page 1. Boards **New update download** (Available),
**New update downloading**, **New update install** (Ready), **New update install** (Installed), and the placement
board *Lisière — prototype actuel V2*. Implementation: `src/update-toast.h`, `src/update-toast.cpp`,
`src/update-toast-motion.h` (C++17, Direct2D/DirectWrite, Win32; no web UI, no new UI engine).
Integration API: `build/coordination/update-toast-contract.md`.

## Exact Paper values (DIP)

| Element | Paper value | Native |
| --- | --- | --- |
| Card | width 269, border-box, padding 16/16, gap 16, radius 10, border 1 `#FFFFFF0D`, bg `#171717` @ 50 % + `backdrop-filter: blur(8px)` | same geometry; material see *Deviations* |
| Heights | Available 153.59375, Downloading 139.59375, Ready 153.59375, Installed 105.59375 | reproduced exactly by `Layout()` (tested); Paper text frames are 0.40625 shorter than their line box, kept as `textTrim` |
| Title row | 16 px Phosphor Bold icon @ 50 %, gap 6, Inter Display Medium 16 / line 14, tracking −0.01 em, `#FFFFFF80` × 0.9, features `cpsp cv12 tnum` | same (DirectWrite typography features, character spacing, uniform CSS line box) |
| Body | Inter Display Regular 16 / line 20, tracking +0.01 em, `#FFFFFF80` × 0.9, `text-wrap: balance` | same; balance = narrowest width that keeps the greedy line count (reproduces Paper's breaks) |
| Button row | gap 8, buttons `flex:1`, height 32, radius 6, `#292929`, padding-inline 8, Inter Display Medium 14 / 14, −0.01 em, white × 0.5, centred, 1-line clamp | same; ellipsis trimming; hover/press from design-system `Raised` (`#333333`, `#3E3E3E`) |
| Download stats | 14 px arrow-down + size, 14 px timer + ETA, Inter Display Regular 14 / 14, gaps 4; track 4 high, white 10 %, pill; fill white 50 % | `50.4/200.0MB`: one decimal always; tabular digits; compact, left-aligned groups. The size slot reserves the measured width of `000.0/000.0MB`, with only 4 DIP before the timer, independent of counter values. ETA starts 4 DIP after its icon. Unknown ETA uses `--:--`. Narrow cards trim the size to preserve the ETA. Values remain real. |
| Placement | card `left: 14`, `bottom: 14.447` of the 1180 × 815 window | owner client left + 14, bottom − 14 (snapped to whole DIP / device px) |

Texts are verbatim: *New update available / A new update is ready to be downloaded. / Download*;
*Downloading... / <size> + <ETA> / Cancel*; *Ready for install / The update is ready to be installed. / Restart and install*;
*Update installed / The new update has been successfully installed.*

Icons are the Phosphor Bold path data copied from the Paper JSX (info, download-simple, check-circle, arrow-down,
timer) and compiled as Direct2D path geometries (bounded cache of five). Phosphor's MIT license already ships in
`assets/icons/phosphor-bold/LICENSE`; no new asset files were added.

## Functional extensions (no Paper board — same visual language)

| Phase | Card |
| --- | --- |
| `Hidden`, `Checking` | no card. A background check never interrupts. |
| `Installing` | download icon, *Installing...* / *The update is being installed.*, no button (Installed layout). |
| `Error` | info icon, *Update failed* / backend message (short French string, verbatim, ≤ 140 chars) or *The update could not be completed.*; **Dismiss** + **Retry** in the Paper two-column button row. |
| Unknown total | stats row shows the received size only (`3.3MB`), empty track, no ETA. No indeterminate animation loop (keeps zero idle frames; the changing number is the liveness cue). |
| Unknown ETA | timer group omitted. Narrow cards drop the ETA rather than overflow. |

`Installed` is shown only when the host sends `Phase::Installed` (the real post-restart success acknowledgement).
It dwells 6 s (paused while hovered or keyboard-focused, +2 s after leaving), then exits and reports `Dismiss` once.
No state that needs an action is ever dismissed on a timer.

## Overlay

Owned layered popup (`WS_POPUP`, `WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW`, owner = main window) presented
with `UpdateLayeredWindow` from a premultiplied DIB rendered by an `ID2D1DCRenderTarget` (grayscale text AA). It sits
above the WebView2 child windows of its owner, below unrelated applications, never takes activation on click
(`MA_NOACTIVATE`), and is click-through outside the rounded card (alpha 0 pixels + `HTTRANSPARENT`). The window rect
is the card only, so it cannot block other browser areas or the right-hand downloads drawer (`SetReservedRight`).
It hides with the owner on minimise, follows owner move/resize/DPI via `Layout()`, and keeps the card on the
monitor work area when a custom-frame window is maximised.

## Motion

Driven by `ToastModel::Presenter` (pure, deterministic, tested). Every change retargets from the **presentation
value and velocity**; springs are critically damped (Apple damping 1.0), so nothing bounces; a retarget that would
cross its target is clamped. Frames come from a dedicated instance of the existing display-paced
`NativeChrome::FrameClock::Clock` (compositor clock → DWM vblank fallback): requested only while something moves,
worker blocked on an event when idle (**zero idle wakes**, verified by the window test). The only `WM_TIMER` is the
one-shot Installed dwell; a 16 ms timer is used solely if the frame clock cannot start at all.

| Transition | Normal | Reduced motion (Windows "Show animations" off) |
| --- | --- | --- |
| Enter | opacity 0→1 and translateY +12→0 DIP, spring response 0.25 s (≥ 99 % at 300 ms, settled < 370 ms) | opacity only, 200 ms |
| State swap | same card (identity kept); height springs with the **bottom edge anchored**; content is laid out from the bottom so the action button stays in place (Download → Cancel → Restart and install); outgoing content fades in 100 ms ease-out, incoming opacity = spring² so two texts are never legible together; incoming title icon scales 0.25→1 | height snaps; 200 ms cross-fade; no scale |
| Exit | opacity →0 and translateY →+12 DIP (leaves by its entrance path), 150 ms ease-out; input ignored | opacity only, 150 ms |
| Button | hover 120 ms, press `#3E3E3E` + scale 0.96 over 150 ms ease-out (design-system `PressScale`) | no scale |
| Progress | spring toward the real fraction, never past it; a restart snaps back | snaps |

Static cues always carry the state (title, icon, label, track); motion is never the only signal. No sound.

## Input and keyboard

* An action is bound at pointer-down and fires on pointer-up only if the same content generation and button are
  still under the pointer: a phase change during a press can never turn *Download* into *Cancel*. The same action
  fires at most once per state.
* Input is never locked during enter or state transitions; it is ignored only during the 150 ms exit.
* Keyboard: the host calls `Focus()` (e.g. F6 cycle). Tab / Shift+Tab / arrows move between buttons, Enter
  activates, Space activates on release, Esc returns focus through the host's release handler (and dismisses
  *Installed*). Focus ring 1 DIP white at 50 % (keyboard) / 10 % (pointer), per design-system `Focus`.
* The popup's window text is "title. body" for screen readers that read window names. Full UI Automation is not
  provided (limitation).

## Deviations from Paper (justified)

1. **Material (user override)**: solid `#171717` at **100 %**. Acrylic and blur are removed.
   A single non-layered owned popup draws with an opaque Direct2D HWND render target; the system backdrop
   is explicitly disabled and the DWM frame is not extended into the client area. The 10-DIP window region
   clips the whole card after sizing. The background stays solid during content fades, and the card keeps
   its existing movement, layout, border, keyboard handling and minimize behavior.
2. **Bottom inset** 14 instead of 14.447 DIP (Paper canvas artefact), so the card's bottom edge lands on whole
   device pixels; heights keep Paper's fractional values and are rounded to device pixels at present time.
3. **Minimum width** 224 DIP (narrowest card keeping *New update available* on one line). Narrower clients (or a
   client too short for the card below the 84 DIP chrome) hide the card and keep its state until it fits again.
4. **Exit direction**: better-ui's default exit is −12 px (up); the toast exits +12 DIP (down), the path it entered
   by (Apple spatial consistency), since it is anchored to the bottom edge.
5. **Text scale**: Windows "Text size" accessibility scaling is not applied (the chrome does not apply it either);
   DPI scaling 100–250 % is covered by tests and snapshots.

No Paper edits were made; specimens for the extension states (Installing, Error, unknown total) are pending root
consolidation on the design-system page.
