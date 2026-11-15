# Import from a browser: plan and availability matrix

Réglages › Confidentialité › Données › **Importer depuis un navigateur…** / **Import from a browser…**
(task_fb373e25a2ec). Bookmarks and history only, plus an HTML bookmark file. Cookies are shown and
**unavailable** (see "Cookies" below).

## Availability matrix (this delivery)

| Source | Bookmarks | History | Cookies |
|---|---|---|---|
| Chrome (`%LOCALAPPDATA%\Google\Chrome\User Data\<profile>`) | yes: `Bookmarks` JSON | yes: `History` SQLite, table `urls` | not available |
| Edge (`%LOCALAPPDATA%\Microsoft\Edge\User Data\<profile>`) | yes: same JSON contract, checked per file | yes: `History`, schema checked per query | not available |
| Brave (`%LOCALAPPDATA%\BraveSoftware\Brave-Browser\User Data\<profile>`) | yes: same | yes: same | not available |
| Firefox (`%APPDATA%\Mozilla\Firefox\profiles.ini` → relative profile paths) | yes: `places.sqlite` `moz_bookmarks` + `moz_places` | yes: `places.sqlite` `moz_places` | not available |
| HTML bookmark file (Netscape format, any browser's export) | yes | n/a (the format holds no history) | n/a |

Chromium profiles = sub-folders `Default` and `Profile N` that hold `Bookmarks` and/or `History`. Names
come from the folder name only: `Local State` is never read (it also holds key material we must not
touch). Firefox: only `IsRelative=1` profiles whose path stays inside the Firefox folder (no `..`, no
absolute path, no reparse point).

## Source contracts (primary documentation, checked per source)

* Chromium `Bookmarks`: JSON object `roots` → `bookmark_bar`, `other`, `synced`; each node `type`
  (`url`/`folder`), `name`, `url`, `date_added` (decimal string, µs since 1601-01-01 UTC), `children`.
  Read with the in-house strict RFC 8259 parser (src/strict-json.h), bounds below. Unknown members ignored.
* Chromium `History` (SQLite): `urls(url, title, visit_count, last_visit_time, hidden)`;
  `last_visit_time` µs since 1601. Edge/Brave/older Chrome may lack `hidden`: the query falls back to
  the same query without that column; any other schema mismatch reports that category as unsupported.
* Firefox `places.sqlite`: `moz_places(url, title, visit_count, last_visit_date, hidden)`;
  `moz_bookmarks(id, type 1=link 2=folder 3=separator, fk, parent, position, title, dateAdded, guid)`;
  PRTime = µs since 1970. Roots by GUID `toolbar_____`, `menu________`, `unfiled_____`, `mobile______`.
* HTML: `<DT><H3 ...>folder</H3><DL>…</DL>`, `<DT><A HREF="…" ADD_DATE="s">title</A>`; entities
  decoded; tags/scripts never executed (pure text tokenizer).

## Dependency: SQLite

Windows' own **winsqlite3.dll** (System32, part of Windows 10/11), loaded with
`LoadLibraryExW(…, LOAD_LIBRARY_SEARCH_SYSTEM32)`; prototypes of the stable documented SQLite C API are
declared locally (no header, no new package, nothing added to the release). If it is missing, the
database-backed categories say "unavailable on this Windows"; bookmarks from Chromium JSON and HTML still
work.

## Consistent, non-modifying reads

* SQLite: URI `file:…?mode=ro` + `SQLITE_OPEN_READONLY | SQLITE_OPEN_URI | SQLITE_OPEN_NOFOLLOW`, never
  `immutable=1`, never a copy of a live database. All queries run in ONE read transaction
  (`BEGIN` … `COMMIT`): one consistent snapshot, WAL included. 2 s busy timeout. When the source browser
  holds an exclusive lock or a hot journal needs recovery, the import stops with "Close <browser> and try
  again" (retry keeps the dialog open). Nothing is written next to the source.
* Files are opened read-only with sharing, refused when they are reparse points or not regular files.
* Discovery reads only directory metadata and `profiles.ini`; content is read only after **Importer**.

## Bounds (untrusted input)

Bookmarks JSON ≤ 32 MiB, nesting ≤ 256, items ≤ 1 M; HTML ≤ 32 MiB, nesting ≤ 256; SQLite rows read:
bookmarks ≤ 200 000, history ≤ 50 000 newest; titles clipped by the store (1 024 units); URLs must pass
`AddressBar::Recordable` (http/https/file, < 4 096) — everything else (javascript:, chrome://, place:,
data:, about:) counted as **unsupported**. Search-result pages are excluded from history as for normal
browsing.

## Destination and rules

* Destination = the profile shown in this window (name in the dialog). The read runs on a worker
  thread; the commit runs on the UI thread only if that same workspace is still the one on screen,
  otherwise nothing is written ("profile changed, nothing imported").
* Commit = the existing interprocess transaction (src/profile-store.h: profile mutex, re-read, three-way
  merge, atomic write). The import is applied INSIDE the merge, onto the file's current state, so other
  windows' changes are kept and duplicates are judged against what is really on disk. Any failure
  (busy, damaged/unknown file, write error) writes nothing and the dialog says so, with retry.
* Bookmarks: appended under **Autres favoris › « Importés de Chrome »** (profile folder appended for a
  non-default Chromium profile, « Importés d'un fichier HTML » for a file). Source roots become sub-folders
  (Barre de favoris, Autres favoris, Mobile / Firefox menu…); order, titles, folders kept.
  * Repeat import: folders are matched by title in the same parent (k-th same-titled source folder ↔ k-th
    destination folder); a link is a **duplicate** when the same folder already holds that URL (UrlKey) as
    many times as the source does. Intentional repeats in distinct folders, or twice in one folder, are
    therefore kept, and a second import of an unchanged source adds nothing.
  * Capacity: store limit 10 000 nodes, depth 8. Over the limit → **not imported (full)**; a folder that
    would sit deeper than 8 → its whole subtree **not imported (too deep)**, counted. Never flattened.
* History (store = 1 000 pages, one row per URL with visit count + last visit): source rows newest first.
  New URL → added with its source last-visit time and count while there is room; existing URL →
  **duplicate**: count = max, last visit = max (repeat import is idempotent, nothing is replaced, existing
  pages are never evicted); no room → **not imported (full)**.
* Timestamps: Chromium `t µs since 1601` → `t/1000 − 11 644 473 600 000` ms; Firefox `t/1000`; HTML
  `ADD_DATE` seconds (values ≥ 10¹⁴ read as µs, ≥ 10¹¹ as ms); ≤ 0 → now (bookmarks) / skipped
  (history); future values clamped to now.

## Report

Per category: imported · already present · unsupported · not imported (full / too deep) · failed.
"Importation terminée" only when every selected category committed; otherwise "Importation partielle"
or "Importation impossible" with the reason. No personal URL/title is logged: log lines carry counts only.

## Cookies (unavailable in this delivery)

Shown as a disabled check with "Non disponible : vos connexions ne sont pas transférées. Reconnectez-vous
aux sites." No cookie, session or key material is read from any browser. Scope decision recorded in
design/browser-data-import-report.md.

## Tests (synthetic roots only, never `%LOCALAPPDATA%`)

* tests/browser-import/browser_import_test.cpp (pure + Win32 + winsqlite3): discovery on injected roots;
  Unicode nested bookmarks; HTML export; duplicates/repeat import; malformed/oversized JSON/HTML;
  depth/capacity; timestamps; WAL database and an exclusively locked database (another connection holds
  `locking_mode=EXCLUSIVE`) → busy, source bytes unchanged; source read failure; destination save failure
  (unknown format / locked) → nothing written; history merge idempotence.
* Modal: `Select::checks` updates availability per source (modal test).
* End-to-end: real browser.exe with an isolated data root and `--import-root` synthetic sources, driven
  by UIA in FR and EN; PrintWindow captures of its own window only.
