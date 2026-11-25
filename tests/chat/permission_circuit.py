#!/usr/bin/env python3
"""The permission circuit in the REAL browser, bubble and panel — the owner's trial 0.3.952 (24 Nov 2026) replayed with
the fake agent: one request per change (the host asks once, answers once, the agent gets one outcome), a card that keeps
its shape when it is clicked or its switch is used, a thread that reads « Veut modifier … » until the answer and « A
modifié … » once done, a refused step kept as refused by the host (nothing said of it, also in the stored conversation),
two requests in a row, and in the bubble the mode and « Arrêter » under the card (« Arrêter » withdraws the request).
The page alone is tests/chat-panel/run.py (block « perm »); this one proves the host's side (design/chat-panel.md §27).

    python tests/chat/permission_circuit.py [--journey bulle|panneau[,…]]
    CHAT_QA_BUILD=<folder under build/>   the build to copy (default lot2-app)
    -> build/chat-qa/perm/report[-<journey>].json

No window on the user's screen: the browser runs on a desktop of its own (tests/chat/lot2_desktop.py — lot 2's harness,
which this file needs). Clicks are real mouse input through DevTools on the chat page (pressed and released at the same
point). Fake agent only, a brand-new profile, the real Claude / Codex configuration never touched.
"""
from __future__ import annotations
import json, pathlib, re, sys, time, traceback
HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import lot2_desktop as D   # noqa: E402
from lot2_desktop import C, V   # noqa: E402

ONLY = (sys.argv[sys.argv.index('--journey') + 1].split(',') if '--journey' in sys.argv else None)
B = D.Browser('perm', page_port=5219, cdp_port=9393)
R = D.Report(B.out)
EL = 'h1.hero-title'
CARDSEL = '.bperm,.perm'


def card():
    try:
        return C.panel("(function(){var c=document.querySelector('.bperm,.perm');if(!c)return null;return {shownAt:Math.round(c.__shownAt),now:Math.round(performance.now()),title:(document.getElementById('perm-t')||{}).textContent||''}})()")
    except Exception:
        return None


def fake_log():
    try:
        return (B.work / 'fake-agent.log').read_text(encoding='utf-8', errors='replace')
    except OSError:
        return ''


outcomes = lambda: re.findall(r'permission-outcome (\S+) ?(\S*)', fake_log())
answered = lambda since: re.findall(r'permission answered: (\S+)', D.log_since(since))
asked = lambda since: len(re.findall(r'permission asked', D.log_since(since)))
text = lambda: C.panel("document.getElementById('app').innerText") or ''
GEO = ("(function(){var c=document.querySelector('.bperm,.perm');if(!c)return null;var R=c.getBoundingClientRect(),o={w:Math.round(R.width),h:Math.round(R.height),out:[],cls:c.className};"
       "c.querySelectorAll('.perm-hd,.perm-t,.diff,.instead,.perm-ft,.bperm-file,.bperm-in,.bperm-ft,.btn').forEach(function(n){var r=n.getBoundingClientRect();"
       "var k=n.className.split(' ').slice(0,2).join('.')+(n.classList.contains('btn')?':'+n.firstChild.textContent:'');o[k]=[Math.round(r.left-R.left),Math.round(r.top-R.top),Math.round(r.width)];"
       "if(r.left<R.left-.5||r.right>R.right+.5)o.out.push(k)});o.doc=document.documentElement.scrollWidth>document.documentElement.clientWidth;return o})()")


def ask(words):
    m = D.mark(); V.focus_composer(); C.type_panel(words); time.sleep(0.15); C.enter_panel()
    return m, C.wait_for(card, timeout=25, every=0.03)


def turn_over(m, timeout=25):
    ok = C.wait_log(r'turn \S+ ended', timeout, m); time.sleep(0.8); return bool(ok)


def stored_refused():
    """The conversation as the host keeps it on disk: its tool calls marked refused."""
    out = []
    for root in (B.app, B.work):
        for f in root.rglob('*'):
            try:
                if f.is_file() and f.stat().st_size < 4_000_000 and f.suffix.lower() in ('', '.json', '.jsonl', '.txt'):
                    t = f.read_text(encoding='utf-8', errors='ignore')
                    if '"toolCall"' in t and '"refused"' in t:
                        out.append((str(f.relative_to(root)), len(re.findall(r'"refused"\s*:\s*true', t))))
            except OSError:
                pass
    return out


def circuit(j, bubble):
    sel = '.bperm' if bubble else '.perm'
    # ---- one request, asked once; the thread before the answer
    n0 = len(outcomes()); m, c = ask('vrai-autorisation svp')
    R.step(f'{j} · the agent\'s request shows its card', c, D.log_since(m)[-300:])
    if not c:
        return
    time.sleep(0.5); t0 = text()
    R.step(f'{j} · point 1 · ONE request: the host asked once, one card', asked(m) == 1 and C.panel(f"document.querySelectorAll({json.dumps(CARDSEL)}).length") == 1, {'asked': asked(m)})
    want = 'Attend votre accord…' if bubble else 'Veut modifier landing.css'
    R.step(f'{j} · point 3 · before the answer the thread reads « {want} » — never « A modifié », never « Modifie… »',
           want in t0 and 'A modifié' not in t0 and 'a modifié' not in t0 and 'Modifie landing' not in t0, t0[-400:].replace('\n', ' | '))
    R.measures[f'{j}-thread-waiting'] = t0[-400:]
    if not bubble:
        w = C.panel("(function(){var f=document.querySelector('.log .steps > .fold');if(!f)return null;f.click();var n=document.querySelector('.log .step.want');"
                    "var o=n?{text:n.textContent,color:getComputedStyle(n).color,spin:!!n.querySelector('.spin'),icon:n.querySelector('svg').getAttribute('viewBox')}:null;f.click();return o})()")
        R.step(f'{j} · in the opened list the waiting step reads « Attend votre accord » with a still clock, #A6A6A6 (no ring)',
               w and w['text'] == 'Attend votre accord' and w['color'] == 'rgb(166, 166, 166)' and not w['spin'] and w['icon'] == '0 0 256 256', w)
    # ---- points 2 and 4: a click on the card outside its buttons, then the switch: the card keeps its shape
    g0 = C.panel(GEO)
    C.click_panel(sel + (' .t' if bubble else ' .perm-t')) if not bubble else C.click_panel('.bperm-in .ic')
    time.sleep(0.3); g1 = C.panel(GEO)
    if bubble and C.panel("!document.getElementById('bview').hidden"):   # (a click on the file row opens the diff: not this test)
        C.panel("document.querySelector('.bview-x').click()"); time.sleep(0.3); g1 = C.panel(GEO)
    C.click_panel(sel + ' .always'); time.sleep(0.3); g2 = C.panel(GEO)
    on = C.panel(f"document.querySelector({json.dumps(sel + ' .always')}).getAttribute('aria-checked')")
    shape = lambda g: g and [g['w'], g['out'], g['doc']] + [g[k][:2] for k in sorted(g) if k not in ('w', 'h', 'out', 'doc', 'cls') and not k.startswith('btn')]
    R.step(f'{j} · point 2 · a click on the card outside its buttons: the same shape, nothing leaves the card, nothing answered',
           g1 and shape(g1) == shape(g0) and not g1['out'] and not answered(m), {'before': g0, 'after': g1})
    R.step(f'{j} · point 4 · the switch turns on with one click and the card keeps its shape', on == 'true' and shape(g2) == shape(g0) and not g2['out'] and not answered(m), {'checked': on, 'after': g2})
    C.click_panel(sel + ' .always'); time.sleep(0.2)   # off again: « Autoriser » is this once
    # ---- point 1: ONE real click on « Autoriser »
    C.click_panel(sel + ' .btn.primary'); time.sleep(0.6)
    R.step(f'{j} · point 1 · ONE real click on « Autoriser » answers: the host answered once, the agent got « allow » once, the card is gone',
           answered(m) == ['allow'] and outcomes()[n0:] == [('selected', 'allow')] and not card(), {'host': answered(m), 'agent': outcomes()[n0:]})
    R.step(f'{j} · the turn ends', turn_over(m))
    t1 = text(); done0 = len(re.findall(r'[Aa] modifié', t1))
    if not bubble:
        R.step(f'{j} · point 3 · once done the thread reads « A modifié landing.css »', 'A modifié landing.css' in t1 and 'Veut modifier' not in t1, t1[-300:].replace('\n', ' | '))
    # ---- refused: nothing is said of the edit, and the host keeps it as refused
    n1 = len(outcomes()); m, c = ask('vrai-autorisation encore')
    R.step(f'{j} · a second request shows its card (asked once)', c and asked(m) == 1)
    if c:
        C.click_panel(sel + ' .btn:not(.primary)'); time.sleep(0.6)
        R.step(f'{j} · ONE real click on « Refuser » refuses: host « reject » once, agent « reject » once', answered(m) == ['reject'] and outcomes()[n1:] == [('selected', 'reject')], {'host': answered(m), 'agent': outcomes()[n1:]})
        R.step(f'{j} · the turn ends', turn_over(m))
        t2 = text()
        R.step(f'{j} · point 3 · refused: nothing is said of the edit (no « A modifié », no « échec », no « Veut modifier »)',
               len(re.findall(r'[Aa] modifié', t2)) == done0 and 'échec' not in t2 and 'Veut modifier' not in t2, t2[-300:].replace('\n', ' | '))
        time.sleep(1.5); kept = stored_refused()
        R.measures[f'{j}-stored'] = kept
        R.step(f'{j} · the host keeps the refused step as refused in the stored conversation', any(n >= 1 for _, n in kept), kept)
    # ---- several messages in a row: each request asked once, answered once
    n2 = len(outcomes()); m = D.mark()
    C.panel("(function(){var w=window.chrome.webview;w.postMessage({v:1,type:'send',text:'permission un',elements:[],attachments:[]});w.postMessage({v:1,type:'send',text:'permission deux',elements:[],attachments:[]});return 1})()")
    c1 = C.wait_for(card, timeout=25, every=0.03)
    if c1:
        C.click_panel(sel + ' .btn.primary'); time.sleep(0.4)
        c2 = C.wait_for(lambda: (lambda k: k if k and k['shownAt'] != c1['shownAt'] else None)(card()), timeout=20, every=0.05)
        R.step(f'{j} · two messages in a row: the first answered once, then the second request shows its own card', answered(m) == ['allow'] and c2, {'host': answered(m)})
        if c2:
            C.click_panel(sel + ' .btn.primary'); time.sleep(0.6)
            R.step(f'{j} · …answered once too: 2 requests asked, 2 answers, never a duplicate', asked(m) == 2 and answered(m) == ['allow', 'allow'] and outcomes()[n2:] == [('selected', 'allow')] * 2, {'asked': asked(m), 'host': answered(m), 'agent': outcomes()[n2:]})
            turn_over(D.mark() - 1, 20)
    else:
        R.step(f'{j} · two messages in a row: the first request shows its card', False, D.log_since(m)[-300:])
    # ---- Codex asks to leave its sandbox (the fake agent sends the title codex-acp sends): said in the browser's language
    time.sleep(0.6); m, c = ask('mcptool Additional sandbox permissions|{}')
    R.step(f"{j} · « Additional sandbox permissions » is asked as « … demande plus d'accès à cet ordinateur »",
           c and c['title'].endswith("demande plus d'accès à cet ordinateur") and 'Additional' not in c['title'], c and c['title'])
    if c:
        C.click_panel(sel + ' .btn:not(.primary)'); time.sleep(0.6)
        R.step(f'{j} · …and « Refuser » refuses it (once)', answered(m) == ['reject'], answered(m))
        turn_over(m)


def bulle():
    j = 'bulle'
    R.step(f'{j} · « Mode Commentaire » from the band', B.layout(D.BANNER_COMMENT))
    m = D.mark(); C.click_page(EL)
    R.step(f'{j} · a click opens the bubble', C.wait_log(r'CHAT bubble opened on', 5, m) and B.bubble_region()); time.sleep(2.5)
    circuit(j, True)
    # the row under the card: « Arrêter » while a request waits
    n0 = len(outcomes()); m, c = ask('vrai-autorisation stop')
    if c:
        time.sleep(0.5)   # the card's entrance (200 ms) is over
        foot = C.panel("(function(){var t=document.querySelector('.send.stop'),c=document.querySelector('.bperm');if(!t||!c)return null;var r=t.getBoundingClientRect(),k=c.getBoundingClientRect(),a=document.getElementById('app').getBoundingClientRect();"
                       "return [Math.round(r.top-k.bottom),Math.round(a.bottom-r.bottom),k.bottom<=a.bottom,Math.round(a.height)]})()")
        R.step(f'{j} · the mode and « Arrêter » are under the card (12 px below), whole in the bubble', foot and foot[0] in (11, 12) and foot[1] >= 6 and foot[2], foot)   # 12 CSS px, measured at 125 %: 11.2 to 12
        R.measures[f'{j}-bubble-with-request'] = {'foot': foot, 'region': B.bubble_region()}
        C.click_panel('.send.stop'); time.sleep(1.2)
        since = D.log_since(m)
        R.step(f'{j} · « Arrêter » under the card stops the agent: the request is withdrawn (nothing answered by the page), the turn ends cancelled',
               not answered(m) and re.search(r'turn \S+ ended: cancelled', since) and not card(), [l for l in since.splitlines() if re.search(r'permission|turn ', l)][-5:])
        t = text()
        R.step(f'{j} · …and nothing is said of the edit that never happened', 'échec' not in t and 'Veut modifier' not in t, t[-200:].replace('\n', ' | '))


def panneau():
    j = 'panneau'
    if not C.wait_for(C.panel_exists, timeout=6):
        m = D.mark(); B.banner(D.BANNER_CHAT); C.wait_log(r'CHAT panel opened on', 8, m)
    R.step(f'{j} · the panel is open (« Chat à droite »)', C.wait_for(C.panel_exists, timeout=8)); time.sleep(2.0)
    circuit(j, False)


JOURNEYS = [('bulle', bulle), ('panneau', panneau)]


def main():
    before = C.fingerprints()
    fake = B.tools / 'fake-acp-agent.exe'
    assert fake.is_file(), f'missing {fake}'
    env = {'BROWSER_CHAT_FAKE_AGENT': f'"{fake}" --resume --log "{B.work / "fake-agent.log"}"'}
    try:
        for name, fn in JOURNEYS:
            if ONLY and name not in ONLY:
                continue
            print(f'--- {name}', flush=True)
            try:
                try:
                    (B.work / 'fake-agent.log').unlink()
                except OSError:
                    pass
                R.step(f'{name} · the browser starts on a brand-new profile', B.launch(env, fresh=True))
                R.step(f'{name} · nothing of the test browser is on the user\'s desktop', not B.on_user_desktop(), B.on_user_desktop())
                R.step(f'{name} · the mode turns on', B.mode_on()); time.sleep(1.2)
                if B.window('BrowserAnnotationLayer', 'Connecter un agent'):
                    B.invoke('AnnotationConnectLater') or B.invoke('AnnotationConnectClose'); time.sleep(0.8)
                fn()
                errs = [l for l in D.log_since(0).splitlines() if re.search(r'CHAT panel (error|warn)|panel renderer failed', l)]
                R.step(f'{name} · the chat page reported no error', not errs, errs)
            except BaseException:
                traceback.print_exc()
                R.step(f'{name} · the journey ran to its end', False, traceback.format_exc()[-900:])
            finally:
                try:
                    (B.out / f'{name}.log').write_text(C.log_text(), encoding='utf-8')
                except Exception:
                    pass
                B.quit()
    finally:
        B.close()
        R.step('real Claude / Codex configuration files unchanged', before == C.fingerprints())
        left = D.leftovers(B)
        R.step('nothing of this suite is left running (by executable path)', not left, left)
        R.save({'journeys': ONLY or [n for n, _ in JOURNEYS]}, name='report' + ('-' + '-'.join(ONLY) if ONLY else ''))
    failed = R.failed
    print(f"{len(R.steps) - len(failed)} / {len(R.steps)} steps passed")
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
