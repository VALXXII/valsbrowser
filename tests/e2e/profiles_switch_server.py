# Local pages for tests/e2e/profiles_switch_e2e.ps1 (127.0.0.1 only).
#   /tick  title "tick N", N counting up every 400 ms (a parked tab whose title keeps changing)
#   /show  static title "show"
#   /opener?c=RRGGBB  opens /pat?c=RRGGBB in a new tab (target=_blank) after 1.2 s, as a link would
import http.server, sys
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8768
class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def page(self, body):
        self.send_response(200); self.send_header('Content-Type', 'text/html; charset=utf-8'); self.send_header('Content-Length', str(len(body))); self.end_headers(); self.wfile.write(body)
    def do_GET(self):
        if self.path.startswith('/tick'):
            self.page(b"<title>tick 0</title><script>let n=0;setInterval(()=>{document.title='tick '+(++n)},400)</script>"); return
        if self.path.startswith('/slow.bin'):   # a 4 MB attachment over ~40 s (cancellation QA)
            import time
            name = 'qa-cancel-' + (self.path.split('n=')[-1] if 'n=' in self.path else 'x') + '.bin'
            self.send_response(200); self.send_header('Content-Type', 'application/octet-stream')
            self.send_header('Content-Disposition', 'attachment; filename="%s"' % name); self.send_header('Content-Length', str(4 * 1024 * 1024)); self.end_headers()
            for _ in range(128):
                try: self.wfile.write(b'\x00' * 32768); self.wfile.flush()
                except Exception: return
                time.sleep(0.3)
            return
        if self.path.startswith('/dl'):   # starts /slow.bin?n=<tag> after 800 ms
            tag = self.path.split('n=')[-1] if 'n=' in self.path else 'x'
            self.page(("<title>dl</title><a id=d href='/slow.bin?n=%s'>f</a><script>setTimeout(()=>document.getElementById('d').click(),800)</script>" % tag).encode()); return
        if self.path.startswith('/pat'):   # a high-contrast page (stripes + large text) to see a blur in captures
            c = self.path.split('c=')[-1][:6] if 'c=' in self.path else '3366ee'
            body = ("<title>pat %s</title><body style='margin:0;font:700 64px sans-serif;background:repeating-linear-gradient(90deg,#%s 0 40px,#fff 40px 80px)'>"
                    "<div style='padding:60px;color:#111;background:rgba(255,255,255,.7)'>PAGE %s<br>Lorem ipsum dolor sit amet</div></body>") % (c, c, c.upper())
            self.page(body.encode()); return
        if self.path.startswith('/opener'):
            c = self.path.split('c=')[-1][:6] if 'c=' in self.path else 'ee3333'
            self.page(("<title>opener</title><a id=o target=_blank href='/pat?c=%s'>open</a><script>setTimeout(()=>window.open('/pat?c=%s','_blank'),1200)</script>" % (c, c)).encode()); return
        if self.path.startswith('/show'):
            self.page(b"<title>show</title>show"); return
        self.page(b"<title>ok</title>ok")
http.server.ThreadingHTTPServer(('127.0.0.1', PORT), H).serve_forever()
