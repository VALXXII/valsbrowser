"""After an update, the ad blocker is kept: never removed and reinstalled under restored tabs.

    python -I tests/e2e/adblock_update_e2e.py --app <built app dir, preview build> --out <scratch dir>

Bug (2026-11-16): uBlock Origin Lite was installed from versions\\<v>\\assets\\extensions\\ublock-lite. The
unpacked extension's id follows its folder, so the first launch after every update found « a previous install »,
removed it and installed again while the session's tabs were loading: those navigations were aborted
(WebErrorStatus=9, « La connexion a été interrompue ») although the same page loads fine afterwards.

This drives an isolated installed layout only: LOCALAPPDATA of the child process points into <out>, so the app
resolves <out>\\localappdata\\browser\\{versions,data} and never the person's own install. The same payload is
placed as versions\\0.3.1 and versions\\0.3.2 (what an update does), each launched once over the same data. Pages
come from a local HTTP server on 127.0.0.1; windows are closed with WM_CLOSE posted to our own process only.
Use a --preview build (tools/release/build.py --preview): updates are compiled out, nothing is downloaded.

Checks:
  first launch    the ad blocker is installed (fresh profile)
  second launch   from the other version folder: no « removing a previous install », no new install, the ad blocker
                  is on, and the restored tab loads without any NAVIGATION FAILED
"""
import argparse, ctypes, ctypes.wintypes as W, http.server, os, shutil, subprocess, sys, threading, time
from pathlib import Path
os.environ.setdefault('BROWSER_NO_ONBOARDING', '1')   # fresh installed layout: not the first-run window (src/onboarding-flow.h)

ap = argparse.ArgumentParser(); ap.add_argument('--app', required=True); ap.add_argument('--out', required=True); a = ap.parse_args()
src, out = Path(a.app).resolve(), Path(a.out).resolve()


class H(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        time.sleep(0.4)   # a page still loading while the extension sync runs
        b = b'<!doctype html><title>restored</title><p>restored tab</p>'
        self.send_response(200); self.send_header('Content-Type', 'text/html'); self.send_header('Content-Length', str(len(b)))
        self.end_headers(); self.wfile.write(b)

    def log_message(self, *x):
        pass


shutil.rmtree(out, ignore_errors=True)
local = out / 'localappdata'; root = local / 'browser'; data = root / 'data'
for v in ('0.3.1', '0.3.2'):
    shutil.copytree(src, root / 'versions' / v)
data.mkdir(parents=True)
srv = http.server.ThreadingHTTPServer(('127.0.0.1', 0), H); base = f'http://127.0.0.1:{srv.server_port}'
threading.Thread(target=srv.serve_forever, daemon=True).start()
(data / 'settings').write_text('browser-settings 1\nrestore-tabs=1\nconfirm-close-tabs=0\nad-block=1\n', encoding='utf-8', newline='\n')
log = data / 'browser.log'
u32 = ctypes.windll.user32
u32.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]


def text():
    return log.read_text(encoding='utf-8', errors='replace') if log.exists() else ''


def launch(version, until, timeout=90):
    shutil.rmtree(data / 'sessions', ignore_errors=True)   # one window: the previous run's window is not reopened
    (data / 'session').write_text(f'browser-session-2\n0\n2\n"{base}/a"\n"{base}/b"\n0\n', encoding='utf-8', newline='\n')
    start = len(text())
    env = dict(os.environ, LOCALAPPDATA=str(local))
    proc = subprocess.Popen([str(root / 'versions' / version / 'browser.exe')], cwd=root / 'versions' / version, env=env)
    end = time.monotonic() + timeout
    while time.monotonic() < end and not any(u in text()[start:] for u in until):
        time.sleep(0.25)
    time.sleep(3)   # let the restored tabs finish and any late sync step log
    run = text()[start:]

    @ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    def close_own(h, _):
        pid = W.DWORD(); u32.GetWindowThreadProcessId(h, ctypes.byref(pid))
        if pid.value == proc.pid and u32.IsWindowVisible(h):
            u32.PostMessageW(h, 0x0010, 0, 0)
        return True
    u32.EnumWindows(close_own, 0)
    try:
        proc.wait(timeout=30)
    except subprocess.TimeoutExpired:
        proc.kill(); proc.wait(timeout=10)
    time.sleep(2)   # WebView2 child processes release the profile
    return run


fails = 0


def check(ok, what):
    global fails
    print(('ok   ' if ok else 'FAIL ') + what, flush=True)
    fails += 0 if ok else 1


try:
    first = launch('0.3.1', ['SETTINGS adblock installed', 'SETTINGS adblock install failed', 'adblock: extension missing'])
    check('SETTINGS adblock installed' in first, 'first launch (0.3.1): the ad blocker is installed')
    second = launch('0.3.2', ['SETTINGS adblock on', 'SETTINGS adblock installed', 'SETTINGS adblock install failed'])
    adblock = [l for l in second.splitlines() if 'adblock' in l]
    check('removing a previous install' not in second, f'after the update (0.3.2): the ad blocker is not removed ({adblock})')
    check('SETTINGS adblock installed' not in second and 'SETTINGS adblock on' in second, 'after the update: kept and on, no reinstall')
    failed = [l for l in second.splitlines() if 'NAVIGATION FAILED' in l]
    check(not failed and second.count('NAVIGATION OK') >= 1, f'after the update: the restored tab loads, no aborted navigation ({failed})')
finally:
    srv.shutdown()
(out / 'log-tail.txt').write_text(text()[-20000:], encoding='utf-8')
print(f'{fails} failure(s)')
sys.exit(1 if fails else 0)
