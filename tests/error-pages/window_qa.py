"""#121 error pages in the REAL browser window against the Paper boards — night run only (real window, focus).

    python tests/error-pages/window_qa.py [--app build/error-pages-qa-<stamp>] [--skip-build]

What it does, on its own copy only:
  1. builds a FRESH folder (tools/release/build.py --out build/error-pages-qa-<stamp>, dev channel): its own
     portable profile beside the exe, never %LOCALAPPDATA%\\browser; settings « HTTPS uniquement » on, French,
     no onboarding (BROWSER_NO_ONBOARDING=1);
  2. starts it with --debug-sandbox inside a job object (KILL_ON_JOB_CLOSE), DevTools on 127.0.0.1 only
     (WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS, this copy only), and puts its window on a 100 % monitor when there is
     one (this PC: the portrait one, low on it, away from the floating widgets);
  3. makes each failure happen for real in the tab, with the boards' addresses, WITHOUT any network: DevTools
     Fetch fails the request locally (lemonde.fr offline, lemond.fr not found, localhost:3000 refused, impots.gouv.fr
     timed out, bibliotheque-municipale.fr's https:// attempt refused → the « non sécurisé » page, lemonde.fr reset,
     intranet.local unreachable, exemple.fr/rapport failed, impots.gouv.fr's 2nd failure with the sheet open); the
     browser then shows our page through its own code path (NavigationFailed reads the engine's error document for
     a refusal, ShowErrorPage / ShowInsecureInterstitial). Then the copy button's 5 states (hover, pressed, « Copié »
     after main's answer, focus). « Reprise » and the 5 certificate cases come from the sandbox (browser://debug,
     forced variants, *.sandbox.invalid);
  4. per case: the layout against the boards (cases.py), the copy button (the clipboard holds the code), the focus
     on the primary action, a 1x capture of the block compared with the board's PNG, and a screen capture of the
     whole window for the review (needs an unlocked session);
  5. writes build/error-pages-qa-report/<stamp>/report.md (+ JSON, images) and exits 1 on a layout difference.
Nothing else is touched: no real site is contacted (every intercepted request is failed or the run stops), no
other browser process, no profile but the fresh one.
"""
from __future__ import annotations
import argparse, base64, ctypes, ctypes.wintypes as W, io, json, os, socket, struct, subprocess, sys, time, urllib.request
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import cases  # noqa: E402
from run import kill_on_close_job, k32, ntdll  # noqa: E402  (same job object helper as the windowless run)

PORT = 9421
user32 = ctypes.windll.user32
user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))   # per-monitor v2: real pixels
SANDBOX = 'https://browser-debug.invalid/'
# Board address → how DevTools fails its request (CDP Network.ErrorReason). Nothing reaches the network.
FAIL = {'www.lemonde.fr': 'InternetDisconnected', 'lemond.fr': 'NameNotResolved', 'localhost': 'ConnectionRefused',
        'impots.gouv.fr': 'TimedOut', 'bibliotheque-municipale.fr': 'ConnectionRefused',
        # board « compléments » 1SYN-0 (24/11): the three remaining pages, with the board's addresses
        'lemonde.fr': 'ConnectionReset', 'intranet.local': 'AddressUnreachable', 'exemple.fr': 'Failed'}
REAL = [('offline', 'https://www.lemonde.fr/planete/'), ('dns', 'https://lemond.fr/'), ('refused', 'http://localhost:3000/'),
        ('timeout', 'https://impots.gouv.fr/'), ('insecure', 'http://bibliotheque-municipale.fr/horaires'),
        ('reset', 'https://lemonde.fr/'), ('unreachable', 'http://intranet.local/'), ('generic', 'https://exemple.fr/rapport'),
        ('timeout-open', 'https://impots.gouv.fr/')]   # the 2nd failure (after the 8 s retry), sheet open: then the copy states
SANDBOXED = [('restored', 'error.offline', 'restored'), ('cert-invalid', 'error.page', 'certInvalid'), ('cert-expired', 'error.page', 'certExpired'),
             ('cert-wrong-name', 'error.page', 'certWrongName'), ('cert-revoked', 'error.page', 'certRevoked'), ('cert-client', 'error.page', 'certClient')]


class Cdp:
    """Minimal DevTools WebSocket client that answers Fetch.requestPaused while it waits."""
    def __init__(self, url):
        hostport, path = url[5:].split('/', 1)
        host, port = hostport.split(':')
        self.s = socket.create_connection((host, int(port)), timeout=15)
        key = base64.b64encode(os.urandom(16)).decode()
        self.s.sendall((f'GET /{path} HTTP/1.1\r\nHost: {hostport}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n'
                        f'Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n').encode())
        resp = b''
        while b'\r\n\r\n' not in resp:
            resp += self.s.recv(4096)
        if b' 101 ' not in resp.split(b'\r\n')[0]:
            raise RuntimeError(resp[:200])
        self.buf, self.n, self.failed, self.leaked = resp.split(b'\r\n\r\n', 1)[1], 0, [], []

    def _recv(self, n):
        while len(self.buf) < n:
            chunk = self.s.recv(65536)
            if not chunk:
                raise EOFError
            self.buf += chunk
        out, self.buf = self.buf[:n], self.buf[n:]
        return out

    def _send(self, obj):
        data = json.dumps(obj).encode(); mask = os.urandom(4); head = bytearray([0x81])
        if len(data) < 126:
            head.append(0x80 | len(data))
        elif len(data) < 65536:
            head.append(0x80 | 126); head += struct.pack('>H', len(data))
        else:
            head.append(0x80 | 127); head += struct.pack('>Q', len(data))
        self.s.sendall(bytes(head) + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(data)))

    def _read(self):
        msg = b''
        while True:
            b0, b1 = self._recv(2); ln = b1 & 0x7F
            if ln == 126:
                ln = struct.unpack('>H', self._recv(2))[0]
            elif ln == 127:
                ln = struct.unpack('>Q', self._recv(8))[0]
            payload = self._recv(ln)
            if (b0 & 0x0F) in (0, 1):
                msg += payload
                if b0 & 0x80:
                    return json.loads(msg.decode('utf-8', 'replace'))

    def _event(self, m):
        if m.get('method') != 'Fetch.requestPaused':
            return
        p = m['params']; url = p['request']['url']; host = url.split('://', 1)[-1].split('/', 1)[0].split(':')[0].lower()
        reason = FAIL.get(host)
        if reason:
            self.failed.append(url); self._send({'id': 0, 'method': 'Fetch.failRequest', 'params': {'requestId': p['requestId'], 'errorReason': reason}})
        else:   # never let anything out: an unexpected request is failed too, and reported
            self.leaked.append(url); self._send({'id': 0, 'method': 'Fetch.failRequest', 'params': {'requestId': p['requestId'], 'errorReason': 'BlockedByClient'}})

    def call(self, method, params=None):
        self.n += 1; self._send({'id': self.n, 'method': method, 'params': params or {}})
        while True:
            m = self._read()
            if m.get('id') == self.n:
                if 'error' in m:
                    raise RuntimeError(f'{method}: {m["error"]}')
                return m.get('result', {})
            self._event(m)

    def eval(self, js):
        r = self.call('Runtime.evaluate', {'expression': js, 'returnByValue': True, 'awaitPromise': True})
        return r.get('result', {}).get('value')

    def pump(self, seconds):
        end = time.time() + seconds; self.s.settimeout(0.2)
        try:
            while time.time() < end:
                try:
                    self._event(self._read())
                except (socket.timeout, TimeoutError):
                    pass
        finally:
            self.s.settimeout(15)


def targets():
    return json.loads(urllib.request.urlopen(f'http://127.0.0.1:{PORT}/json', timeout=5).read())


def page(predicate, wait=30):
    end = time.time() + wait
    while time.time() < end:
        try:
            for t in targets():
                if t.get('type') == 'page' and predicate(t.get('url', '')):
                    return Cdp(t['webSocketDebuggerUrl'])
        except OSError:
            pass
        time.sleep(0.4)
    raise SystemExit('page target not found')


def mark_old(cdp):
    """Marks the current document, so the wait below only accepts the NEXT one."""
    try:
        cdp.eval('window.__qaOld=1')
    except RuntimeError:
        pass


def wait_error_page(cdp, seconds=20):
    end = time.time() + seconds
    while time.time() < end:
        cdp.pump(0.3)
        try:
            if cdp.eval('!window.__qaOld&&!!document.querySelector("main .text h1")&&document.fonts.status==="loaded"'):
                cdp.pump(0.4)   # entrance animation (160 ms)
                return True
        except RuntimeError:
            pass
    return False


def main_window(pid):
    found = []
    def cb(h, _):
        p = W.DWORD(); user32.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value == pid and user32.IsWindowVisible(h) and not user32.GetWindow(h, 4):
            found.append(h)
        return True
    user32.EnumWindows(ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)(cb), 0)
    return found[0] if found else None


def place_on_100_percent(hwnd):
    """The window on a 96-DPI monitor when there is one (1x captures, like the boards), else left where it is."""
    shcore = ctypes.windll.shcore; mons = []
    shcore.GetDpiForMonitor.argtypes = [W.HMONITOR, ctypes.c_int, ctypes.POINTER(W.UINT), ctypes.POINTER(W.UINT)]
    def cb(h, _hdc, rect, _):
        x, y = W.UINT(), W.UINT(); shcore.GetDpiForMonitor(h, 0, ctypes.byref(x), ctypes.byref(y))
        r = rect.contents; mons.append((x.value, r.left, r.top, r.right, r.bottom)); return True
    user32.EnumDisplayMonitors(None, None, ctypes.WINFUNCTYPE(W.BOOL, W.HMONITOR, W.HDC, ctypes.POINTER(W.RECT), W.LPARAM)(cb), 0)
    for dpi, l, t, r, b in mons:
        if dpi == 96 and r - l >= 1200 and b - t >= 700:
            y = t + min(1100, max(0, b - t - 720))
            user32.ShowWindow(hwnd, 9); user32.SetWindowPos(hwnd, None, l + 40, y, 1180, 667, 0x0004)
            return 96
    return user32.GetDpiForWindow(hwnd)


def window_shot(hwnd, path):
    try:
        from PIL import ImageGrab
        r = W.RECT(); user32.GetWindowRect(hwnd, ctypes.byref(r))
        ImageGrab.grab(bbox=(r.left, r.top, r.right, r.bottom), all_screens=True).save(path)
        return True
    except Exception as e:   # locked session: no screen grab
        print(f'      window capture unavailable: {e}')
        return False


def block_shot(cdp, m, dpr):
    from PIL import Image
    r = m['main']; clip = {'x': r['x'], 'y': r['y'], 'width': r['w'], 'height': r['h'], 'scale': 1}
    png = base64.b64decode(cdp.call('Page.captureScreenshot', {'format': 'png', 'clip': clip})['data'])
    img = Image.open(io.BytesIO(png)).convert('RGB')
    if dpr != 1:   # not on a 100 % monitor: brought back to 1x for the comparison (noted in the report)
        img = img.resize((round(r['w']), round(r['h'])), Image.LANCZOS)
    return img


def clipboard_text():
    CF_UNICODETEXT = 13
    user32.GetClipboardData.restype = ctypes.c_void_p
    k = ctypes.windll.kernel32; k.GlobalLock.restype = ctypes.c_void_p; k.GlobalLock.argtypes = [ctypes.c_void_p]; k.GlobalUnlock.argtypes = [ctypes.c_void_p]
    if not user32.OpenClipboard(None):
        return None
    h = None
    try:
        h = user32.GetClipboardData(CF_UNICODETEXT)
        p = k.GlobalLock(h) if h else None
        return ctypes.wstring_at(p) if p else ''
    finally:
        if h:
            k.GlobalUnlock(h)
        user32.CloseClipboard()


def run_case(name, cdp, out, dpr, hwnd, rows, failures, sandboxed=False):
    if name in cases.PREPARE:
        xy = cdp.eval(cases.PREPARE[name])
        if xy:
            x, y = map(float, xy.split(','))
            cdp.call('Input.dispatchMouseEvent', {'type': 'mouseMoved', 'x': x, 'y': y}); cdp.pump(0.3)
    m = json.loads(cdp.eval(cases.MEASURE_JS))
    layout = cases.check(name, m)
    if sandboxed:   # *.sandbox.invalid hosts: the text wraps differently from the board's host
        layout = [d for d in layout if not d.startswith(('text height', 'block height'))]
    notes = []
    want_focus = 'back' if name == 'insecure' else 'primary'
    if name != 'restored' and m.get('focused') != want_focus:
        notes.append(f'focus on {m.get("focused")!r} (expected the primary action)')
    pixels = {}
    try:   # captured before the copy test moves the pointer (the refused board shows « Détails techniques » hovered)
        img = block_shot(cdp, m, dpr); img.save(out / f'{name}-block.png')
        pixels = cases.compare(name, img, out)
    except Exception as e:
        notes.append(f'block capture failed: {e}')
    window_shot(hwnd, out / f'{name}-window.png')
    if m.get('copy'):
        c = m['copy']; cdp.call('Input.dispatchMouseEvent', {'type': 'mouseMoved', 'x': c['x'] + 8, 'y': c['y'] + 9})
        for kind in ('mousePressed', 'mouseReleased'):
            cdp.call('Input.dispatchMouseEvent', {'type': kind, 'x': c['x'] + 8, 'y': c['y'] + 9, 'button': 'left', 'clickCount': 1})
        cdp.pump(0.5)
        code = (m.get('code') or [{}])[0].get('text', '')
        got = clipboard_text()
        if got != code:
            layout.append(f'copy button: clipboard {got!r}, expected {code!r}')
    rows.append({'name': name, 'layout': layout, 'notes': notes, 'pixels': pixels, 'title': m.get('title'), 'sandboxed': sandboxed})
    print(f'  {name:16} {"OK" if not layout else "DIFF"}  pixels {pixels or "-"}  {"; ".join(notes)}')
    for d in layout:
        print(f'      {d}')
    failures += [f'{name}: {d}' for d in layout]


def copy_states(cdp, out, dpr, rows, failures):
    """The copy button's five states in the real window (1T2J-0), on the time-out page with its sheet open: rest,
    hover (C5 tooltip after 600 ms), pressed, « Copié » once main has put the code on the clipboard (1.5 s, then rest),
    keyboard focus (the owner's A1 inset ring)."""
    def measure():
        return json.loads(cdp.eval(cases.MEASURE_JS))

    def record(state, label, m, shot=False):
        bad = cases.check_copy(state, m)
        if shot:
            try:
                block_shot(cdp, m, dpr).save(out / f'{label}-block.png')
            except Exception as e:
                bad.append(f'capture failed: {e}')
        rows.append({'name': label, 'layout': bad, 'notes': [], 'pixels': {}})
        print(f'  {label:16} {"OK" if not bad else "DIFF"}  {m.get("copyState")}')
        for d in bad:
            print(f'      {d}')
        failures.extend(f'{label}: {d}' for d in bad)

    slot = measure().get('copy')
    if not slot:
        failures.append('copy states: no copy button on the open time-out sheet'); return
    x, y = slot['x'] + slot['w'] / 2, slot['y'] + slot['h'] / 2
    mouse = lambda kind, px, py, **k: cdp.call('Input.dispatchMouseEvent', {'type': kind, 'x': px, 'y': py, **k})
    mouse('mouseMoved', 5, 5); cdp.pump(1.8)   # run_case's copy test just clicked: let its « Copié » (1.5 s) end
    record('copy-rest', 'copy-rest', measure())
    mouse('mouseMoved', x, y); cdp.pump(0.9)
    record('copy-hover', 'copy-hover', measure(), shot=True)
    mouse('mousePressed', x, y, button='left', clickCount=1); cdp.pump(0.25)
    record('copy-pressed', 'copy-pressed', measure(), shot=True)
    mouse('mouseReleased', x, y, button='left', clickCount=1); cdp.pump(0.3)   # a click: main copies, answers « copied »
    mouse('mouseMoved', 5, 5); cdp.pump(0.2)
    m = measure(); record('copy-copied', 'copy-copied', m, shot=True)
    code = (m.get('code') or [{}])[0].get('text', '')
    if clipboard_text() != code:
        failures.append(f'copy-copied: clipboard {clipboard_text()!r}, expected {code!r}')
    cdp.pump(1.6)
    record('copy-rest', 'copy-rest-after', measure())   # « Copié » lasts 1.5 s
    # The window is not brought to the front (no SendInput): without focus emulation a background window's page never
    # matches :focus-visible (night 25/11: ring « none », no tooltip). The page is told it has the focus, as a front window.
    cdp.call('Emulation.setFocusEmulationEnabled', {'enabled': True})
    # The last input was the mouse (the states above): a scripted focus() then never matches :focus-visible. A key first, as
    # a keyboard user's Tab would (Shift: nothing moves, nothing is typed).
    for kind in ('keyDown', 'keyUp'):
        cdp.call('Input.dispatchKeyEvent', {'type': kind, 'key': 'Shift', 'code': 'ShiftLeft', 'windowsVirtualKeyCode': 16})
    cdp.eval('document.querySelector("[data-action=copy]").focus({focusVisible:true})'); cdp.pump(0.3)
    record('copy-focus', 'copy-focus', measure(), shot=True)


def main() -> int:
    ap = argparse.ArgumentParser(); ap.add_argument('--app'); ap.add_argument('--skip-build', action='store_true'); a = ap.parse_args()
    stamp = datetime.now().strftime('%Y%m%d-%H%M%S')
    app = Path(a.app) if a.app else ROOT / 'build' / f'error-pages-qa-{stamp}'
    if (app / 'profile').exists() or (app / 'session').exists():
        sys.exit(f'{app} already has a profile: use a fresh folder (the default)')
    if not a.skip_build:
        if subprocess.run([sys.executable, str(ROOT / 'tools' / 'release' / 'build.py'), '--out', str(app)], cwd=ROOT).returncode:
            sys.exit('FAILED: build')
    (app / 'settings').write_text('browser-settings 1\nhttps-only=1\n', encoding='utf-8', newline='\n')
    (app / 'ui-language').write_text('browser-ui-language 1\nlanguage=fr\n', encoding='utf-8', newline='\n')
    out = ROOT / 'build' / 'error-pages-qa-report' / stamp; out.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, BROWSER_NO_ONBOARDING='1',
               WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS=f'--remote-debugging-port={PORT} --remote-allow-origins=http://127.0.0.1:{PORT}')
    job = kill_on_close_job(); rows, failures = [], []
    try:
        proc = subprocess.Popen([str(app / 'browser.exe'), '--debug-sandbox'], cwd=app, env=env, creationflags=0x4)
        handle = W.HANDLE(int(proc._handle))
        if not k32.AssignProcessToJobObject(job, handle):
            proc.kill(); sys.exit('AssignProcessToJobObject failed')
        ntdll.NtResumeProcess(handle)
        sandbox = page(lambda u: u.startswith(SANDBOX), 60)
        hwnd = None
        for _ in range(50):
            hwnd = main_window(proc.pid)
            if hwnd:
                break
            time.sleep(0.2)
        dpi = place_on_100_percent(hwnd) if hwnd else 96
        dpr = dpi / 96
        print(f'window on a {dpi} DPI monitor (captures {"1x" if dpr == 1 else "brought back to 1x"})')
        # Real failures in a tab of their own (opened from the sandbox page: a plain new tab).
        before = {t.get('id') for t in targets() if t.get('type') == 'page'}
        sandbox.eval('window.open("about:blank","_blank")'); time.sleep(1.5)
        # the new tab's target (its URL may still read '' rather than about:blank: night run 24/11)
        tab = None
        for _ in range(12):
            new = [t for t in targets() if t.get('type') == 'page' and t.get('id') not in before and t.get('url', '') in ('', 'about:blank')]
            if new:
                tab = Cdp(new[0]['webSocketDebuggerUrl']); break
            time.sleep(0.4)
        if tab is None:   # no reachable popup tab: the window's first tab is a plain new tab too (« Nouvel onglet », a data: page)
            tab = page(lambda u: u == 'about:blank' or (u.startswith('data:text/html') and u != SANDBOX))
            if hwnd:   # bring that first tab to the front (posted click on its tab, 96 DPI monitor: DIP = px)
                for m, w in ((0x0200, 0), (0x0201, 1), (0x0202, 0)):
                    user32.PostMessageW(hwnd, m, w, (100 & 0xFFFF) | (20 << 16))
                time.sleep(1.0)
        tab.call('Fetch.enable', {'patterns': [{'urlPattern': '*', 'requestStage': 'Request'}]})
        for name, url in REAL:
            if name == 'timeout-open':   # start the count again (another address), then the 1st failure of impots.gouv.fr
                mark_old(tab); tab.call('Page.navigate', {'url': 'https://lemond.fr/'}); wait_error_page(tab)
            mark_old(tab); tab.call('Page.navigate', {'url': url})
            if not wait_error_page(tab):
                failures.append(f'{name}: our page did not appear (title {tab.eval("document.title")!r})'); continue
            if name == 'timeout-open':   # its automatic retry after 8 s fails again: the 2nd failure, « nouvel essai dans 16 s »
                mark_old(tab); t0 = time.time()
                if not wait_error_page(tab, 30):
                    failures.append('timeout-open: the automatic retry did not bring a new page'); continue
                print(f'  automatic retry after the 1st time-out: new page {time.time() - t0:.1f} s later (8 s countdown)')
            if name.startswith('timeout'):   # hold this page's own retry (8 / 16 s) while it is measured: the page stays
                tab.eval('busy=true')
            if name == 'refused':   # the board shows the second attempt: « Réessayer » once (failed again, locally)
                mark_old(tab); tab.eval('document.getElementById("primary").click()')
                if not wait_error_page(tab):
                    failures.append('refused: « Réessayer » did not bring a new page'); continue
            run_case(name, tab, out, dpr, hwnd, rows, failures)
            if name == 'timeout-open':
                copy_states(tab, out, dpr, rows, failures)
        if tab.leaked:
            failures.append(f'unexpected requests (failed, never sent): {tab.leaked}')
        print(f'  requests failed locally by DevTools: {len(tab.failed)}')
        for name, command, variant in SANDBOXED:
            if hwnd:   # the sandbox tab (second) in front for its own commands
                for m, w in ((0x0200, 0), (0x0201, 1), (0x0202, 0)):
                    user32.PostMessageW(hwnd, m, w, (250 & 0xFFFF) | (20 << 16))
                time.sleep(0.6)
            before = {t['id'] for t in targets() if t.get('type') == 'page'}
            sandbox.eval(f'document.querySelector(\'[data-command="{command}"][data-variant="{variant}"]\').click()')
            new = None
            for _ in range(120):   # up to 30 s: a tab's engine view can come late
                fresh = [t for t in targets() if t.get('type') == 'page' and t['id'] not in before]
                if fresh:
                    new = Cdp(fresh[0]['webSocketDebuggerUrl']); break
                time.sleep(0.25)
            # The sandbox tab really looks up *.sandbox.invalid: on this PC Windows takes ~11 s to answer « no such
            # host » (getaddrinfo, VPN DNS) and the engine up to ~25 s, past the old 20 s wait (night 25/11, the page
            # of each case came at the next command). Up to 75 s here.
            if not new or not wait_error_page(new, 75):
                # Night 24/11 these cases were never reached: say what the browser did (its sandbox log, the targets).
                try:
                    journal = sandbox.eval('[...document.getElementById("log").children].slice(-4).map(li=>li.textContent).join(" | ")')
                except Exception as e:
                    journal = f'unreadable ({e})'
                seen = [t.get('url', '')[:60] for t in targets() if t.get('type') == 'page']
                failures.append(f'{name}: the sandbox tab did not show our page (new target: {"yes" if new else "no"}; sandbox log: {journal}; page targets: {seen})'); continue
            run_case(name, new, out, dpr, hwnd, rows, failures, sandboxed=True)
    finally:
        k32.CloseHandle(job)   # KILL_ON_JOB_CLOSE: the browser and its engine end here
    cases.write_report(out / 'report.md', f'#121 pages d\'erreur — vraie fenêtre vs planches Paper ({stamp})', rows)
    print(f'report: {out / "report.md"}')
    print('error pages (window) vs boards: ' + ('FAILED (' + str(len(failures)) + ')' if failures else f'OK ({len(rows)} cases)'))
    for f in failures:
        print('FAILED: ' + f)
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
