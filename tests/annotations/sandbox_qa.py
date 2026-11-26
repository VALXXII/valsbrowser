#!/usr/bin/env python3
"""Step driver for the annotation-mode QA run on the sandbox (T3). Drives ONLY its own copy of the
preview build (build/annotations-qa-app, own profile and store) — never another application.

    python -I tests/annotations/sandbox_qa.py launch            # copy build/annotations-preview, start it on the sandbox
    python -I tests/annotations/sandbox_qa.py eval "<js>"       # Runtime.evaluate in the sandbox page (CDP, loopback)
    python -I tests/annotations/sandbox_qa.py move X Y | click X Y | rclick X Y   (CSS px of the page; CDP input)
    python -I tests/annotations/sandbox_qa.py keys ctrl+shift+a | escape | enter | down ...   (SendInput, our window in front)
    python -I tests/annotations/sandbox_qa.py type "texte"      # SendInput Unicode into the focused control
    python -I tests/annotations/sandbox_qa.py shot NAME         # PNG of the browser window -> build/annotations-v1-qa/NAME.png
    python -I tests/annotations/sandbox_qa.py windows           # our process's visible top-level/child windows
    python -I tests/annotations/sandbox_qa.py quit              # WM_CLOSE to our browser

The remote debugging port (9333, 127.0.0.1) is enabled for this QA copy only, through the process
environment (WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS); the preview build itself is never started with it.
"""
from __future__ import annotations
import base64
import ctypes
import ctypes.wintypes as W
import json
import os
import pathlib
import shutil
import socket
import struct
import subprocess
import sys
import time
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = ROOT / 'build' / 'annotations-preview'
APP = ROOT / 'build' / 'annotations-qa-app'
QA = ROOT / 'build' / 'annotations-v1-qa'
PORT = 9333
URL = 'http://127.0.0.1:5173/'
user32 = ctypes.windll.user32
user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))


# ---------------------------------------------------------------- minimal WebSocket (CDP)
class WS:
    def __init__(self, url):
        assert url.startswith('ws://')
        hostport, path = url[5:].split('/', 1)
        host, port = hostport.split(':')
        self.s = socket.create_connection((host, int(port)), timeout=10)
        key = base64.b64encode(os.urandom(16)).decode()
        self.s.sendall((f'GET /{path} HTTP/1.1\r\nHost: {hostport}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n'
                        f'Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n').encode())
        resp = b''
        while b'\r\n\r\n' not in resp:
            resp += self.s.recv(4096)
        assert b' 101 ' in resp.split(b'\r\n')[0], resp[:200]
        self.buf = resp.split(b'\r\n\r\n', 1)[1]
        self.n = 0

    def _recv(self, n):
        while len(self.buf) < n:
            chunk = self.s.recv(65536)
            if not chunk:
                raise EOFError
            self.buf += chunk
        out, self.buf = self.buf[:n], self.buf[n:]
        return out

    def send(self, text):
        data = text.encode()
        head = bytearray([0x81])
        mask = os.urandom(4)
        if len(data) < 126:
            head.append(0x80 | len(data))
        elif len(data) < 65536:
            head.append(0x80 | 126); head += struct.pack('>H', len(data))
        else:
            head.append(0x80 | 127); head += struct.pack('>Q', len(data))
        self.s.sendall(bytes(head) + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(data)))

    def recv(self):
        msg = b''
        while True:
            b0, b1 = self._recv(2)
            ln = b1 & 0x7F
            if ln == 126:
                ln = struct.unpack('>H', self._recv(2))[0]
            elif ln == 127:
                ln = struct.unpack('>Q', self._recv(8))[0]
            payload = self._recv(ln)
            if (b0 & 0x0F) in (1, 0):
                msg += payload
                if b0 & 0x80:
                    return msg.decode('utf-8', 'replace')

    def call(self, method, params=None):
        self.n += 1
        self.send(json.dumps({'id': self.n, 'method': method, 'params': params or {}}))
        while True:
            m = json.loads(self.recv())
            if m.get('id') == self.n:
                if 'error' in m:
                    raise RuntimeError(m['error'])
                return m.get('result', {})


def page_ws():
    targets = json.loads(urllib.request.urlopen(f'http://127.0.0.1:{PORT}/json', timeout=5).read())
    for t in targets:
        if t.get('type') == 'page' and t.get('url', '').startswith('http://127.0.0.1:5173'):
            return WS(t['webSocketDebuggerUrl'])
    raise SystemExit('sandbox page target not found: ' + ', '.join(t.get('url', '') for t in targets))


def evaluate(js):
    ws = page_ws()
    r = ws.call('Runtime.evaluate', {'expression': js, 'returnByValue': True, 'awaitPromise': True})
    return r.get('result', {}).get('value', r)


def mouse(kind, x, y, button='left'):
    ws = page_ws()
    if kind == 'move':
        ws.call('Input.dispatchMouseEvent', {'type': 'mouseMoved', 'x': x, 'y': y})
        return
    ws.call('Input.dispatchMouseEvent', {'type': 'mouseMoved', 'x': x, 'y': y})
    ws.call('Input.dispatchMouseEvent', {'type': 'mousePressed', 'x': x, 'y': y, 'button': button, 'clickCount': 1})
    ws.call('Input.dispatchMouseEvent', {'type': 'mouseReleased', 'x': x, 'y': y, 'button': button, 'clickCount': 1})


# ---------------------------------------------------------------- our windows
def our_pid():
    p = APP / 'qa.pid'
    return int(p.read_text()) if p.exists() else 0


def main_window():
    pid = our_pid(); found = []
    @ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    def cb(h, _):
        p = W.DWORD(); user32.GetWindowThreadProcessId(h, ctypes.byref(p))
        name = ctypes.create_unicode_buffer(64); user32.GetClassNameW(h, name, 64)
        if p.value == pid and name.value == 'LisiereBrowser' and user32.IsWindowVisible(h):
            found.append(h)
        return True
    user32.EnumWindows(cb, 0)
    return found[0] if found else None


def windows():
    pid = our_pid(); out = []
    @ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    def cb(h, _):
        p = W.DWORD(); user32.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value == pid and user32.IsWindowVisible(h):
            name = ctypes.create_unicode_buffer(128); user32.GetClassNameW(h, name, 128)
            title = ctypes.create_unicode_buffer(128); user32.GetWindowTextW(h, title, 128)
            r = W.RECT(); user32.GetWindowRect(h, ctypes.byref(r))
            out.append((h, name.value, title.value, (r.left, r.top, r.right, r.bottom)))
        return True
    user32.EnumWindows(cb, 0)
    return out


def front():
    h = main_window()
    if not h:
        raise SystemExit('browser window not found')
    user32.ShowWindow(h, 9)
    # Foreground rules: a tiny Alt tap lets this process hand the foreground to our own browser window.
    user32.keybd_event(0x12, 0, 0, 0); user32.keybd_event(0x12, 0, 2, 0)
    user32.SetForegroundWindow(h)
    time.sleep(0.25)
    return h


class KI(ctypes.Structure):
    _fields_ = [('wVk', W.WORD), ('wScan', W.WORD), ('dwFlags', W.DWORD), ('time', W.DWORD), ('dwExtraInfo', ctypes.c_size_t)]


class INPUT(ctypes.Structure):
    class _U(ctypes.Union):
        _fields_ = [('ki', KI), ('pad', ctypes.c_byte * 32)]
    _fields_ = [('type', W.DWORD), ('u', _U)]


def send(vk=0, scan=0, flags=0):
    i = INPUT(); i.type = 1; i.u.ki = KI(vk, scan, flags, 0, 0)
    user32.SendInput(1, ctypes.byref(i), ctypes.sizeof(INPUT))


VK = {'ctrl': 0x11, 'shift': 0x10, 'alt': 0x12, 'escape': 0x1B, 'enter': 0x0D, 'tab': 0x09, 'up': 0x26, 'down': 0x28,
      'left': 0x25, 'right': 0x27, 'plus': 0xBB, 'minus': 0xBD, 'zero': 0x30, 'f10': 0x79, 'apps': 0x5D, 'space': 0x20}


def keys(spec):
    for combo in spec.split():
        parts = combo.lower().split('+')
        codes = [VK[p] if p in VK else ord(p.upper()) for p in parts]
        for c in codes:
            send(c)
        for c in reversed(codes):
            send(c, flags=2)
        time.sleep(0.12)


def type_text(text):
    for ch in text:
        send(0, ord(ch), 4); send(0, ord(ch), 4 | 2)
    time.sleep(0.1)


def shot(name, hwnd=None, whole=False):
    from PIL import ImageGrab
    h = hwnd or main_window()
    r = W.RECT(); user32.GetWindowRect(h, ctypes.byref(r))
    QA.mkdir(parents=True, exist_ok=True)
    img = ImageGrab.grab(bbox=(r.left, r.top, r.right, r.bottom), all_screens=True)
    path = QA / f'{name}.png'
    img.save(path)
    print(path)


def launch():
    if our_pid():
        try:
            os.kill(our_pid(), 0)
        except OSError:
            pass
    APP.mkdir(parents=True, exist_ok=True)
    for f in SRC.iterdir():   # the payload only: never a profile/session/store of the preview
        if f.name in ('profile', 'session', 'annotations', 'prototype.log', 'downloads', 'bookmarks', 'settings'):
            continue
        dst = APP / f.name
        if f.is_dir():
            shutil.copytree(f, dst, dirs_exist_ok=True)
        else:
            shutil.copy2(f, dst)
    shutil.rmtree(APP / 'sessions', ignore_errors=True)   # window sessions of a previous QA run: one window only
    (APP / 'session').write_text('browser-session-2\n0\n1\n' + json.dumps(URL) + '\n0\n', encoding='utf-8', newline='\n')
    env = dict(os.environ)
    env['WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS'] = f'--remote-debugging-port={PORT} --remote-allow-origins=http://127.0.0.1:{PORT}'
    # agent guard (tests/agent-guard/agent_guard.py): no agent in this QA — the browser's test mode with no fake agent
    # refuses every agent (the mode's « Chat à droite » would otherwise start the computer's own Claude Code), the
    # « Connecter » writes go under build/, keys stripped, the « user » folders and PATH redirected; a watch in a process
    # of its own (this launcher exits) stops the browser if an agent appears under it anyway
    sys.path.insert(0, str(ROOT / 'tests' / 'agent-guard'))
    import agent_guard as G
    env = G.secure_env(env, work=QA, exe=APP / 'browser.exe', agent=False)
    p = subprocess.Popen([str(APP / 'browser.exe')], cwd=str(APP), env=env)
    (APP / 'qa.pid').write_text(str(p.pid))
    G.clear_marker(QA)
    G.spawn_detached_watch(p.pid, env, work=QA, log=APP / 'prototype.log')
    for _ in range(100):
        try:
            page_ws(); break
        except Exception:
            time.sleep(0.3)
    print('launched', p.pid)


# ---------------------------------------------------------------- input without the input desktop
# (the screen saver may own the input desktop: SendInput and screen grabs then fail). Messages are posted
# to OUR browser windows only; modifier keys go through the browser thread's shared keyboard state.
WM_KEYDOWN, WM_KEYUP, WM_CHAR, WM_MOUSEMOVE, WM_LBUTTONDOWN, WM_LBUTTONUP = 0x100, 0x101, 0x102, 0x200, 0x201, 0x202


def scale(h):
    return user32.GetDpiForWindow(h) / 96.0


def post_click(x_dip, y_dip, hwnd=None):
    h = hwnd or main_window(); k = scale(h)
    lp = (int(y_dip * k) << 16) | (int(x_dip * k) & 0xFFFF)
    user32.PostMessageW(h, WM_MOUSEMOVE, 0, lp); time.sleep(0.05)
    user32.PostMessageW(h, WM_LBUTTONDOWN, 1, lp); time.sleep(0.05)
    user32.PostMessageW(h, WM_LBUTTONUP, 0, lp)


def post_key(vk, target=None, ctrl=False, shift=False):
    h = target or main_window()
    tid = user32.GetWindowThreadProcessId(h, None); me = ctypes.windll.kernel32.GetCurrentThreadId()
    attached = user32.AttachThreadInput(me, tid, True)
    state = (ctypes.c_ubyte * 256)(); user32.GetKeyboardState(state)
    old = bytes(state)
    if ctrl: state[0x11] = state[0xA2] = 0x80
    if shift: state[0x10] = state[0xA0] = 0x80
    user32.SetKeyboardState(state)
    user32.PostMessageW(h, WM_KEYDOWN, vk, 1); time.sleep(0.15)
    user32.PostMessageW(h, WM_KEYUP, vk, 0xC0000001); time.sleep(0.15)
    restore = (ctypes.c_ubyte * 256).from_buffer_copy(old); user32.SetKeyboardState(restore)
    if attached:
        user32.AttachThreadInput(me, tid, False)


def post_text(text, hwnd):
    for ch in text:
        user32.PostMessageW(hwnd, WM_CHAR, ord(ch), 1)
    time.sleep(0.2)


def find_child(parent, cls):
    return user32.FindWindowExW(parent, None, cls, None)


def print_window(h):
    from PIL import Image
    g = ctypes.windll.gdi32
    r = W.RECT(); user32.GetWindowRect(h, ctypes.byref(r)); w, hh = r.right - r.left, r.bottom - r.top
    if w <= 0 or hh <= 0:
        return None, r
    hdc = user32.GetDC(0); mdc = g.CreateCompatibleDC(hdc)
    class BIH(ctypes.Structure):
        _fields_ = [('biSize', W.DWORD), ('biWidth', W.LONG), ('biHeight', W.LONG), ('biPlanes', W.WORD), ('biBitCount', W.WORD),
                    ('biCompression', W.DWORD), ('biSizeImage', W.DWORD), ('a', W.LONG), ('b', W.LONG), ('c', W.DWORD), ('d', W.DWORD)]
    bi = BIH(ctypes.sizeof(BIH), w, -hh, 1, 32, 0, 0, 0, 0, 0, 0); bits = ctypes.c_void_p()
    bmp = g.CreateDIBSection(mdc, ctypes.byref(bi), 0, ctypes.byref(bits), None, 0); old = g.SelectObject(mdc, bmp)
    user32.PrintWindow(h, mdc, 2)
    img = Image.frombuffer('RGBA', (w, hh), ctypes.string_at(bits, w * hh * 4), 'raw', 'BGRA', 0, 1).copy()
    g.SelectObject(mdc, old); g.DeleteObject(bmp); g.DeleteDC(mdc); user32.ReleaseDC(0, hdc)
    return img, r


def pw_shot(name):
    """Composite of PrintWindow(PW_RENDERFULLCONTENT) for the browser and each of our visible popups."""
    main = main_window()
    base, r0 = print_window(main)
    base = base.convert('RGB')
    from PIL import Image
    wins = [w for w in windows() if w[0] != main]
    for h, cls, title, rect in reversed(wins):   # bottom-most first (EnumWindows lists the top first)
        if cls == 'BrowserAnnotationVeil':        # the modal's veil: black at 0.549, drawn as such
            base = Image.blend(base, Image.new('RGB', base.size, (0, 0, 0)), 0x8C / 255)
            continue
        img, r = print_window(h)
        if img is None:
            continue
        base.paste(img.convert('RGB'), (r.left - r0.left, r.top - r0.top))
    QA.mkdir(parents=True, exist_ok=True)
    path = QA / f'{name}.png'; base.save(path); print(path)


def main():
    cmd, args = sys.argv[1], sys.argv[2:]
    if cmd == 'launch':
        launch()
    elif cmd == 'eval':
        print(json.dumps(evaluate(args[0]), ensure_ascii=False))
    elif cmd in ('move', 'click', 'rclick'):
        mouse('move' if cmd == 'move' else 'click', float(args[0]), float(args[1]), 'right' if cmd == 'rclick' else 'left')
    elif cmd == 'keys':
        front(); keys(' '.join(args))
    elif cmd == 'keysnofront':
        keys(' '.join(args))
    elif cmd == 'type':
        type_text(args[0])
    elif cmd == 'front':
        front()
    elif cmd == 'shot':
        shot(args[0])
    elif cmd == 'shotwin':
        shot(args[0], int(args[1], 0))
    elif cmd == 'pclick':
        post_click(float(args[0]), float(args[1]), int(args[2], 0) if len(args) > 2 else None)
    elif cmd == 'pkey':
        mods = set(args[1:]); post_key(int(args[0], 0), None, 'ctrl' in mods, 'shift' in mods)
    elif cmd == 'pkeyto':
        mods = set(args[2:]); post_key(int(args[1], 0), int(args[0], 0), 'ctrl' in mods, 'shift' in mods)
    elif cmd == 'ptext':
        post_text(args[1], int(args[0], 0))
    elif cmd == 'note':   # types into the note popover's field (our window), WM_CHAR only
        pop = [w for w in windows() if w[1] == 'BrowserAnnotationNote']
        if not pop:
            raise SystemExit('no note popover')
        edit = find_child(pop[0][0], 'Edit'); user32.SendMessageW(edit, 0x0C, 0, ctypes.c_wchar_p(''))
        post_text(args[0], edit)
    elif cmd == 'pwshot':
        pw_shot(args[0])
    elif cmd == 'children':
        @ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
        def cb(h, _):
            name = ctypes.create_unicode_buffer(128); user32.GetClassNameW(h, name, 128)
            r = W.RECT(); user32.GetWindowRect(h, ctypes.byref(r))
            print(hex(h), name.value, bool(user32.IsWindowVisible(h)), (r.left, r.top, r.right, r.bottom)); return True
        user32.EnumChildWindows(int(args[0], 0), cb, 0)
    elif cmd == 'windows':
        for w in windows():
            print(hex(w[0]), w[1], repr(w[2]), w[3])
    elif cmd == 'quit':
        h = main_window()
        if h:
            user32.PostMessageW(h, 0x10, 0, 0)
    else:
        raise SystemExit('unknown command ' + cmd)


if __name__ == '__main__':
    main()
