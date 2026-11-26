#!/usr/bin/env python3
"""Iframes case 2 — a local demo page with an iframe of ANOTHER site (two local origins, no internet).

    python tests/annotations/iframe-demo/serve.py [--shop 5180] [--other 5181]

Serves the shop on http://localhost:<shop>/ (a local project: the annotation mode is on there) and « the other site » on
http://127.0.0.1:<other>/ (another origin: another host and another port). Open http://localhost:<shop>/ in the browser,
turn the annotation mode on (Ctrl+Maj+A) and hover the « Paiement sécurisé » frame: the whole frame is outlined with
« iframe · autre site » inside it and the card appears under it (Paper « Iframes · cas 2 · v2 »). The « Livraison » frame
comes from the shop itself (same origin): its elements are reached one by one, no card. The « Carte » frame sits near the
bottom right of a 1180 × 740 window: its card goes above it, slid to stay in the page. Ctrl+C stops the servers.
"""
import argparse, functools, http.server, os, threading

HERE = os.path.dirname(os.path.abspath(__file__))


class Quiet(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *a): pass
    def end_headers(self): self.send_header('Cache-Control', 'no-store'); super().end_headers()


class Shop(Quiet):
    other = ''
    def do_GET(self):
        if self.path in ('/', '/index.html'):
            body = open(os.path.join(HERE, 'shop.html'), encoding='utf-8').read().replace('{{OTHER}}', self.other).encode('utf-8')
            self.send_response(200); self.send_header('Content-Type', 'text/html; charset=utf-8'); self.send_header('Content-Length', str(len(body))); self.end_headers()
            self.wfile.write(body); return
        super().do_GET()


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--shop', type=int, default=5180); p.add_argument('--other', type=int, default=5181)
    a = p.parse_args()
    Shop.other = f'http://127.0.0.1:{a.other}'
    shop = http.server.ThreadingHTTPServer(('127.0.0.1', a.shop), functools.partial(Shop, directory=HERE))   # opened as http://localhost:<shop>/
    other = http.server.ThreadingHTTPServer(('127.0.0.1', a.other), functools.partial(Quiet, directory=HERE))
    threading.Thread(target=other.serve_forever, daemon=True).start()
    print(f'shop (local project):  http://localhost:{a.shop}/', flush=True)
    print(f'the other site:        http://127.0.0.1:{a.other}/paiement.html (shown in the shop\'s frames)', flush=True)
    try:
        shop.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        shop.shutdown(); other.shutdown()


if __name__ == '__main__':
    main()
