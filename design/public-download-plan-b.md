# Plan B — annotations + MCP bridge in the release product (before code)

Root decision (2026-11-14): the public download must include annotations (profile accents). The
bridge ships as documented product payload `assets/bin/browser-annotations-mcp.exe`. Owners:
distribution worker = `tools/release/*`, packaging tests, `src/update-helper.cpp` + helper tests;
color writer = `src/main.cpp`, `src/annotation-mode.h`, `src/agent-connect.cpp` + their tests.
The gate in `src/main.cpp` opens only once every item below is in and reviewed.

## Facts this plan relies on (code read on 6c57d88)

- Old clients validate the package with **their** `AllowedPayloadPath` (identical v0.3.239 → main):
  fixed root names + licences, else `assets/**` (prefix only). `assets/bin/<exe>` passes; a root
  exe does not, for ever.
- The install is run by the **new** package's helper (`update-core.cpp` copies
  `stage\update-helper.exe`): helper logic below applies from the first update, even from v0.3.239.
- `VerifyStagedTree` re-hashes every staged file against `.stage`; extra/missing files fail.
- Helper keeps only current + previous version (`Prune`); renaming a directory with a running exe
  inside fails, so a version whose bridge is running is kept, not deleted.
- `StableBridgePath` (agent-connect) prefers `<root>\bin\browser-annotations-mcp.exe` if it exists,
  else the sibling of `browser.exe`. Nothing creates `<root>\bin` today.
- `BA::DefaultStoreRoot`: a bridge in `<root>\bin` or `<root>\versions\<v>` defaults to
  `<root>\data\annotations`; elsewhere `<exeDir>\annotations`.
- `MigratePortable` copies profile, session, downloads, bookmarks, profiles registry/folders —
  **not** `annotations`: a portable user's annotations would stay behind after the first update.

## Work items

1. **Payload contract** (distribution) — `build.py`: release builds the bridge into
   `assets/bin/browser-annotations-mcp.exe` (console PE x64). `--channel release` with
   `--annotations-bridge off` is refused (no UI without bridge); `--no-annotations` ⇒ no bridge.
   `package.py`: under `assets/`, `.exe`/`.dll` are refused **except** exactly
   `assets/bin/browser-annotations-mcp.exe` (today any `.exe` under assets would pass: this is a
   tightening). ZIP and `.lsrupd` keep the same file list (already guaranteed). Updater format and
   `AllowedPayloadPath` unchanged.
2. **Old-validator test** (distribution) — compile `src/update-package.cpp` from v0.3.239
   (`git show f66594…`) with a small driver and validate + stage the new `.lsrupd` (positive), and a
   package with a root-level bridge (negative, proves why). Needs the pinned w64devkit (CI step;
   locally from an existing checkout's toolchain). Plus `make_update_package.py --verify`.
3. **Stable bridge publication** (distribution, helper) — during install, after the tree is
   verified and before the new browser is launched:
   copy `versions\<v>\assets\bin\bridge` → `bin\browser-annotations-mcp.exe.new-<nonce>`, verify its
   SHA-256 against the staged list; if `bin\browser-annotations-mcp.exe` exists, **rename** it to
   `bin\.old-<nonce>.exe` (allowed while an agent runs it), then rename `.new` → final. Any failure
   ⇒ `abortBeforeSwitch("bridge-publish")` (no silent stale copy). Rollback (no startup ack) renames
   the old copy back. `.old-*` files are deleted best effort on later installs (still-running ones
   stay). `bin` and its files: no reparse points (same rules as `versions`).
   A running bridge keeps its old code until the agent restarts it; store-format compatibility
   between one bridge version and the next browser is the store owner's contract (limit to state).
4. **Portable annotations migration** (distribution, helper) — `MigratePortable` also copies
   `<exeDir>\annotations` (no reparse points, same deadline/copy rules), so data follows the user.
5. **Lookup + server name** (color writer) — sibling lookup becomes `assets\bin\…`; release server
   name distinct from `browser-annotations-preview` (e.g. `browser-annotations`), so dev and
   release coexist in the same Claude/Codex config.
6. **Agent config written from a portable run** — needs a decision (see below).
7. **Gate** (color writer) — remove `!BROWSER_CHANNEL_RELEASE` only with 1–6 merged; `run_all.py
   --app` static check: release app has the bridge iff annotations are compiled in.

## Decision needed (item 6)

A portable run writes `bridge = <extracted>\assets\bin\…` and `--store <extracted>\annotations`.
After the first update the browser uses `<root>\data\annotations`; the agent still reads the
portable folder. Options, none rewrites agent configs during an update:

- **(a, recommended)** Same policy as the browser's own portable forward
  (`PortableForwardTarget`): a bridge started from a folder named in `<root>\data\migrated-from.txt`
  with `--store` equal to that folder's `annotations` serves `<root>\data\annotations` instead
  (bridge-side, read-only check, no config write). Owner of `src/mcp-bridge.cpp` to assign.
- (b) The installed browser detects agent entries pointing into the migrated-from folder and
  offers « Reconnecter » (explicit user action, existing connect UI).
- (c) Connexion disabled in portable runs until installed — but a portable copy only installs on
  the next update, so friends could not use agents at first.

## Tests to add

Packaging: bridge present only at the exact path, refused elsewhere/other exe under assets, same
entries in ZIP and `.lsrupd`, release build without bridge refused. Helper (e2e_install fixture):
first install creates `bin` + bridge; second install with the bridge **running** (holds the image)
succeeds via rename; publish failure aborts and relaunches previous; rollback restores previous
bridge; two updates later the `bin` path still works; portable `annotations` migrated, source
untouched; reparse `bin` refused. Old-validator positive/negative (item 2).
