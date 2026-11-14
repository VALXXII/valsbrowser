#!/usr/bin/env python3
"""Petit client MCP stdio pour jouer l'agent sans Claude ni Codex (V2 : annotations groupées par site).

    python sandbox/annotations/try-mcp.py list [open|claimed|resolved|needs_info|dismissed|all] [--site http://127.0.0.1:5173]
    python sandbox/annotations/try-mcp.py get <id>                 # contexte Markdown + capture enregistrée (PNG)
    python sandbox/annotations/try-mcp.py claim <id>               # « Claude s'en occupe » côté navigateur (agent try-mcp)
    python sandbox/annotations/try-mcp.py resolve <id> <résumé…>  [--outcome resolved|needs_info|dismissed]
    python sandbox/annotations/try-mcp.py release <id> [raison]    # rend l'annotation (retour à « En attente »), raison lue par l'utilisateur
    python sandbox/annotations/try-mcp.py wait [--timeout 300]     # attend la prochaine annotation ouverte et l'affiche
    python sandbox/annotations/try-mcp.py demo [--work 8]          # wait + claim + (8 s de « travail ») + resolve
    python sandbox/annotations/try-mcp.py tools                    # outils exposés par le connecteur

Chaque commande lance build/annotations-preview/browser-annotations-mcp.exe avec --store
build/annotations-preview/annotations (le magasin du build preview) et, si --site est donné, --site <origine>
(le connecteur ne sert alors que ce site). Poignée de main MCP (initialize 2025-11-25), appels d'outils, puis
stdin fermé : le connecteur s'arrête. Les jetons de réservation sont gardés dans
build/annotations-sandbox/claims.json pour que resolve/release marchent d'une commande à l'autre.

« wait » : le connecteur n'a pas d'outil d'attente (le protocole MCP stdio est à la demande) ; try-mcp
interroge browser_annotations_list toutes les 1,5 s dans UNE session du connecteur, jusqu'à une annotation
ouverte qu'il n'avait pas encore vue. Rien n'est écrit ailleurs que dans le magasin du preview et
build/annotations-sandbox/.
"""
from __future__ import annotations
import argparse
import base64
import json
import pathlib
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
PREVIEW = ROOT / 'build' / 'annotations-preview'
BRIDGE = PREVIEW / 'browser-annotations-mcp.exe'
STORE = PREVIEW / 'annotations'
STATE = ROOT / 'build' / 'annotations-sandbox'
CLAIMS = STATE / 'claims.json'
AGENT = 'try-mcp'


class Bridge:
    def __init__(self, bridge: pathlib.Path, store: pathlib.Path, site: str | None, verbose: bool):
        if not bridge.is_file():
            sys.exit(f'Connecteur introuvable : {bridge}\nConstruisez d\'abord : python sandbox/annotations/run-sandbox.py --build')
        self.verbose = verbose
        cmd = [str(bridge), '--store', str(store)] + (['--site', site] if site else [])
        self.p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=None if verbose else subprocess.DEVNULL, cwd=str(ROOT))
        self.next = 0

    def send(self, obj):
        line = json.dumps(obj, ensure_ascii=False)
        if self.verbose:
            print('>>', line[:300], file=sys.stderr)
        self.p.stdin.write(line.encode('utf-8') + b'\n')
        self.p.stdin.flush()

    def request(self, method, params=None):
        self.next += 1
        self.send({'jsonrpc': '2.0', 'id': self.next, 'method': method, 'params': params or {}})
        while True:
            raw = self.p.stdout.readline()
            if not raw:
                sys.exit('Le connecteur s\'est arrêté sans répondre.')
            msg = json.loads(raw.decode('utf-8'))
            if self.verbose:
                print('<<', raw.decode('utf-8')[:300].rstrip(), file=sys.stderr)
            if msg.get('id') == self.next:
                if 'error' in msg:
                    sys.exit(f"Erreur JSON-RPC {msg['error'].get('code')}: {msg['error'].get('message')}")
                return msg['result']

    def start(self):
        r = self.request('initialize', {'protocolVersion': '2025-11-25', 'capabilities': {},
                                        'clientInfo': {'name': 'try-mcp', 'title': 'try-mcp (sandbox)', 'version': '2.0'}})
        self.send({'jsonrpc': '2.0', 'method': 'notifications/initialized'})
        return r

    def call(self, name, args):
        return self.request('tools/call', {'name': name, 'arguments': args})

    def close(self):
        try:
            self.p.stdin.close()
            self.p.wait(timeout=5)
        except Exception:
            self.p.kill()


def load_claims():
    try:
        return json.loads(CLAIMS.read_text(encoding='utf-8'))
    except (OSError, ValueError):
        return {}


def save_claims(c):
    STATE.mkdir(parents=True, exist_ok=True)
    CLAIMS.write_text(json.dumps(c, indent=2), encoding='utf-8')


def text_of(result):
    return '\n'.join(c.get('text', '') for c in result.get('content', []) if c.get('type') == 'text')


def items_of(b: Bridge, status='all', site=None):
    args = {'status': status, 'limit': 50}
    if site:
        args['site'] = site
    r = b.call('browser_annotations_list', args)
    if r.get('isError'):
        sys.exit(text_of(r))
    return (r.get('structuredContent') or {}).get('annotations', [])


def show(it):
    who = (it.get('claimedBy') or {}).get('agent')
    print(f"{it['id']}  [{it['status']}{' · ' + who if who else ''}]  {it.get('site', '')}  {it.get('selector', '')}")
    print(f"    « {it.get('note', '')} »  {it.get('url', '')}")


def get(b: Bridge, ident: str) -> int:
    r = b.call('browser_annotation_get', {'id': ident, 'include_screenshot': True})
    print(text_of(r))
    for c in r.get('content', []):
        if c.get('type') == 'image':
            out = STATE / f'{ident}.png'
            STATE.mkdir(parents=True, exist_ok=True)
            out.write_bytes(base64.b64decode(c['data']))
            print(f'\nCapture enregistrée : {out} ({out.stat().st_size} octets, {c.get("mimeType")})')
    return 1 if r.get('isError') else 0


def claim(b: Bridge, ident: str, claims: dict) -> int:
    args = {'id': ident, 'agent_label': AGENT}
    if ident in claims:
        args['claim_token'] = claims[ident]
    r = b.call('browser_annotation_claim', args)
    print(text_of(r))
    sc = r.get('structuredContent') or {}
    if not r.get('isError') and sc.get('claim_token'):
        claims[ident] = sc['claim_token']
        save_claims(claims)
        print(f"Jeton gardé dans {CLAIMS} (expire {sc.get('expiresAt')})")
    return 1 if r.get('isError') else 0


def finish(b: Bridge, command: str, ident: str, claims: dict, summary: str, outcome: str) -> int:
    token = claims.get(ident)
    if not token:
        sys.exit(f'Aucun jeton pour {ident} : lancez d\'abord « claim {ident} ».')
    if command == 'resolve':
        r = b.call('browser_annotation_resolve', {'id': ident, 'claim_token': token, 'outcome': outcome, 'summary': summary or 'Corrigé.'})
    else:
        args = {'id': ident, 'claim_token': token}
        if summary:
            args['reason'] = summary   # « release <id> <raison> » : montrée à l'utilisateur dans l'historique
        r = b.call('browser_annotation_release', args)
    print(text_of(r))
    if not r.get('isError'):
        claims.pop(ident, None)
        save_claims(claims)
    return 1 if r.get('isError') else 0


def wait(b: Bridge, site, timeout: float) -> str | None:
    seen = {it['id'] for it in items_of(b, 'open', site)}
    if seen:
        print(f'{len(seen)} annotation(s) déjà ouverte(s) ignorée(s) ; en attente d\'une nouvelle…', flush=True)
    else:
        print('En attente d\'une annotation (Ajouter dans le navigateur)…', flush=True)
    end = time.time() + timeout
    while time.time() < end:
        time.sleep(1.5)
        for it in items_of(b, 'open', site):
            if it['id'] not in seen:
                print('Nouvelle annotation :')
                show(it)
                return it['id']
    print('Rien de nouveau avant la fin du délai.')
    return None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('command', choices=['list', 'get', 'claim', 'resolve', 'release', 'wait', 'demo', 'tools'])
    ap.add_argument('args', nargs='*')
    ap.add_argument('--outcome', default='resolved', choices=['resolved', 'needs_info', 'dismissed'])
    ap.add_argument('--site', help='origine (ou URL) : le connecteur ne sert que ce site (--site du connecteur)')
    ap.add_argument('--project', help=argparse.SUPPRESS)   # V1 : remplacé par --site
    ap.add_argument('--timeout', type=float, default=300, help='wait/demo : délai max en secondes (300)')
    ap.add_argument('--work', type=float, default=8, help='demo : secondes de « travail » entre claim et resolve (8)')
    ap.add_argument('--bridge', default=str(BRIDGE))
    ap.add_argument('--store', default=str(STORE))
    ap.add_argument('-v', '--verbose', action='store_true', help='affiche les trames JSON-RPC et le journal du connecteur')
    a = ap.parse_args()
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding='utf-8')
        except Exception:
            pass
    if a.project and not a.site:
        print('--project n\'existe plus (V2 : annotations groupées par site) : utilisez --site <origine>.', file=sys.stderr)
    b = Bridge(pathlib.Path(a.bridge), pathlib.Path(a.store), a.site, a.verbose)
    try:
        info = b.start()
        if a.verbose:
            print('serveur :', info.get('serverInfo'), 'protocole :', info.get('protocolVersion'), file=sys.stderr)
        claims = load_claims()
        if a.command == 'tools':
            for t in b.request('tools/list')['tools']:
                print(t['name'], '-', t.get('description', '')[:90])
            return 0
        if a.command == 'list':
            status = a.args[0] if a.args else 'all'
            items = items_of(b, status, a.site)
            if not items:
                print('Aucune annotation (' + status + (', ' + a.site if a.site else '') + ').')
            for it in items:
                show(it)
            return 0
        if a.command in ('wait', 'demo'):
            ident = wait(b, a.site, a.timeout)
            if not ident:
                return 1
            if a.command == 'wait':
                return 0
            print()
            get(b, ident)
            print()
            if claim(b, ident, claims):
                return 1
            print(f'\n… « {AGENT} s\'en occupe » dans le navigateur ; résolution dans {a.work:g} s.', flush=True)
            time.sleep(max(0.0, a.work))
            summary = ' '.join(a.args).strip() or 'Fait par try-mcp (sandbox) : rien n\'a vraiment changé dans la page.'
            return finish(b, 'resolve', ident, claims, summary, a.outcome)
        if not a.args:
            ap.error('identifiant manquant (ann_…)')
        ident = a.args[0]
        if a.command == 'get':
            return get(b, ident)
        if a.command == 'claim':
            return claim(b, ident, claims)
        return finish(b, a.command, ident, claims, ' '.join(a.args[1:]).strip(), a.outcome)
    finally:
        b.close()


if __name__ == '__main__':
    sys.exit(main())
