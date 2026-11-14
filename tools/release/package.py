"""Turn a built application directory into release assets.

1. Copies ONLY the allowlisted payload of <app> (browser.exe, update-helper.exe,
   WebView2Loader.dll, version.json, *-LICENSE.txt, *-NOTICE.txt, assets/**) into a
   fresh staging directory. Profiles, sessions, logs, downloads history, objects,
   tests or anything unexpected in <app> are refused (not silently skipped) unless
   they are known runtime data, which is reported and never copied.
2. Runs the update-core packer (tools/update-package/make_update_package.py), which
   applies the same allowlist again and writes
   browser-<version>-win-x64.lsrupd + update-manifest-win-x64.txt.
3. Re-reads the manifest and checks version/arch/package/size/sha256 against the
   bytes on disk.
4. Writes the public first-run download browser-<version>-win-x64.zip from the SAME
   staging tree (one top folder browser-<version>-win-x64/, sorted entries, fixed
   timestamps), then re-opens it and checks that its entries are exactly the staged
   files, byte for byte. Nothing else (no readme, no runtime data) is added.
5. Writes SHA256SUMS.txt (package, manifest, ZIP) and release-info.json.
"""
from __future__ import annotations
import argparse, hashlib, json, re, shutil, subprocess, sys, zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKER = ROOT / 'tools/update-package/make_update_package.py'
REQUIRED = ('browser.exe', 'update-helper.exe', 'WebView2Loader.dll', 'version.json')
TOP_PATTERN = re.compile(r'^(browser\.exe|update-helper\.exe|WebView2Loader\.dll|version\.json|'
                         r'[A-Za-z0-9.]+-(LICENSE|NOTICE)\.txt)$')
# Runtime data that may sit beside a portable exe; never packaged, never touched.
RUNTIME_DATA = {'profile', 'session', 'session.tmp', 'downloads', 'downloads.tmp',
                'bookmarks', 'bookmarks.bak', 'bookmarks.tmp', 'prototype.log', 'browser.log',
                'annotations'}  # annotation store of a portable/preview build (never packaged)
FORBIDDEN_SUFFIXES = ('.o', '.obj', '.a', '.pdb', '.log', '.tmp', '.partial', '.cpp', '.h', '.py', '.ps1')


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def stage(app: Path, staging: Path) -> list[str]:
    if staging.exists():
        shutil.rmtree(staging)
    staging.mkdir(parents=True)
    copied, skipped = [], []
    for entry in sorted(app.iterdir(), key=lambda p: p.name.lower()):
        if entry.is_symlink():
            raise SystemExit(f'Refusing symlink/reparse point in payload: {entry}')
        if entry.name in RUNTIME_DATA or entry.name.startswith('bookmarks.damaged-'):
            skipped.append(entry.name)
            continue
        if entry.is_dir():
            if entry.name != 'assets':
                raise SystemExit(f'Unexpected directory in app payload: {entry.name}')
            for item in sorted(entry.rglob('*')):
                if item.is_symlink():
                    raise SystemExit(f'Refusing symlink in assets: {item}')
                if item.is_file():
                    if item.suffix.lower() in FORBIDDEN_SUFFIXES:
                        raise SystemExit(f'Refusing non-asset file: {item}')
                    rel = item.relative_to(app)
                    (staging / rel).parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(item, staging / rel)
                    copied.append(rel.as_posix())
            continue
        if not TOP_PATTERN.match(entry.name):
            raise SystemExit(f'Unexpected file in app payload: {entry.name}')
        shutil.copy2(entry, staging / entry.name)
        copied.append(entry.name)
    for name in REQUIRED:
        if not (staging / name).is_file():
            raise SystemExit(f'Required payload file missing: {name}')
    if skipped:
        print('Not packaged (runtime data):', ', '.join(skipped))
    return copied


ZIP_TIME = (1980, 1, 1, 0, 0, 0)  # fixed: same payload -> same ZIP bytes


def zip_name(version: str) -> str:
    return f'browser-{version}-win-x64.zip'


def write_zip(staging: Path, files: list[str], archive: Path, top: str) -> None:
    """Archive exactly `files` (posix paths relative to `staging`) under `top`/."""
    if archive.exists():
        raise SystemExit(f'Refusing to overwrite {archive}')
    with zipfile.ZipFile(archive, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for rel in sorted(files, key=str.lower):
            src = staging / rel
            if src.is_symlink() or not src.is_file():
                raise SystemExit(f'Refusing non-regular staged file: {rel}')
            info = zipfile.ZipInfo(f'{top}/{rel}', date_time=ZIP_TIME)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            info.create_system = 0
            z.writestr(info, src.read_bytes(), compresslevel=9)


def verify_zip(archive: Path, staging: Path, files: list[str], top: str) -> None:
    """The ZIP must hold exactly the staged files, byte-identical, under one top folder."""
    expected = sorted(f'{top}/{rel}' for rel in files)
    with zipfile.ZipFile(archive) as z:
        names = z.namelist()
        if sorted(names) != expected or len(set(names)) != len(names):
            raise SystemExit(f'ZIP entries differ from the staged payload: {sorted(set(names) ^ set(expected))}')
        if z.testzip() is not None:
            raise SystemExit('ZIP CRC check failed')
        for rel in files:
            if hashlib.sha256(z.read(f'{top}/{rel}')).digest() != hashlib.sha256((staging / rel).read_bytes()).digest():
                raise SystemExit(f'ZIP entry differs from staged bytes: {rel}')


def read_manifest(path: Path) -> dict:
    raw = path.read_bytes()
    if len(raw) > 1024 or b'\r' in raw:
        raise SystemExit('Manifest too large or not LF')
    lines = raw.decode('utf-8').rstrip('\n').split('\n')
    keys = ['browser-update', 'version', 'arch', 'package', 'size', 'sha256']
    if len(lines) != 6:
        raise SystemExit(f'Manifest must have 6 lines, has {len(lines)}')
    fields = {}
    for key, line in zip(keys, lines):
        k, _, v = line.partition(' ')
        if k != key or not v:
            raise SystemExit(f'Bad manifest line: {line!r}')
        fields[k] = v
    return fields


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--app', required=True, help='built application directory')
    p.add_argument('--out', required=True, help='release asset directory (recreated)')
    a = p.parse_args()
    app, out = (ROOT / a.app).resolve(), (ROOT / a.out).resolve()
    info = json.loads((app / 'version.json').read_text(encoding='utf-8'))
    version = info['version']
    if not re.fullmatch(r'(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})', version):
        raise SystemExit(f'Bad version {version!r}')
    if info.get('arch') != 'x64':
        raise SystemExit('Only x64 payloads are packaged')
    staging = out.parent / (out.name + '-payload')
    files = stage(app, staging)
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    subprocess.run([sys.executable, '-I', str(PACKER), '--input', str(staging), '--version', version,
                    '--out', str(out)], check=True)
    package = out / f'browser-{version}-win-x64.lsrupd'
    manifest = out / 'update-manifest-win-x64.txt'
    m = read_manifest(manifest)
    expected = {'browser-update': '1', 'version': version, 'arch': 'x64', 'package': package.name,
                'size': str(package.stat().st_size), 'sha256': sha256(package)}
    if m != expected:
        raise SystemExit(f'Manifest mismatch:\n{m}\n!=\n{expected}')
    unexpected = sorted(p.name for p in out.iterdir()) != sorted([package.name, manifest.name])
    if unexpected:
        raise SystemExit(f'Unexpected packer outputs: {[p.name for p in out.iterdir()]}')
    archive = out / zip_name(version)
    top = archive.name[:-len('.zip')]
    write_zip(staging, files, archive, top)
    verify_zip(archive, staging, files, top)
    zip_sha = sha256(archive)
    (out / 'SHA256SUMS.txt').write_text(
        ''.join(f'{sha256(f)}  {f.name}\n' for f in (package, manifest, archive)), encoding='ascii', newline='\n')
    (out / 'release-info.json').write_text(json.dumps({**info, 'package': package.name,
        'packageSize': package.stat().st_size, 'packageSha256': expected['sha256'],
        'zip': archive.name, 'zipSize': archive.stat().st_size, 'zipSha256': zip_sha,
        'files': len(files)}, indent=2) + '\n', encoding='utf-8', newline='\n')
    print(f'Package OK: {package.name} {expected["size"]} bytes sha256 {expected["sha256"]} ({len(files)} files)')
    print(f'ZIP OK: {archive.name} {archive.stat().st_size} bytes sha256 {zip_sha} (top folder {top}/)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
