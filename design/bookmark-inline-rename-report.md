# Bookmark folders: rename in place — report

Request (2026-11-15, screenshot of the bar folder « Game »): « Renommer » must edit the name **directly on
the label next to the folder icon**: no floating frame, name selected, Enter commits, Escape cancels.
Branch `VALXXII/bookmark-inline-rename`, base `75c544b`. Plan: `design/bookmark-inline-rename-plan.md`.
Evidence: `design/bookmark-inline-rename-evidence/`.

## Result

| Before (popover) | After (in place) |
|---|---|
| « Renommer… » / F2 opened the bookmark popover: Nom, Dossier, Supprimer, Terminé | the folder's own title becomes the text field, name selected; icon and chevron stay; no frame |

Real browser at this monitor's 120 DPI (`app-band-before-120.png` → `app-band-renaming-120.png` →
`app-band-after-120.png`): « Game » selected in place on its hovered band item, then « Jeux vidéo » with
keyboard focus back on the item.

## Approach

- **`NativeUI::InlineRename`** (`src/inline-rename.h`, new): a borderless single-line native `EDIT` (the
  address field's control family) laid exactly over the label's text box, in the label's font (Inter Display
  Medium, 14 DIP band / 16 DIP drawer, DPI-scaled) and colours (white on the item's hover surface / the
  drawer canvas). Starts with the name selected. Enter commits, Escape restores, Tab and focus loss commit
  (Explorer's rename); during IME composition Enter/Escape belong to the IME. All other keys are the EDIT's
  own (caret, selection, word moves, text undo, clipboard, IME, Unicode). Commit/cancel are deferred out of
  `WM_KILLFOCUS` and the EDIT is destroyed before the owner's callback runs.
- **Surfaces** draw everything except the title while editing: `Chrome::bandRenaming` +
  `Chrome::BandTitleBox()` (`src/chrome.h`), `Drawer::SetRenaming()` / `TitleBox()` + `editColor` /
  `relayout` hooks (`src/bookmarks-panel.h`); a press elsewhere on the band or drawer ends the edit (commit).
- **Owner** (`src/main.cpp`): `StartFolderRename` / `CommitFolderRename` / `SyncFolderRename` /
  `RenameOrEdit`. Keyed by the stable bookmark id and the profile data folder, never a row index.
  Commit = `bookmarks.Rename` + one undo step « Renamed » + `SaveBookmarks()` + `BookmarksChanged()`; blank
  or unchanged → nothing written (old name kept, never deleted); folder gone or profile changed → dropped
  (no resurrection). « Favori enregistré » is announced only when the save succeeded; a failed save keeps
  the name in memory and announces the existing `bookmark_save_failed` (the retry toast is already up).
  `SyncFolderRename()` runs after every `BookmarksChanged()` (own edits, other windows' merges, profile
  reload), every `Layout()` (resize, DPI, fullscreen, band shown/hidden) and every drawer paint (scroll):
  the editor follows its label or is cancelled when the label is no longer shown.
- `Key()` returns 0 while the editor has focus, like the popover's fields: no app shortcut fires from it.
- **Accessibility**: the EDIT is labelled through the windows' own UIA hosts (`host.label`, the mechanism
  that names the address EDIT) — Edit, Name « Nom » / « Name » (existing `bookmark_field_name`), Value = the
  name, AutomationId `BookmarkRename` — plus Dynamic Annotation (MSAA name) for MSAA clients. No new string;
  `tools/i18n/check_strings.py`: 0 findings.

## Surfaces covered (bounded, honest)

| Entry point | Behaviour |
|---|---|
| Bar folder on the band: menu « Renommer… », F2, item menu « Nouveau dossier », band menu « Nouveau dossier » | in place on the band title |
| Favoris drawer folder row: F2, row menu « Renommer… » / « Nouveau dossier » (row shown) | in place on the row title |
| Sub-folder reached in a native folder menu, a bar folder behind « » », a new folder whose row is not shown | **existing popover**: the label is no longer on screen once the menu closes; no editor in a detached rectangle |
| Links (« Modifier… », F2, star, Ctrl+D) | existing popover, unchanged |
| « Barre de favoris », « Autres favoris » | never renamable (unchanged) |
| Move / delete of folders | unchanged (context menu « Déplacer vers », « Supprimer ») |

## Tests (exact)

1. `tests/bookmarks/inline_rename_test.cpp` — the editor alone on an own window, messages + thread keyboard
   state only, at 96 and 144 DPI: placement on the label box (centred), label font, no border, colours,
   select-all, MSAA name/value, Unicode typing (surrogate pair), EDIT text undo, word selection, no key reaching
   the host, IME composition guard, Enter / Escape / Tab / focus loss (deferred), empty reported to the owner,
   Move / Cancel, a new edit started from the callback. **PASS**.
2. `tests/bookmarks/bookmark_rename_test.cpp` — production `main.cpp`, demo data, own off-screen window,
   native COM UIA client on a second thread: F2 on « Game » → editor on `BandTitleBox` (no popover), band hides
   its title, « Game » selected, Inter Display Medium 14 DIP; UIA Edit « Nom » value « Game »; Ctrl+W / Ctrl+D /
   Ctrl+Z / F5 return 0 from `Key()`; Enter commits « Jeux vidéo » (one undo step, focus back on the band item);
   bookmark undo restores; Escape; blank name keeps « Game »; blur commits; Tab commits; emoji + Japanese;
   menu « Renommer… » (posted keys into the menu's own modal loop); band menu « Nouveau dossier » → its default
   name selected in place → « Musique »; reorder → editor follows; 560 DIP → follows; folder deleted mid-edit
   → cancelled, no resurrection; profile folder changed mid-edit → cancelled, nothing written; sub-folder from
   a menu → popover; link F2 → popover; roots and links refused; drawer F2 → editor on `TitleBox` (UIA Edit
   « Nom »), commit; file written and read back; unknown-format bookmarks file (store fail-closed) → save
   failed, name kept in memory, « Favori enregistré » never logged, `bookmark_save_failed` logged. **PASS**.
3. `tests/e2e/bookmark_rename_e2e.ps1` — real `browser.exe` built from this tree, fresh temp root with a
   seeded bookmarks file, deleted afterwards; UIA SetFocus on `Bookmark.10`, F2 posted to the window,
   `WM_SETTEXT` + Enter / Escape posted to the editor: one editor inside the band on the item (x 112, y 92 DIP),
   UIA keyboard focus on it named « Nom », « Game » before typing; Enter → file has « Jeux vidéo », band item
   renamed; Escape → file unchanged; relaunch → « Jeux vidéo » read back. **PASS** (`app-e2e.txt`).
4. Regression: `test-bookmarks-ui.ps1` (existing bookmarks UI fixture + 1 + 2) **PASS**;
   `test-accessibility.ps1` **PASS** (`host.label` is shared with the address EDIT).

Both fixtures are wired into `test-bookmarks-ui.ps1`. `run_all.py` was not changed or run (scope: no broad
run).

## First failures kept

- `bookmark_rename_test-run1-first-failures.txt`: 17 failures. They were in the test, not the product: the
  band menu opened by pointer has no hovered row (two Down needed), the drawer editor is the font cell (24 px)
  centred on the 20 px title line, the offscreen pixels could not be read from a private member (read back
  through its PNG instead), and the save-failure setup was wrong (a file path as profile folder did not make
  the commit fail; the store's fail-closed unknown format does).
- `bookmark_rename_test-run9-uia-band.txt`: UIA keyboard focus is false in the fixture because it never
  activates its window. The fixture now asserts type, name, value and id; the real app asserts focus.
- `app-e2e-run1.txt`: the script had no UTF-8 BOM, so Windows PowerShell 5.1 mangled « é » on both sides
  of the comparisons. That run proves less than it looks; the BOM was added. `app-e2e-run2.txt`: inside
  the browser the editor was an unnamed Pane (the window's UIA host handles its child HWNDs), so it is now
  labelled through `host.label`.
- Fixture captures before the final run were **invalid** for the editor area: the fixture window is
  off-screen, so GDI never painted the child EDIT and PrintWindow returned stale pixels from an earlier
  editor. The live editor now prints itself (`WM_PRINT`) into the capture.

## Captures

- Real app, 120 DPI: `app-band-before-120.png`, `app-band-renaming-120.png`, `app-band-after-120.png`.
- Fixture window (system 96 DPI): `window-band-before/renaming/after.png`, `window-band-renaming-560.png`,
  `drawer-renaming.png`, `drawer-after.png`.
- Real editor over the offscreen band at **96 and 144 DPI**, 1180 / 560 DIP and a long name
  (`band-*-96-*.png`, `band-*-144-*.png`, `band-long-*`).

## Independent review of `8c5f1ae`: three P2 fixed

Review: root `build/bookmark-inline-rename-independent-review-8c5f1ae.md`. Its external native COM UIA
client confirmed Edit + Value on the band and in the drawer. Fixes are in a distinct commit on top of
`8c5f1ae`. Logs: `design/bookmark-inline-rename-evidence/review-8c5f1ae/`.

1. **Ctrl+A** — the stock single-line EDIT does not select all. `InlineRename` now handles Ctrl+A itself
   (`EM_SETSEL 0,-1`) and drops its WM_CHAR 0x01. During IME composition it leaves Ctrl+A to the IME/EDIT.
   Every other key is still the EDIT's. Red on `8c5f1ae`: 4 failures (2 checks × 96/144 DPI). Green: the
   tracked fixture and the reviewer's probe, unchanged.
2. **Fullscreen** — `FolderLabelBox` refuses band labels while `InFullscreen()`: the chrome is hidden even
   though `bandShown` and the band metadata stay filled. So `SyncFolderRename` (run from `Layout()`) cancels
   an active band editor **without writing**, and none starts. A drawer row editor is kept while the drawer
   is really shown (`Drawer::TitleBox` requires a visible drawer). Root's forthcoming content-only chrome
   state must be refused in the same function (comment left there). Red on `8c5f1ae`: 2 failures
   (cancel; start refused). Green: 3 checks.
3. **DPI change while editing** — `SyncFolderRename` compares the editor's font with the label size at the
   host's current DPI. On a change it creates the new font, gives it to the same EDIT
   (`InlineRename::SetFont`: `WM_SETFONT`, margins, re-centring), then deletes the old font. The EDIT, its
   text, selection and caret are kept. Red on `8c5f1ae`: `-14 -> -14`. Green: `-14 -> -21`, text « Jeux »
   and selection 1–3 kept, editor on the new title box. The reviewer's DPI probe, unchanged: PASS.

**Fixture foreground (disclosed by the review):** before this commit, `bookmark_rename_test`'s drawer section
called the production `Drawer::Show`, which calls `SetForegroundWindow`, despite the fixture's « no
foreground change » note. The fixture compile unit now shims `SetForegroundWindow` (recorded and dropped:
6 production requests per run) and `GetDpiForWindow` (for the DPI transition). Production behaviour is
unchanged. `tests/bookmarks/bookmarks_ui_test.cpp` (pre-existing, not mine) still takes the foreground
through the same path, so it was not rerun for this fix.

## Limits

- Typography: same family, weight and size, but the editor is GDI text (like the address field while
  editing), not DirectWrite. The band's −0.28 DIP tracking is not applied, so glyph positions differ by a
  fraction of a pixel per character. The vertical position is the font cell centred on the label line.
- While editing, the item keeps its width. A longer name scrolls inside the field (`ES_AUTOHSCROLL`, as in
  the long-name capture) and the item takes its new width after the commit.
- An IME composition in progress during a DPI change gets the new font through `WM_SETFONT` on the same
  EDIT; not exercised with a real IME session.
- The drawer's row actions (×) stay drawn at the right of the row while it is renamed; the editor stops
  before them.
- UIA in the real app: the native COM UIA client sees the editor as an Edit with its Value (fixture). The
  managed `System.Windows.Automation` client used by the PowerShell E2E sees it as a Pane named « Nom »,
  exactly as it sees the address bar's EDIT (compared in `app-e2e.txt`).
- 144 DPI with the full browser window was not possible on this 96/120 DPI desktop. 144 is covered by the
  offscreen band at 144 DPI with the real editor at 144-scaled geometry and font, and by the editor fixture.
- The selection inside the field uses the system highlight. The profile-coloured address selection lives
  on the separate branch `VALXXII/profile-address-selection` and is not applied here.

## Integration hunks (for root, after the i18n checkpoint)

- New `src/inline-rename.h`.
- `src/chrome.h`: `bandRenaming`, `BandTitleBox()`, two lines in `DrawBandItem`.
- `src/bookmarks-panel.h`: hooks `editColor` / `relayout`, `renaming_`, `TitleBox()`, `SetRenaming()`,
  `Renaming()`, title skip, `WM_CTLCOLOREDIT`, press → blur, UIA `host.label`.
- `src/main.cpp`: include; `FolderRenameFontPx/Font`, fullscreen refusal in `FolderLabelBox`, font refresh in `SyncFolderRename`; `SyncFolderRename()` in `Layout()` and `BookmarksChanged()`; `Key()` guard; the
  rename block after `EditBookmark`; context menu Edit / new folder, band menu new folder, band F2, drawer
  `d.edit` / `d.context` / `editColor` / `relayout`; `WM_CTLCOLOREDIT`; `WM_LBUTTONDOWN` blur; `host.label`.
  No catalog change.
