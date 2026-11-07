# Self-updates — integration and release delivery

Owner of this document: update integration/release worker. Engine details: [update-engine.md](update-engine.md)
(core peer); toast visuals/motion: [update-toast.md](update-toast.md) (UI peer); Paper specimens of every
toast state incl. Installing / Error / unknown total: [update-paper.md](update-paper.md). Contracts:
`build/coordination/update-core-contract.md`, `build/coordination/update-toast-contract.md`.
Latest evidence and hashes: `build/coordination/update-integration-report.md`.

## What the user gets

1. A merge lands on **remote** `VALXXII/browser` `main` (a local merge that is not pushed does nothing).
2. GitHub Actions workflow `update-release` builds Windows x64 with the pinned project toolchain, runs the
   native-only regression + update suite, packages the allowlisted payload and — only if every step
   passed — publishes release `v0.3.<N>` to the **public binary feed** `VALXXII/browser-releases`
   (the source repository stays private).
3. A running browser checks once, ~4 s after start (TTL 6 h; failure back-off 1 h → 24 h; no idle
   timer, no polling). Offline / no published release / private feed ⇒ silent.
4. The toast (bottom-left, above the page, never takes focus) offers **Download** → real byte progress
   and ETA (or unknown) with **Cancel** → **Restart and install**. Nothing is installed without that click.
5. Restart: the session is saved, the validated helper is started, the browser closes normally. The helper
   waits for that exact PID (creation time + image path), never kills anything, installs the new version
   beside the old one, switches `current`, starts it. **Update installed** appears only when the new
   version itself acknowledges its start (exclusive ack file); a failed start rolls back and shows an error once.
6. Manual check: main menu (flower ▸ menu) **Rechercher des mises à jour**. Version shown in **À propos**.

## Build identity (one algorithm everywhere)

`N = git rev-list --count HEAD` (full history; shallow clones are refused), version `0.3.N`, tag `v0.3.N`.
`main` only grows (no force-push), so N strictly increases in merge order whatever the merge style
(first-parent counting was rejected: on this repository it gives 8 where the full count is 29 because
merges were made from feature branches). Computed by `tools/release/version.py`; embedded by
`tools/release/build.py` into the generated `build-info.h` (`BROWSER_UPDATE_VERSION`, repository,
commit, channel), the Windows VERSIONINFO (`0.3.N.0`) and `version.json`. The engine compares
`MAJOR.MINOR.BUILD` numerically.

Identity-stamped builds (`tools/release/build.py`, also behind `build.ps1`) define `BROWSER_HAS_BUILD_INFO`
and therefore compile the update host code. The existing fixtures that `#include "src/main.cpp"` are
compiled without it and keep linking only `native-lottie.cpp`.

## Host integration (src/main.cpp)

| Concern | Implementation |
| --- | --- |
| Data paths | `ResolveBrowserPaths()` → `UpdateCore::ResolveDataPaths(exe)` before the WebView2 environment. Portable: `<exeDir>\profile`, `session`, `prototype.log` (unchanged). Installed (`<root>\versions\<v>\browser.exe`): `<root>\data\profile`, `data\session`, `data\browser.log`; `data` created if missing. |
| Start | `StartUpdates()` after the window is shown: `Toast::Init(window, <exeDir>\assets, UpdateAction)`, `Engine::Start(Config{BROWSER_UPDATE_VERSION, exe, repository=VALXXII/browser-releases, notify=WM_APP+9})`. Never blocks; `--smoke-test` disables updates. |
| Thread safety | Engine threads only `PostMessage(WM_APP+9, generation)`; `WndProc` calls `OnNotify(w)` (stale generations refused) then `View()` → `Toast::SetView`. Toast actions arrive on the UI thread from a posted message. |
| Actions | `UpdateAction`: RestartInstall first saves the session, `Perform` launches the helper, `CloseForInstall` → `closingForInstall` and `WM_CLOSE` (which saves the session again atomically and destroys the window normally). |
| Window hooks | `WM_SIZE` (minimised → `SetOwnerVisible(false)`, restored → visible + `Layout`), `WM_WINDOWPOSCHANGED` (falls through to `DefWindowProc`), `WM_EXITSIZEMOVE`, `WM_DPICHANGED` → `Toast::Layout`. |
| Keyboard | F6 cycle page → toolbar → toast (when shown) → page; the toast's Esc release handler returns focus to the page. Browser shortcuts keep working while the toast has focus. |
| Close | `WM_DESTROY` → `StopUpdates()` (engine `Shutdown`, bounded; toast `Close`) before tabs are torn down. An already started helper is a separate process and continues. |
| Isolation | Update failures never set `halted`, never touch tabs, WebView2, page downloads or the session; a toast/engine init failure only logs. |
| Menu | Main menu item `Rechercher des mises à jour` (greyed while disabled/closing) → `Engine::CheckNow()` (manual: shows Error/Available, even a dismissed version). |

## Stable data, install layout, migration

```
%LOCALAPPDATA%\browser\           (no admin, no registry, no service)
  browser.exe        stable launcher (copy of update-helper.exe) -> versions\<current>\browser.exe
  current, previous  version pointers (atomic replace)
  versions\<v>\      immutable payload per version (previous kept for rollback)
  data\profile\      WebView2 user data folder (cookies, logins, cache …)
  data\session       tabs; data\browser.log
  updates\           state (TTL/back-off/dismissed), downloads\*.partial, staging\<v>, helper\, plans, acks
```

* The first update from a portable build (e.g. today's `build\v2\browser.exe`) installs into
  `%LOCALAPPDATA%\browser` and the helper performs a **one-time copy** of `profile\` and `session`
  and the `downloads` history into `data\` — after the exact browser PID
  exited **and** the WebView2 `lockfile` can be opened exclusively (≤ 60 s). Locked or failing copy ⇒
  migration and install aborted, nothing overwritten, the previous app restarted, error shown once.
  The legacy folder is never moved, modified or deleted; an existing `data\` is never overwritten.
* Packages can never contain profile/session/log/unknown files (packer allowlist, verified again by
  the extractor; `tools/release/package.py` refuses such entries before packing).
* Development never touched the real profile/session: every test uses isolated directories under
  `build/update-integration-tests` with SHA-256 canary trees.

## Release pipeline

Files: `.github/workflows/update-release.yml`, `tools/release/{version,build,package,publish}.py`,
core packer `tools/update-package/make_update_package.py`.

* Triggers: `push` to `main`, `workflow_dispatch`. Default token permission `contents: read`.
* `build` job (windows-2022): official actions pinned by commit SHA (checkout v4.2.2, setup-python
  v5.6.0 / Python 3.12.10, upload-artifact v4.6.2), full history, `setup.ps1` (w64devkit 2.11.0 and
  WebView2 SDK 1.0.4314.35, SHA-256 checked), ThorVG 1.1.3 (SHA-256 checked, meson 1.9.1 / ninja 1.13.0
  pinned), `build.py --channel release`, `tests/update-integration/run_all.py --app … --ci`,
  `package.py`, artifact upload. A newer push cancels an older running build.
* `publish` job (ubuntu-24.04) runs only on `refs/heads/main` of `VALXXII/browser` when repository
  variable `UPDATES_PUBLISH_ENABLED` is `true`; uses `secrets.UPDATES_RELEASE_TOKEN` (fine-grained,
  Contents read/write on `VALXXII/browser-releases` only). Serialized, never cancelled midway.
  `publish.py` guards: compute the newest version over all non-draft feed releases — newer than ours ⇒
  publish nothing; our tag already public ⇒ accept only if byte-identical (immutable); otherwise upload
  to a draft, verify server SHA-256 digests, re-check that nothing newer appeared, publish as latest, and
  verify anonymously that `releases/latest/download/update-manifest-win-x64.txt` serves our manifest.
  So the feed (and fresh installs, which read `latest`) never moves backwards.
* Builds are byte-reproducible: identical commit ⇒ identical `browser.exe` / `update-helper.exe`
  (`SOURCE_DATE_EPOCH` = commit time for the PE timestamp, `-ffile-prefix-map`), and the packer sorts entries.
* `/tools/` is ignored by `.gitignore`: `tools/release/` and `tools/update-package/` must be force-added
  (scripts only, never the toolchain) or the workflow cannot run.
* Assets per release: `browser-0.3.N-win-x64.lsrupd`, `update-manifest-win-x64.txt`, `SHA256SUMS.txt`.
  Payload: `browser.exe`, `update-helper.exe`, `WebView2Loader.dll`, `version.json`, licences/notices,
  `assets/**` only.

### Activation (owner, after review — nothing of this has been done)

1. Create public `VALXXII/browser-releases` with one initial commit on `main` (README); optionally enable
   GitHub "release immutability" there.
2. Create the fine-grained token (resource owner VALXXII, repository access: only `browser-releases`,
   permission Contents: read and write) and store it as secret `UPDATES_RELEASE_TOKEN` in `VALXXII/browser`.
3. Set repository variable `UPDATES_PUBLISH_ENABLED=true` in `VALXXII/browser`.
4. Push the reviewed branch and merge it to remote `main`; the first automatic release is produced by
   that push (or by "Run workflow"). Before that there is **no published release**: clients stay silent.
5. Installed users must start once from a stamped build (0.3.N) for updates to apply; build/v2 users get
   the one-time migration on their first update.

## Trust model and limits (honest)

* Integrity = HTTPS to github.com / its release-asset hosts + exact size + SHA-256 from a manifest fetched
  over HTTPS from the same feed repository. No code signing certificate: whoever can publish releases in
  `VALXXII/browser-releases` (token holder, workflow compromise) can ship code. Executables are unsigned;
  Windows SmartScreen may warn when a user downloads the package manually.
* The live browser path (WebView2 page + toast over it, real restart) is **not** executed in this task
  (WebView2 hold): host code is verified by compilation of the production build and by native fixtures that
  drive the production `main.cpp` update branches with the real engine, helper and toast.

## Reconciled with latest origin/main (download history drawer PR #28/#29, navigation states PR #30)

Merge of updater baseline 728fb29 with origin/main 1a69d7b (resolved in `src/main.cpp` and `README.md`):
* download history lives at `downloadsFile`: `<exeDir>\downloads` when portable (unchanged),
  `<root>\data\downloads` when installed (`DataPaths::downloadsFile`); the helper copies it once from a
  portable folder together with profile and session (never `downloads.tmp`, never moves the source);
* `WM_CLOSE` saves session and downloads (atomic temp + replace) before `DestroyWindow`; the install
  handoff relies on it, and `WM_DESTROY` then stops the engine and closes the toast;
* `WM_SIZE` keeps the upstream drawer reposition / close-on-minimise and calls `Toast::SetOwnerVisible`
  (which re-lays the card out); `WM_MOVE` repositions drawer and toast; `WM_DPICHANGED`/`WM_EXITSIZEMOVE`
  lay the toast out; the earlier `WM_WINDOWPOSCHANGED` hook was dropped (it duplicated WM_SIZE/WM_MOVE);
* `SyncUpdateToastReserve()` sets `Toast::SetReservedRight(DrawerLayout::width + margin)` while the drawer
  is open and 0 once it starts closing (called when the drawer is shown and from the surfaces' `closed` hook);
* focus: F6 cycles page → toolbar → toast (when shown) → page; the drawer keeps its own Tab/Esc handling
  and Esc returns focus to the page, so both surfaces give focus back the same way;
* no id collisions: upstream uses WM_APP+1/2/3/8, updates WM_APP+9 and menu command 3020.
Evidence: `build/coordination/update-integration-report.md` (final section).
