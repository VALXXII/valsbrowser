"""Iframes case 2 in the REAL browser, on a never-displayed Windows desktop: nothing on the shared screen, no SendInput,
no real agent (the pattern of tests/chat/final/c1_hidden_desktop.py).

    python -X utf8 -I tests/annotations/iframe-demo/card_hidden_desktop.py <build-dir> [--ports 5196,5197,9361]

<build-dir> holds browser.exe and its fake agent in <build-dir>-test/. A copy, its profile and log: build/iframes-card-run/
(replay.txt: every line timed). The demo page (tests/annotations/iframe-demo: the shop on http://localhost:<shop>/, « the
other site » on http://127.0.0.1:<other>/). With the mode on (the browser's own command), the pointer (DevTools input,
trusted in the page) goes over the « Paiement sécurisé » iframe of the other site:
  - the card appears under the iframe (its top 12 DIP under the iframe's bottom, its left on the iframe's left), 300 DIP
    wide, never activated;
  - the pointer onto an element of the page: the card goes (after its 300 ms grace);
  - over the « Livraison » iframe of the same site: no card;
  - « En savoir plus » (the card's own Invoke path): the panel at the drawers' place (the window's right edge, 332 DIP);
    Échap closes it;
  - « Annoter le cadre »: the iframe is selected (« select » handled, frame « cross-origin »), the card goes.
SAFETY (rule of 25 Nov 2026), before any launch: the fake agent is an absolute existing path, the home is sealed
(USERPROFILE / APPDATA / LOCALAPPDATA under the copy, PATH = Windows only), checked; during the run any sign of a real agent
kills our browser at once. Our own processes only, stopped by PID. Exit code 1 on any failure."""
import argparse, ctypes, ctypes.wintypes as W, json, os, pathlib, re, shutil, subprocess, sys, threading, time, urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tests' / 'annotations'))
import sandbox_qa as Q   # noqa: E402  (CDP WebSocket)

ap = argparse.ArgumentParser(); ap.add_argument('build'); ap.add_argument('--ports', default='5196,5197,9361')
ap.add_argument('--layout', choices=('top', 'side', 'compact'), default='top',
                help='tabs position: the drawers (and the help panel) dock on the right in « top », on the left against the side panel otherwise (#143)')
args = ap.parse_args()
BUILD = pathlib.Path(args.build).resolve(); SRC = BUILD if (BUILD / 'browser.exe').is_file() else BUILD / 'app'
FAKE = (BUILD.parent / (BUILD.name + '-test') / 'fake-acp-agent.exe').resolve()
assert FAKE.is_absolute() and FAKE.is_file(), 'the fake agent must be an absolute, existing path (the browser runs from its own folder)'
SHOP, OTHER, CDP_PORT = (int(x) for x in args.ports.split(','))
BASE = ROOT / 'build' / 'iframes-card-run'; APP, WORK = BASE / 'app', BASE / 'work'; AGENTS = WORK / 'agent-configs'; FLOG = WORK / 'fake.log'
URL = f'http://localhost:{SHOP}/'
OUT = BASE / 'replay.txt'
k32 = ctypes.WinDLL('kernel32', use_last_error=True); u32 = ctypes.WinDLL('user32', use_last_error=True)
u32.CreateDesktopW.restype = W.HANDLE
DESK = f'iframes-card-{os.getpid()}'
hdesk = u32.CreateDesktopW(DESK, None, None, 0, 0x10000000, None)
lines_out, failures = [], []
T0 = time.perf_counter()
def say(s): t = time.perf_counter() - T0; lines_out.append(f'{t:8.3f}  {s}'); print(f'{t:8.3f}  {s}', flush=True)
def check(ok, what, detail=''):
    say(('ok   ' if ok else 'FAIL ') + what + (f' — {detail}' if detail != '' else ''))
    if not ok: failures.append(what)
    return ok

# ---------------------------------------------------------------- the copy
shutil.rmtree(BASE, ignore_errors=True); APP.mkdir(parents=True); WORK.mkdir(parents=True)
for f in SRC.iterdir():
    (shutil.copytree(f, APP / f.name) if f.is_dir() else shutil.copy2(f, APP / f.name))
(APP / 'session').write_text('browser-session-2\n0\n1\n' + json.dumps(URL) + '\n0\n', encoding='utf-8', newline='\n')
sys.path.insert(0, str(ROOT / 'tests' / 'chat'))
import chat_qa as C   # noqa: E402  (the tutorial marked as seen: its veil never covers the page)
C.mark_tutorial_seen(APP)
with open(APP / 'settings', 'a', encoding='utf-8', newline='\n') as f: f.write(f'tabs-position={args.layout}\n')   # the layout under test
cfg = APP / 'annotations-chat'; cfg.mkdir(parents=True, exist_ok=True)
(cfg / 'projects.json').write_text(json.dumps({'v': 1, 'layout': 'comment', 'sites': {}}), encoding='utf-8')   # Mode Commentaire
for sub in ('AppData/Roaming/Claude', 'AppData/Local', 'home/.codex'): (AGENTS / sub).mkdir(parents=True, exist_ok=True)

# ---------------------------------------------------------------- the hidden desktop
class SI(ctypes.Structure):
    _fields_ = [('cb', W.DWORD), ('lpReserved', W.LPWSTR), ('lpDesktop', W.LPWSTR), ('lpTitle', W.LPWSTR), ('dwX', W.DWORD), ('dwY', W.DWORD),
                ('dwXSize', W.DWORD), ('dwYSize', W.DWORD), ('dwXCountChars', W.DWORD), ('dwYCountChars', W.DWORD), ('dwFillAttribute', W.DWORD),
                ('dwFlags', W.DWORD), ('wShowWindow', W.WORD), ('cbReserved2', W.WORD), ('lpReserved2', ctypes.c_void_p),
                ('hStdInput', W.HANDLE), ('hStdOutput', W.HANDLE), ('hStdError', W.HANDLE)]
class PI(ctypes.Structure):
    _fields_ = [('hProcess', W.HANDLE), ('hThread', W.HANDLE), ('dwProcessId', W.DWORD), ('dwThreadId', W.DWORD)]
class RECT(ctypes.Structure): _fields_ = [('l', W.LONG), ('t', W.LONG), ('r', W.LONG), ('b', W.LONG)]
def on_desk(fn):
    out = {}
    def body(): u32.SetThreadDesktop(W.HANDLE(hdesk)); out['r'] = fn()
    t = threading.Thread(target=body); t.start(); t.join(); return out.get('r')
def windows_of(pid, cls):
    found = []
    @ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    def cb(h, _):
        c = ctypes.create_unicode_buffer(64); u32.GetClassNameW(h, c, 64); p = W.DWORD(); u32.GetWindowThreadProcessId(h, ctypes.byref(p))
        if c.value == cls and p.value == pid: found.append(h)
        return True
    u32.EnumWindows(cb, 0); return found
def rect(h): r = RECT(); u32.GetWindowRect(h, ctypes.byref(r)); return (r.l, r.t, r.r, r.b)
def shown(cls):
    """(hwnd, rect) of our visible window of that class, else None."""
    pid = proc.dwProcessId
    def body():
        for h in windows_of(pid, cls):
            if u32.IsWindowVisible(h): return (h, rect(h))
        return None
    return on_desk(body)

proc = None; server = None
def launch():
    global proc, server
    server = subprocess.Popen([sys.executable, str(ROOT / 'tests' / 'annotations' / 'iframe-demo' / 'serve.py'), '--shop', str(SHOP), '--other', str(OTHER)],
                              cwd=str(WORK), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=0x08000000)
    env = dict(os.environ)
    for k in list(env):
        if k.upper().startswith(('BROWSER_CHAT_', 'BROWSER_ANNOTATIONS_', 'WEBVIEW2_')): env.pop(k)
    env['WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS'] = f'--remote-debugging-port={CDP_PORT} --remote-allow-origins=http://127.0.0.1:{CDP_PORT}'
    env['BROWSER_ANNOTATIONS_CONNECT_ROOT'] = str(AGENTS); env['BROWSER_ANNOTATIONS_TEST'] = '1'
    env['BROWSER_CHAT_FAKE_AGENT'] = f'"{FAKE}" --resume --claude-like --start-mode auto --log "{FLOG}"'
    home = BASE / 'home'
    for sub in ('AppData/Roaming', 'AppData/Local', '.local/bin'): (home / sub).mkdir(parents=True, exist_ok=True)
    env['USERPROFILE'] = str(home); env['HOMEDRIVE'] = str(home)[:2]; env['HOMEPATH'] = str(home)[2:]
    env['APPDATA'] = str(home / 'AppData' / 'Roaming'); env['LOCALAPPDATA'] = str(home / 'AppData' / 'Local')
    env['PATH'] = r'C:\Windows\System32;C:\Windows;C:\Windows\System32\WindowsPowerShell\v1.0'
    for k in [k for k in env if k.upper() in ('CLAUDE_CONFIG_DIR', 'CODEX_HOME', 'ANTHROPIC_API_KEY', 'OPENAI_API_KEY', 'NODE_OPTIONS')]: env.pop(k)
    # SAFETY, BEFORE launching: no real CLI reachable, the fake agent the one used, a test environment
    for n in ('claude', 'codex', 'node', 'npx', 'npm'):
        assert shutil.which(n, path=env['PATH']) is None, f'{n} reachable from the sealed PATH: refused'
        for d in (home / '.local' / 'bin', home / 'AppData' / 'Roaming' / 'npm'):
            assert not any((d / (n + x)).exists() for x in ('', '.exe', '.cmd')), f'{n} in the sealed home: refused'
    assert pathlib.Path(env['BROWSER_CHAT_FAKE_AGENT'].split('"')[1]).is_absolute() and FAKE.is_file(), 'fake agent path: refused'
    assert env['BROWSER_ANNOTATIONS_TEST'] == '1' and str(AGENTS).startswith(str(ROOT / 'build')), 'test environment: refused'
    block = ''.join(f'{k}={v}\0' for k, v in env.items()) + '\0'
    si = SI(); si.cb = ctypes.sizeof(si); si.lpDesktop = 'WinSta0\\' + DESK; si.dwFlags = 1; si.wShowWindow = 1   # normal (1180 x 740 or as restored)
    pi = PI()
    ok = k32.CreateProcessW(str(APP / 'browser.exe'), None, None, None, False, 0x00000400, ctypes.create_unicode_buffer(block), str(APP), ctypes.byref(si), ctypes.byref(pi))
    assert ok, ctypes.get_last_error()
    proc = pi; say(f'>>> browser started (pid {pi.dwProcessId}) on the hidden desktop')
    end = time.time() + 60
    while time.time() < end and not page_target(): time.sleep(0.2)
    assert page_target(), 'page target never came'
    say('>>> page target up')

def quit_browser():
    if not proc: return
    pid = proc.dwProcessId
    for h in on_desk(lambda: windows_of(pid, 'LisiereBrowser')) or []: u32.PostMessageW(h, 0x10, 0, 0)
    if k32.WaitForSingleObject(proc.hProcess, 20000) != 0: subprocess.run(['taskkill', '/T', '/F', '/PID', str(pid)], capture_output=True); say('>>> browser killed (did not close in 20 s)')
    else: say('>>> browser closed')
    if server: subprocess.run(['taskkill', '/T', '/F', '/PID', str(server.pid)], capture_output=True)

# ---------------------------------------------------------------- CDP and the log
def targets():
    try: return json.loads(urllib.request.urlopen(f'http://127.0.0.1:{CDP_PORT}/json', timeout=3).read())
    except Exception: return []
def page_target(): return next((t for t in targets() if t.get('type') == 'page' and t.get('url', '').startswith(URL)), None)
_ws = {}
def ws(t):
    if t['id'] not in _ws: _ws[t['id']] = Q.WS(t['webSocketDebuggerUrl'])
    return _ws[t['id']]
def ev(js):
    t = page_target()
    if not t: return None
    try:
        r = ws(t).call('Runtime.evaluate', {'expression': js, 'returnByValue': True, 'awaitPromise': True}); return r.get('result', {}).get('value')
    except Exception:
        _ws.pop(t['id'], None); return None
def move(x, y): ws(page_target()).call('Input.dispatchMouseEvent', {'type': 'mouseMoved', 'x': x, 'y': y})
def box(sel): return ev("(function(){var e=document.querySelector(%s);if(!e||document.readyState!=='complete')return null;var r=e.getBoundingClientRect();return [r.left,r.top,r.width,r.height,devicePixelRatio]})()" % json.dumps(sel))
def main_hwnd():
    hs = on_desk(lambda: windows_of(proc.dwProcessId, 'LisiereBrowser')) or []; return hs[0] if hs else None
def page_origin():
    """The page's top-left corner in screen pixels: its input window (Chrome_RenderWidgetHostHWND, the largest visible one)."""
    main = main_hwnd(); found = []
    def body():
        @ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
        def cb(h, _):
            c = ctypes.create_unicode_buffer(64); u32.GetClassNameW(h, c, 64)
            if c.value == 'Chrome_RenderWidgetHostHWND' and u32.IsWindowVisible(h):
                r = rect(h); found.append(((r[2] - r[0]) * (r[3] - r[1]), r))
            return True
        u32.EnumChildWindows(main, cb, 0)
    on_desk(body)
    return max(found)[1] if found else None
LOG = APP / 'prototype.log'; seen = 0
def pump_log():
    global seen
    try: ls = LOG.read_text(encoding='utf-8', errors='replace').splitlines()
    except OSError: return []
    new = ls[seen:]; seen = len(ls)
    for l in new:
        if l.startswith(('ANNOTATE', 'AGENTS', 'CHAT')): say('    ' + l[:200])
        if (re.match(r'AGENTS (claude|codex): ready \d', l) or 'runtime absent' in l or 'needs_prepare' in l or re.match(r'CHAT prepare ', l) or 'downloaded' in l):
            say('!!! NOT THE FAKE AGENT: ' + l[:160] + ' -> our browser is killed now')
            subprocess.run(['taskkill', '/T', '/F', '/PID', str(proc.dwProcessId)], capture_output=True)
            if server: subprocess.run(['taskkill', '/T', '/F', '/PID', str(server.pid)], capture_output=True)
            OUT.write_text('\n'.join(lines_out), encoding='utf-8'); os._exit(3)
    return new
def wait_log(pat, timeout):
    end = time.time() + timeout
    while time.time() < end:
        for l in pump_log():
            if re.search(pat, l): return True
        time.sleep(0.05)
    return False
def wait_for(fn, timeout):
    end = time.time() + timeout
    while time.time() < end:
        v = fn(); pump_log()
        if v: return v
        time.sleep(0.03)
    return fn()

try:
    launch()
    # the demo page loaded (its iframes too) BEFORE the mode starts, as a person would see it (the session restore may still
    # be on the initial empty document when the page target appears)
    assert wait_for(lambda: box('#pay'), 40), 'the demo page never loaded'
    time.sleep(0.5)
    u32.PostMessageW(main_hwnd(), 0x8000 + 1, 3060, 0)   # the browser's own Shortcut message: AnnotateCommand (Ctrl+Maj+A)
    check(wait_log(r'ANNOTATE mode on', 20), 'the annotation mode is on (the browser\'s command)')
    # the chat's warm-up may reload the page once (« mode re-armed after a reload »): measure once it has settled
    wait_log(r'mode re-armed after a reload', 6); time.sleep(1.0); pump_log()
    p = wait_for(lambda: box('#pay'), 40)
    if not p:
        t = page_target(); say(f'    target {t and t.get("url")}; readyState {ev("document.readyState")}; title {ev("document.title")}; html {ev("document.documentElement&&document.documentElement.outerHTML.slice(0,300)")}; server {len(urllib.request.urlopen(URL, timeout=3).read())} bytes;targets {[x.get("url") for x in targets()]}')
    assert p, 'the demo page never settled'
    dpi = u32.GetDpiForWindow(main_hwnd()) / 96.0
    org = page_origin(); k = p[4]
    say(f'    #pay {p[:4]} css px, page at {org}, dpr {k}, dpi {dpi}')
    move(p[0] + 120, p[1] + 90); time.sleep(0.05); move(p[0] + 130, p[1] + 100)
    card = wait_for(lambda: shown('BrowserAnnotationFrameCard'), 3)
    want_x = org[0] + round(p[0] * k); want_y = org[1] + round((p[1] + p[3]) * k) + round(12 * dpi)
    check(bool(card), 'over the iframe of another site: the card appears', card)
    if card:
        h, r = card
        check(abs(r[0] - want_x) <= 2 and abs(r[1] - want_y) <= 2 and abs((r[2] - r[0]) - round(300 * dpi)) <= 1,
              'the card under the iframe: its left on the iframe\'s left, its top 12 DIP under its bottom, 300 DIP wide', {'card': r, 'want': (want_x, want_y)})
        ex = u32.GetWindowLongPtrW(h, -20) if hasattr(u32, 'GetWindowLongPtrW') else u32.GetWindowLongW(h, -20)
        check((ex & 0x08000000) != 0, 'the card never takes the keyboard (WS_EX_NOACTIVATE)', hex(ex))
    check(wait_log(r'frame card shown under the iframe of another site', 2) or any('frame card shown under' in l for l in lines_out), 'the log says « under »')
    # the label's JetBrains Mono (Paper 26EP-0) is loaded in this page (the page script's embedded face), and what it draws
    fonts = ev("(function(){var a=[];document.fonts.forEach(function(f){if(f.family.indexOf('Browser Annotation')>=0)a.push(f.family+' '+f.weight+' '+f.status)});"
               "return {mono:document.fonts.check('10px \"Browser Annotation Mono\"'),faces:a}})()")
    check(bool(fonts) and fonts.get('mono') and any('Mono' in f and f.endswith('loaded') for f in fonts.get('faces', [])),
          'the label\'s JetBrains Mono is loaded in the page of the real browser (no fallback)', fonts)
    try:
        import base64
        t = page_target(); shot = ws(t).call('Page.captureScreenshot', {'format': 'png', 'clip': {'x': p[0] - 10, 'y': p[1] - 30, 'width': 340, 'height': 90, 'scale': 1}})
        (BASE / f'label-{args.layout}.png').write_bytes(base64.b64decode(shot['data'])); say(f'    capture: {BASE / f"label-{args.layout}.png"}')
    except Exception as e:
        say(f'    capture failed: {e}')
    t0 = time.perf_counter(); move(40, 20)   # the shop's header: an element of the page
    left = wait_log(r'frame hover left: the card goes in 300 ms', 4); t1 = time.perf_counter()
    hid = wait_log(r'frame card hidden \(the pointer left', 4); t2 = time.perf_counter()
    gone = wait_for(lambda: shown('BrowserAnnotationFrameCard') is None, 2); t3 = time.perf_counter()
    check(left and hid and gone, 'the pointer onto the page: the card goes (after its grace)',
          f'leave seen {"%.0f" % ((t1 - t0) * 1000)} ms, hidden {"%.0f" % ((t2 - t0) * 1000)} ms, window gone {"%.0f" % ((t3 - t0) * 1000)} ms after the move')
    mark = len(lines_out); s = box('#ship'); move(s[0] + 40, s[1] + 40); time.sleep(0.2); move(s[0] + 60, s[1] + 70); time.sleep(0.6); pump_log()
    check(shown('BrowserAnnotationFrameCard') is None and not any('frame card shown' in l for l in lines_out[mark:]), 'over the iframe of the same site: no card')
    move(p[0] + 100, p[1] + 60)
    card = wait_log(r'frame card shown under', 4) and wait_for(lambda: shown('BrowserAnnotationFrameCard'), 2)
    check(bool(card), 'back over the iframe of another site: the card again')
    if card:
        u32.PostMessageW(card[0], 0x8000 + 1, 2, 0)   # « En savoir plus » (the card's Invoke path: its click without the hit test)
        help_ = wait_for(lambda: shown('BrowserAnnotationFrameHelp'), 3)
        check(bool(help_), '« En savoir plus »: the help panel opens', help_)
        if help_:
            time.sleep(0.6)
            mr = rect(main_hwnd()); cr = RECT(); u32.GetClientRect(main_hwnd(), ctypes.byref(cr)); pt = W.POINT(cr.r, cr.b); u32.ClientToScreen(main_hwnd(), ctypes.byref(pt))
            hr = rect(help_[0])
            if args.layout != 'top':   # #143: docked on the left, against the side panel (its right edge = the page's left edge)
                check(abs(hr[0] - org[0]) <= 2 and abs((hr[2] - hr[0]) - round(332 * dpi)) <= 1 and abs(hr[3] - pt.y) <= 1,
                      f'the panel at the drawers\' place in the « {args.layout} » layout: docked on the LEFT against the side panel (the page\'s left edge), 332 DIP wide, down to the bottom',
                      {'panel': hr, 'page left': org[0], 'client bottom': pt.y})
            else: check(abs(hr[2] - pt.x) <= 1 and abs((hr[2] - hr[0]) - round(332 * dpi)) <= 1 and abs(hr[3] - pt.y) <= 1,
                  'the panel at the drawers\' place: the window\'s right edge, 332 DIP wide, down to the bottom', {'panel': hr, 'client right/bottom': (pt.x, pt.y)})
            check(shown('BrowserAnnotationFrameCard') is None, 'the card goes when the panel opens')
            u32.PostMessageW(help_[0], 0x0100, 0x1B, 0); u32.PostMessageW(help_[0], 0x0101, 0x1B, 0)   # Échap
            check(wait_for(lambda: shown('BrowserAnnotationFrameHelp') is None, 3), 'Échap closes the panel')
            check(wait_log(r'frame help panel closed', 2) or any('frame help panel closed' in l for l in lines_out), 'the log says it closed')
    move(40, 20); wait_for(lambda: shown('BrowserAnnotationFrameCard') is None, 3); move(p[0] + 110, p[1] + 70)
    card = wait_log(r'frame card shown under', 4) and wait_for(lambda: shown('BrowserAnnotationFrameCard'), 2)
    if check(bool(card), 'the card once more (for « Annoter le cadre »)'):
        u32.PostMessageW(card[0], 0x8000 + 1, 1, 0)   # « Annoter le cadre »
        check(wait_log(r'« select » handled', 5), '« Annoter le cadre »: the iframe is selected (« select » handled)')
        check(wait_for(lambda: shown('BrowserAnnotationFrameCard') is None, 2), 'the card goes with the selection')
        sel = [l for l in LOG.read_text(encoding='utf-8', errors='replace').splitlines() if 'Annoter le cadre' in l]
        check(bool(sel), 'the log names the card\'s button', sel[-1:] if sel else '')
finally:
    try: quit_browser()
    except Exception as e: say(f'quit: {e}')
    OUT.write_text('\n'.join(lines_out), encoding='utf-8')
say(f'{len(failures)} failure(s)' + (': ' + '; '.join(failures) if failures else ''))
sys.exit(1 if failures else 0)
