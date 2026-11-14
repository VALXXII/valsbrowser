#!/usr/bin/env python3
"""Annotation sandbox server — standard library only.

    python sandbox/annotations/serve.py            # until Ctrl+C

  http://127.0.0.1:5173/   the « Boutique » checkout page (site/), SPA fallback to index.html
  http://127.0.0.2:5174/   the cross-origin iframe (cross/), a second loopback address = another origin

Loopback only, nothing else is reachable. No cache (every reload shows the files on disk).
"""
from __future__ import annotations
import functools
import http.server
import pathlib
import sys
import threading

HERE = pathlib.Path(__file__).resolve().parent
MAIN = ('127.0.0.1', 5173)
CROSS = ('127.0.0.2', 5174)


class Handler(http.server.SimpleHTTPRequestHandler):
    spa = False

    def end_headers(self):
        self.send_header('Cache-Control', 'no-store')
        super().end_headers()

    def send_head(self):
        path = self.translate_path(self.path)
        if self.spa and not pathlib.Path(path).exists() and '.' not in pathlib.PurePosixPath(self.path.split('?')[0]).name:
            self.path = '/index.html'   # SPA routes (/merci) serve the app
        return super().send_head()

    def log_message(self, fmt, *args):
        sys.stderr.write('[sandbox %s] %s\n' % (self.server.server_address[1], fmt % args))


class SpaHandler(Handler):
    spa = True


def make(address, root: pathlib.Path, handler):
    server = http.server.ThreadingHTTPServer(address, functools.partial(handler, directory=str(root)))
    server.daemon_threads = True
    return server


def main() -> int:
    servers = [make(MAIN, HERE / 'site', SpaHandler), make(CROSS, HERE / 'cross', Handler)]
    for s in servers:
        threading.Thread(target=s.serve_forever, daemon=True).start()
    print(f'Sandbox: http://{MAIN[0]}:{MAIN[1]}/  (iframe croisée http://{CROSS[0]}:{CROSS[1]}/cross.html) — Ctrl+C pour arrêter', flush=True)
    try:
        threading.Event().wait()
    except KeyboardInterrupt:
        pass
    finally:
        for s in servers:
            s.shutdown()
            s.server_close()
    return 0


if __name__ == '__main__':
    sys.exit(main())
