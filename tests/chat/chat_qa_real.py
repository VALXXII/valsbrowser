#!/usr/bin/env python3
"""Chat intégré — THE one short real trial with Claude Code (design/chat-v1-validation.md).

    python tests/chat/chat_qa_real.py            # asks nothing: run it only when a real trial is wanted

!! This uses the user's real Claude Code and account: ONE small request (one turn) in the sandbox copy. !!
It is never part of an automatic suite. Same browser copy and guards as chat_qa.py (BROWSER_ANNOTATIONS_TEST=1,
MCP connection redirected under build/), the real Claude / Codex configuration files fingerprinted before and after.

Steps: open the chat on the sandbox, link the detected folder, aim the « Payer » button, send one request (align it
to the right in styles/checkout.css), let the private Node and the adapter download if they are missing (progress in
the panel), check that the session is put in « Demander » before the prompt, authorise the edit(s), wait for the end,
check the file, the reload and the mark, then « Tout annuler » (no agent involved) so the copy is left as it was.
Report: build/chat-qa/real-trial.json, captures build/chat-qa/real-*.png.
"""
from __future__ import annotations
import json
import re
import sys
import time

import chat_qa as C
from chat_qa import Q

PROMPT = ("Dans styles/checkout.css, ajoute « margin-left: auto; » dans la règle .btn-primary pour aligner ce bouton à droite. "
          "Ne change rien d'autre et ne lance aucune commande. Réponds en une phrase.")
report = {'steps': [], 'measures': {}, 'prompt': PROMPT}


def step(name, ok, detail=''):
    report['steps'].append({'step': name, 'ok': bool(ok), 'detail': str(detail)})
    print(('PASS ' if ok else 'FAIL ') + name + (' — ' + str(detail) if detail else ''), flush=True)
    return bool(ok)


def main() -> int:
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding='utf-8')
        except Exception:
            pass
    report['fingerprintsBefore'] = C.fingerprints()
    css = C.SITE / 'styles' / 'checkout.css'
    p = C.launch(real=True, extra_env={'BROWSER_CHAT_PANEL_IDLE_MS': '2500'})
    try:
        original = css.read_text(encoding='utf-8')
        C.shortcut('D')
        step('panel opens', C.wait_log(r'CHAT panel ready', 15))
        det = C.wait_log(r'CHAT agent claude: cli (found|missing) (\S*) \((\w*)\), node (\w+), adapter (\w+)', 30)
        step('Claude Code found on this computer', det is not None and det.group(1) == 'found', det.group(0)[5:] if det else '')
        if not det or det.group(1) != 'found':
            return 1
        report['measures']['cli'] = det.group(2); report['measures']['runtimeAlreadyThere'] = det.group(4) == 'ready' and det.group(5) == 'ready'
        C.wait_for(lambda: 'Utiliser' in C.panel_text(), 10)
        C.click_panel(text='Utiliser')
        step('site linked to the sandbox copy', C.wait_log(r'CHAT site \S+ linked to', 8))
        time.sleep(0.5)
        C.click_page('.btn-primary')
        aimed = C.wait_log(r'CHAT element aimed: (\S+) — source (\S+)', 10)
        step('button aimed with its file:line', aimed is not None, aimed.group(0)[5:] if aimed else '')
        C.panel("(function(){var t=document.querySelector('[contenteditable=true],textarea');t.focus();return 1})()")
        C.type_panel(PROMPT); time.sleep(0.3)
        mark = len(C.log_text()); t0 = time.time()
        C.enter_panel()
        step('turn started', C.wait_log(r'CHAT turn t1 started', 10, mark))
        # preparation (first use only): visible progress, then the session
        shown_prepare = False
        end = time.time() + 600
        # « session start: needs_prepare » comes first on a new data folder: the download follows, then a second start
        while time.time() < end and not re.search(r'CHAT session start: (?!needs_prepare)|CHAT prepare claude: (?!none)', C.log_text()[mark:]):
            if not shown_prepare and re.search(r'Téléchargement \d+ %|Préparation', C.panel_text()):
                shown_prepare = True; C.grab('real-01-preparation')
            time.sleep(0.4)
        prep = re.search(r'CHAT prepare claude: (\w+)', C.log_text()[mark:])
        if prep:
            step('private Node and adapter prepared (pinned fingerprints)', prep.group(1) == 'none', prep.group(0)[5:])
            step('preparation shown in the panel while it ran', shown_prepare)
        started = C.wait_log(r'CHAT session start: (ready \((?:new|resumed)\)|(?!needs_prepare)\S+)', 120, mark)
        step('real agent session ready', started is not None and started.group(1).startswith('ready'), started.group(0)[5:] if started else '')
        report['measures']['sendToSessionReadyS'] = round(time.time() - t0, 1)
        if not started or not started.group(1).startswith('ready'):
            C.grab('real-00-not-ready'); return 1
        mode = C.wait_log(r'CHAT mode (?:set to « ([\w-]+) » \(the session started in « ([\w-]+) »\)|« ([\w-]+) » kept)', 10, mark)
        step('« Demander » imposed before the prompt', mode is not None and (mode.group(1) or mode.group(3)) == 'default', mode.group(0)[5:] if mode else '')
        # the turn: authorise each edit asked, at most 6 cards, 5 minutes
        cards = 0; deadline = time.time() + 300; shot = False
        while time.time() < deadline and not re.search(r'CHAT turn t1 ended', C.log_text()[mark:]):
            asked = len(re.findall(r'CHAT permission asked', C.log_text()[mark:]))
            answered = len(re.findall(r'CHAT permission answered', C.log_text()[mark:]))
            if asked > answered and cards < 6:
                time.sleep(0.6)
                if not shot:
                    C.grab('real-02-autorisation'); shot = True
                step('nothing written before the authorisation', css.read_text(encoding='utf-8') == original) if cards == 0 else None
                try:
                    C.click_panel(text='Autoriser'); cards += 1
                except Exception:
                    pass
                time.sleep(0.6)
            time.sleep(0.3)
        ended = re.search(r'CHAT turn t1 ended: (\w+) in (\d+) ms, (\d+) file\(s\) changed \(\+(\d+) -(\d+)\)', C.log_text()[mark:])
        step('turn ended normally', ended is not None and ended.group(1) == 'end_turn', ended.group(0)[5:] if ended else C.log_text()[-600:])
        first = re.search(r'CHAT first agent text (\d+) ms after the send', C.log_text()[mark:])
        if first:
            report['measures']['promptToFirstAgentTextMs'] = int(first.group(1))
        if ended:
            report['measures']['turnMs'] = int(ended.group(2)); report['measures']['permissionCards'] = cards
        now = css.read_text(encoding='utf-8')
        step('the agent edited styles/checkout.css of the sandbox copy', now != original and 'margin-left' in now, f'{ended.group(3) if ended else "?"} file(s)')
        step('page reloaded and the element marked', C.wait_log(r'ANNOTATE mode re-armed after a reload', 12, mark) and C.wait_log(r'mark « done » on the element: (found|not found)', 10, mark))
        left = C.page("document.querySelector('.btn-primary').getBoundingClientRect().left")
        step('the page shows the button on the right', (left or 0) > 150, f'left {left}')
        step('annotation resolved in the store', C.wait_log(r'CHAT annotation ann_\w+: resolved ok', 10, mark))
        time.sleep(0.8)
        report['agentAnswer'] = (C.panel("(function(){var l=document.querySelector('[role=log]');return l?l.innerText:''})()") or '')[-600:]
        C.grab('real-03-fait')
        # leave the copy as it was (no agent involved)
        m2 = len(C.log_text())
        C.click_panel(text='Tout annuler')
        undone = C.wait_log(r'CHAT undo turn: (\w+)', 15, m2)
        step('Tout annuler puts the file back', undone is not None and undone.group(1) == 'ok' and css.read_text(encoding='utf-8') == original)
        time.sleep(1.0)
        C.grab('real-04-annule')
    finally:
        C.quit_all(); time.sleep(1.5)
    report['fingerprintsAfter'] = C.fingerprints()
    step('real Claude / Codex configurations untouched', report['fingerprintsBefore'] == report['fingerprintsAfter'])
    return 0


if __name__ == '__main__':
    code = 1
    try:
        code = main()
    except Exception:
        import traceback
        traceback.print_exc(); C.quit_all()
        step('run completed', False, 'exception')
    report['steps'] = [s for s in report['steps'] if s]
    C.QA.mkdir(parents=True, exist_ok=True)
    (C.QA / 'real-trial.json').write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf-8')
    failed = [s for s in report['steps'] if not s['ok']]
    print(f"\n{len(report['steps']) - len(failed)}/{len(report['steps'])} steps passed; measures: {json.dumps(report['measures'])}")
    sys.exit(1 if failed or code else 0)
