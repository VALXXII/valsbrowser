"""An installed browser's windows carry one taskbar identity and relaunch through the stable launcher.

    python -I tests/e2e/taskbar_identity_e2e.py --app <built app dir, preview build> --out <scratch dir>

Bug (2026-11-16): without an AppUserModelID the taskbar groups by executable path. The windows of
<root>\\versions\\<v>\\browser.exe showed apart from the pinned launcher, and pinning a window pinned the version
folder, which the next update replaces: the pin went stale after every update.

Isolated installed layout only (LOCALAPPDATA of the child points into <out>), local page, WM_CLOSE to our own
process. The window's properties are read by tests/e2e/taskbar_identity_probe.cpp (read-only). The person's own
Start Menu shortcut is only checked to be untouched. Use a --preview build (no update download).

Checks: installed 0.3.1 and 0.3.2 -> the same identity, relaunch = "<root>\\browser.exe", name and icon;
a portable copy -> no identity; the real Start Menu shortcut unchanged.
"""
import argparse, ctypes, ctypes.wintypes as W, http.server, os, shutil, subprocess, sys, threading, time
from pathlib import Path

ap = argparse.ArgumentParser(); ap.add_argument('--app', required=True); ap.add_argument('--out', required=True); a = ap.parse_args()
src, out = Path(a.app).resolve(), Path(a.out).resolve()
repo = Path(__file__).resolve().parents[2]
shutil.rmtree(out, ignore_errors=True); out.mkdir(parents=True)
probe = out / 'taskbar_identity_probe.exe'
subprocess.run([str(repo / 'tools/w64devkit/bin/g++.exe'), '-std=c++17', '-static', '-Wall', '-Wextra', '-Werror', '-municode',
                str(repo / 'tests/e2e/taskbar_identity_probe.cpp'), '-o', str(probe), '-lole32', '-luuid', '-lshell32', '-lshlwapi', '-lpropsys'], check=True)


class H(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        b = b'<!doctype html><title>t</title>t'
        self.send_response(200); self.send_header('Content-Type', 'text/html'); self.send_header('Content-Length', str(len(b))); self.end_headers(); self.wfile.write(b)

    def log_message(self, *x):
        pass


srv = http.server.ThreadingHTTPServer(('127.0.0.1', 0), H); base = f'http://127.0.0.1:{srv.server_port}'
threading.Thread(target=srv.serve_forever, daemon=True).start()
local = out / 'localappdata'; root = local / 'browser'
for v in ('0.3.1', '0.3.2'):
    shutil.copytree(src, root / 'versions' / v)
portable = out / 'portable'; shutil.copytree(src, portable)
u32 = ctypes.windll.user32; u32.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
start_menu = Path(os.environ['APPDATA']) / 'Microsoft/Windows/Start Menu/Programs/browser.lnk'
before = start_menu.stat().st_mtime_ns if start_menu.exists() else None


def run(exe, data):
    data.mkdir(parents=True, exist_ok=True)
    shutil.rmtree(data / 'sessions', ignore_errors=True)
    (data / 'session').write_text(f'browser-session-2\n0\n1\n"{base}/t"\n0\n', encoding='utf-8', newline='\n')
    (data / 'settings').write_text('browser-settings 1\nrestore-tabs=1\nconfirm-close-tabs=0\n', encoding='utf-8', newline='\n')
    proc = subprocess.Popen([str(exe)], cwd=exe.parent, env=dict(os.environ, LOCALAPPDATA=str(local)))
    props = {}
    for _ in range(120):
        r = subprocess.run([str(probe), str(proc.pid)], capture_output=True, text=True, encoding='utf-8')
        if r.returncode == 0:
            props = dict(l.split('=', 1) for l in r.stdout.splitlines() if '=' in l); break
        time.sleep(0.25)
    time.sleep(1.5)

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
    time.sleep(2)
    return props


fails = 0


def check(ok, what):
    global fails
    print(('ok   ' if ok else 'FAIL ') + what, flush=True); fails += 0 if ok else 1


try:
    launcher = f'"{root}\\browser.exe"'
    one = run(root / 'versions/0.3.1/browser.exe', root / 'data')
    two = run(root / 'versions/0.3.2/browser.exe', root / 'data')
    check(one.get('aumid') == 'ValXXII.browser', f'installed 0.3.1: the window declares the identity ({one})')
    check(two.get('aumid') == one.get('aumid') and two.get('aumid'), f'installed 0.3.2 (after an update): the same identity ({two.get("aumid")})')
    check(one.get('relaunch') == launcher and two.get('relaunch') == launcher, f'pinning a window pins the stable launcher {launcher}, never versions\\<v> ({two.get("relaunch")})')
    check(two.get('name') == 'browser' and two.get('icon') == f'{root}\\browser.ico,0', 'with the name « browser » and the launcher icon')
    p = run(portable / 'browser.exe', portable)
    check(not p.get('aumid') and not p.get('relaunch'), f'portable copy: no identity, Windows groups it by its own path ({p})')
    after = start_menu.stat().st_mtime_ns if start_menu.exists() else None
    check(after == before, 'the real Start Menu shortcut (another install) is untouched')
finally:
    srv.shutdown()
print(f'{fails} failure(s)'); sys.exit(1 if fails else 0)
