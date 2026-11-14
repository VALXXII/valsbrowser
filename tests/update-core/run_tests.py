#!/usr/bin/env python3
"""Build and run the update-core native test suite (no WebView2, no network, isolated temp roots).

    python -I tests/update-core/run_tests.py

Outputs go to build/update-core. Exit code is non-zero if any build or test fails.
"""
import os
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / "build" / "update-core"
GXX = str(ROOT / "tools" / "w64devkit" / "bin" / "g++.exe")
FLAGS = ["-std=c++17", "-O2", "-Wall", "-Wextra", "-Wno-cast-function-type", "-Wno-missing-field-initializers",
         "-static", "-municode", "-Isrc"]
CORE = ["src/update-core.cpp", "src/update-package.cpp"]
LIBS = ["-lwinhttp", "-lbcrypt"]
failures = []


def run(cmd, label, expect=0, timeout=600):
    print(f"$ {label}", flush=True)
    r = subprocess.run(cmd, cwd=ROOT, timeout=timeout, capture_output=True, text=True)
    out = (r.stdout or "") + (r.stderr or "")
    if out.strip():
        print(out.rstrip())
    if r.returncode != expect:
        print(f"!! {label}: exit {r.returncode}, expected {expect}")
        failures.append(label)
    return r.returncode


def compile_exe(name, sources, extra=()):
    run([GXX, *FLAGS, *extra, *sources, "-o", str(OUT / name), *LIBS], f"compile {name}")


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    # Fixture helper: same source, plus test-only fault points (BROWSER_HELPER_FAULT, see src/update-helper.cpp).
    compile_exe("update-helper.exe", ["src/update-helper.cpp", "src/update-package.cpp"], ["-mwindows", "-DUPDATE_HELPER_FAULTS"])
    # Real MCP bridges (src/mcp-bridge.cpp) with distinct bytes per version: live agent processes in install_test.
    for version in ("0.3.1", "0.3.2", "0.3.3"):
        compile_exe(f"bridge-{version}.exe", ["src/mcp-bridge.cpp", "src/annotations.cpp", *CORE], [f'-DBROWSER_VERSION_TEXT="{version}"'])
    for name, version, crash in [("fake-0.3.1.exe", "0.3.1", 0), ("fake-0.3.2.exe", "0.3.2", 0), ("fake-0.3.2-crash.exe", "0.3.2", 1),
                                 ("fake-0.3.3.exe", "0.3.3", 0)]:
        compile_exe(name, ["tests/update-core/fake_browser.cpp", *CORE],
                    ["-mwindows", f'-DFAKE_VERSION=L"{version}"', f"-DFAKE_CRASH={crash}"])
    compile_exe("update_core_test.exe", ["tests/update-core/update_core_test.cpp", *CORE])
    compile_exe("install_test.exe", ["tests/update-core/install_test.cpp", *CORE])
    compile_exe("interop_check.exe", ["tests/update-core/interop_check.cpp", "src/update-package.cpp"])
    if failures:
        return finish()
    run([str(OUT / "update_core_test.exe")], "update_core_test")
    run([str(OUT / "install_test.exe"), str(OUT)], "install_test")

    # Python packer <-> C++ reader interop and payload policy.
    tmp = OUT / "tmp" / "packer"
    shutil.rmtree(tmp, ignore_errors=True)
    payload = tmp / "payload"
    (payload / "assets" / "fonts").mkdir(parents=True)
    shutil.copy(OUT / "fake-0.3.2.exe", payload / "browser.exe")
    shutil.copy(OUT / "update-helper.exe", payload / "update-helper.exe")
    (payload / "WebView2Loader.dll").write_bytes(b"loader")
    (payload / "ThorVG-LICENSE.txt").write_text("license")
    (payload / "build-info.txt").write_text("0.3.2\n")
    (payload / "assets" / "fonts" / "Inter Display (1).ttf").write_bytes(b"font")
    packer = [sys.executable, "-I", "tools/update-package/make_update_package.py"]
    run(packer + ["--input", str(payload), "--version", "0.3.2", "--out", str(tmp / "out")], "packer build")
    pkg, manifest = tmp / "out" / "browser-0.3.2-win-x64.lsrupd", tmp / "out" / "update-manifest-win-x64.txt"
    run(packer + ["--verify", str(pkg), "--manifest", str(manifest)], "packer verify")
    (tmp / "stage").mkdir()
    run([str(OUT / "interop_check.exe"), str(pkg), str(manifest), str(tmp / "stage")], "C++ reads Python package")
    first = pkg.read_bytes()
    run(packer + ["--input", str(payload), "--version", "0.3.2", "--out", str(tmp / "out2")], "packer rebuild")
    if (tmp / "out2" / pkg.name).read_bytes() != first:
        print("!! packer output is not reproducible")
        failures.append("packer reproducible")
    for bad, label in [("profile/Cookies", "profile"), ("session", "session"), ("prototype.log", "log"), ("browser-res.o", "object")]:
        target = payload / bad
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(b"x")
        run(packer + ["--input", str(payload), "--version", "0.3.2", "--out", str(tmp / "bad")], f"packer rejects {label}", expect=1)
        target.unlink()
    run(packer + ["--input", str(payload), "--version", "0.0.0", "--out", str(tmp / "bad")], "packer rejects 0.0.0", expect=1)
    corrupt = tmp / "corrupt.lsrupd"
    corrupt.write_bytes(first[:-1])
    run(packer + ["--verify", str(corrupt)], "packer verify rejects truncated", expect=1)
    return finish()


def finish():
    if failures:
        print(f"FAILED: {failures}")
        return 1
    print("update-core: all builds and tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
