"""Engine facts behind the crashed-tab page and the screen-share confirmation (#121), with no window shown.

    python -I tests/crash-screenshare/engine_probe.py [--modes crash-builtin,crash-ours,...]

Serves tests/crash-screenshare/site from a local server only (127.0.0.1 pages, a localhost frame: two sites), then runs
engine_probe_host.exe once per mode, each with a fresh temporary profile, inside a job object with KILL_ON_JOB_CLOSE.
The host's window is never shown (off every monitor, never activated): nothing appears on screen, the mouse and focus
are left alone; the engine's screen picker only opens in the picker mode, on a desktop that is never displayed. No real site, no
real profile. The FACT lines go to build/crash-screenshare/engine-facts.txt and the checks below must hold:

  * a renderer crash (DevTools Page.crash) raises ProcessFailed RENDER_PROCESS_EXITED; Reload() brings the page back;
  * our document shown with NavigateToString right in the handler appears, and navigating to the crashed address
    (« Recharger ») brings the page back;
  * renderer processes terminated from outside raise the same kind;
  * ScreenCaptureStarting fires for getDisplayMedia() (page and cross-site frame), its deferral holds the page's
    promise, and Cancel gives the page a NotAllowedError (« Permission denied »), the normal denial;
  * picker (« Choisir… »: Cancel FALSE): the engine's own picker opens INSIDE the engine's window (no window of its
    own; picker-open.bmp shows it), and a click on its « Cancel » closes it: the page gets a NotAllowedError
    (« Permission denied by user »). This mode alone lets the picker open, so it runs on a Windows desktop of its own
    that is never displayed (CreateDesktop): nothing reaches the user's screen, mouse, keyboard or foreground;
  * hang-again: the engine reports a frozen page once (new input after the report brings no second one, 24/11): what
    « Attendre » cannot wait for, so Browser asks again itself (src/crash-page.h AfterWait).

Tools: tools/w64devkit and deps/webview2 of this checkout, or W64DEVKIT_BIN / WEBVIEW2_INCLUDE / WEBVIEW2_LOADER.
"""
from __future__ import annotations
import argparse, ctypes, ctypes.wintypes as W, functools, http.server, os, shutil, subprocess, sys, tempfile, threading, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
OUT = ROOT / 'build' / 'crash-screenshare'
TOOLS = Path(os.environ.get('W64DEVKIT_BIN') or ROOT / 'tools' / 'w64devkit' / 'bin')
WV2_INCLUDE = Path(os.environ.get('WEBVIEW2_INCLUDE') or ROOT / 'deps' / 'webview2' / 'build' / 'native' / 'include')
WV2_LOADER = Path(os.environ.get('WEBVIEW2_LOADER') or WV2_INCLUDE.parent / 'x64' / 'WebView2Loader.dll')
MODES = ['crash-builtin', 'crash-ours', 'crash-off', 'crash-kill', 'share', 'limit', 'render', 'hang', 'hang-recover', 'hang-reload', 'hang-cadence', 'crash-twice',
         'picker', 'hang-again']
HIDDEN_MODES = {'picker'}   # the engine's picker may open: never on the user's desktop

k32 = ctypes.WinDLL('kernel32', use_last_error=True)
ntdll = ctypes.WinDLL('ntdll')
k32.CreateJobObjectW.restype = W.HANDLE
k32.CreateJobObjectW.argtypes = [ctypes.c_void_p, W.LPCWSTR]
k32.SetInformationJobObject.argtypes = [W.HANDLE, ctypes.c_int, ctypes.c_void_p, W.DWORD]
k32.AssignProcessToJobObject.argtypes = [W.HANDLE, W.HANDLE]
k32.CloseHandle.argtypes = [W.HANDLE]
ntdll.NtResumeProcess.argtypes = [W.HANDLE]


class IoCounters(ctypes.Structure):
    _fields_ = [(n, ctypes.c_ulonglong) for n in ('r', 'w', 'o', 'rb', 'wb', 'ob')]


class BasicLimit(ctypes.Structure):
    _fields_ = [('PerProcessUserTimeLimit', ctypes.c_longlong), ('PerJobUserTimeLimit', ctypes.c_longlong),
                ('LimitFlags', W.DWORD), ('MinimumWorkingSetSize', ctypes.c_size_t), ('MaximumWorkingSetSize', ctypes.c_size_t),
                ('ActiveProcessLimit', W.DWORD), ('Affinity', ctypes.c_size_t), ('PriorityClass', W.DWORD), ('SchedulingClass', W.DWORD)]


class ExtendedLimit(ctypes.Structure):
    _fields_ = [('BasicLimitInformation', BasicLimit), ('IoInfo', IoCounters), ('ProcessMemoryLimit', ctypes.c_size_t),
                ('JobMemoryLimit', ctypes.c_size_t), ('PeakProcessMemoryUsed', ctypes.c_size_t), ('PeakJobMemoryUsed', ctypes.c_size_t)]


def kill_on_close_job():
    job = k32.CreateJobObjectW(None, None)
    if not job:
        raise OSError('CreateJobObject failed')
    info = ExtendedLimit(); info.BasicLimitInformation.LimitFlags = 0x2000   # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
    if not k32.SetInformationJobObject(job, 9, ctypes.byref(info), ctypes.sizeof(info)):
        raise OSError('SetInformationJobObject failed')
    return job


class Quiet(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def end_headers(self):
        self.send_header('Cache-Control', 'no-store')
        super().end_headers()


class STARTUPINFO(ctypes.Structure):
    _fields_ = [('cb', W.DWORD), ('lpReserved', W.LPWSTR), ('lpDesktop', W.LPWSTR), ('lpTitle', W.LPWSTR), ('dwX', W.DWORD), ('dwY', W.DWORD),
                ('dwXSize', W.DWORD), ('dwYSize', W.DWORD), ('dwXCountChars', W.DWORD), ('dwYCountChars', W.DWORD), ('dwFillAttribute', W.DWORD),
                ('dwFlags', W.DWORD), ('wShowWindow', W.WORD), ('cbReserved2', W.WORD), ('lpReserved2', ctypes.c_void_p),
                ('hStdInput', W.HANDLE), ('hStdOutput', W.HANDLE), ('hStdError', W.HANDLE)]


class PROCESS_INFORMATION(ctypes.Structure):
    _fields_ = [('hProcess', W.HANDLE), ('hThread', W.HANDLE), ('dwProcessId', W.DWORD), ('dwThreadId', W.DWORD)]


def run_hidden(cmd: list[str], job, out_path: Path, seconds: int) -> None:
    """`cmd` on a new desktop of the session that is never displayed, stdout/stderr into `out_path`, inside `job`."""
    u32 = ctypes.WinDLL('user32', use_last_error=True)
    u32.CreateDesktopW.restype = W.HANDLE
    u32.CreateDesktopW.argtypes = [W.LPCWSTR, W.LPCWSTR, ctypes.c_void_p, W.DWORD, W.DWORD, ctypes.c_void_p]
    u32.CloseDesktop.argtypes = [W.HANDLE]
    name = f'crash-probe-{os.getpid()}'
    desk = u32.CreateDesktopW(name, None, None, 0, 0x10000000, None)   # GENERIC_ALL
    if not desk:
        raise OSError(f'CreateDesktop failed ({ctypes.get_last_error()})')
    import msvcrt
    with open(out_path, 'wb') as f:
        handle = msvcrt.get_osfhandle(f.fileno()); os.set_handle_inheritable(handle, True)
        si = STARTUPINFO(); si.cb = ctypes.sizeof(si); si.lpDesktop = 'WinSta0\\' + name
        si.dwFlags = 0x100; si.hStdOutput = handle; si.hStdError = handle   # STARTF_USESTDHANDLES
        pi = PROCESS_INFORMATION()
        line = ctypes.create_unicode_buffer(subprocess.list2cmdline(cmd))
        if not k32.CreateProcessW(None, line, None, None, True, 0x00000004 | 0x08000000, None, str(OUT), ctypes.byref(si), ctypes.byref(pi)):   # suspended, no console
            raise OSError(f'CreateProcess failed ({ctypes.get_last_error()})')
        if not k32.AssignProcessToJobObject(job, W.HANDLE(pi.hProcess)):
            k32.TerminateProcess(W.HANDLE(pi.hProcess), 1); raise OSError('AssignProcessToJobObject failed')
        k32.ResumeThread(W.HANDLE(pi.hThread)); k32.CloseHandle(W.HANDLE(pi.hThread))
        k32.WaitForSingleObject(W.HANDLE(pi.hProcess), seconds * 1000); k32.TerminateProcess(W.HANDLE(pi.hProcess), 1); k32.CloseHandle(W.HANDLE(pi.hProcess))
    for _ in range(40):   # the desktop closes once its last process (the engine's too) is gone
        if u32.CloseDesktop(desk):
            break
        time.sleep(0.25)


def build() -> Path:
    OUT.mkdir(parents=True, exist_ok=True)
    exe = OUT / 'engine_probe_host.exe'
    cmd = [str(TOOLS / 'g++.exe'), '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-Wno-missing-field-initializers', '-Wno-unknown-pragmas', '-Wno-misleading-indentation', '-static',
           '-municode', '-Isrc', f'-I{WV2_INCLUDE}', 'tests/crash-screenshare/engine_probe_host.cpp', '-o', str(exe), '-lole32', '-luuid', '-luser32', '-lgdi32']
    if subprocess.run(cmd, cwd=ROOT).returncode:
        sys.exit('FAILED: compile engine_probe_host')
    shutil.copy2(WV2_LOADER, OUT / 'WebView2Loader.dll')
    return exe


def run_host(exe: Path, url: str, url2: str, mode: str) -> list[str]:
    profile = Path(tempfile.mkdtemp(prefix='crash-share-probe-'))
    job = kill_on_close_job()
    try:
        extra = ['--fonts', str(ROOT / 'assets' / 'fonts'), '--shot', str(OUT / f'{mode}.png')] if mode in ('crash-ours', 'crash-kill', 'render', 'crash-twice', 'picker') else []
        if mode in HIDDEN_MODES:
            out_path = OUT / f'{mode}-out.txt'
            run_hidden([str(exe), '--url', url, '--url2', url2, '--udf', str(profile), '--mode', mode, *extra], job, out_path, 120)
            return out_path.read_bytes().decode('utf-8', 'replace').splitlines()
        proc = subprocess.Popen([str(exe), '--url', url, '--url2', url2, '--udf', str(profile), '--mode', mode, *extra], cwd=OUT,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, creationflags=0x4)   # CREATE_SUSPENDED: in the job first
        handle = W.HANDLE(int(proc._handle))
        if not k32.AssignProcessToJobObject(job, handle):
            proc.kill(); raise OSError('AssignProcessToJobObject failed')
        ntdll.NtResumeProcess(handle)
        try:
            out, _ = proc.communicate(timeout=120)
        except subprocess.TimeoutExpired:
            proc.kill(); out, _ = proc.communicate()
    finally:
        k32.CloseHandle(job)   # KILL_ON_JOB_CLOSE: the engine's processes end here too
        for _ in range(20):
            shutil.rmtree(profile, ignore_errors=True)
            if not profile.exists():
                break
            time.sleep(0.5)
    return out.decode('utf-8', 'replace').splitlines()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--modes', default=','.join(MODES))
    modes = [m for m in ap.parse_args().modes.split(',') if m]
    exe = build()
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), functools.partial(Quiet, directory=str(HERE / 'site')))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    port = server.server_address[1]
    a = f'http://127.0.0.1:{port}/a.html?frame=http://localhost:{port}/frame.html'
    b = f'http://127.0.0.1:{port}/b.html'
    facts: dict[str, list[str]] = {}
    try:
        for mode in modes:
            lines = run_host(exe, a, b, mode)
            facts[mode] = lines
            print(f'== {mode}')
            for line in lines:
                print('  ' + line)
    finally:
        server.shutdown()
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'engine-facts.txt').write_text('\n'.join(f'[{m}] {l}' for m, ls in facts.items() for l in ls) + '\n', encoding='utf-8')

    failures = []
    def has(mode: str, *needles: str) -> bool:
        return any(all(n in l for n in needles) for l in facts.get(mode, []))
    if 'crash-builtin' in facts:
        if not has('crash-builtin', 'FACT failed kind=1'): failures.append('crash-builtin: no RENDER_PROCESS_EXITED')
        if not has('crash-builtin', 'engine.after.document', '"title":"B"'): failures.append('crash-builtin: Reload() did not bring page B back')
    if 'crash-twice' in facts:   # our page's own renderer dies: Reload() of its entry brings OUR page back, not B
        if not has('crash-twice', 'twice.document', 'Cette page a cess'): failures.append('crash-twice: reloading our entry after its renderer died did not bring our page back')
        if not has('crash-twice', 'twice.history index=2'): failures.append('crash-twice: the history changed (our entry should stay where it was)')
    for mode in ('crash-ours', 'crash-kill'):
        if mode in facts:
            if not has(mode, 'FACT failed kind=1'): failures.append(f'{mode}: no RENDER_PROCESS_EXITED')
            if not has(mode, 'ours.document', 'Cette page a cessé de fonctionner'): failures.append(f'{mode}: our page did not show')
            if not has(mode, 'ours.recharger.document', '"title":"B"'): failures.append(f'{mode}: going back to the crashed entry did not bring page B back')
    if 'share' in facts:
        if not has('share', 'share.event'): failures.append('share: ScreenCaptureStarting never fired')
        if not has('share', 'share.page.gesture', 'NotAllowedError'): failures.append('share: Cancel did not give the page a NotAllowedError')
    if 'picker' in facts:
        if not has('picker', 'picker.choose'): failures.append('picker: ScreenCaptureStarting never fired')
        if not has('picker', 'picker.click posted'): failures.append("picker: the engine's window was not found in ours")
        if not has('picker', 'picker.page', 'NotAllowedError'): failures.append("picker: « Cancel » did not close the engine's picker with a refusal")
    if 'hang-again' in facts:
        if not has('hang-again', 'hang.unresponsive #1'): failures.append('hang-again: no first « ne répond plus » report')
        if has('hang-again', 'hang.unresponsive #2'): print('NOTE: hang-again: the engine reported the frozen page again (src/crash-page.h AfterWait still asks itself)')
    for f in failures:
        print('FAILED: ' + f)
    print('engine probe: ' + ('FAILED' if failures else 'OK') + f' (facts: {(OUT / "engine-facts.txt").relative_to(ROOT)})')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
