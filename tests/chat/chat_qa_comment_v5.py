#!/usr/bin/env python3
"""Mode Commentaire v5, real-window pass of the host (fake agent), frame by frame.

    CHAT_QA_BUILD=chat-cv5 python tests/chat/chat_qa_comment_v5.py   # -> build/chat-qa/comment-v5/report.json + frames

  R39   the bubble stays glued to its element at every image while the page scrolls: wheel, fast wheel, scrollbar, keyboard.
        Measured on the screen's pixels (GDI, ~8 ms): a magenta element and the bubble's dark surface, one column each;
        the gap between their edges must not change (a lag of one image shows as the scroll's step in pixels).
  R39b  the element passes under the page area's top / bottom edge: the bubble passes under it like the content (never
        drawn over the browser's bars, same offset to the last image).
  R40   the diff « en grand »: the handle (real mouse drag) widens / heightens the bubble, its anchored corner fixed, never
        beyond the page area, never below 320 × its size before; Échap gives the size of before back.
  R41   menu -> menu (model -> mode -> « + », model -> Effort): the window's region covers both menus at every image,
        no image without a menu (gap), no black image (flash).

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
import chat_qa_comment_v4b as V4

OUT = C.QA / 'comment-v5'
report = {'steps': [], 'measures': {}, 'captures': []}
u32 = ctypes.windll.user32
grab = V4.fast_grab


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
        path = C.grab(f'comment-v5/{name}')
        report['captures'].append(path.name)
    except Exception as e:
        print('capture failed', name, e)


def save_png(frame, name):
    from PIL import Image
    w, h, raw = frame
    Image.frombuffer('RGBA', (w, h), raw, 'raw', 'BGRA', 0, 1).convert('RGB').save(OUT / name)
    report['captures'].append(name)


def mouse(flags, data=0):
    i = V._IN(); i.type = 0; i.mi = V._MI(0, 0, ctypes.c_uint32(data).value, flags, 0, 0); u32.SendInput(1, ctypes.byref(i), ctypes.sizeof(V._IN))


def wheel(notches, every):
    for _ in range(abs(notches)):
        mouse(0x0800, -120 if notches > 0 else 120); time.sleep(every)


# ---------------------------------------------------------------- pixels
def px(frame, x, y):
    w, h, raw = frame
    i = (y * w + x) * 4; return raw[i + 2], raw[i + 1], raw[i]


def magenta(c):
    r, g, b = c; return r > g + 80 and b > g + 80


def dark(c):
    return sum(c) < 200


def black(c):
    return sum(c) < 12


def run_of(frame, x, test, y0=0, y1=None):
    """First and last row (in [y0, y1)) whose pixel at column x passes `test`, or None."""
    w, h, _ = frame; y1 = h if y1 is None else y1
    rows = [y for y in range(y0, y1) if test(px(frame, x, y))]
    if not rows:
        return None
    runs = [[rows[0], rows[0]]]   # the longest continuous run (gaps of a few rows: text inside the surface)
    for y in rows[1:]:
        if y - runs[-1][1] <= 8: runs[-1][1] = y
        else: runs.append([y, y])
    best = max(runs, key=lambda r: r[1] - r[0])
    return tuple(best) if best[1] - best[0] >= 16 else None


class Strips(threading.Thread):
    """Two thin columns of the screen (the element's, the bubble's), grabbed together every ~6 ms."""
    def __init__(self, xs, top, bottom, limit=400):
        super().__init__(daemon=True); self.xs = xs; self.top = top; self.bottom = bottom; self.frames = []; self.stop = False; self.limit = limit
    def run(self):
        while not self.stop and len(self.frames) < self.limit:
            t = time.perf_counter()
            self.frames.append((t, [grab((x - 1, self.top, x + 2, self.bottom)) for x in self.xs]))
            time.sleep(max(0.0, 0.006 - (time.perf_counter() - t)))


class Frames(threading.Thread):
    def __init__(self, region, limit=80, every=0.008):
        super().__init__(daemon=True); self.region = region; self.frames = []; self.stop = False; self.limit = limit; self.every = every
    def run(self):
        while not self.stop and len(self.frames) < self.limit:
            t = time.perf_counter(); self.frames.append((t, grab(self.region)))
            time.sleep(max(0.0, self.every - (time.perf_counter() - t)))


def bubble_surface():
    """The bubble's surface on screen (#app's box in its window), or None."""
    b = V.bubble_hwnd()
    if not b:
        return None
    r = C.panel("(function(){var a=document.getElementById('app')||document.body;var r=a.getBoundingClientRect();return [r.left,r.top,r.right,r.bottom,devicePixelRatio]})()")
    wr = b[0][3]; k = r[4]
    return [int(wr[0] + r[0] * k), int(wr[1] + r[1] * k), int(wr[0] + r[2] * k), int(wr[1] + r[3] * k)]


def region_box(hwnd):
    """The window's region on screen; None when empty or when the window moved during the reading (two reads disagree)."""
    w0 = V3.win_rect(hwnd)
    rg = W.RECT(); kind = u32.GetWindowRgnBox(hwnd, ctypes.byref(rg))
    wr = V3.win_rect(hwnd)
    if kind == 1 or wr != w0:
        return None
    return (wr[0] + rg.left, wr[1] + rg.top, wr[0] + rg.right, wr[1] + rg.bottom)


# ---------------------------------------------------------------- R39 / R39b
def follow(label, act, edge="bottom"):
    """Scroll with `act`; per image, the gap between the element's edge and the bubble's (pixels) must not change."""
    V.front(); time.sleep(0.15)
    pg = CC.page_widget(); el = CC.element_screen('#qa-mag'); sf = bubble_surface(); bh = V.bubble_hwnd()
    if not sf or not bh:
        return step(f'R39 · {label}: the bubble is open', False)
    xm = (el[0] + el[2]) // 2; xb = (sf[0] + sf[2]) // 2
    top, bottom = pg[1], pg[3]
    s = Strips([xm, xb], top, bottom); regions = V3.Sampler(lambda: region_box(bh[0][0])); s.start(); regions.start(); time.sleep(0.12)
    m0 = mark(); act(); time.sleep(0.6); s.stop = True; regions.stop = True; s.join(); regions.join()
    y0, y1 = 0, pg[3] - pg[1]
    # R39b: the window's region never goes over the browser's bars (the page area's edges)
    t0 = regions.samples[0][0] if regions.samples else 0
    over = [(round((t - t0) * 1000), r) for t, r in regions.samples if isinstance(r, tuple) and len(r) == 4 and (r[1] < pg[1] - 1 or r[3] > pg[3] + 1)]
    bars = len(over)
    rows = []; blacks = 0
    for t, (fm, fb) in s.frames:
        rm = run_of(fm, 1, magenta, y0, y1); rb = run_of(fb, 1, dark, y0, y1)
        if rb and any(black(px(fb, 1, y)) for y in range(rb[0], rb[1] + 1)): blacks += 1
        if not rm or not rb:
            rows.append((t, None)); continue
        a, b = (rm[1], rb[1]) if edge == 'bottom' else (rm[0], rb[0])
        clipped = a <= y0 + 1 or a >= y1 - 2 or b <= y0 + 1 or b >= y1 - 2
        rows.append((t, None if clipped else (a, b - a)))
    seen = [r for _, r in rows if r]
    if len(seen) < 3:
        f0 = s.frames[0][1] if s.frames else None
        diag = {'frames': len(s.frames), 'seen': len(seen), 'element': el, 'surface': sf, 'page': pg,
                'firstMagenta': run_of(f0[0], 1, magenta, y0, y1) if f0 else None, 'firstDark': run_of(f0[1], 1, dark, y0, y1) if f0 else None}
        if f0:
            from PIL import Image
            for i, fr in enumerate(f0):
                w_, h_, raw = fr; Image.frombuffer('RGBA', (w_, h_), raw, 'raw', 'BGRA', 0, 1).convert('RGB').resize((30, h_)).save(OUT / f'r39-{label}-colonne-{i}.png')
        return step(f'R39 · {label}: measurable (element and bubble seen together)', False, diag)
    base = seen[0][1]
    devs = [g - base for _, g in seen]
    moves = [abs(b[0] - a[0]) for a, b in zip(seen, seen[1:]) if b[0] != a[0]]
    step_px = round(sorted(moves)[len(moves) // 2], 1) if moves else 0
    bad = [d for d in devs if abs(d) > 2]
    lag = [l for l in C.log_text()[m0:].splitlines() if 'anchor: the page' in l]
    m = {'frames': len(s.frames), 'measured': len(seen), 'scrolled_px': (seen[0][0] - seen[-1][0]), 'medianStepPx': step_px,
         'maxGapChangePx': max((abs(d) for d in devs), default=0), 'imagesOff': len(bad), 'offValues': bad[:12],
         'barChanged': bars, 'overBars': over[:6], 'page': pg, 'blackImages': blacks, 'hostLatency': lag[-2:]}
    report['measures']['r39-' + label] = m
    moved = abs(m['scrolled_px']) > 20
    step(f'R39 · {label}: the bubble follows its element at every image (gap constant ±2 px, {len(seen)} images)', moved and not bad, m)
    step(f'R39b · {label}: never drawn over the browser\'s bar, no black image', bars == 0 and blacks == 0, {'barChanged': bars, 'blackImages': blacks})
    return not bad


def edge_frames(label, act, near):
    """A few full images of the bubble passing under an edge of the page area (proof for R39b)."""
    sf = bubble_surface(); pg = CC.page_widget()
    if not sf:
        return
    y = pg[1] if near == 'top' else pg[3]
    region = (max(0, sf[0] - 30), max(0, y - 60), sf[2] + 30, y + 60)
    f = Frames(region, 60, 0.012); f.start(); time.sleep(0.05); act(); time.sleep(0.5); f.stop = True; f.join()
    for i, (_, fr) in enumerate(f.frames[:: max(1, len(f.frames) // 8)][:8]):
        save_png(fr, f'r39b-{label}-{i:02d}.png')


# ---------------------------------------------------------------- R41
def morph(label, open_first, then):
    bw = V.bubble_hwnd()
    if not bw:
        return step(f'R41 · {label}: the bubble is open', False)
    h = bw[0][0]
    V.front(); time.sleep(0.15)
    open_first(); time.sleep(0.5)
    before = C.panel("(function(){var m=document.querySelector('.menu:not(.menu-ghost)');if(!m)return null;var r=m.getBoundingClientRect();return [r.left,r.top,r.right,r.bottom,devicePixelRatio]})()")
    wr = V3.win_rect(h)
    if not before:
        return step(f'R41 · {label}: the first menu is open', False)
    k = before[4]; scr = lambda r: (int(wr[0] + r[0] * k), int(wr[1] + r[1] * k), int(wr[0] + r[2] * k), int(wr[1] + r[3] * k))
    old = scr(before)
    big = (max(0, old[0] - 160), max(0, old[1] - 160), old[2] + 160, old[3] + 160)
    f = Frames(big, 90, 0.005)
    regions = V3.Sampler(lambda: region_box(h)); regions.start(); f.start(); time.sleep(0.04)
    t_click = time.perf_counter(); then(); time.sleep(0.45); f.stop = True; regions.stop = True; f.join(); regions.join()
    after = C.panel("(function(){var m=document.querySelector('.menu:not(.menu-ghost)');if(!m)return null;var r=m.getBoundingClientRect();return [r.left,r.top,r.right,r.bottom]})()")
    if not after:
        return step(f'R41 · {label}: the second menu is open', False)
    wr2 = V3.win_rect(h); new = (int(wr2[0] + after[0] * k), int(wr2[1] + after[1] * k), int(wr2[0] + after[2] * k), int(wr2[1] + after[3] * k))
    union = (min(old[0], new[0]), min(old[1], new[1]), max(old[2], new[2]), max(old[3], new[3]))
    # every region sample after the click covers what is on screen of the two menus (the window's own origin may move)
    # the region once the page has asked for the new menu (CDP click -> page -> host: allow 60 ms): it covers the new box
    rs = [(t, r) for t, r in regions.samples if isinstance(r, tuple) and r and len(r) == 4]
    late = [r for t, r in rs if t > t_click + 0.06]
    short = [r for r in late if not (r[0] <= new[0] + 1 and r[1] <= new[1] + 1 and r[2] >= new[2] - 1 and r[3] >= new[3] - 1)]
    # every image: the box the two menus share is drawn (dark surface) — an image where it is not is a gap; black = a flash
    inter = (max(old[0], new[0]), max(old[1], new[1]), min(old[2], new[2]), min(old[3], new[3]))
    gaps = blacks = 0; shares = []
    for _, fr in f.frames:
        n = d = b = 0
        for y in range(inter[1] - big[1] + 4, inter[3] - big[1] - 4, 4):
            for x in range(inter[0] - big[0] + 4, inter[2] - big[0] - 4, 4):
                c = px(fr, x, y); n += 1; d += dark(c); b += black(c)
        sh = round(d / max(1, n), 2); shares.append(sh)
        if sh < 0.5: gaps += 1
        if b / max(1, n) > 0.2: blacks += 1
    timeline = []
    for t, r in rs:
        if not timeline or timeline[-1][1] != r: timeline.append((round((t - t_click) * 1000), r))
    m = {'old': old, 'new': new, 'union': union, 'regionSamplesAfterClick': len(late), 'regionShort': len(short), 'images': len(f.frames),
         'regionTimelineMs': timeline[:12], 'imageMs': [round((t - t_click) * 1000) for t, _ in f.frames][:60],
         'drawnShare': shares[:60], 'emptyImages': gaps, 'blackImages': blacks}
    report['measures']['r41-' + label] = m
    for i, (_, fr) in enumerate(f.frames):
        save_png(fr, f'r41-{label}-{i:02d}.png')
    return step(f'R41 · {label}: the region covers the menus at every image, no empty or black image', not short and gaps == 0 and blacks == 0, m)


def close_menus():
    """Escape only while a menu is open (an Escape with none would close the bubble)."""
    for _ in range(3):
        try:
            if not C.panel("!!document.querySelector('.menu:not(.menu-ghost)')"):
                break
            C.key_panel('Escape', 'Escape', 27)
        except Exception:
            break
        time.sleep(0.3)


# ---------------------------------------------------------------- R40
def drag(x0, y0, x1, y1, steps=24, every=0.016):
    V.front(); u32.SetCursorPos(x0, y0); time.sleep(0.1); mouse(0x0002); time.sleep(0.05)
    for i in range(1, steps + 1):
        u32.SetCursorPos(int(x0 + (x1 - x0) * i / steps), int(y0 + (y1 - y0) * i / steps)); time.sleep(every)
    time.sleep(0.05); mouse(0x0004); time.sleep(0.4)


def grip_point():
    b = V.bubble_hwnd()
    r = C.panel("(function(){var g=document.querySelector('.b-grip');if(!g||g.hidden)return null;var r=g.getBoundingClientRect();return [r.left+r.width/2,r.top+r.height/2,devicePixelRatio]})()")
    if not b or not r:
        return None
    wr = b[0][3]; return (int(wr[0] + r[0] * r[2]), int(wr[1] + r[1] * r[2]))


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
                        'BROWSER_CHAT_FAKE_AGENT': f'"{fake}" --resume --claude-like --log "{flog}" --edit "{V.GAP_EDIT}"',
                        **({'BROWSER_CHAT_FOLLOW_FRAMES': __import__('os').environ['BROWSER_CHAT_FOLLOW_FRAMES']} if 'BROWSER_CHAT_FOLLOW_FRAMES' in __import__('os').environ else {})})
    try:
        time.sleep(1.0)
        C.shortcut('A'); step('mode on', logged(r'ANNOTATE mode on', 0, 10)); time.sleep(1.2)
        if C.invoke('AnnotationConnectLater') or C.invoke('AnnotationConnectClose'):
            time.sleep(0.6)
        V.front()
        m1 = mark(); C.invoke('AnnotationLayoutComment'); logged(r'CHAT layout: Commentaire', m1, 5); time.sleep(1.5)

        # a long white page with one magenta element (its edges are found on the screen's pixels)
        C.page("(function(){document.documentElement.style.background='#fff';document.body.style.background='#fff';"
               "var d=document.createElement('div');d.id='qa-mag';d.style.cssText='position:absolute;left:40px;top:320px;width:220px;height:90px;background:#ff00ff;z-index:5';"
               "document.body.appendChild(d);var s=document.createElement('div');s.id='qa-long';s.style.cssText='position:absolute;left:0;top:0;width:1px;height:5200px';"
               "document.body.appendChild(s);window.scrollTo(0,0);return 1})()")
        time.sleep(0.6)
        m2 = mark(); C.click_page('#qa-mag'); step('the bubble opens on the element', logged(r'CHAT bubble opened on', m2, 10)); logged(r'CHAT panel ready', 0, 5); time.sleep(1.2)
        capture('r39-depart')
        V.front(); time.sleep(0.3)
        mw = V3.win_rect(Q.main_window()); bh = V.bubble_hwnd()
        geo = {'main': mw, 'page': CC.page_widget(), 'element': CC.element_screen('#qa-mag'), 'bubbleWindow': bh[0][3] if bh else None,
               'bubbleRegion': region_box(bh[0][0]) if bh else None, 'surface': bubble_surface(), 'dpr': C.page('devicePixelRatio'),
               'scroll': C.page('[scrollX,scrollY,innerWidth,innerHeight,document.scrollingElement.scrollHeight]')}
        report['measures']['geometry'] = geo; print('geometry', geo, flush=True)
        save_png(grab(mw), 'r39-depart-ecran.png')
        if __import__('os').environ.get('V5_PROBE'):
            raise SystemExit(0)
        pg = CC.page_widget(); park = (pg[0] + 12, (pg[1] + pg[3]) // 2)   # the pointer over the page, away from the bubble

        def reset(y):
            C.page(f"(function(){{window.scrollTo(0,{y});return 1}})()"); time.sleep(0.7)

        # ---- R39: wheel (smooth), fast wheel, scrollbar, keyboard
        reset(0); V.front(); u32.SetCursorPos(*park); time.sleep(0.2)
        follow('molette', lambda: wheel(3, 0.12))
        reset(0); u32.SetCursorPos(*park); time.sleep(0.2)
        follow('molette-rapide', lambda: wheel(8, 0.012))
        reset(0)
        def scrollbar():
            x = pg[2] - 7; drag(x, pg[1] + 40, x, pg[1] + 150, steps=14, every=0.016)
        follow('barre-de-defilement', scrollbar)
        reset(0)
        reset(0)
        def keyboard():
            # final QA: the page's right edge has no real scrollbar here (a click there selects an element): the page gets the
            # keyboard by a Ctrl+click on a plain place of the page (R31: Ctrl held = the site as usual, nothing selected)
            V4.key(0x11); u32.SetCursorPos(pg[0] + 400, pg[1] + 40); time.sleep(0.1); mouse(0x0002); time.sleep(0.03); mouse(0x0004); time.sleep(0.1); V4.key(0x11, up=True); time.sleep(0.15)
            u32.SetCursorPos(*park); time.sleep(0.1)
            for vk in (0x28, 0x28, 0x28, 0x22):   # Down x3, Page Down — real keys
                V4.key(vk); time.sleep(0.03); V4.key(vk, up=True); time.sleep(0.15)
        follow('clavier', keyboard)

        # ---- R39b: under the top edge (the element starts just below it), then under the bottom edge
        dpr = C.page('devicePixelRatio'); vh = C.page('innerHeight')
        reset(320 - 24); u32.SetCursorPos(*park); time.sleep(0.2)
        follow('bord-haut', lambda: wheel(2, 0.2), edge='bottom')
        reset(320 - 24); u32.SetCursorPos(*park); time.sleep(0.2)
        edge_frames('bord-haut', lambda: wheel(2, 0.2), 'top')
        # the element lower in the document (the bubble follows it), its top 40 px above the page area's bottom
        reset(1500 - 300); C.page("(function(){document.getElementById('qa-mag').style.top='1500px';return 1})()"); time.sleep(0.6)
        reset(int(1500 - vh + 60)); u32.SetCursorPos(*park); time.sleep(0.2)
        follow('bord-bas', lambda: wheel(-2, 0.2), edge='top')
        reset(int(1500 - vh + 60)); u32.SetCursorPos(*park); time.sleep(0.2)
        edge_frames('bord-bas', lambda: wheel(-2, 0.2), 'bottom')
        reset(1500 - 300); C.page("(function(){document.getElementById('qa-mag').style.top='320px';return 1})()"); time.sleep(0.4)

        if __import__('os').environ.get('V5_ONLY') == 'r39':
            raise SystemExit(0)
        # ---- R41: menu -> menu (the bubble back at its place)
        reset(0); time.sleep(0.4)
        morph('modele-vers-mode', lambda: C.click_panel('button.m'), lambda: C.click_panel('button.tb.mode'))
        morph('mode-vers-modele', lambda: None, lambda: C.click_panel('button.m'))   # « + » in the bubble is the file picker itself (R22): no menu
        close_menus()
        morph('modele-vers-effort', lambda: C.click_panel('button.m'), lambda: C.click_panel(text='Effort'))
        close_menus()

        # ---- R40: the diff « en grand », its handle
        m3 = mark(); V.send_in_view('Change le gap en 8 px')
        for _ in range(16):
            try:
                C.click_panel(text='Autoriser')
            except Exception:
                pass
            if logged(r'CHAT permission answered', m3, 1):
                break
        logged(r'CHAT turn t\d+ ended', m3, 30); time.sleep(1.0)
        s0 = bubble_surface()
        C.click_panel('.bt-bar-main'); time.sleep(0.8)
        s1 = bubble_surface(); g = grip_point()
        step('R40 · the diff opens keeping the bubble\'s size, the handle shown', s0 and s1 and g and abs((s1[2] - s1[0]) - (s0[2] - s0[0])) <= 2, {'before': s0, 'view': s1, 'grip': g})
        capture('r40-diff-ouvert')
        if g:
            bw = V.bubble_hwnd()[0][0]
            reg = V3.Sampler(lambda: region_box(bw)); reg.start()
            drag(g[0], g[1], g[0] + 180, g[1] + 120, steps=30, every=0.016)
            reg.stop = True; reg.join()
            s2 = bubble_surface(); pg = CC.page_widget()
            boxes = [r for _, r in reg.samples if isinstance(r, tuple) and len(r) == 4]
            sizes = sorted(set((r[2] - r[0], r[3] - r[1]) for r in boxes)); corner = set((r[0], r[1]) for r in boxes)
            m = {'before': s1, 'after': s2, 'distinctSizes': len(sizes), 'anchoredCorners': sorted(corner)[:6]}
            report['measures']['r40-drag'] = m
            step('R40 · dragging the handle widens and heightens the bubble, many steps (fluid), its corner fixed',
                 s2 and s2[2] - s2[0] > (s1[2] - s1[0]) + 100 and s2[3] - s2[1] > (s1[3] - s1[1]) + 60 and abs(s2[0] - s1[0]) <= 1 and abs(s2[1] - s1[1]) <= 1 and len(sizes) >= 8, m)
            capture('r40-agrandi')
            g2 = grip_point()
            drag(g2[0], g2[1], pg[2] + 400, pg[3] + 400, steps=20, every=0.016); s3 = bubble_surface()
            step('R40 · never beyond the page area', s3 and s3[2] <= pg[2] and s3[3] <= pg[3], {'surface': s3, 'page': pg})
            C.key_panel('Escape', 'Escape', 27); time.sleep(0.8); s3b = bubble_surface()   # closed at its largest
            step('R40 · Échap at the largest size gives the size of before back', s3b and abs((s3b[2] - s3b[0]) - (s0[2] - s0[0])) <= 2 and abs((s3b[3] - s3b[1]) - (s0[3] - s0[1])) <= 2,
                 {'largest': s3, 'closed': s3b, 'before': s0})
            C.click_panel('.bt-bar-main'); time.sleep(0.8); s1 = bubble_surface()
            g3 = grip_point(); drag(g3[0], g3[1], g3[0] + 200, g3[1] + 100, steps=12, every=0.016)
            g3 = grip_point(); drag(g3[0], g3[1], s1[0] + 20, s1[1] + 20, steps=20, every=0.016); s4 = bubble_surface()
            step('R40 · never smaller than 320 × the size before the diff', s4 and s4[2] - s4[0] >= (s1[2] - s1[0]) - 1 and s4[3] - s4[1] >= (s1[3] - s1[1]) - 1, {'surface': s4, 'view': s1})
            g4 = grip_point(); drag(g4[0], g4[1], g4[0] + 120, g4[1] + 80, steps=12, every=0.016); s5 = bubble_surface()
            C.key_panel('Escape', 'Escape', 27); time.sleep(0.8); s6 = bubble_surface()
            step('R40 · Échap gives the size of before back', s6 and abs((s6[2] - s6[0]) - (s0[2] - s0[0])) <= 2 and abs((s6[3] - s6[1]) - (s0[3] - s0[1])) <= 2,
                 {'resized': s5, 'closed': s6, 'before': s0})
            capture('r40-ferme')
    except Exception:
        traceback.print_exc()
        step('run without exception', False, traceback.format_exc()[-400:])
    finally:
        try:
            mouse(0x0004)
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
