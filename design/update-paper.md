# Paper update component synchronization

File: Browser Design, page `Design system — main 0.3.113` (formerly `browser - Design system`).
Board: **04 - Updates / native states and motion**.
Link: https://app.paper.design/file/01M6Q7YT14ZKMGET54ASPNGQEC/p-2-0

This board is now the single Paper reference for the update toast: the four original
source cards on Page 1 were removed on 2026-11-09 as duplicates (PNG backups outside the
repo). Their clones document the native flow; the download specimen now uses a proportional25.2% fill for
50.4/200MB. Native runtime values always come from real transport progress.

Added editable Installing, unknown-size/ETA and Error specimens, plus native
material, bottom-left placement and motion notes. All specimens use the existing
Inter Display family and project surface tokens; original Phosphor vector paths
are retained. Native material is now a single fully opaque #171717 card (no acrylic, no blur), corner
rounded natively by DWM at 8 DIP (Paper 10), and the inset is 14 DIP. Actionable states remain visible; success has a finite dwell.

Review: spacing and row alignment consistent; typography and heading contrast
verified; card geometry preserved; no clipped artboard content (fit-content);
functional extension states share the original visual language. Initial heading
inheritance and clone absolute positioning were corrected before final review.
Paper screenshots were taken after each section and after the final corrections;
working indicators released. Final screenshot: ignored
`build/coordination/paper-update-board.png`.

This synchronizes the update component only; historical pending design-system
changes elsewhere on the canvas are outside this component update.
