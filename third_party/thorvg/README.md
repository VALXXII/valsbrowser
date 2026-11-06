ThorVG 1.1.3 (MIT), official source https://github.com/thorvg/thorvg/releases/tag/v1.1.3

Pinned archive SHA256: `03850904c300e5969560d49446eab3e534c4160c379909f32bc49a32ea6f4072`.
Run `python build-lottie.py` using Python 3.12 to fetch, verify, and build locally.
It bootstraps Meson 1.9.1 and Ninja 1.13.0 into ignored tools/lottie-build.
Source lives in ignored deps/thorvg-1.1.3; static library in build/lottie-backend.
No system installation; child-process tool paths only. Local GCC 16.2.0 has no
LTO support, so LTO is disabled. CPU-only, single-thread, SVG and Lottie loaders;
no expressions/JavaScript, GPU, external images/fonts, file loading, or savers.

This is ThorVG's native Lottie renderer, not a custom tween interpreting icon names.
Production assets exercise cubic paths, morphs, stroke, trim, opacity and transforms.
Other ThorVG features have not been certified by this task. External resources and
expressions are deliberately unavailable; bundled icons must be self-contained.
The icon adapter rejects files over 256 KiB, durations over 10 seconds, more than
120 frames, and output larger than 64x64. These are documented icon resource bounds.
It caches at most one pixel resolution per asset and frees caches on DPI change,
reload, and teardown. The source archive includes upstream dependency notices.
