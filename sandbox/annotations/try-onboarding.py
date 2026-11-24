#!/usr/bin/env python3
"""Essai de l'onboarding du mode Annotation (#125, variante A), comme un nouvel utilisateur.

    python sandbox/annotations/try-onboarding.py --app <dossier d'un build> [--keep] [--english]

1. copie le build dans un dossier temporaire neuf (%TEMP%\\browser-essai-onboarding-annotation\\app) : profil, réglages et
   session vides, donc un nouvel utilisateur. --keep garde la copie de l'essai précédent : l'onboarding, déjà vu ou passé,
   ne revient pas (c'est la règle « une seule fois »).
2. sert la page de démonstration de la sandbox (la « Boutique » de serve.py) sur http://127.0.0.1:5173/, depuis ce processus.
3. ouvre le navigateur sur cette page. La connexion des agents est redirigée vers un dossier de test
   (BROWSER_ANNOTATIONS_CONNECT_ROOT, comme run-sandbox.py --agents sandbox) : « Connecter avec Claude / Codex » écrit dans
   de fausses configurations, jamais dans les vraies.
4. à la fermeture du navigateur, arrête la page de démonstration : rien ne reste en marche.

Le profil réel (%LOCALAPPDATA%\\browser) n'est jamais lu ni écrit : la copie est portable, ses données restent à côté d'elle.
"""
from __future__ import annotations
import argparse
import os
import pathlib
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time

HERE = pathlib.Path(__file__).resolve().parent
URL = 'http://127.0.0.1:5173/'


def port_open(host: str, port: int) -> bool:
    try:
        with socket.create_connection((host, port), timeout=0.3):
            return True
    except OSError:
        return False


def wait_port(host: str, port: int, timeout: float = 10.0) -> bool:
    end = time.time() + timeout
    while time.time() < end:
        if port_open(host, port):
            return True
        time.sleep(0.1)
    return False


def prepare_agents(root: pathlib.Path) -> None:
    """Faux dossiers de configuration de Claude et Codex (la détection regarde leur présence)."""
    claude = root / 'AppData' / 'Roaming' / 'Claude'
    codex = root / 'home' / '.codex'
    for d in (root / 'AppData' / 'Local', claude, codex):
        d.mkdir(parents=True, exist_ok=True)
    cfg = claude / 'claude_desktop_config.json'
    if not cfg.exists():
        cfg.write_text('{\n  "mcpServers": {}\n}\n', encoding='utf-8', newline='\n')
    toml = codex / 'config.toml'
    if not toml.exists():
        toml.write_text('# config.toml de TEST (essai de l\'onboarding) : vos vrais réglages Codex ne sont pas ici\n', encoding='utf-8', newline='\n')


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--app', required=True, help='dossier du build (browser.exe, assets\\…)')
    ap.add_argument('--keep', action='store_true', help="reprendre l'essai précédent (sans remise à zéro)")
    ap.add_argument('--english', action='store_true', help='interface en anglais')
    ap.add_argument('--dry-run', action='store_true', help='tout préparer (copie, page de démonstration) sans ouvrir le navigateur (vérification sans écran)')
    a = ap.parse_args()
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding='utf-8')
        except Exception:
            pass
    app = pathlib.Path(a.app).resolve()
    if not (app / 'browser.exe').is_file():
        print(f'Build introuvable : {app}\\browser.exe', file=sys.stderr)
        return 1
    home = pathlib.Path(tempfile.gettempdir()) / 'browser-essai-onboarding-annotation'
    run, agents = home / 'app', home / 'agents'
    if not a.keep or not (run / 'browser.exe').is_file():
        if run.exists():
            try:
                shutil.rmtree(run)
            except OSError:
                print("Un navigateur de l'essai précédent est encore ouvert : fermez-le, puis relancez.", file=sys.stderr)
                return 1
        shutil.copytree(app, run, ignore=shutil.ignore_patterns('settings', 'session', 'sessions', 'profile', 'profiles', 'profiles.txt', 'annotations', 'ui-language', '*.log'))
        if agents.exists():
            shutil.rmtree(agents)
        print('Nouvel utilisateur : profil vide.')
    else:
        print("Reprise de l'essai précédent (l'onboarding déjà vu ne revient pas).")
    if a.english:
        (run / 'ui-language').write_text('browser-ui-language 1\nlanguage=en\n', encoding='utf-8', newline='\n')   # src/ui-language.h
    prepare_agents(agents)
    if port_open('127.0.0.1', 5173):
        print('Le port 5173 est déjà pris (une autre sandbox tourne ?) : fermez-la, puis relancez.', file=sys.stderr)
        return 1
    # The demo page is served from this process (serve.py's handlers): the « Boutique » on 127.0.0.1:5173 first; the
    # page's cross-origin iframe (127.0.0.2:5174) only when that address can be bound — it is not needed here.
    sys.path.insert(0, str(HERE))
    import serve
    servers = [serve.make(serve.MAIN, HERE / 'site', serve.SpaHandler)]
    for srv in servers:
        threading.Thread(target=srv.serve_forever, daemon=True).start()
    def cross() -> None:
        try:
            srv = serve.make(serve.CROSS, HERE / 'cross', serve.Handler); servers.append(srv); srv.serve_forever()
        except OSError:
            pass
    threading.Thread(target=cross, daemon=True).start()
    try:
        if not wait_port('127.0.0.1', 5173):
            print('La page de démonstration ne démarre pas.', file=sys.stderr)
            return 1
        env = dict(os.environ)
        env['BROWSER_ANNOTATIONS_CONNECT_ROOT'] = str(agents)
        env['BROWSER_ANNOTATIONS_TEST'] = '1'   # garde-fou : aucune écriture dans les vrais dossiers, même si la redirection échouait
        env.pop('BROWSER_ANNOTATIONS_CONNECT_PATH', None)
        print(f'Navigateur : {run}\\browser.exe')
        print(f'Page       : {URL}')
        if a.dry_run:
            import urllib.request
            with urllib.request.build_opener(urllib.request.ProxyHandler({})).open(URL, timeout=5) as r:
                print(f'--dry-run : page de démonstration {r.status}, navigateur non lancé ({run / "browser.exe"} {URL})')
            return 0
        print('Fermez le navigateur pour terminer l\'essai (cette fenêtre se fermera).')
        proc = subprocess.Popen([str(run / 'browser.exe'), URL], cwd=str(run), env=env)
        proc.wait()
        return 0
    finally:
        for srv in list(servers):
            srv.shutdown(); srv.server_close()


if __name__ == '__main__':
    sys.exit(main())
