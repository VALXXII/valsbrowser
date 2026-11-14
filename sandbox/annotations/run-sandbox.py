#!/usr/bin/env python3
"""Lance tout le sandbox d'annotations (V2) en une commande.

    python sandbox/annotations/run-sandbox.py [--build] [--agents sandbox|real] [--reset-launch] [--no-browser]

1. construit le preview s'il manque (ou --build) : tools/release/build.py --out build/annotations-preview --channel dev --preview
2. démarre serve.py (127.0.0.1:5173 + 127.0.0.2:5174)
3. prépare la connexion des agents :
     --agents sandbox (défaut) : « Connecter avec Claude / Codex » écrit dans un DOSSIER DE TEST
       (build/annotations-sandbox/agent-configs) au lieu de vos vraies configurations. Le navigateur reçoit
       BROWSER_ANNOTATIONS_CONNECT_ROOT ; ce dossier contient un faux %APPDATA%\\Claude et un faux .codex
       (avec une entrée « autre-serveur » pour voir qu'elle est conservée). Aucun .mcpb n'est ouvert, la CLI
       codex n'est pas lancée (édition directe du config.toml de test).
     --agents real : aucune redirection. Un clic sur « Connecter avec Claude » MODIFIE VOTRE VRAIE
       configuration de Claude Desktop (%APPDATA%\\Claude\\claude_desktop_config.json, ou ouvre un .mcpb si
       ce type de fichier est associé à Claude) ; « Connecter avec Codex » modifie %USERPROFILE%\\.codex\\config.toml
       (ou lance « codex mcp add »). Sauvegarde horodatée avant toute écriture ; « Déconnecter » retire l'entrée.
4. ouvre build/annotations-preview/browser.exe sur http://127.0.0.1:5173/ (browser.exe n'accepte pas d'URL :
   la session du preview est écrite seulement s'il n'en a aucune ; sinon l'adresse est affichée)
5. attend la fermeture du navigateur (ou Ctrl+C), puis arrête les serveurs : rien ne reste en marche.

--reset-launch remet à 0 le compteur de « Plus tard » du preview (la modale de connexion se rouvre au
prochain lancement du mode). Le build preview a son propre profil, ses réglages et son magasin
(build/annotations-preview/…) : aucun profil de navigation existant n'est utilisé.
"""
from __future__ import annotations
import argparse
import json
import os
import pathlib
import socket
import subprocess
import sys
import time

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
PREVIEW = ROOT / 'build' / 'annotations-preview'
SANDBOX = ROOT / 'build' / 'annotations-sandbox'
AGENT_ROOT = SANDBOX / 'agent-configs'
URL = 'http://127.0.0.1:5173/'
SERVER = 'browser-annotations-preview'


def wait_port(host, port, timeout=10.0):
    end = time.time() + timeout
    while time.time() < end:
        try:
            with socket.create_connection((host, port), timeout=0.5):
                return True
        except OSError:
            time.sleep(0.1)
    return False


def prepare_agent_root() -> None:
    """Faux dossiers de configuration (détection = présence du dossier, comme sur un vrai poste)."""
    claude = AGENT_ROOT / 'AppData' / 'Roaming' / 'Claude'
    codex = AGENT_ROOT / 'home' / '.codex'
    (AGENT_ROOT / 'AppData' / 'Local').mkdir(parents=True, exist_ok=True)
    claude.mkdir(parents=True, exist_ok=True)
    codex.mkdir(parents=True, exist_ok=True)
    cfg = claude / 'claude_desktop_config.json'
    if not cfg.exists():
        cfg.write_text('{\n  "mcpServers": {\n    "autre-serveur": {\n      "command": "C:\\\\outils\\\\autre.exe",\n      "args": ["--port", "1234"]\n    }\n  },\n'
                       '  "globalShortcut": "Ctrl+Space"\n}\n', encoding='utf-8', newline='\n')
    toml = codex / 'config.toml'
    if not toml.exists():
        toml.write_text('# config.toml de TEST (sandbox) : vos vrais réglages Codex ne sont pas ici\nmodel = "gpt-5-codex"\n\n'
                        '[mcp_servers.autre-serveur]\ncommand = \'C:\\outils\\autre.exe\'\nargs = ["--port", "1234"]\n', encoding='utf-8', newline='\n')


def reset_launch() -> None:
    settings = PREVIEW / 'settings'
    if not settings.exists():
        return
    lines = settings.read_text(encoding='utf-8').splitlines()
    lines = [l for l in lines if not l.startswith('annotation-connect-later=')] + ['annotation-connect-later=0']
    settings.write_text('\n'.join(lines) + '\n', encoding='utf-8', newline='\n')
    print('Compteur « Plus tard » remis à 0 (build/annotations-preview/settings).')


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--build', action='store_true', help='reconstruire le preview avant de lancer')
    ap.add_argument('--agents', choices=['sandbox', 'real'], default='sandbox',
                    help='sandbox (défaut) : la connexion des agents écrit dans un dossier de test ; real : dans vos vraies configs')
    ap.add_argument('--reset-launch', action='store_true', help='la modale « Connecter un agent » se rouvrira (compteur « Plus tard » à 0)')
    ap.add_argument('--no-browser', action='store_true', help='serveurs seulement (Ctrl+C pour arrêter)')
    a = ap.parse_args()
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding='utf-8')
        except Exception:
            pass
    browser, bridge = PREVIEW / 'browser.exe', PREVIEW / 'browser-annotations-mcp.exe'
    if a.build or (not a.no_browser and not browser.is_file()):
        subprocess.run([sys.executable, str(ROOT / 'tools/release/build.py'), '--out', 'build/annotations-preview',
                        '--channel', 'dev', '--preview'], cwd=ROOT, check=True)
    if a.reset_launch:
        reset_launch()
    env = dict(os.environ)
    if a.agents == 'sandbox':
        prepare_agent_root()
        env['BROWSER_ANNOTATIONS_CONNECT_ROOT'] = str(AGENT_ROOT)
        env['BROWSER_ANNOTATIONS_TEST'] = '1'   # guard: refuses any write in the real folders even if the redirection failed
        env.pop('BROWSER_ANNOTATIONS_CONNECT_PATH', None)
    else:
        env.pop('BROWSER_ANNOTATIONS_CONNECT_ROOT', None)
        env.pop('BROWSER_ANNOTATIONS_TEST', None)
    server = subprocess.Popen([sys.executable, str(HERE / 'serve.py')], cwd=ROOT)
    try:
        if not (wait_port('127.0.0.1', 5173) and wait_port('127.0.0.2', 5174)):
            print('Les serveurs du sandbox ne répondent pas (ports 5173/5174 déjà pris ?)', file=sys.stderr)
            return 1
        opened_by_session = False
        sessions = PREVIEW / 'sessions'
        if not a.no_browser and not (PREVIEW / 'session').exists() and not (sessions.is_dir() and any(sessions.iterdir())):
            (PREVIEW / 'session').write_text('browser-session-2\n0\n1\n' + json.dumps(URL) + '\n0\n', encoding='utf-8', newline='\n')
            opened_by_session = True
        store = PREVIEW / 'annotations'
        print('\n=== Sandbox annotations (V2) ===')
        print(f'Page            : {URL}')
        print(f'Navigateur      : {browser}')
        print(f'Connecteur MCP  : {bridge}')
        print(f'Magasin         : {store}')
        if a.agents == 'sandbox':
            print(f'Connexion agents: SANDBOX — « Connecter avec Claude / Codex » écrit dans {AGENT_ROOT}')
            print(f'                  (Claude : {AGENT_ROOT / "AppData/Roaming/Claude/claude_desktop_config.json"},')
            print(f'                   Codex  : {AGENT_ROOT / "home/.codex/config.toml"}) — vos vraies configs ne sont pas touchées.')
        else:
            print('Connexion agents: RÉELLE — un clic sur « Connecter avec … » modifie la configuration de votre app')
            print('                  (sauvegarde .bak horodatée à côté ; « Déconnecter » retire l\'entrée).')
        if not a.no_browser and not opened_by_session:
            print(f'\nSession preview existante conservée : ouvrez {URL} dans un onglet (Ctrl+L, collez, Entrée).')
        print(f'\nRéglage manuel (repli, à coller vous-même) — serveur « {SERVER} » :')
        print(json.dumps({'mcpServers': {SERVER: {'command': str(bridge), 'args': ['--store', str(store)]}}}, indent=2))
        tm = 'python sandbox/annotations/try-mcp.py'
        print('\nJouer l\'agent :')
        print(f'  {tm} demo            # attend la prochaine annotation, la prend (« s\'en occupe »), la termine 8 s plus tard')
        print(f'  {tm} list --site http://127.0.0.1:5173')
        print(f'  {tm} get <id>        # contexte + capture → build/annotations-sandbox/<id>.png')
        print(f'  {tm} claim <id>  |  resolve <id> <résumé>  |  release <id>  |  wait')
        print(f'\nScénario pas à pas : {HERE / "README.md"}')
        if a.no_browser:
            print('\nServeurs en marche — Ctrl+C pour arrêter.')
            server.wait()
            return 0
        proc = subprocess.Popen([str(browser)], cwd=str(PREVIEW), env=env)
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
