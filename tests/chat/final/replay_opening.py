#!/usr/bin/env python3
"""Windowless replay of a bubble's opening: the exchange between the host and the chat page (`layout` / `bubble.size`), and
where the page draws its foot (input, mode and model buttons) compared with the surface the host shows and makes clickable.

Why (real-window monkey of 24 Nov 2026, seed 1, #185 / #219): a click on the model button of a bubble « went through » it and
opened another element's bubble. The host's log gave the exchange; this script plays it again on the real page
(assets/chat-panel, headless Chromium from the WebView2 runtime, the fake host's fixtures) with the host's answers computed
here as src/chat-comment.h computes them. No window on the screen, no browser of ours, no profile.

    python -I tests/chat/final/replay_opening.py            # every case
    python -I tests/chat/final/replay_opening.py --assets <dir>   # another version's assets/ (git archive <commit> assets/chat-panel assets/fonts)

What it says:
  1. the host of before lot 1b — the conversation sent first in the layout of before, then the new bounds, then the opening
     layout: the sizes the page publishes, and the surface the host shows after each (shown, then corrected);
  2. the host of lot 1b — the opening layout only, then the conversation: one size, its acknowledgement, nothing corrected;
  3. the foot is inside the surface at rest (a long thread: the thread scrolls);
  4. the same with 4 messages waiting in the bubble (O5 / #110, and the clicks « through » the bubble #185 / #219): the
     waiting messages had no height limit, the input and the buttons were drawn under the surface, out of the clickable
     area — a click aimed at them reached the site's page. Fixed by lot R in assets/chat-panel (358676e, d9dbf03: 2 messages
     at most, « +N en attente », the input always whole).
     BLOCKING as soon as the page under test has lot R's fix — the script looks for it in the page itself (LOT_R_FIX below),
     so nothing is to be switched on at the merge: then the foot must be inside the surface at 269 px offered and at the
     small heights (150, 120, 104). On a page WITHOUT the fix (this branch before lot R is merged, an older version given
     with --assets) the same failure is said « DÉFAUT CONNU, corrigé par le lot R » and does not turn the pass red.

Exit code 0 when every check passes (the known defect of a page without lot R's fix aside), 1 otherwise.
"""
import base64, ctypes, glob, http.server, json, os, pathlib, re, socket, struct, subprocess, sys, tempfile, threading, time, urllib.request

for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding='utf-8')
    except Exception:
        pass

ROOT = pathlib.Path(__file__).resolve().parents[3]
ARGS = sys.argv[1:]
ASSETS = pathlib.Path(ARGS[ARGS.index('--assets') + 1]).resolve() if '--assets' in ARGS else ROOT / 'assets'
EDGE = next(iter(sorted(glob.glob(os.path.join(os.environ.get('LOCALAPPDATA', ''), 'Microsoft', 'EdgeCore', '*', 'msedge.exe'))
                        + glob.glob(os.path.join(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)'), 'Microsoft', 'EdgeCore', '*', 'msedge.exe')), reverse=True)), None)


def lot_r_fix():
    """Lot R's fix of O5 / #110 is in the page under test: its waiting messages are bounded to the room left (`bQueueRoom`,
    assets/chat-panel/panel.js, d9dbf03). Read in the page itself — true on every commit that holds the fix, whatever its branch."""
    try:
        return 'function bQueueRoom(' in (ASSETS / 'chat-panel' / 'panel.js').read_text(encoding='utf-8')
    except OSError:
        return False


LOT_R_FIX = lot_r_fix()
# without lot R's fix in the page, the foot out of the surface is the defect lot R fixes: said, never a failure of this commit
KNOWN = {} if LOT_R_FIX else {'pied-file-attente': 'O5 / #110 — les messages en attente n\'ont pas de hauteur limite ; corrigé par le lot R (assets/chat-panel), '
                                                   'pas encore dans la page testée : le contrôle devient bloquant tout seul dès que le lot R est fusionné'}

# the opening of #219 (CSS px): below its element 269 are free, above it 307; the bubble's window is 782 wide
ROOM_BELOW, ROOM_ABOVE, TOP_BELOW, TOP_ABOVE = 269, 307, 400, 212
results = []


def check(cid, ok, name, detail=''):
    known = cid in KNOWN
    mark = 'PASS' if ok else ('DÉFAUT CONNU, corrigé par le lot R' if known else 'FAIL')
    print(f'{mark} {name}' + (f' — {detail}' if detail and not ok else ''), flush=True)
    if known and not ok:
        print(f'       {KNOWN[cid]}', flush=True)
    results.append(ok or known)


class Handler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def translate_path(self, path):
        return str(ASSETS / path.split('?')[0].lstrip('/'))

    def end_headers(self):
        self.send_header('Cache-Control', 'no-store'); super().end_headers()


class Cdp:
    def __init__(self, url):
        host, port, path = re.match(r'ws://([^:/]+):(\d+)(/.*)', url).groups()
        self.s = socket.create_connection((host, int(port)), timeout=60)
        key = base64.b64encode(os.urandom(16)).decode()
        self.s.sendall(f'GET {path} HTTP/1.1\r\nHost: {host}:{port}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n'.encode())
        buf = b''
        while b'\r\n\r\n' not in buf:
            buf += self.s.recv(4096)
        self.rest = buf.split(b'\r\n\r\n', 1)[1]; self.n = 0

    def _read(self, k):
        while len(self.rest) < k:
            self.rest += self.s.recv(1 << 20)
        out, self.rest = self.rest[:k], self.rest[k:]; return out

    def _frame(self):
        b1, b2 = self._read(2); ln = b2 & 127
        if ln == 126: ln = struct.unpack('>H', self._read(2))[0]
        elif ln == 127: ln = struct.unpack('>Q', self._read(8))[0]
        data = self._read(ln)
        while not (b1 & 0x80):
            b1, b2 = self._read(2); ln = b2 & 127
            if ln == 126: ln = struct.unpack('>H', self._read(2))[0]
            elif ln == 127: ln = struct.unpack('>Q', self._read(8))[0]
            data += self._read(ln)
        return b1 & 15, data

    def call(self, method, **params):
        self.n += 1; msg = json.dumps({'id': self.n, 'method': method, 'params': params}).encode()
        head = bytes([0x81]) + (bytes([0x80 | len(msg)]) if len(msg) < 126 else bytes([0x80 | 126]) + struct.pack('>H', len(msg)) if len(msg) < 65536 else bytes([0x80 | 127]) + struct.pack('>Q', len(msg)))
        mask = os.urandom(4); self.s.sendall(head + mask + bytes(c ^ mask[i % 4] for i, c in enumerate(msg)))
        while True:
            op, data = self._frame()
            if op != 1:
                continue
            r = json.loads(data)
            if r.get('id') == self.n:
                if 'error' in r:
                    raise RuntimeError(f'{method}: {r["error"]}')
                return r.get('result', {})

    def js(self, expr):
        r = self.call('Runtime.evaluate', expression=expr, awaitPromise=True, returnByValue=True)
        if 'exceptionDetails' in r:
            raise RuntimeError('JS: ' + json.dumps(r['exceptionDetails'])[:600])
        return r['result'].get('value')


def bind_to_runner(p):
    """The headless browser dies with this script, even killed (a job object closed with the process)."""
    from ctypes import wintypes
    k32 = ctypes.WinDLL('kernel32', use_last_error=True)
    k32.CreateJobObjectW.restype = wintypes.HANDLE; k32.OpenProcess.restype = wintypes.HANDLE

    class BASIC(ctypes.Structure):
        _fields_ = [('PerProcessUserTimeLimit', ctypes.c_int64), ('PerJobUserTimeLimit', ctypes.c_int64), ('LimitFlags', wintypes.DWORD),
                    ('MinimumWorkingSetSize', ctypes.c_size_t), ('MaximumWorkingSetSize', ctypes.c_size_t), ('ActiveProcessLimit', wintypes.DWORD),
                    ('Affinity', ctypes.c_size_t), ('PriorityClass', wintypes.DWORD), ('SchedulingClass', wintypes.DWORD)]

    class IO(ctypes.Structure):
        _fields_ = [(n, ctypes.c_uint64) for n in ('R', 'W', 'O', 'RB', 'WB', 'OB')]

    class EXT(ctypes.Structure):
        _fields_ = [('Basic', BASIC), ('Io', IO), ('ProcessMemoryLimit', ctypes.c_size_t), ('JobMemoryLimit', ctypes.c_size_t),
                    ('PeakProcessMemoryUsed', ctypes.c_size_t), ('PeakJobMemoryUsed', ctypes.c_size_t)]
    job = k32.CreateJobObjectW(None, None)
    if not job:
        return None
    info = EXT(); info.Basic.LimitFlags = 0x2000   # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
    k32.SetInformationJobObject(job, 9, ctypes.byref(info), ctypes.sizeof(info))
    h = k32.OpenProcess(0x0101, False, p.pid)
    if h:
        k32.AssignProcessToJobObject(job, h); k32.CloseHandle(h)
    return job


MEASURE = """(()=>{const cs=getComputedStyle(document.documentElement),a=document.getElementById('app').getBoundingClientRect();
  const r=s=>{const e=document.querySelector(s);if(!e)return null;const b=e.getBoundingClientRect();return [Math.round(b.top),Math.round(b.bottom)]};
  return {it:cs.getPropertyValue('--it').trim(),bh:cs.getPropertyValue('--bh').trim(),up:document.documentElement.classList.contains('bub-up'),app:[Math.round(a.top),Math.round(a.bottom)],
    dock:r('.dock'),model:r('.model .m'),mode:r('.tb.mode'),input:r('.cin'),waiting:document.querySelectorAll('.bq').length}})()"""


def main() -> int:
    if not EDGE:
        print('FAIL no headless Chromium (EdgeCore) found'); return 1
    srv = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    base = f'http://127.0.0.1:{srv.server_address[1]}'
    profile = tempfile.mkdtemp(prefix='replay-opening-')
    proc = subprocess.Popen([EDGE, '--headless=new', '--disable-gpu', '--no-first-run', '--no-default-browser-check', '--disable-extensions', '--disable-sync',
                             '--disable-background-networking', '--disable-component-update', '--remote-debugging-port=0', '--user-data-dir=' + profile,
                             '--force-device-scale-factor=1', '--window-size=782,1320', 'about:blank'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    job = bind_to_runner(proc)   # noqa: F841 — kept alive: closing it ends the browser
    try:
        pf = os.path.join(profile, 'DevToolsActivePort')
        for _ in range(300):
            if os.path.exists(pf) and open(pf).read().strip():
                break
            time.sleep(.1)
        port = open(pf).read().split()[0]
        targets = json.load(urllib.request.urlopen(f'http://127.0.0.1:{port}/json/list'))
        cdp = Cdp([t for t in targets if t['type'] == 'page'][0]['webSocketDebuggerUrl'])
        for d in ('Page', 'Runtime'):
            cdp.call(d + '.enable')
        cdp.call('Emulation.setFocusEmulationEnabled', enabled=True)
        # what the fake host sends is recorded: its fixtures (state, a finished turn) are borrowed
        cdp.call('Page.addScriptToEvaluateOnNewDocument', source="window.__in=[];try{Object.defineProperty(window.chrome,'webview',{configurable:true,get(){return this._wv},"
                 "set(v){this._wv=v;v.addEventListener('message',e=>window.__in.push(JSON.parse(JSON.stringify(e.data))))}})}catch(e){window.__hookErr=String(e)}")

        def viewport(w, h):
            cdp.call('Emulation.setDeviceMetricsOverride', width=w, height=h, deviceScaleFactor=1, mobile=False)

        def send(*msgs):
            cdp.js('(()=>{for(const m of ' + json.dumps(msgs) + ')window.devHost.send(m)})()')

        def frames(n=3):
            cdp.js('new Promise(r=>{let n=' + str(n) + ';const f=()=>{if(--n<=0)r(true);else requestAnimationFrame(f)};requestAnimationFrame(f)})')

        def sizes():
            return cdp.js("(()=>{const o=window.__out.filter(m=>m.type==='bubble.size').map(m=>m.height);window.__out=[];return o})()")

        def lay(open_, maxh, side, top, element='h2.section-title'):
            return {'type': 'layout', 'mode': 'bubble', 'maxWidth': 320, 'maxHeight': maxh, 'side': side, 'element': element, 'elementKey': element, 'open': open_,
                    'room': {'top': 300, 'right': 900, 'bottom': 300, 'left': 60}, 'inset': {'top': top, 'right': 400, 'bottom': 400, 'left': 62}}

        def start():
            """A fresh page showing the bubble of ANOTHER element at rest (279 high, 520 offered); from then on this script is the host."""
            viewport(1120, 1320)
            cdp.call('Page.navigate', url=f'{base}/chat-panel/dev.html?scenario=c-3-termine')
            cdp.js("new Promise(r=>{const t0=performance.now();const f=()=>{if(window.__scenarioDone)r(true);else if(performance.now()-t0>15000)r(false);else setTimeout(f,20)};f()})")
            cdp.js("document.fonts.ready.then(()=>new Promise(r=>setTimeout(r,400)))")
            cdp.js("window.__out=[];window.chrome.webview.postMessage=function(m){window.__out.push(JSON.parse(JSON.stringify(m)))};"
                   "window.__st=window.__in.filter(m=>m.type==='state').pop();window.__items=window.__in.filter(m=>m.type==='thread.reset').pop().items;"
                   "window.__thread=function(conv,n){const out=[];for(let i=0;i<n;i++)for(const it of window.__items){const c=JSON.parse(JSON.stringify(it));"
                   "for(const k of ['id','turnId','messageId','checkpointId'])if(c[k])c[k]=c[k]+'_'+conv+i;out.push(c)}return out};true")
            send(lay(True, 520, 'right', 400, 'div.old'), *conversation('cold', 1, 0)); frames(); sizes()
            send(lay(False, 520, 'right', 400, 'div.old')); frames(); sizes()

        def conversation(conv, turns, waiting):
            st = dict(cdp.js('window.__st')); st['conversation'] = {'id': conv, 'title': 'Conversation ' + conv}; st['running'] = False
            q = [{'id': f'q{conv}{i}', 'text': 'Change la couleur du bouton en vert, puis applique styles/landing.css display: flex; gap: 8px; jeton ' + str(i), 'after': 'p.hero-text'} for i in range(waiting)]
            return [st, {'type': 'thread.reset', 'conversationId': conv, 'items': cdp.js(f'window.__thread({json.dumps(conv)},{turns})')}, {'type': 'queue', 'items': q}, {'type': 'focus', 'target': 'composer'}]

        def opening(turns, waiting, before_1b, room=ROOM_BELOW):
            """The click on h2.section-title while the bubble of div.old is open and a turn runs elsewhere (its conversation is
            shown « aside »); `room`: the height offered below the element. Returns what the host showed after each size, and
            the page's measure at rest."""
            start()
            viewport(782, 1320)
            conv = conversation('cnew', turns, waiting)
            if before_1b:   # ShowAside sent the conversation first; PlaceBubble told the new bounds twice (room, then inset); then the opening layout
                send(*conv[:3]); frames(1)
                send(lay(False, room, 'below', 0), lay(False, room, 'below', TOP_BELOW), lay(True, room, 'below', TOP_BELOW), *conv)
            else:
                send(lay(True, room, 'below', TOP_BELOW), *conv)
            host = {'size': -1, 'waiting': True, 'side': 'below', 'room': room, 'top': TOP_BELOW, 'h': 0}
            shown = []; got_all = []
            for _ in range(8):
                frames(2)
                got = sizes()
                if not got:
                    break
                for h in got:
                    got_all.append(h)
                    if h == host['size']:
                        continue   # contract §20: a repeat gets no answer
                    host['size'] = h; hc = max(40, min(520, h))
                    if host['waiting']:   # the first size: placed with it (Place, src/chat-bubble.h) — neither side holds more than its room
                        host['waiting'] = False
                        if hc > room:
                            host.update(side='above', room=ROOM_ABOVE); viewport(782, 919)
                        host['h'] = min(hc, host['room'])
                    else:
                        host['h'] = hc
                    host['top'] = TOP_BELOW if host['side'] == 'below' else TOP_ABOVE + ROOM_ABOVE - host['h']
                    shown.append((host['side'], host['h']))
                    send(lay(False, host['room'], host['side'], host['top']))
            frames(4)
            return got_all, shown, host, cdp.js(MEASURE)

        def inside(host, m):
            top, foot = host['top'], host['top'] + host['h']
            return all(m[k] and m[k][0] >= top - 1 and m[k][1] <= foot + 1 for k in ('model', 'mode', 'input'))

        # 1. before lot 1b: informative (what the monkey's log showed: 460 -> 250 -> 460 px, i.e. 368 -> 200 -> 368 CSS px there)
        got, shown, host, m = opening(4, 0, True)
        print(f'avant lot 1b · la page publie {got} ; l\'hôte montre {[f"{s} {h}" for s, h in shown]} (affichée dès la première, corrigée ensuite)', flush=True)
        check('avant', len(shown) >= 2, 'replay: the host of before lot 1b shows the bubble on a size of the layout of before, then corrects it (the defect is reproduced)', f'{got} {shown}')

        # 2. lot 1b: the opening layout only, then the conversation
        got, shown, host, m = opening(4, 0, False)
        print(f'lot 1b · la page publie {got} ; l\'hôte montre {[f"{s} {h}" for s, h in shown]}', flush=True)
        check('accuse-unique', len(shown) == 1 and shown[0] == ('below', ROOM_BELOW) and got[0] <= ROOM_BELOW,
              'lot 1b: the opening layout alone is answered by one size, the opening\'s own (at most the height offered) — shown once, nothing corrected', f'{got} {shown}')
        # 3. the foot inside the surface (a long thread)
        check('pied-fil-long', inside(host, m), 'a long thread: the input and the mode / model buttons are inside the surface the host shows (the thread scrolls)', json.dumps(m))

        # 4. four messages waiting in the bubble: blocking with lot R's fix in the page (then at the small heights too), said « known » without it
        print('lot R (messages en attente bornés) dans la page testée : ' + ('oui — le pied dans la surface est un contrôle bloquant' if LOT_R_FIX
              else 'non — le pied hors de la surface est le défaut connu que le lot R corrige (bloquant dès sa fusion)'), flush=True)
        for room in (ROOM_BELOW, 150, 120, 104) if LOT_R_FIX else (ROOM_BELOW,):
            got, shown, host, m = opening(1, 4, False, room)
            foot = host['top'] + host['h']
            check('pied-file-attente', inside(host, m) and host['h'] <= room,
                  f'4 messages waiting, {room} px offered: the input and the mode / model buttons are inside the surface the host shows',
                  f'surface {host["top"]}..{foot} CSS px in the window ({host["side"]}), page: input {m["input"]}, mode {m["mode"]}, model {m["model"]}, foot {m["dock"]} — drawn {m["model"][1] - foot if m["model"] else "?"} px under the surface, out of the clickable area')
    finally:
        subprocess.run(['taskkill', '/PID', str(proc.pid), '/T', '/F'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    ok = all(results)
    print(f'{sum(results)} / {len(results)} checks' + ('' if ok else ' — FAILED'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
