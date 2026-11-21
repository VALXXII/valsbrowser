#!/usr/bin/env python3
"""Build and run the unit tests of the chat's browser integration. No WebView2, no agent, no network.

    python -I tests/chat/run_tests.py

  chat_units_test   src/chat-local.h (local sites only), src/chat-prompt.h (what the agent receives, file:line
                    never guessed), src/chat-store.h (site -> folder, history), src/chat-folder.h (project root,
                    the process listening on a port). Temporary folder only, removed at the end.

Compiled with -Wall -Wextra -Werror into build/chat-units-test. The user's real Claude / Codex configuration
files are fingerprinted before and after: any change fails the run. The real run in the browser (fake agent)
is tests/chat/chat_qa.py run.
"""
import hashlib
import os
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / 'build' / 'chat-units-test'
GXX = str(ROOT / 'tools' / 'w64devkit' / 'bin' / 'g++.exe')
# the browser's own warning set (tools/release/build.py WARNINGS), as errors
FLAGS = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Wno-unknown-pragmas', '-Wno-cast-function-type', '-Wno-misleading-indentation',
         '-Wno-missing-field-initializers', '-Werror', '-static', '-municode', '-Isrc']
LIBS = ['-lole32', '-luuid', '-lshell32', '-lws2_32']


def configs():
    home = pathlib.Path(os.environ.get('USERPROFILE', str(pathlib.Path.home())))
    appdata = pathlib.Path(os.environ.get('APPDATA', str(home / 'AppData' / 'Roaming')))
    return [appdata / 'Claude' / 'claude_desktop_config.json', home / '.codex' / 'config.toml', home / '.claude' / 'settings.json']


def fingerprint():
    return {str(p): (hashlib.sha256(p.read_bytes()).hexdigest() if p.is_file() else 'absent') for p in configs()}


def main() -> int:
    OUT.mkdir(parents=True, exist_ok=True)
    before = fingerprint()
    env = dict(os.environ, BROWSER_ANNOTATIONS_TEST='1')
    exe = OUT / 'chat_units_test.exe'
    build = subprocess.run([GXX, *FLAGS, 'tests/chat/chat_units_test.cpp', '-o', str(exe), *LIBS], cwd=ROOT, capture_output=True, text=True, encoding='utf-8', errors='replace')
    if build.returncode != 0 or build.stderr.strip():
        print(build.stdout + build.stderr)
        print('FAIL build (errors or warnings)')
        return 1
    run = subprocess.run([str(exe)], cwd=ROOT, capture_output=True, text=True, encoding='utf-8', errors='replace', env=env)
    print((run.stdout + run.stderr).rstrip())
    ok = run.returncode == 0
    contracts = subprocess.run([sys.executable, '-I', str(ROOT / 'tests' / 'chat' / 'host_contract_test.py')], cwd=ROOT, capture_output=True, text=True, encoding='utf-8', errors='replace')
    print((contracts.stdout + contracts.stderr).rstrip())
    ok = ok and contracts.returncode == 0
    if fingerprint() != before:
        print('FAIL a real Claude / Codex configuration file changed during the tests')
        ok = False
    print('OK' if ok else 'FAILED')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
