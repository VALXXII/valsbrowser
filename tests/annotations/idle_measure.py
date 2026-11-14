#!/usr/bin/env python3
"""Idle cost of the annotation mode (V2: the launcher is always shown), audit protocol (audit-performances-browser, §2.4):
host CPU = delta GetProcessTimes (user+kernel) of browser.exe over a 30 s window after a 15 s
settle, in % of one core; disk = delta GetProcessIoCounters (write operations / bytes) over the
same window; plus thread count, handles and USER/GDI objects at the end of the window.

    python tests/annotations/idle_measure.py [--runs 3] [--settle 15] [--window 30]

Variants (each in its own fresh copy, own profile, on the sandbox page — serve.py must run):
  baseline   build/annotations-baseline  (--no-annotations: the mode compiled out, no launcher)
  never      build/annotations-preview   (launcher shown at rest, mode never activated)
  used       build/annotations-preview   (mode activated and left, history opened and closed, before the window;
             annotation-connect-prompt=0 so no modal waits for an answer)
Only our own browser.exe is measured (its WebView2 processes are not counted, as in the audit).
Output: build/annotations-v2-qa/idle.json + a Markdown table on stdout.
"""
from __future__ import annotations
import argparse
import ctypes
import ctypes.wintypes as W
import json
import os
import pathlib
import shutil
import statistics
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
QA = ROOT / 'build' / 'annotations-v2-qa'
URL = 'http://127.0.0.1:5173/'
k32, u32, psapi = ctypes.windll.kernel32, ctypes.windll.user32, ctypes.windll.psapi
u32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))


class IO(ctypes.Structure):
    _fields_ = [(n, ctypes.c_ulonglong) for n in ('ReadOp', 'WriteOp', 'OtherOp', 'ReadBytes', 'WriteBytes', 'OtherBytes')]


def open_proc(pid):
    return k32.OpenProcess(0x1000 | 0x0400, False, pid)   # QUERY_LIMITED_INFORMATION | QUERY_INFORMATION


def sample(h, pid):
    c, e, kt, ut = W.FILETIME(), W.FILETIME(), W.FILETIME(), W.FILETIME()
    k32.GetProcessTimes(h, ctypes.byref(c), ctypes.byref(e), ctypes.byref(kt), ctypes.byref(ut))
    cpu = ((kt.dwHighDateTime << 32 | kt.dwLowDateTime) + (ut.dwHighDateTime << 32 | ut.dwLowDateTime)) / 1e4   # ms
    io = IO(); k32.GetProcessIoCounters(h, ctypes.byref(io))
    handles = W.DWORD(); k32.GetProcessHandleCount(h, ctypes.byref(handles))
    return {'cpu_ms': cpu, 'write_ops': io.WriteOp, 'write_bytes': io.WriteBytes, 'handles': handles.value,
            'user_objects': u32.GetGuiResources(h, 1), 'gdi_objects': u32.GetGuiResources(h, 0), 'threads': threads(pid)}


def threads(pid):
    class TE(ctypes.Structure):
        _fields_ = [('dwSize', W.DWORD), ('cntUsage', W.DWORD), ('th32ThreadID', W.DWORD), ('th32OwnerProcessID', W.DWORD),
                    ('tpBasePri', W.LONG), ('tpDeltaPri', W.LONG), ('dwFlags', W.DWORD)]
    snap = k32.CreateToolhelp32Snapshot(4, 0); te = TE(); te.dwSize = ctypes.sizeof(TE); n = 0
    ok = k32.Thread32First(snap, ctypes.byref(te))
    while ok:
        n += te.th32OwnerProcessID == pid
        ok = k32.Thread32Next(snap, ctypes.byref(te))
    k32.CloseHandle(snap); return n


def main_window(pid):
    found = []
    @ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    def cb(h, _):
        p = W.DWORD(); u32.GetWindowThreadProcessId(h, ctypes.byref(p))
        name = ctypes.create_unicode_buffer(64); u32.GetClassNameW(h, name, 64)
        if p.value == pid and name.value == 'LisiereBrowser' and u32.IsWindowVisible(h):
            found.append(h)
        return True
    u32.EnumWindows(cb, 0)
    return found[0] if found else None


def post_key(h, vk, ctrl=False, shift=False):
    """Ctrl/Maj state through the browser thread's keyboard state, then WM_KEYDOWN/UP to OUR window only."""
    tid = u32.GetWindowThreadProcessId(h, None); me = k32.GetCurrentThreadId()
    attached = u32.AttachThreadInput(me, tid, True)
    state = (ctypes.c_ubyte * 256)(); u32.GetKeyboardState(state); old = bytes(state)
    if ctrl: state[0x11] = state[0xA2] = 0x80
    if shift: state[0x10] = state[0xA0] = 0x80
    u32.SetKeyboardState(state)
    u32.PostMessageW(h, 0x100, vk, 1); time.sleep(0.15); u32.PostMessageW(h, 0x101, vk, 0xC0000001); time.sleep(0.15)
    u32.SetKeyboardState((ctypes.c_ubyte * 256).from_buffer_copy(old))
    if attached:
        u32.AttachThreadInput(me, tid, False)


def use_once(pid, log):
    """Mode on and off (Ctrl+Maj+A twice), history open and closed (Ctrl+Maj+H twice)."""
    h = main_window(pid)
    for vk in (ord('A'), ord('A'), ord('H')):
        post_key(h, vk, ctrl=True, shift=True); time.sleep(1.5)
    # The history panel has the focus: Échap to it closes it (the browser's own keys are not routed to it).
    class GTI(ctypes.Structure):
        _fields_ = [('cbSize', W.DWORD), ('flags', W.DWORD), ('hwndActive', W.HWND), ('hwndFocus', W.HWND), ('hwndCapture', W.HWND),
                    ('hwndMenuOwner', W.HWND), ('hwndMoveSize', W.HWND), ('hwndCaret', W.HWND), ('rcCaret', W.RECT)]
    g = GTI(); g.cbSize = ctypes.sizeof(GTI)
    u32.GetGUIThreadInfo(u32.GetWindowThreadProcessId(h, None), ctypes.byref(g))
    post_key(g.hwndFocus or h, 0x1B); time.sleep(1.5)
    text = log.read_text(encoding='utf-8', errors='replace')
    return 'ANNOTATE mode on' in text and 'ANNOTATE mode off' in text and 'history opened' in text and text.count('watching the store') == text.count('store watch stopped')


def run(variant, src, settle, window, index):
    app = ROOT / 'build' / f'annotations-idle-{variant}'
    if app.exists():
        for _ in range(20):   # a just-closed copy can keep its exe locked for a moment
            shutil.rmtree(app, ignore_errors=True)
            if not app.exists(): break
            time.sleep(1)
    shutil.copytree(src, app, ignore=shutil.ignore_patterns('profile', 'session', 'sessions', 'annotations', '*.log', 'settings', 'history', 'downloads', 'adblock'))
    (app / 'session').write_text('browser-session-2\n0\n1\n' + json.dumps(URL) + '\n0\n', encoding='utf-8', newline='\n')
    if variant == 'used':   # no launch modal waiting for an answer (the key is read when the settings worker's build has it)
        (app / 'settings').write_text('browser-settings 1\nannotation-connect-prompt=0\n', encoding='utf-8', newline='\n')
    # Test copies never reach the real Claude / Codex configurations (incident 14 Nov 2026): guard + redirection.
    env = dict(os.environ); env['BROWSER_ANNOTATIONS_TEST'] = '1'; env['BROWSER_ANNOTATIONS_CONNECT_ROOT'] = str(app / 'agent-configs')
    p = subprocess.Popen([str(app / 'browser.exe')], cwd=str(app), env=env)
    log = app / 'prototype.log'; t0 = time.time()
    while time.time() - t0 < 60 and not (log.exists() and 'NAVIGATION OK' in log.read_text(encoding='utf-8', errors='replace')):
        time.sleep(0.2)
    time.sleep(3)
    used = use_once(p.pid, log) if variant == 'used' else None
    time.sleep(settle)
    h = open_proc(p.pid); a = sample(h, p.pid); time.sleep(window); b = sample(h, p.pid)
    text = log.read_text(encoding='utf-8', errors='replace')
    r = {'variant': variant, 'run': index, 'cpu_pct_core': round((b['cpu_ms'] - a['cpu_ms']) / (window * 1000) * 100, 3),
         'cpu_ms': round(b['cpu_ms'] - a['cpu_ms'], 1), 'write_ops': b['write_ops'] - a['write_ops'], 'write_bytes': b['write_bytes'] - a['write_bytes'],
         'threads': b['threads'], 'handles': b['handles'], 'user_objects': b['user_objects'], 'gdi_objects': b['gdi_objects'],
         'store_dir_exists': (app / 'annotations').exists(), 'annotate_log_lines': sum(1 for l in text.splitlines() if l.startswith('ANNOTATE')), 'used_ok': used,
         'watch_running_at_end': text.count('ANNOTATE watching the store') > text.count('ANNOTATE store watch stopped')}
    k32.CloseHandle(h)
    hw = main_window(p.pid)
    if hw:
        u32.PostMessageW(hw, 0x10, 0, 0)
    try:
        p.wait(timeout=30)
    except subprocess.TimeoutExpired:
        p.kill()   # our own instance only
    time.sleep(2)
    return r


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--runs', type=int, default=3); ap.add_argument('--settle', type=int, default=15); ap.add_argument('--window', type=int, default=30)
    a = ap.parse_args()
    builds = {'baseline': ROOT / 'build/annotations-baseline', 'never': ROOT / 'build/annotations-preview', 'used': ROOT / 'build/annotations-preview'}
    results = []
    for i in range(a.runs):
        for v in ('baseline', 'never', 'used'):
            r = run(v, builds[v], a.settle, a.window, i + 1); results.append(r); print(json.dumps(r), flush=True)
    QA.mkdir(parents=True, exist_ok=True)
    (QA / 'idle.json').write_text(json.dumps({'protocol': {'settle_s': a.settle, 'window_s': a.window, 'runs': a.runs}, 'results': results}, indent=2), encoding='utf-8')
    print('\n| variante | CPU hôte (% d\'un cœur) médiane [min–max] | écritures (ops / octets) | threads | handles | USER | GDI | dossier annotations |')
    print('|---|---|---|---|---|---|---|---|')
    for v in ('baseline', 'never', 'used'):
        rs = [r for r in results if r['variant'] == v]
        cpu = [r['cpu_pct_core'] for r in rs]
        med = lambda k: statistics.median(r[k] for r in rs)
        print(f"| {v} | {statistics.median(cpu):.3f} [{min(cpu):.3f}–{max(cpu):.3f}] | {med('write_ops'):.0f} / {med('write_bytes'):.0f} | {med('threads'):.0f} | {med('handles'):.0f} | {med('user_objects'):.0f} | {med('gdi_objects'):.0f} | {', '.join(str(r['store_dir_exists']) for r in rs)} |")


if __name__ == '__main__':
    main()
