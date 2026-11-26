#!/usr/bin/env python3
"""The annotation mode's unit fixtures that no annotation runner ran: until now only tests/update-integration/run_all.py
compiled them, and that runner is the installer's (real windows, kept out of the general test). No window, no profile.

    python -I tests/annotation-general/units.py

  annotation_sites_test        where « Annoter » is on (#125, src/annotation-sites.h): local projects and local files on,
                               live sites and LAN addresses off, a private window the same
  annotation_private_test      a private window's notes are erased when it closes, a crashed one's at the next start
  annotation_onboarding_test   the mode's tutorial (#125): flow, first-time rule, its steps drawn OFF SCREEN, keys, languages

Same compiler flags as tests/update-integration/run_all.py (read from it). Every fixture is compiled and run; one that
cannot be compiled (deps/ or the thorvg library missing: setup.ps1, test-lottie.py) is a failure, never a skip.
Outputs: build/annotation-general/units/. Exit code 0 when all passed.
"""
import importlib.util
import pathlib
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / 'build' / 'annotation-general' / 'units'

for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding='utf-8')
    except Exception:
        pass


def flags():
    spec = importlib.util.spec_from_file_location('run_all', ROOT / 'tests' / 'update-integration' / 'run_all.py')
    mod = importlib.util.module_from_spec(spec); spec.loader.exec_module(mod)   # its main() is guarded: nothing runs
    return mod


def main() -> int:
    R = flags(); OUT.mkdir(parents=True, exist_ok=True)
    evidence = OUT / 'annotation-onboarding-render'; evidence.mkdir(parents=True, exist_ok=True)
    fixtures = [
        ('annotation_sites_test', [*R.STRICT, 'tests/annotations/annotation_sites_test.cpp'], []),
        ('annotation_private_test', [*R.STRICT, '-O2', 'tests/annotations/annotation_private_test.cpp', 'src/annotations.cpp', '-lbcrypt'], []),
        ('annotation_onboarding_test', [*R.LOOSE, '-Werror', '-municode', *R.TVG, R.WV2, 'tests/onboarding/annotation_onboarding_test.cpp',
                                        'src/native-lottie.cpp', 'src/agent-install.cpp', 'src/agent-runtime.cpp', 'src/acp-client.cpp', R.THORVG,
                                        *R.UI_LIBS, '-loleaut32', '-lshell32', '-luserenv', '-lwinhttp', '-lbcrypt'], [ROOT, evidence]),
    ]
    failed = []
    for name, compile_args, run_args in fixtures:
        exe = OUT / f'{name}.exe'; t0 = time.time()
        print(f'== compile {name}', flush=True)
        c = subprocess.run([R.GXX, '-std=c++17', '-static', *[str(a) for a in compile_args], '-o', str(exe)], cwd=ROOT)
        if c.returncode != 0:
            print(f'FAIL {name}: does not compile (exit {c.returncode})', flush=True); failed.append(name); continue
        print(f'== run {name}', flush=True)
        r = subprocess.run([str(exe), *[str(a) for a in run_args]], cwd=ROOT)
        print(('PASS ' if r.returncode == 0 else f'FAIL (exit {r.returncode}) ') + f'{name} ({time.time() - t0:.1f} s)', flush=True)
        if r.returncode != 0:
            failed.append(name)
    print(f'{len(fixtures) - len(failed)} / {len(fixtures)} annotation unit fixtures passed' + ('' if not failed else ' — failed: ' + ', '.join(failed)))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
