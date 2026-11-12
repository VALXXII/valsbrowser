# Paper synchronization — done (2026-11-09)

File `01M6Q7YT14ZKMGET54ASPNGQEC` (*Browser Design*). Reference: `main` 58b782e, release 0.3.113.
The quota block of 7 November is over; every item that was pending here has been applied through
the Paper MCP tools and checked with screenshots. Nothing remains pending. The page **Icon** was not
touched (no node, position, token dependency or render change).

## Design system page (`p-2-0`, now « Design system — main 0.3.113 »)

- **01 Foundations**: header points at `main` 58b782e / 0.3.113; focus is 1 px white, 10 % after a
  pointer press and 50 % from the keyboard; spacing 4/6/8/16/24/32, button radius 6, inner radius 3;
  windows, menus and popups take the native DWM corner (8 DIP, standard Windows shadow). New
  section 04: accent `#3E67FF`, danger `#FF7B6E`, 1 px rounded caret at accent 90 % on every valid
  field, danger on an invalid bookmark URL until fixed, opaque popups (no acrylic, no blur).
- **02 Components**: the keyboard-focus column uses the 1 px inset white 50 % ring (the old 2 px
  `#BEBEBE` ring is gone). Tab samples use the exact `TabStrip` geometry: favicon at (8,8) 16 × 16,
  title slot x 32..96 × 32, close slot 24 × 24 at (100,4) with its 16 px glyph centred. New
  specimen: flower 16 at .32 with glyph `{14,14,30,30}`, first tab surface x 44, second x 178 (6 gap),
  new-tab 32 × 32 at x 312. Address text/placeholder at 50 %; loading veil white 8 % full height.
- **03 Motion**: static, editable stages generated from `assets/lottie/copy-link.json` and
  `refresh.json` (Phosphor Bold paths, stroke 24/256, ink composited at 50 %). Copy: 0 / 150 (real
  per-vertex morph at frame 4.5) / 300 / 1800 (hold, frame 10) / 2100 ms. Refresh: one rigid CCW turn
  in 500 ms, stages 0°, −97° (133 ms), −275° (233 ms), −340° (333 ms), rest at frame 14 = 0°, static
  paths, never restarted mid-turn, independent of navigation completion. Reduced motion: no Lottie,
  copy shows the check during the dwell then the link, refresh stays at rest. Menus: 180 ms entry,
  row hover follows the pointer even during the entry or with a submenu open; keyboard unchanged.
  Frames are display-paced (compositor clock), no idle ticks.
- **04 Updates**: single reference for the toast. Fully opaque `#171717` card (no acrylic, no blur),
  Paper radius 10 → native DWM 8 DIP; counter `50.4/200.0MB` with one decimal, size slot measured on
  `000.0/000.0MB`, 4 DIP before the timer.
- Tokens: removed unused `--text-browser-8` and `--font-browser-brand` (Gelasio, legacy 8 px brand
  role); `--color-browser-split-accent` is now an alias of `--color-browser-accent`.

## File cleanup

Pages now: Chrome — prototype, adresse, menus, téléchargements (ex Page 1) · Icon · Design system ·
Split View — flow (implémenté) · Split View — menu d'onglet V2 (retenu) · Favoris — flow (implémenté) ·
Réglages v4 — implémenté · Réglages — états détaillés (champs, menus, autorisations) · Aperçu URL —
implémenté.

Removed as abandoned or duplicated: split-view menu variants V1, V3, comparison and clone bases; the
whole split-view drop archive (A/B/C, M, R); Lisière V1 and two outdated V2 frames plus one exact
duplicate; the four original update cards (now only on board 04); an orphan frame; the old address
suggestion list; the tab menu without Print/PDF; Réglages panel v1 and v3; the non-described
anti-tracking menu; the old isolated URL-preview reconstruction. PNG exports of every removed frame
are kept outside the repo in `build/design-system-cleanup/exports-before-delete/`.

Kept on purpose: « Réglages — panneau v2 (défilé) » on the Chrome page (it carries an open user
thread about the retouched back/forward buttons), Lisière V3 (split-view clone source), the
scrollbar states, external reference images and the grain-gradient experiment (status unknown).
