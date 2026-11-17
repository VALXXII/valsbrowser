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
     main.cpp compiled WITH the update modules), mocked end-to-end temp install,
     Windows setup suite (tests/setup/run_tests.py, temp roots and test keys only);
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
    # Interface language (design/browser-languages-plan.md): catalog, formats, plurals, the browser-wide
    # preference (temporary folders only), then Reglages > Langue on the real drawer (FR and EN processes).
    run('i18n coverage gate (catalog parity, no untranslated interface text)', [sys.executable, ROOT / 'tools/i18n/check_strings.py'])
    fixture('i18n_test', [*STRICT, 'tests/i18n_test.cpp'])
    # The ad blocker installs from a folder updates never move (src/adblock-folder.h; e2e: tests/e2e/adblock_update_e2e.py).
    fixture('adblock_folder_test', [*STRICT, 'tests/adblock_folder_test.cpp'])
    # One taskbar identity for every installed version (src/taskbar-identity.h; e2e: tests/e2e/taskbar_identity_e2e.py).
    fixture('taskbar_identity_test', [*STRICT, 'tests/taskbar_identity_test.cpp', '-lole32', '-luuid', '-lshell32', '-lshlwapi'])
    language_evidence = ROOT / 'build/browser-languages/captures/settings'
    language_evidence.mkdir(parents=True, exist_ok=True)
    fixture('language_settings_test', [*LOOSE, '-Werror', '-municode', *TVG, WV2, 'tests/language_settings_test.cpp', 'src/native-lottie.cpp', THORVG,
            *UI_LIBS, '-loleaut32', '-lshell32', '-lmsimg32', '-lbcrypt'], [ROOT, language_evidence])
    fixture('bookmarks_test', [*STRICT, 'tests/bookmarks_test.cpp'])
    # Import from a browser (design/browser-data-import-plan.md): synthetic roots only, Windows' winsqlite3.dll
    import_work = OUT / 'browser-import-work'
    import_work.mkdir(parents=True, exist_ok=True)
    fixture('browser_import_test', [*STRICT, '-Isrc', 'tests/browser-import/browser_import_test.cpp', '-lshell32', '-lole32', '-ladvapi32'], [import_work])
    # Settings wiring of the import row and the password manager row (main.cpp hooks + real drawer, synthetic hooks)
    fixture('import_settings_test', [*LOOSE, '-Werror', '-municode', *TVG, WV2, 'tests/browser-import/import_settings_test.cpp', 'src/native-lottie.cpp', THORVG,
            *UI_LIBS, '-loleaut32', '-lshell32', '-lmsimg32', '-lbcrypt'], [ROOT])
    # Import dialog states rendered offscreen by the production modal (native fixture), each PNG checked non-blank
    import_render = OUT / 'import-render'
    import_render.mkdir(parents=True, exist_ok=True)
    fixture('import_dialog_render_test', [*LOOSE, '-Werror', '-municode', *TVG, WV2, 'tests/browser-import/import_dialog_render_test.cpp', 'src/native-lottie.cpp', THORVG,
            *UI_LIBS, '-loleaut32', '-lshell32', '-lmsimg32', '-lbcrypt', '-ladvapi32'], [ROOT, import_render])
    bookmarks_evidence = ROOT / 'build/bookmarks-ui-qa'
    bookmarks_evidence.mkdir(parents=True, exist_ok=True)
    fixture('bookmarks_ui_test', [*LOOSE, '-municode', *TVG, WV2,
            'tests/bookmarks/bookmarks_ui_test.cpp', 'src/native-lottie.cpp', THORVG,
            *UI_LIBS, '-loleaut32', '-lshell32'], [ROOT, bookmarks_evidence])
    fixture('geometry_test', [*STRICT, 'tests/design-system/geometry_test.cpp'])
    fixture('motion_test', [*STRICT, '-pedantic', 'tests/motion/motion_test.cpp', '-luser32'])
    fixture('polish_test', [*STRICT, 'tests/motion/polish_test.cpp', '-luser32'])
    fixture('address_background_test', [*STRICT, 'tests/address_background_test.cpp', '-lgdi32', '-luser32', '-ldwmapi'])
    # Address selection in the profile colour: real EDIT pixels, keyboard/mouse/edit, live switch, 96/144/192 DPI
    fixture('address_selection_test', [*STRICT, 'tests/address_selection_test.cpp', '-lgdi32', '-luser32', '-ldwmapi', '-lcomctl32'])
    fixture('shortcuts_test', ['-O0', *TVG, WV2, 'tests/shortcuts_test.cpp', 'src/native-lottie.cpp', THORVG, *UI_LIBS])
    fixture('navigation_errors_test', [*LOOSE, *TVG, WV2, 'tests/navigation_errors_test.cpp', 'src/native-lottie.cpp', THORVG, *UI_LIBS])
    fixture('tab_drag_test', [*LOOSE, *TVG, WV2, 'tests/ui/tab_drag_test.cpp', 'src/native-lottie.cpp', THORVG, *UI_LIBS])
    # Split view: pure model (zones, capacity, rails, springs) and host behaviour through WndProc
    fixture('split_view_test', [*STRICT, 'tests/split-view/split_view_test.cpp'])
    fixture('site_name_test', [*STRICT, 'tests/split-view/site_name_test.cpp'])
    fixture('split_ui_test', [*LOOSE, *TVG, WV2, 'tests/ui/split_ui_test.cpp', 'src/native-lottie.cpp', THORVG, *UI_LIBS])
    # Screen-reader view of the chrome through a real UI Automation client (issue #2)
    # Native menus: a submenu's row hover follows the pointer mid-entrance (cascade, keyboard, reduced motion)
    fixture('context_menu_hover_test', [*LOOSE, '-municode', *TVG, 'tests/ui/context_menu_hover_test.cpp', 'src/native-lottie.cpp', THORVG, *UI_LIBS, '-lshell32', '-loleaut32'], [ROOT])
    # Suggestions popup and Favoris drawer redraw at the DPI of the monitor they land on (SKIP with a single DPI, as on CI).
    fixture('address_popup_dpi_test', [*LOOSE, '-Werror', '-municode', *TVG, WV2, 'tests/ui/address_popup_dpi_test.cpp', 'src/native-lottie.cpp', THORVG, *UI_LIBS, '-lshell32', '-loleaut32', '-lmsimg32', '-lshcore'], [ROOT])
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
    webview2 = ROOT / 'tests/webview2-runtime/run_tests.py'   # startup recovery (fakes only, read-only loader probe)
    for label, script in (('update-core tests', core), ('update-toast tests', toast), ('webview2 startup recovery tests', webview2)):
        if not script.exists():
            print(f'FAILED: {label}: runner {script.relative_to(ROOT)} missing')
            sys.exit(3)
        run(label, [sys.executable, script])


def integration() -> None:
    run('release tooling tests', [sys.executable, '-I', HERE / 'test_release_tools.py'])
    # v0.3.239's own package validator (from git history) accepts the assets/bin bridge, refuses a root one
    run('old client validator accepts the release bridge payload', [sys.executable, HERE / 'old_validator.py'])
    run('host integration + mocked end-to-end install', [sys.executable, HERE / 'e2e_install.py'])
    # Windows setup (design/windows-setup-plan.md): real test-build stub + real fault-build helper around synthetic
    # payloads; LOCALAPPDATA, Start Menu, Desktop, Temp and the uninstall key redirected to build/setup-tests/tmp and
    # HKCU/Software/browser-setup-tests (deleted per case). Never the user's install, shortcuts or Apps entry.
    run('windows setup suite (isolated roots and keys)', [sys.executable, '-I', ROOT / 'tests/setup/run_tests.py'])


def ico_png(path: Path) -> bytes:
    """The 256 px PNG entry of an ICO (unique per variant; windres embeds it verbatim)."""
    data = path.read_bytes()
    for i in range(struct.unpack_from('<H', data, 4)[0]):
        size, offset = struct.unpack_from('<II', data, 6 + 16 * i + 8)
        if data[offset:offset + 8] == b'\x89PNG\r\n\x1a\n':
            return data[offset:offset + size]
    sys.exit(f'{path} has no PNG entry')


def version_strings(exe: Path) -> dict:
    """The StringFileInfo values Windows itself reads (GetFileVersionInfoW), first translation."""
    import ctypes
    from ctypes import wintypes as W
    v = ctypes.windll.version
    v.GetFileVersionInfoSizeW.argtypes = [W.LPCWSTR, ctypes.POINTER(W.DWORD)]
    v.GetFileVersionInfoW.argtypes = [W.LPCWSTR, W.DWORD, W.DWORD, ctypes.c_void_p]
    v.VerQueryValueW.argtypes = [ctypes.c_void_p, W.LPCWSTR, ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(W.UINT)]
    size = v.GetFileVersionInfoSizeW(str(exe), None)
    if not size:
        return {}
    buf = ctypes.create_string_buffer(size)
    if not v.GetFileVersionInfoW(str(exe), 0, size, buf):
        return {}
    ptr, n = ctypes.c_void_p(), W.UINT()
    if not v.VerQueryValueW(buf, r'\VarFileInfo\Translation', ctypes.byref(ptr), ctypes.byref(n)) or n.value < 4:
        return {}
    lang, cp = struct.unpack('<HH', ctypes.string_at(ptr.value, 4))
    out = {}
    for key in ('CompanyName', 'FileDescription', 'ProductName', 'LegalCopyright', 'OriginalFilename',
                'FileVersion', 'ProductVersion', 'InternalName'):
        if v.VerQueryValueW(buf, rf'\StringFileInfo\{lang:04x}{cp:04x}\{key}', ctypes.byref(ptr), ctypes.byref(n)) and n.value:
            out[key] = ctypes.wstring_at(ptr.value, n.value).rstrip('\0')
    return out


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
    # Publisher metadata (src/version-info.rc): every executable we ship names its publisher, product,
    # description and copyright, with the stamped version. Unsigned files with empty metadata are what
    # antivirus heuristics and SmartScreen distrust most; these strings are also what Windows shows.
    ours = [app / 'browser.exe', app / 'update-helper.exe']
    ours += [p for p in (app / 'browser-annotations-mcp.exe', app / 'assets/bin/browser-annotations-mcp.exe') if p.is_file()]
    # Setup stub (tools/release/build.py: <app>-setup/browser-setup.exe, not part of the payload): required for a
    # release app, since package.py turns it into the published browser-<v>-win-x64-setup.exe.
    setup_stub = app.parent / (app.name + '-setup') / 'browser-setup.exe'
    if info['channel'] == 'release' and not setup_stub.is_file():
        sys.exit(f'release app has no setup stub at {setup_stub}')
    if setup_stub.is_file():
        ours.append(setup_stub)
    for exe in ours:
        strings = version_strings(exe)
        for key in ('CompanyName', 'FileDescription', 'ProductName', 'LegalCopyright', 'OriginalFilename'):
            if not strings.get(key):
                sys.exit(f'{exe.name}: version resource has no {key} (got {strings})')
        if strings.get('FileVersion') != info['version'] or strings.get('ProductVersion') != info['version']:
            sys.exit(f'{exe.name}: FileVersion/ProductVersion {strings.get("FileVersion")}/{strings.get("ProductVersion")} != {info["version"]}')
        if strings['OriginalFilename'].lower() != exe.name.lower():
            sys.exit(f'{exe.name}: OriginalFilename is {strings["OriginalFilename"]}')
    # App icon policy (tools/release/version.py): release -> live, dev -> preview. Both executables
    # embed exactly that ICO's 256 px image, never the other one, and assets/browser.ico is that ICO.
    sys.path.insert(0, str(ROOT / 'tools/release'))
    import version as V
    want = V.app_icon(info['channel'])
    other = V.app_icon('dev' if info['channel'] == 'release' else 'release')
    if (app / 'assets/browser.ico').read_bytes() != want.read_bytes():
        sys.exit(f'assets/browser.ico is not {want.name}')
    for exe in [app / 'browser.exe', app / 'update-helper.exe', *([setup_stub] if setup_stub.is_file() else [])]:
        data = exe.read_bytes()
        if ico_png(want) not in data or ico_png(other) in data:
            sys.exit(f'{exe.name} does not embed {want.name} (channel {info["channel"]})')
    # Annotations MCP bridge (tools/release/build.py bridge_location): a release ships it at exactly
    # assets/bin/ (the only place existing clients accept), never at the root, and ships it iff the
    # annotation mode is compiled into browser.exe (no UI without bridge, no orphan bridge).
    import package as K
    bridge = app / K.RELEASE_BRIDGE
    if info['channel'] == 'release':
        if (app / 'browser-annotations-mcp.exe').exists():
            sys.exit('release app has a root-level bridge (existing clients would reject the update)')
        annotations = 'ANNOTATE '.encode('utf-16-le') in (app / 'browser.exe').read_bytes()
        if bridge.is_file() != annotations:
            sys.exit(f'release app: bridge present={bridge.is_file()} but annotation mode compiled in={annotations}')
    if bridge.is_file():
        K.check_console_x64(bridge)
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
