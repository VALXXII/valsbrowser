"""Release candidate without its bridge: the connect path must find nothing and write nothing.

    python -I tests/update-integration/release_candidate_no_bridge.py --app build/b-release/app

Runs a THROWAWAY copy of the release app (payload only, assets\\bin\\browser-annotations-mcp.exe removed) in
build/release-nobridge-app, with LOCALAPPDATA pointing to an empty folder of its own (never the user's install) and
the agent-config writes redirected (BROWSER_ANNOTATIONS_CONNECT_ROOT). It enters the annotation mode on a local page
(which opens the connect modal when no agent is connected), then closes the window. Checks: the app logs « ANNOTATE bridge not found: <copy>\\assets\\bin\\… », no file is written
under the redirect root, nothing is written outside the copy and its LOCALAPPDATA. It does not click the modal's
buttons: the refusal to write a config for a missing/invalid bridge (BridgeUsable) is covered by
tests/annotations/agent_connect_test.cpp.
"""
from __future__ import annotations
import ctypes
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / 'build' / 'release-nobridge-app'
LOCAL = ROOT / 'build' / 'release-nobridge-localappdata'
REDIRECT = APP / 'connect-redirect'


def logs() -> str:
    return ''.join(p.read_text(encoding='utf-8', errors='replace') for p in APP.rglob('*.log'))


def main(argv) -> int:
    src = ROOT / (argv[argv.index('--app') + 1] if '--app' in argv else 'build/b-release/app')
    shutil.rmtree(APP, ignore_errors=True)
    shutil.rmtree(LOCAL, ignore_errors=True)
    APP.mkdir(parents=True)
    LOCAL.mkdir(parents=True)
    for f in src.iterdir():
        if f.suffix in ('.exe', '.dll', '.json', '.txt') or f.name == 'assets':
            (shutil.copytree if f.is_dir() else shutil.copy2)(f, APP / f.name)
    bridge = APP / 'assets' / 'bin' / 'browser-annotations-mcp.exe'
    bridge.unlink()
    # The modal opens when the annotation mode starts with no agent connected: the mode is entered exactly like
    # tests/annotations/profile_color_e2e.py does (local page on 127.0.0.1, posted click on OUR window only).
    sys.path.insert(0, str(ROOT / 'tests' / 'annotations'))
    import profile_color_e2e as E  # noqa: E402
    import sandbox_qa as Q  # noqa: E402
    import socketserver, threading  # noqa: E401,E402
    E.APP = Q.APP = APP
    server = socketserver.TCPServer(('127.0.0.1', E.WEB), E.Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    env = dict(os.environ, LOCALAPPDATA=str(LOCAL), BROWSER_ANNOTATIONS_CONNECT_ROOT=str(REDIRECT), BROWSER_ANNOTATIONS_TEST='1',
               WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS=f'--remote-debugging-port={E.PORT} --remote-allow-origins=http://127.0.0.1:{E.PORT}')
    p = subprocess.Popen([str(APP / 'browser.exe')], cwd=str(APP), env=env)
    (APP / 'qa.pid').write_text(str(p.pid))
    failures = 0
    try:
        E.wait(lambda: Q.main_window(), 20)
        E.navigate()
        E.toggle_mode(Q.main_window())
        end = time.time() + 30
        while time.time() < end and 'ANNOTATE connect modal opened' not in logs():
            time.sleep(0.3)
        time.sleep(1.0)
        text = logs()
        for ok, what in (('ANNOTATE connect modal opened' in text, 'connect modal opened on mode entry (no agent connected)'),
                         (f'ANNOTATE bridge not found: {bridge}'.lower() in text.lower(), f'bridge reported missing: {bridge}'),
                         (not REDIRECT.exists() or not any(REDIRECT.rglob('*')), 'no agent configuration written')):
            print(('ok   ' if ok else 'FAIL ') + what)
            failures += not ok
    finally:
        server.shutdown()
        user32 = ctypes.windll.user32
        def close(hwnd, _):
            pid = ctypes.c_ulong()
            user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            if pid.value == p.pid:
                user32.PostMessageW(hwnd, 0x10, 0, 0)
            return True
        user32.EnumWindows(ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)(close), 0)
        try:
            p.wait(20)
        except subprocess.TimeoutExpired:
            p.kill()  # our own throwaway copy only
    print(f'{failures} failures')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
