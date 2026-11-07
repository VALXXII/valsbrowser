# Paper update component synchronization

File: Browser Design, page `browser - Design system`.
Board: **04 - Updates / native states and motion**.
Link: https://app.paper.design/file/01M6Q7YT14ZKMGET54ASPNGQEC/p-2-0

The four original update cards on Page1 remain unchanged. Their clones document
the native flow; the download specimen now uses a proportional25.2% fill for
50.4/200MB. Native runtime values always come from real transport progress.

Added editable Installing, unknown-size/ETA and Error specimens, plus native
material, bottom-left placement and motion notes. All specimens use the existing
Inter Display family and project surface tokens; original Phosphor vector paths
are retained. Native material is88% without blur, as documented by the toast peer,
and the inset is14DIP. Actionable states remain visible; success has a finite dwell.

Review: spacing and row alignment consistent; typography and heading contrast
verified; card geometry preserved; no clipped artboard content (fit-content);
functional extension states share the original visual language. Initial heading
inheritance and clone absolute positioning were corrected before final review.
Paper screenshots were taken after each section and after the final corrections;
working indicators released. Final screenshot: ignored
`build/coordination/paper-update-board.png`.

This synchronizes the update component only; historical pending design-system
changes elsewhere on the canvas are outside this component update.
