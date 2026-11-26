"""Our print window (#121): screen-free checks. No window is ever shown, focused or activated.

    python -I tests/print/run.py [--skip-engine]

  1. print_dialog_test.exe (tests/print/print_dialog_test.cpp): page ranges, the window's logic on hidden windows,
     offscreen renders compared pixel by pixel with Paper's export of the board (tests/print/paper/), the preview
     worker on generated PDFs (a 300-page one included).
  2. print_motion_test.exe (tests/print/print_motion_test.cpp): src/main.cpp's own motion loop (ScheduleMotion, the
     motion timer's InvalidateMotion) runs the window's entrance and exit while nothing else in the browser moves
     (release 25/11: the loop never gave the fade a frame, the window and its scrim stayed at alpha 0).
  3. print_pipeline_host.exe (tests/print/print_pipeline_host.cpp): the real engine, in a WebView2 host whose window is
     never shown, on local pages served from 127.0.0.1 only, with a fresh temporary profile: PrintToPdfStream with
     the window's settings (portrait / landscape, page ranges, backgrounds, A4), the preview worker on the engine's
     PDFs, a very long page (time measured, UI thread never blocked), and window.print() of the top document
     reaching the browser as a message (our print script) while a frame's stays the frame's.

Every process runs in a job object with KILL_ON_JOB_CLOSE. No printer is involved: nothing is ever printed.
Outputs (renders, diff images, engine PDFs): build/print-tests/.

Tools: tools/w64devkit and deps/webview2 of this checkout, or W64DEVKIT_BIN / WEBVIEW2_INCLUDE / WEBVIEW2_LOADER.
"""
from __future__ import annotations
import argparse, ctypes, ctypes.wintypes as W, functools, http.server, os, shutil, subprocess, sys, tempfile, threading, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
OUT = ROOT / 'build' / 'print-tests'
TOOLS = Path(os.environ.get('W64DEVKIT_BIN') or ROOT / 'tools' / 'w64devkit' / 'bin')
WV2_INCLUDE = Path(os.environ.get('WEBVIEW2_INCLUDE') or ROOT / 'deps' / 'webview2' / 'build' / 'native' / 'include')
WV2_LOADER = Path(os.environ.get('WEBVIEW2_LOADER') or WV2_INCLUDE.parent / 'x64' / 'WebView2Loader.dll')
THORVG = ROOT / 'build' / 'lottie-backend' / 'src' / 'libthorvg-1.a'
FLAGS = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-unknown-pragmas', '-Wno-cast-function-type', '-Wno-misleading-indentation',
         '-Wno-missing-field-initializers', '-DTVG_STATIC', f'-I{ROOT / "deps" / "thorvg-1.1.3" / "inc"}', f'-I{WV2_INCLUDE}', '-static', '-municode']
UI_LIBS = ['-lole32', '-luuid', '-lcomctl32', '-lshlwapi', '-lgdi32', '-ld2d1', '-ldwrite', '-lwindowscodecs', '-ldwmapi', '-loleaut32', '-lshell32', '-lmsimg32']

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


def run_in_job(cmd: list[str], timeout: int, cwd: Path) -> tuple[int, str]:
    job = k32.CreateJobObjectW(None, None)
    info = ExtendedLimit(); info.BasicLimitInformation.LimitFlags = 0x2000   # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
    if not job or not k32.SetInformationJobObject(job, 9, ctypes.byref(info), ctypes.sizeof(info)):
        raise OSError('job object unavailable')
    try:
        proc = subprocess.Popen(cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, creationflags=0x4)   # suspended until in the job
        handle = W.HANDLE(int(proc._handle))
        if not k32.AssignProcessToJobObject(job, handle):
            proc.kill(); raise OSError('AssignProcessToJobObject failed')
        ntdll.NtResumeProcess(handle)
        try:
            out, _ = proc.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            proc.kill(); out, _ = proc.communicate(); return 124, out.decode('utf-8', 'replace') + '\nTIMEOUT'
        return proc.returncode, out.decode('utf-8', 'replace')
    finally:
        k32.CloseHandle(job)   # the engine's processes end here too


def compile_exe(name: str, sources: list[str], libs: list[str], extra: tuple[str, ...] = ()) -> Path:
    OUT.mkdir(parents=True, exist_ok=True)
    exe = OUT / f'{name}.exe'
    cmd = [str(TOOLS / 'g++.exe'), *FLAGS, *extra, *sources, '-o', str(exe), *libs]
    print(f'== compile {name}', flush=True)
    if subprocess.run(cmd, cwd=ROOT).returncode:
        sys.exit(f'FAILED: compile {name}')
    return exe


class Quiet(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def end_headers(self):
        self.send_header('Cache-Control', 'no-store')
        super().end_headers()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--skip-engine', action='store_true', help='only the dialog / worker test (no WebView2)')
    args = ap.parse_args()
    failed = []
    exe = compile_exe('print_dialog_test', ['tests/print/print_dialog_test.cpp', 'src/native-lottie.cpp', str(THORVG)], UI_LIBS)
    code, text = run_in_job([str(exe), str(ROOT), str(OUT)], 180, ROOT)
    print(text, end='')
    if code:
        failed.append('print_dialog_test')
    # The whole browser (src/main.cpp) at -O0: the loop's logic, not its speed, and a few minutes less to compile.
    motion = compile_exe('print_motion_test', ['tests/print/print_motion_test.cpp', 'src/native-lottie.cpp', str(THORVG)], UI_LIBS, ('-O0',))
    code, text = run_in_job([str(motion), str(ROOT)], 120, ROOT)
    print(text, end='')
    if code:
        failed.append('print_motion_test')
    if not args.skip_engine:
        host = compile_exe('print_pipeline_host', ['tests/print/print_pipeline_host.cpp'], UI_LIBS)
        shutil.copy2(WV2_LOADER, OUT / 'WebView2Loader.dll')
        server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), functools.partial(Quiet, directory=str(HERE / 'site')))
        threading.Thread(target=server.serve_forever, daemon=True).start()
        base = f'http://127.0.0.1:{server.server_address[1]}'
        profile = Path(tempfile.mkdtemp(prefix='print-pipeline-'))
        try:
            code, text = run_in_job([str(host), '--base', base, '--udf', str(profile), '--out', str(OUT)], 240, OUT)
            print(text, end='')
            if code:
                failed.append('print_pipeline_host')
        finally:
            server.shutdown()
            for _ in range(20):
                shutil.rmtree(profile, ignore_errors=True)
                if not profile.exists():
                    break
                time.sleep(0.5)
    print('print window (screen-free): ' + ('FAILED: ' + ', '.join(failed) if failed else 'OK'))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
