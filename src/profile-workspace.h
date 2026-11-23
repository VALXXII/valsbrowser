// Same-window profiles (task_5773d89042ca; part of main.cpp, included just before wWinMain so it sees every
// profile-scoped global). One OS window, several profiles: clicking a profile switches THIS window; the
// previous profile stays alive, hidden (its WebView2 controllers keep DOM, scroll and forms), and comes back
// at once. « Ouvrir dans une nouvelle fenêtre » (right-click / Maj+F10 / Menu on a profile row) is the
// explicit way to get another window.
//
// Model
//  * A Workspace is one profile in this window: its own WebView2 environment on its own user data folder
//    (cookies / storage isolated by the engine), its tabs, its window session claim, its settings, history,
//    bookmarks, downloads, site icons, site volumes, vault, ad-block state and identity.
//  * The workspace "in the globals" is the one main.cpp's code works on. The others hold their state in their
//    stash. Exchange() swaps a workspace with the globals (O(1) swaps, UI thread only).
//  * Ownership: every COM callback and deferred job remembers the workspace that was in the globals when it
//    was created (Handler / Defer) and runs inside a WorkspaceScope of it. A hidden profile's tab therefore
//    writes ITS history / session / downloads / icons / passwords, never the profile on screen; and while a
//    parked workspace is in the globals, every UI entry point is inert (WorkspaceParked()).
//  * Bounded cache: at most 3 live workspaces (active + 2 parked); the least recently used idle one is made
//    COLD (session saved, controllers closed, engine released, claim kept): its tabs come back from disk
//    (URLs only). A workspace with a download running, a save dialog waiting or a page dialog pending is
//    never made cold (it may exceed the bound).
#include <memory>

struct WorkspaceStash {
    Com<ICoreWebView2Environment> environment;
    std::vector<std::shared_ptr<Tab>> pages; int current = -1; std::vector<ClosedTab> closedTabs; bool sessionReady = false;
    std::wstring dataDir, profileDir, sessionFile, downloadsFile, settingsFile, profileId;
    BrowserSettings::State settings, settingsBase;
    AddressBar::History history, historyBase; ProfileStore::Stamp historySeen, bookmarksSeen;
    std::map<std::wstring, std::shared_ptr<const NativeChrome::FaviconImage>> siteIcons; std::set<std::wstring> siteIconsMissing; std::map<std::wstring, size_t> siteIconsWritten;
    std::vector<DL::Item> downloads, downloadsBase; bool downloadFolderOnProfile = false;
    std::vector<std::shared_ptr<PendingDownload>> pendingDownloads;
    bool adBlockSynced = false, adBlockBusy = false, adBlockAgain = false, adBlockNoProfileLogged = false;
    BM::Store bookmarks, bookmarksBase; BM::Id lastFolder = BM::BarRoot; bool bookmarksSaveFailed = false;
    AU::Volumes audioVolumes;
    Vault::Store vault, vaultBase; bool vaultLoaded = false; std::set<unsigned> vaultPromptOpen; std::map<unsigned, Vault::CaptureBinding> vaultCapture;
    std::wstring windowId; HANDLE windowLive = nullptr; bool windowForced = false; std::vector<HANDLE> windowReservations;
    std::map<int, SV::Orientation> splitGroups; int nextSplitGroup = 1;
};
struct Workspace {
    std::wstring id; WorkspaceStash s;
    bool started = false;   // its stores were loaded and its session claimed in this window
    bool cold = false;      // no engine, no controllers: its tabs come back from its session file
    bool dead = false;      // disposed (profile deleted / window closing): late callbacks are dropped
    std::uint64_t used = 0;
    std::wstring slotSession; HANDLE slotLive = nullptr;   // a cached slot restored from the window set, not started yet
    bool publishDirty = false;   // its titles changed while parked: its own sidecar is republished (L1)
    bool enginePending = false;  // its environment is being created: the completion owns the lease from then on
    bool engineFailed = false;   // its engine failed (creation or a controller): handled once, retried on the next switch
    unsigned long long failedAt = 0;   // tick of its last engine failure: never a fallback for a while
};
std::vector<std::unique_ptr<Workspace>> workspaces;   // never freed before exit: callbacks hold raw pointers
Workspace* activeWorkspace = nullptr;                  // the one on screen
Workspace* globalsWorkspace = nullptr;                 // the one whose state is in the globals now
std::uint64_t workspaceClock = 0;
std::uint64_t workspaceSwitches = 0;                   // bumped by every switch of the window (scope restore, M2)
constexpr int LiveWorkspaceBound = Profiles::WorkspaceCache::DefaultLive;

template<class T> void SwapCom(Com<T>& a, Com<T>& b) { std::swap(a.p, b.p); }
// Globals <-> stash. Called twice per change of globals (the one leaving, then the one arriving).
void Exchange(Workspace& w) {
    auto& s = w.s;
    SwapCom(environment, s.environment);
    std::swap(pages, s.pages); std::swap(current, s.current); std::swap(closedTabs, s.closedTabs); std::swap(sessionReady, s.sessionReady);
    std::swap(dataDir, s.dataDir); std::swap(profileDir, s.profileDir); std::swap(sessionFile, s.sessionFile); std::swap(downloadsFile, s.downloadsFile); std::swap(settingsFile, s.settingsFile); std::swap(profileId, s.profileId);
    std::swap(settings, s.settings); std::swap(settingsBase, s.settingsBase);
    std::swap(history, s.history); std::swap(historyBase, s.historyBase); std::swap(historySeen, s.historySeen); std::swap(bookmarksSeen, s.bookmarksSeen);
    std::swap(siteIcons, s.siteIcons); std::swap(siteIconsMissing, s.siteIconsMissing); std::swap(siteIconsWritten, s.siteIconsWritten);
    std::swap(downloads, s.downloads); std::swap(downloadsBase, s.downloadsBase); std::swap(downloadFolderOnProfile, s.downloadFolderOnProfile);
    std::swap(pendingDownloads, s.pendingDownloads);
    std::swap(adBlockSynced, s.adBlockSynced); std::swap(adBlockBusy, s.adBlockBusy); std::swap(adBlockAgain, s.adBlockAgain); std::swap(adBlockNoProfileLogged, s.adBlockNoProfileLogged);
    std::swap(bookmarks, s.bookmarks); std::swap(bookmarksBase, s.bookmarksBase); std::swap(lastFolder, s.lastFolder); std::swap(bookmarksSaveFailed, s.bookmarksSaveFailed);
    std::swap(audioVolumes, s.audioVolumes);
    std::swap(vault, s.vault); std::swap(vaultBase, s.vaultBase); std::swap(vaultLoaded, s.vaultLoaded); std::swap(vaultPromptOpen, s.vaultPromptOpen); std::swap(vaultCapture, s.vaultCapture);
    std::swap(windowId, s.windowId); std::swap(windowLive, s.windowLive); std::swap(windowForced, s.windowForced); std::swap(windowReservations, s.windowReservations);
    std::swap(splitGroups, s.splitGroups); std::swap(nextSplitGroup, s.nextSplitGroup);
}
void BringToGlobals(Workspace* w) {
    if (!w || w == globalsWorkspace) return;
    if (globalsWorkspace) Exchange(*globalsWorkspace);
    Exchange(*w); globalsWorkspace = w;
}
bool unownedCallbacks = false;   // a callback that must outlive its workspace (engine exit after disposal)
Workspace* WorkspaceNow() { return unownedCallbacks ? nullptr : globalsWorkspace; }
bool WorkspaceParked() { return globalsWorkspace && activeWorkspace && globalsWorkspace != activeWorkspace; }
bool WorkspaceDead(Workspace* w) { return w && w->dead; }
WorkspaceScope::WorkspaceScope(Workspace* w) {
    if (!w || w == globalsWorkspace) return;
    previous = globalsWorkspace; switches = workspaceSwitches; BringToGlobals(w); entered = true;
}
// The scope restores what was in the globals before it, unless the window switched meanwhile (a job run in a
// parked scope that switched the window, nested or not): then the workspace ON SCREEN comes back, never a
// parked one, so the UI never stays inert (review M2). A dead previous one is never brought back either.
WorkspaceScope::~WorkspaceScope() {
    if (!entered) return;
    if (switches != workspaceSwitches || !previous || previous->dead) BringToGlobals(activeWorkspace); else BringToGlobals(previous);
}

void DropDownloadOps(Workspace* owner); void WriteThisWindowSet(); void ReleaseEngineWhenExited(); std::vector<Workspace*> LiveWorkspaces(); std::wstring WorkingFallback(const std::wstring& failing); extern bool workspaceTest, qaControllerFailure, qaExpectTerminal; extern int qaEngineFailure; bool WorkspaceEngineFailed(const std::wstring& where, HRESULT hr, bool hasEngine); bool SwitchWorkspace(const std::wstring& id);
// --workspace-test: the completion runs later with a failure, like an asynchronous creation error.
void PostDeferredFailure(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* done) {
    unownedCallbacks = true; Defer([done] { done->Invoke(HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND), nullptr); done->Release(); }); unownedCallbacks = false;
}
// ---- engine leases of this process (one per live environment of a profile) ---------------------------------
// AcquireEngineArgs records this process as a holder of the profile's engine; it stops being one only when
// its LAST environment on that folder has exited. A cold workspace warmed again before its old engine exited
// holds twice: the late exit of the old one must not release the new one's lease.
std::map<std::wstring, int> engineHolds;
void EngineHeld(const std::wstring& id) { ++engineHolds[id]; }
int EngineHoldCount(const std::wstring& id) { auto it = engineHolds.find(id); return it == engineHolds.end() ? 0 : it->second; }
void EngineDropped(const std::wstring& root, const std::wstring& id, const wchar_t* why) {
    auto it = engineHolds.find(id); if (it == engineHolds.end()) return;   // never leased here (smoke / private)
    if (--it->second > 0) { Log(L"PROFILES engine of " + id + L" " + why + L", still held here"); return; }
    engineHolds.erase(it); Profiles::ReleaseEngineArgs(root, id); Log(L"PROFILES engine of " + id + L" " + why + L", lease released");
}
// ---- life cycle ------------------------------------------------------------------------------------------
// The workspace the process started with: it adopts whatever is in the globals (startup ran as before).
void StartWorkspaces() {
    if (!workspaces.empty()) return;
    auto w = std::make_unique<Workspace>(); w->id = profileId; w->started = true; w->used = ++workspaceClock;
    activeWorkspace = globalsWorkspace = w.get(); workspaces.push_back(std::move(w));
}
Workspace* FindWorkspace(const std::wstring& id) { for (auto& w : workspaces) if (!w->dead && w->id == id) return w.get(); return nullptr; }
bool WorkspaceBusy(Workspace& w) {
    if (popups.Pending(&w)) return true;   // a window.open target being created: never evicted by the LRU bound
    WorkspaceScope scope(&w);
    if (!pendingDownloads.empty()) return true;
    for (const auto& d : downloads) if (d.state == DL::State::InProgress || d.state == DL::State::Paused) return true;
    for (const auto& job : modalQueue) { auto t = job.tab.lock(); if (t && std::find(pages.begin(), pages.end(), t) != pages.end()) return true; }
    return false;
}
// Tabs of the workspace in the globals: hidden (kept alive) — never closed by a switch.
void HidePages() { for (auto& t : pages) if (t && t->controller.p && t->placedVisible != 0) { t->controller->put_IsVisible(FALSE); t->placedVisible = 0; t->shown = false; } }
// Cold: session saved, controllers closed, engine released; the window keeps its session claim.
void MakeCold(Workspace& w) {
    if (w.cold || &w == activeWorkspace) return;
    popups.CancelOwner(&w);   // forced cold (never through the LRU while pending): its window.open requests settled first
    WorkspaceScope scope(&w);
    SaveSession();
    for (auto& t : pages) if (t) { t->closed = true; t->favicon.Close(); if (t->controller.p) t->controller->Close(); }
    ReleaseEngineWhenExited();
    DropDownloadOps(&w);   // its engine is released: no live operation left (a busy workspace is never made cold)
    pages.clear(); current = -1; environment.set(nullptr); w.cold = true;
    PublishProfileWindow(true);   // still this window's (cached), its session kept
    Log(L"PROFILES workspace cold " + w.id);
}
void EnforceWorkspaceBound() {
    // The accepted policy itself (Profiles::WorkspaceCache::ToCold), not a copy: LRU, never the shown one,
    // never a pinned (busy) one: the bound is exceeded rather than a download cancelled.
    if (!activeWorkspace) return;
    std::vector<Profiles::WorkspaceCache::Entry> entries;
    for (auto* w : LiveWorkspaces()) { Profiles::WorkspaceCache::Entry e; e.id = w->id; e.lastUsed = w->used; e.live = w->started && !w->cold; e.pinned = e.live && w != activeWorkspace && WorkspaceBusy(*w); entries.push_back(e); }
    for (const auto& id : Profiles::WorkspaceCache::ToCold(entries, activeWorkspace->id, LiveWorkspaceBound)) if (Workspace* w = FindWorkspace(id)) MakeCold(*w);
}
// Disposal (its profile is being deleted, or the window closes): session saved and released (kept for
// « Rouvrir »), controllers closed, engine released, sidecar removed; pending COM deferrals settled.
void DisposeWorkspace(Workspace& w, bool keepSession = true) {
    if (w.dead) return;
    if (!w.started) {
        // A cached slot restored from the window set and never shown (review H1): no stores, no engine, no
        // sidecar, only its claim. Released here, so the profile counts no window of ours any more.
        if (w.slotLive || !w.slotSession.empty()) {
            const auto paths = Profiles::ResolvePaths(profileRoot, w.id);
            if (!w.slotSession.empty()) Profiles::UnpublishWindow(paths, w.slotSession, window);
            WindowSessions::Release(paths.dataDir, w.slotSession, keepSession, w.slotLive);
            w.slotSession.clear(); w.slotLive = nullptr;
        }
        w.dead = true; Log(L"PROFILES cached slot released " + w.id); return;
    }
    popups.CancelOwner(&w);   // before its pages and environment go: no deferral left pending, no orphan target
    {
        WorkspaceScope scope(&w);
        SaveSession();
        for (auto& p : pendingDownloads) { if (p->args.p) p->args->put_Cancel(FALSE); if (p->deferral.p) p->deferral->Complete(); }   // the engine decides: never a silent cancel
        pendingDownloads.clear();
        for (auto& t : pages) if (t) { t->closed = true; t->favicon.Close(); if (t->controller.p) t->controller->Close(); }
        ReleaseEngineWhenExited();   // at close the process exits: the lease's dead holder is pruned anyway
        DropDownloadOps(&w);         // its own operations only (its engine closes with it)
        pages.clear(); current = -1; environment.set(nullptr);
        UnpublishProfileWindow();
        if (!windowId.empty()) WindowSessions::Release(dataDir, windowId, keepSession, windowLive);
        windowId.clear();
    }
    w.dead = true; Log(L"PROFILES workspace disposed " + w.id);
}

// The profile's engine lease is released only once its browser process has exited (a new environment on that
// folder with other options would fail with ERROR_INVALID_STATE meanwhile). Called with the workspace in the
// globals, BEFORE its environment is released; without Environment5, at once.
// The environment is KEPT ALIVE by a watch until the event fires (review M1: the event lives on the
// environment object; released at once, it may never be raised and the lease would leak). The handler then
// unregisters itself and drops the environment from a deferred job (never inside its own invocation). If the
// event cannot be registered (no Environment5, failure), the lease is released at once. At process exit the
// watch dies with the process, which AcquireEngineArgs prunes as a dead holder.
struct EngineExitWatch { Com<ICoreWebView2Environment5> env; EventRegistrationToken token{}; bool fired = false; };
std::vector<std::shared_ptr<EngineExitWatch>> engineExitWatches;   // alive until their engine exited
int EngineExitWatchCount() { return static_cast<int>(engineExitWatches.size()); }
void EngineExited(const std::shared_ptr<EngineExitWatch>& watch, const std::wstring& root, const std::wstring& id) {
    if (watch->fired) return; watch->fired = true;
    EngineDropped(root, id, L"exited");
    unownedCallbacks = true;
    Defer([watch] {
        if (watch->env.p) watch->env->remove_BrowserProcessExited(watch->token);
        watch->env.set(nullptr);
        engineExitWatches.erase(std::remove(engineExitWatches.begin(), engineExitWatches.end(), watch), engineExitWatches.end());
    });
    unownedCallbacks = false;
}
void WatchEngineExit(ICoreWebView2Environment* environment, const std::wstring& root, const std::wstring& id) {
    auto watch = std::make_shared<EngineExitWatch>();
    if (environment && SUCCEEDED(environment->QueryInterface(IID_ICoreWebView2Environment5, reinterpret_cast<void**>(&watch->env.p))) && watch->env.p) {
        unownedCallbacks = true;
        std::weak_ptr<EngineExitWatch> weak = watch;   // the list owns the watch; the handler only refers to it
        auto exited = Callback<ICoreWebView2BrowserProcessExitedEventHandler, ICoreWebView2Environment*, ICoreWebView2BrowserProcessExitedEventArgs*>([weak, root, id](ICoreWebView2Environment*, ICoreWebView2BrowserProcessExitedEventArgs*) -> HRESULT {
            if (auto w = weak.lock()) EngineExited(w, root, id); return S_OK; });
        unownedCallbacks = false;
        const bool ok = SUCCEEDED(watch->env->add_BrowserProcessExited(exited, &watch->token)); exited->Release();
        if (ok) { engineExitWatches.push_back(watch); Log(L"PROFILES engine of " + id + L" released, waiting for its exit"); return; }
        watch->env.set(nullptr);
    }
    EngineDropped(root, id, L"released (no exit event)");
}
// Called with the workspace in the globals, BEFORE its environment is released. An environment still being
// created is left to its completion (it watches it, or drops the lease if creation failed).
void ReleaseEngineWhenExited() {
    if (!environment.p && globalsWorkspace && globalsWorkspace->enginePending) return;
    WatchEngineExit(environment.p, profileRoot, profileId);
}
// ---- engine of a workspace ---------------------------------------------------------------------------------
std::wstring EngineArgumentsFor(const BrowserSettings::State& s);
// Its own environment on its own user data folder, with its own engine arguments (never the process-wide
// variable: WebView2 would apply one profile's flags to every environment).
void StartWorkspaceEngine(Workspace& w) {
    WorkspaceScope scope(&w);
    if (environment.p || w.enginePending || !engineCreate) return;   // one creation at a time (switching away and back while it runs)
    auto* target = &w;
    unownedCallbacks = true;   // the completion must run even if the workspace was disposed meanwhile (it owns the lease)
    auto done = Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT, ICoreWebView2Environment*>([target](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
        // Runs inside `target`'s scope (Handler) while it lives: whatever is on screen meanwhile.
        target->enginePending = false;
        if (FAILED(hr) || !env) {   // L3: its lease is released; the profile, not the window, failed
            EngineDropped(profileRoot, target->id, L"failed to start");
            if (!target->dead && !halted) { WorkspaceScope scope(target); const HRESULT why = FAILED(hr) ? hr : E_POINTER; if (!WorkspaceEngineFailed(L"CreateCoreWebView2Environment", why, false) && target == activeWorkspace) Fail(L"CreateCoreWebView2Environment", why); }
            return S_OK; }
        if (halted || target->dead) { WatchEngineExit(env, profileRoot, target->id); return S_OK; }   // disposed meanwhile: unused, released once exited
        WorkspaceScope scope(target);
        environment.set(env); target->cold = false;
        RestoreSession();
        if (target == activeWorkspace) { Layout(); Update(); }
        return S_OK; });
    unownedCallbacks = false;
    auto options = NewEngineOptions();   // its own arguments (leased per user data folder)
    w.enginePending = true;
    // --workspace-test: a creation failure. WebView2 reports a bad folder only at controller creation, so the hook
    // drives the REAL completion handler with a failure (1) or the synchronous failure path (2).
    const int failure = workspaceTest && (qaExpectTerminal || !WorkingFallback(w.id).empty()) ? qaEngineFailure : 0; if (failure) qaEngineFailure = 0;
    HRESULT hr = S_OK;
    if (failure == 1) { done->AddRef(); PostDeferredFailure(done); }
    else if (failure == 2) hr = E_INVALIDARG;
    else hr = engineCreate(nullptr, profileDir.c_str(), options, done);
    options->Release(); done->Release();
    if (FAILED(hr)) w.enginePending = false;
    if (FAILED(hr)) { EngineDropped(profileRoot, w.id, L"not started"); if (!WorkspaceEngineFailed(L"CreateCoreWebView2Environment", hr, false) && &w == activeWorkspace) Fail(L"CreateCoreWebView2Environment", hr); }   // L3: the lease NewEngineOptions took
}

// ---- switching --------------------------------------------------------------------------------------------
void WorkspaceShown();
// This window becomes `id`: the current profile is parked (hidden, alive), the target comes back as it was,
// or starts (its stores, its session, its engine). Same HWND, same process.
bool SwitchWorkspace(const std::wstring& id) {
    if (privateWindow || !activeWorkspace || halted) return false;
    if (activeWorkspace->id == id) return true;
    if (!Profiles::ValidId(id) || !profileRegistry.Find(id)) return false;
    // Called from a parked workspace's scope (a deferred job, a nested loop): the leaving state is the one ON
    // SCREEN, so it comes back first; the enclosing scopes then restore the new active one (M2).
    BringToGlobals(activeWorkspace); ++workspaceSwitches;
    // Leave: the on-screen state is saved and hidden; page-scoped surfaces close.
    CancelTabDrag(); CloseFindBar(false); CloseDownloadSurfaces(false); settingsDrawer.Hide(false, false); historyDrawer.Hide(false, false); bookmarksDrawer.Hide(false, false);
    editing = false; ShowWindow(address, SW_HIDE); hover = pressed = keyboardFocus = 0;
    BeginContentTransition(ContentTransition::Kind::Profile);   // snapshot of the outgoing profile's page, before it is hidden
    SaveSession(); PublishProfileWindow(true); HidePages();   // the profile left on screen stays this window's, cached
    Workspace* target = FindWorkspace(id);
    if (!target) { auto w = std::make_unique<Workspace>(); w->id = id; target = w.get(); workspaces.push_back(std::move(w)); }
    activeWorkspace = target; BringToGlobals(target); target->used = ++workspaceClock;
    if (!target->started) {
        // First time in this window: the profile's own paths, stores and window session.
        const auto paths = Profiles::ResolvePaths(profileRoot, id);
        dataDir = paths.dataDir; profileDir = paths.profileDir; sessionFile = paths.sessionFile; downloadsFile = paths.downloadsFile; settingsFile = paths.settingsFile; profileId = id;
        std::error_code ec; std::filesystem::create_directories(std::filesystem::path(dataDir), ec);
        LoadSettings(); LoadAudioVolumes(); LoadDownloads(); LoadHistory(); LoadBookmarks();
        if (!target->slotSession.empty()) {   // a cached slot of this window set (restart): its claimed session
            windowId = target->slotSession; windowLive = target->slotLive; windowForced = true; sessionFile = WindowSessions::File(dataDir, windowId); target->slotLive = nullptr;
        } else { windowRequested.clear(); claimingInWindow = true; ClaimWindowSession(); claimingInWindow = false; }
        target->started = true;
    }
    if (!environment.p) StartWorkspaceEngine(*target);   // new or cold: its tabs come back when its engine is ready
    Profiles::SetLast(profileRoot, profileRegistry, id);
    WorkspaceShown();
    EnforceWorkspaceBound();
    WriteThisWindowSet();
    Log(L"PROFILES switched this window to " + id);
    RevealContentTransition();   // the switch returned: the outgoing snapshot fades over the live page
    return true;
}
// Everything on screen follows the workspace now in the globals.
void WorkspaceShown() {
    ApplyProfileChrome(false); PublishProfileWindow();
    for (auto& t : pages) if (t) t->placedVisible = -1;   // re-placed by ApplyPages
    chrome.tabMotion.Settle(); motion.selection.Settle();
    SyncBookmarkChrome(); DownloadsChanged(); UpdateDownloadIcon(false); historyDrawer.Changed();
    if (current >= 0 && current < static_cast<int>(pages.size())) { KeepContentTransition keep; Select(current); } else { Layout(); Update(); }   // the profile's own tab: under its switch transition
    InvalidateRect(window, nullptr, FALSE); PumpModal();
    if (!pendingDownloads.empty()) PostMessageW(window, DownloadAskMessage, 0, 0);   // its save dialogs waited while it was hidden
}
// Profiles open in this window (for menus, delete and close).
std::vector<Workspace*> LiveWorkspaces() { std::vector<Workspace*> v; for (auto& w : workspaces) if (!w->dead) v.push_back(w.get()); return v; }

// A profile leaves this window (it is being deleted): on screen, the window first switches to the most recent
// other cached profile, else the last used other profile; then its workspace is disposed (session kept).
bool LeaveProfileHere(const std::wstring& id, bool openOther) {
    Workspace* w = FindWorkspace(id); if (!w) return id != profileId;
    if (w == activeWorkspace) {
        std::vector<Profiles::WorkspaceCache::Entry> entries;   // the accepted policy (WorkspaceCache::Successor)
        for (auto* o : LiveWorkspaces()) { Profiles::WorkspaceCache::Entry e; e.id = o->id; e.lastUsed = o->used; e.live = o->started && !o->cold; entries.push_back(e); }
        std::wstring to = Profiles::WorkspaceCache::Successor(entries, id);
        if (to.empty() && openOther) for (const auto& p : profileRegistry.items) if (p.id != id) { to = p.id; break; }
        if (to.empty()) { closeWithoutPrompt = true; PostMessageW(window, WM_CLOSE, 0, 0); return true; }   // its only profile: the window closes (as before)
        if (!SwitchWorkspace(to)) return false;
    }
    DisposeWorkspace(*w, true); WriteThisWindowSet();
    return true;
}
// The tab preview's Entrée on a tab of a profile cached in this window.
bool OpenSessionTabHere(const std::wstring& id, const std::wstring& session, int tab) {
    Workspace* w = FindWorkspace(id); if (!w || w->s.windowId != session) return false;
    if (!SwitchWorkspace(id)) return false;
    if (tab >= 0 && tab < static_cast<int>(pages.size())) { KeepContentTransition keep; Select(tab); }   // part of this switch: its transition stays
    return true;
}
// Window close: each parked session follows the active one's rule (L2): kept when the close was not asked by
// the user or the workspace never got ready; otherwise WindowSessions::Release removes it only when another
// window of that profile is open (else it stays, for the next start).
void DisposeParkedWorkspaces() {
    for (auto* w : LiveWorkspaces()) if (w != activeWorkspace) DisposeWorkspace(*w, closeWithoutPrompt || !w->started || !w->s.sessionReady);
}
// L1: a parked workspace's title changes publish ITS sidecar (the timer runs in each dirty owner's scope).
void MarkWorkspacePublish() { if (globalsWorkspace) globalsWorkspace->publishDirty = true; }
void PublishDirtyWorkspaces() {
    for (auto* w : LiveWorkspaces()) {
        if (!w->publishDirty || !w->started || w->cold) continue; w->publishDirty = false;
        WorkspaceScope scope(w); PublishProfileWindow(w != activeWorkspace);
    }
}
// Downloads running in a profile cached in this window (closing the window cancels them: asked first).
int ParkedDownloadsRunning() {
    int n = 0;
    for (auto* w : LiveWorkspaces()) { if (w == activeWorkspace) continue; WorkspaceScope scope(w); for (const auto& d : downloads) if (d.state == DL::State::InProgress) ++n; }
    return n;
}

// ---- window set: the profiles of this window come back in this window after a restart ----------------------
std::vector<Profiles::WindowSlot> ThisWindowSlots() {
    std::vector<Profiles::WindowSlot> slots;
    for (auto* w : LiveWorkspaces()) {
        const std::wstring session = w == globalsWorkspace ? windowId : (!w->s.windowId.empty() ? w->s.windowId : w->slotSession);
        if (session.empty()) continue;
        Profiles::WindowSlot slot; slot.profile = w->id; slot.session = session; slot.state = w == activeWorkspace ? Profiles::WorkspaceState::Active : Profiles::WorkspaceState::Cached; slots.push_back(slot);
    }
    return slots;
}
void WriteThisWindowSet() {
    if (!profilesEnabled || smoke || privateWindow || !window) return;
    const auto slots = ThisWindowSlots(); if (slots.size() < 2) { const auto name = Profiles::WindowSetName(window); if (!name.empty()) Profiles::RetireWindowSet(profileRoot, name); return; }
    if (Profiles::WriteWindowSet(profileRoot, window, slots) != Profiles::SetResult::Written) Log(L"PROFILES window set not written");
}
// Start: the set this window's adopted session belonged to brings its cached profiles back, cold (no engine
// until switched to), under this window's token; the old set is retired.
void RestoreWindowSet() {
    if (!profilesEnabled || smoke || privateWindow || windowId.empty()) return;
    Profiles::WindowSet set; if (!Profiles::FindWindowSet(profileRoot, profileId, windowId, set)) return;
    for (const auto& slot : set.slots) {
        if (slot.profile == profileId || slot.state != Profiles::WorkspaceState::Cached || FindWorkspace(slot.profile) || !profileRegistry.Find(slot.profile)) continue;
        HANDLE live = Profiles::ClaimSlot(profileRoot, slot); if (!live) continue;
        auto w = std::make_unique<Workspace>(); w->id = slot.profile; w->slotSession = slot.session; w->slotLive = live; w->cold = true; w->used = 0;
        workspaces.push_back(std::move(w));
        Log(L"PROFILES cached slot restored " + slot.profile);
    }
    WriteThisWindowSet();
    if (set.token != Profiles::WindowSetName(window)) Profiles::RetireWindowSet(profileRoot, set.token);
}
// Another window deletes a profile (evict-profile): this window drops it (switching first if it is on screen).
int EvictProfileHere(const std::wstring& id) {
    Workspace* w = FindWorkspace(id); if (!w) return Profiles::EvictNotApplicable;
    if (w != activeWorkspace && WorkspaceBusy(*w)) return Profiles::EvictBusy;
    Defer([id] { LeaveProfileHere(id, false); WriteThisWindowSet(); });
    return Profiles::EvictAccepted;
}

// ---- engine failure of a profile started in this window --------------------------------------------------------
// The window's sole workspace keeps the global behaviour (Fail: halted, reported). A profile switched INTO this
// window whose engine fails (environment or controller creation) fails alone: logged, its controllers closed, its
// environment released after its exit (lease), its session kept (it comes back from disk on the next try), and, if
// it is on screen, the window returns to the most recent other profile with a non-modal notice. Called in the
// failing workspace's scope; `hasEngine` false when no environment was ever created (nothing to release).
// The failover candidates of this window (Profiles::WorkspaceFailover): what each workspace demonstrably has now.
std::vector<Profiles::WorkspaceFailover::Candidate> FailoverCandidates() {
    std::vector<Profiles::WorkspaceFailover::Candidate> out;
    for (auto& w : workspaces) {
        Profiles::WorkspaceFailover::Candidate c; c.id = w->id; c.lastUsed = w->used; c.dead = w->dead; c.started = w->started; c.cold = w->cold; c.pending = w->enginePending; c.failedAt = w->failedAt;
        const bool inGlobals = w.get() == globalsWorkspace;
        c.engine = inGlobals ? environment.p != nullptr : w->s.environment.p != nullptr;
        for (const auto& t : inGlobals ? pages : w->s.pages) if (t && !t->closed && t->controller.p) { c.controller = true; break; }
        out.push_back(c);
    }
    return out;
}
std::wstring WorkingFallback(const std::wstring& failing) { return Profiles::WorkspaceFailover::Target(FailoverCandidates(), failing, GetTickCount64()); }
// The engine of the workspace in the globals failed (environment creation, or a controller). Called in its scope.
// LOCAL failure only when another profile of this window demonstrably works (WorkingFallback): that profile is
// cleaned up (controllers closed, environment released after its exit, lease, session kept), the window shows the
// working one with a non-modal notice, and choosing the failed profile again retries it. Otherwise false: the
// caller runs the normal recovery (Fail) once; nothing is switched or started (no loop between broken profiles).
// While the window closes: cleanup only (no switch, no notice). `hasEngine` false when no environment exists.
bool WorkspaceEngineFailed(const std::wstring& where, HRESULT hr, bool hasEngine) {
    Workspace* w = globalsWorkspace;
    if (!w || w->dead || halted) return false;
    wchar_t code[32]; swprintf(code, 32, L"0x%08lX", static_cast<unsigned long>(hr));
    auto cleanup = [&] {
        popups.CancelOwner(w);   // its pending window.open requests: settled before its pages and engine go
        if (!hasEngine || !environment.p) return;
        SaveSession();
        for (auto& t : pages) if (t) { t->closed = true; t->favicon.Close(); if (t->controller.p) t->controller->Close(); }
        ReleaseEngineWhenExited();
        pages.clear(); current = -1; environment.set(nullptr);
    };
    if (windowClosing) { Log(L"PROFILES workspace " + w->id + L" " + where + L" " + code + L" while closing: cleanup only"); cleanup(); w->cold = true; w->enginePending = false; return true; }
    if (w->engineFailed) { Log(L"PROFILES workspace " + w->id + L" " + where + L" " + code + L" (already failed)"); return true; }
    w->failedAt = GetTickCount64();
    const std::wstring to = WorkingFallback(w->id);
    if (to.empty()) { Log(L"PROFILES workspace engine failed " + w->id + L": " + where + L" " + code + L"; no working profile in this window: normal recovery"); return false; }
    w->engineFailed = true;
    Log(L"PROFILES workspace engine failed " + w->id + L": " + where + L" " + code + L"; working fallback " + to);
    cleanup();
    w->cold = true; w->enginePending = false;
    const std::wstring id = w->id, where0 = where; const bool shown = w == activeWorkspace;
    unownedCallbacks = true;
    Defer([id, shown, where0, hr] {   // outside the failing callback: show the working profile, then tell the user (non-modal)
        Workspace* f = FindWorkspace(id); if (!f || halted || windowClosing) return;
        f->engineFailed = false;   // choosing it again retries its engine (its failedAt keeps it from being a fallback)
        if (shown && f == activeWorkspace) {
            const std::wstring to = WorkingFallback(id);   // still working now?
            if (to.empty()) { Log(L"PROFILES no working profile left for " + id + L": normal recovery"); Fail(where0, hr); return; }
            SwitchWorkspace(to);
        }
        std::wstring name = id; if (const auto* p = profileRegistry.Find(id)) name = p->name;
        VaultNotice(TRF(workspace_profile_failed, name), TR(workspace_profile_failed_body));
    });
    unownedCallbacks = false;
    return true;
}
bool WorkspaceEngineFailed(const std::wstring& where, HRESULT hr) { return WorkspaceEngineFailed(where, hr, true); }
// --workspace-test injection guard: a failure may be injected only if it is guaranteed to stay local (a working
// fallback exists), or if the test armed `expect-terminal` (Fail is then captured, no dialog).
bool WorkspaceCanFailAlone() { return globalsWorkspace && !globalsWorkspace->dead && !halted && (qaExpectTerminal || !WorkingFallback(globalsWorkspace->id).empty()); }

// ---- --workspace-test: runtime regression hooks (WM_COPYDATA « PROF » « qa ... », ignored otherwise) --------
// They drive the REAL paths (Defer scopes, SwitchWorkspace, MakeCold, engine creation) of a running window and
// log the outcome as « WSQA ... » lines; test-profiles-switch.ps1 reads them.
bool workspaceTest = false, qaControllerFailure = false, qaExpectTerminal = false; int qaEngineFailure = 0;
std::wstring QaWorkspaceState() {
    std::wstring t = L"globals=" + (globalsWorkspace ? globalsWorkspace->id : L"-") + L" active=" + (activeWorkspace ? activeWorkspace->id : L"-") + L" parked=" + (WorkspaceParked() ? L"1" : L"0") + L" watches=" + std::to_wstring(EngineExitWatchCount());
    for (auto& w : workspaces) t += L" [" + w->id + (w->dead ? L" dead" : L"") + (w->started ? L" started" : L" slot") + (w->cold ? L" cold" : L"") + L" holds=" + std::to_wstring(EngineHoldCount(w->id)) + L"]";   // i18n-exempt: QA/log description
    return t;
}
int WorkspaceQaCommand(const std::wstring& text) {
    if (!workspaceTest) return 0;
    std::wistringstream in(text); std::wstring verb, a, b; in >> verb >> a >> b;
    auto later = [](std::function<void()> job) { unownedCallbacks = true; Defer(std::move(job)); unownedCallbacks = false; };
    if (verb == L"state") { Log(L"WSQA state " + QaWorkspaceState()); return 1; }
    if (verb == L"scoped-switch") {   // M2: a job created in PARKED workspace `a`'s scope switches the window to `b`
        Workspace* parked = FindWorkspace(a); if (!parked || parked == activeWorkspace) { Log(L"WSQA scoped-switch refused"); return 2; }
        { WorkspaceScope scope(parked); const std::wstring to = b; Defer([to] { Log(L"WSQA in-job before " + QaWorkspaceState()); SwitchWorkspace(to); }); }
        later([] { Log(L"WSQA after-scoped-switch " + QaWorkspaceState()); });
        return 1;
    }
    if (verb == L"cold") { Workspace* w = FindWorkspace(a); if (!w || w == activeWorkspace) return 2; MakeCold(*w); Log(L"WSQA cold " + QaWorkspaceState()); return 1; }
    if (verb == L"autoplay") { Workspace* w = FindWorkspace(a); if (!w || !w->started) return 2; WorkspaceScope scope(w); settings.autoplay = b == L"allow" ? BrowserSettings::Autoplay::Allowed : BrowserSettings::Autoplay::Muted; Log(L"WSQA autoplay " + a + L" " + b); return 1; }
    if (verb == L"switch") { const bool ok = SwitchWorkspace(a); Log(L"WSQA switch " + a + (ok ? L"" : L" refused")); return ok ? 1 : 2; }   // direct (rapid A/B/A)
    if (verb == L"input") { SendMessageW(window, WM_PARENTNOTIFY, MAKEWPARAM(WM_LBUTTONDOWN, 0), 0); Log(L"WSQA input notified"); return 1; }   // the notification the system sends for a click in a page
    if (verb == L"transition-frames") { qaFramesDir = a; qaFrameIndex = 0; Log(L"WSQA transition frames armed"); return 1; }
    if (verb == L"transition-state") { Log(L"WSQA transition active=" + std::to_wstring(contentTransition.Active()) + L" phase=" + std::to_wstring(static_cast<int>(contentTransition.CurrentPhase())) + L" surfaces=" + std::to_wstring(contentTransition.Surfaces()) + L" captures=" + std::to_wstring(contentTransition.Captures()) + L" retargets=" + std::to_wstring(contentTransition.Retargets())); return 1; }
    if (verb == L"clear-downloads") {   // the real clear-data paths, in the profile on screen: « all » (ClearLocalTraces) or the last hour (selected: downloads only)
        if (a == L"all") ClearLocalTraces(); else { std::vector<bool> c(8, false); c[3] = true; ClearLocalTracesSelected(c, ClearPeriodStartMs(0)); }
        Log(L"WSQA cleared downloads (" + a + L") in " + (globalsWorkspace ? globalsWorkspace->id : L"-")); return 1;
    }
    if (verb == L"ops") {   // live download operations per owner
        std::wstring t = L"WSQA ops";   // i18n-exempt: QA trace
        for (const auto& [id, e] : downloadOps) { std::wstring state = L"?"; for (auto* w : LiveWorkspaces()) if (w == e->owner) { WorkspaceScope scope(w); if (auto* d = FindDownload(id)) state = DL::FileName(d->path) + L":" + std::to_wstring(static_cast<int>(d->state)); } t += L" [" + std::to_wstring(id) + L" " + (e->owner ? e->owner->id : L"-") + L" " + state + L"]"; }
        Log(t); return 1;
    }
    if (verb == L"modal-primary") { const bool ok = modal.PointerForTest(MD::Role::Primary, a == L"hover" || a == L"press", a == L"press"); Log(L"WSQA modal primary " + a + (ok ? L"" : L" (no dialog)")); return ok ? 1 : 2; }   // rest | hover | press
    if (verb == L"expect-terminal") { qaExpectTerminal = true; Log(L"WSQA a terminal failure is expected: Fail is captured (no dialog)"); return 1; }
    if (verb == L"controller-fail") { qaControllerFailure = true; Log(L"WSQA next controller creation fails"); return 1; }
    if (verb == L"bad-engine") { qaEngineFailure = a == L"sync" ? 2 : 1; Log(L"WSQA next workspace engine fails " + (a == L"sync" ? std::wstring(L"synchronously") : std::wstring(L"in its completion"))); return 1; }
    return 2;
}
