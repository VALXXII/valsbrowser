#!/usr/bin/env python3
"""Final QA, phase 3 (a): one test per sequence the user lived (R1 to R48 and the bugs), in the order they lived it, in a
REAL window (real mouse and keys through SendInput, real captures), on landing.html, the page area at the user's size
(about 1470 × 665), the fake agent started in « auto » like the real Claude adapter. After every step the plan's
invariants are checked too (qa_final.Invariants).

    CHAT_QA_BUILD=chat-preview python tests/chat/final/journeys.py [--only name,name] [--list]
        -> build/chat-qa/final/journeys/<name>/report.json + captures, build/chat-qa/final/journeys.json

Only when the computer is free (« Libre »): one real-window suite at a time. Each journey starts its own copy of the browser.
Purely visual retours measured to the pixel elsewhere are listed in COVERED_ELSEWHERE (the suite that proves them runs in
the same final pass).
"""
from __future__ import annotations
import json
import re
import sys
import time
import traceback

import qa_final as F
from qa_final import C

JOURNEYS = []
COVERED_ELSEWHERE = {
    'R1': 'tests/chat/chat_qa_comment_v2.py (bubble corners, DWM) + J bulle_forme here',
    'R3': 'tests/chat/chat_qa_comment_v2.py r3_measure (menus not clipped)',
    'R10': 'tests/annotations-ui (band mask, Paper 1AYJ-0)',
    'R16': 'tests/annotations-ui page_v4.py (padding / gap inspector)',
    'R17': 'tests/chat-panel/run.py (grouped cards, corners)',
    'R25': 'tests/chat/chat_qa_comment_v4b.py (band glide)',
    'R27': 'tests/annotations-ui page_v4.py (one contour, 100/125/150 %)',
    'R37': 'tests/chat/chat_qa_comment_v4b.py (inspector glued, frame by frame)',
    'R39': 'tests/chat/chat_qa_comment_v5.py follow() (wheel, fast wheel, bar, keyboard)',
    'R39b': 'tests/chat/chat_qa_comment_v5.py edge_frames()',
    'R41': 'tests/chat/chat_qa_comment_v5.py morph()',
    'R18': 'tests/chat/chat_qa_comment_v4b.py (entry without intermediate image) + every opening here (< 150 ms, whole)',
    'R30': 'tests/chat-panel/run.py (entry 250 ms, 12 px, cubic-bezier(.23,1,.32,1))',
    'R36': 'J r38_r40_diff_poignee (reload / glide logged) + tests/chat/chat_qa_comment_v4b.py (fade images)',
}


def journey(name, retours, agent_args='', extra_env=None):
    def deco(fn):
        JOURNEYS.append({'name': name, 'retours': retours, 'fn': fn, 'agent': agent_args, 'env': extra_env}); return fn
    return deco


# ---------------------------------------------------------------- shared steps
class J:
    """One journey: its report, its invariants, its steps."""
    def __init__(self, name):
        self.R = F.Report('journeys/' + name); self.inv = None; self.n = 0; self.uia = None

    def step(self, name, ok, detail=''):
        return self.R.step(name, ok, detail)

    def check(self, where, **k):
        self.n += 1
        return self.inv.check(f'{self.n:02d} {where}', **k)

    def open_on(self, sel, where='', scroll=None, front=True):
        """A real click on `sel` (a point of the element itself, out of the open bubble), then the opening invariants."""
        if front:
            F.front()
        if scroll:
            F.scroll_into_view(sel, scroll)
        shown, rb = F.bubble_shown()
        p = F.element_point(sel, avoid=rb if shown and isinstance(rb, tuple) else None)
        m = F.log_mark(); t = time.perf_counter(); F.click_at(*p, settle=0.05)
        self.n += 1
        self.inv.after_element_click(sel, t, f'{self.n:02d} {where or "click"}', mark=m)
        F.wait_log(r'CHAT panel ready|CHAT bubble opened on', m, 5)
        return m

    def send(self, sel, scenario, token=None, wait_end=True, kind='bubble', timeout=40):
        token = token or f'jeton{len(self.inv.m.conv) + 1}x{int(time.time()) % 1000}'
        m = F.log_mark(); F.type_and_send(f'{scenario} {token}', kind)
        self.inv.m.conv[sel] = token
        if wait_end:
            self.step(f'the turn « {scenario} » ends', F.wait_log(r'CHAT turn t\d+ ended', m, timeout))
            time.sleep(0.8)
        return m

    def close(self):
        m = F.log_mark(); self.inv.allow_close = True; F.escape_chat('bubble'); time.sleep(0.6)
        self.inv.m.open = None
        return m


def mode_label():
    return F.chat_state('bubble').get('mode')


# ---------------------------------------------------------------- the journeys (in the order the user lived them)
@journey('r02_r32_marque_apres_envoi', 'R2 R32 R27')
def j_marque(j):
    j.open_on('a.btn-primary', 'click without sending')
    j.close(); j.check('closed without sending')
    j.step('R2/R32 · no conversation mark after a click and Échap without sending', len(F.conversation_marks()) == 0, F.conversation_marks())
    j.open_on('h1.hero-title', 'another element'); j.open_on('p.hero-text', 'and another'); j.close()
    j.step('R32 · still no mark after several elements', len(F.conversation_marks()) == 0)
    j.open_on('a.btn-primary', 'click to send'); j.send('a.btn-primary', 'reply')
    j.check('after the send')
    j.step('R2 · the mark appears after the first send (one)', len(F.conversation_marks()) == 1, F.conversation_marks())
    j.close(); j.check('closed')
    F.set_layout('panel'); j.inv.m.layout = 'panel'
    F.front(); p = F.element_point('div.hero-visual'); F.click_at(*p); time.sleep(0.8)
    j.check('panel: an element picked')
    j.step('R32 · Chat à droite: a picked element gets no mark without a send', len(F.conversation_marks()) == 1, F.conversation_marks())


@journey('r06_conversation_bulle_fermee', 'R6 R44 R18 (a click on an element WITH a conversation while the bubble is closed)')
def j_r6(j):
    for sel in ('a.btn-primary', 'section.hero', 'div.plans'):
        j.open_on(sel, f'{sel} first click', scroll='center' if sel == 'div.plans' else None)
        j.send(sel, 'vrai-markdown'); j.check(f'{sel} conversation')
        j.R.capture(f'{sel}-conversation')
        j.close(); j.check(f'{sel} closed')
        j.open_on(sel, f'{sel} with its conversation, bubble closed')
        j.check(f'{sel} reopened'); j.R.capture(f'{sel}-rouvert')
        j.close()
    # closed, then a click on ANOTHER element that has a conversation (the first one)
    F.scroll_into_view('a.btn-primary', 'center')
    j.open_on('a.btn-primary', 'back to the first element (closed, scrolled away and back)'); j.check('first element again')


@journey('r44_grand_element_taille_utilisateur', 'R44 R29 (section.hero, div.plans, page area ≈ 1470 × 665)')
def j_r44(j):
    size = F.set_page_size(*F.USER_PAGE)
    j.step('the page area is the user\'s (≈ 1470 × 665)', abs(size[0] - 1470) <= 4 and abs(size[1] - 665) <= 4, size)
    for sel, scroll in (('section.hero', None), ('div.plans', 'center'), ('div.plans', 'start'), ('section.hero', 'end')):
        if scroll:
            F.scroll_into_view(sel, scroll)
        else:
            F.page('window.scrollTo(0,0)'); time.sleep(0.4)
        m = j.open_on(sel, f'{sel} ({scroll or "top"})')
        side = [l for l in C.log_text()[m:].splitlines() if 'bubble' in l and ('side' in l or 'inside' in l or 'right' in l or 'below' in l)]
        j.step(f'R44 · the log says the side chosen and why ({sel}, {scroll or "top"})', bool(side), C.log_text()[m:].splitlines()[-8:])
        j.R.capture(f'{sel}-{scroll or "haut"}')
        j.check(f'{sel} open'); j.close()
    # the same with a conversation (the case the user met: it reopens, never under the page area)
    F.page('window.scrollTo(0,0)'); time.sleep(0.4)
    j.open_on('section.hero', 'hero'); j.send('section.hero', 'vrai-tour'); j.check('hero after a long answer'); j.close()
    for y in (0, 120, 300):
        F.page(f'window.scrollTo(0,{y})'); time.sleep(0.4)
        j.open_on('section.hero', f'hero with its conversation, scrolled {y}'); j.close()


@journey('r47_autorisation_defilement', 'R47 R9 R29 R39 (permission, then scroll up and back)')
def j_r47(j):
    F.scroll_into_view('div.plans', 'center')
    j.open_on('div.plans', 'plans')
    m = F.log_mark(); F.type_and_send('vrai-autorisation jetonperm', 'bubble'); j.inv.m.conv['div.plans'] = 'jetonperm'
    j.step('R9 · the permission is asked', F.wait_log(r'CHAT permission asked', m, 20)); time.sleep(0.8)
    st = F.chat_state('bubble'); sf = F.bubble_surface(st)
    j.step('R47 · the whole permission card is shown, Refuser and Autoriser inside the bubble, not greyed',
           st.get('perm') and len(st['perm']['buttons']) == 2 and all(b['op'] >= 0.99 for b in st['perm']['buttons']), st.get('perm'))
    j.R.capture('autorisation'); j.check('permission shown')
    F.front(); pg = F.page_area(); F.u32.SetCursorPos(pg[0] + 20, (pg[1] + pg[3]) // 2); time.sleep(0.2)
    F.wheel(-3, 0.08); time.sleep(0.8); j.check('scrolled up (wheel)', wheel_moved=False)
    F.wheel(3, 0.08); time.sleep(0.8); j.check('and back')
    st2 = F.chat_state('bubble')
    j.step('R47 · after the scroll, the card and its buttons are still whole', st2.get('perm') and len(st2['perm']['buttons']) == 2, st2.get('perm'))
    j.R.capture('autorisation-apres-defilement')
    j.step('R9 · « Autoriser » answers it', F.answer_permission('Autoriser', 'bubble', m))
    j.step('the turn ends with the answer in French', F.wait_log(r'CHAT turn t\d+ ended', m, 20) and "C'est fait" in F.chat_state('bubble').get('text', ''))
    j.check('after the answer')


@journey('r46_coller_mode_stable', 'R46 R28 (paste text while the session starts in « auto »)', agent_args='--new-delay 1500')
def j_r46(j):
    j.open_on('h1.hero-title', 'title')
    first = mode_label()
    F.set_clipboard('Rends ce titre plus court, s\'il te plaît')
    p = F.chat_point('[contenteditable=true]')
    F.click_at(*p); time.sleep(0.1)
    seen = []; t0 = time.time()
    F.tap(0x56, ctrl=True)   # Ctrl+V
    while time.time() - t0 < 4.0:
        seen.append(mode_label()); time.sleep(0.015)
    changed = sorted(set(x for x in seen if x))
    j.step('R46 · pasting text: the mode shown never changes (sampled every ~20 ms for 4 s)', changed == [first], {'first': first, 'seen': changed, 'samples': len(seen)})
    j.step('R46 · the text is pasted', 'plus court' in (F.chat("(document.querySelector('[contenteditable=true]')||{}).innerText||''") or ''))
    log = [l for l in C.log_text().splitlines() if 'mode' in l and 'CHAT' in l]
    j.R.data['measures']['modeLog'] = log[-8:]
    j.check('after the paste')
    m = F.log_mark(); F.focus_composer(); F.tap(0x0D)   # Entrée, real key
    seen = []; t0 = time.time()
    while time.time() - t0 < 3.0:
        seen.append(mode_label()); time.sleep(0.02)
    j.step('R46 · sending: the mode shown never changes either', sorted(set(x for x in seen if x)) == [first], sorted(set(x for x in seen if x)))
    F.wait_log(r'CHAT turn t\d+ ended', m, 20); j.inv.m.conv['h1.hero-title'] = 'plus court'
    j.check('after the turn')


@journey('e4_autre_application', 'E4 R18 (another application in front, then back)')
def j_e4(j):
    j.open_on('a.btn-primary', 'A'); j.send('a.btn-primary', 'reply'); j.check('A conversation')
    with F.OtherApp() as o:
        time.sleep(0.8)
        shown, rb = F.bubble_shown()
        j.step('E4 · another application in front: the bubble is hidden, not closed', o.in_front() and not shown and 'bubble closed' not in C.log_text()[-3000:], {'shown': shown, 'front': o.in_front()})
        j.inv.m.hidden_by_other = True
        j.check('other application in front')
    F.front(); time.sleep(0.8); j.inv.m.hidden_by_other = False
    j.check('back to the browser: the bubble is back at its place')
    with F.OtherApp():
        time.sleep(0.6); j.inv.m.hidden_by_other = True
        j.open_on('a.btn-primary', 'other application in front, a click on A', front=False)
    j.check('A on screen as it was')
    with F.OtherApp():
        time.sleep(0.6)
        j.open_on('h1.hero-title', 'other application in front, a click on B (no conversation)', front=False)
    j.check('B on screen')


@journey('abab', 'R6 R18 (A → B → A → B, slow then fast)')
def j_abab(j):
    j.open_on('a.btn-primary', 'A'); j.send('a.btn-primary', 'reply')
    j.open_on('div.hero-visual', 'B'); j.send('div.hero-visual', 'reply')
    for i, s in enumerate(['a.btn-primary', 'div.hero-visual', 'a.btn-primary', 'div.hero-visual']):
        j.open_on(s, f'slow {i + 1}'); j.check(f'slow {i + 1}')
    F.front()
    for s in ['a.btn-primary', 'div.hero-visual', 'a.btn-primary']:
        shown, rb = F.bubble_shown(); p = F.element_point(s, avoid=rb if isinstance(rb, tuple) else None); F.click_at(*p, settle=0.02); time.sleep(0.18)
    j.open_on('div.hero-visual', 'fast A-B-A-B, the last one'); j.check('after the fast series')


@journey('r38_r40_diff_poignee', 'R36 R38 R40 (a real edit, the diff « en grand », its handle)')
def j_diff(j):
    sel = 'div.hero-actions'
    j.open_on(sel, 'hero-actions')
    m = F.log_mark(); F.type_and_send('apply ' + F.GAP_TO_8, 'bubble'); j.inv.m.conv[sel] = 'gap: 8px'
    F.wait_log(r'CHAT permission asked', m, 20); j.step('permission answered', F.answer_permission('Autoriser', 'bubble', m))
    j.step('the edit is done, the page shows it (R36: reload with fade, or the style glides)', F.wait_log(r'CHAT turn t\d+ ended', m, 20)
           and F.wait_log(r'ANNOTATE mode re-armed after a reload|the page shows them \(transition\)', m, 10))
    time.sleep(1.5); j.inv.rebase(); j.check('after the edit')
    s0 = F.bubble_surface()
    j.step('the changes bar is there', F.real_click_chat('.bt-bar-main')); time.sleep(0.8)
    s1 = F.bubble_surface(); st = F.chat_state('bubble')
    j.step('R38/R40 · the diff opens in the bubble at its place, the same size', s0 and s1 and abs((s1[2] - s1[0]) - (s0[2] - s0[0])) <= 2 and abs(s1[0] - s0[0]) <= 1 and st.get('diff'), {'before': s0, 'diff': s1})
    j.R.capture('diff-ouvert'); j.check('diff open')
    g = F.chat_point('.b-grip')
    if j.step('R40 · the handle is shown', g):
        F.drag(g[0], g[1], g[0] + 180, g[1] + 120, 24)
        s2 = F.bubble_surface()
        j.step('R40 · the handle widens and heightens it, its corner fixed', s2 and s2[2] - s2[0] > (s1[2] - s1[0]) + 100 and s2[3] - s2[1] > (s1[3] - s1[1]) + 60 and abs(s2[0] - s1[0]) <= 1 and abs(s2[1] - s1[1]) <= 1, {'diff': s1, 'resized': s2})
        j.R.capture('diff-agrandi'); j.check('resized', wheel_moved=True)
        F.escape_chat('bubble'); time.sleep(0.8); s3 = F.bubble_surface()
        j.step('R40 · Échap gives the size of before back, at the same place', s3 and abs((s3[2] - s3[0]) - (s0[2] - s0[0])) <= 2 and abs((s3[3] - s3[1]) - (s0[3] - s0[1])) <= 2 and abs(s3[0] - s0[0]) <= 1, {'before': s0, 'after': s3})
        j.check('diff closed')
    # undo the edit for the next runs (the site copy is reset at each launch anyway)
    m = F.log_mark(); F.type_and_send('apply ' + F.GAP_TO_4, 'bubble'); F.wait_log(r'CHAT permission asked', m, 20); F.answer_permission('Autoriser', 'bubble', m)
    F.wait_log(r'CHAT turn t\d+ ended', m, 20); time.sleep(1.5); j.inv.rebase(); j.check('edit undone')


@journey('r34_r35_bascules_mode', 'R34 R35 (Commentaire ↔ Chat à droite, fast, during and after a turn)')
def j_switch(j):
    j.open_on('a.btn-primary', 'A'); j.send('a.btn-primary', 'reply')
    for i in range(4):
        F.set_layout('panel'); j.inv.m.layout = 'panel'; j.inv.m.open = None
        st = F.chat_state('panel')
        j.step(f'R34 · switch {i + 1}: the panel page has the panel layout (full height, header)', st.get('w', 0) >= 340 and 'Nouveau chat' in st.get('text', '') or 'Historique' in st.get('text', ''), {'w': st.get('w'), 'h': st.get('h')})
        j.check(f'panel {i + 1}')
        F.set_layout('comment'); j.inv.m.layout = 'comment'; j.check(f'comment {i + 1}')
    # R35: the panel's draft never reaches a bubble
    F.set_layout('panel'); j.inv.m.layout = 'panel'
    F.focus_composer('panel'); F._ws_for(F.chat_target('panel')).call('Input.insertText', {'text': 'brouillon-du-panneau'})
    F.front(); F.click_at(*F.element_point('div.hero-visual')); time.sleep(0.8)   # a chip in the panel's composer
    F.set_layout('comment'); j.inv.m.layout = 'comment'
    j.open_on('p.hero-text', 'an element after the panel\'s draft')
    comp = F.chat("(document.querySelector('[contenteditable=true]')||{}).innerText||''") or ''
    chips = F.chat("document.querySelectorAll('[contenteditable=true] .chip').length")
    j.step('R35 · the bubble\'s composer has neither the panel\'s text nor its chips', 'brouillon' not in comp and not chips, {'composer': comp, 'chips': chips})
    # during a turn
    j.open_on('a.btn-primary', 'A again'); m = F.log_mark(); F.type_and_send('vrai-lent jetonlent', 'bubble'); j.inv.m.conv['a.btn-primary'] = 'jetonlent'
    time.sleep(0.5)
    for i in range(3):
        F.set_layout('panel'); j.inv.m.layout = 'panel'; j.inv.m.open = None; j.check(f'panel during the turn {i + 1}')
        F.set_layout('comment'); j.inv.m.layout = 'comment'; j.check(f'comment during the turn {i + 1}')
    F.wait_log(r'CHAT turn t\d+ ended', m, 30)


@journey('r31_ctrl_maintenu', 'R31 (Ctrl held: the site as usual)')
def j_ctrl(j):
    F.front(); import chat_qa_comment_v4b as V4
    V4.system_cursors()
    p = F.element_point('h1.hero-title'); F.u32.SetCursorPos(*p); time.sleep(0.5)
    aim = V4.cursor_now()
    F.key(0x11); time.sleep(0.4)
    try:
        F.u32.SetCursorPos(p[0] + 5, p[1] + 2); time.sleep(0.4)
        held = V4.cursor_now()
        hover = [x for x in F.overlay_nodes() if x['cls'].split()[:1] == ['x'] and 'opacity: 1' in x['style']]
        m = F.log_mark(); F.mouse(0x0002); time.sleep(0.03); F.mouse(0x0004); time.sleep(0.8)
        lines = C.log_text()[m:]
        j.step('R31 · Ctrl held: the system cursor, no hover frame', held in V4.SYSTEM.values() and held != aim and not hover, {'held': held, 'aim': aim, 'system': V4.SYSTEM, 'hover': len(hover)})
        j.step('R31 · Ctrl held: a click goes to the site (no element aimed, no bubble)', 'element aimed' not in lines and 'bubble opened' not in lines, lines[-600:])
    finally:
        F.key(0x11, True)
    time.sleep(0.4)
    j.step('R31 · Ctrl released: the aiming cursor is back', V4.cursor_now() == aim, {'now': V4.cursor_now(), 'aim': aim})
    j.check('after Ctrl')
    j.open_on('h1.hero-title', 'a click after Ctrl: selects again'); j.check('selected')


@journey('r42_modele', 'R42 R13 (a model of the same provider, then of another provider)')
def j_model(j):
    j.open_on('a.btn-primary', 'A'); j.send('a.btn-primary', 'reply')
    cur = F.chat_state('bubble').get('model') or ''
    F.real_click_chat('button.m'); time.sleep(0.6)
    menu = F.chat("Array.from(document.querySelectorAll('.menu .mh,.menu .mi')).map(function(e){return (e.classList.contains('mh')?'#':'')+e.innerText.replace(/\\n+/g,' | ')})") or []
    j.R.data['measures']['menu'] = menu
    groups = [x[1:] for x in menu if x.startswith('#')]
    target = 'Opus 5.5' if 'Opus' not in cur else 'Sonnet 5.5'
    m = F.log_mark(); F.real_click_chat(text=target); time.sleep(0.8)
    st = F.chat_state('bubble')
    warn = F.chat("!!document.querySelector('.mwarn')")
    j.step(f'R42 · {target} (same provider): no « Changer d\'agent », the same conversation, « Passé à » in the thread',
           not warn and j.inv.m.conv['a.btn-primary'] in st.get('text', '') and 'Passé à' in st.get('text', ''), {'warn': warn, 'model': st.get('model')})
    j.inv.m.model = st.get('model'); j.check('model changed by the user')
    j.send('a.btn-primary', 'reply', token=j.inv.m.conv['a.btn-primary'] + 'b')
    j.step('R42 · the next message goes to the same session', 'new conversation' not in C.log_text()[m:] and 'session starting' not in C.log_text()[m:], C.log_text()[m:][-800:])
    other = [g for g in groups if 'Codex' in g or 'OpenAI' in g]
    if not other:
        j.R.data['measures']['otherProvider'] = 'not testable: one provider connected in the test copy (menu: %s)' % groups
        print('SKIP R42 (b): only one provider connected in the test copy', groups)
        return
    F.real_click_chat('button.m'); time.sleep(0.6)
    names = [x for x in menu[menu.index('#' + other[0]) + 1:] if not x.startswith('#')]
    m = F.log_mark(); F.real_click_chat(text=names[0].split(' | ')[0]); time.sleep(0.8)
    st = F.chat_state('bubble'); j.inv.m.model = st.get('model')
    j.step('R42 · another provider: the same chat, no box, a mark', not F.chat("!!document.querySelector('.mwarn')") and 'Passé à' in st.get('text', ''), st.get('model'))
    j.send('a.btn-primary', 'reply', token=j.inv.m.conv['a.btn-primary'] + 'c')
    j.step('R42 · the history is given to the other agent', F.wait_log(r'the conversation so far given to the agent that took the chat over', m, 5))
    j.check('after the provider change')


@journey('adresse_nue', 'bare addresses in a message (the endless link parsing bug) + R43 R48')
def j_address(j):
    j.open_on('p.hero-text', 'text')
    m = F.log_mark(); F.type_and_send('vrai-adresse voir http://127.0.0.1:5183/landing.html et file:///C:/projet/a.css jetonurl', 'bubble'); j.inv.m.conv['p.hero-text'] = 'jetonurl'
    j.step('the turn ends', F.wait_log(r'CHAT turn t\d+ ended', m, 20)); time.sleep(0.8)
    st = F.chat_state('bubble')
    links = F.chat("document.querySelectorAll('.scroller a').length") or 0
    j.step('the thread is drawn with the addresses (user and agent), links made, no JS error', 'jetonurl' in st.get('text', '') and 'https://example.test/a_(b)?x=1&y=2' in st.get('text', '') and links >= 3 and not F.ERRORS.collect(), {'links': links})
    j.R.capture('adresses'); j.check('addresses shown')


@journey('r43_r48_annonces_markdown', 'R8 R43 R48 R12 (announcements, long Markdown, code blocks, no blue caret)')
def j_announce(j):
    j.open_on('a.btn-primary', 'A')
    m = F.log_mark(); F.type_and_send('vrai-tour jetontour', 'bubble'); j.inv.m.conv['a.btn-primary'] = 'jetontour'
    follow = caret = 0; samples = 0; positions = []
    while not F.wait_log(r'CHAT turn t\d+ ended', m, 0.02):
        s = F.chat("(function(){var t=[...document.querySelectorAll('.turn.bt')].pop();if(!t)return null;var vis=function(e){var r=e.getBoundingClientRect();return r.height>0&&getComputedStyle(e).display!=='none'&&!e.hidden};"
                   "var f=[...t.querySelectorAll('.bt-follow')].filter(vis);var c=[...document.querySelectorAll('.caret,.cursor,.blink,[class*=caret]')].filter(vis);"
                   "return {f:f.length,card:f.some(function(e){return getComputedStyle(e).backgroundColor!=='rgba(0, 0, 0, 0)'}),caret:c.length}})()")
        if s:
            samples += 1; follow += bool(s['f']) and not s['card']; caret += s['caret']
        sf = F.bubble_surface()
        if sf:
            positions.append(sf)
        if samples > 600:
            break
    j.step('R43 · during the turn: grey follow-up lines, no card', follow > 0, {'samples': samples, 'withFollow': follow})
    j.step('R48 · no caret block at the end of the streamed text', caret == 0, caret)
    lefts = set(p[0] for p in positions); tops = set(p[1] for p in positions); bottoms = set(p[3] for p in positions)
    j.step('R29 · during the turn the bubble never moves (left and its anchored edge fixed)', len(lefts) <= 1 and (max(tops) - min(tops) <= 1 or max(bottoms) - min(bottoms) <= 1) if positions else False,
           {'lefts': sorted(lefts)[:5], 'tops': (min(tops), max(tops)) if tops else None, 'bottoms': (min(bottoms), max(bottoms)) if bottoms else None})
    time.sleep(1.2)
    end = F.chat("(function(){var t=[...document.querySelectorAll('.turn.bt')].pop();return {follow:t.querySelectorAll('.bt-follow:not([hidden])').length,pre:t.querySelectorAll('pre').length,table:t.querySelectorAll('table').length,links:t.querySelectorAll('a').length,"
                 "hist:(document.body.innerText||'').includes('Historique'),logo:!!t.querySelector('svg')}})()")
    j.step('R43 · at the end: the lines joined « Historique », the answer stays (code blocks, table, links), in French', end and end['hist'] and end['pre'] >= 2 and end['links'] >= 2 and "C'est fait" in F.chat_state('bubble').get('text', ''), end)
    j.step('R12 · the provider\'s logo in the answer\'s header', end and end['logo'], end)
    j.R.capture('reponse-longue'); j.check('after the long answer')


@journey('r20_r30_r01_bulle_forme', 'R1 R20 R30 R21 R4 R13 R28')
def j_shape(j):
    j.open_on('h2.section-title', 'title', scroll='center')
    st = F.chat_state('bubble'); sf = F.bubble_surface(st)
    w = (sf[2] - sf[0]) / st['dpr'] if sf else 0
    j.step('R20 · the bubble is 320 px wide', abs(w - 320) <= 1, w)
    if sf:
        k = st['dpr']; corner = F.drawn_share((sf[0], sf[1], sf[0] + int(4 * k), sf[1] + int(4 * k)))
        inner = F.drawn_share((sf[0] + int(14 * k), sf[1] + int(14 * k), sf[0] + int(30 * k), sf[1] + int(30 * k)))
        j.step('R1 · rounded corners (the corner shows the page, inside is the surface)', corner < 0.5 and inner > 0.9, {'corner': corner, 'inside': inner})
    mode_txt = F.chat("(document.querySelector('button.tb.mode')||{}).innerText||''") or ''
    j.step('R21 · the mode button is an icon alone (its name in the accessible label)', not mode_txt.strip() and (st.get('mode') or '').startswith('Mode'), {'text': mode_txt, 'label': st.get('mode')})
    j.step('R13/R28 · the model button: model · effort, never « Claude Code »', st.get('model') and '·' in st['model'] and 'Claude Code' not in st['model'], st.get('model'))
    F.real_click_chat('button.tb.mode'); time.sleep(0.6)
    rows = F.chat("Array.from(document.querySelectorAll('.menu .mi')).map(function(b){return b.innerText.replace(/\\n+/g,' | ')})") or []
    want = ['Accept edits | Automatically accept all file edits', 'Auto | Claude handles permission decisions', 'Demander | Propose chaque modification.', 'Planifier | Propose un plan, ne touche à aucun fichier.']
    j.step('R4 · the mode menu: 4 lines, this order, these words', [r.strip() for r in rows] == want, rows)
    j.R.capture('menu-mode'); F.close_menus()
    plus = F.chat("document.body.innerText.indexOf('Choisir un autre dossier')<0")
    j.step('R5 · nowhere « Choisir un autre dossier »', plus)
    j.check('shape')


@journey('r14_r45_etiquettes', 'R14 R33 R45 (labels identical on hover, during the capture handoff and selected; 100 / 125 / 150 %)')
def j_labels(j):
    """The name label filmed (GDI, every ~8 ms) from the hover, through the click (the native copy of the frame takes over
    while the page is captured) to the selection settled: every image of its box equals the hover image."""
    import chat_qa_comment_v4b as V4
    from PIL import Image
    base = F.dpr()

    def zoom_to(ratio):
        F.front(); F.tap(0x30, ctrl=True); time.sleep(0.5)   # Ctrl+0
        for _ in range(8):
            if F.dpr() / base >= ratio - 0.02:
                break
            F.tap(0xBB, ctrl=True); time.sleep(0.5)          # Ctrl+=
        return round(F.dpr() / base, 2)

    def img(fr):
        w_, h_, raw = fr
        return Image.frombuffer('RGBA', (w_, h_), raw, 'raw', 'BGRA', 0, 1).convert('RGB')

    def differs(a, b):
        pa, pb = a.load(), b.load(); n = d = 0
        for y in range(0, a.size[1]):
            for x in range(0, a.size[0], 2):
                n += 1; d += sum(abs(u - v) for u, v in zip(pa[x, y], pb[x, y])) > 24
        return d / max(1, n)

    for ratio in (1.0, 1.25, 1.5):
        got = zoom_to(ratio)
        if abs(got - ratio) > 0.03:
            j.R.data['measures'][f'zoom-{ratio}'] = f'not reached (got {got})'
            j.step(f'R45 · zoom {int(ratio * 100)} % reached (Ctrl+=)', False, got); continue
        for sel in ('h2.section-title', 'a.btn-primary', 'div.plans'):
            F.scroll_into_view(sel, 'center'); F.front(); time.sleep(0.3)
            el = F.element_screen(sel); k = F.dpr(); pg = F.page_area()
            box = (max(pg[0], el[0] - int(4 * k)), max(pg[1], el[1] - int(28 * k)), min(pg[2], el[0] + int(300 * k)), min(pg[3], el[1] + int(4 * k)))
            p = F.element_point(sel); F.u32.SetCursorPos(*p); time.sleep(0.8)
            ref = img(V4.fast_grab(box)); tag = f'{int(ratio * 100)}-{sel}'
            ref.save(j.R.dir / f'{tag}-survol.png')
            film = V4.FastFrames(box); film.start(); time.sleep(0.05)
            m = F.log_mark(); t = time.perf_counter(); F.mouse(0x0002); time.sleep(0.03); F.mouse(0x0004)
            time.sleep(1.4); film.stop = True; film.join()
            shares = [round(differs(ref, img(fr)), 4) for _, fr in film.frames]
            worst = max(range(len(shares)), key=lambda i: shares[i]) if shares else None
            if worst is not None:
                img(film.frames[worst][1]).save(j.R.dir / f'{tag}-pire-image.png')
                img(film.frames[-1][1]).save(j.R.dir / f'{tag}-selection.png')
            phases = {'before': shares[:3], 'during (max)': max(shares[3:-10] or [0]), 'after': shares[-3:]}
            j.step(f'R45 · {sel} at {int(ratio * 100)} %: the label is the same before, during the capture handoff and after (every image, < 1 % pixels)',
                   shares and max(shares) < 0.01, {'images': len(shares), 'worst': shares[worst] if worst is not None else None, 'worstMs': round((film.frames[worst][0] - t) * 1000) if worst is not None else None, **phases})
            j.inv.m.open = sel; j.inv.rebase()
            j.close()
    zoom_to(1.0)
    j.check('labels')


@journey('r07_r15_r19_r22_details', 'R7 R15 R19 R22 (no chip under the message, labels 2 px from the window\'s edge, menus without a jump, « + » direct)')
def j_details(j):
    import chat_qa_comment_v4b as V4
    from PIL import Image
    F.page('window.scrollTo(0,0)'); time.sleep(0.4); F.front()
    el = F.element_screen('section.hero'); k = F.dpr(); pg = F.page_area()
    band = (pg[0], max(pg[1], el[1] - int(26 * k)), pg[0] + int(60 * k), max(pg[1], el[1] - int(26 * k)) + int(24 * k))
    F.u32.SetCursorPos(pg[2] - int(40 * k), pg[3] - 4); time.sleep(0.5)   # nothing hovered
    before = V4.fast_grab(band)
    j.open_on('section.hero', 'hero (full width)')
    after = V4.fast_grab(band)
    ia = Image.frombuffer('RGBA', before[:2], before[2], 'raw', 'BGRA', 0, 1).convert('RGB'); ib = Image.frombuffer('RGBA', after[:2], after[2], 'raw', 'BGRA', 0, 1).convert('RGB')
    edge = int(2 * k)
    def changed(x0, x1):
        pa, pb = ia.load(), ib.load(); n = d = 0
        for y in range(ia.size[1]):
            for x in range(x0, x1):
                n += 1; d += sum(abs(u - v) for u, v in zip(pa[x, y], pb[x, y])) > 24
        return d / max(1, n)
    outer, inner = changed(0, max(1, edge - 1)), changed(edge + 1, ia.size[0])
    j.step('R15 · the label keeps 2 px from the window\'s left edge (nothing drawn in the first 2 px, the label right after)', outer < 0.02 and inner > 0.05, {'first2px': round(outer, 3), 'after': round(inner, 3)})
    ib.save(j.R.dir / 'r15-bord-gauche.png')
    j.send('section.hero', 'reply')
    chips = F.chat("document.querySelectorAll('.scroller .chip').length")
    j.step('R7 · no element chip under the user\'s message in the bubble', not chips and 'section.hero' not in F.chat_state('bubble').get('text', ''), chips)
    plus = F.chat("(function(){var b=Array.from(document.querySelectorAll('button.tb')).find(function(e){return /Joindre/.test(e.getAttribute('aria-label')||'')});return b?{popup:b.getAttribute('aria-haspopup')}:null})()")
    j.step('R22 · « + » opens the image picker directly (no menu) — not clicked: a system dialog', plus and plus.get('popup') != 'menu', plus)
    for name, sel in (('modèle', 'button.m'), ('mode', 'button.tb.mode')):
        s0 = F.bubble_surface(); sc0 = (F.chat_state('bubble').get('scroller') or {}).get('st')
        top = (s0[0] + 4, s0[1] + 4, s0[2] - 4, s0[1] + int(60 * k)) if s0 else None
        g0 = V4.fast_grab(top) if top else None
        F.real_click_chat(sel); time.sleep(0.6)
        s1 = F.bubble_surface(); sc1 = (F.chat_state('bubble').get('scroller') or {}).get('st'); g1 = V4.fast_grab(top) if top else None
        same = g0 and g1 and g0[2] == g1[2]
        j.step(f'R19 · menu {name}: the surface and the thread stay still (same box, same scroll, the top of the thread the same image)', s0 == s1 and sc0 == sc1 and same, {'before': s0, 'open': s1, 'scroll': (sc0, sc1), 'sameImage': same})
        F.close_menus(); time.sleep(0.4)
    j.check('details')


@journey('r11_r23_r24_panneau', 'R11 R23 R24 (the panel: title, empty chat, suggestions)')
def j_panel(j):
    F.set_layout('panel'); j.inv.m.layout = 'panel'
    st = F.chat_state('panel')
    pos = F.chat("(function(){var e=document.getElementById('empty-h');if(!e)return null;var r=e.getBoundingClientRect();return [r.top,innerHeight]})()", 'panel')
    j.step('R23 · empty chat: « Que voulez-vous changer ? » at the bottom, above the composer', pos and pos[0] > pos[1] * 0.5, pos)
    sugs = F.chat("Array.from(document.querySelectorAll('.sug')).map(function(b){return b.innerText.trim()})", 'panel') or []
    j.step('R24 · no « annotations en attente » suggestion, none twice', not any('annotation' in s.lower() for s in sugs) and len(sugs) == len(set(sugs)), sugs)
    j.R.capture('panneau-vide')
    F.focus_composer('panel'); m = F.log_mark(); F.type_and_send('reply jetonpanneau', 'panel'); F.wait_log(r'CHAT turn t\d+ ended', m, 20); time.sleep(0.8)
    F.real_click_chat('.hd-title', kind='panel'); time.sleep(0.6)
    hist = F.chat("!document.getElementById('history').hidden", 'panel')
    j.step('R11 · a click on the title does not open the history', not hist)
    p = F.chat_point('.hd-title', kind='panel')
    if p:
        F.click_at(*p); time.sleep(0.05); F.mouse(0x0002); time.sleep(0.02); F.mouse(0x0004); time.sleep(0.5)
    field = F.chat("!!document.querySelector('.hd-title input, .hd input')", 'panel')
    j.step('R11 · a double click on the title renames it (a field in place)', field)
    F.escape_chat('panel'); time.sleep(0.4)
    j.step('R11 · Échap cancels', not F.chat("!!document.querySelector('.hd-title input, .hd input')", 'panel'))
    j.check('panel')


@journey('r26_survol_puce', 'R26 (hovering a chip scrolls the page to its element)')
def j_chip(j):
    F.set_layout('panel'); j.inv.m.layout = 'panel'
    F.page('window.scrollTo(0,0)'); time.sleep(0.4); F.front()
    F.click_at(*F.element_point('section.hero')); time.sleep(0.8)
    F.page('window.scrollTo(0,document.body.scrollHeight)'); time.sleep(0.6)
    y0 = F.page('scrollY')
    p = F.chat_point('[contenteditable=true] .chip', kind='panel')
    if not j.step('R26 · the chip is in the composer', p):
        return
    F.u32.SetCursorPos(*p); time.sleep(1.2)
    y1 = F.page('scrollY')
    hover = [x for x in F.overlay_nodes() if x['cls'].split()[:1] == ['x'] and 'opacity: 1' in x['style']]
    j.step('R26 · hovering the chip: the page scrolls to the element and shows it as on hover', y1 < y0 - 100 and hover, {'before': y0, 'after': y1, 'hover': len(hover)})
    F.u32.SetCursorPos(p[0] - 300, p[1] - 300); time.sleep(0.8)
    j.step('R26 · leaving the chip: no scroll back', abs(F.page('scrollY') - y1) < 4)
    j.check('chip')


@journey('r09_rapide_bulle_panneau', 'R9 (first text and end of turn as fast in the bubble as in the panel)')
def j_speed(j):
    def run(kind, sel=None):
        if kind == 'bubble':
            j.open_on(sel, 'speed bubble')
        m = F.log_mark(); t0 = time.time(); F.type_and_send('vrai-tour jetonvitesse', kind)
        first = F.wait_log(r'CHAT first agent text[^\n]*?(\d+) ms', m, 20); F.wait_log(r'CHAT turn t\d+ ended', m, 30)
        return time.time() - t0, first.group(0) if first else None
    tb, fb = run('bubble', 'a.btn-primary'); j.inv.m.conv['a.btn-primary'] = 'jetonvitesse'; j.close()
    F.set_layout('panel'); j.inv.m.layout = 'panel'
    tp, fp = run('panel')
    j.step('R9 · the bubble is as fast as the panel (end of turn within 1.3×)', tb <= tp * 1.3 + 0.5, {'bubble s': round(tb, 2), 'panel s': round(tp, 2), 'firstBubble': fb, 'firstPanel': fp})


@journey('erreurs_agent', 'errors: 529 overload, a failed tool, a stop (« Arrêter »), slowness')
def j_errors(j):
    j.open_on('a.btn-primary', 'A')
    m = F.log_mark(); F.type_and_send('vrai-surcharge jetonerr', 'bubble'); j.inv.m.conv['a.btn-primary'] = 'jetonerr'
    F.wait_log(r'CHAT turn t\d+ ended', m, 20); time.sleep(0.8)
    st = F.chat_state('bubble')
    j.step('529: an error the user understands, in French, the bubble whole', 'surcharg' in st.get('text', '').lower() or 'indisponible' in st.get('text', '').lower() or 'réessay' in st.get('text', '').lower(), st.get('text', '')[-400:])
    j.R.capture('erreur-529'); j.check('after 529')
    m = F.log_mark(); F.type_and_send('vrai-erreur-outil', 'bubble'); F.wait_log(r'CHAT turn t\d+ ended', m, 20); time.sleep(0.6); j.check('after a failed tool')
    m = F.log_mark(); F.type_and_send('vrai-lent', 'bubble'); time.sleep(3.2)
    j.step('« Arrêter » stops a slow turn', F.real_click_chat(text='Arrêter') and F.wait_log(r'CHAT turn t\d+ ended', m, 10))
    time.sleep(0.6); j.check('after the stop')


# ---------------------------------------------------------------- the independent review's sequences (design/chat-final-review.md, « Pour Q »)
def _sid_of(token):
    """The fake agent's session id that received the prompt holding `token` (its log of every received line)."""
    try:
        for line in F.FLOG.read_text(encoding='utf-8', errors='replace').splitlines():
            if '"session/prompt"' in line and token in line:
                m = re.search(r'"sessionId":"([^"]+)"', line)
                if m:
                    return m.group(1)
    except OSError:
        pass
    return None


@journey('c2_envoi_dans_b_apres_tour_de_a', 'review C2 (A works, open B, wait for the end of A\'s turn, send in B)')
def j_c2(j):
    j.open_on('div.hero-visual', 'B first'); j.send('div.hero-visual', 'reply', token='jetonB1'); j.close()
    j.open_on('a.btn-primary', 'A'); m = F.log_mark(); F.type_and_send('vrai-lent jetonA1', 'bubble'); j.inv.m.conv['a.btn-primary'] = 'jetonA1'
    time.sleep(0.8)
    j.open_on('div.hero-visual', 'B while A works'); j.check('B open, A working')
    j.step('A\'s turn ends', F.wait_log(r'CHAT turn t\d+ ended', m, 20)); time.sleep(0.8); j.check('A done, B still shown')
    m2 = F.log_mark(); F.type_and_send('reply jetonB2', 'bubble'); j.inv.m.conv['div.hero-visual'] = 'jetonB2'
    j.step('C2 · B\'s turn ends', F.wait_log(r'CHAT turn t\d+ ended', m2, 20)); time.sleep(0.8)
    j.inv.sent('bubble', 'jetonB2', 'C2 send in B')
    st = F.chat_state('bubble')
    j.step('C2 · the message and its answer are in B\'s bubble (with B\'s first message)', 'jetonB1' in st.get('text', '') and 'Bonjour' in st.get('text', ''), st.get('text', '')[-300:])
    j.step('C2 · the fake agent got it in B\'s session, not A\'s', _sid_of('jetonB2') and _sid_of('jetonB2') != _sid_of('jetonA1'), {'A': _sid_of('jetonA1'), 'B2': _sid_of('jetonB2'), 'B1': _sid_of('jetonB1')})
    j.close(); j.open_on('a.btn-primary', 'A again')
    j.step('C2 · A\'s conversation does not hold B\'s message', 'jetonB2' not in F.chat_state('bubble').get('text', ''))
    j.check('end')


@journey('c1_clic_avant_page_prete', 'review C1 (a click right after the start, before the chat page is ready: the conversation, the model, Envoyer)')
def j_c1(j):
    j.open_on('a.btn-primary', 'A'); j.send('a.btn-primary', 'reply', token='jetonC1'); j.close()
    F.release_all(); C.quit_all(); time.sleep(1.0)
    F.launch(fresh=False)   # the same profile: A's conversation is on disk
    j.inv.m.open = None; j.inv.log_at = F.log_mark(); j.inv.uia_base = None
    F.front(); m = F.log_mark(); C.shortcut('A'); F.wait_log(r'ANNOTATE mode on', m, 10)
    F.set_layout('comment')
    p = F.element_point('a.btn-primary'); t = time.perf_counter(); F.click_at(*p, settle=0.02)
    j.inv.after_element_click('a.btn-primary', t, 'C1 first click after the start', mark=m)
    samples = []; t0 = time.time()
    while time.time() - t0 < 3:
        st = F.chat_state('bubble'); samples.append((round(time.time() - t0, 2), st.get('model'), 'jetonC1' in (st.get('text') or '')))
        time.sleep(0.1)
    bad = [s for s in samples if not s[1] or 'Agent' == (s[1] or '').split(':')[-1].strip() or not s[2]]
    j.step('C1 · from the first image: the conversation, the model · effort (never « Agent »)', not bad, bad[:5])
    j.R.capture('c1-premier-clic'); j.check('C1')


@journey('c3_element_disparait_pendant_tour', 'review C3 (the element renamed by the agent during a turn with a permission; removed without a turn)')
def j_c3(j):
    j.open_on('p.hero-text', 'text')
    m = F.log_mark(); F.type_and_send('vrai-autorisation jetonC3', 'bubble'); j.inv.m.conv['p.hero-text'] = 'jetonC3'
    j.step('permission asked', F.wait_log(r'CHAT permission asked', m, 20))
    F.page("(function(){var e=document.querySelector('p.hero-text');e.className='hero-copy';return 1})()"); time.sleep(1.2)
    shown, _ = F.bubble_shown()
    j.step('C3 · the element renamed during the turn: the bubble stays open', shown and 'no longer in the page' not in C.log_text()[m:], [l for l in C.log_text()[m:].splitlines() if 'bubble' in l][-4:])
    j.step('C3 · the permission is still answered in the bubble', F.answer_permission('Autoriser', 'bubble', m) and F.wait_log(r'CHAT turn t\d+ ended', m, 20))
    F.page("(function(){var e=document.querySelector('.hero-copy');e.className='hero-text';return 1})()"); time.sleep(0.5)
    j.inv.m.open = None; F.escape_chat('bubble'); time.sleep(0.5)
    j.open_on('h1.hero-title', 'title (no turn)')
    j.inv.allow_close = True
    F.page("(function(){document.querySelector('h1.hero-title').remove();return 1})()"); time.sleep(1.0)
    j.check('the element removed without a turn: the bubble may close')


@journey('e2_modele_sur_deux_elements', 'review E2 + arbitration (the model, effort and mode chosen are kept for every new element)')
def j_e2(j):
    j.open_on('a.btn-primary', 'A')
    F.real_click_chat('button.m'); time.sleep(0.6); F.real_click_chat(text='Opus 5.5'); time.sleep(0.8); F.close_menus()
    F.real_click_chat('button.tb.mode'); time.sleep(0.6); F.real_click_chat(text='Accept edits'); time.sleep(0.6); F.close_menus()
    st = F.chat_state('bubble'); j.inv.m.model, j.inv.m.mode = st.get('model'), st.get('mode')
    j.step('Opus 5.5 and « Accept edits » chosen in A', 'Opus' in (st.get('model') or '') and 'Accept' in (st.get('mode') or ''), st)
    j.send('a.btn-primary', 'reply', token='jetonE2a'); j.close()
    j.open_on('div.hero-visual', 'B (new)')
    seen = set(); t0 = time.time()
    while time.time() - t0 < 3.5:
        s = F.chat_state('bubble'); seen.add((s.get('model'), s.get('mode'))); time.sleep(0.05)
    j.step('E2 · in B, from the first image and for 3.5 s: the same model, effort and mode', seen == {(j.inv.m.model, j.inv.m.mode)}, sorted(map(str, seen)))
    j.send('div.hero-visual', 'reply', token='jetonE2b')
    sid = _sid_of('jetonE2b'); flog = F.FLOG.read_text(encoding='utf-8', errors='replace') if F.FLOG.exists() else ''
    applied = [l for l in flog.splitlines() if sid and sid in l and ('set_config_option' in l or 'set_mode' in l)]
    j.step('E2 · B\'s session got the model (opus) and the mode (acceptEdits) before its prompt', any('opus' in l for l in applied) and any('acceptEdits' in l for l in applied), applied[:6])
    j.check('E2')


@journey('e3_choix_dans_b_pendant_tour_de_a', 'review E3 (a mode chosen in B while A works never reaches A\'s session)')
def j_e3(j):
    j.open_on('a.btn-primary', 'A'); m = F.log_mark(); F.type_and_send('vrai-lent jetonE3a', 'bubble'); j.inv.m.conv['a.btn-primary'] = 'jetonE3a'; time.sleep(0.8)
    j.open_on('div.hero-visual', 'B while A works')
    F.real_click_chat('button.tb.mode'); time.sleep(0.6); F.real_click_chat(text='Accept edits'); time.sleep(0.6); F.close_menus()
    j.inv.m.mode = F.chat_state('bubble').get('mode')
    time.sleep(0.5); sa = _sid_of('jetonE3a')
    flog = F.FLOG.read_text(encoding='utf-8', errors='replace') if F.FLOG.exists() else ''
    leaked = [l for l in flog.splitlines() if sa and sa in l and 'acceptEdits' in l]
    j.step('E3 · « Accept edits » chosen in B is not sent to A\'s session', sa and not leaked, leaked[:3])
    F.wait_log(r'CHAT turn t\d+ ended', m, 20); j.check('E3')


@journey('e4_defilement_puis_menu', 'review E4 (scroll 150 px, then a menu: the surface never shifts, 60+ images per second)')
def j_e4b(j):
    import chat_qa_comment_v5 as V5
    F.scroll_into_view('div.plans', 'center')
    j.open_on('div.plans', 'plans'); j.send('div.plans', 'reply')
    F.front(); pg = F.page_area(); F.u32.SetCursorPos(pg[0] + 20, (pg[1] + pg[3]) // 2); time.sleep(0.2)
    F.wheel(1, 0.05); time.sleep(0.9)
    sf = F.bubble_surface(); j.inv.rebase()
    if not j.step('E4 · the bubble is on screen after the scroll', sf):
        return
    region = (sf[0] - 40, sf[1] - 40, sf[2] + 40, sf[3] + 40)
    film = V5.Frames(region, 90, 0.010); film.start(); time.sleep(0.05)
    F.real_click_chat('button.m'); time.sleep(0.7); film.stop = True; film.join()
    x = (sf[2] - sf[0]) // 2 + 40; tops = []
    for _, fr in film.frames:
        r = V5.run_of(fr, x, V5.dark, 0, fr[1] - 1)
        tops.append(r[0] if r else None)
    vals = [t for t in tops if t is not None]
    j.step('E4 · while the menu opens, the surface\'s top edge does not move (every image)', vals and max(vals) - min(vals) <= 1, {'images': len(tops), 'tops': sorted(set(vals))[:8]})
    for i, (_, fr) in enumerate(film.frames[:: max(1, len(film.frames) // 6)][:6]):
        V5.save_png(fr, f'e4-{i:02d}.png')
    F.close_menus(); j.check('E4')


@journey('e1_iframe_composant_web', 'review E1 (an iframe of the same origin, a web component with an open shadow root)')
def j_e1(j):
    F.inject_embedded()
    for host in ('#qa-frame', '#qa-card'):
        F.scroll_into_view(host, 'center'); F.front()
        p = F.embedded_point(host); m = F.log_mark(); t = time.perf_counter(); F.click_at(*p, settle=0.04)
        j.n += 1; j.inv.after_element_click(host, t, f'{j.n:02d} E1 click inside {host}', mark=m)
        time.sleep(1.5)
        j.step(f'E1 · {host}: the bubble stays open (never « element no longer in the page »)', F.bubble_shown()[0] and 'no longer in the page' not in C.log_text()[m:], [l for l in C.log_text()[m:].splitlines() if 'bubble' in l][-5:])
        j.send(host, 'reply'); j.check(f'{host} after a send')
        j.close(); F.scroll_into_view(host, 'center'); p = F.embedded_point(host)
        m = F.log_mark(); t = time.perf_counter(); F.click_at(*p, settle=0.04)
        j.n += 1; j.inv.after_element_click(host, t, f'{j.n:02d} E1 {host} again, its conversation', mark=m)
        j.R.capture(f'e1-{host[1:]}'); j.close()


@journey('m5_bascule_puis_clic', 'review M5 (the Commentaire switch, then a click on an element 50 ms later, while the panel slides out)')
def j_m5(j):
    for i in range(3):
        F.set_layout('panel'); j.inv.m.layout = 'panel'; time.sleep(0.8)
        b = j.uia.bounds(['AnnotationLayoutComment']).get('AnnotationLayoutComment') if j.uia else None
        if not j.step(f'M5 · the « Commentaire » button located ({i + 1})', b):
            return
        p = F.element_point('a.btn-primary')
        F.front(); F.click_at((b[0] + b[2]) // 2, (b[1] + b[3]) // 2, settle=0.03); time.sleep(0.05)
        m = F.log_mark(); t = time.perf_counter(); F.click_at(*p, settle=0.0)
        j.inv.m.layout = 'comment'
        j.n += 1; j.inv.after_element_click('a.btn-primary', t, f'{j.n:02d} M5 switch then click {i + 1}', mark=m)
        st = F.chat_state('bubble')
        j.step(f'M5 · the bubble has the bubble layout (narrow, no panel header) ({i + 1})', st.get('w', 999) < 700 and 'Nouveau chat' not in (st.get('text') or '')[:40], {'w': st.get('w')})
        j.check(f'M5 {i + 1}'); j.close()


@journey('m4_clavier_apres_premier_plan', 'review M4 (back from another application: typing reaches the bubble; Échap closes the bubble, not the mode)')
def j_m4(j):
    j.open_on('h1.hero-title', 'title')
    F.click_at(*F.chat_point('[contenteditable=true]')); time.sleep(0.2)
    with F.OtherApp():
        time.sleep(0.6); j.inv.m.hidden_by_other = True
    F.front(); time.sleep(0.8); j.inv.m.hidden_by_other = False
    F.type_keys('abc'); time.sleep(0.4)
    comp = F.chat("(document.querySelector('[contenteditable=true]')||{}).innerText||''") or ''
    j.step('M4 · back from another application, the keyboard writes in the bubble', 'abc' in comp, comp)
    m = F.log_mark(); F.tap(0x1B); time.sleep(0.8); j.inv.allow_close = True
    j.step('M4 · Échap (real key) closes the bubble and keeps the mode', not F.bubble_shown()[0] and 'ANNOTATE mode off' not in C.log_text()[m:], C.log_text()[m:][-400:])
    j.inv.m.open = None; j.check('M4')
    # Échap with the keyboard in the page (after a click on its scrollbar)
    j.open_on('h1.hero-title', 'title again'); pg = F.page_area(); k = F.dpr()
    F.click_at(pg[2] - int(6 * k), pg[1] + int(30 * k)); time.sleep(0.3)
    m = F.log_mark(); F.tap(0x1B); time.sleep(0.8); j.inv.allow_close = True
    j.step('M4 · Échap with the keyboard in the page: the bubble closes, the mode stays', 'ANNOTATE mode off' not in C.log_text()[m:], C.log_text()[m:][-400:])
    j.inv.m.open = None; j.check('M4 page')


@journey('m11_fermeture_pendant_tour', 'review M11 (the browser closed during a turn, opened again: no endless « Travaille »)')
def j_m11(j):
    j.open_on('a.btn-primary', 'A'); F.type_and_send('vrai-lent jetonM11', 'bubble'); time.sleep(1.5)
    F.release_all(); C.quit_all(); time.sleep(1.0)
    F.launch(fresh=False); j.inv.m.open = None; j.inv.log_at = F.log_mark(); j.inv.uia_base = None; j.inv.m.conv = {'a.btn-primary': 'jetonM11'}
    F.mode_on('comment'); j.open_on('a.btn-primary', 'A after the restart'); time.sleep(2.5)
    st = F.chat_state('bubble')
    j.step('M11 · the interrupted turn is closed (no ring, no « Arrêter », Envoyer usable)', not st.get('busy') and 'Travaille' not in (st.get('text') or ''), (st.get('text') or '')[-300:])
    j.R.capture('m11-apres-relance'); j.check('M11')


@journey('m15_defilement_puis_redimensionnement', 'review M15 (a long thread, scroll, then the window made lower: the bubble whole, its composer seen)')
def j_m15(j):
    F.scroll_into_view('div.plans', 'center')
    j.open_on('div.plans', 'plans'); j.send('div.plans', 'vrai-markdown'); j.send('div.plans', 'vrai-code', token='jetonM15b')
    F.front(); pg = F.page_area(); F.u32.SetCursorPos(pg[0] + 20, (pg[1] + pg[3]) // 2); F.wheel(1); time.sleep(0.9); j.inv.rebase()
    for h in (480, 400, 665):
        F.set_page_size(1470, h); time.sleep(1.2); j.inv.rebase()
        st = F.chat_state('bubble'); sf = F.bubble_surface(st); pg = F.page_area()
        comp = F.chat("(function(){var c=document.querySelector('[contenteditable=true]');var r=c.getBoundingClientRect();return [r.top,r.bottom,innerHeight]})()")
        whole = sf and sf[1] >= pg[1] - 1 and sf[3] <= pg[3] + 1
        j.step(f'M15 · page area {h} px high: the bubble is whole in it, its composer inside the surface', whole and comp and comp[1] <= st['app'][3] + 1, {'surface': sf, 'page': pg, 'composer': comp, 'app': st.get('app')})
        j.R.capture(f'm15-{h}'); j.check(f'M15 {h}')


@journey('m6_clic_sans_envoi_puis_panneau', 'review M6 (a click without a send, then the panel: nothing anchored, no mark after a send from the panel)')
def j_m6(j):
    F.scroll_into_view('div.plans', 'center')
    j.open_on('div.plans', 'plans, no send'); j.close()
    F.set_layout('panel'); j.inv.m.layout = 'panel'
    title = F.chat("(document.querySelector('.hd-title-text')||{}).textContent||''", 'panel')
    j.step('M6 · the panel\'s title is not the element clicked without a send', 'div.plans' not in title, title)
    m = F.log_mark(); F.type_and_send('reply jetonM6', 'panel'); F.wait_log(r'CHAT turn t\d+ ended', m, 20); time.sleep(0.6)
    F.set_layout('comment'); j.inv.m.layout = 'comment'
    j.step('M6 · div.plans has no conversation mark', len(F.conversation_marks()) == 0, F.conversation_marks())
    j.open_on('div.plans', 'plans again'); j.step('M6 · its bubble does not hold the panel\'s message', 'jetonM6' not in F.chat_state('bubble').get('text', ''))
    j.check('M6')


@journey('m16_rechargement_avant_clic', 'review M16 (F5 before any click in Commentaire: the mode stays)')
def j_m16(j):
    F.front(); m = F.log_mark()
    pg = F.page_area(); k = F.dpr(); F.click_at(pg[2] - int(6 * k), pg[1] + int(30 * k)); time.sleep(0.2)   # the page has the keyboard (scrollbar)
    F.tap(0x74); time.sleep(2.0)
    j.step('M16 · F5 before a click: the annotation mode stays on', 'ANNOTATE mode off' not in C.log_text()[m:] and F.wait_log(r'ANNOTATE mode re-armed after a reload|ANNOTATE mode on', m, 5), C.log_text()[m:][-500:])
    j.open_on('a.btn-primary', 'a click after the reload'); j.check('M16')


@journey('m2_jamais_l_ancien_fil', 'review M2 (opening B after A: never an image of A\'s conversation at B\'s place)')
def j_m2(j):
    j.open_on('a.btn-primary', 'A'); j.send('a.btn-primary', 'vrai-markdown', token='jetonM2a')
    F.front(); rb = F.bubble_shown()[1]; p = F.element_point('h1.hero-title', avoid=rb if isinstance(rb, tuple) else None)
    el = F.element_screen('h1.hero-title'); old = F.bubble_surface()
    F.click_at(*p, settle=0.02); t0 = time.time(); stale = []
    while time.time() - t0 < 1.0:
        shown, rb = F.bubble_shown()
        if shown and isinstance(rb, tuple) and rb != old and 'jetonM2a' in (F.chat_state('bubble').get('text') or ''):
            stale.append(round((time.time() - t0) * 1000))
        time.sleep(0.01)
    j.step('M2 · B\'s bubble never shows A\'s conversation, not even one image', not stale, stale[:5])
    j.inv.m.open = 'h1.hero-title'; j.inv.rebase(); j.check('M2')



@journey('m1_arreter_agent_sourd', 'review M1 (« Arrêter » while the agent never answers the cancel: the turn ends within ~5 s, the next message works)')
def j_m1(j):
    j.open_on('a.btn-primary', 'A')
    m = F.log_mark(); F.type_and_send('vrai-sourd jetonM1', 'bubble'); j.inv.m.conv['a.btn-primary'] = 'jetonM1'; time.sleep(1.5)
    t0 = time.time(); j.step('« Arrêter » clicked', F.real_click_chat(text='Arrêter'))
    ended = F.wait_log(r'CHAT turn t\d+ ended', m, 12); secs = round(time.time() - t0, 1)
    j.step('M1 · the turn ends on its own within ~6 s of « Arrêter »', ended and secs <= 7, secs)
    time.sleep(0.8); st = F.chat_state('bubble')
    j.step('M1 · the bubble is usable again (no « Arrêter »)', not st.get('busy'), (st.get('text') or '')[-200:])
    m = F.log_mark(); F.type_and_send('reply jetonM1b', 'bubble'); j.inv.m.conv['a.btn-primary'] = 'jetonM1b'
    j.step('M1 · the next message is answered', F.wait_log(r'CHAT turn t\d+ ended', m, 20)); j.inv.sent('bubble', 'jetonM1b', 'M1 next')
    j.check('M1')


@journey('c3_historique_du_lanceur', 'review C3 (a conversation whose element is gone, opened from the launcher\'s history: it opens and stays) — to enable after H1\'s fix')
def j_c3_history(j):
    j.open_on('p.hero-text', 'text'); j.send('p.hero-text', 'reply', token='jetonC3h')
    # the element leaves the site for good: the agent renames its class in landing.html (the page reloads)
    m = F.log_mark(); F.type_and_send('apply landing.html|<p class="hero-text"|<p class="hero-copy"', 'bubble')
    F.wait_log(r'CHAT permission asked', m, 20); F.answer_permission('Autoriser', 'bubble', m)
    j.step('the element is gone from the page after the edit', F.wait_log(r'CHAT turn t\d+ ended', m, 20) and (time.sleep(2.5) or True)
           and not F.page("!!document.querySelector('p.hero-text')"))
    j.inv.allow_close = True; F.escape_chat('bubble'); time.sleep(0.6); j.inv.m.open = None
    j.inv.m.conv.pop('p.hero-text', None)
    m = F.log_mark(); F.front()
    j.step('the launcher (bottom right) opens the history', C.invoke('AnnotationLauncher')); time.sleep(1.0)
    names = C.uia('-List') or ''
    rows = [l for l in names.splitlines() if 'hero-text' in l or 'jetonC3h' in l or 'Bonjour' in l]
    j.R.data['measures']['historyRows'] = rows[:6]
    name = rows[0].split(' | ')[1] if rows else None
    if not j.step('C3 · the conversation whose element is gone is listed', name, names.splitlines()[-30:]):
        return
    C.uia('-Name', name, '-Invoke'); time.sleep(1.5)
    shown, rb = F.bubble_shown(); st = F.chat_state('bubble')
    j.step('C3 · it opens in the bubble (bottom right) with its conversation', shown and 'jetonC3h' in (st.get('text') or ''), {'region': rb})
    time.sleep(2.0)
    j.step('C3 · and stays open (never closed as « element no longer in the page »)', F.bubble_shown()[0] and 'no longer in the page' not in C.log_text()[m:], [l for l in C.log_text()[m:].splitlines() if 'bubble' in l][-5:])
    j.R.capture('c3-historique')


@journey('c1_prechauffage_retarde', 'review C1 with the warm-up held back (BROWSER_CHAT_TEST_WARM_DELAY_MS, asked to H1): a click before the chat page is ready',
         extra_env={'BROWSER_CHAT_TEST_WARM_DELAY_MS': '4000'})
def j_c1_delay(j):
    j.open_on('a.btn-primary', 'A'); j.send('a.btn-primary', 'reply', token='jetonC1d'); j.close()
    F.release_all(); C.quit_all(); time.sleep(1.0)
    F.launch(fresh=False, extra_env={'BROWSER_CHAT_TEST_WARM_DELAY_MS': '4000'})
    j.inv.m.open = None; j.inv.log_at = F.log_mark(); j.inv.uia_base = None
    F.front(); m = F.log_mark(); C.shortcut('A'); F.wait_log(r'ANNOTATE mode on', m, 10); F.set_layout('comment')
    j.R.data['measures']['warmDelayed'] = bool(F.wait_log(r'warm[^\n]*delay|held back', m, 0.5))
    p = F.element_point('a.btn-primary'); t = time.perf_counter(); F.click_at(*p, settle=0.02)
    j.inv.after_element_click('a.btn-primary', t, 'C1 click before the chat page is ready', mark=m)
    samples = []; t0 = time.time()
    while time.time() - t0 < 5:
        st = F.chat_state('bubble'); samples.append((round(time.time() - t0, 2), st.get('model'), 'jetonC1d' in (st.get('text') or '')))
        time.sleep(0.1)
    bad = [x for x in samples if x[1] is not None and (not x[2] or (x[1] or '').endswith('Agent'))]
    j.step('C1 · as soon as the page shows: the conversation and the model (never « Agent », never an empty thread)', not bad and any(x[2] for x in samples), bad[:5])
    j.check('C1 delayed')

# ---------------------------------------------------------------- runner
def run_one(jn):
    name = jn['name']; print(f'===== {name} ({jn["retours"]})', flush=True)
    j = J(name); before = C.fingerprints(); uia = None
    try:
        F.launch(jn['agent'], extra_env=jn.get('env'))
        F.mode_on('comment')
        uia = F.Uia(); j.uia = uia; j.inv = F.Invariants(j.R, uia)
        F.ERRORS.collect()
        jn['fn'](j)
    except Exception:
        traceback.print_exc()
        j.step('journey without exception', False, traceback.format_exc()[-800:])
    finally:
        if uia:
            uia.close()
        F.finish(j.R, before)
    return {'retours': jn['retours'], 'steps': len(j.R.data['steps']), 'failed': j.R.failed}


def main():
    for s in (sys.stdout, sys.stderr):
        try:
            s.reconfigure(encoding='utf-8')
        except Exception:
            pass
    if '--list' in sys.argv:
        for jn in JOURNEYS:
            print(f"{jn['name']:40} {jn['retours']}")
        print('covered elsewhere:', json.dumps(COVERED_ELSEWHERE, indent=1, ensure_ascii=False))
        return 0
    only = set(sys.argv[sys.argv.index('--only') + 1].split(',')) if '--only' in sys.argv else None
    results = {}
    for jn in JOURNEYS:
        if only and jn['name'] not in only:
            continue
        results[jn['name']] = run_one(jn)
    (F.OUT / 'journeys.json').write_text(json.dumps({'results': results, 'coveredElsewhere': COVERED_ELSEWHERE}, indent=2, ensure_ascii=False), encoding='utf-8')
    bad = {k: v['failed'] for k, v in results.items() if v['failed']}
    print(f'{len(results) - len(bad)} / {len(results)} journeys without failure')
    for k, v in bad.items():
        print('  FAILED', k, v[:6])
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
