#!/usr/bin/env python3
"""Lance tout le sandbox d'annotations en une commande.

    python sandbox/annotations/run-sandbox.py [--build] [--no-browser]

1. (--build) construit le preview : tools/release/build.py --out build/annotations-preview --channel dev --preview
2. démarre serve.py (127.0.0.1:5173 + 127.0.0.2:5174)
3. ouvre build/annotations-preview/browser.exe sur http://127.0.0.1:5173/ — browser.exe n'accepte pas d'URL
   en argument : le script écrit la session du build preview (build/annotations-preview/session) SEULEMENT si
   elle n'existe pas ; sinon il affiche l'adresse à ouvrir à la main (la session existante n'est pas touchée)
4. affiche la configuration MCP et les commandes try-mcp.py
5. attend la fermeture du navigateur (ou Ctrl+C), puis arrête les serveurs : rien ne reste en marche.

Le build preview a son propre profil (build/annotations-preview/profile) et son propre magasin
(build/annotations-preview/annotations) : aucun profil de navigation existant n'est utilisé.
"""
from __future__ import annotations
import argparse
import json
import pathlib
import socket
import subprocess
import sys
import time

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
PREVIEW = ROOT / 'build' / 'annotations-preview'
URL = 'http://127.0.0.1:5173/'


def wait_port(host, port, timeout=10.0):
    end = time.time() + timeout
    while time.time() < end:
        try:
            with socket.create_connection((host, port), timeout=0.5):
                return True
        except OSError:
            time.sleep(0.1)
    return False


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--build', action='store_true', help='reconstruire le preview avant de lancer')
    ap.add_argument('--no-browser', action='store_true', help='serveurs seulement (Ctrl+C pour arrêter)')
    a = ap.parse_args()
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding='utf-8')
        except Exception:
            pass
    if a.build:
        subprocess.run([sys.executable, str(ROOT / 'tools/release/build.py'), '--out', 'build/annotations-preview',
                        '--channel', 'dev', '--preview'], cwd=ROOT, check=True)
    browser, bridge = PREVIEW / 'browser.exe', PREVIEW / 'browser-annotations-mcp.exe'
    if not a.no_browser and not browser.is_file():
        sys.exit(f'{browser} introuvable : relancez avec --build')
    server = subprocess.Popen([sys.executable, str(HERE / 'serve.py')], cwd=ROOT)
    try:
        if not (wait_port('127.0.0.1', 5173) and wait_port('127.0.0.2', 5174)):
            print('Les serveurs du sandbox ne répondent pas (ports 5173/5174 déjà pris ?)', file=sys.stderr)
            return 1
        session = PREVIEW / 'session'
        opened_by_session = False
        if not a.no_browser:
            if not session.exists():
                session.write_text('browser-session-2\n0\n1\n' + json.dumps(URL) + '\n0\n', encoding='utf-8', newline='\n')
                opened_by_session = True
        store = PREVIEW / 'annotations'
        print('\n=== Sandbox annotations ===')
        print(f'Page            : {URL}')
        print(f'Navigateur      : {browser}')
        print(f'Connecteur MCP  : {bridge}')
        print(f'Magasin         : {store}')
        if not a.no_browser and not opened_by_session:
            print(f'\nSession preview existante conservée : ouvrez {URL} dans un onglet (Ctrl+L, collez, Entrée).')
        print('\nConfig Claude Desktop (à coller à la main, rien n\'est écrit pour vous) :')
        print(json.dumps({'mcpServers': {'browser-annotations-preview': {'command': str(bridge), 'args': []}}}, indent=2))
        print('\nConfig Codex (~/.codex/config.toml, à la main) :')
        print(f"[mcp_servers.browser-annotations-preview]\ncommand = '{bridge}'\nargs = []")
        tm = 'python sandbox/annotations/try-mcp.py'
        print('\nSans agent :')
        print(f'  {tm} list            # toutes les annotations du magasin preview')
        print(f'  {tm} get <id>        # contexte + capture enregistrée dans build/annotations-sandbox/')
        print(f'  {tm} claim <id>')
        print(f'  {tm} resolve <id> Alignement corrigé dans Checkout.tsx')
        print(f'  {tm} release <id>')
        print(f'\nScénario pas à pas : {HERE / "README.md"}')
        if a.no_browser:
            print('\nServeurs en marche — Ctrl+C pour arrêter.')
            server.wait()
            return 0
        proc = subprocess.Popen([str(browser)], cwd=str(PREVIEW))
        print(f'\nNavigateur lancé (pid {proc.pid}). Fermez-le (ou Ctrl+C ici) pour tout arrêter.')
        try:
            proc.wait()
        except KeyboardInterrupt:
            print('\nCtrl+C : le navigateur reste ouvert, les serveurs s\'arrêtent.')
        return 0
    finally:
        server.terminate()
        try:
            server.wait(timeout=5)
        except subprocess.TimeoutExpired:
            server.kill()
        print('Serveurs du sandbox arrêtés.')


if __name__ == '__main__':
    sys.exit(main())
