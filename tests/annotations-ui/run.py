"""Build and run the native annotation-mode UI harness, V2 (no WebView2, no network).

    python tests/annotations-ui/run.py              # build, run every test, write PNG captures + report.json
    python tests/annotations-ui/run.py --build      # build only
    python tests/annotations-ui/run.py --no-window  # skip live-window tests (headless CI without a desktop)

Outputs: build/annotations-ui/annotations-ui-test.exe and build/annotations-ui-qa/v2/{*.png,report.json}.
Exit code != 0 on any build or test failure.
"""
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
BIN = os.path.join(ROOT, 'build', 'annotations-ui')
QA = os.path.join(ROOT, 'build', 'annotations-ui-qa', 'v2')
TOOLS = os.path.join(ROOT, 'tools', 'w64devkit', 'bin')
GXX = os.path.join(TOOLS, 'g++.exe')
WINDRES = os.path.join(TOOLS, 'windres.exe')
FLAGS = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-unknown-pragmas', '-Wno-cast-function-type',
         '-Wno-missing-field-initializers', '-Wno-misleading-indentation', '-static', '-municode', '-mconsole', '-Isrc']
LIBS = ['-ld2d1', '-ldwrite', '-lwindowscodecs', '-ldwmapi', '-lgdi32', '-luser32', '-lole32', '-luuid', '-lcomctl32']


def build():
    os.makedirs(BIN, exist_ok=True)
    res = os.path.join(BIN, 'harness-res.o')
    steps = [
        [WINDRES, os.path.join('tests', 'annotations-ui', 'harness.rc'), '-O', 'coff', '-o', res],
        [GXX] + FLAGS + [os.path.join('tests', 'annotations-ui', 'annotations_ui_test.cpp'), res,
                         '-o', os.path.join(BIN, 'annotations-ui-test.exe')] + LIBS,
    ]
    for command in steps:
        print(' '.join(command), flush=True)
        result = subprocess.run(command, cwd=ROOT)
        if result.returncode != 0:
            print('build failed', file=sys.stderr)
            return result.returncode
    return 0


def main(argv):
    code = build()
    if code or '--build' in argv:
        return code
    os.makedirs(QA, exist_ok=True)
    for name in os.listdir(QA):  # a clean capture set per run
        if name.endswith('.png') or name == 'report.json':
            os.remove(os.path.join(QA, name))
    command = [os.path.join(BIN, 'annotations-ui-test.exe'), ROOT, QA] + [a for a in argv if a == '--no-window']
    print(' '.join(command), flush=True)
    return subprocess.run(command, cwd=ROOT, timeout=600).returncode


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
