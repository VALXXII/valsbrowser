"""Existing clients accept the release bridge payload (plan B item 2).

Compiles the update-package validator EXACTLY as shipped in v0.3.239 (source commit
f66594395cfc1682ee45dc88966271f4e8797eab, the public latest before the bridge) from git history,
with its own interop_check driver (manifest parse, size/SHA-256, ExtractPackage, VerifyStagedTree:
the steps an installed v0.3.239 runs before staging an update). Then:

  positive  package.py output whose payload holds a REAL compiled browser-annotations-mcp.exe at
            assets/bin/ (built from the current src/mcp-bridge.cpp) -> accepted, staged bytes
            identical to the payload;
  negative  the same payload with the bridge at the root (what a naive release would ship),
            written with the documented .lsrupd v1 format -> rejected by the old validator.

  python tests/update-integration/old_validator.py [--payload <app dir>]

--payload uses a real application directory (for example an unpacked public release) instead of
the synthetic one; the bridge is added to a copy. Never runs browser.exe or the bridge. Needs the
pinned tools/w64devkit and full git history (CI: fetch-depth 0).
"""
from __future__ import annotations
import argparse, hashlib, json, shutil, struct, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OLD_COMMIT = 'f66594395cfc1682ee45dc88966271f4e8797eab'   # v0.3.239 (release notes of the public feed)
OLD_FILES = ['src/update-package.h', 'src/update-package.cpp', 'tests/update-core/interop_check.cpp']
GXX = ROOT / 'tools/w64devkit/bin/g++.exe'
OUT = ROOT / 'build/old-validator'
BRIDGE = 'assets/bin/browser-annotations-mcp.exe'


def sh(cmd, **kw):
    return subprocess.run([str(c) for c in cmd], cwd=ROOT, check=True, capture_output=True, text=True, **kw)


def build_old_validator() -> Path:
    src = OUT / 'v0.3.239'
    for rel in OLD_FILES:
        text = sh(['git', 'show', f'{OLD_COMMIT}:{rel}']).stdout
        (src / rel).parent.mkdir(parents=True, exist_ok=True)
        (src / rel).write_text(text, encoding='utf-8', newline='\n')
    exe = OUT / 'interop_check_v0.3.239.exe'
    sh([GXX, '-std=c++17', '-O2', '-static', '-municode', src / 'tests/update-core/interop_check.cpp',
        src / 'src/update-package.cpp', '-o', exe, '-lbcrypt'])
    return exe


def build_bridge() -> Path:
    exe = OUT / 'browser-annotations-mcp.exe'
    sh([GXX, '-std=c++17', '-O2', '-static', '-municode', '-Isrc', 'src/mcp-bridge.cpp', 'src/annotations.cpp',
        'src/update-core.cpp', 'src/update-package.cpp', '-o', exe, '-lwinhttp', '-lbcrypt', '-lkernel32'])
    return exe


def synthetic_app(root: Path) -> Path:
    app = root / 'app'
    (app / 'assets/fonts').mkdir(parents=True)
    for name in ('browser.exe', 'update-helper.exe', 'WebView2Loader.dll', 'WebView2-NOTICE.txt'):
        (app / name).write_bytes(name.encode() * 64)
    (app / 'assets/fonts/a.ttf').write_bytes(b'font')
    (app / 'version.json').write_text(json.dumps({'version': '0.3.7', 'arch': 'x64', 'channel': 'release'}))
    return app


def write_lsrupd(payload: Path, version: str, out: Path) -> tuple[Path, Path]:
    """.lsrupd v1 exactly as documented in tools/update-package/make_update_package.py, WITHOUT its
    allowlist (only used to build the negative case the real packer refuses to produce)."""
    files = sorted((p.relative_to(payload).as_posix() for p in payload.rglob('*') if p.is_file()), key=str.lower)
    data = bytearray(b'LSRUPD01')
    for s in (version, 'x64'):
        data += struct.pack('<H', len(s)) + s.encode('ascii')
    data += struct.pack('<I', len(files))
    for rel in files:
        body = (payload / rel).read_bytes()
        data += struct.pack('<H', len(rel)) + rel.encode('ascii') + struct.pack('<Q', len(body))
        data += hashlib.sha256(body).digest() + body
    data += b'LSRUEND1'
    out.mkdir(parents=True, exist_ok=True)
    pkg = out / f'browser-{version}-win-x64.lsrupd'
    pkg.write_bytes(bytes(data))
    manifest = out / 'update-manifest-win-x64.txt'
    manifest.write_bytes((f'browser-update 1\nversion {version}\narch x64\npackage {pkg.name}\n'
                          f'size {len(data)}\nsha256 {hashlib.sha256(data).hexdigest()}\n').encode('ascii'))
    return pkg, manifest


def old_validate(exe: Path, pkg: Path, manifest: Path, stage_parent: Path) -> subprocess.CompletedProcess:
    stage_parent.mkdir(parents=True, exist_ok=True)
    return subprocess.run([str(exe), str(pkg), str(manifest), str(stage_parent)], capture_output=True, text=True)


def tree(path: Path) -> dict:
    return {p.relative_to(path).as_posix(): p.read_bytes() for p in path.rglob('*') if p.is_file() and p.name != '.stage'}


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--payload', help='real application directory to use instead of the synthetic one')
    a = p.parse_args()
    if not GXX.exists():
        print(f'FAILED: {GXX} missing (run setup.ps1)')
        return 2
    OUT.mkdir(parents=True, exist_ok=True)
    validator, bridge = build_old_validator(), build_bridge()
    print(f'old validator built from {OLD_COMMIT} ({", ".join(OLD_FILES)})')
    with tempfile.TemporaryDirectory(dir=OUT) as tmp:
        tmp = Path(tmp)
        if a.payload:
            app = tmp / 'app'
            shutil.copytree(Path(a.payload), app)
        else:
            app = synthetic_app(tmp)
        (app / BRIDGE).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(bridge, app / BRIDGE)
        version = json.loads((app / 'version.json').read_text(encoding='utf-8'))['version']
        dist = tmp / 'dist'
        sh([sys.executable, '-I', 'tools/release/package.py', '--app', app, '--out', dist])
        pkg, manifest = dist / f'browser-{version}-win-x64.lsrupd', dist / 'update-manifest-win-x64.txt'
        r = old_validate(validator, pkg, manifest, tmp / 'stage-positive')
        print(f'positive (bridge at {BRIDGE}): exit {r.returncode} {r.stdout.strip()}')
        if r.returncode != 0:
            return 1
        staged = tree(tmp / 'stage-positive' / version)
        if staged != tree(app) or staged.get(BRIDGE) != bridge.read_bytes():
            print('FAILED: staged tree differs from the payload')
            return 1
        print(f'  staged {len(staged)} files == payload, bridge {len(staged[BRIDGE])} bytes identical')
        bad = tmp / 'root-bridge'
        shutil.copytree(app, bad)
        (bad / BRIDGE).rename(bad / 'browser-annotations-mcp.exe')
        pkg, manifest = write_lsrupd(bad, version, tmp / 'dist-root')
        r = old_validate(validator, pkg, manifest, tmp / 'stage-negative')
        print(f'negative (bridge at the root): exit {r.returncode} {r.stdout.strip()}')
        if r.returncode == 0:
            print('FAILED: the old validator accepted a root-level bridge')
            return 1
    print('OLD VALIDATOR OK')
    return 0


if __name__ == '__main__':
    sys.exit(main())
