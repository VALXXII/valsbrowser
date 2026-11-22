"""Real-app E2E for « Nouveautés » (design/release-notes-modal-report.md), on synthetic install roots only.

Each scenario builds a disposable installed layout under build/e2e-release-notes/<scenario>/lad/browser:
  versions/<v>/      copy of the built browser (the e2e build points its update feed at a repository that
                     does not exist, so the startup check stays silent; nothing is downloaded or installed)
  current            <v>
  previous           what the update helper records after a switch (« installed X » / « portable <path> »)
  updates/pending    the helper's record for the version it launched (browser-pending 1, nonce, version)
  data/              ui-language, settings (reduce-motion), release-notes read state, browser.log
and starts versions/<v>/browser.exe with LOCALAPPDATA pointing at <scenario>/lad, so the production
UpdateCore::NoteStartup acknowledges THAT pending record and the engine reports Installed exactly as after a
real update. Nothing outside the fixture is written: no real install root, no personal profile, no shortcut
(the taskbar identity only stamps shortcuts that target the fixture's launcher; none does).

Scenarios:
  update-fr       previous installed 0.3.379 -> Mini FR shown once, seen recorded after display; UIA name, items,
                  Voir tout, Fermer; Tab (posted WM_KEYDOWN) moves focus; Voir tout (UIA Invoke) -> Full in place;
                  Échap (posted) closes; logo frames change between two captures.
  restart         same root, helper's pending/ack gone -> ordinary launch: no modal, nothing logged.
  first-install   no `previous` -> baseline recorded, no modal.
  already-seen    previous + pending, state already holds these notes -> no modal, file unchanged.
  future-state    previous + pending, state in a future format -> no modal, file unchanged.
  update-en       language=en, previous portable -> Mini EN (« What’s new », « Show all »).
  reduced         reduce-motion=1 in Réglages -> Mini, logo identical between captures (poster), visible.
  about-full      ordinary launch; command 'A' posted to our window (À propos), UIA Invoke « Notes de version »
                  -> Full view, no Voir tout; X (UIA Invoke) closes; no read state written.

Isolation as tests/e2e/language_e2e.py: only processes whose image lives in the fixture are touched; windows are
never brought to the foreground, no SendInput; captures are PrintWindow of OUR windows only and are gated: an
all-black capture is reported UNAVAILABLE, never used as visual proof.
Usage: python tests/e2e/release_notes_e2e.py <build dir> [notes id]
"""
import ctypes, ctypes.wintypes as wt, json, os, shutil, struct, subprocess, sys, time, zlib
sys.stdout.reconfigure(encoding='utf-8')

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'build', 'release-notes-e2e'))
BASE = os.path.join(ROOT, 'build', 'e2e-release-notes')
SHOTS = os.path.join(BASE, 'captures')
k32, u32, g32 = ctypes.windll.kernel32, ctypes.windll.user32, ctypes.windll.gdi32
u32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
failures = 0
unavailable = []


def notes_id():
    if len(sys.argv) > 2:
        return sys.argv[2]
    for line in open(os.path.join(ROOT, 'src', 'release-notes.h'), encoding='utf-8'):
        if 'inline constexpr const char* NotesId' in line:
            return line.split('"')[1]
    raise SystemExit('NotesId not found')


NOTES = notes_id()
SEEN = f'browser-release-notes 1\nseen={NOTES}\n'


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


def fixture_processes(fix):
    pids = []
    out = subprocess.run(['powershell', '-NoProfile', '-Command', "Get-Process browser -ErrorAction SilentlyContinue | ForEach-Object { $_.Id.ToString() + '|' + $_.Path }"], capture_output=True, text=True).stdout
    for line in out.splitlines():
        if '|' in line:
            pid, path = line.split('|', 1)
            if path and os.path.normcase(path).startswith(os.path.normcase(fix + os.sep)):
                pids.append(int(pid))
    return pids


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [('biSize', wt.DWORD), ('biWidth', wt.LONG), ('biHeight', wt.LONG), ('biPlanes', wt.WORD), ('biBitCount', wt.WORD),
                ('biCompression', wt.DWORD), ('biSizeImage', wt.DWORD), ('biXPelsPerMeter', wt.LONG), ('biYPelsPerMeter', wt.LONG),
                ('biClrUsed', wt.DWORD), ('biClrImportant', wt.DWORD)]


def capture(hwnd):
    """BGRA pixels of OUR window through PrintWindow(PW_RENDERFULLCONTENT): no foreground change, no screen readback."""
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


def surface_ok(shot):
    """Gate: a capture is evidence only if it shows the #171717 surface (an all-black PrintWindow is not)."""
    if not shot:
        return False
    w, h, px = shot
    x, y = w // 2, h // 2
    b, g, r = px[(y * w + x) * 4:(y * w + x) * 4 + 3]
    lit = sum(1 for i in range(0, len(px), 4 * 97) if px[i + 1] > 0x10)
    return abs(g - 0x17) <= 3 and lit > 0


def region(shot, dip_rect, dpi):
    w, h, px = shot
    s = dpi / 96
    l, t, r, b = (int(v * s) for v in dip_rect)
    return bytes(px[(y * w + x) * 4 + 1] for y in range(t, min(b, h)) for x in range(l, min(r, w)))


PS_HEAD = r'''
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
$A = [System.Windows.Automation.AutomationElement]; $S = [System.Windows.Automation.TreeScope]
$P = [System.Windows.Automation.InvokePattern]::Pattern
$win = $A::FromHandle([IntPtr]{hwnd})
function ById($root, $id) {{ $root.FindFirst($S::Descendants, (New-Object System.Windows.Automation.PropertyCondition($A::AutomationIdProperty, $id))) }}
function ByName($root, $name) {{ $root.FindFirst($S::Descendants, (New-Object System.Windows.Automation.PropertyCondition($A::NameProperty, $name))) }}
'''

PS_DIALOG = PS_HEAD + r'''
$root = $win
$g = ById $win 'ReleaseNotes'   # the dialog's group node (the HWND element above it carries only the window title)
if ($g) {{ 'name=' + $g.Current.Name; 'id=' + $g.Current.AutomationId; 'help=' + ($g.Current.HelpText -replace "`n", ' ') }} else {{ 'name=' + $win.Current.Name }}
$all = $root.FindAll($S::Descendants, [System.Windows.Automation.Condition]::TrueCondition)
$texts = @(); $off = 0
foreach ($e in $all) {{ if ($e.Current.ControlType.ProgrammaticName -eq 'ControlType.Text') {{ $texts += $e.Current.Name; if ($e.Current.IsOffscreen) {{ $off++ }} }} }}
'texts=' + ($texts -join ' | ')
'offscreen=' + $off
$b = ById $root 'ReleaseNotesShowAll'; if ($b) {{ 'showall=' + $b.Current.Name; 'showallFocus=' + $b.Current.HasKeyboardFocus }}
$c = ById $root 'DialogClose'; if ($c) {{ 'close=' + $c.Current.Name; 'closeFocus=' + $c.Current.HasKeyboardFocus }}
'''

PS_INVOKE = PS_HEAD + r'''
$e = ById $win '{target}'
if (-not $e) {{ $e = ByName $win '{target}' }}
if (-not $e) {{ 'error=no {target}'; exit }}
$e.GetCurrentPattern($P).Invoke(); 'invoked=' + $e.Current.Name
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


def make_root(name, previous=None, pending=True, data=None):
    fix = os.path.join(BASE, name)
    if os.path.isdir(fix):
        shutil.rmtree(fix)   # our own disposable fixture only
    root = os.path.join(fix, 'lad', 'browser'); v = version()
    shutil.copytree(SRC, os.path.join(root, 'versions', v))
    os.makedirs(os.path.join(root, 'updates')); os.makedirs(os.path.join(root, 'data'))
    open(os.path.join(root, 'current'), 'w', newline='\n').write(v + '\n')
    if previous is not None:
        open(os.path.join(root, 'previous'), 'w', newline='\n').write(previous)
    nonce = os.urandom(16).hex()
    if pending:
        open(os.path.join(root, 'updates', 'pending'), 'w', newline='\n').write(f'browser-pending 1\nnonce {nonce}\nversion {v}\n')
    for rel, text in (data or {}).items():
        open(os.path.join(root, 'data', rel), 'w', encoding='utf-8', newline='\n').write(text)
    return fix, root, nonce


def start(fix, root):
    log = os.path.join(root, 'data', 'browser.log'); size = os.path.getsize(log) if os.path.exists(log) else 0
    # BROWSER_NO_ONBOARDING: the fixture has no profile registry, which the first-run onboarding (main since PR #100)
    # reads as a fresh install; a real update always finds the older build's data, so it never sees the onboarding.
    env = dict(os.environ, LOCALAPPDATA=os.path.join(fix, 'lad'), BROWSER_NO_ONBOARDING='1')
    exe = os.path.join(root, 'versions', version(), 'browser.exe')
    p = subprocess.Popen([exe], cwd=os.path.dirname(exe), env=env)
    hwnd = 0
    for _ in range(150):
        time.sleep(0.2)
        w = windows(p.pid, 'LisiereBrowser')
        if w:
            hwnd = w[0]; break
    return p, hwnd, log, size


def dialog(pid, wait=8.0):
    end = time.time() + wait
    while time.time() < end:
        d = windows(pid, 'BrowserModalDialog')
        if d:
            return d[0]
        time.sleep(0.2)
    return 0


def stop(p, hwnd, log, size):
    if hwnd:
        u32.PostMessageW(hwnd, 0x0010, 0, 0)   # WM_CLOSE to our own window
    try: p.wait(25)
    except subprocess.TimeoutExpired: p.kill()
    return open(log, 'rb').read()[size:].decode('utf-8', 'replace') if os.path.exists(log) else ''


def read(path):
    return open(path, 'rb').read().decode('utf-8', 'replace') if os.path.exists(path) else None


def shoot(name, hwnd, dpi):
    shot = capture(hwnd)
    if surface_ok(shot):
        save_png(shot, os.path.join(SHOTS, name + '.png'))
        return shot
    unavailable.append(name)
    print(f'     capture {name}: UNAVAILABLE (PrintWindow returned no surface: not used as visual proof)', flush=True)
    return None


def dpi_of(hwnd):
    return u32.GetDpiForWindow(wt.HWND(hwnd)) or 96


def corner_inset(card, owner):
    """DIP between the card and the owner's client right / bottom edges (the card's corner placement)."""
    c = wt.RECT(); u32.GetClientRect(owner, ctypes.byref(c)); pt = wt.POINT(c.right, c.bottom); u32.ClientToScreen(owner, ctypes.byref(pt))
    r = wt.RECT(); u32.GetWindowRect(card, ctypes.byref(r)); s = dpi_of(owner) / 96
    return (pt.x - r.right) / s, (pt.y - r.bottom) / s


def toast_seen(pid, wait):
    end = time.time() + wait
    while time.time() < end:
        if windows(pid, 'BrowserUpdateToast'):
            return True
        time.sleep(0.2)
    return False


def size_dip(hwnd):
    r = wt.RECT(); u32.GetWindowRect(hwnd, ctypes.byref(r)); s = dpi_of(hwnd) / 96
    return (r.right - r.left) / s, (r.bottom - r.top) / s


def scenario_update(name, lang, previous, expect, data=None):
    print(f'-- {name}', flush=True)
    data = dict(data or {})
    if lang == 'en':
        data['ui-language'] = 'browser-ui-language 1\nlanguage=en\n'
    fix, root, nonce = make_root(name, previous=previous, data=data)
    p, hwnd, log, size = start(fix, root)
    check(hwnd != 0, f'{name}: our window started from the synthetic install root')
    d, toast, end = 0, False, time.time() + 45   # the Mini shows once startup settles (first navigation); the toast is watched from launch
    while time.time() < end and not d:
        toast = toast or bool(windows(p.pid, 'BrowserUpdateToast')); found = windows(p.pid, 'BrowserModalDialog'); d = found[0] if found else 0
        time.sleep(0.15)
    check(d != 0, f'{name}: the Nouveautés dialog is on screen after the update')
    state = os.path.join(root, 'data', 'release-notes')
    if d:
        time.sleep(0.6)
        dpi = dpi_of(d); w, h = size_dip(d)
        check(abs(w - 343) <= 1 and abs(h - 218) <= 1, f'{name}: Mini {w:.1f} x {h:.1f} DIP at {dpi} DPI (Paper 343 x 218)')
        ix, iy = corner_inset(d, hwnd)
        check(abs(ix - 14) <= 1 and abs(iy - 14) <= 1, f'{name}: bottom-right of the window, {ix:.1f} / {iy:.1f} DIP from its right / bottom edges (14)')
        check(not windows(p.pid, 'BrowserModalScrim'), f'{name}: no scrim: the window stays usable')
        check(u32.GetForegroundWindow() != d, f'{name}: shown without taking the keyboard from the page')
        check(not toast and not toast_seen(p.pid, 3), f'{name}: the « Mise à jour installée » toast is replaced (never shown from launch on)')
        u = ps(PS_DIALOG.format(hwnd=int(d)))
        print('     UIA:', {k: u.get(k) for k in ('name', 'id', 'showall', 'close', 'showallFocus', 'closeFocus', 'offscreen')}, flush=True)
        check(u.get('id') == 'ReleaseNotes' and u.get('name') == expect['title'], f'{name}: UIA dialog {expect["title"]!r} (got {u.get("name")!r})')
        check(expect['last'] in u.get('help', '') and expect['last'] in u.get('texts', ''), f'{name}: every item reachable through UIA (the hidden part of the Mini included)')
        check(u.get('showall') == expect['showall'] and u.get('close') == expect['close'], f'{name}: UIA buttons {expect["showall"]!r} and {expect["close"]!r}')
        check(int(u.get('offscreen', '0')) >= 3, f'{name}: items under the fade reported offscreen ({u.get("offscreen")})')
        check(read(state) == SEEN, f'{name}: read state recorded once the dialog was shown ({read(state)!r})')
        a = shoot(f'{name}-mini-{dpi}', d, dpi); time.sleep(0.45); b = capture(d)
        if a and surface_ok(b):
            logo_a, logo_b = region(a, (17, 17, 37, 37), dpi), region(b, (17, 17, 37, 37), dpi)
            ink = sum(1 for v in logo_a if v > 0x17 + 12)
            check(ink > (20 * dpi / 96) ** 2 / 12, f'{name}: logo ink visible in its 20 DIP slot ({ink} px)')
            if expect.get('reduced'):
                check(logo_a == logo_b, f'{name}: reduced motion: the logo stays on its poster (two captures 450 ms apart identical)')
            else:
                check(logo_a != logo_b, f'{name}: the logo turns (two captures 450 ms apart differ)')
            check(region(a, (43, 106, 326, 125), dpi) != region(a, (43, 205, 326, 216), dpi), f'{name}: first item drawn, bottom faded')
        u32.PostMessageW(d, 0x0100, 0x09, 0)   # WM_KEYDOWN Tab, posted to our dialog (no SendInput)
        time.sleep(0.4); u = ps(PS_DIALOG.format(hwnd=int(d)))
        print('     after Tab:', {k: u.get(k) for k in ('showallFocus', 'closeFocus')}, flush=True)
        if u.get('showallFocus') == 'False' and u.get('closeFocus') == 'False':
            unavailable.append(f'{name}-focus')   # the dialog is not the foreground window here: keyboard focus is not observable
            print(f'     focus {name}: UNAVAILABLE (no keyboard focus reported: window not foreground in this session)', flush=True)
        else:
            check(u.get('closeFocus') == 'True', f'{name}: Tab moves the focus to Fermer')
        r = ps(PS_INVOKE.format(hwnd=int(d), target='ReleaseNotesShowAll'))
        check('error' not in r, f'{name}: Voir tout invoked through UIA ({r.get("error", "ok")})')
        time.sleep(0.6)
        w2, h2 = size_dip(d)
        check(dialog(p.pid, 1) == d and abs(w2 - 343) <= 1 and h2 > 300, f'{name}: Full view in the same dialog: {w2:.1f} x {h2:.1f} DIP (Paper 144O-0 height to content)')
        ix, iy = corner_inset(d, hwnd)
        check(abs(ix - 14) <= 1 and abs(iy - 14) <= 1, f'{name}: the Full view stays in the bottom-right corner ({ix:.1f} / {iy:.1f} DIP)')
        u = ps(PS_DIALOG.format(hwnd=int(d)))
        check('showall' not in u and u.get('offscreen') == '0', f'{name}: Full view: no Voir tout, every item on screen')
        shoot(f'{name}-full-{dpi}', d, dpi)
        u32.PostMessageW(d, 0x0100, 0x1B, 0)   # WM_KEYDOWN Échap
        time.sleep(0.8)
        check(not windows(p.pid, 'BrowserModalDialog'), f'{name}: Échap closes the dialog')
    text = stop(p, hwnd, log, size)
    check('RELEASE NOTES startup: show' in text and 'RELEASE NOTES shown mini' in text and 'RELEASE NOTES seen recorded' in text, f'{name}: log: decision show, shown mini, seen recorded')
    check(text.count('RELEASE NOTES shown') == 1, f'{name}: shown exactly once')
    check('UPDATE installed toast replaced by Nouveautés' in text, f'{name}: log: installed toast replaced')
    check(os.path.exists(os.path.join(root, 'updates', 'ack-' + nonce)), f'{name}: the startup acknowledgement the helper waits for was written (production NoteStartup)')
    return fix, root


def scenario_quiet(name, fix_root=None, previous=None, pending=True, data=None, expect_state=None, expect_log=None, expect_toast=None):
    print(f'-- {name}', flush=True)
    if fix_root:
        fix, root = fix_root
        for f in os.listdir(os.path.join(root, 'updates')):
            if f == 'pending' or f.startswith('ack-'):
                os.remove(os.path.join(root, 'updates', f))   # what the helper does after the acknowledgement
    else:
        fix, root, _ = make_root(name, previous=previous, pending=pending, data=data)
    state = os.path.join(root, 'data', 'release-notes'); before = read(state)
    p, hwnd, log, size = start(fix, root)
    check(hwnd != 0, f'{name}: our window started')
    if expect_toast is not None:
        seen = toast_seen(p.pid, 15)
        check(seen == expect_toast, f'{name}: « Mise à jour installée » toast {"shown (nothing replaces it)" if expect_toast else "absent"}')
    d = dialog(p.pid, 25)
    check(d == 0, f'{name}: no Nouveautés dialog (25 s observed)')
    text = stop(p, hwnd, log, size)
    if expect_log:
        check(expect_log in text, f'{name}: log says {expect_log!r}')
    else:
        check('RELEASE NOTES' not in text, f'{name}: nothing about release notes read or logged')
    want = before if expect_state is None else expect_state
    check(read(state) == want, f'{name}: read state {"unchanged" if expect_state is None else "as expected"} ({read(state)!r})')


def scenario_about():
    name = 'about-full'
    print(f'-- {name}', flush=True)
    fix, root, _ = make_root(name, previous='installed 0.3.379\n', pending=False)
    p, hwnd, log, size = start(fix, root)
    check(hwnd != 0, f'{name}: our window started')
    time.sleep(2.0)
    u32.PostMessageW(hwnd, 0x8000 + 1, ord('A'), 0)   # WM_APP+1 « Shortcut » command 'A' (À propos), posted to our window
    d = dialog(p.pid)
    check(d != 0, f'{name}: À propos is open')
    if d:
        r = ps(PS_INVOKE.format(hwnd=int(d), target='Notes de version'))
        check('error' not in r, f'{name}: « Notes de version » invoked through UIA ({r.get("error", "ok")})')
        u, n = {}, 0
        for _ in range(20):   # the same dialog window is reused: wait until it carries the notes
            time.sleep(0.5); n = dialog(p.pid, 2); u = ps(PS_DIALOG.format(hwnd=int(n))) if n else {}
            if u.get('id') == 'ReleaseNotes': break
        print('     UIA:', {k: u.get(k) for k in ('name', 'id', 'showall', 'close', 'offscreen')}, flush=True)
        check(u.get('id') == 'ReleaseNotes' and 'showall' not in u and u.get('close') == 'Fermer', f'{name}: the Full view opens (no Voir tout), with Fermer')
        if n:
            dpi = dpi_of(n); w, h = size_dip(n)
            check(abs(w - 343) <= 1 and h > 300, f'{name}: Full {w:.1f} x {h:.1f} DIP at {dpi} DPI')
            shoot(f'{name}-full-{dpi}', n, dpi)
            r = ps(PS_INVOKE.format(hwnd=int(n), target='DialogClose'))
            time.sleep(0.8)
            check('error' not in r and not windows(p.pid, 'BrowserModalDialog'), f'{name}: X (UIA Invoke) closes it')
    text = stop(p, hwnd, log, size)
    check('ABOUT release notes: Nouveautés (full)' in text and 'RELEASE NOTES shown full' in text and 'RELEASE NOTES closed' in text, f'{name}: log: opened from À propos, shown full, closed')
    check(read(os.path.join(root, 'data', 'release-notes')) is None, f'{name}: opening by hand never writes the read state')


def contain_children():
    """Every process this script starts (browser.exe and its WebView2 children) dies with it: this Python process joins
    a job object with KILL_ON_JOB_CLOSE, which its children inherit; the handle is closed only when Python exits."""
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
    print('job object: children die with this script' if ok else 'job object UNAVAILABLE: relying on the kill by exact path', flush=True)
    return job


def kill_fixture_processes():
    """Last resort: any browser whose image lives in this script's fixture folder (exact path) is ended."""
    for pid in fixture_processes(BASE):
        subprocess.run(['taskkill', '/F', '/T', '/PID', str(pid)], capture_output=True)
        print(f'killed leftover fixture browser {pid}', flush=True)


def main():
    contain_children()
    try:
        return run()
    finally:
        kill_fixture_processes()


def run():
    if not os.path.isfile(os.path.join(SRC, 'browser.exe')):
        print('missing build', SRC); return 2
    if fixture_processes(BASE):
        print('a fixture browser is already running: close it first'); return 2
    os.makedirs(SHOTS, exist_ok=True)
    print(f'build {SRC} version {version()} notes {NOTES}', flush=True)
    fr = {'title': 'Nouveautés', 'showall': 'Voir tout', 'close': 'Fermer', 'last': 'Renommage des dossiers de favoris sur place.'}
    en = {'title': 'What’s new', 'showall': 'Show all', 'close': 'Close', 'last': 'Bookmark folders renamed in place.'}
    first = scenario_update('update-fr', 'fr', 'installed 0.3.379\n', fr)
    scenario_quiet('restart', fix_root=first)
    scenario_quiet('first-install', previous=None, expect_state=SEEN, expect_log='RELEASE NOTES baseline recorded (first install)')
    scenario_quiet('already-seen', previous='installed 0.3.379\n', data={'release-notes': SEEN}, expect_log='RELEASE NOTES startup: hidden (state ok)', expect_toast=True)
    scenario_quiet('future-state', previous='installed 0.3.379\n', data={'release-notes': 'browser-release-notes 9\nseen=x\n'}, expect_log='RELEASE NOTES startup: hidden (state unknown-version)')
    scenario_update('update-en', 'en', 'portable C:\\portable\\browser\n', en)
    scenario_update('reduced', 'fr', 'installed 0.3.379\n', dict(fr, reduced=True), data={'settings': 'browser-settings 1\nreduce-motion=1\n'})
    scenario_about()
    print(f'\n{failures} failure(s); unavailable: {", ".join(unavailable) if unavailable else "none"}')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
