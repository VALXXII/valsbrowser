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
    exactly our manifest.
`--dry-run` performs only read-only queries and prints the plan.
"""
from __future__ import annotations
import argparse, hashlib, json, os, re, subprocess, sys, time, urllib.error, urllib.request
from pathlib import Path

VERSION_RE = re.compile(r'^v(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})$')
REPO_RE = re.compile(r'^[A-Za-z0-9._-]+/[A-Za-z0-9._-]+$')
MANIFEST = 'update-manifest-win-x64.txt'


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


def fetch(url: str) -> bytes:
    req = urllib.request.Request(url, headers={'User-Agent': 'browser-release-verifier'})
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read(4096)


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--repo', required=True, help='public releases repository owner/name')
    p.add_argument('--dist', required=True)
    p.add_argument('--source-commit', required=True)
    p.add_argument('--dry-run', action='store_true')
    a = p.parse_args()
    if not REPO_RE.match(a.repo):
        raise SystemExit('bad --repo')
    if not re.fullmatch(r'[0-9a-f]{40}', a.source_commit):
        raise SystemExit('bad --source-commit')
    dist = Path(a.dist)
    info = json.loads((dist / 'release-info.json').read_text(encoding='utf-8'))
    tag = f'v{info["version"]}'
    ours = parse_tag(tag)
    if not ours or info['commit'] != a.source_commit or info['channel'] != 'release':
        raise SystemExit('release-info does not match this workflow run')
    package, manifest = dist / info['package'], dist / MANIFEST
    if sha256(package) != info['packageSha256'] or package.stat().st_size != info['packageSize']:
        raise SystemExit('package changed since packaging')
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
        if not a.dry_run:
            gh('release', 'edit', tag, '--repo', a.repo, '--latest')
        print(f'{tag} already published with identical assets; latest confirmed.')
        return 0
    if a.dry_run:
        print(f'DRY RUN: would create draft {tag}, upload {package.name}, {MANIFEST}, SHA256SUMS.txt, then publish as latest.')
        return 0
    if existing and existing.get('draft'):
        gh('release', 'delete', tag, '--repo', a.repo, '--yes')  # leftover draft of a failed run, never public
    notes = (f'Automatic build {info["version"]} of VALXXII/browser main, commit {a.source_commit}.\n\n'
             f'Package SHA-256: {info["packageSha256"]}\n')
    gh('release', 'create', tag, '--repo', a.repo, '--draft', '--title', f'browser {info["version"]}',
       '--notes', notes, str(package), str(manifest), str(dist / 'SHA256SUMS.txt'))
    # Draft releases are not reachable by tag; find it in the list.
    draft = next(r for r in releases(a.repo) if r.get('tag_name') == tag and r.get('draft'))
    digests = {x['name']: x.get('digest') for x in draft.get('assets', [])}
    for f in (package, manifest):
        if digests.get(f.name) != 'sha256:' + sha256(f):
            raise SystemExit(f'Uploaded digest mismatch for {f.name}: {digests.get(f.name)}')
    if decide(ours, published_versions(releases(a.repo)), False) != 'publish':
        gh('release', 'delete', tag, '--repo', a.repo, '--yes')
        print('Superseded while uploading; draft removed, nothing published.')
        return 0
    gh('release', 'edit', tag, '--repo', a.repo, '--draft=false', '--latest')
    url = f'https://github.com/{a.repo}/releases/latest/download/{MANIFEST}'
    for attempt in range(10):
        try:
            if fetch(url) == manifest.read_bytes():
                print(f'Published {tag}; public latest manifest verified.')
                return 0
        except urllib.error.URLError as e:
            print('verify retry:', e)
        time.sleep(6)
    raise SystemExit('Published, but the public latest manifest does not match (check feed manually).')


if __name__ == '__main__':
    sys.exit(main())
