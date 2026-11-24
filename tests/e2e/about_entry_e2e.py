"""Real-window check (night run only: it opens a real browser window and moves the pointer) for Réglages › the version
line that opens « À propos de Browser » (src/settings-panel.h; Paper « Browser — référence unifiée », « À propos : point
d'entrée » variant A). The windowless twin is tests/settings_about_test.cpp (geometry, Tab order, UIA, pictures).

Disposable install root under build/e2e-about-entry/<scenario>/lad/browser (as tests/e2e/release_notes_e2e.py): a copy
of the build, LOCALAPPDATA pointed at it, no onboarding. Nothing outside the fixture is written; the owner's profile
(%LOCALAPPDATA%\\browser) is never read. Every process started here dies with the script (job object).

  fr   Réglages (posted command 'S') -> UIA button « À propos de Browser » (help = the version line);
       at rest no plate; pointer on the line (SetCursorPos, restored after) -> white 4 % plate + chevron, tooltip;
       click (posted WM_LBUTTONDOWN/UP) -> the drawer closes and the A8 window opens (UIA AboutBrowser); Échap closes it;
       Tab (posted) until the line has the focus ring, Entrée -> A8 again (logged « (keyboard) »);
       UIA Invoke on the button -> A8 again.
  en   ui-language en -> UIA « About Browser ».

Usage: python tests/e2e/about_entry_e2e.py <build dir>
       (build: python tools/release/build.py --out build/about-entry-e2e --update-repo qa-offline/about-entry-none)
"""
import ctypes, ctypes.wintypes as wt, json, os, shutil, struct, subprocess, sys, time, zlib
sys.stdout.reconfigure(encoding='utf-8')

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'build', 'about-entry-e2e'))
BASE = os.path.join(ROOT, 'build', 'e2e-about-entry')
SHOTS = os.path.join(BASE, 'captures')
k32, u32, g32 = ctypes.windll.kernel32, ctypes.windll.user32, ctypes.windll.gdi32
u32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
WM_KEYDOWN, WM_MOUSEMOVE, WM_LBUTTONDOWN, WM_LBUTTONUP, SHORTCUT = 0x0100, 0x0200, 0x0201, 0x0202, 0x8000 + 1
CANVAS, PLATE = 0x17, 0x20   # #171717; white 4 % over it
failures, unavailable = 0, []


def check(ok, what):
    global failures
    print(('ok   ' if ok else 'FAIL ') + what, flush=True)
    if not ok:
        failures += 1


def windows(pid, cls):
    found = []
    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def each(hwnd, _):
        c = ctypes.create_unicode_buffer(64); u32.GetClassNameW(hwnd, c, 64)
        p = wt.DWORD(); u32.GetWindowThreadProcessId(hwnd, ctypes.byref(p))
        if c.value == cls and p.value == pid and u32.IsWindowVisible(hwnd):
            found.append(hwnd)
        return True
    u32.EnumWindows(each, 0)
    return found


def wait_window(pid, cls, present=True, wait=8.0):
    end = time.time() + wait
    while time.time() < end:
        w = windows(pid, cls)
        if bool(w) == present:
            return w[0] if w else 0
        time.sleep(0.15)
    w = windows(pid, cls)
    return (w[0] if w else 0) if present else (0 if not w else -1)


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [('biSize', wt.DWORD), ('biWidth', wt.LONG), ('biHeight', wt.LONG), ('biPlanes', wt.WORD), ('biBitCount', wt.WORD),
                ('biCompression', wt.DWORD), ('biSizeImage', wt.DWORD), ('biXPelsPerMeter', wt.LONG), ('biYPelsPerMeter', wt.LONG),
                ('biClrUsed', wt.DWORD), ('biClrImportant', wt.DWORD)]


def capture(hwnd):
    """BGRA pixels of OUR window (PrintWindow, PW_RENDERFULLCONTENT): no screen readback."""
    r = wt.RECT(); u32.GetWindowRect(hwnd, ctypes.byref(r))
    w, h = r.right - r.left, r.bottom - r.top
    if w <= 0 or h <= 0:
        return None
    screen = u32.GetDC(None); dc = g32.CreateCompatibleDC(screen); bmp = g32.CreateCompatibleBitmap(screen, w, h)
    old = g32.SelectObject(dc, bmp)
    ok = bool(u32.PrintWindow(wt.HWND(hwnd), dc, 2))
    info = BITMAPINFOHEADER(ctypes.sizeof(BITMAPINFOHEADER), w, -h, 1, 32, 0, 0, 0, 0, 0, 0)
    pixels = ctypes.create_string_buffer(w * h * 4)
    g32.GetDIBits(dc, bmp, 0, h, pixels, ctypes.byref(info), 0)
    g32.SelectObject(dc, old); g32.DeleteObject(bmp); g32.DeleteDC(dc); u32.ReleaseDC(None, screen)
    return (w, h, pixels.raw) if ok else None


def save_png(shot, path):
    w, h, px = shot
    raw = bytearray()
    for y in range(h):
        row = px[y * w * 4:(y + 1) * w * 4]
        raw.append(0)
        for x in range(0, len(row), 4):
            raw += bytes((row[x + 2], row[x + 1], row[x]))
    chunk = lambda tag, data: struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(bytes(raw), 6)) + chunk(b'IEND', b''))


def green(shot, x_dip, y_dip, dpi):
    w, h, px = shot
    x, y = int(x_dip * dpi / 96), int(y_dip * dpi / 96)
    return px[(y * w + x) * 4 + 1] if 0 <= x < w and 0 <= y < h else -1


def peak(shot, box, dpi):
    l, t, r, b = box
    s = dpi / 96
    return max((green(shot, l + i / s, t + j / s, dpi) for j in range(int((b - t) * s)) for i in range(int((r - l) * s))), default=-1)


def shoot(name, hwnd):
    shot = capture(hwnd)
    if shot and green(shot, 160, 60, dpi_of(hwnd)) == CANVAS:
        save_png(shot, os.path.join(SHOTS, name + '.png'))
        return shot
    unavailable.append(name)
    print(f'     capture {name}: UNAVAILABLE (PrintWindow gave no drawer surface: not used as visual proof)', flush=True)
    return None


def dpi_of(hwnd):
    return u32.GetDpiForWindow(wt.HWND(hwnd)) or 96


PS_HEAD = r'''
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
$A = [System.Windows.Automation.AutomationElement]; $S = [System.Windows.Automation.TreeScope]
$win = $A::FromHandle([IntPtr]{hwnd})
function ById($root, $id) {{ $root.FindFirst($S::Descendants, (New-Object System.Windows.Automation.PropertyCondition($A::AutomationIdProperty, $id))) }}
'''
PS_ELEMENT = PS_HEAD + r'''
$e = ById $win '{id}'
if ($e) {{ 'name=' + $e.Current.Name; 'help=' + $e.Current.HelpText; 'type=' + $e.Current.ControlType.ProgrammaticName; 'focus=' + $e.Current.HasKeyboardFocus }} else {{ 'error=no {id}' }}
'''
PS_INVOKE = PS_HEAD + r'''
$e = ById $win '{id}'
if (-not $e) {{ 'error=no {id}'; exit }}
$e.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke(); 'invoked=' + $e.Current.Name
'''


def ps(script):
    r = subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-Command', script], capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=120)
    out = {}
    for line in r.stdout.splitlines():
        if '=' in line:
            k, v = line.split('=', 1); out[k.strip()] = v.strip()
    if r.returncode != 0 and r.stderr.strip():
        out.setdefault('error', r.stderr.strip().splitlines()[-1])
    return out


def version():
    return json.load(open(os.path.join(SRC, 'version.json'), encoding='utf-8'))['version']


def make_root(name, data=None):
    fix = os.path.join(BASE, name)
    if os.path.isdir(fix):
        shutil.rmtree(fix)   # our own disposable fixture only
    root = os.path.join(fix, 'lad', 'browser'); v = version()
    shutil.copytree(SRC, os.path.join(root, 'versions', v))
    os.makedirs(os.path.join(root, 'updates')); os.makedirs(os.path.join(root, 'data'))
    open(os.path.join(root, 'current'), 'w', newline='\n').write(v + '\n')
    for rel, text in (data or {}).items():
        open(os.path.join(root, 'data', rel), 'w', encoding='utf-8', newline='\n').write(text)
    return fix, root


def start(fix, root):
    log = os.path.join(root, 'data', 'browser.log'); size = os.path.getsize(log) if os.path.exists(log) else 0
    env = dict(os.environ, LOCALAPPDATA=os.path.join(fix, 'lad'), BROWSER_NO_ONBOARDING='1')
    exe = os.path.join(root, 'versions', version(), 'browser.exe')
    p = subprocess.Popen([exe], cwd=os.path.dirname(exe), env=env)
    return p, wait_window(p.pid, 'LisiereBrowser', wait=30), log, size


def stop(p, hwnd, log, size):
    if hwnd:
        u32.PostMessageW(hwnd, 0x0010, 0, 0)   # WM_CLOSE to our own window
    try: p.wait(25)
    except subprocess.TimeoutExpired: p.kill()
    return open(log, 'rb').read()[size:].decode('utf-8', 'replace') if os.path.exists(log) else ''


def drawer_geometry(drawer):
    """Paper 1V0L-0 in the drawer's DIP: plate (8, h-48, 282, h-8), chevron (260, h-34, 272, h-22)."""
    r = wt.RECT(); u32.GetClientRect(drawer, ctypes.byref(r)); h = r.bottom * 96 / dpi_of(drawer)
    return h, (8, h - 48, 282, h - 8), (260, h - 34, 272, h - 22)


def open_drawer(p, hwnd):
    u32.PostMessageW(hwnd, SHORTCUT, ord('S'), 0)   # « Shortcut » command 'S' (Réglages), posted to our window
    d = wait_window(p.pid, 'LisiereSettingsDrawer')
    time.sleep(0.8)   # the slide settles
    return d


def client_lparam(drawer, x_dip, y_dip):
    s = dpi_of(drawer) / 96
    return (int(y_dip * s) << 16) | (int(x_dip * s) & 0xFFFF)


def scenario_fr():
    name = 'fr'
    print(f'-- {name}', flush=True)
    fix, root = make_root(name)
    p, hwnd, log, size = start(fix, root)
    check(hwnd != 0, f'{name}: our window started from the disposable root')
    time.sleep(2.0)
    d = open_drawer(p, hwnd)
    check(d != 0, f'{name}: Réglages open')
    if d:
        dpi = dpi_of(d); h, plate, chevron = drawer_geometry(d)
        u = ps(PS_ELEMENT.format(hwnd=int(d), id='SettingsAbout'))
        print('     UIA:', u, flush=True)
        check(u.get('name') == 'À propos de Browser' and u.get('type') == 'ControlType.Button' and u.get('help', '').startswith('Browser'),
              f'{name}: UIA button « À propos de Browser », the version line as help')
        rest = shoot(f'{name}-rest-{dpi}', d)
        if rest:
            check(green(rest, 12, h - 10, dpi) == CANVAS and peak(rest, chevron, dpi) <= CANVAS + 1, f'{name}: at rest no plate, no chevron')
        # Pointer: the real cursor over the line (night run only), then put back where it was.
        was = wt.POINT(); u32.GetCursorPos(ctypes.byref(was))
        r = wt.RECT(); u32.GetWindowRect(d, ctypes.byref(r)); s = dpi / 96
        u32.SetCursorPos(r.left + int(140 * s), r.top + int((h - 28) * s))
        time.sleep(1.4)   # hover fade + tooltip delay
        hover = shoot(f'{name}-hover-{dpi}', d)
        if hover:
            check(abs(green(hover, 12, h - 10, dpi) - PLATE) <= 1, f'{name}: hover: the white 4 % plate')
            check(peak(hover, chevron, dpi) > CANVAS + 80, f'{name}: hover: the chevron')
        tip = windows(p.pid, 'LisiereTooltip')
        t = ps(PS_ELEMENT.format(hwnd=int(tip[0]), id='Tooltip')) if tip else {}
        check(t.get('name') == 'À propos de Browser', f'{name}: tooltip « À propos de Browser » ({t.get("name")!r})')
        u32.SetCursorPos(was.x, was.y)
        # Click (posted to our drawer: no SendInput).
        lp = client_lparam(d, 140, h - 28)
        u32.PostMessageW(d, WM_MOUSEMOVE, 0, lp); u32.PostMessageW(d, WM_LBUTTONDOWN, 1, lp); u32.PostMessageW(d, WM_LBUTTONUP, 0, lp)
        dlg = wait_window(p.pid, 'BrowserModalDialog')
        a = ps(PS_ELEMENT.format(hwnd=int(dlg), id='AboutBrowser')) if dlg else {}
        check(dlg != 0 and 'error' not in a, f'{name}: click: the A8 window « À propos » opens')
        check(wait_window(p.pid, 'LisiereSettingsDrawer', present=False) == 0, f'{name}: click: Réglages closes')
        if dlg:
            u32.PostMessageW(dlg, WM_KEYDOWN, 0x1B, 0); time.sleep(0.8)
            check(not windows(p.pid, 'BrowserModalDialog'), f'{name}: Échap closes À propos')
        # Keyboard: Tab (posted) until the line wears the focus ring, then Entrée.
        d = open_drawer(p, hwnd); ringed = False
        for _ in range(40):
            u32.PostMessageW(d, WM_KEYDOWN, 0x09, 0); time.sleep(0.15)
            shot = capture(d)
            if shot and green(shot, 8.5, h - 28, dpi) > 0x60:
                ringed = True; break
        check(ringed, f'{name}: Tab reaches the version line (focus ring)')
        if ringed:
            shoot(f'{name}-focus-{dpi}', d)
            u32.PostMessageW(d, WM_KEYDOWN, 0x0D, 0)
            dlg = wait_window(p.pid, 'BrowserModalDialog')
            check(dlg != 0, f'{name}: Entrée opens À propos')
            if dlg:
                u32.PostMessageW(dlg, WM_KEYDOWN, 0x1B, 0); time.sleep(0.8)
        # Screen readers: UIA Invoke.
        d = open_drawer(p, hwnd)
        r = ps(PS_INVOKE.format(hwnd=int(d), id='SettingsAbout')) if d else {'error': 'no drawer'}
        dlg = wait_window(p.pid, 'BrowserModalDialog')
        check('error' not in r and dlg != 0, f'{name}: UIA Invoke opens À propos ({r.get("error", "ok")})')
        if dlg:
            u32.PostMessageW(dlg, WM_KEYDOWN, 0x1B, 0); time.sleep(0.8)
    text = stop(p, hwnd, log, size)
    check(text.count('ABOUT opened from Réglages') == 3 and text.count('ABOUT opened from Réglages (keyboard)') == 2,
          f'{name}: log: opened 3 times from Réglages, 2 from the keyboard (Entrée, UIA)')


def scenario_en():
    name = 'en'
    print(f'-- {name}', flush=True)
    fix, root = make_root(name, {'ui-language': 'browser-ui-language 1\nlanguage=en\n'})
    p, hwnd, log, size = start(fix, root)
    check(hwnd != 0, f'{name}: our window started')
    time.sleep(2.0)
    d = open_drawer(p, hwnd)
    u = ps(PS_ELEMENT.format(hwnd=int(d), id='SettingsAbout')) if d else {}
    check(u.get('name') == 'About Browser', f'{name}: UIA « About Browser » ({u.get("name")!r})')
    stop(p, hwnd, log, size)


def contain_children():
    """Every process this script starts dies with it (job object, KILL_ON_JOB_CLOSE, inherited by children)."""
    class LIMIT(ctypes.Structure):
        _fields_ = [('PerProcessUserTimeLimit', ctypes.c_int64), ('PerJobUserTimeLimit', ctypes.c_int64), ('LimitFlags', wt.DWORD),
                    ('MinimumWorkingSetSize', ctypes.c_size_t), ('MaximumWorkingSetSize', ctypes.c_size_t), ('ActiveProcessLimit', wt.DWORD),
                    ('Affinity', ctypes.c_size_t), ('PriorityClass', wt.DWORD), ('SchedulingClass', wt.DWORD)]
    class EXTENDED(ctypes.Structure):
        _fields_ = [('Basic', LIMIT), ('Io', ctypes.c_uint64 * 6), ('ProcessMemoryLimit', ctypes.c_size_t), ('JobMemoryLimit', ctypes.c_size_t),
                    ('PeakProcessMemoryUsed', ctypes.c_size_t), ('PeakJobMemoryUsed', ctypes.c_size_t)]
    k32.CreateJobObjectW.restype = wt.HANDLE
    k32.GetCurrentProcess.restype = wt.HANDLE
    job = k32.CreateJobObjectW(None, None)
    info = EXTENDED(); info.Basic.LimitFlags = 0x2000   # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
    ok = job and k32.SetInformationJobObject(wt.HANDLE(job), 9, ctypes.byref(info), ctypes.sizeof(info)) and k32.AssignProcessToJobObject(wt.HANDLE(job), wt.HANDLE(k32.GetCurrentProcess()))
    print('job object: children die with this script' if ok else 'job object UNAVAILABLE', flush=True)
    return job


def main():
    if not os.path.isfile(os.path.join(SRC, 'browser.exe')):
        print('missing build', SRC); return 2
    job = contain_children()
    os.makedirs(SHOTS, exist_ok=True)
    print(f'build {SRC} version {version()}', flush=True)
    scenario_fr()
    scenario_en()
    print(f'\n{failures} failure(s); unavailable: {", ".join(unavailable) if unavailable else "none"}')
    del job
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
