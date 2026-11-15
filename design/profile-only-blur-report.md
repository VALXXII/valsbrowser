# Blur only on actual profile switches

Request (user): « remove le blur in and out à chaque changement d'onglet et l'appliquer uniquement au changement de profil ».

Base: `origin/main` d060366 (0.3.294). Branch `VALXXII/profile-only-blur`.

## Behaviour

| Action | Before | After |
| --- | --- | --- |
| Tab switch in the same profile (click, Ctrl+Tab, Ctrl+1…9, UIA `SelectionItem.Select`, close the active tab, new tab, programmatic `Select`) | outgoing snapshot + 3 DIP blur, readiness poll, incoming blur-in / fade (140 ms) | **immediate**: no capture, no overlay, no readiness poll, no timer |
| Actual profile switch | outgoing blur + incoming sharpen, 210 ms / 4 DIP; reduced motion 140 ms opacity only | **unchanged** |
| Same profile re-selected, profile colour / name edited | nothing | nothing |
| Tab selected **during** a profile transition | a *tab* transition retargeted the overlay: the old image (and the incoming snapshot of the previous tab) stayed over the chosen tab | the transition is **cancelled at once**: overlay removed, both snapshots released, readiness and end timers killed |

Chrome tab-indicator motion (`motion.SelectTab`), loading, layout and resize behaviour are untouched.

## Change (narrow)

`src/main.cpp`
- `Select(int)`: the `BeginContentTransition(Kind::Tab)` / `RevealContentTransition()` pair is removed. Instead, when the
  tab to show differs from the one `Select` showed last (compared by `Tab::serial`, since closing / moving tabs shifts
  indices), a running transition is cancelled — unless the selection belongs to a profile switch.
- `CancelContentTransition(why)`: `Overlay::Cancel()` + `KillTimer` of `ContentReadyTimerId` and `ContentTransitionTimerId`
  (no pending readiness poll or end timer is left to wake an idle window). `ContentTransitionInput` (first click / key
  in a page) now goes through it too.
- `KeepContentTransition` (a scope counter): the internal selections that complete a profile switch keep its transition:
  `RestoreSession()` (a cold profile's tabs, restored when its engine is ready, while the switch overlay waits for it).

`src/profile-workspace.h`
- `WorkspaceShown()` (inside `SwitchWorkspace`: re-selects the incoming profile's own tab) and `OpenSessionTabHere()`
  (tab preview: switch to another cached profile, then that tab) select under `KeepContentTransition`.

Not changed: `src/content-transition.h` (the generic overlay keeps `Kind::Tab` and its timing; its unit tests in
`tests/ui/profile_ui_test.cpp` still use it), the profile timing (210 ms / 4 DIP, reduced 140 ms opacity), the GPU
failure / capture budget fallback, generation checks, private-window policy (`SwitchWorkspace` refuses in private
windows), no new setting, no translation file touched.

## Tests

Real app, composed pixels: `tests/e2e/profiles_switch_e2e.ps1 -TransitionsOnly` (new section « T », reusing the
existing harness, `--workspace-test` QA channel and `profiles_switch_server.py` `/pat` stripe pages). A disposable copy
of the build runs in its own temporary root; the window is never brought to the foreground; no system setting is
changed (reduced motion uses the app's own `reduce-motion=1` profile setting). Pixels: our own window rendered by
`PrintWindow(PW_CLIENTONLY|PW_RENDERFULLCONTENT)` (DirectComposition overlay included), one viewport row classified as
pure stripe colour / white / dark canvas / **mixed** (anything between: a blur or a cross-fade), sampled continuously
for ~300 ms after each action.

| Check | Fix (01b) | main d060366 (02) |
| --- | --- | --- |
| T1 Ctrl+2, Ctrl+Tab, UIA select, close active tab: worst mixed % over ~13–19 frames | 0 / 0 / 0 / 0 | 57 / 59 / 49 / 0 |
| T1 captures after 5 tab switches (`qa transition-state`) | 0 | 1 (`TRANSITION tab` logged) |
| T2 same profile re-selected: no transition | ok | (cascade of T1) |
| T3 cold profile switch: `TRANSITION profile capture … gpu blur`, incoming phase, restore never cancels it, released at end | ok | ok |
| T3 warm switch: composed viewport shows the blur (worst mixed) | 49 % | 53 % |
| T4 tab chosen during a profile transition: cancelled, surfaces=0, chosen tab sharp, no poll / end timer afterwards | ok (red 49 %, mixed 0) | FAIL: overlay still active, phase 3, surfaces=2, red 0 % |
| T5 rapid A→B→A: retarget, then idle with nothing held | ok | ok |
| T6 reduced motion: tab switch immediate / profile switch « reduced (opacity only) » / released | ok | tab switch mixed 49 % |

The baseline column is the same new test run against an unmodified build of d060366 (built from `git archive`): it
proves the checks discriminate.

Evidence (`design/profile-only-blur-evidence/`):
- `00-dev-build.txt` — dev build of the fix, 0 warnings.
- `01a-transitions-e2e-first-run-3-failures-metric.txt` — first run, kept: 3 failures came from the **test metric**
  (a not-yet-painted tab is a dark canvas, counted as « mixed »); dark pixels are now classified apart. No product change
  between 01a and 01b.
- `01b-transitions-e2e-final-GREEN.txt` — 23/23.
- `02-transitions-e2e-baseline-main-d060366-RED.txt` — 12 failures on main.
- `03-profiles-switch-e2e-full.txt` — the whole existing script (H1, N1, M1, M2, L1–L6, failures) + T on the fix.
- `04-profiles-ui-unit.txt` — `test-profiles-ui.ps1` (includes the `ContentTransition::Overlay` unit tests: timing,
  retarget, budget, reduced motion, cancel, capture allowlist).
- `05-smoke.txt` — `test.ps1` (session / settings / address unit tests + `--smoke-test`).

## Limits

- Pixel proof is `PrintWindow` of our own window (the same path as the in-app `qa transition-frames`), not a desktop
  screen grab; DWM composition on screen is assumed equivalent.
- Mouse clicks on the tab strip were not injected (no input injection / focus stealing on the user's desktop); the
  click path calls the same `Select(int)` as the shortcut, UIA and close paths that were exercised.
- `OpenSessionTabHere` (tab preview → another cached profile's tab) is covered by source only.
- Profile colour / name edits do not call `SwitchWorkspace` (source); not exercised at runtime here.
- Split view panes made active by focusing a page (`ActivatePanel`) do not go through `Select` and never had a transition.
