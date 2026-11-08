"""Pinned, unmodified uBlock Origin Lite (MV3) for the built-in ad blocker.

Downloads the official Chromium build from https://github.com/uBlockOrigin/uBOL-home
releases, checks its SHA-256 (the digest GitHub publishes for the release asset) and
unpacks it, untouched, into the ignored assets/extensions/ublock-lite/. build.py runs
this when that folder is missing or holds another version, then ships the folder with
the other assets. The archive is cached in the ignored deps/ folder.

uBOL is GPLv3: it ships as a separate, unmodified component next to the browser, with
its LICENSE.txt (inside the folder and as uBOLite-LICENSE.txt) and
third_party/ublock-lite/NOTICE.txt (version and source link). To update: change
VERSION and SHA256 together, and the NOTICE.
"""
from pathlib import Path
import hashlib, json, shutil, sys, urllib.request, zipfile

ROOT = Path(__file__).resolve().parent
VERSION = '2026.1103.1743'
SHA256 = '79236476535b63706749e088e0ce99bb31d3d2ff15fabb4d7e990be23d6e4613'
URL = f'https://github.com/uBlockOrigin/uBOL-home/releases/download/{VERSION}/uBOLite_{VERSION}.chromium.zip'
archive = ROOT / f'deps/uBOLite_{VERSION}.chromium.zip'
target = ROOT / 'assets/extensions/ublock-lite'


def installed_version() -> str:
    try:
        return json.loads((target / 'manifest.json').read_text(encoding='utf-8')).get('version', '')
    except (OSError, ValueError):
        return ''


def main() -> int:
    if installed_version() == VERSION:
        print(f'uBlock Origin Lite {VERSION} already in {target}')
        return 0
    archive.parent.mkdir(parents=True, exist_ok=True)
    if not archive.exists() or hashlib.sha256(archive.read_bytes()).hexdigest() != SHA256:
        partial = archive.with_suffix('.partial')
        urllib.request.urlretrieve(URL, partial)
        partial.replace(archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != SHA256:
        raise SystemExit('uBlock Origin Lite archive checksum mismatch')
    staging = target.with_name(target.name + '.staging')
    if staging.exists():
        shutil.rmtree(staging)
    with zipfile.ZipFile(archive) as z:
        for name in z.namelist():  # extractall sanitises too; refuse anything odd outright
            if name.startswith(('/', '\\')) or '..' in Path(name).parts or ':' in name:
                raise SystemExit(f'Unsafe path in uBlock Origin Lite archive: {name}')
        z.extractall(staging)
    if json.loads((staging / 'manifest.json').read_text(encoding='utf-8')).get('version') != VERSION:
        raise SystemExit('uBlock Origin Lite manifest version mismatch')
    if target.exists():
        shutil.rmtree(target)
    staging.replace(target)
    print(f'uBlock Origin Lite {VERSION} unpacked into {target}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
