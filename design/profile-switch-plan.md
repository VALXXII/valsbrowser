# Same-window profile switch: architecture proposal (task_5773d89042ca)

The user requirement supersedes "one profile per window". A click on a profile switches THIS OS window (same HWND, no relaunch). The old profile's live tabs stay cached for an instant return. Right-click (or Shift+F10 / the context key) on a profile offers « Ouvrir dans une nouvelle fenêtre ».

No code has been changed yet. Development and tests will happen in `build/profiles-switch-qa` with its own temporary root. `build/profiles-qa` (which the user is running) and `build/profiles-app` are not touched.

## Facts from reconnaissance
- **One process = one window.**
  - **WebView2 environment:** created once in `StartEngine()` (main.cpp ~4300), with `EngineOptions.arguments`. Per-environment `AdditionalBrowserArguments` is supported, so per-profile engine flags (autoplay, 3rd-party cookies, LCD text) are possible per environment.
  - **Tabs:** `pages` / `current` globals. `NewTab` → `environment->CreateCoreWebView2Controller(window, …)`. All handlers capture `weak_ptr<Tab>`.
  - **Data roots:** `dataDir`, `profileDir`, `sessionFile`, `settingsFile`, `downloadsFile` and the stores (history, bookmarks, downloads, settings, site volumes, vault, adblock, favicons) are process globals. There are about 40 `dataDir` uses (main 27, audio 3, elements 6, profiles 4), most of them read at call time.
- **WebView2 isolation:** a separate user-data folder means a separate environment and a separate browser process, so cookies are fully isolated per profile. Several environments with different user-data folders can coexist in one process, and controllers of any environment can be parented to the same HWND. Hidden controllers keep their state: DOM, scroll, forms and history all stay alive.
- **Per-tab event surface to audit:**
  - main.cpp: Title, NavigationCompleted, NewWindowRequested, ProcessFailed, WebMessage, favicon (_15), audio (_8/_4/_11), download starting;
  - elements-integration.h: ScriptDialog, Permission, BasicAuth, FullScreen, ContextMenu, ServerCertificate, find.

## Design: in-process workspaces ("swap with ownership guard")
- **`struct Workspace`** (new, `src/profile-workspace.h` + integration). One per profile used in this window. It holds:
  - `Profiles::Paths`;
  - its `environment` (created lazily with the profile's own engine arguments);
  - `pages` / `current`;
  - the window-session claim (`windowId`, live mutex);
  - `settings`, `history`, `bookmarks`, `downloads` and site volumes, plus the vault state;
  - the accent;
  - `lastUsed` and a `cold` flag.
- **The active workspace lives in the existing globals**, so the 4400-line main.cpp keeps working unchanged. To switch:
  1. **Park(active):**
     - SaveSession, then hide every controller (`put_IsVisible(FALSE)`; not closed);
     - stop page-scoped UI (find bar, fullscreen, open popovers);
     - swap every per-profile global into the Workspace object (`std::swap` of containers; paths assigned);
     - reset hover/focus state.
  2. **Activate(target):**
     - swap the target's stash back into the globals. A target new to this window creates its environment (async), loads its stores from its paths and claims or adopts its window session (`WindowSessions::Adopt` on its dataDir);
     - re-apply settings to live tabs, the accent (no fade, as switching windows doesn't fade per P05) and the chrome (button, tabs, bookmarks band);
     - refresh the drawers (history / downloads / settings / bookmarks), then Layout, then show the active tab.
  - It is synchronous on the UI thread, so no handler runs in the middle of a swap.
- **Ownership guard (the safety point):**
  - Every Tab gets `Workspace* owner` (stable pointer; workspaces are never moved).
  - Every async handler resolves its tab, then:
    - **if `owner` is active:** today's behaviour;
    - **if parked:** a background path that touches only `owner`'s own objects: title/uri/favicon on the tab, `owner->history`, `owner` session save to `owner->paths`. Downloads go into `owner->downloads` and continue (never cancelled). New windows open as hidden tabs in `owner`. Permission requests, script dialogs and auth stay deferred (queued per workspace) until it is active again; leaving them pending is today's semantics for background tabs. Fullscreen and find are ignored.
  - A per-handler checklist plus a "parked" fixture test make sure nothing reads the active globals for a parked tab.
- **Bounded cache:**
  - At most 3 live workspaces per window (active + 2 parked, configurable).
  - The least recently used one is evicted to "cold": its session is saved, its controllers closed and its environment released (its browser process exits). Its window-session claim is kept, so its tabs return from disk when it is next clicked.
  - Return from a cold workspace restores URLs only. Scroll and form state are kept only while cached; WebView2 can't serialise them.
  - **Never evicted:** a workspace with downloads in progress or a pending permission/dialog. It stays live and the cache may exceed the bound.

## Behaviours
- **Click a profile row:** switch this window, both cached and cold profiles.
  - The current profile: the menu just closes.
  - Private windows: unchanged (no switch; private menu).
- **Right-click a row / Shift+F10 / context key on a focused row:**
  - opens a small native context menu with « Ouvrir dans une nouvelle fenêtre » (exposed to UIA);
  - it runs `Profiles::Launch(exe, id)`, a new process (today's path);
  - it never triggers the switch.
- **Profile already open in another window:** this window switches anyway. Two windows may show the same profile; WebView2 allows the same user-data folder from several processes, and the sessions stay separate. « Ouvrir dans une nouvelle fenêtre » remains the explicit path.
- **Downloads:**
  - continue in parked workspaces; the downloads drawer shows the active profile's;
  - the toolbar ring aggregates the active profile only, plus a small rule: the success animation shows on return;
  - closing the window while parked downloads run uses the existing confirm-close (extended to every workspace's downloads).
- **Delete:**
  - **A cached or cold profile of this window:** dispose that workspace first (save, close its controllers, release its environment and claim with keep=true), then run the backend Delete.
  - **The active profile (own delete):** switch this window to another profile first; this process then deletes locally and owns the undo. The take-over hand-over is no longer needed for the same window but is kept for other windows.
  - « Rouvrir ses fenêtres » after Undo reopens sessions as before.
- **Window close:** every workspace saves and releases its session (keep for restore). Sidecars are published per workspace.
- **Window counts in the menu:** a cached workspace counts as an open window of that profile, since its session is claimed by this HWND.

## Backend contract changes needed (worker B, profiles.h only)
- Sidecar validation for one HWND hosting several profiles: a property per profile id (or a set), so `LiveWindows`/`FocusOrLaunch`/`Delete` see a cached profile as open in this window. `PublishWindow`/`UnpublishWindow` per (profile, session, hwnd).
- A pure, testable `WorkspaceCache` policy: LRU order, bound, eviction candidates excluding busy ones, the cold set.

## Ownership
- **Me (A):**
  - `src/profile-workspace.h` (Workspace + Park/Activate glue declarations);
  - `profiles-integration.h` (switch flow, eviction, delete integration);
  - `main.cpp` (globals stash/swap, Tab owner, handler guards, per-environment creation with per-profile arguments);
  - `elements-integration.h` / `audio-integration.h` (guards);
  - `profile-menu.h` (right-click/Shift+F10/Apps → « Ouvrir dans une nouvelle fenêtre », UIA);
  - tests `tests/ui/profile_ui_test.cpp`;
  - a new `tests/profile_switch_test.cpp` (pure model + parked-guard fixtures);
  - runtime QA in `build/profiles-switch-qa`.
- **B:** `profiles.h` (multi-profile HWND sidecars, WorkspaceCache policy) + tests.

## Phases
1. Pure model + tests: WorkspaceCache (B); menu right-click/keyboard action + UIA (A).
2. Workspace struct + Park/Activate swap + Tab owner + per-handler guards (A); multi-profile sidecar (B).
3. Per-profile lazy environment with its own arguments; Adopt/claim per workspace; switch UI wiring.
4. Eviction/cold return, downloads, delete of a cached/active profile, window close.
5. Full build, all suites, runtime QA in the separate root:
   - same HWND before and after the switch (GetWindow handle, PID unchanged);
   - cookie isolation (local test server sets a cookie in A, absent in B);
   - cached return keeps scroll and form text;
   - cold return restores URLs;
   - right-click opens a new process;
   - a download continues while parked;
   - deleting a cached profile;
   - private window unchanged.

## Open points for alignment
1. Cache bound: 3 live workspaces per window? (memory: about one WebView2 browser process per profile).
2. When the profile is already open in another window, switch here anyway (proposed) rather than bring that window forward?
3. Per-profile engine flags (autoplay, 3rd-party cookies): applied per environment, so a profile's flags take effect when its environment is created. Is that acceptable?
