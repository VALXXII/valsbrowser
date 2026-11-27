"""Build and run the native update-card checks (no WebView2, no network, nothing on the screen).

    python tests/update-toast/run.py            # build toast-tests.exe + preview.exe, run the tests, write PNG snapshots,
                                                # then compare the native card with Val's validated preview (compare.py)
    python tests/update-toast/run.py --build    # build only
    python tests/update-toast/run.py --no-window  # skip the popup-window tests
    python tests/update-toast/run.py --like-runner  # as on GitHub's runners: no GPU (the background on WARP) and
                                                    # every window at 96 dpi (DPI-unaware), whatever this machine has

The popup-window tests run on a Windows desktop of their own that is never displayed (toast_tests.cpp creates it
before any window): nothing reaches the screen, the mouse, the keyboard or the foreground. So does
card_layouts_host_test.cpp (src/main.cpp itself: the card in the three tab layouts, next to the drawers).

Outputs: build/update-toast/{toast-tests.exe,preview.exe,snapshots/*.png,compare/*}. Exit code != 0 on any failure.
Tools: tools/w64devkit, deps/thorvg-1.1.3, build/lottie-backend of this checkout (or W64DEVKIT_BIN / THORVG_INCLUDE /
THORVG_LIB).
"""
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
OUT = os.path.join(ROOT, 'build', 'update-toast')
GXX = os.path.join(os.environ.get('W64DEVKIT_BIN') or os.path.join(ROOT, 'tools', 'w64devkit', 'bin'), 'g++.exe')
TVG_INCLUDE = os.environ.get('THORVG_INCLUDE') or os.path.join(ROOT, 'deps', 'thorvg-1.1.3', 'inc')
THORVG = os.environ.get('THORVG_LIB') or os.path.join(ROOT, 'build', 'lottie-backend', 'src', 'libthorvg-1.a')
FLAGS = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-unknown-pragmas', '-Wno-cast-function-type',
         '-Wno-missing-field-initializers', '-static', '-municode', '-Isrc', '-DTVG_STATIC', '-I' + TVG_INCLUDE]
# The card draws the Downloads drawer's Lottie frames (src/native-lottie.cpp + ThorVG) and the GrainGradient
# (Direct3D 11 and its compiler are loaded at run time by src/onboarding-shaders.h: no import library).
LIBS = ['src/native-lottie.cpp', THORVG, '-ld2d1', '-ldwrite', '-ldwmapi', '-lgdi32', '-luser32', '-lole32', '-luuid',
        '-lcomctl32', '-lwindowscodecs']

# src/main.cpp itself (the card's placement in the browser's three tab layouts): tests/update-toast/card_layouts_host_test.cpp,
# stamped like a dev build (BROWSER_HAS_BUILD_INFO: main.cpp compiles the update host, the annotation mode and its chat
# in), exactly as tests/update-integration/e2e_install.py builds its host.
def build_host(exe):
    sys.path.insert(0, os.path.join(ROOT, 'tools', 'release'))
    import build as B      # noqa: E402  (tools/release/build.py: sources, libraries, build-info.h)
    import version as V    # noqa: E402
    gen = os.path.join(OUT, 'host-gen')
    from pathlib import Path
    B.write_build_info(Path(gen), V.identity(1, 'f' * 40, 'dev'), 'VALXXII/browser-releases')
    command = [GXX, '-std=c++17', '-O1', *B.WARNINGS, '-static', '-municode', '-Isrc', '-I' + gen, '-DBROWSER_HAS_BUILD_INFO',
               '-DTVG_STATIC', '-I' + TVG_INCLUDE, f'-I{B.SDK / "include"}', 'tests/update-toast/card_layouts_host_test.cpp',
               'src/native-lottie.cpp', 'src/update-core.cpp', 'src/update-package.cpp', 'src/update-toast.cpp', 'src/annotations.cpp',
               'src/agent-connect.cpp', *B.CHAT_SOURCES, str(B.THORVG), '-o', exe, *B.BROWSER_LIBS, *B.CHAT_LIBS]
    print(' '.join(str(c) for c in command), flush=True)
    return subprocess.run([str(c) for c in command], cwd=ROOT).returncode


TARGETS = {
    'toast-tests.exe': (['tests/update-toast/toast_tests.cpp'], ['-mconsole']),
    'preview.exe': (['tests/update-toast/preview.cpp', 'src/update-toast.cpp'], ['-mwindows']),
}


def build():
    os.makedirs(OUT, exist_ok=True)
    for name, (sources, mode) in TARGETS.items():
        command = [GXX] + FLAGS + mode + sources + ['-o', os.path.join(OUT, name)] + LIBS
        print(' '.join(command), flush=True)
        result = subprocess.run(command, cwd=ROOT)
        if result.returncode != 0:
            print(f'build failed: {name}', file=sys.stderr)
            return result.returncode
    return 0


def main(argv):
    code = build()
    if code or '--build' in argv:
        return code
    snapshots = os.path.join(OUT, 'snapshots')
    os.makedirs(snapshots, exist_ok=True)
    runner = ['--cpu-raster', '--dpi-unaware'] if '--like-runner' in argv else []
    command = [os.path.join(OUT, 'toast-tests.exe'), ROOT, snapshots] + [a for a in argv if a == '--no-window'] + runner
    print(' '.join(command), flush=True)
    result = subprocess.run(command, cwd=ROOT, timeout=600)
    if result.returncode:
        return result.returncode
    if '--no-window' not in argv:
        host = os.path.join(OUT, 'card-layouts-host-test.exe')
        if build_host(host):
            print('build failed: card-layouts-host-test.exe', file=sys.stderr)
            return 1
        layouts = os.path.join(OUT, 'layouts')
        os.makedirs(layouts, exist_ok=True)
        result = subprocess.run([host, ROOT, layouts] + (['--dpi-unaware'] if runner else []), cwd=ROOT, timeout=600)
        if result.returncode:
            return result.returncode
    compare = [sys.executable, os.path.join(ROOT, 'tests', 'update-toast', 'compare.py'), snapshots]
    print(' '.join(compare), flush=True)
    return subprocess.run(compare, cwd=ROOT, timeout=300).returncode


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
