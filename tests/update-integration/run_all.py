"""Native-only regression + update suite (local and CI).

Never launches browser.exe, never loads the WebView2 runtime, never touches a real
profile, session or %LOCALAPPDATA%\\browser. Every step must exit 0; the first
failure stops the run with a nonzero exit code.

  python tests/update-integration/run_all.py [--app build/update-integration] [--ci]

Steps:
  1. existing native fixtures (same compile lines as test-*.ps1 / test-lottie.py,
     invoked directly so a Restricted PowerShell policy is irrelevant);
  2. update-core peer tests, update-toast peer tests (their own runners);
  3. integration: release tooling tests, host integration fixture (production
     main.cpp compiled WITH the update modules), mocked end-to-end temp install;
  4. --app: static checks of the built application directory.
"""
from __future__ import annotations
import argparse, json, struct, subprocess, sys, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
BIN = ROOT / 'tools/w64devkit/bin'
GXX = str(BIN / 'g++.exe')
OUT = ROOT / 'build/update-integration-tests'
THORVG = 'build/lottie-backend/src/libthorvg-1.a'
WV2 = '-Ideps/webview2/build/native/include'
TVG = ['-DTVG_STATIC', '-Ideps/thorvg-1.1.3/inc']
UI_LIBS = ['-lole32', '-luuid', '-lcomctl32', '-lshlwapi', '-lgdi32', '-ld2d1', '-ldwrite',
           '-lwindowscodecs', '-ldwmapi']
STRICT = ['-Wall', '-Wextra', '-Werror', '-Wno-missing-field-initializers']
LOOSE = ['-O2', '-Wall', '-Wextra', '-Wno-unknown-pragmas', '-Wno-cast-function-type',
         '-Wno-misleading-indentation', '-Wno-missing-field-initializers']

results: list[tuple[str, float]] = []


def run(label: str, cmd: list, **kw) -> None:
    start = time.time()
    print(f'== {label}', flush=True)
    proc = subprocess.run([str(c) for c in cmd], cwd=kw.pop('cwd', ROOT), **kw)
    if proc.returncode != 0:
        print(f'FAILED ({proc.returncode}): {label}', flush=True)
        sys.exit(proc.returncode or 1)
    results.append((label, time.time() - start))


def fixture(name: str, compile_args: list, run_args: list | None = None) -> None:
    exe = OUT / f'{name}.exe'
    run(f'compile {name}', [GXX, '-std=c++17', '-static', *compile_args, '-o', exe])
    run(f'run {name}', [exe, *(run_args or [])])


def existing_fixtures() -> None:
    ev = OUT / 'evidence'
    ev.mkdir(parents=True, exist_ok=True)
    # test-lottie.py: loading, frame clock, lottie, clipboard gate, design system, hover, ui
    run('test-lottie.py (native lottie/ui fixtures)', [sys.executable, ROOT / 'test-lottie.py'])
    fixture('session_test', [*STRICT, 'tests/session_test.cpp'])
    fixture('bookmarks_test', [*STRICT, 'tests/bookmarks_test.cpp'])
    bookmarks_evidence = ROOT / 'build/bookmarks-ui-qa'
    bookmarks_evidence.mkdir(parents=True, exist_ok=True)
    fixture('bookmarks_ui_test', [*LOOSE, '-municode', *TVG, WV2,
            'tests/bookmarks/bookmarks_ui_test.cpp', 'src/native-lottie.cpp', THORVG,
            *UI_LIBS, '-loleaut32', '-lshell32'], [ROOT, bookmarks_evidence])
    fixture('geometry_test', [*STRICT, 'tests/design-system/geometry_test.cpp'])
    fixture('motion_test', [*STRICT, '-pedantic', 'tests/motion/motion_test.cpp', '-luser32'])
    fixture('polish_test', [*STRICT, 'tests/motion/polish_test.cpp', '-luser32'])
    fixture('address_background_test', [*STRICT, 'tests/address_background_test.cpp', '-lgdi32', '-luser32', '-ldwmapi'])
    fixture('shortcuts_test', ['-O0', *TVG, WV2, 'tests/shortcuts_test.cpp', 'src/native-lottie.cpp', THORVG, *UI_LIBS])
    fixture('navigation_errors_test', [*LOOSE, *TVG, WV2, 'tests/navigation_errors_test.cpp', 'src/native-lottie.cpp', THORVG, *UI_LIBS])
    fixture('tab_drag_test', [*LOOSE, *TVG, WV2, 'tests/ui/tab_drag_test.cpp', 'src/native-lottie.cpp', THORVG, *UI_LIBS])
    # Split view: pure model (zones, capacity, rails, springs) and host behaviour through WndProc
    fixture('split_view_test', [*STRICT, 'tests/split-view/split_view_test.cpp'])
    fixture('site_name_test', [*STRICT, 'tests/split-view/site_name_test.cpp'])
    fixture('split_ui_test', [*LOOSE, *TVG, WV2, 'tests/ui/split_ui_test.cpp', 'src/native-lottie.cpp', THORVG, *UI_LIBS])
    # Screen-reader view of the chrome through a real UI Automation client (issue #2)
    fixture('accessibility_test', [*LOOSE, *TVG, WV2, 'tests/accessibility/accessibility_test.cpp', 'src/native-lottie.cpp', THORVG, *UI_LIBS, '-loleaut32'])
    # Download history popover/drawer (upstream PR #28/#29, same line as test-downloads.ps1)
    downloads_evidence = ROOT / 'build/downloads-qa'
    downloads_evidence.mkdir(parents=True, exist_ok=True)
    fixture('download_panel_test', [*STRICT, '-municode', *TVG, 'tests/downloads/download_panel_test.cpp', 'src/native-lottie.cpp', THORVG,
            '-ld2d1', '-ldwrite', '-lwindowscodecs', '-lole32', '-luuid', '-lcomctl32', '-lgdi32', '-ldwmapi', '-lshell32'],
            [ROOT, downloads_evidence])
    # Browsing history drawer opened by the toolbar button (same line as test-history.ps1)
    history_evidence = ROOT / 'build/history-qa'
    history_evidence.mkdir(parents=True, exist_ok=True)
    fixture('history_panel_test', [*STRICT, '-municode', *TVG, 'tests/history/history_panel_test.cpp', 'src/native-lottie.cpp', THORVG,
            '-ld2d1', '-ldwrite', '-lwindowscodecs', '-lole32', '-luuid', '-lcomctl32', '-lgdi32', '-ldwmapi', '-lshell32'],
            [ROOT, history_evidence])


def peer_suites() -> None:
    core = ROOT / 'tests/update-core/run_tests.py'
    toast = ROOT / 'tests/update-toast/run.py'
    for label, script in (('update-core tests', core), ('update-toast tests', toast)):
        if not script.exists():
            print(f'FAILED: {label}: runner {script.relative_to(ROOT)} missing')
            sys.exit(3)
        run(label, [sys.executable, script])


def integration() -> None:
    run('release tooling tests', [sys.executable, '-I', HERE / 'test_release_tools.py'])
    run('host integration + mocked end-to-end install', [sys.executable, HERE / 'e2e_install.py'])


def check_app(app: Path) -> None:
    print(f'== static checks {app}')
    info = json.loads((app / 'version.json').read_text(encoding='utf-8'))
    for exe in ('browser.exe', 'update-helper.exe'):
        data = (app / exe).read_bytes()
        pe = struct.unpack_from('<I', data, 0x3C)[0]
        machine = struct.unpack_from('<H', data, pe + 4)[0]
        if data[:2] != b'MZ' or data[pe:pe + 4] != b'PE\0\0' or machine != 0x8664:
            sys.exit(f'{exe} is not a Windows x64 PE')
        # VS_FIXEDFILEINFO (browser.exe): file version must equal the stamped identity
        if exe == 'browser.exe':
            sig = data.find(struct.pack('<I', 0xFEEF04BD))
            if sig < 0:
                sys.exit('browser.exe has no version resource')
            ms, ls = struct.unpack_from('<II', data, sig + 8)
            got = f'{ms >> 16}.{ms & 0xFFFF}.{ls >> 16}'
            if got != info['version']:
                sys.exit(f'version resource {got} != version.json {info["version"]}')
            if info['version'].encode('utf-16-le') not in data:
                sys.exit('BROWSER_UPDATE_VERSION string not embedded in browser.exe')
            if info['updateRepository'].encode('utf-16-le') not in data:
                sys.exit('update repository not embedded in browser.exe')
    for required in ('WebView2Loader.dll', 'assets/fonts/InterDisplay-Regular.ttf', 'WebView2-LICENSE.txt'):
        if not (app / required).is_file():
            sys.exit(f'missing {required}')
    results.append((f'static checks {info["version"]}', 0.0))


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--app', help='built application directory to check statically')
    p.add_argument('--ci', action='store_true')
    p.add_argument('--only', choices=('existing', 'peers', 'integration', 'app'))
    a = p.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    steps = {'existing': existing_fixtures, 'peers': peer_suites, 'integration': integration}
    for key, step in steps.items():
        if a.only in (None, key):
            step()
    if a.app and a.only in (None, 'app'):
        check_app((ROOT / a.app).resolve())
    print('\nALL PASSED')
    for label, seconds in results:
        print(f'  ok  {seconds:6.1f}s  {label}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
