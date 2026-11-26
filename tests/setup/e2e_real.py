#!/usr/bin/env python3
"""Real-application end to end of the Windows setup, fully isolated (evidence for design/windows-setup-report.md).

    python -I tests/setup/e2e_real.py --app build/setup-e2e/app --out design/windows-setup-evidence

1. tools/release/package.py on a real --channel release build: .lsrupd, ZIP and the RELEASE setup.exe; checks that
   the setup embeds the .lsrupd byte for byte and that the ZIP holds exactly the same files and bytes.
2. A test-build stub (-DSETUP_TESTING: Start Menu / Desktop / uninstall key / temp redirected, same sources, same
   build-info.h as the release stub) is wrapped around that SAME .lsrupd. The release stub is not executed: it would
   write the real Start Menu and HKCU uninstall entry.
3. Its window is driven only through its own handles (BM_CLICK on its child buttons, WM_CLOSE), captured with
   PrintWindow; no SendInput, no foreground change, no screenshot of anything else.
4. The real browser installed under <run>\\LocalAppData\\browser starts (empty synthetic data), acknowledges, is
   captured and closed with WM_CLOSE; the stable annotations bridge answers MCP initialize/tools/list over stdio.
5. Repair page, « WebView2 absent » note (simulated: BROWSER_SETUP_TEST_WEBVIEW2=0), uninstall window (data kept).
"""
from __future__ import annotations
import argparse, ctypes, hashlib, json, os, shutil, struct, subprocess, sys, time, zipfile, zlib
from ctypes import wintypes as W
from pathlib import Path
os.environ.setdefault('BROWSER_NO_ONBOARDING', '1')   # fresh installed layout: not the first-run window (src/onboarding-flow.h)

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/release'))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import package as K  # noqa: E402

user32, gdi32 = ctypes.WinDLL('user32', use_last_error=True), ctypes.WinDLL('gdi32')
user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))  # per-monitor v2: real pixel rects for PrintWindow
user32.FindWindowExW.restype = W.HWND
user32.FindWindowExW.argtypes = [W.HWND, W.HWND, W.LPCWSTR, W.LPCWSTR]
user32.GetDlgItem.restype = W.HWND
user32.GetDlgItem.argtypes = [W.HWND, ctypes.c_int]
user32.SendMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
user32.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
user32.GetWindowThreadProcessId.argtypes = [W.HWND, ctypes.POINTER(W.DWORD)]
user32.GetWindowTextW.argtypes = [W.HWND, W.LPWSTR, ctypes.c_int]
user32.IsWindowVisible.argtypes = [W.HWND]
user32.GetWindowRect.argtypes = [W.HWND, ctypes.POINTER(W.RECT)]
user32.GetDC.restype = W.HDC
user32.GetDC.argtypes = [W.HWND]
user32.PrintWindow.argtypes = [W.HWND, W.HDC, W.UINT]
gdi32.CreateCompatibleDC.restype = W.HDC
gdi32.CreateCompatibleDC.argtypes = [W.HDC]
gdi32.CreateCompatibleBitmap.restype = W.HBITMAP
gdi32.CreateCompatibleBitmap.argtypes = [W.HDC, ctypes.c_int, ctypes.c_int]
gdi32.SelectObject.argtypes = [W.HDC, W.HGDIOBJ]
gdi32.GetDIBits.argtypes = [W.HDC, W.HBITMAP, W.UINT, W.UINT, ctypes.c_void_p, ctypes.c_void_p, W.UINT]
gdi32.DeleteObject.argtypes = [W.HGDIOBJ]
gdi32.DeleteDC.argtypes = [W.HDC]
EnumProc = ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
user32.EnumWindows.argtypes = [EnumProc, W.LPARAM]
user32.GetClassNameW.argtypes = [W.HWND, W.LPWSTR, ctypes.c_int]
user32.ReleaseDC.argtypes = [W.HWND, W.HDC]
WM_CLOSE, BM_CLICK = 0x0010, 0x00F5
IdDesktop, IdLaunch, IdRemoveData, IdPrimary, IdSecondary = 101, 102, 103, 104, 105
log_lines: list[str] = []


def log(msg):
    print(msg, flush=True)
    log_lines.append(msg)


def windows_of(pid: int, cls: str | None = None) -> list[int]:
    found = []

    def cb(h, _):
        p = W.DWORD()
        user32.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value == pid and user32.IsWindowVisible(h):
            if cls is None:
                found.append(h)
            else:
                name = ctypes.create_unicode_buffer(256)
                user32.GetClassNameW(h, name, 256)
                if name.value == cls:
                    found.append(h)
        return True
    user32.EnumWindows(EnumProc(cb), 0)
    return found


def setup_window(pid: int, timeout=30) -> int:
    for _ in range(timeout * 10):
        w = windows_of(pid, 'BrowserSetupWindow')
        if w:
            time.sleep(0.4)
            return w[0]
        time.sleep(0.1)
    raise SystemExit(f'no setup window for pid {pid}')


def text(h) -> str:
    b = ctypes.create_unicode_buffer(512)
    user32.GetWindowTextW(h, b, 512)
    return b.value


def click(win, cid):
    user32.SendMessageW(user32.GetDlgItem(win, cid), BM_CLICK, 0, 0)
    time.sleep(0.3)


def capture(hwnd, path: Path):
    # Own window only: force a synchronous repaint of it and its children, then let DWM compose it.
    user32.RedrawWindow(W.HWND(hwnd), None, None, 0x0001 | 0x0100 | 0x0080 | 0x0004)  # INVALIDATE|UPDATENOW|ALLCHILDREN|ERASE
    time.sleep(0.6)
    r = W.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(r))
    w, h = r.right - r.left, r.bottom - r.top
    screen = user32.GetDC(None)
    dc = gdi32.CreateCompatibleDC(screen)
    bmp = gdi32.CreateCompatibleBitmap(screen, w, h)
    gdi32.SelectObject(dc, bmp)
    user32.PrintWindow(hwnd, dc, 2)  # PW_RENDERFULLCONTENT, own window only
    header = struct.pack('<IiiHHIIiiII', 40, w, -h, 1, 32, 0, 0, 0, 0, 0, 0)
    buf = ctypes.create_string_buffer(w * h * 4)
    gdi32.GetDIBits(dc, bmp, 0, h, buf, header, 0)
    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(dc)
    user32.ReleaseDC(None, screen)
    rows = []
    for y in range(h):
        row = buf.raw[y * w * 4:(y + 1) * w * 4]
        rgb = bytearray(w * 3)
        rgb[0::3], rgb[1::3], rgb[2::3] = row[2::4], row[1::4], row[0::4]  # BGRA -> RGB
        rows.append(b'\0' + bytes(rgb))
    raw = b''.join(rows)
    chunk = lambda t, d: struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                     chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))
    log(f'  captured {path.name} ({w}x{h})')


def wait(cond, seconds, what):
    for _ in range(int(seconds * 10)):
        if cond():
            return True
        time.sleep(0.1)
    raise SystemExit(f'timeout: {what}')


def main() -> int:
    a = argparse.ArgumentParser()
    a.add_argument('--app', required=True)
    a.add_argument('--out', required=True)
    args = a.parse_args()
    app, out = (ROOT / args.app).resolve(), (ROOT / args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    work = app.parent
    dist = work / 'dist'
    info = json.loads((app / 'version.json').read_text(encoding='utf-8'))
    v = info['version']

    # 1. Release assets from the real build.
    log(f'== package {app} ({v}, channel {info["channel"]}, commit {info["commit"]})')
    r = subprocess.run([sys.executable, str(ROOT / 'tools/release/package.py'), '--app', str(app), '--out', str(dist)],
                       capture_output=True, text=True)
    log(r.stdout.strip())
    if r.returncode:
        raise SystemExit(r.stderr)
    package, archive, setup = dist / f'browser-{v}-win-x64.lsrupd', dist / K.zip_name(v), dist / K.setup_name(v)
    K.verify_setup(setup, package, info)
    data, pkg = setup.read_bytes(), package.read_bytes()
    start, end, signed = K.pe_overlay(data)
    log(f'  setup {len(data)} bytes = stub {start} + package {len(pkg)} + trailer {end - start - len(pkg)}; signed={signed}')
    assert data[start:start + len(pkg)] == pkg
    import run_tests as RT
    entries = RT.lsrupd_entries(pkg)
    top = archive.name[:-4]
    with zipfile.ZipFile(archive) as z:
        zipped = {n.split('/', 1)[1]: z.read(n) for n in z.namelist()}
    assert zipped == entries, 'ZIP != .lsrupd entries'
    log(f'  .lsrupd entries == ZIP entries ({len(entries)} files, byte-identical), bridge at assets/bin: '
        f'{"assets/bin/browser-annotations-mcp.exe" in entries}')
    for marker in ('BROWSER_SETUP_TEST_ROOT', 'BROWSER_SETUP_FAULT', '--unattended'):
        assert marker.encode('utf-16-le') not in data[:start], marker
    log('  release stub: no test hooks; identity ' + K.stub_identity(data[:start]))

    # 2. Test stub, same build-info, around the SAME package.
    gen = app.parent / (app.name + '-obj') / 'gen'
    bin_ = ROOT / 'tools/w64devkit/bin'
    stub = work / 'stub-test.exe'
    res = work / 'setup-test-res.o'
    subprocess.run([str(bin_ / 'windres.exe'), '-I.', '-Isrc', f'-I{gen}', '-DBROWSER_HAS_BUILD_INFO', 'src/setup.rc', '-O', 'coff',
                    '-o', str(res)], cwd=ROOT, check=True)
    subprocess.run([str(bin_ / 'g++.exe'), *RT.FLAGS, '-mwindows', f'-I{gen}', '-DBROWSER_HAS_BUILD_INFO', '-DSETUP_TESTING',
                    'src/setup-main.cpp', 'src/setup-core.cpp', *RT.CORE, str(res), '-o', str(stub), *RT.SETUP_LIBS], cwd=ROOT, check=True)
    subprocess.run([str(bin_ / 'strip.exe'), '--strip-all', str(stub)], check=True)
    assert K.stub_identity(stub.read_bytes()) == K.stub_identity(data[:start])
    test_setup = work / f'browser-{v}-win-x64-setup-TEST.exe'
    test_setup.write_bytes(K.setup_bytes(stub.read_bytes(), pkg, v))

    run = work / 'run'
    if run.exists():
        shutil.rmtree(run)
    for sub in ('LocalAppData', 'StartMenu/Programs', 'Desktop', 'Temp'):
        (run / sub).mkdir(parents=True)
    root = run / 'LocalAppData/browser'
    env = dict(os.environ, LOCALAPPDATA=str(run / 'LocalAppData'), BROWSER_SETUP_TEST_ROOT=str(run), TEMP=str(run / 'Temp'),
               TMP=str(run / 'Temp'), BROWSER_SETUP_TEST_WEBVIEW2='1', BROWSER_SETUP_TEST_LANG='fr')
    env.pop('WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS', None)
    key = rf'Software\browser-setup-tests\run\Uninstall\VALXXII.browser'
    browser_pid = None
    try:
        # 3. Install through the window (French).
        log('== install window (fr)')
        p = subprocess.Popen([str(test_setup)], env=env, cwd=str(run))
        win = setup_window(p.pid)
        capture(win, out / 'setup-1-options-fr.png')
        click(win, IdDesktop)
        capture(win, out / 'setup-2-options-desktop-fr.png')
        log(f'  primary button: {text(user32.GetDlgItem(win, IdPrimary))!r}')
        click(win, IdPrimary)
        wait(lambda: text(user32.GetDlgItem(win, IdPrimary)) in ('Fermer', 'Réessayer'), 300, 'install end')
        capture(win, out / 'setup-3-done-fr.png')
        result = (root / 'updates/last-result').read_text()
        log(f'  last-result: {result.strip()!r}')
        assert 'status installed' in result and 'reason ack' in result, result
        helper_log = (root / 'updates/helper.log').read_text()
        browser_pid = int(helper_log.split('new version launched pid ')[-1].split()[0])
        log(f'  real browser pid {browser_pid} acknowledged its start')
        for rel in ('browser.exe', 'browser.ico', 'uninstall.exe', 'current', f'versions/{v}/browser.exe', 'bin/browser-annotations-mcp.exe'):
            assert (root / rel).exists(), rel
        assert (root / 'bin/browser-annotations-mcp.exe').read_bytes() == entries['assets/bin/browser-annotations-mcp.exe']
        installed = {p.relative_to(root / 'versions' / v).as_posix(): p.read_bytes() for p in (root / 'versions' / v).rglob('*')
                     if p.is_file() and p.name != '.stage'}
        # Pre-existing product behaviour (not the setup): at first run WebView2 indexes the unpacked uBO Lite rulesets
        # INTO assets/extensions/ublock-lite/_metadata/generated_indexed_rulesets/ of the version directory.
        generated = sorted(k for k in installed if '/_metadata/generated_indexed_rulesets/' in k and k not in entries)
        installed = {k: b for k, b in installed.items() if k not in generated}
        assert installed == entries == zipped, 'installed tree != .lsrupd != ZIP'
        log(f'  installed versions/{v} == .lsrupd == ZIP ({len(installed)} files, byte-identical); plus {len(generated)} files '
            'generated at runtime by WebView2 under assets/extensions/ublock-lite/_metadata/generated_indexed_rulesets/')
        click(win, IdPrimary)  # Fermer
        p.wait(30)
        log(f'  setup exit {p.returncode}')
        # Real browser window: wait, capture, close (our own process, WM_CLOSE).
        bw = []
        wait(lambda: bool(bw.extend(windows_of(browser_pid)) or bw), 60, 'browser window')
        time.sleep(6)
        main = windows_of(browser_pid, 'LisiereBrowser')
        capture(main[0] if main else bw[0], out / 'browser-installed-running.png')
        data_dir = root / 'data'
        log(f'  data dir created by the browser: {sorted(x.name for x in data_dir.iterdir())}')
        # 4. Stable annotations bridge over stdio (installed kind: store under <root>\\data).
        req = ('{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},'
               '"clientInfo":{"name":"setup-e2e","version":"1"}}}\n'
               '{"jsonrpc":"2.0","method":"notifications/initialized"}\n'
               '{"jsonrpc":"2.0","id":2,"method":"tools/list"}\n')
        br = subprocess.run([str(root / 'bin/browser-annotations-mcp.exe')], input=req, capture_output=True, text=True, env=env, timeout=60)
        lines = [json.loads(l) for l in br.stdout.splitlines() if l.strip()]
        tools = next((m['result']['tools'] for m in lines if m.get('id') == 2 and 'result' in m), [])
        log(f'  bridge: initialize ok={any(m.get("id") == 1 and "result" in m for m in lines)}, tools/list -> {len(tools)} tools')
        assert tools
        for h in windows_of(browser_pid):
            user32.PostMessageW(h, WM_CLOSE, 0, 0)
        ph = ctypes.windll.kernel32.OpenProcess(0x00100000, False, browser_pid)
        ctypes.windll.kernel32.WaitForSingleObject(ph, 30000)
        ctypes.windll.kernel32.CloseHandle(ph)
        log('  browser closed with WM_CLOSE')
        time.sleep(3)

        # 5a. Repair page (English) + WebView2 absent note.
        log('== repair window (en), WebView2 absent simulated')
        env_en = dict(env, BROWSER_SETUP_TEST_LANG='en', BROWSER_SETUP_TEST_WEBVIEW2='0')
        p = subprocess.Popen([str(test_setup)], env=env_en, cwd=str(run))
        win = setup_window(p.pid)
        capture(win, out / 'setup-4-repair-en.png')
        click(win, IdLaunch)  # do not start the browser this time
        click(win, IdPrimary)
        wait(lambda: text(user32.GetDlgItem(win, IdPrimary)) in ('Close', 'Retry'), 300, 'repair end')
        capture(win, out / 'setup-5-repair-done-webview2-note-en.png')
        log(f'  repair result: {(root / "updates/last-result").read_text().strip()!r}')
        click(win, IdPrimary)
        p.wait(30)

        # 5b. Uninstall window (keeps data).
        log('== uninstall window (en)')
        before = sorted(x.name for x in (root / 'data').iterdir())
        p = subprocess.Popen([str(root / 'uninstall.exe')], env=env_en, cwd=str(run))
        p.wait(30)
        temp_pid = None
        for _ in range(100):
            for exe in (run / 'Temp').glob('browser-uninstall-*.exe'):
                out_ = subprocess.run(['tasklist', '/FI', f'IMAGENAME eq {exe.name}', '/FO', 'CSV', '/NH'], capture_output=True, text=True).stdout
                if exe.name in out_:
                    temp_pid = int(out_.split('","')[1])
            if temp_pid:
                break
            time.sleep(0.1)
        win = setup_window(temp_pid)
        capture(win, out / 'uninstall-1-confirm-en.png')
        click(win, IdPrimary)
        wait(lambda: text(user32.GetDlgItem(win, IdPrimary)) in ('Close', 'Retry'), 120, 'uninstall end')
        capture(win, out / 'uninstall-2-done-en.png')
        click(win, IdPrimary)
        assert not (root / 'versions').exists() and not (root / 'browser.exe').exists() and not (root / 'uninstall.exe').exists()
        after = sorted(x.name for x in (root / 'data').iterdir())
        assert before == after, (before, after)
        log(f'  program removed, shortcuts removed: {not (run / "StartMenu/Programs/Browser.lnk").exists()}, data kept: {after}')
    finally:
        import winreg

        def delete_tree(sub):   # this run's own test key only (uninstall entry + default-browser registration)
            assert sub.startswith('Software\\browser-setup-tests\\'), sub
            try:
                with winreg.OpenKey(winreg.HKEY_CURRENT_USER, sub) as k:
                    children = []
                    while True:
                        try:
                            children.append(winreg.EnumKey(k, len(children)))
                        except OSError:
                            break
                for c in children:
                    delete_tree(sub + '\\' + c)
                winreg.DeleteKey(winreg.HKEY_CURRENT_USER, sub)
            except OSError:
                pass
        delete_tree(key.rsplit('\\', 2)[0])
        (out / 'e2e-real.txt').write_text('\n'.join(log_lines) + '\n', encoding='utf-8')
    log('E2E OK')
    (out / 'e2e-real.txt').write_text('\n'.join(log_lines) + '\n', encoding='utf-8')
    return 0


if __name__ == '__main__':
    sys.exit(main())
