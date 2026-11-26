"""A download the engine holds for the user's decision (B07b « À confirmer »): real browser, real engine, local server only.

    python tests/downloads/held_download_e2e.py --app <built app dir> --out <dir> [--only keep,discard,...] [--repeat 3]
           [--desktop hidden|visible]

Engine facts (tests/downloads/download_engine_probe.py, measured 2026-11-23 on WebView2 157 and 2026-11-26 on 158.0.4370.33):
an unsigned, never-seen .exe arrives whole at its final path, then stays IN_PROGRESS with no interrupt reason and no further
event: Microsoft Defender SmartScreen waits for « keep / delete » in the engine's own flyout, which this browser replaces
(with SmartScreen off the same file completes). An http:// download started from an https:// page is held the same way
(Chromium's insecure-download warning), whatever its type. Cancel() deletes the held file at once when the engine's own pass
on it (the zone mark) is over, seconds later otherwise; a second hard link to it survives. The engine deletes a held file
when it exits.
Expected (« Conserver », src/main.cpp KeepHeldDownload / WaitEnginePass / WatchKept):
  - held:            log « DOWNLOAD held by the engine », the popover's card exposes « Supprimer » / « Conserver » (UIA);
  - keep (x --repeat), keep-again (two invokes), keep-insecure (an http:// file from an https:// page, throwaway certificate,
                     this test's engine ignores certificate errors): the row is a finished download at once; after the guard
                     our own name (<app>/kept-downloads) goes and the file is settled (no second attachment services pass
                     when the engine marked it): whole at its path,
                     zone mark, visible, nothing else in the download folder or in kept-downloads; it survives the exit;
  - keep-late-removal: the test removes the path right after the file was given back (as the engine can, once more):
                     it is given back again, then the same as keep;
  - keep-read:       another program reads the held file across the cancel without allowing its deletion: the engine's
                     removal fails, the file stays at its path; at exit the row points at a whole file, nothing of ours left;
  - keep-then-discard: « Supprimer » 300 ms after « Conserver » is no longer offered (the row is finished), the file is kept;
  - keep-then-close: the window closed right after « Conserver »: the row points at a whole, visible file: its own path when
                     the engine's removal was seen and undone (its pass over), else a name the engine never knew; nothing of ours left;
  - keep-early-cancel (A): the cancel forced while the engine's pass on a 40 MB file still runs (test hook
                     BROWSER_TEST_KEEP_CAP_MS=0): its late removal is seen and undone, the file ends kept as above;
  - keep-save-removes (B): the attachment services pass removes the kept file without a verdict (test hook
                     BROWSER_TEST_KEEP_SAVE=remove): it is given back from our name, the file ends kept;
  - keep-then-kill (x --kill-repeat): the browser and its engine killed right after « Conserver », then started again on
                     the same profile: the file still whole at its path 60 s after that start (the restarted engine's own
                     clean-up of its unfinished download: measured ~8 s after a kill), and our watched leftover gone in the end;
  - keep-then-restart: a keep settled, the browser closed and started again: the file still whole 60 s after that start;
  - keep-blocked:    our name cannot be made (a folder sits at it): « Échec · Erreur de fichier » with « Retirer », no dead
                     « Conserver », the engine's held file deleted, the folder left alone, nothing else left;
  - discard:         UIA Invoke on « Supprimer » → the file is gone and the row with it;
  - control:         a plain .bin completes as before and is never held.
  - card-slow-check: an .exe while the reputation service never answers (SmartScreen hosts on a silent local socket in this
                     test's engine): no « À confirmer » card, the engine completes it by itself;
  - card-plain-big:  a 300 MB .bin: no card, completes;
  - card-slow-check also reads the checking row (UIA): Cancel there, no Pause;
  - card-cancel-checking: « Annuler » on that checking row: the row stays cancelled, the engine's file goes, nothing is left;
  - card-real-hold:  a fresh unsigned .exe (a real SmartScreen hold): the card, promptly (seconds after the start recorded);
  - card-never-marked: no zone mark can come (test hook): the card after the fallback, « Conserver » keeps the file.
  Every keep case records the download folder's events (ReadDirectoryChangesW, timed). Engine facts behind the design:
  tests/downloads/download_engine_probe.py --observe (the engine removes a cancelled held file by name when its own pass on
  the file, the one that writes the zone mark, ends: at once when it was over, seconds later when it still ran).

Isolation: one throwaway copy of the app per case (payload only, its own profile/settings/log beside the exe) and a sealed
home (USERPROFILE / APPDATA / LOCALAPPDATA under the copy, PATH = Windows only: no agent CLI reachable; nothing here opens
the chat); downloads go to <out>/<case>/downloads only; the .exe is compiled here (a unique unsigned binary, never run);
a local HTTP fixture on 127.0.0.1. --desktop hidden (default): the browser runs on a Windows desktop that is never displayed
(nothing on screen, no focus taken); UIA Invoke reaches the popover through its own provider from a PowerShell started on that
desktop (no SendInput, no click). Every process started here is in a job object (KILL_ON_JOB_CLOSE), one per browser: a case
waits for that job to empty (the engine gone) and stops what is left through it, never by name. No real site, no real profile.
"""
from __future__ import annotations
import argparse, ctypes, ctypes.wintypes as W, http.server, importlib.util, json, os, random, shutil, ssl, struct, subprocess, sys, threading, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
_spec = importlib.util.spec_from_file_location('download_navigation_e2e', Path(__file__).with_name('download_navigation_e2e.py'))
dn = importlib.util.module_from_spec(_spec); _spec.loader.exec_module(dn)
k32 = ctypes.WinDLL('kernel32', use_last_error=True)
u32 = ctypes.WinDLL('user32', use_last_error=True)
u32.CreateDesktopW.restype = W.HANDLE
u32.SetThreadDesktop.argtypes = [W.HANDLE]
u32.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
k32.CreateJobObjectW.restype = W.HANDLE
k32.CreateJobObjectW.argtypes = [ctypes.c_void_p, W.LPCWSTR]
k32.SetInformationJobObject.argtypes = [W.HANDLE, ctypes.c_int, ctypes.c_void_p, W.DWORD]
k32.QueryInformationJobObject.argtypes = [W.HANDLE, ctypes.c_int, ctypes.c_void_p, W.DWORD, ctypes.c_void_p]
k32.AssignProcessToJobObject.argtypes = [W.HANDLE, W.HANDLE]
k32.TerminateJobObject.argtypes = [W.HANDLE, W.UINT]
k32.ResumeThread.argtypes = [W.HANDLE]
k32.WaitForSingleObject.argtypes = [W.HANDLE, W.DWORD]
k32.CloseHandle.argtypes = [W.HANDLE]
k32.GetFileAttributesW.argtypes = [W.LPCWSTR]
k32.GetFileAttributesW.restype = W.DWORD
FILES: dict[str, tuple[bytes, str]] = {}
WM_CLOSE = 0x0010

UIA = r'''param([int]$Hwnd, [string]$Id, [string]$Out, [switch]$Probe, [int]$Times = 1, [string]$ThenId = '', [int]$ThenMs = 0)
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
$r = ''
try {
  $root = [System.Windows.Automation.AutomationElement]::FromHandle([IntPtr]$Hwnd)
  $cond = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::AutomationIdProperty, $Id)
  $e = $root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $cond)
  if (-not $e) { $r = 'missing' }
  elseif ($Probe) { $r = 'found' }
  else {
    $p = $null; $name = $e.Current.Name
    if ($e.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref]$p)) { for ($i = 0; $i -lt $Times; $i++) { $p.Invoke() }; $r = 'invoked:' + $name } else { $r = 'no-invoke' }
    if ($ThenId) {   # a second button of the same popover, $ThenMs later (one session: no PowerShell start in between)
      Start-Sleep -Milliseconds $ThenMs
      $c2 = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::AutomationIdProperty, $ThenId)
      $e2 = $root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $c2); $p2 = $null
      if ($e2 -and $e2.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref]$p2)) { $p2.Invoke(); $r += ';then invoked:' + $e2.Current.Name } else { $r += ';then missing' }
    }
  }
} catch { $r = 'error: ' + $_.Exception.Message }
[System.IO.File]::WriteAllText($Out, $r)
'''


class Fixture(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def do_GET(self):
        path = self.path.split('?')[0]
        if path == '/page':
            body, ctype, name = b'<!doctype html><title>OPENER</title><p>held download fixture</p>', 'text/html; charset=utf-8', None
        elif path[1:] in FILES:
            body, name = FILES[path[1:]]; ctype = 'application/octet-stream'
        else:
            self.send_response(404); self.send_header('Content-Length', '0'); self.end_headers(); return
        self.send_response(200); self.send_header('Content-Type', ctype)
        if name:
            self.send_header('Content-Disposition', f'attachment; filename="{name}"')
        self.send_header('Content-Length', str(len(body))); self.send_header('Cache-Control', 'no-store'); self.end_headers(); self.wfile.write(body)

    def log_message(self, *a):
        pass


def fresh_exe(work: Path, tag: str) -> bytes:
    """A unique, unsigned console program (never executed): its hash has no reputation anywhere."""
    src = work / f'{tag}.c'; exe = work / f'{tag}.exe'
    src.write_text('int main(void){volatile unsigned x=%du;return (int)(x&1u);}\n' % random.getrandbits(31), encoding='ascii')
    r = subprocess.run([str(ROOT / 'tools/w64devkit/bin/gcc.exe'), '-O0', str(src), '-o', str(exe)], capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError('fixture exe build failed: ' + r.stderr)
    return exe.read_bytes()


# ---------------------------------------------------------------- processes: one job per browser, a desktop never shown
class SI(ctypes.Structure):
    _fields_ = [('cb', W.DWORD), ('lpReserved', W.LPWSTR), ('lpDesktop', W.LPWSTR), ('lpTitle', W.LPWSTR), ('dwX', W.DWORD), ('dwY', W.DWORD),
                ('dwXSize', W.DWORD), ('dwYSize', W.DWORD), ('dwXCountChars', W.DWORD), ('dwYCountChars', W.DWORD), ('dwFillAttribute', W.DWORD),
                ('dwFlags', W.DWORD), ('wShowWindow', W.WORD), ('cbReserved2', W.WORD), ('lpReserved2', ctypes.c_void_p),
                ('hStdInput', W.HANDLE), ('hStdOutput', W.HANDLE), ('hStdError', W.HANDLE)]


class PI(ctypes.Structure):
    _fields_ = [('hProcess', W.HANDLE), ('hThread', W.HANDLE), ('dwProcessId', W.DWORD), ('dwThreadId', W.DWORD)]


class Accounting(ctypes.Structure):
    _fields_ = [('TotalUserTime', ctypes.c_longlong), ('TotalKernelTime', ctypes.c_longlong), ('ThisPeriodTotalUserTime', ctypes.c_longlong),
                ('ThisPeriodTotalKernelTime', ctypes.c_longlong), ('TotalPageFaultCount', W.DWORD), ('TotalProcesses', W.DWORD),
                ('ActiveProcesses', W.DWORD), ('TotalTerminatedProcesses', W.DWORD)]


class Job:
    def __init__(self):
        self.h = k32.CreateJobObjectW(None, None)
        info = dn.ExtendedLimit(); info.BasicLimitInformation.LimitFlags = 0x2000   # KILL_ON_JOB_CLOSE
        if not self.h or not k32.SetInformationJobObject(self.h, 9, ctypes.byref(info), ctypes.sizeof(info)):
            raise OSError('job object')

    def active(self):
        a = Accounting(); k32.QueryInformationJobObject(self.h, 1, ctypes.byref(a), ctypes.sizeof(a), None); return a.ActiveProcesses

    def drain(self, timeout):
        """Waits for every process of the job to exit by itself, then stops what is left (through the job only)."""
        end = time.monotonic() + timeout
        while self.active() and time.monotonic() < end:
            time.sleep(.2)
        left = self.active(); k32.TerminateJobObject(self.h, 1); k32.CloseHandle(self.h); self.h = None; return left


class Desk:
    def __init__(self, hidden: bool):
        self.name = f'held-dl-{os.getpid()}' if hidden else None
        self.h = u32.CreateDesktopW(self.name, None, None, 0, 0x10000000, None) if hidden else None
        if hidden and not self.h:
            raise OSError(f'CreateDesktopW failed {ctypes.get_last_error()}')

    def run(self, fn):
        """fn on a thread attached to this desktop (window enumeration and posting see only their own desktop)."""
        if not self.h:
            return fn()
        out = {}

        def body():
            u32.SetThreadDesktop(self.h); out['r'] = fn()
        t = threading.Thread(target=body); t.start(); t.join(); return out.get('r')

    def start(self, exe, cmdline, cwd, env=None, job=None, flags=0):
        si = SI(); si.cb = ctypes.sizeof(si)
        if self.name:
            si.lpDesktop = 'WinSta0\\' + self.name
        si.dwFlags = 1; si.wShowWindow = 1   # SW_SHOWNORMAL, on that desktop
        pi = PI(); block = ctypes.create_unicode_buffer(''.join(f'{k}={v}\0' for k, v in env.items()) + '\0') if env else None
        if not k32.CreateProcessW(exe, ctypes.create_unicode_buffer(cmdline) if cmdline else None, None, None, False,
                                  flags | 0x4 | (0x400 if env else 0), block, cwd, ctypes.byref(si), ctypes.byref(pi)):
            raise OSError(f'CreateProcessW failed {ctypes.get_last_error()}')
        if job and not k32.AssignProcessToJobObject(job.h, pi.hProcess):
            k32.TerminateProcess(pi.hProcess, 1); raise OSError('AssignProcessToJobObject failed')
        k32.ResumeThread(pi.hThread); return pi


class FolderWatch:
    """Every name added / removed / renamed in the download folder, timed (ReadDirectoryChangesW): what really happens to
    the file around « Conserver ». Evidence only; it never touches the files."""
    ACTIONS = {1: 'added', 2: 'removed', 3: 'modified', 4: 'renamed-from', 5: 'renamed-to'}

    def __init__(self, folder: Path):
        k32.CreateFileW.restype = W.HANDLE
        k32.CreateFileW.argtypes = [W.LPCWSTR, W.DWORD, W.DWORD, ctypes.c_void_p, W.DWORD, W.DWORD, W.HANDLE]
        k32.ReadDirectoryChangesW.argtypes = [W.HANDLE, ctypes.c_void_p, W.DWORD, W.BOOL, W.DWORD, ctypes.POINTER(W.DWORD), ctypes.c_void_p, ctypes.c_void_p]
        k32.CancelIoEx.argtypes = [W.HANDLE, ctypes.c_void_p]
        self.h = k32.CreateFileW(str(folder), 0x0001, 0x7, None, 3, 0x02000000, None)   # FILE_LIST_DIRECTORY, share all, BACKUP_SEMANTICS
        self.events, self.t0, self.stop = [], time.monotonic(), False
        threading.Thread(target=self.run, daemon=True).start()

    def run(self):
        buf = ctypes.create_string_buffer(65536); got = W.DWORD()
        while not self.stop:
            if not k32.ReadDirectoryChangesW(self.h, buf, len(buf), False, 0x1 | 0x2 | 0x8 | 0x10, ctypes.byref(got), None, None) or self.stop:
                return
            at, off = round((time.monotonic() - self.t0) * 1000), 0
            while got.value:
                nxt, action, n = struct.unpack_from('<III', buf, off)
                name = buf.raw[off + 12: off + 12 + n].decode('utf-16-le')
                self.events.append(f'{at}ms {self.ACTIONS.get(action, action)} {name}')
                if not nxt:
                    break
                off += nxt

    def close(self):
        self.stop = True; k32.CancelIoEx(self.h, None); k32.CloseHandle(self.h); return self.events


def windows_of(pid, cls):
    found = []

    @ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    def each(hwnd, _):
        p = W.DWORD(); u32.GetWindowThreadProcessId(hwnd, ctypes.byref(p))
        c = ctypes.create_unicode_buffer(64); u32.GetClassNameW(hwnd, c, 64)
        if p.value == pid and c.value == cls:
            found.append(hwnd)
        return True
    u32.EnumWindows(each, 0)
    return found


def sealed_env(app: Path, extra_args: str, cdp_port: int, extra_env=None) -> dict:
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith(('BROWSER_', 'WEBVIEW2_'))}
    for k in [k for k in env if k.upper() in ('CLAUDE_CONFIG_DIR', 'CODEX_HOME', 'ANTHROPIC_API_KEY', 'OPENAI_API_KEY', 'NODE_OPTIONS')]:
        env.pop(k)
    home = app / 'home'
    for sub in ('AppData/Roaming', 'AppData/Local', '.local/bin'):
        (home / sub).mkdir(parents=True, exist_ok=True)
    env.update(USERPROFILE=str(home), HOMEDRIVE=str(home)[:2], HOMEPATH=str(home)[2:], APPDATA=str(home / 'AppData' / 'Roaming'),
               LOCALAPPDATA=str(home / 'AppData' / 'Local'), PATH=r'C:\Windows\System32;C:\Windows;C:\Windows\System32\WindowsPowerShell\v1.0')
    env['WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS'] = f'--remote-debugging-port={cdp_port} --remote-debugging-address=127.0.0.1 {extra_args}'.strip()
    env.update(extra_env or {})   # the browser's non-release test hooks (BROWSER_TEST_KEEP_*), never an agent's
    for n in ('claude', 'codex', 'node', 'npx', 'npm'):   # SAFETY before any launch: no agent CLI reachable
        assert shutil.which(n, path=env['PATH']) is None, f'{n} reachable from the sealed PATH: refused'
    assert str(home).startswith(str(app)) and env['USERPROFILE'] == str(home), 'sealed home: refused'
    return env


class Browser:
    def __init__(self, app: Path, base: str, downloads: Path, desk: Desk, extra_args='', extra_env=None):
        self.app, self.desk, self.port = app, desk, dn.free_port()
        self.http = dn.urllib.request.build_opener(dn.urllib.request.ProxyHandler({}))
        (app / 'session').write_text(f'browser-session-2\n0\n1\n"{base}/page"\n0\n', encoding='utf-8', newline='\n')
        (app / 'settings').write_text('browser-settings 1\nrestore-tabs=1\nconfirm-close-tabs=0\n'
                                      f'download-folder={downloads}\nask-where-to-save=0\n', encoding='utf-8', newline='\n')
        self.job = Job(); self.sockets = []
        self.pi = desk.start(str(app / 'browser.exe'), None, str(app), sealed_env(app, extra_args, self.port, extra_env), self.job)
        self.pid = self.pi.dwProcessId

    targets = dn.Browser.targets
    connect = dn.Browser.connect
    log = dn.Browser.log
    model = dn.Browser.model

    def popover(self):
        hs = self.desk.run(lambda: windows_of(self.pid, 'LisiereDownloads')) or []
        return hs[0] if hs else None

    def uia(self, uia_script: Path, automation_id: str, probe=False, times=1, then_id='', then_ms=0):
        h = self.popover()
        if not h:
            return 'no popover'
        out = uia_script.parent / f'uia-{random.getrandbits(32):08x}.txt'
        cmd = (f'powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "{uia_script}" -Hwnd {h} -Id {automation_id} '
               f'-Out "{out}" -Times {times}' + (' -Probe' if probe else '') + (f' -ThenId {then_id} -ThenMs {then_ms}' if then_id else ''))
        pi = self.desk.start(None, cmd, str(uia_script.parent), flags=0x08000000)   # CREATE_NO_WINDOW
        k32.WaitForSingleObject(pi.hProcess, 120000); k32.CloseHandle(pi.hProcess); k32.CloseHandle(pi.hThread)
        try:
            return out.read_text(encoding='utf-8-sig').strip()
        except OSError:
            return 'no output'

    def kill(self):
        """The browser and its engine stopped at once (as a crash or a killed process): through its job only."""
        k32.TerminateJobObject(self.job.h, 1); k32.WaitForSingleObject(self.pi.hProcess, 20000)
        left = self.job.drain(5); k32.CloseHandle(self.pi.hProcess); k32.CloseHandle(self.pi.hThread); return left

    def close(self):
        """WM_CLOSE to our window, then the engine is given time to exit by itself (it deletes what it still holds then)."""
        for ws in self.sockets:
            try:
                ws.s.close()
            except OSError:
                pass
        hs = self.desk.run(lambda: windows_of(self.pid, 'LisiereBrowser')) or []
        for h in hs:
            u32.PostMessageW(h, WM_CLOSE, 0, 0)
        k32.WaitForSingleObject(self.pi.hProcess, 20000)
        left = self.job.drain(25)
        k32.CloseHandle(self.pi.hProcess); k32.CloseHandle(self.pi.hThread)
        return left


def main() -> int:
    ap = argparse.ArgumentParser(); ap.add_argument('--app', required=True); ap.add_argument('--out', required=True)
    ap.add_argument('--only', default=''); ap.add_argument('--desktop', choices=('hidden', 'visible'), default='hidden')
    ap.add_argument('--repeat', type=int, default=3, help='runs of the plain keep case (the engine\'s late removal is a race)')
    ap.add_argument('--kill-repeat', type=int, default=1, help='runs of keep-then-kill (a race with the restarted engine\'s clean-up)')
    a = ap.parse_args()
    src, out = Path(a.app).resolve(), Path(a.out).resolve()
    shutil.rmtree(out, ignore_errors=True); out.mkdir(parents=True)
    work = out / 'work'; work.mkdir()
    uia = work / 'uia.ps1'; uia.write_text(UIA, encoding='utf-8')
    keeps = ['keep'] + [f'keep-r{n}' for n in range(2, a.repeat + 1)]
    kills = ['keep-then-kill'] + [f'keep-then-kill-r{n}' for n in range(2, a.kill_repeat + 1)]
    for tag in keeps + kills + ['keep-again', 'keep-late-removal', 'keep-read', 'keep-then-discard', 'keep-then-close', 'keep-then-restart', 'keep-insecure', 'keep-blocked',
                        'keep-early-cancel', 'keep-save-removes', 'discard']:
        FILES[tag + '.exe'] = (fresh_exe(work, tag.replace('-', '_')), f'held-{tag}.exe')
    FILES['keep-early-cancel.exe'] = (FILES['keep-early-cancel.exe'][0] + os.urandom(150 << 20), 'held-keep-early-cancel.exe')   # a long scan
    FILES['plain.bin'] = (os.urandom(150000), 'plain-control.bin')
    for tag in ('card-slow-check', 'card-real-hold', 'card-never-marked', 'card-cancel-checking'):
        FILES[tag + '.exe'] = (fresh_exe(work, tag.replace('-', '_')), f'{tag}.exe')
    FILES['card-plain-big.bin'] = (os.urandom(300 << 20), 'card-plain-big.bin')
    # A reputation service that never answers (SmartScreen behind a VPN / proxy that swallows it): this test's engine maps
    # the SmartScreen hosts to a local socket that accepts and never replies. Nothing outside the test's engine changes.
    import socket
    blackhole = socket.socket(); blackhole.bind(('127.0.0.1', 0)); blackhole.listen(64); swallowed = []
    def swallow():
        while True:
            try:
                swallowed.append(blackhole.accept()[0])
            except OSError:
                return
    threading.Thread(target=swallow, daemon=True).start()
    slow_reputation = f'--host-resolver-rules="MAP *smartscreen* 127.0.0.1:{blackhole.getsockname()[1]}"'
    srv = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Fixture); port = srv.server_port; base = f'http://127.0.0.1:{port}'
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    secure = None   # the fixture page over https (throwaway self-signed certificate made by Git's openssl, nothing installed)
    openssl = Path(r'C:\Program Files\Git\usr\bin\openssl.exe')
    if openssl.is_file():
        subprocess.run([str(openssl), 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-keyout', str(work / 'k.pem'), '-out', str(work / 'c.pem'),
                        '-days', '1', '-subj', '/CN=127.0.0.1', '-addext', 'subjectAltName=IP:127.0.0.1'], capture_output=True)
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); ctx.load_cert_chain(str(work / 'c.pem'), str(work / 'k.pem'))
        tls = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Fixture); tls.socket = ctx.wrap_socket(tls.socket, server_side=True)
        threading.Thread(target=tls.serve_forever, daemon=True).start(); secure = f'https://127.0.0.1:{tls.server_port}'
    desk = Desk(a.desktop == 'hidden')
    only = {n for n in a.only.split(',') if n}
    res = {'checks': {}, 'failures': []}

    def check(name, ok, **seen):
        res['checks'][name] = {'ok': bool(ok), **seen}
        if not ok:
            res['failures'].append(name)
        print(('ok   ' if ok else 'FAIL ') + name + ' ' + json.dumps(seen, ensure_ascii=False, default=str)[:3000], flush=True)

    def launch(name, page=base, extra_args='', extra_env=None):
        app = out / name / 'app'; app.mkdir(parents=True)
        for f in ('browser.exe', 'WebView2Loader.dll', 'version.json'):
            if (src / f).exists():
                shutil.copy2(src / f, app / f)
        shutil.copytree(src / 'assets', app / 'assets')
        downloads = out / name / 'downloads'; downloads.mkdir()
        b = Browser(app, page, downloads, desk, extra_args, extra_env)
        op = b.connect(lambda t: t.get('url', '') == page + '/page', 60)
        if not op or not dn.wait(lambda: 'NAVIGATION OK ' + page + '/page' in b.log(), 30):
            b.close(); raise RuntimeError(f'{name}: the fixture page did not load')
        time.sleep(1)
        return b, op, downloads

    def lines(b, mark):
        return [l for l in b.log()[mark:].splitlines() if l.startswith('DOWNLOAD')]

    def row_of(b, file_name):
        return next((l for l in b.model().splitlines() if file_name in l), '')

    def zone(path: Path):
        try:
            return open(str(path) + ':Zone.Identifier', encoding='utf-8', errors='replace').read()
        except OSError:
            return ''

    def stamp(watch, text):
        watch.events.append(f'{round((time.monotonic() - watch.t0) * 1000)}ms -- {text}')

    def hidden(path: Path):
        a = k32.GetFileAttributesW(str(path)); return a != 0xFFFFFFFF and bool(a & 0x2)

    def row_file(b, name):
        """The file the history row of `name` points at (after « Conserver » it may be « name (1).exe »)."""
        row = row_of(b, Path(name).stem)
        f = row.split('\t')
        return Path(f[3]) if len(f) > 3 else None, row

    def keep_case(name, page=base, file_base=None, extra_args='', times=1, reader_ms=0, then='', extra_env=None, expect_log='', must_settle=None):
        """« Conserver » on a held download: a finished download at once; once the engine has let go and its removal is
        undone, our own name (in the app's kept-downloads folder) goes after the guard and the file is handed to attachment
        services: whole at its path, marked, visible, nothing else left in the download folder or in kept-downloads, and
        still there once the browser and its engine have exited. reader_ms: another program reads the file across the
        cancel without allowing its deletion (the engine's removal fails); then: 'late-removal' (the path removed by the test
        right after the file was given back: a removal by the engine once more), 'discard' (« Supprimer » 300 ms after
        « Conserver »: no longer offered), 'close' (the window closed right after « Conserver »)."""
        file_base = file_base or base
        must_settle = (bool(expect_log) or then == 'late-removal') if must_settle is None else must_settle
        b, op, downloads = launch(name, page, extra_args, extra_env); target = downloads / f'held-{name}.exe'; size = len(FILES[name + '.exe'][0])
        kept_dir = b.app / 'kept-downloads'
        watch = FolderWatch(downloads); closed = False; left = None
        events = lambda: [e for e in watch.events if 'Zone.Identifier' not in e]
        ours = lambda: sorted(p.name for p in kept_dir.iterdir()) if kept_dir.exists() else []
        try:
            mark = len(b.log()); dn.ev(op, f"location.href='{file_base}/{name}.exe'")
            held = dn.wait(lambda: 'DOWNLOAD held by the engine' in b.log()[mark:], 40)
            whole = target.exists() and target.stat().st_size == size
            card = dn.wait(lambda: b.uia(uia, 'DownloadKeep', True) == 'found' and b.uia(uia, 'DownloadDiscard', True) == 'found', 15)
            check(f'{name}: held_detected', held and whole and card, held=bool(held), file_whole_while_held=whole, card_buttons=bool(card), log=lines(b, mark))
            reader = None
            if reader_ms:
                reader = k32.CreateFileW(str(target), 0x80000000, 0x1 | 0x2, None, 3, 0, None)   # GENERIC_READ, share read + write only
                threading.Timer(reader_ms / 1000, lambda: (k32.CloseHandle(reader), stamp(watch, 'the reader closed the file'))).start()
                stamp(watch, f'a reader opened the file for {reader_ms} ms (no delete sharing)')
            stamp(watch, f'UIA Invoke « Conserver » x{times}' + (', then « Supprimer » 300 ms later' if then == 'discard' else ''))
            r = b.uia(uia, 'DownloadKeep', times=times, then_id='DownloadDiscard' if then == 'discard' else '', then_ms=300)
            stamp(watch, 'UIA Invoke returned: ' + r)
            if then in ('kill', 'restart'):
                # kill: stopped right after « Conserver » (browser and engine, as a crash); restart: the keep settled (our name
                # gone after its guard), then the browser closed normally. Started again on the same profile, the file must
                # still be whole at its path 60 s later (the restarted engine cleans its unfinished downloads by name: measured
                # ~8 s after a kill), and, once the watch of an adopted leftover is over, nothing of ours left.
                if then == 'restart':
                    dn.wait(lambda: any(s in b.log()[mark:] for s in ('DOWNLOAD kept file handed to attachment services', 'attachment services refused',
                                                                      "marked and checked by the engine's own pass")), 150)
                    left = b.close(); closed = True; stamp(watch, 'browser and engine closed (the keep settled)')
                else:
                    left = b.kill(); closed = True; stamp(watch, 'browser and engine killed')
                left_over = ours(); mark2 = len(b.log()); restarted = time.monotonic()
                b2 = Browser(b.app, page, downloads, desk, extra_args, extra_env)
                stamp(watch, 'started again on the same profile')
                gone_at = None
                while time.monotonic() - restarted < 65:
                    if gone_at is None and not target.exists():
                        gone_at = round(time.monotonic() - restarted, 1); stamp(watch, 'the file is not at its path')
                    time.sleep(.2)
                whole_60 = target.exists() and target.stat().st_size == size
                settled = dn.wait(lambda: not ours(), 60) if left_over else True   # an adopted leftover's watch: 60 s from the engine's start
                text = b2.log()[mark2:]; b2.close()
                size_ok = target.exists() and target.stat().st_size == size
                check(f'{name}: {"killed during the keep" if then == "kill" else "restarted after the keep"}: the file whole at its path 60 s after the next start, nothing of ours left in the end',
                      r.startswith('invoked') and whole_60 and size_ok and not hidden(target) and not ours(),
                      invoke=r, ours_after_stop=left_over, file_whole_60s_after_start=whole_60, ever_missing_after_start_s=gone_at, file_whole_at_the_end=size_ok,
                      ours_left=ours(), folder=sorted(x.name for x in downloads.iterdir()), next_start=[l for l in text.splitlines() if l.startswith('DOWNLOAD')], folder_events=events())
                return
            if then == 'close':
                left = b.close(); closed = True; stamp(watch, 'browser and engine exited')
                f, row = row_file(b, target.name); size_ok = bool(f) and f.exists() and f.stat().st_size == size
                common = 'DOWNLOAD kept file given back at its path' in b.log()[mark:] and '(its pass on the file is over)' in b.log()[mark:]
                check(f'{name}: window closed during the keep: the row points at the whole file, visible, nothing left of ours',
                      r.startswith('invoked') and row.startswith('C\t') and size_ok and not hidden(f) and not ours() and (not common or f == target),
                      invoke=r, row_file=str(f.name) if f else None, file_whole=size_ok, hidden=bool(f) and hidden(f), ours_left=ours(),
                      folder=sorted(p.name for p in downloads.iterdir()), log=lines(b, mark), folder_events=events())
                return
            if then == 'late-removal':   # a removal by the engine once more, right after the file was given back
                if dn.wait(lambda: 'DOWNLOAD kept file given back at its path' in b.log()[mark:], 40, .01):
                    try:
                        os.remove(target); stamp(watch, 'the test removed the file given back (a removal once more)')
                    except OSError as e:
                        stamp(watch, f'the test could not remove the file: {e}')
            kept = dn.wait(lambda: 'DOWNLOAD kept by the user' in b.log()[mark:], 20)
            row_now = dn.wait(lambda: row_of(b, target.name) if row_of(b, target.name).startswith('C\t') else '', 10) or row_of(b, target.name)   # read again: the history file is rewritten atomically
            settled = dn.wait(lambda: any(s in b.log()[mark:] for s in ('DOWNLOAD kept file handed to attachment services', 'attachment services refused',
                                                                       "marked and checked by the engine's own pass")), 150) if not reader_ms else False
            if reader_ms:   # the engine's removal failed: never seen, the file stays at its path and our name holds the bytes until exit
                dn.wait(lambda: 'DOWNLOAD keep: the engine is let go' in b.log()[mark:], 40)   # no zone mark while the reader holds it: the cap
                time.sleep(8)
            row = row_of(b, target.name); size_ok = target.exists() and target.stat().st_size == size
            leftovers = sorted(p.name for p in downloads.iterdir() if p.name != target.name)
            ok = r.startswith('invoked') and bool(kept) and row_now.startswith('C\t') and size_ok and row.startswith('C\t') and 'ZoneId=3' in zone(target) and not hidden(target) and not leftovers
            exercised = not expect_log or expect_log in b.log()[mark:]   # a timing-dependent case may not happen in this run: recorded, not failed
            if then == 'discard':   # « Supprimer » is no longer offered once « Conserver » was chosen (or, still found an instant, does nothing)
                ok = ok and (r.endswith(';then missing') or 'DOWNLOAD held file discarded' not in b.log()[mark:])
            # No loss is the gate. Settled (our name gone after the guard) is the usual end; « never seen » (the engine's removal
            # did not come: our name holds the bytes until exit, which gives the kept file a name the engine never knew) is safe too.
            outcome = 'settled' if settled else ('removal never seen: our name holds the bytes' if "our name holds the bytes until it goes" in b.log()[mark:] and ours() else
                       'waiting for the engine\'s pass: our name holds the bytes' if 'DOWNLOAD keep: the engine is let go' not in b.log()[mark:] and ours() else 'neither')
            if must_settle:
                ok = ok and bool(settled) and not ours()
            else:
                ok = ok and outcome != 'neither' and (bool(settled) == (not ours()))
            check(f'{name}: keep_finishes_the_download', ok, invoke=r, kept=bool(kept), row_at_once=row_now[:2], outcome=outcome, exercised=exercised, file_whole=size_ok, history_row=row[:140],
                  zone_mark='ZoneId=3' in zone(target), hidden=hidden(target), leftovers=leftovers, ours_left=ours(), log=lines(b, mark), folder_events=events())
        finally:
            if not closed:
                left = b.close()
            watch.close()
        if then in ('close', 'kill', 'restart'):
            return
        f, row = row_file(b, target.name); size_ok = bool(f) and f.exists() and f.stat().st_size == size
        check(f'{name}: kept_file_survives_exit', size_ok and row.startswith('C\t') and not ours(), row_file=str(f.name) if f else None, file_whole=size_ok,
              engine_processes_left_after_25s=left, ours_left=ours(), folder=sorted(p.name for p in downloads.iterdir()))

    try:
        def card_case(name, file_key, expect_card, expect_complete, wait_s, extra_args='', extra_env=None, keep=False, probe_checking=False, cancel=False):
            """Whether, and how long after the download's start, the « À confirmer » card comes (log « DOWNLOAD held by the
            engine »), and whether the engine completes the download by itself. keep: « Conserver » on the card, then the
            file must be a finished download, whole, surviving the exit."""
            b, op, downloads = launch(name, base, extra_args, extra_env); fname = FILES[file_key][1]; target = downloads / fname; size = len(FILES[file_key][0])
            try:
                mark = len(b.log()); t0 = time.monotonic(); dn.ev(op, f"location.href='{base}/{file_key}'")
                card_at = done_at = None; end = t0 + wait_s; checking = None; cancelled = None
                while time.monotonic() < end and not (done_at and (card_at or not expect_card)):
                    text = b.log()[mark:]
                    if (probe_checking or cancel) and checking is None and 'DOWNLOAD checking' in text and 'DOWNLOAD state=2' not in text:
                        # the checking row: « Vérification… », Cancel kept, no Pause (UIA, while the engine still checks)
                        checking = {'pause': b.uia(uia, 'DownloadPause', True), 'cancel': b.uia(uia, 'DownloadCancel', True),
                                    'still_checking': 'DOWNLOAD state=2' not in b.log()[mark:] and 'DOWNLOAD held' not in b.log()[mark:]}
                        if cancel:
                            cancelled = b.uia(uia, 'DownloadCancel'); break
                    if card_at is None and 'DOWNLOAD held by the engine' in text:
                        card_at = round(time.monotonic() - t0, 1)
                    if done_at is None and f'DOWNLOAD state=2 {fname}' in text:
                        done_at = round(time.monotonic() - t0, 1)
                    if card_at is not None and expect_card and not expect_complete:
                        break
                    time.sleep(.1)
                kept = None
                if keep and card_at is not None:
                    r = b.uia(uia, 'DownloadKeep'); kept = dn.wait(lambda: row_of(b, fname).startswith('C\t'), 30)
                    kept = bool(kept) and r.startswith('invoked')
                if cancel:   # « Annuler » during the engine's check: the row stays cancelled, the engine's file goes (late, by name), nothing left
                    gone = dn.wait(lambda: not any(downloads.iterdir()), 60); time.sleep(5)
                    row = row_of(b, fname); text = b.log()[mark:]
                    ok = bool(checking) and checking['still_checking'] and checking['pause'] == 'missing' and checking['cancel'] == 'found' and str(cancelled).startswith('invoked') \
                         and bool(gone) and not any(downloads.iterdir()) and row.startswith('X\t') and 'DOWNLOAD held' not in text and f'DOWNLOAD state=2 {fname}' not in text
                    check(f'{name}: « Annuler » while checking: cancelled, the file gone, nothing left, the row does not come back', ok,
                          checking_row=checking, cancel=cancelled, folder=sorted(x.name for x in downloads.iterdir()), history_row=row[:2], log=lines(b, mark))
                    return
                ok = (card_at is not None) == expect_card and (done_at is not None) == expect_complete and (kept is not False)
                if probe_checking:
                    ok = ok and bool(checking) and (not checking['still_checking'] or (checking['pause'] == 'missing' and checking['cancel'] == 'found'))
                check(f'{name}: card {"shown" if expect_card else "not shown"}{", completes by itself" if expect_complete else ""}', ok,
                      card_after_s=card_at, completed_after_s=done_at, kept=kept, checking_row=checking, log=lines(b, mark))
            finally:
                b.close()
            if keep:
                check(f'{name}: kept file survives exit', target.exists() and target.stat().st_size == size, folder=sorted(x.name for x in downloads.iterdir()))
        if not only or 'card-slow-check' in only:
            # The reputation service never answers: the engine completes the .exe by itself ~16 s after the last byte
            # (measured). No card: a slow check is not a risk verdict.
            card_case('card-slow-check', 'card-slow-check.exe', False, True, 60, extra_args=slow_reputation, probe_checking=True)
        if not only or 'card-cancel-checking' in only:
            # « Annuler » on the checking row while the engine's check runs (reputation never answering): measured, the engine
            # removes a file cancelled during its pass late, by name; the row must stay cancelled and no file be left behind.
            card_case('card-cancel-checking', 'card-cancel-checking.exe', False, False, 60, extra_args=slow_reputation, cancel=True)
        if not only or 'card-plain-big' in only:
            card_case('card-plain-big', 'card-plain-big.bin', False, True, 60)
        if not only or 'card-real-hold' in only:
            card_case('card-real-hold', 'card-real-hold.exe', True, False, 40)
        if not only or 'card-never-marked' in only:
            # Where no zone mark can come (a volume without streams, zone information off by policy; test hook
            # BROWSER_TEST_HELD_UNMARKABLE=1): the card after the fallback, and « Conserver » still keeps the file.
            card_case('card-never-marked', 'card-never-marked.exe', True, False, 60, extra_env={'BROWSER_TEST_HELD_UNMARKABLE': '1'}, keep=True)
        if not only or 'keep' in only:
            for name in keeps:
                keep_case(name)
        if not only or 'keep-again' in only:
            keep_case('keep-again', times=2)
        if not only or 'keep-late-removal' in only:
            keep_case('keep-late-removal', then='late-removal')
        if not only or 'keep-read' in only:
            keep_case('keep-read', reader_ms=4000)
        if not only or 'keep-then-discard' in only:
            keep_case('keep-then-discard', reader_ms=1500, then='discard')
        if not only or 'keep-early-cancel' in only:
            # (A) the cancel forced at once (test hook: no wait for the zone mark) on a 150 MB file, so that the engine's own
            # pass usually still runs: its removal comes late, by itself; our name must hold the bytes until it is seen.
            # Whether the pass still ran at the cancel is recorded (« exercised »): the engine's timing decides it.
            keep_case('keep-early-cancel', extra_env={'BROWSER_TEST_KEEP_CAP_MS': '0', 'BROWSER_TEST_KEEP_GUARD_MS': '15000'},
                      expect_log='its pass still runs after the cap')
        if not only or 'keep-save-removes' in only:
            # (B) our attachment services pass removes the kept file and fails with E_INVALIDARG (test hook, what was
            # measured once): it is given back from our name.
            keep_case('keep-save-removes', extra_env={'BROWSER_TEST_KEEP_SAVE': 'remove', 'BROWSER_TEST_KEEP_GUARD_MS': '5000'},
                      expect_log='given back: attachment services removed it without a verdict')
            if not res['checks'].get('keep-save-removes: keep_finishes_the_download', {}).get('exercised'):
                check('keep-save-removes: the attachment services removal was undone', False, error='the hook did not act')
        if not only or 'keep-then-kill' in only:
            for name in kills:
                keep_case(name, then='kill')
        if not only or 'keep-then-restart' in only:
            keep_case('keep-then-restart', then='restart')
        if not only or 'keep-then-close' in only:
            keep_case('keep-then-close', then='close')
        if (not only or 'keep-insecure' in only) and secure:
            # The page on https, the file on plain http from a host that is not local: held as an insecure download.
            # No zone mark ever comes on an insecure hold (measured): the cancel comes at the cap (20 s), then the forced
            # guard (5 min), shortened here by the test hook so the whole flow ends within the case.
            keep_case('keep-insecure', page=secure, file_base=f'http://insecure.test:{port}',
                      extra_args='--ignore-certificate-errors --host-resolver-rules="MAP insecure.test 127.0.0.1"',
                      extra_env={'BROWSER_TEST_KEEP_GUARD_MS': '15000'}, expect_log='its pass still runs after the cap')
        elif not secure:
            check('keep-insecure: https fixture', False, error='no openssl: case not run')
        if not only or 'keep-blocked' in only:
            # The kept copy cannot be made: a folder already has every name the copy could take. The row must not stay
            # a dead « Conserver »: it ends as a failure with « Retirer », the held file is not left behind.
            b, op, downloads = launch('keep-blocked'); target = downloads / 'held-keep-blocked.exe'
            try:
                mark = len(b.log()); dn.ev(op, f"location.href='{base}/keep-blocked.exe'")
                held = dn.wait(lambda: 'DOWNLOAD held by the engine' in b.log()[mark:], 40)
                # The browser's own name for the kept bytes (src/main.cpp KeepBackupPath: <app>/kept-downloads/<pid>-<download
                # id>-<file>, the first download of a fresh profile is id 1) taken by a folder: no hard link, no copy there.
                blockers = [b.app / 'kept-downloads' / f'{b.pid}-1-held-keep-blocked.exe']
                for p in blockers:
                    p.mkdir(parents=True)
                r = b.uia(uia, 'DownloadKeep') if held else 'not held'
                settled = dn.wait(lambda: row_of(b, target.name).startswith('F\t'), 15)
                time.sleep(1.5)
                card_after = b.uia(uia, 'DownloadKeep', True)
                engine_file = target.exists()
                check('keep-blocked: a keep that cannot succeed ends clearly', bool(held) and r.startswith('invoked') and bool(settled) and card_after == 'missing',
                      held=bool(held), invoke=r, history_row=row_of(b, target.name)[:140], keep_button_after=card_after,
                      held_file_still_there=engine_file, folder=sorted(p.name for p in downloads.iterdir()), log=lines(b, mark))
            finally:
                left = b.close()
            ours = sorted(p.name for p in (b.app / 'kept-downloads').iterdir())
            check('keep-blocked: nothing left once the browser exits', not target.exists() and all(p.is_dir() for p in blockers)
                  and not list(downloads.iterdir()) and ours == [blockers[0].name],
                  folder=sorted(p.name for p in downloads.iterdir()), kept_downloads=ours, engine_processes_left_after_25s=left)
        if not only or 'discard' in only:
            b, op, downloads = launch('discard'); target = downloads / 'held-discard.exe'
            try:
                mark = len(b.log()); dn.ev(op, f"location.href='{base}/discard.exe'")
                held = dn.wait(lambda: 'DOWNLOAD held by the engine' in b.log()[mark:], 40)
                r = b.uia(uia, 'DownloadDiscard') if held else 'not held'
                gone = dn.wait(lambda: not target.exists(), 10)
                time.sleep(1)
                row_left = 'held-discard.exe' in b.model()
                check('discard: deletes_the_file', bool(held) and r.startswith('invoked') and bool(gone) and not row_left,
                      held=bool(held), invoke=r, file_gone=bool(gone), row_in_history=row_left, log=lines(b, mark))
            finally:
                b.close()
        if not only or 'control' in only:
            b, op, downloads = launch('control'); target = downloads / 'plain-control.bin'
            try:
                mark = len(b.log()); dn.ev(op, f"location.href='{base}/plain.bin'")
                done = dn.wait(lambda: 'DOWNLOAD state=2 plain-control.bin' in b.log()[mark:], 20)
                time.sleep(3)
                held = 'DOWNLOAD held' in b.log()[mark:]
                check('control: plain_file_completes_unheld', bool(done) and not held and target.exists(), completed=bool(done), held=held, log=lines(b, mark))
            finally:
                b.close()
    except Exception as e:   # noqa: BLE001 - reported as a failure
        check('harness', False, error=repr(e))
    finally:
        srv.shutdown()
    (out / 'result.json').write_text(json.dumps(res, ensure_ascii=False, indent=1), encoding='utf-8')
    print('FAILED: ' + ', '.join(res['failures']) if res['failures'] else 'held download checks passed', flush=True)
    return 1 if res['failures'] else 0


if __name__ == '__main__':
    sys.exit(main())
