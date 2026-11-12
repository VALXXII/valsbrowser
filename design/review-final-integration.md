# Independent final integration review: vault (B03/B04/B05/E8), mTLS test host, stores and A5, PR62 merge points

Branch `VALXXII/Design-missing-elements`, HEAD `7a344e4`. Read-only review. The only file written is this report.
Line numbers refer to HEAD.

## Verdict

**The vault must not be activated yet.** The core (DPAPI per origin, v2 format, `Vault::Commit` fail-closed, `CanFill`,
`FillScript` self-checks) is sound. The integration around it still has the blockers below. Each one is concrete and
small to fix.

Activation blockers, in order:
1. **F1 (High).** In the B05/E8 manager, a second removal can delete a **different account** from the one shown (index shift).
2. **F3 (Medium).** A page can raise the B04 **update** prompt at will, with « Mettre à jour » as the Enter default and no arming delay. One keystroke then overwrites the stored password of an existing account with a value the page chose.
3. **F4 (Medium).** The « Mots de passe » switch (`settings.passwords`) is ignored by the capture. Once the vault is active the engine's autosave is off, so with the switch off the browser still offers to save, against the user's setting.
4. **F5 (Medium).** Failures are silent. A damaged or unknown vault gives no prompt and no message, and the Settings entry shows « 0 ». A failed commit is only logged, and the unsaved entry stays in memory as if it were saved. Fill refusals (D2) are only logged.
5. **F6 (Medium).** V7 is not met as claimed. The submitted password survives unwiped in several host copies, listed in the V7 section.
6. **The visible B03 trigger and its V5 arming are not built yet.** A contract is proposed in section 5.
7. **The D3 private read-only path is not delivered.** Its required guarantees are in section 6.
8. **There is no E2E of capture, save, fill or remove in the dedicated test host** (synthetic profile and secrets only).

There is also a non-vault **High** in the stores. A web page can make the history file read as « damaged » permanently (**F2**). After that, history saving and A5 period erasure fail for good.

## Tests run (from the worktree root, outputs under `build/` only)

| Command | Result | Duration |
|---|---|---|
| `powershell -NoProfile -ExecutionPolicy Bypass -File ./test-password-vault.ps1` | exit 0, `PASS password vault: DPAPI per origin, file, merge, guards, escaping` | 6 s |
| `./test-profile-store.ps1` | exit 0, `PASS profile store: merge, 2 processes, crash, torn reads` | 14 s |
| `./test-e2e-pin.ps1` | exit 0, `PASS e2e pin: negative matrix …` + `ok no fixture pin in build/real-integration-check/browser.exe` | 5 s |
| `./test-elements-integration.ps1` | exit 0, `PASS elements integration: deferrals once, deny by default, sandbox guard and bridge, …` | 59 s |
| `./test-popups.ps1` | exit 0, `46 checks, 0 failures` | 95 s |

None of these exercises F1, F2, F3, F4 or the V7 copies, so all of them pass despite the defects.
No browser was launched. No network, certificate store or Paper was used.

## Findings

| # | Sev. | Area | Where | One line |
|---|---|---|---|---|
| F1 | **High** (inactive) | B05/E8 manager | `src/password-demo.h:81,89,91`; `src/elements-integration.h:981-982` | `remove_(current_)` maps the **current** row index onto the `ids` captured when the list was built. After one removal the rows shift and the ids do not, so the wrong account is deleted |
| F2 | **High** (web-triggerable, active today) | History store / A5 | `src/downloads.h:179-191` (ToUtf8), `:193-209` (FromUtf8), `src/address.h:271-289`, `src/main.cpp:1156`, `src/trace-erase.h:28` | A page title with a lone surrogate is written as CESU bytes, which the strict reader rejects, so `RecordLines != Items()` and the file is « damaged » forever. History is never saved again and the A5 history erase returns Failed |
| F3 | Medium (inactive) | B04 consent | `src/elements-integration.h:917-932`; `src/origin-prompt.h:262` | A spoofed `{browserVault:'capture'}` raises « Mettre à jour » (Enter = default, no arming) prefilled with the existing account, so the page can overwrite a stored password |
| F4 | Medium (inactive) | Settings / consent | `src/elements-integration.h:920,1007`; `src/main.cpp:1726` | The capture ignores `settings.passwords` |
| F5 | Medium (inactive) | Honesty / D2 | `src/elements-integration.h:901-904,922,936-937,954,963,976-978` | Damaged/unknown vault, failed commit and fill refusals are silent (Log only). The Settings entry shows `0` for a damaged vault. `vault` is not rolled back after a failed save |
| F6 | Medium (inactive) | V7 memory | see the V7 section | Several unwiped host copies of the submitted secret (`PageMessage` local, `TakeString` buffer, `Prompt::spec_`, queue/job specs, dialog field and `Modal::Result::values`, `JsonField` growth) |
| F7 | Medium | A5 privacy | `src/profile-store.h:141-145`; `src/main.cpp:1673-1679`; `src/trace-erase.h` | `history.damaged-<stamp>` and `history.damaged-last` (full copies of the history) survive « Effacer les données », the full wipe and « Effacer en quittant » |
| F8 | Low-Med | A5 privacy | `src/elements-integration.h:678` | A period erase (`since>0`) keeps `favicons\<site>.png` of sites visited in the period, a trace of those sites |
| F9 | Low-Med (inactive) | Manager / UI thread | `src/settings-panel.h:475`; `src/elements-integration.h:976` | `VaultInfo()` → `LoadVault()` on every `Input()` build. If the vault is damaged or unknown, each build takes the profile lock (up to 5 s) and re-reads the file on the UI thread |
| F10 | Low (inactive) | Capture DoS | `src/elements-integration.h:922` | When `LoadVault` fails, every page capture message re-takes the lock and re-reads the file on the UI thread (a page can spam) |
| F11 | Low (inactive) | Load | `src/elements-integration.h:901` | `LoadVault` ignores `ReadAll`'s result. A sharing or read error becomes an empty vault, cached for the window's life (`vaultLoaded=true`), so Save is offered instead of Update and the manager shows nothing |
| F12 | Low (inactive) | Format | `src/password-vault.h:159,195` | `Digits` allows 19 digits but `std::stoll` throws `out_of_range` above INT64_MAX. Nothing catches it, so a digest-valid file with `created=9999999999999999999` aborts the process |
| F13 | Low (inactive) | Format (V2-L4 remainder) | `src/password-vault.h:156,166` | `Write` still emits `end\tN\t\n` when the hash fails, and `Commit` renames it in place. The vault is then « damaged » and every later save is blocked |
| F14 | Low (inactive) | Secret fidelity | `src/elements-integration.h:912`; `src/password-vault.h:35` | `JsonField` keeps lone surrogates, `Utf8()` maps them to U+FFFD, so the stored password differs from the typed one and is always reported as « Updated » |
| F15 | Low (inactive) | Manager re-entry | `src/password-demo.h:117`; `src/elements-integration.h:981-986` | `SetReal` replaces `accounts_`/`remove_` even while the surface is visible (`Show` then returns false). Rows on screen and the indexes behind them can then disagree |
| F16 | Low | E2E harness | `tests/e2e/mtls_e2e.ps1:52-57` | The thumbprint reaches the manifest only after certutil and a Check. An interrupt in that window leaves a `status='creating'` manifest without a thumbprint, and recovery then removes nothing (leaked test certificate) |
| F17 | Low | E2E harness | `tests/e2e/mtls_e2e.ps1:62-66` | The marker check runs **before** the host is rebuilt, so it inspects the previous host. On a clean checkout `ReadAllBytes` throws. `:58` is a tautology (`… -or $true`) |
| F18 | Low | mTLS host bound | `src/main.cpp:2407` | `AddWebResourceRequestedFilter(L"*",ALL)` does not cover service-worker or shared-worker requests (SDK `ICoreWebView2_22::AddWebResourceRequestedFilterWithRequestSourceKinds`). Test-only, fixture-controlled page |
| F19 | Low | Relaunch | `src/main.cpp:4150-4151` | `RelaunchArgs` strips from the first `--restore-window` to the next space even inside a URL argument |
| F20 | Low | Sessions | `src/window-sessions.h:80-82` | `OthersOpen` counts only windows with a session **file**. Close window A while window B has not saved yet, and A keeps its session and is restored at the next start |
| F21 | Low (inactive) | Usage trace | `src/elements-integration.h:937,968` | `prototype.log` records « VAULT filled » and « VAULT saved » events (no origin, no secret), which is still a usage trace (D3 spirit) |
| F22 | Info | D1 copy | `src/origin-prompt.h:254,258` | `m.validate=nullptr` on line 254 is overwritten on line 258. In D1 mode an empty password shows « Saisissez un identifiant. » |
| F23 | Info | navStamp | `src/elements-integration.h:342` | `const unsigned stamp=t->navStamp` truncates the `uint64_t` (harmless in practice) |

### F1 (High): the manager removes the wrong account
`ShowVaultManager` builds `rows` and `ids` in the same order and captures `ids` **by value** in `remove(i)`.
`Demo::OnButton(ConfirmButton)` calls `remove_(current_)` and then `accounts_.erase(begin+current_)` (`password-demo.h:89-91`).

Scenario: rows [A,B,C]. The user removes A: `ids[0]`=A is gone and the rows become [B,C]. The user then opens C
(`current_`=1) and confirms: `remove_(1)` → `vault.Remove(ids[1])` = **B**. The commit succeeds, so B is deleted on disk
and C is erased from the display. The screen shows [B] while the vault holds [C]. The secret is irrecoverable: no backup
is kept on a normal write.

Minimal fix: carry the id in the row (`Account::id`) and call `remove_(accounts_[current_].id)`. Alternatively, erase
`ids[i]` inside the callback on success, with `ids` in a `shared_ptr<vector>`. Also refuse `SetReal` while `Visible()` (F15).
Add a test: remove the first row, then the last.

### F2 (High): a page can lock the history file forever
`History::Write` uses `Downloads::ToUtf8`, which encodes an unpaired surrogate as a 3-byte sequence (`ED A0 80`).
`FromUtf8` correctly rejects code points D800–DFFF, so `History::Read` skips that line. `SaveHistory`
(`main.cpp:1156`) then flags the file damaged (`RecordLines != Items().size()`) with `writeOverDamaged=false`: it is backed up and
**never rewritten**. `TraceErase::History` (`trace-erase.h:28`) returns Failed, so the A5 history erase fails with
« L'effacement n'a pas abouti » every time.

Trigger: any page running `document.title='\uD800'`. The title reaches `history.SetTitle` through
`DocumentTitleChanged` (`main.cpp`, the title callback). Chromium keeps titles as UTF-16 and `document.title` keeps
unpaired surrogates. This should be confirmed with one E2E in the test host, but the writer/reader asymmetry is
certain in code. Bookmarks already handle this case (`bookmarks.h:73-76`, `\uXXXX` escape). History and downloads do not.

Minimal fix: make `ToUtf8` map unpaired surrogates to U+FFFD, or use the bookmarks `Escape` for titles. Add a
round-trip test with `L"\xD800"` in the title, the URL and the download path. For files already locked, offer recovery
(see F7 for the backup copies).

### F3 (Medium): a page-forged update prompt
A page can post `{browserVault:'capture',user:'',password:'x'}` whenever it likes. The engine's `Source` is correct, so this
is limited to the page's own origin. With one stored account, `VaultCapture` builds `PasswordUpdate` prefilled with that
account (`:928`). « Mettre à jour » is `isDefault=true` (`origin-prompt.h:262`) and there is no input arming. A user
pressing Enter at the moment the prompt appears replaces the stored password with the page's value. The old blob is not
kept anywhere. After « Plus tard » the page can repost at once, which gives an endless prompt loop (one at a time per tab).

What the page cannot do: read any secret, learn whether accounts exist (no feedback crosses the bridge), or target
another origin.

Minimal fix:
- (a) give Save and Update no Enter default, and ignore input for 500 ms after show (the same arming as section 5);
- (b) after Dismiss or Later, swallow further captures for that tab until its next top-level navigation;
- (c) optionally, for an Update, require the captured user to be non-empty or require an explicit account choice even with one account.

### F4 (Medium): the « Mots de passe » switch is not honoured
`VaultCapture` checks `Active/privateWindow/smoke` only. `RegisterElementEvents` installs the capture script whenever the
vault is active. Once the vault is active, `ApplyPrivacySettings` turns the engine autosave off whatever the switch says.
With the switch **off**, the browser still prompts to save, which contradicts the user's setting.

Minimal fix: return early in `VaultCapture` when `!settings.passwords`. Install the script only when the switch is on, or
check the switch at message time, which is simpler and handles live toggling.

### F5 (Medium): silent failures and memory drift
- `LoadVault()` failing (damaged/unknown/busy) → the capture shows nothing (`:922`) and `ShowVaultManager` returns silently
  (`:978`). Meanwhile `VaultInfo` returns `{true,0}`, so the entry reads « 0 » and opening it does nothing, which is an inert
  action. With the engine autosave off (vault active), passwords are then saved **nowhere** and nobody is told.
- A failed `Vault::Commit` after « Enregistrer » is only logged (`:937`). `vault` keeps the new entry, not rolled back
  (unlike the manager at `:984`). The manager and fill then show an account that is not on disk. A later manager-removal
  failure does `vault=vaultBase`, which silently drops that consented entry.
- Fill refusals use `RefusalText` only in the log (`:954,963`), so D2's « short state on explicit request » never reaches the
  user.

Minimal fix: a status state for « coffre endommagé / format inconnu / profil occupé » (in E8 and in the save/fill prompt
area), `vault=vaultBase` plus a visible « Non enregistré » on commit failure, and `RefusalText` shown in the B03 popover.

### F7 (Medium): the damaged-history backups survive every erase
`CommitWith` writes `path.damaged-<stamp>` and `path.damaged-last`. Each is an exact copy of the history or downloads file.
Neither `WipeLocalTraceFiles`, `TraceErase::*`, `ClearOnExitAndClose` nor the « Effacer les données » confirmation deletes
them. Combined with F2, the full browsing history stays on disk after the user has « erased » it.

Fix: on history or downloads erase, delete `HistoryFile()+L".damaged-*"` (and the downloads equivalent), or say explicitly that a damaged copy is kept.

### F8 to F23
The table rows give the scenario and the fix. Notes:
- F9/F10: cache the failed state (with the file's last-write time) and re-check only on explicit user action.
- F12: change `Digits` to ≤ 18 digits, or catch `out_of_range` in `Read` and flag damage.
- F13: `Write` must fail (or `acceptable` must check that the digest is non-empty) so that `Commit` never renames such a file.
- F16: save the manifest right after `$m.thumbprint=…`. In recovery, when `status -eq 'creating'` and the thumbprint is empty, look up the
  exact unique `subject` (it carries the run id) and remove that one.
- F17: move `Has-Marker` after the build, or guard it with `Test-Path`.

## Section A, vault checks against the coordinator's rules

| Rule | State | Evidence |
|---|---|---|
| Origin from the ENGINE (message Source), never from the page | **Sound** | `VaultCapture` derives `origin` only from `source` (`a->get_Source`, `:991`); the page's JSON carries only `user`/`password` |
| https only | **Sound** | `CanonicalOrigin` returns "" for non-https, and a refusal follows |
| Top-level only (iframes refused) | **Sound in practice** | `ICoreWebView2::WebMessageReceived` receives top-level messages, and the script also checks `window.top!==window` (page-tamperable, but not needed). Fill: `ExecuteScript` on the top document plus `ancestorOrigins`. `CanFill`'s ThirdPartyFrame check is degenerate (frameSource=topSource=`t->uri`, frameId=0), which is acceptable because injection is top-only |
| Consent before any write | **Sound** | `vault.Save` and `Commit` run only in the `Action::Save` branch. Caveat: the consent can be tricked (F3) |
| One prompt per tab | **Sound for save** (`vaultPromptOpen`, erased on every answer, including tab close via `AbortTabPrompts`). Fill prompts are not deduplicated (low) |
| No vault access from a page message alone | **Partial** | No decryption and no write. But a page message triggers `LoadVault` (lock + file read) and `vault.For(origin)` to choose Save/Update and list usernames in the native prompt. Reads only, but page-initiated (see F10) |
| Target checked at action and right before injection | **Sound** | `atAction` = {navStamp, `t->uri`} at `OfferVaultFill`; `CanFill` at choice; `FillScript` re-checks `location.origin`, `self.origin`, `isSecureContext` and `ancestorOrigins`. `navStamp` is incremented on `NavigationStarting` **and** `SourceChanged` (`main.cpp:2369,2371`), so a pending navigation, pushState or fragment change counts as stale (safe direction). Recommend also refusing when `t->https.shown` or `t->errorPage.shown` is set (the bar URI then masks a data:/about: document; FillScript still refuses) |
| Pending capture vs navigation | **By design**: the save prompt is not aborted by navigation (only Permission/ClientCertificate are, `:330-331`), because a login submit navigates. It saves under the **capture** origin, which is correct. Risk: the prompt names `https://a` while the bar shows `https://b` after a redirect (copy is honest; consider emphasising the origin) |
| No secret in logs/files/metadata | **Sound** | Log lines carry no user, origin or secret. The file and its backups hold DPAPI blobs only. Temp `.tmp<pid>` files hold blobs only |
| No usage metadata | **Mostly** | Entry has created/updated only, and fill changes nothing. But the log records fill and save events (F21) |
| Host copies wiped (V7) | **Partial**: see V7 below |
| Private window never captures | **Sound** | `VaultCapture` returns before parsing when `privateWindow`. Script not installed in private. `Log` is silent in private |
| Engine autosave off only once active | **Sound** | `put_IsPasswordAutosaveEnabled(settings.passwords&&!privateWindow&&!Vault::Active())` |
| Manager never decrypts | **Sound** | Rows have an empty password, `pass.reveal=!real_`, and no `Reveal` call in the manager |
| No « Jamais » inert action (real path) | **Sound** | `origin-prompt.h:261-264` |
| D1 explicit choice | **Sound** | Several accounts plus an empty user → radio rows, « Mettre à jour » disabled until chosen, `Map` returns the chosen user. One account → prefilled and editable. Copy issue F22 |

### JsonField and spoofed capture messages
- In engine-produced JSON, a structural `"key":"` cannot occur inside a string value, because inner quotes are `\"`. A page can still
  put decoy keys in nested objects or choose the key order, but it is the author of the whole message anyway. Parsing
  therefore gives the page **nothing beyond what it could post directly**. `\uXXXX` uses `wcstoul`, which tolerates signs, whitespace and
  partial hex. That is harmless for engine JSON. Surrogate pairs come through as two UTF-16 units (correct). Lone surrogates: F14.
- When the page posts a **string** (`TryGetWebMessageAsString` succeeds), the raw string is parsed the same way, with the same conclusion.
- What a spoofed capture achieves: a native save or update prompt for **its own** origin, carrying page-chosen data (F3), repeated
  prompts, and a lock plus file read per message when the vault is unusable (F10). It cannot read secrets, learn whether accounts exist, or target
  another origin.
- The capture script runs in the page main world: the page can suppress it (stopImmediatePropagation on a window capture
  listener) or forge it. That is acceptable, because nothing trusts it.

### LoadVault on damaged or unknown files
An unknown header gives a refusal plus a log line. Damaged gives a refusal and no write (`Read` + digest). A 0-byte existing file is
treated as a new vault, but `Commit` refuses it (V2-M1), so the file is safe while saves keep failing silently (F5). An absent file
is a new vault. A read error is treated as an empty vault (F11).

### ShowVaultManager rollback (`vault=vaultBase`)
This is correct only if `vault==vaultBase` before the removal, which is false after a failed save (F5). `Commit` updates
`base`/`mine` only on Written/Unchanged, so the rollback restores the last on-disk state. The real defect here is F1.

### FillScript targeting
Top document only. `activeElement.form` is required and visible, enabled fields only (V4). All values go through `JsonString`
(`" \ < > U+2028/2029`, controls). Because the script runs in the page main world, a hostile same-origin page can hook
`Object.getOwnPropertyDescriptor`, `Array.prototype.find` or `dispatchEvent` and receive the password even with no visible form. This is
inherent: the header comment already says the value is readable by the page. The real boundary is « same origin as the
account », and that holds (`location` is unforgeable).

## V7: what is still left in host memory (submitted password; decrypted secret)
1. `PageMessage` local `message` (`:992-996`): `VaultCapture` takes it **by value** and wipes its copy. The caller's
   copy is destroyed unwiped. Fix: pass by reference.
2. `TakeString` (`:47`) frees the CoTaskMem buffer of the message without zeroing it.
3. `JsonField` `out+=` growth leaves partial copies in freed blocks. Fix: `out.reserve(json.size())`.
4. `OP::Spec::secret` is copied into the queue `PromptJob.spec`. `originPrompt.Show(promptActive.spec,…)` takes it **by value**
   and stores it in `Prompt::spec_`, which **keeps the secret after the prompt closes** until the next prompt.
   `FinishPrompt` destroys `job.spec` unwiped.
5. `Build()` copies the secret into `Modal::Field::value`, then into the dialog's edit control (Win32 EDIT buffer) and
   into `Modal::Result::values`, none of which is wiped. `Prompt::Show` wipes only `out.password`.
6. Fill: `JsonString(secret)` temporary and the `L"…"+…` concatenation temporaries (`password-vault.h:290`). `script` is wiped, but
   its earlier buffers are not. `ExecuteScript` copies into WebView2 IPC and renderer memory (uncontrollable).
7. MSVC/libstdc++ small-string moves leave ≤7 characters in the moved-from object.
8. Already wiped: `Protect` plain, `Unprotect` plain and DPAPI out buffer, `Save` comparison `old`, `VaultCapture` `secret` and
   `message`, `r.password` (twice), the demo passwords.

## Section B: mTLS test host guards
- `BROWSER_E2E_FIXTURE_HOOK` is defined only in `tests/e2e/mtls_host.cpp:4` and `tests/e2e/e2e_pin_test.cpp:4`.
  `tools/release/build.py` builds `src/main.cpp` with no such define (only version macros, `:71-81`). `e2e-fixture-pin.h`
  raises `#error` without it. `test-e2e-pin` confirms the dev binary has no marker. **Sound.**
- `E2EAllowedUri`: allows `about:` and `data:`. Everything else is allowed only after `e2eCleared` and only for the exact `https://127.0.0.1:<port>`
  prefix followed by `/ ? #` or end of string. Default-port, case and userinfo variants fail closed. **Sound.**
- `NavigationStarting` plus `FrameNavigationStarting` cancel. `WebResourceRequested` gives a 403 response, and if the response
  cannot be installed the host calls `E2EHostAbort` → `TerminateProcess(…,41)` inside the callback, so the paused request is never released.
  **Sound.** Gap: F18 (worker requests).
- `ClearServerCertificateErrorActions` gating: nothing reaches the fixture before the completion handler sets
  `e2eCleared` (on success only). A failure, or an injected failure, aborts. `e2eCleared` is process-global, so later tabs rely on the first
  clear (decisions are profile-wide, so this is acceptable). **Sound.**
- `mtls_e2e.ps1`: exact-thumbprint cleanup with subject verification and `-DeleteKey`, key-file presence check, recovery manifest,
  exclusive lock file, host rebuilt from sources (P-M1). Defects F16 and F17. Removing the `CurrentUser\My` entry by thumbprint is correct.
  The final state is compared by joined thumbprint order, which can produce a false FAIL but never a false PASS.

## Section C: stores and A5
- `CommitWith`: known-header gate (UnknownFormat, nothing written); 0-byte existing file refused when `!writeOverDamaged`
  (V2-M1); damaged → exact backup read back and compared before any write, `.damaged-last` dedupe; `acceptable` refusal
  (V2-M2); base/mine assigned only after a successful write. `WriteAtomic` retries the rename on sharing, access or lock violations (≤ 1 s,
  under the lock, on the UI thread). **Sound**, apart from F7 and F13.
- `RecordLines` against the readers:
  - **History** (`Read` limit 1000, skips duplicates by `Key`, non-recordable URLs and malformed UTF-8): our writer never
    emits more than 1000 lines, a duplicate key or a non-recordable URL. It **does** emit invalid UTF-8 for lone surrogates (**F2**,
    false positive = saves blocked forever).
  - **Downloads** (`ReadHistory` skips `started<0`, an empty path, a bad state or bad UTF-8; limit 50 = writer limit): consistent except
    for lone surrogates in path or URI (unlikely from the engine) and a negative `started` (a file timestamp before 1970). Both are low.
  - **Settings**: no damage detection (key=value merge), so no false positive.
  - **Bookmarks**: their own `Read` with a trailer, plus the lone-surrogate escape. Consistent.
  - The count rules are consistent between `RecordLines` and the readers: empty lines and lone `\r` lines are skipped by both, a last line
    without `\n` counts twice (cut file), and CRLF works.
- `DamagedStampForTest` is test-only (empty in production, so a time stamp is used). **Sound.**
- `trace-erase.h`: a deletion delta under the lock on the current file, a refusal on damage or unknown format, base/mine updated only after the
  write. **Sound** (F2 makes it fail, F7 and F8 leave traces).
- `window-sessions.h`: `Release` decides under the lock and closes the live handle inside it (deterministic). `Adopt` never adopts
  without the lock, and reservations are held for the process life. **Sound** (F20 low).
- `WipeLocalTraceFiles`, `ClearLocalTracesSelected` and `StartClearSelected`: the local failure is propagated to the A5 result (never
  « effacé »), and engine categories without a profile give `E_NOINTERFACE`. **Sound**, apart from F7 and F8.

## Section D: PR62 merge points
- `WM_CLOSE` (`main.cpp:4051`): `Release(keep=closeWithoutPrompt||!sessionReady)` keeps the session for the update handoff,
  the engine relaunch (`:4175` sets `closeWithoutPrompt`) and a window that never loaded its session. A repeated `WM_CLOSE` during the exit
  clear is harmless (`windowLive` is already null). **Sound.** Note: with « Effacer en quittant » on, the tab URLs are still saved in the session
  file by design, because restoring tabs is a separate setting.
- `RelaunchArgs`/`StartRelaunchedInstance`: `--private` is preserved and the normal window re-adopts its own id. **Sound** (F19 low).
- `Log`: serialized by a static mutex, and silent when `privateWindow`. `privateWindow` is set first thing in `wWinMain`, before the first
  `Log`. **Sound.**
- `EngineReady`/`RestoreSession` once: `webview2-runtime.h:338` (`readyFired_`). **Sound.**

## Re-check of earlier corrections (not trusted, re-verified in code)

| # | Claimed | Re-check |
|---|---|---|
| V2-M1 | Fixed | **Confirmed** in `CommitWith:130-131`. `LoadVault` still shows an existing 0-byte file as a new empty vault (display only; saves fail silently, F5) |
| V2-M2 | Fixed | **Confirmed**: `Merge` never truncates, `acceptable` refuses over the limit, `Save` refuses at the limit |
| V2-M3 | Fixed | **Confirmed**: `Vault::Commit` is the only vault write path (`ProfileStore::Commit<Vault::Store>` appears nowhere else). Header v2, `writeOverDamaged=false`, limit check |
| V2-L1 | (dedupe fixed) | **Partial**: `.damaged-last` dedupe is done; there is still no distinct `Damaged` result |
| V2-L2/L3 | Fixed | **Mostly**: strict shapes are in place. Residual: leading zeros accepted, non-canonical base64 tail bits accepted, and F12 (19-digit overflow throws) |
| V2-L4 | (read side) | **Partial**: `Read` refuses an empty digest, but `Write` can still produce one (F13) |
| V1 | Fixed | **Confirmed** (`FillScript` origin, opaque, secure-context and ancestor checks; `location` is unforgeable) |
| V2 | Fixed | **Confirmed** (top-only injection plus ancestors) |
| V3 | Fixed | **Confirmed** (`Save` and `Read` refuse "" and non-canonical origins) |
| V4 | Fixed | **Confirmed** (no document fallback; hidden and disabled fields skipped) |
| V5 | Open | **Open**: contract proposed in section 5. Also applies to the B04 prompts (F3) |
| V6 | Fixed + policy at B04 | **Confirmed** (edit survives a remote delete; same account → newest). The policy is now backed by explicit B04 consent |
| V7 | Partial | **Partial, worse than described**: see the V7 section (prompt spec and `PageMessage` copies) |
| V8 | Fixed | **Confirmed** (RNG failure → `RandomId()=0` → refused; count over the limit → damage) |
| S1 | Fixed | **Confirmed** (`main.cpp:1123` assigns `sessionFile=WindowSessions::File(…)`) |
| S2 | Fixed | **Confirmed** (deterministic `Release`). F20 residual |
| S3 | Fixed | **Confirmed** for vault, history, downloads and bookmarks. **New false-positive source F2** |
| S4 | Fixed | **Confirmed** (final-path key, `Adopt` under the lock). A second Windows logon session still gets its own `Local\` namespace (documented as unverified) |
| S5 | Fixed (A5 delta) | **Confirmed** for the files. Other windows' memory is reconciled at their next commit. F7 and F8 residual |
| P1 | Fixed | **Confirmed** (`ApplyPrivacySettings`: private → autosave and autofill FALSE) |
| P2 | Fixed | **Confirmed** (`ForgetSiteIcon`/`ClearSiteIcons` have no disk effect in private, `main.cpp:1205-1206`) |
| P3 | Fixed | **Confirmed** (`LoadDownloads` returns in private) |
| P-M1 | Fixed | **Confirmed** (host rebuilt in stage 4). But the marker check precedes the rebuild (F17) |
| P-L1 | Fixed | **Confirmed** (comment reworded) |
| P-L2 | Partial → now guarded | **Confirmed improved**: `ClearServerCertificateErrorActions` gating plus exact-origin bound on navigation, frame navigation and requests, fail-closed abort. Residual F18 |
| P-L3 | Open | **Open** (`atoi`, last duplicate wins; fail-closed through the exact pin) |
| P-L4 | Fixed | **Confirmed** (test-e2e-pin negative matrix PASS) |
| P-I1 | Fixed | **Partial**: `test-e2e-pin` checks `build/real-integration-check/browser.exe`, not the release artifact produced by `tools/release/build.py` |

## 5. Proposed contract: the VISIBLE native B03 fill command (base: proposal 11)

**Trigger and visibility.** A native key control in the address bar, right of the origin indicator. It is a chrome region and a UIA
button named « Remplir un mot de passe enregistré ». It is shown only when all of the following hold:
- `Vault::Active()`;
- the tab is active and not `halted`;
- `t->home`, `t->sandbox`, `t->https.shown` and `t->errorPage.shown` are all false;
- `CanonicalOrigin(t->uri)` is non-empty;
- the last top-level `NavigationCompleted` succeeded with the current `navStamp`;
- `vault.For(origin)` is non-empty and the vault loaded cleanly.

Visibility is recomputed on `SourceChanged`, `NavigationCompleted`, tab switch, vault commit and Settings change. A page message never shows
it. A page « field focused » hint (if added later) may only **position a secondary native affordance**. That affordance opens the same
popover anchored to the address-bar control, and it never carries identity, origin or consent. Fallback and default anchor: the
address-bar control.

**Arming against click-jacking (V5).** The control and the popover's actionable rows ignore activation until they have been
**visible, stationary and in a foreground window for ≥ 500 ms**. The timer resets on show, on any move or resize, on tab switch, on
`navStamp` change, on window (re)activation and when the popover opens. A pointer press that began before arming, or outside the
control, never activates it (pointer-down and pointer-up must both fall inside and after arming). The same rule applies to keyboard
activation: a key held down (auto-repeat) or pressed before arming is ignored. In the popover, no button is the Enter default until
armed, and initial focus is on the list (no account preselected). The same arming applies to the B04 Save and Update prompts (F3).

**Focus and keyboard path (no new shortcut).** The control is in the toolbar's existing Tab and F6 order next to the origin
indicator. Space or Enter activates it. In the popover:
- Up and Down move between accounts;
- Enter or Space on a focused row fills;
- « Gérer les mots de passe » is reachable by Tab;
- Échap closes and returns focus to the page (to the element that had it, which `FillScript` needs as `document.activeElement`).

**Guards at click** (`OfferVaultFill`, extended):
- all the visibility conditions above, re-evaluated;
- `get_Source()` read from the engine now and equal to `t->uri`;
- `atAction = {tab serial, navStamp, canonical origin}`;
- `userAction=true` only from this native control's input path (never from a page message, a script or the sandbox bridge);
- the vault is loaded and not damaged, otherwise a visible short state;
- no decryption here.

**Guards at choice** (prompt answer, before `Reveal`):
- the tab is alive, still the active tab, the window is foreground and the prompt was not aborted;
- `navStamp` is unchanged;
- `CanonicalOrigin(get_Source())` equals `atAction.origin`, which equals `entry.origin` (also checked by id);
- no interstitial or error page is shown;
- the entry id still exists with the same origin;
- then `Reveal`, `FillScript` (in-page re-check), `ExecuteScript` on the top document only, and wipe;
- any refusal shows `RefusalText` in the popover (D2), never just a log line.

## 6. D3: what a safe read-only private path must enforce
Today `OfferVaultFill` and `ShowVaultManager` refuse `privateWindow`, which is stricter than the contract. To deliver
explicit READ and FILL in private, the private path must:
1. **Never capture**: no `VaultCaptureScript`, and `VaultCapture` keeps returning early (already so).
2. **Never write**: no `Save`, no `Remove`, no `Vault::Commit` from a private process. Enforce this inside a single wrapper
   (`if(privateWindow) return Failed`), not only in the UI. The manager in private shows **no delete action** (or a disabled
   one with « Disponible dans une fenêtre normale »), so there is no inert action.
3. **Never create any file**: not even `.damaged-*` backups or `.tmp` files, which only `Commit` writes, so rule 2 covers this. Reading is done
   under the profile lock with `ReadAll`, and a damaged or unknown vault is reported, never repaired.
4. **Keep no usage metadata or log**: no updated/used timestamps, no log line (`Log` is already silent in private), and no
   « recent account » ordering stored.
5. **Use the same B03 guards and arming** as section 5, the same `FillScript` in-page checks and the same wiping. Keep no decrypted secret
   beyond the fill and cache no plaintext.
6. **Use a fresh snapshot**: re-read the file at each explicit open instead of caching `vaultLoaded` for the process life, so a removal in a
   normal window is honoured.
7. **Keep the engine autosave and autofill FALSE in private** (P1, already so), so the filled form is not saved by the engine
   into the private profile.
8. **Label honestly**: the B05 subtitle in private says the list is read-only, and the Settings changes made in private stay transient
   (not persisted), as the plan notes.

## Sound (checked, no defect)
- DPAPI entropy is bound to the origin, so a blob moved to another origin's line fails to decrypt.
- `CRYPTPROTECT_UI_FORBIDDEN` is set, and crypto comes from System32 only (`LOAD_LIBRARY_SEARCH_SYSTEM32`). A missing entry point makes the operation fail closed.
- Ids are random 64-bit. A file that is damaged, has an unknown version or is over the limit is never written.
- `CanonicalOrigin` rejects userinfo, non-https and odd hosts, and elides the default port.
- `JsonString` escaping of `"`, `\`, `<`, `>`, U+2028/2029 and controls.
- `VaultCapture` is swallowed while inactive, so nothing is kept.
- No secret in any log line. The manager never decrypts. There is no « Jamais » on the real path.
- The E2E hook is absent from the dev and release builds. The mTLS host guards fail closed.
- `Release`/`Adopt` determinism. `Log` serialization and private silence. `EngineReady` runs once.

---

## Corrections (intégrateur, après ce rapport) — à recontrôler par une revue indépendante sur le SHA intégré

| # | État | Correction | Preuve |
|---|---|---|---|
| F2 (High, live) | Corrigé fe270aa | `ToUtf8` écrit un demi-substitut isolé en U+FFFD : un titre de page ne rend plus l'historique « endommagé » | E2E-store : aller-retour d'un titre `\uD800`, effacement possible |
| F7 | Corrigé fe270aa | tout effacement explicite supprime aussi les copies `.damaged-*` du fichier | E2E-store |
| F8 | Corrigé fe270aa | effacement par période : favicons des sites sortis de l'historique oubliées | revue de code |
| F1 (High) | Corrigé 8237c2a | chaque ligne B05 porte l'id du coffre | U : A puis C supprime exactement A et C |
| F3 | Corrigé 8237c2a | prompts identifiants armés 500 ms (premier plan) ; mise à jour sans Entrée par défaut | U |
| F4 | Corrigé 8237c2a | la capture respecte l'interrupteur « Mots de passe » | revue de code |
| F5 | Corrigé 8237c2a | coffre illisible, échec d'enregistrement (mémoire ramenée au fichier), refus de remplissage : affichés ; Réglages « Illisible » | revue de code + E2E (chemins nominaux) |
| F6 / V7 | Partiel 8237c2a | copies effacées : chaîne de l'appelant, tampon moteur (`TakeString`), secret du prompt, valeurs du résultat et des champs de la modale ; restent hors maîtrise : chaînes internes WebView2 et allocations temporaires de la STL | — |
| V5 / §5 | Corrigé dde6765 | contrôle natif visible (id 906), conditions de visibilité, armement 500 ms (réarmé onglet/navigation, premier plan), source moteur revérifiée au clic et au choix, refus affichés | U (armement) ; **E2E coffre synthétique** `tests/e2e/vault_e2e.ps1` (hôte de test : condition « premier plan » levée là seulement, armement conservé) |
| §6 D3 privé | Corrigé dde6765 | lecture/remplissage explicites en privé, jamais de capture, jamais d'écriture (`Vault::WritesForbidden` dans le seul chemin d'écriture), relectures fraîches, gestionnaire en lecture seule | E2E : remplissage privé OK, aucune invite de capture, fichier du coffre inchangé |
| Capture perdue à la navigation | Corrigé dde6765 | message aussi conservé dans le sessionStorage de la même origine et renvoyé une fois par le document suivant (id unique par soumission) | E2E : capture → B04 → enregistrement |
| V2-L2 / V2-L4 / P-L3 / P-L2 | Corrigés c86bbcf | nombre hors plage = dommage ; pas d'écriture sans digest ; manifeste strict ; requêtes des workers couvertes par le filtre de l'hôte de test | U ; mTLS E2E PASS |
| Script mTLS | Corrigé c86bbcf | empreinte écrite au manifeste dès la création ; marqueur vérifié après reconstruction de l'hôte ; vrai contrôle de non-exportabilité | mTLS E2E PASS |
| P-I1 | Partiel | le marqueur est vérifié dans le binaire dev ; un build `release` n'est pas reconstruit par le test | — |

Le coffre reste **inactif** en dev/release. Activation : seulement après recontrôle indépendant de ce SHA intégré (déclencheur visible inclus).
