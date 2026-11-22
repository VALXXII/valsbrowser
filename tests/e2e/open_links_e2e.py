"""Real-app E2E: links and local files given to browser.exe, and local files typed in the address field.

A disposable INSTALLED layout under build/e2e-open-links/lad/browser (versions/<v>/ = a copy of the build, the stable
launcher <root>\\browser.exe = its update-helper.exe, `current`), started with LOCALAPPDATA pointing there and
BROWSER_NO_ONBOARDING=1 (fresh profile root; the onboarding is covered by its own suites). Every launch goes through
the stable launcher, exactly what Windows runs for a link once Browser is the default
("<root>\\browser.exe" --single-argument "%1", src/default-browser.h). Pages come from a local HTTP server on
127.0.0.1 only; local files from build/e2e-open-links/files. No Windows setting, registry key or default browser is
read or changed.

Scenarios:
  cold        no window yet: browser.exe http://127.0.0.1:<p>/first.html opens a window on that page.
  hand-over   a window is open: browser.exe --single-argument "http://127.0.0.1:<p>/second.html?q=--private --smoke-test"
              -> the SAME window opens it in a new tab (no second window, no private window: switches inside a link are
              never switches), the launch process ends.
  file path   browser.exe "<files>\\Ma page #1 (été).html"  -> the running window opens that file (spaces, #, accents).
  file url    browser.exe file:///<files>/url%20page.html     -> idem.
  typed path  the address field (UI Automation focus, WM_SETTEXT, Entrée posted): a quoted Windows path, then a
              file:/// URL, each opens its file.
  private     browser.exe --private http://127.0.0.1:<p>/private.html -> its own private window (never handed over).
Isolation: this script joins a job object with KILL_ON_JOB_CLOSE (every browser / WebView2 child dies with it) and
ends any browser.exe whose image lives in the fixture, by exact path, at the end.
Usage: python tests/e2e/open_links_e2e.py <build dir (tools/release/build.py --out)>
"""
import ctypes, ctypes.wintypes as wt, http.server, json, os, shutil, subprocess, sys, threading, time, urllib.parse
sys.stdout.reconfigure(encoding='utf-8')

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'build', 'ndb-release'))
BASE = os.path.join(ROOT, 'build', 'e2e-open-links')
k32, u32 = ctypes.windll.kernel32, ctypes.windll.user32
failures = 0
requests = []


def check(ok, what):
    global failures
    print(('ok   ' if ok else 'FAIL ') + what, flush=True)
    if not ok:
        failures += 1


def contain_children():
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
    ok = job and k32.SetInformationJobObject(wt.HANDLE(job), 9, ctypes.byref(info), ctypes.sizeof(info)) and \
        k32.AssignProcessToJobObject(wt.HANDLE(job), wt.HANDLE(k32.GetCurrentProcess()))
    print('job object: children die with this script' if ok else 'job object UNAVAILABLE: relying on the kill by exact path', flush=True)
    return job


def fixture_processes():
    out = subprocess.run(['powershell', '-NoProfile', '-Command',
                          "Get-CimInstance Win32_Process -Filter \"Name='browser.exe'\" | ForEach-Object { $_.ProcessId.ToString() + '|' + $_.ExecutablePath }"],
                         capture_output=True, text=True).stdout
    pids = []
    for line in out.splitlines():
        if '|' in line:
            pid, path = line.split('|', 1)
            if path and os.path.normcase(path).startswith(os.path.normcase(BASE + os.sep)):
                pids.append(int(pid))
    return pids


def kill_fixture_processes():
    for pid in fixture_processes():
        subprocess.run(['taskkill', '/F', '/T', '/PID', str(pid)], capture_output=True)
        print(f'killed leftover fixture browser {pid}', flush=True)


def browser_windows():
    """Visible LisiereBrowser windows of fixture processes: [(hwnd, title)]."""
    pids = set(fixture_processes()); found = []
    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def each(hwnd, _):
        c = ctypes.create_unicode_buffer(64); u32.GetClassNameW(hwnd, c, 64)
        p = wt.DWORD(); u32.GetWindowThreadProcessId(hwnd, ctypes.byref(p))
        if c.value == 'LisiereBrowser' and p.value in pids and u32.IsWindowVisible(hwnd):
            t = ctypes.create_unicode_buffer(512); u32.GetWindowTextW(hwnd, t, 512); found.append((hwnd, t.value))
        return True
    u32.EnumWindows(each, 0)
    return found


def wait(cond, seconds=30.0):
    end = time.time() + seconds
    while time.time() < end:
        v = cond()
        if v:
            return v
        time.sleep(0.25)
    return cond()


class Pages(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        requests.append(self.path)
        name = urllib.parse.urlparse(self.path).path.strip('/').split('.')[0] or 'root'
        body = f'<!doctype html><meta charset="utf-8"><title>E2E-{name.upper()}</title><p>{name}</p>'.encode()
        self.send_response(200); self.send_header('Content-Type', 'text/html; charset=utf-8'); self.send_header('Content-Length', str(len(body)))
        self.end_headers(); self.wfile.write(body)

    def log_message(self, *a):
        pass


def version():
    return json.load(open(os.path.join(SRC, 'version.json'), encoding='utf-8'))['version']


def make_root():
    if os.path.isdir(BASE):
        shutil.rmtree(BASE)   # our own disposable fixture only
    root = os.path.join(BASE, 'lad', 'browser'); v = version()
    shutil.copytree(SRC, os.path.join(root, 'versions', v))
    shutil.copy(os.path.join(root, 'versions', v, 'update-helper.exe'), os.path.join(root, 'browser.exe'))   # the stable launcher
    os.makedirs(os.path.join(root, 'updates')); os.makedirs(os.path.join(root, 'data'))
    open(os.path.join(root, 'current'), 'w', newline='\n').write(v + '\n')
    files = os.path.join(BASE, 'files', 'dossier avec espaces')
    os.makedirs(files)
    for name, title in (('Ma page #1 (été).html', 'E2E-FILE-PATH'), ('url page.html', 'E2E-FILE-URL'),
                        ('typed path.html', 'E2E-TYPED-PATH'), ('typed url.html', 'E2E-TYPED-URL')):
        open(os.path.join(files, name), 'w', encoding='utf-8').write(f'<!doctype html><meta charset="utf-8"><title>{title}</title><p>{title}</p>')
    return root, files


def launch(root, *args, env):
    """Through the stable launcher, as Windows does; returns its exit code (it ends at once)."""
    p = subprocess.run([os.path.join(root, 'browser.exe'), *args], env=env, cwd=os.path.dirname(root), timeout=60)
    return p.returncode


def titled(text):
    return lambda: [w for w in browser_windows() if text in w[1]]


def log_text(root):
    p = os.path.join(root, 'data', 'browser.log')
    return open(p, 'rb').read().decode('utf-8', 'replace') if os.path.exists(p) else ''


PS_TYPE = r'''
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class K {{
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr p, IntPtr a, string cls, string name);
  [DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="SendMessageW")] public static extern IntPtr SendText(IntPtr h, uint m, IntPtr w, string l);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
}}
"@
$A = [System.Windows.Automation.AutomationElement]
$win = $A::FromHandle([IntPtr]{hwnd})
$addr = $win.FindFirst([System.Windows.Automation.TreeScope]::Descendants, (New-Object System.Windows.Automation.PropertyCondition($A::AutomationIdProperty, 'Address')))
if (-not $addr) {{ 'error=no Address'; exit }}
$addr.SetFocus(); Start-Sleep -Milliseconds 400
$edit = [K]::FindWindowEx([IntPtr]{hwnd}, [IntPtr]::Zero, 'Edit', $null)
for ($i = 0; $i -lt 10 -and -not [K]::IsWindowVisible($edit); $i++) {{ $edit = [K]::FindWindowEx([IntPtr]{hwnd}, $edit, 'Edit', $null); if ($edit -eq [IntPtr]::Zero) {{ break }} }}
if ($edit -eq [IntPtr]::Zero) {{ 'error=no edit'; exit }}
[void][K]::SendText($edit, 0x000C, [IntPtr]::Zero, [IO.File]::ReadAllText('{textfile}', [Text.Encoding]::UTF8))
Start-Sleep -Milliseconds 300
[void][K]::PostMessage($edit, 0x0100, [IntPtr]0x0D, [IntPtr]0x001C0001)
'typed=1'
'''


def type_in_address(hwnd, text):
    f = os.path.join(BASE, 'typed.txt')
    open(f, 'w', encoding='utf-8').write(text)
    r = subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-Command', PS_TYPE.format(hwnd=int(hwnd), textfile=f)],
                       capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=120)
    return r.stdout.strip()


def run():
    if not os.path.isfile(os.path.join(SRC, 'browser.exe')):
        print('missing build', SRC); return 2
    if fixture_processes():
        print('a fixture browser is already running: close it first'); return 2
    root, files = make_root()
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Pages)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    port = server.server_address[1]; site = f'http://127.0.0.1:{port}'
    env = dict(os.environ, LOCALAPPDATA=os.path.join(BASE, 'lad'), BROWSER_NO_ONBOARDING='1')
    env.pop('WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS', None)
    print(f'build {SRC} version {version()} pages {site}', flush=True)

    # cold: no window yet.
    check(launch(root, f'{site}/first.html', env=env) == 0, 'cold: launcher started the installed browser')
    first = wait(titled('E2E-FIRST'), 60)
    check(bool(first) and '/first.html' in requests, f'cold: a window opened on the link ({first and first[0][1]})')
    check(bool(first) and first[0][1].endswith('— Browser'), 'cold: window title names « Browser »')
    check(len(browser_windows()) == 1, 'cold: one window')
    hwnd = first[0][0] if first else 0

    # hand-over: the running window opens the next link; switches inside it are never switches.
    time.sleep(1.5)
    before = len(requests)
    check(launch(root, '--single-argument', f'{site}/second.html?q=--private --smoke-test', env=env) == 0, 'hand-over: launcher exit 0')
    second = wait(titled('E2E-SECOND'), 30)
    check(bool(second) and any(r.startswith('/second.html?q=--private') for r in requests[before:]), f'hand-over: the link loaded ({requests[before:]})')
    check(len(browser_windows()) == 1 and bool(second) and second[0][0] == hwnd, f'hand-over: the SAME window, no second window ({browser_windows()})')
    check(not any('privée' in t or 'private' in t for _, t in browser_windows()), 'hand-over: « --private » inside the link opened no private window')
    log = wait(lambda: 'OPEN handed target in a new tab' in log_text(root) and log_text(root), 10) or log_text(root)
    check('OPEN launch target handed to the running window' in log and 'OPEN handed target in a new tab' in log, 'hand-over: logged by both sides (sender, receiver)')
    time.sleep(1.0)
    check(len(fixture_processes()) == 1, f'hand-over: the second browser process ended ({fixture_processes()})')

    # local files on the command line: a Windows path with spaces, # and accents; a file:/// URL.
    path = os.path.join(files, 'Ma page #1 (été).html')
    check(launch(root, path, env=env) == 0, 'file path: launcher exit 0')
    check(bool(wait(titled('E2E-FILE-PATH'), 30)), 'file path: the running window opened « Ma page #1 (été).html »')
    url = 'file:///' + urllib.parse.quote(os.path.join(files, 'url page.html').replace('\\', '/'), safe='/:')
    check(launch(root, '--single-argument', url, env=env) == 0, 'file url: launcher exit 0')
    check(bool(wait(titled('E2E-FILE-URL'), 30)), f'file url: the running window opened {url}')
    check(len(browser_windows()) == 1, 'files: still one window')

    # typed in the address field: a quoted Windows path (Explorer « Copier en tant que chemin d'accès »), a file:/// URL.
    typed = type_in_address(hwnd, '"' + os.path.join(files, 'typed path.html') + '"')
    check('typed=1' in typed, f'typed path: entered ({typed})')
    check(bool(wait(titled('E2E-TYPED-PATH'), 30)), 'typed path: the quoted Windows path opened the file (not a search)')
    typed = type_in_address(hwnd, 'file:///' + urllib.parse.quote(os.path.join(files, 'typed url.html').replace('\\', '/'), safe='/:'))
    check('typed=1' in typed, f'typed url: entered ({typed})')
    check(bool(wait(titled('E2E-TYPED-URL'), 30)), 'typed url: the file:/// URL opened the file')

    # private: never handed over, its own private window opens the link.
    check(launch(root, '--private', f'{site}/private.html', env=env) == 0, 'private: launcher exit 0')
    priv = wait(lambda: [w for w in browser_windows() if 'E2E-PRIVATE' in w[1]], 60)
    check(bool(priv) and priv[0][0] != hwnd and len(browser_windows()) == 2, f'private: its own window on the link ({priv and priv[0][1]})')

    for h, _ in browser_windows():
        u32.PostMessageW(h, 0x0010, 0, 0)   # WM_CLOSE to our own windows
    wait(lambda: not fixture_processes(), 25)
    server.shutdown()
    print(f'\n{failures} failure(s)')
    return 1 if failures else 0


def main():
    contain_children()
    try:
        return run()
    finally:
        kill_fixture_processes()


if __name__ == '__main__':
    sys.exit(main())
