"""A download the engine holds for the user's decision (B07b « À confirmer »): real engine, local server only.

    python tests/downloads/held_download_e2e.py --app <built app dir> --out <dir>

Measured (2026-11-23, WebView2 157): an unsigned, never-seen .exe — the browser's own installer from its release
page, or a fresh local build served from 127.0.0.1 — arrives whole at its final path, then stays IN_PROGRESS with no
interrupt reason and no further event (Microsoft Defender SmartScreen waits for « keep / delete » in the engine's own
flyout, which this browser replaces). The browser used to show 100 % forever; the engine deletes the file when it exits.
Expected now:
  - held:    log « DOWNLOAD held by the engine », the popover's card exposes « Supprimer » / « Conserver » (UIA);
  - keep:    UIA Invoke on « Conserver » → the row is a finished download, the file is whole at its path with its
             Mark of the Web, and it is still there after the browser exits;
  - discard: UIA Invoke on « Supprimer » → the file is gone and the row with it;
  - control: a plain .bin completes as before and is never held.

Isolation: one throwaway copy of the app per case (payload only, its own profile/settings/log beside the exe);
downloads go to <out>/downloads only; the .exe is compiled here (a unique unsigned binary, never run); a local HTTP
fixture on 127.0.0.1. UIA Invoke reaches the popover through its own provider (no SendInput, no click, no foreground
change). Every process started here is in a job object with KILL_ON_JOB_CLOSE, and the copy's processes are also
stopped by exact path at the end. No real site, no real profile.
"""
from __future__ import annotations
import argparse, ctypes, ctypes.wintypes as W, http.server, importlib.util, json, os, random, shutil, subprocess, sys, threading, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
_spec = importlib.util.spec_from_file_location('download_navigation_e2e', Path(__file__).with_name('download_navigation_e2e.py'))
dn = importlib.util.module_from_spec(_spec); _spec.loader.exec_module(dn)
u32 = ctypes.windll.user32
FILES: dict[str, tuple[bytes, str]] = {}

UIA = r'''param([int]$Hwnd, [string]$Id, [switch]$Probe)
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
$root = [System.Windows.Automation.AutomationElement]::FromHandle([IntPtr]$Hwnd)
$cond = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::AutomationIdProperty, $Id)
$e = $root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $cond)
if (-not $e) { 'missing'; exit 0 }
if ($Probe) { 'found'; exit 0 }
$p = $null
$name = $e.Current.Name
if ($e.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref]$p)) { $p.Invoke(); 'invoked:' + $name } else { 'no-invoke' }
'''


class Fixture(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def do_GET(self):
        path = self.path.split('?')[0]
        if path == '/page':
            body, ctype, name = b'<!doctype html><title>OPENER</title><p>held download fixture</p>', 'text/html; charset=utf-8', None
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


def popover_hwnd(pid: int):
    found = []

    @ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    def each(hwnd, _):
        p = W.DWORD(); u32.GetWindowThreadProcessId(hwnd, ctypes.byref(p))
        cls = ctypes.create_unicode_buffer(64); u32.GetClassNameW(hwnd, cls, 64)
        if p.value == pid and cls.value == 'LisiereDownloads':
            found.append(hwnd)
        return True
    u32.EnumWindows(each, 0)
    return found[0] if found else None


def main() -> int:
    ap = argparse.ArgumentParser(); ap.add_argument('--app', required=True); ap.add_argument('--out', required=True)
    a = ap.parse_args()
    src, out = Path(a.app).resolve(), Path(a.out).resolve()
    shutil.rmtree(out, ignore_errors=True); out.mkdir(parents=True)
    downloads = out / 'downloads'; downloads.mkdir(); work = out / 'work'; work.mkdir()
    uia = work / 'uia.ps1'; uia.write_text(UIA, encoding='utf-8')
    FILES['keep.exe'] = (fresh_exe(work, 'keep'), 'held-keep.exe')
    FILES['discard.exe'] = (fresh_exe(work, 'discard'), 'held-discard.exe')
    FILES['plain.bin'] = (os.urandom(150000), 'plain-control.bin')
    srv = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Fixture); base = f'http://127.0.0.1:{srv.server_port}'
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    job = dn.kill_on_close_job()
    res = {'checks': {}, 'failures': []}; apps = []

    def check(name, ok, **seen):
        res['checks'][name] = {'ok': bool(ok), **seen}
        if not ok:
            res['failures'].append(name)
        print(('ok   ' if ok else 'FAIL ') + name + ' ' + json.dumps(seen, ensure_ascii=False, default=str)[:700], flush=True)

    def invoke(b, automation_id, probe=False):
        h = popover_hwnd(b.proc.pid)
        if not h:
            return 'no popover'
        r = subprocess.run(['powershell', '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', str(uia), '-Hwnd', str(h), '-Id', automation_id] + (['-Probe'] if probe else []),
                           capture_output=True, text=True, timeout=120)
        return (r.stdout + r.stderr).strip()

    def launch(name):
        app = out / name; app.mkdir()
        for f in ('browser.exe', 'WebView2Loader.dll', 'version.json'):
            if (src / f).exists():
                shutil.copy2(src / f, app / f)
        shutil.copytree(src / 'assets', app / 'assets'); apps.append(app)
        b = dn.Browser(app, base, downloads, False, job)
        op = b.connect(lambda t: t.get('url', '') == base + '/page', 40)
        if not op or not dn.wait(lambda: 'NAVIGATION OK ' + base + '/page' in b.log(), 20):
            raise RuntimeError('the fixture page did not load')
        time.sleep(1)
        return b, op

    def lines(b, mark):
        return [l for l in b.log()[mark:].splitlines() if l.startswith('DOWNLOAD')]

    try:
        # ---- keep
        b, op = launch('app-keep'); target = downloads / 'held-keep.exe'
        try:
            mark = len(b.log()); dn.ev(op, f"location.href='{base}/keep.exe'")
            held = dn.wait(lambda: 'DOWNLOAD held by the engine' in b.log()[mark:], 30)
            whole = target.exists() and target.stat().st_size == len(FILES['keep.exe'][0])
            card = dn.wait(lambda: invoke(b, 'DownloadKeep', True) == 'found' and invoke(b, 'DownloadDiscard', True) == 'found', 10)
            check('held_detected', held and whole and card, held=bool(held), file_whole_while_held=whole, card_buttons=bool(card), log=lines(b, mark))
            r = invoke(b, 'DownloadKeep')
            kept = dn.wait(lambda: 'DOWNLOAD kept by the user' in b.log()[mark:], 20)
            dn.wait(lambda: '\tC\t' in '\t' + b.model() or ('C\t' in b.model() and 'held-keep.exe' in b.model()), 10)
            time.sleep(2.5)   # attachment services (zone mark + scan) run off the UI thread
            zone = ''
            try:
                zone = open(str(target) + ':Zone.Identifier', encoding='utf-8', errors='replace').read()
            except OSError:
                pass
            model = b.model()
            row = next((l for l in model.splitlines() if 'held-keep.exe' in l), '')
            size_ok = target.exists() and target.stat().st_size == len(FILES['keep.exe'][0])
            check('keep_finishes_the_download', r.startswith('invoked') and bool(kept) and size_ok and row.startswith('C\t') and 'ZoneId=3' in zone,
                  invoke=r, kept=bool(kept), file_whole=size_ok, history_row=row[:120], zone_mark='ZoneId=3' in zone, log=lines(b, mark))
        finally:
            b.close(); dn.stop_by_path(out / 'app-keep')
        time.sleep(1)
        check('kept_file_survives_exit', target.exists() and target.stat().st_size == len(FILES['keep.exe'][0]), exists=target.exists())
        # ---- discard
        b, op = launch('app-discard'); target = downloads / 'held-discard.exe'
        try:
            mark = len(b.log()); dn.ev(op, f"location.href='{base}/discard.exe'")
            held = dn.wait(lambda: 'DOWNLOAD held by the engine' in b.log()[mark:], 30)
            r = invoke(b, 'DownloadDiscard') if held else 'not held'
            gone = dn.wait(lambda: not target.exists(), 10)
            time.sleep(1)
            row_left = 'held-discard.exe' in b.model()
            check('discard_deletes_the_file', bool(held) and r.startswith('invoked') and bool(gone) and not row_left,
                  held=bool(held), invoke=r, file_gone=bool(gone), row_in_history=row_left, log=lines(b, mark))
        finally:
            b.close(); dn.stop_by_path(out / 'app-discard')
        # ---- control: an ordinary file is never held
        b, op = launch('app-control'); target = downloads / 'plain-control.bin'
        try:
            mark = len(b.log()); dn.ev(op, f"location.href='{base}/plain.bin'")
            done = dn.wait(lambda: 'DOWNLOAD state=2 plain-control.bin' in b.log()[mark:], 20)
            time.sleep(3)
            held = 'DOWNLOAD held' in b.log()[mark:]
            check('plain_file_completes_unheld', bool(done) and not held and target.exists(), completed=bool(done), held=held, log=lines(b, mark))
        finally:
            b.close(); dn.stop_by_path(out / 'app-control')
    except Exception as e:   # noqa: BLE001 - reported as a failure
        check('harness', False, error=repr(e))
    finally:
        srv.shutdown()
        for app in apps:
            dn.stop_by_path(app)
    (out / 'result.json').write_text(json.dumps(res, ensure_ascii=False, indent=1), encoding='utf-8')
    print('FAILED: ' + ', '.join(res['failures']) if res['failures'] else 'held download checks passed', flush=True)
    return 1 if res['failures'] else 0


if __name__ == '__main__':
    sys.exit(main())
