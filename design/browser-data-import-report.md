# Import from a browser: report

Task task_fb373e25a2ec, worktree `browser-data-import` (base `main` 18ddbd3). Plan and rules:
[browser-data-import-plan.md](browser-data-import-plan.md). Evidence: [browser-data-import-evidence/](browser-data-import-evidence/).

## What ships

Réglages › Confidentialité › Données › **Importer depuis un navigateur…** / **Import from a browser…** (also
found by the settings search; absent in a private window). One window dialog built from the existing modal
(src/modal.h): source select, three checks with availability and the reason, the destination profile named in
the body. « Importer » reads the source on a worker thread, then commits each category in its own
profile-store transaction. A summary reports each category: imported · already there · unsupported · not
imported (limit / too deep), and says cookies were not transferred. « Importation partielle » whenever a
category failed or entries were left out.

| Source | Bookmarks | History | Cookies |
|---|---|---|---|
| Chrome, Edge, Brave (`Default`, `Profile N`) | yes (`Bookmarks` JSON) | yes (`History`, `urls`; schema fallback without `hidden`) | **not available** |
| Firefox (relative `profiles.ini` profiles) | yes (`places.sqlite`, toolbar/menu/unfiled/mobile; tags and separators skipped) | yes (`moz_places`) | **not available** |
| HTML bookmark file (Netscape export) | yes | n/a | n/a |

Dependencies: none added. SQLite is Windows' own `winsqlite3.dll` (System32 only); if absent, the database
categories say « non disponible sur cette version de Windows ».

## Cookies: scope decision

Cookie / session migration is **not implemented**. During the task, a safety check stopped work on reading
cookies or session data out of other browsers' stores (category: extraction of credential/session data from
another application's profile). On the coordinator's instruction (msg_6f2a04119cbe / msg_4bfca6c62bbc) the
delivery was narrowed to bookmarks + history + HTML: no cookie, session or key material is read from any
source, no decryption, exporter, extension or workaround. The dialog shows « Cookies et connexions » disabled
with « Non disponibles : reconnectez-vous aux sites. », and every browser summary ends with « Cookies : non
transférés. Reconnectez-vous aux sites. ». Chromium `Local State` is never read (profile names come from the
folder names).

## Data rules (as implemented)

* Append only. Bookmarks go to « Autres favoris › Importés de <source> » (source roots become sub-folders),
  order/titles/folders kept. Repeat import adds nothing; a link is « déjà présent » when the same folder already
  holds that URL as many times as the source does, so intentional repeats are kept.
* Limits: 10 000 bookmarks, 8 folder levels, 1 000 history pages. Overflow is counted as « non importés (limite
  atteinte) » / « (dossiers trop imbriqués) »; nothing flattened, nothing evicted (E2E: 9 968 imported, 170 032
  reported).
* History: newest first, URLs through `AddressBar::Recordable` + the search-result-page filter; existing page →
  count and last visit = max (idempotent). Chromium µs-since-1601 and Firefox PRTime converted to Unix ms.
* Consistency: SQLite databases are never opened in place. The database and its `-wal`/`-journal` are opened
  with read sharing only (a running browser makes that fail → « fermez Chrome, puis réessayez »), copied to a
  private temporary folder, read in one transaction, deleted on every exit. Tests prove the source trees
  (sidecars included) are byte-identical afterwards, also with a deny-write ACL on the source folder.
* Destination: pinned when the dialog opens; checked when « Importer » is pressed and when the read returns.
  A different profile on screen → nothing read or written, « Le profil a changé : rien n'a été importé. ».
* Commits: bookmarks and history are separate transactions (`ProfileStore::Commit`, profile mutex, merge with
  other windows, atomic write). A failed one writes nothing and is reported; the other still commits.
* Untrusted input: JSON/HTML ≤ 32 MiB, nesting ≤ 256, rows bounded, reparse points refused (files and every
  folder below the browser root), no HTML executed (tokenizer only; `<script>` content ignored), Firefox
  paths with `..`, drive or absolute forms refused. Logs carry counts and the brand only.

## Commits (local, not pushed)

| Commit | Content |
|---|---|
| d5ca242 | backend: `src/browser-import.h`, `AddressBar::History::Import`, catalogue, plan, `tests/browser-import/browser_import_test.cpp` |
| c5dfa88 | UI wiring: Settings row + search, `Modal::Select::checks`, `src/browser-import-integration.h`, main.cpp hooks, `--import-sources`, modal test |
| 2c4b159 | destination pinned at dialog open; « partielle » when entries are left out |
| ce96190 | E2E script, synthetic source generator, runner `test-browser-import.ps1`, run_all entry, captures, this report |

## Tests

| Test | Result |
|---|---|
| `test-browser-import.ps1` (backend, synthetic roots, winsqlite3) | 142 checks pass; settings wiring fixture 8/8 |
| `tests/e2e/browser_import_e2e.ps1` on `build/import-app` (real browser.exe, FR + EN, UIA only) | 58/58 pass ([e2e-results.txt](browser-data-import-evidence/e2e-results.txt)) |
| `tests/modal_test.cpp` | new « select checks » cases pass; 13 distinct render-sample checks FAIL **identically on baseline 18ddbd3** (pre-existing harness limit) |
| bookmarks, address, settings, i18n, session, profile_store tests | pass |
| language_settings_test, accessibility_test | pass |
| annotation_settings_test | 7 agent-connect FAILs, **identical on baseline 18ddbd3** (pre-existing) |
| `tools/i18n/check_strings.py` | 0 findings |
| `tools/release/build.py --out build/import-app` | Build OK at HEAD (launchable: `build/import-app/browser.exe`) |

E2E covers: dialog FR/EN, cancel unchanged, Chrome import (Unicode nested folders, duplicates, javascript:,
chrome://, hidden rows, search pages, epoch conversion), repeat import byte-identical, source busy (stays open,
retry), destination save failure on bookmarks only (partial, file untouched, retry idempotent), Firefox WAL
source, HTML through the real file dialog, Edge bookmarks-only profile, profile switch with the dialog open
(refused, nothing written to either profile), capacity on 180 000 bookmarks, sources byte-identical, no
snapshot left, log free of URLs/titles/paths.

## Harness limits and remaining limitations

* Mid-read events are now driven deterministically through a dev-build-only seam (below). UIA can still reach
  the profile menu under the window modal (pre-existing; pointer and keyboard are blocked).
* The E2E file dialog is driven by WM_SETTEXT + posted BM_CLICK on its own controls (its UIA tree exposes them
  as panes).
* Symbolic-link refusal of an HTML file is skipped without developer mode; junctions are covered.
* Chromium profile labels are folder names (« profil principal », « Profile 2 »), since `Local State` is not read.
* Firefox absolute-path profiles are not listed.
* Personal browsers were never discovered or read in QA (`--import-sources` synthetic roots only).

## run_all.py reconciliation

This worktree adds one hunk to `tests/update-integration/run_all.py` (after `bookmarks_test`): the
`browser_import_test` fixture with its work folder. Root main has unrelated local WIP in the same file; the hunk
is independent and should be re-applied on top of it, not by overwriting.

## Independent review 934ef4b: fixes

Review: `review-windows-setup-release/build/review-browser-import-934ef4b.md` (four P2, one evidence gap).

| Finding | Fix | Red (934ef4b) → green |
|---|---|---|
| 1. `hooks.vaultInfo` commented out by the import hook's trailing comment (password manager row hidden) | assignment back on its own line; `tests/browser-import/import_settings_test.cpp` checks main.cpp's hooks with comments stripped and the real drawer's rows from synthetic hooks (no vault opened, no credential read) | fixture on 934ef4b's main.cpp: `FAIL hooks.vaultInfo is executable code`; now 8/8 |
| 2. Folders identified by translated titles (FR→EN, or a source gaining a profile, duplicated groups) | `FolderMap` sidecar `<data>\bookmark-imports` (header-versioned): stable keys (browser + profile folder; Chromium root key / Firefox root GUID) → folder id + `added`; read and written under the same profile lock as the bookmarks transaction, validated on every use, pruned, never rewritten if of another version; missing/stale/unwritable map → titles of both languages, then create. Bookmarks file format unchanged | reviewer's scenario: switched-import 1 / 2 groups → present 6, imported 0, 1 group (backend + real-app EN repeat of the FR Chrome import: file byte-identical) |
| 3. Folder replaced by a junction between discovery and « Importer » was read | `Pin`: every folder from the browser root to the profile (HTML: the file's folder) opened with data access and no delete sharing, not a reparse point, final path = parent's + name; each source handle (JSON, database, -wal, -journal, HTML) accepted only if its final path is inside | reviewer probe (retargeted to this worktree): `reparse=1 read-failure=0 roots=1` → `read-failure=3 (Unreadable) roots=0`; tests for root, intermediate Firefox folder, HTML folder, held folders not renamable |
| 4. Reader depth bound silently pruned (257 folders + 1 link → « terminée », nothing imported) | `TooDeep` (refused whole, « dossiers imbriqués trop profondément, rien n'a été importé. ») for JSON, Firefox and HTML; parser limits map to TooDeep/TooLarge; damaged source entries counted as « entrées illisibles » and make the summary « partielle » | reviewer depth probe: `failure=0 … folders=7` → `failure=TooDeep roots=0`; boundary tests at 256 kept / 257 refused for all three formats |
| Evidence gap: mid-read switch / delete / close | dev-build-only seam `--import-hold read|snapshot` (compiled out of `--channel release`; named events `Local\browser-import-reached/release-<pid>`), plus a sweep of private snapshot folders left by ended processes | E2E: switch while parked → dropped, nothing written to either profile or the map; window closed with the snapshot taken → nothing written, the next import removes the leftover copy; destination profile deleted while parked → nothing written to « Personnel », « Travail » deleted, no snapshot left |

Note: while reproducing finding 3, my first copy of the reviewer's probe still pointed at the reviewer's fixture
folder; it recreated a synthetic `Default\Bookmarks` there and rewrote `foreign\Bookmarks` with identical bytes
before failing. Both were restored (the extra folder removed, identical content, timestamps reset from siblings);
the probe was then retargeted to this worktree's `build/review-probes/fixtures`.

## Captures (evidence correction after 99959e3)

The screenshots committed in af9636e were entirely black: PrintWindow of the app's own windows returned blank
bitmaps in that session, and the script saved them without checking. They are preserved, renamed, in
`design/browser-data-import-evidence/blank-captures-af9636e/`; the valid first-delivery captures are restored in
`realapp-ce96190/`; layout of the current dialog states (FR/EN: dialog, no-history source, HTML, busy source,
complete, partial save failure, partial capacity, too deep) is shown by a **native fixture** (production modal
rendered offscreen, every PNG verified non-blank) in `native-fixture/`. A targeted re-capture of the real app's own
dialog HWND was still blank (real-app capture unavailable in this session; no OS/display change, no foreground
change). The E2E now gates every capture (blank → `BLANK-*.png`, « visual UNAVAILABLE », `-RequireVisual` to
fail). Re-run at 99959e3: functional 58/58, visual 0 ok / 9 blank (`e2e-99959e3/`). See the folder README.

Harness note found on the way: the 13 « render sample » failures of `tests/modal_test.cpp`, identical at baseline,
come from that test writing to `build/popups/`, which does not exist in this worktree (`test-popups.ps1` normally
provides it); they are not a rendering defect. Not changed here.

Fixture data: from now on only fresh fixtures of this worktree are used; the earlier accidental touch of the
reviewer's `import-probe-fixtures` (restored) stays disclosed above as a limit.

## Closure review 99959e3: durable identity and deletion evidence

Review: `review-windows-setup-release/build/review-browser-import-99959e3.md` — vault, source pinning and depth
CLOSED; folder identity remained open when the map could not be written.

**Fix (af02996, source).** The identity map is now a prerequisite of the bookmarks commit: written in the profile
store's pre-write step (after the merge fixed the ids, before the bookmarks file), one transaction under the profile
lock. Map unwritable/unreadable, or of a newer version (left untouched) → bookmarks not written, category failed and
shown as such (no title-fallback success). Order makes interruption recoverable: map ahead of bookmarks leaves only
entries for non-existent folders, never trusted (id + kind + `added`), replaced on retry. Bookmarks format unchanged.

| Case | Before (99959e3) | After |
|---|---|---|
| Reviewer production probe (fresh copy, own folder), map held without delete sharing | `first saved=1 imported=1 mapSaved=0`; after release + rename + EN + label change: `imported=1 present=0 groups=2` | `first saved=0 imported=0 mapSaved=0` (refused, nothing written). The probe then exits 3: it assumes the first import succeeded |
| Same sequence in the backend suite: refused → retry → rename container and root → EN + multi-profile label | — | retry imported 1; then `present=1 imported=0`, one group, custom names kept |
| Bookmarks write fails after the map (state of a crash between the writes); another window then creates a folder taking the dangling id | — | nothing claimed, bookmarks untouched; dangling entry never adopted; exact retry; later rename + EN still one group |
| Another window renames the imported folder between imports | — | still found, its name kept |
| Map of another version | not used, bookmarks still imported | not used, not rewritten, nothing imported (« données d'import d'une version plus récente ») |

Backend: 151 checks pass (`test-browser-import.ps1`).

**Deletion evidence (E2E phase c, tests only).** The phase now counts as a deletion test only if « Travail » is gone
from the profile registry while the worker is still parked (checked before the release); otherwise it reports
« UNVERIFIED » and counts nothing. The fallback that released first and accepted « deleted or untouched » is gone.
The outcome is read from the import dialog's UIA text (the window's log lived in Travail's folder, deleted with it).
Targeted run (`e2e-af02996-phase-c/phase-c-results.txt`): confirmation reached, deleted before release, worker
still parked → the returned read was dropped (« Le profil a changé : rien n'a été importé. »), nothing written to
« Personnel », nothing recreated for « Travail », no snapshot left. Limit: the window had also left « Travail »
(it was showing « Personnel »), so the UI cannot tell which part of the destination check refused the read; the
dead-owner branch alone is proven by the reviewer's controlled harness (import999-completion-guard, 4/4).

## Closure review ff1bcbe: existing-identity recovery and lifecycle

Review: `review-windows-setup-release/build/review-browser-import-ff1bcbe.md` — locked-map case CLOSED; one P2 left
at the map-ahead boundary for an EXISTING mapping.

**Fix (2030f27, source).** Replacing an identity no longer destroys the previous one. A key of `bookmark-imports`
holds ordered candidate folders (preferred first, at most 4) in the same atomically replaced file: a new identity
goes first, the previous stays after it while its folder exists in the bookmarks file as read; resolution takes the
first candidate that exists (same id, kind, `added`) in the actual bookmarks under the profile lock; a candidate is
pruned only once its folder is gone from both the file as read and the result written. So whether the bookmarks
write then succeeds (new candidate valid, old one pruned next commit), fails, or the process stops in between (old
candidate valid on restart), the right folder is found. No separate journal; header unchanged; a map of another
version is still never used or rewritten and still refuses the import; counts stay zero on any failure.

| Case | ff1bcbe | 2030f27 |
|---|---|---|
| Reviewer recovery probe (fresh own copy): existing renamed mapping, pending deletion, bookmarks write fails, restart, EN + label | `imported=1 present=0 groups=2` | `imported=0 present=1 groups=1`, custom names kept |
| Same, bookmarks write failed (backend) | — | map holds new + old candidate; restart → present, one group; dangling candidate pruned; FR repeat still one group |
| Same, thread stopped right after the map write (abandoned profile mutex = crash) | — | bookmarks untouched; restart → present, one group, custom names; real deletion later → new group, old candidate pruned |

Backend: 161 checks pass.

**Lifecycle (76fe51f, helper + packaging).** The portable → installed migration now copies `bookmark-imports` with
`bookmarks` in the same all-or-nothing copy (other profiles already move as whole trees, which carries theirs);
`package.py` lists it and `bookmark-imports.tmp*` as runtime data, so it is skipped and never in a release payload
(before, staging aborted on it as an unexpected file). Contract: the import keeps exactly one metadata file per
profile, `bookmark-imports`, next to `bookmarks`; no pending/journal file; its content has no absolute path (keys
are browser + profile folder name, Firefox's profile path relative to its root; values are folder ids and times),
so it stays valid after a move. Copy it whenever `bookmarks` is copied; if it is missing, imports fall back to
titles (degraded, never wrong). Red/green: e2e_install with the ff1bcbe helper fails (`data/bookmark-imports`
missing), with 76fe51f passes; staging test passes. The update-helper and package.py hunks are small and separate
for the WindowsSetup owner's merge. Excerpts: `browser-data-import-evidence/ff1bcbe-followup/results.txt`.
