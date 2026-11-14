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
- **Annotations:** with lot A alone (at 6c57d88), the release product has no annotation mode. Lot B
  (below) ships it: release builds compile it in with `BROWSER_RELEASE_ANNOTATIONS` only together
  with the bridge at `assets/bin/browser-annotations-mcp.exe`.
- The root published `v0.3.239-portable` (ZIP of the existing stable release, made with lot A's
  `package.py`, `--latest=false`) as a stop-gap. It has no annotation mode.

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

## Lot B — annotations + MCP bridge in the release product

Branch `VALXXII/release-annotations-bridge` (stacked on A, then merged with `main` 411acbb, which
holds PR #72 profile colours and PR #73 release wiring). Plan, protocol and the decisions it records:
`design/public-download-plan-b.md`.

### Payload and compatibility (items 1–2)

- `build.py`: a release build places the bridge at **`assets/bin/browser-annotations-mcp.exe`** and
  passes `-DBROWSER_RELEASE_ANNOTATIONS` only in that case (PR #73's gate in `src/main.cpp`). Release +
  bridge off, and bridge on + `--no-annotations`, are refused; `--no-annotations` stays available as
  a measurement baseline (no bridge). Dev builds keep the bridge next to `browser.exe`.
- `package.py`: any executable under `assets/` is refused except that exact path and case, which must
  be a PE32+ x64 **console** image. `run_all.py --app`: a release app has no root bridge, and ships
  the bridge iff `browser.exe` contains the annotation mode.
- **Why `assets/bin`:** an installed client validates an update with its **own** `AllowedPayloadPath`
  (identical from v0.3.239 to now): fixed root names + licences, else `assets/**`. A root-level bridge
  would make every existing client reject every future update. `tests/update-integration/old_validator.py`
  compiles v0.3.239's validator from git (f66594) and proves: bridge at `assets/bin` accepted and
  staged byte-identical (synthetic payload, real 0.3.239 payload, and the real integrated 0.3.285
  package), root bridge rejected.

### Stable bridge path for agents (item 3)

Agents are configured with `%LOCALAPPDATA%\browser\bin\browser-annotations-mcp.exe` (PR #73's
lookup never falls back to a versioned copy, which pruning would delete).
`update-helper.exe` keeps that file **byte-identical to `versions\<current>\assets\bin\…`**, or absent
when `current` ships none, under an exclusive `bin\.publish-lock`:

- published **before** the new version starts, lock kept until `current` names it; restored on every
  failure path and on rollback; repaired by the launcher at every start, which reads `current`
  **under the lock** and launches that same version;
- a running agent's bridge is never killed or written: Windows refuses to replace a mapped exe (probe:
  `MoveFileEx` and POSIX rename both fail with error 5), so the old file is renamed aside (allowed,
  process keeps running) and deleted once unused. The final name is absent only between those two
  renames; a crash there is repaired by the next launcher start;
- every object read or written is opened without following a final reparse point and must resolve
  (`GetFinalPathNameByHandle`) exactly under the install root: a junction anywhere below the root
  (`versions\<v>\assets`, `bin`, …) is refused; the published bytes are re-read and compared;
- the install replaces the stable launcher **before** publishing `bin` (busy launcher ⇒ the install
  aborts before anything is published, and is retried later), so the first upgrade from a launcher
  that predates the bridge is also repaired by the next launcher start.

### Portable annotations follow the install, live agents never diverge (items 4 + 6)

A friend runs the extracted ZIP (portable: store `<folder>\annotations`) and may connect an agent,
whose config then names `<folder>\assets\bin\…` and that store. The first installed update moves the
data to `%LOCALAPPDATA%\browser\data`. Protocol (`src/annotations-migration.h`):

- **Gate** `<folder>\annotations.write-gate`: every bridge request (all of them: list/get also write
  reader records) holds it **shared for the whole request**, during which the store is re-resolved;
  every browser store mutation holds it too. The helper holds it **exclusively** from before the copy
  until the outcome. Meanwhile requests get a retryable « Migration … en cours » and change nothing.
- **Durable record** `data\annotations-migration`: `copied` is published with the copied data;
  `committed` is written only **after the new version's startup ack**, then a mirror in the portable
  folder. Only `committed` naming exactly that folder, plus the browser's own
  `UpdateCore::PortableForwardTarget` rules (valid non-zero current with its exe, last result not
  failed, no reparse root/data/versions, local drive), lets a **release** bridge on its **default**
  portable store serve `data\annotations`. Custom `--store`, installed bridges and dev/preview builds
  never redirect. Leases, tokens and `--site` are data in the store: they travel with the copy.
- The installed copy refuses mutations while `copied`; the portable store refuses them once the
  mirror says it moved (a portable browser process still running there). Rollback discards the copy
  under the gate (no record ⇒ the source stays authoritative).
- **Bounded recovery** at every launcher and install start: an uncommitted migration (helper died
  after ack, before commit) is redone from the source under the exclusive gate (re-copy, swap,
  commit); an install migrated before the record existed is committed if nothing was moved, else
  marked `skipped` (its installed store is kept, no redirect).

### Tests and evidence (lot B)

| Evidence | Result |
|---|---|
| `test_release_tools.py` | 38 OK, 1 skipped (symlink privilege) |
| `old-validator-v0.3.239.txt` | v0.3.239 validator: `assets/bin` accepted + byte-identical staging (synthetic and real 0.3.239 payload); root bridge rejected |
| `b-update-core-tests.txt` (`tests/update-core/run_tests.py`) | update_core_test 315/0; **install_test 491/0** with real live MCP bridge processes — see list below |
| `b-annotations-tests.txt` (`tests/annotations/run_tests.py --quick`) | store_test 2662/0, json_fuzz 156715/0, host_test 18183/0, agent_connect_test 288/0 (after merge) |
| `b-item3-helper-mutations.txt`, `b-review-fix-mutations.txt` | 8 deliberate helper regressions, all detected |
| `b-item3-running-exe-replace-probe.txt` | replacing a running exe fails, rename aside works |
| `b-integrated-release-0.3.285.txt` | **release CANDIDATE** (`--channel release`, source = B branch d3e81c6 = main 411acbb + B; `--commit 411acbb` was passed only to satisfy `require_on_main` locally, so its stamped identity is NOT its source and it must never be published — the final artifact is rebuilt by CI from the exact merged SHA): `-DBROWSER_RELEASE_ANNOTATIONS`, static check OK, package + ZIP (1630 entries) == app payload == tree staged by the real v0.3.239 validator, bridge only at `assets/bin`, release server name, no preview name |
| `b-release-bridge-suite.txt` | the annotation MCP suite run against the candidate's bridge (LOCALAPPDATA redirected to a temp dir): all passed (protocol level) |
| `b-release-candidate-app-e2e.txt` + 3 PNG | **real application run** of isolated copies of the candidate (LOCALAPPDATA redirected, agent-config writes redirected, nothing of the user touched): `tests/annotations/profile_color_e2e.py` 0 failures (annotation mode present, profile colour at entry, live edit and profile switch, band colours on screen, no annotation written); the connect target resolved `<copy>\assets\bin\…`; `tests/update-integration/release_candidate_no_bridge.py` 0 failures (copy without `assets\bin` bridge: mode entry opens the connect modal, « bridge not found » logged, no config written). Modal buttons not clicked (BridgeUsable refusal is unit-tested in agent_connect_test) |
| `b-run-all-0.3.285.txt` | `tests/update-integration/run_all.py` on the candidate: step « existing fixtures » blocked by the pre-existing `download_panel_test` `-Werror` failure on main (CI broken since PR #69, fix owned elsewhere); peers / integration / app steps run separately (results in the file) |

install_test bridge/migration cases: two updates with the same agent alive (0.3.1 pruned under
it); rollback; faults at copy / aside / final / current-write then retry; helper killed after
aside / after publish / after switch then launcher repair + reinstall; portable first install and
rollback (no bridge left); `bin` junction refused; junction above the bridge source and at `bin` for
the launcher (nothing outside root read or written); real concurrent launcher during an install
paused between publish and switch (launcher waits ~4 s, launches and keeps 0.3.2); first transition
with the launcher compiled from 9f73bdd (kill after publish → repair → ordinary retry; busy launcher
→ clean abort); migration committed with a live portable release bridge (same process resolves with
its original token, two later updates); helper killed before commit (no redirect, installed copy
frozen, agent keeps writing the source, launcher recovery re-copies including those writes, same
agent resolves with its newest token); rollback never redirects; Busy during the copy window.

### Limitations (lot B)

- **First transition, launcher busy:** if the stable launcher can't be replaced, the install aborts
  and is retried at the next update check (nothing published). Interrupted first upgrades are covered
  by the tests above; launchers older than this helper never repair anything themselves.
- **Gap of two renames:** the stable bridge name is briefly absent while a running copy is renamed
  aside; an agent starting exactly then fails once and retries. A crash in that gap is repaired at
  the next launcher start, not before.
- **After a helper crash,** the gate is released when the kernel closes the dead process's handles
  (measured ~190 ms after its image was released); requests in between get the retryable Busy.
- **Store format across versions:** an agent keeps running its old bridge after an update until it
  restarts it; compatibility between that bridge and the newer browser's store is the store's own
  versioning contract (unchanged here).
- **Recovery needs a start:** an uncommitted migration stays « installed copy frozen, source
  authoritative » until the next launcher or install start.
- Not exercised: a real public publication of this build, a clean-machine first run, SmartScreen,
  WebView2 bootstrap, real Claude/Codex clients (the bridge was driven over stdio by the tests).
