"""Engine facts for downloads the engine does not let finish (real WebView2, no browser code, local server only).

    python tests/downloads/download_engine_probe.py --out <dir> [--only name,name] [--night]

Builds tests/downloads/download_engine_probe_host.cpp (a WebView2 host whose window is never shown: nothing on screen,
no focus taken) and runs one case per fresh temporary profile. Each case prints the host's FACT lines: DownloadStarting,
SaveFileSecurityCheckStarting, every StateChanged (state, interrupt reason, bytes, ResultFilePath), every change of the
download folder (where the bytes really are), « held » (every byte in, still in progress), what Cancel() does, and the
folder once the engine has exited. States: 0 in progress, 1 interrupted, 2 completed; reasons: COREWEBVIEW2_DOWNLOAD_
INTERRUPT_REASON (6 FILE_MALICIOUS, 8 FILE_BLOCKED_BY_POLICY, 9 FILE_SECURITY_CHECK_FAILED, 26 USER_CANCELED).

Cases:
  held-none, held-cancel, held-link-cancel   a fresh unsigned .exe (never seen anywhere) opened by Navigate(): the
                                             SmartScreen hold, left alone / cancelled / hard-linked then cancelled
  held-reputation-off                        the same with IsReputationCheckingRequired FALSE (this probe profile
                                             only): tells a SmartScreen hold from a file-type one
  exe-script, exe-gesture                    the .exe started by the page without / with a user gesture
  plain                                      control: an ordinary .bin
  held-restore, -aes, -late, -aes-sealed     what « Conserver » does (src/main.cpp): hard link, Cancel(), the link back
                                             at the path on StateChanged (or 3 s later), attachment services, a sealed
                                             home: does the file given back stay?
  held-other-handle(-late), held-reader-no-delete
                                             another program has the file open while the engine removes it (every sharing
                                             allowed / no delete sharing): is the removal delayed, does it take?
  insecure-bin, insecure-exe                 an http:// file on a host that is not localhost (insecure.test, mapped to
                                             127.0.0.1 by --host-resolver-rules) from a page on 127.0.0.1: not blocked
  https-page-insecure-*                      the same from an https:// page (throwaway certificate made by Git's openssl,
                                             this probe's engine ignores certificate errors): held like SmartScreen's
  eicar (--night only)                       the EICAR antivirus TEST string (harmless, standard; assembled here at run
                                             time, never stored in the repository): Microsoft Defender quarantines it
                                             and shows its own toast on screen, so it is refused in the daytime.

Isolation: every process in a job object (KILL_ON_JOB_CLOSE); a temporary profile and download folder per case under
--out; 127.0.0.1 only; no real site, no real profile, no agent.
"""
from __future__ import annotations
import argparse, ctypes, ctypes.wintypes as W, http.server, os, random, shutil, ssl, subprocess, sys, threading, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
k32 = ctypes.WinDLL('kernel32', use_last_error=True)
k32.CreateJobObjectW.restype = W.HANDLE
k32.CreateJobObjectW.argtypes = [ctypes.c_void_p, W.LPCWSTR]
k32.SetInformationJobObject.argtypes = [W.HANDLE, ctypes.c_int, ctypes.c_void_p, W.DWORD]
k32.QueryInformationJobObject.argtypes = [W.HANDLE, ctypes.c_int, ctypes.c_void_p, W.DWORD, ctypes.c_void_p]
k32.AssignProcessToJobObject.argtypes = [W.HANDLE, W.HANDLE]
k32.TerminateJobObject.argtypes = [W.HANDLE, W.UINT]
k32.CloseHandle.argtypes = [W.HANDLE]
FILES: dict[str, tuple[bytes, str]] = {}


class BasicLimit(ctypes.Structure):
    _fields_ = [('PerProcessUserTimeLimit', ctypes.c_longlong), ('PerJobUserTimeLimit', ctypes.c_longlong), ('LimitFlags', W.DWORD),
                ('MinimumWorkingSetSize', ctypes.c_size_t), ('MaximumWorkingSetSize', ctypes.c_size_t), ('ActiveProcessLimit', W.DWORD),
                ('Affinity', ctypes.c_size_t), ('PriorityClass', W.DWORD), ('SchedulingClass', W.DWORD)]


class ExtendedLimit(ctypes.Structure):
    _fields_ = [('BasicLimitInformation', BasicLimit), ('IoInfo', ctypes.c_ulonglong * 6), ('ProcessMemoryLimit', ctypes.c_size_t),
                ('JobMemoryLimit', ctypes.c_size_t), ('PeakProcessMemoryUsed', ctypes.c_size_t), ('PeakJobMemoryUsed', ctypes.c_size_t)]


class Accounting(ctypes.Structure):
    _fields_ = [('TotalUserTime', ctypes.c_longlong), ('TotalKernelTime', ctypes.c_longlong), ('ThisPeriodTotalUserTime', ctypes.c_longlong),
                ('ThisPeriodTotalKernelTime', ctypes.c_longlong), ('TotalPageFaultCount', W.DWORD), ('TotalProcesses', W.DWORD),
                ('ActiveProcesses', W.DWORD), ('TotalTerminatedProcesses', W.DWORD)]


class Job:
    """KILL_ON_JOB_CLOSE job: the host and every engine process it starts. Stopped only through this job."""
    def __init__(self):
        self.h = k32.CreateJobObjectW(None, None)
        info = ExtendedLimit(); info.BasicLimitInformation.LimitFlags = 0x2000
        if not self.h or not k32.SetInformationJobObject(self.h, 9, ctypes.byref(info), ctypes.sizeof(info)):
            raise OSError('job object')

    def add(self, proc):
        if not k32.AssignProcessToJobObject(self.h, W.HANDLE(int(proc._handle))):
            proc.kill(); raise OSError('AssignProcessToJobObject failed')

    def active(self):
        a = Accounting(); k32.QueryInformationJobObject(self.h, 1, ctypes.byref(a), ctypes.sizeof(a), None); return a.ActiveProcesses

    def drain(self, timeout):   # wait for the engine to exit by itself (it deletes what it still holds then)
        end = time.monotonic() + timeout
        while self.active() and time.monotonic() < end:
            time.sleep(.2)
        left = self.active()
        k32.TerminateJobObject(self.h, 1); k32.CloseHandle(self.h); return left


def eicar() -> bytes:
    """The EICAR antivirus test file (eicar.org): harmless, recognised by every antivirus. Assembled here so that no file
    of the repository holds it (Defender would quarantine the test script itself)."""
    parts = ['X5O!P%@AP[4\\PZX54(P^)7CC)7}$', 'EICAR-STANDARD-ANTIVIRUS-TEST-FILE!', '$H+H*']
    return ''.join(parts).encode('ascii')


class Fixture(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def do_GET(self):
        path = self.path.split('?')[0]
        if path == '/page':
            body, ctype, name = b'<!doctype html><title>PROBE</title><p>download engine probe</p>', 'text/html; charset=utf-8', None
        elif path[1:] in FILES:
            body, name = FILES[path[1:]]; ctype = 'application/octet-stream'
        else:
            self.send_response(404); self.send_header('Content-Length', '0'); self.end_headers(); return
        self.send_response(200); self.send_header('Content-Type', ctype)
        if name:
            self.send_header('Content-Disposition', f'attachment; filename="{name}"')
        self.send_header('Content-Length', str(len(body))); self.send_header('Cache-Control', 'no-store'); self.end_headers(); self.wfile.write(body)

    def log_message(self, *a):
        pass


def fresh_exe(work: Path, tag: str) -> bytes:
    """A unique, unsigned console program (never executed): its hash has no reputation anywhere."""
    src = work / f'{tag}.c'; exe = work / f'{tag}.exe'
    src.write_text('int main(void){volatile unsigned x=%du;return (int)(x&1u);}\n' % random.getrandbits(31), encoding='ascii')
    r = subprocess.run([str(ROOT / 'tools/w64devkit/bin/gcc.exe'), '-O0', str(src), '-o', str(exe)], capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError('fixture exe build failed: ' + r.stderr)
    return exe.read_bytes()


def build_host(out: Path) -> Path:
    host = out / 'host'; host.mkdir(parents=True, exist_ok=True); exe = host / 'download_engine_probe_host.exe'
    cmd = [str(ROOT / 'tools/w64devkit/bin/g++.exe'), '-std=c++17', '-O1', '-Wall', '-Wextra', '-Wno-missing-field-initializers', '-municode', '-static',
           '-I' + str(ROOT / 'deps/webview2/build/native/include'), str(ROOT / 'tests/downloads/download_engine_probe_host.cpp'),
           '-o', str(exe), '-lole32', '-luuid', '-luser32', '-lkernel32']
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError('probe host build failed:\n' + r.stderr)
    shutil.copy2(ROOT / 'deps/webview2/build/native/x64/WebView2Loader.dll', host / 'WebView2Loader.dll')
    return exe


def main() -> int:
    ap = argparse.ArgumentParser(); ap.add_argument('--out', required=True); ap.add_argument('--only', default='')
    ap.add_argument('--night', action='store_true', help='also the EICAR case (Defender shows its own toast on screen)')
    ap.add_argument('--observe', default='', help='a lingering engine file: leave (both names alone 60 s), drop (our link deleted at +6 s), '
                    'drop-visible (the same, link not hidden), drop-released (the same, the operation released at once), touch '
                    '(an unrelated file in the folder at +6 s, our link deleted at +12 s)')
    ap.add_argument('--runs', type=int, default=10, help='--observe: runs at most (a fresh .exe and profile each)')
    ap.add_argument('--lingering', type=int, default=2, help='--observe: stop after this many lingering runs')
    ap.add_argument('--hold-ms', default='4000', help='the « held » point: this long after every byte is in (the probe acts then)')
    ap.add_argument('--big', type=int, default=0, help='MB of random bytes appended to each fresh .exe (a longer antivirus scan)')
    ap.add_argument('--observe-ms', default='', help='--observe: the watch after the cancel, in ms')
    a = ap.parse_args()
    out = Path(a.out).resolve(); shutil.rmtree(out, ignore_errors=True); out.mkdir(parents=True)
    work = out / 'work'; work.mkdir()
    host = build_host(out)
    srv = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Fixture); port = srv.server_port; base = f'http://127.0.0.1:{port}'
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    insecure = f'http://insecure.test:{port}'
    # The same pages over HTTPS (a throwaway self-signed certificate made by Git's openssl, nothing installed; this
    # probe's engine ignores certificate errors): a page that is unquestionably a secure context.
    secure = None
    openssl = Path(r'C:\Program Files\Git\usr\bin\openssl.exe')
    if openssl.is_file():
        pki = out / 'pki'; pki.mkdir()
        subprocess.run([str(openssl), 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-keyout', str(pki / 'k.pem'), '-out', str(pki / 'c.pem'), '-days', '1',
                        '-subj', '/CN=127.0.0.1', '-addext', 'subjectAltName=IP:127.0.0.1'], capture_output=True)
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); ctx.load_cert_chain(str(pki / 'c.pem'), str(pki / 'k.pem'))
        tls = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Fixture); tls.socket = ctx.wrap_socket(tls.socket, server_side=True)
        threading.Thread(target=tls.serve_forever, daemon=True).start(); secure = f'https://127.0.0.1:{tls.server_port}'
    FILES['plain.bin'] = (os.urandom(150000), 'probe-plain.bin')
    cases = []   # name, file url, trigger, then, reputation, page, extra host arguments

    def exe_case(name, trigger='navigate', then='none', reputation='on', origin=base, page=base, more=()):
        key = name + '.exe'; FILES[key] = (fresh_exe(work, name) + (os.urandom(a.big << 20) if a.big else b''), f'probe-{name}.exe')
        cases.append((name, f'{origin}/{key}', trigger, then, reputation, page, list(more)))
    exe_case('held-none'); exe_case('held-cancel', then='cancel'); exe_case('held-link-cancel', then='link-cancel')
    exe_case('held-reputation-off', reputation='off')
    # What src/main.cpp FinishKeep does: the link back at the final path as soon as the engine reports the cancel
    # (or later), then attachment services: does the restored file stay?
    exe_case('held-restore', then='link-cancel-restore')
    exe_case('held-restore-aes', then='link-cancel-restore', more=('--aes', 'on'))
    exe_case('held-restore-late', then='link-cancel-restore', more=('--restore-delay', '3000', '--aes', 'on'))
    # The same with the home sealed as the browser e2e seals it (USERPROFILE / APPDATA / LOCALAPPDATA under the case).
    exe_case('held-restore-aes-sealed', then='link-cancel-restore', more=('--aes', 'on'))
    # Another program reading the held file while the engine removes it (every sharing allowed, as an antivirus or an
    # indexer opens files): is the engine's removal delayed, and what does it do to the name given back meanwhile?
    exe_case('held-other-handle', then='link-cancel-restore', more=('--other-handle-ms', '4000'))
    exe_case('held-other-handle-late', then='link-cancel-restore', more=('--other-handle-ms', '4000', '--restore-delay', '6000'))
    exe_case('held-reader-no-delete', then='link-cancel-restore', more=('--other-handle-ms', '4000', '--other-share', 'read'))
    exe_case('exe-script', trigger='script'); exe_case('exe-gesture', trigger='gesture')
    cases.append(('plain', f'{base}/plain.bin', 'navigate', 'none', 'on', base, []))
    FILES['insecure.bin'] = (os.urandom(120000), 'probe-insecure.bin')
    cases.append(('insecure-bin', f'{insecure}/insecure.bin', 'gesture', 'none', 'on', base, []))
    exe_case('insecure-exe', trigger='gesture', origin=insecure)
    if secure:   # from an https:// page: the download itself is plain http on a non-local host
        cases.append(('https-page-insecure-bin', f'{insecure}/insecure.bin', 'gesture', 'none', 'on', secure, []))
        cases.append(('https-page-insecure-bin-cancel', f'{insecure}/insecure.bin', 'gesture', 'cancel', 'on', secure, []))
        cases.append(('https-page-insecure-restore', f'{insecure}/insecure.bin', 'gesture', 'link-cancel-restore', 'on', secure, ['--aes', 'on']))
        exe_case('https-page-insecure-exe', trigger='gesture', origin=insecure, page=secure)
    if a.night:
        FILES['eicar.com'] = (eicar(), 'probe-eicar.com')
        cases.append(('eicar', f'{base}/eicar.com', 'navigate', 'none', 'on', base, []))
    if a.observe:   # replaces the case list: one variant, run until enough lingering runs
        more = {'leave': ['--observe-ms', '60000'], 'drop': ['--drop-link-ms', '6000', '--observe-ms', '20000'],
                'drop-visible': ['--drop-link-ms', '6000', '--observe-ms', '20000', '--link-hidden', 'off'],
                'drop-released': ['--drop-link-ms', '6000', '--observe-ms', '20000', '--release-op', 'now'],
                'touch': ['--touch-ms', '6000', '--drop-link-ms', '12000', '--observe-ms', '25000'],
                # cancel only once the zone mark is there (cap 20 s), give the file back at each removal, count them for 60 s
                'zone': ['--act-on', 'zone', '--restore-on-gone', 'on', '--link-hidden', 'off', '--observe-ms', '60000']}[a.observe]
        cases.clear()
        for n in range(a.runs):
            exe_case(f'observe-{a.observe}-{n + 1}', then='observe', more=more)
    only = {n for n in a.only.split(',') if n}
    lingering = 0
    failures = 0
    try:
        for name, file_url, trigger, then, reputation, page, more in cases:
            if only and name not in only:
                continue
            case = out / name; udf = case / 'profile'; dl = case / 'downloads'; dl.mkdir(parents=True); udf.mkdir()
            env = {k: v for k, v in os.environ.items() if not k.upper().startswith(('WEBVIEW2_', 'BROWSER_'))}
            env['WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS'] = '--host-resolver-rules="MAP insecure.test 127.0.0.1" --ignore-certificate-errors'
            if name.endswith('-sealed'):
                home = case / 'home'
                for sub in ('AppData/Roaming', 'AppData/Local'):
                    (home / sub).mkdir(parents=True, exist_ok=True)
                env.update(USERPROFILE=str(home), HOMEDRIVE=str(home)[:2], HOMEPATH=str(home)[2:], APPDATA=str(home / 'AppData' / 'Roaming'),
                           LOCALAPPDATA=str(home / 'AppData' / 'Local'), PATH=r'C:\Windows\System32;C:\Windows;C:\Windows\System32\WindowsPowerShell1.0')
            cmd = [str(host), '--url', f'{page}/page', '--file', file_url, '--udf', str(udf), '--dir', str(dl), '--trigger', trigger,
                   '--then', then, '--reputation', reputation, '--hold-ms', a.hold_ms, '--seconds', '90' if a.observe else '45'] + more + (['--observe-ms', a.observe_ms] if a.observe and a.observe_ms else [])
            print(f'===== {name}: {file_url} trigger={trigger} then={then} reputation={reputation} {" ".join(more)}', flush=True)
            job = Job()
            proc = subprocess.Popen(cmd, cwd=str(host.parent), env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    creationflags=0x4 | 0x08000000)   # suspended (in the job before any child), no console window
            job.add(proc); ctypes.WinDLL('ntdll').NtResumeProcess(W.HANDLE(int(proc._handle)))
            try:
                text, _ = proc.communicate(timeout=150)
            except subprocess.TimeoutExpired:
                text = '(host timed out)'; failures += 1
            text = text.decode('utf-8', errors='replace') if isinstance(text, bytes) else text
            print(text.rstrip(), flush=True)
            left = job.drain(20)
            after = sorted(f'{p.name}={p.stat().st_size}' for p in dl.iterdir()) if dl.exists() else []
            print(f'FACT after the engine exited ({left} process(es) still running at 20 s, then stopped through the job): '
                  + (' '.join(after) or '(empty)'), flush=True)
            (case / 'facts.txt').write_text(text + '\nafter exit: ' + ' '.join(after) + '\n', encoding='utf-8')
            if a.observe and ('FACT LINGERING' in text or 'REMOVAL #2' in text):
                lingering += 1
                if lingering >= a.lingering:
                    break
    finally:
        srv.shutdown()
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
