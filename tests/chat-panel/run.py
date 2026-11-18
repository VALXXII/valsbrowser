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
requests_seen = []
class Handler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *a): pass
    def translate_path(self, path):
        p = urllib.parse.unquote(urllib.parse.urlsplit(path).path)
        for prefix, base in (("/out/", OUT), ("/ref/", REF)):
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
HOVER = {"6.6-survol": ".turn", "6.6-fil": ".um[data-id='u2'] .bubble", "6.11-fait": ".um[data-id='u1'] .bubble"}

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
             "window.__long=[];try{new PerformanceObserver(l=>{for(const e of l.getEntries())window.__long.push(e.duration)}).observe({type:'longtask',buffered:true})}catch(e){}")

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

    # ---------------------------------------------------------------- 4. every scenario: screenshot + a11y + errors
    names = cdp.js("new Promise(r=>{})", await_promise=False) if False else None
    cdp.call("Page.navigate", url=f"{BASE}/chat-panel/dev.html?scenario=6.7a-vide")
    time.sleep(.5)
    names = cdp.js("new Promise(r=>{const f=()=>window.__scenarios?r(window.__scenarios):setTimeout(f,20);f()})")
    scen = [n for n in names if (not ONLY or n in ONLY) and n not in ("stream", "perf-500")]
    for name in scen:
        h = 777 if name == "6.6-fil" else 640 if name in ("6.7a-vide", "6.7b-historique") else 698
        ok = load(name, 380, h)
        if name in HOVER:
            box = cdp.js(f"(()=>{{const n=document.querySelector({json.dumps(HOVER[name])});if(!n)return null;const r=n.getBoundingClientRect();return [r.left+r.width/2,r.top+r.height/2]}})()")
            if box: cdp.call("Input.dispatchMouseEvent", type="mouseMoved", x=box[0], y=box[1]); time.sleep(.25)
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
