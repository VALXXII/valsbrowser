"""Workers of a page and of its cross-site frame must run while the password vault is attached (no window shown).

    python -I tests/frame-workers/run.py

Why: the vault attaches to every frame of another site before it runs (Target.setAutoAttach with
waitForDebuggerOnStart, src/password-vault.h AutoAttachParams). The engine then also holds the workers such a frame
starts; a worker left out of the attach filter is never announced to the browser, so it stayed paused for good.
Cloudflare Turnstile runs its check in workers of its frame: on polar.sh it never finished and the page said
« Verification failed. Please try again. ».

What it does: serves tests/frame-workers/site from a local server only (127.0.0.1 page, localhost frame: two
sites, so the frame gets its own renderer), then runs frame_workers_host.exe three times, each with a fresh temporary
profile, inside a job object with KILL_ON_JOB_CLOSE. The host's window is never shown (off every monitor, never
activated): nothing appears on screen, the mouse and focus are left alone. No real site, no real profile.
  1. --attach none: every kind of worker the engine runs here answers (the reference set);
  2. --attach iframe-only: the vault's filter before the fix must reproduce the bug (a frame worker stays silent),
     else this test could not see it;
  3. --attach vault: the vault's real parameters (Vault::AutoAttachParams): every worker of the reference set answers.

Tools: tools/w64devkit and deps/webview2 of this checkout, or W64DEVKIT_BIN / WEBVIEW2_INCLUDE / WEBVIEW2_LOADER
(another checkout's copies; never junctions).
"""
from __future__ import annotations
import ctypes, ctypes.wintypes as W, functools, http.server, json, os, shutil, subprocess, sys, tempfile, threading, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
OUT = ROOT / 'build' / 'frame-workers'
TOOLS = Path(os.environ.get('W64DEVKIT_BIN') or ROOT / 'tools' / 'w64devkit' / 'bin')
WV2_INCLUDE = Path(os.environ.get('WEBVIEW2_INCLUDE') or ROOT / 'deps' / 'webview2' / 'build' / 'native' / 'include')
WV2_LOADER = Path(os.environ.get('WEBVIEW2_LOADER') or WV2_INCLUDE.parent / 'x64' / 'WebView2Loader.dll')

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


def build() -> Path:
    OUT.mkdir(parents=True, exist_ok=True)
    exe = OUT / 'frame_workers_host.exe'
    cmd = [str(TOOLS / 'g++.exe'), '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-Wno-missing-field-initializers', '-Wno-unknown-pragmas', '-static',
           '-municode', '-Isrc', f'-I{WV2_INCLUDE}', 'tests/frame-workers/frame_workers_host.cpp', '-o', str(exe),
           '-lole32', '-luuid', '-luser32', '-lcrypt32', '-lbcrypt']
    if subprocess.run(cmd, cwd=ROOT).returncode:
        sys.exit('FAILED: compile frame_workers_host')
    shutil.copy2(WV2_LOADER, OUT / 'WebView2Loader.dll')
    return exe


def run_host(exe: Path, url: str, attach: str) -> dict:
    profile = Path(tempfile.mkdtemp(prefix='frame-workers-'))
    job = kill_on_close_job()
    try:
        proc = subprocess.Popen([str(exe), '--url', url, '--udf', str(profile), '--attach', attach, '--seconds', '12'], cwd=OUT,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, creationflags=0x4)   # CREATE_SUSPENDED: in the job first
        handle = W.HANDLE(int(proc._handle))
        if not k32.AssignProcessToJobObject(job, handle):
            proc.kill(); raise OSError('AssignProcessToJobObject failed')
        ntdll.NtResumeProcess(handle)
        try:
            out, _ = proc.communicate(timeout=90)
        except subprocess.TimeoutExpired:
            proc.kill(); out, _ = proc.communicate()
    finally:
        k32.CloseHandle(job)   # KILL_ON_JOB_CLOSE: the engine's processes end here too
        for _ in range(20):
            shutil.rmtree(profile, ignore_errors=True)
            if not profile.exists():
                break
            time.sleep(0.5)
    text = out.decode('utf-8', 'replace')
    attached = [line[9:] for line in text.splitlines() if line.startswith('ATTACHED ')]
    results = {}
    for line in text.splitlines():
        if line.startswith('RESULTS '):
            raw = json.loads(line[8:])
            results = json.loads(raw) if isinstance(raw, str) else raw
    print(f'  --attach {attach}: attached {attached or "nothing"}')
    for key in sorted(results):
        print(f'    {key}: {"answered" if results[key] else "FAILED"}')
    if any(line.startswith(('ENGINE FAILED', 'LOAD FAILED', 'WebView2Loader', 'DEVTOOLS FAILED')) for line in text.splitlines()):
        print(text)
    return {'results': results, 'attached': attached}


def main() -> int:
    exe = build()
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), functools.partial(Quiet, directory=str(HERE / 'site')))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    url = f'http://127.0.0.1:{server.server_address[1]}/top.html'
    failures = []
    try:
        base = run_host(exe, url, 'none')
        reference = {k for k, ok in base['results'].items() if ok}
        if not any(k.startswith('frame:') for k in reference) or 'frame:blob-worker' not in reference:
            failures.append('without the vault, the cross-site frame workers did not answer: the environment cannot run this test')
        old = run_host(exe, url, 'iframe-only')
        if 'iframe' not in ' '.join(old['attached']):
            failures.append('the frame was not attached as a separate target (no cross-site renderer): the test cannot see the bug')
        silent = sorted(k for k in reference if not old['results'].get(k))
        if not silent:
            failures.append('the filter before the fix (iframes only) no longer holds any worker: the reference does not reproduce the bug')
        else:
            print(f'  reference reproduced: with the old filter {", ".join(silent)} never answered')
        fixed = run_host(exe, url, 'vault')
        missing = sorted(k for k in reference if not fixed['results'].get(k))
        if missing:
            failures.append('with the vault attached, these workers never answered: ' + ', '.join(missing))
    finally:
        server.shutdown()
    for f in failures:
        print('FAILED: ' + f)
    print('frame workers: ' + ('FAILED' if failures else f'OK ({len(reference)} workers answered with the vault attached)'))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
