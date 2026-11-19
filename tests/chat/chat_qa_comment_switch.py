#!/usr/bin/env python3
"""Mode Commentaire v2 — SHORT real-window pass (fake agent), about 2 minutes:

    python tests/chat/chat_qa_comment_switch.py   # -> build/chat-qa/comment-v2/switch-report.json + captures

  1  D1 after a reload: the agent's edit reloads the page; the bubble opened again on the element still draws Paper's
     frame + two labels, no hover frame, no inspector.
  2  the band's switch with the bubble's WebView in existence: Panneau then Commentaire, each tried first with a REAL
     mouse click (SendInput, as the user does), then, for the diagnosis, with the UIA invoke the other scripts use;
     the UIA search time per top-level window is recorded (does it stall on the bubble?).
  3  D5: a conversation whose file is gone (and no longer in memory) still opens a bubble, on a new conversation.

Same guards as tests/chat/chat_qa.py (own copy, own profile, BROWSER_ANNOTATIONS_TEST=1, real configs fingerprinted).
The real mouse is moved: run it only when the user said the computer is free (« Libre »). Nothing left running.
"""
from __future__ import annotations
import ctypes
import ctypes.wintypes as W
import json
import re
import subprocess
import sys
import time
import traceback

import chat_qa as C
from chat_qa import Q
import chat_qa_comment as CC
import chat_qa_comment_v2 as V

OUT = C.QA / 'comment-v2'
report = {'steps': [], 'measures': {}, 'captures': []}
u32 = ctypes.windll.user32
started = time.time()


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


def uia_bounds(ids):
    t0 = time.time()
    try:
        r = subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(C.ROOT / 'tests' / 'chat' / 'uia_bounds.ps1'),
                            '-ProcessId', str(Q.our_pid()), '-Ids', ','.join(ids)], capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=90)
        out = r.stdout.strip()
    except subprocess.TimeoutExpired:
        out = 'TIMEOUT'
    rects = {}
    for line in out.splitlines():
        p = line.split()
        if len(p) == 6 and p[0] == 'ID':
            rects[p[1]] = tuple(int(v) for v in p[2:])
    return rects, out, round(time.time() - t0, 2)


class MOUSEINPUT(ctypes.Structure):
    _fields_ = [('dx', W.LONG), ('dy', W.LONG), ('mouseData', W.DWORD), ('dwFlags', W.DWORD), ('time', W.DWORD), ('dwExtraInfo', ctypes.c_size_t)]


class INPUT(ctypes.Structure):
    class _U(ctypes.Union):
        _fields_ = [('mi', MOUSEINPUT), ('pad', ctypes.c_byte * 32)]
    _anonymous_ = ('u',)
    _fields_ = [('type', W.DWORD), ('u', _U)]


def real_click(rect):
    """A real left click (SendInput) in the middle of `rect` (screen px): the same path as the user's mouse."""
    x, y = (rect[0] + rect[2]) // 2, (rect[1] + rect[3]) // 2
    u32.SetCursorPos(x, y); time.sleep(0.15)
    for flag in (0x0002, 0x0004):   # LEFTDOWN, LEFTUP
        i = INPUT(); i.type = 0; i.mi = MOUSEINPUT(0, 0, 0, flag, 0, 0)
        u32.SendInput(1, ctypes.byref(i), ctypes.sizeof(INPUT)); time.sleep(0.06)
    time.sleep(0.2)
    u32.SetCursorPos(rect[0] - 400 if rect[0] > 600 else rect[2] + 400, rect[3] + 500)   # then out of the band and of the element


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
        rects, out0, s0 = uia_bounds(['AnnotationLayoutComment', 'AnnotationAskAgent'])
        report['measures']['uiaBeforeBubble'] = {'seconds': s0, 'out': out0}
        step('the switch\'s two buttons located (UIA, no bubble yet)', 'AnnotationLayoutComment' in rects and 'AnnotationAskAgent' in rects, f'{rects} in {s0} s')
        comment_r, panel_r = rects.get('AnnotationLayoutComment'), rects.get('AnnotationAskAgent')
        m0 = mark(); real_click(comment_r)
        step('real click on « Commentaire » (no bubble yet)', logged(r'CHAT layout: Commentaire', m0, 5) is not None)

        # ---- 1: D1 after the reload caused by the agent's edit
        m1 = mark(); C.click_page('.btn-primary')
        opened = logged(r'CHAT bubble opened on (\S+) for conversation (c_\w+)', m1, 10); conv = opened.group(2) if opened else ''
        logged(r'CHAT panel ready', m1, 15); time.sleep(1.0)
        m2 = mark(); V.send_in_view('Change le gap en 8 px')
        logged(r'CHAT permission asked', m2, 30)
        C.wait_for(lambda: 'Autoriser' in (C.panel_text() or ''), 6); C.click_panel(text='Autoriser')
        end = logged(r'CHAT turn t\d+ ended', m2, 30)
        reload = logged(r'ANNOTATE mode re-armed after a reload', m2, 10)
        step('the edit is done and the page reloads', end is not None and reload is not None)
        time.sleep(1.5); V.pointer_away()
        bf = V.bubble_frame(); report['measures']['d1AfterReload'] = bf
        step('D1 after the reload: the open bubble\'s frame + two labels, no hover label, no inspector',
             bf['frames'] == 1 and len(bf['labels']) == 2 and bf['hoverLabel'] == 0 and bf['inspector'] == 0, bf)
        capture('s1-d1-apres-rechargement')

        # ---- 2: the switch, the bubble's WebView in existence
        rects2, out2, s2 = uia_bounds(['AnnotationLayoutComment', 'AnnotationAskAgent'])
        report['measures']['uiaWithBubble'] = {'seconds': s2, 'out': out2}
        print(out2)
        m3 = mark(); real_click(panel_r)
        to_panel = logged(r'CHAT layout: Panneau', m3, 5); popen = logged(r'CHAT panel opened', m3, 10)
        step('real click on « Panneau » with the bubble open: the panel opens', to_panel is not None and popen is not None)
        time.sleep(1.2); capture('s2-panneau')
        m4 = mark(); real_click(comment_r)
        back = logged(r'CHAT layout: Commentaire', m4, 5)
        step('real click on « Commentaire » with the panel open: back to Commentaire (the user\'s path)', back is not None)
        time.sleep(0.8); capture('s2-retour-commentaire')
        # the same two moves through UIA (the other scripts' path), for the diagnosis
        m5 = mark(); t5 = time.time(); inv1 = C.uia('-Id', 'AnnotationAskAgent', '-Invoke'); d5 = round(time.time() - t5, 2)
        u_panel = logged(r'CHAT layout: Panneau', m5, 5)
        time.sleep(1.0)
        m6 = mark(); t6 = time.time(); inv2 = C.uia('-Id', 'AnnotationLayoutComment', '-Invoke'); d6 = round(time.time() - t6, 2)
        u_back = logged(r'CHAT layout: Commentaire', m6, 5)
        report['measures']['uiaInvoke'] = {'toPanel': [inv1, d5, u_panel is not None], 'toComment': [inv2, d6, u_back is not None]}
        step('(diagnosis) UIA invoke « Panneau » then « Commentaire »', u_panel is not None and u_back is not None, report['measures']['uiaInvoke'])
        if not u_back:
            m6b = mark(); real_click(comment_r); logged(r'CHAT layout: Commentaire', m6b, 5)
        time.sleep(0.8)

        # ---- 3: D5, a conversation whose file is gone and that is no longer in memory
        m7 = mark(); C.click_page('.field-label'); logged(r'CHAT bubble opened on', m7, 10); time.sleep(1.0)
        V.send_in_view('echo autre element'); logged(r'CHAT turn t\d+ ended', m7, 30); time.sleep(0.5)
        V.focus_composer(); C.key_panel('Escape', 'Escape', 27); time.sleep(0.6)
        removed = [f for f in (C.APP / 'annotations-chat').rglob(f'{conv}.json')] if conv else []
        for f in removed:
            try:
                f.unlink()
            except OSError:
                pass
        m8 = mark(); C.click_page('.btn-primary')
        fresh = logged(r'CHAT bubble opened on (\S+) for conversation (c_\w+)', m8, 10)
        nf = logged(r'CHAT conversation not found: (c_\w+)', m8, 5)
        step('D5: its conversation unreadable: the bubble opens anyway, on a new conversation', fresh is not None and nf is not None and fresh.group(2) != conv,
             (fresh.group(0)[5:] if fresh else '') + f' ({len(removed)} file(s) removed; was {conv})')
        time.sleep(0.8); capture('s3-d5-nouvelle-conversation')
    except Exception:
        traceback.print_exc()
        step('run without exception', False, traceback.format_exc()[-400:])
    finally:
        C.quit_all()
        after = C.fingerprints(); report['fingerprintsAfter'] = after
        step('real Claude / Codex configuration files unchanged', before == after)
        ps = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq fake-acp-agent.exe', '/NH'], capture_output=True, text=True).stdout
        step('nothing left running (fake agent)', 'fake-acp-agent' not in ps)
        report['seconds'] = round(time.time() - started, 1)
        (OUT / 'switch-report.json').write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf-8')
    failed = [s['step'] for s in report['steps'] if not s['ok']]
    print(f"{len(report['steps']) - len(failed)} / {len(report['steps'])} steps passed in {report['seconds']} s")
    return 1 if failed else 0


if __name__ == '__main__':
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding='utf-8')
        except Exception:
            pass
    sys.exit(run_main())
