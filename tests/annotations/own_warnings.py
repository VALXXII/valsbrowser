#!/usr/bin/env python3
"""V2-C: proves that the annotation integration compiles with 0 warnings on its own, while another worker edits
src/settings.h / src/settings-panel.h in the same worktree. Copies src/ to build/annotations-v2-qa/own-src, puts
back the versions at BASE (default cf9e7c3, before V2-D) of the files owned by the settings worker, and compiles main.cpp with the exact
flags of tools/release/build.py (-O2 -c, stamped dev identity: the annotation mode is compiled in).

    python tests/annotations/own_warnings.py
"""
import pathlib, shutil, subprocess, sys
ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'release'))
import build as B   # noqa: E402

OUT = ROOT / 'build' / 'annotations-v2-qa' / 'own-src'
# The settings files as they were before the settings worker (V2-D, 4e167a0) added Change values that main.cpp
# will only handle once its wiring lands (after V2-C): those 3 -Wswitch warnings are not V2-C's.
BASE = sys.argv[1] if len(sys.argv) > 1 else 'cf9e7c3'
shutil.rmtree(OUT, ignore_errors=True)
shutil.copytree(ROOT / 'src', OUT / 'src')
for f in ('src/settings.h', 'src/settings-panel.h'):
    (OUT / f).write_bytes(subprocess.run(['git', 'show', BASE + ':' + f], cwd=ROOT, capture_output=True, check=True).stdout)
(OUT / 'src' / 'annotation-settings.h').unlink(missing_ok=True)
gen = ROOT / 'build' / 'annotations-preview-obj' / 'gen'
cmd = [str(B.BIN / 'g++.exe'), '-std=c++17', *B.WARNINGS,
       '-municode', '-I' + str(OUT / 'src'), '-I' + str(gen), '-DBROWSER_HAS_BUILD_INFO', '-DBROWSER_PREVIEW',
       '-DTVG_STATIC', '-I' + str(ROOT / 'deps/thorvg-1.1.3/inc'), '-I' + str(B.SDK / 'include'), '-O2', '-c', '-o', str(OUT / 'main.o'), str(OUT / 'src' / 'main.cpp')]
r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
out = r.stdout + r.stderr
warnings = [l for l in out.splitlines() if 'warning:' in l]
errors = [l for l in out.splitlines() if 'error:' in l]
print(out[-3000:])
print(f'main.cpp (annotation mode compiled in, settings files at {BASE}): exit {r.returncode}, {len(warnings)} warnings, {len(errors)} errors')
sys.exit(0 if r.returncode == 0 and not warnings else 1)
