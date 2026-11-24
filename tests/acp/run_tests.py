#!/usr/bin/env python3
"""Build and run the integrated chat's agent tests: the ACP client and the agent runtime.

    python -I tests/acp/run_tests.py [--quick] [--no-console] [--real] [--real-only]

  acp_client_test      pure helpers (error classification, environment block, quoting), then end to end against the
                       fake agent (tests/acp/fake_agent.cpp): start, chunks, thoughts, tools with diff, permission,
                       questions, plan, modes and config options, sign-in, crashes, garbage lines, time-out, cancel,
                       dispose, Job Object (no orphan), a 3 MiB prompt that does not block, resume, cwd with accents
  agent_runtime_test   versions and pins, hashes, inflate / zip / npm tarballs (hostile archives generated here),
                       CLI detection on fake folders with a fake CLI, launch command, the fake-agent hook and the
                       terminal sign-in; then REAL downloads of the pinned Node.js and adapters from nodejs.org and
                       registry.npmjs.org into a temporary folder, fingerprints checked, cache, refusals
  --quick              skip the real downloads (about 120 MB)
  --no-console         skip the ONE test that opens a visible console window (the terminal sign-in of
                       agent_runtime_test): for runs outside the night round. The run then says
                       « acp: 1 test skipped (--no-console) »; tests/annotation-general replays it at night
  --real               also probe the REAL adapters with the user's own claude / codex CLI: initialize + session/new in
                       an empty temporary folder, never session/prompt (nothing is consumed), once as the user is and
                       once with the CLI's configuration redirected to an empty folder (what "signed out" looks like).
                       Report: build/acp-test/real-adapters.json
  --real-only          only that probe

Everything runs with BROWSER_ANNOTATIONS_TEST=1, in build/acp-test. The real Claude / Codex configuration files are
fingerprinted before and after; any process left running from build/acp-test fails the run.
"""
import argparse
import hashlib
import io
import json
import os
import pathlib
import random
import shutil
import subprocess
import sys
import tarfile
import time
import zipfile
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / "build" / "acp-test"
TMP = OUT / "tmp"
GXX = str(ROOT / "tools" / "w64devkit" / "bin" / "g++.exe")
FLAGS = ["-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-Wno-cast-function-type", "-Wno-missing-field-initializers",
         "-static", "-municode", "-Isrc"]
CLIENT = ["src/acp-client.cpp"]
RUNTIME = ["src/agent-runtime.cpp", *CLIENT]
CLIENT_LIBS = ["-luserenv"]
RUNTIME_LIBS = ["-luserenv", "-lwinhttp", "-lbcrypt", "-lshell32"]
HOME = pathlib.Path(os.environ.get("USERPROFILE", ""))
REAL_CONFIGS = [pathlib.Path(os.environ.get("APPDATA", "")) / "Claude" / "claude_desktop_config.json",
                HOME / ".codex" / "config.toml", HOME / ".claude" / "settings.json"]
failures = []


def fail(label, detail=""):
    print(f"!! {label}" + (f": {detail}" if detail else ""), flush=True)
    failures.append(label)


def run(cmd, label, timeout=1200, env=None):
    print(f"$ {label}", flush=True)
    t = time.time()
    r = subprocess.run([str(c) for c in cmd], cwd=ROOT, timeout=timeout, capture_output=True, env=env)
    out = (r.stdout or b"").decode("utf-8", "replace") + (r.stderr or b"").decode("utf-8", "replace")
    if out.strip():
        print(out.rstrip())
    print(f"  ({time.time() - t:.1f} s)", flush=True)
    if r.returncode != 0:
        fail(label, f"exit {r.returncode}")
    return r


def compile_exe(name, sources, libs=()):
    run([GXX, *FLAGS, *sources, "-o", OUT / name, *libs], f"compile {name}")


def fingerprints():
    return {str(p): (hashlib.sha256(p.read_bytes()).hexdigest() if p.is_file() else "absent") for p in REAL_CONFIGS}


def test_env():
    env = dict(os.environ)
    env["BROWSER_ANNOTATIONS_TEST"] = "1"
    env.pop("BROWSER_CHAT_FAKE_AGENT", None)
    return env


# ---------------------------------------------------------------- fixtures
def raw_deflate(data, level=6, strategy=zlib.Z_DEFAULT_STRATEGY):
    c = zlib.compressobj(level, zlib.DEFLATED, -15, 9, strategy)
    return c.compress(data) + c.flush()


def mixed(size, seed):
    """Compressible, binary-looking data: runs, repeats from far back, and noise."""
    rng = random.Random(seed)
    out = bytearray()
    words = [bytes(rng.randrange(256) for _ in range(rng.randrange(3, 40))) for _ in range(200)]
    while len(out) < size:
        kind = rng.randrange(4)
        if kind == 0:
            out += rng.choice(words)
        elif kind == 1:
            out += bytes([rng.randrange(256)]) * rng.randrange(1, 300)
        elif kind == 2 and len(out) > 40000:
            start = len(out) - rng.randrange(1, 32768)
            out += out[start:start + rng.randrange(3, 258)]
        else:
            out += bytes(rng.randrange(256) for _ in range(rng.randrange(1, 64)))
    return bytes(out[:size])


def make_fixtures(root):
    shutil.rmtree(root, ignore_errors=True)
    (root / "inflate").mkdir(parents=True)
    (root / "zip").mkdir()
    (root / "tar").mkdir()
    rng = random.Random(7)
    text = ("Le navigateur télécharge Node et l'adaptateur, puis vérifie leurs empreintes. " * 40).encode("utf-8")
    streams = {
        "empty": raw_deflate(b""),
        "text": raw_deflate(text),
        "zeros": raw_deflate(bytes(100_000), 9),
        "random-stored": raw_deflate(bytes(rng.randrange(256) for _ in range(50_000)), 6),
        "level0-multi": raw_deflate(mixed(200_000, 1), 0),
        "text-2mb": raw_deflate(text * 700, 9),
        "fixed-huffman": raw_deflate(mixed(300_000, 2), 6, zlib.Z_FIXED),
        "huffman-only": raw_deflate(mixed(300_000, 3), 6, zlib.Z_HUFFMAN_ONLY),
        "rle": raw_deflate(mixed(300_000, 4), 6, zlib.Z_RLE),
        "fast": raw_deflate(mixed(1_000_000, 5), 1),
        "best-5mb": raw_deflate(mixed(5_500_000, 6), 9),
        "one-byte": raw_deflate(b"x"),
    }
    raws = {"empty": b"", "text": text, "zeros": bytes(100_000), "level0-multi": mixed(200_000, 1), "text-2mb": text * 700,
            "fixed-huffman": mixed(300_000, 2), "huffman-only": mixed(300_000, 3), "rle": mixed(300_000, 4), "fast": mixed(1_000_000, 5),
            "best-5mb": mixed(5_500_000, 6), "one-byte": b"x"}
    rng = random.Random(7)
    raws["random-stored"] = bytes(rng.randrange(256) for _ in range(50_000))
    for name, packed in streams.items():
        assert zlib.decompress(packed, -15) == raws[name], name
        (root / "inflate" / f"{name}.deflate").write_bytes(packed)
        (root / "inflate" / f"{name}.raw").write_bytes(raws[name])

    # zip: like the Node archive (one big deflated exe, a stored licence, other files we do not take)
    exe = mixed(6_000_000, 11)
    (root / "zip" / "node.exe.raw").write_bytes(exe)
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w") as z:
        z.writestr(zipfile.ZipInfo("top/"), b"")
        z.writestr(zipfile.ZipInfo("top/other.txt"), b"not wanted", zipfile.ZIP_DEFLATED)
        z.writestr(zipfile.ZipInfo("top/node.exe"), exe, zipfile.ZIP_DEFLATED)
        z.writestr(zipfile.ZipInfo("top/LICENSE"), b"licence text\n", zipfile.ZIP_STORED)
        z.writestr(zipfile.ZipInfo("../evil.txt"), b"never extracted: not asked for")
    good = buffer.getvalue()
    (root / "zip" / "good.zip").write_bytes(good)
    at = good.index(b"top/node.exe") + len("top/node.exe") + 5000
    (root / "zip" / "damaged.zip").write_bytes(good[:at] + bytes(b ^ 0x5A for b in good[at:at + 64]) + good[at + 64:])
    (root / "zip" / "wrongcrc.zip").write_bytes(good.replace(b"licence text\n", b"licence texT\n", 1))
    (root / "zip" / "truncated.zip").write_bytes(good[:len(good) // 2])
    (root / "zip" / "notazip.zip").write_bytes(bytes(rng.randrange(256) for _ in range(5000)))

    # npm tarballs: the same package in the three tar dialects
    long_dir = "dist/" + "/".join(f"niveau-{i:02d}-avec-un-nom-assez-long" for i in range(4))
    files = {
        "package.json": json.dumps({"name": "pkg", "version": "1.0.0"}).encode(),
        "dist/index.js": mixed(20_000, 21),
        "dist/deep/a/b/c/file.txt": b"deep\n",
        "README.md": b"",
        "docs/r\u00e9sum\u00e9.md": "accentué\n".encode("utf-8"),
        f"{long_dir}/module.js": b"export default 1\n",
        "LICENSE": b"MIT\n",
    }
    (root / "tar" / "good.json").write_text(json.dumps({k: hashlib.sha256(v).hexdigest() for k, v in files.items()}), encoding="utf-8")

    def tarball(name, fmt, top, extra=None, mutate=None, gz=True):
        raw = io.BytesIO()
        with tarfile.open(fileobj=raw, mode="w", format=fmt) as t:
            d = tarfile.TarInfo(f"{top}/emptydir")
            d.type = tarfile.DIRTYPE
            t.addfile(d)
            for rel, data in files.items():
                info = tarfile.TarInfo(f"{top}/{rel}")
                info.size = len(data)
                t.addfile(info, io.BytesIO(data))
            if extra:
                extra(t)
        data = raw.getvalue()
        if mutate:
            data = mutate(data)
        if gz:
            packed = io.BytesIO()
            import gzip
            with gzip.GzipFile(filename="package.tar", fileobj=packed, mode="wb", mtime=0) as g:
                g.write(data)
            data = packed.getvalue()
        (root / "tar" / f"{name}.tgz").write_bytes(data)
        return data

    tarball("good", tarfile.USTAR_FORMAT, "package")
    tarball("good-gnu", tarfile.GNU_FORMAT, "another-top-folder")
    tarball("good-pax", tarfile.PAX_FORMAT, "package")

    def entry(name, kind=tarfile.REGTYPE, link="", data=b"evil", pax=None):
        def add(t):
            info = tarfile.TarInfo(name)
            info.type = kind
            info.linkname = link
            if pax:
                info.pax_headers = pax
            if kind == tarfile.REGTYPE:
                info.size = len(data)
                t.addfile(info, io.BytesIO(data))
            else:
                t.addfile(info)
        return add

    hostile = {
        "dotdot": entry("package/../evil.txt"),
        "dotdot-deep": entry("package/a/../../../evil.txt"),
        "absolute": entry("/evil.txt"),
        "unc": entry("//server/share/evil.txt"),
        "drive": entry("package/C:/evil.txt"),
        "stream": entry("package/file.txt:evil"),
        "backslash": entry("package/..\\..\\evil.txt"),
        "device": entry("package/NUL.txt"),
        "device-bare": entry("package/sub/con"),
        "trailing-dot": entry("package/evil.txt."),
        "trailing-space": entry("package/evil.txt "),
        "control": entry("package/ev\x01il.txt"),
        "symlink": entry("package/link", tarfile.SYMTYPE, "../../evil.txt"),
        "hardlink": entry("package/hard", tarfile.LNKTYPE, "package/package.json"),
        "fifo": entry("package/pipe", tarfile.FIFOTYPE),
        "chardev": entry("package/dev", tarfile.CHRTYPE),
    }
    for name, extra in hostile.items():
        tarball(f"bad-{name}", tarfile.GNU_FORMAT, "package", extra)
    tarball("bad-pax-path", tarfile.PAX_FORMAT, "package", entry("package/innocent.txt", pax={"path": "package/../../evil.txt"}))
    tarball("bad-pax-linkpath", tarfile.PAX_FORMAT, "package", entry("package/innocent.txt", pax={"linkpath": "../../evil.txt"}))
    tarball("bad-header-checksum", tarfile.USTAR_FORMAT, "package", mutate=lambda d: d[:600] + bytes([d[600] ^ 1]) + d[601:])
    tarball("bad-not-gzip", tarfile.USTAR_FORMAT, "package", gz=False)
    whole = (root / "tar" / "good.tgz").read_bytes()
    (root / "tar" / "bad-truncated.tgz").write_bytes(whole[:len(whole) // 2])
    (root / "tar" / "bad-gzip-crc.tgz").write_bytes(whole[:-8] + bytes([whole[-8] ^ 1]) + whole[-7:])
    (root / "tar" / "bad-gzip-size.tgz").write_bytes(whole[:-4] + bytes([whole[-4] ^ 1]) + whole[-3:])
    (root / "tar" / "bad-deflate.tgz").write_bytes(whole[:200] + bytes(b ^ 0xFF for b in whole[200:260]) + whole[260:])
    empty = io.BytesIO()
    with tarfile.open(fileobj=empty, mode="w:gz"):
        pass
    (root / "tar" / "bad-empty.tgz").write_bytes(empty.getvalue())


# ---------------------------------------------------------------- real adapters (no prompt)
def tree_state(root):
    """Names, sizes and times under a folder (never the contents): what changed is reported, nothing is read."""
    state = {}
    if not root.is_dir():
        return state
    for dirpath, _dirs, names in os.walk(root):
        for n in names:
            p = pathlib.Path(dirpath) / n
            try:
                s = p.stat()
                state[str(p.relative_to(root))] = (s.st_size, int(s.st_mtime))
            except OSError:
                pass
    return state


def changed_top_level(before, after):
    changed = {k for k in after if before.get(k) != after[k]} | {k for k in before if k not in after}
    return sorted({pathlib.PurePath(k).parts[0] for k in changed})


def real_probe(env):
    data = OUT / "real-data"          # kept between runs: the downloads are cached and re-verified by size
    report = {}
    for agent in ("claude", "codex"):
        home = HOME / (".claude" if agent == "claude" else ".codex")
        before = tree_state(home)
        for variant in ("as-user", "signed-out"):
            project = TMP / "real" / f"{agent}-{variant}-project"
            shutil.rmtree(project, ignore_errors=True)
            project.mkdir(parents=True)
            out = OUT / f"real-{agent}-{variant}.json"
            out.unlink(missing_ok=True)
            cmd = [OUT / "real_adapter_probe.exe", data, agent, project, out]
            if variant == "signed-out":
                redirect = TMP / "real" / f"{agent}-empty-config"
                shutil.rmtree(redirect, ignore_errors=True)
                cmd += ["--signed-out", redirect]
            run(cmd, f"real adapter probe: {agent} ({variant}), no prompt", timeout=400, env=env)
            if out.is_file():
                report[f"{agent}-{variant}"] = json.loads(out.read_text(encoding="utf-8"))
        report[f"{agent}-home-changes"] = changed_top_level(before, tree_state(home))
    (OUT / "real-adapters.json").write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"real adapter report: {OUT / 'real-adapters.json'}")
    for key, value in report.items():
        if key.endswith("-home-changes"):
            print(f"  {key}: {value if value else 'nothing'}")
            continue
        start = value.get("start", {})
        print(f"  {key}: cli {value['cli']['state']} {value['cli']['version']} login {value['cli']['login']} -> "
              f"{start.get('state', value.get('result'))} in {start.get('ms', '-')} ms; prompts sent: {value.get('promptsSent', 0)}")
        if value.get("promptsSent", 0) != 0:
            fail("real probe sent a prompt")


def leftover_processes():
    script = ("$p = Get-CimInstance Win32_Process | Where-Object { $_.ExecutablePath -and $_.ExecutablePath.StartsWith('" + str(OUT).replace("'", "''") +
              "', [System.StringComparison]::OrdinalIgnoreCase) }; $p | ForEach-Object { $_.ProcessId.ToString() + ' ' + $_.ExecutablePath }")
    r = subprocess.run(["powershell", "-NoProfile", "-Command", script], capture_output=True, text=True, timeout=120)
    return [line for line in r.stdout.splitlines() if line.strip()]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--quick", action="store_true")
    parser.add_argument("--no-console", action="store_true")
    parser.add_argument("--real", action="store_true")
    parser.add_argument("--real-only", action="store_true")
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    before = fingerprints()
    env = test_env()
    try:
        compile_exe("fake-acp-agent.exe", ["tests/acp/fake_agent.cpp"])
        if not args.real_only:
            compile_exe("fake-cli.exe", ["tests/acp/fake_cli.cpp"])
            compile_exe("acp_client_test.exe", ["tests/acp/acp_client_test.cpp", *CLIENT], CLIENT_LIBS)
            compile_exe("model_probe_test.exe", ["tests/acp/model_probe_test.cpp", *CLIENT], CLIENT_LIBS)   # #128 v2
            compile_exe("agent_runtime_test.exe", ["tests/acp/agent_runtime_test.cpp", *RUNTIME], RUNTIME_LIBS)
            # « Ton agent » (design/ton-agent-interface.md): the one status + the official installers, on fakes only.
            compile_exe("agent_status_test.exe", ["tests/acp/agent_status_test.cpp", "src/agent-install.cpp", *RUNTIME], [*RUNTIME_LIBS, "-lpsapi"])
        if args.real or args.real_only:
            compile_exe("real_adapter_probe.exe", ["tests/acp/real_adapter_probe.cpp", *RUNTIME], RUNTIME_LIBS)
        if failures:
            return 1
        if not args.real_only:
            make_fixtures(TMP / "fixtures")
            run([OUT / "acp_client_test.exe", OUT / "fake-acp-agent.exe", TMP / "client"], "acp_client_test", env=env)
            run([OUT / "model_probe_test.exe", OUT / "fake-acp-agent.exe", TMP / "probe", ROOT], "model_probe_test", env=env)   # #128 v2: an agent's models read before its first chat
            cmd = [OUT / "agent_runtime_test.exe", OUT / "fake-acp-agent.exe", OUT / "fake-cli.exe", TMP / "runtime", TMP / "fixtures"]
            run(cmd + ([] if args.quick else ["--network"]) + (["--no-console"] if args.no_console else []),
                "agent_runtime_test" + (" (no network)" if args.quick else " (with real downloads)") + (" (no console)" if args.no_console else ""), env=env)
            if args.no_console:
                print("acp: 1 test skipped (--no-console): the terminal sign-in, which opens a visible console")
            run([OUT / "agent_status_test.exe", OUT / "fake-cli.exe", TMP / "status", ROOT / "sandbox" / "agents" / "fake-install.ps1"], "agent_status_test", env=env)
        if args.real or args.real_only:
            real_probe(env)
    finally:
        time.sleep(0.5)
        left = leftover_processes()
        if left:
            fail("processes left running", "; ".join(left))
            for line in left:
                subprocess.run(["taskkill", "/F", "/PID", line.split()[0]], capture_output=True)
        else:
            print("no process left running from build/acp-test")
        after = fingerprints()
        for path, digest in before.items():
            same = after[path] == digest
            print(f"{'unchanged' if same else 'CHANGED  '} {digest[:16]}… {path}")
            if not same:
                fail("real configuration changed", path)
    print("FAILED: " + ", ".join(failures) if failures else "ALL ACP TESTS PASSED")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
