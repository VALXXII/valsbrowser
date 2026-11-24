#!/usr/bin/env python3
"""#129 / #130 in a REAL window (night run only: it takes the screen and the mouse). Fake agent, fresh profile, the sandbox
checkout page (« Payer 48,00 € », the issues' own example), the pay button pinned near the bottom of a page that cannot scroll.

    CHAT_QA_BUILD=<build folder under build/> python tests/chat/chat_qa_comment_129_130.py
    -> build/chat-qa/comment-129-130/report.json + captures

#129  before the agent said its models (fake agent answering session/new only after 10 s), the bubble's model button shows
      « Opus 5.5 · Moyen »; a click opens a menu WITH rows (the providers, as the panel), above the bubble, whole in the page
      area, inside the bubble window's region, and on top of everything (WindowFromPoint on it = the bubble's window).
      Once the models are known: a model and an effort chosen in the bubble reach the session (fake agent log:
      session/set_config_option) before the comment is sent.
#130  variant C (Paper « #130 Bulle sans place dessous · C remonte le long du côté (validée) »): the bubble of that low button
      grows with an authorization card — it stays at the button's right (+12 px), slides up along it until its bottom is 8 px
      above the page area's bottom (never flipped above it), Refuser / Autoriser on screen and on top, a REAL click on Autoriser
      answers it; the model menu still opens whole on top. Rule 5 (recomputing during a scroll) is not part of this run.

Same guards as tests/chat/chat_qa.py (copy of the build, its own profile, BROWSER_ANNOTATIONS_TEST=1, agent configurations
under build/, the real Claude / Codex configuration files fingerprinted before and after). The browser, its server and the
fake agent run in a job object closed with this process (KILL_ON_JOB_CLOSE).
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
import chat_qa_comment as CC
import chat_qa_comment_v2 as V
import chat_qa_comment_v3 as V3
import chat_qa_comment_v5b as V5

OUT = C.QA / 'comment-129-130'
report = {'steps': [], 'measures': {}, 'captures': []}
u32 = ctypes.windll.user32
k32 = ctypes.WinDLL('kernel32', use_last_error=True)


def step(name, ok, detail=''):
    report['steps'].append({'step': name, 'ok': bool(ok), 'detail': str(detail)[:1500]})
    print(('PASS ' if ok else 'FAIL ') + name + (' — ' + str(detail)[:400] if detail else ''), flush=True)
    return bool(ok)


def mark():
    return len(C.log_text())


def capture(name):
    try:
        path = C.grab(f'comment-129-130/{name}'); report['captures'].append(path.name)
    except Exception as e:
        print('capture failed', name, e)


def job_for(pids):
    """KILL_ON_JOB_CLOSE: the processes started here die with this runner, whatever happens."""
    class BASIC(ctypes.Structure):
        _fields_ = [('a', ctypes.c_int64), ('b', ctypes.c_int64), ('LimitFlags', W.DWORD), ('c', ctypes.c_size_t), ('d', ctypes.c_size_t),
                    ('e', W.DWORD), ('f', ctypes.c_size_t), ('g', W.DWORD), ('h', W.DWORD)]
    class EXT(ctypes.Structure):
        _fields_ = [('Basic', BASIC), ('Io', ctypes.c_uint64 * 6), ('i', ctypes.c_size_t), ('j', ctypes.c_size_t), ('k', ctypes.c_size_t), ('l', ctypes.c_size_t)]
    k32.CreateJobObjectW.restype = W.HANDLE; k32.OpenProcess.restype = W.HANDLE
    job = k32.CreateJobObjectW(None, None)
    info = EXT(); info.Basic.LimitFlags = 0x2000
    k32.SetInformationJobObject(job, 9, ctypes.byref(info), ctypes.sizeof(info))
    for pid in pids:
        h = k32.OpenProcess(0x0101, False, pid)
        if h:
            k32.AssignProcessToJobObject(job, h); k32.CloseHandle(h)
    return job


def bubble():
    b = V.bubble_hwnd()
    return b[0][0] if b else 0


def to_screen(h, r):
    """A rectangle of the bubble's page (CSS px, its viewport) on screen: the WebView fills the bubble window from (0, 0)."""
    w = V3.win_rect(h); dpr = C.panel('devicePixelRatio') or 1
    return (int(w[0] + r[0] * dpr), int(w[1] + r[1] * dpr), int(w[0] + r[2] * dpr), int(w[1] + r[3] * dpr))


def rect_of(js_el):
    return C.panel("(function(){var e=" + js_el + ";if(!e)return null;var r=e.getBoundingClientRect();return [r.left,r.top,r.right,r.bottom]})()")


def inside(a, b, slack=1):
    return bool(a and b) and a[0] >= b[0] - slack and a[1] >= b[1] - slack and a[2] <= b[2] + slack and a[3] <= b[3] + slack


def on_top(h, r):
    """Every probe point of `r` (screen px) belongs to the bubble's window: nothing (page, browser, other window) over it."""
    pts = [((r[0] + r[2]) // 2, (r[1] + r[3]) // 2), (r[0] + 8, r[1] + 8), (r[2] - 8, r[3] - 8)]
    roots = []
    for x, y in pts:
        w = u32.WindowFromPoint(W.POINT(x, y)); roots.append(u32.GetAncestor(w, 2) if w else 0)   # GA_ROOT
    return all(x == h for x in roots), roots


def region(h):
    """V5.region_box, read again when the window moved between its two reads (it returns None then, not a wrong box)."""
    for _ in range(10):
        rb = V5.region_box(h) if h else None
        if rb:
            return rb
        time.sleep(0.05)
    return None


def menu_check(label, expect_rows=None):
    h = bubble(); rb = region(h); pg = CC.page_widget()
    m = rect_of("document.querySelector('.menu:not(.menu-ghost)')")
    rows = C.panel("Array.from(document.querySelectorAll('.menu:not(.menu-ghost) .mi .lb')).map(function(n){return n.textContent})") or []
    sm = to_screen(h, m) if h and m else None
    top, roots = on_top(h, sm) if sm else (False, [])
    d = {'rows': rows, 'menu': sm, 'region': rb, 'page': pg, 'roots': roots, 'bubble': h}
    ok = bool(rows) and sm and (sm[3] - sm[1]) > 40 and inside(sm, rb) and inside(sm, pg) and top and (expect_rows is None or all(r in rows for r in expect_rows))
    return step(label, ok, json.dumps(d, ensure_ascii=False)), rows


def run_main():
    before = C.fingerprints(); report['fingerprintsBefore'] = before
    OUT.mkdir(parents=True, exist_ok=True)
    flog = C.WORK / 'fake-agent.log'
    try:
        flog.unlink()
    except OSError:
        pass
    fake = C.TOOLS / 'fake-acp-agent.exe'
    job = None
    p = C.launch(extra_env={'BROWSER_CHAT_PANEL_IDLE_MS': '60000', 'BROWSER_CHAT_AGENT_IDLE_MS': '600000',
                            'BROWSER_CHAT_FAKE_AGENT': f'"{fake}" --resume --claude-like --new-delay 10000 --log "{flog}"'})
    try:
        spid = int((C.WORK / 'server.pid').read_text())
        job = job_for([p.pid, spid])
        time.sleep(1.0)
        C.shortcut('A'); step('mode on', C.wait_log(r'ANNOTATE mode on', 10, 0)); time.sleep(1.2)
        if C.invoke('AnnotationConnectLater') or C.invoke('AnnotationConnectClose'):
            time.sleep(0.6)
        V.front()
        m1 = mark(); C.invoke('AnnotationLayoutComment'); C.wait_log(r'CHAT layout: Commentaire', 5, m1); time.sleep(1.5)
        # the issues' page: « Payer 48,00 € » low in a page that cannot scroll (the end of the document)
        C.page("(function(){var d=document.documentElement,b=document.body;d.style.height='100%';d.style.overflow='hidden';b.style.height='100%';b.style.overflow='hidden';"
               "var p=document.querySelector('button.btn-primary');p.style.cssText+=';position:fixed;left:64px;bottom:40px;width:278px;height:44px;z-index:5';window.scrollTo(0,0);return 1})()")
        time.sleep(0.6)
        report['measures']['pageScrollRoom'] = C.page("(function(){var s=document.scrollingElement||document.documentElement;return s.scrollHeight-innerHeight-scrollY})()")

        # ---------------------------------------------------------------- #129: before the agent's models
        m = mark(); V5.real_click('button.btn-primary'); step('bubble opened on the pay button', C.wait_log(r'CHAT bubble opened on', 10, m)); time.sleep(1.2)
        label = C.panel("(function(){var m=document.querySelector('.model .m');var e=m.querySelector('.e');return [m.querySelector('.mn').textContent, e&&!e.hidden?e.textContent.trim():'']})()")
        report['measures']['buttonBeforeModels'] = label
        C.click_panel('button.m'); time.sleep(0.6)
        ok, rows = menu_check('#129 · before the models: the bubble\'s model menu opens with rows (#128: Claude Code\'s group names the model its agent will use, « Opus 5.5 »), whole, inside the bubble\'s region and the page area, on top of everything', ['Opus 5.5'])
        report['measures']['rowsBeforeModels'] = rows
        capture('129-menu-avant-modeles')
        C.key_panel('Escape', 'Escape', 27); time.sleep(0.4)
        step('#129 · Échap closes the menu only (the bubble stays)', not C.panel("!!document.querySelector('.menu:not(.menu-ghost)')") and bubble() and u32.IsWindowVisible(bubble()))

        # ---------------------------------------------------------------- #129: the models known, a choice made in the bubble
        time.sleep(11)   # --new-delay 10000 (counted from the bubble's opening, R13): the session's lists are there now
        C.click_panel('button.m'); time.sleep(0.6)
        ok, rows = menu_check('#129 · models known: the menu lists the models', ['Haiku 5.5'])
        C.click_panel(text='Haiku 5.5'); time.sleep(0.8)
        C.click_panel('button.m'); time.sleep(0.5); C.click_panel('.menu .mi[data-key=effort]'); time.sleep(0.6)
        C.click_panel(text='Faible'); time.sleep(0.8)
        shown = C.panel("document.querySelector('.model .m').textContent")
        step('#129 · Haiku 5.5 and « Faible » chosen in the bubble are shown on its button', 'Haiku 5.5' in (shown or '') and 'Faible' in (shown or ''), shown)

        # ---------------------------------------------------------------- #130: the bubble grows with an authorization
        m = mark(); V.send_in_view('permission'); time.sleep(0.5)
        C.wait_for(lambda: C.panel("Array.from(document.querySelectorAll('button')).some(function(b){return b.textContent.trim()==='Autoriser'})"), timeout=15)
        time.sleep(1.2)
        fl = flog.read_text(encoding='utf-8', errors='replace') if flog.exists() else ''
        i_model = fl.find('"value":"haiku"'); i_effort = fl.find('"value":"low"'); i_prompt = fl.find('session/prompt')
        step('#129 · the model and the effort chosen in the bubble reached the session (set_config_option) before the comment was sent',
             0 <= i_model < i_prompt and 0 <= i_effort < i_prompt, {'model': i_model, 'effort': i_effort, 'prompt': i_prompt})
        h = bubble(); rb = region(h); pg = CC.page_widget()
        auto = rect_of("Array.from(document.querySelectorAll('button')).find(function(b){return b.textContent.trim()==='Autoriser'})")
        refuse = rect_of("Array.from(document.querySelectorAll('button')).find(function(b){return b.textContent.trim()==='Refuser'})")
        sa = to_screen(h, auto) if h and auto else None; sr = to_screen(h, refuse) if h and refuse else None
        top_a, roots = on_top(h, sa) if sa else (False, [])
        lines = [l for l in C.log_text()[m:].splitlines() if 'bubble' in l]
        report['measures']['130-log'] = lines[:12]
        step('#130 · with its authorization, the whole bubble is in the page area', inside(rb, pg), {'region': rb, 'page': pg})
        # variant C (Paper « #130 Bulle sans place dessous · C remonte le long du côté (validée) »): next to the element (+12 px from
        # it = 8 from its selection frame), slid up along it, its bottom 8 px above the page area's bottom, never above 8 px under
        # the banner, never over the element, never flipped above it
        el = CC.element_screen('button.btn-primary'); dpr = C.evaluate(C.URL, 'devicePixelRatio') or 1
        k8, k12 = round(8 * dpr), round(12 * dpr)
        cd = {'region': rb, 'element': el, 'page': pg, 'gap': rb and rb[0] - el[2], 'bottom': rb and pg[3] - rb[3], 'top': rb and rb[1] - pg[1]}
        step('#130 C · at the element\'s right, 12 px from it (8 from its frame), not covering it, not flipped above it',
             bool(rb) and abs((rb[0] - el[2]) - k12) <= 2 and rb[0] >= el[2] and rb[3] >= el[1], cd)
        step('#130 C · slid up: its bottom 8 px above the page area\'s bottom, its top at least 8 px under the banner',
             bool(rb) and abs((pg[3] - rb[3]) - k8) <= 2 and rb[1] - pg[1] >= k8 - 2, cd)
        step('#130 C · the slide is logged (« slid up along its element »)', any('#130 C' in l for l in lines), lines[:6])
        step('#130 · Refuser and Autoriser are on screen, in the bubble, on top of everything', inside(sa, rb) and inside(sr, rb) and inside(sa, pg) and top_a,
             {'autoriser': sa, 'refuser': sr, 'roots': roots})
        report['measures']['replacedLogged'] = any('#130' in l for l in lines)
        capture('130-autorisation')
        # the model menu with the bubble placed again (above its element, or slid)
        # While an authorization waits, its card takes the dock's place (panel.js dockBubble, since 18/11): no input, no
        # model button until it is answered — the menu is checked again after « Autoriser ».
        if C.panel("!!document.querySelector('button.m')"):
            C.click_panel('button.m'); time.sleep(0.6)
            menu_check('#129 / #130 · the bubble placed again: the model menu still opens whole, on top of everything')
            capture('130-menu-modele')
            C.key_panel('Escape', 'Escape', 27); time.sleep(0.5)
        else:
            report['measures']['modelButtonDuringPermission'] = 'absent (the authorization card replaces the input)'
        # a REAL click on Autoriser (what the user does)
        sa = to_screen(h, rect_of("Array.from(document.querySelectorAll('button')).find(function(b){return b.textContent.trim()==='Autoriser'})"))
        m = mark(); time.sleep(0.8)   # SEC-23: no « Autoriser » in a card's first 600 ms
        u32.SetCursorPos((sa[0] + sa[2]) // 2, (sa[1] + sa[3]) // 2); time.sleep(0.1); V5.mouse(0x0002); time.sleep(0.03); V5.mouse(0x0004)
        step('#130 · a real click on Autoriser answers the authorization', C.wait_log(r'permission answered', 8, m))
        capture('130-apres-autoriser')
        if 'modelButtonDuringPermission' in report['measures']:
            C.wait_for(lambda: C.panel("!!document.querySelector('button.m')"), timeout=10)
            C.click_panel('button.m'); time.sleep(0.6)
            menu_check('#129 / #130 · the bubble placed again, authorization answered: the model menu opens whole, on top of everything')
            capture('130-menu-modele')
            C.key_panel('Escape', 'Escape', 27); time.sleep(0.5)
    except Exception:
        traceback.print_exc()
        step('run without exception', False, traceback.format_exc()[-600:])
    finally:
        C.quit_all()
        after = C.fingerprints(); report['fingerprintsAfter'] = after
        step('real Claude / Codex configuration files unchanged', before == after)
        ps = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq fake-acp-agent.exe', '/NH'], capture_output=True, text=True).stdout
        step('nothing left running (fake agent)', 'fake-acp-agent' not in ps)
        (OUT / 'report.json').write_text(json.dumps(report, indent=2, ensure_ascii=False, default=str), encoding='utf-8')
        if job:
            k32.CloseHandle(job)
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
