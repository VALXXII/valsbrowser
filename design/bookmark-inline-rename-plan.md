# Bookmark folders: rename in place — plan

User request (2026-11-15, screenshot of the bar folder « Game »): « Renommer » opens the full bookmark
popover (Nom / Dossier / Supprimer / Terminé). The user wants the name edited **directly where it is
written**, next to the folder icon: no floating frame. Interaction fixed by root: the name is selected for
replacement, **Enter** commits, **Escape** cancels.

Base: `75c544b` (branch `VALXXII/bookmark-inline-rename`). Native Win32 + Direct2D chrome, no new library.

## Current flow (audit of 75c544b)

- `BookmarkContextMenu` (`main.cpp`): « Renommer… » (folders) / « Modifier… » (links), F2 → `EditBookmark`.
- Band F2 (`BandKey`) → `EditBookmark(id, region)`.
- « Nouveau dossier » (item menu, band menu) → `NewFolder` then `EditBookmark` on the new folder.
- Drawer F2 / row menu → `d.edit` → `EditBookmark(id, rowRect)`.
- `EditBookmark` always opens `BookmarkPopover` (folders too: Nom, Dossier, Supprimer, Terminé);
  `CommitBookmark` applies rename/address/move with one undo entry, `SaveBookmarks`, `BookmarksChanged`,
  « Favori enregistré ».
- Band title: Direct2D/DirectWrite, Inter Display **Medium 14 DIP**, tracking −0.28, between the 16 DIP icon
  (+6 gap) and the folder chevron (12 DIP, 6 gap), opacity secondary → primary on hover; long titles fade
  out. Drawer row title: Inter Display Medium 16 DIP (`Panel::line`), raised 1.5 DIP.

## Design

### One small native editor, `NativeUI::InlineRename` (`src/inline-rename.h`)

A borderless single-line `EDIT` (the same control family as the address field, which already edits the
URL in place with Inter Display through GDI), created as a child of the surface that draws the label,
exactly over the label's text box:

- same family and weight (Inter Display Medium, private font already registered), same size (14 DIP band,
  16 DIP drawer), DPI-scaled; text colour = the label's hovered colour, background = the item's own
  surface: no border, no shadow, no frame. The surface hides its own label while editing (the icon and
  chevron stay drawn by the surface).
- Starts with the whole name selected (EM_SETSEL 0,-1), caret at the end.
- **Enter** commits, **Escape** restores (nothing written), **Tab** commits (leaves the field like a native
  rename), **focus loss** (click elsewhere, window deactivated, surface hidden) **commits** — Explorer's
  rename pattern. During IME composition Enter/Escape belong to the IME (same rule as the address field:
  `WM_IME_STARTCOMPOSITION/ENDCOMPOSITION`).
- Every other key is the EDIT's: caret, selection, Ctrl+←/→, Home/End, Ctrl+Z (text undo), Ctrl+A,
  cut/copy/paste, IME, Unicode. `Key()` returns 0 while it has focus, like the popover's fields: no app
  shortcut (Ctrl+W, Ctrl+D, F5, Ctrl+Z bookmark undo…) and no folder opening/drag can start from it.
- Accessibility: the EDIT is a native Edit in the UIA tree (value = the name, focus = keyboard focus); its
  Name is set with documented Dynamic Annotation (`IAccPropServices::SetHwndPropStr`, PROPID_ACC_NAME) to
  the existing catalog string `bookmark_field_name` (« Nom » / « Name »). No new string.

### Commit (owner: `main.cpp`)

`RenameFolderInline(id, newTitle)`:

- **Keyed by the stable bookmark id**, never a row index: the editor carries the id; the commit re-finds it.
- trimmed empty → **nothing changes** (old name kept, folder never deleted); unchanged → nothing written.
- folder gone (another window deleted it, profile switched) → nothing applied (no resurrection).
- otherwise `bookmarks.Rename` + one undo step « Renamed » (`bookmark_undo_edit`) + `SaveBookmarks()` +
  `BookmarksChanged()` — the same paths as the popover. Announcement: « Favori enregistré » only if the
  save succeeded; if `SaveBookmarks` failed, its existing sticky toast (`bookmark_save_failed`, Réessayer)
  stays the message and the announcement says the same — never a false « saved ». The new name stays in
  memory (existing failure contract).

### Anchors (where the editor goes) — bounded, honest

| Entry point | Editor |
|---|---|
| Bar folder visible on the band: menu « Renommer… », F2, « Nouveau dossier » created on the bar | in place on the band item's title |
| Drawer folder row (F2, row menu « Renommer… », « Nouveau dossier » while the drawer shows the row) | in place on the row's title |
| Folder only reachable inside a native menu (sub-folder in a folder menu, a bar folder behind « » ») | **existing popover**, unchanged: the label is no longer on screen once the menu closes, so no editor is invented in a detached rectangle |
| Links (« Modifier… »), Ctrl+D, star | existing popover, unchanged |
| Root folders (« Barre de favoris », « Autres favoris ») | never renamable (unchanged) |

### Lifecycle

`SyncInlineRename()` runs after every `BookmarksChanged()` (own edits, other windows' merges, profile
reload) and `Layout()` (resize, DPI, fullscreen, band shown/hidden), and from the drawer's scroll/rebuild:

- the folder no longer exists, is no longer a folder, the profile data folder changed, or its label is no
  longer visible on the surface that started the edit → **cancel** (nothing written);
- still visible → the editor **moves** to the label's new box (reorder, resize, scroll).

Focus after the edit: keyboard-started edits return to the band item (region) or the drawer row; pointer
ones leave focus where the user clicked.

## Tests (red → green)

1. `tests/bookmarks/inline_rename_test.cpp` (pure native, own window, messages only): the editor's
   position/font/colours, select-all on start, Enter / Escape / Tab / blur, IME composition guard, Unicode,
   EDIT text undo, empty/whitespace, keys never reaching the host, UIA name/value through
   `AccessibleObjectFromWindow`, 96/144 DPI.
2. `tests/bookmarks/bookmark_rename_test.cpp` (production `main.cpp`, like `bookmarks_ui_test`, demo data,
   own folder): context menu / F2 / new folder → editor on the band title (no popover), commit → store,
   undo, file written and read back; Escape restores; empty keeps; delete from « another window » / profile
   folder change mid-edit cancels without resurrection; resize to 560 DIP moves or cancels; drawer row
   rename; sub-folder / overflow → popover (documented fallback); links still use the popover; save failure
   (unwritable profile folder) never announced as saved. PrintWindow captures at 96 and 144 DPI, narrow
   width, long name.
3. Isolated real `browser.exe` (temp root, seeded bookmarks file with « Game » on the bar): UIA focus on the
   band item, F2 by window message, name typed with `WM_SETTEXT`, Enter by message; file read back and
   relaunch shows the new name; Escape case. No SendInput, no foreground forcing, no clipboard.
