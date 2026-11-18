#!/usr/bin/env python3
"""Chat intégré — Mode Commentaire, checked in a REAL window on the FAKE agent (design/chat-integration.md).

    python tests/chat/chat_qa_comment.py      # -> build/chat-qa/comment/report.json + PNG captures

Same guards and same copy of the browser as tests/chat/chat_qa.py (build/chat-qa-app, its own profile, agent configurations
redirected under build/, BROWSER_ANNOTATIONS_TEST=1, the real Claude / Codex files fingerprinted before and after). The real
mouse pointer is not used: clicks through CDP, keys posted to our windows. One run at a time, nothing left running.

  1  the band's switch Commentaire | Panneau (Paper 1AYP-0), remembered
  2  a click on an element: the bubble (a native window hosting the chat) next to the element, inside the page; it follows it
  3  one conversation per element: frame + icon (Paper 18IJ-0) during and after the work; Échap closes, the agent goes on;
     a click on the element opens the same conversation again
  4  another element while the agent works: its message waits, its frame says « En attente », it starts by itself
  5  the element is gone (another page): the conversation is in the launcher's history, and opens from there
  6  switching to the panel loses nothing
"""
from __future__ import annotations
import ctypes
import ctypes.wintypes as W
import json
import sys
import time
import traceback

import chat_qa as C
from chat_qa import Q

OUT = C.QA / 'comment'
report = {'steps': [], 'measures': {}, 'captures': []}
u32 = ctypes.windll.user32


def step(name, ok, detail=''):
    report['steps'].append({'step': name, 'ok': bool(ok), 'detail': str(detail)})
    print(('PASS ' if ok else 'FAIL ') + name + (' — ' + str(detail) if detail else ''), flush=True)
    return bool(ok)


def mark():
    return len(C.log_text())


def logged(pattern, after=0, timeout=15.0):
    return C.wait_log(pattern, timeout, after)


def capture(name):
    try:
        path = C.grab(f'comment/{name}')
        report['captures'].append(path.name)
    except Exception as e:
        print('capture failed', name, e)


def bubble_window():
    w = [x for x in Q.windows() if x[1] == 'BrowserChatBubble']
    return w[0] if w else None


def page_widget():
    out = []
    @ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    def cb(h, _):
        n = ctypes.create_unicode_buffer(128); u32.GetClassNameW(h, n, 128)
        if n.value == 'Chrome_RenderWidgetHostHWND' and u32.IsWindowVisible(h):
            r = W.RECT(); u32.GetWindowRect(h, ctypes.byref(r)); out.append((r.left, r.top, r.right, r.bottom))
        return True
    u32.EnumChildWindows(Q.main_window(), cb, 0)
    return sorted(out, key=lambda r: r[2] - r[0], reverse=True)[0]


def element_screen(sel, prefix=None):
    p = page_widget(); dpr = C.evaluate(prefix or C.URL, 'devicePixelRatio')
    b = C.evaluate(prefix or C.URL, "(function(){var r=document.querySelector(%s).getBoundingClientRect();return [r.left,r.top,r.right,r.bottom]})()" % json.dumps(sel))
    return [int(p[0] + b[0] * dpr), int(p[1] + b[1] * dpr), int(p[0] + b[2] * dpr), int(p[1] + b[3] * dpr)]


def conv_marks(prefix=None):
    """The conversation frames + icons the page draws (its closed shadow root, read through DevTools)."""
    ws = C.ws_for(prefix or C.URL); ws.call('DOM.enable')
    root = ws.call('DOM.getDocument', {'depth': -1, 'pierce': True})['root']
    out = []
    def walk(n):
        a = n.get('attributes', []); d = dict(zip(a[::2], a[1::2]))
        if 'ct' in d.get('class', '').split():
            out.append(d.get('class'))
        for k in n.get('children', []) + n.get('shadowRoots', []):
            walk(k)
    walk(root)
    return out


def run_main():
    before = C.fingerprints(); report['fingerprintsBefore'] = before
    OUT.mkdir(parents=True, exist_ok=True)
    flog = C.WORK / 'fake-agent.log'
    try:
        flog.unlink()
    except OSError:
        pass
    fake = C.TOOLS / 'fake-acp-agent.exe'
    p = C.launch(extra_env={'BROWSER_CHAT_PANEL_IDLE_MS': '60000', 'BROWSER_CHAT_AGENT_IDLE_MS': '600000',
                            'BROWSER_CHAT_FAKE_AGENT': f'"{fake}" --resume --claude-like --log "{flog}" --edit "{C.FAKE_EDIT}"'})
    try:
        time.sleep(1.0)
        C.shortcut('A'); step('mode on', logged(r'ANNOTATE mode on', 0, 10)); time.sleep(1.2)
        if C.invoke('AnnotationConnectLater') or C.invoke('AnnotationConnectClose'):
            time.sleep(0.6)
        u32.SetForegroundWindow(Q.main_window())

        # ---- 1: the band's switch
        ids = C.uia('-List')
        step('1 · the band shows the switch Commentaire | Panneau (Paper 1AYP-0)', 'AnnotationLayoutComment' in ids and 'AnnotationAskAgent' in ids)
        m1 = mark(); step('1 · « Commentaire » chosen', C.invoke('AnnotationLayoutComment') and logged(r'CHAT layout: Commentaire', m1, 5) is not None)
        saved = json.loads((C.APP / 'annotations-chat' / 'projects.json').read_text(encoding='utf-8')) if (C.APP / 'annotations-chat' / 'projects.json').exists() else {}
        step('1 · …remembered (projects.json « layout »)', saved.get('layout') == 'comment', saved.get('layout'))
        time.sleep(0.5); capture('1-bandeau-commentaire')

        # ---- 2: a click on an element opens the bubble next to it
        m2 = mark()
        C.click_page('.btn-primary')
        opened = logged(r'CHAT bubble opened on (\S+) for conversation (c_\w+)', m2, 10)
        ready = logged(r'CHAT panel ready', m2, 15)
        time.sleep(1.0)
        bw = bubble_window(); el = element_screen('.btn-primary'); pg = page_widget()
        conv1 = opened.group(2) if opened else ''
        step('2 · a click on an element opens a bubble (no panel, no note)', opened is not None and bw is not None and 'CHAT panel opened' not in C.log_text()[m2:] and not [w for w in Q.windows() if w[1] == 'BrowserAnnotationNote'], opened.group(0)[5:] if opened else '')
        if bw:
            r = bw[3]; report['measures']['bubble'] = {'rect': r, 'element': el, 'page': pg}
            beside = r[0] >= el[2] or r[2] <= el[0] or r[1] >= el[3] or r[3] <= el[1]
            inside = r[0] >= pg[0] and r[1] >= pg[1] and r[2] <= pg[2] and r[3] <= pg[3]
            step('2 · …next to the element, never over it, inside the page', beside and inside, f'bubble {r}, element {el}, page {pg}')
        has_bubble_layout = C.wait_for(lambda: C.panel("document.documentElement.className + ' ' + document.body.className"), 3) or ''
        report['measures']['panelClasses'] = has_bubble_layout
        capture('2-bulle-ouverte')

        # ---- 3: the conversation, its frame + icon, Échap, reopen
        m3 = mark()
        C.panel("(function(){var t=document.querySelector('[contenteditable=true]');if(t)t.focus();return 1})()"); C.type_panel('Aligne-le à droite'); time.sleep(0.2); C.enter_panel()
        step('3 · the message starts a turn with the element', logged(r'CHAT turn t\d+ started', m3, 15))
        got = C.wait_for(lambda: [l for l in flog.read_text(encoding='utf-8', errors='replace').splitlines() if l.startswith('prompt-text ')], 15)
        step('3 · …the element joins it by itself (the bubble lists none)', logged(r'CHAT annotation ann_\w+: write ok, claim ok', m3, 10) is not None, got[-1][:80] if got else '')
        if logged(r'CHAT permission asked: edit', m3, 20):
            time.sleep(0.6); capture('3-autorisation-dans-la-bulle')
            C.wait_for(lambda: 'Autoriser' in C.panel_text(), 5)
            try:
                C.click_panel(text='Autoriser')
            except Exception as e:
                print('no Autoriser button', e)
        step('3 · the turn ends', logged(r'CHAT turn t\d+ ended: end_turn', m3, 25))
        time.sleep(2.0)
        marks = C.wait_for(lambda: conv_marks(), 6) or []
        step('3 · the element carries its frame + icon (Paper 18IJ-0)', len(marks) >= 1, str(marks))
        capture('3-termine')
        m4 = mark()
        C.panel("(function(){var t=document.querySelector('[contenteditable=true]');if(t)t.focus();return 1})()")
        C.key_panel('Escape', 'Escape', 27)
        closed = logged(r'CHAT bubble closed \(the agent goes on\)', m4, 5)
        time.sleep(0.5)
        step('3 · Échap closes the bubble, the agent is not stopped', closed is not None and 'agent stopped' not in C.log_text()[m4:] and 'cancelled' not in C.log_text()[m4:])
        step('3 · …the frame + icon stay on the element', len(conv_marks()) >= 1)
        capture('3-bordure-icone-apres-fermeture')
        m5 = mark()
        C.click_page('.btn-primary')
        again = logged(r'CHAT bubble opened on (\S+) for conversation (c_\w+)', m5, 10)
        time.sleep(0.8)
        step('3 · a click on the element opens the same conversation again', again is not None and again.group(2) == conv1 and 'Aligne-le' in (C.panel_text() or ''), again.group(0)[5:] if again else '')

        # ---- 2 (follow): the page scrolls, the bubble stays on the element
        C.page("document.body.style.paddingBottom='1600px'"); time.sleep(0.4)
        b0 = bubble_window()[3]; e0 = element_screen('.btn-primary')
        C.ws_for(C.URL).call('Input.dispatchMouseEvent', {'type': 'mouseWheel', 'x': 100, 'y': 200, 'deltaX': 0, 'deltaY': 160})
        time.sleep(1.0)
        bw2 = bubble_window(); b1 = bw2[3] if bw2 else b0; e1 = element_screen('.btn-primary')
        report['measures']['follow'] = {'before': [b0, e0], 'after': [b1, e1]}
        step('2 · the bubble follows the element when the page scrolls', bw2 is not None and abs((b1[1] - e1[1]) - (b0[1] - e0[1])) <= 3 and e1[1] != e0[1], f'element {e0[1]}->{e1[1]}, bubble {b0[1]}->{b1[1]}')
        capture('2-suivi-defilement')

        # ---- 4: another element while the agent works: it waits, then starts by itself
        m6 = mark()
        C.panel("(function(){var t=document.querySelector('[contenteditable=true]');if(t)t.focus();return 1})()"); C.type_panel('slow'); time.sleep(0.2); C.enter_panel()
        step('4 · a long turn on the first element', logged(r'CHAT turn t\d+ started', m6, 15))
        C.key_panel('Escape', 'Escape', 27); time.sleep(0.4)
        m7 = mark()
        C.click_page('.field-label')
        other = logged(r'CHAT bubble opened on (\S+) for conversation (c_\w+)', m7, 10)
        time.sleep(1.0)
        C.panel("(function(){var t=document.querySelector('[contenteditable=true]');if(t)t.focus();return 1})()"); C.type_panel('Plus grand, ce libellé'); time.sleep(0.2); C.enter_panel()
        time.sleep(0.8)
        waits = C.panel_text() or ''
        wmarks = conv_marks()
        step('4 · the other element\'s message waits: « Commencera après le travail en cours sur … »', 'Commencera après' in waits and 'CHAT turn t' not in C.log_text()[m7:], waits[-160:].replace('\n', ' | '))
        step('4 · …its frame says « En attente »', any('wait' in c for c in wmarks), str(wmarks))
        capture('4-file-attente')
        m8 = mark()
        # stop the first element's turn from its own bubble (« Arrêter »)
        C.click_page('.btn-primary'); time.sleep(1.0); C.post({'type': 'stop'})
        nxt = logged(r'CHAT the next element\'s turn: conversation (c_\w+)', m8, 20)
        started = logged(r'CHAT turn t\d+ started', m8, 20)
        step('4 · when the first turn ends, the waiting one starts by itself', nxt is not None and started is not None and (other is None or nxt.group(1) == other.group(2)), nxt.group(0)[5:] if nxt else '')
        logged(r'CHAT turn t\d+ ended', m8, 25); time.sleep(1.5)

        # ---- 5: the elements are gone (another page): the launcher's history lists their conversations
        if bubble_window():
            C.panel("(function(){var t=document.querySelector('[contenteditable=true]');if(t)t.focus();return 1})()"); C.key_panel('Escape', 'Escape', 27); time.sleep(0.4)
        m9 = mark()
        C.ws_for(C.URL).call('Page.navigate', {'url': C.URL + '?autre-page'})
        time.sleep(3.0)
        step('5 · on another page of the site the mode stays (re-armed)', logged(r'ANNOTATE mode re-armed after a reload', m9, 10) is not None)
        step('5 · …no frame there (their elements are not in this page)', not conv_marks(C.URL))
        C.invoke('AnnotationLauncher'); time.sleep(1.2)
        hist = C.uia('-List')
        report['measures']['historyIds'] = hist[:400]
        step('5 · the launcher\'s history lists the conversations whose element is gone', logged(r'CHAT history of the conversations whose element is gone: (\d+)', m9, 5) is not None)
        capture('5-historique-conversations-disparues')

        # ---- 6: switching to the panel loses nothing
        m10 = mark()
        C.invoke('AnnotationAskAgent')
        step('6 · « Panneau »: the chat opens at the right', logged(r'CHAT layout: Panneau', m10, 5) is not None and logged(r'CHAT panel opened', m10, 10) is not None)
        time.sleep(1.0)
        step('6 · …with the conversation that was shown', bool(C.panel_text()))
        capture('6-panneau-apres-bascule')
    except Exception:
        traceback.print_exc()
        step('run without exception', False, traceback.format_exc()[-400:])
    finally:
        C.quit_all()
        after = C.fingerprints(); report['fingerprintsAfter'] = after
        step('real Claude / Codex configuration files unchanged', before == after)
        ps = __import__('subprocess').run(['tasklist', '/FI', 'IMAGENAME eq fake-acp-agent.exe', '/NH'], capture_output=True, text=True).stdout
        step('nothing left running (fake agent)', 'fake-acp-agent' not in ps)
        (OUT / 'report.json').write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf-8')
    failed = [s['step'] for s in report['steps'] if not s['ok']]
    print(f"{len(report['steps']) - len(failed)} / {len(report['steps'])} steps passed")
    return 1 if failed else 0


if __name__ == '__main__':
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding='utf-8')
        except Exception:
            pass
    sys.exit(run_main())
