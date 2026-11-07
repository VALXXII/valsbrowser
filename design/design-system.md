# Current integration note

Final integration: `main`, derived from `feat/design-system-polish`. Flower16/.32 centred(22,22), first tab x44 aligned with forward, rigid CCW refresh once500ms independent of network completion. Full native regression and packaging: [release validation](release-validation.md).

Current source of truth: [Lottie and geometry contract](lottie-spec.md) and [native integration evidence](lottie-integration-validation.md). Focus is a 1 DIP white ring with two strengths: pointer focus 10%, keyboard focus 50% (issue #3); previous contrast measurements for the old stronger ring below are historical, not the current focus style. Copy uses authored Lottie segments (300 / 1500 / 300 ms); native tween specification remains a fallback. Paper synchronization is partially blocked by quota: [remaining work](paper-sync-pending.md).

# Native design system foundation

`src/design-system.h` is the shared C++17 token and state policy. The existing
Direct2D/DirectWrite renderer consumes it directly; no chrome WebView, framework,
network asset, font replacement, or browser engine change is introduced.

## Component contract

- Semantic surfaces: canvas #171717, raised/address approximately #292929,
  selected #292929, hover #333333, pressed #3e3e3e, inactive-tab hover #222222.
  Embedded controls interpolate opaque colors from their actual raised parent.
- Text roles: primary, secondary, muted, disabled and address. The native edit
  and rendered address share the same composited address text color. Separator
  opacity has its own surface role. `NativeGray` maps the same tokens to Win32.
- Geometry: 32 DIP controls, 24 DIP compact/close targets, 16 DIP Phosphor Bold
  glyphs, 14 DIP window glyphs, 6 DIP control radius, 3 DIP embedded radius and
  1 DIP inset focus perimeter: white at 10% alpha after a pointer press,
  white at 50% alpha when focus moved by keyboard. Spacing steps are 4/6/8/16 DIPs; tab
  pitch is 134 (6 DIP visible gap), visible tab width 128, close slot remains 24.
- Crowded tab strip (`TabStrip::Layout`/`Strip`): tabs share the room evenly
  between 128 and 32 DIP (whole DIPs, 6 DIP gap kept). Below 96 the close slot
  goes and the title takes its room; below 56 a tab is its centred favicon only.
  On those narrow tabs the selected one swaps its favicon for a centred 24 DIP
  close on hover/focus (cross-fade); inactive ones keep that spot for selecting.
  Past that the strip scrolls: the selected tab always stays clear, and each side
  that still hides tabs gets a 24 DIP canvas-coloured linear-gradient mask over a
  16 DIP peek of the next hidden tab. Masked tabs stay clickable (selecting scrolls
  them clear) but take no focus and show no close. Tooltips carry the full title;
  the flower tooltip carries the tab count. After a pointer close the width stays
  locked until the pointer leaves the tabs.
- Type roles preserve bundled Inter Display 14px medium labels / 16px regular
  address and flower vector 16 DIP brand (Gelasio 8px is now a legacy unused role). Real DirectWrite line metrics preserve full
  accents/descenders, with horizontal trimming only. Branding and app icon stay.
- `ButtonVariant`: Ghost, Raised, Embedded, Window and TabClose. `Resolve`
  gives a single fill/radius/icon/focus policy, independent of command IDs.
  `VariantFor` maps existing controls to variants in the renderer adapter.
- `ButtonState` accepts bounded hover/press, disabled and keyboard focus.
  Disabled suppresses interactive fills, focus, press transform and hit testing;
  keyboard order omits disabled controls. Copy is unavailable on an empty home.
  Window close/minimize/maximize share one variant and existing icon intensity.
- Keyboard focus draws a distinct perimeter rather than masquerading as hover.
  Tab traverses tab bodies and their close actions; arrows switch a focused tab;
  Enter/Space select or close. Mouse press clears the keyboard focus marker.
- Two focus modalities share one ring geometry. Tab, arrows, Enter/Space on a
  focused control and shortcuts (Ctrl+L/T/W/R/Tab, F5, Alt+arrows) switch to
  keyboard modality (50%); a mouse press on the chrome or in the address field
  switches back to pointer modality (10%, the original user-chosen value).
  Measured WCAG contrast of the 50% ring: #8b8b8b/#171717 5.26:1,
  #949494/#292929 4.79:1, #999999/#333333 4.43:1, #9f9f9f/#3e3e3e 3.99:1.
  The focused address field also keeps the perimeter while its native edit owns
  input. Existing Ctrl+Tab/Ctrl+W/address shortcuts remain.
- The close slot is always reserved; showing its glyph does not move the title.
  Tab/close hover paints one opaque parent surface, never two composited fills.

## Motion integration

Production startup opts into the peer's `EnablePolish` model. Buttons and close
icons use `PressScale` (0.96 endpoint, no scale for disabled/reduced motion).
Copy/check use complementary `CopyPresentation` layers in the same icon slot,
including press scaling, driven only by the existing successful clipboard event.
`Chrome::animateSelection` opts into one blended tab surface; main drives
`SelectTab(n+1)` and clears interaction/selection state before index reuse.
Active labels, content, focus and hit geometry update immediately. See
[motion-polish.md](motion-polish.md) for model timings and tests. No download
engine hook or synthetic download event was added.

## Native-only review and verification

`tests/design-system/design_system_test.cpp` calls production `DrawButton`,
`Resolve`, `PaintStateSheet` and `Paint`. It links Direct2D/DirectWrite/WIC only;
asserts WebView2Loader is never loaded; checks variant disabled/focus separation,
opaque state interpolation, keyboard exclusion/order, 24 DIP close behavior,
complete glyph ink and rendered tab selection/reduced-motion endpoints.
`test-design-system.ps1` builds/runs this fixture and the existing focused
hover/favicon regression into `build/design-system-preview`.

Generated artifacts (not committed):

- `build/design-system-preview/components-{96,144,192}.png`: default, hover,
  pressed, disabled, keyboard focus; equal window controls; tab/close surfaces.
- `build/design-system-preview/chrome-focus-{96,144,192}.png`: production chrome
  with embedded reload focus and full-height native text.
- `build/design-system-preview/favicon-alpha-{96,144,192}.png`: existing focused
  regression using an in-memory PNG to verify alpha, without a site or engine.
- `build/design-system-preview/browser.exe`: compile-only isolated executable.
  It was never launched and is not deployed or substituted for build/v2.

Validation performed:

- Focused design fixture compiled with C++17, `-Wall -Wextra -Werror`, and passed.
- Focused hover/favicon fixture passed: refresh/copy entry/exit samples remain
  41 -> 46 -> 51 -> 46 -> 41; all three window hover fills match each phase;
  alpha and stale-navigation/closed-tab favicon guards remain intact.
- State sheets at 96 and 144 DPI visually inspected; all three scales exported.
- Isolated browser resource/C++ compilation passed with existing build.ps1
  compiler options. Running build.ps1 itself was blocked by the machine's
  script execution policy; equivalent resource/compiler invocations were run
  directly without changing that policy. No shared build script was edited.
- Measured grayscale contrast using WCAG sRGB luminance: selected label
  #949494/#292929 4.80:1; inactive label #808080/#171717 4.54:1; focus perimeter
  #bebebe/#3e3e3e 5.75:1. Address opacity increased to the secondary role (0.5),
  giving approximately #949494/#292929 4.80:1 instead of the previous 4.21:1.

Not verified: live browser keyboard walk, UI Automation/screen-reader names and
roles, forced-colors behavior, live resize/200% text zoom and WebView2 runtime.
The existing owner-drawn chrome has no new UI Automation provider in this scope;
this is a component foundation, not an accessibility conformance certification.
The user's runtime hold remains in force: the prior download-dialog assertion
0x80004005 was neither reproduced nor changed; no engine/download smoke,
launch, retry, profile change or deployment occurred.
