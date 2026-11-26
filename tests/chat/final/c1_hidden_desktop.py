"""Journeys C1, replayed on a never-displayed Windows desktop: nothing on the shared screen, no SendInput, no real agent.

Found on 25 Nov 2026 (« Ton agent », PR #145; Fix's release check: the first click after the mode's start opened the
link; a bubble without its conversation). Two cases, both reproduced here and fixed:
  --ctrlmove cdp   the page script believed Ctrl held from ONE pointer event's Ctrl flag (the journey's Ctrl+Maj+A is a key
                   state only, the page moved under a still pointer); no key, no host reading confirmed it, no move came
                   after: the next click went to the site (src/annotation-script.h, staleCtrl). Lot R alone hid it: its
                   « Connecter un agent » window opened at every start and its focus change reset Ctrl.
  --warm 4000      a click before the chat's page exists (its warm-up held back): the 2 s safety showed an EMPTY bubble
                   when the page took longer (src/chat-comment.h, BubblePageWaitMs).
Part 1 (unless --keep): a fresh copy, the mode on (the browser's own command), a click on a.btn-primary, a message with a
token, its turn ends, the browser quits. Part 2, once per --delays value: the SAME profile relaunched, the mode on, the
click on a.btn-primary, then every ~25 ms for 4 s: did the page navigate (the click went through)? is the bubble shown
(non-empty region)? does its page hold the token (the element's conversation)? Exit code 1 on either failure.

    python -X utf8 -I tests/chat/final/c1_hidden_desktop.py <build-dir> <label> [--keep] [--native] [--ctrlmove cdp]
           [--warm 4000] [--delays 0.3,1.5] [--journey] [--forget-models] [--first N] [--ports 5193,9357]

<build-dir> holds browser.exe (its fake agent in <build-dir>-test/), or app/ and app-test/. Its copy, profile and log:
build/c1-run-<label>/ (replay.txt: every line timed).
SAFETY (incident of 25 Nov 2026: a relative fake-agent path let the REAL agent run with the owner's account): before any
launch the fake agent is an absolute existing path, the home is sealed (USERPROFILE / APPDATA / LOCALAPPDATA under the
copy, PATH = Windows only: no real CLI, no sign-in reachable) and checked; during the run any sign of a real agent (a CLI
version, a runtime to prepare, a download) kills our browser at once. Our own processes only, stopped by PID; ports of our
own (the journeys use 5183 / 9347). It shows nothing on screen (hidden desktop)."""
import argparse, ctypes, ctypes.wintypes as W, json, os, pathlib, shutil, subprocess, sys, threading, time, urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tests' / 'annotations'))
import sandbox_qa as Q   # noqa: E402  (CDP WebSocket)

ap = argparse.ArgumentParser()
ap.add_argument('build'); ap.add_argument('label')
ap.add_argument('--warm', type=int, default=0)
ap.add_argument('--delays', default='0.3')
ap.add_argument('--ports', default='5193,9357')
ap.add_argument('--keep', action='store_true', help='part 2 only (the profile of a previous part 1)')
ap.add_argument('--ctrlmove', nargs='?', const='native', default=None, help='part 2: one Ctrl-flagged pointer move over the element, then the click without a move')
ap.add_argument('--first', type=int, default=0, help='N fresh launches, the first click of each (the journey\'s part 1)')
ap.add_argument('--forget-models', action='store_true', help='remove agent-options.json before each part 2 (the model read runs)')
ap.add_argument('--native', action='store_true', help='clicks posted to the page\'s Win32 input window (not CDP)')
ap.add_argument('--journey', action='store_true', help="part 2 as the journey: UIA « Plus tard » / close, « Commentaire », its 5 s wait")
args = ap.parse_args()
BUILD = pathlib.Path(args.build).resolve(); SRC = BUILD if (BUILD / 'browser.exe').is_file() else BUILD / 'app'
FAKE = next(p for p in (BUILD.parent / (BUILD.name + '-test') / 'fake-acp-agent.exe', BUILD / 'app-test' / 'fake-acp-agent.exe') if p.is_file()).resolve()
assert FAKE.is_absolute() and FAKE.is_file(), 'the fake agent must be an absolute, existing path (the browser runs from its own folder)'
PAGE_PORT, CDP_PORT = (int(x) for x in args.ports.split(','))
BASE = ROOT / 'build' / f'c1-run-{args.label}'
APP, WORK = BASE / 'app', BASE / 'work'; SITE = WORK / 'site'; AGENTS = WORK / 'agent-configs'; FLOG = WORK / 'fake.log'
URL = f'http://127.0.0.1:{PAGE_PORT}/'
OUT = BASE / 'replay.txt'
k32 = ctypes.WinDLL('kernel32', use_last_error=True); u32 = ctypes.WinDLL('user32', use_last_error=True)
u32.CreateDesktopW.restype = W.HANDLE
DESK = f'c1-{args.label}'
hdesk = u32.CreateDesktopW(DESK, None, None, 0, 0x10000000, None)
lines_out = []
T0 = time.perf_counter()
def say(s): t = time.perf_counter() - T0; lines_out.append(f'{t:8.3f}  {s}'); print(f'{t:8.3f}  {s}', flush=True)

# ---------------------------------------------------------------- the copy
def copy_app(fresh):
    APP.mkdir(parents=True, exist_ok=True); WORK.mkdir(parents=True, exist_ok=True)
    keep = ('profile', 'session', 'sessions', 'annotations', 'prototype.log', 'downloads', 'bookmarks', 'settings', 'history',
            'annotations-connect', 'annotations-chat', 'chat-snapshots', 'agents', 'profiles', 'profiles.txt', 'favicons', 'extensions')
    if fresh:
        for n in keep:
            p = APP / n
            shutil.rmtree(p, ignore_errors=True) if p.is_dir() else (p.unlink() if p.exists() else None)
    for f in SRC.iterdir():
        if f.name in keep: continue
        (shutil.copytree(f, APP / f.name, dirs_exist_ok=True) if f.is_dir() else shutil.copy2(f, APP / f.name))
    for n in ('prototype.log',):
        try: (APP / n).unlink()
        except OSError: pass
    shutil.rmtree(APP / 'sessions', ignore_errors=True)
    (APP / 'session').write_text('browser-session-2\n0\n1\n' + json.dumps(URL + 'landing.html') + '\n0\n', encoding='utf-8', newline='\n')
    sys.path.insert(0, str(ROOT / 'tests' / 'chat'))
    import chat_qa as C   # the tutorial marked as seen (its veil never covers the page)
    C.mark_tutorial_seen(APP)
    if fresh:
        shutil.rmtree(SITE, ignore_errors=True); shutil.copytree(ROOT / 'sandbox' / 'chat' / 'site', SITE)
        cfg = APP / 'annotations-chat'; cfg.mkdir(parents=True, exist_ok=True)
        (cfg / 'projects.json').write_text(json.dumps({'v': 1, 'layout': 'comment', 'sites': {}}), encoding='utf-8')   # Mode Commentaire, as the journeys
    for sub in ('AppData/Roaming/Claude', 'AppData/Local', 'home/.codex'):
        (AGENTS / sub).mkdir(parents=True, exist_ok=True)

# ---------------------------------------------------------------- the hidden desktop
class SI(ctypes.Structure):
    _fields_ = [('cb', W.DWORD), ('lpReserved', W.LPWSTR), ('lpDesktop', W.LPWSTR), ('lpTitle', W.LPWSTR), ('dwX', W.DWORD), ('dwY', W.DWORD),
                ('dwXSize', W.DWORD), ('dwYSize', W.DWORD), ('dwXCountChars', W.DWORD), ('dwYCountChars', W.DWORD), ('dwFillAttribute', W.DWORD),
                ('dwFlags', W.DWORD), ('wShowWindow', W.WORD), ('cbReserved2', W.WORD), ('lpReserved2', ctypes.c_void_p),
                ('hStdInput', W.HANDLE), ('hStdOutput', W.HANDLE), ('hStdError', W.HANDLE)]
class PI(ctypes.Structure):
    _fields_ = [('hProcess', W.HANDLE), ('hThread', W.HANDLE), ('dwProcessId', W.DWORD), ('dwThreadId', W.DWORD)]
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
class RECT(ctypes.Structure): _fields_ = [('l', W.LONG), ('t', W.LONG), ('r', W.LONG), ('b', W.LONG)]
gdi = ctypes.WinDLL('gdi32')
def region_box(h):
    rg = gdi.CreateRectRgn(0, 0, 0, 0); kind = u32.GetWindowRgn(h, rg); r = RECT(); gdi.GetRgnBox(rg, ctypes.byref(r)); gdi.DeleteObject(rg)
    return None if kind == 0 else (r.l, r.t, r.r, r.b)

proc = None; server = None
def launch(extra=None):
    global proc, server
    server = subprocess.Popen([sys.executable, str(ROOT / 'sandbox' / 'chat' / 'serve.py'), '--root', str(SITE), '--port', str(PAGE_PORT)],
                              cwd=str(SITE), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=0x08000000)
    env = dict(os.environ)
    for k in list(env):
        if k.upper().startswith(('BROWSER_CHAT_', 'BROWSER_ANNOTATIONS_', 'WEBVIEW2_')): env.pop(k)
    env['WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS'] = f'--remote-debugging-port={CDP_PORT} --remote-allow-origins=http://127.0.0.1:{CDP_PORT}'
    env['BROWSER_ANNOTATIONS_CONNECT_ROOT'] = str(AGENTS); env['BROWSER_ANNOTATIONS_TEST'] = '1'
    env['BROWSER_CHAT_PANEL_IDLE_MS'] = '60000'; env['BROWSER_CHAT_AGENT_IDLE_MS'] = '600000'
    env['BROWSER_CHAT_FAKE_AGENT'] = f'"{FAKE}" --resume --claude-like --start-mode auto --log "{FLOG}"'
    env.update(extra or {})
    # SAFETY, BEFORE launching (rule of 25 Nov): a sealed home — the real Claude / Codex CLIs and their sign-ins cannot be
    # reached (USERPROFILE / APPDATA / LOCALAPPDATA under our copy, PATH = Windows only), and the fake agent is the one used.
    home = BASE / 'home'
    for sub in ('AppData/Roaming', 'AppData/Local', '.local/bin'): (home / sub).mkdir(parents=True, exist_ok=True)
    env['USERPROFILE'] = str(home); env['HOMEDRIVE'] = str(home)[:2]; env['HOMEPATH'] = str(home)[2:]
    env['APPDATA'] = str(home / 'AppData' / 'Roaming'); env['LOCALAPPDATA'] = str(home / 'AppData' / 'Local')
    env['PATH'] = r'C:\Windows\System32;C:\Windows;C:\Windows\System32\WindowsPowerShell\v1.0'
    for k in [k for k in env if k.upper() in ('CLAUDE_CONFIG_DIR', 'CODEX_HOME', 'ANTHROPIC_API_KEY', 'OPENAI_API_KEY', 'NODE_OPTIONS')]: env.pop(k)
    for n in ('claude', 'codex', 'node', 'npx', 'npm'):
        assert shutil.which(n, path=env['PATH']) is None, f'{n} reachable from the sealed PATH: refused'
        for d in (home / '.local' / 'bin', home / 'AppData' / 'Roaming' / 'npm'):
            assert not any((d / (n + x)).exists() for x in ('', '.exe', '.cmd')), f'{n} in the sealed home: refused'
    assert pathlib.Path(env['BROWSER_CHAT_FAKE_AGENT'].split('"')[1]).is_absolute() and FAKE.is_file(), 'fake agent path: refused'
    assert env['BROWSER_ANNOTATIONS_TEST'] == '1' and str(AGENTS).startswith(str(ROOT / 'build')), 'test environment: refused'
    block = ''.join(f'{k}={v}\0' for k, v in env.items()) + '\0'
    si = SI(); si.cb = ctypes.sizeof(si); si.lpDesktop = 'WinSta0\\' + DESK; si.dwFlags = 1; si.wShowWindow = 3   # maximized, as a user window
    pi = PI()
    ok = k32.CreateProcessW(str(APP / 'browser.exe'), None, None, None, False, 0x00000400, ctypes.create_unicode_buffer(block), str(APP), ctypes.byref(si), ctypes.byref(pi))
    assert ok, ctypes.get_last_error()
    proc = pi; say(f'>>> browser started (pid {pi.dwProcessId})')
    end = time.time() + 60
    while time.time() < end:
        if page_target(): break
        time.sleep(0.2)
    assert page_target(), 'page target never came'
    say('>>> page target up')

def quit_browser():
    pid = proc.dwProcessId
    hs = on_desk(lambda: windows_of(pid, 'LisiereBrowser')) or []
    for h in hs: u32.PostMessageW(h, 0x10, 0, 0)
    r = k32.WaitForSingleObject(proc.hProcess, 20000)
    if r != 0: subprocess.run(['taskkill', '/T', '/F', '/PID', str(pid)], capture_output=True); say('>>> browser killed (did not close in 20 s)')
    else: say('>>> browser closed')
    k32.CloseHandle(proc.hProcess); k32.CloseHandle(proc.hThread)
    subprocess.run(['taskkill', '/T', '/F', '/PID', str(server.pid)], capture_output=True)
    globals()['proc'] = None; globals()['server'] = None   # never a PID again once ours ended (it may be reused)
    time.sleep(1.0)

# ---------------------------------------------------------------- CDP
def targets():
    try: return json.loads(urllib.request.urlopen(f'http://127.0.0.1:{CDP_PORT}/json', timeout=3).read())
    except Exception: return []
def page_target(): return next((t for t in targets() if t.get('type') == 'page' and t.get('url', '').startswith(URL) and 'landing' in t.get('url', '') or (t.get('type') == 'page' and t.get('url', '').startswith(URL + 'index'))), None)
_ws = {}
def ws_of(t):
    if t['id'] not in _ws: _ws[t['id']] = Q.WS(t['webSocketDebuggerUrl'])
    return _ws[t['id']]
def ev(t, js):
    try:
        r = ws_of(t).call('Runtime.evaluate', {'expression': js, 'returnByValue': True})
        return r.get('result', {}).get('value')
    except Exception as e:
        _ws.pop(t['id'], None); return {'error': str(e)[:120]}
def chat_targets(): return [t for t in targets() if t.get('type') == 'page' and t.get('url', '').startswith('https://chat.browser.internal')]
def main_hwnd():
    pid = proc.dwProcessId; hs = on_desk(lambda: windows_of(pid, 'LisiereBrowser')) or []; return hs[0] if hs else None
def bubble_hwnd():
    pid = proc.dwProcessId; hs = on_desk(lambda: windows_of(pid, 'BrowserChatBubble')) or []; return hs[0] if hs else None
def command(cmd_id): h = main_hwnd(); u32.PostMessageW(h, 0x8000 + 1, cmd_id, 0)
def uia_invoke(aid):
    """tests/annotations/uia.ps1, started ON the hidden desktop (UI Automation sees only the windows of its own desktop)."""
    out = BASE / f'uia-{aid}.txt'
    try: out.unlink()
    except OSError: pass
    ps = str(ROOT / 'tests' / 'annotations' / 'uia.ps1')
    cmd = (f'powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "& \'{ps}\' -ProcessId {proc.dwProcessId} -Id {aid} -Invoke | '
           f'Out-File -Encoding utf8 \'{out}\'"')
    si = SI(); si.cb = ctypes.sizeof(si); si.lpDesktop = 'WinSta0\\' + DESK; pi = PI()
    ok = k32.CreateProcessW(None, ctypes.create_unicode_buffer(cmd), None, None, False, 0x08000000, None, str(BASE), ctypes.byref(si), ctypes.byref(pi))
    if not ok: return f'START FAILED {ctypes.get_last_error()}'
    k32.WaitForSingleObject(pi.hProcess, 60000); k32.CloseHandle(pi.hProcess); k32.CloseHandle(pi.hThread)
    try: return out.read_text(encoding='utf-8-sig').strip()
    except OSError: return 'NO OUTPUT'
def render_hwnd():
    """The page's own input window (Chrome_RenderWidgetHostHWND under our main window, the largest one: the tab's page)."""
    main = main_hwnd(); found = []
    def body():
        @ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
        def cb(h, _):
            c = ctypes.create_unicode_buffer(64); u32.GetClassNameW(h, c, 64)
            if c.value == 'Chrome_RenderWidgetHostHWND' and u32.IsWindowVisible(h):
                r = RECT(); u32.GetWindowRect(h, ctypes.byref(r)); found.append(((r.r - r.l) * (r.b - r.t), h))
            return True
        u32.EnumChildWindows(main, cb, 0)
    on_desk(body)
    return max(found)[1] if found else None
def click_native(sel):
    """A click through the page's Win32 input window (posted WM_MOUSEMOVE / LBUTTONDOWN / UP: Chromium's own mouse path,
    its modifiers read from the key state) — the closest to a real click a never-displayed desktop allows."""
    t = page_target()
    r = ev(t, "(function(){var e=document.querySelector(%s);var r=e.getBoundingClientRect();return [r.left+r.width/2,r.top+r.height/2,devicePixelRatio]})()" % json.dumps(sel))
    x, y, k = r; h = render_hwnd()
    lp = (int(y * k) << 16) | (int(x * k) & 0xFFFF)
    if not args.ctrlmove: u32.PostMessageW(h, 0x0200, 0, lp); time.sleep(0.02)   # --ctrlmove: the pointer is already there (SetCursorPos to the same point: no move)
    u32.PostMessageW(h, 0x0201, 0x0001, lp); time.sleep(0.03)
    u32.PostMessageW(h, 0x0202, 0, lp)
    return x, y
def click_element(sel):
    if args.native: return click_native(sel)
    t = page_target(); w = ws_of(t)
    r = ev(t, "(function(){var e=document.querySelector(%s);var r=e.getBoundingClientRect();return [r.left+r.width/2,r.top+r.height/2]})()" % json.dumps(sel))
    x, y = r
    w.call('Input.dispatchMouseEvent', {'type': 'mouseMoved', 'x': x, 'y': y})
    w.call('Input.dispatchMouseEvent', {'type': 'mousePressed', 'x': x, 'y': y, 'button': 'left', 'clickCount': 1, 'buttons': 1})
    w.call('Input.dispatchMouseEvent', {'type': 'mouseReleased', 'x': x, 'y': y, 'button': 'left', 'clickCount': 1, 'buttons': 0})
    return x, y

# ---------------------------------------------------------------- the log, timed
LOG = APP / 'prototype.log'; seen = 0
def pump_log():
    global seen
    try: ls = LOG.read_text(encoding='utf-8', errors='replace').splitlines()
    except OSError: return []
    new = ls[seen:]; seen = len(ls)
    import re as _re
    for l in new:
        if l.startswith(('CHAT', 'ANNOTATE', 'AGENTS', 'NAVIGATION', 'START')): say('    ' + l[:220])
        # SAFETY: only the fake agent, ever. A real CLI (its version shown), a runtime to prepare, a download: our browser
        # is killed at once and the script stops (incident of 25 Nov 12:56: a relative fake path let the real agent run).
        if (_re.match(r'AGENTS (claude|codex): ready \d', l) or 'runtime absent' in l or 'needs_prepare' in l or _re.match(r'CHAT prepare ', l)
                or 'downloaded' in l):
            say('!!! NOT THE FAKE AGENT: ' + l[:160] + ' -> our browser is killed now')
            if proc: subprocess.run(['taskkill', '/T', '/F', '/PID', str(proc.dwProcessId)], capture_output=True)
            if server: subprocess.run(['taskkill', '/T', '/F', '/PID', str(server.pid)], capture_output=True)
            OUT.write_text(chr(10).join(lines_out), encoding='utf-8'); os._exit(3)
    return new
def wait_log(pat, timeout):
    import re
    end = time.time() + timeout
    while time.time() < end:
        for l in pump_log():
            if re.search(pat, l): return True
        time.sleep(0.05)
    return False

import atexit
def _cleanup():
    try:
        if proc: subprocess.run(['taskkill', '/T', '/F', '/PID', str(proc.dwProcessId)], capture_output=True)
    except Exception: pass
    try:
        if server: subprocess.run(['taskkill', '/T', '/F', '/PID', str(server.pid)], capture_output=True)
    except Exception: pass
    OUT.write_text(chr(10).join(lines_out), encoding='utf-8')
atexit.register(_cleanup)
# ---------------------------------------------------------------- the journey's FIRST part only, N fresh launches (rel2's 01 A)
if args.first:
    outcome = []
    for n in range(args.first):
        copy_app(True)
        try: (APP / 'annotations-chat' / 'projects.json').unlink()   # no layout chosen yet: lot R opens the panel with the mode (Panneau)
        except OSError: pass
        seen = 0; launch(); time.sleep(1.5); pump_log()
        command(3060); say('>>> AnnotateCommand (mode on)'); wait_log(r'ANNOTATE mode on', 10); time.sleep(1.2)   # qa_final.mode_on, settle
        r = uia_invoke('AnnotationConnectLater'); say(f'>>> UIA AnnotationConnectLater: {r}')
        if not r.startswith('INVOKED'): r = uia_invoke('AnnotationConnectClose'); say(f'>>> UIA AnnotationConnectClose: {r}')
        if r.startswith('INVOKED'): time.sleep(0.6)
        r = uia_invoke('AnnotationLayoutComment'); say(f'>>> UIA AnnotationLayoutComment: {r}')
        wait_log(r'CHAT layout: Commentaire', 5); time.sleep(1.2)
        href0 = ev(page_target(), 'location.href')
        tc = time.perf_counter(); x, y = click_element('a.btn-primary'); say(f'>>> click a.btn-primary at {x:.0f},{y:.0f}')
        nav = None; sel = False
        while time.perf_counter() - tc < 3.0:
            for l in pump_log():
                if 'selecting' in l: sel = True
            pt = page_target(); href = ev(pt, 'location.href') if pt else None
            if href and href != href0 and nav is None: nav = time.perf_counter() - tc; say(f'>>> the page NAVIGATED to {href}: the click went through')
            time.sleep(0.05)
        v = 'WENT THROUGH' if nav is not None else ('captured' if sel else 'neither selected nor navigated')
        say(f'=== first click of launch {n + 1}: {v}'); outcome.append(v)
        quit_browser(); _ws.clear()
    print('RESULT first clicks:', outcome)
    u32.CloseDesktop(W.HANDLE(hdesk)); sys.exit(0)

# ---------------------------------------------------------------- part 1
if not args.keep:
    copy_app(True); launch()
    time.sleep(1.5); pump_log()
    command(3060); say('>>> AnnotateCommand (mode on)')
    assert wait_log(r'ANNOTATE mode on', 15), 'mode never on'
    wait_log(r'CHAT panel ready', 15); time.sleep(1.0)
    x, y = click_element('a.btn-primary'); say(f'>>> click a.btn-primary at {x:.0f},{y:.0f}')
    assert wait_log(r'CHAT bubble (opened|shown)', 10), 'no bubble'
    time.sleep(1.0)
    ct = None
    for t in chat_targets():
        if ev(t, "document.documentElement.classList.contains('bub')"): ct = t
    ct = ct or (chat_targets() or [None])[0]
    ev(ct, "(function(){var t=document.querySelector('[contenteditable=true]');t.focus();var s=getSelection();s.selectAllChildren(t);s.collapseToEnd();return 1})()")
    w = ws_of(ct); w.call('Input.insertText', {'text': 'reply jetonC1r'}); time.sleep(0.2)
    w.call('Input.dispatchKeyEvent', {'type': 'keyDown', 'key': 'Enter', 'code': 'Enter', 'windowsVirtualKeyCode': 13, 'nativeVirtualKeyCode': 13, 'text': '\r'})
    w.call('Input.dispatchKeyEvent', {'type': 'keyUp', 'key': 'Enter', 'code': 'Enter', 'windowsVirtualKeyCode': 13, 'nativeVirtualKeyCode': 13})
    say('>>> message sent')
    assert wait_log(r'CHAT turn t\d+ ended', 20), 'turn never ended'
    time.sleep(1.5); pump_log()
    quit_browser(); _ws.clear(); seen = 0

# ---------------------------------------------------------------- part 2, for each delay
results = []
for i, d in enumerate(float(x) for x in args.delays.split(',')):
    copy_app(False); seen = 0
    if args.forget_models:   # the agents' model lists forgotten: the background read of #128 v2 runs again in this launch
        try: (APP / 'annotations-chat' / 'agent-options.json').unlink(); say('>>> agent-options.json removed (the model read will run)')
        except OSError: pass
    launch({'BROWSER_CHAT_TEST_WARM_DELAY_MS': str(args.warm)} if args.warm else None)
    time.sleep(1.5); pump_log()
    tmode = time.perf_counter(); command(3060); say(f'>>> AnnotateCommand (mode on), click in {d} s' + (' after the journey\'s steps' if args.journey else ''))
    if args.journey:   # qa_final.mode_on('comment', settle=False), step by step
        wait_log(r'ANNOTATE mode on', 10); time.sleep(0.3)
        r = uia_invoke('AnnotationConnectLater'); say(f'>>> UIA AnnotationConnectLater: {r}')
        if not r.startswith('INVOKED'): r = uia_invoke('AnnotationConnectClose'); say(f'>>> UIA AnnotationConnectClose: {r}')
        if r.startswith('INVOKED'): time.sleep(0.2)
        r = uia_invoke('AnnotationLayoutComment'); say(f'>>> UIA AnnotationLayoutComment: {r}')
        if not wait_log(r'CHAT layout: Commentaire', 5): say('>>> (no « CHAT layout: Commentaire » line in 5 s: the journey waits that long)')
        time.sleep(0.05); tmode = time.perf_counter()
    while time.perf_counter() - tmode < d:
        pump_log(); time.sleep(0.02)
    href0 = ev(page_target(), 'location.href')
    if args.ctrlmove:   # what the journey's Ctrl+Maj+A leaves: ONE pointer move over the element carrying the Ctrl flag (the key
        # state said Ctrl down for 0.3 s while the band moved the page under a still cursor), then nothing until the click
        wait_log(r'CHAT panel ready', 8); time.sleep(1.0)
        t = page_target(); r = ev(t, "(function(){var e=document.querySelector('a.btn-primary');var r=e.getBoundingClientRect();return [r.left+r.width/2,r.top+r.height/2,devicePixelRatio]})()")
        ev(t, "window.__mv=[];addEventListener('pointermove',function(e){__mv.push([e.ctrlKey,e.isTrusted,Math.round(e.clientX),Math.round(e.clientY)])},true);"
              "window.__rules=function(){return document.adoptedStyleSheets.map(function(s){return s.cssRules.length})};1")
        say(f'>>> before: viseur sheets {ev(t, "__rules()")}')
        h = render_hwnd(); lp = (int(r[1] * r[2]) << 16) | (int(r[0] * r[2]) & 0xFFFF)
        if args.ctrlmove == 'cdp':
            ws_of(t).call('Input.dispatchMouseEvent', {'type': 'mouseMoved', 'x': r[0], 'y': r[1], 'modifiers': 2})
            say('>>> one pointer move over a.btn-primary WITH the Ctrl flag (CDP modifiers), Ctrl not held for the host')
        else:
            u32.PostMessageW(h, 0x0200, 0x0008, lp); say('>>> one pointer move over a.btn-primary WITH the Ctrl flag (MK_CONTROL), Ctrl not held for the host (GetAsyncKeyState up)')
        time.sleep(0.3); say(f'>>> page saw moves {ev(t, "__mv")}, viseur sheets now {ev(t, "__rules()")} (0 rules = the page script thinks Ctrl is held)')
        for _ in range(54): pump_log(); time.sleep(0.05)   # 3 s: the host's own Ctrl reading runs meanwhile
        say(f'>>> 3 s later: viseur sheets {ev(t, "__rules()")}')
    tc = time.perf_counter(); x, y = click_element('a.btn-primary'); say(f'>>> click a.btn-primary at {x:.0f},{y:.0f} ({tc - tmode:.2f} s after the mode command)')
    first_visible = None; nav = None; samples = []
    while time.perf_counter() - tc < 4.0:
        pump_log()
        pt = page_target(); href = ev(pt, 'location.href') if pt else None
        if href and href != href0 and nav is None: nav = time.perf_counter() - tc; say(f'>>> the page NAVIGATED to {href} ({nav:.3f} s after the click): the click went through')
        h = bubble_hwnd(); rb = region_box(h) if h else None; vis = bool(h and u32.IsWindowVisible(h) and rb and rb[2] - rb[0] > 40 and rb[3] - rb[1] > 30)
        txt = None
        if vis:
            for t in chat_targets():
                v = ev(t, "document.documentElement.classList.contains('bub') ? (document.body.textContent||'').replace(/\\s+/g,' ') : null")
                if isinstance(v, str): txt = v
            el = time.perf_counter() - tc
            if first_visible is None: first_visible = el; say(f'>>> bubble first seen visible {el:.3f} s after the click, token there: {bool(txt and "jetonC1r" in txt)}')
            samples.append((round(el, 3), bool(txt and 'jetonC1r' in txt), (txt or '')[:80]))
        time.sleep(0.025)
    pump_log()
    bad = [s for s in samples if not s[1]]
    verdict = ('WENT THROUGH' if nav is not None else 'captured') + f'; bubble seen {len(samples)} times, without the conversation {len(bad)} times' + (f' (first: {bad[0]})' if bad else '')
    say(f'=== delay {d}: {verdict}'); results.append((d, verdict))
    quit_browser(); _ws.clear()

OUT.write_text('\n'.join(lines_out), encoding='utf-8')
u32.CloseDesktop(W.HANDLE(hdesk))
print('\n'.join(f'RESULT delay {d}: {v}' for d, v in results))
bad = [v for _, v in results if v.startswith('WENT THROUGH') or ', without the conversation 0 times' not in v]
print('C1 replay: ' + (f'FAIL {len(bad)} / {len(results)}' if bad else f'{len(results)} / {len(results)} ok'))
sys.exit(1 if bad else 0)
