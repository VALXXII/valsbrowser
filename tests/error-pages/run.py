"""#121 error pages against the Paper boards, without any window (safe in the daytime).

    python tests/error-pages/run.py

What it does: builds gen_pages.exe (the pages exactly as src/error-pages.h writes them, real bundled fonts, the
boards' texts) and page_check_host.exe (a WebView2 host whose window is never shown: off every monitor, never
activated, no ShowWindow), then shows each page with NavigateToString at 1x in a fresh temporary profile, inside a
job object with KILL_ON_JOB_CLOSE. Nothing appears on screen, the mouse and focus are left alone, no network.
Per case it checks the layout against the boards (tests/error-pages/cases.py: block size, 16 / 8 gaps, icon,
button widths, sheet, status) and compares the pixels with the boards' exports (tests/error-pages/boards/):
build/error-pages/report.md lists everything and the side-by-side images are beside it.
Fails (exit 1) on a layout difference or a page NavigateToString refuses; the pixel figures are for the review
(Paper and WebView2 do not rasterise text identically).

Tools: tools/w64devkit and deps/webview2 of this checkout, or W64DEVKIT_BIN / WEBVIEW2_INCLUDE / WEBVIEW2_LOADER
(another checkout's copies; never junctions). Needs Pillow (python -m pip install pillow) for the pixel part.
"""
from __future__ import annotations
import ctypes, ctypes.wintypes as W, json, os, shutil, subprocess, sys, tempfile, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
OUT = ROOT / 'build' / 'error-pages'
TOOLS = Path(os.environ.get('W64DEVKIT_BIN') or ROOT / 'tools' / 'w64devkit' / 'bin')
WV2_INCLUDE = Path(os.environ.get('WEBVIEW2_INCLUDE') or ROOT / 'deps' / 'webview2' / 'build' / 'native' / 'include')
WV2_LOADER = Path(os.environ.get('WEBVIEW2_LOADER') or WV2_INCLUDE.parent / 'x64' / 'WebView2Loader.dll')
sys.path.insert(0, str(HERE))
import cases  # noqa: E402

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


def run_in_job(cmd: list, cwd: Path, timeout: int) -> str:
    job = kill_on_close_job()
    try:
        proc = subprocess.Popen(cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, creationflags=0x4)   # CREATE_SUSPENDED: in the job first
        handle = W.HANDLE(int(proc._handle))
        if not k32.AssignProcessToJobObject(job, handle):
            proc.kill(); raise OSError('AssignProcessToJobObject failed')
        ntdll.NtResumeProcess(handle)
        try:
            out, _ = proc.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            proc.kill(); out, _ = proc.communicate()
    finally:
        k32.CloseHandle(job)   # KILL_ON_JOB_CLOSE: the engine's processes end here too
    return out.decode('utf-8', 'replace')


def build() -> tuple[Path, Path]:
    OUT.mkdir(parents=True, exist_ok=True)
    common = [str(TOOLS / 'g++.exe'), '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-Wno-missing-field-initializers', '-Wno-unknown-pragmas',
              '-Wno-misleading-indentation', '-Wno-cast-function-type',   # the repository's warning set (tools/release/build.py)
              '-static', '-municode', '-Isrc', f'-I{WV2_INCLUDE}']
    gen, host = OUT / 'gen_pages.exe', OUT / 'page_check_host.exe'
    for src, exe, libs in (('tests/error-pages/gen_pages.cpp', gen, ['-lole32']), ('tests/error-pages/page_check_host.cpp', host, ['-lole32', '-luuid', '-luser32', '-lcrypt32'])):
        if subprocess.run([*common, src, '-o', str(exe), *libs], cwd=ROOT).returncode:
            sys.exit(f'FAILED: compile {src}')
    shutil.copy2(WV2_LOADER, OUT / 'WebView2Loader.dll')
    return gen, host


def main() -> int:
    gen, host = build()
    pages = OUT / 'pages'; shots = OUT / 'shots'
    for d in (pages, shots):
        shutil.rmtree(d, ignore_errors=True); d.mkdir(parents=True)
    sizes = {}
    for line in subprocess.run([str(gen), str(ROOT), str(pages)], capture_output=True, text=True, check=True).stdout.splitlines():
        name, size = line.split(); sizes[name] = int(size)
    (pages / 'measure.js').write_text(cases.MEASURE_JS, encoding='utf-8')
    for name, js in cases.PREPARE.items():
        (pages / f'{name}.prepare.js').write_text(js, encoding='utf-8')
    # The copy button's states, each on its own copy of the time-out page; keyboard focus needs a focused window
    # (a page that never had focus shows no :focus-visible): window_qa.py checks it at night.
    windowless = [n for n, s in cases.COPY_STATES.items() if not s.get('night')]
    for name in windowless:
        shutil.copy2(pages / f'{cases.COPY_PAGE}.html', pages / f'{name}.html')
    names = list(cases.CASES) + windowless
    profile = Path(tempfile.mkdtemp(prefix='error-pages-'))
    try:
        text = run_in_job([str(host), '--pages', str(pages), '--names', ','.join(names), '--udf', str(profile), '--out', str(shots)], OUT, 180)
    finally:
        for _ in range(20):
            shutil.rmtree(profile, ignore_errors=True)
            if not profile.exists():
                break
            time.sleep(0.5)
    measures = {}
    for line in text.splitlines():
        if line.startswith('MEASURE '):
            _, name, raw = line.split(' ', 2)
            value = json.loads(raw)
            measures[name] = json.loads(value) if isinstance(value, str) else value
    failures, rows = [], []
    try:
        from PIL import Image
    except ImportError:
        Image = None
        print('  (Pillow missing: no pixel comparison)')
    for name in names:
        if sizes.get(name, 0) >= 1_500_000:   # NavigateToString refuses ~1.5 MB and more (base64 data: URL, 2 MB)
            failures.append(f'{name}: {sizes[name]} bytes, too big for NavigateToString')
        m = measures.get(name)
        if not m:
            failures.append(f'{name}: not measured (see the host output)'); rows.append({'name': name, 'layout': ['not measured'], 'pixels': {}}); continue
        if name in cases.COPY_STATES:
            layout = cases.check_copy(name, m)
            rows.append({'name': name, 'layout': layout, 'pixels': {}})
            print(f'  {name:16} {"OK" if not layout else "DIFF"}  {m.get("copyState")}')
            for d in layout:
                print(f'      {d}')
            failures += [f'{name}: {d}' for d in layout]
            continue
        layout = cases.check(name, m)
        pixels = {}
        if Image and (shots / f'{name}.png').exists() and m.get('main'):
            main = m['main']; shot = Image.open(shots / f'{name}.png')
            block = shot.crop((round(main['x']), round(main['y']), round(main['x'] + main['w']), round(main['y'] + main['h'])))
            block.save(shots / f'{name}-block.png')
            pixels = cases.compare(name, block, OUT / 'compare')
        rows.append({'name': name, 'layout': layout, 'pixels': pixels, 'bytes': sizes.get(name)})
        print(f'  {name:16} {"OK" if not layout else "DIFF"}  {sizes.get(name, 0):>8} B  pixels {pixels or "-"}')
        for d in layout:
            print(f'      {d}')
        failures += [f'{name}: {d}' for d in layout]
    cases.write_report(OUT / 'report.md', '#121 pages d\'erreur — rendu WebView2 sans fenêtre vs planches Paper', rows)
    if any(line.startswith(('ENGINE FAILED', 'LOAD FAILED', 'WebView2Loader', 'EMULATION FAILED', 'CAPTURE FAILED', 'FONTS NOT LOADED', 'TIMEOUT')) or ' hr=-' in line for line in text.splitlines()):
        print(text)
        failures.append('the host reported a failure (above)')
    print(f'report: {OUT / "report.md"}')
    print('error pages vs boards: ' + ('FAILED (' + str(len(failures)) + ')' if failures else f'OK ({len(names)} cases as drawn)'))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
