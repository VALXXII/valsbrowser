#!/usr/bin/env python3
"""The chat PANEL's model button (« Sonnet 5.5 · Moyen ») and a REAL click (SendInput) — real window: night run only (it
takes the screen, the mouse and the foreground).

25 Nov 2026: a daytime real-window run reported « a real click on the panel's model button does not open its menu »
(same on 0.3.898), while every DevTools click was green. Cause, on the TEST side: qa_final.real_click_chat clicked where
the button is on the screen without our window being in front. In the day the test browser opens behind the owner's /
Orca's window (no foreground for a process started in the background): the click went to THAT window, and the capture
taken next (chat_qa.grab brings our window up first) showed the cursor hovering the button, as if it had been clicked.
Not the coordinates (per-monitor DPI aware, the panel's WebView window, the only « Modèle » control), not the product:
on a hidden desktop (no screen) the panel's menu opens for a posted click, the whole message chain of a real click
(WM_SETCURSOR, WM_MOUSEACTIVATE, WM_PARENTNOTIFY), the browser's activation messages, DevTools and element.click().
qa_final.click_ours now brings our window up first and never clicks a point that is not on the test browser.

  1. our browser in front: a real click on the model button opens its menu (the product);
  2. a window of another process over the button and in front: the click the old helper sent (click_at at the button's
     point) reaches THAT window and the menu stays closed — the failure of 25 Nov, reproduced with real input;
  3. the same window in front again: real_click_chat (guarded) brings our window up first — the menu opens and the
     other window gets no click.

The fake agent only, by an absolute path; a sealed home (USERPROFILE / APPDATA / LOCALAPPDATA / PATH under build/: no
Claude / Codex CLI of this computer can be found); a watch stops this browser (by process id) at the first agent
process that is not the fake one.
    CHAT_QA_BUILD=<build folder under build/> python tests/chat/final/panel_model_click.py
"""
from __future__ import annotations
import ctypes
import ctypes.wintypes as W
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import threading
import time

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import qa_final as F           # noqa: E402
from qa_final import C, Q      # noqa: E402

u32 = ctypes.windll.user32
k32 = ctypes.windll.kernel32
MENU = ("(function(){var m=document.querySelector('.menu.models');var b=document.querySelector('button.m');"
        "return {open:!!(m&&m.getBoundingClientRect().width>0),expanded:b&&b.getAttribute('aria-expanded'),"
        "rows:m?Array.from(m.querySelectorAll('[role=menuitemradio],[role=menuitem]')).map(function(e){return (e.innerText||'').replace(/\\s+/g,' ').trim()}).slice(0,6):[]}})()")
AGENT_PROCESS = re.compile(r'^(claude|codex|node|nodejs|npx|npm|bun|bunx|deno|uv|uvx)([-_. ][^\\/]*)?\.(exe|cmd|bat|com)$', re.I)
SECRET = re.compile(r'^(ANTHROPIC|OPENAI|CLAUDE|CODEX)|(_API_KEY|_TOKEN|_SECRET)$', re.I)


# ---------------------------------------------------------------- the fake agent only
def sealed_env():
    """The « new user's » computer for the test browser: no Claude / Codex CLI, configuration or key of this one."""
    fake = F.fake_agent_exe()
    if not (fake.is_absolute() and fake.is_file() and fake.resolve().is_relative_to(C.ROOT / 'build')):
        raise SystemExit(f'AGENT GUARD: the fake agent must be an existing file under build/, named by an absolute path: {fake}')
    for k in [k for k in os.environ if SECRET.search(k)]:   # never handed to the test browser (C.launch copies os.environ)
        os.environ.pop(k)
    home = C.WORK / 'agent-home'
    for sub in ('AppData/Roaming', 'AppData/Local', '.claude', '.codex'):
        (home / sub).mkdir(parents=True, exist_ok=True)
    windir = os.environ.get('SystemRoot', r'C:\Windows')
    env = {'USERPROFILE': str(home), 'HOME': str(home), 'APPDATA': str(home / 'AppData' / 'Roaming'),
           'LOCALAPPDATA': str(home / 'AppData' / 'Local'), 'CLAUDE_CONFIG_DIR': str(home / '.claude'),
           'CODEX_HOME': str(home / '.codex'), 'PATH': f'{windir}\\System32;{windir}'}
    for name in ('claude', 'codex', 'node', 'npx'):
        if shutil.which(name, path=env['PATH']):
            raise SystemExit(f'AGENT GUARD: {name} is visible on the test browser\'s PATH')
    return env, fake


class PE(ctypes.Structure):
    _fields_ = [('dwSize', W.DWORD), ('cntUsage', W.DWORD), ('pid', W.DWORD), ('heap', ctypes.c_size_t), ('mod', W.DWORD),
                ('threads', W.DWORD), ('ppid', W.DWORD), ('pri', ctypes.c_long), ('flags', W.DWORD), ('exe', ctypes.c_wchar * 260)]


def descendants(root):
    k32.CreateToolhelp32Snapshot.restype = W.HANDLE
    snap = k32.CreateToolhelp32Snapshot(2, 0); kids = {}
    e = PE(); e.dwSize = ctypes.sizeof(e)
    ok = k32.Process32FirstW(W.HANDLE(snap), ctypes.byref(e))
    while ok:
        kids.setdefault(e.ppid, []).append((e.pid, e.exe)); ok = k32.Process32NextW(W.HANDLE(snap), ctypes.byref(e))
    k32.CloseHandle(W.HANDLE(snap))
    out, todo = [], [root]
    while todo:
        for pid, exe in kids.get(todo.pop(), []):
            if pid != root:
                out.append((pid, exe)); todo.append(pid)
    return out


class Watch(threading.Thread):
    """Every 0.2 s: an agent process under OUR browser that is not the fake one stops our browser (its process id only)."""
    def __init__(self, pid):
        super().__init__(daemon=True); self.pid = pid; self.caught = None; self.stop = threading.Event()

    def run(self):
        while not self.stop.wait(0.2):
            for pid, exe in descendants(self.pid):
                if AGENT_PROCESS.match(exe):
                    self.caught = f'{exe} (pid {pid})'
                    h = k32.OpenProcess(0x0001, False, self.pid)
                    if h:
                        k32.TerminateProcess(h, 1); k32.CloseHandle(h)
                    print(f'!! AGENT GUARD: {self.caught} under the test browser: browser {self.pid} stopped', flush=True)
                    return


# ---------------------------------------------------------------- a window of another process in front of ours
class Cover:
    """A plain Tk window of ANOTHER process laid over the test browser's window and brought to the front; it counts the
    clicks it gets (a file). What the owner's own window was to the daytime run of 25 Nov."""
    TITLE = 'QA fenetre devant'

    def __init__(self, rect):
        self.rect, self.hits = rect, C.WORK / 'cover-clicks.txt'

    def __enter__(self):
        try:
            self.hits.unlink()
        except OSError:
            pass
        x, y, r, b = self.rect
        code = ("import ctypes,tkinter as t;ctypes.windll.user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4));"
                f"w=t.Tk();w.title({self.TITLE!r});w.overrideredirect(True);w.geometry('{r - x + 40}x{b - y + 40}+{x - 20}+{y - 20}');"
                "w.configure(bg='#30304a');n=[0]\n"
                f"def hit(e):\n n[0]+=1;open({str(self.hits)!r},'w').write(str(n[0]))\n"
                "w.bind('<Button-1>',hit);w.after(90000,w.destroy);w.mainloop()")
        self.p = subprocess.Popen([sys.executable, '-c', code])
        self.h = 0
        for _ in range(60):
            self.h = u32.FindWindowW(None, self.TITLE)
            if self.h:
                break
            time.sleep(0.1)
        self.raise_()
        return self

    def raise_(self):
        for _ in range(10):
            u32.keybd_event(0x12, 0, 0, 0); u32.keybd_event(0x12, 0, 2, 0); u32.SetForegroundWindow(self.h); time.sleep(0.1)
            if u32.GetForegroundWindow() == self.h:
                return True
        return False

    def clicks(self):
        try:
            return int(self.hits.read_text())
        except (OSError, ValueError):
            return 0

    def __exit__(self, *a):
        try:
            self.p.kill(); self.p.wait(3)
        except Exception:
            pass


def menu():
    return F.chat(MENU, 'panel') or {}


def close_menu():
    F.close_menus('panel'); time.sleep(0.3)


def main():
    for s in (sys.stdout, sys.stderr):
        try:
            s.reconfigure(encoding='utf-8')
        except Exception:
            pass
    R = F.Report('panel-model-click'); before = C.fingerprints(); watch = None
    env, fake = sealed_env()
    try:
        F.launch(extra_env=env)
        watch = Watch(Q.our_pid()); watch.start()
        F.mode_on('panel')
        p = None
        for _ in range(40):
            p = F.chat_point('button.m', kind='panel')
            if p:
                break
            time.sleep(0.25)
        if not R.step('the panel is open with its model button', p, F.chat('document.body.innerText.slice(0,300)', 'panel')):
            return 1
        R.data['measures']['button'] = {'point': p, 'label': F.chat("document.querySelector('button.m').getAttribute('aria-label')", 'panel')}

        # 1) the product: our window in front, a real click opens the menu
        F.front(); time.sleep(0.3)
        R.step('1 · our window in front: the point is on the test browser', F.ours_at(p), F.window_at(p))
        F.click_at(*p); time.sleep(0.8); m = menu()
        R.step('1 · a real click on the model button opens its menu', m.get('open') and m.get('expanded') == 'true', m)
        close_menu()

        wr = F.V3.win_rect(Q.main_window())
        with Cover(wr) as cover:
            # 2) the failure of 25 Nov: our window behind another one, the old helper's click (where the button is)
            R.step('2 · another window in front, over the button (the daytime run\'s state)', cover.raise_() and not F.ours_at(p), F.window_at(p))
            F.click_at(*p); time.sleep(0.8); m = menu()
            R.step('2 · reproduced: the old helper\'s real click goes to the window in front — no menu', cover.clicks() == 1 and not m.get('open'), {'coverClicks': cover.clicks(), 'menu': m})
            close_menu()
            # 3) the helper now: our window brought up first, then the click
            cover.raise_(); n0 = cover.clicks()
            ok = F.real_click_chat('button.m', kind='panel'); time.sleep(0.8); m = menu()
            R.step('3 · real_click_chat brings our window up first: the menu opens, the other window gets no click',
                   ok and m.get('open') and cover.clicks() == n0, {'clicked': ok, 'coverClicks': cover.clicks() - n0, 'menu': m})
            close_menu()
    except BaseException:
        import traceback
        traceback.print_exc()
        R.step('the check ran without an exception', False, traceback.format_exc()[-800:])
    finally:
        if watch:
            watch.stop.set()
            R.step('no agent process other than the fake one', watch.caught is None, watch.caught)
        F.finish(R, before)
    bad = R.failed
    print(f'RESULT {len(R.data["steps"]) - len(bad)} pass, {len(bad)} fail' + (' — ' + ' | '.join(bad) if bad else ''), flush=True)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
