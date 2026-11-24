#!/usr/bin/env python3
"""Mode Focus with the annotation mode, in a real window (night only: it takes the screen and the pointer).

The owner's rule (2026-11-24): when the annotation mode runs and Mode Focus is entered (or the reverse), the annotation
banner and the chat (mode Panneau) stay visible and usable; Focus hides the rest of the UI. Paper page « #127 — Panneau
latéral »: « Annotation + Focus · barre du haut » (203J-0, at rest), « … panneau révélé » (20MM-0), « … tiroir ouvert »
(21OK-0). Geometry in DIP, client of the window (src/focus-mode.h « With the annotation mode »):
  banner x 4 .. W - 4, y 0 .. 32 · page x 4, top 32, 4 DIP before the chat card, bottom H - 4 · chat card 380 wide,
  4 DIP from the window's right and bottom edges, top 32 (its WebView 1 DIP inside its ring) · revealed panel top 36.

Journey A (Annotation then Focus): mode on, chat open in Panneau mode -> Ctrl+Maj+F -> banner and chat present, at their
Focus places, the chat answers -> the hover strip under the banner reveals the panel at 36 -> a drawer opens over the chat
(top 124) -> Ctrl+Maj+F again -> nothing jumps: banner, chat and page go straight back to where they were (one change
each, sampled every ~10 ms), never hidden meanwhile.
Journey B (Focus then Annotation): Focus on -> mode on -> banner at the very top, page under it -> chat open (card) -> mode
off -> page back to the 4 DIP inset -> Focus off.

    python tests/focus/annotation_focus_journey.py [--tab-layout top|side|compact]
    -> build/chat-qa/focus-journey-<layout>/report.json + captures (eye check)

Same build, profile copy and guards as the other real-window chat suites (tests/chat/chat_qa.py: build/chat-preview,
BROWSER_ANNOTATIONS_TEST=1, agent configurations under build/, fake agent). --tab-layout is #127's switch (side panel /
compact layouts): until #127 is merged only « top » runs; « side » and « compact » keep the same rest checks (Paper
2091-0 / 20EJ-0 draw the banner, page and chat at the same places) and fail on the reveal step until it is wired.
"""
from __future__ import annotations
import argparse
import ctypes
import ctypes.wintypes as W
import json
import re
import subprocess
import sys
import time
import traceback
import pathlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'chat'))
import chat_qa as C   # noqa: E402
Q = C.Q
user32 = ctypes.windll.user32

ap = argparse.ArgumentParser()
ap.add_argument('--tab-layout', choices=('top', 'side', 'compact'), default='top')
ARGS = ap.parse_args()
LAYOUT = ARGS.tab_layout
OUT = C.QA / f'focus-journey-{LAYOUT}'
report = {'layout': LAYOUT, 'steps': [], 'measures': {}, 'captures': []}

# Paper values (DIP), src/focus-mode.h
INSET, BANNER, CHAT, GAP, PANEL_TOP, DRAWER_TOP = 4, 32, 380, 4, 36, 124
TOL = 1   # px: rounding of DIP to device pixels


def step(name, ok, detail=''):
    report['steps'].append({'step': name, 'ok': bool(ok), 'detail': detail if isinstance(detail, (dict, list)) else str(detail)})
    print(('PASS ' if ok else 'FAIL ') + name + (' — ' + json.dumps(detail, ensure_ascii=False, default=str) if detail else ''), flush=True)
    return bool(ok)


# ---- the build's launch: the test profile's settings (tutorial seen) and #127's layout switch ----
_Popen = subprocess.Popen


def _popen(cmd, *a, **k):
    if isinstance(cmd, (list, tuple)) and cmd and str(cmd[0]).lower().endswith('browser.exe'):
        f = C.APP / 'settings'   # the annotation tutorial (#125) would veil the page: the profile starts with it seen
        lines = [l for l in (f.read_text(encoding='utf-8', errors='replace').splitlines() if f.exists() else []) if l and not l.startswith('annotation-onboarded=')]
        if not lines or lines[0] != 'browser-settings 1':
            lines = ['browser-settings 1'] + [l for l in lines if l != 'browser-settings 1']
        f.write_text('\n'.join(lines + ['annotation-onboarded=1']) + '\n', encoding='utf-8', newline='\n')
        if LAYOUT != 'top':
            cmd = [*cmd, f'--tab-layout={LAYOUT}']
    return _Popen(cmd, *a, **k)


subprocess.Popen = _popen


# ---- what is on screen: our window's banner, chat and page, client pixels ----
def client_origin(h):
    p = W.POINT(0, 0); user32.ClientToScreen(h, ctypes.byref(p)); return p.x, p.y


def client_size(h):
    r = W.RECT(); user32.GetClientRect(h, ctypes.byref(r)); return r.right, r.bottom


def scene():
    """{'banner', 'chat', 'page'}: (left, top, right, bottom) in the main window's client pixels, or None when absent /
    hidden. The banner: the widest visible annotation layer about 32 DIP high; the chat: the panel host; the page: the
    widest visible WebView host that is a direct child of the window."""
    h = Q.main_window()
    if not h:
        return {}
    ox, oy = client_origin(h); k = Q.scale(h); found = {'banner': None, 'chat': None, 'page': None}; best = {'banner': 0, 'page': 0}
    pid = Q.our_pid()

    def rect_of(w):
        r = W.RECT(); user32.GetWindowRect(w, ctypes.byref(r)); return (r.left - ox, r.top - oy, r.right - ox, r.bottom - oy)

    @ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    def cb(w, _):
        p = W.DWORD(); user32.GetWindowThreadProcessId(w, ctypes.byref(p))
        if p.value != pid or not user32.IsWindowVisible(w):
            return True
        name = ctypes.create_unicode_buffer(64); user32.GetClassNameW(w, name, 64); cls = name.value; r = rect_of(w)
        width, height = r[2] - r[0], r[3] - r[1]
        if cls == 'BrowserAnnotationLayer' and abs(height - BANNER * k) <= 2 and width > best['banner']:
            best['banner'] = width; found['banner'] = r
        elif cls == 'BrowserChatPanelHost':
            found['chat'] = r
        elif cls.startswith('Chrome_WidgetWin') and user32.GetParent(w) == h and width > best['page']:
            best['page'] = width; found['page'] = r
        return True
    user32.EnumChildWindows(h, cb, 0)
    for hw, cls, _, _ in Q.windows():   # an owned popup layer, if the banner is one
        if cls == 'BrowserAnnotationLayer':
            r = rect_of(hw)
            if abs((r[3] - r[1]) - BANNER * k) <= 2 and (r[2] - r[0]) > best['banner']:
                best['banner'] = r[2] - r[0]; found['banner'] = r
    return found


def near(a, b):
    return a is not None and b is not None and all(abs(x - y) <= TOL for x, y in zip(a, b))


def expected_focus(chat_open):
    """Where Paper puts banner, page and chat card in Focus (client px)."""
    h = Q.main_window(); k = Q.scale(h); w, hh = client_size(h); S = lambda d: round(d * k)
    page_right = w / k - INSET - ((CHAT + GAP) if chat_open else 0)
    exp = {'banner': (S(INSET), 0, w - S(INSET), S(BANNER)), 'page': (S(INSET), S(BANNER), S(page_right), S(hh / k - INSET))}
    if chat_open:
        right = w - S(INSET); left = right - S(CHAT); b = S(1)   # src/chat-host-ui.h PanelRect + CardView (1 DIP ring)
        exp['chat'] = (left + b, S(BANNER) + b, right - b, S(hh / k - INSET) - b)
    return exp


def check_focus_scene(label, chat_open):
    s = scene(); e = expected_focus(chat_open)
    ok = all(near(s.get(n), e[n]) for n in e)
    report['measures'][label] = {'seen': s, 'expected': e}
    return step(f'{label}: banner at the very top, page under it' + (', chat card on the right' if chat_open else '') + ' (Paper 203J-0)', ok, {'seen': s, 'expected': e})


def sample_until_still(seconds=1.6):
    """The scene every ~10 ms after a change, to see every place each surface passes through."""
    out = []; t0 = time.time()
    while time.time() - t0 < seconds:
        out.append(scene()); time.sleep(0.01)
    return out


def no_jump(label, samples, start, final):
    """Each surface goes straight from `start` to `final` (at most one change), is never hidden meanwhile, and ends at
    `final`."""
    bad = {}
    for n in ('banner', 'chat', 'page'):
        seq = [s.get(n) for s in samples]
        if final.get(n) is None:
            continue
        if any(v is None for v in seq):
            bad[n] = 'hidden during the change'; continue
        distinct = [seq[0]]
        for v in seq[1:]:
            if v != distinct[-1]:
                distinct.append(v)
        if len(distinct) > 2 or not near(distinct[-1], final[n]) or (start.get(n) and not near(distinct[0], start[n]) and len(distinct) > 1):
            bad[n] = distinct
    return step(label, not bad, bad or {'samples': len(samples)})


def capture(name):
    try:
        p = C.grab(f'focus-journey-{LAYOUT}/{name}'); report['captures'].append(str(p))
    except Exception as e:   # a capture is evidence, never the result
        report['captures'].append(f'{name}: {e}')


def live_panel(after):
    m = C.wait_log(r'FOCUS live \(holes \d+, picture \d+, panel (-?\d+),(-?\d+),(-?\d+),(-?\d+)\)', 3, after)
    return tuple(int(v) for v in m.groups()) if m else None


def reveal_top():
    """The 14 DIP strip right under the banner (32 .. 46, 20MM-0) reveals the panel at top 36, over the page only."""
    h = Q.main_window(); k = Q.scale(h); ox, oy = client_origin(h)
    Q.front(); time.sleep(0.4)
    m = len(C.log_text())
    user32.SetCursorPos(ox + round(200 * k), oy + round((BANNER + 6) * k))
    shown = C.wait_log(r'FOCUS bar shown', 3, m)
    panel = live_panel(m)
    s = scene(); w = client_size(h)[0] / k
    ok = bool(shown) and panel is not None and panel[1] == PANEL_TOP and panel[0] == INSET and panel[2] <= round(w - INSET - CHAT - GAP) + 1
    step('reveal: the hover strip under the banner brings the panel at top 36, over the page only (20MM-0)', ok, {'panel': panel, 'scene': s})
    still = scene()
    step('reveal: the banner and the chat stay where they are', all(near(still.get(n), s.get(n)) for n in ('banner', 'chat')), still)
    return ok


def reveal_side():
    raise NotImplementedError('side / compact: the column comes over the page from the left edge after 400 ms (Paper 2091-0 / 20EJ-0); '
                              'wire it to #127\'s overlay once #127 is merged (tests/focus/annotation_focus_journey.py, reveal_side)')


def drawer_over_chat():
    """Ctrl+H from the revealed panel: the history drawer at top 124, its right edge on W - 4, over the chat (21OK-0)."""
    h = Q.main_window(); k = Q.scale(h); ox, oy = client_origin(h); w, hh = client_size(h)
    m = len(C.log_text()); Q.post_key(ord('H'), ctrl=True); time.sleep(0.9)
    found = drawer = None
    for hw, cls, _, r in Q.windows():   # src/history-panel.h: class LisiereHistoryDrawer
        if cls == 'LisiereHistoryDrawer':
            drawer = hw; found = (r[0] - ox, r[1] - oy, r[2] - ox, r[3] - oy)
    capture('03-drawer-over-chat')
    ok = found is not None and abs(found[1] - round(DRAWER_TOP * k)) <= TOL and abs(found[2] - (w - round(INSET * k))) <= TOL and abs(found[3] - (hh - round(INSET * k))) <= TOL
    step('drawer: top 124, right edge W - 4 over the chat, bottom H - 4 (21OK-0)', ok, {'drawer': found, 'client': (w, hh)})
    if drawer:
        Q.post_key(0x1B, target=drawer)   # Escape in the drawer: it closes, the chat is back
    time.sleep(0.8)
    step('drawer closed: the chat is back', scene().get('chat') is not None, scene())


def journey_a():
    m = len(C.log_text()); C.shortcut('A')
    step('mode on', C.wait_log(r'ANNOTATE mode on', 10, m) is not None); time.sleep(1.2)
    if C.invoke('AnnotationConnectLater') or C.invoke('AnnotationConnectClose'):
        time.sleep(0.6)
    m = len(C.log_text()); C.invoke('AnnotationAskAgent')   # the banner's switch: « Panneau » (the chat on the right)
    if not C.wait_log(r'CHAT panel in place', 8, m):
        C.shortcut('D'); C.wait_log(r'CHAT panel in place', 8, m)
    time.sleep(1.0)
    before = scene(); report['measures']['A-before'] = before
    step('before Focus: banner, chat and page on screen', all(before.get(n) for n in ('banner', 'chat', 'page')), before)
    capture('01-annotation-chat')
    m = len(C.log_text()); C.shortcut('F')
    step('Ctrl+Maj+F: Mode Focus on, the annotation mode still on', C.wait_log(r'FOCUS mode on', 5, m) is not None and 'ANNOTATE mode off' not in C.log_text()[m:])
    time.sleep(0.8)
    check_focus_scene('A · Focus', True)
    usable = C.panel("(()=>{const t=document.querySelector('textarea,[contenteditable=true]');if(!t)return false;t.focus();return document.activeElement===t})()")
    step('A · Focus: the chat is usable (its field takes the keyboard)', usable is True, usable)
    capture('02-focus-rest')
    if LAYOUT == 'top':
        reveal_top(); capture('02b-focus-revealed'); drawer_over_chat()
    else:
        try:
            reveal_side()
        except NotImplementedError as e:
            step(f'reveal ({LAYOUT}): pending #127', False, str(e))
    user32.SetCursorPos(*[v + 600 for v in client_origin(Q.main_window())]); time.sleep(0.8)   # away: the panel leaves
    start = scene(); m = len(C.log_text()); C.shortcut('F')
    samples = sample_until_still()
    step('Ctrl+Maj+F again: Mode Focus off', C.wait_log(r'FOCUS mode off', 3, m) is not None)
    no_jump('A · leaving Focus: banner, chat and page go straight back, never hidden (nothing jumps)', samples, start, before)
    after = scene(); report['measures']['A-after'] = after
    step('A · back exactly where they were before Focus', all(near(after.get(n), before.get(n)) for n in ('banner', 'chat', 'page')), {'before': before, 'after': after})
    capture('04-focus-off')


def journey_b():
    m = len(C.log_text()); C.invoke('AnnotationQuit') or Q.post_key(0x1B)
    C.wait_log(r'ANNOTATE mode off', 8, m); time.sleep(1.0)
    m = len(C.log_text()); C.shortcut('F'); step('B · Focus on first', C.wait_log(r'FOCUS mode on', 5, m) is not None); time.sleep(0.8)
    m = len(C.log_text()); C.shortcut('A'); step('B · then the annotation mode', C.wait_log(r'ANNOTATE mode on', 10, m) is not None); time.sleep(1.4)
    check_focus_scene('B · Focus then Annotation (Commentaire or chat closed)', scene().get('chat') is not None)
    m = len(C.log_text()); C.invoke('AnnotationAskAgent')
    if not C.wait_log(r'CHAT panel in place', 8, m):
        C.shortcut('D'); C.wait_log(r'CHAT panel in place', 8, m)
    time.sleep(1.0)
    check_focus_scene('B · chat opened in Focus', True); capture('05-focus-then-annotation')
    m = len(C.log_text()); C.invoke('AnnotationQuit') or Q.post_key(0x1B)
    step('B · annotation mode off', C.wait_log(r'ANNOTATE mode off', 8, m) is not None); time.sleep(1.2)
    s = scene(); h = Q.main_window(); k = Q.scale(h)
    step('B · Focus kept, the page back to the 4 DIP inset', s.get('page') is not None and abs(s['page'][1] - round(INSET * k)) <= TOL and s.get('banner') is None, s)
    m = len(C.log_text()); C.shortcut('F'); step('B · Focus off', C.wait_log(r'FOCUS mode off', 5, m) is not None)


def run_main():
    before = C.fingerprints()
    OUT.mkdir(parents=True, exist_ok=True)
    C.launch(extra_env={'BROWSER_CHAT_PANEL_IDLE_MS': '60000', 'BROWSER_CHAT_AGENT_IDLE_MS': '600000'})
    try:
        if LAYOUT != 'top':
            step(f'--tab-layout={LAYOUT} understood by this build (#127)', C.wait_log(r'layout: ' + ('side' if LAYOUT == 'side' else 'compact'), 5, 0) is not None)
        Q.front(); time.sleep(0.8)
        journey_a()
        journey_b()
    except Exception:
        traceback.print_exc(); step('run without exception', False, traceback.format_exc()[-600:])
    finally:
        C.quit_all()
        step('real Claude / Codex configuration files unchanged', before == C.fingerprints())
        ps = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq fake-acp-agent.exe', '/NH'], capture_output=True, text=True).stdout
        step('nothing left running (fake agent)', 'fake-acp-agent' not in ps)
        (OUT / 'report.json').write_text(json.dumps(report, indent=2, ensure_ascii=False, default=str), encoding='utf-8')
    failed = [s for s in report['steps'] if not s['ok']]
    print(f"{len(report['steps']) - len(failed)} / {len(report['steps'])} steps passed")
    return 1 if failed else 0


if __name__ == '__main__':
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding='utf-8')
        except Exception:
            pass
    sys.exit(run_main())
