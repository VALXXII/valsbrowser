#!/usr/bin/env python3
"""Chat intégré — Mode Commentaire v2 (host side), checked in a REAL window on the FAKE agent.

    python tests/chat/chat_qa_comment_v2.py      # -> build/chat-qa/comment-v2/host-report.json + PNG captures

Same guards as tests/chat/chat_qa.py (its own copy of the browser, its own profile, agent configurations redirected under
build/, BROWSER_ANNOTATIONS_TEST=1, the real Claude / Codex files fingerprinted before and after, nothing left running).
Run it only when no other real-window suite runs.

  R9  « change the gap to 8 px » in the bubble, Demander mode: the permission is asked; the bubble is closed and opened
      again (as the user did): the card must still be there, « Autoriser » answers it, the turn ends. Then the same
      request in the panel: first text and end of turn measured in both layouts.
  R2  a click on an element then Échap without sending: no frame + icon on it.
  R6  a click on an element that has a conversation opens the bubble on it — also when its conversation file is gone.
  R4  the modes offered: acceptEdits=edits, auto=auto, default=ask, plan=plan (never bypassPermissions).
  R1  the bubble's WebView is hosted as a transparent visual (log), the bubble window has no DWM rounding.
  R10 the band (capture, compared by F with Paper 1AYH-0).
  Input forwarding: a real WM_LBUTTONDOWN / UP posted to the bubble window reaches the page (« Autoriser »).
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
import chat_qa_comment as CC

OUT = C.QA / 'comment-v2'
report = {'steps': [], 'measures': {}, 'captures': []}
u32 = ctypes.windll.user32
GAP_EDIT = 'styles/checkout.css|margin-top: 4px;|margin-top: 4px; margin-left: auto;'


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
        path = C.grab(f'comment-v2/{name}')
        report['captures'].append(path.name)
    except Exception as e:
        print('capture failed', name, e)


def capture_bubble(name):
    """The bubble alone, from the screen (its corners over the page: R1)."""
    try:
        from PIL import ImageGrab
        bw = CC.bubble_window()
        if not bw:
            return
        r = bw[3]
        ImageGrab.grab(bbox=(r[0] - 16, r[1] - 16, r[2] + 16, r[3] + 16), all_screens=True).save(OUT / f'{name}.png')
        report['captures'].append(f'{name}.png')
    except Exception as e:
        print('bubble capture failed', name, e)


def overlay():
    """The mode's drawing in the page (closed shadow root, read through DevTools): classes and texts."""
    ws = C.ws_for(C.URL); ws.call('DOM.enable')
    root = ws.call('DOM.getDocument', {'depth': -1, 'pierce': True})['root']
    out = []
    def walk(n, parent_cls=''):
        a = n.get('attributes', []); d = dict(zip(a[::2], a[1::2]))
        cls = d.get('class', ''); style = d.get('style', '')
        text = ''.join(c.get('nodeValue', '') for c in n.get('children', []) if c.get('nodeType') == 3)
        if cls:
            out.append({'cls': cls, 'style': style, 'text': text})
        for k in n.get('children', []) + n.get('shadowRoots', []):
            walk(k, cls)
    walk(root)
    return out


def bubble_frame():
    """D1: the open bubble's element frame and its two labels; also whether the hover label / inspector show."""
    o = overlay()
    labels = [x['text'] for x in o if x['cls'] == 'bl']
    frames = [x for x in o if x['cls'] == 'bf']
    hover = [x for x in o if x['cls'].split()[:1] == ['x'] and 'l' in x['cls'].split() and 'opacity: 1' in x['style']]
    inspector = [x for x in o if x['cls'].split()[:1] == ['i'] and 'opacity: 1' in x['style']]
    spin = [x for x in o if 'spin' in x['cls'].split()]
    return {'labels': labels, 'frames': len(frames), 'hoverLabel': len(hover), 'inspector': len(inspector), 'spinner': len(spin), 'marks': [x['cls'] for x in o if 'ct' in x['cls'].split()]}


def pointer_away():
    """The page's pointer far from every element of the test (top left corner of the page), as a real move would."""
    C.ws_for(C.URL).call('Input.dispatchMouseEvent', {'type': 'mouseMoved', 'x': 3, 'y': 3})
    C.page("document.dispatchEvent(new PointerEvent('pointerout',{bubbles:true,relatedTarget:null}))")   # leaves the page
    time.sleep(0.4)


def launcher_shown():
    return 'AnnotationLauncher' in (C.uia('-List') or [])


FLOATS_JS = ("(function(){var o=[];var l=document.querySelectorAll('.menu,.b-tip,.sub-menu');for(var i=0;i<l.length;i++){var e=l[i];"
             "if(e.hidden)continue;var cs=getComputedStyle(e);if(cs.display==='none'||cs.visibility==='hidden'||+cs.opacity<0.5)continue;var r=e.getBoundingClientRect();"
             "if(r.width<2||r.height<2)continue;o.push({cls:e.className,label:e.getAttribute('aria-label')||'',x:r.left,y:r.top,r:r.right,b:r.bottom})}"
             "var st=getComputedStyle(document.documentElement);function v(n){return parseFloat(st.getPropertyValue(n))||0}"
             "return {floats:o,w:innerWidth,h:innerHeight,dpr:devicePixelRatio,inset:{top:v('--it'),right:v('--ir'),bottom:v('--ib'),left:v('--il')}}})()")


def screen_grab(rect):
    from PIL import ImageGrab
    return ImageGrab.grab(bbox=rect, all_screens=True).convert('RGB')


def r3_measure(name, open_fn, rest_rect, rest_img, close_fn=None):
    """R3: a menu / tooltip of the bubble opened by `open_fn`: not clipped, out of the surface when it needs to, the
    transparent window around it shows the page, and once closed the window is the surface again (clicks to the page)."""
    open_fn(); time.sleep(0.7)
    f = C.panel(FLOATS_JS) or {}
    bw = CC.bubble_window(); win = bw[3] if bw else None
    floats = f.get('floats', []); ins = f.get('inset', {}); dpr = f.get('dpr', 1) or 1; W_, H_ = f.get('w', 0), f.get('h', 0)
    surf = (ins.get('left', 0), ins.get('top', 0), W_ - ins.get('right', 0), H_ - ins.get('bottom', 0))
    clipped = [x for x in floats if x['x'] < -1 or x['y'] < -1 or x['r'] > W_ + 1 or x['b'] > H_ + 1]
    outside = [x for x in floats if x['x'] < surf[0] - 1 or x['y'] < surf[1] - 1 or x['r'] > surf[2] + 1 or x['b'] > surf[3] + 1]
    # a point of the transparent margin covered by no float: the page must show there as it did before the opening
    see = None
    if win and outside:
        for (cx, cy) in [(2, 2), (W_ - 3, 2), (2, H_ - 3), (W_ - 3, H_ - 3), (surf[0] / 2, H_ / 2), (W_ - (W_ - surf[2]) / 2, H_ / 2), (W_ / 2, surf[1] / 2), (W_ / 2, H_ - (H_ - surf[3]) / 2)]:
            inside_surf = surf[0] <= cx <= surf[2] and surf[1] <= cy <= surf[3]
            covered = any(x['x'] - 2 <= cx <= x['r'] + 2 and x['y'] - 2 <= cy <= x['b'] + 2 for x in floats)
            if inside_surf or covered:
                continue
            sx, sy = int(win[0] + cx * dpr), int(win[1] + cy * dpr)
            if rest_rect[0] <= sx < rest_rect[2] and rest_rect[1] <= sy < rest_rect[3]:
                continue   # under the bubble at rest: no « before » to compare with
            now = screen_grab((sx, sy, sx + 1, sy + 1)).getpixel((0, 0))
            before = rest_img.getpixel((sx - rest_img_box[0], sy - rest_img_box[1])) if rest_img_box[0] <= sx < rest_img_box[2] and rest_img_box[1] <= sy < rest_img_box[3] else None
            see = {'at': (sx, sy), 'now': now, 'before': before}
            break
    capture(f'r3-{name}')
    try:
        from PIL import ImageGrab
        if win:
            ImageGrab.grab(bbox=(win[0] - 8, win[1] - 8, win[2] + 8, win[3] + 8), all_screens=True).save(OUT / f'r3-{name}-fenetre.png'); report['captures'].append(f'r3-{name}-fenetre.png')
    except Exception as e:
        print('capture failed', e)
    if close_fn:
        close_fn()
    else:
        focus_composer(); C.key_panel('Escape', 'Escape', 27)
    time.sleep(0.6)
    after = CC.bubble_window(); after_r = after[3] if after else None
    probe = None
    if win and after_r and (win[0] < after_r[0] or win[1] < after_r[1] or win[2] > after_r[2] or win[3] > after_r[3]):
        # a point that was in the grown window and is outside the bubble now: a click there reaches the page
        px_, py_ = (win[0] + after_r[0]) // 2, (win[1] + after_r[1]) // 2
        if win[1] >= after_r[1]: py_ = (after_r[1] + after_r[3]) // 2
        if win[0] >= after_r[0]: px_ = (after_r[0] + after_r[2]) // 2
        if win[2] > after_r[2]: px_ = (after_r[2] + win[2]) // 2
        if win[3] > after_r[3] and win[1] >= after_r[1]: py_ = (after_r[3] + win[3]) // 2
        h = u32.WindowFromPoint(W.POINT(px_, py_)); cls = ctypes.create_unicode_buffer(64); u32.GetClassNameW(h, cls, 64)
        probe = {'at': (px_, py_), 'window': cls.value}
    m = {'floats': [(x['label'] or x['cls'])[:30] for x in floats], 'clipped': len(clipped), 'outsideSurface': len(outside), 'window': win, 'rest': rest_rect,
         'afterClose': after_r, 'inset': ins, 'transparent': see, 'clickAfterClose': probe}
    report['measures'].setdefault('r3', {})[name] = m
    ok_open = (bool(floats) or name in ('menu-plus',)) and not clipped   # R22: « + » opens the image picker directly (no menu)
    ok_grow = True   # R19 (v3): the window keeps its size; only its region widens (tests/chat/chat_qa_comment_v3.py measures it)
    ok_see = True   # R19: measured to the pixel in chat_qa_comment_v3.py (the margin is part of the window now)
    ok_rest = after_r is not None and after_r == rest_rect   # the window never moved
    ok_click = probe is None or probe['window'] != 'BrowserChatBubble'
    step(f'R3 · {name}: shown whole (not clipped), out of the bubble with the window grown, the page seen around it, back to the surface after Échap (clicks to the page)',
         ok_open and ok_grow and ok_see and ok_rest and ok_click, m)


rest_img_box = (0, 0, 0, 0)


def focus_composer():
    C.panel("(function(){var t=document.querySelector('[contenteditable=true]');if(t)t.focus();return 1})()")


def send_in_view(text):
    focus_composer(); C.type_panel(text); time.sleep(0.2); C.enter_panel()


class _MI(ctypes.Structure):
    _fields_ = [('dx', W.LONG), ('dy', W.LONG), ('mouseData', W.DWORD), ('dwFlags', W.DWORD), ('time', W.DWORD), ('dwExtraInfo', ctypes.c_size_t)]


class _IN(ctypes.Structure):
    class _U(ctypes.Union):
        _fields_ = [('mi', _MI), ('pad', ctypes.c_byte * 32)]
    _anonymous_ = ('u',)
    _fields_ = [('type', W.DWORD), ('u', _U)]


def bubble_hwnd():
    """[(hwnd, class, title, rect)] of OUR bubble window, found by its class (hidden or not) — no enumeration per sample."""
    h = 0
    while True:
        h = u32.FindWindowExW(None, h, 'BrowserChatBubble', None)
        if not h:
            return []
        pid = W.DWORD(); u32.GetWindowThreadProcessId(h, ctypes.byref(pid))
        if pid.value == Q.our_pid():
            r = W.RECT(); u32.GetWindowRect(h, ctypes.byref(r)); return [(h, 'BrowserChatBubble', '', (r.left, r.top, r.right, r.bottom))]


def front():
    """Our test browser in front (the user's own browser, another process of the same class, may have taken the foreground)."""
    for _ in range(10):
        h = Q.main_window(); u32.SetForegroundWindow(h); time.sleep(0.08)
        fg = u32.GetForegroundWindow(); pid = W.DWORD(); u32.GetWindowThreadProcessId(fg, ctypes.byref(pid))
        if pid.value == Q.our_pid():
            return True
        u32.keybd_event(0x12, 0, 0, 0); u32.keybd_event(0x12, 0, 2, 0)   # an Alt tap lets SetForegroundWindow through
    return False


def real_click_in_bubble(label):
    """A REAL mouse click (SendInput) on the bubble's button named `label`: the user's path into the chat page."""
    bw = CC.bubble_window()
    if not bw:
        return False
    js = ("(function(){var b=[].slice.call(document.querySelectorAll('button')).filter(function(x){return (x.textContent||'').trim()===%s&&x.getBoundingClientRect().width>0})[0];"
          "if(!b)return null;var r=b.getBoundingClientRect();return [r.left+r.width/2,r.top+r.height/2,devicePixelRatio]})()" % json.dumps(label))
    at = None
    for _ in range(20):   # the card's text is announced before its button is laid out
        at = C.panel(js)
        if at:
            break
        time.sleep(0.25)
    if not at:
        return False
    x, y = bw[3][0] + int(at[0] * at[2]), bw[3][1] + int(at[1] * at[2])
    u32.SetCursorPos(x, y); time.sleep(0.15)
    for flag in (0x0002, 0x0004):
        i = _IN(); i.type = 0; i.mi = _MI(0, 0, 0, flag, 0, 0)
        u32.SendInput(1, ctypes.byref(i), ctypes.sizeof(_IN)); time.sleep(0.06)
    u32.SetCursorPos(bw[3][0] - 200 if bw[3][0] > 300 else bw[3][2] + 200, bw[3][3] + 300)
    return True


def _old_posted_click(label):
    """(kept for the visual hosting, BROWSER_CHAT_VISUAL=1) a mouse message posted to the bubble's window."""
    bw = CC.bubble_window()
    if not bw:
        return False
    js = ("(function(){var b=[].slice.call(document.querySelectorAll('button')).filter(function(x){return (x.textContent||'').trim()===%s})[0];"
          "if(!b)return null;var r=b.getBoundingClientRect();return [r.left+r.width/2,r.top+r.height/2,devicePixelRatio]})()" % json.dumps(label))
    at = C.panel(js)
    if not at:
        return False
    x, y = int(at[0] * at[2]), int(at[1] * at[2])
    lp = (y << 16) | (x & 0xFFFF)
    h = bw[0]
    u32.PostMessageW(h, 0x0200, 0, lp); time.sleep(0.05)        # WM_MOUSEMOVE
    u32.PostMessageW(h, 0x0201, 0x0001, lp); time.sleep(0.05)   # WM_LBUTTONDOWN (MK_LBUTTON)
    u32.PostMessageW(h, 0x0202, 0, lp)                          # WM_LBUTTONUP
    return True


def ms_after(pattern, after):
    m = logged(pattern, after, 30)
    return float(m.group(1)) if m else None


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
                            'BROWSER_CHAT_FAKE_AGENT': f'"{fake}" --resume --claude-like --log "{flog}" --edit "{GAP_EDIT}"'})
    try:
        time.sleep(1.0)
        C.shortcut('A'); step('mode on', logged(r'ANNOTATE mode on', 0, 10)); time.sleep(1.2)
        if C.invoke('AnnotationConnectLater') or C.invoke('AnnotationConnectClose'):
            time.sleep(0.6)
        u32.SetForegroundWindow(Q.main_window())
        m0 = mark()
        step('Commentaire chosen', C.invoke('AnnotationLayoutComment') and logged(r'CHAT layout: Commentaire', m0, 5) is not None)
        time.sleep(0.6); capture('r10-bandeau')

        # ---- R2: a click, then Échap without sending: nothing stays on the element
        m1 = mark()
        C.click_page('.btn-primary')
        step('R2 · a click opens the bubble', logged(r'CHAT bubble opened on (\S+) for conversation (c_\w+)', m1, 10) is not None)
        logged(r'CHAT panel ready', m1, 15); time.sleep(1.0)
        report['measures']['hosting'] = 'visual' if 'visual hosting (transparent)' in C.log_text() else 'windowed'
        step('R1 · windowed hosting (screen readers read the chat), no DWM corner, no region', report['measures']['hosting'] == 'windowed')
        bwc = CC.bubble_window()
        if bwc:
            rgb = W.RECT(); u32.GetWindowRgnBox(bwc[0], ctypes.byref(rgb)); r = (bwc[3][0] + rgb.left, bwc[3][1] + rgb.top, bwc[3][0] + rgb.right, bwc[3][1] + rgb.bottom)   # the surface (R19: the region)
            img = screen_grab((r[0] - 6, r[1], r[0] + 40, r[1] + 40)); corner = img.getpixel((6, 0)); beside = img.getpixel((0, 20)); surface = img.getpixel((6 + 14, 6))   # the 12 px padding, inside the 16 px curve
            report['measures']['corner'] = {'corner': corner, 'pageBeside': beside, 'surface': surface}
            step('R1 · the 16 px corner shows the page (no black corner, no border line), the surface is #171717',
                 sum(abs(a - b) for a, b in zip(corner, beside)) <= 24 and sum(abs(c - 0x17) for c in surface) <= 12, report['measures']['corner'])
        bw = CC.bubble_window()
        if bw:
            pref = ctypes.c_int(0); ctypes.windll.dwmapi.DwmGetWindowAttribute(bw[0], 33, ctypes.byref(pref), 4)
            ex = u32.GetWindowLongW(bw[0], -20)
            report['measures']['bubbleWindow'] = {'rect': bw[3], 'cornerPreference': pref.value, 'exStyle': hex(ex & 0xFFFFFFFF)}
            step('R1 · …window: DWMWCP_DONOTROUND, WS_EX_NOREDIRECTIONBITMAP', pref.value == 1 and (ex & 0x00200000) != 0, report['measures']['bubbleWindow'])
            rgw = W.RECT(); u32.GetWindowRgnBox(bw[0], ctypes.byref(rgw))
            step('R1 · …surface 320 CSS px wide (R20; R19: the window keeps a transparent margin, the region is the surface)', abs((rgw.right - rgw.left) - round(320 * C.evaluate(C.URL, 'devicePixelRatio'))) <= 1, (rgw.left, rgw.top, rgw.right, rgw.bottom))
        pointer_away()
        bf = bubble_frame(); report['measures']['d1'] = bf
        size = C.evaluate(C.URL, "(function(){var r=document.querySelector('.btn-primary').getBoundingClientRect();return Math.round(r.width)+'x'+Math.round(r.height)})()")
        step('D1 · bubble open: the element\'s frame (4 px out) and two labels « selector » | « LxH », no hover label (R16: its inspector stays)',
             bf['frames'] == 1 and len(bf['labels']) == 2 and bf['labels'][1] == size and bf['hoverLabel'] == 0, bf)   # R16: the inspector stays
        step('D3 · no conversation whose element is gone: no launcher', not launcher_shown())
        capture_bubble('r1-bulle-coins'); capture('r2-bulle-ouverte')
        m2 = mark()
        focus_composer(); C.key_panel('Escape', 'Escape', 27)
        step('R2 · Échap closes it', logged(r'CHAT bubble closed', m2, 5) is not None)
        time.sleep(0.8)
        marks = CC.conv_marks()
        step('R2 · nothing sent: no frame + icon on the element', not marks, str(marks))
        capture('r2-rien-apres-fermeture')

        # ---- R9: the gap request, its permission, the bubble closed and opened again
        m3 = mark()
        C.click_page('.btn-primary'); opened = logged(r'CHAT bubble opened on (\S+) for conversation (c_\w+)', m3, 10)
        conv = opened.group(2) if opened else ''
        time.sleep(1.0)
        m3b = mark()
        send_in_view('Change le gap en 8 px')
        step('R9 · the turn starts', logged(r'CHAT turn t\d+ started', m3b, 15) is not None)
        asked = logged(r'CHAT permission asked: (\S+)', m3b, 30)
        logged(r'CHAT modes offered', 0, 5)
        modes = re.findall(r'CHAT modes offered: ([^\r\n]+)', C.log_text())
        report['measures']['modes'] = modes[-1] if modes else ''
        step('R4 · the modes of Paper 14RK-0, in order: Accept edits, Auto, Demander, Planifier',
             bool(modes) and modes[-1].strip() == 'acceptEdits=edits, auto=auto, default=ask, plan=plan', modes[-1] if modes else 'no log')
        step('R9 · the permission is asked', asked is not None, asked.group(0) if asked else '')
        step('R2 / D1 · after the first message, bubble open: the element shows the bubble frame (its icon comes when the bubble closes)', C.wait_for(lambda: bubble_frame()['frames'] == 1, 6))
        m4 = mark()
        focus_composer(); C.key_panel('Escape', 'Escape', 27); logged(r'CHAT bubble closed', m4, 5); time.sleep(0.8)
        pointer_away()
        b2 = bubble_frame(); report['measures']['d2'] = b2
        step('D2 · bubble closed, agent at work: the icon of 18IJ-0, no spinner, no frame of the bubble', b2['spinner'] == 0 and b2['frames'] == 0 and len(b2['marks']) == 1, b2)
        step('D3 · the element is in the page: no launcher, no activity on it', not launcher_shown())
        capture('r9-bulle-fermee-autorisation-en-attente')
        capture('d2-agent-au-travail-bulle-fermee')
        m5 = mark()
        C.click_page('.btn-primary')
        again = logged(r'CHAT bubble opened on (\S+) for conversation (c_\w+)', m5, 10)
        step('R6 · a click on the element with a conversation opens the bubble on it', again is not None and again.group(2) == conv, again.group(0)[5:] if again else '')
        card = C.wait_for(lambda: 'Autoriser' in (C.panel_text() or ''), 6)
        step('R9 · opened again: the permission card is there (it was lost before: cause of the stuck turn)', bool(card), (C.panel_text() or '')[-200:].replace('\n', ' | '))
        time.sleep(0.4); capture('r9-autorisation-apres-reouverture'); capture_bubble('r9-bulle-autorisation')
        m6 = mark()
        clicked = real_click_in_bubble('Autoriser')
        answered = logged(r'CHAT permission answered: (\S+)', m6, 6)
        step('input · a real mouse click (SendInput) in the bubble answers the permission', clicked and answered is not None, answered.group(0) if answered else '')
        if not answered:
            C.click_panel(text='Autoriser')
        ended = logged(r'CHAT turn t\d+ ended: (\w+) in (\d+) ms', m3b, 30)
        step('R9 · the turn ends', ended is not None and ended.group(1) == 'end_turn', ended.group(0) if ended else '')
        time.sleep(1.0); pointer_away(); capture('r9-termine'); capture('d1-bulle-ouverte-session-prete')
        m6b = mark(); focus_composer(); C.key_panel('Escape', 'Escape', 27); logged(r'CHAT bubble closed', m6b, 5); time.sleep(0.6); pointer_away()
        b3 = bubble_frame(); report['measures']['d2idle'] = b3
        step('D2 · bubble closed, agent idle: the icon alone (no label, no inspector)', b3['frames'] == 0 and len(b3['marks']) == 1 and b3['hoverLabel'] == 0 and b3['inspector'] == 0, b3)
        capture('d2-bulle-fermee')
        C.click_page('.btn-primary'); time.sleep(1.2)

        # ---- R3 in the windowed hosting: menus and tooltips out of the bubble
        global rest_img_box
        bw0 = CC.bubble_window(); rest = bw0[3]
        rest_img_box = (max(0, rest[0] - 420), max(0, rest[1] - 420), rest[2] + 420, rest[3] + 420)
        rest_img = screen_grab(rest_img_box)
        def click_sel(sel):
            return lambda: C.click_panel(selector=sel)
        r3_measure('menu-modele', click_sel('button.m'), rest, rest_img)
        def open_effort():
            C.click_panel(selector='button.m'); time.sleep(0.5)
            row = C.panel("(function(){var r=document.querySelector('.menu [data-key=effort]');if(!r)return null;var b=r.getBoundingClientRect();return [b.left+b.width/2,b.top+b.height/2]})()")
            if row:
                C.ws_for(C.PANEL).call('Input.dispatchMouseEvent', {'type': 'mouseMoved', 'x': row[0], 'y': row[1]})
                time.sleep(0.4)
        r3_measure('sous-menu-effort', open_effort, rest, rest_img)
        r3_measure('menu-mode', click_sel('button.tb.mode'), rest, rest_img)
        # R22 (v3): « + » opens the image picker directly (a system dialog): not a menu any more, not measured here
        def hover_history():
            at = C.panel("(function(){var l=[].slice.call(document.querySelectorAll('button,[role=button],span,div')).filter(function(e){return (e.textContent||'').trim()==='Historique'&&e.getBoundingClientRect().width>0});"
                         "var e=l[l.length-1];if(!e)return null;var b=e.getBoundingClientRect();return [b.left+b.width/2,b.top+b.height/2]})()")
            if at:   # final QA: the REAL pointer (the host closes a hover tooltip when the real pointer is not over the bubble, M10)
                bw = CC.bubble_window(); k = C.panel('devicePixelRatio') or 1
                if bw:
                    u32.SetCursorPos(int(bw[3][0] + at[0] * k), int(bw[3][1] + at[1] * k)); time.sleep(0.05)
                    u32.SetCursorPos(int(bw[3][0] + at[0] * k) + 2, int(bw[3][1] + at[1] * k))
        def leave_history():   # a hover tooltip closes when the pointer leaves it (Échap would close the bubble: §19.4)
            bw = CC.bubble_window()
            if bw:
                u32.SetCursorPos(bw[3][0] - 200 if bw[3][0] > 300 else bw[3][2] + 200, bw[3][3] + 200)
        r3_measure('infobulle-historique', hover_history, rest, rest_img, leave_history)

        # ---- R9 measures: the same request, permission answered at once, bubble vs panel
        def measure(where):
            u32.SetForegroundWindow(Q.main_window()); time.sleep(0.3)
            mm = mark()
            if where == 'panneau':   # final QA: the panel's page by its own target, a real click in its composer and real keys (the user's way)
                sys.path.insert(0, str(C.HERE / 'final')); import qa_final as QF
                QF.type_and_send('Change le gap en 8 px (permission)', 'panel')
            else:
                send_in_view('Change le gap en 8 px (permission)')   # the fake agent's « permission » scenario: same steps every time
            shown, answered, end, deadline = [], 0, None, time.time() + 40
            while time.time() < deadline:
                text = C.log_text()[mm:]
                end = re.search(r'CHAT turn t\d+ ended: \w+ in (\d+) ms', text)
                if end:
                    break
                if text.count('CHAT permission asked') > answered:
                    t0 = time.time()
                    if where == 'panneau':
                        if C.wait_for(lambda: 'Autoriser' in (QF.chat('document.body.innerText', 'panel') or ''), 6):
                            shown.append(round(time.time() - t0, 3)); QF.real_click_chat(text='Autoriser', kind='panel', tries=4)
                    elif C.wait_for(lambda: 'Autoriser' in (C.panel_text() or ''), 6):
                        shown.append(round(time.time() - t0, 3)); C.click_panel(text='Autoriser')
                    answered += 1
                time.sleep(0.05)
            first = re.search(r'CHAT first agent text ([\d.]+) ms after the send', C.log_text()[mm:])
            return {'where': where, 'firstTextMs': float(first.group(1)) if first else None, 'turnMs': float(end.group(1)) if end else None,
                    'permissions': answered, 'cardShownAfterAskS': shown}
        bub = measure('bulle'); time.sleep(1.0)
        m7 = mark(); C.invoke('AnnotationAskAgent'); logged(r'CHAT panel opened', m7, 10); time.sleep(1.5)
        pan = measure('panneau')
        report['measures']['r9'] = [bub, pan]
        ok = bub['turnMs'] and pan['turnMs'] and bub['turnMs'] <= max(3 * pan['turnMs'], pan['turnMs'] + 1500)
        step('R9 · bubble as fast as the panel (same order of magnitude)', bool(ok), f'{bub} / {pan}')
        back = None
        for _ in range(3):
            m8 = mark(); u32.SetForegroundWindow(Q.main_window()); C.invoke('AnnotationLayoutComment')
            back = logged(r'CHAT layout: Commentaire', m8, 5)
            if back:
                break
        step('back to Commentaire', back is not None)
        time.sleep(0.8)
        m8b = mark(); C.click_page('.field-label'); other = logged(r'CHAT bubble opened on (\S+) for conversation (c_\w+)', m8b, 10); time.sleep(1.0)
        send_in_view('echo autre element'); logged(r'CHAT turn t\d+ ended', m8b, 30); time.sleep(0.5)
        focus_composer(); C.key_panel('Escape', 'Escape', 27); time.sleep(0.6)

        # ---- R6: its conversation file is gone: the bubble still opens, on a new conversation
        store = C.APP / 'annotations-chat'
        removed = [f for f in store.rglob(f'{conv}.json')] if conv else []
        for f in removed:
            try:
                f.unlink()
            except OSError:
                pass
        m9 = mark()
        C.click_page('.btn-primary')
        fresh = logged(r'CHAT bubble opened on (\S+) for conversation (c_\w+)', m9, 10)
        nf = logged(r'CHAT conversation not found: (c_\w+)', m9, 5)
        step('R6 / D5 · its conversation unreadable: the bubble opens anyway, on a new conversation', fresh is not None and nf is not None and fresh.group(2) != conv,
             (fresh.group(0)[5:] if fresh else '') + f' ({len(removed)} file(s) removed; was {conv})')
        capture('r6-reouverture')
    except Exception:
        traceback.print_exc()
        step('run without exception', False, traceback.format_exc()[-400:])
    finally:
        C.quit_all()
        after = C.fingerprints(); report['fingerprintsAfter'] = after
        step('real Claude / Codex configuration files unchanged', before == after)
        ps = __import__('subprocess').run(['tasklist', '/FI', 'IMAGENAME eq fake-acp-agent.exe', '/NH'], capture_output=True, text=True).stdout
        step('nothing left running (fake agent)', 'fake-acp-agent' not in ps)
        (OUT / 'host-report.json').write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf-8')
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
