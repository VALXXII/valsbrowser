#!/usr/bin/env python3
"""Runs the repository's existing suites one after the other and records each result (chat V1 validation).

    python tests/chat/run_suites.py [--only name,...]      # -> build/chat-qa/suites.json + build/chat-qa/suites/<name>.log

Each suite is started exactly as its own header says (python -I …, or PowerShell for the test-*.ps1 scripts); each
sets its own guards. Nothing is retried. A suite that does not end within its time limit is stopped (its whole
process tree) and reported as « timeout ».
"""
import json
import os
import pathlib
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / 'build' / 'chat-qa'
PY = sys.executable
PS = ['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File']
SUITES = [
    ('i18n-gate', [PY, 'tools/i18n/check_strings.py'], 300),
    ('chat-units', [PY, '-I', 'tests/chat/run_tests.py'], 600),
    ('chat-snapshots', [PY, '-I', 'tests/snapshots/run_tests.py'], 1800),
    ('chat-acp', [PY, '-I', 'tests/acp/run_tests.py', '--quick'], 1800),
    ('chat-panel', [PY, '-I', 'tests/chat-panel/run.py'], 1800),
    ('annotations', [PY, '-I', 'tests/annotations/run_tests.py'], 3600),
    ('annotations-ui', [PY, 'tests/annotations-ui/run.py'], 1800),
    ('smoke (test.ps1)', PS + ['test.ps1'], 1800),
    ('shortcuts', PS + ['test-shortcuts.ps1'], 1200),
    ('annotation-settings', PS + ['test-annotation-settings.ps1'], 1200),
    ('accessibility', PS + ['test-accessibility.ps1'], 1800),
    ('split-view', PS + ['test-split-view.ps1'], 1800),
    ('ui', PS + ['test-ui.ps1'], 1800),
    ('design-system', PS + ['test-design-system.ps1'], 1200),
    ('elements-integration', PS + ['test-elements-integration.ps1'], 1800),
    ('profiles', PS + ['test-profiles.ps1'], 1800),
    ('profiles-ui', PS + ['test-profiles-ui.ps1'], 1800),
    ('profile-store', PS + ['test-profile-store.ps1'], 1200),
    ('bookmarks-ui', PS + ['test-bookmarks-ui.ps1'], 1800),
    ('history', PS + ['test-history.ps1'], 1200),
    ('downloads', PS + ['test-downloads.ps1'], 1200),
    ('audio', PS + ['test-audio.ps1'], 1200),
    ('popups', PS + ['test-popups.ps1'], 1800),
    ('navigation-errors', PS + ['test-navigation-errors.ps1'], 1200),
    ('password-vault', PS + ['test-password-vault.ps1'], 1200),
    ('browser-import', PS + ['test-browser-import.ps1'], 1200),
    ('status-bubble', PS + ['test-status-bubble.ps1'], 1200),
    ('tab-drag', PS + ['test-tab-drag.ps1'], 1200),
    ('hover-favicon', PS + ['test-hover-favicon.ps1'], 1200),
    ('sandbox', PS + ['test-sandbox.ps1'], 1200),
    ('e2e-pin', PS + ['test-e2e-pin.ps1'], 1200),
]


def main() -> int:
    only = set()
    if '--only' in sys.argv:
        only = set(sys.argv[sys.argv.index('--only') + 1].split(','))
    logs = OUT / 'suites'
    logs.mkdir(parents=True, exist_ok=True)
    results_file = OUT / 'suites.json'
    results = json.loads(results_file.read_text(encoding='utf-8')) if results_file.exists() and only else {}
    env = dict(os.environ)   # each suite sets its own guards (BROWSER_ANNOTATIONS_TEST would switch tests/annotations' connection tests to their « test mode » branch)
    env.pop('BROWSER_ANNOTATIONS_TEST', None)
    # agent guard (tests/agent-guard/agent_guard.py): no API key or agent credential, no switch to this computer's agents
    sys.path.insert(0, str(ROOT / 'tests' / 'agent-guard'))
    import agent_guard as G
    env, _stripped = G.scrub(env, keep_locations=True)
    for k in (G.REAL_AGENT_SWITCH, G.REAL_OPT_IN[0], 'BROWSER_CHAT_FAKE_AGENT'):
        env.pop(k, None)
    for name, cmd, limit in SUITES:
        if only and name not in only:
            continue
        started = time.time()
        log = logs / (name.replace(' ', '-').replace('(', '').replace(')', '') + '.log')
        with open(log, 'wb') as out:
            p = subprocess.Popen(cmd, cwd=ROOT, stdout=out, stderr=subprocess.STDOUT, env=env)
            try:
                code = p.wait(timeout=limit)
                status = 'pass' if code == 0 else f'fail (exit {code})'
            except subprocess.TimeoutExpired:
                subprocess.run(['taskkill', '/PID', str(p.pid), '/T', '/F'], capture_output=True)
                status = 'timeout'
        results[name] = {'status': status, 'seconds': round(time.time() - started), 'log': str(log.relative_to(ROOT))}
        print(f'{status:16} {name} ({results[name]["seconds"]} s)', flush=True)
        results_file.write_text(json.dumps(results, indent=2), encoding='utf-8')
    return 0 if all(r['status'] == 'pass' for r in results.values()) else 1


if __name__ == '__main__':
    sys.exit(main())
