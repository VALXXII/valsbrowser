#!/usr/bin/env python3
"""Bug « clic sur un élément, pas de bulle » (test réel 0.3.473), real window, fake agent, REAL clicks.

    CHAT_QA_BUILD=chat-cv5b python tests/chat/chat_qa_comment_v5b.py   # -> build/chat-qa/comment-v5b/report.json + captures

First (the user's case): a click on an element that ALREADY has a conversation (frame + icon) shows the bubble with that
conversation, whatever came before:
  E1  another element's bubble open (a narrow one, the element clicked is page-wide like `section.hero`)
  E2  this element's bubble already open
  E3  this element's bubble closed (Échap)
  E4  after another application took the foreground (the bubble is hidden, then back)
  E5  while its agent works on another element (a turn running): never stopped
Then: A -> other application -> B -> A ; A -> B -> A -> B fast ; the log says the idle agent stops unambiguously.

« Visible » = the bubble's window is visible, its region is not empty, inside the page area, next to the element, drawn
(dark surface pixels), and the page shows that element's conversation.
Same guards as tests/chat/chat_qa.py. Only when the computer is free (« Libre »).
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
import chat_qa_comment_v4b as V4

OUT = C.QA / 'comment-v5b'
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
        path = C.grab(f'comment-v5b/{name}'); report['captures'].append(path.name)
    except Exception as e:
        print('capture failed', name, e)


def mouse(flags):
    i = V._IN(); i.type = 0; i.mi = V._MI(0, 0, 0, flags, 0, 0); u32.SendInput(1, ctypes.byref(i), ctypes.sizeof(V._IN))


def real_click(sel, front=True):
    """A real click (SendInput) in the middle of a page element — what the user does."""
    if front:
        V.front()
    el = CC.element_screen(sel)
    u32.SetCursorPos((el[0] + el[2]) // 2, (el[1] + el[3]) // 2); time.sleep(0.08)
    mouse(0x0002); time.sleep(0.03); mouse(0x0004)


def region_box(hwnd):
    w0 = V3.win_rect(hwnd); rg = W.RECT(); kind = u32.GetWindowRgnBox(hwnd, ctypes.byref(rg)); wr = V3.win_rect(hwnd)
    if kind == 1 or wr != w0:
        return None
    return (wr[0] + rg.left, wr[1] + rg.top, wr[0] + rg.right, wr[1] + rg.bottom)


def shown_on(sel, text):
    """Is the bubble on screen, next to `sel`, drawn, showing the conversation that contains `text`?"""
    b = V.bubble_hwnd()
    if not b:
        return False, {'window': None}
    h = b[0][0]; vis = bool(u32.IsWindowVisible(h)); rb = region_box(h); el = CC.element_screen(sel); pg = CC.page_widget()
    d = {'visible': vis, 'region': rb, 'element': el, 'page': pg}
    if not vis or not rb or rb[2] - rb[0] < 100 or rb[3] - rb[1] < 40:
        return False, d
    inside = rb[0] >= pg[0] - 1 and rb[1] >= pg[1] - 1 and rb[2] <= pg[2] + 1 and rb[3] <= pg[3] + 1
    gap_x = max(0, el[0] - rb[2], rb[0] - el[2]); gap_y = max(0, el[1] - rb[3], rb[1] - el[3])
    w_, h_, raw = V4.fast_grab(rb); n = dk = 0
    for y in range(4, h_ - 4, 6):
        for x in range(4, w_ - 4, 6):
            i = (y * w_ + x) * 4; n += 1; dk += (raw[i] + raw[i + 1] + raw[i + 2]) < 200
    body = C.panel("(document.body.innerText||'').replace(/\\s+/g,' ')") or ''
    d.update({'inside': inside, 'gap': (gap_x, gap_y), 'drawn': round(dk / max(1, n), 2), 'conversation': text in body})
    return inside and gap_x <= 60 and gap_y <= 60 and dk / max(1, n) > 0.4 and text in body, d


def wait_shown(sel, text, label, timeout=3.0):
    t0 = time.time(); ok, d = False, {}
    while time.time() - t0 < timeout:
        ok, d = shown_on(sel, text)
        if ok:
            break
        time.sleep(0.1)
    d['ms'] = round((time.time() - t0) * 1000)
    return step(label, ok, d)


class OtherApp:
    """A window of ANOTHER process (a tiny Tk window of our own) brought to the front, then closed."""
    def __enter__(self):
        self.p = subprocess.Popen([sys.executable, '-c', "import tkinter as t;r=t.Tk();r.title('QA autre application');r.geometry('320x160+40+40');r.after(15000,r.destroy);r.mainloop()"])
        self.h = 0
        for _ in range(50):
            self.h = u32.FindWindowW(None, 'QA autre application')
            if self.h:
                break
            time.sleep(0.1)
        for _ in range(10):
            u32.keybd_event(0x12, 0, 0, 0); u32.keybd_event(0x12, 0, 2, 0); u32.SetForegroundWindow(self.h); time.sleep(0.1)
            if u32.GetForegroundWindow() == self.h:
                break
        return self
    def __exit__(self, *a):
        try:
            self.p.kill(); self.p.wait(3)
        except Exception:
            pass


def send_and_wait(text):
    m = mark(); V.send_in_view(text); logged(r'CHAT turn t\d+ ended', m, 30); time.sleep(0.6)


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
                        'BROWSER_CHAT_FAKE_AGENT': f'"{fake}" --resume --claude-like --log "{flog}"'})
    try:
        time.sleep(1.0)
        C.shortcut('A'); step('mode on', logged(r'ANNOTATE mode on', 0, 10)); time.sleep(1.2)
        if C.invoke('AnnotationConnectLater') or C.invoke('AnnotationConnectClose'):
            time.sleep(0.6)
        V.front()
        m1 = mark(); C.invoke('AnnotationLayoutComment'); logged(r'CHAT layout: Commentaire', m1, 5); time.sleep(1.5)
        # A: page-wide (like section.hero), B: narrow (like h2.section-title)
        C.page("(function(){document.body.style.background='#fff';"
               "var a=document.createElement('section');a.id='qa-a';a.textContent='Section large';a.style.cssText='position:absolute;left:0;right:0;top:250px;height:150px;background:#eef;z-index:5';document.body.appendChild(a);"
               "var b=document.createElement('h2');b.id='qa-b';b.textContent='Titre';b.style.cssText='position:absolute;left:40px;top:470px;width:200px;height:40px;margin:0;background:#efe;z-index:5';document.body.appendChild(b);"
               "window.scrollTo(0,0);return 1})()")
        time.sleep(0.6)

        # the two conversations (frame + icon on each element afterwards)
        real_click('#qa-a'); logged(r'CHAT bubble opened on', 0, 10); time.sleep(1.2)
        send_and_wait('echo conversation-A'); capture('a-conversation')
        C.key_panel('Escape', 'Escape', 27); time.sleep(0.8)
        m = mark(); real_click('#qa-b'); logged(r'CHAT bubble opened on', m, 10); time.sleep(1.2)
        send_and_wait('echo conversation-B'); capture('b-conversation')
        marks = CC.conv_marks(); report['measures']['marks'] = len(marks) if isinstance(marks, list) else marks

        # ---- E1: B open, a click on A (page-wide, has its conversation)
        m = mark(); real_click('#qa-a'); wait_shown('#qa-a', 'conversation-A', 'E1 · B open, click on A (its conversation, page-wide): A\'s bubble on screen'); capture('e1-a-apres-b')
        report['measures']['e1-log'] = [l for l in C.log_text()[m:].splitlines() if 'bubble' in l][:8]
        # ---- E2: A open, a click on A again
        real_click('#qa-a'); time.sleep(0.5); wait_shown('#qa-a', 'conversation-A', 'E2 · A open, click on A again: still on screen')
        # ---- E3: A closed by Échap, a click on A
        C.key_panel('Escape', 'Escape', 27); time.sleep(0.8)
        real_click('#qa-a'); wait_shown('#qa-a', 'conversation-A', 'E3 · A closed (Échap), click on A: on screen')
        # ---- E4: another application in front, then a click on A
        m = mark()
        with OtherApp():
            time.sleep(0.8)
            b = V.bubble_hwnd(); hidden = b and not u32.IsWindowVisible(b[0][0])
            step('E4 · another application in front: the bubble is hidden (not closed)', hidden and 'bubble closed' not in C.log_text()[m:], [l for l in C.log_text()[m:].splitlines() if 'bubble' in l][:4])
            real_click('#qa-a', front=False)
            wait_shown('#qa-a', 'conversation-A', 'E4 · back with a click on A: A on screen as it was')
        # ---- A -> other application -> B -> A
        with OtherApp():
            time.sleep(0.6); real_click('#qa-b', front=False)
            wait_shown('#qa-b', 'conversation-B', 'A, other application, click on B: B on screen')
        real_click('#qa-a'); wait_shown('#qa-a', 'conversation-A', 'then click on A: A on screen')
        # ---- fast A -> B -> A -> B
        for s in ('#qa-b', '#qa-a', '#qa-b'):
            real_click(s); time.sleep(0.18)
        wait_shown('#qa-b', 'conversation-B', 'fast A -> B -> A -> B: B on screen at the end')
        real_click('#qa-a'); wait_shown('#qa-a', 'conversation-A', 'and A again: on screen')
        # ---- E5: A's agent working (a turn running), click on B, then back on A
        m = mark(); V.send_in_view('slow'); logged(r'CHAT turn t\d+ started|CHAT turn t\d+ sent|agent', m, 10); time.sleep(1.5)
        real_click('#qa-b'); wait_shown('#qa-b', 'conversation-B', 'E5 · A working, click on B: B on screen')
        real_click('#qa-a'); wait_shown('#qa-a', 'conversation-A', 'E5 · and back on A: on screen')
        lines = C.log_text()[m:].splitlines()
        stopped = [l for l in lines if 'agent stopped' in l]
        ended = [l for l in lines if re_turn_end(l)]
        step('E5 · a working agent is never stopped by a click on another element', not any('a turn was running' in l for l in stopped) and not ended, {'stopped': stopped, 'ended': ended})
        V.focus_composer(); C.key_panel('Escape', 'Escape', 27); time.sleep(0.3)
        try:
            C.click_panel(text='Arrêter')
        except Exception:
            pass
        time.sleep(1.0)
        idle = [l for l in C.log_text().splitlines() if 'agent stopped (' in l]
        report['measures']['agentStoppedLines'] = idle[-6:]
        step('the log says when an idle agent stops, unambiguously', all('no turn was running' in l or 'a turn was running' in l for l in idle), idle[-4:])
    except Exception:
        traceback.print_exc()
        step('run without exception', False, traceback.format_exc()[-400:])
    finally:
        C.quit_all()
        after = C.fingerprints(); report['fingerprintsAfter'] = after
        step('real Claude / Codex configuration files unchanged', before == after)
        ps = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq fake-acp-agent.exe', '/NH'], capture_output=True, text=True).stdout
        step('nothing left running (fake agent)', 'fake-acp-agent' not in ps)
        step('nothing left open (the other application\'s window)', not u32.FindWindowW(None, 'QA autre application'))
        (OUT / 'report.json').write_text(json.dumps(report, indent=2, ensure_ascii=False, default=str), encoding='utf-8')
    failed = [s['step'] for s in report['steps'] if not s['ok']]
    print(f"{len(report['steps']) - len(failed)} / {len(report['steps'])} steps passed")
    return 1 if failed else 0


def re_turn_end(line):
    import re
    return re.search(r'CHAT turn t\d+ ended', line) is not None


if __name__ == '__main__':
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding='utf-8')
        except Exception:
            pass
    sys.exit(run_main())
