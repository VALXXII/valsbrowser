# Update card — native design spec

Source of truth: Val's « Mise à jour » card, Paper file *Browser Design*, page **Atelier de Val** (read only), boards
*New update download* (277H-0), *New update downloading* (2784-0), *New update completed* (278R-0); validated by Val on
2026-11-26 on the live preview (branch `VALXXII/atelier-carte-maj`, `design/update-card-preview/`, not part of the
product). Placement unchanged: bottom-left of the window, 14 DIP from the edges (board 26R6-0).
Implementation: `src/update-toast.h`, `src/update-toast.cpp`, `src/update-toast-motion.h` (C++17, Direct2D/DirectWrite,
Direct3D 11 for the background, Win32; no web UI). Integration API unchanged: `build/coordination/update-toast-contract.md`.

## Exact Paper values (DIP)

| Element | Paper value |
| --- | --- |
| Card | width 269; heights 209.59375 (Disponible, Prête) / 187.59375 (En cours) — Paper's boards cut their content by 0.40625 at the bottom (bottom padding 15.59375); `#171717`; border 1 `#FFFFFF0D`; radius 8; overflow clip; padding 86 / 16 / 16; centred column |
| Background | Paper Shaders GrainGradient 268 × 202 at (0, 0): wave, speed 1, scale .39, softness .75, intensity .34, noise 0, colours `#8A66FF` `#ECAEFF` `#F4F4FF`, back transparent over `#171717`; time origin Paper's frame 6 409 645.9 ms |
| Fade | `linear-gradient(in oklab 180deg, #17171700 0%, #171717 79.64%)`, 269 × 347, at (−1, −73) (Disponible, Prête) / (−2, −100) (En cours) |
| Icon | 53 × 53, centred, top 20, white at 85 %; download-simple (viewBox 1.6 3.2 424 424), check-circle (top 19.8, viewBox 0 0 424 424) |
| Title | Inter Display Medium 16 / line 14, −0.01 em, `#FFFFFFE6`, features `cpsp cv12 tnum` |
| Text | Inter Display Regular 14 / line 18, +0.01 em, `#FFFFFFBF`, centred, `text-wrap: balance` (Paper's breaks reproduced, near ties go to the earlier break) |
| Progress line | ↓ 14 + « 50,4/200,0 Mo », gap 4, timer 14 + « 00:14 »; Regular 14 / 14, −0.01 em, `#FFFFFFBF`; centred |
| Buttons | row gap 8, height 32, `#292929`, radius 6; Medium 14 / 14, −0.01 em, white; primary flex 1, padding 8, label 80 %; secondary (« Plus tard ») its own width, padding 16, label 50 % |

Texts (French, Val's, with Chief's two corrections): « Mise à jour disponible » / « Une nouvelle mise à jour est prête
à être téléchargée. » / Télécharger · Plus tard; « Téléchargement en cours… » / size · time / Annuler; « Prête à
installer » / « La mise à jour est prête à être installée. » / « Redémarrer et installer ».

## The icon while downloading

The Downloads drawer's own icon (`src/download-icon.h`, `assets/lottie/download-*.json`): same model, frames and
timings (start 233 ms, 1.3 s loop, 40 fill steps, success morph 400 ms, ripple 600 ms), drawn at 53 DIP in white:
grey parts at the 85 % ink, the fill white at the ink (coordinator, 2026-11-26), the track dimmed to half the ink while
the fill means progress (following the ring's own frames). The ring = real progress (`received / total`).

## Motion (Designer's values where Paper has none)

| Change | Normal | Reduced motion (Windows or the app's setting) |
| --- | --- | --- |
| Appear | rises 8 DIP + fade, no-bounce spring 1.0 / 0.3 s | 120 ms fade |
| Disappear | 160 ms fade, no movement | same |
| Disponible → En cours | the drawer's start morph (tray curls into the ring), texts cross-fade 120 ms, « Plus tard » folds (width, its 8 DIP gap and opacity → 0), height 209.6 → 187.6 and fade (−1, −73) → (−2, −100), spring 1.0 / 0.3 s, bottom edge anchored | 120 ms cross-fades, sizes snap, ring static with the progress |
| En cours → Prête | the drawer's ripple at 100 %; the card waits for the arrow to come back to rest (≤ 1.3 s), then texts, button and height change while the arrow morphs into the check (400 ms); the ring then grows from the drawer's radius 88 to Val's 96 and the fill returns to the ink (450 ms, `cubic-bezier(.45, 0, .15, 1)`); the card keeps its check | 120 ms cross-fade to the check-circle, no ripple |
| En cours → Disponible (Annuler) | texts and buttons at once; the icon finishes its loop, then the ring unrolls into the tray (the drawer's cancel) | 120 ms cross-fades |
| Any other change | cross-fades (texts 120 ms, icon 120 ms) and the same spring for sizes | 120 ms cross-fades |
| Button | hover 120 ms, press `#3E3E3E` + scale 0.96 over 150 ms ease-out | no scale |

Texts never compete: each side of a cross-fade is drawn at the square of its opacity. Every change starts from what is
displayed (springs keep their velocity; the icon follows the drawer's own interruption rules).

## Cost

The background is drawn by the existing Paper-shader renderer (`src/onboarding-shaders.h`, Direct3D 11 read back as a
Direct2D bitmap — the onboarding's and the Réglages card's way): 30 frames a second only while the card can be seen
(owner visible, not minimised, not cloaked) and motion is allowed, on a hardware device; one still frame otherwise;
created when the card first shows and released when it has left. Hidden card: no frame, no timer, no GPU memory.
While idle on screen the vsync clock stops; only the 30 Hz background timer runs.

## States without a board (current behaviour kept, same layout)

| Phase | Card |
| --- | --- |
| `Hidden`, `Checking` | no card |
| `Installing` | tray icon, « Installation… » / its text, no button |
| `Installed` | check-circle, « Mise à jour installée » / its text, no button; 6 s dwell (paused while hovered or focused, +2 s), then Dismiss once |
| `Error` | info icon, « Échec de la mise à jour » / the backend's message; Réessayer (primary) + Ignorer (secondary) |
| Unknown total / time | size only (« 3,3 Mo »), ring empty; the timer group appears once the time is known |

## « Plus tard » (Val, choice B)

Hides the card for this session only (`Action::Later`, kept by the host, `BrowserUpdate::CardView`); the engine keeps its
offer and saves nothing, so the card comes back at the next start — shown from the last check's saved result, without
a network request (`src/update-core.cpp`, 6 h rule unchanged). A newer version or the manual « Rechercher une mise à
jour » shows it at once. « Ignorer » (error card) is the only real refusal (`dismissed` in the state file); old
`notified available:<v>` values written by released builds meant « shown once » and no longer hide anything.

## Input and keyboard

Unchanged: actions bound at pointer-down and fired on pointer-up for the same content generation; one activation per
action per state; F6 focus, Tab / arrows between buttons (primary first), Enter / Space, Esc returns focus.

## Deviations

1. Solid card (no acrylic), region-rounded corners at 8 DIP (no DWM shadow), bottom inset 14 (Paper's 14.447 is a
   canvas artefact).
2. Minimum width 226 DIP (« Téléchargement en cours… » is 190.1 DIP); narrower windows hide the card and keep its state.
3. The drawer's frames are bitmaps: placed on whole device pixels nearest to Paper's 0.2 / 0.4 DIP offset.
