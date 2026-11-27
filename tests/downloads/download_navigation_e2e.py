"""A navigation that turns into a download never shows our network error page (real engine, local server only).

    python tests/downloads/download_navigation_e2e.py --app <built app dir> --out <dir> [--observe]

The engine cancels a top-level navigation whose response becomes a download (Content-Disposition: attachment,
a type it cannot render, a redirect to such a file): NavigationCompleted reports a failure (CONNECTION_ABORTED or
OPERATION_CANCELED) and DownloadStarting fires, in either order. Expected (as in Chrome / Edge):
  - no error page (log marker « ERROR PAGE kind= »), the tab keeps its page and the address keeps its URL;
  - the download is in the downloads model (« DOWNLOAD started <name> », the `downloads` file, the file on disk);
  - a tab opened only for that download (target=_blank) closes once the download starts;
  - control: a connection that really breaks (reset, no response) still shows the error page.

Isolation: a throwaway copy of the app (payload only) with its own profile, session, settings and log beside the exe;
downloads go to <out>/downloads only; a local HTTP fixture on 127.0.0.1; CDP over loopback for this copy only; the
address text is read with WM_GETTEXT from OUR window. Every process started here is in a job object with
KILL_ON_JOB_CLOSE, and the copy's processes are also stopped by exact path at the end. No real site, no real profile,
no SendInput, no foreground change.
"""
from __future__ import annotations
import argparse, ast, base64, ctypes, ctypes.wintypes as W, http.server, json, os, re, shutil, socket, struct
import subprocess, sys, threading, time, urllib.request
from pathlib import Path

# The agent guard (tests/agent-guard/agent_guard.py) for every browser.exe these download tests start: no agent at all.
# Why a sealed environment is not enough: without BROWSER_ANNOTATIONS_TEST the browser's agent runtime ignores its own
# process environment on purpose (src/agent-runtime.cpp Environment::FromProcess -> freshUserEnvironment; src/acp-client.cpp
# BuildEnvironmentBlock -> CreateEnvironmentBlock on the process token): it rebuilds the user's REAL environment (USERPROFILE,
# APPDATA, PATH from the registry), finds this computer's Claude Code / Codex and probes their version (« AGENTS claude:
# ready <version> », 2026-11-26). Only the browser's test switch closes that way. So guarded_env() (secure_env, agent=False)
# turns it on and refuses BROWSER_CHAT_REAL_AGENT / a fake agent before any process exists; launch_guard() refuses to start
# a browser.exe whose environment block lacks it; AgentQuietWatch stops that browser (by its process id) at the first agent
# process, agent launch, or « AGENTS <cli>: ready / installing / signing in / prepare / download » line.
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'agent-guard'))
import agent_guard as ag   # noqa: E402  (one module for every harness: one AgentGuardError)
AGENT_ACTIVE = ag.AGENTS_ACTIVE   # the browser's « AGENTS <cli>: ready / installing / signing in / prepare / download » lines


def guarded_env(env: dict, app: Path) -> dict:
    """The environment of a test browser with no agent, checked before the launch (raises AgentGuardError)."""
    return launch_guard(ag.secure_env(env, work=app, exe=app / 'browser.exe', agent=False))


def launch_guard(env) -> dict:
    """The last check before a browser.exe starts: its environment carries the test switch and names no agent."""
    if not isinstance(env, dict) or env.get('BROWSER_ANNOTATIONS_TEST') != '1' or 'BROWSER_CHAT_REAL_AGENT' in env or 'BROWSER_CHAT_FAKE_AGENT' in env:
        raise ag.AgentGuardError('a browser.exe launch without the agent guard (BROWSER_ANNOTATIONS_TEST=1, no agent named): refused before the launch')
    return env


class AgentQuietWatch(ag.Watch):
    """agent_guard's Watch for a browser that must use no agent at all, plus the browser's own « AGENTS … » state lines."""

    def __init__(self, pid: int, *, log: Path, work: Path):
        super().__init__(pid, fake=None, log=log, work=work, expect_agent=False)
        self._agents_at = self._log_at

    def tick(self) -> bool:
        try:
            if os.path.getsize(self.log) < self._agents_at:   # the log rotated: from its start
                self._agents_at = 0
            with open(self.log, 'rb') as f:
                f.seek(self._agents_at); data = f.read()
        except OSError:
            data = b''
        cut = data.rfind(b'\n') + 1
        if cut:
            self._agents_at += cut
            for line in data[:cut].decode('utf-8', 'replace').splitlines():
                if AGENT_ACTIVE.search(line):
                    self._trip('the browser found or prepared an agent of this computer: ' + line.strip()[:200], {'log': str(self.log)})
                    return False
        return super().tick()

ROOT = Path(__file__).resolve().parents[2]
tree = ast.parse((ROOT / 'tests/annotations/sandbox_qa.py').read_text(encoding='utf-8'))   # its stdlib CDP transport only
exec(compile(ast.Module(body=[n for n in tree.body if isinstance(n, ast.ClassDef) and n.name == 'WS'], type_ignores=[]),
             '<cdp-transport>', 'exec'), globals())

PAGE = """<!doctype html><title>OPENER</title><body><p>Local download fixture</p>
<a id="blank" href="/redir?name=blank.bin&amp;slow=1" target="_blank">download in a new tab</a>
<form id="post" method="post" action="/post-download"><input name="x" value="1"></form>
<script>window.sentinel='kept';</script>"""
FILE_BODY = b'download fixture bytes\n' * 64
FLAKY = {'first': 0.0}   # /flaky: breaks for 3 s after its first request (the engine's own silent retry included), then serves the file

k32 = ctypes.WinDLL('kernel32', use_last_error=True)
u32 = ctypes.windll.user32
ntdll = ctypes.WinDLL('ntdll')
k32.CreateJobObjectW.restype = W.HANDLE
k32.CreateJobObjectW.argtypes = [ctypes.c_void_p, W.LPCWSTR]
k32.SetInformationJobObject.argtypes = [W.HANDLE, ctypes.c_int, ctypes.c_void_p, W.DWORD]
k32.AssignProcessToJobObject.argtypes = [W.HANDLE, W.HANDLE]
k32.CloseHandle.argtypes = [W.HANDLE]
ntdll.NtResumeProcess.argtypes = [W.HANDLE]
u32.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
u32.SendMessageTimeoutW.argtypes = [W.HWND, W.UINT, W.WPARAM, ctypes.c_void_p, W.UINT, W.UINT, ctypes.POINTER(ctypes.c_size_t)]
WM_CLOSE, WM_GETTEXT = 0x0010, 0x000D


class IoCounters(ctypes.Structure):
    _fields_ = [(n, ctypes.c_ulonglong) for n in ('r', 'w', 'o', 'rb', 'wb', 'ob')]


class BasicLimit(ctypes.Structure):
    _fields_ = [('PerProcessUserTimeLimit', ctypes.c_longlong), ('PerJobUserTimeLimit', ctypes.c_longlong),
                ('LimitFlags', W.DWORD), ('MinimumWorkingSetSize', ctypes.c_size_t), ('MaximumWorkingSetSize', ctypes.c_size_t),
                ('ActiveProcessLimit', W.DWORD), ('Affinity', ctypes.c_size_t), ('PriorityClass', W.DWORD), ('SchedulingClass', W.DWORD)]


class ExtendedLimit(ctypes.Structure):
    _fields_ = [('BasicLimitInformation', BasicLimit), ('IoInfo', IoCounters), ('ProcessMemoryLimit', ctypes.c_size_t),
                ('JobMemoryLimit', ctypes.c_size_t), ('PeakProcessMemoryUsed', ctypes.c_size_t), ('PeakJobMemoryUsed', ctypes.c_size_t)]


def kill_on_close_job():
    job = k32.CreateJobObjectW(None, None)
    if not job:
        raise OSError('CreateJobObject failed')
    info = ExtendedLimit(); info.BasicLimitInformation.LimitFlags = 0x2000   # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
    if not k32.SetInformationJobObject(job, 9, ctypes.byref(info), ctypes.sizeof(info)):
        raise OSError('SetInformationJobObject failed')
    return job


class Fixture(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def _file(self, name, ctype='application/octet-stream', disposition=True, slow=False):
        self.send_response(200); self.send_header('Content-Type', ctype)
        if disposition:
            self.send_header('Content-Disposition', f'attachment; filename="{name}"')
        self.send_header('Content-Length', str(len(FILE_BODY))); self.send_header('Cache-Control', 'no-store'); self.end_headers()
        if not slow:
            self.wfile.write(FILE_BODY); return
        for i in range(0, len(FILE_BODY), 256):   # ~3 s: the download is still running when its tab closes
            self.wfile.write(FILE_BODY[i:i + 256]); self.wfile.flush(); time.sleep(.5)

    def do_GET(self):
        path, _, query = self.path.partition('?')
        name = dict(p.split('=', 1) for p in query.split('&') if '=' in p).get('name', 'file.bin')
        if path.startswith('/page'):
            body = PAGE.encode()
            self.send_response(200); self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.send_header('Content-Length', str(len(body))); self.send_header('Cache-Control', 'no-store'); self.end_headers(); self.wfile.write(body); return
        if path == '/redir':   # a short link: 302 to the real file
            self.send_response(302); self.send_header('Location', f'/files/download?{query}'); self.send_header('Content-Length', '0'); self.end_headers(); return
        if path == '/files/download':
            return self._file(name, slow='slow=1' in query)
        if path == '/attach-html':   # an attachment served with an HTML type: still a download
            return self._file(name, 'text/html; charset=utf-8')
        if path.startswith('/octet/'):   # no Content-Disposition, a type the engine does not render (named from the URL)
            return self._file(name, 'application/x-fixture-binary', disposition=False)
        if path == '/flaky':
            FLAKY['first'] = FLAKY['first'] or time.monotonic()
            if time.monotonic() - FLAKY['first'] < 3:
                self.connection.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack('ii', 1, 0)); self.connection.close(); self.close_connection = True; return
            return self._file(name)
        if path == '/rst':   # a connection that really breaks: reset without any response
            self.connection.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack('ii', 1, 0)); self.connection.close(); self.close_connection = True; return
        if path == '/empty':   # closed without any response
            self.connection.shutdown(socket.SHUT_RDWR); self.close_connection = True; return
        self.send_response(404); self.send_header('Content-Length', '0'); self.end_headers()

    def do_POST(self):   # a form posted to an API that answers with the file (the reported site's pattern)
        n = int(self.headers.get('Content-Length') or 0)
        if n:
            self.rfile.read(n)
        if self.path.startswith('/post-download'):
            self.send_response(302); self.send_header('Location', '/files/download?name=posted.bin'); self.send_header('Content-Length', '0'); self.end_headers(); return
        self.send_response(405); self.send_header('Content-Length', '0'); self.end_headers()

    def log_message(self, *a):
        pass


def free_port():
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0)); return s.getsockname()[1]


def wait(fn, timeout=8.0, step=.15):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        v = fn()
        if v:
            return v
        time.sleep(step)
    return fn()


class Browser:
    def __init__(self, app: Path, base: str, downloads: Path, ask: bool, job):
        self.app, self.port = app, free_port()
        self.exe = app / 'browser.exe'
        self.http = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        (app / 'session').write_text(f'browser-session-2\n0\n1\n"{base}/page"\n0\n', encoding='utf-8', newline='\n')
        (app / 'settings').write_text('browser-settings 1\nrestore-tabs=1\nconfirm-close-tabs=0\n'
                                      f'download-folder={downloads}\nask-where-to-save={1 if ask else 0}\n', encoding='utf-8', newline='\n')
        env = guarded_env(dict(os.environ, WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS=f'--remote-debugging-port={self.port} --remote-debugging-address=127.0.0.1'), app)
        self.proc = subprocess.Popen([str(self.exe)], cwd=app, env=launch_guard(env), creationflags=0x4)   # CREATE_SUSPENDED: in the job before any child
        handle = W.HANDLE(int(self.proc._handle))
        if not k32.AssignProcessToJobObject(job, handle):
            self.proc.kill(); raise OSError('AssignProcessToJobObject failed')
        self.guard = AgentQuietWatch(self.proc.pid, log=app / 'prototype.log', work=app).start()   # before it runs
        ntdll.NtResumeProcess(handle)
        self.sockets = []

    def targets(self):
        try:
            return [t for t in json.loads(self.http.open(f'http://127.0.0.1:{self.port}/json', timeout=3).read()) if t.get('type') == 'page']
        except OSError:
            return []

    def connect(self, pred, timeout=10.0):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            t = next((t for t in self.targets() if pred(t)), None)
            if t:
                ws = WS(t['webSocketDebuggerUrl']); self.sockets.append(ws); return ws
            time.sleep(.2)
        return None

    def window(self):
        own = []

        @ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
        def find(hwnd, _):
            pid = W.DWORD(); u32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            cls = ctypes.create_unicode_buffer(64); u32.GetClassNameW(hwnd, cls, 64)
            if pid.value == self.proc.pid and cls.value == 'LisiereBrowser':
                own.append(hwnd)
            return True
        u32.EnumWindows(find, 0)
        return own[0] if own else None

    def dialogs(self):   # the save dialog (#32770) of THIS process
        own = []

        @ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
        def find(hwnd, _):
            pid = W.DWORD(); u32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            cls = ctypes.create_unicode_buffer(64); u32.GetClassNameW(hwnd, cls, 64)
            if pid.value == self.proc.pid and cls.value == '#32770' and u32.IsWindowVisible(hwnd):
                own.append(hwnd)
            return True
        u32.EnumWindows(find, 0)
        return own

    def address(self):   # the address field (EDIT child of our window), read with WM_GETTEXT
        h = self.window()
        if not h:
            return None
        edit = u32.FindWindowExW(h, None, 'Edit', None)
        if not edit:
            return None
        buf = ctypes.create_unicode_buffer(4096); res = ctypes.c_size_t()
        u32.SendMessageTimeoutW(edit, WM_GETTEXT, 4096, ctypes.cast(buf, ctypes.c_void_p), 2, 3000, ctypes.byref(res))
        return buf.value

    def log(self):
        p = self.app / 'prototype.log'
        return p.read_bytes().decode('utf-8', errors='replace') if p.exists() else ''

    def model(self):
        p = self.app / 'downloads'
        return p.read_bytes().decode('utf-8', errors='replace') if p.exists() else ''

    def close(self):
        for ws in self.sockets:
            try:
                ws.s.close()
            except OSError:
                pass
        h = self.window()
        if h:
            u32.PostMessageW(h, WM_CLOSE, 0, 0)
        try:
            self.proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            self.proc.kill()


def ev(ws, code):
    r = ws.call('Runtime.evaluate', {'expression': code, 'returnByValue': True, 'awaitPromise': True, 'userGesture': True})
    return r.get('result', {}).get('value', r)


def stop_by_path(app: Path):
    """Every browser.exe of the copy (exact image path) and the engine processes of its own profile folder."""
    exe = str(app / 'browser.exe').lower(); profile = str(app / 'profile').lower()
    ps = ("Get-CimInstance Win32_Process | Where-Object { ($_.ExecutablePath -and $_.ExecutablePath.ToLower() -eq '%s') -or "
          "($_.Name -eq 'msedgewebview2.exe' -and $_.CommandLine -and $_.CommandLine.ToLower().Contains('%s')) } | "
          "ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }") % (exe.replace("'", "''"), profile.replace("'", "''"))
    subprocess.run(['powershell', '-NoProfile', '-NonInteractive', '-Command', ps], capture_output=True, timeout=60)


def main() -> int:
    ap = argparse.ArgumentParser(); ap.add_argument('--app', required=True); ap.add_argument('--out', required=True)
    ap.add_argument('--observe', action='store_true', help='record the facts (BEFORE measurement): failures are reported, exit 0')
    a = ap.parse_args()
    src, out = Path(a.app).resolve(), Path(a.out).resolve()
    shutil.rmtree(out, ignore_errors=True); out.mkdir(parents=True)
    downloads = out / 'downloads'; downloads.mkdir()
    apps = []

    def fresh(name):   # one throwaway copy per launch: payload only, a profile of its own
        app = out / name; app.mkdir()
        for f in ('browser.exe', 'WebView2Loader.dll', 'version.json'):
            if (src / f).exists():
                shutil.copy2(src / f, app / f)
        shutil.copytree(src / 'assets', app / 'assets')
        apps.append(app); return app
    srv = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Fixture); base = f'http://127.0.0.1:{srv.server_port}'
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    job = kill_on_close_job()
    res = {'checks': {}, 'failures': []}

    def check(name, ok, **seen):
        res['checks'][name] = {'ok': bool(ok), **seen}
        if not ok:
            res['failures'].append(name)
        print(('ok   ' if ok else 'FAIL ') + name + ' ' + json.dumps(seen, ensure_ascii=False, default=str)[:600], flush=True)

    def in_tab(b, op, name, trigger, filename, popup=False):
        """Trigger a download from the page; the page, its address and its tab count stay; no error page."""
        reset(b, op)
        before_log, before_targets = len(b.log()), len(b.targets())
        ev(op, trigger)
        started = wait(lambda: f'DOWNLOAD started {filename}' in b.log()[before_log:], 12)
        done = wait(lambda: (downloads / filename).exists() and (downloads / filename).stat().st_size == len(FILE_BODY)
                    and 'DOWNLOAD state=2 ' + filename in b.log()[before_log:], 12)
        time.sleep(2.5)   # past any late error page
        seg = b.log()[before_log:]
        error_page = 'ERROR PAGE kind=' in seg
        href = ev(op, 'location.href'); kept = ev(op, 'window.sentinel'); addr = b.address() or ''
        tabs = len(b.targets())
        nav = [l for l in seg.splitlines() if l.startswith(('NAVIGATION', 'ERROR PAGE', 'DOWNLOAD', 'POPUP', 'TABS'))]
        ok = bool(started) and bool(done) and not error_page and href == base + '/page' and kept == 'kept' \
            and addr.endswith('/page') and filename in b.model() and tabs == before_targets
        check(name, ok, download_started=bool(started), file_complete=bool(done), error_page=error_page, page_href=href,
              page_untouched=kept == 'kept', address=addr, in_model=filename in b.model(), tabs_before=before_targets, tabs_after=tabs,
              popup=popup, log=nav)

    def reset(b, op):   # each case starts on a freshly loaded fixture page (the engine asks « download multiple
        # files? » for a second automatic download from one document: covered by its own case below)
        ev(op, 'window.sentinel=null')
        op.call('Page.navigate', {'url': base + '/page'})
        wait(lambda: ev(op, 'window.sentinel') == 'kept' and (b.address() or '').endswith('/page'), 10)
        time.sleep(.5)

    def control(b, op, name, path):
        reset(b, op)
        before_log = len(b.log())
        ev(op, f"location.href='{path}'")
        shown = wait(lambda: 'ERROR PAGE kind=' in b.log()[before_log:], 10)
        time.sleep(1)
        addr = b.address() or ''
        nav = [l for l in b.log()[before_log:].splitlines() if l.startswith(('NAVIGATION', 'ERROR PAGE', 'DOWNLOAD'))]
        check(name, bool(shown) and addr.endswith(path), error_page=bool(shown), address=addr, log=nav)

    try:
        # ---- 1. automatic save (no dialog)
        app = fresh('app-auto'); b = Browser(app, base, downloads, False, job)
        try:
            op = b.connect(lambda t: t.get('url', '') == base + '/page' and t.get('title') == 'OPENER', 40)
            if not op:
                raise RuntimeError('the fixture page did not load: ' + json.dumps(b.targets()))
            wait(lambda: (b.address() or '').endswith('/page'), 10)
            in_tab(b, op, 'redirect_to_attachment', "location.href='/redir?name=redirected.bin'", 'redirected.bin')
            in_tab(b, op, 'direct_attachment', "location.href='/files/download?name=direct.bin'", 'direct.bin')
            in_tab(b, op, 'attachment_with_html_type', "location.href='/attach-html?name=page-as-file.html'", 'page-as-file.html')
            in_tab(b, op, 'non_renderable_type', "location.href='/octet/octet.bin'", 'octet.bin')
            in_tab(b, op, 'form_post_redirect_to_file', "document.getElementById('post').submit()", 'posted.bin')
            # target=_blank: the new tab exists only for the download, it closes again
            before_log = len(b.log())
            in_tab(b, op, 'target_blank_tab_closes', "document.getElementById('blank').click()", 'blank.bin', popup=True)
            res['checks']['target_blank_tab_closes']['popup_requested'] = 'POPUP request' in b.log()[before_log:]
            if 'POPUP request' not in b.log()[before_log:]:
                check('target_blank_opened_a_tab', False, note='no popup request: the click did not open a new tab')
            # a second and third automatic download from the same document: the engine asks « download multiple
            # files? » (PermissionRequested, no DownloadStarting), then blocks silently while it asks: never an error page
            reset(b, op); before_log = len(b.log())
            ev(op, "location.href='/files/download?name=first.bin'")
            first = wait(lambda: 'DOWNLOAD started first.bin' in b.log()[before_log:], 12)
            time.sleep(1)
            ev(op, "location.href='/files/download?name=second.bin'")
            asked = wait(lambda: 'PERMISSION request kind=7' in b.log()[before_log:], 8)
            time.sleep(2.5)
            ev(op, "location.href='/files/download?name=third.bin'")
            time.sleep(3)
            seg = b.log()[before_log:]
            href = ev(op, 'location.href'); addr = b.address() or ''
            check('second_download_asks_no_error_page', bool(first) and bool(asked) and 'ERROR PAGE kind=' not in seg and href == base + '/page'
                  and ev(op, 'window.sentinel') == 'kept' and addr.endswith('/page'),
                  first=bool(first), asked=bool(asked), error_page='ERROR PAGE kind=' in seg, page_href=href, address=addr,
                  log=[l for l in seg.splitlines() if l.startswith(('NAVIGATION', 'ERROR PAGE', 'DOWNLOAD', 'PERMISSION'))])
            # « Réessayer » on our error page, and this time the address answers with a file: the error page and its
            # address stay as they were (the tab's own document), no second error page, the download is there
            reset(b, op); before_log = len(b.log())
            ev(op, "location.href='/flaky?name=retry.bin'")
            shown = wait(lambda: 'ERROR PAGE kind=' in b.log()[before_log:], 10)
            time.sleep(3.5)
            clicked = ev(op, "(()=>{const x=document.querySelector('button[data-action=retry]');if(!x)return false;x.click();return true})()")
            mid = len(b.log())
            started = wait(lambda: 'DOWNLOAD started retry.bin' in b.log()[mid:], 12)
            time.sleep(2.5)
            seg = b.log()[mid:]; addr = b.address() or ''
            check('retry_from_error_page_becomes_download', bool(shown) and clicked is True and bool(started) and 'ERROR PAGE kind=' not in seg
                  and addr.endswith('/flaky?name=retry.bin') and 'retry.bin' in b.model(),
                  first_error_page=bool(shown), retry_clicked=clicked, download_started=bool(started), second_error_page='ERROR PAGE kind=' in seg,
                  address=addr, in_model='retry.bin' in b.model(), log=[l for l in seg.splitlines() if l.startswith(('NAVIGATION', 'ERROR PAGE', 'DOWNLOAD'))])
            # control: real breaks still show our error page
            control(b, op, 'control_reset_shows_error_page', '/rst')
            control(b, op, 'control_empty_reply_shows_error_page', '/empty')
        finally:
            b.close(); stop_by_path(app)
        # ---- 2. « Demander où enregistrer »: the user cancels the save dialog (download cancelled by the user)
        app = fresh('app-ask'); b = Browser(app, base, downloads, True, job)
        try:
            op = b.connect(lambda t: t.get('url', '') == base + '/page' and t.get('title') == 'OPENER', 40)
            if not op:
                raise RuntimeError('the fixture page did not load (second launch)')
            wait(lambda: (b.address() or '').endswith('/page'), 10)
            before_log = len(b.log())
            ev(op, "location.href='/redir?name=cancelled.bin'")
            dlg = wait(lambda: b.dialogs(), 12)
            for h in dlg or []:
                u32.PostMessageW(h, WM_CLOSE, 0, 0)
            cancelled = wait(lambda: 'DOWNLOAD cancelled from the save dialog' in b.log()[before_log:], 10)
            time.sleep(2.5)
            seg = b.log()[before_log:]
            href = ev(op, 'location.href'); addr = b.address() or ''
            check('user_cancelled_download', bool(dlg) and bool(cancelled) and 'ERROR PAGE kind=' not in seg and href == base + '/page'
                  and addr.endswith('/page') and not (downloads / 'cancelled.bin').exists(),
                  dialog=bool(dlg), cancelled=bool(cancelled), error_page='ERROR PAGE kind=' in seg, page_href=href, address=addr,
                  log=[l for l in seg.splitlines() if l.startswith(('NAVIGATION', 'ERROR PAGE', 'DOWNLOAD'))])
        finally:
            b.close(); stop_by_path(app)
    finally:
        k32.CloseHandle(job)   # KILL_ON_JOB_CLOSE: anything left of the copies dies here
        for app in apps:
            stop_by_path(app)
        srv.shutdown()
    (out / 'result.json').write_text(json.dumps(res, indent=1, ensure_ascii=False), encoding='utf-8')
    n, bad = len(res['checks']), len(res['failures'])
    print(f'{"OBSERVED" if a.observe else ("FAIL" if bad else "PASS")} download navigation e2e: {n - bad}/{n} checks'
          + (f' (failed: {", ".join(res["failures"])})' if bad else ''), flush=True)
    return 0 if a.observe or not bad else 1


if __name__ == '__main__':
    sys.exit(main())
