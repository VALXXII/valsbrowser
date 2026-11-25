# Mode Focus — the edge that recalls the side column

Owner's request (25 Nov 2026): in Mode Focus, the screen-edge zone that brings the hidden side column back (#143 side
layout, and the compact column) is too hard to hit. Before this change it was an 8 DIP band inside the window's left
edge, on which the pointer had to rest 400 ms, and the column left the instant the pointer was off it.

Code: `src/sidebar.h` (`EdgeZone`, `FocusReveal`), pure and tested in `tests/sidebar_test.cpp`; the window side is one
function, `SideFocusEvaluate` in `src/main.cpp` (device pixels in, nothing else).

## What the others do

| Product | Edge zone | Before opening | Closing | Window not maximized / 2nd monitor | Status |
| --- | --- | --- | --- | --- | --- |
| **Dia** (The Browser Company), sidebar hidden | the window's left edge | about **0.1 s** | when the pointer leaves, after the same ~0.1 s | the zone is the **window's** edge | **Inferred, strong**: DiaTweaks' « Open Sidebar Instantly » patch removes « Dia's short wait … about a tenth of a second » (a constant it patches in Dia's code, so read from the binary, not documented). The edge reveal itself is confirmed by DiaTweaks issues. No official Dia doc found. |
| **Arc**, hidden sidebar (⌘S / Ctrl+S) | the window's left edge, **and the overshoot past it** (the pointer landing left of a window that is not at the screen's edge, e.g. a right-half window) | not measured | stays out with the pointer **≈ 21 pt off its edge** | the zone follows the window, the overshoot counts | Shortcut: **confirmed** (Arc help center). Overshoot + 21 pt: **measured by a third party** (the Ardia project, from Arc screen recordings, PR mantrakp04/ardia#11, which ships 24 pt for both). |
| **Edge**, vertical tabs collapsed | the collapsed tab pane itself (an icon column, not an edge band) | fixed, not configurable | when the pointer leaves the pane | the pane is in the window | « by design, can't change » (Microsoft Q&A answer): **confirmed** that the delay is fixed; its value is not published. Edge's sidebar auto-hide: a setting existed (Tech Community threads), its timing is undocumented. |
| **Firefox**, sidebar « expand on hover » | the collapsed sidebar | **200 ms** (pref `sidebar.animation.expand-on-hover.delay-duration-ms`, Firefox 145) | when the pointer leaves the sidebar; open bugs about leaving « from the wrong side » and flicker | — | **Confirmed** (Mozilla bugs 1993806, 1983125). |
| **Zen** (Firefox-based), compact mode | the side edge of the window | — | `zen.view.compact.toolbar-hide-after-hover.duration` keeps it after the pointer left | — | Pref names **documented** (Zen docs and a docs pull request), defaults not stated. |
| **macOS Dock**, auto-hide | the **screen's** bottom (or side) edge: the pointer is stopped by the edge (infinite target) | `autohide-delay` ≈ **0.5 s** by default (community; set 0 to remove) | hides when the pointer leaves | between two displays: the Dock moves to the display whose bottom edge the pointer **pushes** and holds there (about 2–3 s, per guides); a display stacked above another cannot get it | Default delay and multi-display behaviour: **community sources only** (Apple Community threads, MacPaw / Setapp guides), no Apple doc found. |
| **Windows taskbar**, auto-hide | the screen edge | — | — | one auto-hidden bar per screen side; on extra monitors it often fails to unhide (forum reports) | Forum reports only. Windows' own **hover time is 400 ms** (`SPI_GETMOUSEHOVERTIME` default). |

Sources: [DiaTweaks wiki, Open Sidebar Instantly](https://github.com/JaceThings/DiaTweaks/wiki), [DiaTweaks issue 541](https://github.com/JaceThings/DiaTweaks/issues/541),
[mourato/DiaTweaks issue 1](https://github.com/mourato/DiaTweaks/issues/1), [Arc help: hide the sidebar](https://resources.arc.net/hc/en-us/articles/25619487530519-How-Do-You-Hide-the-Sidebar),
[Ardia PR 11 (Arc's peek measured)](https://github.com/mantrakp04/ardia/pull/11), [Ardia PR 768](https://github.com/ardia-browser/ardia/pull/768), [Ardia PR 2935 (Windows edge)](https://github.com/ardia-browser/ardia/pull/2935),
[Edge vertical tabs delay (Microsoft Q&A)](https://learn.microsoft.com/en-us/answers/questions/443522/how-to-make-edge-vertical-tabs-appear-slower-than),
[Firefox bug 1993806](https://bugzilla.mozilla.org/show_bug.cgi?id=1993806), [Firefox bug 1983125](https://bugzilla.mozilla.org/show_bug.cgi?id=1983125), [Firefox bug 1985692](https://bugzilla.mozilla.org/show_bug.cgi?id=1985692),
[Zen about:config flags](https://docs.zen-browser.app/guides/about-config-flags), [Zen compact mode](https://docs.zen-browser.app/user-manual/compact-mode),
[Dock delay (TIL)](https://benji011.github.io/til/2025-09-17-how-to-make-auto-hide-show-for-the-dock-faster), [Dock between displays (Setapp)](https://setapp.com/how-to/how-to-move-dock-on-mac-to-a-second-monitor), [Dock on stacked displays (Apple Community)](https://discussions.apple.com/thread/254783024),
[Windows multi-monitor auto-hide (Tech Community)](https://techcommunity.microsoft.com/t5/windows-insider-program/windows-11-multi-monitor-taskbar-issue/td-p/2814918).

What explains the owner's trouble on this PC (inferred from the setup, to be confirmed in the window check): the second
monitor sits at negative coordinates, beside the main one. A maximized window's left edge is then **not a wall**: the
pointer flies through the 8 DIP band (10 px at 125 %) onto the other monitor, and resting 400 ms inside those 10 px is
hard. Arc's answer — the overshoot past the window's edge counts — is the one that fixes it.

## What we do (values in DIP of the window's monitor, per-monitor DPI)

Depth = distance from the window's edge on the column's side, inward (negative = beyond the window). The pointer counts
only while the window (or one of its popups) is in front, not minimised, in the column's rows (under the annotation banner).

| Part | Where | Waits | Why |
| --- | --- | --- | --- |
| **Monitor edge** (pinned) | the monitor's outer pixel, no monitor beyond, the window's edge on it | **60 ms** (two 30 ms samples) | the pointer is stopped there: clear intent; two samples so a fling that grazes the edge once does nothing |
| **Edge** | the 4 DIP gutter between the window's edge and the page (maximized) + **24 DIP beyond** the window's edge (the next monitor; the desktop or the app beside a restored window) | **120 ms** of it | Dia's ~0.1 s graze wait; Arc's overshoot, 24 DIP like Ardia's measure of Arc (21 pt). Crossing 4 + 24 DIP to the next monitor at ≥ 250 DIP/s takes < 120 ms: an ordinary move to the other screen does not open it |
| **Rest** | the page's first 12 DIP (16 from the edge) and, on a restored window, the 4 DIP gutter (it is the window's resize border) | **400 ms** | Windows' hover time and the old value; 12 DIP = the overlay scrollbar's own strip (`Scrollbar::hitBand`), the browser's thinnest grab size; the resize border waits so aiming to resize does not pop the column |
| **Scrollbar side** (the right, when the column is there) | only the gutter, the monitor edge and the 24 DIP beyond; the page's 12 DIP are the overlay scrollbar's strip | **400 ms** for all | a pointer thrown past the scrollbar comes back onto it in less; the scrollbar never opens the column |

- **Presses.** Any press disarms the edge until the pointer has been seen off it, buttons up: a scrollbar drag (or a text
  selection) released on the edge, a click on the page's first pixels, or Échap / the shortcut with the pointer on the
  edge never bring the column back by themselves.
- **Closing.** Shown, the column stays while the pointer is over it or **within 24 DIP of it** (its rows, and the 24 DIP
  beyond the window's edge), and leaves **300 ms** after the pointer left that (the top bar's grace, `FocusMode::graceMs`;
  Dia ~0.1 s, the Dock ~0.5 s). A **press outside** sends it at once (the click is the page's). A drawer, a menu, a drag,
  the address field still hold it; Ctrl+Maj+, still pins it.
- **Compact.** Same zone and timers; the hold margin follows the shown column: 40 + 1 + 24 = 65 DIP compact, 248 + 1 + 24
  complete (the column's actual width when resized).
- **Right side.** Ready in the logic (`EdgeZone::Side::Right`, mirrored, scrollbar rules) and tested; the product has no
  right-side placement yet (`TabsPosition` is Top / Side / Compact), so `SideFocusEvaluate` passes `Left`.
- **DPI.** Measured in device pixels (screen coordinates, the process is per-monitor-v2), read in DIP at the window's DPI
  as `px × 96 / dpi` (exact on whole DIP). At 125 %: edge 5 px, zone 20 px, beyond 30 px; at 100 %: 4 / 16 / 24 px.

The log says what brought the column: `FOCUS edge zone: monitor edge 60 ms | edge 120 ms | rest 400 ms`, next to the
existing `FOCUS column in (left edge)`.

## Tests

- Windowless (`tests/sidebar_test.cpp`, run by `test-sidebar.ps1`): hit test of every part, left and right, the scrollbar
  side, the resize border, rows / banner, another app in front; the hold margin complete and compact; pixels → DIP at
  96 / 120 / 144 / 192 dpi, the monitor edge with and without a monitor beyond, a restored window, a window half off
  screen; every timer (60 / 120 / 400 / 300), the sweep to the next monitor, presses, Échap, the shortcut, and one chain
  from pixels to open / hold / close. `tests/shortcuts_test.cpp` (windows never shown) still passes.
- In a window (not run: they move the real pointer). **Short, in a daytime screen slot** (≈ 5 min, the test browser
  maximized on the main monitor, Focus on, side layout):
  1. pointer slammed left onto the other monitor and left there: the column comes within ~0.15 s (log « edge 120 ms »);
  2. same in the compact layout;
  3. back over the column, then 20 px right of it: it stays; 60 px right of it: it leaves after ~0.3 s.
- **Night run (3:00), long click series** (SetCursorPos / SendInput on our own window, throwaway profile, fake agent):
  1. sweeps from the page to the left monitor at 150, 250, 500, 1500 DIP/s: opens only at 150 (slow), never at ≥ 250;
  2. a restored window in the middle of the monitor: overshoot onto the desktop → opens; resting on the left resize
     border 400 ms → opens, and a resize drag from that border still resizes the window, column shown or not;
  3. scrollbar drag (page with a long document) released past the right edge and past the left edge: never opens; a
     click in the page's first 10 px then resting: never opens until the pointer left the edge;
  4. column shown, click on the page at 100 px right of it: it leaves at once and the page gets the click;
  5. Échap with the pointer on the edge: stays away; off the edge and back: comes;
  6. the window on the 100 % portrait monitor (per-monitor DPI): the edge is 4 px / the zone 16 px / beyond 24 px there;
  7. window snapped to the right half: overshoot into the app on the left half opens it (in front only);
  8. annotation banner on: nothing above the column's top (the banner), the column comes under it;
  9. a drawer opened from the column, a context menu on a tab: the column stays the whole time;
  10. Ctrl+Maj+, in / out and Ctrl+L in compact (the complete column): unchanged.

## Shared files with VALXXII/focus-transition (Mode Focus motion)

Pushed state of that branch (3 commits, last 2026-11-25 00:07): it touches `src/main.cpp`, `src/chrome.h`,
`src/content-transition.h`, `src/focus-motion.h`, tests and design notes. **Only `src/main.cpp` is shared**, in different
sections (theirs: the top bar's entry / exit, `FocusEvaluate`, `FocusPlace`, `ContentBox`, timers; mine: the body of
`SideFocusEvaluate` only). `git merge-tree`: that branch, as pushed, already conflicts with today's main (4 places in
`src/main.cpp`, 1 in `src/chrome.h`: it predates the later merges); this branch adds no conflict of its own (the same
hunks with or without it, none in `SideFocusEvaluate`). If the side / compact transitions in progress
edit `src/sidebar.h`, the touching points are the `FocusSide` comment (4 lines rewritten here) and the removed
`FocusSide::edge` constant; the motion values (`enterResponse`, `exitResponse`, `reducedFadeMs`) and `SideRevealChanged`,
`SideSlideFrame`, `SidePicture*` are untouched.
