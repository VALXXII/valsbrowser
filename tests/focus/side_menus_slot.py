#!/usr/bin/env python3
"""Mode Focus with the side panel: the toolbar menus opened by their shortcut come from the column (owner, 2026-11-26:
Ctrl+J opened the downloads menu top-right). The short checks in a real window (a screen slot: real keys, captures).

For each layout (« side » complete, « compact »), Focus on, the column hidden:
  1. Ctrl+J in the page: the column comes back at rest (log « FOCUS column in (downloads) ») and the Downloads menu
     (LisiereDownloads) hangs from it, on the panel's side (left half of the window), never top-right; capture;
  2. Échap: the menu closes, the pointer away: the column leaves (« FOCUS column out »);
  3. Ctrl+H: the column back with the Historique drawer docked against it on the left; capture; Échap;
  4. Ctrl+D: the bookmark popover inside the window on the panel's side (compact: beside the column); capture; Échap.

    python tests/focus/side_menus_slot.py --build build/focus-side-menus [--fake <fake-acp-agent.exe>]
    -> build/focus-side-menus-slot/slot-report.json and its PNGs (plain `python`: ImageGrab is in the user's site-packages)

Safety: a fresh copy of the build in build/focus-side-menus-run (portable: its profile beside browser.exe, never the
installed profile); a sealed home (USERPROFILE / APPDATA / LOCALAPPDATA under build/, PATH = Windows only: no real
Claude / Codex reachable) and an absolute fake agent; the browser in a job object closed at the end (only our processes).
"""
from __future__ import annotations
import argparse
import ctypes
import ctypes.wintypes as W
import json
import os
import pathlib
import shutil
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tests' / 'agent-guard'))
import agent_guard as G   # noqa: E402  (no agent of this computer: tests/agent-guard)
ap = argparse.ArgumentParser()
ap.add_argument('--build', default='build/focus-side-menus')
ap.add_argument('--fake', default='')
ARGS = ap.parse_args()
SRC = (ROOT / ARGS.build).resolve()
APP = ROOT / 'build' / 'focus-side-menus-run'
HOME = ROOT / 'build' / 'focus-side-menus-home'
OUT = ROOT / 'build' / 'focus-side-menus-slot'
FAKE = pathlib.Path(ARGS.fake).resolve() if ARGS.fake else (SRC.parent / (SRC.name + '-test') / 'fake-acp-agent.exe')
LOG = APP / 'prototype.log'

u32 = ctypes.WinDLL('user32', use_last_error=True)
k32 = ctypes.WinDLL('kernel32', use_last_error=True)
u32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))   # per-monitor v2: device pixels everywhere
u32.WindowFromPoint.argtypes = [W.POINT]; u32.WindowFromPoint.restype = W.HWND
u32.GetAncestor.argtypes = [W.HWND, W.UINT]; u32.GetAncestor.restype = W.HWND
u32.GetForegroundWindow.restype = W.HWND
k32.CreateJobObjectW.restype = W.HANDLE
report = {'build': str(SRC), 'steps': [], 'captures': []}


def step(name, ok, detail=''):
    report['steps'].append({'step': name, 'ok': bool(ok), 'detail': detail})
    print(('PASS ' if ok else 'FAIL ') + name + (' - ' + json.dumps(detail, default=str) if detail != '' else ''), flush=True)
    return bool(ok)


class JOBOBJECT_BASIC_LIMIT_INFORMATION(ctypes.Structure):
    _fields_ = [('PerProcessUserTimeLimit', ctypes.c_int64), ('PerJobUserTimeLimit', ctypes.c_int64), ('LimitFlags', W.DWORD),
                ('MinimumWorkingSetSize', ctypes.c_size_t), ('MaximumWorkingSetSize', ctypes.c_size_t), ('ActiveProcessLimit', W.DWORD),
                ('Affinity', ctypes.c_size_t), ('PriorityClass', W.DWORD), ('SchedulingClass', W.DWORD)]
class IO_COUNTERS(ctypes.Structure):
    _fields_ = [(n, ctypes.c_uint64) for n in ('r', 'w', 'o', 'rb', 'wb', 'ob')]
class JOBOBJECT_EXTENDED_LIMIT_INFORMATION(ctypes.Structure):
    _fields_ = [('Basic', JOBOBJECT_BASIC_LIMIT_INFORMATION), ('Io', IO_COUNTERS), ('ProcessMemoryLimit', ctypes.c_size_t),
                ('JobMemoryLimit', ctypes.c_size_t), ('PeakProcessMemoryUsed', ctypes.c_size_t), ('PeakJobMemoryUsed', ctypes.c_size_t)]
JOB = k32.CreateJobObjectW(None, None)
info = JOBOBJECT_EXTENDED_LIMIT_INFORMATION(); info.Basic.LimitFlags = 0x2000   # KILL_ON_JOB_CLOSE
k32.SetInformationJobObject(W.HANDLE(JOB), 9, ctypes.byref(info), ctypes.sizeof(info))


def launch(layout):
    shutil.rmtree(APP, ignore_errors=True)
    shutil.copytree(SRC, APP)
    for n in ('profile', 'session', 'sessions', 'settings', 'prototype.log', 'profiles', 'profiles.txt'):
        p = APP / n
        shutil.rmtree(p, ignore_errors=True) if p.is_dir() else p.unlink(missing_ok=True)
    page = OUT / 'site' / 'page.html'; page.parent.mkdir(parents=True, exist_ok=True)
    page.write_text('<!doctype html><meta charset="utf-8"><title>Focus menus</title><body style="margin:0;font:16px sans-serif;background:#f4f4f4">'
                    + ''.join(f'<p style="margin:0;padding:8px">line {i}</p>' for i in range(60)) + '</body>', encoding='utf-8')
    (APP / 'session').write_text('browser-session-2\n0\n1\n' + json.dumps(page.resolve().as_uri()) + '\n0\n', encoding='utf-8', newline='\n')
    (APP / 'settings').write_text('browser-settings 1\nfocus-mode=1\ntabs-position=' + layout + '\nannotation-onboarded=1\nrestore-tabs=1\n', encoding='utf-8', newline='\n')
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith(('BROWSER_', 'WEBVIEW2_'))}
    for sub in ('AppData/Roaming', 'AppData/Local', '.local/bin'):
        (HOME / sub).mkdir(parents=True, exist_ok=True)
    env['USERPROFILE'] = str(HOME); env['HOMEDRIVE'] = str(HOME)[:2]; env['HOMEPATH'] = str(HOME)[2:]
    env['APPDATA'] = str(HOME / 'AppData' / 'Roaming'); env['LOCALAPPDATA'] = str(HOME / 'AppData' / 'Local')
    env['PATH'] = r'C:\Windows\System32;C:\Windows;C:\Windows\System32\WindowsPowerShell\v1.0'
    for k in [k for k in env if k.upper() in ('CLAUDE_CONFIG_DIR', 'CODEX_HOME', 'ANTHROPIC_API_KEY', 'OPENAI_API_KEY', 'NODE_OPTIONS')]:
        env.pop(k)
    env['BROWSER_CHAT_FAKE_AGENT'] = f'"{FAKE}" --resume --log "{OUT / "fake-agent.log"}"'
    # SAFETY, before launching: no real agent reachable, the fake one absolute and present, the profile ours.
    for n in ('claude', 'codex', 'node', 'npx', 'npm'):
        assert shutil.which(n, path=env['PATH']) is None, f'{n} reachable from the sealed PATH: refused'
    assert FAKE.is_absolute() and FAKE.is_file(), f'fake agent missing: {FAKE}'
    real = pathlib.Path(os.environ.get('LOCALAPPDATA', 'C:/nowhere')) / 'browser'
    assert APP.resolve().is_relative_to((ROOT / 'build').resolve()) and not APP.resolve().is_relative_to(real.resolve()), 'profile folder: refused'
    # the browser's test switch on (without it the fake agent is not used and this computer's CLIs are probed), the fake
    # agent checked, keys stripped; then the watch on our browser (agent processes, launches, « AGENTS … ready »)
    env = G.secure_env(env, work=OUT, exe=APP / 'browser.exe', fake=FAKE)
    proc = subprocess.Popen([str(APP / 'browser.exe')], cwd=str(APP), env=env)
    k32.AssignProcessToJobObject(W.HANDLE(JOB), W.HANDLE(int(proc._handle)))
    G.watch(proc.pid, env, log=LOG, work=OUT)
    return proc


def log_lines():
    try:
        return LOG.read_text(encoding='utf-8', errors='replace').splitlines()
    except OSError:
        return []


def wait_log(text, timeout, after=0):
    end = time.perf_counter() + timeout
    while time.perf_counter() < end:
        lines = log_lines()
        for i, l in enumerate(lines[after:], after):
            if text in l:
                return l
        time.sleep(0.01)
    return None


def real_agent_seen():
    t = '\n'.join(log_lines())
    return any(m in t for m in ('runtime absent', 'needs_prepare', 'AGENTS prepare', 'download'))


def windows_of(pid):
    found = []
    @ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    def cb(h, _):
        p = W.DWORD(); u32.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value == pid and u32.IsWindowVisible(h):
            r = W.RECT(); u32.GetWindowRect(h, ctypes.byref(r)); c = ctypes.create_unicode_buffer(128); u32.GetClassNameW(h, c, 128)
            found.append({'hwnd': h, 'class': c.value, 'rect': (r.left, r.top, r.right, r.bottom), 'area': (r.right - r.left) * (r.bottom - r.top)})
        return True
    u32.EnumWindows(cb, 0)
    return found


def main_window(pid):
    w = windows_of(pid)
    return max(w, key=lambda x: x['area'])['hwnd'] if w else None


def pid_of(h):
    p = W.DWORD(); u32.GetWindowThreadProcessId(h, ctypes.byref(p)); return p.value


def ours_at(x, y, pid):
    h = u32.WindowFromPoint(W.POINT(x, y)); return bool(h) and pid_of(u32.GetAncestor(h, 2)) == pid


class MOUSEINPUT(ctypes.Structure):
    _fields_ = [('dx', W.LONG), ('dy', W.LONG), ('mouseData', W.DWORD), ('dwFlags', W.DWORD), ('time', W.DWORD), ('dwExtraInfo', ctypes.c_size_t)]
class KEYBDINPUT(ctypes.Structure):
    _fields_ = [('wVk', W.WORD), ('wScan', W.WORD), ('dwFlags', W.DWORD), ('time', W.DWORD), ('dwExtraInfo', ctypes.c_size_t)]
class _U(ctypes.Union):
    _fields_ = [('mi', MOUSEINPUT), ('ki', KEYBDINPUT)]
class INPUT(ctypes.Structure):
    _fields_ = [('type', W.DWORD), ('u', _U)]   # 40 bytes on x64, as SendInput requires


def click():
    for f in (0x2, 0x4):
        i = INPUT(); i.type = 0; i.u.mi.dwFlags = f; u32.SendInput(1, ctypes.byref(i), ctypes.sizeof(INPUT)); time.sleep(0.03)


def keys(*vks):
    seq = [(vk, 0) for vk in vks] + [(vk, 2) for vk in reversed(vks)]   # down in order, up in reverse (KEYEVENTF_KEYUP)
    for vk, flags in seq:
        i = INPUT(); i.type = 1; i.u.ki.wVk = vk; i.u.ki.dwFlags = flags; u32.SendInput(1, ctypes.byref(i), ctypes.sizeof(INPUT)); time.sleep(0.03)


def front(pid):
    fg = u32.GetForegroundWindow(); return bool(fg) and pid_of(u32.GetAncestor(fg, 3)) == pid


def bring_front(pid, h, x, y):
    for _ in range(5):
        if front(pid) and ours_at(x, y, pid):
            return True
        u32.SetWindowPos(h, W.HWND(-1), 0, 0, 0, 0, 0x0001 | 0x0002 | 0x0010)   # topmost a moment, no activation
        time.sleep(0.2)
        u32.SetCursorPos(x, y); time.sleep(0.15)
        if ours_at(x, y, pid):
            click(); time.sleep(0.4)
        u32.SetWindowPos(h, W.HWND(-2), 0, 0, 0, 0, 0x0001 | 0x0002 | 0x0010)
        time.sleep(0.2)
    return front(pid)


def press(pid, *vks):
    """Real keys only while OUR window (or one of its popups) is the foreground one; otherwise nothing is sent."""
    if not front(pid):
        step('real keys refused: our window is not in front', False, describe_front()); return False
    keys(*vks); return True


def describe_front():
    fg = u32.GetForegroundWindow(); c = ctypes.create_unicode_buffer(128)
    if fg:
        u32.GetClassNameW(fg, c, 128)
    return {'class': c.value, 'pid': pid_of(fg) if fg else 0}


def capture(name, box):
    try:
        from PIL import ImageGrab
        img = ImageGrab.grab(bbox=box, all_screens=True); path = OUT / f'{name}.png'; img.save(path)
        report['captures'].append(str(path)); return str(path)
    except Exception as e:   # a locked session: no capture, said so
        return f'no capture: {e}'


def run(layout):
    proc = launch(layout); pid = proc.pid
    ok = wait_log('NAVIGATION OK', 60) is not None
    step(f'{layout}: launched (page loaded)', ok)
    h = None; end = time.time() + 20
    while not h and time.time() < end:
        h = main_window(pid); time.sleep(0.2)
    u32.ShowWindow(h, 1); u32.SetWindowPos(h, None, 200, 1100, 1500, 950, 0x0004 | 0x0010)   # windowed, low on the main monitor
    time.sleep(1.0)
    r = W.RECT(); u32.GetWindowRect(h, ctypes.byref(r)); mid = ((r.left + r.right) // 2, (r.top + r.bottom) // 2)
    if not step(f'{layout}: our window in front', bring_front(pid, h, *mid)):
        return proc
    if real_agent_seen():
        step('SAFETY: a real agent showed up, stopping', False); return proc
    time.sleep(0.8)
    out0 = len(log_lines())
    step(f'{layout}: the column hidden (Focus)', wait_log('FOCUS column in', 0.01, out0) is None)
    # 1. Ctrl+J in the page
    n = len(log_lines()); press(pid, 0x11, 0x4A)
    line = wait_log('FOCUS column in (downloads)', 2.0, n)
    time.sleep(0.6)
    pop = [w for w in windows_of(pid) if w['class'] == 'LisiereDownloads']
    pr = pop[0]['rect'] if pop else None
    step(f'{layout}: Ctrl+J: the column back at rest', line is not None, line or '')
    step(f'{layout}: Ctrl+J: the Downloads menu on the panel side (left half), not top-right', pr is not None and pr[2] < mid[0] and pr[1] > r.top + 120, {'menu': pr, 'window': (r.left, r.top, r.right, r.bottom)})
    step(f'{layout}: capture, Ctrl+J', True, capture(f'{layout}-ctrl-j', (r.left, r.top, r.right, r.bottom)))
    # 2. Échap, the pointer away from the column: it leaves
    n = len(log_lines()); press(pid, 0x1B); u32.SetCursorPos(r.right - 300, mid[1])
    step(f'{layout}: Échap: the menu closed, the column leaves (pointer away)', wait_log('FOCUS column out', 3.0, n) is not None and not [w for w in windows_of(pid) if w['class'] == 'LisiereDownloads'])
    time.sleep(0.6)
    # 3. Ctrl+H: the drawer docked against the column
    n = len(log_lines()); press(pid, 0x11, 0x48)
    line = wait_log('FOCUS column in (', 2.0, n); time.sleep(0.8)
    dr = [w for w in windows_of(pid) if w['class'].startswith('LisiereHistory')]
    step(f'{layout}: Ctrl+H: the column back, Historique docked on the left', line is not None and bool(dr) and dr[0]['rect'][0] < mid[0], {'drawer': dr[0]['rect'] if dr else None, 'log': line})
    step(f'{layout}: capture, Ctrl+H', True, capture(f'{layout}-ctrl-h', (r.left, r.top, r.right, r.bottom)))
    press(pid, 0x1B); time.sleep(0.8); u32.SetCursorPos(r.right - 300, mid[1]); time.sleep(0.8)
    # 4. Ctrl+D: the bookmark popover on the panel's side (compact: beside the column, no star there; it went above the window)
    press(pid, 0x11, 0x44); time.sleep(0.8)
    bp = [w for w in windows_of(pid) if w['class'] == 'LisiereBookmarkPopover']
    step(f'{layout}: Ctrl+D: the bookmark popover inside the window, on the panel side', bool(bp) and bp[0]['rect'][1] >= r.top and bp[0]['rect'][2] < mid[0], {'popover': bp[0]['rect'] if bp else None})
    step(f'{layout}: capture, Ctrl+D', True, capture(f'{layout}-ctrl-d', (r.left, r.top, r.right, r.bottom)))
    press(pid, 0x1B); time.sleep(0.5)
    return proc


def stop(proc):
    h = main_window(proc.pid)
    if h:
        u32.PostMessageW(h, 0x10, 0, 0)
    try:
        proc.wait(15)
    except subprocess.TimeoutExpired:
        pass
    k32.TerminateJobObject(W.HANDLE(JOB), 1)   # what is left of OUR processes, nothing else
    time.sleep(1.0)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    try:
        for layout in ('side', 'compact'):
            p = run(layout)
            stop(p)
            if any(s['step'].startswith('SAFETY') for s in report['steps']):
                break
    finally:
        k32.TerminateJobObject(W.HANDLE(JOB), 1)
        (OUT / 'slot-report.json').write_text(json.dumps(report, indent=1, default=str), encoding='utf-8')
    fails = [s['step'] for s in report['steps'] if not s['ok']]
    print('RESULT', 'all passed' if not fails else f'{len(fails)} failed', flush=True)
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
