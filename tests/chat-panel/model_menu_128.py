"""#128 — the model menu as one list grouped by provider (Paper « #128 Menu des modèles · A liste groupée (validée) »),
in the chat panel and in the comment bubble. Headless, no window: the panel (assets/chat-panel) in the WebView2 runtime's
Chromium (EdgeCore), driven over DevTools with a throw-away profile; pages served on 127.0.0.1; the fake host
(dev-host.js) plays the agents. Nothing touches the network, a real agent or a user profile.

  python -I tests/chat-panel/model_menu_128.py [--edge <msedge.exe>] [--out build/chat-qa/128]

Checks: the list comes from the agents (headers, models, order, nothing hard-coded); a provider not ready is listed with
its state and action (Connecter / Installer / Réessayer: the existing flows) and its models greyed out, not choosable;
the effort stays the last row and opens its sub-menu; another provider's model says it opens a new chat (only when the
chat has messages) and choosing it tells the host, which opens the new chat; the same provider's model keeps the chat;
every Paper value (sizes, paddings, radii, type, colours) measured in the page. Renders: <out>/<name>.png (the menu) and
<out>/cote-a-cote-<name>.png (Paper | page | differences), from tests/chat-panel/reference/128 (Paper exports, 1x).
Exit code 0 when every check passes.
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
check(not re.search(r"GPT|gpt-|Sonnet|Haiku|Opus|o3|o4-mini", menu_src), "static: the model menu writes no model name (the list comes from the agents)")
check("PER_GROUP" not in js and "Autres modèles" not in menu_src, "static: every model in the list, no « Autres modèles » cap any more")

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
    def menu():   # the open model menu, read in the page
        return cdp.js("""(()=>{const m=document.querySelector('.menu.models');if(!m)return null;const r=m.getBoundingClientRect(),cs=getComputedStyle(m);
          const rows=[];for(const n of m.children){const b=n.getBoundingClientRect(),s=getComputedStyle(n);
            if(n.classList.contains('mm-head'))rows.push({k:'head',t:n.querySelector('.mm-hn').textContent,h:b.height,act:n.querySelector('.mm-act')?n.querySelector('.mm-act').textContent:'',logo:!!n.querySelector('svg.mm-logo')});
            else if(n.classList.contains('sep'))rows.push({k:'sep'});
            else if(n.classList.contains('mi'))rows.push({k:n.classList.contains('mm-eff')?'effort':'row',t:n.querySelector('.lb').textContent,h:b.height,tag:(n.querySelector('.tag')||{}).textContent||'',
              val:(n.querySelector('.val')||{}).textContent||'',checked:n.getAttribute('aria-checked')==='true',off:n.getAttribute('aria-disabled')==='true',op:+s.opacity,
              hint:(()=>{const x=n.querySelector('.mm-hint');return x&&x.getClientRects().length?x.textContent:''})()});}
          return {w:r.width,h:r.height,x:r.left,y:r.top,right:r.right,bottom:r.bottom,radius:cs.borderTopLeftRadius,pad:cs.paddingTop,bg:cs.backgroundColor,border:cs.borderTopColor,shadow:cs.boxShadow,rows,label:m.getAttribute('aria-label')}})()""")
    labels = lambda mm: [("·" if r["k"] == "sep" else ("#" + r["t"]) if r["k"] == "head" else r["t"]) for r in mm["rows"]]
    def posted(types): return cdp.js(f"window.__posted.filter(m=>{json.dumps(types)}.includes(m.type)).map(m=>{{const o=Object.assign({{}},m);delete o.v;return o}})")
    def center(sel): return cdp.js(f"(()=>{{const n=Array.from(document.querySelectorAll('.menu.models .mi')).find(b=>b.querySelector('.lb').textContent==={json.dumps(sel)});if(!n)return null;const r=n.getBoundingClientRect();return [r.left+r.width/2,r.top+r.height/2]}})()")
    def hover(label):
        c = center(label)
        if c: cdp.call("Input.dispatchMouseEvent", type="mouseMoved", x=c[0], y=c[1]); time.sleep(.15)
        return c
    def click_row(label):
        cdp.js(f"Array.from(document.querySelectorAll('.menu.models .mi')).find(b=>b.querySelector('.lb').textContent==={json.dumps(label)}).click()"); time.sleep(.15)
    def key(k):
        kc = {"ArrowDown": 40, "ArrowUp": 38, "Enter": 13, "Escape": 27, "ArrowRight": 39, "ArrowLeft": 37}[k]
        for t in ("rawKeyDown", "keyUp"): cdp.call("Input.dispatchKeyEvent", type=t, key=k, code=k, windowsVirtualKeyCode=kc)
        time.sleep(.08)
    active = lambda: cdp.js("(()=>{const a=document.activeElement;return a?(a.getAttribute('aria-label')||(a.querySelector&&a.querySelector('.lb')?a.querySelector('.lb').textContent:a.textContent)).trim():''})()")
    def shot(name, pad=0):   # the menu alone; the focus ring of the scenario's scripted click is not a user's (a real click shows none)
        cdp.js("document.activeElement&&document.activeElement.blur()"); time.sleep(.05)
        r = cdp.js("(()=>{const m=document.querySelector('.menu.models');const r=m.getBoundingClientRect();return {x:r.left,y:r.top,width:r.width,height:r.height}})()")
        clip = {"x": r["x"] - pad, "y": r["y"] - pad, "width": r["width"] + pad * 2, "height": r["height"] + pad * 2, "scale": 1}
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

    W90, W50, W60 = "rgba(255, 255, 255, 0.9)", "rgba(255, 255, 255, 0.5)", "rgba(255, 255, 255, 0.6)"
    PAPER_PANEL = ["#Claude Code", "Opus 5.5", "Sonnet 5.5", "Haiku 5.5", "·", "#Codex", "GPT-5.5", "GPT-5.5 mini", "·", "Effort"]

    # ---------------------------------------------------------------- panel: the grouped list, from the agents
    load("128-panneau")
    m = menu()
    check(m and labels(m) == PAPER_PANEL, "panel: one list — Claude Code's models, then Codex's, then Effort (as the board)", m and labels(m))
    check(m and [r["tag"] for r in m["rows"] if r["k"] == "row"] == ["", "Par défaut", "", "", ""], "panel: « Par défaut » on the agent's default model only", m and m["rows"])
    check(m and [r["t"] for r in m["rows"] if r.get("checked")] == ["Sonnet 5.5"], "panel: the chosen model is checked", m and m["rows"])
    check(m and m["rows"][-1]["k"] == "effort" and m["rows"][-1]["val"] == "Élevé", "panel: the effort stays the last row, with its level", m and m["rows"][-1])
    check(m and all(r["logo"] for r in m["rows"] if r["k"] == "head"), "panel: each group header has its provider's logo")
    check(m and m["w"] == 280 and m["radius"] == "8px" and m["pad"] == "4px" and m["bg"] == "rgb(36, 36, 36)" and m["border"] == "rgba(255, 255, 255, 0.08)" and m["shadow"] == "none",
          "panel: menu 280 px, radius 8, padding 4, #242424, border white 8 %, no shadow (Paper)", m and {k: m[k] for k in ("w", "radius", "pad", "bg", "border", "shadow")})
    check(m and [r["h"] for r in m["rows"] if r["k"] in ("head", "row", "effort")] == [28, 32, 32, 32, 28, 32, 32, 32], "panel: headers 28 px, rows 32 px (Paper)", m and [r.get("h") for r in m["rows"]])
    v = cdp.js("""(()=>{const m=document.querySelector('.menu.models');const q=(s)=>m.querySelector(s);const cs=(n)=>n?getComputedStyle(n):{};
      const row=Array.from(m.querySelectorAll('.mi.mm-row')).find(b=>b.getAttribute('aria-checked')==='true'),lb=row.querySelector('.lb'),tag=row.querySelector('.tag'),ck=row.querySelector('.ck'),head=q('.mm-head'),hn=q('.mm-hn'),logo=q('.mm-logo'),sep=q('.sep'),eff=q('.mi.mm-eff'),val=eff.querySelector('.val'),sub=eff.querySelector('.sub');
      const r=(n)=>n.getBoundingClientRect();
      return {row:[cs(row).paddingLeft,cs(row).paddingRight,cs(row).borderTopLeftRadius,cs(row).backgroundColor],lb:[cs(lb).fontSize,cs(lb).lineHeight,cs(lb).fontWeight,cs(lb).color],
        tag:[cs(tag).fontSize,cs(tag).lineHeight,cs(tag).fontWeight,cs(tag).color,cs(tag).backgroundColor,cs(tag).borderTopLeftRadius,cs(tag).paddingTop,cs(tag).paddingLeft,Math.round(r(tag).left-r(lb).right)],
        ck:[r(ck).width,cs(ck).color,Math.round(r(row).right-r(ck).right)],head:[cs(head).paddingLeft,Math.round(r(hn).left-r(logo).right),r(logo).width],hn:[cs(hn).fontSize,cs(hn).lineHeight,cs(hn).fontWeight,cs(hn).color],
        sep:[r(sep).height,cs(sep).backgroundColor,cs(sep).marginTop,cs(sep).marginLeft],eff:[cs(eff.querySelector('.lb')).fontSize,cs(eff.querySelector('.lb')).color,cs(val).fontSize,cs(val).lineHeight,cs(val).color,r(sub).width,Math.round(r(sub).left-r(val).right)]}})()""")
    exp = {"row": ["8px", "8px", "6px", "rgba(255, 255, 255, 0.06)"], "lb": ["14px", "20px", "400", W90],
           "tag": ["11px", "14px", "400", W60, "rgba(255, 255, 255, 0.08)", "4px", "1px", "5px", 8], "ck": [14, "rgb(143, 169, 255)", 8],
           "head": ["8px", 8, 14], "hn": ["12px", "16px", "500", W50], "sep": [1, "rgba(255, 255, 255, 0.06)", "4px", "8px"],
           "eff": ["14px", W90, "13px", "18px", W50, 12, 6]}
    for k in exp: check(v[k] == exp[k], f"panel: Paper values — {k}", f"page {v[k]} / Paper {exp[k]}")
    # no hint while the pointer is elsewhere; on hover of the other provider's model: the hint, 44 px, white 8 %
    check(not any(r.get("hint") for r in m["rows"]), "panel: no hint until a model of the other provider is hovered")
    hover("GPT-5.5"); m = menu()
    g = next(r for r in m["rows"] if r.get("t") == "GPT-5.5")
    bg = cdp.js("getComputedStyle(Array.from(document.querySelectorAll('.menu.models .mi')).find(b=>b.querySelector('.lb').textContent==='GPT-5.5')).backgroundColor")
    check(g["hint"] == "Passer à Codex ouvre un nouveau chat" and g["h"] == 44 and bg == "rgba(255, 255, 255, 0.08)" and m["h"] == 288,
          "panel: hover on Codex's model — « Passer à Codex ouvre un nouveau chat », row 44 px, white 8 %, menu 288 px (Paper)", [g, bg, m["h"]])
    shot("128-panneau-survol"); r1 = side_by_side("128-panneau-survol", "panneau-survol")
    check(r1["sameSize"], "render: panel menu (hover) the size of the Paper board's (280 × 288)", r1)
    load("128-panneau"); hover("Opus 5.5"); m = menu()
    check(not any(r.get("hint") for r in m["rows"]), "panel: a model of the same provider has no hint (same chat)")
    # keyboard focus says it too
    cdp.js("Array.from(document.querySelectorAll('.menu.models .mi')).find(b=>b.querySelector('.lb').textContent==='Haiku 5.5').focus()")
    cdp.call("Input.dispatchMouseEvent", type="mouseMoved", x=5, y=5); key("ArrowDown"); m = menu()
    check(active().startswith("GPT-5.5") and next(r for r in m["rows"] if r.get("t") == "GPT-5.5")["hint"], "panel: keyboard focus on Codex's model shows the hint too", active())
    # same provider: the same chat
    n0 = len(posted(["setModel", "setAgent", "newChat"])); click_row("Opus 5.5")
    sent = posted(["setModel", "setAgent", "newChat"])[n0:]
    kept = cdp.js("document.querySelectorAll('.um').length")
    check(sent == [{"type": "setModel", "id": "opus", "agentId": "claude-code"}] and kept == 1 and cdp.js("!document.querySelector('.menu')"),
          "panel: another Claude Code model — setModel to the same agent, the chat stays (its message still shown), menu closed", [sent, kept])
    # another provider: the host opens a new chat
    cdp.js("document.querySelector('.model .m').click()"); time.sleep(.15)
    n0 = len(posted(["setModel", "setAgent", "newChat"])); click_row("GPT-5.5"); time.sleep(.2)
    sent = posted(["setModel", "setAgent", "newChat"])[n0:]
    after = cdp.js("[document.querySelectorAll('.um').length, document.querySelector('.model .mn').textContent]")
    check(sent == [{"type": "setModel", "id": "gpt-5.5", "agentId": "codex"}] and after == [0, "GPT-5.5"],
          "panel: Codex's model — setModel with agent codex; the host opens a new chat (empty thread), the button names GPT-5.5", [sent, after])

    # the list follows what the agents say (other names, other counts, no Codex model at all)
    load("128-panneau")
    cdp.js("""window.devHost.send({type:'state',agents:[{id:'claude-code',name:'Claude Code',icon:'claude',status:'ready',models:[{id:'a',label:'Modèle A'},{id:'b',label:'Modèle B',isDefault:true}]},
      {id:'codex',name:'Codex',icon:'codex',status:'ready',models:[{id:'x',label:'X 1'},{id:'y',label:'X 2'},{id:'z',label:'X 3'},{id:'w',label:'X 4'},{id:'v',label:'X 5'}]}],agentId:'claude-code',model:'b'})""")
    time.sleep(.1); key("Escape"); cdp.js("document.querySelector('.model .m').click()"); time.sleep(.15); m = menu()
    check(m and labels(m) == ["#Claude Code", "Modèle A", "Modèle B", "·", "#Codex", "X 1", "X 2", "X 3", "X 4", "X 5", "·", "Effort"],
          "panel: the list is whatever the agents say — all of them, in their order, no cap", m and labels(m))

    # empty chat: no hint (switching only changes the agent)
    load("128-panneau-vide"); hover("GPT-5.5"); m = menu()
    check(m and not any(r.get("hint") for r in m["rows"]) and m["h"] == 276, "panel, empty chat: no « nouveau chat » hint (nothing to leave), menu 276 px", m and [labels(m), m["h"]])

    # ---------------------------------------------------------------- Codex not connected / not installed / error
    load("128-codex-non-connecte"); m = menu()
    heads = [r for r in m["rows"] if r["k"] == "head"]
    codex = [r for r in m["rows"] if r.get("t") in ("GPT-5.5", "GPT-5.5 mini")]
    check(heads[1]["t"] == "Codex · non connecté" and heads[1]["act"] == "Connecter" and heads[1]["h"] == 32, "not connected: « Codex · non connecté » + « Connecter », header 32 px (Paper)", heads)
    check(len(codex) == 2 and all(r["off"] and abs(r["op"] - .32) < .001 for r in codex), "not connected: its models stay listed, greyed (32 %), not choosable", codex)
    check(m["h"] == 280, "not connected: menu 280 × 280 (Paper)", m["h"])
    b = cdp.js("(()=>{const b=document.querySelector('.mm-act'),s=getComputedStyle(b),r=b.getBoundingClientRect(),m=document.querySelector('.menu.models').getBoundingClientRect();return [r.height,s.paddingLeft,s.borderTopLeftRadius,s.backgroundColor,s.color,s.fontSize,s.fontWeight,Math.round(m.right-1-4-r.right)]})()")
    check(b == [24, "8px", "6px", "rgb(62, 103, 255)", "rgb(255, 255, 255)", "12px", "500", 4], "not connected: Connecter 24 px, 0 8, radius 6, #3E67FF, 12 Medium white, 4 px from the edge (Paper)", b)
    shot("128-codex-non-connecte"); r2 = side_by_side("128-codex-non-connecte", "codex-non-connecte")
    check(r2["sameSize"], "render: not-connected menu the size of the Paper board's (280 × 280)", r2)
    load("128-codex-non-connecte")
    n0 = len(posted(["setModel", "setAgent"])); cdp.js("Array.from(document.querySelectorAll('.menu.models .mi')).find(b=>b.querySelector('.lb').textContent==='GPT-5.5').click()"); time.sleep(.1)
    check(posted(["setModel", "setAgent"])[n0:] == [] and cdp.js("!!document.querySelector('.menu')"), "not connected: a click on a greyed model does nothing")
    # keyboard: arrows skip the greyed models, reach Connecter
    cdp.js("Array.from(document.querySelectorAll('.menu.models .mi')).find(b=>b.querySelector('.lb').textContent==='Haiku 5.5').focus()")
    key("ArrowDown"); a1 = active(); key("ArrowDown"); a2 = active()
    check(a1 == "Connecter Codex" and a2 == "Effort", "not connected: arrows go Haiku → Connecter → Effort (greyed models skipped)", [a1, a2])
    n0 = len(posted(["agent.connect"])); cdp.js("document.querySelector('.mm-act').click()"); time.sleep(.1)
    check(posted(["agent.connect"])[n0:] == [{"type": "agent.connect", "agentId": "codex", "methodId": "chatgpt"}] and cdp.js("!document.querySelector('.menu')"),
          "not connected: Connecter = the existing sign-in flow (agent.connect), the menu closes", posted(["agent.connect"])[n0:])
    # « Ton agent » (worker B): « Installer » opens the « Ton agent » screen (its strong button is the install's consent)
    for scen, word, act, msg, sent in (("128-codex-pas-installe", "Codex · pas installé", "Installer", "agents.setup.open", {"type": "agents.setup.open"}),
                                       ("128-codex-erreur", "Codex · erreur", "Réessayer", "agent.prepare", {"type": "agent.prepare", "agentId": "codex"})):
        load(scen); m = menu(); hd = [r for r in m["rows"] if r["k"] == "head"][1]
        n0 = len(posted([msg])); cdp.js("document.querySelector('.mm-act').click()"); time.sleep(.1)
        check(hd["t"] == word and hd["act"] == act and posted([msg])[n0:] == [sent], f"{scen}: « {word} » + « {act} » ({msg})", [hd, posted([msg])[n0:]])
    load("128-codex-pas-installe"); m = menu()
    check(labels(m)[4:7] == ["·", "#Codex · pas installé", "·"], "not installed, no model known: its header only, no model invented", labels(m))

    # ---------------------------------------------------------------- effort: the last row, its sub-menu
    load("128-effort")
    e = cdp.js("(()=>{const m=document.querySelector('.menu');return {label:m.getAttribute('aria-label'),rows:Array.from(m.querySelectorAll('.mi .lb')).map(n=>n.textContent),mm:m.classList.contains('mm-list')}})()")
    check(e["label"] == "Effort" and e["rows"] == ["Effort", "Faible", "Moyen", "Élevé", "X-Élevé", "Max"] and not e["mm"], "panel: Effort › opens its levels in the menu (‹ Effort), as before", e)
    n0 = len(posted(["setEffort"])); cdp.js("Array.from(document.querySelectorAll('.menu .mi')).find(b=>b.querySelector('.lb').textContent==='Max').click()"); time.sleep(.1)
    check(posted(["setEffort"])[n0:] == [{"type": "setEffort", "id": "max"}], "panel: a level chosen is told to the host (setEffort)")
    load("128-effort"); cdp.js("document.querySelector('.menu .mi.back').click()"); time.sleep(.1); m = menu()
    check(m and labels(m) == PAPER_PANEL and m["w"] == 280, "panel: ‹ Effort goes back to the grouped list", m and labels(m))

    # ---------------------------------------------------------------- #129 in the new list: models not known yet
    for scen in ("128-modeles-inconnus", "c-128-modeles-inconnus"):
        load(scen); m = menu()
        check(m and labels(m) == ["#Claude Code", "Opus 5.5", "·", "#Codex", "Modèle par défaut"] and [r["t"] for r in m["rows"] if r.get("checked")] == ["Opus 5.5"],
              f"{scen}: no model said yet — each group names the model its agent will use (the button's « Opus 5.5 »), never empty", m and labels(m))
        n0 = len(posted(["setModel", "setAgent"])); click_row("Modèle par défaut")
        check(posted(["setModel", "setAgent"])[n0:] == [{"type": "setAgent", "id": "codex"}], f"{scen}: Codex's row tells the host (setAgent)")

    # ---------------------------------------------------------------- the comment bubble: the same menu, above its model
    load("c-128-bulle"); m = menu()
    geo = cdp.js("""(()=>{const a=document.getElementById('app').getBoundingClientRect(),b=document.querySelector('.model .m').getBoundingClientRect(),m=document.querySelector('.menu.models').getBoundingClientRect();
      return {aTop:a.top,aRight:a.right,bTop:b.top,bRight:b.right,mBottom:m.bottom,mRight:m.right,mTop:m.top,winH:innerHeight,winW:innerWidth}})()""")
    check(m and labels(m) == PAPER_PANEL and m["w"] == 280, "bubble: the same grouped list, 280 px", m and [labels(m), m["w"]])
    check(m and [r["t"] for r in m["rows"] if r.get("checked")] == ["Sonnet 5.5"] and m["h"] == 276, "bubble: Sonnet 5.5 checked, menu 280 × 276 (Paper)", m and m["h"])
    check(geo["mBottom"] <= geo["bTop"] - 4 and geo["mTop"] >= 0 and abs(geo["mRight"] - geo["bRight"]) <= 1, "bubble: the menu opens above its model button, whole, right edges aligned", geo)
    shot("c-128-bulle"); r3 = side_by_side("c-128-bulle", "bulle")
    check(r3["sameSize"], "render: bubble menu the size of the Paper board's (280 × 276)", r3)
    load("c-128-bulle"); last = lambda: cdp.js("window.__posted.filter(m=>m.type==='bubble.size').slice(-1)[0]")
    s0 = last(); hover("GPT-5.5"); time.sleep(.2); m = menu(); s1 = last()
    g = next(r for r in m["rows"] if r.get("t") == "GPT-5.5")
    inside = cdp.js("(()=>{const m=document.querySelector('.menu.models').getBoundingClientRect();return m.top>=0&&m.bottom<=innerHeight})()")
    check(g["hint"] == "Passer à Codex ouvre un nouveau chat" and m["h"] == 288 and (s1.get("overflowTop") or 0) - (s0.get("overflowTop") or 0) == 12 and inside,
          "bubble: hover on Codex's model — the hint, the menu grows upward by 12 (288) and the host is asked 12 px more above (overflowTop)", [g, m["h"], s0, s1])
    n0 = len(posted(["setModel", "setAgent"])); click_row("GPT-5.5"); time.sleep(.2)
    check(posted(["setModel", "setAgent"])[n0:] == [{"type": "setModel", "id": "gpt-5.5", "agentId": "codex"}] and cdp.js("document.querySelectorAll('.bt, .um').length") == 0,
          "bubble: Codex's model — setModel to codex; the host opens a new chat on the element (empty thread)", posted(["setModel", "setAgent"])[n0:])
    load("c-128-bulle-vide"); hover("GPT-5.5"); m = menu()
    check(not any(r.get("hint") for r in m["rows"]), "bubble, nothing sent yet: no « nouveau chat » hint")
    load("c-128-bulle-non-connecte"); m = menu()
    hd = [r for r in m["rows"] if r["k"] == "head"][1]
    check(hd["t"] == "Codex · non connecté" and hd["act"] == "Connecter" and all(r["off"] for r in m["rows"] if r.get("t", "").startswith("GPT")), "bubble: Codex not connected — header, Connecter, greyed models", hd)
    load("c-128-bulle-effort")
    sb = cdp.js("(()=>{const s=document.querySelector('.menu.sub-menu');if(!s)return null;const m=document.querySelector('.menu.mm-list').getBoundingClientRect(),r=s.getBoundingClientRect();return {rows:Array.from(s.querySelectorAll('.mi .lb')).map(n=>n.textContent),gap:Math.round(r.left-m.right),top:Math.round(r.top-m.top),w:r.width}})()")
    check(sb and sb["rows"] == ["Faible", "Moyen", "Élevé", "X-Élevé", "Max"] and sb["gap"] == 4 and sb["top"] == 0 and sb["w"] == 214,
          "bubble: Effort › opens its sub-menu beside the list (4 px, tops aligned, 214 px), as before", sb)

    # ---------------------------------------------------------------- Codex chosen: Claude Code's models are now « the other provider's »
    load("128-codex-choisi"); hover("Sonnet 5.5"); m = menu()
    s = next(r for r in m["rows"] if r.get("t") == "Sonnet 5.5")
    check([r["t"] for r in m["rows"] if r.get("checked")] == ["GPT-5.5"] and s["hint"] == "Passer à Claude Code ouvre un nouveau chat", "Codex chosen: GPT-5.5 checked; Claude Code's models say they open a new chat", [m["rows"]])
finally:
    try: srv.shutdown()
    except Exception: pass
    kill_tree(proc)

print(f"\n{count[0] - len(failures)} / {count[0]} checks — renders in {OUT}")
sys.exit(1 if failures else 0)
