# Independent security review: password vault core, private window, interprocess store

Branch `VALXXII/Design-missing-elements`, HEAD `ac1e1a5`. Read-only review of the source. Nothing was built or run.
Line numbers refer to HEAD.

Verdict: **the vault must stay inactive.** Two of its fill guards have design gaps (V1 and V2) and `Save` breaks the
https-only rule (V3). Separately, one high-severity bug in shipped code (S1) means the per-window sessions are not actually in use.

## Summary of findings

| # | Sev. | Area | Where | One line |
|---|------|------|-------|----------|
| S1 | **High** | Store / sessions | `src/main.cpp:1106` | `sessionFile=` assignment is inside a `//` comment: every window still reads/writes the single legacy `session` file; per-window sessions, handover and relaunch restore nothing |
| V1 | **High** (design, vault inactive) | Vault fill | `src/password-vault.h:174-181, 204-210` | Check-then-inject TOCTOU: the script carries no origin assertion, so a navigation committed between `CanFill` and execution receives the secret. An intermediate cross-origin frame can trigger that navigation on purpose |
| V2 | Medium (design) | Vault fill | `src/password-vault.h:172-181` | Only frame and top are compared. Intermediate ancestors and sandboxed/opaque-origin documents served from the account's URL are accepted |
| V3 | Medium | Vault store | `src/password-vault.h:109, 145` | `Save(L"")` and `Read` accept the empty origin (`CanonicalOrigin(L"")==L""`). Credentials from any non-https page (whose canonical origin is `""`) get saved and pooled together |
| P1 | Medium (likely, verify on runtime) | Private | `src/main.cpp:1684-1691` | Private webviews get `IsPasswordAutosaveEnabled` and `IsGeneralAutofillEnabled` from the normal settings. Chromium saves incognito passwords (and addresses/cards) to the parent profile, here the on-disk `browser-private` profile |
| S2 | Medium | Store / sessions | `src/window-sessions.h:114-117`, `src/main.cpp:3932` | Windows closing together (e.g. taskbar "Close all windows") each still see the others as live, so all of them delete their session files |
| S3 | Medium | Store / vault | `src/profile-store.h:101-107` and every model `Read` | A damaged or unknown-version file merges as an empty file: entries in `base` count as "deleted elsewhere" and the file is rewritten without them. Vault, history and downloads keep no aside copy (only bookmarks do, at load) |
| P2 | Low | Private | `src/main.cpp:1188-1189, 1648-1652` | `ClearSiteIcons` / `ForgetSiteIcon` are not private-guarded: a private window deletes the normal profile's favicons, and "Effacer les données" in a private window says the data is cleared when the normal profile's history and downloads are not |
| P3 | Low | Private | `src/main.cpp:1295` | `LoadDownloads` is not private-guarded: the private window shows the normal profile's download list |
| V4 | Low | Vault fill | `src/password-vault.h:204-209` | The field choice falls back to `document` when the focus is not in a form, so the first password field anywhere in the document gets filled (including hidden fields). The username selector takes the first text input |
| V5 | Low | Vault fill (UI) | design | No guard against a page making the native fill control appear under the pointer just before a click |
| V6 | Low | Vault merge | `src/password-vault.h:154-164` | A password changed here but deleted in another window is silently dropped (no resurrection, but the user's newest credential is lost). Concurrent saves of the same account create duplicates, and `Save` then updates only the first |
| V7 | Low | Vault memory | `src/password-vault.h:80, 123, 194-210` | Secret copies stay in freed heap: `secret.clear()` without wiping, `JsonString`/`FillScript` growth and temporaries, the script string passed to `ExecuteScript` |
| V8 | Low | Vault ids/read | `src/password-vault.h:87, 136` | `BCryptGenRandom` status ignored (on failure every id is 1, the file then reads as duplicates and entries are dropped). `Read` stops at `Limit` without setting `damaged`, while `Merge` can exceed `Limit` |
| S4 | Low | Store | `src/window-sessions.h:92`, `src/profile-store.h:25-34` | `Adopt` ignores `lock.Held()`. Path aliases (8.3 names, junctions, subst) and a second logon session of the same user (`Local\`) get a different mutex, so there is no exclusion |
| S5 | Low | Store | `src/main.cpp:1649`, `src/address.h:259` | "Effacer" in one window leaves the history in the other windows' memory (still suggested, still on their home page) until each one commits |

---

## Findings in detail

### S1 (High): per-window session path never assigned
`src/main.cpp:1106`:
```cpp
windowId=a.id;...;windowReservations=a.reservations; // held for this process's lifesessionFile=WindowSessions::File(dataDir,windowId);
```
The assignment `sessionFile=WindowSessions::File(dataDir,windowId);` sits inside the comment. It has been there since `6a1a4c1`. So `sessionFile` stays `<data>\session`, the legacy path.

What happens:
1. Every normal window writes the same `<data>\session` in `SaveSession` (main.cpp:1122-1130). That write happens without the lock and uses the same temp name `session.tmp` in every process. Two windows closing together truncate and write the same temp file at once (`std::ofstream` opens it shared), and a torn or mixed file gets renamed into place.
2. At the next start, `Adopt` → `MigrateLegacy` moves `session` to `sessions\legacy-…`, and the window adopts it. But `RestoreSession` (main.cpp:1252) reads `sessionFile`, which is still the legacy path the file was just moved away from. Result: no tabs are restored.
3. Each start adds one more `legacy-*` orphan. With restoration on, each later start relaunches one more empty window, and the number keeps growing.
4. A handover (`OpenWindowWith`) writes `sessions\<id>`. The launched process then reads the legacy path, so the handed-over URLs never open. They also stay on disk as an orphan.

`tests/e2e/windows_e2e.py` checks only directory listings, not what the windows actually show, so it does not catch this.
**Fix:** move the assignment out of the comment (one line). Add an E2E assertion on the restored URLs, or on the per-window file contents after a close.

### V1 (High, design): TOCTOU between the guard and the injection, which a third party can trigger
`CanFill` is pure and runs on the UI thread with the last frame state reported by the engine. `ExecuteScript` (`ICoreWebView2Frame2`) is asynchronous and runs in the frame's **current** document at the moment of execution. `frameId` survives navigations; only `navigation` changes, and the app learns about the new navigation only when the event arrives on the UI thread. `FillScript` (password-vault.h:204-209) does not check where it is running.

Scenario: top `https://bank.test` → iframe `https://ads.evil` → iframe `https://bank.test/login` (see also V2: the intermediate frame is not checked). The user clicks fill. `CanFill` passes. Right then, the evil frame navigates its child to `https://ads.evil/catch` (a parent may always navigate its child). The script runs in evil's document and `p` ends up in a page of another origin. The same gap exists, without an attacker, for any pending redirect or navigation that commits during the hop.
**Fix:** make the script check itself, atomically in the target document, before touching anything, using unforgeable properties only:
`if(location.origin!==EXPECTED||self.origin!==EXPECTED||!isSecureContext)return;` plus `location.ancestorOrigins` (all equal to EXPECTED, see V2). `EXPECTED` is passed through `JsonString`. `location`/`origin` are `[LegacyUnforgeable]`; a new document cannot pre-patch them. Keep the native re-check too. Add a test that the script contains the origin assertion before any `set(`.

### V2 (Medium, design): ancestor chain and opaque origins
`CanFill` compares the frame with the top only (password-vault.h:178). Two cases get through:
- A same-origin frame under a cross-origin intermediate frame (above) is accepted. That frame's placement and navigation belong to a third party: UI redress, plus the V1 trigger.
- A document whose URL is `https://site/...` but whose origin is opaque (CSP `sandbox` header without `allow-same-origin`, or `<iframe sandbox>` on the account's URL) is accepted. These are exactly the places sites use to host untrusted user content.

**Fix:** `Target` should carry every ancestor origin (from the engine), and every one of them must equal the account origin. In the script, require `self.origin===EXPECTED` (an opaque origin gives `"null"`) and check `location.ancestorOrigins`.

### V3 (Medium): empty origin accepted by `Save` and `Read`
`Save`: `if (CanonicalOrigin(origin) != origin || ...)` (line 109). `CanonicalOrigin(L"")` returns `L""`, so the empty origin passes. The natural caller pattern `Save(CanonicalOrigin(pageUrl), …)` on an `http://`, `file:` or `data:` page therefore **stores** the password (`Saved::Added`) instead of refusing it. It also pools every non-https site's accounts under the single key `""`: `For(L"")` lists them all, and `Reveal(id, L"")` decrypts them. `CanFill` still blocks the injection (NotHttps), but the https-only storage rule is broken and the B05 list or suggestions would mix sites. `Read` has the same hole (line 145): an invalid-UTF-8 or empty origin field decodes to `""` and is accepted.
The test at line 30 checks only `"http://exemple.test"` (not canonical, so refused). It does not cover `""`.
**Fix:** `if (origin.empty() || CanonicalOrigin(origin) != origin …)` in `Save`, `Reveal` and `Read`. Better still, make `CanonicalOrigin` the only way to obtain an origin type. Add tests for `Save(L"")` and `Save(CanonicalOrigin(L"http://x"))`.

### P1 (Medium, needs a runtime check): engine password/autofill save from private windows
`ApplyPrivacySettings` (main.cpp:1684-1691) applies `settings.passwords` and `settings.autofill` to every webview, including the InPrivate ones. In Chromium, the off-the-record profile's password store (and the address/card autofill) redirects to the **original** profile. Here that is the regular `browser-private` profile on disk under the user data folder (`…\EBWebView\browser-private\Login Data`, which holds the origin and username in clear plus the OS-encrypted password). So a password saved in a private window would persist and be offered in later private windows, which breaks "the app leaves no trace".
**Fix:** in a private window, force `put_IsPasswordAutosaveEnabled(FALSE)` and `put_IsGeneralAutofillEnabled(FALSE)` whatever the settings say. When the vault is activated, make `Vault` save and fill unavailable in private windows (or at least save), with a guard like the others. Verify on the Fixed Version runtime that `browser-private\Login Data` and `Web Data` stay unchanged after a private session that submits a login form.

### S2 (Medium): close race deletes every session
`Release` deletes this window's file when `OthersOpen` is true (window-sessions.h:114-117). But `windowLive` is never closed before the process exits (main.cpp:1093/1106; there is no `CloseHandle(windowLive)`). After `Release`, WM_CLOSE still runs `SaveDownloads`, `SaveBookmarks`, possibly the 3 s clear-on-exit, and the WebView2 teardown. During all of that this window still counts as open. With windows A and B closed within that interval (taskbar "Close all windows", or two quick Alt+F4), A sees B live and deletes. B then sees A still live and deletes as well. Nothing is restored at the next start, contrary to "the last one keeps it".
**Fix:** in `Release`, under the profile lock, close this process's live handle (and its reservations) after the decision, whether the file was kept or deleted. Then the last window to release always keeps its own file. For "close all", consider keeping files closed less than N seconds before the last one, for example by renaming them to `closing-<id>` and resolving at the next `Adopt`.

### S3 (Medium): a damaged or newer-format file is read as "everything was deleted elsewhere"
`CommitWith` (profile-store.h:101-107) merges onto `theirs` even when `damaged` is true. With an unknown header (a future `browser-passwords 2` written by a newer build, a truncated file, or a file damaged by an external tool), `theirs` is empty. Every three-way merge (`Vault::Store::Merge`, `History::Merge`, `DL::MergeHistory`, `BM::Store::Merge`) then treats the entries of `base` as deleted by another window. The rewritten file keeps only what is new here. A rollback after an update would thus erase the whole vault on the first commit. Line-level damage drops the bad lines permanently, again with no aside copy, except for bookmarks at load (main.cpp:3009-3013). `History::Read` and `DL::ReadHistory` do not even report damage.
**Fix:** in `CommitWith`, when `damaged` (or the header is unknown): copy the file aside (`<file>.damaged-<time>`, as `LoadBookmarks` does) **before** writing. For the vault, refuse to write on an unknown header (return `Failed`, keep everything in memory). Make every reader report damage.

### P2 (Low): private window touches the normal profile's favicons
`ClearLocalTraces` → `ClearSiteIcons()` (guarded only by `smoke`) deletes `<data>\favicons`. It runs on "Effacer les données" and on clear-on-exit in a private window. `ForgetSiteIcon` (deleting a private history entry) deletes the normal favicon of that site. On top of that, the private window shows "cleared" while the normal history and downloads are untouched (`SaveHistory`/`SaveDownloads` are guarded). This is not a leak, but it is a write to the profile from a private window, plus a misleading confirmation.
**Fix:** `if(privateWindow)return;` before the disk part of both functions. In a private window, word the clear confirmation as covering the private session only.

### P3 (Low): normal downloads shown in a private window
`LoadDownloads` (main.cpp:1295) has no private guard, so the private window lists the normal profile's downloads. Nothing is written (`SaveDownloads` is guarded), but this contradicts "a private window shows no history" (`LoadHistory` comment, point 8). **Fix:** `if(smoke||privateWindow)return;` at the top.

### V4 (Low): field selection
`var f=a&&a.form?a.form:document`. When the focus is not inside a form (body, a shadow host, a frame element), the first `input[type=password]` of the whole document is filled. That can be a hidden field or another form. `input[type=text]` takes the first text input of the form (a search box, for example). Everything stays same-origin, so this is a reliability and least-surprise issue more than a leak.
**Fix:** require `a` to be an `HTMLInputElement` of type password (or the username field of the same form), and fill only within `a.form`. Never fall back to `document`. Skip fields that are not rendered (`!e.offsetParent && getClientRects().length===0`).

### V5 (Low): click timing on the native control
A page can trigger the vault control (focus on a password field) right under the user's pointer and get an unintended "fill" click. Only same-origin accounts can be filled, which limits the impact. **Fix:** ignore activation of the control for about 500 ms after it appears or moves, as Chromium does for its permission bubbles.

### V6 (Low): merge semantics for credentials
- Changed here but deleted elsewhere: dropped (lines 157-158 and 162). This is consistent with "never resurrect", but the user may just have changed the password on the site and our copy of the new one is gone. Suggest: if `mine` differs from `base` **and** `theirs` deleted it, keep it under a new id. That is a fresh explicit consent, not a resurrection of the old value.
- Two windows saving the same origin+user before a commit produce two ids. `Save` then updates only the first, so the old password stays fillable. Suggest de-duplicating by (origin, user) in `Merge`: keep the most recent `updated`, and drop the other only if `base` did not contain it.

### V7 (Low): memory hygiene
- `Unprotect`/`Reveal` call `secret.clear()` (lines 80, 123), which frees nothing and wipes nothing. A reused `std::wstring` keeps the previous secret in its buffer.
- `JsonString` builds `o` by `+=`, and each reallocation frees a partial copy of the secret. `FillScript`'s `operator+` chain creates several temporaries holding the secret. None of them are wiped.
- The script string passed to `ExecuteScript` must be wiped by the caller after the call returns.
**Fix:** `reserve()` the exact size up front (`s.size()*6+2`), build in place, and wipe every intermediate. Provide a `Wipe(std::wstring&)` helper and use it in the fill path. Never log `ExecuteScript*` results or exceptions in the fill path: a page that monkeypatches `Object.getOwnPropertyDescriptor`/`Event` can throw an error containing `v`, and `ExecuteScriptWithResult` would return it.

### V8 (Low): ids and limits
- `RandomId` ignores the `NTSTATUS`. On failure every id is 1; `Read` then rejects the duplicates as `bad`, and they are lost on the next commit. **Fix:** check `BCRYPT_SUCCESS`, and make `Save` return `Refused` on failure.
- `Read` silently stops at `Limit` (line 136) without setting `damaged`, while `Merge` can produce more than `Limit` entries (two windows near the limit). The surplus then disappears on the next commit. **Fix:** enforce `Limit` in `Merge`, refusing new entries rather than truncating, and mark `damaged` when `Read` truncates.
- `Save` refuses an *update* when the store is at `Limit` (the check comes before the update loop). This is minor.

### S4 (Low): lock scope
- `Adopt` (window-sessions.h:92) builds a `Lock` and never checks `Held()`. After a 10 s timeout it adopts, reserves, and with restoration off **deletes** orphans without the lock, and can race another starting process. **Fix:** on a failed lock, take a new id only (no adoption, no deletion).
- `MutexName` uses `GetFullPathNameW` plus lowercase. 8.3 names, junctions, `subst` and UNC paths to the same directory give different mutexes. **Fix:** use `GetFinalPathNameByHandleW` on a handle to the directory.
- The `Local\` namespace is per logon session. The same user running the browser in two sessions (console + RDP) on the same `%LOCALAPPDATA%` gets no exclusion. This is acceptable if documented. Otherwise use `Global\` with an explicit DACL for the current user SID (beware of squatting).

### S5 (Low): clearing does not reach the other windows' memory
`History::Merge` correctly never resurrects, but a window that does not commit keeps the cleared history in memory and keeps suggesting it. **Fix:** after a clear, broadcast a registered window message (or a named event) so other windows reload under the lock.

---

## Checked and found sound
- **DPAPI use**: current-user `CryptProtectData`/`CryptUnprotectData` with `CRYPTPROTECT_UI_FORBIDDEN`; entropy `browser-vault-v1|<canonical origin>`, which is unambiguous (`|` cannot appear in a canonical origin). A blob moved to another origin fails to decrypt (tested). The output buffer is wiped before `LocalFree` and the UTF-8 plaintext is wiped. No plaintext secret or username in the file (usernames are base64, which is encoding, not confidentiality: by design they are not secret). Residual (inherent, document it): any process of the same user can decrypt, since the entropy is not a secret. Blobs can also be swapped between two accounts of the *same* origin by anyone who can write the file, because the entropy binds neither the id nor the username. Optionally add `|<id>|<user>` to the entropy.
- **Base64**: the encoder is correct for 0/1/2 trailing bytes. The decoder is correct for valid input. It is lenient (stops at `=`, ignores trailing garbage and non-zero pad bits), which affects neither safety nor round-trip.
- **CanonicalOrigin**: fails closed on uppercase scheme (lowercased), userinfo (`@` anywhere in authority, including `a:443@evil`), backslashes, `%`, whitespace and control chars, empty host, non-digit ports, and non-https schemes. Default port elided. `#`/`?`/`/` end the authority. Differences from WHATWG (IDN not punycoded, fullwidth not folded, `[::1]:443` not elided, leading-zero ports, a trailing dot kept) all produce a **different** string, which means refusal, never confusion, as long as both sides come from the engine's URLs. The only real gap is the empty string (V3).
- **JsonString/FillScript escaping**: `"`, `\`, `<`, `>`, all C0 controls (including NUL, so no truncation at `LPCWSTR`), U+2028/2029. No way to break out of the string literal. The script's completion value is `undefined`, so nothing comes back through `ExecuteScript`.
- **Bridge principle**: `CanFill` requires `atAction.userAction` and compares frame id, navigation id and the origin at action time against the origin now. Account origin equals frame origin equals top origin. Nothing in the vault reacts to a WebMessage. The vault is not referenced anywhere outside its header and test (`Vault::Active()` false).
- **Vault file and merge**: header check; 6 fields; duplicate and zero ids rejected; written only through `ProfileStore::Commit`. Deletions here are applied; deletions elsewhere are never undone (tested both ways); additions on both sides kept. No usage timestamp, no logging.
- **Private window**: the controller is created through `ICoreWebView2Environment10::CreateCoreWebView2ControllerWithOptions` with `IsInPrivateModeEnabled=TRUE` and `ProfileName=browser-private`. If either fails it **fails closed** (`Fail`, no fallback to the live profile). There is a single creation path (popups become `NewTab` in the same window). Guards present and correct: `Log` (all logging), `SaveSession`, `SaveHistory`, `LoadHistory`, `RememberSiteIcon`, `SaveDownloads`, `SaveSettings`, `ApplyAdBlock` (adblock marker file), `ClaimWindowSession` (no adoption, no live mutex, no session file), `Release` (skipped), `OpenWindowWith` (refused), `RestoreSession` (empty start), update engine (`config.enabled=false`), tab-limit toast action removed, the close modal's « Ne plus demander » removed. `privateWindow` is set before the first `Log`. `PrivateCommandLine` passes nothing but `--private`. Bookmarks written from a private window (D3, voluntary) go through the normal transaction, and `RememberSiteIcon` stays guarded, so bookmarking writes no favicon.
- **Profile store**: `WriteAtomic` writes a complete per-PID temp file, `FlushFileBuffers`, then `MoveFileEx(REPLACE_EXISTING|WRITE_THROUGH)` with a bounded retry; the temp is deleted on failure. So no torn target, and an abandoned mutex is safe (handled via `WAIT_ABANDONED`). `ReadAll` uses full sharing and a 1 GB cap. On `Busy`/`Failed`, `base` and `mine` are untouched, so a retry replays correctly. Mutex names are per profile (FNV-1a 64 of the normalised path), and live/reserve names include the profile hash, so different profiles do not collide. Session ids are validated (`[A-Za-z0-9-]{1,40}`), so `--restore-window` cannot traverse paths, and temp files (`.`) are not listed as sessions.
- **Merges**: `History::Merge` adds only fresh visits and never resurrects a removed page with its old count. `BM::Store::Merge` uses node identity (id, kind, added), so an edit never lands on a reused id, a deleted node is never resurrected, and new ids are remapped. `DL::MergeHistory` keys rows by path, start and uri; removals here are applied and other windows' removals kept. `MergeKeyValues` keeps the other windows' settings and applies only keys changed here.
- **Interprocess bookmarks at load**: a damaged file is copied aside before any rewrite, and `.bak` is restored only when clean.

---

## Corrections (intégrateur, après ce rapport)

| # | État | Correction | Preuve |
|---|---|---|---|
| S1 | **Corrigé** 218fa39 | affectation `sessionFile=` sortie du commentaire ; recherche systématique d'autres `//` avalant du code dans tout le diff de la branche : aucun | `windows_e2e.py` vérifie maintenant le contenu (chaque fenêtre navigue vers SA propre URL restaurée, fichier de la fenêtre restante = ses onglets, aucun fichier `session` hérité) — E2E-app PASS sur runtime Fixed Version |
| V1 | **Corrigé** | `FillScript(origin,…)` revérifie à l'exécution `location.origin`, `self.origin` (origine opaque/sandbox refusée), `isSecureContext`, chaque `location.ancestorOrigins` | U `test-password-vault` |
| V2 | **Corrigé** (même mécanisme côté page) + `CanFill` inchangé côté hôte | idem | U |
| V3 | **Corrigé** | `Save("")` et lecture d'une origine vide refusés (dommage signalé) | U |
| V4 | **Corrigé** | plus de repli sur `document` ; champs cachés/désactivés ignorés | U |
| V6 | **Corrigé** | une modification ici survit à une suppression ailleurs ; même compte enregistré dans deux fenêtres → le plus récent | U |
| V8 | **Corrigé** | échec RNG → enregistrement refusé ; lecture au-delà de la limite = dommage | U |
| S2 | **Corrigé, déterministe** (décision coordinateur, l'heuristique 10 s proposée par la revue est abandonnée) | sous le verrou profil la fenêtre qui se ferme compte exactement les autres fenêtres ouvertes puis libère son marqueur avant de rendre le verrou : une fenêtre fermée volontairement parmi d'autres ne revient jamais (0 s, 2 s, 9 s…), la dernière garde sa session ; crash = session conservée ; #60 = conservée | E2E-store : fermetures 0 s et 2 s → dernière seule (URL vérifiée), 2 vrais processus fermant simultanément (barrière) → exactement une session, crash avant release → restaurée ; E2E-app `windows_e2e.py` rejoué |
| S3 | **Corrigé, fail closed** | en-tête/version inconnus (même famille future ou pas notre format) → `UnknownFormat`, aucune écriture, base/mine/octets inchangés ; format connu endommagé → copie `.damaged-<horodatage>` relue et comparée octet à octet AVANT toute écriture, échec de copie → `Failed` sans écriture ; ce qui est lisible sert aussi de base (illisible ≠ supprimé) ; absent = nouveau ; `Busy` ne change rien. Coffre, favoris, historique, réglages (en-têtes connus) et téléchargements (v1/v2) | tests dédiés `test-password-vault` (version future, en-tête illisible, ligne endommagée + sauvegarde exacte, sauvegarde impossible ⇒ zéro écriture, absent accepté, verrou occupé) et `test-profile-store` (favoris version future). Limite : une troncature pile en fin de ligne du coffre n'est pas détectable (pas de trailer) |
| S4 | **Corrigé** | clé de verrou = chemin final (`GetFinalPathNameByHandleW`, GUID de volume) ; aucune adoption sans verrou | E2E-app (même hachage recalculé) |
| P1 | **Corrigé** | fenêtre privée : `IsPasswordAutosaveEnabled` et `IsGeneralAutofillEnabled` à FALSE | revue de code ; vérif runtime à ajouter |
| P2 | **Corrigé** | `ForgetSiteIcon`/`ClearSiteIcons` sans effet disque en privé | revue de code |
| P3 | **Corrigé** | liste de téléchargements non chargée en privé | revue de code |
| V6 (politique) | **À revoir au branchement B04** | la conservation d'une modification locale face à une suppression distante doit reposer sur un consentement explicite au vrai Save (B04), pas sur la fusion automatique | — |
| P1 (preuve runtime) | **Ouvert** | vérifier sur la fixture que les fichiers moteur Login Data / Web Data du profil privé ne reçoivent rien | — |
| V5, V7, S5 | **Ouvert** | V5 relève de l'UI de remplissage (non codée) ; V7 effacement mémoire partiel (SecureZeroMemory sur les copies maîtrisées, chaînes temporaires restantes) ; S5 propagation d'« Effacer » aux autres fenêtres | — |

Le coffre reste **inactif**. Une revue indépendante **complète** du branchement B03/B04/B05 (frontières origine/cadre/document/consentement et stockage) est requise avant toute activation ou PR finale ; ce rapport ne la remplace pas.


**Erratum de contexte.** S1 portait sur la branche feature `VALXXII/Design-missing-elements`, **non fusionnée** : le code n'a jamais été livré.
Les résultats E2E de restauration antérieurs à `218fa39` ne valent pas preuve (ils ne vérifiaient que les marqueurs) ; les E2E ont été rejoués
après correction, avec vérification du contenu (URL restaurée par fenêtre, fichiers réels).
