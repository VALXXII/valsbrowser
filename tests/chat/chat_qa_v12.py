#!/usr/bin/env python3
"""Chat intégré V1.2 — the user's real-test feedback, checked in a REAL window on the FAKE agent.

    python tests/chat/chat_qa_v12.py          # -> build/chat-qa/v12/report.json + PNG captures

Same guards and same copy of the browser as tests/chat/chat_qa.py (build/chat-qa-app, its own profile, agent
configurations redirected under build/, BROWSER_ANNOTATIONS_TEST=1, the real Claude / Codex files fingerprinted before
and after). The agent is the fake ACP agent with the real Claude adapter's lists (--claude-like: « Default » effort).
The real mouse pointer is moved over OUR window only (SetCursorPos); clicks and keys go through CDP and messages
posted to our own windows. One run at a time, nothing left running.

  1  the note's text reaches the agent (session/prompt) and the user's bubble, with the element, on the FIRST opening
  2  no « Default » effort: Medium set at the session start, in French in the button and the turn's header
  4  the gauge's bubble goes when the real pointer leaves the gauge / the panel
  5  the « done » mark and the selection frame stay on their element while the page scrolls (no lag)
  6  the pointer hidden by typing in the note comes back when the mouse moves over the page
  7  chat closed: a click opens the note; chat open: a click is a chip in the composer, the caret goes there
"""
from __future__ import annotations
import ctypes
import ctypes.wintypes as W
import json
import re
import sys
import time
import traceback

import chat_qa as C
from chat_qa import Q

OUT = C.QA / 'v12'
NOTE = 'Mets ce titre en gras'
report = {'steps': [], 'measures': {}, 'captures': []}
u32 = ctypes.windll.user32; g32 = ctypes.windll.gdi32
g32.GetObjectW.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p]
g32.GetDIBits.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint, ctypes.c_uint, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint]
g32.DeleteObject.argtypes = [ctypes.c_void_p]; g32.CreateCompatibleDC.argtypes = [ctypes.c_void_p]; g32.CreateCompatibleDC.restype = ctypes.c_void_p
g32.CreateCompatibleBitmap.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int]; g32.CreateCompatibleBitmap.restype = ctypes.c_void_p
g32.SelectObject.argtypes = [ctypes.c_void_p, ctypes.c_void_p]; g32.SelectObject.restype = ctypes.c_void_p
g32.BitBlt.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_uint]
g32.DeleteDC.argtypes = [ctypes.c_void_p]
u32.GetDC.restype = ctypes.c_void_p; u32.GetDC.argtypes = [ctypes.c_void_p]; u32.ReleaseDC.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
u32.DrawIconEx.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_uint, ctypes.c_void_p, ctypes.c_uint]
u32.GetIconInfo.argtypes = [ctypes.c_void_p, ctypes.c_void_p]


class CURSORINFO(ctypes.Structure):
    _fields_ = [('cbSize', W.DWORD), ('flags', W.DWORD), ('hCursor', ctypes.c_void_p), ('pt', W.POINT)]


class ICONINFO(ctypes.Structure):
    _fields_ = [('fIcon', W.BOOL), ('x', W.DWORD), ('y', W.DWORD), ('mask', ctypes.c_void_p), ('color', ctypes.c_void_p)]


class BIH(ctypes.Structure):
    _fields_ = [('size', W.DWORD), ('w', W.LONG), ('h', W.LONG), ('planes', W.WORD), ('bpp', W.WORD), ('comp', W.DWORD), ('img', W.DWORD),
                ('xp', W.LONG), ('yp', W.LONG), ('cu', W.DWORD), ('ci', W.DWORD)]


def step(name, ok, detail=''):
    report['steps'].append({'step': name, 'ok': bool(ok), 'detail': str(detail)})
    print(('PASS ' if ok else 'FAIL ') + name + (' — ' + str(detail) if detail else ''), flush=True)
    return bool(ok)


def mark():
    return len(C.log_text())


def logged(pattern, after=0, timeout=15.0):
    return C.wait_log(pattern, timeout, after)


def cursor():
    c = CURSORINFO(); c.cbSize = ctypes.sizeof(CURSORINFO); u32.GetCursorInfo(ctypes.byref(c))
    return {'showing': bool(c.flags & 1), 'handle': c.hCursor or 0, 'pt': (c.pt.x, c.pt.y)}


def capture(name, with_cursor=False):
    """The browser window as on screen; with_cursor: the pointer drawn where it is (a screen capture never has it)."""
    try:
        path = C.grab(f'v12/{name}')
        if with_cursor:
            from PIL import Image
            c = cursor()
            if c['showing'] and c['handle']:
                ii = ICONINFO(); u32.GetIconInfo(ctypes.c_void_p(c['handle']), ctypes.byref(ii))
                for b in (ii.mask, ii.color):
                    if b:
                        g32.DeleteObject(b)
                h = Q.main_window(); r = W.RECT()
                if ctypes.windll.dwmapi.DwmGetWindowAttribute(h, 9, ctypes.byref(r), ctypes.sizeof(r)) != 0:
                    u32.GetWindowRect(h, ctypes.byref(r))
                size = 96; sx, sy = c['pt'][0] - ii.x, c['pt'][1] - ii.y
                screen = u32.GetDC(None); mem = g32.CreateCompatibleDC(screen); bmp = g32.CreateCompatibleBitmap(screen, size, size); old = g32.SelectObject(mem, bmp)
                g32.BitBlt(mem, 0, 0, size, size, screen, sx, sy, 0x00CC0020)
                u32.DrawIconEx(mem, 0, 0, ctypes.c_void_p(c['handle']), 0, 0, 0, None, 3)
                bi = BIH(); bi.size = ctypes.sizeof(BIH); bi.w = size; bi.h = -size; bi.planes = 1; bi.bpp = 32
                buf = (ctypes.c_ubyte * (size * size * 4))()
                g32.SelectObject(mem, old); g32.GetDIBits(mem, bmp, 0, size, buf, ctypes.byref(bi), 0)
                g32.DeleteObject(bmp); g32.DeleteDC(mem); u32.ReleaseDC(None, screen)
                patch = Image.frombuffer('RGBA', (size, size), bytes(buf), 'raw', 'BGRA', 0, 1).convert('RGB')
                img = Image.open(path).convert('RGB'); img.paste(patch, (sx - r.left, sy - r.top)); img.save(path)
        report['captures'].append(path.name)
    except Exception as e:   # a capture never fails the run
        print('capture failed', name, e)


def widgets():
    """The page's and the panel's render windows on screen, widest (the page) first."""
    out = []
    @ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    def cb(h, _):
        n = ctypes.create_unicode_buffer(128); u32.GetClassNameW(h, n, 128)
        if n.value == 'Chrome_RenderWidgetHostHWND' and u32.IsWindowVisible(h):
            r = W.RECT(); u32.GetWindowRect(h, ctypes.byref(r)); out.append((r.left, r.top, r.right, r.bottom))
        return True
    u32.EnumChildWindows(Q.main_window(), cb, 0)
    return sorted(out, key=lambda r: r[2] - r[0], reverse=True)


def page_point(sel, dy=0.5):
    r = widgets()[0]; dpr = C.page('devicePixelRatio')
    b = C.page("(function(){var r=document.querySelector(%s).getBoundingClientRect();return [r.left+r.width/2,r.top+r.height*%s]})()" % (json.dumps(sel), dy))
    return int(r[0] + b[0] * dpr), int(r[1] + b[1] * dpr)


def panel_point(sel):
    ws = widgets(); r = ws[1] if len(ws) > 1 else ws[0]; dpr = C.panel('devicePixelRatio')
    b = C.panel("(function(){var r=document.querySelector(%s).getBoundingClientRect();return [r.left+r.width/2,r.top+r.height/2]})()" % json.dumps(sel))
    return int(r[0] + b[0] * dpr), int(r[1] + b[1] * dpr)


def glide_to(x0, y0, x1, y1, steps=8):
    for k in range(steps):
        u32.SetCursorPos(x0 + (x1 - x0) * (k + 1) // steps, y0 + (y1 - y0) * (k + 1) // steps); time.sleep(0.03)


def shadow_rects(ws, want, sel, samples, gap=0.012):
    """The page's closed shadow root, read through DevTools: the box whose class list holds `want` and the element `sel`,
    measured in the SAME task (main thread), `samples` times while the page scrolls."""
    ws.call('DOM.enable')
    root = ws.call('DOM.getDocument', {'depth': -1, 'pierce': True})['root']
    found = []
    def walk(n):
        a = n.get('attributes', []); d = dict(zip(a[::2], a[1::2]))
        if set(want.split()) <= set(d.get('class', '').split()):
            found.append(n['nodeId'])
        for k in n.get('children', []) + n.get('shadowRoots', []):
            walk(k)
    walk(root)
    if not found:
        return []
    obj = ws.call('DOM.resolveNode', {'nodeId': found[0]})['object']['objectId']
    fn = ('function(s){var a=this.getBoundingClientRect(),b=document.querySelector(s).getBoundingClientRect();'
          'var w=this.parentElement;while(w&&!w.classList.contains("w"))w=w.parentElement;'
          'return [a.left,a.top,b.left,b.top,scrollY,w?getComputedStyle(w).position:"",getComputedStyle(this).transitionDuration]}')
    out = []
    for _ in range(samples):
        r = ws.call('Runtime.callFunctionOn', {'objectId': obj, 'functionDeclaration': fn, 'arguments': [{'value': sel}], 'returnByValue': True})
        out.append(r['result']['value']); time.sleep(gap)
    return out


def scroll_and_measure(want, sel, offset, label):
    """Wheel scrolls (the compositor's smooth scroll, as a mouse wheel) down then up; the frame's offset to its element."""
    ws = C.ws_for(C.URL)
    x, y = C.page('[innerWidth/2, innerHeight/2]')
    rows = []
    for dy in (360, 360, -720):
        ws.call('Input.dispatchMouseEvent', {'type': 'mouseWheel', 'x': x, 'y': y, 'deltaX': 0, 'deltaY': dy})
        rows += shadow_rects(ws, want, sel, 14)
    if not rows:
        return None
    moved = max(r[4] for r in rows) - min(r[4] for r in rows)
    worst = max(max(abs(r[0] - (r[2] + offset)), abs(r[1] - (r[3] + offset))) for r in rows)
    m = {'samples': len(rows), 'scrolledPx': round(moved, 1), 'worstOffsetPx': round(worst, 2), 'wrapper': rows[-1][5], 'transition': rows[-1][6]}
    report['measures'][label] = m
    return m


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
        C.shortcut('A')
        step('mode on', logged(r'ANNOTATE mode on', 0, 10))
        time.sleep(1.2)
        if C.invoke('AnnotationConnectLater') or C.invoke('AnnotationConnectClose'):
            time.sleep(0.6)
        step('the « Connecter un agent » modal is out of the way (fresh profile)', not [w for w in Q.windows() if w[1] == 'BrowserAnnotationVeil'])
        u32.SetForegroundWindow(Q.main_window())

        # ---- 7 (chat closed) + 6 + 1: a click opens the note; typing hides the pointer; « Demander à l'agent »
        m1 = mark()
        tx, ty = page_point('.title')
        u32.SetCursorPos(tx, ty); time.sleep(0.3)
        C.click_page('.title')
        note = C.wait_for(lambda: [w for w in Q.windows() if w[1] == 'BrowserAnnotationNote'], 6)
        step('7 · chat closed: a click on an element opens the note panel, as before', bool(note))
        if note:
            edit = Q.find_child(note[0][0], 'Edit')
            Q.post_text(NOTE, edit); time.sleep(0.4)
            typed = cursor()
            report['measures']['pointerRightAfterTyping'] = typed
            print('pointer right after typing:', 'hidden (Windows « hide pointer while typing »)' if not typed['showing'] else 'shown')
            hx, hy = page_point('.hint')
            glide_to(tx, ty, hx, hy); time.sleep(0.35)
            back = cursor()
            report['measures']['pointerAfterMovingOverPage'] = back
            step('6 · after typing in the note, the pointer is visible again as soon as it moves over the page', back['showing'] and back['handle'] != 0, json.dumps(back))
            capture('6-curseur-visible-apres-saisie', with_cursor=True)
            # 1: Entrée = « Demander à l'agent »: the chat opens for the FIRST time on this site, with the element and the text
            Q.post_key(0x0D, edit)
            step('1 · « Demander à l\'agent » from the note, with its text', logged(r"CHAT « Demander à l'agent » from the note panel \(with its text\)", m1, 8))
            step('1 · the chat opens (first opening on this site: a new conversation)', logged(r'CHAT panel ready', m1, 15))
            step("1 · the note's text goes with the element", logged(r"CHAT the note's text goes with the element", m1, 25))
            step('1 · a turn starts', logged(r'CHAT turn t1 started', m1, 20))
            time.sleep(0.8)
            left = [w[2] for w in Q.windows() if w[1] in ('BrowserAnnotationLayer', 'BrowserAnnotationNote') and w[2] in ("Ce que l'agent verra", 'Nouvelle annotation')]
            step('1 · the note and its « Ce que l\'agent verra » pill are gone once the chat has it', not left, str(left))
            got = C.wait_for(lambda: [l for l in flog.read_text(encoding='utf-8', errors='replace').splitlines() if l.startswith('prompt-text ')], 15)
            report['measures']['promptText'] = got[-1][:300] if got else ''
            step('1 · the agent receives the note\'s text in session/prompt (fake agent\'s log)', bool(got) and NOTE in got[-1], got[-1][:160] if got else 'no prompt')
            bubble = C.wait_for(lambda: C.panel("(function(){var b=document.querySelector('.um .bubble');return b?b.innerText:''})()"), 8) or ''
            chips = C.panel("document.querySelectorAll('.um .chip').length") or 0
            step('1 · the user\'s bubble shows the text, with the element\'s chip', NOTE in bubble and chips >= 1, f'{bubble!r}, {chips} chip(s)')
            perm = logged(r'CHAT permission asked: edit', m1, 20)
            time.sleep(0.6)
            capture('1-note-texte-dans-le-chat')
            if perm:
                C.wait_for(lambda: 'Refuser' in C.panel_text(), 5)
                C.click_panel(text='Refuser')
            step('turn 1 ends', logged(r'CHAT turn t1 ended', m1, 20))

        # ---- 2: no « Default »: Medium at the session start, in French
        eff = logged(r'CHAT effort set to « (\w+) » \(the session started with « (\w+) »\)', 0, 5)
        step('2 · the browser sets Medium at the session start (the agent started on « Default »)', eff is not None and eff.group(1) == 'medium' and eff.group(2) == 'default', eff.group(0)[5:] if eff else '')
        time.sleep(0.5)
        chip_e = C.panel("(document.querySelector('.model .e')||{}).textContent||''")
        metas = C.panel("Array.from(document.querySelectorAll('.turn .meta')).map(function(m){return m.textContent})") or []
        step('2 · the model button says « Moyen »; no « Default » anywhere in the panel', chip_e == 'Moyen' and 'Default' not in C.panel_text(), f'{chip_e!r} {metas}')
        step('2 · the turn\'s header: model · Moyen · duration', bool(metas) and ' · Moyen · ' in metas[-1], str(metas))

        # ---- 7 (chat open): a click is a chip, the caret goes to the composer
        m2 = mark()
        C.click_page('.btn-primary')
        aimed = logged(r'CHAT element aimed: (\S+)', m2, 10)
        caret = logged(r'CHAT chip added: the caret goes to the composer', m2, 5)
        time.sleep(0.5)
        no_note = not [w for w in Q.windows() if w[1] == 'BrowserAnnotationNote']
        foc = C.panel("({chip:document.querySelectorAll('.cin .chip').length,active:document.activeElement&&document.activeElement.id,focus:document.hasFocus()})") or {}
        step('7 · chat open: a click on an element adds it as a chip, no note panel', aimed is not None and no_note and foc.get('chip', 0) >= 1, json.dumps(foc))
        step('7 · …and the caret goes into the composer', caret is not None and foc.get('active') == 'composer-input' and foc.get('focus'), json.dumps(foc))
        capture('7-clic-element-puce-dans-le-chat')

        # ---- a turn that changes the file: the « done » mark and the gauge
        m3 = mark()
        C.type_panel('Aligne-le à droite'); time.sleep(0.2); C.enter_panel()
        step('turn 2 started (with the chip)', logged(r'CHAT turn t2 started', m3, 10))
        if logged(r'CHAT permission asked: edit', m3, 20):
            C.wait_for(lambda: 'Autoriser' in C.panel_text(), 5)
            C.click_panel(selector='.perm .btn.primary')
        step('turn 2 ends', logged(r'CHAT turn t2 ended: end_turn', m3, 25))
        done = logged(r'mark « done » on the element: (\w+)', m3, 20)
        step('the changed element gets its « done » mark', done is not None and done.group(1) == 'found', done.group(0) if done else '')

        # ---- 4: the gauge's bubble, with the REAL pointer
        g = C.wait_for(lambda: C.panel("(function(){var g=document.querySelector('.gauge');return g&&!g.hidden})()"), 8)
        if step('4 · the gauge is shown', bool(g)):
            gx, gy = panel_point('.gauge')
            u32.SetCursorPos(gx, gy); time.sleep(0.7)
            on = C.panel("document.querySelector('.gauge').classList.contains('tip-on')")
            capture('4-jauge-survol')
            m4 = mark()
            ws = widgets(); pr = ws[1] if len(ws) > 1 else ws[0]
            glide_to(gx, gy, gx, pr[1] - 40, 4)    # straight up, out of the panel (the band / the tabs)
            time.sleep(0.5)
            off = C.panel("[document.querySelector('.gauge').classList.contains('tip-on'), getComputedStyle(document.querySelector('.gauge .tip')).opacity]")
            capture('4-jauge-apres-sortie')
            step('4 · the bubble shows while the real pointer is on the gauge', on)
            step('4 · and goes when the pointer leaves the panel upwards (band / tabs)', off and not off[0] and off[1] == '0', f'{off}, host: {bool(logged(r"CHAT pointer left the panel", m4, 1))}')
            u32.SetCursorPos(gx, gy); time.sleep(0.7)
            glide_to(gx, gy, pr[0] - 200, gy, 4)   # straight to the page, on the left
            time.sleep(0.5)
            off2 = C.panel("document.querySelector('.gauge').classList.contains('tip-on')")
            step('4 · and when it leaves to the page', off2 is False)

        # ---- 5: the « done » mark glued to its element while the page scrolls
        C.page("document.body.style.paddingBottom='2400px'"); time.sleep(0.7)
        mm = scroll_and_measure('mk', '.btn-primary', -4, 'doneMarkWhileScrolling')
        step('5 · the « done » mark stays on its element at every sample while the page scrolls (document-anchored)', mm is not None and mm['scrolledPx'] > 100 and mm['worstOffsetPx'] <= 1.0 and mm['wrapper'] == 'absolute', json.dumps(mm))
        C.page("scrollTo(0, 120)"); time.sleep(0.6)
        capture('5-cadre-fait-colle-apres-defilement')

        # ---- 5: the selection frame (note open, chat closed)
        C.shortcut('D'); logged(r'CHAT panel closed', 0, 5); time.sleep(0.8)
        C.page("scrollTo(0, 0)"); time.sleep(0.3)
        m5 = mark()
        C.click_page('.hint')
        note2 = C.wait_for(lambda: [w for w in Q.windows() if w[1] == 'BrowserAnnotationNote'], 6)
        time.sleep(1.2)   # the capture's hand-off is over: the page draws its frame again
        if step('the note opens on another element (chat closed)', bool(note2)):
            fm = scroll_and_measure('x h', '.hint', 0, 'selectionFrameWhileScrolling')
            step('5 · the selection frame stays on its element while the page scrolls (no glide behind it)', fm is not None and fm['scrolledPx'] > 100 and fm['worstOffsetPx'] <= 1.0, json.dumps(fm))
            C.page("scrollTo(0, 90)"); time.sleep(0.5)
            capture('5-cadre-selection-colle-apres-defilement')
            Q.post_key(0x1B, Q.find_child(note2[0][0], 'Edit'))
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
