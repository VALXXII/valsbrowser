# URL loading surface

Implemented on main. Additional white fill at8% opacity, covering the URL bar height with the existing6-DIP rounded silhouette. The URL, reload and copy icons remain above the background. Embedded button surfaces repaint the same overlay once, avoiding a second layer.

Each tab owns a native Loading::State. NavigationStarting starts15%, retaining the same navigation ID across redirects. SourceChanged advances to65%. A matching NavigationCompleted reaches100%; stale completion cannot complete a newer load. This is milestone progress, not a measured network percentage. Background tabs retain independent state; selecting one displays its current state. Home clears the progress. Errors retain the existing stop policy and clear loading state.

Width uses the shared CSS ease-out curve cubic-bezier(0,0,.58,1) over360ms. Completion reaches full width over360ms then fades out over200ms. Reduced motion uses immediate milestone widths and an opacity fade. The shared native chrome scheduler wakes only during interpolation/fade, stops on waits/idle and pauses while minimised. No JS, new renderer or polling timer.

Native fixture tests/loading_test.cpp covers events, redirects, stale IDs, tab isolation, reduced motion, idle wakes, half/full width and actual8% Direct2D pixels. Run python test-lottie.py (now six native fixtures). Offscreen sample: build/url-loading/loading-half.png.

All six native fixtures passed. Installed target: build/v2/browser-display-paced.exe; Windows browser shortcut updated and verified, existing build/v2/profile retained. No running process closed.

Compile-only package: build/url-loading/browser.exe. No browser/WebView2 runtime launched; live navigation and desktop timing remain unverified under the existing runtime hold. The prior download-dialog assertion is unchanged. Paper sync pending due MCP quota; see paper-sync-pending.md.

Final scheduling now uses the on-demand compositor frame clock rather than16ms WM_TIMER frame wakes; see [display-pacing.md](display-pacing.md). User approved the interactive native preview; no further benchmark work is pending.
