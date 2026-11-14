#!/usr/bin/env python3
"""Réglages › « Annotations et agents » — short real run in a COPY of the preview build (build/annotations-qa-app,
its own profile, settings and store), driven through UI Automation only (tests/annotations/uia.ps1, scoped to that
process). Same guards as sandbox_qa_v2.py: BROWSER_ANNOTATIONS_TEST=1, agent configurations redirected to
build/annotations-v2-qa/agent-configs (BROWSER_ANNOTATIONS_CONNECT_ROOT), the REAL %APPDATA%\\Claude and
%USERPROFILE%\\.codex fingerprinted before and after, the browser and the page server closed in a finally.

    python -I tests/annotations/settings_walk.py      # -> build/annotations-v2-qa/settings-walk/{report.json,*.png,uia-*.txt}

Steps: open Réglages, open « Annotations et agents », read the section's UIA tree, turn the launcher off then on
(settings file + log + launcher window), Connecter Claude Desktop (redirected config gets our entry, the row says
Connecté / Déconnecter), Déconnecter (entry removed), close.
"""
from __future__ import annotations
import json
import pathlib
import socket
import subprocess
import sys
import time
import traceback

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import sandbox_qa_v2 as V   # noqa: E402  (launch with guards, fingerprint, pw_shot, quit)

Q = V.Q
ROOT = V.ROOT
OUT = V.QA / 'settings-walk'
report = {'steps': []}


def step(name, ok, detail=''):
    report['steps'].append({'step': name, 'ok': bool(ok), 'detail': detail})
    print(('PASS ' if ok else 'FAIL ') + name + (' — ' + str(detail) if detail else ''), flush=True)
    return ok


def uia(pid, *args):
    r = subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(HERE / 'uia.ps1'), '-ProcessId', str(pid), *args],
                       capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=120)
    return r.returncode, (r.stdout or '') + (r.stderr or '')


def wait_for(pid, name, seconds=15):
    end = time.time() + seconds
    while time.time() < end:
        code, out = uia(pid, '-Name', name)
        if code == 0 and 'FOUND' in out:
            return True
        time.sleep(0.5)
    return False


def listing(pid, tag):
    code, out = uia(pid, '-List')
    (OUT / f'uia-{tag}.txt').write_text(out, encoding='utf-8')
    return out


def settings_text():
    try:
        return (V.APP / 'settings').read_text(encoding='utf-8', errors='replace')
    except OSError:
        return ''


def shot(name):
    time.sleep(0.6)
    try:
        V.QA = OUT; V.pw_shot(name)
    except Exception as e:   # captures are evidence, not the test
        print('shot failed', name, e)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    before = V.fingerprint()
    claude_cfg, codex_cfg = V.prepare_agents()
    server = subprocess.Popen([sys.executable, str(ROOT / 'sandbox/annotations/serve.py')], cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    p = None
    try:
        for _ in range(100):
            try:
                socket.create_connection(('127.0.0.1', 5173), timeout=0.3).close(); break
            except OSError:
                time.sleep(0.1)
        p = V.launch()   # asserts BROWSER_ANNOTATIONS_TEST=1 and a CONNECT_ROOT under build/
        pid = p.pid
        step('preview copy launched with the test guard', p.poll() is None, f'pid {pid}')

        code, out = uia(pid, '-Name', 'Réglages', '-Invoke')
        step('toolbar « Réglages » invoked through UIA', code == 0, out.strip())
        step('« Annotations et agents » row in the root', wait_for(pid, 'Annotations et agents'))
        shot('01-root')
        code, out = uia(pid, '-Name', 'Annotations et agents', '-Invoke')
        ok = wait_for(pid, 'Proposer de connecter un agent')
        step('section opened', code == 0 and ok, out.strip())
        time.sleep(1.5)   # agents' status (read off the UI thread)
        tree = listing(pid, 'section')
        step('UIA: « Connecter Claude Desktop » (redirected Claude folder, no entry yet)', 'Connecter Claude Desktop' in tree)
        step('UIA: Codex row and its button', 'Connecter Codex' in tree or 'Codex :' in tree)
        step('UIA: switch, retention, folder, clear all; no launcher switch (V3)', all(n in tree for n in ('Proposer de connecter un agent', 'Conserver les terminées', 'Ouvrir le dossier des annotations', 'Effacer toutes les annotations…')) and "Afficher le lanceur d'annotations" not in tree)
        shot('02-section')

        log0 = len(V.log_text())
        code, out = uia(pid, '-Name', 'Proposer de connecter un agent', '-Invoke'); time.sleep(1.0)
        log1 = V.log_text()[log0:]
        step('prompt off: settings file (no annotation-launcher key, V3)', 'annotation-connect-prompt=0' in settings_text() and 'annotation-launcher' not in settings_text(), [l for l in settings_text().splitlines() if l.startswith('annotation-')])
        step('prompt off: applied at once (log)', 'SETTINGS annotation connect prompt off' in log1, log1.strip()[-200:])
        shot('03-prompt-off')
        code, out = uia(pid, '-Name', 'Proposer de connecter un agent', '-Invoke'); time.sleep(1.0)
        step('prompt on again: settings file', 'annotation-connect-prompt=1' in settings_text())

        claude_before = claude_cfg.read_text(encoding='utf-8')
        code, out = uia(pid, '-Name', 'Connecter Claude Desktop', '-Invoke')
        step('Connecter Claude Desktop invoked', code == 0, out.strip())
        connected = wait_for(pid, 'Déconnecter Claude Desktop', 20)
        cfg = claude_cfg.read_text(encoding='utf-8')
        step('redirected claude_desktop_config.json has our entry, the rest kept', '"browser-annotations' in cfg and 'autre-serveur' in cfg and 'globalShortcut' in cfg)
        step('row now offers « Déconnecter Claude Desktop »', connected)
        tree = listing(pid, 'claude-connected')
        step('UIA: Claude status says Connecté', 'Claude Desktop : Connecté' in tree)
        shot('04-claude-connected')
        code, out = uia(pid, '-Name', 'Déconnecter Claude Desktop', '-Invoke')
        step('Déconnecter Claude Desktop', wait_for(pid, 'Connecter Claude Desktop', 20) and '"browser-annotations' not in claude_cfg.read_text(encoding='utf-8'))
        step('redirected config back to its original keys', json.loads(claude_cfg.read_text(encoding='utf-8')) == json.loads(claude_before))
        shot('05-claude-disconnected')
        report['log'] = [l for l in V.log_text().splitlines() if 'SETTINGS annotation' in l or 'ANNOTATE connect' in l or 'ANNOTATE disconnect' in l][-20:]
    except Exception:
        step('no exception', False, traceback.format_exc())
    finally:
        if p is not None:
            V.quit_browser(p)
            step('browser closed', p.poll() is not None)
        server.kill()
        after = V.fingerprint()
        step('REAL Claude / Codex configurations unchanged', before == after, '' if before == after else {'before': before, 'after': after})
        report['fingerprint'] = after
        (OUT / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    failed = [s for s in report['steps'] if not s['ok']]
    print(f"{len(report['steps']) - len(failed)}/{len(report['steps'])} steps passed")
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
