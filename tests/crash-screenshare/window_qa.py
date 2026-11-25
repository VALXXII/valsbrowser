#!/usr/bin/env python3
"""REAL-BROWSER QA for #121 « Onglet planté · A », « Partage d'écran, confirmation · A » and « Page qui ne répond plus · A »
(Paper p-V-0).

    python tests/crash-screenshare/window_qa.py --app build/121-crash-share                    # night: on screen, with pixels
    python tests/crash-screenshare/window_qa.py --app build/121-crash-share --hidden-desktop   # daytime: nothing on screen

(plain `python`: the captures need Pillow from the user site-packages.) Report: build/crash-screenshare-qa/report.txt.

On screen (NIGHT RUN ONLY): the browser opens on the user's desktop, takes the foreground and its windows are captured
(never while the owner uses the PC). --hidden-desktop runs the same journeys on a Windows desktop of its own
(CreateDesktop) that is never displayed: real windows, real engine, but nothing reaches the user's screen, mouse,
keyboard or foreground. Left to the night run there: the pixel comparisons and the hover colours (screen captures),
« Bloquer » / « Choisir… » (8, 10, 12: a popover that is not in the foreground ignores its buttons, SEC-22, and that
desktop is never in the foreground; 13-14 then start from the profile's list written as « Bloquer » would) and
« Recharger » of the frozen page (F4; F3's « Attendre » is Échap there). A DevTools client attached to a page keeps the
engine from reporting it frozen: the script never stays connected while it waits for the report.

Drives ONLY its own throwaway copy of a dev build (build/crash-screenshare-qa-app: payload copied, portable data next to
the exe, a fresh profile every run). Pages come from this script's server on 127.0.0.1 (never a real site). This
process puts itself in a job object with KILL_ON_JOB_CLOSE first: the browser, its engine and everything started here
die with it, even when interrupted. The WebView2 remote debugging port (loopback) is enabled for this copy only, through
the process environment, to read page state. Clicks inside pages and popovers are window messages posted to OUR windows
(a real user activation for the engine); browser commands are the app's own shortcut messages; keys for the engine's
picker go to windows of THIS copy's engine processes only (never another program's window). A tab « crashes » the way
a real one does from outside: its renderer processes (msedgewebview2 --type=renderer of THIS copy's profile only) are
terminated, as the task manager or Windows under low memory would.

Each tab is followed by its own DevTools target (its id lasts as long as the tab, across crashes and documents) and every
page question has a short timeout: no check waits on another tab (20:00 run, 24/11: questions to every tab in turn
outlasted the 10 s checks of « Recharger », F5 and Back while the browser had done them, see prototype.log).

Checks, each against the board or the engine's normal behaviour:
  crash  1. our page replaces the engine's (title « Cette page a cessé de fonctionner »), the address keeps the crashed
            address, the window title follows; its page area (the window sized so the page area is the board's,
            1172 x 730 DIP) matches the Paper board (compare.py: the board says lemonde.fr, the test site 127.0.0.1:5207:
            the two text lines holding the site's name are checked by top and centre, the rest by pixels and boxes);
         2. « Recharger » brings the site back (same address, the site's own document);
         3. F5 on our page (the toolbar reload) does the same;
         4. Back from our page goes to the page BEFORE the crashed one; the crashed entry stays reachable with Forward;
         5. a background tab that crashed (twice) shows our page when selected;
         6. « Fermer l'onglet » closes that tab only.
  share  7. a click on the site's « Présenter » (getDisplayMedia) shows OUR popover first, 121/88 DIP from the window's
            client origin (Paper 1LMF-0 position), matching the Paper export (the title's site name aside);
         8. « Bloquer »: the page gets NotAllowedError « Permission denied »; asking again shows the popover again;
         9. Échap and the close button refuse the same way;
        10. « Choisir… » opens the engine's own picker, our popover does not come back; cancelling the picker (Échap to
            the engine's own window that holds the keyboard) gives the page the engine's refusal;
        11. a navigation while the popover is open closes it and refuses the request;
        12-14. « Toujours bloquer ce site » + « Bloquer »: kept in this profile's list, the next request is refused
            without the popover, listed in Réglages › Confidentialité › Autorisations des sites (« 127.0.0.1:5207 »,
            « Partage d'écran bloqué »; on its page « Partage d'écran » [Bloqué]) and removable there with « Retirer les
            autorisations » (then the popover asks again).
  states S. the crashed-tab page buttons: hover / pressed colours of « Recharger » and « Fermer l'onglet » (Paper 1O26-0).
  frozen F1. a frozen page (a fixture button loops) reported by the engine: our window centred in the browser window
            over the page veiled black 50 %, matching the Paper export (1O1M-0, the title's site name aside);
         F2. the page runs again: the window closes by itself;
         F3. « Attendre »: closed, and back by itself 15 s later while the page stays frozen (the engine reports a
            freeze only once: 20:00 run 24/11, it never came back);
         F4. « Recharger »: the tab's renderer ends and the page reloads, no crashed-tab page.
Captures, diffs and the report go to build/crash-screenshare-qa/.
"""
from __future__ import annotations
import argparse
import ctypes
import ctypes.wintypes as W
import http.server
import json
import os
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
import sandbox_qa as Q   # noqa: E402  (CDP WebSocket client)

APP = ROOT / 'build' / 'crash-screenshare-qa-app'
OUT = ROOT / 'build' / 'crash-screenshare-qa'
PORT, WEB = 9351, 5207
BASE = f'http://127.0.0.1:{WEB}'
A_URL, B_URL, M_URL = f'{BASE}/accueil', f'{BASE}/planete/', f'{BASE}/meet/xmc-pqrt-bvn'
CRASH_TITLE = 'Cette page a cessé de fonctionner'
user32 = ctypes.WinDLL('user32', use_last_error=True)
kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)
user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))   # per-monitor v2: physical pixels everywhere
user32.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
Shortcut, SelectTabBase = 0x8001, 3010
WM_MOUSEMOVE, WM_LBUTTONDOWN, WM_LBUTTONUP, WM_KEYDOWN, WM_KEYUP, WM_CLOSE = 0x200, 0x201, 0x202, 0x100, 0x101, 0x10
HIDDEN = {'name': None, 'hdesk': None}   # --hidden-desktop: the desktop this run lives on
TABS: dict[str, str] = {}               # 'site' / 'meet' -> the tab's DevTools target id
lines: list[str] = []
failures = 0


def log(text):
    print(text, flush=True); lines.append(text)


def check(ok, what):
    global failures
    log(('ok   ' if ok else 'FAIL ') + what)
    if not ok:
        failures += 1
    return ok


def hidden():
    return HIDDEN['name'] is not None


# ---- job object: everything started here dies with this script ------------------------------------------------------
class _IO(ctypes.Structure):
    _fields_ = [(n, ctypes.c_ulonglong) for n in ('r', 'w', 'o', 'rb', 'wb', 'ob')]


class _Basic(ctypes.Structure):
    _fields_ = [('PerProcessUserTimeLimit', ctypes.c_longlong), ('PerJobUserTimeLimit', ctypes.c_longlong), ('LimitFlags', W.DWORD),
                ('MinimumWorkingSetSize', ctypes.c_size_t), ('MaximumWorkingSetSize', ctypes.c_size_t), ('ActiveProcessLimit', W.DWORD),
                ('Affinity', ctypes.c_size_t), ('PriorityClass', W.DWORD), ('SchedulingClass', W.DWORD)]


class _Extended(ctypes.Structure):
    _fields_ = [('Basic', _Basic), ('Io', _IO), ('ProcessMemoryLimit', ctypes.c_size_t), ('JobMemoryLimit', ctypes.c_size_t),
                ('PeakProcessMemoryUsed', ctypes.c_size_t), ('PeakJobMemoryUsed', ctypes.c_size_t)]


def enter_job():
    kernel32.CreateJobObjectW.restype = W.HANDLE
    job = kernel32.CreateJobObjectW(None, None)
    info = _Extended(); info.Basic.LimitFlags = 0x2000   # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
    ok = job and kernel32.SetInformationJobObject(W.HANDLE(job), 9, ctypes.byref(info), ctypes.sizeof(info)) \
        and kernel32.AssignProcessToJobObject(W.HANDLE(job), W.HANDLE(-1))   # GetCurrentProcess(): its -1 passed as a 32-bit int is not the pseudo handle
    if not ok:
        sys.exit('job object unavailable: refusing to start the browser without it')
    return job   # the handle stays open for the whole run; Windows closes it (and kills the job) when we exit


# ---- a desktop of its own (--hidden-desktop) ------------------------------------------------------------------------
class STARTUPINFO(ctypes.Structure):
    _fields_ = [('cb', W.DWORD), ('lpReserved', W.LPWSTR), ('lpDesktop', W.LPWSTR), ('lpTitle', W.LPWSTR), ('dwX', W.DWORD), ('dwY', W.DWORD),
                ('dwXSize', W.DWORD), ('dwYSize', W.DWORD), ('dwXCountChars', W.DWORD), ('dwYCountChars', W.DWORD), ('dwFillAttribute', W.DWORD),
                ('dwFlags', W.DWORD), ('wShowWindow', W.WORD), ('cbReserved2', W.WORD), ('lpReserved2', ctypes.c_void_p),
                ('hStdInput', W.HANDLE), ('hStdOutput', W.HANDLE), ('hStdError', W.HANDLE)]


class PROCESS_INFORMATION(ctypes.Structure):
    _fields_ = [('hProcess', W.HANDLE), ('hThread', W.HANDLE), ('dwProcessId', W.DWORD), ('dwThreadId', W.DWORD)]


def enter_hidden_desktop():
    """A new desktop of the session, never displayed; this thread (all window work of this script) moves onto it."""
    user32.CreateDesktopW.restype = W.HANDLE
    user32.CreateDesktopW.argtypes = [W.LPCWSTR, W.LPCWSTR, ctypes.c_void_p, W.DWORD, W.DWORD, ctypes.c_void_p]
    user32.SetThreadDesktop.argtypes = [W.HANDLE]
    name = f'crash-qa-{os.getpid()}'
    hdesk = user32.CreateDesktopW(name, None, None, 0, 0x10000000, None)   # GENERIC_ALL
    if not hdesk or not user32.SetThreadDesktop(hdesk):
        sys.exit(f'hidden desktop unavailable ({ctypes.get_last_error()}): refusing to run on the user\'s desktop in the daytime')
    HIDDEN['name'], HIDDEN['hdesk'] = name, hdesk


class Proc:
    """A process started on this run's desktop (CreateProcess with lpDesktop); in this script's job like any child."""
    def __init__(self, cmd, cwd, env, flags=0):
        si = STARTUPINFO(); si.cb = ctypes.sizeof(si)
        if hidden():
            si.lpDesktop = 'WinSta0\\' + HIDDEN['name']
        pi = PROCESS_INFORMATION()
        block = ''.join(f'{k}={v}\0' for k, v in sorted(env.items(), key=lambda kv: kv[0].upper())) + '\0'
        line = ctypes.create_unicode_buffer(subprocess.list2cmdline([str(c) for c in cmd]))
        if not kernel32.CreateProcessW(None, line, None, None, False, 0x00000400 | flags, ctypes.create_unicode_buffer(block), str(cwd), ctypes.byref(si), ctypes.byref(pi)):
            raise ctypes.WinError(ctypes.get_last_error())
        kernel32.CloseHandle(pi.hThread)
        self.pid, self.handle = pi.dwProcessId, pi.hProcess

    def wait(self, seconds):
        return kernel32.WaitForSingleObject(W.HANDLE(self.handle), int(seconds * 1000)) == 0

    def kill(self):
        kernel32.TerminateProcess(W.HANDLE(self.handle), 1)


def user_desktop_windows(pids):
    """Windows of these processes on the desktop the user sees (must be none with --hidden-desktop)."""
    user32.OpenInputDesktop.restype = W.HANDLE
    hd = user32.OpenInputDesktop(0, False, 0x0040)   # DESKTOP_ENUMERATE
    found = []
    if not hd:
        return found

    @ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    def cb(h, _):
        p = W.DWORD(); user32.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value in pids:
            found.append(class_name(h))
        return True
    user32.EnumDesktopWindows(W.HANDLE(hd), cb, 0); user32.CloseDesktop(W.HANDLE(hd))
    return found


# ---- fixture site ---------------------------------------------------------------------------------------------------
def page(title, body, script=''):
    return (f'<!doctype html><html lang="fr"><head><meta charset="utf-8"><title>{title}</title><style>html,body{{margin:0;background:#f4f4f2;'
            f'font:16px system-ui}}#present{{position:fixed;left:40px;top:40px;width:160px;height:40px}}#out{{position:fixed;left:40px;top:100px}}</style></head>'
            f'<body>{body}<script>{script}</script></body></html>').encode('utf-8')


MEET_SCRIPT = ('window.results=[];document.getElementById("present").addEventListener("click",()=>{'
               'navigator.mediaDevices.getDisplayMedia({video:true}).then(s=>{s.getTracks().forEach(t=>t.stop());results.push("granted")},'
               'e=>results.push(e.name+": "+e.message)).then(()=>{document.getElementById("out").textContent=results.join(" | ")})});')
# A button that freezes the page: for good (/freeze) or for 40 s (/freeze40).
FREEZE_SCRIPT = ('document.getElementById("present").addEventListener("click",()=>{const end=location.pathname.startsWith("/freeze40")?Date.now()+40000:Infinity;'
                 'setTimeout(()=>{while(Date.now()<end){}},50)});')


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path.startswith('/freeze'):
            body = page('Planète', '<button id="present">Figer la page</button><p id="out">Page locale de test (figée au clic).</p>', FREEZE_SCRIPT)
        elif self.path.startswith('/meet'):
            body = page('Point hebdo', '<button id="present">Présenter</button><p id="out"></p>', MEET_SCRIPT)
        elif self.path.startswith('/planete'):
            body = page('Planète', '<h1>Planète</h1><p>Page locale de test (B).</p>')
        else:
            body = page('Accueil', '<h1>Accueil</h1><p>Page locale de test (A).</p>')
        self.send_response(200); self.send_header('Content-Type', 'text/html; charset=utf-8'); self.send_header('Cache-Control', 'no-store')
        self.end_headers(); self.wfile.write(body)

    def log_message(self, *a):
        pass


# ---- CDP (loopback, this copy only) -----------------------------------------------------------------------------------
def targets():
    try:
        return [t for t in json.loads(urllib.request.urlopen(f'http://127.0.0.1:{PORT}/json', timeout=5).read()) if t.get('type') == 'page']
    except Exception:
        return []


def target(pred):
    for t in targets():
        if pred(t):
            return t
    return None


def ws(t, timeout=5.0):
    if not t:
        return None
    w = Q.WS(t['webSocketDebuggerUrl']); w.s.settimeout(timeout)
    return w


_slow = {'n': 0}


def evaluate(t, js, timeout=4.0):
    """One page question with a short timeout (a page that does not answer must not hold the other checks)."""
    if not t:
        return None
    t0 = time.time()
    try:
        w = ws(t, timeout)
        try:
            r = w.call('Runtime.evaluate', {'expression': js, 'returnByValue': True, 'awaitPromise': True})
        finally:
            w.s.close()
        return r.get('result', {}).get('value')
    except Exception as e:
        if time.time() - t0 > 2 and _slow['n'] < 8:
            _slow['n'] += 1; log(f'     (DevTools: no answer from {t.get("url", "")[:50]!r} in {time.time() - t0:.1f} s)')
        return f'<{e}>'


def wait(pred, seconds=8.0):
    end = time.time() + seconds
    while time.time() < end:
        v = pred()
        if v:
            return v
        time.sleep(0.2)
    return pred()


def tab(name):
    tid = TABS.get(name)
    return target(lambda t: t.get('id') == tid) if tid else None


def is_crash_page(t):
    return bool(t) and t.get('url', '').startswith(('about:blank', 'data:')) and evaluate(t, "(document.querySelector('h1')||{}).textContent") == CRASH_TITLE


def crash_in(name):
    t = tab(name)
    return t if is_crash_page(t) else None


def site_in(name, url, title):
    """The tab shows the site itself again: its address, and a live document with the site's own title."""
    t = tab(name)
    return t if t and t.get('url') == url and evaluate(t, 'document.title') == title else None


def on_screen(name):
    return evaluate(tab(name), 'document.visibilityState') == 'visible'


def where(name):
    t = tab(name)
    return f'{name}: {t.get("url", "")[:60] if t else "gone"}'


def cdp_mouse(t, kind, x, y):
    w = ws(t)
    if w:
        w.call('Input.dispatchMouseEvent', {'type': kind, 'x': x, 'y': y, 'button': 'left' if kind != 'mouseMoved' else 'none', 'clickCount': 1 if kind != 'mouseMoved' else 0})
        w.s.close()


# ---- windows --------------------------------------------------------------------------------------------------------
def class_name(h):
    n = ctypes.create_unicode_buffer(128); user32.GetClassNameW(h, n, 128); return n.value


def top_windows(pid=None, cls=None, pids=None):
    out = []

    @ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    def cb(h, _):
        p = W.DWORD(); user32.GetWindowThreadProcessId(h, ctypes.byref(p))
        name = class_name(h)
        if user32.IsWindowVisible(h) and (pid is None or p.value == pid) and (pids is None or p.value in pids) and (cls is None or name == cls):
            out.append((h, name, p.value))
        return True
    user32.EnumWindows(cb, 0)
    return out


def main_window(pid):
    w = top_windows(pid, 'LisiereBrowser')
    return w[0][0] if w else None


def rect(h):
    r = W.RECT(); user32.GetWindowRect(h, ctypes.byref(r)); return (r.left, r.top, r.right, r.bottom)


def client_origin(h):
    p = W.POINT(0, 0); user32.ClientToScreen(h, ctypes.byref(p)); return (p.x, p.y)


def title(h):
    b = ctypes.create_unicode_buffer(512); user32.GetWindowTextW(h, b, 512); return b.value


def address_text(h):
    """The address field's text. GetWindowText on another process's control returns its creation caption ('' here),
    by design (night 24/11: « adresse vide » although the bar showed the address): WM_GETTEXT reads the control."""
    e = user32.FindWindowExW(h, None, 'Edit', None)
    if not e:
        return None
    b = ctypes.create_unicode_buffer(4096); got = ctypes.c_size_t(0)
    user32.SendMessageTimeoutW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM, W.UINT, W.UINT, ctypes.POINTER(ctypes.c_size_t)]
    if not user32.SendMessageTimeoutW(e, 0x000D, 4096, ctypes.cast(b, ctypes.c_void_p).value, 0x0002, 2000, ctypes.byref(got)):   # WM_GETTEXT, SMTO_ABORTIFHUNG
        return None
    return b.value


def page_host_window(h):
    """The visible WebView host window under our window (Chrome_WidgetWin_*): (hwnd, screen rect)."""
    found = []

    @ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    def cb(c, _):
        if user32.GetParent(c) == h and user32.IsWindowVisible(c) and class_name(c).startswith('Chrome_WidgetWin'):
            found.append((c, rect(c)))
        return True
    user32.EnumChildWindows(h, cb, 0)
    return found[0] if found else (None, None)


def page_host(h):
    return page_host_window(h)[1]


def render_widget(h):
    found = []

    @ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    def cb(c, _):
        if class_name(c) == 'Chrome_RenderWidgetHostHWND' and user32.IsWindowVisible(c):
            found.append(c)
        return True
    user32.EnumChildWindows(h, cb, 0)
    return found[0] if found else None


def click_window(hwnd, x, y):
    lp = (int(y) << 16) | (int(x) & 0xFFFF)
    user32.PostMessageW(hwnd, WM_MOUSEMOVE, 0, lp); time.sleep(0.05)
    user32.PostMessageW(hwnd, WM_LBUTTONDOWN, 1, lp); time.sleep(0.05)
    user32.PostMessageW(hwnd, WM_LBUTTONUP, 0, lp)


def click_in_page(h, t, selector):
    """Posted click at the centre of `selector` (CSS px from CDP) on the render widget of the tab on screen: a real user
    activation. `t` must be that tab (the render widget shown is always the tab on screen's)."""
    box = evaluate(t, f'(()=>{{const r=document.querySelector({json.dumps(selector)}).getBoundingClientRect();return [r.x+r.width/2,r.y+r.height/2,devicePixelRatio]}})()')
    rw = render_widget(h)
    if not isinstance(box, list) or not rw:
        return False
    click_window(rw, box[0] * box[2], box[1] * box[2]); return True


def key(hwnd, vk):
    user32.PostMessageW(hwnd, WM_KEYDOWN, vk, 1); time.sleep(0.1); user32.PostMessageW(hwnd, WM_KEYUP, vk, 0xC0000001)


def front(h):
    user32.ShowWindow(h, 9)
    if hidden():   # nothing to take: no input reaches this desktop, and no key may be sent to the user's own desktop
        return
    user32.keybd_event(0x12, 0, 0, 0); user32.keybd_event(0x12, 0, 2, 0)   # Alt tap: lets us hand the foreground to OUR window
    user32.SetForegroundWindow(h); time.sleep(0.3)


def post(h, cmd):
    user32.PostMessageW(h, Shortcut, cmd, 0)


def dpi(h):
    return user32.GetDpiForWindow(h)


def capture(h):
    if hidden():
        return None
    img, _ = Q.print_window(h)
    return img.convert('RGB') if img else None


def compare(capture_png, paper_png, diff_png, *extra):
    r = subprocess.run([sys.executable, str(HERE / 'compare.py'), str(capture_png), str(paper_png), '--out', str(diff_png), *[str(e) for e in extra]], capture_output=True, text=True)
    for line in r.stdout.strip().splitlines():
        log('     ' + line)
    return r.returncode == 0


def monitors():
    out = []
    MonitorEnumProc = ctypes.WINFUNCTYPE(W.BOOL, W.HMONITOR, W.HDC, ctypes.POINTER(W.RECT), W.LPARAM)
    shcore = ctypes.windll.shcore

    def cb(hm, _hdc, r, _):
        x, y = W.UINT(), W.UINT(); shcore.GetDpiForMonitor(hm, 0, ctypes.byref(x), ctypes.byref(y))
        out.append(((r.contents.left, r.contents.top, r.contents.right, r.contents.bottom), x.value)); return True
    user32.EnumDisplayMonitors(None, None, MonitorEnumProc(cb), 0)
    return out


def paper_page_size(d):
    from PIL import Image
    return Image.open(HERE / 'paper' / ('crash-page-1x.png' if d == 96 else 'crash-page-1.25x.png')).size


def place(h):
    """On a 100 % monitor when there is one (Paper 1x), else 125 % (Paper 1.25x); low on it (floating widgets above)."""
    mons = monitors()
    pick = next((m for m in mons if m[1] == 96), None) or next((m for m in mons if m[1] == 120), None)
    if not pick:
        log(f'     no 100 % or 125 % monitor ({mons}): the pixel comparison is skipped'); return None
    (l, t, r, b), d = pick
    k = d / 96.0; w, hh = round(1180 * k), round(815 * k)
    x, y = l + round(60 * k), max(t + 20, b - hh - round(240 * k))
    user32.ShowWindow(h, 9); user32.SetWindowPos(h, None, x, y, w, hh, 0x0004 | 0x0010); time.sleep(1.2)
    # Moving onto a monitor of another scale sends WM_DPICHANGED, whose suggested rect rescales the window (80 % from
    # 125 % to 100 %: 944 x 652 instead of 1180 x 815, night run 24/11): size it again once it is there.
    user32.SetWindowPos(h, None, x, y, w, hh, 0x0004 | 0x0010); time.sleep(0.8)
    log(f'     window on the {d}-DPI monitor {pick[0]}: {rect(h)}')
    return d


def fit_page_area(h, d):
    """The window sized so its page area is the board's (1172 x 730 DIP): the boards' window frame and Browser's differ
    by a few pixels (20:00 run 24/11: 1176 x 732 for a 1180 x 815 window), and the crashed-tab page is centred in it."""
    if not d:
        return
    want = paper_page_size(d)
    for _ in range(3):
        host = page_host(h)
        if not host:
            time.sleep(0.5); continue
        dw, dh = want[0] - (host[2] - host[0]), want[1] - (host[3] - host[1])
        if not dw and not dh:
            break
        r = rect(h); user32.SetWindowPos(h, None, 0, 0, (r[2] - r[0]) + dw, (r[3] - r[1]) + dh, 0x0002 | 0x0004 | 0x0010); time.sleep(0.8)
    host = page_host(h)
    log(f'     page area {(host[2] - host[0], host[3] - host[1]) if host else None} (board {want}), window {rect(h)}')


# ---- engine processes of THIS copy ----------------------------------------------------------------------------------
def engine_processes():
    """[(pid, command line)] of msedgewebview2.exe processes of this copy's profile only (command line names APP)."""
    ps = ("Get-CimInstance Win32_Process -Filter \"Name='msedgewebview2.exe'\" | ForEach-Object { '' + $_.ProcessId + '|' + $_.CommandLine }")
    r = subprocess.run(['powershell', '-NoProfile', '-Command', ps], capture_output=True, text=True, timeout=60)
    mine = str(APP).lower()
    return [(int(line.split('|', 1)[0]), line.split('|', 1)[1]) for line in r.stdout.splitlines() if '|' in line and mine in line.lower()]


def kill_renderers():
    """Terminates the renderer processes of this copy's profile only."""
    pids = [pid for pid, cmd in engine_processes() if '--type=renderer' in cmd]
    for pid in pids:
        h = kernel32.OpenProcess(0x0001, False, pid)
        if h:
            kernel32.TerminateProcess(h, 1); kernel32.CloseHandle(h)
    return pids


class GUITHREADINFO(ctypes.Structure):
    _fields_ = [('cbSize', W.DWORD), ('flags', W.DWORD), ('hwndActive', W.HWND), ('hwndFocus', W.HWND), ('hwndCapture', W.HWND),
                ('hwndMenuOwner', W.HWND), ('hwndMoveSize', W.HWND), ('hwndCaret', W.HWND), ('rcCaret', W.RECT)]


def engine_root(h, engine):
    """The engine's own window inside ours (a Chrome_WidgetWin of one of `engine` pids): the engine draws its dialogs,
    the screen picker among them, inside it (no window of their own, 20:00 run 24/11: none appeared)."""
    found = []

    @ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    def cb(c, _):
        p = W.DWORD(); user32.GetWindowThreadProcessId(c, ctypes.byref(p))
        if p.value in engine and user32.IsWindowVisible(c) and class_name(c).startswith('Chrome_WidgetWin'):
            found.append(c)
        return True
    user32.EnumChildWindows(h, cb, 0)
    return found[0] if found else None


def keyboard_window(h, engine):
    """The window that holds the keyboard in the engine's UI thread (the thread of its window inside ours): (hwnd, pid)."""
    root = engine_root(h, engine)
    if not root:
        return None, 0
    tid = user32.GetWindowThreadProcessId(root, None)
    g = GUITHREADINFO(); g.cbSize = ctypes.sizeof(g)
    if not user32.GetGUIThreadInfo(tid, ctypes.byref(g)):
        return None, 0
    w = g.hwndFocus or g.hwndActive
    p = W.DWORD()
    if w:
        user32.GetWindowThreadProcessId(w, ctypes.byref(p))
    return w, p.value


# ---- the run --------------------------------------------------------------------------------------------------------
def setup_tabs():
    """The two restored tabs (accueil, then the meet page): their DevTools targets, kept for the whole run."""
    ta = wait(lambda: target(lambda t: t['url'].startswith((A_URL, B_URL))), 25)
    tm = wait(lambda: target(lambda t: t['url'] == M_URL), 25)
    if ta:
        TABS['site'] = ta['id']
    if tm:
        TABS['meet'] = tm['id']
    return check(bool(ta and tm), 'the two restored tabs are loaded')


def crash_steps(h, pid, d):
    A, B, M = A_URL, B_URL, M_URL
    w = ws(tab('site')); w.call('Page.navigate', {'url': B}); w.s.close()
    check(bool(wait(lambda: site_in('site', B, 'Planète'), 10)), 'tab 1: A then B (history of two entries)')
    front(h)
    # 1. crash -> our page in the tab on screen
    killed = kill_renderers(); log(f'     renderers terminated: {killed}')
    ct = wait(lambda: crash_in('site'), 15)
    check(bool(ct) and on_screen('site'), '1 crash: our page « Cette page a cessé de fonctionner » replaces the engine\'s (tab on screen)')
    check(wait(lambda: address_text(h) == B, 5), f'1 crash: the address keeps the crashed address ({address_text(h)!r})')
    check(title(h).startswith(f'127.0.0.1:{WEB} '), f'1 crash: the tab / window title keeps the site name, as the board\'s tab ({title(h)!r})')
    time.sleep(0.6)   # entrance settled
    img, host = capture(h), page_host(h)
    if hidden():
        log('     1 pixels: night run only (screen captures)')
    elif img and host and d:
        ox, oy = rect(h)[0], rect(h)[1]
        crop = img.crop((host[0] - ox, host[1] - oy, host[2] - ox, host[3] - oy)); OUT.mkdir(parents=True, exist_ok=True)
        crop.save(OUT / 'crash-page.png'); img.save(OUT / 'crash-window.png')
        ref = HERE / 'paper' / ('crash-page-1x.png' if d == 96 else 'crash-page-1.25x.png')
        log(f'     page area {crop.size} vs Paper {ref.name} (the board says lemonde.fr, this page 127.0.0.1:{WEB})')
        check(compare(OUT / 'crash-page.png', ref, OUT / 'crash-page-diff.png', '--name-band', '2:center', '--name-band', '3:center'),
              '1 crash: the page area matches the Paper board (compare.py)')
    else:
        check(False, '1 crash: capture of the page area')
    # 2. « Recharger »
    check(click_in_page(h, crash_in('site'), '#reload'), '2 « Recharger » clicked')
    check(bool(wait(lambda: site_in('site', B, 'Planète'), 10)), f'2 « Recharger » brings the site back ({where("site")})')
    check(wait(lambda: address_text(h) == B, 5), f'2 the address is the site\'s ({address_text(h)!r})')
    # 3. F5 on our page
    kill_renderers(); check(bool(wait(lambda: crash_in('site'), 15)), '3 crashed again: our page')
    post(h, ord('R'))
    check(bool(wait(lambda: site_in('site', B, 'Planète'), 10)), f'3 F5 / toolbar reload on our page brings the site back ({where("site")})')
    # 4. Back skips the crashed entry; Forward reaches it again
    kill_renderers(); check(bool(wait(lambda: crash_in('site'), 15)), '4 crashed again: our page')
    post(h, ord('B'))
    check(bool(wait(lambda: site_in('site', A, 'Accueil'), 10)), f'4 Back goes to the page BEFORE the crashed one (A) ({where("site")})')
    post(h, ord('F'))
    check(bool(wait(lambda: site_in('site', B, 'Planète'), 10)), f'4 Forward reaches the crashed address again ({where("site")})')
    # 5. a background tab that crashed, then crashed AGAIN while on our page (every renderer ended twice: night 24/11
    #    it came back as the site): selected -> still our page
    kill_renderers(); time.sleep(2); kill_renderers(); time.sleep(2)
    post(h, SelectTabBase + 1)
    check(bool(wait(lambda: address_text(h) == M and crash_in('meet') and on_screen('meet'), 10)), f'5 a background tab that crashed (twice) shows our page when selected ({address_text(h)!r}, {where("meet")})')
    check(click_in_page(h, crash_in('meet'), '#reload') and bool(wait(lambda: site_in('meet', M, 'Point hebdo'), 10)), f'5 « Recharger » brings it back ({where("meet")})')
    # 6. « Fermer l'onglet » on tab 1 (crashed, selected first)
    post(h, SelectTabBase + 0); time.sleep(0.8)
    kill_renderers(); ct = wait(lambda: crash_in('meet') and on_screen('site') and crash_in('site'), 15)   # both tabs crashed
    check(bool(ct), f'6 crashed again: our page in both tabs ({where("site")}, {where("meet")})')
    check(click_in_page(h, ct, '#close'), '6 « Fermer l\'onglet » clicked')
    check(bool(wait(lambda: not tab('site') and crash_in('meet'), 10)), f'6 that tab only is closed, the other crashed tab kept ({len(targets())} page(s) left)')
    if not tab('site'):
        TABS.pop('site', None)


def popover(pid):
    w = top_windows(pid, 'BrowserModalDialog')
    return w[0][0] if w else None


def results(t):
    v = evaluate(t, 'window.results')
    return v if isinstance(v, list) else []


# Popover geometry (DIP inside the window), from the layout (padding 17 = 16 inside the 1 px border, gap 16): head
# 17..41, request row + note 57..129, « Toujours bloquer ce site » row 145..163, buttons 179..211.
POP_BLOCK, POP_CHOOSE, POP_CLOSE, POP_CHECK = (91, 195), (248, 195), (311, 27), (100, 154)
# Frozen-page window (400 x 210, padding 21): buttons right-aligned at 157..189.
FROZEN_WAIT, FROZEN_RELOAD = (245, 173), (335, 173)


def click_dip(hwnd, point):
    k = dpi(hwnd) / 96.0; click_window(hwnd, point[0] * k, point[1] * k)


def grab(box):
    from PIL import ImageGrab
    return ImageGrab.grab(bbox=box, all_screens=True).convert('RGB')


def mix(c, a, under):
    return tuple(round(a * x + (1 - a) * y) for x, y in zip(c, under))


def near(a, b, tol=6):
    return a is not None and b is not None and all(abs(int(x) - int(y)) <= tol for x, y in zip(a, b))


def meet_on_screen(h):
    """The meet tab selected (index 0 once tab 1 is closed) and showing the meet page itself."""
    post(h, SelectTabBase + (1 if tab('site') else 0)); time.sleep(0.8)
    if crash_in('meet'):   # it crashed with the others: back to the site first
        click_in_page(h, crash_in('meet'), '#reload')
    return wait(lambda: site_in('meet', M_URL, 'Point hebdo') if on_screen('meet') else None, 10)


def cancel_engine_picker(h, pid, before, tm, n):
    """« Choisir… » let the engine show its own picker. Where it is, and Échap for it — only to windows of THIS copy's
    engine processes (20:00 run 24/11: the first new window of ANY program was taken, another browser's status bubble,
    and the keys went there; the picker stayed open and refused the next requests). The engine draws the picker inside
    its own window in ours (no window of its own); a top-level one is looked for too. Returns (seen, how cancelled)."""
    engine = {p for p, _ in engine_processes()}
    time.sleep(1.0)
    new = [x for x in top_windows(pids=engine) if x[0] not in before]
    root = engine_root(h, engine); kw, kp = keyboard_window(h, engine)
    log(f'     engine picker: new top-level windows of this copy\'s engine {[(x[1], x[2]) for x in new]}; the engine\'s window in ours '
        f'{class_name(root) if root else None}; keyboard in its thread {(class_name(kw), kp) if kw else None}')
    names = uia(pid, extra_pids=sorted(engine))
    log('     UI Automation after « Choisir… »: ' + names[:700])
    seen = bool(new) or any(w in names for w in ('Annuler', 'Cancel', 'Partager', 'Share'))
    if not hidden():
        img, _ = Q.print_window(new[0][0] if new else h)
        if img:
            img.convert('RGB').save(OUT / 'engine-picker.png')
    tries = [(x[0], f'Échap to its window {x[1]}') for x in new]
    if kw and kp in engine:
        tries.append((kw, f'Échap to the engine\'s keyboard window {class_name(kw)}'))
    if root and root != kw:
        tries.append((root, f'Échap to the engine\'s window {class_name(root)}'))
    for w_, label in tries:
        key(w_, 0x1B)
        if wait(lambda: len(results(tm)) > n, 3):
            return seen, label
    if root:   # its « Annuler », as a user clicks it: the picker sits centred at the top of the page, the button at its
        # bottom right (engine_probe.py picker, 24/11: « Cancel » at 908 x 523 of a 1172 x 730 page; a posted click there
        # closed it and the page got NotAllowedError « Permission denied by user »)
        r = rect(root); k = dpi(h) / 96.0
        x, y = ((r[2] - r[0]) / k / 2 + 329) * k, 523 * k
        click_window(root, x, y)
        if wait(lambda: len(results(tm)) > n, 3):
            return seen, f'a click on its « Annuler » ({round(x)}, {round(y)} of the engine\'s window)'
    out = uia(pid, 'Annuler', extra_pids=sorted(engine))
    log('     ' + out[:160])
    if out.startswith('INVOKED') and wait(lambda: len(results(tm)) > n, 3):
        return seen, 'its « Annuler » button (UI Automation)'
    return seen, ''


def share_steps(h, pid, d):
    tm = meet_on_screen(h)
    if not check(bool(tm), 'meet page ready (tab on screen)'):
        return
    w = ws(tm); w.call('Page.reload', {}); w.s.close(); time.sleep(2); tm = wait(lambda: site_in('meet', M_URL, 'Point hebdo'), 8)
    front(h)
    # 7. our popover first
    check(click_in_page(h, tm, '#present'), '7 « Présenter » clicked')
    pop = wait(lambda: popover(pid), 6)
    if not check(bool(pop), '7 our popover appears before any engine picker'):
        return
    time.sleep(0.5)
    co, pr = client_origin(h), rect(pop); k = dpi(h) / 96.0
    dx, dy = (pr[0] - co[0]) / k, (pr[1] - co[1]) / k
    check(abs(dx - 121) <= 1 and abs(dy - 88) <= 1, f'7 popover at {dx:.1f}/{dy:.1f} DIP from the client origin (Paper 121/88)')
    kp = dpi(pop) / 96
    check(abs((pr[3] - pr[1]) - round(228 * kp)) <= 1, f'7 popover height {pr[3] - pr[1]} px = 228 DIP (Paper 230 with its 16/20 check label)')
    img = capture(pop)
    if img and d:
        img.save(OUT / 'share-popover.png')
        ref = HERE / 'paper' / ('share-popover-check-1x.png' if d == 96 else 'share-popover-check-1.25x.png')
        log(f'     popover {img.size} vs Paper {ref.name} (expected: the check row is 14/18 at 70 %, owner decision, so the '
            'popover is 2 DIP shorter than the board: compared down to the check row, the rest by position below; the board '
            f'says meet.google.com, this site 127.0.0.1:{WEB})')
        check(compare(OUT / 'share-popover.png', ref, OUT / 'share-popover-diff.png', '--crop-height', round(143 * kp), '--surface', f'{round(170 * kp)},{round(50 * kp)}',
                      '--inset', round(12 * kp), '--name-band', '0:left'), '7 the popover matches the Paper export above the check row (compare.py)')
        full, _ = Q.print_window(h)
        if full:
            full.convert('RGB').save(OUT / 'share-window.png')
    elif hidden():
        log('     7 pixels: night run only (screen captures)')
    def refused(n):   # one more answer, a refusal
        r = results(tm); return len(r) == n + 1 and r[n] == 'NotAllowedError: Permission denied'
    # 8. Bloquer (after the 500 ms arming), then again
    if hidden():
        log('     8 / 10 / 12 « Bloquer », « Choisir… »: night run only. A popover that is not in the foreground ignores its '
            'buttons (SEC-22, against clicks a page provokes), and a desktop that is never displayed is never in the foreground.')
        key(pop, 0x1B); wait(lambda: not popover(pid) and refused(0), 4)
    else:
        time.sleep(0.7)
        click_dip(pop, POP_BLOCK)
        check(bool(wait(lambda: not popover(pid), 4)), '8 « Bloquer » closes the popover')
        check(bool(wait(lambda: refused(0), 4)), f'8 the page gets NotAllowedError « Permission denied » ({results(tm)})')
    click_in_page(h, tm, '#present')
    check(bool(wait(lambda: popover(pid), 6)), '8 asking again shows the popover again (box left clear)')
    # 9. Échap, then the close button
    pop = popover(pid); time.sleep(0.7); n = len(results(tm))
    if pop:
        key(pop, 0x1B)
    check(bool(wait(lambda: not popover(pid) and refused(n), 4)), f'9 Échap refuses ({results(tm)})')
    click_in_page(h, tm, '#present'); pop = wait(lambda: popover(pid), 6); time.sleep(0.7); n = len(results(tm))
    if pop:
        click_dip(pop, POP_CLOSE)
    check(bool(wait(lambda: not popover(pid) and refused(n), 4)), f'9 the close button refuses ({results(tm)})')
    # 10. Choisir… -> the engine's own picker
    if not hidden():
        before = {x[0] for x in top_windows()}
        click_in_page(h, tm, '#present'); pop = wait(lambda: popover(pid), 6); time.sleep(0.7); n = len(results(tm))
        if pop:
            click_dip(pop, POP_CHOOSE)
        check(bool(wait(lambda: not popover(pid), 4)), '10 « Choisir… » closes our popover (it does not come back)')
        time.sleep(1.5)
        check(len(results(tm)) == n, f'10 the request still waits for the engine\'s picker ({results(tm)})')
        seen, how = cancel_engine_picker(h, pid, before, tm, n)
        check(seen, '10 the engine\'s own picker is open (its window, or its buttons in UI Automation)')
        check(bool(how) and len(results(tm)) == n + 1 and results(tm)[n].startswith('NotAllowedError'),   # the engine's own words: « Permission denied by user »
              f'10 cancelling the engine picker settles the request: {how or "not cancelled"} ({results(tm)})')
        if not how:   # never leave it open: it would hold the next requests (20:00 run 24/11, steps 11-12)
            w = ws(tm); w.call('Page.navigate', {'url': M_URL}); w.s.close(); time.sleep(2); tm = wait(lambda: site_in('meet', M_URL, 'Point hebdo'), 8)
    # 11. navigation while the popover is open
    click_in_page(h, tm, '#present'); check(bool(wait(lambda: popover(pid), 6)), '11 popover open')
    w = ws(tm); w.call('Page.navigate', {'url': M_URL}); w.s.close(); time.sleep(2)
    check(bool(wait(lambda: not popover(pid), 6)), '11 a navigation closes the popover')
    always_block_steps(h, pid)


def blocked_file():
    f = APP / 'screen-share-blocked'   # portable copy: the profile's data folder is the app folder
    return f.read_text(encoding='utf-8') if f.exists() else ''


UIA_SCRIPT = r'''
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
$AE = [System.Windows.Automation.AutomationElement]
$names = @()
foreach ($p in @(__PIDS__)) {
  $c = New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, [int]$p)
  foreach ($w in $AE::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children, $c)) {
    foreach ($e in $w.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition)) {
      $n = $e.Current.Name; $s = $e.Current.ItemStatus; $hl = $e.Current.HelpText
      $names += ($n + $(if ($s) { ' [' + $s + ']' } else { '' }) + $(if ($hl) { ' {' + $hl + '}' } else { '' }))
      if ('__NAME__' -ne '' -and $n -like '__NAME__') { try { $e.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke(); 'INVOKED ' + $n; exit } catch { 'NOINVOKE ' + $n } }
    }
  }
}
'NAMES ' + ($names -join ' | ')
'''


def uia(pid, invoke='', extra_pids=()):
    """UI Automation over this copy's windows (the browser's, plus `extra_pids`): every name [status] {help}, or the
    first element whose name matches `invoke` invoked. With --hidden-desktop it runs ON that desktop (UI Automation does
    not cross desktops)."""
    script = UIA_SCRIPT.replace('__PIDS__', ','.join(str(p) for p in [pid, *extra_pids])).replace('__NAME__', invoke.replace("'", "''"))
    if not hidden():   # UTF-8 both ways: the engine's picker has characters the console code page lacks (slot 24/11)
        r = subprocess.run(['powershell', '-NoProfile', '-Command', '[Console]::OutputEncoding=[Text.Encoding]::UTF8;' + script],
                           capture_output=True, encoding='utf-8', errors='replace', timeout=90)
        return (r.stdout or '').strip()
    out = OUT / 'uia-out.txt'; ps1 = OUT / 'uia-run.ps1'
    try:
        out.unlink()
    except OSError:
        pass
    ps1.write_text(script, encoding='utf-8-sig')
    p = Proc(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-Command', f"& '{ps1}' | Out-File -Encoding utf8 -FilePath '{out}'"], ROOT, dict(os.environ), 0x08000000)
    p.wait(90); kernel32.CloseHandle(W.HANDLE(p.handle))
    try:
        return out.read_text(encoding='utf-8-sig', errors='replace').strip()
    except OSError:
        return ''


def always_block_steps(h, pid):
    """« Toujours bloquer ce site » (board 1O5F-0, variant B): remembered with « Bloquer » only, refused next time
    without the popover, listed and removable in Réglages › Confidentialité › Autorisations des sites."""
    tm = wait(lambda: site_in('meet', M_URL, 'Point hebdo'), 8)
    if not check(bool(tm), '12 meet page ready'):
        return
    evaluate(tm, 'window.results.length=0')
    if hidden():   # the buttons need the foreground (see 8): the list written as « Bloquer » with the box checked writes it
        (APP / 'screen-share-blocked').write_text(f'browser-screen-share-blocked 1\nhttp://127.0.0.1:{WEB}\n', encoding='utf-8', newline='\n')
        log('     12 (hidden desktop) the profile\'s list written as « Toujours bloquer » + « Bloquer » would (src/screen-share-blocks.h format)')
        evaluate(tm, 'window.results.push("NotAllowedError: Permission denied")')
    else:
        click_in_page(h, tm, '#present'); pop = wait(lambda: popover(pid), 6); time.sleep(0.7)
        if not check(bool(pop), '12 popover open'):
            return
        click_dip(pop, POP_CHECK); time.sleep(0.3)
        img = capture(pop)
        if img:
            img.save(OUT / 'share-popover-checked.png')
        click_dip(pop, POP_BLOCK)
        check(bool(wait(lambda: results(tm) == ['NotAllowedError: Permission denied'], 4)), f'12 « Toujours bloquer » + « Bloquer »: refused ({results(tm)})')
        check(f'http://127.0.0.1:{WEB}' in blocked_file(), f'12 the site is kept in this profile\'s list ({blocked_file()!r})')
    click_in_page(h, tm, '#present')
    check(bool(wait(lambda: len(results(tm)) == 2, 4)) and not popover(pid) and results(tm)[1].startswith('NotAllowedError'),
          f'13 next request: normal refusal, no popover ({results(tm)})')
    # Réglages › Confidentialité › Autorisations des sites: the site, its row, then « Retirer les autorisations ».
    site = f'127.0.0.1:{WEB}'
    for name in ('Réglages', 'Confidentialité', 'Autorisations des sites'):
        log(f'     {uia(pid, name)[:160]}'); time.sleep(1.5)
    names = uia(pid)
    check(f'{site} {{Partage d\'écran bloqué}}' in names, f'14 Réglages › Autorisations des sites lists « {site} » with « Partage d\'écran bloqué »')
    log(f'     {uia(pid, site)[:160]}'); time.sleep(1.5)
    names = uia(pid)
    check('Partage d\'écran [Bloqué]' in names, '14 its page shows « Partage d\'écran » « Bloqué »')
    if not ('Partage d\'écran [Bloqué]' in names):
        log('     ' + names[:900])
    log(f'     {uia(pid, "Retirer les autorisations")[:160]}'); time.sleep(1.5)
    check(f'http://127.0.0.1:{WEB}' not in blocked_file(), f'14 removed there: the list no longer holds the site ({blocked_file()!r})')
    log(f'     {uia(pid, "Fermer les réglages")[:160]}'); time.sleep(1.0)
    click_in_page(h, tm, '#present')
    check(bool(wait(lambda: popover(pid), 6)), '14 after removal the popover asks again')
    if popover(pid):
        key(popover(pid), 0x1B)


def freeze_input(h, t, sock=None):
    """Input the frozen page cannot answer (what the engine's hang monitor waits for): keys posted to the render widget
    and the same keys from DevTools (on a connection opened BEFORE the page froze, never waiting for an answer)."""
    rw = render_widget(h)
    for i in range(4):
        if rw:
            user32.PostMessageW(rw, WM_KEYDOWN, 0x41, 1); user32.PostMessageW(rw, WM_KEYUP, 0x41, 0xC0000001)
        if sock:
            try:
                sock.send(json.dumps({'id': 9000 + i, 'method': 'Input.dispatchKeyEvent', 'params': {'type': 'keyDown', 'key': 'a', 'text': 'a'}}))
            except Exception:
                pass
        time.sleep(0.3)
    if sock:   # closed at once: while a DevTools client stays attached to the page, the engine never reports it frozen
        try:   # (slot 24/11: kept open, no report in 3 tries; the 20:00 run's script dropped it, report in 17 s)
            sock.s.close()
        except Exception:
            pass
    return None


def frozen_steps(h, pid, d):
    """« Page qui ne répond plus · A »: the engine reports the frozen page (~17 s after unanswered input); our window
    over the veiled page; Attendre / the page coming back / Recharger."""
    F, F40 = f'{BASE}/freeze', f'{BASE}/freeze40'
    tm = tab('meet')   # the tab ON SCREEN: our window only shows for it (night 24/11: a background tab was frozen)
    if not tm or not on_screen('meet'):
        post(h, SelectTabBase + (1 if tab('site') else 0)); time.sleep(0.8); tm = tab('meet')
    if not tm:
        check(False, 'F frozen-page fixture'); return
    w = ws(tm); w.call('Page.navigate', {'url': F40}); w.s.close(); time.sleep(2)
    tf = wait(lambda: site_in('meet', F40, 'Planète'), 8)
    if not check(bool(tf) and on_screen('meet'), 'F the frozen-page fixture is the tab on screen'):
        return
    host = page_host(h); before = grab(host) if host and not hidden() else None
    front(h)

    def freeze(t):   # the DevTools connection opened while the page still answers (as in the 20:00 run, where it worked)
        sock = ws(t)
        click_in_page(h, t, '#present'); time.sleep(0.5)
        return freeze_input(h, t, sock)
    t0 = time.time(); sock = freeze(tf)
    win = wait(lambda: popover(pid), 35)
    log(f'     frozen window after {time.time() - t0:.1f} s')
    if not check(bool(win), 'F1 our window appears over the frozen page'):
        return
    time.sleep(0.5)
    cr = W.RECT(); user32.GetClientRect(h, ctypes.byref(cr)); co = client_origin(h); r = rect(win); k = dpi(h) / 96.0
    want = (co[0] + (cr.right - round(400 * k)) // 2, co[1] + (cr.bottom - (r[3] - r[1])) // 2)
    check(abs(r[0] - want[0]) <= 1 and abs(r[1] - want[1]) <= 1 and abs((r[2] - r[0]) - round(400 * k)) <= 1,
          f'F1 centred in the window: {r} (expected left/top {want}, 400 DIP wide)')
    img = capture(win)
    if img and d:
        img.save(OUT / 'frozen-window.png')
        ref = HERE / 'paper' / ('frozen-window-1x.png' if d == 96 else 'frozen-window-1.25x.png')
        log(f'     the board says lemonde.fr, this page 127.0.0.1:{WEB}')
        check(compare(OUT / 'frozen-window.png', ref, OUT / 'frozen-window-diff.png', '--surface', f'{round(10 * k)},{round(100 * k)}', '--inset', round(12 * k), '--name-band', '1:left'),
              f'F1 the window matches the Paper export ({ref.name})')
    elif hidden():
        log('     F1 pixels: night run only (screen captures)')
    scrim = top_windows(pid, 'BrowserModalScrim')
    check(bool(scrim) and host and rect(scrim[0][0]) == host, f'F1 the veil covers the page area ({rect(scrim[0][0]) if scrim else None} vs {host})')
    if host and before:
        after = grab(host); px = (20, (host[3] - host[1]) // 2)   # a page pixel beside the window
        check(near(after.getpixel(px), mix((0, 0, 0), 0x80 / 255, before.getpixel(px))), f'F1 veil black 50 %: {before.getpixel(px)} -> {after.getpixel(px)}')
        corner = (1, host[3] - host[1] - 2)   # inside the page's rounded bottom-left corner (radius 6): not veiled
        log(f'     bottom-left corner pixel before/after: {before.getpixel(corner)} / {after.getpixel(corner)}')
        grab((host[0] - 10, host[1] - 90, host[2] + 10, host[3] + 10)).save(OUT / 'frozen-screen.png')
    # F2. the page runs again (40 s): the window closes by itself
    check(bool(wait(lambda: not popover(pid), 40)), f'F2 the page answers again: the window closes by itself ({time.time() - t0:.1f} s)')
    if sock:
        sock.s.close()
    check(evaluate(tab('meet'), '1+1') == 2, 'F2 the page runs again')
    # F3. Attendre, frozen for good: closed, then back by itself 15 s later (the engine reports a freeze once)
    w = ws(tab('meet')); w.call('Page.navigate', {'url': F}); w.s.close(); time.sleep(2)
    tf = wait(lambda: site_in('meet', F, 'Planète'), 8)
    sock = freeze(tf); win = wait(lambda: popover(pid), 35)
    if check(bool(win), 'F3 window shown for a page frozen for good'):
        time.sleep(0.7)
        if hidden():   # its buttons need the foreground (SEC-22, see 8); Échap is « Attendre » and is never held back
            key(win, 0x1B)
        else:
            click_dip(win, FROZEN_WAIT)
        check(bool(wait(lambda: not popover(pid), 4)), 'F3 « Attendre »' + (' (Échap)' if hidden() else '') + ' closes it')
        t1 = time.time(); time.sleep(5)
        check(not popover(pid), 'F3 not back at once (5 s after « Attendre »)')
        win = wait(lambda: popover(pid), 30)
        took = time.time() - t1
        check(bool(win) and 13 <= took <= 25, f'F3 still frozen: the window comes back by itself ({took:.1f} s after « Attendre », expected 15)')
    # F4. Recharger: the tab's renderer ends, the page reloads (no crashed-tab page)
    if win and hidden():
        log('     F4 « Recharger »: night run only (a button: needs the foreground, SEC-22)')
    elif win:
        time.sleep(0.7); click_dip(win, FROZEN_RELOAD)
        ok = wait(lambda: site_in('meet', F, 'Planète') and evaluate(tab('meet'), 'document.readyState') == 'complete', 20)
        check(bool(ok) and not crash_in('meet') and not popover(pid), 'F4 « Recharger » reloads the page (its renderer ended), no crashed-tab page')
    if sock:
        try:
            sock.s.close()
        except Exception:
            pass


def button_state_steps(h, pid):
    """Crashed-tab page buttons (Paper 1O26-0): hover / pressed colours. The pointer is the engine's own (DevTools
    Input.dispatchMouseEvent on the tab on screen): a posted WM_MOUSEMOVE does not hover a page the real pointer is not over."""
    if hidden():
        log('     S hover / pressed colours: night run (screen captures); their CSS is checked by crash_screenshare_test'); return
    kill_renderers(); ct = wait(lambda: crash_in('meet'), 15)
    if not check(bool(ct), 'S crash page for the button states'):
        return
    host = page_host(h)

    def box(sel):
        return evaluate(ct, f'(()=>{{const r=document.querySelector({json.dumps(sel)}).getBoundingClientRect();return [r.x,r.y,r.width,r.height,devicePixelRatio]}})()')
    canvas = (0x17, 0x17, 0x17)
    for sel, rest, hover, pressed in (('#reload', (0xE6,) * 3, (0xFF,) * 3, mix((255, 255, 255), 0xBF / 255, canvas)),
                                      ('#close', mix((255, 123, 110), .10, canvas), mix((255, 123, 110), .16, canvas), mix((255, 123, 110), .22, canvas))):
        b = box(sel)
        if not isinstance(b, list):
            check(False, f'S {sel} found'); continue
        x, y, w_, h_, dpr = b; px = (round((x + 4) * dpr), round((y + 4) * dpr))   # a corner of the fill, away from the text
        cx, cy = x + w_ / 2, y + h_ / 2
        cdp_mouse(ct, 'mouseMoved', cx, cy); time.sleep(0.4)
        hov = grab(host).getpixel(px)
        cdp_mouse(ct, 'mousePressed', cx, cy); time.sleep(0.3)
        pre = grab(host).getpixel(px)
        cdp_mouse(ct, 'mouseMoved', x, y + h_ + 80); cdp_mouse(ct, 'mouseReleased', x, y + h_ + 80); time.sleep(0.4)   # released away: nothing activated
        check(near(hov, hover), f'S {sel} hover {hov} (Paper {hover})')
        check(near(pre, pressed), f'S {sel} pressed {pre} (Paper {pressed})')
    grab(host).save(OUT / 'crash-buttons-states.png')
    if crash_in('meet'):
        click_in_page(h, crash_in('meet'), '#reload')
        wait(lambda: site_in('meet', M_URL, 'Point hebdo'), 10)


def run(app_src, only):
    shutil.rmtree(APP, ignore_errors=True); APP.mkdir(parents=True)
    for f in app_src.iterdir():
        if f.name in ('profile', 'session', 'sessions', 'profiles', 'windows', 'prototype.log', 'downloads', 'bookmarks', 'settings', 'history', 'annotations', 'ui-language', 'profiles.txt', 'screen-share-blocked'):
            continue
        (shutil.copytree(f, APP / f.name) if f.is_dir() else shutil.copy2(f, APP / f.name))
    (APP / 'session').write_text(f'browser-session-2\n0\n2\n"{A_URL}"\n"{M_URL}"\n0\n', encoding='utf-8', newline='\n')
    (APP / 'settings').write_text('browser-settings 1\nrestore-tabs=1\n', encoding='utf-8', newline='\n')
    srv = socketserver.ThreadingTCPServer(('127.0.0.1', WEB), Handler); threading.Thread(target=srv.serve_forever, daemon=True).start()
    env = dict(os.environ)
    env['WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS'] = f'--remote-debugging-port={PORT} --remote-allow-origins=http://127.0.0.1:{PORT}'
    if hidden():   # a desktop that is never displayed reads as « occluded » to the engine (tests/chat/lot2_desktop.py does the same)
        env['WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS'] += ' --disable-features=CalculateNativeWinOcclusion --disable-backgrounding-occluded-windows'
    proc = Proc([APP / 'browser.exe'], APP, env)
    h = None
    try:
        h = wait(lambda: main_window(proc.pid), 25)
        if not check(bool(h), 'the throwaway copy starts' + (f' (on the hidden desktop {HIDDEN["name"]})' if hidden() else '')):
            return
        d = place(h)
        if not setup_tabs():
            return
        fit_page_area(h, d)
        if 'crash' in only:
            crash_steps(h, proc.pid, d)
        if 'share' in only:
            share_steps(h, proc.pid, d)
        if 'states' in only:
            button_state_steps(h, proc.pid)
        if 'frozen' in only:
            frozen_steps(h, proc.pid, d)
        if hidden():
            mine = {proc.pid} | {p for p, _ in engine_processes()}
            seen = user_desktop_windows(mine)
            check(not seen, f'H nothing of the test browser on the user\'s desktop ({seen})')
    finally:
        try:
            if h and main_window(proc.pid):
                user32.PostMessageW(main_window(proc.pid), WM_CLOSE, 0, 0)
            if not proc.wait(10):
                proc.kill()
        except Exception:
            proc.kill()
    srv.shutdown()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--app', default=str(ROOT / 'build' / '121-crash-share'))
    ap.add_argument('--hidden-desktop', action='store_true', help='a desktop of its own, never displayed (daytime): no pixels, no hover colours')
    ap.add_argument('--only', default='crash,share,states,frozen', help='comma-separated groups: crash, share, states, frozen')
    a = ap.parse_args()
    src = pathlib.Path(a.app)
    if not (src / 'browser.exe').exists():
        sys.exit(f'no build at {src}: python tools/release/build.py --out {src}')
    job = enter_job()   # noqa: F841 (kept open until exit)
    if a.hidden_desktop:
        enter_hidden_desktop()
    OUT.mkdir(parents=True, exist_ok=True)
    run(src, set(a.only.split(',')))
    log(f'crash-screenshare QA{" (hidden desktop)" if hidden() else ""}: {"FAILED (" + str(failures) + ")" if failures else "OK"}')
    (OUT / ('report-hidden.txt' if hidden() else 'report.txt')).write_text('\n'.join(lines) + '\n', encoding='utf-8')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
