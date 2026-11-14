"""Mocked end-to-end update through the production host code (no WebView2, no network).

Builds:
  * host_e2e.exe   - production src/main.cpp + real update-core/update-package/update-toast,
                     stamped 0.3.5 exactly like a release build (BROWSER_HAS_BUILD_INFO);
  * update-helper.exe from src (the real helper);
  * fake browser 0.3.6 (tests/update-core/fake_browser.cpp, owned by update-core): the
    "new version" the helper starts. It only calls UpdateCore::NoteStartup and logs.
    The real browser.exe is never started by anything here.
Packs 0.3.6 with the real packer (tools/update-package/make_update_package.py) and serves
it from a local directory through a fake Transport.

Scenarios (each in its own fresh directory under build/update-integration-tests/e2e):
  installed  <root>\\versions\\0.3.5\\browser.exe -> install 0.3.6; stable data preserved.
  portable   <tmp>\\portable\\browser.exe (legacy exe-adjacent profile/session) -> first install
             into a temp root with a one-time COPY of profile/session; legacy left untouched.
  cancel     cancel mid-download, dismiss, manual re-check, close during download (bounded).
  offline    network failure: silent startup, manual check shows Error (Retry).
  norelease  no published release (404): silent startup, manual check ends up-to-date.
  periodic   mocked clock: one-shot due-aware timer finds a release published while running.
The portable scenario then starts the legacy exe again (old shortcut target): it must hand over to the
installed current version on the stable data folder, with guard runs that must NOT hand over.
Data checks use SHA-256 canary hashes of whole directory trees; the download history (upstream
drawer) is checked for preservation (installed) and exact one-time migration (portable).
"""
from __future__ import annotations
import hashlib, json, os, shutil, subprocess, sys, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'tools/release'))
import build as B  # noqa: E402
import version as V  # noqa: E402

OUT = ROOT / 'build/update-integration-tests'
GXX = str(B.BIN / 'g++.exe')
OLD, NEW = '0.3.5', '0.3.6'
FLAGS = ['-std=c++17', '-O2', *B.WARNINGS, '-static', '-municode', '-Isrc']
CORE = ['src/update-core.cpp', 'src/update-package.cpp']
NET_LIBS = ['-lwinhttp', '-lbcrypt', '-lshlwapi', '-lshell32', '-ladvapi32', '-lole32']
failures: list[str] = []


def sh(cmd, **kw):
    print('+', ' '.join(str(c) for c in cmd)[:240], flush=True)
    return subprocess.run([str(c) for c in cmd], cwd=ROOT, check=True, **kw)


def check(ok: bool, what: str):
    print(('PASS ' if ok else 'FAIL ') + what, flush=True)
    if not ok:
        failures.append(what)


def tree_hash(path: Path) -> str:
    h = hashlib.sha256()
    for f in sorted((p for p in path.rglob('*') if p.is_file()), key=lambda p: p.as_posix().lower()):
        h.update(f.relative_to(path).as_posix().encode() + b'\0' + hashlib.sha256(f.read_bytes()).digest())
    return h.hexdigest()


def make_profile(path: Path, tag: str):
    for rel, data in {'EBWebView/Local State': '{"canary":"%s"}' % tag,
                      'EBWebView/Default/Network/Cookies': 'cookie-canary-' + tag,
                      'EBWebView/Default/Login Data': 'login-canary-' + tag,
                      'unknown-future-file.bin': os.urandom(4096).hex()}.items():
        f = path / rel
        f.parent.mkdir(parents=True, exist_ok=True)
        f.write_text(data, encoding='utf-8')


HISTORY_ROW = 'C\t1234\t1780000000000\tC:\\canary\\old-report.pdf\thttps://example.com/old-report.pdf\t\n'


def make_history(path: Path):
    path.write_text('browser-downloads 2\n' + HISTORY_ROW, encoding='utf-8', newline='\n')


def check_history(path: Path, what: str):
    text = path.read_text(encoding='utf-8') if path.exists() else ''
    check('https://example.com/old-report.pdf' in text and 'https://example.com/canary-download.bin' in text,
          f'{what}: download history keeps the old row and the row saved on close')


def build_fixtures(work: Path) -> dict:
    gen = work / 'gen'
    B.write_build_info(gen, V.identity(int(OLD.split('.')[-1]), 'f' * 40, 'dev'), 'VALXXII/browser-releases')
    host = work / 'host_e2e.exe'
    sh([GXX, *FLAGS, '-mconsole', f'-I{gen}', '-DBROWSER_HAS_BUILD_INFO', '-DTVG_STATIC', '-Ideps/thorvg-1.1.3/inc',
        f'-I{B.SDK / "include"}', 'tests/update-integration/host_e2e.cpp', 'src/native-lottie.cpp', *CORE,
        'src/update-toast.cpp', 'src/annotations.cpp', 'src/agent-connect.cpp',   # stamped dev identity: main.cpp compiles the annotation mode in
        B.THORVG, '-o', host, *B.BROWSER_LIBS])
    helper = work / 'update-helper.exe'
    sh([GXX, *FLAGS, '-mwindows', *B.HELPER_SOURCES, '-o', helper, *B.HELPER_LIBS])
    fake = work / 'fake-browser.exe'
    sh([GXX, *FLAGS, '-mwindows', f'-DFAKE_VERSION=L"{NEW}"', '-Itests/update-core',
        'tests/update-core/fake_browser.cpp', *CORE, '-o', fake, *NET_LIBS])
    payload = work / 'payload'
    shutil.rmtree(payload, ignore_errors=True)
    (payload / 'assets').mkdir(parents=True)
    shutil.copy2(fake, payload / 'browser.exe')
    shutil.copy2(helper, payload / 'update-helper.exe')
    shutil.copytree(ROOT / 'assets/fonts', payload / 'assets/fonts')
    shutil.copy2(ROOT / 'src/icons/browser-live.ico', payload / 'assets/browser.ico')  # as build.py stamps it
    shutil.copy2(ROOT / 'third_party/thorvg/LICENSE', payload / 'ThorVG-LICENSE.txt')
    (payload / 'version.json').write_text(json.dumps({'version': NEW, 'arch': 'x64'}), encoding='utf-8')
    serve = work / 'serve'
    shutil.rmtree(serve, ignore_errors=True)
    sh([sys.executable, '-I', ROOT / 'tools/update-package/make_update_package.py', '--input', payload,
        '--version', NEW, '--out', serve])
    return {'host': host, 'helper': helper, 'serve': serve}


def place_app(dest: Path, fx: dict, exe_name: str = 'browser.exe') -> Path:
    dest.mkdir(parents=True, exist_ok=True)
    shutil.copy2(fx['host'], dest / exe_name)
    shutil.copy2(fx['helper'], dest / 'update-helper.exe')
    shutil.copytree(ROOT / 'assets', dest / 'assets', dirs_exist_ok=True)  # toast fonts + download panel icons
    return dest / exe_name


def run_host(exe: Path, mode: str, root: Path, serve: Path, report: Path, arg: str = OLD) -> int:
    proc = subprocess.run([str(exe), mode, str(root), str(serve), arg, str(report)], cwd=exe.parent, timeout=120)
    print(report.read_text(encoding='utf-8') if report.exists() else '(no report)')
    return proc.returncode


def wait_install(root: Path, timeout=120.0) -> str:
    log = root / 'fake-launches.txt'
    end = time.time() + timeout
    while time.time() < end:  # bounded wait for the separate helper process
        if log.exists() and f'start {NEW}' in log.read_text(encoding='utf-8'):
            time.sleep(1.0)
            return log.read_text(encoding='utf-8')
        if (root / 'updates/last-result').exists() and 'fail' in (root / 'updates/last-result').read_text(errors='replace'):
            break
        time.sleep(0.25)
    return log.read_text(encoding='utf-8') if log.exists() else ''


def common_install_checks(root: Path, launches: str, session_file: Path):
    check(f'start {NEW} phase=6' in launches, f'new version {NEW} started by the helper and acknowledged (Installed once)')
    check((root / 'current').read_text().strip() == NEW, 'current points to the new version')
    check((root / 'versions' / NEW / 'browser.exe').is_file(), 'new version installed in versions\\' + NEW)
    check((root / 'browser.exe').is_file(), 'stable launcher installed at <root>\\browser.exe')
    check((root / 'browser.ico').read_bytes() == (ROOT / 'src/icons/browser-live.ico').read_bytes(),
          'stable <root>\\browser.ico refreshed from the installed version (shortcut icon follows updates)')
    check(not (root / 'versions' / NEW / 'profile').exists() and not (root / 'versions' / NEW / 'session').exists(),
          'no profile/session inside the version directory')
    if session_file.exists():
        text = session_file.read_text(encoding='utf-8')
        check('https://example.org/b?q=canary' in text, 'session written by the host survives the install')
    else:
        check(False, f'session file missing: {session_file}')


def scenario_installed(base: Path, fx: dict):
    root = base / 'root'
    app = root / 'versions' / OLD
    place_app(app, fx)
    (root / 'current').write_text(OLD + '\n')
    make_profile(root / 'data/profile', 'installed')
    make_history(root / 'data/downloads')
    saved_bookmarks=b'browser-bookmarks 1\n3\t1\tL\t1\tCanary\thttps://bookmarks.example/\nend\t1\n'
    (root / 'data/bookmarks').write_bytes(saved_bookmarks)
    before = tree_hash(root / 'data/profile')
    rc = run_host(app / 'browser.exe', 'install', root, fx['serve'], base / 'host-report.txt')
    check(rc == 0, 'host flow (installed): Available -> Download -> Ready -> RestartInstall -> session saved -> clean exit')
    launches = wait_install(root)
    common_install_checks(root, launches, root / 'data/session')
    check((app / 'browser.exe').is_file(), f'previous version {OLD} retained for rollback')
    check(tree_hash(root / 'data/profile') == before, 'stable profile byte-identical after install (canary hash)')
    check_history(root / 'data/downloads', 'installed')
    check((root / 'data/bookmarks').read_bytes()==saved_bookmarks,'installed bookmarks survive the update byte-for-byte')
    check(not (app / 'downloads').exists(), 'no download history written inside the version directory')


def scenario_portable(base: Path, fx: dict):
    portable, root = base / 'portable', base / 'root'
    # Real dev builds are named like build2rowser-main-78f5340.exe, not browser.exe.
    exe = place_app(portable, fx, 'browser-main-78f5340.exe')
    make_profile(portable / 'profile', 'portable')
    (portable / 'session').write_text('browser-session-1\n0\n1\n"https://legacy.example"\n', encoding='utf-8')
    make_history(portable / 'downloads')
    saved_bookmarks=b'browser-bookmarks 1\n3\t1\tL\t1\tCanary\thttps://bookmarks.example/\nend\t1\n'
    (portable / 'bookmarks').write_bytes(saved_bookmarks)
    rc = run_host(exe, 'install', root, fx['serve'], base / 'host-report.txt')
    check(rc == 0, 'host flow (portable/legacy exe-adjacent data): install requested and clean exit')
    legacy_before = tree_hash(portable / 'profile')  # after host exit, before/while helper migrates
    session_before = (portable / 'session').read_bytes()
    history_before = (portable / 'downloads').read_bytes()
    launches = wait_install(root)
    common_install_checks(root, launches, root / 'data/session')
    check(tree_hash(root / 'data/profile') == legacy_before, 'one-time migration copied the legacy profile exactly (canary hash)')
    check(tree_hash(portable / 'profile') == legacy_before, 'legacy profile left untouched (copy, never move)')
    check((portable / 'session').read_bytes() == session_before, 'legacy session left untouched')
    check_history(portable / 'downloads', 'portable (written by the legacy app on close)')
    migrated = root / 'data/downloads'
    check(migrated.exists() and migrated.read_bytes() == history_before, 'one-time migration copied the download history exactly')
    check((portable / 'downloads').read_bytes() == history_before, 'legacy download history left untouched')
    check((root / 'data/bookmarks').read_bytes()==saved_bookmarks,'portable bookmarks are copied to the installed data directory')
    check((portable / 'bookmarks').read_bytes()==saved_bookmarks,'portable bookmarks remain untouched')
    check((root / 'data/bookmarks.bak').read_bytes()==saved_bookmarks,'portable bookmarks recovery copy is migrated too')
    relaunch_legacy(base, root, portable, exe, fx)


def launches(root: Path) -> list[str]:
    log = root / 'fake-launches.txt'
    return log.read_text(encoding='utf-8').splitlines() if log.exists() else []


def wait_new_line(root: Path, count: int, prefix: str, timeout=20.0) -> str:
    end = time.time() + timeout
    while time.time() < end:  # bounded wait for the launched fake process
        lines = launches(root)
        if len(lines) > count and any(l.startswith(prefix) for l in lines[count:]):
            return next(l for l in lines[count:] if l.startswith(prefix))
        time.sleep(0.2)
    return ''


def relaunch_legacy(base: Path, root: Path, portable: Path, exe: Path, fx: dict):
    """The old Start Menu shortcut target (the legacy portable exe itself) is started again."""
    profile_before = tree_hash(portable / 'profile')
    n = len(launches(root))
    rc = run_host(exe, 'relaunch', root, fx['serve'], base / 'relaunch-report.txt', 'expect-forward')
    check(rc == 0, 'legacy shortcut target relaunched after migration hands over (production ForwardToInstalled)')
    line = wait_new_line(root, n, f'start {NEW} ')
    data = str((root / 'data').resolve())
    check(bool(line) and line.lower().endswith('data=' + data.lower()),
          f'installed current {NEW} started on the stable data folder ({line!r})')
    check(tree_hash(portable / 'profile') == profile_before, 'legacy profile still untouched after the hand-over')
    # Guards (no hand-over): tampered migration record, failed install of current, bogus current pointer.
    record = root / 'data' / 'migrated-from.txt'
    original = record.read_bytes()
    record.write_text(str(base / 'somewhere-else') + '\n', encoding='utf-8')
    check(run_host(exe, 'relaunch', root, fx['serve'], base / 'guard1.txt', 'expect-stay') == 0,
          'guard: migration record of another folder -> legacy stays portable')
    record.write_bytes(original)
    result = root / 'updates' / 'last-result'
    saved_result = result.read_bytes() if result.exists() else None
    result.write_text(f'browser-result 1\nstatus failed\nversion {NEW}\nreason no-startup-ack\nreported 0\n', encoding='utf-8')
    check(run_host(exe, 'relaunch', root, fx['serve'], base / 'guard2.txt', 'expect-stay') == 0,
          'guard: install of current recorded as failed (rollback) -> never forwards to it')
    if saved_result is None:
        result.unlink()
    else:
        result.write_bytes(saved_result)
    current = root / 'current'
    current.write_text('..\\..\\evil\n', encoding='utf-8')
    check(run_host(exe, 'relaunch', root, fx['serve'], base / 'guard3.txt', 'expect-stay') == 0,
          'guard: bogus current pointer -> legacy stays portable')
    current.write_text(NEW + '\n', encoding='utf-8')
    # Installed kind never forwards (no loop): the installed copy of the host exe in a versions dir.
    installed_host = root / 'versions' / '9.9.9' / 'browser.exe'
    installed_host.parent.mkdir(parents=True)
    shutil.copy2(fx['host'], installed_host)
    shutil.copytree(ROOT / 'assets', installed_host.parent / 'assets')
    check(run_host(installed_host, 'relaunch', root, fx['serve'], base / 'guard4.txt', 'expect-stay') == 0,
          'guard: installed-kind exe never forwards (no launch loop)')
    shutil.rmtree(installed_host.parent)
    check(len([l for l in launches(root)[n:] if l.startswith('start ')]) == 1, 'exactly one hand-over launch across all guard runs')
    check(exe.is_file(), 'legacy portable app (browser-main-78f5340.exe) left in place')


def scenario_periodic(base: Path, fx: dict):
    """Running browser, mocked clock: startup finds nothing, the one-shot due-aware timer later finds 0.3.6."""
    root = base / 'root'
    app = root / 'versions' / OLD
    place_app(app, fx)
    (root / 'current').write_text(OLD + '\n')
    make_profile(root / 'data/profile', 'periodic')
    before = tree_hash(root / 'data/profile')
    rc = run_host(app / 'browser.exe', 'periodic', root, fx['serve'], base / 'host-report.txt')
    check(rc == 0, 'periodic: newer release found without restart, TTL respected, timer bounded and cancelled on close')
    check(not (root / 'fake-launches.txt').exists(), 'periodic: nothing installed or launched without RestartInstall')
    check(tree_hash(root / 'data/profile') == before, 'periodic: profile untouched')


def scenario_inprocess(base: Path, fx: dict, mode: str):
    root = base / 'root'
    app = root / 'versions' / OLD
    place_app(app, fx)
    (root / 'current').write_text(OLD + '\n')
    make_profile(root / 'data/profile', mode)
    before = tree_hash(root / 'data/profile')
    serve = fx['serve'] if mode == 'cancel' else base / 'empty-feed'
    serve.mkdir(exist_ok=True)
    t0 = time.time()
    rc = run_host(app / 'browser.exe', mode, root, serve, base / 'host-report.txt')
    check(rc == 0, f'host flow ({mode})')
    check(time.time() - t0 < 60, f'{mode}: finished promptly')
    check(not list((root / 'updates').glob('install-*')) if (root / 'updates').exists() else True,
          f'{mode}: no install plan / helper launched without RestartInstall')
    check(not (root / 'fake-launches.txt').exists(), f'{mode}: nothing was launched')
    check(tree_hash(root / 'data/profile') == before, f'{mode}: profile untouched')


def main() -> int:
    work = OUT / 'e2e'
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)
    fx = build_fixtures(work / 'fixtures')
    for name, fn in (('installed', scenario_installed), ('portable', scenario_portable),
                     ('cancel', lambda b, f: scenario_inprocess(b, f, 'cancel')),
                     ('offline', lambda b, f: scenario_inprocess(b, f, 'offline')),
                     ('norelease', lambda b, f: scenario_inprocess(b, f, 'norelease')),
                     ('periodic', scenario_periodic)):
        print(f'\n===== scenario {name}', flush=True)
        base = work / name
        base.mkdir()
        fn(base, fx)
    leftovers = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq update-helper*', '/FO', 'CSV', '/NH'],
                               capture_output=True, text=True).stdout
    check('update-helper' not in leftovers.lower(), 'no update helper process left running')
    print('\nE2E', 'FAILED: ' + '; '.join(failures) if failures else 'PASSED')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
