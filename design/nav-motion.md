# Navigation toolbar: Back/Forward pair and Reload <-> Stop

Source: Paper file "Browser Design", Page 1, artboard "Réglages — panneau v2 (défilé)",
frame "Chrome", comment by Val.S on 2026-11-09: « Bouton avant arrière retouché,
fais un test dessus stp ». Code: `src/nav-motion.h` (models), `src/chrome.h`
(`DrawNavButton`, `DrawStopMorph`), `src/main.cpp` (wiring), tokens in
`src/design-system.h` (`Design::Toolbar`). Test: `tests/motion/nav_motion_test.cpp`
(run by `test-ui.ps1`), geometry in `tests/design-system/geometry_test.cpp`.

## Back/Forward pair (from Paper, exact values)

| | Paper | Native |
|---|---|---|
| Back | 32 x 32, `#292929`, corners TL/BL 6, TR/BR 3 | `{6,44,38,76}`, Raised surface, `FillShape(r,6,3)` |
| Hairline | 1 x 10, radius 1, white at `--opacity-browser-focus` (10 %), 3 px gaps | x 41..42, centred on y 60, white 10 % |
| Forward | 32 x 32, `#292929`, corners TL/BL 3, TR/BR 6 | `{45,44,77,76}`, `FillShape(r,3,6)` |
| Glyphs | Phosphor arrow-left/right bold, 16 px, opacity .5 | existing `back`/`forward` icons (white 50 %) |
| Reload | 32 x 32, `#FFFFFF14` (= raised `#292929` on the canvas), radius 6, reload glyph 16 px at .5, 6 px after the pair | id `'R'` at `{83,44,115,76}`, Raised variant |
| Field | 6 px after Reload; Paper also keeps a Reload inside the field | address field starts at x 121 (was 82); in-field Reload removed after review, field text now ends 10 DIP before Copy |

Hover / press / disabled / focus keep the existing Raised states (hover #333, pressed #3E3E3E,
disabled glyph .32, 1 DIP focus ring that now follows the asymmetric corners). Paper shows
only the rest state, so no state was invented. Hit regions equal the visible halves.

## Back/Forward travel

- Pointer-down: the existing press feedback, immediately (surface to pressed, glyph .96).
- After a *successful* `GoBack` / `GoForward` (pointer, Alt+arrows, mouse X buttons — all
  go through `Command`, which already refuses disabled buttons), the arrow glyph travels in
  its own direction and settles on a critically damped spring (damping 1, response 0.4 s):
  peak ~3 DIP about 64 ms after the click, no overshoot, at rest within ~0.6 s.
- Interruptible: a new click adds velocity to the live position (scaled by the room left
  before the peak, so rapid clicks stay ~3 DIP); no restart, no jump, no input lock.
- Only the glyph moves; surface, hit testing and focus never move.
- Reduced motion (Windows or the app setting): no travel. Tab switch / halt: lands at rest.

## Reload <-> Stop (restored)

The standalone toolbar button (id `'R'`) is the only Reload/Stop control: it calls the
active pane's real `Reload()` / `Stop()`, like Ctrl+R, F5, the mouse/menu Reload items.
Tooltip and accessible name follow the loading state (« Actualiser » / « Arrêter le
chargement », Ctrl+R; UIA automation id `Reload`, read right after « Suivant »). It is never
disabled. The in-field Reload Paper also shows was removed at the user's request (review
of 2026-11-09); Tab from the open address goes to Copy (or the star, then History).

Since "Show stop button while active tab is loading" (1f0f020) the reload turn was hidden:
the button switched to a static X the moment the navigation started. Now:

1. Click Reload (or Ctrl+R / F5): `Reload()` runs, the authored Lottie turn plays (one CCW
   revolution, 500 ms), even though the tab is already loading.
2. If the tab is still loading when the turn ends, the reload glyph continues a quarter turn
   CCW while it shrinks and fades, and the X turns in the same direction while growing in
   (240 ms, `Curve::Icon`, same layer scales as Copy -> check). This is a native
   rotate/scale/cross-fade morph between the two Phosphor glyphs, not a path morph.
3. The button is Stop (label « Arrêter le chargement ») while loading; clicking it calls
   `Stop()`.
4. When loading really ends (completed, failed or stopped), the X morphs back to Reload by
   the inverse path.

Timing comes only from real WebView2 events (NavigationStarting / NavigationCompleted /
ProcessFailed). A load that ends during the turn never shows the X. A navigation that does
not come from Reload (link, address, Back/Forward) goes straight to the X morph. The morph
re-targets from its current value, so stop/start mid-morph reverses without a jump.

No idle timer: frames are requested only while a spring or the morph moves; while the X
waits for the turn to end, the turn already drives frames. Tab switches snap to the new
tab's state. Reduced motion: no turn, immediate glyph swap.

## Differences from the Paper frame

- Paper also keeps a Reload inside the address field; it was removed after review (user
  request) so Reload/Stop exists once, beside the nav pair.
- Paper names the standalone Reload frame « Suivant » (a duplicated layer name); the app
  names it from its function (« Actualiser » / « Arrêter le chargement »).
- Hover/press of the standalone Reload use the Raised states (#333 / #3E3E3E); Paper shows
  only its rest state.
- Paper's tab row is older than the app (no star/history/private buttons, stale tab
  styles); only the nav pair was taken from it.
- Forward now starts at x 45 (Paper: 32 + 3 + 1 + 3), so it is 1 DIP right of the first
  tab (x 44, unchanged). Paper has the same 1 px offset.

## Motion lab (review only)

`browser.exe --motion-lab` opens « Animations des icônes — labo », a native Win32 tool
window beside the browser (toggle with Ctrl+Alt+Maj+M; closing it only hides it). A normal
launch never creates it, never reads its file, and keeps the shipped values; the smoke and
split tests ignore the flag. Code: `src/motion-tuning.h` (parameters, bounds, text format,
unit-tested), `src/motion-lab.h` (panel), `MotionLabReplay` / `ApplyMotionTuning` in
`src/main.cpp`.

Every slider feeds a real model input read by the native renderer:

| Icon | Knob | Range (default) | Drives |
|---|---|---|---|
| Précédent / Suivant (each) | Amplitude | 0–8 DIP (3) | spring peak (`SpringParams.peak`) |
| | Réponse | 0.15–1 s (0.40) | spring response |
| | Amortissement | 0.5–1 (1.00) | damping ratio; below 1 the arrow overshoots |
| Actualiser / Arrêter | Durée du tour | 250–1500 ms (500) | Lottie turn length (`Playback::Tune`) |
| | Morph vers la croix | 0–600 ms (240) | `StopMorph::Configure` duration |
| | Rotation du morph | 0–180° (90) | `StopMorph::Configure` angle |
| | Chargement simulé (aperçu) | 300–4000 ms (1200) | replay only: how long the X is held |
| Copier le lien | Vitesse du morph | 0.5–2× (1) | both Lottie copy segments |
| | Maintien de la coche | 300–3000 ms (1500) | check dwell |
| Téléchargements | Vitesse | 0.5–2× (1) | icon frame rate (next run) |
| | Durée de l'onde | 200–1500 ms (600) | ripple length (next ripple) |

New values apply from the next activation, so nothing jumps mid-flight. « Rejouer » plays one
icon through the real models and renderer without any side effect: no `GoBack`, `Reload`,
clipboard write or download. Refresh replays the turn and a visual-only loading cycle
(`StopMorph::Preview`; the real command is unchanged).
Download replays a 1.4 s run then the completion ripple. A replay is refused while a real
load/download is running, and a real download cancels the download replay. Under reduced
motion, replays show the end state only, like the real triggers.

« Enregistrer » writes `motion-tuning.ini` next to the executable (never the browsing
profile); it is reloaded on the next `--motion-lab` launch. « Recharger » reads it again,
« Exporter » copies the same `key=value` text to the clipboard, « Réinitialiser » restores
the shipped values (not saved until « Enregistrer »). The defaults are what ships; to adopt
tuned values, copy them into the constants listed above.

Cost in a normal launch: one static table and a few float reads per activation; no window,
timer or file access.
