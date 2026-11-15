"""Script-opened windows (window.open) get a real target: own isolated browser copy, local HTTP fixture only.

    python -I tests/popup/popup_target_e2e.py --app <built app dir> --out <empty-able dir> [--label baseline]

Drives ONLY a throwaway copy of a built app (payload copied, its own profile/session next to the exe) whose page is
served by this script on 127.0.0.1; the WebView2 remote debugging port (loopback) is enabled for this copy only through
its environment. The page scripts are driven over CDP with userGesture (no SendInput, no foreground change, no
clipboard, no desktop capture). The window is closed with WM_CLOSE posted to our own window only.
Never a payment, wallet SDK, merchant, account or card: the pages are local fixtures.

Checks (each recorded with its observed values in <out>/result.json):
  direct        window.open(http target) returns a reference; the child sees window.opener; a nonce postMessage goes
                child -> opener and opener -> child -> opener (ack); the opener did not navigate
  blank         window.open('about:blank') returns a reference; document.write in it is visible in its own tab
                (document.visibilityState 'visible', title shown); a delayed redirect then loads the target, which
                sees its opener and completes the nonce exchange
  named         a second window.open with the same name reuses that window (same WindowProxy, one target page)
  noopener      window.open(..., 'noopener') returns null and the child has no opener (engine semantics)
  blank_target  target _blank returns a reference
  child_close   window.close() in the child removes only its tab; the opener sees .closed === true
  refused       file:, data:, javascript: and the debug sandbox origin are refused (null, no new page);
                about:blank then navigated to the debug sandbox is cancelled
  https_only    HTTPS-only (on in this copy) still runs in the popup's first load: an http:// popup to a non-exempt host
                (popup.test, mapped to 127.0.0.1 by --host-resolver-rules: no DNS leaves the machine) shows the host's
                « not secure » interstitial
  cert_denied   an invalid certificate (throwaway self-signed, local TLS server) is still refused in the popup's first load
  paymentRequest  typeof PaymentRequest, recorded only (WebView2 disables the Web Payment API; not changed here)
"""
from __future__ import annotations
import argparse, ast, base64, ctypes, ctypes.wintypes as W, hashlib, struct, http.server, json, os, shutil, socket, subprocess, sys, threading, time
import ssl, urllib.parse, urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
tree = ast.parse((ROOT / 'tests/annotations/sandbox_qa.py').read_text(encoding='utf-8'))   # its stdlib CDP transport only
exec(compile(ast.Module(body=[n for n in tree.body if isinstance(n, ast.ClassDef) and n.name == 'WS'], type_ignores=[]),
             '<cdp-transport>', 'exec'), globals())

OPENER = """<!doctype html><title>OPENER</title><body>Local opener fixture
<script>window.received=[];addEventListener('message',e=>{if(e.origin===location.origin)received.push(String(e.data))});</script>"""
TARGET = """<!doctype html><title>TARGET</title><body>Local target fixture
<script>const n=new URLSearchParams(location.search).get('n')||'';document.title='TARGET '+n+' opener='+!!window.opener;
addEventListener('message',e=>{if(e.origin===location.origin&&String(e.data)==='ping:'+n&&window.opener)window.opener.postMessage('ack:'+n,location.origin)});
if(window.opener)window.opener.postMessage('hello:'+n,location.origin);</script>"""


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


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--app', required=True); ap.add_argument('--out', required=True); ap.add_argument('--label', default='')
    a = ap.parse_args()
    src, out = Path(a.app).resolve(), Path(a.out).resolve()
    shutil.rmtree(out, ignore_errors=True); app = out / 'app'; app.mkdir(parents=True)
    for name in ('browser.exe', 'WebView2Loader.dll', 'version.json'):
        shutil.copy2(src / name, app / name)
    shutil.copytree(src / 'assets', app / 'assets')
    srv = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler); base = f'http://127.0.0.1:{srv.server_port}'
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    # Local TLS server with a throwaway self-signed certificate (never trusted anywhere): the certificate must be refused.
    tls = None
    try:
        key, crt = out / 'tls-key.pem', out / 'tls-cert.pem'
        subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-keyout', str(key), '-out', str(crt), '-days', '1',
                        '-subj', '/CN=127.0.0.1'], check=True, capture_output=True)
        tls = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); ctx.load_cert_chain(str(crt), str(key))
        tls.socket = ctx.wrap_socket(tls.socket, server_side=True)
        threading.Thread(target=tls.serve_forever, daemon=True).start()
    except (OSError, subprocess.CalledProcessError) as e:
        print('TLS fixture unavailable:', e, flush=True); tls = None
    port = free_port()
    (app / 'session').write_text(f'browser-session-2\n0\n1\n"{base}/opener"\n0\n', encoding='utf-8', newline='\n')
    (app / 'settings').write_text('browser-settings 1\nrestore-tabs=1\nconfirm-close-tabs=0\nhttps-only=1\n', encoding='utf-8', newline='\n')
    env = dict(os.environ, WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS=f'--remote-debugging-port={port} --remote-debugging-address=127.0.0.1 '
               '"--host-resolver-rules=MAP popup.test 127.0.0.1, MAP later.test 127.0.0.1"')
    http_open = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    proc = subprocess.Popen([str(app / 'browser.exe')], cwd=app, env=env)
    u32 = ctypes.windll.user32; u32.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
    res = {'label': a.label, 'binary_sha256': hashlib.sha256((app / 'browser.exe').read_bytes()).hexdigest(),
           'version': json.loads((app / 'version.json').read_text(encoding='utf-8')), 'fixture': base, 'checks': {}, 'failures': []}
    sockets = []

    def targets():
        return [t for t in json.loads(http_open.open(f'http://127.0.0.1:{port}/json', timeout=3).read()) if t.get('type') == 'page']

    def connect(pred, timeout=10.0):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            try:
                t = next((t for t in targets() if pred(t)), None)
                if t:
                    ws = WS(t['webSocketDebuggerUrl']); sockets.append(ws); return ws
            except OSError:
                pass
            time.sleep(.2)
        return None

    def ev(ws, code):
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

    def check(name, ok, **seen):
        res['checks'][name] = {'ok': bool(ok), **seen}
        if not ok:
            res['failures'].append(name)
        print(('ok   ' if ok else 'FAIL ') + name + ' ' + json.dumps(seen, ensure_ascii=False)[:400], flush=True)

    def pages_with(fragment):
        return [t['url'] for t in targets() if fragment in t.get('url', '')]

    try:
        op = connect(lambda t: t.get('url', '') == base + '/opener', 35)
        if not op:
            raise RuntimeError('own opener page did not load')
        res['paymentRequest'] = ev(op, 'typeof PaymentRequest')
        # ---- direct
        ref = ev(op, "(()=>{window.r1=window.open(location.origin+'/target?n=A','direct');return !!r1})()")
        child = connect(lambda t: '/target?n=A' in t.get('url', ''))
        hello = wait(lambda: 'hello:A' in ev(op, 'received'))
        ev(op, "r1&&r1.postMessage('ping:A',location.origin)")
        ack = wait(lambda: 'ack:A' in ev(op, 'received'))
        title = ev(child, 'document.title') if child else None
        check('direct', ref and child and hello and ack and title == 'TARGET A opener=true' and ev(op, 'location.href') == base + '/opener',
              reference=ref, child=bool(child), hello=bool(hello), ack=bool(ack), title=title, opener_url=ev(op, 'location.href'))
        # ---- blank, document.write, delayed redirect
        ref = ev(op, "(()=>{window.r2=window.open('about:blank','blankwin');if(!r2)return false;r2.document.write('<title>BLANK WRITTEN</title><p>written by the opener</p>');r2.document.close();return r2.document.title})()")
        bl = connect(lambda t: t.get('title') == 'BLANK WRITTEN')
        vis = wait(lambda: bl and ev(bl, 'document.visibilityState') == 'visible', 6) if bl else False
        text = ev(bl, 'document.body.innerText') if bl else None
        time.sleep(.8)
        still = ev(bl, 'document.title') if bl else None
        ev(op, "r2&&(r2.location.href=location.origin+'/target?n=B')")
        hello = wait(lambda: 'hello:B' in ev(op, 'received'))
        ev(op, "r2&&r2.postMessage('ping:B',location.origin)")
        ack = wait(lambda: 'ack:B' in ev(op, 'received'))
        check('blank', ref == 'BLANK WRITTEN' and bool(bl) and vis and still == 'BLANK WRITTEN' and hello and ack,
              returned=ref, tab_found=bool(bl), visible=bool(vis), text=text, title_after_800ms=still, hello=bool(hello), ack=bool(ack))
        # ---- named reuse
        # control: the opener navigates its loaded popup through the reference (no name involved)
        ev(op, "(()=>{window.rp=window.open(location.origin+'/target?n=P','pnav');return !!rp})()")
        hello_p = wait(lambda: 'hello:P' in ev(op, 'received'))
        ev(op, "rp&&(rp.location.href=location.origin+'/target?n=Q')")
        hello_q = wait(lambda: 'hello:Q' in ev(op, 'received'))
        check('proxy_nav', hello_p and hello_q, hello_P=bool(hello_p), hello_Q=bool(hello_q), pages_Q=pages_with('n=Q'))
        # a named window opened, loaded, then reused by name (what a page does when it reopens its popup)
        first = ev(op, "(()=>{window.rc=window.open(location.origin+'/target?n=C','named');return !!rc})()")
        hello_c = wait(lambda: 'hello:C' in ev(op, 'received'))
        same = ev(op, "(()=>{const b=window.open(location.origin+'/target?n=D','named');return !!b&&b===rc})()")
        hello_d = wait(lambda: 'hello:D' in ev(op, 'received'))
        time.sleep(.5)
        check('named', first and hello_c and same and hello_d and not pages_with('n=C') and len(pages_with('n=D')) == 1,
              reference=first, hello_C=bool(hello_c), same_proxy=same, hello_D=bool(hello_d), pages_C=pages_with('n=C'), pages_D=pages_with('n=D'))
        # recorded only (not asserted): two opens with one name in the same task, before the first target exists
        same2 = ev(op, "(()=>{const a=window.open(location.origin+'/target?n=G','named2');const b=window.open(location.origin+'/target?n=H','named2');return !!a&&a===b})()")
        time.sleep(1.5)
        res['observed_same_task_named_reopen'] = {'same_proxy': same2, 'pages_G': pages_with('n=G'), 'pages_H': pages_with('n=H'),
                                                  'titles': [t.get('title') for t in targets() if 'named2' in t.get('url', '') or 'n=G' in t.get('url', '') or 'n=H' in t.get('url', '')]}
        print('observed same-task named reopen', json.dumps(res['observed_same_task_named_reopen']), flush=True)
        # ---- noopener
        ref = ev(op, "window.open(location.origin+'/target?n=E','_blank','noopener')")
        ch = connect(lambda t: '/target?n=E' in t.get('url', ''))
        title = wait(lambda: ch and ev(ch, 'document.title'), 6) if ch else None
        check('noopener', ref is None and ch and title == 'TARGET E opener=false', returned=ref, child=bool(ch), title=title)
        # ---- _blank
        ref = ev(op, "(()=>{window.rb=window.open(location.origin+'/target?n=F','_blank');return !!rb})()")
        hello = wait(lambda: 'hello:F' in ev(op, 'received'))
        check('blank_target', ref and hello, reference=ref, hello=bool(hello))
        # ---- child close
        before = len(targets())
        ev(child, 'window.close()') if child else None
        closed = wait(lambda: ev(op, 'r1?r1.closed:null') is True)
        after = wait(lambda: len(targets()) == before - 1 and len(targets()), 6)
        check('child_close', closed and after == before - 1 and ev(op, 'location.href') == base + '/opener',
              opener_sees_closed=closed, pages_before=before, pages_after=len(targets()))
        # ---- refused schemes
        before = len(targets())
        refused = {}
        for label, url in (('file', 'file:///C:/Windows/win.ini'), ('data', 'data:text/html,<b>x</b>'), ('debug', 'https://browser-debug.invalid/')):
            refused[label] = ev(op, f"(()=>{{const w=window.open({json.dumps(url)},'_blank');return w===null}})()")
        time.sleep(1.0)
        # javascript: the engine asks the host for about:blank and runs the script in it (indistinguishable from an
        # exact blank open, which the opener already scripts fully): recorded, and no javascript: document may exist.
        js = ev(op, "(()=>{const w=window.open('javascript:void(0)','_blank');return w===null?'null':(w.location.href)})()")
        time.sleep(.8)
        res['observed_javascript_open'] = {'result': js, 'javascript_pages': pages_with('javascript:')}
        ev(op, "(()=>{window.rd=window.open('about:blank','todebug');if(rd)rd.location.href='https://browser-debug.invalid/';return !!rd})()")
        time.sleep(1.5)
        debug_pages = pages_with('browser-debug.invalid')
        extra = len(targets()) - before
        check('refused', all(refused.values()) and not debug_pages and not pages_with('javascript:') and pages_with('file:') == [],
              null_returned=refused, debug_pages=debug_pages, new_pages=extra, javascript_open=res['observed_javascript_open'])
        # ---- HTTPS-only in the popup's first load (http:// to a non-exempt host)
        ref = ev(op, f"(()=>{{window.rh=window.open('http://popup.test:{srv.server_port}/target?n=T','https');return !!rh}})()")
        time.sleep(3)
        hlog = (app / 'prototype.log').read_text(encoding='utf-8', errors='replace') if (app / 'prototype.log').exists() else ''
        served = [t.get('title') for t in targets() if t.get('title', '').startswith('TARGET T')]
        check('https_only', ref and 'HTTPS interstitial http://popup.test:' in hlog and not served,
              reference=ref, interstitial_logged='HTTPS interstitial http://popup.test:' in hlog, target_served=served)
        # ---- invalid certificate refused in the popup's first load
        if tls:
            tport = tls.server_address[1]
            ref = ev(op, f"(()=>{{window.rk=window.open('https://127.0.0.1:{tport}/target?n=K','cert');return !!rk}})()")
            time.sleep(3)
            served = [t.get('title') for t in targets() if t.get('title', '').startswith('TARGET K')]
            log = (app / 'prototype.log').read_text(encoding='utf-8', errors='replace') if (app / 'prototype.log').exists() else ''
            check('cert_denied', ref and not served and 'CERTIFICATE error https://127.0.0.1:' in log, reference=ref, target_served=served,
                  certificate_logged='CERTIFICATE error https://127.0.0.1:' in log)
        else:
            check('cert_denied', False, reason='TLS fixture unavailable')
        # ---- the host's own pages keep working in a tab a page opened (independent review of 582f15f, P1): their source is
        #      about:blank, so SourceChanged must still put the real address back or every button guard refuses
        def log_text():
            return (app / 'prototype.log').read_text(encoding='utf-8', errors='replace') if (app / 'prototype.log').exists() else ''
        def press(button, exclude):
            pressed = []
            for t in targets():
                if t['id'] in exclude or not t.get('url', '').startswith(('about:blank', 'data:')):
                    continue
                w = WS(t['webSocketDebuggerUrl']); sockets.append(w)
                pressed.append(ev(w, f"(()=>{{const b=document.getElementById('{button}');if(!b)return 'no button';b.click();return 'clicked'}})()"))
            return pressed
        dead = free_port(); retries0 = log_text().count('ERROR PAGE retry'); known = {t['id'] for t in targets()}
        ev(op, f"(()=>{{const a=document.createElement('a');a.href='http://127.0.0.1:{dead}/gone';a.target='_blank';document.body.appendChild(a);a.click();return true}})()")
        time.sleep(4)
        clicked = press('primary', known)
        retried = wait(lambda: log_text().count('ERROR PAGE retry') > retries0, 4)
        check('link_error_retry', 'clicked' in clicked and retried, clicked=clicked, retry_logged=log_text().count('ERROR PAGE retry') - retries0)
        ev(op, "(()=>{window.rl=window.open(location.origin+'/target?n=L','later');return !!rl})()")
        hello_l = wait(lambda: 'hello:L' in ev(op, 'received'))
        known = {t['id'] for t in targets()}; popup_l = {t['id'] for t in targets() if '/target?n=L' in t.get('url', '')}
        ev(op, f"rl&&(rl.location.href='http://later.test:{srv.server_port}/target?n=M')")
        time.sleep(3)
        clicked = press('go', known - popup_l)
        allowed = wait(lambda: 'HTTPS allowed for this session later.test' in log_text(), 4)
        check('popup_interstitial_continue', hello_l and 'clicked' in clicked and allowed, hello_L=bool(hello_l), clicked=clicked, allowed_logged=bool(allowed))
        # ---- an ordinary new tab (Ctrl+T's command, posted to OUR window only) still gets the native home:
        #      its about:blank stays the hidden home document, unlike a script-opened blank page
        own = []

        @ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
        def find_own(hwnd, _):
            pid = W.DWORD(); u32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            cls = ctypes.create_unicode_buffer(64); u32.GetClassNameW(hwnd, cls, 64)
            if pid.value == proc.pid and cls.value == 'LisiereBrowser':
                own.append(hwnd)
            return True
        u32.EnumWindows(find_own, 0)
        before_ids = {t['id'] for t in targets()}
        if own:
            u32.PostMessageW(own[0], 0x8000 + 1, ord('T'), 0)
        new_tab = wait(lambda: next((t for t in targets() if t['id'] not in before_ids), None), 8)
        home_ws = connect(lambda t: new_tab is not None and t['id'] == new_tab['id']) if new_tab else None
        home_title = wait(lambda: home_ws and ev(home_ws, 'document.title'), 8) if home_ws else None
        home_vis = ev(home_ws, 'document.visibilityState') if home_ws else None
        check('home_tab', bool(new_tab) and home_title in ('Nouvel onglet', 'New tab') and home_vis == 'hidden',
              window_found=bool(own), url=(new_tab.get('url') or '')[:40] if new_tab else None, title=home_title, visibility=home_vis)
    except Exception as e:   # recorded, never hidden
        res['failures'].append(f'aborted: {e!r}'); print('FAIL aborted', repr(e), flush=True)
    finally:
        for ws in sockets:
            try:
                ws.s.close()
            except OSError:
                pass

        @ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
        def close_own(hwnd, _):
            pid = W.DWORD(); u32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            if pid.value == proc.pid:
                cls = ctypes.create_unicode_buffer(64); u32.GetClassNameW(hwnd, cls, 64)
                if cls.value == 'LisiereBrowser':
                    u32.PostMessageW(hwnd, 0x0010, 0, 0)
            return True
        u32.EnumWindows(close_own, 0)
        try:
            proc.wait(timeout=20)
        except subprocess.TimeoutExpired:
            proc.terminate(); proc.wait(timeout=10)
        srv.shutdown(); srv.server_close()
        if tls:
            tls.shutdown(); tls.server_close()
        log = app / 'prototype.log'
        res['log_tail'] = [l for l in (log.read_text(encoding='utf-8', errors='replace').splitlines() if log.exists() else [])
                           if 'POPUP' in l or 'ERROR' in l][-40:]
        (out / 'result.json').write_text(json.dumps(res, ensure_ascii=False, indent=2), encoding='utf-8')
    print(f"{len(res['failures'])} failures: {res['failures']}")
    return 1 if res['failures'] else 0


if __name__ == '__main__':
    sys.exit(main())
