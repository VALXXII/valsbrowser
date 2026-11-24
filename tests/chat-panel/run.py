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
COMMENT = os.path.join(os.path.dirname(OUT), "comment-v2", "panel")   # comment-mode bubble (B 1.8/1.9): build/chat-qa/comment-v2/panel
REF_C = os.path.join(REF, "comment")                      # the bubbles of the Paper boards 18NS-0 … 19IO-0, exported at 1x
ONLY = set(filter(None, (opt("--only", "") or "").split(",")))
EDGE = opt("--edge") or next(iter(sorted(glob.glob(os.path.join(os.environ.get("LOCALAPPDATA", ""), "Microsoft", "EdgeCore", "*", "msedge.exe"))
                                         + glob.glob(os.path.join(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"), "Microsoft", "EdgeCore", "*", "msedge.exe")), reverse=True)), None)
os.environ["BROWSER_ANNOTATIONS_TEST"] = "1"

# ------------------------------------------------------------------ Paper values (worker F's survey) for the fidelity table
PAPER_MD = os.path.join(ROOT, "design", "chat-comment-paper-values.md")
FID = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "fidelity.json"), encoding="utf-8"))
def paper_nodes():
    out = {}
    if not os.path.exists(PAPER_MD): return out
    for line in open(PAPER_MD, encoding="utf-8"):
        m = re.match(r"\|\s*(.*?)\s*\|\s*`([0-9A-Z]+-0)`\s*\|\s*([\d.]+)×([\d.]+)\s*\|\s*(-?[\d.]+),\s*(-?[\d.]+)\s*\|(?:[^|]*\|)?\s*([^|]*)\|\s*$", line)
        if not m or m.group(2) in out: continue
        st = {}
        for kv in m.group(7).split(";"):
            if ":" in kv: k, v = kv.split(":", 1); st[k.strip()] = v.strip()
        out[m.group(2)] = {"name": m.group(1).replace("└", "").strip(), "w": float(m.group(3)), "h": float(m.group(4)), "x": float(m.group(5)), "y": float(m.group(6)), "st": st}
    return out
PAPER = paper_nodes()
fidelity = {}

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

# ------------------------------------------------------------------ fidelity: DOM vs Paper, value by value
def rgba(v):
    v = (v or "").strip()
    m = re.match(r"color-mix\((#[0-9A-Fa-f]{6,8})\s+([\d.]+)%,\s*transparent\)", v)
    if m: c = rgba(m.group(1)); return (c[0], c[1], c[2], round(c[3] * float(m.group(2)) / 100, 3))
    m = re.match(r"#([0-9A-Fa-f]{6})([0-9A-Fa-f]{2})?$", v)
    if m: h = m.group(1); return (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16), round(int(m.group(2) or "FF", 16) / 255, 3))
    m = re.match(r"color\(srgb\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)(?:\s*/\s*([\d.]+))?\)", v)
    if m: return (round(float(m.group(1)) * 255), round(float(m.group(2)) * 255), round(float(m.group(3)) * 255), round(float(m.group(4) or 1), 3))
    m = re.match(r"rgba?\(([\d.]+),\s*([\d.]+),\s*([\d.]+)(?:,\s*([\d.]+))?\)", v)
    if m: return (round(float(m.group(1))), round(float(m.group(2))), round(float(m.group(3))), round(float(m.group(4) or 1), 3))
    if v in ("transparent", "none", ""): return (0, 0, 0, 0)
    return None
def over_canvas(c):   # a fill as seen on the bubble's #171717
    if c is None: return None
    return tuple(round(c[i] * c[3] + 23 * (1 - c[3])) for i in range(3)) + (1,)
def same_fill(a, b):
    x, y = over_canvas(rgba(a)), over_canvas(rgba(b))
    if x is None or y is None: return a == b
    return all(abs(x[i] - y[i]) <= 1 for i in range(3))
def same_color(a, b):
    x, y = rgba(a), rgba(b)
    if x is None or y is None: return a == b
    if x[3] == 0 and y[3] == 0: return True
    return all(abs(x[i] - y[i]) <= 1 for i in range(3)) and abs(x[3] - y[3]) <= 0.012
px = lambda v: float(re.sub(r"px$", "", str(v))) if re.match(r"^-?[\d.]+(px)?$", str(v).strip()) else None
# Écarts assumés (design/chat-panel.md §19.7, §20): (Paper node or "*", property) -> reason
JUSTIFIED = {
    ("*", "texte « Ask »"): "Nom de mode en français imposé (« Demander », brief règle 7, contrat §18) : le libellé et la largeur du bouton du mode changent",
    ("1AH9-0", "w"): "Paper dessine la bulle de 1AE7-0 à 220 px, les autres planches à 210 : une seule valeur retenue, 210 (§19.7.4)",
    ("1AH9-0", "x"): "Même cause (210 au lieu de 220 : le bord gauche se décale de 10 px) (§19.7.4)",
    ("1AHA-0", "x"): "Même cause : le texte suit la bulle de 210 px (§19.7.4)",
    ("1AZT-0", "radius"): "Paper dessine la bulle de 1AWI-0 en rayon 10, les autres en 12 : une seule valeur retenue, 12 (§19.7.4)",
    ("1B6U-0", "x"): "Survol de l'étape : 1AIE-0 la place 2 px plus à droite que 1B30-0 (sans le retrait de 2 px de la ligne) ; l'étape reste où la met 1B30-0, l'infobulle se cale sur elle (§19.7.4)",
    ("1B6U-0", "w"): "Paper fige l'étape survolée à 128 px ; la page garde la largeur de son texte (128 px ici, mais un autre libellé change la largeur)",
    ("1B7H-0", "x"): "L'infobulle se cale sur le bord gauche de l'étape survolée, placée comme dans 1B30-0 (voir 1B6U-0)",
    ("1ARB-0", "x"): "Étape en cours : 1ANU-0 dessine un anneau de 12 px collé au bord droit, 1B30-0 un anneau de 14 px en retrait ; une seule variante retenue, 1B30-0 (§19.7.4)",
    ("1AS9-0", "padding top"): "Les 6 px au-dessus et au-dessous des lignes sont portés par la zone qui défile (.bview-body) plutôt que par son cadre : mêmes positions de lignes (mesurées égales)",
    ("1AS9-0", "padding bottom"): "Même cause que le haut : la marge est dans la zone qui défile",
    ("1B1J-0", "padding top"): "Les 6 px sont portés par la zone qui défile (.bview-body) : mêmes positions de lignes (mesurées égales)",
    ("1B1J-0", "padding bottom"): "Même cause que le haut : la marge est dans la zone qui défile",
    ("1B08-0", "padding left"): "Le retrait de 10 px et l'écart de 8 px sont dans le bouton de la barre (.bt-bar-main), pour que toute la barre soit cliquable : icône, nom et +/− aux mêmes positions (mesurées égales)",
    ("1B08-0", "gap"): "Même cause : l'écart de 8 px est entre les enfants du bouton de la barre",
    ("17US-0", "h"): "Hauteur du menu : la planche montre « Autres modèles » ; il n'apparaît que si un fournisseur a plus de 3 modèles (§16) — le scénario comparé en a 4, la hauteur doit donc être égale",
    ("*", "x texte"): "Position du premier caractère : Inter Display rendu par Chromium et par Paper diffère de moins de 2 px (§19.7.5)",
}
def fidelity_table():
    rows, bad = [], 0
    for scen, items in fidelity.items():
        extra = FID[scen].get("paper", {}) if isinstance(FID.get(scen), dict) else {}   # values read from a board updated after F's survey
        for o in items:
            P = extra.get(o["id"]) or PAPER.get(o["id"])
            if not P: rows.append((scen, o["id"], o["sel"], "nœud", "absent du relevé", "", "", "")); bad += 1; continue
            if o.get("missing"): rows.append((scen, o["id"], o["sel"], "élément", "présent", "absent", "", "")); bad += 1; continue
            st, ds = P["st"], o["s"]
            def add(prop, pv, dv, ok, key=None):
                nonlocal bad
                if ok: rows.append((scen, o["id"], o["sel"], prop, pv, dv, "=", "")); return
                why = JUSTIFIED.get((o["id"], key or prop)) or JUSTIFIED.get(("*", key or prop)) or ""
                if not why and "Ask" in P["name"]: why = JUSTIFIED[("*", "texte « Ask »")]
                if not why and o["sel"] in (".model .m", ".model .mn", ".model .e") and prop in ("x", "w", "x texte"):
                    why = "cv4 R28 (décision de l'utilisateur) : le bouton modèle garde son chevron dans la bulle — plus large de la place du chevron, aligné à droite, il commence donc plus à gauche"
                if not why and P["name"] in ("Mode",) and prop in ("w", "padding left", "padding right"): why = "R21 (décision de l'utilisateur) : le bouton du mode est son icône seule dans un carré de 24 px — plus de libellé, donc ni sa largeur ni ses marges"
                if not why: bad += 1
                rows.append((scen, o["id"], o["sel"], prop, pv, dv, "≠", why))
            k = o["kind"]
            if k == "f":
                for prop, pv, dv in (("x", P["x"], o["x"]), ("y", P["y"], o["y"]), ("w", P["w"], o["w"]), ("h", P["h"], o["h"])):
                    add(prop, round(pv, 1), round(dv, 1), abs(pv - dv) <= 1)
            elif k == "ph":   # a placeholder: where its line starts and its vertical centre
                add("x", round(P["x"], 1), round(o["x"], 1), abs(P["x"] - o["x"]) <= 1)
                add("centre y", round(P["y"] + P["h"] / 2, 1), round(o["y"] + o["h"] / 2, 1), abs(P["y"] + P["h"] / 2 - o["y"] - o["h"] / 2) <= 1.5)
            else:
                cy_p, cy_d = P["y"] + P["h"] / 2, o["y"] + o["h"] / 2
                add("centre y", round(cy_p, 1), round(cy_d, 1), abs(cy_p - cy_d) <= 1.5)
                if k == "tc": add("centre x", round(P["x"] + P["w"] / 2, 1), round(o["x"] + o["w"] / 2, 1), abs(P["x"] + P["w"] / 2 - o["x"] - o["w"] / 2) <= 2.5, "x texte")
                else:
                    d = abs(P["x"] - o["x"])
                    add("x", round(P["x"], 1), round(o["x"], 1), d <= 1, "x" if d > 2 else "x texte")
            if "bg" in st and k == "f": add("fond", st["bg"], ds["bg"], same_fill(st["bg"], ds["bg"]))
            corners = {"rtl": "borderTopLeftRadius", "rtr": "borderTopRightRadius", "rbr": "borderBottomRightRadius", "rbl": "borderBottomLeftRadius"}
            if k == "f":
                for dk, pk in corners.items():
                    pv = st.get(pk, st.get("radius"))
                    if pv is None: continue
                    if "infinity" in pv: ok = px(ds[dk]) is not None and px(ds[dk]) >= o["h"] / 2 - .5; pv = "pilule"
                    else: ok = px(pv) is not None and px(ds[dk]) is not None and abs(px(pv) - px(ds[dk])) < .5
                    add("rayon " + pk.replace("border", "").replace("Radius", ""), pv, ds[dk], ok, "radius")
                pads = {"pt": ["paddingTop", "paddingBlock", "padding"], "pb": ["paddingBottom", "paddingBlock", "padding"], "pl": ["paddingLeft", "paddingInline", "padding"], "pr": ["paddingRight", "paddingInline", "padding"]}
                for dk, keys in pads.items():
                    pv = next((st[x] for x in keys if x in st), None)
                    if pv is None or px(pv) is None: continue
                    add("padding " + keys[0][7:].lower(), pv, ds[dk], abs(px(pv) - px(ds[dk])) < .5)
                if "gap" in st and px(st["gap"]) is not None and o["sel"] != "#app":   # the surface's 12 px sit on its inner column (.main)
                    g = px(st["gap"]); dg = px(ds["rg"] if ds["dir"].startswith("column") else ds["cg"])
                    add("gap", st["gap"], ds["rg"] if ds["dir"].startswith("column") else ds["cg"], dg is not None and abs(g - dg) < .5)
                if "opacity" in st and k == "f": add("opacité", st["opacity"], ds["opacity"], abs(float(st["opacity"].rstrip("%")) / (100 if st["opacity"].endswith("%") else 1) - float(ds["opacity"])) < .01)
            if k != "f":
                if "size" in st: add("taille", st["size"], ds["size"], px(st["size"]) == px(ds["size"]))
                if "lh" in st: add("interligne", st["lh"], ds["lh"], px(st["lh"]) == px(ds["lh"]))
                if "weight" in st: add("graisse", st["weight"], ds["weight"], str(st["weight"]) == str(ds["weight"]))
                if "color" in st:   # a text's own opacity (Paper 1B7W-0: white at 75 %) is part of its colour
                    pc = st["color"]
                    if "opacity" in st and rgba(pc): c = rgba(pc); op = float(st["opacity"].rstrip("%")) / (100 if st["opacity"].endswith("%") else 1); pc = "rgba(%d, %d, %d, %.3f)" % (c[0], c[1], c[2], c[3] * op)
                    add("couleur", st["color"] + (" × " + st["opacity"] if "opacity" in st else ""), ds["color"], same_color(pc, ds["color"]))
            if k == "f" and o["id"] in ("14RK-0", "17US-0"):   # the menus: their border and shadow (Paper 14RK-0 has one, 17US-0 none)
                add("bordure", st.get("borderWidth", "0px"), ds["bw"], px(st.get("borderWidth", "0px")) == px(ds["bw"]))
                sh = {"14RK-0": "rgba(0, 0, 0, 0.5) 0px 16px 48px 0px", "17US-0": "none"}[o["id"]]
                add("ombre", st.get("boxShadow", "aucune"), ds["shadow"], ds["shadow"] == sh)
            if k == "f" and o["id"] in ("18TO-0", "1B60-0", "1ALE-0", "1AH7-0", "1AQU-0", "19RN-0", "1ADE-0", "1A9L-0", "1AZR-0", "1B19-0"):
                add("ombre", "aucune", ds["shadow"], ds["shadow"] == "none"); add("bordure", "aucune", ds["bw"], ds["bw"] == "0px")
    out = ["# Bulle du mode Commentaire — écarts mesurés (page) / relevé Paper", "",
           "Généré par `tests/chat-panel/run.py` : chaque nœud Paper du relevé de F (`design/chat-comment-paper-values.md`) est mesuré dans la page au même état (boîtes relatives à la bulle, styles calculés). Tolérance : 1 px pour les boîtes, 1,5 px pour le centre vertical d'un texte, 1/255 pour une couleur. Les largeurs de texte ne sont pas comparées (rendu des polices, §19.7.5).", "",
           f"**Écarts non justifiés : {bad}** · valeurs comparées : {len(rows)} · justifiés : {sum(1 for r in rows if r[6] == '≠' and r[7])}", "",
           "| État | Nœud Paper | Élément | Propriété | Paper | Page | | Justification |", "|---|---|---|---|---|---|---|---|"]
    for r in rows:
        if r[6] == "=": continue
        out.append("| " + " | ".join(str(x).replace("|", "\\|") for x in r) + " |")
    out += ["", "## Toutes les valeurs égales", "", "| État | Nœud Paper | Élément | Propriété | Paper | Page |", "|---|---|---|---|---|---|"]
    for r in rows:
        if r[6] == "=": out.append("| " + " | ".join(str(x).replace("|", "\\|") for x in r[:6]) + " |")
    open(os.path.join(COMMENT, "ecarts.md"), "w", encoding="utf-8").write("\n".join(out) + "\n")
    report["fidelity"] = {"compared": len(rows), "unjustified": bad}
    check(bad == 0 and len(fidelity) == len([k for k in FID if not k.startswith("_")]), f"fidelity: every Paper value of the 10 bubble boards and the 3 menus matches the page, or its gap is justified ({len(rows)} values)", f"{bad} unjustified — build/chat-qa/comment-v2/panel/ecarts.md")

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


def bind_to_runner(p):
    """Windows only: put the test browser in a job object closed with this process, so a killed runner
    (timeout, closed session) never leaves headless browsers behind. Returns the job handle (kept alive)."""
    if os.name != "nt": return None
    import ctypes
    from ctypes import wintypes
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.CreateJobObjectW.restype = wintypes.HANDLE
    k32.OpenProcess.restype = wintypes.HANDLE
    class BASIC(ctypes.Structure):
        _fields_ = [("PerProcessUserTimeLimit", ctypes.c_int64), ("PerJobUserTimeLimit", ctypes.c_int64), ("LimitFlags", wintypes.DWORD),
                    ("MinimumWorkingSetSize", ctypes.c_size_t), ("MaximumWorkingSetSize", ctypes.c_size_t), ("ActiveProcessLimit", wintypes.DWORD),
                    ("Affinity", ctypes.c_size_t), ("PriorityClass", wintypes.DWORD), ("SchedulingClass", wintypes.DWORD)]
    class IO(ctypes.Structure):
        _fields_ = [(n, ctypes.c_uint64) for n in ("R", "W", "O", "RB", "WB", "OB")]
    class EXT(ctypes.Structure):
        _fields_ = [("Basic", BASIC), ("Io", IO), ("ProcessMemoryLimit", ctypes.c_size_t), ("JobMemoryLimit", ctypes.c_size_t),
                    ("PeakProcessMemoryUsed", ctypes.c_size_t), ("PeakJobMemoryUsed", ctypes.c_size_t)]
    job = k32.CreateJobObjectW(None, None)
    if not job: return None
    info = EXT(); info.Basic.LimitFlags = 0x2000  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
    k32.SetInformationJobObject(job, 9, ctypes.byref(info), ctypes.sizeof(info))  # JobObjectExtendedLimitInformation
    h = k32.OpenProcess(0x0101, False, p.pid)  # PROCESS_SET_QUOTA | PROCESS_TERMINATE
    if h: k32.AssignProcessToJobObject(job, h); k32.CloseHandle(h)
    return job

def kill_tree(p):
    if p is None: return
    if os.name == "nt": subprocess.run(["taskkill", "/PID", str(p.pid), "/T", "/F"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    else: p.kill()
    try: p.wait(10)
    except Exception: pass

profile = tempfile.mkdtemp(prefix="chat-panel-qa-")
proc = None
try:
    check(bool(EDGE and os.path.exists(EDGE)), "runtime: headless Chromium (EdgeCore) found", EDGE or "none")
    if not EDGE: raise SystemExit(1)
    proc = subprocess.Popen([EDGE, "--headless=new", "--disable-gpu", "--no-first-run", "--no-default-browser-check", "--disable-extensions", "--disable-sync",
                             "--disable-background-networking", "--disable-component-update", "--remote-debugging-port=0", "--user-data-dir=" + profile,
                             "--force-device-scale-factor=1", "--window-size=380,698", "about:blank"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    browser_job = bind_to_runner(proc)  # dies with this runner, even if it is killed
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
        if name.startswith("c-") and name != "c-bascule-panneau" and (w, h) == (380, 698): w, h = 1200, 1400   # cv3: the bubble's window has up to 400 px of margin each side
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

    # every value Paper gives, measured in the page: boxes relative to the bubble, computed styles
    FID_JS = r"""(maps,root,dy)=>{const a=document.querySelector(root).getBoundingClientRect();return maps.map(([id,sel,kind])=>{const el=document.querySelector(sel);if(!el)return {id,sel,kind,missing:true};
      const cs=getComputedStyle(el);let r=el.getBoundingClientRect();const o={id,sel,kind};
      if(kind==='t'||kind==='tc'){const rs=[];const w=document.createTreeWalker(el,NodeFilter.SHOW_TEXT);let n;while((n=w.nextNode())){if(!n.nodeValue.trim())continue;if(n.parentElement.closest('.sr-only,svg'))continue;const g=document.createRange();g.selectNodeContents(n);for(const q of g.getClientRects())rs.push(q);}
        if(rs.length){const L=Math.min(...rs.map(q=>q.left)),T=Math.min(...rs.map(q=>q.top)),R=Math.max(...rs.map(q=>q.right)),B=Math.max(...rs.map(q=>q.bottom));r={left:L,top:T,width:R-L,height:B-T};}}
      let st=cs;if(kind==='ph'){st=getComputedStyle(el,el.tagName==='INPUT'?'::placeholder':'::before');const pl=parseFloat(cs.paddingLeft),pr=parseFloat(cs.paddingRight);r={left:r.left+pl,top:r.top,width:r.width-pl-pr,height:r.height};}
      o.x=r.left-a.left;o.y=r.top-a.top+(sel===root?0:dy);o.w=r.width;o.h=r.height;
      o.s={bg:cs.backgroundColor,rtl:cs.borderTopLeftRadius,rtr:cs.borderTopRightRadius,rbr:cs.borderBottomRightRadius,rbl:cs.borderBottomLeftRadius,pt:cs.paddingTop,pr:cs.paddingRight,pb:cs.paddingBottom,pl:cs.paddingLeft,
        rg:cs.rowGap,cg:cs.columnGap,dir:cs.flexDirection,size:st.fontSize,lh:kind==='ph'&&el.tagName==='INPUT'?cs.lineHeight:st.lineHeight,weight:st.fontWeight,color:st.color,opacity:cs.opacity,bw:cs.borderTopWidth,shadow:cs.boxShadow};return o;})}"""

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
        ok = load(name, 380, h)   # bubble: room for what floats around the surface (sub-menus on the right)
        if name in HOVER:
            box = cdp.js(f"(()=>{{const n=document.querySelector({json.dumps(HOVER[name])});if(!n)return null;const r=n.getBoundingClientRect();return [r.left+r.width/2,r.top+r.height/2]}})()")
            if box: cdp.call("Input.dispatchMouseEvent", type="mouseMoved", x=box[0], y=box[1]); time.sleep(.65 if bubble else .25)
        if bubble:
            # the bubble at the size it asked for (the fake host resizes its « window » from bubble.size), transparent around it
            cdp.js("new Promise(r=>{document.activeElement&&document.activeElement.blur();requestAnimationFrame(()=>requestAnimationFrame(r))})")   # no caret, no focus ring: the boards show neither
            size = bubble_size()
            bubble_sizes[name] = size
            if name in FID:
                spec = FID[name] if isinstance(FID[name], dict) else {"nodes": FID[name]}
                fidelity[name] = cdp.js("(" + FID_JS + ")(" + json.dumps(spec["nodes"]) + "," + json.dumps(spec.get("root", "#app")) + "," + str(spec.get("dy", 0)) + ")")
            over = cdp.js("(()=>{const s=document.querySelector('.scroller'),a=document.getElementById('app');return [s.scrollHeight-s.clientHeight, s.scrollTop, a.scrollTop, Math.round(a.getBoundingClientRect().height)]})()")
            if name in ("c-2-autorisation", "c-fil-long", "c-autorisation-reouverte", "c-autorisation-courte"): check(over[0] > 0 and over[2] == 0, f"{name}: at its maximum height only the thread scrolls", str(over))
            else: check(over[:3] == [0, 0, 0] and size and over[3] == size[1], f"{name}: the window fits the content exactly (nothing scrolls, nothing is cut)", str(over) + " " + str(size))
            out = os.path.join(COMMENT, name + ".png")
            cdp.call("Emulation.setDefaultBackgroundColorOverride", color={"r": 0, "g": 0, "b": 0, "a": 0})
            ins = cdp.js("window.__inset||{top:0,right:0,bottom:0,left:0}")
            # the surface alone, as Paper exports the bubble's node (what floats outside it is another layer there too)
            cdp.js("document.querySelector('.layer').style.visibility='hidden'")
            shot(out, {"x": ins["left"], "y": ins["top"], "width": size[0], "height": size[1]} if size else None)
            cdp.js("document.querySelector('.layer').style.visibility=''")
            reg = cdp.js("window.__region||null")
            if reg and (reg["width"] != size[0] or reg["height"] != size[1]):   # the visible region: the surface and what floats outside it
                shot(os.path.join(COMMENT, name + "-fenetre.png"), reg)
            if name == "c-menu-effort":   # the Effort sub-menu alone, next to its board (Paper 17VU-0)
                sb = cdp.js("(()=>{const n=document.querySelector('.sub-menu');if(!n)return null;const r=n.getBoundingClientRect();return {x:r.left,y:r.top,width:r.width,height:r.height}})()")
                if sb: shot(os.path.join(COMMENT, "c-menu-effort-sous-menu.png"), sb)
            if name == "c-2.1-survol":   # the hover list alone, next to its board (Paper 1B7H-0)
                tb = cdp.js("(()=>{const r=document.getElementById('b-tip').getBoundingClientRect();return {x:r.left,y:r.top,width:r.width,height:r.height}})()")
                if tb["width"]: shot(os.path.join(COMMENT, "c-2.1-infobulle-etapes.png"), tb)
            cdp.call("Emulation.setDefaultBackgroundColorOverride")
            cdp.call("Input.dispatchMouseEvent", type="mouseMoved", x=370, y=690)
        else:
            out = os.path.join(OUT, name + ".png")
            shot(out)
        tt = cdp.js("Array.from(document.querySelectorAll('[title]')).map(n=>n.className+': '+n.title).slice(0,4)")
        check(not tt, f"{name}: no system tooltip (no `title` attribute; audit D7)", "; ".join(tt))
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
        if name not in ("c-2.1-infobulle-etapes", "c-menu-effort-sous-menu"):
            check((r["w"], r["h"]) == (rw, rh), f"{name}: the bubble has the size of its Paper board ({rw}x{rh})", f"{r['w']}x{r['h']}")

    if fidelity: fidelity_table()

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
        md = cdp.js("(()=>{const b=document.querySelector('.tb.mode');return [b.textContent, b.getAttribute('aria-label'), Math.round(b.getBoundingClientRect().width), b.querySelectorAll('svg').length, document.getElementById('mode-tip').textContent]})()")
        check(md == ["", "Mode : Demander", 24, 1, "Demander"] and "Ask" not in b["all"], "bubble (R21): the mode is its icon alone (24 px); its French name is its accessible name and its drawn bubble", str(md))
        check(b["radius"] == "16px" and cdp.js("getComputedStyle(document.getElementById('app')).boxShadow") == "none" and cdp.js("getComputedStyle(document.getElementById('app')).borderTopWidth") == "0px",
              "bubble (Paper 18TO-0): radius 16, no shadow, no border")
        check(b["focus"] == "composer-input", "bubble: the cursor is in the input at the opening", b["focus"])
        check(sizes() == [[305, 84]], "bubble: one `bubble.size` per change (305 x 84 for the empty input), never the same value twice", str(sizes()))
        host({"type": "layout", "mode": "bubble", "maxWidth": 305, "maxHeight": 520, "open": False, "inset": {"top": 0, "right": 0, "bottom": 0, "left": 0}})
        check(sizes() == [[305, 84]], "bubble (B 1.9): a `layout` with open:false (the host's answer) is not acknowledged when nothing changed — no loop", str(sizes()))
        host({"type": "layout", "mode": "bubble", "maxWidth": 305, "maxHeight": 520, "element": "button.btn-primary"})
        check(sizes() == [[305, 84], [305, 84]], "bubble: an opening `layout` is always answered by `bubble.size`, even unchanged (acknowledgement)", str(sizes()))
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
        r = cdp.js("""(()=>{const s=document.querySelector('.send');return {cin:V('.cin'),plus:document.querySelector('.tools .tb[aria-label=Joindre]').getClientRects().length,
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
        tg = cdp.js("""(()=>{const a=document.getElementById('app').getBoundingClientRect(),t=document.getElementById('b-tip').getBoundingClientRect(),s=document.querySelector('.bt-st').getBoundingClientRect(),cs=getComputedStyle(document.getElementById('b-tip'));
          return {dx:Math.round(t.left-a.left),dy:Math.round(t.top-a.top),w:Math.round(t.width),h:Math.round(t.height),gap:Math.round(s.top-t.bottom),left:Math.round(t.left-s.left),radius:[cs.borderTopLeftRadius,cs.borderTopRightRadius,cs.borderBottomRightRadius,cs.borderBottomLeftRadius].join(' '),
          shadow:cs.boxShadow,bg:cs.backgroundColor,ask:window.__posted.filter(m=>m.type==='bubble.size').pop(),inset:window.__inset,winTop:Math.round(t.top),stBg:getComputedStyle(document.querySelector('.bt-st')).backgroundColor}})()""")
        check(tg["w"] == 178 and tg["h"] == 82 and tg["gap"] == 2 and tg["left"] == 0 and tg["dx"] in (163, 165) and tg["dy"] == -7 and tg["radius"] == "10px 10px 10px 4px" and tg["shadow"] == "none" and tg["bg"] == "rgb(47, 47, 47)",
              "bubble (hover, R3, Paper 1AIE-0): the list sits 2 px above its step, left edges aligned, 178×82 at (165, −7) from the bubble, corners 10/10/10/4, no shadow", json.dumps(tg)[:300])
        check(tg["ask"].get("overflowTop") == 7 and abs(tg["ask"].get("overflowRight", 0) - (tg["dx"] + 178 - 305)) <= 1 and tg["inset"]["top"] >= 7 and tg["winTop"] >= 0 and tg["stBg"] == "rgba(255, 255, 255, 0.1)",
              "bubble (hover, R3): it leaves the bubble — `bubble.size` asks 7 px above and 36–38 px right, the host grants them, nothing is cut; the hovered step is white 10 %", json.dumps(tg)[-260:])
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
        check(cdp.js("document.activeElement.classList.contains('bperm')"), "bubble (permission, SEC-23): the card holds the focus, never « Autoriser »", active())
        load("c-2-autorisation"); cdp.js("document.querySelector('.bperm').__shownAt = performance.now()"); key("Enter"); click(".bperm .btn.primary")
        check(not any(x.startswith("permission.respond") for x in posted()), "bubble (permission, SEC-23): Entrée or a click in the card's first 600 ms answers nothing", str(posted()[-3:]))
        time.sleep(0.7); key("Enter")
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
        click(".bperm-file"); time.sleep(0.6); click(".bperm .btn.primary")   # SEC-23: armed 600 ms after the card appeared
        check("permission.respond:always" in posted() and cdp.js("document.getElementById('bview').hidden && !document.querySelector('.bperm')"), "bubble (diff): « Autoriser » answers from the open diff and closes it")
        load("c-autorisation-commande")
        cmd = cdp.js("[document.querySelector('#bview .bview-pre').textContent, document.querySelectorAll('#bview .num').length]")
        check(cmd == ["npm test -- --run checkout", 0] and sizes()[-1] == [305, 224], "bubble (permission): a command opens the same way, at its own height (never smaller than the turn's bubble)", f"{cmd} {sizes()[-1]}")

        # error
        load("c-2.2b-erreur")
        e = cdp.js("""(()=>({color:getComputedStyle(document.querySelector('.bt-text .text')).color,line:document.querySelector('.bt-err .w').textContent,more:document.querySelector('.bt-more').getAttribute('aria-expanded'),
          buttons:Array.from(document.querySelectorAll('.b-errft .btn')).map(b=>b.textContent),composer:(r=>Math.round(r.width*r.height))(document.querySelector('.composer').getBoundingClientRect()),st:document.querySelector('.bt-stw').textContent}))()""")
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
        check(rt == [{"v": 1, "type": "retry", "turnId": "t1"}] and cdp.js("(r=>r.width>0&&r.height>0)(document.querySelector('.composer').getBoundingClientRect())"), "bubble (error): « Réessayer » sends `retry` for that turn", json.dumps(rt))
        load("c-2.2b-erreur"); click(".b-errft .btn:not(.danger)")
        check(cdp.js("(r=>r.width>0&&r.height>0)(document.querySelector('.composer').getBoundingClientRect())") and not vis(".b-errft") and cdp.js("document.activeElement.id") == "composer-input" and not of("retry"), "bubble (error): « Annuler » brings the input back, nothing is sent")
        # H1 (real window, monkey seed 1 #29): after a turn in error the input was taken out of the page — whatever was typed
        # next (Entrée) sent NOTHING to the host until « Annuler ». Rule: after an error the user can always write and send.
        # The input stays in the page (out of sight: Paper 1AAE-0 draws Annuler / Réessayer alone) and keeps the keyboard;
        # the first character written brings it back (as « Annuler » does), Entrée sends. Typed as the monkey types.
        drawn = lambda sel: cdp.js(f"(()=>{{const n=document.querySelector({json.dumps(sel)});if(!n)return false;const r=n.getBoundingClientRect();return r.width>0&&r.height>0&&getComputedStyle(n).opacity!=='0'}})()")
        load("c-2.2b-erreur")
        rest = cdp.js("(()=>({foot:!!document.querySelector('.b-errft'),input:!!document.querySelector('[contenteditable=true]')}))()")
        check(rest["foot"] and rest["input"] and not drawn(".composer") and sizes()[-1] == [305, 248], "bubble (error, monkey #29): the input is still in the page behind Annuler / Réessayer, not drawn (the bubble keeps Paper's 248)", f"{rest} drawn={drawn('.composer')} {sizes()[-1]}")
        took = cdp.js("(()=>{const t=document.querySelector('[contenteditable=true]');if(!t)return 0;t.focus();const s=getSelection();s.selectAllChildren(t);s.collapseToEnd();return document.activeElement===t?1:0})()")
        cdp.call("Input.insertText", text="Essaie autrement"); time.sleep(.2)
        w = cdp.js("(()=>({text:(document.getElementById('composer-input')||{innerText:''}).innerText.trim(),focus:document.activeElement.id,foot:!!document.querySelector('.b-errft'),err:!!document.querySelector('.bt-err'),more:!!document.querySelector('.bt-more')}))()")
        check(took == 1 and w["text"] == "Essaie autrement" and w["focus"] == "composer-input" and drawn(".composer") and not w["foot"] and w["err"] and w["more"],
              "bubble (error, monkey #29): writing after an error brings the input back with what was written; the error and its « Plus d'infos » stay in the thread", f"focus taken {took} {json.dumps(w, ensure_ascii=False)} drawn={drawn('.composer')}")
        cdp.call("Input.insertText", text=" svp"); time.sleep(.1)
        check(cdp.js("(document.getElementById('composer-input')||{innerText:''}).innerText.trim()") == "Essaie autrement svp", "bubble (error, monkey #29): the caret stays at the end of what was written (the next characters follow)", cdp.js("(document.getElementById('composer-input')||{innerText:''}).innerText"))
        key("Enter"); time.sleep(.15)
        sent = [m.get("text") for m in of("send")]
        check(sent == ["Essaie autrement svp"] and not of("retry"), "bubble (error, monkey #29): Entrée sends the new message to the host (no « Réessayer » needed, none sent)", json.dumps(sent, ensure_ascii=False))
        load("c-2.2b-erreur"); cdp.js("(document.querySelector('[contenteditable=true]')||document.body).focus()"); key("Enter"); time.sleep(.1)
        check(not of("send") and vis(".b-errft") and not drawn(".composer"), "bubble (error, monkey #29): Entrée with nothing written sends nothing and leaves Annuler / Réessayer")
        key("Tab", mods=8); time.sleep(.1)
        check(not of("setMode"), "bubble (error, monkey #29): Maj+Tab does not change the mode while the input is out of sight", json.dumps(of("setMode")))

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
        check(hist == ["A lu index.html", "A lu Checkout.tsx", "A utilisé 1 outil MCP", "A lu checkout.css", "A modifié checkout.css", "A lancé npm run build"],
              "bubble (done): hovering « Historique » lists the turn's actions (reads, MCP tools, edits, commands)", " | ".join(hist))
        move(370, 690); time.sleep(.2)
        click(".bt-bar-main"); time.sleep(.2)
        v = cdp.js("""(()=>{const v=document.getElementById('bview');return {open:!v.hidden,title:v.querySelector('.t').textContent,nums:Array.from(v.querySelectorAll('.num')).map(n=>n.textContent),lines:v.querySelectorAll('.l').length,
          copy:!!v.querySelector('.bview-copy'),composer:V('.composer'),asked:window.__posted.filter(m=>m.type==='checkpoint.viewChanges').map(m=>m.turnId)}})()""")
        check(v["open"] and v["title"] == "checkout.css" and v["nums"] == ["+6", "−2"] and v["lines"] == 3 and v["copy"] and v["composer"] == 1 and v["asked"] == ["t1"] and sizes()[-1] == [305, 279],
              "bubble (done, cv5 R40): the bar opens the file's diff in the bubble (title, +6 −2, Copier), asks the host for the full diff, at the bubble's size of before (279)", json.dumps(v, ensure_ascii=False) + f" {sizes()[-1]}")
        click(".bview-copy"); click(".bview-x")
        check(len(of("copy")) == 2 and cdp.js("document.getElementById('bview').hidden") and "bt-bar-main" in cdp.js("document.activeElement.className") and sizes()[-1] == [305, 279], "bubble (done): Copier in the diff; closing returns to the bar and to 279")
        load("c-diff-plusieurs-fichiers")
        mf = cdp.js("[document.querySelector('#bview .t').textContent, Array.from(document.querySelectorAll('#bview .l.file')).map(l=>l.textContent), document.querySelectorAll('#bview .l.add').length>5]")
        check(mf == ["3 fichiers modifiés", ["src/styles/checkout.css", "src/pages/Checkout.tsx", "src/styles/pay-button.css"], True], "bubble (done): several files are stacked in the same view, each under its path", str(mf)[:200])

        # queue between elements, menus, long thread, no folder
        load("c-file-attente")
        q = cdp.js("[document.querySelector('.bq .bubble').textContent, document.querySelector('.bq-cap .tx').textContent, V('.composer')]")
        host({"type": "strip", "id": "wait", "tone": "neutral", "text": "Commencera après le travail en cours sur button.btn-primary."})
        check(cdp.js("V('.strip')") == 0 and cdp.js("V('.bq-cap')") == 1, "bubble (queue): the host's `wait` strip, which says the same sentence, is not shown twice")
        click(".bq-cap .icon-btn")
        check(q == ["Mets ce titre en gras", "Commencera après le travail en cours sur button.btn-primary", 1] and "queue.remove:q1" in posted(), "bubble (queue): a request written during another element's turn waits, with its discreet line and « × »", str(q))
        # monkey seed 3 #63/#70: in the bubble of an element that already has an exchange, the user writes and presses Entrée while
        # another element's turn runs; the host queues it (queue.items[].after) — the waiting message is seen AT ONCE in this
        # bubble, whole, inside the visible surface: dimmed user bubble, « Commencera après le travail en cours sur … », « × »
        Q63_JS = """(()=>{const a=document.getElementById('app').getBoundingClientRect(),R=window.__region||{x:0,y:0,width:1e5,height:1e5};
          const inside=(n)=>{if(!n||!n.getClientRects().length)return false;const r=n.getBoundingClientRect();const vis=(()=>{let p=n;while(p&&p!==document.body){const cs=getComputedStyle(p);if(cs.visibility==='hidden'||cs.display==='none'||+cs.opacity===0)return false;p=p.parentElement}return true})();
            return vis&&r.width>0&&r.height>0&&r.top>=a.top-.5&&r.bottom<=a.bottom+.5&&r.left>=a.left-.5&&r.right<=a.right+.5&&r.top>=R.y-.5&&r.bottom<=R.y+R.height+.5};
          const b=document.querySelector('.bq .bubble'),c=document.querySelector('.bq-cap .tx'),x=document.querySelector('.bq-cap .icon-btn');
          return {text:b&&b.textContent,cap:c&&c.textContent,bubble:inside(b),cap_in:inside(c),x:inside(x),strip:V('.strip'),users:document.querySelectorAll('.um').length}})()"""
        load("c-3-termine")
        cdp.js("document.getElementById('composer-input').focus()"); cdp.call("Input.insertText", text="vrai-code jt3x6"); key("Enter"); time.sleep(.05)
        sent = [m for m in cdp.js("window.__posted") if m["type"] == "send"]
        host({"type": "queue", "items": [{"id": "q_a1", "text": "vrai-code jt3x6", "after": "p.hero-text"}]})
        host({"type": "strip", "id": "wait", "tone": "neutral", "text": "Commencera après le travail en cours sur p.hero-text."})
        time.sleep(.4)
        q63 = cdp.js(Q63_JS)
        check(len(sent) == 1 and sent[0]["text"] == "vrai-code jt3x6" and q63 == {"text": "vrai-code jt3x6", "cap": "Commencera après le travail en cours sur p.hero-text", "bubble": True, "cap_in": True, "x": True, "strip": 0, "users": 1},
              "monkey 3 #63 (bubble with an exchange): a message sent during another element's turn is seen at once in this bubble — waiting bubble, « Commencera après… », « × », all inside the surface", json.dumps(q63, ensure_ascii=False))
        # #63/#70 (the build's case: the host answered nothing for seconds while it handed the agent to the next element): the
        # message sent from the bubble stays seen, dimmed, until the host echoes it (message.user) or queues it — then one only
        load("c-3-termine")
        cdp.js("document.getElementById('composer-input').focus()"); cdp.call("Input.insertText", text="vrai-code jt3x9"); key("Enter"); time.sleep(.4)
        o1 = cdp.js(Q63_JS)
        host({"type": "queue", "items": [{"id": "q_a2", "text": "vrai-code jt3x9", "after": "p.hero-text"}]}); time.sleep(.1)
        o2 = cdp.js("[document.querySelectorAll('.bq').length, document.querySelector('.bq-cap .tx').textContent]")
        host({"type": "queue", "items": []}); host({"type": "message.user", "id": "u9", "text": "vrai-code jt3x9", "at": "2026-11-22T10:00:00Z", "elements": []}); time.sleep(.1)
        o3 = cdp.js("[document.querySelectorAll('.bq').length, document.querySelectorAll('.um').length]")
        check(o1["text"] == "vrai-code jt3x9" and o1["bubble"] and o1["users"] == 1 and o2 == [1, "Commencera après le travail en cours sur p.hero-text"] and o3 == [0, 2],
              "monkey 3 #70: before any answer of the host the sent message is already seen, dimmed, in the bubble; the host's queue then replaces it (one only), its echo ends it", json.dumps([o1, o2, o3], ensure_ascii=False))
        # H1 c342b23: a message held while the element's image gets ready comes as the queue item « held » (no after): the
        # same waiting bubble, the discreet « Envoi dans un instant… », « × » = queue.remove held; the next queue shows the others
        load("c-3-termine")
        host({"type": "queue", "items": [{"id": "held", "text": "vrai-code jt3x7"}, {"id": "q_b1", "text": "et le sous-titre", "after": "p.hero-text"}]}); time.sleep(.1)
        hq = cdp.js(Q63_JS)
        caps = cdp.js("Array.from(document.querySelectorAll('.bq-cap .tx')).map(n=>n.textContent)")
        click(".bq-cap .icon-btn")
        check(hq["text"] == "vrai-code jt3x7" and hq["bubble"] and hq["cap_in"] and hq["x"] and caps == ["Envoi dans un instant…", "Commencera après le travail en cours sur p.hero-text"] and "queue.remove:held" in posted(),
              "H1 queue « held »: a message held for the element's image waits in the bubble with « Envoi dans un instant… » and its « × » (queue.remove held); every item received is shown", json.dumps([hq, caps], ensure_ascii=False))
        # GRAVE (Q, build 0.3.546): Commentaire, bubble on h1.hero-title, a sent and finished turn, then « Chat à droite » — the
        # host closes the bubble for real (bubble.hidden: inert, aria-hidden, b-out b-gone) then lays the panel out: the panel
        # must be alive at once — not inert, nothing hidden, a click in the input reaches it, Entrée sends
        load("c-3-termine")
        host({"type": "bubble.hidden"})
        host({"type": "layout", "mode": "panel"}); host({"type": "state", "running": False, "conversation": {"id": "c1", "title": "Bouton de paiement"}})
        host({"type": "thread.reset", "conversationId": "c1", "items": []}); time.sleep(.3)
        alive = cdp.js("""(()=>{const a=document.getElementById('app'),c=document.getElementById('composer-input'),r=c.getBoundingClientRect();
          const hit=document.elementFromPoint(r.left+r.width/2,r.top+r.height/2);
          return {inert:a.inert,hidden:a.getAttribute('aria-hidden'),cls:['b-out','b-gone','b-in'].filter(k=>a.classList.contains(k)),hit:!!hit&&(c===hit||c.contains(hit))}})()""")
        n0 = len(cdp.js("window.__posted"))
        cdp.js("document.getElementById('composer-input').focus()"); cdp.call("Input.insertText", text="Depuis le panneau"); key("Enter"); time.sleep(.15)
        sent_p = [m for m in cdp.js("window.__posted")[n0:] if m["type"] == "send"]
        check(alive == {"inert": False, "hidden": None, "cls": [], "hit": True} and len(sent_p) == 1 and sent_p[0]["text"] == "Depuis le panneau",
              "GRAVE (Q 0.3.546): after a real closing of the bubble (bubble.hidden), the panel laid out by « Chat à droite » is alive — not inert, not hidden, the input takes the click, Entrée sends", json.dumps([alive, sent_p], ensure_ascii=False))
        # B 1.9 (R3): menus are drop-downs above their button that may leave the bubble; the window grows around the surface
        geo_js = """(()=>{const m=document.querySelector('.menu'),r=m.getBoundingClientRect(),a=document.getElementById('app').getBoundingClientRect(),cs=getComputedStyle(m);
          return {mTop:Math.round(r.top),mBottom:Math.round(r.bottom),mLeft:Math.round(r.left),mRight:Math.round(r.right),w:Math.round(r.width),h:Math.round(r.height),aTop:Math.round(a.top),aLeft:Math.round(a.left),aH:Math.round(a.height),
          winH:document.documentElement.getBoundingClientRect().height,winW:document.documentElement.getBoundingClientRect().width,scroll:m.scrollHeight-m.clientHeight,shadow:cs.boxShadow,gap:cs.rowGap,
          ask:window.__posted.filter(x=>x.type==='bubble.size').pop(),inset:window.__inset}})()"""
        load("c-menu-modele")
        g = cdp.js(geo_js)
        check(g["mBottom"] <= g["aTop"] + 84 and g["mTop"] < g["aTop"] and g["mTop"] >= 0 and g["mBottom"] <= g["winH"] and g["mLeft"] >= 0 and g["mRight"] <= g["winW"] and g["scroll"] <= 0 and g["aH"] == 84 and g["w"] == 280,
              "bubble (menu, R3): the model menu (280 px, #128) opens above its button, out of the bubble, whole (nothing cut, nothing scrolled); the surface keeps its 84 px", json.dumps(g)[:300])
        check(g["ask"]["height"] == 84 and g["ask"].get("overflowTop", 0) >= g["aTop"] - g["mTop"] and g["aTop"] == g["inset"]["top"] and g["shadow"] == "none" and g["gap"] == "0px",
              "bubble (menu, R3): `bubble.size` keeps the height and asks `overflowTop`; the surface is drawn at the granted inset; Paper #128 A (1USM-0): no shadow, rows touching", json.dumps(g["ask"]) + " " + json.dumps(g["inset"]))
        key("Escape")
        last = sizes()[-1]; lastm = of("bubble.size")[-1]
        check(cdp.js("!document.querySelector('.menu')") and last == [305, 84] and not any(k.startswith("overflow") for k in lastm) and "close" not in posted() and cdp.js("Math.round(document.getElementById('app').getBoundingClientRect().top)") == cdp.js("window.__inset.top"),
              "bubble (menu): Échap closes the menu only; the window shrinks back to the surface (no overflow)", json.dumps(lastm))
        load("c-menu-refuse")
        g = cdp.js(geo_js)
        check(g["inset"]["top"] <= 40 and g["mTop"] > g["aTop"] + 60 and g["mBottom"] <= g["winH"] and (g["ask"].get("overflowBottom") or 0) > 0 and g["scroll"] <= 0,
              "bubble (menu, R3): when the host cannot give the room above (screen edge: 40 px), the menu turns under its button and asks the room below", json.dumps(g)[:300])
        # Effort and Autres modèles: true sub-menus next to the model menu (Paper 6.4: 17VU-0 beside 17US-0)
        sub_js = """(()=>{const m=document.querySelector('.menu.models:not(.sub-menu)'),s=document.querySelector('.sub-menu');if(!m||!s)return null;const a=m.getBoundingClientRect(),b=s.getBoundingClientRect();
          return {gapR:Math.round(b.left-a.right),gapL:Math.round(a.left-b.right),top:Math.round(b.top-a.top),w:Math.round(b.width),inWin:b.left>=0&&b.right<=document.documentElement.getBoundingClientRect().width,
          rows:Array.from(s.querySelectorAll('.mi .lb')).map(n=>n.textContent),back:s.querySelectorAll('.mi.back').length,mainRows:m.querySelectorAll('.mi').length,
          exp:(m.querySelector('.mi[aria-expanded=true]')||{dataset:{}}).dataset.key||'',focus:document.activeElement.closest('.sub-menu')?document.activeElement.textContent.trim():'',ask:window.__posted.filter(x=>x.type==='bubble.size').pop()}})()"""
        load("c-menu-effort")
        se = cdp.js(sub_js)
        check(se and se["gapR"] == 4 and se["top"] == 0 and se["w"] == 214 and se["inWin"] and se["rows"] == ["Faible", "Moyen", "Élevé", "X-Élevé", "Max"] and se["back"] == 0 and se["mainRows"] >= 7
              and se["exp"] == "effort" and se["focus"].startswith("Élevé") and (se["ask"].get("overflowRight") or 0) > 0,
              "sub-menu (Paper 6.4): Effort opens beside the model menu (on its right, 4 px, tops aligned, 214 px, no « ‹ Retour »), the window grows on the right; focus on the chosen level", json.dumps(se, ensure_ascii=False)[:300])
        key("ArrowLeft")
        check(cdp.js("!document.querySelector('.sub-menu') && !!document.querySelector('.menu.models') && document.activeElement.dataset.key==='effort'"), "sub-menu: ArrowLeft closes it, the model menu stays, focus back on « Effort »")
        key("ArrowRight")
        check(cdp.js("!!document.querySelector('.sub-menu') && !!document.activeElement.closest('.sub-menu')"), "sub-menu: ArrowRight on « Effort » opens it again, focus inside")
        key("Escape")
        check(cdp.js("!document.querySelector('.sub-menu') && !!document.querySelector('.menu.models')") and "close" not in posted(), "sub-menu: Échap closes the sub-menu only")
        key("ArrowRight"); key("ArrowDown"); key("Enter")
        check(cdp.js("!document.querySelector('.menu')") and any(x.startswith("setEffort") for x in posted()), "sub-menu: Entrée on a level sets it and closes both menus", str(posted()[-2:]))
        load("c-menu-modele")
        c = center(".menu .mi[data-key=effort]"); move(*c); time.sleep(.4)
        hv = cdp.js("[!!document.querySelector('.sub-menu'), document.activeElement.closest('.sub-menu')?1:0]")
        c2 = center(".menu .mi.mm-row"); move(*c2); time.sleep(.5)   # #128: the first model row (the list starts with a provider's header)
        check(hv == [True, 0] and cdp.js("!document.querySelector('.sub-menu')"), "sub-menu: hovering « Effort » opens it (the focus stays), moving to another row closes it", str(hv))
        load("c-menu-autres")   # #128 (Paper A): every model in the list — the 4th Claude model too, no « Autres modèles » sub-menu
        sa = cdp.js("(()=>({rows:Array.from(document.querySelectorAll('.menu.mm-list .mi .lb')).map(n=>n.textContent),sub:!!document.querySelector('.sub-menu')}))()")
        check(sa["rows"] == ["Opus 5.5", "Sonnet 5.5", "Haiku 5.5", "Opus 4.1", "6.1 Sol", "6.1 Terra", "6.1 Luna", "Effort"] and not sa["sub"],
              "sub-menu (#128): no « Autres modèles » — a 4th model is in the list itself, nothing opens beside it", json.dumps(sa, ensure_ascii=False)[:240])
        load("c-menu-effort-gauche")
        sl = cdp.js(sub_js)
        check(sl and sl["gapL"] == 4 and sl["top"] == 0 and sl["inWin"] and (sl["ask"].get("overflowRight") or 0) == 0 and (sl["ask"].get("overflowLeft") or 0) > 0,
              "sub-menu: no room on the right (screen edge), it opens on the left of the menu (overflowLeft)", json.dumps(sl, ensure_ascii=False)[:240])

        load("c-menu-mode")
        mm = cdp.js("""(()=>{const m=document.querySelector('.menu');return {rows:Array.from(m.querySelectorAll('.mi')).map(b=>[b.querySelector('.lb').textContent,(b.querySelector('.ds')||{}).textContent||'']),
          icons:Array.from(m.querySelectorAll('.mi .lead')).map(i=>{const r=i.getBoundingClientRect(),cs=getComputedStyle(i);return [Math.round(r.width),cs.color,cs.opacity]}),
          checked:(m.querySelector('.mi[aria-checked=true] .lb')||{}).textContent,w:Math.round(m.getBoundingClientRect().width),shadow:getComputedStyle(m).boxShadow,gap:getComputedStyle(m).rowGap,
          rowH:Math.round(m.querySelector('.mi').getBoundingClientRect().height)}})()""")
        check(mm["rows"] == [["Accept edits", "Automatically accept all file edits"], ["Auto", "Claude handles permission decisions"], ["Demander", "Propose chaque modification."], ["Planifier", "Propose un plan, ne touche à aucun fichier."]],
              "modes (R4, Paper 14RK-0): exactly Accept edits, Auto, Demander, Planifier with Paper's words, in that order", json.dumps(mm["rows"], ensure_ascii=False))
        check(mm["icons"] == [[16, "rgb(62, 103, 255)", "1"], [16, "rgb(62, 103, 255)", "1"], [15, "rgb(255, 255, 255)", "0.85"], [15, "rgb(255, 255, 255)", "0.85"]] and mm["checked"] == "Demander" and mm["w"] == 290
              and mm["shadow"] == "rgba(0, 0, 0, 0.5) 0px 16px 48px 0px" and mm["gap"] == "2px" and mm["rowH"] == 51,
              "modes (R4, Paper 14RK-0): double chevron and bolt 16 px #3E67FF, shield and list 15 px white 85 %, 290 px, shadow 0 16 48, rows 51 px, 2 px apart", json.dumps(mm)[:300])
        load("c-menu-au-dessus")
        up = cdp.js("(()=>{const a=document.getElementById('app').getBoundingClientRect(),c=document.querySelector('.composer').getBoundingClientRect(),b=document.querySelector('.tb.mode').getBoundingClientRect(),m=document.querySelector('.menu').getBoundingClientRect();return [Math.round(a.bottom-c.bottom), m.bottom<=b.top, m.top>=0]})()")
        check(up == [12, True, True] and sizes()[-1][1] == 84, "bubble (above its element): content sits on the bottom edge, the menu opens upward out of the bubble", f"{up} {sizes()[-1]}")
        load("c-menu-plus")
        pl = cdp.js("Array.from(document.querySelectorAll('.menu .mi .lb')).map(n=>n.textContent)")
        check(any(x.startswith("attachImage") for x in posted()) and cdp.js("!document.querySelector('.menu')"), "« + » (R22): its only action at once — the image picker (attachImage), no menu")
        load("c-menu-plus-incertain")
        pl2 = cdp.js("Array.from(document.querySelectorAll('.menu .mi .lb')).map(n=>n.textContent)")
        check(pl == [] and pl2 == ["Joindre une image", "Choisir le dossier…"], "« + » (R5, R22): with a folder found for sure, « + » opens no menu; when the browser is not sure, « Joindre une image » and « Choisir le dossier… »", f"{pl} / {pl2}")
        load("c-infobulle-envoyer")
        cdp.js("document.getElementById('composer-input').focus()")
        for _ in range(4): key("Tab")
        time.sleep(.3)
        st_ = cdp.js("""(()=>{const t=document.getElementById('send-tip'),r=t.getBoundingClientRect(),b=document.querySelector('.send').getBoundingClientRect(),a=document.getElementById('app').getBoundingClientRect();
          return {on:getComputedStyle(t).opacity,text:t.innerText,top:Math.round(r.top),bottom:Math.round(b.top-r.bottom),right:Math.round(b.right-r.right),w:r.width,cut:t.scrollWidth>t.clientWidth+1,inLayer:t.parentNode.className,above:r.top<a.top}})()""")
        check(st_["on"] == "1" and "Garder en annotation" in st_["text"] and "Ctrl" in st_["text"] and st_["bottom"] == 6 and st_["right"] == 0 and st_["top"] >= 0 and not st_["cut"] and st_["inLayer"] == "layer" and st_["above"],
              "bubble (send button, R3): « Demander à l'agent ↵ / Garder en annotation Ctrl ↵ » opens above the button, out of the bubble, never cut", json.dumps(st_, ensure_ascii=False)[:240])
        cdp.js("document.activeElement.blur()"); time.sleep(.3)
        check(cdp.js("getComputedStyle(document.getElementById('send-tip')).opacity") == "0" and not any(k.startswith("overflow") for k in of("bubble.size")[-1]), "bubble (send button): it goes with the focus and the window shrinks back")

        # R9 (page side): the waiting request the host sends again after its thread.reset is shown again, once, focused
        load("c-autorisation-reouverte")
        ro = cdp.js("[document.querySelectorAll('.bperm').length, document.getElementById('perm-t').textContent, document.activeElement.classList.contains('bperm')]")
        check(ro == [1, "Claude veut modifier checkout.css", True], "bubble (R9): reopened (thread.reset), the waiting permission card is back — once, the card focused (SEC-23: never « Autoriser »)", str(ro))
        # D4 (audit): the agent's raw titles become Paper's French labels, the file's name alone, « … » while running
        load("c-etapes-brutes")
        st4 = cdp.js("document.querySelector('.bt-stw .lb').textContent")
        c = center(".bt-st"); move(*c); time.sleep(.6)
        rows4 = cdp.js("Array.from(document.querySelectorAll('#b-tip .r')).map(r=>r.textContent)")
        check(st4 == "Modifie landing.css…" and rows4 == ["A lu index.html", "A utilisé 1 outil MCP", "A lancé npm run build"],
              "bubble (D4, Paper 1B30-0 / 1AIE-0): « Edit styles\\landing.css » is said « Modifie landing.css… »; done steps « A lu index.html », « A utilisé 1 outil MCP », « A lancé npm run build »", f"{st4} / {rows4}")
        move(370, 690)
        # B 1.9: a second turn.start of the same turn only updates its header (the host learns model and effort late)
        load("c-2-envoye")
        n0 = cdp.js("[document.querySelectorAll('.turn').length, document.querySelector('.bt-text .text').textContent, document.querySelectorAll('.marker').length]")
        host({"type": "turn.start", "turnId": "t1", "model": "Opus 5.5", "effort": "medium", "agentId": "claude-code", "at": "2026-11-19T10:00:00Z"})
        n1 = cdp.js("[document.querySelectorAll('.turn').length, document.querySelector('.bt-text .text').textContent, document.querySelectorAll('.marker').length, document.querySelector('.bt-model').textContent, !!document.querySelector('.bt-model svg'), document.querySelector('.bt-stw .lb').textContent]")
        check(n1[:3] == n0 and n1[3] == "Opus 5.5Moyen" and n1[4] and n1[5] == "Lit checkout.css…", "bubble (B 1.9): a second `turn.start` of the same turn updates « modèle · effort » only — same turn, same text, same step, no marker", f"{n0} -> {n1}")
        # cv3 — R20: 320 px; R13: the model button always names a model; R17: one card per message; R18: slide; R19: still surface; R21: drawn mode name
        load("c-largeur-320")
        check(sizes()[-1][0] == 320 and cdp.js("Math.round(document.getElementById('app').getBoundingClientRect().width)") == 320, "bubble (R20): 320 px wide (user's decision over Paper's 305)", str(sizes()[-1]))
        load("c-modele-inconnu")
        mb = cdp.js("[document.querySelector('.model .mn').textContent, !!document.querySelector('.tb.mode')]")
        click(".model .m")
        rows13 = cdp.js("Array.from(document.querySelectorAll('.menu .mi .lb')).map(n=>n.textContent)")
        check(mb == ["Sonnet 5.5", True] and "Codex" not in rows13 and "Claude Code" not in rows13 and "Sonnet 5.5" in rows13, "bubble (R13): mode on the left, the model button names a model (the agent's default) — never « Claude Code » / « Codex » as a choice", f"{mb} {rows13}")
        load("c-messages-multiples")
        cards = cdp.js("Array.from(document.querySelectorAll('.turn.bt > .bt-text, .turn.bt > .bt-bar')).map(c=>{const s=getComputedStyle(c);return [c.className.replace('bt-text','').trim(),s.borderTopLeftRadius,s.borderTopRightRadius,s.borderBottomRightRadius,s.borderBottomLeftRadius].join(' ')})")
        gaps = cdp.js("(()=>{const c=Array.from(document.querySelectorAll('.turn.bt > .bt-text, .turn.bt > .bt-bar'));return c.slice(1).map((x,i)=>Math.round(x.getBoundingClientRect().top-c[i].getBoundingClientRect().bottom))})()")
        check(cards == ["c-top 4px 10px 4px 4px", "c-mid 4px 4px 4px 4px", "bt-bar 4px 4px 10px 10px"] and gaps == [2, 2], "bubble (R17, Paper 1AZV-0): one card per agent message, 2 px apart; corners 4/10/4/4, then 4, the bar last 4/4/10/10", f"{cards} {gaps}")
        load("c-messages-deux")
        cards2 = cdp.js("Array.from(document.querySelectorAll('.turn.bt > .bt-text')).map(c=>{const s=getComputedStyle(c);return [s.borderTopLeftRadius,s.borderTopRightRadius,s.borderBottomRightRadius,s.borderBottomLeftRadius].join(' ')})")
        check(cards2 == ["4px 10px 4px 4px", "4px 4px 16px 12px"], "bubble (R17): without a bar, the last card closes the group (4/4/16/12)", str(cards2))
        load("c-1-vide")
        host({"type": "layout", "mode": "bubble", "maxWidth": 320, "maxHeight": 520, "side": "right", "element": "button.btn-primary"})
        an = cdp.js("(()=>{const a=document.getElementById('app'),s=getComputedStyle(a);return [s.animationName, s.animationDuration, a.style.getPropertyValue('--b-dx')]})()")
        host({"type": "layout", "mode": "bubble", "maxWidth": 320, "maxHeight": 520, "side": "left", "element": "button.btn-primary"})
        an2 = cdp.js("document.getElementById('app').style.getPropertyValue('--b-dx')")
        check(an == ["b-in", "0.25s", "-12px"] and an2 == "12px", "bubble (R18): it slides in from the element's side (12 px, 250 ms — cv4 R30) — no scale", f"{an} {an2}")
        load("c-menu-modele")
        r0 = cdp.js("(()=>{const r=document.getElementById('app').getBoundingClientRect(),c=document.getElementById('composer-input').getBoundingClientRect();return [r.left,r.top,r.width,r.height,c.top]})()")
        key("Escape"); time.sleep(.15)
        r1 = cdp.js("(()=>{const r=document.getElementById('app').getBoundingClientRect(),c=document.getElementById('composer-input').getBoundingClientRect();return [r.left,r.top,r.width,r.height,c.top]})()")
        click(".model .m"); time.sleep(.15)
        r2 = cdp.js("(()=>{const r=document.getElementById('app').getBoundingClientRect(),c=document.getElementById('composer-input').getBoundingClientRect();return [r.left,r.top,r.width,r.height,c.top]})()")
        check(r0 == r1 == r2, "bubble (R19): opening and closing a menu never moves nor resizes the surface or its input (same box to the pixel)", f"{r0} {r1} {r2}")
        load("c-1-vide")
        c = center(".tb.mode"); move(*c); time.sleep(.6)
        mt = cdp.js("(()=>{const t=document.getElementById('mode-tip'),b=document.querySelector('.tb.mode').getBoundingClientRect(),r=t.getBoundingClientRect();return [getComputedStyle(t).opacity, t.textContent, Math.round(b.top-r.bottom), Math.round(r.left-b.left)]})()")
        check(mt == ["1", "Demander", 6, 0], "bubble (R21): hovering the mode's icon shows its name in the drawn bubble, 6 px above, left edges aligned", str(mt))
        move(c[0], c[1] + 300); time.sleep(.3)
        # cv4 — R28: the bottom of the input is right from the first image and never loses its effort nor its chevron
        load("c-1-vide")
        f0 = cdp.js("(()=>{const m=document.querySelector('.model .m'),e=document.querySelector('.model .e');return [m.textContent, !e.hidden, getComputedStyle(m.querySelector('.mcv')).display!=='none', Math.round(m.getBoundingClientRect().height)]})()")
        host({"type": "state", "agents": [{"id": "claude-code", "name": "Claude Code", "icon": "claude", "status": "starting"}], "efforts": [], "model": "sonnet", "effort": "high"})
        f1 = cdp.js("(()=>{const m=document.querySelector('.model .m'),e=document.querySelector('.model .e');return [m.textContent, !e.hidden, getComputedStyle(m.querySelector('.mcv')).display!=='none', Math.round(m.getBoundingClientRect().height)]})()")
        check(f0 == ["Sonnet 5.5Élevé", True, True, 24] and f1 == f0 and sizes()[-1] == [305, 84], "bubble (cv4 R28): model, effort and chevron shown at once and kept when the agent's state comes back without its lists — nothing changes size", f"{f0} -> {f1} {sizes()[-1]}")
        # fix 0.3.690 (R13): a new profile, no saved lists, the agent being prepared — the button names the pinned adapter's
        # default model, never the tool; the agent's live list replaces it, the cached choice wins over the fallback
        load("c-profil-neuf")
        MB = "(()=>{const m=document.querySelector('.model .m'),e=document.querySelector('.model .e');return [m.querySelector('.mn').textContent, e.hidden?'':e.textContent.trim(), Math.round(m.getBoundingClientRect().height)]})()"
        n0 = cdp.js(MB); txt0 = cdp.js("document.body.innerText")
        check(n0 == ["Opus 5.5", "Moyen", 24] and "Claude Code" not in txt0, "bubble (fix 0.3.690 R13): new profile without saved lists — the button names the fallback model « Opus 5.5 · Moyen », never « Claude Code »", f"{n0} {'Claude Code' in txt0}")
        host({"type": "state", "agents": [{"id": "claude", "name": "Claude Code", "icon": "claude", "status": "ready", "models": [{"id": "opus", "label": "Opus 5.5", "isDefault": True}, {"id": "sonnet", "label": "Sonnet 5.5"}]}], "agentId": "claude", "model": "sonnet", "efforts": [{"id": "low", "label": "Low"}, {"id": "high", "label": "High"}], "effort": "high"})
        n1 = cdp.js(MB)
        check(n1 == ["Sonnet 5.5", "Élevé", 24], "bubble (fix 0.3.690 R13): the cached / live list replaces the fallback — the remembered model and effort, same height", str(n1))
        load("profil-neuf")
        host({"type": "turn.start", "turnId": "t1", "agentId": "claude", "at": "2026-11-23T10:00:00Z"})
        tl = cdp.js("(()=>{const t=document.querySelector('.turn[data-turn=t1]'),m=t.querySelector('.meta'),w=t.querySelector('.working'),l=t.querySelector('.mlogo').getBoundingClientRect(),x=(m.hidden?w:m).getBoundingClientRect();return [m.hidden?'':m.textContent, Math.abs((l.top+l.bottom)/2-(x.top+x.bottom)/2)<=1, t.querySelectorAll('.mlogo').length, w.textContent.startsWith('Travaille')]})()")
        check(tl == ["Opus 5.5", True, 1, True] and "Claude Code" not in cdp.js("document.body.innerText"), "panel (fix 0.3.690): a turn started before any model is known names the fallback model; the logo is on that line, centred, never alone above « Travaille… »", str(tl))
        # R29: a new card of the agent comes in softly
        load("c-2-envoye")
        host({"type": "message.agentChunk", "turnId": "t1", "messageId": "m9", "text": "Je vérifie aussi la largeur sur mobile."})
        nc = cdp.js("(()=>{const c=Array.from(document.querySelectorAll('.turn.bt > .bt-text'));const l=c[c.length-1];return [c.length, l.classList.contains('enter'), getComputedStyle(l).animationName]})()")
        check(nc == [2, True, "rise"], "bubble (cv4 R29): a new message of the agent is a new card that comes in softly (rise 200 ms), the cards above do not move", str(nc))
        # R35: a draft per conversation — the panel's chips never reach a bubble, each element keeps its own words
        load("brouillons")
        dr = cdp.js("window.__drafts")
        check(dr == {"bubbleA": "", "chipsInBubble": 0, "bubbleB": "", "backToA": "Texte pour A", "panel": "div.hero-visual Pour le panneau"},
              "bubble (cv4 R35): the panel's chip and words stay in the panel's draft; each element's bubble keeps its own words; a bubble never shows a chip", json.dumps(dr, ensure_ascii=False))
        # cv5 R40: the diff keeps the bubble's size; its handle resizes it within resizeMin / resizeMax; closing gives the size back
        load("c-diff-redimension")
        g0 = cdp.js("(()=>{const g=document.querySelector('.b-grip'),r=g.getBoundingClientRect(),a=document.getElementById('app').getBoundingClientRect(),cs=getComputedStyle(g);return [!g.hidden, Math.round(a.right-r.right), Math.round(a.bottom-r.bottom), cs.cursor, getComputedStyle(g.querySelector('path')).stroke, Math.round(g.querySelector('svg').getBoundingClientRect().width)]})()")
        s0 = sizes()[-1]
        host({"type": "layout", "mode": "bubble", "open": False, "maxWidth": 320, "maxHeight": 520, "resizeMax": {"width": 480, "height": 600}, "resizeMin": {"width": 320, "height": s0[1]}, "inset": cdp.js("window.__inset")})
        def drag(dx, dy):
            cdp.js(f"(()=>{{const g=document.querySelector('.b-grip');const ev=(t,x,y)=>g.dispatchEvent(new PointerEvent(t,{{bubbles:true,pointerId:7,button:0,buttons:t==='pointerup'?0:1,screenX:x,screenY:y,pointerType:'mouse'}}));ev('pointerdown',500,500);ev('pointermove',{500 + dx},{500 + dy});ev('pointerup',{500 + dx},{500 + dy});}})()"); time.sleep(.15)
        drag(100, 150)
        big = of("bubble.size")[-1]
        bw = cdp.js("Math.round(document.getElementById('app').getBoundingClientRect().width)")
        drag(400, 900)
        capped = of("bubble.size")[-1]
        drag(-900, -900)
        floor = of("bubble.size")[-1]
        click(".bview-x"); time.sleep(.15)
        back = of("bubble.size")[-1]
        check(g0 == [True, 4, 4, "nwse-resize", "rgba(255, 255, 255, 0.3)", 10] and s0[0] == 320, "bubble (cv5 R40): the diff opens at the bubble's size; its handle (two strokes, white 30 %, 10 px) sits bottom right with a resize cursor", f"{g0} {s0}")
        check(big.get("resized") is True and [big["width"], big["height"]] == [420, s0[1] + 150] and bw == 420 and [capped["width"], capped["height"]] == [480, 600] and [floor["width"], floor["height"]] == [320, s0[1]],
              "bubble (cv5 R40): dragging sends bubble.size {resized: true} at each image, bounded by resizeMin (320 × the height of before) and resizeMax", f"{big} {capped} {floor}")
        check("resized" not in back and [back["width"], back["height"]] == [320, s0[1]] and cdp.js("Math.round(document.getElementById('app').getBoundingClientRect().width)") == 320, "bubble (cv5 R40): closing the diff sends the size of before (no resized)", json.dumps(back))
        # cv5 R41: from one menu to another, the surface transforms (no exit), the region covers both
        load("c-menus-morph")
        m0 = cdp.js("(()=>{const r=document.querySelector('.menu').getBoundingClientRect();return [r.left,r.top,r.width,r.height]})()")
        n0 = len(of("bubble.size"))
        cdp.js("document.querySelector('.tb.mode').dispatchEvent(new PointerEvent('pointerdown',{bubbles:true}));document.querySelector('.tb.mode').click()"); time.sleep(.03)
        mid = cdp.js("(()=>{const ms=Array.from(document.querySelectorAll('.menu'));return ms.map(m=>[m.className.replace('menu ','').trim(), m.getAttribute('role')||'', Math.round(m.getBoundingClientRect().width)])})()")
        surf = cdp.js("(()=>{const m=document.querySelector('.menu.morph-in');const s=getComputedStyle(m);return [s.animationName, s.opacity, s.backgroundColor]})()")
        union = of("bubble.size")[n0:]
        first_ask = union[0] if union else {}
        time.sleep(.35)
        m1 = cdp.js("(()=>{const ms=document.querySelectorAll('.menu');const r=ms[0].getBoundingClientRect();return [ms.length, ms[0].getAttribute('aria-label'), Math.round(r.width), ms[0].style.transform, ms[0].classList.contains('morph-in')]})()")
        check(len(mid) == 2 and "menu-ghost" in mid[0][0] and mid[0][1] == "" and "morph-in" in mid[1][0] and 280 <= mid[1][2] <= 290 and m1 == [1, "Mode", 290, "", False],   # #128: from 280
              "bubble (cv5 R41): model menu → mode menu: the old one stays as a ghost (no surface, content fading), the new one grows from its box to 290 px in 200 ms; then a single menu", f"{mid} {m1}")
        final = cdp.js("(()=>{const a=document.getElementById('app').getBoundingClientRect(),r=document.querySelector('.menu').getBoundingClientRect();return [Math.round(a.top-r.top)+32, Math.round(r.right-a.right)+48]})()")
        during = of("bubble.size")[n0:]
        check(surf == ["none", "1", "rgb(36, 36, 36)"], "bubble (cv5 R41): the transforming menu's surface stays opaque (no entrance fade on it) — only its content cross-fades", str(surf))
        check(first_ask.get("overflowTop", 0) >= final[0] - 1 and all(u.get("overflowTop", 0) >= final[0] - 1 for u in during[:-1]),
              "bubble (cv5 R41): from the first image of the transformation, the region asked covers the new menu's FINAL box (shadow included), and shrinks only after the end", f"final top {final[0]} / asks {[u.get('overflowTop', 0) for u in during]}")
        # R41 (H1's measure): no image with a translucent menu surface, from the click to 500 ms after (end of the morph included)
        load("c-menus-morph")
        op = cdp.js("""new Promise(r=>{const b=document.querySelector('.tb.mode');b.dispatchEvent(new PointerEvent('pointerdown',{bubbles:true}));b.click();
          const t0=performance.now(),seen=[];const f=()=>{const m=document.querySelector('.menu:not(.menu-ghost)');if(m)seen.push(+getComputedStyle(m).opacity);
          if(performance.now()-t0<520)requestAnimationFrame(f);else r({frames:seen.length,min:Math.min(...seen)})};requestAnimationFrame(f)})""")
        check(op["frames"] >= 20 and op["min"] == 1, "bubble (cv5 R41): the new menu's surface is opaque at every image from the click to 500 ms after — the entrance fade never replays at the end", json.dumps(op))
        # R43: announcements are grey follow-up lines during the turn; the last text is the answer (a card); at the end the lines
        # close softly and join « Historique »
        load("c-suivi")
        fl = cdp.js("""(()=>{const ls=Array.from(document.querySelectorAll('.turn.bt > .bt-follow'));return ls.map(l=>{const t=l.querySelector('.text'),c=getComputedStyle(t),b=getComputedStyle(l),i=l.querySelector('.fl-ic');
          return [t.textContent, c.fontSize+'/'+c.lineHeight, c.color, b.backgroundColor, i.className.replace('fl-ic ',''), getComputedStyle(i.querySelector('svg')).width]})})()""")
        check(fl == [["Je cherche cette phrase dans landing.html.", "13px/20px", "rgba(255, 255, 255, 0.5)", "rgba(0, 0, 0, 0)", "done", "12px"],
                     ["Je raccourcis la phrase dans landing.html.", "13px/20px", "rgba(255, 255, 255, 0.5)", "rgba(0, 0, 0, 0)", "run", "12px"]],
              "bubble (R43): each announcement before an action is a grey line without a card (13/20, #FFFFFF80), a 12 px check once done, a ring while it runs", json.dumps(fl, ensure_ascii=False))
        host({"type": "toolCallUpdate", "id": "s2", "status": "completed"})
        host({"type": "message.agentChunk", "turnId": "t1", "messageId": "a3", "text": "J'ai raccourci le texte de la carte."})
        host({"type": "turn.end", "turnId": "t1", "stopReason": "end_turn", "durationMs": 9000, "changes": [{"path": "landing.html", "status": "modified", "added": 1, "removed": 1}]})
        mid43 = cdp.js("[document.querySelectorAll('.turn.bt > .fl-closing').length, getComputedStyle(document.querySelector('.fl-closing')||document.body).transitionDuration]")
        time.sleep(.4)
        end43 = cdp.js("[document.querySelectorAll('.turn.bt > .bt-follow, .turn.bt > .fl-closing').length, Array.from(document.querySelectorAll('.turn.bt > .bt-text')).map(c=>c.textContent), !!document.querySelector('.bt-bar')]")
        c = center(".bt-st.done"); move(*c); time.sleep(.6)
        h43 = cdp.js("Array.from(document.querySelectorAll('#b-tip .r')).map(r=>r.textContent)")
        move(c[0], c[1] + 300)
        check(mid43[0] == 2 and "0.25s" in mid43[1] and end43 == [0, ["J'ai raccourci le texte de la carte."], True], "bubble (R43): at the end of the turn the lines close (250 ms); only the answer card and the changes bar stay", f"{mid43} {end43}")
        check(h43 == ["Je cherche cette phrase dans landing.html.", "A lu landing.html", "Je raccourcis la phrase dans landing.html.", "A modifié landing.html"], "bubble (R43): the announcements join « Historique », in their order", str(h43))
        # R42: same provider = same agent even when the state came back (other objects); the thread says « Passé à … »
        load("c-1-vide")
        cdp.js("window.devHost.send(JSON.parse(JSON.stringify({type:'state',agents:[{id:'claude-code',name:'Claude Code',icon:'claude',status:'ready',models:[{id:'opus',label:'Opus 5.5'},{id:'sonnet',label:'Sonnet 5.5',isDefault:true},{id:'haiku',label:'Haiku 5.5'}]},{id:'codex',name:'Codex',icon:'codex',status:'ready',models:[{id:'terra',label:'6.1 Terra'}]}]})))"); time.sleep(.1)
        click(".model .m")
        chk = cdp.js("(document.querySelector('.menu .mi[aria-checked=true] .lb')||{}).textContent")
        cdp.js("Array.from(document.querySelectorAll('.menu .mi')).find(b=>b.textContent.startsWith('Opus 5.5')).click()"); time.sleep(.1)
        r42 = cdp.js("[window.__posted.filter(m=>m.type==='setModel').map(m=>m.id+'@'+m.agentId), !!document.querySelector('.menu'), document.querySelector('.model .mn').textContent, window.__posted.some(m=>m.type==='setAgent')]")
        check(chk == "Sonnet 5.5" and r42 == [["opus@claude-code"], False, "Opus 5.5", False], "bubble (R42): a model of the same provider applies at once (compared by id, the state having been sent again), no box, no agent change", f"{chk} {r42}")
        host({"type": "model.switch", "agentId": "claude-code", "model": "Opus 5.5", "modelId": "opus", "effort": "Moyen", "effortId": "medium"})
        host({"type": "model.switch", "agentId": "codex", "agentName": "Codex"})
        ms = cdp.js("Array.from(document.querySelectorAll('.mswitch')).map(n=>[n.textContent, getComputedStyle(n).display])")
        check(ms == [["Passé à Opus 5.5 · Moyen", "flex"], ["Passé à Codex", "flex"]], "bubble (R42): `model.switch` draws a discreet grey line « Passé à Opus 5.5 · Moyen » (« Passé à Codex » without a model)", str(ms))
        load("c-modele-change")
        check(cdp.js("Array.from(document.querySelectorAll('.mswitch')).map(n=>n.textContent)") == ["Passé à Opus 5.5 · Moyen"], "bubble (R42): the « Passé à » line comes back with the conversation (thread.reset)")
        # R47: with little room, the whole permission card shows (buttons included); only the thread's top fades, never the card
        for nm in ("c-2-autorisation", "c-autorisation-courte"):
            load(nm)
            pr = cdp.js("""(()=>{const a=document.getElementById('app').getBoundingClientRect(),c=document.querySelector('.bperm').getBoundingClientRect(),f=document.querySelector('.b-fade').getBoundingClientRect(),
              b=Array.from(document.querySelectorAll('.bperm .btn')).map(x=>x.getBoundingClientRect()),s=document.querySelector('.scroller').getBoundingClientRect();
              return {inside:b.every(x=>x.bottom<=a.bottom-11.5&&x.top>=a.top), fadeOverCard:f.bottom>c.top+0.5, fadeInThread:f.top>=s.top-0.5&&f.bottom<=s.bottom+0.5, faded:document.getElementById('app').classList.contains('faded'), h:Math.round(a.height)}})()""")
            check(pr["inside"] and not pr["fadeOverCard"] and pr["fadeInThread"], f"bubble (R47, {nm}): the permission card and its buttons are whole inside the bubble; the fade stays on the thread's top, never on the card", json.dumps(pr))
        # R47 (the user's sequence: a permission in the bubble, the page scrolled up then down): if the host offers less room for a
        # while, then the room of before, the bubble asks its full height again — whole card, nothing faded but the thread's top
        load("c-2-autorisation")
        before = sizes()[-1]
        host({"type": "layout", "mode": "bubble", "open": False, "maxWidth": 305, "maxHeight": 150, "inset": cdp.js("window.__inset")})
        small = sizes()[-1]
        host({"type": "layout", "mode": "bubble", "open": False, "maxWidth": 305, "maxHeight": 303, "inset": cdp.js("window.__inset")})
        after = sizes()[-1]
        back47 = cdp.js("(()=>{const a=document.getElementById('app').getBoundingClientRect(),b=Array.from(document.querySelectorAll('.bperm .btn')).map(x=>x.getBoundingClientRect()),c=document.querySelector('.bperm'),f=document.querySelector('.b-fade').getBoundingClientRect();return [Math.round(a.height), b.every(x=>x.bottom<=a.bottom-11.5), getComputedStyle(c).opacity, f.bottom<=c.getBoundingClientRect().top+0.5]})()")
        check(small[1] < before[1] and after == before and back47 == [before[1], True, "1", True], "bubble (R47): less room for a while, then the room of before — the bubble asks its full height again, the card whole and opaque, the fade off the card", f"{before} {small} {after} {back47}")
        # R48: no typing caret at the end of the agent's text while it streams (bubble and panel)
        load("c-2-envoye")
        host({"type": "message.agentChunk", "turnId": "t1", "messageId": "m1", "text": " Et encore"})
        car = cdp.js("Array.from(document.querySelectorAll('.text, .text *')).map(n=>getComputedStyle(n,'::after').content).filter(c=>c&&c!=='none'&&c!=='normal')")
        check(car == [], "bubble (R48): no blue typing caret after the agent's text while it streams", str(car))
        # ---- review (design/chat-final-review.md), P's defects
        # F10: a request without a refuse option shows no « Refuser » (it did nothing) nor the field that refuses
        load("c-autorisation-sans-refus")
        f10 = cdp.js("[Array.from(document.querySelectorAll('.bperm .btn')).map(b=>b.textContent), !!document.querySelector('.bperm .instead')]")
        check(f10 == [["Autoriser"], False], "review F10 (bubble): a request with no refuse option shows only « Autoriser »", str(f10))
        # M11: a turn replayed without its end while nothing runs ends as stopped (no endless « Travaille… »)
        load("c-tour-interrompu")
        m11 = cdp.js("[document.querySelector('.bt-st .lb')&&document.querySelector('.bt-st .lb').textContent, !!document.querySelector('.bt-st.run'), document.querySelector('.turn').getAttribute('aria-busy')]")
        check(m11 == ["S'est arrêté", False, "false"], "review M11: a turn replayed with no end (browser closed during it) shows « S'est arrêté », not a ring for ever", str(m11))
        # M2 / F4: the host closes the bubble (bubble.hidden): nothing visible nor readable, the old thread is gone before the next opening
        load("c-fil-long")
        host({"type": "bubble.hidden"})
        m2 = cdp.js("(()=>{const a=document.getElementById('app');return [getComputedStyle(a).opacity, a.inert===true, a.getAttribute('aria-hidden'), document.querySelectorAll('.turn').length]})()")
        host({"type": "layout", "mode": "bubble", "maxWidth": 305, "maxHeight": 520, "element": "h1.title"})
        first = cdp.js("[document.querySelectorAll('.turn, .um').length, document.getElementById('app').inert===true, document.getElementById('app').getAttribute('aria-hidden')]")
        check(m2 == ["0", True, "true", 0] and first == [0, False, None], "review M2/F4: bubble.hidden makes the surface invisible, inert, hidden from screen readers and empties the thread; the next opening never shows the old conversation", f"{m2} {first}")
        # F6: words typed in an element's bubble, the bubble closed by a click elsewhere, the element clicked again (a new
        # conversation each time): the words come back
        load("c-1-vide")
        cdp.js("document.getElementById('composer-input').focus()"); cdp.call("Input.insertText", text="Plus grand")
        host({"type": "bubble.hidden"})
        host({"type": "layout", "mode": "bubble", "maxWidth": 305, "maxHeight": 520, "element": "button.btn-primary", "elementKey": "main form > button.btn-primary"})
        host({"type": "thread.reset", "conversationId": "new-2", "items": []})
        f6 = cdp.js("document.getElementById('composer-input').textContent")
        check(f6 == "Plus grand", "review F6: a bubble's unsent words come back when the same element is clicked again (draft kept by element while nothing is sent)", repr(f6))
        # E4 (b): with layout.room (the page area measured by the host), a menu turns before the host has to refuse room
        load("c-1-vide")
        host({"type": "layout", "mode": "bubble", "open": False, "maxWidth": 305, "maxHeight": 520, "inset": cdp.js("window.__inset"), "room": {"top": 40, "right": 400, "bottom": 400, "left": 400}})
        n0 = len(of("bubble.size"))
        click(".model .m"); time.sleep(.15)
        e4 = cdp.js("(()=>{const a=document.getElementById('app').getBoundingClientRect(),m=document.querySelector('.menu').getBoundingClientRect();return [m.top>a.top+30, Math.round(m.top-a.top)]})()")
        asks = of("bubble.size")[n0:]
        check(e4[0] and all((u.get("overflowTop") or 0) <= 40 for u in asks), "review E4 (b): with room 40 px above (layout.room), the model menu opens under its button at once, never asking past the page area", f"{e4} {[u.get('overflowTop', 0) for u in asks]}")
        # margin cap (r44 / r06 / r47): an inset over 400 px (a bubble whose bottom stays fixed, grown at once) is drawn as given
        load("c-1-vide")
        host({"type": "layout", "mode": "bubble", "open": False, "maxWidth": 305, "maxHeight": 520, "inset": {"top": 700, "right": 0, "bottom": 0, "left": 0}})
        cap = cdp.js("[Math.round(document.getElementById('app').getBoundingClientRect().top), getComputedStyle(document.documentElement).getPropertyValue('--it').trim()]")
        host({"type": "layout", "mode": "bubble", "open": False, "maxWidth": 305, "maxHeight": 520, "inset": {"top": 1500, "right": 0, "bottom": 0, "left": 0}})
        cap2 = cdp.js("getComputedStyle(document.documentElement).getPropertyValue('--it').trim()")
        check(cap == [700, "700px"] and cap2 == "1000px", "margin cap: the surface is drawn at an inset of 700 px as given (it stopped at 400 and moved); the single bound is 1000 px", f"{cap} {cap2}")
        # pass 1, defect 8 (R22): « + » that opens the image picker directly is not announced as a menu
        load("c-1-vide")
        d8 = cdp.js("[document.querySelector('.tools .tb[aria-label=Joindre]').getAttribute('aria-haspopup'), document.querySelector('.tools .tb[aria-label=Joindre]').getAttribute('aria-expanded')]")
        load("c-menu-plus-incertain")
        d8b = cdp.js("[document.querySelector('.tools .tb[aria-label=Joindre]').getAttribute('aria-haspopup')]")
        check(d8 == [None, None] and d8b == ["menu"], "pass 1 #8 (R22): « + » with its single action has no aria-haspopup / aria-expanded; with two actions it is a menu again", f"{d8} {d8b}")
        # pass 1, defect 5 (E4, page side): after the page was scrolled (the host sends new inset / room), opening the model menu never
        # moves the surface — its top is the same at every image for 400 ms
        load("c-1-vide")
        ins = cdp.js("window.__inset")
        host({"type": "layout", "mode": "bubble", "open": False, "maxWidth": 305, "maxHeight": 520, "inset": ins, "room": {"top": 150, "right": 600, "bottom": 600, "left": 600}})
        e4p = cdp.js("""new Promise(r=>{const a=document.getElementById('app'),tops=[];document.querySelector('.model .m').click();const t0=performance.now();
          const f=()=>{tops.push(Math.round(a.getBoundingClientRect().top*10)/10);if(performance.now()-t0<400)requestAnimationFrame(f);else r({frames:tops.length,tops:Array.from(new Set(tops))})};requestAnimationFrame(f)})""")
        check(e4p["frames"] >= 15 and len(e4p["tops"]) == 1, "pass 1 #5 (E4, page): opening the model menu after a scroll never moves the surface (same top at every image)", json.dumps(e4p))
        # H1's measure: the host shows the bubble at the page's acknowledgement (bubble.size of the opening layout) — the page must
        # answer at once, whatever it has to draw (a long thread still there, a long conversation coming)
        LAT_JS = """((big)=>{const send=window.devHost.send,P=window.__posted;
          const items=[];for(let i=0;i<big;i++){items.push({type:'message.user',id:'u'+i,text:'Demande '+i+' : '+'mot '.repeat(40),at:new Date().toISOString(),elements:[]},{type:'turn.start',turnId:'t'+i,model:'Sonnet 5.5',effort:'Élevé',agentId:'claude-code',at:new Date().toISOString()},{type:'message.agentChunk',turnId:'t'+i,messageId:'m'+i,text:'## Titre\\n\\n'+'Une phrase de réponse assez longue. '.repeat(30)+'\\n\\n```css\\n.a{b:c}\\n```'},{type:'turn.end',turnId:'t'+i,stopReason:'end_turn',durationMs:9000})}
          send({type:'layout',mode:'bubble',maxWidth:320,maxHeight:520,element:'section.a',elementKey:'A'});send({type:'thread.reset',conversationId:'cA',items});
          const n0=P.length,t0=performance.now();send({type:'layout',mode:'bubble',maxWidth:320,maxHeight:520,element:'section.b',elementKey:'B'});
          const early=P.slice(n0).some(m=>m.type==='bubble.size');
          const t1=performance.now();send({type:'thread.reset',conversationId:'cB',items});const reset=performance.now()-t1;
          const ack=!early&&P.slice(n0).filter(m=>m.type==='bubble.size').length===1?performance.now()-t0:-1;
          return {ack:Math.round(ack*10)/10,reset:Math.round(reset)}})"""
        PAINT_JS = """((big)=>new Promise(r=>{const send=window.devHost.send;
          const items=[];for(let i=0;i<big;i++){items.push({type:'message.user',id:'u'+i,text:'Demande '+i+' : '+'mot '.repeat(40),at:new Date().toISOString(),elements:[]},{type:'turn.start',turnId:'t'+i,model:'Sonnet 5.5',effort:'Élevé',agentId:'claude-code',at:new Date().toISOString()},{type:'message.agentChunk',turnId:'t'+i,messageId:'m'+i,text:'## Titre\\n\\n'+'Une phrase de réponse assez longue. '.repeat(30)+'\\n\\n```css\\n.a{b:c}\\n```'},{type:'turn.end',turnId:'t'+i,stopReason:'end_turn',durationMs:9000})}
          send({type:'bubble.hidden'});const t0=performance.now();
          send({type:'layout',mode:'bubble',maxWidth:320,maxHeight:520,element:'section.b',elementKey:'B'});send({type:'state',running:false});send({type:'thread.reset',conversationId:'cB',items});
          const sync=performance.now()-t0;const sc=document.querySelector('.scroller');const lastBox=()=>{const u=document.querySelectorAll('.um');return Math.round(u[u.length-1].getBoundingClientRect().top-sc.getBoundingClientRect().bottom)};
          requestAnimationFrame(()=>{const raf=performance.now()-t0,first=document.querySelectorAll('.turn').length,b0=lastBox();
            setTimeout(()=>{const ids=Array.from(document.querySelectorAll('.um')).map(n=>+n.dataset.id.slice(1));
              r({sync:Math.round(sync),firstImage:Math.round(raf),firstTurns:first,turns:document.querySelectorAll('.turn').length,ordered:ids.every((v,i)=>v===i),
                 stayed:lastBox()===b0,atEnd:Math.round(sc.scrollHeight-sc.scrollTop-sc.clientHeight)<=1})},300)})}))"""
        load("c-1-vide")
        paint = cdp.js("(" + PAINT_JS + ")(40)")
        check(paint["firstImage"] < 30 and 1 <= paint["firstTurns"] <= 6 and paint["turns"] == 40 and paint["ordered"] and paint["stayed"] and paint["atEnd"],
              "H1 latency: a 40-exchange conversation reaches its first image fast (only its last exchanges that fill the bubble; was 55 ms for all), then the older ones come above — all 40 in order, nothing seen moves, still at the end", json.dumps(paint))
        # Q's journeys (first openings 150 to 211 ms for < 150): the page's share, as H1 sends an opening (layout, state, thread,
        # queue, focus in one go): layout -> acknowledgement (bubble.size), acknowledgement -> first image (rAF), for a new bubble,
        # an existing conversation (6 exchanges), the switch from the panel (M5); median of 3. The bubble's height at the first
        # image is its height once the older exchanges are there (no jump).
        OPEN_JS = """((kind)=>new Promise(r=>{const send=window.devHost.send,P=window.__posted;const n=kind==='neuve'?0:6;
          const items=[];for(let i=0;i<n;i++){items.push({type:'message.user',id:'u'+i,text:'Demande '+i+' : '+'mot '.repeat(40),at:new Date().toISOString(),elements:[]},{type:'turn.start',turnId:'t'+i,model:'Sonnet 5.5',effort:'Élevé',agentId:'claude-code',at:new Date().toISOString()},{type:'message.agentChunk',turnId:'t'+i,messageId:'m'+i,text:'## Titre\\n\\n'+'Une phrase de réponse assez longue. '.repeat(30)+'\\n\\n```css\\n.a{b:c}\\n```'},{type:'turn.end',turnId:'t'+i,stopReason:'end_turn',durationMs:9000})}
          if(kind!=='M5')send({type:'bubble.hidden'});
          setTimeout(()=>{const n0=P.length,t0=performance.now();
            send({type:'layout',mode:'bubble',maxWidth:320,maxHeight:520,element:'a.btn-primary',elementKey:'K'+kind,inset:window.__inset||{top:400,right:400,bottom:400,left:400}});
            send({type:'state',running:false,conversation:n?{id:'cX',title:'Bouton'}:null});send({type:'thread.reset',conversationId:n?'cX':'new',items});
            let ack=P.slice(n0).filter(m=>m.type==='bubble.size').length?performance.now()-t0:-1;const acks=()=>P.slice(n0).filter(m=>m.type==='bubble.size');
            send({type:'queue',items:[]});send({type:'focus',target:'composer'});
            requestAnimationFrame(()=>{const img=performance.now()-t0,h0=document.getElementById('app').getBoundingClientRect().height;if(ack<0&&acks().length)ack=img;const a0=acks()[0];
              const hA=a0?a0.height:-1;
              setTimeout(()=>{const last=acks().pop();r({ack:Math.round(ack*10)/10,image:Math.round((img-ack)*10)/10,jump:(a0?acks().reduce((m,x)=>Math.max(m,Math.abs(x.height-hA)),0)+Math.abs(Math.round(document.getElementById('app').getBoundingClientRect().height)-hA):999)})},300)})},150)}))"""
        opening = {}
        for kind, start in (("neuve", "c-1-vide"), ("existante", "c-1-vide"), ("M5", "6.7a-vide")):
            rows = []
            for _ in range(3):
                load(start); rows.append(cdp.js("(" + OPEN_JS + ")(" + json.dumps(kind) + ")"))
            opening[kind] = {k: sorted(x[k] for x in rows)[1] for k in ("ack", "image")}
            opening[kind]["jump"] = max(x["jump"] for x in rows)   # every later bubble.size and the height drawn in the end vs the acknowledged one
        report["perf"]["opening"] = opening
        print("     opening (page share, ms):", json.dumps(opening))
        check(all(0 <= v["ack"] < (8 if k == "neuve" else 16) and v["image"] < 8 and v["jump"] == 0 for k, v in opening.items()),
              "Q's journeys: the page acknowledges an opening (layout + thread, as H1 sends it) in < 8 ms for a new bubble, < 16 ms with a conversation (also from the panel, M5), its first image right after; the acknowledged height is the one the bubble keeps (no jump)", json.dumps(opening))
        load("c-1-vide")
        lat = cdp.js("(" + LAT_JS + ")(40)")
        check(lat["ack"] >= 0 and lat["ack"] < 16, "H1 latency: the opening layout is acknowledged (one bubble.size) with the thread that follows it, within one image, even with a 40-turn thread still drawn and another coming", json.dumps(lat))
        # F5: drafts never grow without end — after 45 elements with unsent words, the oldest one is dropped, the latest kept
        load("c-1-vide")
        f5 = cdp.js("""(()=>{const send=window.devHost.send,cin=()=>document.getElementById('composer-input');
          for(let i=0;i<45;i++){send({type:'layout',mode:'bubble',maxWidth:305,maxHeight:520,element:'div.e'+i,elementKey:'k'+i});send({type:'thread.reset',conversationId:'n'+i,items:[]});
            cin().focus();document.execCommand('insertText',false,'mot '+i);send({type:'bubble.hidden'});}
          const back=(i)=>{send({type:'layout',mode:'bubble',maxWidth:305,maxHeight:520,element:'div.e'+i,elementKey:'k'+i});send({type:'thread.reset',conversationId:'z'+i,items:[]});const t=cin().textContent;send({type:'bubble.hidden'});return t};
          return [back(44), back(0)]})()""")
        check(f5 == ["mot 44", ""], "review F5: drafts are capped (the oldest of 45 unsent drafts is dropped, the latest kept)", str(f5))
        # F1: a heading made of thousands of spaces never freezes the page
        load("c-2-envoye")
        f1 = cdp.js("new Promise(r=>{const t0=performance.now();window.devHost.send({type:'message.agentChunk',turnId:'t1',messageId:'f1',text:'# x'+' '.repeat(5000)+'x'});requestAnimationFrame(()=>requestAnimationFrame(()=>r(Math.round(performance.now()-t0))))})")
        host({"type": "message.agentChunk", "turnId": "t1", "messageId": "f1b", "text": "## Titre ##  "}); time.sleep(.2)
        f1b = cdp.js("Array.from(document.querySelectorAll('.md-h')).map(h=>h.textContent.replace(/ +/g,' '))")
        check(f1 < 300 and f1b == ["x x", "Titre"], "review F1: « # x » + 5 000 spaces + « x » renders at once (the old pattern took 41 s); closing #s still go", f"{f1} ms {f1b}")
        # F2: a file:/// address is not a dead link
        host({"type": "message.agentChunk", "turnId": "t1", "messageId": "f2", "text": "Voir file:///C:/site/landing.css et https://example.com"}); time.sleep(.2)
        f2 = cdp.js("Array.from(document.querySelectorAll('.md-link')).map(a=>a.textContent)")
        check(f2 == ["https://example.com"], "review F2: file:/// stays text (the host opens http(s) only); http(s) is still a link", str(f2))
        # R7: no element chip under the user's message in the bubble
        load("c-2-puce")
        check(cdp.js("document.querySelectorAll('.um .chip').length") == 0 and cdp.js("document.querySelector('.um .bubble').textContent") == "Fais en sorte que le bouton prenne toute la largeur du form",
              "bubble (R7): no element chip under (or in) the user's message — the element is the bubble's")
        # R8: until the agent writes, the step in progress stands in the message's place, under the model
        load("c-2-sans-texte")
        r8 = cdp.js("""(()=>{const p=document.querySelector('.bt-text .bt-pend .bt-st'),h=document.querySelector('.bt-head'),t=document.querySelector('.bt-text');return {inBody:!!p,label:p?p.querySelector('.lb').textContent:'',right:document.querySelector('.bt-stw').textContent.trim(),
          under:p?Math.round(p.getBoundingClientRect().top-h.getBoundingClientRect().bottom):null,shown:t.getClientRects().length>0}})()""")
        host({"type": "message.agentChunk", "turnId": "t1", "messageId": "m1", "text": "Je passe le bouton en pleine largeur."}); time.sleep(.1)
        r8b = cdp.js("[!!document.querySelector('.bt-text .bt-pend'), document.querySelector('.bt-stw .lb').textContent, document.querySelector('.bt-text .text').textContent]")
        check(r8["inBody"] and r8["label"] == "Lit checkout.css…" and r8["right"] == "" and r8["shown"] and r8["under"] is not None and r8["under"] >= 0,
              "bubble (R8, Paper 1B30-0): before any words, the step in progress sits in the message's place under « modèle · effort »", json.dumps(r8, ensure_ascii=False))
        check(r8b == [False, "Lit checkout.css…", "Je passe le bouton en pleine largeur."], "bubble (R8): the first words take that place; the step moves right of the model", json.dumps(r8b, ensure_ascii=False))
        # R12: the provider's logo left of the model's name in the turn's header
        load("c-2-envoye")
        lg = cdp.js("(()=>{const s=document.querySelector('.bt-model svg');const r=s.getBoundingClientRect();return [s.querySelector('path').getAttribute('fill'), Math.round(r.width), s.nextElementSibling.textContent]})()")
        check(lg == ["#D97757", 14, "Sonnet 5.5"], "bubble (R12): Claude's logo (14 px) left of « Sonnet 5.5 » in the answer's header", str(lg))
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
        check(cdp.js("(()=>{const s=document.querySelector('.turn .meta svg');return !!s&&s.nextElementSibling.textContent==='Sonnet · Élevé · 18 s'&&Math.round(s.getBoundingClientRect().width)===12})()"),
              "panel (R12): the provider's logo left of « Sonnet · Élevé · 18 s » in the answer's header")
        check(not pn["bub"] and pn["hd"] == 1 and pn["role"] is None and pn["meta"] == "Sonnet · Élevé · 18 s" and pn["bt"] == 0 and pn["pick"] == 1 and pn["bar"] and len(sizes()) == n0,
              "layout: back to `panel` the chat is the panel again (header, thread drawn again by thread.reset), no more `bubble.size`", json.dumps(pn, ensure_ascii=False))

    # B 1.8 — the folder's name rather than its path or the site's address (Paper 156L-0)
    if not ONLY:
        load("6.7a-vide"); cdp.js("document.querySelector('.tools .tb[aria-label=Joindre]').click()"); time.sleep(.1)
        plus = cdp.js("(document.querySelector('.menu')||{innerText:''}).innerText")
        load("6.2-premiere-fois")
        tile = cdp.js("[document.querySelector('.tile.found .nm').textContent, document.querySelectorAll('.tile.found .path').length, document.querySelector('.tile.found').getAttribute('aria-description')]")
        load("6.7b-historique")
        cdp.js("window.devHost.send({type:'history',items:[{id:'c2',title:'Bouton de paiement',subtitle:'1 fichier modifié',siteLabel:'127.0.0.1:5173',thisSite:true,agentIcon:'claude',updatedAt:new Date().toISOString()},"
               "{id:'c4',title:'Menu de navigation',subtitle:'3 fichiers modifiés',siteLabel:'localhost:4173',folderName:'vitrine',thisSite:false,agentIcon:'claude',updatedAt:new Date().toISOString()}]})"); time.sleep(.15)
        hs = cdp.js("[document.querySelector('.hsec').textContent, Array.from(document.querySelectorAll('.hrow .s')).map(s=>s.textContent)]")
        check("dossier" not in plus and "C:\\" not in plus and tile[0].startswith("boutique") and tile[1] == 0 and tile[2].endswith("boutique")
              and hs == ["Ce site · boutique", ["1 fichier modifié", "vitrine · 3 fichiers modifiés"]],
              "folder (B 1.8/1.9): the folder's name instead of its path or the site's address (folder screen, history); a sure folder is not in « + »", f"{plus!r} {tile} {hs}"[:300])

    if not ONLY:
        load("6.1-autorisation")
        check(cdp.js("document.activeElement.classList.contains('perm')"), "keyboard (SEC-23): the permission card holds the focus, never « Autoriser »", active())
        load("6.1-autorisation"); cdp.js("document.querySelector('.perm').__shownAt = performance.now()"); key("Enter")
        check(not any(x.startswith("permission.respond") for x in posted()), "keyboard (SEC-23): Entrée in the card's first 600 ms answers nothing", str(posted()[-3:]))
        cdp.js("document.querySelector('.perm').__shownAt = 0")
        cdp.call("Input.dispatchKeyEvent", type="rawKeyDown", key="Enter", code="Enter", windowsVirtualKeyCode=13, modifiers=0, autoRepeat=True)
        cdp.call("Input.dispatchKeyEvent", type="keyUp", key="Enter", code="Enter", windowsVirtualKeyCode=13, modifiers=0)
        check(not any(x.startswith("permission.respond") for x in posted()), "keyboard (SEC-23): a held Entrée (auto-repeat) answers nothing", str(posted()[-3:]))
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
        check(any(p.startswith("setMode:plan") for p in posted()), "keyboard: Maj+Tab switches to the next of the 4 modes (Demander → Planifier)", str(posted()[-3:]))

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
        check(order[:3] == ["Nouveau chat", "Historique", "Fermer le chat"] and "Message à l'agent" in order, "keyboard: Tab order header → content → input → tools (a new chat's title is not a stop: nothing to rename)", " | ".join(order)[:300])

        load("6.7b-historique"); key("Escape")
        check(cdp.js("document.getElementById('history').hidden") and active() == "Historique", "keyboard: Échap closes the history and returns to its button", active())

        # B 1.9 (R11): the title does not open the history; a double-click renames it in place
        load("titre-renommer")
        tc = center(".hd-title")
        cdp.call("Input.dispatchMouseEvent", type="mousePressed", x=tc[0], y=tc[1], button="left", clickCount=1); cdp.call("Input.dispatchMouseEvent", type="mouseReleased", x=tc[0], y=tc[1], button="left", clickCount=1); time.sleep(.2)
        one = cdp.js("[document.getElementById('history').hidden, window.__posted.some(m=>m.type==='history.open'), !!document.querySelector('.hd-rename')]")
        cdp.call("Input.dispatchMouseEvent", type="mousePressed", x=tc[0], y=tc[1], button="left", clickCount=2); cdp.call("Input.dispatchMouseEvent", type="mouseReleased", x=tc[0], y=tc[1], button="left", clickCount=2); time.sleep(.15)
        ed = cdp.js("(()=>{const i=document.querySelector('.hd-rename');return i?[i.value, document.activeElement===i, i.selectionStart, i.selectionEnd]:null})()")
        cdp.call("Input.insertText", text="Paiement pleine largeur"); key("Enter"); time.sleep(.1)
        rn = cdp.js("[window.__posted.filter(m=>m.type==='conversation.rename').map(m=>m.id+':'+m.title), document.querySelector('.hd-title-text').textContent, !!document.querySelector('.hd-rename'), document.activeElement.className]")
        check(one == [True, False, False], "header (R11): a click on the title opens nothing (the history has its own button)", str(one))
        check(ed == ["Bouton de paiement", True, 0, 18] and rn[0] == ["c1:Paiement pleine largeur"] and rn[1] == "Paiement pleine largeur" and not rn[2] and rn[3] == "hd-title",
              "header (R11): a double-click renames in place — the field is focused with the name selected, Entrée sends `conversation.rename` and keeps the name", f"{ed} {rn}")
        cdp.js("document.querySelector('.hd-title').focus()"); key("F2", "F2"); time.sleep(.1)
        cdp.call("Input.insertText", text="Autre nom"); key("Escape"); time.sleep(.1)
        esc = cdp.js("[window.__posted.filter(m=>m.type==='conversation.rename').length, document.querySelector('.hd-title-text').textContent, window.__posted.some(m=>m.type==='close')]")
        check(esc == [1, "Paiement pleine largeur", False], "header (R11): F2 opens the same field; Échap cancels (nothing sent, the old name stays, the panel stays open)", str(esc))

        load("6.10-travaille")
        p0 = cdp.js("[document.querySelectorAll('.turn').length, document.querySelectorAll('.marker').length, document.querySelectorAll('.step').length]")
        tid = cdp.js("document.querySelector('.turn:last-of-type').dataset.turn || document.querySelectorAll('.turn')[document.querySelectorAll('.turn').length-1].dataset.turn")
        cdp.js("window.devHost.send({type:'turn.start',turnId:" + json.dumps(tid) + ",model:'Opus 5.5',effort:'high',agentId:'claude-code',at:new Date().toISOString()})"); time.sleep(.1)
        p1 = cdp.js("(()=>{const ts=document.querySelectorAll('.turn'),t=ts[ts.length-1];return [ts.length, document.querySelectorAll('.marker').length, document.querySelectorAll('.step').length, t.querySelector('.meta').textContent, !!t.querySelector('.meta svg')]})()")
        check(p1[:3] == p0 and p1[3] == "Opus 5.5 · Élevé" and p1[4], "panel (B 1.9): a second `turn.start` of the same turn updates its header only (« Opus 5.5 · Élevé »), no « Passé à » marker", f"{p0} -> {p1}")
        # cv4 R26: a pause on an element chip (input or thread) asks the host to show the element; leaving gives the page back
        load("6.9-ecrire")
        c = center(".cin .chip"); move(*c); time.sleep(.05)
        early = cdp.js("window.__posted.filter(m=>m.type==='element.hover').length")
        time.sleep(.25)
        on26 = cdp.js("window.__posted.filter(m=>m.type==='element.hover').map(m=>m.id+':'+m.active)")
        move(c[0], c[1] + 200); time.sleep(.1)
        off26 = cdp.js("window.__posted.filter(m=>m.type==='element.hover').map(m=>m.id+':'+m.active)")
        check(early == 0 and on26 == ["el-btn:true"] and off26 == ["el-btn:true", "el-btn:false"], "panel (cv4 R26): 120 ms on a chip sends element.hover {id, active: true}; leaving it sends active: false; passing over sends nothing", f"{early} {on26} {off26}")
        load("6.11-fait")
        tc = center(".um .chip")
        if tc: move(*tc); time.sleep(.3); move(tc[0], tc[1] + 300); time.sleep(.1)
        th26 = cdp.js("window.__posted.filter(m=>m.type==='element.hover').map(m=>m.active)")
        check(tc is not None and th26 == [True, False], "panel (cv4 R26): the same for a chip in the thread", str(th26))
        load("suivi")
        p43 = cdp.js("[document.querySelectorAll('.follow').length, Array.from(document.querySelectorAll('.turn .text')).map(t=>t.textContent)]")
        check(p43 == [0, ["J'ai raccourci le texte."]], "panel (R43): a finished turn shows only its answer — the announcements are gone (folded with the steps)", str(p43))
        load("6.10-travaille")
        cdp.js("window.devHost.send({type:'message.agentChunk',turnId:document.querySelectorAll('.turn')[document.querySelectorAll('.turn').length-1].dataset.turn,messageId:'zz',text:'Je cherche le style de la grille.'})"); time.sleep(.15)
        car2 = cdp.js("Array.from(document.querySelectorAll('.text, .text *')).map(n=>getComputedStyle(n,'::after').content).filter(c=>c&&c!=='none'&&c!=='normal')")
        check(car2 == [], "panel (R48): no blue typing caret after the agent's streaming text", str(car2))
        load("autorisation-sans-refus")
        f10p = cdp.js("[Array.from(document.querySelectorAll('.perm .btn')).map(b=>b.firstChild.textContent), !!document.querySelector('.perm .instead')]")
        check(f10p == [["Autoriser"], False], "review F10 (panel): a request with no refuse option shows only « Autoriser »", str(f10p))
        load("etapes-brutes")
        pl4 = cdp.js("Array.from(document.querySelectorAll('.steps .step span:not(.spin):not(.fail-tag)')).map(n=>n.textContent)")
        check(pl4 == ["A lu index.html", "A utilisé 1 outil MCP", "A lancé npm run build", "Modifie landing.css…"], "panel (D4): the same French labels for the steps, never the agent's raw title", str(pl4))
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
        check(not cdp.js("!!document.querySelector('.menu .mwarn')") and cdp.js("!document.querySelector('.menu')"), "model menu (R42): another provider's model applies at once, no « Changer d'agent » box, the menu closes")
        sent = cdp.js("window.__posted.filter(m=>m.type==='setModel').map(m=>m.id+'@'+m.agentId)")
        check(sent[-1:] == ["terra@codex"] and cdp.js("document.querySelector('.model .mn').textContent") == "6.1 Terra" and cdp.js("!!document.querySelector('.model .mw svg')"),
              "model menu (V1.2): confirmed, the agent changes with that model; the button shows the provider's logo and the model", str(sent))
        # #128 (Paper A): every provider is listed, connected or not — its header says its state with the action that fixes it
        mm_js = """(()=>({heads:Array.from(document.querySelectorAll('.menu .mm-head')).map(h=>[h.querySelector('.mm-hn').textContent,(h.querySelector('.mm-act')||{}).textContent||'']),
          rows:Array.from(document.querySelectorAll('.menu .mi')).map(b=>b.textContent.trim()+(b.getAttribute('aria-disabled')==='true'?' (grisé)':''))}))()"""
        load("modeles-un-fournisseur")
        one = cdp.js(mm_js)
        check(one["heads"] == [["Claude Code", ""], ["Codex · non connecté", "Connecter"]] and one["rows"][:3] == ["Opus 5.5", "Sonnet 5.5Par défaut", "Haiku 5.5"]
              and one["rows"][3:6] == ["6.1 Sol (grisé)", "6.1 Terra (grisé)", "6.1 Luna (grisé)"] and one["rows"][-1].startswith("Effort"),
              "model menu (#128): a provider that is not connected stays listed — « Codex · non connecté » + « Connecter », its models greyed", json.dumps(one, ensure_ascii=False)[:300])
        load("modeles-aucun-fournisseur")
        none_ = cdp.js(mm_js)
        check(none_["heads"] == [["Claude Code · non connecté", "Connecter"], ["Codex · pas installé", "Installer"]] and none_["rows"] == ["EffortÉlevé"],
              "model menu (#128): nobody connected — each header says its state with its action (Connecter, Installer); no model invented; Effort stays last", json.dumps(none_, ensure_ascii=False)[:300])
        load("modeles-codex-inconnu")
        unk = cdp.js(mm_js)
        check(["Codex", ""] in unk["heads"] and unk["rows"][3] == "Modèle par défaut" and "Codex" not in unk["rows"],
              "model menu (#128, R13): a connected provider whose models are not known yet — its header, then one row naming the model its agent will use", json.dumps(unk, ensure_ascii=False)[:300])
        load("6.4-autres-modeles-long")   # the scenario opens the menu (its « Autres modèles » click finds nothing now)
        more = cdp.js(mm_js)
        check(more["rows"][:5] == ["Opus 5.5", "Sonnet 5.5Par défaut", "Haiku 5.5", "Sonnet 4.5", "Opus 4.1"] and [h[0] for h in more["heads"]] == ["Claude Code", "Codex"]
              and not any(r.startswith("Autres modèles") for r in more["rows"]), "model menu (#128): every model in the list, by provider — no « Autres modèles » beyond 3", json.dumps(more, ensure_ascii=False)[:300])
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
        cdp.js("document.querySelector('.tools .tb[aria-label=Joindre]').click()")
        plus = cdp.js("(document.querySelector('.menu')||{textContent:''}).textContent")
        check(not fb["chip"] and not fb["first"] and fb["tools"] == 3 and "dossier" not in plus, "folder (V1.2/R5): no folder chip in the composer, no folder screen, and « + » does not offer to change a folder found for sure", json.dumps(fb) + " " + plus)
        load("dossier-demande")
        check(cdp.js("!document.querySelector('.first').hidden && !document.querySelector('.agents')"), "folder: the folder screen shows only when the host asks (askFolder), without the agent list when the agent is ready")
        # R24 (user's decision): « Traiter les annotations en attente » is gone — no suggestion, no « /annotations », no batch in the input
        load("annotations-avant")
        check(cdp.js("!document.querySelector('.batch-row') && document.querySelector('.batch').hidden"), "annotations (R24): an `annotations.batch` from the host is ignored — no batch in the input")
        load("6.7a-vide")
        sg = cdp.js("Array.from(document.querySelectorAll('.sug .lb')).map(n=>n.textContent)")
        check(sg == ["Vérifier cette page sur mobile", "Trouver les textes trop peu contrastés"], "empty chat (R24): no « Traiter les annotations en attente » suggestion, even with pendingAnnotations", str(sg))
        cdp.js("window.devHost.send({type:'state',suggestions:[{id:'mobile',label:'Vérifier cette page sur mobile'},{id:'mobile',label:'Vérifier cette page sur mobile'},{id:'m2',label:'Vérifier cette page sur mobile'},{id:'annotations',label:'Traiter les 3 annotations en attente'}]})"); time.sleep(.1)
        check(cdp.js("Array.from(document.querySelectorAll('.sug .lb')).map(n=>n.textContent)") == ["Vérifier cette page sur mobile"], "empty chat (R24): a suggestion never shows twice (same id or same words)")
        # R23 (Paper 156O-0 / 1575-0): the empty chat sits at the bottom, right above the input
        load("6.7a-vide")
        e23 = cdp.js("(()=>{const e=document.querySelector('.empty').getBoundingClientRect(),c=document.querySelector('.dock').getBoundingClientRect(),h=document.querySelector('.empty h2').getBoundingClientRect(),l=document.querySelectorAll('.sug');const s=l[l.length-1].getBoundingClientRect();return [Math.round(c.top-s.bottom), Math.round(h.top), Math.round(e.bottom-c.top)]})()")
        check(e23[0] == 16 and e23[1] > 300 and abs(e23[2]) <= 1, "empty chat (R23): « Que voulez-vous changer ? » and its suggestions sit at the bottom, 16 px above the input (Paper 1575-0)", str(e23))
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
        check(not cdp.js("Array.from(document.querySelectorAll('#combo [role=option]')).some(o=>o.textContent.startsWith('/annotations'))"), "annotations (R24): « /annotations » is no longer in the / menu")

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
        bare = cdp.js("(()=>{try{const d=document.createElement('div');const t='Voir http://127.0.0.1:5183/checkout et file:///C:/site/index.html ou [http://a.test/x](http://a.test/x).';"
                      "window.ChatMarkdown.inline(d,t,{});const a=[...d.querySelectorAll('a.md-link')].map(x=>x.textContent);return {ok:true,links:a,text:d.textContent}}catch(e){return {ok:false,err:String(e)}}})()")
        check(bare["ok"] and "http://127.0.0.1:5183/checkout" in bare["links"] and "http://a.test/x" in bare["links"] and "file:///C:/site/index.html" in bare["text"],
              "markdown: a bare address (http://, file:///) and a link whose label is an address render once, no endless recursion", json.dumps(bare))
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
    kill_tree(proc)  # the browser and all its child processes
    srv.shutdown()
    time.sleep(.5); shutil.rmtree(profile, ignore_errors=True)
    json.dump(report, open(os.path.join(OUT, "report.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=1)

print(f"\n{len(report['checks']) - len(failures)} / {len(report['checks'])} checks passed" + (f" — FAILED: {', '.join(failures)}" if failures else ""))
sys.exit(1 if failures else 0)
