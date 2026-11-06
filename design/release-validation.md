# Final native integration and local release

Approved preview integrated on `feat/design-system-polish`, then merged into local `main`.
The repository originally had `master` and the feature branch, with no `main`.
`main` is created from the existing `master` before merging; the `master` ref is preserved.

## Final behavior

- Brand flower: 16 DIP at32% opacity, centre(22,22), glyph14..30. Its38-DIP hit region no longer overlaps the first tab.
- First tab surface starts at44, aligned with the forward button. Tab/plus gaps6, close slot24 with equal4-DIP insets, focus white10%/1DIP.
- Refresh: one rigid counter-clockwise turn over500ms after successful Reload. Repeated activations during the turn do not rewind it; early/late navigation completion cannot truncate, extend or repeat it. Switching tabs clears the visual without starting a new turn from network loading state. The paths never trim/regrow, and the last frame is byte-identical to rest.
- Copy: successful clipboard transfer triggers300ms reveal,1500ms check dwell,300ms return; repeat feedback remains continuous. Reduced motion has no Lottie frame wakes. Existing clipboard success gate, favicon guards and hover fills are retained.

## Validation performed

- `python test-lottie.py`: all five production native fixtures passed: Lottie pixels/lifecycle, clipboard success gate, design system, hover/favicon and UI. UI fixture reports712219 checks across96/120/144/168/192/240/288/384 DPI.
- Added focused checks for exactly500ms refresh, ignored mid-turn triggers, late completion, no frame timer after rest, another activation after rest, exact refresh end pixels and first-tab/brand hit separation.
- `tests/design-system/geometry_test.cpp`: compiled with warnings as errors and passed, including brand alignment, first-tab column, equal close insets and focus tokens.
- `tests/lottie-assets/validate.py` under isolated rlottie1.3.8: zero failures; paths remain static, no trim, orientation is monotonic CCW, exactly one turn, duration500ms, last frames equal rest.
- Production Direct2D offscreen image `build/lottie-polish/evidence/keyframe-192-333.png` visually inspected: aligned flower/tab row, subtle flower opacity, centred close glyph and copy check.
- Final browser resource/C++ compilation passed using the existing build.ps1 flags directly. The machine has Restricted PowerShell script policy; no policy changed. ThorVG remains statically linked.

## Installed package

- Standalone bundle: `build/lottie-alignment/browser.exe`.
- Installed executable: `build/v2/browser-main.exe`.
- SHA256 for both: `D810636C693458CC17F35975ABA1690BF31DE4889CF59E409E6216D51D31C092`.
- All168 package files verified by source/destination SHA256. Unchanged files were left in place. Old executables retained.
- Windows Start Menu `browser.lnk` target and working directory verified after saving. The previous shortcut properties are recorded in ignored `build/lottie-alignment/previous-shortcut.json`.
- Existing profile `build/v2/profile` preserved in place. No process was killed, browser launched or profile copied/rewritten during installation.

## Remaining limits

No browser/WebView2 runtime test was run. The prior `Smoke download dialog did not open : 0x80004005` assertion remains untouched. Actual desktop animation pacing, live navigation and download UI remain unverified.

Paper basic information is readable, but `get_children` still returns `Weekly MCP limit reached` with8hours until reset. No further canvas mutations attempted; exact pending geometry/motion changes remain in [paper-sync-pending.md](paper-sync-pending.md).
