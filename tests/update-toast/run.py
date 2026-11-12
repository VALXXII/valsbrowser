"""Build and run the native update-toast checks (no WebView2, no network).

    python tests/update-toast/run.py            # build toast-tests.exe + preview.exe, run tests, write PNG snapshots
    python tests/update-toast/run.py --build    # build only
    python tests/update-toast/run.py --no-window  # skip popup-window tests (headless CI without a desktop)

Outputs: build/update-toast/{toast-tests.exe,preview.exe,snapshots/*.png}. Exit code != 0 on any failure.
"""
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
OUT = os.path.join(ROOT, 'build', 'update-toast')
GXX = os.path.join(ROOT, 'tools', 'w64devkit', 'bin', 'g++.exe')
FLAGS = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-unknown-pragmas', '-Wno-cast-function-type',
         '-Wno-missing-field-initializers', '-static', '-municode', '-Isrc']
LIBS = ['-ld2d1', '-ldwrite', '-ldwmapi', '-lgdi32', '-luser32', '-lole32', '-luuid', '-lcomctl32']  # comctl32: native-ui.h window subclassing

TARGETS = {
    'toast-tests.exe': (['tests/update-toast/toast_tests.cpp'], ['-mconsole'], LIBS + ['-lwindowscodecs']),
    'preview.exe': (['tests/update-toast/preview.cpp', 'src/update-toast.cpp'], ['-mwindows'], LIBS),
}


def build():
    os.makedirs(OUT, exist_ok=True)
    for name, (sources, mode, libs) in TARGETS.items():
        command = [GXX] + FLAGS + mode + sources + ['-o', os.path.join(OUT, name)] + libs
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
    result = subprocess.run(command, cwd=ROOT, timeout=300)
    return result.returncode


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
