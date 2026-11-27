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

Two phases, never together. Everything the selected steps compile is planned first: the fixtures of step 1 and the
compiles of the scripts that hand theirs over (compile_plan.py: test-lottie.py, the drawer side, the update-core,
update-toast and webview2 suites, the mocked end-to-end install), each with its own command line (the same flags as
before, one g++ per output). Phase A compiles them all, up to --jobs at a time (default: one per core) within the free
memory (a src/main.cpp compile needs about 3.5 GB: one at a time on an 8 GB runner, light ones beside it), nothing else
running; each compile's output is printed whole when it ends; the first failure stops it and the run. Phase B then runs
every step in the order below, one at a time, as before, with nothing compiling or running beside it (a timing test
never shares the processor): the fixtures' runs and the scripts (with --prebuilt). --jobs 1 compiles one at a time;
--runs-only runs phase B alone from an earlier run's outputs (local measurements only).
"""
from __future__ import annotations
import argparse, json, os, re, shutil, struct, subprocess, sys, threading, time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import compile_plan  # noqa: E402

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
# While steps are planned (planned()), fixture() records its compile for phase A and run() records the step for phase B;
# run(..., now=True) runs at once (a resource object a later compile needs). A planned step: (label, command, kw).
PLAN: dict | None = None
RUNS_ONLY = False   # --runs-only: phase B alone, from an earlier full run's outputs (local measurements)
# Measured on 2026-11-27 (job accounting, peak commit): a compile of src/main.cpp (the test file that includes it, or a
# script's host) peaks at 2.8-4.3 GB; every other compile of the suite at 1.3 GB or less. A hosted windows-2022 runner of a
# private repository has 2 vCPUs and 8 GB: one src/main.cpp compile at a time there, a light one beside it.
HEAVY_GB, LIGHT_GB, RESERVE_GB = 3.5, 1.0, 0.5


def run(label: str, cmd: list, now: bool = False, **kw) -> None:
    if PLAN is not None and not now:
        PLAN['steps'].append((label, cmd, kw))
        return
    execute(label, cmd, **kw)


def execute(label: str, cmd: list, **kw) -> None:
    start = time.time()
    print(f'== {label}', flush=True)
    proc = subprocess.run([str(c) for c in cmd], cwd=kw.pop('cwd', ROOT), **kw)
    if proc.returncode != 0:
        print(f'FAILED ({proc.returncode}): {label}', flush=True)
        sys.exit(proc.returncode or 1)
    results.append((label, time.time() - start))


def fixture(name: str, compile_args: list, run_args: list | None = None) -> None:
    exe = OUT / f'{name}.exe'
    command = [GXX, '-std=c++17', '-static', *compile_args, '-o', exe]
    if PLAN is not None:
        PLAN['compiles'].append((name, command, ROOT))
        PLAN['steps'].append((f'run {name}', [exe, *(run_args or [])], {}))
        return
    run(f'compile {name}', command)
    run(f'run {name}', [exe, *(run_args or [])])


def free_memory_gb() -> float:
    import ctypes
    class Status(ctypes.Structure):
        _fields_ = [('dwLength', ctypes.c_ulong), ('dwMemoryLoad', ctypes.c_ulong), ('ullTotalPhys', ctypes.c_ulonglong),
                    ('ullAvailPhys', ctypes.c_ulonglong), ('ullTotalPageFile', ctypes.c_ulonglong), ('ullAvailPageFile', ctypes.c_ulonglong),
                    ('ullTotalVirtual', ctypes.c_ulonglong), ('ullAvailVirtual', ctypes.c_ulonglong), ('ullAvailExtendedVirtual', ctypes.c_ulonglong)]
    s = Status(); s.dwLength = ctypes.sizeof(s)
    return s.ullAvailPhys / 2**30 if ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(s)) else 0.0


def compiles_main_cpp(command: list, cwd) -> bool:
    """A compile of src/main.cpp: named on the command line, or included by the test file compiled."""
    for arg in (str(a) for a in command):
        if not arg.endswith('.cpp'):
            continue
        if arg.replace('\\', '/').endswith('src/main.cpp'):
            return True
        source = Path(arg) if Path(arg).is_absolute() else Path(cwd) / arg
        try:
            text = source.read_text(encoding='utf-8', errors='replace')
        except OSError:
            continue
        if re.search(r'#\s*include\s*"[^"]*\bmain\.cpp"', text):
            return True
    return False


def compile_all(compiles: list, jobs: int, memory_gb: float = 0.0) -> None:
    """Phase A: every planned compile, `jobs` at a time within the memory (a src/main.cpp compile first whenever one fits:
    they are the long ones), nothing else running. Each compile's output is printed whole when it ends (never interleaved);
    the first failure stops the queue and ends the compiles still running (ours), then the run fails with its output last."""
    items = [{'name': n, 'cmd': [str(c) for c in cmd], 'cwd': cwd, 'heavy': compiles_main_cpp(cmd, cwd)} for n, cmd, cwd in compiles]
    pending, procs, failed = list(items), set(), []
    state = {'free': max(HEAVY_GB, memory_gb or free_memory_gb() - RESERVE_GB), 'running': 0}
    cv, start = threading.Condition(), time.time()
    cost = lambda item: HEAVY_GB if item['heavy'] else LIGHT_GB
    print(f"== phase A: {len(items)} compiles ({sum(1 for i in items if i['heavy'])} of src/main.cpp), up to {jobs} at a time within "
          f"{state['free']:.1f} GB ({os.cpu_count()} cores, {free_memory_gb():.1f} GB free); nothing else runs meanwhile", flush=True)

    def take():
        fits = [i for i in pending if cost(i) <= state['free']]
        item = next((i for i in fits if i['heavy']), None) or (fits[0] if fits else None)
        if item is None and state['running'] == 0 and pending:
            item = pending[0]   # never stuck: alone, a compile always starts
        if item is not None:
            pending.remove(item); state['free'] -= cost(item); state['running'] += 1
        return item

    def worker() -> None:
        while True:
            with cv:
                item = None
                while not failed and pending and (item := take()) is None:
                    cv.wait()
                if item is None:
                    return
            begun = time.time()
            proc = subprocess.Popen(item['cmd'], cwd=item['cwd'], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            with cv:
                procs.add(proc)
            out, _ = proc.communicate()
            took, text = time.time() - begun, out.decode('utf-8', errors='replace')
            with cv:
                procs.discard(proc); state['free'] += cost(item); state['running'] -= 1
                if not failed:
                    print(f"== compile {item['name']} ({took:.1f} s{', src/main.cpp' if item['heavy'] else ''})", flush=True)
                    if text.strip():
                        print(text.rstrip(), flush=True)
                if proc.returncode != 0 and not failed:
                    failed.append((item['name'], proc.returncode, text))
                    for other in list(procs):   # the first failure ends the run: the compiles still running are ours
                        other.kill()
                elif proc.returncode == 0:
                    results.append((f"compile {item['name']}", took))
                cv.notify_all()

    threads = [threading.Thread(target=worker, daemon=True) for _ in range(max(1, jobs))]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    if failed:
        name, code, text = failed[0]
        print(f'\n== the first failure: compile {name}\n{text.rstrip()}\nFAILED ({code}): compile {name}', flush=True)
        sys.exit(code or 1)
    busy = sum(seconds for label, seconds in results if label.startswith('compile '))
    results.append((f'phase A: {len(items)} compiles, {busy / 60:.1f} min of compiling in {(time.time() - start) / 60:.1f} min', 0.0))


def default_jobs() -> int:
    return max(1, os.cpu_count() or 1)


def script(label: str, path: Path, flags: list | None = None) -> None:
    """A test script that compiles before it runs. In a planned step its compiles join phase A (--compile-plan: its own
    preparation runs now, nothing is compiled) and it runs in phase B in its place with --prebuilt; otherwise as it always did."""
    command = [sys.executable, *(flags or []), path]
    if PLAN is None:
        run(label, command)
        return
    if not RUNS_ONLY:
        plan_file = OUT / 'compile-plans' / f'{path.parent.name}-{path.stem}.json'
        execute(f'{label}: its preparation and compile commands', [*command, '--compile-plan', plan_file])
        PLAN['compiles'] += [(f'{label} / {name}', cmd, cwd) for name, cmd, cwd in compile_plan.read(plan_file)]
    PLAN['steps'].append((label, [*command, '--prebuilt'], {}))


def planned(step, jobs: int, memory_gb: float = 0.0) -> None:
    """Steps planned first (their compiles collected, the scripts' preparations done); phase A compiles everything, nothing
    else running; then phase B runs the runs and scripts in their order, one at a time, nothing compiling or running beside."""
    global PLAN
    PLAN = {'compiles': [], 'steps': []}
    try:
        step()
        plan = PLAN
    finally:
        PLAN = None
    if not RUNS_ONLY:
        compile_all(plan['compiles'], jobs, memory_gb)
    start = time.time()
    print(f"== phase B: {len(plan['steps'])} runs and scripts, one at a time", flush=True)
    for label, command, kw in plan['steps']:
        run(label, command, **kw)
    results.append((f"phase B: {len(plan['steps'])} runs and scripts in {(time.time() - start) / 60:.1f} min", 0.0))


def existing_fixtures() -> None:
    ev = OUT / 'evidence'
    ev.mkdir(parents=True, exist_ok=True)
    # test-lottie.py: loading, frame clock, lottie, clipboard gate, design system, hover, ui
    script('test-lottie.py (native lottie/ui fixtures)', ROOT / 'test-lottie.py')
    fixture('session_test', [*STRICT, 'tests/session_test.cpp'])
    # Mode Focus (src/focus-mode.h, design/focus-mode-plan.md): panel geometry, hit box, slide timing, easing, hole spans.
    fixture('focus_mode_test', [*STRICT, 'tests/focus_mode_test.cpp'])
    # Its entry / exit motion (src/focus-motion.h, design/focus-motion-report.md): spring 1.0 / 0.3 s, interruption, fades, glued layout.
    fixture('focus_motion_test', [*STRICT, 'tests/focus_motion_test.cpp'])
    # Its drawers' antialiased top corners (src/drawer-corners.h): coverage, region cut, layered corner windows.
    fixture('drawer_corners_test', [*STRICT, '-municode', 'tests/drawer_corners_test.cpp', '-lgdi32', '-luser32'])
    # #143: the drawers on the tab panel's side (pure geometry at 4 DPI, then src/main.cpp's real drawers, side Focus and the
    # toolbar tooltip muted over its open menu, on a desktop of its own that is never displayed).
    script('drawer side (#143): pure + host on a hidden desktop', ROOT / 'tests/drawer-side/run.py', ['-I'])
    # #143 · Infobulle B of the side column's tabs and favourites (src/side-tip.h): box, place, delay / chaining, motion (pure).
    fixture('side_tip_test', [*STRICT, 'tests/side_tip_test.cpp', '-luser32'])
    # The window's resize border over the chat panel, the drawers and the layers (src/resize-border.h; trial 0.3.952):
    # hidden windows only, never shown. The browser's manifest is linked in (layered child windows need Windows 8+ declared).
    resize_manifest = OUT / 'resize_border_manifest.o'
    run('windres resize_border_test.rc', [BIN / 'windres.exe', 'tests/resize_border_test.rc', '-O', 'coff', '-o', resize_manifest], now=True)
    fixture('resize_border_test', [*STRICT, '-municode', 'tests/resize_border_test.cpp', resize_manifest, '-lgdi32', '-luser32'])
    # Address field model: display, suggestions, and what a typed text opens (links, local paths and file: URLs, searches).
    fixture('address_test', [*STRICT, 'tests/address_test.cpp'])
    fixture('history_index_test', ['-O2', *STRICT, 'tests/history_index_test.cpp'])
    # Interface language (design/browser-languages-plan.md): catalog, formats, plurals, the browser-wide
    # preference (temporary folders only), then Reglages > Langue on the real drawer (FR and EN processes).
    run('i18n coverage gate (catalog parity, no untranslated interface text)', [sys.executable, ROOT / 'tools/i18n/check_strings.py'])
    # No test script may start the built browser without tests/agent-guard (static: no build, no process started).
    run('agent guard: no test script starts browser.exe unguarded (static)', [sys.executable, '-I', ROOT / 'tests/agent-guard/run_tests.py', '--static'])
    fixture('i18n_test', [*STRICT, 'tests/i18n_test.cpp'])
    # The ad blocker installs from a folder updates never move (src/adblock-folder.h; e2e: tests/e2e/adblock_update_e2e.py).
    fixture('adblock_folder_test', [*STRICT, 'tests/adblock_folder_test.cpp'])
    # browser.log: origins only in release builds, capped at 1 MiB, erased with the browsing data (src/log-privacy.h, SEC-05).
    fixture('log_privacy_test', [*STRICT, 'tests/log_privacy_test.cpp'])
    # « Conserver »: what a browser stopped in the middle of a keep leaves in kept-downloads goes back to the user at the next
    # start (src/kept-downloads.h). Windowless, temporary folders under the output folder.
    kept_work = OUT / 'kept-downloads-work'
    kept_work.mkdir(parents=True, exist_ok=True)
    fixture('kept_downloads_test', [*STRICT, '-municode', 'tests/downloads/kept_downloads_test.cpp'], [kept_work])
    # Directory loops that never throw (src/fs-safe.h, SEC-27): an unlistable sub-folder ends the loop, not the process.
    fixture('fs_safe_test', [*STRICT, 'tests/fs_safe_test.cpp', '-ladvapi32'])
    # One taskbar identity for every installed version (src/taskbar-identity.h; e2e: tests/e2e/taskbar_identity_e2e.py).
    fixture('taskbar_identity_test', [*STRICT, 'tests/taskbar_identity_test.cpp', '-lole32', '-luuid', '-lshell32', '-lshlwapi'])
    # Browser as a selectable default browser (src/default-browser.h: registration under a throwaway HKCU test key only,
    # faked default status) and the link / file a launch opens (src/launch-args.h).
    fixture('default_browser_test', [*STRICT, 'tests/default_browser_test.cpp', '-lole32', '-luuid', '-lshell32', '-lshlwapi', '-ladvapi32'])
    default_browser_evidence = ROOT / 'build/default-browser/captures/settings'
    default_browser_evidence.mkdir(parents=True, exist_ok=True)
    fixture('default_browser_settings_test', [*LOOSE, '-Werror', '-municode', *TVG, WV2, 'tests/default_browser_settings_test.cpp', 'src/native-lottie.cpp', THORVG,
            *UI_LIBS, '-loleaut32', '-lshell32', '-lmsimg32', '-lbcrypt'], [ROOT, default_browser_evidence])
    # Réglages › the version line opens « À propos de Browser » (Paper « À propos : point d'entrée » A). Windowless: the
    # drawer is never shown. Its real-window twin (night run only): tests/e2e/about_entry_e2e.py.
    about_evidence = ROOT / 'build/settings-about/captures'
    about_evidence.mkdir(parents=True, exist_ok=True)
    fixture('settings_about_test', [*LOOSE, '-Werror', '-municode', *TVG, WV2, 'tests/settings_about_test.cpp', 'src/native-lottie.cpp', THORVG,
            *UI_LIBS, '-loleaut32', '-lshell32', '-lmsimg32', '-lbcrypt'], [ROOT, about_evidence])
    # Réglages' root footer: the layout switch (one setting with « Position des onglets ») and the « BETA » chip (Val's
    # « Atelier » boards « 01 · Réglages — racine »). Windowless; pictures for tests/atelier-a/compose.py.
    switch_evidence = ROOT / 'build/atelier-a-qa/switch'
    switch_evidence.mkdir(parents=True, exist_ok=True)
    fixture('settings_layout_switch_test', [*LOOSE, '-Werror', '-municode', *TVG, WV2, 'tests/settings_layout_switch_test.cpp', 'src/native-lottie.cpp', THORVG,
            *UI_LIBS, '-loleaut32', '-lshell32', '-lmsimg32', '-lbcrypt'], [ROOT, switch_evidence])
    # Réglages stays open when it changes the layout (Val 2026-11-26): src/main.cpp on a desktop of its own, never displayed.
    keep_evidence = ROOT / 'build/atelier-a-qa/layout-keep'
    keep_evidence.mkdir(parents=True, exist_ok=True)
    fixture('settings_layout_keep_test', [*LOOSE, '-municode', *TVG, WV2, 'tests/atelier-a/settings_layout_keep_test.cpp', 'src/native-lottie.cpp', THORVG,
            *UI_LIBS, '-loleaut32', '-lshell32', '-lmsimg32'], [ROOT, keep_evidence])
    # The « À propos de Browser » B window it opens (src/modal.h Kind::About, src/about-icons.h; Paper 257B-0): layout
    # values, the six update states, tile actions, Tab order, focus styles, FR/EN, profile colours. Screen-free: laid out
    # with ShowHiddenForTest (never shown nor activated), pictures drawn off screen.
    about_window_evidence = ROOT / 'build/about-window/captures'
    about_window_evidence.mkdir(parents=True, exist_ok=True)
    fixture('about_window_test', [*LOOSE, '-Werror', '-municode', *TVG, WV2, 'tests/about_window_test.cpp', 'src/native-lottie.cpp', THORVG,
            *UI_LIBS, '-loleaut32', '-lshell32', '-lmsimg32', '-lbcrypt'], [ROOT, about_window_evidence])
    language_evidence = ROOT / 'build/browser-languages/captures/settings'
    language_evidence.mkdir(parents=True, exist_ok=True)
    fixture('language_settings_test', [*LOOSE, '-Werror', '-municode', *TVG, WV2, 'tests/language_settings_test.cpp', 'src/native-lottie.cpp', THORVG,
            *UI_LIBS, '-loleaut32', '-lshell32', '-lmsimg32', '-lbcrypt'], [ROOT, language_evidence])
    fixture('bookmarks_test', [*STRICT, 'tests/bookmarks_test.cpp'])
    fixture('bookmark_drag_test', [*STRICT, 'tests/bookmark_drag_test.cpp'])   # drag and drop rules (src/bookmark-drag.h)
    promote_work = OUT / 'bookmark-promote-work'
    promote_work.mkdir(parents=True, exist_ok=True)
    fixture('bookmark_promote_test', [*STRICT, '-municode', '-Isrc', 'tests/browser-import/bookmark_promote_test.cpp', '-lshell32', '-lole32', '-ladvapi32'], [promote_work])
    fixture('bookmark_move_test', [*STRICT, '-municode', '-Isrc', 'tests/browser-import/bookmark_move_test.cpp', '-lshell32', '-lole32', '-ladvapi32'], [promote_work])
    fixture('browser_catalog_test', [*STRICT, '-municode', '-Isrc', 'tests/browser-import/browser_catalog_test.cpp', '-lshell32', '-lole32', '-ladvapi32', '-lgdi32', '-luser32'], [promote_work])
    # Import from a browser (design/browser-data-import-plan.md): synthetic roots only, Windows' winsqlite3.dll
    import_work = OUT / 'browser-import-work'
    import_work.mkdir(parents=True, exist_ok=True)
    fixture('browser_import_test', [*STRICT, '-Isrc', 'tests/browser-import/browser_import_test.cpp', '-lshell32', '-lole32', '-ladvapi32'], [import_work])
    # Imported bookmarks' site icons (src/import-icons.h): the source's own (synthetic Chromium / Firefox / HTML), saved
    # without replacing any, the missing ones fetched from loopback servers only (127.0.0.1, ephemeral ports)
    icons_work = OUT / 'import-icons-work'
    icons_work.mkdir(parents=True, exist_ok=True)
    fixture('import_icons_test', [*STRICT, '-Isrc', 'tests/browser-import/import_icons_test.cpp', '-lshell32', '-lole32', '-ladvapi32', '-lws2_32'], [icons_work])   # WinHTTP: loaded at run time, as in the browser
    # Settings wiring of the import row and the password manager row (main.cpp hooks + real drawer, synthetic hooks)
    fixture('import_settings_test', [*LOOSE, '-Werror', '-municode', *TVG, WV2, 'tests/browser-import/import_settings_test.cpp', 'src/native-lottie.cpp', THORVG,
            *UI_LIBS, '-loleaut32', '-lshell32', '-lmsimg32', '-lbcrypt'], [ROOT])
    # Import dialog states rendered offscreen by the production modal (native fixture), each PNG checked non-blank
    import_render = OUT / 'import-render'
    import_render.mkdir(parents=True, exist_ok=True)
    fixture('import_dialog_render_test', [*LOOSE, '-Werror', '-municode', *TVG, WV2, 'tests/browser-import/import_dialog_render_test.cpp', 'src/native-lottie.cpp', THORVG,
            *UI_LIBS, '-loleaut32', '-lshell32', '-lmsimg32', '-lbcrypt', '-ladvapi32'], [ROOT, import_render])
    # #121: typed-info store, engine import (fixture SQLite), forms world, placement, arming, our list and bubble rendered
    # offscreen (no window). The Paper comparison of those renders runs in test-form-memory.ps1 (needs Pillow).
    forms_out = OUT / 'form-memory'; forms_out.mkdir(parents=True, exist_ok=True)
    fixture('form_memory_test', [*LOOSE, '-Werror', '-municode', '-Isrc', *TVG, 'tests/form_memory_test.cpp', 'src/native-lottie.cpp', THORVG,
            *UI_LIBS, '-loleaut32', '-lshell32', '-ladvapi32'], [ROOT, forms_out])
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
    # Typing flicker: every native text field shows no frame between before and after a keystroke (real EDITs, surface pixels)
    flicker_evidence = ROOT / 'build/flicker-qa'
    flicker_evidence.mkdir(parents=True, exist_ok=True)
    fixture('edit_flicker_test', [*LOOSE, '-Werror', '-municode', *TVG, WV2, 'tests/ui/edit_flicker_test.cpp', 'src/native-lottie.cpp', THORVG,
            *UI_LIBS, '-lshell32', '-loleaut32', '-lmsimg32', '-lshcore'], [ROOT, flicker_evidence])
    fixture('shortcuts_test', ['-O0', *TVG, WV2, 'tests/shortcuts_test.cpp', 'src/native-lottie.cpp', THORVG, *UI_LIBS])
    fixture('navigation_errors_test', [*LOOSE, *TVG, WV2, 'tests/navigation_errors_test.cpp', 'src/native-lottie.cpp', THORVG, *UI_LIBS])
    # #121 crashed-tab page and screen-share confirmation (src/crash-page.h, src/origin-prompt.h ScreenCapture). The engine
    # facts behind them are measured by tests/crash-screenshare/engine_probe.py (real engine, window never shown).
    fixture('crash_screenshare_test', [*LOOSE, '-Werror', '-municode', *TVG, WV2, 'tests/crash_screenshare_test.cpp', *UI_LIBS], [ROOT])
    # A navigation the engine turned into a download is never an error page (src/download-navigation.h; real engine:
    # tests/downloads/download_navigation_e2e.py)
    fixture('download_navigation_test', [*STRICT, 'tests/download_navigation_test.cpp'])
    fixture('tab_drag_test', [*LOOSE, *TVG, WV2, 'tests/ui/tab_drag_test.cpp', 'src/native-lottie.cpp', THORVG, *UI_LIBS])
    # Atelier B: a tab dropped on the favorites (rules, then the real window procedure on a never-shown window +
    # offscreen renders, the 3 layouts; its throwaway data folder lives under the output).
    fixture('tab_favorite_drop_test', [*STRICT, 'tests/tab_favorite_drop_test.cpp'])
    fixture('tab_favorite_lastrow_test', [*STRICT, 'tests/tab_favorite_lastrow_test.cpp'])   # the grey place = the grid with one more tile (last row too)
    favorites_drag = OUT / 'tab-favorite-drag'
    favorites_drag.mkdir(parents=True, exist_ok=True)
    fixture('tab_favorite_drag_test', [*LOOSE, '-municode', *TVG, WV2, 'tests/ui/tab_favorite_drag_test.cpp', 'src/native-lottie.cpp', THORVG,
            *UI_LIBS, '-loleaut32', '-lshell32'], [ROOT, favorites_drag])
    # Split view: pure model (zones, capacity, rails, springs) and host behaviour through WndProc
    fixture('split_view_test', [*STRICT, 'tests/split-view/split_view_test.cpp'])
    fixture('site_name_test', [*STRICT, 'tests/split-view/site_name_test.cpp'])
    fixture('split_ui_test', [*LOOSE, *TVG, WV2, 'tests/ui/split_ui_test.cpp', 'src/native-lottie.cpp', THORVG, *UI_LIBS])
    # Split view hover frames (Val's « Atelier » boards « Fenêtre »): the pure state machine and geometry, then the host
    # (réduire / fermer through the real split view, the layered windows under a never-shown window, renders).
    fixture('split_frames_test', [*STRICT, 'tests/split-view/split_frames_test.cpp'])
    frames_manifest = OUT / 'split_frames_manifest.o'
    run('windres split_frames_ui_test.rc', [BIN / 'windres.exe', 'tests/ui/split_frames_ui_test.rc', '-O', 'coff', '-o', frames_manifest], now=True)
    fixture('split_frames_ui_test', [*LOOSE, *TVG, WV2, 'tests/ui/split_frames_ui_test.cpp', frames_manifest, 'src/native-lottie.cpp', THORVG, *UI_LIBS])
    # Window title / icon changes never hide the page, even for an instant (the canvas flashed in place of the page on
    # clicks in a site); the same title is not sent again on each page event. Screen-free: a DWM-cloaked window.
    fixture('window_title_test', [*LOOSE, '-Werror', *TVG, WV2, 'tests/ui/window_title_test.cpp', 'src/native-lottie.cpp', THORVG, *UI_LIBS])
    # Screen-reader view of the chrome through a real UI Automation client (issue #2)
    # Native menus: a submenu's row hover follows the pointer mid-entrance (cascade, keyboard, reduced motion)
    fixture('context_menu_hover_test', [*LOOSE, '-municode', *TVG, 'tests/ui/context_menu_hover_test.cpp', 'src/native-lottie.cpp', THORVG, *UI_LIBS, '-lshell32', '-loleaut32'], [ROOT])
    # Suggestions popup and Favoris drawer redraw at the DPI of the monitor they land on (SKIP with a single DPI, as on CI).
    fixture('address_popup_dpi_test', [*LOOSE, '-Werror', '-municode', *TVG, WV2, 'tests/ui/address_popup_dpi_test.cpp', 'src/native-lottie.cpp', THORVG, *UI_LIBS, '-lshell32', '-loleaut32', '-lmsimg32', '-lshcore'], [ROOT])
    fixture('select_all_test', [*STRICT, 'tests/ui/select_all_test.cpp', '-luser32', '-lcomctl32', '-ldwmapi', '-lgdi32'])
    # Paint path (offscreen): bar favicons uploaded once, text cache kept across paints, audio/downloads icon repaint rates.
    paint_evidence = ROOT / 'build/paint-perf-qa'
    paint_evidence.mkdir(parents=True, exist_ok=True)
    fixture('paint_perf_render_test', [*STRICT, '-Wno-misleading-indentation', '-municode', *TVG, 'tests/ui/paint_perf_render_test.cpp', 'src/native-lottie.cpp', THORVG,
            '-ld2d1', '-ldwrite', '-lwindowscodecs', '-lole32', '-luuid', '-lcomctl32', '-lgdi32', '-ldwmapi', '-lshell32'],
            [ROOT, paint_evidence])
    fixture('accessibility_test', [*LOOSE, *TVG, WV2, 'tests/accessibility/accessibility_test.cpp', 'src/native-lottie.cpp', THORVG, *UI_LIBS, '-loleaut32'])
    # Download history popover/drawer (upstream PR #28/#29, same line as test-downloads.ps1)
    downloads_evidence = ROOT / 'build/downloads-qa'
    downloads_evidence.mkdir(parents=True, exist_ok=True)
    fixture('download_panel_test', [*STRICT, '-municode', *TVG, 'tests/downloads/download_panel_test.cpp', 'src/native-lottie.cpp', THORVG,
            '-ld2d1', '-ldwrite', '-lwindowscodecs', '-lole32', '-luuid', '-lcomctl32', '-lgdi32', '-ldwmapi', '-lshell32'],
            [ROOT, downloads_evidence])
    # Its rows' file icons: type by extension + MIME, image thumbnails (fallbacks, cache), SVG never decoded. No window.
    fixture('download_file_kinds_test', [*STRICT, '-municode', *TVG, 'tests/downloads/download_file_kinds_test.cpp', 'src/native-lottie.cpp', THORVG,
            '-ld2d1', '-ldwrite', '-lwindowscodecs', '-lole32', '-luuid', '-lcomctl32', '-lgdi32', '-ldwmapi', '-lshell32'],
            [ROOT, downloads_evidence])
    # Browsing history drawer opened by the toolbar button (same line as test-history.ps1)
    history_evidence = ROOT / 'build/history-qa'
    history_evidence.mkdir(parents=True, exist_ok=True)
    fixture('history_panel_test', [*STRICT, '-municode', *TVG, 'tests/history/history_panel_test.cpp', 'src/native-lottie.cpp', THORVG,
            '-ld2d1', '-ldwrite', '-lwindowscodecs', '-lole32', '-luuid', '-lcomctl32', '-lgdi32', '-ldwmapi', '-lshell32'],
            [ROOT, history_evidence])
    # First-run onboarding (Paper p-R-0): the flow, the first-run rule and the motion policy (pure), then the seven
    # boards drawn off screen by the production window at 96 and 120 DPI (shaders on Direct3D, WARP without a GPU),
    # with the Paper geometry, keyboard order, events and both languages. Real window: tests/onboarding/qa.py.
    fixture('onboarding_flow_test', [*STRICT, 'tests/onboarding/onboarding_flow_test.cpp'])
    # The 2 -> 3 transition as numbers: defaults, text round trip of the debug panel's file, bounds, timeline
    fixture('onboarding_tuning_test', [*STRICT, 'tests/onboarding/onboarding_tuning_test.cpp'])
    # Its import: board 3's lists and the commit into a profile folder, on synthetic sources only (*.example)
    onboarding_sources, onboarding_work = OUT / 'onboarding-import-sources', OUT / 'onboarding-import-work'
    for folder in (onboarding_sources, onboarding_work):
        if folder.exists():
            shutil.rmtree(folder)
    onboarding_work.mkdir(parents=True)
    run('synthetic import sources for the onboarding', [sys.executable, '-I', ROOT / 'tests/browser-import/make_sources.py', onboarding_sources])
    fixture('onboarding_import_test', [*STRICT, '-municode', '-Isrc', 'tests/onboarding/onboarding_import_test.cpp', '-lshell32', '-lole32', '-ladvapi32', '-lgdi32', '-luser32'],
            [onboarding_sources, onboarding_work])
    onboarding_evidence = ROOT / 'build/onboarding-render'
    onboarding_evidence.mkdir(parents=True, exist_ok=True)
    fixture('onboarding_render_test', [*LOOSE, '-Werror', '-municode', *TVG, WV2, 'tests/onboarding/onboarding_render_test.cpp', 'src/native-lottie.cpp', THORVG,
            *UI_LIBS, '-loleaut32', '-lshell32'], [ROOT, onboarding_evidence])
    # Where « Annoter » is on (#125): local projects and files, normal or private window; live sites off (src/annotation-sites.h)
    fixture('annotation_sites_test', [*STRICT, 'tests/annotations/annotation_sites_test.cpp'])
    # A private window's notes are erased when it closes; a crashed one's at the next start; normal notes untouched (src/annotation-private.h)
    fixture('annotation_private_test', [*STRICT, '-O2', 'tests/annotations/annotation_private_test.cpp', 'src/annotations.cpp', '-lbcrypt'])
    # The annotation mode's onboarding (#125, Paper « MCP - Design UI UX » A1 – A4): flow, first-time rule and its setting,
    # the four boards drawn off screen by the production window, Paper geometry, buttons, keys, A3's connected state.
    annotation_onboarding_evidence = ROOT / 'build/annotation-onboarding-render'
    annotation_onboarding_evidence.mkdir(parents=True, exist_ok=True)
    fixture('annotation_onboarding_test', [*LOOSE, '-Werror', '-municode', *TVG, WV2, 'tests/onboarding/annotation_onboarding_test.cpp', 'src/native-lottie.cpp', 'src/agent-install.cpp', 'src/agent-runtime.cpp', 'src/acp-client.cpp', THORVG,
            *UI_LIBS, '-loleaut32', '-lshell32', '-luserenv', '-lwinhttp', '-lbcrypt'], [ROOT, annotation_onboarding_evidence])


def peer_suites() -> None:
    core = ROOT / 'tests/update-core/run_tests.py'
    toast = ROOT / 'tests/update-toast/run.py'
    webview2 = ROOT / 'tests/webview2-runtime/run_tests.py'   # startup recovery (fakes only, read-only loader probe)
    for label, path in (('update-core tests', core), ('update-toast tests', toast), ('webview2 startup recovery tests', webview2)):
        if not path.exists():
            print(f'FAILED: {label}: runner {path.relative_to(ROOT)} missing')
            sys.exit(3)
        script(label, path)


def integration() -> None:
    run('release tooling tests', [sys.executable, '-I', HERE / 'test_release_tools.py'])
    # v0.3.239's own package validator (from git history) accepts the assets/bin bridge, refuses a root one
    run('old client validator accepts the release bridge payload', [sys.executable, HERE / 'old_validator.py'])
    script('host integration + mocked end-to-end install', HERE / 'e2e_install.py')
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
        # The name Windows shows (Properties, Task Manager, SmartScreen) is the product « Browser », capital B; the
        # file names keep their lowercase (browser.exe, update-helper.exe: updates and pins rely on them).
        if strings['ProductName'] != 'Browser' or not strings['FileDescription'].startswith('Browser'):
            sys.exit(f'{exe.name}: ProductName/FileDescription {strings["ProductName"]!r}/{strings["FileDescription"]!r} do not name « Browser »')
    # The stable launcher (<root>\browser.exe = update-helper.exe) carries the name a pinned window shows: string 1
    # (src/taskbar-identity.h NameResource; a plain string would read back « browser », window properties are atoms).
    import ctypes
    buf = ctypes.create_unicode_buffer(256)
    shown = buf.value if ctypes.windll.shlwapi.SHLoadIndirectString(f'@{app / "update-helper.exe"},-1', buf, 256, None) == 0 else None
    if shown != 'Browser':
        sys.exit(f'update-helper.exe string 1 is {shown!r}, not « Browser » (pinned window name)')
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
    p.add_argument('--jobs', type=int, default=0, help='compiles side by side (default: one per core, within the free memory; 1: one at a time)')
    p.add_argument('--runs-only', action='store_true', help="phase B alone, from an earlier full run's outputs (local measurements; never on CI)")
    p.add_argument('--memory-gb', type=float, default=0.0, help="memory the compiles may use (default: the free memory less 0.5 GB), e.g. to try a smaller runner")
    a = p.parse_args()
    jobs = a.jobs if a.jobs > 0 else default_jobs()
    global RUNS_ONLY
    RUNS_ONLY = a.runs_only
    # agent guard (tests/agent-guard/agent_guard.py): every program this run starts (installers, browsers, peers) inherits
    # no API key or agent credential, and no switch to this computer's agents or to a fake agent
    sys.path.insert(0, str(ROOT / 'tests' / 'agent-guard'))
    import agent_guard as G
    import os
    clean, _stripped = G.scrub(dict(os.environ), keep_locations=True)
    for k in [k for k in os.environ if k not in clean] + [G.REAL_AGENT_SWITCH, G.REAL_OPT_IN[0], 'BROWSER_CHAT_FAKE_AGENT']:
        os.environ.pop(k, None)
    OUT.mkdir(parents=True, exist_ok=True)
    steps = {'existing': existing_fixtures, 'peers': peer_suites, 'integration': integration}
    selected = [step for key, step in steps.items() if a.only in (None, key)]
    if selected:   # one pool for all their compiles, then their runs in this order
        planned(lambda: [step() for step in selected], jobs, a.memory_gb)
    if a.app and a.only in (None, 'app'):
        check_app((ROOT / a.app).resolve())
    print('\nALL PASSED')
    for label, seconds in results:
        print(f'  ok  {seconds:6.1f}s  {label}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
