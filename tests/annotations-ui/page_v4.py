"""Comment mode v4, H2 — the page script in a real engine, headless (no window on screen, no network, no agent).

    python tests/annotations-ui/page_v4.py [--out build/chat-cv4-h2]

Builds tests/annotations-ui/page_v4_test.cpp (pure host checks; it also writes the page script and the R36 swap script),
then drives the page script in the WebView2 runtime's Chromium (EdgeCore, --headless=new, throw-away profile) on a
fixture page served from 127.0.0.1:
  R27  the hover frame, the conversation frame and both together: the same device pixels at 100 / 125 / 150 %
  R33  a narrow element: the size label under the name, inside the frame, at its inner top-left corner
  R26  « peek »: soft scroll to an element out of view, shown as hovered, back without scrolling; over the bubble's frame
  R31  Ctrl held: the page's cursor, the site gets its click, nothing selected, Ctrl+C not consumed; released: selection
  R37  the bubble element's inspector, frame by frame during a fast scroll: no lag (no transition), same frame;
       a value change still transitions
  R36  level 2: the style sheet swapped in place, values glide (intermediate frames), state done, fallback « nomatch »
Report: <out>/page-v4.json; captures <out>/r27-*.png. Exit code 0 when every check passes.
"""
import base64, glob, http.server, io, json, os, re, shutil, socket, struct, subprocess, sys, tempfile, threading, time, urllib.request

sys.stdout.reconfigure(encoding="utf-8")
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
args = sys.argv[1:]
OUT = os.path.abspath(args[args.index("--out") + 1] if "--out" in args else os.path.join(ROOT, "build", "chat-cv4-h2"))
TOOLS = os.path.join(ROOT, "tools", "w64devkit", "bin")
EDGE = next(iter(sorted(glob.glob(os.path.join(os.environ.get("LOCALAPPDATA", ""), "Microsoft", "EdgeCore", "*", "msedge.exe"))
                        + glob.glob(os.path.join(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"), "Microsoft", "EdgeCore", "*", "msedge.exe")), reverse=True)), None)
os.environ["BROWSER_ANNOTATIONS_TEST"] = "1"
failures, report = [], {"checks": []}
def check(ok, name, detail=""):
    report["checks"].append({"ok": bool(ok), "name": name, "detail": str(detail)})
    print(("ok   " if ok else "FAIL ") + name + (" — " + str(detail) if detail != "" else ""), flush=True)
    if not ok: failures.append(name)

# ------------------------------------------------------------------ 1. the unit part (and the scripts)
os.makedirs(OUT, exist_ok=True)
exe = os.path.join(OUT, "page-v4-test.exe")
cmd = [os.path.join(TOOLS, "g++.exe"), "-std=c++17", "-O1", "-Wall", "-Wextra", "-Werror", "-Wno-unknown-pragmas", "-Wno-cast-function-type",
       "-Wno-missing-field-initializers", "-Wno-misleading-indentation", "-static", "-municode", "-mconsole", "-Isrc",
       "-Ideps/webview2/build/native/include", "tests/annotations-ui/page_v4_test.cpp", "src/annotations.cpp", "-o", exe,
       "-lbcrypt", "-lole32", "-lwindowscodecs", "-lshlwapi", "-lgdi32", "-luser32"]
r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
check(r.returncode == 0, "unit: build page_v4_test.cpp", (r.stderr or "")[-800:])
if r.returncode: sys.exit(1)
r = subprocess.run([exe, OUT], cwd=ROOT, capture_output=True, text=True)
check(r.returncode == 0, "unit: " + (r.stdout.strip() or "no output"), r.stderr[-800:])
PAGE_JS = open(os.path.join(OUT, "page-script.js"), encoding="utf-8").read()
SWAP_JS = open(os.path.join(OUT, "swap.js"), encoding="utf-8").read()
SWAP_MISSING_JS = open(os.path.join(OUT, "swap-missing.js"), encoding="utf-8").read()

# ------------------------------------------------------------------ 2. the fixture site
SITE = os.path.join(OUT, "site"); shutil.rmtree(SITE, ignore_errors=True); os.makedirs(os.path.join(SITE, "styles"))
CSS0 = ".box{width:100px;height:40px;background:#222;margin:20px;padding:10px}\n"
CSS1 = ".box{width:300px;height:40px;background:#222;margin:20px;padding:30px}\n"
open(os.path.join(SITE, "styles", "site.css"), "w").write(CSS0)
open(os.path.join(SITE, "index.html"), "w", encoding="utf-8").write("""<!doctype html><html><head><meta charset="utf-8">
<link rel="stylesheet" href="/styles/site.css">
<style>body{margin:0;font:14px sans-serif;background:#f4f4f3}
.card{position:absolute;left:101.3px;top:150.6px;width:333.7px;height:121.2px;background:#ddd}
.narrow{position:absolute;left:600px;top:150px;width:40px;height:90px;background:#bbb}
.pad{position:absolute;left:80px;top:420px;width:300px;padding:20px;display:flex;gap:12px;background:#eee}
.pad>div{width:60px;height:40px;background:#999}
.far{position:absolute;left:200px;top:2600px;width:240px;height:80px;background:#ccc}
.btn{position:absolute;left:700px;top:420px;width:120px;height:40px}
.spacer{height:4000px}</style></head><body>
<div class="card"></div><div class="narrow"></div><div class="pad"><div></div><div></div><div></div></div>
<div class="far"></div><button class="btn" onclick="window.__clicked=(window.__clicked||0)+1">site</button>
<div class="box"></div><div class="spacer"></div></body></html>""")

class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *a, **k): super().__init__(*a, directory=SITE, **k)
    def log_message(self, *a): pass
    def end_headers(self): self.send_header("Cache-Control", "no-store"); super().end_headers()
srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
threading.Thread(target=srv.serve_forever, daemon=True).start()
BASE = f"http://127.0.0.1:{srv.server_address[1]}"

# ------------------------------------------------------------------ 3. DevTools (as tests/chat-panel/run.py)
class Cdp:
    def __init__(self, url):
        host, port, path = re.match(r"ws://([^:/]+):(\d+)(/.*)", url).groups()
        self.s = socket.create_connection((host, int(port)), timeout=60)
        key = base64.b64encode(os.urandom(16)).decode()
        self.s.sendall(f"GET {path} HTTP/1.1\r\nHost: {host}:{port}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n".encode())
        buf = b""
        while b"\r\n\r\n" not in buf: buf += self.s.recv(4096)
        self.rest = buf.split(b"\r\n\r\n", 1)[1]; self.n = 0; self.events = []
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
            self.events.append(r)
    def js(self, expr):
        r = self.call("Runtime.evaluate", expression=expr, awaitPromise=True, returnByValue=True)
        if "exceptionDetails" in r: raise RuntimeError("JS: " + json.dumps(r["exceptionDetails"])[:600])
        return r["result"].get("value")

PRE = r"""(function(){var o=Element.prototype.attachShadow;Element.prototype.attachShadow=function(i){var r=o.call(this,i);(window.__roots=window.__roots||[]).push(r);return r};
window.__msgs=[];window.chrome=window.chrome||{};window.chrome.webview={postMessage:function(m){window.__msgs.push(JSON.parse(JSON.stringify(m)))}};
window.__samePath=function(p){var n=function(t){var m=/M\s*([-\d.]+)[\s,]+([-\d.]+)/.exec(t);return m?[+m[1],+m[2]]:null};var a=n(getComputedStyle(p).clipPath),b=n(p.style.clipPath);
  return !!a&&!!b&&Math.abs(a[0]-b[0])<.01&&Math.abs(a[1]-b[1])<.01&&!p.getAnimations().length};
window.__keys=[];addEventListener('keydown',function(e){window.__keys.push([e.key,e.ctrlKey,e.defaultPrevented])},false);})();"""
H = "window[Symbol.for('browser.annotation')]"
R = "window.__roots[window.__roots.length-1]"
def inject(cdp, reduced=False):
    params = {"nonce": "n4", "navStamp": 1, "styles": [], "attrs": ["id", "class"], "accent": "62,103,255", "reduced": reduced, "cursor": {"svg": "<svg xmlns='http://www.w3.org/2000/svg' width='32' height='32'><circle cx='16' cy='16' r='4' fill='red'/></svg>", "size": 32, "scale": 1, "hx": 16, "hy": 16}}
    return cdp.js("(" + PAGE_JS + ")(" + json.dumps(params) + ")")
def frames(cdp, n=2):
    cdp.js("new Promise(r=>{let k=%d;const f=()=>{if(--k<=0)r(1);else requestAnimationFrame(f)};requestAnimationFrame(f)})" % (n + 1))
def wait(cdp, ms): cdp.js(f"new Promise(r=>setTimeout(r,{ms}))")
def mouse(cdp, x, y, kind="mouseMoved", mods=0, button="none", clicks=0):
    cdp.call("Input.dispatchMouseEvent", type=kind, x=x, y=y, modifiers=mods, button=button, clickCount=clicks)

profile = tempfile.mkdtemp(prefix="page-v4-")
proc = None
try:
    check(bool(EDGE), "runtime: headless Chromium (EdgeCore)", EDGE or "none")
    if not EDGE: raise SystemExit(1)
    proc = subprocess.Popen([EDGE, "--headless=new", "--disable-gpu", "--no-first-run", "--no-default-browser-check", "--disable-extensions", "--disable-sync",
                             "--disable-background-networking", "--disable-component-update", "--remote-debugging-port=0", "--user-data-dir=" + profile,
                             "--window-size=1000,700", "about:blank"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    pf = os.path.join(profile, "DevToolsActivePort")
    for _ in range(300):
        if os.path.exists(pf) and open(pf).read().strip(): break
        time.sleep(.1)
    port = open(pf).read().split()[0]
    cdp = Cdp([t for t in json.load(urllib.request.urlopen(f"http://127.0.0.1:{port}/json/list")) if t["type"] == "page"][0]["webSocketDebuggerUrl"])
    for d in ("Page", "Runtime"): cdp.call(d + ".enable")
    cdp.call("Emulation.setFocusEmulationEnabled", enabled=True)
    cdp.call("Page.addScriptToEvaluateOnNewDocument", source=PRE)

    def load(dpr=1.0, reduced=False):
        cdp.call("Emulation.setDeviceMetricsOverride", width=1000, height=700, deviceScaleFactor=dpr, mobile=False)
        cdp.call("Emulation.setEmulatedMedia", features=[{"name": "prefers-reduced-motion", "value": "reduce" if reduced else "no-preference"}])
        cdp.call("Page.navigate", url=BASE + "/index.html")
        cdp.js("new Promise(r=>{const f=()=>document.readyState==='complete'?r(1):setTimeout(f,20);f()})")
        cdp.js("scrollTo(0,0)")
        check(inject(cdp, reduced) == "ok", f"page script injected (dpr {dpr})")
        frames(cdp)

    def shot(name, clip):
        data = cdp.call("Page.captureScreenshot", format="png", clip=dict(clip, scale=1), captureBeyondViewport=False)["data"]
        raw = base64.b64decode(data); open(os.path.join(OUT, name), "wb").write(raw)
        from PIL import Image
        return Image.open(io.BytesIO(raw)).convert("RGB")
    def edge_mask(img):   # solid accent pixels (the frame's border); fill / page never come close
        w, h = img.size; px = img.load()
        return {(x, y) for y in range(h) for x in range(w) if abs(px[x, y][0] - 62) < 30 and abs(px[x, y][1] - 103) < 30 and px[x, y][2] > 215}

    # ---------------- R27: same pixels at 100 / 125 / 150 %
    for dpr in (1.0, 1.25, 1.5):
        load(dpr)
        clip = {"x": 80, "y": 136, "width": 380, "height": 152}   # the card's frame, without the labels above it (y < 146.6)
        cdp.js(f"{H}.cmd('cmarks',[{{id:'c1',selector:'.card',state:'idle'}}])"); frames(cdp, 3)
        a = shot(f"r27-conversation-{int(dpr*100)}.png", clip)
        cdp.js(f"{H}.cmd('cmarks',[])"); mouse(cdp, 260, 210); wait(cdp, 400)
        b = shot(f"r27-hover-{int(dpr*100)}.png", clip)
        cdp.js(f"{H}.cmd('cmarks',[{{id:'c1',selector:'.card',state:'idle'}}])"); frames(cdp, 3)
        c = shot(f"r27-both-{int(dpr*100)}.png", clip)
        ma, mb, mc = edge_mask(a), edge_mask(b), edge_mask(c)
        tag = {(x, y) for (x, y) in ma if x < (101.3 - 80 + 26) * dpr and y < (150.6 - 136 + 26) * dpr}   # the conversation icon (top left)
        top = round((round((150.6 - 4) * dpr) / dpr - 136) * dpr)   # the frame's first device row: the labels above it are not the frame
        ma = {p for p in ma if p[1] >= top}; mb = {p for p in mb if p[1] >= top}; mc = {p for p in mc if p[1] >= top}
        border_a, border_b = ma - tag, mb - tag
        check(len(border_a) > 0 and border_a == border_b, f"R27 {int(dpr*100)} %: hover frame = conversation frame, pixel for pixel",
              f"{len(border_a)} px vs {len(border_b)} px, {len(border_a ^ border_b)} differ")
        check(mc == ma, f"R27 {int(dpr*100)} %: both together = one contour (no double edge)", f"{len(mc ^ ma)} px differ {sorted(mc ^ ma)[:6]}")
        e = cdp.js(f"(()=>{{const f={R}.querySelector('.f'),c={R}.querySelector('.cf');return [getComputedStyle(f).borderTopWidth,getComputedStyle(c).borderTopWidth]}})()")
        check(e[0] == e[1], f"R27 {int(dpr*100)} %: same border width", e)
        mouse(cdp, 990, 690); cdp.js(f"{H}.cmd('cmarks',[])")

    # ---------------- R33: narrow element
    load(1.25)
    mouse(cdp, 620, 200); wait(cdp, 350)
    g = cdp.js(f"""(()=>{{const r={R},f=r.querySelector('.f').getBoundingClientRect(),l=r.querySelectorAll('.l'),a=l[0].getBoundingClientRect(),b=l[1].getBoundingClientRect(),
        e=parseFloat(getComputedStyle(r.querySelector('.f')).borderTopWidth);return {{f:[f.left,f.top,f.width],a:[a.left,a.top,a.height],b:[b.left,b.top,b.height,b.width],e:e,vis:getComputedStyle(l[1]).visibility,t:l[1].textContent}}}})()""")
    check(g["vis"] == "visible" and g["t"] == "40x90", "R33: the size label stays (« 40x90 »)", g)
    check(abs(g["b"][0] - (g["f"][0] + g["e"])) < .01 and abs(g["b"][1] - (g["f"][1] + g["e"])) < .01, "R33: under the name, inside the frame, at its inner top-left corner", g)
    check(g["a"][1] + g["a"][2] <= g["f"][1] + .01, "R33: the name stays above the frame", g)
    mouse(cdp, 620, 200, "mousePressed", button="left", clicks=1); mouse(cdp, 620, 200, "mouseReleased", button="left", clicks=1); frames(cdp)
    m = cdp.js("window.__msgs.filter(m=>m.op==='select').pop()")
    check(m and m["mark"]["rw"] > 0 and abs(m["mark"]["ry"] - (m["mark"]["y"] + g["e"])) < .01, "R33: the native copy gets the size label where the page draws it", m and {k: m["mark"][k] for k in ("x", "y", "rx", "ry", "rw")})
    cdp.js(f"{H}.cmd('resume')")

    # ---------------- R26: peek
    load(1.0)
    mouse(cdp, 990, 690); frames(cdp)
    y0 = cdp.js("scrollY")
    check(cdp.js(f"{H}.cmd('peek',{{active:true,selector:'.far'}})") is True, "R26: peek finds .far")
    samples = cdp.js("new Promise(r=>{const out=[];const f=()=>{out.push(scrollY);if(out.length<90)requestAnimationFrame(f);else r(out)};requestAnimationFrame(f)})")
    distinct = len(set(int(v) for v in samples))
    check(samples[-1] > 2000 and distinct > 5, "R26: soft scroll to the element out of view", f"{y0} -> {samples[-1]}, {distinct} distinct positions")
    st = cdp.js(f"""(()=>{{const r={R},f=r.querySelector('.f'),fb=f.getBoundingClientRect(),e=document.querySelector('.far').getBoundingClientRect();
        return {{op:getComputedStyle(f).opacity,dx:fb.left-(e.left-4),dy:fb.top-(e.top-4),name:r.querySelector('.l').textContent,inView:e.top>=0&&e.bottom<=innerHeight}}}})()""")
    check(st["op"] == "1" and abs(st["dx"]) < .6 and abs(st["dy"]) < .6 and st["name"] == "div.far" and st["inView"], "R26: shown as hovered (frame, name)", st)
    wait(cdp, 300)
    ins = cdp.js(f"getComputedStyle({R}.querySelector('.i')).opacity")
    check(ins == "0", "R26: no inspector for an element without padding / gap", ins)
    yb = cdp.js("scrollY"); cdp.js(f"{H}.cmd('peek',{{active:false}})"); wait(cdp, 300)
    st2 = cdp.js(f"[getComputedStyle({R}.querySelector('.f')).opacity, scrollY]")
    check(st2[0] == "0" and abs(st2[1] - yb) < 1, "R26: back to what was there, without scrolling back", st2)
    cdp.js("scrollTo(0,0)"); frames(cdp)
    cdp.js(f"{H}.cmd('peek',{{active:true,selector:'.card'}})"); frames(cdp, 5)
    check(cdp.js("scrollY") == 0, "R26: no scroll when the element is in view")
    cdp.js(f"{H}.cmd('peek',{{active:true,selector:'.pad'}})"); wait(cdp, 400)
    pad = cdp.js(f"[getComputedStyle({R}.querySelector('.i')).opacity,{R}.querySelector('.l').textContent]")
    check(pad[0] == "1" and pad[1].startswith("div.pad"), "R26: a new peek replaces the previous one, with its inspector", pad)
    cdp.js(f"{H}.cmd('peek',{{active:false}})")
    # over the bubble's frame (Mode Commentaire): the bubble's element keeps its frame, comes back with its inspector
    cdp.js(f"{H}.cmd('follow',{{selector:'.pad',draw:{{label:'div.pad'}}}})"); wait(cdp, 300)
    cdp.js(f"{H}.cmd('peek',{{active:true,selector:'.card'}})"); wait(cdp, 300)
    o = cdp.js(f"(()=>{{const r={R};return [getComputedStyle(r.querySelector('.f')).opacity,!!r.querySelector('.bf'),r.querySelector('.l').textContent]}})()")
    check(o[0] == "1" and o[1] and o[2] == "div.card", "R26: over the open bubble: the chip's element shown, the bubble's frame kept", o)
    cdp.js(f"{H}.cmd('peek',{{active:false}})"); wait(cdp, 400)
    o = cdp.js(f"(()=>{{const r={R};return [getComputedStyle(r.querySelector('.f')).opacity,getComputedStyle(r.querySelector('.i')).opacity]}})()")
    check(o == ["0", "1"], "R26: back to the bubble's element and its inspector", o)
    cdp.js(f"{H}.cmd('follow',null)")

    # ---------------- R31: Ctrl held
    load(1.0)
    mouse(cdp, 760, 440); wait(cdp, 250)
    cur0 = cdp.js("getComputedStyle(document.querySelector('.btn')).cursor")
    cdp.call("Input.dispatchKeyEvent", type="rawKeyDown", key="Control", code="ControlLeft", windowsVirtualKeyCode=17, modifiers=2)
    frames(cdp)
    st = cdp.js(f"[getComputedStyle(document.querySelector('.btn')).cursor,getComputedStyle({R}.querySelector('.f')).opacity]")
    check(cur0.startswith("image-set") or "url(" in cur0, "R31: the « viseur » cursor before Ctrl", cur0[:40])
    check(st[0] == "default", "R31: Ctrl held: the page's own cursor", st[0][:40])
    wait(cdp, 200)
    check(cdp.js(f"getComputedStyle({R}.querySelector('.f')).opacity") == "0", "R31: Ctrl held: no hover frame")
    n0 = cdp.js("window.__msgs.filter(m=>m.op==='select').length")
    mouse(cdp, 760, 440, "mouseMoved", mods=2)
    mouse(cdp, 760, 440, "mousePressed", mods=2, button="left", clicks=1); mouse(cdp, 760, 440, "mouseReleased", mods=2, button="left", clicks=1); frames(cdp)
    st = cdp.js("[window.__clicked||0, window.__msgs.filter(m=>m.op==='select').length]")
    check(st[0] == 1 and st[1] == n0, "R31: Ctrl+click goes to the site, nothing selected", st)
    cdp.call("Input.dispatchKeyEvent", type="rawKeyDown", key="c", code="KeyC", windowsVirtualKeyCode=67, modifiers=2)
    cdp.call("Input.dispatchKeyEvent", type="keyUp", key="c", code="KeyC", windowsVirtualKeyCode=67, modifiers=2)
    k = cdp.js("window.__keys.filter(k=>k[0]==='c').pop()")
    check(k == ["c", True, False], "R31: Ctrl+C not consumed", k)
    cdp.call("Input.dispatchKeyEvent", type="keyUp", key="Control", code="ControlLeft", windowsVirtualKeyCode=17, modifiers=0)
    frames(cdp)
    cur1 = cdp.js("getComputedStyle(document.querySelector('.btn')).cursor")
    check(cur1 == cur0, "R31: Ctrl released: the « viseur » again at once")
    mouse(cdp, 761, 441); wait(cdp, 250)
    check(cdp.js(f"getComputedStyle({R}.querySelector('.f')).opacity") == "1", "R31: Ctrl released: the hover frame again")
    mouse(cdp, 761, 441, "mousePressed", button="left", clicks=1); mouse(cdp, 761, 441, "mouseReleased", button="left", clicks=1); frames(cdp)
    st = cdp.js("[window.__clicked||0, window.__msgs.filter(m=>m.op==='select').length]")
    check(st[0] == 1 and st[1] == n0 + 1, "R31: a click without Ctrl selects, the site does not get it", st)
    cdp.js(f"{H}.cmd('resume')")
    # Ctrl seen on the pointer alone (focus in the panel): a move with Ctrl down is enough
    mouse(cdp, 300, 200, "mouseMoved", mods=2); frames(cdp)
    check(cdp.js("getComputedStyle(document.querySelector('.card')).cursor") == "auto", "R31: Ctrl seen on a pointer move (focus elsewhere)")
    mouse(cdp, 301, 200, "mouseMoved", mods=0); frames(cdp)
    check(cdp.js("getComputedStyle(document.querySelector('.card')).cursor") != "auto", "R31: released, seen on the next move")

    # ---------------- R37: glued while scrolling, transition only for values
    load(1.25)
    cdp.js(f"{H}.cmd('follow',{{selector:'.pad',draw:{{label:'div.pad'}}}})"); wait(cdp, 400)
    MEASURE = f"""(()=>{{const r={R},p=r.querySelector('.pb'),e=document.querySelector('.pad').getBoundingClientRect(),bf=r.querySelector('.bf').getBoundingClientRect(),
        cs=getComputedStyle(p).clipPath,m=/M\\s*([-\\d.]+)[\\s,]+([-\\d.]+)/.exec(cs);
        return {{dx:m?+m[1]-e.left:null,dy:m?+m[2]-e.top:null,same:window.__samePath(p),fy:bf.top-(e.top-4),sm:p.classList.contains('sm')}}}})()"""
    rows = cdp.js("new Promise(r=>{const out=[];let k=0;const f=()=>{out.push(" + MEASURE + ");if(++k<40){scrollBy(0,23);requestAnimationFrame(()=>requestAnimationFrame(f))}else r(out)};requestAnimationFrame(f)})")
    bad = [x for x in rows[1:] if x["dy"] is None or abs(x["dy"]) > .6 or abs(x["fy"]) > .6 or not x["same"]]
    check(not bad, "R37: inspector and frame glued to the element, every frame of a fast scroll (no transition on a move)", f"{len(rows)} frames, {len(bad)} off" + (f": {bad[:2]}" if bad else ""))
    cdp.js("document.querySelector('.pad').style.padding='36px'"); frames(cdp, 3)
    v = cdp.js(MEASURE)
    check(v["sm"] and not v["same"], "R37: a value change (padding) still moves softly", v)
    wait(cdp, 400); v = cdp.js(MEASURE)
    check(v["same"] and abs(v["dy"]) < .6, "R37: … and lands on the new value", v)
    cdp.js("window.dispatchEvent(new Event('resize'))"); frames(cdp, 2)
    cdp.js(f"{H}.cmd('follow',null)")

    # ---------------- R36: level 2, the style sheet swapped in place
    load(1.0)
    cdp.js(f"{H}.cmd('follow',{{selector:'.box',draw:{{label:'div.box'}}}})"); wait(cdp, 400)
    w0 = cdp.js("document.querySelector('.box').getBoundingClientRect().width")
    open(os.path.join(SITE, "styles", "site.css"), "w").write(CSS1)
    check(cdp.js(SWAP_MISSING_JS) == "nomatch", "R36: a changed file that is not a linked sheet -> « nomatch » (fallback: fade + reload)")
    check(cdp.js(SWAP_JS) == "started", "R36: the swap starts")
    rows = cdp.js(f"""new Promise(r=>{{const out=[];const t0=performance.now();const f=()=>{{const b=document.querySelector('.box').getBoundingClientRect(),p={R}.querySelector('.pb');
        out.push([performance.now()-t0,b.width,window.__samePath(p),p.classList.contains('sm')]);
        if(performance.now()-t0<700)requestAnimationFrame(f);else r(out)}};requestAnimationFrame(f)}})""")
    widths = [x[1] for x in rows]
    mid = [w for w in widths if w0 + 5 < w < 300 + 60 - 5]
    check(len(mid) >= 3, "R36: the values glide (intermediate frames)", f"{w0:.0f} -> {widths[-1]:.0f}, {len(mid)} intermediate frames of {len(rows)}")
    check(abs(widths[-1] - 360) < .5, "R36: the new value at the end", widths[-1])
    check(all(x[2] and not x[3] for x in rows), "R36: the inspector follows frame by frame (no transition of its own)", [x for x in rows if not x[2] or x[3]][:3])
    wait(cdp, 200)
    st = cdp.js("[window.__browserRestyle&&window.__browserRestyle.state,document.querySelectorAll('link[rel=stylesheet]').length,document.querySelectorAll('style[data-browser-restyle]').length]")
    check(st == ["done", 1, 0], "R36: state done, the old sheet and the temporary rule removed", st)
    open(os.path.join(SITE, "styles", "site.css"), "w").write(CSS0)
finally:
    try:
        if proc: proc.kill(); proc.wait(10)
    except Exception: pass
    srv.shutdown()
    shutil.rmtree(profile, ignore_errors=True)
    report["failures"] = failures
    json.dump(report, open(os.path.join(OUT, "page-v4.json"), "w", encoding="utf-8"), indent=1, ensure_ascii=False)
print(f"\npage-v4: {len(report['checks'])} checks, {len(failures)} failures")
sys.exit(1 if failures else 0)
