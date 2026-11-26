"""Build and run the native update-card checks (no WebView2, no network, nothing on the screen).

    python tests/update-toast/run.py            # build toast-tests.exe + preview.exe, run the tests, write PNG snapshots,
                                                # then compare the native card with Val's validated preview (compare.py)
    python tests/update-toast/run.py --build    # build only
    python tests/update-toast/run.py --no-window  # skip the popup-window tests

The popup-window tests run on a Windows desktop of their own that is never displayed (toast_tests.cpp creates it
before any window): nothing reaches the screen, the mouse, the keyboard or the foreground.

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
    command = [os.path.join(OUT, 'toast-tests.exe'), ROOT, snapshots] + [a for a in argv if a == '--no-window']
    print(' '.join(command), flush=True)
    result = subprocess.run(command, cwd=ROOT, timeout=600)
    if result.returncode:
        return result.returncode
    compare = [sys.executable, os.path.join(ROOT, 'tests', 'update-toast', 'compare.py'), snapshots]
    print(' '.join(compare), flush=True)
    return subprocess.run(compare, cwd=ROOT, timeout=300).returncode


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
