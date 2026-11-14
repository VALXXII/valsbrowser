"""Publish verified release assets to the PUBLIC binary feed repository.

Run only by the CI `publish` job after build, tests and packaging passed. Uses the
GitHub CLI with GH_TOKEN = secrets.UPDATES_RELEASE_TOKEN (a fine-grained token with
contents:write on the releases repository ONLY). No credential is stored anywhere.

Guards (the feed must never move backwards, also for fresh installs that read
`releases/latest`):
  * the newest published version in the feed is computed from ALL non-draft
    releases; if it is newer than ours, nothing is published (superseded build);
  * an existing release with our tag is immutable: it is accepted only if its
    manifest asset is byte-identical, otherwise the job fails;
  * assets are uploaded to a DRAFT, their server-side SHA-256 digests are checked,
    the newest version is re-read right before the draft is published as latest;
  * finally the anonymous public URL releases/latest/download/<manifest> must serve
    exactly our manifest, and the versioned public ZIP URL exactly our ZIP.
Public first-run download: browser-<version>-win-x64.zip (built by package.py from the
same staged payload as the .lsrupd) is a REQUIRED asset. It must match release-info.json
and SHA256SUMS.txt before anything is uploaded, its server digest is checked on the
draft and on an already existing release; a release without it is never accepted.
`--dry-run` performs only read-only queries and prints the plan.
"""
from __future__ import annotations
import argparse, hashlib, json, os, re, subprocess, sys, time, urllib.error, urllib.request
from pathlib import Path

VERSION_RE = re.compile(r'^v(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})$')
REPO_RE = re.compile(r'^[A-Za-z0-9._-]+/[A-Za-z0-9._-]+$')
MANIFEST = 'update-manifest-win-x64.txt'
SUMS = 'SHA256SUMS.txt'
MAX_ZIP = 512 * 1024 * 1024


def parse_tag(tag: str):
    m = VERSION_RE.match(tag or '')
    return tuple(int(x) for x in m.groups()) if m else None


def decide(ours: tuple, published: list[tuple], ours_exists: bool) -> str:
    """Pure decision: 'superseded' | 'verify-existing' | 'publish'."""
    newest = max(published, default=None)
    if newest is not None and newest > ours:
        return 'superseded'
    if ours_exists:
        return 'verify-existing'
    return 'publish'


def gh(*args: str, check=True, capture=True) -> subprocess.CompletedProcess:
    return subprocess.run(['gh', *args], check=check, capture_output=capture, text=True)


def releases(repo: str) -> list[dict]:
    out = gh('api', '--paginate', f'repos/{repo}/releases?per_page=100').stdout
    # --paginate concatenates JSON arrays: "[...][...]"
    items, decoder, pos = [], json.JSONDecoder(), 0
    out = out.strip()
    while pos < len(out):
        chunk, pos = decoder.raw_decode(out, pos)
        items.extend(chunk)
        while pos < len(out) and out[pos].isspace():
            pos += 1
    return items


def published_versions(items: list[dict]) -> list[tuple]:
    return [v for r in items if not r.get('draft') and (v := parse_tag(r.get('tag_name')))]


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fetch(url: str, limit: int = 4096) -> bytes:
    req = urllib.request.Request(url, headers={'User-Agent': 'browser-release-verifier'})
    with urllib.request.urlopen(req, timeout=120) as r:
        return r.read(limit)


def verify_dist(dist: Path, info: dict, source_commit: str) -> tuple:
    """Local, offline checks of the packaged assets. Returns (tag, ours, package, manifest, zip)."""
    version = info.get('version', '')
    tag = f'v{version}'
    ours = parse_tag(tag)
    if not ours or info.get('commit') != source_commit or info.get('channel') != 'release':
        raise SystemExit('release-info does not match this workflow run')
    if info.get('package') != f'browser-{version}-win-x64.lsrupd':
        raise SystemExit('release-info package name does not match its version')
    if info.get('zip') != f'browser-{version}-win-x64.zip':
        raise SystemExit('release-info has no public ZIP for this version (re-run package.py)')
    package, manifest, archive = dist / info['package'], dist / MANIFEST, dist / info['zip']
    for f in (package, manifest, archive, dist / SUMS):
        if not f.is_file() or f.is_symlink():
            raise SystemExit(f'missing release asset: {f.name}')
    if sha256(package) != info['packageSha256'] or package.stat().st_size != info['packageSize']:
        raise SystemExit('package changed since packaging')
    if (archive.stat().st_size != info.get('zipSize') or archive.stat().st_size > MAX_ZIP
            or sha256(archive) != info.get('zipSha256')):
        raise SystemExit('ZIP changed since packaging')
    sums = (dist / SUMS).read_bytes().decode('ascii')
    expected = ''.join(f'{sha256(f)}  {f.name}\n' for f in (package, manifest, archive))
    if sums != expected:
        raise SystemExit(f'{SUMS} does not cover exactly the package, manifest and ZIP bytes')
    return tag, ours, package, manifest, archive


def release_notes(info: dict, repo: str, source_commit: str) -> str:
    v, z = info['version'], info['zip']
    top = z[:-len('.zip')]
    return (f'## Télécharger browser {v} (Windows 10/11 x64)\n\n'
            f'**[{z}](https://github.com/{repo}/releases/download/v{v}/{z})** · '
            f'dernière version toujours ici : https://github.com/{repo}/releases/latest\n\n'
            f'1. Télécharger le ZIP, puis clic droit > **Extraire tout…** (ne pas lancer depuis le ZIP).\n'
            f'2. Ouvrir le dossier `{top}` et lancer **browser.exe**. Garder tout le dossier '
            f'(`assets`, `WebView2Loader.dll`, `update-helper.exe`) à côté du programme.\n'
            f'3. Windows SmartScreen peut avertir (programme non signé) : *Informations complémentaires* > '
            f'*Exécuter quand même*.\n\n'
            f'Premier lancement : version portable, le profil est créé dans ce dossier. Le navigateur vérifie '
            f'ensuite les mises à jour ; installer une mise à jour place l\'application dans '
            f'`%LOCALAPPDATA%\\browser` et y copie une fois le profil portable. Le moteur Microsoft Edge '
            f'WebView2 (fourni avec Windows 11) est requis ; s\'il manque, le navigateur propose de l\'installer.\n\n'
            f'Les fichiers `.lsrupd` et `update-manifest-win-x64.txt` servent uniquement aux mises à jour automatiques.\n\n'
            f'Automatic build {v} of VALXXII/browser main, commit {source_commit}.\n\n'
            f'ZIP SHA-256: {info["zipSha256"]}\n'
            f'Package SHA-256: {info["packageSha256"]}\n')


def check_digests(release: dict, files: list, what: str) -> None:
    digests = {x.get('name'): x.get('digest') for x in release.get('assets', [])}
    for f in files:
        if digests.get(f.name) != 'sha256:' + sha256(f):
            raise SystemExit(f'{what}: digest mismatch or missing asset {f.name}: {digests.get(f.name)}')


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--repo', required=True, help='public releases repository owner/name')
    p.add_argument('--dist', required=True)
    p.add_argument('--source-commit', required=True)
    p.add_argument('--dry-run', action='store_true')
    a = p.parse_args(argv)
    if not REPO_RE.match(a.repo):
        raise SystemExit('bad --repo')
    if not re.fullmatch(r'[0-9a-f]{40}', a.source_commit):
        raise SystemExit('bad --source-commit')
    dist = Path(a.dist)
    info = json.loads((dist / 'release-info.json').read_text(encoding='utf-8'))
    tag, ours, package, manifest, archive = verify_dist(dist, info, a.source_commit)
    if not a.dry_run and not os.environ.get('GH_TOKEN'):
        raise SystemExit('GH_TOKEN (UPDATES_RELEASE_TOKEN) is required to publish')

    items = releases(a.repo)
    existing = next((r for r in items if r.get('tag_name') == tag), None)
    action = decide(ours, published_versions(items), bool(existing and not existing.get('draft')))
    print(f'{a.repo}: ours={tag} newest={max(published_versions(items), default=None)} -> {action}')
    if action == 'superseded':
        print('A newer version is already published; this build is not published.')
        return 0
    if action == 'verify-existing':
        url = f'https://github.com/{a.repo}/releases/download/{tag}/{MANIFEST}'
        if fetch(url) != manifest.read_bytes():
            raise SystemExit(f'{tag} already exists with a DIFFERENT manifest; releases are immutable.')
        # Same version, same bytes: the public ZIP must already be there, identical (never added later).
        check_digests(existing, [package, manifest, archive], f'existing {tag}')
        if not a.dry_run:
            gh('release', 'edit', tag, '--repo', a.repo, '--latest')
        print(f'{tag} already published with identical assets; latest confirmed.')
        return 0
    if a.dry_run:
        print(f'DRY RUN: would create draft {tag}, upload {package.name}, {MANIFEST}, {archive.name}, {SUMS}, then publish as latest.')
        return 0
    if existing and existing.get('draft'):
        gh('release', 'delete', tag, '--repo', a.repo, '--yes')  # leftover draft of a failed run, never public
    notes = release_notes(info, a.repo, a.source_commit)
    gh('release', 'create', tag, '--repo', a.repo, '--draft', '--title', f'browser {info["version"]}',
       '--notes', notes, str(package), str(manifest), str(archive), str(dist / SUMS))
    # Draft releases are not reachable by tag; find it in the list.
    draft = next((r for r in releases(a.repo) if r.get('tag_name') == tag and r.get('draft')), None)
    if draft is None:
        raise SystemExit(f'draft {tag} not found after upload')
    check_digests(draft, [package, manifest, archive, dist / SUMS], 'uploaded draft')
    if decide(ours, published_versions(releases(a.repo)), False) != 'publish':
        gh('release', 'delete', tag, '--repo', a.repo, '--yes')
        print('Superseded while uploading; draft removed, nothing published.')
        return 0
    gh('release', 'edit', tag, '--repo', a.repo, '--draft=false', '--latest')
    url = f'https://github.com/{a.repo}/releases/latest/download/{MANIFEST}'
    zip_url = f'https://github.com/{a.repo}/releases/download/{tag}/{archive.name}'
    zip_sha = sha256(archive)
    manifest_ok = zip_ok = False
    for attempt in range(10):
        try:
            manifest_ok = manifest_ok or fetch(url) == manifest.read_bytes()
            zip_ok = zip_ok or hashlib.sha256(fetch(zip_url, MAX_ZIP + 1)).hexdigest() == zip_sha
            if manifest_ok and zip_ok:
                print(f'Published {tag}; public latest manifest and ZIP verified.')
                print(f'Public download: {zip_url}')
                return 0
        except urllib.error.URLError as e:
            print('verify retry:', e)
        time.sleep(6)
    raise SystemExit(f'Published, but the public {"ZIP" if manifest_ok else "latest manifest"} does not match '
                     '(check feed manually).')


if __name__ == '__main__':
    sys.exit(main())
