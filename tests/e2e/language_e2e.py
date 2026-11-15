"""Real-app E2E for the interface language (design/browser-languages-plan.md), on OUR fixture copy only.

Copies the built browser into build/e2e-language (portable layout: the preference, profile and log stay
in that folder), then for each step writes <fixture>\\ui-language, starts a FRESH process, and checks what
the window really shows through UI Automation (toolbar names, window title) and what the log says:
  1. no preference          -> French (default)
  2. language=en            -> English
  3. back to language=fr    -> French again (nothing switched half-way inside a process)
  4. unknown version file   -> French, reported as « unknown-version » in the log
Each step saves a screenshot of the window (build/browser-languages/captures/e2e). Only processes whose
image lives in the fixture folder are touched. The installed WebView2 runtime is used read-only; nothing
is installed, no personal profile is opened.
Usage: python tests/e2e/language_e2e.py <build dir>
"""
import ctypes, ctypes.wintypes as wt, os, shutil, subprocess, sys, time
sys.stdout.reconfigure(encoding='utf-8')

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'build', 'i18n-dev'))
FIX = os.path.join(ROOT, 'build', 'e2e-language')
SHOTS = os.path.join(ROOT, 'build', 'browser-languages', 'captures', 'e2e')
k32, u32 = ctypes.windll.kernel32, ctypes.windll.user32
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


UIA = r'''
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, System.Drawing, System.Windows.Forms
$A = [System.Windows.Automation.AutomationElement]
$win = $A::FromHandle([IntPtr]{hwnd})
$out = @()
foreach ($id in @('Settings','History','Downloads','NewTab','Reload','Address','TabList','ProfileButton')) {{
  $c = New-Object System.Windows.Automation.PropertyCondition($A::AutomationIdProperty, $id)
  $e = $win.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $c)
  if ($e) {{ $out += ($id + '=' + $e.Current.Name) }}
}}
$r = $win.Current.BoundingRectangle
$bmp = New-Object System.Drawing.Bitmap ([int]$r.Width), ([int]$r.Height)
$g = [System.Drawing.Graphics]::FromImage($bmp); $g.CopyFromScreen([int]$r.X, [int]$r.Y, 0, 0, $bmp.Size); $bmp.Save('{shot}', [System.Drawing.Imaging.ImageFormat]::Png)
$out -join "`n"
'''


def uia(hwnd, shot):
    script = UIA.format(hwnd=int(hwnd), shot=shot.replace("'", "''"))
    r = subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-Command', script], capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=60)
    names = {}
    for line in r.stdout.splitlines():
        if '=' in line:
            k, v = line.split('=', 1); names[k.strip()] = v.strip()
    return names


def run(step, preference, expect):
    pref = os.path.join(FIX, 'ui-language')
    if preference is None:
        if os.path.exists(pref): os.remove(pref)
    else:
        with open(pref, 'wb') as f: f.write(preference)
    log = os.path.join(FIX, 'prototype.log'); size = os.path.getsize(log) if os.path.exists(log) else 0
    p = subprocess.Popen([os.path.join(FIX, 'browser.exe')], cwd=FIX)
    hwnd = 0
    for _ in range(150):
        time.sleep(0.2)
        w = [h for pid, h in ours() if pid == p.pid]
        if w and title(w[0]):
            hwnd = w[0]; break
    check(hwnd != 0, f'{step}: a fresh window started')
    if not hwnd:
        p.kill(); return
    time.sleep(2.5)   # chrome painted, UIA tree built
    u32.SetForegroundWindow(hwnd)
    time.sleep(0.5)
    names = uia(hwnd, os.path.join(SHOTS, f'{step}.png'))
    print('     UIA:', names, '| title:', title(hwnd), flush=True)
    for key, want in expect['names'].items():
        check(names.get(key) == want, f'{step}: UIA {key} = {want!r} (got {names.get(key)!r})')
    check(title(hwnd).endswith(expect['title']), f'{step}: window title ends with {expect["title"]!r}')
    u32.PostMessageW(hwnd, 0x0010, 0, 0)   # WM_CLOSE
    try: p.wait(20)
    except subprocess.TimeoutExpired: p.kill()
    text = open(log, 'rb').read()[size:].decode('utf-8', 'replace') if os.path.exists(log) else ''
    check(expect['log'] in text, f'{step}: log says {expect["log"]!r}')


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
    run('1-default-french', None, dict(fr, log='UI LANGUAGE fr (missing)'))
    run('2-english', b'browser-ui-language 1\nlanguage=en\n', dict(en, log='UI LANGUAGE en (ok)'))
    run('3-back-to-french', b'browser-ui-language 1\nlanguage=fr\n', dict(fr, log='UI LANGUAGE fr (ok)'))
    run('4-unknown-version', b'browser-ui-language 9\nlanguage=en\n', dict(fr, log='UI LANGUAGE fr (unknown-version)'))
    print(f'\n{failures} failure(s)')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
