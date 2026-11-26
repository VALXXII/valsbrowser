"""Fullscreen in one step (user report 25/11: the window « jumps » several times entering and leaving a video's
fullscreen): screen-free check.

    python -I tests/fullscreen/run.py            # report in build/fullscreen-jump/report.txt

Builds tests/fullscreen/fullscreen_jump_test.cpp (src/main.cpp's own window procedure, a stand-in WebView2 controller)
and runs it in a job object (KILL_ON_JOB_CLOSE). Its window lives on a Windows desktop of its own that is never
displayed: nothing reaches the screen, the mouse, the keyboard or the foreground. No WebView2, no network, no profile.

Tools: tools/w64devkit and deps/webview2 / deps/thorvg-1.1.3 / build/lottie-backend of this checkout, or
W64DEVKIT_BIN / WEBVIEW2_INCLUDE / THORVG_INCLUDE / THORVG_LIB (an Orca worktree has none of them: point them at the
main checkout's copies).
"""
from __future__ import annotations
import ctypes, ctypes.wintypes as W, os, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'build' / 'fullscreen-jump'
TOOLS = Path(os.environ.get('W64DEVKIT_BIN') or ROOT / 'tools' / 'w64devkit' / 'bin')
WV2_INCLUDE = Path(os.environ.get('WEBVIEW2_INCLUDE') or ROOT / 'deps' / 'webview2' / 'build' / 'native' / 'include')
TVG_INCLUDE = Path(os.environ.get('THORVG_INCLUDE') or ROOT / 'deps' / 'thorvg-1.1.3' / 'inc')
THORVG = Path(os.environ.get('THORVG_LIB') or ROOT / 'build' / 'lottie-backend' / 'src' / 'libthorvg-1.a')
FLAGS = ['-std=c++17', '-O1', '-Wall', '-Wextra', '-Wno-unknown-pragmas', '-Wno-cast-function-type', '-Wno-misleading-indentation',
         '-Wno-missing-field-initializers', '-DTVG_STATIC', f'-I{TVG_INCLUDE}', f'-I{WV2_INCLUDE}', '-static', '-municode']
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


def run_in_job(cmd: list[str], timeout: int) -> tuple[int, str]:
    job = k32.CreateJobObjectW(None, None)
    info = ExtendedLimit(); info.BasicLimitInformation.LimitFlags = 0x2000   # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
    if not job or not k32.SetInformationJobObject(job, 9, ctypes.byref(info), ctypes.sizeof(info)):
        raise OSError('job object unavailable')
    try:
        proc = subprocess.Popen(cmd, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, creationflags=0x4)   # suspended until in the job
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
        k32.CloseHandle(job)


def main() -> int:
    OUT.mkdir(parents=True, exist_ok=True)
    exe = OUT / 'fullscreen_jump_test.exe'
    cmd = [str(TOOLS / 'g++.exe'), *FLAGS, 'tests/fullscreen/fullscreen_jump_test.cpp', 'src/native-lottie.cpp', str(THORVG), '-o', str(exe), *UI_LIBS]
    print('== compile fullscreen_jump_test', flush=True)
    if subprocess.run(cmd, cwd=ROOT).returncode:
        print('FAILED: compile'); return 1
    code, text = run_in_job([str(exe), str(ROOT), str(OUT)], 180)
    sys.stdout.write(text)
    (OUT / 'report.txt').write_text(text, encoding='utf-8')
    print(f'== exit {code}; report {OUT / "report.txt"}, log {OUT / "fullscreen.log"}')
    return code


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8')
    sys.exit(main())
