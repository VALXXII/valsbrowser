#!/usr/bin/env python3
"""Petit client MCP stdio pour essayer le connecteur sans Claude ni Codex.

    python sandbox/annotations/try-mcp.py list [open|claimed|resolved|needs_info|dismissed|all]
    python sandbox/annotations/try-mcp.py get <id>                 # enregistre aussi la capture (PNG)
    python sandbox/annotations/try-mcp.py claim <id>
    python sandbox/annotations/try-mcp.py resolve <id> <résumé…>  [--outcome resolved|needs_info|dismissed]
    python sandbox/annotations/try-mcp.py release <id>

Chaque commande lance build/annotations-preview/browser-annotations-mcp.exe avec
--store build/annotations-preview/annotations (le magasin du build preview), fait la poignée
de main MCP (initialize 2025-11-25), appelle un outil, puis ferme stdin (le connecteur s'arrête).
Les jetons de réservation sont gardés dans build/annotations-sandbox/claims.json pour que
resolve/release fonctionnent d'une commande à l'autre. Rien n'est écrit ailleurs.
"""
from __future__ import annotations
import argparse
import base64
import json
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
PREVIEW = ROOT / 'build' / 'annotations-preview'
BRIDGE = PREVIEW / 'browser-annotations-mcp.exe'
STORE = PREVIEW / 'annotations'
STATE = ROOT / 'build' / 'annotations-sandbox'
CLAIMS = STATE / 'claims.json'


class Bridge:
    def __init__(self, bridge: pathlib.Path, store: pathlib.Path, verbose: bool):
        if not bridge.is_file():
            sys.exit(f'Connecteur introuvable : {bridge}\nConstruisez d\'abord : python tools/release/build.py --out build/annotations-preview --channel dev --preview')
        self.verbose = verbose
        self.p = subprocess.Popen([str(bridge), '--store', str(store)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
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
                                        'clientInfo': {'name': 'try-mcp', 'title': 'try-mcp (sandbox)', 'version': '1.0'}})
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


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('command', choices=['list', 'get', 'claim', 'resolve', 'release', 'tools'])
    ap.add_argument('args', nargs='*')
    ap.add_argument('--outcome', default='resolved', choices=['resolved', 'needs_info', 'dismissed'])
    ap.add_argument('--project', help='filtrer list sur un projet (ex. boutique)')
    ap.add_argument('--bridge', default=str(BRIDGE))
    ap.add_argument('--store', default=str(STORE))
    ap.add_argument('-v', '--verbose', action='store_true', help='affiche les trames JSON-RPC et le journal du connecteur')
    a = ap.parse_args()
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding='utf-8')
        except Exception:
            pass
    b = Bridge(pathlib.Path(a.bridge), pathlib.Path(a.store), a.verbose)
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
            args = {'status': a.args[0] if a.args else 'all', 'limit': 50}
            if a.project:
                args['project'] = a.project
            r = b.call('browser_annotations_list', args)
            items = (r.get('structuredContent') or {}).get('annotations', [])
            if not items:
                print('Aucune annotation (' + args['status'] + ').')
            for it in items:
                who = (it.get('claimedBy') or {}).get('agent')
                print(f"{it['id']}  [{it['status']}{' · ' + who if who else ''}]  {it['project']}  {it.get('selector', '')}")
                print(f"    « {it.get('note', '')} »  {it.get('url', '')}")
            return 0
        if not a.args:
            ap.error('identifiant manquant (ann_…)')
        ident = a.args[0]
        if a.command == 'get':
            r = b.call('browser_annotation_get', {'id': ident, 'include_screenshot': True})
            print(text_of(r))
            for c in r.get('content', []):
                if c.get('type') == 'image':
                    out = STATE / f'{ident}.png'
                    STATE.mkdir(parents=True, exist_ok=True)
                    out.write_bytes(base64.b64decode(c['data']))
                    print(f'\nCapture enregistrée : {out} ({out.stat().st_size} octets, {c.get("mimeType")})')
            if r.get('isError'):
                return 1
            return 0
        if a.command == 'claim':
            args = {'id': ident, 'agent_label': 'try-mcp'}
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
        token = claims.get(ident)
        if not token:
            sys.exit(f'Aucun jeton pour {ident} : lancez d\'abord « claim {ident} ».')
        if a.command == 'resolve':
            summary = ' '.join(a.args[1:]).strip() or 'Corrigé.'
            r = b.call('browser_annotation_resolve', {'id': ident, 'claim_token': token, 'outcome': a.outcome, 'summary': summary})
        else:
            r = b.call('browser_annotation_release', {'id': ident, 'claim_token': token})
        print(text_of(r))
        if not r.get('isError'):
            claims.pop(ident, None)
            save_claims(claims)
        return 1 if r.get('isError') else 0
    finally:
        b.close()


if __name__ == '__main__':
    sys.exit(main())
