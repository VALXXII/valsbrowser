#!/usr/bin/env python3
"""Mode Focus entry / exit, frame by frame, on a HIDDEN Windows desktop (daytime-safe: nothing on the user's screen).

    python tests/focus/transition_qa.py --app <built app dir> --out <scratch dir> [--layout top|side|compact]
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

ap = argparse.ArgumentParser()
ap.add_argument('--app', required=True); ap.add_argument('--out', required=True)
ap.add_argument('--layout', choices=('top', 'side', 'compact'), default='top')
ap.add_argument('--reduced', action='store_true', help='the browser\'s « Réduire les animations » on')
ap.add_argument('--scenario', choices=('enter-exit', 'interrupt'), default='enter-exit')
ap.add_argument('--max-step', type=float, default=40.0, help='DIP the page edge may move between two frames (a jump above)')
ap.add_argument('--start-focus', action='store_true', help='start in Mode Focus (the first press is an exit)')
ap.add_argument('--frames-png', action='store_true')
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
(app / 'settings').write_text('browser-settings 1\nrestore-tabs=1\nconfirm-close-tabs=0\nfocus-mode=' + ('1' if a.start_focus else '0') + '\nannotation-onboarded=1\n' +('reduce-motion=1\n' if a.reduced else ''), encoding='utf-8', newline='\n')

job = k32.CreateJobObjectW(None, None); info = EXT(); info.Basic.LimitFlags = 0x2000   # KILL_ON_JOB_CLOSE
k32.SetInformationJobObject(job, 9, ctypes.byref(info), ctypes.sizeof(info))
desk_name = f'focus-transition-{os.getpid()}'
hdesk = u32.CreateDesktopW(desk_name, None, None, 0, 0x10000000, None)
assert hdesk, f'CreateDesktop failed {ctypes.get_last_error()}'
env = dict(os.environ, LOCALAPPDATA=str(local), BROWSER_NO_ONBOARDING='1')
block = ''.join(f'{k}={v}\0' for k, v in env.items()) + '\0'
si = STARTUPINFO(); si.cb = ctypes.sizeof(si); si.lpDesktop = f'WinSta0\\{desk_name}'
pi = PROCESS_INFORMATION()
cmd = f'"{app / "browser.exe"}"' + (f' --tab-layout={a.layout}' if a.layout != 'top' else '')
ok = k32.CreateProcessW(None, ctypes.create_unicode_buffer(cmd), None, None, False, 0x4 | 0x400, ctypes.create_unicode_buffer(block), str(app), ctypes.byref(si), ctypes.byref(pi))   # SUSPENDED | UNICODE_ENVIRONMENT
assert ok, f'CreateProcess failed {ctypes.get_last_error()}'
k32.AssignProcessToJobObject(job, pi.hProcess); k32.ResumeThread(pi.hThread); k32.CloseHandle(pi.hThread)
pid = pi.dwProcessId

Shortcut, FocusModeCommand = 0x8000 + 1, 3070
report = {'layout': a.layout, 'reduced': a.reduced, 'scenario': a.scenario, 'frames': [], 'checks': []}
result = {}

def driver():
    assert u32.SetThreadDesktop(hdesk), f'SetThreadDesktop {ctypes.get_last_error()}'
    found = []
    @ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    def each(h, _):
        p = W.DWORD(); u32.GetWindowThreadProcessId(h, ctypes.byref(p)); n = ctypes.create_unicode_buffer(64); u32.GetClassNameW(h, n, 64)
        if p.value == pid and n.value == 'LisiereBrowser' and u32.IsWindowVisible(h): found.append(h)
        return True
    win = None
    for _ in range(240):
        found.clear(); u32.EnumDesktopWindows(hdesk, each, 0)
        if found: win = found[0]; break
        time.sleep(0.25)
    assert win, 'browser window on the hidden desktop'
    s = u32.GetDpiForWindow(win) / 96
    u32.SetWindowPos(win, None, 40, 40, round(1180 * s), round(705 * s), 0x0014); time.sleep(0.8)
    r = W.RECT(); u32.GetClientRect(win, ctypes.byref(r)); w, h = r.right, r.bottom
    screen = u32.GetDC(win); mem = g32.CreateCompatibleDC(screen)
    class BIH(ctypes.Structure):
        _fields_ = [('biSize', W.DWORD), ('biWidth', W.LONG), ('biHeight', W.LONG), ('biPlanes', W.WORD), ('biBitCount', W.WORD), ('biCompression', W.DWORD),
                    ('biSizeImage', W.DWORD), ('biXPelsPerMeter', W.LONG), ('biYPelsPerMeter', W.LONG), ('biClrUsed', W.DWORD), ('biClrImportant', W.DWORD)]
    bi = BIH(); bi.biSize = ctypes.sizeof(bi); bi.biWidth = w; bi.biHeight = -h; bi.biPlanes = 1; bi.biBitCount = 32
    bits = ctypes.c_void_p(); dib = g32.CreateDIBSection(screen, ctypes.byref(bi), 0, ctypes.byref(bits), None, 0); g32.SelectObject(mem, dib)
    buf = (ctypes.c_uint32 * (w * h)).from_address(bits.value)
    def grab():
        # (a hidden desktop composes only when something presents: the page's animated corner, see PAGE)
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
        for y in range(int(90 * s), h, 8):   # a grid every 8 px, below the bar in every layout (its own text and icons are white)
            line = data[y * stride:(y + 1) * stride]
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
        # interrupt: the second press once the page is seen half-way (the app's first motion really under way)
        if a.scenario == 'interrupt' and not turned and marks and f['top'] is not None and 20 < f['top'] < 50: press(); turned = True
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
u32.CloseDesktop(hdesk)

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
    seen = [f for f in fr if f['top'] is not None]
    steps = [abs(b['top'] - a2['top']) for a2, b in zip(seen, seen[1:]) if b['t'] - a2['t'] < 40]
    stalls = [round(b['t'] - a2['t']) for a2, b in zip(fr, fr[1:]) if b['t'] - a2['t'] >= 40]
    report['capture_stalls_ms'] = stalls
    check(all(f['top'] is not None for f in fr), 'a page on every frame (never a frame without the page)', f"{sum(f['top'] is None for f in fr)} of {len(fr)} without")
    if not a.reduced:
        check(max(steps or [0]) <= a.max_step, f'no jump on screen: the page\'s top edge moves at most {a.max_step} DIP between two captures', f'largest step {max(steps or [0]):.1f} DIP over {len(steps)} close pairs (capture stalls: {stalls} ms)')
        # The app's own trace: the page's top edge at EVERY frame it drew (src/main.cpp « FOCUS motion trace »).
        log = (app / 'prototype.log').read_text(encoding='utf-8', errors='replace') if (app / 'prototype.log').exists() else ''
        traces = [[float(v) for v in line.split(':', 1)[1].split()] for line in log.splitlines() if 'FOCUS motion trace' in line]
        report['app_traces'] = traces
        worst = max((abs(b - a2) for t in traces for a2, b in zip(t, t[1:])), default=None)
        check(bool(traces) and worst is not None and worst <= 16, 'no jump in any frame the app drew: the page\'s edge moves at most 16 DIP per frame', f'{len(traces)} motions, {sum(len(t) for t in traces)} frames, largest step {worst if worst is None else round(worst, 1)} DIP')
        ends_ok = all(t and min(abs(t[-1] - 4), abs(t[-1] - 81)) < 1 for t in traces)
        check(ends_ok, 'every motion lands exactly on a layout (4 or 81 DIP)', [round(t[-1], 1) for t in traces if t])
    check(sum(f['white'] for f in fr) == 0, 'no white pixel in any frame', f"{sum(f['white'] > 0 for f in fr)} frames with white")
    check(sum(f['holes'] for f in fr) == 0, 'no hole inside the page in any frame (canvas, black or white where the page is)', f"{sum(f['holes'] > 0 for f in fr)} frames with holes")
    # (black outside the page is not judged here: on a hidden desktop PrintWindow does not render the window's own Direct2D
    # surface — the bar and the canvas inset come out black at rest; the night QA judges those from the screen)
    if not a.reduced:
        moving = [f for f in fr if f['top'] is not None and 5 < f['top'] < 80]
        check(len(moving) >= 4, 'the page\'s edge is seen in between (a motion, not a switch)', f'{len(moving)} frames between the two layouts')
    if a.scenario == 'interrupt' and tops:
        check(abs(tops[-1] - tops[0]) < 1.5, 'interrupted: back where it started', f'start {tops[0]:.1f}, end {tops[-1]:.1f} DIP')
    report['summary'] = {'frames': len(fr), 'largest_step_dip': max(steps or [0]), 'tops': [round(x, 1) for x in tops][:400]}
(out / 'report.json').write_text(json.dumps(report, indent=1), encoding='utf-8')
print(f"{len(report['checks']) - len(fails)} / {len(report['checks'])} checks passed; report {out / 'report.json'}")
sys.exit(1 if fails else 0)
