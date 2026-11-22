#!/usr/bin/env python3
"""Build and run the change-snapshot tests (chat V1, contract C). No WebView2, no network, no git, no agent.

    python -I tests/snapshots/run_tests.py [--quick] [--files N] [--budget-ms N]

  snapshot_test    created / modified / deleted / renamed, binary, CRLF / LF, encodings, big file left out,
                   .gitignore (root and nested), default exclusions, secrets, links never followed, conflicts,
                   restore across several turns, purge (count and size), a cut in the middle of a restore
                   (safe resume), interrupted turn, two stores on one project. Temporary folders only.
  snapshot_bench   a generated project of 5 000 kept files (+ 3 700 ignored): first snapshot, warm reading
                   (budget: 300 ms), a turn, a diff, an undo, a purge. Fails when the warm reading is over budget.

--quick      skip snapshot_bench
--files N    size of the generated project (default 5000)
--budget-ms  budget of the warm reading (default 300)

Everything is compiled with -Wall -Wextra -Werror. Outputs go to build/chat-snapshots-test; the temporary
projects live under %TEMP%\\browser-snapshots-test-<pid> and are removed. The user's real Claude / Codex
configuration files are fingerprinted before and after: any change fails the run.
Exit code is non-zero if any build or test fails.
"""
import argparse
import hashlib
import os
import pathlib
import subprocess
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / "build" / "chat-snapshots-test"
GXX = str(ROOT / "tools" / "w64devkit" / "bin" / "g++.exe")
FLAGS = ["-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-static", "-Isrc"]
SOURCES = ["src/change-snapshots.cpp"]
LIBS = ["-lbcrypt"]
failures = []


def fail(label, detail=""):
    print(f"!! {label}" + (f": {detail}" if detail else ""), flush=True)
    failures.append(label)


def run(cmd, label, timeout=900):
    print(f"$ {label}", flush=True)
    started = time.time()
    env = dict(os.environ, BROWSER_ANNOTATIONS_TEST="1")
    r = subprocess.run([str(c) for c in cmd], cwd=ROOT, timeout=timeout, capture_output=True, env=env)
    out = (r.stdout or b"").decode("utf-8", "replace") + (r.stderr or b"").decode("utf-8", "replace")
    out = out.replace("\r\n", "\n")
    if out.strip():
        print(out.rstrip())
    print(f"  ({time.time() - started:.1f} s)", flush=True)
    if r.returncode != 0:
        fail(label, f"exit {r.returncode}")
    return r


def real_configs():
    home = pathlib.Path(os.environ.get("USERPROFILE", str(pathlib.Path.home())))
    appdata = pathlib.Path(os.environ.get("APPDATA", str(home / "AppData" / "Roaming")))
    return [appdata / "Claude" / "claude_desktop_config.json", home / ".codex" / "config.toml",
            home / ".claude" / "settings.json"]


def fingerprint():
    lines = []
    for path in real_configs():
        try:
            data = path.read_bytes()
            stat = path.stat()
            lines.append(f"{path} {len(data)} {stat.st_mtime_ns} {hashlib.sha256(data).hexdigest()}")
        except OSError:
            lines.append(f"{path} absent")
    return "\n".join(lines)


def leftovers():
    base = pathlib.Path(tempfile.gettempdir())
    return sorted(p.name for p in base.glob("browser-snapshots-test-*"))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--quick", action="store_true")
    parser.add_argument("--files", type=int, default=5000)
    parser.add_argument("--budget-ms", type=int, default=300)
    args = parser.parse_args()
    if not pathlib.Path(GXX).exists():
        print(f"Missing {GXX}: run setup.ps1 first.")
        return 2
    OUT.mkdir(parents=True, exist_ok=True)
    before = fingerprint()
    stale = set(leftovers())

    built = run([GXX, *FLAGS, "tests/snapshots/snapshot_test.cpp", *SOURCES, "-o", OUT / "snapshot_test.exe", *LIBS],
                "compile snapshot_test")
    if built.returncode == 0:
        run([OUT / "snapshot_test.exe"], "snapshot_test")
    if not args.quick:
        built = run([GXX, *FLAGS, "tests/snapshots/snapshot_bench.cpp", *SOURCES, "-o", OUT / "snapshot_bench.exe", *LIBS],
                    "compile snapshot_bench")
        if built.returncode == 0:
            run([OUT / "snapshot_bench.exe", str(args.files), "--budget-ms", str(args.budget_ms)], "snapshot_bench")

    left = [name for name in leftovers() if name not in stale]
    if left:
        fail("temporary folders left behind", ", ".join(left))
    after = fingerprint()
    print("real Claude / Codex configuration files (before == after):" if before == after else "BEFORE:\n" + before + "\nAFTER:")
    print(after)
    if before != after:
        fail("the REAL Claude / Codex configuration changed during the tests")

    if failures:
        print(f"\nFAILED: {len(failures)} step(s): " + "; ".join(failures))
        return 1
    print("\nAll change-snapshot tests passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
