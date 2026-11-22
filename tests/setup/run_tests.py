#!/usr/bin/env python3
"""Build and run the Windows setup suite (no WebView2, no network, nothing of the user touched).

    python -I tests/setup/run_tests.py [--only <case>[,<case>...]] [--keep]

Every case runs the REAL setup stub (test build: -DSETUP_TESTING) assembled exactly like the release
(tools/release/package.py: stub || .lsrupd || trailer) around a synthetic payload whose browser.exe is the
update-core fake browser (acks its start like the real one) and whose update-helper.exe is the real helper
(fault build). Isolation, per case under build/setup-tests/tmp/<case>:
  LOCALAPPDATA=<T>\\LocalAppData  -> install root <T>\\LocalAppData\\browser
  BROWSER_SETUP_TEST_ROOT=<T>     -> Start Menu, Desktop, Temp under <T>; uninstall key and the default-browser
                                     registration (src/default-browser.h) under
                                     HKCU\\Software\\browser-setup-tests\\<case> (deleted afterwards)
  TEMP/TMP=<T>\\Temp, BROWSER_SETUP_TEST_WEBVIEW2=1, BROWSER_SETUP_TEST_LANG=en
The real %LOCALAPPDATA%\\browser, Start Menu, Desktop, uninstall entry and default-browser registration are never
read or written.
"""
from __future__ import annotations
import argparse, ctypes, hashlib, json, os, shutil, struct, subprocess, sys, time, traceback, winreg
from ctypes import wintypes as W
from pathlib import Path
os.environ.setdefault('BROWSER_NO_ONBOARDING', '1')   # fresh installed layout: not the first-run window (src/onboarding-flow.h)

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'build' / 'setup-tests'
TMP = OUT / 'tmp'
GXX = str(ROOT / 'tools/w64devkit/bin/g++.exe')
WINDRES = str(ROOT / 'tools/w64devkit/bin/windres.exe')
FLAGS = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Wno-cast-function-type', '-Wno-missing-field-initializers',
         '-static', '-municode', '-Isrc']
CORE = ['src/update-core.cpp', 'src/update-package.cpp']
SETUP_LIBS = ['-lwinhttp', '-lbcrypt', '-lole32', '-luuid', '-lshell32', '-lshlwapi', '-lcomctl32', '-ladvapi32', '-luser32', '-lgdi32']
COMMIT = '5e' * 20
sys.path.insert(0, str(ROOT / 'tools/release'))
import package as K  # noqa: E402

# SetupCore::Error + 10 (src/setup-main.cpp exit codes); 3 = no payload (truncated download).
E = {'ok': 0, 'nopayload': 3, 'damaged': 11, 'unsafe': 12, 'newer': 13, 'running': 14, 'disk': 15, 'cancelled': 16,
     'helper': 17, 'start': 18, 'integration': 19, 'busy': 20}
VERSIONS = ('0.3.2', '0.3.3')
failures: list[str] = []
checks = 0


SRC_BASE = [ROOT]  # --src-rev compiles the stub, helper and fakes from another revision (red runs)


def run(cmd, label):
    print(f'$ {label}', flush=True)
    r = subprocess.run([str(c) for c in cmd], cwd=SRC_BASE[0], capture_output=True, text=True)
    if r.returncode:
        print((r.stdout or '') + (r.stderr or ''))
        sys.exit(f'build failed: {label}')


def build():
    OUT.mkdir(parents=True, exist_ok=True)
    run([GXX, *FLAGS, '-mwindows', '-DUPDATE_HELPER_FAULTS', 'src/update-helper.cpp', 'src/update-package.cpp',
         '-o', OUT / 'update-helper.exe', '-lbcrypt', '-lshlwapi', '-lshell32', '-luser32', '-lole32', '-ladvapi32'],
        'compile update-helper.exe (fault build)')
    for v in VERSIONS:
        for crash in (0, 1):
            name = f'fake-{v}{"-crash" if crash else ""}.exe'
            run([GXX, *FLAGS, '-mwindows', f'-DFAKE_VERSION=L"{v}"', f'-DFAKE_CRASH={crash}', 'tests/update-core/fake_browser.cpp',
                 *CORE, '-o', OUT / name, '-lwinhttp', '-lbcrypt'], f'compile {name}')
        gen = OUT / f'gen-{v}'
        gen.mkdir(exist_ok=True)
        (gen / 'build-info.h').write_text(
            '#pragma once\n'
            f'#define BROWSER_VERSION_TEXT "{v}"\n#define BROWSER_VERSION_WIDE L"{v}"\n'
            f'#define BROWSER_COMMIT_TEXT "{COMMIT}"\n#define BROWSER_CHANNEL_TEXT "dev"\n'
            f'#define BROWSER_FILEVERSION {v.replace(".", ",")},0\n', encoding='ascii', newline='\n')
        res = OUT / f'setup-res-{v}.o'
        run([WINDRES, '-I.', '-Isrc', f'-I{gen}', '-DBROWSER_HAS_BUILD_INFO', 'src/setup.rc', '-O', 'coff', '-o', res], f'windres setup {v}')
        run([GXX, *FLAGS, '-mwindows', f'-I{gen}', '-DSETUP_TESTING', 'src/setup-main.cpp', 'src/setup-core.cpp', *CORE, res,
             '-o', OUT / f'stub-{v}.exe', *SETUP_LIBS], f'compile setup stub {v} (test build)')
        run([str(ROOT / 'tools/w64devkit/bin/strip.exe'), '--strip-all', OUT / f'stub-{v}.exe'], f'strip setup stub {v}')


# ---------------------------------------------------------------- payloads / setups
def payload(v: str, crash=False) -> Path:
    d = OUT / f'payload-{v}{"-crash" if crash else ""}'
    if d.exists():
        shutil.rmtree(d)
    (d / 'assets/bin').mkdir(parents=True)
    (d / 'assets/fonts').mkdir()
    shutil.copy(OUT / f'fake-{v}{"-crash" if crash else ""}.exe', d / 'browser.exe')
    shutil.copy(OUT / 'update-helper.exe', d / 'update-helper.exe')
    (d / 'WebView2Loader.dll').write_bytes(b'loader')
    (d / 'ThorVG-LICENSE.txt').write_text('license')
    (d / 'version.json').write_text(json.dumps({'version': v, 'commit': COMMIT, 'channel': 'dev', 'arch': 'x64'}))
    shutil.copy(ROOT / 'src/icons/browser-preview.ico', d / 'assets/browser.ico')
    (d / 'assets/bin/browser-annotations-mcp.exe').write_bytes(b'MZ bridge ' + v.encode())
    (d / 'assets/fonts/Inter (1).ttf').write_bytes(b'font ' + v.encode())
    return d


def pack(v: str, crash=False) -> Path:
    d = payload(v, crash)
    out = OUT / f'dist-{v}{"-crash" if crash else ""}'
    shutil.rmtree(out, ignore_errors=True)
    r = subprocess.run([sys.executable, '-I', str(ROOT / 'tools/update-package/make_update_package.py'), '--input', str(d),
                        '--version', v, '--out', str(out)], capture_output=True, text=True)
    if r.returncode:
        sys.exit(r.stderr)
    return out / f'browser-{v}-win-x64.lsrupd'


def assemble(stub_version: str, package: Path, out: Path, version: str | None = None) -> Path:
    """Exactly package.write_setup's layout (the test stub is refused by write_setup on purpose)."""
    stub = (OUT / f'stub-{stub_version}.exe').read_bytes()
    pkg = package.read_bytes()
    out.write_bytes(stub + pkg + K.setup_trailer(version or stub_version, len(stub), pkg))
    return out


def lsrupd_entries(data: bytes) -> dict:
    pos, out = 0, {}

    def take(n):
        nonlocal pos
        pos += n
        return data[pos - n:pos]
    assert take(8) == b'LSRUPD01'
    take(struct.unpack('<H', take(2))[0])
    take(struct.unpack('<H', take(2))[0])
    for _ in range(struct.unpack('<I', take(4))[0]):
        name = take(struct.unpack('<H', take(2))[0]).decode('ascii')
        size = struct.unpack('<Q', take(8))[0]
        take(32)
        out[name] = take(size)
    assert take(8) == b'LSRUEND1' and pos == len(data)
    return out


def craft_lsrupd(version: str, entries: list[tuple[str, bytes]]) -> bytes:
    """A structurally valid .lsrupd with arbitrary entry names (the packer refuses bad ones; the reader must too)."""
    b = b'LSRUPD01' + struct.pack('<H', len(version)) + version.encode() + struct.pack('<H', 3) + b'x64'
    b += struct.pack('<I', len(entries))
    for name, data in entries:
        b += struct.pack('<H', len(name)) + name.encode() + struct.pack('<Q', len(data)) + hashlib.sha256(data).digest() + data
    return b + b'LSRUEND1'


# ---------------------------------------------------------------- case environment
class Case:
    def __init__(self, name: str):
        self.name = name
        self.t = TMP / name
        if self.t.exists():
            shutil.rmtree(self.t)
        for sub in ('LocalAppData', 'StartMenu/Programs', 'Desktop', 'Temp'):
            (self.t / sub).mkdir(parents=True)
        self.root = self.t / 'LocalAppData' / 'browser'
        self.programs, self.desktop = self.t / 'StartMenu/Programs', self.t / 'Desktop'
        self.key = rf'Software\browser-setup-tests\{name}\Uninstall\VALXXII.browser'
        self.env = dict(os.environ, LOCALAPPDATA=str(self.t / 'LocalAppData'), BROWSER_SETUP_TEST_ROOT=str(self.t),
                        TEMP=str(self.t / 'Temp'), TMP=str(self.t / 'Temp'), BROWSER_SETUP_TEST_WEBVIEW2='1',
                        BROWSER_SETUP_TEST_LANG='en')
        for k in ('BROWSER_SETUP_FAULT', 'BROWSER_HELPER_FAULT'):
            self.env.pop(k, None)

    def setup(self, exe: Path, *args, fault=None, helper_fault=None, expect=0, label=''):
        env = dict(self.env)
        if fault:
            env['BROWSER_SETUP_FAULT'] = fault
        if helper_fault:
            env['BROWSER_HELPER_FAULT'] = helper_fault
        r = subprocess.run([str(exe), '--unattended', *args], env=env, cwd=str(self.t), timeout=600)
        check(r.returncode == expect, f'{label or exe.name} exit {r.returncode}, expected {expect}')
        return r.returncode

    def popen_setup(self, exe: Path, *args, fault=None, helper_fault=None):
        env = dict(self.env)
        if fault:
            env['BROWSER_SETUP_FAULT'] = fault
        if helper_fault:
            env['BROWSER_HELPER_FAULT'] = helper_fault
        return subprocess.Popen([str(exe), '--unattended', *args], env=env, cwd=str(self.t))

    def uninstall(self, *args, expect=0, label='uninstall', fault=None, wait=True):
        """<root>/uninstall.exe re-runs from a temp copy and exits at once; the copy reports its exit code."""
        result = self.t / 'uninstall-result.txt'
        result.unlink(missing_ok=True)
        env = dict(self.env)
        if fault:
            env['BROWSER_SETUP_FAULT'] = fault
        r = subprocess.run([str(self.root / 'uninstall.exe'), '--unattended', *args], env=env, cwd=str(self.t), timeout=60)
        check(r.returncode == 0, f'{label}: in-root uninstaller exit {r.returncode}')
        if not wait:
            return
        for _ in range(600):
            if result.exists():
                break
            time.sleep(0.2)
        code = int(result.read_text()) if result.exists() else None
        check(code == expect, f'{label} exit {code}, expected {expect}')

    def copy_setup(self, version: str, package: Path, version_override=None) -> Path:
        return assemble(version, package, self.t / f'browser-{version}-win-x64-setup.exe', version_override)

    def shortcut_target(self, lnk: Path) -> str:
        out = self.t / 'lnk-target.txt'
        subprocess.run([str(OUT / 'stub-0.3.2.exe'), '--shortcut-target', str(lnk), str(out)], env=self.env, timeout=60)
        return out.read_text() if out.exists() else ''

    def reg(self) -> dict:
        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, self.key) as k:
                out, i = {}, 0
                while True:
                    try:
                        n, v, _ = winreg.EnumValue(k, i)
                    except OSError:
                        return out
                    out[n] = v
                    i += 1
        except FileNotFoundError:
            return {}

    def cleanup_registry(self):
        delete_tree(rf'Software\browser-setup-tests\{self.name}')   # this case's own test key only

    # Default-browser registration (src/default-browser.h), written by the setup and the helper under the case's key.
    def db(self, rel: str) -> str:
        return rf'Software\browser-setup-tests\{self.name}\DefaultBrowser\Software\{rel}'

    def db_value(self, rel: str, name: str = ''):
        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, self.db(rel)) as k:
                return winreg.QueryValueEx(k, name)[0]
        except OSError:
            return None

    def db_registered_ok(self, label: str):
        root = str(self.root)
        client = r'Clients\StartMenuInternet\ValXXII.browser'
        caps = client + r'\Capabilities'
        check(self.db_value(client) == 'Browser' and self.db_value(caps, 'ApplicationName') == 'Browser', f'{label}: listed as « Browser » in Applications par défaut')
        check(self.db_value(client + r'\shell\open\command') == f'"{root}\\browser.exe"', f'{label}: client opens the stable launcher')
        check(self.db_value(caps + r'\URLAssociations', 'http') == 'ValXXII.BrowserURL' and self.db_value(caps + r'\URLAssociations', 'https') == 'ValXXII.BrowserURL', f'{label}: http/https -> ValXXII.BrowserURL')
        check(self.db_value(caps + r'\FileAssociations', '.html') == 'ValXXII.BrowserHTML' and self.db_value(caps + r'\FileAssociations', '.pdf') == 'ValXXII.BrowserPDF', f'{label}: .html/.pdf associations')
        reg_apps = self.db_value('RegisteredApplications', 'ValXXII.browser')
        check(reg_apps == self.db(caps), f'{label}: RegisteredApplications -> capabilities ({reg_apps})')
        for prog in ('ValXXII.BrowserHTML', 'ValXXII.BrowserURL', 'ValXXII.BrowserPDF'):
            cmd = self.db_value(rf'Classes\{prog}\shell\open\command')
            check(cmd == f'"{root}\\browser.exe" --single-argument "%1"', f'{label}: {prog} opens "<root>\\browser.exe" --single-argument "%1" ({cmd})')
            check(self.db_value(rf'Classes\{prog}\DefaultIcon') == f'{root}\\browser.ico,0', f'{label}: {prog} stable icon')
        check(self.db_value(caps, 'ApplicationDescription') == 'Web browser for Windows.', f'{label}: description in the setup language (en)')

    def db_absent(self) -> bool:
        return all(self.db_value(rel, name) is None for rel, name in (
            (r'Clients\StartMenuInternet\ValXXII.browser', ''), ('RegisteredApplications', 'ValXXII.browser'),
            (r'Classes\ValXXII.BrowserURL\shell\open\command', ''), (r'Classes\ValXXII.BrowserHTML', ''),
            (r'Classes\ValXXII.BrowserPDF', ''), (r'Classes\.html\OpenWithProgids', 'ValXXII.BrowserHTML')))

    def on_disk_name(self, path: Path) -> str:
        return next((n for n in os.listdir(path.parent) if n.lower() == path.name.lower()), '')

    def launches(self) -> list[str]:
        f = self.root / 'fake-launches.txt'
        return f.read_text().splitlines() if f.exists() else []


def delete_tree(sub: str):
    """Recursive delete of one test key (never anything outside HKCU\\Software\\browser-setup-tests)."""
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


def check(cond, message):
    global checks
    checks += 1
    if not cond:
        failures.append(message)
        print(f'  FAIL {message}', flush=True)


def tree(path: Path) -> dict:
    """{relative posix path: sha256} of regular files (no reparse descent)."""
    out = {}
    if not path.exists():
        return out
    for p in sorted(path.rglob('*')):
        if p.is_file() and not p.is_symlink() and p.name != 'install.lock':  # (held open while a transaction runs)
            out[p.relative_to(path).as_posix()] = hashlib.sha256(p.read_bytes()).hexdigest()
    return out


def data_canaries(root: Path) -> dict:
    d = root / 'data'
    files = {'profile/EBWebView/Default/Cookies': b'canary-cookie', 'profile/EBWebView/Local State': b'{"canary":1}',
             'session': b'browser-session-1\ncanary\n', 'bookmarks': b'canary-bookmarks', 'ui-language': b'browser-ui-language 1\nlanguage=en\n',
             'annotations/store.json': b'{"canary":"annotation"}', 'profiles.txt': b'canary-profiles',
             'profiles/work/profile/EBWebView/Default/History': b'canary-history', 'downloads': b'canary-downloads'}
    for rel, data in files.items():
        (d / rel).parent.mkdir(parents=True, exist_ok=True)
        (d / rel).write_bytes(data)
    return tree(d)


kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)
kernel32.CreateFileW.restype = W.HANDLE
kernel32.CreateFileW.argtypes = [W.LPCWSTR, W.DWORD, W.DWORD, ctypes.c_void_p, W.DWORD, W.DWORD, W.HANDLE]


def hold_exclusive(path: Path):
    h = kernel32.CreateFileW(str(path), 0x80000000, 0, None, 3, 0x80, None)  # GENERIC_READ, no sharing, OPEN_EXISTING
    assert h and h != W.HANDLE(-1).value, ctypes.get_last_error()
    return h


def hold_exclusive_create(path: Path):
    h = kernel32.CreateFileW(str(path), 0xC0000000, 0, None, 4, 0x80, None)  # OPEN_ALWAYS, no sharing
    assert h and h != W.HANDLE(-1).value, ctypes.get_last_error()
    return h


def junction(link: Path, target: Path):
    import _winapi
    _winapi.CreateJunction(str(target), str(link))


def installed_ok(c: Case, v: str, payload_dir: Path, package: Path):
    """The installed version directory is byte-identical to the payload, to the .lsrupd entries (== ZIP contents)."""
    vdir = c.root / 'versions' / v
    got = {k: h for k, h in tree(vdir).items() if k != '.stage'}
    check(got == tree(payload_dir), f'versions/{v} != payload')
    entries = {k: hashlib.sha256(b).hexdigest() for k, b in lsrupd_entries(package.read_bytes()).items()}
    check(got == entries, f'versions/{v} != .lsrupd entries')
    check((c.root / 'current').read_text().strip() == v, f'current is {v}')
    launcher = c.root / 'browser.exe'
    check(launcher.exists() and launcher.read_bytes() == (vdir / 'update-helper.exe').read_bytes(), 'stable launcher == helper')
    check((c.root / 'bin/browser-annotations-mcp.exe').read_bytes() == (vdir / 'assets/bin/browser-annotations-mcp.exe').read_bytes(),
          'stable bridge in bin == versioned bridge')
    check((c.root / 'browser.ico').read_bytes() == (vdir / 'assets/browser.ico').read_bytes(), 'stable icon')
    check((c.root / 'uninstall.exe').read_bytes() == (OUT / f'stub-{v}.exe').read_bytes(), 'uninstall.exe == setup stub (no payload)')
    start = c.programs / 'Browser.lnk'
    check(start.exists() and c.shortcut_target(start).lower() == str(launcher).lower(), 'Start Menu shortcut -> stable launcher')
    check(c.on_disk_name(start) == 'Browser.lnk', f'Start Menu shortcut named « Browser » ({c.on_disk_name(start)})')
    reg = c.reg()
    check(reg.get('DisplayVersion') == v and reg.get('InstallLocation') == str(c.root) and
          reg.get('UninstallString') == f'"{c.root}\\uninstall.exe"' and reg.get('NoModify') == 1, f'uninstall entry {reg}')
    check(reg.get('DisplayName') == 'Browser', f'Applications et fonctionnalités: « Browser » ({reg.get("DisplayName")})')
    c.db_registered_ok('default-browser registration')
    leftovers = [p.name for p in (c.root / 'updates/downloads').glob('*')] if (c.root / 'updates/downloads').exists() else []
    check(not leftovers, f'no downloads leftovers {leftovers}')
    check(not any((c.root / 'updates/staging').glob('*')) if (c.root / 'updates/staging').exists() else True, 'staging consumed')


# ---------------------------------------------------------------- cases
P: dict = {}


def case_fresh_no_launch():
    c = Case('fresh_no_launch')
    s = c.copy_setup('0.3.2', P['0.3.2'])
    c.setup(s, '--no-launch')
    installed_ok(c, '0.3.2', OUT / 'payload-0.3.2', P['0.3.2'])
    check(not c.launches(), 'nothing launched with --no-launch')
    check(not (c.desktop / 'browser.lnk').exists(), 'no desktop shortcut by default')
    check(not (c.root / 'data').exists(), 'setup creates no data (the browser does, at first start)')
    check(K.pe_overlay(s.read_bytes())[0] == len((OUT / 'stub-0.3.2.exe').read_bytes()), 'overlay starts after the stub')
    return c


def case_fresh_launch_ack():
    c = Case('fresh_launch_ack')
    c.setup(c.copy_setup('0.3.2', P['0.3.2']), '--desktop')
    installed_ok(c, '0.3.2', OUT / 'payload-0.3.2', P['0.3.2'])
    res = (c.root / 'updates/last-result').read_text()
    check('status installed' in res and 'reason ack' in res, f'acknowledged start: {res!r}')
    check(any(l.startswith('start 0.3.2 phase=') and str(c.root / 'data') in l for l in c.launches()), f'launched with installed data {c.launches()}')
    check(c.shortcut_target(c.desktop / 'browser.lnk').lower() == str(c.root / 'browser.exe').lower(), 'desktop shortcut -> stable launcher')
    return c


def case_update_keeps_data_and_repair():
    c = Case('update_repair')
    c.setup(c.copy_setup('0.3.2', P['0.3.2']), '--no-launch', '--desktop')
    canaries = data_canaries(c.root)
    c.setup(c.copy_setup('0.3.3', P['0.3.3']), label='update 0.3.2 -> 0.3.3')
    installed_ok(c, '0.3.3', OUT / 'payload-0.3.3', P['0.3.3'])
    check(tree(c.root / 'data') == {**canaries, **{k: v for k, v in tree(c.root / 'data').items() if k not in canaries}} and
          all(tree(c.root / 'data').get(k) == h for k, h in canaries.items()), 'data canaries unchanged by update')
    check((c.root / 'previous').read_text().strip() == 'installed 0.3.2', 'previous = installed 0.3.2')
    check((c.root / 'versions/0.3.2').is_dir(), 'previous version kept for rollback')
    check(not (c.desktop / 'browser.lnk').exists(), 'desktop shortcut removed when the option is off (ours only)')
    # Repair: damage a payload file, run the same setup again.
    (c.root / 'versions/0.3.3/assets/fonts/Inter (1).ttf').write_bytes(b'damaged')
    c.setup(c.copy_setup('0.3.3', P['0.3.3']), '--no-launch', label='repair 0.3.3')
    installed_ok(c, '0.3.3', OUT / 'payload-0.3.3', P['0.3.3'])
    check(all(tree(c.root / 'data').get(k) == h for k, h in canaries.items()), 'data canaries unchanged by repair')
    check((c.root / 'previous').read_text().strip() == 'installed 0.3.2', 'repair keeps previous')
    check((c.root / 'versions/0.3.2').is_dir(), 'repair keeps the previous version')
    check(not list((c.root / 'versions').glob('.repair-*')), 'repair aside removed')
    # Newer installed: an older setup changes nothing.
    before = tree(c.root)
    c.setup(c.copy_setup('0.3.2', P['0.3.2']), expect=E['newer'], label='older setup over newer install')
    check(tree(c.root) == before, 'older setup changed nothing')
    return c


def case_foreign_shortcuts():
    c = Case('foreign_shortcuts')
    other = c.t / 'Downloads/browser-portable/browser.exe'
    other.parent.mkdir(parents=True)
    other.write_bytes(b'portable')
    # A foreign Start Menu link and desktop link (made with the stub's own shell link writer is not possible here:
    # copy a real .lnk produced by a first install into another root, so its target is a different copy).
    c2 = Case('foreign_shortcuts_seed')
    c2.setup(c2.copy_setup('0.3.2', P['0.3.2']), '--no-launch', '--desktop')
    seed = (c2.programs / 'browser.lnk').read_bytes()
    (c.programs / 'browser.lnk').write_bytes(seed)
    (c.desktop / 'browser.lnk').write_bytes(seed)
    c.setup(c.copy_setup('0.3.2', P['0.3.2']), '--no-launch', '--desktop')
    check((c.programs / 'browser.lnk').read_bytes() == seed, 'foreign Start Menu link untouched')
    check((c.desktop / 'browser.lnk').read_bytes() == seed, 'foreign desktop link untouched')
    check((c.root / 'current').read_text().strip() == '0.3.2', 'installed anyway')
    c2.cleanup_registry()
    return c


def case_running_browser():
    c = Case('running')
    c.setup(c.copy_setup('0.3.2', P['0.3.2']), '--no-launch')
    canaries = data_canaries(c.root)
    lock = c.root / 'data/profile/EBWebView/lockfile'
    lock.write_bytes(b'')
    before = tree(c.root)
    h = hold_exclusive(lock)
    try:
        c.setup(c.copy_setup('0.3.3', P['0.3.3']), expect=E['running'], label='update while profile locked')
    finally:
        kernel32.CloseHandle(h)
    check({k: v for k, v in tree(c.root).items() if not k.startswith('updates/')} ==
          {k: v for k, v in before.items() if not k.startswith('updates/')}, 'nothing changed while running')
    # A running browser.exe image (no lock yet): also refused, never killed.
    p = subprocess.Popen([str(c.root / 'versions/0.3.2/browser.exe'), '--hold', '6000'], env=c.env)
    time.sleep(0.5)
    c.setup(c.copy_setup('0.3.3', P['0.3.3']), expect=E['running'], label='update while browser.exe runs')
    check(p.poll() is None, 'running browser not killed')
    p.wait()
    check(all(tree(c.root / 'data').get(k) == v for k, v in canaries.items()), 'data canaries unchanged')
    c.setup(c.copy_setup('0.3.3', P['0.3.3']), '--no-launch', label='retry after close')
    check((c.root / 'current').read_text().strip() == '0.3.3', 'retry succeeds')
    return c


def case_damaged_setups():
    c = Case('damaged')
    good = c.copy_setup('0.3.2', P['0.3.2']).read_bytes()
    stub_len = len((OUT / 'stub-0.3.2.exe').read_bytes())

    def flip(data, at):
        b = bytearray(data)
        b[at] ^= 0x20
        return bytes(b)
    cases = {
        'truncated by 1 byte': (good[:-1], E['nopayload']),
        'truncated in the package': (good[:stub_len + 5000], E['nopayload']),
        'stub only': (good[:stub_len], E['nopayload']),
        'byte flipped in the package': (flip(good, stub_len + 3000), E['damaged']),
        'trailer hash flipped': (flip(good, len(good) - 128 + 70), E['damaged']),
        'trailer version other': (good[:-128] + good[-128:-112] + b'0.3.3'.ljust(32, b'\0') + good[-80:], E['nopayload']),
        'trailer size off by one': (good[:-72] + (int.from_bytes(good[-72:-64], 'little') - 1).to_bytes(8, 'little') + good[-64:], E['nopayload']),
        'appended bytes': (good + b'x', E['nopayload']),
    }
    for label, (data, code) in cases.items():
        f = c.t / 'bad-setup.exe'
        f.write_bytes(data)
        c.setup(f, '--no-launch', expect=code, label=label)
        check(not (c.root / 'current').exists() and not (c.root / 'versions').exists(), f'{label}: nothing installed')
        check(not list((c.root / 'updates/downloads').glob('*')) if (c.root / 'updates/downloads').exists() else True, f'{label}: no temp left')
        check(not (c.programs / 'browser.lnk').exists() and not c.reg(), f'{label}: no shortcut, no entry')
    # Valid trailer around a package that the updater's validator must refuse (traversal, allowlist, reparse names).
    outside = c.t / 'LocalAppData' / 'evil.txt'
    for label, entries in {
        'path traversal': [('browser.exe', b'x'), ('update-helper.exe', b'y'), ('../../evil.txt', b'evil')],
        'absolute path': [('browser.exe', b'x'), ('update-helper.exe', b'y'), ('C:/evil.txt', b'evil')],
        'not allowlisted (profile)': [('browser.exe', b'x'), ('update-helper.exe', b'y'), ('profile/Cookies', b'evil')],
        'duplicate (case)': [('browser.exe', b'x'), ('Browser.exe', b'x'), ('update-helper.exe', b'y')],
        'ADS colon': [('browser.exe', b'x'), ('update-helper.exe', b'y'), ('assets/a.txt:evil', b'evil')],
    }.items():
        pkg = c.t / 'evil.lsrupd'
        pkg.write_bytes(craft_lsrupd('0.3.2', entries))
        c.setup(assemble('0.3.2', pkg, c.t / 'evil-setup.exe'), '--no-launch', expect=E['damaged'], label=label)
        check(not outside.exists() and not (c.t / 'evil.txt').exists(), f'{label}: nothing written outside')
        check(not (c.root / 'current').exists(), f'{label}: nothing installed')
        staging = c.root / 'updates/staging'
        check(not list(staging.glob('*')) if staging.exists() else True, f'{label}: no partial staging left')
    # Corruption after the trailer check (copied package altered): the package validator refuses.
    c.setup(c.copy_setup('0.3.2', P['0.3.2']), '--no-launch', fault='corrupt-copied-package', expect=E['damaged'], label='copied package corrupted')
    check(not (c.root / 'current').exists(), 'corrupted copy: nothing installed')
    return c


def case_reparse_roots():
    c = Case('reparse')
    elsewhere = c.t / 'elsewhere'
    elsewhere.mkdir()
    (elsewhere / 'keep.txt').write_text('keep')
    c.root.mkdir(parents=True)
    junction(c.root / 'versions', elsewhere)
    c.setup(c.copy_setup('0.3.2', P['0.3.2']), '--no-launch', expect=E['unsafe'], label='versions is a junction')
    check(tree(elsewhere) == {'keep.txt': hashlib.sha256(b'keep').hexdigest()}, 'junction target untouched')
    os.rmdir(c.root / 'versions')
    shutil.rmtree(c.root)
    junction(c.root, elsewhere)
    c.setup(c.copy_setup('0.3.2', P['0.3.2']), '--no-launch', expect=E['unsafe'], label='root is a junction')
    check(tree(elsewhere) == {'keep.txt': hashlib.sha256(b'keep').hexdigest()}, 'root junction target untouched')
    os.rmdir(c.root)
    # data as a junction: install refused too (the browser would write the profile elsewhere).
    c.root.mkdir()
    junction(c.root / 'data', elsewhere)
    c.setup(c.copy_setup('0.3.2', P['0.3.2']), '--no-launch', expect=E['unsafe'], label='data is a junction')
    os.rmdir(c.root / 'data')
    return c


def case_faults_rollback():
    c = Case('faults')
    c.setup(c.copy_setup('0.3.2', P['0.3.2']), '--no-launch')
    canaries = data_canaries(c.root)
    # (`previous` is the helper's informational pointer; an aborted update does not roll it back either.)
    snapshot = {k: v for k, v in tree(c.root).items() if not k.startswith(('updates/', 'data/', 'fake-launches', 'previous'))}

    def unchanged(label):
        # An aborted helper may leave the not-current versions/0.3.3 behind (same as an aborted update; replaced or
        # pruned by the next install). Everything that was there must be byte-identical.
        now = {k: v for k, v in tree(c.root).items() if not k.startswith(('updates/', 'data/', 'fake-launches', 'versions/0.3.3/', 'previous'))}
        if now != snapshot:
            print('    diff:', sorted(set(now.items()) ^ set(snapshot.items()))[:6])
        check(now == snapshot, f'{label}: program files, current, launcher, bin unchanged')
        check(all(tree(c.root / 'data').get(k) == v for k, v in canaries.items()), f'{label}: data canaries unchanged')
    s = c.copy_setup('0.3.3', P['0.3.3'])
    c.setup(s, fault='extract', expect=E['disk'], label='extraction write failure'); unchanged('extract fault')
    c.setup(s, fault='mkdir', expect=E['disk'], label='folder creation failure'); unchanged('mkdir fault')
    c.setup(s, '--test-cancel', expect=E['cancelled'], label='cancel'); unchanged('cancel')
    # (launcher-busy is exercised by tests/update-core install_test: here both versions ship the same helper bytes.)
    for hf in ('move', 'current-write', 'bin-copy', 'bin-final'):
        c.setup(s, helper_fault=hf, expect=E['helper'], label=f'helper fault {hf}')
        unchanged(f'helper fault {hf}')
        check((c.root / 'current').read_text().strip() == '0.3.2', f'{hf}: current still 0.3.2')
    # New version crashes at start (no ack): rolled back to 0.3.2, data intact, failure not re-toasted.
    crash = assemble('0.3.3', P['0.3.3-crash'], c.t / 'crash-setup.exe')
    c.setup(crash, expect=E['start'], label='new version exits without ack')
    unchanged('no ack rollback')
    res = (c.root / 'updates/last-result').read_text()
    check('status failed' in res and 'reported 1' in res, f'failure recorded, already shown by setup: {res!r}')
    check(not (c.root / 'versions/0.3.3').exists(), 'failed version removed')
    # Repair fault: the live directory cannot be renamed (in use) -> intact.
    c.setup(c.copy_setup('0.3.2', P['0.3.2']), '--no-launch', helper_fault='repair-aside', expect=E['running'], label='repair aside fails')
    unchanged('repair aside fault')
    # Finally a clean update works.
    c.setup(s, '--no-launch', label='clean update after faults')
    installed_ok(c, '0.3.3', OUT / 'payload-0.3.3', P['0.3.3'])
    return c


def case_first_install_rollback():
    c = Case('first_install_rollback')
    c.setup(assemble('0.3.2', P['0.3.2-crash'], c.t / 'crash-setup.exe'), expect=E['start'], label='first install, no ack')
    check(not (c.root / 'current').exists(), 'first install rolled back: no current')
    check(not (c.root / 'versions/0.3.2').exists(), 'failed version removed')
    check(not (c.programs / 'browser.lnk').exists() and not c.reg(), 'no shortcut / entry for a failed install')
    c.setup(c.copy_setup('0.3.2', P['0.3.2']), '--no-launch', label='retry')
    installed_ok(c, '0.3.2', OUT / 'payload-0.3.2', P['0.3.2'])
    return c


def case_uninstall():
    c = Case('uninstall')
    c.setup(c.copy_setup('0.3.2', P['0.3.2']), '--no-launch', '--desktop')
    canaries = data_canaries(c.root)
    with winreg.CreateKey(winreg.HKEY_CURRENT_USER, c.db(r'Classes\.html\OpenWithProgids')) as k:   # another browser's value
        winreg.SetValueEx(k, 'OtherApp.HTML', 0, winreg.REG_SZ, '')
    c.db_registered_ok('before uninstall')
    # Refused while running.
    lock = c.root / 'data/profile/EBWebView/lockfile'
    lock.write_bytes(b'')
    h = hold_exclusive(lock)
    try:
        c.uninstall(expect=E['running'], label='uninstall while running')
    finally:
        kernel32.CloseHandle(h)
    check((c.root / 'current').exists() and (c.programs / 'browser.lnk').exists() and c.reg(), 'nothing removed while running')
    c.uninstall(label='uninstall (keep data)')
    for rel in ('versions', 'updates', 'bin', 'current', 'browser.exe', 'browser.ico', 'uninstall.exe'):
        check(not (c.root / rel).exists(), f'{rel} removed')
    check(not (c.programs / 'browser.lnk').exists() and not (c.desktop / 'browser.lnk').exists(), 'shortcuts removed')
    check(not c.reg(), 'uninstall entry removed')
    check(c.db_absent(), 'default-browser registration removed (Browser leaves Applications par défaut)')
    check(c.db_value(r'Classes\.html\OpenWithProgids', 'OtherApp.HTML') == '', "another app's « Ouvrir avec » value kept")
    check(all(tree(c.root / 'data').get(k) == v for k, v in canaries.items()), 'data kept byte-identical')
    check(list((c.t / 'Temp').glob('browser-uninstall-*.exe')), 'uninstaller ran from a temp copy')
    # Reinstall finds the kept data; then remove everything incl. data.
    c.setup(c.copy_setup('0.3.2', P['0.3.2']), '--no-launch', label='reinstall over kept data')
    check(all(tree(c.root / 'data').get(k) == v for k, v in canaries.items()), 'reinstall keeps data')
    (c.root / 'fake-launches.txt').unlink(missing_ok=True)
    c.uninstall('--remove-data', label='uninstall with data')
    check(not c.root.exists(), 'root removed entirely with --remove-data')
    return c


def case_uninstall_data_junction():
    c = Case('uninstall_junction')
    c.setup(c.copy_setup('0.3.2', P['0.3.2']), '--no-launch')
    elsewhere = c.t / 'elsewhere'
    elsewhere.mkdir()
    (elsewhere / 'precious.txt').write_text('precious')
    junction(c.root / 'data', elsewhere)
    c.uninstall('--remove-data', expect=E['unsafe'], label='uninstall with data junction')
    check((elsewhere / 'precious.txt').read_text() == 'precious', 'junction target untouched')
    check((c.root / 'current').exists(), 'nothing removed')
    os.rmdir(c.root / 'data')
    return c


def case_helper_refreshes_display_version():
    """After a setup install, the BROWSER'S updater (engine + helper, not the setup) updates: entry follows."""
    c = Case('engine_update_entry')
    c.setup(c.copy_setup('0.3.2', P['0.3.2']), '--no-launch')
    # The helper writes the registration texts in the person's interface language (the browser-wide preference).
    (c.root / 'data').mkdir(exist_ok=True)
    (c.root / 'data/ui-language').write_bytes(b'browser-ui-language 1\nlanguage=en\n')
    feed = c.t / 'feed'
    feed.mkdir()
    shutil.copy(P['0.3.3'], feed / P['0.3.3'].name)
    shutil.copy(P['0.3.3'].parent / 'update-manifest-win-x64.txt', feed / 'update-manifest-win-x64.txt')
    r = subprocess.run([str(c.root / 'versions/0.3.2/browser.exe'), '--update', str(c.root), str(feed)], env=c.env, timeout=120)
    check(r.returncode == 0, f'engine update exit {r.returncode}')
    for _ in range(100):
        if (c.root / 'current').read_text().strip() == '0.3.3' and 'installed' in (c.root / 'updates/last-result').read_text():
            break
        time.sleep(0.2)
    check((c.root / 'current').read_text().strip() == '0.3.3', 'engine update installed 0.3.3')
    time.sleep(1)
    check(c.reg().get('DisplayVersion') == '0.3.3', f'DisplayVersion refreshed by the helper: {c.reg()}')
    check(c.reg().get('DisplayName') == 'Browser', f'DisplayName « Browser » after the update ({c.reg().get("DisplayName")})')
    c.db_registered_ok('existing install, after an engine update (helper)')
    return c


def case_existing_install_gets_name_and_registration():
    """An install made before this change: « browser.lnk » shortcuts, « browser » in Apps, no default-browser
    registration. The next setup (update) renames the shortcuts in place, names the entry « Browser » and registers."""
    c = Case('legacy_name')
    c.setup(c.copy_setup('0.3.2', P['0.3.2']), '--no-launch', '--desktop')
    for lnk in (c.programs / 'Browser.lnk', c.desktop / 'Browser.lnk'):
        os.rename(lnk, lnk.with_name('browser.lnk'))           # what the setups before « Browser » left
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, c.key, 0, winreg.KEY_SET_VALUE) as k:
        winreg.SetValueEx(k, 'DisplayName', 0, winreg.REG_SZ, 'browser')
    delete_tree(rf'Software\browser-setup-tests\{c.name}\DefaultBrowser')
    check(c.on_disk_name(c.programs / 'browser.lnk') == 'browser.lnk' and c.db_absent(), 'legacy state prepared')
    target_before = c.shortcut_target(c.programs / 'browser.lnk')
    c.setup(c.copy_setup('0.3.3', P['0.3.3']), '--no-launch', '--desktop', label='update 0.3.2 -> 0.3.3 over a legacy install')
    for folder in (c.programs, c.desktop):
        names = [n for n in os.listdir(folder) if n.lower() == 'browser.lnk']
        check(names == ['Browser.lnk'], f'{folder.name}: one shortcut, renamed « Browser.lnk » ({names})')
    check(c.shortcut_target(c.programs / 'Browser.lnk') == target_before, 'same target (the stable launcher): what was pinned keeps working')
    check(c.reg().get('DisplayName') == 'Browser', 'entry renamed « Browser »')
    c.db_registered_ok('legacy install after update')
    return c


def read_repair_nonce(c: Case) -> str:
    rec = c.root / 'updates/repair-transaction'
    return next((l.split(' ', 1)[1] for l in rec.read_text().splitlines() if l.startswith('nonce ')), '') if rec.exists() else ''


def case_repair_rollback():
    """Review P2 #1: a failed or interrupted repair must give back the ORIGINAL same-version files."""
    c = Case('repair_rollback')
    c.setup(c.copy_setup('0.3.3', P['0.3.3']), '--no-launch')
    original = c.root / 'versions/0.3.3/assets/original-canary.txt'

    def plant():
        original.write_text('original copy')

    def original_back(label):
        check(original.exists() and original.read_text() == 'original copy', f'{label}: original same-version copy restored')
        check(not list((c.root / 'versions').glob('.repair*')), f'{label}: no repair leftovers')
        check(not (c.root / 'updates/repair-transaction').exists(), f'{label}: transaction closed')
        check((c.root / 'current').read_text().strip() == '0.3.3', f'{label}: current still 0.3.3')
    s = c.copy_setup('0.3.3', P['0.3.3'])
    # (bridge faults are no-ops in a same-version repair: the published bridge is already byte-identical)
    for hf in ('current-write',):
        plant()
        c.setup(s, '--no-launch', helper_fault=hf, expect=E['helper'], label=f'repair + helper fault {hf}')
        original_back(f'repair fault {hf}')
    plant()
    c.setup(assemble('0.3.3', P['0.3.3-crash'], c.t / 'crash-repair.exe'), expect=E['start'], label='repair, new copy exits without ack')
    original_back('repair no-ack')
    # Helper killed between the aside rename and the move: `current` names a missing directory until recovery.
    plant()
    c.setup(s, '--no-launch', helper_fault='kill-after-repair-aside', expect=E['helper'], label='helper killed after repair aside')
    check(not (c.root / 'versions/0.3.3').exists() and (c.root / 'updates/repair-transaction').exists(), 'kill gap: record kept, target missing')
    subprocess.run([str(c.root / 'browser.exe'), '--echo', 'after-kill'], env=c.env, timeout=60)  # stable launcher start
    original_back('launcher recovery after kill gap')
    for _ in range(50):
        if any(l.startswith('echo 0.3.3 [after-kill]') for l in c.launches()):
            break
        time.sleep(0.1)
    check(any(l.startswith('echo 0.3.3 [after-kill]') for l in c.launches()), 'launcher then started the restored copy')
    # Killed after the move (both copies present, no ack): recovery at the next setup rolls back, then repairs.
    plant()
    c.setup(s, '--no-launch', helper_fault='kill-after-repair-move', expect=E['helper'], label='helper killed after repair move')
    check(not original.exists() and (c.root / 'updates/repair-transaction').exists(), 'killed after move: replacement in place, record kept')
    # The next setup recovers under its lock before anything else (here cancelled right after: recovery only).
    c.setup(s, '--no-launch', '--test-cancel', expect=E['cancelled'], label='next setup: recovery under the lock, then cancel')
    original_back('setup recovery after kill post-move')
    # Same state, but the repaired version acknowledged its start meanwhile: recovery commits instead.
    plant()
    c.setup(s, '--no-launch', helper_fault='kill-after-repair-move', expect=E['helper'], label='helper killed after repair move (2)')
    nonce = read_repair_nonce(c)
    (c.root / f'updates/ack-{nonce}').write_text('browser-ack 1\nversion 0.3.3\npid 1\n', newline='\n')
    subprocess.run([str(c.root / 'browser.exe'), '--echo', 'after-ack'], env=c.env, timeout=60)
    check(not original.exists() and (c.root / 'versions/0.3.3/browser.exe').exists(), 'acknowledged repair committed (fresh copy kept)')
    check(not list((c.root / 'versions').glob('.repair*')) and not (c.root / 'updates/repair-transaction').exists(), 'commit closed the transaction')
    # Successful repair still commits (original deleted) and is intact.
    plant()
    c.setup(s, '--no-launch', label='successful repair')
    installed_ok(c, '0.3.3', OUT / 'payload-0.3.3', P['0.3.3'])
    check(not original.exists() and not list((c.root / 'versions').glob('.repair*')), 'successful repair committed')
    return c


def case_ancestor_junction():
    """Review P2 #2: a junction ABOVE the root (here: LOCALAPPDATA itself) is never followed by setup or uninstall."""
    c = Case('ancestor_junction')
    target = c.t / 'ancestor-target'
    real = Case.__new__(Case)
    c.setup(c.copy_setup('0.3.2', P['0.3.2']), '--no-launch')  # a real install in <T>/LocalAppData/browser
    data_canaries(c.root)
    shutil.copytree(c.t / 'LocalAppData', target)  # plain copy of that install + data, reached only through a link
    canary = target / 'browser/data/profile/EBWebView/Default/Cookies'
    before = tree(target)
    link = c.t / 'ancestor-link'
    junction(link, target)
    linked = dict(c.env, LOCALAPPDATA=str(link))
    r = subprocess.run([str(link / 'browser/uninstall.exe'), '--unattended', '--remove-data'], env=linked, timeout=60)
    for _ in range(300):
        if (c.t / 'uninstall-result.txt').exists():
            break
        time.sleep(0.2)
    code = int((c.t / 'uninstall-result.txt').read_text()) if (c.t / 'uninstall-result.txt').exists() else None
    check(r.returncode == 0 and code == E['unsafe'], f'uninstall through ancestor junction refused (exit {code})')
    check(canary.exists() and tree(target) == before, 'junction target untouched (data canary kept)')
    s = c.copy_setup('0.3.3', P['0.3.3'])
    r = subprocess.run([str(s), '--unattended', '--no-launch'], env=linked, timeout=120)
    check(r.returncode == E['unsafe'], f'setup through ancestor junction refused (exit {r.returncode})')
    check(tree(target) == before, 'setup wrote nothing through the junction')
    os.rmdir(link)
    del real
    return c


def case_serialization():
    """Review P2 #3: one transaction per root across setup, uninstall and helper (also when the setup dies)."""
    c = Case('serialization')
    c.setup(c.copy_setup('0.3.2', P['0.3.2']), '--no-launch')
    canaries = data_canaries(c.root)

    def program():
        return {k: v for k, v in tree(c.root).items() if not k.startswith(('updates/', 'data/', 'fake-launches'))}
    # 1. setup paused after staging (lock held) -> uninstall is Busy and changes nothing; the setup then completes.
    s3 = c.copy_setup('0.3.3', P['0.3.3'])
    p = c.popen_setup(s3, '--no-launch', fault='pause-after-stage')
    time.sleep(2.0)
    before = program()
    c.uninstall(expect=E['busy'], label='uninstall during a staged setup')
    check(program() == before, 'busy uninstall changed nothing')
    p.wait(120)
    check(p.returncode == 0, f'paused setup completed (exit {p.returncode})')
    check((c.root / 'current').read_text().strip() == '0.3.3', 'setup result intact')
    # 2. helper paused right after the repair aside (lock held by the helper) -> uninstall Busy, repair completes.
    p = c.popen_setup(s3, '--no-launch', helper_fault='pause-after-repair-aside')
    for _ in range(100):
        if list((c.root / 'versions').glob('.repair-*')):
            break
        time.sleep(0.1)
    check(bool(list((c.root / 'versions').glob('.repair-*'))), 'repair aside reached')
    c.uninstall(expect=E['busy'], label='uninstall during a repair')
    p.wait(120)
    check(p.returncode == 0, f'repair completed (exit {p.returncode})')
    installed_ok(c, '0.3.3', OUT / 'payload-0.3.3', P['0.3.3'])
    # 3. helper in its ack wait and the setup killed: the helper still holds the inherited lock -> Busy.
    helper_log = c.root / 'updates/helper.log'
    seen = len(helper_log.read_text()) if helper_log.exists() else 0
    p = c.popen_setup(s3, helper_fault='pause-in-ack-wait')
    for _ in range(300):
        if 'new version launched pid' in (helper_log.read_text()[seen:] if helper_log.exists() else ''):
            break
        time.sleep(0.1)
    p.kill()
    p.wait()
    c.uninstall(expect=E['busy'], label='uninstall while the orphaned helper waits for the ack')
    for _ in range(200):  # the helper finishes on its own
        if 'reason ack' in (c.root / 'updates/last-result').read_text():
            break
        time.sleep(0.2)
    check('status installed' in (c.root / 'updates/last-result').read_text(), 'orphaned helper finished the repair')
    time.sleep(1)
    # 4. two uninstallers: the second is Busy while the first (paused, lock held) runs; the first succeeds.
    c.uninstall(fault='pause-uninstall', wait=False, label='first uninstaller (paused)')
    time.sleep(1.5)
    first = c.t / 'uninstall-result.txt'
    first_code = None
    # the second uninstaller writes the same result file: wait for the second, keep reading until the first ends
    c.uninstall(expect=E['busy'], label='competing uninstaller')
    for _ in range(300):
        if first.exists() and first.read_text() == '0':
            first_code = 0
            break
        time.sleep(0.2)
    check(first_code == 0, 'first uninstaller completed')
    check(not (c.root / 'versions').exists(), 'uninstalled once')
    check(all(tree(c.root / 'data').get(k) == v for k, v in canaries.items()), 'data kept')
    # 5. engine-started helper while a setup holds the lock: clean failure, nothing switched.
    c.setup(c.copy_setup('0.3.2', P['0.3.2']), '--no-launch', label='reinstall 0.3.2')
    feed = c.t / 'feed'
    feed.mkdir(exist_ok=True)
    shutil.copy(P['0.3.3'], feed / P['0.3.3'].name)
    shutil.copy(P['0.3.3'].parent / 'update-manifest-win-x64.txt', feed / 'update-manifest-win-x64.txt')
    h = hold_exclusive_create(c.root / 'install.lock')
    try:
        env = dict(c.env, BROWSER_HELPER_LOCK_WAIT_MS='1500')
        subprocess.run([str(c.root / 'versions/0.3.2/browser.exe'), '--update', str(c.root), str(feed)], env=env, timeout=120)
        for _ in range(100):
            if 'install-busy' in (c.root / 'updates/last-result').read_text():
                break
            time.sleep(0.2)
    finally:
        kernel32.CloseHandle(h)
    check('install-busy' in (c.root / 'updates/last-result').read_text(), 'engine helper refused while the lock is held')
    check((c.root / 'current').read_text().strip() == '0.3.2', 'engine install did not switch')
    return c


CASES = [case_fresh_no_launch, case_fresh_launch_ack, case_update_keeps_data_and_repair, case_foreign_shortcuts,
         case_running_browser, case_damaged_setups, case_reparse_roots, case_faults_rollback, case_first_install_rollback,
         case_uninstall, case_uninstall_data_junction, case_helper_refreshes_display_version, case_existing_install_gets_name_and_registration,
         case_repair_rollback, case_ancestor_junction, case_serialization]


def main() -> int:
    a = argparse.ArgumentParser()
    a.add_argument('--only')
    a.add_argument('--keep', action='store_true')
    a.add_argument('--no-build', action='store_true')
    a.add_argument('--src-rev', help='compile src/ and tests/update-core/ from this git revision (red runs)')
    args = a.parse_args()
    if args.src_rev:
        base = OUT / f'src-{args.src_rev}'
        shutil.rmtree(base, ignore_errors=True)
        base.mkdir(parents=True)
        archive = subprocess.run(['git', 'archive', '--format=tar', args.src_rev, 'src', 'tests/update-core'], cwd=ROOT,
                                 capture_output=True, check=True).stdout
        subprocess.run(['tar', '-xf', '-', '-C', str(base)], input=archive, check=True)
        SRC_BASE[0] = base
    if not args.no_build:
        build()
    for v in VERSIONS:
        P[v] = pack(v)
        P[f'{v}-crash'] = pack(v, crash=True)
    only = set(args.only.split(',')) if args.only else None
    for fn in CASES:
        name = fn.__name__[5:]
        if only and name not in only:
            continue
        print(f'== {name}', flush=True)
        before = len(failures)
        c = None
        try:
            c = fn()
        except Exception:
            failures.append(f'{name}: exception')
            traceback.print_exc()
        finally:
            for leftover in (TMP / name, ):
                pass
            if c:
                c.cleanup_registry()
        print(f'   {"ok" if len(failures) == before else "FAILED"}', flush=True)
    print(f'setup suite: {checks} checks, {len(failures)} failures')
    for f in failures:
        print('  -', f)
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
