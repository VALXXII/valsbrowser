#!/usr/bin/env python3
"""Final QA, phase 3 (b): the « monkey » test — 300+ random actions of a user, REPRODUCIBLE from a seed, in a REAL window
(real mouse and keys through SendInput), with the plan's invariants checked after EVERY action
(design/chat-comment-final-plan.md, « Invariants du test de désordre »; qa_final.Invariants):

  - a click on an element in Comment mode shows a bubble entirely visible, natural height, < 150 ms, the right conversation
  - an open bubble stays glued to its element (±2 px), never cut by its region, never empty, never greyed
  - bubble window visible ⇔ « open » (no ghost bubble, no invisible open state); one bubble window at most
  - the mode, the model and the effort shown change only by an action of the user
  - the conversation mark only after a send; one contour per element
  - no JavaScript error (chat page, page), no exception in the host's log, UI Automation tree complete
  - (review) after each send the user's message is shown in the bubble it was sent from; an open bubble stays open until
    the user closes it (or its element is gone with no turn running); never two « conversation of an element reopened »
    for one click; the mode, model and effort chosen are kept for every new element (coordinator's arbitration)
  - at the end: nothing left running or open

    CHAT_QA_BUILD=chat-preview python tests/chat/final/monkey.py --seed 1 [--actions 320] [--until N] [--dry]
        -> build/chat-qa/final/monkey-<seed>/report.json, plan.json, echec-NN.json + .png (capture, log, seed, step)

--dry prints the plan of a seed (no window). --until N replays the same seed up to action N (a failure's file says which).
The plan is a pure function of the seed: each action carries its own random numbers, its target is resolved on screen
when it runs (« an element that is visible », « the n-th model of the menu »), so a replay does the same thing.
Only when the computer is free (« Libre »): one real-window suite at a time.
"""
from __future__ import annotations
import json
import random
import sys
import time
import traceback

import qa_final as F
from qa_final import C

WEIGHTS = [('click_element', 24), ('click_elsewhere', 4), ('escape', 7), ('wheel', 10), ('scrollbar', 3), ('keyboard_scroll', 3),
           ('toggle_layout', 5), ('other_app', 4), ('paste', 4), ('menu', 6), ('diff', 3), ('ctrl_held', 4), ('send', 9),
           ('permission', 4), ('stop', 2), ('model_change', 2), ('mode_change', 2),
           # the independent review's sequences (design/chat-final-review.md, « Pour Q »)
           ('click_embedded', 4), ('element_change', 2), ('resize', 2), ('switch_then_click', 2), ('keys_after_other_app', 2)]
SCENARIOS = ['reply', 'vrai-markdown', 'vrai-adresse', 'vrai-code', 'vrai-autorisation', 'vrai-lent', 'vrai-erreur-outil', 'vrai-surcharge',
             'annonce', 'vrai-tour', 'apply']
PASTES = ['Change la couleur du bouton en vert', 'Voir http://127.0.0.1:5183/landing.html', 'Texte collé\nsur deux lignes', '```css\n.a{gap:8px}\n```']


def plan(seed, n):
    rng = random.Random(seed)
    kinds = [k for k, _ in WEIGHTS]; weights = [w for _, w in WEIGHTS]
    out = []
    for i in range(n):
        k = rng.choices(kinds, weights)[0]
        out.append({'i': i, 'kind': k, 'r': [rng.random() for _ in range(4)], 'n': rng.randrange(1, 7), 'sign': rng.choice((-1, 1)),
                    'scenario': rng.choice(SCENARIOS), 'paste': rng.choice(PASTES)})
    return out


class Monkey:
    def __init__(self, seed, R, inv, uia):
        self.seed = seed; self.R = R; self.inv = inv; self.uia = uia; self.gap8 = False; self.tok = 0

    # ---- helpers
    def visible_elements(self):
        js = ("(function(){return %s.filter(function(s){var e=document.querySelector(s);if(!e)return false;var r=e.getBoundingClientRect();"
              "return r.bottom>40&&r.top<innerHeight-40&&r.right>20&&r.left<innerWidth-20})})()") % json.dumps(F.ELEMENTS)
        return F.page(js) or []

    def bubble_box(self):
        shown, rb = F.bubble_shown()
        return rb if shown and isinstance(rb, tuple) else None

    def turn_wait(self, m, timeout=0.0):
        return F.wait_log(r'CHAT turn t\d+ ended', m, timeout) if timeout else None

    # ---- the actions (each returns a short description; `settle` seconds to wait before the invariants)
    def click_element(self, a):
        vis = self.visible_elements()
        if not vis:
            F.front(); F.wheel(-3); return 'no element visible: scrolled up', 0.8
        sel = vis[int(a['r'][0] * len(vis))]
        F.front()
        p = F.element_point(sel, avoid=self.bubble_box())
        if self.inv.m.layout == 'panel':
            F.click_at(*p); return f'panel: click on {sel} (a chip)', 0.6
        m = F.log_mark(); t = F.click_at(*p, settle=0.04)
        self.inv.after_element_click(sel, t, f'#{a["i"]} click {sel}', mark=m)
        return f'click on {sel}', 0.2

    def click_elsewhere(self, a):
        F.front(); pg = F.page_area()
        js = ("(function(){var o=[];for(var j=1;j<10;j++)for(var i=1;i<14;i++){var x=innerWidth*i/14,y=innerHeight*j/10,e=document.elementFromPoint(x,y);"
              "if(e&&(e===document.body||e===document.documentElement||e.tagName==='MAIN'))o.push([x,y])}return o})()")
        pts = F.page(js) or []
        m = F.log_mark()
        if not pts:
            return 'no empty point', 0.1
        k = F.dpr(); x, y = pts[int(a['r'][1] * len(pts))]; self.inv.allow_close = True
        F.click_at(pg[0] + x * k, pg[1] + y * k); time.sleep(0.6)
        lines = C.log_text()[m:]
        if self.inv.m.layout == 'comment':
            if 'click elsewhere closed' in lines or 'bubble closed' in lines:
                self.inv.m.open = None
            elif 'bubble opened on' in lines:
                self.inv.m.open = None; self.inv.m.anchor = None   # a container got selected: its own bubble (not one of ours)
                return 'click elsewhere: a container selected (its bubble)', 0.3
        return 'click elsewhere', 0.3

    def escape(self, a):
        if self.inv.m.layout == 'comment' and self.inv.m.open:
            st = F.chat_state('bubble')
            F.escape_chat('bubble')
            if st.get('menus') or st.get('diff'):
                return 'Échap (a menu / the diff)', 0.5
            self.inv.m.open = None; self.inv.allow_close = True
            return 'Échap: the bubble closes', 0.6
        return 'Échap with nothing open: nothing', 0.1

    def wheel(self, a):
        F.front(); pg = F.page_area(); bb = self.bubble_box()
        x = pg[0] + 30 if not bb or bb[0] > pg[0] + 80 else pg[2] - 60
        F.u32.SetCursorPos(x, (pg[1] + pg[3]) // 2); time.sleep(0.05)
        F.wheel(a['n'] * a['sign'], 0.012 if a['r'][2] < 0.4 else 0.08)
        return f'wheel {a["n"] * a["sign"]}', 0.9

    def scrollbar(self, a):
        F.front(); pg = F.page_area()
        y = F.page('[scrollY,document.scrollingElement.scrollHeight,innerHeight]'); k = F.dpr()
        th = pg[1] + int((y[0] / max(1, y[1])) * (pg[3] - pg[1])) + int(20 * k)
        x = pg[2] - int(6 * k); dy = int(a['sign'] * a['n'] * 25 * k)
        F.drag(x, th, x, max(pg[1] + 5, min(pg[3] - 5, th + dy)), steps=12)
        return f'scrollbar drag {dy} px', 0.9

    def keyboard_scroll(self, a):
        F.front(); pg = F.page_area(); k = F.dpr()
        F.click_at(pg[2] - int(6 * k), pg[1] + int(30 * k)); time.sleep(0.15)   # the page gets the keyboard (scrollbar: no selection)
        vk = [0x28, 0x26, 0x22, 0x21][int(a['r'][3] * 4)]
        for _ in range(min(3, a['n'])):
            F.tap(vk); time.sleep(0.12)
        return f'keyboard scroll {hex(vk)}', 0.9

    def toggle_layout(self, a):
        to = 'panel' if self.inv.m.layout == 'comment' else 'comment'
        self.inv.allow_close = True; F.set_layout(to); self.inv.m.layout = to; self.inv.m.open = None
        return f'layout -> {to}', 0.4

    def other_app(self, a):
        with F.OtherApp() as o:
            time.sleep(0.5)
            self.inv.m.hidden_by_other = bool(self.inv.m.open)
            shown, _ = F.bubble_shown()
            if shown and o.in_front():
                self.R.step(f'#{a["i"]} invariant: another application in front hides the bubble', False, {'shown': shown})
            if a['r'][0] < 0.4 and self.inv.m.layout == 'comment':
                vis = self.visible_elements()
                if vis:
                    sel = vis[int(a['r'][1] * len(vis))]; p = F.element_point(sel)
                    m = F.log_mark(); t = F.click_at(*p, settle=0.04)
                    self.inv.after_element_click(sel, t, f'#{a["i"]} other application in front, click {sel}', mark=m)
                    return f'other application, then a click on {sel}', 0.3
        F.front(); time.sleep(0.5); self.inv.m.hidden_by_other = False
        return 'other application, then back', 0.4

    def paste(self, a):
        kind = 'bubble' if self.inv.m.layout == 'comment' else 'panel'
        if kind == 'bubble' and not self.inv.m.open:
            return 'paste: no bubble open', 0.1
        F.set_clipboard(a['paste'])
        p = F.chat_point('[contenteditable=true]', kind=kind)
        if not p:
            return 'paste: no composer', 0.1
        F.click_at(*p); time.sleep(0.1)
        before = F.chat_state(kind).get('mode'); seen = set()
        F.tap(0x56, ctrl=True)
        t0 = time.time()
        while time.time() - t0 < 1.2:
            seen.add(F.chat_state(kind).get('mode')); time.sleep(0.02)
        seen.discard(None)
        if seen and seen != {before}:
            self.R.step(f'#{a["i"]} invariant: pasting never changes the mode shown (R46)', False, {'before': before, 'seen': sorted(seen)})
        return 'paste', 0.2

    def menu(self, a):
        kind = 'bubble' if self.inv.m.layout == 'comment' else 'panel'
        if kind == 'bubble' and not self.inv.m.open:
            return 'menu: no bubble', 0.1
        sel = 'button.m' if a['r'][0] < 0.5 else 'button.tb.mode'
        F.real_click_chat(sel, kind=kind, tries=2); time.sleep(0.5)
        if a['r'][1] < 0.3:   # menu -> another menu (R41)
            F.real_click_chat('button.tb.mode' if sel == 'button.m' else 'button.m', kind=kind, tries=2); time.sleep(0.5)
        F.close_menus(kind)
        return f'menu {sel} open / close', 0.3

    def diff(self, a):
        if self.inv.m.layout != 'comment' or not self.inv.m.open or not F.chat_point('.bt-bar-main'):
            return 'diff: no changes bar', 0.1
        F.real_click_chat('.bt-bar-main', tries=2); time.sleep(0.7)
        g = F.chat_point('.b-grip')
        if g and a['r'][2] < 0.6:
            F.drag(g[0], g[1], g[0] + int(a['r'][0] * 200), g[1] + int(a['r'][1] * 150), steps=14)
        self.inv.check(f'#{a["i"]} diff open')
        F.escape_chat('bubble'); time.sleep(0.6)
        return 'diff open, handle, Échap', 0.3

    def ctrl_held(self, a):
        F.front(); vis = [s for s in self.visible_elements() if s in ('h1.hero-title', 'p.hero-text', 'h2.section-title', 'div.hero-visual', 'div.feature')]
        if not vis:
            return 'Ctrl: no plain element visible', 0.1
        sel = vis[int(a['r'][0] * len(vis))]; p = F.element_point(sel, avoid=self.bubble_box())
        m = F.log_mark(); F.key(0x11)
        try:
            time.sleep(0.2); F.u32.SetCursorPos(*p); time.sleep(0.2)
            F.mouse(0x0002); time.sleep(0.03); F.mouse(0x0004); time.sleep(0.4)
        finally:
            F.key(0x11, True)
        lines = C.log_text()[m:]
        if 'element aimed' in lines or 'bubble opened on' in lines:
            self.R.step(f'#{a["i"]} invariant: Ctrl held, a click goes to the site (R31)', False, lines[-500:])
        return f'Ctrl held, click on {sel}', 0.4

    def send(self, a):
        kind = 'bubble' if self.inv.m.layout == 'comment' else 'panel'
        if kind == 'bubble' and not self.inv.m.open:
            return 'send: no bubble', 0.1
        self.tok += 1; token = f'jt{self.seed}x{self.tok}'
        sc = a['scenario']
        if sc == 'apply':
            sc = 'apply ' + (F.GAP_TO_4 if self.gap8 else F.GAP_TO_8); self.gap8 = not self.gap8
        F.type_and_send(f'{sc} {token}', kind)
        if kind == 'bubble':
            self.inv.m.conv[self.inv.m.open] = token
        self.inv.sent(kind, token, f'#{a["i"]} send')
        return f'send « {sc} »', 1.0

    def permission(self, a):
        kind = 'bubble' if self.inv.m.layout == 'comment' else 'panel'
        st = F.chat_state(kind)
        if not st.get('perm'):
            return 'permission: none asked', 0.1
        label = 'Autoriser' if a['r'][0] < 0.7 else 'Refuser'
        F.real_click_chat(text=label, kind=kind, tries=3)
        return f'permission: {label}', 1.0

    def stop(self, a):
        kind = 'bubble' if self.inv.m.layout == 'comment' else 'panel'
        if not F.chat_state(kind).get('busy'):
            return 'stop: no turn running', 0.1
        F.real_click_chat(text='Arrêter', kind=kind, tries=3)
        return 'Arrêter', 0.8

    def model_change(self, a):
        kind = 'bubble' if self.inv.m.layout == 'comment' else 'panel'
        if kind == 'bubble' and not self.inv.m.open:
            return 'model: no bubble', 0.1
        if not F.real_click_chat('button.m', kind=kind, tries=2):
            return 'model: no button', 0.1
        time.sleep(0.5)
        names = F.chat("Array.from(document.querySelectorAll('.menu .mi')).map(function(e){return e.innerText.split('\\n')[0].trim()})"
                       ".filter(function(t){return /Opus|Sonnet|Haiku/.test(t)})", kind) or []
        if not names:
            F.close_menus(kind); return 'model: no model listed', 0.2
        name = names[int(a['r'][1] * len(names))]
        F.real_click_chat(text=name, kind=kind, tries=2); time.sleep(0.8); F.close_menus(kind)
        self.inv.m.model = F.chat_state(kind).get('model') or self.inv.m.model
        return f'model -> {name}', 0.3

    def mode_change(self, a):
        kind = 'bubble' if self.inv.m.layout == 'comment' else 'panel'
        if kind == 'bubble' and not self.inv.m.open:
            return 'mode: no bubble', 0.1
        if not F.real_click_chat('button.tb.mode', kind=kind, tries=2):
            return 'mode: no button', 0.1
        time.sleep(0.5)
        rows = F.chat("Array.from(document.querySelectorAll('.menu .mi')).map(function(e){return e.innerText.split('\\n')[0].trim()})", kind) or []
        if not rows:
            return 'mode: no menu', 0.2
        name = rows[int(a['r'][2] * len(rows))]
        F.real_click_chat(text=name, kind=kind, tries=2); time.sleep(0.6); F.close_menus(kind)
        got = F.chat_state(kind).get('mode')
        if got and name not in got:
            self.R.step(f'#{a["i"]} the mode chosen is the mode shown', False, {'chosen': name, 'shown': got})
        self.inv.m.mode = got or self.inv.m.mode
        return f'mode -> {name}', 0.3

    # ---- the review's sequences
    def click_embedded(self, a):
        if self.inv.m.layout != 'comment':
            return 'embedded: panel layout', 0.1
        host = '#qa-frame' if a['r'][0] < 0.5 else '#qa-card'
        F.inject_embedded(); F.scroll_into_view(host, 'center'); F.front()
        p = F.embedded_point(host)
        if not p:
            return 'embedded: not found', 0.1
        m = F.log_mark(); t = F.click_at(*p, settle=0.04)
        self.inv.after_element_click(host, t, f'#{a["i"]} click inside {host}', mark=m)
        return f'click inside {host}', 0.8

    def element_change(self, a):
        """The open bubble's element renamed (as an agent's edit would) — kept open during a turn — then given back."""
        sel = self.inv.m.open
        if self.inv.m.layout != 'comment' or not sel or sel.startswith('#qa-'):
            return 'element change: no bubble', 0.1
        busy = F.chat_state('bubble').get('busy')
        F.page("(function(){var e=document.querySelector(%s);if(!e)return 0;e.dataset.qaClass=e.className;e.className='qa-renamed';return 1})()" % json.dumps(sel))
        time.sleep(1.0)
        shown, _ = F.bubble_shown()
        if busy and not shown and F.foreground_is_ours():
            self.R.step(f'#{a["i"]} invariant: the element renamed during a turn keeps its bubble open (C3)', False, sel)
        F.page("(function(){var e=document.querySelector('.qa-renamed');if(e)e.className=e.dataset.qaClass;return 1})()")
        if not busy:
            self.inv.allow_close = True
        time.sleep(0.4)
        return f'element {sel} renamed then given back (turn running: {busy})', 0.4

    def resize(self, a):
        w = 1100 + int(a['r'][0] * 370); h = 420 + int(a['r'][1] * 245)
        if a['r'][2] < 0.4:
            w, h = F.USER_PAGE
        F.front(); pg = F.page_area(); F.u32.SetCursorPos(pg[0] + 20, (pg[1] + pg[3]) // 2); F.wheel(a['sign'])
        time.sleep(0.6); got = F.set_page_size(w, h); time.sleep(0.8); self.inv.rebase()
        if self.inv.m.open and self.inv.m.layout == 'comment' and F.foreground_is_ours():
            sf = F.bubble_surface(); pg = F.page_area()
            comp = F.chat("(function(){var c=document.querySelector('[contenteditable=true]');if(!c)return null;var r=c.getBoundingClientRect();return r.bottom})()")
            st = F.chat_state('bubble')
            if sf and comp and st.get('app') and comp > st['app'][3] + 1:
                self.R.step(f'#{a["i"]} invariant: after a resize the composer stays inside the bubble (M15)', False, {'composer': comp, 'app': st['app']})
        return f'scroll then page area {got}', 0.3

    def switch_then_click(self, a):
        """Panel -> Commentaire by the band's button, then a click on an element 50 ms later (M5)."""
        if self.inv.m.layout != 'panel':
            self.inv.allow_close = True; F.set_layout('panel'); self.inv.m.layout = 'panel'; self.inv.m.open = None; time.sleep(0.6)
        b = self.uia.bounds(['AnnotationLayoutComment']).get('AnnotationLayoutComment') if self.uia else None
        vis = [s for s in self.visible_elements() if s not in F.BIG] or self.visible_elements()
        if not b or not vis:
            return 'switch then click: no button or element', 0.1
        sel = vis[int(a['r'][0] * len(vis))]; p = F.element_point(sel)
        F.front(); F.click_at((b[0] + b[2]) // 2, (b[1] + b[3]) // 2, settle=0.03); time.sleep(0.05)
        m = F.log_mark(); t = F.click_at(*p, settle=0.0)
        self.inv.m.layout = 'comment'
        self.inv.after_element_click(sel, t, f'#{a["i"]} switch then click {sel}', mark=m)
        st = F.chat_state('bubble'); aw = (st['app'][2] - st['app'][0]) if st.get('app') else 0
        if self.inv.m.open and abs(aw - 320) > 2:
            self.R.step(f'#{a["i"]} invariant: the bubble has the bubble layout (M5: a 320 px surface)', False, {'surface width': aw, 'window': st.get('w')})
        return f'Commentaire then click {sel} 50 ms later', 0.3

    def keys_after_other_app(self, a):
        if self.inv.m.layout != 'comment' or not self.inv.m.open:
            return 'keys: no bubble', 0.1
        p = F.chat_point('[contenteditable=true]')
        if not p:
            return 'keys: no composer', 0.1
        F.click_at(*p); time.sleep(0.2)
        with F.OtherApp():
            time.sleep(0.5); self.inv.m.hidden_by_other = True
        F.front(); time.sleep(0.7); self.inv.m.hidden_by_other = False
        word = f'k{a["i"]}'; F.type_keys(word); time.sleep(0.4)
        comp = F.chat("(document.querySelector('[contenteditable=true]')||{}).innerText||''") or ''
        if word not in comp:
            self.R.step(f'#{a["i"]} invariant: back from another application, the keyboard writes in the bubble (M4)', False, comp[-80:])
        F.chat("(function(){var t=document.querySelector('[contenteditable=true]');if(t){t.textContent='';t.dispatchEvent(new Event('input',{bubbles:true}))}return 1})()")
        return 'other application, back, typing', 0.2

    def run(self, actions):
        for a in actions:
            self.R.context = {'seed': self.seed, 'index': a['i'], 'action': a,
                              'replay': f'CHAT_QA_BUILD=<build> python tests/chat/final/monkey.py --seed {self.seed} --until {a["i"]}'}
            try:
                desc, settle = getattr(self, a['kind'])(a)
            except Exception:
                F.release_all()
                desc, settle = f'{a["kind"]}: exception', 0.3
                self.R.step(f'#{a["i"]} {a["kind"]} without exception', False, traceback.format_exc()[-800:])
            print(f'[{a["i"]:3}] {desc}', flush=True)
            self.R.data.setdefault('done', []).append(f'{a["i"]} {desc}')
            time.sleep(settle)
            self.inv.check(f'#{a["i"]} {desc}')
            if len(self.R.data['failures']) >= 30:
                self.R.step('stopped after 30 failures (the first ones say enough)', False)
                break


def main():
    for s in (sys.stdout, sys.stderr):
        try:
            s.reconfigure(encoding='utf-8')
        except Exception:
            pass
    seed = int(sys.argv[sys.argv.index('--seed') + 1]) if '--seed' in sys.argv else 1
    n = int(sys.argv[sys.argv.index('--actions') + 1]) if '--actions' in sys.argv else 320
    actions = plan(seed, n)
    if '--until' in sys.argv:
        actions = actions[:int(sys.argv[sys.argv.index('--until') + 1]) + 1]
    if '--dry' in sys.argv:
        for a in actions:
            print(a['i'], a['kind'], a['scenario'] if a['kind'] == 'send' else '', [round(x, 3) for x in a['r']])
        print('kinds:', {k: sum(1 for a in actions if a['kind'] == k) for k, _ in WEIGHTS})
        return 0
    R = F.Report(f'monkey-{seed}'); before = C.fingerprints(); uia = None
    (R.dir / 'plan.json').write_text(json.dumps(actions, indent=1), encoding='utf-8')
    try:
        F.launch()
        F.mode_on('comment')
        F.inject_embedded()
        uia = F.Uia(); inv = F.Invariants(R, uia); F.ERRORS.collect()
        Monkey(seed, R, inv, uia).run(actions)
    except Exception:
        traceback.print_exc(); R.step('monkey without exception', False, traceback.format_exc()[-800:])
    finally:
        if uia:
            uia.close()
        F.finish(R, before)
    failed = R.failed
    print(f'seed {seed}: {len(actions)} actions, {len(R.data["steps"]) - len(failed)} / {len(R.data["steps"])} checks passed, {len(R.data["failures"])} failure files')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
