#!/usr/bin/env python3
"""Restored tabs load the first time and the window never freezes at a first launch: the REAL browser, started on a
Windows desktop of its own (never displayed).

What was reported (24 Nov 2026, annotation lot 2): a new profile that restores a session lost its first page
(« NAVIGATION FAILED engine WebErrorStatus=9 »), 4 launches out of 4. What this test measured: the browser never aborts
that page by itself. At a first launch it copied the bundled ad blocker (uBlock Origin Lite, ~1000 files / 47 MB, each
one scanned by the antivirus) on its UI thread: the window froze 9-11 s, and the restored page, whose load needs that
thread, appeared only after ~15 s. Harnesses that ask the page again after 10 s (tests/chat/lot2_desktop.py,
tests/chat/chat_qa.py) did it in the middle of that freeze: their own navigation replaced the browser's, and that
replaced navigation is the WebErrorStatus=9 they saw (reproduced 2 out of 2 by a probe doing the same; 0 failures out
of 9 without it). Installing or removing the extension while a page loads does not abort it either (« update »).
The fix: the folder is copied off the UI thread (src/main.cpp, AdBlockSourceStart / AdBlockWaitSource).

Each launch here: a copy of a build under build/restore-first-load/, a session file naming one page of a local server
of this script (its document after --doc-ms, its picture after --img-ms: a real site's load), the browser started on
a desktop of its own (CreateDesktop: its windows are real, nothing reaches the user's screen, mouse, keyboard or
foreground), every process in a job object closed with the script (KILL_ON_JOB_CLOSE), the « user's » folders
(USERPROFILE, APPDATA, LOCALAPPDATA) redirected under build/. The page is NEVER asked again by the test. A launch passes
when:
  - the browser loads the page by itself: its log says « NAVIGATION OK <url> », and has no « NAVIGATION FAILED »;
  - the ad blocker's sync of that launch is over (installed / on), so the risky moment is behind;
  - DevTools shows the tab's document is the served page (its title, complete), not an error page or a blank one;
  - the window, once shown, always answered within MAX_FREEZE_MS (WM_NULL sent every 50 ms from its desktop).

Scenarios (all by default):
  new       a brand-new profile per launch (the reported case);
  home      a brand-new profile without a session: its home tab (NavigateToString);
  existing  one profile set up once (ad blocker installed), then relaunched: nothing to install;
  update    that profile relaunched after « an update » shipping another uBlock version (its manifest version is
            bumped in the copy): a new copy of the folder, the old install removed, the new one installed.

  python tests/restore-first-load/run.py --build build/restore-fix
  python tests/restore-first-load/run.py --build build/restore-base --scenario new --runs 4 --verbose   # before the fix
Exit code 0 = every launch passed. Screen-free: allowed in the daytime (it never shows a window on the screen).
"""
from __future__ import annotations
import argparse
import base64
import ctypes
import ctypes.wintypes as W
import http.server
import json
import os
import pathlib
import queue
import re
import shutil
import socket
import struct
import subprocess
import sys
import threading
import time
import urllib.request

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
WORK = ROOT / 'build' / 'restore-first-load'
TITLE = 'Restored page'
SETTINGS_HEADER = 'browser-settings 1'

u32 = ctypes.WinDLL('user32', use_last_error=True)
k32 = ctypes.WinDLL('kernel32', use_last_error=True)
u32.CreateDesktopW.restype = W.HANDLE
u32.CreateDesktopW.argtypes = [W.LPCWSTR, W.LPCWSTR, ctypes.c_void_p, W.DWORD, W.DWORD, ctypes.c_void_p]
u32.SetThreadDesktop.argtypes = [W.HANDLE]
u32.CloseDesktop.argtypes = [W.HANDLE]
u32.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
u32.SendMessageTimeoutW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM, W.UINT, W.UINT, ctypes.POINTER(ctypes.c_size_t)]
u32.IsWindow.argtypes = [W.HWND]
u32.IsWindowVisible.argtypes = [W.HWND]
k32.CreateJobObjectW.restype = W.HANDLE
k32.SetInformationJobObject.argtypes = [W.HANDLE, ctypes.c_int, ctypes.c_void_p, W.DWORD]
k32.QueryInformationJobObject.argtypes = [W.HANDLE, ctypes.c_int, ctypes.c_void_p, W.DWORD, ctypes.c_void_p]
k32.TerminateJobObject.argtypes = [W.HANDLE, W.UINT]
k32.AssignProcessToJobObject.argtypes = [W.HANDLE, W.HANDLE]
k32.WaitForSingleObject.argtypes = [W.HANDLE, W.DWORD]
k32.CloseHandle.argtypes = [W.HANDLE]
k32.ResumeThread.argtypes = [W.HANDLE]
k32.GetExitCodeProcess.argtypes = [W.HANDLE, ctypes.POINTER(W.DWORD)]
WM_CLOSE = 0x0010


class STARTUPINFO(ctypes.Structure):
    _fields_ = [('cb', W.DWORD), ('lpReserved', W.LPWSTR), ('lpDesktop', W.LPWSTR), ('lpTitle', W.LPWSTR), ('dwX', W.DWORD), ('dwY', W.DWORD),
                ('dwXSize', W.DWORD), ('dwYSize', W.DWORD), ('dwXCountChars', W.DWORD), ('dwYCountChars', W.DWORD), ('dwFillAttribute', W.DWORD),
                ('dwFlags', W.DWORD), ('wShowWindow', W.WORD), ('cbReserved2', W.WORD), ('lpReserved2', ctypes.c_void_p),
                ('hStdInput', W.HANDLE), ('hStdOutput', W.HANDLE), ('hStdError', W.HANDLE)]


class PROCESS_INFORMATION(ctypes.Structure):
    _fields_ = [('hProcess', W.HANDLE), ('hThread', W.HANDLE), ('dwProcessId', W.DWORD), ('dwThreadId', W.DWORD)]


class _BASIC(ctypes.Structure):
    _fields_ = [('a', ctypes.c_int64), ('b', ctypes.c_int64), ('LimitFlags', W.DWORD), ('c', ctypes.c_size_t), ('d', ctypes.c_size_t),
                ('e', W.DWORD), ('f', ctypes.c_size_t), ('g', W.DWORD), ('h', W.DWORD)]


class _EXT(ctypes.Structure):
    _fields_ = [('Basic', _BASIC), ('Io', ctypes.c_uint64 * 6), ('i', ctypes.c_size_t), ('j', ctypes.c_size_t), ('k', ctypes.c_size_t), ('l', ctypes.c_size_t)]


class _ACCOUNTING(ctypes.Structure):
    _fields_ = [('t', ctypes.c_int64 * 4), ('TotalPageFaultCount', W.DWORD), ('TotalProcesses', W.DWORD),
                ('ActiveProcesses', W.DWORD), ('TotalTerminatedProcesses', W.DWORD)]


def new_job():
    job = k32.CreateJobObjectW(None, None)
    info = _EXT(); info.Basic.LimitFlags = 0x2000   # KILL_ON_JOB_CLOSE: nothing of a launch outlives this script
    k32.SetInformationJobObject(job, 9, ctypes.byref(info), ctypes.sizeof(info))
    return job


def job_processes(job):
    info = _ACCOUNTING()
    if not k32.QueryInformationJobObject(job, 1, ctypes.byref(info), ctypes.sizeof(info), None):
        return -1
    return info.ActiveProcesses


def free_port():
    s = socket.socket(); s.bind(('127.0.0.1', 0)); p = s.getsockname()[1]; s.close(); return p


# ---------------------------------------------------------------- DevTools (a minimal WebSocket client)
class WS:
    def __init__(self, url):
        hostport, path = url[5:].split('/', 1)
        host, port = hostport.split(':')
        self.s = socket.create_connection((host, int(port)), timeout=10)
        key = base64.b64encode(os.urandom(16)).decode()
        self.s.sendall((f'GET /{path} HTTP/1.1\r\nHost: {hostport}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n'
                        f'Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n').encode())
        resp = b''
        while b'\r\n\r\n' not in resp:
            resp += self.s.recv(4096)
        assert b' 101 ' in resp.split(b'\r\n')[0], resp[:200]
        self.buf = resp.split(b'\r\n\r\n', 1)[1]; self.n = 0

    def _recv(self, n):
        while len(self.buf) < n:
            chunk = self.s.recv(65536)
            if not chunk:
                raise EOFError
            self.buf += chunk
        out, self.buf = self.buf[:n], self.buf[n:]
        return out

    def send(self, text):
        data = text.encode(); head = bytearray([0x81]); mask = os.urandom(4)
        if len(data) < 126:
            head.append(0x80 | len(data))
        elif len(data) < 65536:
            head.append(0x80 | 126); head += struct.pack('>H', len(data))
        else:
            head.append(0x80 | 127); head += struct.pack('>Q', len(data))
        self.s.sendall(bytes(head) + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(data)))

    def recv(self):
        msg = b''
        while True:
            b0, b1 = self._recv(2); ln = b1 & 0x7F
            if ln == 126:
                ln = struct.unpack('>H', self._recv(2))[0]
            elif ln == 127:
                ln = struct.unpack('>Q', self._recv(8))[0]
            msg += self._recv(ln)
            if b0 & 0x80:
                return msg.decode('utf-8', 'replace')

    def call(self, method, params=None):
        self.n += 1; self.send(json.dumps({'id': self.n, 'method': method, 'params': params or {}}))
        while True:
            m = json.loads(self.recv())
            if m.get('id') == self.n:
                return m

    def close(self):
        try:
            self.s.close()
        except OSError:
            pass


def page_targets(cdp_port):
    try:
        return [t for t in json.loads(urllib.request.urlopen(f'http://127.0.0.1:{cdp_port}/json', timeout=5).read())
                if t.get('type') == 'page' and not t.get('url', '').startswith('https://chat.browser.internal')]
    except Exception:
        return []


def document_of(target):
    ws = WS(target['webSocketDebuggerUrl'])
    try:
        r = ws.call('Runtime.evaluate', {'expression': 'JSON.stringify({title: document.title, href: location.href, '
                                         'state: document.readyState, tiles: document.querySelectorAll("nav li, nav a").length})',
                                         'returnByValue': True})
        return json.loads(r['result']['result']['value'])
    finally:
        ws.close()


# ---------------------------------------------------------------- the page server
class Site:
    """One page, served from this process: records when it is asked. Like a real site, it takes a while to load: its
    document comes after `doc_ms` and its picture after `img_ms` (the tab's navigation completes with the page's load)."""

    def __init__(self, doc_ms=0, img_ms=0):
        self.hits = []; self.t0 = time.time(); self.doc_ms, self.img_ms = doc_ms, img_ms
        site = self

        class H(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                site.hits.append((round((time.time() - site.t0) * 1000), self.path))
                if self.path.startswith('/restored.html'):
                    time.sleep(site.doc_ms / 1000)
                    body = (f'<!doctype html><meta charset="utf-8"><title>{TITLE}</title>'
                            '<body style="background:#fff"><h1>Restored</h1><p>The restored tab loaded.</p>'
                            f'<img src="/picture.svg?{time.time()}" width="40" height="40" alt=""></body>').encode()
                    self.send_response(200); self.send_header('Content-Type', 'text/html; charset=utf-8')
                elif self.path.startswith('/picture.svg'):
                    time.sleep(site.img_ms / 1000)
                    body = b'<svg xmlns="http://www.w3.org/2000/svg" width="40" height="40"><rect width="40" height="40" fill="#3e67ff"/></svg>'
                    self.send_response(200); self.send_header('Content-Type', 'image/svg+xml')
                else:
                    body = b''; self.send_response(404)
                self.send_header('Content-Length', str(len(body))); self.send_header('Cache-Control', 'no-store'); self.end_headers()
                self.wfile.write(body)

            def log_message(self, *a):
                pass
        self.httpd = http.server.ThreadingHTTPServer(('127.0.0.1', 0), H)
        self.port = self.httpd.server_address[1]
        threading.Thread(target=self.httpd.serve_forever, daemon=True).start()

    @property
    def url(self):
        return f'http://127.0.0.1:{self.port}/restored.html'

    def reset(self):
        self.hits = []; self.t0 = time.time()

    def page_hits(self):
        return [h for h in self.hits if h[1].startswith('/restored.html')]


def find_main(pid):
    """The browser's main window, from a thread that lives on its desktop."""
    found = []

    @ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    def cb(h, _):
        q = W.DWORD(); u32.GetWindowThreadProcessId(h, ctypes.byref(q))
        if q.value == pid:
            n = ctypes.create_unicode_buffer(64); u32.GetClassNameW(h, n, 64)
            if n.value == 'LisiereBrowser':
                found.append(h)
        return True
    u32.EnumWindows(cb, 0)
    return found[0] if found else None


# ---------------------------------------------------------------- one test browser on its own desktop
class Browser:
    def __init__(self, build: pathlib.Path, name: str):
        self.src = build
        self.app = WORK / name / 'app'
        self.home = WORK / name / 'home'
        for p in (self.app, self.home):
            assert p.resolve().is_relative_to(ROOT / 'build'), f'unsafe folder {p}'
        self.desk_name = f'restore-first-load-{name}-{os.getpid()}'
        self.hdesk = u32.CreateDesktopW(self.desk_name, None, None, 0, 0x10000000, None)   # GENERIC_ALL
        if not self.hdesk:
            raise ctypes.WinError(ctypes.get_last_error())
        self._q = queue.Queue(); self.pid = 0; self.hproc = None; self.job = None
        threading.Thread(target=self._desk_thread, daemon=True).start()

    def _desk_thread(self):
        if not u32.SetThreadDesktop(self.hdesk):
            print('SetThreadDesktop failed', ctypes.get_last_error(), flush=True)
        while True:
            fn, out = self._q.get()
            try:
                out.put((True, fn()))
            except BaseException as e:
                out.put((False, e))

    def desk(self, fn, timeout=30):
        out = queue.Queue(); self._q.put((fn, out)); ok, v = out.get(timeout=timeout)
        if not ok:
            raise v
        return v

    @property
    def log(self):
        return self.app / 'prototype.log'

    def fresh(self):
        shutil.rmtree(self.app, ignore_errors=True); shutil.rmtree(self.home, ignore_errors=True)
        shutil.copytree(self.src, self.app)
        for sub in ('AppData/Roaming', 'AppData/Local'):
            (self.home / sub).mkdir(parents=True, exist_ok=True)

    def write_session(self, url):
        shutil.rmtree(self.app / 'sessions', ignore_errors=True)
        if url is None:
            try:
                (self.app / 'session').unlink()
            except OSError:
                pass
            return
        (self.app / 'session').write_text('browser-session-2\n0\n1\n' + json.dumps(url) + '\n0\n', encoding='utf-8', newline='\n')

    def write_settings(self, adblock=True):
        """The tutorial marked as seen (#125: its veil never covers a test page); ad-block=0 only for the control."""
        f = self.app / 'settings'
        lines = [l for l in (f.read_text(encoding='utf-8', errors='replace').splitlines() if f.exists() else [])
                 if l and l != SETTINGS_HEADER and not l.startswith(('annotation-onboarded=', 'ad-block='))]
        lines = [SETTINGS_HEADER] + lines + ['annotation-onboarded=1', f'ad-block={1 if adblock else 0}']
        f.write_text('\n'.join(lines) + '\n', encoding='utf-8', newline='\n')

    def bump_ublock(self):
        """« An update shipping another uBlock version »: the copy's bundled manifest gets a new version number."""
        m = self.app / 'assets' / 'extensions' / 'ublock-lite' / 'manifest.json'
        text = m.read_text(encoding='utf-8')
        v = re.search(r'"version"\s*:\s*"([0-9.]+)"', text).group(1)
        parts = v.split('.'); parts[-1] = str(int(parts[-1]) + 1); nv = '.'.join(parts)
        m.write_text(text.replace(f'"{v}"', f'"{nv}"', 1), encoding='utf-8', newline='')
        return v, nv

    def env(self, cdp_port):
        env = {k: v for k, v in os.environ.items() if not k.upper().startswith(('BROWSER_', 'WEBVIEW2_'))}
        env['WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS'] = (f'--remote-debugging-port={cdp_port} --remote-allow-origins=http://127.0.0.1:{cdp_port} '
                                                        '--disable-features=CalculateNativeWinOcclusion --disable-backgrounding-occluded-windows')
        env['BROWSER_NO_ONBOARDING'] = '1'
        env['USERPROFILE'] = str(self.home)
        env['APPDATA'] = str(self.home / 'AppData' / 'Roaming')
        env['LOCALAPPDATA'] = str(self.home / 'AppData' / 'Local')
        windir = os.environ.get('SystemRoot', r'C:\Windows')
        env['PATH'] = f'{windir}\\System32;{windir}'
        assert pathlib.Path(env['LOCALAPPDATA']).resolve().is_relative_to(ROOT / 'build'), 'never the real user folders'
        return env

    def start(self, cdp_port):
        assert not self.pid
        si = STARTUPINFO(); si.cb = ctypes.sizeof(si); si.lpDesktop = 'WinSta0\\' + self.desk_name
        pi = PROCESS_INFORMATION()
        block = ''.join(f'{k}={v}\0' for k, v in sorted(self.env(cdp_port).items(), key=lambda kv: kv[0].upper())) + '\0'
        line = ctypes.create_unicode_buffer(subprocess.list2cmdline([str(self.app / 'browser.exe')]))
        self.job = new_job()
        # CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED: in its job before it runs a single instruction
        if not k32.CreateProcessW(None, line, None, None, False, 0x00000400 | 0x00000004, ctypes.create_unicode_buffer(block),
                                  str(self.app), ctypes.byref(si), ctypes.byref(pi)):
            raise ctypes.WinError(ctypes.get_last_error())
        k32.AssignProcessToJobObject(self.job, pi.hProcess)
        k32.ResumeThread(pi.hThread); k32.CloseHandle(pi.hThread)
        self.pid, self.hproc = pi.dwProcessId, pi.hProcess
        return time.time()

    def alive(self):
        if not self.hproc:
            return False
        code = W.DWORD(); k32.GetExitCodeProcess(self.hproc, ctypes.byref(code)); return code.value == 259

    def main_window(self):
        pid = self.pid
        return self.desk(lambda: find_main(pid))

    def quit(self):
        """Closed as the user does (its window's close), its engine left to exit; then nothing of the launch is left."""
        if not self.pid:
            return True
        clean = False
        try:
            h = self.main_window()
            if h:
                self.desk(lambda: u32.PostMessageW(h, WM_CLOSE, 0, 0))
                clean = k32.WaitForSingleObject(self.hproc, 20000) == 0
                end = time.time() + 15
                while time.time() < end and job_processes(self.job) > 0:   # the engine writes its profile on exit
                    time.sleep(0.2)
        except Exception:
            pass
        k32.TerminateJobObject(self.job, 1); time.sleep(0.3)
        k32.CloseHandle(self.job); k32.CloseHandle(self.hproc)
        self.pid, self.hproc, self.job = 0, None, None
        return clean

    def close(self):
        try:
            self.quit()
        finally:
            for _ in range(20):
                if u32.CloseDesktop(self.hdesk):
                    break
                time.sleep(0.25)


# ---------------------------------------------------------------- one launch and its verdict
class Tail:
    """The browser's log as it is written, each line with the time it appeared (ms since the launch)."""

    def __init__(self, path, t0, start_at=0):
        self.path, self.t0, self.pos, self.lines, self.stop = path, t0, start_at, [], False
        self.th = threading.Thread(target=self._run, daemon=True); self.th.start()

    def _run(self):
        rest = ''
        while not self.stop:
            try:
                with open(self.path, 'rb') as f:
                    f.seek(self.pos); data = f.read(); self.pos += len(data)
            except OSError:
                data = b''
            if data:
                rest += data.decode('utf-8', 'replace')
                *done, rest = rest.split('\n')
                now = round((time.time() - self.t0) * 1000)
                self.lines += [(now, l.rstrip('\r')) for l in done]
            time.sleep(0.02)

    def has(self, prefix):
        return any(l.startswith(prefix) for _, l in self.lines)

    def first(self, prefix):
        return next((t for t, l in self.lines if l.startswith(prefix)), None)

    def close(self):
        self.stop = True; self.th.join(2)


class Responsiveness:
    """How long the browser's window takes to answer a message (WM_NULL sent from a thread on its desktop, every
    50 ms): the stalls of its UI thread, as the user would feel them (a window that does not repaint nor react).
    Only once the window is shown counts (`worst`): the start before it (building the window) is not a freeze."""

    def __init__(self, b, t0):
        self.b, self.t0, self.stalls, self.stop, self.answered = b, t0, [], False, 0
        self.th = threading.Thread(target=self._run, daemon=True); self.th.start()

    def _run(self):
        if not u32.SetThreadDesktop(self.b.hdesk):
            return
        h = None
        while not self.stop:
            if not h:
                h = find_main(self.b.pid)
            if h:
                shown = bool(u32.IsWindowVisible(h))
                res = ctypes.c_size_t(); s = time.time()
                ok = u32.SendMessageTimeoutW(h, 0, 0, 0, 0, 60000, ctypes.byref(res))
                dt = (time.time() - s) * 1000
                if ok:
                    self.answered += 1
                elif not u32.IsWindow(h):
                    h = None
                if dt >= 250:
                    self.stalls.append((round((s - self.t0) * 1000), round(dt), shown))
            time.sleep(0.05)

    def close(self):
        self.stop = True; self.th.join(5)

    def worst(self):
        return max((d for _, d, shown in self.stalls if shown), default=0)


ADBLOCK_SETTLED = ('SETTINGS adblock installed', 'SETTINGS adblock on', 'SETTINGS adblock install failed',
                   'SETTINGS adblock: extension', 'SETTINGS adblock Enable', 'SETTINGS adblock off')


MAX_FREEZE_MS = 3000   # the window, once shown, always answers within this (before the fix: 9-11 s at a first launch)


def launch(b: Browser, site: Site, url, expect_title, timeout=40):
    """Starts the browser on its session and watches, without ever asking the page again. Returns a verdict dict."""
    cdp = free_port(); site.reset()
    start_at = b.log.stat().st_size if b.log.exists() else 0
    t0 = b.start(cdp); tail = Tail(b.log, t0, start_at); ui = Responsiveness(b, t0)
    ok_line = 'NAVIGATION OK ' + url if url else 'NAVIGATION OK'
    end = t0 + timeout
    while time.time() < end and b.alive():
        if tail.has(ok_line) and tail.has(ADBLOCK_SETTLED):
            break
        time.sleep(0.1)
    time.sleep(2.5)   # what may follow: a late abort, a held failure's error page (1.5 s), the blocker's last step
    doc = None
    for t in page_targets(cdp):
        try:
            d = document_of(t)
        except Exception as e:
            d = {'error': str(e)}
        if doc is None or (d.get('title') == expect_title):
            doc = d
    tail.close(); ui.close()
    failed = [l for _, l in tail.lines if l.startswith('NAVIGATION FAILED') or 'error page' in l.lower()]
    verdict = {
        'loaded_by_itself': tail.has(ok_line),
        'failures': failed,
        'adblock': next((l for _, l in tail.lines if l.startswith(ADBLOCK_SETTLED)), None),
        'document': doc,
        'page_requests': len(site.page_hits()),
        'requests': list(site.hits),
        'ui_stalls': list(ui.stalls), 'ui_worst': ui.worst(), 'ui_answered': ui.answered,
        'timeline': list(tail.lines),
    }
    verdict['loaded_at'] = tail.first(ok_line)
    verdict['passed'] = bool(verdict['loaded_by_itself'] and not failed and verdict['adblock'] and verdict['ui_worst'] < MAX_FREEZE_MS
                             and doc and doc.get('title') == expect_title and doc.get('state') == 'complete')
    return verdict


def show(name, i, v, verbose):
    mark = 'PASS' if v['passed'] else 'FAIL'
    d = v['document'] or {}
    print(f'  {mark} {name} #{i}: loaded by itself={v["loaded_by_itself"]} failures={len(v["failures"])} '
          f'page asked {v["page_requests"]}x, document title={d.get("title")!r} state={d.get("state")} | '
          f'loaded at {v["loaded_at"]} ms, window frozen at most {v["ui_worst"]} ms (limit {MAX_FREEZE_MS}) | {v["adblock"]}', flush=True)
    if verbose or not v['passed']:
        events = [(t, 'log', l) for t, l in v['timeline']] + [(t, 'server', 'GET ' + path) for t, path in v['requests']] + \
                 [(t, 'window', f'did not answer for {d} ms' + ('' if shown else ' (not shown yet)')) for t, d, shown in v['ui_stalls']]
        for t, kind, l in sorted(events, key=lambda e: e[0]):
            print(f'      {t:6d} ms  {kind:6s} {l[:150]}', flush=True)


def scenario_new(build, site, runs, adblock, verbose):
    res = []
    b = Browser(build, 'new')
    try:
        for i in range(1, runs + 1):
            b.fresh(); b.write_session(site.url); b.write_settings(adblock)
            v = launch(b, site, site.url, TITLE); b.quit(); show('new profile, restored tab', i, v, verbose); res.append(v['passed'])
    finally:
        b.close()
    return res


def scenario_home(build, site, runs, adblock, verbose):
    res = []
    b = Browser(build, 'home')
    try:
        for i in range(1, runs + 1):
            b.fresh(); b.write_session(None); b.write_settings(adblock)
            v = launch(b, site, None, None)
            d = v['document'] or {}
            # the home tab: our own document (NavigateToString: « Nouvel onglet » / « New tab »), complete
            v['passed'] = bool(v['loaded_by_itself'] and not v['failures'] and v['adblock'] and v['ui_worst'] < MAX_FREEZE_MS and d.get('state') == 'complete'
                               and d.get('title') and not str(d.get('href', '')).startswith('chrome-error'))
            b.quit(); show('new profile, home tab', i, v, verbose); res.append(v['passed'])
    finally:
        b.close()
    return res


def scenario_existing(build, site, runs, update, adblock, verbose):
    name = 'update' if update else 'existing'
    res = []
    b = Browser(build, name)
    try:
        b.fresh(); b.write_session(site.url); b.write_settings(adblock)
        v = launch(b, site, site.url, TITLE); clean = b.quit()
        print(f'  setup {name}: first launch {"passed" if v["passed"] else "failed"} (not counted), ad blocker: {v["adblock"]}, clean exit={clean}', flush=True)
        for i in range(1, runs + 1):
            if update:
                old, new = b.bump_ublock(); print(f'    "update": bundled uBlock {old} -> {new}', flush=True)
            b.write_session(site.url); b.write_settings(adblock)
            v = launch(b, site, site.url, TITLE); b.quit()
            show('existing profile after an update of uBlock' if update else 'existing profile', i, v, verbose); res.append(v['passed'])
    finally:
        b.close()
    return res


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--build', required=True, help='a build folder (tools/release/build.py --out), e.g. build/restore-fix')
    p.add_argument('--scenario', choices=('all', 'new', 'home', 'existing', 'update'), default='all')
    p.add_argument('--runs', type=int, default=3)
    p.add_argument('--adblock', choices=('on', 'off'), default='on', help='off: the control (no install at all)')
    p.add_argument('--doc-ms', type=int, default=300, help="the page's document comes after this delay (a real site's answer)")
    p.add_argument('--img-ms', type=int, default=3000, help="its picture after this one: the page loads while the blocker installs")
    p.add_argument('--verbose', action='store_true')
    a = p.parse_args()
    build = (ROOT / a.build).resolve() if not pathlib.Path(a.build).is_absolute() else pathlib.Path(a.build)
    assert (build / 'browser.exe').is_file(), f'no browser.exe in {build}'
    assert (build / 'assets' / 'extensions' / 'ublock-lite' / 'manifest.json').is_file(), 'this build has no bundled ad blocker'
    WORK.mkdir(parents=True, exist_ok=True)
    site = Site(a.doc_ms, a.img_ms); adblock = a.adblock == 'on'
    print(f'restore-first-load: build {build}, page {site.url} (document after {a.doc_ms} ms, picture after {a.img_ms} ms), '
          f'ad blocker {"on" if adblock else "OFF (control)"}', flush=True)
    results = {}
    if a.scenario in ('all', 'new'):
        results['new'] = scenario_new(build, site, a.runs, adblock, a.verbose)
    if a.scenario in ('all', 'home'):
        results['home'] = scenario_home(build, site, max(1, a.runs - 1) if a.scenario == 'all' else a.runs, adblock, a.verbose)
    if a.scenario in ('all', 'existing'):
        results['existing'] = scenario_existing(build, site, max(1, a.runs - 1) if a.scenario == 'all' else a.runs, False, adblock, a.verbose)
    if a.scenario in ('all', 'update'):
        results['update'] = scenario_existing(build, site, a.runs, True, adblock, a.verbose)
    site.httpd.shutdown()
    total = sum(len(r) for r in results.values()); passed = sum(sum(r) for r in results.values())
    print('restore-first-load: ' + ', '.join(f'{k} {sum(r)}/{len(r)}' for k, r in results.items()) + f' -> {passed}/{total} passed', flush=True)
    return 0 if passed == total else 1


if __name__ == '__main__':
    sys.exit(main())
