#!/usr/bin/env python3
"""The final pass (design/chat-comment-final-plan.md, phases 3 and 5): ONE suite at a time, nothing retried, each result
recorded; then build/chat-qa/final/report.md.

    python tests/chat/final/run_final.py [--build] [--only name,name] [--seeds 1,2,3]

  --build   python tools/release/build.py --out build/chat-preview --channel dev --preview first (its warnings counted)
  order     the fake agent's scenarios, the repository's suites (tests/chat/run_suites.py: units, ACP, panel, snapshots,
            annotations, annotations-ui, i18n, smoke and the other test-*.ps1), every real-window chat suite (v1 to v5b,
            R43), the user's journeys (journeys.py), the monkey (monkey.py, 3 seeds).
Only when the computer is free (« Libre »). Logs: build/chat-qa/final/runs/<name>.log; results: build/chat-qa/final/runs.json.
"""
from __future__ import annotations
import json
import os
import pathlib
import re
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[3]
OUT = ROOT / 'build' / 'chat-qa' / 'final'
PY = sys.executable
REAL = [('chat-v1 (chat_qa run)', [PY, 'tests/chat/chat_qa.py', 'run'], 2400),
        ('comment', [PY, 'tests/chat/chat_qa_comment.py'], 1800),
        ('comment-switch', [PY, 'tests/chat/chat_qa_comment_switch.py'], 1800),
        ('v12', [PY, 'tests/chat/chat_qa_v12.py'], 1800),
        ('comment-v2', [PY, 'tests/chat/chat_qa_comment_v2.py'], 1800),
        ('comment-v3', [PY, 'tests/chat/chat_qa_comment_v3.py'], 1800),
        ('comment-v4', [PY, 'tests/chat/chat_qa_comment_v4.py'], 1800),
        ('comment-v4b', [PY, 'tests/chat/chat_qa_comment_v4b.py'], 1800),
        ('comment-v5', [PY, 'tests/chat/chat_qa_comment_v5.py'], 1800),
        ('comment-v5b', [PY, 'tests/chat/chat_qa_comment_v5b.py'], 1800),
        ('r43', [PY, 'tests/chat/chat_qa_r43.py'], 900)]


def run(name, cmd, limit, env, results):
    logs = OUT / 'runs'; logs.mkdir(parents=True, exist_ok=True)
    log = logs / (re.sub(r'[^\w.-]+', '-', name).strip('-') + '.log')
    t0 = time.time()
    with open(log, 'wb') as out:
        p = subprocess.Popen(cmd, cwd=ROOT, stdout=out, stderr=subprocess.STDOUT, env=env)
        try:
            code = p.wait(timeout=limit); status = 'pass' if code == 0 else f'fail (exit {code})'
        except subprocess.TimeoutExpired:
            subprocess.run(['taskkill', '/PID', str(p.pid), '/T', '/F'], capture_output=True); status = 'timeout'
    tail = log.read_text(encoding='utf-8', errors='replace').splitlines()[-4:]
    results[name] = {'status': status, 'seconds': round(time.time() - t0), 'log': str(log.relative_to(ROOT)), 'tail': tail}
    print(f'{status:16} {name} ({results[name]["seconds"]} s)', flush=True)
    (OUT / 'runs.json').write_text(json.dumps(results, indent=2, ensure_ascii=False), encoding='utf-8')


def main():
    for s in (sys.stdout, sys.stderr):
        try:
            s.reconfigure(encoding='utf-8')
        except Exception:
            pass
    only = set(sys.argv[sys.argv.index('--only') + 1].split(',')) if '--only' in sys.argv else None
    seeds = sys.argv[sys.argv.index('--seeds') + 1].split(',') if '--seeds' in sys.argv else ['1', '2', '3']
    env = dict(os.environ); env.setdefault('CHAT_QA_BUILD', 'chat-preview'); env.pop('BROWSER_ANNOTATIONS_TEST', None)
    results = json.loads((OUT / 'runs.json').read_text(encoding='utf-8')) if only and (OUT / 'runs.json').exists() else {}
    plan = []
    if '--build' in sys.argv:
        plan.append(('build chat-preview', [PY, 'tools/release/build.py', '--out', 'build/chat-preview', '--channel', 'dev', '--preview'], 3600))
    plan.append(('fake agent scenarios', [PY, '-I', 'tests/chat/final/fake_agent_scenarios.py', '--exe', f'build/{env["CHAT_QA_BUILD"]}-test/fake-acp-agent.exe'], 600))
    plan.append(('repository suites (run_suites.py)', [PY, 'tests/chat/run_suites.py'], 6 * 3600))
    plan += REAL
    plan.append(('journeys', [PY, 'tests/chat/final/journeys.py'], 3 * 3600))
    plan += [(f'monkey seed {s}', [PY, 'tests/chat/final/monkey.py', '--seed', s], 3 * 3600) for s in seeds]
    for name, cmd, limit in plan:
        if only and name not in only and not any(o in name for o in only):
            continue
        run(name, cmd, limit, env, results)
    write_report(results)
    return 0 if all(r['status'] == 'pass' for r in results.values()) else 1


def write_report(results):
    """build/chat-qa/final/report.md from runs.json, the journeys' and monkeys' reports, the build log."""
    lines = ['# QA finale — Mode Commentaire + Chat', '', f'Passe du {time.strftime("%d/%m/%Y %H:%M")}, build `{os.environ.get("CHAT_QA_BUILD", "chat-preview")}`.', '',
             '| Suite | Résultat | Durée | Journal |', '|---|---|---|---|']
    for name, r in results.items():
        lines.append(f'| {name} | {"✅" if r["status"] == "pass" else "❌ " + r["status"]} | {r["seconds"]} s | `{r["log"]}` |')
    build = OUT / 'runs' / 'build-chat-preview.log'
    if build.exists():
        warn = [l for l in build.read_text(encoding='utf-8', errors='replace').splitlines() if re.search(r'warning', l, re.I)]
        lines += ['', f'Build : {len(warn)} avertissement(s).'] + [f'    {w}' for w in warn[:10]]
    j = OUT / 'journeys.json'
    if j.exists():
        data = json.loads(j.read_text(encoding='utf-8'))
        lines += ['', '## Parcours réels', '', '| Parcours | Retours | Étapes | Échecs |', '|---|---|---|---|']
        for name, r in data['results'].items():
            lines.append(f'| {name} | {r["retours"]} | {r["steps"]} | {len(r["failed"])} {"" if not r["failed"] else "— " + "; ".join(r["failed"][:3])} |')
        lines += ['', 'Prouvés par d\'autres suites de la même passe :'] + [f'- {k} : {v}' for k, v in data.get('coveredElsewhere', {}).items()]
    for d in sorted(OUT.glob('monkey-*')):
        rep = d / 'report.json'
        if rep.exists():
            r = json.loads(rep.read_text(encoding='utf-8'))
            lines += ['', f'## Monkey {d.name}', '', f'{len(r.get("done", []))} actions, {r["measures"].get("invariantChecks", 0)} vérifications d\'invariants, '
                      f'{len(r["failures"])} échec(s).']
            for f in r['failures'][:10]:
                lines.append(f'- pas {f.get("index")} ({(f.get("action") or {}).get("kind")}) : {f["step"]} — capture `{f.get("capture")}`, rejouer : `{f.get("replay")}`')
    (OUT / 'report.md').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    print('report:', OUT / 'report.md')


if __name__ == '__main__':
    sys.exit(main())
