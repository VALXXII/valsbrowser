#pragma once
// Annotation mode — shared native kit for the annotation surfaces (banner, note popover,
// launcher + card, onboarding). Nothing here exists before the mode is first used:
//  - Ink: one process-wide Direct2D/DirectWrite/WIC set (bundled Inter Display, system
//    monospace for selectors/CSS/config), created at the first surface's creation.
//  - Layer: a per-pixel-alpha window (UpdateLayeredWindow): antialiased corners, fully
//    transparent pixels pass the mouse through (the launcher never steals a click outside
//    its shape), child of the browser window (above the WebView) or owned popup.
//  - Animator: ONE message-only window + ONE FrameClock for every annotation surface.
//    The clock thread starts at the first animation and is dormant (blocked, INFINITE)
//    whenever no surface moves: zero wake-ups at rest. No recurring SetTimer (only when the
//    compositor clock cannot start, a 16 ms fallback that is killed as soon as motion ends).
// No disk I/O except reading the bundled font files. All calls on the UI thread.
#include <windows.h>
#include <windowsx.h>
#include <d2d1.h>
#include <dwrite_1.h>
#include <wincodec.h>
#include <dwmapi.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>
#include "design-system.h"
#include "svg-path.h"
#include "frame-clock.h"
#include "accessibility.h"
#include "annotation-motion.h"

namespace NativeChrome { namespace Annotations {
template<class T> inline void Release(T*& p) { if (p) { p->Release(); p = nullptr; } }

// ---- Colours (Paper « MCP - Design UI UX », tokens of src/design-system.h) ----------------
inline constexpr D2D1_COLOR_F Hex(std::uint32_t rgba) {
    return {((rgba >> 24) & 255) / 255.f, ((rgba >> 16) & 255) / 255.f, ((rgba >> 8) & 255) / 255.f, (rgba & 255) / 255.f};
}
inline D2D1_COLOR_F Accent(float alpha = 1) { const auto a = Design::Accent::Get(); return {a.r, a.g, a.b, alpha}; }
inline D2D1_COLOR_F WithAlpha(D2D1_COLOR_F c, float a) { c.a *= a; return c; }
namespace Ink_ {
inline constexpr std::uint32_t canvas = 0x171717FF, raised = 0x292929FF, hover = 0x333333FF, pressed = 0x3E3E3EFF, danger = 0xFF7B6EFF;
inline constexpr std::uint32_t claude = 0xD97757FF, page = 0xF4F4F3FF;
inline constexpr std::uint32_t amber = 0xE6AC3DFF;   // V3 (Paper 5.5, token profile-ambre): « … n'a pas pu la traiter »
}

// ---- Phosphor (bold) glyphs, exact Paper exports ---------------------------------------
struct Glyph { const char* d; float x, y, w, h; };
namespace Glyphs {
inline constexpr Glyph caretDown{"M216.49,104.49l-80,80a12,12,0,0,1-17,0l-80-80a12,12,0,0,1,17-17L128,159l71.51-71.52a12,12,0,0,1,17,17Z", 0, 0, 256, 256};
inline constexpr Glyph info{"M108,84a16,16,0,1,1,16,16A16,16,0,0,1,108,84Zm128,44A108,108,0,1,1,128,20,108.12,108.12,0,0,1,236,128Zm-24,0a84,84,0,1,0-84,84A84.09,84.09,0,0,0,212,128Zm-72,36.68V132a20,20,0,0,0-20-20,12,12,0,0,0-4,23.32V168a20,20,0,0,0,20,20,12,12,0,0,0,4-23.32Z", 0, 0, 256, 256};
inline constexpr Glyph check{"M232.49,80.49l-128,128a12,12,0,0,1-17,0l-56-56a12,12,0,1,1,17-17L96,183,215.51,63.51a12,12,0,0,1,17,17Z", 0, 0, 256, 256};
inline constexpr Glyph close{"M208.49,191.51a12,12,0,0,1-17,17L128,145,64.49,208.49a12,12,0,0,1-17-17L111,128,47.51,64.49a12,12,0,0,1,17-17L128,111l63.51-63.52a12,12,0,0,1,17,17L145,128Z", 0, 0, 256, 256};
inline constexpr Glyph plus{"M228,128a12,12,0,0,1-12,12H140v76a12,12,0,0,1-24,0V140H40a12,12,0,0,1,0-24h76V40a12,12,0,0,1,24,0v76h76A12,12,0,0,1,228,128Z", 0, 0, 256, 256};
inline constexpr Glyph lock{"M208,76H180V56A52,52,0,0,0,76,56V76H48A20,20,0,0,0,28,96V208a20,20,0,0,0,20,20H208a20,20,0,0,0,20-20V96A20,20,0,0,0,208,76ZM100,56a28,28,0,0,1,56,0V76H100ZM204,204H52V100H204Z", 0, 0, 256, 256};
inline constexpr Glyph copy{"M216,28H88A12,12,0,0,0,76,40V76H40A12,12,0,0,0,28,88V216a12,12,0,0,0,12,12H168a12,12,0,0,0,12-12V180h36a12,12,0,0,0,12-12V40A12,12,0,0,0,216,28ZM156,204H52V100H156Zm48-48H180V88a12,12,0,0,0-12-12H100V52H204Z", 0, 0, 256, 256};
inline constexpr Glyph openOut{"M228,104a12,12,0,0,1-24,0V69l-59.51,59.51a12,12,0,0,1-17-17L187,52H152a12,12,0,0,1,0-24h64a12,12,0,0,1,12,12Zm-44,24a12,12,0,0,0-12,12v64H52V84h64a12,12,0,0,0,0-24H48A20,20,0,0,0,28,80V208a20,20,0,0,0,20,20H176a20,20,0,0,0,20-20V140A12,12,0,0,0,184,128Z", 0, 0, 256, 256};
inline constexpr Glyph code{"M69.12,94.15,28.5,128l40.62,33.85a12,12,0,1,1-15.36,18.44l-51.69-43.08a12,12,0,0,1,0-18.42L53.76,75.71A12,12,0,1,1,69.12,94.15Zm176.69,24.63-51.69-43.07a12,12,0,1,0-15.36,18.44L219.4,128l-40.64,33.85a12,12,0,1,0,15.36,18.44l51.69-43.08a12,12,0,0,0,0-18.43ZM162.1,32.72a12,12,0,0,0-15.38,7.18l-64,176a12,12,0,0,0,7.18,15.37A11.79,11.79,0,0,0,94,232a12,12,0,0,0,11.28-7.9l64-176A12,12,0,0,0,162.1,32.72Z", 0, 0, 256, 256};
inline constexpr Glyph danger{"M240.26,186.1,152.81,34.23h0a28.74,28.74,0,0,0-49.62,0L15.74,186.1a27.45,27.45,0,0,0,0,27.71A28.31,28.31,0,0,0,40.55,228h174.9a28.31,28.31,0,0,0,24.79-14.19A27.45,27.45,0,0,0,240.26,186.1Zm-20.8,15.7a4.46,4.46,0,0,1-4,2.2H40.55a4.46,4.46,0,0,1-4-2.2,3.56,3.56,0,0,1,0-3.73L124,46.2a4.77,4.77,0,0,1,8,0l87.44,151.87A3.56,3.56,0,0,1,219.46,201.8ZM116,136V104a12,12,0,0,1,24,0v32a12,12,0,0,1-24,0Zm28,40a16,16,0,1,1-16-16A16,16,0,0,1,144,176Z", 0, 0, 256, 256};
// The annotation mark (launcher, banner, toolbar). Paper path + translate(-9.5 -9.8).
inline constexpr Glyph mark{"M46.008 256.187L46.082 241.973C46.085 241.297 46.668 240.757 47.376 240.774L63.371 240.829C66.248 240.899 67.033 244.643 64.407 245.765L55.985 248.654C55.284 248.954 54.691 249.439 54.276 250.053 54.225 250.127 54.185 250.207 54.149 250.288L51.029 257.525C49.617 259.778 45.994 258.813 46.008 256.187Z", 46.0, 240.756f, 20, 18.862f};
// ---- V2 (Paper « MCP - Design UI UX », Phosphor bold unless noted) ----
// Network « tree structure » (modal title, « Connecter un agent »): Paper viewBox 0 0 160 160.
inline constexpr Glyph tree{"M145 67.5H87.5V57.5h2.5a12.5 12.5 0 0 0 12.5-12.5V25a12.5 12.5 0 0 0-12.5-12.5H70A12.5 12.5 0 0 0 57.5 25V45a12.5 12.5 0 0 0 12.5 12.5h2.5v10H15a7.5 7.5 0 0 0 0 15H32.5v15H30a12.5 12.5 0 0 0-12.5 12.5v20a12.5 12.5 0 0 0 12.5 12.5H50a12.5 12.5 0 0 0 12.5-12.5V110a12.5 12.5 0 0 0-12.5-12.5H47.5V82.5H112.5v15h-2.5a12.5 12.5 0 0 0-12.5 12.5v20a12.5 12.5 0 0 0 12.5 12.5h20a12.5 12.5 0 0 0 12.5-12.5V110a12.5 12.5 0 0 0-12.5-12.5h-2.5V82.5h17.5a7.5 7.5 0 0 0 0-15ZM72.5 27.5h15V42.5H72.5ZM47.5 127.5H32.5V112.5H47.5Zm80 0H112.5V112.5h15Z", 0, 0, 160, 160};
inline constexpr Glyph infoLocal{"M47.25 36.751a7 7 0 1 1 7 7A7 7 0 0 1 47.25 36.751Zm56 19.25A47.25 47.25 0 1 1 56 8.751 47.302 47.302 90 0 1 103.25 56.001Zm-10.5 0a36.75 36.75 0 1 0-36.75 36.75A36.789 36.789 90 0 0 92.75 56.001Zm-31.5 16.047V57.751a8.75 8.75 0 0 0-8.75-8.75 5.25 5.25 0 0 0-1.75 10.202V73.501a8.75 8.75 0 0 0 8.75 8.75 5.25 5.25 0 0 0 1.75-10.203Z", 0, 0, 112, 112};
inline constexpr Glyph warningCircle{"M128,20A108,108,0,1,0,236,128,108.12,108.12,0,0,0,128,20Zm0,192a84,84,0,1,1,84-84A84.09,84.09,0,0,1,128,212Zm-12-80V80a12,12,0,0,1,24,0v52a12,12,0,0,1-24,0Zm28,40a16,16,0,1,1-16-16A16,16,0,0,1,144,172Z", 0, 0, 256, 256};
inline constexpr Glyph globe{"M128,20A108,108,0,1,0,236,128,108.12,108.12,0,0,0,128,20Zm83.13,96H172.7c-2.08-31.31-12.9-56.83-27.61-73.22A84.24,84.24,0,0,1,211.13,116ZM128,212c-11.36-12.3-22.41-36.53-24.67-72h49.34C150.41,175.47,139.36,199.7,128,212Zm-24.67-96c2.26-35.47,13.31-59.7,24.67-72,11.36,12.3,22.41,36.53,24.67,72Zm7.58-73.22C96.2,59.17,85.38,84.69,83.3,116H44.87A84.24,84.24,0,0,1,110.91,42.78ZM44.87,140H83.3c2.08,31.31,12.9,56.83,27.61,73.22A84.24,84.24,0,0,1,44.87,140Zm100.22,73.22c14.71-16.39,25.53-41.91,27.61-73.22h38.43A84.24,84.24,0,0,1,145.09,213.22Z", 0, 0, 256, 256};
inline constexpr Glyph dots{"M144,128a16,16,0,1,1-16-16A16,16,0,0,1,144,128ZM60,112a16,16,0,1,0,16,16A16,16,0,0,0,60,112Zm136,0a16,16,0,1,0,16,16A16,16,0,0,0,196,112Z", 0, 0, 256, 256};
inline constexpr Glyph caretLeft{"M168.49,199.51a12,12,0,0,1-17,17l-80-80a12,12,0,0,1,0-17l80-80a12,12,0,0,1,17,17L97,128Z", 0, 0, 256, 256};
// Phosphor « eye » (bold), Paper 5.5 « Lue par Claude ».
inline constexpr Glyph eye{"M251,123.13c-.37-.81-9.13-20.26-28.48-39.61C196.63,57.67,164,44,128,44S59.37,57.67,33.51,83.52C14.16,102.87,5.4,122.32,5,123.13a12.08,12.08,0,0,0,0,9.75c.37.82,9.13,20.26,28.49,39.61C59.37,198.34,92,212,128,212s68.63-13.66,94.48-39.51c19.36-19.35,28.12-38.79,28.49-39.61A12.08,12.08,0,0,0,251,123.13Zm-46.06,33C183.47,177.27,157.59,188,128,188s-55.47-10.73-76.91-31.88A130.36,130.36,0,0,1,29.52,128,130.45,130.45,0,0,1,51.09,99.89C72.54,78.73,98.41,68,128,68s55.46,10.73,76.91,31.89A130.36,130.36,0,0,1,226.48,128,130.45,130.45,0,0,1,204.91,156.12ZM128,84a44,44,0,1,0,44,44A44.05,44.05,0,0,0,128,84Zm0,64a20,20,0,1,1,20-20A20,20,0,0,1,128,148Z", 0, 0, 256, 256};
inline constexpr Glyph history{"M140,80v41.21l34.17,20.5a12,12,0,1,1-12.34,20.58l-40-24A12,12,0,0,1,116,128V80a12,12,0,0,1,24,0ZM128,28A99.38,99.38,0,0,0,57.24,57.34c-4.69,4.74-9,9.37-13.24,14V64a12,12,0,0,0-24,0v40a12,12,0,0,0,12,12H72a12,12,0,0,0,0-24H57.77C63,86,68.37,80.22,74.26,74.26a76,76,0,1,1,1.58,109,12,12,0,0,0-16.48,17.46A100,100,0,1,0,128,28Z", 0, 0, 256, 256};
// Launcher « tiroir » (corbeille à courrier): stroked outline, Paper viewBox 0 0 24 24, stroke 2, round caps/joins.
inline constexpr Glyph tray{"M4 13.5V18a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2v-4.5M4 13.5 6.2 5.6A2 2 0 0 1 8.1 4h7.8a2 2 0 0 1 1.9 1.6L20 13.5M4 13.5h4.5l1.2 2h4.6l1.2-2H20", 0, 0, 24, 24};
// Agent logos (Simple Icons 13.21.0, CC0), exact Paper paths: Claude #D97757, OpenAI (Codex) white.
inline constexpr Glyph claude{"M4.452 15.736l4.455-2.5 0.075-0.218-0.075-0.12h-0.218l-0.745-0.046-2.547-0.069-2.207-0.092-2.139-0.114-0.539-0.115-0.504-0.665 0.051-0.333 0.454-0.304 0.648 0.058 1.433 0.097 2.151 0.15 1.559 0.091 2.311 0.241h0.367l0.052-0.149-0.126-0.092-0.098-0.091L6.586 9.956l-2.409-1.594-1.262-0.918-0.682-0.464-0.344-0.436-0.149-0.952 0.619-0.682 0.831 0.573 0.213 0.573 0.843 0.648 1.8 1.393 2.351 1.732 0.344 0.287 0.138-0.098 0.017-0.069-0.155-0.258-1.279-2.311-1.365-2.35-0.607-0.975-0.161-0.585c-0.057-0.241-0.097-0.441-0.097-0.688L5.938 0.793 6.328 0.667l0.94 0.126 0.396 0.344 0.585 1.336 0.946 2.105 1.468 2.861 0.43 0.849 0.229 0.786 0.086 0.241h0.149v-0.138l0.121-1.611 0.223-1.979 0.218-2.546 0.075-0.717 0.355-0.86 0.705-0.464 0.551 0.264 0.453 0.648-0.631 0.418-0.269 1.749-0.528 2.741-0.344 1.835h0.201l0.229-0.23 0.929-1.232 1.559-1.949 0.688-0.774 0.803-0.855 0.516-0.407h0.974l0.717 1.066-0.321 1.102-1.004 1.273-0.831 1.077-1.193 1.606-0.746 1.284 0.069 0.104 0.178-0.018 2.694-0.573 1.457-0.264 1.737-0.298 0.786 0.367 0.86 0.373-0.31 0.762-1.858 0.459-2.178 0.436-3.246 0.768-0.401 0.029 0.459 0.057 1.462 0.138 0.625 0.034h1.531l2.851 0.212 0.745 0.493 0.447 0.603-0.746 0.459-1.146 0.585-1.548-0.367-3.613-0.861-1.238-0.309h-0.172v0.103l1.032 1.01 1.892 1.708 2.368 2.202 0.12 0.545-0.304 0.429-0.321-0.046-2.081-1.565-0.803-0.705-1.818-1.531h-0.121v0.161l0.419 0.614 2.214 3.325 0.114 1.021-0.16 0.332-0.574 0.201-0.63-0.115-1.296-1.818L13.581 17.628l-1.078-1.835-0.131 0.074-0.637 6.852-0.298 0.35-0.688 0.263-0.574-0.435-0.304-0.706 0.305-1.393 0.367-1.818 0.298-1.445 0.269-1.794 0.16-0.597-0.011-0.04-0.132 0.017-1.353 1.858-2.059 2.781-1.628 1.744-0.39 0.155-0.677-0.35 0.063-0.625 0.379-0.556 2.254-2.868 1.359-1.777 0.877-1.027-0.006-0.149h-0.051l-5.986 3.887-1.067 0.138-0.458-0.43 0.057-0.705 0.219-0.23 1.8-1.238Z", 0.667f, 1.6f, 21.333f, 21.333f};
inline constexpr Glyph openai{"M19.806 10.063a5.32 5.32 0 0 0-0.458-4.365 5.374 5.374 0 0 0-5.787-2.578A5.391 5.391 0 0 0 4.428 5.051a5.32 5.32 0 0 0-3.554 2.577 5.374 5.374 0 0 0 0.66 6.309 5.316 5.316 0 0 0 0.454 4.365 5.379 5.379 0 0 0 5.791 2.578A5.32 5.32 0 0 0 11.787 22.667a5.383 5.383 0 0 0 5.13-3.739 5.324 5.324 0 0 0 3.554-2.578 5.383 5.383 0 0 0-0.664-6.287z m-8.019 11.207a3.979 3.979 0 0 1-2.557-0.925l0.126-0.072 4.248-2.451a0.707 0.706 90 0 0 0.349-0.606v-5.988l1.795 1.039a0.063 0.063 0 0 1 0.034 0.046v4.963a4.004 4.004 0 0 1-3.996 3.995z m-8.588-3.666a3.974 3.974 0 0 1-0.475-2.68l0.126 0.076 4.252 2.452a0.686 0.685 0 0 0 0.694 0l5.193-2.995v2.073a0.715 0.071 0 0 1-0.295 0.055L8.658 19.067a3.999 3.999 0 0 1-5.459-1.463zM2.081 8.352a3.987 3.987 0 0 1 2.102-1.754V11.644a0.681 0.681 0 0 0 0.345 0.602l5.168 2.982-1.796 1.039a0.673 0.068 0 0 1-0.063 0l-4.293-2.478A4.004 4.004 0 0 1 2.081 8.331z m14.752 3.428L11.648 8.768 13.439 7.733a0.673 0.068 0 0 1 0.063 0l4.294 2.481a3.995 3.995 0 0 1-0.602 7.204v-5.046a0.702 0.702 0 0 0-0.361-0.593z m1.788-2.688l-0.127-0.075-4.243-2.473a0.69 0.69 90 0 0-0.698 0L8.364 9.538V7.464a0.588 0.059 0 0 1 0.025-0.055l4.293-2.477a3.999 3.999 0 0 1 5.938 4.142zM7.383 12.767l-1.795-1.035a0.715 0.071 0 0 1-0.034-0.05V6.732a3.999 3.999 0 0 1 6.556-3.07l-0.126 0.072L7.737 6.186a0.707 0.706 90 0 0-0.349 0.605z m0.976-2.102l2.313-1.333 2.317 1.333v2.666l-2.308 1.333-2.317-1.333Z", 0, 1.6f, 21.333f, 21.333f};
// « En cours » ring: a 3/4 arc (Paper WOD-0 288 vb, WOL-0 128 vb: same proportions).
inline constexpr Glyph ring{"M118.004 63.985a54 54 0 0 1-108 0c0-21.26 12.365-40.67 31.5-49.45A6 6 0 1 1 46.504 25.439C31.624 32.27 22.004 47.399 22.004 63.985a42 42 0 0 0 84 0c0-16.585-9.62-31.715-24.5-38.546A6 6 0 1 1 86.504 14.535C105.639 23.315 118.004 42.725 118.004 63.985Z", 0, 0, 128, 128};
}

// ---- Typography -------------------------------------------------------------------------
// Inter Display everywhere (bundled Regular/Medium: Paper's 600 renders Medium, no faux
// bold); JetBrains Mono only for selectors, CSS and config: the bundled JetBrains Mono 2.304
// Regular (assets/fonts, OFL; T3), else the first installed of Cascadia Mono, Consolas. Sizes/line heights in DIP, tracking in em.
struct Font {
    float size = 14, line = 20; int weight = 400; bool mono = false, tabular = false, plain = false; float tracking = 0;
    bool system = false;   // V3 (mono only): the first INSTALLED of JetBrains Mono, Cascadia Mono, Consolas — what the page's
                           // CSS stack resolves to — never the bundled copy (the native selection label must match the page's)
};
inline Font F(float size, float line, int weight = 400, float tracking = 0) { Font f; f.size = size; f.line = line; f.weight = weight; f.tracking = tracking; return f; }
inline Font Mono(float size, float line, bool plain = false) { Font f; f.size = size; f.line = line; f.mono = true; f.plain = plain; return f; }
enum class Align { Leading, Center, Trailing };
struct Span { std::wstring text; D2D1_COLOR_F color; int weight = 400; };

namespace Detail {
struct FontEnum final : IDWriteFontFileEnumerator {
    LONG refs = 1; IDWriteFactory* factory; std::wstring folder; int index = -1; IDWriteFontFile* file = nullptr;
    FontEnum(IDWriteFactory* f, std::wstring d) : factory(f), folder(std::move(d)) {
        for (const wchar_t* c : candidates) if (GetFileAttributesW((folder + c).c_str()) != INVALID_FILE_ATTRIBUTES) files.push_back(c);
    }
    ~FontEnum() { Annotations::Release(file); }
    // JetBrains Mono is optional: a payload without it keeps the Inter collection and falls back to a system mono.
    static constexpr const wchar_t* candidates[3] = {L"InterDisplay-Regular.ttf", L"InterDisplay-Medium.ttf", L"JetBrainsMono-Regular.ttf"};
    std::vector<std::wstring> files;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** p) override { if (!p) return E_POINTER; *p = nullptr; if (iid == IID_IUnknown || iid == __uuidof(IDWriteFontFileEnumerator)) { *p = this; AddRef(); return S_OK; } return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override { const ULONG r = InterlockedDecrement(&refs); if (!r) delete this; return r; }
    HRESULT STDMETHODCALLTYPE MoveNext(BOOL* more) override { Annotations::Release(file); *more = ++index < int(files.size()); if (!*more) return S_OK; return factory->CreateFontFileReference((folder + files[size_t(index)]).c_str(), nullptr, &file); }
    HRESULT STDMETHODCALLTYPE GetCurrentFontFile(IDWriteFontFile** out) override { if (!file) return E_FAIL; *out = file; file->AddRef(); return S_OK; }
};
struct FontLoader final : IDWriteFontCollectionLoader {
    LONG refs = 1; std::wstring folder;
    explicit FontLoader(std::wstring d) : folder(std::move(d)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** p) override { if (!p) return E_POINTER; *p = nullptr; if (iid == IID_IUnknown || iid == __uuidof(IDWriteFontCollectionLoader)) { *p = this; AddRef(); return S_OK; } return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override { const ULONG r = InterlockedDecrement(&refs); if (!r) delete this; return r; }
    HRESULT STDMETHODCALLTYPE CreateEnumeratorFromKey(IDWriteFactory* f, const void*, UINT32, IDWriteFontFileEnumerator** out) override { *out = new FontEnum(f, folder); return S_OK; }
};
}

// Thumbnail of the capture (already cropped and reduced by the caller, off the UI thread):
// premultiplied BGRA. `mark` = the annotated element inside it, in thumbnail pixels.
struct Shot { unsigned width = 0, height = 0; std::vector<std::uint32_t> pixels; D2D1_RECT_F mark{0, 0, 0, 0}; };

class Ink {
    ID2D1Factory* factory_ = nullptr; IDWriteFactory* write_ = nullptr; Detail::FontLoader* loader_ = nullptr;
    IDWriteFontCollection* inter_ = nullptr; IWICImagingFactory* wic_ = nullptr;
    std::wstring mono_ = L"Consolas"; bool monoBundled_ = false; std::wstring systemMono_ = L"Consolas";
    float interAscent_ = .96875f, interDescent_ = .2421875f, monoAscent_ = .8f, monoDescent_ = .2f, sysAscent_ = .8f, sysDescent_ = .2f;
    std::map<const char*, ID2D1PathGeometry*> geometries_;
    struct Key { std::wstring text; float size, line, tracking, width; int weight; bool mono, tabular, plain, wrap; int align; bool system;
        bool operator<(const Key& o) const { return std::tie(text, size, line, tracking, width, weight, mono, tabular, plain, wrap, align, system) < std::tie(o.text, o.size, o.line, o.tracking, o.width, o.weight, o.mono, o.tabular, o.plain, o.wrap, o.align, o.system); } };
    std::map<Key, IDWriteTextLayout*> layouts_;
    std::map<ID2D1RenderTarget*, ID2D1SolidColorBrush*> brushes_;
    struct BitmapKey { ID2D1RenderTarget* target; const void* shot; const void* data; bool operator<(const BitmapKey& o) const { return std::tie(target, shot, data) < std::tie(o.target, o.shot, o.data); } };
    std::map<BitmapKey, ID2D1Bitmap*> bitmaps_;
    ID2D1RenderTarget* target_ = nullptr; ID2D1SolidColorBrush* brush_ = nullptr; ID2D1StrokeStyle* round_ = nullptr;
    unsigned long long layoutsCreated_ = 0;

    static void Metrics(IDWriteFontCollection* collection, const wchar_t* family, float& ascent, float& descent) {
        UINT32 index = 0; BOOL exists = FALSE; IDWriteFontFamily* fam = nullptr; IDWriteFont* font = nullptr;
        if (SUCCEEDED(collection->FindFamilyName(family, &index, &exists)) && exists && SUCCEEDED(collection->GetFontFamily(index, &fam)) &&
            SUCCEEDED(fam->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font))) {
            DWRITE_FONT_METRICS m{}; font->GetMetrics(&m);
            if (m.designUnitsPerEm) { ascent = float(m.ascent) / m.designUnitsPerEm; descent = float(m.descent) / m.designUnitsPerEm; }
        }
        Release(font); Release(fam);
    }
    float Baseline(const Font& f) const {
        const bool sys = f.mono && f.system;
        const float a = (sys ? sysAscent_ : f.mono ? monoAscent_ : interAscent_) * f.size, d = (sys ? sysDescent_ : f.mono ? monoDescent_ : interDescent_) * f.size;
        return (f.line - (a + d)) / 2 + a;   // CSS half-leading
    }
public:
    Ink() = default; Ink(const Ink&) = delete; Ink& operator=(const Ink&) = delete;
    ~Ink() { Dispose(); }
    static Ink*& Instance() { static Ink* ink = nullptr; return ink; }
    // The process-wide instance, created at the first annotation surface. root = app folder (assets\fonts).
    static Ink* Shared(const std::wstring& root) {
        auto*& ink = Instance();
        if (!ink) { ink = new Ink(); if (FAILED(ink->Init(root))) { delete ink; ink = nullptr; } }
        return ink;
    }
    static bool Exists() { return Instance() != nullptr; }
    HRESULT Init(const std::wstring& root) {
        HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &factory_); if (FAILED(hr)) return hr;
        hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&write_)); if (FAILED(hr)) return hr;
        loader_ = new Detail::FontLoader(root + L"\\assets\\fonts\\");
        hr = write_->RegisterFontCollectionLoader(loader_); if (FAILED(hr)) return hr;
        const UINT key = 0xA770; hr = write_->CreateCustomFontCollection(loader_, &key, sizeof(key), &inter_); if (FAILED(hr)) return hr;
        UINT32 index = 0; BOOL exists = FALSE; inter_->FindFamilyName(L"Inter Display", &index, &exists); if (!exists) return E_FAIL;
        Metrics(inter_, L"Inter Display", interAscent_, interDescent_);
        IDWriteFontCollection* system = nullptr;
        if (SUCCEEDED(write_->GetSystemFontCollection(&system, FALSE))) {   // the page's CSS stack, as Chromium resolves it
            for (const wchar_t* name : {L"JetBrains Mono", L"Cascadia Mono", L"Consolas"}) {
                if (SUCCEEDED(system->FindFamilyName(name, &index, &exists)) && exists) { systemMono_ = name; Metrics(system, name, sysAscent_, sysDescent_); break; }
            }
        }
        if (SUCCEEDED(inter_->FindFamilyName(L"JetBrains Mono", &index, &exists)) && exists) { mono_ = L"JetBrains Mono"; monoBundled_ = true; Metrics(inter_, L"JetBrains Mono", monoAscent_, monoDescent_); }
        else { mono_ = systemMono_; monoAscent_ = sysAscent_; monoDescent_ = sysDescent_; }
        Release(system);
        return CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic_));
    }
    void Dispose() {
        for (auto& b : bitmaps_) Release(b.second); bitmaps_.clear();
        for (auto& b : brushes_) Release(b.second); brushes_.clear();
        for (auto& l : layouts_) Release(l.second); layouts_.clear();
        for (auto& g : geometries_) Release(g.second); geometries_.clear();
        Release(inter_); if (write_ && loader_) write_->UnregisterFontCollectionLoader(loader_); Release(loader_);
        Release(round_); Release(write_); Release(wic_); Release(factory_); target_ = nullptr; brush_ = nullptr;
    }
    ID2D1Factory* Factory() const { return factory_; }
    float BaselineOf(const Font& f) const { return Baseline(f); }
    IWICImagingFactory* Wic() const { return wic_; }
    const std::wstring& MonoFamily() const { return mono_; }
    const std::wstring& SystemMonoFamily() const { return systemMono_; }
    unsigned long long LayoutsCreated() const { return layoutsCreated_; }
    // Bind before drawing; a target's brush is created once and kept until Forget(target).
    bool Bind(ID2D1RenderTarget* t) {
        target_ = t; brush_ = nullptr; if (!t) return false;
        auto& b = brushes_[t]; if (!b) t->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &b);
        brush_ = b; return brush_ != nullptr;
    }
    void Forget(ID2D1RenderTarget* t) {
        auto it = brushes_.find(t); if (it != brushes_.end()) { Release(it->second); brushes_.erase(it); }
        for (auto b = bitmaps_.begin(); b != bitmaps_.end();) { if (b->first.target == t) { Release(b->second); b = bitmaps_.erase(b); } else ++b; }
        if (target_ == t) { target_ = nullptr; brush_ = nullptr; }
    }
    // Drops the cached thumbnails of a target (new data: the old pixel buffers are gone).
    void ForgetBitmaps(ID2D1RenderTarget* t) { for (auto b = bitmaps_.begin(); b != bitmaps_.end();) { if (!t || b->first.target == t) { Release(b->second); b = bitmaps_.erase(b); } else ++b; } }
    // Longest prefix of `text` that fits `lines` lines of `width` with an ellipsis (« … ») appended.
    std::wstring Clamp(const std::wstring& text, const Font& f, float width, int lines, const std::wstring& before = L"", const std::wstring& after = L"") {
        if (Lines(before + text + after, f, width) <= lines) return before + text + after;
        size_t lo = 0, hi = text.size();
        while (lo < hi) { const size_t mid = (lo + hi + 1) / 2; std::wstring t = text.substr(0, mid); while (!t.empty() && t.back() == L' ') t.pop_back();
            if (Lines(before + t + L"…" + after, f, width) <= lines) lo = mid; else hi = mid - 1; }
        std::wstring t = text.substr(0, lo); while (!t.empty() && (t.back() == L' ' || t.back() == L',')) t.pop_back();
        return before + t + L"…" + after;
    }
    ID2D1RenderTarget* Target() const { return target_; }
    ID2D1SolidColorBrush* Brush(D2D1_COLOR_F c) { brush_->SetColor(c); return brush_; }

    // ---- shapes (DIP) ----
    void Fill(D2D1_RECT_F r, float radius, D2D1_COLOR_F c) {
        if (c.a <= 0 || !target_) return;
        if (radius > 0) target_->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), Brush(c)); else target_->FillRectangle(r, Brush(c));
    }
    // Inside stroke (CSS border): the stroke lies within r.
    void Stroke(D2D1_RECT_F r, float radius, D2D1_COLOR_F c, float width = 1) {
        if (c.a <= 0 || !target_) return; const float h = width / 2; const D2D1_RECT_F s{r.left + h, r.top + h, r.right - h, r.bottom - h};
        const float rr = std::max(0.f, radius - h);
        if (rr > 0) target_->DrawRoundedRectangle(D2D1::RoundedRect(s, rr, rr), Brush(c), width); else target_->DrawRectangle(s, Brush(c), width);
    }
    void Line(float x0, float y0, float x1, float y1, D2D1_COLOR_F c, float width = 1) { if (target_) target_->DrawLine({x0, y0}, {x1, y1}, Brush(c), width); }
    void Disc(float cx, float cy, float r, D2D1_COLOR_F c) { if (target_ && c.a > 0) target_->FillEllipse(D2D1::Ellipse({cx, cy}, r, r), Brush(c)); }
    void Ring(float cx, float cy, float r, D2D1_COLOR_F c, float width) { if (target_ && c.a > 0) target_->DrawEllipse(D2D1::Ellipse({cx, cy}, r - width / 2, r - width / 2), Brush(c), width); }
    // Soft glow (Paper box-shadow 0 0 8px accent 50 %): stacked rings, decreasing alpha.
    void Glow(D2D1_RECT_F r, float radius, D2D1_COLOR_F c, float blur = 8) {
        if (!target_) return; const int steps = 6;
        for (int i = steps; i >= 1; --i) {
            const float t = i / float(steps), grow = blur * t * .6f, a = c.a * (1 - t) * (1 - t) * .55f;
            const D2D1_RECT_F g{r.left - grow, r.top - grow, r.right + grow, r.bottom + grow};
            target_->DrawRoundedRectangle(D2D1::RoundedRect(g, radius + grow, radius + grow), Brush(WithAlpha(c, a / std::max(c.a, .001f))), blur / steps * 1.4f);
        }
    }
    ID2D1PathGeometry* Geometry(const Glyph& g) {
        auto& p = geometries_[g.d]; if (!p) p = DialogKit::SvgGeometry(factory_, g.d); return p;
    }
    // Draws glyph `g` fitted to `box` (exact Paper icon box), rotated `degrees` about its centre.
    void Icon(const Glyph& g, D2D1_RECT_F box, D2D1_COLOR_F c, float degrees = 0) {
        auto* geometry = Geometry(g); if (!geometry || !target_ || c.a <= 0) return;
        D2D1_MATRIX_3X2_F base; target_->GetTransform(&base);
        const float sx = (box.right - box.left) / g.w, sy = (box.bottom - box.top) / g.h;
        auto m = D2D1::Matrix3x2F::Translation(-g.x, -g.y) * D2D1::Matrix3x2F::Scale(sx, sy) * D2D1::Matrix3x2F::Translation(box.left, box.top);
        if (degrees != 0) m = m * D2D1::Matrix3x2F::Rotation(degrees, {(box.left + box.right) / 2, (box.top + box.bottom) / 2});
        target_->SetTransform(m * base);
        target_->FillGeometry(geometry, Brush(c));
        target_->SetTransform(base);
    }
    void IconBrush(const Glyph& g, D2D1_RECT_F box, ID2D1Brush* brush) {
        auto* geometry = Geometry(g); if (!geometry || !target_) return;
        D2D1_MATRIX_3X2_F base; target_->GetTransform(&base);
        const float sx = (box.right - box.left) / g.w, sy = (box.bottom - box.top) / g.h;
        target_->SetTransform(D2D1::Matrix3x2F::Translation(-g.x, -g.y) * D2D1::Matrix3x2F::Scale(sx, sy) * D2D1::Matrix3x2F::Translation(box.left, box.top) * base);
        target_->FillGeometry(geometry, brush);
        target_->SetTransform(base);
    }
    // Stroked outline glyph (the launcher's tray): SVG stroke-width in glyph units, round caps/joins.
    void IconStroke(const Glyph& g, D2D1_RECT_F box, D2D1_COLOR_F c, float strokeUnits) {
        auto* geometry = Geometry(g); if (!geometry || !target_ || c.a <= 0) return;
        if (!round_) { const auto p = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND); factory_->CreateStrokeStyle(p, nullptr, 0, &round_); }
        D2D1_MATRIX_3X2_F base; target_->GetTransform(&base);
        const float sx = (box.right - box.left) / g.w, sy = (box.bottom - box.top) / g.h;
        target_->SetTransform(D2D1::Matrix3x2F::Translation(-g.x, -g.y) * D2D1::Matrix3x2F::Scale(sx, sy) * D2D1::Matrix3x2F::Translation(box.left, box.top) * base);
        target_->DrawGeometry(geometry, Brush(c), strokeUnits, round_);
        target_->SetTransform(base);
    }
    // A glyph with a halo of `halo` colour around it (Paper « paint-order: stroke », empty state).
    void IconHalo(const Glyph& g, D2D1_RECT_F box, D2D1_COLOR_F fill, D2D1_COLOR_F halo, float haloUnits) {
        auto* geometry = Geometry(g); if (!geometry || !target_) return;
        if (!round_) { const auto p = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND); factory_->CreateStrokeStyle(p, nullptr, 0, &round_); }
        D2D1_MATRIX_3X2_F base; target_->GetTransform(&base);
        const float sx = (box.right - box.left) / g.w, sy = (box.bottom - box.top) / g.h;
        target_->SetTransform(D2D1::Matrix3x2F::Translation(-g.x, -g.y) * D2D1::Matrix3x2F::Scale(sx, sy) * D2D1::Matrix3x2F::Translation(box.left, box.top) * base);
        target_->DrawGeometry(geometry, Brush(halo), haloUnits, round_);
        target_->FillGeometry(geometry, Brush(fill));
        target_->SetTransform(base);
    }
    // ---- text ----
    IDWriteTextLayout* Layout(const std::wstring& text, const Font& f, float width, bool wrap, Align align = Align::Leading) {
        const bool sys = f.mono && f.system;
        Key key{text, f.size, f.line, f.tracking, std::max(1.f, width), f.mono && !sys ? 400 : (f.weight >= 500 ? 500 : 400), f.mono, f.tabular, f.plain, wrap, int(align), sys};
        auto it = layouts_.find(key); if (it != layouts_.end()) return it->second;
        if (layouts_.size() > 512) { for (auto& l : layouts_) Release(l.second); layouts_.clear(); }
        IDWriteTextFormat* format = nullptr;
        if (FAILED(write_->CreateTextFormat(sys ? systemMono_.c_str() : f.mono ? mono_.c_str() : L"Inter Display", sys ? nullptr : f.mono ? (monoBundled_ ? inter_ : nullptr) : inter_,
                key.weight >= 500 ? DWRITE_FONT_WEIGHT_MEDIUM : DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, f.size, L"fr-FR", &format))) return nullptr;
        format->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
        format->SetTextAlignment(align == Align::Center ? DWRITE_TEXT_ALIGNMENT_CENTER : align == Align::Trailing ? DWRITE_TEXT_ALIGNMENT_TRAILING : DWRITE_TEXT_ALIGNMENT_LEADING);
        format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, f.line, Baseline(f));
        IDWriteTextLayout* layout = nullptr;
        const HRESULT hr = write_->CreateTextLayout(text.c_str(), UINT32(text.size()), format, key.width, 100000.f, &layout);
        Release(format); if (FAILED(hr)) return nullptr; ++layoutsCreated_;
        const DWRITE_TEXT_RANGE all{0, UINT32(text.size())};
        if (f.tracking != 0) { IDWriteTextLayout1* l1 = nullptr; if (SUCCEEDED(layout->QueryInterface(__uuidof(IDWriteTextLayout1), reinterpret_cast<void**>(&l1)))) { l1->SetCharacterSpacing(0, f.tracking * f.size, 0, all); Release(l1); } }
        IDWriteTypography* typo = nullptr;
        if (SUCCEEDED(write_->CreateTypography(&typo))) {
            if (!f.mono) for (auto tag : {DWRITE_MAKE_OPENTYPE_TAG('c', 'v', '0', '1'), DWRITE_MAKE_OPENTYPE_TAG('c', 'v', '0', '2'), DWRITE_MAKE_OPENTYPE_TAG('c', 'v', '0', '3'), DWRITE_MAKE_OPENTYPE_TAG('c', 'v', '0', '4'), DWRITE_MAKE_OPENTYPE_TAG('c', 'v', '1', '3')}) typo->AddFontFeature({DWRITE_FONT_FEATURE_TAG(tag), 1});
            if (f.tabular) typo->AddFontFeature({DWRITE_FONT_FEATURE_TAG_TABULAR_FIGURES, 1});
            if (f.plain) { typo->AddFontFeature({DWRITE_FONT_FEATURE_TAG_CONTEXTUAL_ALTERNATES, 0}); typo->AddFontFeature({DWRITE_FONT_FEATURE_TAG_STANDARD_LIGATURES, 0}); }
            layout->SetTypography(typo, all); Release(typo);
        }
        if (!wrap) {
            DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0}; IDWriteInlineObject* ellipsis = nullptr;
            if (SUCCEEDED(write_->CreateEllipsisTrimmingSign(layout, &ellipsis))) { layout->SetTrimming(&trim, ellipsis); Release(ellipsis); }
        }
        layouts_.emplace(std::move(key), layout); return layout;
    }
    float Width(const std::wstring& text, const Font& f) {
        if (text.empty()) return 0; auto* l = Layout(text, f, 100000.f, false); if (!l) return 0;
        DWRITE_TEXT_METRICS m{}; l->GetMetrics(&m); return std::ceil(m.widthIncludingTrailingWhitespace * 4) / 4;
    }
    int Lines(const std::wstring& text, const Font& f, float width) {
        if (text.empty()) return 1; auto* l = Layout(text, f, width, true); if (!l) return 1;
        DWRITE_TEXT_METRICS m{}; l->GetMetrics(&m); return std::max<int>(1, int(m.lineCount));
    }
    float Height(const std::wstring& text, const Font& f, float width) { return Lines(text, f, width) * f.line; }
    // Text from the top of its CSS line box; single lines are centred vertically in `box`
    // when `center` (buttons, rows).
    void Text(const std::wstring& text, const Font& f, D2D1_RECT_F box, D2D1_COLOR_F c, bool wrap = false, Align align = Align::Leading, bool center = false) {
        if (text.empty() || !target_ || c.a <= 0) return;
        auto* l = Layout(text, f, box.right - box.left, wrap, align); if (!l) return;
        float y = box.top; if (center && !wrap) y = box.top + ((box.bottom - box.top) - f.line) / 2;
        target_->DrawTextLayout({box.left, y}, l, Brush(c), D2D1_DRAW_TEXT_OPTIONS_NONE);
    }
    // One line of styled runs (« Annotation ajoutée · Boutique »); the last run trims.
    float Spans(const std::vector<Span>& spans, Font f, D2D1_RECT_F box, bool center = true) {
        float x = box.left;
        for (size_t i = 0; i < spans.size(); ++i) {
            Font g = f; g.weight = spans[i].weight;
            const float w = Width(spans[i].text, g);
            Text(spans[i].text, g, {x, box.top, i + 1 == spans.size() ? box.right : std::min(box.right, x + w + 1), box.bottom}, spans[i].color, false, Align::Leading, center);
            x += w; if (x >= box.right) break;
        }
        return x - box.left;
    }
    // Middle ellipsis for a path (« C:\Users\vous\…\browser-annotations-mcp.exe »).
    std::wstring Middle(const std::wstring& text, const Font& f, float width) {
        if (Width(text, f) <= width) return text;
        const size_t cut = text.find_last_of(L"\\/"); const std::wstring tail = cut == std::wstring::npos ? text.substr(text.size() / 2) : text.substr(cut);
        std::wstring head = text.substr(0, cut == std::wstring::npos ? text.size() / 2 : cut);
        while (!head.empty() && Width(head + L"…" + tail, f) > width) head.pop_back();
        return head + L"…" + tail;
    }
    // The capture thumbnail, cover-fitted and clipped to a rounded box. contain (V3, retours §11): the whole image,
    // centred on the neutral page colour, never cropped and never enlarged beyond its own pixels.
    void Thumbnail(const Shot* shot, D2D1_RECT_F box, float radius, float opacity = 1, bool contain = false) {
        if (!shot || !shot->width || !shot->height || shot->pixels.size() < size_t(shot->width) * shot->height || !target_) return;
        auto& bitmap = bitmaps_[{target_, shot, shot->pixels.data()}];
        if (!bitmap) target_->CreateBitmap(D2D1::SizeU(shot->width, shot->height), shot->pixels.data(), shot->width * 4,
            D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)), &bitmap);
        if (!bitmap) return;
        const float bw = box.right - box.left, bh = box.bottom - box.top;
        float dpiX = 96, dpiY = 96; target_->GetDpi(&dpiX, &dpiY);
        const float k = contain ? std::min({bw / shot->width, bh / shot->height, 96.f / std::max(1.f, dpiX)}) : std::max(bw / shot->width, bh / shot->height);
        if (contain) Fill(box, radius, WithAlpha(Hex(Ink_::page), opacity));
        const float w = shot->width * k, h = shot->height * k, x = box.left + (bw - w) / 2, y = box.top + (bh - h) / 2;
        ID2D1RoundedRectangleGeometry* clip = nullptr; ID2D1Layer* layer = nullptr;
        if (FAILED(factory_->CreateRoundedRectangleGeometry(D2D1::RoundedRect(box, radius, radius), &clip)) || FAILED(target_->CreateLayer(nullptr, &layer))) { Release(clip); return; }
        target_->PushLayer(D2D1::LayerParameters(box, clip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), opacity), layer);
        target_->DrawBitmap(bitmap, {x, y, x + w, y + h}, 1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        if (shot->mark.right > shot->mark.left) {
            const D2D1_RECT_F m{x + shot->mark.left * k, y + shot->mark.top * k, x + shot->mark.right * k, y + shot->mark.bottom * k};
            Stroke(m, 3, Accent(), 1);
        }
        target_->PopLayer(); Release(layer); Release(clip);
    }
    // Opacity group (fades of whole surfaces).
    bool PushOpacity(float opacity, ID2D1Layer*& layer) {
        layer = nullptr; if (opacity >= .999f || !target_) return false;
        if (FAILED(target_->CreateLayer(nullptr, &layer))) return false;
        target_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), Motion::Clamp(opacity)), layer);
        return true;
    }
    void PopOpacity(ID2D1Layer*& layer) { if (layer && target_) { target_->PopLayer(); Release(layer); } }
};

// Keyboard key cap (« Échap », « Entrée »): h 18, radius 3, padding 5, 11/14 medium.
inline float KeycapWidth(Ink& ink, const std::wstring& label, int weight = 500) { return ink.Width(label, F(11, 14, weight)) + 10; }
inline void Keycap(Ink& ink, const std::wstring& label, float x, float cy, D2D1_COLOR_F plate, D2D1_COLOR_F text, int weight = 500) {
    const float w = KeycapWidth(ink, label, weight);
    ink.Fill({x, cy - 9, x + w, cy + 9}, 3, plate);
    ink.Text(label, F(11, 14, weight), {x + 5, cy - 9, x + w - 4, cy + 9}, text, false, Align::Leading, true);
}
// Focus ring (design-system: 1 DIP white, 50 % keyboard / 10 % pointer) just outside `r`.
inline void FocusRing(Ink& ink, D2D1_RECT_F r, float radius, bool keyboard) {
    const D2D1_RECT_F o{r.left - 2, r.top - 2, r.right + 2, r.bottom + 2};
    ink.Stroke(o, radius + 2, {1, 1, 1, Design::Focus::Opacity(keyboard)}, Design::Focus::stroke);
}
inline bool Inside(D2D1_RECT_F r, float x, float y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }
inline D2D1_RECT_F Inflate(D2D1_RECT_F r, float d) { return {r.left - d, r.top - d, r.right + d, r.bottom + d}; }
// Scales a drawn control about its centre (press 0.96).
struct ScaleScope {
    ID2D1RenderTarget* t; D2D1_MATRIX_3X2_F base{};
    ScaleScope(ID2D1RenderTarget* target, D2D1_RECT_F r, float s) : t(target) {
        t->GetTransform(&base);
        if (s != 1) t->SetTransform(D2D1::Matrix3x2F::Scale(s, s, {(r.left + r.right) / 2, (r.top + r.bottom) / 2}) * base);
    }
    ~ScaleScope() { t->SetTransform(base); }
};

// ---- V2 shared pieces ---------------------------------------------------------------------
enum class AgentKind { Claude = 0, Codex = 1, Other = 2 };
inline const wchar_t* AgentName(AgentKind a) { return a == AgentKind::Claude ? L"Claude" : a == AgentKind::Codex ? L"Codex" : L"Agent"; }
// Agent logo in a square box: Claude #D97757, OpenAI (Codex) white, another client: the tree glyph at 60 %.
inline void Logo(Ink& ink, AgentKind a, D2D1_RECT_F box, float opacity = 1) {
    if (a == AgentKind::Claude) ink.Icon(Glyphs::claude, box, WithAlpha(Hex(Ink_::claude), opacity));
    else if (a == AgentKind::Codex) ink.Icon(Glyphs::openai, box, {1, 1, 1, opacity});
    else ink.Icon(Glyphs::tree, box, {1, 1, 1, .6f * opacity});
}
// Raised control (Paper: raised #292929 radius 6): hover -> #333333, press -> #3E3E3E.
inline D2D1_COLOR_F RaisedFill(const Motion::InteractionVisual& iv, float alpha = 1) {
    const auto a = Hex(Ink_::raised), h = Hex(Ink_::hover), p = Hex(Ink_::pressed);
    D2D1_COLOR_F c{Lerp(a.r, h.r, iv.hover), Lerp(a.g, h.g, iv.hover), Lerp(a.b, h.b, iv.hover), alpha};
    c.r = Lerp(c.r, p.r, iv.press); c.g = Lerp(c.g, p.g, iv.press); c.b = Lerp(c.b, p.b, iv.press); return c;
}
// A row of text / glyph / logo / key-cap pieces, measured then drawn left, centred or right in a box.
struct Piece {
    enum Kind { Text, Icon, Stroke, Logo, Key } kind = Text;
    std::wstring text; Font font; D2D1_COLOR_F color{1, 1, 1, 1}; const Glyph* glyph = nullptr; float size = 16, rotate = 0;
    AgentKind agent = AgentKind::Claude; D2D1_COLOR_F plate{0, 0, 0, 0}; float gapAfter = 6; bool underline = false;
};
inline Piece PText(std::wstring t, Font f, D2D1_COLOR_F c, float gap = 6) { Piece p; p.kind = Piece::Text; p.text = std::move(t); p.font = f; p.color = c; p.gapAfter = gap; return p; }
inline Piece PIcon(const Glyph& g, float size, D2D1_COLOR_F c, float gap = 6, float rotate = 0) { Piece p; p.kind = Piece::Icon; p.glyph = &g; p.size = size; p.color = c; p.gapAfter = gap; p.rotate = rotate; return p; }
inline Piece PLogo(AgentKind a, float size, float gap = 6) { Piece p; p.kind = Piece::Logo; p.agent = a; p.size = size; p.gapAfter = gap; return p; }
inline Piece PKey(std::wstring t, D2D1_COLOR_F plate, D2D1_COLOR_F c, float gap = 2) { Piece p; p.kind = Piece::Key; p.text = std::move(t); p.plate = plate; p.color = c; p.gapAfter = gap; return p; }
inline float PieceWidth(Ink& ink, const Piece& p) {
    switch (p.kind) { case Piece::Text: return ink.Width(p.text, p.font); case Piece::Key: return KeycapWidth(ink, p.text); default: return p.size; }
}
inline float PiecesWidth(Ink& ink, const std::vector<Piece>& ps) {
    float w = 0; for (size_t i = 0; i < ps.size(); ++i) w += PieceWidth(ink, ps[i]) + (i + 1 < ps.size() ? ps[i].gapAfter : 0); return w;
}
inline float Pieces(Ink& ink, const std::vector<Piece>& ps, D2D1_RECT_F r, Align align = Align::Center) {
    const float total = PiecesWidth(ink, ps), cy = (r.top + r.bottom) / 2;
    float x = align == Align::Center ? (r.left + r.right - total) / 2 : align == Align::Trailing ? r.right - total : r.left;
    x = std::floor(x * 2 + .5f) / 2;
    for (const auto& p : ps) {
        const float w = PieceWidth(ink, p);
        switch (p.kind) {
        case Piece::Text: {
            const float right = std::min(r.right, x + w + 1);
            ink.Text(p.text, p.font, {x, r.top, right, r.bottom}, p.color, false, Align::Leading, true);
            if (p.underline) { const float base = cy - p.font.line / 2 + ink.BaselineOf(p.font) + 3; ink.Fill({x, base - .5f, x + w, base + .5f}, 0, p.color); }
            break; }
        case Piece::Icon: ink.Icon(*p.glyph, {x, cy - p.size / 2, x + p.size, cy + p.size / 2}, p.color, p.rotate); break;
        case Piece::Stroke: ink.IconStroke(*p.glyph, {x, cy - p.size / 2, x + p.size, cy + p.size / 2}, p.color, 2); break;
        case Piece::Logo: Logo(ink, p.agent, {x, cy - p.size / 2, x + p.size, cy + p.size / 2}, p.color.a); break;
        case Piece::Key: Keycap(ink, p.text, x, cy, p.plate, p.color); break;
        }
        x += w + p.gapAfter;
    }
    return total;
}
// Close button 26/28 (Paper « Fermer »): glyph at 70 %, hover plate 8 % white.
inline void CloseButton(Ink& ink, D2D1_RECT_F r, float glyph, const Motion::InteractionVisual& iv, bool focus, bool keyboard, bool reduced) {
    ScaleScope s(ink.Target(), r, Motion::PressScale(iv.press, reduced));
    if (iv.hover > 0 || iv.press > 0) ink.Fill(r, 6, {1, 1, 1, .08f * iv.hover + .04f * iv.press});
    const float cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
    ink.Icon(Glyphs::close, {cx - glyph / 2, cy - glyph / 2, cx + glyph / 2, cy + glyph / 2}, {1, 1, 1, .7f + .3f * iv.hover});
    if (focus) FocusRing(ink, r, 6, keyboard);
}
// Rounded clip for scrolling content and morphing cards (pushes a layer; pair with PopClip).
inline bool PushRoundClip(Ink& ink, D2D1_RECT_F r, float radius, ID2D1Layer*& layer, ID2D1Geometry*& geometry, float opacity = 1) {
    layer = nullptr; geometry = nullptr; auto* t = ink.Target(); if (!t) return false;
    ID2D1RoundedRectangleGeometry* g = nullptr;
    if (FAILED(ink.Factory()->CreateRoundedRectangleGeometry(D2D1::RoundedRect(r, radius, radius), &g))) return false;
    if (FAILED(t->CreateLayer(nullptr, &layer))) { g->Release(); return false; }
    geometry = g;
    t->PushLayer(D2D1::LayerParameters(r, geometry, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), Motion::Clamp(opacity)), layer);
    return true;
}
inline void PopClip(Ink& ink, ID2D1Layer*& layer, ID2D1Geometry*& geometry) { if (layer && ink.Target()) ink.Target()->PopLayer(); Release(layer); Release(geometry); }
// Draws `paint` with a transform (scale about `origin`, then translate) and an opacity group.
template<class F> inline void Posed(Ink& ink, const Pose& p, D2D1_POINT_2F origin, F&& paint) {
    auto* t = ink.Target(); D2D1_MATRIX_3X2_F base; t->GetTransform(&base);
    t->SetTransform(D2D1::Matrix3x2F::Scale(p.scale, p.scale, origin) * D2D1::Matrix3x2F::Translation(p.dx, p.dy) * base);
    ID2D1Layer* group = nullptr; const bool pushed = ink.PushOpacity(p.opacity, group);
    paint(); if (pushed) ink.PopOpacity(group);
    t->SetTransform(base);
}
inline std::wstring Plural(int n, const wchar_t* one, const wchar_t* many) { return std::to_wstring(n) + L" " + (n == 1 ? one : many); }

// ---- Animator: on-demand, display-paced frames for every annotation surface -------------
class Animator {
public:
    struct Client { virtual ~Client() = default; virtual bool Frame(Motion::Time now) = 0; };
private:
    static constexpr UINT FrameMessage = WM_APP + 0x7A1;
    static constexpr UINT_PTR FallbackTimer = 1;
    HWND hwnd_ = nullptr; FrameClock::Clock clock_; bool clockTried_ = false, fallback_ = false;
    std::vector<Client*> active_;
    unsigned long long frames_ = 0, fallbackStarts_ = 0;
    static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<Animator*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (m == WM_NCCREATE) { self = static_cast<Animator*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); }
        if (self && m == FrameMessage) { self->clock_.Acknowledge(); if (l == FrameClock::ClockLost) self->clock_.Stop(); self->Tick(); return 0; }
        if (self && m == WM_TIMER && w == FallbackTimer) { self->Tick(); return 0; }
        return DefWindowProcW(h, m, w, l);
    }
    bool Ensure() {
        if (hwnd_) return true;
        HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = instance; wc.lpszClassName = L"BrowserAnnotationAnimator";
        RegisterClassW(&wc);
        hwnd_ = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, this);
        return hwnd_ != nullptr;
    }
    void Pump() {
        if (!clockTried_) { clockTried_ = true; clock_.Start(hwnd_, FrameMessage); }
        if (clock_.Running()) clock_.Request();
        else if (!fallback_) { fallback_ = true; ++fallbackStarts_; SetTimer(hwnd_, FallbackTimer, 16, nullptr); }
    }
    void Tick() {
        ++frames_;
        const auto now = Motion::Now(); auto list = active_; active_.clear();
        for (auto* c : list) if (c->Frame(now) && std::find(active_.begin(), active_.end(), c) == active_.end()) active_.push_back(c);
        if (!active_.empty()) Pump();
        else { clock_.Cancel(); if (fallback_) { KillTimer(hwnd_, FallbackTimer); fallback_ = false; } }
    }
public:
    static Animator*& Instance() { static Animator* a = nullptr; return a; }
    static Animator& Get() { auto*& a = Instance(); if (!a) a = new Animator(); return *a; }
    static bool Exists() { return Instance() != nullptr; }
    ~Animator() { clock_.Stop(); if (hwnd_) DestroyWindow(hwnd_); }
    // A surface wants frames until its Frame() returns false. Idempotent.
    void Request(Client* c) {
        if (!c || !Ensure()) return;
        if (std::find(active_.begin(), active_.end(), c) == active_.end()) active_.push_back(c);
        Pump();
    }
    void Forget(Client* c) { active_.erase(std::remove(active_.begin(), active_.end(), c), active_.end()); }
    bool Animating() const { return !active_.empty(); }
    bool ClockRunning() const { return clock_.Running(); }
    bool FallbackTimerArmed() const { return fallback_; }
    unsigned long long Frames() const { return frames_; }
    unsigned long long FallbackStarts() const { return fallbackStarts_; }
    const FrameClock::Counters* Stats() const { return clock_.Stats(); }
};

// ---- Layer: per-pixel alpha surface ----------------------------------------------------
// Child (launcher, card, banner: above the WebView inside the browser window) or owned
// popup (ghost, list, tooltip, modal). Fully transparent pixels are not hit.
class Layer {
public:
    struct Options { bool child = false, activatable = false, clickThrough = false, tabStop = false; const wchar_t* title = L""; };
    std::function<void(Ink&, float w, float h)> paint;                // DIP; target already bound
    std::function<bool(UINT, WPARAM, LPARAM, LRESULT&)> handle;      // input; true = handled
    Accessibility::Bridge uia;
private:
    HWND hwnd_ = nullptr, owner_ = nullptr; Options options_; bool shown_ = false;
    HDC mem_ = nullptr; HBITMAP dib_ = nullptr; HGDIOBJ old_ = nullptr; void* bits_ = nullptr; int bw_ = 0, bh_ = 0;
    ID2D1DCRenderTarget* rt_ = nullptr; UINT rtDpi_ = 0;
    RECT rect_{}; unsigned long long presents_ = 0;
    static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<Layer*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (m == WM_NCCREATE) { self = static_cast<Layer*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); self->hwnd_ = h; }
        if (self) {
            if (m == WM_GETOBJECT) { LRESULT r; if (self->uia.OnGetObject(h, w, l, r)) return r; }
            if (m == WM_MOUSEACTIVATE && !self->options_.activatable) return MA_NOACTIVATE;
            if (m == WM_NCDESTROY) { self->hwnd_ = nullptr; self->shown_ = false; }
            LRESULT r = 0; if (self->handle && self->handle(m, w, l, r)) return r;
            if (m == WM_ERASEBKGND) return 1;
            if (m == WM_PAINT) { PAINTSTRUCT ps; BeginPaint(h, &ps); EndPaint(h, &ps); return 0; }
        }
        return DefWindowProcW(h, m, w, l);
    }
    void DropTarget() { if (rt_) { if (auto* ink = Ink::Instance()) ink->Forget(rt_); } Release(rt_); }
    void DropBitmap() { if (mem_) { if (old_) SelectObject(mem_, old_); DeleteDC(mem_); } if (dib_) DeleteObject(dib_); mem_ = nullptr; dib_ = nullptr; old_ = nullptr; bits_ = nullptr; bw_ = bh_ = 0; }
    bool EnsureBitmap(int w, int h) {
        if (mem_ && w <= bw_ && h <= bh_ && w * 2 > bw_ && h * 2 > bh_) return true;   // reuse while it fits
        DropBitmap();
        BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
        HDC screen = GetDC(nullptr); mem_ = CreateCompatibleDC(screen); dib_ = CreateDIBSection(mem_, &bi, DIB_RGB_COLORS, &bits_, nullptr, 0); ReleaseDC(nullptr, screen);
        if (!dib_ || !bits_) { DropBitmap(); return false; }
        old_ = SelectObject(mem_, dib_); bw_ = w; bh_ = h; return true;
    }
public:
    Layer() = default; Layer(const Layer&) = delete; Layer& operator=(const Layer&) = delete;
    ~Layer() { Destroy(); }
    static unsigned& Created() { static unsigned n = 0; return n; }
    bool Create(HINSTANCE instance, HWND owner, Options options) {
        if (hwnd_) return true;
        owner_ = owner; options_ = options;
        WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = instance; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.lpszClassName = L"BrowserAnnotationLayer";
        RegisterClassW(&wc);
        DWORD ex = WS_EX_LAYERED | (options.child ? 0 : WS_EX_TOOLWINDOW) | (options.activatable ? 0 : WS_EX_NOACTIVATE) | (options.clickThrough ? WS_EX_TRANSPARENT : 0);
        DWORD style = options.child ? (WS_CHILD | WS_CLIPSIBLINGS | (options.tabStop ? WS_TABSTOP : 0)) : WS_POPUP;
        CreateWindowExW(ex, wc.lpszClassName, options.title, style, 0, 0, 1, 1, owner, nullptr, instance, this);
        if (hwnd_) ++Created();
        return hwnd_ != nullptr;
    }
    void Destroy() { uia.Detach(); DropTarget(); DropBitmap(); if (hwnd_ && IsWindow(hwnd_)) { HWND h = hwnd_; hwnd_ = nullptr; SetWindowLongPtrW(h, GWLP_USERDATA, 0); DestroyWindow(h); } hwnd_ = nullptr; shown_ = false; }
    HWND Window() const { return hwnd_; }
    bool Shown() const { return shown_; }
    UINT Dpi() const { const UINT d = GetDpiForWindow(hwnd_ ? hwnd_ : owner_); return d ? d : 96; }
    float Scale() const { return Dpi() / 96.f; }
    RECT Rect() const { return rect_; }          // last presented rect (screen, or parent client for a child)
    unsigned long long Presents() const { return presents_; }
    // Renders `paint` into a w x h pixel surface and shows it at r (screen px for a popup,
    // parent-client px for a child). The window size equals r; everything else is alpha.
    bool Present(RECT r, bool raise = false) {
        if (!hwnd_) return false;
        Ink* ink = Ink::Instance(); if (!ink) return false;
        const int w = std::max<LONG>(1, r.right - r.left), h = std::max<LONG>(1, r.bottom - r.top);
        if (!EnsureBitmap(w, h)) return false;
        const UINT dpi = Dpi();
        if (!rt_ || rtDpi_ != dpi) {
            DropTarget();
            const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), float(dpi), float(dpi));
            if (FAILED(ink->Factory()->CreateDCRenderTarget(&props, &rt_))) return false;
            rt_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE); rtDpi_ = dpi;
        }
        RECT bind{0, 0, w, h};
        if (FAILED(rt_->BindDC(mem_, &bind))) return false;
        ink->Bind(rt_);
        rt_->BeginDraw(); rt_->SetTransform(D2D1::IdentityMatrix()); rt_->Clear(D2D1::ColorF(0, 0, 0, 0));
        if (paint) paint(*ink, w * 96.f / dpi, h * 96.f / dpi);
        const HRESULT hr = rt_->EndDraw();
        if (hr == D2DERR_RECREATE_TARGET) { DropTarget(); return false; }
        POINT src{0, 0}; SIZE size{w, h}; BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        if (options_.child) {
            // V3 (no flash): position, size and pixels in ONE update. (Moving with SetWindowPos first gave the
            // compositor a frame of the old pixels at the new place / size.) pptDst = parent-client px for a child.
            POINT dst{r.left, r.top};
            UpdateLayeredWindow(hwnd_, nullptr, &dst, &size, mem_, &src, 0, &blend, ULW_ALPHA);
            if (raise) SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOREDRAW);
        } else {
            POINT dst{r.left, r.top};
            HDC screen = GetDC(nullptr); UpdateLayeredWindow(hwnd_, screen, &dst, &size, mem_, &src, 0, &blend, ULW_ALPHA); ReleaseDC(nullptr, screen);
            if (raise) SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        rect_ = r; ++presents_;
        if (!shown_) { ShowWindow(hwnd_, options_.activatable ? SW_SHOW : SW_SHOWNOACTIVATE); shown_ = true; }
        return true;
    }
    void Hide() { if (hwnd_ && shown_) { ShowWindow(hwnd_, SW_HIDE); shown_ = false; } }
    // Last presented pixels (premultiplied BGRA, top-down) — tests and QA captures.
    std::vector<std::uint32_t> Pixels() const {
        std::vector<std::uint32_t> out; if (!bits_) return out;
        const int w = rect_.right - rect_.left, h = rect_.bottom - rect_.top; out.resize(size_t(w) * h);
        for (int y = 0; y < h; ++y) std::copy_n(static_cast<const std::uint32_t*>(bits_) + size_t(y) * bw_, w, out.data() + size_t(y) * w);
        return out;
    }
    // Screen rect of a DIP rect of this surface.
    RECT Screen(D2D1_RECT_F r) const {
        const float s = Scale(); RECT p{LONG(std::lround(r.left * s)), LONG(std::lround(r.top * s)), LONG(std::lround(r.right * s)), LONG(std::lround(r.bottom * s))};
        RECT w{}; if (hwnd_) GetWindowRect(hwnd_, &w); OffsetRect(&p, w.left, w.top); return p;
    }
};

// ---- Offscreen rendering (tests, QA captures): the same paint code, WIC bitmap target ----
struct Canvas {
    unsigned width = 0, height = 0; std::vector<std::uint32_t> pixels; // premultiplied BGRA, top-down
    std::uint32_t At(int x, int y) const { if (x < 0 || y < 0 || unsigned(x) >= width || unsigned(y) >= height) return 0; return pixels[size_t(y) * width + x]; }
};
// Renders paint(ink, wDip, hDip) at `dpi` over an opaque `backdrop` (#RRGGBBAA, alpha ignored).
inline bool RenderOffscreen(Ink& ink, float wDip, float hDip, UINT dpi, std::uint32_t backdrop, const std::function<void(Ink&, float, float)>& paint, Canvas& out) {
    const UINT w = UINT(std::lround(wDip * dpi / 96.f)), h = UINT(std::lround(hDip * dpi / 96.f));
    IWICBitmap* bitmap = nullptr; ID2D1RenderTarget* t = nullptr;
    if (FAILED(ink.Wic()->CreateBitmap(w, h, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap))) return false;
    const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), float(dpi), float(dpi));
    if (FAILED(ink.Factory()->CreateWicBitmapRenderTarget(bitmap, props, &t))) { Release(bitmap); return false; }
    t->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    ink.Bind(t); t->BeginDraw(); const auto c = Hex(backdrop); t->Clear(D2D1::ColorF(c.r, c.g, c.b, 1));
    paint(ink, wDip, hDip);
    const HRESULT hr = t->EndDraw(); ink.Forget(t); Release(t);
    if (FAILED(hr)) { Release(bitmap); return false; }
    out.width = w; out.height = h; out.pixels.assign(size_t(w) * h, 0);
    WICRect all{0, 0, INT(w), INT(h)}; const HRESULT copy = bitmap->CopyPixels(&all, w * 4, UINT(out.pixels.size() * 4), reinterpret_cast<BYTE*>(out.pixels.data()));
    Release(bitmap); return SUCCEEDED(copy);
}
inline HRESULT SavePng(Ink& ink, const Canvas& c, const std::wstring& path) {
    IWICBitmap* bitmap = nullptr; IWICStream* stream = nullptr; IWICBitmapEncoder* encoder = nullptr; IWICBitmapFrameEncode* frame = nullptr;
    HRESULT hr = ink.Wic()->CreateBitmapFromMemory(c.width, c.height, GUID_WICPixelFormat32bppPBGRA, c.width * 4, UINT(c.pixels.size() * 4), const_cast<BYTE*>(reinterpret_cast<const BYTE*>(c.pixels.data())), &bitmap);
    if (SUCCEEDED(hr)) hr = ink.Wic()->CreateStream(&stream);
    if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
    if (SUCCEEDED(hr)) hr = ink.Wic()->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (SUCCEEDED(hr)) hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(&frame, nullptr);
    if (SUCCEEDED(hr)) hr = frame->Initialize(nullptr);
    if (SUCCEEDED(hr)) hr = frame->WriteSource(bitmap, nullptr);
    if (SUCCEEDED(hr)) hr = frame->Commit();
    if (SUCCEEDED(hr)) hr = encoder->Commit();
    Release(frame); Release(encoder); Release(stream); Release(bitmap); return hr;
}
}} // namespace NativeChrome::Annotations
