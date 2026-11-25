#!/usr/bin/env python3
"""Lot R (2026-11-24) — real-window pass, fake agent, about 6 minutes:

    python tests/chat/chat_qa_lot_r.py   # -> build/chat-qa/lot-r/report.json + captures

  1  « Panneau »: the chat opens with the mode, takes no keyboard focus, and the page does not move between the mode's
     start and the first element picked (its box and its scroll position are the same). Ctrl+Maj+D gives it the caret,
     the next one closes it.
  2  An element beyond the composer's 10: nothing added, no capture, the strip « 10 éléments au maximum… » for 4 s,
     again at each extra click.
  3  local_en_ligne (#125 + decision A): where « Annoter » starts — 127.0.0.1, localhost, *.localhost, [::1] = on with
     the chat; a local file = on with the chat (its folder is its site); an online site, a fake localhost
     (localhost.example.com) and a LAN address (192.168.77.77) = off (the card says why).
  4  A private window: the chat works there; what it keeps (conversation, element sent) lives beside the private
     ledger and is erased when the window closes, and at the next start after the window was killed.
  5  Nothing of the old annotation system is left on screen: no note window, no launcher, Ctrl+Maj+H does nothing.
  6  Commentaire, 4 messages waiting in a bubble (O5): 2 shown, « +2 en attente » opens the list, the input stays whole.

Same guards as tests/chat/chat_qa.py (own copy of the build, own profile, BROWSER_ANNOTATIONS_TEST=1, agent
configurations redirected, real configurations fingerprinted). The online site, the fake localhost and the LAN
address are mapped to 127.0.0.1 by the engine's own resolver rule, and every test server listens on a loopback
address only: nothing leaves the machine, no firewall question. Nothing left running.
"""
from __future__ import annotations
import json
import os
import pathlib
import shutil
import subprocess
import sys
import time
import traceback

import chat_qa as C
from chat_qa import Q

OUT = C.QA / 'lot-r'
FILES = C.WORK / 'lot-r-fichiers' / 'Mon site'          # a local « project » opened as files (a space in its path)
PORT_ALL = 5187                                           # 127.0.0.1: 127.x, localhost, *.localhost and the names / address mapped to it
LAN = '192.168.77.77'                                     # a LAN address, mapped to 127.0.0.1 by the resolver rule
PORT_V6 = 5186                                            # ::1
PORT_PRIVATE = 5188                                       # the private window's site, served as the sandbox's own (its pages name their files)
PRIVATE = C.APP / 'annotations-private'
report = {'steps': [], 'measures': {}, 'captures': []}
servers = []
pids = []


def step(name, ok, detail=''):
    report['steps'].append({'step': name, 'ok': bool(ok), 'detail': str(detail)[:600]})
    print(('PASS ' if ok else 'FAIL ') + name + (' — ' + str(detail)[:300] if detail else ''), flush=True)
    return bool(ok)


def mark():
    return len(C.log_text())


def logged(pattern, after=0, timeout=15.0):
    return C.wait_log(pattern, timeout, after)


def capture(name):
    try:
        report['captures'].append(C.grab(f'lot-r/{name}').name)
    except Exception as e:
        print('capture failed', name, e)


def at(prefix, js):
    return C.evaluate(prefix, js)


def navigate(url, from_prefix=None):
    """The tab goes to `url` (the engine's own navigation); True once a page of that address answers."""
    ws = None
    for t in C.targets():
        if t.get('type') == 'page' and not t.get('url', '').startswith(C.PANEL) and (from_prefix is None or t.get('url', '').startswith(from_prefix)):
            ws = Q.WS(t['webSocketDebuggerUrl']); break
    if ws is None:
        return False
    ws.call('Page.navigate', {'url': url})
    ok = C.wait_for(lambda: C.ws_for(url.split('#')[0].split('?')[0][:40]) is not None, 15, 0.3)
    time.sleep(1.2)
    return bool(ok)


def box(prefix):
    """Where the page is: its size, its scroll position, where its pay button sits in it."""
    return at(prefix, "(function(){var r=document.querySelector('.btn-primary').getBoundingClientRect();"
                      "return [innerWidth,innerHeight,Math.round(scrollX),Math.round(scrollY),Math.round(r.left),Math.round(r.top)]})()")


def chips():
    return C.panel("document.querySelectorAll('.cin .chip').length") or 0


def old_windows():
    """Top-level windows of the old system (note panel) — none may exist."""
    return [w[1:3] for w in Q.windows() if w[1] == 'BrowserAnnotationNote']


def dismiss_connect():
    if C.invoke('AnnotationConnectLater') or C.invoke('AnnotationConnectClose'):
        time.sleep(0.6)


def serve(bind, port, root):
    p = subprocess.Popen([sys.executable, '-m', 'http.server', str(port), '--bind', bind], cwd=str(root), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    servers.append(p)
    return p


def serve_site(port):
    """The sandbox site as tests/chat/chat_qa.py serves it (sandbox/chat/serve.py, from the site's own folder): its pages name
    their files, so the project folder is found « without asking » — as for a real dev server."""
    p = subprocess.Popen([sys.executable, str(C.ROOT / 'sandbox' / 'chat' / 'serve.py'), '--root', str(C.SITE), '--port', str(port)], cwd=str(C.SITE),
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    servers.append(p)
    time.sleep(1.0)


def env_for(extra):
    """The same guarded environment as C.launch (for the launches this script makes itself: a private window)."""
    env = dict(os.environ)
    env['WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS'] = ARGS
    env['BROWSER_ANNOTATIONS_CONNECT_ROOT'] = str(C.AGENTS); env['BROWSER_ANNOTATIONS_TEST'] = '1'
    env.pop('BROWSER_ANNOTATIONS_CONNECT_PATH', None)
    fake = C.TOOLS / 'fake-acp-agent.exe'
    env['BROWSER_CHAT_FAKE_AGENT'] = f'"{fake}" --resume --log "{C.WORK / "fake-agent.log"}" --edit "{C.FAKE_EDIT}"'
    env.update(extra or {})
    assert pathlib.Path(env['BROWSER_ANNOTATIONS_CONNECT_ROOT']).resolve().is_relative_to(C.ROOT / 'build'), 'unsafe environment'
    return env


ARGS = (f'--remote-debugging-port={Q.PORT} --remote-allow-origins=http://127.0.0.1:{Q.PORT} '
        f'--host-resolver-rules="MAP exemple.test 127.0.0.1, MAP localhost.example.com 127.0.0.1, MAP {LAN} 127.0.0.1"')


def launch_private(url):
    p = subprocess.Popen([str(C.APP / 'browser.exe'), '--private', url], cwd=str(C.APP), env=env_for(None))
    (C.APP / 'qa.pid').write_text(str(p.pid)); pids.append(p.pid)
    C.wait_for(lambda: C.ws_for(url[:30]) is not None, 60, 0.3)
    time.sleep(1.5)
    return p


def close_window(p, timeout=25):
    h = Q.main_window()
    if h:
        C.user32.PostMessageW(h, 0x10, 0, 0)
    end = time.time() + timeout
    while time.time() < end and p.poll() is None:
        time.sleep(0.3)
    return p.poll() is not None


def private_files():
    if not PRIVATE.exists():
        return [], []
    return sorted(f.name for f in PRIVATE.glob('*.ledger')), sorted(f.name for f in PRIVATE.glob('*.data'))


def store_entries():
    root = C.APP / 'annotations'
    return sorted(str(f.parent.relative_to(root)) for f in root.rglob('annotation.json')) if root.exists() else []


def normal_conversations():
    d = C.APP / 'annotations-chat' / 'conversations'
    return sorted(f.name for f in d.glob('*.json')) if d.exists() else []


def mode_starts(prefix, label, expect_on, chat=True):
    """Ctrl+Maj+A on the page shown: does « Annoter » start there? Leaves the mode off."""
    m = mark(); C.shortcut('A'); time.sleep(1.6)
    seg = C.log_text()[m:]
    on = 'ANNOTATE mode on' in seg
    card = 'ANNOTATE live-site card shown' in seg
    if expect_on:
        ok = on and not card and (not chat or logged(r'CHAT panel opened', m, 8) is not None) and not old_windows()
        step(f'local_en_ligne · {label}: « Annoter » starts, with the chat', ok, seg[-260:] if not ok else '')
        dismiss_connect()
        m2 = mark(); C.shortcut('A'); logged(r'ANNOTATE mode off', m2, 6); time.sleep(0.6)
    else:
        ok = not on and card and 'CHAT panel opened' not in seg and not old_windows()
        step(f'local_en_ligne · {label}: « Annoter » stays off (the card says why), no chat, no note', ok, seg[-260:] if not ok else '')
        C.shortcut('A'); time.sleep(0.6)   # a second press puts the card away
    return ok


def part_panel_at_start():
    time.sleep(1.0)
    C.page("window.scrollTo(0, 60)"); time.sleep(0.4)
    m0 = mark(); C.shortcut('A')
    step('mode on (Ctrl+Maj+A)', logged(r'ANNOTATE mode on', m0, 10))
    step('1 · « Panneau »: the chat opens with the mode, before any element is picked', logged(r'CHAT panel opened with the mode', m0, 10) is not None)
    logged(r'CHAT panel in place', m0, 8); time.sleep(1.4)
    dismiss_connect(); C.user32.SetForegroundWindow(Q.main_window()); time.sleep(0.5)
    C.wait_for(C.panel_exists, 10)
    step('1 · …it took no keyboard focus (opened by the mode, not asked for)', not C.panel('document.hasFocus()'), C.panel('[document.hasFocus(), (document.activeElement||{}).id]'))
    step('1 · …its empty view is there (« Que voulez-vous changer ? »)', 'Que voulez-vous changer' in C.panel_text(), C.panel_text()[:160])
    before = box(C.URL)
    capture('1-panneau-au-lancement')
    m1 = mark(); C.click_page('.title')
    picked = C.wait_for(lambda: chips() >= 1, 10)
    time.sleep(0.8)
    after = box(C.URL)
    report['measures']['pageBox'] = {'afterModeStart': before, 'afterFirstPick': after}
    step('1 · no page displacement between the mode\'s start and the first element picked (size, scroll, content)', bool(picked) and before == after, f'{before} -> {after}')
    step('1 · …and that click opened no panel (it was there)', 'CHAT panel opened' not in C.log_text()[m1:] and 'a click on an element opens the chat' not in C.log_text()[m1:])
    capture('1-premier-element')
    # Ctrl+Maj+D: the caret first, the close next
    C.page("document.body.focus()"); time.sleep(0.2)
    m2 = mark(); C.shortcut('D')
    asked = logged(r'CHAT Ctrl\+Maj\+D on the panel opened with the mode: the caret goes to the composer', m2, 5)
    time.sleep(0.5)
    step('1 · Ctrl+Maj+D on the panel the mode opened: the caret goes to its composer (it does not close)', asked is not None and 'CHAT panel closed' not in C.log_text()[m2:] and C.panel_exists())
    m3 = mark(); C.shortcut('D')
    step('1 · the next Ctrl+Maj+D closes it', logged(r'CHAT panel closed', m3, 5) is not None)
    time.sleep(0.8)
    m4 = mark(); C.shortcut('D')
    step('1 · and the next opens it again', logged(r'CHAT panel opened', m4, 8) is not None)
    time.sleep(1.2)
    # nothing of the old system
    m5 = mark(); Q.post_key(ord('H'), ctrl=True, shift=True); time.sleep(1.0)
    step('5 · Ctrl+Maj+H does nothing (no annotation history)', 'history' not in C.log_text()[m5:].lower() and not old_windows(), C.log_text()[m5:][-200:])
    ids = C.uia('-List') or ''
    step('5 · no launcher, no note, no history among the windows', 'AnnotationLauncher' not in ids and 'AnnotationNote' not in ids and 'AnnotationHistory' not in ids and not old_windows())


def part_ten_elements():
    selectors = ['.field-label', '.btn-primary', '.hint', '#react-button', '.brand', '#cart', '.summary', '.lines', '.site-header', '.field', '.form', '.title', '.checkout']
    for sel in selectors * 2:
        if chips() >= 10:
            break
        try:
            C.click_page(sel)
        except RuntimeError:
            continue
        time.sleep(0.9)
    n = chips()
    if not step('2 · ten elements in the composer', n == 10, f'{n} chip(s)'):
        return
    for turn in (1, 2):
        m = mark(); C.click_page('.hint'); time.sleep(0.9)
        seg = lambda: C.log_text()[m:]
        said = logged(r'CHAT an element beyond the 10 of the composer was clicked', m, 5) is not None
        strip = C.wait_for(lambda: '10 éléments au maximum. Envoyez la demande ou retirez-en un.' in C.panel_text(), 3)
        step(f'2 · click {10 + turn}: nothing added, the strip « 10 éléments au maximum… » (time {turn})', said and bool(strip) and chips() == 10, f'{chips()} chips')
        step(f'2 · …no frame nor capture started for it (time {turn})', 'ANNOTATE capture' not in seg() and 'ANNOTATE inspector at the selection' not in seg() and 'CHAT element aimed' not in seg(), seg()[-200:])
        if turn == 1:
            capture('2-dix-elements')
        gone = C.wait_for(lambda: '10 éléments au maximum' not in C.panel_text(), 6, 0.2)
        report['measures'].setdefault('stripGone', []).append(bool(gone))
        step(f'2 · …the strip leaves by itself after about 4 s (time {turn})', bool(gone))
    # the composer is emptied as the user would (nothing sent)
    C.panel("(function(){var c=document.getElementById('composer-input');c.innerHTML='';c.dispatchEvent(new Event('input',{bubbles:true}));return 1})()"); time.sleep(0.5)
    m = mark(); C.shortcut('A'); logged(r'ANNOTATE mode off', m, 6); time.sleep(0.8)


def part_local_en_ligne():
    FILES.mkdir(parents=True, exist_ok=True)
    (FILES / 'index.html').write_text('<!doctype html><meta charset="utf-8"><title>Fichier local</title><h1 class="title">Un fichier local</h1>'
                                       '<p class="hint">Ouvert par file://, sans serveur.</p><button class="btn-primary">Payer</button>', encoding='utf-8')
    serve('127.0.0.1', PORT_ALL, C.SITE); serve('::1', PORT_V6, C.SITE)
    time.sleep(1.5)
    cases = [('127.0.0.1', f'http://127.0.0.1:{PORT_ALL}/', True), ('localhost', f'http://localhost:{PORT_ALL}/', True),
             ('*.localhost', f'http://app.localhost:{PORT_ALL}/', True), ('[::1]', f'http://[::1]:{PORT_V6}/', True),
             ('an online site (exemple.test)', f'http://exemple.test:{PORT_ALL}/', False),
             ('a fake localhost (localhost.example.com)', f'http://localhost.example.com:{PORT_ALL}/', False),
             (f'a LAN address ({LAN})', f'http://{LAN}:{PORT_ALL}/', False)]
    here = C.URL
    for label, url, on in cases:
        if not step(f'local_en_ligne · {label}: the page loads', navigate(url, here) and at(url[:24], "!!document.querySelector('.btn-primary')"), url):
            here = None
            continue
        here = url[:24]
        mode_starts(here, label, on)
    # a local file: the mode AND the chat (decision A); its folder is its site and its project folder
    furl = FILES.joinpath('index.html').resolve().as_uri()
    if step('local_en_ligne · a local file: the page loads', navigate(furl, here), furl):
        m = mark(); C.shortcut('A')
        on = logged(r'ANNOTATE mode on', m, 8) is not None
        opened = logged(r'CHAT panel opened on file:///', m, 10)
        folder = logged(r'CHAT local file: its folder (.+)', m, 10)
        used = logged(r'CHAT folder used without asking \(sure\): (.+)', m, 10)
        dismiss_connect()
        step('local_en_ligne · a local file: « Annoter » starts, with the chat (decision A)', on and opened is not None and not old_windows(), C.log_text()[m:][-260:])
        step('…its project folder is the folder that holds the file, never one above', folder is not None and used is not None and pathlib.Path(used.group(1).strip()).resolve() == FILES.resolve(), used.group(0)[5:] if used else '')
        m2 = mark()
        ws = C.ws_for('file:///')
        r = at('file:///', "(function(){var r=document.querySelector('.title').getBoundingClientRect();return [r.left+r.width/2,r.top+r.height/2]})()")
        for kind in ('mouseMoved', 'mousePressed', 'mouseReleased'):
            ws.call('Input.dispatchMouseEvent', {'type': kind, 'x': r[0], 'y': r[1], 'button': 'none' if kind == 'mouseMoved' else 'left', 'clickCount': 0 if kind == 'mouseMoved' else 1})
        step('…an element of the file becomes a chip of the chat', bool(C.wait_for(lambda: chips() >= 1, 10)))
        C.panel("document.getElementById('composer-input').focus()"); C.type_panel(' Mets ce titre en gras'); time.sleep(0.2); C.enter_panel()
        step('…and a request starts a turn in that folder', logged(r'CHAT turn t\d+ started', m2, 25) is not None)
        C.wait_for(lambda: 'Refuser' in C.panel_text() or logged(r'CHAT turn t\d+ ended', m2, 0.2) is not None, 15)
        if 'Refuser' in C.panel_text():
            C.click_panel(text='Refuser')
        logged(r'CHAT turn t\d+ ended', m2, 30)
        capture('3-fichier-local')
        m3 = mark(); C.shortcut('A'); logged(r'ANNOTATE mode off', m3, 6); time.sleep(0.6)


def part_queue():
    """4 messages written in a bubble while another element's turn runs: they wait there (O5: they pushed the input out)."""
    if not step('6 · back on the sandbox site', navigate(C.URL, None) and C.page("!!document.querySelector('.btn-primary')")):
        return
    m = mark(); C.shortcut('A'); logged(r'ANNOTATE mode on', m, 8); time.sleep(1.5); dismiss_connect()
    m1 = mark(); C.invoke('AnnotationLayoutComment')
    if not step('6 · « Commentaire » chosen', logged(r'CHAT layout: Commentaire', m1, 6) is not None):
        return
    time.sleep(1.2)
    m2 = mark(); C.click_page('.btn-primary'); logged(r'CHAT bubble opened on', m2, 10); time.sleep(1.2)
    C.panel("document.getElementById('composer-input').focus()"); C.type_panel('vrai-lent jetonLotR'); time.sleep(0.2); C.enter_panel()
    step('6 · a slow turn runs on a first element', logged(r'CHAT turn t\d+ started', m2, 20) is not None)
    m3 = mark(); C.click_page('.title'); logged(r'CHAT bubble opened on', m3, 10); time.sleep(1.2)
    for i in range(1, 5):
        C.panel("document.getElementById('composer-input').focus()"); C.type_panel(f'Message en attente numéro {i}'); time.sleep(0.15); C.enter_panel(); time.sleep(0.45)
    state = """(function(){var a=document.getElementById('app').getBoundingClientRect(),c=document.querySelector('.composer'),m=document.querySelector('.bq-more'),l=document.querySelector('.bq-list');
      var r=c?c.getBoundingClientRect():null;
      return {n:document.querySelectorAll('.bq').length,more:m?m.innerText.trim():'',open:m?m.getAttribute('aria-expanded'):'',list:l?Math.round(l.getBoundingClientRect().height):0,
              input:!!r&&r.height>30&&r.top>=a.top-1&&r.bottom<=a.bottom+1&&r.bottom<=innerHeight+1,app:[Math.round(a.width),Math.round(a.height)],win:[innerWidth,innerHeight]}})()"""
    st = C.wait_for(lambda: (lambda s: s if s and s.get('more') else None)(C.panel(state)), 6) or C.panel(state) or {}
    report['measures']['queue'] = {'closed': st}
    step('6 · 4 messages wait in the bubble: the 2 most recent are shown, « +2 en attente » above them', st.get('n') == 2 and st.get('more') == '+2 en attente' and st.get('open') == 'false', st)
    step('6 · …the input is whole inside the bubble (O5)', bool(st.get('input')), st)
    capture('6-file-attente')
    try:
        C.click_panel(selector='.bq-more'); time.sleep(0.5)
    except RuntimeError as e:
        step('6 · the « +N en attente » line can be clicked', False, e)
    st2 = C.panel(state) or {}
    report['measures']['queue']['open'] = st2
    step('6 · a click opens the whole list (160 px at most), the input still whole', st2.get('n') == 4 and st2.get('open') == 'true' and 0 < st2.get('list', 0) <= 160 and bool(st2.get('input')), st2)
    capture('6-file-attente-ouverte')
    try:
        C.click_panel(selector='.bq-more'); time.sleep(0.4)
    except RuntimeError:
        pass
    st3 = C.panel(state) or {}
    step('6 · a second click closes it', st3.get('n') == 2 and st3.get('open') == 'false', st3)


def part_private():
    # Night of 25 Nov 2026 (first real-window run of this suite): the window was opened on this script's plain file server
    # (PORT_ALL). Its pages name no file, so the folder could not be found without asking: the bubble kept « Choisissez le
    # dossier du projet pour commencer. », nothing was sent, and the two steps about what is stored failed — the script's
    # hole, not the product's. The site is now served as the sandbox's own, and the script says so before it sends.
    serve_site(PORT_PRIVATE)
    url = f'http://127.0.0.1:{PORT_PRIVATE}/'
    before_store, before_convs = store_entries(), normal_conversations()
    for how in ('closed', 'killed'):
        p = launch_private(url)
        try:
            C.shortcut('A'); time.sleep(2.0)
            dismiss_connect()
            opened = C.wait_for(C.panel_exists, 12)
            step(f'4 · private window ({how}): the chat opens with the mode (decision A)', bool(opened))
            ledgers, datas = private_files()
            step(f'4 · …its own folder exists beside its ledger ({how})', len(ledgers) == 1 and len(datas) == 1 and datas[0][:-5] == ledgers[0][:-7], f'{ledgers} {datas}')
            ws = C.ws_for(url[:24])
            r = at(url[:24], "(function(){var r=document.querySelector('.title').getBoundingClientRect();return [r.left+r.width/2,r.top+r.height/2]})()")
            for kind in ('mouseMoved', 'mousePressed', 'mouseReleased'):
                ws.call('Input.dispatchMouseEvent', {'type': kind, 'x': r[0], 'y': r[1], 'button': 'none' if kind == 'mouseMoved' else 'left', 'clickCount': 0 if kind == 'mouseMoved' else 1})
            # a private window writes nothing to the browser's log (src/main.cpp, Log): everything is read in the chat's page
            C.wait_for(lambda: chips() >= 1 or 'Demander' in (C.panel_text() or ''), 10)   # a chip (« Panneau ») or the element's bubble (« Commentaire », the layout of the last window)
            time.sleep(1.0); txt = C.panel_text() or ''
            step(f'4 · …its project folder is found without asking, as in a normal window ({how})', 'Choisissez le dossier' not in txt and 'Où est le code' not in txt, ' '.join(txt.split())[:200])
            C.panel("document.getElementById('composer-input').focus()"); C.type_panel(' Message privé jetonPrive'); time.sleep(0.2); C.enter_panel()
            sent_js = "(function(){var c=document.getElementById('composer-input');var own=c?c.innerText:'';return document.body.innerText.split('jetonPrive').length-1-(own.indexOf('jetonPrive')>=0?1:0)})()"
            step(f'4 · …a request starts a turn: the message is in the thread ({how})', bool(C.wait_for(lambda: (C.panel(sent_js) or 0) >= 1, 20)), ' '.join((C.panel_text() or '').split())[:200])
            # the turn is LEFT RUNNING (the fake agent's request is not answered): a private window closed in the middle of a
            # turn must erase everything too (25 Nov 2026: the snapshot store's lock kept the folder until the next start)
            C.wait_for(lambda: 'Refuser' in (C.panel_text() or ''), 20)
            time.sleep(1.5)
            data = PRIVATE / datas[0] if datas else None
            convs = sorted(f.name for f in (data / 'annotations-chat' / 'conversations').glob('*.json')) if data and (data / 'annotations-chat' / 'conversations').exists() else []
            step(f'4 · …its conversation is kept in that folder, not with the normal windows\' ({how})', len(convs) >= 1 and normal_conversations() == before_convs, f'{convs} / normal {normal_conversations()}')
            new_store = [e for e in store_entries() if e not in before_store]
            lines = (PRIVATE / ledgers[0]).read_text(encoding='utf-8', errors='replace').split() if ledgers else []
            step(f'4 · …the element sent is listed in the ledger ({how})', len(new_store) >= 1 and all(e.replace('\\', '/').split('/')[-1] in ' '.join(lines) for e in new_store), f'{new_store} / {lines[:6]}')
            if how == 'closed':
                capture('4-fenetre-privee')
                gone = close_window(p)
                time.sleep(1.0)
                ledgers2, datas2 = private_files()
                step('4 · the private window closes: its conversation, its snapshots and its element are erased', gone and not ledgers2 and not datas2 and store_entries() == before_store, f'{ledgers2} {datas2} store+{[e for e in store_entries() if e not in before_store]}')
            else:
                subprocess.run(['taskkill', '/PID', str(p.pid), '/T', '/F'], capture_output=True); time.sleep(1.5)
                ledgers2, datas2 = private_files()
                step('4 · the private window is killed: nothing could be erased yet (its ledger and its folder are still there)', len(ledgers2) == 1 and len(datas2) == 1, f'{ledgers2} {datas2}')
                n = subprocess.Popen([str(C.APP / 'browser.exe')], cwd=str(C.APP), env=env_for(None))   # the next start, a normal window
                (C.APP / 'qa.pid').write_text(str(n.pid)); pids.append(n.pid)
                cleaned = C.wait_for(lambda: private_files() == ([], []), 25, 0.5)
                time.sleep(1.0)
                step('4 · …the next start erases them (conversation, snapshots, element)', bool(cleaned) and store_entries() == before_store, f'{private_files()} store+{[e for e in store_entries() if e not in before_store]}')
                step('4 · …and the normal windows never saw that conversation', normal_conversations() == before_convs and not any('jetonPrive' in f.read_text(encoding='utf-8', errors='replace') for f in (C.APP / 'annotations-chat' / 'conversations').glob('*.json')) if (C.APP / 'annotations-chat' / 'conversations').exists() else True)
                close_window(n)
        finally:
            if p.poll() is None:
                subprocess.run(['taskkill', '/PID', str(p.pid), '/T', '/F'], capture_output=True)


def run_main():
    before = C.fingerprints(); report['fingerprintsBefore'] = before
    OUT.mkdir(parents=True, exist_ok=True)
    first = C.launch(extra_env={'WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS': ARGS}); pids.append(first.pid)
    try:
        part_panel_at_start()
        part_ten_elements()
        part_local_en_ligne()
        part_queue()
    except Exception:
        step('lot R (normal window): the script ran to its end', False, traceback.format_exc()[-500:])
    finally:
        C.quit_all(); time.sleep(1.0)
    try:
        part_private()
    except Exception:
        step('lot R (private window): the script ran to its end', False, traceback.format_exc()[-500:])
    finally:
        try:
            C.quit_all()
        except Exception:
            pass
        for s in servers:
            try:
                s.terminate(); s.wait(timeout=5)
            except Exception:
                s.kill()
        shutil.rmtree(FILES.parent, ignore_errors=True)
    step('real Claude / Codex configuration files unchanged', before == C.fingerprints())
    left = [pid for pid in pids if C.pid_alive(pid)]
    step('nothing of the test copy left running', not left, left)
    ok = all(s['ok'] for s in report['steps'])
    report['ok'] = ok
    (OUT / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=1), encoding='utf-8')
    print(f"\n{sum(1 for s in report['steps'] if s['ok'])} / {len(report['steps'])} steps — " + ('OK' if ok else 'FAILED'))
    return 0 if ok else 1


if __name__ == '__main__':
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding='utf-8')
        except Exception:
            pass
    sys.exit(run_main())
