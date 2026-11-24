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
  6  the pointer hidden by typing in the note comes back when the mouse moves over the page (a site that is not local:
     the only place the note panel still shows)
  7  V1.3: a click on an element ALWAYS goes to the chat (opened if closed): a chip, the caret in the composer; Ctrl+Entrée
     keeps it as an annotation of the store (no agent); the send button's bubble says both; not local: the note panel
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


def pg(js, prefix=None):
    return C.evaluate(prefix or C.URL, js)


def click_on(sel, prefix=None):
    """A trusted click (CDP) in the middle of an element of the page at `prefix`."""
    r = pg(f"(function(){{var e=document.querySelector({json.dumps(sel)});if(!e)return null;var r=e.getBoundingClientRect();return [r.left+r.width/2,r.top+r.height/2];}})()", prefix)
    ws = C.ws_for(prefix or C.URL)
    for kind in ('mouseMoved', 'mousePressed', 'mouseReleased'):
        ws.call('Input.dispatchMouseEvent', {'type': kind, 'x': r[0], 'y': r[1], 'button': 'none' if kind == 'mouseMoved' else 'left', 'clickCount': 0 if kind == 'mouseMoved' else 1})


def page_point(sel, dy=0.5, prefix=None):
    r = widgets()[0]; dpr = pg('devicePixelRatio', prefix)
    b = pg("(function(){var r=document.querySelector(%s).getBoundingClientRect();return [r.left+r.width/2,r.top+r.height*%s]})()" % (json.dumps(sel), dy), prefix)
    return int(r[0] + b[0] * dpr), int(r[1] + b[1] * dpr)


def panel_point(sel):
    ws = widgets(); r = ws[1] if len(ws) > 1 else ws[0]; dpr = C.panel('devicePixelRatio')
    b = C.panel("(function(){var r=document.querySelector(%s).getBoundingClientRect();return [r.left+r.width/2,r.top+r.height/2]})()" % json.dumps(sel))
    return int(r[0] + b[0] * dpr), int(r[1] + b[1] * dpr)


def glide_to(x0, y0, x1, y1, steps=8):
    for k in range(steps):
        u32.SetCursorPos(x0 + (x1 - x0) * (k + 1) // steps, y0 + (y1 - y0) * (k + 1) // steps); time.sleep(0.03)


def hover(x, y, shown, tries=3):
    """The real pointer comes onto (x, y) like a hand does (a short approach, not a jump), then rests; tried again if the
    bubble did not show (the user's own mouse may have moved meanwhile on a shared desktop)."""
    for _ in range(tries):
        glide_to(x - 40, y - 24, x, y, 6); time.sleep(0.7)
        if shown():
            return True
        u32.SetCursorPos(x - 120, y - 120); time.sleep(0.3)
    return False


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


def scroll_and_measure(want, sel, offset, label, prefix=None):
    """Wheel scrolls (the compositor's smooth scroll, as a mouse wheel) down then up; the frame's offset to its element."""
    ws = C.ws_for(prefix or C.URL)
    x, y = pg('[innerWidth/2, innerHeight/2]', prefix)
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


def chips():
    return C.panel("document.querySelectorAll('.cin .chip').length") or 0


def run_main():
    before = C.fingerprints(); report['fingerprintsBefore'] = before
    OUT.mkdir(parents=True, exist_ok=True)
    flog = C.WORK / 'fake-agent.log'
    try:
        flog.unlink()
    except OSError:
        pass
    fake = C.TOOLS / 'fake-acp-agent.exe'
    other = f'http://exemple.test:{C.PORT_PAGE}/'   # the same sandbox site under a name that is NOT local: no chat there
    p = C.launch(extra_env={'BROWSER_CHAT_PANEL_IDLE_MS': '60000', 'BROWSER_CHAT_AGENT_IDLE_MS': '600000',
                            'WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS': f'--remote-debugging-port={Q.PORT} --remote-allow-origins=http://127.0.0.1:{Q.PORT} --host-resolver-rules="MAP exemple.test 127.0.0.1"',
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

        # ---- 7 (V1.3): chat CLOSED, a click on an element opens the chat: the element is a chip, the caret is there
        m1 = mark()
        C.click_page('.title')
        # final QA: the panel is created in advance (O1): its opening by a click may be logged « panel in place » / « panel opened »
        opened = logged(r'CHAT (a click on an element opens the chat|panel in place|panel opened)', m1, 8)
        # the panel's page is created when the mode starts (O1, « Chat à droite »): it says « panel ready » ONCE, usually before
        # this click. Waited for after the click it was never found (15 s lost, step 7 failed on the 20:00 release's pass,
        # ae51111): read from the start of the log — ready by the time the click has opened the chat, whenever it was said
        ready = logged(r'CHAT panel ready', 0, 15)
        time.sleep(0.4)
        step('7 · chat closed: a click on an element OPENS the chat (no note panel)', opened is not None and ready is not None and not [w for w in Q.windows() if w[1] == 'BrowserAnnotationNote'])
        has_chip = C.wait_for(chips, 10)
        time.sleep(0.6)
        foc = C.panel("({chip:document.querySelectorAll('.cin .chip').length,label:(document.querySelector('.cin .chip')||{}).textContent||'',active:document.activeElement&&document.activeElement.id,focus:document.hasFocus()})") or {}
        step('7 · …the element is a chip in the composer and the caret is in the composer', bool(has_chip) and foc.get('active') == 'composer-input' and foc.get('focus'), json.dumps(foc, ensure_ascii=False))
        capture('7-clic-ouvre-le-chat')

        # ---- 1: the words typed next to the chip reach the agent (session/prompt) and the bubble, on the FIRST opening
        C.type_panel(' ' + NOTE); time.sleep(0.2); C.enter_panel()
        step('1 · a turn starts with the element and the text', logged(r'CHAT turn t1 started', m1, 20))
        got = C.wait_for(lambda: [l for l in flog.read_text(encoding='utf-8', errors='replace').splitlines() if l.startswith('prompt-text ')], 15)
        report['measures']['promptText'] = got[-1][:300] if got else ''
        step('1 · the agent receives the text in session/prompt (fake agent\'s log)', bool(got) and NOTE in got[-1], got[-1][:160] if got else 'no prompt')
        bubble = C.wait_for(lambda: C.panel("(function(){var b=document.querySelector('.um .bubble');return b?b.innerText:''})()"), 8) or ''
        step('1 · the user\'s bubble shows the text with the element\'s chip', NOTE in bubble and (C.panel("document.querySelectorAll('.um .chip').length") or 0) >= 1, repr(bubble))
        if logged(r'CHAT permission asked: edit', m1, 20):
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

        # ---- V1.3: Ctrl+Entrée keeps an annotation (no agent), read by the MCP agents
        m2 = mark()
        C.click_page('.hint')
        C.wait_for(chips, 10); time.sleep(0.4)
        sx, sy = panel_point('.send')
        hover(sx, sy, lambda: C.panel("getComputedStyle(document.getElementById('send-tip')).opacity") == '1')
        tip = C.panel("[getComputedStyle(document.getElementById('send-tip')).opacity, document.getElementById('send-tip').innerText]") or ['', '']
        capture('7-infobulle-envoyer')
        step('the send button\'s bubble: Entrée = ask the agent, Ctrl+Entrée = keep as an annotation', tip[0] == '1' and "Demander à l'agent" in tip[1] and 'Garder en annotation' in tip[1], repr(tip))
        glide_to(sx, sy, sx - 300, sy - 300, 4); time.sleep(0.3)
        prompts = len([l for l in flog.read_text(encoding='utf-8', errors='replace').splitlines() if l.startswith('prompt-text ')])
        C.panel("document.getElementById('composer-input').focus()"); C.type_panel(' Texte trop discret'); time.sleep(0.2)
        C.key_panel('Enter', 'Enter', 13, modifiers=2)
        wrote = logged(r'CHAT annotation kept \(Ctrl\+Entrée\) (ann_\w+): write ok', m2, 10)
        time.sleep(0.6)
        line = C.panel("(document.querySelector('.kept')||{}).innerText||''") or ''
        after = len([l for l in flog.read_text(encoding='utf-8', errors='replace').splitlines() if l.startswith('prompt-text ')])
        step('V1.3 · Ctrl+Entrée: the element and its words become an annotation of the store (open, for the MCP agents)', wrote is not None, wrote.group(0)[5:] if wrote else '')
        step('V1.3 · …nothing is sent to the agent; the thread says « Gardé en annotation »', after == prompts and 'CHAT turn t2' not in C.log_text()[m2:] and 'Gardé en annotation' in line and 'Texte trop discret' in line, line.replace('\n', ' | '))
        capture('7-garde-en-annotation')

        # ---- V1.4: the aim button switches the page's selection off — the chat stays open with its thread — and on again
        m5 = mark()
        pg("window.__clicks=0;document.addEventListener('click',function(){window.__clicks++},false);1")
        h0 = pg('innerHeight'); cur0 = pg("getComputedStyle(document.querySelector('.field-label')).cursor") or ''
        C.click_panel(selector='.tb.pick')
        off = logged(r'ANNOTATE selection off', m5, 5)
        time.sleep(0.7)
        sw = C.panel("[document.querySelector('.tb.pick').getAttribute('aria-pressed'), document.querySelectorAll('.um, .kept').length]") or ['', 0]
        h1 = pg('innerHeight'); cur1 = pg("getComputedStyle(document.querySelector('.field-label')).cursor") or ''
        band_off = not [w for w in Q.windows() if w[1] == 'BrowserAnnotationLayer' and 'Mode Annotation' in (w[2] or '')]
        report['measures']['selectionSwitch'] = {'heightOn': h0, 'heightOff': h1, 'cursorOn': cur0[:40], 'cursorOff': cur1[:40]}
        step('V1.4 · the aim button switches the selection off: not pressed, the band goes up (page +32 px), no « viseur » cursor',
             off is not None and sw[0] == 'false' and h1 - h0 >= 30 and 'url(' in cur0 and 'url(' not in cur1, json.dumps(report['measures']['selectionSwitch']))
        C.click_page('.field-label'); time.sleep(0.7)
        clicks = pg('window.__clicks'); aimed = 'CHAT element aimed' in C.log_text()[m5:]
        step('V1.4 · …the page gets its own clicks again (no chip), the chat stays open with its thread', clicks >= 1 and not aimed and C.panel_exists()   # a <label> also clicks its field: the page counts 2, as in any browser
            and True and sw[1] >= 2, f'clicks {clicks}, aimed {aimed}, {sw[1]} thread item(s)')
        capture('10-selection-coupee')
        m6 = mark(); C.shortcut('A')
        on = logged(r'ANNOTATE selection on', m6, 5); time.sleep(0.8)
        pressed = C.panel("document.querySelector('.tb.pick').getAttribute('aria-pressed')")
        cur2 = pg("getComputedStyle(document.querySelector('.field-label')).cursor") or ''
        step('V1.4 · Ctrl+Maj+A with the chat open switches it on again: pressed, band back, « viseur » back', on is not None and pressed == 'true' and pg('innerHeight') == h0 and 'url(' in cur2, f'{pressed} {pg("innerHeight")}')
        step('V1.4 · the chat was never closed by the switch', 'CHAT panel closed' not in C.log_text()[m5:])
        capture('10-selection-active')
        # ---- V1.4: the modes in French (Paper 14RK-0), whatever the adapter calls them (the fake agent says « Manual », « Accept edits »…)
        C.click_panel(selector='.tb.mode'); time.sleep(0.4)
        rows = C.panel("Array.from(document.querySelectorAll('.menu .mi')).map(function(b){return b.innerText.replace(/\\n+/g,' | ')})") or []
        report['measures']['modeMenu'] = rows
        # final QA: R4 replaced V1.4 — Paper 14RK-0 as the user updated it: 4 rows, this order, these words
        step('R4 · the mode menu: Accept edits, Auto, Demander, Planifier with their descriptions (Paper 14RK-0)',
             [r.strip() for r in rows] == ['Accept edits | Automatically accept all file edits', 'Auto | Claude handles permission decisions',
                                           'Demander | Propose chaque modification.', 'Planifier | Propose un plan, ne touche à aucun fichier.'], ' / '.join(rows))
        capture('10-modes-en-francais')
        C.panel("Array.from(document.querySelectorAll('.menu .mi')).find(function(b){return b.innerText.indexOf('Auto')===0}).click();1"); time.sleep(0.5)
        # final QA: R21 — the mode button is an icon alone, its name in its accessible label
        chipm = C.panel("(document.querySelector('.tb.mode')||{}).getAttribute('aria-label')||''")
        step('R21 · the mode button is « Auto » (accessible name, icon alone)', 'Auto' in (chipm or '') and 'Accept' not in (chipm or ''), repr(chipm))
        C.panel("Array.from(document.querySelectorAll('.tb.mode')).forEach(function(b){b.click()});1"); time.sleep(0.3)
        C.panel("(function(){var d=Array.from(document.querySelectorAll('.menu .mi')).find(function(b){return b.innerText.indexOf('Demander')===0});if(d)d.click();return 1})()"); time.sleep(0.4)

        # ---- a turn that changes the file: the « done » mark and the gauge
        m3 = mark()
        C.click_page('.btn-primary'); C.wait_for(chips, 10); time.sleep(0.3)
        C.type_panel(' Aligne-le à droite'); time.sleep(0.2); C.enter_panel()
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
            hover(gx, gy, lambda: C.panel("document.querySelector('.gauge').classList.contains('tip-on')"))
            on = C.panel("document.querySelector('.gauge').classList.contains('tip-on')")
            capture('4-jauge-survol')
            ws = widgets(); pr = ws[1] if len(ws) > 1 else ws[0]
            glide_to(gx, gy, gx, pr[1] - 40, 4); time.sleep(0.5)
            off = C.panel("[document.querySelector('.gauge').classList.contains('tip-on'), getComputedStyle(document.querySelector('.gauge .tip')).opacity]")
            capture('4-jauge-apres-sortie')
            step('4 · the bubble shows while the real pointer is on the gauge', on)
            step('4 · and goes when the pointer leaves the panel upwards (band / tabs)', off and not off[0] and off[1] == '0', str(off))
            u32.SetCursorPos(gx, gy); time.sleep(0.7)
            glide_to(gx, gy, pr[0] - 200, gy, 4); time.sleep(0.5)
            step('4 · and when it leaves to the page', C.panel("document.querySelector('.gauge').classList.contains('tip-on')") is False)

        # ---- 5: the « done » mark glued to its element while the page scrolls
        C.page("document.body.style.paddingBottom='2400px'"); time.sleep(0.7)
        mm = scroll_and_measure('mk', '.btn-primary', -4, 'doneMarkWhileScrolling')
        step('5 · the « done » mark stays on its element at every sample while the page scrolls (document-anchored)', mm is not None and mm['scrolledPx'] > 100 and mm['worstOffsetPx'] <= 1.0 and mm['wrapper'] == 'absolute', json.dumps(mm))
        C.page("scrollTo(0, 120)"); time.sleep(0.6)
        capture('5-cadre-fait-colle-apres-defilement')

        # ---- a site that is NOT local: no chat there, the small note panel stays (V1.3), and with it points 6 and 5
        C.shortcut('D'); logged(r'CHAT panel closed', 0, 5); time.sleep(0.6)
        C.ws_for(C.URL).call('Page.navigate', {'url': other})
        C.wait_for(lambda: C.ws_for(other) is not None and pg('document.readyState', other) == 'complete', 15); time.sleep(0.8)
        # #125 (the owner, 24 Nov 2026; src/annotation-sites.h): « Annoter » is off on a site that is not local — the mode does
        # not start there and the small note panel never opens (before: V1.3 kept the note panel on such a site). Points 6 (the
        # pointer after typing in the note) and 5 (the note's selection frame while scrolling) were checked through that note:
        # they move to a local FILE page, where the note panel still is — lot 3 (tests/annotation-general/coverage.json).
        mA = mark(); C.shortcut('A'); time.sleep(2.0)
        seg = C.log_text()[mA:]
        step('#125 · a site that is not local: « Annoter » does not start the mode there (no note panel, no chat)',
             'ANNOTATE mode on' not in seg and 'CHAT panel opened' not in seg and not [w for w in Q.windows() if w[1] == 'BrowserAnnotationNote'], seg[-300:])
        capture('7-site-non-local-annoter-eteint')
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
