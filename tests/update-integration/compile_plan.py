"""The suite's one compile pool (tests/update-integration/run_all.py) and the test scripts that join it.

A script that compiles before it runs can hand its compile commands to the pool, then run without compiling:
    <script> --compile-plan <file.json>   its own preparation done (sources from history, build-info.h, ThorVG...), its
                                          compile commands written in their order (label, command, folder); nothing is
                                          compiled, nothing is run
    <script> --prebuilt                   runs as usual without compiling: the pool compiled exactly those commands
Without either option a script compiles and runs as it always did (one command after another).
"""
from __future__ import annotations
import json
from pathlib import Path


def requested(argv: list) -> Path | None:
    """The plan file asked for (--compile-plan <file>), or None."""
    return Path(argv[argv.index('--compile-plan') + 1]) if '--compile-plan' in argv else None


def prebuilt(argv: list) -> bool:
    return '--prebuilt' in argv


def write(path: Path, commands: list) -> int:
    """commands: (label, command, folder) in the order the script would compile them."""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps([{'label': label, 'cmd': [str(c) for c in command], 'cwd': str(cwd)} for label, command, cwd in commands],
                               indent=1), encoding='utf-8')
    print(f'compile plan: {len(commands)} command(s) -> {path}', flush=True)
    return 0


def read(path: Path) -> list:
    return [(c['label'], c['cmd'], c['cwd']) for c in json.loads(path.read_text(encoding='utf-8'))]
