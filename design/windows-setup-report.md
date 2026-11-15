# Windows Setup EXE — report

Task task_c00c37340774, branch `VALXXII/windows-setup` (base main 18ddbd3). Plan and contract:
[windows-setup-plan.md](windows-setup-plan.md). Evidence: `design/windows-setup-evidence/`.
Nothing pushed, merged, released or uploaded.

## Decision (made after the audit)

Self-contained `browser-<v>-win-x64-setup.exe` = native stub ‖ the published `.lsrupd` byte for byte ‖ 128-byte
trailer. The stub copies the package out (SHA-256 vs trailer, bounded), hands it to the updater's own
`ReadPackageIndex`/`ExtractPackage`, then runs the payload's own `update-helper.exe --install` with a setup plan.
No second archive format, unpacker or install engine; no NSIS/Inno/WiX, no new toolchain, no runtime download.
A bootstrapper was rejected (network at install time, second trust path, same helper at the end anyway).
Cost: the `.lsrupd` is uncompressed → setup 50.6 MB vs ZIP 15.0 MB for 0.3.336.

## What changed

| Area | Change |
| --- | --- |
| `src/setup-payload.h` | PE overlay location (end of last section → Authenticode table or EOF), strict trailer parse (magics, zero padding, version == compiled version, offset == overlay start, size fills overlay, ≤ 512 MiB), hashed bounded copy (`CREATE_NEW`, deleted on any failure), stub writer for `uninstall.exe`. |
| `src/setup-core.{h,cpp}` | Inspect (fresh / update / repair / newer / unsafe: any reparse root/versions/data/updates/bin), running check (profile lockfiles incl. other profiles, mapped `browser.exe` image — read-only probes, nothing killed), stage, plan + helper, integrate (stable launcher check, `browser.ico`, `uninstall.exe`, Start Menu/optional Desktop `.lnk` → `<root>\browser.exe`, HKCU uninstall entry), uninstall (whole-directory renames first so nothing is half removed; `bin` left if an agent runs the bridge; data only on explicit option, plain directory only). |
| `src/setup-main.cpp`, `setup.rc`, `setup.manifest` | Compact dark window (browser canvas/accent), owner-drawn real BUTTON controls (Tab/Space/Enter/Esc, BM_CLICK), FR/EN (`data\ui-language` else Windows UI language), pages options → progress (cancellable while preparing) → done/error with Retry. `asInvoker` manifest (required: Windows auto-elevates exes named *setup* otherwise). Uninstaller re-runs from `%TEMP%` (it must delete itself). Test hooks only under `-DSETUP_TESTING`. |
| `src/update-helper.cpp` | Plan line `origin setup` (absent from engine plans → parsed exactly as before): `previous-kind none / installed (pid 0) / repair`, `launch 0|1`, no relaunch on failure; repair renames the live `versions\<v>` aside and restores it on every failure path; `previous` kept for first install/repair; `DisplayVersion` refreshed after any install only if the setup's key exists **and** names this root. Fault points `move`, `repair-aside` (fault build only). |
| `src/setup-uninstall-key.h` | One app-owned key `HKCU\...\Uninstall\VALXXII.browser`; fixture redirection compiled only into test builds. |
| `tools/release/build.py` | Builds the stub into `<out>-setup/browser-setup.exe` (never in the payload), `--strip-all` (a COFF symbol table would read as an overlay). |
| `tools/release/package.py` | Stub checked **before** packing (GUI x64 PE, no overlay/signature, `asInvoker`, identity string == version/commit/channel of `version.json`, no test-build markers); writes + re-verifies the setup; `SHA256SUMS.txt` + `release-info.json` (`setup`, `setupSize`, `setupSha256`). |
| `tools/release/publish.py` | Setup is a required asset like the ZIP: local size/hash/embedding check, draft digest, existing-release digest, anonymous public download hash; notes lead with the setup, ZIP second. |
| Workflow | Same order; `package.py --setup-stub build/release/app-setup/browser-setup.exe`. |
| Docs | README download section, `design/updates.md` assets line. |

`.lsrupd` format, manifest, allowlist, payload contents and the feed are unchanged: existing clients see exactly the
same update assets (the setup is an extra asset they never read).

## Installation flow (what a friend sees)

Download one file → SmartScreen may warn (unsigned) → window « Installer browser » (version, « pour votre compte
Windows, sans droits administrateur », where it goes) with « Créer un raccourci sur le Bureau » (off) and « Ouvrir
browser une fois installé » (on) → Installer → progress → « browser est installé et démarre » → Fermer. Same file
later: « Mettre à jour » (data kept) or « Réparer »; an older setup over a newer install changes nothing.
Uninstall: Settings › Apps › browser → confirmation, « Supprimer aussi mes données » unticked by default (second
confirmation when ticked). Screens: `setup-*.png`, `uninstall-*.png`.

## Results (all run here)

| Evidence | Command | Result |
| --- | --- | --- |
| `setup-suite.txt` | `python -I tests/setup/run_tests.py` | **231 checks, 0 failures**, 12 cases (below) |
| `setup-suite-red-base-helper.txt` | same suite, payload helper from base 18ddbd3 | red: `plan rejected: plan version` → exit 17 « helper », nothing installed |
| `release-tools-tests.txt` | `python -I tests/update-integration/test_release_tools.py` | 57 OK, 1 skipped (symlink privilege, unchanged) |
| `release-tools-red-on-base.txt` | new tests against base `tools/release` | red: 19 failures + 10 errors |
| `update-core-tests.txt` | `python -I tests/update-core/run_tests.py` (helper changed) | update_core_test 315/0, **install_test 539/0**, packer interop OK |
| `e2e-real.txt` + PNGs | `python -I tests/setup/e2e_real.py --app build/setup-e2e/app` | E2E OK (below) |

Setup suite cases (real test-build stub, payload = fake browser that acks like the real one + real fault-build
helper; `LOCALAPPDATA`, Start Menu, Desktop, Temp and the uninstall key redirected under
`build/setup-tests/tmp/<case>`; keys deleted after each case): fresh install without/with launch (ack), installed
tree == payload == `.lsrupd` entries, launcher == helper, `bin` bridge == versioned bridge, `uninstall.exe` == stub,
shortcuts → `<root>\browser.exe`, registry values; update 0.3.2→0.3.3 with 9 data canaries (profile, other profile,
session, bookmarks, annotations, ui-language, downloads, profiles.txt) byte-identical; repair of a damaged file;
older setup over newer = no change; desktop link removed when the option is turned off (ours only); foreign Start
Menu/Desktop links untouched; locked profile and running `browser.exe` refused (process not killed), retry works;
8 damaged downloads (truncated, stub only, flipped package byte, trailer hash/version/size, appended bytes) and 5
hostile packages behind a valid trailer (traversal, absolute, not allowlisted `profile/`, case duplicate, ADS) all
refused with nothing written outside and nothing installed; copied-package corruption refused by the validator;
junction at `versions`, root, `data` refused with targets untouched; extract/mkdir write faults, cancel, helper faults
`move`, `current-write`, `bin-copy`, `bin-final`, no-ack crash rollback (update and first install), failing repair
rename — program files, `current`, launcher, bridge and data unchanged each time, then a clean retry; uninstall
refused while running, keeps data byte-identical, reinstall finds it, `--remove-data` removes the root, `data`
junction refused; after a setup install the **browser's own engine update** (fake 0.3.2 `--update`) installs 0.3.3
and the helper refreshes `DisplayVersion`.

Real-app E2E (release-channel build 0.3.336 of this branch, stamped `--commit 18ddbd3` only to pass the local
main check and `--update-repo VALXXII/setup-e2e-no-feed` — **a local candidate that must never be published**; CI
rebuilds the exact merged SHA): package.py produced `.lsrupd` 49 854 085 B (sha 3ba62465…), ZIP 15 008 790 B
(164091bb…), setup 50 611 461 B (0fbacb06…) = stub 757 248 + package + trailer 128; ZIP entries == `.lsrupd` entries
(1630 files, byte-identical, bridge at `assets/bin`); release stub has no test hooks. A test stub with the same
`build-info.h` around the same package was driven through its own window handles (BM_CLICK, PrintWindow; no
SendInput, no foreground change): FR install with desktop shortcut → real browser started from
`<run>\LocalAppData\browser\versions\0.3.336`, **acknowledged** (`status installed, reason ack`), window captured,
closed with WM_CLOSE; installed tree == `.lsrupd` == ZIP; stable bridge `bin\browser-annotations-mcp.exe` answered
MCP `initialize` + `tools/list` (5 tools); EN repair without launch with « WebView2 absent » simulated (note shown);
EN uninstall window → program and shortcuts removed, data kept.

## Findings outside this change

* **Version directories are not immutable at runtime:** on first start WebView2 writes 56 files under
  `versions\<v>\assets\extensions\ublock-lite\_metadata\generated_indexed_rulesets\`. All payload files stay
  byte-identical; the E2E excludes only those generated files. Consequence today: none for setup or updater (neither
  re-verifies a running version), but anything that later re-verifies `versions\<current>` against `.stage` would
  report extra files. Worth an owner decision (e.g. copy the extension to `data` before loading).

## Integration with root WIP (not required now)

* `src/version-info.rc` (root WIP): add `VI_*` + `#include "version-info.rc"` to `src/setup.rc` and add
  `browser-setup.exe` (in `<out>-setup/`) to `run_all.py`'s version-resource check.
* `tools/release/build.py`: the WIP edits the windres loop and the bridge link; this change adds a separate block
  after the strip loop and constants after `BRIDGE_LIBS` — textual merge only.
* `tests/update-integration/run_all.py`: untouched here; suggested follow-up: run `tests/setup/run_tests.py` in CI
  (it needs ~3 min and the pinned toolchain only).

## Limitations (honest)

* Not tested: a clean machine/VM, a machine **without** WebView2 (only the setup note was simulated; the browser's
  own recovery dialog is the existing, separately tested path), real SmartScreen/Mark-of-the-Web, real Start Menu,
  real Apps & features list, the release stub executed (it would write the real Start Menu and HKCU; it is verified
  structurally and shares all code with the executed test stub except the redirection hooks).
* Unsigned: SmartScreen will likely warn. Signing would not by itself remove warnings (reputation); no certificate
  was sought. The trailer hash protects against corruption, not a malicious re-pack (provenance = HTTPS + published
  SHA-256, same as the ZIP).
* The setup never migrates a portable folder (by design). A portable copy whose updater later installs keeps the
  existing behaviour: an existing `data` (e.g. from the setup) is not overwritten, so that copy's portable profile is
  not merged.
* A same-named Start Menu link pointing to another copy is left untouched (reported on the done page): that user then
  has no Start Menu entry for the installed copy.
* `uninstall.exe` leaves its small temp copy in `%TEMP%` (no admin-only delayed delete). A bridge run by an agent
  during uninstall stays in `bin` (reported); agent configs pointing to it are not edited.
* A crash exactly between the repair rename and the move leaves `current` naming a missing directory; the launcher
  reports a broken install and the next setup run installs again (data untouched). Not fault-injected.
* Size: setup ≈ 3.4× the ZIP (uncompressed package).
