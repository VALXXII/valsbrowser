#!/usr/bin/env python3
"""WebView2 startup recovery suite (design/webview2-startup-recovery.md).

    python -I tests/webview2-runtime/run_tests.py [--bootstrapper PATH] [--loader PATH ...]

  runtime_test   classification, overrides, URL policy, controller state machine (fake hooks)
  install_test   private dir ACL, download/trust/launch pipeline with a FAKE network and a FAKE
                 setup program (real CreateProcess), single-flight worker, REAL Authenticode checks
                 on local signed files (+ --bootstrapper: a real MicrosoftEdgeWebview2Setup.exe,
                 verified only, never executed)
  host_test      production main.cpp startup against a FAKE WebView2Loader.dll, 6 scenarios
  loader_probe   read-only: the REAL loader's detection answer on this machine (--loader, default
                 the SDK loader); never creates an environment

Nothing here installs WebView2, touches the registry, the user's profile or %LOCALAPPDATA%\\browser.
Outputs go to build/webview2-runtime. Exit code is non-zero if any build or test fails.
"""
import argparse
import os
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / 'build' / 'webview2-runtime'
GXX = str(ROOT / 'tools' / 'w64devkit' / 'bin' / 'g++.exe')
WV2 = '-Ideps/webview2/build/native/include'
STRICT = ['-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-Wno-missing-field-initializers', '-static']
HOST = ['-DBROWSER_WEBVIEW2_TEST_HOOKS', '-std=c++17', '-O1', '-Wall', '-Wextra', '-Wno-unknown-pragmas', '-Wno-cast-function-type', '-Wno-misleading-indentation',
        '-Wno-missing-field-initializers', '-static', '-municode', '-DTVG_STATIC', '-Ideps/thorvg-1.1.3/inc', WV2]
UI_LIBS = ['-lole32', '-luuid', '-lcomctl32', '-lshlwapi', '-lgdi32', '-ld2d1', '-ldwrite', '-lwindowscodecs', '-ldwmapi']
THORVG = ROOT / 'build' / 'lottie-backend' / 'src' / 'libthorvg-1.a'
SCENARIOS = ['recover', 'not-detected', 'loader-missing', 'denied', 'close-during-install', 'relaunch', 'install-running']
failures = []


def run(cmd, label, timeout=600):
    print(f'$ {label}', flush=True)
    try:
        r = subprocess.run([str(c) for c in cmd], cwd=ROOT, timeout=timeout, capture_output=True, text=True, encoding='utf-8', errors='replace')
    except subprocess.TimeoutExpired:
        print(f'!! {label}: timeout'); failures.append(label); return None
    out = (r.stdout or '') + (r.stderr or '')
    if out.strip():
        print(out.rstrip())
    if r.returncode != 0:
        print(f'!! {label}: exit {r.returncode}')
        failures.append(label)
    return r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--bootstrapper', help='real MicrosoftEdgeWebview2Setup.exe to VERIFY (never executed)')
    ap.add_argument('--loader', action='append', help='real WebView2Loader.dll for the read-only probe')
    args = ap.parse_args()
    sys.stdout.reconfigure(errors="replace")
    OUT.mkdir(parents=True, exist_ok=True)
    if not THORVG.exists():
        run([sys.executable, ROOT / 'build-lottie.py'], 'build ThorVG')
    run([GXX, *STRICT, 'tests/webview2-runtime/runtime_test.cpp', '-o', OUT / 'runtime_test.exe'], 'compile runtime_test')
    run([GXX, *STRICT, '-municode', '-mwindows', 'tests/webview2-runtime/fake_setup.cpp', '-o', OUT / 'fake_setup.exe'], 'compile fake_setup')
    run([GXX, *STRICT, '-municode', 'tests/webview2-runtime/install_test.cpp', '-o', OUT / 'install_test.exe'], 'compile install_test')
    run([GXX, *STRICT, '-municode', WV2, '-Wno-unknown-pragmas', 'tests/webview2-runtime/loader_probe.cpp', '-o', OUT / 'loader_probe.exe', '-lole32'], 'compile loader_probe')
    run([GXX, '-std=c++17', '-O1', '-Wno-unknown-pragmas', '-shared', '-static', WV2, 'tests/webview2-runtime/fake_loader.cpp',
         '-o', OUT / 'fake-loader.dll', '-lole32', '-luuid'], 'compile fake WebView2Loader.dll')
    run([GXX, *HOST, 'tests/webview2-runtime/host_test.cpp', 'src/native-lottie.cpp', THORVG, '-o', OUT / 'host_test.exe', *UI_LIBS],
        'compile host_test (main.cpp, unstamped)')
    if failures:
        return finish()
    run([OUT / 'runtime_test.exe'], 'runtime_test')
    work = OUT / 'install-work'
    shutil.rmtree(work, ignore_errors=True)
    run([OUT / 'install_test.exe', work, OUT / 'fake_setup.exe', *([args.bootstrapper] if args.bootstrapper else [])], 'install_test')
    base = OUT / 'host-base'
    if not base.exists():
        base.mkdir()
    if not (base / 'assets').exists():
        subprocess.run(['cmd', '/c', 'mklink', '/J', str(base / 'assets'), str(ROOT / 'assets')], check=True, capture_output=True)
    for scenario in SCENARIOS:
        data = OUT / f'host-data-{scenario}'
        shutil.rmtree(data, ignore_errors=True)
        data.mkdir()
        shutil.copy2(OUT / 'fake-loader.dll', base / 'WebView2Loader.dll')
        run([OUT / 'host_test.exe', base, data, scenario], f'host_test {scenario}', timeout=120)
    loaders = args.loader or [str(ROOT / 'deps/webview2/build/native/x64/WebView2Loader.dll')]
    run([OUT / 'loader_probe.exe', *loaders], 'loader_probe (read-only, real loader)')
    return finish()


def finish():
    if failures:
        print(f'FAILED: {failures}')
        return 1
    print('webview2-runtime: all builds and tests passed')
    return 0


if __name__ == '__main__':
    sys.exit(main())
