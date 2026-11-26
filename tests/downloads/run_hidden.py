"""Runs one command on a Windows desktop that is never displayed (nothing on screen, no focus taken), in a job object.

    python tests/downloads/run_hidden.py <output file> -- <command> [args...]

For the native downloads checks in the daytime: tests/downloads/download_panel_test.exe shows its drawer (cloaked, off every
monitor) with ShowWindow, which may activate it; on its own desktop it cannot touch the shared screen or the foreground.
Its stdout and stderr go to <output file>; the exit code is the command's. Everything it starts is in a job object
(KILL_ON_JOB_CLOSE) and is stopped through it only.
"""
from __future__ import annotations
import ctypes, ctypes.wintypes as W, os, subprocess, sys

k32 = ctypes.WinDLL('kernel32', use_last_error=True)
u32 = ctypes.WinDLL('user32', use_last_error=True)
u32.CreateDesktopW.restype = W.HANDLE
k32.CreateJobObjectW.restype = W.HANDLE
k32.CreateFileW.restype = W.HANDLE
k32.CreateFileW.argtypes = [W.LPCWSTR, W.DWORD, W.DWORD, ctypes.c_void_p, W.DWORD, W.DWORD, W.HANDLE]


class SA(ctypes.Structure):
    _fields_ = [('nLength', W.DWORD), ('lpSecurityDescriptor', ctypes.c_void_p), ('bInheritHandle', W.BOOL)]


class SI(ctypes.Structure):
    _fields_ = [('cb', W.DWORD), ('lpReserved', W.LPWSTR), ('lpDesktop', W.LPWSTR), ('lpTitle', W.LPWSTR), ('dwX', W.DWORD), ('dwY', W.DWORD),
                ('dwXSize', W.DWORD), ('dwYSize', W.DWORD), ('dwXCountChars', W.DWORD), ('dwYCountChars', W.DWORD), ('dwFillAttribute', W.DWORD),
                ('dwFlags', W.DWORD), ('wShowWindow', W.WORD), ('cbReserved2', W.WORD), ('lpReserved2', ctypes.c_void_p),
                ('hStdInput', W.HANDLE), ('hStdOutput', W.HANDLE), ('hStdError', W.HANDLE)]


class PI(ctypes.Structure):
    _fields_ = [('hProcess', W.HANDLE), ('hThread', W.HANDLE), ('dwProcessId', W.DWORD), ('dwThreadId', W.DWORD)]


class BasicLimit(ctypes.Structure):
    _fields_ = [('a', ctypes.c_longlong), ('b', ctypes.c_longlong), ('LimitFlags', W.DWORD), ('c', ctypes.c_size_t), ('d', ctypes.c_size_t),
                ('e', W.DWORD), ('f', ctypes.c_size_t), ('g', W.DWORD), ('h', W.DWORD)]


class ExtendedLimit(ctypes.Structure):
    _fields_ = [('Basic', BasicLimit), ('Io', ctypes.c_ulonglong * 6), ('i', ctypes.c_size_t), ('j', ctypes.c_size_t), ('k', ctypes.c_size_t), ('l', ctypes.c_size_t)]


def main() -> int:
    if len(sys.argv) < 4 or sys.argv[2] != '--':
        print(__doc__); return 2
    out, command = os.path.abspath(sys.argv[1]), sys.argv[3:]
    desk = f'native-tests-{os.getpid()}'
    if not u32.CreateDesktopW(desk, None, None, 0, 0x10000000, None):
        raise OSError(f'CreateDesktopW failed {ctypes.get_last_error()}')
    job = k32.CreateJobObjectW(None, None); info = ExtendedLimit(); info.Basic.LimitFlags = 0x2000
    k32.SetInformationJobObject(W.HANDLE(job), 9, ctypes.byref(info), ctypes.sizeof(info))
    sa = SA(ctypes.sizeof(SA), None, True)
    log = k32.CreateFileW(out, 0x40000000, 0x1 | 0x2, ctypes.byref(sa), 2, 0x80, None)   # GENERIC_WRITE, CREATE_ALWAYS, inheritable
    si = SI(); si.cb = ctypes.sizeof(si); si.lpDesktop = 'WinSta0\\' + desk; si.dwFlags = 0x100; si.hStdOutput = si.hStdError = log
    pi = PI()
    if not k32.CreateProcessW(None, ctypes.create_unicode_buffer(subprocess.list2cmdline(command)), None, None, True, 0x4 | 0x08000000, None, None,
                              ctypes.byref(si), ctypes.byref(pi)):
        raise OSError(f'CreateProcessW failed {ctypes.get_last_error()}')
    k32.AssignProcessToJobObject(W.HANDLE(job), W.HANDLE(pi.hProcess)); k32.ResumeThread(W.HANDLE(pi.hThread))
    k32.WaitForSingleObject(W.HANDLE(pi.hProcess), 0xFFFFFFFF)
    code = W.DWORD(); k32.GetExitCodeProcess(W.HANDLE(pi.hProcess), ctypes.byref(code))
    k32.CloseHandle(W.HANDLE(log)); k32.CloseHandle(W.HANDLE(pi.hProcess)); k32.CloseHandle(W.HANDLE(pi.hThread))
    k32.TerminateJobObject(W.HANDLE(job), 1); k32.CloseHandle(W.HANDLE(job))
    print(open(out, encoding='utf-8', errors='replace').read(), end='')
    return code.value


if __name__ == '__main__':
    sys.exit(main())
