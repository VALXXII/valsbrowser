"""Agent guard for every test that starts browser.exe: the FAKE agent (tests/acp/fake_agent.cpp) or no agent at all —
never this computer's own Claude Code / Codex, never the owner's account, profile, keys or folders.

Incident of 25 Nov 2026: a test script named a fake agent that did not exist; the browser fell back to the real Claude
Code found on PATH and a small request went out with the owner's account. Three layers since:

  1. the browser itself (src/agent-runtime.cpp, under BROWSER_ANNOTATIONS_TEST only): a fake agent that is not there, or
     none named, refuses EVERY agent — no fall back, no CLI looked for, run or downloaded. Its log says
     « CHAT agent runtime (test): fake agent "<full path>" » (or « … every agent refused: <why> ») and
     « CHAT agent launch (test): "<program>" » at each start;
  2. before the launch, secure_env(): the fake agent named by BROWSER_CHAT_FAKE_AGENT exists, by an absolute path, is a
     fake-acp-agent.exe of this repository's build/ folder (or %TEMP%) and the one the harness expects; the test switch
     is on; API keys and agent credentials are stripped (ANTHROPIC_*, OPENAI_*, CLAUDE*, CODEX*, *_API_KEY, tokens…);
     the « user's » folders of the browser (USERPROFILE, HOME, APPDATA, LOCALAPPDATA, PATH, CLAUDE_CONFIG_DIR,
     CODEX_HOME) point to an empty home under the harness's work folder; the browser.exe started is a copy under build/
     or %TEMP%, never the installed browser (%LOCALAPPDATA%\\browser, the owner's profile). Anything else raises
     AgentGuardError BEFORE any process exists;
  3. after the launch, Watch: a thread looks at the browser's descendants every 0.2 s and reads its log. A claude /
     codex / node / npx / bun… process, a fake-acp-agent.exe from another path, an agent launch in the log that is not
     the fake agent, or a browser that refused every agent (misconfigured test) kills THIS browser's process tree (by
     process id: our own processes only), writes <work>/agent-guard-violation.json and stops the test
     (KeyboardInterrupt in the main thread; AgentGuardError at the next check()). check_log() does the same reading
     at the end of a test.

The real agent, on purpose (tests/chat/chat_qa_real.py, tests/chat/chat_qa.py launch --real, tests/acp/run_tests.py
--real, sandbox/chat/run-sandbox.py without --fake-agent): real_env(), only when the person at the keyboard set
BROWSER_TEST_REAL_AGENT=yes-use-my-account in their own shell — never a script, never a night run.

Windowless unit tests: python -I tests/agent-guard/run_tests.py
Detached watch (for a harness whose launcher process exits while the browser keeps running, as chat_qa.py launch):
    python -I tests/agent-guard/agent_guard.py watch --pid <browser pid> --work <folder> [--fake <exe>] [--log <log>]
"""
from __future__ import annotations

import _thread
import ctypes
import ctypes.wintypes as wt
import json
import os
import pathlib
import re
import subprocess
import sys
import threading
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
BUILD = ROOT / 'build'
FAKE_NAME = 'fake-acp-agent.exe'
FAKE_MAX_BYTES = 16 << 20            # the fake agent is a small console program; a real CLI or node.exe is 80-250 MB
REAL_OPT_IN = ('BROWSER_TEST_REAL_AGENT', 'yes-use-my-account')
MARKER = 'agent-guard-violation.json'

# Names stripped from a test browser's environment: AI providers' keys, agent configuration and credentials, tokens.
SECRET_PREFIXES = ('ANTHROPIC', 'OPENAI', 'CLAUDE', 'CODEX', 'AZURE_OPENAI', 'GEMINI', 'GOOGLE_GENAI', 'GOOGLE_API_KEY',
                   'GOOGLE_APPLICATION_CREDENTIALS', 'VERTEX', 'MISTRAL', 'OPENROUTER', 'GROQ', 'XAI_', 'DEEPSEEK',
                   'PERPLEXITY', 'COHERE', 'TOGETHER_', 'FIREWORKS', 'AWS_BEARER_TOKEN', 'BEDROCK', 'GH_TOKEN', 'GITHUB_TOKEN',
                   'GH_ENTERPRISE_TOKEN', 'NPM_TOKEN', 'NODE_AUTH_TOKEN', 'HF_TOKEN', 'HUGGING')
SECRET_SUFFIXES = ('_API_KEY', '_APIKEY', '_AUTH_TOKEN', '_ACCESS_TOKEN', '_OAUTH_TOKEN', '_SESSION_TOKEN', '_REFRESH_TOKEN',
                   '_SECRET', '_SECRET_KEY', '_CLIENT_SECRET', '_CREDENTIALS', '_PASSWORD')
# A test never asks the browser for this computer's agents (src/agent-runtime.cpp): only real_env() sets it.
REAL_AGENT_SWITCH = 'BROWSER_CHAT_REAL_AGENT'

# Processes that are an agent (or what starts one): never a descendant of a test browser. The fake agent is checked by path.
AGENT_PROCESS = re.compile(r'^(claude|codex|node|nodejs|npx|npm|pnpm|pnpx|yarn|bun|bunx|deno|uv|uvx|gemini|aider|goose|'
                           r'opencode|qwen|amp|cursor-agent|copilot)([-_. ][^\\/]*)?\.(exe|cmd|bat|ps1|com)$', re.I)
LOG_LAUNCH = re.compile(r'CHAT agent launch \(test\): "(.*)"\s*$')
LOG_FAKE = re.compile(r'CHAT agent runtime \(test\): fake agent "(.*)"\s*$')
LOG_REFUSED = 'CHAT agent runtime (test): every agent refused'
LOG_REAL = "CHAT agent runtime (test): this computer's agents"
LOG_LAUNCH_REFUSED = 'CHAT agent launch refused (test)'
# The browser's agent states (src/agent-install.cpp: « AGENTS <cli>: <state> [version] »): ready / installing / signing
# in, or a prepare / download, mean it found or set up an agent of THIS computer (2026-11-26: « AGENTS claude: ready
# 2.1.330 » at every start of browsers launched without BROWSER_ANNOTATIONS_TEST). A test browser with no agent logs only
# « notInstalled ». Not part of scan_log: a real trial by hand (real_env) logs them on purpose.
AGENTS_ACTIVE = re.compile(r'^AGENTS [^:\r\n]+: (ready|installing|needsSignIn|signingIn)\b|^AGENTS .*(prepar|download)', re.I | re.M)


def agents_active(text: str) -> list:
    """The « AGENTS … » lines of a log that show an agent of this computer found or set up."""
    return [line.strip() for line in text.splitlines() if AGENTS_ACTIVE.search(line)]


def check_agents_quiet(log, since: int = 0) -> None:
    """A browser that must use no agent: its log (from byte `since`) has no « AGENTS … ready / prepare » line."""
    try:
        with open(log, 'rb') as f:
            f.seek(since); text = f.read().decode('utf-8', 'replace')
    except OSError:
        text = ''
    found = agents_active(text)
    if found:
        raise AgentGuardError(f'the browser found or prepared an agent of this computer ({log}): ' + ' | '.join(found[:3]))


class AgentGuardError(SystemExit):
    """Raised before anything unsafe starts, or once the watch caught an agent that is not the fake one. A SystemExit:
    a test step's `except Exception` never swallows it, and the script exits with status 1 and this message."""

    def __init__(self, message: str):
        super().__init__('AGENT GUARD: ' + message)
        self.message = message


# ---------------------------------------------------------------- Windows helpers (ctypes, no window, no PowerShell)
k32 = ctypes.WinDLL('kernel32', use_last_error=True)
shell32 = ctypes.WinDLL('shell32', use_last_error=True)
ole32 = ctypes.WinDLL('ole32')
k32.OpenProcess.restype = wt.HANDLE
k32.OpenProcess.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
k32.CloseHandle.argtypes = [wt.HANDLE]
k32.TerminateProcess.argtypes = [wt.HANDLE, wt.UINT]
k32.WaitForSingleObject.argtypes = [wt.HANDLE, wt.DWORD]
k32.WaitForSingleObject.restype = wt.DWORD
k32.GetProcessTimes.argtypes = [wt.HANDLE] + [ctypes.POINTER(wt.FILETIME)] * 4
k32.QueryFullProcessImageNameW.argtypes = [wt.HANDLE, wt.DWORD, wt.LPWSTR, ctypes.POINTER(wt.DWORD)]
k32.CreateToolhelp32Snapshot.restype = wt.HANDLE
k32.CreateToolhelp32Snapshot.argtypes = [wt.DWORD, wt.DWORD]
k32.LocalFree.argtypes = [wt.HLOCAL]
shell32.CommandLineToArgvW.restype = ctypes.POINTER(wt.LPWSTR)
shell32.CommandLineToArgvW.argtypes = [wt.LPCWSTR, ctypes.POINTER(ctypes.c_int)]
SYNCHRONIZE, PROCESS_TERMINATE, PROCESS_QUERY_LIMITED_INFORMATION = 0x00100000, 0x0001, 0x1000
INVALID_HANDLE = wt.HANDLE(-1).value


class PROCESSENTRY32W(ctypes.Structure):
    _fields_ = [('dwSize', wt.DWORD), ('cntUsage', wt.DWORD), ('th32ProcessID', wt.DWORD), ('th32DefaultHeapID', ctypes.c_size_t),
                ('th32ModuleID', wt.DWORD), ('cntThreads', wt.DWORD), ('th32ParentProcessID', wt.DWORD), ('pcPriClassBase', ctypes.c_long),
                ('dwFlags', wt.DWORD), ('szExeFile', ctypes.c_wchar * 260)]


k32.Process32FirstW.argtypes = k32.Process32NextW.argtypes = [wt.HANDLE, ctypes.POINTER(PROCESSENTRY32W)]


class GUID(ctypes.Structure):
    _fields_ = [('a', wt.DWORD), ('b', wt.WORD), ('c', wt.WORD), ('d', ctypes.c_ubyte * 8)]


def _known_folder(a, b, c, d) -> str:
    """A Windows known folder as Windows has it — never from the environment variables a test redirects."""
    g = GUID(a, b, c, (ctypes.c_ubyte * 8)(*d)); p = ctypes.c_wchar_p()
    try:
        if shell32.SHGetKnownFolderPath(ctypes.byref(g), 0x00004000, None, ctypes.byref(p)) == 0 and p.value:   # KF_FLAG_DONT_VERIFY
            return p.value
    finally:
        ole32.CoTaskMemFree(p)
    return ''


REAL_PROFILE = _known_folder(0x5E6C858F, 0x0E22, 0x4760, (0x9A, 0xFE, 0xEA, 0x33, 0x17, 0xB6, 0x71, 0x73))
REAL_ROAMING = _known_folder(0x3EB685DB, 0x65F9, 0x4CF6, (0xA0, 0x3A, 0xE3, 0xEF, 0x65, 0x72, 0x9F, 0x3D))
REAL_LOCAL = _known_folder(0xF1B32785, 0x6FBA, 0x4FCF, (0x9D, 0x55, 0x7B, 0x8E, 0x7F, 0x15, 0x70, 0x91))
# Where the real browser keeps its installs and the owner's profile, and where agents' CLIs are installed.
REAL_INSTALL_ROOT = os.path.join(REAL_LOCAL, 'browser') if REAL_LOCAL else ''
REAL_AGENT_DIRS = [p for p in (
    REAL_PROFILE and os.path.join(REAL_PROFILE, '.local', 'bin'), REAL_PROFILE and os.path.join(REAL_PROFILE, '.claude'),
    REAL_PROFILE and os.path.join(REAL_PROFILE, '.codex'), REAL_ROAMING and os.path.join(REAL_ROAMING, 'npm'),
    REAL_ROAMING and os.path.join(REAL_ROAMING, 'Claude'), REAL_LOCAL and os.path.join(REAL_LOCAL, 'Programs', 'OpenAI'),
    REAL_LOCAL and os.path.join(REAL_LOCAL, 'Microsoft', 'WinGet'), REAL_LOCAL and os.path.join(REAL_LOCAL, 'AnthropicClaude'),
    REAL_LOCAL and os.path.join(REAL_LOCAL, 'Programs', 'claude')) if p]


def _norm(path) -> str:
    return os.path.normcase(os.path.abspath(str(path)))


def same_path(a, b) -> bool:
    return bool(a) and bool(b) and _norm(a) == _norm(b)


def inside(path, root) -> bool:
    """`path` is `root` or under it (no file system access: lexical, after abspath)."""
    if not path or not root:
        return False
    p, r = _norm(path), _norm(root).rstrip('\\/')
    return p == r or p.startswith(r + os.sep)


def safe_roots() -> list:
    """Where a test's browser, fake agent, home and data may live: this repository's build/ folder, or %TEMP%."""
    roots = [str(BUILD)]
    for v in ('TEMP', 'TMP'):
        t = os.environ.get(v)
        if t and not inside(REAL_INSTALL_ROOT, t):
            roots.append(t)
    return roots


def in_safe_root(path) -> bool:
    return any(inside(path, r) for r in safe_roots()) and not inside(path, REAL_INSTALL_ROOT)


def split_command_line(spec: str) -> list:
    """BROWSER_CHAT_FAKE_AGENT as the browser reads it (CommandLineToArgvW, src/agent-runtime.cpp)."""
    if not spec or not spec.strip():
        return []
    n = ctypes.c_int(0)
    argv = shell32.CommandLineToArgvW(spec, ctypes.byref(n))
    if not argv:
        return []
    try:
        return [argv[i] for i in range(n.value)]
    finally:
        k32.LocalFree(ctypes.cast(argv, wt.HLOCAL))


def fake_path_of(env: dict):
    """The fake agent's program BROWSER_CHAT_FAKE_AGENT names (absolute, normalised), or None."""
    argv = split_command_line(env.get('BROWSER_CHAT_FAKE_AGENT', ''))
    return os.path.abspath(argv[0]) if argv and argv[0] and os.path.isabs(argv[0]) else None


# ---------------------------------------------------------------- layer 2: before the launch
def is_secret(name: str) -> bool:
    n = name.upper()
    return n.startswith(SECRET_PREFIXES) or n.endswith(SECRET_SUFFIXES)


def scrub(env: dict, keep_locations: bool = False) -> tuple:
    """(env without keys, credentials and agent configuration, the names removed). keep_locations: CLAUDE_CONFIG_DIR and
    CODEX_HOME stay (a runner's own environment: its suites fingerprint the REAL configuration folders through them)."""
    def gone(k):
        return is_secret(k) and not (keep_locations and k.upper() in ('CLAUDE_CONFIG_DIR', 'CODEX_HOME'))
    removed = sorted(k for k in env if gone(k))
    return {k: v for k, v in env.items() if not gone(k)}, removed


def runner_verdict(name: str, path) -> str | None:
    """For a runner watching whole suites (tests/annotation-general/run.py), which does not know the fake agent each
    suite names: a test's own programs under build/ (or %TEMP%) — the fake agent, the fake claude.exe / codex.exe of
    tests/acp and tests/annotations — are allowed; an agent program from anywhere else is not."""
    if path is None:
        return None
    n = (name or '').lower()
    if (n == FAKE_NAME or AGENT_PROCESS.match(n)) and in_safe_root(path):
        return None
    return classify(name, path, None)


def validate_fake(program, expected=None) -> str:
    """The fake agent's program, checked: absolute, named fake-acp-agent.exe, a small file that exists under build/ (or
    %TEMP%), and `expected` when the harness says which one. Returns its absolute path; raises AgentGuardError."""
    if not program:
        raise AgentGuardError('BROWSER_CHAT_FAKE_AGENT names no program')
    p = str(program)
    if not os.path.isabs(p):
        raise AgentGuardError(f'the fake agent must be named by an absolute path, not "{p}"')
    p = os.path.abspath(p)
    if os.path.basename(p).lower() != FAKE_NAME:
        raise AgentGuardError(f'BROWSER_CHAT_FAKE_AGENT must name a {FAKE_NAME}, not "{p}"')
    if not os.path.isfile(p):
        raise AgentGuardError(f'fake agent not found: "{p}" — build it first (tools/release/build.py --preview writes '
                              f'<out>-test/{FAKE_NAME}); nothing was started')
    if os.path.getsize(p) > FAKE_MAX_BYTES:
        raise AgentGuardError(f'"{p}" is {os.path.getsize(p) >> 20} MB: too big to be the fake agent')
    if not in_safe_root(p):
        raise AgentGuardError(f'the fake agent must come from this repository\'s build/ folder (or %TEMP%), not "{p}"')
    if expected is not None and not same_path(p, expected):
        raise AgentGuardError(f'BROWSER_CHAT_FAKE_AGENT names "{p}", the harness expects "{os.path.abspath(str(expected))}"')
    return p


def check_exe(exe) -> str:
    """The browser.exe a test starts: a copy under build/ (or %TEMP%), never the installed browser and its profile."""
    p = os.path.abspath(str(exe))
    if REAL_INSTALL_ROOT and inside(p, REAL_INSTALL_ROOT):
        raise AgentGuardError(f'"{p}" is the installed browser (the owner\'s profile): a test starts a copy under build/')
    if not in_safe_root(p):
        raise AgentGuardError(f'"{p}" is not a test copy (neither under build/ nor under %TEMP%)')
    return p


def _system_path() -> str:
    windir = os.environ.get('SystemRoot') or os.environ.get('windir') or r'C:\Windows'
    return f'{windir}\\System32;{windir}'


def secure_env(env: dict, *, work, exe=None, fake=None, agent: bool = True) -> dict:
    """The environment of a test browser, checked — call it last, on the env passed to Popen.

    env    what the harness built (os.environ plus its own settings, BROWSER_CHAT_FAKE_AGENT included);
    work   the harness's own folder under build/ (or %TEMP%): the empty home is <work>/agent-home;
    exe    the browser.exe about to start (checked: a copy under build/ or %TEMP%, never the installed browser);
    fake   the fake-acp-agent.exe the harness expects BROWSER_CHAT_FAKE_AGENT to name (None: any fake agent of build/);
    agent  False when the test needs no agent at all: BROWSER_CHAT_FAKE_AGENT is removed and the browser, in its test
           mode with no fake agent, refuses every agent.

    Returns a new dict; raises AgentGuardError before anything starts."""
    out, _removed = scrub(dict(env))
    # every check first: when one refuses, nothing has been created or started
    if out.pop(REAL_AGENT_SWITCH, None) is not None:
        raise AgentGuardError(f'{REAL_AGENT_SWITCH} asks the browser for this computer\'s agents: a test never does (real_env() is the only way, by hand)')
    if agent:
        argv = split_command_line(out.get('BROWSER_CHAT_FAKE_AGENT', ''))
        if not argv:
            raise AgentGuardError('no fake agent named (BROWSER_CHAT_FAKE_AGENT): this test would start no agent — or, '
                                  'without the browser\'s guard, the computer\'s own')
        validate_fake(argv[0], expected=fake)
    else:
        out.pop('BROWSER_CHAT_FAKE_AGENT', None)
    if exe is not None:
        check_exe(exe)
    work = pathlib.Path(work).resolve()
    if not in_safe_root(work):
        raise AgentGuardError(f'the work folder "{work}" must be under build/ (or %TEMP%)')
    root = out.get('BROWSER_ANNOTATIONS_CONNECT_ROOT') or str(work / 'agent-configs')
    if not in_safe_root(root):
        raise AgentGuardError(f'BROWSER_ANNOTATIONS_CONNECT_ROOT "{root}" must be under build/ (or %TEMP%)')
    for d in (out.get('BROWSER_ANNOTATIONS_CONNECT_PATH') or '').split(';'):
        if d.strip() and not in_safe_root(d.strip().strip('"')):
            raise AgentGuardError(f'BROWSER_ANNOTATIONS_CONNECT_PATH "{d}" must be under build/ (or %TEMP%): it is where a codex CLI is looked for')
    # the « new user's » computer: no Claude / Codex CLI, configuration or credential of this one can be found
    home = work / 'agent-home'
    for sub in ('AppData/Roaming', 'AppData/Local', '.claude', '.codex'):
        (home / sub).mkdir(parents=True, exist_ok=True)
    for var, mine in (('USERPROFILE', home), ('HOME', home), ('APPDATA', home / 'AppData' / 'Roaming'), ('LOCALAPPDATA', home / 'AppData' / 'Local')):
        if not (out.get(var) and in_safe_root(out[var])):   # a harness that already redirected it under build/ keeps its folder
            out[var] = str(mine)
    out['CLAUDE_CONFIG_DIR'] = str(home / '.claude')
    out['CODEX_HOME'] = str(home / '.codex')
    out['PATH'] = _system_path()
    # the annotation mode's « Connecter » writes only there (src/annotation-mode.h ConnectEnv)
    out['BROWSER_ANNOTATIONS_CONNECT_ROOT'] = root
    for sub in ('AppData/Roaming/Claude', 'AppData/Local', 'home/.codex'):
        (pathlib.Path(root) / sub).mkdir(parents=True, exist_ok=True)
    out['BROWSER_ANNOTATIONS_TEST'] = '1'
    return out


def real_env(env: dict) -> dict:
    """The real agent, on purpose: only when the person at the keyboard set BROWSER_TEST_REAL_AGENT=yes-use-my-account
    in their own shell. The browser's test switch stays on (agent-connect's guard) and is told to use this computer's
    agents (BROWSER_CHAT_REAL_AGENT=1); the home and keys are the user's — that is the point of a real trial."""
    name, value = REAL_OPT_IN
    if os.environ.get(name) != value:
        raise AgentGuardError(f'this run would use the REAL Claude Code / Codex and the owner\'s account. Refused: only a '
                              f'person, by hand, may allow it for one run with {name}={value} in their own shell')
    out = dict(env)
    out.pop('BROWSER_CHAT_FAKE_AGENT', None)
    out['BROWSER_ANNOTATIONS_TEST'] = '1'
    out[REAL_AGENT_SWITCH] = '1'
    print(f'!! AGENT GUARD: the REAL agent and account are used ({name} set by hand) !!', file=sys.stderr, flush=True)
    return out


# ---------------------------------------------------------------- layer 3: after the launch
def classify(name: str, path, fake) -> str | None:
    """Why a descendant of a test browser is not allowed (None: allowed). `path`: its image, or None (gone)."""
    n = (name or '').lower()
    if n == FAKE_NAME:
        if path is None or (fake and same_path(path, fake)):
            return None
        return f'a fake agent that is not the configured one: "{path}" (configured: "{fake}")'
    if AGENT_PROCESS.match(n):
        return f'an agent process: {name} ("{path}")'
    if path and any(inside(path, d) for d in REAL_AGENT_DIRS):
        return f'a program from this computer\'s agent folders: "{path}"'
    return None


def scan_log(text: str, fake, expect_agent: bool = True) -> tuple:
    """(agent launches the log names, problems). Problems: a launch or a runtime line that is not the fake agent, the
    browser using this computer's agents, or — when an agent is expected — a browser that refused every agent."""
    launches, problems = [], []
    for line in text.splitlines():
        m = LOG_LAUNCH.search(line)
        if m:
            launches.append(m.group(1))
            if not fake or not same_path(m.group(1), fake):
                problems.append(f'the browser started "{m.group(1)}", not the fake agent "{fake}"')
            continue
        m = LOG_FAKE.search(line)
        if m and not same_path(m.group(1), fake):
            problems.append(f'the browser was given the fake agent "{m.group(1)}", not "{fake}"')
        elif LOG_REAL in line:
            problems.append('the browser was set to use this computer\'s agents (BROWSER_CHAT_REAL_AGENT)')
        elif expect_agent and (LOG_REFUSED in line or LOG_LAUNCH_REFUSED in line):
            problems.append('the browser refused every agent (its fake agent was not found): ' + line.strip()[:300])
    return launches, problems


def check_log(log, fake, *, since: int = 0, expect_agent: bool = True, require_launch: bool = False) -> list:
    """Reads the browser's log (from byte `since`): every agent it started is the fake agent, by its full path.
    Returns the launches; raises AgentGuardError."""
    try:
        with open(log, 'rb') as f:
            f.seek(since); text = f.read().decode('utf-8', 'replace')
    except OSError:
        text = ''
    launches, problems = scan_log(text, fake, expect_agent)
    if problems:
        raise AgentGuardError('; '.join(problems))
    if require_launch and not launches:
        raise AgentGuardError(f'the log {log} names no agent launch: nothing proves the fake agent ran')
    return launches


def check_marker(work) -> None:
    """A watch (in this process or a detached one) caught something for this work folder: stop here."""
    m = pathlib.Path(work) / MARKER
    if m.is_file():
        raise AgentGuardError(f'a previous step was stopped by the agent guard: {m.read_text(encoding="utf-8", errors="replace")[:800]}')


def clear_marker(work) -> None:
    try:
        (pathlib.Path(work) / MARKER).unlink()
    except OSError:
        pass


def snapshot() -> list:
    """[(pid, parent pid, exe name)] of every process (Toolhelp32: no window, no PowerShell)."""
    h = k32.CreateToolhelp32Snapshot(0x2, 0)   # TH32CS_SNAPPROCESS
    if not h or h == INVALID_HANDLE:
        return []
    out = []
    try:
        e = PROCESSENTRY32W(); e.dwSize = ctypes.sizeof(e)
        ok = k32.Process32FirstW(h, ctypes.byref(e))
        while ok:
            out.append((e.th32ProcessID, e.th32ParentProcessID, e.szExeFile))
            ok = k32.Process32NextW(h, ctypes.byref(e))
    finally:
        k32.CloseHandle(h)
    return out


def _created_and_image(pid: int):
    """(creation time as an int, image path) of a process, or (None, None) when it is gone or not ours to read."""
    h = k32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
    if not h:
        return None, None
    try:
        c, e, kt, ut = wt.FILETIME(), wt.FILETIME(), wt.FILETIME(), wt.FILETIME()
        created = (c.dwHighDateTime << 32 | c.dwLowDateTime) if k32.GetProcessTimes(h, ctypes.byref(c), ctypes.byref(e), ctypes.byref(kt), ctypes.byref(ut)) else None
        buf = ctypes.create_unicode_buffer(32768); n = wt.DWORD(32768)
        image = buf.value if k32.QueryFullProcessImageNameW(h, 0, buf, ctypes.byref(n)) else None
        return created, image
    finally:
        k32.CloseHandle(h)


def descendants(root_pid: int, table=None) -> list:
    """[(pid, name, image)] of every process started (directly or not) by `root_pid`. A child must have been created
    after its parent: a recycled parent id never makes a stranger « ours »."""
    table = table if table is not None else snapshot()
    root_created, _ = _created_and_image(root_pid)
    if root_created is None:
        return []
    out, frontier, seen = [], {root_pid: root_created}, {root_pid}
    while frontier:
        nxt = {}
        for pid, ppid, name in table:
            if ppid in frontier and pid not in seen:
                created, image = _created_and_image(pid)
                if created is None or created < frontier[ppid]:
                    continue
                seen.add(pid); nxt[pid] = created; out.append((pid, name, image))
        frontier = nxt
    return out


def kill_tree(root_pid: int, root_handle=None) -> list:
    """Ends `root_pid` (a process this test started) and everything it started, deepest first, by process id.
    Never by name: other workers run the same programs from their own folders."""
    killed = []
    for pid, name, _image in reversed(descendants(root_pid)):
        h = k32.OpenProcess(PROCESS_TERMINATE, False, pid)
        if h:
            if k32.TerminateProcess(h, 86):
                killed.append(f'{name} {pid}')
            k32.CloseHandle(h)
    h = root_handle or k32.OpenProcess(PROCESS_TERMINATE, False, root_pid)
    if h:
        if k32.TerminateProcess(h, 86):
            killed.append(f'root {root_pid}')
        if not root_handle:
            k32.CloseHandle(h)
    return killed


class Watch:
    """Watches a test browser (started by this test) until it ends: see the module's layer 3.

        w = Watch(p.pid, fake=fake_path_of(env), log=APP / 'prototype.log', work=WORK).start()
        ...; w.check()      # AgentGuardError if something was caught
        w.stop()
    interrupt: also raise KeyboardInterrupt in the main thread when something is caught (default)."""

    def __init__(self, pid: int, *, fake=None, log=None, work=None, expect_agent: bool = True, interrupt: bool = True,
                 every: float = 0.2, quiet: bool = False):
        self.pid, self.fake, self.log, self.work = pid, fake, log, work
        self.expect_agent, self.interrupt, self.every, self.quiet = expect_agent, interrupt, every, quiet
        self.caught, self.killed = [], []
        self._stop = threading.Event(); self._thread = None; self._lock = threading.Lock()
        self._log_at = 0
        try:
            self._log_at = os.path.getsize(log) if log else 0   # only what this launch writes
        except OSError:
            self._log_at = 0
        self._h = k32.OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, False, pid)   # holds the id: never recycled while watched

    def start(self) -> 'Watch':
        self._thread = threading.Thread(target=self.run, name=f'agent-guard {self.pid}', daemon=True)
        self._thread.start()
        return self

    def stop(self) -> None:
        self._stop.set()
        if self._thread and self._thread is not threading.current_thread():
            self._thread.join(2)

    def check(self) -> None:
        if self.caught:
            raise AgentGuardError('; '.join(c['why'] for c in self.caught))

    def tick(self) -> bool:
        """One look. False once the browser has ended."""
        if not self._h or k32.WaitForSingleObject(self._h, 0) == 0:
            return False
        for pid, name, image in descendants(self.pid):
            why = classify(name, image, self.fake)
            if why:
                self._trip(why, {'pid': pid, 'name': name, 'image': image})
                return False
        if self.log:
            try:
                if os.path.getsize(self.log) < self._log_at:   # the browser rotated its log (src/log-privacy.h): from its start
                    self._log_at = 0
                with open(self.log, 'rb') as f:
                    f.seek(self._log_at); data = f.read()
            except OSError:
                data = b''
            cut = data.rfind(b'\n') + 1   # whole lines only
            if cut:
                self._log_at += cut
                _launches, problems = scan_log(data[:cut].decode('utf-8', 'replace'), self.fake, self.expect_agent)
                if problems:
                    self._trip('; '.join(problems), {'log': str(self.log)})
                    return False
        return True

    def run(self) -> None:
        try:
            while not self._stop.is_set() and self.tick():
                self._stop.wait(self.every)
        finally:
            if self._h:
                k32.CloseHandle(self._h); self._h = None

    def _trip(self, why: str, what: dict) -> None:
        with self._lock:
            if self.caught:
                return
            self.killed = kill_tree(self.pid, self._h)
            record = {'why': why, 'what': what, 'browserPid': self.pid, 'killed': self.killed, 'at': time.strftime('%Y-%m-%d %H:%M:%S')}
            self.caught.append(record)
            if self.work:
                try:
                    (pathlib.Path(self.work) / MARKER).write_text(json.dumps(record, indent=2, ensure_ascii=False), encoding='utf-8')
                except OSError:
                    pass
            if not self.quiet:
                print(f'\n!! AGENT GUARD: {why}\n!! this test browser (pid {self.pid}) and what it started were stopped: {", ".join(self.killed) or "already gone"}',
                      file=sys.stderr, flush=True)
            if self.interrupt and threading.current_thread() is not threading.main_thread():
                _thread.interrupt_main()


def watch(pid: int, env: dict, *, log=None, work=None, expect_agent=None, **kw) -> Watch:
    """Starts a Watch for a browser launched with `env` (the dict secure_env returned)."""
    fake = fake_path_of(env)
    if expect_agent is None:
        expect_agent = fake is not None
    return Watch(pid, fake=fake, log=log, work=work, expect_agent=expect_agent, **kw).start()


def spawn_detached_watch(pid: int, env: dict, *, work, log=None) -> int:
    """For a launcher process that exits while the browser runs on (tests/chat/chat_qa.py launch): the same watch, in a
    windowless process of its own that ends with the browser. Returns its process id."""
    cmd = [sys.executable, '-I', str(pathlib.Path(__file__).resolve()), 'watch', '--pid', str(pid), '--work', str(work)]
    fake = fake_path_of(env)
    if fake:
        cmd += ['--fake', fake]
    if log:
        cmd += ['--log', str(log)]
    flags = 0x00000008 | 0x00000200 | 0x08000000   # DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP | CREATE_NO_WINDOW
    return subprocess.Popen(cmd, cwd=str(ROOT), stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                            creationflags=flags, close_fds=True).pid


def main(argv: list) -> int:
    import argparse
    ap = argparse.ArgumentParser(description='Agent guard: watch one test browser until it ends.')
    ap.add_argument('cmd', choices=('watch',))
    ap.add_argument('--pid', type=int, required=True)
    ap.add_argument('--work', required=True)
    ap.add_argument('--fake')
    ap.add_argument('--log')
    a = ap.parse_args(argv)
    w = Watch(a.pid, fake=a.fake or None, log=a.log, work=a.work, expect_agent=bool(a.fake), interrupt=False, quiet=True)
    w.run()
    return 1 if w.caught else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
