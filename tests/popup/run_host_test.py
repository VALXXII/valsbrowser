"""Build and run the window.open request lifecycle fakes (tests/popup/popup_host_test.cpp). No WebView2, no window.

    python -I tests/popup/run_host_test.py
"""
import pathlib, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / 'build' / 'popup-tests'
GXX = ROOT / 'tools' / 'w64devkit' / 'bin' / 'g++.exe'
OUT.mkdir(parents=True, exist_ok=True)
exe = OUT / 'popup_host_test.exe'
build = subprocess.run([str(GXX), '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-static', 'tests/popup/popup_host_test.cpp',
                        '-o', str(exe)], cwd=ROOT)
if build.returncode:
    sys.exit(build.returncode)
sys.exit(subprocess.run([str(exe)]).returncode)
