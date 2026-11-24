#!/usr/bin/env python3
"""Shared helpers of the final QA (design/chat-comment-final-plan.md, phase 3): real windows, real input (SendInput), the
fake agent, captures, the host's log (prototype.log) — and the plan's invariants, checked by `Invariants.check()`.

Used by tests/chat/final/journeys.py (one test per sequence the user lived, R1 to R48) and tests/chat/final/monkey.py
(300+ random actions, seeded). Same guards as tests/chat/chat_qa.py: a COPY of the build (build/chat-qa-app), its own
profile, agent configurations redirected under build/, BROWSER_ANNOTATIONS_TEST=1, the fake agent only, the real Claude /
Codex files fingerprinted before and after. One real-window suite at a time.

    CHAT_QA_BUILD=<folder under build/>   the build to copy (default chat-preview); its fake agent is <build>-test/fake-acp-agent.exe
    FINAL_FAKE_AGENT=<exe>                another fake agent (e.g. build/chat-qa/final/tools/fake-acp-agent.exe)
"""
from __future__ import annotations
import ctypes
import ctypes.wintypes as W
import json
import os
import pathlib
import re
import subprocess
import sys
import threading
import time
import traceback

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import chat_qa as C            # noqa: E402
from chat_qa import Q          # noqa: E402
import chat_qa_comment as CC   # noqa: E402
import chat_qa_comment_v2 as V   # noqa: E402
import chat_qa_comment_v3 as V3  # noqa: E402
import chat_qa_comment_v4b as V4  # noqa: E402

u32 = ctypes.windll.user32
k32 = ctypes.windll.kernel32
OUT = C.QA / 'final'
FLOG = C.WORK / 'fake-agent.log'
# The user's window (test of 20 November): the page area is about 1470 × 665 (CSS pixels).
USER_PAGE = (1470, 665)
# A real edit of the landing page the fake agent can do and undo (« apply <spec> » in a message).
GAP_TO_8 = 'styles/landing.css|display: flex; gap: 4px;|display: flex; gap: 8px;'
GAP_TO_4 = 'styles/landing.css|display: flex; gap: 8px;|display: flex; gap: 4px;'
LANDING = C.URL + 'landing.html'
# Elements of landing.html the tests click (the first match of each selector).
ELEMENTS = ['section.hero', 'div.plans', 'a.btn-primary', 'h1.hero-title', 'div.hero-visual', 'p.hero-text', 'h2.section-title',
            'div.plan-featured', 'div.feature', 'footer.footer']
BIG = ['section.hero', 'div.plans']


# #130, variant C (Paper « #130 Bulle sans place dessous · C règles (validées) », design/annotation-plan-2026-11-24.md §4.4).
# Rules 1 to 4 are in the product (PR #133) and asserted. The others are asserted once their lot is done — the lot flips
# its flag here (tests/annotation-general/coverage.json keeps a gap for each until then):
C130_LOT1_RULES = True      # rules 7 to 10 (lot 1, coded): no page auto-scroll, Refuser / Autoriser in the footer, no free side, never over the element
C130_SCROLL_RULES = False   # rules 5, 6 and 11 (lot 4): the place recomputed while the page scrolls, a 120 ms slide


def scroll_y():
    '''The site page's vertical scroll (CSS px), None when it cannot be read.'''
    try:
        return float(page('window.scrollY'))
    except Exception:
        return None


def window_at(point):
    '''O3 (« clic sans bulle »): the window a click at this screen point really reaches — its class, whether it is one of
    this browser's, and the page element under it. A click that opened nothing says where it went.'''
    try:
        u32.WindowFromPoint.restype = W.HWND
        h = u32.WindowFromPoint(W.POINT(int(point[0]), int(point[1])))
        cls = ctypes.create_unicode_buffer(128); u32.GetClassNameW(h, cls, 128)
        root_h = u32.GetAncestor(h, 2); rcls = ctypes.create_unicode_buffer(128); u32.GetClassNameW(root_h, rcls, 128)   # GA_ROOT
        fg = u32.GetForegroundWindow(); fcls = ctypes.create_unicode_buffer(128); u32.GetClassNameW(fg, fcls, 128)
        try:
            el = element_at(point)
        except Exception:
            el = None
        return {'window': cls.value, 'topWindow': rcls.value, 'foreground': fcls.value, 'pageElementBox': el}
    except Exception as e:
        return {'error': str(e)}


def fake_agent_exe() -> pathlib.Path:
    p = pathlib.Path(os.environ['FINAL_FAKE_AGENT']) if os.environ.get('FINAL_FAKE_AGENT') else C.TOOLS / 'fake-acp-agent.exe'
    return p if p.is_absolute() else C.ROOT / p


# ---------------------------------------------------------------- report
class Report:
    """Steps (PASS / FAIL), measures, captures; on a failure: a capture, the log's end, the chat page's errors."""
    def __init__(self, name):
        self.name = name; self.dir = OUT / name; self.dir.mkdir(parents=True, exist_ok=True)
        self.data = {'steps': [], 'measures': {}, 'captures': [], 'failures': []}
        self.context = {}   # merged into every failure file (monkey: seed, step, action, how to replay)

    def step(self, name, ok, detail='', where=None):
        ok = bool(ok)
        self.data['steps'].append({'step': name, 'ok': ok, 'detail': _short(detail)})
        print(('PASS ' if ok else 'FAIL ') + name + (' — ' + _short(detail, 600) if detail and not ok else ''), flush=True)
        if not ok:
            self.failure(name, detail, where)
        return ok

    def capture(self, label):
        try:
            path = C.grab(f'final/{self.name}/{label}')
            self.data['captures'].append(path.name); return path
        except BaseException as e:   # Q.front() exits when the window is gone: never stop the run for a capture
            print('capture failed', label, e); return None

    def failure(self, name, detail, where=None):
        n = len(self.data['failures']) + 1
        shot = self.capture(f'echec-{n:02d}')
        tail = C.log_text()[-6000:].splitlines()[-60:]
        f = {'n': n, 'step': name, 'detail': _short(detail, 4000), 'capture': shot.name if shot else None, 'logTail': tail,
             'pageErrors': ERRORS.peek(), **self.context, **(where or {})}
        self.data['failures'].append(f)
        (self.dir / f'echec-{n:02d}.json').write_text(json.dumps(f, indent=2, ensure_ascii=False, default=str), encoding='utf-8')

    def save(self):
        (self.dir / 'report.json').write_text(json.dumps(self.data, indent=2, ensure_ascii=False, default=str), encoding='utf-8')

    @property
    def failed(self):
        return [s['step'] for s in self.data['steps'] if not s['ok']]


def _short(x, n=1500):
    s = x if isinstance(x, str) else json.dumps(x, ensure_ascii=False, default=str)
    return s if len(s) <= n else s[:n] + '…'


# ---------------------------------------------------------------- the chat page's two WebViews (bubble / panel)
_ws = {}


def _targets(prefix):
    try:
        return [t for t in C.targets() if t.get('type') == 'page' and t.get('url', '').startswith(prefix)]
    except Exception:
        return []


def _ws_for(target):
    w = _ws.get(target['id'])
    if w is None:
        w = Q.WS(target['webSocketDebuggerUrl']); _ws[target['id']] = w
        try:   # errors from the next loads too (kept while this connection lives)
            w.call('Page.enable'); w.call('Page.addScriptToEvaluateOnNewDocument', {'source': ERROR_HOOK})
        except Exception:
            pass
    return w


def chat_target(kind='bubble'):
    """The chat page in the bubble or in the panel. Both load the same address: the page's root carries the class « bub »
    in the bubble's layout (panel.js applyLayout)."""
    ts = _targets(C.PANEL)
    if len(ts) <= 1:
        return ts[0] if ts else None
    bub = []
    for t in ts:
        try:
            v = _ws_for(t).call('Runtime.evaluate', {'expression': "document.documentElement.classList.contains('bub')", 'returnByValue': True})
            bub.append(bool(v.get('result', {}).get('value')))
        except Exception:
            _ws.pop(t['id'], None); bub.append(False)
    b = [t for t, x in zip(ts, bub) if x]; p = [t for t, x in zip(ts, bub) if not x]
    pick = (b or p) if kind == 'bubble' else (p or b)
    return pick[0] if pick else None


def evaluate_on(target, js):
    if target is None:
        raise RuntimeError('no target')
    for attempt in (0, 1):
        try:
            r = _ws_for(target).call('Runtime.evaluate', {'expression': js, 'returnByValue': True, 'awaitPromise': True})
            if 'exceptionDetails' in r:
                raise RuntimeError(json.dumps(r['exceptionDetails'])[:600])
            return r.get('result', {}).get('value')
        except (OSError, EOFError, ValueError):
            _ws.pop(target['id'], None)
            if attempt:
                raise


def chat(js, kind='bubble'):
    return evaluate_on(chat_target(kind), js)


def page(js):
    ts = _targets(C.URL)
    return evaluate_on(ts[0] if ts else None, js)


def page_ws():
    ts = _targets(C.URL)
    return _ws_for(ts[0]) if ts else None


# ---------------------------------------------------------------- JavaScript errors (chat page, page)
ERROR_HOOK = ("(function(){if(window.__qaE)return;window.__qaE=[];var p=function(s){try{window.__qaE.push(String(s).slice(0,400))}catch(e){}};"
              "addEventListener('error',function(e){p('error: '+(e.message||(e.target&&e.target.src)||e.type)+' @'+(e.filename||'')+':'+(e.lineno||''))},true);"
              "addEventListener('unhandledrejection',function(e){p('rejection: '+(e.reason&&e.reason.stack||e.reason))});"
              "var ce=console.error;console.error=function(){p('console.error: '+Array.prototype.map.call(arguments,String).join(' '));return ce.apply(this,arguments)}})()")
TAKE_ERRORS = "(function(){" + ERROR_HOOK[len('(function(){'):-len('})()')] + ";return window.__qaE.splice(0)})()"


class _Errors:
    def __init__(self):
        self.seen = []

    def collect(self):
        """New JavaScript errors of the chat pages and the page since the last call (installs the hook where missing)."""
        new = []
        for t in _targets(C.PANEL) + _targets(C.URL)[:1]:
            try:
                for e in evaluate_on(t, TAKE_ERRORS) or []:
                    new.append(('chat ' if t['url'].startswith(C.PANEL) else 'page ') + e)
            except Exception:
                pass
        self.seen += new
        return new

    def peek(self):
        return self.seen[-20:]


ERRORS = _Errors()

# Lines of the host's log that are never normal (an exception, a crash, a failed renderer, a refused creation)
BAD_LOG = re.compile(r'exception|assert|access violation|renderer failed|creation refused|unhandled|std::|terminate called|stack overflow|bubble drawn away from its region|R39: a scroll step changed the bubble.s frame', re.I)


def log_mark():
    return len(C.log_text())


def bad_log_lines(after):
    return [l for l in C.log_text()[after:].splitlines() if BAD_LOG.search(l)]


def wait_log(pattern, after=0, timeout=15.0):
    return C.wait_log(pattern, timeout, after)


# ---------------------------------------------------------------- real input
class _MI(ctypes.Structure):
    _fields_ = [('dx', W.LONG), ('dy', W.LONG), ('mouseData', W.DWORD), ('dwFlags', W.DWORD), ('time', W.DWORD), ('dwExtraInfo', ctypes.c_size_t)]


class _KI(ctypes.Structure):
    _fields_ = [('wVk', W.WORD), ('wScan', W.WORD), ('dwFlags', W.DWORD), ('time', W.DWORD), ('dwExtraInfo', ctypes.c_size_t)]


class _IN(ctypes.Structure):
    class _U(ctypes.Union):
        _fields_ = [('mi', _MI), ('ki', _KI), ('pad', ctypes.c_byte * 32)]
    _anonymous_ = ('u',)
    _fields_ = [('type', W.DWORD), ('u', _U)]


def mouse(flags, data=0):
    i = _IN(); i.type = 0; i.mi = _MI(0, 0, ctypes.c_ulong(data & 0xFFFFFFFF).value, flags, 0, 0)
    u32.SendInput(1, ctypes.byref(i), ctypes.sizeof(_IN))


def key(vk, up=False):
    i = _IN(); i.type = 1; i.ki = _KI(vk, 0, 0x0002 if up else 0, 0, 0)
    u32.SendInput(1, ctypes.byref(i), ctypes.sizeof(_IN))


def tap(vk, ctrl=False, shift=False):
    if ctrl: key(0x11)
    if shift: key(0x10)
    key(vk); time.sleep(0.02); key(vk, True)
    if shift: key(0x10, True)
    if ctrl: key(0x11, True)


def click_at(x, y, settle=0.08):
    """A real click; returns the time of the button's release (what the user sees as « the click »)."""
    u32.SetCursorPos(int(x), int(y)); time.sleep(settle)
    mouse(0x0002); time.sleep(0.03); mouse(0x0004)
    return time.perf_counter()


def wheel(notches, every=0.05):
    for _ in range(abs(notches)):
        mouse(0x0800, -120 if notches > 0 else 120); time.sleep(every)


def drag(x0, y0, x1, y1, steps=20, every=0.016):
    u32.SetCursorPos(int(x0), int(y0)); time.sleep(0.1); mouse(0x0002); time.sleep(0.05)
    for i in range(1, steps + 1):
        u32.SetCursorPos(int(x0 + (x1 - x0) * i / steps), int(y0 + (y1 - y0) * i / steps)); time.sleep(every)
    time.sleep(0.05); mouse(0x0004); time.sleep(0.3)


def type_keys(text, every=0.02):
    """Real key presses (KEYEVENTF_UNICODE) into the window that has the keyboard."""
    for ch in text:
        for flags in (0x0004, 0x0004 | 0x0002):
            i = _IN(); i.type = 1; i.ki = _KI(0, ord(ch), flags, 0, 0)
            u32.SendInput(1, ctypes.byref(i), ctypes.sizeof(_IN))
        time.sleep(every)


def release_all():
    """Never leave a key or a button down (Ctrl held by a failed step would change everything after it)."""
    for vk in (0x11, 0x10, 0x12):
        if u32.GetAsyncKeyState(vk) & 0x8000:
            key(vk, True)
    mouse(0x0004)


def set_clipboard(text):
    CF_UNICODETEXT = 13; GMEM_MOVEABLE = 2
    k32.GlobalAlloc.restype = W.HGLOBAL; k32.GlobalLock.restype = ctypes.c_void_p; k32.GlobalLock.argtypes = [W.HGLOBAL]; k32.GlobalUnlock.argtypes = [W.HGLOBAL]
    u32.SetClipboardData.argtypes = [W.UINT, W.HANDLE]
    data = (text + '\0').encode('utf-16-le')
    for _ in range(20):
        if u32.OpenClipboard(None):
            break
        time.sleep(0.05)
    try:
        u32.EmptyClipboard(); h = k32.GlobalAlloc(GMEM_MOVEABLE, len(data)); p = k32.GlobalLock(h)
        ctypes.memmove(p, data, len(data)); k32.GlobalUnlock(h); u32.SetClipboardData(CF_UNICODETEXT, h)
    finally:
        u32.CloseClipboard()


def foreground_is_ours():
    fg = u32.GetForegroundWindow(); pid = W.DWORD(); u32.GetWindowThreadProcessId(fg, ctypes.byref(pid))
    return pid.value == Q.our_pid()


front = V.front


class OtherApp:
    """A window of ANOTHER process (a tiny Tk window) brought to the front, then closed."""
    TITLE = 'QA autre application'

    def __enter__(self):
        self.p = subprocess.Popen([sys.executable, '-c', "import tkinter as t;r=t.Tk();r.title('%s');r.geometry('320x160+40+40');r.after(30000,r.destroy);r.mainloop()" % self.TITLE])
        self.h = 0
        for _ in range(60):
            self.h = u32.FindWindowW(None, self.TITLE)
            if self.h:
                break
            time.sleep(0.1)
        for _ in range(10):
            u32.keybd_event(0x12, 0, 0, 0); u32.keybd_event(0x12, 0, 2, 0); u32.SetForegroundWindow(self.h); time.sleep(0.1)
            if u32.GetForegroundWindow() == self.h:
                break
        return self

    def in_front(self):
        return bool(self.h) and u32.GetForegroundWindow() == self.h

    def __exit__(self, *a):
        try:
            self.p.kill(); self.p.wait(3)
        except Exception:
            pass


# ---------------------------------------------------------------- geometry
def page_area():
    return CC.page_widget()


def dpr():
    try:
        return float(page('devicePixelRatio') or 1)
    except Exception:
        return 1.0


def element_screen(sel):
    return CC.element_screen(sel)


def element_point(sel, avoid=None):
    """A screen point of `sel` where the element ITSELF is under the pointer (not one of its children) and that is visible:
    what the user clicks to select a big element (its padding or a gap). `avoid`: a screen box to stay out of (the bubble)."""
    js = ("(function(){var e=document.querySelector(%s);if(!e)return null;var r=e.getBoundingClientRect();var out=[];"
          "var x0=Math.max(r.left,0)+3,x1=Math.min(r.right,innerWidth)-3,y0=Math.max(r.top,0)+3,y1=Math.min(r.bottom,innerHeight)-3;"
          "for(var j=0;j<=12;j++)for(var i=0;i<=16;i++){var x=x0+(x1-x0)*i/16,y=y0+(y1-y0)*j/12;var h=document.elementFromPoint(x,y);if(h===e)out.push([x,y])}"
          "if(!out.length){var c=[(x0+x1)/2,(y0+y1)/2];out.push(c)}return {pts:out,dpr:devicePixelRatio}})()") % json.dumps(sel)
    r = page(js)
    if not r:
        return None
    pg = page_area(); k = r['dpr']
    pts = [(int(pg[0] + x * k), int(pg[1] + y * k)) for x, y in r['pts']]
    boxes = [avoid] if avoid else []
    oa = u32.FindWindowW(None, OtherApp.TITLE)   # never under the other application's window: the click would reach it, not the page
    if oa and u32.IsWindowVisible(oa):
        boxes.append(tuple(V3.win_rect(oa)))
    for bx in boxes:
        pts = [p for p in pts if not (bx[0] - 4 <= p[0] <= bx[2] + 4 and bx[1] - 4 <= p[1] <= bx[3] + 4)] or pts
    pts = [p for p in pts if pg[0] + 4 <= p[0] <= pg[2] - 20 and pg[1] + 4 <= p[1] <= pg[3] - 4] or pts   # never the scrollbar
    return pts[len(pts) // 2]


EMBED_JS = ("(function(){if(document.getElementById('qa-frame'))return 0;var h=document.querySelector('section.hero');"
            "var f=document.createElement('iframe');f.id='qa-frame';f.style.cssText='display:block;width:380px;height:150px;margin:24px auto;border:0';"
            "f.srcdoc='<body style=\\'margin:0;background:#f4efe6;font:16px sans-serif\\'><div id=\\'qa-in\\' style=\\'margin:22px;padding:18px;background:#fff\\'>Bloc dans un iframe</div></body>';"
            "h.after(f);if(!customElements.get('qa-card'))customElements.define('qa-card',class extends HTMLElement{constructor(){super();"
            "this.attachShadow({mode:'open'}).innerHTML='<div id=\\'qa-sh\\' style=\\'padding:20px;margin:14px auto;width:320px;background:#e8f0ff;font:16px sans-serif\\'>Carte composant web</div>'}});"
            "var c=document.createElement('qa-card');c.id='qa-card';c.style.display='block';f.after(c);return 1})()")
EMBEDDED = {'#qa-frame': "(function(){var f=document.getElementById('qa-frame');if(!f||!f.contentDocument)return null;var a=f.getBoundingClientRect(),"
                         "b=f.contentDocument.getElementById('qa-in').getBoundingClientRect();return [a.left+b.left+b.width/2,a.top+b.top+b.height/2]})()",
            '#qa-card': "(function(){var c=document.getElementById('qa-card');if(!c||!c.shadowRoot)return null;var b=c.shadowRoot.getElementById('qa-sh').getBoundingClientRect();"
                        "return [b.left+b.width/2,b.top+b.height/2]})()"}


EMBEDDED_BOX = {'#qa-frame': "(function(){var f=document.getElementById('qa-frame');var a=f.getBoundingClientRect(),b=f.contentDocument.getElementById('qa-in').getBoundingClientRect();"
                             "return [a.left+b.left,a.top+b.top,a.left+b.right,a.top+b.bottom]})()",
                '#qa-card': "(function(){var b=document.getElementById('qa-card').shadowRoot.getElementById('qa-sh').getBoundingClientRect();return [b.left,b.top,b.right,b.bottom]})()"}


def inject_embedded():
    """The iframe (same origin) and the web component; again after a reload (they are not in the site's files)."""
    r = page(EMBED_JS); time.sleep(0.5 if r else 0); return r


def embedded_point(host):
    """Screen point of the block INSIDE the iframe / the shadow root of `host` (its anchor is the host); never under the
    open bubble (the click would reach the bubble, not the page): then the page is scrolled to free it."""
    inject_embedded()
    p = None
    for _ in range(4):
        r = page(EMBEDDED[host])
        if not r:
            return None
        pg = page_area(); k = dpr(); p = (int(pg[0] + r[0] * k), int(pg[1] + r[1] * k))
        shown, rb = bubble_shown()
        if not (shown and isinstance(rb, tuple) and rb[0] - 6 <= p[0] <= rb[2] + 6 and rb[1] - 6 <= p[1] <= rb[3] + 6):
            return p
        u32.SetCursorPos(pg[0] + 20, (pg[1] + pg[3]) // 2); wheel(2 if p[1] < (pg[1] + pg[3]) // 2 else -2, 0.05); time.sleep(0.6)
    return p


def keyboard_to_page(avoid=None):
    """The site's page gets the keyboard WITHOUT selecting anything: a Ctrl+click on a plain text (R31: Ctrl held = the
    site as usual). The page's right edge has no real scrollbar to click (its width is 0 here)."""
    front()
    for sel in ('p.hero-text', 'h2.section-title', 'footer.footer'):
        if page("(function(){var e=document.querySelector(%s);if(!e)return false;var r=e.getBoundingClientRect();return r.bottom>20&&r.top<innerHeight-20})()" % json.dumps(sel)):
            p = element_point(sel, avoid=avoid); key(0x11)
            try:
                click_at(*p, settle=0.1)
            finally:
                key(0x11, True)
            time.sleep(0.2); return sel
    return None


def scrollbar_width():
    return page('innerWidth - document.documentElement.clientWidth') or 0


def element_at(point):
    """Screen rectangle of the deepest page element under a screen point (what a click there selects)."""
    pg = page_area(); k = dpr()
    x, y = (point[0] - pg[0]) / k, (point[1] - pg[1]) / k
    r = page("(function(){var e=document.elementFromPoint(%f,%f);if(!e||e===document.body||e===document.documentElement)return null;var b=e.getBoundingClientRect();return [b.left,b.top,b.right,b.bottom]})()" % (x, y))
    return [int(pg[0] + r[0] * k), int(pg[1] + r[1] * k), int(pg[0] + r[2] * k), int(pg[1] + r[3] * k)] if r else None


def scroll_into_view(sel, where='center'):
    page("(function(){var e=document.querySelector(%s);if(e)e.scrollIntoView({block:%s,behavior:'instant'});return 1})()" % (json.dumps(sel), json.dumps(where)))
    time.sleep(0.5)


def bubble_hwnd():
    b = V.bubble_hwnd()
    return b[0][0] if b else 0


def bubble_windows():
    """Every BrowserChatBubble window of OUR process (there must be one at most)."""
    out = []; h = 0
    while True:
        h = u32.FindWindowExW(None, h, 'BrowserChatBubble', None)
        if not h:
            return out
        pid = W.DWORD(); u32.GetWindowThreadProcessId(h, ctypes.byref(pid))
        if pid.value == Q.our_pid():
            out.append(h)


def region_box(h):
    if not h:
        return None
    w0 = V3.win_rect(h); rg = W.RECT(); kind = u32.GetWindowRgnBox(h, ctypes.byref(rg)); wr = V3.win_rect(h)
    if wr != w0:
        return 'moving'
    if kind == 1:   # NULLREGION
        return None
    if kind == 0:   # no region: the whole window
        return tuple(wr)
    return (wr[0] + rg.left, wr[1] + rg.top, wr[0] + rg.right, wr[1] + rg.bottom)


def bubble_shown():
    """(visible, region) of our bubble window: shown = visible and a non-empty region."""
    h = bubble_hwnd()
    if not h:
        return False, None
    vis = bool(u32.IsWindowVisible(h)); rb = region_box(h)
    return vis and isinstance(rb, tuple) and rb[2] - rb[0] > 40 and rb[3] - rb[1] > 30, rb


STATE_JS = ("(function(){var a=document.getElementById('app')||document.body;var r=a.getBoundingClientRect();var s=document.querySelector('.scroller');"
            "var mo=document.querySelector('button.tb.mode'),md=document.querySelector('button.m');var pm=document.querySelector('.bperm,.perm');"
            "var vis=function(e){if(!e)return null;var q=e.getBoundingClientRect();return [q.left,q.top,q.right,q.bottom]};"
            "var btns=pm?Array.prototype.slice.call(pm.querySelectorAll('button')).filter(function(b){return /Refuser|Autoriser/.test(b.textContent)}).map(function(b){var q=b.getBoundingClientRect();"
            "var op=1,e=b;while(e&&e!==document.body){op*=+getComputedStyle(e).opacity;e=e.parentElement}return {t:b.textContent.trim(),r:[q.left,q.top,q.right,q.bottom],op:op}}):[];"
            "var menus=Array.prototype.slice.call(document.querySelectorAll('.menu:not(.menu-ghost)')).filter(function(m){return !m.hidden&&getComputedStyle(m).display!=='none'}).length;"
            "return {app:[r.left,r.top,r.right,r.bottom],dpr:devicePixelRatio,w:innerWidth,h:innerHeight,"
            "scroller:s?{sh:s.scrollHeight,ch:s.clientHeight,st:s.scrollTop}:null,"
            "mode:mo?mo.getAttribute('aria-label'):null,model:md?md.getAttribute('aria-label'):null,"
            "perm:pm?{box:vis(pm),buttons:btns}:null,menus:menus,diff:!!document.querySelector('.b-grip:not([hidden])'),"
            "opacity:+getComputedStyle(a).opacity,busy:/Arrêter/.test(document.body.innerText||''),"
            "text:(document.body.textContent||'').replace(/\\s+/g,' ')}})()")


def chat_state(kind='bubble'):
    try:
        return chat(STATE_JS, kind)
    except Exception as e:
        return {'error': str(e)[:200]}


def bubble_surface(st=None):
    """The bubble's surface (#app) on screen, from its window and the page's state."""
    h = bubble_hwnd(); st = st or chat_state('bubble')
    if not h or not st or 'app' not in st:
        return None
    wr = V3.win_rect(h); k = st['dpr']
    a = st['app']
    return (int(wr[0] + a[0] * k), int(wr[1] + a[1] * k), int(wr[0] + a[2] * k), int(wr[1] + a[3] * k))


def drawn_share(box):
    """Share of sampled pixels of `box` that are the bubble's dark surface (an empty / white / grey bubble is not)."""
    if not box or box[2] - box[0] < 12 or box[3] - box[1] < 12:
        return 0.0
    w_, h_, raw = V4.fast_grab(box); n = dk = 0
    for y in range(4, h_ - 4, 6):
        for x in range(4, w_ - 4, 6):
            i = (y * w_ + x) * 4; n += 1; dk += (raw[i] + raw[i + 1] + raw[i + 2]) < 200
    return round(dk / max(1, n), 2)


def intersect(a, b):
    r = (max(a[0], b[0]), max(a[1], b[1]), min(a[2], b[2]), min(a[3], b[3]))
    return r if r[2] > r[0] and r[3] > r[1] else None


def wait_bubble_shown(timeout=3.0):
    """ms until the bubble's window is visible with a region (polled fast), or None."""
    t0 = time.perf_counter()
    while time.perf_counter() - t0 < timeout:
        ok, _ = bubble_shown()
        if ok:
            return round((time.perf_counter() - t0) * 1000)
        time.sleep(0.005)
    return None


# ---------------------------------------------------------------- the page's drawing (closed shadow root, through DevTools)
def overlay_nodes():
    ws = page_ws()
    if ws is None:
        return []
    ws.call('DOM.enable'); root = ws.call('DOM.getDocument', {'depth': -1, 'pierce': True})['root']
    out = []
    def walk(n):
        a = n.get('attributes', []); d = dict(zip(a[::2], a[1::2]))
        if d.get('class'):
            out.append({'cls': d['class'], 'style': d.get('style', ''), 'text': ''.join(c.get('nodeValue', '') for c in n.get('children', []) if c.get('nodeType') == 3)})
        for k in n.get('children', []) + n.get('shadowRoots', []):
            walk(k)
    walk(root)
    return out


def conversation_marks(wait_for=None, timeout=4.0):
    """The conversation marks drawn in the page; with `wait_for`, polled until there are that many (they come after the close)."""
    t0 = time.time()
    while True:
        out = [x for x in overlay_nodes() if 'ct' in x['cls'].split()]
        if wait_for is None or len(out) == wait_for or time.time() - t0 > timeout:
            return out
        time.sleep(0.25)


# ---------------------------------------------------------------- UI Automation (one PowerShell for the whole run)
class Uia:
    def __init__(self):
        self.p = subprocess.Popen(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(HERE / 'uia_server.ps1')],
                                  stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, encoding='utf-8', errors='replace')
        self.lock = threading.Lock()

    def ask(self, line, timeout=30.0):
        with self.lock:
            self.p.stdin.write(line + '\n'); self.p.stdin.flush()
            out = []; end = time.time() + timeout; box = {}
            def read():
                while True:
                    l = self.p.stdout.readline()
                    if not l or l.strip() == 'END':
                        break
                    out.append(l)
                box['done'] = True
            t = threading.Thread(target=read, daemon=True); t.start(); t.join(timeout)
            if not box.get('done'):
                return {'error': 'timeout'}
            try:
                return json.loads(''.join(out) or 'null')
            except ValueError:
                return {'error': ''.join(out)[:300]}

    def probe(self, needle=''):
        t0 = time.time(); r = self.ask(f'probe {Q.our_pid()} {needle}'.rstrip()); return r, round(time.time() - t0, 2)

    def bounds(self, ids):
        return self.ask(f'bounds {Q.our_pid()} {",".join(ids)}') or {}

    def close(self):
        try:
            self.p.stdin.write('quit\n'); self.p.stdin.flush(); self.p.wait(5)
        except Exception:
            self.p.kill()


# ---------------------------------------------------------------- the browser
def launch(agent_args='', size=USER_PAGE, url=LANDING, extra_env=None, fresh=True):
    """The QA copy with the fake agent (started in « auto » like the real Claude adapter), the page area set to `size`."""
    try:
        FLOG.unlink()
    except OSError:
        pass
    fake = fake_agent_exe()
    assert fake.is_file(), f'missing {fake}: build first'
    env = {'BROWSER_CHAT_PANEL_IDLE_MS': '60000', 'BROWSER_CHAT_AGENT_IDLE_MS': '600000',
           'BROWSER_CHAT_FAKE_AGENT': f'"{fake}" --resume --claude-like --start-mode auto --log "{FLOG}" {agent_args}'.rstrip()}
    env.update(extra_env or {})
    C.launch(fresh=fresh, reset_site=fresh, extra_env=env)
    _ws.clear()
    if url != C.URL and not (page('location.href') or '').startswith(url):
        ws = page_ws(); ws.call('Page.navigate', {'url': url}); time.sleep(1.5)
        C.wait_for(lambda: page("document.readyState") == 'complete', 15)
    if size:
        set_page_size(*size)
    return True


def set_page_size(w, h):
    """Resizes the main window so that the page area is `w` × `h` CSS pixels (as far as the screen allows)."""
    hw = Q.main_window(); u32.ShowWindow(hw, 9); time.sleep(0.3)
    for _ in range(3):
        pg = page_area(); k = dpr(); wr = V3.win_rect(hw)
        dw, dh = int(w * k) - (pg[2] - pg[0]), int(h * k) - (pg[3] - pg[1])
        if abs(dw) <= 2 and abs(dh) <= 2:
            break
        u32.SetWindowPos(hw, 0, max(0, wr[0]), max(0, wr[1]), wr[2] - wr[0] + dw, wr[3] - wr[1] + dh, 0x0004 | 0x0010)
        time.sleep(0.8)
    pg = page_area(); k = dpr()
    return (round((pg[2] - pg[0]) / k), round((pg[3] - pg[1]) / k))


def mode_on(layout='comment', settle=True):
    """Annotation mode on (Ctrl+Maj+A), the connection screen dismissed, the band's layout chosen. settle=False: as fast
    as the user (C1: a click before the chat page is ready)."""
    front(); m = log_mark(); C.shortcut('A'); wait_log(r'ANNOTATE mode on', m, 10); time.sleep(1.2 if settle else 0.3)
    if C.invoke('AnnotationConnectLater') or C.invoke('AnnotationConnectClose'):
        time.sleep(0.6 if settle else 0.2)
    set_layout(layout, settle)


def set_layout(layout, settle=True):
    front(); m = log_mark()
    C.invoke('AnnotationLayoutComment' if layout == 'comment' else 'AnnotationAskAgent')
    wait_log(r'CHAT layout: ' + ('Commentaire' if layout == 'comment' else 'Panneau'), m, 5)
    time.sleep(1.2 if settle else 0.05)


def finish(report, before):
    """Closes everything; nothing may be left running or open; the real configuration files are untouched."""
    release_all()
    C.quit_all()
    time.sleep(0.8)
    report.step('real Claude / Codex configuration files unchanged', before == C.fingerprints())
    for _ in range(10):
        ps = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq fake-acp-agent.exe', '/NH'], capture_output=True, text=True).stdout
        if 'fake-acp-agent' not in ps:
            break
        time.sleep(0.5)
    report.step('nothing left running (fake agent)', 'fake-acp-agent' not in ps)
    left = leftover_from_app()
    report.step('nothing left running from the QA copy (browser, WebView2)', not left, left)
    report.step('nothing left open (the other application\'s window)', not u32.FindWindowW(None, OtherApp.TITLE))
    report.save()


def leftover_from_app():
    script = ("Get-CimInstance Win32_Process | Where-Object { $_.ExecutablePath -and $_.ExecutablePath.StartsWith('" + str(C.APP).replace("'", "''") +
              "', [System.StringComparison]::OrdinalIgnoreCase) } | ForEach-Object { $_.ProcessId.ToString() + ' ' + $_.Name }")
    r = subprocess.run(['powershell', '-NoProfile', '-Command', script], capture_output=True, text=True, timeout=120)
    return [l for l in r.stdout.splitlines() if l.strip()]


def holds(text, token):
    '''The token is in the text AS A WHOLE: « jt1x1 » is not in « … jt1x13 … ». The monkey numbers its messages (jt<seed>x<n>): from
    the 10th on, a plain `in` found the 1st one's token in every conversation (real-window monkey of 24 Nov 2026, seed 1: 8
    openings said to show « another conversation's message » — the test's, not the product's).
    Only a DIGIT right after the token is refused: chat_state() reads document.body.textContent, where neighbouring elements
    are glued without a space (« …jeton1x435Revenir »), so a letter on either side is normal (20:00 release, 24 Nov).'''
    return bool(token) and re.search(re.escape(token) + r'(?![0-9])', text or '') is not None


assert holds('vrai-lent jt1x13 et jt1x14', 'jt1x13') and not holds('vrai-lent jt1x13 et jt1x14', 'jt1x1') and holds('« jt1x1 »', 'jt1x1')


# ---------------------------------------------------------------- the chat, through real input
def chat_point(selector=None, text=None, kind='bubble'):
    """Screen point of a control of the chat page (by selector, or by its visible text / accessible name)."""
    find = (f"document.querySelector({json.dumps(selector)})" if selector else
            "(function(){var t=" + json.dumps(text) + ";function vis(e){var r=e.getBoundingClientRect();return r.width>0&&r.height>0}"
            "var c=Array.from(document.querySelectorAll('button,[role=button],[role=menuitem],[role=option],[role=menuitemradio],a')).filter(function(e){return vis(e)&&"
            "(((e.innerText||'')+' '+(e.getAttribute('aria-label')||'')).indexOf(t)>=0)});return c.sort(function(a,b){return a.innerText.length-b.innerText.length})[0]||null})()")
    r = chat(f"(function(){{var e={find};if(!e)return null;var r=e.getBoundingClientRect();if(!r.width)return null;return [r.left+r.width/2,r.top+r.height/2,devicePixelRatio]}})()", kind)
    if not r:
        return None
    h = bubble_hwnd() if kind == 'bubble' else 0
    if kind == 'bubble' and h:
        wr = V3.win_rect(h); return (int(wr[0] + r[0] * r[2]), int(wr[1] + r[1] * r[2]))
    # the panel: its WebView's window (the largest chat child of the main window)
    pr = panel_widget()
    return (int(pr[0] + r[0] * r[2]), int(pr[1] + r[1] * r[2])) if pr else None


def panel_widget():
    out = []
    @ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    def cb(h, _):
        n = ctypes.create_unicode_buffer(128); u32.GetClassNameW(h, n, 128)
        if n.value == 'Chrome_RenderWidgetHostHWND' and u32.IsWindowVisible(h):
            r = W.RECT(); u32.GetWindowRect(h, ctypes.byref(r)); out.append((r.left, r.top, r.right, r.bottom))
        return True
    u32.EnumChildWindows(Q.main_window(), cb, 0)
    pg = page_area()
    out = [r for r in out if r != pg]
    return sorted(out, key=lambda r: r[2] - r[0])[-1] if out else None


def real_click_chat(selector=None, text=None, kind='bubble', tries=12):
    if text == 'Autoriser':
        time.sleep(0.7)   # SEC-23: a permission card takes no « Autoriser » in its first 600 ms
    for _ in range(tries):
        p = chat_point(selector, text, kind)
        if p and kind == 'bubble':
            p = bring_into_view(p, selector, text)   # the bubble may go past the visible area (it grows in place): the user scrolls
        if p:
            click_at(*p); return True
        time.sleep(0.25)
    return False


def bring_into_view(p, selector=None, text=None, tries=10):
    """A point of the bubble below / above the page area: the page is scrolled with the wheel (the bubble follows its
    element) until the point is in it, then read again."""
    for _ in range(tries):
        pg = page_area()
        if p is None or pg[1] + 6 <= p[1] <= pg[3] - 6:
            return p
        u32.SetCursorPos(pg[0] + 20, (pg[1] + pg[3]) // 2); time.sleep(0.05)
        wheel(1 if p[1] > pg[3] - 6 else -1, 0.05); time.sleep(0.5)
        p = chat_point(selector, text, 'bubble')
    return p


def composer_end_point(kind='bubble'):
    """Screen point just after the end of the composer's content (the user clicks there to write; the middle of the
    composer may be a chip, and a click on a chip removes it)."""
    r = chat("(function(){var c=document.querySelector('[contenteditable=true]');if(!c)return null;var b=c.getBoundingClientRect();"
             "var g=document.createRange();g.selectNodeContents(c);g.collapse(false);var rs=g.getClientRects();var q=rs.length?rs[rs.length-1]:null;"
             "var x=q?q.right+6:b.left+12,y=q?q.top+q.height/2:b.top+12;if(x>b.right-6){x=b.right-10}"
             "var h=document.elementFromPoint(x,y);if(h&&h.closest&&h.closest('.chip')){x=b.right-10;y=b.bottom-6}"
             "return [x,y,devicePixelRatio]})()", kind)
    if not r:
        return None
    if kind == 'bubble':
        wr = V3.win_rect(bubble_hwnd()); return (int(wr[0] + r[0] * r[2]), int(wr[1] + r[1] * r[2]))
    pr = panel_widget()
    return (int(pr[0] + r[0] * r[2]), int(pr[1] + r[1] * r[2])) if pr else None


def focus_composer(kind='bubble'):
    return chat("(function(){var t=document.querySelector('[contenteditable=true]');if(!t)return 0;t.focus();"
                "var s=getSelection();s.selectAllChildren(t);s.collapseToEnd();return 1})()", kind)


def type_and_send(text, kind='bubble'):
    """A real click in the composer (the user's way to give it the keyboard), then the message typed (CDP insertText, as
    the keyboard would) and Entrée."""
    if kind == 'panel':   # a real click at the END of the composer's text (never on a chip), then real keys, as the user
        p = composer_end_point(kind)
        if p:
            click_at(*p); time.sleep(0.15)
        focus_composer(kind)
        type_keys(text, every=0.008); time.sleep(0.15); tap(0x0D)
        return
    focus_composer(kind)
    ws = _ws_for(chat_target(kind))
    ws.call('Input.insertText', {'text': text}); time.sleep(0.15)
    ws.call('Input.dispatchKeyEvent', {'type': 'keyDown', 'key': 'Enter', 'code': 'Enter', 'windowsVirtualKeyCode': 13, 'nativeVirtualKeyCode': 13, 'text': '\r'})
    ws.call('Input.dispatchKeyEvent', {'type': 'keyUp', 'key': 'Enter', 'code': 'Enter', 'windowsVirtualKeyCode': 13, 'nativeVirtualKeyCode': 13})


def escape_chat(kind='bubble'):
    ws = _ws_for(chat_target(kind))
    for t in ('rawKeyDown', 'keyUp'):
        ws.call('Input.dispatchKeyEvent', {'type': t, 'key': 'Escape', 'code': 'Escape', 'windowsVirtualKeyCode': 27, 'nativeVirtualKeyCode': 27})


def close_menus(kind='bubble'):
    for _ in range(3):
        st = chat_state(kind)
        if not st.get('menus'):
            return
        escape_chat(kind); time.sleep(0.3)


def answer_permission(label='Autoriser', kind='bubble', after=0):
    # #130 C rule 7: the page is no longer scrolled to show the card (the bubble is whole, its buttons in its foot). The card
    # only takes a click 600 ms after it appeared (SEC-23): waited for here, as a user reading it would
    time.sleep(0.8)
    for _ in range(16):
        real_click_chat(text=label, kind=kind, tries=2)
        if wait_log(r'CHAT permission answered', after, 1):
            return True
    return False


def labels(kind='bubble'):
    st = chat_state(kind)
    return st.get('mode'), st.get('model')


# ---------------------------------------------------------------- the invariants (design/chat-comment-final-plan.md)
class Model:
    """What the user did, so what must be on screen: the layout, the bubble's state and element, each element's
    conversation token (a word only its own messages hold), the mode / model chosen."""
    def __init__(self):
        self.layout = 'comment'; self.open = None; self.conv = {}; self.mode = None; self.model = None
        self.anchor = None; self.size = None; self.hidden_by_other = False; self.scroll_moved = False
        self.scroll = None   # #130: the page's scroll when the bubble was last placed (opening, new anchor)
        self.panel_marked = set()   # elements picked in the panel then sent from it: they get a mark too
        self.unknown = False        # a bubble of an element the model does not know (a container selected by a click « elsewhere »)


class Invariants:
    def __init__(self, report, uia=None):
        self.r = report; self.uia = uia; self.m = Model(); self.log_at = log_mark(); self.uia_base = None
        self.close_at = self.log_at
        self.allow_close = False   # set by an action that may close the bubble (Échap, click elsewhere, switch, element gone)

    # -- called right after a click on an element in Comment mode (before any other wait)
    def after_element_click(self, sel, t_click, where, mark=None, point=None):
        ms = None
        while time.perf_counter() - t_click < 3.0:
            ok, _ = bubble_shown()
            # the bubble of THIS click: the host said it opened it (the previous bubble may still be on screen)
            if ok and mark is not None and 'CHAT bubble opened on' not in C.log_text()[mark:]:
                ok = False
            if ok:
                ms = round((time.perf_counter() - t_click) * 1000); break
            time.sleep(0.004)
        time.sleep(0.35)
        st = chat_state('bubble'); sf = bubble_surface(st); pg = page_area()
        inside = sf is not None and sf[0] >= pg[0] - 1 and sf[1] >= pg[1] - 1 and sf[2] <= pg[2] + 1 and sf[3] <= pg[3] + 1
        sc = st.get('scroller') or {}
        cap = min(520, (pg[3] - pg[1]) / st['dpr'] - 16) if st.get('dpr') else 520   # #130 C rule 4: at most the page area - 16 px
        if mark is not None:   # #130 C rule 10: below / above an element with no free side, the height its side holds (the host says it at each opening)
            offered = re.findall(r'CHAT bubble: height offered (\d+) CSS px', C.log_text()[mark:])
            if offered:
                cap = min(cap, int(offered[-1]))
        natural = bool(sc) and (sc['sh'] <= sc['ch'] + 2 or (sf and (sf[3] - sf[1]) / st['dpr'] >= cap - 2))
        token = self.m.conv.get(sel)
        others = [t for s, t in self.m.conv.items() if s != sel and t]
        text = st.get('text', '')
        t1 = time.time()
        while token and not holds(text, token) and time.time() - t1 < 1.0:   # the older exchanges are drawn after the first image (P, 176e603)
            time.sleep(0.1); text = (chat_state('bubble').get('text') or '')
        right_conv = (holds(text, token) if token else True) and not any(holds(text, t) for t in others)   # whole tokens: jt1x1 is not in jt1x13
        d = {'ms': ms, 'surface': sf, 'page': pg, 'scroller': sc, 'token': token, 'othersShown': [t for t in others if holds(text, t)]}
        if ms is None and point:   # O3: no bubble came — where the click went (the window under it, the foreground, the page element)
            d['underPoint'] = window_at(point); d['point'] = [int(point[0]), int(point[1])]
        # decision 5 (24 Nov 2026): « in less than 150 ms » is an objective (reported); the bubble on screen within the wait,
        # whole, at its height, with the right conversation, is what fails
        self.r.step(f'{where} · click on {sel}: the bubble is entirely visible (page area), at its height (natural, or the page area - 16 px), the right conversation',
                    ms is not None and inside and natural and right_conv, d, {'action': where})
        C.objective('ouverture-150ms', ms is not None and ms < 150, ms, f'{where} · click on {sel}')
        if mark is not None:
            n = len(re.findall(r'conversation of an element reopened', C.log_text()[mark:]))
            if n > 1:
                self.r.step(f'{where} · invariant: one « conversation of an element reopened » per click', False, n, {'action': where})
        self.m.open = sel; self.m.hidden_by_other = False; self.close_at = log_mark(); self.m.unknown = False
        if ms is None:   # no bubble came (reported above, once): no anchor — no cascade of follow-up failures
            # the bubble of BEFORE may rightly still be on screen when the click never opened anything (monkey seed 2 on ae51111:
            # #102, the click was taken by another browser's window; #261, O3): a bubble the model cannot place — it said
            # « closed -> no window » at each of the 6 next actions, twice
            still, _ = bubble_shown()
            self.m.open = None; self.m.anchor = None; self.m.unknown = bool(still); return
        el = element_screen(sel)
        if sel in EMBEDDED_BOX:   # the block clicked inside the iframe / the shadow root is the element (its host is only the anchor)
            r = page(EMBEDDED_BOX[sel]); pg0 = page_area(); kk = dpr()
            if r:
                self.distance(sel, [int(pg0[0] + r[0] * kk), int(pg0[1] + r[1] * kk), int(pg0[0] + r[2] * kk), int(pg0[1] + r[3] * kk)], sf, st.get('dpr', 1), mark, where) if sf and mark is not None else None
                mark = None
        self.m.anchor = (sf[0] - el[0], sf[1] - el[1], sf[3] - el[3]) if sf else None
        self.m.size = (sf[2] - sf[0], sf[3] - sf[1]) if sf else None
        self.m.scroll = scroll_y()
        if sf and mark is not None:
            hit = element_at(point) if point else None   # the element the click really selects (a child of `sel` may be under the point)
            self.distance(sel, hit or el, sf, st.get('dpr', 1), mark, where)

    def distance(self, sel, el, sf, k, mark, where):
        """Paper's distance to the element (coordinator, after the user's look): beside it, 12 px CSS from its edge and the
        tops aligned at -4 (+12 / -4); inside it, 12 px from the edges of the corner nearest to the click. > 2 px = failure."""
        seg = C.log_text()[mark:]
        found = re.findall(r'bubble placed (right of the element|left of the element|below the element|above the element|inside the element, (?:top|bottom) (?:left|right) corner)', seg)
        if not found:
            return
        how = found[-1]; pg = page_area(); vis = intersect(el, pg) or el
        if how.startswith('inside') and re.search(r'bubble over the element slid -?\d+ px', seg):
            return   # over an element smaller than the bubble, slid to be whole in the visible area (H1, e26c39c): the whole-bubble check is what holds
        slid = re.search(r'bubble slid -?\d+ px along its side to be fully visible|slid along it to be whole', seg)   # no side holds it whole: the alignment gives way (H1, #130 C)
        css = lambda v: round(v / k, 1)
        if how.startswith('right'):
            got = {'gap': css(sf[0] - el[2]), 'top': css(sf[1] - el[1])}; want = {'gap': 12, 'top': -4}
        elif how.startswith('left'):
            got = {'gap': css(el[0] - sf[2]), 'top': css(sf[1] - el[1])}; want = {'gap': 12, 'top': -4}
        elif how.startswith('below'):
            got = {'gap': css(sf[1] - el[3]), 'left': css(sf[0] - el[0])}; want = {'gap': 12, 'left': -4}
        elif how.startswith('above'):
            got = {'gap': css(el[1] - sf[3]), 'left': css(sf[0] - el[0])}; want = {'gap': 12, 'left': -4}
        else:
            got = {'x': css(sf[0] - vis[0]) if 'left corner' in how else css(vis[2] - sf[2]),
                   'y': css(sf[1] - vis[1]) if 'top ' in how else css(vis[3] - sf[3])}
            want = {'x': 8, 'y': 8}   # #130 C rule 9: inside the element, the bottom right corner, 8 px from its edges
        bad = {key: (got[key], want[key]) for key in want if abs(got[key] - want[key]) > 2 and not (slid and key in ('top', 'left'))}
        if bad:
            frame = [l for l in seg.splitlines() if 'bubble' in l and ('placed' in l or 'frame' in l or 'shown' in l)][-6:]
            self.r.step(f'{where} · invariant: the bubble at Paper\'s distance from {sel} ({how.split(":")[0]}: 12 px, aligned -4 / 12 px inside)', False,
                        {'measured css': got, 'Paper': want, 'surface': sf, 'element': el, 'dpr': k, 'log': frame}, {'action': where})

    def rebase(self):
        """After an action that may legitimately move the bubble's anchor (a reload, the end of the document)."""
        if self.m.open:
            sf = bubble_surface(); el = element_screen(self.m.open)
            self.m.anchor = (sf[0] - el[0], sf[1] - el[1], sf[3] - el[3]) if sf else None
            self.m.size = (sf[2] - sf[0], sf[3] - sf[1]) if sf else None
            self.m.scroll = scroll_y()

    def sent(self, kind, token, where):
        """After a send: the user's message is in the bubble (or the panel) it was sent from."""
        t0 = time.time(); ok = False
        while time.time() - t0 < 3 and not ok:
            ok = holds(chat_state(kind).get('text') or '', token); time.sleep(0.1)
        if not ok:
            self.r.step(f'{where} · invariant: the message sent is shown where it was sent ({kind})', False, token, {'action': where})
        return ok

    def check(self, where, wheel_moved=False):
        """Every invariant, after an action. Returns the list of broken ones."""
        broken = {}
        self.r.data['measures']['invariantChecks'] = self.r.data['measures'].get('invariantChecks', 0) + 1
        fg = foreground_is_ours()
        seg = C.log_text()[self.close_at:]
        if self.m.open and re.search(r'CHAT (bubble closed|a click elsewhere closed the bubble|the bubble.s element is no longer in the page)', seg):
            if not self.allow_close:
                broken['an open bubble stays open until the user closes it'] = [l for l in seg.splitlines() if 'bubble' in l][-4:]
            self.m.open = None
        self.allow_close = False; self.close_at = log_mark()
        shown, region = bubble_shown()
        wins = bubble_windows()
        if len(wins) > 1:
            broken['one bubble window'] = len(wins)
        if self.m.layout == 'comment' and self.m.unknown:
            pass   # a container's bubble (click « elsewhere »): not one the model can place
        elif self.m.layout == 'comment':
            expect = bool(self.m.open) and fg and not self.m.hidden_by_other
            if expect and self.m.anchor and self.m.size:
                try:   # the element scrolled away with its bubble: nothing to see, nothing to check
                    el0 = element_screen(self.m.open); a0 = self.m.anchor
                    guess = (el0[0] + a0[0], el0[1] + a0[1], el0[0] + a0[0] + self.m.size[0], el0[1] + a0[1] + self.m.size[1])
                    part = intersect(guess, page_area())   # what of it is still in the page area (rule 6: it leaves with its element)
                    if part is None or part[2] - part[0] <= 40 or part[3] - part[1] <= 30:   # a sliver: bubble_shown() asks for more than 40 x 30 px
                        expect = False
                except Exception:
                    pass
            if expect:
                if not shown:
                    broken['open -> window visible'] = {'region': region}
                else:
                    st = chat_state('bubble'); sf = bubble_surface(st); pg = page_area()
                    try:
                        el = element_screen(self.m.open)
                    except Exception:
                        el = None
                    vis = intersect(sf, pg) if sf else None
                    if sf and vis and isinstance(region, tuple):
                        if not (region[0] <= vis[0] + 1 and region[1] <= vis[1] + 1 and region[2] >= vis[2] - 1 and region[3] >= vis[3] - 1):
                            broken['never cut by the region'] = {'region': region, 'visible surface': vis}
                        if not st.get('menus'):   # no menu asked: the region is the surface (+ its shadow), never a transparent zone that takes the clicks
                            k = st.get('dpr', 1); pad = int(40 * k)
                            if region[0] < vis[0] - pad or region[1] < vis[1] - pad or region[2] > vis[2] + pad or region[3] > vis[3] + pad:
                                broken['region = surface (no menu open: nothing beyond its shadow)'] = {'region': region, 'visible surface': vis}
                        share = drawn_share(vis)
                        if share < 0.4:
                            broken['never empty (drawn)'] = share
                    if st.get('opacity', 1) < 0.99:
                        broken['never greyed'] = st.get('opacity')
                    p = st.get('perm')
                    if p and sf:
                        k = st['dpr']; wr = V3.win_rect(bubble_hwnd())
                        for b in p['buttons']:
                            bx = (wr[0] + b['r'][0] * k, wr[1] + b['r'][1] * k, wr[0] + b['r'][2] * k, wr[1] + b['r'][3] * k)
                            if b['op'] < 0.99 or bx[3] > sf[3] + 1 or bx[1] < sf[1] - 1:
                                broken['permission buttons whole and not greyed'] = {'button': b, 'surface': sf}
                    if sf and el and self.m.anchor and not wheel_moved:
                        a = (sf[0] - el[0], sf[1] - el[1], sf[3] - el[3])
                        if abs(a[0] - self.m.anchor[0]) > 2 or (abs(a[1] - self.m.anchor[1]) > 2 and abs(a[2] - self.m.anchor[2]) > 2):
                            # #130 wins over R29: a bubble that grew past the bottom slid up along its element (same side) — not a defect
                            if not re.search(r'the bubble at the end of the document moves up once|slid up along its element|it goes (above|below) it \(#130 C rule 10\)', C.log_text()[self.log_at:]):
                                broken['stuck to its element (±2 px)'] = {'now': a, 'at opening': self.m.anchor, 'surface': sf, 'element': el}
                            self.rebase()
                    # #130 (blocking, « bulle-entiere »): its element whole on screen and the page not scrolled since the bubble
                    # was placed — the bubble is whole in the page area. After a scroll: rule 5, asserted from lot 4.
                    if sf and el and not wheel_moved and el[0] >= pg[0] - 1 and el[1] >= pg[1] - 1 and el[2] <= pg[2] + 1 and el[3] <= pg[3] + 1:
                        sy = scroll_y(); still = sy is not None and self.m.scroll is not None and abs(sy - self.m.scroll) < 1
                        if (still or C130_SCROLL_RULES) and (sf[0] < pg[0] - 1 or sf[1] < pg[1] - 1 or sf[2] > pg[2] + 1 or sf[3] > pg[3] + 1):
                            broken['the bubble is whole in the page area (#130 C)'] = {'surface': sf, 'page area': pg, 'element': el, 'scrolledSincePlaced': not still}
                    self._labels(st, broken)
            elif not self.m.open and shown:
                broken['closed -> no window'] = {'region': region}
        else:
            if shown:
                broken['panel layout -> no bubble'] = {'region': region}
            st = chat_state('panel')
            if st.get('w', 0) and st['w'] < 340:
                broken['panel layout: the panel page has the panel layout'] = st.get('w')
            self._labels(st, broken)
        # conversation marks: only elements with a sent message; one contour per element
        try:
            nodes = overlay_nodes()
            marks = [x for x in nodes if 'ct' in x['cls'].split()]
            frames = [x for x in nodes if x['cls'] == 'bf']
            want = sum(1 for t in self.m.conv.values() if t) + len(self.m.panel_marked - set(self.m.conv))
            if len(marks) > want:
                broken['the conversation mark exists only after a send'] = {'marks': len(marks), 'sent on': want}
            if len(frames) > 1:
                broken['one contour per element'] = len(frames)
        except Exception as e:
            broken['page readable (DevTools)'] = str(e)[:200]
        errs = ERRORS.collect()
        if errs:
            broken['no JavaScript error'] = errs[:5]
        bad = bad_log_lines(self.log_at)
        if bad:
            broken['no exception in the host log'] = bad[:5]
        self.log_at = log_mark()
        if self.uia:
            self._uia(broken)
        for k, v in broken.items():
            self.r.step(f'{where} · invariant: {k}', False, v, {'action': where})
        return broken

    def _labels(self, st, broken):
        if 'error' in st:
            return
        if self.m.mode is None and st.get('mode'):
            self.m.mode, self.m.model = st.get('mode'), st.get('model')
            return
        if st.get('mode') and self.m.mode and st['mode'] != self.m.mode:
            broken['the mode changes only by the user'] = {'shown': st['mode'], 'chosen': self.m.mode}
        if st.get('model') and self.m.model and st['model'] != self.m.model:
            broken['the model and effort change only by the user'] = {'shown': st['model'], 'chosen': self.m.model}
        if st.get('model') and ('Claude Code' in st['model'] or '·' not in st['model']):
            broken['the model button shows model · effort (never the agent)'] = st['model']

    def _uia(self, broken):
        r, s = self.uia.probe()
        if not isinstance(r, list):
            broken['UI Automation answers'] = {'answer': r, 's': s}; return
        main = [x for x in r if x.get('cls') == 'LisiereBrowser']
        if not main or main[0]['n'] <= 0 or main[0]['err']:
            broken['UI Automation tree of the main window complete'] = r; return
        if self.uia_base is None:
            self.uia_base = main[0]['n']
        elif main[0]['n'] < self.uia_base * 0.6:
            # F7 (monkey-3 #214, 113 -> 22): what the short walk held, and whether a second walk 0.5 s later is whole
            # again (a walk cut once while the tree changed) or still short (a tree cut for a screen reader too)
            short = self.uia.ask(f'list {Q.our_pid()}'); time.sleep(0.5); again, _ = self.uia.probe()
            n2 = next((x['n'] for x in again if x.get('cls') == 'LisiereBrowser'), -1) if isinstance(again, list) else -1
            # the 20:00 release's pass on ae51111 (#90: 32 of 113, then 151 half a second later, right after a change of layout):
            # a walk cut ONCE while the tree changed is the reader's own (as chat_qa_uia_offscreen.py reads again) — what fails
            # is a tree still short at the second walk (cut for a screen reader too)
            if n2 < self.uia_base * 0.6:
                broken['UI Automation tree of the main window complete'] = {'elements': main[0]['n'], 'at start': self.uia_base, 'all windows': r,
                                                                            'again after 0.5 s': n2, 'short walk': short}
            else:
                print(f'NOTE UI Automation: a short walk ({main[0]["n"]} of {self.uia_base}), whole again 0.5 s later ({n2})', flush=True)
        if s > 15:
            broken['UI Automation answers in time'] = s
        chats = [x for x in r if x.get('cls', '').startswith('BrowserChat')]
        if any(c['err'] or c['n'] <= 1 for c in chats if self.m.open):
            broken['the chat is readable (UI Automation)'] = chats


class FollowSampler(threading.Thread):
    """R39 while the page scrolls: pairs (the bubble's region on screen, the element's rectangle) read as close together as
    possible (its own DevTools connection, ~every 10 ms). The gap between the bubble and its element must not change: a
    read skew of a few ms is allowed for (tolerance 16 px CSS) — the defect seen by comment-v5 R39 was 27 to 134 px."""
    def __init__(self, sel):
        super().__init__(daemon=True); self.sel = sel; self.stop = False; self.pairs = []
        ts = _targets(C.URL); self.ws = Q.WS(ts[0]['webSocketDebuggerUrl']) if ts else None
        self.h = bubble_hwnd(); self.pg = page_area()
        self.js = "(function(){var e=document.querySelector(%s);if(!e)return null;var r=e.getBoundingClientRect();return [r.left,r.top,r.bottom,devicePixelRatio]})()" % json.dumps(sel)

    def run(self):
        while not self.stop and self.ws and len(self.pairs) < 400:
            try:
                r = self.ws.call('Runtime.evaluate', {'expression': self.js, 'returnByValue': True}).get('result', {}).get('value')
                rb = region_box(self.h)
            except Exception:
                break
            if r and isinstance(rb, tuple):
                k = r[3]; el_top = self.pg[1] + r[1] * k; el_bottom = self.pg[1] + r[2] * k; el_left = self.pg[0] + r[0] * k
                # only while the element and the bubble are both well inside the page area (no clipping by its edges)
                if self.pg[1] + 4 < rb[1] and rb[3] < self.pg[3] - 4 and self.pg[1] < el_top < self.pg[3]:
                    self.pairs.append((time.perf_counter(), rb[1] - el_top, rb[0] - el_left, k))
            time.sleep(0.008)

    def verdict(self):
        if len(self.pairs) < 4:
            return None
        base = self.pairs[0][1]; k = self.pairs[0][3]
        dev = [abs(p[1] - base) / k for p in self.pairs]
        return {'samples': len(self.pairs), 'maxGapChangeCss': round(max(dev), 1), 'over16': sum(1 for d in dev if d > 16)}


def guarded(report, fn, *a, **k):
    try:
        return fn(*a, **k)
    except Exception:
        traceback.print_exc()
        report.step(f'{getattr(fn, "__name__", "step")} without exception', False, traceback.format_exc()[-600:])
        return None
