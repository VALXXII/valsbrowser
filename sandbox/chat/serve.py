#!/usr/bin/env python3
"""Serveur du bac à sable du chat — bibliothèque standard seulement.

    python sandbox/chat/serve.py [--root <dossier>] [--port 5183]

  http://127.0.0.1:5183/   la page « Boutique — Paiement » (site/)

Le processus se place DANS le dossier servi (os.chdir) : le navigateur retrouve ainsi le dossier du site à partir
du port, comme pour un vrai serveur de développement. Boucle locale seulement, aucun cache.

Comme le fait Astro en développement, les pages HTML reçoivent à la volée leurs attributs source :
  data-astro-source-file="@"  ->  chemin absolu du fichier servi
  data-astro-source-loc="@"   ->  ligne:colonne réelles de la balise dans ce fichier
et window.__CHAT_SANDBOX_ROOT__ (pour app.js, qui simule les données de debug de React). Les fichiers sur le
disque gardent leurs « @ » : un agent qui modifie index.html ne casse rien.
"""
from __future__ import annotations
import argparse
import functools
import http.server
import json
import os
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent
HOST = '127.0.0.1'
PORT = 5183
TAG = re.compile(r'<[a-zA-Z][^<>]*data-astro-source-file="@"[^<>]*>')


def stamp(text: str, file: pathlib.Path, root: pathlib.Path) -> str:
    """Remplace les « @ » par le chemin absolu du fichier et la position réelle de chaque balise."""
    path = str(file).replace('\\', '/')
    lines = text.split('\n')
    for number, line in enumerate(lines, start=1):
        def one(match: re.Match) -> str:
            tag = match.group(0)
            column = match.start() + 1
            tag = tag.replace('data-astro-source-file="@"', f'data-astro-source-file="{path}"')
            return tag.replace('data-astro-source-loc="@"', f'data-astro-source-loc="{number}:{column}"')
        lines[number - 1] = TAG.sub(one, line)
    out = '\n'.join(lines)
    script = '<script>window.__CHAT_SANDBOX_ROOT__=' + json.dumps(str(root).replace('\\', '/')) + ';</script>'
    return out.replace('<script src="app.js"></script>', script + '\n<script src="app.js"></script>')


class Handler(http.server.SimpleHTTPRequestHandler):
    root: pathlib.Path = HERE / 'site'

    def end_headers(self):
        self.send_header('Cache-Control', 'no-store')
        super().end_headers()

    def do_GET(self):
        name = self.path.split('?')[0].split('#')[0]
        if name.endswith('/'):
            name += 'index.html'
        target = pathlib.Path(self.translate_path(name))
        if target.suffix == '.html' and target.is_file():
            body = stamp(target.read_text(encoding='utf-8'), target.resolve(), self.root).encode('utf-8')
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        super().do_GET()

    def log_message(self, fmt, *args):
        sys.stderr.write('[chat-sandbox] %s\n' % (fmt % args))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--root', default=str(HERE / 'site'), help='dossier servi (défaut : sandbox/chat/site)')
    ap.add_argument('--port', type=int, default=PORT)
    a = ap.parse_args()
    root = pathlib.Path(a.root).resolve()
    if not (root / 'index.html').is_file():
        print(f'Dossier sans index.html : {root}', file=sys.stderr)
        return 1
    os.chdir(root)   # le dossier de travail du serveur EST le dossier du projet (détection par le port)
    Handler.root = root
    server = http.server.ThreadingHTTPServer((HOST, a.port), functools.partial(Handler, directory=str(root)))
    server.daemon_threads = True
    print(f'Bac à sable du chat : http://{HOST}:{a.port}/  (dossier {root}) — Ctrl+C pour arrêter', flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == '__main__':
    sys.exit(main())
