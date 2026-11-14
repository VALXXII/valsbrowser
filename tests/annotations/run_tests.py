#!/usr/bin/env python3
"""Build and run the annotation store + MCP bridge test suite (no WebView2, no network, isolated temp stores).

    python -I tests/annotations/run_tests.py [--bridge <exe>] [--quick] [--update-golden]

  store_test        ids, schemas, bounds, hostile paths, corrupt files, drafts, leases, retention (simulated clock),
                    pagination, projects, readers, Markdown, caps, store location parity with UpdateCore
  json_fuzz         strict JSON vectors, bounds, 300k mutations, random round trips
  json differential the C++ parser against Python's json module on generated + mutated documents
  claim_race        2 processes x 1000 annotations (fresh and expired leases), 8 threads x 1000
  transcripts       stdin -> expected stdout replays (legacy initialize 2025-11-25, modern _meta 2026-07-28,
                    --project), tools/list determinism (+ golden file), image bytes, stdout strictly JSON-RPC,
                    readers\\<client>.json, idle CPU, exit on stdin close

--bridge   test this browser-annotations-mcp.exe (e.g. build/annotations-preview/...) instead of a fresh test build
--quick    skip claim_race (the slow 1000-annotation race)
Outputs go to build/annotations-test. Exit code is non-zero if any build or test fails.
"""
import argparse
import base64
import ctypes
import json
import pathlib
import queue
import random
import re
import shutil
import subprocess
import sys
import threading
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
HERE = pathlib.Path(__file__).resolve().parent
OUT = ROOT / "build" / "annotations-test"
GXX = str(ROOT / "tools" / "w64devkit" / "bin" / "g++.exe")
FLAGS = ["-std=c++17", "-O2", "-Wall", "-Wextra", "-Wno-cast-function-type", "-Wno-missing-field-initializers",
         "-static", "-Isrc"]
STORE = ["src/annotations.cpp"]
failures = []


def fail(label, detail=""):
    print(f"!! {label}" + (f": {detail}" if detail else ""), flush=True)
    failures.append(label)


def run(cmd, label, expect=0, timeout=900, stdin_data=None):
    print(f"$ {label}", flush=True)
    t = time.time()
    r = subprocess.run([str(c) for c in cmd], cwd=ROOT, timeout=timeout, capture_output=True, input=stdin_data)
    out = (r.stdout or b"").decode("utf-8", "replace") + (r.stderr or b"").decode("utf-8", "replace")
    if out.strip() and stdin_data is None:
        print(out.rstrip())
    print(f"  ({time.time() - t:.1f} s)")
    if r.returncode != expect:
        fail(label, f"exit {r.returncode}, expected {expect}")
    return r


def compile_exe(name, sources, extra=(), libs=("-lbcrypt",)):
    run([GXX, *FLAGS, *extra, *sources, "-o", OUT / name, *libs], f"compile {name}")


# ---------------------------------------------------------------- JSON differential
def py_strict(raw: bytes):
    """Python's verdict with the same strictness: UTF-8, no duplicate keys, no NaN/Infinity, finite numbers.
    Returns ('ok', value) | ('err', None) | ('skip', None) for cases Python cannot judge like C++ does."""
    try:
        text = raw.decode("utf-8", "strict")
    except UnicodeDecodeError:
        return "err", None

    def no_dups(pairs):
        keys = [k for k, _ in pairs]
        if len(keys) != len(set(keys)):
            raise ValueError("dup")
        return dict(pairs)

    def no_constant(name):
        raise ValueError(name)

    try:
        value = json.loads(text, object_pairs_hook=no_dups, parse_constant=no_constant)
    except (ValueError, RecursionError):
        return "err", None

    def walk(v):
        if isinstance(v, float) and (v != v or v in (float("inf"), float("-inf"))):
            raise OverflowError
        if isinstance(v, str) and any(0xD800 <= ord(c) <= 0xDFFF for c in v):
            raise LookupError  # lone surrogate escapes: Python keeps them, C++ refuses them by design
        if isinstance(v, dict):
            for k, x in v.items():
                walk(k)
                walk(x)
        if isinstance(v, list):
            for x in v:
                walk(x)

    try:
        walk(value)
    except OverflowError:
        return "err", None
    except LookupError:
        return "skip", None
    return "ok", value


def same(a, b):
    if isinstance(a, bool) or isinstance(b, bool):
        return a is b
    if isinstance(a, (int, float)) and isinstance(b, (int, float)):
        try:
            return float(a) == float(b)
        except OverflowError:
            return True
    if isinstance(a, dict) and isinstance(b, dict):
        return a.keys() == b.keys() and all(same(a[k], b[k]) for k in a)
    if isinstance(a, list) and isinstance(b, list):
        return len(a) == len(b) and all(same(x, y) for x, y in zip(a, b))
    return a == b


def random_json(rng, depth=0):
    kind = rng.randrange(7 if depth < 6 else 4)
    if kind == 0:
        return rng.choice([None, True, False])
    if kind == 1:
        return rng.randint(-2 ** 53, 2 ** 53)
    if kind == 2:
        return rng.choice([0.5, -1.25e-7, 3.14159, 1e300, -2.5e-300, 123456.789])
    if kind == 3:
        return "".join(rng.choice(["a", "é", "😀", "\"", "\\", "\n", "\x01", "\u2028", "/", " ", "\x7f"]) for _ in range(rng.randrange(8)))
    if kind in (4, 5):
        return [random_json(rng, depth + 1) for _ in range(rng.randrange(5))]
    return {f"k{rng.randrange(20)}{'é' if rng.random() < .3 else ''}": random_json(rng, depth + 1) for _ in range(rng.randrange(5))}


def mutate(rng, data: bytes) -> bytes:
    b = bytearray(data)
    for _ in range(rng.randrange(1, 4)):
        if not b:
            break
        i = rng.randrange(len(b))
        op = rng.randrange(5)
        pick = rng.choice(b'{}[]",:\\0123456789eE+-. \t\n\x00\x01\x80\xc0\xed\xef\xf0\xff')
        if op == 0:
            b[i] ^= 1 << rng.randrange(8)
        elif op == 1:
            b.insert(i, pick)
        elif op == 2:
            del b[i:i + rng.randrange(1, 4)]
        elif op == 3:
            del b[i:]
        else:
            b[i] = pick
    return bytes(b)


def json_differential(exe):
    rng = random.Random(20261114)
    docs = []
    for _ in range(4000):
        v = random_json(rng)
        text = json.dumps(v, ensure_ascii=rng.random() < .5, separators=rng.choice([(",", ":"), (", ", ": ")]))
        raw = (rng.choice(["", " ", "\n", "\t\r\n"]) + text + rng.choice(["", " ", "\n"])).encode("utf-8")
        docs.append(raw)
        docs.append(mutate(rng, raw))
        docs.append(mutate(rng, raw))
    docs += [b"1e400", b'"\\ud800"', b"\xef\xbb\xbf{}", b'{"a":1,"a":2}', b"NaN", b"[1,]", b"01", b'"\xc0\xaf"', b"\x00"]
    payload = b"\n".join(d.hex().encode() for d in docs) + b"\n"
    r = subprocess.run([str(exe), "--check"], input=payload, capture_output=True, timeout=300)
    lines = r.stdout.decode("utf-8").splitlines()
    if len(lines) != len(docs):
        fail("json differential", f"{len(lines)} answers for {len(docs)} documents")
        return
    agree = skipped = 0
    disagreements = []
    for raw, line in zip(docs, lines):
        verdict, value = py_strict(raw)
        if verdict == "skip":
            skipped += 1
            continue
        cpp_ok = line.startswith("OK ")
        if (verdict == "ok") != cpp_ok:
            disagreements.append((raw[:80], verdict, line[:80]))
            continue
        if cpp_ok:
            canonical = line[3:]
            if "\n" in canonical or not same(json.loads(canonical), value):
                disagreements.append((raw[:80], "value", canonical[:80]))
                continue
        agree += 1
    accepted = sum(1 for l in lines if l.startswith("OK "))
    print(f"json differential: {len(docs)} documents, {agree} agree with Python, {skipped} skipped (lone surrogates), "
          f"{accepted} accepted, {len(disagreements)} disagreements")
    for d in disagreements[:10]:
        print("   ", d)
    if disagreements:
        fail("json differential", f"{len(disagreements)} disagreements")


# ---------------------------------------------------------------- MCP transcripts
class Mismatch(Exception):
    pass


def substitute(value, captures, meta):
    if isinstance(value, str):
        if value == "$META":
            return meta
        return re.sub(r"\$\{(\w+)\}", lambda m: captures[m.group(1)], value)
    if isinstance(value, list):
        return [substitute(v, captures, meta) for v in value]
    if isinstance(value, dict):
        return {k: substitute(v, captures, meta) for k, v in value.items()}
    return value


def match(expected, actual, captures, path="$"):
    if isinstance(expected, str):
        if expected == "$ANY":
            return
        if expected.startswith("$CAPTURE:"):
            captures[expected[9:]] = actual
            return
        if expected.startswith("$CONTAINS:"):
            if not isinstance(actual, str) or expected[10:] not in actual:
                raise Mismatch(f"{path}: {actual!r} does not contain {expected[10:]!r}")
            return
        if expected.startswith("$RE:"):
            if not isinstance(actual, str) or not re.fullmatch(expected[4:], actual):
                raise Mismatch(f"{path}: {actual!r} does not match /{expected[4:]}/")
            return
        expected = re.sub(r"\$\{(\w+)\}", lambda m: captures[m.group(1)], expected)
    if isinstance(expected, dict):
        if not isinstance(actual, dict):
            raise Mismatch(f"{path}: expected an object, got {actual!r}")
        keys = [k for k in expected if k != "$EXACT"]
        for k in keys:
            if expected[k] == "$ABSENT":
                if k in actual:
                    raise Mismatch(f"{path}.{k}: must be absent")
                continue
            if k not in actual:
                raise Mismatch(f"{path}.{k}: missing")
            match(expected[k], actual[k], captures, f"{path}.{k}")
        if expected.get("$EXACT") and set(actual) != set(keys):
            raise Mismatch(f"{path}: keys {sorted(actual)} != {sorted(keys)}")
        return
    if isinstance(expected, list):
        if not isinstance(actual, list) or len(actual) != len(expected):
            raise Mismatch(f"{path}: expected {len(expected)} items, got {actual!r}"[:300])
        for i, (e, a) in enumerate(zip(expected, actual)):
            match(e, a, captures, f"{path}[{i}]")
        return
    if isinstance(expected, bool) or isinstance(actual, bool):
        if expected is not actual:
            raise Mismatch(f"{path}: {actual!r} != {expected!r}")
        return
    if expected != actual:
        raise Mismatch(f"{path}: {actual!r} != {expected!r}")


def check_jsonrpc_line(line: bytes, label):
    """stdout must carry only MCP messages: one JSON-RPC 2.0 response per line, UTF-8, no embedded newline."""
    try:
        msg = json.loads(line.decode("utf-8"))
    except Exception as e:  # noqa: BLE001
        raise Mismatch(f"{label}: stdout line is not JSON: {line[:120]!r} ({e})")
    if not isinstance(msg, dict) or msg.get("jsonrpc") != "2.0" or "id" not in msg or (("result" in msg) == ("error" in msg)):
        raise Mismatch(f"{label}: not a JSON-RPC 2.0 response: {line[:200]!r}")
    if "error" in msg and not (isinstance(msg["error"].get("code"), int) and isinstance(msg["error"].get("message"), str)):
        raise Mismatch(f"{label}: malformed error object")
    return msg


class Bridge:
    def __init__(self, exe, store, args):
        self.p = subprocess.Popen([str(exe), "--store", str(store), *args], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE)
        self.lines = queue.Queue()
        self.stderr = []
        threading.Thread(target=self._pump, args=(self.p.stdout, self.lines), daemon=True).start()
        threading.Thread(target=lambda: self.stderr.extend(self.p.stderr), daemon=True).start()

    @staticmethod
    def _pump(stream, q):
        for line in stream:
            q.put(line)
        q.put(None)

    def send(self, raw: bytes):
        self.p.stdin.write(raw + b"\n")
        self.p.stdin.flush()

    def read(self, timeout=20):
        line = self.lines.get(timeout=timeout)
        if line is None:
            raise Mismatch("bridge closed stdout")
        if not line.endswith(b"\n"):
            raise Mismatch("stdout line not newline-terminated")
        return line[:-1]

    def close(self, timeout=10):
        self.p.stdin.close()
        rest = []
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                line = self.lines.get(timeout=max(0.1, deadline - time.time()))
            except queue.Empty:
                break
            if line is None:
                break
            rest.append(line)
        try:
            code = self.p.wait(timeout=max(0.1, deadline - time.time()))
        except subprocess.TimeoutExpired:
            self.p.kill()
            code = None
        return rest, code


def fresh_store(fixture, name):
    store = OUT / "tmp" / "bridge" / name / "annotations"
    shutil.rmtree(store.parent, ignore_errors=True)
    store.parent.mkdir(parents=True)
    r = subprocess.run([str(fixture), str(store)], capture_output=True, timeout=60)
    if r.returncode != 0:
        raise Mismatch(f"fixture failed: {r.stdout!r} {r.stderr!r}")
    return store


def play(exe, fixture, path: pathlib.Path, tool_lists):
    spec = json.loads(path.read_text(encoding="utf-8"))
    label = f"transcript {path.stem} [{exe.parent.name}]"
    store = fresh_store(fixture, path.stem)
    bridge = Bridge(exe, store, spec.get("args", []))
    captures, meta = {}, spec.get("meta", {})
    transcript = []
    try:
        for n, step in enumerate(spec["steps"]):
            raw = step["raw"].encode("utf-8") if "raw" in step else json.dumps(substitute(step["send"], captures, meta), ensure_ascii=False).encode("utf-8")
            bridge.send(raw)
            transcript.append(b"> " + raw)
            if step["expect"] is None:
                continue
            line = bridge.read()
            transcript.append(b"< " + line)
            msg = check_jsonrpc_line(line, f"{label} step {n}")
            match(step["expect"], msg, captures, f"step {n}")
            if step.get("capture_tools"):
                tool_lists.append(json.dumps(msg["result"]["tools"], ensure_ascii=False, sort_keys=False))
        rest, code = bridge.close()
        if rest:
            raise Mismatch(f"unexpected extra stdout after the last step: {rest[:3]!r}")
        if code != 0:
            raise Mismatch(f"bridge exit code {code} after stdin closed")
        after = spec.get("after", {})
        if "png_capture" in after:
            png = base64.b64decode(captures[after["png_capture"]], validate=True)
            if png != (store / after["png_file"]).read_bytes() or not png.startswith(b"\x89PNG\r\n\x1a\n"):
                raise Mismatch("image content differs from shot.png")
        if "reader" in after:
            want = after["reader"]
            data = json.loads((store / want["file"]).read_text(encoding="utf-8"))
            if data.get("client") != want["client"] or data.get("label") != want["label"] or \
                    data.get("project") != want.get("project") or not re.fullmatch(r"\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ", data.get("lastReadAt", "")):
                raise Mismatch(f"readers file {data}")
        stderr = b"".join(bridge.stderr).decode("utf-8", "replace")
        if "started" not in stderr:
            raise Mismatch("expected diagnostics on stderr")
        print(f"ok {label}: {len(spec['steps'])} steps")
    except (Mismatch, queue.Empty, KeyError) as e:
        if bridge.p.poll() is None:
            bridge.p.kill()
        fail(label, str(e) or type(e).__name__)
    log = OUT / f"transcript-{path.stem}-{exe.parent.name}.log"
    log.write_bytes(b"\n".join(transcript) + b"\n")


def idle_and_shutdown(exe, fixture):
    """No polling: an idle bridge uses no CPU; closing stdin ends it promptly."""
    label = f"idle/shutdown [{exe.parent.name}]"
    store = fresh_store(fixture, "idle")
    bridge = Bridge(exe, store, [])
    try:
        bridge.send(json.dumps({"jsonrpc": "2.0", "id": 1, "method": "initialize",
                                "params": {"protocolVersion": "2025-11-25", "capabilities": {}, "clientInfo": {"name": "idle"}}}).encode())
        bridge.read()
        time.sleep(0.3)

        class FILETIME(ctypes.Structure):
            _fields_ = [("lo", ctypes.c_uint32), ("hi", ctypes.c_uint32)]

        def cpu_ms():
            c, e, k, u = FILETIME(), FILETIME(), FILETIME(), FILETIME()
            ctypes.windll.kernel32.GetProcessTimes(ctypes.c_void_p(int(bridge.p._handle)), ctypes.byref(c), ctypes.byref(e), ctypes.byref(k), ctypes.byref(u))
            return ((k.hi << 32 | k.lo) + (u.hi << 32 | u.lo)) / 10000

        before = cpu_ms()
        time.sleep(3)
        used = cpu_ms() - before
        t = time.time()
        rest, code = bridge.close()
        elapsed = time.time() - t
        print(f"{label}: {used:.1f} ms CPU over 3 s idle, exit {code} {elapsed * 1000:.0f} ms after stdin closed")
        if used > 20:
            fail(label, f"idle CPU {used:.1f} ms")
        if code != 0 or elapsed > 2 or rest:
            fail(label, f"exit {code} after {elapsed:.1f} s, extra {rest!r}")
    except (Mismatch, queue.Empty) as e:
        bridge.p.kill()
        fail(label, str(e))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bridge", action="append", default=[], help="bridge executable to test (repeatable)")
    ap.add_argument("--quick", action="store_true")
    ap.add_argument("--update-golden", action="store_true")
    a = ap.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    compile_exe("store_test.exe", ["tests/annotations/store_test.cpp", *STORE, "src/update-core.cpp", "src/update-package.cpp"],
                ["-municode"], ["-lbcrypt", "-lwinhttp"])
    compile_exe("json_fuzz.exe", ["tests/annotations/json_fuzz.cpp"], [], [])
    compile_exe("bridge_fixture.exe", ["tests/annotations/bridge_fixture.cpp", *STORE], ["-municode"])
    compile_exe("browser-annotations-mcp.exe", ["src/mcp-bridge.cpp", *STORE], ["-municode"])
    if not a.quick:
        compile_exe("claim_race.exe", ["tests/annotations/claim_race.cpp", *STORE], ["-municode"])
    if failures:
        return finish()
    run([OUT / "store_test.exe"], "store_test")
    run([OUT / "json_fuzz.exe"], "json_fuzz")
    json_differential(OUT / "json_fuzz.exe")
    if not a.quick:
        run([OUT / "claim_race.exe"], "claim_race")

    bridges = [OUT / "browser-annotations-mcp.exe"] + [pathlib.Path(b).resolve() for b in a.bridge]
    fixture = OUT / "bridge_fixture.exe"
    golden = HERE / "golden" / "tools-list.json"
    for exe in bridges:
        if not exe.is_file():
            fail(f"bridge {exe}", "missing")
            continue
        tool_lists = []
        for path in sorted((HERE / "transcripts").glob("*.json")):
            play(exe, fixture, path, tool_lists)
        idle_and_shutdown(exe, fixture)
        # tools/list is deterministic across eras, runs and processes, and matches the reviewed golden copy.
        if len(set(tool_lists)) != 1:
            fail(f"tools/list determinism [{exe.parent.name}]", f"{len(set(tool_lists))} variants")
        elif a.update_golden:
            golden.parent.mkdir(exist_ok=True)
            golden.write_text(json.dumps(json.loads(tool_lists[0]), ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
            print(f"golden updated: {golden}")
        elif json.loads(tool_lists[0]) != json.loads(golden.read_text(encoding="utf-8")):
            fail(f"tools/list golden [{exe.parent.name}]", f"differs from {golden}")
        else:
            print(f"ok tools/list deterministic ({len(tool_lists)} lists) and equal to golden [{exe.parent.name}]")
        print(f"bridge size: {exe} = {exe.stat().st_size:,} bytes")
    return finish()


def finish():
    if failures:
        print(f"FAILED: {failures}")
        return 1
    print("annotations: all builds and tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
