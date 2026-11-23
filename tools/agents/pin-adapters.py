#!/usr/bin/env python3
"""Pins what the integrated chat downloads at run time: writes src/agent-runtime-pins.inc.

    python -I tools/agents/pin-adapters.py            regenerate with the versions below
    python -I tools/agents/pin-adapters.py --check    verify the committed file against the registry (no write)

What is pinned (one set per browser version, compiled into browser.exe):
  - Node.js for Windows x64: the official zip of one LTS version, its SHA-256 and the SHA-256 of the node.exe inside
    (both from https://nodejs.org/dist/v<version>/SHASUMS256.txt), and their sizes.
  - For each adapter (claude-agent-acp, codex-acp): the package and every package it depends on, each with the exact
    version, the registry tarball it comes from and its sha512 integrity.

How the dependency tree is resolved: `npm install --package-lock-only --ignore-scripts --omit=optional` in a temporary
folder (needs npm on the developer machine; nothing is installed, the user's npmrc and npm cache are not used), then
the lockfile is filtered:
  - optional packages are dropped: they are the 370-500 MB platform builds of the CLIs, which the browser replaces by
    the user's own CLI (CLAUDE_CODE_EXECUTABLE / CODEX_PATH);
  - peer-only packages are dropped: for claude-agent-acp they are @anthropic-ai/sdk and @modelcontextprotocol/sdk with
    about a hundred transitive packages (express...), which neither the adapter nor the bundled Claude Agent SDK
    imports (checked by reading their imports; the real-adapter probe of tests/acp proves the tree starts);
  - a package with an install script, a non-registry source or without sha512 integrity makes this script fail.
At run time the browser downloads exactly these tarballs from registry.npmjs.org, checks each sha512 and unpacks them
itself: npm never runs on the user's machine and no package script can run.

To update: change the three versions below, run this script, run `python -I tests/acp/run_tests.py --real`, read
what the adapters announce (design/chat-acp.md section 9), commit the .inc with the browser version that ships it.
"""
from __future__ import annotations
import argparse, hashlib, json, pathlib, shutil, subprocess, sys, tempfile, urllib.request

NODE_VERSION = '24.22.3'          # LTS "Krypton"; claude-agent-acp needs Node >= 22
CLAUDE_ACP = ('@agentclientprotocol/claude-agent-acp', '0.107.3')
CODEX_ACP = ('@agentclientprotocol/codex-acp', '2.6.0')

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / 'src' / 'agent-runtime-pins.inc'
REGISTRY = 'https://registry.npmjs.org'
NODE_DIST = 'https://nodejs.org/dist'


def fetch(url: str) -> bytes:
    with urllib.request.urlopen(urllib.request.Request(url, headers={'User-Agent': 'pin-adapters'}), timeout=120) as r:
        return r.read()


def content_length(url: str) -> int:
    with urllib.request.urlopen(urllib.request.Request(url, method='HEAD', headers={'User-Agent': 'pin-adapters'}), timeout=120) as r:
        return int(r.headers['Content-Length'])


def node_pin() -> dict:
    sums = fetch(f'{NODE_DIST}/v{NODE_VERSION}/SHASUMS256.txt').decode('ascii')
    table = {line.split()[1]: line.split()[0] for line in sums.splitlines() if line.strip()}
    zip_name = f'node-v{NODE_VERSION}-win-x64.zip'
    return {
        'version': NODE_VERSION, 'zip': zip_name, 'zipSha256': table[zip_name],
        'zipBytes': content_length(f'{NODE_DIST}/v{NODE_VERSION}/{zip_name}'),
        'exeSha256': table['win-x64/node.exe'],
        'exeBytes': content_length(f'{NODE_DIST}/v{NODE_VERSION}/win-x64/node.exe'),
    }


def resolve(package: str, version: str) -> list[dict]:
    work = pathlib.Path(tempfile.mkdtemp(prefix='pin-adapters-'))
    try:
        (work / 'package.json').write_text(json.dumps({'name': 'pins', 'version': '0.0.0', 'private': True,
                                                        'dependencies': {package: version}}), encoding='utf-8')
        (work / 'npmrc').write_text('', encoding='utf-8')
        npm = shutil.which('npm')
        if not npm:
            raise SystemExit('npm not found: it is needed on the developer machine to resolve the dependency tree')
        subprocess.run([npm, 'install', '--package-lock-only', '--ignore-scripts', '--omit=optional', '--no-audit', '--no-fund',
                        '--cache', str(work / 'cache'), '--userconfig', str(work / 'npmrc'), '--registry', REGISTRY + '/'],
                       cwd=work, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        lock = json.loads((work / 'package-lock.json').read_text(encoding='utf-8'))
    finally:
        shutil.rmtree(work, ignore_errors=True)
    pins = []
    for path, entry in sorted(lock['packages'].items()):
        if not path or entry.get('optional') or entry.get('peer') or entry.get('dev') or entry.get('devOptional'):
            continue
        name = path.rsplit('node_modules/', 1)[1]
        problems = []
        if entry.get('hasInstallScript'):
            problems.append('has an install script')
        if entry.get('link') or entry.get('inBundle'):
            problems.append('is a link or bundled')
        if not str(entry.get('resolved', '')).startswith(REGISTRY + '/'):
            problems.append(f'comes from {entry.get("resolved")!r}')
        if not str(entry.get('integrity', '')).startswith('sha512-'):
            problems.append('has no sha512 integrity')
        expected = f'{REGISTRY}/{name}/-/{name.split("/")[-1]}-{entry["version"]}.tgz'
        if entry.get('resolved') != expected:
            problems.append(f'tarball URL {entry.get("resolved")!r} is not the canonical {expected!r}')
        if any(c in path for c in '\\"') or '..' in path.split('/'):
            problems.append('unsafe path')
        if problems:
            raise SystemExit(f'{path}@{entry["version"]}: ' + '; '.join(problems))
        pins.append({'path': path, 'name': name, 'version': entry['version'], 'integrity': entry['integrity']})
    if not any(p['name'] == package and p['version'] == version for p in pins):
        raise SystemExit(f'{package}@{version} missing from its own tree')
    return pins


def manifest(package: str, version: str) -> dict:
    return json.loads(fetch(f'{REGISTRY}/{package}/{version}'))


def adapter_pin(package: str, version: str, cli_line: str, min_cli: str) -> dict:
    pins = resolve(package, version)
    entry = manifest(package, version)['bin']
    script = next(iter(entry.values())) if isinstance(entry, dict) else entry
    digest = hashlib.sha256(''.join(f'{p["path"]}@{p["version"]} {p["integrity"]}\n' for p in pins).encode()).hexdigest()[:8]
    return {'package': package, 'version': version, 'entry': f'node_modules/{package}/{script}', 'cliLine': cli_line,
            'minCli': min_cli, 'packages': pins, 'digest': digest}


def cstr(text: str) -> str:
    if not text.isascii() or any(c in text for c in '"\\\r\n'):
        raise SystemExit(f'unsafe string for the pin table: {text!r}')
    return f'"{text}"'


def render(node: dict, claude: dict, codex: dict) -> str:
    out = ['// Generated by tools/agents/pin-adapters.py. Do not edit: change the versions there and run it again.',
           '// Included only by src/agent-runtime.cpp. Hashes: nodejs.org SHASUMS256.txt and the npm registry lock data.', '']
    for key, adapter in (('Claude', claude), ('Codex', codex)):
        out.append(f'static const PackagePin k{key}Packages[] = {{')
        for p in adapter['packages']:
            out.append(f'    {{{cstr(p["path"])}, {cstr(p["name"])}, {cstr(p["version"])},')
            out.append(f'     {cstr(p["integrity"])}}},')
        out.append('};')
    out.append('static const Pins kBuiltInPins = {')
    out.append(f'    {{{cstr(node["version"])}, {cstr(node["zip"])},')
    out.append(f'     {cstr(node["zipSha256"])}, {node["zipBytes"]}ull,')
    out.append(f'     {cstr(node["exeSha256"])}, {node["exeBytes"]}ull}},')
    for key, adapter in (('Claude', claude), ('Codex', codex)):
        out.append(f'    {{{cstr(adapter["package"])}, {cstr(adapter["version"])}, {cstr(adapter["entry"])},')
        out.append(f'     {cstr(adapter["cliLine"])}, {cstr(adapter["minCli"])}, k{key}Packages, sizeof k{key}Packages / sizeof k{key}Packages[0],')
        out.append(f'     {cstr(adapter["digest"])}}},')
    out.append('};')
    return '\n'.join(out) + '\n'


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--check', action='store_true', help='compare the committed file with a fresh resolution; write nothing')
    args = parser.parse_args()
    # Claude: the CLI must be on the release line the Claude Agent SDK was built against (same major.minor, patch at
    # least the SDK's). Codex: any CLI at or above the range's minimum.
    sdk_version = manifest(*CLAUDE_ACP)['dependencies']['@anthropic-ai/claude-agent-sdk']
    claude_cli = manifest('@anthropic-ai/claude-agent-sdk', sdk_version)['claudeCodeVersion']
    codex_range = manifest(*CODEX_ACP)['dependencies']['@openai/codex']
    codex_min = codex_range.lstrip('^~')
    text = render(node_pin(), adapter_pin(*CLAUDE_ACP, claude_cli, claude_cli), adapter_pin(*CODEX_ACP, codex_min, codex_min))
    if args.check:
        current = OUT.read_text(encoding='ascii') if OUT.exists() else ''
        if current.replace('\r\n', '\n') != text:
            print(f'{OUT.relative_to(ROOT)} differs from a fresh resolution')
            return 1
        print(f'{OUT.relative_to(ROOT)} matches the registry')
        return 0
    OUT.write_text(text, encoding='ascii', newline='\n')
    print(f'wrote {OUT.relative_to(ROOT)}: Node {NODE_VERSION}, {CLAUDE_ACP[1]} (Claude CLI {claude_cli}), {CODEX_ACP[1]} (Codex CLI >= {codex_min})')
    return 0


if __name__ == '__main__':
    sys.exit(main())
