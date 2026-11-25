#!/usr/bin/env python3
"""Ctrl+Maj+F with the annotation banner holding the keyboard, on a HIDDEN Windows desktop (daytime-safe).

    python tests/focus/banner_shortcut_qa.py --app <built app dir> --out <scratch dir>

Night 25/11: on the published 0.3.840 / 0.3.842 the general test's Annotation + Focus journey found Ctrl+Maj+F doing
nothing once the annotation banner held the keyboard (it had taken it from a UI Automation invoke; Tab / F6 or a screen
reader do the same): src/main.cpp Key() left every key to the annotation surfaces, the banner's row of buttons included.
Fixed: the banner keeps its own keys (Tab, the arrows, Entrée, Espace, Échap), the browser's shortcuts go through.

A copy of the build on a desktop of its own (never displayed), a local page (local: the annotation mode starts there), the
mode turned on by the window's own command, the banner given the keyboard (input attached to the browser's UI thread,
SetFocus), then Ctrl+Maj+F as a key the banner holds (WM_KEYDOWN with Ctrl and Maj down in the shared key state): the
browser must log « SETTINGS focus mode on ». Then Tab on the banner must not reach the browser's shortcuts (still the
banner's). Exit code 0 only when both hold. The app runs in a job object (KILL_ON_JOB_CLOSE); only our processes stop.
"""
from __future__ import annotations
import argparse, ctypes, ctypes.wintypes as W, http.server, json, os, shutil, sys, threading, time
from pathlib import Path

ap = argparse.ArgumentParser()
ap.add_argument('--app', required=True); ap.add_argument('--out', required=True)
a = ap.parse_args()
src, out = Path(a.app).resolve(), Path(a.out).resolve()
shutil.rmtree(out, ignore_errors=True); out.mkdir(parents=True)

u32 = ctypes.WinDLL('user32', use_last_error=True); k32 = ctypes.WinDLL('kernel32', use_last_error=True); g32 = ctypes.WinDLL('gdi32')
u32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
for f, args, res in [
    (u32.CreateDesktopW, [W.LPCWSTR, W.LPCWSTR, ctypes.c_void_p, W.DWORD, W.DWORD, ctypes.c_void_p], W.HANDLE),
    (u32.SetThreadDesktop, [W.HANDLE], W.BOOL), (u32.CloseDesktop, [W.HANDLE], W.BOOL),
    (u32.EnumDesktopWindows, [W.HANDLE, ctypes.c_void_p, W.LPARAM], W.BOOL),
    (u32.RedrawWindow, [W.HWND, ctypes.c_void_p, W.HANDLE, W.UINT], W.BOOL), (u32.PostMessageW, [W.HWND, W.UINT, W.WPARAM, W.LPARAM], W.BOOL), (u32.GetClientRect, [W.HWND, ctypes.POINTER(W.RECT)], W.BOOL),
    (u32.GetDpiForWindow, [W.HWND], W.UINT), (u32.GetDC, [W.HWND], W.HDC), (u32.ReleaseDC, [W.HWND, W.HDC], ctypes.c_int),
    (u32.SetWindowPos, [W.HWND, W.HWND, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, W.UINT], W.BOOL),
    (u32.IsWindowVisible, [W.HWND], W.BOOL), (u32.GetClassNameW, [W.HWND, W.LPWSTR, ctypes.c_int], ctypes.c_int),
    (u32.GetWindowThreadProcessId, [W.HWND, ctypes.POINTER(W.DWORD)], W.DWORD), (u32.PrintWindow, [W.HWND, W.HDC, W.UINT], W.BOOL),
    (g32.CreateCompatibleDC, [W.HDC], W.HDC), (g32.CreateDIBSection, [W.HDC, ctypes.c_void_p, W.UINT, ctypes.POINTER(ctypes.c_void_p), W.HANDLE, W.DWORD], W.HBITMAP),
    (g32.SelectObject, [W.HDC, W.HGDIOBJ], W.HGDIOBJ), (g32.DeleteObject, [W.HGDIOBJ], W.BOOL), (g32.DeleteDC, [W.HDC], W.BOOL),
    (k32.CreateJobObjectW, [ctypes.c_void_p, W.LPCWSTR], W.HANDLE), (k32.SetInformationJobObject, [W.HANDLE, ctypes.c_int, ctypes.c_void_p, W.DWORD], W.BOOL),
    (k32.AssignProcessToJobObject, [W.HANDLE, W.HANDLE], W.BOOL), (k32.ResumeThread, [W.HANDLE], W.DWORD), (k32.CloseHandle, [W.HANDLE], W.BOOL)]:
    f.argtypes, f.restype = args, res

class STARTUPINFO(ctypes.Structure):
    _fields_ = [('cb', W.DWORD), ('lpReserved', W.LPWSTR), ('lpDesktop', W.LPWSTR), ('lpTitle', W.LPWSTR), ('dwX', W.DWORD), ('dwY', W.DWORD),
                ('dwXSize', W.DWORD), ('dwYSize', W.DWORD), ('dwXCountChars', W.DWORD), ('dwYCountChars', W.DWORD), ('dwFillAttribute', W.DWORD),
                ('dwFlags', W.DWORD), ('wShowWindow', W.WORD), ('cbReserved2', W.WORD), ('lpReserved2', ctypes.c_void_p),
                ('hStdInput', W.HANDLE), ('hStdOutput', W.HANDLE), ('hStdError', W.HANDLE)]
class PROCESS_INFORMATION(ctypes.Structure):
    _fields_ = [('hProcess', W.HANDLE), ('hThread', W.HANDLE), ('dwProcessId', W.DWORD), ('dwThreadId', W.DWORD)]
class BASIC(ctypes.Structure):
    _fields_ = [('a', ctypes.c_int64), ('b', ctypes.c_int64), ('LimitFlags', W.DWORD), ('c', ctypes.c_size_t), ('d', ctypes.c_size_t), ('e', W.DWORD), ('f', ctypes.c_size_t), ('g', W.DWORD), ('h', W.DWORD)]
class EXT(ctypes.Structure):
    _fields_ = [('Basic', BASIC), ('Io', ctypes.c_uint64 * 6), ('i', ctypes.c_size_t), ('j', ctypes.c_size_t), ('k', ctypes.c_size_t), ('l', ctypes.c_size_t)]
k32.CreateProcessW.argtypes = [W.LPCWSTR, W.LPWSTR, ctypes.c_void_p, ctypes.c_void_p, W.BOOL, W.DWORD, ctypes.c_void_p, W.LPCWSTR, ctypes.POINTER(STARTUPINFO), ctypes.POINTER(PROCESS_INFORMATION)]
k32.CreateProcessW.restype = W.BOOL

PAGE_RGB = (0x2E, 0x7D, 0xF6)   # the page's flat colour: neither the canvas grey, nor black, nor white
# A 2 px corner of the page changes by one level of blue every frame (invisible): the page presents continuously, so the
# hidden desktop's DWM composes the window (and our DirectComposition layers) for every capture.
PAGE = ('<!doctype html><title>Focus</title><style>@keyframes t{from{background:#%02X%02X%02X}to{background:#%02X%02X%02X}}'
        '#t{position:fixed;left:0;top:0;width:2px;height:2px;animation:t .05s linear infinite alternate}</style>'
        '<body style="margin:0;background:#%02X%02X%02X"><div id="t"></div><div style="height:2400px"></div></body>'
        % (PAGE_RGB + (PAGE_RGB[0], PAGE_RGB[1], PAGE_RGB[2] + 1) + PAGE_RGB)).encode()
class H(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200); self.send_header('Content-Type', 'text/html'); self.send_header('Content-Length', str(len(PAGE))); self.end_headers(); self.wfile.write(PAGE)
    def log_message(self, *x): pass
srv = http.server.ThreadingHTTPServer(('127.0.0.1', 0), H); url = f'http://127.0.0.1:{srv.server_port}/page'
threading.Thread(target=srv.serve_forever, daemon=True).start()

app = out / 'app'; shutil.copytree(src, app, ignore=shutil.ignore_patterns('session', 'sessions', 'settings', 'profile', 'profiles', 'prototype.log', 'history', 'bookmarks', 'downloads'))
local = out / 'localappdata'; local.mkdir()
(app / 'session').write_text(f'browser-session-2\n0\n1\n"{url}"\n0\n', encoding='utf-8', newline='\n')
(app / 'settings').write_text('browser-settings 1\nrestore-tabs=1\nconfirm-close-tabs=0\nfocus-mode=0\nannotation-onboarded=1\nannotation-connect-prompt=0\n', encoding='utf-8', newline='\n')

job = k32.CreateJobObjectW(None, None); info = EXT(); info.Basic.LimitFlags = 0x2000   # KILL_ON_JOB_CLOSE
k32.SetInformationJobObject(job, 9, ctypes.byref(info), ctypes.sizeof(info))
desk_name = f'focus-banner-{os.getpid()}'
hdesk = u32.CreateDesktopW(desk_name, None, None, 0, 0x10000000, None)
assert hdesk, f'CreateDesktop failed {ctypes.get_last_error()}'
home = out / 'home'
for sub in ('AppData/Roaming', 'AppData/Local'): (home / sub).mkdir(parents=True, exist_ok=True)
# The « user's » folders of the test browser point to an empty home under <out> (no real Claude / Codex configuration or
# CLI can be seen), agent connection refused (BROWSER_ANNOTATIONS_TEST=1), as tests/chat/chat_qa.py does.
env = dict(os.environ, LOCALAPPDATA=str(local), BROWSER_NO_ONBOARDING='1', BROWSER_ANNOTATIONS_TEST='1', USERPROFILE=str(home), APPDATA=str(home / 'AppData/Roaming'),
           BROWSER_ANNOTATIONS_CONNECT_ROOT=str(home))
block = ''.join(f'{k}={v}\0' for k, v in env.items()) + '\0'
si = STARTUPINFO(); si.cb = ctypes.sizeof(si); si.lpDesktop = f'WinSta0\\{desk_name}'
pi = PROCESS_INFORMATION()
cmd = f'"{app / "browser.exe"}"'
ok = k32.CreateProcessW(None, ctypes.create_unicode_buffer(cmd), None, None, False, 0x4 | 0x400, ctypes.create_unicode_buffer(block), str(app), ctypes.byref(si), ctypes.byref(pi))   # SUSPENDED | UNICODE_ENVIRONMENT
assert ok, f'CreateProcess failed {ctypes.get_last_error()}'
k32.AssignProcessToJobObject(job, pi.hProcess); k32.ResumeThread(pi.hThread); k32.CloseHandle(pi.hThread)
pid = pi.dwProcessId


WM_KEYDOWN, WM_KEYUP, Shortcut, AnnotateCommand = 0x0100, 0x0101, 0x8000 + 1, 3060
for f, args, res in [(u32.EnumChildWindows, [W.HWND, ctypes.c_void_p, W.LPARAM], W.BOOL), (u32.GetWindowRect, [W.HWND, ctypes.POINTER(W.RECT)], W.BOOL),
                     (u32.AttachThreadInput, [W.DWORD, W.DWORD, W.BOOL], W.BOOL), (u32.SetFocus, [W.HWND], W.HWND), (u32.GetFocus, [], W.HWND),
                     (u32.SetKeyboardState, [ctypes.c_void_p], W.BOOL), (u32.GetKeyboardState, [ctypes.c_void_p], W.BOOL)]:
    f.argtypes, f.restype = args, res
k32.GetCurrentThreadId.restype = W.DWORD
report = {'checks': []}; result = {}
log_path = app / 'prototype.log'
def log_text():
    try: return log_path.read_text(encoding='utf-8', errors='replace')
    except OSError: return ''
def wait_log(needle, after, timeout):
    end = time.time() + timeout
    while time.time() < end:
        if needle in log_text()[after:]: return True
        time.sleep(0.1)
    return False

def driver():
    assert u32.SetThreadDesktop(hdesk), f'SetThreadDesktop {ctypes.get_last_error()}'
    found = []
    @ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    def each(h, _):
        p = W.DWORD(); u32.GetWindowThreadProcessId(h, ctypes.byref(p)); n = ctypes.create_unicode_buffer(64); u32.GetClassNameW(h, n, 64)
        if p.value == pid and n.value == 'LisiereBrowser' and u32.IsWindowVisible(h): found.append(h)
        return True
    win = None
    for _ in range(240):
        found.clear(); u32.EnumDesktopWindows(hdesk, each, 0)
        if found: win = found[0]; break
        time.sleep(0.25)
    assert win, 'browser window on the hidden desktop'
    s = u32.GetDpiForWindow(win) / 96
    u32.SetWindowPos(win, None, 40, 40, round(1180 * s), round(705 * s), 0x0014)
    time.sleep(6.0)   # the page first (the mode starts on a local page)
    m = len(log_text()); u32.PostMessageW(win, Shortcut, AnnotateCommand, 0)
    result['mode'] = wait_log('ANNOTATE mode on', m, 15); time.sleep(1.5)
    banner = []
    @ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    def child(h, _):
        n = ctypes.create_unicode_buffer(64); u32.GetClassNameW(h, n, 64); r = W.RECT(); u32.GetWindowRect(h, ctypes.byref(r))
        if n.value == 'BrowserAnnotationLayer' and u32.IsWindowVisible(h) and abs((r.bottom - r.top) - 32 * s) <= 2 and (r.right - r.left) > 400 * s: banner.append(h)
        return True
    u32.EnumChildWindows(win, child, 0)
    result['banner'] = bool(banner)
    if not banner: return
    tid = u32.GetWindowThreadProcessId(win, None); me = k32.GetCurrentThreadId()
    attached = u32.AttachThreadInput(me, tid, True)
    u32.SetFocus(banner[0]); time.sleep(0.3)
    result['banner_has_keyboard'] = u32.GetFocus() == banner[0]
    def key(vk, ctrl=False, shift=False):
        st = (ctypes.c_ubyte * 256)(); u32.GetKeyboardState(st); old = bytes(st)
        st[0x11] = st[0xA2] = 0x80 if ctrl else 0; st[0x10] = st[0xA0] = 0x80 if shift else 0
        u32.SetKeyboardState(st); target = u32.GetFocus() or banner[0]
        u32.PostMessageW(target, WM_KEYDOWN, vk, 1); time.sleep(0.15); u32.PostMessageW(target, WM_KEYUP, vk, 0xC0000001); time.sleep(0.15)
        u32.SetKeyboardState((ctypes.c_ubyte * 256).from_buffer_copy(old))
    m = len(log_text()); key(ord('F'), ctrl=True, shift=True)
    result['focus_on'] = wait_log('SETTINGS focus mode on', m, 3)
    u32.SetFocus(banner[0]); time.sleep(0.3)   # Mode Focus relays the window out: the banner keeps (or gets back) the keyboard
    m = len(log_text()); key(0x09)   # Tab: the banner's own key (moves between its buttons, the keyboard stays on it), never a browser command
    # Only the lines a browser command writes count: other SETTINGS lines (the ad-block list copied in the background…) may land now.
    time.sleep(0.6); after = log_text()[m:]
    result['tab_stayed'] = u32.GetFocus() == banner[0] and 'focus mode' not in after and 'TABS ' not in after
    if attached: u32.AttachThreadInput(me, tid, False)
    result['ok'] = True

t = threading.Thread(target=driver, daemon=True); t.start(); t.join(150)
srv.shutdown(); k32.CloseHandle(job); u32.CloseDesktop(hdesk)   # KILL_ON_JOB_CLOSE: our copy of the browser goes with it
fails = []
def check(ok, what, detail=''):
    report['checks'].append({'ok': bool(ok), 'what': what, 'detail': str(detail)}); print(('PASS ' if ok else 'FAIL ') + what + (f' — {detail}' if detail else ''), flush=True)
    if not ok: fails.append(what)
check(result.get('mode'), 'the annotation mode on (local page)')
check(result.get('banner'), 'its banner on screen')
check(result.get('banner_has_keyboard'), 'the banner holds the keyboard')
check(result.get('focus_on'), 'Ctrl+Maj+F from the banner turns Mode Focus on')
check(result.get('tab_stayed'), 'Tab stays the banner\'s (no browser command)')
report['result'] = result
(out / 'report.json').write_text(json.dumps(report, indent=1), encoding='utf-8')
print(f"{len(report['checks']) - len(fails)} / {len(report['checks'])} checks passed; report {out / 'report.json'}")
sys.exit(1 if fails else 0)
