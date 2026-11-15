"""Real-app E2E for the interface language (design/browser-languages-plan.md), on OUR fixture copy only.

Copies the built browser into build/e2e-language (portable layout: the preference, profile and log stay in
that folder). Every step starts a FRESH process and checks what its window really shows through UI
Automation (toolbar names, window title) and what the log says.

Part A, preference file written by the harness (the startup reader on its own):
  1. no preference          -> French (default)
  2. language=en            -> English
  3. back to language=fr    -> French again
  4. unknown version file   -> French, reported as « unknown-version » in the log
Part B, the real Settings path (no file written by the harness):
  5. French process: Réglages › Langue › « English » chosen through UIA (Invoke on the toolbar button, Expand
     on the row, Invoke on the menu item). The file now says language=en, the open window stays French
     (nothing switches half-way) and the row shows « Au prochain démarrage. ».
  6. fresh process          -> English, from the file the menu saved
  7. English process: Settings › Language › « Français » -> file says language=fr; fresh process -> French.

Isolation: only processes whose image lives in the fixture folder are touched; windows are never brought to
the foreground, no keys or clicks are sent, and screenshots are taken with PrintWindow of OUR window only (no
desktop readback). The installed WebView2 runtime is used read-only; nothing is installed, no personal profile
is opened. Usage: python tests/e2e/language_e2e.py <build dir>
"""
import ctypes, ctypes.wintypes as wt, os, shutil, struct, subprocess, sys, time, zlib
sys.stdout.reconfigure(encoding='utf-8')

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'build', 'i18n-dev'))
FIX = os.path.join(ROOT, 'build', 'e2e-language')
SHOTS = os.path.join(ROOT, 'build', 'browser-languages', 'captures', 'e2e-printwindow')
PREF = os.path.join(FIX, 'ui-language')
k32, u32, g32 = ctypes.windll.kernel32, ctypes.windll.user32, ctypes.windll.gdi32
u32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))   # per-monitor v2: real window sizes, so captures are never cropped
failures = 0


def check(ok, what):
    global failures
    print(('ok   ' if ok else 'FAIL ') + what, flush=True)
    if not ok:
        failures += 1


def ours():
    found = []
    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def each(hwnd, _):
        cls = ctypes.create_unicode_buffer(64); u32.GetClassNameW(hwnd, cls, 64)
        if cls.value != 'LisiereBrowser' or not u32.IsWindowVisible(hwnd):
            return True
        pid = wt.DWORD(); u32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        hp = k32.OpenProcess(0x1000, False, pid.value)
        if hp:
            buf = ctypes.create_unicode_buffer(32768); n = wt.DWORD(32768)
            if k32.QueryFullProcessImageNameW(hp, 0, buf, ctypes.byref(n)) and os.path.normcase(buf.value).startswith(os.path.normcase(FIX + os.sep)):
                found.append((pid.value, hwnd))
            k32.CloseHandle(hp)
        return True
    u32.EnumWindows(each, 0)
    return found


def title(hwnd):
    buf = ctypes.create_unicode_buffer(512); u32.GetWindowTextW(hwnd, buf, 512); return buf.value


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [('biSize', wt.DWORD), ('biWidth', wt.LONG), ('biHeight', wt.LONG), ('biPlanes', wt.WORD), ('biBitCount', wt.WORD),
                ('biCompression', wt.DWORD), ('biSizeImage', wt.DWORD), ('biXPelsPerMeter', wt.LONG), ('biYPelsPerMeter', wt.LONG),
                ('biClrUsed', wt.DWORD), ('biClrImportant', wt.DWORD)]


def print_window(hwnd, path):
    """PNG of OUR window through PrintWindow(PW_RENDERFULLCONTENT): no foreground change, no screen readback."""
    r = wt.RECT(); u32.GetWindowRect(hwnd, ctypes.byref(r))
    w, h = r.right - r.left, r.bottom - r.top
    if w <= 0 or h <= 0:
        return False
    screen = u32.GetDC(None); dc = g32.CreateCompatibleDC(screen); bmp = g32.CreateCompatibleBitmap(screen, w, h)
    old = g32.SelectObject(dc, bmp)
    ok = bool(u32.PrintWindow(wt.HWND(hwnd), dc, 2))
    info = BITMAPINFOHEADER(ctypes.sizeof(BITMAPINFOHEADER), w, -h, 1, 32, 0, 0, 0, 0, 0, 0)
    pixels = ctypes.create_string_buffer(w * h * 4)
    g32.GetDIBits(dc, bmp, 0, h, pixels, ctypes.byref(info), 0)
    g32.SelectObject(dc, old); g32.DeleteObject(bmp); g32.DeleteDC(dc); u32.ReleaseDC(None, screen)
    raw = bytearray()
    for y in range(h):
        row = pixels.raw[y * w * 4:(y + 1) * w * 4]
        raw.append(0)
        for x in range(0, len(row), 4):
            raw += bytes((row[x + 2], row[x + 1], row[x]))
    chunk = lambda tag, data: struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(bytes(raw), 6)) + chunk(b'IEND', b''))
    return ok


PS_HEAD = r'''
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
$A = [System.Windows.Automation.AutomationElement]; $S = [System.Windows.Automation.TreeScope]
$win = $A::FromHandle([IntPtr]{hwnd})
function ById($root, $id) {{ $root.FindFirst($S::Descendants, (New-Object System.Windows.Automation.PropertyCondition($A::AutomationIdProperty, $id))) }}
function ByName($root, $name) {{ $root.FindFirst($S::Descendants, (New-Object System.Windows.Automation.PropertyCondition($A::NameProperty, $name))) }}
'''

PS_NAMES = PS_HEAD + r'''
$out = @()
foreach ($id in @('Settings','History','Downloads','NewTab','Reload','Address','TabList','ProfileButton')) {{
  $e = ById $win $id; if ($e) {{ $out += ($id + '=' + $e.Current.Name) }}
}}
$out -join "`n"
'''

# Settings › Langue › <item>, through UIA patterns only (posted to the app: never keys, clicks or focus changes).
PS_CHOOSE = PS_HEAD + r'''
$settings = ById $win 'Settings'
if (-not $settings) {{ 'error=no Settings button'; exit }}
$settings.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
$row = $null
for ($i = 0; $i -lt 40 -and -not $row; $i++) {{ Start-Sleep -Milliseconds 150; $row = ByName $win '{row}' }}
if (-not $row) {{ 'error=no language row'; exit }}
'before=' + $row.Current.ItemStatus
$row.GetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern).Expand()
$item = $null
$pidCond = New-Object System.Windows.Automation.PropertyCondition($A::ProcessIdProperty, {pid})
for ($i = 0; $i -lt 40 -and -not $item; $i++) {{
  Start-Sleep -Milliseconds 150
  foreach ($top in $A::RootElement.FindAll($S::Children, $pidCond)) {{
    $c = New-Object System.Windows.Automation.AndCondition((New-Object System.Windows.Automation.PropertyCondition($A::AutomationIdProperty, 'MenuItem')), (New-Object System.Windows.Automation.PropertyCondition($A::NameProperty, '{item}')))
    $f = $top.FindFirst($S::Descendants, $c); if ($f) {{ $item = $f; break }}
  }}
}}
if (-not $item) {{ 'error=no menu item {item}'; exit }}
$item.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
Start-Sleep -Milliseconds 1200
$row = ByName $win '{row}'
if ($row) {{ 'after=' + $row.Current.ItemStatus; 'help=' + $row.Current.HelpText }}
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


def start(step):
    log = os.path.join(FIX, 'prototype.log'); size = os.path.getsize(log) if os.path.exists(log) else 0
    p = subprocess.Popen([os.path.join(FIX, 'browser.exe')], cwd=FIX)
    hwnd = 0
    for _ in range(150):
        time.sleep(0.2)
        w = [h for pid, h in ours() if pid == p.pid]
        if w and title(w[0]):
            hwnd = w[0]; break
    check(hwnd != 0, f'{step}: a fresh window started')
    if hwnd:
        time.sleep(2.5)   # chrome painted, UIA tree built
    return p, hwnd, log, size


def stop(p, hwnd, log, size):
    if hwnd:
        u32.PostMessageW(hwnd, 0x0010, 0, 0)   # WM_CLOSE to our own window
    try: p.wait(20)
    except subprocess.TimeoutExpired: p.kill()
    return open(log, 'rb').read()[size:].decode('utf-8', 'replace') if os.path.exists(log) else ''


def expect_window(step, hwnd, expect):
    names = ps(PS_NAMES.format(hwnd=int(hwnd)))
    check(print_window(hwnd, os.path.join(SHOTS, f'{step}.png')), f'{step}: PrintWindow capture of our window')
    print('     UIA:', names, '| title:', title(hwnd), flush=True)
    for key, want in expect['names'].items():
        check(names.get(key) == want, f'{step}: UIA {key} = {want!r} (got {names.get(key)!r})')
    check(title(hwnd).endswith(expect['title']), f'{step}: window title ends with {expect["title"]!r}')


def write_pref(data):
    if data is None:
        if os.path.exists(PREF): os.remove(PREF)
    else:
        with open(PREF, 'wb') as f: f.write(data)


def read_pref():
    return open(PREF, 'rb').read().decode('utf-8', 'replace') if os.path.exists(PREF) else None


KEEP = object()   # leave the preference exactly as the Settings menu saved it


def run_file_step(step, preference, expect):
    if preference is not KEEP:
        write_pref(preference)
    p, hwnd, log, size = start(step)
    if not hwnd:
        p.kill(); return
    expect_window(step, hwnd, expect)
    text = stop(p, hwnd, log, size)
    check(expect['log'] in text, f'{step}: log says {expect["log"]!r}')


def run_menu_step(step, expect_before, row, item, pending, want_file):
    p, hwnd, log, size = start(step)
    if not hwnd:
        p.kill(); return
    expect_window(step + '-before', hwnd, expect_before)
    r = ps(PS_CHOOSE.format(hwnd=int(hwnd), pid=p.pid, row=row, item=item))
    print('     menu:', r, flush=True)
    check('error' not in r, f'{step}: Réglages › {row} › {item} reached through UIA ({r.get("error", "ok")})')
    check(r.get('after') == item, f'{step}: the row now shows {item!r} (got {r.get("after")!r})')
    check(pending in r.get('help', ''), f'{step}: the row says {pending!r} (got {r.get("help")!r})')
    saved = read_pref()
    check(saved is not None and f'language={want_file}' in saved and saved.startswith('browser-ui-language 1'), f'{step}: the menu saved language={want_file} (file: {saved!r})')
    expect_window(step + '-after', hwnd, expect_before)   # the open window keeps its language: nothing half-switched
    stop(p, hwnd, log, size)


def main():
    if not os.path.isfile(os.path.join(SRC, 'browser.exe')):
        print('missing build', SRC); return 2
    if ours():
        print('a fixture browser is already running: close it first'); return 2
    if os.path.isdir(FIX): shutil.rmtree(FIX)   # our own disposable fixture only
    shutil.copytree(SRC, FIX)
    os.makedirs(SHOTS, exist_ok=True)
    fr = {'names': {'Settings': 'Réglages', 'History': 'Historique', 'NewTab': 'Nouvel onglet'}, 'title': ' — browser'}
    en = {'names': {'Settings': 'Settings', 'History': 'History', 'NewTab': 'New tab'}, 'title': ' — browser'}
    print('-- part A: preference file written by the harness', flush=True)
    run_file_step('1-default-french', None, dict(fr, log='UI LANGUAGE fr (missing)'))
    run_file_step('2-english', b'browser-ui-language 1\nlanguage=en\n', dict(en, log='UI LANGUAGE en (ok)'))
    run_file_step('3-back-to-french', b'browser-ui-language 1\nlanguage=fr\n', dict(fr, log='UI LANGUAGE fr (ok)'))
    run_file_step('4-unknown-version', b'browser-ui-language 9\nlanguage=en\n', dict(fr, log='UI LANGUAGE fr (unknown-version)'))
    print('-- part B: the real Settings menu saves, a fresh process applies', flush=True)
    write_pref(None)
    run_menu_step('5-menu-choose-english', fr, 'Langue', 'English', 'Au prochain démarrage.', 'en')
    run_file_step('6-fresh-english', KEEP, dict(en, log='UI LANGUAGE en (ok)'))
    run_menu_step('7-menu-choose-french', en, 'Language', 'Français', 'Applies at next start.', 'fr')
    run_file_step('8-fresh-french', KEEP, dict(fr, log='UI LANGUAGE fr (ok)'))
    print(f'\n{failures} failure(s)')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
