#!/usr/bin/env python3
"""Annotation sandbox server — standard library only.

    python sandbox/annotations/serve.py            # until Ctrl+C
    python serve.py --site <dossier> --no-cross    # another folder on 5173 only (try-onboarding.py: its served COPY)

  http://127.0.0.1:5173/   the « Boutique » checkout page (site/), SPA fallback to index.html
  http://127.0.0.2:5174/   the cross-origin iframe (cross/), a second loopback address = another origin

Loopback only, nothing else is reachable. No cache (every reload shows the files on disk). A port already taken is an
error (exit 2), never shared: on Windows the server does not ask for SO_REUSEADDR, which would let two servers bind it.
"""
from __future__ import annotations
import argparse
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


class Server(http.server.ThreadingHTTPServer):
    allow_reuse_address = sys.platform != 'win32'   # Windows' SO_REUSEADDR lets a second server bind a taken port
    daemon_threads = True


def make(address, root: pathlib.Path, handler):
    return Server(address, functools.partial(handler, directory=str(root)))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--site', type=pathlib.Path, default=HERE / 'site', help='the folder served on 127.0.0.1:5173 (default: site/ next to this script)')
    ap.add_argument('--no-cross', action='store_true', help='without the cross-origin iframe (127.0.0.2:5174)')
    ap.add_argument('--parent', type=int, default=0, help='stop when this process ends (the trial launcher: its window closed with the cross)')
    a = ap.parse_args()
    site = a.site.resolve()
    if not (site / 'index.html').is_file():
        print(f'No index.html in {site}', file=sys.stderr)
        return 1
    servers = []
    try:
        servers.append(make(MAIN, site, SpaHandler))
        if not a.no_cross:
            servers.append(make(CROSS, HERE / 'cross', Handler))
    except OSError as e:
        for s in servers:
            s.server_close()
        print(f'Port already taken ({e}): close the other server, then start again.', file=sys.stderr)
        return 2
    for s in servers:
        threading.Thread(target=s.serve_forever, daemon=True).start()
    stop = threading.Event()
    if a.parent and sys.platform == 'win32':
        def watch(pid: int) -> None:   # the launcher gone (its console closed): no server left holding the port
            import ctypes
            k32 = ctypes.WinDLL('kernel32')
            h = k32.OpenProcess(0x00100000, False, pid)   # SYNCHRONIZE
            if not h:
                stop.set(); return
            k32.WaitForSingleObject(ctypes.c_void_p(h), 0xFFFFFFFF)
            k32.CloseHandle(ctypes.c_void_p(h)); stop.set()
        threading.Thread(target=watch, args=(a.parent,), daemon=True).start()
    print(f'Sandbox: http://{MAIN[0]}:{MAIN[1]}/ ({site})' + ('' if a.no_cross else f'  (iframe croisée http://{CROSS[0]}:{CROSS[1]}/cross.html)')
          + ' — Ctrl+C pour arrêter', flush=True)
    try:
        while not stop.wait(0.5):
            pass
    except KeyboardInterrupt:
        pass
    finally:
        for s in servers:
            s.shutdown()
            s.server_close()
    return 0


if __name__ == '__main__':
    sys.exit(main())
