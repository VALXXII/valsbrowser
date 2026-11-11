"""Real-app E2E for per-window sessions (decision 2) on OUR fixture copy of the build only.

Copies the built browser into build/e2e-windows (portable layout: its data stays in that folder),
seeds two window sessions, starts the browser ONCE and checks that both windows come back (one
adopted, one relaunched), that closing one window among others removes its session, that the last
window keeps its own, and that a killed window's session is restored at the next start. Only
processes whose image lives in the fixture folder are ever touched (closed or killed).
Usage: python tests/e2e/windows_e2e.py <build dir>
"""
import ctypes, ctypes.wintypes as wt, os, shutil, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'build', 'real-integration-check'))
FIX = os.path.join(ROOT, 'build', 'e2e-windows')
k32, u32 = ctypes.windll.kernel32, ctypes.windll.user32
# Optional Microsoft Fixed Version runtime for OUR test processes only (process-scoped environment,
# nothing installed or registered): python windows_e2e.py <build> <fixed runtime folder>
RUNTIME = os.path.abspath(sys.argv[2]) if len(sys.argv) > 2 else ''
ENV = dict(os.environ, WEBVIEW2_BROWSER_EXECUTABLE_FOLDER=RUNTIME) if RUNTIME else None
k32.OpenMutexW.restype = wt.HANDLE
failures = 0

def check(ok, what):
    global failures
    print(('ok   ' if ok else 'FAIL ') + what, flush=True)
    if not ok: failures += 1

def profile_hash(data_dir):
    key = os.path.abspath(data_dir).rstrip('\\/')
    h = 1469598103934665603
    for ch in key.replace('/', '\\').lower():
        h ^= ord(ch); h = (h * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return '%016x' % h

def live(window_id):
    h = k32.OpenMutexW(0x00100000, False, 'Local\\browser-window-%s-%s' % (profile_hash(FIX), window_id))
    if h: k32.CloseHandle(h); return True
    return False

def ours():
    """(pid, hwnd) of top-level browser windows whose process image is in the fixture folder."""
    found = []
    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def each(hwnd, _):
        cls = ctypes.create_unicode_buffer(64); u32.GetClassNameW(hwnd, cls, 64)
        if cls.value != 'LisiereBrowser': return True
        pid = wt.DWORD(); u32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        hp = k32.OpenProcess(0x1000, False, pid.value)
        if hp:
            buf = ctypes.create_unicode_buffer(32768); n = wt.DWORD(32768)
            if k32.QueryFullProcessImageNameW(hp, 0, buf, ctypes.byref(n)) and os.path.normcase(buf.value).startswith(os.path.normcase(FIX + os.sep)):
                found.append((pid.value, hwnd))
            k32.CloseHandle(hp)
        return True
    u32.EnumWindows(each, 0)
    return found

def wait(pred, seconds):
    end = time.time() + seconds
    while time.time() < end:
        if pred(): return True
        time.sleep(0.25)
    return pred()

def session(url):
    return 'browser-session-2\n0\n1\n"%s"\n0\n' % url

class PE(ctypes.Structure):
    _fields_ = [('dwSize', wt.DWORD), ('cntUsage', wt.DWORD), ('th32ProcessID', wt.DWORD), ('th32DefaultHeapID', ctypes.c_void_p), ('th32ModuleID', wt.DWORD),
                ('cntThreads', wt.DWORD), ('th32ParentProcessID', wt.DWORD), ('pcPriClassBase', ctypes.c_long), ('dwFlags', wt.DWORD), ('szExeFile', ctypes.c_wchar * 260)]

def fixture_pids():
    """Every process (window or not) whose image is the fixture's browser.exe."""
    pids = []; snap = k32.CreateToolhelp32Snapshot(0x2, 0); e = PE(); e.dwSize = ctypes.sizeof(PE)
    ok = k32.Process32FirstW(snap, ctypes.byref(e))
    while ok:
        if e.szExeFile.lower() == 'browser.exe':
            hp = k32.OpenProcess(0x1000, False, e.th32ProcessID)
            if hp:
                buf = ctypes.create_unicode_buffer(32768); n = wt.DWORD(32768)
                if k32.QueryFullProcessImageNameW(hp, 0, buf, ctypes.byref(n)) and os.path.normcase(buf.value) == os.path.normcase(os.path.join(FIX, 'browser.exe')): pids.append(e.th32ProcessID)
                k32.CloseHandle(hp)
        ok = k32.Process32NextW(snap, ctypes.byref(e))
    k32.CloseHandle(snap); return pids

def kill_all():
    for pid in fixture_pids():
        hp = k32.OpenProcess(0x0001 | 0x00100000, False, pid)
        if hp: k32.TerminateProcess(hp, 9); k32.WaitForSingleObject(hp, 5000); k32.CloseHandle(hp)

def main():
    kill_all()
    shutil.rmtree(FIX, ignore_errors=True); os.makedirs(FIX)
    for name in ('browser.exe', 'WebView2Loader.dll', 'version.json'):
        shutil.copy2(os.path.join(SRC, name), FIX)
    shutil.copytree(os.path.join(SRC, 'assets'), os.path.join(FIX, 'assets'))
    sessions = os.path.join(FIX, 'sessions'); os.makedirs(sessions)
    with open(os.path.join(FIX, 'settings'), 'w', newline='\n') as f: f.write('browser-settings 1\nrestore-tabs=1\nconfirm-close-tabs=0\n')
    for wid in ('wfixturea', 'wfixtureb'):
        with open(os.path.join(sessions, wid), 'w', newline='\n') as f: f.write(session('https://exemple.invalid/' + wid))
        time.sleep(0.05)
    exe = os.path.join(FIX, 'browser.exe')
    subprocess.Popen([exe], cwd=FIX, env=ENV)
    both = wait(lambda: live('wfixturea') and live('wfixtureb'), 45)
    check(both, 'one start brings both windows back (one adopted, one relaunched)')
    check(wait(lambda: len(ours()) == 2, 20), 'two browser windows of the fixture are open')
    log = open(os.path.join(FIX, 'prototype.log'), encoding='utf-8', errors='replace').read() if os.path.exists(os.path.join(FIX, 'prototype.log')) else ''
    check('(adopted)' in log and '(handed over)' in log, 'log: one window adopted its session, the relaunched one took its own')
    # Close one window among others: its session goes.
    pid_a = None
    for pid, hwnd in ours():
        u32.PostMessageW(hwnd, 0x0010, 0, 0); pid_a = pid; break
    closed = wait(lambda: pid_a not in [p for p, _ in ours()], 20)
    remaining = sorted(os.listdir(sessions))
    check(closed and len(remaining) == 1, 'closing one window among others removes its session (%s left)' % remaining)
    # Close the last window: it keeps its session.
    for pid, hwnd in ours(): u32.PostMessageW(hwnd, 0x0010, 0, 0)
    check(wait(lambda: not ours(), 20) and os.listdir(sessions) == remaining, 'the last window closed keeps its session')
    # Next start restores it; a killed window's session survives for the next start.
    subprocess.Popen([exe], cwd=FIX, env=ENV)
    check(wait(lambda: live(remaining[0]), 45), 'the next start restores the last window')
    kill_all()
    check(wait(lambda: not fixture_pids(), 10) and os.listdir(sessions) == remaining, 'a killed window leaves its session (crash)')
    subprocess.Popen([exe], cwd=FIX, env=ENV)
    check(wait(lambda: live(remaining[0]), 45), 'after the crash, the next start restores it')
    for pid, hwnd in ours(): u32.PostMessageW(hwnd, 0x0010, 0, 0)
    wait(lambda: not fixture_pids(), 20); kill_all(); time.sleep(1)
    # Private window (A9): a real --private process writes no app file and claims no session.
    snapshot = lambda: sorted((os.path.relpath(os.path.join(d, f), FIX), os.path.getsize(os.path.join(d, f))) for d, _, fs in os.walk(FIX) if 'profile' not in os.path.relpath(d, FIX).split(os.sep)[0:1] for f in fs)
    before = snapshot()
    subprocess.Popen([exe, '--private'], cwd=FIX, env=ENV)
    check(wait(lambda: len(ours()) == 1, 45), 'a private window opens')
    title = ''
    if ours():
        buf = ctypes.create_unicode_buffer(512); time.sleep(3); u32.GetWindowTextW(ours()[0][1], buf, 512); title = buf.value
    check(title.endswith('(navigation privée)'), 'private window title says so: %r' % title)
    check(os.listdir(sessions) == remaining, 'a private window claims and writes no session')
    for pid, hwnd in ours(): u32.PostMessageW(hwnd, 0x0010, 0, 0)
    wait(lambda: not ours(), 20); kill_all()
    after = snapshot()
    check(before == after, 'a private window writes no app file (session, history, bookmarks, downloads, settings, log): %s' % sorted(set(after) ^ set(before)))
    log = open(os.path.join(FIX, 'prototype.log'), encoding='utf-8', errors='replace').read() if os.path.exists(os.path.join(FIX, 'prototype.log')) else ''
    runtime = log.count('CreateEnvironment') == 0
    print('NOTE WebView2 runtime: %s' % ('available' if runtime else 'CreateEnvironment failed (%d x): page content NOT verified, process/session level only' % log.count('CreateEnvironment')), flush=True)
    if failures: print('FAIL windows e2e: %d' % failures, flush=True)
    else: print('PASS windows e2e (process/session level): adoption, relaunch, close among others, last window, crash, private writes nothing' + ('' if runtime else '; page content BLOCKED by the WebView2 startup dependency (CreateEnvironment), not verified'), flush=True)
    return 1 if failures else 0

if __name__ == '__main__':
    sys.exit(main())
