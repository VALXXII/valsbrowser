#!/usr/bin/env python3
"""Mode Commentaire v4 (R26 – R38), ONE real-window pass of everything (H1 + H2 + P), on the FAKE agent, with
frame-by-frame measures for R29, R34, R36, R37:

    python tests/chat/chat_qa_comment_v4.py      # -> build/chat-qa/comment-v4/report.json + captures + frame series

  R28  the bubble's composer is right and stable from its first image (mode, model AND effort; no size change)
  R29  during a turn and when the diff opens (R38) the bubble's surface keeps its corner, to the pixel
  R32  a click then Échap (both layouts): no conversation frame + icon
  R34  six fast switches Panneau <-> Commentaire: the page's layout is always the right one
  R35  a chip added in the panel never shows in a bubble
  R26  a chip hovered in the panel: the page shows its element (hover frame + labels)
  R36  after the agent's CSS-only edit: no reload (values glide) — frames captured; log of the transition
  R37  a fast scroll with an element selected: frame and inspector move together (frames captured)

Same guards as tests/chat/chat_qa.py. Real pointer moved: only when the computer is free (« Libre »).
"""
from __future__ import annotations
import ctypes
import ctypes.wintypes as W
import json
import subprocess
import sys
import time
import traceback

import chat_qa as C
from chat_qa import Q
import chat_qa_comment as CC
import chat_qa_comment_v2 as V
import chat_qa_comment_v3 as V3

OUT = C.QA / 'comment-v4'
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
        path = C.grab(f'comment-v4/{name}')
        report['captures'].append(path.name)
    except Exception as e:
        print('capture failed', name, e)


def region_corner():
    bw = CC.bubble_window()
    if not bw:
        return None
    rg = W.RECT(); kind = u32.GetWindowRgnBox(bw[0], ctypes.byref(rg))
    if kind == 1:
        return None
    r = V3.win_rect(bw[0])
    return (r[0] + rg.left, r[1] + rg.top)   # the surface's top-left on screen (the region's)


def composer_bottom():
    return C.panel("(function(){var b=document.querySelector('.cbar')||document.querySelector('.composer');var a=document.getElementById('app');"
                   "var t=(b||a||document.body).innerText||'';var r=a?a.getBoundingClientRect():null;return [t.replace(/\\s+/g,' ').slice(-80), r?Math.round(r.height):0]})()")


def run_main():
    before = C.fingerprints(); report['fingerprintsBefore'] = before
    OUT.mkdir(parents=True, exist_ok=True)
    flog = C.WORK / 'fake-agent.log'
    try:
        flog.unlink()
    except OSError:
        pass
    fake = C.TOOLS / 'fake-acp-agent.exe'
    C.launch(extra_env={'BROWSER_CHAT_PANEL_IDLE_MS': '60000', 'BROWSER_CHAT_AGENT_IDLE_MS': '600000',
                        'BROWSER_CHAT_FAKE_AGENT': f'"{fake}" --resume --claude-like --log "{flog}" --edit "{V.GAP_EDIT}"'})
    try:
        time.sleep(1.0)
        C.shortcut('A'); step('mode on', logged(r'ANNOTATE mode on', 0, 10)); time.sleep(1.2)
        if C.invoke('AnnotationConnectLater') or C.invoke('AnnotationConnectClose'):
            time.sleep(0.6)
        u32.SetForegroundWindow(Q.main_window())
        dpr = C.evaluate(C.URL, 'devicePixelRatio') or 1

        # ---- R35 / R26 / R32 (panel): a chip in the panel; hovering it shows its element; no conversation mark
        m0 = mark(); C.invoke('AnnotationAskAgent'); logged(r'CHAT panel opened', m0, 10); time.sleep(1.5)
        C.click_page('.field-label'); time.sleep(1.2)
        chips = C.panel("document.querySelectorAll('.chip').length")
        step('panel: the clicked element is a chip of the panel\'s composer', (chips or 0) >= 1, chips)
        step('R32 · panel: a click without sending leaves no conversation frame + icon', not CC.conv_marks(), CC.conv_marks())
        at = C.panel("(function(){var c=document.querySelector('.chip');if(!c)return null;var r=c.getBoundingClientRect();return [r.left+r.width/2,r.top+r.height/2]})()")
        if at:
            C.ws_for(C.PANEL).call('Input.dispatchMouseEvent', {'type': 'mouseMoved', 'x': at[0], 'y': at[1]}); time.sleep(0.8)
        peek = V3.overlay_labels()
        report['measures']['r26'] = peek
        step('R26 · a chip hovered: the page shows its element (frame + labels)', len(peek) >= 1 and any('field' in t or 'span' in t for t, _ in peek), peek)
        capture('r26-survol-puce')
        if at:
            C.ws_for(C.PANEL).call('Input.dispatchMouseEvent', {'type': 'mouseMoved', 'x': 5, 'y': 5}); time.sleep(0.4)

        # ---- R34: six fast switches; the page's layout always the right one
        wrong = []
        for i in range(6):
            to_comment = i % 2 == 0
            C.invoke('AnnotationLayoutComment' if to_comment else 'AnnotationAskAgent'); time.sleep(0.35 if i < 5 else 1.2)
            cls = C.panel("document.documentElement.className") or ''
            panel_visible = 'CHAT panel opened' in C.log_text()[m0:] and not to_comment
            if not to_comment and 'bub' in cls.split():
                wrong.append((i, 'panel shows the bubble layout', cls))
        time.sleep(1.0)
        step('R34 · six fast switches: the panel never shows the bubble layout', not wrong, wrong)
        capture('r34-apres-bascules')
        mc = mark(); C.invoke('AnnotationLayoutComment'); step('back to Commentaire', logged(r'CHAT layout: Commentaire', mc, 5) is not None); time.sleep(1.5)

        # ---- R32 (Commentaire) + R35: a click, Échap: nothing; the panel's chip is not in the bubble
        m1 = mark(); C.click_page('.btn-primary')
        logged(r'CHAT bubble opened on', m1, 10); time.sleep(0.2)
        # R28: the composer's bottom, sampled from the first image
        samples = []
        t0 = time.time()
        while time.time() - t0 < 2.5:
            samples.append(composer_bottom()); time.sleep(0.05)
        texts = [s[0] for s in samples if s]; heights = sorted(set(s[1] for s in samples if s and s[1]))
        report['measures']['r28'] = {'texts': sorted(set(texts)), 'heights': heights}
        step('R28 · the bubble\'s composer: mode, model and effort from the first image, no change of size', len(set(texts)) == 1 and len(heights) == 1 and 'Moyen' in (texts[0] if texts else ''), report['measures']['r28'])
        bchips = C.panel("document.querySelectorAll('.chip').length")
        step('R35 · the panel\'s chip is not in the bubble', bchips == 0, bchips)
        V.focus_composer(); C.key_panel('Escape', 'Escape', 27); time.sleep(0.8); V.pointer_away()
        step('R32 · Commentaire: a click then Échap leaves no conversation frame + icon', not CC.conv_marks(), CC.conv_marks())
        capture('r32-rien-apres-echap')

        # ---- R29 / R38: a turn and the diff, the surface's corner sampled every ~4 ms
        m2 = mark(); C.click_page('.btn-primary'); logged(r'CHAT bubble opened on', m2, 10); time.sleep(0.8)
        el0 = CC.element_screen('.btn-primary'); c0 = region_corner()
        s29 = V3.Sampler(region_corner); s29.start()
        m3 = mark(); V.send_in_view('Change le gap en 8 px')   # the fake agent's real edit of styles/checkout.css (a CSS-only turn: R36 glide)
        answered = None
        for _ in range(16):
            try:
                C.click_panel(text='Autoriser')
            except Exception:
                pass
            answered = logged(r'CHAT permission answered', m3, 1)
            if answered:
                break
        end = logged(r'CHAT turn t\d+ ended', m3, 30)
        s29.stop = True; s29.join(); during = sorted(set(c for t, c in s29.samples if c))   # until the edit shows (the element then moves)
        time.sleep(1.5)
        if not region_corner():   # the user's own test browser (another process) may take the foreground: the bubble closed — open it again
            mr = mark(); C.click_page('.btn-primary'); logged(r'CHAT bubble opened on', mr, 10); time.sleep(1.2); report['measures']['r29reopened'] = True
        V.pointer_away(); time.sleep(0.3)
        s29b = V3.Sampler(region_corner); s29b.start()
        try:
            C.click_panel(selector='.bt-bar-main'); time.sleep(1.0)
        except Exception as e:
            print('no changes bar', e)
        capture('r38-diff-ouvert')
        V.focus_composer(); C.key_panel('Escape', 'Escape', 27); time.sleep(0.8)
        s29b.stop = True; s29b.join(); after = sorted(set(c for t, c in s29b.samples if c))
        el1 = CC.element_screen('.btn-primary')
        off0 = (c0[0] - el0[0], c0[1] - el0[1]) if c0 else None; off1 = (after[0][0] - el1[0], after[0][1] - el1[1]) if after else None
        report['measures']['r29'] = {'during': during, 'afterEditAndDiff': after, 'element': [el0, el1], 'offsetBefore': off0, 'offsetAfter': off1}
        step('R29 / R38 · new cards, the edit moving the element, the diff: the surface keeps its corner and its offset to the element',
             end is not None and c0 is not None and all(c[1] == c0[1] and min(c0[0], c0[0] + el1[0] - el0[0]) <= c[0] <= max(c0[0], c0[0] + el1[0] - el0[0]) for c in during) and len(after) == 1 and off0 is not None and off1 is not None and abs(off0[0] - off1[0]) <= 1 and abs(off0[1] - off1[1]) <= 1, report['measures']['r29'])

        # ---- R36: the CSS-only edit: frames of the page while it changes
        m4 = mark()
        tr = logged(r'CHAT files changed and no dev server reload: the page shows them', m3, 5)
        report['measures']['r36'] = {'log': tr.group(0) if tr else None, 'transitionLog': [l for l in C.log_text()[m3:].splitlines() if 'transition' in l.lower() or 'glide' in l.lower()][:6]}
        step('R36 · after the agent\'s edit the page is shown through the transition (log)', tr is not None, report['measures']['r36'])

        # ---- R37: a fast scroll with an element selected: frames (frame and inspector together)
        el = CC.element_screen('.btn-primary')
        region = (max(0, el[0] - 60), max(0, el[1] - 260), el[2] + 400, el[3] + 260)
        s37 = V3.Sampler(lambda: None, region); s37.start()
        C.page("document.body.style.paddingBottom='1600px'")
        for _ in range(8):
            C.ws_for(C.URL).call('Input.dispatchMouseEvent', {'type': 'mouseWheel', 'x': 200, 'y': 300, 'deltaX': 0, 'deltaY': 60}); time.sleep(0.016)
        time.sleep(0.5); s37.stop = True; s37.join()
        for i, (t, img) in enumerate(s37.frames[:40]):
            img.save(OUT / f'r37-defilement-{i:02d}.png')
        report['captures'].append(f'r37-defilement-00..{min(len(s37.frames), 40) - 1:02d}.png')
        step('R37 · fast scroll frames captured (frame and inspector, for the audit)', len(s37.frames) >= 10, len(s37.frames))
    except Exception:
        traceback.print_exc()
        step('run without exception', False, traceback.format_exc()[-400:])
    finally:
        C.quit_all()
        after = C.fingerprints(); report['fingerprintsAfter'] = after
        step('real Claude / Codex configuration files unchanged', before == after)
        ps = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq fake-acp-agent.exe', '/NH'], capture_output=True, text=True).stdout
        step('nothing left running (fake agent)', 'fake-acp-agent' not in ps)
        (OUT / 'report.json').write_text(json.dumps(report, indent=2, ensure_ascii=False, default=str), encoding='utf-8')
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
