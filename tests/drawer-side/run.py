"""#143: the drawers open on the tab panel's side, and a button's tooltip never shows over its own open menu.
Screen-free checks.

    python -I tests/drawer-side/run.py            # report in build/drawer-side/report.txt

1. tests/drawer_side_test.cpp: pure geometry (docked rectangle, slide direction, keyed strip, mirrored region, the side
   layout's dock and page box) at 100 / 125 / 150 / 175 %, both sides, and a mirrored (right-to-left) owner window
   (created hidden, never shown);
2. tests/drawer-side/drawer_side_host_test.cpp: src/main.cpp itself with the real drawers, the side Focus column, the
   narrow window's column over the page and the toolbar tooltip, on a Windows desktop of its own that is never
   displayed (nothing reaches the screen, the mouse, the keyboard or the foreground). A stand-in WebView2 controller:
   no WebView2, no network, no profile (its files go to build/drawer-side). Run in a job object (KILL_ON_JOB_CLOSE).

Tools: tools/w64devkit, deps/webview2, deps/thorvg-1.1.3, build/lottie-backend of this checkout, or W64DEVKIT_BIN /
WEBVIEW2_INCLUDE / THORVG_INCLUDE / THORVG_LIB (an Orca worktree has none of them: point them at the main checkout's).
"""
from __future__ import annotations
import ctypes, ctypes.wintypes as W, os, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'build' / 'drawer-side'
TOOLS = Path(os.environ.get('W64DEVKIT_BIN') or ROOT / 'tools' / 'w64devkit' / 'bin')
WV2_INCLUDE = Path(os.environ.get('WEBVIEW2_INCLUDE') or ROOT / 'deps' / 'webview2' / 'build' / 'native' / 'include')
TVG_INCLUDE = Path(os.environ.get('THORVG_INCLUDE') or ROOT / 'deps' / 'thorvg-1.1.3' / 'inc')
THORVG = Path(os.environ.get('THORVG_LIB') or ROOT / 'build' / 'lottie-backend' / 'src' / 'libthorvg-1.a')
STRICT = ['-std=c++17', '-static', '-Wall', '-Wextra', '-Werror', '-Wno-missing-field-initializers']
HOST = ['-std=c++17', '-O1', '-Wall', '-Wextra', '-Wno-unknown-pragmas', '-Wno-cast-function-type', '-Wno-misleading-indentation',
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
    """Started suspended, put in a job that kills it with the job, then resumed: only what this run started is stopped."""
    job = k32.CreateJobObjectW(None, None)
    info = ExtendedLimit(); info.BasicLimitInformation.LimitFlags = 0x2000   # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
    if not job or not k32.SetInformationJobObject(job, 9, ctypes.byref(info), ctypes.sizeof(info)):
        raise OSError('job object unavailable')
    try:
        proc = subprocess.Popen(cmd, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, creationflags=0x4)   # CREATE_SUSPENDED
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
    # --compile-plan <file> / --prebuilt: its two compiles join tests/update-integration/run_all.py's pool (compile_plan.py)
    sys.path.insert(0, str(ROOT / 'tests' / 'update-integration'))
    import compile_plan as P
    plan, prebuilt = P.requested(sys.argv), P.prebuilt(sys.argv)
    OUT.mkdir(parents=True, exist_ok=True)
    report: list[str] = []
    pure = OUT / 'drawer_side_test.exe'
    host = OUT / 'drawer_side_host_test.exe'
    pure_cmd = [str(TOOLS / 'g++.exe'), *STRICT, 'tests/drawer_side_test.cpp', '-o', str(pure), '-lgdi32', '-luser32', '-ldwmapi']
    host_cmd = [str(TOOLS / 'g++.exe'), *HOST, 'tests/drawer-side/drawer_side_host_test.cpp', 'src/native-lottie.cpp', str(THORVG), '-o', str(host), *UI_LIBS]
    if plan:
        return P.write(plan, [('drawer_side_test', pure_cmd, ROOT), ('drawer_side_host_test (src/main.cpp)', host_cmd, ROOT)])
    if not prebuilt:
        print('== compile drawer_side_test', flush=True)
        if subprocess.run(pure_cmd, cwd=ROOT).returncode:
            print('FAILED: compile drawer_side_test'); return 1
    code, text = run_in_job([str(pure)], 120)
    print(text); report.append(text)
    if code:
        (OUT / 'report.txt').write_text('\n'.join(report), encoding='utf-8'); print(f'FAILED ({code}): drawer_side_test'); return code
    if not prebuilt:
        print('== compile drawer_side_host_test (src/main.cpp)', flush=True)
        if subprocess.run(host_cmd, cwd=ROOT).returncode:
            print('FAILED: compile drawer_side_host_test'); return 1
    code, text = run_in_job([str(host), str(ROOT), str(OUT)], 240)
    print(text); report.append(text)
    (OUT / 'report.txt').write_text('\n'.join(report), encoding='utf-8')
    if code:
        print(f'FAILED ({code}): drawer_side_host_test'); return code
    print('PASS: drawer side (pure + host)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
