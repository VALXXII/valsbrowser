# Popup window target (window.open / target=_blank) — repair report

Branch `VALXXII/fix-popup-window-target` (local only, not pushed), based exactly on
`01bf9f784d2110ac3ccce95ceacee12fb9499cc1`. Plan followed: root's
`build/payment-popup-host-review-plan.md` (reviewed against `78694e2`; the cited lines were unchanged at the base).
Evidence: `design/popup-window-target-evidence/` (see « Tests and evidence »). **Root independent review is required**
before any push/merge; nothing was published.

## Defect (reproduced, RED)

`NewWindowRequested` (`src/main.cpp`, popup lambda) set `Handled=TRUE`, never assigned `NewWindow`, dropped
`about:blank` and navigated an unrelated new tab for HTTP(S). On the unmodified base, our local fixture measures:
`window.open(http)` → `null`, the child has `opener=false`, no postMessage/ack; `window.open('about:blank')` → `null`,
no window; a named window is never reused; `child.close()` does nothing (`03-target-baseline-RED.txt`).

**Scope boundary.** This is a generic host defect. It is independent of the Web Payment API: the fixture still measures
`typeof PaymentRequest === 'undefined'` (WebView2 disables it) and this repair does not touch it. The real Cloudflare
checkout was **not** observed; nothing here claims Apple Pay / Google Pay now work. No wallet SDK, merchant, account,
card, token, payment or login flow was used.

## Repair (what the code does now)

- **Real target, fresh tab, same environment.** An admitted request (exact `about:blank`, or HTTP(S) as before; never
  file:, data:, javascript:, debug/internal, other schemes) is marked handled, its deferral is taken, its args and the
  opener's **exact** environment are retained (`PopupCom`), and a fresh tab is created in the opener's workspace through
  the common creation path (`CreateTab(…, popup)`; ordinary `NewTab` call sites unchanged). Private windows reuse the
  private branch (`PrivateProfileName`, `IsInPrivateModeEnabled`) with the captured environment.
- **Never navigated by the host.** The popup intent never calls `Navigate`/`NavigateToString`/home: the engine loads its
  own request once `put_NewWindow` is assigned and the deferral completed.
- **Ordering (Microsoft's contract).** Settings and ordinary event handlers (navigation/HTTPS/sandbox, error, history,
  title, nested popups, process failure, permissions, dialogs, basic auth, client certificate, external schemes,
  server-certificate refusal, downloads, context menu, `WindowCloseRequested`) are registered before assignment; a failed
  **required** security handler registration refuses the target. A readiness barrier waits for the controller setup and
  every initial document script — scrollbar, vault (when enabled) and audio — before assignment (no dummy flush, no
  script removal meanwhile: audio re-injection is queued). After `put_NewWindow`: the sandbox resource filter/handler and,
  in the E2E test host only, its destination guards (now `RegisterE2EDestinationGuards`), then the deferral completes.
- **Exactly-once lifecycle** (`src/popup-target.h`, COM-free): `Registry::Finish` marks terminal first, leaves the registry,
  completes the deferral, drops the target on failure, releases args/environment; the deferral guard's destructor is the
  backup. Cancellation: tab close (opener or pending target, by identity), manual close, `WM_CLOSE`, `WM_DESTROY`,
  terminal `Fail` (before any modal loop), `MakeCold`, `DisposeWorkspace`, workspace engine failure;
  `WorkspaceBusy` keeps a workspace with a pending request out of the LRU. The controller and script completions are
  **unowned** callbacks (precedent `profile-workspace.h` engine creation): they validate terminal/dead owner themselves,
  close a late controller, and only then enter the owner's scope. A failed placeholder is removed by identity, without a
  recently-closed entry and without the last-tab window close.
- **Blank identity.** `Tab::blankIsHome`: a script-opened `about:blank` is a visible web document (never `home`, never
  hidden as the native canvas, never overwritten by the new-tab-page refresh); a deliberate host home navigation
  (`Navigate` with an empty address) sets it back. A blank target is revealed at assignment (no HTTP
  NavigationCompleted is needed).
- **Content close.** `WindowCloseRequested` on script-opened targets closes only that tab, deferred by identity, once
  (`CloseReason::Script` never posts the last-tab `WM_CLOSE`: a hidden profile's page cannot close the shared window).
- **Unchanged.** Admission of HTTP(S) and the tab cap (128, hidden-profile behaviour), sandbox refusal, HTTPS-only,
  certificate refusal, permissions, external-scheme prompt, mTLS, adblock, cookies, UA, private partition. No
  `Handled=FALSE` escape, no fake `PaymentRequest`, no new popup blocker or setting. `IsUserInitiated` is read and
  logged only (`POPUP request web user=1`); no new gesture policy.

### Decisions to review

1. **`javascript:` opens.** The engine does not hand the host a `javascript:` URL: it requests `about:blank` and runs
   the script in it, which the host cannot distinguish from an exact blank open (the opener already scripts a blank
   window fully). Observed and recorded (`observed_javascript_open`: a blank page, no `javascript:` document); not
   refusable at this layer.
2. **Debug sandbox opener.** A `window.open` from the internal debug sandbox page keeps its previous behaviour (unlinked
   tab for HTTP(S), nothing for blank): no new linked relationship for an internal origin.
3. **Engine-failure halts unchanged.** A synchronous controller-creation failure, a null controller and a failed
   private-controller setup still end in the existing terminal `Fail` (as for an ordinary tab); the request is settled
   first (`Fail` → `CancelAll` before its dialog). Not runtime-tested (a deliberate halt); covered by the fakes and by
   the settle-before-`Fail` path.
4. **QA hooks.** `BROWSER_POPUP_QA` (script-fail / assign-fail / posthook-fail / delay) is read **only** under the
   existing `--workspace-test` QA flag, like `qaControllerFailure`.

## Tests and evidence (`design/popup-window-target-evidence/`)

Exact source and binary identity: `00-identity.txt`. All runtime checks drive only throwaway copies of a built app
(own data next to the exe), local HTTP/TLS fixtures on 127.0.0.1, CDP over loopback for that copy, and messages posted to
our own window (`WM_APP+1` commands, `WM_COPYDATA qa switch`, `WM_CLOSE`, our own address Edit). No SendInput, no
foreground change, no clipboard, no desktop capture, no personal profile/runtime/config, no WebView2 install.

Binary under test: **dev 0.3.315 built from `b5ea557`** (`browser.exe` SHA-256
`a4d1fc7fa85c8aa85f2f50ced9307f3191612da320f91896a97e477a105185f4`, version.json commit = its source). Baseline: dev
0.3.313 built from the untouched base `01bf9f7` (SHA-256 `eb3883b2…084b6e`). The source commit containing this report adds
only fixture/report changes on top of `b5ea557` (no `src/` change).

| Evidence | Command | Result |
|---|---|---|
| `01-popup-host-fakes.txt` | `python -I tests/popup/run_host_test.py` | **35/0**: exactly-once completion on success and on a failure at every barrier step, failing `ready`, cancellation by opener/target tab, by owner (cold/dispose/engine failure), everything (window close/halt), reentrant cancellation from inside the completion, late steps after cancel, admission (HTTP(S), exact blank; refused: empty, `about:blank#x`, srcdoc, file, data, javascript, debug sandbox, internal, other schemes) |
| `02-target-final.txt` | `python -I tests/popup/popup_target_e2e.py --app <final>` | **11/11**: direct (reference, `opener=true`, nonce hello + ping/ack, opener not navigated); blank + `document.write` visible (`visibilityState` visible, title kept after 800 ms) then delayed redirect with hello/ack; opener-driven navigation of a loaded popup; named reuse (same proxy, one page); `noopener` (null, no opener: engine semantics); `_blank`; child `window.close()` (only its tab, opener sees `.closed`); refused file/data/debug + blank→debug cancelled + `javascript:` recorded; HTTPS-only interstitial in the popup's first load; invalid certificate refused in the popup's first load; ordinary Ctrl+T tab still the hidden native home |
| `03-target-baseline-RED.txt` | same on the base | 8 failures (all popup communication checks; `https_only`/`cert_denied` fail only on the missing reference — the interstitial and certificate refusal run on the base too); `noopener`, `refused`, `home_tab` pass on both |
| `04b-lifecycle-final.txt` | `python -I tests/popup/popup_lifecycle_e2e.py --app <final>` | **8/8**: injected script failure / assignment failure → null, placeholder removed, browser keeps working; post-assignment failure → the assigned target closed, no orphan; pending target closed by the user (Ctrl+W) → null, no orphan; `WM_CLOSE` while pending → clean exit (0.6 s); request held across `qa switch` to another profile → target created and linked in the original, hidden profile (reference, hello); a hidden profile's last tab closing itself → only that tab, the window stays on the shown profile; private window: child shares the private opener's localStorage/cookie sentinel, a normal window of the same copy sees neither and shares its own |
| `04a-lifecycle-final-FIRST-RUN-setup-flake.txt` | first run of the same | kept: `profile_switch_pending` aborted at **setup** (the profile's first navigation to the opener, before any popup, was reported `CONNECTION_ABORTED` and recovered after the fixture's wait; it happened next to the fresh profile's first-run adblock install). The fixture now retries that setup navigation (recorded as `profile_setup_attempts`); two targeted reruns and the full rerun passed |
| `05-lifecycle-baseline-RED.txt` | same on the base | 7 failures (the base has no QA hooks and no linked target); `private_partition` passes on both (storage partitioning does not depend on the opener link) |
| `06-touched-fixtures.txt` | `python tests/popup/run_touched_fixtures.py` | the existing fixtures compiling `main.cpp` or a touched header, with run_all's own command lines: language_settings, bookmarks_ui, address_selection, accessibility, elements_integration — all passed |
| `07-e2e-hosts-compile.txt` | tests/e2e host compile lines | `mtls_host` and `vault_host` (`BROWSER_E2E_FIXTURE_HOOK`) compile; their E2E scenarios were not run |

Repeat runs during development: the target suite passed 3/3 consecutive repeats on an earlier build of the same design
(before the fixture's HTTP/1.1 change); the lifecycle profile group passed 2/2 targeted repeats after the setup retry.


## Limits and first failures (kept)

- **Wallets/Cloudflare unverified**: no checkout was run; `PaymentRequest` stays undefined (WebView2). Google Pay's web
  client can use a popup and Apple's third-party flow a code scan; whether either now completes is unknown.
- **Named reuse, first observed failure**: the first realistic named-reuse run failed once (the reused window's
  navigation reported `CONNECTION_ABORTED` by the engine and showed the error page); it did not recur in the following
  runs, which all passed. The fixture server was then switched to HTTP/1.1 keep-alive (Python's HTTP/1.0 server closes
  every connection, a known source of local `ERR_CONNECTION_ABORTED`). Recorded, not claimed fixed.
- **Same-task double named open** (`observed_same_task_named_reopen`): two `window.open` calls with one name in the same
  task, before the first target exists, end with the engine reporting `CONNECTION_ABORTED` for that window. Recorded only.
- **Private windows and CDP `Page.navigate`**: a private window does not load the local fixture through CDP
  `Page.navigate` (`ERR_ABORTED`) — identical on the unmodified base — so the private case types the address in our own
  address field. Not changed here.
- **Not runtime-tested**: the new-tab-page refresh (`ApplyNewTabPage`) over a blank popup (covered by `blankIsHome`, which
  that refresh reads through `home`); vault script readiness (vault inactive in the fixture); external-scheme and mTLS
  prompts inside a popup (registered before assignment, unchanged handlers; prompts not driven); a deliberate halt.
- **DOM execution while unassigned**: the target has no view until its controller exists and the initial audio
  re-injection is queued; other broadcast `ExecuteScript` paths were not exhaustively audited for the short setup window.
- **Session**: a live popup follows the normal-tab session policy; restoring its URL never restores an opener link.
- **Suites**: the broad `run_all` suite was not run here (root's combined suite); only the fixtures that compile the
  touched code (`06-touched-fixtures.txt`) and both E2E test hosts (compiled, `BROWSER_E2E_FIXTURE_HOOK`).
