"""sandbox/agents/offline_runtime.py without a browser: the pins read, the refusals (fail closed), the seed.

    python -X utf8 -I tests/onboarding/offline_runtime_test.py <build-dir>
The seed copies the pinned node.exe from the local cache (build/agents-runtime-cache, filled from a copy already on this
computer — never the network); a computer without any copy is refused, which is the point."""
import importlib.util, pathlib, shutil, sys, tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'sandbox' / 'agents'))
import offline_runtime as O   # noqa: E402

APP = pathlib.Path(sys.argv[1]).resolve()
checks = []
def check(ok, what):
    checks.append(bool(ok)); print(('PASS ' if ok else 'FAIL ') + what, flush=True)
def refused(fn):
    try: fn()
    except SystemExit as e: return str(e) or True
    return False

p = O.read_pins()
check(p.node['version'] and len(p.node['exeSha256']) == 64 and p.node['exeBytes'] > 10_000_000 and set(p.adapters) == {'claude', 'codex'},
      'the pins are read from src/agent-runtime-pins.inc (Node, both adapters)')
check(not refused(lambda: O.check_build(APP, p)), 'the build holds the same pins (its browser.exe has the pinned strings)')
with tempfile.TemporaryDirectory() as t:
    t = pathlib.Path(t)
    (t / 'other').mkdir(); (t / 'other' / 'browser.exe').write_bytes(b'not the pinned build')
    check(refused(lambda: O.check_build(t / 'other', p)), 'a build with other pins is refused (it would download)')
    check(refused(lambda: O.guard(t / 'empty', APP, p)), 'an empty copy is refused (it would download Node.js and the adapter)')
    node = O.node_exe(t / 'wrong', p); node.parent.mkdir(parents=True); node.write_bytes(b'\0' * 16)
    check(refused(lambda: O.guard(t / 'wrong', APP, p)), 'a node.exe that is not the pinned one is refused (SEC-15 would remove it and download)')
    O.seed_and_guard(t / 'seeded', APP)
    check(O.node_exe(t / 'seeded', p).stat().st_size == p.node['exeBytes'] and all(O.adapter_entry(t / 'seeded', p, a).is_file() for a in ('claude', 'codex'))
          and not O.downloads_in(t / 'seeded'), 'the seed: the pinned node.exe, both adapter entries (stubs), nothing else')
    check(not refused(lambda: O.guard(t / 'seeded', APP, p)), 'the seeded copy passes the guard')
    (O.node_exe(t / 'seeded', p).parent.parent / '.download-x.zip').write_bytes(b'x')
    check(refused(lambda: O.guard(t / 'seeded', APP, p)), 'a download left in the copy is refused')
spec = importlib.util.spec_from_file_location('try_onboarding', ROOT / 'sandbox' / 'annotations' / 'try-onboarding.py')
TRY = importlib.util.module_from_spec(spec); spec.loader.exec_module(TRY)
with tempfile.TemporaryDirectory() as t:
    check(refused(lambda: TRY.agents_environment(pathlib.Path(t) / 'p', 'present', True, 'ok', 0, APP)),
          'try-onboarding: a fake profile without data_root is refused (a harness that forgot it would download)')
    check(TRY.agents_environment(pathlib.Path(t) / 'p', 'real', True, 'ok', 0, APP) == {}, 'try-onboarding: « real » (the owner\'s consented trial) is left as it is')
print(f'{sum(checks)} / {len(checks)} offline runtime checks')
sys.exit(0 if all(checks) else 1)
