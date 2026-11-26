#!/usr/bin/env python3
"""Windowless tests of the agent guard (tests/agent-guard/agent_guard.py) and of its use by the chat harness.

    python -I tests/agent-guard/run_tests.py            (everything; real processes, no window)
    python -I tests/agent-guard/run_tests.py --static   (the static launcher check only: no build, no process; CI step
                                                         « existing » of tests/update-integration/run_all.py)

  - a missing / relative / wrongly named / oversized / foreign fake agent: AgentGuardError BEFORE any process starts
    (tests/chat/chat_qa.py launch() is called with subprocess.Popen replaced by a spy: never called, nothing copied);
  - the browser's environment: keys and agent credentials stripped, the « user's » folders and PATH redirected under
    the work folder, the test switch on, BROWSER_CHAT_REAL_AGENT refused, the installed browser refused;
  - the real agent only with the person's own BROWSER_TEST_REAL_AGENT=yes-use-my-account;
  - the browser's log: a launch that is not the fake agent, a refused or « real agents » runtime: refused;
  - REAL processes, no window: a stand-in « browser » (python) starts a « claude.exe » (a copy of PING.EXE) — the watch
    kills that tree by process id and records why; the same stand-in starting the configured fake agent is left alone.
  - STATIC (test_static_launches): no test script may start the built browser without agent_guard (see that section).
Everything is written under build/agent-guard-test; nothing else is touched. Exit code 0 = all passed.
"""
from __future__ import annotations

import hashlib
import importlib.util
import os
import pathlib
import re
import shutil
import subprocess
import sys
import time

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
import agent_guard as G   # noqa: E402

OUT = ROOT / 'build' / 'agent-guard-test'
NO_WINDOW = 0x08000000
failures: list = []
checks = 0


def check(ok, label, detail=''):
    global checks
    checks += 1
    if not ok:
        failures.append(label)
        print(f'FAIL {label}' + (f' — {detail}' if detail else ''), flush=True)
    return bool(ok)


def raises(fn, needle=''):
    """True when fn() raises AgentGuardError (whose message holds `needle`)."""
    try:
        fn()
    except G.AgentGuardError as e:
        return needle.lower() in e.message.lower()
    return False


def fake_file(folder: pathlib.Path, name=G.FAKE_NAME, size=1024) -> pathlib.Path:
    folder.mkdir(parents=True, exist_ok=True)
    p = folder / name
    with open(p, 'wb') as f:
        f.truncate(size)
    return p


def spec(p, *args) -> str:
    return ' '.join([f'"{p}"', *args])


# ---------------------------------------------------------------- before the launch
def test_fake_validation():
    good = fake_file(OUT / 'tools')
    missing = OUT / 'not-built' / G.FAKE_NAME
    check(G.validate_fake(str(good)) == os.path.abspath(good), 'a fake agent of build/ is accepted')
    check(G.validate_fake(str(good), expected=good) == os.path.abspath(good), 'the expected fake agent is accepted')
    check(raises(lambda: G.validate_fake(str(missing)), 'not found'), 'a missing fake agent is refused')
    check(raises(lambda: G.validate_fake(G.FAKE_NAME), 'absolute'), 'a relative fake agent is refused')
    check(raises(lambda: G.validate_fake(''), 'no program'), 'an empty fake agent is refused')
    check(raises(lambda: G.validate_fake(str(fake_file(OUT / 'tools', 'claude.exe'))), G.FAKE_NAME), 'claude.exe named as the fake agent is refused')
    check(raises(lambda: G.validate_fake(str(fake_file(OUT / 'big', size=G.FAKE_MAX_BYTES + 1))), 'too big'), 'a program too big to be the fake agent is refused')
    other = fake_file(OUT / 'other')
    check(raises(lambda: G.validate_fake(str(good), expected=other), 'expects'), 'another fake agent than the expected one is refused')
    if G.REAL_PROFILE:
        foreign = os.path.join(G.REAL_PROFILE, 'Documents', G.FAKE_NAME)   # never created: refused for where it is, or for being absent
        check(raises(lambda: G.validate_fake(foreign)), 'a fake agent outside build/ is refused')
    # parsed as the browser parses it (CommandLineToArgvW)
    argv = G.split_command_line(spec(good, '--resume', '--log', '"C:\\a b\\x.log"'))
    check(argv[:2] == [str(good), '--resume'] and argv[3] == 'C:\\a b\\x.log', 'the fake agent line is split as the browser splits it', argv)
    check(G.fake_path_of({'BROWSER_CHAT_FAKE_AGENT': spec(good, '--resume')}) == os.path.abspath(good), 'fake_path_of names the program')
    check(G.fake_path_of({'BROWSER_CHAT_FAKE_AGENT': 'fake-acp-agent.exe'}) is None and G.fake_path_of({}) is None, 'no absolute program: no fake path')


def test_secure_env():
    work = OUT / 'work'
    good = fake_file(OUT / 'tools')
    real = {'PATH': os.environ.get('PATH', ''), 'USERPROFILE': G.REAL_PROFILE, 'APPDATA': G.REAL_ROAMING, 'LOCALAPPDATA': G.REAL_LOCAL,
            'SystemRoot': os.environ.get('SystemRoot', 'C:\\Windows'), 'TEMP': os.environ.get('TEMP', ''),
            'ANTHROPIC_API_KEY': 'sk-test', 'ANTHROPIC_AUTH_TOKEN': 't', 'OPENAI_API_KEY': 'sk-test', 'CLAUDE_CONFIG_DIR': 'x',
            'CLAUDECODE': '1', 'CLAUDE_CODE_ENTRYPOINT': 'cli', 'CODEX_HOME': 'y', 'GH_TOKEN': 't', 'SOME_SERVICE_API_KEY': 'k',
            'AWS_BEARER_TOKEN_BEDROCK': 'b', 'WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS': '--remote-debugging-port=1',
            'BROWSER_CHAT_FAKE_AGENT': spec(good, '--resume')}
    env = G.secure_env(real, work=work, exe=OUT / 'app' / 'browser.exe', fake=good)
    for k in ('ANTHROPIC_API_KEY', 'ANTHROPIC_AUTH_TOKEN', 'OPENAI_API_KEY', 'CLAUDECODE', 'CLAUDE_CODE_ENTRYPOINT', 'GH_TOKEN',
              'SOME_SERVICE_API_KEY', 'AWS_BEARER_TOKEN_BEDROCK'):
        check(k not in env, f'{k} is stripped from the test browser\'s environment')
    home = (work / 'agent-home').resolve()
    check(env['USERPROFILE'] == str(home) and env['HOME'] == str(home), 'USERPROFILE / HOME: an empty home under the work folder', env['USERPROFILE'])
    check(G.inside(env['APPDATA'], home) and G.inside(env['LOCALAPPDATA'], home), 'APPDATA / LOCALAPPDATA: under that home')
    check(env['CLAUDE_CONFIG_DIR'] == str(home / '.claude') and env['CODEX_HOME'] == str(home / '.codex'), 'CLAUDE_CONFIG_DIR / CODEX_HOME: throwaway folders')
    check(os.path.isdir(env['CLAUDE_CONFIG_DIR']) and not os.listdir(env['CLAUDE_CONFIG_DIR']), 'the throwaway Claude folder exists and is empty')
    paths = [p for p in env['PATH'].split(';') if p]
    check(all(p.lower().startswith(os.environ.get('SystemRoot', 'C:\\Windows').lower()) for p in paths), 'PATH: Windows only (no claude / codex / node)', env['PATH'])
    check(env['BROWSER_ANNOTATIONS_TEST'] == '1', 'the browser\'s test switch is on')
    check(G.inside(env['BROWSER_ANNOTATIONS_CONNECT_ROOT'], work.resolve()), 'the annotation « Connecter » writes under the work folder')
    check(env['WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS'] == real['WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS'], 'the harness\'s own settings are kept')
    check('sk-test' not in repr(env), 'no key value anywhere in the environment')
    # a harness that already redirected a folder under build/ keeps it (tests/onboarding/qa.py: LOCALAPPDATA = its install root)
    mine = OUT / 'onboarding' / 'LOCALAPPDATA'; mine.mkdir(parents=True, exist_ok=True)
    env2 = G.secure_env(dict(real, LOCALAPPDATA=str(mine)), work=work, fake=good)
    check(env2['LOCALAPPDATA'] == str(mine) and env2['USERPROFILE'] == str(home), 'a folder already redirected under build/ is kept, the others are redirected')
    # refusals, before anything starts
    check(raises(lambda: G.secure_env(dict(real, BROWSER_CHAT_FAKE_AGENT=spec(OUT / 'nope' / G.FAKE_NAME)), work=work), 'not found'), 'secure_env: missing fake agent -> refused')
    check(raises(lambda: G.secure_env({k: v for k, v in real.items() if k != 'BROWSER_CHAT_FAKE_AGENT'}, work=work), 'no fake agent'), 'secure_env: no fake agent named -> refused')
    check(raises(lambda: G.secure_env(dict(real, BROWSER_CHAT_REAL_AGENT='1'), work=work), 'BROWSER_CHAT_REAL_AGENT'), 'secure_env: a test asking for real agents -> refused')
    if G.REAL_INSTALL_ROOT:
        installed = os.path.join(G.REAL_INSTALL_ROOT, 'versions', '1.0.0', 'browser.exe')
        check(raises(lambda: G.secure_env(real, work=work, exe=installed), 'installed browser'), 'secure_env: the installed browser (owner\'s profile) -> refused')
    if G.REAL_PROFILE:
        check(raises(lambda: G.secure_env(real, work=os.path.join(G.REAL_PROFILE, 'work')), 'work folder'), 'secure_env: a work folder outside build/ -> refused')
        check(raises(lambda: G.secure_env(dict(real, BROWSER_ANNOTATIONS_CONNECT_ROOT=G.REAL_ROAMING), work=work), 'CONNECT_ROOT'), 'secure_env: the real configuration folders -> refused')
    # no agent at all: the fake agent is removed, the browser refuses every agent in its test mode
    env3 = G.secure_env(real, work=work, agent=False)
    check('BROWSER_CHAT_FAKE_AGENT' not in env3 and env3['BROWSER_ANNOTATIONS_TEST'] == '1', 'agent=False: no fake agent, test switch on (the browser refuses every agent)')


def test_real_opt_in():
    name, value = G.REAL_OPT_IN
    saved = os.environ.pop(name, None)
    try:
        check(raises(lambda: G.real_env({'X': '1'}), name), 'the real agent without the person\'s opt-in -> refused')
        os.environ[name] = 'yes'
        check(raises(lambda: G.real_env({'X': '1'}), name), 'the real agent with a wrong opt-in value -> refused')
        os.environ[name] = value
        import contextlib, io
        with contextlib.redirect_stderr(io.StringIO()) as said:
            env = G.real_env({'BROWSER_CHAT_FAKE_AGENT': 'x'})
        check('REAL agent' in said.getvalue(), 'the real agent by hand is said loudly')
        check(env.get('BROWSER_CHAT_REAL_AGENT') == '1' and 'BROWSER_CHAT_FAKE_AGENT' not in env, 'the real agent by hand: asked by name to the browser')
    finally:
        os.environ.pop(name, None)
        if saved is not None:
            os.environ[name] = saved


def test_chat_harness_refuses_before_popen():
    """tests/chat/chat_qa.py launch(): a missing fake agent stops it before anything is copied or started."""
    sys.path.insert(0, str(ROOT / 'tests' / 'chat'))
    try:
        import chat_qa as C
    except Exception as e:   # noqa: BLE001
        check(False, 'tests/chat/chat_qa.py imports without a window', repr(e)); return
    calls = []
    real_popen = C.subprocess.Popen

    def spy(*a, **k):
        calls.append(a[0] if a else k.get('args'))
        raise AssertionError('Popen reached')
    C.subprocess.Popen = spy
    copied = []
    real_copytree = C.shutil.copytree
    C.shutil.copytree = lambda *a, **k: copied.append(a[0])
    try:
        missing = OUT / 'not-built' / G.FAKE_NAME
        check(raises(lambda: C.launch(extra_env={'BROWSER_CHAT_FAKE_AGENT': spec(missing, '--resume')}), 'not found'),
              'chat_qa.launch with a missing fake agent -> AgentGuardError')
        check(raises(lambda: C.launch(extra_env={'BROWSER_CHAT_FAKE_AGENT': 'fake-acp-agent.exe --resume'}), 'absolute'),
              'chat_qa.launch with a relative fake agent -> AgentGuardError')
        name, _value = G.REAL_OPT_IN
        saved = os.environ.pop(name, None)
        try:
            check(raises(lambda: C.launch(real=True), name), 'chat_qa.launch(real=True) without the person\'s opt-in -> AgentGuardError')
        finally:
            if saved is not None:
                os.environ[name] = saved
        check(not calls, 'no process was started (Popen never called)', calls)
        check(not copied, 'nothing was copied before the refusal', copied)
    finally:
        C.subprocess.Popen = real_popen
        C.shutil.copytree = real_copytree


# ---------------------------------------------------------------- after the launch
def test_classify_and_log():
    fake = str(OUT / 'tools' / G.FAKE_NAME)
    check(G.classify(G.FAKE_NAME, fake, fake) is None, 'the configured fake agent is allowed')
    check(G.classify(G.FAKE_NAME, str(OUT / 'x' / G.FAKE_NAME), fake) is not None, 'a fake agent from another path is caught')
    for name in ('claude.exe', 'Claude.EXE', 'codex.exe', 'codex-x86_64-pc-windows-msvc.exe', 'node.exe', 'npx.cmd', 'bun.exe', 'claude-code.exe'):
        check(G.classify(name, 'C:\\somewhere\\' + name, fake) is not None, f'{name} is caught')
    for name in ('msedgewebview2.exe', 'browser-annotations-mcp.exe', 'conhost.exe', 'WerFault.exe', 'browser.exe', 'nodemon-like.dll'):
        check(G.classify(name, 'C:\\Windows\\' + name, fake) is None, f'{name} is left alone')
    if G.REAL_PROFILE:
        check(G.classify('helper.exe', os.path.join(G.REAL_PROFILE, '.local', 'bin', 'helper.exe'), fake) is not None, 'a program from the real agent folders is caught')
    ok_log = f'CHAT agent runtime (test): fake agent "{fake}"\nCHAT agent launch (test): "{fake}"\nCHAT session starting: claude in C:\\x\n'
    launches, problems = G.scan_log(ok_log, fake)
    check(launches == [fake] and not problems, 'a log of the fake agent only passes', problems)
    real_claude = 'D:\\agents\\.local\\bin\\claude.exe'
    for bad, label in ((f'CHAT agent launch (test): "{real_claude}"', 'a launch of another program'),
                       ('CHAT agent runtime (test): every agent refused: BROWSER_CHAT_FAKE_AGENT names no program found', 'a refused runtime when an agent is expected'),
                       ("CHAT agent runtime (test): this computer's agents (BROWSER_CHAT_REAL_AGENT=1)", 'the computer\'s agents'),
                       (f'CHAT agent runtime (test): fake agent "{real_claude}"', 'another fake agent')):
        check(G.scan_log(bad, fake)[1], f'log: {label} is caught')
    check(not G.scan_log('CHAT agent runtime (test): every agent refused: x', None, expect_agent=False)[1], 'log: refused is fine when the test needs no agent')
    log = OUT / 'prototype.log'
    log.write_text(ok_log, encoding='utf-8')
    check(G.check_log(log, fake, require_launch=True) == [fake], 'check_log: the fake agent by its full path')
    log.write_text('CHAT panel opened\n', encoding='utf-8')
    check(raises(lambda: G.check_log(log, fake, require_launch=True), 'no agent launch'), 'check_log: no launch when one is required -> refused')
    log.write_text(ok_log + f'CHAT agent launch (test): "{real_claude}"\n', encoding='utf-8')
    check(raises(lambda: G.check_log(log, fake), 'not the fake agent'), 'check_log: another program -> refused')
    check(G.check_log(log, fake, since=len(ok_log.encode('utf-8')) + 10**6) == [], 'check_log reads from the given offset')


def stand_in(child: pathlib.Path) -> subprocess.Popen:
    """A « browser » (python, no window) that starts `child` (no window) and waits."""
    code = ('import subprocess, sys, time\n'
            'p = subprocess.Popen([sys.argv[1], "-n", "60", "127.0.0.1"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=0x08000000)\n'
            'time.sleep(60)\n')
    return subprocess.Popen([sys.executable, '-I', '-c', code, str(child)], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL, creationflags=NO_WINDOW)


def wait_child(p, name, timeout=10.0):
    end = time.time() + timeout
    while time.time() < end:
        kids = [d for d in G.descendants(p.pid) if d[1].lower() == name.lower()]
        if kids:
            return kids
        time.sleep(0.1)
    return []


def test_real_processes():
    ping = pathlib.Path(os.environ.get('SystemRoot', 'C:\\Windows')) / 'System32' / 'PING.EXE'
    if not check(ping.is_file(), 'PING.EXE is there (stand-in for an agent program)'):
        return
    bins = OUT / 'bin'; tools = OUT / 'tools'
    bins.mkdir(parents=True, exist_ok=True); tools.mkdir(parents=True, exist_ok=True)
    claude = bins / 'claude.exe'; fake = tools / G.FAKE_NAME
    shutil.copy2(ping, claude); shutil.copy2(ping, fake)
    work = OUT / 'watch'; work.mkdir(parents=True, exist_ok=True); G.clear_marker(work)
    # 1. a « claude.exe » under the browser: caught, the tree killed by process id, the marker written
    p = stand_in(claude)
    try:
        kids = wait_child(p, 'claude.exe')
        check(bool(kids), 'the stand-in browser started its claude.exe', kids)
        w = G.Watch(p.pid, fake=str(fake), work=work, interrupt=False, quiet=True).start()
        end = time.time() + 5
        while time.time() < end and not w.caught:
            time.sleep(0.05)
        check(bool(w.caught) and 'claude.exe' in w.caught[0]['why'], 'the watch caught claude.exe', w.caught)
        try:
            p.wait(5)
        except subprocess.TimeoutExpired:
            pass
        check(p.poll() is not None, 'the stand-in browser was ended')
        check(all(G._created_and_image(pid)[0] is None for pid, _n, _i in kids), 'its claude.exe was ended too')
        check(raises(w.check), 'Watch.check() raises once something was caught')
        check(raises(lambda: G.check_marker(work), 'stopped by the agent guard'), 'the marker stops the next step of the test')
        w.stop()
    finally:
        if p.poll() is None:
            G.kill_tree(p.pid)
    G.clear_marker(work)
    # 2. the configured fake agent under the browser: left alone
    p = stand_in(fake)
    try:
        kids = wait_child(p, G.FAKE_NAME)
        check(bool(kids), 'the stand-in browser started the fake agent', kids)
        w = G.Watch(p.pid, fake=str(fake), work=work, interrupt=False, quiet=True).start()
        time.sleep(1.5)
        check(not w.caught and p.poll() is None, 'the configured fake agent is left running', w.caught)
        w.stop()
    finally:
        G.kill_tree(p.pid)
        try:
            p.wait(5)
        except subprocess.TimeoutExpired:
            pass
    check(p.poll() is not None and not (work / G.MARKER).exists(), 'cleaned up by process id, no marker')
    # 3. the same fake agent but configured elsewhere: caught
    p = stand_in(fake)
    try:
        wait_child(p, G.FAKE_NAME)
        w = G.Watch(p.pid, fake=str(OUT / 'elsewhere' / G.FAKE_NAME), work=work, interrupt=False, quiet=True).start()
        end = time.time() + 5
        while time.time() < end and not w.caught:
            time.sleep(0.05)
        check(bool(w.caught) and 'not the configured one' in w.caught[0]['why'], 'a fake agent from another path is caught', w.caught)
        w.stop()
    finally:
        if p.poll() is None:
            G.kill_tree(p.pid)
    G.clear_marker(work)
    # 4. the log: a launch line that is not the fake agent, written while the browser runs, is caught
    p = stand_in(fake)
    log = OUT / 'watch.log'; log.write_text('CHAT panel opened\n', encoding='utf-8')
    try:
        wait_child(p, G.FAKE_NAME)
        w = G.Watch(p.pid, fake=str(fake), log=log, work=work, interrupt=False, quiet=True).start()
        with open(log, 'a', encoding='utf-8') as f:
            f.write('CHAT agent launch (test): "C:\\elsewhere\\claude.exe"\n')
        end = time.time() + 5
        while time.time() < end and not w.caught:
            time.sleep(0.05)
        check(bool(w.caught) and 'not the fake agent' in w.caught[0]['why'], 'a launch in the log that is not the fake agent is caught', w.caught)
        w.stop()
    finally:
        if p.poll() is None:
            G.kill_tree(p.pid)
    G.clear_marker(work)


# ---------------------------------------------------------------- static: no test script starts the browser unguarded
# Why a sealed or redirected environment is not enough (2026-11-26: the download harnesses' browsers, each start, logged
# « AGENTS claude: ready 2.1.330 » / « AGENTS codex: ready 0.166.0 »): without BROWSER_ANNOTATIONS_TEST the agent runtime
# rebuilds the user's real environment from the process token (src/agent-runtime.cpp Environment::FromProcess ->
# freshUserEnvironment; src/acp-client.cpp BuildEnvironmentBlock -> CreateEnvironmentBlock): USERPROFILE, APPDATA and PATH
# come from the registry, so a sealed or redirected environment is ignored by design, and this computer's Claude Code /
# Codex are found and version-probed. Only the browser's test switch closes that way: every launch goes through
# agent_guard (secure_env / real_env, or a harness's guarded_env built on it; Use-AgentGuardBrowser in PowerShell).
#
# A script is a LAUNCHER when it names the built browser (browser.exe / Lisière.exe) and can start a process (Popen,
# subprocess.run / call / check_*, CreateProcess, os.startfile / spawn / system, ShellExecute; Start-Process,
# [Diagnostics.Process]::Start, the call operator &). A launcher must call the guard. Scanned: tests/**/*.py,
# tests/**/*.ps1 and the test*.ps1 scripts at the root (tests/agent-guard/ itself apart). Bare WebView2 hosts and native
# *_test.cpp never name the browser's exe: not concerned.
#   NEVER_STARTS  names the browser but never starts it: listed with the written reason, never red.
#   PENDING       unguarded on main on 2026-11-26, to be guarded by its owners (path -> content id, one line): printed at
#                 every run. Red: a launcher that is not listed (a new one), a listed one whose content changed (guard it
#                 in that change and remove its entry), a listed one now guarded or gone (remove its entry).
LAUNCH_EXE = re.compile(r'(?i)\b(browser|lisi[eè]re)\.exe\b')
PY_START = re.compile(r'\b(subprocess\.(Popen|run|call|check_call|check_output)|Popen\(|CreateProcess[AW]?\b|os\.startfile|os\.spawn\w*|ShellExecute\w*|os\.system)')
PS_START = re.compile(r'(?im)(Start-Process|\[(System\.)?Diagnostics\.Process\]::Start|CreateProcess|^\s*&\s|[|;(]\s*&\s|Invoke-Expression)')
PY_GUARD = re.compile(r'\b(secure_env|real_env|guarded_env)\s*\(')
PS_GUARD = re.compile(r'\bUse-AgentGuardBrowser\b')
NEVER_STARTS = {
    'tests/annotation-general/run.py': 'runs the annotation suites (each guards its own browser) and watches them with agent_guard; never starts browser.exe itself',
    'tests/e2e/language_concurrent_e2e.py': 'starts the browser only through tests/e2e/language_e2e.py start() (guarded there, or listed there)',
    'tests/update-core/run_tests.py': 'the browser.exe there is a fake program (fake-0.3.2.exe) copied into an update payload',
    'tests/update-integration/e2e_install.py': 'installs and checks files; the real browser.exe is never started (its docstring)',
    'tests/update-integration/old_validator.py': 'validates packages; never runs browser.exe or the bridge (its docstring)',
    'tests/update-integration/run_all.py': 'compiles and runs native fixtures; never launches browser.exe (its docstring)',
    'tests/update-integration/test_release_tools.py': 'packaging tools on synthetic files; browser.exe is only a file name there',
}
PENDING = {
    'test-e2e-pin.ps1': ('cba54796132c457c', 'starts the browser through e2e_pin_test.exe (call operator &), not through agent_guard'),
    'tests/annotations/profile_color_e2e.py': ('1c17a1d714a249a7', 'starts the browser with subprocess.Popen, sets BROWSER_ANNOTATIONS_TEST itself, not through agent_guard'),
    'tests/chat/chat_qa_lot_r.py': ('4261d17241cfd55d', 'starts the browser with subprocess.Popen / subprocess.run, sets BROWSER_ANNOTATIONS_TEST itself, not through agent_guard'),
    'tests/chat/final/c1_hidden_desktop.py': ('bc3df4662c50a7b4', 'starts the browser with subprocess.Popen / subprocess.run, sets BROWSER_ANNOTATIONS_TEST itself, not through agent_guard'),
    'tests/crash-screenshare/window_qa.py': ('273178548228b9a8', 'starts the browser with subprocess.run / CreateProcessW, not through agent_guard'),
    'tests/downloads/morph_flash_qa.py': ('9df64c1160b5dd1d', 'starts the browser with subprocess.Popen, not through agent_guard'),
    'tests/e2e/about_entry_e2e.py': ('4e5f29ba0e2b4872', 'starts the browser with subprocess.Popen / subprocess.run, not through agent_guard'),
    'tests/e2e/adblock_update_e2e.py': ('ce521bedd896a858', 'starts the browser with subprocess.Popen, not through agent_guard'),
    'tests/e2e/address_selection_e2e.ps1': ('5661f55f6eac4362', 'starts the browser with Start-Process, not through agent_guard'),
    'tests/e2e/autofill_qa.ps1': ('b639fa55cbb4dac3', 'starts the browser with Start-Process / the call operator &, not through agent_guard'),
    'tests/e2e/b07_download_e2e.ps1': ('58f06c8e1d097432', 'starts the browser with Start-Process, not through agent_guard'),
    'tests/e2e/bookmark_dnd_e2e.ps1': ('eb6d7e80579ae834', 'starts the browser with Start-Process / the call operator &, not through agent_guard'),
    'tests/e2e/bookmark_rename_e2e.ps1': ('51f1c7cefbc4afa0', 'starts the browser with Start-Process, not through agent_guard'),
    'tests/e2e/browser_import_e2e.ps1': ('49c077ee77d9facc', 'starts the browser with Start-Process / the call operator &, not through agent_guard'),
    'tests/e2e/dpi_follow_e2e.ps1': ('7beca493f8850005', 'starts the browser with Start-Process, not through agent_guard'),
    'tests/e2e/fullscreen_e2e.py': ('44d14638525abdca', 'starts the browser with subprocess.Popen / subprocess.run, not through agent_guard'),
    'tests/e2e/fullscreen_steps.py': ('f0d4936335d55a4c', 'starts the browser with CreateProcessW, not through agent_guard'),
    'tests/e2e/http_status_e2e.ps1': ('8bea05995088f1df', 'starts the browser with Start-Process, not through agent_guard'),
    'tests/e2e/language_e2e.py': ('5a2e25da33ecfbd1', 'starts the browser with subprocess.Popen / subprocess.run, not through agent_guard'),
    'tests/e2e/mtls_e2e.ps1': ('5d333c429603a71f', 'starts the browser with Start-Process / the call operator &, not through agent_guard'),
    'tests/e2e/open_links_e2e.py': ('8f276d781152f747', 'starts the browser with subprocess.run, not through agent_guard'),
    'tests/e2e/permission_help_e2e.ps1': ('fccefa35498008c3', 'starts the browser with Start-Process, not through agent_guard'),
    'tests/e2e/permission_repeat_e2e.ps1': ('064591650fc550c8', 'starts the browser with Start-Process, not through agent_guard'),
    'tests/e2e/private_isolation_e2e.ps1': ('2f51166a82622126', 'starts the browser with Start-Process, not through agent_guard'),
    'tests/e2e/product_markers.ps1': ('9a5a1dc9a3f97bb0', 'starts the browser with Start-Process, not through agent_guard'),
    'tests/e2e/profiles_switch_e2e.ps1': ('a6517a1774922e68', 'starts the browser with Start-Process / the call operator &, not through agent_guard'),
    'tests/e2e/release_notes_e2e.py': ('542f21c7a4a0c866', 'starts the browser with subprocess.Popen / subprocess.run, not through agent_guard'),
    'tests/e2e/select_all_e2e.ps1': ('bcdd37c598794a0d', 'starts the browser with Start-Process, not through agent_guard'),
    'tests/e2e/taskbar_identity_e2e.py': ('f2c64a5c8f543b96', 'starts the browser with subprocess.Popen / subprocess.run, not through agent_guard'),
    'tests/e2e/tls_page_e2e.ps1': ('7e7cb6ab5f8ba487', 'starts the browser with Start-Process / the call operator &, not through agent_guard'),
    'tests/e2e/vault_coverage_e2e.ps1': ('828b6cb79eb6aa01', 'starts the browser with Start-Process / the call operator &, not through agent_guard'),
    'tests/e2e/vault_e2e.ps1': ('746efc99b7344bf5', 'starts the browser with Start-Process / the call operator &, not through agent_guard'),
    'tests/e2e/vault_product_e2e.ps1': ('d5d2a295f587a630', 'starts the browser with Start-Process / the call operator &, not through agent_guard'),
    'tests/e2e/vault_review_e2e.ps1': ('2c35be75991ee62f', 'starts the browser with Start-Process / the call operator &, not through agent_guard'),
    'tests/e2e/windows_e2e.py': ('ef30ba5d396ca2e8', 'starts the browser with subprocess.Popen, not through agent_guard'),
    'tests/error-pages/window_qa.py': ('efdc32462b09b776', 'starts the browser with subprocess.Popen / subprocess.run, not through agent_guard'),
    'tests/focus/annotation_focus_journey.py': ('4530830308c4a14e', 'starts the browser with subprocess.Popen, sets BROWSER_ANNOTATIONS_TEST itself, not through agent_guard'),
    'tests/focus/banner_shortcut_qa.py': ('ce6c4067adf02439', 'starts the browser with CreateProcessW, sets BROWSER_ANNOTATIONS_TEST itself, not through agent_guard'),
    'tests/focus/edge_zone_slot.py': ('e0caa8da649f01d6', 'starts the browser with subprocess.Popen, not through agent_guard'),
    'tests/onboarding/offline_prepare.py': ('bee48f96810043c2', 'starts the browser with subprocess.run / CreateProcessW, not through agent_guard'),
    'tests/popup/popup_lifecycle_e2e.py': ('451c61022a3e4842', 'starts the browser with subprocess.Popen, not through agent_guard'),
    'tests/popup/popup_target_e2e.py': ('391a62ddeee75d76', 'starts the browser with subprocess.Popen / subprocess.run, not through agent_guard'),
    'tests/print/print_qa.py': ('1beac2d0d47a90fe', 'starts the browser with subprocess.Popen / subprocess.run, not through agent_guard'),
    'tests/restore-first-load/run.py': ('4a2cfea60b0579d3', 'starts the browser with CreateProcessW, not through agent_guard'),
    'tests/setup/e2e_real.py': ('21a3ce43403922ab', 'its setup.exe installs and starts the browser (subprocess.Popen), not through agent_guard'),
    'tests/setup/run_tests.py': ('7f64ceaeff0c4d9b', 'starts the browser with subprocess.Popen / subprocess.run, not through agent_guard'),
    'tests/tooltips/qa.ps1': ('b0372af026b874bd', 'starts the browser with Start-Process, not through agent_guard'),
    'tests/ui/edit_flicker_qa.py': ('4bf4608f82137975', 'starts the browser with subprocess.Popen, not through agent_guard'),
    'tests/ui/title_flash_qa.py': ('3d6c2474cb4c6196', 'starts the browser with subprocess.Popen, not through agent_guard'),
    'tests/update-integration/release_candidate_no_bridge.py': ('fa1739347e11ff8f', 'starts the browser with subprocess.Popen, sets BROWSER_ANNOTATIONS_TEST itself, not through agent_guard'),
}


def content_id(data: bytes) -> str:
    """A script's content, line endings aside (a checkout with CRLF and one with LF give the same id)."""
    return hashlib.sha256(data.replace(b'\r\n', b'\n')).hexdigest()[:16]


def launcher_verdict(rel: str, text: str) -> str | None:
    """None: not a launcher; 'guarded' / 'unguarded'."""
    if rel.startswith('tests/agent-guard/') or not LAUNCH_EXE.search(text):
        return None
    py = rel.endswith('.py')
    if not (PY_START if py else PS_START).search(text):
        return None
    return 'guarded' if (PY_GUARD if py else PS_GUARD).search(text) else 'unguarded'


def test_scripts(root: pathlib.Path) -> list:
    """The scripts scanned, relative paths with forward slashes (git's list when there is one, else the tree)."""
    r = subprocess.run(['git', '-C', str(root), 'ls-files'], capture_output=True, text=True, encoding='utf-8', errors='replace')
    names = r.stdout.splitlines() if r.returncode == 0 and r.stdout.strip() else \
        [p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file()]
    return sorted(n for n in names if (n.startswith('tests/') and n.endswith(('.py', '.ps1'))) or re.fullmatch(r'test[^/]*\.ps1', n))


def static_problems(root: pathlib.Path, never=None, pending=None) -> tuple:
    """(problems, pending scripts still unguarded) for the tree at `root`."""
    never = NEVER_STARTS if never is None else never
    pending = PENDING if pending is None else pending
    problems, waiting, seen = [], [], set()
    for rel in test_scripts(root):
        path = root / rel
        try:
            data = path.read_bytes()
        except OSError:
            continue
        verdict = launcher_verdict(rel, data.decode('utf-8', 'replace'))
        if rel in pending:
            seen.add(rel)
        if verdict is None or rel in never:
            continue
        if verdict == 'guarded':
            if rel in pending:
                problems.append(f'{rel}: now goes through agent_guard: remove its PENDING entry')
            continue
        if rel not in pending:
            problems.append(f'{rel}: starts the browser without agent_guard (secure_env / real_env, Use-AgentGuardBrowser): a new launcher must be guarded')
        elif content_id(data) != pending[rel][0]:
            problems.append(f'{rel}: changed while listed as unguarded: guard its browser launch in this change and remove its PENDING entry')
        else:
            waiting.append(rel)
    for rel in sorted(set(pending) - seen):
        problems.append(f'{rel}: listed in PENDING but no longer in the repository: remove its entry')
    return problems, waiting


def test_static_launches(root: pathlib.Path = ROOT):
    # the rule on made-up scripts first (what it catches, what it lets through)
    cases = {
        ('tests/x/new_e2e.py', "subprocess.Popen([str(APP / 'browser.exe')], env=env)"): 'unguarded',
        ('tests/x/ok_e2e.py', "env = G.secure_env(env, work=W, agent=False)\nsubprocess.Popen([str(APP / 'browser.exe')], env=env)"): 'guarded',
        ('tests/x/harness.py', "env = dn.guarded_env(env, app)\nk32.CreateProcessW(str(app / 'browser.exe'), None)"): 'guarded',
        ('tests/x/host_only.py', "subprocess.run([str(OUT / 'probe_host.exe')])"): None,
        ('tests/x/names_only.py', "shutil.copy2(src / 'browser.exe', app)"): None,
        ('tests/x/qa.ps1', "$p = Start-Process \"$app\\browser.exe\" -PassThru"): 'unguarded',
        ('tests/x/qa2.ps1', '. "$PSScriptRoot\\..\\agent-guard\\agent-guard.ps1"\nUse-AgentGuardBrowser { Start-Process "$app\\Browser.exe" }'): 'guarded',
        ('test-x.ps1', "& $exe -ArgumentList x   # $exe = Join-Path $fix 'browser.exe'"): 'unguarded',
    }
    for (rel, text), want in cases.items():
        check(launcher_verdict(rel, text) == want, f'static rule: {rel} -> {want}', launcher_verdict(rel, text))
    # the bookkeeping on a throwaway tree: a new launcher, a listed one changed, a listed one guarded, a listed one gone
    tree = OUT / 'static-tree'
    shutil.rmtree(tree, ignore_errors=True)
    for rel, text in (('tests/a/listed.py', "subprocess.Popen(['browser.exe'])\n"), ('tests/a/changed.py', "subprocess.Popen(['browser.exe'])\n# edited\n"),
                      ('tests/a/fixed.py', "env = secure_env(env, work=W, agent=False)\nsubprocess.Popen(['browser.exe'], env=env)\n"),
                      ('tests/a/new.py', "subprocess.Popen(['browser.exe'])\n"), ('tests/a/never.py', "subprocess.run(['browser.exe'])\n")):
        (tree / rel).parent.mkdir(parents=True, exist_ok=True); (tree / rel).write_text(text, encoding='utf-8')
    listed_id = content_id(b"subprocess.Popen(['browser.exe'])\n")
    pend = {'tests/a/listed.py': (listed_id, 'x'), 'tests/a/changed.py': (listed_id, 'x'), 'tests/a/fixed.py': ('0', 'x'), 'tests/a/gone.py': ('0', 'x')}
    problems, waiting = static_problems(tree, never={'tests/a/never.py': 'never starts it'}, pending=pend)
    said = '\n'.join(problems)
    check(waiting == ['tests/a/listed.py'], 'static: a listed launcher left as it was stays pending (printed, not red)', waiting)
    check('tests/a/new.py: starts the browser without agent_guard' in said, 'static: a NEW unguarded launcher is red', said)
    check('tests/a/changed.py: changed while listed' in said, 'static: a listed launcher whose content changed is red', said)
    check('tests/a/fixed.py: now goes through agent_guard' in said, 'static: a listed launcher now guarded is red until its entry goes', said)
    check('tests/a/gone.py: listed in PENDING but no longer' in said, 'static: a listed launcher that is gone is red until its entry goes', said)
    check('never.py' not in said, 'static: a script that never starts the browser, listed with its reason, is not red', said)
    check(len(problems) == 4, 'static: nothing else is red on the throwaway tree', problems)
    # the repository
    problems, waiting = static_problems(root)
    for p in problems:
        check(False, 'static: ' + p)
    print(f'     static: {len(waiting)} test script(s) still start the browser WITHOUT agent_guard (PENDING, to be guarded by their owners):', flush=True)
    for rel in waiting:
        print(f'       - {rel}: {PENDING[rel][1]}', flush=True)


def test_download_harnesses_refuse_unguarded():
    """The two download harnesses refuse a launch whose environment lacks the guard, before any process exists."""
    def load(name):
        spec_ = importlib.util.spec_from_file_location(name, ROOT / 'tests' / 'downloads' / f'{name}.py')
        mod = importlib.util.module_from_spec(spec_); spec_.loader.exec_module(mod); return mod
    try:
        hd = load('held_download_e2e')
    except Exception as e:   # noqa: BLE001
        check(False, 'tests/downloads/held_download_e2e.py imports without a window', repr(e)); return
    dn = hd.dn
    check(raises(lambda: dn.launch_guard(dict(os.environ, BROWSER_ANNOTATIONS_TEST='')), 'without the agent guard'), 'download harness: an environment without the test switch is refused')
    check(raises(lambda: dn.launch_guard({'BROWSER_ANNOTATIONS_TEST': '1', 'BROWSER_CHAT_REAL_AGENT': '1'})), 'download harness: an environment asking for real agents is refused')
    app = OUT / 'downloads-app'; app.mkdir(parents=True, exist_ok=True)
    env = dn.guarded_env({'PATH': os.environ.get('PATH', ''), 'SystemRoot': os.environ.get('SystemRoot', 'C:\\Windows')}, app)
    check(env.get('BROWSER_ANNOTATIONS_TEST') == '1' and 'BROWSER_CHAT_FAKE_AGENT' not in env, 'download harness: guarded_env turns the switch on, names no agent')

    class Spy:   # a desktop whose start() records instead of starting anything
        def __init__(self): self.calls = []
        def start(self, *a, **k): self.calls.append(a); raise AssertionError('a process start was reached')
    spy = Spy(); real_sealed = hd.sealed_env
    hd.sealed_env = lambda *a, **k: {'PATH': 'C:\\Windows'}   # as if a change dropped the guard from the environment
    try:
        check(raises(lambda: hd.Browser(app, 'http://127.0.0.1:9', app, spy), 'without the agent guard'), 'held_download_e2e: a browser environment without the guard -> refused')
    finally:
        hd.sealed_env = real_sealed
    check(not spy.calls, 'held_download_e2e: no process was started', spy.calls)
    started = []

    def popen_spy(*a, **k):
        started.append(a); raise AssertionError('Popen reached')
    real_popen = dn.subprocess.Popen
    real_guarded = dn.guarded_env
    dn.subprocess.Popen = popen_spy
    dn.guarded_env = lambda env_, app_: {k: v for k, v in env_.items() if k != 'BROWSER_ANNOTATIONS_TEST'}   # as if a change dropped the guard
    try:
        check(raises(lambda: dn.Browser(app, 'http://127.0.0.1:9', app, False, None), 'without the agent guard'), 'download_navigation_e2e: a browser environment without the guard -> refused')
    finally:
        dn.subprocess.Popen = real_popen; dn.guarded_env = real_guarded
    check(not started, 'download_navigation_e2e: Popen never called', started)


def main() -> int:
    for s in (sys.stdout, sys.stderr):
        try:
            s.reconfigure(encoding='utf-8')
        except Exception:   # noqa: BLE001
            pass
    shutil.rmtree(OUT, ignore_errors=True); OUT.mkdir(parents=True)
    static_only = '--static' in sys.argv[1:]
    if '--root' in sys.argv[1:]:   # the check on another tree (for instance an export of origin/main)
        root = pathlib.Path(sys.argv[sys.argv.index('--root') + 1]).resolve()
        test_static_launches(root)
        print(f'agent guard (static, {root}): {checks} checks, {len(failures)} failures')
        return 1 if failures else 0
    tests = (test_static_launches, test_download_harnesses_refuse_unguarded) if static_only else \
        (test_fake_validation, test_secure_env, test_real_opt_in, test_chat_harness_refuses_before_popen, test_download_harnesses_refuse_unguarded,
         test_classify_and_log, test_real_processes, test_static_launches)
    for t in tests:
        before = len(failures); t0 = time.time()
        t()
        print(f'{"ok  " if len(failures) == before else "FAIL"} {t.__name__} ({time.time() - t0:.1f} s)', flush=True)
    print(f'agent guard: {checks} checks, {len(failures)} failures')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
