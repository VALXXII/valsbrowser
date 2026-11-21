#!/usr/bin/env python3
"""R43 in a real window (fake agent): during a turn the agent's announcements are grey follow-up lines (no card); at its
end they close and join « Historique », only the answer (+ the changes bar) stays. Bubble.

    CHAT_QA_BUILD=chat-cv5b python tests/chat/chat_qa_r43.py   # -> build/chat-qa/r43/report.json + captures
"""
from __future__ import annotations
import json
import subprocess
import sys
import time
import traceback

import chat_qa as C
import chat_qa_comment_v2 as V
import chat_qa_comment_v5b as B

OUT = C.QA / 'r43'
report = {'steps': [], 'measures': {}, 'captures': []}


def step(name, ok, detail=''):
    report['steps'].append({'step': name, 'ok': bool(ok), 'detail': str(detail)})
    print(('PASS ' if ok else 'FAIL ') + name + (' — ' + str(detail) if detail else ''), flush=True)
    return bool(ok)


STATE = ("(()=>{const t=[...document.querySelectorAll('.turn.bt')].pop();if(!t)return null;"
         "const vis=e=>{const r=e.getBoundingClientRect();return r.height>0&&getComputedStyle(e).display!=='none'&&!e.hidden};"
         "const f=[...t.querySelectorAll('.bt-follow')].filter(vis);const cards=[...t.querySelectorAll(':scope > .bt-text')].filter(e=>vis(e)&&!e.classList.contains('bt-follow'));"
         "return {follow:f.length,followText:f.map(e=>e.textContent.trim()).slice(0,3),followCard:f.some(e=>getComputedStyle(e).backgroundColor!=='rgba(0, 0, 0, 0)'),"
         "cards:cards.length,answer:cards.map(e=>e.textContent.trim().slice(0,80)),historique:(document.body.innerText||'').includes('Historique')}})()")


def run_main():
    before = C.fingerprints()
    OUT.mkdir(parents=True, exist_ok=True)
    fake = C.TOOLS / 'fake-acp-agent.exe'; flog = C.WORK / 'fake-agent.log'
    C.launch(extra_env={'BROWSER_CHAT_PANEL_IDLE_MS': '60000', 'BROWSER_CHAT_AGENT_IDLE_MS': '600000',
                        'BROWSER_CHAT_FAKE_AGENT': f'"{fake}" --resume --claude-like --log "{flog}" --edit "{V.GAP_EDIT}"'})
    try:
        time.sleep(1.0)
        C.shortcut('A'); C.wait_log(r'ANNOTATE mode on', 10, 0); time.sleep(1.2)
        if C.invoke('AnnotationConnectLater') or C.invoke('AnnotationConnectClose'):
            time.sleep(0.6)
        V.front(); m = len(C.log_text()); C.invoke('AnnotationLayoutComment'); C.wait_log(r'CHAT layout: Commentaire', 5, m); time.sleep(1.5)
        B.real_click('.btn-primary'); C.wait_log(r'CHAT bubble opened on', 10, 0); time.sleep(1.2)
        m = len(C.log_text()); V.send_in_view('annonce')
        samples = []; t0 = time.time(); shot = False
        while time.time() - t0 < 20:
            s = C.panel(STATE)
            if s:
                samples.append(s)
                if s['follow'] and not shot: B.capture('r43-pendant'); shot = True
            if C.wait_log(r'CHAT turn t\d+ ended', 0.05, m):
                break
        during = [s for s in samples if s['follow']]
        step('R43 · during the turn: the announcements are grey follow-up lines, no card', bool(during) and not any(s['followCard'] for s in during),
             {'samples': len(samples), 'withFollow': len(during), 'first': during[0] if during else (samples[-1] if samples else None)})
        time.sleep(1.2)
        end = C.panel(STATE); B.capture('r43-fin')
        report['measures']['end'] = end
        step('R43 · at the end: the lines are closed, « Historique » shown, the answer stays', end and end['follow'] == 0 and end['historique'] and end['cards'] >= 1, end)
    except Exception:
        traceback.print_exc(); step('run without exception', False, traceback.format_exc()[-400:])
    finally:
        C.quit_all()
        step('real Claude / Codex configuration files unchanged', before == C.fingerprints())
        ps = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq fake-acp-agent.exe', '/NH'], capture_output=True, text=True).stdout
        step('nothing left running (fake agent)', 'fake-acp-agent' not in ps)
        (OUT / 'report.json').write_text(json.dumps(report, indent=2, ensure_ascii=False, default=str), encoding='utf-8')
    failed = [s for s in report['steps'] if not s['ok']]
    print(f"{len(report['steps']) - len(failed)} / {len(report['steps'])} steps passed")
    return 1 if failed else 0


if __name__ == '__main__':
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding='utf-8')
        except Exception:
            pass
    sys.exit(run_main())
