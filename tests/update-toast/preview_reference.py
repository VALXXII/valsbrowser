"""Reference renders of Val's VALIDATED update-card preview, for the native card's pixel comparison (compare.py).

    python tests/update-toast/preview_reference.py [--ref origin/VALXXII/atelier-carte-maj]

The preview (design/update-card-preview/index.html, validated by Val on 2026-11-26) lives on its own branch and is not
part of the product. This reads it with `git show` into build/update-toast/preview-page/, opens it headless in the
WebView2 runtime's Chromium (EdgeCore, --headless=new, throw-away profile, no window on the screen, SwiftShader WebGL)
and drives its virtual clock (window.__preview), as the preview's own capture.py does. Writes, card only (border box):
    tests/update-toast/reference/preview-{A,B,C}-{100,150}.png
A: « Mise à jour disponible », B: « Téléchargement en cours… » at Paper's 50,4/200,0 Mo · 00:14 with the drawer's icon
at its loop start, C: « Prête à installer ». The background is the GrainGradient at Paper's own frame (time 0).
"""
import base64, glob, io, json, os, re, shutil, socket, struct, subprocess, sys, tempfile, time, urllib.request

from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "reference")
args = sys.argv[1:]
REF = args[args.index("--ref") + 1] if "--ref" in args else "origin/VALXXII/atelier-carte-maj"
EDGE = next(iter(sorted(glob.glob(os.path.join(os.environ.get("LOCALAPPDATA", ""), "Microsoft", "EdgeCore", "*", "msedge.exe")), reverse=True)), None)
HEIGHTS = {"A": 209.59375, "B": 187.59375, "C": 209.59375}
LEFT, BOTTOM = 32, 272   # the preview's capture mode: card anchor (CSS px)


class Cdp:
    def __init__(self, url):
        host, port, path = re.match(r"ws://([^:/]+):(\d+)(/.*)", url).groups()
        self.s = socket.create_connection((host, int(port)), timeout=120)
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
            m = json.loads(data)
            if m.get("id") == self.n:
                if "error" in m: raise RuntimeError(f"{method}: {m['error']}")
                return m.get("result", {})

    def js(self, expr):
        r = self.call("Runtime.evaluate", expression=expr, awaitPromise=True, returnByValue=True)
        if "exceptionDetails" in r: raise RuntimeError("JS: " + json.dumps(r["exceptionDetails"])[:600])
        return r["result"].get("value")


def main():
    if not EDGE: sys.exit("EdgeCore (WebView2 runtime's Chromium) not found")
    page_dir = os.path.join(ROOT, "build", "update-toast", "preview-page")
    os.makedirs(page_dir, exist_ok=True)
    html = subprocess.run(["git", "show", f"{REF}:design/update-card-preview/index.html"], cwd=ROOT, capture_output=True, check=True).stdout
    page = os.path.join(page_dir, "index.html")
    open(page, "wb").write(html)
    os.makedirs(OUT, exist_ok=True)
    profile = tempfile.mkdtemp(prefix="card-reference-")
    proc = subprocess.Popen([EDGE, "--headless=new", "--use-angle=swiftshader", "--enable-unsafe-swiftshader", "--ignore-gpu-blocklist", "--no-first-run",
                             "--no-default-browser-check", "--disable-extensions", "--disable-sync", "--disable-background-networking", "--disable-component-update",
                             "--hide-scrollbars", "--force-color-profile=srgb", "--remote-debugging-port=0", "--user-data-dir=" + profile,
                             "--window-size=400,340", "about:blank"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        pf = os.path.join(profile, "DevToolsActivePort")
        for _ in range(300):
            if os.path.exists(pf) and open(pf).read().strip(): break
            time.sleep(.1)
        port = open(pf).read().split()[0]
        cdp = Cdp([t for t in json.load(urllib.request.urlopen(f"http://127.0.0.1:{port}/json/list")) if t["type"] == "page"][0]["webSocketDebuggerUrl"])
        for d in ("Page", "Runtime"): cdp.call(d + ".enable")
        for scale in (1.0, 1.5):
            cdp.call("Emulation.setDeviceMetricsOverride", width=400, height=340, deviceScaleFactor=scale, mobile=False)
            cdp.call("Page.navigate", url="file:///" + page.replace("\\", "/") + "?capture=1")
            cdp.js("new Promise(r=>{const f=()=>(window.__preview&&document.readyState==='complete')?document.fonts.ready.then(()=>setTimeout(r,300)):setTimeout(f,30);f()})")
            info = cdp.js("__preview.info()")
            if not info["webgl"] or not info["fonts"]: sys.exit("WebGL or the bundled font is missing in the headless engine")
            for s in "ABC":
                cdp.js(f"__preview.set({{reduced:false, blue:false, drawer:false, rate:'paper', grainTime:6409645.900000468/1000}}); __preview.snap('{s}'); __preview.step(0)")
                data = base64.b64decode(cdp.call("Page.captureScreenshot", format="png", fromSurface=True)["data"])
                im = Image.open(io.BytesIO(data)).convert("RGB")
                h = HEIGHTS[s]
                box = (round(LEFT * scale), round((BOTTOM - h) * scale), round((LEFT + 269) * scale), round(BOTTOM * scale))
                name = f"preview-{s}-{int(scale * 100)}.png"
                im.crop(box).save(os.path.join(OUT, name), optimize=True)
                print("wrote", name, box)
    finally:
        proc.terminate()
        try: proc.wait(10)
        except Exception: proc.kill()
        shutil.rmtree(profile, ignore_errors=True)


if __name__ == "__main__":
    main()
