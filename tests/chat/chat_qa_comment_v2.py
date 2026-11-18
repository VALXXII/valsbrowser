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


def focus_composer():
    C.panel("(function(){var t=document.querySelector('[contenteditable=true]');if(t)t.focus();return 1})()")


def send_in_view(text):
    focus_composer(); C.type_panel(text); time.sleep(0.2); C.enter_panel()


def real_click_in_bubble(label):
    """A real mouse message posted to the bubble's window at the button named `label` (tests the host's forwarding)."""
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
        step('R1 · the bubble\'s page is a transparent visual (no DWM corner, no region)', report['measures']['hosting'] == 'visual')
        bw = CC.bubble_window()
        if bw:
            pref = ctypes.c_int(0); ctypes.windll.dwmapi.DwmGetWindowAttribute(bw[0], 33, ctypes.byref(pref), 4)
            ex = u32.GetWindowLongW(bw[0], -20)
            report['measures']['bubbleWindow'] = {'rect': bw[3], 'cornerPreference': pref.value, 'exStyle': hex(ex & 0xFFFFFFFF)}
            step('R1 · …window: DWMWCP_DONOTROUND, WS_EX_NOREDIRECTIONBITMAP', pref.value == 1 and (ex & 0x00200000) != 0, report['measures']['bubbleWindow'])
            step('R1 · …surface 305 CSS px wide at rest (inset 0: Paper has no shadow)', abs((bw[3][2] - bw[3][0]) - round(305 * C.evaluate(C.URL, 'devicePixelRatio'))) <= 1, bw[3])
        capture_bubble('r1-bulle-coins'); capture('r2-bulle-ouverte')
        modes = re.findall(r'CHAT modes offered: ([^\r\n]+)', C.log_text())
        report['measures']['modes'] = modes[-1] if modes else ''
        step('R4 · the modes of Paper 14RK-0, in order: Accept edits, Auto, Demander, Planifier',
             bool(modes) and modes[-1].strip() == 'acceptEdits=edits, auto=auto, default=ask, plan=plan', modes[-1] if modes else 'no log')
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
        step('R9 · the permission is asked', asked is not None, asked.group(0) if asked else '')
        step('R2 · after the first message the element carries its frame + icon', bool(C.wait_for(lambda: CC.conv_marks(), 6)))
        m4 = mark()
        focus_composer(); C.key_panel('Escape', 'Escape', 27); logged(r'CHAT bubble closed', m4, 5); time.sleep(0.8)
        capture('r9-bulle-fermee-autorisation-en-attente')
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
        step('input · a real mouse click posted to the bubble window answers the permission (host forwarding)', clicked and answered is not None, answered.group(0) if answered else '')
        if not answered:
            C.click_panel(text='Autoriser')
        ended = logged(r'CHAT turn t\d+ ended: (\w+) in (\d+) ms', m3b, 30)
        step('R9 · the turn ends', ended is not None and ended.group(1) == 'end_turn', ended.group(0) if ended else '')
        time.sleep(1.0); capture('r9-termine')

        # ---- R9 measures: the same request, permission answered at once, bubble vs panel
        def measure(where):
            mm = mark()
            send_in_view('Change le gap en 8 px (permission)')   # the fake agent's « permission » scenario: same steps every time
            logged(r'CHAT permission asked', mm, 30)
            t0 = time.time()
            C.wait_for(lambda: 'Autoriser' in (C.panel_text() or ''), 6)
            shown = time.time() - t0
            C.click_panel(text='Autoriser')
            first = ms_after(r'CHAT first agent text ([\d.]+) ms after the send', mm)
            end = logged(r'CHAT turn t\d+ ended: \w+ in (\d+) ms', mm, 30)
            return {'where': where, 'firstTextMs': first, 'turnMs': float(end.group(1)) if end else None, 'cardShownAfterAskS': round(shown, 3)}
        bub = measure('bulle'); time.sleep(1.0)
        m7 = mark(); C.invoke('AnnotationAskAgent'); logged(r'CHAT panel opened', m7, 10); time.sleep(1.5)
        pan = measure('panneau')
        report['measures']['r9'] = [bub, pan]
        ok = bub['turnMs'] and pan['turnMs'] and bub['turnMs'] <= max(3 * pan['turnMs'], pan['turnMs'] + 1500)
        step('R9 · bubble as fast as the panel (same order of magnitude)', bool(ok), f'{bub} / {pan}')
        m8 = mark(); C.invoke('AnnotationLayoutComment'); logged(r'CHAT layout: Commentaire', m8, 5); time.sleep(0.8)

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
        step('R6 · its conversation unreadable: the bubble opens anyway (a new conversation)', fresh is not None, (fresh.group(0)[5:] if fresh else '') + f' ({len(removed)} file(s) removed)')
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
