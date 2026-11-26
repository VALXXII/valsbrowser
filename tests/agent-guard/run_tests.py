#!/usr/bin/env python3
"""Windowless tests of the agent guard (tests/agent-guard/agent_guard.py) and of its use by the chat harness.

    python -I tests/agent-guard/run_tests.py

  - a missing / relative / wrongly named / oversized / foreign fake agent: AgentGuardError BEFORE any process starts
    (tests/chat/chat_qa.py launch() is called with subprocess.Popen replaced by a spy: never called, nothing copied);
  - the browser's environment: keys and agent credentials stripped, the « user's » folders and PATH redirected under
    the work folder, the test switch on, BROWSER_CHAT_REAL_AGENT refused, the installed browser refused;
  - the real agent only with the person's own BROWSER_TEST_REAL_AGENT=yes-use-my-account;
  - the browser's log: a launch that is not the fake agent, a refused or « real agents » runtime: refused;
  - REAL processes, no window: a stand-in « browser » (python) starts a « claude.exe » (a copy of PING.EXE) — the watch
    kills that tree by process id and records why; the same stand-in starting the configured fake agent is left alone.
Everything is written under build/agent-guard-test; nothing else is touched. Exit code 0 = all passed.
"""
from __future__ import annotations

import os
import pathlib
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


def main() -> int:
    for s in (sys.stdout, sys.stderr):
        try:
            s.reconfigure(encoding='utf-8')
        except Exception:   # noqa: BLE001
            pass
    shutil.rmtree(OUT, ignore_errors=True); OUT.mkdir(parents=True)
    for t in (test_fake_validation, test_secure_env, test_real_opt_in, test_chat_harness_refuses_before_popen, test_classify_and_log, test_real_processes):
        before = len(failures); t0 = time.time()
        t()
        print(f'{"ok  " if len(failures) == before else "FAIL"} {t.__name__} ({time.time() - t0:.1f} s)', flush=True)
    print(f'agent guard: {checks} checks, {len(failures)} failures')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
