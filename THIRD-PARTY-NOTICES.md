# Third-party notices

The source code of this project is released under the MIT License (see [LICENSE](LICENSE)).
The components below come from other authors. They keep their own licence, and the MIT License
of this repository does not replace it. Names, logos and screenshots of other companies are
covered separately in [TRADEMARKS.md](TRADEMARKS.md).

## Code adapted or ported into the sources

| Component | Licence | Where it is used | Licence and notice files |
|---|---|---|---|
| Gui (github.com/brijr/gui, commit 0af5e38), Copyright 2026 Bridger Tower | Apache-2.0 | Logic adapted and rewritten in C++ in `src/acp-client.cpp` and `src/agent-runtime.cpp` | `third_party/gui/LICENSE`, `third_party/gui/NOTICE` |
| Paper Shaders (`@paper-design/shaders` 0.0.82), Copyright Lost Coast Labs, Inc. | Apache-2.0 | HLSL port of the vertex shader and of the HalftoneDots, GemSmoke and GrainGradient fragment shaders in `src/onboarding-shaders.h`; the library's noise texture in `assets/onboarding/noise.png` | `third_party/paper-shaders/LICENSE`, `third_party/paper-shaders/NOTICE` |

Paper Shaders notice: Powered by Paper Shaders: https://shaders.paper.design

Gui notice: This product includes software developed by Bridger Tower (https://usegui.com).

## Libraries fetched at build time and linked into the application

These are not stored in this repository. `setup.ps1` and `build-lottie.py` download pinned
versions and check their SHA-256.

| Component | Licence | Notes | Licence files |
|---|---|---|---|
| ThorVG 1.1.3 | MIT | Lottie and SVG rendering, built as a static library | `third_party/thorvg/LICENSE` |
| RapidJSON (bundled with ThorVG) | MIT | JSON parser used by ThorVG | `third_party/thorvg/RapidJSON-LICENSE` |
| Microsoft Edge WebView2 SDK 1.0.4314.35 | Microsoft WebView2 SDK licence (BSD-style) | Headers and `WebView2Loader.dll` | shipped with each build as `WebView2-LICENSE.txt` and `WebView2-NOTICE.txt` |
| w64devkit 2.11.0 (GCC, MinGW-w64) | GCC runtime libraries: GPL-3.0 with the GCC Runtime Library Exception. MinGW-w64 runtime: its own permissive licences | Build toolchain. The executables are linked statically, so they contain these runtime libraries | see the w64devkit distribution |

## Component shipped next to the application, unmodified

| Component | Licence | Notes | Notice |
|---|---|---|---|
| uBlock Origin Lite 2026.1103.1743, Copyright (C) Raymond Hill and contributors | GPL-3.0 | The ad blocker. It is downloaded by `fetch-ublock-lite.py`, never stored in this repository, and shipped unmodified as a separate extension folder. It does not change the licence of this project's own code. | `third_party/ublock-lite/NOTICE.txt` (includes the link to the corresponding source) |

## Icons

| Component | Licence | Where it is used | Licence file |
|---|---|---|---|
| Phosphor Icons (Bold and Fill 2.1.1), Copyright (c) 2023 Phosphor Icons | MIT | `assets/icons/phosphor-bold`, paths in `src/profile-icons.h` and other `src/*.h` headers, `assets/chat-panel/icons.js` | `assets/icons/phosphor-bold/LICENSE` |

## Fonts

The fonts stay under the SIL Open Font License 1.1, whatever the licence of the code.

| Font | Licence | Where it is used | Licence file |
|---|---|---|---|
| Inter and Inter Display | OFL-1.1 | `assets/fonts`, and a subset embedded in `src/annotation-font.h` | `assets/fonts/Inter-LICENSE.txt` |
| JetBrains Mono | OFL-1.1 | `assets/fonts` | `assets/fonts/JetBrainsMono-OFL.txt` |
| Gelasio | OFL-1.1 | `assets/fonts` | `assets/fonts/Gelasio-OFL.txt` |

## Downloaded by the application at run time

The integrated chat downloads Node.js and the agent adapters (`claude-agent-acp`, `codex-acp`
and their dependencies) from their official sources the first time the chat needs them. They are
not part of this repository or of the installer, and each one comes with its own licence. The
exact versions and checksums are pinned in `src/agent-runtime-pins.inc`.
