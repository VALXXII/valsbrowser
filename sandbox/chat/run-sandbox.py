#!/usr/bin/env python3
"""Lance le bac à sable du chat intégré en une commande.

    python sandbox/chat/run-sandbox.py [--fake-agent] [--build] [--reset] [--no-browser]

1. construit le navigateur d'essai s'il manque (ou --build) :
       tools/release/build.py --out build/chat-preview --channel dev --preview
2. copie le petit site sandbox/chat/site dans build/chat-sandbox/site : c'est CETTE copie que l'agent modifie
   (les fichiers du dépôt ne changent jamais ; --reset remet la copie à neuf)
3. démarre serve.py sur http://127.0.0.1:5183/ (son dossier de travail = la copie du site, pour que le
   navigateur retrouve seul le dossier du projet)
4. choisit l'agent :
     --fake-agent : un FAUX agent (build/chat-preview-test/fake-acp-agent.exe). Aucun compte, aucun réseau,
                    aucun coût. À un message ordinaire il répond par une vraie petite modification (le bouton
                    « Payer » passe à droite), après vous avoir demandé l'accord. Des mots spéciaux déclenchent
                    d'autres scénarios : « think », « plan », « slow », « question », « crash »…
                    (liste : tests/acp/fake_agent.cpp).
     sans option  : VOS vrais agents (Claude Code, Codex), avec votre compte. Un message = une vraie demande.
                    Au premier message, le navigateur télécharge son Node privé et l'adaptateur (une fois),
                    dans build/chat-preview/agents — rien n'est installé ailleurs.
5. ouvre build/chat-preview/browser.exe sur la page, attend sa fermeture (ou Ctrl+C), puis arrête le serveur :
   rien ne reste en marche.

Dans les deux cas « Connecter avec Claude / Codex » (annotations par MCP) écrit dans un dossier de test
(build/chat-sandbox/agent-configs), jamais dans vos vraies configurations.
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
import time

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
PREVIEW = ROOT / 'build' / 'chat-preview'
TOOLS = ROOT / 'build' / 'chat-preview-test'
SANDBOX = ROOT / 'build' / 'chat-sandbox'
SITE = SANDBOX / 'site'
AGENT_ROOT = SANDBOX / 'agent-configs'
PORT = 5183
FAKE_EDIT = 'styles/checkout.css|margin-top: 4px;|margin-top: 4px; margin-left: auto;'
URL = f'http://127.0.0.1:{PORT}/landing.html'


def wait_port(port: int, timeout: float = 10.0) -> bool:
    end = time.time() + timeout
    while time.time() < end:
        try:
            with socket.create_connection(('127.0.0.1', port), timeout=0.5):
                return True
        except OSError:
            time.sleep(0.1)
    return False


def prepare_site(reset: bool) -> None:
    """La copie de travail du site : ce que l'agent a le droit de modifier."""
    if reset and SITE.exists():
        shutil.rmtree(SITE)
    if not SITE.exists():
        shutil.copytree(HERE / 'site', SITE)


def prepare_agent_root() -> None:
    """Faux dossiers de configuration pour la connexion MCP (jamais les vrais)."""
    for sub in ('AppData/Roaming/Claude', 'AppData/Local', 'home/.codex'):
        (AGENT_ROOT / sub).mkdir(parents=True, exist_ok=True)


def sandbox_env(fake_agent: bool) -> dict:
    env = dict(os.environ)
    prepare_agent_root()
    env['BROWSER_ANNOTATIONS_CONNECT_ROOT'] = str(AGENT_ROOT)
    env['BROWSER_ANNOTATIONS_TEST'] = '1'   # garde-fou : aucune écriture dans les vraies configurations
    env.pop('BROWSER_ANNOTATIONS_CONNECT_PATH', None)
    env.pop('BROWSER_CHAT_FAKE_AGENT', None)
    if fake_agent:
        fake = TOOLS / 'fake-acp-agent.exe'
        if not fake.is_file():
            raise SystemExit(f'Faux agent introuvable : {fake} (relancez avec --build)')
        # --edit : la modification que le faux agent applique VRAIMENT (après la carte d'autorisation) quand le
        # message ne contient aucun mot de scénario : le bouton « Payer » passe à droite.
        env['BROWSER_CHAT_FAKE_AGENT'] = f'"{fake}" --resume --log "{SANDBOX / "fake-agent.log"}" --edit "{FAKE_EDIT}"'
    return env


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--fake-agent', action='store_true', help='faux agent : aucun compte, aucun coût')
    ap.add_argument('--build', action='store_true', help='reconstruire build/chat-preview avant de lancer')
    ap.add_argument('--reset', action='store_true', help='remettre à neuf la copie du site (build/chat-sandbox/site)')
    ap.add_argument('--no-browser', action='store_true', help='serveur seulement (Ctrl+C pour arrêter)')
    a = ap.parse_args()
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding='utf-8')
        except Exception:
            pass
    browser = PREVIEW / 'browser.exe'
    if a.build or (not a.no_browser and not browser.is_file()):
        subprocess.run([sys.executable, str(ROOT / 'tools/release/build.py'), '--out', 'build/chat-preview',
                        '--channel', 'dev', '--preview'], cwd=ROOT, check=True)
    SANDBOX.mkdir(parents=True, exist_ok=True)
    prepare_site(a.reset)
    env = sandbox_env(a.fake_agent)
    server = subprocess.Popen([sys.executable, str(HERE / 'serve.py'), '--root', str(SITE), '--port', str(PORT)], cwd=str(SITE))
    try:
        if not wait_port(PORT):
            print(f'Le serveur du bac à sable ne répond pas (port {PORT} déjà pris ?)', file=sys.stderr)
            return 1
        sessions = PREVIEW / 'sessions'
        opened_by_session = False
        if not a.no_browser and not (PREVIEW / 'session').exists() and not (sessions.is_dir() and any(sessions.iterdir())):
            (PREVIEW / 'session').write_text('browser-session-2\n0\n1\n' + json.dumps(URL) + '\n0\n', encoding='utf-8', newline='\n')
            opened_by_session = True
        print('\n=== Bac à sable du chat intégré ===')
        print(f'Page        : {URL}')
        print(f'Navigateur  : {browser}')
        print(f'Site modifié: {SITE}   (copie de sandbox/chat/site ; --reset pour repartir de zéro)')
        if a.fake_agent:
            print('Agent       : FAUX agent — aucun compte, aucun coût.')
            print('              Un message ordinaire : il demande votre accord puis met VRAIMENT le bouton « Payer » à droite')
            print('              (styles/checkout.css). Mots spéciaux : think (réflexion), plan, slow (pour tester Stop),')
            print('              question, crash, refusal (liste : tests/acp/fake_agent.cpp).')
        else:
            print('Agent       : VOS vrais agents (Claude Code / Codex). Chaque message est une vraie demande à votre compte.')
            print(f'              Node privé et adaptateur téléchargés une fois dans {PREVIEW / "agents"}')
        print('\nPour essayer :')
        print('  1. Ctrl+Maj+D (ou Ctrl+Maj+A puis « Demander à l\'agent » dans le bandeau) ouvre le chat à droite.')
        print('  2. La première fois : « Utiliser » le dossier détecté (c\'est la copie du site ci-dessus).')
        print('  3. Cliquez le bouton « Payer 48,00 € » dans la page : il devient une puce dans la saisie.')
        print('  4. Écrivez votre demande, Entrée.')
        if not a.no_browser and not opened_by_session:
            print(f'\nUne session existe déjà : ouvrez {URL} dans un onglet (Ctrl+L, collez, Entrée).')
        print(f'\nMode d\'emploi pas à pas : {HERE / "README.md"}')
        if a.no_browser:
            print('\nServeur en marche — Ctrl+C pour arrêter.')
            server.wait()
            return 0
        proc = subprocess.Popen([str(browser)], cwd=str(PREVIEW), env=env)
        print(f'\nNavigateur lancé (pid {proc.pid}). Fermez-le (ou Ctrl+C ici) pour tout arrêter.')
        try:
            proc.wait()
        except KeyboardInterrupt:
            proc.terminate()
        return 0
    except KeyboardInterrupt:
        return 0
    finally:
        server.terminate()
        try:
            server.wait(timeout=5)
        except subprocess.TimeoutExpired:
            server.kill()


if __name__ == '__main__':
    sys.exit(main())
