# Tab open/close motion

Branch `VALXXII/animation-ouverture-fermeture-onglets`, rebased on `main` f3e21df (split view, 0.3.121).
Native only: `src/tab-motion.h` (model), `Motion::Spring` in `src/motion.h`, the renderer in
`src/chrome.h` and the host hooks in `src/main.cpp`. This supersedes the line in
`motion-polish.md` that said removed tabs disappear immediately with no layout slide.

## Behaviour

| Event | Presentation |
| --- | --- |
| Open (Ctrl+T, +, popup, reopen) | The tab grows from zero width; neighbours and `+` are pushed aside. Content (favicon, title) fades in once the surface is about one third open. Spring response 0.30 s, damping 1.0 (no bounce). |
| Close (×, middle click, Ctrl+W, context menu) | The tab stays in its old place, at full width, as a ghost that cannot be clicked, and blurs out: Gaussian blur 0 → 4 DIP and opacity 1 → 0 over 150 ms ease-out (better-ui exit values). Neighbours and `+` glide over it to close the gap (spring response 0.22 s). Without Direct2D 1.1 the ghost only fades. |
| Rapid repeats and reversals | Each event retargets from the value *and velocity* on screen. Nothing restarts from a logical value, and closing a tab that is still opening reverses it in place. |
| Width unlock after pointer closes | The shared tab width glides to its new value (`animateTabLayout`). |
| Resize, DPI change, wheel scroll, selection scroll, drag reorder | The layout snaps immediately, so direct manipulation never lags. |
| Restored session and first tab | Settled at once: no entrance cascade and no selection fade. |
| Reduced motion (Windows or the app setting) | Geometry is final at once. A new tab's opaque fill and content cross-fade over 150 ms; a closed tab disappears at once. Turning reduced motion on while a tab is moving settles it. |

Live surfaces stay opaque. They are the existing `Surface::canvas`, `tabHover` and `selected`
tokens, interpolated as colours. Only the closing ghost becomes transparent, during its blur-out.
It is drawn once per frame into a cached compatible layer, with the fill it last showed, and then
composited through `ID2D1Effect` (CLSID_D2D1GaussianBlur). Ghosts are painted beneath live tabs.

## Functional state is never delayed

`pages`, `current`, focus, WebView visibility and accessibility change synchronously. Only the
drawing is animated. Hit regions are built from the geometry drawn in the last frame, so a
click lands on what is visible: the `+` button can be clicked again while it is still moving.
A ghost exposes no region. An opening tab exposes its close target only once the glyph is fully
revealed. Selection fades are keyed by the stable `Tab::serial`, not by index, so index reuse
can no longer pass a departing tab's state to its neighbour. This removes the earlier
`selection.Reset()` calls.

## Frames

`chrome.tabMotion.NextWake` feeds the existing `ScheduleMotion` / `InvalidateMotion` paths
(display-paced FrameClock, full-header invalidation while moving, endpoint frame painted). When
a `Build` starts motion, the host schedules frames (`TakeTabMotionStarted`). The springs settle
to their exact targets, after roughly 0.4 s when opening and 0.3 s when closing. After that
there are no wakes, and settled geometry is bit-identical to the static layout. Text layouts
keep the settled tab width while the strip moves, so animation frames do not rebuild DirectWrite
layouts.

## Verification

```
g++ … tests/motion/tab_motion_test.cpp        # PASS: 60 deterministic tab motion checks
g++ … tests/ui/tab_motion_render_test.cpp     # PASS: 217 render checks (96/144 DPI)
```

Both are in `test-ui.ps1`. The model test covers the following:

- spring continuity of value and velocity on retarget
- no overshoot from rest, exact settling and finite wakes
- opening and closing geometry, contiguity and ghost placement
- closing that is shorter than opening
- the blur-out exit: monotonic ease-out, done in 150 ms before the slot closes, and a revived tab that un-blurs from where it was
- close-during-open reversal
- rapid opens and closes, including with a locked width
- a revived ghost
- resize snap, reflow glide and reorder
- overflow entry and exit (view within content, scroll within range)
- reduced motion, a preference change mid-flight and the restore settle

The render test covers the following:

- `+` hit test equals the drawn position on every frame
- no hit target for ghosts or unrevealed close glyphs
- a ghost that starts sharp with its selected fill, is partly transparent mid-exit through the Gaussian blur path (not the fade fallback), and is gone after 150 ms
- settled regions equal the static layout exactly at 96 and 144 DPI
- no animation on resize
- the reduced-motion in-place cross-fade

The existing motion, polish, nav-motion, UI, address, hover/favicon, design-system, tab-drag, shortcut, split-view, bookmarks, history, downloads, status-bubble, navigation-error and
accessibility fixtures still pass. The stamped release build compiles without warnings.

The interactive preview is the unstamped build `build/tab-motion-preview/browser.exe`. Its
portable profile, session and log sit beside the exe. It has no self-update and does not touch
the installed browser or its data.
