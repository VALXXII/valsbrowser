# Import roots, bookmark drag and drop, detected browsers: delivery report

**Branch:** `VALXXII/import-bar-roots-dnd`, local only, based on 1f56554. Nothing has been pushed, merged or released.
**Task:** task_dabffe90311d / ctx_d8923992c6eb.
**Launchable build:** `build/import-app/browser.exe` (0.3.388, built from 40dc694, a clean tree).

## Part 1: imported bookmarks where they belong; correcting v0.3.376

**Status:** READY. Independent closure at **80590be**: `review-windows-setup-release/build/review-import-bar-roots-80590be.md`. Every P1 and P2 is closed.

**Commits:**
- 9ef33c5: semantic roots.
- 9fda932: promote as one transaction.
- b7b3ce5: one pass per root.
- 8340e37: promote target identity.
- 7c997fb: refuse a source with ambiguous guids.
- eb542bc: legacy HTML folders need proof from the file's content.
- 80590be: that proof checked at every depth.

**Where bookmarks go:**
- The browser's bar root goes onto « Barre de favoris ». Other, menu and unfiled roots go into « Autres favoris ». The mobile root becomes one folder.
- HTML: `PERSONAL_TOOLBAR_FOLDER` / `UNFILED_BOOKMARKS_FOLDER` at the file's top level are recognised. An HTML file without marks goes onto the bar as it is, with no wrapper.

**Correcting v0.3.376 imports:**
- Repairs happen only on proof: the folder map's provenance, and for HTML the file's own content compared recursively.
- When nothing is proven, the old folder stays where it is and is counted and reported.
- The folder-menu command « Placer le contenu dans la barre de favoris » does the move by hand. It runs as one transaction with one undo, keeps the person's own folders, and follows the chosen folder's identity.

**Ambiguous guids:** a source in which two folders share one guid is refused whole, with its reason.

**Red/green probes:**
- build/fixb7b/probes-final.txt
- build/fixeb5/nested-proof.txt

**Tests:**
- browser import: 243 checks
- promote: 21 checks

## Part 2: bookmark drag and drop

**Status:** READY. Independent closure at **b4f8a43**: `review-windows-setup-install/build/review-bookmark-dnd-b4f8a43.md`. The four P2s are closed. 484b83f adds tests and evidence only.

**Commits:**
- 2ddf97f: drag-and-drop controller.
- 71b262d: E2E and the dev-only `--qa-drawer-stays`.
- b4f8a43: one transaction on captured identities; the preview never moves anything.
- 484b83f: E2E for the bar contract.

**What works:**
- Reorder on the bar.
- Drop into a folder.
- Drag out of a folder menu: opt-in only, stable ids.
- Drawer to bar and bar to drawer.
- Insertion line and folder tint.

**How a drop commits:**
- `src/bookmark-move.h` applies one move inside the profile lock, on the merged tree.
- The item, the destination folder and the sibling it goes before are checked by id, kind and added time, following remaps.
- The outcome is typed: Moved, Unchanged, Refused, Gone, TargetChanged or SaveFailed.
- Only Moved records an undo and announces success.
- A failed save shows the sticky retry.
- An item or place that changed in another window moves nothing and says so.
- The drop is recomputed at the release point. Releasing anywhere else, Escape, losing capture, switching profile or closing the window all cancel.

**Tests:**
- drag: 30 checks
- move: 19 checks, real save, reload and undo

**Production-host probe:** red at eb542bc, green at b4f8a43. See `design/bookmark-dnd-evidence/host-probe-eb542bc-vs-b4f8a43.txt`.

**Real app:** `tests/e2e/bookmark_dnd_e2e.ps1` passes all 16 functional checks.

**Visuals:** the three captures in `design/bookmark-dnd-evidence/visual-40dc694/` show the markers. The E2E's light-pixel threshold classes them as blank. They were checked by eye and are labelled that way. The classifier was not changed.

## Part 3: every browser on this PC, with its logo; Comet

**Status:** awaiting independent review. Pin: **40dc694** (source). 83cdfc0 adds evidence only.

**Where the list comes from:**
- Installed browsers: Windows' own registration, StartMenuInternet under HKCU and HKLM, both views, read through the registry API.
- Only the name, the icon reference and the command's executable path are read. Nothing is run or written, and nothing is registered.
- Profiles: the documented « User Data » / profiles.ini folders, metadata only.

**Comet:**
- Listed first, as a Chromium-based adapter.
- Candidate folders:
  - `%LOCALAPPDATA%\Comet\User Data`, from the Perplexity Help Center;
  - `%LOCALAPPDATA%\Perplexity\Comet\User Data`, the vendor folder.
- The first one holding a profile is used.
- **Not verified on this PC:** no personal Comet data was read. Both paths come from documentation.

**Also supported:** Chromium (path from chromium.googlesource docs), alongside Chrome, Edge, Brave and Firefox.

**Rows that cannot be imported:**
- A supported browser installed without data is shown disabled: « Installé, aucun favori ni historique trouvé ».
- An installed browser this build cannot read is shown disabled: « Installé, pas encore pris en charge ».
- A profile whose browser is no longer registered stays importable: « Navigateur introuvable : données récupérables ».
- This browser never lists itself.

**Logos:**
- Read from the browser's own local .exe or .ico as resources only (LoadLibraryEx as data / image resource), so no code runs.
- Only absolute paths on a local fixed drive, to a regular file that is not a reparse point.
- UNC, device or relative paths, and any failure, get the neutral glyph.
- Logos are decorative: UIA names each row by its text.

**Tests:**
- browser_catalog_test: 37 checks. Synthetic registrations and roots; Comet with two profiles and the documented folder; the fallbacks; a real logo read as data; FR and EN.
- modal_test: 283 checks, including Select rows.
- Import E2E at 40dc694: all functional checks, 10/10 captures non-blank (`design/browser-data-import-evidence/e2e-40dc694/`, including `fr-1b-source-list.png`).

**Not exercised by tests:** the real registry reader. By design, tests use synthetic registrations only.

## Not done, by instruction

- The 26-minute run_all, pending the Part 3 review.
- No push, merge or release.
- No Paper changes.
- No cookies.
