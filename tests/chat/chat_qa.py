#!/usr/bin/env python3
"""Chat intégré — real run on the chat sandbox with a COPY of build/chat-preview (build/chat-qa-app: its own profile,
settings, store, agents folder). Drives only that copy: the page and the chat panel through CDP (loopback debugging
port for this copy only), the browser's own shortcuts through messages posted to OUR window, captures through
PrintWindow. No SendInput, no other application touched. The agent is the FAKE ACP agent unless --real is given.

    python -I tests/chat/chat_qa.py launch [--real]     # start server + browser (leaves them running, pid files)
    python -I tests/chat/chat_qa.py page "<js>"         # evaluate in the sandbox page
    python -I tests/chat/chat_qa.py panel "<js>"        # evaluate in the chat panel
    python -I tests/chat/chat_qa.py key D               # Ctrl+Maj+<key> to the browser window
    python -I tests/chat/chat_qa.py shot <name>         # build/chat-qa/<name>.png (whole window)
    python -I tests/chat/chat_qa.py log [pattern]       # CHAT / ANNOTATE lines of the copy's log
    python -I tests/chat/chat_qa.py quit                # close the browser, stop the server
    python -I tests/chat/chat_qa.py run                 # the whole validation (design/chat-v1-validation.md)

Guards (same as tests/annotations): BROWSER_ANNOTATIONS_TEST=1, agent configurations redirected under build/, the real
Claude / Codex configuration files fingerprinted before and after.
"""
from __future__ import annotations
import ctypes
import hashlib
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import time
import urllib.request

# Per-monitor DPI aware (v2): window, region and screen coordinates are physical pixels, the same as the captures.
ctypes.windll.user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / 'tests' / 'annotations'))
import sandbox_qa as Q   # noqa: E402  (CDP WebSocket, window helpers, PrintWindow)

# CHAT_QA_BUILD=<folder under build/> runs the QA on another build than build/chat-preview (the user may be testing that one)
import os as _os
SRC = ROOT / 'build' / (_os.environ.get('CHAT_QA_BUILD') or 'chat-preview')
TOOLS = ROOT / 'build' / ((_os.environ.get('CHAT_QA_BUILD') or 'chat-preview') + '-test')
APP = ROOT / 'build' / 'chat-qa-app'
QA = ROOT / 'build' / 'chat-qa'
WORK = ROOT / 'build' / 'chat-qa-work'
SITE = WORK / 'site'
AGENTS = WORK / 'agent-configs'
LOG = APP / 'prototype.log'
PORT_PAGE = 5183
URL = f'http://127.0.0.1:{PORT_PAGE}/'
Q.SRC, Q.APP, Q.QA, Q.PORT, Q.URL = SRC, APP, QA, 9347, URL
PANEL = 'https://chat.browser.internal'
# What the fake agent really does to the site copy for an ordinary message (tests/acp/fake_agent.cpp, « apply »).
FAKE_EDIT = 'styles/checkout.css|margin-top: 4px;|margin-top: 4px; margin-left: auto;'
REAL_CONFIGS = [pathlib.Path(os.environ['APPDATA']) / 'Claude' / 'claude_desktop_config.json',
                pathlib.Path(os.environ['USERPROFILE']) / '.codex' / 'config.toml',
                pathlib.Path(os.environ['USERPROFILE']) / '.claude' / 'settings.json']
user32 = ctypes.windll.user32


def objective(oid, ok, value='', label=''):
    """An OBJECTIVE, not a release blocker (owner's decision 5 of 24 Nov 2026, design/annotation-plan-2026-11-24.md §4.1):
    the bubble's opening under 150 ms (`ouverture-150ms`) and its scroll tracking within 2 px (`suivi-2px`). Measured and
    said at every run — one line the general test (tests/annotation-general/run.py) reads — and never a failed step."""
    print(f"OBJECTIF {oid} {'atteint' if ok else 'manque'} | {label} | {value}", flush=True)
    return True


def fingerprints() -> dict:
    out = {}
    for f in REAL_CONFIGS:
        out[str(f)] = hashlib.sha256(f.read_bytes()).hexdigest() if f.is_file() else 'absent'
    return out


def targets():
    return json.loads(urllib.request.urlopen(f'http://127.0.0.1:{Q.PORT}/json', timeout=5).read())


def ws_for(prefix):
    for t in targets():
        if t.get('type') == 'page' and t.get('url', '').startswith(prefix):
            return Q.WS(t['webSocketDebuggerUrl'])
    return None


def evaluate(prefix, js):
    ws = ws_for(prefix)
    if ws is None:
        raise RuntimeError('no target ' + prefix)
    r = ws.call('Runtime.evaluate', {'expression': js, 'returnByValue': True, 'awaitPromise': True})
    if 'exceptionDetails' in r:
        raise RuntimeError(json.dumps(r['exceptionDetails'])[:600])
    return r.get('result', {}).get('value')


def page(js):
    return evaluate(URL, js)


def panel(js):
    return evaluate(PANEL, js)


def panel_exists():
    try:
        return ws_for(PANEL) is not None
    except Exception:
        return False


def click_page(selector):
    """A real click (trusted input through CDP) in the middle of an element of the page."""
    r = page(f"(function(){{var e=document.querySelector({json.dumps(selector)});if(!e)return null;var r=e.getBoundingClientRect();return [r.left+r.width/2,r.top+r.height/2];}})()")
    if not r:
        raise RuntimeError('no element ' + selector)
    ws = ws_for(URL)
    for kind in ('mouseMoved', 'mousePressed', 'mouseReleased'):
        ws.call('Input.dispatchMouseEvent', {'type': kind, 'x': r[0], 'y': r[1], 'button': 'none' if kind == 'mouseMoved' else 'left', 'clickCount': 0 if kind == 'mouseMoved' else 1})
    return r


def click_panel(selector=None, text=None):
    """A real click in the panel: on a selector, or on the visible control whose text / accessible name contains `text`
    (a button or role first, else the smallest element holding that text)."""
    if text == 'Autoriser' or (selector and '.btn.primary' in selector and 'perm' in selector):
        time.sleep(0.7)   # SEC-23: a permission card takes no « Autoriser » in its first 600 ms
    find = (f"document.querySelector({json.dumps(selector)})" if selector else
            "(function(){var t=" + json.dumps(text) + ";function vis(e){var r=e.getBoundingClientRect();return r.width>0&&r.height>0}"
            "function has(e){return ((e.innerText||'')+' '+(e.getAttribute('aria-label')||'')).indexOf(t)>=0}"
            "var c=Array.from(document.querySelectorAll('button,[role=button],[role=menuitem],[role=option],summary,a')).filter(function(e){return vis(e)&&has(e)});"
            "if(c.length)return c.sort(function(a,b){return a.innerText.length-b.innerText.length})[0];"
            "var all=Array.from(document.querySelectorAll('body *')).filter(function(e){return vis(e)&&(e.innerText||'').indexOf(t)>=0});"
            "return all.sort(function(a,b){return a.innerText.length-b.innerText.length})[0]||null})()")
    r = panel(f"(function(){{var e={find};if(!e)return null;e.scrollIntoView({{block:'nearest'}});var r=e.getBoundingClientRect();return [r.left+Math.min(r.width/2,60),r.top+r.height/2];}})()")
    if not r:
        raise RuntimeError(f'no panel element {selector or text}')
    ws = ws_for(PANEL)
    for kind in ('mouseMoved', 'mousePressed', 'mouseReleased'):
        ws.call('Input.dispatchMouseEvent', {'type': kind, 'x': r[0], 'y': r[1], 'button': 'none' if kind == 'mouseMoved' else 'left', 'clickCount': 0 if kind == 'mouseMoved' else 1})
    return r


def type_panel(text):
    ws = ws_for(PANEL)
    ws.call('Input.insertText', {'text': text})


def key_panel(key, code=None, vk=0, modifiers=0):
    ws = ws_for(PANEL)
    for kind in ('rawKeyDown', 'keyUp'):
        ws.call('Input.dispatchKeyEvent', {'type': kind, 'key': key, 'code': code or key, 'windowsVirtualKeyCode': vk, 'nativeVirtualKeyCode': vk, 'modifiers': modifiers})


def enter_panel():
    ws = ws_for(PANEL)
    ws.call('Input.dispatchKeyEvent', {'type': 'keyDown', 'key': 'Enter', 'code': 'Enter', 'windowsVirtualKeyCode': 13, 'nativeVirtualKeyCode': 13, 'text': '\r'})
    ws.call('Input.dispatchKeyEvent', {'type': 'keyUp', 'key': 'Enter', 'code': 'Enter', 'windowsVirtualKeyCode': 13, 'nativeVirtualKeyCode': 13})


# The annotation mode's tutorial (#125, « Bienvenue sur le mode Annotation ») opens the first time the mode is turned on
# in a profile, with a veil over the whole page: clicks, the wheel and the hover land on it. Every test profile is new, so
# it masked 13 real-window suites (first full pass of the general test, 24 Nov 2026). The test profiles start with it
# marked as seen — the product's own setting, `annotation-onboarded=1` in the profile's « settings » file. The tutorial
# itself is tested by tests/onboarding/ (its own harness), never through this one.
TUTORIAL_SEEN = 'annotation-onboarded=1'
SETTINGS_HEADER = 'browser-settings 1'   # src/settings.h, Read: a settings file is read only under this first line
TUTORIAL_OPENED = 'ANNOTATE onboarding opened'
_tutorial_said = False


def mark_tutorial_seen(app):
    '''Writes `annotation-onboarded=1` in the test profile's settings (the other settings keep their value, or their default).'''
    f = pathlib.Path(app) / 'settings'
    lines = [l for l in (f.read_text(encoding='utf-8', errors='replace').splitlines() if f.exists() else []) if not l.startswith('annotation-onboarded=')]
    if not lines or lines[0] != SETTINGS_HEADER:   # without its header line the browser takes every default: the tutorial opened anyway
        lines = [SETTINGS_HEADER] + [l for l in lines if l != SETTINGS_HEADER]
    f.write_text('\n'.join(lines + [TUTORIAL_SEEN]) + '\n', encoding='utf-8', newline='\n')


def tutorial_guard(text):
    '''The guard: the tutorial opened in a test profile although it was marked as seen — whatever the suite measures from
    here is the tutorial's veil, not the product. Said once, loudly, and the suite stops (exit code 1).'''
    global _tutorial_said
    if _tutorial_said or TUTORIAL_OPENED not in text:
        return
    _tutorial_said = True
    line = next((l for l in text.splitlines() if TUTORIAL_OPENED in l), TUTORIAL_OPENED)
    msg = ('HARNESS FAILURE — the annotation tutorial (#125) opened in this test profile: « ' + line.strip() + ' ». Its veil covers the page: '
           'clicks, scroll and hover land on it and nothing this suite measures is the product. The profile must start with ' + TUTORIAL_SEEN +
           ' in its « settings » file (tests/chat/chat_qa.py, mark_tutorial_seen); only tests/onboarding/ tests the tutorial.')
    print('FAIL ' + msg, flush=True)
    raise SystemExit(msg)


def log_text():
    try:
        text = LOG.read_text(encoding='utf-8', errors='replace')
    except OSError:
        return ''
    tutorial_guard(text)
    return text


def wait_log(pattern, timeout=15.0, after=0):
    end = time.time() + timeout
    while time.time() < end:
        m = re.search(pattern, log_text()[after:])
        if m:
            return m
        time.sleep(0.1)
    return None


def wait_for(fn, timeout=15.0, every=0.15):
    end = time.time() + timeout
    last = None
    while time.time() < end:
        try:
            last = fn()
            if last:
                return last
        except Exception as e:   # the target may not exist yet
            last = None
        time.sleep(every)
    return last


def shortcut(letter):
    Q.post_key(ord(letter.upper()), ctrl=True, shift=True)


def shot(name):
    QA.mkdir(parents=True, exist_ok=True)
    Q.pw_shot(name)
    return QA / f'{name}.png'


def grab(name):
    """The browser window as it is on screen (chrome included): brought to the front, then a screen capture of its
    rectangle. When the screen cannot be read (locked session), falls back to PrintWindow of our own windows."""
    import ctypes.wintypes as W
    QA.mkdir(parents=True, exist_ok=True)
    path = QA / f'{name}.png'
    try:
        from PIL import ImageGrab
        # Already in front (the browser or one of its own popups): no Alt tap. On an unlocked session that tap is a real
        # key press: it puts the window in the system-menu state and the next calls to the page time out.
        h = Q.main_window(); fg = user32.GetForegroundWindow(); pid = W.DWORD(); user32.GetWindowThreadProcessId(fg, ctypes.byref(pid))
        if not fg or pid.value != Q.our_pid():
            h = Q.front()
        time.sleep(0.35)
        r = W.RECT()
        if ctypes.windll.dwmapi.DwmGetWindowAttribute(h, 9, ctypes.byref(r), ctypes.sizeof(r)) != 0:   # DWMWA_EXTENDED_FRAME_BOUNDS
            user32.GetWindowRect(h, ctypes.byref(r))
        ImageGrab.grab(bbox=(r.left, r.top, r.right, r.bottom), all_screens=True).save(path)
    except Exception:
        h = Q.main_window()
        user32.RedrawWindow(h, None, None, 0x0001 | 0x0100 | 0x0080)   # RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN
        time.sleep(0.15)
        Q.pw_shot(name)
    return path


def post(message: dict):
    """A panel -> host message sent from the panel's own page (the same channel as its buttons)."""
    message = dict(message, v=1)
    return panel('(function(){window.chrome.webview.postMessage(' + json.dumps(message) + ');return true})()')


def uia(*args):
    r = subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(ROOT / 'tests' / 'annotations' / 'uia.ps1'),
                        '-ProcessId', str(Q.our_pid()), *args], capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=60)
    return r.stdout.strip()


def invoke(aid):
    out = uia('-Id', aid, '-Invoke')
    return out.startswith('INVOKED') or out.startswith('EXPANDED')


def panel_text():
    return panel('document.body.innerText') or ''


def prepare_site(reset=True):
    if reset and SITE.exists():
        shutil.rmtree(SITE)
    if not SITE.exists():
        shutil.copytree(ROOT / 'sandbox' / 'chat' / 'site', SITE)


def launch(real=False, fresh=True, extra_env=None, reset_site=True):
    APP.mkdir(parents=True, exist_ok=True); WORK.mkdir(parents=True, exist_ok=True)
    keep = ('profile', 'session', 'sessions', 'annotations', 'prototype.log', 'downloads', 'bookmarks', 'settings', 'history',
            'annotations-connect', 'annotations-chat', 'chat-snapshots', 'agents',
            'profiles', 'profiles.txt', 'favicons', 'extensions')   # the user's own profiles in build/chat-preview (made by the sandbox) never reach the copy
    for f in SRC.iterdir():
        if f.name in keep:
            continue
        dst = APP / f.name
        if f.is_dir():
            shutil.copytree(f, dst, dirs_exist_ok=True)
        else:
            shutil.copy2(f, dst)
    if fresh:
        for n in ('annotations', 'annotations-connect', 'annotations-chat', 'chat-snapshots', 'profiles'):
            shutil.rmtree(APP / n, ignore_errors=True)
        for n in ('settings', 'prototype.log', 'profiles.txt'):
            try:
                (APP / n).unlink()
            except OSError:
                pass
    shutil.rmtree(APP / 'sessions', ignore_errors=True)
    (APP / 'session').write_text('browser-session-2\n0\n1\n' + json.dumps(URL) + '\n0\n', encoding='utf-8', newline='\n')
    global _tutorial_said
    mark_tutorial_seen(APP); _tutorial_said = False   # #125: the tutorial's veil never covers a test's page (see mark_tutorial_seen)
    prepare_site(reset_site)
    for sub in ('AppData/Roaming/Claude', 'AppData/Local', 'home/.codex'):
        (AGENTS / sub).mkdir(parents=True, exist_ok=True)
    server = subprocess.Popen([sys.executable, str(ROOT / 'sandbox' / 'chat' / 'serve.py'), '--root', str(SITE), '--port', str(PORT_PAGE)],
                              cwd=str(SITE), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    (WORK / 'server.pid').write_text(str(server.pid))
    env = dict(os.environ)
    env['WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS'] = f'--remote-debugging-port={Q.PORT} --remote-allow-origins=http://127.0.0.1:{Q.PORT}'
    env['BROWSER_ANNOTATIONS_CONNECT_ROOT'] = str(AGENTS)
    env['BROWSER_ANNOTATIONS_TEST'] = '1'
    env.pop('BROWSER_ANNOTATIONS_CONNECT_PATH', None)
    env.pop('BROWSER_CHAT_FAKE_AGENT', None)
    if not real:
        fake = TOOLS / 'fake-acp-agent.exe'
        assert fake.is_file(), f'missing {fake}: build first'
        env['BROWSER_CHAT_FAKE_AGENT'] = f'"{fake}" --resume --log "{WORK / "fake-agent.log"}" --edit "{FAKE_EDIT}"'
    env.update(extra_env or {})
    assert env.get('BROWSER_ANNOTATIONS_TEST') == '1' and pathlib.Path(env['BROWSER_ANNOTATIONS_CONNECT_ROOT']).resolve().is_relative_to(ROOT / 'build'), 'unsafe environment'
    p = subprocess.Popen([str(APP / 'browser.exe')], cwd=str(APP), env=env)
    (APP / 'qa.pid').write_text(str(p.pid))
    ok = wait_for(lambda: ws_for(URL) is not None, timeout=60, every=0.3)
    if not ok:   # a fresh profile may abort its first navigation while the content blocker installs: navigate again
        for t in targets():
            if t.get('type') == 'page':
                Q.WS(t['webSocketDebuggerUrl']).call('Page.navigate', {'url': URL}); break
        wait_for(lambda: ws_for(URL) is not None, timeout=30, every=0.3)
    time.sleep(1.2)
    return p


def pid_alive(pid):
    out = subprocess.run(['tasklist', '/FI', f'PID eq {pid}', '/NH'], capture_output=True, text=True).stdout
    return str(pid) in out


def quit_all():
    try:
        pid = int((APP / 'qa.pid').read_text())
    except (OSError, ValueError):
        pid = 0
    h = Q.main_window()
    if h:
        user32.PostMessageW(h, 0x10, 0, 0)
    end = time.time() + 20
    while pid and time.time() < end and pid_alive(pid):
        time.sleep(0.3)
    if pid and pid_alive(pid):
        subprocess.run(['taskkill', '/PID', str(pid), '/T', '/F'], capture_output=True)
    try:
        spid = int((WORK / 'server.pid').read_text())
        subprocess.run(['taskkill', '/PID', str(spid), '/T', '/F'], capture_output=True)
    except (OSError, ValueError):
        pass


def descendants(pid):
    """Every process whose ancestor is `pid` (name, pid, command line)."""
    ps = ("Get-CimInstance Win32_Process | Select-Object ProcessId,ParentProcessId,Name,CommandLine | ConvertTo-Json -Compress")
    raw = subprocess.run(['powershell', '-NoProfile', '-Command', ps], capture_output=True, text=True, encoding='utf-8', errors='replace').stdout
    rows = json.loads(raw, strict=False) if raw.strip() else []   # command lines of other programs may hold control characters
    kids, frontier = [], {pid}
    while frontier:
        nxt = set()
        for r in rows:
            if r['ParentProcessId'] in frontier and r['ProcessId'] not in {k['ProcessId'] for k in kids} and r['ProcessId'] != pid:
                kids.append(r); nxt.add(r['ProcessId'])
        frontier = nxt
    return kids


def main():
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding='utf-8')
        except Exception:
            pass
    cmd, args = sys.argv[1], sys.argv[2:]
    if cmd == 'launch':
        p = launch(real='--real' in args, fresh='--keep' not in args, reset_site='--keep' not in args)
        print('launched', p.pid)
    elif cmd == 'page':
        print(json.dumps(page(args[0]), ensure_ascii=False))
    elif cmd == 'panel':
        print(json.dumps(panel(args[0]), ensure_ascii=False))
    elif cmd == 'key':
        shortcut(args[0])
    elif cmd == 'shot':
        print(shot(args[0]))
    elif cmd == 'log':
        pat = re.compile(args[0]) if args else re.compile(r'CHAT|ANNOTATE')
        for line in log_text().splitlines():
            if pat.search(line):
                print(line)
    elif cmd == 'click-page':
        print(click_page(args[0]))
    elif cmd == 'click-panel':
        print(click_panel(text=args[0]) if not args[0].startswith(('.', '#', '[')) else click_panel(selector=args[0]))
    elif cmd == 'type':
        type_panel(args[0])
    elif cmd == 'enter':
        enter_panel()
    elif cmd == 'quit':
        quit_all()
    elif cmd == 'run':
        import chat_qa_run
        sys.exit(chat_qa_run.main(args))
    else:
        raise SystemExit(__doc__)


if __name__ == '__main__':
    main()
