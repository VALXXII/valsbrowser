#!/usr/bin/env python3
"""Essai du mode Annotation (#125) comme un nouvel utilisateur : l'onboarding, et où « Annoter » marche.

    python sandbox/annotations/try-onboarding.py --app <dossier d'un build> [--keep] [--private] [--welcome] [--english] [--local-pill] [--dry-run]

1. copie le build dans un dossier temporaire neuf (%TEMP%\\browser-essai-onboarding-annotation\\app) : profil, réglages et
   session vides, donc un nouvel utilisateur. --keep garde la copie de l'essai précédent : l'onboarding, déjà vu ou passé,
   ne revient pas (c'est la règle « une seule fois »).
2. sert une COPIE de la page de démonstration de la sandbox (la « Boutique ») sur http://127.0.0.1:5173/, par un
   serveur à part (serve.py copié à côté) dont le dossier de travail est la copie : le navigateur, qui reconnaît le
   dossier du projet au processus qui sert la page, ne voit que la copie. Un agent connecté qui modifie la page modifie
   la copie, jamais le dépôt. Si le port 5173 est déjà pris (un autre essai, un autre serveur), l'essai refuse de
   démarrer. Écrit aussi une page en fichier local (…\\fichier-local\\index.html).
3. ouvre le navigateur avec trois onglets : la page locale (Annoter allumé), le fichier local (allumé) et
   https://example.com/ (site en ligne : Annoter éteint, grisé, avec son info-bulle). --private ouvre une fenêtre privée
   sur la page locale (même règle). --welcome commence par la fenêtre d'accueil d'une nouvelle installation (« --onboarding ») :
   son étape « Mode Annotation » propose « Essayer en 1 minute », qui ouvre le tutoriel une fois le navigateur ouvert. La connexion des agents est redirigée vers un dossier de test
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
import time

HERE = pathlib.Path(__file__).resolve().parent
URL = 'http://127.0.0.1:5173/'
ONLINE = 'https://example.com/'   # a harmless public page (IANA's example domain): the « site en ligne » case
ONLINE_HTTP = 'http://example.com/'   # the same page over http: « Non sécurisé » (the --local-pill trial's contrast)
LOCALHOST = 'http://localhost:5173/'   # the demo page by name: « Local » too
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


# « Ton agent » (design/ton-agent-interface.md): a FAKE agents profile for the browser — the folders it looks in for
# claude.exe / codex.exe (USERPROFILE, LOCALAPPDATA, APPDATA, PATH) point at temporary ones, the official installer is
# replaced by sandbox/agents/fake-install.ps1 and the user PATH by a test key: nothing of the real Windows account is used.
#   present  Claude Code installed and signed in, Codex installed and signed out
#   absent   neither installed (the installs run the fake installer)
#   desktop  Claude Desktop only (its folders, no claude.exe): Claude Code « not installed »
#   real     the REAL detection and the REAL official installers (only with Val's consent: the click « Installer … »)
REPO = HERE.parents[1]
FAKE_PATH_KEY = r'Software\BrowserTests\TonAgentTrial'


def fake_cli() -> pathlib.Path:
    exe = REPO / 'build' / 'acp-test' / 'fake-cli.exe'
    if not exe.is_file():   # the tests' fake CLI (tests/acp/fake_cli.cpp): --version, auth/login status, auth login / login
        exe.parent.mkdir(parents=True, exist_ok=True)
        gxx = REPO / 'tools' / 'w64devkit' / 'bin' / 'g++.exe'
        subprocess.run([str(gxx), '-std=c++17', '-O2', '-static', '-municode', str(REPO / 'tests' / 'acp' / 'fake_cli.cpp'), '-o', str(exe)], check=True)
    return exe


def put_cli(folder: pathlib.Path, name: str, version: str, login: str, cli: pathlib.Path) -> None:
    folder.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(cli, folder / name)
    (folder / (name + '.version')).write_text(version, encoding='ascii')
    (folder / (name + '.login')).write_text(login, encoding='ascii')


def agents_environment(root: pathlib.Path, profile: str, fresh: bool, mode: str, delay_ms: int, app: pathlib.Path) -> dict[str, str]:
    if profile == 'real':
        return {}
    home, local, roaming = root / 'home', root / 'local', root / 'roaming'
    if fresh and root.exists():
        shutil.rmtree(root)
    for d in (home, local, roaming):
        d.mkdir(parents=True, exist_ok=True)
    cli = fake_cli()
    if fresh:
        if profile == 'present':
            put_cli(home / '.local' / 'bin', 'claude.exe', '2.1.325 (Claude Code)', '0', cli)
            put_cli(local / 'Programs' / 'OpenAI' / 'Codex' / 'bin', 'codex.exe', 'codex-cli 0.166.0', '1', cli)
        elif profile == 'desktop':
            (local / 'AnthropicClaude').mkdir(parents=True, exist_ok=True)
            (roaming / 'Claude').mkdir(parents=True, exist_ok=True)
        import winreg
        try:
            winreg.DeleteKey(winreg.HKEY_CURRENT_USER, FAKE_PATH_KEY)
        except OSError:
            pass
    system = os.environ.get('SystemRoot', r'C:\Windows')
    return {
        'USERPROFILE': str(home), 'LOCALAPPDATA': str(local), 'APPDATA': str(roaming),
        'PATH': ';'.join([system + r'\System32', system, system + r'\System32\WindowsPowerShell\v1.0']),
        'BROWSER_AGENT_INSTALL_SCRIPT': str(REPO / 'sandbox' / 'agents' / 'fake-install.ps1'),
        'BROWSER_AGENT_PATH_KEY': FAKE_PATH_KEY,
        'BROWSER_FAKE_CLI': str(cli),
        'BROWSER_FAKE_INSTALL_MODE': mode,
        'BROWSER_FAKE_INSTALL_DELAY_MS': str(delay_ms),
        **fake_agent(app),
    }


def fake_agent(app: pathlib.Path) -> dict[str, str]:
    """An honest simulated profile: the chat's session runs the tests' fake ACP agent (it answers « (essai) Réponse simulée »)
    instead of the fake CLI, which cannot hold a session. Launch only (BROWSER_CHAT_FAKE_AGENT_LAUNCH_ONLY): the detection,
    the runtime download and the sign-in stay the browser's real code on the fake profile."""
    exe = app.parent / (app.name + '-test') / 'fake-acp-agent.exe'   # tools/release/build.py: <out>-test/fake-acp-agent.exe
    if not exe.is_file():
        print(f"Agent simulé introuvable ({exe}) : le chat de ce profil factice ne pourra pas répondre.")
        return {}
    return {'BROWSER_CHAT_FAKE_AGENT': f'"{exe}" --reply "(essai) Réponse simulée : avec un vrai agent, sa réponse apparaîtrait ici."',
            'BROWSER_CHAT_FAKE_AGENT_LAUNCH_ONLY': '1'}


def write_session(run: pathlib.Path, urls: list[str]) -> None:
    """The first window's tabs (src/session.h, format 2): the local page first and active."""
    lines = ['browser-session-2', '0', str(len(urls))] + [json.dumps(u) for u in urls] + ['0']
    (run / 'session').write_text('\n'.join(lines) + '\n', encoding='utf-8', newline='\n')


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--app', required=True, help='dossier du build (browser.exe, assets\\…)')
    ap.add_argument('--keep', action='store_true', help="reprendre l'essai précédent (sans remise à zéro)")
    ap.add_argument('--private', action='store_true', help="une fenêtre privée sur la page locale (reprend l'essai précédent s'il existe)")
    ap.add_argument('--welcome', action='store_true', help="comme une nouvelle installation : la fenêtre d'accueil d'abord (son étape « Mode Annotation » propose le tutoriel)")
    ap.add_argument('--english', action='store_true', help='interface en anglais')
    ap.add_argument('--agents', choices=('present', 'absent', 'desktop', 'real'),
                    help="« Ton agent » : un profil d'agents factice (present, absent, desktop) ou la détection et les installateurs RÉELS (real)")
    ap.add_argument('--install-mode', default='ok', choices=('ok', 'nocli', 'policy', 'inuse', 'network', 'fail', 'hang'),
                    help="--agents factice : ce que fait le faux installateur (ok par défaut ; les autres : les erreurs)")
    ap.add_argument('--install-delay-ms', type=int, default=4000, help="--agents factice : durée du faux installateur (pour voir l'animation)")
    ap.add_argument('--local-pill', action='store_true', help="essai de la pastille « Local » de l'adresse (#127) : 127.0.0.1, localhost, un fichier, un site en http et en https")
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
    if port_open('127.0.0.1', 5173):
        # Never reuse a server already there: it may serve the repository's own files, which the agent would then edit.
        print("Le port 5173 est déjà pris (un autre essai ou un autre serveur est ouvert) : fermez-le, puis relancez.\n"
              "L'essai ne démarre pas, pour que l'agent ne modifie jamais d'autres fichiers que la copie de la page.", file=sys.stderr)
        return 1
    home = pathlib.Path(tempfile.gettempdir()) / ('browser-essai-pastille-local' if a.local_pill else 'browser-essai-onboarding-annotation')
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
        # --local-pill: « Local » on 127.0.0.1 and localhost, « Fichier » on the file, « Non sécurisé » on http, the lock on https.
        write_session(run, [URL, LOCALHOST, file_url, ONLINE_HTTP, ONLINE] if a.local_pill else [URL, file_url, ONLINE])
    if a.english:
        (run / 'ui-language').write_text('browser-ui-language 1\nlanguage=en\n', encoding='utf-8', newline='\n')   # src/ui-language.h
    prepare_agents(agents)
    # The demo page: a server of its own, started from a copy of serve.py beside the trial with the served copy as its
    # working folder — its command line and folder name only the copy, so the browser picks the copy as the project's
    # folder (never this repository, where try-onboarding.py runs). No cross-origin iframe: it is not needed here.
    server_dir = home / 'serveur'
    server_dir.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(HERE / 'serve.py', server_dir / 'serve.py')
    server = subprocess.Popen([sys.executable, '-I', str(server_dir / 'serve.py'), '--site', str(site), '--no-cross'], cwd=str(site),
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    try:
        if not wait_port('127.0.0.1', 5173) or server.poll() is not None:
            print('La page de démonstration ne démarre pas (port 5173 pris entre-temps ?).', file=sys.stderr)
            return 1
        env = dict(os.environ)
        env['BROWSER_ANNOTATIONS_CONNECT_ROOT'] = str(agents)
        env['BROWSER_ANNOTATIONS_TEST'] = '1'   # garde-fou : aucune écriture dans les vrais dossiers, même si la redirection échouait
        env.pop('BROWSER_ANNOTATIONS_CONNECT_PATH', None)
        if a.agents:
            env.update(agents_environment(home / 'agents-profile', a.agents, fresh, a.install_mode, a.install_delay_ms, app))
            print(f'Agents        : profil « {a.agents} »' + (' — DÉTECTION ET INSTALLATEURS RÉELS (votre compte Windows)' if a.agents == 'real'
                  else f' (factice : dossiers sous {home / "agents-profile"}, faux installateur « {a.install_mode} »)'))
        print(f'Navigateur     : {run}\\browser.exe' + (' (fenêtre privée)' if a.private else ''))
        print(f'Page locale    : {URL}   (Annoter allumé)')
        print(f'Fichier local  : {file_url}   (Annoter allumé)')
        print(f'Site en ligne  : {ONLINE}   (Annoter éteint)')
        if a.local_pill:
            print(f'Pastille « Local » : {URL} et {LOCALHOST} ; « Fichier » : le fichier ; « Non sécurisé » : {ONLINE_HTTP} ; cadenas : {ONLINE}')
        command = [str(run / 'browser.exe')] + (['--private', URL] if a.private else ['--onboarding'] if a.welcome else [])
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
        if server.poll() is None:   # our own server, by its handle (never by its command line)
            server.terminate()
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()


if __name__ == '__main__':
    sys.exit(main())
