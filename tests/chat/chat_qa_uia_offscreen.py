#!/usr/bin/env python3
"""F7 (audit pass 5, monkey-3 #214: the main window's UI Automation tree 113 -> 22): the tree of the main window stays whole
and the chat is found in it, in every state of the chat — WITHOUT a window on the user's screens.

    CHAT_QA_BUILD=<folder under build/> python tests/chat/chat_qa_uia_offscreen.py     (default build/chat-a11y)

Off screen: the browser starts hidden (SW_HIDE), then is moved OUTSIDE the virtual screen and shown there without
activation; every window of the browser (the bubble too) is checked to stay off screen. No SendInput, no UI Automation
Invoke (an Invoke lets the browser take the foreground: it did on 22 Nov 17:36 during another worker's real-window run).
The browser still gives the keyboard to a bubble it opens (its own SetForegroundWindow, measured 22 Nov 18:08): run it only
when no real-window run is going on. Posted keys (Ctrl+Maj+A, Échap) and the banner's own
posted activation (WM_APP+1) only; the pages are driven through DevTools. Own copy and ports: build/chat-a11y-app, page
5197, DevTools 9371 (never the real-window QA's).

Checks, after each step: the main window's element count >= 90 % of its count at start (page loaded, mode off) — a walk
found short is read again 0.5 s later and both are written with the short walk's elements (tests/chat/final/uia_server.ps1
« list »); the chat's last message token found by name in the main window's tree (bubble or panel) when a chat is shown.
Steps: comment layout (warm bubble), bubble open, message sent, wheel, closed; panel layout, long answers, turns running
during the wheel; layout switches. -> build/chat-a11y-qa/uia-offscreen.json
"""
from __future__ import annotations
import ctypes
import ctypes.wintypes as W
import json
import os
import pathlib
import shutil
import subprocess
import sys
import time

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE)); sys.path.insert(0, str(HERE / 'final'))
import chat_qa as C            # noqa: E402
from chat_qa import Q          # noqa: E402

BUILD = os.environ.get('CHAT_QA_BUILD') or 'chat-a11y'
C.SRC = ROOT / 'build' / BUILD
C.TOOLS = ROOT / 'build' / 'chat-qa' / 'final' / 'tools'
C.APP = ROOT / 'build' / 'chat-a11y-app'; Q.APP = C.APP
C.WORK = ROOT / 'build' / 'chat-a11y-work'; C.SITE = C.WORK / 'site'; C.AGENTS = C.WORK / 'agent-configs'; C.LOG = C.APP / 'prototype.log'
C.PORT_PAGE = 5197; C.URL = 'http://127.0.0.1:5197/'; Q.PORT = 9371; Q.URL = C.URL
import qa_final as F           # noqa: E402  (reads C.* at call time)

LANDING = C.URL + 'landing.html'
OUT = ROOT / 'build' / 'chat-a11y-qa'
u32 = ctypes.windll.user32
WM_APP, WM_KEYDOWN, WM_KEYUP = 0x8000, 0x0100, 0x0101
BANNER_CHAT, BANNER_COMMENT = 3, 4   # src/annotation-banner.h BannerHit
results, notes = [], []


def check(name, ok, detail=None):
    results.append({'step': name, 'ok': bool(ok), 'detail': detail})
    print(('PASS ' if ok else 'FAIL ') + name + ('' if ok or detail is None else f' — {json.dumps(detail, ensure_ascii=False)[:600]}'), flush=True)
    return ok


def windows(pid):
    out = []
    @ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    def cb(h, _):
        q = W.DWORD(); u32.GetWindowThreadProcessId(h, ctypes.byref(q))
        if q.value == pid:
            n = ctypes.create_unicode_buffer(64); u32.GetClassNameW(h, n, 64)
            t = ctypes.create_unicode_buffer(128); u32.GetWindowTextW(h, t, 128)
            out.append((h, n.value, t.value))
        return True
    u32.EnumWindows(cb, 0)
    main = [h for h, c, _ in out if c == 'LisiereBrowser']
    if main:   # the banner's layer may be a child of the browser window
        u32.EnumChildWindows(main[0], cb, 0)
    return out


def our_windows(cls=None, title=None):
    return [h for h, c, t in windows(Q.our_pid()) if (cls is None or c == cls) and (title is None or t == title)]


def offscreen_ok():
    for h in our_windows():
        if u32.IsWindowVisible(h) and not (u32.GetWindowLongW(h, -16) & 0x40000000):   # top-level and owned popups (bubble), not WS_CHILD
            r = W.RECT(); u32.GetWindowRect(h, ctypes.byref(r))
            if r.right > u32.GetSystemMetrics(76) and r.right > r.left and r.bottom > r.top:
                return False
    return True


def foreground_ours():
    q = W.DWORD(); u32.GetWindowThreadProcessId(u32.GetForegroundWindow(), ctypes.byref(q)); return q.value == Q.our_pid()


def launch():
    app = C.APP; app.mkdir(parents=True, exist_ok=True); C.WORK.mkdir(parents=True, exist_ok=True)
    keep = ('profile', 'session', 'sessions', 'annotations', 'prototype.log', 'downloads', 'bookmarks', 'settings', 'history',
            'annotations-connect', 'annotations-chat', 'chat-snapshots', 'agents', 'profiles', 'profiles.txt', 'favicons', 'extensions')
    for f in C.SRC.iterdir():
        if f.name not in keep:
            (shutil.copytree(f, app / f.name, dirs_exist_ok=True) if f.is_dir() else shutil.copy2(f, app / f.name))
    for n in ('annotations', 'annotations-connect', 'annotations-chat', 'chat-snapshots', 'profiles', 'sessions'):
        shutil.rmtree(app / n, ignore_errors=True)
    for n in ('settings', 'prototype.log', 'profiles.txt'):
        try:
            (app / n).unlink()
        except OSError:
            pass
    (app / 'session').write_text('browser-session-2\n0\n1\n' + json.dumps(LANDING) + '\n0\n', encoding='utf-8', newline='\n')
    C.mark_tutorial_seen(app)   # #125: the tutorial (its veil, its own windows) is not what this suite reads
    shutil.rmtree(C.SITE, ignore_errors=True); shutil.copytree(ROOT / 'sandbox' / 'chat' / 'site', C.SITE)
    for sub in ('AppData/Roaming/Claude', 'AppData/Local', 'home/.codex'):
        (C.AGENTS / sub).mkdir(parents=True, exist_ok=True)
    server = subprocess.Popen([sys.executable, str(ROOT / 'sandbox' / 'chat' / 'serve.py'), '--root', str(C.SITE), '--port', str(C.PORT_PAGE)],
                              cwd=str(C.SITE), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    (C.WORK / 'server.pid').write_text(str(server.pid))
    fake = C.TOOLS / 'fake-acp-agent.exe'; assert fake.is_file(), f'missing {fake}'
    env = dict(os.environ)
    env['WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS'] = (f'--remote-debugging-port={Q.PORT} --remote-allow-origins=http://127.0.0.1:{Q.PORT} '
                                                    '--disable-features=CalculateNativeWinOcclusion --disable-backgrounding-occluded-windows')
    env['BROWSER_ANNOTATIONS_CONNECT_ROOT'] = str(C.AGENTS); env['BROWSER_ANNOTATIONS_TEST'] = '1'
    env.pop('BROWSER_ANNOTATIONS_CONNECT_PATH', None)
    env['BROWSER_CHAT_PANEL_IDLE_MS'] = '60000'; env['BROWSER_CHAT_AGENT_IDLE_MS'] = '600000'
    env['BROWSER_CHAT_FAKE_AGENT'] = f'"{fake}" --resume --claude-like --start-mode auto --log "{C.WORK / "fake-agent.log"}"'
    assert pathlib.Path(env['BROWSER_ANNOTATIONS_CONNECT_ROOT']).resolve().is_relative_to(ROOT / 'build'), 'unsafe environment'
    si = subprocess.STARTUPINFO(); si.dwFlags = 1; si.wShowWindow = 0   # STARTF_USESHOWWINDOW, SW_HIDE
    p = subprocess.Popen([str(app / 'browser.exe')], cwd=str(app), env=env, startupinfo=si)
    (app / 'qa.pid').write_text(str(p.pid))
    ok = C.wait_for(lambda: C.ws_for(C.URL) is not None, timeout=60, every=0.3)
    h = next(iter(our_windows('LisiereBrowser')), None)
    vx, vy = u32.GetSystemMetrics(76), u32.GetSystemMetrics(77)
    u32.SetWindowPos(h, 0, vx - 2600, vy, 1900, 1000, 0x0010 | 0x0004 | 0x0040)   # NOACTIVATE | NOZORDER | SHOWWINDOW
    time.sleep(1.5)
    return ok and h, h


def post_key(h, vk, ctrl=False, shift=False):
    Q.post_key(vk, target=h, ctrl=ctrl, shift=shift)


def wait_log(pattern, mark, timeout=8):
    return bool(F.wait_log(pattern, mark, timeout))


def layout(item):
    """The band's « Mode Commentaire » / « Chat en panneau » by the layer's own posted activation (no Invoke, no input)."""
    banner = next(iter(our_windows('BrowserAnnotationLayer', 'Mode Annotation')), None)
    m = F.log_mark(); u32.PostMessageW(banner, WM_APP + 1, item, 0)
    ok = wait_log(r'CHAT layout: ' + ('Panneau' if item == BANNER_CHAT else 'Commentaire'), m, 6); time.sleep(1.5)
    return ok


def wheel_page(dy):
    F.page_ws().call('Input.dispatchMouseEvent', {'type': 'mouseWheel', 'x': 600, 'y': 300, 'deltaX': 0, 'deltaY': dy})


def send(text, kind):
    F.focus_composer(kind)
    ws = F._ws_for(F.chat_target(kind))
    ws.call('Input.insertText', {'text': text}); time.sleep(0.15)
    ws.call('Input.dispatchKeyEvent', {'type': 'keyDown', 'key': 'Enter', 'code': 'Enter', 'windowsVirtualKeyCode': 13, 'nativeVirtualKeyCode': 13, 'text': '\r'})
    ws.call('Input.dispatchKeyEvent', {'type': 'keyUp', 'key': 'Enter', 'code': 'Enter', 'windowsVirtualKeyCode': 13, 'nativeVirtualKeyCode': 13})


class Tree:
    def __init__(self):
        self.uia = F.Uia(); self.base = None

    def main_count(self, needle=''):
        r, s = self.uia.probe(needle)
        main = [x for x in r if x.get('cls') == 'LisiereBrowser'] if isinstance(r, list) else []
        return (main[0]['n'] if main else -1), (main[0].get('found') if main else False), r, s

    def step(self, label, needle='', settle=True):
        """A walk found short is read again 0.5 s later: still short = the tree is cut (a screen reader loses it: FAIL);
        whole again = the walk crossed the page while it changed (FindAll is not atomic: noted with what it held)."""
        n, found, r, s = self.main_count(needle)
        if self.base is None:
            self.base = n; notes.append({'base': n, 'windows': r}); check(f'{label}: the main window has a tree ({n} elements)', n > 10, r); return
        if n < self.base * 0.9:
            short = self.uia.ask(f'list {Q.our_pid()}'); time.sleep(0.5); n2, found2, r2, _ = self.main_count(needle)
            entry = {'step': label, 'elements': n, 'again after 0.5 s': n2, 'base': self.base, 'windows': r, 'short walk': short}
            notes.append(entry); print(f'  short walk: {n} then {n2} 0.5 s later (base {self.base})', flush=True)
            check(f'{label}: main window tree whole (>= 90 % of {self.base}; {n} then {n2} 0.5 s later)', n2 >= self.base * 0.9, entry)
            n, found = n2, found2
        elif settle:
            check(f'{label}: main window tree whole ({n} >= 90 % of {self.base}, {s} s)', True)
        if needle and not found:
            # General test of 24 Nov 2026 (1 read in 25, twice): the tree was whole (283 / 305 elements) but the chat's words were
            # not in THAT walk — read at once after a wheel while the thread was being drawn again (a turn ending). The same rule
            # as a short walk: read again 0.5 s later. Still missing = a screen reader loses the chat (FAIL); there again = the
            # walk crossed the thread while it changed (FindAll is not atomic), noted with what the first read held.
            time.sleep(0.5); n3, found3, r3, _ = self.main_count(needle)
            notes.append({'step': label, 'needle': needle, 'first read': r, 'elements': n, 'again after 0.5 s': {'found': found3, 'elements': n3}})
            print(f'  chat not in this walk ({n} elements): read again 0.5 s later -> {"found" if found3 else "STILL MISSING"} ({n3} elements)', flush=True)
            found, r = found3, r3
        if needle:
            check(f"{label}: the chat found in the main window's tree (« {needle} »)", found, r)
        if settle:
            check(f'{label}: every window of the browser still off screen', offscreen_ok())
            if foreground_ours():   # the browser gives the keyboard to an opened bubble itself (SetForegroundWindow): noted
                notes.append({'step': label, 'foreground': 'the browser (off screen)'})

    def stress(self, label, needle, rounds=25):
        """F7 (#128 and #214 came right after a wheel): the tree read AT ONCE after each wheel step, no settle."""
        before = len(notes)
        for i in range(rounds):
            wheel_page(700 if i % 2 == 0 else -700)
            self.step(f'{label} {i}', needle, settle=False)
        check(f'{label}: {rounds} reads right after a wheel, {len(notes) - before} read again (short walk or chat not in the walk), none still wrong 0.5 s later',
              all(r['ok'] for r in results if r['step'].startswith(label)))


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    before = C.fingerprints(); tree = None
    ok, h = launch()
    try:
        if not check('started off screen (hidden, then outside the virtual screen, not activated)', ok and offscreen_ok() and not foreground_ours()):
            return 1
        tree = Tree(); tree.step('page loaded, mode off')
        m = F.log_mark(); post_key(h, ord('A'), ctrl=True, shift=True)
        check('mode on (posted Ctrl+Maj+A)', wait_log(r'ANNOTATE mode on', m, 10)); time.sleep(1.2)
        for d in our_windows('BrowserModalDialog'):   # the connection screen: Échap (« plus tard » / « fermer »)
            post_key(d, 0x1B)
        time.sleep(0.8)
        check("comment layout (the banner's posted activation)", layout(BANNER_COMMENT))
        tree.step('comment layout, nothing open (warm bubble)')
        m = F.log_mark(); C.click_page('section.hero'); check('bubble opened (DevTools click)', wait_log(r'CHAT bubble shown|bubble opened', m, 6)); time.sleep(1.5)
        tree.step('bubble open')
        send('reply a11ybulle1', 'bubble'); time.sleep(2.5)
        tree.step('bubble, message sent', 'a11ybulle1')
        for dy in (300, -300, 600, -600):
            wheel_page(dy); time.sleep(0.3); tree.step(f'bubble, wheel {dy}', 'a11ybulle1')
        C.page("document.querySelector('div.plans').scrollIntoView({block:'center'})"); time.sleep(0.6)
        m = F.log_mark(); C.click_page('div.plans'); check('bubble opened on div.plans', wait_log(r'CHAT bubble shown|bubble opened', m, 6)); time.sleep(1.2)
        send('reply a11yplans', 'bubble'); time.sleep(2.0)
        tree.stress('comment, bubble on div.plans, read at once after a wheel', 'a11yplans')
        F.escape_chat('bubble'); time.sleep(1.0); tree.step('bubble closed (Échap)')
        check("panel layout (the banner's posted activation)", layout(BANNER_CHAT))
        tree.step('panel open')
        for k in range(3):
            send(f'vrai-surcharge a11ypanneau{k}', 'panel'); time.sleep(2.5); tree.step(f'panel, long answer {k}', f'a11ypanneau{k}')
        for dy in (300, -600, 900, -900):
            wheel_page(dy); time.sleep(0.25); tree.step(f'panel, wheel {dy}', 'a11ypanneau2')
        for k in range(3):
            send(f'vrai-tour a11ytour{k}', 'panel'); time.sleep(0.3)
            for j in range(3):
                wheel_page(-300 if j % 2 else 300); time.sleep(0.15); tree.step(f'panel, turn {k} running, wheel {j}', f'a11ytour{k}')
        tree.stress('panel, read at once after a wheel', 'a11ytour2')
        for k in range(2):
            layout(BANNER_COMMENT); tree.step(f'comment layout again {k}')
            layout(BANNER_CHAT); tree.step(f'panel layout again {k}', 'a11ytour2')
    except Exception as e:
        import traceback
        check('without exception', False, traceback.format_exc()[-1200:]); print(e)
    finally:
        if tree:
            tree.uia.close()
        C.quit_all()
        check('real Claude / Codex configuration files unchanged', before == C.fingerprints())
        (OUT / 'uia-offscreen.json').write_text(json.dumps({'build': BUILD, 'notes': notes, 'steps': results}, ensure_ascii=False, indent=1), encoding='utf-8')
    failed = [r for r in results if not r['ok']]
    print(f'{len(results) - len(failed)} / {len(results)} checks passed -> {OUT / "uia-offscreen.json"}')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8')
    sys.exit(main())
