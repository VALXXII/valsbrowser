# Public download (Windows x64 ZIP) — report

Base: `origin/main` 6c57d88 (PR #69 Profiles + PR #70 Annotations merged).
Scope of this change (lot **A**): `tools/release/package.py`, `tools/release/publish.py`,
`tests/update-integration/test_release_tools.py`, README section, this report and its evidence.
No product C++, build flags, updater format, workflow or allowlist change. Nothing published.

## Why

The public feed `VALXXII/browser-releases` (latest `v0.3.239`, checked 2026-11-14) only carries
`browser-0.3.239-win-x64.lsrupd`, `update-manifest-win-x64.txt` and `SHA256SUMS.txt`. The `.lsrupd`
is the updater's own format: a friend cannot open it, and the CI workflow artifact is private.
There was no first-run download.

## What

**package.py** — after the existing steps (allowlisted staging, real `.lsrupd` packer, manifest
re-check) it writes `browser-<version>-win-x64.zip`:

- built from the **same staging tree** and the **same file list** as the `.lsrupd` (no second
  allowlist, no readme added, nothing else); runtime data (`profile`, `session`, `annotations`,
  logs, …) is skipped by `stage()` as before, any unknown file/dir or symlink still fails;
- one top folder `browser-<version>-win-x64/`, entries sorted, fixed timestamp 1980-01-01,
  deflate 9 → the same payload gives the same ZIP bytes (verified on the real v0.3.239 payload);
- re-opened and checked: entry set == staged files, CRC OK, every entry byte-identical to the
  staged file; otherwise packaging fails;
- `SHA256SUMS.txt` now lists package, manifest **and ZIP**; `release-info.json` gains
  `zip`, `zipSize`, `zipSha256`.

**publish.py** — the ZIP is a required asset:

- before any network mutation: `release-info.json` must name `browser-<version>-win-x64.zip`
  (same version as the package), the file must exist, size + SHA-256 must match release-info, and
  `SHA256SUMS.txt` must be exactly the three lines for package, manifest, ZIP. Channel must be
  `release`, commit must be the workflow commit (unchanged checks);
- upload to the draft includes the ZIP; server digests of package, manifest, ZIP and
  `SHA256SUMS.txt` are checked before the draft is published (the monotonic re-check is unchanged);
- **existing release** with our tag: manifest byte-identical (unchanged) **and** server digests of
  package, manifest and ZIP identical; a release without the ZIP is refused (exit ≠ 0), never
  edited nor extended — same immutability rule as before;
- after publishing: `releases/latest/download/<manifest>` must serve our manifest (unchanged) and
  the anonymous versioned ZIP URL must hash to our ZIP SHA-256, with the same retry loop;
- release notes start with the ZIP link, the stable `releases/latest` page, and French
  extract/run instructions (see below); the checksums stay at the bottom.
- `main(argv)` is now callable from tests; `fetch()` takes a byte limit (default unchanged).

**Stable link.** No second "latest" ZIP asset: it would double storage and add a second name to
keep consistent. The stable link to give friends is the release page
`https://github.com/VALXXII/browser-releases/releases/latest`; the versioned ZIP link is in the
notes of each release and printed by `publish.py` after verification.

**Workflow.** Unchanged: `build/release/dist/` (now with the ZIP) is already the uploaded artifact
the `publish` job reads; the staging directory `dist-payload` is outside it.

## First run — what really happens (audited, not changed)

- `ResolveBrowserPaths()` (src/main.cpp) + `UpdateCore::ResolveDataPaths()` (src/update-core.cpp):
  an extracted `browser-<v>-win-x64\browser.exe` is not under `%LOCALAPPDATA%\browser\versions\<v>`,
  so it runs **portable**: `profile`, `session`, `downloads`, `settings`, `prototype.log` are
  created **inside the extracted folder**.
- The ZIP is the `--channel release` build (`version.json` `channel: release`, update repository
  `VALXXII/browser-releases`), not a design preview: self-update is compiled in.
- When an update is installed, the new package's `update-helper.exe` moves the verified payload to
  `%LOCALAPPDATA%\browser\versions\<v>` and copies the portable profile/session/profiles **once**
  into `%LOCALAPPDATA%\browser\data` (`migrated-from.txt` records the portable folder). Later
  launches of the portable `browser.exe` forward to the installed copy
  (`UpdateCore::PortableForwardTarget`). The portable folder is not deleted.
- WebView2: required. When the runtime is missing, the existing startup recovery dialog offers
  « Installer WebView2 » (Microsoft bootstrapper, signature-checked) or the Microsoft page. **Not
  tested here** on a machine without the runtime; no install was performed.
- Windows SmartScreen / Mark of the Web will likely warn (unsigned exe); the notes say how to
  continue. Not reproduced here.
- **Annotations are not in this product.** `src/main.cpp:62-66` compiles the annotation mode out of
  `--channel release`, and `build.py` builds the MCP bridge for non-release channels only. Lot A
  alone therefore does **not** deliver the annotation/profile accents to friends; see lot B below.

## Tests and evidence (`design/public-download-evidence/`)

| Evidence | Command | Result |
|---|---|---|
| `test_release_tools.txt` | `python -I tests/update-integration/test_release_tools.py` | 32 tests, OK, 1 skipped (symlink creation needs privilege on this machine; junctions are covered without privilege) |
| `review-fixes-red-green.txt` | the 4 review-fix tests run against the reviewed b83bee7 code, then with the fix; real v0.3.239 payload repacked again | red: 9 failures (2 junction cases, existing release without SUMS, 6 manifest identity cases); green: 32 OK; real payload: same `.lsrupd` and ZIP SHA-256 as before |
| `mutation-checks.txt` | 7 deliberate regressions, suite re-run, file restored | all 7 detected |
| `real-payload-v0.3.239-zip.txt` | public `v0.3.239` `.lsrupd` (SHA256SUMS OK) unpacked as data, run through new `package.py` | `.lsrupd` re-created byte-identical (sha 734a73…), ZIP 13 279 149 bytes, 1625 entries == payload, bytes identical, one top folder, reproducible |
| `publish-dry-run-real-feed.txt` | `publish.py --dry-run` on that dist against the real feed (read-only) | `verify-existing` → refused: existing v0.3.239 has no ZIP, exit 1 |

New tests (`PublicZip`, `Publisher` with an in-memory fake GitHub; no network, no token):

- ZIP == staged payload: entry names, bytes, fixed dates, no dir entries, no `..`/absolute paths,
  no profile/session/annotations/log/MCP names; app directory canary unchanged; version.json inside;
  exact set of dist outputs.
- SHA256SUMS covers the three assets with correct digests; release-info zip fields; file count.
- Reproducible ZIP; `write_zip` never overwrites; `verify_zip` rejects extra/missing/altered entries.
- Packaging fails (no ZIP written) on `browser-annotations-mcp.exe`, `.mcp.json`, `settings`,
  `profiles/`, `.git/`, `src/` in the app dir. Symlink refusal (skipped without privilege);
  junction refusal (see review fixes).
- Publisher: happy path (ZIP uploaded to draft, notes contain link/instructions/hash, published
  latest, public ZIP verified); missing ZIP (3 variants) → exit, zero mutations; tampered ZIP, size,
  SHA256SUMS without ZIP or missing, changed package, dev channel, other commit → exit, zero
  mutations; server ZIP digest mismatch → never un-drafted; public ZIP mismatch → failure; existing
  release: identical → accepted without upload, missing/different ZIP → refused with zero mutations;
  superseded and dry-run → zero mutations.

### Independent review fixes (review of b83bee7)

- **P2, junctions leaked into the ZIP** (reproduced by the reviewer: `Path.is_symlink()` is false
  for a Windows junction and `rglob` followed it, so an outside `Cookies` file reached the public
  ZIP). `stage()` now checks every entry with `os.lstat` (`S_ISLNK` or
  `FILE_ATTRIBUTE_REPARSE_POINT`) **before** descending or copying: the app directory, every root
  entry, `assets` itself and every nested entry. Special files are refused too. Tests create an
  unprivileged junction (`_winapi.CreateJunction`) as `assets` and inside it: packaging fails, no
  ZIP is written, nothing from the target is staged, the target is untouched.
- **P3, existing release accepted without SHA256SUMS**: verify-existing now requires the server
  digests of package, manifest, ZIP **and** `SHA256SUMS.txt`; missing or different sums ⇒ refused,
  zero mutations.
- **P3, manifest identity**: before any upload the manifest is parsed strictly (6 LF lines, fixed
  key order) and must equal `browser-update 1`, our version, `x64`, our package name, its real size
  and SHA-256. Self-consistent artifacts with another version/arch/package/size/hash or CRLF are
  refused even when SHA256SUMS was regenerated.

Not run here: the native suite `tests/update-integration/run_all.py` (needs the pinned w64devkit,
absent in this worktree; no native file changed) and the real CI publish.

## Limitations

- Real publication, GitHub server digests and anonymous download are only exercised against the
  fake; the real feed was queried read-only.
- First run on a clean machine, SmartScreen and WebView2 bootstrap are documented from code, not
  tested.
- Existing releases (≤ v0.3.239) stay without ZIP; the first release built with this code carries it.
- The CI run for PR #70 (started before this change) uses the old publisher and, if it publishes,
  produces a release without ZIP.

## Lot B (annotations + bridge in release) — audit summary

Root decision: B is required for the final public download. Facts found:

1. `AllowedPayloadPath` (src/update-package.cpp) is identical in v0.3.239 (f66594…) and main: root
   files are a fixed list (+ `*-LICENSE.txt`/`*-NOTICE.txt`), otherwise `assets/**`. The **old**
   browser validates the package before staging, so a root-level `browser-annotations-mcp.exe`
   would make every existing client reject every future update. The only compatible location is
   under `assets/` (decided: `assets/bin/browser-annotations-mcp.exe`, documented product payload).
2. The installing helper is the **new** package's `update-helper.exe` (copied from the stage), so
   helper changes apply from the first update.
3. `StableBridgePath` looks for `<root>\bin\browser-annotations-mcp.exe`, but nothing creates
   `<root>\bin`; the helper keeps only current + previous versions, so a versioned path in an agent
   config breaks after two updates.
4. Server name is fixed to `browser-annotations-preview`; release needs its own.
5. The gate in `src/main.cpp` must only open together with the bridge.
The plan for B is `design/public-download-plan-b.md`; B is a separate commit/PR.
