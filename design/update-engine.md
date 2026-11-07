# Update engine (update-core)

Native C++17 self-update for the Windows x64 browser: check → download → stage → user-confirmed install → verified relaunch,
with retained previous version and a stable profile/session outside version directories. No WebView2, no framework,
no services, no registry, no admin rights. Public API and wire formats: `build/coordination/update-core-contract.md`.

## Modules

| File | Role |
|---|---|
| `src/update-package.h/.cpp` | Manifest + `.lsrupd` parsing, path policy/allowlist, SHA-256 (BCrypt), staged extraction and re-verification. Shared by browser and helper. |
| `src/update-core.h/.cpp` | `UpdateCore::Engine` (state machine, worker thread, WinHTTP transport, TTL/backoff, notifications), `ResolveDataPaths`, `NoteStartup`. |
| `src/update-helper.cpp` | `update-helper.exe`: `--install <nonce>` installer, otherwise stable launcher (`<root>\browser.exe`). |
| `tools/update-package/make_update_package.py` | Release packer + manifest writer + verifier (stdlib only, reproducible). |
| `tests/update-core/*` | Unit/engine tests, end-to-end helper fixtures with fake browsers, packer interop. |

## Lifecycle

1. **Startup** — `Engine::Start` runs `NoteStartup` (startup ack / rollback notice) and, if enabled, one background check after
   `startupDelayMs` (cancellable). The check is skipped while the TTL holds (6 h after success; failure backoff 1, 2, 4 … 24 h;
   a `next-check` more than 24 h in the future — clock change — is ignored). There are no timers or loops at idle.
   Automatic failures, offline and "no release yet" (404) stay silent. A version already surfaced automatically
   (`notified available:<v>` / `ready:<v>`, also set by Dismiss) is not surfaced again automatically; `CheckNow` always shows it.
2. **Check** — GET `https://github.com/<repo>/releases/latest/download/update-manifest-win-x64.txt` (≤ 1 KiB). Strict 6-line
   manifest; the version must be strictly greater than the running one (no downgrade/reinstall, so a stale publisher cannot
   regress clients); arch must be `x64`; the package name must equal `browser-<v>-win-x64.lsrupd`.
3. **Download** (user action) — GET `…/releases/download/v<v>/<package>`, constructed (never from the manifest). Streamed to
   `updates\downloads\<pkg>.partial` with on-the-fly SHA-256; Content-Length (if present) and total byte count must equal the
   manifest size exactly, oversize aborts early. Progress ≤ 10 Hz, ETA from an EMA of throughput, `-1` (unknown) during the
   first second / first 64 KiB. Cancel or Shutdown aborts the in-flight WinHTTP request handle and deletes the partial.
4. **Stage** — the verified package is extracted into `updates\staging\<v>.partial-<rand>` (fresh directory), each file created
   with `CREATE_NEW`, checked to resolve inside the staging root (`GetFinalPathNameByHandle`), hashed against its entry; a `.stage`
   list is written last; the directory is then renamed to `updates\staging\<v>`. View → `Ready`.
5. **RestartInstall** (user action) — re-verify the staged tree; copy its `update-helper.exe` to `updates\helper\update-helper-<nonce>.exe`;
   write the single-use plan `updates\install-<nonce>` (pid, process creation time, previous exe/kind/version); start the helper
   (`CreateProcessW`, structured arguments `--install <32 hex>`, breakaway from job when allowed); return `CloseForInstall`.
   The host saves the session and exits normally.
6. **Helper** — derives the root only from its own location, validates the plan (installed previous exe must be
   `<root>\versions\<prev>\browser.exe`; a portable previous exe may keep its dev name such as `browser-main-78f5340.exe` —
   `[A-Za-z0-9._-]{1,64}.exe` on a local drive path, outside root, existing file, no reparse parent), deletes it (single use), waits for the
   exact process (pid + creation time + image path; never kills; 10 min limit), re-verifies the staged tree, renames it to
   `versions\<v>` (same volume, never touching live executables), migrates portable data once, waits for the profile lock,
   switches `current` atomically (`MoveFileEx` replace + write-through), refreshes the root launcher best-effort, writes
   `updates\pending`, launches the new `browser.exe`, then waits ≤ 120 s for `updates\ack-<nonce>` (exclusively created by the
   new process via `NoteStartup`, carrying its pid and version).
   - ack ⇒ `last-result installed`, prune versions other than current + previous (only if the directory can be renamed, i.e. unused).
   - new process exited without ack ⇒ restore `current`, discard the failed version dir and any data copied by this run,
     `last-result failed` and relaunch the previous exe, which shows the failure once (`Phase::Error`).
   - still running without ack ⇒ leave it alone (`unconfirmed`); it can still ack later.
   Any failure before the switch (stage invalid, move failed, profile locked, migration failed) leaves `current` untouched and
   relaunches the previous browser.
7. **Installed** — reported only by the new process itself after it created the ack (exactly once).

## Data safety

- Data lives in `<root>\data` (installed) or next to the exe (portable/dev builds, unchanged behaviour). Versions never contain
  data: the package allowlist rejects `profile/`, `session`, logs and anything not `browser.exe`, `update-helper.exe`,
  `WebView2Loader.dll`, `build-info.txt`, `version.json`, `*-LICENSE.txt`, `*-NOTICE.txt`, `assets/**`.
- Migration portable → installed: copy-only of `profile\` (all files, including unknown ones; reparse points are skipped, not
  followed), `session`, `downloads` (not `*.tmp`) into `data.migrating-<nonce>`, then one rename to `data`. Existing `data` is
  never overwritten. The source is never modified. A WebView2 process outliving the browser is handled by waiting for
  `profile\EBWebView\lockfile` to open exclusively (≤ 60 s); still locked ⇒ abort.
- Development and tests never use the real profile: every fixture runs in `build\update-core\tmp\<case>` with fake executables
  and canary files compared by SHA-256.

## Security model (honest)

- **Trust root**: TLS to `github.com` (system certificate validation, TLS 1.2+, no error suppression) + the release assets of
  the configured repository. The manifest gives exact size and SHA-256 of the package; each entry has its own SHA-256.
- **Not provided**: there is no code signature. Anyone able to publish releases in the configured repository (account, token or
  workflow compromise) can ship an update that clients will install. A TLS-intercepting proxy whose root is trusted by the
  machine is also trusted by WinHTTP. Same-user malware can modify `%LOCALAPPDATA%\browser` regardless; staged re-verification
  only catches corruption/tampering between staging and install, not a same-user attacker.
- **Hardening present**: fixed host allowlist (`github.com` initial; redirects only to `github.com`,
  `objects.githubusercontent.com`, `release-assets.githubusercontent.com`, https:443, no userinfo, ≤ 5 hops, followed manually),
  cookies/auth disabled, constructed URLs only, bounded sizes everywhere (manifest 1 KiB, package 512 MiB, entry 256 MiB,
  4096 entries, 240-byte ASCII paths), strict path grammar (no traversal, absolute, drive, UNC, ADS `:`, backslash, device
  names, trailing dot/space, hidden components, case-insensitive duplicates, file/dir conflicts), exact EOF, reparse-point
  checks on staging parents and staged trees, `CREATE_NEW` + final-path containment, helper inputs constrained to the root.
- Not changed: PATH, execution policy, timer resolution, scheduled tasks, services, shortcuts.

## Build identity

`M.m.B` compared numerically. The integrator stamps `0.3.<N>` with `N = git rev-list --count HEAD` on main (monotonic as long
as main is never force-pushed). `0.0.0` (local/dev) disables the engine.

## Known limitations

- No delta updates, no resume of interrupted downloads (restart from zero), uncompressed package (~payload size).
- A browser left open for days does not re-check until next start or a manual check (by design: no idle loop).
- Taskbar pins created on a versioned exe path do not follow updates unless the host sets an explicit AppUserModelID; shortcuts
  should target `<root>\browser.exe`.
- The root launcher can only be refreshed when not running at that instant (best effort; format is stable).
- The helper of the NEW version executes the install, so plan format `browser-install 1` must remain backward compatible.
- The 10-minute "browser did not exit" and 60-second "profile still locked" timeouts are implemented but not exercised to the end
  in tests (time); the lock wait itself is exercised with a held lockfile.
