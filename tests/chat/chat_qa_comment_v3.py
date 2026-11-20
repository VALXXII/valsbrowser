#!/usr/bin/env python3
"""Mode Commentaire v3 (R13 – R25), the host's points, checked in a REAL window on the FAKE agent, with pixel and
frame-by-frame measures:

    python tests/chat/chat_qa_comment_v3.py      # -> build/chat-qa/comment-v3/report.json + captures + frame series

  R13  the bubble starts its agent at once (no message sent): model list + model · effort before any turn
  R14  the hover labels in Paper's style: two labels, the name | the size « 240x44 »
  R15  an element touching the window's left / right side: frame and labels 2 px in
  R16  the selected element's padding / gap stay drawn while its bubble is open
  R18  the bubble's window is placed and sized BEFORE it shows: sampled every ~4 ms during the opening, it never moves
  R19  a menu opened in the bubble: the window does not move, the surface's pixels outside the menu are the same
  R20  the surface is 320 px wide
  R24  no « annotations en attente » suggestion anywhere
  R25  panel: the selection switched off / on — the page and the panel move in small steps with the band, no jump

Same guards as tests/chat/chat_qa.py (own copy, own profile, BROWSER_ANNOTATIONS_TEST=1, the real configs
fingerprinted, nothing left running). Real pointer moved: only when the computer is free.
"""
from __future__ import annotations
import ctypes
import ctypes.wintypes as W
import json
import subprocess
import sys
import threading
import time
import traceback

import chat_qa as C
from chat_qa import Q
import chat_qa_comment as CC
import chat_qa_comment_v2 as V

OUT = C.QA / 'comment-v3'
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
        path = C.grab(f'comment-v3/{name}')
        report['captures'].append(path.name)
    except Exception as e:
        print('capture failed', name, e)


def grab(rect):
    from PIL import ImageGrab
    return ImageGrab.grab(bbox=rect, all_screens=True).convert('RGB')


def win_rect(h):
    r = W.RECT(); u32.GetWindowRect(h, ctypes.byref(r)); return (r.left, r.top, r.right, r.bottom)


class Sampler(threading.Thread):
    """Samples a function every ~4 ms (and optionally grabs the screen region every ~16 ms) until stopped."""
    def __init__(self, fn, region=None, every_frames=1):
        super().__init__(daemon=True); self.fn = fn; self.region = region; self.samples = []; self.frames = []; self.stop = False
    def run(self):
        last = 0
        while not self.stop:
            t = time.perf_counter()
            try:
                self.samples.append((t, self.fn()))
            except Exception as e:
                self.samples.append((t, ('error', str(e))))
            if self.region and t - last >= 0.016 and len(self.frames) < 60:
                try:
                    self.frames.append((t, grab(self.region))); last = t
                except Exception:
                    pass
            time.sleep(0.004)


def overlay_labels():
    """The page's hover labels (closed shadow root): visible `.x.l` texts and their transforms."""
    o = V.overlay()
    return [(x['text'], x['style']) for x in o if x['cls'].split() == ['x', 'l'] and 'visibility: hidden' not in x['style']]


def tx(style):
    import re
    m = re.search(r'translate\(([-\d.]+)px,\s*([-\d.]+)px\)', style or '')
    return (float(m.group(1)), float(m.group(2))) if m else None


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
        m0 = mark(); C.invoke('AnnotationLayoutComment'); step('Commentaire chosen', logged(r'CHAT layout: Commentaire', m0, 5) is not None)
        dpr = C.evaluate(C.URL, 'devicePixelRatio') or 1

        # ---- R14: hover labels in Paper's style
        el = CC.element_screen('.btn-primary'); pg = CC.page_widget()
        cx, cy = ((el[0] + el[2]) / 2 - pg[0]) / dpr, ((el[1] + el[3]) / 2 - pg[1]) / dpr
        C.ws_for(C.URL).call('Input.dispatchMouseEvent', {'type': 'mouseMoved', 'x': cx, 'y': cy}); time.sleep(0.5)
        labels = overlay_labels(); size = C.evaluate(C.URL, "(function(){var r=document.querySelector('.btn-primary').getBoundingClientRect();return Math.round(r.width)+'x'+Math.round(r.height)})()")
        report['measures']['r14'] = labels
        step('R14 · hover: two labels in Paper\'s style, « name » | « LxH »', len(labels) == 2 and labels[0][0] == 'button.btn-primary' and labels[1][0] == size, labels)
        capture('r14-survol')

        # ---- R15: an element touching the window's sides
        C.page("(function(){var d=document.createElement('div');d.id='qa-edge';d.style.cssText='position:absolute;left:0;top:640px;width:100%;height:40px;background:#e8e8e8';document.body.appendChild(d);return 1})()")
        time.sleep(0.3)
        vw = C.evaluate(C.URL, 'innerWidth')
        C.ws_for(C.URL).call('Input.dispatchMouseEvent', {'type': 'mouseMoved', 'x': 200, 'y': 655 - C.evaluate(C.URL, 'scrollY')}); time.sleep(0.5)
        labs = overlay_labels(); pos = [tx(s) for _, s in labs]
        o = V.overlay(); fills = [tx(x['style']) for x in o if x['cls'].split() == ['x', 'f']]
        ok15 = len(pos) == 2 and pos[0] and pos[0][0] >= 2 - 0.01 and fills and fills[0] and fills[0][0] >= 2 - 0.01
        report['measures']['r15'] = {'labels': labs, 'fill': fills, 'innerWidth': vw}
        step('R15 · an element touching the sides: frame and labels 2 px in', ok15, report['measures']['r15'])
        capture('r15-bord')
        C.page("document.getElementById('qa-edge').remove()")

        # ---- R18: the opening, sampled every ~4 ms: the window never moves once shown
        hwnd_box = {}
        def bubble_sample():
            bw = CC.bubble_window()
            if not bw:
                return None
            hwnd_box['h'] = bw[0]
            return (bool(u32.IsWindowVisible(bw[0])), win_rect(bw[0]))
        region = (max(0, el[0] - 40), max(0, el[1] - 120), el[2] + 760, el[3] + 420)
        s18 = Sampler(bubble_sample, region); s18.start()
        m1 = mark(); C.click_page('.btn-primary')
        opened = logged(r'CHAT bubble opened on (\S+) for conversation (c_\w+)', m1, 10)
        time.sleep(1.2); s18.stop = True; s18.join()
        shown = [r for _, r in s18.samples if r and r[0]]
        rects = sorted(set(r[1] for r in shown))
        report['measures']['r18'] = {'samples': len(s18.samples), 'shownSamples': len(shown), 'rects': rects, 'log': (logged(r'CHAT bubble shown at its place ([^\r\n]+)', m1, 2) or [None])[0]}
        step('R18 · the window is shown once, already at its place and size (no move while it appears)', opened is not None and len(shown) > 5 and len(rects) == 1, report['measures']['r18'])
        for i, (t, img) in enumerate(s18.frames[:40]):
            img.save(OUT / f'r18-ouverture-{i:02d}.png')
        report['captures'].append(f'r18-ouverture-00..{min(len(s18.frames), 40) - 1:02d}.png')

        # ---- R13: the bubble's agent starts with the bubble (no message sent): its lists come before any turn
        st = logged(r'CHAT session starting', m1, 15); ready = logged(r'CHAT session start: ready', m1, 20)
        step('R13 · the bubble starts its agent at once (model list, model · effort before any message)', st is not None and ready is not None and 'turn t' not in C.log_text()[m1:])
        time.sleep(0.8)
        report['measures']['r13composer'] = (C.panel("(function(){var b=document.querySelector('.cbar,.composer,#composer,.cin');var e=document.body.innerText||'';return e.slice(-160)})()") or '')
        capture('r13-bulle-modele')

        # ---- R16: the selected element's inspector while its bubble is open; R20: 320 px
        V.pointer_away()
        bf = V.bubble_frame(); report['measures']['r16'] = bf
        step('R16 · bubble open: the element\'s frame + labels and its padding / gap inspector stay', bf['frames'] == 1 and bf['inspector'] == 1, bf)
        wsurf = C.panel("(function(){var a=document.getElementById('app');return a?Math.round(a.getBoundingClientRect().width):0})()")
        step('R20 · the bubble\'s surface is 320 px wide', wsurf == 320, wsurf)
        capture('r16-inspecteur-bulle-ouverte')

        # ---- R19: a menu in the bubble: the window does not move; the surface's pixels outside the menu do not change
        h = hwnd_box.get('h'); w0 = win_rect(h)
        ins = C.panel(V.FLOATS_JS) or {}; i0 = ins.get('inset', {})
        surf = (w0[0] + int(i0.get('left', 0) * dpr), w0[1] + int(i0.get('top', 0) * dpr), w0[0] + int((i0.get('left', 0) + 320) * dpr), w0[1] + int((i0.get('top', 0) + 104) * dpr))
        bubble_h = C.panel("(function(){var a=document.getElementById('app');return a?Math.round(a.getBoundingClientRect().height):0})()") or 104
        surf = (surf[0], surf[1], surf[2], w0[1] + int((i0.get('top', 0) + bubble_h) * dpr))
        before_img = grab(surf)
        s19 = Sampler(lambda: win_rect(h)); s19.start()
        C.click_panel(selector='button.m'); time.sleep(0.8)
        s19.stop = True; s19.join()
        f = C.panel(V.FLOATS_JS) or {}
        after_img = grab(surf)
        # the menu and its shadow (0 16px 48px) are excluded from the comparison
        cover = [((x['x'] - 56) * dpr + w0[0] - surf[0], (x['y'] - 56) * dpr + w0[1] - surf[1], (x['r'] + 56) * dpr + w0[0] - surf[0], (x['b'] + 72) * dpr + w0[1] - surf[1]) for x in f.get('floats', [])]
        diff, n = 0, 0
        pa, pb = before_img.load(), after_img.load()
        for yy in range(0, before_img.size[1], 2):
            for xx in range(0, before_img.size[0], 2):
                if any(c[0] <= xx <= c[2] and c[1] <= yy <= c[3] for c in cover):
                    continue
                n += 1; d = sum(abs(a - b) for a, b in zip(pa[xx, yy], pb[xx, yy]))
                if d > 6: diff += 1
        moves = sorted(set(r for _, r in s19.samples))
        report['measures']['r19'] = {'windowRects': moves, 'comparedPixels': n, 'changedPixels': diff, 'floats': [x.get('label') for x in f.get('floats', [])]}
        step('R19 · a menu opens: the window does not move, the surface\'s pixels outside the menu are the same', len(moves) == 1 and n > 200 and diff == 0, report['measures']['r19'])
        before_img.save(OUT / 'r19-surface-avant.png'); after_img.save(OUT / 'r19-surface-apres.png'); report['captures'] += ['r19-surface-avant.png', 'r19-surface-apres.png']
        V.focus_composer(); C.key_panel('Escape', 'Escape', 27); time.sleep(0.5)

        # ---- R24 and R25: the panel
        m2 = mark(); C.invoke('AnnotationAskAgent'); logged(r'CHAT panel opened', m2, 10); time.sleep(1.5)
        txt = C.panel_text() or ''
        step('R24 · no « annotations en attente » suggestion', 'annotations en attente' not in txt and "l'annotation en attente" not in txt, txt[-160:].replace('\n', ' | '))
        def layout_sample():
            ws = [x for x in Q.windows() if x[1] == 'BrowserChatPanelHost' or x[1] == 'Chrome_WidgetWin_0']
            pgw = CC.page_widget()
            return (pgw[1], tuple(sorted(x[3][1] for x in Q.windows() if x[1] == 'BrowserAnnotationLayer')))
        for label, want in (('off', 'off'), ('on', 'on')):
            s25 = Sampler(lambda: CC.page_widget()[1]); s25.start()
            m3 = mark(); C.shortcut('A'); logged(r'ANNOTATE selection ' + want, m3, 5); time.sleep(0.6)
            s25.stop = True; s25.join()
            ys = [y for _, y in s25.samples if isinstance(y, int)]
            steps_ = [abs(b - a) for a, b in zip(ys, ys[1:]) if b != a]
            report['measures'][f'r25-{label}'] = {'from': ys[0] if ys else None, 'to': ys[-1] if ys else None, 'distinct': len(set(ys)), 'maxStep': max(steps_) if steps_ else 0}
            step(f'R25 · selection {label}: the page moves with the band in small steps (no jump)', ys and ys[0] != ys[-1] and len(set(ys)) >= 4 and max(steps_) <= int(16 * dpr), report['measures'][f'r25-{label}'])
            capture(f'r25-selection-{label}')
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
