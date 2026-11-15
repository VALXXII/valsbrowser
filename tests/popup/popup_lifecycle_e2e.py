"""window.open target lifecycle on an isolated browser copy: failures, cancellation, profiles, private partition.

    python -I tests/popup/popup_lifecycle_e2e.py --app <built app dir> --out <dir>

Same isolation as popup_target_e2e.py: a throwaway copy of the app (its own data next to the exe), local HTTP fixture
pages, CDP over loopback for this copy only, messages posted to OUR window only (WM_APP+1 commands, WM_COPYDATA
« qa switch » of --workspace-test, WM_CLOSE). Failure stages are injected with BROWSER_POPUP_QA, which the host reads
only under --workspace-test. No SendInput, no foreground change, no clipboard, no desktop capture, no real profile.

Scenarios (each a fresh launch; observed values in <out>/result.json):
  script_fail / assign_fail   an injected initial-script / put_NewWindow failure: window.open returns null, the
                              placeholder tab is removed (no orphan page), the browser keeps working
  posthook_fail               the post-assignment handler failure: the assigned target is closed, no orphan
  pending_target_closed       the pending target's tab closed by the user while the request is held: null, no orphan
  window_close_pending        WM_CLOSE while a request is held: the process exits cleanly (no halt/error)
  profile_switch_pending      a request held while the window switches to another profile: the target is created in
                              the ORIGINAL profile (hidden), linked to its opener; the shown profile is untouched
  hidden_last_tab_close       a script-opened page that is its hidden profile's last tab calls window.close(): only that
                              tab closes; the shared window stays open on the other profile
  private_partition           in a private window the child sees the opener's localStorage/cookie sentinel; a normal
                              window of the same copy sees neither (and its own sentinel is shared with its child)
"""
from __future__ import annotations
import argparse, ast, base64, ctypes, ctypes.wintypes as W, hashlib, http.server, json, os, shutil, socket, struct, subprocess, sys
import threading, time, urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
tree = ast.parse((ROOT / 'tests/annotations/sandbox_qa.py').read_text(encoding='utf-8'))   # its stdlib CDP transport only
exec(compile(ast.Module(body=[n for n in tree.body if isinstance(n, ast.ClassDef) and n.name == 'WS'], type_ignores=[]),
             '<cdp-transport>', 'exec'), globals())

OPENER = """<!doctype html><title>OPENER</title><body>Local opener fixture
<script>window.received=[];addEventListener('message',e=>{if(e.origin===location.origin)received.push(String(e.data))});</script>"""
TARGET = """<!doctype html><title>TARGET</title><body>Local target fixture
<script>const n=new URLSearchParams(location.search).get('n')||'';document.title='TARGET '+n+' opener='+!!window.opener;
window.sentinel={ls:localStorage.getItem('sentinel'),cookie:(document.cookie.match(/sentinel=([^;]*)/)||[])[1]||null};
if(window.opener)window.opener.postMessage('hello:'+n,location.origin);</script>"""
u32 = ctypes.windll.user32
u32.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
WM_APP_SHORTCUT, WM_CLOSE, SELECT_TAB0 = 0x8000 + 1, 0x0010, 3010


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'   # keep-alive with Content-Length: no per-response socket close (local ERR_CONNECTION_ABORTED flakes)

    def do_GET(self):
        body = (TARGET if self.path.startswith('/target') else OPENER).encode()
        self.send_response(200); self.send_header('Content-Type', 'text/html; charset=utf-8'); self.send_header('Content-Length', str(len(body)))
        self.end_headers(); self.wfile.write(body)

    def log_message(self, *a):
        pass


def free_port():
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0)); return s.getsockname()[1]


class Run:
    """One launch of the throwaway copy."""
    def __init__(self, app: Path, base: str, args=(), qa='', session=True, profiles=False):
        self.app, self.base, self.port = app, base, free_port()
        self.http = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        if session and not profiles:
            (app / 'session').write_text(f'browser-session-2\n0\n1\n"{base}/opener"\n0\n', encoding='utf-8', newline='\n')
            (app / 'settings').write_text('browser-settings 1\nrestore-tabs=1\nconfirm-close-tabs=0\n', encoding='utf-8', newline='\n')
        env = dict(os.environ, WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS=f'--remote-debugging-port={self.port} --remote-debugging-address=127.0.0.1')
        env.pop('BROWSER_POPUP_QA', None)
        if qa:
            env['BROWSER_POPUP_QA'] = qa
        self.log_start = {p: p.stat().st_size for p in self.log_files()}
        self.proc = subprocess.Popen([str(app / 'browser.exe'), *args], cwd=app, env=env)
        self.sockets = []

    def targets(self):
        return [t for t in json.loads(self.http.open(f'http://127.0.0.1:{self.port}/json', timeout=3).read()) if t.get('type') == 'page']

    def connect(self, pred, timeout=10.0):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            try:
                t = next((t for t in self.targets() if pred(t)), None)
                if t:
                    ws = WS(t['webSocketDebuggerUrl']); self.sockets.append(ws); return ws
            except OSError as e:
                self.last_error = repr(e)
            time.sleep(.2)
        print('connect timed out', getattr(self, 'last_error', ''), flush=True)
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

    def command(self, cmd):
        h = self.window()
        if h:
            u32.PostMessageW(h, WM_APP_SHORTCUT, cmd, 0)

    def copydata(self, text):
        class CDS(ctypes.Structure):
            _fields_ = [('dwData', ctypes.c_size_t), ('cbData', W.DWORD), ('lpData', ctypes.c_void_p)]
        buf = ctypes.create_unicode_buffer(text)
        cds = CDS(0x464F5250, ctypes.sizeof(buf), ctypes.cast(buf, ctypes.c_void_p))
        res = ctypes.c_size_t()
        u32.SendMessageTimeoutW(self.window(), 0x4A, 0, ctypes.byref(cds), 2, 4000, ctypes.byref(res))
        return res.value

    def log_files(self):   # portable root log, and each profile's own log (profiles/<id>/browser.log)
        return [p for p in [self.app / 'prototype.log', *sorted((self.app / 'profiles').glob('*/browser.log'))] if p.exists()]

    def log(self):
        text = ''
        for p in self.log_files():
            with p.open('rb') as f:
                f.seek(self.log_start.get(p, 0)); text += f.read().decode('utf-8', errors='replace')
        return text

    def close(self, timeout=20):
        for ws in self.sockets:
            try:
                ws.s.close()
            except OSError:
                pass
        h = self.window()
        if h:
            u32.PostMessageW(h, WM_CLOSE, 0, 0)
        try:
            return self.proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            self.proc.terminate(); self.proc.wait(timeout=10); return 'terminated'


def ev(ws, code, timeout=None):
    r = ws.call('Runtime.evaluate', {'expression': code, 'returnByValue': True, 'awaitPromise': True, 'userGesture': True})
    return r.get('result', {}).get('value', r)


def wait(fn, timeout=8.0):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        v = fn()
        if v:
            return v
        time.sleep(.15)
    return fn()


def background(fn):
    box = {}
    th = threading.Thread(target=lambda: box.setdefault('value', fn()), daemon=True); th.start()
    return th, box


def main() -> int:
    ap = argparse.ArgumentParser(); ap.add_argument('--app', required=True); ap.add_argument('--out', required=True)
    ap.add_argument('--only', default='', help='comma-separated groups: failures,pending,close,profiles,private')
    a = ap.parse_args()
    only = set(filter(None, a.only.split(',')))

    def want(group):
        return not only or group in only
    src, out = Path(a.app).resolve(), Path(a.out).resolve()
    shutil.rmtree(out, ignore_errors=True); out.mkdir(parents=True)
    srv = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler); base = f'http://127.0.0.1:{srv.server_port}'
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    res = {'binary_sha256': hashlib.sha256((src / 'browser.exe').read_bytes()).hexdigest(),
           'version': json.loads((src / 'version.json').read_text(encoding='utf-8')), 'checks': {}, 'failures': []}

    def fresh(name):
        app = out / name / 'app'; app.mkdir(parents=True)
        for f in ('browser.exe', 'WebView2Loader.dll', 'version.json'):
            shutil.copy2(src / f, app / f)
        shutil.copytree(src / 'assets', app / 'assets')
        return app

    def check(name, ok, **seen):
        res['checks'][name] = {'ok': bool(ok), **seen}
        if not ok:
            res['failures'].append(name)
        print(('ok   ' if ok else 'FAIL ') + name + ' ' + json.dumps(seen, ensure_ascii=False, default=str)[:500], flush=True)

    def opener(run, timeout=35):
        return run.connect(lambda t: t.get('url', '') == base + '/opener', timeout)

    # ---- injected failures before / at / after assignment
    failure_cases = (('script-fail', 'script_fail', False), ('assign-fail', 'assign_fail', False), ('posthook-fail', 'posthook_fail', None))
    for qa, name, expect_ref in (failure_cases if want('failures') else ()):
        run = Run(fresh(name), base, ['--workspace-test'], qa=qa)
        try:
            op = opener(run)
            before = len(run.targets())
            ref = ev(op, "(()=>{window.r=window.open(location.origin+'/target?n=X','x');return r===null?'null':'ref'})()")
            settled = wait(lambda: len(run.targets()) == before, 6)
            closed = ev(op, "window.r?window.r.closed:null")
            ok_after = ev(op, "(()=>{const w=window.open('about:blank','again');return w===null})()") if qa != 'posthook-fail' else None
            log = run.log()
            marker = {'script_fail': 'POPUP initial script failed', 'assign_fail': 'POPUP put_NewWindow failed', 'posthook_fail': 'POPUP post-assignment handlers failed'}[name]
            ref_ok = ref == 'null' if expect_ref is False else (ref == 'null' or closed is True)
            check(name, ref_ok and settled and marker in log and 'POPUP target dropped' in log and 'WEBVIEW2 ERROR' not in log,
                  returned=ref, closed_after=closed, pages_before=before, pages_after=len(run.targets()), log_marker=marker in log,
                  dropped='POPUP target dropped' in log, later_open_null=ok_after)
        except Exception as e:
            check(name, False, aborted=repr(e))
        finally:
            code = run.close(); res['checks'][name]['exit'] = code

    # ---- the pending target closed by the user (Ctrl+W on it, it is the active tab) while the request is held
    run = Run(fresh('pending_target_closed'), base, ['--workspace-test'], qa='delay') if want('pending') else None
    try:
        if run is None:
            raise StopIteration
        op = opener(run)
        before = len(run.targets())
        th, box = background(lambda: ev(op, "(()=>{const w=window.open(location.origin+'/target?n=Y','y');return w===null?'null':'ref'})()"))
        held = wait(lambda: 'POPUP QA held before assignment' in run.log(), 6)
        run.command(ord('W'))
        th.join(15)
        settled = wait(lambda: len(run.targets()) == before, 6)
        log = run.log()
        check('pending_target_closed', held and box.get('value') == 'null' and settled and 'POPUP target assigned' not in log and 'WEBVIEW2 ERROR' not in log,
              held=bool(held), returned=box.get('value'), pages_before=before, pages_after=len(run.targets()))
    except StopIteration:
        pass
    except Exception as e:
        check('pending_target_closed', False, aborted=repr(e))
    finally:
        if run:
            run.close()

    # ---- WM_CLOSE while a request is held
    run = Run(fresh('window_close_pending'), base, ['--workspace-test'], qa='delay') if want('close') else None
    try:
        if run is None:
            raise StopIteration
        op = opener(run)
        th, box = background(lambda: ev(op, "(()=>{const w=window.open(location.origin+'/target?n=Z','z');return w===null?'null':'ref'})()"))
        held = wait(lambda: 'POPUP QA held before assignment' in run.log(), 6)
        h = run.window(); u32.PostMessageW(h, WM_CLOSE, 0, 0)
        t0 = time.monotonic()
        try:
            code = run.proc.wait(timeout=20)
        except subprocess.TimeoutExpired:
            code = 'timeout'
        log = run.log()
        check('window_close_pending', held and code == 0 and 'WEBVIEW2 ERROR' not in log and 'POPUP target assigned' not in log,
              held=bool(held), exit=code, seconds=round(time.monotonic() - t0, 1))
    except StopIteration:
        pass
    except Exception as e:
        check('window_close_pending', False, aborted=repr(e))
    finally:
        if run:
            run.close()

    # ---- profiles: a request held while the window switches profile; then the hidden profile's last tab closes itself
    app = fresh('profiles') if want('profiles') else None
    for pid in ('vert', 'ambre') if app else ():   # no « close N tabs? » prompt in this throwaway copy's profiles
        (app / 'profiles' / pid).mkdir(parents=True, exist_ok=True)
        (app / 'profiles' / pid / 'settings').write_text('browser-settings 1\nconfirm-close-tabs=0\n', encoding='utf-8', newline='\n')
    app and (app / 'profiles.txt').write_text('browser-profiles 1\nlast=vert\np=default|house|0|0|1|Personnel\np=vert|leaf|150|140|0|Vert\np=ambre|briefcase|55|140|0|Ambre\n',
                                      encoding='utf-8', newline='\n')
    run = Run(app, base, ['--profile', 'vert', '--workspace-test'], qa='delay', profiles=True) if app else None
    try:
        if run is None:
            raise StopIteration
        first = run.connect(lambda t: True, 35)
        first.call('Page.navigate', {'url': base + '/opener'})
        op = run.connect(lambda t: t.get('url', '') == base + '/opener', 15)
        wait(lambda: ev(op, 'document.readyState') == 'complete', 8)
        th, box = background(lambda: ev(op, "(()=>{window.r=window.open(location.origin+'/target?n=V','v');return r===null?'null':'ref'})()"))
        held = wait(lambda: 'POPUP QA held before assignment' in run.log(), 6)
        switched = run.copydata('qa switch ambre') == 1
        th.join(15)
        hello = wait(lambda: 'hello:V' in ev(op, 'received'), 8)
        log = run.log()
        child = run.connect(lambda t: '/target?n=V' in t.get('url', ''), 6)   # vert's engine (the only one on this port)
        check('profile_switch_pending', held and switched and box.get('value') == 'ref' and hello and child is not None and 'POPUP target assigned' in log,
              held=bool(held), switched=switched, returned=box.get('value'), hello=bool(hello), child_in_vert_engine=child is not None)
        # vert hidden now: make the child its last tab (switch back, select tab 1 = opener, close it, switch away), then
        # the child closes itself while ambre is shown.
        run.copydata('qa switch vert'); time.sleep(1.0)
        run.command(SELECT_TAB0); time.sleep(.4); run.command(ord('W')); time.sleep(1.0)
        only_child = [t['url'] for t in run.targets()]
        run.copydata('qa switch ambre'); time.sleep(1.0)
        ev(child, 'window.close()') if child else None
        gone = wait(lambda: not any('/target?n=V' in t.get('url', '') for t in run.targets()), 8)
        time.sleep(1.5)
        log = run.log()
        alive = run.proc.poll() is None and run.window() is not None
        check('hidden_last_tab_close', gone and alive and 'POPUP closed by its page' in log and 'closing the window' not in log,
              vert_pages_before=only_child, child_gone=bool(gone), window_alive=alive)
    except StopIteration:
        pass
    except Exception as e:
        check('profile_switch_pending', False, aborted=repr(e)) if 'profile_switch_pending' not in res['checks'] else check('hidden_last_tab_close', False, aborted=repr(e))
    finally:
        if run:
            run.close()

    # ---- private partition sentinels, then a normal window of the same copy
    app = fresh('private') if want('private') else None
    seen = {}
    for mode in ('private', 'normal') if app else ():
        run = Run(app, base, ['--private'] if mode == 'private' else [], session=(mode == 'normal'))
        try:
            if mode == 'private':
                # A private window does not load the local fixture through CDP Page.navigate (ERR_ABORTED, the same on
                # the unmodified baseline): the opener is typed in OUR window's address field instead (own Edit control).
                run.connect(lambda t: True, 35)
                run.command(ord('L')); time.sleep(.6)
                edit = u32.FindWindowExW(run.window(), None, 'Edit', None)
                ctypes.windll.user32.SendMessageW(edit, 0x0C, 0, ctypes.c_wchar_p(base + '/opener'))
                u32.PostMessageW(edit, 0x100, 0x0D, 1); u32.PostMessageW(edit, 0x101, 0x0D, 0xC0000001)
            op = opener(run, 20)
            wait(lambda: ev(op, 'document.readyState') == 'complete', 8)
            seen[mode + '_before'] = ev(op, "({ls:localStorage.getItem('sentinel'),cookie:(document.cookie.match(/sentinel=([^;]*)/)||[])[1]||null})")
            ev(op, f"localStorage.setItem('sentinel','{mode}');document.cookie='sentinel={mode};path=/'")
            ev(op, "(()=>{window.r=window.open(location.origin+'/target?n=S','s');return !!r})()")
            child = run.connect(lambda t: '/target?n=S' in t.get('url', ''), 8)
            seen[mode + '_child'] = wait(lambda: child and ev(child, 'window.sentinel'), 6) if child else None
        except Exception as e:
            seen[mode + '_error'] = repr(e)
        finally:
            run.close()
    app and check('private_partition', seen.get('private_child') == {'ls': 'private', 'cookie': 'private'}
          and seen.get('normal_before') == {'ls': None, 'cookie': None} and seen.get('normal_child') == {'ls': 'normal', 'cookie': 'normal'}, **seen)

    srv.shutdown(); srv.server_close()
    (out / 'result.json').write_text(json.dumps(res, ensure_ascii=False, indent=2, default=str), encoding='utf-8')
    print(f"{len(res['failures'])} failures: {res['failures']}")
    return 1 if res['failures'] else 0


if __name__ == '__main__':
    sys.exit(main())
