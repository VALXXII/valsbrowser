# Mode Focus motion — the one-line changes for the chat file (`src/chat-host-ui.h`, lot R's)

## 1. Top bar layout: the card's right edge (applied, in main since 0.3.1062)

`PanelRect`, Mode Focus branch: `right = c.right - S(chrome.ClientWidth() - card.right)` — the card's right edge follows
the entry / exit (W → W − 4) instead of stepping 4 DIP sideways. Nothing more to do.

## 2. Side layouts (#127): the card's top (applied on `VALXXII/focus-transition`, coordinator's go 2026-11-26: lot R is in main)

Why: in the side layouts (branch `VALXXII/focus-transition`, design/focus-motion-report.md « Side layouts ») the chat card
(mode Panneau) keeps its left, right and bottom edges between the normal layout and Focus; only its top moves, with the
page's top (complete 40 / compact 32, plus the annotation banner → 4, or under the banner). `PanelRect`'s #127 branch reads
`chrome.ContentTop()`, the live layout's top, which switches at once at the start of an entry (and at the end of an exit):
the card's content jumps 36 DIP while the page glides (the motion's canvas band hides the card's top meanwhile).
`FocusMotionChatTop()` (src/main.cpp, Mode Focus's own code, declared at the top of main.cpp next to
`FocusChatCard`) returns the moving top during the motion and `chrome.ContentTop()` otherwise.

Where: `src/chat-host-ui.h`, function `RECT PanelRect(float shown)`, the `if (chrome.sidebar)` branch, its return line.

Before:

```cpp
        return {left, S(chrome.ContentTop()), left + w, c.bottom - in + (AnnotationBandSliding() ? S(chrome.annotationBand) : 0)};
```

After:

```cpp
        return {left, S(FocusMotionChatTop()), left + w, c.bottom - in + (AnnotationBandSliding() ? S(chrome.annotationBand) : 0)};   // Mode Focus motion: the card's top glides with the page's (src/main.cpp)
```

Effect:
- At rest: identical (`FocusMotionChatTop()` is `chrome.ContentTop()` when no motion is on screen).
- During a Focus entry / exit in the side layouts: the card's top moves with the page on the same spring, no step.
- Before `VALXXII/focus-transition` is merged: does not compile (`FocusMotionChatTop` is defined there): apply it with or
  after that branch.

Nothing else in the chat files is needed for the motion.
