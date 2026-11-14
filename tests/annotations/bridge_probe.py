#!/usr/bin/env python3
"""BridgeUsable probes in separate processes (review of 65adadd, two P2): malformed PE offsets must be refused without a
crash, and a reparse point anywhere in the bridge's path (not only its folder) must be refused.

    python -I tests/annotations/bridge_probe.py [--bridge build/annotations-test/browser-annotations-mcp.exe]

Builds build/bridge-probe/bridge_probe.exe from the CURRENT source, makes fixtures under build/bridge-probe/fx (local,
temporary: no real configuration, profile or user folder is touched) and prints one line per case. Exit 0 when every
case gives its expected verdict (and no process crashed).
"""
import os
import pathlib
import shutil
import struct
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / 'build' / 'bridge-probe'
FX = OUT / 'fx'
GXX = ROOT / 'tools' / 'w64devkit' / 'bin' / 'g++.exe'
FLAGS = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-cast-function-type', '-Wno-missing-field-initializers', '-static', '-Isrc', '-municode']


def build():
    OUT.mkdir(parents=True, exist_ok=True)
    cmd = [str(GXX), *FLAGS, 'tests/annotations/bridge_probe.cpp', 'src/agent-connect.cpp', 'src/annotations.cpp', '-o', str(OUT / 'bridge_probe.exe'), '-lbcrypt', '-lshlwapi', '-lshell32', '-lole32']
    r = subprocess.run(cmd, cwd=ROOT)
    if r.returncode:
        raise SystemExit('build failed')


def probe(path):
    r = subprocess.run([str(OUT / 'bridge_probe.exe'), str(path)], capture_output=True, text=True, timeout=30)
    return r.returncode, r.stdout.strip()


def junction(link, target):
    r = subprocess.run(['cmd', '/c', 'mklink', '/J', str(link), str(target)], capture_output=True, text=True)
    return r.returncode == 0


def pe_with_offset(base: bytes, offset: int, size=None) -> bytes:
    b = bytearray(base if size is None else base[:size].ljust(size, b'\0'))
    if len(b) < 0x40:
        b = b.ljust(0x40, b'\0')
    b[0x3C:0x40] = struct.pack('<I', offset & 0xFFFFFFFF)
    return bytes(b)


def main(argv):
    bridge = ROOT / (argv[argv.index('--bridge') + 1] if '--bridge' in argv else 'build/annotations-test/browser-annotations-mcp.exe')
    if not bridge.exists():
        raise SystemExit(f'{bridge} missing: python -I tests/annotations/run_tests.py --quick builds it')
    build()
    if FX.exists():
        for p in sorted(FX.rglob('*'), key=lambda p: -len(p.parts)):   # junctions first: never followed when deleting
            if p.is_dir() and os.path.isjunction(p):
                os.rmdir(p)
        shutil.rmtree(FX, ignore_errors=True)
    FX.mkdir(parents=True)
    real = FX / 'real' / 'bin'; real.mkdir(parents=True)
    shutil.copy2(bridge, real / 'browser-annotations-mcp.exe')
    good = (real / 'browser-annotations-mcp.exe').read_bytes()
    cases = []   # (name, path, expected usable)
    cases.append(('valid bridge, plain path', real / 'browser-annotations-mcp.exe', True))
    # ---- P2 #2: reparse points in ANY ancestor
    ok_alias = junction(FX / 'alias', FX / 'real')
    cases.append(('ancestor junction alias -> real (bin itself plain)', FX / 'alias' / 'bin' / 'browser-annotations-mcp.exe', False))
    deep = FX / 'a' / 'b'; deep.mkdir(parents=True)
    ok_deep = junction(deep / 'c', FX / 'real')
    cases.append(('junction two levels above bin', deep / 'c' / 'bin' / 'browser-annotations-mcp.exe', False))
    ok_bin = junction(FX / 'binlink', real)
    cases.append(('immediate folder is a junction', FX / 'binlink' / 'browser-annotations-mcp.exe', False))
    # ---- P2 #1: malformed e_lfanew (offsets near UINT32_MAX, past the read, near the boundary, truncated files)
    mal = FX / 'mal'; mal.mkdir()
    for off in (0xFFFFFFF0, 0xFFFFFFFF, 0xFFFFFFA0, 0x80000000, 0x7FFFFFFF, 0x1000, 0xFFF, 0xF00, 0x40 - 1):
        p = mal / f'off-{off:08x}.exe'; p.write_bytes(pe_with_offset(b'MZ'.ljust(128, b'\0'), off)); cases.append((f'128 B, e_lfanew 0x{off:08X}', p, False))
    pe = struct.unpack('<I', good[0x3C:0x40])[0]
    for cut in (0x40, pe, pe + 4, pe + 24, pe + 24 + 69, pe + 24 + 70):
        p = mal / f'truncated-{cut}.exe'; p.write_bytes(good[:cut]); cases.append((f'real bridge truncated to {cut} B (e_lfanew {pe})', p, cut >= pe + 24 + 70))   # a header screen: a complete header passes
    for off in (4096 - 24 - 70, 4096 - 24 - 69, 4096 - 4):   # in the 4096-byte read window, header straddling its end
        p = mal / f'edge-{off}.exe'; p.write_bytes(pe_with_offset(good.ljust(8192, b'\0'), off)); cases.append((f'8 KiB, e_lfanew {off} (window edge)', p, False))
    failures = 0
    print(f'junctions made: alias={ok_alias} deep={ok_deep} bin={ok_bin}')
    for name, path, expected in cases:
        code, out = probe(path)
        crashed = code not in (0, 1)
        verdict = 'CRASH' if crashed else ('usable' if code == 0 else 'refused')
        good_case = not crashed and (code == 0) == expected
        failures += not good_case
        print(f"{'ok  ' if good_case else 'FAIL'} {name}: exit {code} ({verdict}), expected {'usable' if expected else 'refused'} | {out}")
    if not (ok_alias and ok_deep and ok_bin):
        failures += 1; print('FAIL a fixture junction could not be created')
    print(f'{failures} failures')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
