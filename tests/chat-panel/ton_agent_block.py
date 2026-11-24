"""« Ton agent » (worker B) — the chat's first-message block while no agent is ready (Paper « Onboarding agents — Ton agent
(maquette prête) » › 22N3-0 « Chat — premier message, rien n'est prêt »), in the chat panel and in the comment bubble.
Headless, no window: the panel (assets/chat-panel) in the WebView2 runtime's Chromium (EdgeCore), driven over DevTools with
a throw-away profile; pages served on 127.0.0.1; the fake host (dev-host.js, scenarios « ta-* ») sends `agents.setup` as
src/chat-ton-agent.h does. Nothing touches the network, a real agent, an installer or a user profile.

  python -I tests/chat-panel/ton_agent_block.py [--edge <msedge.exe>] [--out build/chat-qa/ton-agent]

Checks: the block in the place of « Que voulez-vous changer ? » on an empty chat (not over a thread, not once an agent is
ready); its rows (logo, name, the status dot and words, the button: strong for install / retry, raised for sign-in); each
button tells the host (`agents.setup.act`) — a busy one does not; after a failed install the two lines and « Installer
moi-même ↗ » (`agents.setup.manual`); no duplicate « pas installé » strip; the strip / menu « Installer » of a missing agent
opens the « Ton agent » screen (`agents.setup.open`); the bubble shows the same block and asks for its height; the Paper
values measured in the page; renders vs the Paper export (design/ton-agent-evidence/paper/chat-first-message@96.png).
Exit code 0 when every check passes.
"""
import base64, glob, http.server, json, os, re, socket, struct, subprocess, sys, tempfile, threading, time, urllib.parse, urllib.request

sys.stdout.reconfigure(encoding="utf-8")
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
ASSETS = os.path.join(ROOT, "assets")
PAPER = os.path.join(ROOT, "design", "ton-agent-evidence", "paper")
args = sys.argv[1:]
opt = lambda n, d=None: args[args.index(n) + 1] if n in args else d
OUT = os.path.abspath(opt("--out", os.path.join(ROOT, "build", "chat-qa", "ton-agent")))
EDGE = opt("--edge") or next(iter(sorted(glob.glob(os.path.join(os.environ.get("LOCALAPPDATA", ""), "Microsoft", "EdgeCore", "*", "msedge.exe"))
                                         + glob.glob(os.path.join(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"), "Microsoft", "EdgeCore", "*", "msedge.exe")), reverse=True)), None)
os.makedirs(OUT, exist_ok=True)

failures, count = [], [0]
def check(ok, name, detail=""):
    count[0] += 1
    print(("ok   " if ok else "FAIL ") + name + (" — " + str(detail)[:600] if detail and not ok else ""))
    if not ok: failures.append(name)

class Handler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *a): pass
    def translate_path(self, path):
        p = urllib.parse.unquote(urllib.parse.urlsplit(path).path)
        return os.path.join(ASSETS, p.lstrip("/"))
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

profile = tempfile.mkdtemp(prefix="chat-ta-qa-")
proc = None
try:
    check(bool(EDGE and os.path.exists(EDGE)), "runtime: headless Chromium (EdgeCore) found", EDGE or "none")
    if not EDGE: raise SystemExit(1)
    proc = subprocess.Popen([EDGE, "--headless=new", "--disable-gpu", "--no-first-run", "--no-default-browser-check", "--disable-extensions", "--disable-sync",
                             "--disable-background-networking", "--disable-component-update", "--remote-debugging-port=0", "--user-data-dir=" + profile,
                             "--force-device-scale-factor=1", "--window-size=380,664", "about:blank"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    port_file = os.path.join(profile, "DevToolsActivePort")
    for _ in range(300):
        if os.path.exists(port_file) and open(port_file).read().strip(): break
        time.sleep(.1)
    port = open(port_file).read().split()[0]
    targets = json.load(urllib.request.urlopen(f"http://127.0.0.1:{port}/json/list"))
    cdp = Cdp([t for t in targets if t["type"] == "page"][0]["webSocketDebuggerUrl"])
    for d in ("Page", "Runtime", "Log"): cdp.call(d + ".enable")
    cdp.call("Emulation.setFocusEmulationEnabled", enabled=True)

    def load(name, w=380, h=664):
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
    def posted(types): return cdp.js(f"window.__posted.filter(m=>{json.dumps(types)}.includes(m.type)).map(m=>{{const o=Object.assign({{}},m);delete o.v;return o}})")
    def shot(name, clip):
        cdp.js("document.activeElement&&document.activeElement.blur()"); time.sleep(.05)
        path = os.path.join(OUT, name + ".png")
        open(path, "wb").write(base64.b64decode(cdp.call("Page.captureScreenshot", format="png", clip=dict(clip, scale=1))["data"]))
        return path
    def compare(mine, paper, box_mine, box_paper, out):
        try:
            from PIL import Image, ImageChops, ImageStat, ImageCms
        except Exception:
            print("     (PIL missing: no pixel comparison)"); return None
        import io as _io
        p = Image.open(paper); icc = p.info.get("icc_profile"); p = p.convert("RGB")
        if icc: p = ImageCms.profileToProfile(p, ImageCms.ImageCmsProfile(_io.BytesIO(icc)), ImageCms.createProfile("sRGB"), outputMode="RGB")
        a = p.crop(box_paper); b = Image.open(mine).convert("RGB").crop(box_mine)
        if a.size != b.size: print(f"     sizes differ: Paper {a.size} vs page {b.size}"); b = b.resize(a.size)
        d = ImageChops.difference(a, b); mean = sum(ImageStat.Stat(d).mean[:3]) / 3
        w, h = a.size; s = Image.new("RGB", (w * 3 + 16, h), (255, 0, 255)); s.paste(a, (0, 0)); s.paste(b, (w + 8, 0)); s.paste(d.point(lambda v: min(255, v * 4)), (2 * w + 16, 0))
        s = s.resize((s.width * 2, s.height * 2), Image.NEAREST); s.save(out)
        print(f"     render vs Paper: mean difference {mean:.2f} / 255 — {out}")
        return mean

    block = lambda: cdp.js("""(()=>{const t=document.querySelector('.tas');if(!t||t.hidden||!t.getClientRects().length)return null;const r=(n)=>{const b=n.getBoundingClientRect();return [Math.round(b.left),Math.round(b.top),Math.round(b.width),Math.round(b.height)]};
      return {title:(t.querySelector('.tas-h')||{}).textContent,sub:(t.querySelector('.tas-sub')||{}).textContent,note:(t.querySelector('.tas-note')||{}).textContent,
        rows:Array.from(t.querySelectorAll('.tas-row')).map(n=>{const st=n.querySelector('.tas-st'),b=n.querySelector('.tas-btn');return {name:n.querySelector('.tas-nm').textContent,status:st.lastChild.textContent,
          ink:['grey','accent','danger'].find(k=>st.classList.contains(k)),color:getComputedStyle(st).color,icon:!!st.querySelector('svg'),anim:!!st.querySelector('svg.tas-anim'),dot:!!n.querySelector('.tas-dot'),
          note:(n.querySelector('.tas-rn')||{}).textContent||'',link:(n.querySelector('.tas-link')||{}).textContent||'',btn:b?b.textContent:'',strong:!!(b&&b.classList.contains('strong')),logo:!!n.querySelector('.tas-logo svg'),box:r(n)}}),
        box:r(t),h2:r(t.querySelector('.tas-h')),noteBox:r(t.querySelector('.tas-note'))}})()""")
    row = lambda b, i: (b["rows"][i]["status"], b["rows"][i]["ink"], b["rows"][i]["icon"], b["rows"][i]["btn"], b["rows"][i]["strong"]) if b else None
    GREY, ACCENT, DANGER = "rgb(166, 166, 166)", "rgb(47, 209, 217)", "rgb(255, 123, 110)"
    # ---------------------------------------------------------------- panel: the board (Claude Code absent, Codex to sign in)
    load("ta-premier-message")
    b = block()
    check(b is not None and cdp.js("document.querySelector('.empty').hidden"), "panel: the block in the place of « Que voulez-vous changer ? » (empty chat, no agent ready)")
    check(b and b["title"] == "Choisissez votre agent" and b["sub"] == "Le chat utilise Claude Code ou Codex, installés sur cet ordinateur." and b["note"] == "Claude Code demande un compte Claude payant (Pro, Max, Team ou Enterprise).",
          "panel: the board's words (title, line, the paid Claude account)", b)
    check(b and row(b, 0) == ("Pas installé", "grey", True, "Installer", True) and row(b, 1) == ("Pas connecté", "grey", True, "Se connecter", True),
          "panel: the rows — ⤓ « Pas installé » + strong « Installer »; 📶 « Pas connecté » + strong « Se connecter » (one strong button, on the missing action)", b and b["rows"])
    check(b and not any(r["dot"] for r in b["rows"]) and all(r["logo"] for r in b["rows"]) and b["rows"][0]["color"] == GREY, "panel: no coloured dot — the state is an icon and its words (grey)", b and b["rows"])
    check(not cdp.js("Array.from(document.querySelectorAll('.dock .strip')).some(s=>/pas installé/.test(s.textContent))"), "panel: no « n'est pas installé » strip under it (the block says it)")
    v = cdp.js("""(()=>{const t=document.querySelector('.tas');const q=(s)=>t.querySelector(s);const c=(n)=>getComputedStyle(n);const card=q('.tas-card'),row=q('.tas-row'),logo=q('.tas-logo'),nm=q('.tas-nm'),st=q('.tas-st'),ic=q('.tas-st svg'),strong=q('.tas-btn.strong'),h=q('.tas-h'),sub=q('.tas-sub'),note=q('.tas-note');
      const b=(n)=>n.getBoundingClientRect();
      return {wrap:[c(t).paddingTop,c(t).paddingLeft,c(t).rowGap],h:[c(h).fontSize,c(h).lineHeight,c(h).color],sub:[c(sub).fontSize,c(sub).lineHeight,c(sub).color],note:[c(note).fontSize,c(note).lineHeight,c(note).color],
        card:[c(card).backgroundColor,c(card).borderTopColor,c(card).borderTopWidth,c(card).borderTopLeftRadius,c(card).paddingTop,c(card).rowGap],
        row:[c(row).paddingTop,c(row).borderTopLeftRadius,c(row).columnGap,Math.round(b(row).height)],logo:[b(logo).width,b(logo).height,c(logo).backgroundColor,c(logo).borderTopLeftRadius,b(logo.querySelector('svg')).width],
        nm:[c(nm).fontSize,c(nm).lineHeight,c(nm).fontWeight,c(nm).color],st:[c(st).fontSize,c(st).lineHeight,c(st).color,c(st).columnGap,b(ic).width],
        strong:[b(strong).height,c(strong).paddingLeft,c(strong).borderTopLeftRadius,c(strong).backgroundColor,c(strong).fontSize,c(strong).lineHeight,c(strong).fontWeight]}})()""")
    exp = {"wrap": ["16px", "12px", "12px"], "h": ["16px", "22px", "rgb(255, 255, 255)"], "sub": ["12px", "17px", "rgb(166, 166, 166)"], "note": ["12px", "17px", "rgb(140, 140, 140)"],
           "card": ["rgb(28, 28, 28)", "rgb(38, 38, 38)", "1px", "12px", "4px", "2px"], "row": ["10px", "10px", "10px", 56], "logo": [32, 32, "rgb(38, 38, 38)", "8px", 16],
           "nm": ["14px", "18px", "500", "rgb(255, 255, 255)"], "st": ["12px", "16px", GREY, "4px", 14],
           "strong": [28, "10px", "6px", "rgb(0, 153, 160)", "13px", "16px", "500"]}
    for k in exp: check(v[k] == exp[k], f"panel: Paper values — {k}", f"page {v[k]} / Paper {exp[k]}")
    # the render: the block, from its title to its note, against the board's (Paper 22N3-0: x 12 … 368, y 321 … 554, measured on
    # the export). The board still draws the old dots and Codex's neutral button: the Designer's states differ there (informative).
    if b:
        top, bottom = b["h2"][1], b["noteBox"][1] + b["noteBox"][3]
        check(bottom - top == 233, "panel: the block 233 high, as the board's (title … note; rows 56)", bottom - top)
        mine = shot("panneau-premier-message", {"x": 0, "y": 0, "width": 380, "height": 664})
        compare(mine, os.path.join(PAPER, "chat-first-message@96.png"), (12, top, 368, top + 233), (12, 321, 368, 554), os.path.join(OUT, "cote-a-cote-panneau.png"))
    n0 = len(posted(["agents.setup.act"]))
    cdp.js("Array.from(document.querySelectorAll('.tas-btn')).find(b=>b.textContent==='Installer').click()"); time.sleep(.1)
    cdp.js("Array.from(document.querySelectorAll('.tas-btn')).find(b=>b.textContent==='Se connecter').click()"); time.sleep(.1)
    check(posted(["agents.setup.act"])[n0:] == [{"type": "agents.setup.act", "agentId": "claude", "action": "install"}, {"type": "agents.setup.act", "agentId": "codex", "action": "signIn"}],
          "panel: « Installer » → install Claude Code (the click is the consent), « Se connecter » → sign in to Codex", posted(["agents.setup.act"]))
    check(cdp.js("Array.from(document.querySelectorAll('.tas-btn')).map(b=>b.getAttribute('aria-label'))") == ["Installer Claude Code", "Se connecter Codex"], "panel: the buttons name their agent for screen readers")

    # ---------------------------------------------------------------- installing: the morph, no button
    load("ta-installation"); b = block()
    check(b and row(b, 0) == ("Installation…", "accent", True, "", False) and b["rows"][0]["color"] == ACCENT and not b["rows"][0]["anim"], "installing: ⤓ « Installation… » in the accent, no button (reduced motion: the still icon)", b and b["rows"][0])
    shot("panneau-installation", {"x": 0, "y": 0, "width": 380, "height": 664})
    cdp.call("Emulation.setEmulatedMedia", features=[{"name": "prefers-reduced-motion", "value": "no-preference"}])
    cdp.call("Page.navigate", url=f"{BASE}/chat-panel/dev.html?scenario=ta-installation")
    cdp.js("new Promise(r=>{const f=()=>{if(window.__scenarioDone)r(true);else setTimeout(f,20)};f()})"); time.sleep(.5)
    m = cdp.js("""(()=>{const s=document.querySelector('.tas-row .tas-st svg.tas-anim');if(!s)return null;const g=s.querySelectorAll('animateTransform'),a=s.querySelector('animate');
      return {ring:a.getAttribute('values').split(';')[1].slice(0,12),dur:a.getAttribute('dur'),loop:g[1].getAttribute('dur'),bob:g[1].getAttribute('values'),repeat:g[1].getAttribute('repeatCount'),mask:!!s.querySelector('mask'),
        moving:(()=>{const g=s.querySelector('animateTransform[id$=a]').parentNode;const t=g.transform.animVal;return t.length?t[0].matrix.f:null})()}})()""")
    check(m and m["dur"] == "0.2333s" and m["loop"] == "1.3s" and m["bob"] == "0 30;0 42;0 30" and m["repeat"] == "indefinite" and m["mask"] and m["ring"].startswith("M128 40"),
          "installing: the Downloads morph — tray → ring in 233 ms, the arrow bobbing 12 units every 1.3 s, a band cut around it", m)
    time.sleep(.3); y1 = cdp.js("(()=>{const s=document.querySelector('.tas-row .tas-st svg.tas-anim');const g=s.querySelector('animateTransform[id$=a]').parentNode;const t=g.transform.animVal;return t.length?t[0].matrix.f:null})()")
    time.sleep(.35); y2 = cdp.js("(()=>{const s=document.querySelector('.tas-row .tas-st svg.tas-anim');const g=s.querySelector('animateTransform[id$=a]').parentNode;const t=g.transform.animVal;return t.length?t[0].matrix.f:null})()")
    check(y1 is not None and y2 is not None and y1 != y2 and 30 <= min(y1, y2) and max(y1, y2) <= 42, "installing: the arrow moves (between its rest and its low point)", [y1, y2])
    cdp.js("devHost.send(" + json.dumps({"type": "agents.setup", "show": True, "title": "Choisissez votre agent", "body": "x", "note": "y", "accent": "#0099A0",
          "rows": [{"id": "claude", "name": "Claude Code", "icon": "download", "ink": "accent", "animate": True, "status": "Installation…"}, {"id": "codex", "name": "Codex", "icon": "wifi", "ink": "grey", "status": "Pas connecté", "action": {"id": "signIn", "label": "Se connecter", "strong": True}}]}) + ")")
    same = cdp.js("document.querySelector('.tas-row .tas-st svg.tas-anim').querySelector('mask').id")
    check(same == "tasCut1", "installing: the block sent again (another agent changed) keeps the same running morph", same)
    cdp.call("Emulation.setEmulatedMedia", features=[{"name": "prefers-reduced-motion", "value": "reduce"}])

    # ---------------------------------------------------------------- signing in: « Annuler »; too old: « Mettre à jour »
    load("ta-connexion"); b = block()
    check(b and row(b, 0) == ("Version trop ancienne", "grey", True, "Mettre à jour", True), "too old: ⤓ « Version trop ancienne » + strong « Mettre à jour »", b and b["rows"][0])
    check(b and row(b, 1) == ("Connexion…", "accent", True, "Annuler", False), "signing in: 📶 « Connexion… » in the accent + neutral « Annuler »", b and b["rows"][1])
    n0 = len(posted(["agents.setup.act"])); cdp.js("Array.from(document.querySelectorAll('.tas-btn')).find(b=>b.textContent==='Annuler').click()"); time.sleep(.1)
    check(posted(["agents.setup.act"])[n0:] == [{"type": "agents.setup.act", "agentId": "codex", "action": "cancel"}], "signing in: « Annuler » tells the host (CancelSignIn)", posted(["agents.setup.act"])[n0:])
    shot("panneau-connexion", {"x": 0, "y": 0, "width": 380, "height": 664})

    # ---------------------------------------------------------------- the install failed / the sign-in failed
    load("ta-erreur"); b = block()
    check(b and row(b, 0) == ("Échec de l'installation", "danger", True, "Réessayer", False) and b["rows"][0]["color"] == DANGER and b["rows"][0]["link"] == "Installer moi-même ↗",
          "error: ☹ « Échec de l'installation » (danger) + neutral « Réessayer » + « Installer moi-même ↗ » under it", b and b["rows"][0])
    n0 = len(posted(["agents.setup.manual", "agents.setup.act"]))
    cdp.js("document.querySelector('.tas-link').click()"); time.sleep(.1); cdp.js("Array.from(document.querySelectorAll('.tas-btn')).find(b=>b.textContent==='Réessayer').click()"); time.sleep(.1)
    check(posted(["agents.setup.manual", "agents.setup.act"])[n0:] == [{"type": "agents.setup.manual", "agentId": "claude"}, {"type": "agents.setup.act", "agentId": "claude", "action": "retry"}],
          "error: « Installer moi-même ↗ » and « Réessayer » tell the host", posted(["agents.setup.manual", "agents.setup.act"])[n0:])
    lk = cdp.js("(()=>{const l=document.querySelector('.tas-link'),c=getComputedStyle(l);return [c.fontSize,c.lineHeight,c.textDecorationLine,c.color]})()")
    check(lk == ["13px", "17px", "underline", "rgb(230, 230, 230)"], "error: the link 13 px, underlined", lk)
    shot("panneau-erreur", {"x": 0, "y": 0, "width": 380, "height": 664})
    load("ta-connexion-echec"); b = block()
    check(b and row(b, 1) == ("Échec de la connexion", "danger", True, "Réessayer", False) and not b["rows"][1]["link"], "sign-in failed: « Échec de la connexion » + « Réessayer », no « Installer moi-même »", b and b["rows"][1])
    load("ta-desktop"); b = block()
    check(b and row(b, 0) == ("Pas installé", "grey", True, "Installer", True) and b["rows"][0]["note"] == "L'appli Claude est installée, mais le chat a besoin de Claude Code (même compte).",
          "Claude Desktop only: « Pas installé » + its 13 px note", b and b["rows"][0])
    rn = cdp.js("(()=>{const c=getComputedStyle(document.querySelector('.tas-rn'));return [c.fontSize,c.color]})()")
    check(rn == ["13px", "rgb(140, 140, 140)"], "Claude Desktop only: the note 13 px #8C8C8C", rn)
    shot("panneau-desktop", {"x": 0, "y": 0, "width": 380, "height": 664})

    # ---------------------------------------------------------------- ready, or a thread: no block
    load("ta-pret")
    check(block() is None and not cdp.js("document.querySelector('.empty').hidden") and cdp.js("document.querySelector('#empty-h').textContent") == "Que voulez-vous changer ?", "ready: no block — the empty chat as before")
    load("ta-fil")
    check(block() is None and not cdp.js("document.querySelector('.log').hidden"), "a chat with messages: no block over the thread")
    strip = cdp.js("(()=>{const s=Array.from(document.querySelectorAll('.dock .strip')).find(s=>/pas installé/.test(s.textContent));return s?s.textContent:null})()")
    check(strip is not None and "Installer" in strip, "a chat with messages: the « pas installé » strip stays, with « Installer »", strip)
    if strip:
        n0 = len(posted(["agents.setup.open", "agent.prepare"]))
        cdp.js("Array.from(document.querySelectorAll('.dock .strip .btn-s')).find(b=>b.textContent==='Installer').click()"); time.sleep(.1)
        check(posted(["agents.setup.open", "agent.prepare"])[n0:] == [{"type": "agents.setup.open"}], "the strip's « Installer » opens the « Ton agent » screen (never the old download)", posted(["agents.setup.open", "agent.prepare"])[n0:])

    # ---------------------------------------------------------------- the comment bubble: the same block
    load("c-ta-premier-message"); b = block()
    check(b is not None and len(b["rows"]) == 2 and b["rows"][0]["btn"] == "Installer", "bubble: the same block (no first-run screen there)", b)
    size = cdp.js("(window.__posted.filter(m=>m.type==='bubble.size').pop()||{})")
    check(b and size.get("height", 0) >= b["box"][3] + 24, "bubble: its height asked includes the block", [size, b and b["box"]])
    if b:
        r = cdp.js("(()=>{const a=document.getElementById('app').getBoundingClientRect();return {x:a.left,y:a.top,width:a.width,height:a.height}})()")
        shot("bulle-premier-message", r)
    load("c-ta-erreur"); b = block()
    check(b and b["rows"][0]["link"] == "Installer moi-même ↗", "bubble: after a failure, « Échec de l'installation » and the link", b)
    if b:
        r = cdp.js("(()=>{const a=document.getElementById('app').getBoundingClientRect();return {x:a.left,y:a.top,width:a.width,height:a.height}})()")
        shot("bulle-erreur", r)
finally:
    try: srv.shutdown()
    except Exception: pass
    kill_tree(proc)

print(f"\n{count[0] - len(failures)} / {count[0]} checks — renders in {OUT}")
sys.exit(1 if failures else 0)
