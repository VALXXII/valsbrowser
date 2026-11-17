#!/usr/bin/env python3
"""Chat intégré — the whole validation run on the FAKE agent (design/chat-v1-validation.md).

    python tests/chat/chat_qa.py run            # -> build/chat-qa/report.json + PNG captures (6.8 … 6.12 and more)

Real browser (a copy of build/chat-preview), real WebView2 panel, real sandbox site. The agent is C1's fake ACP agent
(no account, no network): an ordinary message makes it really edit styles/checkout.css of the site copy after the
permission card; « slow », « plan », « echo » pick its other scenarios. Nothing here talks to Claude Code or Codex.
"""
from __future__ import annotations
import json
import re
import sys
import time
import traceback

import chat_qa as C
from chat_qa import Q

report = {'steps': [], 'measures': {}, 'captures': []}
CSS = C.SITE / 'styles' / 'checkout.css'
MOVED = 'margin-left: auto'


def step(name, ok, detail=''):
    report['steps'].append({'step': name, 'ok': bool(ok), 'detail': str(detail)})
    print(('PASS ' if ok else 'FAIL ') + name + (' — ' + str(detail) if detail else ''), flush=True)
    return bool(ok)


def capture(name):
    try:
        path = C.grab(name)
        report['captures'].append(path.name)
    except Exception as e:   # a capture never fails the run
        print('capture failed', name, e)


def mark():
    return len(C.log_text())


def logged(pattern, after=0, timeout=15.0):
    return C.wait_log(pattern, timeout, after)


def page_width():
    return C.page('innerWidth')


def button_left():
    return C.page("document.querySelector('.btn-primary').getBoundingClientRect().left")


def focus_composer():
    return C.panel("(function(){var t=document.querySelector('[contenteditable=true],textarea');if(!t)return 0;t.focus();return 1})()")


def send(text):
    focus_composer(); C.type_panel(text); time.sleep(0.15); C.enter_panel()


def agent_processes():
    return [r for r in C.descendants(Q.our_pid()) if 'fake-acp-agent' in (r.get('Name') or '').lower()]


def launcher_visible():
    import ctypes
    import ctypes.wintypes as W
    main = Q.main_window(); out = []
    @ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    def cb(h, _):
        name = ctypes.create_unicode_buffer(128); C.user32.GetClassNameW(h, name, 128)
        title = ctypes.create_unicode_buffer(128); C.user32.GetWindowTextW(h, title, 128)
        if name.value == 'BrowserAnnotationLayer' and title.value in ('Annotations', 'Annotation') and C.user32.IsWindowVisible(h):
            out.append(title.value)
        return True
    C.user32.EnumChildWindows(main, cb, 0)
    return bool(out)


def ax_summary():
    """Roles and names of the panel's accessibility tree (Chromium's own, what a screen reader gets)."""
    ws = C.ws_for(C.PANEL)
    ws.call('Accessibility.enable')
    nodes = ws.call('Accessibility.getFullAXTree').get('nodes', [])
    out = []
    for n in nodes:
        if n.get('ignored'):
            continue
        role = (n.get('role') or {}).get('value', ''); name = (n.get('name') or {}).get('value', '')
        out.append((role, name))
    return out


def run_main():
    before = C.fingerprints()
    report['fingerprintsBefore'] = before
    p = C.launch(extra_env={'BROWSER_CHAT_PANEL_IDLE_MS': '2500', 'BROWSER_CHAT_AGENT_IDLE_MS': '600000',
                            'WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS': f'--remote-debugging-port={Q.PORT} --remote-allow-origins=http://127.0.0.1:{Q.PORT} --host-resolver-rules="MAP exemple.test 127.0.0.1"'})
    try:
        # ---- at rest: nothing of the chat exists
        time.sleep(1.0)
        step('rest: no chat WebView before the first opening', not C.panel_exists())
        step('rest: no agent process before the first opening', not agent_processes())
        step('rest: no chat activity in the log', 'CHAT ' not in C.log_text())
        wide = page_width()

        # ---- 6.8 viser: the mode, the band's button
        C.shortcut('A')
        step('mode on (Ctrl+Maj+A)', logged(r'ANNOTATE mode on'))
        time.sleep(0.9)
        r = C.page("(function(){var r=document.querySelector('.btn-primary').getBoundingClientRect();return [r.left+r.width-30,r.top+r.height/2]})()")
        C.ws_for(C.URL).call('Input.dispatchMouseEvent', {'type': 'mouseMoved', 'x': r[0], 'y': r[1]})
        time.sleep(0.5)
        capture('6.8-viser')
        ids = C.uia('-List')
        step('band button « Demander à l\'agent » exposed to UI Automation', 'AnnotationAskAgent' in ids, 'Ctrl+Maj+D')
        step('launcher visible in the mode (panel closed)', launcher_visible())

        # ---- open the panel (band button), first time: where is the code?
        m0 = mark()
        step('band button invoked', C.invoke('AnnotationAskAgent'))
        ready = logged(r'CHAT panel ready (\d+) ms after the request', m0, 15)
        step('panel opens', ready is not None)
        created = logged(r'CHAT panel WebView created in (\d+) ms', m0, 5)
        inplace = logged(r'CHAT panel in place (\d+) ms', m0, 5)
        if ready:
            report['measures']['firstOpen'] = {'webviewCreatedMs': int(created.group(1)) if created else None, 'panelInPlaceMs': int(inplace.group(1)) if inplace else None, 'panelReadyMs': int(ready.group(1))}
        time.sleep(0.8)
        narrow = page_width()
        step('the page shrinks, it is not covered', narrow is not None and wide - narrow >= 370, f'page width {wide} -> {narrow} CSS px')
        step('launcher hidden while the panel is open', not launcher_visible())
        sug = logged(r'CHAT folder suggestion from the process listening on the site\'s port \(pid \d+\): (.+)', m0, 10)
        step('folder detected from the port of the site (no admin rights)', sug is not None and sug.group(1).strip().lower() == str(C.SITE).lower(), sug.group(1).strip() if sug else 'none')
        C.wait_for(lambda: 'Utiliser' in C.panel_text(), 8)
        capture('6.2-dossier')
        step('first time: the panel asks where the code is', 'Où est le code de ce site' in C.panel_text())
        m1 = mark()
        C.click_panel(text='Utiliser')
        step('site linked to its folder', logged(r'CHAT site http://127\.0\.0\.1:5183 linked to', m1, 8))
        step('agent session ready', logged(r'CHAT session start: ready', m1, 20))
        modeline = logged(r'CHAT mode (?:set to « (\w[\w-]*) »|« (\w[\w-]*) » kept)', m1, 8)
        step('default mode « Demander » applied right after the start', modeline is not None and (modeline.group(1) or modeline.group(2)) == 'default', modeline.group(0) if modeline else '')
        time.sleep(0.6)
        txt = C.panel_text()
        step('models and effort come from the agent', 'Sonnet' in txt or 'Opus' in txt, re.sub(r'\s+', ' ', txt[-80:]))
        capture('6.1-chat-vide')

        # ---- 6.8 élément visé: the chip, file:line from the Astro attribute
        m2 = mark()
        C.click_page('.btn-primary')
        aimed = logged(r'CHAT element aimed: (\S+) — source (\S+) \((\w+)\), image (\d+) bytes', m2, 10)
        step('aimed element becomes a chip, with its image', aimed is not None and aimed.group(1) == 'button.btn-primary', aimed.group(0)[5:] if aimed else '')
        line = None
        for i, l in enumerate((C.SITE / 'index.html').read_text(encoding='utf-8').split('\n'), 1):
            if 'class="btn-primary"' in l:
                line = i
        step('file:line from the Astro attribute is the real line', aimed is not None and aimed.group(2).startswith(f'index.html:{line}:') and aimed.group(3) == 'astro', f'{aimed.group(2) if aimed else ""} (real line {line})')
        time.sleep(0.5)
        capture('6.8-element-vise')

        # ---- 6.9 écrire la demande
        focus_composer(); C.type_panel('Aligne-le à droite, et en pleine largeur sur mobile')
        time.sleep(0.4)
        capture('6.9-demande')

        # ---- 6.10 / 6.1: the agent works, asks before it writes
        m3 = mark(); t_send = time.time()
        C.enter_panel()
        step('turn started', logged(r'CHAT turn t1 started', m3, 8))
        step('the element says who works on it (mark « working »)', logged(r'mark « working » on the element: found', m3, 8))
        step('annotation written and claimed by the chat agent', logged(r'CHAT annotation ann_\w+: write ok, claim ok', m3, 8))
        snap = logged(r'CHAT snapshot before the turn: ok, (\d+) files, (\d+) ms', m3, 10)
        step('BeginTurn before the prompt', snap is not None, snap.group(0)[5:] if snap else '')
        step('permission asked before the file is written', logged(r'CHAT permission asked: edit', m3, 15))
        step('nothing written before the answer', MOVED not in CSS.read_text(encoding='utf-8'))
        time.sleep(0.6)
        capture('6.10-autorisation')
        ax = ax_summary()
        roles = {r for r, _ in ax}
        step('a11y: permission card is an alert dialog', 'alertdialog' in roles, sorted(roles)[:14])
        unnamed = [r for r, n in ax if r == 'button' and not n.strip()]
        step('a11y: every button of the panel has a name', not unnamed, f'{sum(1 for r, _ in ax if r == "button")} buttons')
        step('a11y: the thread is a live log', 'log' in roles)
        first = logged(r'CHAT first agent text (\d+) ms after the send', m3, 3)

        # ---- 6.11: authorised -> the file changes -> the page reloads -> the element is marked
        m4 = mark(); left0 = button_left()
        C.click_panel(text='Autoriser')
        ended = logged(r'CHAT turn t1 ended: (\w+) in (\d+) ms, (\d+) file\(s\) changed \(\+(\d+) -(\d+)\)', m4, 20)
        step('turn ends with 1 file changed', ended is not None and ended.group(1) == 'end_turn' and ended.group(3) == '1', ended.group(0)[5:] if ended else '')
        step('the file was really edited by the agent', MOVED in CSS.read_text(encoding='utf-8'))
        step('page reloaded by the chat (no dev server reload)', logged(r'CHAT files changed and no dev server reload: reloading the page', m4, 8))
        step('the mode survives the reload', logged(r'ANNOTATE mode re-armed after a reload', m4, 10))
        step('element found again and marked « done »', logged(r'mark « done » on the element: found', m4, 10))
        step('annotation resolved with the agent\'s summary', logged(r'CHAT annotation ann_\w+: resolved ok', m4, 8))
        left1 = C.wait_for(lambda: (button_left() or 0) > (left0 or 0) + 50 and button_left(), 8)
        step('the page shows the change', bool(left1), f'button left {left0} -> {button_left()}')
        if first is None:
            first = logged(r'CHAT first agent text (\d+) ms after the send', m3, 2)
        time.sleep(0.7)
        txt = C.panel_text()
        step('change bar « 1 fichier modifié » with Tout annuler', '1 fichier modifié' in txt and 'Tout annuler' in txt)
        capture('6.11-fait')

        # ---- voir les changements (contract B 1.1)
        try:
            C.click_panel(text='1 fichier modifié'); time.sleep(0.7)
            txt = C.panel_text()
            step('« Voir les changements » shows the diff', 'margin-left' in txt, 'diff lines in the panel')
            capture('6.11-voir-les-changements')
            if C.panel("!!document.getElementById('chg-back')"):
                C.click_panel(selector='#chg-back'); time.sleep(0.3)
        except Exception as e:
            step('« Voir les changements » shows the diff', False, e)

        # ---- tout annuler
        m5 = mark()
        C.wait_for(lambda: 'Tout annuler' in C.panel_text(), 5)
        C.click_panel(text='Tout annuler')
        undone = logged(r'CHAT undo turn: (\w+), (\d+) file\(s\) put back', m5, 15)
        step('Tout annuler puts the file back', undone is not None and undone.group(1) == 'ok' and MOVED not in CSS.read_text(encoding='utf-8'), undone.group(0)[5:] if undone else '')
        back = C.wait_for(lambda: abs((button_left() or 999) - (left0 or 0)) < 2, 8)
        step('the page shows the file as before', bool(back), f'button left {button_left()}')
        time.sleep(0.6)
        capture('6.11-annule')

        # ---- React debug data (simulated fiber): file:line of another element
        m6 = mark()
        C.click_page('#react-button')
        aimed2 = logged(r'CHAT element aimed: (\S+) — source (\S+) \((\w+)\)', m6, 10)
        step('file:line from React debug data', aimed2 is not None and aimed2.group(2) == 'src/CartButton.jsx:6:5' and aimed2.group(3) == 'react', aimed2.group(0)[5:] if aimed2 else '')
        m6b = mark()
        C.click_page('.hint')
        aimed3 = logged(r'CHAT element aimed: (\S+) — (no source[^,]*|source \S+)', m6b, 10)
        step('no debug data on the element: no file:line of its own (never a guess)', aimed3 is not None and aimed3.group(2).startswith('no source'), aimed3.group(0)[5:] if aimed3 else '')

        # ---- stop, queue
        C.click_panel(selector='button[aria-label="Nouveau chat"]'); time.sleep(0.8)
        m7 = mark()
        send('Écris lentement (slow)')
        stream = logged(r'CHAT first agent text (\d+) ms after the send', m7, 10)
        step('streaming turn started', logged(r'CHAT turn t1 started', m7, 8) and stream)
        if stream:   # the measure that means something: no permission card in between
            report['measures']['firstAgentTextAfterSendMs'] = int(stream.group(1))
        time.sleep(0.8)
        capture('6.10-travaille')
        send('echo deuxième message')
        C.wait_for(lambda: 'En file' in C.panel_text() or 'deuxième' in C.panel_text(), 4)
        step('a message sent during a turn waits in the queue', 'deuxième message' in C.panel_text())
        capture('6.5-file-attente')
        C.click_panel(selector='button.send.stop')
        stopped = logged(r'CHAT turn t1 ended: cancelled', m7, 10)
        step('Stop ends the turn (cancelled)', stopped is not None)
        step('the queued message runs next', logged(r'CHAT turn t2 ended: end_turn', m7, 15))

        # ---- 6.12 Planifier
        m8 = mark()
        C.post({'type': 'setMode', 'id': 'plan'}); time.sleep(0.8)
        send('Comment rendre cette page plus claire sur mobile ? Propose un plan, ne touche à rien pour l\'instant.')
        step('plan turn ends', logged(r'CHAT turn t3 ended: end_turn', m8, 15))
        time.sleep(0.6)
        txt = C.panel_text()
        step('« Plan proposé » card with Lancer le plan', 'Plan proposé' in txt and 'Lancer le plan' in txt)
        step('nothing written in Plan mode', MOVED not in CSS.read_text(encoding='utf-8'))
        capture('6.12-planifier')
        m9 = mark()
        C.click_panel(text='Lancer le plan')
        step('Lancer le plan leaves Plan mode for « Demander »', logged(r'CHAT mode asked: « default »', m9, 8))
        step('…and tells the agent to go', logged(r'CHAT turn t4 ended: end_turn', m9, 15))
        # back in « Demander »: an ordinary request is asked before anything is written, and can be refused
        m9b = mark()
        send('Mets le bouton à droite')
        step('in « Demander » the agent asks before writing', logged(r'CHAT permission asked: edit', m9b, 15))
        C.wait_for(lambda: 'Refuser' in C.panel_text(), 5)
        C.click_panel(text='Refuser')
        step('Refuser: nothing written', logged(r'CHAT turn t5 ended: end_turn in \d+ ms, 0 file', m9b, 15) and MOVED not in CSS.read_text(encoding='utf-8'))

        # ---- history of this site
        C.click_panel(selector='button[aria-label="Historique"]'); time.sleep(0.9)
        txt = C.panel_text()
        step('history lists the conversations of this site', 'Aligne-le à droite' in txt, re.sub(r'\s+', ' ', txt)[:160])
        capture('6.7-historique')
        m10 = mark()
        C.click_panel(text='Aligne-le à droite')
        step('a conversation is reopened from the history', logged(r'CHAT conversation reopened: c_\w+ \((\d+) items\)', m10, 8))
        time.sleep(0.6)
        step('its thread is back (message, change bar)', 'pleine largeur sur mobile' in C.panel_text())

        # ---- close: the page takes its width back; destroyed after it stayed closed; agent stopped
        m11 = mark()
        C.click_panel(selector='button[aria-label="Fermer le chat"]')
        step('panel closed', logged(r'CHAT panel closed', m11, 5))
        time.sleep(0.7)
        if not step('the page takes its width back', page_width() == wide, f'{page_width()} CSS px'):
            print('DIAG', C.page('[innerWidth, outerWidth, devicePixelRatio, location.href]'), [w[1:] for w in Q.windows()])
            print('DIAG targets', [(t.get('type'), t.get('url')) for t in C.targets()])
            print('DIAG log', C.log_text().splitlines()[-25:])
        step('launcher back', launcher_visible())
        destroyed = logged(r'CHAT panel WebView destroyed', m11, 12)
        step('panel WebView destroyed after staying closed', destroyed is not None and not C.panel_exists())
        step('agent stopped after the panel closed', logged(r'CHAT agent stopped \(idle\)', m11, 12))
        time.sleep(2.5)
        left = agent_processes()
        step('rest again: 0 agent process, 0 chat WebView', not left and not C.panel_exists(), f'{len(left)} agent process(es)')
        report['measures']['restAfterClose'] = {'agentProcesses': len(left), 'chatWebViews': 1 if C.panel_exists() else 0}

        # ---- from the note panel of an annotation: « Demander à l'agent »
        m12 = mark()
        C.click_page('.title')
        note = C.wait_for(lambda: [w for w in Q.windows() if w[1] == 'BrowserAnnotationNote'], 6)
        step('a click outside the chat opens the note panel as before', bool(note))
        if note:
            edit = Q.find_child(note[0][0], 'Edit')
            Q.post_text('Mets ce titre en gras', edit)
            time.sleep(0.4)
            capture('2.1-note-demander')
            step('note button « Demander à l\'agent » exposed', 'AnnotationNoteAskAgent' in C.uia('-List'))
            step('note button invoked', C.invoke('AnnotationNoteAskAgent'))
            second = logged(r'CHAT panel ready (\d+) ms after the request', m12, 15)
            step('the chat opens from the note (new WebView)', second is not None)
            if second:
                report['measures']['secondOpenPanelReadyMs'] = int(second.group(1))
            step('the selection and its text start a turn', logged(r'CHAT turn t\d+ started', m12, 20) and logged(r'CHAT permission asked: edit', m12, 20))
            C.wait_for(lambda: 'Refuser' in C.panel_text(), 5)
            C.click_panel(text='Refuser')
            step('turn refused ends cleanly', logged(r'CHAT turn t\d+ ended: end_turn', m12, 15))
            time.sleep(0.4)

        # ---- Échap closes, Ctrl+Maj+D toggles
        m13 = mark()
        focus_composer(); C.key_panel('Escape', 'Escape', 27)
        step('Échap in the panel closes it', logged(r'CHAT panel closed', m13, 5))
        time.sleep(0.5)
        m14 = mark(); C.shortcut('D')
        step('Ctrl+Maj+D opens it again', logged(r'CHAT panel opened', m14, 5))
        time.sleep(0.8)
        m15 = mark(); C.shortcut('D')
        step('Ctrl+Maj+D closes it', logged(r'CHAT panel closed', m15, 5))
        time.sleep(0.5)

        # ---- a site that is not local: disabled, explained
        m16 = mark()
        C.ws_for(C.URL).call('Page.navigate', {'url': f'http://exemple.test:{C.PORT_PAGE}/'})
        step('leaving the site ends the mode', logged(r'ANNOTATE mode off \(navigation\)', m16, 8))
        time.sleep(1.0)
        m17 = mark(); C.shortcut('D')
        opened = logged(r'CHAT panel opened on (\S+)( \(not local: disabled\))?', m17, 8)
        step('not a local site: the panel opens disabled', opened is not None and opened.group(2) is not None, opened.group(0)[5:] if opened else '')
        C.wait_for(lambda: C.panel_exists() and 'sites locaux' in C.panel_text(), 8)
        step('…and says why', 'sites locaux' in C.panel_text())
        capture('6.x-site-non-local')
        m18 = mark()
        try:
            send('echo interdit')
        except Exception:
            pass
        time.sleep(1.0)
        step('…and nothing can be sent', 'turn t' not in C.log_text()[m18:])
        C.shortcut('A'); time.sleep(0.8)   # leave the mode (the panel closes with it)
    finally:
        C.quit_all()
        time.sleep(1.0)

    # ---- restart: the folder is remembered, the conversation resumes
    p = C.launch(fresh=False, reset_site=False, extra_env={'BROWSER_CHAT_PANEL_IDLE_MS': '2500'})
    try:
        time.sleep(0.8)
        m20 = mark(); C.shortcut('D')
        opened = logged(r'CHAT panel opened on \S+, folder (.+)', m20, 10)
        step('after a restart the folder is remembered', opened is not None, opened.group(1)[:80] if opened else '')
        C.wait_for(lambda: C.panel_exists() and 'Que' in C.panel_text(), 10)
        step('no « where is the code » step the second time', 'Où est le code de ce site' not in C.panel_text())
        C.click_panel(selector='button[aria-label="Historique"]'); time.sleep(0.9)
        step('history kept across the restart', 'Aligne-le à droite' in C.panel_text())
        m21 = mark()
        C.click_panel(text='Aligne-le à droite')
        step('conversation reopened after the restart', logged(r'CHAT conversation reopened', m21, 8))
        resumed = logged(r'CHAT session start: ready \((resumed|new)\)', m21, 20)
        step('session started again for it (resume asked)', resumed is not None, resumed.group(1) if resumed else '')
        time.sleep(0.5)
        m22 = mark()
        C.post({'type': 'undo.turn', 'turnId': 't1'})
        again = logged(r'CHAT undo turn: (\w+)', m22, 10)
        step('undo after a restart answers (already undone: refused cleanly)', again is not None, again.group(1) if again else '')
        capture('6.7-reprise')
    finally:
        C.quit_all()
        time.sleep(1.5)
    after = C.fingerprints()
    report['fingerprintsAfter'] = after
    step('real Claude / Codex configurations untouched', before == after)
    leftovers = [r for r in C.descendants(0) if False]
    ps = __import__('subprocess').run(['tasklist', '/FI', 'IMAGENAME eq fake-acp-agent.exe', '/NH'], capture_output=True, text=True).stdout
    step('nothing left running (browser, server, fake agent)', 'fake-acp-agent' not in ps and not C.pid_alive(p.pid))


def main(args):
    try:
        run_main()
    except Exception:
        traceback.print_exc()
        step('run completed', False, 'exception')
        C.quit_all()
    C.QA.mkdir(parents=True, exist_ok=True)
    (C.QA / 'report.json').write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf-8')
    failed = [s for s in report['steps'] if not s['ok']]
    print(f"\n{len(report['steps']) - len(failed)}/{len(report['steps'])} steps passed; report: {C.QA / 'report.json'}")
    print('measures:', json.dumps(report['measures']))
    return 1 if failed else 0
