"""Real-window QA of the address bar while typing (bug « des fois le texte clignote un peu »).

    python tests/ui/edit_flicker_qa.py --app <built app dir> --out <scratch dir> [--rounds 12]

A throwaway portable copy of the app, its data in <out> (LOCALAPPDATA of the child points there too),
one local page (a 127.0.0.1 server of ours) and a history where « yourrr.example » is a frequent site, so typing « yourr » inline-completes to it.
Characters are posted to the address field (no injected input); a second thread copies the field
from the SCREEN (what DWM composed, what a person sees) as fast as it can meanwhile.

Every capture taken after the first character of a round must show the inline completion selected
(the highlight cell). A capture with text but no highlight is the typed text shown alone for a
moment, before its completion came back: the flicker. Blank captures (field erased) count too.
The app runs in a job object (KILL_ON_JOB_CLOSE): it dies with this script whatever happens.
Needs an unlocked session (screen captures). Run alone: no other real-window suite at the same time.
"""
import argparse, ctypes, ctypes.wintypes as W, http.server, os, shutil, subprocess, sys, threading, time
from pathlib import Path

ap = argparse.ArgumentParser(); ap.add_argument('--app', required=True); ap.add_argument('--out', required=True); ap.add_argument('--rounds', type=int, default=12)
a = ap.parse_args()
src, out = Path(a.app).resolve(), Path(a.out).resolve()
shutil.rmtree(out, ignore_errors=True); out.mkdir(parents=True)

u32, k32, g32 = ctypes.windll.user32, ctypes.windll.kernel32, ctypes.windll.gdi32
u32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
for f, args, res in [
    (u32.PostMessageW, [W.HWND, W.UINT, W.WPARAM, W.LPARAM], W.BOOL), (u32.SendMessageW, [W.HWND, W.UINT, W.WPARAM, W.LPARAM], W.LPARAM),
    (u32.FindWindowExW, [W.HWND, W.HWND, W.LPCWSTR, W.LPCWSTR], W.HWND), (u32.GetWindowRect, [W.HWND, ctypes.POINTER(W.RECT)], W.BOOL),
    (u32.IsWindowVisible, [W.HWND], W.BOOL), (u32.GetForegroundWindow, [], W.HWND), (u32.GetDpiForWindow, [W.HWND], W.UINT), (u32.GetDC, [W.HWND], W.HDC), (u32.ReleaseDC, [W.HWND, W.HDC], ctypes.c_int),
    (g32.CreateCompatibleDC, [W.HDC], W.HDC), (g32.CreateDIBSection, [W.HDC, ctypes.c_void_p, W.UINT, ctypes.POINTER(ctypes.c_void_p), W.HANDLE, W.DWORD], W.HBITMAP),
    (g32.SelectObject, [W.HDC, W.HGDIOBJ], W.HGDIOBJ), (g32.BitBlt, [W.HDC, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, W.HDC, ctypes.c_int, ctypes.c_int, W.DWORD], W.BOOL),
    (g32.DeleteObject, [W.HGDIOBJ], W.BOOL), (g32.DeleteDC, [W.HDC], W.BOOL), (u32.SetForegroundWindow, [W.HWND], W.BOOL),
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

class H(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        b = b'<!doctype html><title>Page locale</title><body style="background:#fff">Page locale de test</body>'
        self.send_response(200); self.send_header('Content-Type', 'text/html'); self.send_header('Content-Length', str(len(b))); self.end_headers(); self.wfile.write(b)
    def log_message(self, *x): pass
srv = http.server.ThreadingHTTPServer(('127.0.0.1', 0), H); base = f'http://127.0.0.1:{srv.server_port}'
threading.Thread(target=srv.serve_forever, daemon=True).start()

app = out / 'app'; shutil.copytree(src, app)
local = out / 'localappdata'; local.mkdir()
(app / 'session').write_text(f'browser-session-2\n0\n1\n"{base}/t"\n0\n', encoding='utf-8', newline='\n')
(app / 'settings').write_text('browser-settings 1\nrestore-tabs=1\nconfirm-close-tabs=0\n', encoding='utf-8', newline='\n')
# A site visited often (never opened here: only its name completes inline, as « yourrr » did for the user).
(app / 'history').write_text(f'browser-history 1\n20\t{int(time.time())}\thttps://yourrr.example/\tYourrr\n', encoding='utf-8', newline='\n')
proc = subprocess.Popen([str(app / 'browser.exe')], cwd=app, env=dict(os.environ, LOCALAPPDATA=str(local)))
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

class Grabber:
    def __init__(self, rect): self.rect, self.frames, self.stop = rect, [], False
    def run(self):
        r = self.rect; w, h = r.right - r.left, r.bottom - r.top
        bi = (ctypes.c_byte * 44)(); ctypes.memmove(bi, (ctypes.c_int32 * 11)(40, w, -h, 0x200001, 0, 0, 0, 0, 0, 0, 0), 44)
        screen = u32.GetDC(None); mem = g32.CreateCompatibleDC(screen); bits = ctypes.c_void_p()
        bmp = g32.CreateDIBSection(screen, bi, 0, ctypes.byref(bits), None, 0); g32.SelectObject(mem, bmp)
        while not self.stop:
            g32.BitBlt(mem, 0, 0, w, h, screen, r.left, r.top, 0x00CC0020)
            self.frames.append((time.perf_counter(), ctypes.string_at(bits, w * h * 4)))
        g32.DeleteObject(bmp); g32.DeleteDC(mem); u32.ReleaseDC(None, screen)

def reach(px, w, h):   # rightmost column holding text or the selection cell (the field colour is read at its right end)
    row = w * 4; base = [px[(h // 2) * row + (w - 3) * 4 + k] for k in range(3)]
    right = -1
    for y in range(h):
        for x in range(w - 1, right, -1):
            i = y * row + x * 4
            if abs(px[i] - base[0]) + abs(px[i + 1] - base[1]) + abs(px[i + 2] - base[2]) > 90: right = x; break
    return right

fails = 0
try:
    win = None
    for _ in range(120):
        win = main_window()
        if win: break
        time.sleep(0.25)
    assert win, 'browser window'
    time.sleep(4)   # the local page loads and is recorded in this profile's history
    u32.SetForegroundWindow(win); time.sleep(0.3)
    WM_CHAR, WM_SETTEXT, Shortcut = 0x0102, 0x000C, 0x8000 + 1
    u32.PostMessageW(win, Shortcut, ord('L'), 0); time.sleep(0.5)
    edit = u32.FindWindowExW(win, None, 'Edit', None)
    print(f'browser in the foreground: {u32.GetForegroundWindow() == win}, address field shown: {bool(u32.IsWindowVisible(edit))}', flush=True)
    scale = u32.GetDpiForWindow(edit) / 96
    r = W.RECT(); u32.GetWindowRect(edit, ctypes.byref(r))
    print(f'address field on screen: {r.left},{r.top} {r.right - r.left}x{r.bottom - r.top}', flush=True)
    typed = 'yourr'
    empty = ctypes.create_unicode_buffer('')   # WM_SETTEXT's string, alive for the whole run
    rounds = []
    g = Grabber(r); t = threading.Thread(target=g.run); t.start()
    try:
        for _ in range(a.rounds):
            if not u32.IsWindowVisible(edit): u32.PostMessageW(win, Shortcut, ord('L'), 0); time.sleep(0.3)   # editing again
            u32.SendMessageW(edit, WM_SETTEXT, 0, ctypes.addressof(empty)); time.sleep(0.15)
            ready = time.perf_counter()   # the emptied field is on screen by now
            first = None
            for i, c in enumerate(typed):
                u32.SendMessageW(edit, WM_CHAR, ord(c), 1)   # returns once the key and its EN_CHANGE work are done
                if i == 0: first = time.perf_counter()
                time.sleep(0.045)
            time.sleep(0.12)
            if u32.IsWindowVisible(edit): rounds.append((ready, first, time.perf_counter()))   # a round where the field lost the focus is not counted
    finally:
        g.stop = True; t.join()
    # « yourr » alone ends near 50 DIP; with its completion « yourrr.example » the text reaches past 100 DIP.
    w, h = r.right - r.left, r.bottom - r.top; limit = int(80 * scale)
    # The screen shows what DWM composed, up to a frame after the app drew it: right after the first key,
    # captures may still show the empty field of before. Those (equal to the empty field, the blinking
    # caret aside) are not counted until the first capture that shows anything else.
    def near(p, q): return sum(1 for i in range(0, len(p), 4) if p[i:i + 3] != q[i:i + 3]) <= 3 * h
    total = alone = blank = 0; samples = []
    for (t00, t0, t1) in rounds:
        before = [px for ts, px in g.frames if t00 <= ts < t0]
        reference = before[-1] if before else None
        prev = None; started = False
        for ts, px in g.frames:
            if ts < t0 or ts > t1 or px == prev: continue
            if not started and reference is not None and near(px, reference): continue
            started = True
            prev = px; total += 1
            right = reach(px, w, h)
            if right < 0: blank += 1; samples.append(('blank', px))
            elif right < limit: alone += 1; samples.append(('alone', px))
    print(f'{len(rounds)} rounds of « {typed} », {len(g.frames)} captures, {total} distinct field images while typing, {alone} without the inline completion, {blank} blank', flush=True)
    try:
        from PIL import Image
        for i, (kind, px) in enumerate(samples[:6]):
            Image.frombuffer('RGBA', (w, h), px, 'raw', 'BGRA', 0, 1).convert('RGB').save(out / f'flicker-{i}-{kind}.png')
        if g.frames: Image.frombuffer('RGBA', (w, h), g.frames[-1][1], 'raw', 'BGRA', 0, 1).convert('RGB').save(out / 'last.png')
    except ImportError:
        pass
    ok = len(rounds) >= a.rounds // 2 and total > 0 and alone == 0 and blank == 0
    print(('ok   ' if ok else 'FAIL ') + 'the address field never shows the typed text without its completion, nor an empty field, while typing', flush=True)
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
