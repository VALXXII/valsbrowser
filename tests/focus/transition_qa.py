#!/usr/bin/env python3
"""Mode Focus entry / exit, frame by frame, on a HIDDEN Windows desktop (daytime-safe: nothing on the user's screen).

    python tests/focus/transition_qa.py --app <built app dir> --out <scratch dir> [--layout top|side|compact]
    (side / compact: the #127 side layouts, --tab-layout: the column slides out by the left edge; the page's left edge is
    judged frame by frame as its top is)
                                        [--reduced] [--scenario enter-exit|interrupt] [--frames-png]

A throwaway portable copy of the app (its data beside it, LOCALAPPDATA of the child redirected, onboarding and the
annotation tutorial off) runs on a second desktop of the session (CreateDesktop: never displayed, no input, no
foreground). One local page (a 127.0.0.1 server of ours) painted in one flat colour. A thread living on that desktop
posts the window's own Shortcut message (Ctrl+Maj+F's command) and copies the window with PrintWindow
(PW_RENDERFULLCONTENT: what DWM composes for it, its child windows and its DirectComposition layers included) as fast as
it can. Per frame:
  * the page's top edge, left edge and bottom edge on the window's centre lines (the flat colour's first / last pixel);
  * the bar: its bottom edge (the tab row and toolbar are drawn near the canvas grey, the page is the flat colour);
  * foreign pixels in the page column: near white (min channel >= 245) or near black (max <= 8): never drawn by the
    browser around this page, so they are a flash; « background » = the canvas grey (#171717) where the page or the bar
    must be — a hole between the bar and the page wider than the inset the page is heading to.
  * a JUMP: the page's top edge moving more than --max-step DIP between two consecutive frames (the switch done at once).
Scenarios: enter-exit (Ctrl+Maj+F, wait, Ctrl+Maj+F), interrupt (Ctrl+Maj+F, a second one mid-way: the page's edge must
turn back from where it is, no jump). Exit code 0 only when every frame passes. JSON report + optional PNG frames (PIL).
The app runs in a job object (KILL_ON_JOB_CLOSE); the desktop is closed at the end. Only our own processes are stopped.
"""
from __future__ import annotations
import argparse, ctypes, ctypes.wintypes as W, http.server, json, os, shutil, sys, threading, time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'agent-guard'))
import agent_guard as G   # noqa: E402  (no agent of this computer: tests/agent-guard)

ap = argparse.ArgumentParser()
ap.add_argument('--app', required=True); ap.add_argument('--out', required=True)
ap.add_argument('--layout', choices=('top', 'side', 'compact'), default='top')
ap.add_argument('--reduced', action='store_true', help='the browser\'s « Réduire les animations » on')
ap.add_argument('--scenario', choices=('enter-exit', 'interrupt'), default='enter-exit')
ap.add_argument('--max-step', type=float, default=40.0, help='DIP the page edge may move between two frames (a jump above)')
ap.add_argument('--start-focus', action='store_true', help='start in Mode Focus (the first press is an exit)')
ap.add_argument('--frames-png', action='store_true')
ap.add_argument('--screen', action='store_true', help='the REAL desktop (a screen slot only): captured as displayed (BitBlt of the screen)')
ap.add_argument('--annotate', action='store_true', help='the annotation mode on first (its banner; the chat in Panneau, agents refused)')
a = ap.parse_args()
src, out = Path(a.app).resolve(), Path(a.out).resolve()
shutil.rmtree(out, ignore_errors=True); out.mkdir(parents=True)

u32 = ctypes.WinDLL('user32', use_last_error=True); k32 = ctypes.WinDLL('kernel32', use_last_error=True); g32 = ctypes.WinDLL('gdi32')
u32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
for f, args, res in [
    (u32.CreateDesktopW, [W.LPCWSTR, W.LPCWSTR, ctypes.c_void_p, W.DWORD, W.DWORD, ctypes.c_void_p], W.HANDLE),
    (u32.SetThreadDesktop, [W.HANDLE], W.BOOL), (u32.CloseDesktop, [W.HANDLE], W.BOOL),
    (u32.EnumDesktopWindows, [W.HANDLE, ctypes.c_void_p, W.LPARAM], W.BOOL),
    (u32.RedrawWindow, [W.HWND, ctypes.c_void_p, W.HANDLE, W.UINT], W.BOOL), (u32.PostMessageW, [W.HWND, W.UINT, W.WPARAM, W.LPARAM], W.BOOL), (u32.GetClientRect, [W.HWND, ctypes.POINTER(W.RECT)], W.BOOL),
    (u32.GetDpiForWindow, [W.HWND], W.UINT), (u32.GetDC, [W.HWND], W.HDC), (u32.ReleaseDC, [W.HWND, W.HDC], ctypes.c_int),
    (u32.SetWindowPos, [W.HWND, W.HWND, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, W.UINT], W.BOOL),
    (u32.EnumWindows, [ctypes.c_void_p, W.LPARAM], W.BOOL), (u32.SetForegroundWindow, [W.HWND], W.BOOL),
    (u32.ClientToScreen, [W.HWND, ctypes.POINTER(W.POINT)], W.BOOL),
    (g32.BitBlt, [W.HDC, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, W.HDC, ctypes.c_int, ctypes.c_int, W.DWORD], W.BOOL),
    (u32.IsWindowVisible, [W.HWND], W.BOOL), (u32.GetClassNameW, [W.HWND, W.LPWSTR, ctypes.c_int], ctypes.c_int),
    (u32.GetWindowThreadProcessId, [W.HWND, ctypes.POINTER(W.DWORD)], W.DWORD), (u32.PrintWindow, [W.HWND, W.HDC, W.UINT], W.BOOL),
    (g32.CreateCompatibleDC, [W.HDC], W.HDC), (g32.CreateDIBSection, [W.HDC, ctypes.c_void_p, W.UINT, ctypes.POINTER(ctypes.c_void_p), W.HANDLE, W.DWORD], W.HBITMAP),
    (g32.SelectObject, [W.HDC, W.HGDIOBJ], W.HGDIOBJ), (g32.DeleteObject, [W.HGDIOBJ], W.BOOL), (g32.DeleteDC, [W.HDC], W.BOOL),
    (k32.CreateJobObjectW, [ctypes.c_void_p, W.LPCWSTR], W.HANDLE), (k32.SetInformationJobObject, [W.HANDLE, ctypes.c_int, ctypes.c_void_p, W.DWORD], W.BOOL),
    (k32.AssignProcessToJobObject, [W.HANDLE, W.HANDLE], W.BOOL), (k32.ResumeThread, [W.HANDLE], W.DWORD), (k32.CloseHandle, [W.HANDLE], W.BOOL)]:
    f.argtypes, f.restype = args, res

class STARTUPINFO(ctypes.Structure):
    _fields_ = [('cb', W.DWORD), ('lpReserved', W.LPWSTR), ('lpDesktop', W.LPWSTR), ('lpTitle', W.LPWSTR), ('dwX', W.DWORD), ('dwY', W.DWORD),
                ('dwXSize', W.DWORD), ('dwYSize', W.DWORD), ('dwXCountChars', W.DWORD), ('dwYCountChars', W.DWORD), ('dwFillAttribute', W.DWORD),
                ('dwFlags', W.DWORD), ('wShowWindow', W.WORD), ('cbReserved2', W.WORD), ('lpReserved2', ctypes.c_void_p),
                ('hStdInput', W.HANDLE), ('hStdOutput', W.HANDLE), ('hStdError', W.HANDLE)]
class PROCESS_INFORMATION(ctypes.Structure):
    _fields_ = [('hProcess', W.HANDLE), ('hThread', W.HANDLE), ('dwProcessId', W.DWORD), ('dwThreadId', W.DWORD)]
class BASIC(ctypes.Structure):
    _fields_ = [('a', ctypes.c_int64), ('b', ctypes.c_int64), ('LimitFlags', W.DWORD), ('c', ctypes.c_size_t), ('d', ctypes.c_size_t), ('e', W.DWORD), ('f', ctypes.c_size_t), ('g', W.DWORD), ('h', W.DWORD)]
class EXT(ctypes.Structure):
    _fields_ = [('Basic', BASIC), ('Io', ctypes.c_uint64 * 6), ('i', ctypes.c_size_t), ('j', ctypes.c_size_t), ('k', ctypes.c_size_t), ('l', ctypes.c_size_t)]
k32.CreateProcessW.argtypes = [W.LPCWSTR, W.LPWSTR, ctypes.c_void_p, ctypes.c_void_p, W.BOOL, W.DWORD, ctypes.c_void_p, W.LPCWSTR, ctypes.POINTER(STARTUPINFO), ctypes.POINTER(PROCESS_INFORMATION)]
k32.CreateProcessW.restype = W.BOOL

PAGE_RGB = (0x2E, 0x7D, 0xF6)   # the page's flat colour: neither the canvas grey, nor black, nor white
# The edge that carries the motion: the top bar's layout, the page's top (81 -> 4); the side layouts (#127), its left edge
# (the column's width -> 4: the column slides out by the left edge) — their top (40 / 32 -> 4) is judged too.
EDGE = 'top' if a.layout == 'top' else 'left'
# A 2 px corner of the page changes by one level of blue every frame (invisible): the page presents continuously, so the
# hidden desktop's DWM composes the window (and our DirectComposition layers) for every capture.
PAGE = ('<!doctype html><title>Focus</title><style>@keyframes t{from{background:#%02X%02X%02X}to{background:#%02X%02X%02X}}'
        '#t{position:fixed;left:0;top:0;width:2px;height:2px;animation:t .05s linear infinite alternate}</style>'
        '<body style="margin:0;background:#%02X%02X%02X"><div id="t"></div><div style="height:2400px"></div></body>'
        % (PAGE_RGB + (PAGE_RGB[0], PAGE_RGB[1], PAGE_RGB[2] + 1) + PAGE_RGB)).encode()
class H(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200); self.send_header('Content-Type', 'text/html'); self.send_header('Content-Length', str(len(PAGE))); self.end_headers(); self.wfile.write(PAGE)
    def log_message(self, *x): pass
srv = http.server.ThreadingHTTPServer(('127.0.0.1', 0), H); url = f'http://127.0.0.1:{srv.server_port}/page'
threading.Thread(target=srv.serve_forever, daemon=True).start()

app = out / 'app'; shutil.copytree(src, app, ignore=shutil.ignore_patterns('session', 'sessions', 'settings', 'profile', 'profiles', 'prototype.log', 'history', 'bookmarks', 'downloads'))
local = out / 'localappdata'; local.mkdir()
(app / 'session').write_text(f'browser-session-2\n0\n1\n"{url}"\n0\n', encoding='utf-8', newline='\n')
(app / 'settings').write_text('browser-settings 1\nrestore-tabs=1\nconfirm-close-tabs=0\nfocus-mode=' + ('1' if a.start_focus else '0') + '\nannotation-onboarded=1\nannotation-connect-prompt=0\n' +('reduce-motion=1\n' if a.reduced else ''), encoding='utf-8', newline='\n')

job = k32.CreateJobObjectW(None, None); info = EXT(); info.Basic.LimitFlags = 0x2000   # KILL_ON_JOB_CLOSE
k32.SetInformationJobObject(job, 9, ctypes.byref(info), ctypes.sizeof(info))
desk_name = f'focus-transition-{os.getpid()}'
hdesk = None if a.screen else u32.CreateDesktopW(desk_name, None, None, 0, 0x10000000, None)
assert a.screen or hdesk, f'CreateDesktop failed {ctypes.get_last_error()}'
env = dict(os.environ, LOCALAPPDATA=str(local), BROWSER_NO_ONBOARDING='1')
if a.annotate:   # the « user's » folders point to an empty home under <out> (no real Claude / Codex seen), agents refused
    home = out / 'home'
    for sub in ('AppData/Roaming', 'AppData/Local'): (home / sub).mkdir(parents=True, exist_ok=True)
    env.update(BROWSER_ANNOTATIONS_TEST='1', USERPROFILE=str(home), APPDATA=str(home / 'AppData/Roaming'), BROWSER_ANNOTATIONS_CONNECT_ROOT=str(home))
env = G.secure_env(env, work=out, exe=app / 'browser.exe', agent=False)   # the test switch on, no agent of this computer
block = ''.join(f'{k}={v}\0' for k, v in env.items()) + '\0'
si = STARTUPINFO(); si.cb = ctypes.sizeof(si); si.lpDesktop = None if a.screen else f'WinSta0\\{desk_name}'
pi = PROCESS_INFORMATION()
cmd = f'"{app / "browser.exe"}"' + (f' --tab-layout={a.layout}' if a.layout != 'top' else '')
ok = k32.CreateProcessW(None, ctypes.create_unicode_buffer(cmd), None, None, False, 0x4 | 0x400, ctypes.create_unicode_buffer(block), str(app), ctypes.byref(si), ctypes.byref(pi))   # SUSPENDED | UNICODE_ENVIRONMENT
assert ok, f'CreateProcess failed {ctypes.get_last_error()}'
k32.AssignProcessToJobObject(job, pi.hProcess)
G.watch(pi.dwProcessId, env, log=app / 'prototype.log', work=out)   # before it runs: agent processes, launches, « AGENTS … ready »
k32.ResumeThread(pi.hThread); k32.CloseHandle(pi.hThread)
pid = pi.dwProcessId

Shortcut, FocusModeCommand = 0x8000 + 1, 3070
report = {'layout': a.layout, 'reduced': a.reduced, 'scenario': a.scenario, 'frames': [], 'checks': []}
result = {}

def driver():
    if hdesk: assert u32.SetThreadDesktop(hdesk), f'SetThreadDesktop {ctypes.get_last_error()}'
    found = []
    @ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    def each(h, _):
        p = W.DWORD(); u32.GetWindowThreadProcessId(h, ctypes.byref(p)); n = ctypes.create_unicode_buffer(64); u32.GetClassNameW(h, n, 64)
        if p.value == pid and n.value == 'LisiereBrowser' and u32.IsWindowVisible(h): found.append(h)
        return True
    win = None
    for _ in range(240):
        found.clear(); (u32.EnumDesktopWindows(hdesk, each, 0) if hdesk else u32.EnumWindows(each, 0))
        if found: win = found[0]; break
        time.sleep(0.25)
    assert win, 'browser window on the hidden desktop'
    s = u32.GetDpiForWindow(win) / 96
    u32.SetWindowPos(win, None, 40, 40, round(1180 * s), round(705 * s), 0x0014); time.sleep(0.8)
    if a.screen: u32.SetForegroundWindow(win); time.sleep(0.3)
    r = W.RECT(); u32.GetClientRect(win, ctypes.byref(r)); w, h = r.right, r.bottom
    screen = u32.GetDC(win); mem = g32.CreateCompatibleDC(screen)
    class BIH(ctypes.Structure):
        _fields_ = [('biSize', W.DWORD), ('biWidth', W.LONG), ('biHeight', W.LONG), ('biPlanes', W.WORD), ('biBitCount', W.WORD), ('biCompression', W.DWORD),
                    ('biSizeImage', W.DWORD), ('biXPelsPerMeter', W.LONG), ('biYPelsPerMeter', W.LONG), ('biClrUsed', W.DWORD), ('biClrImportant', W.DWORD)]
    bi = BIH(); bi.biSize = ctypes.sizeof(bi); bi.biWidth = w; bi.biHeight = -h; bi.biPlanes = 1; bi.biBitCount = 32
    bits = ctypes.c_void_p(); dib = g32.CreateDIBSection(screen, ctypes.byref(bi), 0, ctypes.byref(bits), None, 0); g32.SelectObject(mem, dib)
    buf = (ctypes.c_uint32 * (w * h)).from_address(bits.value)
    sdc = u32.GetDC(None) if a.screen else None
    def grab():
        if a.screen:   # what is displayed: the window's client area copied from the screen (its layers composed by DWM)
            o = W.POINT(0, 0); u32.ClientToScreen(win, ctypes.byref(o)); g32.BitBlt(mem, 0, 0, w, h, sdc, o.x, o.y, 0x00CC0020 | 0x40000000)
        else:   # (a hidden desktop composes only when something presents: the page's animated corner, see PAGE)
            u32.PrintWindow(win, mem, 2)   # PW_RENDERFULLCONTENT
        return bytes(memoryview(buf).cast('B'))
    def px(data, x, y):
        o = (y * w + x) * 4; return data[o + 2], data[o + 1], data[o]
    def is_page(p): return all(abs(p[i] - PAGE_RGB[i]) <= 6 for i in range(3))
    def channels(seq):   # BGRA bytes -> (r, g, b) tuples, sliced in C
        return list(zip(seq[2::4], seq[1::4], seq[0::4]))
    def analyse(data):
        cx, cy, stride = w // 2, h // 2, w * 4
        col = list(zip(data[cx * 4 + 2::stride], data[cx * 4 + 1::stride], data[cx * 4::stride]))   # the centre column, top to bottom
        row = channels(data[cy * stride:(cy + 1) * stride])                                       # the centre row
        top = next((y for y, p in enumerate(col) if is_page(p)), -1)
        bottom = next((y for y in range(h - 1, -1, -1) if is_page(col[y])), -1)
        left = next((x for x, p in enumerate(row) if is_page(p)), -1)
        white = black = 0
        # a grid every 8 px, below the bar in every layout (its own text and icons are white); the side layouts: right of the
        # docked column too (248 complete, 40 compact: its titles and icons are white, and its picture slides there)
        skip = 0 if a.layout == 'top' else round((250 if a.layout == 'side' else 42) * s)
        keep = w - round(400 * s) if a.annotate else w   # the chat panel (its own white text) on the right
        for y in range(int(90 * s), h, 8):
            line = data[y * stride + skip * 4:y * stride + keep * 4]
            for p in zip(line[2::32], line[1::32], line[0::32]):
                if min(p) >= 245: white += 1
                elif max(p) <= 8: black += 1
        right = next((x for x in range(w - 1, -1, -1) if is_page(row[x])), -1)
        # holes INSIDE the page: on its centre lines, between its edges (8 px in), anything that is not the page's colour
        # (the canvas, black, white where the live page has not drawn its new size)
        holes = 0
        if top >= 0 and bottom > top: holes += sum(1 for y in range(top + 8, bottom - 8) if not is_page(col[y]))
        if left >= 0 and right > left: holes += sum(1 for x in range(left + 8, right - 8) if not is_page(row[x]))
        return {'top': top / s if top >= 0 else None, 'bottom': bottom / s if bottom >= 0 else None, 'left': left / s if left >= 0 else None,
                'right': right / s if right >= 0 else None, 'holes': holes, 'white': white, 'black': black}
    # the page on screen first
    t0 = time.time()
    while time.time() - t0 < 40:
        d = grab()
        if is_page(px(d, w // 2, h // 2)): break
        time.sleep(0.2)
    else:
        result['error'] = 'the page never showed (PrintWindow on the hidden desktop?)'; return
    time.sleep(1.0)
    if a.annotate:   # the annotation mode (a local page: it starts there), its banner and the chat in Panneau, then Focus
        logp = app / 'prototype.log'; n0 = len(logp.read_text(encoding='utf-8', errors='replace')) if logp.exists() else 0
        u32.PostMessageW(win, Shortcut, 3060, 0)   # AnnotateCommand
        t1 = time.time()
        while time.time() - t1 < 20 and 'CHAT panel in place' not in (logp.read_text(encoding='utf-8', errors='replace')[n0:] if logp.exists() else ''): time.sleep(0.2)
        report['annotate'] = 'CHAT panel in place' in (logp.read_text(encoding='utf-8', errors='replace')[n0:] if logp.exists() else '')
        time.sleep(2.5)
    rest = analyse(grab()); report['rest'] = rest
    frames = []; marks = []
    def press(): marks.append(time.perf_counter()); u32.PostMessageW(win, Shortcut, FocusModeCommand, 0)
    plan = [(0.15, press), (1.4, press)] if a.scenario == 'enter-exit' else [(0.15, press)]
    start = time.perf_counter(); i = 0; keep = []; turned = False
    while time.perf_counter() - start < 2.8:
        now = time.perf_counter() - start
        while i < len(plan) and now >= plan[i][0]: plan[i][1](); i += 1
        d = grab(); t = (time.perf_counter() - start) * 1000
        f = analyse(d); f['t'] = round(t, 1); frames.append(f)
        # interrupt: the second press once the page is seen half-way (the app's first motion really under way); the side
        # layouts: its left edge between a quarter and three quarters of the way from the column's edge to 4
        if a.scenario == 'interrupt' and not turned and marks and f[EDGE] is not None and rest[EDGE] is not None:
            lo, hi = (20, 50) if EDGE == 'top' else (4 + .25 * (rest[EDGE] - 4), 4 + .75 * (rest[EDGE] - 4))
            if lo < f[EDGE] < hi: press(); turned = True
        if a.frames_png: keep.append((t, d))
    report['frames'] = frames; report['presses_ms'] = [round((m - start) * 1000, 1) for m in marks]
    if a.frames_png:
        try:
            from PIL import Image
            for t, d in keep[::2]:
                Image.frombuffer('RGBA', (w, h), d, 'raw', 'BGRA', 0, 1).convert('RGB').save(out / f'frame-{int(t):05d}.png')
        except Exception as e:
            report['png'] = str(e)
    result['ok'] = True

t = threading.Thread(target=driver, daemon=True); t.start(); t.join(120)
srv.shutdown()
k32.CloseHandle(job)   # KILL_ON_JOB_CLOSE: our copy of the browser goes with it
if hdesk: u32.CloseDesktop(hdesk)

fails = []
def check(ok, what, detail=''):
    report['checks'].append({'ok': bool(ok), 'what': what, 'detail': detail}); print(('PASS ' if ok else 'FAIL ') + what + (f' — {detail}' if detail else ''), flush=True)
    if not ok: fails.append(what)
if 'error' in result or not result.get('ok'):
    check(False, 'capture on the hidden desktop', result.get('error', 'driver did not finish'))
else:
    fr = report['frames']
    tops = [f['top'] for f in fr if f['top'] is not None]
    # Captured frames: a step is judged only between two captures less than 40 ms apart (a hidden desktop's capture can
    # stall ~250 ms while the app keeps drawing: its own trace below says what every frame was).
    # Where each motion lands and how far each edge travels: 4 in Focus, the normal layout's edge otherwise (81 under the
    # top bar; the side layouts: the window bar's height and the column's width, from the app's « FOCUS motion » line).
    log = (app / 'prototype.log').read_text(encoding='utf-8', errors='replace') if (app / 'prototype.log').exists() else ''
    import re
    m = re.search(r'side layout, column (\d+) DIP, window bar (\d+) DIP', log)
    normal = {'top': 81.0} if a.layout == 'top' else ({'left': float(m.group(1)), 'top': float(m.group(2))} if m else {'left': 248.0, 'top': 40.0})
    # A jump is judged against the distance: the spec's one spring moves every edge on the same curve, so an edge that
    # travels three times as far moves three times as much per frame (the top bar's 77 DIP: 40 DIP between two captures,
    # 16 per app frame, the thresholds of the first version; the complete column's 244 DIP: 127 and 51).
    TOP_TRAVEL = 77.0
    def scale(kind): return max(1.0, (normal.get(kind, 81.0) - 4) / TOP_TRAVEL)
    # Captured frames: a step is judged only between two captures less than 40 ms apart (a hidden desktop's capture can
    # stall ~250 ms while the app keeps drawing: its own trace below says what every frame was).
    seen = [f for f in fr if f['top'] is not None and f[EDGE] is not None]
    kinds = ['top'] if EDGE == 'top' else [EDGE, 'top']
    pairs = [(a2, b) for a2, b in zip(seen, seen[1:]) if b['t'] - a2['t'] < 40]
    steps = [max(abs(b[k] - a2[k]) / scale(k) for k in kinds) for a2, b in pairs]   # in « top bar DIP »
    raw = {k: max((abs(b[k] - a2[k]) for a2, b in pairs), default=0) for k in kinds}
    stalls = [round(b['t'] - a2['t']) for a2, b in zip(fr, fr[1:]) if b['t'] - a2['t'] >= 40]
    report['capture_stalls_ms'] = stalls
    check(all(f['top'] is not None for f in fr), 'a page on every frame (never a frame without the page)', f"{sum(f['top'] is None for f in fr)} of {len(fr)} without")
    if not a.reduced:
        limits = ', '.join(f'{k} {a.max_step * scale(k):.0f}' for k in kinds)
        check(max(steps or [0]) <= a.max_step, f'no jump on screen: the page\'s edges move at most {a.max_step} DIP per 77 DIP of travel between two captures ({limits} DIP)', f'largest steps {", ".join(f"{k} {v:.1f}" for k, v in raw.items())} DIP over {len(pairs)} close pairs (capture stalls: {stalls} ms)')
        # The app's own trace: the page's top edge (side layouts: and its left edge) at EVERY frame it drew (src/main.cpp
        # « FOCUS motion trace »).
        def traced(kind): return [[float(v) for v in line.split(':', 1)[1].split()] for line in log.splitlines() if f'FOCUS motion trace (page {kind}' in line]
        traces = {'top': traced('top'), 'left': traced('left')}
        report['app_traces'] = traces
        if a.layout != 'top': check(bool(m), 'the app moved in its side layout (« FOCUS motion … side layout »)', m.group(0) if m else 'no side motion logged')
        # Frame pacing (the app's « FOCUS motion landed (x …, N frames, T ms) »): on a busy machine or a hidden desktop the
        # app may draw 30 frames a second with a 50 ms hitch now and then; the spring is in time, so a longer frame carries
        # the edge further. The limit is the spring's largest move in the app's longest frame step (50 ms), never below 16
        # DIP per 77 DIP of travel: the whole distance in one frame (the old switch) always fails; smoothness itself is the
        # screen's eye check (the slowest motion's mean frame is reported).
        paced = [int(t2) / max(1, int(n)) for n, t2 in re.findall(r'FOCUS motion landed \(x [-0-9.]+, (\d+) frames, (\d+) ms\)', log)]
        frame_ms = max(paced) if paced else 16.7
        report['frame_ms'] = [round(v, 1) for v in paced]
        OMEGA_E = 2 * 3.141592653589793 / 0.3 / 2.718281828459045   # a critically damped spring's top speed per DIP of travel (1/s)
        for kind in kinds:
            travel = normal.get(kind, 81.0) - 4
            ts = traces[kind]; worst = max((abs(b - a2) for t in ts for a2, b in zip(t, t[1:])), default=None)
            limit = max(16 * scale(kind), travel * OMEGA_E * 0.05)   # one frame's step is capped at 50 ms by the app (FocusMotionFrame)
            check(bool(ts) and worst is not None and worst <= limit and limit < travel * 0.6, f'no jump in any frame the app drew: the page\'s {kind} edge moves at most {limit:.0f} DIP per frame (the spring over the app\'s 50 ms frame cap; slowest mean frame {frame_ms:.0f} ms)', f'{len(ts)} motions, {sum(len(t) for t in ts)} frames, largest step {worst if worst is None else round(worst, 1)} DIP')
            end = normal.get(kind)
            if a.annotate:   # under the banner: each motion lands where another started (the two layouts)
                starts = [t[0] for t in ts if t]; end = 'a start'; ends_ok = bool(starts) and all(t and min(abs(t[-1] - v) for v in starts) < 1 for t in ts)
            else: ends_ok = end is not None and all(t and min(abs(t[-1] - 4), abs(t[-1] - end)) < 1 for t in ts)
            check(ends_ok, f'every motion lands exactly on a layout ({kind}: 4 or {end} DIP)', [round(t[-1], 1) for t in ts if t])
    if a.annotate: check(report.get('annotate'), 'the annotation mode on, its chat in place (Panneau)')
    check(sum(f['white'] for f in fr) == 0, 'no white pixel in any frame', f"{sum(f['white'] > 0 for f in fr)} frames with white")
    check(sum(f['holes'] for f in fr) == 0, 'no hole inside the page in any frame (canvas, black or white where the page is)', f"{sum(f['holes'] > 0 for f in fr)} frames with holes")
    # (black outside the page is not judged here: on a hidden desktop PrintWindow does not render the window's own Direct2D
    # surface — the bar and the canvas inset come out black at rest; the night QA judges those from the screen)
    edges = [f[EDGE] for f in fr if f[EDGE] is not None]
    if not a.reduced:
        far = max(edges or [0]) if a.annotate else normal.get(EDGE, 81.0)
        moving = [f for f in fr if f[EDGE] is not None and 5 < f[EDGE] < far - 1]
        check(len(moving) >= 4, f'the page\'s {EDGE} edge is seen in between (a motion, not a switch)', f'{len(moving)} frames between the two layouts')
    if a.scenario == 'interrupt' and edges:
        check(abs(edges[-1] - edges[0]) < 1.5, 'interrupted: back where it started', f'{EDGE} start {edges[0]:.1f}, end {edges[-1]:.1f} DIP')
    report['summary'] = {'frames': len(fr), 'largest_step_dip': max(steps or [0]), 'tops': [round(x, 1) for x in tops][:400]}
(out / 'report.json').write_text(json.dumps(report, indent=1), encoding='utf-8')
print(f"{len(report['checks']) - len(fails)} / {len(report['checks'])} checks passed; report {out / 'report.json'}")
sys.exit(1 if fails else 0)
