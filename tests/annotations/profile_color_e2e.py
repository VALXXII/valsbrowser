#!/usr/bin/env python3
"""Annotation mode x profile colour — real application run (design/annotation-profile-color-plan.md).

    python -I tests/annotations/profile_color_e2e.py [--app build/accent-app]   # -> build/accent-e2e/report.txt + PNGs

Drives ONLY its own throwaway copy of a dev build (build/accent-e2e-app: payload copied, data next to the exe, two
throwaway profiles written before the start); never the user's session, profiles, store or agent configurations.
The page is served by this script on 127.0.0.1 (loopback); the WebView2 remote debugging port (loopback) is enabled for
this copy only, through the process environment. The page's attachShadow is made « open » from the test (CDP) before
the mode starts, so the page script's own style rules can be read back; nothing else of the page script is touched.

Checks:
  entry         the window of profile « vert » enters the mode: the page script paints the profile's FILL (the value in
                the log « mode on (…, accent r,g,b) »), its cursor dot too
  live edit     profiles.txt rewritten (vert -> hue 55) + BrowserProfilesChanged posted (what another window's edit
                does): the page gets the new fill once (no step of the 120 ms fade), the band repaints in it
  switch        « qa switch ambre » (--workspace-test) while the mode is on: the mode ends with the old page
                (« mode off (tab change) »), no accent is sent to the old page; re-entering in « ambre » paints ambre
  store         the global annotation store gets no annotation from this run
"""
from __future__ import annotations
import ctypes
import ctypes.wintypes as W
import http.server
import json
import pathlib
import re
import shutil
import socketserver
import subprocess
import sys
import threading
import time
import urllib.request

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import sandbox_qa as Q   # noqa: E402  (WS client, post_key, print_window)

ROOT = HERE.parents[1]
APP = ROOT / 'build' / 'accent-e2e-app'
OUT = ROOT / 'build' / 'accent-e2e'
PORT, WEB = 9341, 5197
URL = f'http://127.0.0.1:{WEB}/'
user32 = ctypes.windll.user32
# Fills computed by ProfileColor::AccentFill (tests/annotations-ui harness, report « Contrast »): hue, intensity .14.
FILL = {'vert': (27, 134, 65), 'ambre': (181, 93, 6)}
lines: list[str] = []
failures = 0


def log(text):
    print(text, flush=True); lines.append(text)


def check(ok, what):
    global failures
    log(('ok   ' if ok else 'FAIL ') + what)
    if not ok:
        failures += 1


PAGE = '''<!doctype html><html lang="fr"><head><meta charset="utf-8"><title>Accent E2E</title>
<style>body{font:16px system-ui;margin:40px;background:#fff;color:#111}button{font:16px system-ui;padding:10px 18px}</style></head>
<body><h1>Paiement</h1><p>Une page de test servie en local.</p><button id="pay">Payer 48,00 €</button></body></html>'''.encode('utf-8')


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200); self.send_header('Content-Type', 'text/html; charset=utf-8'); self.end_headers(); self.wfile.write(PAGE)

    def log_message(self, *a):
        pass


def registry(vert_hue):
    return ('browser-profiles 1\nlast=vert\n'
            'p=default|house|0|0|1|Personnel\n'
            f'p=vert|leaf|{vert_hue}|140|0|Vert\n'
            'p=ambre|briefcase|55|140|0|Ambre\n')


def page_ws():
    for t in json.loads(urllib.request.urlopen(f'http://127.0.0.1:{PORT}/json', timeout=5).read()):
        if t.get('type') == 'page' and t.get('url', '').startswith(URL):
            return Q.WS(t['webSocketDebuggerUrl'])
    return None


def any_page_ws():
    for t in json.loads(urllib.request.urlopen(f'http://127.0.0.1:{PORT}/json', timeout=5).read()):
        if t.get('type') == 'page':
            return Q.WS(t['webSocketDebuggerUrl'])
    return None


def evaluate(js):
    ws = page_ws()
    r = ws.call('Runtime.evaluate', {'expression': js, 'returnByValue': True, 'awaitPromise': True})
    return r.get('result', {}).get('value')


OPEN_SHADOW = ("(function(){if(window.__accentE2E)return 1;window.__accentE2E=1;var o=Element.prototype.attachShadow;"
               "Element.prototype.attachShadow=function(i){return o.call(this,Object.assign({},i,{mode:'open'}));};return 1;})()")
READ_LAYER = ("(function(){var h=document.querySelector('browser-annotation-layer');if(!h||!h.shadowRoot)return null;"
              "var a=h.shadowRoot.querySelector('style.a');var f=h.shadowRoot.querySelector('.f');"
              "return {css:a?a.textContent:'',fill:f?getComputedStyle(f).backgroundColor:''};})()")


def all_logs():
    text = ''
    for p in APP.rglob('*.log'):
        try:
            text += p.read_text(encoding='utf-8', errors='replace')
        except OSError:
            pass
    return text


def wait(pred, seconds=8.0):
    end = time.time() + seconds
    while time.time() < end:
        v = pred()
        if v:
            return v
        time.sleep(0.15)
    return pred()


def send_copydata(h, text):
    class CDS(ctypes.Structure):
        _fields_ = [('dwData', ctypes.c_size_t), ('cbData', W.DWORD), ('lpData', ctypes.c_void_p)]
    buf = ctypes.create_unicode_buffer(text)
    cds = CDS(0x464F5250, ctypes.sizeof(buf), ctypes.cast(buf, ctypes.c_void_p))
    res = ctypes.c_size_t()
    user32.SendMessageTimeoutW(h, 0x4A, 0, ctypes.byref(cds), 2, 4000, ctypes.byref(res))
    return res.value


def navigate():
    ws = wait(any_page_ws, 20)
    ws.call('Page.navigate', {'url': URL})
    ws = wait(page_ws, 15)
    wait(lambda: evaluate('document.readyState') == 'complete', 10)
    evaluate(OPEN_SHADOW)


def screen_client(h):
    """The window's client area as it is ON SCREEN (BitBlt of the desktop: the band and the page are child windows, the
    band a layered one), top-down RGB rows, rendered by PrintWindow (works without an interactive desktop)."""
    g = ctypes.windll.gdi32
    r = W.RECT(); user32.GetClientRect(h, ctypes.byref(r)); pt = W.POINT(0, 0); user32.ClientToScreen(h, ctypes.byref(pt))
    w, hh = r.right, r.bottom
    hdc = user32.GetDC(0); mdc = g.CreateCompatibleDC(hdc)
    class BIH(ctypes.Structure):
        _fields_ = [('biSize', W.DWORD), ('biWidth', W.LONG), ('biHeight', W.LONG), ('biPlanes', W.WORD), ('biBitCount', W.WORD),
                    ('biCompression', W.DWORD), ('biSizeImage', W.DWORD), ('a', W.LONG), ('b', W.LONG), ('c', W.DWORD), ('d', W.DWORD)]
    bi = BIH(ctypes.sizeof(BIH), w, -hh, 1, 32, 0, 0, 0, 0, 0, 0); bits = ctypes.c_void_p()
    bmp = g.CreateDIBSection(mdc, ctypes.byref(bi), 0, ctypes.byref(bits), None, 0); old = g.SelectObject(mdc, bmp)
    user32.PrintWindow(h, mdc, 3)   # PW_CLIENTONLY | PW_RENDERFULLCONTENT: the composed content (no interactive desktop needed)
    raw = ctypes.string_at(bits, w * hh * 4)
    g.SelectObject(mdc, old); g.DeleteObject(bmp); g.DeleteDC(mdc); user32.ReleaseDC(0, hdc)
    rows = [bytes(raw[y * w * 4 + x * 4 + c] for x in range(w) for c in (2, 1, 0)) for y in range(hh)]
    return w, hh, rows


def write_png(path, w, hh, rows):
    import struct, zlib
    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xFFFFFFFF)
    data = b''.join(bytes([0]) + r for r in rows)
    sig = bytes([0x89]) + b'PNG' + bytes([13, 10, 26, 10])
    path.write_bytes(sig + chunk(b'IHDR', struct.pack('>IIBBBBB', w, hh, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(data, 6)) + chunk(b'IEND', b''))


def band_colour(name):
    """Mean colour of the band's text-free middle, sampled on screen; the window's client area saved as evidence."""
    h = Q.main_window(); time.sleep(.4); s = Q.scale(h)
    w, hh, rows = screen_client(h)
    write_png(OUT / f'{name}.png', w, hh, rows)
    y = int(round((81 + 26) * s))   # the band (81..113 DIP under the toolbar): its lower part, where the gradient is strongest
    xs = range(int(w * .45), int(w * .55)); r = g = b = 0
    for x in xs:
        r += rows[y][3 * x]; g += rows[y][3 * x + 1]; b += rows[y][3 * x + 2]
    n = len(xs); return (r / n, g / n, b / n)


def main(argv):
    src = ROOT / (argv[argv.index('--app') + 1] if '--app' in argv else 'build/accent-app')
    if not (src / 'browser.exe').exists():
        raise SystemExit(f'{src}/browser.exe missing: python tools/release/build.py --out build/accent-app --channel dev')
    shutil.rmtree(APP, ignore_errors=True); shutil.rmtree(OUT, ignore_errors=True)
    APP.mkdir(parents=True); OUT.mkdir(parents=True)
    for f in src.iterdir():   # the payload only
        if f.suffix in ('.exe', '.dll', '.json', '.txt') or f.name == 'assets':
            (shutil.copytree if f.is_dir() else shutil.copy2)(f, APP / f.name)
    (APP / 'profiles.txt').write_text(registry(150), encoding='utf-8', newline='\n')
    server = socketserver.TCPServer(('127.0.0.1', WEB), Handler); threading.Thread(target=server.serve_forever, daemon=True).start()
    env = dict(**__import__('os').environ)
    env['WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS'] = f'--remote-debugging-port={PORT} --remote-allow-origins=http://127.0.0.1:{PORT}'
    env['BROWSER_ANNOTATIONS_TEST'] = '1'; env['BROWSER_ANNOTATIONS_CONNECT_ROOT'] = str(APP / 'connect-redirect')
    env['BROWSER_ANNOTATIONS_QA_RAW_DIR'] = ''
    p = subprocess.Popen([str(APP / 'browser.exe'), '--profile', 'vert', '--workspace-test'], cwd=str(APP), env=env)
    Q.APP = APP; (APP / 'qa.pid').write_text(str(p.pid))
    try:
        run()
    except Exception as e:
        check(False, f'run aborted: {e!r}')
    finally:
        h = Q.main_window()
        if h:
            user32.PostMessageW(h, 0x10, 0, 0)
        try:
            p.wait(15)
        except subprocess.TimeoutExpired:
            p.kill()
        server.shutdown()
        tail = [l for l in all_logs().splitlines() if 'ANNOTATE' in l or 'PROFILES' in l or 'WSQA' in l]
        (OUT / 'app-log-excerpt.txt').write_text('\n'.join(tail) + '\n', encoding='utf-8')
        (OUT / 'report.txt').write_text('\n'.join(lines) + f'\n{failures} failures\n', encoding='utf-8')
        print(f'{failures} failures; report in {OUT}')
    return 1 if failures else 0


def rgb_in(css, rgb):
    return f'rgb({rgb[0]},{rgb[1]},{rgb[2]})' in css.replace(' ', '')


def run():
    check(wait(lambda: Q.main_window(), 20) is not None, 'the throwaway copy starts in profile « vert »')
    navigate()
    h = Q.main_window(); Q.front()
    # ---- entry
    Q.post_key(0x41, h, ctrl=True, shift=True)
    on = wait(lambda: re.search(r'ANNOTATE mode on \(.*accent (\d+),(\d+),(\d+)\)', all_logs()), 10)
    check(bool(on), 'the mode starts (log « ANNOTATE mode on … accent r,g,b »)')
    if not on:
        return
    logged = tuple(int(x) for x in on.groups())
    check(logged == FILL['vert'], f'accent handed to the page = profile « vert » fill {FILL["vert"]} (log: {logged})')
    layer = wait(lambda: evaluate(READ_LAYER), 6) or {}
    check(rgb_in(layer.get('css', ''), FILL['vert']), f'page script rules in the profile fill: {layer.get("css", "")[:90]}…')
    check(layer.get('fill', '') == f'rgba({FILL["vert"][0]}, {FILL["vert"][1]}, {FILL["vert"][2]}, 0.08)', f'frame fill computed in the page: {layer.get("fill")}')
    check(('%23' + '%02X%02X%02X' % FILL['vert']) in layer.get('css', '').upper(), 'cursor « viseur »: its dot is the profile fill (shield rule SVG)')
    time.sleep(.8); c1 = band_colour('e2e-1-mode-vert')
    if c1:
        log(f'     band colour (vert): {tuple(round(v) for v in c1)}')
        check(c1[1] > c1[0] and c1[1] > c1[2], 'band in green (G dominant), not the reference blue')
    # ---- live edit: another window changes « vert » to hue 55 (amber)
    sent_before = all_logs().count('ANNOTATE accent ')
    (APP / 'profiles.txt').write_text(registry(55), encoding='utf-8', newline='\n')
    user32.PostMessageW(h, user32.RegisterWindowMessageW('BrowserProfilesChanged'), 0, 0)
    got = wait(lambda: rgb_in((evaluate(READ_LAYER) or {}).get('css', ''), FILL['ambre']), 6)
    check(bool(got), 'live edit: the page rules take the new fill without leaving the mode')
    time.sleep(.6)
    sends = re.findall(r'ANNOTATE accent (\d+,\d+,\d+) sent to the page', all_logs())[sent_before:]
    check(sends == ['%d,%d,%d' % FILL['ambre']], f'one accent sent to the page, the settled value, no fade step: {sends}')
    c2 = band_colour('e2e-2-live-edit-amber')
    if c2:
        log(f'     band colour (after the edit): {tuple(round(v) for v in c2)}')
        check(c2[0] > c2[1] > c2[2], 'band repainted in amber (R > G > B)')
    # ---- switch to profile « ambre » in this window while the mode is on
    (APP / 'profiles.txt').write_text(registry(150), encoding='utf-8', newline='\n')   # vert back to green: the mode is still on its page
    user32.PostMessageW(h, user32.RegisterWindowMessageW('BrowserProfilesChanged'), 0, 0)
    wait(lambda: rgb_in((evaluate(READ_LAYER) or {}).get('css', ''), FILL['vert']), 6)
    # Counts, not offsets: each profile writes its own log file.
    before = all_logs(); sent_before = before.count('ANNOTATE accent '); offs = before.count('ANNOTATE mode off (tab change)')
    ons = len(re.findall(r'ANNOTATE mode on \(', before))
    check(send_copydata(h, 'qa switch ambre') == 1, 'qa switch ambre accepted (--workspace-test)')
    off = wait(lambda: all_logs().count('ANNOTATE mode off (tab change)') > offs, 8)
    check(bool(off), 'the mode ends with the old profile\'s page (« mode off (tab change) »)')
    time.sleep(.8)
    check(all_logs().count('ANNOTATE accent ') == sent_before, 'no accent of « ambre » sent to « vert »\'s page')
    # « ambre » runs its own engine (its own WebView2 folder; the debug port stays with « vert »'s engine): the page is
    # opened through the address bar, the value handed to the new page script is read from the log.
    wait(lambda: 'engine of vert exited' in all_logs(), 10); time.sleep(1.0)
    Q.post_key(0x4C, h, ctrl=True); time.sleep(.3)
    edit = user32.FindWindowExW(h, None, 'Edit', None)
    for ch in URL:
        user32.PostMessageW(edit, 0x102, ord(ch), 1)
    time.sleep(.2); Q.post_key(0x0D, edit); time.sleep(2.5)
    Q.post_key(0x41, h, ctrl=True, shift=True)

    def second_on():
        found = re.findall(r'ANNOTATE mode on \(.*accent (\d+),(\d+),(\d+)\)', all_logs())
        return found[ons] if len(found) > ons else None
    on2 = wait(second_on, 10)
    check(bool(on2) and tuple(int(x) for x in on2) == FILL['ambre'], f'in « ambre », a new document: its page script gets ambre\'s fill, nothing of « vert » ({on2})')
    time.sleep(.8); c3 = band_colour('e2e-3-switch-ambre')
    if c3:
        log(f'     band colour (ambre): {tuple(round(v) for v in c3)}')
        check(c3[0] > c3[1] > c3[2], 'band in amber after the switch')
    Q.post_key(0x1B, h)
    # ---- the store
    store = APP / 'annotations'
    written = [p for p in store.rglob('*.json')] if store.exists() else []
    check(not [p for p in written if 'readers' not in p.parts], f'no annotation written by this run ({len(written)} json files)')


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
