#!/usr/bin/env python3
"""Annotations V3-B — real run on the sandbox with a COPY of the V3 preview (build/annotations-v3-qa-app, its own profile,
settings and store; never the user's build/annotations-preview). Reuses the V3-A driver's helpers (sandbox_qa_v2.py).

    python -I tests/annotations/sandbox_qa_v3b.py run       # -> build/annotations-v3b-qa/report.json + PNGs
    python -I tests/annotations/sandbox_qa_v3b.py idle      # rest outside the mode: V3-A reference build vs this build

What is checked, on the real browser:
  entry       the veil on screen (blue at the top of the page) then gone, its window destroyed; the duration from the
              log (« entry veil destroyed after N ms »); an exit during the entry (interruption); reduced motion
  cursor      the REAL Windows cursor (GetCursorInfo + GetIconInfo: size, hot spot, blue pixels of the « + ») over a
              text, a link, a field, the canvas, the same-origin and the cross-origin iframe; the arrow over the band
              and the note panel, the I-beam in the note field; no hourglass while the shot is taken; after the exit,
              the page's own cursors again (I-beam on text, hand on the link) and no style left in the page
  inspector   on a flex container (form.checkout, gap 6), a flex-wrap row (.row, gap 8), a grid (.frames, gap 16), a
              padded block (.summary): blue / pink hatch pixels on screen; nothing during a fast movement; zoom 150 %
  capture     the stored shot has no frame, no hatch and no cursor pixel; the screen keeps the hatches during the shot
              (native copy); target.spacing in the store and the « Style » line through the MCP bridge
  measures    CPU + thread context switches (wake-ups) of browser.exe at rest outside the mode, in the mode with the
              wave (foreground), in reduced motion
The pointer is moved with SetCursorPos over our own window only (topmost while probing) and put back afterwards.
Agent connection is redirected; the real Claude / Codex configurations are fingerprinted before and after.
"""
from __future__ import annotations
import ctypes
import ctypes.wintypes as W
import json
import pathlib
import re
import shutil
import subprocess
import sys
import time
import traceback

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import sandbox_qa_v2 as V   # noqa: E402
Q = V.Q
ROOT = V.ROOT
QA = ROOT / 'build' / 'annotations-v3b-qa'
V.QA = Q.QA = QA
user32, k32, gdi32 = ctypes.windll.user32, ctypes.windll.kernel32, ctypes.windll.gdi32
step, report = V.step, V.report

# ---------------------------------------------------------------- foreground without keyboard input
def foreground():
    h = Q.main_window()
    if not h:
        return False
    fg = user32.GetForegroundWindow(); other = user32.GetWindowThreadProcessId(fg, None) if fg else 0; me = k32.GetCurrentThreadId()
    if other and other != me:
        user32.AttachThreadInput(other, me, True)
    user32.SetForegroundWindow(h); user32.BringWindowToTop(h)
    if other and other != me:
        user32.AttachThreadInput(other, me, False)
    time.sleep(0.15)
    pid = W.DWORD(); user32.GetWindowThreadProcessId(user32.GetForegroundWindow(), ctypes.byref(pid))
    if pid.value != Q.our_pid():
        user32.SwitchToThisWindow(h, True); time.sleep(0.25)
    pid = W.DWORD(); user32.GetWindowThreadProcessId(user32.GetForegroundWindow(), ctypes.byref(pid))
    return pid.value == Q.our_pid()

# ---------------------------------------------------------------- is there a desktop to look at?
def desktop_ok():
    """False when this session has no input desktop (workstation locked / console disconnected): no screen grab,
    no cursor information, no foreground window. Screen probes are then skipped (reported, not passed)."""
    try:
        from PIL import ImageGrab
        ImageGrab.grab(bbox=(0, 0, 8, 8)); return True
    except Exception:
        return False

def skipped(name, why='desktop unavailable (session locked / disconnected): screen probe not run'):
    report.setdefault('skipped', []).append({'step': name, 'why': why}); print('SKIP ' + name + ' — ' + why, flush=True)

# ---------------------------------------------------------------- the closed root, read through CDP (DOM pierce)
class Dom:
    opened = []
    def __init__(self):
        self.ws = Q.page_ws(); self.ws.call('DOM.enable', {}); self.ws.call('CSS.enable', {}); Dom.opened.append(self)
    @staticmethod
    def close_all():
        """The DevTools sessions this driver opened (DOM / CSS agents enabled): closed before any rest measure."""
        for d in Dom.opened:
            try:
                d.ws.call('CSS.disable', {}); d.ws.call('DOM.disable', {}); d.ws.s.close()
            except Exception:
                pass
        Dom.opened = []
    def root(self):
        doc = self.ws.call('DOM.getDocument', {'depth': -1, 'pierce': True})['root']
        stack = [doc]
        while stack:
            n = stack.pop()
            if n.get('localName') == 'browser-annotation-layer' and n.get('shadowRoots'):
                return n['shadowRoots'][0]
            stack.extend(n.get('children', [])); stack.extend(n.get('shadowRoots', []))
            if n.get('contentDocument'): stack.append(n['contentDocument'])
        return None
    @staticmethod
    def attrs(n):
        a = n.get('attributes', []); return dict(zip(a[::2], a[1::2]))
    def cursor(self, node_id):
        cs = self.ws.call('CSS.getComputedStyleForNode', {'nodeId': node_id})['computedStyle']
        return next((p['value'] for p in cs if p['name'] == 'cursor'), None)
    def inspector(self, boxes=False):
        """(opacity, {'pb': 0/1 padding ring shown, 'gb': 0/1 gap union shown}, label texts[, label boxes, element label
        box, order of the root's layers]). V3-B rev.: one ring + one union in `.i`, the value labels in `.j` (after `.l`)."""
        r = self.root()
        if not r:
            return None
        kids = r.get('children', [])
        cont = next((c for c in kids if Dom.attrs(c).get('class', '').split()[:1] == ['i']), None)
        labs = next((c for c in kids if Dom.attrs(c).get('class', '').split()[:1] == ['j']), None)
        if not cont or not labs:
            return None
        style = Dom.attrs(cont).get('style', '')
        m = re.search(r'opacity:\s*([0-9.]+)', style); op = float(m.group(1)) if m else 0.0
        bands = {'pb': 0, 'gb': 0}; labels = []; lboxes = []
        for ch in cont.get('children', []):
            a = Dom.attrs(ch); cls = a.get('class', ''); st = a.get('style', '')
            if cls in bands and 'display: none' not in st and 'clip-path' in st:
                bands[cls] += 1
        for ch in labs.get('children', []):
            a = Dom.attrs(ch); cls = a.get('class', ''); st = a.get('style', '')
            if 'display: none' in st or cls not in ('pl', 'gl'):
                continue
            txt = ''.join(t.get('nodeValue', '') for t in ch.get('children', []) if t.get('nodeType') == 3)
            labels.append(('p' if cls == 'pl' else 'g') + txt)
            if boxes:
                lboxes.append((labels[-1], self.box(ch['nodeId'])))
        if not boxes:
            return op, bands, labels
        el = next((c for c in kids if Dom.attrs(c).get('class', '').split()[:2] == ['x', 'l']), None)
        order = [Dom.attrs(c).get('class', '').replace(' on', '') for c in kids if Dom.attrs(c).get('class', '') not in ('s',) and c.get('nodeType') == 1 and c.get('localName') == 'div']
        return op, bands, labels, lboxes, self.box(el['nodeId']) if el else None, order
    def box(self, node_id):
        try:
            q = self.ws.call('DOM.getBoxModel', {'nodeId': node_id})['model']['border']
            xs, ys = q[0::2], q[1::2]; return [min(xs), min(ys), max(xs) - min(xs), max(ys) - min(ys)]
        except Exception:
            return None
    def shields(self):
        r = self.root()
        return [c for c in (r.get('children', []) if r else []) if Dom.attrs(c).get('class') == 's' and 'display:none' not in Dom.attrs(c).get('style', '').replace(' ', '')]

def css_cursor(selector):
    return Q.evaluate("(function(){var e=document.querySelector(%s);return e?getComputedStyle(e).cursor:null})()" % json.dumps(selector))

def our_css_cursor(v):
    return isinstance(v, str) and 'image-set(' in v and 'data:image/svg+xml' in v and '3E67FF' in v.upper()

# ---------------------------------------------------------------- the real cursor
class CURSORINFO(ctypes.Structure):
    _fields_ = [('cbSize', W.DWORD), ('flags', W.DWORD), ('hCursor', W.HANDLE), ('pt', W.POINT)]
class ICONINFO(ctypes.Structure):
    _fields_ = [('fIcon', W.BOOL), ('xHotspot', W.DWORD), ('yHotspot', W.DWORD), ('hbmMask', W.HBITMAP), ('hbmColor', W.HBITMAP)]
class BITMAP(ctypes.Structure):
    _fields_ = [('bmType', W.LONG), ('bmWidth', W.LONG), ('bmHeight', W.LONG), ('bmWidthBytes', W.LONG), ('bmPlanes', W.WORD), ('bmBitsPixel', W.WORD), ('bmBits', ctypes.c_void_p)]
class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [('biSize', W.DWORD), ('biWidth', W.LONG), ('biHeight', W.LONG), ('biPlanes', W.WORD), ('biBitCount', W.WORD), ('biCompression', W.DWORD),
                ('biSizeImage', W.DWORD), ('biXPelsPerMeter', W.LONG), ('biYPelsPerMeter', W.LONG), ('biClrUsed', W.DWORD), ('biClrImportant', W.DWORD)]
user32.LoadCursorW.restype = W.HANDLE
gdi32.GetObjectW.argtypes = [W.HANDLE, ctypes.c_int, ctypes.c_void_p]
gdi32.GetDIBits.argtypes = [W.HDC, W.HANDLE, W.UINT, W.UINT, ctypes.c_void_p, ctypes.c_void_p, W.UINT]
gdi32.DeleteObject.argtypes = [W.HANDLE]
user32.GetDC.restype = W.HDC
user32.ReleaseDC.argtypes = [W.HWND, W.HDC]
user32.GetIconInfo.argtypes = [W.HANDLE, ctypes.c_void_p]
user32.LoadCursorW.argtypes = [W.HINSTANCE, ctypes.c_void_p]
STANDARD = {32512: 'arrow', 32513: 'ibeam', 32514: 'wait', 32515: 'cross', 32649: 'hand', 32650: 'appstarting', 32516: 'uparrow'}
STD = {}
def standard_cursors():
    if not STD:
        for i, n in STANDARD.items():
            STD[user32.LoadCursorW(None, ctypes.c_void_p(i)) or 0] = n
    return STD

def cursor_now():
    ci = CURSORINFO(); ci.cbSize = ctypes.sizeof(CURSORINFO)
    if not user32.GetCursorInfo(ctypes.byref(ci)) or not ci.hCursor:
        return {'kind': 'none'}
    name = standard_cursors().get(ci.hCursor)
    if name:
        return {'kind': name}
    info = ICONINFO()
    if not user32.GetIconInfo(W.HANDLE(ci.hCursor), ctypes.byref(info)):
        return {'kind': 'custom'}
    out = {'kind': 'custom', 'hot': [info.xHotspot, info.yHotspot]}
    if info.hbmColor:
        bm = BITMAP(); gdi32.GetObjectW(info.hbmColor, ctypes.sizeof(BITMAP), ctypes.byref(bm))
        out['size'] = [bm.bmWidth, bm.bmHeight]
        bi = BITMAPINFOHEADER(); bi.biSize = ctypes.sizeof(BITMAPINFOHEADER); bi.biWidth = bm.bmWidth; bi.biHeight = -bm.bmHeight; bi.biPlanes = 1; bi.biBitCount = 32
        buf = (ctypes.c_ubyte * (bm.bmWidth * bm.bmHeight * 4))(); dc = user32.GetDC(None)
        gdi32.GetDIBits(dc, info.hbmColor, 0, bm.bmHeight, buf, ctypes.byref(bi), 0); user32.ReleaseDC(None, dc)
        blue = dark = 0
        for i in range(0, len(buf), 4):
            b, g, r = buf[i], buf[i + 1], buf[i + 2]
            if b > 200 and r < 110 and g < 150: blue += 1
            if r < 60 and g < 60 and b < 60 and buf[i + 3] > 0: dark += 1
        out['bluePixels'] = blue; out['darkPixels'] = dark
    for hb in (info.hbmMask, info.hbmColor):
        if hb: gdi32.DeleteObject(hb)
    return out

def cursor_at(sx, sy, settle=0.25):
    user32.SetCursorPos(int(sx), int(sy)); time.sleep(0.05)
    user32.SetCursorPos(int(sx) + 1, int(sy)); time.sleep(settle)
    return cursor_now()

def css_to_screen(cx, cy):
    o = V.page_origin(); dpr = Q.evaluate('devicePixelRatio')
    return o[0] + cx * dpr, o[1] + cy * dpr

def is_ours(c):   # the « viseur »: a custom cursor with its blue centre dot, hot spot at the centre
    sz = c.get('size') or [0, 0]; hot = c.get('hot') or [-9, -9]
    return c.get('kind') == 'custom' and c.get('bluePixels', 0) > 8 and abs(hot[0] - sz[0] / 2) <= 1.5 and abs(hot[1] - sz[1] / 2) <= 1.5

# ---------------------------------------------------------------- hatches on screen
def hatch_counts(im):
    px = im.convert('RGB').load(); w, h = im.size; pink = blue = 0
    for y in range(h):
        for x in range(w):
            r, g, b = px[x, y]
            if r > 180 and b > 110 and r - g > 45 and r > b: pink += 1
            elif b > 235 and 110 < r < 215 and b - r > 35 and g < 225: blue += 1
    return pink, blue

def point_on(selector):
    """A point (CSS px of the viewport) where elementFromPoint IS the element itself (its padding or a gap), the
    closest to its centre on a 3 px grid."""
    return Q.evaluate("(function(){var e=document.querySelector(%s);if(!e)return null;e.scrollIntoView({block:'center'});var r=e.getBoundingClientRect();"
                      "var cx=r.left+r.width/2,cy=r.top+r.height/2,best=null,bd=1e9;"
                      "for(var y=Math.max(0,r.top+1);y<Math.min(innerHeight,r.bottom);y+=3)for(var x=Math.max(0,r.left+1);x<Math.min(innerWidth,r.right);x+=3){"
                      "if(document.elementFromPoint(x,y)!==e)continue;var d=(x-cx)*(x-cx)+(y-cy)*(y-cy);if(d<bd){bd=d;best=[x,y];}}"
                      "return best?[best[0],best[1],r.left,r.top,r.width,r.height]:null})()" % json.dumps(selector))

def hover(x, y):
    Q.page_ws().call('Input.dispatchMouseEvent', {'type': 'mouseMoved', 'x': x, 'y': y})

def box_of(rect, pad=6):
    o = V.page_origin(); dpr = Q.evaluate('devicePixelRatio')
    return (int(o[0] + rect[0] * dpr) - pad, int(o[1] + rect[1] * dpr) - pad, int(o[0] + (rect[0] + rect[2]) * dpr) + pad, int(o[1] + (rect[1] + rect[3]) * dpr) + pad)

def inspector_on(selector, label, expect_pink=True):
    pt = point_on(selector)
    if not pt:
        return step(f'5.3 {label}: a point on {selector} itself', False, 'none')
    hover(pt[0] + 0.5, pt[1]); time.sleep(0.6)
    im = V.grab(box_of(pt[2:])); pink, blue = hatch_counts(im)
    im.save(QA / f"v3b-inspector-{label.split()[0].replace('.', '-')}.png")
    rec = {'selector': selector, 'pinkPixels': pink, 'bluePixels': blue}
    report['measures'].setdefault('inspector', []).append(rec)
    return step(f'5.3 {label}: hatches on screen (padding blue / gap pink)', (pink > 60 if expect_pink else True) and blue > 60, rec)

# ---------------------------------------------------------------- process measures (CPU + context switches)
class UNICODE_STRING(ctypes.Structure):
    _fields_ = [('Length', W.USHORT), ('MaximumLength', W.USHORT), ('Buffer', ctypes.c_void_p)]

def context_switches(pid):
    """Sum of the context switches of every thread of `pid` (SystemProcessInformation)."""
    ntdll = ctypes.windll.ntdll; size = 1 << 20
    while True:
        buf = ctypes.create_string_buffer(size); ret = W.ULONG()
        st = ntdll.NtQuerySystemInformation(5, buf, size, ctypes.byref(ret)) & 0xFFFFFFFF
        if st == 0xC0000004:
            size *= 2; continue
        if st != 0:
            return None
        break
    off = 0; raw = buf.raw
    while True:
        nxt = int.from_bytes(raw[off:off + 4], 'little'); threads = int.from_bytes(raw[off + 4:off + 8], 'little')
        upid = int.from_bytes(raw[off + 0x50:off + 0x58], 'little')
        if upid == pid:
            total = 0; t = off + 0x100   # SYSTEM_PROCESS_INFORMATION is 0x100 bytes on x64, then SYSTEM_THREAD_INFORMATION[0x50]
            for i in range(threads):
                total += int.from_bytes(raw[t + i * 0x50 + 0x40:t + i * 0x50 + 0x44], 'little')   # ContextSwitches
            return {'threads': threads, 'switches': total}
        if not nxt:
            return None
        off += nxt

def measure(label, seconds=15):
    pid = Q.our_pid(); s0 = context_switches(pid); r = V.rest_cpu(seconds); s1 = context_switches(pid)
    r['contextSwitches'] = (s1['switches'] - s0['switches']) if s0 and s1 else None
    r['switchesPerSecond'] = round(r['contextSwitches'] / seconds, 1) if r['contextSwitches'] is not None else None
    r['threads'] = s1['threads'] if s1 else None
    r.pop('layerWindows', None)
    report['measures'][label] = r
    print(label, r, flush=True)
    return r

def write_settings(**kv):
    lines = ['browser-settings 1'] + [f'{k}={v}' for k, v in kv.items()]
    (V.APP / 'settings').write_text('\n'.join(lines) + '\n', encoding='utf-8', newline='\n')

def transparent_layers():
    """Our click-through layers (the frame copy, the entry veil): (visible) list."""
    main = Q.main_window(); out = []
    @ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    def cb(h, _):
        name = ctypes.create_unicode_buffer(64); user32.GetClassNameW(h, name, 64)
        if name.value == 'BrowserAnnotationLayer' and user32.GetWindowLongW(h, -20) & 0x20:
            out.append(bool(user32.IsWindowVisible(h)))
        return True
    if main:
        user32.EnumChildWindows(main, cb, 0)
    return out

# ---------------------------------------------------------------- scenario
def click_at(x, y):
    ws = Q.page_ws()
    ws.call('Input.dispatchMouseEvent', {'type': 'mousePressed', 'x': x, 'y': y, 'button': 'left', 'clickCount': 1})
    ws.call('Input.dispatchMouseEvent', {'type': 'mouseReleased', 'x': x, 'y': y, 'button': 'left', 'clickCount': 1})

def later():
    if 'AnnotationConnectLater' in V.uia_list():
        V.invoke('AnnotationConnectLater'); time.sleep(0.5)

def inspector_dom(selector, label, expect_gap=True):
    pt = point_on(selector)
    if not pt:
        return step(f'5.3 {label}: a point on {selector} itself', False, 'none')
    hover(pt[0] + 0.5, pt[1]); time.sleep(0.45)
    got = Dom().inspector()
    rec = {'selector': selector, 'inspector': got}
    report['measures'].setdefault('inspectorDom', []).append(rec)
    ok = got is not None and got[0] >= 0.99 and got[1]['pb'] + got[1]['gb'] > 0 and (got[1]['gb'] > 0 if expect_gap else got[1]['gb'] == 0) and got[2]
    step(f'5.3 {label}: inspector shown in the page (opacity, bands, labels)', ok, rec)
    if desktop_ok():
        im = V.grab(box_of(pt[2:])); pink, blue = hatch_counts(im); im.save(QA / f"v3b-inspector-{label.split()[0].replace('.', '-')}.png")
        step(f'5.3 {label}: hatches on screen (padding blue / gap pink)', (pink > 60 if expect_gap else True) and blue > 60, {'pink': pink, 'blue': blue})
    return got

def scenario():
    p = V.launch(); screen = desktop_ok()
    report['desktop'] = screen
    try:
        V.topmost(True); fg = foreground() if screen else False; time.sleep(0.5)
        if screen:
            step('setup: QA copy in the foreground (needed for the wave and the cursor)', fg)
        else:
            skipped('setup: foreground (no input desktop)')
        measure('restBeforeUse', 10)
        sheets_before = Q.evaluate('document.adoptedStyleSheets.length')
        # ---- entry: duration from the log, window destroyed; on screen when there is a screen
        mark = len(V.log_text()); o = V.page_origin(); r = W.RECT(); user32.GetWindowRect(Q.main_window(), ctypes.byref(r))
        if screen:
            frames, _ = V.frames_during((r.left + 8, o[1], r.right - 8, o[1] + 110), lambda: V.invoke('AnnotateButton'), 1400)
        else:
            frames = []; V.invoke('AnnotateButton')
        ent = V.wait_log(r'ANNOTATE entry veil destroyed after (\d+) ms \((\d+) frames, worst frame (\d+) ms\)', 5, mark)
        on = V.wait_log(r'ANNOTATE mode on[^\n]*', 1, mark)
        report['measures']['entry'] = {'logged': ent.group(0) if ent else None}
        step('5.1 entry veil destroyed ~500 ms after the start (log: real duration, frames, worst frame)', ent is not None and 480 <= int(ent.group(1)) <= 650, ent.group(0) if ent else 'no log line')
        step('5.1 no click-through layer left after the entry (veil window destroyed)', not any(transparent_layers()), transparent_layers())
        if frames:
            dpi_ = user32.GetDpiForWindow(Q.main_window())
            def tint(im):
                px = im.convert('RGB').load(); w, h = im.size; s_ = n = 0
                for x in range(10, w - 10, 7):
                    for y in range(int(32 * dpi_ / 96) + 8, min(h, int(32 * dpi_ / 96) + 60), 3):   # the page's top once moved under the band
                        q = px[x, y]; s_ += q[2] - q[0]; n += 1
                return s_ / max(1, n)
            tints = [(round(t), round(tint(im), 1)) for t, im in frames]
            base = sorted(v for t, v in tints if t < 0)[:1] or [0]
            lit = [t for t, v in tints if v > base[0] + 12]
            best = max(range(len(frames)), key=lambda i: tints[i][1]); frames[best][1].save(QA / 'v3b-5.1-real-entry-peak.png')
            rec = {'framesTinted': len(lit), 'tintedFromMs': lit[0] if lit else None, 'tintedToMs': lit[-1] if lit else None, 'maxTint': max(v for t, v in tints), 'baseTint': base[0]}
            report['measures']['entry'].update(rec)
            step('5.1 entry veil on screen, then gone (blue at the top of the page during the entry only)', len(lit) >= 3 and tints[-1][1] <= base[0] + 6, rec)
        else:
            skipped('5.1 entry veil on screen (screen frames)')
        later()
        # ---- the mode with the wave: CPU + wake-ups (the wave runs only when the window can be seen)
        if screen:
            def ours_fg():
                pid = W.DWORD(); user32.GetWindowThreadProcessId(user32.GetForegroundWindow(), ctypes.byref(pid)); return pid.value == Q.our_pid()
            for attempt in range(3):   # the wave runs only while our window is in the foreground: held for the whole window
                foreground(); time.sleep(0.5); before_fg = ours_fg(); m_ = measure('modeWithWave', 15); after_fg = ours_fg()
                m_['foregroundBeforeAfter'] = [before_fg, after_fg]
                if before_fg and after_fg:
                    break
            step('mode with the wave measured with the window in the foreground (before and after the 15 s)', before_fg and after_fg, m_)
        else:
            measure('modeWaveNotVisible', 15)
            skipped('CPU in the mode with the wave running (needs the window in the foreground)')
        # ---- cursor: CSS on the page (computed) + shields over both iframes; the real Windows cursor when possible
        sels = [('h1', 'texte (h1)'), ('a.brand', 'lien'), ('#email', 'champ'), ('canvas', 'canvas (image)'), ('label', 'libellé')]
        css = {name: css_cursor(sel) for sel, name in sels}
        report['measures']['cssCursors'] = {k: (v[:80] + '…' if isinstance(v, str) and len(v) > 80 else v) for k, v in css.items()}
        step('5.4 page: the « viseur » cursor on text, link, field, canvas (computed style)', all(our_css_cursor(v) for v in css.values()), report['measures']['cssCursors'])
        d = Dom(); sh = d.shields(); frames_ = Q.evaluate("Array.from(document.querySelectorAll('iframe')).map(function(f){var r=f.getBoundingClientRect();return [f.title,Math.round(r.left),Math.round(r.top),Math.round(r.width),Math.round(r.height)]})")
        shield_cursors = [d.cursor(n['nodeId']) for n in sh]
        rec = {'iframes': frames_, 'shields': [Dom.attrs(n).get('style') for n in sh], 'cursors': [c[:60] + '…' if c else c for c in shield_cursors]}
        step('5.4 iframes (same origin and cross origin): covered by shields carrying the same cursor', len(sh) >= len(frames_) >= 2 and all(our_css_cursor(c) for c in shield_cursors), rec)
        size = re.search(r"width='(\d+)'", css['texte (h1)'] or '') or re.search(r'width%3D%27(\d+)%27', css['texte (h1)'] or '')
        dpi = user32.GetDpiForWindow(Q.main_window()); expect = 64 if dpi / 96 >= 1.75 else 48 if dpi / 96 >= 1.25 else 32
        step(f'5.4 cursor size from the DPI ({dpi}) x pointer size: {expect} px', size is not None and int(size.group(1)) == expect, size.group(0) if size else css['texte (h1)'][:120])
        if screen:
            old = W.POINT(); user32.GetCursorPos(ctypes.byref(old)); cursors = {}
            try:
                for sel, name in [('h1', 'texte (h1)'), ('a.brand', 'lien'), ('#email', 'champ'), ('canvas', 'canvas'), ('iframe[title=Livraison]', 'iframe même origine'), ('iframe[title="Paiement PayCo"]', 'iframe croisée')]:
                    rc = V.rect_of(sel); foreground(); c = cursor_at(*css_to_screen(rc[0], rc[1]))
                    if not is_ours(c):   # once more: a window of another app may have passed over ours (shared desktop)
                        V.topmost(True); foreground(); c = cursor_at(*css_to_screen(rc[0], rc[1]), settle=0.5); c['retried'] = True
                    cursors[name] = c
                    step(f'5.4 real Windows cursor « viseur » over {name}', is_ours(c), c)
                c = cursor_at((r.left + r.right) / 2, V.page_origin()[1] - 16 * dpi / 96); cursors['bandeau'] = c
                step('5.4 real cursor: normal arrow over the band (our native UI)', c.get('kind') == 'arrow', c)
                report['measures']['cursors'] = cursors
                # no hourglass while the shot is taken (the pointer on the element clicked), then the note panel's cursors
                pos = V.rect_of('h1'); cursor_at(*css_to_screen(pos[0], pos[1])); mark = len(V.log_text()); seen = []
                click_at(pos[0], pos[1]); t0 = time.time()
                while time.time() - t0 < 0.6:
                    seen.append(cursor_now().get('kind')); time.sleep(0.015)
                V.wait_log(r'ANNOTATE capture', 3, mark)
                step(f'5.4 no hourglass while the shot is taken ({len(seen)} samples over 600 ms)', not any(k_ in ('wait', 'appstarting') for k_ in seen), sorted(set(seen)))
                time.sleep(0.4); pop = [w_ for w_ in Q.windows() if w_[1] == 'BrowserAnnotationNote']
                if pop:
                    edit = Q.find_child(pop[0][0], 'Edit'); er = W.RECT(); user32.GetWindowRect(edit, ctypes.byref(er)); pr = pop[0][3]
                    c1 = cursor_at((er.left + er.right) / 2, (er.top + er.bottom) / 2); c2 = cursor_at(er.left + 10, er.bottom + 30)   # the row under the field (site label): the panel's layer, not the field's window
                    step('5.4 note panel: I-beam in the note field, arrow on the panel', c1.get('kind') == 'ibeam' and c2.get('kind') == 'arrow', {'field': c1, 'panel': c2})
                else:
                    step('5.4 note panel open after the click', False, 'no BrowserAnnotationNote window')
                V.close_note()
            finally:
                user32.SetCursorPos(old.x, old.y)
        else:
            skipped('5.4 real Windows cursor over text / link / field / canvas / iframes / band (GetCursorInfo)')
        # ---- inspector (DOM of the closed root through CDP; the screen when there is one)
        inspector_dom('form.checkout', 'form.checkout flex column gap 6')
        inspector_dom('.row', '.row flex-wrap gap 8')
        inspector_dom('.frames', '.frames grid gap 16')
        inspector_dom('aside.summary', '.summary padding 18/20 (block)', expect_gap=False)
        # ---- value labels: above the frame's edges and the element label, whole on a 4 px side (span.badge: padding 4 / 10,
        #      in promo-badge's open shadow root — the case seen in the real test of 0.3.258)
        bd = Q.evaluate("(function(){var h=document.querySelector('promo-badge');h.scrollIntoView({block:'center'});var b=h.shadowRoot.querySelector('.badge'),r=b.getBoundingClientRect();"
                        "for(var x=r.left+1;x<r.left+10;x+=1)if(h.shadowRoot.elementFromPoint(x,r.top+r.height/2)===b)return [x,r.top+r.height/2,r.left,r.top,r.width,r.height];return null})()")
        if bd:
            hover(bd[0], bd[1]); time.sleep(0.5); got = Dom().inspector(boxes=True)
            rec = {'badge': bd[2:], 'inspector': got}
            report['measures']['badgeLabels'] = rec
            if got:
                op, bands, labels, lboxes, elbox, order = got
                step('5.3 layers: hatches under the frame edges, value labels above the edges AND the element label (DOM order)',
                     order.index('i') < order.index('x h') and order.index('j') > order.index('x l'), order)
                def inter(a, b): return a and b and a[0] < b[0] + b[2] - 0.01 and b[0] < a[0] + a[2] - 0.01 and a[1] < b[1] + b[3] - 0.01 and b[1] < a[1] + a[3] - 0.01
                collide = [t for t, b in lboxes if inter(b, elbox)] + [f'{t1}/{t2}' for i1, (t1, b1) in enumerate(lboxes) for (t2, b2) in lboxes[i1 + 1:] if inter(b1, b2)]
                step('5.3 span.badge: value labels « 4 » and « 10 », none under the element label nor on each other', 'p4' in labels and 'p10' in labels and not collide, {'labels': lboxes, 'elementLabel': elbox, 'collisions': collide})
                x0, y0, w0, h0 = bd[2:]; dprb = 1   # boxes are CSS px of the viewport
                whole = all(b and b[3] >= 13.5 and b[2] >= 12 and b[0] >= -0.5 and b[1] >= -0.5 for t, b in lboxes)
                inward = all(b[1] >= y0 - 0.5 for t, b in lboxes if t == 'p4') and all(b[0] >= x0 - 0.5 for t, b in lboxes if t == 'p10')
                step('5.3 span.badge: each label whole (14 px) and overflowing INWARD on the 4 px / 10 px sides', whole and inward, lboxes)
                if desktop_ok():
                    foreground(); im = V.grab(box_of([x0 - 30, y0 - 30, w0 + 60, h0 + 60])); im.save(QA / 'v3b-real-badge-value-labels.png')
        else:
            step('5.3 span.badge found', False)
        # ---- no seam at the padding ring's four inner corners (.summary, padding 18/20, radius 12): on screen, each pixel
        #      equals the pixel 4 device px further along the stripes (135 deg: direction (-1, 1))
        if desktop_ok():
            foreground()
            geo = Q.evaluate("(function(){var e=document.querySelector('aside.summary');e.scrollIntoView({block:'center'});var r=e.getBoundingClientRect(),c=getComputedStyle(e),f=parseFloat;"
                             "var bl=f(c.borderLeftWidth),bt=f(c.borderTopWidth),br=f(c.borderRightWidth),bb=f(c.borderBottomWidth);"
                             "return [r.left+bl,r.top+bt,r.width-bl-br,r.height-bt-bb,f(c.paddingLeft),f(c.paddingTop),f(c.paddingRight),f(c.paddingBottom)]})()")
            pt_ = point_on('aside.summary'); hover(pt_[0], pt_[1]); time.sleep(0.6)
            o = V.page_origin(); dpr = Q.evaluate('devicePixelRatio')
            ix, iy, iw, ih, pl_, pt2, pr_, pb_ = geo
            cl, ct, cr, cb = ix + pl_, iy + pt2, ix + iw - pr_, iy + ih - pb_
            im = V.grab((int(o[0] + ix * dpr) - 2, int(o[1] + iy * dpr) - 2, int(o[0] + (ix + iw) * dpr) + 2, int(o[1] + (iy + ih) * dpr) + 2)).convert('RGB')
            im.save(QA / 'v3b-real-summary-ring.png'); px = im.load(); ox, oy = int(o[0] + ix * dpr) - 2, int(o[1] + iy * dpr) - 2
            def D(cx, cy): return int(o[0] + cx * dpr) - ox, int(o[1] + cy * dpr) - oy
            corners = {'top-left': (ix + 6, cl - 1, ct - 6, ct + 6, -1), 'top-right': (cr + 1, ix + iw - 6, ct - 6, ct + 6, 1),
                       'bottom-left': (ix + 6, cl - 1, cb - 6, cb + 6, -1), 'bottom-right': (cr + 1, ix + iw - 6, cb - 6, cb + 6, 1)}
            res = {}
            for name, (xa, xb, ya, yb, sgn) in corners.items():
                (X0, Y0), (X1, Y1) = D(xa, ya), D(xb, yb); over = worst = n = 0
                for yy in range(Y0, Y1):
                    for xx in range(X0, X1):
                        a_ = px[xx, yy]; b_ = px[xx + 4 * sgn, yy - 4 * sgn]; d_ = max(abs(a_[0] - b_[0]), abs(a_[1] - b_[1]), abs(a_[2] - b_[2])); n += 1
                        worst = max(worst, d_); over += d_ > 3
                res[name] = {'pixels': n, 'over3': over, 'worst': worst}
            report['measures']['ringCornerSeams'] = res
            step('5.3 no seam at the padding ring\'s four inner corners (on screen: pattern equal along the stripes)', all(v['over3'] == 0 and v['pixels'] > 50 for v in res.values()), res)
        else:
            skipped('5.3 corner seams on screen')
        # nothing during a fast movement: a new container every 25 ms, the container read after each move
        pts = [x for x in (point_on(s_) for s_ in ('form.checkout', '.row', '.frames', 'aside.summary', 'main#app')) if x]
        hover(1, 1); time.sleep(0.3); d = Dom(); ws = Q.page_ws(); seen = []
        t0 = time.time()
        for i in range(16):
            pt = pts[i % len(pts)]; ws.call('Input.dispatchMouseEvent', {'type': 'mouseMoved', 'x': pt[0] + (i % 3), 'y': pt[1]})
            got = d.inspector(); seen.append(round(got[0], 2) if got else None); time.sleep(0.025)
        sweep_ms = round((time.time() - t0) * 1000)
        time.sleep(0.4); rest = d.inspector()
        step(f'5.3 nothing during a fast movement (16 containers in {sweep_ms} ms: inspector opacity 0 after every move)', all(v == 0 for v in seen) and rest and rest[0] >= 0.99, {'opacityAfterEachMove': seen, 'atRest': rest})
        # ---- capture on a flex and a grid container: no frame / hatch / cursor in the stored shot
        caps = []
        for sel, name in (('form.checkout', 'flex'), ('.frames', 'grid')):
            pt = point_on(sel); hover(pt[0], pt[1]); time.sleep(0.5)
            box = box_of(pt[2:]); before = hatch_counts(V.grab(box)) if screen else None; mark = len(V.log_text())
            if screen:
                frames, _ = V.frames_during(box, lambda: click_at(pt[0], pt[1]), 450)
                counts = [(round(t), *hatch_counts(im)) for t, im in frames]
                low = [c for c in counts if c[0] >= 0 and (c[1] + c[2]) < (before[0] + before[1]) * 0.7]
            else:
                click_at(pt[0], pt[1])
            insp = V.wait_log(r'ANNOTATE inspector at the selection: [^\n]*', 3, mark)
            cap = V.wait_log(r'ANNOTATE capture \d+x\d+[^\n]*|ANNOTATE capture unavailable[^\n]*', 5, mark)
            step(f'capture {name}: the inspector goes to the native copy at the selection (log)', insp is not None and 'gap rectangles' in insp.group(0), insp.group(0) if insp else None)
            if screen:
                step(f'capture {name}: the hatches stay on screen during the shot (native copy)', before[0] + before[1] > 60 and not low and cap is not None, {'before': before, 'frames': len(counts), 'framesBelow70pc': len(low)})
            else:
                skipped(f'capture {name}: hatches kept on screen during the shot (screen frames)')
            step(f'capture {name}: the engine shot was taken', cap is not None and 'unavailable' not in cap.group(0), cap.group(0)[:200] if cap else None)
            crop = re.search(r'viewport crop (\d+),(\d+) (\d+)x(\d+)', cap.group(0)) if cap else None
            scroll = Q.evaluate('[scrollX, scrollY]'); at = Q.evaluate("(function(){var r=document.querySelector(%s).getBoundingClientRect();return [r.left,r.top]})()" % json.dumps(sel))
            V.note_type(f'V3-B {name} : réduire le gap'); time.sleep(0.2); V.invoke('AnnotationAdd')
            w = V.wait_log(r'ANNOTATE written (\S+)/(ann_\w+)[^\n]*', 5, mark)
            if not w:
                step(f'capture {name}: written', False); continue
            time.sleep(1.0)
            d_ = next(iter(V.STORE.glob(f'*/{w.group(2)}')), None); shotp = d_ / 'shot.png' if d_ else None
            if shotp and shotp.exists() and crop:
                caps.append({'name': name, 'sel': sel, 'shot': shotp, 'crop': tuple(int(crop.group(k)) for k in (1, 2, 3, 4)), 'scroll': scroll, 'point': pt[:2], 'at': at})
            else:
                step(f'capture {name}: shot.png + crop', False, str(d_))
            js = json.loads((d_ / 'annotation.json').read_text(encoding='utf-8')) if d_ else {}
            sp = js.get('target', {}).get('spacing')
            step(f'capture {name}: target.spacing in the store', bool(sp) and sp.get('layout') == name and sp.get('gap'), sp)
            rc_, out = V.try_mcp('get', w.group(2))
            line = next((l for l in out.splitlines() if 'Style (px CSS)' in l), None)
            step(f'capture {name}: « Style » line through the MCP bridge', line is not None and 'gap' in line, line)
            time.sleep(0.6)
        # ---- exit: no style left in the page, its own cursors back
        V.mode(False); time.sleep(0.5)
        # The stored shots = the engine's own shot of the same crop, mode OFF, same scroll, same hover: no frame, no
        # hatch, no value label, no cursor (any of them would differ by far more than the fill removal's 1-2 levels).
        import base64, io
        from PIL import Image, ImageChops
        for c in caps:
            # Mode off the viewport is 32 DIP taller (no band) and the scroll may clamp: the reference crop follows the
            # element (its position now vs at the capture), the hover point too.
            Q.evaluate(f'window.scrollTo({c["scroll"][0]}, {c["scroll"][1]})'); time.sleep(0.2)
            now = Q.evaluate("(function(){var r=document.querySelector(%s).getBoundingClientRect();return [r.left,r.top]})()" % json.dumps(c['sel']))
            dxc, dyc = now[0] - c['at'][0], now[1] - c['at'][1]; dpr = Q.evaluate('devicePixelRatio')
            hover(c['point'][0] + dxc, c['point'][1] + dyc); time.sleep(0.5)
            ws = Q.page_ws(); x, y, cw, ch = c['crop']; x += round(dxc * dpr); y += round(dyc * dpr)
            ref = Image.open(io.BytesIO(base64.b64decode(ws.call('Page.captureScreenshot', {'format': 'png', 'fromSurface': True, 'captureBeyondViewport': False})['data']))).convert('RGB')
            ref = ref.crop((x, y, x + cw, y + ch)); got = Image.open(c['shot']).convert('RGB')
            ref.save(QA / f"v3b-capture-{c['name']}-reference-mode-off.png"); got.save(QA / f"v3b-capture-{c['name']}-stored.png")
            if ref.size != got.size:
                step(f"capture {c['name']}: stored shot vs the page mode off", False, {'sizes': (ref.size, got.size)}); continue
            hist = ImageChops.difference(ref, got).convert('L').histogram()
            worst = max(i for i, v in enumerate(hist) if v); over2 = sum(hist[3:]); total = sum(hist)
            rec = {'size': got.size, 'worstLevel': worst, 'pixelsOver2Levels': over2, 'pixels': total, 'accentPixels': V.accent_count(got)}
            report['measures'].setdefault('shotVsModeOff', []).append(rec)
            step(f"capture {c['name']}: stored shot = the page mode off (no frame, no hatches, no labels, no cursor)", over2 <= total * 0.002 and rec['accentPixels'] == V.accent_count(ref), rec)
        # (the browser itself adopts one sheet on every page: « :root { scrollbar-width: none } », its own scrollbars)
        left = Q.evaluate("[Array.prototype.some.call(document.adoptedStyleSheets, function(s){return Array.prototype.some.call(s.cssRules, function(r){return /cursor/.test(r.cssText)})}), document.adoptedStyleSheets.length, document.querySelectorAll('browser-annotation-layer').length, getComputedStyle(document.querySelector('h1')).cursor, getComputedStyle(document.querySelector('a.brand')).cursor, getComputedStyle(document.querySelector('#email')).cursor]")
        step('5.4 after the exit: no style left in the page (no cursor rule, sheets back to the browser\'s own one, no layer), its own cursors (auto / pointer / text)',
             left == [False, sheets_before, 0, 'auto', 'pointer', 'text'], {'got': left, 'sheetsBefore': sheets_before})
        if screen:
            old = W.POINT(); user32.GetCursorPos(ctypes.byref(old))
            try:
                foreground(); rc = V.rect_of('h1'); c1 = cursor_at(*css_to_screen(rc[0] - rc[2] / 4, rc[1])); rc = V.rect_of('a.brand'); c2 = cursor_at(*css_to_screen(rc[0], rc[1]))
                step('5.4 after the exit: real cursor I-beam on text, hand on the link', c1.get('kind') == 'ibeam' and c2.get('kind') == 'hand', {'text': c1, 'link': c2})
            finally:
                user32.SetCursorPos(old.x, old.y)
        Dom.close_all(); import gc; gc.collect()
        time.sleep(2); measure('restAfterUse', 15)
        step('rest outside the mode after use: no click-through layer, no launcher window', not transparent_layers() and not V.launcher_windows(), V.layer_windows())
        # ---- interruption: the mode ends ~300 ms after it began (UIA invoke, then Échap posted to the band)
        mark = len(V.log_text()); V.invoke('AnnotateButton')
        on = V.wait_log(r'ANNOTATE mode on', 2, mark)
        band = next((w_ for w_ in V.layer_windows() if w_[0] == 'Mode Annotation'), None)
        Q.post_key(0x1B, Q.main_window())
        it = V.wait_log(r'ANNOTATE entry veil destroyed after (\d+) ms[^\n]*', 4, mark); off = V.wait_log(r'ANNOTATE mode off', 1, mark)
        step('5.2 an exit during the entry fades the veil from its value (destroyed before 500 ms)', it is not None and int(it.group(1)) < 480 and on is not None and off is not None, it.group(0) if it else 'no veil line')
        if V.mode_active():
            V.mode(False)
    finally:
        V.quit_browser(p)
    # ---- zoom 150 %: labels in CSS px, same cursor size
    write_settings(**{'zoom-percent': 150, 'annotation-connect-prompt': 0})
    p = V.launch(fresh=False)
    try:
        V.topmost(True); V.mode(True); time.sleep(0.8); later()
        dpi = user32.GetDpiForWindow(Q.main_window()); dpr = Q.evaluate('devicePixelRatio')
        step('zoom 150 %: the page is zoomed', abs(dpr / (dpi / 96) - 1.5) < 0.05, dpr)
        got = inspector_dom('form.checkout', 'form.checkout at 150 % zoom')
        step('zoom 150 %: value labels in CSS px (gap « 6 », padding none)', got is not None and 'g6' in got[2], got[2] if got else None)
        c = css_cursor('h1'); size = re.search(r'width%3D%27(\d+)%27', c or ''); res = re.search(r'\)\s*([0-9.]+)(x|dppx)\)', c or '')
        expect = 64 if dpi / 96 >= 1.75 else 48 if dpi / 96 >= 1.25 else 32
        if desktop_ok():
            old = W.POINT(); user32.GetCursorPos(ctypes.byref(old))
            try:
                foreground(); rc = V.rect_of('h1'); real = cursor_at(*css_to_screen(rc[0], rc[1])); report['measures']['cursorZoom150'] = real
                step('zoom 150 %: real Windows cursor, same 48 px image and hot spot as at 100 %', is_ours(real) and real.get('size') == report['measures'].get('cursors', {}).get('texte (h1)', {}).get('size') and real.get('hot') == report['measures'].get('cursors', {}).get('texte (h1)', {}).get('hot'), real)
            finally:
                user32.SetCursorPos(old.x, old.y)
        step('zoom 150 %: the cursor keeps its size (same image, image-set resolution = the screen scale, not the zoomed pixel ratio)',
             size is not None and int(size.group(1)) == expect and res is not None and abs(float(res.group(1)) - dpi / 96) < 0.01, {'size': size.group(0) if size else None, 'resolution': res.group(0) if res else (c or '')[-60:], 'screenScale': dpi / 96, 'pageRatio': dpr})
        V.mode(False)
    finally:
        V.quit_browser(p)
    # ---- reduced motion: veil without texture (~300 ms), still band
    write_settings(**{'reduce-motion': 1, 'annotation-connect-prompt': 0})
    p = V.launch(fresh=False)
    try:
        V.topmost(True); mark = len(V.log_text()); V.mode(True)
        red = V.wait_log(r'ANNOTATE entry veil destroyed after (\d+) ms[^\n]*', 4, mark)
        step('reduced motion: veil without texture, out by ~300 ms', red is not None and 280 <= int(red.group(1)) <= 420, red.group(0) if red else None)
        time.sleep(1); measure('modeReduced', 15)
        V.mode(False)
    finally:
        V.quit_browser(p)

def run():
    QA.mkdir(parents=True, exist_ok=True)
    before = V.fingerprint(); V.prepare_agents()
    server = subprocess.Popen([sys.executable, str(ROOT / 'sandbox/annotations/serve.py')], cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        time.sleep(1); scenario()
    except BaseException:
        step('driver', False, traceback.format_exc())
    finally:
        server.terminate()
        try:
            server.wait(timeout=5)
        except subprocess.TimeoutExpired:
            server.kill()
        alive = subprocess.run(['tasklist', '/FI', f'IMAGENAME eq browser.exe', '/FO', 'CSV'], capture_output=True, text=True).stdout
        mine = [l for l in alive.splitlines() if str(V.APP) in l]
        after = V.fingerprint()
        step('real Claude/Codex configurations unchanged (fingerprints before/after)', before == after, after)
        report['fingerprints'] = {'before': before, 'after': after}
        (QA / 'report.json').write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf-8')
        fails = [s for s in report['steps'] if not s['ok']]
        print(f"\n{len(report['steps'])} steps, {len(fails)} failed -> {QA / 'report.json'}")

def idle():
    """Rest outside the mode after one use: V3-A reference build vs this build, same protocol, 3 runs each."""
    QA.mkdir(parents=True, exist_ok=True)
    before = V.fingerprint(); V.prepare_agents()
    server = subprocess.Popen([sys.executable, str(ROOT / 'sandbox/annotations/serve.py')], cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    out = {}
    try:
        time.sleep(1)
        for variant, src in (('v3a', ROOT / 'build' / 'annotations-v3a-ref'), ('v3b', ROOT / 'build' / 'annotations-v3-preview')):
            for run_ in range(3):
                Q.SRC = src; shutil.rmtree(V.APP, ignore_errors=True)
                p = V.launch()
                try:
                    V.topmost(True); foreground(); write_ok = True
                    V.mode(True); time.sleep(0.8)
                    if 'AnnotationConnectLater' in V.uia_list():
                        V.invoke('AnnotationConnectLater'); time.sleep(0.4)
                    time.sleep(1.5); V.mode(False); time.sleep(5)
                    m = measure(f'idle-{variant}-{run_}', 20)
                    out.setdefault(variant, []).append({k: m[k] for k in ('cpuMs', 'percentOfOneCore', 'contextSwitches', 'switchesPerSecond', 'threads')})
                    V.topmost(False)
                finally:
                    V.quit_browser(p)
    finally:
        Q.SRC = ROOT / 'build' / 'annotations-v3-preview'
        server.terminate()
        after = V.fingerprint()
        step('real Claude/Codex configurations unchanged (idle runs)', before == after, after)
        report['idle'] = out
        (QA / 'idle.json').write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf-8')
        for k, v in out.items():
            print(k, v)

if __name__ == '__main__':
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding='utf-8')
        except Exception:
            pass
    if len(sys.argv) > 1 and sys.argv[1] == 'run':
        run()
    elif len(sys.argv) > 1 and sys.argv[1] == 'idle':
        idle()
    else:
        print(__doc__)
