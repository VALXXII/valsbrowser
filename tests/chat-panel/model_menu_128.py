"""#128 v2 — the model menu as the old menu with a header per agent (Paper « #128 Menu des modèles · v2 ancien menu +
groupes (maquette prête) », page « MCP - Design UI UX »), in the chat panel and in the comment bubble, and the bubble's
menus never cut by the bubble (Val's trial on 0.3.833: the menu squeezed into the bubble, one row and a scrollbar).
Headless, no window: the panel (assets/chat-panel) in the WebView2 runtime's Chromium (EdgeCore), driven over DevTools
with a throw-away profile; pages served on 127.0.0.1; the fake host (dev-host.js) plays the agents. Nothing touches the
network, a real agent or a user profile.

  python -I tests/chat-panel/model_menu_128.py [--edge <msedge.exe>] [--out build/chat-qa/128]

Checks: the list comes from the agents (headers, models, order, nothing hard-coded); the first level shows each agent's
latest models (the rule of panel.js `splitModels`: per line, its newest version; the default always; a context-size variant
or a name without a version under « Plus de modèles »), the others under « Plus de modèles › » — beside the menu in the
bubble (214 px), the list replacing the menu in the panel (« ‹ Plus de modèles »); keyboard (→ / Entrée open, ← / Échap
go back, arrows skip greyed rows); a provider not ready is listed with its state and action (Connecter / Installer /
Réessayer: the existing flows) and its models greyed out, not choosable; another provider's model says it opens a new
chat (only when the chat has messages); every Paper value (sizes, paddings, radii, type, colours) measured in the page;
the bubble at each edge of its page area (top, bottom, left, right, and squeezed between): the menu and each sub-menu
whole — every row up to « Effort » inside, nothing scrolled, inside the room the host can show (the page area, the
window's margin, 400 px). Renders: <out>/<name>.png (the menu) and <out>/cote-a-cote-<name>.png (Paper | page |
differences), from tests/chat-panel/reference/128 (Paper exports, 1x). Exit code 0 when every check passes.
"""
import base64, glob, http.server, json, os, re, socket, struct, subprocess, sys, tempfile, threading, time, urllib.parse, urllib.request

sys.stdout.reconfigure(encoding="utf-8")
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
ASSETS = os.path.join(ROOT, "assets")
REF = os.path.join(HERE, "reference", "128")
args = sys.argv[1:]
opt = lambda n, d=None: args[args.index(n) + 1] if n in args else d
OUT = os.path.abspath(opt("--out", os.path.join(ROOT, "build", "chat-qa", "128")))
EDGE = opt("--edge") or next(iter(sorted(glob.glob(os.path.join(os.environ.get("LOCALAPPDATA", ""), "Microsoft", "EdgeCore", "*", "msedge.exe"))
                                         + glob.glob(os.path.join(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"), "Microsoft", "EdgeCore", "*", "msedge.exe")), reverse=True)), None)
os.makedirs(OUT, exist_ok=True)

failures, count = [], [0]
def check(ok, name, detail=""):
    count[0] += 1
    print(("ok   " if ok else "FAIL ") + name + (" — " + str(detail)[:600] if detail and not ok else ""))
    if not ok: failures.append(name)

# ------------------------------------------------------------------ static: no model name written in the page
js = open(os.path.join(ASSETS, "chat-panel", "panel.js"), encoding="utf-8").read()
menu_src = js[js.index("function modelItems("):js.index("function openModeMenu(")]
rule_src = js[js.index("function splitModels("):js.index("function modelItems(")]
check(not re.search(r"GPT|gpt-|Sonnet|Haiku|Opus|Fable|o3|o4-mini", menu_src + rule_src), "static: the model menu and its « latest » rule write no model name (the list comes from the agents)")
check("PER_GROUP" not in js and "Autres modèles" not in menu_src and "Plus de modèles" in menu_src, "static: no fixed « 3 per group » cap; the others are under « Plus de modèles »")

# ------------------------------------------------------------------ server, browser, DevTools
COMPARE_HTML = """<!doctype html><meta charset=utf-8><body style="margin:0;background:#171717"><script>
window.compare=(a,b)=>new Promise(res=>{const A=new Image(),B=new Image();let n=0;const go=()=>{if(++n<2)return;
const w=Math.max(A.width,B.width),h=Math.max(A.height,B.height),G=16,c=document.createElement('canvas');c.width=w*3+G*4;c.height=h+G*2;
const g=c.getContext('2d');g.fillStyle='#171717';g.fillRect(0,0,c.width,c.height);g.drawImage(A,G,G);g.drawImage(B,w+G*2,G);
const pix=(I)=>{const k=document.createElement('canvas');k.width=w;k.height=h;const x=k.getContext('2d');x.fillStyle='#171717';x.fillRect(0,0,w,h);x.drawImage(I,0,0);return x.getImageData(0,0,w,h).data};
const da=pix(A),db=pix(B),out=g.createImageData(w,h);let sum=0,big=0;
for(let i=0;i<da.length;i+=4){const d=(Math.abs(da[i]-db[i])+Math.abs(da[i+1]-db[i+1])+Math.abs(da[i+2]-db[i+2]))/3;sum+=d;if(d>40)big++;
out.data[i]=d>40?255:d*4;out.data[i+1]=d>40?60:d*2;out.data[i+2]=d>40?60:d*2;out.data[i+3]=255}
g.putImageData(out,w*2+G*3,G);res({png:c.toDataURL('image/png'),mean:Math.round(sum/(w*h)*100)/100,big,sameSize:A.width===B.width&&A.height===B.height,a:[A.width,A.height],b:[B.width,B.height]})};
A.onload=B.onload=go;A.src=a;B.src=b});
</script>"""
class Handler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *a): pass
    def translate_path(self, path):
        p = urllib.parse.unquote(urllib.parse.urlsplit(path).path)
        if p.startswith("/ref128/"): return os.path.join(REF, p[8:])
        if p.startswith("/out128/"): return os.path.join(OUT, p[8:])
        return os.path.join(ASSETS, p.lstrip("/"))
    def do_GET(self):
        if self.path.startswith("/compare128.html"):
            body = COMPARE_HTML.encode(); self.send_response(200); self.send_header("Content-Type", "text/html; charset=utf-8"); self.send_header("Content-Length", str(len(body))); self.end_headers(); self.wfile.write(body); return
        super().do_GET()
    def end_headers(self):
        self.send_header("Cache-Control", "no-store"); super().end_headers()
srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
threading.Thread(target=srv.serve_forever, daemon=True).start()
BASE = f"http://127.0.0.1:{srv.server_address[1]}"

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
        op, data = b1 & 15, self._read(ln)
        while not (b1 & 0x80):  # continuation frames
            b1, b2 = self._read(2); ln = b2 & 127
            if ln == 126: ln = struct.unpack(">H", self._read(2))[0]
            elif ln == 127: ln = struct.unpack(">Q", self._read(8))[0]
            data += self._read(ln)
        return op, data
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
    def js(self, expr):
        r = self.call("Runtime.evaluate", expression=expr, awaitPromise=True, returnByValue=True)
        if "exceptionDetails" in r: raise RuntimeError("JS: " + json.dumps(r["exceptionDetails"])[:500])
        return r["result"].get("value")

def kill_tree(p):
    if p is None: return
    subprocess.run(["taskkill", "/PID", str(p.pid), "/T", "/F"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) if os.name == "nt" else p.kill()
    try: p.wait(10)
    except Exception: pass

profile = tempfile.mkdtemp(prefix="chat-128-qa-")
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
    for d in ("Page", "Runtime", "Log"): cdp.call(d + ".enable")
    cdp.call("Emulation.setFocusEmulationEnabled", enabled=True)

    def load(name, w=380, h=698):
        if name.startswith("c-"): w, h = 1200, 1400   # the bubble's window: up to 400 px of transparent margin each side
        cdp.call("Emulation.setDeviceMetricsOverride", width=w, height=h, deviceScaleFactor=1, mobile=False)
        cdp.call("Emulation.setEmulatedMedia", features=[{"name": "prefers-reduced-motion", "value": "reduce"}])
        cdp.events.clear()
        cdp.call("Page.navigate", url=f"{BASE}/chat-panel/dev.html?scenario={urllib.parse.quote(name)}")
        ok = cdp.js("new Promise(r=>{const t0=performance.now();const f=()=>{if(window.__scenarioDone)r(true);else if(performance.now()-t0>15000)r(false);else setTimeout(f,20)};f()})")
        cdp.js("document.fonts.ready.then(()=>new Promise(r=>setTimeout(r,300)))")
        errs = [e for e in cdp.events if e["method"] == "Runtime.exceptionThrown" or (e["method"] == "Log.entryAdded" and e["params"]["entry"]["level"] == "error")]
        logs = cdp.js("(window.__posted||[]).filter(m=>m.type==='panel.log').map(m=>m.message)")
        check(ok and not errs and not logs, f"{name}: played without error", json.dumps([e["params"] for e in errs] + logs)[:400])
        return ok
    MENU = ".menu.models:not(.sub-menu)"
    def menu(sel=MENU):   # an open model menu (or its sub-menu), read in the page
        return cdp.js("""(sel=>{const m=document.querySelector(sel);if(!m)return null;const r=m.getBoundingClientRect(),cs=getComputedStyle(m);
          const rows=[];for(const n of m.children){const b=n.getBoundingClientRect(),s=getComputedStyle(n);
            if(n.classList.contains('mm-head'))rows.push({k:'head',t:n.querySelector('.mm-hn').textContent,h:b.height,act:n.querySelector('.mm-act')?n.querySelector('.mm-act').textContent:'',logo:!!n.querySelector('svg.mm-logo')});
            else if(n.classList.contains('sep'))rows.push({k:'sep'});
            else if(n.classList.contains('mi'))rows.push({k:n.classList.contains('back')?'back':n.dataset.key||'row',t:n.querySelector('.lb').textContent,h:b.height,top:b.top,bottom:b.bottom,tag:(n.querySelector('.tag')||{}).textContent||'',
              val:(n.querySelector('.val')||{}).textContent||'',checked:n.getAttribute('aria-checked')==='true',off:n.getAttribute('aria-disabled')==='true',op:+s.opacity,
              hint:(()=>{const x=n.querySelector('.mm-hint');return x&&x.getClientRects().length?x.textContent:''})()});}
          return {w:r.width,h:r.height,x:r.left,y:r.top,right:r.right,bottom:r.bottom,radius:cs.borderTopLeftRadius,pad:cs.paddingTop,bg:cs.backgroundColor,border:cs.borderTopColor,bw:cs.borderTopWidth,
            shadow:cs.boxShadow,gap:cs.rowGap,scroll:m.scrollHeight-m.clientHeight,rows,label:m.getAttribute('aria-label')}})(""" + json.dumps(sel) + ")")
    labels = lambda mm: [("·" if r["k"] == "sep" else ("#" + r["t"]) if r["k"] == "head" else ("‹" + r["t"]) if r["k"] == "back" else r["t"]) for r in mm["rows"]]
    def posted(types): return cdp.js(f"window.__posted.filter(m=>{json.dumps(types)}.includes(m.type)).map(m=>{{const o=Object.assign({{}},m);delete o.v;return o}})")
    def row_el(label, sel=".menu"): return f"Array.from(document.querySelectorAll({json.dumps(sel + ' .mi')})).find(b=>b.querySelector('.lb').textContent==={json.dumps(label)})"
    def center(label, sel=".menu"): return cdp.js(f"(()=>{{const n={row_el(label, sel)};if(!n)return null;const r=n.getBoundingClientRect();return [r.left+r.width/2,r.top+r.height/2]}})()")
    def hover(label, sel=".menu"):
        c = center(label, sel)
        if c: cdp.call("Input.dispatchMouseEvent", type="mouseMoved", x=c[0], y=c[1]); time.sleep(.15)
        return c
    def click_row(label, sel=".menu"): cdp.js(row_el(label, sel) + ".click()"); time.sleep(.15)
    def key(k):
        kc = {"ArrowDown": 40, "ArrowUp": 38, "Enter": 13, "Escape": 27, "ArrowRight": 39, "ArrowLeft": 37}[k]
        cdp.call("Input.dispatchKeyEvent", type="rawKeyDown", key=k, code=k, windowsVirtualKeyCode=kc)
        if k == "Enter": cdp.call("Input.dispatchKeyEvent", type="char", key=k, code=k, windowsVirtualKeyCode=kc, text=chr(13))   # a real Entrée: the button's click
        cdp.call("Input.dispatchKeyEvent", type="keyUp", key=k, code=k, windowsVirtualKeyCode=kc)
        time.sleep(.1)
    active = lambda: cdp.js("(()=>{const a=document.activeElement;return a?(a.getAttribute('aria-label')||(a.querySelector&&a.querySelector('.lb')?a.querySelector('.lb').textContent:a.textContent)).trim():''})()")
    def shot(name, sel=MENU, blur=True):   # the menu alone; the focus ring of the scenario's scripted click is not a user's (a real click shows none)
        if blur: cdp.js("document.activeElement&&document.activeElement.blur()"); time.sleep(.05)
        r = cdp.js(f"(()=>{{const m=document.querySelector({json.dumps(sel)});const r=m.getBoundingClientRect();return {{x:r.left,y:r.top,width:r.width,height:r.height}}}})()")
        clip = {"x": r["x"], "y": r["y"], "width": r["width"], "height": r["height"], "scale": 1}
        path = os.path.join(OUT, name + ".png")
        open(path, "wb").write(base64.b64decode(cdp.call("Page.captureScreenshot", format="png", clip=clip)["data"]))
        return path
    def side_by_side(name, ref):
        cdp.call("Page.navigate", url=f"{BASE}/compare128.html"); time.sleep(.3)
        r = cdp.js(f"window.compare('/ref128/{ref}.png?{time.time()}','/out128/{name}.png?{time.time()}')")
        out = os.path.join(OUT, "cote-a-cote-" + name + ".png")
        open(out, "wb").write(base64.b64decode(r["png"].split(",", 1)[1]))
        print(f"     render {name}: Paper {r['a']} | page {r['b']} | mean difference {r['mean']} / 255, {r['big']} px above 40 — {out}")
        return r
    # the same box to the pixel; what differs is the glyphs' antialiasing (Paper's text renderer vs Chromium's) and the
    # rounded outline's edge: a mean difference of a few levels out of 255
    def render_ok(r, max_big=None): return r["sameSize"] and r["mean"] <= 4

    W60 = "rgba(255, 255, 255, 0.6)"
    MAIN = ["#Claude Code", "Opus 5.5", "Sonnet 5.5", "Fable 5.2", "Haiku 5.5", "·", "#Codex", "GPT-5.5", "GPT-5.5 mini", "·", "Plus de modèles", "Effort"]
    MORE = ["#Claude Code", "Opus 5", "Sonnet 5", "Opus 4.6", "Sonnet 4.6", "Haiku 4.5", "·", "#Codex", "GPT-5.4", "GPT-5.4 mini", "GPT-5.3-Codex"]

    # ---------------------------------------------------------------- panel: the old menu, a header per agent, the latest models
    load("128-panneau")
    m = menu()
    check(m and labels(m) == MAIN, "panel: Claude Code's latest models, Codex's latest, then « Plus de modèles › » and « Effort › » (Paper v2)", m and labels(m))
    check(m and [r["tag"] for r in m["rows"] if r["k"] == "row"] == ["", "Par défaut", "", "", "", ""], "panel: « Par défaut » on the agent's default model only", m and m["rows"])
    check(m and [r["t"] for r in m["rows"] if r.get("checked")] == ["Sonnet 5.5"], "panel: the chosen model is checked", m and m["rows"])
    check(m and m["rows"][-1]["k"] == "effort" and m["rows"][-1]["val"] == "Élevé" and m["rows"][-2]["k"] == "more", "panel: « Plus de modèles › » then « Effort  Élevé › » at the bottom", m and m["rows"][-2:])
    check(m and all(r["logo"] for r in m["rows"] if r["k"] == "head"), "panel: each group header has its agent's logo")
    check(m and m["w"] == 300 and m["radius"] == "12px" and m["pad"] == "6px" and m["bg"] == "rgb(36, 36, 36)" and m["bw"] == "1px" and m["shadow"] == "none" and m["gap"] == "1px" and m["h"] == 331,
          "panel: menu 300 × 331, radius 12, padding 6, #242424, 1 px outline, no shadow, rows 1 px apart (Paper 265R-0)", m and {k: m[k] for k in ("w", "h", "radius", "pad", "bg", "bw", "shadow", "gap")})
    check(m and [r["h"] for r in m["rows"] if r["k"] != "sep"] == [24, 30, 30, 30, 30, 24, 30, 30, 30, 30], "panel: headers 24 px (6 / 2), rows 30 px (6 / 10) (Paper)", m and [r.get("h") for r in m["rows"]])
    v = cdp.js("""(()=>{const m=document.querySelector('.menu.models');const q=(s)=>m.querySelector(s);const cs=(n)=>n?getComputedStyle(n):{};const r=(n)=>n.getBoundingClientRect();
      const row=Array.from(m.querySelectorAll('.mi')).find(b=>b.getAttribute('aria-checked')==='true'),lb=row.querySelector('.lb'),tag=row.querySelector('.tag'),ck=row.querySelector('.ck'),head=q('.mm-head'),hn=q('.mm-hn'),logo=q('.mm-logo'),sep=q('.sep'),
        more=q('.mi[data-key=more]'),eff=q('.mi[data-key=effort]'),val=eff.querySelector('.val'),sub=eff.querySelector('.sub');
      return {row:[cs(row).paddingTop,cs(row).paddingLeft,cs(row).paddingRight,cs(row).borderTopLeftRadius,cs(row).backgroundColor,cs(row).columnGap],lb:[cs(lb).fontSize,cs(lb).lineHeight,cs(lb).fontWeight,cs(lb).color],
        tag:[cs(tag).fontSize,cs(tag).lineHeight,cs(tag).fontWeight,cs(tag).color,cs(tag).backgroundColor,cs(tag).borderTopLeftRadius,r(tag).height,cs(tag).paddingLeft,Math.round(r(tag).left-r(lb).right)],
        ck:[r(ck).width,cs(ck).color,Math.round(r(row).right-r(ck).right)],head:[cs(head).paddingTop,cs(head).paddingBottom,cs(head).paddingLeft,Math.round(r(hn).left-r(logo).right),r(logo).width],
        hn:[cs(hn).fontSize,cs(hn).lineHeight,cs(hn).fontWeight,cs(hn).color],sep:[r(sep).height,cs(sep).backgroundColor,cs(sep).marginTop,cs(sep).marginLeft],
        more:[cs(more).columnGap,r(more.querySelector('.sub')).width,+getComputedStyle(more.querySelector('.sub')).opacity],
        eff:[cs(eff.querySelector('.lb')).fontSize,cs(val).fontSize,cs(val).lineHeight,cs(val).color,r(sub).width,Math.round(r(row).right-r(sub).right)]}})()""")
    exp = {"row": ["6px", "10px", "10px", "8px", "rgba(255, 255, 255, 0.06)", "10px"], "lb": ["13px", "18px", "500", "rgb(255, 255, 255)"],
           "tag": ["10px", "12px", "500", "rgb(198, 198, 198)", "rgba(255, 255, 255, 0.08)", "4px", 16, "5px", 6], "ck": [14, "rgb(143, 169, 255)", 10],
           "head": ["6px", "2px", "10px", 8, 14], "hn": ["12px", "16px", "500", "rgb(146, 146, 146)"], "sep": [1, "rgba(255, 255, 255, 0.08)", "4px", "10px"],
           "more": ["8px", 11, .5], "eff": ["13px", "12px", "16px", W60, 11, 10]}
    for k in exp: check(v[k] == exp[k], f"panel: Paper values — {k}" + (" (#313131 = white 6 % on #242424)" if k == "row" else " (#353535 = white 8 %)" if k == "tag" else " (#363636 = white 8 %)" if k == "sep" else ""), f"page {v[k]} / Paper {exp[k]}")
    # the board: « Plus de modèles » hovered, the chosen row still filled
    hover("Plus de modèles"); time.sleep(.1)
    check(not cdp.js("!!document.querySelector('.sub-menu')") and cdp.js("document.querySelectorAll('.menu').length") == 1, "panel: hovering « Plus de modèles » opens nothing beside (the panel opens it on click / → / Entrée)")
    shot("v2-menu-plus-survole"); r1 = side_by_side("v2-menu-plus-survole", "v2-menu-plus-survole")
    check(render_ok(r1, 400), "render: the panel's menu, « Plus de modèles » hovered, the size and pixels of the Paper board (265R-0, 300 × 331)", r1)
    # « Plus de modèles »: the list replaces the menu (« ‹ Plus de modèles »), as the old menu did
    load("128-panneau-plus")
    m = menu()
    check(m and labels(m) == ["‹Plus de modèles", "·"] + MORE and m["w"] == 300 and m["label"] == "Plus de modèles" and m["scroll"] <= 0,
          "panel: « Plus de modèles › » — the other models replace the list in the same menu, by agent, with « ‹ Plus de modèles » (Paper v2, board 1)", m and [labels(m), m["w"], m["label"]])
    b = cdp.js("(()=>{const b=document.querySelector('.menu .mi.back'),s=getComputedStyle(b),i=b.querySelector('.backi'),r=(n)=>n.getBoundingClientRect();return [s.color,s.columnGap,r(i).width,+getComputedStyle(i).opacity,Math.round(r(b.querySelector('.lb')).left-r(i).right),Math.round(r(i).left-r(b).left)]})()")
    check(b == ["rgb(255, 255, 255)", "8px", 11, .5, 8, 10], "panel: the back row — chevron 11 at 50 % (#929292), 8 px, label white, padding 10 (Paper)", b)
    check(active() == "Opus 5", "panel: the focus goes to the first model of the list", active())
    shot("v2-panneau-plus"); r2 = side_by_side("v2-panneau-plus", "v2-panneau-plus")
    check(render_ok(r2, 400), "render: the panel's « Plus de modèles » list, the size and pixels of the Paper board (266Z-0, 300 × 362)", r2)
    load("128-panneau-plus"); key("ArrowLeft")
    check(menu() and labels(menu()) == MAIN and cdp.js("document.activeElement.dataset.key") == "more", "panel: ← goes back to the latest models, the focus on « Plus de modèles »")
    key("Enter")
    check(menu() and labels(menu())[0] == "‹Plus de modèles" and active() == "Opus 5", "panel: Entrée on « Plus de modèles » opens its list again", active())
    key("Escape")
    check(cdp.js("!document.querySelector('.menu')") and cdp.js("document.activeElement === document.querySelector('.model .m')"), "panel: Échap closes the menu (as before), the focus back on its button")
    load("128-panneau-plus"); click_row("GPT-5.4")
    check(posted(["setModel", "setAgent"])[-1:] == [{"type": "setModel", "id": "gpt-5.4", "agentId": "codex"}] and cdp.js("!document.querySelector('.menu')"),
          "panel: a model chosen in « Plus de modèles » is told to the host (setModel), the menu closes", posted(["setModel", "setAgent"])[-1:])

    # ---------------------------------------------------------------- the « latest » rule, for any agent (nothing written per model)
    def rule(models_a, models_b=None):
        cdp.js("window.devHost.send({type:'state',agents:[{id:'claude-code',name:'Claude Code',icon:'claude',status:'ready',models:" + json.dumps(models_a) + "},"
               "{id:'codex',name:'Codex',icon:'codex',status:'ready',models:" + json.dumps(models_b or [{"id": "x", "label": "X 1"}]) + "}],agentId:'claude-code',model:" + json.dumps(models_a[0]["id"]) + "})")
        time.sleep(.1)
        if cdp.js("!!document.querySelector('.menu')"): key("Escape")
        cdp.js("document.querySelector('.model .m').click()"); time.sleep(.15); first = labels(menu())
        cdp.js("document.querySelector('.menu .mi[data-key=more]')&&document.querySelector('.menu .mi[data-key=more]').click()"); time.sleep(.15)
        more = labels(menu()) if menu() and menu()["label"] == "Plus de modèles" else []
        if cdp.js("!!document.querySelector('.menu')"): key("Escape")
        return first, more
    load("128-panneau")
    f1, m1 = rule([{"id": "a", "label": "Opus 5.5"}, {"id": "b", "label": "Opus 5.5 (1M context)"}, {"id": "c", "label": "Opus Plan"}, {"id": "d", "label": "Sonnet 5.5", "isDefault": True},
                   {"id": "e", "label": "Sonnet 5"}, {"id": "f", "label": "Haiku 4.5"}])
    check(f1[:4] == ["#Claude Code", "Opus 5.5", "Sonnet 5.5", "Haiku 4.5"] and m1[2:] == ["#Claude Code", "Opus 5.5 (1M context)", "Opus Plan", "Sonnet 5"],
          "rule: per line its newest version (Haiku 4.5 is the newest Haiku); a context-size variant, a name without version and an older version go under « Plus de modèles »", [f1, m1])
    f2, m2 = rule([{"id": "a", "label": "Alpha"}, {"id": "b", "label": "Beta"}, {"id": "c", "label": "Gamma"}, {"id": "d", "label": "Delta"}, {"id": "e", "label": "Epsilon"}, {"id": "f", "label": "Zeta", "isDefault": True}])
    check(f2[:6] == ["#Claude Code", "Alpha", "Beta", "Gamma", "Delta", "Zeta"] and m2[2:] == ["#Claude Code", "Epsilon"],
          "rule: an agent that versions no model keeps its first 4 on the first level (+ its default), the others under « Plus de modèles »", [f2, m2])
    f3, m3 = rule([{"id": "a", "label": "Opus 5.5"}], [{"id": "g1", "label": "gpt-5.5"}, {"id": "g2", "label": "gpt-5.5-codex"}, {"id": "g3", "label": "gpt-5.4"}, {"id": "g4", "label": "Codex Mini"}])
    check(f3 == ["#Claude Code", "Opus 5.5", "·", "#Codex", "gpt-5.5", "gpt-5.5-codex", "·", "Plus de modèles", "Effort"] and m3[2:] == ["#Codex", "gpt-5.4", "Codex Mini"],
          "rule: the agent's own spellings (gpt-5.5, gpt-5.5-codex) — one line, its newest version first; « Plus de modèles » lists only the groups that have more", [f3, m3])
    f4, _ = rule([{"id": "a", "label": "Opus 5.5"}, {"id": "b", "label": "Sonnet 5.5"}])
    check("Plus de modèles" not in f4, "rule: nothing older, no « Plus de modèles » row", f4)

    # ---------------------------------------------------------------- another agent's model: a new chat (#128 behaviour kept)
    load("128-panneau")
    check(not any(r.get("hint") for r in menu()["rows"]), "panel: no hint until a model of the other agent is hovered")
    hover("GPT-5.5"); m = menu()
    g = next(r for r in m["rows"] if r.get("t") == "GPT-5.5")
    check(g["hint"] == "Passer à Codex ouvre un nouveau chat" and g["h"] == 47 and m["h"] == 331 + 17,
          "panel: hover on Codex's model — « Passer à Codex ouvre un nouveau chat » under its name (12/16, 1 px), the row 47 px", [g, m["h"]])
    load("128-panneau"); hover("Opus 5.5")
    check(not any(r.get("hint") for r in menu()["rows"]), "panel: a model of the same agent has no hint (same chat)")
    cdp.js(row_el("Haiku 5.5") + ".focus()"); cdp.call("Input.dispatchMouseEvent", type="mouseMoved", x=5, y=5); key("ArrowDown")
    check(active().startswith("GPT-5.5") and next(r for r in menu()["rows"] if r.get("t") == "GPT-5.5")["hint"], "panel: keyboard focus on Codex's model shows the hint too", active())
    n0 = len(posted(["setModel", "setAgent", "newChat"])); click_row("Opus 5.5")
    sent = posted(["setModel", "setAgent", "newChat"])[n0:]
    check(sent == [{"type": "setModel", "id": "opus", "agentId": "claude-code"}] and cdp.js("document.querySelectorAll('.um').length") == 1 and cdp.js("!document.querySelector('.menu')"),
          "panel: another Claude Code model — setModel to the same agent, the chat stays, menu closed", sent)
    cdp.js("document.querySelector('.model .m').click()"); time.sleep(.15)
    n0 = len(posted(["setModel", "setAgent", "newChat"])); click_row("GPT-5.5"); time.sleep(.2)
    sent = posted(["setModel", "setAgent", "newChat"])[n0:]
    check(sent == [{"type": "setModel", "id": "gpt-5.5", "agentId": "codex"}] and cdp.js("[document.querySelectorAll('.um').length, document.querySelector('.model .mn').textContent]") == [0, "GPT-5.5"],
          "panel: Codex's model — setModel with agent codex; the host opens a new chat (empty thread), the button names GPT-5.5", sent)
    load("128-panneau-vide"); hover("GPT-5.5")
    check(not any(r.get("hint") for r in menu()["rows"]) and menu()["h"] == 331, "panel, empty chat: no « nouveau chat » hint (nothing to leave)")
    load("128-codex-choisi"); hover("Sonnet 5.5"); m = menu()
    s = next(r for r in m["rows"] if r.get("t") == "Sonnet 5.5")
    check([r["t"] for r in m["rows"] if r.get("checked")] == ["GPT-5.5"] and s["hint"] == "Passer à Claude Code ouvre un nouveau chat", "Codex chosen: GPT-5.5 checked; Claude Code's models say they open a new chat", m["rows"])

    # ---------------------------------------------------------------- Codex not connected / not installed / error
    load("128-codex-non-connecte"); m = menu()
    heads = [r for r in m["rows"] if r["k"] == "head"]
    codex = [r for r in m["rows"] if r.get("t") in ("GPT-5.5", "GPT-5.5 mini")]
    check(heads[1]["t"] == "Codex · non connecté" and heads[1]["act"] == "Connecter" and heads[1]["h"] == 30, "not connected: « Codex · non connecté » + « Connecter », header 30 px (Paper)", heads)
    check(len(codex) == 2 and all(r["off"] and abs(r["op"] - .32) < .001 for r in codex), "not connected: its latest models stay listed, greyed (32 %), not choosable", codex)
    check(m["h"] == 337 and labels(m)[-2:] == ["Plus de modèles", "Effort"], "not connected: menu 300 × 337 (Paper 268E-0)", [m["h"], labels(m)])
    b = cdp.js("(()=>{const b=document.querySelector('.mm-act'),s=getComputedStyle(b),r=b.getBoundingClientRect(),m=document.querySelector('.menu.models').getBoundingClientRect();return [r.height,s.paddingLeft,s.borderTopLeftRadius,s.backgroundColor,s.color,s.fontSize,s.fontWeight,Math.round(m.right-1-6-r.right)]})()")
    check(b == [22, "8px", "6px", "rgb(62, 103, 255)", "rgb(255, 255, 255)", "12px", "500", 10], "not connected: Connecter 22 px, 0 8, radius 6, #3E67FF, 12 Medium white, 10 px from the edge (Paper)", b)
    shot("v2-codex-non-connecte"); r3 = side_by_side("v2-codex-non-connecte", "v2-codex-non-connecte")
    check(render_ok(r3, 400), "render: Codex not connected, the size and pixels of the Paper board (268E-0, 300 × 337)", r3)
    load("128-codex-non-connecte")
    n0 = len(posted(["setModel", "setAgent"])); cdp.js(row_el("GPT-5.5") + ".click()"); time.sleep(.1)
    check(posted(["setModel", "setAgent"])[n0:] == [] and cdp.js("!!document.querySelector('.menu')"), "not connected: a click on a greyed model does nothing")
    cdp.js(row_el("Haiku 5.5") + ".focus()"); key("ArrowDown"); a1 = active(); key("ArrowDown"); a2 = active(); key("ArrowDown"); a3 = active()
    check([a1, a2, a3] == ["Connecter Codex", "Plus de modèles", "Effort"], "not connected: arrows go Haiku → Connecter → Plus de modèles → Effort (greyed models skipped)", [a1, a2, a3])
    n0 = len(posted(["agent.connect"])); cdp.js("document.querySelector('.mm-act').click()"); time.sleep(.1)
    check(posted(["agent.connect"])[n0:] == [{"type": "agent.connect", "agentId": "codex", "methodId": "chatgpt"}] and cdp.js("!document.querySelector('.menu')"),
          "not connected: Connecter = the existing sign-in flow (agent.connect), the menu closes", posted(["agent.connect"])[n0:])
    # « Ton agent » (worker B): « Installer » opens the « Ton agent » screen (its strong button is the install's consent)
    for scen, word, act, msg, sent in (("128-codex-pas-installe", "Codex · pas installé", "Installer", "agents.setup.open", {"type": "agents.setup.open"}),
                                       ("128-codex-erreur", "Codex · erreur", "Réessayer", "agent.prepare", {"type": "agent.prepare", "agentId": "codex"})):
        load(scen); m = menu(); hd = [r for r in m["rows"] if r["k"] == "head"][1]
        n0 = len(posted([msg])); cdp.js("document.querySelector('.mm-act').click()"); time.sleep(.1)
        check(hd["t"] == word and hd["act"] == act and posted([msg])[n0:] == [sent], f"{scen}: « {word} » + « {act} » ({msg})", [hd, posted([msg])[n0:]])
    load("128-codex-pas-installe")
    check(labels(menu())[5:8] == ["·", "#Codex · pas installé", "·"], "not installed, no model known: its header only, no model invented", labels(menu()))

    # ---------------------------------------------------------------- effort (panel): its levels inside the menu, as before
    load("128-effort")
    e = cdp.js("(()=>{const m=document.querySelector('.menu');return {label:m.getAttribute('aria-label'),rows:Array.from(m.querySelectorAll('.mi .lb')).map(n=>n.textContent),w:m.getBoundingClientRect().width}})()")
    check(e["label"] == "Effort" and e["rows"] == ["Effort", "Faible", "Moyen", "Élevé", "X-Élevé", "Max"] and e["w"] == 214, "panel: Effort › opens its levels in the menu (‹ Effort, 214 px), as before", e)
    n0 = len(posted(["setEffort"])); click_row("Max")
    check(posted(["setEffort"])[n0:] == [{"type": "setEffort", "id": "max"}], "panel: a level chosen is told to the host (setEffort)")

    # ---------------------------------------------------------------- #129: models not known yet — the groups show, never empty
    for scen in ("128-modeles-inconnus", "c-128-modeles-inconnus"):
        load(scen); m = menu()
        check(m and labels(m) == ["#Claude Code", "Opus 5.5", "·", "#Codex", "Modèle par défaut"] and [r["t"] for r in m["rows"] if r.get("checked")] == ["Opus 5.5"],
              f"{scen}: no model said yet — each group names the model its agent will use (the button's « Opus 5.5 »), never empty", m and labels(m))
        n0 = len(posted(["setModel", "setAgent"])); click_row("Modèle par défaut")
        check(posted(["setModel", "setAgent"])[n0:] == [{"type": "setAgent", "id": "codex"}], f"{scen}: Codex's row tells the host (setAgent)")

    # ---------------------------------------------------------------- the comment bubble: the same menu, above its button, whole
    def geo(sub=False):
        return cdp.js("""(sub=>{const m=document.querySelector(sub?'.sub-menu':'.menu.models:not(.sub-menu)');if(!m)return null;const r=m.getBoundingClientRect(),a=document.getElementById('app').getBoundingClientRect(),
          b=document.querySelector('.model .m').getBoundingClientRect(),ask=window.__posted.filter(x=>x.type==='bubble.size').pop()||{},room=window.__lastRoom||null;
          const rows=Array.from(m.querySelectorAll('.mi')).map(n=>{const q=n.getBoundingClientRect();return [n.querySelector('.lb').textContent,q.top>=r.top-.5&&q.bottom<=r.bottom+.5&&n.getClientRects().length>0]});
          return {top:r.top-a.top,bottom:r.bottom-a.top,left:r.left-a.left,right:r.right-a.left,w:r.width,h:r.height,aW:a.width,aH:a.height,bTop:b.top-a.top,bBottom:b.bottom-a.top,bRight:b.right-a.left,
            scroll:m.scrollHeight-m.clientHeight,rows,ask,inset:window.__inset}})(""" + ("true" if sub else "false") + ")")
    load("c-128-bulle"); m = menu(); g = geo()
    check(m and labels(m) == MAIN and m["w"] == 300 and m["h"] == 331, "bubble: the same menu as the panel (300 × 331)", m and [labels(m), m["w"], m["h"]])
    check(g["bottom"] <= g["bTop"] - 4 and abs(g["right"] - g["bRight"]) <= 1 and g["scroll"] <= 0 and (g["ask"].get("overflowTop") or 0) >= -g["top"],
          "bubble: the menu opens above its model button, right edges aligned, whole; the host is asked the room above (overflowTop)", g)
    hover("Plus de modèles"); cdp.js("document.activeElement&&document.activeElement.blur()")
    shot("v2-bulle-plus-survole", blur=False); r4 = side_by_side("v2-bulle-plus-survole", "v2-menu-plus-survole")
    check(render_ok(r4, 400), "render: the bubble's menu, « Plus de modèles » hovered — the Paper board's (265R-0)", r4)
    load("c-128-bulle"); s0 = cdp.js("window.__posted.filter(m=>m.type==='bubble.size').slice(-1)[0]"); hover("GPT-5.5"); time.sleep(.2)
    m = menu(); s1 = cdp.js("window.__posted.filter(m=>m.type==='bubble.size').slice(-1)[0]")
    g = next(r for r in m["rows"] if r.get("t") == "GPT-5.5")
    check(g["hint"] == "Passer à Codex ouvre un nouveau chat" and m["h"] == 348 and (s1.get("overflowTop") or 0) - (s0.get("overflowTop") or 0) == 17,
          "bubble: hover on Codex's model — the hint, the menu grows upward by 17 and the host is asked 17 px more above", [g, m["h"], s0, s1])
    n0 = len(posted(["setModel", "setAgent"])); click_row("GPT-5.5"); time.sleep(.2)
    check(posted(["setModel", "setAgent"])[n0:] == [{"type": "setModel", "id": "gpt-5.5", "agentId": "codex"}] and cdp.js("document.querySelectorAll('.bt, .um').length") == 0,
          "bubble: Codex's model — setModel to codex; the host opens a new chat on the element", posted(["setModel", "setAgent"])[n0:])
    load("c-128-bulle-non-connecte"); hd = [r for r in menu()["rows"] if r["k"] == "head"][1]
    check(hd["t"] == "Codex · non connecté" and hd["act"] == "Connecter" and all(r["off"] for r in menu()["rows"] if r.get("t", "").startswith("GPT")), "bubble: Codex not connected — header, Connecter, greyed models", hd)
    # « Plus de modèles » in the bubble: a 214 px sub-menu beside the menu (right, else left), tops aligned, grouped the same way
    load("c-128-bulle-plus"); s = menu(".sub-menu"); g = geo(True); gm = geo()
    check(s and labels(s) == MORE and s["w"] == 214 and s["label"] == "Plus de modèles" and abs(g["top"] - gm["top"]) <= .5 and abs(g["left"] - gm["right"] - 4) <= .5 and g["scroll"] <= 0,
          "bubble: « Plus de modèles › » opens beside the menu (4 px, tops aligned, 214 px), the other models by agent, whole", s and [labels(s), g, gm])
    check(active() == "Opus 5" and cdp.js("document.querySelector('.mi[data-key=more]').getAttribute('aria-expanded')") == "true", "bubble: the focus in the sub-menu, its row says expanded", active())
    shot("v2-bulle-plus-sous-menu", ".sub-menu")
    load("c-128-bulle-plus"); key("ArrowLeft")
    check(not cdp.js("!!document.querySelector('.sub-menu')") and cdp.js("document.activeElement.dataset.key") == "more", "bubble: ← closes the sub-menu, the focus back on « Plus de modèles »")
    key("ArrowRight")
    check(cdp.js("!!document.querySelector('.sub-menu') && !!document.activeElement.closest('.sub-menu')"), "bubble: → on « Plus de modèles » opens it again, the focus inside")
    key("Escape")
    check(not cdp.js("!!document.querySelector('.sub-menu')") and cdp.js("!!document.querySelector('.menu.models')"), "bubble: Échap closes the sub-menu only")
    key("Escape")
    check(cdp.js("!document.querySelector('.menu')") and cdp.js("document.activeElement === document.querySelector('.model .m')"), "bubble: Échap again closes the menu, the focus back on its button")
    load("c-128-bulle"); c = hover("Plus de modèles"); time.sleep(.3)
    hv = cdp.js("[!!document.querySelector('.sub-menu'), document.activeElement && document.activeElement.closest('.sub-menu') ? 1 : 0]")
    cdp.call("Input.dispatchMouseEvent", type="mousePressed", x=5, y=5, button="left", clickCount=1); cdp.call("Input.dispatchMouseEvent", type="mouseReleased", x=5, y=5, button="left", clickCount=1); time.sleep(.2)
    check(c and hv == [True, 0] and cdp.js("!document.querySelector('.menu')"), "bubble: hovering « Plus de modèles » opens it (the focus stays); a click outside closes both", hv)
    load("c-128-bulle-effort")
    sb = cdp.js("(()=>{const s=document.querySelector('.menu.sub-menu');if(!s)return null;const m=document.querySelector('.menu.models:not(.sub-menu)').getBoundingClientRect(),r=s.getBoundingClientRect();return {rows:Array.from(s.querySelectorAll('.mi .lb')).map(n=>n.textContent),gap:Math.round(r.left-m.right),top:Math.round(r.top-m.top),w:r.width}})()")
    check(sb and sb["rows"] == ["Faible", "Moyen", "Élevé", "X-Élevé", "Max"] and sb["gap"] == 4 and sb["top"] == 0 and sb["w"] == 214,
          "bubble: Effort › opens its sub-menu beside the menu (4 px, tops aligned, 214 px), as before", sb)

    # ---------------------------------------------------------------- the bubble at each edge of the page area: never cut
    # The host shows what floats around the surface within the room it measured (`layout.room`: to the page area's edges),
    # the window's margin (`layout.inset`, capped by the screen) and 400 px (MaxOverflowCss). The dev host plays an edge with
    # both (dev-host.js EDGE). Each menu must lie whole inside that room, every row up to « Effort » inside it, nothing
    # scrolled, and the overflow asked must cover it (else the host would not show it).
    EDGES = {"haut": {"top": 8, "right": 700, "bottom": 620, "left": 300}, "bas": {"top": 620, "right": 700, "bottom": 8, "left": 300},
             "gauche": {"top": 300, "right": 900, "bottom": 300, "left": 8}, "droite": {"top": 300, "right": 8, "bottom": 300, "left": 900},
             "serre": {"top": 24, "right": 700, "bottom": 200, "left": 300}}
    def whole(g, room, what):
        lim = {k: min(room[k], g["inset"][k], 400) for k in room}
        ask = {k: g["ask"].get("overflow" + k.capitalize()) or 0 for k in room}
        inside = g["top"] >= -lim["top"] - .5 and g["bottom"] <= g["aH"] + lim["bottom"] + .5 and g["left"] >= -lim["left"] - .5 and g["right"] <= g["aW"] + lim["right"] + .5
        shown = g["top"] >= -ask["top"] - .5 and g["bottom"] <= g["aH"] + ask["bottom"] + .5 and g["left"] >= -ask["left"] - .5 and g["right"] <= g["aW"] + ask["right"] + .5
        rows = all(ok for _, ok in g["rows"])
        return inside and shown and rows and g["scroll"] <= 0, {"what": what, "box": [round(g["top"]), round(g["bottom"]), round(g["left"]), round(g["right"])], "surface": [g["aW"], g["aH"]], "room": lim, "ask": ask, "scroll": g["scroll"], "cut": [t for t, ok in g["rows"] if not ok]}
    for edge, room in EDGES.items():
        load("c-128-bord-" + edge); g = geo()
        ok, d = whole(g, room, "menu")
        side = "under" if g["top"] >= g["bBottom"] else "above" if g["bottom"] <= g["bTop"] else "over"
        rows = [t for t, _ in g["rows"]]
        exp_side = {"haut": "under", "bas": "above", "gauche": "above", "droite": "above", "serre": "over"}[edge]   # serré: neither side holds it — slid over its button, whole
        check(ok and side == exp_side and rows[-2:] == ["Plus de modèles", "Effort"] and len(rows) == 8,
              f"edge « {edge} »: the menu whole ({side} its button), its 8 rows up to « Effort » inside, nothing scrolled, inside the room the host shows", [side, d])
        shot("v2-bord-" + edge, "#app")   # the bubble, for the record (the menu around it is in the full-page render)
        open(os.path.join(OUT, "v2-bord-" + edge + "-page.png"), "wb").write(base64.b64decode(cdp.call("Page.captureScreenshot", format="png")["data"]))
        for sub in ("plus", "effort") if edge in ("droite", "bas") else ("plus",):
            load(f"c-128-bord-{edge}-{sub}"); gs = geo(True); gm = geo()
            if not gs: check(False, f"edge « {edge} »: « {sub} » sub-menu opens", "none"); continue
            ok, d = whole(gs, room, "sub-menu " + sub)
            okm, dm = whole(gm, room, "menu")
            lr = "left" if gs["right"] <= gm["left"] + .5 else "right" if gs["left"] >= gm["right"] - .5 else "over"
            exp_lr = "left" if edge == "droite" else "right"
            check(ok and okm and lr == exp_lr, f"edge « {edge} »: the « {sub} » sub-menu whole, on the {lr} of the menu, both inside the room the host shows", [lr, d, dm])
            if sub == "plus": open(os.path.join(OUT, f"v2-bord-{edge}-plus-page.png"), "wb").write(base64.b64decode(cdp.call("Page.captureScreenshot", format="png")["data"]))
finally:
    try: srv.shutdown()
    except Exception: pass
    kill_tree(proc)

print(f"\n{count[0] - len(failures)} / {count[0]} checks — renders in {OUT}")
sys.exit(1 if failures else 0)
