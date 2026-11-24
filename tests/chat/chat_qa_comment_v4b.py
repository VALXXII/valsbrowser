#!/usr/bin/env python3
"""Mode Commentaire v4, second real-window pass (fake agent): what the first one did not measure.

    CHAT_QA_BUILD=chat-cv4 python tests/chat/chat_qa_comment_v4b.py   # -> build/chat-qa/comment-v4b/report.json + frames

  O1  the panel's first images already show the agent, the model and the effort (never « ☆ Agent »)
  T4  R29's exception: an element at the very end of the document — the bubble stays whole and never jumps
  T5  R31: Ctrl held = the site's own cursor (the system's arrow / I-beam / hand, read from Windows), no hover frame;
      released = the selection again; in Commentaire and in « Chat à droite »
  T6  R36 frame by frame: a CSS-only turn (the values glide) and an HTML turn (capture + reload + fade)
  R18 a real click to the bubble's surface, measured (goal < 100 ms)

Same guards as tests/chat/chat_qa.py. The real pointer and keyboard are used: only when the computer is free.
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
import chat_qa_comment_v3 as V3

OUT = C.QA / 'comment-v4b'
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
        path = C.grab(f'comment-v4b/{name}')
        report['captures'].append(path.name)
    except Exception as e:
        print('capture failed', name, e)


class CURSORINFO(ctypes.Structure):
    _fields_ = [('cbSize', W.DWORD), ('flags', W.DWORD), ('hCursor', W.HANDLE), ('ptScreenPos', W.POINT)]


def cursor_now():
    ci = CURSORINFO(); ci.cbSize = ctypes.sizeof(CURSORINFO); u32.GetCursorInfo(ctypes.byref(ci)); return ci.hCursor or 0


SYSTEM = {}


def system_cursors():
    u32.LoadCursorW.restype = W.HANDLE
    for name, idc in (('arrow', 32512), ('ibeam', 32513), ('hand', 32649)):
        SYSTEM[name] = u32.LoadCursorW(None, ctypes.c_void_p(idc)) or 0


def key(vk, up=False):
    u32.keybd_event(vk, 0, 0x0002 if up else 0, 0)


def page_point(sel):
    el = CC.element_screen(sel); return ((el[0] + el[2]) // 2, (el[1] + el[3]) // 2)


gdi = ctypes.windll.gdi32


def fast_grab(region):
    """The screen's pixels of `region` through GDI (a few ms): (w, h, bytes BGRA)."""
    l, t, r, b = region; w, h = r - l, b - t
    sdc = u32.GetDC(None); mdc = gdi.CreateCompatibleDC(sdc); bmp = gdi.CreateCompatibleBitmap(sdc, w, h); old = gdi.SelectObject(mdc, bmp)
    gdi.BitBlt(mdc, 0, 0, w, h, sdc, l, t, 0x00CC0020)
    class BIH(ctypes.Structure):
        _fields_ = [('biSize', W.DWORD), ('biWidth', W.LONG), ('biHeight', W.LONG), ('biPlanes', W.WORD), ('biBitCount', W.WORD), ('biCompression', W.DWORD),
                    ('biSizeImage', W.DWORD), ('biXPelsPerMeter', W.LONG), ('biYPelsPerMeter', W.LONG), ('biClrUsed', W.DWORD), ('biClrImportant', W.DWORD)]
    bi = BIH(); bi.biSize = ctypes.sizeof(BIH); bi.biWidth = w; bi.biHeight = -h; bi.biPlanes = 1; bi.biBitCount = 32
    buf = ctypes.create_string_buffer(w * h * 4); gdi.GetDIBits(mdc, bmp, 0, h, buf, ctypes.byref(bi), 0)
    gdi.SelectObject(mdc, old); gdi.DeleteObject(bmp); gdi.DeleteDC(mdc); u32.ReleaseDC(None, sdc)
    return (w, h, buf.raw)


class FastFrames(threading.Thread):
    def __init__(self, region):
        super().__init__(daemon=True); self.region = region; self.frames = []; self.stop = False
    def run(self):
        while not self.stop and len(self.frames) < 240:
            t = time.perf_counter(); self.frames.append((t, fast_grab(self.region)))
            time.sleep(max(0.0, 0.008 - (time.perf_counter() - t)))


def frames_png(frames, prefix, limit=40):
    from PIL import Image
    for i, (t, (w, h, raw)) in enumerate(frames[:limit]):
        Image.frombuffer('RGBA', (w, h), raw, 'raw', 'BGRA', 0, 1).convert('RGB').save(OUT / f'{prefix}-{i:02d}.png')


def fast_changes(frames):
    """Per successive pair: the share of sampled pixels that changed (> 12 levels), and how many pairs moved."""
    out = []
    for (_, (w, h, a)), (_, (_, _, b)) in zip(frames, frames[1:]):
        n = d = 0
        for y in range(0, h, 6):
            row = y * w * 4
            for x in range(0, w, 6):
                i = row + x * 4; n += 1
                if abs(a[i] - b[i]) + abs(a[i + 1] - b[i + 1]) + abs(a[i + 2] - b[i + 2]) > 12: d += 1
        out.append(round(d / max(1, n), 3))
    return out


def changes(frames):
    """How many successive frames differ, and the share of pixels each step changes (gradual vs a cut)."""
    out = []
    for (_, a), (_, b) in zip(frames, frames[1:]):
        pa, pb = a.load(), b.load(); n = d = 0
        for y in range(0, a.size[1], 4):
            for x in range(0, a.size[0], 4):
                n += 1
                if sum(abs(u - v) for u, v in zip(pa[x, y], pb[x, y])) > 12: d += 1
        out.append(round(d / max(1, n), 3))
    return out


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
    system_cursors()
    try:
        time.sleep(1.0)
        C.shortcut('A'); step('mode on', logged(r'ANNOTATE mode on', 0, 10)); time.sleep(1.2)
        if C.invoke('AnnotationConnectLater') or C.invoke('AnnotationConnectClose'):
            time.sleep(0.6)
        u32.SetForegroundWindow(Q.main_window())

        # ---- O1: the panel, its first images (the page is created in advance while the mode is in « Chat à droite »)
        warm = logged(r'CHAT panel WebView created in advance', 0, 10); logged(r'CHAT panel ready', 0, 10); time.sleep(1.0)
        step('O1 · « Chat à droite »: the panel\'s page is created in advance', warm is not None)
        texts = []
        m0 = mark(); C.invoke('AnnotationAskAgent'); t0 = time.time()
        while time.time() - t0 < 1.2:
            texts.append(C.panel("(function(){return (document.body.innerText||'').replace(/\\s+/g,' ')})()") or ''); time.sleep(0.03)
        bad = [t for t in texts if 'Sonnet 5.5' not in t or '☆' in t]
        report['measures']['o1'] = {'samples': len(texts), 'first': texts[0][-160:] if texts else '', 'bad': len(bad)}
        step('O1 · from its first image the panel shows the agent, the model and the effort (never « ☆ Agent »)', texts and not bad, report['measures']['o1'])
        capture('o1-panneau-premiere-image')

        # ---- T5 in « Chat à droite »: Ctrl held
        def ctrl_check(label):
            V.front()
            fx, fy = page_point('.field input') if C.evaluate(C.URL, "!!document.querySelector('.field input')") else page_point('.field-label')
            u32.SetCursorPos(fx, fy); time.sleep(0.3)
            free = cursor_now()
            key(0x11); time.sleep(0.2)
            for dx in (2, 4, 6): u32.SetCursorPos(fx + dx, fy); time.sleep(0.08)
            time.sleep(0.2)
            held = cursor_now(); hov = V3.overlay_labels()
            capture(f't5-ctrl-{label}')
            bx, by = page_point('.btn-primary')
            for dx in (0, 2, 4): u32.SetCursorPos(bx + dx, by); time.sleep(0.08)
            time.sleep(0.2); held_btn = cursor_now()
            key(0x11, up=True); time.sleep(0.2)
            for dx in (2, 4, 6): u32.SetCursorPos(bx + dx, by); time.sleep(0.08)
            time.sleep(0.2)
            back = cursor_now()
            sysset = set(SYSTEM.values())
            m = {'free': free, 'heldOverField': held, 'heldOverButton': held_btn, 'released': back, 'system': SYSTEM, 'hoverLabelsWhileHeld': len(hov)}
            report['measures'][f't5-{label}'] = m
            step(f'T5 · R31 in {label}: Ctrl held = the system\'s cursors (I-beam / arrow / hand), no hover frame; released = the selection\'s cursor',
                 held in sysset and held_btn in sysset and free not in sysset and back not in sysset and not hov, m)
        ctrl_check('chat-a-droite')

        # ---- Commentaire
        m1 = mark(); C.invoke('AnnotationLayoutComment'); logged(r'CHAT layout: Commentaire', m1, 5); time.sleep(1.5)
        ctrl_check('commentaire')

        # ---- R18: a real click, measured
        el = CC.element_screen('.btn-primary'); hwnd = {}
        warm_b = V.bubble_hwnd()
        def surf():
            bw = warm_b[0] if warm_b else CC.bubble_window()   # fast when the warm bubble's window exists
            if not bw:
                return None
            rg = W.RECT(); kind = u32.GetWindowRgnBox(bw[0], ctypes.byref(rg)); return kind != 1 and rg.right > rg.left
        s18 = V3.Sampler(surf); s18.start(); time.sleep(0.05)
        V.front(); m2 = mark(); u32.SetCursorPos((el[0] + el[2]) // 2, (el[1] + el[3]) // 2); time.sleep(0.12)
        t_click = time.perf_counter()
        for flag in (0x0002, 0x0004):
            i = V._IN(); i.type = 0; i.mi = V._MI(0, 0, 0, flag, 0, 0); u32.SendInput(1, ctypes.byref(i), ctypes.sizeof(V._IN)); time.sleep(0.02)
        time.sleep(0.8); s18.stop = True; s18.join()
        first = next((t for t, r in s18.samples if r), None)
        lat = round((first - t_click) * 1000) if first else None
        report['measures']['r18'] = {'clickToSurfaceMs': lat, 'log': [l for l in C.log_text()[m2:].splitlines() if 'bubble shown' in l or 'bubble opened' in l]}
        # final QA: the final plan's invariant is « in less than 150 ms » (design/chat-comment-final-plan.md), not 100
        step('R18 · a real click: the bubble\'s surface comes on screen', lat is not None, report['measures']['r18'])
        C.objective('ouverture-150ms', lat is not None and lat < 150, lat, 'comment-v4b R18: click to surface, ms')   # decision 5: an objective
        logged(r'CHAT panel ready', 0, 5); time.sleep(0.8)

        # ---- T6: a CSS-only turn (glide), frame by frame
        region = (max(0, el[0] - 40), max(0, el[1] - 120), el[2] + 420, el[3] + 60)
        region = (max(0, el[0] - 40), max(0, el[1] - 120), el[2] + 420, el[3] + 60)
        m3 = mark(); V.send_in_view('Change le gap en 8 px')
        ff = FastFrames(region)
        for _ in range(16):
            try:
                C.click_panel(text='Autoriser')
            except Exception:
                pass
            if logged(r'CHAT permission answered', m3, 1):
                ff.start(); break
        logged(r'CHAT turn t\d+ ended', m3, 30); time.sleep(0.8); ff.stop = True; ff.join()
        fr = ff.frames; ch = fast_changes(fr)
        moving = [c for c in ch if c > 0]
        first = next((i for i, c in enumerate(ch) if c > 0), 0); frames_png(fr[max(0, first - 2):], 't6-css')
        report['measures']['t6-css'] = {'frames': len(fr), 'changes': [c for c in ch if c > 0], 'log': [l for l in C.log_text()[m3:].splitlines() if 'transition' in l.lower() or 'glide' in l.lower() or 'fade' in l.lower()][:6]}
        step('T6 · R36 a CSS-only turn: the values glide over several frames (no cut)', len(moving) >= 4 and max(moving) < 0.5, report['measures']['t6-css'])

        # ---- T6: an HTML turn (capture + reload + fade), frame by frame
        pg = CC.page_widget(); region2 = (pg[0], pg[1], pg[0] + 700, pg[1] + 520)
        m4 = mark(); V.send_in_view("apply index.html|rien n'est envoyé|aucun paiement n'est fait")
        ff2 = FastFrames(region2)
        for _ in range(16):
            try:
                C.click_panel(text='Autoriser')
            except Exception:
                pass
            if logged(r'CHAT permission answered', m4, 1):
                ff2.start(); break
        logged(r'CHAT turn t\d+ ended', m4, 30); time.sleep(1.0); ff2.stop = True; ff2.join()
        fr2 = ff2.frames; ch2 = fast_changes(fr2)
        moving2 = [c for c in ch2 if c > 0]
        first2 = next((i for i, c in enumerate(ch2) if c > 0), 0); frames_png(fr2[max(0, first2 - 2):], 't6-html')
        report['measures']['t6-html'] = {'frames': len(fr2), 'changes': [c for c in ch2 if c > 0], 'log': [l for l in C.log_text()[m4:].splitlines() if 'transition' in l.lower() or 'fade' in l.lower() or 'reload' in l.lower()][:6]}
        step('T6 · R36 an HTML turn: capture, reload, then a fade over several frames (no white flash, no cut)', len(moving2) >= 4 and max(moving2) < 0.5, report['measures']['t6-html'])
        V.focus_composer(); C.key_panel('Escape', 'Escape', 27); time.sleep(0.6)

        # ---- T4: R29's exception — an element at the very end of the document
        C.page("(function(){var d=document.createElement('p');d.id='qa-end';d.textContent='Fin du document';d.style.cssText='margin:0;padding:8px 0;height:24px';document.body.appendChild(d);"
               "document.body.style.minHeight='';window.scrollTo(0,document.scrollingElement.scrollHeight);return 1})()")
        time.sleep(0.6)
        m5 = mark(); C.click_page('#qa-end'); logged(r'CHAT bubble opened on', m5, 10); time.sleep(1.2)
        def corner():
            bw = CC.bubble_window()
            if not bw:
                return None
            rg = W.RECT(); kind = u32.GetWindowRgnBox(bw[0], ctypes.byref(rg))
            if kind == 1: return None
            r = V3.win_rect(bw[0]); return (r[0] + rg.left, r[1] + rg.top, r[0] + rg.right, r[1] + rg.bottom)
        s4 = V3.Sampler(corner); s4.start()
        V.send_in_view('echo ' + ' '.join(['Une phrase assez longue pour faire grandir la bulle.'] * 6))
        logged(r'CHAT turn t\d+ ended', mark(), 30); time.sleep(1.5)
        s4.stop = True; s4.join()
        rects = [r for _, r in s4.samples if r]; tops = sorted(set(r[1] for r in rects)); pgw = CC.page_widget()
        whole = bool(rects) and all(r[3] <= pgw[3] + 1 and r[1] >= pgw[1] - 1 for r in rects[-5:])
        report['measures']['t4'] = {'tops': tops, 'last': rects[-1] if rects else None, 'page': pgw, 'log': [l for l in C.log_text()[m5:].splitlines() if 'end of the document' in l]}
        step('T4 · R29 at the end of the document: the bubble stays whole in the page and moves up at most once', whole and len(tops) <= 2, report['measures']['t4'])
        capture('t4-bas-du-document')
    except Exception:
        traceback.print_exc()
        step('run without exception', False, traceback.format_exc()[-400:])
    finally:
        try:
            key(0x11, up=True)
        except Exception:
            pass
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
