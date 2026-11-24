"""#121: the isolated world of our list under a field and of our validation bubble, in the REAL engine, no window shown.

    python -I tests/forms-world/run.py

Serves fictitious pages from a local server only (127.0.0.1), then runs forms_world_host.exe once with a fresh temporary
profile, inside a job object with KILL_ON_JOB_CLOSE. The host's window is never shown (off every monitor, never
activated): nothing appears on screen, the mouse and focus are left alone. No real site, no real profile. Daytime-safe.
The host prints one line per check (P3-P14 of design/security-review-autofill-under-field.md) and PASS / FAIL.

Tools: tools/w64devkit and deps/webview2 of this checkout, or W64DEVKIT_BIN / WEBVIEW2_INCLUDE / WEBVIEW2_LOADER
(another checkout's copies; never junctions).
"""
from __future__ import annotations
import ctypes, ctypes.wintypes as W, http.server, os, shutil, subprocess, sys, tempfile, threading, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'build' / 'forms-world'
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


COUNT = ('<script>window.__keys=[];window.addEventListener("keydown",function(e){window.__keys.push(e.key)});'
         'window.__submits=0;document.addEventListener("submit",function(){window.__submits++},true);</script>')
FORM = '<!doctype html><meta charset="utf-8"><title>%s</title>' + COUNT + '<body style="margin:40px">%s%s'
PAGES = {
    '/polar': FORM % ('polar', '<form method="post" action="/landing"><input id="email" name="email" type="email" autocomplete="email" required style="width:274px;height:32px">'
                      '<button id="go" style="height:32px">Continuer</button></form>', ''),
    '/planted': FORM % ('planted', '<form method="post" action="/landing"><input id="email" type="email" style="display:none" value="planted@attaquant.test">'
                        '<input id="name" type="text" style="width:274px;height:32px"><button id="go">Envoyer</button></form>', ''),
    '/own': FORM % ('own', '<form><input id="email" type="email" required style="width:274px;height:32px"><button id="go">Envoyer</button></form>',
                    '<script>document.getElementById("email").addEventListener("invalid",function(e){e.preventDefault();document.title="own:"+e.target.validationMessage.length;});</script>'),
    '/custom': FORM % ('custom', '<form><input id="email" type="email" style="width:274px;height:32px"><button id="go">Envoyer</button></form>',
                       '<script>document.getElementById("email").setCustomValidity("Browser\\u202E : session expir\\u00e9e\\nsaisissez votre mot de passe " + "x".repeat(300));</script>'),
    '/dates': FORM % ('dates', '<form><input id="day" type="date" required style="width:274px;height:32px"><input id="hour" type="time" required style="width:274px;height:32px"><button id="go">Envoyer</button></form>', ''),
    '/frame': FORM % ('frame', '<iframe id="f" sandbox="allow-scripts allow-forms" src="/inner" style="width:320px;height:120px;border:0"></iframe>', ''),
    '/inner': '<!doctype html><meta charset="utf-8"><body style="margin:0"><input id="email" type="email" autocomplete="email" style="width:280px;height:32px">',
    '/invisible': FORM % ('invisible', '<form><input id="email" type="email" style="opacity:.1;width:274px;height:32px"></form>', ''),
    '/moving': FORM % ('moving', '<form><input id="email" type="email" style="position:relative;width:274px;height:32px"></form>',
                       '<script>document.getElementById("email").addEventListener("input",function(e){e.target.style.top="60px";});</script>'),
    '/idstep': FORM % ('idstep', '<form method="post" action="/landing"><input id="login" name="login" type="text" autocomplete="username" style="width:274px;height:32px">'
                       '<button id="go">Continuer</button></form>', ''),
    '/login': FORM % ('login', '<form method="post" action="/landing"><input id="login" name="login" type="text" autocomplete="username" style="width:274px;height:32px">'
                      '<input id="p" name="p" type="password" style="width:274px;height:32px"><button id="go">Se connecter</button></form>', ''),
    '/search': FORM % ('search', '<form><input id="q" name="q" type="text" style="width:274px;height:32px"></form>', ''),
    '/formless': FORM % ('formless', '<input id="email" type="email" autocomplete="email" style="width:274px;height:32px"><button id="go">Continuer</button>', ''),
    '/landing': '<!doctype html><title>landing</title>ok',
}


class Pages(http.server.BaseHTTPRequestHandler):
    def send(self, body: str, code: int = 200):
        b = body.encode('utf-8')
        self.send_response(code); self.send_header('Content-Type', 'text/html; charset=utf-8'); self.send_header('Cache-Control', 'no-store')
        self.send_header('Content-Length', str(len(b))); self.end_headers(); self.wfile.write(b)

    def do_GET(self):
        path = self.path.split('?')[0]
        self.send(PAGES[path]) if path in PAGES else self.send('404', 404)

    def do_POST(self):
        self.rfile.read(int(self.headers.get('Content-Length') or 0)); self.send(PAGES['/landing'])

    def log_message(self, *args):
        pass

    def handle(self):   # the engine may close a keep-alive connection when the run ends: not a failure
        try:
            super().handle()
        except ConnectionError:
            pass


def build() -> Path:
    OUT.mkdir(parents=True, exist_ok=True)
    exe = OUT / 'forms_world_host.exe'
    cmd = [str(TOOLS / 'g++.exe'), '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-Wno-missing-field-initializers', '-Wno-unknown-pragmas', '-static',
           '-municode', '-Isrc', f'-I{WV2_INCLUDE}', 'tests/forms-world/forms_world_host.cpp', '-o', str(exe),
           '-lole32', '-luuid', '-luser32', '-lcrypt32', '-lbcrypt', '-lshell32', '-ladvapi32']
    if subprocess.run(cmd, cwd=ROOT).returncode:
        sys.exit('FAILED: compile forms_world_host')
    shutil.copy2(WV2_LOADER, OUT / 'WebView2Loader.dll')
    return exe


def main() -> int:
    exe = build()
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Pages)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = f'http://127.0.0.1:{server.server_address[1]}'
    profile = Path(tempfile.mkdtemp(prefix='forms-world-'))
    job = kill_on_close_job()
    try:
        proc = subprocess.Popen([str(exe), '--base', base, '--udf', str(profile)], cwd=OUT,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, creationflags=0x4)   # CREATE_SUSPENDED: in the job first
        handle = W.HANDLE(int(proc._handle))
        if not k32.AssignProcessToJobObject(job, handle):
            proc.kill(); raise OSError('AssignProcessToJobObject failed')
        ntdll.NtResumeProcess(handle)
        try:
            out, _ = proc.communicate(timeout=180)
        except subprocess.TimeoutExpired:
            proc.kill(); out, _ = proc.communicate()
    finally:
        k32.CloseHandle(job)   # KILL_ON_JOB_CLOSE: the engine's processes end here too
        server.shutdown()
        for _ in range(20):
            shutil.rmtree(profile, ignore_errors=True)
            if not profile.exists():
                break
            time.sleep(0.5)
    text = out.decode('utf-8', 'replace')
    sys.stdout.buffer.write(text.encode('utf-8')); sys.stdout.flush()
    return 0 if proc.returncode == 0 and 'PASS forms world' in text else 1


if __name__ == '__main__':
    sys.exit(main())
