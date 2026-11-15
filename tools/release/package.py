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
5. Writes the public installer browser-<version>-win-x64-setup.exe = setup stub (built by
   build.py into <app>-setup/, checked: GUI x64 PE, asInvoker manifest, identity string ==
   version/commit/channel of version.json) || the .lsrupd above, byte for byte || a 128-byte
   trailer (src/setup-payload.h), then re-reads it (overlay bounds, trailer, embedded bytes).
6. Writes SHA256SUMS.txt (package, manifest, ZIP, setup) and release-info.json.
"""
from __future__ import annotations
import argparse, hashlib, json, os, re, shutil, stat, subprocess, sys, zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKER = ROOT / 'tools/update-package/make_update_package.py'
REQUIRED = ('browser.exe', 'update-helper.exe', 'WebView2Loader.dll', 'version.json')
TOP_PATTERN = re.compile(r'^(browser\.exe|update-helper\.exe|WebView2Loader\.dll|version\.json|'
                         r'[A-Za-z0-9.]+-(LICENSE|NOTICE)\.txt)$')
# Runtime data that may sit beside a portable exe; never packaged, never touched.
RUNTIME_DATA = {'profile', 'session', 'session.tmp', 'downloads', 'downloads.tmp',
                'bookmarks', 'bookmarks.bak', 'bookmarks.tmp', 'prototype.log', 'browser.log',
                'annotations',  # annotation store of a portable/preview build (never packaged)
                'bookmark-imports',  # stable identities of imported bookmark folders (src/browser-import.h)
                'ui-language'}  # browser-wide interface language preference (src/ui-language.h)
FORBIDDEN_SUFFIXES = ('.o', '.obj', '.a', '.pdb', '.log', '.tmp', '.partial', '.cpp', '.h', '.py', '.ps1')
# Executable code under assets/ is refused, with ONE documented exception: the annotations MCP
# bridge (tools/release/build.py RELEASE_BRIDGE). assets/** is the only location every existing
# client's AllowedPayloadPath accepts besides the fixed root names (src/update-package.cpp).
RELEASE_BRIDGE = 'assets/bin/browser-annotations-mcp.exe'
EXECUTABLE_SUFFIXES = ('.exe', '.dll', '.com', '.scr', '.sys', '.cpl', '.ocx', '.msi', '.msp', '.bat',
                       '.cmd', '.vbs', '.vbe', '.lnk', '.url', '.appx', '.msix')


def check_console_x64(path: Path) -> None:
    """The bridge must be a Windows x64 console PE (stdio is its MCP transport)."""
    data = path.read_bytes()
    if len(data) < 0x40 or data[:2] != b'MZ':
        raise SystemExit(f'{path.name} is not a PE image')
    pe = int.from_bytes(data[0x3C:0x40], 'little')
    if data[pe:pe + 4] != b'PE\0\0' or int.from_bytes(data[pe + 4:pe + 6], 'little') != 0x8664:
        raise SystemExit(f'{path.name} is not a Windows x64 PE')
    opt = pe + 24
    if int.from_bytes(data[opt:opt + 2], 'little') != 0x20B:  # PE32+
        raise SystemExit(f'{path.name} is not PE32+')
    if int.from_bytes(data[opt + 68:opt + 70], 'little') != 3:  # IMAGE_SUBSYSTEM_WINDOWS_CUI
        raise SystemExit(f'{path.name} is not a console executable')


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


FILE_ATTRIBUTE_REPARSE_POINT = 0x400


def is_reparse(path: Path) -> bool:
    """Symlink, junction or any other reparse point (Path.is_symlink() misses junctions)."""
    st = os.lstat(path)
    return stat.S_ISLNK(st.st_mode) or bool(getattr(st, 'st_file_attributes', 0) & FILE_ATTRIBUTE_REPARSE_POINT)


def walk_assets(root: Path) -> list[Path]:
    """All regular files under root. Every entry is checked with lstat BEFORE it is descended
    into or copied: a reparse point anywhere (including root) fails packaging."""
    if is_reparse(root):
        raise SystemExit(f'Refusing symlink/reparse point in payload: {root}')
    files, pending = [], [root]
    while pending:
        current = pending.pop()
        for item in sorted(current.iterdir(), key=lambda p: p.name):
            if is_reparse(item):
                raise SystemExit(f'Refusing symlink/reparse point in assets: {item}')
            mode = os.lstat(item).st_mode
            if stat.S_ISDIR(mode):
                pending.append(item)
            elif stat.S_ISREG(mode):
                files.append(item)
            else:
                raise SystemExit(f'Refusing special file in assets: {item}')
    return sorted(files)


def stage(app: Path, staging: Path) -> list[str]:
    if is_reparse(app):
        raise SystemExit(f'Refusing symlink/reparse point as app directory: {app}')
    if staging.exists():
        shutil.rmtree(staging)
    staging.mkdir(parents=True)
    copied, skipped = [], []
    for entry in sorted(app.iterdir(), key=lambda p: p.name.lower()):
        if is_reparse(entry):
            raise SystemExit(f'Refusing symlink/reparse point in payload: {entry}')
        if entry.name in RUNTIME_DATA or entry.name.startswith(('bookmarks.damaged-', 'ui-language.tmp', 'bookmark-imports.tmp')):
            skipped.append(entry.name)
            continue
        if entry.is_dir():
            if entry.name != 'assets':
                raise SystemExit(f'Unexpected directory in app payload: {entry.name}')
            for item in walk_assets(entry):
                if item.suffix.lower() in FORBIDDEN_SUFFIXES:
                    raise SystemExit(f'Refusing non-asset file: {item}')
                rel = item.relative_to(app)
                if item.name.lower().endswith(EXECUTABLE_SUFFIXES):
                    if rel.as_posix() != RELEASE_BRIDGE:  # exact path and case
                        raise SystemExit(f'Refusing executable under assets: {rel.as_posix()}')
                    check_console_x64(item)
                (staging / rel).parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(item, staging / rel)
                copied.append(rel.as_posix())
            continue
        if not stat.S_ISREG(os.lstat(entry).st_mode):
            raise SystemExit(f'Refusing special file in app payload: {entry.name}')
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


# ---------------------------------------------------------------- setup (design/windows-setup-plan.md)
# browser-<version>-win-x64-setup.exe = setup stub (built by build.py into <app>-setup/) || the .lsrupd, byte for
# byte || a 128-byte trailer. Same layout as src/setup-payload.h (LocateOverlay / ParseTrailer).
SETUP_MAGIC = b'BROWSER-SETUP 1\0'
SETUP_END = b'BRSETUP!'
SETUP_TRAILER = 128
SETUP_STUB_NAME = 'browser-setup.exe'
MAX_PACKAGE = 512 * 1024 * 1024  # UpdatePackage::MaxPackageBytes


def setup_name(version: str) -> str:
    return f'browser-{version}-win-x64-setup.exe'


def pe_overlay(data: bytes) -> tuple[int, int, bool]:
    """(overlay start, overlay end, signed) of a PE32+ x64 image; mirrors SetupPayload::LocateOverlay."""
    if len(data) < 0x40 or data[:2] != b'MZ':
        raise SystemExit('setup: not a PE image')
    pe = int.from_bytes(data[0x3C:0x40], 'little')
    if pe < 0x40 or pe + 24 > min(len(data), 4096) or data[pe:pe + 4] != b'PE\0\0':
        raise SystemExit('setup: bad PE header')
    if int.from_bytes(data[pe + 4:pe + 6], 'little') != 0x8664:
        raise SystemExit('setup: not x64')
    sections = int.from_bytes(data[pe + 6:pe + 8], 'little')
    opt_size = int.from_bytes(data[pe + 20:pe + 22], 'little')
    opt = pe + 24
    if opt_size < 152 or opt + opt_size > 4096 or int.from_bytes(data[opt:opt + 2], 'little') != 0x20B:
        raise SystemExit('setup: not PE32+')
    if not 0 < sections <= 96 or opt + opt_size + 40 * sections > 4096:
        raise SystemExit('setup: bad section table')
    end = int.from_bytes(data[opt + 60:opt + 64], 'little')
    for i in range(sections):
        s = opt + opt_size + 40 * i
        raw, ptr = int.from_bytes(data[s + 16:s + 20], 'little'), int.from_bytes(data[s + 20:s + 24], 'little')
        if raw and ptr + raw > end:
            end = ptr + raw
    if end > len(data):
        raise SystemExit('setup: image larger than file')
    overlay_end, signed = len(data), False
    if int.from_bytes(data[opt + 108:opt + 112], 'little') > 4:
        cert = int.from_bytes(data[opt + 144:opt + 148], 'little')
        cert_size = int.from_bytes(data[opt + 148:opt + 152], 'little')
        if cert or cert_size:
            if cert < end or cert + cert_size != len(data):
                raise SystemExit('setup: bad certificate table')
            overlay_end, signed = cert, True
    return end, overlay_end, signed


def stub_identity(stub: bytes) -> str:
    """The `kSetupIdentity` string compiled into the stub (src/setup-main.cpp)."""
    marker = b'browser-setup-identity '
    if stub.count(marker) != 1:
        raise SystemExit('setup stub: identity string missing or ambiguous')
    start = stub.index(marker) + len(marker)
    end = stub.index(b';', start)
    return stub[start:end].decode('ascii')


def check_setup_stub(stub: bytes, info: dict) -> None:
    """The stub must be a GUI x64 PE without overlay or signature, built for exactly this version/commit/channel."""
    start, end, signed = pe_overlay(stub)
    if start != len(stub) or end != len(stub) or signed:
        raise SystemExit('setup stub already has an overlay or a signature')
    pe = int.from_bytes(stub[0x3C:0x40], 'little')
    if int.from_bytes(stub[pe + 24 + 68:pe + 24 + 70], 'little') != 2:  # IMAGE_SUBSYSTEM_WINDOWS_GUI
        raise SystemExit('setup stub is not a GUI executable')
    want = f'{info["version"]} {info["commit"]} {info["channel"]}'
    if stub_identity(stub) != want:
        raise SystemExit(f'setup stub identity {stub_identity(stub)!r} != payload {want!r}')
    if b'requestedExecutionLevel level="asInvoker"' not in stub:
        raise SystemExit('setup stub has no asInvoker manifest (Windows would elevate a "setup" exe)')
    # Test builds (-DSETUP_TESTING: folder/registry redirection, fault points, unattended mode) never ship.
    for marker in ('BROWSER_SETUP_TEST_ROOT', 'BROWSER_SETUP_FAULT', '--unattended'):
        if marker.encode('utf-16-le') in stub:
            raise SystemExit(f'setup stub is a test build ({marker})')


def setup_trailer(version: str, offset: int, package: bytes) -> bytes:
    v = version.encode('ascii')
    if len(v) > 32:
        raise SystemExit('version too long for the setup trailer')
    t = SETUP_MAGIC + v.ljust(32, b'\0') + offset.to_bytes(8, 'little') + len(package).to_bytes(8, 'little') \
        + hashlib.sha256(package).digest() + b'\0' * 24 + SETUP_END
    assert len(t) == SETUP_TRAILER
    return t


def write_setup(stub_path: Path, package: Path, info: dict, out: Path) -> None:
    if out.exists():
        raise SystemExit(f'Refusing to overwrite {out}')
    if is_reparse(stub_path) or not stub_path.is_file():
        raise SystemExit(f'setup stub missing or not a regular file: {stub_path}')
    stub, pkg = stub_path.read_bytes(), package.read_bytes()
    check_setup_stub(stub, info)
    if not 0 < len(pkg) <= MAX_PACKAGE:
        raise SystemExit('package size out of range for the setup')
    with out.open('xb') as f:
        f.write(stub + pkg + setup_trailer(info['version'], len(stub), pkg))


def verify_setup(setup: Path, package: Path, info: dict) -> None:
    """Re-reads the setup: overlay bounds, trailer fields, embedded bytes == the .lsrupd, stub identity."""
    data, pkg = setup.read_bytes(), package.read_bytes()
    start, end, _ = pe_overlay(data)
    if end - start < SETUP_TRAILER:
        raise SystemExit('setup has no embedded package')
    t = data[end - SETUP_TRAILER:end]
    if t[:16] != SETUP_MAGIC or t[120:] != SETUP_END or any(t[96:120]):
        raise SystemExit('setup trailer magic/padding invalid')
    version = t[16:48].rstrip(b'\0')
    if b'\0' in version or version.decode('ascii') != info['version']:
        raise SystemExit('setup trailer version differs')
    offset, size = int.from_bytes(t[48:56], 'little'), int.from_bytes(t[56:64], 'little')
    if offset != start or offset + size + SETUP_TRAILER != end or size != len(pkg):
        raise SystemExit('setup trailer offsets do not match the overlay')
    if t[64:96] != hashlib.sha256(pkg).digest() or data[offset:offset + size] != pkg:
        raise SystemExit('setup does not embed exactly the .lsrupd')
    check_setup_stub(data[:start], info)


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
    p.add_argument('--setup-stub', help=f'setup stub built by build.py (default: <app>-setup/{SETUP_STUB_NAME})')
    a = p.parse_args()
    app, out = (ROOT / a.app).resolve(), (ROOT / a.out).resolve()
    stub = (ROOT / a.setup_stub).resolve() if a.setup_stub else app.parent / (app.name + '-setup') / SETUP_STUB_NAME
    info = json.loads((app / 'version.json').read_text(encoding='utf-8'))
    version = info['version']
    if not re.fullmatch(r'(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})', version):
        raise SystemExit(f'Bad version {version!r}')
    if info.get('arch') != 'x64':
        raise SystemExit('Only x64 payloads are packaged')
    # The setup is a required asset: its stub is checked before anything is packed.
    if not stub.is_file() or is_reparse(stub):
        raise SystemExit(f'setup stub missing: {stub} (built by tools/release/build.py)')
    check_setup_stub(stub.read_bytes(), info)
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
    setup = out / setup_name(version)
    write_setup(stub, package, info, setup)
    verify_setup(setup, package, info)
    setup_sha = sha256(setup)
    (out / 'SHA256SUMS.txt').write_text(
        ''.join(f'{sha256(f)}  {f.name}\n' for f in (package, manifest, archive, setup)), encoding='ascii', newline='\n')
    (out / 'release-info.json').write_text(json.dumps({**info, 'package': package.name,
        'packageSize': package.stat().st_size, 'packageSha256': expected['sha256'],
        'zip': archive.name, 'zipSize': archive.stat().st_size, 'zipSha256': zip_sha,
        'setup': setup.name, 'setupSize': setup.stat().st_size, 'setupSha256': setup_sha,
        'files': len(files)}, indent=2) + '\n', encoding='utf-8', newline='\n')
    print(f'Package OK: {package.name} {expected["size"]} bytes sha256 {expected["sha256"]} ({len(files)} files)')
    print(f'ZIP OK: {archive.name} {archive.stat().st_size} bytes sha256 {zip_sha} (top folder {top}/)')
    print(f'Setup OK: {setup.name} {setup.stat().st_size} bytes sha256 {setup_sha} (embeds {package.name})')
    return 0


if __name__ == '__main__':
    sys.exit(main())
