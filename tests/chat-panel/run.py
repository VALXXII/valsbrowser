"""Chat panel QA: screenshots of every board state, comparison with the Paper references,
accessibility, keyboard, security (CSP, no network) and performance checks.

The panel (assets/chat-panel) runs in headless Chromium from the WebView2 runtime (EdgeCore),
driven over the DevTools protocol with a throw-away profile. Pages are served by a loopback-only
HTTP server started here (fonts need http, not file://). The fake host (dev-host.js) replays one
scenario per board. Nothing touches the network, a real agent, or any user configuration.

Usage: python -I tests/chat-panel/run.py [--edge <msedge.exe>] [--out build/chat-qa/panel] [--only name,...]
Exit code 0 when every check passes. Screenshots: <out>/<scenario>.png, comparisons: <out>/compare/.
"""
import base64, glob, http.server, json, os, re, shutil, socket, struct, subprocess, sys, tempfile, threading, time, urllib.parse, urllib.request

sys.stdout.reconfigure(encoding="utf-8")
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
ASSETS = os.path.join(ROOT, "assets")
PANEL = os.path.join(ASSETS, "chat-panel")
REF = os.path.join(os.path.dirname(os.path.abspath(__file__)), "reference")
args = sys.argv[1:]
def opt(name, default=None):
    return args[args.index(name) + 1] if name in args else default
OUT = os.path.abspath(opt("--out", os.path.join(ROOT, "build", "chat-qa", "panel")))
COMMENT = os.path.join(os.path.dirname(OUT), "comment")   # comment-mode bubble (B 1.8): build/chat-qa/comment
REF_C = os.path.join(REF, "comment")                      # the bubbles of the Paper boards 18NS-0 … 19IO-0, exported at 1x
ONLY = set(filter(None, (opt("--only", "") or "").split(",")))
EDGE = opt("--edge") or next(iter(sorted(glob.glob(os.path.join(os.environ.get("LOCALAPPDATA", ""), "Microsoft", "EdgeCore", "*", "msedge.exe")), reverse=True)), None)
os.environ["BROWSER_ANNOTATIONS_TEST"] = "1"

failures = []
report = {"checks": [], "scenarios": {}, "perf": {}}
def check(ok, name, detail=""):
    report["checks"].append({"ok": bool(ok), "name": name, "detail": detail})
    print(("ok   " if ok else "FAIL ") + name + (" — " + detail if detail else ""))
    if not ok: failures.append(name)

# ------------------------------------------------------------------ 1. static checks (production files)
PROD = ["index.html", "panel.css", "panel.js", "markdown.js", "icons.js"]
src = {f: open(os.path.join(PANEL, f), encoding="utf-8").read() for f in PROD}
js = "\n".join(src[f] for f in PROD if f.endswith(".js"))
for pat, why in [(r"\beval\s*\(", "eval"), (r"new\s+Function\b", "new Function"), (r"\bfetch\s*\(", "fetch"), (r"XMLHttpRequest", "XMLHttpRequest"),
                 (r"WebSocket", "WebSocket"), (r"EventSource", "EventSource"), (r"sendBeacon", "sendBeacon"), (r"localStorage|sessionStorage|indexedDB", "storage"),
                 (r"\bimport\s*\(", "dynamic import"), (r"document\.write", "document.write"), (r"setTimeout\s*\(\s*['\"]", "string timer"), (r"\.outerHTML\s*=|insertAdjacentHTML", "HTML injection")]:
    check(not re.search(pat, js), "static: no " + why)
inner = [l.strip() for l in js.splitlines() if "innerHTML" in l]
check(len(inner) == 1 and "def[1]" in inner[0], "static: innerHTML only for constant icon strings", "; ".join(inner)[:200])
check("Content-Security-Policy" in src["index.html"] and "default-src 'none'" in src["index.html"] and "connect-src 'none'" in src["index.html"], "static: strict CSP in index.html")
check(not re.search(r"(src|href)=\"https?:", src["index.html"]) and not re.search(r"url\(\s*['\"]?https?:", src["panel.css"]), "static: no remote script, style or font")
check("dev-host" not in src["index.html"], "static: index.html does not load the fake host")

# ------------------------------------------------------------------ 2. loopback server
CSP = "default-src 'none'; script-src 'self'; style-src 'self'; font-src 'self'; img-src data:; connect-src 'none'; base-uri 'none'; form-action 'none'"
os.makedirs(os.path.join(OUT, "compare"), exist_ok=True)
os.makedirs(os.path.join(COMMENT, "compare"), exist_ok=True)
requests_seen = []
class Handler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *a): pass
    def translate_path(self, path):
        p = urllib.parse.unquote(urllib.parse.urlsplit(path).path)
        for prefix, base in (("/out/", OUT), ("/ref/", REF), ("/cmt/", COMMENT)):
            if p.startswith(prefix): return os.path.join(base, p[len(prefix):])
        if p == "/compare.html": return os.path.join(os.path.dirname(os.path.abspath(__file__)), "compare.html")
        return os.path.join(ASSETS, p.lstrip("/"))
    def end_headers(self):
        if self.path.split("?")[0].endswith(".html") and not self.path.startswith("/compare"): self.send_header("Content-Security-Policy", CSP)
        self.send_header("Cache-Control", "no-store")
        super().end_headers()
    def do_GET(self):
        requests_seen.append(self.path)
        super().do_GET()
srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
threading.Thread(target=srv.serve_forever, daemon=True).start()
BASE = f"http://127.0.0.1:{srv.server_address[1]}"

# ------------------------------------------------------------------ 3. DevTools client
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
        while not (b1 & 0x80):  # continuation frames
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
                if "error" in r: raise RuntimeError(f"{method}: {r['error']}")
                return r.get("result", {})
            if "method" in r: self.events.append(r)
    def js(self, expr, await_promise=True):
        r = self.call("Runtime.evaluate", expression=expr, awaitPromise=await_promise, returnByValue=True)
        if "exceptionDetails" in r: raise RuntimeError("JS: " + json.dumps(r["exceptionDetails"])[:500])
        return r["result"].get("value")

def size_of(png):
    d = open(png, "rb").read(24); return struct.unpack(">II", d[16:24])

# scenario -> (viewport w, h, hover selector, compare mode)
FULL = {"6.1-autorisation", "6.2-premiere-fois", "6.3-termine", "6.8-viser", "6.9-ecrire", "6.10-travaille", "6.11-fait", "6.12-plan", "6.6-fil", "6.7a-vide", "6.7b-historique"}
HOVER = {"6.6-survol": ".turn", "6.6-fil": ".um[data-id='u2'] .bubble", "6.11-fait": ".um[data-id='u1'] .bubble",
         "c-2.1-survol": ".bt-st", "c-3-historique": ".bt-st"}

profile = tempfile.mkdtemp(prefix="chat-panel-qa-")
proc = None
try:
    check(bool(EDGE and os.path.exists(EDGE)), "runtime: headless Chromium (EdgeCore) found", EDGE or "none")
    if not EDGE: raise SystemExit(1)
    proc = subprocess.Popen([EDGE, "--headless=new", "--disable-gpu", "--no-first-run", "--no-default-browser-check", "--disable-extensions", "--disable-sync",
                             "--disable-background-networking", "--disable-component-update", "--remote-debugging-port=0", "--user-data-dir=" + profile,
                             "--force-device-scale-factor=1", "--window-size=380,698", "about:blank"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    port_file = os.path.join(profile, "DevToolsActivePort")
    for _ in range(300):
        if os.path.exists(port_file) and open(port_file).read().strip(): break
        time.sleep(.1)
    port = open(port_file).read().split()[0]
    targets = json.load(urllib.request.urlopen(f"http://127.0.0.1:{port}/json/list"))
    cdp = Cdp([t for t in targets if t["type"] == "page"][0]["webSocketDebuggerUrl"])
    for d in ("Page", "Runtime", "Network", "Log"): cdp.call(d + ".enable")
    cdp.call("Emulation.setFocusEmulationEnabled", enabled=True)  # :focus styles as in a focused window
    cdp.call("Page.addScriptToEvaluateOnNewDocument", source="window.__csp=[];document.addEventListener('securitypolicyviolation',e=>window.__csp.push(e.violatedDirective+' '+e.blockedURI));"
             "window.V=s=>{const n=document.querySelector(s);return n?n.getClientRects().length:0};window.__long=[];try{new PerformanceObserver(l=>{for(const e of l.getEntries())window.__long.push(e.duration)}).observe({type:'longtask',buffered:true})}catch(e){}")

    def viewport(w, h, reduced=False):
        cdp.call("Emulation.setDeviceMetricsOverride", width=w, height=h, deviceScaleFactor=1, mobile=False)
        cdp.call("Emulation.setEmulatedMedia", features=[{"name": "prefers-reduced-motion", "value": "reduce" if reduced else "no-preference"}])

    def load(name, w=380, h=698, reduced=False):
        viewport(w, h, reduced)
        cdp.events.clear()
        cdp.call("Page.navigate", url=f"{BASE}/chat-panel/dev.html?scenario={urllib.parse.quote(name)}")
        ok = cdp.js("new Promise(r=>{const t0=performance.now();const f=()=>{if(window.__scenarioDone)r(true);else if(performance.now()-t0>15000)r(false);else setTimeout(f,20)};f()})")
        cdp.js("document.fonts.ready.then(()=>new Promise(r=>setTimeout(r,350)))")  # entrances (200 ms) finish
        return ok

    def errors_of(name):
        errs = [e for e in cdp.events if e["method"] == "Runtime.exceptionThrown" or (e["method"] == "Runtime.consoleAPICalled" and e["params"]["type"] in ("error", "assert"))
                or (e["method"] == "Log.entryAdded" and e["params"]["entry"]["level"] == "error")]
        logs = cdp.js("(window.__posted||[]).filter(m=>m.type==='panel.log').map(m=>m.level+': '+m.message)")
        csp = cdp.js("window.__csp")
        return [json.dumps(e["params"])[:300] for e in errs] + logs + ["CSP " + c for c in csp]

    def shot(path, clip=None):
        p = {"format": "png"}
        if clip: p["clip"] = dict(clip, scale=1)
        data = cdp.call("Page.captureScreenshot", **p)["data"]
        open(path, "wb").write(base64.b64decode(data))

    def compare(name, ours, ref):
        r = cdp.js(f"window.compareImages({json.dumps('/out/' + os.path.relpath(ours, OUT).replace(os.sep, '/'))},{json.dumps('/ref/' + os.path.basename(ref))})")
        open(os.path.join(OUT, "compare", name + ".png"), "wb").write(base64.b64decode(r["png"].split(",", 1)[1]))
        return r
    def compare_bubble(name):
        r = cdp.js(f"window.compareImages({json.dumps('/cmt/' + name + '.png')},{json.dumps('/ref/comment/' + name + '.png')})")
        open(os.path.join(COMMENT, "compare", name + ".png"), "wb").write(base64.b64decode(r["png"].split(",", 1)[1]))
        return r
    def bubble_size():
        return cdp.js("(()=>{const s=window.__posted.filter(m=>m.type==='bubble.size').pop();return s?[s.width,s.height]:null})()")

    # ---------------------------------------------------------------- 4. every scenario: screenshot + a11y + errors
    names = cdp.js("new Promise(r=>{})", await_promise=False) if False else None
    cdp.call("Page.navigate", url=f"{BASE}/chat-panel/dev.html?scenario=6.7a-vide")
    time.sleep(.5)
    names = cdp.js("new Promise(r=>{const f=()=>window.__scenarios?r(window.__scenarios):setTimeout(f,20);f()})")
    wanted = lambda n: not ONLY or n in ONLY or any(o.endswith("*") and n.startswith(o[:-1]) for o in ONLY)   # --only c-* : a prefix
    scen = [n for n in names if wanted(n) and n not in ("stream", "perf-500")]
    bubble_sizes = {}
    for name in scen:
        h = 777 if name == "6.6-fil" else 640 if name in ("6.7a-vide", "6.7b-historique") else 698
        bubble = name.startswith("c-") and name != "c-bascule-panneau"
        ok = load(name, 380, h)
        if name in HOVER:
            box = cdp.js(f"(()=>{{const n=document.querySelector({json.dumps(HOVER[name])});if(!n)return null;const r=n.getBoundingClientRect();return [r.left+r.width/2,r.top+r.height/2]}})()")
            if box: cdp.call("Input.dispatchMouseEvent", type="mouseMoved", x=box[0], y=box[1]); time.sleep(.65 if bubble else .25)
        if bubble:
            # the bubble at the size it asked for (the fake host resizes its « window » from bubble.size), transparent around it
            cdp.js("new Promise(r=>{document.activeElement&&document.activeElement.blur();requestAnimationFrame(()=>requestAnimationFrame(r))})")   # no caret, no focus ring: the boards show neither
            size = bubble_size()
            bubble_sizes[name] = size
            out = os.path.join(COMMENT, name + ".png")
            cdp.call("Emulation.setDefaultBackgroundColorOverride", color={"r": 0, "g": 0, "b": 0, "a": 0})
            shot(out, {"x": 0, "y": 0, "width": size[0], "height": size[1]} if size else None)
            if name == "c-2.1-survol":   # the hover list alone, next to its board (Paper 1B7H-0)
                tb = cdp.js("(()=>{const r=document.getElementById('b-tip').getBoundingClientRect();return {x:r.left,y:r.top,width:r.width,height:r.height}})()")
                if tb["width"]: shot(os.path.join(COMMENT, "c-2.1-infobulle-etapes.png"), tb)
            cdp.call("Emulation.setDefaultBackgroundColorOverride")
            cdp.call("Input.dispatchMouseEvent", type="mouseMoved", x=370, y=690)
        else:
            out = os.path.join(OUT, name + ".png")
            shot(out)
        errs = errors_of(name)
        a11y = cdp.js(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "a11y.js"), encoding="utf-8").read())
        report["scenarios"][name] = {"screenshot": os.path.relpath(out, ROOT), "errors": errs, "a11y": a11y}
        check(ok, f"{name}: scenario replayed")
        check(not errs, f"{name}: no error, CSP violation or panel.log", "; ".join(errs)[:300])
        check(not a11y["problems"], f"{name}: accessibility audit ({a11y['controls']} controls)", "; ".join(a11y["problems"])[:400])

    # ---------------------------------------------------------------- 5. comparison with the boards
    cdp.call("Emulation.setDeviceMetricsOverride", width=1200, height=900, deviceScaleFactor=1, mobile=False)
    cdp.call("Page.navigate", url=f"{BASE}/compare.html"); time.sleep(.5)
    for ref in sorted(glob.glob(os.path.join(REF, "*.png"))):
        name = os.path.splitext(os.path.basename(ref))[0]
        ours = os.path.join(OUT, name + ".png")
        if not os.path.exists(ours): continue
        r = compare(name, ours, ref)
        report["scenarios"].setdefault(name, {})["compare"] = {k: r[k] for k in ("mean", "over", "w", "h", "mode")}
        print(f"     compare {name}: {r['mode']} {r['w']}x{r['h']}" + (f", mean diff {r['mean']:.1f}/255, pixels off >48: {r['over']:.1f} %" if r['mean'] is not None else ""))

    # comment-mode bubble: each state next to the bubble of its Paper board (same size expected, then a pixel diff)
    for ref in sorted(glob.glob(os.path.join(REF_C, "*.png"))):
        name = os.path.splitext(os.path.basename(ref))[0]
        if not os.path.exists(os.path.join(COMMENT, name + ".png")): continue
        r = compare_bubble(name)
        rw, rh = size_of(ref)
        report["scenarios"].setdefault(name, {})["compare"] = dict({k: r[k] for k in ("mean", "over", "w", "h", "mode")}, board=[rw, rh])
        print(f"     compare {name}: {r['mode']} {r['w']}x{r['h']} (board {rw}x{rh})" + (f", mean diff {r['mean']:.1f}/255, pixels off >48: {r['over']:.1f} %" if r['mean'] is not None else ""))
        if name != "c-2.1-infobulle-etapes":
            check((r["w"], r["h"]) == (rw, rh), f"{name}: the bubble has the size of its Paper board ({rw}x{rh})", f"{r['w']}x{r['h']}")

    shots = sorted(os.path.splitext(os.path.basename(f))[0] for f in glob.glob(os.path.join(OUT, "*.png")) if not os.path.basename(f).startswith(("planches", "stream")))
    for i in range(0, len(shots), 12):
        png = cdp.js(f"window.montage({json.dumps(shots[i:i + 12])}, 6)")
        open(os.path.join(OUT, f"planches-{i // 12 + 1}.png"), "wb").write(base64.b64decode(png.split(",", 1)[1]))

    # ---------------------------------------------------------------- 6. keyboard and behaviour
    def posted(): return cdp.js("window.__posted.map(m=>m.type+(m.optionId?':'+m.optionId:'')+(m.id?':'+m.id:''))")
    def key(k, code=None, mods=0, text=None):
        kc = {"Escape": 27, "Enter": 13, "Tab": 9, "ArrowDown": 40, "ArrowUp": 38, "/": 191}.get(k, 0)
        base = {"key": k, "code": code or k, "windowsVirtualKeyCode": kc, "modifiers": mods}
        cdp.call("Input.dispatchKeyEvent", type="rawKeyDown", **base)
        if text is None and k == "Enter": text = "\r"
        if text: cdp.call("Input.dispatchKeyEvent", type="char", text=text, **base)
        cdp.call("Input.dispatchKeyEvent", type="keyUp", **base)
        time.sleep(.08)
    active = lambda: cdp.js("(()=>{const a=document.activeElement;return a?(a.getAttribute('aria-label')||a.textContent||a.className).trim().slice(0,40):''})()")

    # ---------------------------------------------------------------- 6b. comment-mode bubble (B 1.8, design/chat-panel.md §19)
    if not ONLY or any(o.startswith("c-") for o in ONLY):
        sizes = lambda: cdp.js("window.__posted.filter(m=>m.type==='bubble.size').map(m=>[m.width,m.height])")
        of = lambda t: cdp.js(f"window.__posted.filter(m=>m.type==={json.dumps(t)})")
        host = lambda m: (cdp.js(f"window.devHost.send({json.dumps(m)})"), time.sleep(.12))
        def center(sel): return cdp.js(f"(()=>{{const n=document.querySelector({json.dumps(sel)});if(!n)return null;const r=n.getBoundingClientRect();return [r.left+r.width/2,r.top+r.height/2]}})()")
        def move(x, y): cdp.call("Input.dispatchMouseEvent", type="mouseMoved", x=x, y=y)
        def click(sel): cdp.js(f"document.querySelector({json.dumps(sel)}).click()"); time.sleep(.15)
        vis = lambda sel: cdp.js(f"(()=>{{const n=document.querySelector({json.dumps(sel)});return !!n&&n.getClientRects().length>0}})()")
        inside = lambda sel: cdp.js(f"(()=>{{const a=document.getElementById('app').getBoundingClientRect(),r=document.querySelector({json.dumps(sel)}).getBoundingClientRect();return r.left>=a.left-.5&&r.top>=a.top-.5&&r.right<=a.right+.5&&r.bottom<=a.bottom+.5}})()")

        boards = {"c-1-saisie": 104, "c-2-envoye": 224, "c-2.1-arrete": 265, "c-2-autorisation": 303, "c-2-autorisation-diff": 303, "c-2.2b-erreur": 248, "c-2.2b-erreur-details": 279, "c-3-termine": 279, "c-3-termine-diff": 303}
        seen = {n: bubble_sizes[n] for n in boards if n in bubble_sizes}
        check(bool(seen) and all(seen[n] == [305, boards[n]] for n in seen), "bubble: `bubble.size` gives the height of each Paper board (104, 224, 265, 303, 303, 248, 279, 279, 303)", json.dumps(seen))

        # the layout: no header, no conversation list, a non-modal dialog, the input focused, French mode names
        load("c-1-vide")
        b = cdp.js("""(()=>{const a=document.getElementById('app'),cs=getComputedStyle(a);return {bub:document.documentElement.classList.contains('bub'),hd:document.querySelector('.hd').getClientRects().length,
          hist:document.getElementById('history').getClientRects().length,pick:document.querySelector('.tb.pick').getClientRects().length,role:a.getAttribute('role'),modal:a.getAttribute('aria-modal'),name:a.getAttribute('aria-label'),
          radius:cs.borderTopLeftRadius,bg:cs.backgroundColor,page:getComputedStyle(document.body).backgroundColor,w:a.getBoundingClientRect().width,mode:document.querySelector('.tb.mode').textContent,
          focus:document.activeElement.id,first:window.__posted.findIndex(m=>m.type==='bubble.size'),all:document.body.innerText}})()""")
        check(b["bub"] and b["hd"] == 0 and b["hist"] == 0 and b["pick"] == 0, "bubble: no header, no conversation list, no aim button", json.dumps(b)[:200])
        check(b["role"] == "dialog" and b["modal"] == "false" and b["name"] == "Commentaire sur button.btn-primary", "bubble: a non-modal dialog named after its element", f"{b['role']} {b['modal']} {b['name']}")
        check(b["radius"] == "16px" and b["bg"] == "rgb(23, 23, 23)" and b["page"] == "rgba(0, 0, 0, 0)" and round(b["w"]) == 305, "bubble: 305 px surface #171717, radius 16, transparent page around it", f"{b['radius']} {b['bg']} {b['page']} {b['w']}")
        check(b["mode"] == "Demander" and "Ask" not in b["all"], "bubble: the mode shows its French name (« Demander », never « Ask »)", b["mode"])
        check(b["focus"] == "composer-input", "bubble: the cursor is in the input at the opening", b["focus"])
        check(sizes() == [[305, 84]], "bubble: one `bubble.size` per change (305 x 84 for the empty input), never the same value twice", str(sizes()))
        host({"type": "layout", "mode": "bubble", "maxWidth": 305, "maxHeight": 520, "open": False})
        check(sizes() == [[305, 84], [305, 84]], "bubble: every `layout` is answered by `bubble.size`, even unchanged (acknowledgement)", str(sizes()))
        host({"type": "layout", "mode": "bubble", "maxWidth": 280, "maxHeight": 60, "open": False})
        check(sizes()[-1] == [280, 84], "bubble: the width follows maxWidth (280)", str(sizes()[-1]))
        host({"type": "layout", "mode": "bubble", "maxWidth": 305, "maxHeight": 520, "open": False})
        cdp.js("document.getElementById('composer-input').focus()"); cdp.call("Input.insertText", text="Plus grand"); key("Enter", mods=2); time.sleep(.15)
        ann = of("annotate")
        cdp.call("Input.insertText", text="Plus grand"); key("Enter"); time.sleep(.15)
        snd = [{"text": m["text"], "elements": m["elements"]} for m in of("send")]
        check(snd == [{"text": "Plus grand", "elements": []}], "bubble: Entrée sends the words; the element is implicit (elements: [])", json.dumps(snd, ensure_ascii=False))
        check(len(ann) == 1 and ann[0]["text"] == "Plus grand" and ann[0]["elements"] == [], "bubble: Ctrl+Entrée keeps an annotation with words only (the host adds the anchored element)", json.dumps(ann, ensure_ascii=False)[:160])

        # during a turn: the mode and « Arrêter », no input; Échap closes the bubble and never stops the agent
        load("c-2-envoye")
        r = cdp.js("""(()=>{const s=document.querySelector('.send');return {cin:V('.cin'),plus:document.querySelector('.tools .tb[aria-haspopup=menu]:not(.mode)').getClientRects().length,
          model:document.querySelector('.model').getClientRects().length,stop:s.classList.contains('stop'),label:s.getAttribute('aria-label'),bg:getComputedStyle(s).backgroundColor,focusStop:document.activeElement===s,
          st:document.querySelector('.bt-st .lb').textContent,chip:document.querySelector('.bt-model').textContent,rule:V('.b-rule'),caret:document.querySelectorAll('.text.streaming').length}})()""")
        check(r["cin"] == 0 and r["plus"] == 0 and r["model"] == 0 and r["stop"] and r["label"].startswith("Arrêter") and r["rule"] == 0, "bubble (turn): no input, only the mode and « Arrêter »", json.dumps(r, ensure_ascii=False)[:240])
        check(r["st"] == "Lit checkout.css…" and r["chip"] == "Sonnet 5.5Élevé" and not r["focusStop"] and r["caret"] == 0, "bubble (turn): « modèle · effort », the step in progress at its right; the focus is never on « Arrêter »", f"{r['st']} / {r['chip']}")
        host({"type": "toolCallUpdate", "id": "r4", "status": "completed", "title": "A lu checkout.css"})
        host({"type": "toolCall", "turnId": "t1", "id": "r5", "title": "Modifie checkout.css…", "kind": "edit", "status": "in_progress"})
        check(cdp.js("document.querySelector('.bt-st .lb').textContent") == "Modifie checkout.css…", "bubble (turn): the status follows the step in progress")
        key("Escape")
        t = [m["type"] for m in cdp.js("window.__posted")]
        check("close" in t and "stop" not in t and cdp.js("document.getElementById('app').classList.contains('b-out')"), "bubble: Échap sends `close` (the host closes the bubble), never `stop`; the surface goes", str(t[-4:]))
        host({"type": "layout", "mode": "bubble", "maxWidth": 305, "maxHeight": 520})
        check(not cdp.js("document.getElementById('app').classList.contains('b-out')") and cdp.js("getComputedStyle(document.getElementById('app')).animationName") == "b-in", "bubble: the next `layout` shows it again, opening from the element's side (180 ms)")
        click(".send.stop")
        check("stop" in [m["type"] for m in cdp.js("window.__posted")], "bubble (turn): « Arrêter » sends `stop`")
        time.sleep(.3)
        s2 = cdp.js("[document.querySelector('.bt-st .lb').textContent, document.getElementById('composer-input').dataset.placeholder, V('.cin'), V('.b-rule')]")
        check(s2 == ["S'est arrêté", "Que voulez-vous ajouter ?", 1, 1], "bubble (stopped): « S'est arrêté », the input is back (« Que voulez-vous ajouter ? ») under its hairline", str(s2))

        # hover list of the steps: inside the bubble, by pointer and by keyboard
        load("c-2.1-survol")
        c = center(".bt-st"); move(*c); time.sleep(.6)
        tip = cdp.js("""(()=>{const t=document.getElementById('b-tip');return {on:!t.hidden&&getComputedStyle(t).opacity,rows:Array.from(t.querySelectorAll('.r')).map(r=>r.textContent),role:t.getAttribute('role'),
          desc:document.querySelector('.bt-st').getAttribute('aria-describedby'),hover:window.__posted.filter(m=>m.type==='hover').map(m=>m.active)}})()""")
        check(tip["on"] == "1" and tip["rows"] == ["A lu index.html", "A lu Checkout.tsx", "A utilisé 1 outil MCP"] and tip["role"] == "tooltip" and tip["desc"] == "b-tip" and tip["hover"][-1:] == [True],
              "bubble (hover): the step in progress shows the steps done (« A lu index.html », « A lu Checkout.tsx », « A utilisé 1 outil MCP »)", json.dumps(tip, ensure_ascii=False)[:260])
        check(inside("#b-tip"), "bubble (hover): the list opens inside the bubble (a window cannot draw outside)")
        move(c[0] - 150, c[1] + 100); time.sleep(.3)
        check(cdp.js("document.getElementById('b-tip').hidden") and cdp.js("window.__posted.filter(m=>m.type==='hover').pop().active") is False, "bubble (hover): it goes when the pointer leaves")
        move(*c); time.sleep(.6); host({"type": "pointer.leave"})
        check(cdp.js("document.getElementById('b-tip').hidden"), "bubble (hover): the host's `pointer.leave` closes it too")
        move(370, 690); cdp.js("document.activeElement&&document.activeElement.blur()"); key("Tab"); time.sleep(.2)
        kb = cdp.js("[document.activeElement.className, !document.getElementById('b-tip').hidden, document.activeElement.getAttribute('aria-describedby')]")
        check("bt-st" in kb[0] and kb[1] and kb[2] == "b-tip", "bubble (hover): keyboard focus on the step opens the same list", str(kb))

        # stopped state: the lit send button over an empty input puts the cursor in the input
        load("c-2.1-arrete")
        cdp.js("document.activeElement&&document.activeElement.blur()")
        s = cdp.js("(()=>{const b=document.querySelector('.send');return [b.classList.contains('ready'), b.getAttribute('aria-disabled'), document.querySelector('.bt-st').tagName]})()")
        click(".send")
        check(s == [True, None, "SPAN"] and cdp.js("document.activeElement.id") == "composer-input" and not of("send"), "bubble (stopped): send stays lit (boards), pressing it empty focuses the input; « S'est arrêté » is plain text", str(s))

        # permission in the bubble
        load("c-2-autorisation")
        p = cdp.js("""(()=>{const a=document.getElementById('app'),s=document.querySelector('.scroller');return {role:document.querySelector('.bperm').getAttribute('role'),title:document.getElementById('perm-t').textContent,
          nums:Array.from(document.querySelectorAll('.bperm-file .num')).map(n=>n.textContent),ph:document.querySelector('.instead').placeholder,auto:document.querySelector('.bperm .always').textContent,
          checked:document.querySelector('.bperm .always').getAttribute('aria-checked'),faded:a.classList.contains('faded'),fade:getComputedStyle(document.querySelector('.b-fade')).opacity,top:s.scrollTop,
          composer:V('.composer'),rule:V('.b-rule')}})()""")
        check(p["role"] == "alertdialog" and p["title"] == "Claude veut modifier checkout.css" and p["nums"] == ["+6", "−2"] and p["ph"] == "Dire à Claude quoi faire à la place…" and p["auto"] == "Auto." and p["checked"] == "true"
              and p["composer"] == 0 and p["rule"] == 1, "bubble (permission): the request is in the bubble, in place of the input", json.dumps(p, ensure_ascii=False)[:300])
        check(p["faded"] and p["fade"] == "1" and p["top"] > 0, "bubble (permission): at its maximum height the thread scrolls and its top fades", f"faded {p['faded']}, opacity {p['fade']}, scrollTop {p['top']}")
        check(active().startswith("Autoriser"), "bubble (permission): focus on « Autoriser »", active())
        key("Enter")
        check("permission.respond:always" in posted(), "bubble (permission): Entrée = Autoriser (Auto on: the allow_always option)", str(posted()[-3:]))
        load("c-2-autorisation"); key("Escape")
        t = posted()
        check("close" in t and not any(x.startswith("permission.respond") for x in t), "bubble (permission): Échap closes the bubble and answers nothing (the request waits)", str(t[-3:]))
        load("c-2-autorisation"); click(".bperm .btn:not(.primary)")
        check("permission.respond:reject" in posted() and cdp.js("!document.querySelector('.bperm')"), "bubble (permission): « Refuser » rejects")
        load("c-2-autorisation"); cdp.js("document.querySelector('.instead').focus()"); cdp.call("Input.insertText", text="Garde 240 px"); key("Enter")
        resp = of("permission.respond")
        check(len(resp) == 1 and resp[0]["optionId"] == "reject" and resp[0].get("message") == "Garde 240 px", "bubble (permission): « Dire à Claude quoi faire à la place » rejects with the words", json.dumps(resp, ensure_ascii=False))
        load("c-autorisation-grande")
        g = cdp.js("[document.getElementById('app').classList.contains('faded'), document.querySelector('.scroller').scrollTop]")
        check(sizes()[-1] == [305, 313] and g == [False, 0], "bubble (permission): with more room (maxHeight 520) the bubble grows to its content (313) and nothing fades", f"{sizes()[-1]} {g}")

        # the diff « en grand »
        load("c-2-autorisation-diff")
        v = cdp.js("""(()=>{const v=document.getElementById('bview');return {open:!v.hidden,role:v.getAttribute('role'),name:v.getAttribute('aria-label'),lines:Array.from(v.querySelectorAll('.l')).map(l=>l.className.replace('l ','')),
          add:getComputedStyle(v.querySelector('.l.add')).color,file:document.querySelector('.bperm-file').getClientRects().length,thread:document.querySelector('.body').getClientRects().length,
          mono:getComputedStyle(v.querySelector('.bview-body')).fontFamily,focus:document.activeElement.getAttribute('aria-label'),buttons:document.querySelectorAll('.bperm .btn').length}})()""")
        check(v["open"] and v["role"] == "dialog" and v["name"] == "Claude veut modifier checkout.css" and v["lines"] == ["del", "add", "add"] and v["add"] == "rgb(122, 255, 171)" and "JetBrains Mono" in v["mono"]
              and v["file"] == 0 and v["thread"] == 0 and v["buttons"] == 2, "bubble (diff): a click on the file line opens the diff in the bubble, above Refuser / Autoriser", json.dumps(v, ensure_ascii=False)[:300])
        check(sizes()[-1] == [305, 303] and v["focus"] == "Fermer l'aperçu", "bubble (diff): it takes all the height there is (maxHeight), focus on its close button", f"{sizes()[-1]} {v['focus']}")
        key("Escape")
        check(cdp.js("document.getElementById('bview').hidden") and "close" not in posted() and "bperm-file" in cdp.js("document.activeElement.className"), "bubble (diff): Échap closes the diff first, focus back on the file line", active())
        click(".bperm-file"); click(".bperm .btn.primary")
        check("permission.respond:always" in posted() and cdp.js("document.getElementById('bview').hidden && !document.querySelector('.bperm')"), "bubble (diff): « Autoriser » answers from the open diff and closes it")
        load("c-autorisation-commande")
        cmd = cdp.js("[document.querySelector('#bview .bview-pre').textContent, document.querySelectorAll('#bview .num').length]")
        check(cmd == ["npm test -- --run checkout", 0] and sizes()[-1] == [305, 224], "bubble (permission): a command opens the same way, at its own height (never smaller than the turn's bubble)", f"{cmd} {sizes()[-1]}")

        # error
        load("c-2.2b-erreur")
        e = cdp.js("""(()=>({color:getComputedStyle(document.querySelector('.bt-text .text')).color,line:document.querySelector('.bt-err .w').textContent,more:document.querySelector('.bt-more').getAttribute('aria-expanded'),
          buttons:Array.from(document.querySelectorAll('.b-errft .btn')).map(b=>b.textContent),composer:V('.composer'),st:document.querySelector('.bt-stw').textContent}))()""")
        check(e["color"] == "rgb(255, 123, 110)" and e["line"] == "Une erreur est apparue." and e["more"] == "false" and e["buttons"] == ["Annuler", "Réessayer"] and e["composer"] == 0 and e["st"] == "",
              "bubble (error): the agent's text in red, « Une erreur est apparue. », Annuler / Réessayer in place of the input", json.dumps(e, ensure_ascii=False))
        click(".bt-more")
        d = cdp.js("(()=>({open:document.querySelector('.bt-more').getAttribute('aria-expanded'),text:document.querySelector('.bt-errd').innerText,code:getComputedStyle(document.querySelector('.bt-errd .ecode')).fontFamily,body:V('.bt-text')}))()")
        check(d["open"] == "true" and d["text"].startswith("Erreur 529 · overloaded_error") and "Les serveurs de Claude étaient surchargés. La réponse" in d["text"] and "« Réessayer » relance la même demande." in d["text"] and "JetBrains Mono" in d["code"]
              and sizes()[-1] == [305, 279], "bubble (error): « Plus d'infos » unfolds the code, what happened, what changed and what to do (248 → 279)", d["text"].replace("\n", " | ")[:200] + f" {sizes()[-1]}")
        click(".bt-more")
        check(sizes()[-1] == [305, 248] and cdp.js("V('.bt-text')") == 1, "bubble (error): folded again, the bubble is back to 248")
        click(".b-errft .btn.danger")
        rt = of("retry")
        check(rt == [{"v": 1, "type": "retry", "turnId": "t1"}] and vis(".composer"), "bubble (error): « Réessayer » sends `retry` for that turn", json.dumps(rt))
        load("c-2.2b-erreur"); click(".b-errft .btn:not(.danger)")
        check(vis(".composer") and not vis(".b-errft") and cdp.js("document.activeElement.id") == "composer-input" and not of("retry"), "bubble (error): « Annuler » brings the input back, nothing is sent")

        # result
        load("c-3-termine")
        d = cdp.js("""(()=>({bar:document.querySelector('.bt-bar-main').textContent,copy:document.querySelector('.bt-copy').textContent,st:document.querySelector('.bt-st .lb').textContent,tag:document.querySelector('.bt-st').tagName,
          ph:document.getElementById('composer-input').dataset.placeholder,text:document.querySelector('.bt-text').textContent,actions:document.querySelectorAll('.actions, .rewind, .um-tools .icon-btn').length}))()""")
        check(d["bar"] == "1 fichier modifié+6−2" and d["copy"] == "Copier" and d["st"] == "Historique" and d["tag"] == "BUTTON" and d["ph"] == "Que voulez-vous ajouter de plus ?" and d["text"].startswith("C'est fait.") and d["actions"] == 0,
              "bubble (done): the words, « 1 fichier modifié +6 −2 › » with Copier, « Historique », « Que voulez-vous ajouter de plus ? »", json.dumps(d, ensure_ascii=False)[:260])
        click(".bt-copy"); time.sleep(.2)
        cp = of("copy")
        check(len(cp) == 1 and "-width: 240px;" in cp[0]["text"] and "+margin-left: auto;" in cp[0]["text"] and cdp.js("document.querySelector('.bt-copy span').textContent") == "Copié", "bubble (done): Copier copies the diff, then says « Copié »", cp[0]["text"][:80].replace("\n", " | ") if cp else "")
        c = center(".bt-st"); move(*c); time.sleep(.6)
        hist = cdp.js("Array.from(document.querySelectorAll('#b-tip .r')).map(r=>r.textContent)")
        check(hist == ["A lu index.html", "A lu Checkout.tsx", "A utilisé 1 outil MCP", "A lu checkout.css", "A modifié checkout.css", "A lancé npm run build"] and inside("#b-tip"),
              "bubble (done): hovering « Historique » lists the turn's actions (reads, MCP tools, edits, commands), inside the bubble", " | ".join(hist))
        move(370, 690); time.sleep(.2)
        click(".bt-bar-main"); time.sleep(.2)
        v = cdp.js("""(()=>{const v=document.getElementById('bview');return {open:!v.hidden,title:v.querySelector('.t').textContent,nums:Array.from(v.querySelectorAll('.num')).map(n=>n.textContent),lines:v.querySelectorAll('.l').length,
          copy:!!v.querySelector('.bview-copy'),composer:V('.composer'),asked:window.__posted.filter(m=>m.type==='checkpoint.viewChanges').map(m=>m.turnId)}})()""")
        check(v["open"] and v["title"] == "checkout.css" and v["nums"] == ["+6", "−2"] and v["lines"] == 3 and v["copy"] and v["composer"] == 1 and v["asked"] == ["t1"] and sizes()[-1] == [305, 520],
              "bubble (done): the bar opens the file's diff in the bubble (title, +6 −2, Copier), asks the host for the full diff, takes maxHeight", json.dumps(v, ensure_ascii=False) + f" {sizes()[-1]}")
        click(".bview-copy"); click(".bview-x")
        check(len(of("copy")) == 2 and cdp.js("document.getElementById('bview').hidden") and "bt-bar-main" in cdp.js("document.activeElement.className") and sizes()[-1] == [305, 279], "bubble (done): Copier in the diff; closing returns to the bar and to 279")
        load("c-diff-plusieurs-fichiers")
        mf = cdp.js("[document.querySelector('#bview .t').textContent, Array.from(document.querySelectorAll('#bview .l.file')).map(l=>l.textContent), document.querySelectorAll('#bview .l.add').length>5]")
        check(mf == ["3 fichiers modifiés", ["src/styles/checkout.css", "src/pages/Checkout.tsx", "src/styles/pay-button.css"], True], "bubble (done): several files are stacked in the same view, each under its path", str(mf)[:200])

        # queue between elements, menus, long thread, no folder
        load("c-file-attente")
        q = cdp.js("[document.querySelector('.bq .bubble').textContent, document.querySelector('.bq-cap .tx').textContent, V('.composer')]")
        click(".bq-cap .icon-btn")
        check(q == ["Mets ce titre en gras", "Commencera après le travail en cours sur button.btn-primary", 1] and "queue.remove:q1" in posted(), "bubble (queue): a request written during another element's turn waits, with its discreet line and « × »", str(q))
        load("c-menu-modele")
        m = cdp.js("[!!document.querySelector('.menu[role=menu]'), document.querySelectorAll('.menu .mi').length]")
        grown = sizes()
        geo = cdp.js("(()=>{const m=document.querySelector('.menu'),r=m.getBoundingClientRect(),a=document.getElementById('app').getBoundingClientRect();return {top:Math.round(r.top),h:Math.round(r.height),scroll:m.scrollHeight,app:Math.round(a.height)}})()")
        check(m[0] and m[1] >= 7 and inside(".menu") and grown[0] == [305, 84] and len(grown) == 2 and abs(grown[-1][1] - (geo["top"] + geo["h"] + 6)) <= 1 and geo["scroll"] <= geo["h"] and geo["app"] == grown[-1][1],
              "bubble (menu): the model menu opens inside the bubble, which grows for it (nothing cut, nothing scrolled)", f"{m} {grown} {geo}")
        key("Escape")
        check(cdp.js("!document.querySelector('.menu')") and sizes()[-1] == [305, 84] and "close" not in posted(), "bubble (menu): Échap closes the menu only; the bubble is back to its size", str(sizes()[-2:]))
        load("c-menu-mode")
        check(inside(".menu") and sizes()[-1] == [305, 279] and cdp.js("Array.from(document.querySelectorAll('.menu .mi .lb')).map(n=>n.textContent)") == ["Demander", "Auto", "Planifier"], "bubble (menu): with room above its button the menu covers the thread, the size does not change", str(sizes()[-1]))
        load("c-menu-au-dessus")
        up = cdp.js("(()=>{const a=document.getElementById('app').getBoundingClientRect(),c=document.querySelector('.composer').getBoundingClientRect(),b=document.querySelector('.tb.mode').getBoundingClientRect(),m=document.querySelector('.menu').getBoundingClientRect();return [Math.round(a.bottom-c.bottom), m.bottom<=b.top, m.top>=a.top]})()")
        check(up == [12, True, True] and inside(".menu") and sizes()[-1][1] > 200, "bubble (above its element): content sits on the bottom edge, the menu opens upward", f"{up} {sizes()[-1]}")
        load("c-fil-long")
        lg = cdp.js("(()=>{const s=document.querySelector('.scroller');return [s.scrollHeight>s.clientHeight, Math.round(s.scrollTop+s.clientHeight)===s.scrollHeight, document.getElementById('app').classList.contains('faded')]})()")
        check(sizes()[-1] == [305, 360] and lg == [True, True, True], "bubble (long thread): at maxHeight (360) the thread scrolls, glued to its end, the top fades", f"{sizes()[-1]} {lg}")
        load("c-sans-dossier")
        nf = cdp.js("[(document.querySelector('.strip')||{}).textContent||'', document.querySelector('.send').getAttribute('aria-disabled'), document.querySelector('.first').getClientRects().length]")
        click(".strip .btn-s")
        check("Choisissez le dossier du projet" in nf[0] and nf[1] == "true" and nf[2] == 0 and "folder.choose" in posted(), "bubble (no folder): one line with « Choisir le dossier… », no folder screen; send is off", str(nf))

        # a live turn: the height is published as the content grows
        cdp.events.clear(); viewport(380, 698)
        cdp.call("Page.navigate", url=f"{BASE}/chat-panel/dev.html?layout=bubble")
        cdp.js("new Promise(r=>{const f=()=>window.__scenarioDone?r(1):setTimeout(f,20);f()})"); time.sleep(.3)
        early = cdp.js("document.documentElement.classList.contains('bub')")
        cdp.js("document.getElementById('composer-input').focus()"); cdp.call("Input.insertText", text="Bonjour"); key("Enter"); time.sleep(.5)
        mid = cdp.js("[document.querySelector('.send').classList.contains('stop'), V('.cin')]")
        time.sleep(2.2)
        lv = cdp.js("""(()=>{const a=document.getElementById('app'),s=document.querySelector('.scroller'),z=window.__posted.filter(m=>m.type==='bubble.size').map(m=>m.height);
          return {z,app:Math.round(a.getBoundingClientRect().height),fit:s.scrollHeight<=s.clientHeight,done:!!document.querySelector('.bt-st.done'),bar:!!document.querySelector('.bt-bar'),focus:document.activeElement.id}})()""")
        check(early and mid == [True, 0] and len(set(lv["z"])) >= 4 and lv["z"][-1] == lv["app"] and lv["fit"] and lv["done"] and lv["bar"] and all(a != b for a, b in zip(lv["z"][1:], lv["z"][2:])),
              "bubble (live): `?layout=bubble` from the first paint; the height is published at each change and the window fits the content", json.dumps(lv))
        check(lv["focus"] == "composer-input" and not errors_of("live"), "bubble (live): when the turn ends the cursor is back in the input; no error", lv["focus"])

        # motion, switch back to the panel
        load("c-1-vide", reduced=True)
        cdp.js("window.devHost.send({type:'layout',mode:'bubble',maxHeight:520})")
        check(cdp.js("getComputedStyle(document.getElementById('app')).animationName") == "fade", "bubble (motion): reduced motion opens with a fade only")
        key("Escape"); time.sleep(.2)
        check(cdp.js("getComputedStyle(document.getElementById('app')).transform") == "none" and cdp.js("getComputedStyle(document.getElementById('app')).opacity") == "0", "bubble (motion): reduced motion closes without scale")
        load("c-bascule-panneau")
        n0 = len(sizes())
        host({"type": "state", "running": False})
        pn = cdp.js("""(()=>({bub:document.documentElement.classList.contains('bub'),hd:document.querySelector('.hd').getClientRects().length,role:document.getElementById('app').getAttribute('role'),
          meta:(document.querySelector('.turn .meta')||{}).textContent,bt:document.querySelectorAll('.turn.bt').length,pick:document.querySelector('.tb.pick').getClientRects().length,bar:!!document.querySelector('.card4')}))()""")
        check(not pn["bub"] and pn["hd"] == 1 and pn["role"] is None and pn["meta"] == "Sonnet · Élevé · 18 s" and pn["bt"] == 0 and pn["pick"] == 1 and pn["bar"] and len(sizes()) == n0,
              "layout: back to `panel` the chat is the panel again (header, thread drawn again by thread.reset), no more `bubble.size`", json.dumps(pn, ensure_ascii=False))

    # B 1.8 — the folder's name rather than its path or the site's address (Paper 156L-0)
    if not ONLY:
        load("6.7a-vide"); cdp.js("document.querySelector('.tools .tb[aria-haspopup=menu]').click()"); time.sleep(.1)
        plus = cdp.js("document.querySelector('.menu').innerText")
        load("6.2-premiere-fois")
        tile = cdp.js("[document.querySelector('.tile.found .nm').textContent, document.querySelectorAll('.tile.found .path').length, document.querySelector('.tile.found').title]")
        load("6.7b-historique")
        cdp.js("window.devHost.send({type:'history',items:[{id:'c2',title:'Bouton de paiement',subtitle:'1 fichier modifié',siteLabel:'127.0.0.1:5173',thisSite:true,agentIcon:'claude',updatedAt:new Date().toISOString()},"
               "{id:'c4',title:'Menu de navigation',subtitle:'3 fichiers modifiés',siteLabel:'localhost:4173',folderName:'vitrine',thisSite:false,agentIcon:'claude',updatedAt:new Date().toISOString()}]})"); time.sleep(.15)
        hs = cdp.js("[document.querySelector('.hsec').textContent, Array.from(document.querySelectorAll('.hrow .s')).map(s=>s.textContent)]")
        check("boutique" in plus and "C:\\" not in plus and tile[0].startswith("boutique") and tile[1] == 0 and tile[2].endswith("boutique")
              and hs == ["Ce site · boutique", ["1 fichier modifié", "vitrine · 3 fichiers modifiés"]],
              "folder (B 1.8): the folder's name instead of its path or the site's address (« + » menu, folder screen, history)", f"{plus!r} {tile} {hs}"[:300])

    if not ONLY:
        load("6.1-autorisation")
        check(active().startswith("Autoriser"), "keyboard: permission card focuses « Autoriser »", active())
        key("Enter")
        check("permission.respond:always" in posted(), "keyboard: Entrée = Autoriser (toujours, case cochée)", str(posted()[-3:]))
        load("6.1-autorisation"); key("Escape")
        check("permission.respond:reject" in posted(), "keyboard: Échap = Refuser", str(posted()[-3:]))
        check(cdp.js("!!document.querySelector('[role=alertdialog]')") is False and cdp.js("document.activeElement.id") == "composer-input", "keyboard: after answering, the input is back and focused")

        load("6.10-travaille"); cdp.js("document.activeElement&&document.activeElement.blur()"); key("Escape")
        check("stop" in posted(), "keyboard: Échap during a turn sends stop")
        load("6.7a-vide"); cdp.js("document.body.focus()"); key("Escape")
        check("close" in posted(), "keyboard: Échap with nothing open closes the panel")

        load("6.7a-vide"); cdp.js("document.getElementById('composer-input').focus()"); key("/", "Slash", 2)
        check(cdp.js("!!document.querySelector('.menu[role=menu]')") and "Sonnet" in active(), "keyboard: Ctrl+/ opens the model menu on the current model", active())
        key("ArrowDown"); a1 = active(); key("Escape")
        check(a1.startswith("Haiku") and cdp.js("!document.querySelector('.menu')") and active().startswith("Modèle"), "keyboard: arrows move, Échap closes and returns to the trigger", a1 + " / " + active())

        load("6.7a-vide"); cdp.js("document.getElementById('composer-input').focus()"); key("Tab", "Tab", 8)
        check(any(p.startswith("setMode:acceptEdits") for p in posted()), "keyboard: Maj+Tab switches mode")

        load("6.5-4-arobase")
        check(cdp.js("document.getElementById('composer-input').getAttribute('aria-activedescendant')") == "opt-0", "keyboard: @ menu exposes the active row")
        key("Enter")
        check(cdp.js("!!document.querySelector('.cin .chip')") and "mention.query" in "".join(posted()), "keyboard: Entrée in the @ menu inserts the element chip")

        load("6.7a-vide"); cdp.js("document.getElementById('composer-input').focus()")
        for ch in "Bonjour": key(ch, "Key" + ch.upper(), 0, ch)
        key("Enter")
        sent = cdp.js("window.__posted.filter(m=>m.type==='send').map(m=>m.text)")
        check(sent == ["Bonjour"], "keyboard: Entrée sends the message", str(sent))

        load("6.7a-vide")
        order = []
        cdp.js("document.activeElement&&document.activeElement.blur()")
        for _ in range(16): key("Tab"); order.append(active())
        check(order[:4] == ["Nouveau chat — ouvrir l'historique", "Nouveau chat", "Historique", "Fermer le chat"] and "Message à l'agent" in order, "keyboard: Tab order header → content → input → tools", " | ".join(order)[:300])

        load("6.7b-historique"); key("Escape")
        check(cdp.js("document.getElementById('history').hidden") and active().startswith("Nouveau chat") is False and "historique" in active(), "keyboard: Échap closes the history and returns to the title", active())

        load("6.7c-modifier-confirmer")
        check(cdp.js("!!document.querySelector('.editor .warn:not([hidden])')"), "edit: changed files are named before resending (edit.confirm)")

        # B 1.2 — header gauge, one model menu, readable permission titles
        load("6.10-travaille")
        check(cdp.js("!document.querySelector('.hd .spin, .hd svg.spin') && document.querySelector('.gauge').hidden"), "header: no spinner in the header while working (« Travaille » stays in the thread)")
        load("en-tete-jauge")
        g = cdp.js("(()=>{const g=document.querySelector('.gauge');const t=g.querySelector('.tip');return {label:g.getAttribute('aria-label'),high:g.classList.contains('high'),"
                   "dash:g.querySelector('.arc').getAttribute('stroke-dasharray'),tip:getComputedStyle(t).opacity,text:t.textContent,focus:document.activeElement===g,tab:g.tabIndex}})()")
        check(g["label"] == "Mémoire du chat : 124 000 tokens sur 200 000" and not g["high"] and g["tab"] == 0 and g["dash"].startswith("23.37"), "header: context gauge at 62 % is neutral, named, focusable", json.dumps(g, ensure_ascii=False)[:200])
        check(g["focus"] and g["tip"] == "1" and g["text"] == "124k / 200k", "header: the bubble « 124k / 200k » shows on keyboard focus", str(g["tip"]) + " " + g["text"])
        load("jauge-survol")
        def tip(): return cdp.js("[document.querySelector('.gauge').classList.contains('tip-on'), getComputedStyle(document.querySelector('.gauge .tip')).opacity]")
        def hovers(): return cdp.js("window.__posted.filter(m=>m.type==='hover').map(m=>m.active)")
        gx = cdp.js("(()=>{const r=document.querySelector('.gauge').getBoundingClientRect();return [r.left+r.width/2,r.top+r.height/2]})()")
        cdp.call("Input.dispatchMouseEvent", type="mouseMoved", x=gx[0], y=gx[1]); time.sleep(.5)
        on1 = tip(); h1 = hovers()
        cdp.call("Input.dispatchMouseEvent", type="mouseMoved", x=gx[0], y=gx[1] + 140); time.sleep(.25)
        off1 = tip()
        check(on1[0] and on1[1] == "1" and not off1[0] and h1[-1:] == [True] and hovers()[-1:] == [False], "gauge (B 1.5): the bubble shows on hover and goes when the pointer leaves the gauge", f"{on1} -> {off1}, hover {hovers()}")
        cdp.call("Input.dispatchMouseEvent", type="mouseMoved", x=gx[0], y=gx[1]); time.sleep(.5)
        cdp.js("window.devHost.send({type:'pointer.leave'})"); time.sleep(.2)
        off2 = tip()
        check(not off2[0] and off2[1] == "0", "gauge (B 1.5): the host's pointer.leave (cursor out of the panel, no leave event in the page) closes it", str(off2))
        cdp.call("Input.dispatchMouseEvent", type="mouseMoved", x=gx[0] + 1, y=gx[1]); time.sleep(.5)
        cdp.js("window.dispatchEvent(new Event('blur'))"); time.sleep(.2)
        check(not tip()[0], "gauge (B 1.5): the window losing focus closes it too")
        cdp.call("Input.dispatchMouseEvent", type="mouseMoved", x=gx[0], y=gx[1] + 140); time.sleep(.1)
        cdp.call("Input.dispatchMouseEvent", type="mousePressed", x=gx[0], y=gx[1], button="left", clickCount=1); cdp.call("Input.dispatchMouseEvent", type="mouseReleased", x=gx[0], y=gx[1], button="left", clickCount=1)
        cdp.call("Input.dispatchMouseEvent", type="mouseMoved", x=gx[0], y=gx[1] + 140); time.sleep(.25)
        check(not tip()[0] and tip()[1] == "0", "gauge (B 1.5): a mouse click on the gauge does not keep the bubble (focus by the pointer is not keyboard focus)", str(tip()))
        # B 1.6 — Ctrl+Entrée keeps an annotation (no agent), Entrée asks the agent; the send button says both
        load("garder-annotation")
        key("Enter", mods=2)   # Ctrl
        time.sleep(.3)
        kept = cdp.js("(()=>{const k=document.querySelector('.kept');return {sent:window.__posted.filter(m=>m.type==='annotate').map(m=>({text:m.text,els:m.elements})),"
                      "send:window.__posted.filter(m=>m.type==='send').length,line:k?k.innerText:'',chip:k?k.querySelectorAll('.chip').length:0,empty:!document.querySelector('.cin .chip')&&!document.querySelector('.cin').textContent.trim()}})()")
        check(kept["sent"] == [{"text": "h1.title Mets ce titre en gras", "els": ["el-title"]}] and kept["send"] == 0, "keep (B 1.6): Ctrl+Entrée sends `annotate` with the words and the chip, nothing to the agent", json.dumps(kept["sent"], ensure_ascii=False))
        check("Mets ce titre en gras" in kept["line"] and "Gardé en annotation" in kept["line"] and kept["chip"] == 1 and kept["empty"],
              "keep (B 1.6): a discreet « Gardé en annotation » line in the thread (words, element), the composer is empty", kept["line"].replace("\n", " | ")[:160])
        load("6.7a-vide")
        cdp.js("document.getElementById('composer-input').focus()"); cdp.call("Input.insertText", text="Plus grand"); key("Enter", mods=2); time.sleep(.2)
        h1 = cdp.js("[window.__posted.filter(m=>m.type==='annotate').length, (document.querySelector('.strip')||{}).textContent||'']")
        load("garder-sans-texte")
        cdp.js("document.getElementById('composer-input').focus()"); key("Enter", mods=2); time.sleep(.2)
        h2 = cdp.js("[window.__posted.filter(m=>m.type==='annotate').length, (document.querySelector('.strip')||{}).textContent||'']")
        check(h1[0] == 0 and "Cliquez un élément" in h1[1] and h2[0] == 0 and "Écrivez ce qu'il faut changer" in h2[1], "keep (B 1.6): no element, or no words: nothing is kept, a short hint says what is missing", f"{h1} / {h2}")
        load("envoyer-infobulle")
        sx = cdp.js("(()=>{const r=document.querySelector('.send').getBoundingClientRect();return [r.left+r.width/2,r.top+r.height/2]})()")
        cdp.call("Input.dispatchMouseEvent", type="mouseMoved", x=sx[0], y=sx[1]); time.sleep(.5)
        tip = cdp.js("(()=>{const t=document.getElementById('send-tip');const b=document.querySelector('.send');return {on:getComputedStyle(t).opacity,text:t.innerText,keys:b.getAttribute('aria-keyshortcuts'),desc:b.getAttribute('aria-describedby'),title:b.title,"
                     "above:t.getBoundingClientRect().bottom<=b.getBoundingClientRect().top,inside:t.getBoundingClientRect().left>=0}})()")
        cdp.call("Input.dispatchMouseEvent", type="mouseMoved", x=sx[0] - 200, y=sx[1] - 200); time.sleep(.3)
        off = cdp.js("getComputedStyle(document.getElementById('send-tip')).opacity")
        check(tip["on"] == "1" and "Demander à l'agent" in tip["text"] and "Garder en annotation" in tip["text"] and "Ctrl" in tip["text"] and tip["above"] and tip["inside"] and off == "0"
              and tip["keys"] == "Enter Control+Enter" and tip["desc"] == "send-tip" and not tip["title"],
              "send button (B 1.6): its bubble says « Demander à l'agent ↵ » and « Garder en annotation Ctrl ↵ », above it, inside the panel, gone at the leave", json.dumps(tip, ensure_ascii=False)[:240])
        load("garde-en-annotation")
        check(cdp.js("!!document.querySelector('.kept') && document.querySelector('.kept').innerText.includes('Gardé en annotation')"), "keep (B 1.6): the line comes back when the conversation is reopened (thread.reset)")
        # B 1.7 — the aim button is the selection switch: pressed while on, a click switches it off (the chat stays), again on
        load("selection-active")
        sw = lambda: cdp.js("(()=>{const b=document.querySelector('.tb.pick');const cs=getComputedStyle(b);return {pressed:b.getAttribute('aria-pressed'),bg:cs.backgroundColor,keys:b.getAttribute('aria-keyshortcuts'),label:b.getAttribute('aria-label')}})()")
        on1 = sw(); cdp.js("document.querySelector('.tb.pick').click()"); time.sleep(.3); off1 = sw()
        sent = cdp.js("window.__posted.filter(m=>m.type==='pickElement').map(m=>m.active)")
        cdp.js("document.querySelector('.tb.pick').click()"); time.sleep(.3); on2 = sw()
        sent2 = cdp.js("window.__posted.filter(m=>m.type==='pickElement').map(m=>m.active)")
        check(on1["pressed"] == "true" and off1["pressed"] == "false" and on2["pressed"] == "true" and sent == [False] and sent2 == [False, True] and on1["bg"] != off1["bg"]
              and on1["keys"] == "Control+Shift+A" and "Sélection" in on1["label"] and cdp.js("!!document.querySelector('.cin')") ,
              "selection switch (B 1.7): pressed while on (accent), a click sends pickElement off then on, aria-pressed follows; the chat stays", json.dumps([on1, off1, on2, sent2], ensure_ascii=False)[:300])
        load("en-tete-jauge-pleine")
        check(cdp.js("document.querySelector('.gauge').classList.contains('high') && getComputedStyle(document.querySelector('.gauge .arc')).stroke==='rgb(230, 172, 61)'"), "header: above 80 % the ring turns amber (86 %)")
        load("6.7a-vide")
        check(cdp.js("document.querySelectorAll('.model button').length") == 1, "model menu: the « modèle · effort » chip is a single trigger")
        cdp.js("document.querySelector('.model .m').click()"); time.sleep(.15)
        main_rows = cdp.js("Array.from(document.querySelectorAll('.menu .mi')).map(b=>b.textContent.trim())")
        check(main_rows[:6] == ["Opus 5.5", "Sonnet 5.5Par défaut", "Haiku 5.5", "6.1 Sol", "6.1 Terra", "6.1 Luna"] and main_rows[-1].startswith("Effort") and not any(r.startswith("Autres modèles") for r in main_rows)
              and cdp.js("document.querySelectorAll('.menu .sep').length") == 2, "model menu (V1.2): Claude Code's group, a rule, Codex's group, a rule, « Effort › » (no « Autres modèles » with 3 models each)", " | ".join(main_rows))
        cdp.js("document.querySelector(\".menu .mi[data-key='effort']\").focus()"); key("ArrowRight")
        eff = cdp.js("[document.querySelector('.menu').getAttribute('aria-label'), document.activeElement.textContent.trim(), document.querySelectorAll('.menu .mi.back').length]")
        key("ArrowLeft")
        check(eff[0] == "Effort" and eff[1].startswith("Élevé") and eff[2] == 1 and cdp.js("document.activeElement.dataset.key") == "effort", "model menu: → opens Effort inside the same menu, ← returns to its row", str(eff))
        cdp.js("Array.from(document.querySelectorAll('.menu .mi')).find(b=>b.textContent==='6.1 Terra').click()"); time.sleep(.1)
        warn = cdp.js("(document.querySelector('.menu .mwarn')||{}).textContent||''")
        check("Passer à Codex ouvre un nouveau chat. Celui-ci reste dans l'historique." in warn and not any(p.startswith("setModel:terra") for p in posted()), "model menu (V1.2): another provider's model first says it opens a new chat", warn[:120])
        cdp.js("Array.from(document.querySelectorAll('.menu .btn-s')).find(b=>b.textContent==='Passer à Codex').click()"); time.sleep(.15)
        sent = cdp.js("window.__posted.filter(m=>m.type==='setModel').map(m=>m.id+'@'+m.agentId)")
        check(sent[-1:] == ["terra@codex"] and cdp.js("document.querySelector('.model .mn').textContent") == "6.1 Terra" and cdp.js("!!document.querySelector('.model .mw svg')"),
              "model menu (V1.2): confirmed, the agent changes with that model; the button shows the provider's logo and the model", str(sent))
        load("modeles-un-fournisseur")
        one = cdp.js("Array.from(document.querySelectorAll('.menu .mi')).map(b=>b.textContent.trim())")
        check(not any("6.1" in r or "Codex" in r for r in one) and one[:3] == ["Opus 5.5", "Sonnet 5.5Par défaut", "Haiku 5.5"] and cdp.js("document.querySelectorAll('.menu .sep').length") == 1
              and not cdp.js("!!document.querySelector('.menu .btn-s')"), "model menu (V1.2): a provider that is not connected is not listed at all (no grey row, no « Se connecter »)", " | ".join(one))
        load("modeles-aucun-fournisseur")
        none_ = cdp.js("document.querySelector('.menu').innerText")
        check("Pas connecté" in none_ and "Se connecter" in none_ and not cdp.js("!!document.querySelector('.menu .mi[data-key=effort]')"), "model menu (V1.2): nobody connected, the menu shows the connection state and its action", none_.replace("\n", " | ")[:160])
        load("modeles-codex-inconnu")
        unk = cdp.js("Array.from(document.querySelectorAll('.menu .mi')).map(b=>b.textContent.trim())")
        check("Codex" in unk, "model menu (V1.2): a connected provider whose models are not known yet is one row with its name", " | ".join(unk))
        load("6.4-autres-modeles-long")   # the scenario opens the menu, then « Autres modèles »
        more = cdp.js("Array.from(document.querySelectorAll('.menu .mi:not(.back)')).map(b=>b.textContent.trim())")
        check(more == ["Sonnet 4.5", "Opus 4.1"] and cdp.js("Array.from(document.querySelectorAll('.menu .mh')).map(h=>h.textContent)") == ["Claude Code"],
              "model menu (V1.2): « Autres modèles » lists what is beyond 3 per provider, by provider", " | ".join(more))
        load("effort-adaptateur")
        eff2 = cdp.js("(()=>({rows:Array.from(document.querySelectorAll('.menu .mi[role=menuitemradio]')).map(b=>b.textContent.trim()),checked:(document.querySelector('.menu .mi[aria-checked=true]')||{}).textContent,"
                      "chip:document.querySelector('.model .e').textContent,metas:Array.from(document.querySelectorAll('.turn .meta')).map(m=>m.textContent),w:document.querySelector('.menu').getBoundingClientRect().width}))()")
        check(eff2["rows"] == ["Faible", "Moyen", "Élevé", "X-Élevé", "Max"] and eff2["checked"] == "Moyen" and eff2["chip"] == "Moyen" and round(eff2["w"]) == 214,
              "effort (V1.2): Faible, Moyen, Élevé, X-Élevé, Max in French, no « Default », 214 px sub-menu", json.dumps(eff2, ensure_ascii=False)[:300])
        check(eff2["metas"] == ["Opus · 4 s", "Opus · Moyen · 4 s"], "effort (V1.2): the turn's header never says « Default » (« Opus · Moyen · 4 s »)", json.dumps(eff2["metas"], ensure_ascii=False))
        load("6.1c-outil-mcp")
        perm = cdp.js("(()=>{const c=document.querySelector('.perm');const d=document.getElementById('perm-details');return {title:document.getElementById('perm-t').textContent,"
                      "visible:c.innerText,details:d.textContent,open:!d.hidden,step:(()=>{const f=document.querySelector('.steps .fold');if(f)f.click();return document.querySelector('.steps').textContent})(),wait:(document.querySelector('.waiting')||{}).textContent}})()")
        check(perm["title"] == "Claude veut utiliser l'outil « Lister les annotations » (Annotations du navigateur)", "permission: a raw MCP name becomes a readable title", perm["title"])
        check(perm["open"] and "mcp__browser-annotations-preview__browser_annotations_list" in perm["details"] and '"status": "pending"' in perm["details"]
              and "mcp__" not in perm["title"] and "mcp__" not in perm["step"], "permission: raw name and arguments only in « Détails »; steps rewritten too", perm["step"] + " / " + (perm["wait"] or ""))
        load("6.1d-outil-hote")
        check(cdp.js("document.getElementById('perm-t').textContent") == "Claude veut lire les 3 annotations en attente sur 127.0.0.1:5173", "permission: the host's human title is kept as is")

        # B 1.3 — folded step summaries never invent « fichier »
        load("etapes-annotations")
        folds = cdp.js("Array.from(document.querySelectorAll('.steps .fold > span:first-child')).map(s=>s.textContent.replace(/\u00a0/g,' '))")
        check(folds == ["A lu 2 annotations et landing.css, a marqué l'annotation « Bouton de paiement » comme terminée", "A lu 1 annotation",
                        "A marqué l'annotation « Titre » et l'annotation « Badge » comme terminées"], "steps: host summaries merge by verb, a one-step group shows its title, no false « fichier »", " | ".join(folds))
        load("6.3-termine")
        check(cdp.js("document.querySelector('.steps .fold > span').textContent") == "A lu 2 fichiers, en a modifié 1", "steps: real file paths still fold to « A lu 2 fichiers, en a modifié 1 » (6.3)")

        # B 1.4 — folder chip, first-run only when asked, « Traiter les annotations »
        load("dossier-puce")
        fb = cdp.js("({chip:!!document.querySelector('.tb.folder, [aria-label^=\"Dossier du projet\"]'),first:!document.querySelector('.first').hidden,tools:document.querySelectorAll('.foot .tools > *').length})")
        cdp.js("document.querySelector('.tools .tb[aria-haspopup=menu]').click()")
        plus = cdp.js("document.querySelector('.menu').textContent")
        check(not fb["chip"] and not fb["first"] and fb["tools"] == 3 and "Choisir un autre dossier" in plus, "folder (V1.2): no folder chip in the composer (one project per chat), no folder screen; « + » can still change it", json.dumps(fb))
        load("dossier-demande")
        check(cdp.js("!document.querySelector('.first').hidden && !document.querySelector('.agents')"), "folder: the folder screen shows only when the host asks (askFolder), without the agent list when the agent is ready")
        load("annotations-avant")
        bt = cdp.js("(()=>{const b=document.querySelector('.batch');return {h:b.querySelector('.t').textContent,rows:b.querySelectorAll('.batch-row[aria-checked=true]').length,ph:document.getElementById('composer-input').dataset.placeholder,"
                    "ready:document.querySelector('.send').classList.contains('ready'),meta:b.querySelector('.meta').textContent}})()")
        check(bt["h"] == "2 annotations à traiter" and bt["rows"] == 2 and bt["ph"].startswith("Ajouter une précision") and bt["ready"] and bt["meta"] == "p.hero-text · landing.html:23",
              "annotations: the batch sits in the composer, all checked, sendable without text", json.dumps(bt, ensure_ascii=False))
        cdp.js("document.querySelector(\".batch-row[data-id='a-badge']\").click()")
        check(cdp.js("document.querySelector('.batch .t').textContent") == "1 annotation à traiter" and cdp.js("document.activeElement.dataset.id") == "a-badge", "annotations: unchecking keeps one for later, focus stays on the row")
        cdp.js("document.getElementById('composer-input').focus()"); key("Enter")
        sent = cdp.js("window.__posted.filter(m=>m.type==='send').map(m=>m.annotations)")
        check(sent == [{"batchId": "b1", "ids": ["a-hero"]}], "annotations: Entrée sends only the checked annotations", json.dumps(sent))
        time.sleep(.3)
        check(cdp.js("!!document.querySelector('.ann-card') && document.querySelector('.batch').hidden"), "annotations: the sent batch becomes a card in the thread and leaves the composer")
        load("annotations-avant-long")
        check(cdp.js("document.querySelectorAll('.batch-row').length") == 5 and "+ 2 autres" in cdp.js("document.querySelector('.batch-more').textContent"), "annotations: beyond 5, « + 2 autres »")
        load("annotations-statuts")
        st_ = cdp.js("(()=>{const c=document.querySelector('.ann-card');return {prog:c.querySelector('.ann-prog').textContent,rows:Array.from(c.querySelectorAll('.ann-row')).map(r=>r.className.replace('ann-row is-','')+':'+r.querySelector('.ann-st').textContent.trim())}})()")
        check(st_["prog"] == "2 sur 2" and st_["rows"] == ["done:Couleur passée au gris secondaire du site.", "working:Claude s'en occupe"], "annotations: rows go En attente → s'en occupe → Terminée with the summary, header « 2 sur 2 »", json.dumps(st_, ensure_ascii=False))
        cdp.js("document.querySelector(\".ann-row[data-id='a-badge']\").click()")
        check("annotation.show:a-badge" in posted(), "annotations: a click on a row asks the host to show it on the page")
        load("annotations-fin")
        check(cdp.js("document.querySelector('.ann-prog').textContent") == "2 terminées" and cdp.js("!!document.querySelector('.ann-prog.ok svg')"), "annotations: finished card says « ✓ 2 terminées »")
        load("annotations-non-traitee")
        nt = cdp.js("(()=>{const r=document.querySelector(\".ann-row[data-id='a-badge'] .ann-st\");return {t:r.textContent,c:getComputedStyle(r).color,prog:document.querySelector('.ann-prog').textContent,inst:!document.querySelector('.ann-card .detail').hidden}})()")
        check(nt["t"].startswith("Non traitée : le badge") and nt["c"] == "rgb(230, 172, 61)" and nt["prog"] == "1 terminée · 1 non traitée" and nt["inst"], "annotations: a skipped one is amber with its reason; the instruction unfolds", json.dumps(nt, ensure_ascii=False))
        load("6.7a-vide"); cdp.js("document.getElementById('composer-input').focus()"); key("/", "Slash", 0, "/"); time.sleep(.15)
        check(cdp.js("Array.from(document.querySelectorAll('#combo [role=option]')).some(o=>o.textContent.startsWith('/annotations'))"), "annotations: « /annotations » is always in the / menu")
        key("Enter"); time.sleep(.2)
        check("annotations.request" in posted() and cdp.js("!document.querySelector('.batch').hidden"), "annotations: /annotations asks the host and the batch lands in the composer")

        # « Voir les changements » (checkpoint.viewChanges → changes.diff)
        load("6.6-voir-changements")
        view = cdp.js("""(()=>{const v=document.getElementById('changes');return {open:!v.hidden,role:v.getAttribute('role'),files:v.querySelectorAll('section.chg-file').length,
          diffs:v.querySelectorAll('.udiff').length,add:v.querySelectorAll('.ul.add').length,del:v.querySelectorAll('.ul.del').length,folds:v.querySelectorAll('.unfold').length,
          nums:Array.from(v.querySelectorAll('.udiff')[0].querySelectorAll('.ul:not(.hunk) .ln')).slice(0,2).map(n=>n.textContent),
          mono:getComputedStyle(v.querySelector('.udiff')).fontFamily,addBg:getComputedStyle(v.querySelector('.ul.add')).backgroundColor,
          permAddBg:'rgba(111, 208, 135, 0.1)',focus:document.activeElement.getAttribute('aria-label')}})()""")
        check(view["open"] and view["role"] == "dialog" and view["files"] == 3 and view["diffs"] == 3, "changes view: opens as a dialog with one unified diff per file", json.dumps(view)[:300])
        check(any(p.startswith("checkpoint.viewChanges") for p in posted()), "changes view: asks the host (checkpoint.viewChanges) and fills from changes.diff")
        check(view["folds"] >= 1 and len(view["nums"]) == 2 and view["nums"][0] == view["nums"][1] and int(view["nums"][0]) > 18, "changes view: unchanged runs fold, line numbers follow the @@ header", f"{view['folds']} fold(s), first numbers {view['nums']}")
        check("JetBrains Mono" in view["mono"] and view["addBg"] == view["permAddBg"] and view["add"] >= 10 and view["del"] >= 3, "changes view: JetBrains Mono, added/removed lines colored like the permission card", view["mono"][:40] + " " + view["addBg"])
        check(view["focus"] == "Retour au fil", "changes view: focus lands on « Retour au fil »", str(view["focus"]))
        n0 = cdp.js("document.querySelectorAll('.changes .udiff')[0].querySelectorAll('.ul').length")
        cdp.js("document.querySelector('.changes .unfold').click()"); time.sleep(.1)
        n1 = cdp.js("document.querySelectorAll('.changes .udiff')[0].querySelectorAll('.ul').length")
        check(n1 > n0 and cdp.js("document.activeElement.classList.contains('udiff')"), "changes view: a fold expands in place and keeps focus in that diff", f"{n0} → {n1} lines")
        key("Escape")
        check(cdp.js("document.getElementById('changes').hidden") and active() == "Voir les changements", "changes view: Échap returns to the thread, focus back on the trigger", active())
        load("6.6-voir-changements-fichier")
        pos = cdp.js("(()=>{const v=document.getElementById('changes');const s=document.getElementById('chg-2');const a=v.getBoundingClientRect(),r=s.getBoundingClientRect();return {top:r.top-a.top,bottom:a.bottom-r.bottom,scrolled:v.scrollTop,sh:v.scrollHeight,ch:v.clientHeight,hs:Array.from(v.children).map(c=>c.offsetHeight+'/'+c.scrollHeight).join(' '),pb:getComputedStyle(v).paddingBottom,empty:v.querySelectorAll('.chg-empty').length,off:s.offsetTop,par:s.offsetParent&&s.offsetParent.className}})()")
        check(pos["scrolled"] > 0 and pos["top"] >= 52 and pos["bottom"] >= 0 and "viewChanges" in "".join(posted()), "changes view: opening from a file row scrolls that file into view", json.dumps(pos))

        load("6.6-nouvelles-etapes")
        check(cdp.js("!document.querySelector('.newsteps').hidden") and "2 nouvelles" in cdp.js("document.querySelector('.newsteps').textContent"), "scroll: scrolled up, the thread stays and the « nouvelles étapes » pill counts")
        load("markdown")
        check(cdp.js("!document.querySelector('.text script') && document.querySelector('.text').textContent.includes('<script>alert(1)</script>')"), "markdown: raw HTML from the agent stays text")
        check(cdp.js("!!document.querySelector('.code.clamped') && !!document.querySelector('.more')"), "markdown: code over 15 lines is clamped with « Afficher plus »")
        cdp.js("document.querySelector('.md-link').click()")
        check(any(p.startswith("link.open") for p in posted()) and cdp.js("location.pathname").endswith("dev.html"), "markdown: a link asks the host and never navigates")

        # reduced motion
        load("6.10-travaille", reduced=True)
        anim = cdp.js("[getComputedStyle(document.querySelector('.turn')).animationName, getComputedStyle(document.querySelector('.working')).animationName]")
        check(anim[0] in ("fade", "none") and anim[1] == "none", "motion: reduced motion keeps fades only (no rise, no shimmer)", str(anim))
        load("6.10-travaille")
        anim = cdp.js("[getComputedStyle(document.querySelector('.working')).animationName]")
        check(anim[0] == "shimmer", "motion: « Travaille… » shimmer runs when motion is allowed", str(anim))

        # production page alone (no host): loads, no error, no request outside its files
        requests_seen.clear(); cdp.events.clear(); viewport(380, 698)
        cdp.call("Page.navigate", url=f"{BASE}/chat-panel/index.html"); time.sleep(1.0)
        errs = errors_of("index")
        allowed = re.compile(r"^/(chat-panel/(index\.html|panel\.css|panel\.js|markdown\.js|icons\.js)|fonts/(InterDisplay-Regular|InterDisplay-Medium|JetBrainsMono-Regular)\.ttf)(\?.*)?$")
        extra = [r for r in requests_seen if not allowed.match(r)]
        check(not errs and not extra, "security: index.html alone loads only its own files, no error", "; ".join(errs + extra)[:300])
        ext = [e["params"]["request"]["url"] for e in cdp.events if e["method"] == "Network.requestWillBeSent" and not e["params"]["request"]["url"].startswith((BASE, "data:"))]
        check(not ext, "security: no request leaves the loopback server", "; ".join(ext)[:200])
        fonts = cdp.js("document.fonts.ready.then(()=>Array.from(document.fonts).filter(f=>f.status==='loaded').map(f=>f.family+' '+f.weight))")
        check({"Inter Display 400", "Inter Display 500"} <= set(fonts), "fonts: Inter Display Regular and Medium load from the app's files", ", ".join(fonts))
        load("markdown")
        mono = cdp.js("document.fonts.ready.then(()=>Array.from(document.fonts).filter(f=>f.status==='loaded'&&f.family.includes('JetBrains')).length)")
        check(mono >= 1, "fonts: JetBrains Mono loads from the app's files for code", f"{mono} face(s)")

        # ------------------------------------------------------------ 7. performance
        t0 = time.time(); load("perf-500"); t_load = time.time() - t0
        n_items = cdp.js("document.querySelectorAll('.log > .um, .log > .turn').length")
        perf = cdp.js("""new Promise(r=>{const s=document.querySelector('.scroller');s.scrollTop=0;const d=[];let last=performance.now();const t0=last;
          const f=(t)=>{d.push(t-last);last=t;s.scrollTop+=60;if(t-t0<3000&&s.scrollTop+s.clientHeight<s.scrollHeight)requestAnimationFrame(f);else{d.shift();d.sort((a,b)=>a-b);
          r({frames:d.length,p95:d[Math.floor(d.length*.95)]||0,max:d[d.length-1]||0,long:window.__long.filter(x=>x>50).length,height:s.scrollHeight})}};requestAnimationFrame(f)})""")
        report["perf"]["thread500"] = dict(perf, items=n_items, load_s=round(t_load, 2))
        check(n_items == 500, "perf: 500 messages rendered", f"{n_items} items, replay+render {t_load:.2f} s")
        check(perf["p95"] < 34 and perf["max"] < 120, "perf: scrolling 500 messages stays fluid", f"{perf['frames']} frames, p95 {perf['p95']:.1f} ms, max {perf['max']:.1f} ms")

        load("stream")
        s = cdp.js("""new Promise(r=>{const d=[];let last=performance.now(),on=true;const f=(t)=>{d.push(t-last);last=t;if(on)requestAnimationFrame(f)};requestAnimationFrame(f);
          window.__long.length=0;window.devHost.stream(3000).then(k=>{on=false;d.shift();d.sort((a,b)=>a-b);r({chunks:k,frames:d.length,p95:d[Math.floor(d.length*.95)],max:d[d.length-1],
          long:window.__long.filter(x=>x>50).length,chars:document.querySelector('.text').textContent.length})})})""")
        report["perf"]["stream"] = s
        check(s["p95"] < 25 and s["long"] == 0, "perf: streamed text without jank", f"{s['chunks']} chunks in 3 s, {s['chars']} chars, p95 frame {s['p95']:.1f} ms, max {s['max']:.1f} ms, long tasks {s['long']}")
        shot(os.path.join(OUT, "stream-fin.png"))
finally:
    if proc:
        proc.terminate()
        try: proc.wait(10)
        except Exception: proc.kill()
    srv.shutdown()
    time.sleep(.5); shutil.rmtree(profile, ignore_errors=True)
    json.dump(report, open(os.path.join(OUT, "report.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=1)

print(f"\n{len(report['checks']) - len(failures)} / {len(report['checks'])} checks passed" + (f" — FAILED: {', '.join(failures)}" if failures else ""))
sys.exit(1 if failures else 0)
