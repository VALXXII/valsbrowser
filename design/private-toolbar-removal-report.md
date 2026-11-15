# Private-window button removed from the toolbar

Request (user): remove the detective / private-browsing icon from the toolbar, since « Nouvelle fenêtre privée » is now
an entry of the profile menu. A separate commit on PR #76 after 1e9b21c (profile-only blur, already reviewed).

## What changed

`src/chrome.h`
- The `'P'` region (button, tooltip, hit, focus stop) is no longer built. The right cluster is compacted with the same
  sizes and gaps: Téléchargements takes the freed slot (`width-67..-43`), Historique `width-97..-73`, Son des sites
  (`AudioRight/AudioLeft` 133/157 → 103/127), Annoter 6 DIP left of whichever comes first (`AnnotateRight`: 127 → 97
  without audio). Réglages, the separator (`width-37`) and the 13 DIP around it are unchanged.
- The address field ends 6 DIP before the first toolbar button as before; it gains exactly the 30 DIP freed
  (`bar.right` formula unchanged, it reads the moved constants). The download-icon dirty rectangle follows its button
  (`cx = width-55`).

`src/main.cpp` — the UIA toolbar list `{'V','Y','D','P','S'}` → `{'V','Y','D','S'}`.

Kept on purpose: command `'P'` (`OpenPrivateWindow`), Ctrl+Maj+N (`Key` → `'P'`), the profile menu's private entry
(`PM::Action::Private`), the private window's own indication (incognito profile slot « Fenêtre privée », OS title
« (navigation privée) »), private-window policies, all assets/glyphs. Left as dead but harmless, to avoid touching
localized strings that the i18n work owns: the painter's `case 'P': icon=L"private"` and `AccessibleButton('P')`
(nothing builds a `'P'` region any more, so neither is reached from the toolbar).

`tests/annotations/profile_color_e2e.py` — its posted click on « Annoter » used the old `AnnotateRight()` (133);
now 103.

## Tests

| Evidence | Kind | Result |
| --- | --- | --- |
| `03-ui-fixture-GREEN.txt` (`test-ui.ps1`, new block in `tests/ui/ui_test.cpp`) | native chrome renderer, no WebView2 | 96/144 DPI × 560/1180 DIP × normal/private × audio on/off × annotation on/off: no `'P'` region; `Enabled('P')`; exact rects of Réglages / Téléchargements / Historique / Son; 24 DIP buttons with constant 6 DIP gaps; separator kept; field ends 6 DIP before the first button and is exactly 30 DIP longer; hit at every centre (old private centre now hits Téléchargements); star at the field's new end; nothing of the field overlaps the cluster; focus order ends `[N] [V] Y D S`; private window's profile slot intact. PASS |
| `02-ui-fixture-RED-on-previous-chrome-h.txt` | same fixture, previous `src/chrome.h` | FAIL at « no private-window button region » (the checks discriminate) |
| `04a/04b-accessibility-*.txt` (`test-accessibility.ps1`) | real UIA client on the production provider (no WebView2) | toolbar reading order without « Nouvelle fenêtre privée »; no such button anywhere. PASS |
| `05-shortcuts.txt` (`test-shortcuts.ps1`) | production dispatcher | Ctrl+Maj+N → `'P'`, `'P'` enabled. PASS |
| `07-e2e-private-toolbar-GREEN.txt` (`tests/e2e/profiles_switch_e2e.ps1 -PrivateToolbarOnly`) | **real app**, isolated copy | UIA toolbar ids end History, Downloads, Settings, no Private; real on-screen gaps 6 / 13 DIP; field before Historique; profile menu « Nouvelle fenêtre privée » invoked (UIA) → a `--private` process of the copy; that private window's toolbar has no private button, its profile slot says « Fenêtre privée » and its title « (navigation privée) »; command `'P'` posted as the shortcut message → another private window. 9/9 |
| `06-e2e-private-toolbar-RED-main-d060366.txt` | same, unmodified main build | 3 failures: exactly the removal checks; menu entry, command and private indication pass on both |
| `08-annotation-profile-color-e2e.txt` | real app (annotation suite) | Annoter clicked at its new place, mode starts; 0 failures |
| `09a/09b/09c-profiles-switch-e2e-full-*.txt` | real app | the whole script (existing sections + T transitions + P). Kept in order: 09a, run concurrently with the annotation e2e and the UI fixture, failed one pre-existing H1 timing check (« its slot claim was released », profile deletion: no code touched here); 09b stopped on a harness `ReadAllText` sharing violation on the app's log (pre-existing `LogText`); 09c, alone: 83/83 |
| `01-before-after-captures.txt` + `before-*/after-*.png` | offscreen renderer (`toolbar_shot.cpp`, compiled against d060366's `src/` and the branch's) | geometry printed per capture; the detective icon is gone and the field reaches 6 DIP before Historique |

Limits
- The PNGs come from the offscreen renderer (no host painters): the audio / annotate glyphs and the private slot's
  eye-slash are not drawn there, only their regions exist; the real-window checks are UIA bounds and names, not pixels.
- Mouse clicks and the real Ctrl+Maj+N key press were not injected (no SendInput, no focus change); the key mapping is
  unit-tested and the command it yields was posted to the real window.
- 560 DIP and 144 DPI are covered by the renderer fixture; the real app ran at its default size and the machine's DPI.
- Process launch of the private window: real (two launches, both from the isolated copy; stopped by the script).
