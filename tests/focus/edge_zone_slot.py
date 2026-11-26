#!/usr/bin/env python3
"""Mode Focus edge zone (design/focus-edge-zone.md): the 3 short checks in a real window (a daytime screen slot or the
night run: it moves the real pointer).

  1. Focus + side layout, the window maximized on the main monitor, a monitor on its left: the pointer thrown left onto
     that monitor and left 13 px past the edge: the column comes (log « FOCUS edge zone: edge 120 ms ») ~120 ms after
     the pointer reached the edge;
  2. the same in the compact layout;
  3. (both layouts) back over the column, then 20 px right of it: it stays; 60 px right of it: it leaves ~300 ms later.

    python tests/focus/edge_zone_slot.py --build build/edge-zone-app [--fake <fake-acp-agent.exe>]
    -> build/edge-zone/slot-report.json

Safety: a fresh copy of the build in build/edge-zone-run (portable: its profile beside browser.exe, never the installed
profile); a sealed home (USERPROFILE / APPDATA / LOCALAPPDATA under build/, PATH = Windows only: no real Claude / Codex
reachable) and an absolute fake agent; the browser in a job object closed at the end (only our processes stop).
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
ap = argparse.ArgumentParser()
ap.add_argument('--build', default='build/edge-zone-app')
ap.add_argument('--fake', default='')
ARGS = ap.parse_args()
SRC = (ROOT / ARGS.build).resolve()
APP = ROOT / 'build' / 'edge-zone-run'
HOME = ROOT / 'build' / 'edge-zone-home'
OUT = ROOT / 'build' / 'edge-zone'
FAKE = pathlib.Path(ARGS.fake).resolve() if ARGS.fake else (SRC.parent / (SRC.name + '-test') / 'fake-acp-agent.exe')
LOG = APP / 'prototype.log'

u32 = ctypes.WinDLL('user32', use_last_error=True)
k32 = ctypes.WinDLL('kernel32', use_last_error=True)
u32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))   # per-monitor v2: device pixels everywhere
u32.WindowFromPoint.argtypes = [W.POINT]; u32.WindowFromPoint.restype = W.HWND
u32.GetAncestor.argtypes = [W.HWND, W.UINT]; u32.GetAncestor.restype = W.HWND
u32.GetForegroundWindow.restype = W.HWND
k32.CreateJobObjectW.restype = W.HANDLE
report = {'build': str(SRC), 'steps': [], 'measures': {}}


def step(name, ok, detail=''):
    report['steps'].append({'step': name, 'ok': bool(ok), 'detail': detail})
    print(('PASS ' if ok else 'FAIL ') + name + (' - ' + json.dumps(detail, default=str) if detail != '' else ''), flush=True)
    return bool(ok)


# ---- the job object: everything the browser starts dies with it, nothing else ----------------------------------------
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
    page = OUT / 'site' / 'long.html'; page.parent.mkdir(parents=True, exist_ok=True)
    page.write_text('<!doctype html><meta charset="utf-8"><title>edge zone</title><body style="margin:0;font:16px sans-serif">'
                    + ''.join(f'<p style="margin:0;padding:8px 0 8px 2px">line {i}</p>' for i in range(400)) + '</body>', encoding='utf-8')
    url = page.resolve().as_uri()
    (APP / 'session').write_text('browser-session-2\n0\n1\n' + json.dumps(url) + '\n0\n', encoding='utf-8', newline='\n')
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
    proc = subprocess.Popen([str(APP / 'browser.exe')], cwd=str(APP), env=env)
    k32.AssignProcessToJobObject(W.HANDLE(JOB), W.HANDLE(int(proc._handle)))
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
                return time.perf_counter(), i
        time.sleep(0.002)
    return None, None


def real_agent_seen():
    t = '\n'.join(log_lines())
    return any(m in t for m in ('runtime absent', 'needs_prepare', 'AGENTS prepare', 'download'))


def main_window(pid):
    found = []
    @ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    def cb(h, _):
        p = W.DWORD(); u32.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value == pid and u32.IsWindowVisible(h):
            r = W.RECT(); u32.GetWindowRect(h, ctypes.byref(r)); found.append(((r.right - r.left) * (r.bottom - r.top), h))
        return True
    u32.EnumWindows(cb, 0)
    return max(found)[1] if found else None


def pid_of(h):
    p = W.DWORD(); u32.GetWindowThreadProcessId(h, ctypes.byref(p)); return p.value


def ours_at(x, y, pid):
    h = u32.WindowFromPoint(W.POINT(x, y)); return bool(h) and pid_of(u32.GetAncestor(h, 2)) == pid


class MOUSEINPUT(ctypes.Structure):
    _fields_ = [('dx', W.LONG), ('dy', W.LONG), ('mouseData', W.DWORD), ('dwFlags', W.DWORD), ('time', W.DWORD), ('dwExtraInfo', ctypes.c_size_t)]
class INPUT(ctypes.Structure):
    # Exactly the size of Windows' INPUT (40 bytes on x64): SendInput refuses any other cbSize (ERROR_INVALID_PARAMETER),
    # and the click that brings our window in front never happened (the side run of the 02:13 slot).
    _fields_ = [('type', W.DWORD), ('mi', MOUSEINPUT)]


def click():
    for f in (0x2, 0x4):
        i = INPUT(); i.type = 0; i.mi.dwFlags = f; u32.SendInput(1, ctypes.byref(i), ctypes.sizeof(INPUT)); time.sleep(0.03)


def front(pid, h):
    fg = u32.GetForegroundWindow()
    return bool(fg) and pid_of(u32.GetAncestor(fg, 3)) == pid


def describe_front():
    fg = u32.GetForegroundWindow(); c = ctypes.create_unicode_buffer(128)
    if fg:
        u32.GetClassNameW(fg, c, 128)
    return {'class': c.value, 'pid': pid_of(fg) if fg else 0}


def bring_front(pid, h, x, y):
    # A script in the background may not take the foreground (SetForegroundWindow is refused), and the test browser opens
    # behind the window in use: put ours above everything for a moment, then a real click on it makes it the foreground.
    for _ in range(5):
        if front(pid, h):
            return True
        u32.SetWindowPos(h, W.HWND(-1), 0, 0, 0, 0, 0x0001 | 0x0002 | 0x0010)   # HWND_TOPMOST, no move / size / activation
        time.sleep(0.2)
        u32.SetCursorPos(x, y); time.sleep(0.15)
        if ours_at(x, y, pid):
            click(); time.sleep(0.4)
        u32.SetWindowPos(h, W.HWND(-2), 0, 0, 0, 0, 0x0001 | 0x0002 | 0x0010)   # HWND_NOTOPMOST again
        time.sleep(0.2)
    return front(pid, h)


def run(layout):
    proc = launch(layout); pid = proc.pid
    t, _ = wait_log('NAVIGATION OK', 60)
    step(f'{layout}: launched (page loaded)', t is not None)
    h = None
    end = time.time() + 20
    while not h and time.time() < end:
        h = main_window(pid); time.sleep(0.2)
    u32.ShowWindow(h, 3); time.sleep(1.0)   # maximized on the main monitor
    r = W.RECT(); u32.GetWindowRect(h, ctypes.byref(r)); c = W.RECT(); u32.GetClientRect(h, ctypes.byref(c))
    pt = W.POINT(0, 0); u32.ClientToScreen(h, ctypes.byref(pt))
    dpi = u32.GetDpiForWindow(h); s = dpi / 96
    step(f'{layout}: maximized on the main monitor, client from x 0', pt.x == 0 and c.right >= 3800, {'client_origin': (pt.x, pt.y), 'client': (c.right, c.bottom), 'dpi': dpi})
    mid = (1920, 1080)
    ok = bring_front(pid, h, *mid)
    step(f'{layout}: our window in front', ok, describe_front())
    if not ok:
        return proc
    time.sleep(0.6)   # the click disarmed the edge: the pointer is off it now, armed again
    if real_agent_seen():
        step('SAFETY: a real agent showed up, stopping', False); return proc
    # 1/2: thrown left onto the left monitor, 13 px past the edge (10.4 DIP at 125 %), left there.
    n0 = len(log_lines())
    t_edge = None
    for x in list(range(1920, -1, -96)) + [0, -6, -13]:
        u32.SetCursorPos(x, 1080)
        if x <= 4 and t_edge is None:
            t_edge = time.perf_counter()   # the edge part: gutter (5 px) and beyond
        time.sleep(0.004)
    t_open, i = wait_log('FOCUS edge zone:', 2.0, n0)
    line = log_lines()[i] if i is not None else ''
    delay = round((t_open - t_edge) * 1000) if t_open else None
    report['measures'][f'{layout}_open_ms'] = delay
    step(f'{layout}: thrown onto the left monitor, the column comes by the edge in ~120 ms', t_open is not None and 'edge 120 ms' in line and delay <= 250, {'line': line, 'ms_after_reaching_edge': delay})
    t_in, _ = wait_log('FOCUS column in (left edge)', 1.0, n0)
    step(f'{layout}: « FOCUS column in (left edge) » logged', t_in is not None)
    # 3: over the column, then 20 px right of it (stays), then 60 px right of it (leaves ~300 ms later).
    column = (248 if layout == 'side' else 40) + 1   # DIP, border included
    edge_px = round(column * s)
    n1 = len(log_lines())
    u32.SetCursorPos(edge_px // 2, 1080); time.sleep(0.6)
    u32.SetCursorPos(edge_px + 20, 1080); time.sleep(0.8)
    out, _ = wait_log('FOCUS column out', 0.01, n1)
    step(f'{layout}: over the column, then 20 px right of it: it stays', out is None, {'column_px': edge_px, 'pointer_x': edge_px + 20, 'depth_dip': round((edge_px + 20) / s, 1)})
    n2 = len(log_lines())
    u32.SetCursorPos(edge_px + 60, 1080); t_away = time.perf_counter()
    t_out, _ = wait_log('FOCUS column out', 2.0, n2)
    away = round((t_out - t_away) * 1000) if t_out else None
    report['measures'][f'{layout}_close_ms'] = away
    step(f'{layout}: 60 px right of it: it leaves ~300 ms later', away is not None and 280 <= away <= 420, {'pointer_x': edge_px + 60, 'depth_dip': round((edge_px + 60) / s, 1), 'ms': away})
    u32.SetCursorPos(*mid)
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
