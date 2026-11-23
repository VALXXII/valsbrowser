"""Real-window QA: « Afficher tout » grows the downloads popover into the drawer without any flash at its edges
(owner report 2026-11-23: « pendant que la fenêtre grandit, des petits flashs blancs apparaissent autour, sur les
bords, puis disparaissent »).

    python tests/downloads/morph_flash_qa.py --app <built app dir> --out <scratch dir> [--rounds 6] [--baseline-app <older build>]

Cause: the drawer is a colour-keyed layered window (since 0.3.757); a present into it only reaches the pixels inside
the window region in force when it presents. The morph painted, then grew the region: every frame, the strip just added
showed what the window held there before (black on a new surface, earlier drawer pixels) until the next frame. Fixed
by keeping one region for the whole morph and keying out what the growing shape does not cover (one present per
frame), the window veiled until its first frame fills it. tests/downloads/download_panel_test.cpp checks that without a
screen (surface read through the region after every frame); this script checks what a person sees.

A throwaway portable copy of the app, its data in <out> (LOCALAPPDATA of the child points there too, onboarding off),
one local page (a 127.0.0.1 server of ours) painted in one flat light colour, a download history of missing files
(their icons are drawn at half opacity: nothing of the drawer is ever near black or pure white). Each round opens the
popover (a click posted on the toolbar button: the pointer never moves), invokes « Afficher tout » (the message UI
Automation's Invoke sends), copies the SCREEN (what DWM composed) around the drawer as fast as it can for 900 ms, then
closes the drawer with Échap. Rounds alternate a quick invoke (before the drawer is warmed, 150 ms) and a slow one
(700 ms); before round 3 the window is resized (a new surface: black where nothing was ever presented). Per frame:
  * foreign pixels in the drawer's area: near black (max channel <= 12), pure white (min >= 250) or the key colour
    (magenta): the drawer never draws them, the page is neither: they are what the window held before (the flashes);
  * hand-off hole: a strip inside the popover that the shape always covers: the page colour there means a frame
    with neither the popover nor the drawer (a blank hand-off);
  * duration: first to last changing frame, to compare with the spring (Settle(.32) = 470 ms; the morph's timing must
    not change).
--baseline-app runs the same rounds on an older build first and reports its counts (expect flashes before the fix);
only --app decides the exit code. Frames with foreign pixels or holes are saved as PNG in <out> (PIL if present).
The apps run in a job object (KILL_ON_JOB_CLOSE): they die with this script whatever happens.
Needs an unlocked session (screen captures). Run alone, at night: no other real-window suite at the same time.
"""
import argparse, ctypes, ctypes.wintypes as W, http.server, os, shutil, subprocess, sys, threading, time
from pathlib import Path

ap = argparse.ArgumentParser(); ap.add_argument('--app', required=True); ap.add_argument('--out', required=True)
ap.add_argument('--rounds', type=int, default=6); ap.add_argument('--baseline-app')
a = ap.parse_args()
out = Path(a.out).resolve(); shutil.rmtree(out, ignore_errors=True); out.mkdir(parents=True)

u32, k32, g32 = ctypes.windll.user32, ctypes.windll.kernel32, ctypes.windll.gdi32
u32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
for f, args, res in [
    (u32.PostMessageW, [W.HWND, W.UINT, W.WPARAM, W.LPARAM], W.BOOL), (u32.FindWindowExW, [W.HWND, W.HWND, W.LPCWSTR, W.LPCWSTR], W.HWND),
    (u32.GetWindowRect, [W.HWND, ctypes.POINTER(W.RECT)], W.BOOL), (u32.GetClientRect, [W.HWND, ctypes.POINTER(W.RECT)], W.BOOL),
    (u32.ClientToScreen, [W.HWND, ctypes.POINTER(W.POINT)], W.BOOL), (u32.IsWindowVisible, [W.HWND], W.BOOL), (u32.GetForegroundWindow, [], W.HWND),
    (u32.GetDpiForWindow, [W.HWND], W.UINT), (u32.GetDC, [W.HWND], W.HDC), (u32.ReleaseDC, [W.HWND, W.HDC], ctypes.c_int),
    (u32.SetForegroundWindow, [W.HWND], W.BOOL), (u32.SetWindowPos, [W.HWND, W.HWND, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, W.UINT], W.BOOL),
    (u32.GetClassNameW, [W.HWND, W.LPWSTR, ctypes.c_int], ctypes.c_int), (u32.GetWindowThreadProcessId, [W.HWND, ctypes.POINTER(W.DWORD)], W.DWORD),
    (g32.CreateCompatibleDC, [W.HDC], W.HDC), (g32.CreateDIBSection, [W.HDC, ctypes.c_void_p, W.UINT, ctypes.POINTER(ctypes.c_void_p), W.HANDLE, W.DWORD], W.HBITMAP),
    (g32.SelectObject, [W.HDC, W.HGDIOBJ], W.HGDIOBJ), (g32.BitBlt, [W.HDC, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, W.HDC, ctypes.c_int, ctypes.c_int, W.DWORD], W.BOOL),
    (g32.DeleteObject, [W.HGDIOBJ], W.BOOL), (g32.DeleteDC, [W.HDC], W.BOOL),
    (k32.CreateJobObjectW, [ctypes.c_void_p, W.LPCWSTR], W.HANDLE), (k32.SetInformationJobObject, [W.HANDLE, ctypes.c_int, ctypes.c_void_p, W.DWORD], W.BOOL),
    (k32.AssignProcessToJobObject, [W.HANDLE, W.HANDLE], W.BOOL), (k32.OpenProcess, [W.DWORD, W.BOOL, W.DWORD], W.HANDLE), (k32.CloseHandle, [W.HANDLE], W.BOOL)]:
    f.argtypes, f.restype = args, res

# ---- job object: the apps (and anything they start) are killed when this script's handle closes ----
class IO_COUNTERS(ctypes.Structure): _fields_ = [(n, ctypes.c_ulonglong) for n in ('r', 'w', 'o', 'rb', 'wb', 'ob')]
class BASIC(ctypes.Structure): _fields_ = [('a', ctypes.c_longlong), ('b', ctypes.c_longlong), ('LimitFlags', W.DWORD), ('c', ctypes.c_size_t), ('d', ctypes.c_size_t), ('e', W.DWORD), ('f', ctypes.c_size_t), ('g', W.DWORD), ('h', W.DWORD)]
class EXT(ctypes.Structure): _fields_ = [('Basic', BASIC), ('Io', IO_COUNTERS), ('p', ctypes.c_size_t), ('j', ctypes.c_size_t), ('pp', ctypes.c_size_t), ('jp', ctypes.c_size_t)]
job = k32.CreateJobObjectW(None, None)
info = EXT(); info.Basic.LimitFlags = 0x2000   # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
assert k32.SetInformationJobObject(job, 9, ctypes.byref(info), ctypes.sizeof(info)), 'job object'

PAGE = (0xF4, 0xF1, 0xE8)      # the page's flat colour (RGB): light, never pure white
HTML = b'<!doctype html><title>Morph</title><style>html,body{margin:0;height:100%;background:#f4f1e8}</style><body></body>'
class H(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200); self.send_header('Content-Type', 'text/html'); self.send_header('Cache-Control', 'no-store')
        self.send_header('Content-Length', str(len(HTML))); self.end_headers(); self.wfile.write(HTML)
    def log_message(self, *x): pass
srv = http.server.ThreadingHTTPServer(('127.0.0.1', 0), H); base = f'http://127.0.0.1:{srv.server_port}'
threading.Thread(target=srv.serve_forever, daemon=True).start()

WM_LBUTTONDOWN, WM_LBUTTONUP, WM_MOUSEMOVE, WM_KEYDOWN, VK_ESCAPE = 0x0201, 0x0202, 0x0200, 0x0100, 0x1B
INVOKE, VIEW_ALL = 0x8000 + 61, 2   # DownloadsUI::Host::InvokeMessage (UIA Invoke), ViewAllHit

def windows_of(pid, cls):
    found = []
    @ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    def each(h, _):
        p = W.DWORD(); u32.GetWindowThreadProcessId(h, ctypes.byref(p)); name = ctypes.create_unicode_buffer(64); u32.GetClassNameW(h, name, 64)
        if p.value == pid and name.value == cls: found.append(h)
        return True
    u32.EnumWindows(each, 0)
    return found[0] if found else None

def rect(h):
    r = W.RECT(); u32.GetWindowRect(h, ctypes.byref(r)); return r

class Grabber:   # one screen rectangle, as fast as possible
    def __init__(self, r): self.r, self.frames, self.stop = r, [], False
    def run(self):
        r = self.r; w, h = r.right - r.left, r.bottom - r.top; screen = u32.GetDC(None)
        bi = (ctypes.c_byte * 44)(); ctypes.memmove(bi, (ctypes.c_int32 * 11)(40, w, -h, 0x200001, 0, 0, 0, 0, 0, 0, 0), 44)
        mem = g32.CreateCompatibleDC(screen); bits = ctypes.c_void_p(); bmp = g32.CreateDIBSection(screen, bi, 0, ctypes.byref(bits), None, 0); g32.SelectObject(mem, bmp)
        while not self.stop:
            g32.BitBlt(mem, 0, 0, w, h, screen, r.left, r.top, 0x00CC0020 | 0x40000000)   # SRCCOPY | CAPTUREBLT
            self.frames.append((time.perf_counter(), ctypes.string_at(bits, w * h * 4)))
        g32.DeleteObject(bmp); g32.DeleteDC(mem); u32.ReleaseDC(None, screen)

# Pixel tests in C speed: one 0/1 byte per pixel and channel (bytes.translate), channels combined as integers.
def table(test): return bytes(1 if test(v) else 0 for v in range(256))
def mask(px, tables): return [int.from_bytes(px[k::4].translate(tables[k]), 'big') for k in range(3)]   # B, G, R
DARK, LIGHT, HIGH, LOW = table(lambda v: v <= 12), table(lambda v: v >= 250), table(lambda v: v >= 240), table(lambda v: v <= 16)
def near(c, tol=6): return table(lambda v: abs(v - c) <= tol)
PAGE_T = (near(PAGE[2]), near(PAGE[1]), near(PAGE[0]))
def foreign(px):
    b, g, r = mask(px, (DARK,) * 3); dark = b & g & r
    b, g, r = mask(px, (LIGHT,) * 3); white = b & g & r
    b, g, r = mask(px, (HIGH, LOW, HIGH)); key = b & g & r
    return (dark | white | key).bit_count()
def page_count(px):
    b, g, r = mask(px, PAGE_T); return (b & g & r).bit_count()
def crop(px, w, x0, y0, x1, y1):
    return b''.join(px[(y * w + x0) * 4:(y * w + x1) * 4] for y in range(y0, y1))

def save(name, px, w, h):
    try:
        from PIL import Image
        Image.frombuffer('RGBA', (w, h), px, 'raw', 'BGRA', 0, 1).convert('RGB').save(out / name)
    except ImportError:
        pass

def session(app_dir, label):
    """One app run: `rounds` morphs. Returns (foreign frames, hole frames, durations, valid)."""
    app = out / label / 'app'; shutil.copytree(app_dir, app)
    local = out / label / 'localappdata'; local.mkdir(parents=True)
    (app / 'session').write_text(f'browser-session-2\n0\n1\n"{base}/p"\n0\n', encoding='utf-8', newline='\n')
    (app / 'settings').write_text('browser-settings 1\nrestore-tabs=1\nconfirm-close-tabs=0\n', encoding='utf-8', newline='\n')
    now = int(time.time() * 1000); missing = out / label / 'gone'   # never created: every row is a missing file
    rows = ''.join(f'C\t{1000 * (k + 1)}\t{now - k * 3600_000}\t{missing / f"report-{k + 1}.bin"}\t{base}/f{k + 1}\t\n' for k in range(8))
    (app / 'downloads').write_text('browser-downloads 2\n' + rows, encoding='utf-8', newline='\n')
    proc = subprocess.Popen([str(app / 'browser.exe')], cwd=app, env=dict(os.environ, LOCALAPPDATA=str(local), BROWSER_NO_ONBOARDING='1'))
    hp = k32.OpenProcess(0x1F0FFF, False, proc.pid); k32.AssignProcessToJobObject(job, hp); k32.CloseHandle(hp)
    foreign_frames, holes, durations, valid = 0, 0, [], True
    try:
        main = None
        for _ in range(120):
            main = windows_of(proc.pid, 'LisiereBrowser')
            if main and u32.IsWindowVisible(main): break
            time.sleep(0.25)
        assert main, 'browser window'
        u32.SetWindowPos(main, None, 160, 120, 1180, 815, 0x0004 | 0x0010)   # a known, normal size
        u32.SetForegroundWindow(main); time.sleep(4)                          # the local page loads
        scale = u32.GetDpiForWindow(main) / 96; px = lambda dip: int(round(dip * scale))
        for n in range(a.rounds):
            if n == 2:   # a new surface: the drawer window grows with the browser window
                wr = rect(main); u32.SetWindowPos(main, None, wr.left, wr.top, wr.right - wr.left, wr.bottom - wr.top + px(48), 0x0004 | 0x0010); time.sleep(1)
            u32.SetForegroundWindow(main); time.sleep(0.3)
            client = W.RECT(); u32.GetClientRect(main, ctypes.byref(client)); width = client.right / scale
            at = (px(width - 55) & 0xFFFF) | (px(60) << 16)                   # the toolbar's Téléchargements button (chrome.h)
            for m in (WM_MOUSEMOVE, WM_LBUTTONDOWN, WM_LBUTTONUP): u32.PostMessageW(main, m, 1 if m == WM_LBUTTONDOWN else 0, at)
            popover = None
            for _ in range(40):
                popover = windows_of(proc.pid, 'LisiereDownloads')
                if popover and u32.IsWindowVisible(popover): break
                time.sleep(0.025)
            if not popover or not u32.IsWindowVisible(popover): print(f'  round {n + 1}: the popover did not open', flush=True); valid = False; continue
            time.sleep(0.15 if n % 2 == 0 else 0.7)
            pr = rect(popover)
            # Capture: the drawer's whole column (the owner's right 300 DIP, from the toolbar down).
            o = W.POINT(client.right - px(300), px(76)); u32.ClientToScreen(main, ctypes.byref(o))
            area = W.RECT(o.x, o.y, o.x + px(300), o.y + client.bottom - px(76) - px(12)); cw, ch = area.right - area.left, area.bottom - area.top   # above the window's rounded corner
            g = Grabber(area); t = threading.Thread(target=g.run); t.start()
            try:
                time.sleep(0.05); invoked = time.perf_counter()
                u32.PostMessageW(popover, INVOKE, VIEW_ALL, 0)
                time.sleep(0.9)
            finally:
                g.stop = True; t.join()
            drawer = windows_of(proc.pid, 'LisiereDownloadsDrawer')
            opened = bool(drawer and u32.IsWindowVisible(drawer))
            # Probe strip inside the popover that the growing shape always covers (popover-local DIP x 230..262, y 40..h-4).
            sx0, sx1 = pr.left - area.left + px(230), pr.left - area.left + px(262); sy0, sy1 = pr.top - area.top + px(40), pr.bottom - area.top - px(4)
            previous, changes, f_frames, h_frames = None, [], 0, 0
            for i, (ts, frame) in enumerate(g.frames):
                if frame == previous: continue
                previous = frame
                if ts >= invoked: changes.append(ts)
                bad = foreign(frame); hole = ts >= invoked and page_count(crop(frame, cw, sx0, sy0, sx1, sy1)) > (sx1 - sx0) * (sy1 - sy0) // 20
                if bad: f_frames += 1; save(f'{label}-r{n + 1}-{(ts - invoked) * 1000:+06.0f}ms-foreign{bad}.png', frame, cw, ch)
                if hole: h_frames += 1; save(f'{label}-r{n + 1}-{(ts - invoked) * 1000:+06.0f}ms-hole.png', frame, cw, ch)
            duration = (changes[-1] - changes[0]) * 1000 if len(changes) > 1 else 0
            print(f'  round {n + 1} ({"quick" if n % 2 == 0 else "slow"}{", new surface" if n == 2 else ""}): {len(g.frames)} grabs, '
                  f'{len(changes)} distinct frames after « Afficher tout », {f_frames} with foreign pixels, {h_frames} with a hand-off hole, '
                  f'morph on screen {duration:.0f} ms, drawer open: {opened}', flush=True)
            foreign_frames += f_frames; holes += h_frames; durations.append(duration); valid = valid and opened and len(changes) > 10
            if drawer: u32.PostMessageW(drawer, WM_KEYDOWN, VK_ESCAPE, 0)
            time.sleep(0.8)
        valid = valid and u32.GetForegroundWindow() in (main, windows_of(proc.pid, 'LisiereDownloadsDrawer'), windows_of(proc.pid, 'LisiereDownloads'))
    finally:
        @ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
        def close_own(h, _):
            p = W.DWORD(); u32.GetWindowThreadProcessId(h, ctypes.byref(p))
            if p.value == proc.pid and u32.IsWindowVisible(h): u32.PostMessageW(h, 0x0010, 0, 0)
            return True
        u32.EnumWindows(close_own, 0)
        try: proc.wait(timeout=15)
        except subprocess.TimeoutExpired: proc.kill()
    return foreign_frames, holes, durations, valid

fails = 0
try:
    if a.baseline_app:
        print(f'baseline {a.baseline_app}:', flush=True)
        bf, bh, bd, bv = session(Path(a.baseline_app).resolve(), 'baseline')
        print(f'baseline: {bf} frame(s) with foreign pixels, {bh} hand-off hole(s), morph {", ".join(f"{d:.0f}" for d in bd)} ms (valid run: {bv})', flush=True)
    print(f'app {a.app}:', flush=True)
    f, h, d, v = session(Path(a.app).resolve(), 'app')
    for ok, what in [(v, 'the run is valid (popover and drawer opened every round, window in front, enough frames)'),
                     (f == 0, 'no flash: nothing the window held before ever shows around the growing drawer'),
                     (h == 0, 'no blank hand-off: the popover area is always covered while it turns into the drawer'),
                     (all(350 <= x <= 650 for x in d), 'the morph lasts what its spring says (470 ms, its last sub-pixel steps invisible: about 430 ms of changing frames)')]:
        print(('ok   ' if ok else 'FAIL ') + what, flush=True); fails += 0 if ok else 1
finally:
    k32.CloseHandle(job)   # KILL_ON_JOB_CLOSE: nothing of ours survives
    srv.shutdown()
print(f'{fails} failure(s)'); sys.exit(1 if fails else 0)
