#!/usr/bin/env python3
"""Fullscreen content E2E (design/fullscreen-content-fix-report.md): F11 (browser) and a page's requestFullscreen
must give the page the whole monitor; no tab strip, toolbar, bookmarks or annotation chrome on screen, in hit
testing, in keyboard focus or in UI Automation; leaving restores the window (windowed bounds or maximized) and the
chrome.

    python -I tests/e2e/fullscreen_e2e.py [--app build/fullscreen-fix] [--split]   # report in build/fullscreen-e2e/report.txt
    --split: the two fixture pages restored as a side-by-side group (page fullscreen alone, F11 keeps the split).

Drives ONLY its own throwaway copy of a dev build (build/fullscreen-e2e-app: payload copied, portable data next to the
exe). Pages come from a server of this script on 127.0.0.1. The WebView2 remote debugging port (loopback) is enabled
for this copy only, through the process environment: it reads page metrics and, for the page fullscreen, clicks the
fixture's button with a trusted CDP mouse event (a real user activation for the engine; no policy is relaxed).
Browser commands (F11, Échap's command, Ctrl+1/2, Agrandir) are the app's own shortcut messages posted to OUR window.
Pixels: PrintWindow of our window only. No SendInput, no foreground change, no clipboard, no screen grab.
"""
from __future__ import annotations
import argparse
import ctypes
import ctypes.wintypes as W
import http.server
import json
import pathlib
import shutil
import socketserver
import subprocess
import sys
import threading
import time
import urllib.request

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / 'tests' / 'annotations'))
import sandbox_qa as Q   # noqa: E402  (CDP WebSocket client, PrintWindow)

APP = ROOT / 'build' / 'fullscreen-e2e-app'
OUT = ROOT / 'build' / 'fullscreen-e2e'
PORT, WEB = 9347, 5203
BASE = f'http://127.0.0.1:{WEB}'
user32 = ctypes.windll.user32
Shortcut, FullscreenCommand, ExitFullscreenCommand, SelectTabBase, MaximizeCommand = 0x8001, 3052, 3055, 3010, ord('M')
BLUE, GREEN = (0x33, 0x66, 0xEE), (0x22, 0xAA, 0x55)
lines: list[str] = []
failures = 0


def log(text):
    print(text, flush=True); lines.append(text)


def check(ok, what):
    global failures
    log(('ok   ' if ok else 'FAIL ') + what)
    if not ok:
        failures += 1


def page(color, title):
    return (f'<!doctype html><html lang="fr"><head><meta charset="utf-8"><title>{title}</title><style>html,body{{margin:0;height:100%;'
            f'background:#{color}}}#fs{{position:fixed;left:40px;top:40px;width:200px;height:60px;font:16px system-ui}}</style></head>'
            '<body><button id="fs" onclick="document.documentElement.requestFullscreen()">Plein écran</button></body></html>').encode('utf-8')


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        body = page('3366ee', 'Bleu') if self.path.startswith('/blue') else page('22aa55', 'Vert')
        self.send_response(200); self.send_header('Content-Type', 'text/html; charset=utf-8'); self.end_headers(); self.wfile.write(body)

    def log_message(self, *a):
        pass


def targets():
    return json.loads(urllib.request.urlopen(f'http://127.0.0.1:{PORT}/json', timeout=5).read())


def ws_for(prefix):
    for t in targets():
        if t.get('type') == 'page' and t.get('url', '').startswith(prefix):
            return Q.WS(t['webSocketDebuggerUrl'])
    return None


def evaluate(prefix, js):
    ws = ws_for(prefix)
    if not ws:
        return None
    r = ws.call('Runtime.evaluate', {'expression': js, 'returnByValue': True, 'awaitPromise': True})
    return r.get('result', {}).get('value')


def render_widget(h):
    found = []
    @ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    def cb(c, _):
        name = ctypes.create_unicode_buffer(64); user32.GetClassNameW(c, name, 64)
        if name.value == 'Chrome_RenderWidgetHostHWND' and user32.IsWindowVisible(c):
            found.append(c)
        return True
    user32.EnumChildWindows(h, cb, 0)
    return found[0] if found else None


def click_fs(h):
    """A click on the fixture's button (CSS 120,70), posted to OUR page's render widget: a real user activation for
    the engine (a CDP-dispatched click is refused by WebView2 with « not granted »)."""
    rw = render_widget(h)
    if not rw:
        return False
    k = user32.GetDpiForWindow(h) / 96.0; lp = (int(70 * k) << 16) | int(120 * k)
    user32.PostMessageW(rw, 0x200, 0, lp); time.sleep(0.05); user32.PostMessageW(rw, 0x201, 1, lp); time.sleep(0.05); user32.PostMessageW(rw, 0x202, 0, lp)
    return True


def main_window(pid):
    found = []
    @ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    def cb(h, _):
        p = W.DWORD(); user32.GetWindowThreadProcessId(h, ctypes.byref(p))
        name = ctypes.create_unicode_buffer(64); user32.GetClassNameW(h, name, 64)
        if p.value == pid and name.value == 'LisiereBrowser' and user32.IsWindowVisible(h):
            found.append(h)
        return True
    user32.EnumWindows(cb, 0)
    return found[0] if found else None


def rect(h):
    r = W.RECT(); user32.GetWindowRect(h, ctypes.byref(r)); return (r.left, r.top, r.right, r.bottom)


def client_screen(h):
    r = W.RECT(); user32.GetClientRect(h, ctypes.byref(r))
    p = W.POINT(0, 0); user32.ClientToScreen(h, ctypes.byref(p))
    return (p.x, p.y, p.x + r.right, p.y + r.bottom)


def monitor(h):
    class MI(ctypes.Structure):
        _fields_ = [('cbSize', W.DWORD), ('rcMonitor', W.RECT), ('rcWork', W.RECT), ('dwFlags', W.DWORD)]
    mi = MI(); mi.cbSize = ctypes.sizeof(MI)
    user32.GetMonitorInfoW(user32.MonitorFromWindow(h, 2), ctypes.byref(mi))
    m = mi.rcMonitor; return (m.left, m.top, m.right, m.bottom)


def page_hosts(h):
    """Visible WebView host windows (Chrome_WidgetWin_*) that are direct children of our window."""
    out = []
    @ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    def cb(c, _):
        name = ctypes.create_unicode_buffer(64); user32.GetClassNameW(c, name, 64)
        if user32.GetParent(c) == h and user32.IsWindowVisible(c) and name.value.startswith('Chrome_WidgetWin'):
            out.append(rect(c))
        return True
    user32.EnumChildWindows(h, cb, 0)
    return out


def visible_edit(h):
    e = user32.FindWindowExW(h, None, 'Edit', None)
    return bool(e and user32.IsWindowVisible(e))


UIA_PS = r'''
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
$AE = [System.Windows.Automation.AutomationElement]
$c = New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, [int]__PID__)
$ids = @()
foreach ($w in $AE::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children, $c)) {
  if ($w.Current.ClassName -ne 'LisiereBrowser') { continue }
  foreach ($e in $w.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition)) { $ids += $e.Current.AutomationId }
}
$ids -join ','
'''


def uia_children(pid):
    r = subprocess.run(['powershell', '-NoProfile', '-Command', UIA_PS.replace('__PID__', str(pid))], capture_output=True, text=True, timeout=60)
    return [x for x in r.stdout.strip().split(',') if x]


def pixel(h, x, y):
    """One pixel of OUR window's client area, rendered by PrintWindow (PW_CLIENTONLY | PW_RENDERFULLCONTENT)."""
    g = ctypes.windll.gdi32
    c = client_screen(h); w, hh = c[2] - c[0], c[3] - c[1]
    if w <= 0 or hh <= 0 or user32.IsIconic(h) or not (0 <= x < w and 0 <= y < hh):
        return None
    class BIH(ctypes.Structure):
        _fields_ = [('biSize', W.DWORD), ('biWidth', W.LONG), ('biHeight', W.LONG), ('biPlanes', W.WORD), ('biBitCount', W.WORD),
                    ('biCompression', W.DWORD), ('biSizeImage', W.DWORD), ('a', W.LONG), ('b', W.LONG), ('c', W.DWORD), ('d', W.DWORD)]
    hdc = user32.GetDC(h); mdc = g.CreateCompatibleDC(hdc); user32.ReleaseDC(h, hdc)
    bi = BIH(ctypes.sizeof(BIH), w, -hh, 1, 32, 0, 0, 0, 0, 0, 0); bits = ctypes.c_void_p()
    bmp = g.CreateDIBSection(mdc, ctypes.byref(bi), 0, ctypes.byref(bits), None, 0); old = g.SelectObject(mdc, bmp)
    user32.PrintWindow(h, mdc, 3)
    off = (y * w + x) * 4; b, gr, r = ctypes.string_at(bits.value + off, 3)
    g.SelectObject(mdc, old); g.DeleteObject(bmp); g.DeleteDC(mdc)
    return (r, gr, b)


def near(a, b, tol=12):
    return a is not None and all(abs(int(x) - int(y)) <= tol for x, y in zip(a, b))


def post(h, cmd):
    user32.PostMessageW(h, Shortcut, cmd, 0)


def wait(pred, seconds=6.0):
    end = time.time() + seconds
    while time.time() < end:
        v = pred()
        if v:
            return v
        time.sleep(0.15)
    return pred()


def metrics(prefix):
    return evaluate(prefix, '({w:innerWidth,h:innerHeight,dpr:devicePixelRatio,fs:!!document.fullscreenElement,focus:document.hasFocus()})')


CHROME_IDS = {'Toolbar', 'Tabs', 'Tab', 'CloseTab', 'NewTab', 'ProfileButton', 'TabList', 'Minimize', 'Maximize', 'Close', 'Back', 'Forward', 'Reload',
              'SiteSecurity', 'Address', 'CopyLink', 'Bookmark.Star', 'AnnotateButton', 'SiteAudio', 'History', 'Downloads', 'Settings'}


def check_full(h, pid, prefix, color, what):
    m = monitor(h)
    check(rect(h) == m, f'{what}: the window covers its monitor {rect(h)} == {m}')
    cs = client_screen(h)
    hosts = page_hosts(h)
    check(hosts == [cs], f'{what}: the page host covers the whole client area {hosts} vs {cs}')
    mt = metrics(prefix)
    if mt:
        ok = abs(mt['w'] * mt['dpr'] - (cs[2] - cs[0])) <= 2 and abs(mt['h'] * mt['dpr'] - (cs[3] - cs[1])) <= 2
        check(ok, f'{what}: page viewport {mt["w"]}x{mt["h"]} CSS px x {mt["dpr"]} = client {cs[2] - cs[0]}x{cs[3] - cs[1]} px')
    else:
        check(False, f'{what}: page metrics readable')
    top, chrome_row = pixel(h, (cs[2] - cs[0]) // 2, 6), pixel(h, (cs[2] - cs[0]) // 2, 60)
    check(near(top, color) and near(chrome_row, color), f'{what}: own-window render: the page colour at the top rows (y 6: {top}, y 60: {chrome_row})')
    ids = set(uia_children(pid))
    check(not (ids & CHROME_IDS), f'{what}: no chrome control exposed to UI Automation ({sorted(ids & CHROME_IDS)})')
    check(not visible_edit(h), f'{what}: no native address field shown')


def check_restored(h, pid, prefix, before, zoomed, what):
    check(rect(h) == before, f'{what}: window bounds restored {rect(h)} == {before}')
    check(bool(user32.IsZoomed(h)) == zoomed, f'{what}: maximized state restored ({zoomed})')
    cs = client_screen(h); hosts = page_hosts(h)
    k = user32.GetDpiForWindow(h) / 96.0
    check(len(hosts) == 1 and hosts[0][1] - cs[1] >= int(70 * k), f'{what}: the page sits under the chrome again ({hosts}, client top {cs[1]})')
    ids = set(uia_children(pid))
    check({'Toolbar', 'Tabs', 'Settings'} <= ids, f'{what}: chrome back in UI Automation')
    mt = metrics(prefix)
    check(bool(mt) and not mt['fs'], f'{what}: no element left in fullscreen')


def grips(h):
    n = []
    @ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    def cb(c, _):
        name = ctypes.create_unicode_buffer(64); user32.GetClassNameW(c, name, 64)
        if name.value == 'BrowserSplitGrip' and user32.IsWindowVisible(c):
            n.append(c)
        return True
    user32.EnumChildWindows(h, cb, 0)
    return len(n)


def widget_at_left(h):
    """The render widget of the left panel (the blue page in the split fixture)."""
    found = []
    @ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    def cb(c, _):
        name = ctypes.create_unicode_buffer(64); user32.GetClassNameW(c, name, 64)
        if name.value == 'Chrome_RenderWidgetHostHWND' and user32.IsWindowVisible(c):
            found.append((rect(c)[0], c))
        return True
    user32.EnumChildWindows(h, cb, 0)
    return min(found)[1] if found else None


def click_fs_left(h):
    rw = widget_at_left(h)
    if not rw:
        return False
    k = user32.GetDpiForWindow(h) / 96.0; lp = (int(70 * k) << 16) | int(120 * k)
    user32.PostMessageW(rw, 0x200, 0, lp); time.sleep(0.05); user32.PostMessageW(rw, 0x201, 1, lp); time.sleep(0.05); user32.PostMessageW(rw, 0x202, 0, lp)
    return True


def split_alone(h, pid, blue, what):
    m = monitor(h); cs = client_screen(h)
    check(rect(h) == m, f'{what}: the window covers its monitor')
    hosts = page_hosts(h)
    check(hosts == [cs], f'{what}: only the fullscreen page is shown, over the whole client area {hosts} vs {cs}')
    mt = metrics(blue)
    check(bool(mt) and mt['fs'] and abs(mt['w'] * mt['dpr'] - (cs[2] - cs[0])) <= 2 and abs(mt['h'] * mt['dpr'] - (cs[3] - cs[1])) <= 2,
          f'{what}: the page in fullscreen sees the whole monitor ({mt and (mt["w"], mt["h"], mt["dpr"])})')
    check(grips(h) == 0, f'{what}: no split grip left over the page')
    right = pixel(h, (cs[2] - cs[0]) * 3 // 4, (cs[3] - cs[1]) // 2)
    check(near(right, BLUE), f'{what}: own-window render: the right half shows the fullscreen page, not the other panel ({right})')
    check(not (set(uia_children(pid)) & CHROME_IDS), f'{what}: no chrome control exposed to UI Automation')


def split_steps(h, pid, blue, green):
    check(bool(wait(lambda: metrics(green), 15)), 'split: the green panel is loaded too')
    time.sleep(1.0)
    if user32.IsZoomed(h):
        post(h, MaximizeCommand); time.sleep(1)
    windowed = rect(h); split0 = page_hosts(h); g0 = grips(h)
    check(len(split0) == 2 and g0 >= 1, f'split setup: two panels side by side {split0}, {g0} grip(s)')
    # Page fullscreen from the left (active) panel, then Échap.
    click_fs_left(h)
    check(bool(wait(lambda: (metrics(blue) or {}).get('fs'), 5)), 'split: requestFullscreen accepted on a user click in the left panel')
    time.sleep(1.5)
    split_alone(h, pid, blue, 'split, page fullscreen')
    post(h, ExitFullscreenCommand); time.sleep(1.5)
    check(rect(h) == windowed and page_hosts(h) == split0 and grips(h) == g0 and not (metrics(blue) or {}).get('fs', True),
          f'split, Échap: the exact split comes back {page_hosts(h)} == {split0}, grips {grips(h)}, window {rect(h)}')
    # F11 keeps the split; page fullscreen inside it; leaving the page returns to the F11 split; F11 off: windowed split.
    post(h, FullscreenCommand); time.sleep(1.5)
    f11 = page_hosts(h)
    check(rect(h) == monitor(h) and len(f11) == 2 and grips(h) >= 1, f'split, F11: the split stays, over the whole monitor {f11}')
    click_fs_left(h); wait(lambda: (metrics(blue) or {}).get('fs'), 5); time.sleep(1.5)
    split_alone(h, pid, blue, 'split, F11 + page fullscreen')
    post(h, ExitFullscreenCommand); time.sleep(1.5)
    check(rect(h) == monitor(h) and page_hosts(h) == f11 and grips(h) >= 1, f'split, F11 + page off: back to the F11 split {page_hosts(h)} == {f11}')
    post(h, FullscreenCommand); time.sleep(1.5)
    check(rect(h) == windowed and page_hosts(h) == split0 and grips(h) == g0 and {'Toolbar', 'Tabs', 'Settings'} <= set(uia_children(pid)),
          f'split, F11 off: the windowed split and the chrome come back {page_hosts(h)} == {split0}')


def run(app_src, split=False):
    shutil.rmtree(APP, ignore_errors=True); APP.mkdir(parents=True)
    for f in app_src.iterdir():
        if f.name in ('profile', 'session', 'sessions', 'profiles', 'windows', 'prototype.log', 'downloads', 'bookmarks', 'settings', 'history', 'annotations', 'ui-language', 'profiles.txt'):
            continue
        (shutil.copytree(f, APP / f.name) if f.is_dir() else shutil.copy2(f, APP / f.name))
    groups = '1\n0 2 0 1 1 1\n' if split else '0\n'   # --split: the two pages restored as a side-by-side group
    (APP / 'session').write_text(f'browser-session-2\n0\n2\n"{BASE}/blue"\n"{BASE}/green"\n{groups}', encoding='utf-8', newline='\n')
    (APP / 'settings').write_text('browser-settings 1\nrestore-tabs=1\n', encoding='utf-8', newline='\n')
    srv = socketserver.ThreadingTCPServer(('127.0.0.1', WEB), Handler); threading.Thread(target=srv.serve_forever, daemon=True).start()
    env = dict(__import__('os').environ)
    env['WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS'] = f'--remote-debugging-port={PORT} --remote-allow-origins=http://127.0.0.1:{PORT}'
    proc = subprocess.Popen([str(APP / 'browser.exe')], cwd=str(APP), env=env)
    try:
        h = wait(lambda: main_window(proc.pid), 20)
        check(bool(h), 'the throwaway copy starts')
        blue, green = f'{BASE}/blue', f'{BASE}/green'
        check(bool(wait(lambda: ws_for(blue), 20)) and bool(wait(lambda: metrics(blue), 10)), 'the blue fixture page is loaded')
        if split:
            split_steps(h, proc.pid, blue, green)
        else:
            time.sleep(1.5)
            if user32.IsZoomed(h):
                post(h, MaximizeCommand); time.sleep(1)
            windowed = rect(h)
            log(f'     windowed bounds {windowed}, monitor {monitor(h)}, dpi {user32.GetDpiForWindow(h)}')
            # F11, three times in a row, windowed.
            for i in (1, 2, 3):
                post(h, FullscreenCommand); time.sleep(1.2)
                check_full(h, proc.pid, blue, BLUE, f'F11 #{i} (windowed)')
                post(h, FullscreenCommand); time.sleep(1.2)
                check_restored(h, proc.pid, blue, windowed, False, f'F11 #{i} off (windowed)')
            # Page fullscreen (trusted click on the fixture's button), left with the Échap command.
            click_fs(h)
            check(bool(wait(lambda: (metrics(blue) or {}).get('fs'), 5)), 'page: requestFullscreen accepted on a user click')
            time.sleep(1.2)
            check_full(h, proc.pid, blue, BLUE, 'page fullscreen (windowed)')
            mt = metrics(blue)
            check(bool(mt) and mt['focus'], 'page fullscreen: the page keeps focus (document.hasFocus())')
            post(h, ExitFullscreenCommand); time.sleep(1.5)
            check_restored(h, proc.pid, blue, windowed, False, 'page fullscreen off by Échap (windowed)')
            # Maximized: F11 and page fullscreen both come back maximized.
            post(h, MaximizeCommand); time.sleep(1.2)
            maxed = rect(h)
            check(bool(user32.IsZoomed(h)), f'maximized for the next checks {maxed}')
            post(h, FullscreenCommand); time.sleep(1.2)
            check_full(h, proc.pid, blue, BLUE, 'F11 (maximized)')
            post(h, FullscreenCommand); time.sleep(1.2)
            check_restored(h, proc.pid, blue, maxed, True, 'F11 off (maximized)')
            click_fs(h); wait(lambda: (metrics(blue) or {}).get('fs'), 5); time.sleep(1.2)
            check_full(h, proc.pid, blue, BLUE, 'page fullscreen (maximized)')
            post(h, ExitFullscreenCommand); time.sleep(1.5)
            check_restored(h, proc.pid, blue, maxed, True, 'page fullscreen off (maximized)')
            # Another tab chosen during a page fullscreen: the page leaves it, the window comes back.
            click_fs(h); wait(lambda: (metrics(blue) or {}).get('fs'), 5); time.sleep(1.0)
            post(h, SelectTabBase + 1); time.sleep(2.0)
            check(not (metrics(blue) or {}).get('fs', True), 'tab change: the page that was fullscreen left it')
            check_restored(h, proc.pid, green, maxed, True, 'tab change during page fullscreen')
            # F11 on the other tab, then a tab change inside browser fullscreen: still full, the new page fills it.
            post(h, FullscreenCommand); time.sleep(1.2)
            check_full(h, proc.pid, green, GREEN, 'F11 on the green tab')
            post(h, SelectTabBase); time.sleep(1.5)
            check_full(h, proc.pid, blue, BLUE, 'F11 then Ctrl+1: the blue tab fills the screen')
            tabs = len([t for t in targets() if t.get('type') == 'page'])
            user32.PostMessageW(h, 0x0201, 1, (20 << 16) | 200); user32.PostMessageW(h, 0x0202, 0, (20 << 16) | 200)   # where the tab strip was
            time.sleep(1.0)
            check(len([t for t in targets() if t.get('type') == 'page']) == tabs, 'F11: a click where the tab strip was hits no hidden chrome control')
            # Chrome shortcuts inside fullscreen: Ctrl+L / F6 have no chrome to reach; the page keeps the keyboard.
            post(h, ord('L')); time.sleep(0.8)
            check(not visible_edit(h), 'F11: Ctrl+L shows no address field over the page')
            post(h, 3000); time.sleep(0.8)   # FocusChrome (F6)
            mt = metrics(blue)
            check(bool(mt) and mt['focus'] and not visible_edit(h), 'F11: F6 keeps the keyboard in the page (document.hasFocus())')
            check(not (set(uia_children(proc.pid)) & CHROME_IDS), 'F11: still no chrome in UI Automation after Ctrl+L / F6')
            post(h, FullscreenCommand); time.sleep(1.2)
            check_restored(h, proc.pid, blue, maxed, True, 'F11 off after the tab change')
            # A new (home) tab, its address field being edited when F11 comes: the field and the chrome leave, only the
            # canvas / home page shows; F11 again brings everything back.
            before = len([t for t in targets() if t.get('type') == 'page'])
            post(h, ord('T')); time.sleep(1.5)
            check(visible_edit(h), 'home tab: Ctrl+T opens the address field (setup)')
            post(h, FullscreenCommand); time.sleep(1.2)
            cs = client_screen(h)
            check(rect(h) == monitor(h), 'F11 on a home tab: the window covers its monitor')
            check(not visible_edit(h), 'F11 on a home tab: the address field being edited is closed')
            top = pixel(h, (cs[2] - cs[0]) // 2, 20)
            check(near(top, (23, 23, 23)), f'F11 on a home tab: only the canvas where the chrome was (y 20: {top})')
            check(not (set(uia_children(proc.pid)) & CHROME_IDS), 'F11 on a home tab: no chrome control exposed to UI Automation')
            user32.PostMessageW(h, 0x0201, 1, (20 << 16) | 200); user32.PostMessageW(h, 0x0202, 0, (20 << 16) | 200)
            time.sleep(1.0)
            check(len([t for t in targets() if t.get('type') == 'page']) <= before + 1, 'F11 on a home tab: a click where the tab strip was opens nothing')
            post(h, FullscreenCommand); time.sleep(1.2)
            check(rect(h) == maxed and bool(user32.IsZoomed(h)) and {'Toolbar', 'Tabs', 'Settings'} <= set(uia_children(proc.pid)),
                  'F11 off on the home tab: maximized again, chrome back')
    finally:
        try:
            h = main_window(proc.pid)
            if h:
                user32.PostMessageW(h, 0x0010, 0, 0)
            proc.wait(10)
        except Exception:
            subprocess.run(['taskkill', '/F', '/T', '/PID', str(proc.pid)], capture_output=True)
        srv.shutdown()
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'report.txt').write_text('\n'.join(lines) + f'\n{failures} failures\n', encoding='utf-8')
    log(f'{failures} failures; report in {OUT}')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8')
    ap = argparse.ArgumentParser(); ap.add_argument('--app', default='build/fullscreen-fix'); ap.add_argument('--split', action='store_true')
    a = ap.parse_args()
    sys.exit(run((ROOT / a.app).resolve(), a.split))
