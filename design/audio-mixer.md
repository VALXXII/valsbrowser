# Son des sites: per-site volume

A toolbar button left of History appears as soon as a web site makes sound. It opens an
anchored panel that lists the sites that played, with their media metadata and an
independent volume slider each. Code: `src/audio-mixer.h` (pure model),
`src/audio-script.h` (page side), `src/audio-panel.h` (popover), `src/audio-integration.h`
(host wiring), `src/chrome.h` (`'V'` button, `DrawAudioButton`, `DrawCover`),
`src/accessibility.h` (Slider role, RangeValue pattern). Lottie generator:
`design/lottie/audio-meter.py` → `assets/lottie/audio-meter.json`.
Tests: `test-audio.ps1` (`-Live` adds the WebView2 run).
Paper: [Son des sites — volume par site](https://app.paper.design/file/01M6Q7YT14ZKMGET54ASPNGQEC/p-G-0)
(context with the panel open, icon states, interaction notes).

## Behaviour

| Situation | Toolbar | Panel row |
| --- | --- | --- |
| No site has made sound | no button; the address field keeps its full width | — |
| Short sounds (clicks, notifications, game effects) | button appears once, **static** speaker with one bar, ink 38 % (subdued); never animates | « Sons courts · site » while audible, « Silencieux · site » after |
| Music / video: playing element ≥ 20 s or live, a Media Session title, or 5 s of continuous sound | speaker whose two bars move like a level meter (Lottie, 30 fps), full ink | Media Session title, « artiste · site », artwork |
| Music pauses | the meter finishes its cycle, then folds back to the static subdued speaker (400 ms); a pause shorter than 1.5 s (track change, buffering) does not stop it | « En pause · site » |
| Site volume at 0 % | (the engine stops hearing it, so the meter stops) | « Son coupé pour ce site · site », speaker-x glyph |
| Tab closed / navigated to another site | row removed; button removed with the last row (focus moves to History) | — |
| Quiet short-sound site at 100 % for 10 min | row dropped | — |

Short sounds therefore never look like music and never make the icon flicker: the button
appears once and stays static; only evidence of music animates it. The row remains after
the sound ends, so the site's volume stays one click away.

## Volume: what it really controls

WebView2 has no per-tab volume (only mute), so the volume is a **browser gain applied in
the page** by a script injected into every document and frame before page scripts run:

- `<audio>`/`<video>` play at *site volume × browser gain*. The `volume` property still
  returns the site's own value and the `volumechange` events our changes cause are
  swallowed before page listeners (also for `new Audio()` / `createElement` elements that
  never join the document), so a site's own slider never moves.
- Web Audio: every connection to a live `AudioContext` destination passes through one
  hidden `GainNode` per context (15 ms ramp, no zipper noise). `OfflineAudioContext`
  renders are untouched; an element routed into Web Audio is attenuated once, by the graph.
- Frames get the gain from their parent (postMessage relay); a new document starts at its
  site's volume from a table baked into the injected script (re-injected after a change),
  so autoplay never starts loud.
- Range 0–100 % (no amplification), per site (host without `www.`), persisted in
  `site-volumes` beside `session` (only sites not at 100 %).

**Limits.** Sound that bypasses these APIs (plugins, `<embed>`, a closed shadow root that
autoplays without `play()`, protected media whose element is created outside the page's
realm) is listed (the engine's audible flag) but may not be attenuated. A page could detect
the patched prototypes (`HTMLMediaElement.prototype.volume`, `AudioNode.prototype.connect`).
Artwork is fetched by the browser from the page's https Media Session URL (no cookies,
2 MB / 5 s caps, only while the panel is open). The model cannot know whether audio is
music: it only uses the evidence listed above.

## Panel (Paper p-G-0: TJ2-0, TGK-0, U40-0, T9Q-0)

**Popover**: 320 DIP, 1 DIP #FFFFFF0F border, radius 8 (window region, no shadow), padding
16, no title (the window's accessible name is « Son des sites »), close 24 DIP at right 11 /
top 10 (14 DIP glyph, 50 %). At most two rows; with more sites a 32 DIP « Voir tout (N) »
button (#292929, radius 6, 14 medium at 50 %) opens the drawer. Rows are separated by a
1 DIP #DDDDDD 10 % rule with 16 DIP each side. Heights match Paper: 211 (2 sites), 259 (7).
Entrance 180 ms fade + 6 DIP; light dismiss.

**Drawer** (« Voir tout »): docked on the window's right edge under the chrome, 320 DIP, full
height, #FFFFFF05 left rule, padding 16, every row, wheel scrolls, close 24 DIP at right 14 /
top 14 (16 DIP glyph). Enters with a 220 ms fade + 12 DIP from the right and leaves the same
way in 150 ms; it stays docked until closed and closes the other docked drawers (downloads,
settings, history, bookmarks), which close it in turn.

**Row**: no hover or press fill on the tile/title/metadata area (user feedback): it still
brings its tab forward on click or Enter and shows the keyboard focus ring; the real
buttons (mute, info, « Voir tout », close) keep their hover feedback. 40 DIP tile (artwork cropped to fill with a 1 DIP white 10 % edge, else the favicon
on #292929), 12 DIP gap, title 14 medium; then the artist (music) or « Sons et musiques » +
an info glyph (14 DIP, 50 %). Its tooltip is the native toolbar tooltip (Tooltip::Tip:
600 ms hover delay, at once on keyboard focus, 120 ms fade, kept inside the browser window)
with the Paper copy « Ce site peut jouer des sons ou de la musique, mais rien ne joue pour
l'instant. » Controls row: mute 24 DIP, sixteen 2 × 12 DIP ticks spread with
space-between, value 36 DIP (13 medium, 50 %, centred).

**Ticks are only a picture.** The volume is continuous (any of 0–100 % in 1 % steps, e.g.
72 %): the pointer sets it 1:1 anywhere along the ticks (tick 0's centre = 0 %, tick 15's =
100 %, about 2 DIP per percent; pressing on the value's spot keeps the grab offset), arrows
move it 1 %, Page Up/Down 10 %, Home/End, the wheel 5 %. That value is what the pages get,
what is stored and what the percentage shows. The picture lights the tick **nearest** to it
(72 % → tick 11 of 0–15): ticks before it in the accent, the nearest one 16 DIP tall with a
soft accent glow (Paper: accent 50 %, blur 6, spread 2), the rest #DDDDDD 10 %. The picture
never feeds back into the value. When the value changes, the lit position glides to its new
tick (exponential, 40 ms time constant) so a drag reads as one sweep; reduced motion snaps.
At 0 % no accent is shown (the nearest tick is white 32 %) and the mute glyph is speaker-x.

**Keyboard and UI Automation**: Tab walks row → info (short-sound rows) → mute → slider → …
→ « Voir tout » → close; Enter on a row brings its tab forward; Escape hides an open tooltip,
else closes and returns focus to the toolbar button. Per row: a button « Afficher l'onglet :
titre », an info button whose help text is the tooltip, a toggle « Couper le son de site », a
**Slider** « Volume de site » (RangeValue 0–100, small change 1, large 10, value change
events); then « Voir tout (N) » and « Fermer ».

## Toolbar icon

One Lottie, 97 frames at 60 fps: 0–72 the meter loop (frame 0 = Phosphor Bold
speaker-simple-high), 72–96 the outro to speaker-simple-low; played backwards it is the
intro. The static states are the Phosphor paths drawn natively, and the fixture checks the
Lottie rest frames against them (mean alpha error 0.16 %). Ink: white 50 % like every
toolbar icon, × 0.76 when subdued (38 %, 3.6:1 on the canvas, WCAG 1.4.11). While music
plays (Paper TO2-0 / UCD-0) the meter is the accent #3E67FF (3.7:1), and hover or an open
panel lays the accent at 16 % under it; the colour blends in with the intro and out with
the outro. Short sounds and silence keep the white, subdued, static speaker. The loop wakes
once per frame (~69 ms) and only while music plays; at rest there is no timer. Reduced
motion: static high (music) or low (otherwise), swapped at once.

**Meter character (user-chosen default, 2026-11-12; the first version was too
strong and fast).** The loop's bars come from one rule, `loop_keys` in
`design/lottie/audio-meter.py`, run live by the preview's debug mode:

| Setting | Default | First version | Where |
| --- | --- | --- | --- |
| Loop duration (speed) | 5 s | 1.2 s | `Audio::Icon::LoopMs` (playback; 72 frames over it) |
| Amplitude (swing from the rest pose) | 0.3 | 1 | `--amplitude` (asset) |
| Beats per loop | 5 | 5 | `--beats` (asset) |
| Outer bar delay (part of half a beat) | 0.7 | 0 | `--offset` (asset) |
| Ease (Bezier handle of each key) | 0.4 | 0.45 | `--ease` (asset) |

At the default (values chosen by the user in the preview's debug mode) the outer bar's length
moves within 67–90 (rest 80) instead of 48–112 and the inner one within 40–55 (rest 48)
instead of 20–72; a loop lasts 5 s (one frame every ~69 ms). Frames 0, 72 and 96 stay the exact
Phosphor glyphs whatever the settings. `--amplitude 1 --beats 5 --offset 0 --ease 0.45`
regenerates the first version byte for byte. To apply values chosen in the preview: run the
« Asset » command it exports, set `LoopMs`, rebuild.

## Theme

The browser chrome has a single dark theme (Design::Surface tokens); the panel and icon
use only those tokens, so they follow any future theme change through them.

## Preview sandbox

`design/audio-preview/index.html`: an **HTML simulation** of the component (clearly labelled
as such) with the same values, the real Lottie file, the tick picture rule, pointer drag,
keyboard, wheel, tooltip, popover and drawer, scenarios (music / short sounds / silence /
no sound, 2 or 7 sites) and reduced motion, plus the native renders from
`build/audio-qa` as the pixel reference.

**Mode debug — animation de l'icône** (collapsible, closed by default): live sliders for
speed (loop duration), amplitude, beats per loop, delay between the bars and ease; a 4×
preview; Pause/Lecture; « Réinitialiser (défaut natif) »; « Copier les valeurs » and
« Exporter (.json) » (values + the generator command + `LoopMs`); « Animer malgré
« mouvement réduit » » (preview only). Values persist in the browser (localStorage) and
apply immediately: the page rebuilds the Lottie from the shipped file with the generator's
rule and checks that the defaults reproduce `assets/lottie/audio-meter.json` exactly. Serve the repository root
(`python -m http.server 8777 --bind 127.0.0.1`) and open
http://127.0.0.1:8777/design/audio-preview/.

## Verification

`test-audio.ps1`: model (volumes, reports, classification, rows, keys, icon motion), Lottie
frames vs glyphs, renders in `build/audio-qa` (panel 100/150 %, focus, hover, drag, 100 %,
empty; toolbar without/after short sounds/music/focus; meter frames), the injected script
in a fake DOM (Node). `-Live`: real WebView2, two local pages; the button appears, the
music page is named, the blip page reads « Silencieux/Sons courts », slider 0 % → the
**engine** reports the site silent, 40 % → audible again, the site still reads its own
0.80 with no extra `volumechange`, the value is persisted.
