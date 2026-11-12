"""Engine check of the overlay scrollbar page script (comment c-l6gvbn): the colour it reports
under each track is the page's background layers composed source-over down to its canvas.

Runs the exact ScrollbarScript of src/viewport-scrollbar-host.h in headless Chromium (the Edge
runtime that WebView2 uses, given by argument) on local fixture pages, driven over the DevTools
protocol with a throw-away --user-data-dir: no personal profile, no network. The page's
window.chrome.webview.postMessage is stubbed to record the messages; the OS colour preference is
emulated per case (Emulation.setEmulatedMedia), so this machine's Windows theme never matters.

Usage: python -I scrollbar_surface_e2e.py <repo root> <msedge.exe> <work dir>
Exit 0 when every case reports the expected surfaces (within 1 per channel).
"""
import base64, json, os, re, shutil, socket, struct, subprocess, sys, tempfile, time, urllib.request

root, edge, work = sys.argv[1], sys.argv[2], sys.argv[3]
src = open(os.path.join(root, "src", "viewport-scrollbar-host.h"), encoding="utf-8").read()
script = re.search(r'ScrollbarScript\[\]=LR"JS\((.*?)\)JS";', src, re.S).group(1)
os.makedirs(work, exist_ok=True)

STUB = ("<script>window.chrome=window.chrome||{};window.__m=[];window.chrome.webview={postMessage:m=>{window.__m.push(m);"
        "if(window.__m.filter(x=>x.startsWith('lisiere-scrollbar ')).length===1&&window.__first)window.__first();}};</script>")

def page(style, body, extra="", on_first=""):
    hook = f"<script>window.__first=()=>{{{on_first}}};</script>" if on_first else ""
    return f"<!doctype html><html><head><meta charset=utf-8>{extra}<style>{style}</style>{hook}{STUB}<script>{script}</script></head><body>{body}</body></html>"

TALL = "<div style='height:3000px;width:3000px'></div>"
NARROW = "<div style='height:3000px'></div>"  # content no wider than the body: the default margin stays bare
FULL = "position:fixed;inset:0;"
# name: (html, expected vertical, expected horizontal, OS preference, reports to wait for)
# Expected None = « dark » only (oklch: exact sRGB depends on gamut mapping).
CASES = {
    "white-default": (page("", TALL), 0xFFFFFF, 0xFFFFFF, "light", 1),
    "dark-body": (page("body{background:#121212;margin:0}", TALL), 0x121212, 0x121212, "light", 1),
    "dark-html-oklch": (page("html{background:oklch(20% 0 0)}", TALL), None, None, "light", 1),
    "black-50-on-white": (page("html{background:#fff}.l{" + FULL + "background:rgba(0,0,0,.5)}", "<div class=l></div>" + TALL), 0x808080, 0x808080, "light", 1),
    "white-50-on-black": (page("html{background:#000}.l{" + FULL + "background:rgba(255,255,255,.5)}", "<div class=l></div>" + TALL), 0x808080, 0x808080, "light", 1),
    "nested-translucent": (page("html{background:#fff}.a{" + FULL + "background:rgba(0,0,0,.5)}.b{position:absolute;inset:0;background:rgba(0,0,0,.5)}", "<div class=a><div class=b></div></div>" + TALL), 0x404040, 0x404040, "light", 1),
    "transparent-app-shell-dark": (page("html,body{margin:0}.app{" + FULL + "background:#0d1117}.app>div{position:absolute;inset:0}", "<div class=app><div></div></div>" + TALL), 0x0D1117, 0x0D1117, "light", 1),
    "per-track": (page("html{background:#fff}.r{position:fixed;top:0;bottom:0;right:0;width:40px;background:#111}", "<div class=r></div>" + TALL), 0x111111, 0xFFFFFF, "light", 1),
    "color-scheme-dark-canvas": (page(":root{color-scheme:dark}", TALL), 0x121212, 0x121212, "light", 1),
    "meta-color-scheme-dark": (page("", TALL, "<meta name=color-scheme content=dark>"), 0x121212, 0x121212, "light", 1),
    "color-scheme-light-dark-in-light-os": (page(":root{color-scheme:light dark}", TALL), 0xFFFFFF, 0xFFFFFF, "light", 1),
    "color-scheme-light-dark-in-dark-os": (page(":root{color-scheme:light dark}", TALL), 0x121212, 0x121212, "dark", 1),
    "light-page-in-dark-os": (page("html{background:#fff}", TALL), 0xFFFFFF, 0xFFFFFF, "dark", 1),
    "unstyled-page-in-dark-os": (page("", TALL), 0xFFFFFF, 0xFFFFFF, "dark", 1),
    "theme-switch-class": (page("html{background:#fff}html.dark{background:#181818}", TALL, on_first="document.documentElement.classList.add('dark')"), 0x181818, 0x181818, "light", 2),
    "theme-switch-body-style": (page("body{margin:0}", TALL, on_first="document.body.style.background='#222'"), 0x222222, 0x222222, "light", 2),
    # CSS root/body background propagation (review of PR68): html transparent, so the body background
    # paints the whole canvas, including the default 8 px margin under the right-hand track.
    "body-dark-default-margin": (page("body{background:#121212}", TALL), 0x121212, 0x121212, "light", 1),
    "body-dark-default-margin-dark-os": (page("body{background:#121212}", TALL), 0x121212, 0x121212, "dark", 1),
    "body-translucent-default-margin": (page("body{background:rgba(0,0,0,.5)}", TALL), 0x808080, 0x808080, "light", 1),
    "body-translucent-margin0": (page("body{background:rgba(0,0,0,.5);margin:0}", TALL), 0x808080, 0x808080, "light", 1),
    # html has its own background: it is the canvas, the body paints only its own box (margin shows html).
    "html-white-body-dark-default-margin": (page("html{background:#fff}body{background:#121212}", TALL), 0xFFFFFF, 0x121212, "light", 1),
    "html-translucent-body-dark": (page("html{background:rgba(0,0,0,.5)}body{background:#121212}", TALL), 0x808080, 0x121212, "light", 1),
    "body-image-only-html-transparent": (page("body{background:#121212 linear-gradient(#fff,#fff) no-repeat 0 0/4px 4px}", TALL), 0x121212, 0x121212, "light", 1),
    # Review PR68 #1, exact case: html transparent, body dark, default 8 px margin, light OS, content only tall.
    "review-body-dark-default-margin-narrow": (page("body{background:#121212}", NARROW), 0x121212, 0x121212, "light", 1),
    "body-dark-margin0-narrow": (page("body{background:#121212;margin:0}", NARROW), 0x121212, 0x121212, "light", 1),
    "body-translucent-default-margin-narrow": (page("body{background:rgba(0,0,0,.5)}", NARROW), 0x808080, 0x808080, "light", 1),
    "html-white-body-dark-narrow": (page("html{background:#fff}body{background:#121212}", NARROW), 0xFFFFFF, 0x121212, "light", 1),
    "html-translucent-body-dark-narrow": (page("html{background:rgba(0,0,0,.5)}body{background:#121212}", NARROW), 0x808080, 0x121212, "light", 1),
    "html-image-only-body-dark-narrow": (page("html{background-image:linear-gradient(#fff,#fff);background-size:1px 1px;background-repeat:no-repeat}body{background:#121212}", NARROW), 0xFFFFFF, 0x121212, "light", 1),
    # Review PR68 #2: the CSS color-scheme of the root wins over the meta tag (meta only when CSS is normal).
    "review-css-light-meta-dark-dark-os": (page(":root{color-scheme:light}", NARROW, "<meta name=color-scheme content=dark>"), 0xFFFFFF, 0xFFFFFF, "dark", 1),
    "review-css-dark-meta-light-light-os": (page(":root{color-scheme:dark}", NARROW, "<meta name=color-scheme content=light>"), 0x121212, 0x121212, "light", 1),
    "css-light-meta-light-dark-dark-os": (page(":root{color-scheme:light}", NARROW, "<meta name=color-scheme content='light dark'>"), 0xFFFFFF, 0xFFFFFF, "dark", 1),
    "meta-light-dark-dark-os": (page("", NARROW, "<meta name=color-scheme content='light dark'>"), 0x121212, 0x121212, "dark", 1),
    "meta-light-dark-light-os": (page("", NARROW, "<meta name=color-scheme content='light dark'>"), 0xFFFFFF, 0xFFFFFF, "light", 1),
    "css-only-light-dark-os": (page(":root{color-scheme:only light}", NARROW), 0xFFFFFF, 0xFFFFFF, "dark", 1),
    "background-image-falls-back-to-color": (page("html{background:#1a1a1a linear-gradient(#fff,#fff) no-repeat 0 0/10px 10px}", TALL), 0x1A1A1A, 0x1A1A1A, "light", 1),
}

class Cdp:
    """Minimal DevTools WebSocket client (stdlib only)."""
    def __init__(self, url):
        host, port, path = re.match(r"ws://([^:/]+):(\d+)(/.*)", url).groups()
        self.s = socket.create_connection((host, int(port)), timeout=30)
        key = base64.b64encode(os.urandom(16)).decode()
        self.s.sendall(f"GET {path} HTTP/1.1\r\nHost: {host}:{port}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n".encode())
        buf = b""
        while b"\r\n\r\n" not in buf: buf += self.s.recv(4096)
        self.rest = buf.split(b"\r\n\r\n", 1)[1]; self.n = 0
    def _read(self, k):
        while len(self.rest) < k: self.rest += self.s.recv(65536)
        out, self.rest = self.rest[:k], self.rest[k:]; return out
    def _frame(self):
        b1, b2 = self._read(2); ln = b2 & 127
        if ln == 126: ln = struct.unpack(">H", self._read(2))[0]
        elif ln == 127: ln = struct.unpack(">Q", self._read(8))[0]
        return b1 & 15, self._read(ln)
    def call(self, method, **params):
        self.n += 1; msg = json.dumps({"id": self.n, "method": method, "params": params}).encode()
        head = bytes([0x81]) + (bytes([0x80 | len(msg)]) if len(msg) < 126 else bytes([0x80 | 126]) + struct.pack(">H", len(msg)) if len(msg) < 65536 else bytes([0x80 | 127]) + struct.pack(">Q", len(msg)))
        mask = os.urandom(4); self.s.sendall(head + mask + bytes(c ^ mask[i % 4] for i, c in enumerate(msg)))
        while True:
            op, data = self._frame()
            if op != 1: continue
            r = json.loads(data)
            if r.get("id") == self.n:
                if "error" in r: raise RuntimeError(r["error"])
                return r.get("result", {})

profile = tempfile.mkdtemp(prefix="sb-e2e-")
proc = subprocess.Popen([edge, "--headless=new", "--disable-gpu", "--no-first-run", "--no-default-browser-check", "--disable-extensions",
                         "--remote-debugging-port=0", "--user-data-dir=" + profile, "--window-size=800,600", "about:blank"],
                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
failures = 0
try:
    port_file = os.path.join(profile, "DevToolsActivePort")
    for _ in range(200):
        if os.path.exists(port_file) and open(port_file).read().strip(): break
        time.sleep(.1)
    port = open(port_file).read().split()[0]
    targets = json.load(urllib.request.urlopen(f"http://127.0.0.1:{port}/json/list"))
    cdp = Cdp([t for t in targets if t["type"] == "page"][0]["webSocketDebuggerUrl"])
    cdp.call("Page.enable")
    for name, (html, ev, eh, scheme, want) in CASES.items():
        path = os.path.join(work, name + ".html")
        open(path, "w", encoding="utf-8").write(html)
        cdp.call("Emulation.setEmulatedMedia", features=[{"name": "prefers-color-scheme", "value": scheme}])
        cdp.call("Page.navigate", url="file:///" + os.path.abspath(path).replace("\\", "/"))
        wait = ("new Promise(r=>{const t0=performance.now();const f=()=>{const m=(window.__m||[]).filter(x=>x.startsWith('lisiere-scrollbar '));"
                f"if(m.length>={want}||performance.now()-t0>4000)r(m);else setTimeout(f,25);}};f();}})")
        time.sleep(.2)
        msgs = cdp.call("Runtime.evaluate", expression=wait, awaitPromise=True, returnByValue=True)["result"].get("value") or []
        if len(msgs) < want:
            print(f"FAIL {name}: {len(msgs)} report(s), {want} expected"); failures += 1; continue
        f = msgs[-1].split(" ")
        if len(f) != 13:
            print(f"FAIL {name}: {len(f) - 1} fields in {msgs[-1]!r}"); failures += 1; continue
        sv, sh = int(f[11]), int(f[12])
        if ev is None:
            ok = all(((v >> s) & 255) < 60 for v in (sv, sh) for s in (16, 8, 0))
        else:
            ok = all(abs(((v >> s) & 255) - ((e >> s) & 255)) <= 1 for v, e in ((sv, ev), (sh, eh)) for s in (16, 8, 0))
        print(f"{'ok  ' if ok else 'FAIL'} {name} [{scheme} OS]: vertical #{sv:06X} horizontal #{sh:06X} expected "
              f"{'dark' if ev is None else f'#{ev:06X} / #{eh:06X}'} (report {len(msgs)} of {want} awaited)")
        failures += 0 if ok else 1
finally:
    proc.terminate()
    try: proc.wait(10)
    except Exception: proc.kill()
    time.sleep(.5); shutil.rmtree(profile, ignore_errors=True)
print(f"{failures} failure(s)" if failures else f"all {len(CASES)} surface cases passed")
sys.exit(1 if failures else 0)
