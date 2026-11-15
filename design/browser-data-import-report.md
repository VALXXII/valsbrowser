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
| `test-browser-import.ps1` (backend, synthetic roots, winsqlite3) | 98 checks pass |
| `tests/e2e/browser_import_e2e.ps1` on `build/import-app` (real browser.exe, FR + EN, UIA only) | 44/44 pass ([e2e-results.txt](browser-data-import-evidence/e2e-results.txt)) |
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

* A switch landing exactly while the worker reads is guarded by the same check but not reproduced
  deterministically (a 30 MB read takes well under a second). UIA can still reach the profile menu under the
  window modal (pre-existing; pointer and keyboard are blocked).
* The E2E file dialog is driven by WM_SETTEXT + posted BM_CLICK on its own controls (its UIA tree exposes them
  as panes).
* Symbolic-link refusal of an HTML file is skipped without developer mode; junctions are covered.
* Chromium profile labels are folder names (« profil principal », « Profile 2 »), since `Local State` is not read.
* Bookmark folder titles follow the interface language at import time; importing again after switching
  language creates a second « Imported from … » folder.
* Firefox absolute-path profiles are not listed.
* Personal browsers were never discovered or read in QA (`--import-sources` synthetic roots only).

## run_all.py reconciliation

This worktree adds one hunk to `tests/update-integration/run_all.py` (after `bookmarks_test`): the
`browser_import_test` fixture with its work folder. Root main has unrelated local WIP in the same file; the hunk
is independent and should be re-applied on top of it, not by overwriting.
