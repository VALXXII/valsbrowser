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


def park_mouse():
    """The pointer leaves the page (no hover frame in the captures that show a result)."""
    try:
        C.page("document.dispatchEvent(new PointerEvent('pointerout', {bubbles: true, relatedTarget: null}))")
        C.ws_for(C.URL).call('Input.dispatchMouseEvent', {'type': 'mouseMoved', 'x': -10, 'y': -10})
    except Exception:
        pass


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


def perm_title():
    return C.panel("(function(){var t=document.querySelector('.perm .perm-t');return t?t.innerText:''})()") or ''


def unfold_steps():
    """Opens every folded group of steps of the thread (the panel folds finished steps into one counted line)."""
    C.panel("(function(){var n=0;document.querySelectorAll('.steps > button.fold[aria-expanded=false]').forEach(function(b){b.click();n++});return n})()")
    time.sleep(0.4)


def gauge_label():
    """The context gauge's accessible name, '' while it is hidden."""
    return C.panel("(function(){var g=document.querySelector('.gauge');return g&&!g.hidden?(g.getAttribute('aria-label')||'?'):''})()") or ''


def corner_zoom(name, rect, box=26, zoom=12):
    """The REAL screen around the top-left corner of a window rect, enlarged (nearest neighbour), and how many of its
    pixels are blends between the page behind and the panel (the antialiasing of the corner's silhouette)."""
    from PIL import ImageGrab, Image
    x, y = rect[0] - 6, rect[1] - 6
    try:
        img = ImageGrab.grab(bbox=(x, y, x + box, y + box), all_screens=True).convert('RGB')
    except OSError as e:   # locked session: the screen cannot be read
        return {'unavailable': str(e)}
    img.resize((box * zoom, box * zoom), Image.NEAREST).save(C.QA / f'{name}.png')
    report['captures'].append(f'{name}.png')
    lum = lambda p: .2126 * p[0] + .7152 * p[1] + .0722 * p[2]
    # behind: the page at the very corner; panel: the canvas just inside the edge, mid-height (away from the note's text)
    back = lum(img.getpixel((0, 0))); inside = lum(img.getpixel((14, box - 1)))
    lo, hi = min(back, inside) + 60, max(back, inside) - 25
    blended = sum(1 for yy in range(20) for xx in range(20) if lo < lum(img.getpixel((xx, yy))) < hi)   # the corner only, not the text
    return {'behind': round(back), 'panel': round(inside), 'blendedPixels': blended}


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
        # contract B 1.4: a sure detection (one server, its working folder is a project, the page's own file marks point
        # into it) is used at once — no « Où est le code de ce site ? » screen; the folder shows as a chip in the composer
        m1 = m0
        marks = logged(r"CHAT the page's file marks agree with the folder \((\d+) file", m0, 10)
        step('the page\'s own file marks point into that folder', marks is not None, marks.group(0)[5:] if marks else '')
        auto = logged(r'CHAT folder used without asking \(sure\): (.+)', m0, 10)
        step('sure detection: the folder is used at once', auto is not None and auto.group(1).strip().lower() == str(C.SITE).lower(), auto.group(1).strip() if auto else '')
        step('site linked to its folder', logged(r'CHAT site http://127\.0\.0\.1:5183 linked to', m1, 8))
        step('agent session ready', logged(r'CHAT session start: ready', m1, 20))
        time.sleep(0.6)
        step('no « Où est le code de ce site ? » screen', 'Où est le code de ce site' not in C.panel_text())
        chip = C.panel("(function(){var b=document.querySelector('[aria-label^=\"Dossier du projet\"], .tb.folder');return b?b.getAttribute('aria-label')||'?':''})()") or ''
        step('V1.2: no folder chip in the composer (one project per chat; « + » can still change it)', chip == '', chip)
        capture('6.2-dossier-automatique')
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
        step('permission card title: a sentence, verb first (« … veut modifier checkout.css »)', 'veut modifier checkout.css' in perm_title(), perm_title())
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
        park_mouse(); time.sleep(0.4)
        capture('6.11-fait')

        # ---- contract B 1.2: the context gauge (usage_update of the agent -> « usage »)
        used = logged(r'CHAT usage (\d+) / (\d+)', m3, 5)
        step('the agent\'s context usage is relayed to the panel', used is not None and int(used.group(2)) == 200000, used.group(0)[5:] if used else '')
        g1 = C.wait_for(gauge_label, 5) or ''
        step('the gauge shows it (accessible name)', 'Mémoire du chat' in g1 and '200' in g1, g1)
        C.panel("(function(){var g=document.querySelector('.gauge');if(g)g.focus();return 1})()"); time.sleep(0.5)
        capture('6.13-jauge-contexte')
        focus_composer()

        # ---- voir les changements (contract B 1.1): the bar unfolds, « Voir les changements » opens the view
        try:
            m4b = mark()
            C.click_panel(text='Voir les fichiers'); time.sleep(0.5)          # the bar's chevron: the file and its diff unfold under it
            inline = 'margin-left: auto' in C.panel_text()
            capture('6.11-fichiers-deplies')
            C.click_panel(selector='button.more'); time.sleep(0.9)            # « Voir les changements »: the full view
            opened = C.panel("(function(){var b=document.getElementById('chg-back');return !!b&&b.getBoundingClientRect().width>0})()")
            view = C.panel("(function(){var b=document.getElementById('chg-back');var v=b&&b.closest('[role=dialog],section,div.changes,#changes');return (v||document.body).innerText})()") or ''
            step('the unfolded bar shows the diff of the single file (FileChange.diff)', inline)
            step('« Voir les changements » opens the view with the diff', bool(opened) and 'margin-left: auto' in view, 'styles/checkout.css' if 'checkout.css' in view else view[:80])
            capture('6.11-voir-les-changements')
            if opened:
                C.click_panel(selector='#chg-back'); time.sleep(0.4)
        except Exception as e:
            step('« Voir les changements » opens the view with the diff', False, e)

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
        step('a new conversation hides the gauge (nothing known yet)', gauge_label() == '')
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

        # ---- contract B 1.2: readable permissions. Our own annotation tools (they only touch the annotation store)
        #      are allowed without a card, even in « Demander », and leave a step line; any other MCP tool gets a card
        #      with a sentence for a title and the raw name + arguments under « Détails ».
        server = 'browser-annotations-preview'
        notes = '{"annotations":[{"id":"ann_qa1","note":"Aligne le bouton à droite"},{"id":"ann_qa2","note":"Titre trop pâle"}],"nextCursor":null}'
        raw_list = f'mcp__{server}__browser_annotations_list'
        mA = mark()
        send(f'mcptool {raw_list}|{{}}|{notes}')
        allowedA = logged(r'CHAT annotation tool allowed without a card: browser_annotations_list', mA, 15)
        endedA = logged(r'CHAT turn t\d+ ended: end_turn', mA, 15)
        step('our annotation tool (Claude Code\'s name) is allowed without a card, in « Demander »', allowedA is not None and endedA is not None and 'permission asked' not in C.log_text()[mA:])
        time.sleep(0.6)
        unfold_steps()
        txt = C.panel_text()
        step('…and leaves a step line « A lu 2 annotations » (never counted as a file)', 'A lu 2 annotations' in txt and 'A lu 1 fichier' not in txt)
        step('…the raw tool name appears nowhere but in the message typed for the fake agent', txt.count(raw_list) == 1, txt.count(raw_list))
        mB = mark()
        send(f'mcptool mcp.{server}.browser_annotation_resolve|{{"id":"ann_qa1","summary":"fait"}}|{{"ok":true}}')
        allowedB = logged(r'CHAT annotation tool allowed without a card: browser_annotation_resolve', mB, 15)
        endedB = logged(r'CHAT turn t\d+ ended: end_turn', mB, 15)
        step('our annotation tool (Codex\'s name) too', allowedB is not None and endedB is not None and 'permission asked' not in C.log_text()[mB:])
        time.sleep(0.6)
        unfold_steps()
        step('…its step line names the annotation by its note', 'A marqué l\'annotation « Aligne le bouton à droite » comme terminée' in C.panel_text())
        C.panel("(function(){var l=document.querySelector('[role=log]');if(l)l.scrollTop=l.scrollHeight;return 1})()"); time.sleep(0.3)
        capture('6.14-outils-annotations-sans-carte')

        def step_summaries():
            """« summary » of the finished steps kept with the conversation (what the panel was sent), by step title."""
            out = {}
            for f in (C.APP / 'annotations-chat' / 'conversations').glob('c_*.json'):
                try:
                    for it in json.loads(f.read_text(encoding='utf-8')).get('items', []):
                        if it.get('type') == 'toolCall' and isinstance(it.get('summary'), dict):
                            out[it.get('title', '')] = it['summary']
                except Exception:
                    pass
            return out
        sums = C.wait_for(lambda: (lambda d: d if any(k.startswith('A marqué') for k in d) else None)(step_summaries()), 5) or step_summaries()
        s_list = sums.get('A lu 2 annotations') or {}
        s_done = next((v for k, v in sums.items() if k.startswith('A marqué')), {})
        step('each finished step of ours carries its « summary » for the folded line (contract B 1.3)',
             s_list.get('verb') == 'a lu' and s_list.get('count') == 2 and s_list.get('many') == 'annotations'
             and s_done.get('verb') == 'a marqué' and s_done.get('after') == 'comme terminée' and 'Aligne le bouton à droite' in s_done.get('object', ''),
             json.dumps([s_list, s_done], ensure_ascii=False))
        mC = mark()
        send('mcptool mcp__github__create_issue|{"repo":"atelier/site","label":"bug"}|ok')
        askedC = logged(r'CHAT permission asked: other mcp__github__create_issue', mC, 15)
        C.wait_for(lambda: 'Refuser' in C.panel_text(), 5)
        tC = perm_title()
        step('another MCP server: a card, titled with a sentence', askedC is not None and 'veut utiliser l\'outil « create_issue » de « github »' in tC, tC)
        C.click_panel(selector='.perm .perm-more'); time.sleep(0.4)
        details = C.panel("(function(){var d=document.getElementById('perm-details');return d&&!d.hidden?d.innerText:''})()") or ''
        step('…« Détails » holds the raw name and the arguments', 'mcp__github__create_issue' in details and '"repo"' in details, re.sub(r'\s+', ' ', details)[:140])
        capture('6.10-autorisation-outil-mcp')
        C.click_panel(text='Refuser')
        step('…refused: the turn ends', logged(r'CHAT turn t\d+ ended: end_turn', mC, 15))
        mD = mark()
        send(f'mcptool mcp__{server}-evil__browser_annotations_list|{{}}|x')
        askedD = logged(r'CHAT permission asked: other mcp__browser-annotations-preview-evil__browser_annotations_list', mD, 15)
        step('a look-alike server is never allowed by itself (card)', askedD is not None and 'allowed without a card' not in C.log_text()[mD:])
        C.wait_for(lambda: 'Refuser' in C.panel_text(), 5)
        C.click_panel(text='Refuser')
        step('…refused: the turn ends', logged(r'CHAT turn t\d+ ended: end_turn', mD, 15))
        time.sleep(0.4)

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
        g2 = C.wait_for(gauge_label, 5) or ''
        step('…and its gauge (usage kept with the conversation)', 'Mémoire du chat' in g2, g2)

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

        # ---- V1.3 (the user's decision): with the chat closed, a click on an element OPENS the chat — the element is a
        #      chip, the caret is in the composer; no note panel on a local site
        m12 = mark()
        C.click_page('.field-label')   # (not h1.title: « title » in a message picks the fake agent's rename scenario)
        second = logged(r'CHAT panel ready (\d+) ms after the request', m12, 15)
        step('V1.3: chat closed, a click on an element opens the chat (new WebView), no note panel', second is not None and not [w for w in Q.windows() if w[1] == 'BrowserAnnotationNote'])
        if second:
            report['measures']['secondOpenPanelReadyMs'] = int(second.group(1))
        step('…the element becomes a chip of the composer', logged(r'CHAT element aimed: \S*field-label', m12, 10) is not None and bool(C.wait_for(lambda: C.panel("document.querySelectorAll('.cin .chip').length"), 6)))
        time.sleep(0.4)
        focus_composer(); C.type_panel(' Mets ce libellé en gras'); time.sleep(0.2); C.enter_panel()
        step('the element and its text start a turn', logged(r'CHAT turn t\d+ started', m12, 20) and logged(r'CHAT permission asked: edit', m12, 20))
        C.wait_for(lambda: 'Refuser' in C.panel_text(), 5)
        C.click_panel(text='Refuser')
        step('turn refused ends cleanly', logged(r'CHAT turn t\d+ ended: end_turn', m12, 15))
        time.sleep(0.4)

        # ---- Échap closes, Ctrl+Maj+D toggles
        m13 = mark()
        focus_composer(); C.key_panel('Escape', 'Escape', 27)
        step('Échap in the panel closes it', logged(r'CHAT panel closed', m13, 5))
        time.sleep(0.5)
        # ---- Ctrl+Entrée in the composer: an annotation without the agent (the note panel's « Ajouter » before V1.3).
        #      Two of them for « Traiter les annotations » (the fake agent gives back a note that says « impossible » and
        #      does not touch one that says « oublie »)
        kept = []
        for sel, text in (('.hint', 'Texte trop discret'), ('.btn-primary', 'Déplace ce bouton dans l\'en-tête (impossible sans le gabarit)'), ('#react-button', 'On oublie celle-ci pour le moment')):
            mN = mark()
            C.click_page(sel)
            C.wait_for(lambda: C.panel_exists() and C.panel("document.querySelectorAll('.cin .chip').length"), 15)
            time.sleep(0.3)
            focus_composer(); C.type_panel(' ' + text); time.sleep(0.2)
            C.key_panel('Enter', 'Enter', 13, modifiers=2)   # Ctrl+Entrée
            kept.append(logged(r'CHAT annotation kept \(Ctrl\+Entrée\) ann_\w+: write ok', mN, 10) is not None)
            time.sleep(0.4)
        step('Ctrl+Entrée keeps each element and its words as a plain annotation (written, no turn)', all(kept) and 'CHAT turn t' not in C.log_text()[m13:], str(kept))
        step('…the thread says it discreetly (« Gardé en annotation »)', 'Gardé en annotation' in C.panel_text())
        capture('6.18-garde-en-annotation')
        m14 = mark()
        time.sleep(0.4)

        # ---- « Traiter les annotations » (contract B 1.4, Paper 186K-0): the pending annotations of the site are posed
        #      in the composer, the ticked ones leave as one message, and that message is a card that follows each one
        try:
            C.wait_for(lambda: C.panel_exists() and logged(r'CHAT session start: ready', m12, 1) is not None, 20)
            mT = mark()
            C.post({'type': 'annotations.request'})                         # what the « /annotations » command of the panel sends
            posed = logged(r'CHAT batch (b_\w+) posed \(([^)]*)\): (\d+) annotation\(s\):((?: ann_\w+)+)', mT, 10)
            ids = posed.group(4).split() if posed else []
            step('« Traiter les annotations »: the site\'s pending annotations are posed in the composer', len(ids) >= 4, f'{len(ids)} annotation(s)')
            C.wait_for(lambda: 'à traiter' in C.panel_text(), 5)
            C.panel("(function(){var m=document.querySelector('.batch-more');if(m)m.click();return 1})()"); time.sleep(0.3)
            rows = C.panel("(function(){return Array.prototype.map.call(document.querySelectorAll('.batch-row'),function(r){return [r.dataset.id,r.getAttribute('aria-checked'),r.innerText]})})()") or []
            step('…one line each, with its note, all ticked', len(rows) == len(ids) and all(r[1] == 'true' for r in rows) and any('Texte trop discret' in r[2] for r in rows), f'{len(rows)} line(s)')
            capture('6.15-lot-avant-envoi')
            keep = next((r[0] for r in rows if 'Texte trop discret' in r[2]), None)
            if keep:
                C.panel("(function(){var r=document.querySelector(\".batch-row[data-id='" + keep + "']\");if(r)r.click();return 1})()"); time.sleep(0.3)
            focus_composer(); C.type_panel('Garde les couleurs du site.'); time.sleep(0.2)
            mS = mark(); C.enter_panel()
            sent = logged(r'CHAT batch b_\w+ sent: (\d+) of (\d+) annotation', mS, 8)
            step('the ticked ones leave as one message; the unticked one stays for later', sent is not None and int(sent.group(1)) == len(ids) - 1 and int(sent.group(2)) == len(ids), sent.group(0)[5:] if sent else '')
            working = logged(r'CHAT batch status (ann_\w+): working \(tool\)', mS, 20)
            step('claim by the agent: the line says who takes care of it', working is not None)
            C.wait_for(lambda: "s'en occupe" in C.panel_text(), 5)
            capture('6.16-lot-en-cours')
            ended = logged(r'CHAT turn t\d+ ended: end_turn', mS, 90)
            over = logged(r'CHAT batch b_\w+ over: (\d+) done, (\d+) not handled', mS, 10)
            step('the turn ends; each annotation has its outcome', ended is not None and over is not None and int(over.group(1)) >= 1 and int(over.group(2)) == 2, over.group(0)[5:] if over else '')
            step('…and our annotation tools never asked for a card', 'permission asked' not in C.log_text()[mS:])
            time.sleep(0.8)
            card = C.panel("(function(){var c=document.querySelectorAll('.ann-card');c=c[c.length-1];return c?c.innerText:''})()") or ''
            step('done: the agent\'s one-sentence summary on the line', "C'est corrigé dans le code (faux agent)." in card)
            step('given back by the agent: its reason on the line', "Je n'ai pas trouvé cet élément dans le code." in card)
            step('never touched: said plainly, it stays waiting', "ne l'a pas commencée. Elle reste en attente." in card)
            C.panel("(function(){var c=document.querySelectorAll('.ann-card');c=c[c.length-1];if(c)c.scrollIntoView({block:'center'});return 1})()"); time.sleep(0.4)
            capture('6.17-lot-termine')
            C.panel("(function(){var b=document.querySelectorAll('.ann-inst');b=b[b.length-1];if(b)b.click();return 1})()"); time.sleep(0.4)
            card2 = C.panel("(function(){var c=document.querySelectorAll('.ann-card');c=c[c.length-1];return c?c.innerText:''})()") or ''
            step('« Voir la consigne » shows the exact text sent (tool names only there, and the precision)', 'browser_annotation_claim' in card2 and 'Garde les couleurs du site.' in card2 and 'browser_annotation_claim' not in card)
            capture('6.17-lot-consigne')
            mShow = mark()
            C.panel("(function(){var c=document.querySelectorAll('.ann-card');c=c[c.length-1];var r=c&&c.querySelector('.ann-row');if(r)r.click();return 1})()")
            shown = logged(r'CHAT mark « show » on the element: (found|not found)', mShow, 6)
            step('a click on a line shows the annotation\'s element on the page', shown is not None and shown.group(1) == 'found', shown.group(0)[5:] if shown else '')
            time.sleep(0.5); capture('6.17-lot-montrer-sur-la-page')
            left = C.panel('window.__n=0') or 0
            mP = mark(); C.post({'type': 'annotations.request'})
            again = logged(r'CHAT batch (b_\w+) posed \(([^)]*)\): (\d+) annotation', mP, 10)
            step('what was left is still pending: a next batch offers it again', again is not None and int(again.group(3)) >= 3, again.group(0)[5:] if again else '')
            C.panel("(function(){document.querySelectorAll('.batch-row[aria-checked=true]').forEach(function(r){r.click()});return 1})()"); time.sleep(0.3)
        except Exception as e:
            step('« Traiter les annotations » flow', False, repr(e))
        m15 = mark(); C.shortcut('D')
        step('Ctrl+Maj+D closes it', logged(r'CHAT panel closed', m15, 5))
        time.sleep(0.5)

        # ---- contract B 1.4: an uncertain detection still asks (a server running from a folder that is no project)
        import subprocess, tempfile, shutil
        loose = tempfile.mkdtemp(prefix='chat-qa-vrac-')
        with open(loose + '/page.html', 'w', encoding='utf-8') as f:
            f.write('<!doctype html><meta charset="utf-8"><title>Vrac</title><h1 class="title">Un dossier sans projet</h1><p>Rien ici ne dit où est le code.</p>')
        other = subprocess.Popen([sys.executable, '-m', 'http.server', '5184', '--bind', '127.0.0.1'], cwd=loose, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            import socket
            def listening():
                try:
                    socket.create_connection(('127.0.0.1', 5184), timeout=0.3).close(); return True
                except OSError:
                    return False
            C.wait_for(listening, 10)
            mU = mark()
            C.ws_for(C.URL).call('Page.navigate', {'url': 'http://127.0.0.1:5184/page.html'})
            logged(r'ANNOTATE mode off \(navigation\)', mU, 8)
            logged(r'NAVIGATION OK http://127\.0\.0\.1:5184/page\.html', mU, 8); time.sleep(1.0)
            mU2 = mark(); C.shortcut('D')
            doubt = logged(r'CHAT folder to confirm: (.+)', mU2, 12)
            step('uncertain detection (the folder does not look like a project): not used by itself', doubt is not None and 'used without asking' not in C.log_text()[mU2:], doubt.group(1).strip() if doubt else '')
            C.wait_for(lambda: C.panel_exists() and 'Où est le code de ce site' in C.panel_text(), 8)
            txt = C.panel_text()
            step('…the panel asks « Où est le code de ce site ? » and proposes the folder found', 'Où est le code de ce site' in txt and 'Utiliser' in txt)
            step('…and the batch of the other site left the composer', 'à traiter' not in txt and 'annotation cochée' not in txt)
            capture('6.2-dossier')
            C.shortcut('A'); time.sleep(0.9)                                   # leave the mode: the panel closes with it
            C.ws_for('http://127.0.0.1:5184').call('Page.navigate', {'url': C.URL}); time.sleep(1.2)
            mU3 = mark(); C.shortcut('A')
            step('back on the sandbox site, mode on again', logged(r'ANNOTATE mode on', mU3, 8))
            time.sleep(0.9)
        finally:
            other.terminate()
            try:
                other.wait(timeout=5)
            except Exception:
                other.kill()
            shutil.rmtree(loose, ignore_errors=True)

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
