# Windows Setup EXE — plan and integration contract

Task task_c00c37340774. Base: main 18ddbd3. Goal: friends download ONE file,
`browser-<version>-win-x64-setup.exe`, run it, get a per-user install in the existing
`%LOCALAPPDATA%\browser` layout with a Start Menu shortcut. The ZIP stays as the portable
secondary download. Nothing of the updater feed (`.lsrupd`, manifest, allowlist) changes.

## Architectural call: self-contained setup that carries the exact `.lsrupd`

**Chosen:** `setup.exe = setup stub (native C++) ‖ browser-<v>-win-x64.lsrupd ‖ 128-byte trailer`.

* The embedded blob is **byte-identical to the published `.lsrupd`**. The setup never parses
  a new archive format: it copies the blob out (bounded, SHA-256 checked against the trailer),
  then hands it to the existing `UpdatePackage::ReadPackageIndex` / `ExtractPackage`
  (strict path grammar, allowlist, duplicates, sizes, `CREATE_NEW`, final-path containment,
  per-entry SHA-256) — the same validator every client already runs on downloaded updates.
* The install itself is done by the **payload's own `update-helper.exe --install <nonce>`**
  from `updates\helper\`, exactly as the engine does on "Restart and install": move staged tree
  to `versions\<v>`, verify again, stable launcher, `bin\` bridge publication under
  `bin\.publish-lock`, atomic `current` switch, launch + startup ack, rollback. The setup adds
  no second install engine; the plan format gains setup-only values (below) that only a helper
  of the same version ever reads.
* **Bootstrapper rejected:** it needs network at install time, a second trust path for the
  download, retry/offline UI, and would still end in the same helper. A self-contained file has
  the same provenance as the ZIP (HTTPS from the GitHub release + `SHA256SUMS.txt`) and works
  offline. Cost: the `.lsrupd` is uncompressed, so the setup is larger than the ZIP (measured in
  the report). No compression is added (no new decompressor/unpacker).
* **No NSIS/Inno/WiX:** no new toolchain, licence or download; the stub is built with the
  pinned w64devkit by `tools/release/build.py`, reproducible like the other executables.

### Trailer and layout validation (`src/setup-payload.h`)

The overlay starts at the end of the last PE section's raw data and ends at the Authenticode
certificate table if one exists (so a later signature does not break it), else at EOF.
Trailer (last 128 bytes of the overlay, little endian):
`"BROWSER-SETUP 1\0"(16) | version ASCII NUL-padded(32) | package offset u64 | package size
u64 | package SHA-256(32) | zero(16) | "BRSETUP!"(8)`. Required: both magics, version parses and
equals the stub's compiled `BROWSER_VERSION_TEXT`, offset == overlay start,
offset + size + 128 == overlay end, size ≤ `UpdatePackage::MaxPackageBytes`, zero padding.
Any mismatch ⇒ "this download is damaged, download it again", nothing written outside a temp
file in `updates\`. The trailer hash detects truncation/corruption, **not** a malicious re-pack
(that is what the published SHA-256 and HTTPS provenance are for — same as ZIP; no signing).

## Install flow (`src/setup-main.cpp` UI, `src/setup-core.{h,cpp}` logic)

1. Single instance (`Local\browser-setup` mutex). Root = `UpdateCore::DefaultInstallRoot()`
   (`%LOCALAPPDATA%\browser`, same resolution as the browser). No admin (`asInvoker`).
2. Inspect: not installed / older `current` (update, data kept) / same version (repair) /
   newer (refuse downgrade, offer to open it) / root without valid `current` (install).
   Root, `versions`, `data`, `updates` must not be reparse points.
3. Options page: Start Menu shortcut (always), Desktop shortcut (opt-in, unchecked), launch at
   the end (checked). FR or EN (the existing `data\ui-language` preference, else the Windows UI
   language).
4. Running check: browser profile lock (`data\profile\EBWebView\lockfile`) or a version
   directory in use ⇒ "close browser, then Retry". Nothing is killed.
5. Stage: blob → `updates\downloads\setup-<nonce>.lsrupd` (hash checked) → `ExtractPackage`
   → `updates\staging\<v>.partial-<nonce>` → rename to `updates\staging\<v>` (an existing
   staged `<v>` is moved aside and removed first). Cancel allowed until here; temp files removed.
6. Helper: copy `staging\<v>\update-helper.exe` to `updates\helper\update-helper-<nonce>.exe`,
   write `updates\install-<nonce>`, run `--install <nonce>`, wait, read `updates\last-result`.
7. Integrate (only after `installed`/`unconfirmed`): shortcuts → `<root>\browser.exe`
   (never a version directory), icon `<root>\browser.ico`; `<root>\uninstall.exe` (= setup stub
   image without the payload); HKCU uninstall entry.
8. Result page: done / error with retry. WebView2 runtime absent (registry probe) ⇒ an honest
   note: the browser offers Microsoft's installer at first start (existing recovery dialog,
   signature-checked bootstrapper). The setup never downloads anything.

### Plan format additions (helper of the same version only)

`browser-install 1` + new optional `origin setup`. With it:
* `previous-kind none` (first install: no previous version, no migration, rollback deletes
  `current`), `installed` (update from an installed current; `pid 0` = not running),
  `repair` (`version == previous-version == current`: the live directory is renamed aside, the
  fresh one installed, restored on any failure);
* `launch 0|1`: with 0 no process is started (result `installed`, reason `setup-no-launch`);
* nothing is relaunched on failure (the setup shows the error).
Engine plans (no `origin`) are parsed exactly as before. `previous-kind portable` from a setup is
refused: the setup never migrates a portable folder.

## Uninstall

`<root>\uninstall.exe` copies itself to `%TEMP%\browser-uninstall-<nonce>.exe` and re-runs from
there (it must delete its own file). Confirmation page; checkbox "also delete my data"
(unchecked; second confirmation). Refuses while the browser runs. Removes only owned items:
`browser.exe`, `browser.ico`, `current`, `previous`, `versions\`, `updates\`, `bin\` (a bridge
still run by an agent stays renamed aside and is reported), `uninstall.exe`, our shortcuts
(only `.lnk` whose target is `<root>\browser.exe`), the HKCU key. `data\` is kept unless the
option is ticked; then it is removed only if it is a plain directory under root (no reparse
point is followed, `RemoveTree`). The temp copy of the uninstaller stays in `%TEMP%`
(no admin-only delayed delete).

Registry: `HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\VALXXII.browser`
(DisplayName, DisplayVersion, Publisher, DisplayIcon, InstallLocation, UninstallString,
NoModify, NoRepair, EstimatedSize). The helper refreshes `DisplayVersion` after an update only
if that key exists and its `InstallLocation` is its root. No HKLM, no services, no autostart, no
default-browser, no agent configuration, no import.

## Release pipeline

* `build.py`: builds `browser-setup.exe` (stub) into `<out>-setup\` (never in the app payload).
* `package.py --setup-stub`: after `.lsrupd`/ZIP, writes `browser-<v>-win-x64-setup.exe` =
  stub ‖ package ‖ trailer, re-verifies it (PE overlay, trailer, embedded bytes == `.lsrupd`,
  stub embeds the version/commit), adds it to `SHA256SUMS.txt` and `release-info.json`
  (`setup`, `setupSize`, `setupSha256`).
* `publish.py`: setup is a required asset (local checks, draft digest, verify-existing digest,
  anonymous public download hash), notes lead with the setup link, ZIP second.
* Workflow: build → tests → package unchanged in order; the stub path is passed to package.py.

## Tests (all under injected temp roots)

`tests/setup/`: test build of the stub with `-DSETUP_TESTING` (env `BROWSER_SETUP_TEST_ROOT`
redirects Start Menu, Desktop and the uninstall key to a disposable location; the release stub
has no such switch), fault-injecting helper and fake browsers from `tests/update-core`.
Cases: first install, repeat/repair, update from older with data canaries, newer refused,
stable shortcut target, uninstall keeps data, uninstall with data, truncated/corrupt/tampered
setup, traversal entry in embedded package, reparse root, disk/write faults, locked profile,
running browser, helper rollback, cancel. Python: setup writer/verifier in
`test_release_tools.py`. Real-app: isolated setup UI driven through its own window handles
(PostMessage/BM_CLICK, PrintWindow), installed real browser acks with synthetic data.

## Reconciliation points with root WIP

* `src/version-info.rc` / `*.rc` (root WIP): the stub uses `src/setup.rc` (icon + manifest);
  when the WIP lands, add `#include "version-info.rc"` with VI_* for `browser-setup.exe` and add
  it to `run_all.py`'s version-resource check.
* `tools/release/build.py` (root WIP touches the windres loop and bridge link): this change
  adds a separate stub build block after the bridge; textual merge only.
* `tests/update-integration/run_all.py` (root WIP): no change here.
