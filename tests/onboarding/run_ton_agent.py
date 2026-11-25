#!/usr/bin/env python3
"""Build and run the « Ton agent » tests of worker B (headless: no window is ever shown, no real installer).

    python -I tests/onboarding/run_ton_agent.py [--toolchain <checkout with tools/ and deps/>] [--only ton|tutorial]

  ton_agent_test            the screen's decisions (src/annotation-onboarding-agent.h), the tutorial's flow around A6, then
                            the production window on the real hub over A's fake profiles (fake CLI, fake official installer
                            sandbox/agents/fake-install.ps1, the user PATH on a test key): Claude Code present / absent /
                            Claude Desktop only, Codex signed in / out, install success / failure, retry, « Installer
                            moi-même », « Plus tard », « Retour », « Ouvrir le chat », alone; every artboard rendered at
                            96 and 120 DPI into build/ton-agent-render (compare: tests/onboarding/ton_agent_compare.py).
  annotation_onboarding_test  the tutorial's other steps (A1 – A5, A7) as before, with A6 « Ton agent ».

An Orca worktree has no tools/ nor deps/: --toolchain points at a checkout that has them (only its g++, ThorVG and
WebView2 headers are used; nothing is written there). Work files: build/ton-agent-test. The real user PATH
(HKCU\\Environment) is read before and after: any change fails the run.
"""
import argparse
import os
import shutil
import subprocess
import sys
import time
import winreg
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'build' / 'ton-agent-test'
RENDERS = ROOT / 'build' / 'ton-agent-render'
TUTORIAL_RENDERS = ROOT / 'build' / 'annotation-onboarding-render'
LOOSE = ['-O2', '-Wall', '-Wextra', '-Werror', '-Wno-unknown-pragmas', '-Wno-cast-function-type', '-Wno-misleading-indentation',
         '-Wno-missing-field-initializers', '-municode', '-DTVG_STATIC']
UI_LIBS = ['-lole32', '-luuid', '-lcomctl32', '-lshlwapi', '-lgdi32', '-ld2d1', '-ldwrite', '-lwindowscodecs', '-ldwmapi', '-loleaut32', '-lshell32']
HUB = ['src/agent-install.cpp', 'src/agent-runtime.cpp', 'src/acp-client.cpp']
HUB_LIBS = ['-luserenv', '-lwinhttp', '-lbcrypt']


def real_path():
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, 'Environment') as k:
        try:
            return winreg.QueryValueEx(k, 'Path')[0]
        except FileNotFoundError:
            return None


def run(cmd, label, env=None):
    print(f'$ {label}', flush=True)
    t = time.time()
    r = subprocess.run([str(c) for c in cmd], cwd=ROOT, env=env)
    print(f'  ({time.time() - t:.1f} s, exit {r.returncode})', flush=True)
    return r.returncode == 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--toolchain', default=str(ROOT))
    ap.add_argument('--only', choices=('ton', 'tutorial'))
    a = ap.parse_args()
    tc = Path(a.toolchain)
    gxx = tc / 'tools/w64devkit/bin/g++.exe'
    inc = [f'-I{tc / "deps/thorvg-1.1.3/inc"}', f'-I{tc / "deps/webview2/build/native/include"}']
    thorvg = tc / 'build/lottie-backend/src/libthorvg-1.a'
    for p in (gxx, thorvg):
        if not p.exists():
            sys.exit(f'missing {p}: give --toolchain <a checkout with tools/ and deps/>')
    OUT.mkdir(parents=True, exist_ok=True)
    before = real_path()
    ok = True
    env = dict(os.environ, BROWSER_ANNOTATIONS_TEST='1')
    if a.only in (None, 'ton'):
        ok &= run([gxx, '-std=c++17', '-O2', '-static', '-municode', 'tests/acp/fake_cli.cpp', '-o', OUT / 'fake-cli.exe'], 'compile fake-cli.exe')
        ok &= run([gxx, '-std=c++17', '-static', *LOOSE, *inc, '-Isrc', 'tests/onboarding/ton_agent_test.cpp', 'src/native-lottie.cpp', *HUB, str(thorvg),
                   *UI_LIBS, *HUB_LIBS, '-o', OUT / 'ton_agent_test.exe'], 'compile ton_agent_test')
        work = OUT / 'work'
        if work.exists():
            shutil.rmtree(work)
        if RENDERS.exists():
            shutil.rmtree(RENDERS)
        RENDERS.mkdir(parents=True)
        if ok:
            ok &= run([OUT / 'ton_agent_test.exe', ROOT, RENDERS, OUT / 'fake-cli.exe', work], 'ton_agent_test', env)
    if a.only in (None, 'tutorial'):
        ok &= run([gxx, '-std=c++17', '-static', *LOOSE, *inc, '-Isrc', 'tests/onboarding/annotation_onboarding_test.cpp', 'src/native-lottie.cpp', *HUB, str(thorvg),
                   *UI_LIBS, *HUB_LIBS, '-o', OUT / 'annotation_onboarding_test.exe'], 'compile annotation_onboarding_test')
        TUTORIAL_RENDERS.mkdir(parents=True, exist_ok=True)
        if ok:
            ok &= run([OUT / 'annotation_onboarding_test.exe', ROOT, TUTORIAL_RENDERS], 'annotation_onboarding_test', env)
    if real_path() != before:
        print('!! the real user PATH (HKCU\\Environment) changed'); ok = False
    print('ALL OK' if ok else 'FAILED')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
