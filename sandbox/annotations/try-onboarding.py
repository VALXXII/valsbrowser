#!/usr/bin/env python3
"""Essai du mode Annotation (#125) comme un nouvel utilisateur : l'onboarding, et où « Annoter » marche.

    python sandbox/annotations/try-onboarding.py --app <dossier d'un build> [--keep] [--private] [--english] [--dry-run]

1. copie le build dans un dossier temporaire neuf (%TEMP%\\browser-essai-onboarding-annotation\\app) : profil, réglages et
   session vides, donc un nouvel utilisateur. --keep garde la copie de l'essai précédent : l'onboarding, déjà vu ou passé,
   ne revient pas (c'est la règle « une seule fois »).
2. sert une COPIE de la page de démonstration de la sandbox (la « Boutique ») sur http://127.0.0.1:5173/ depuis ce
   processus : un agent connecté qui modifie la page modifie la copie, jamais le dépôt. Écrit aussi une page en fichier
   local (…\\fichier-local\\index.html).
3. ouvre le navigateur avec trois onglets : la page locale (Annoter allumé), le fichier local (allumé) et
   https://example.com/ (site en ligne : Annoter éteint, grisé, avec son info-bulle). --private ouvre une fenêtre privée
   sur la page locale (même règle). La connexion des agents est redirigée vers un dossier de test
   (BROWSER_ANNOTATIONS_CONNECT_ROOT, comme run-sandbox.py --agents sandbox) : « Connecter avec Claude / Codex » écrit dans
   de fausses configurations, jamais dans les vraies.
4. à la fermeture du navigateur, arrête la page de démonstration : rien ne reste en marche.

Le profil réel (%LOCALAPPDATA%\\browser) n'est jamais lu ni écrit : la copie est portable, ses données restent à côté d'elle.
"""
from __future__ import annotations
import argparse
import json
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
ONLINE = 'https://example.com/'   # a harmless public page (IANA's example domain): the « site en ligne » case
FILE_PAGE = '''<!doctype html>
<html lang="fr"><head><meta charset="utf-8"><title>Fichier local — essai</title>
<style>
body { margin: 0; font: 15px/1.5 system-ui, sans-serif; background: #f6f6f4; color: #1c1c1c; }
main { max-width: 560px; margin: 64px auto; padding: 32px; background: #fff; border: 1px solid #e4e4e0; border-radius: 12px; }
h1 { font-size: 22px; margin: 0 0 8px; } p { margin: 0 0 20px; color: #555; }
button { height: 44px; padding: 0 24px; border: 0; border-radius: 8px; background: #1c1c1c; color: #fff; font: inherit; font-weight: 500; }
code { background: #f0f0ec; padding: 1px 5px; border-radius: 4px; }
</style></head>
<body><main>
<h1>Un fichier sur votre ordinateur</h1>
<p>Cette page est ouverte depuis un fichier (<code>file://</code>), pas depuis un site : le mode Annotation y marche.</p>
<button type="button">Envoyer le devis</button>
</main></body></html>
'''


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


def write_session(run: pathlib.Path, urls: list[str]) -> None:
    """The first window's tabs (src/session.h, format 2): the local page first and active."""
    lines = ['browser-session-2', '0', str(len(urls))] + [json.dumps(u) for u in urls] + ['0']
    (run / 'session').write_text('\n'.join(lines) + '\n', encoding='utf-8', newline='\n')


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--app', required=True, help='dossier du build (browser.exe, assets\\…)')
    ap.add_argument('--keep', action='store_true', help="reprendre l'essai précédent (sans remise à zéro)")
    ap.add_argument('--private', action='store_true', help="une fenêtre privée sur la page locale (reprend l'essai précédent s'il existe)")
    ap.add_argument('--english', action='store_true', help='interface en anglais')
    ap.add_argument('--dry-run', action='store_true', help='tout préparer (copie, pages) sans ouvrir le navigateur (vérification sans écran)')
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
    run, agents, site, files = home / 'app', home / 'agents', home / 'site', home / 'fichier-local'
    keep = a.keep or a.private
    fresh = not keep or not (run / 'browser.exe').is_file()
    if fresh:
        for d in (run, agents, site):
            if d.exists():
                try:
                    shutil.rmtree(d)
                except OSError:
                    print("Un navigateur de l'essai précédent est encore ouvert : fermez-le, puis relancez.", file=sys.stderr)
                    return 1
        shutil.copytree(app, run, ignore=shutil.ignore_patterns('settings', 'session', 'sessions', 'profile', 'profiles', 'profiles.txt', 'annotations', 'ui-language', '*.log'))
        print('Nouvel utilisateur : profil vide.')
    else:
        print("Reprise de l'essai précédent (l'onboarding déjà vu ne revient pas).")
    if not site.exists():
        shutil.copytree(HERE / 'site', site)   # the page an agent may edit: a copy, never the repository's
    files.mkdir(parents=True, exist_ok=True)
    page = files / 'index.html'
    if not page.exists():
        page.write_text(FILE_PAGE, encoding='utf-8', newline='\n')
    file_url = page.as_uri()
    if fresh:
        write_session(run, [URL, file_url, ONLINE])
    if a.english:
        (run / 'ui-language').write_text('browser-ui-language 1\nlanguage=en\n', encoding='utf-8', newline='\n')   # src/ui-language.h
    prepare_agents(agents)
    # The demo page is served from this process (serve.py's handlers) unless another trial already serves it; the page's
    # cross-origin iframe (127.0.0.2:5174) only when that address can be bound — it is not needed here.
    servers = []
    if not port_open('127.0.0.1', 5173):
        sys.path.insert(0, str(HERE))
        import serve
        servers.append(serve.make(serve.MAIN, site, serve.SpaHandler))
        threading.Thread(target=servers[0].serve_forever, daemon=True).start()
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
        print(f'Navigateur     : {run}\\browser.exe' + (' (fenêtre privée)' if a.private else ''))
        print(f'Page locale    : {URL}   (Annoter allumé)')
        print(f'Fichier local  : {file_url}   (Annoter allumé)')
        print(f'Site en ligne  : {ONLINE}   (Annoter éteint)')
        command = [str(run / 'browser.exe')] + (['--private', URL] if a.private else [])
        if a.dry_run:
            import urllib.request
            with urllib.request.build_opener(urllib.request.ProxyHandler({})).open(URL, timeout=5) as r:
                print(f'--dry-run : page de démonstration {r.status}, session {(run / "session").read_text(encoding="utf-8").splitlines()[2:5] if (run / "session").exists() else "-"}, navigateur non lancé ({" ".join(command)})')
            return 0
        print('Fermez le navigateur pour terminer l\'essai (cette fenêtre se fermera).')
        proc = subprocess.Popen(command, cwd=str(run), env=env)
        proc.wait()
        return 0
    finally:
        for srv in list(servers):
            srv.shutdown(); srv.server_close()


if __name__ == '__main__':
    sys.exit(main())
