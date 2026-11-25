#!/usr/bin/env python3
"""Fullscreen in ONE step on YouTube (user report 25/11: entering / leaving a video's fullscreen, the window « jumps »
several times before it settles; same on exit). WINDOWED check: it takes the screen (the window comes to the front and
is filmed), so it runs only in a slot given by the coordinator, never while someone uses the PC.

    python tests/e2e/fullscreen_youtube.py --app build/fs-jump [--layout top|side] [--focus] [--split] [--windowed]

Each run: a throwaway copy of the build (build/fullscreen-youtube-app, portable: profile, session and log beside the
exe; never the installed browser's data, nobody signed in), one YouTube video (+ a local page; --split: both side by
side), then
  1. the player's fullscreen button  → in;  Échap → out;
  2. the F key                         → in;  the player's button → out.
Around each switch, every ~5 ms: the window rectangle and show state, and the rectangles of the page windows (the
WebView2 controllers' bounds); every ~16 ms: a small film of the monitor (frames stacked in one BMP). The browser's
own trace (« FULLSCREEN switch / step » in prototype.log) must show one window size change at most, in the show state
it started from, and one bounds change at most per page on screen. Reports: build/fullscreen-youtube/<run>/.

Clicks and keys are messages posted to OUR page's windows (trusted input for the engine, no SendInput); the YouTube
consent page (EU) is answered « reject all » through DevTools (loopback port of this copy only). The video is paused
so the film only changes when the layout does. Every process of the copy runs in a job object, closed at the end.
"""
from __future__ import annotations
import argparse, ctypes, ctypes.wintypes as W, http.server, json, os, pathlib, shutil, socketserver, struct, subprocess, sys, threading, time, urllib.request

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / 'tests' / 'annotations'))
import sandbox_qa as Q   # noqa: E402  (CDP WebSocket client)

APP = ROOT / 'build' / 'fullscreen-youtube-app'
PORT, WEB = 9348, 5204
VIDEO = 'https://www.youtube.com/watch?v=aqz-KE-bpKQ'   # Big Buck Bunny (Blender Foundation), public
LOCAL = f'http://127.0.0.1:{WEB}/notes'
user32, gdi32, k32 = ctypes.windll.user32, ctypes.windll.gdi32, ctypes.WinDLL('kernel32', use_last_error=True)
ntdll = ctypes.WinDLL('ntdll')
Shortcut, ExitFullscreenCommand, MaximizeCommand = 0x8001, 3055, ord('M')
WM_MOUSEMOVE, WM_LBUTTONDOWN, WM_LBUTTONUP, WM_KEYDOWN, WM_KEYUP, WM_CHAR = 0x200, 0x201, 0x202, 0x100, 0x101, 0x102
out_dir: pathlib.Path
lines: list[str] = []
failures = 0


def log(text):
    print(text, flush=True); lines.append(text)


def check(ok, what):
    global failures
    log(('ok   ' if ok else 'FAIL ') + what)
    if not ok:
        failures += 1


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        body = '<!doctype html><html lang="fr"><head><meta charset="utf-8"><title>Notes</title><style>html,body{margin:0;height:100%;background:#22aa55}</style></head><body></body></html>'.encode()
        self.send_response(200); self.send_header('Content-Type', 'text/html; charset=utf-8'); self.end_headers(); self.wfile.write(body)

    def log_message(self, *a):
        pass


# ---- job object: every process of the copy ends with it ------------------------------------------------------------
class BasicLimit(ctypes.Structure):
    _fields_ = [('a', ctypes.c_longlong), ('b', ctypes.c_longlong), ('LimitFlags', W.DWORD), ('c', ctypes.c_size_t), ('d', ctypes.c_size_t),
                ('e', W.DWORD), ('f', ctypes.c_size_t), ('g', W.DWORD), ('h', W.DWORD)]


class ExtendedLimit(ctypes.Structure):
    _fields_ = [('Basic', BasicLimit), ('Io', ctypes.c_ulonglong * 6), ('i', ctypes.c_size_t), ('j', ctypes.c_size_t), ('k', ctypes.c_size_t), ('l', ctypes.c_size_t)]


k32.CreateJobObjectW.restype = W.HANDLE
k32.CreateJobObjectW.argtypes = [ctypes.c_void_p, W.LPCWSTR]
k32.SetInformationJobObject.argtypes = [W.HANDLE, ctypes.c_int, ctypes.c_void_p, W.DWORD]
k32.AssignProcessToJobObject.argtypes = [W.HANDLE, W.HANDLE]
k32.CloseHandle.argtypes = [W.HANDLE]
ntdll.NtResumeProcess.argtypes = [W.HANDLE]


def start_in_job(cmd, env):
    job = k32.CreateJobObjectW(None, None)
    info = ExtendedLimit(); info.Basic.LimitFlags = 0x2000   # KILL_ON_JOB_CLOSE
    if not job or not k32.SetInformationJobObject(job, 9, ctypes.byref(info), ctypes.sizeof(info)):
        raise OSError('job object unavailable')
    proc = subprocess.Popen(cmd, cwd=str(APP), env=env, creationflags=0x4)   # suspended until in the job
    handle = W.HANDLE(int(proc._handle))
    if not k32.AssignProcessToJobObject(job, handle):
        proc.kill(); raise OSError('AssignProcessToJobObject failed')
    ntdll.NtResumeProcess(handle)
    return proc, job


# ---- windows --------------------------------------------------------------------------------------------------------
def rect(h):
    r = W.RECT(); user32.GetWindowRect(h, ctypes.byref(r)); return (r.left, r.top, r.right, r.bottom)


def children(h, cls_prefix, direct=False):
    found = []
    @ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    def cb(c, _):
        name = ctypes.create_unicode_buffer(64); user32.GetClassNameW(c, name, 64)
        if name.value.startswith(cls_prefix) and user32.IsWindowVisible(c) and (not direct or user32.GetParent(c) == h):
            found.append(c)
        return True
    user32.EnumChildWindows(h, cb, 0)
    return found


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


def monitor(h):
    class MI(ctypes.Structure):
        _fields_ = [('cbSize', W.DWORD), ('rcMonitor', W.RECT), ('rcWork', W.RECT), ('dwFlags', W.DWORD)]
    mi = MI(); mi.cbSize = ctypes.sizeof(MI); user32.GetMonitorInfoW(user32.MonitorFromWindow(h, 2), ctypes.byref(mi))
    m = mi.rcMonitor; return (m.left, m.top, m.right, m.bottom)


def wait(pred, seconds=8.0, step=0.1):
    end = time.time() + seconds
    while time.time() < end:
        v = pred()
        if v:
            return v
        time.sleep(step)
    return pred()


def front(h):
    user32.SwitchToThisWindow(h, True); time.sleep(0.3)


# ---- DevTools (loopback, this copy only) ----------------------------------------------------------------------------
def youtube_target():
    try:
        for t in json.loads(urllib.request.urlopen(f'http://127.0.0.1:{PORT}/json', timeout=5).read()):
            if t.get('type') == 'page' and 'youtube.com' in t.get('url', ''):
                return t
    except Exception:
        return None
    return None


def js(expr):
    t = youtube_target()
    if not t:
        return None
    ws = Q.WS(t['webSocketDebuggerUrl'])
    try:
        r = ws.call('Runtime.evaluate', {'expression': expr, 'returnByValue': True, 'awaitPromise': True})
        return r.get('result', {}).get('value')
    except Exception:
        return None
    finally:
        try: ws.s.close()   # an attached client must not stay (it changes how the engine reports the page)
        except Exception: pass


CONSENT = r'''(() => { const b = [...document.querySelectorAll('button, tp-yt-paper-button, a')].find(x => /reject all|tout refuser|alle ablehnen/i.test((x.innerText || x.getAttribute('aria-label') || '').trim()));
  if (b) { b.click(); return 'rejected'; } return location.host; })()'''
STATE = r'''(() => { const v = document.querySelector('video'), b = document.querySelector('.ytp-fullscreen-button'), r = b && b.getBoundingClientRect();
  return {fs: !!document.fullscreenElement, dpr: devicePixelRatio, w: innerWidth, h: innerHeight, player: !!document.querySelector('#movie_player'),
          paused: v ? v.paused : null, button: r && r.width ? [r.left + r.width / 2, r.top + r.height / 2] : null}; })()'''


def state():
    return js(STATE) or {}


# ---- input posted to our page's windows -----------------------------------------------------------------------------
def page_widget(h, x_screen=None):
    widgets = children(h, 'Chrome_RenderWidgetHostHWND')
    if x_screen is None or len(widgets) <= 1:
        return widgets[0] if widgets else None
    for w in widgets:
        r = rect(w)
        if r[0] <= x_screen < r[2]:
            return w
    return widgets[0]


def youtube_widget(h):
    """The render widget showing YouTube: in a split, the panel holding the player (left panel by the session)."""
    widgets = children(h, 'Chrome_RenderWidgetHostHWND')
    return min(widgets, key=lambda w: rect(w)[0]) if widgets else None


def click_css(h, x, y, dpr):
    w = youtube_widget(h)
    if not w:
        return False
    lp = (int(y * dpr) << 16) | int(x * dpr)
    user32.PostMessageW(w, WM_MOUSEMOVE, 0, lp); time.sleep(0.25)
    user32.PostMessageW(w, WM_MOUSEMOVE, 0, lp + 1); time.sleep(0.05)
    user32.PostMessageW(w, WM_LBUTTONDOWN, 1, lp); time.sleep(0.05); user32.PostMessageW(w, WM_LBUTTONUP, 0, lp)
    return True


class GUITHREADINFO(ctypes.Structure):
    _fields_ = [('cbSize', W.DWORD), ('flags', W.DWORD), ('hwndActive', W.HWND), ('hwndFocus', W.HWND), ('hwndCapture', W.HWND),
                ('hwndMenuOwner', W.HWND), ('hwndMoveSize', W.HWND), ('hwndCaret', W.HWND), ('rcCaret', W.RECT)]


def key(h, vk, char=None):
    """A key to the window that holds the keyboard in the page's thread (the page sees a trusted key)."""
    w = youtube_widget(h)
    if not w:
        return False
    tid = user32.GetWindowThreadProcessId(w, None); gi = GUITHREADINFO(); gi.cbSize = ctypes.sizeof(gi)
    target = gi.hwndFocus if user32.GetGUIThreadInfo(tid, ctypes.byref(gi)) and gi.hwndFocus else w
    scan = user32.MapVirtualKeyW(vk, 0)
    user32.PostMessageW(target, WM_KEYDOWN, vk, 1 | (scan << 16))
    if char:
        user32.PostMessageW(target, WM_CHAR, ord(char), 1 | (scan << 16))
    time.sleep(0.05); user32.PostMessageW(target, WM_KEYUP, vk, 1 | (scan << 16) | (3 << 30))
    return True


# ---- recording: window / page rectangles every ~5 ms, a small film every ~16 ms -------------------------------------
class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [('biSize', W.DWORD), ('biWidth', W.LONG), ('biHeight', W.LONG), ('biPlanes', W.WORD), ('biBitCount', W.WORD), ('biCompression', W.DWORD),
                ('biSizeImage', W.DWORD), ('a', W.LONG), ('b', W.LONG), ('c', W.DWORD), ('d', W.DWORD)]


class Recorder:
    FW, FH = 320, 180

    def __init__(self, h):
        self.h = h; self.stop = threading.Event(); self.samples = []; self.frames = []
        self.mon = monitor(h)

    def _rects(self):
        while not self.stop.is_set():
            s = (rect(self.h), bool(user32.IsZoomed(self.h)), tuple(sorted(rect(c) for c in children(self.h, 'Chrome_WidgetWin', direct=True))))
            if not self.samples or self.samples[-1][1:] != s:
                self.samples.append((time.perf_counter(),) + s)
            time.sleep(0.005)

    def _film(self):
        screen = user32.GetDC(None); mem = gdi32.CreateCompatibleDC(screen)
        bi = BITMAPINFOHEADER(ctypes.sizeof(BITMAPINFOHEADER), self.FW, -self.FH, 1, 32, 0, 0, 0, 0, 0, 0); bits = ctypes.c_void_p()
        bmp = gdi32.CreateDIBSection(mem, ctypes.byref(bi), 0, ctypes.byref(bits), None, 0); old = gdi32.SelectObject(mem, bmp)
        gdi32.SetStretchBltMode(mem, 3)   # COLORONCOLOR: fast
        m = self.mon
        try:
            while not self.stop.is_set():
                t = time.perf_counter()
                gdi32.StretchBlt(mem, 0, 0, self.FW, self.FH, screen, m[0], m[1], m[2] - m[0], m[3] - m[1], 0x00CC0020 | 0x40000000)
                self.frames.append((t, ctypes.string_at(bits.value, self.FW * self.FH * 4)))
                time.sleep(max(0, 0.016 - (time.perf_counter() - t)))
        finally:
            gdi32.SelectObject(mem, old); gdi32.DeleteObject(bmp); gdi32.DeleteDC(mem); user32.ReleaseDC(None, screen)

    def __enter__(self):
        self.t0 = time.perf_counter()
        self.threads = [threading.Thread(target=self._rects, daemon=True), threading.Thread(target=self._film, daemon=True)]
        for t in self.threads:
            t.start()
        return self

    def __exit__(self, *a):
        self.stop.set()
        for t in self.threads:
            t.join(2)

    def save(self, name):
        path = out_dir / f'{name}.timeline.txt'
        with open(path, 'w', encoding='utf-8') as f:
            for s in self.samples:
                f.write(f'+{(s[0] - self.t0) * 1000:7.1f} ms  window {s[1]} {"maximized" if s[2] else "normal"}  pages {list(s[3])}\n')
        # Frames stacked top to bottom in one BMP (first frame on top); the time of each in <name>.film.txt.
        if self.frames:
            w, h = self.FW, self.FH * len(self.frames)
            # A BMP stores its rows bottom-up: frames in reverse order, each one's rows reversed.
            data = b''.join(b''.join(fr[r * w * 4:(r + 1) * w * 4] for r in range(self.FH - 1, -1, -1)) for _, fr in reversed(self.frames))
            head = struct.pack('<2sIHHI', b'BM', 54 + len(data), 0, 0, 54) + struct.pack('<IiiHHIIiiII', 40, w, h, 1, 32, 0, len(data), 2835, 2835, 0, 0)
            (out_dir / f'{name}.film.bmp').write_bytes(head + data)
            with open(out_dir / f'{name}.film.txt', 'w', encoding='utf-8') as f:
                for i, (t, _) in enumerate(self.frames):
                    f.write(f'frame {i:3d} +{(t - self.t0) * 1000:7.1f} ms\n')
        return path

    def summary(self):
        wins = []
        for s in self.samples:
            if not wins or wins[-1] != (s[1], s[2]):
                wins.append((s[1], s[2]))
        return wins


# ---- the browser's own trace -----------------------------------------------------------------------------------------
trace_pos = 0


def trace_since(what):
    global trace_pos
    p = APP / 'prototype.log'
    data = p.read_bytes() if p.exists() else b''
    if len(data) < trace_pos:
        trace_pos = 0
    text = data[trace_pos:].decode('utf-8', 'replace'); trace_pos = len(data)
    groups, cur = [], None
    for line in text.splitlines():
        if 'FULLSCREEN switch ' in line:
            cur = [line]; groups.append(cur)
        elif 'FULLSCREEN step ' in line and cur is not None:
            cur.append(line)
    for g in groups:
        start = g[0].rsplit(' ', 1)[-1]
        windows = [l for l in g if ' ms window ' in l]
        bounds: dict[str, int] = {}
        for l in g:
            if ' ms page ' in l and not l.endswith(('(hidden)', ' hidden', ' shown')):
                serial = l.split(' ms page ', 1)[1].split(' ', 1)[0]; bounds[serial] = bounds.get(serial, 0) + 1
        log('     trace ' + ' | '.join(x.split('FULLSCREEN ', 1)[-1] for x in g))
        check(len(windows) <= 1 and all(l.rsplit(' ', 1)[-1] == start for l in windows) and all(n <= 1 for n in bounds.values()),
              f'{what}: the browser trace shows one step ({len(windows)} window size change(s), page bounds changes {bounds})')
    if not groups:
        check(False, f'{what}: a FULLSCREEN switch in the browser trace')


def switch(h, name, act, want_fs):
    with Recorder(h) as rec:
        time.sleep(0.15); act()
        ok = wait(lambda: state().get('fs') == want_fs, 6, 0.05)
        time.sleep(1.2)
    rec.save(name)
    wins = rec.summary()
    log(f'     {name}: window states ' + ' → '.join(f'{r} {"max" if z else "normal"}' for r, z in wins) + f'; {len(rec.frames)} frames filmed')
    check(bool(ok), f'{name}: the page is {"in" if want_fs else "out of"} fullscreen')
    check(len(wins) <= 2 and len({z for _, z in wins}) == 1, f'{name}: the window went straight from one rectangle to the other, show state kept ({len(wins)} states)')
    pages = [s[3] for s in rec.samples]
    distinct = []
    for p in pages:
        if not distinct or distinct[-1] != p:
            distinct.append(p)
    log(f'     {name}: page windows ' + ' → '.join(str(list(p)) for p in distinct))
    trace_since(name)


def main():
    global out_dir
    ap = argparse.ArgumentParser()
    ap.add_argument('--app', default='build/fs-jump'); ap.add_argument('--layout', choices=('top', 'side'), default='top')
    ap.add_argument('--focus', action='store_true'); ap.add_argument('--split', action='store_true'); ap.add_argument('--windowed', action='store_true')
    a = ap.parse_args()
    label = '-'.join([a.layout] + (['focus'] if a.focus else []) + (['split'] if a.split else []) + (['windowed'] if a.windowed else ['maximized']))
    out_dir = ROOT / 'build' / 'fullscreen-youtube' / label; shutil.rmtree(out_dir, ignore_errors=True); out_dir.mkdir(parents=True)
    src = (ROOT / a.app).resolve()
    shutil.rmtree(APP, ignore_errors=True); APP.mkdir(parents=True)
    for f in src.iterdir():
        if f.name in ('profile', 'session', 'sessions', 'profiles', 'windows', 'prototype.log', 'downloads', 'bookmarks', 'settings', 'history', 'annotations', 'ui-language', 'profiles.txt'):
            continue
        (shutil.copytree(f, APP / f.name) if f.is_dir() else shutil.copy2(f, APP / f.name))
    groups = '1\n0 2 0 1 1 1\n' if a.split else '0\n'
    (APP / 'session').write_text(f'browser-session-2\n0\n2\n"{VIDEO}"\n"{LOCAL}"\n{groups}', encoding='utf-8', newline='\n')
    (APP / 'settings').write_text('browser-settings 1\nrestore-tabs=1\n' + ('focus-mode=1\n' if a.focus else '') + f'tabs-position={a.layout}\n', encoding='utf-8', newline='\n')
    srv = socketserver.ThreadingTCPServer(('127.0.0.1', WEB), Handler); threading.Thread(target=srv.serve_forever, daemon=True).start()
    env = dict(os.environ); env['BROWSER_NO_ONBOARDING'] = '1'
    env['WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS'] = f'--remote-debugging-port={PORT} --remote-allow-origins=http://127.0.0.1:{PORT}'
    proc, job = start_in_job([str(APP / 'browser.exe'), f'--tab-layout={a.layout}'], env)
    log(f'run {label}: copy {APP}, pid {proc.pid}')
    try:
        h = wait(lambda: main_window(proc.pid), 20)
        check(bool(h), 'the throwaway copy starts')
        if not h:
            return 1
        front(h)
        if a.windowed and user32.IsZoomed(h):
            user32.PostMessageW(h, Shortcut, MaximizeCommand, 0); time.sleep(1)
        if not a.windowed and not user32.IsZoomed(h):
            user32.PostMessageW(h, Shortcut, MaximizeCommand, 0); time.sleep(1)
        check(bool(wait(youtube_target, 30)), 'the YouTube page is open')
        for _ in range(3):   # the EU consent page, answered « reject all »
            if js(CONSENT) == 'rejected':
                time.sleep(4)
        st = wait(lambda: (lambda s: s if s.get('player') and s.get('button') else None)(state()), 40, 0.5)
        check(bool(st), f'the player and its fullscreen button are there ({st})')
        if not st:
            return 1
        js("(() => { const v = document.querySelector('video'); if (v) { v.muted = true; v.pause(); } })()"); time.sleep(0.5)
        log(f'     window {rect(h)} {"maximized" if user32.IsZoomed(h) else "normal"}, monitor {monitor(h)}, page {st["w"]}x{st["h"]} CSS px x {st["dpr"]}')
        global trace_pos   # the trace is read from here on
        p = APP / 'prototype.log'; trace_pos = p.stat().st_size if p.exists() else 0
        before = rect(h); zoomed = bool(user32.IsZoomed(h))
        front(h)
        # 1. The player's button in, Échap out.
        s = state(); switch(h, '1-button-in', lambda: click_css(h, *s['button'], s['dpr']), True)
        check(rect(h) == monitor(h), f'1-button-in: the window covers its monitor {rect(h)}')
        js("(() => { const v = document.querySelector('video'); if (v) v.pause(); })()")
        def esc():
            key(h, 0x1B)
            if not wait(lambda: not state().get('fs'), 2, 0.05):
                log('     (posted Échap not seen by the page: the browser\'s Échap command is used)'); user32.PostMessageW(h, Shortcut, ExitFullscreenCommand, 0)
        switch(h, '2-escape-out', esc, False)
        check(rect(h) == before and bool(user32.IsZoomed(h)) == zoomed, f'2-escape-out: the window is back {rect(h)} == {before}, {"maximized" if zoomed else "windowed"}')
        # 2. The F key in, the player's button out.
        front(h)
        js("(() => { document.activeElement && document.activeElement.blur && document.activeElement.blur(); const p = document.querySelector('#movie_player'); if (p) p.focus(); })()"); time.sleep(0.3)
        switch(h, '3-f-key-in', lambda: key(h, 0x46, 'f'), True)
        check(rect(h) == monitor(h), f'3-f-key-in: the window covers its monitor {rect(h)}')
        js("(() => { const v = document.querySelector('video'); if (v) v.pause(); })()")
        def button_out():
            w = youtube_widget(h); r = rect(w); k = st['dpr']
            user32.PostMessageW(w, WM_MOUSEMOVE, 0, (int((r[3] - r[1]) // 2) << 16) | int((r[2] - r[0]) // 2)); time.sleep(0.6)   # the controls come back
            s2 = state()
            if s2.get('button'):
                click_css(h, *s2['button'], s2['dpr'])
            else:
                log('     (no fullscreen button in fullscreen: the browser\'s Échap command is used)'); user32.PostMessageW(h, Shortcut, ExitFullscreenCommand, 0)
        switch(h, '4-button-out', button_out, False)
        check(rect(h) == before and bool(user32.IsZoomed(h)) == zoomed, f'4-button-out: the window is back {rect(h)} == {before}, {"maximized" if zoomed else "windowed"}')
    finally:
        try:
            h = main_window(proc.pid)
            if h:
                user32.PostMessageW(h, 0x0010, 0, 0)
            proc.wait(10)
        except Exception:
            pass
        k32.CloseHandle(job)   # KILL_ON_JOB_CLOSE: every process of the copy ends here
        srv.shutdown()
    (out_dir / 'report.txt').write_text('\n'.join(lines) + f'\n{failures} failures\n', encoding='utf-8')
    log(f'{failures} failures; report, timelines and films in {out_dir}')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8')
    sys.exit(main())
