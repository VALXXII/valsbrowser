#!/usr/bin/env python3
"""Build a browser update package (.lsrupd v1) and its manifest. Python 3 stdlib only.

    python -I tools/update-package/make_update_package.py --input <payloadDir> --version 0.3.N --out <outDir>
    python -I tools/update-package/make_update_package.py --verify <package> [--manifest <manifest>]

The payload policy mirrors src/update-package.cpp (ValidEntryPath + AllowedPayloadPath). Any file outside the
allowlist (profile, session, logs, object files, ...) makes the build FAIL: nothing is silently skipped.
Format (little-endian): "LSRUPD01", u16+version, u16+"x64", u32 count, entries (u16+path, u64 size,
32-byte sha256, content), "LSRUEND1", EOF. Entries are sorted (case-insensitive) for reproducibility.
"""
import argparse
import hashlib
import os
import re
import stat
import struct
import sys

MAGIC = b"LSRUPD01"
TRAILER = b"LSRUEND1"
ARCH = "x64"
MAX_PACKAGE = 512 * 1024 * 1024
MAX_ENTRY = 256 * 1024 * 1024
MAX_ENTRIES = 4096
MAX_PATH = 240
VERSION_RE = re.compile(r"^(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})$")
PATH_CHARS = re.compile(r"^[A-Za-z0-9._\-() +/]+$")
DEVICES = {"con", "prn", "aux", "nul", "conin$", "conout$"} | {f"com{i}" for i in range(1, 10)} | {f"lpt{i}" for i in range(1, 10)}
TOP_LEVEL = {"browser.exe", "update-helper.exe", "WebView2Loader.dll", "build-info.txt", "version.json"}
REQUIRED = {"browser.exe", "update-helper.exe"}


def fail(message):
    print(f"error: {message}", file=sys.stderr)
    sys.exit(1)


def package_name(version):
    return f"browser-{version}-win-x64.lsrupd"


def valid_path(path):
    if not path or len(path.encode()) > MAX_PATH or not PATH_CHARS.match(path):
        return False
    if path.startswith("/") or path.endswith("/"):
        return False
    for part in path.split("/"):
        if part in ("", ".", "..") or part[0] in ". " or part[-1] in ". ":
            return False
        if part.split(".")[0].lower() in DEVICES:
            return False
    return True


def allowed(path):
    if "/" not in path:
        return path in TOP_LEVEL or (len(path) > 12 and path.endswith("-LICENSE.txt")) or (len(path) > 11 and path.endswith("-NOTICE.txt"))
    return path.startswith("assets/")


def is_reparse(full):
    st = os.lstat(full)
    return stat.S_ISLNK(st.st_mode) or bool(getattr(st, "st_file_attributes", 0) & 0x400)


def collect(root):
    files = []
    for dirpath, dirnames, filenames in os.walk(root, followlinks=False):
        for name in list(dirnames):
            if is_reparse(os.path.join(dirpath, name)):
                fail(f"reparse point/symlink directory in payload: {os.path.join(dirpath, name)}")
        for name in filenames:
            full = os.path.join(dirpath, name)
            if is_reparse(full):
                fail(f"reparse point/symlink in payload: {full}")
            rel = os.path.relpath(full, root).replace(os.sep, "/")
            if not valid_path(rel):
                fail(f"invalid payload path: {rel}")
            if not allowed(rel):
                fail(f"payload entry not allowed (profile/session/logs/build files must not ship): {rel}")
            files.append((rel, full))
    files.sort(key=lambda f: (f[0].lower(), f[0]))
    seen, dirs = set(), set()
    for rel, _ in files:
        key = rel.lower()
        if key in seen or key in dirs:
            fail(f"duplicate/conflicting path (case-insensitive): {rel}")
        seen.add(key)
        parts = key.split("/")
        for i in range(1, len(parts)):
            d = "/".join(parts[:i])
            if d in seen:
                fail(f"file/directory conflict: {rel}")
            dirs.add(d)
    names = {rel for rel, _ in files}
    if not REQUIRED <= names:
        fail(f"payload misses required files: {sorted(REQUIRED - names)}")
    if not 0 < len(files) <= MAX_ENTRIES:
        fail("entry count out of range")
    return files


def build(input_dir, version, out_dir):
    if not VERSION_RE.match(version) or version == "0.0.0":
        fail(f"invalid version {version!r} (expected M.m.B, not 0.0.0)")
    files = collect(input_dir)
    os.makedirs(out_dir, exist_ok=True)
    pkg_path = os.path.join(out_dir, package_name(version))
    tmp = pkg_path + ".tmp"
    total = hashlib.sha256()
    size = 0
    with open(tmp, "wb") as out:
        def put(data):
            nonlocal size
            out.write(data)
            total.update(data)
            size += len(data)
        put(MAGIC)
        put(struct.pack("<H", len(version)) + version.encode("ascii"))
        put(struct.pack("<H", len(ARCH)) + ARCH.encode("ascii"))
        put(struct.pack("<I", len(files)))
        for rel, full in files:
            with open(full, "rb") as f:
                data = f.read()
            if len(data) > MAX_ENTRY:
                fail(f"entry too large: {rel}")
            encoded = rel.encode("ascii")
            put(struct.pack("<H", len(encoded)) + encoded + struct.pack("<Q", len(data)) + hashlib.sha256(data).digest())
            put(data)
        put(TRAILER)
    if size > MAX_PACKAGE:
        os.remove(tmp)
        fail("package exceeds 512 MiB")
    os.replace(tmp, pkg_path)
    manifest = (f"browser-update 1\nversion {version}\narch {ARCH}\npackage {package_name(version)}\n"
                f"size {size}\nsha256 {total.hexdigest()}\n")
    manifest_path = os.path.join(out_dir, "update-manifest-win-x64.txt")
    with open(manifest_path, "w", encoding="ascii", newline="\n") as f:
        f.write(manifest)
    print(f"package  {pkg_path}\nmanifest {manifest_path}\nentries  {len(files)}\nsize     {size}\nsha256   {total.hexdigest()}")


def verify(pkg_path, manifest_path=None):
    with open(pkg_path, "rb") as f:
        data = f.read()
    pos = 0

    def take(n):
        nonlocal pos
        if pos + n > len(data):
            fail("truncated package")
        chunk = data[pos:pos + n]
        pos += n
        return chunk

    if take(8) != MAGIC:
        fail("bad magic")
    version = take(struct.unpack("<H", take(2))[0]).decode("ascii")
    arch = take(struct.unpack("<H", take(2))[0]).decode("ascii")
    if not VERSION_RE.match(version) or arch != ARCH:
        fail("bad version/arch")
    count = struct.unpack("<I", take(4))[0]
    if not 0 < count <= MAX_ENTRIES:
        fail("bad entry count")
    names = set()
    for _ in range(count):
        rel = take(struct.unpack("<H", take(2))[0]).decode("ascii")
        if not valid_path(rel) or not allowed(rel) or rel.lower() in names:
            fail(f"bad entry {rel!r}")
        names.add(rel.lower())
        size = struct.unpack("<Q", take(8))[0]
        digest = take(32)
        if size > MAX_ENTRY or hashlib.sha256(take(size)).digest() != digest:
            fail(f"entry hash/size mismatch {rel!r}")
    if take(8) != TRAILER or pos != len(data):
        fail("bad trailer or trailing data")
    if manifest_path:
        with open(manifest_path, "rb") as f:
            text = f.read().decode("ascii")
        expected = (f"browser-update 1\nversion {version}\narch {ARCH}\npackage {package_name(version)}\n"
                    f"size {len(data)}\nsha256 {hashlib.sha256(data).hexdigest()}\n")
        if text != expected:
            fail("manifest does not match package")
    print(f"ok {version} {count} entries {len(data)} bytes")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--input")
    ap.add_argument("--version")
    ap.add_argument("--out")
    ap.add_argument("--verify")
    ap.add_argument("--manifest")
    a = ap.parse_args()
    if a.verify:
        verify(a.verify, a.manifest)
    elif a.input and a.version and a.out:
        build(a.input, a.version, a.out)
    else:
        ap.error("use --input/--version/--out or --verify")


if __name__ == "__main__":
    main()
