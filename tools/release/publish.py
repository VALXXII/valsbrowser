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
Public installer: browser-<version>-win-x64-setup.exe (package.py: setup stub + the SAME .lsrupd
byte for byte) is REQUIRED exactly like the ZIP: local size/SHA-256 vs release-info and SHA256SUMS,
server digest on the draft and on an existing release, anonymous versioned download hash after
publishing. The release notes lead with it; the ZIP stays as the portable alternative.
Signed releases (design/installer-signing-options.md): `--require-signer <name>` refuses to publish anything that
package.py did not sign and check with that signer (release-info.json), a test signature, or files without a
certificate table (the setup, the uninstaller it carries, every .exe of the ZIP). This job cannot run Windows' own
signature check (it runs on Linux): the cryptographic check is package.py's, on Windows, before these files exist.
Without the option nothing changes: unsigned releases are published as before.
`--dry-run` performs only read-only queries and prints the plan.
"""
from __future__ import annotations
import argparse, hashlib, json, os, re, subprocess, sys, time, urllib.error, urllib.request, zipfile
from pathlib import Path

import package as K   # the setup's layout (same directory; standard library only)

VERSION_RE = re.compile(r'^v(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})$')
REPO_RE = re.compile(r'^[A-Za-z0-9._-]+/[A-Za-z0-9._-]+$')
MANIFEST = 'update-manifest-win-x64.txt'
SUMS = 'SHA256SUMS.txt'
MAX_ZIP = 512 * 1024 * 1024
MAX_SETUP = 600 * 1024 * 1024  # stub + package (UpdatePackage::MaxPackageBytes) + trailer


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
    return subprocess.run(['gh', *args], check=check, capture_output=capture, text=True, encoding='utf-8')


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


def parse_manifest(raw: bytes) -> dict:
    """Same strict format as package.read_manifest (6 LF lines, fixed key order)."""
    keys = ['browser-update', 'version', 'arch', 'package', 'size', 'sha256']
    if len(raw) > 1024 or b'\r' in raw or not raw.endswith(b'\n'):
        return {}
    lines = raw.decode('utf-8', 'replace')[:-1].split('\n')
    if len(lines) != len(keys):
        return {}
    out = {}
    for key, line in zip(keys, lines):
        k, _, v = line.partition(' ')
        if k != key or not v:
            return {}
        out[k] = v
    return out


def verify_dist(dist: Path, info: dict, source_commit: str) -> tuple:
    """Local, offline checks of the packaged assets. Returns (tag, ours, package, manifest, zip, setup)."""
    version = info.get('version', '')
    tag = f'v{version}'
    ours = parse_tag(tag)
    if not ours or info.get('commit') != source_commit or info.get('channel') != 'release':
        raise SystemExit('release-info does not match this workflow run')
    if info.get('package') != f'browser-{version}-win-x64.lsrupd':
        raise SystemExit('release-info package name does not match its version')
    if info.get('zip') != f'browser-{version}-win-x64.zip':
        raise SystemExit('release-info has no public ZIP for this version (re-run package.py)')
    if info.get('setup') != f'browser-{version}-win-x64-setup.exe':
        raise SystemExit('release-info has no public setup for this version (re-run package.py)')
    package, manifest, archive = dist / info['package'], dist / MANIFEST, dist / info['zip']
    setup = dist / info['setup']
    for f in (package, manifest, archive, setup, dist / SUMS):
        if not f.is_file() or f.is_symlink():
            raise SystemExit(f'missing release asset: {f.name}')
    if sha256(package) != info['packageSha256'] or package.stat().st_size != info['packageSize']:
        raise SystemExit('package changed since packaging')
    # The manifest is what every client reads: its fields must name exactly this version/package.
    want = {'browser-update': '1', 'version': version, 'arch': 'x64', 'package': package.name,
            'size': str(package.stat().st_size), 'sha256': sha256(package)}
    if parse_manifest(manifest.read_bytes()) != want:
        raise SystemExit(f'{MANIFEST} does not describe {package.name} for {tag}')
    if (archive.stat().st_size != info.get('zipSize') or archive.stat().st_size > MAX_ZIP
            or sha256(archive) != info.get('zipSha256')):
        raise SystemExit('ZIP changed since packaging')
    if (setup.stat().st_size != info.get('setupSize') or setup.stat().st_size > MAX_SETUP
            or sha256(setup) != info.get('setupSha256')):
        raise SystemExit('setup changed since packaging')
    # The setup must carry exactly this package (layout of src/setup-payload.h, with or without a signature; offline).
    data, pkg = setup.read_bytes(), package.read_bytes()
    lay = K.setup_layout(data)
    offset, size = lay['package']
    if (lay['version'] != version or size != len(pkg) or data[offset:offset + size] != pkg
            or lay['sha256'] != hashlib.sha256(pkg).digest()):
        raise SystemExit('setup does not embed exactly the package')
    # What release-info says about signing must be what the files are (structure; see --require-signer).
    u_offset, u_size = lay['uninstaller']
    exes = {}
    with zipfile.ZipFile(archive) as z:
        for name in z.namelist():
            if name.lower().endswith('.exe'):
                exes[name] = has_certificate_table(z.read(name))
    said = bool(info.get('signed'))
    if said != lay['signed'] or said != bool(u_size) or (exes and said != all(exes.values())) or (not said and any(exes.values())):
        raise SystemExit('release-info "signed" does not match the files (setup, embedded uninstaller, executables of the ZIP)')
    if said and (not K.pe_overlay(data[u_offset:u_offset + u_size])[2] or not info.get('signer')):
        raise SystemExit('signed release without a signed uninstaller or without a signer name')
    sums = (dist / SUMS).read_bytes().decode('ascii')
    expected = ''.join(f'{sha256(f)}  {f.name}\n' for f in (package, manifest, archive, setup))
    if sums != expected:
        raise SystemExit(f'{SUMS} does not cover exactly the package, manifest, ZIP and setup bytes')
    return tag, ours, package, manifest, archive, setup


def release_notes(info: dict, repo: str, source_commit: str) -> str:
    v, z, st = info['version'], info['zip'], info['setup']
    top = z[:-len('.zip')]
    base = f'https://github.com/{repo}/releases/download/v{v}'
    return (f'## Télécharger browser {v} (Windows 10/11 x64)\n\n'
            f'**[{st}]({base}/{st})** — installation en un seul fichier · '
            f'dernière version toujours ici : https://github.com/{repo}/releases/latest\n\n'
            f'1. Télécharger **{st}** et l\'ouvrir. Aucun droit administrateur : browser s\'installe pour votre '
            f'compte Windows dans `%LOCALAPPDATA%\\browser`, avec un raccourci dans le menu Démarrer '
            f'(raccourci Bureau en option).\n'
            f'2. Windows SmartScreen peut avertir (programme non signé) : *Informations complémentaires* > '
            f'*Exécuter quand même*.\n'
            f'3. Déjà installé ? Le même fichier met à jour ou répare ; vos profils et données sont conservés. '
            f'Désinstallation : Paramètres Windows > Applications (vos données sont gardées sauf demande).\n\n'
            f'Le moteur Microsoft Edge WebView2 (fourni avec Windows 11) est requis ; s\'il manque, le navigateur '
            f'propose de l\'installer depuis Microsoft au premier démarrage.\n\n'
            f'**Version portable** (sans installation) : [{z}]({base}/{z}) — clic droit > **Extraire tout…**, '
            f'puis lancer `{top}\\browser.exe` en gardant tout le dossier. Le profil est alors créé dans ce dossier.\n\n'
            f'Les fichiers `.lsrupd` et `update-manifest-win-x64.txt` servent uniquement aux mises à jour automatiques.\n\n'
            f'Automatic build {v} of VALXXII/browser main, commit {source_commit}.\n\n'
            f'Setup SHA-256: {info["setupSha256"]}\n'
            f'ZIP SHA-256: {info["zipSha256"]}\n'
            f'Package SHA-256: {info["packageSha256"]}\n')


def check_digests(release: dict, files: list, what: str) -> None:
    digests = {x.get('name'): x.get('digest') for x in release.get('assets', [])}
    for f in files:
        if digests.get(f.name) != 'sha256:' + sha256(f):
            raise SystemExit(f'{what}: digest mismatch or missing asset {f.name}: {digests.get(f.name)}')


def verify_public(repo: str, tag: str, manifest: Path, archive: Path, setup: Path, latest: bool = True) -> None:
    """Anonymous downloads must serve exactly our bytes: the latest manifest (when `latest`), the versioned ZIP and the
    versioned setup. Shared by a fresh publication and by verify-existing (a rerun after a failed check), so a
    recovery run can never turn green while a public asset is still wrong. Read-only (no asset is ever replaced)."""
    url = f'https://github.com/{repo}/releases/latest/download/{MANIFEST}'
    zip_url = f'https://github.com/{repo}/releases/download/{tag}/{archive.name}'
    setup_url = f'https://github.com/{repo}/releases/download/{tag}/{setup.name}'
    zip_sha, setup_sha = sha256(archive), sha256(setup)
    manifest_ok, zip_ok, setup_ok = not latest, False, False
    for attempt in range(10):
        try:
            manifest_ok = manifest_ok or fetch(url) == manifest.read_bytes()
            zip_ok = zip_ok or hashlib.sha256(fetch(zip_url, MAX_ZIP + 1)).hexdigest() == zip_sha
            setup_ok = setup_ok or hashlib.sha256(fetch(setup_url, MAX_SETUP + 1)).hexdigest() == setup_sha
            if manifest_ok and zip_ok and setup_ok:
                print(f'{tag}: public {"latest manifest, " if latest else ""}setup and ZIP verified.')
                print(f'Public download: {setup_url}')
                print(f'Portable ZIP: {zip_url}')
                return
        except urllib.error.URLError as e:
            print('verify retry:', e)
        time.sleep(6)
    missing = 'latest manifest' if not manifest_ok else 'ZIP' if not zip_ok else 'setup'
    raise SystemExit(f'Published, but the public {missing} does not match (check feed manually).')


def has_certificate_table(data: bytes) -> bool:
    """True when a PE file carries an Authenticode certificate table (structure only)."""
    try:
        return K.pe_overlay(data)[2]
    except SystemExit:
        return False


def require_signer(info: dict, name: str) -> None:
    """--require-signer: only a release that package.py signed and checked with this signer may be published."""
    if not name.strip():
        raise SystemExit('--require-signer needs the signer name')
    if not info.get('signed'):
        raise SystemExit('this release is not signed: refused (--require-signer)')
    if info.get('testSignature') is not False:
        raise SystemExit('this release carries a TEST signature: refused (--require-signer)')
    if name.strip().lower() not in str(info.get('signer', '')).lower():
        raise SystemExit('this release is not signed by the required signer: refused (--require-signer)')


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--repo', required=True, help='public releases repository owner/name')
    p.add_argument('--dist', required=True)
    p.add_argument('--source-commit', required=True)
    p.add_argument('--dry-run', action='store_true')
    p.add_argument('--require-signer', help='refuse a release that is not signed by this name (default: not required)')
    a = p.parse_args(argv)
    if not REPO_RE.match(a.repo):
        raise SystemExit('bad --repo')
    if not re.fullmatch(r'[0-9a-f]{40}', a.source_commit):
        raise SystemExit('bad --source-commit')
    dist = Path(a.dist)
    info = json.loads((dist / 'release-info.json').read_text(encoding='utf-8'))
    tag, ours, package, manifest, archive, setup = verify_dist(dist, info, a.source_commit)
    if a.require_signer is not None:
        require_signer(info, a.require_signer)
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
        check_digests(existing, [package, manifest, archive, setup, dist / SUMS], f'existing {tag}')
        if not a.dry_run:
            gh('release', 'edit', tag, '--repo', a.repo, '--latest')
        # A rerun after a failed public check lands here: success only once the anonymous downloads are right
        # (dry run: read-only, latest not edited so only the versioned setup/ZIP are checked).
        verify_public(a.repo, tag, manifest, archive, setup, latest=not a.dry_run)
        print(f'{tag} already published with identical assets; latest confirmed.')
        return 0
    if a.dry_run:
        print(f'DRY RUN: would create draft {tag}, upload {package.name}, {MANIFEST}, {archive.name}, {setup.name}, '
              f'{SUMS}, then publish as latest.')
        return 0
    if existing and existing.get('draft'):
        gh('release', 'delete', tag, '--repo', a.repo, '--yes')  # leftover draft of a failed run, never public
    notes = release_notes(info, a.repo, a.source_commit)
    gh('release', 'create', tag, '--repo', a.repo, '--draft', '--title', f'browser {info["version"]}',
       '--notes', notes, str(package), str(manifest), str(archive), str(setup), str(dist / SUMS))
    # Draft releases are not reachable by tag; find it in the list.
    draft = next((r for r in releases(a.repo) if r.get('tag_name') == tag and r.get('draft')), None)
    if draft is None:
        raise SystemExit(f'draft {tag} not found after upload')
    check_digests(draft, [package, manifest, archive, setup, dist / SUMS], 'uploaded draft')
    if decide(ours, published_versions(releases(a.repo)), False) != 'publish':
        gh('release', 'delete', tag, '--repo', a.repo, '--yes')
        print('Superseded while uploading; draft removed, nothing published.')
        return 0
    gh('release', 'edit', tag, '--repo', a.repo, '--draft=false', '--latest')
    verify_public(a.repo, tag, manifest, archive, setup)
    print(f'Published {tag}.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
