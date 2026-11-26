"""Our print window (#121) in the real browser window — NIGHT RUN ONLY (it takes the screen: captures, our own window
brought forward). Daytime checks without a screen: tests/print/run.py.

    python tests/print/print_qa.py --app <fresh build dir> --out <empty-able dir>

Uses throwaway copies of a built app (their own portable profile next to the exe: never the owner's profile), pages
served from 127.0.0.1 only (tests/print/site), the browser in a job object with KILL_ON_JOB_CLOSE, and the QA hooks of
src/print-integration.h under --workspace-test: fake printers (BROWSER_PRINT_QA_PRINTERS), a sink that receives instead
of any printer the PDF and settings a printer would get (BROWSER_PRINT_QA_SINK), a job held on screen
(BROWSER_PRINT_QA_DELAY), a failing job (BROWSER_PRINT_QA_FAIL=offline), a preview held (BROWSER_PRINT_QA_PREVIEW_DELAY) or
refused (BROWSER_PRINT_QA_NO_PREVIEW). Nothing ever reaches a real printer and Windows Settings never opens. Controls are
found and clicked through their UI Automation bounds (posted mouse messages to our own windows, no SendInput).

Paper references: tests/print/paper/ (« Impression · A » and « Impression · états »). Captures are compared pixel by
pixel (1 px slack, the exports sit half a pixel off the grid) over the regions the boards define; the sheets' insides
are excluded where the boards show a mock page. Results: <out>/result.json, captures and diffs in <out>/shots.

Session « main » (printers « QA Laser », « QA & Jet »):
  entrance    night 24/11 (~15 % opacity) and release 25/11 (alpha 0): the browser's motion loop never gave the window's
              fade its frames. 0.6 s after Ctrl+P the panel's layered alpha is 255, the preview column is #0F0F0F and
              the page beside the panel is darkened by the 50 % scrim
  loading     state 5 right after Ctrl+P (preview held 2.5 s): « Calcul des feuilles… », « Préparation de l’aperçu… »
  open        our window (class BrowserPrintDialog), never the engine's preview; vs modal-*.png
  long        the long page's first sheet within 3 s of the preview (time taken by a watcher thread, apart from the UI
              Automation reads); the UI thread answered WM_NULL within 200 ms
  pages       « N feuilles », « Page 1 sur N » (N > 10); next / previous turn the page
  hover_focus state 9: Paysage hovered, Tab focus rings (captures for the review)
  choose      state 1: « Choisir », thumbnails with boxes; ticking page 3 writes it in the field
  beyond      state 2: « 999 »: « Le document n’a que N pages. », action off; vs state2 (settings column)
  printer     list under the select; « QA Laser » chosen with ↓ Entrée: « Imprimer »
  landscape   state 3: « Paysage »: the sheet wider than tall; vs state3 (settings column)
  printing    state 7: the window closes, the C8 toast « Impression en cours… » with the printer and « Annuler »;
              it goes when the job ends; the sink receives printer, landscape, ranges 2-3
  cancel      « Annuler » on the toast: the job never reaches the sink
  pdf         « Enregistrer en PDF »: a PDF in <out>/downloads
  page_print  window.print() from the page opens our window; frame_print: a frame's keeps the engine's (recorded)
  narrow      state 10: page area under 760: 400 wide, one column, « Aperçu » chip; vs state10 (chip excluded)
Session « fail » (printer « QA Laser », every job fails « hors ligne »):
  error       state 8: the toast « Impression impossible · L’imprimante est hors ligne », « Réessayer », ✕; vs state8;
              it stays (8 s); Réessayer prints again; ✕ closes it
Session « bare » (no printer, preview refused):
  unavailable state 6: « Aperçu indisponible », the action stays; vs state6
  no_printer  state 4: the list « Aucune imprimante installée », « Ajouter une imprimante… »; vs state4 (settings + list)
"""
from __future__ import annotations
import argparse, ast, base64, ctypes, ctypes.wintypes as W, functools, http.server, json, os, re, shutil, socket, struct, subprocess, sys, threading, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
PAPER = HERE / 'paper'
tree = ast.parse((ROOT / 'tests/annotations/sandbox_qa.py').read_text(encoding='utf-8'))   # its stdlib CDP transport only
exec(compile(ast.Module(body=[n for n in tree.body if isinstance(n, ast.ClassDef) and n.name == 'WS'], type_ignores=[]), '<cdp-transport>', 'exec'), globals())

u32 = ctypes.windll.user32
# Per-monitor DPI aware, as the browser: UIA gives physical pixels, and ScreenToClient of an unaware process would scale
# them (125 % screen: every posted click landed off its control; night 25/11).
u32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
k32 = ctypes.WinDLL('kernel32', use_last_error=True)
ntdll = ctypes.WinDLL('ntdll')
u32.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
u32.SendMessageTimeoutW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM, W.UINT, W.UINT, ctypes.POINTER(ctypes.c_size_t)]
k32.CreateJobObjectW.restype = W.HANDLE
k32.CreateJobObjectW.argtypes = [ctypes.c_void_p, W.LPCWSTR]
k32.SetInformationJobObject.argtypes = [W.HANDLE, ctypes.c_int, ctypes.c_void_p, W.DWORD]
k32.AssignProcessToJobObject.argtypes = [W.HANDLE, W.HANDLE]
ntdll.NtResumeProcess.argtypes = [W.HANDLE]
WM_APP, PRINT_ACTIVE = 0x8000, 3002          # src/main.cpp: Shortcut = WM_APP+1, ShortcutCommand PrintActive
WM_KEYDOWN, WM_CHAR, WM_LBUTTONDOWN, WM_LBUTTONUP, WM_MOUSEMOVE, WM_CLOSE, WM_NULL = 0x100, 0x102, 0x201, 0x202, 0x200, 0x10, 0
VK_TAB, VK_RETURN, VK_ESCAPE, VK_DOWN = 0x09, 0x0D, 0x1B, 0x28


class IoCounters(ctypes.Structure):
    _fields_ = [(n, ctypes.c_ulonglong) for n in ('r', 'w', 'o', 'rb', 'wb', 'ob')]


class BasicLimit(ctypes.Structure):
    _fields_ = [('PerProcessUserTimeLimit', ctypes.c_longlong), ('PerJobUserTimeLimit', ctypes.c_longlong), ('LimitFlags', W.DWORD),
                ('MinimumWorkingSetSize', ctypes.c_size_t), ('MaximumWorkingSetSize', ctypes.c_size_t), ('ActiveProcessLimit', W.DWORD),
                ('Affinity', ctypes.c_size_t), ('PriorityClass', W.DWORD), ('SchedulingClass', W.DWORD)]


class ExtendedLimit(ctypes.Structure):
    _fields_ = [('BasicLimitInformation', BasicLimit), ('IoInfo', IoCounters), ('ProcessMemoryLimit', ctypes.c_size_t),
                ('JobMemoryLimit', ctypes.c_size_t), ('PeakProcessMemoryUsed', ctypes.c_size_t), ('PeakJobMemoryUsed', ctypes.c_size_t)]


class Quiet(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *a):
        pass


def free_port():
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0)); return s.getsockname()[1]


def windows_of(pid: int, cls: str | None = None, visible=True) -> list[int]:
    found = []
    proc = ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)

    def each(h, _):
        p = W.DWORD(); u32.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value == pid and (not visible or u32.IsWindowVisible(h)):
            if cls:
                b = ctypes.create_unicode_buffer(128); u32.GetClassNameW(h, b, 128)
                if b.value != cls:
                    return True
            found.append(h)
        return True
    u32.EnumWindows(proc(each), 0)
    return found


def rect_of(h) -> tuple[int, int, int, int]:
    r = W.RECT()
    if ctypes.windll.dwmapi.DwmGetWindowAttribute(W.HWND(h), 9, ctypes.byref(r), ctypes.sizeof(r)) != 0:
        u32.GetWindowRect(h, ctypes.byref(r))
    return r.left, r.top, r.right, r.bottom


def grab(path: Path, box):
    from PIL import ImageGrab   # needs an unlocked session
    path.parent.mkdir(parents=True, exist_ok=True)
    im = ImageGrab.grab(bbox=box, all_screens=True); im.save(path); return im


u32.GetLayeredWindowAttributes.argtypes = [W.HWND, ctypes.POINTER(W.DWORD), ctypes.POINTER(ctypes.c_ubyte), ctypes.POINTER(W.DWORD)]


def layered_alpha(h) -> int:
    key, alpha, flags = W.DWORD(), ctypes.c_ubyte(), W.DWORD()
    return alpha.value if h and u32.GetLayeredWindowAttributes(h, ctypes.byref(key), ctypes.byref(alpha), ctypes.byref(flags)) else -1


def uia(pid: int) -> list[dict]:
    """UI Automation nodes of our print window and toast: id, name, help, enabled, toggle, selected, rect (screen px).

    Read from our windows' own handles (AutomationElement.FromHandle): never a search through the browser's window,
    whose subtree holds the page's whole accessibility tree (release 25/11: tens of seconds on the long page)."""
    handles = windows_of(pid, 'BrowserPrintDialog') + windows_of(pid, 'LisiereContextToast')
    if not handles:
        return []
    script = r'''
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
# UTF-8 out: a redirected Windows PowerShell writes in the OEM code page (« Préparation » came back « Pr?paration »,
# « … » as « . »).
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)
$out = @()
$wins = @()
foreach ($h in @(''' + ','.join(str(int(h)) for h in handles) + r''')) { try { $wins += [System.Windows.Automation.AutomationElement]::FromHandle([IntPtr][long]$h) } catch {} }
foreach ($w in $wins) {
  foreach ($e in $w.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition)) {
    $c = $e.Current; $r = $c.BoundingRectangle; $t = $null; $s = $null
    try { $t = $e.GetCurrentPattern([System.Windows.Automation.TogglePattern]::Pattern).Current.ToggleState.ToString() } catch {}
    try { $s = $e.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Current.IsSelected } catch {}
    $out += @{ window = $w.Current.ClassName; id = $c.AutomationId; name = $c.Name; help = $c.HelpText; enabled = $c.IsEnabled; toggle = $t; selected = $s;
               rect = @([int]$r.Left, [int]$r.Top, [int]$r.Right, [int]$r.Bottom) } } }
ConvertTo-Json -InputObject $out -Depth 4 -Compress'''
    r = subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-Command', script], capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=120)
    try:
        v = json.loads(r.stdout.strip() or '[]'); return v if isinstance(v, list) else [v]
    except json.JSONDecodeError:
        return []


def node(nodes, ident, name=None, window=None):
    """The first node with this automation id (any id when empty), name and window class (when given)."""
    for n in nodes:
        if (not ident or n.get('id') == ident) and (name is None or n.get('name') == name) and (window is None or n.get('window') == window):
            return n
    return None


def compare(shot, ref: Path, out: Path, k: float, keep=None, skip=()):
    """Share of pixels off by more than 40 (1 px slack), over `keep` (DIP box) minus `skip` boxes; diff image to `out`."""
    from PIL import Image
    if not ref.is_file():
        return None
    paper = Image.open(ref).convert('RGB'); ours = shot.convert('RGB')
    if ours.size != paper.size:
        ours = ours.resize(paper.size)
    a, b = ours.load(), paper.load(); w, h = paper.size; s = w / (paper.size[0] / k) if k else 1
    box = [round(v * k) for v in keep] if keep else [0, 0, w, h]
    skips = [[round(v * k) for v in r] for r in skip]
    diff = Image.new('RGB', paper.size); d = diff.load(); off = n = 0
    for y in range(max(2, box[1]), min(h - 2, box[3])):
        for x in range(max(2, box[0]), min(w - 2, box[2])):
            if any(r[0] <= x < r[2] and r[1] <= y < r[3] for r in skips):
                continue
            n += 1; best = 255
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    p, q = a[x, y], b[x + dx, y + dy]; best = min(best, max(abs(p[0] - q[0]), abs(p[1] - q[1]), abs(p[2] - q[2])))
            off += best > 40; g = b[x, y][1] // 3; d[x, y] = (255, g, g) if best > 40 else (g, g, g)
    diff.save(out)
    return round(off / max(1, n), 4)


class Browser:
    def __init__(self, src: Path, out: Path, name: str, base: str, env_extra: dict):
        self.out = out / name; shutil.rmtree(self.out, ignore_errors=True); self.app = self.out / 'app'; self.app.mkdir(parents=True)
        for f in ('browser.exe', 'WebView2Loader.dll', 'version.json'):
            shutil.copy2(src / f, self.app / f)
        shutil.copytree(src / 'assets', self.app / 'assets')
        self.downloads, self.sink, self.shots = self.out / 'downloads', self.out / 'sink', out / 'shots'
        for d in (self.downloads, self.sink, self.shots):
            d.mkdir(parents=True, exist_ok=True)
        (self.app / 'session').write_text(f'browser-session-2\n0\n1\n"{base}/long.html"\n0\n', encoding='utf-8', newline='\n')
        (self.app / 'settings').write_text(f'browser-settings 1\nrestore-tabs=1\nconfirm-close-tabs=0\ndownload-folder={self.downloads}\n', encoding='utf-8', newline='\n')
        self.port = free_port()
        env = dict(os.environ, WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS=f'--remote-debugging-port={self.port} --remote-debugging-address=127.0.0.1', BROWSER_PRINT_QA_SINK=str(self.sink))
        env.update(env_extra)
        self.job = k32.CreateJobObjectW(None, None)
        info = ExtendedLimit(); info.BasicLimitInformation.LimitFlags = 0x2000   # KILL_ON_JOB_CLOSE
        k32.SetInformationJobObject(self.job, 9, ctypes.byref(info), ctypes.sizeof(info))
        self.proc = subprocess.Popen([str(self.app / 'browser.exe'), '--workspace-test'], cwd=self.app, env=env, creationflags=0x4)
        k32.AssignProcessToJobObject(self.job, W.HANDLE(int(self.proc._handle))); ntdll.NtResumeProcess(W.HANDLE(int(self.proc._handle)))
        self.pid = self.proc.pid; self.ws = None; self.main = 0; self.k = 1.0

    def log(self):
        p = self.app / 'prototype.log'
        return p.read_text(encoding='utf-8', errors='replace') if p.exists() else ''

    def ready(self):
        import urllib.request
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))

        def page():
            try:
                for t in json.loads(opener.open(f'http://127.0.0.1:{self.port}/json', timeout=3).read()):
                    if t.get('type') == 'page' and t.get('url', '').endswith('/long.html'):
                        return WS(t['webSocketDebuggerUrl'])
            except OSError:
                pass
            return None
        self.ws = wait(page, 40, .5)
        if not self.ws:
            raise RuntimeError('the long page did not load')
        wait(lambda: self.ev('document.readyState') == 'complete', 20)
        self.main = max(windows_of(self.pid), key=lambda h: (lambda r: (r[2] - r[0]) * (r[3] - r[1]))(rect_of(h)))
        self.k = u32.GetDpiForWindow(self.main) / 96
        self.size(1180, 815)

    def size(self, w, h):   # the board's window: low on its monitor (floating widgets of other apps sit high)
        u32.ShowWindow(self.main, 1); u32.SetWindowPos(self.main, 0, 40, 200, round(w * self.k), round(h * self.k), 0x0040)
        u32.SetForegroundWindow(self.main); time.sleep(1)

    def ev(self, code):
        r = self.ws.call('Runtime.evaluate', {'expression': code, 'returnByValue': True, 'awaitPromise': True, 'userGesture': True})
        return r.get('result', {}).get('value', r)

    def dialog(self):
        hs = windows_of(self.pid, 'BrowserPrintDialog'); return hs[0] if hs else 0

    def toast(self):
        hs = windows_of(self.pid, 'LisiereContextToast'); return hs[0] if hs else 0

    def ctrl_p(self):
        u32.PostMessageW(self.main, WM_APP + 1, PRINT_ACTIVE, 0); return wait(self.dialog, 5)

    def click_rect(self, hwnd, rect, move_only=False):
        pt = W.POINT((rect[0] + rect[2]) // 2, (rect[1] + rect[3]) // 2); u32.ScreenToClient(hwnd, ctypes.byref(pt)); lp = (pt.y << 16) | (pt.x & 0xFFFF)
        u32.PostMessageW(hwnd, WM_MOUSEMOVE, 0, lp)
        if not move_only:
            u32.PostMessageW(hwnd, WM_LBUTTONDOWN, 1, lp); u32.PostMessageW(hwnd, WM_LBUTTONUP, 0, lp)
        time.sleep(.5)

    def click(self, ident, name=None, hwnd=None, move_only=False):
        n = node(uia(self.pid), ident, name, 'LisiereContextToast' if hwnd and hwnd == self.toast() else 'BrowserPrintDialog')
        if not n:
            return False
        self.click_rect(hwnd or self.dialog(), n['rect'], move_only); return True

    def key(self, vk, hwnd=None):
        u32.PostMessageW(hwnd or self.dialog(), WM_KEYDOWN, vk, 0); time.sleep(.3)

    def type(self, text):
        edit = u32.FindWindowExW(self.dialog(), 0, 'Edit', None)
        for ch in text:
            u32.PostMessageW(edit, WM_CHAR, ord(ch), 0)
        time.sleep(.6)

    def previews(self):
        return len(re.findall(r'PRINT preview \d+ page', self.log()))

    def shot(self, name, hwnd=None):
        return grab(self.shots / f'{name}.png', rect_of(hwnd or self.dialog()))

    def close(self):
        try:
            for h in windows_of(self.pid):
                u32.PostMessageW(h, WM_CLOSE, 0, 0)
            self.proc.wait(15)
        except Exception:
            pass
        k32.CloseHandle(self.job)


def wait(fn, timeout=10.0, step=.15):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        v = fn()
        if v:
            return v
        time.sleep(step)
    return fn()


def main() -> int:
    ap = argparse.ArgumentParser(); ap.add_argument('--app', required=True); ap.add_argument('--out', required=True)
    a = ap.parse_args()
    src, out = Path(a.app).resolve(), Path(a.out).resolve()
    shutil.rmtree(out, ignore_errors=True); out.mkdir(parents=True)
    srv = http.server.ThreadingHTTPServer(('127.0.0.1', 0), functools.partial(Quiet, directory=str(HERE / 'site')))
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    base = f'http://127.0.0.1:{srv.server_port}'
    res = {'checks': {}, 'failures': [], 'notes': {}}

    def check(name, ok, **seen):
        res['checks'][name] = {'ok': bool(ok), **seen}
        if not ok:
            res['failures'].append(name)
        print(('ok   ' if ok else 'FAIL ') + name + ' ' + json.dumps(seen, ensure_ascii=False, default=str)[:500], flush=True)

    def ref(name, k):
        p = PAPER / (name.replace('.png', '-1.25x.png') if abs(k - 1.25) < .01 else name)
        return p if p.is_file() else PAPER / name

    # ---------------------------------------------------------------- session « main »
    b = Browser(src, out, 'main', base, {'BROWSER_PRINT_QA_PRINTERS': 'QA Laser;QA & Jet', 'BROWSER_PRINT_QA_DELAY': '4000', 'BROWSER_PRINT_QA_PREVIEW_DELAY': '2500'})
    try:
        b.ready(); k = b.k
        worst = [0.0]; stop = threading.Event(); counted = [None]; before = b.previews(); t0 = [time.monotonic()]

        def heartbeat():   # the UI thread's answers, and when the preview's count reaches the log (apart from the UIA reads)
            while not stop.is_set():
                s = time.perf_counter(); r = ctypes.c_size_t(); u32.SendMessageTimeoutW(b.main, WM_NULL, 0, 0, 0x2, 2000, ctypes.byref(r))
                worst[0] = max(worst[0], (time.perf_counter() - s) * 1000)
                if counted[0] is None and b.previews() > before:
                    counted[0] = time.monotonic()
                time.sleep(.05)
        hb = threading.Thread(target=heartbeat, daemon=True); hb.start()
        t0[0] = time.monotonic(); h = b.ctrl_p(); time.sleep(.6)
        # Entrance (night 24/11: stuck at ~15 %): full alpha, the panel's own colour, the page darkened beside it.
        alpha = layered_alpha(h); panel = rect_of(h) if h else (0, 0, 0, 0); page = rect_of(b.main)
        column = grab(b.shots / 'entrance-panel.png', panel).convert('RGB').getpixel((round(10 * k), round(200 * k))) if h else None
        beside = None
        if h:   # brightest pixel of a 16 x 16 patch of page beside the panel (text aside): white page under a 50 % scrim
            win = grab(b.shots / 'entrance-window.png', page).convert('RGB'); x0, y0 = max(4, panel[0] - page[0] - round(40 * k)), round(400 * k)
            beside = max((win.getpixel((x, y)) for x in range(x0, x0 + 16) for y in range(y0, y0 + 16)), key=sum)
        check('entrance', alpha == 255 and column is not None and max(abs(c - 15) for c in column) <= 3 and beside is not None and max(beside) <= 140,
              panel_alpha=alpha, preview_column_rgb=column, page_beside_rgb=beside, expected='alpha 255, column (15,15,15), white page darkened to about (128,128,128)')
        n = uia(b.pid); loading = b.shot('state5-loading')
        check('loading', bool(h) and node(n, 'PrintDialog') and node(n, 'PrintDialog')['help'] == 'Calcul des feuilles…' and node(n, 'PrintPageNumber') and node(n, 'PrintPageNumber')['name'] == 'Préparation de l’aperçu…',
              share_off=compare(loading, ref('state5-loading.png', k), b.shots / 'state5-loading-diff.png', k, keep=(0, 0, 720, 420)), engine_preview='ShowPrintUI' in b.log())
        wait(lambda: counted[0] is not None, 15); first = (counted[0] or time.monotonic()) - t0[0] - 2.5; time.sleep(1); stop.set(); hb.join(3)
        check('long', counted[0] is not None and first < 3 and worst[0] < 200, first_sheet_s=round(first, 2), worst_ui_answer_ms=round(worst[0], 1))
        opened = b.shot('open')
        check('open', bool(h), share_off=compare(opened, ref('modal-1x.png', k), b.shots / 'open-diff.png', k, skip=[(41, 25, 321, 421)]))
        n = uia(b.pid); pager = (node(n, 'PrintPageNumber') or {}).get('name', ''); total = int(m.group(1)) if (m := re.search(r'sur (\d+)', pager)) else 0
        check('pages', total > 10 and pager.startswith('Page 1 sur'), pager=pager, subtitle=(node(n, 'PrintDialog') or {}).get('help'))
        b.click('PrintNextPage'); n2 = (node(uia(b.pid), 'PrintPageNumber') or {}).get('name', ''); b.click('PrintPreviousPage'); n1 = (node(uia(b.pid), 'PrintPageNumber') or {}).get('name', '')
        check('pages_turn', n2.startswith('Page 2 sur') and n1.startswith('Page 1 sur'), after_next=n2, after_previous=n1)
        b.click('PrintLandscape', move_only=True); b.shot('state9-hover-landscape')
        for i in range(4):
            b.key(VK_TAB); b.shot(f'state9-focus-{i}')
        res['notes']['hover_focus'] = 'shots/state9-*.png against tests/print/paper/state9-hover-focus.png (review by eye: the board is a table)'
        # State 1
        b.click('PrintPagesChoose'); time.sleep(.8); n = uia(b.pid); thumbs = [x for x in n if x.get('id') == 'PrintThumbnail']
        b.shot('state1-choose')
        b.type('1-2'); n = uia(b.pid); thumbs = [x for x in n if x.get('id') == 'PrintThumbnail']
        if len(thumbs) >= 3:
            b.click_rect(b.dialog(), thumbs[2]['rect']); n = uia(b.pid); thumbs = [x for x in n if x.get('id') == 'PrintThumbnail']
        check('choose', len(thumbs) >= 3 and (node(n, 'PrintDialog') or {}).get('help') == '3 feuilles', thumbnails=len(thumbs), toggles=[t.get('toggle') for t in thumbs[:4]], subtitle=(node(n, 'PrintDialog') or {}).get('help'))
        b.shot('state1-choose-ticked')
        # State 2
        b.type('\b' * 8 + '999'); n = uia(b.pid); err = node(n, 'PrintRangeError'); acc = node(n, 'PrintAccept') or {}
        beyond = b.shot('state2-beyond')
        check('beyond', bool(err) and err['name'].startswith('Le document n’a que') and acc.get('enabled') is False, error=(err or {}).get('name'), accept=acc,
              share_off=compare(beyond, ref('state2-beyond.png', k), b.shots / 'state2-beyond-diff.png', k, keep=(361, 0, 720, 482)))
        b.type('\b' * 8 + '2-3')
        # Printer list, QA Laser, landscape (state 3)
        b.click('PrintPrinter'); b.shot('printer-list')
        b.key(VK_DOWN); b.key(VK_RETURN); time.sleep(.6)
        b.click('PrintPagesAll'); before = b.previews(); b.click('PrintLandscape'); wait(lambda: b.previews() > before, 10); time.sleep(.8)
        n = uia(b.pid); acc = node(n, 'PrintAccept') or {}; sheet = node(n, 'PrintPreviewSheet') or {'rect': [0, 0, 0, 0]}; sr = sheet['rect']
        land = b.shot('state3-printer-landscape')
        check('landscape', acc.get('name') == 'Imprimer' and (sr[2] - sr[0]) > (sr[3] - sr[1]), accept=acc.get('name'), sheet=sr,
              share_off=compare(land, ref('state3-printer-landscape.png', k), b.shots / 'state3-diff.png', k, keep=(361, 0, 720, 420)))
        b.click('PrintPagesChoose'); b.type('\b' * 8 + '2-3')
        # State 7: printing
        b.click('PrintAccept'); t = wait(b.toast, 5); n = uia(b.pid)
        tshot = b.shot('state7-toast-printing', t) if t else None
        check('printing', bool(t) and not b.dialog() and any(x.get('name') == 'Annuler' and x.get('window') == 'LisiereContextToast' for x in n),
              share_off=compare(tshot, PAPER / 'state7-toast-printing.png', b.shots / 'state7-diff.png', k, keep=(0, 0, 360, 58), skip=[(38, 28, 260, 50)]) if tshot else None)
        job = wait(lambda: (b.sink / 'print-job.txt').exists() and (b.sink / 'print-job.txt').read_text(encoding='utf-8'), 15)
        gone = wait(lambda: not b.toast(), 8)
        check('printed', bool(job) and 'printer=QA Laser' in job and 'orientation=landscape' in job and 'ranges=2-3' in job and bool(gone), job=job)
        # Cancel
        for f in b.sink.glob('*'):
            f.unlink()
        time.sleep(1); b.ctrl_p(); time.sleep(1); b.click('PrintAccept'); t = wait(b.toast, 5)
        if t:
            b.click('', 'Annuler', hwnd=t)   # the toast's action
        time.sleep(6)
        check('cancel', bool(t) and not (b.sink / 'print-job.txt').exists() and 'cancelled by the user' in b.log(), log=[l for l in b.log().splitlines() if 'PRINT' in l][-4:])
        # PDF
        time.sleep(1); b.ctrl_p(); time.sleep(3); b.click('PrintPrinter'); b.click('PrintPrinterChoice', 'Enregistrer en PDF')
        before = set(b.downloads.glob('*.pdf')); b.click('PrintAccept')
        made = wait(lambda: set(b.downloads.glob('*.pdf')) - before, 15); pdf = next(iter(made), None)
        pages = len(re.findall(rb'/Type\s*/Page[^s]', pdf.read_bytes())) if pdf else 0
        check('pdf', bool(pdf) and pages >= 1, file=str(pdf), pages=pages)
        # window.print() and a frame's
        time.sleep(1); b.ev('setTimeout(() => window.print(), 50); true'); h = wait(b.dialog, 5)
        check('page_print', bool(h))
        if h:
            b.key(VK_ESCAPE); wait(lambda: not b.dialog(), 3)
        time.sleep(1.5); b.ev("setTimeout(() => document.getElementById('frame').contentWindow.print(), 50); true"); time.sleep(3)
        res['notes']['frame_print'] = {'our_window': bool(b.dialog()), 'capture': 'shots/frame-print.png'}; grab(b.shots / 'frame-print.png', rect_of(b.main))
        u32.PostMessageW(b.main, WM_KEYDOWN, VK_ESCAPE, 0); time.sleep(1)
        # State 10: narrow
        b.size(750, 815); h = b.ctrl_p(); time.sleep(3.5)
        narrow = b.shot('state10-narrow') if h else None; r = rect_of(h) if h else (0, 0, 0, 0)
        check('narrow', bool(h) and round((r[2] - r[0]) / k) == 400, panel=r,
              share_off=compare(narrow, PAPER / 'state10-narrow.png', b.shots / 'state10-diff.png', k, skip=[(280, 17, 384, 70)]) if narrow else None)
    except Exception as e:
        res['failures'].append(f'main: {e!r}'); print('ERROR main', repr(e), flush=True)
    finally:
        res['notes']['main_log'] = [l for l in b.log().splitlines() if 'PRINT' in l][-40:]; b.close()

    # ---------------------------------------------------------------- session « fail »
    b = Browser(src, out, 'fail', base, {'BROWSER_PRINT_QA_PRINTERS': 'QA Laser', 'BROWSER_PRINT_QA_FAIL': 'offline'})
    try:
        b.ready(); k = b.k; b.ctrl_p(); time.sleep(3)
        b.click('PrintPrinter'); b.click('PrintPrinterChoice', 'QA Laser'); b.click('PrintAccept')
        t = wait(lambda: b.toast() if node(uia(b.pid), '', 'Réessayer') else 0, 10)
        es = b.shot('state8-toast-error', t) if t else None; time.sleep(8)
        check('error', bool(t) and bool(b.toast()), stays_after_8s=bool(b.toast()),
              share_off=compare(es, PAPER / 'state8-toast-error.png', b.shots / 'state8-diff.png', k, keep=(0, 0, 360, 58)) if es else None)
        if t:
            b.click('', 'Réessayer', hwnd=t); again = wait(lambda: b.toast() if node(uia(b.pid), '', 'Réessayer') else 0, 10)
            check('retry', bool(again) and b.log().count('PRINT job failed') >= 2)
            if again:
                b.click('', 'Fermer', hwnd=again); check('error_close', bool(wait(lambda: not b.toast(), 3)))
    except Exception as e:
        res['failures'].append(f'fail: {e!r}'); print('ERROR fail', repr(e), flush=True)
    finally:
        res['notes']['fail_log'] = [l for l in b.log().splitlines() if 'PRINT' in l][-20:]; b.close()

    # ---------------------------------------------------------------- session « bare »
    b = Browser(src, out, 'bare', base, {'BROWSER_PRINT_QA_NO_PREVIEW': '1'})
    try:
        b.ready(); k = b.k; h = b.ctrl_p(); time.sleep(3)
        n = uia(b.pid); un = b.shot('state6-unavailable')
        check('unavailable', node(n, 'PrintPreviewSheet', 'Aperçu indisponible') is not None and (node(n, 'PrintAccept') or {}).get('enabled') is True and 'ShowPrintUI' not in b.log(),
              share_off=compare(un, ref('state6-unavailable.png', k), b.shots / 'state6-diff.png', k, keep=(0, 0, 720, 420)))
        b.click('PrintPrinter'); n = uia(b.pid); lst = b.shot('state4-no-printer')
        check('no_printer', node(n, 'PrintNoPrinter') is not None and node(n, 'PrintAddPrinter') is not None,
              share_off=compare(lst, ref('state4-no-printer.png', k), b.shots / 'state4-diff.png', k, keep=(361, 0, 720, 420)))
        b.click('PrintAddPrinter'); check('add_printer', 'PRINT Windows printer settings asked for' in b.log() and bool(b.dialog()))
    except Exception as e:
        res['failures'].append(f'bare: {e!r}'); print('ERROR bare', repr(e), flush=True)
    finally:
        res['notes']['bare_log'] = [l for l in b.log().splitlines() if 'PRINT' in l][-20:]; b.close()

    srv.shutdown()
    (out / 'result.json').write_text(json.dumps(res, ensure_ascii=False, indent=1, default=str), encoding='utf-8')
    print('print QA: ' + ('FAILED: ' + ', '.join(res['failures']) if res['failures'] else 'OK') + f'  ({out / "result.json"})')
    return 1 if res['failures'] else 0


if __name__ == '__main__':
    sys.exit(main())
