#!/usr/bin/env python3
"""Fullscreen in ONE step (user report 25/11: entering or leaving a video's fullscreen, the window « jumps » several
times before it settles; same on exit), on the real browser.

    python tests/e2e/fullscreen_steps.py --app build/fs-jump [--site local|vimeo|youtube] [--layout top|side] [--focus] [--split]
                                         [--windowed] [--desktop screen|hidden]

--desktop screen (default) TAKES THE SCREEN: the window comes to the front and the monitor is filmed. Only in a slot
given by the coordinator, never while someone uses the PC. --desktop hidden runs the browser on a Windows desktop of
its own that is never displayed (nothing on the screen, mouse or keyboard; no film): for the driver itself, daytime.

Each run: a throwaway copy of the build (build/fullscreen-steps-app, portable: profile, session and log beside the
exe; never the installed browser's data, nobody signed in, no chat), the player page (--site local: a page of this
script on 127.0.0.1, a green « player » on a blue page with its own fullscreen button and the F key; --site youtube:
one public video, the EU consent answered « reject all ») + a local page (--split: both side by side), then
  1. the player's fullscreen button  → in;  Échap → out;
  2. the F key                         → in;  the player's button → out.
Around each switch, every ~5 ms: the window rectangle and show state and the page windows' rectangles (the WebView2
controllers' bounds); on the screen, every ~16 ms a small film of the monitor (frames stacked in one BMP) and, for the
local page, the share of the monitor that is the player's green per frame: its distinct values are the visible states.
The browser's own trace (« FULLSCREEN switch / step » in prototype.log) must show one window size change at most, in
the show state it started from, and one bounds change at most per page on screen. Reports: build/fullscreen-steps/<run>/.

Clicks and keys are messages posted to OUR page's windows (trusted input for the engine, no SendInput). DevTools
(loopback port of this copy only) reads the page's state. Every process of the copy runs in a job object, closed at
the end; nothing else is stopped.
"""
from __future__ import annotations
import argparse, ctypes, ctypes.wintypes as W, http.server, json, os, pathlib, shutil, socketserver, struct, sys, threading, time, urllib.request

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / 'tests' / 'annotations'))
import sandbox_qa as Q   # noqa: E402  (CDP WebSocket client)

APP = ROOT / 'build' / 'fullscreen-steps-app'
PORT, WEB = 9348, 5204
YOUTUBE = 'https://www.youtube.com/watch?v=aqz-KE-bpKQ'   # Big Buck Bunny (Blender Foundation), public
PLAYER, NOTES = f'http://127.0.0.1:{WEB}/player', f'http://127.0.0.1:{WEB}/notes'
user32, gdi32 = ctypes.WinDLL('user32', use_last_error=True), ctypes.windll.gdi32
k32, ntdll = ctypes.WinDLL('kernel32', use_last_error=True), ctypes.WinDLL('ntdll')
Shortcut, ExitFullscreenCommand, MaximizeCommand = 0x8001, 3055, ord('M')
WM_MOUSEMOVE, WM_LBUTTONDOWN, WM_LBUTTONUP, WM_KEYDOWN, WM_KEYUP, WM_CHAR, WM_CLOSE = 0x200, 0x201, 0x202, 0x100, 0x101, 0x102, 0x10
GREEN = (0x22, 0xAA, 0x55)
out_dir: pathlib.Path
desk = None   # the hidden desktop, when --desktop hidden
lines: list[str] = []
failures = 0


def log(text):
    print(text, flush=True); lines.append(text)


def check(ok, what):
    global failures
    log(('ok   ' if ok else 'FAIL ') + what)
    if not ok:
        failures += 1


PLAYER_PAGE = '''<!doctype html><html lang="fr"><head><meta charset="utf-8"><title>Lecteur</title><style>
html,body{margin:0;height:100%;background:#3366ee;overflow:hidden}
#player{position:absolute;left:12%;top:14%;width:56%;height:52%;background:#22aa55}
#fs{position:absolute;right:16px;bottom:16px;width:96px;height:48px;font:16px system-ui}
</style></head><body><div id="player" tabindex="0"><button id="fs">Plein écran</button></div><script>
const p = document.getElementById('player');
const toggle = () => document.fullscreenElement ? document.exitFullscreen() : p.requestFullscreen();
document.getElementById('fs').onclick = toggle;
document.addEventListener('keydown', e => { if (e.key === 'f' || e.key === 'F') toggle(); });
p.focus();
</script></body></html>'''.encode('utf-8')
NOTES_PAGE = '<!doctype html><html lang="fr"><head><meta charset="utf-8"><title>Notes</title><style>html,body{margin:0;height:100%;background:#eeeeee}</style></head><body></body></html>'.encode('utf-8')


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        body = PLAYER_PAGE if self.path.startswith('/player') else NOTES_PAGE
        self.send_response(200); self.send_header('Content-Type', 'text/html; charset=utf-8'); self.end_headers(); self.wfile.write(body)

    def log_message(self, *a):
        pass


# ---- process: in a job object (KILL_ON_JOB_CLOSE), on the screen or on a hidden desktop -----------------------------
class BasicLimit(ctypes.Structure):
    _fields_ = [('a', ctypes.c_longlong), ('b', ctypes.c_longlong), ('LimitFlags', W.DWORD), ('c', ctypes.c_size_t), ('d', ctypes.c_size_t),
                ('e', W.DWORD), ('f', ctypes.c_size_t), ('g', W.DWORD), ('h', W.DWORD)]


class ExtendedLimit(ctypes.Structure):
    _fields_ = [('Basic', BasicLimit), ('Io', ctypes.c_ulonglong * 6), ('i', ctypes.c_size_t), ('j', ctypes.c_size_t), ('k', ctypes.c_size_t), ('l', ctypes.c_size_t)]


class STARTUPINFO(ctypes.Structure):
    _fields_ = [('cb', W.DWORD), ('lpReserved', W.LPWSTR), ('lpDesktop', W.LPWSTR), ('lpTitle', W.LPWSTR), ('dwX', W.DWORD), ('dwY', W.DWORD),
                ('dwXSize', W.DWORD), ('dwYSize', W.DWORD), ('dwXCountChars', W.DWORD), ('dwYCountChars', W.DWORD), ('dwFillAttribute', W.DWORD),
                ('dwFlags', W.DWORD), ('wShowWindow', W.WORD), ('cbReserved2', W.WORD), ('lpReserved2', ctypes.c_void_p),
                ('hStdInput', W.HANDLE), ('hStdOutput', W.HANDLE), ('hStdError', W.HANDLE)]


class PROCESS_INFORMATION(ctypes.Structure):
    _fields_ = [('hProcess', W.HANDLE), ('hThread', W.HANDLE), ('dwProcessId', W.DWORD), ('dwThreadId', W.DWORD)]


k32.CreateJobObjectW.restype = W.HANDLE
k32.CreateJobObjectW.argtypes = [ctypes.c_void_p, W.LPCWSTR]
k32.SetInformationJobObject.argtypes = [W.HANDLE, ctypes.c_int, ctypes.c_void_p, W.DWORD]
k32.AssignProcessToJobObject.argtypes = [W.HANDLE, W.HANDLE]
k32.CloseHandle.argtypes = [W.HANDLE]
k32.ResumeThread.argtypes = [W.HANDLE]
k32.WaitForSingleObject.argtypes = [W.HANDLE, W.DWORD]
k32.CreateProcessW.argtypes = [W.LPCWSTR, W.LPWSTR, ctypes.c_void_p, ctypes.c_void_p, W.BOOL, W.DWORD, ctypes.c_void_p, W.LPCWSTR,
                               ctypes.POINTER(STARTUPINFO), ctypes.POINTER(PROCESS_INFORMATION)]
user32.CreateDesktopW.restype = W.HANDLE
user32.CreateDesktopW.argtypes = [W.LPCWSTR, W.LPCWSTR, ctypes.c_void_p, W.DWORD, W.DWORD, ctypes.c_void_p]
user32.SetThreadDesktop.argtypes = [W.HANDLE]
user32.CloseDesktop.argtypes = [W.HANDLE]


def start_in_job(cmd: list[str], env: dict, desktop: str | None):
    job = k32.CreateJobObjectW(None, None)
    info = ExtendedLimit(); info.Basic.LimitFlags = 0x2000
    if not job or not k32.SetInformationJobObject(job, 9, ctypes.byref(info), ctypes.sizeof(info)):
        raise OSError('job object unavailable')
    si = STARTUPINFO(); si.cb = ctypes.sizeof(si)
    if desktop:
        si.lpDesktop = 'WinSta0\\' + desktop
    pi = PROCESS_INFORMATION()
    block = ''.join(f'{k}={v}\0' for k, v in env.items()) + '\0'
    line = ctypes.create_unicode_buffer(' '.join(f'"{c}"' for c in cmd))
    if not k32.CreateProcessW(cmd[0], line, None, None, False, 0x4 | 0x400, ctypes.create_unicode_buffer(block), str(APP), ctypes.byref(si), ctypes.byref(pi)):
        raise OSError(f'CreateProcessW failed {ctypes.get_last_error()}')
    if not k32.AssignProcessToJobObject(job, pi.hProcess):
        raise OSError('AssignProcessToJobObject failed')
    k32.ResumeThread(pi.hThread); k32.CloseHandle(pi.hThread)
    return pi.hProcess, pi.dwProcessId, job


def on_desktop():
    """This thread works on the run's desktop (EnumWindows and GUI thread info are per desktop)."""
    if desk:
        user32.SetThreadDesktop(desk)


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
    if not desk:
        user32.SwitchToThisWindow(h, True); time.sleep(0.3)


# ---- the sites ------------------------------------------------------------------------------------------------------
class Site:
    local = True   # the local player page: its green is measured on the film
    url = PLAYER
    host = '127.0.0.1:%d/player' % WEB
    # The page's state: in fullscreen, devicePixelRatio, the viewport, the player's fullscreen button (CSS px, centre).
    state_js = r'''(() => { const b = document.getElementById('fs'), r = b && b.getBoundingClientRect();
      return {fs: !!document.fullscreenElement, dpr: devicePixelRatio, w: innerWidth, h: innerHeight, ready: !!b,
              button: r && r.width ? [r.left + r.width / 2, r.top + r.height / 2] : null}; })()'''
    focus_js = "(() => { const p = document.getElementById('player'); if (p) p.focus(); return !!p; })()"

    def prepare(self):
        return True

    def quiet(self):
        pass


class Vimeo(Site):
    url = 'https://player.vimeo.com/video/76979871'   # « The New Vimeo Player » (Vimeo Staff), public
    host = 'player.vimeo.com'
    local = False
    state_js = r'''(() => { const b = document.querySelector('[data-fullscreen-button], button.fullscreen, button[aria-label*="ullscreen" i], button[aria-label*="plein" i]'), r = b && b.getBoundingClientRect();
      const on = r && r.width && r.right <= innerWidth + 1 && r.bottom <= innerHeight + 1;
      return {fs: !!document.fullscreenElement, dpr: devicePixelRatio, w: innerWidth, h: innerHeight, ready: !!document.querySelector('video'),
              seen: r ? [Math.round(r.left), Math.round(r.top), Math.round(r.width), Math.round(r.height)] : null,
              button: on ? [r.left + r.width / 2, r.top + r.height / 2] : null}; })()'''
    focus_js = "(() => { const v = document.querySelector('.vp-video-wrapper, video, body'); if (v) { v.setAttribute('tabindex', '-1'); v.focus(); } return !!v; })()"

    def prepare(self):
        # The controls show on a pointer over the player.
        return True

    def quiet(self):
        js("(() => { for (const v of document.querySelectorAll('video')) { v.muted = true; v.pause(); } })()", self.host)


class YouTube(Site):
    local = False
    url = YOUTUBE
    host = 'youtube.com'
    state_js = r'''(() => { const p = document.querySelector('#movie_player'), b = p && p.querySelector('.ytp-fullscreen-button'), r = b && b.getBoundingClientRect();
      const on = r && r.width && r.right <= innerWidth + 1 && r.bottom <= innerHeight + 1;
      return {fs: !!document.fullscreenElement, dpr: devicePixelRatio, w: innerWidth, h: innerHeight, ready: !!p, url: location.href,
              seen: r ? [Math.round(r.left), Math.round(r.top), Math.round(r.width), Math.round(r.height)] : null,
              ad: !!(p && p.classList.contains('ad-showing')), autohide: !!(p && p.classList.contains('ytp-autohide')),
              button: on ? [r.left + r.width / 2, r.top + r.height / 2] : null}; })()'''
    focus_js = "(() => { document.activeElement && document.activeElement.blur && document.activeElement.blur(); const p = document.querySelector('#movie_player'); if (p) p.focus(); return !!p; })()"
    CONSENT = r'''(() => { const b = [...document.querySelectorAll('button, tp-yt-paper-button, a, [role=button]')].find(x => /reject all|tout refuser|alle ablehnen|rechazar todo/i.test((x.innerText || x.getAttribute('aria-label') || '').trim()));
      if (b) { b.click(); return 'rejected'; } return location.host; })()'''

    def prepare(self):
        # The EU consent (consent.youtube.com, or a dialog over the page that comes a few seconds later), answered
        # « reject all ». NOTE 25/11: on this PC a fresh profile then meets « Connectez-vous pour confirmer que vous
        # n'êtes pas un robot » instead of the video, and no test may sign in: use --site vimeo for a real site.
        end = time.time() + 20
        while time.time() < end:
            r = js(self.CONSENT, self.host)
            if r == 'rejected':
                log('     consent: rejected'); time.sleep(4); continue
            st = js(self.state_js, self.host) or {}
            if st.get('button'):
                break
            time.sleep(1.5)
        return True

    def quiet(self):   # the video (or its ad) paused and muted: the film changes only with the layout
        js("(() => { for (const v of document.querySelectorAll('video')) { v.muted = true; v.pause(); } })()", self.host)


# ---- DevTools (loopback, this copy only) ----------------------------------------------------------------------------
def target(host):
    try:
        for t in json.loads(urllib.request.urlopen(f'http://127.0.0.1:{PORT}/json', timeout=5).read()):
            if t.get('type') == 'page' and host in t.get('url', ''):
                return t
    except Exception:
        return None
    return None


def js(expr, host):
    t = target(host)
    if not t:
        return None
    try:
        ws = Q.WS(t['webSocketDebuggerUrl'])
    except Exception:
        return None
    try:
        r = ws.call('Runtime.evaluate', {'expression': expr, 'returnByValue': True, 'awaitPromise': True})
        return r.get('result', {}).get('value')
    except Exception:
        return None
    finally:
        try: ws.s.close()   # never left attached (an attached client changes how the engine reports the page)
        except Exception: pass


def shot(host, path):
    """The page as the engine renders it (DevTools Page.captureScreenshot: no screen involved)."""
    t = target(host)
    if not t:
        return
    ws = Q.WS(t['webSocketDebuggerUrl'])
    try:
        import base64
        path.write_bytes(base64.b64decode(ws.call('Page.captureScreenshot', {'format': 'png'})['data'])); log(f'     page picture: {path.relative_to(ROOT)}')
    except Exception as e:
        log(f'     page picture failed: {e}')
    finally:
        try: ws.s.close()
        except Exception: pass


# ---- input posted to our page's windows -----------------------------------------------------------------------------
def player_widget(h):
    """The render widget of the player page: the only one, or the left panel of the split (the session's order)."""
    widgets = children(h, 'Chrome_RenderWidgetHostHWND')
    return min(widgets, key=lambda w: rect(w)[0]) if widgets else None


def click_css(h, x, y, dpr):
    w = player_widget(h)
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
    w = player_widget(h)
    if not w:
        return False
    tid = user32.GetWindowThreadProcessId(w, None); gi = GUITHREADINFO(); gi.cbSize = ctypes.sizeof(gi)
    target_hwnd = gi.hwndFocus if user32.GetGUIThreadInfo(tid, ctypes.byref(gi)) and gi.hwndFocus else w
    scan = user32.MapVirtualKeyW(vk, 0)
    user32.PostMessageW(target_hwnd, WM_KEYDOWN, vk, 1 | (scan << 16))
    if char:
        user32.PostMessageW(target_hwnd, WM_CHAR, ord(char), 1 | (scan << 16))
    time.sleep(0.05); user32.PostMessageW(target_hwnd, WM_KEYUP, vk, 1 | (scan << 16) | (3 << 30))
    return True


# ---- recording: window / page rectangles every ~5 ms; on the screen, a small film every ~16 ms ----------------------
class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [('biSize', W.DWORD), ('biWidth', W.LONG), ('biHeight', W.LONG), ('biPlanes', W.WORD), ('biBitCount', W.WORD), ('biCompression', W.DWORD),
                ('biSizeImage', W.DWORD), ('a', W.LONG), ('b', W.LONG), ('c', W.DWORD), ('d', W.DWORD)]


class Recorder:
    FW, FH = 320, 180

    def __init__(self, h):
        self.h = h; self.stop = threading.Event(); self.samples = []; self.frames = []; self.mon = monitor(h)

    def _rects(self):
        on_desktop()
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
        self.threads = [threading.Thread(target=self._rects, daemon=True)] + ([] if desk else [threading.Thread(target=self._film, daemon=True)])
        for t in self.threads:
            t.start()
        return self

    def __exit__(self, *a):
        self.stop.set()
        for t in self.threads:
            t.join(2)

    @staticmethod
    def green_share(frame):
        """Share of the frame (BGRA) that is the player's green, in percent."""
        n = len(frame) // 4; g = 0
        for i in range(0, len(frame), 16):   # every 4th pixel
            b, gg, r = frame[i], frame[i + 1], frame[i + 2]
            if abs(r - GREEN[0]) < 24 and abs(gg - GREEN[1]) < 24 and abs(b - GREEN[2]) < 24:
                g += 1
        return round(100 * g * 4 / n)

    def save(self, name, local):
        with open(out_dir / f'{name}.timeline.txt', 'w', encoding='utf-8') as f:
            for s in self.samples:
                f.write(f'+{(s[0] - self.t0) * 1000:7.1f} ms  window {s[1]} {"maximized" if s[2] else "normal"}  pages {list(s[3])}\n')
        states = []
        if self.frames:
            w = self.FW
            # A BMP stores its rows bottom-up: frames in reverse order, each one's rows reversed (first frame on top).
            data = b''.join(b''.join(fr[r * w * 4:(r + 1) * w * 4] for r in range(self.FH - 1, -1, -1)) for _, fr in reversed(self.frames))
            head = struct.pack('<2sIHHI', b'BM', 54 + len(data), 0, 0, 54) + struct.pack('<IiiHHIIiiII', 40, w, self.FH * len(self.frames), 1, 32, 0, len(data), 2835, 2835, 0, 0)
            (out_dir / f'{name}.film.bmp').write_bytes(head + data)
            with open(out_dir / f'{name}.film.txt', 'w', encoding='utf-8') as f:
                for i, (t, fr) in enumerate(self.frames):
                    share = self.green_share(fr) if local else None
                    f.write(f'frame {i:3d} +{(t - self.t0) * 1000:7.1f} ms' + (f'  green {share} %' if local else '') + '\n')
                    if local and (not states or abs(states[-1][0] - share) > 2):
                        states.append([share, 1])
                    elif local:
                        states[-1][1] += 1
        return states

    def windows(self):
        out = []
        for s in self.samples:
            if not out or out[-1] != (s[1], s[2]):
                out.append((s[1], s[2]))
        return out


# ---- the browser's own trace ----------------------------------------------------------------------------------------
trace_pos = 0


def trace_check(what):
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
    check(len(groups) == 1, f'{what}: one FULLSCREEN switch in the browser trace ({len(groups)})')


def switch(h, site, name, act, want_fs, before, zoomed):
    st = lambda: js(site.state_js, site.host) or {}
    with Recorder(h) as rec:
        time.sleep(0.15); act()
        ok = wait(lambda: st().get('fs') == want_fs, 6, 0.05)
        time.sleep(1.2)
    states = rec.save(name, site.local)
    wins = rec.windows()
    log(f'     {name}: window ' + ' → '.join(f'{r} {"max" if z else "normal"}' for r, z in wins) + (f'; {len(rec.frames)} frames filmed' if rec.frames else ''))
    pages = []
    for s in rec.samples:
        if not pages or pages[-1] != s[3]:
            pages.append(s[3])
    log(f'     {name}: page windows ' + ' → '.join(str(list(p)) for p in pages))
    if states:
        log(f'     {name}: green share of the monitor per visible state: ' + ' → '.join(f'{s}% ×{n}' for s, n in states))
    check(bool(ok), f'{name}: the page is {"in" if want_fs else "out of"} fullscreen')
    check(len(wins) <= 2 and len({z for _, z in wins}) == 1, f'{name}: the window went straight to its new rectangle, show state kept ({len(wins)} states)')
    want = monitor(h) if want_fs else before
    check(rect(h) == want and bool(user32.IsZoomed(h)) == zoomed, f'{name}: window {rect(h)} == {want}, {"maximized" if zoomed else "windowed"}')
    trace_check(name)
    return bool(ok)


def run(a) -> int:
    global out_dir, desk, trace_pos
    site = {'youtube': YouTube, 'vimeo': Vimeo}.get(a.site, Site)()
    label = '-'.join([a.site, a.layout] + (['focus'] if a.focus else []) + (['split'] if a.split else []) + (['windowed'] if a.windowed else ['maximized']) + ([a.desktop] if a.desktop != 'screen' else []))
    out_dir = ROOT / 'build' / 'fullscreen-steps' / label; shutil.rmtree(out_dir, ignore_errors=True); out_dir.mkdir(parents=True)
    src = (ROOT / a.app).resolve()
    shutil.rmtree(APP, ignore_errors=True); APP.mkdir(parents=True)
    for f in src.iterdir():
        if f.name in ('profile', 'session', 'sessions', 'profiles', 'windows', 'prototype.log', 'downloads', 'bookmarks', 'settings', 'history', 'annotations', 'ui-language', 'profiles.txt'):
            continue
        (shutil.copytree(f, APP / f.name) if f.is_dir() else shutil.copy2(f, APP / f.name))
    groups = '1\n0 2 0 1 1 1\n' if a.split else '0\n'
    (APP / 'session').write_text(f'browser-session-2\n0\n2\n"{site.url}"\n"{NOTES}"\n{groups}', encoding='utf-8', newline='\n')
    (APP / 'settings').write_text('browser-settings 1\nrestore-tabs=1\n' + ('focus-mode=1\n' if a.focus else '') + f'tabs-position={a.layout}\n', encoding='utf-8', newline='\n')
    srv = socketserver.ThreadingTCPServer(('127.0.0.1', WEB), Handler); threading.Thread(target=srv.serve_forever, daemon=True).start()
    env = dict(os.environ); env['BROWSER_NO_ONBOARDING'] = '1'
    env['WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS'] = f'--remote-debugging-port={PORT} --remote-allow-origins=http://127.0.0.1:{PORT}'
    name = None
    if a.desktop == 'hidden':
        name = f'fsjump{os.getpid()}'; desk = user32.CreateDesktopW(name, None, None, 0, 0x10000000, None)
        if not desk or not user32.SetThreadDesktop(desk):
            log(f'FAIL hidden desktop ({ctypes.get_last_error()})'); return 1
    hproc, pid, job = start_in_job([str(APP / 'browser.exe'), f'--tab-layout={a.layout}'], env, name)
    log(f'run {label}: throwaway copy {APP.relative_to(ROOT)}, pid {pid}' + (f', desktop {name} (never displayed)' if name else ''))
    try:
        h = wait(lambda: main_window(pid), 25)
        check(bool(h), 'the throwaway copy starts')
        if not h:
            return 1
        front(h)
        if a.windowed == bool(user32.IsZoomed(h)):
            user32.PostMessageW(h, Shortcut, MaximizeCommand, 0); time.sleep(1.2)
        check(bool(wait(lambda: target(site.host), 30)), f'the player page is open ({site.url})')
        site.prepare()
        st = wait(lambda: (lambda s: s if s.get('ready') and s.get('button') else None)(js(site.state_js, site.host) or {}), 45, 0.5)
        check(bool(st), f'the player and its fullscreen button are there ({st or js(site.state_js, site.host)})')
        if not st:
            shot(site.host, out_dir / 'page-without-player.png')
            log('     buttons: ' + str(js("[...document.querySelectorAll('button')].slice(0, 40).map(b => { const r = b.getBoundingClientRect(); return [(b.getAttribute('aria-label') || b.innerText || b.className || '').slice(0, 40), Math.round(r.left), Math.round(r.top), Math.round(r.width)]; })", site.host)))
            return 1
        site.quiet(); time.sleep(0.5)
        before, zoomed = rect(h), bool(user32.IsZoomed(h))
        log(f'     window {before} {"maximized" if zoomed else "normal"}, monitor {monitor(h)}, page {st["w"]}x{st["h"]} CSS px x {st["dpr"]}')
        p = APP / 'prototype.log'; trace_pos = p.stat().st_size if p.exists() else 0
        state = lambda: js(site.state_js, site.host) or {}
        # 1. The player's button in, Échap out.
        front(h); s = state()
        switch(h, site, '1-button-in', lambda: click_css(h, *s['button'], s['dpr']), True, before, zoomed)
        site.quiet()

        def esc():
            key(h, 0x1B)
            if not wait(lambda: not state().get('fs'), 2, 0.05):
                log('     (posted Échap not seen: the browser\'s Échap command is posted instead)'); user32.PostMessageW(h, Shortcut, ExitFullscreenCommand, 0)
        switch(h, site, '2-escape-out', esc, False, before, zoomed)
        # 2. The F key in, the player's button out.
        front(h); js(site.focus_js, site.host); time.sleep(0.3)
        switch(h, site, '3-f-key-in', lambda: key(h, 0x46, 'f'), True, before, zoomed)
        site.quiet()

        def button_out():
            w = player_widget(h); r = rect(w)
            user32.PostMessageW(w, WM_MOUSEMOVE, 0, (((r[3] - r[1]) // 2) << 16) | ((r[2] - r[0]) // 2)); time.sleep(0.6)   # the player's controls come back
            s2 = state()
            if s2.get('button'):
                click_css(h, *s2['button'], s2['dpr'])
            else:
                log('     (no fullscreen button in fullscreen: the browser\'s Échap command is posted instead)'); user32.PostMessageW(h, Shortcut, ExitFullscreenCommand, 0)
        switch(h, site, '4-button-out', button_out, False, before, zoomed)
        return 0
    finally:
        try:
            h = main_window(pid)
            if h:
                user32.PostMessageW(h, WM_CLOSE, 0, 0)
            k32.WaitForSingleObject(hproc, 10000)
        except Exception:
            pass
        k32.CloseHandle(job)   # KILL_ON_JOB_CLOSE: every process of this copy ends here, nothing else
        k32.CloseHandle(hproc)
        srv.shutdown(); srv.server_close()
        (out_dir / 'report.txt').write_text('\n'.join(lines) + f'\n{failures} failures\n', encoding='utf-8')
        log(f'{failures} failures; report, timelines' + ('' if desk else ' and films') + f' in {out_dir.relative_to(ROOT)}')


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8')
    ap = argparse.ArgumentParser()
    ap.add_argument('--app', default='build/fs-jump'); ap.add_argument('--site', choices=('local', 'vimeo', 'youtube'), default='local')
    ap.add_argument('--layout', choices=('top', 'side'), default='top'); ap.add_argument('--focus', action='store_true')
    ap.add_argument('--split', action='store_true'); ap.add_argument('--windowed', action='store_true')
    ap.add_argument('--desktop', choices=('screen', 'hidden'), default='screen')
    args = ap.parse_args()
    code = run(args)
    sys.exit(1 if code or failures else 0)
