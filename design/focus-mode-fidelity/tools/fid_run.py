"""Mode Focus fidelity audit: real-window captures (independent of the implementation's own QA script).

    python design/focus-mode-fidelity/tools/fid_run.py --app build/fid-head --out build/fid-run --dest design/focus-mode-fidelity/raw
        --monitor primary|secondary --page dark|white [--band] [--mode head|base] [--y 120]

mode=base: a build of main; only normal-mode captures (off at 1180 and 1170 DIP client).
mode=head: off at 1180 / 1170, then focus mode: hidden, hit box rows, shown, slide strips (in / out / reversal), drawer.
Captures copy the screen (DWM composition). Pointer by SetCursorPos (the app polls it); commands by the window's own
Shortcut message; clicks posted on the main window. Writes PNGs + measurements.json into <dest>/<tag>/.
"""
import argparse, ctypes, ctypes.wintypes as W, http.server, json, math, os, shutil, subprocess, sys, threading, time
from pathlib import Path
from PIL import Image

ap = argparse.ArgumentParser()
ap.add_argument('--app', required=True); ap.add_argument('--out', required=True); ap.add_argument('--dest', required=True)
ap.add_argument('--monitor', default='primary', choices=('primary', 'secondary')); ap.add_argument('--band', action='store_true')
ap.add_argument('--page', default='dark', choices=('dark', 'white')); ap.add_argument('--mode', default='head', choices=('head', 'base'))
ap.add_argument('--y', type=int, default=120)
a = ap.parse_args()
src, out = Path(a.app).resolve(), Path(a.out).resolve()
shutil.rmtree(out, ignore_errors=True); out.mkdir(parents=True)

u32, k32, g32 = ctypes.windll.user32, ctypes.windll.kernel32, ctypes.windll.gdi32
u32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
for f, args, res in [
    (u32.PostMessageW, [W.HWND, W.UINT, W.WPARAM, W.LPARAM], W.BOOL),
    (u32.GetWindowRect, [W.HWND, ctypes.POINTER(W.RECT)], W.BOOL), (u32.GetClientRect, [W.HWND, ctypes.POINTER(W.RECT)], W.BOOL),
    (u32.ClientToScreen, [W.HWND, ctypes.POINTER(W.POINT)], W.BOOL), (u32.IsWindowVisible, [W.HWND], W.BOOL), (u32.GetForegroundWindow, [], W.HWND),
    (u32.GetDpiForWindow, [W.HWND], W.UINT), (u32.GetDC, [W.HWND], W.HDC), (u32.ReleaseDC, [W.HWND, W.HDC], ctypes.c_int),
    (u32.SetWindowPos, [W.HWND, W.HWND, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, W.UINT], W.BOOL), (u32.ShowWindow, [W.HWND, ctypes.c_int], W.BOOL),
    (u32.SetCursorPos, [ctypes.c_int, ctypes.c_int], W.BOOL), (u32.SetForegroundWindow, [W.HWND], W.BOOL), (u32.GetParent, [W.HWND], W.HWND),
    (u32.GetClassNameW, [W.HWND, W.LPWSTR, ctypes.c_int], ctypes.c_int), (u32.GetWindowThreadProcessId, [W.HWND, ctypes.POINTER(W.DWORD)], W.DWORD),
    (g32.CreateCompatibleDC, [W.HDC], W.HDC), (g32.CreateDIBSection, [W.HDC, ctypes.c_void_p, W.UINT, ctypes.POINTER(ctypes.c_void_p), W.HANDLE, W.DWORD], W.HBITMAP),
    (g32.SelectObject, [W.HDC, W.HGDIOBJ], W.HGDIOBJ), (g32.BitBlt, [W.HDC, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, W.HDC, ctypes.c_int, ctypes.c_int, W.DWORD], W.BOOL),
    (g32.DeleteObject, [W.HGDIOBJ], W.BOOL), (g32.DeleteDC, [W.HDC], W.BOOL),
    (k32.CreateJobObjectW, [ctypes.c_void_p, W.LPCWSTR], W.HANDLE), (k32.SetInformationJobObject, [W.HANDLE, ctypes.c_int, ctypes.c_void_p, W.DWORD], W.BOOL),
    (k32.AssignProcessToJobObject, [W.HANDLE, W.HANDLE], W.BOOL), (k32.OpenProcess, [W.DWORD, W.BOOL, W.DWORD], W.HANDLE), (k32.CloseHandle, [W.HANDLE], W.BOOL)]:
    f.argtypes, f.restype = args, res

class IO_COUNTERS(ctypes.Structure): _fields_ = [(n, ctypes.c_ulonglong) for n in ('r', 'w', 'o', 'rb', 'wb', 'ob')]
class BASIC(ctypes.Structure): _fields_ = [('a', ctypes.c_longlong), ('b', ctypes.c_longlong), ('LimitFlags', W.DWORD), ('c', ctypes.c_size_t), ('d', ctypes.c_size_t), ('e', W.DWORD), ('f', ctypes.c_size_t), ('g', W.DWORD), ('h', W.DWORD)]
class EXT(ctypes.Structure): _fields_ = [('Basic', BASIC), ('Io', IO_COUNTERS), ('p', ctypes.c_size_t), ('j', ctypes.c_size_t), ('pp', ctypes.c_size_t), ('jp', ctypes.c_size_t)]
job = k32.CreateJobObjectW(None, None); info = EXT(); info.Basic.LimitFlags = 0x2000
assert k32.SetInformationJobObject(job, 9, ctypes.byref(info), ctypes.sizeof(info))

BG = '#0E0E0E' if a.page == 'dark' else '#FFFFFF'
PAGE = (f'<!doctype html><title>Page test</title><body style="margin:0;background:{BG};height:2400px">'
        f'<div style="position:absolute;left:24px;top:420px;width:8px;height:8px;background:#E00"></div></body>').encode()
class H(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200); self.send_header('Content-Type', 'text/html; charset=utf-8'); self.send_header('Content-Length', str(len(PAGE))); self.end_headers(); self.wfile.write(PAGE)
    def log_message(self, *x): pass
srv = http.server.ThreadingHTTPServer(('127.0.0.1', 47611), H); base = f'http://127.0.0.1:{srv.server_port}'
threading.Thread(target=srv.serve_forever, daemon=True).start()

app = out / 'app'; shutil.copytree(src, app, ignore=shutil.ignore_patterns('session', 'settings', 'profile', 'prototype.log', 'history', 'bookmarks', 'downloads'))
local = out / 'localappdata'; local.mkdir()
(app / 'session').write_text(f'browser-session-2\n0\n1\n"{base}/test"\n0\n', encoding='utf-8', newline='\n')
(app / 'settings').write_text('browser-settings 1\nrestore-tabs=1\nconfirm-close-tabs=0\nfocus-mode=0\n' + ('bookmarks-bar=always\n' if a.band else ''), encoding='utf-8', newline='\n')
proc = subprocess.Popen([str(app / 'browser.exe')], cwd=app, env=dict(os.environ, LOCALAPPDATA=str(local), BROWSER_NO_ONBOARDING='1'))
hp = k32.OpenProcess(0x1F0FFF, False, proc.pid); k32.AssignProcessToJobObject(job, hp); k32.CloseHandle(hp)

def windows_of(pid, cls=None):
    found = []
    @ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    def each(h, _):
        p = W.DWORD(); u32.GetWindowThreadProcessId(h, ctypes.byref(p)); buf = ctypes.create_unicode_buffer(128); u32.GetClassNameW(h, buf, 128)
        if p.value == pid and u32.IsWindowVisible(h) and (cls is None or buf.value == cls): found.append(h)
        return True
    u32.EnumWindows(each, 0); return found

win = None
for _ in range(160):
    w = windows_of(proc.pid, 'LisiereBrowser')
    if w: win = w[0]; break
    time.sleep(0.25)
assert win, 'browser window'
mons = []
@ctypes.WINFUNCTYPE(W.BOOL, W.HMONITOR, W.HDC, ctypes.POINTER(W.RECT), W.LPARAM)
def mon(h, dc, r, l):
    x = ctypes.c_uint(); y = ctypes.c_uint(); ctypes.windll.shcore.GetDpiForMonitor(h, 0, ctypes.byref(x), ctypes.byref(y))
    mons.append((r.contents.left, r.contents.top, r.contents.right, r.contents.bottom, x.value)); return True
u32.EnumDisplayMonitors(None, None, mon, 0)
primary = [m for m in mons if m[0] == 0 and m[1] == 0][0]; others = [m for m in mons if m is not primary]
M = primary if a.monitor == 'primary' or not others else others[0]
def place(wdip=1180, wpx=None):
    u32.ShowWindow(win, 9); time.sleep(.2)
    u32.SetWindowPos(win, None, M[0] + 60, M[1] + a.y, 400, 300, 0x0014); time.sleep(.4)
    d = u32.GetDpiForWindow(win) / 96
    u32.SetWindowPos(win, None, M[0] + 60, M[1] + a.y, wpx or round(wdip * d), round(705 * d), 0x0014); time.sleep(.8)
place()
scale = u32.GetDpiForWindow(win) / 96
tag = f"{a.mode}-{round(scale * 100)}-{a.page}" + ('-band' if a.band else '')
dest = Path(a.dest).resolve() / tag; shutil.rmtree(dest, ignore_errors=True); dest.mkdir(parents=True)
def S(v): return int(math.floor(v * scale + .5))
def client_rect():
    r = W.RECT(); u32.GetClientRect(win, ctypes.byref(r)); p = W.POINT(0, 0); u32.ClientToScreen(win, ctypes.byref(p))
    return W.RECT(p.x, p.y, p.x + r.right, p.y + r.bottom)
def front():
    for _ in range(8):
        if u32.GetForegroundWindow() == win: return True
        u32.keybd_event(0x12, 0, 0, 0); u32.keybd_event(0x12, 0, 2, 0)
        u32.SetForegroundWindow(win); time.sleep(.25)
    return u32.GetForegroundWindow() == win
def pointer_px(x, y): r = client_rect(); u32.SetCursorPos(r.left + x, r.top + y)
def pointer(x, y): pointer_px(S(x), S(y))
Shortcut, FocusModeCommand = 0x8000 + 1, 3070
def command(cmd): u32.PostMessageW(win, Shortcut, cmd, 0)
def click(x, y):
    l = (S(y) << 16) | (S(x) & 0xFFFF); u32.PostMessageW(win, 0x0201, 1, l); time.sleep(.05); u32.PostMessageW(win, 0x0202, 0, l)

def grab_rect(r):
    w, h = r.right - r.left, r.bottom - r.top
    bi = (ctypes.c_byte * 44)(); ctypes.memmove(bi, (ctypes.c_int32 * 11)(40, w, -h, 0x200001, 0, 0, 0, 0, 0, 0, 0), 44)
    screen = u32.GetDC(None); mem = g32.CreateCompatibleDC(screen); bits = ctypes.c_void_p()
    bmp = g32.CreateDIBSection(screen, bi, 0, ctypes.byref(bits), None, 0); g32.SelectObject(mem, bmp)
    ok = g32.BitBlt(mem, 0, 0, w, h, screen, r.left, r.top, 0x00CC0020); data = ctypes.string_at(bits, w * h * 4)
    g32.DeleteObject(bmp); g32.DeleteDC(mem); u32.ReleaseDC(None, screen)
    if not ok: raise SystemExit('screen grab failed (session locked?)')
    return Image.frombuffer('RGBA', (w, h), data, 'raw', 'BGRA', 0, 1).convert('RGB')
def shot(): return grab_rect(client_rect())
def save(img, name): img.save(dest / f'{name}.png'); print('  saved', name, flush=True)

class Burst:
    def __init__(self, rows): self.frames, self.stop, self.rows = [], False, rows
    def run(self):
        r = client_rect(); w = r.right - r.left; top = self.rows
        bi = (ctypes.c_byte * 44)(); ctypes.memmove(bi, (ctypes.c_int32 * 11)(40, w, -top, 0x200001, 0, 0, 0, 0, 0, 0, 0), 44)
        screen = u32.GetDC(None); mem = g32.CreateCompatibleDC(screen); bits = ctypes.c_void_p()
        bmp = g32.CreateDIBSection(screen, bi, 0, ctypes.byref(bits), None, 0); g32.SelectObject(mem, bmp)
        while not self.stop:
            g32.BitBlt(mem, 0, 0, w, top, screen, r.left, r.top, 0x00CC0020)
            self.frames.append((time.perf_counter(), ctypes.string_at(bits, w * top * 4)))
        g32.DeleteObject(bmp); g32.DeleteDC(mem); u32.ReleaseDC(None, screen); self.w = w
def burst(seconds, actions):
    """actions: list of (delay_s_from_t0, fn)."""
    b = Burst(min(client_rect().bottom - client_rect().top, S(170))); t = threading.Thread(target=b.run); t.start(); time.sleep(.03)
    t0 = time.perf_counter()
    for dt, fn in actions:
        while time.perf_counter() - t0 < dt: time.sleep(.001)
        fn()
    while time.perf_counter() - t0 < seconds: time.sleep(.005)
    b.stop = True; t.join()
    keep, last = [], None
    for ts, data in b.frames:
        if data != last: keep.append((1000 * (ts - t0), Image.frombuffer('RGBA', (b.w, b.rows), data, 'raw', 'BGRA', 0, 1).convert('RGB'))); last = data
    return keep

m = {'tag': tag, 'scale': scale, 'page': a.page, 'band': a.band, 'mode': a.mode}
try:
    time.sleep(5); m['front'] = front()
    r = client_rect(); m['client_px'] = (r.right - r.left, r.bottom - r.top)
    pointer(590, 420); time.sleep(.6); save(shot(), 'off-1180')
    # normal chrome laid out on W - 10 DIP (what the panel lays out on); pointer at the panel's equivalent spot
    for wpx in sorted({math.floor(1170 * scale), math.ceil(1170 * scale)}):
        place(wpx=wpx); front(); pointer(590, 420); time.sleep(.6); save(shot(), f'off-w{wpx}px')
    place(); front(); time.sleep(.3)
    if a.mode == 'head':
        pointer(590, 420); command(FocusModeCommand); time.sleep(1.0); front(); time.sleep(.3)
        save(shot(), 'on-hidden')
        # hit box: pixel rows (client px) around the 14 DIP edge, at three x positions
        hb = {}
        W_px = r.right - r.left
        edge = math.ceil(14 * scale) if (14 * scale) != int(14 * scale) else int(14 * scale)  # first px row whose DIP y >= 14
        for x in (0, W_px // 2, W_px - 1):
            for y in (edge - 1, edge):
                pointer(590, 420); time.sleep(.9); front(); pointer_px(x, y); time.sleep(.7)
                img = shot(); px = img.getpixel((S(860) if x != W_px - 1 else S(300), S(40)))
                hb[f'x{x}_y{y}'] = {'pixel_at_40dip': px, 'revealed': px != img.getpixel((S(860), S(300)))}
        m['hitbox'] = hb; m['hitbox_edge_px'] = edge
        # shown at rest (pointer in the hit box, then over the panel's top padding)
        pointer(590, 420); time.sleep(.9); front()
        fr = burst(.6, [(0, lambda: pointer(700, 6))]); m['slide_in_frames'] = len(fr)
        for i, (t, img) in enumerate(fr): img.save(dest / f'slidein-{i:02d}-{t:06.1f}ms.png')
        time.sleep(.3); save(shot(), 'on-shown')
        # slide out
        fr = burst(1.0, [(0, lambda: pointer(700, 420))])
        for i, (t, img) in enumerate(fr): img.save(dest / f'slideout-{i:02d}-{t:06.1f}ms.png')
        time.sleep(.3); save(shot(), 'on-hidden-after')
        # reversal: slide-out interrupted at ~70 ms by the pointer back at the top edge
        front(); pointer(700, 6); time.sleep(.8)
        fr = burst(1.2, [(0, lambda: pointer(700, 420)), (0.30 + 0.07, lambda: pointer(700, 6))])
        for i, (t, img) in enumerate(fr): img.save(dest / f'reversal-{i:02d}-{t:06.1f}ms.png')
        time.sleep(.5)
        # keyboard reveal (Ctrl+L): appears at once
        pointer(590, 600); time.sleep(1.0); front()
        fr = burst(.3, [(0, lambda: command(ord('L')))])
        for i, (t, img) in enumerate(fr): img.save(dest / f'ctrlL-{i:02d}-{t:06.1f}ms.png')
        u32.FindWindowExW.argtypes = [W.HWND, W.HWND, W.LPCWSTR, W.LPCWSTR]; u32.FindWindowExW.restype = W.HWND
        edit = u32.FindWindowExW(win, None, 'Edit', None)
        if edit: u32.PostMessageW(edit, 0x0100, 0x1B, 0)
        time.sleep(1.0)
        # drawer: Réglages from the panel's gear
        front(); pointer(700, 6); time.sleep(.6); pointer(1180 - 18 - 5, 60); time.sleep(.3)
        click(1180 - 4 - 1 - 18, 4 + 1 + 60); time.sleep(1.0)
        drawers = windows_of(proc.pid, 'LisiereSettingsDrawer')
        if drawers:
            dr = W.RECT(); u32.GetWindowRect(drawers[0], ctypes.byref(dr)); r = client_rect()
            m['drawer_px'] = {'top': dr.top - r.top, 'left': dr.left - r.left, 'right_gap': r.right - dr.right, 'bottom_gap': r.bottom - dr.bottom}
            pointer(300, 500); time.sleep(1.0); save(shot(), 'drawer')
            u32.PostMessageW(drawers[0], 0x0100, 0x1B, 0); time.sleep(1.2)
        else: m['drawer_px'] = None
        command(FocusModeCommand); time.sleep(.8); place(); front(); pointer(590, 420); time.sleep(.6); save(shot(), 'off-again')
finally:
    (dest / 'run.json').write_text(json.dumps(m, indent=1, default=str), encoding='utf-8'); print(json.dumps(m, indent=1, default=str))
    try: proc.kill()
    except Exception: pass
