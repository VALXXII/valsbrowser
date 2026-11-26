"""The chat agents' private runtime for TESTS, offline: nothing is ever downloaded from nodejs.org or registry.npmjs.org.

With the launch-only fake agent (BROWSER_CHAT_FAKE_AGENT_LAUNCH_ONLY=1) the browser runs its real detection and its real
preparation: a test copy without <data>\\agents downloads the pinned Node.js (~38 MB zip) and the pinned adapter
(~118 MB) at the first session start (« session start: needs_prepare »), then launches the fake agent anyway. That is
what Fix's « present » runs did (25 Nov 2026, 04:56 and 13:11), and « absent » does it too after the fake install and
sign-in.

What the browser checks (src/agent-runtime.cpp), so no download is ever needed:
  NodeReady        <data>\\agents\\node\\<version>\\node.exe has the pinned size
  ResolveLaunch    its SHA-256 is the pinned one (SEC-15, before EVERY launch, the fake one included)
  AdapterReady     <data>\\agents\\adapters\\<id>\\<version>-<treeDigest>\\<entry> exists
So the copy gets the REAL pinned node.exe from a local cache (build/agents-runtime-cache, filled once from a copy already on
this computer: never from the network; its SHA-256 checked) and, for each adapter, a stub entry file (the launch-only
fake agent is launched instead of node + adapter, so the adapter is never run).

    seed_and_guard(data_root, app)   seeds the copy, then refuses (SystemExit) unless no download can be needed
    guard(data_root, app)            the check alone: every pin satisfied, and the build's own pins are the source's
The pins are read from src/agent-runtime-pins.inc and checked against the build (its browser.exe holds the same pinned
strings): a build with other pins is refused, never left to download.
"""
from __future__ import annotations

import glob
import hashlib
import os
import pathlib
import re
import shutil
import tempfile

REPO = pathlib.Path(__file__).resolve().parents[2]
PINS_FILE = REPO / 'src' / 'agent-runtime-pins.inc'
CACHE = REPO / 'build' / 'agents-runtime-cache'
STUB = ('// Offline test stub (sandbox/agents/offline_runtime.py): the launch-only fake agent runs instead of node + this '
        'adapter. If this ever runs, the test set-up is wrong.\nprocess.exit(97);\n')
BLOCKED_HOSTS = ('nodejs.org', 'registry.npmjs.org')


class Pins:
    def __init__(self, node: dict, adapters: dict):
        self.node, self.adapters = node, adapters


def read_pins(path: pathlib.Path = PINS_FILE) -> Pins:
    text = path.read_text(encoding='utf-8')
    block = text[text.index('static const Pins kBuiltInPins = {'):]
    m = re.search(r'\{"(\d+\.\d+\.\d+)", "([^"]+\.zip)",\s*"([0-9a-f]{64})", (\d+)ull,\s*"([0-9a-f]{64})", (\d+)ull\}', block)
    if not m:
        raise SystemExit(f'offline runtime: the Node pin was not found in {path}: refused (the format changed)')
    node = {'version': m.group(1), 'zip': m.group(2), 'zipSha256': m.group(3), 'zipBytes': int(m.group(4)),
            'exeSha256': m.group(5), 'exeBytes': int(m.group(6))}
    adapters = {}
    found = re.findall(r'\{"(@agentclientprotocol/[\w-]+)", "([^"]+)", "([^"]+)",\s*"[^"]*", "[^"]*", \w+, [^}]*?,\s*"([0-9a-f]{8})"\}', block)
    for package, version, entry, digest in found:
        agent = 'claude' if 'claude' in package else 'codex'
        adapters[agent] = {'package': package, 'version': version, 'entry': entry, 'digest': digest}
    if set(adapters) != {'claude', 'codex'}:
        raise SystemExit(f'offline runtime: the adapter pins were not found in {path} ({sorted(adapters)}): refused')
    return Pins(node, adapters)


def sha256(path: pathlib.Path) -> str:
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def node_exe(data_root: pathlib.Path, pins: Pins) -> pathlib.Path:
    return pathlib.Path(data_root) / 'agents' / 'node' / pins.node['version'] / 'node.exe'


def adapter_entry(data_root: pathlib.Path, pins: Pins, agent: str) -> pathlib.Path:
    a = pins.adapters[agent]
    return pathlib.Path(data_root) / 'agents' / 'adapters' / agent / f"{a['version']}-{a['digest']}" / pathlib.PureWindowsPath(a['entry'].replace('/', '\\'))


def _good_node(path: pathlib.Path, pins: Pins) -> bool:
    return path.is_file() and path.stat().st_size == pins.node['exeBytes'] and sha256(path) == pins.node['exeSha256']


def cached_node(pins: Pins) -> pathlib.Path:
    """The pinned node.exe from the local cache; filled once from a copy ALREADY on this computer (a test copy or a trial
    that prepared it earlier), its SHA-256 checked. Never from the network: none found = refused."""
    cache = CACHE / 'node' / pins.node['version'] / 'node.exe'
    if _good_node(cache, pins):
        return cache
    tail = os.path.join('agents', 'node', pins.node['version'], 'node.exe')
    candidates = [os.environ.get('BROWSER_TEST_NODE_EXE', '')]
    candidates += glob.glob(str(REPO / 'build' / '**' / tail), recursive=True)
    candidates += glob.glob(os.path.join(tempfile.gettempdir(), 'browser-essai-*', 'app', tail))
    candidates += glob.glob(str(REPO.parent / '*' / 'build' / '*' / tail))   # the other worktrees' test copies
    for c in candidates:
        p = pathlib.Path(c) if c else None
        if p and p.is_file() and _good_node(p, pins):
            cache.parent.mkdir(parents=True, exist_ok=True)
            tmp = cache.with_suffix('.part')
            shutil.copyfile(p, tmp)
            if not _good_node(tmp, pins):
                tmp.unlink(missing_ok=True); continue
            os.replace(tmp, cache)
            return cache
    raise SystemExit(f"offline runtime: no copy of the pinned node.exe {pins.node['version']} (SHA-256 {pins.node['exeSha256'][:12]}…) on this "
                     f"computer, and a test never downloads it. Put one in {cache} (or set BROWSER_TEST_NODE_EXE): refused.")


def check_build(app: pathlib.Path, pins: Pins) -> None:
    """The build's own pins are the ones read here (else the browser would look for other folders and download)."""
    exe = pathlib.Path(app) / 'browser.exe'
    data = exe.read_bytes()
    need = [pins.node['exeSha256'], pins.node['zip']] + [f"{a['version']}" for a in pins.adapters.values()] + [a['digest'] for a in pins.adapters.values()]
    missing = [s for s in need if s.encode('ascii') not in data]
    if missing:
        raise SystemExit(f'offline runtime: {exe} was built with other pins than {PINS_FILE} ({missing[:3]}): refused (it would download)')


def guard(data_root: pathlib.Path, app: pathlib.Path, pins: Pins | None = None) -> Pins:
    """Refuses (SystemExit) unless the copy needs no download at all: every pin satisfied, the build's pins the same."""
    pins = pins or read_pins()
    check_build(app, pins)
    node = node_exe(data_root, pins)
    if not _good_node(node, pins):
        raise SystemExit(f'offline runtime: {node} is not the pinned node.exe: the browser would download Node.js — refused')
    for agent in ('claude', 'codex'):
        if not adapter_entry(data_root, pins, agent).is_file():
            raise SystemExit(f'offline runtime: the {agent} adapter entry is missing in {data_root}: the browser would download it — refused')
    agents = pathlib.Path(data_root) / 'agents'
    left = [str(p) for p in agents.rglob('.download-*')] + [str(p) for p in agents.rglob('.unpack-*')]
    if left:
        raise SystemExit(f'offline runtime: a download was left in the copy ({left[:2]}): refused')
    return pins


def seed_and_guard(data_root: pathlib.Path, app: pathlib.Path) -> Pins:
    """Puts the pinned node.exe (local cache) and the adapters' stub entries in <data_root>\\agents, then guard()."""
    pins = read_pins()
    check_build(app, pins)
    node = node_exe(data_root, pins)
    if not _good_node(node, pins):
        node.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(cached_node(pins), node)
    for agent in ('claude', 'codex'):
        entry = adapter_entry(data_root, pins, agent)
        if not entry.is_file():
            entry.parent.mkdir(parents=True, exist_ok=True)
            entry.write_text(STUB, encoding='utf-8', newline='\n')
    return guard(data_root, app, pins)


def downloads_in(data_root: pathlib.Path) -> list[str]:
    """What a download would have left in the copy: its temporary files, or an adapter tree beyond the stub."""
    agents = pathlib.Path(data_root) / 'agents'
    out = [str(p) for p in agents.rglob('.download-*')] + [str(p) for p in agents.rglob('.unpack-*')]
    for p in agents.rglob('package.json'):
        out.append(str(p))   # a real adapter tree (the stub has none)
    return out
