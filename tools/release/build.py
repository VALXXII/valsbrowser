"""Reproducible native Windows x64 build of browser + update helper.

Used identically by local builds (no PowerShell needed) and by CI. Only project-local,
pinned tools are used: tools/w64devkit 2.11.0 (setup.ps1, SHA-256 checked), the
WebView2 SDK 1.0.4314.35 (setup.ps1, SHA-256 checked) and ThorVG 1.1.3
(build-lottie.py, SHA-256 checked). Nothing is installed globally and the global
PATH is not modified.

Output layout (the application directory, exactly the release payload):
  <out>/browser.exe, <out>/<helper>.exe, WebView2Loader.dll, licences, assets/**,
  version.json
  + the MCP stdio bridge over the annotation store (browser-annotations-mcp.exe), placed by
    bridge_location():
      --channel release: <out>/assets/bin/browser-annotations-mcp.exe. Documented product
        payload: it is the only place every existing client accepts (their AllowedPayloadPath
        knows fixed root names + assets/**; a root-level exe would make them reject every
        update). tools/release/package.py refuses any other executable under assets/.
      other channels: <out>/browser-annotations-mcp.exe (dev bridge next to browser.exe).
  + the chat panel's page (assets/chat-panel/: index.html, panel.css, panel.js, markdown.js, icons.js; its QA
    host dev.html / dev-host.js is never copied) — design/chat-integration.md.
Test tools go to <out>-test (never in a package): the fake ACP agent fake-acp-agent.exe (tests/acp), built for
non-release channels only; the browser uses it when BROWSER_ANNOTATIONS_TEST=1 and BROWSER_CHAT_FAKE_AGENT name it.
Intermediates (resources object, generated headers) go to <out>-obj so they can
never leak into a package. Among them, release-notes-data.h: the « Nouveautés » card's notes, checked and
generated from release-notes/notes.json (tools/release/release_notes.py, release-notes/README.md).
"""
from __future__ import annotations
import argparse, json, os, shutil, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import version as version_mod  # noqa: E402
import release_notes as notes_mod  # noqa: E402

BIN = ROOT / 'tools/w64devkit/bin'
SDK = ROOT / 'deps/webview2/build/native'
THORVG = ROOT / 'build/lottie-backend/src/libthorvg-1.a'
DEFAULT_REPO = 'VALXXII/browser-releases'

# Browser translation units. Update engine/toast sources are owned by their peers;
# the lists below name them exactly as their contracts publish them.
BROWSER_SOURCES = ['src/main.cpp', 'src/native-lottie.cpp',
                   'src/update-core.cpp', 'src/update-package.cpp', 'src/update-toast.cpp',
                   'src/annotations.cpp',
                   # Annotations V2: one-click agent connection (Claude .mcpb / config merge, Codex), no UI.
                   'src/agent-connect.cpp']
# Chat intégré (compiled with the annotation mode, src/chat-host.h). Linked into every binary that compiles the
# annotation mode (tests/update-integration/e2e_install.py uses this list too).
CHAT_SOURCES = [
    # Chat V1 (C1): ACP v1 client (one agent process in a Job Object) and the agents' runtime (private Node,
    # pinned adapters, CLI detection). Tests: tests/acp/run_tests.py.
    'src/acp-client.cpp', 'src/agent-runtime.cpp',
    # « Ton agent »: the agents' one status (src/agent-status.h) and their official installers (src/agent-install.h).
    # Tests: tests/acp/run_tests.py (agent_status_test).
    'src/agent-install.cpp',
    # Chat V1 (C3): undo by our own file copies, no git, no UI. Tests: tests/snapshots/run_tests.py.
    'src/change-snapshots.cpp']
CHAT_LIBS = ['-luserenv']   # CreateEnvironmentBlock (the agent gets the user's current environment)
# Fake ACP agent of the tests (console exe, never packaged): <out>-test/fake-acp-agent.exe.
FAKE_AGENT_SOURCES = ['tests/acp/fake_agent.cpp']
FAKE_AGENT_NAME = 'fake-acp-agent.exe'
# QA-only files of assets/ that are never part of a build (design/chat-panel.md §1).
ASSET_EXCLUDES = {'chat-panel': ('dev.html', 'dev-host.js')}


def test_tools_dir(out: Path) -> Path:
    return out.parent / (out.name + '-test')


def ignore_qa_assets(directory: str, names: list[str]) -> list[str]:
    return [n for n in names if n in ASSET_EXCLUDES.get(Path(directory).name, ())]
BROWSER_LIBS = ['-lole32', '-luuid', '-lcomctl32', '-lshlwapi', '-lgdi32', '-ld2d1', '-ldwrite',
                '-lwindowscodecs', '-ldwmapi', '-lwinhttp', '-lbcrypt', '-lshell32', '-luser32',
                '-lkernel32', '-ladvapi32']
HELPER_SOURCES = ['src/update-helper.cpp', 'src/update-package.cpp']
HELPER_NAME = 'update-helper.exe'
HELPER_LIBS = ['-lbcrypt', '-lshlwapi', '-lshell32', '-luser32', '-lkernel32', '-ladvapi32', '-lole32']
# Annotations MCP bridge: console exe, static, no runtime; JSON parser is in-house (src/strict-json.h),
# so no third-party licence is added.
# update-core/update-package: the portable->installed migration reuses UpdateCore::PortableForwardTarget.
BRIDGE_SOURCES = ['src/mcp-bridge.cpp', 'src/annotations.cpp', 'src/update-core.cpp', 'src/update-package.cpp']
BRIDGE_NAME = 'browser-annotations-mcp.exe'
RELEASE_BRIDGE = f'assets/bin/{BRIDGE_NAME}'  # same constant in tools/release/package.py
BRIDGE_LIBS = ['-lwinhttp', '-lbcrypt', '-lkernel32']
# Setup stub: browser-<version>-win-x64-setup.exe WITHOUT its payload (design/windows-setup-plan.md). Never part of
# the app payload: written to <out>-setup/, tools/release/package.py appends the .lsrupd it just packed.
SETUP_SOURCES = ['src/setup-main.cpp', 'src/setup-core.cpp', 'src/update-core.cpp', 'src/update-package.cpp']
SETUP_NAME = 'browser-setup.exe'
SETUP_LIBS = ['-lwinhttp', '-lbcrypt', '-lole32', '-luuid', '-lshell32', '-lshlwapi', '-lcomctl32', '-ladvapi32', '-luser32',
              '-lgdi32', '-lkernel32']


def setup_stub_dir(out: Path) -> Path:
    return out.parent / (out.name + '-setup')
WARNINGS = ['-Wall', '-Wextra', '-Wno-unknown-pragmas', '-Wno-cast-function-type',
            '-Wno-misleading-indentation', '-Wno-missing-field-initializers']
LICENSES = {  # destination name -> source
    'WebView2-LICENSE.txt': 'deps/webview2/LICENSE.txt',
    'WebView2-NOTICE.txt': 'deps/webview2/NOTICE.txt',
    'ThorVG-LICENSE.txt': 'third_party/thorvg/LICENSE',
    'RapidJSON-LICENSE.txt': 'third_party/thorvg/RapidJSON-LICENSE',
    # Ad blocker: unmodified uBlock Origin Lite (GPLv3) in assets/extensions/ublock-lite.
    'uBOLite-LICENSE.txt': 'assets/extensions/ublock-lite/LICENSE.txt',
    'uBOLite-NOTICE.txt': 'third_party/ublock-lite/NOTICE.txt',
    # Chat (C1): src/acp-client.cpp and src/agent-runtime.cpp adapt logic from Gui (Apache-2.0); see their headers.
    'Gui-LICENSE.txt': 'third_party/gui/LICENSE',
    'Gui-NOTICE.txt': 'third_party/gui/NOTICE',
}


# Reproducible PE headers: binutils stamps SOURCE_DATE_EPOCH instead of the wall clock.
BUILD_ENV = dict(os.environ)


def run(cmd: list[str]) -> None:
    print('+', ' '.join(str(c) for c in cmd), flush=True)
    subprocess.run([str(c) for c in cmd], cwd=ROOT, check=True, env=BUILD_ENV)


def c_string(text: str) -> str:
    if any(c in text for c in '"\\\r\n') or not text.isascii():
        raise SystemExit(f'Refusing to embed unsafe build string: {text!r}')
    return f'"{text}"'


def write_build_info(gen: Path, info: dict, repo: str) -> None:
    gen.mkdir(parents=True, exist_ok=True)
    v = info
    (gen / 'build-info.h').write_text(
        '// Generated by tools/release/build.py. Do not edit.\n#pragma once\n'
        f'#define BROWSER_VERSION_MAJOR {v["major"]}\n#define BROWSER_VERSION_MINOR {v["minor"]}\n'
        f'#define BROWSER_BUILD_NUMBER {v["build"]}\n'
        f'#define BROWSER_VERSION_TEXT {c_string(v["version"])}\n'
        f'#define BROWSER_VERSION_WIDE L{c_string(v["version"])}\n'
        f'#define BROWSER_COMMIT_TEXT {c_string(v["commit"])}\n'
        f'#define BROWSER_CHANNEL_TEXT {c_string(v["channel"])}\n'
        f'#define BROWSER_UPDATE_REPO_WIDE L{c_string(repo)}\n'
        '#define BROWSER_UPDATE_VERSION BROWSER_VERSION_WIDE\n'
        f'#define BROWSER_FILEVERSION {v["major"]},{v["minor"]},{v["build"]},0\n'
        # src/app-icon.rc: live icon for release builds, preview icon otherwise.
        + ('#define BROWSER_CHANNEL_RELEASE 1\n' if v['channel'] == 'release' else ''),
        encoding='ascii', newline='\n')


PAYLOAD_FILES = ['browser.exe', HELPER_NAME, BRIDGE_NAME, 'WebView2Loader.dll', 'version.json', *LICENSES]


def bridge_location(channel: str, mode: str, annotations: bool) -> str | None:
    """Where the bridge goes (relative to <out>), or None. Refuses incoherent combinations:
    an annotation UI without its bridge, or a bridge without the annotation mode."""
    if mode not in ('auto', 'on', 'off'):
        raise SystemExit(f'bad --annotations-bridge {mode!r}')
    if not annotations:
        if mode == 'on':
            raise SystemExit('--annotations-bridge on contradicts --no-annotations')
        return None
    if channel == 'release':
        if mode == 'off':
            raise SystemExit('a release build with annotations ships its bridge; use --no-annotations for a baseline')
        return RELEASE_BRIDGE
    return None if mode == 'off' else BRIDGE_NAME


def write_release_notes(gen: Path, info: dict) -> None:
    """The card's notes (refused with a message when notes.json breaks a rule of release-notes/README.md). A release
    whose merge did not change them shows no card after its update: said loudly in the CI log, never an error."""
    notes = notes_mod.write_header(gen / 'release-notes-data.h')
    nid = notes_mod.notes_id(notes)
    print(f'Release notes: {nid or "none (no card after the update)"}', flush=True)
    if info['channel'] != 'release':
        return
    rel = notes_mod.NOTES.relative_to(ROOT).as_posix()
    try:
        before = version_mod.git('show', f'{info["commit"]}^1:{rel}')
        now = version_mod.git('show', f'{info["commit"]}:{rel}')
    except subprocess.CalledProcessError:
        return
    if before == now:
        prefix = '::warning title=Release notes::' if os.environ.get('GITHUB_ACTIONS') == 'true' else 'WARNING: '
        print(f'{prefix}{rel} unchanged by this merge: users updating from the previous build get the generic '
              '"Mise a jour installee" notification, not the "Nouveautes" card (release-notes/README.md).', flush=True)


def release_annotation_defines(channel: str, bridge: str | None, annotations: bool) -> list[str]:
    """src/main.cpp compiles the annotation mode into a --channel release build only with
    BROWSER_RELEASE_ANNOTATIONS, which is passed only when the bridge ships at RELEASE_BRIDGE."""
    if channel == 'release' and annotations and bridge == RELEASE_BRIDGE:
        return ['-DBROWSER_RELEASE_ANNOTATIONS']
    return []


def clean_payload(out: Path) -> None:
    """Remove only previously built payload entries. Anything else in the directory
    (an old exe-adjacent profile, session or log) is left strictly untouched."""
    if not out.exists():
        return
    for name in PAYLOAD_FILES:
        target = out / name
        if target.is_file() and not target.is_symlink():
            target.unlink()
    assets = out / 'assets'
    if assets.is_dir() and not assets.is_symlink():
        shutil.rmtree(assets)


def build(out: Path, info: dict, repo: str, preview: bool = False, bridge: str | None = None,
          annotations: bool = True) -> None:
    for tool in ('g++.exe', 'windres.exe', 'strip.exe'):
        if not (BIN / tool).exists():
            raise SystemExit(f'Missing {BIN / tool}: run setup.ps1 (pinned w64devkit) first.')
    if not (SDK / 'include/WebView2.h').exists():
        raise SystemExit('Missing WebView2 SDK: run setup.ps1 first.')
    if not THORVG.exists():
        run([sys.executable, ROOT / 'build-lottie.py'])
    run([sys.executable, ROOT / 'fetch-ublock-lite.py'])  # pinned ad blocker; no-op when present
    if '/' not in repo or any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._/' for c in repo):
        raise SystemExit(f'Invalid update repository: {repo!r}')
    obj = out.parent / (out.name + '-obj')
    clean_payload(out)
    if obj.exists():
        shutil.rmtree(obj)
    out.mkdir(parents=True, exist_ok=True)
    write_build_info(obj / 'gen', info, repo)
    write_release_notes(obj / 'gen', info)
    res, helper_res, bridge_res = obj / 'browser-res.o', obj / 'update-helper-res.o', obj / 'mcp-bridge-res.o'
    # Both executables embed the channel's app icon (src/app-icon.rc): the helper is also the
    # stable launcher <root>\browser.exe that the Start Menu shortcut targets.
    # All three carry the publisher metadata of src/version-info.rc; the bridge has no icon.
    for rc, o in (('src/browser.rc', res), ('src/update-helper.rc', helper_res), ('src/mcp-bridge.rc', bridge_res)):
        run([BIN / 'windres.exe', '-I.', '-Isrc', f'-I{obj / "gen"}', '-DBROWSER_HAS_BUILD_INFO',
             rc, '-O', 'coff', '-o', o])
    # No PE link timestamp and relative source paths: identical inputs give identical binaries.
    common = ['-std=c++17', '-O2', *WARNINGS, '-static', '-municode', '-Isrc', f'-I{obj / "gen"}',
              '-DBROWSER_HAS_BUILD_INFO', '-Wl,--no-insert-timestamp', f'-ffile-prefix-map={ROOT}=.',
              # Hardening (audit SEC-16): stack canaries for the hand-written parsers; ASLR / DEP stated, not assumed.
              '-fstack-protector-strong', '-Wl,--dynamicbase,--nxcompat,--high-entropy-va']
    if preview:  # design preview: updates and the hand-over to an installed copy are compiled out
        common.append('-DBROWSER_PREVIEW')
    if not annotations:  # measurement baseline: the annotation mode compiled out (src/main.cpp)
        common.append('-DBROWSER_NO_ANNOTATIONS')
    common += release_annotation_defines(info['channel'], bridge, annotations)
    chat = CHAT_SOURCES if annotations else []   # the chat lives inside the annotation mode
    run([BIN / 'g++.exe', *common, '-mwindows', '-DTVG_STATIC', '-Ideps/thorvg-1.1.3/inc',
         f'-I{SDK / "include"}', *BROWSER_SOURCES, *chat, res, THORVG, '-o', out / 'browser.exe',
         *BROWSER_LIBS, *(CHAT_LIBS if annotations else [])])
    run([BIN / 'g++.exe', *common, '-mwindows', *HELPER_SOURCES, helper_res, '-o', out / HELPER_NAME,
         *HELPER_LIBS])
    executables = ['browser.exe', HELPER_NAME]
    if bridge:  # console subsystem: stdio is the MCP transport; linked in <out>-obj, placed below
        run([BIN / 'g++.exe', *common, *BRIDGE_SOURCES, bridge_res, '-o', obj / BRIDGE_NAME, *BRIDGE_LIBS])
        run([BIN / 'strip.exe', '--strip-debug', obj / BRIDGE_NAME])
    for exe in executables:
        run([BIN / 'strip.exe', '--strip-debug', out / exe])
    tools_dir = test_tools_dir(out)
    if tools_dir.exists():
        shutil.rmtree(tools_dir)
    if annotations and info['channel'] != 'release':  # the sandbox's fake agent: no account, no network, no cost
        tools_dir.mkdir(parents=True)
        run([BIN / 'g++.exe', *common, '-mconsole', *FAKE_AGENT_SOURCES, '-o', tools_dir / FAKE_AGENT_NAME])
        run([BIN / 'strip.exe', '--strip-debug', tools_dir / FAKE_AGENT_NAME])
    if not preview:  # setup stub (no self-update in a preview, so no installer either)
        stub_dir = setup_stub_dir(out)
        if stub_dir.exists():
            shutil.rmtree(stub_dir)
        stub_dir.mkdir(parents=True)
        setup_res = obj / 'setup-res.o'
        run([BIN / 'windres.exe', '-I.', '-Isrc', f'-I{obj / "gen"}', '-DBROWSER_HAS_BUILD_INFO', 'src/setup.rc',
             '-O', 'coff', '-o', setup_res])
        run([BIN / 'g++.exe', *common, '-mwindows', *SETUP_SOURCES, setup_res, '-o', stub_dir / SETUP_NAME, *SETUP_LIBS])
        # --strip-all: a COFF symbol table after the sections would read as an overlay (package.py refuses it).
        run([BIN / 'strip.exe', '--strip-all', stub_dir / SETUP_NAME])
    shutil.copy2(SDK / 'x64/WebView2Loader.dll', out / 'WebView2Loader.dll')
    for name, source in LICENSES.items():
        shutil.copy2(ROOT / source, out / name)
    shutil.copytree(ROOT / 'assets', out / 'assets', ignore=ignore_qa_assets)
    # Same icon as a loose file: the updater refreshes <root>\browser.ico from it for shortcuts.
    shutil.copy2(version_mod.app_icon(info['channel']), out / 'assets/browser.ico')
    if bridge:
        (out / bridge).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(obj / BRIDGE_NAME, out / bridge)
    (out / 'version.json').write_text(json.dumps({
        'product': 'browser', 'version': info['version'], 'build': info['build'],
        'commit': info['commit'], 'channel': info['channel'], 'arch': 'x64',
        'updateRepository': repo}, indent=2) + '\n', encoding='utf-8', newline='\n')
    print(f'Build OK: {out / "browser.exe"} ({info["version"]}, {info["commit"][:12]})')


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--out', default='build/update-integration')
    p.add_argument('--channel', default='dev', choices=('dev', 'release'))
    p.add_argument('--build', type=int, help='override build number (default: git rev-list --count HEAD)')
    p.add_argument('--commit')
    p.add_argument('--preview', action='store_true',
                   help='design preview build: no self-update, never forwards to an installed copy')
    p.add_argument('--update-repo', default=DEFAULT_REPO,
                   help='owner/repo whose GitHub releases are the only trusted update feed')
    p.add_argument('--annotations-bridge', choices=('auto', 'on', 'off'), default='auto',
                   help=f'build {BRIDGE_NAME} (auto: release -> {RELEASE_BRIDGE}, other channels -> next to browser.exe)')
    p.add_argument('--no-annotations', action='store_true',
                   help='compile the annotation mode out of browser.exe (baseline for measurements); no bridge')
    a = p.parse_args()
    out = (ROOT / a.out).resolve()
    if ROOT.resolve() not in out.parents:
        raise SystemExit(f'Refusing output directory outside the repository: {out}')
    info = version_mod.identity(a.build, a.commit, a.channel)
    if info['channel'] == 'release':
        version_mod.require_on_main(info['commit'])
    try:
        BUILD_ENV['SOURCE_DATE_EPOCH'] = version_mod.git('show', '-s', '--format=%ct', info['commit'])
    except subprocess.CalledProcessError:
        BUILD_ENV['SOURCE_DATE_EPOCH'] = '0'
    bridge = bridge_location(info['channel'], a.annotations_bridge, not a.no_annotations)
    build(out, info, a.update_repo, a.preview, bridge, not a.no_annotations)
    return 0


if __name__ == '__main__':
    sys.exit(main())
