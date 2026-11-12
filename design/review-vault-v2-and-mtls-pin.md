# Independent review: vault file format v2 and test-only mTLS fixture pin

Scope: HEAD `ea096a1`, branch `VALXXII/Design-missing-elements`. Read-only review. The password vault is inactive.
Commands run: `test-password-vault.ps1` (PASS, every check ok) and `test-e2e-pin.ps1` (PASS, 13/13; one `-Wmisleading-indentation` warning at `src/e2e-fixture-pin.h:47`, harmless).

Severity scale: High = breaks the contract in a realistic case; Medium = must be fixed before activation, or the contract is only partly met; Low = hardening, strictness or a test gap; Info = a remark.

---

## Verdict

**The v2 format contract is ADOPTED, with three conditions to meet before the vault is activated (V2-M1, V2-M2, V2-M3).**
The format itself does what the contract asks: a versioned header, records, then `end\t<count>\t<sha256 of every byte before the trailer>`, nothing after it. It detects a premature end, a missing whole entry, a missing or invalid trailer and extra bytes. A legitimately empty vault (header plus `end\t0\t…`) is distinct from a damaged one. v1 and future versions are refused, never migrated. The checksum is honestly described as detecting accidental damage only. No parser hole was found that lets accidental damage pass as a valid file.
The remaining problems are in how the format is reached: a zero-byte file, a silent truncation in Merge, and a vault policy that only the tests apply.

**mTLS fixture pin: sound for its purpose.** The hook cannot reach release or dev binaries. URI matching is exact-string and fails closed. One claim in the documentation is wider than the code (V-status). The way the host binary is built is not reproducible (P-M1).

---

## Review 1: vault file format v2

### Findings

**V2-M1. Medium. A zero-byte existing file counts as "absent", and the window's base is then treated as deleted elsewhere.**
`src/profile-store.h:66-75` (`ReadAll` returns `""` for both a missing file and an empty file), `src/password-vault.h:157` (`if (text.empty()) return s;`, not damaged), `src/profile-store.h:120,126,133`.
Scenario: the passwords file is truncated to 0 bytes by something outside this store, such as a sync tool, an antivirus restore or a disk tool. A window whose `base` holds N accounts then commits one new save. `known` is skipped because `bytes` is empty, `Read` reports not damaged, and `Merge(base, mine, theirs={})` drops every entry that is unchanged since base, because its loop over mine skips `b && *b==m`. The file is rewritten with only the new account and `Result::Written`. All N accounts are then lost from the file and from memory, since `base = mine = merged`.
This contradicts the header comment "Unreadable is never treated as « deleted »". It also contradicts "legitimately empty distinct from truncated": a real empty vault is never 0 bytes in v2. The test at `tests/password_vault_test.cpp:118` ("an absent (zero-byte) file is a new vault") writes this conflation into the suite.
Minimal fix: make `ReadAll` report whether the file exists (for example an out flag). In `CommitWith`, treat an existing zero-length file as damaged, then back it up and fail closed. Optionally, also refuse with `Failed` when the file is absent while `base` is non-empty. Split the test into "absent → new vault" and "0-byte existing → damaged, nothing written".

**V2-M2. Medium. `Merge` silently truncates to `Limit`. The result is a valid, digest-correct file that has lost accounts.**
`src/password-vault.h:206` (`if (out.size() > Limit) out.resize(Limit);`).
Scenario: the file holds 4,999 accounts and this window has two new ones. `Save` passed in each window, because the check is per window at `:111`. Merge produces 5,001 entries, drops the last one (one of this window's new accounts), `Commit` returns `Written`, and `mine` is replaced. A password the user agreed to save disappears, with no message and no `damaged` signal. This is the "count/limit consistency" and "no silent destructive" point of the contract. Read refuses `count > Limit` (`:168`), so the writer side should never need to cut.
Minimal fix: never truncate in Merge. Have `CommitWith` (or a vault wrapper) return a distinct refusal (`Failed`, or a new `Full`) when `merged.size() > Limit`, leaving the file, base and mine untouched. Add a test.

**V2-M3. Medium (activation blocker). The fail-closed vault policy exists only in the tests. The library defaults are permissive.**
`src/profile-store.h:115,143` (`known = {}`, `writeOverDamaged = true` by default). `src/main.cpp` has no vault caller yet (grep: no `Vault::`/`passwords` commit).
Scenario: the future activation code calls `ProfileStore::Commit<Vault::Store>(dir, path, base, mine, merge)` the way `tests/password_vault_test.cpp:62` already does. A v1, future or foreign file then skips the header gate, `Read` flags it damaged, it is backed up and then overwritten with `merge(theirs, mine, theirs)`. That is the silent destructive migration the contract forbids.
Minimal fix: add `Vault::Commit(dataDir, base, mine)` in `password-vault.h` that hard-codes `known={Store::Header}`, `writeOverDamaged=false` and the Merge lambda. Make it the only entry point, and test through it (including line 62).

**V2-L1. Low. `Failed` covers both "I/O error, retry" and "vault damaged, backed up, needs explicit recovery".**
`src/profile-store.h:130-131`. The caller cannot tell the user which one applies. Each retry also writes a new `.damaged-<second>` copy (up to 1 GiB each, see `:128`), so a damaged vault plus periodic retries fills the disk.
Fix: add `Result::Damaged`, and skip the backup if an identical `.damaged-*` already exists (or keep one backup per content digest).

**V2-L2. Low. The record fields are parsed leniently.** The digest covers them, so accidental damage is still caught. A non-canonical writer (another tool, or a same-user process, which is out of scope) gets through, and the reader accepts several encodings for one value.
- `src/password-vault.h:175`: `stoull(f[0], nullptr, 16)` and `stoll(f[1])`/`stoll(f[2])` have no full-consumption check. `"abcXYZ"`, `" +0x1f"`, `"-1"` (ULLONG_MAX) and `"12abc"` are all accepted.
- `src/password-vault.h:47`: `Unbase64` stops at the first `=` and ignores the rest (`"YQ==garbage"` decodes as `"a"`). It also accepts unpadded or wrong-length input.
- `src/password-vault.h:177`: invalid UTF-8 in the username makes `Wide()` return `{}`. The entry is kept with an empty username instead of being flagged `bad` (the origin path is protected by the `empty()` check, the username path is not).
Fix: require `used == size` and a `[0-9a-f]+` / `-?[0-9]+` shape. Make Unbase64 strict (length % 4 == 0, padding only at the end, nothing after it). Flag `bad` when `!u.empty() && Wide(u).empty()`.

**V2-L3. Low. The count is parsed leniently and is not covered by the digest.**
`src/password-vault.h:167`: `stoull` accepts leading whitespace and `+` (`" 3"`, `"+3"`, `"03"` all pass `used == tab`). The count stays cross-checked against the parsed entries, which the digest does cover, so this is not a completeness hole, only a canonicity one.
Fix: require `t[0]` to be a digit and no leading zero unless the value is `"0"`. Optionally compute the digest over the body plus `end\t<count>\t` so the trailer is covered too.

**V2-L4. Low. An empty digest on a BCrypt failure is not refused.**
`src/password-vault.h:142` returns `{}`. `Write` (`:149`) would then emit `end\tN\t\n`, and `Read` (`:168`) accepts an empty digest field whenever `Digest` also fails. This is practically unreachable, because `BCRYPT_SHA256_ALG_HANDLE` needs Windows 10 and the binary does not load without it, but it is not fail-closed in principle.
Fix: `Read` fails if `Digest(body).empty()`. `Write` should be able to fail (return bool), and `CommitWith` should not rename on failure.

**V2-I1. Info.** On a trailer or digest failure, `Read` returns an empty store (`fail()` runs before any parsing). The comment at `:151-153` ("what could be read is returned for display only") only holds when the digest is valid but a line is bad. Either update the comment or parse best-effort for display.

**V2-I2. Info.** CRLF: `FirstLine` strips `\r` (`profile-store.h:104`) but `Read` compares the header exactly (`password-vault.h:160`). A CRLF v2 file therefore passes the gate, is flagged damaged, is backed up and `Failed` is returned. That is consistent and fail-closed, just classified as "damaged" rather than "unknown". A header cut in the middle (`"browser-pass"`) is classified `UnknownFormat` rather than damaged. Nothing is written in either case.

### Parser-hole checks (all sound)
- **`\nend\t` inside data:** the base64 alphabet (`A-Z a-z 0-9 + /` and `=`) has no tab or newline. id, created and updated are hex or decimal. A record line can never contain `\n`, and `rfind` takes the last trailer. A forged earlier `end\t…` line inside the body is caught by the digest, and even with a recomputed digest it fails the 6-field check (`bad`).
- **Duplicate trailers:** `full + "end\t0\t…\n"` is tested (`test:127`). A crafted double trailer with a correct digest still fails because of the 3-field "record".
- **Trailing whitespace / CR in the trailer:** the digest string compare fails, so the file is damaged.
- **Empty lines in the body:** `getline` gives `""`, 1 field, `bad`.
- **Huge counts:** `stoull` out_of_range is caught, so the read fails. `count > Limit` fails. A file of 1 GiB or more makes `ReadAll` fail, so the result is `Failed` and nothing is written.
- **Digest scope:** exactly `text[0 .. endPos]`, that is the header line plus `\n` plus every record line with its `\n`, matching the comment (`:133`) and `Write` (`:146-149`).
- **Empty body:** `header\nend\t0\t<Digest("header\n")>\n` round-trips (`test:117`). `endPos == first` is handled.
- **Compatibility:** v1 → `UnknownFormat`, file byte-identical (`test:78-80`). v3 → `UnknownFormat`, file, base and mine untouched (`test:73-76`). Foreign bytes → `UnknownFormat` (`test:82-84`). Rollback from a future v3 build to this one: refused, never overwritten. No migration code exists.
- **Original bytes on failure:** `WriteAtomic` writes a temp file, flushes it, then does `MoveFileEx`. On `Busy`, `Failed` or `UnknownFormat` nothing is renamed, and base and mine are assigned only after a successful write (`profile-store.h:134-138`). The exact backup is read back and compared before any write (`:130`).
- **Unchanged:** no write when `merged == theirs` and not damaged.

### Do the tests exercise what they claim?
| Test (password_vault_test.cpp) | Assessment |
|---|---|
| :31 "a stored empty origin is dropped as damage" | **Passes for the wrong reason.** The input has no trailer, so `fail()` runs before the empty-origin check. Fix: build the body with a valid trailer and digest so that only the origin check can trigger. |
| :86-91 "one damaged line" | It actually tests **bytes after the trailer**, which fails at the trailer check. A damaged line *inside* a digest-valid body is never tested. "Readable entries kept" is not observable here, because `fail()` returns an empty store. |
| :118 "absent (zero-byte) file is a new vault" | It codifies V2-M1. See the fix above. |
| :120-122 cut at every line boundary | Sound (header-only, after each record, before the trailer). "Inside the trailer" is only covered by `:123` (final `\n` missing). Cuts in the middle of a line are not iterated, though they would fail anyway. |
| :124 lost entry | Sound, caught by the digest (and the count). |
| :125 wrong count | Sound. It isolates the count check, because the digest stays valid. |
| :126 wrong digest | Sound. |
| :127 extra bytes | Sound (one stray byte, and a duplicate trailer). |
| :128 missing trailer | Sound. |
| :129 count over limit | **Not isolated.** 0 entries against count 5001 already fails the count==entries check. To isolate the limit, use a digest-valid file with 5001 real entries. |
| :73-109 S3 (future, v1, garbage, backup, backup impossible, absent, busy) | Sound. They all pass `known` and `false` explicitly, which V2-M3 should make the default for the vault. |
| Missing | 0-byte existing file; Merge over Limit; CRLF file; damaged line inside a valid digest; non-canonical numbers or base64; invalid UTF-8 username; a test through a vault-specific commit wrapper. |

---

## Review 2: test-only mTLS fixture pin

### Findings

**P-M1. Medium (test integrity). Nothing in the repository builds the E2E host, so the binary under test can be stale.**
`tests/e2e/mtls_host.cpp:1-2` says "Built only by tests/e2e/mtls_e2e.ps1", but `mtls_e2e.ps1` never compiles it: it only consumes `build/e2e-mtls-host/browser.exe` (`:63-67`). No other script references `mtls_host.cpp`. In this worktree that binary is dated 21:58:27, before the last change to `src/main.cpp` (22:04:38). The marker check at `:63` only proves that *some* old build contains the string. It does not prove that the current hook code is what ran.
Fix: compile `tests/e2e/mtls_host.cpp` in `mtls_e2e.ps1` stage 4 (the same flags as `build.py` minus `BROWSER_HAS_BUILD_INFO`, plus `-municode -mwindows`) into a fresh folder, and fail if the compile fails.

**P-L1. Low (documentation vs code). "Authority not trusted only" is broader in practice.**
`src/e2e-fixture-pin.h:8-9`, `:78`. `COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_IS_INVALID` is WebView2's catch-all status: untrusted authority, but also weak signature or key, name-constraint violation, validity too long, missing CT, pin mismatch, legacy Symantec. WebView2 exposes nothing finer, so the code cannot do better through this API. The actual bound is the leaf pin, plus SAN, plus validity, all checked locally, and that is sufficient.
Fix: reword the comment to "status CERTIFICATE_IS_INVALID (WebView2's bucket that includes an untrusted authority); expired, revoked, wrong name and client-cert statuses are refused". Optionally, have the harness assert the fixture cert's own properties (RSA-2048, SHA-256 signature, 1-day validity), which it already does by construction.

**P-L2. Low. The ALWAYS_ALLOW cache is bounded by process lifetime only, and fresh-profile cleanup is not verified.**
WebView2 caches ALWAYS_ALLOW per (host, server certificate) for the session, that is the browser-process lifetime. It is not per port and is not re-evaluated, and nothing calls `ICoreWebView2_14::ClearServerCertificateErrorActions`.
What *is* bounded:
- only the pinned leaf, whose private key belongs to the harness and is deleted with `pki` in `finally`, can benefit;
- another cert on any port raises a new event, and `Decide` refuses it;
- the cache is not persisted beyond the browser process;
- the server certificate is regenerated each run (new pin).

What is *not* bounded:
- within one session, the same cert served on another 127.0.0.1 port is accepted without the port check (test-only, harness-owned key);
- if the cert's validity ends mid-session, whether the cached allow still applies depends on Chromium's error-keyed cache (not verifiable here);
- `Fresh-Profile` (`mtls_e2e.ps1`, the function on the line defining it) kills the host, sleeps 1 s and removes `profile` with `-ErrorAction SilentlyContinue` without checking that it is gone or that `msedgewebview2.exe` children have exited. A lingering browser process could carry the stage-4 cache and the client-cert choice into stage 5.

The stage-5 check fails rather than passing falsely in that case (no dialog means `$cancel` is null, which is a FAIL), so this is a robustness issue, not a false pass.
Fix: after the kill, wait for no `msedgewebview2.exe` whose command line contains `$hostDir\profile`, then assert `-not (Test-Path profile)`. Optionally, call `ClearServerCertificateErrorActions` at host startup in the hook block.

**P-L3. Low. Manifest parsing is lenient.**
`src/e2e-fixture-pin.h:34-36`: `atoi("18445junk")` gives 18445, `sha256` is checked for length only (non-hex or uppercase never matches, which is fail-closed), and duplicate keys mean the last one wins. This is harmless, because host is fixed to `127.0.0.1` and the pin must match exactly.
Fix: strict decimal and `[0-9a-f]{64}`, and reject duplicates.

**P-L4. Low (test gaps).** `tests/e2e/e2e_pin_test.cpp` has no case for:
- a pinned cert **without SAN** (the subject-only `CN=127.0.0.1` case that the contract names explicitly). The code handles it (`CertFindExtension` gives null, so false), but it is untested;
- URI edge forms: `HTTPS://` (uppercase), `127.0.0.1.:18445`, `[::ffff:127.0.0.1]:18445`, `127.0.0.1:018445`, `%3127.0.0.1`, `127.0.0.1:18445\@x`, a missing port.

Add them as negative cases. They are all refused today by exact string equality (see below).

**P-I1. Info.** The marker check (`mtls_e2e.ps1:63`) inspects `build/real-integration-check/browser.exe` only, and only during the manual E2E. It does not inspect the release artifact. Isolation actually rests on the preprocessor (see below), but a cheap CI assertion would guard against regressions: grep the release `browser.exe` for `E2E FIXTURE PIN` and `BROWSER_E2E_PIN_MANIFEST`, both UTF-16.

### URI parsing edge cases (Decide, `src/e2e-fixture-pin.h:75-77`)
Matching is `starts_with("https://")`, then authority (up to the first `/?#`) `== "127.0.0.1:" + port`. Exact equality means anything that is not byte-identical is refused:

| Form | Result |
|---|---|
| uppercase scheme | refused |
| userinfo (`u@127.0.0.1:p`, `127.0.0.1:p@evil`) | refused |
| trailing dot | refused |
| IPv6 / IPv4-mapped / decimal or hex IPv4 | refused |
| percent-encoding | refused |
| leading-zero port | refused |
| default port omitted | refused |
| backslash | refused |

Every one of these fails closed. Chromium canonicalizes `RequestUri` before the event, so the legitimate URL is always the canonical `https://127.0.0.1:18445/...`.

### Isolation: sound
- `BROWSER_E2E_FIXTURE_HOOK` is defined only in `tests/e2e/mtls_host.cpp:4` and `tests/e2e/e2e_pin_test.cpp:4`. `src/e2e-fixture-pin.h:13-15` raises `#error` without it.
- Every use in `src/main.cpp` (`:59-63` include, `:2233-2250` `E2EFixtureAllows`, `:2344-2346` ALWAYS_ALLOW) is inside `#ifdef`. Outside it, the handler always sets `CANCEL`.
- `tools/release/build.py:30-31,126-131` compiles `BROWSER_SOURCES` (no tests) with only `-DBROWSER_HAS_BUILD_INFO` and optionally `-DBROWSER_PREVIEW`. `build.ps1` wraps `build.py`. CI (`.github/workflows/update-release.yml:64`) calls `build.py`. GCC reads no macro definitions from the environment, and `BUILD_ENV` passthrough cannot inject `-D`.
- In a production binary, the env var `BROWSER_E2E_PIN_MANIFEST` is never read (the read sits inside the `#ifdef`). No CLI option exists.
- The pin comes only from a file named by the host's environment and written by the harness. Page messages and the bridge never touch it. `Load` hard-requires host `127.0.0.1` and port 1025-65535.
- `Decide` checks, in order: manifest, https, exact host:port, status, the validity window from `ICoreWebView2Certificate` (the leaf), SHA-256 of the leaf DER (from `ToPemEncoding` → `DerFromPem`, first block only, a second BEGIN without END is refused) against the manifest, and SAN exactly one IPv4 entry `127.0.0.1` (subject ignored). Expired, revoked, wrong-name and other statuses are refused. Any failure in the COM getters returns false, and the caller then CANCELs.
- `GetEnvironmentVariableW` overflow (more than 1024 characters) leaves the zeroed buffer, `Load("")` fails, and the pin is refused.

---

## Corrections (intégrateur, après cette revue)

| # | État | Correction | Preuve (tests exécutés) |
|---|---|---|---|
| V2-M1 | Corrigé | `CommitWith` : un fichier **existant** de 0 octet avec un format connu et `writeOverDamaged=false` → `Failed`, rien d'écrit, base/mine intacts (absent ≠ vide existant) | `test-password-vault` « V2-M1 » |
| V2-M2 | Corrigé | `Merge` ne tronque plus ; option `acceptable` de la transaction : au-delà de la limite → `Failed`, fichier inchangé, rien de retiré en mémoire ; `Save` refuse explicitement à la limite | « V2-M2 » ×3 |
| V2-M3 | Corrigé | `Vault::Commit` (seul point d'entrée) fige en-tête v2, `writeOverDamaged=false` et la limite | « V2-M3 » ×2 (v1 refusé, coffre endommagé jamais réécrit) |
| Bas : sauvegardes répétées | Corrigé | une seule copie par contenu endommagé distinct (`.damaged-last`) | — |
| Bas : analyse laxiste | Corrigé | hex/décimal exacts, base64 canonique (longueur %4, padding final), UTF-8 invalide d'un identifiant non vide = dommage, digest vide jamais accepté | « lenient numbers » |
| Tests mal ciblés (l.31, l.86-91, l.129) | Corrigés | fichier v2 complet avec seule l'origine vide ; ligne invalide dans un fichier complet (compte/digest corrects) ; limite isolée avec digest correct | « isolated » ×3 |
| P-M1 | Corrigé | `mtls_e2e.ps1` reconstruit l'hôte depuis les sources courantes avant chaque exécution | mTLS E2E PASS ×2 après correction |
| P-L1 | Corrigé (formulation) | `CERTIFICATE_IS_INVALID` décrit comme statut générique ; pin + SAN + validité = la vraie borne | — |
| P-L2 | Partiel | profil réellement supprimé et processus moteur du runtime fixture terminés vérifiés, sinon l'étape s'arrête ; `ClearServerCertificateErrorActions` non appelé (borne = processus moteur neuf par étape) ; même certificat sur un autre port 127.0.0.1 dans une session : non borné par le code, aucun autre serveur n'existe dans la fixture | — |
| P-L4 | Corrigé | cas sans SAN (CN seul) et formes d'URI limites (casse, point final, IPv6 mappé, zéro initial, %, backslash, hex) | `test-e2e-pin` PASS |
| P-I1 | Corrigé | `test-e2e-pin.ps1` échoue si le marqueur apparaît dans le binaire dev/release | « no fixture pin in build/real-integration-check/browser.exe » |
| P-L3 | Ouvert (bas) | parseur du manifeste toujours tolérant (`atoi`), échec fermé | — |

Contrat du format v2 : **adopté par la revue**, corrections M1-M3 faites et prouvées. Le coffre reste **inactif** : revue d'intégration complète requise après branchement B03/B04/B05.
