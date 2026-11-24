# Mode Focus motion — the one-line change for lot R (`src/chat-host-ui.h`)

Why: during the Mode Focus entry / exit (branch `VALXXII/focus-transition`, design/focus-motion-report.md), the chat card
(mode Panneau) follows the page's top and bottom, but its right edge is fixed at W − 4 by `PanelRect`, so it steps 4 DIP
sideways at the switch (normal: flush with the window's right edge; Focus: 4 DIP in). `FocusChatCard()` (src/main.cpp,
Mode Focus's own code) already returns the moving card during the motion; `PanelRect` only has to read its right edge too.

Where: `src/chat-host-ui.h`, function `RECT PanelRect(float shown)`, the Mode Focus branch:

```cpp
    if (chrome.floating) {   // Mode Focus (Paper 203J-0): the card, 4 DIP from the window's right and bottom edges, under the banner
        const auto card = FocusChatCard(PanelDip()); const int in = S(NativeChrome::FocusMode::chatInset), right = c.right - in;
        const int left = right - w + int(std::lround((1 - shown) * (w + in)));
        return {left, S(card.top), left + w, S(card.bottom)};
    }
```

Before (the second line of that branch):

```cpp
        const auto card = FocusChatCard(PanelDip()); const int in = S(NativeChrome::FocusMode::chatInset), right = c.right - in;
```

After:

```cpp
        const auto card = FocusChatCard(PanelDip()); const int in = S(NativeChrome::FocusMode::chatInset), right = c.right - S(chrome.ClientWidth() - card.right);   // Mode Focus motion: the card's right edge follows the entry / exit (FocusChatCard), W - 4 at rest
```

Effect:
- At rest: identical (`card.right` is W − 4, so `right` is `c.right - S(4)` = `c.right - in`).
- During the Focus entry / exit: the card's right edge moves with the page on the same spring (W → W − 4 and back), no step.
- Before `VALXXII/focus-transition` is merged: a no-op (`FocusChatCard` never returns a moving card).

Nothing else in lot R's files is needed for the motion.
