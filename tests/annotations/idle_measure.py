#!/usr/bin/env python3
"""Idle cost of the annotation mode (T3), audit protocol (audit-performances-browser, §2.4):
host CPU = delta GetProcessTimes (user+kernel) of browser.exe over a 30 s window after a 15 s
settle, in % of one core; disk = delta GetProcessIoCounters (write operations / bytes) over the
same window; plus thread count, handles and USER/GDI objects at the end of the window.

    python tests/annotations/idle_measure.py [--runs 3] [--settle 15] [--window 30]

Variants (each in its own fresh copy, own profile, on the sandbox page — serve.py must run):
  baseline   build/annotations-baseline  (--no-annotations: the mode compiled out)
  never      build/annotations-preview   (mode compiled in, never activated)
  used       build/annotations-preview   (mode activated then left once, before the window)
Only our own browser.exe is measured (its WebView2 processes are not counted, as in the audit).
Output: build/annotations-v1-qa/idle.json + a Markdown table on stdout.
"""
from __future__ import annotations
import argparse
import ctypes
import ctypes.wintypes as W
import json
import pathlib
import shutil
import statistics
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
QA = ROOT / 'build' / 'annotations-v1-qa'
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


def click_annotate(pid):
    """Activate then leave the mode once (toolbar button 'Annoter', posted to our own window)."""
    h = main_window(pid); k = u32.GetDpiForWindow(h) / 96
    rc = W.RECT(); u32.GetClientRect(h, ctypes.byref(rc)); x = (rc.right / k - 157 + 12); y = 60
    lp = (int(y * k) << 16) | int(x * k)
    for _ in range(2):
        for m, w in ((0x200, 0), (0x201, 1), (0x202, 0)):
            u32.PostMessageW(h, m, w, lp); time.sleep(0.05)
        time.sleep(1.5)


def run(variant, src, settle, window, index):
    app = ROOT / 'build' / f'annotations-idle-{variant}'
    if app.exists():
        shutil.rmtree(app)
    shutil.copytree(src, app, ignore=shutil.ignore_patterns('profile', 'session', 'sessions', 'annotations', '*.log', 'settings', 'history', 'downloads', 'adblock'))
    (app / 'session').write_text('browser-session-2\n0\n1\n' + json.dumps(URL) + '\n0\n', encoding='utf-8', newline='\n')
    p = subprocess.Popen([str(app / 'browser.exe')], cwd=str(app))
    log = app / 'prototype.log'; t0 = time.time()
    while time.time() - t0 < 60 and not (log.exists() and 'NAVIGATION OK' in log.read_text(encoding='utf-8', errors='replace')):
        time.sleep(0.2)
    time.sleep(3)
    if variant == 'used':
        click_annotate(p.pid)
    time.sleep(settle)
    h = open_proc(p.pid); a = sample(h, p.pid); time.sleep(window); b = sample(h, p.pid)
    text = log.read_text(encoding='utf-8', errors='replace')
    r = {'variant': variant, 'run': index, 'cpu_pct_core': round((b['cpu_ms'] - a['cpu_ms']) / (window * 1000) * 100, 3),
         'cpu_ms': round(b['cpu_ms'] - a['cpu_ms'], 1), 'write_ops': b['write_ops'] - a['write_ops'], 'write_bytes': b['write_bytes'] - a['write_bytes'],
         'threads': b['threads'], 'handles': b['handles'], 'user_objects': b['user_objects'], 'gdi_objects': b['gdi_objects'],
         'store_dir_exists': (app / 'annotations').exists(), 'annotate_log_lines': sum(1 for l in text.splitlines() if l.startswith('ANNOTATE'))}
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
