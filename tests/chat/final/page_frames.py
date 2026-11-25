#!/usr/bin/env python3
"""The bubble's frame and the place given to the host for an element INSIDE an iframe — the page script in a real engine,
headless (no window on the screen, no network, no agent, no profile of ours).

    python tests/chat/final/page_frames.py [--out build/chat-frames]

Why (the owner's trial of 24 Nov 2026, sandbox page): a click on « Standard · 3 à 5 jours » inside the same-origin iframe
opened the bubble near the click, but its frame (« div.opt 254x35 ») was drawn at the TOP LEFT of the page, over the e-mail
field: the element's rectangle in its own frame, the frame's place in the page forgotten.

Builds tests/annotations-ui/page_v4_test.cpp (it writes the page script, as tests/annotations-ui/page_v4.py does), then
drives the script in the WebView2 runtime's Chromium (EdgeCore, --headless=new, throw-away profile) on a fixture page
served from 127.0.0.1: a same-origin iframe with a border and a padding, a nested iframe inside it, an iframe of ANOTHER
origin, a canvas, an element of the page itself. The truth for every rectangle is the engine's own (DevTools
DOM.getBoxModel: the element's border box in the top viewport, through its frames) — never the script's arithmetic.

  hover / click   inside the iframe: the hover frame is around the element, the selection says « same-origin » and its
                  rectangle in the page's viewport
  bubble frame    `follow {current}`: the frame `.bf` is around the element (4 px out), its labels on it; the place
                  posted to the host (`anchor`) is the element's — at 100 % and 125 %
  page scrolled   after a scroll of the page: frame and anchor follow, in the same image
  frame scrolled  after a scroll INSIDE the iframe: frame and anchor follow at once (not at the next 250 ms check);
                  an element partly out of its frame: only what the frame shows is framed; wholly out: no frame
  case 1 = A      (the owner, 24 Nov 2026; Paper « Iframes · cas 1 · l'élément sort de l'iframe · A ») the element leaves
                  its iframe by a scroll inside it: the place given to the host stays in the iframe's box — the part
                  still in view, then a point of the edge it left by (top, bottom; nested: the inner iframe's edge) —
                  so the bubble stays stuck to that edge, never « gone »; back in view: as before. In the page itself:
                  unchanged (the element out of the screen takes its place with it)
  case 3          (Paper « Iframes · cas 3 · l'iframe se recharge ») the iframe reloads: the bubble's frame and the
                  mark « En attente » come back around the element found again; the element no longer there: nothing
                  drawn, « gone »; after a reload of the PAGE the host gives the element back by its key
  nested          the same for an element of an iframe inside the iframe
  frame removed   the iframe leaves the page: the element is « gone » (after the 500 ms of C3)
  another origin  the iframe of another origin is ONE box (script header: « a cross-origin iframe is annotated as a box »):
                  hover, selection « cross-origin », bubble frame around the whole iframe
  canvas          one box: the canvas element
  page element    an element of the page itself: unchanged
  margins         (the owner's choice B, 24 Nov 2026; Paper « 5.3 · Espacements au survol — padding, gap et marges ») the
                  spacing inspector on a container whose children have their OWN margin: the space between two
                  children = that margin + the gap, each with its band and its value — the margin a flat light band
                  (#FBE3EF) against its child, its label bordered (1 px #D63E92, text #B0307A); the gap hatched in what
                  is left, its filled label. The Paper card to the pixel; a form like the sandbox's (« 20 » flat under
                  the title, then « 6 » hatched); a row (left / right margins); a grid; no margin: as before
Report: <out>/page-frames.json. Exit code 0 when every check passes.
"""
import base64, glob, http.server, json, os, re, shutil, socket, struct, subprocess, sys, tempfile, threading, time, urllib.request

sys.stdout.reconfigure(encoding="utf-8")
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
args = sys.argv[1:]
OUT = os.path.abspath(args[args.index("--out") + 1] if "--out" in args else os.path.join(ROOT, "build", "chat-frames"))
TOOLS = os.environ.get("W64DEVKIT_BIN") or os.path.join(ROOT, "tools", "w64devkit", "bin")
EDGE = next(iter(sorted(glob.glob(os.path.join(os.environ.get("LOCALAPPDATA", ""), "Microsoft", "EdgeCore", "*", "msedge.exe"))
                        + glob.glob(os.path.join(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"), "Microsoft", "EdgeCore", "*", "msedge.exe")), reverse=True)), None)
os.environ["BROWSER_ANNOTATIONS_TEST"] = "1"
failures, report = [], {"checks": []}


def check(ok, name, detail=""):
    report["checks"].append({"ok": bool(ok), "name": name, "detail": str(detail)})
    print(("ok   " if ok else "FAIL ") + name + (" — " + str(detail) if detail != "" and not ok else ""), flush=True)
    if not ok:
        failures.append(name)
    return bool(ok)


# ------------------------------------------------------------------ 1. the page script (written by the unit fixture)
os.makedirs(OUT, exist_ok=True)
exe = os.path.join(OUT, "page-v4-test.exe")
cmd = [os.path.join(TOOLS, "g++.exe"), "-std=c++17", "-O1", "-Wall", "-Wextra", "-Werror", "-Wno-unknown-pragmas", "-Wno-cast-function-type",
       "-Wno-missing-field-initializers", "-Wno-misleading-indentation", "-static", "-municode", "-mconsole", "-Isrc",
       "-I" + (os.environ.get("WEBVIEW2_INCLUDE") or "deps/webview2/build/native/include"), "tests/annotations-ui/page_v4_test.cpp", "src/annotations.cpp", "-o", exe,
       "-lbcrypt", "-lole32", "-luuid", "-lwindowscodecs", "-lshlwapi", "-lgdi32", "-luser32"]
r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
check(r.returncode == 0, "build: the page script's fixture (tests/annotations-ui/page_v4_test.cpp)", (r.stderr or "")[-800:])
if r.returncode:
    sys.exit(1)
r = subprocess.run([exe, OUT], cwd=ROOT, capture_output=True, text=True)
check(r.returncode == 0, "build: the page script written", (r.stdout + r.stderr)[-400:])
PAGE_JS = open(os.path.join(OUT, "page-script.js"), encoding="utf-8").read()

# ------------------------------------------------------------------ 2. the fixture site (two origins: two ports)
SITE = os.path.join(OUT, "site"); shutil.rmtree(SITE, ignore_errors=True); os.makedirs(SITE)
OTHER = os.path.join(OUT, "other-site"); shutil.rmtree(OTHER, ignore_errors=True); os.makedirs(OTHER)
open(os.path.join(OTHER, "other.html"), "w", encoding="utf-8").write(
    "<!doctype html><meta charset='utf-8'><body style='margin:0;font:14px sans-serif;background:#ddd'><p id='in' style='margin:20px'>Une autre origine</p></body>")
DEEP = ("<body style='margin:0;font:13px sans-serif'><div class='leaf' id='leaf' style='margin:9px 13px;width:120px;height:30px;background:#cde'>Feuille</div>"
        "<div style='height:600px'></div></body>")
open(os.path.join(SITE, "inner.html"), "w", encoding="utf-8").write("""<!doctype html><html><head><meta charset="utf-8">
<style>body{margin:0;font:14px sans-serif;background:#fff}h3{margin:14px 16px 6px;font-size:16px}
.opt{margin:8px 16px;height:27px;padding:4px 8px;display:flex;gap:6px;align-items:center;justify-content:space-between;border-top:1px solid #eee}
.opt button{font:13px sans-serif}</style></head><body>
<h3>Livraison</h3>
<div class="opt" id="o1"><span>Standard · 3 à 5 jours</span><button>Choisir</button></div>
<div class="opt" id="o2"><span>Express · demain</span><button>Choisir</button></div>
<iframe id="deep" srcdoc="%s" style="display:block;margin:10px 20px;width:200px;height:60px;border:2px solid #777;padding:3px 0 0 5px"></iframe>
<div style="height:260px"></div>
<div class="opt" id="o3"><span>Point relais · 2 jours</span><button>Choisir</button></div>
<div style="height:900px"></div></body></html>""" % DEEP.replace('"', "&quot;"))
open(os.path.join(SITE, "inner-empty.html"), "w", encoding="utf-8").write(
    "<!doctype html><meta charset='utf-8'><body style='margin:0;font:14px sans-serif'><h3 style='margin:14px 16px'>Livraison indisponible</h3></body>")


# the spacing inspector's page: the Paper card (11FT-0), a form like the sandbox's, a row, a grid, a container without margins
open(os.path.join(SITE, "spacing.html"), "w", encoding="utf-8").write("""<!doctype html><html><head><meta charset="utf-8">
<style>body{margin:0;font:14px sans-serif;background:#f4f4f2}*{box-sizing:border-box}
.summary{position:absolute;left:140px;top:96px;width:340px;background:#fff;border:1px solid #e2e2e0;border-radius:10px;padding:20px;display:flex;flex-direction:column;gap:14px}
.summary .t{font-size:15px;line-height:20px;margin-bottom:8px}.summary .r{display:flex;justify-content:space-between;line-height:20px}.summary .sep{height:1px;background:#e2e2e0;flex-shrink:0}
form.checkout{position:absolute;left:560px;top:60px;width:420px;display:flex;flex-direction:column;gap:6px}
form.checkout h1{font-size:28px;line-height:40px;margin:0 0 20px}form.checkout label{font-size:13px;line-height:19px;margin-top:10px}
form.checkout input{height:44px;border:1px solid #ddd;padding:0 12px}form.checkout .actions{margin-top:18px;height:44px;background:#222}
.rowf{position:absolute;left:140px;top:420px;width:380px;height:50px;display:flex;gap:12px}
.rowf>div{height:50px;background:#bbb}.rowf .a{width:90px;margin-right:8px}.rowf .b{width:110px;margin-left:5px}.rowf .c{width:60px}
.grid{position:absolute;left:560px;top:470px;width:300px;display:grid;grid-template-columns:1fr 1fr;gap:16px}
.grid>div{height:30px;background:#bbb}.grid .low{margin-top:9px}
.plain{position:absolute;left:140px;top:540px;width:300px;display:flex;flex-direction:column;gap:10px}.plain>div{height:24px;background:#bbb}</style></head><body>
<div class="summary"><div class="t">Récapitulatif</div><div class="r"><span>Tasse grès, sable</span><span>24,00 €</span></div><div class="r"><span>Carnet A5 ligné</span><span>24,00 €</span></div><div class="sep"></div><div class="r"><b>Total</b><b>48,00 €</b></div></div>
<form class="checkout"><h1>Paiement</h1><label>Adresse e-mail</label><input value="client@exemple.fr"><label>Carte</label><input><div class="actions"></div></form>
<div class="rowf"><div class="a"></div><div class="b"></div><div class="c"></div></div>
<div class="grid"><div></div><div></div><div class="low"></div><div class="low"></div></div>
<div class="plain"><div></div><div></div><div></div></div>
</body></html>""")


class Site(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *a, **k): super().__init__(*a, directory=SITE, **k)
    def log_message(self, *a): pass
    def end_headers(self): self.send_header("Cache-Control", "no-store"); super().end_headers()


class Other(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *a, **k): super().__init__(*a, directory=OTHER, **k)
    def log_message(self, *a): pass


srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Site); srv.handle_error = lambda *a: None
osrv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Other); osrv.handle_error = lambda *a: None
threading.Thread(target=srv.serve_forever, daemon=True).start(); threading.Thread(target=osrv.serve_forever, daemon=True).start()
BASE = f"http://127.0.0.1:{srv.server_address[1]}"; OBASE = f"http://127.0.0.1:{osrv.server_address[1]}"
open(os.path.join(SITE, "index.html"), "w", encoding="utf-8").write("""<!doctype html><html><head><meta charset="utf-8">
<style>body{margin:0;font:14px sans-serif;background:#f4f4f3}
#mail{position:absolute;left:83px;top:160px;width:520px;height:52px;background:#fff;border:1px solid #ddd}
.mainbox{position:absolute;left:83.4px;top:300.6px;width:210px;height:44px;background:#222}
#same{position:absolute;left:83px;top:420.5px;width:360px;height:250px;border:3px solid #999;padding:7px 5px 2px 11px;background:#fff}
#cross{position:absolute;left:500px;top:420px;width:300px;height:160px;border:0}
#cv{position:absolute;left:830px;top:420px;background:#fff}
.spacer{height:4000px}</style></head><body>
<div id="mail"></div><div class="mainbox"></div>
<iframe id="same" src="/inner.html"></iframe>
<iframe id="cross" src="%s/other.html"></iframe>
<canvas id="cv" width="140" height="120"></canvas>
<div class="spacer"></div></body></html>""" % OBASE)


# ------------------------------------------------------------------ 3. DevTools (as tests/annotations-ui/page_v4.py)
class Cdp:
    def __init__(self, url):
        host, port, path = re.match(r"ws://([^:/]+):(\d+)(/.*)", url).groups()
        self.s = socket.create_connection((host, int(port)), timeout=60)
        key = base64.b64encode(os.urandom(16)).decode()
        self.s.sendall(f"GET {path} HTTP/1.1\r\nHost: {host}:{port}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n".encode())
        buf = b""
        while b"\r\n\r\n" not in buf: buf += self.s.recv(4096)
        self.rest = buf.split(b"\r\n\r\n", 1)[1]; self.n = 0

    def _read(self, k):
        while len(self.rest) < k: self.rest += self.s.recv(1 << 20)
        out, self.rest = self.rest[:k], self.rest[k:]; return out

    def _frame(self):
        b1, b2 = self._read(2); ln = b2 & 127
        if ln == 126: ln = struct.unpack(">H", self._read(2))[0]
        elif ln == 127: ln = struct.unpack(">Q", self._read(8))[0]
        data = self._read(ln)
        while not (b1 & 0x80):
            b1, b2 = self._read(2); ln = b2 & 127
            if ln == 126: ln = struct.unpack(">H", self._read(2))[0]
            elif ln == 127: ln = struct.unpack(">Q", self._read(8))[0]
            data += self._read(ln)
        return b1 & 15, data

    def call(self, method, **params):
        self.n += 1; msg = json.dumps({"id": self.n, "method": method, "params": params}).encode()
        head = bytes([0x81]) + (bytes([0x80 | len(msg)]) if len(msg) < 126 else bytes([0x80 | 126]) + struct.pack(">H", len(msg)) if len(msg) < 65536 else bytes([0x80 | 127]) + struct.pack(">Q", len(msg)))
        mask = os.urandom(4); self.s.sendall(head + mask + bytes(c ^ mask[i % 4] for i, c in enumerate(msg)))
        while True:
            op, data = self._frame()
            if op != 1: continue
            r = json.loads(data)
            if r.get("id") == self.n:
                if "error" in r: raise RuntimeError(method + ": " + json.dumps(r["error"]))
                return r.get("result", {})

    def js(self, expr):
        r = self.call("Runtime.evaluate", expression=expr, awaitPromise=True, returnByValue=True)
        if "exceptionDetails" in r: raise RuntimeError("JS: " + json.dumps(r["exceptionDetails"])[:600])
        return r["result"].get("value")


PRE = r"""(function(){var o=Element.prototype.attachShadow;Element.prototype.attachShadow=function(i){var r=o.call(this,i);(window.__roots=window.__roots||[]).push(r);return r};
window.__msgs=[];window.chrome=window.chrome||{};window.chrome.webview={postMessage:function(m){window.__msgs.push(JSON.parse(JSON.stringify(m)))}};})();"""
H = "window[Symbol.for('browser.annotation')]"
R = "window.__roots[window.__roots.length-1]"
SAME = "document.getElementById('same').contentDocument"
DEEPDOC = SAME + ".getElementById('deep').contentDocument"

profile = tempfile.mkdtemp(prefix="page-frames-")
proc = None
try:
    if not check(bool(EDGE), "runtime: headless Chromium (EdgeCore)", EDGE or "none"):
        raise SystemExit(1)
    proc = subprocess.Popen([EDGE, "--headless=new", "--disable-gpu", "--no-first-run", "--no-default-browser-check", "--disable-extensions", "--disable-sync",
                             "--disable-background-networking", "--disable-component-update", "--remote-debugging-port=0", "--user-data-dir=" + profile,
                             "--window-size=1100,700", "about:blank"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    pf = os.path.join(profile, "DevToolsActivePort")
    for _ in range(300):
        if os.path.exists(pf) and open(pf).read().strip(): break
        time.sleep(.1)
    port = open(pf).read().split()[0]
    cdp = Cdp([t for t in json.load(urllib.request.urlopen(f"http://127.0.0.1:{port}/json/list")) if t["type"] == "page"][0]["webSocketDebuggerUrl"])
    for d in ("Page", "Runtime", "DOM"): cdp.call(d + ".enable")
    cdp.call("Emulation.setFocusEmulationEnabled", enabled=True)
    cdp.call("Page.addScriptToEvaluateOnNewDocument", source=PRE)

    def frames(n=2):
        cdp.js("new Promise(r=>{let k=%d;const f=()=>{if(--k<=0)r(1);else requestAnimationFrame(f)};requestAnimationFrame(f)})" % (n + 1))

    def wait(ms): cdp.js(f"new Promise(r=>setTimeout(r,{ms}))")

    def mouse(x, y, kind="mouseMoved", button="none", clicks=0):
        cdp.call("Input.dispatchMouseEvent", type=kind, x=x, y=y, modifiers=0, button=button, clickCount=clicks)

    def load(dpr):
        cdp.call("Emulation.setDeviceMetricsOverride", width=1100, height=700, deviceScaleFactor=dpr, mobile=False)
        cdp.call("Page.navigate", url=BASE + "/index.html")
        cdp.js("new Promise(r=>{const f=()=>{const a=document.getElementById('same');const d=a&&a.contentDocument&&a.contentDocument.getElementById('deep');"
               "(document.readyState==='complete'&&d&&d.contentDocument&&d.contentDocument.getElementById('leaf'))?r(1):setTimeout(f,20)};f()})")
        cdp.js("scrollTo(0,0)"); cdp.js("window.__msgs.length=0")
        params = {"nonce": "nf", "navStamp": 1, "styles": [], "attrs": ["id", "class"], "accent": "62,103,255", "reduced": False,
                  "cursor": {"svg": "<svg xmlns='http://www.w3.org/2000/svg' width='32' height='32'><circle cx='16' cy='16' r='4' fill='red'/></svg>", "x": 16, "y": 16}}
        ok = cdp.js("(" + PAGE_JS + ")(" + json.dumps(params) + ")")
        frames()
        return ok == "ok"

    def truth(expr):
        """The element's border box in the TOP viewport, as the engine says it (through its frames): [x, y, w, h]."""
        oid = cdp.call("Runtime.evaluate", expression=expr)["result"]["objectId"]
        q = cdp.call("DOM.getBoxModel", objectId=oid)["model"]["border"]
        xs, ys = q[0::2], q[1::2]
        return [min(xs), min(ys), max(xs) - min(xs), max(ys) - min(ys)]

    def box(cls):
        return cdp.js(f"(()=>{{const e={R}.querySelector('{cls}');if(!e||getComputedStyle(e).display==='none'||(e.closest('.w')&&e.closest('.w').style.display==='none'))return null;"
                      "const b=e.getBoundingClientRect();return [b.left,b.top,b.width,b.height]})()")

    def around(b, t, out=4, tol=1.0):
        """The frame `b` is the element `t` + `out` px on each side (Paper 18QJ-0; device-pixel snapping: `tol`)."""
        return bool(b) and abs(b[0] - (t[0] - out)) <= tol and abs(b[1] - (t[1] - out)) <= tol and abs(b[2] - (t[2] + 2 * out)) <= 2 * tol and abs(b[3] - (t[3] + 2 * out)) <= 2 * tol

    def anchor():
        return cdp.js("window.__msgs.filter(m=>m.op==='anchor').pop()||null")

    def anchored(t, tol=0.6):
        a = anchor()
        return bool(a) and not a.get("gone") and abs(a["x"] - t[0]) <= tol and abs(a["y"] - t[1]) <= tol and abs(a["w"] - t[2]) <= tol and abs(a["h"] - t[3]) <= tol

    def stuck(t, e):
        """What the iframe's box `e` leaves of the element `t` (case 1): the part in it, or a point of the edge it left by."""
        l = min(max(t[0], e[0]), e[0] + e[2]); tp = min(max(t[1], e[1]), e[1] + e[3])
        r = min(max(t[0] + t[2], e[0]), e[0] + e[2]); b = min(max(t[1] + t[3], e[1]), e[1] + e[3])
        return [l, tp, max(0, r - l), max(0, b - tp)]

    def gone():
        return cdp.js("window.__msgs.filter(m=>m.op==='anchor'&&m.gone).length")

    def reload_same(url=None):
        """The same-origin iframe reloads (or goes to `url`); returns once its new document is there."""
        cdp.js("window.__gen=(window.__gen||0)+1;document.getElementById('same').contentWindow.__old=1")
        cdp.js("document.getElementById('same').contentWindow.location." + (f"replace({json.dumps(url)})" if url else "reload()"))
        cdp.js("new Promise(r=>{const f=()=>{const w=document.getElementById('same').contentWindow;(w&&!w.__old&&w.document.readyState==='complete')?r(1):setTimeout(f,10)};f()})")

    def labels():
        return cdp.js(f"[...{R}.querySelectorAll('.bl')].map(e=>{{const b=e.getBoundingClientRect();return [e.textContent,b.left,b.top,b.width,b.height]}})")

    def select(expr, label):
        """Hover, click, then what the host does when the bubble opens on an element with no selector of the top document."""
        cdp.js(f"{H}.cmd('resume')")   # the selection of before is released: the hover follows the pointer again
        t = truth(expr); x, y = t[0] + min(t[2] - 3, 40), t[1] + t[3] / 2
        mouse(x, y); frames(3); wait(180)
        hov = box('.f')
        n0 = cdp.js("window.__msgs.length")
        mouse(x, y, "mousePressed", "left", 1); mouse(x, y, "mouseReleased", "left", 1); frames(3)
        sel = cdp.js(f"window.__msgs.slice({n0}).filter(m=>m.op==='select').pop()||null")
        cdp.js(f"{H}.cmd('follow',{{current:true,draw:{{label:{json.dumps(label)}}}}})"); frames(3)
        return t, hov, sel

    for dpr in (1.0, 1.25):
        pc = f"{int(dpr * 100)} %"
        if not check(load(dpr), f"{pc} · page script injected"):
            continue
        O1 = SAME + ".getElementById('o1')"

        # ---------------- the owner's case: an element of the same-origin iframe
        t, hov, sel = select(O1 + ".querySelector('span')", "span")
        check(around(hov, t), f"{pc} · iframe: the hover frame is around the element under the pointer", {"frame": hov, "element": t})
        cdp.js(f"{H}.cmd('follow',null)"); frames(2)
        # « Standard · 3 à 5 jours »: the row itself (its padding is under the pointer at its left edge)
        cdp.js(f"{H}.cmd('resume')"); t = truth(O1); x, y = t[0] + 3, t[1] + t[3] / 2
        mouse(x, y); frames(3); wait(180); hov = box('.f'); n0 = cdp.js("window.__msgs.length")
        mouse(x, y, "mousePressed", "left", 1); mouse(x, y, "mouseReleased", "left", 1); frames(3)
        sel = cdp.js(f"window.__msgs.slice({n0}).filter(m=>m.op==='select').pop()||null")
        rect = sel and sel["target"]["rect"]
        check(bool(sel) and (sel.get("frame") or {}).get("kind") == "same-origin" and sel["target"]["path"][-1].startswith("div.opt")
              and abs(rect["x"] - t[0]) < 0.6 and abs(rect["y"] - t[1]) < 0.6 and abs(rect["width"] - t[2]) < 0.6,
              f"{pc} · iframe: the click selects div.opt, « same-origin », its rectangle in the page's viewport", {"select": sel and [sel.get("frame"), sel["target"]["path"][-1], rect], "element": t})
        cdp.js("window.__msgs.length=0"); cdp.js(f"{H}.cmd('follow',{{current:true,draw:{{label:'div.opt'}}}})"); frames(3)
        bf = box('.bf'); mail = truth("document.getElementById('mail')")
        check(around(bf, t), f"{pc} · iframe: the BUBBLE's frame is around the element (the owner's bug: it was at the top left of the page)", {"frame": bf, "element": t})
        check(bool(bf) and not (bf[1] < mail[1] + mail[3] and bf[1] + bf[3] > mail[1]), f"{pc} · iframe: …and not over the e-mail field", {"frame": bf, "mail": mail})
        ls = labels()
        check(len(ls) == 2 and ls[0][0] == 'div.opt' and ls[1][0] == f"{round(t[2])}x{round(t[3])}" and bool(bf) and all(abs((l[2] + 20) - bf[1]) <= 1.5 and bf[0] - 1.5 <= l[1] and l[1] + l[3] <= bf[0] + bf[2] + 1.5 for l in ls),
              f"{pc} · iframe: its two labels (name, size) sit on the frame", {"labels": ls, "frame": bf})
        check(anchored(t), f"{pc} · iframe: the place given to the host is the element's (the bubble is next to it)", {"anchor": anchor(), "element": t})

        # ---------------- the page scrolls: frame and anchor follow, in the same image
        cdp.js("scrollTo(0,137)"); frames(1)
        t2 = truth(O1); bf = box('.bf')
        check(abs(t2[1] - (t[1] - 137)) < 0.6 and around(bf, t2) and anchored(t2), f"{pc} · iframe, page scrolled by 137 px: the frame and the place given to the host follow at once",
              {"frame": bf, "anchor": anchor(), "element": t2})

        # ---------------- the iframe's own scroll
        cdp.js(SAME + ".defaultView.scrollTo(0,18)"); frames(1)
        t3 = truth(O1); bf = box('.bf')
        check(abs(t3[1] - (t2[1] - 18)) < 0.6 and around(bf, t3) and anchored(t3), f"{pc} · iframe scrolled by 18 px inside: the frame and the place follow at once (its scroll is heard)",
              {"frame": bf, "anchor": anchor(), "element": t3})
        fr = truth("document.getElementById('same')")   # the frame's content box starts under its border (3) and padding (7): top + 10
        cdp.js(SAME + ".defaultView.scrollTo(0,60)"); frames(2)
        t4 = truth(O1); bf = box('.bf'); inner_top = fr[1] + 10
        cut = t4[1] < inner_top < t4[1] + t4[3]
        check(cut and bool(bf) and abs(bf[1] - (inner_top - 4)) <= 1.0 and abs((bf[1] + bf[3]) - (t4[1] + t4[3] + 4)) <= 1.0,
              f"{pc} · iframe scrolled, the element partly out of it: only what the iframe shows is framed", {"frame": bf, "element": t4, "iframe content top": inner_top})
        check(anchored(stuck(t4, fr)), f"{pc} · case 1 · partly out: the place given to the host is the part of the element still in the iframe", {"anchor": anchor(), "expected": stuck(t4, fr)})
        cdp.js("window.__msgs.length=0"); cdp.js(SAME + ".defaultView.scrollTo(0,400)"); frames(2)
        t5 = truth(O1); bf = box('.bf'); want = [t5[0], fr[1], t5[2], 0]
        check(t5[1] + t5[3] < inner_top and bf is None, f"{pc} · iframe scrolled, the element wholly out of it: no frame drawn over the page", {"frame": bf, "element": t5})
        check(stuck(t5, fr) == want and anchored(want) and gone() == 0,
              f"{pc} · case 1 · wholly out by the top: the place given to the host stays on the iframe's top edge (Paper: the bubble stuck to it), never « gone »", {"anchor": anchor(), "expected": want, "gone": gone()})
        cdp.js(SAME + ".defaultView.scrollTo(0,700)"); frames(2); wait(700)
        check(anchored(want) and gone() == 0 and box('.bf') is None, f"{pc} · case 1 · scrolled further, 700 ms later: still stuck to the same edge, still not « gone »", {"anchor": anchor(), "gone": gone()})
        cdp.js("scrollTo(0,97)"); frames(2); fr2 = truth("document.getElementById('same')"); t5b = truth(O1); want2 = [t5b[0], fr2[1], t5b[2], 0]
        check(abs(fr2[1] - (fr[1] - (97 - 137))) < 0.6 and anchored(want2), f"{pc} · case 1 · the page scrolls while it is stuck: the place moves with the iframe's edge", {"anchor": anchor(), "expected": want2})
        cdp.js("scrollTo(0,137)"); cdp.js(SAME + ".defaultView.scrollTo(0,0)"); frames(2)
        t6 = truth(O1); check(around(box('.bf'), t6) and anchored(t6), f"{pc} · iframe scrolled back: the frame is back around the element, its place is the element's again")
        cdp.js(f"{H}.cmd('follow',null)"); cdp.js("scrollTo(0,0)"); frames(2)

        # ---------------- case 1, the other side: an element low in the iframe leaves by the bottom
        O3 = SAME + ".getElementById('o3').querySelector('span')"   # the text of the row: what is under the pointer
        cdp.js(SAME + ".defaultView.scrollTo(0," + SAME + ".getElementById('o3').offsetTop-90)"); frames(2)
        t, hov, sel = select(O3, "span"); fr = truth("document.getElementById('same')")
        check(around(box('.bf'), t) and anchored(t), f"{pc} · case 1 · an element low in the iframe (the iframe scrolled to it): framed, its place the element's", {"frame": box('.bf'), "anchor": anchor(), "element": t})
        cdp.js("window.__msgs.length=0"); cdp.js(SAME + ".defaultView.scrollTo(0,0)"); frames(2)
        t2 = truth(O3); want = [t2[0], fr[1] + fr[3], t2[2], 0]
        check(t2[1] > fr[1] + fr[3] and box('.bf') is None and stuck(t2, fr) == want and anchored(want) and gone() == 0,
              f"{pc} · case 1 · wholly out by the bottom: the place given to the host stays on the iframe's BOTTOM edge", {"anchor": anchor(), "expected": want, "element": t2})
        cdp.js(f"{H}.cmd('follow',null)"); frames(2)

        # ---------------- nested: an iframe inside the iframe
        LEAF = DEEPDOC + ".getElementById('leaf')"
        t, hov, sel = select(LEAF, "div.leaf")
        check(around(hov, t) and bool(sel) and (sel.get("frame") or {}).get("kind") == "same-origin" and sel["target"]["path"][-1].startswith("div.leaf"),
              f"{pc} · nested iframe: hover and click reach the element of the inner iframe", {"hover": hov, "select": sel and [sel.get("frame"), sel["target"]["path"][-1]], "element": t})
        bf = box('.bf')
        check(around(bf, t) and anchored(t), f"{pc} · nested iframe: the bubble's frame and the place given to the host are the element's", {"frame": bf, "anchor": anchor(), "element": t})
        cdp.js("scrollTo(0,93)"); cdp.js(SAME + ".defaultView.scrollTo(0,11)"); frames(2)
        t2 = truth(LEAF); bf = box('.bf')
        check(around(bf, t2) and anchored(t2), f"{pc} · nested iframe, page and outer iframe scrolled: both follow", {"frame": bf, "anchor": anchor(), "element": t2})
        cdp.js("window.__msgs.length=0"); cdp.js(DEEPDOC + ".defaultView.scrollTo(0,300)"); frames(2)
        check(box('.bf') is None, f"{pc} · nested iframe scrolled, the element out of it: no frame", box('.bf'))
        dp = truth(SAME + ".getElementById('deep')"); t3 = truth(LEAF); want = [t3[0], dp[1], t3[2], 0]
        check(anchored(want) and gone() == 0, f"{pc} · case 1 · nested: the place given to the host stays on the INNER iframe's top edge", {"anchor": anchor(), "expected": want, "inner iframe": dp})
        cdp.js(DEEPDOC + ".defaultView.scrollTo(0,0)"); cdp.js(SAME + ".defaultView.scrollTo(0,0)"); cdp.js("scrollTo(0,0)"); frames(2)
        cdp.js(f"{H}.cmd('follow',null)"); frames(2)

        # ---------------- an iframe of another origin: one box
        cdp.js(f"{H}.cmd('resume')"); t = truth("document.getElementById('cross')"); x, y = t[0] + 60, t[1] + 40
        mouse(x, y); frames(3); wait(180); hov = box('.f'); n0 = cdp.js("window.__msgs.length")
        mouse(x, y, "mousePressed", "left", 1); mouse(x, y, "mouseReleased", "left", 1); frames(3)
        sel = cdp.js(f"window.__msgs.slice({n0}).filter(m=>m.op==='select').pop()||null")
        check(around(hov, t) and bool(sel) and (sel.get("frame") or {}).get("kind") == "cross-origin" and sel["target"]["path"][-1].startswith("iframe"),
              f"{pc} · iframe of another origin: one box — hover and selection are the iframe itself, « cross-origin »", {"hover": hov, "select": sel and [sel.get("frame"), sel["target"]["path"][-1]], "element": t})
        cdp.js("window.__msgs.length=0"); cdp.js(f"{H}.cmd('follow',{{selector:'#cross',draw:{{label:'iframe'}}}})"); frames(3)
        check(around(box('.bf'), t) and anchored(t), f"{pc} · iframe of another origin: the bubble's frame is around the whole iframe", {"frame": box('.bf'), "anchor": anchor(), "element": t})
        cdp.js(f"{H}.cmd('follow',null)"); frames(2)

        # ---------------- a canvas: one box
        t, hov, sel = select("document.getElementById('cv')", "canvas")
        check(around(hov, t) and bool(sel) and sel["target"]["path"][-1].startswith("canvas") and around(box('.bf'), t) and anchored(t),
              f"{pc} · canvas: one box — hover, selection and the bubble's frame are the canvas", {"hover": hov, "frame": box('.bf'), "select": sel and sel["target"]["path"][-1], "element": t})
        cdp.js(f"{H}.cmd('follow',null)"); frames(2)

        # ---------------- an element of the page itself: as before
        t, hov, sel = select("document.querySelector('.mainbox')", "div.mainbox")
        check(around(hov, t) and around(box('.bf'), t) and anchored(t), f"{pc} · element of the page itself: unchanged", {"hover": hov, "frame": box('.bf'), "anchor": anchor(), "element": t})
        cdp.js("scrollTo(0,55)"); frames(1); t2 = truth("document.querySelector('.mainbox')")
        check(around(box('.bf'), t2) and anchored(t2), f"{pc} · element of the page itself, page scrolled: unchanged")
        cdp.js("scrollTo(0,0)"); cdp.js(f"{H}.cmd('follow',null)"); frames(2)

    # ---------------- in the page itself nothing changes: an element out of the screen keeps its true place (rule 6 of #130)
    if load(1.0):
        t, hov, sel = select("document.querySelector('.mainbox')", "div.mainbox")
        cdp.js("scrollTo(0,900)"); frames(2); t2 = truth("document.querySelector('.mainbox')")
        check(t2[1] + t2[3] < 0 and anchored(t2), "case 1 · in the page itself: an element scrolled out of the screen keeps its true place (it takes its bubble with it)", {"anchor": anchor(), "element": t2})
        cdp.js("scrollTo(0,0)"); cdp.js(f"{H}.cmd('follow',null)"); frames(2)

    # ---------------- case 3: the iframe reloads
    if load(1.0):
        O1 = SAME + ".getElementById('o1').querySelector('span')"; O2 = SAME + ".getElementById('o2')"   # the bubble's element: the text of the first row
        t, hov, sel = select(O1, "span")
        found = cdp.js(f"{H}.cmd('cmarks',[{{id:'c9',key:'same-origin||#o2',state:'waiting',waitLabel:'En attente'}}])"); frames(3)
        t2 = truth(O2); tag = cdp.js(f"(()=>{{const e={R}.querySelector('.ct');return e?e.textContent:''}})()")
        check(found == ['c9'] and around(box('.cf'), t2) and 'En attente' in tag,
              "case 3 · an element of the iframe with a message waiting has its frame and its mark « En attente » (found by its key)", {"found": found, "mark": box('.cf'), "tag": tag, "element": t2})
        check(around(box('.bf'), t), "case 3 · before: the bubble's element has its frame", {"frame": box('.bf'), "element": t})
        reload_same(); frames(4); wait(350)   # (its place is the same as before the reload: nothing new to tell the host)
        t = truth(O1); t2 = truth(O2)
        check(around(box('.bf'), t) and anchored(t) and gone() == 0, "case 3 · the iframe reloaded: the bubble's frame is back around the element found again, its place given to the host, never « gone »",
              {"frame": box('.bf'), "anchor": anchor(), "element": t, "gone": gone()})
        wait(450)   # the marks are placed again at their next check (400 ms)
        check(around(box('.cf'), t2), "case 3 · the iframe reloaded: the mark « En attente » is back around its element", {"mark": box('.cf'), "element": t2})
        cdp.js("scrollTo(0,61)"); frames(2); t = truth(O1); t2 = truth(O2)
        check(around(box('.bf'), t) and around(box('.cf'), t2), "case 3 · …and both follow a scroll of the page as before", {"frame": box('.bf'), "mark": box('.cf')})
        cdp.js("scrollTo(0,0)"); frames(2)
        # the element is no longer there after the reload
        cdp.js("window.__msgs.length=0"); reload_same(BASE + "/inner-empty.html"); frames(4); wait(1100)
        found = cdp.js(f"{H}.cmd('cmarks',[{{id:'c9',key:'same-origin||#o2',state:'waiting',waitLabel:'En attente'}}])")
        check(box('.bf') is None and box('.cf') is None and found == [] and gone() == 1,
              "case 3 · the iframe reloaded without the element: no frame, no mark, said « gone » once (the conversation keeps its chip: the host's)", {"frame": box('.bf'), "mark": box('.cf'), "found": found, "gone": gone()})
        # back: found again, even after « gone »
        cdp.js("window.__msgs.length=0"); reload_same(BASE + "/inner.html"); frames(4); wait(700)
        t = truth(O1); t2 = truth(O2)
        check(around(box('.bf'), t) and anchored(t) and around(box('.cf'), t2), "case 3 · the element is there again: frame, place and mark come back", {"frame": box('.bf'), "mark": box('.cf'), "anchor": anchor()})
        cdp.js(f"{H}.cmd('follow',null)"); cdp.js(f"{H}.cmd('cmarks',[])"); frames(2)

    # ---------------- case 3, after a reload of the PAGE: the host gives the element back by its key
    if load(1.0):
        ok = cdp.js(f"{H}.cmd('follow',{{key:'same-origin||#o1',draw:{{label:'div.opt'}}}})"); frames(3)
        t = truth(SAME + ".getElementById('o1')")
        check(ok is True and around(box('.bf'), t) and anchored(t), "case 3 · after a reload of the page: `follow {key}` finds the iframe's element, frames it, gives its place", {"ok": ok, "frame": box('.bf'), "anchor": anchor(), "element": t})
        cdp.js(f"{H}.cmd('follow',null)"); frames(2)
        no = [cdp.js(f"{H}.cmd('follow',{{key:{json.dumps(k)},draw:{{label:'x'}}}})") for k in ('same-origin|x-card>>|#o1', 'cross-origin||#o1', 'same-origin||.opt', 'same-origin||#nothing', 'main||#o1')]
        cdp.js(f"{H}.cmd('follow',null)"); frames(2)
        check(no == [False] * 5, "case 3 · a key that names a shadow path, another origin, several elements, or nothing: not followed (nothing drawn)", no)

    # ---------------- the spacing inspector: a child's own margin apart from the gap (choice B, Paper 5.3)
    INS = f"""(()=>{{const r={R},rects=c=>{{const e=r.querySelector(c);if(!e||e.style.display==='none')return [];const out=[];const re=/M\\s*([-\\d.]+)[\\s,]+([-\\d.]+)\\s*h\\s*([-\\d.]+)\\s*v\\s*([-\\d.]+)\\s*h/gi;let m;
      while((m=re.exec(e.style.clipPath||'')))out.push([+m[1],+m[2],+m[3],+m[4]]);return out}},
      labs=c=>[...r.querySelectorAll(c)].filter(e=>e.style.display!=='none').map(e=>{{const b=e.getBoundingClientRect(),s=getComputedStyle(e);return {{t:e.textContent,x:b.left,y:b.top,w:b.width,h:b.height,bg:s.backgroundColor,color:s.color,border:s.borderTopWidth+' '+s.borderTopStyle+' '+s.borderTopColor}}}}),
      mb=r.querySelector('.mb');
      return {{shown:getComputedStyle(r.querySelector('.i')).opacity,margins:rects('.mb'),gaps:rects('.gb'),ml:labs('.ml'),gl:labs('.gl'),pl:labs('.pl'),
               fill:mb?getComputedStyle(mb).backgroundColor+' / '+getComputedStyle(mb).backgroundImage:''}}}})()"""

    def near(a, b, tol=0.6):
        return len(a) == len(b) and all(abs(x - y) <= tol for x, y in zip(a, b))

    def bands(got, want):
        return len(got) == len(want) and all(near(g, w) for g, w in zip(sorted(got), sorted(want)))

    def hover_box(sel, dx=5, dy=5):
        """The pointer rests on the container itself (its padding / a gap), the inspector has faded in."""
        b = truth(f"document.querySelector({json.dumps(sel)})"); cdp.js(f"{H}.cmd('resume')")
        mouse(990, 690); frames(2); mouse(b[0] + dx, b[1] + dy); frames(3); wait(450)
        return b, cdp.js(INS)

    def load_spacing(dpr):
        cdp.call("Emulation.setDeviceMetricsOverride", width=1100, height=700, deviceScaleFactor=dpr, mobile=False)
        cdp.call("Page.navigate", url=BASE + "/spacing.html")
        cdp.js("new Promise(r=>{const f=()=>document.readyState==='complete'?r(1):setTimeout(f,20);f()})"); cdp.js("window.__msgs.length=0")
        params = {"nonce": "nf", "navStamp": 1, "styles": [], "attrs": ["id", "class"], "accent": "62,103,255", "reduced": False,
                  "cursor": {"svg": "<svg xmlns='http://www.w3.org/2000/svg' width='32' height='32'><circle cx='16' cy='16' r='4' fill='red'/></svg>", "x": 16, "y": 16}}
        ok = cdp.js("(" + PAGE_JS + ")(" + json.dumps(params) + ")"); frames()
        return ok == "ok"

    for dpr in (1.0, 1.25):
        pc = f"{int(dpr * 100)} %"
        if not check(load_spacing(dpr), f"{pc} · spacing page: script injected"):
            continue
        # ---- the Paper card (« Inspecteur » 11GG-0, at the card's box): margin 26IE-0, its label 26IF-0, the gaps 11GQ-0 … 11GW-0, « 14 » 11H7-0
        b, s = hover_box('.summary')
        X, Y = b[0], b[1]
        check(near(b, [140, 96, 340, 187]) and s["shown"] == "1", f"{pc} · margins · the Paper card is 340 x 187 and its inspector is shown", {"box": b, "shown": s["shown"]})
        check(bands(s["margins"], [[X + 21, Y + 41, 298, 8]]), f"{pc} · margins · Paper: the title's own margin is ONE flat band against it (298 x 8 at 21, 41)", s["margins"])
        check(bands(s["gaps"], [[X + 21, Y + 49, 298, 14], [X + 21, Y + 83, 298, 14], [X + 21, Y + 117, 298, 14], [X + 21, Y + 132, 298, 14]]),
              f"{pc} · margins · Paper: the gap is hatched in what is left (4 bands of 14, the first at 49: under the margin, no longer 22 high)", s["gaps"])
        check(s["fill"].startswith("rgb(251, 227, 239)") and s["fill"].endswith("none"), f"{pc} · margins · Paper: the margin's band is a flat #FBE3EF, no hatching", s["fill"])
        gl = s["gl"][0] if len(s["gl"]) == 1 else None; ml = s["ml"][0] if len(s["ml"]) == 1 else None
        check(bool(gl) and gl["t"] == "14" and abs((gl["x"] + gl["w"] / 2) - (X + 170)) <= 0.6 and abs(gl["y"] - (Y + 49)) <= 0.6 and gl["bg"] == "rgb(214, 62, 146)",
              f"{pc} · margins · Paper: the gap's value « 14 », filled #D63E92, centred on its band (top 49)", gl)
        check(bool(ml) and ml["t"] == "8" and abs(ml["y"] - (Y + 38)) <= 0.6 and abs(ml["h"] - 14) <= 0.1 and ml["bg"] == "rgb(251, 227, 239)" and ml["color"] == "rgb(176, 48, 122)"
              and ml["border"] == "1px solid rgb(214, 62, 146)",
              f"{pc} · margins · Paper: the margin's value « 8 » — 14 high at top 38 (centred on its band), #FBE3EF, 1 px #D63E92 border, text #B0307A", ml)
        check(bool(gl and ml) and abs(ml["x"] - (gl["x"] + gl["w"] + 12)) <= 0.6, f"{pc} · margins · Paper: the margin's label sits 12 px after the gap's (192 = 160 + 20 + 12)", {"gap label": gl, "margin label": ml})
        check(len(s["pl"]) == 2 and sorted(l["t"] for l in s["pl"]) == ["20", "20"], f"{pc} · margins · the padding's two « 20 » are as before", s["pl"])

        # ---- a form like the sandbox's: gap 6, the title's margin 20, the labels' 10, the actions' 18
        b, s = hover_box('form.checkout', dx=200, dy=41)   # a point of the first space between two children
        kids = cdp.js("[...document.querySelector('form.checkout').children].map(e=>{const r=e.getBoundingClientRect();return [r.top,r.bottom]})")
        X, W = b[0], b[2]
        wm = [[X, kids[0][1], W, 20], [X, kids[1][0] - 10, W, 10], [X, kids[3][0] - 10, W, 10], [X, kids[5][0] - 18, W, 18]]
        wg = [[X, kids[0][1] + 20, W, 6], [X, kids[1][1], W, 6], [X, kids[2][1], W, 6], [X, kids[3][1], W, 6], [X, kids[4][1], W, 6]]
        check(bands(s["margins"], wm), f"{pc} · margins · form: « 20 » flat under the title, « 10 » above each label, « 18 » above the actions — each against its child", {"got": s["margins"], "want": wm})
        check(bands(s["gaps"], wg), f"{pc} · margins · form: then the gap, 6 px hatched, between them (it was one band of 36 under the title)", {"got": s["gaps"], "want": wg})
        check([l["t"] for l in s["gl"]] == ["6"] and sorted(l["t"] for l in s["ml"]) == ["10", "18", "20"],
              f"{pc} · margins · form: one « 6 » for the gap; one label per distinct margin (20, 10, 18 — the two « 10 » share one)", {"gap": [l["t"] for l in s["gl"]], "margins": [l["t"] for l in s["ml"]]})
        boxes = [[l["x"], l["y"], l["w"], l["h"]] for l in s["gl"] + s["ml"]]
        check(all(not (a[0] < c[0] + c[2] and c[0] < a[0] + a[2] and a[1] < c[1] + c[3] and c[1] < a[1] + a[3]) for i, a in enumerate(boxes) for c in boxes[i + 1:]),
              f"{pc} · margins · form: no value label over another", boxes)

        # ---- a row: the left child's right margin, the gap, the right child's left margin
        b, s = hover_box('.rowf', dx=95, dy=25)
        k = cdp.js("[...document.querySelector('.rowf').children].map(e=>{const r=e.getBoundingClientRect();return [r.left,r.right]})"); Yr, Hr = b[1], b[3]
        check(bands(s["margins"], [[k[0][1], Yr, 8, Hr], [k[1][0] - 5, Yr, 5, Hr]]) and bands(s["gaps"], [[k[0][1] + 8, Yr, 12, Hr], [k[1][1], Yr, 12, Hr]])
              and [l["t"] for l in s["gl"]] == ["12"] and sorted(l["t"] for l in s["ml"]) == ["5", "8"],
              f"{pc} · margins · row: the right margin (8) against the first child, the gap (12), the left margin (5) against the second; values 12, 8, 5", s)

        # ---- a grid: the gap just before the next track, the child's top margin against it
        b, s = hover_box('.grid', dx=150, dy=38)
        k = cdp.js("[...document.querySelector('.grid').children].map(e=>{const r=e.getBoundingClientRect();return [r.top,r.bottom,r.left,r.right]})")
        check(bands(s["margins"], [[b[0], k[2][0] - 9, b[2], 9]]) and any(near(g, [b[0], k[2][0] - 9 - 16, b[2], 16]) for g in s["gaps"]) and [l["t"] for l in s["ml"]] == ["9"],
              f"{pc} · margins · grid: the second row's top margin (9) against it, the row gap (16) just before it", s)

        # ---- no margin of their own: as before (no margin band, no margin label)
        b, s = hover_box('.plain', dx=150, dy=29)
        k = cdp.js("[...document.querySelector('.plain').children].map(e=>{const r=e.getBoundingClientRect();return [r.top,r.bottom]})")
        check(s["margins"] == [] and s["ml"] == [] and bands(s["gaps"], [[b[0], k[0][1], b[2], 10], [b[0], k[1][1], b[2], 10]]) and [l["t"] for l in s["gl"]] == ["10"],
              f"{pc} · margins · children without a margin of their own: the gap's bands and its one label, as before", s)

        # ---- the selection's message (the native copy): padding and gaps only, its two kinds of labels
        b = truth("document.querySelector('.summary')"); cdp.js(f"{H}.cmd('resume')"); mouse(b[0] + 5, b[1] + 5); frames(3); wait(450)
        n0 = cdp.js("window.__msgs.length"); mouse(b[0] + 5, b[1] + 5, "mousePressed", "left", 1); mouse(b[0] + 5, b[1] + 5, "mouseReleased", "left", 1); frames(3)
        sp = cdp.js(f"(()=>{{const m=window.__msgs.slice({n0}).filter(m=>m.op==='select').pop();return m&&m.mark?m.mark.sp:null}})()")
        check(bool(sp) and len(sp["gaps"]) == 4 and all(l[0] in (0, 1) for l in sp["labels"]) and sorted(l[5] for l in sp["labels"]) == ["14", "20", "20"],
              f"{pc} · margins · the selection's message keeps its form (padding, 4 gaps, labels of kinds 0 / 1): the native copy is not given the margins", sp)
        cdp.js(f"{H}.cmd('resume')"); mouse(990, 690); frames(2)

    # ---------------- the iframe leaves the page while its element is followed
    if load(1.0):
        t, hov, sel = select(SAME + ".getElementById('o2')", "div.opt")
        cdp.js("window.__msgs.length=0"); cdp.js("document.getElementById('same').remove()"); wait(250)
        g1 = cdp.js("window.__msgs.filter(m=>m.op==='anchor'&&m.gone).length"); wait(900)
        g2 = cdp.js("window.__msgs.filter(m=>m.op==='anchor'&&m.gone).length")
        check(g1 == 0 and g2 == 1 and box('.bf') is None, "iframe removed from the page: its element is « gone » after 500 ms (C3), once; no frame left", [g1, g2, box('.bf')])
finally:
    try:
        if proc: proc.kill(); proc.wait(10)
    except Exception: pass
    srv.shutdown(); osrv.shutdown()
    shutil.rmtree(profile, ignore_errors=True)
    report["failures"] = failures
    json.dump(report, open(os.path.join(OUT, "page-frames.json"), "w", encoding="utf-8"), indent=1, ensure_ascii=False)
print(f"\npage-frames: {len(report['checks'])} checks, {len(failures)} failures")
sys.exit(1 if failures else 0)
