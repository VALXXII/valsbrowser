#!/usr/bin/env python3
"""V2-C: where does an idle browser.exe spend CPU? Per-thread CPU (GetThreadTimes) over a window, for a running
browser.exe given by pid, plus the context-switch-free threads count. Read-only: it only queries our own process.

    python tests/annotations/idle_probe.py <pid> [--window 20]
"""
import argparse, ctypes, ctypes.wintypes as W, json, time
k32 = ctypes.windll.kernel32


class TE(ctypes.Structure):
    _fields_ = [('dwSize', W.DWORD), ('cntUsage', W.DWORD), ('th32ThreadID', W.DWORD), ('th32OwnerProcessID', W.DWORD),
                ('tpBasePri', W.LONG), ('tpDeltaPri', W.LONG), ('dwFlags', W.DWORD)]


def tids(pid):
    snap = k32.CreateToolhelp32Snapshot(4, 0); te = TE(); te.dwSize = ctypes.sizeof(TE); out = []
    ok = k32.Thread32First(snap, ctypes.byref(te))
    while ok:
        if te.th32OwnerProcessID == pid: out.append(te.th32ThreadID)
        ok = k32.Thread32Next(snap, ctypes.byref(te))
    k32.CloseHandle(snap); return out


def cpu(tid):
    h = k32.OpenThread(0x0800, False, tid)   # THREAD_QUERY_LIMITED_INFORMATION
    if not h: return None
    c, e, kt, ut = W.FILETIME(), W.FILETIME(), W.FILETIME(), W.FILETIME()
    k32.GetThreadTimes(h, ctypes.byref(c), ctypes.byref(e), ctypes.byref(kt), ctypes.byref(ut))
    desc = ctypes.c_wchar_p(); name = ''
    try:
        if k32.GetThreadDescription(h, ctypes.byref(desc)) >= 0 and desc.value: name = desc.value
    except Exception:
        pass
    k32.CloseHandle(h)
    return ((kt.dwHighDateTime << 32 | kt.dwLowDateTime) + (ut.dwHighDateTime << 32 | ut.dwLowDateTime)) / 1e4, name


ap = argparse.ArgumentParser(); ap.add_argument('pid', type=int); ap.add_argument('--window', type=float, default=20); a = ap.parse_args()
before = {t: cpu(t) for t in tids(a.pid)}
time.sleep(a.window)
rows = []
for t in tids(a.pid):
    now = cpu(t)
    if not now: continue
    was = before.get(t)
    rows.append({'tid': t, 'name': now[1], 'ms': round(now[0] - (was[0] if was else 0), 1), 'new': was is None})
rows.sort(key=lambda r: -r['ms'])
print(json.dumps({'threads': len(rows), 'total_ms': round(sum(r['ms'] for r in rows), 1), 'top': rows[:12]}, indent=1))
