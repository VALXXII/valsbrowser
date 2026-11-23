"""Real-window QA: the page never flashes away while it changes its title (bug « un très court flash où la page
disparaît et on voit le fond du navigateur », on clicks in a site).

    python tests/ui/title_flash_qa.py --app <built app dir> --out <scratch dir> [--seconds 20]

Cause: every title, address and history change of the page set the window title, and the window title was stored
with the main window briefly marked invisible, which hid the WebView2 page for that instant; a frame DWM composed
then showed the canvas (#171717) in place of the page. tests/ui/window_title_test.cpp checks the mechanism without a
screen; this script checks what a person sees.

A throwaway portable copy of the app, its data in <out> (LOCALAPPDATA of the child points there too, onboarding
off), one local page (a 127.0.0.1 server of ours) painted in one flat colour. Once the rest frames are taken, the
page changes its title and its address (history.replaceState) about 60 times a second, with no input at all; a
second thread copies the SCREEN (what DWM composed) as fast as it can meanwhile:
  * page square: a block in the middle of the page must never show the canvas colour (the flash);
  * caption band: the top 40 DIP of the window must never gain a classic Windows caption (light pixels well above
    the rest level: the tab titles are the only light pixels there).
Before the fix, expect flash frames (run it on an older build to see them); after, 0 and 0.
The app runs in a job object (KILL_ON_JOB_CLOSE): it dies with this script whatever happens.
Needs an unlocked session (screen captures). Run alone, at night: no other real-window suite at the same time.
"""
import argparse, ctypes, ctypes.wintypes as W, http.server, os, shutil, subprocess, sys, threading, time
from pathlib import Path

ap = argparse.ArgumentParser(); ap.add_argument('--app', required=True); ap.add_argument('--out', required=True); ap.add_argument('--seconds', type=float, default=20)
a = ap.parse_args()
src, out = Path(a.app).resolve(), Path(a.out).resolve()
shutil.rmtree(out, ignore_errors=True); out.mkdir(parents=True)

u32, k32, g32 = ctypes.windll.user32, ctypes.windll.kernel32, ctypes.windll.gdi32
u32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
for f, args, res in [
    (u32.PostMessageW, [W.HWND, W.UINT, W.WPARAM, W.LPARAM], W.BOOL), (u32.FindWindowExW, [W.HWND, W.HWND, W.LPCWSTR, W.LPCWSTR], W.HWND),
    (u32.GetWindowRect, [W.HWND, ctypes.POINTER(W.RECT)], W.BOOL), (u32.IsWindowVisible, [W.HWND], W.BOOL), (u32.GetForegroundWindow, [], W.HWND),
    (u32.GetDpiForWindow, [W.HWND], W.UINT), (u32.GetDC, [W.HWND], W.HDC), (u32.ReleaseDC, [W.HWND, W.HDC], ctypes.c_int),
    (u32.SetForegroundWindow, [W.HWND], W.BOOL), (u32.SetWindowPos, [W.HWND, W.HWND, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, W.UINT], W.BOOL),
    (u32.GetWindowTextW, [W.HWND, W.LPWSTR, ctypes.c_int], ctypes.c_int), (u32.GetWindowThreadProcessId, [W.HWND, ctypes.POINTER(W.DWORD)], W.DWORD),
    (g32.CreateCompatibleDC, [W.HDC], W.HDC), (g32.CreateDIBSection, [W.HDC, ctypes.c_void_p, W.UINT, ctypes.POINTER(ctypes.c_void_p), W.HANDLE, W.DWORD], W.HBITMAP),
    (g32.SelectObject, [W.HDC, W.HGDIOBJ], W.HGDIOBJ), (g32.BitBlt, [W.HDC, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, W.HDC, ctypes.c_int, ctypes.c_int, W.DWORD], W.BOOL),
    (g32.DeleteObject, [W.HGDIOBJ], W.BOOL), (g32.DeleteDC, [W.HDC], W.BOOL),
    (k32.CreateJobObjectW, [ctypes.c_void_p, W.LPCWSTR], W.HANDLE), (k32.SetInformationJobObject, [W.HANDLE, ctypes.c_int, ctypes.c_void_p, W.DWORD], W.BOOL),
    (k32.AssignProcessToJobObject, [W.HANDLE, W.HANDLE], W.BOOL), (k32.OpenProcess, [W.DWORD, W.BOOL, W.DWORD], W.HANDLE), (k32.CloseHandle, [W.HANDLE], W.BOOL)]:
    f.argtypes, f.restype = args, res

# ---- job object: the app (and anything it starts) is killed when this script's handle closes ----
class IO_COUNTERS(ctypes.Structure): _fields_ = [(n, ctypes.c_ulonglong) for n in ('r', 'w', 'o', 'rb', 'wb', 'ob')]
class BASIC(ctypes.Structure): _fields_ = [('a', ctypes.c_longlong), ('b', ctypes.c_longlong), ('LimitFlags', W.DWORD), ('c', ctypes.c_size_t), ('d', ctypes.c_size_t), ('e', W.DWORD), ('f', ctypes.c_size_t), ('g', W.DWORD), ('h', W.DWORD)]
class EXT(ctypes.Structure): _fields_ = [('Basic', BASIC), ('Io', IO_COUNTERS), ('p', ctypes.c_size_t), ('j', ctypes.c_size_t), ('pp', ctypes.c_size_t), ('jp', ctypes.c_size_t)]
job = k32.CreateJobObjectW(None, None)
info = EXT(); info.Basic.LimitFlags = 0x2000   # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
assert k32.SetInformationJobObject(job, 9, ctypes.byref(info), ctypes.sizeof(info)), 'job object'

PAGE = (0x3A, 0x7B, 0xD5)      # the page's flat colour
CANVAS = (0x17, 0x17, 0x17)    # the browser's canvas, what a flash shows
state = {'go': 'wait'}         # wait -> go -> stop (read by the page)
HTML = b'''<!doctype html><title>Titre 0</title><style>html,body{margin:0;height:100%;background:#3a7bd5}</style><body><script>
let n = 0, timer = 0;
setInterval(async () => {
  const s = await (await fetch('/state', {cache: 'no-store'})).text();
  if (s === 'go' && !timer) timer = setInterval(() => { n++; document.title = 'Titre ' + n; history.replaceState(null, '', '/p?' + n); }, 16);
  if (s === 'stop' && timer) { clearInterval(timer); timer = 0; }
}, 100);
</script></body>'''
class H(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        b = state['go'].encode() if self.path.startswith('/state') else HTML
        self.send_response(200); self.send_header('Content-Type', 'text/plain' if self.path.startswith('/state') else 'text/html')
        self.send_header('Cache-Control', 'no-store'); self.send_header('Content-Length', str(len(b))); self.end_headers(); self.wfile.write(b)
    def log_message(self, *x): pass
srv = http.server.ThreadingHTTPServer(('127.0.0.1', 0), H); base = f'http://127.0.0.1:{srv.server_port}'
threading.Thread(target=srv.serve_forever, daemon=True).start()

app = out / 'app'; shutil.copytree(src, app)
local = out / 'localappdata'; local.mkdir()
(app / 'session').write_text(f'browser-session-2\n0\n1\n"{base}/p"\n0\n', encoding='utf-8', newline='\n')
(app / 'settings').write_text('browser-settings 1\nrestore-tabs=1\nconfirm-close-tabs=0\n', encoding='utf-8', newline='\n')
proc = subprocess.Popen([str(app / 'browser.exe')], cwd=app, env=dict(os.environ, LOCALAPPDATA=str(local), BROWSER_NO_ONBOARDING='1'))
hp = k32.OpenProcess(0x1F0FFF, False, proc.pid); k32.AssignProcessToJobObject(job, hp); k32.CloseHandle(hp)

def main_window():
    found = []
    @ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    def each(h, _):
        pid = W.DWORD(); u32.GetWindowThreadProcessId(h, ctypes.byref(pid))
        if pid.value == proc.pid and u32.IsWindowVisible(h) and u32.FindWindowExW(h, None, 'Edit', None): found.append(h)
        return True
    u32.EnumWindows(each, 0)
    return found[0] if found else None

class Grabber:   # two screen regions per capture: the page square and the caption band
    def __init__(self, regions): self.regions, self.frames, self.stop = regions, [], False
    def run(self):
        screen = u32.GetDC(None); grabs = []
        for r in self.regions:
            w, h = r.right - r.left, r.bottom - r.top
            bi = (ctypes.c_byte * 44)(); ctypes.memmove(bi, (ctypes.c_int32 * 11)(40, w, -h, 0x200001, 0, 0, 0, 0, 0, 0, 0), 44)
            mem = g32.CreateCompatibleDC(screen); bits = ctypes.c_void_p()
            bmp = g32.CreateDIBSection(screen, bi, 0, ctypes.byref(bits), None, 0); g32.SelectObject(mem, bmp); grabs.append((r, w, h, mem, bmp, bits))
        while not self.stop:
            shot = []
            for r, w, h, mem, bmp, bits in grabs:
                g32.BitBlt(mem, 0, 0, w, h, screen, r.left, r.top, 0x00CC0020); shot.append(ctypes.string_at(bits, w * h * 4))
            self.frames.append((time.perf_counter(), shot))
        for r, w, h, mem, bmp, bits in grabs: g32.DeleteObject(bmp); g32.DeleteDC(mem)
        u32.ReleaseDC(None, screen)

# Pixel counts in C speed: one 0/1 byte per pixel and channel (bytes.translate), the three channels ANDed as integers.
def table(test): return bytes(1 if test(v) else 0 for v in range(256))
def near(c, tol=8): return table(lambda v: abs(v - c) <= tol)
def count(px, tables):   # tables: (blue, green, red); px: BGRA
    b, g, r = (int.from_bytes(px[k::4].translate(tables[k]), 'big') for k in range(3))
    return (b & g & r).bit_count()
LIGHT = (table(lambda v: v > 200),) * 3
CANVAS_T = (near(CANVAS[2]), near(CANVAS[1]), near(CANVAS[0]))
PAGE_T = (near(PAGE[2]), near(PAGE[1]), near(PAGE[0]))

fails = 0
try:
    win = None
    for _ in range(120):
        win = main_window()
        if win: break
        time.sleep(0.25)
    assert win, 'browser window'
    u32.SetWindowPos(win, None, 120, 1100, 1180, 815, 0x0004 | 0x0010)   # SWP_NOZORDER | SWP_NOACTIVATE: a known, normal size
    u32.SetForegroundWindow(win); time.sleep(4)   # the local page loads
    scale = u32.GetDpiForWindow(win) / 96
    wr = W.RECT(); u32.GetWindowRect(win, ctypes.byref(wr))
    cx, cy = (wr.left + wr.right) // 2, (wr.top + wr.bottom) // 2 + int(40 * scale)   # well inside the page (it starts 81 DIP down)
    side = int(160 * scale)
    square = W.RECT(cx - side // 2, cy - side // 2, cx + side // 2, cy + side // 2)
    band = W.RECT(wr.left + int(8 * scale), wr.top, wr.right - int(8 * scale), wr.top + int(40 * scale))
    print(f'window {wr.left},{wr.top} {wr.right - wr.left}x{wr.bottom - wr.top} at {scale * 100:.0f} %, foreground: {u32.GetForegroundWindow() == win}', flush=True)
    g = Grabber([square, band]); t = threading.Thread(target=g.run); t.start()
    try:
        time.sleep(1.5); rest_end = time.perf_counter()
        state['go'] = 'go'; time.sleep(0.5); start = time.perf_counter()
        time.sleep(a.seconds); end = time.perf_counter()
        state['go'] = 'stop'; time.sleep(0.3)
    finally:
        g.stop = True; t.join()
    title = ctypes.create_unicode_buffer(256); u32.GetWindowTextW(win, title, 256)
    foreground = u32.GetForegroundWindow() == win
    rest = [f for ts, f in g.frames if ts < rest_end]
    run = [f for ts, f in g.frames if start <= ts <= end]
    assert rest and run, 'captures'
    page_ok = all(count(f[0], PAGE_T) > len(f[0]) // 4 * 0.9 for f in rest)
    rest_light = max(count(f[1], LIGHT) for f in rest)
    flash, caption, samples = 0, 0, []
    for f in run:
        canvas = count(f[0], CANVAS_T)
        if canvas > len(f[0]) // 4 // 50: flash += 1; samples.append(('flash', f))
        if count(f[1], LIGHT) > rest_light + int(3000 * scale * scale): caption += 1; samples.append(('caption', f))
    print(f'last window title: « {title.value} » (it must have moved on from « Titre 0 »)', flush=True)
    print(f'{len(rest)} rest captures (page colour seen: {page_ok}, light px in the caption band at rest: {rest_light}), '
          f'{len(run)} captures over {end - start:.1f} s of title changes: {flash} with the canvas in place of the page, {caption} with a classic caption', flush=True)
    try:
        from PIL import Image
        for i, (kind, f) in enumerate(samples[:6]):
            Image.frombuffer('RGBA', (square.right - square.left, square.bottom - square.top), f[0], 'raw', 'BGRA', 0, 1).convert('RGB').save(out / f'{kind}-{i}-page.png')
            Image.frombuffer('RGBA', (band.right - band.left, band.bottom - band.top), f[1], 'raw', 'BGRA', 0, 1).convert('RGB').save(out / f'{kind}-{i}-band.png')
    except ImportError:
        pass
    ok = foreground and page_ok and title.value.startswith('Titre ') and title.value != 'Titre 0' and len(run) > 100
    print(('ok   ' if ok else 'FAIL ') + 'the run is valid (window in front, page painted, its title changing, enough captures)', flush=True)
    fails += 0 if ok else 1
    ok = flash == 0
    print(('ok   ' if ok else 'FAIL ') + 'the page never disappears (canvas) while its title changes', flush=True)
    fails += 0 if ok else 1
    ok = caption == 0
    print(('ok   ' if ok else 'FAIL ') + 'no classic Windows caption over the chrome while the title changes', flush=True)
    fails += 0 if ok else 1
finally:
    @ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    def close_own(h, _):
        pid = W.DWORD(); u32.GetWindowThreadProcessId(h, ctypes.byref(pid))
        if pid.value == proc.pid and u32.IsWindowVisible(h): u32.PostMessageW(h, 0x0010, 0, 0)
        return True
    u32.EnumWindows(close_own, 0)
    try: proc.wait(timeout=15)
    except subprocess.TimeoutExpired: pass
    k32.CloseHandle(job)   # KILL_ON_JOB_CLOSE: nothing of ours survives
    srv.shutdown()
print(f'{fails} failure(s)'); sys.exit(1 if fails else 0)
