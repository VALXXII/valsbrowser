#pragma once
#include "i18n.h"
// Annotation mode — browser integration (V2). Included once by src/main.cpp, after the page menu (it
// uses the host's globals: tabs, chrome, accessibility, settings, Log, Layout…).
// Contract: plan-annotations-mcp-local/design/annotations-v2-brief.md (+ the Paper « Spéc » boards).
//
// Budget « au repos = aucune activité »: until the user does something, nothing here runs — no script,
// listener, thread, timer, folder watch, store read or window. V3: the launcher (40 DIP drawer button, bottom
// right of the page) exists only while the mode is on: it appears 120 ms after the band and goes back into its
// corner at the exit, then its windows are destroyed. The history stays reachable outside the mode by
// Ctrl+Maj+H (anchored 16 DIP from the bottom); its windows are destroyed too once it closes.
//
//   activation   ExecuteScript(page script) in the ACTIVE tab only (src/annotation-script.h); the page
//                moves down 32 DIP under the band. « Ton agent »: nothing opens by itself any more — the
//                band's reminder « Aucun agent connecté · Connecter » shows while no CHAT agent is ready
//                (src/agent-status.h; the chat panel closed) and its click opens « Ton agent »
//                (src/ton-agent-entry.h). « Connecter un agent » (MCP): Réglages › Agents › advanced, on a click.
//   connect      « Connecter avec Claude / Codex » (explicit click only) -> src/agent-connect.h off the UI
//                thread (backup + atomic write; .mcpb when associated, else config merge); « Déconnecter »
//                removes only our entry. BROWSER_ANNOTATIONS_CONNECT_ROOT redirects every write (sandbox).
//   selection    one page message (validated: src/annotation-host.h) -> note panel at once. The page hid
//                its frame at the click and says « hidden » two frames later -> Page.captureScreenshot (CDP,
//                clip = element + margin, <= 1600 px), fallback CapturePreview + crop; decoding/encoding on
//                the thread pool (src/annotation-capture.h). Nothing is written before « Ajouter ».
//   submit       Store::WriteAnnotation on the thread pool at once; a capture not ready yet joins the
//                annotation when it lands (second write). Cancel = the image is dropped.
//   launcher     click / Ctrl+Maj+H -> history (store read when it opens, watched only while it is open);
//                cards 3.1 -> 3.3 follow the annotations of this session while the store is watched.
//   watch        ReadDirectoryChangesW only while the mode, the history or the modal is open, or while an
//                agent is working (the launcher's ring turns anyway).
//   V3-B         entry: the band slides (220 ms), the page receives a 500 ms blue wash (EntryVeil, destroyed at
//                the end, faded from its value if the mode ends first), the launcher follows at 120 ms, the band's
//                wave starts at 220 ms (stopped when the window is minimised, hidden or in the background: the host
//                calls AnnotationWindowState). The page script gets the cursor (SVG + size from the DPI and the
//                Windows pointer size) and draws the spacing inspector; its geometry joins the native frame copy.
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <type_traits>
#include <utility>

#ifdef BROWSER_ANNOTATIONS
#include "annotations.h"
#include "annotation-problem.h"
#include "agent-connect.h"
#include "annotation-ui.h"
#include "annotation-private.h"      // #125: a private window's annotations are erased when it closes
#include "annotation-sites.h"        // #125: « Annoter » on local projects and files only (the one rule)
#include "annotation-onboarding.h"   // #125 v2: the mode's tutorial, in the place of « Connecter un agent » the first time
#include "annotation-local-card.h"   // #125 v2 E1b: « Annoter marche sur vos projets et fichiers locaux » on a live site
RECT RegionScreen(int id);           // src/main.cpp: a chrome control in screen pixels (the card sits under the button)
#include "ton-agent-entry.h"          // « Ton agent »: the band's reminder opens the agents' screen (worker B defines it)
#include "chat-agents-view.h"        // « Ton agent »: the chat agents' one status, as the reminder and Réglages › Agents read it
#include "annotation-script.h"
#include "annotation-host.h"
#include "annotation-capture.h"

namespace Ann {
namespace BA = BrowserAnnotations; namespace AUI = NativeChrome::Annotations; namespace AH = AnnotationHost; namespace AC = AgentConnect;

struct Start { int kind = 0; POINT at{}; std::wstring text; };   // 0 none, 1 point (WebView px), 2 text selection
enum class Cap { None, Pending, Ready, Failed };
struct Session {
    bool active = false, injected = false, frozen = false, saving = false, captureWanted = false;
    std::weak_ptr<Tab> tab; std::uint64_t navStamp = 0; std::wstring source; std::string nonce; double zoom = 1;
    std::uint64_t serial = 0;                 // one per selection: late async results are dropped
    std::int64_t pageSeq = -1;                // the page's selection counter (its « hidden » message)
    double selectedAt = 0, clickLagMs = -1;   // NowMs() at the selection message; page click -> host (ms)
    AH::Selection sel; BA::Annotation pending; std::string origin;
    Cap cap = Cap::None; std::string png; double shotScale = 1; std::string shotCrop;
    std::wstring draft;                       // kept when the panel is cancelled
    std::uint32_t pageAccent = 0x3E67FF;      // the accent the page script paints (0xRRGGBB): the window's SETTLED accent
    bool accentOwed = false;                  // an accent change waits for the capture under way (ShotDone)
    bool paused = false;                      // chat intégré (V1.4): selection switched off, the chat panel stays open
} s;
std::uint64_t serials = 0;                    // never reset: a new session never matches an old result
AUI::Banner banner; AUI::NotePopover note; AUI::Launcher launcher; AUI::ConnectModal connect; AUI::SelectionFrame frame; AUI::EntryVeil veil;
// #125: the onboarding window, made when it opens (a closed one stays until the next, never deleted from its own callback).
std::unique_ptr<NativeChrome::AnnotationOnboardingUI::Window> onboarding;
// #125 (Val, 2026-11-24): what a private window annotates is erased when it closes (src/annotation-private.h): this
// process's ledger (opened at its first note), and the notes still being written (the close waits for them).
AnnotationPrivate::Ledger privateLedger; std::atomic<int> jobsInFlight{0};
bool OnboardingOpen() { return onboarding && onboarding->Visible(); }
NativeChrome::AnnotationLocalCard::Card localCard;   // #125 v2 E1b
bool buttonOff = false;                              // #125 v2 E1a: the toolbar button greyed on a live site (still clickable)
// V3 capture pipeline (src/annotation-frame.h): one shot at a time; it outlives the panel (« Ajouter » / Échap before
// the image is ready) so the page is only told to resume once the engine has its frame.
struct ShotJob { std::uint64_t serial = 0; std::int64_t pageSeq = -1; bool waiting = false, inFlight = false, deferred = false; AH::Selection sel; double selectedAt = 0, lag = -1, handoffAt = 0, frameMs = 0, panelMs = 0, hiddenAt = 0; };
ShotJob shot; bool resumeOwed = false; std::uint64_t frameSerial = 0;
bool uiReady = false, purged = false, countLoaded = false, laterThisSession = false, connectLaunch = false;
std::unique_ptr<BA::Store> store; std::mutex writeLock;
std::optional<BA::Entry> last;                // last annotation added in this session (« Copier en Markdown »)
std::set<std::string> sessionIds, seenWorking, seenDone, seenRead, seenUnhandled;   // this session's annotations, cards already shown
std::vector<BA::Reader> readers;              // V3: last read of readers\ (« Lue par … »), with the store's other reads
int claimed = 0;                              // launcher counter: annotations an agent is handling
bool statusBusy = false, statusDirty = false;
enum class Card { None, Working, NoAgent, Added, Done, Read, Unhandled } card = Card::None; std::string cardId, cardSite;
std::wstring loggedOnce;
HWND post = nullptr; constexpr UINT JobDone = WM_APP + 1, WatchChanged = WM_APP + 2, ShotStart = WM_APP + 3; constexpr UINT_PTR HideTimer = 1, FrameTimer = 2, PanelTimer = 3, CtrlTimer = 4;
// V3: the note panel is built right after the engine is asked for the shot (building it takes 30-55 ms of the UI
// thread: done before, it delayed the page's « hidden » and so the shot by as much). 60 ms at most after the click.
struct PendingPanel { bool on = false; std::uint64_t serial = 0; AUI::NoteRequest req; RECT target{}, page{}; bool keyboard = false; } panel;
struct Watch { HANDLE thread = nullptr, stop = nullptr; std::wstring root; } watch; std::atomic<bool> watchSignal{false};
struct Menu { bool valid = false, selection = false; POINT at{}; std::wstring text; } menu;
// A capture still being prepared when « Ajouter » was clicked: it joins the written annotation.
struct Late { std::uint64_t serial = 0; std::string site, id; bool written = false; std::shared_ptr<AnnotationCapture::Result> shot; };
std::vector<Late> lates;
// Agents (src/agent-connect.h), read off the UI thread.
struct Agents { bool known = false, claudeFolder = false, codexFolder = false; AC::Status claude, codex; std::wstring removable; bool any() const { return claude.connected() || codex.connected(); } } agents;
bool agentsBusy = false; std::vector<std::function<void()>> agentsThen;
bool connectBusy[2] = {false, false}, pendingInstall = false;
// History (launcher): entries of the last read, thumbnails cached by id + updatedAt.
struct Cached { std::int64_t updatedAt = 0; AUI::Shot shot; };
std::map<std::string, Cached> thumbs; std::map<std::string, BA::Entry> historyEntries, trashed;   // trashed: deleted rows still undoable (toast)
bool historyBusy = false, historyDirty = false; std::string historyFocus;   // detail to open once loaded (card « Voir l'annotation »)

bool Reduced() { return motion.ReducedMotion(); }
} // namespace Ann
// Chat intégré (src/chat-host.h, included at the end of this file): what the mode asks it.
namespace Chat {
bool Open();                                             // the panel is open (or opening)
bool Shown();                                            // … or still sliding out
bool TakeSelection(const std::shared_ptr<Tab>& t, std::uint64_t serial, bool captureImpossible);   // V1.3: a local site's selection goes to the chat (opened if needed); false: the note panel
void OnSelection(const std::shared_ptr<Tab>& t, std::uint64_t serial, bool captureImpossible);
void SelectionChanged();                                 // V1.4: the selection was switched on / off (the aim button's state)
void OnAnchor(const AnnotationHost::Envelope& e);        // Mode Commentaire: the bubble's element moved (or is gone)
bool BubbleOpen();                                       // … its bubble is open (review M4: Escape closes it first)
void CloseBubble(bool focusPage);
bool CommentHistory(bool open, bool keyboard);           // Mode Commentaire: the launcher's history lists the conversations whose element is gone
int CommentLauncherCount(bool workingOnly);              // … D3: -1 = not that layout; else how many of them (shown only when > 0)
bool OpenFromHistory(const std::wstring& id);            // … and opens one of them
void SwitchLayout(bool comment);                         // the band's switch (Paper 1AYP-0)
void ScriptReady();                                      // the mode's page script is in the page
bool OnCaptured(std::uint64_t serial, const AnnotationCapture::Result& r);   // true: the image was for the chat
bool CommentFirst(const std::shared_ptr<Tab>& t);        // R18: Mode Commentaire — the bubble opens before the shot starts
bool OpenEarly(const std::shared_ptr<Tab>& t, const AnnotationHost::Envelope& e);   // R18: the click's light « selecting »: the bubble opens now
bool AskAvailable(const std::shared_ptr<Tab>& t);        // « Demander à l'agent » in the note panel (local sites)
void AskFromNote(const std::wstring& draft);
void Toggle(bool keyboard);                              // band button / Ctrl+Maj+D
void ModeEntered();
void ModeExited();
bool KeepsMode(const std::shared_ptr<Tab>& t);           // a navigation of the chat's page keeps the mode (re-armed after)
void PageNavigating(const std::shared_ptr<Tab>& t);
void Layout();
}
namespace Ann {
// ---------------------------------------------------------------- settings (src/annotation-entry.h): keys owned by the settings
// worker, read when they exist (compile-time detection), defaults otherwise.
namespace Prefs {
#define ANN_HAS(Name, field) template<class S, class = void> struct Name : std::false_type {}; \
    template<class S> struct Name<S, std::void_t<decltype(std::declval<S&>().field)>> : std::true_type {};
ANN_HAS(HasPrompt, annotationConnectPrompt) ANN_HAS(HasDeclines, annotationConnectDeclines) ANN_HAS(HasRetention, annotationRetentionDays)
ANN_HAS(HasOnboarded, annotationOnboarded)
#undef ANN_HAS
constexpr int MaxDeclines = 3;
int sessionDeclines = 0;   // without the key: counted for this run only
template<class S> bool Prompt(const S& st) { if constexpr (HasPrompt<S>::value) return bool(st.annotationConnectPrompt); else return (void)st, true; }
template<class S> int Declines(const S& st) { if constexpr (HasDeclines<S>::value) return int(st.annotationConnectDeclines); else return (void)st, sessionDeclines; }
template<class S> void AddDecline(S& st) {
    if constexpr (HasDeclines<S>::value) { if (int(st.annotationConnectDeclines) < MaxDeclines) { st.annotationConnectDeclines = st.annotationConnectDeclines + 1; SaveSettings(); } }
    else { (void)st; if (sessionDeclines < MaxDeclines) ++sessionDeclines; }
}
// #125: the onboarding was finished or skipped once (without the key: for this run only).
bool sessionOnboarded = false;
template<class S> bool Onboarded(const S& st) { if constexpr (HasOnboarded<S>::value) return bool(st.annotationOnboarded); else return (void)st, sessionOnboarded; }
template<class S> void SetOnboarded(S& st) {
    if constexpr (HasOnboarded<S>::value) { if (!st.annotationOnboarded) { st.annotationOnboarded = true; SaveSettings(); } }
    else { (void)st; sessionOnboarded = true; }
}
template<class S> std::int64_t Retention(const S& st) {
    if constexpr (HasRetention<S>::value) { const int d = int(st.annotationRetentionDays); return d == 7 || d == 30 || d == 90 ? std::int64_t(d) * 86400 : 0; }
    else return (void)st, 0;
}
}
double NowMs() { static LARGE_INTEGER f{}; if (!f.QuadPart) QueryPerformanceFrequency(&f); LARGE_INTEGER c; QueryPerformanceCounter(&c); return double(c.QuadPart) * 1000.0 / double(f.QuadPart); }
std::wstring Ms(double v) { return std::to_wstring(int(std::lround(v))); }
long long UtcMs() { FILETIME ft; GetSystemTimePreciseAsFileTime(&ft); ULARGE_INTEGER u{}; u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime; return (long long)(u.QuadPart / 10000ULL) - 11644473600000LL; }
void Announce(const std::wstring& text) { accessibility.Notify('N', text.c_str(), L"Annotation" /* i18n-exempt: UIA activity id */); Log(L"ANNOTATE " + text); }
void LogOnce(const std::wstring& what) { if (loggedOnce.find(what) != std::wstring::npos) return; loggedOnce += what + L";"; Log(L"ANNOTATE ignored page message: " + what); }
// Annotations are GLOBAL (one store for every profile, src/annotations.h): the application's data root
// (profiles-integration.h `profileRoot`: the folder holding profiles.txt and the default profile's layout),
// never `dataDir`, which follows the profile shown in this window. Before profiles, both were the same folder.
std::wstring DataRoot() { return profileRoot.empty() ? dataDir : profileRoot; }
std::wstring PrivateDir() { return DataRoot() + L"\\annotations-private"; }   // the private windows' ledgers (beside the store, never in it)
// Created on the UI thread before any job uses it (every caller of Run touches it first).
BA::Store& Store() { if (!store) store = std::make_unique<BA::Store>(BA::GlobalStoreRoot(DataRoot())); return *store; }
// Older builds kept one store per profile (<data root>\profiles\<id>\annotations): merged once into the global
// store, without loss, by the first job of the session, before it touches the store (RunJob).
std::once_flag migrateOnce; BA::MergeReport migration; std::atomic<bool> migrationToLog{false};
void MigrateOnce() {
    std::call_once(migrateOnce, [] { migration = BA::MigrateProfileStores(DataRoot()); if (migration.stores) migrationToLog = true; });
}
void LogMigration() {
    if (!migrationToLog.exchange(false)) return;
    const auto& m = migration;
    // i18n-exempt-begin: log lines (English diagnostics)
    Log(L"ANNOTATE per-profile stores merged into " + Store().Root() + L": " + std::to_wstring(m.stores) + L" store(s), " + std::to_wstring(m.moved) + L" moved, " +
        std::to_wstring(m.identical) + L" identical, " + std::to_wstring(m.kept) + L" kept in place (conflict or link), " + std::to_wstring(m.failed) + L" failed" +
        (m.removed ? L"; old folders removed" : L"; old folders kept (see kept / failed)"));   // i18n-exempt-end
}
// Identity per channel (public download plan B, item 5): a release install and a dev / preview copy coexist in the same
// Claude / Codex configuration under two names; the release payload carries its bridge in assets\bin, dev / preview
// builds next to browser.exe (an installed browser always uses <root>\bin, see AgentConnect::LocateBridge).
#ifdef BROWSER_CHANNEL_RELEASE
constexpr const char* ServerNameText = AC::DefaultServerName;   // "browser-annotations"
constexpr AC::BridgeLayout BridgeLayoutOfBuild = AC::BridgeLayout::AssetsBin;
#else
constexpr const char* ServerNameText = "browser-annotations-preview";
constexpr AC::BridgeLayout BridgeLayoutOfBuild = AC::BridgeLayout::Sibling;
#endif
// source.instance.channel of an annotation: the build's channel ("release" / "dev"); a design preview build
// (BROWSER_PREVIEW) and an unstamped build keep the historical "preview".
#if defined(BROWSER_PREVIEW) || !defined(BROWSER_CHANNEL_TEXT)
constexpr const char* SourceChannel = "preview";
#else
constexpr const char* SourceChannel = BROWSER_CHANNEL_TEXT;
#endif
std::string ServerName() { return ServerNameText; }
std::wstring BrowserExe() { std::wstring exe(32768, L'\0'); const DWORD n = GetModuleFileNameW(nullptr, exe.data(), DWORD(exe.size())); exe.resize(n < exe.size() ? n : 0); return exe; }
AC::Target ConnectTarget() {
    AC::Target t; t.bridgeExe = AC::LocateBridge(BrowserExe(), BridgeLayoutOfBuild).path; t.storeRoot = Store().Root(); t.serverName = ServerName();
#ifdef BROWSER_VERSION_TEXT
    t.version = BROWSER_VERSION_TEXT;
#endif
    return t;
}
std::wstring EnvVar(const wchar_t* name) {
    wchar_t b[2048]; const DWORD n = GetEnvironmentVariableW(name, b, 2048);
    return n > 0 && n < 2048 ? std::wstring(b, n) : std::wstring();
}
// The real environment, or — BROWSER_ANNOTATIONS_CONNECT_ROOT set (sandbox, tests) — a test folder that receives
// every write: <root>\AppData\Roaming (Claude), <root>\home\.codex (Codex); no .mcpb opening, no codex CLI
// unless BROWSER_ANNOTATIONS_CONNECT_PATH names a folder for it.
AC::Environment ConnectEnv() {
    auto env = AC::Environment::FromProcess();
    const std::wstring root = EnvVar(L"BROWSER_ANNOTATIONS_CONNECT_ROOT");
    if (!root.empty()) {
        env.appData = root + L"\\AppData\\Roaming"; env.localAppData = root + L"\\AppData\\Local";
        env.userProfile = root + L"\\home"; env.codexHome = root + L"\\home\\.codex";
        env.path = EnvVar(L"BROWSER_ANNOTATIONS_CONNECT_PATH");
        env.mcpbAssociated = [] { return false; }; env.openFile = [](const std::wstring&) { return false; };
        env.testMode = true;   // + the guard: even a wrong redirection can never reach the real folders
    }
    return env;
}
bool ConnectRedirected() { return !EnvVar(L"BROWSER_ANNOTATIONS_CONNECT_ROOT").empty(); }

// ---------------------------------------------------------------- thread pool -> UI thread
struct Job { std::function<void()> work, done; };
// SEC-26: finished jobs wait here; JobDone carries no pointer (another process could post a forged one).
std::mutex jobsMutex; std::vector<std::unique_ptr<Job>> jobsDone;
LRESULT CALLBACK PostProc(HWND h, UINT m, WPARAM w, LPARAM l);
bool EnsurePost() {
    if (post) return true;
    WNDCLASSEXW wc{sizeof(wc)}; wc.lpfnWndProc = PostProc; wc.hInstance = instance; wc.lpszClassName = L"BrowserAnnotationPost";
    RegisterClassExW(&wc);
    post = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, nullptr);
    return post != nullptr;
}
VOID CALLBACK RunJob(PTP_CALLBACK_INSTANCE, PVOID p) {
    auto* j = static_cast<Job*>(p);
    try { MigrateOnce(); } catch (...) {}   // every job waits for the one-time merge of the old per-profile stores
    try { if (j->work) j->work(); } catch (...) {}
    --jobsInFlight;
    if (!post) { delete j; return; }
    { std::lock_guard<std::mutex> lock(jobsMutex); jobsDone.emplace_back(j); }
    PostMessageW(post, JobDone, 0, 0);
}
void Run(std::function<void()> work, std::function<void()> done) {
    Store();
    auto* j = new Job{std::move(work), std::move(done)};
    ++jobsInFlight;
    if (!EnsurePost() || !TrySubmitThreadpoolCallback(RunJob, j, nullptr)) { --jobsInFlight; Log(L"ANNOTATE thread pool unavailable"); delete j; }
}

// ---------------------------------------------------------------- availability, chrome, launcher
std::shared_ptr<Tab> ModeTab() { return s.active ? s.tab.lock() : nullptr; }
// #125: the mode starts only where src/annotation-sites.h allows it — local projects (localhost, 127.x, [::1],
// *.localhost) and local files — in a normal window as in a private one (AnnotationSites::OnlineInPrivateWindow). Anywhere
// else the toolbar button keeps its unavailable state, with Paper E1a's tooltip as a whole (not « Annoter — {reason} »):
// « Mode Annotation - seulement sur vos projets et fichiers locaux ».
bool SiteAllowed(const std::wstring& url) { return AnnotationSites::Allowed(AH::Utf8(url), privateWindow); }
bool Available(const std::shared_ptr<Tab>& t, std::wstring* why) {
    auto no = [&](const wchar_t* w) { if (why) *why = w; return false; };
    if (halted || !t || t->closed || !t->view.p || !t->controller.p) return no(TR(annotate_no_page));
    if (t->home) return no(TR(annotate_open_web_page));
    if (!t->https.shown.empty() || !t->errorPage.shown.empty() || t->sandbox || IsSandboxUri(t->uri)) return no(TR(annotate_no_internal));
    if (!SiteAllowed(t->uri)) return no(TR(annotate_local_only));
    if (InFullscreen() || PageFullscreenActive()) return no(TR(annotate_no_fullscreen));
    return true;
}
bool Selecting() { return s.active && !s.paused; }   // the mode as the user sees it: band, toolbar button, « viseur »
void SyncChrome() {
    chrome.annotateShown = true; chrome.annotateActive = Selecting();
    std::wstring why; const bool ok = Available(Active(), &why);
    buttonOff = !ok && !s.active && why == TR(annotate_local_only);   // #125 v2: a live site — greyed, its click opens the E1b card
    chrome.SetEnabled('N', ok || s.active || buttonOff); chrome.annotateOff = buttonOff;   // E1a also for screen readers (AnnotateHelp)
    chrome.annotateTip = Selecting() ? std::wstring(TR(annotate_tip_quit)) : ok ? std::wstring(TR(chrome_tip_annotate)) : why == TR(annotate_local_only) ? why : TRF(annotate_tip_unavailable, why);   // #125 E1a: the live-site tooltip is a whole sentence
    if (!chrome.paintAnnotate) chrome.paintAnnotate = [](ID2D1RenderTarget* t, ID2D1Factory* f, ID2D1SolidColorBrush* b, D2D1_RECT_F box, NativeChrome::Motion::InteractionVisual iv, bool focus) {
        AUI::ToolbarVisual v; v.active = Selecting(); v.hover = chrome.Enabled('N') ? iv.hover : 0; v.press = iv.press; v.focus = focus; v.keyboard = keyboardModality; v.reduced = Reduced(); v.off = buttonOff;
        AUI::PaintToolbarButton(t, f, b, box, v);
    };
}
RECT BandRect() { RECT c{}; GetClientRect(window, &c); return {0, S(chrome.ContentTop() - chrome.annotationBand), c.right, S(chrome.ContentTop())}; }
RECT PageClient(const std::shared_ptr<Tab>& t) {
    if (t && !IsRectEmpty(&t->placed)) return t->placed;
    RECT c{}; GetClientRect(window, &c); c.top = S(chrome.ContentTop()); return c;
}
RECT ClientScreen() { RECT r{}; GetClientRect(window, &r); MapWindowPoints(window, nullptr, reinterpret_cast<POINT*>(&r), 2); return r; }
RECT PageScreen(const std::shared_ptr<Tab>& t) { RECT r = PageClient(t); MapWindowPoints(window, nullptr, reinterpret_cast<POINT*>(&r), 2); return r; }
void EnsureUi();
void RefreshCount();
// V3 (retours §6): only in the mode, on the mode's tab, never in full screen.
bool LauncherWanted() {
    auto t = Active();
    return s.active && ModeTab() == t && !halted && t && !t->closed && !InFullscreen() && !PageFullscreenActive() && !IsIconic(window) && !Chat::Open()
        && Chat::CommentLauncherCount(false) != 0;   // D3: in Mode Commentaire, only when a conversation's element is gone
}
void ShowLauncher(int delayMs = 0) {
    const int gone = Chat::CommentLauncherCount(true);   // D3: in Mode Commentaire it signals only the gone elements' conversations
    const int n = gone >= 0 ? gone : claimed;
    launcher.Show(n > 0 ? AUI::LauncherState::Working : AUI::LauncherState::Rest, n, Reduced(), delayMs);
}
// entering: the mode just started (the launcher follows the band by 120 ms). Leaving: animated (into its corner).
void LauncherSync(bool entering = false) {
    if (!uiReady) return;
    launcher.Place(PageClient(Active()));
    if (!LauncherWanted()) {
        if (launcher.Visible()) launcher.Hide(!InFullscreen() && !PageFullscreenActive() && !IsIconic(window));
        launcher.SetDormant(!s.active);
        return;
    }
    if (!launcher.Visible()) { ShowLauncher(entering && !Reduced() ? 120 : 0); if (!countLoaded) RefreshCount(); }
}
void Place() {
    if (s.active) banner.Move(BandRect());
    if (veil.Running()) veil.Move(PageClient(Active()));
    LauncherSync();
    Chat::Layout();
}

// ---------------------------------------------------------------- page script
void Cmd(const std::shared_ptr<Tab>& t, const std::wstring& op, const std::wstring& arg = L"null") {
    if (!t || !t->view.p || t->closed || halted) return;
    auto cb = Callback<ICoreWebView2ExecuteScriptCompletedHandler, HRESULT, LPCWSTR>([](HRESULT, LPCWSTR) -> HRESULT { return S_OK; });
    t->view->ExecuteScript(AUI::PageCommand(AH::Wide(s.nonce.empty() ? std::string("-") : s.nonce), op, arg).c_str(), cb); cb->Release();
}
// Final pass 1, R31: Ctrl held = the site as usual. The page sees Ctrl only when its own WebView has the keyboard (its
// pointer moves carry no Ctrl flag otherwise: keyboard in the band, the bubble, the panel). The host reads the state of
// the Ctrl key (GetAsyncKeyState: no hook, nothing else read) at each pointer move over the page (the page asks:
// « mods ») and every ~30 ms, only while the mode is on and this window is in front, and tells the page the changes.
// The page's zoom factor, read again when a page message needs it (it changes with Ctrl+= / Ctrl+- while the mode is on).
void RefreshZoom(const std::shared_ptr<Tab>& t) {
    if (!t || !t->controller.p) return;
    double z = 0; if (FAILED(t->controller->get_ZoomFactor(&z)) || !(z > 0)) return;
    if (std::fabs(z - s.zoom) > 1e-6) { Log(L"ANNOTATE zoom " + std::to_wstring(int(std::lround(s.zoom * 100))) + L" % -> " + std::to_wstring(int(std::lround(z * 100))) + L" %"); s.zoom = z; }
}
bool ctrlHeld = false;
bool OursInFront() { DWORD pid = 0; const HWND fg = GetForegroundWindow(); if (fg) GetWindowThreadProcessId(fg, &pid); return fg && pid == GetCurrentProcessId(); }
void CtrlPoll() {
    const bool down = s.active && OursInFront() && (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    if (down == ctrlHeld) return;
    ctrlHeld = down;
    if (s.active && s.injected) Cmd(ModeTab(), L"ctrl", down ? L"true" : L"false");
}
// The ~30 ms reading runs only while the mode is on and the window is in front (WM_ACTIVATEAPP / show: AnnotationWindowState).
// PERF-20: ~30 ms only while the pointer is over the page (where Ctrl changes what a click does); elsewhere every 200 ms,
// enough to pass on a release (a pointer move over the page reads Ctrl at once anyway: « mods »).
UINT ctrlPeriod = 0;
bool PointerOverPage() { POINT p{}; if (!GetCursorPos(&p)) return true; const RECT r = PageScreen(ModeTab()); return PtInRect(&r, p) != FALSE; }
void CtrlArm(UINT ms) { if (post && ms != ctrlPeriod) { SetTimer(post, CtrlTimer, ms, nullptr); ctrlPeriod = ms; } }
void CtrlWatch() {
    const bool on = s.active && OursInFront();
    if (post) { if (on) CtrlArm(PointerOverPage() ? 30 : 200); else { KillTimer(post, CtrlTimer); ctrlPeriod = 0; } }
    if (!on) CtrlPoll();   // released (or left behind): the page gets its selection back
}
// The accent the page script paints (its frame fill, edges, label, padding hatches, cursor dot): the native frame and
// the capture's fill removal use the same value. It is the window's SETTLED accent (Design::Accent::Settled: the profile
// colour stored, never a step of the 120 ms fade) given at the injection and replaced by SendAccent.
std::uint32_t AccentRgb() { return s.pageAccent; }
std::uint32_t SettledAccentRgb() { return AUI::Rgb24(NativeChrome::Design::Accent::Settled()); }
std::string AccentInts(std::uint32_t c) { return std::to_string((c >> 16) & 255) + "," + std::to_string((c >> 8) & 255) + "," + std::to_string(c & 255); }
// V3-B cursor size: 32 / 48 / 64 from the window's DPI and the Windows pointer size (CursorBaseSize, 32 by default).
unsigned PointerBaseSize() {
    DWORD v = 0, n = sizeof v, type = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Control Panel\\Cursors", L"CursorBaseSize", RRF_RT_REG_DWORD, &type, &v, &n) != ERROR_SUCCESS) return 32;
    return unsigned(v);
}
int CursorSize() { return AUI::Dither::Cursor::SizeFor(GetDpiForWindow(window), PointerBaseSize()); }
std::wstring Params(const std::shared_ptr<Tab>& t, const Start& st) {
    namespace SJ = StrictJson; SJ::Value p = SJ::Value::Obj();
    p.Set("nonce", SJ::Value::Str(s.nonce)); p.Set("navStamp", SJ::Value::Int(std::int64_t(t->navStamp)));
    SJ::Value styles = SJ::Value::Arr(); for (const auto& n : BA::AllowedStyleProperties()) styles.Push(SJ::Value::Str(n));
    SJ::Value attrs = SJ::Value::Arr(); for (const auto& n : BA::AllowedAttributes()) attrs.Push(SJ::Value::Str(n));
    p.Set("styles", std::move(styles)); p.Set("attrs", std::move(attrs)); p.Set("reduced", SJ::Value::Bool(Reduced()));
    p.Set("accent", SJ::Value::Str(AccentInts(s.pageAccent)));
    SJ::Value start = SJ::Value::Obj();
    start.Set("kind", SJ::Value::Str(st.kind == 1 ? "point" : st.kind == 2 ? "selection" : "none"));
    start.Set("x", SJ::Value::Int(st.at.x)); start.Set("y", SJ::Value::Int(st.at.y));
    start.Set("text", SJ::Value::Str(BA::CleanText(AH::Utf8(st.text), BA::MaxTextChars, true)));
    p.Set("start", std::move(start));
    SJ::Value cursor = SJ::Value::Obj();
    cursor.Set("svg", SJ::Value::Str(AUI::Dither::Cursor::Svg(s.pageAccent))); cursor.Set("size", SJ::Value::Int(CursorSize()));
    cursor.Set("hx", SJ::Value::Int(AUI::Dither::Cursor::hotX)); cursor.Set("hy", SJ::Value::Int(AUI::Dither::Cursor::hotY));
    cursor.Set("scale", SJ::Value::Num(GetDpiForWindow(window) / 96.0));   // the screen's scale (never the page zoom)
    p.Set("cursor", std::move(cursor));
    return AH::Wide(SJ::Serialize(p));
}
// The window's accent changed (src/profiles-integration.h: SetAccents, then each step of the 120 ms fade; once under
// reduced motion). Native surfaces repaint with the accent as presented (they follow the fade on its own timer, no
// clock here). The page of the mode gets the SETTLED accent, once per change: never a step of the fade (it would stay
// there), never while a capture is under way (the page's fill, the native frame and the fill removal must agree on
// one colour: it is sent at ShotDone), never to a page no longer on screen (a profile switch leaves it: the mode ends
// with it, and nothing of the other profile is sent to that page).
void SendAccent() {
    if (!s.active) return;
    const std::uint32_t target = SettledAccentRgb();
    if (target == s.pageAccent) { s.accentOwed = false; return; }
    auto t = ModeTab(); if (!t || t != Active()) return;
    if (shot.waiting || shot.inFlight) { s.accentOwed = true; return; }
    namespace SJ = StrictJson; SJ::Value a = SJ::Value::Obj();
    a.Set("accent", SJ::Value::Str(AccentInts(target))); a.Set("svg", SJ::Value::Str(AUI::Dither::Cursor::Svg(target)));
    s.pageAccent = target; s.accentOwed = false;
    Cmd(t, L"accent", AH::Wide(SJ::Serialize(a)));
    Log(L"ANNOTATE accent " + AH::Wide(AccentInts(target)) + L" sent to the page");
}
void AccentChanged() {
    if (!uiReady) return;
    banner.AccentChanged(); note.AccentChanged(); launcher.AccentChanged(); connect.AccentChanged();
    SendAccent();
}
void SendStart(const std::shared_ptr<Tab>& t, const Start& st) {
    if (st.kind == 1) Cmd(t, L"point", L"{\"x\":" + std::to_wstring(st.at.x) + L",\"y\":" + std::to_wstring(st.at.y) + L"}");
    else if (st.kind == 2) Cmd(t, L"selection", AH::Wide(StrictJson::Serialize(StrictJson::Value::Str(BA::CleanText(AH::Utf8(st.text), BA::MaxTextChars, true)))));
}

// ---------------------------------------------------------------- store watch
DWORD WINAPI WatchMain(LPVOID p) {
    auto* w = static_cast<Watch*>(p);
    CreateDirectoryW(w->root.c_str(), nullptr);   // the store root (its parent is the data folder)
    HANDLE dir = CreateFileW(w->root.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    if (dir == INVALID_HANDLE_VALUE) return 1;
    OVERLAPPED ov{}; ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    alignas(8) static thread_local BYTE buffer[16 * 1024];
    for (;;) {
        ResetEvent(ov.hEvent);
        if (!ReadDirectoryChangesW(dir, buffer, sizeof buffer, TRUE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE, nullptr, &ov, nullptr)) break;
        HANDLE waits[2] = {ov.hEvent, w->stop};
        const DWORD r = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        if (r != WAIT_OBJECT_0) { CancelIoEx(dir, &ov); DWORD n = 0; GetOverlappedResult(dir, &ov, &n, TRUE); break; }
        DWORD n = 0; GetOverlappedResult(dir, &ov, &n, FALSE);
        if (WaitForSingleObject(w->stop, 120) == WAIT_OBJECT_0) break;   // coalesce a burst of writes
        if (!watchSignal.exchange(true)) PostMessageW(post, WatchChanged, 0, 0);
    }
    CloseHandle(ov.hEvent); CloseHandle(dir); return 0;
}
// V3: no launcher outside the mode, so no watch for its ring either.
bool WatchWanted() { return uiReady && (s.active || launcher.HistoryVisible() || connect.Visible() || OnboardingOpen() || pendingInstall); }
void SyncWatch() {
    if (WatchWanted()) {
        if (watch.thread || !EnsurePost()) return;
        watch.root = Store().Root(); watch.stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        watch.thread = CreateThread(nullptr, 0, WatchMain, &watch, 0, nullptr);
        if (!watch.thread) { CloseHandle(watch.stop); watch.stop = nullptr; return; }
        Log(L"ANNOTATE watching the store");
    } else if (watch.thread) {
        SetEvent(watch.stop); WaitForSingleObject(watch.thread, 2000);
        CloseHandle(watch.thread); CloseHandle(watch.stop); watch.thread = watch.stop = nullptr;
        Log(L"ANNOTATE store watch stopped");
    }
}

// ---------------------------------------------------------------- time texts (local)
struct Local { int year = 0, month = 0, day = 0, weekday = 0, hour = 0, minute = 0; std::int64_t dayIndex = 0; };
Local LocalOf(std::int64_t unix) {
    Local l; ULARGE_INTEGER u; u.QuadPart = ULONGLONG(unix + 11644473600LL) * 10000000ULL;
    FILETIME ft{u.LowPart, u.HighPart}; SYSTEMTIME utc{}, loc{};
    if (!FileTimeToSystemTime(&ft, &utc) || !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &loc)) return l;
    l.year = loc.wYear; l.month = loc.wMonth; l.day = loc.wDay; l.weekday = loc.wDayOfWeek; l.hour = loc.wHour; l.minute = loc.wMinute;
    FILETIME lf{}; SystemTimeToFileTime(&loc, &lf); ULARGE_INTEGER x{}; x.LowPart = lf.dwLowDateTime; x.HighPart = lf.dwHighDateTime;
    l.dayIndex = std::int64_t(x.QuadPart / (86400ULL * 10000000ULL));
    return l;
}
std::wstring ClockText(std::int64_t unix) { const auto l = LocalOf(unix); wchar_t b[16]; swprintf(b, 16, L"%02d:%02d", l.hour, l.minute); return b; }
std::wstring Age(std::int64_t unix, std::int64_t now) {
    const auto a = LocalOf(unix), n = LocalOf(now);
    return AH::AgeLabel(std::max<std::int64_t>(0, now - unix), int(n.dayIndex - a.dayIndex), a.weekday, a.day, a.month);
}
std::wstring When(std::int64_t unix, std::int64_t now) {
    const auto a = LocalOf(unix), n = LocalOf(now);
    if (a.dayIndex == n.dayIndex) return TRF(when_today, ClockText(unix));
    return TRF(when_date, AH::AgeLabel(86400 * 30, 30, 0, a.day, a.month), ClockText(unix));
}

// ---------------------------------------------------------------- agents (connection)
// V3 (retours §1): a client only loads its MCP servers when it starts. Configured but not read since (never, or
// not since a connection made in this session) => « Redémarrez Claude pour l'activer », in the modal and the settings.
std::int64_t connectedAt[2] = {0, 0};
bool RestartNeeded(int app, const AC::Status& st) {
    return st.configured && !st.stalePath && (st.lastReadAt == 0 || (connectedAt[app] && st.lastReadAt < connectedAt[app]));
}
std::wstring RestartLine(int app) { return TRF(agent_restart_to_enable, app == 0 ? L"Claude" : L"Codex"); }
AUI::AppStatus StatusFor(int app, const AC::Status& st) {
    if (st.connected()) {
        if (st.stalePath || (st.removableDrive && st.configured)) return {AUI::ConnectState::Reconnect, L""};
        return {AUI::ConnectState::Connected, RestartNeeded(app, st) ? RestartLine(app) : L""};
    }
    return {AUI::ConnectState::Idle, L""};
}
// « Ton agent » (src/agent-status.h): the band's reminder is the CHAT's — no chat agent ready (once both are detected) and
// the chat panel closed (AnnotationSettings::BandReminder); its click opens « Ton agent ». The « Connecter un agent »
// window (MCP, external apps) never opens by itself: only from Réglages › Agents › « Connecter un agent externe » (and the
// tutorial's « Autre app ou réglage manuel »), always on a click.
AnnotationSettings::ChatAgents ChatAgentsNow() { return AnnotationSettings::FromStatuses(AgentStatus::Get(AgentStatus::Agent::Claude), AgentStatus::Get(AgentStatus::Agent::Codex)); }
bool ReminderWanted() { return AnnotationSettings::BandReminder(ChatAgentsNow(), Chat::Open()); }
void SyncReminder() { if (s.active && banner.Visible() && !connect.Visible() && !OnboardingOpen()) banner.SetReminder(ReminderWanted()); }
void ApplyAgents() {
    SyncReminder();   // (the tutorial's A6 / A7 follow the agents' one status by themselves: no MCP rows to update)
    if (connect.Visible()) {
        if (!connectBusy[0] && !(pendingInstall && !agents.claude.connected())) connect.SetStatus(AUI::AgentKind::Claude, StatusFor(0, agents.claude));
        if (!connectBusy[1]) connect.SetStatus(AUI::AgentKind::Codex, StatusFor(1, agents.codex));
        connect.SetRemovable(agents.removable);
    }
    if (pendingInstall && agents.claude.connected()) { pendingInstall = false; Log(L"ANNOTATE Claude extension installed or read the store"); SyncWatch(); }
}
void RefreshAgents(std::function<void()> then = nullptr) {
    if (then) agentsThen.push_back(std::move(then));
    if (agentsBusy) return;
    agentsBusy = true;
    auto out = std::make_shared<Agents>(); const auto target = ConnectTarget();
    Run([out, target] {
            const auto env = ConnectEnv(); const auto readers = Store().ListReaders();
            out->claude = AC::ClientStatus(env, AC::Client::Claude, target, readers);
            out->codex = AC::ClientStatus(env, AC::Client::Codex, target, readers);
            if (AC::IsOnRemovableDrive(env, target.bridgeExe) || AC::IsOnRemovableDrive(env, target.storeRoot)) out->removable = target.bridgeExe.substr(0, 2);
            const auto d = AC::Detect(env); out->claudeFolder = d.claude; out->codexFolder = d.codex;
            out->known = true;
        },
        [out] {
            agentsBusy = false; agents = *out; ApplyAgents();
            auto then = std::move(agentsThen); agentsThen.clear();
            for (auto& f : then) if (f) f();
        });
}
void OpenConnect(bool launch, bool manual = false) {
    EnsureUi();
    if (connect.Visible()) return;
    AUI::ConnectSetup setup; const auto target = ConnectTarget();
    setup.launch = launch; setup.manual = manual; setup.command = target.bridgeExe; setup.args = {L"--store", target.storeRoot}; setup.serverName = AH::Wide(target.serverName);
    setup.removableDrive = agents.removable; setup.claude = StatusFor(0, agents.claude); setup.codex = StatusFor(1, agents.codex); setup.disconnectable = true;
    if (pendingInstall && !agents.claude.connected()) setup.claude = {AUI::ConnectState::Working, TR(connect_click_install_claude)};
    if (GetFileAttributesW(setup.command.c_str()) == INVALID_FILE_ATTRIBUTES) Log(L"ANNOTATE bridge not found: " + setup.command);
    connectLaunch = launch;
    if (connect.Show(setup, ClientScreen(), Reduced(), launch ? 80 : 0)) { if (s.active) banner.SetReminder(false, true); SyncWatch(); RefreshAgents(); Log(std::wstring(L"ANNOTATE connect modal opened (") + (launch ? L"launch" : L"on demand") + (ConnectRedirected() ? L", writes redirected to BROWSER_ANNOTATIONS_CONNECT_ROOT)" : L")")); }
}
AUI::AppStatus ConnectOutcome(AUI::AgentKind a, const AC::Report& r) {
    const std::wstring msg = AH::Wide(r.message);
    switch (r.result) {
    case AC::Result::Connected: return {AUI::ConnectState::Connected, RestartLine(a == AUI::AgentKind::Codex ? 1 : 0)};
    case AC::Result::AlreadyConnected: { const auto& st = a == AUI::AgentKind::Codex ? agents.codex : agents.claude; const int i = a == AUI::AgentKind::Codex ? 1 : 0;
        return {AUI::ConnectState::Connected, RestartNeeded(i, st) ? RestartLine(i) : L""}; }
    case AC::Result::PendingInstall: return {AUI::ConnectState::Working, TR(connect_click_install_claude)};
    default: return {AUI::ConnectState::Failed, msg.empty() ? std::wstring(TR(connect_impossible_cap)) : msg};
    }
}
void ConnectApp(AUI::AgentKind a, bool on, std::function<void(const AC::Report&)> then = nullptr) {
    const int i = a == AUI::AgentKind::Claude ? 0 : 1;
    if (a == AUI::AgentKind::Other || connectBusy[i]) { if (then) { AC::Report r; r.result = AC::Result::Failed; r.message = I18n::U8(I18n::Id::connect_already_running); then(r); } return; }
    connectBusy[i] = true; Store();
    if (on && connect.Visible()) connect.SetStatus(a, {AUI::ConnectState::Working, TRF(connect_connecting_to, a == AUI::AgentKind::Claude ? L"Claude" : L"Codex")});
    auto report = std::make_shared<AC::Report>(); const auto target = ConnectTarget(); const std::wstring work = DataRoot() + L"\\annotations-connect";
    Run([a, on, report, target, work] {
            const auto env = ConnectEnv();
            if (on) { CreateDirectoryW(work.c_str(), nullptr); *report = a == AUI::AgentKind::Claude ? AC::ConnectClaude(env, target, work) : AC::ConnectCodex(env, target); }
            else *report = a == AUI::AgentKind::Claude ? AC::DisconnectClaude(env, target) : AC::DisconnectCodex(env, target);
        },
        [a, on, i, report, then] {
            connectBusy[i] = false;
            // i18n-exempt-begin: log line (English diagnostics)
            Log(std::wstring(L"ANNOTATE ") + (on ? L"connect " : L"disconnect ") + (a == AUI::AgentKind::Claude ? L"claude: " : L"codex: ") + AH::Wide(AC::ResultName(report->result)) +
                L" — " + AH::Wide(report->message) + (report->file.empty() ? L"" : L" [" + report->file + L"]") + (report->backup.empty() ? L"" : L" backup " + report->backup) + (report->usedCli ? L" (codex CLI)" : L""));
            // i18n-exempt-end
            if (on) {
                if (report->result == AC::Result::Connected) connectedAt[i] = BA::SystemNow();
                const auto st = ConnectOutcome(a, *report);
                if (a == AUI::AgentKind::Claude) pendingInstall = report->result == AC::Result::PendingInstall;
                if (report->ok() && card == Card::NoAgent && launcher.CardVisible()) { launcher.HideCard(); card = Card::None; }   // « Aucun agent ne la lit encore » is no longer true
                if (connect.Visible()) connect.SetStatus(a, st);
            } else if (connect.Visible()) {
                if (report->result == AC::Result::Disconnected || report->result == AC::Result::NotConnected) connect.SetStatus(a, {AUI::ConnectState::Idle, L""});
                else connect.SetStatus(a, {AUI::ConnectState::Connected, AH::Wide(report->message)});
                Announce(report->ok() ? TRF(settings_announce_disconnected, AUI::AppLong(a)) : AH::Wide(report->message));
            }
            SyncWatch(); RefreshAgents();
            if (then) then(*report);
        });
}

// V3 (retours §5, Paper 5.5): who read an annotation (the bridge records the ids an agent listed or opened), and
// whether an agent gave it back with a reason (release reason: the last claimed -> open event of an agent).
struct ReadBy { std::string label; std::int64_t at = 0; };
ReadBy ReadOf(const std::string& id) {
    ReadBy r; for (const auto& rd : readers) if (const auto at = rd.SeenAt(id); at > 0 && (!r.at || at < r.at)) r = {rd.label.empty() ? rd.client : rd.label, at};
    return r;
}
const BA::Event* GivenBack(const BA::Entry& e) {
    if (e.state.status != BA::Status::Open) return nullptr;
    for (auto it = e.state.events.rbegin(); it != e.state.events.rend(); ++it) {
        if (it->to != "open") continue;
        if (it->from == "claimed" && it->actor != "user" && !it->summary.empty() && it->summary != "libérée") return &*it;   // i18n-exempt: stored event summary written by the store (data)
        return nullptr;   // the last return to « open » was something else (plain release, reopened by the user)
    }
    return nullptr;
}
std::wstring StepTime(std::int64_t at, std::int64_t now) {
    const auto a = LocalOf(at), n = LocalOf(now);
    return a.dayIndex == n.dayIndex ? ClockText(at) : AH::AgeLabel(86400 * 30, 30, 0, a.day, a.month) + L" " + ClockText(at);
}
std::vector<AUI::HistoryEntry::Step> StepsOf(const BA::Entry& e, std::int64_t now) {
    using Step = AUI::HistoryEntry::Step; std::vector<std::pair<std::int64_t, Step>> out;
    auto add = [&](std::int64_t at, Step::Kind kind, const std::string& who, std::wstring label, std::wstring quote = L"") {
        Step s; s.kind = kind; s.agent = AUI::AgentKind(AH::AgentKindOf(who)); s.label = std::move(label); s.time = StepTime(at, now); s.quote = std::move(quote); out.emplace_back(at, s); };
    const auto& a = e.annotation;
    add(a.createdAt, Step::Added, "", a.source.profileName.empty() ? std::wstring(TR(step_added)) : TRF(step_added_profile, AH::Wide(a.source.profileName)));
    if (const auto r = ReadOf(a.id); r.at) add(r.at, Step::Read, r.label, TRF(annotation_agent_read, AH::AgentDisplay(r.label)));
    for (const auto& ev : e.state.events) {
        const std::wstring who = AH::AgentDisplay(ev.actor), quote = AH::Wide(ev.summary);
        if (ev.to == "claimed") add(ev.at, Step::Claimed, ev.actor, TRF(step_claimed_by, who));
        else if (ev.from == "claimed" && ev.to == "open") {
            if (ev.actor == "user") add(ev.at, Step::Released, ev.actor, TR(step_released_by_you));
            else if (ev.summary.empty() || ev.summary == "libérée" /* i18n-exempt: stored event summary (data) */) add(ev.at, Step::Released, ev.actor, TRF(step_given_back_by, who));
            else add(ev.at, Step::Unhandled, ev.actor, TRF(annotation_agent_unhandled, who), quote);
        }
        else if (ev.to == "resolved") add(ev.at, Step::Done, ev.actor, TRF(history_done_by, who), e.state.resolution && e.state.status == BA::Status::Resolved ? AH::Wide(e.state.resolution->summary) : quote);
        else if (ev.to == "needs_info") add(ev.at, Step::Question, ev.actor, TRF(step_question_from, who), e.state.resolution && e.state.status == BA::Status::NeedsInfo ? AH::Wide(e.state.resolution->summary) : quote);
        else if (ev.to == "dismissed") add(ev.at, Step::Dismissed, ev.actor, TRF(step_dismissed_by, who), quote);
        else if (ev.to == "open" && ev.actor == "user" && ev.from != "draft") add(ev.at, Step::Reopened, ev.actor, TR(step_reopened));
    }
    std::stable_sort(out.begin(), out.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
    std::vector<Step> steps; for (auto& o : out) steps.push_back(std::move(o.second)); return steps;
}

// ---------------------------------------------------------------- status: counter, live cards
void ShowCardFor(const BA::Entry& e, Card kind) {
    AUI::CardContent c; const auto now = BA::SystemNow();
    c.id = AH::Wide(e.annotation.id); c.note = AH::Wide(e.annotation.note);
    if (kind == Card::Working) {
        c.kind = AUI::CardKind::Working; const std::string who = e.state.claimedBy ? e.state.claimedBy->agent : "";
        c.agent = AH::AgentDisplay(who); c.agentKind = AUI::AgentKind(AH::AgentKindOf(who)); c.since = AH::SinceLower(now - AH::ClaimTime(e));
        seenWorking.insert(e.annotation.id);
    } else if (kind == Card::Read) {
        c.kind = AUI::CardKind::Read; const auto r = ReadOf(e.annotation.id);
        c.agent = AH::AgentDisplay(r.label); c.agentKind = AUI::AgentKind(AH::AgentKindOf(r.label));
        seenRead.insert(e.annotation.id);
    } else if (kind == Card::Unhandled) {
        c.kind = AUI::CardKind::Unhandled; const auto* back = GivenBack(e); const std::string who = back ? back->actor : "";
        c.agent = AH::AgentDisplay(who); c.agentKind = AUI::AgentKind(AH::AgentKindOf(who)); c.summary = back ? AH::Wide(back->summary) : L"";
        seenUnhandled.insert(e.annotation.id);
    } else {
        c.kind = AUI::CardKind::Done; const std::string who = e.state.resolution ? e.state.resolution->by : "";
        c.agent = AH::AgentDisplay(who); c.agentKind = AUI::AgentKind(AH::AgentKindOf(who));
        c.summary = e.state.resolution ? AH::Wide(e.state.resolution->summary) : L"";
        if (e.state.status == BA::Status::NeedsInfo && !c.summary.empty()) c.summary = L"Question : " + c.summary;
        seenDone.insert(e.annotation.id);
    }
    card = kind; cardId = e.annotation.id; cardSite = e.annotation.siteKey;
    launcher.ShowCard(c);
}
void ApplyStatus(const std::vector<BA::Entry>& entries) {
    const int was = claimed;
    claimed = int(std::count_if(entries.begin(), entries.end(), [](const BA::Entry& e) { return !e.draft && e.state.status == BA::Status::Claimed; }));
    if (claimed != was && launcher.Visible()) ShowLauncher();
    // Cards follow this session's annotations: claimed -> 3.2 (once), done -> 3.3 (once).
    if (!note.Visible() && !launcher.HistoryVisible() && launcher.Visible()) {
        const BA::Entry* done = nullptr; const BA::Entry* working = nullptr; const BA::Entry* back = nullptr; const BA::Entry* read = nullptr;
        for (const auto& e : entries) {
            if (!sessionIds.count(e.annotation.id)) continue;
            if (BA::IsDone(e.state.status) && e.state.resolution && !seenDone.count(e.annotation.id)) { if (!done || e.state.resolution->at > done->state.resolution->at) done = &e; }
            else if (e.state.status == BA::Status::Claimed && !seenWorking.count(e.annotation.id)) { if (!working || AH::ClaimTime(e) > AH::ClaimTime(*working)) working = &e; }
            else if (GivenBack(e) && !seenUnhandled.count(e.annotation.id)) back = &e;
            else if (e.state.status == BA::Status::Open && !GivenBack(e) && ReadOf(e.annotation.id).at && !seenRead.count(e.annotation.id) && !seenWorking.count(e.annotation.id)) read = &e;
        }
        if (done) ShowCardFor(*done, Card::Done);
        else if (working) ShowCardFor(*working, Card::Working);
        else if (back) ShowCardFor(*back, Card::Unhandled);
        else if (read && card != Card::Working) ShowCardFor(*read, Card::Read);
    }
    SyncWatch();
}
void RefreshStatus() {
    if (statusBusy) { statusDirty = true; return; }
    statusBusy = true;
    auto out = std::make_shared<std::vector<BA::Entry>>(); auto rd = std::make_shared<std::vector<BA::Reader>>();
    Run([out, rd] { BA::ListQuery q; q.newestFirst = true; q.limit = 500; *out = Store().List(q).entries; *rd = Store().ListReaders(); },
        [out, rd] { statusBusy = false; readers = std::move(*rd); ApplyStatus(*out); if (statusDirty) { statusDirty = false; RefreshStatus(); } });
}
// The counter at the launcher's first display: one count, only when the store already exists.
void RefreshCount() {
    countLoaded = true;
    const DWORD at = GetFileAttributesW(BA::GlobalStoreRoot(DataRoot()).c_str());
    if (at == INVALID_FILE_ATTRIBUTES || !(at & FILE_ATTRIBUTE_DIRECTORY)) return;
    auto n = std::make_shared<std::size_t>(0);
    Run([n] { *n = Store().CountClaimed(); }, [n] { if (int(*n) != claimed) { claimed = int(*n); if (launcher.Visible()) ShowLauncher(); SyncWatch(); } });
}

// ---------------------------------------------------------------- history (launcher)
AUI::Shot ToShot(const AnnotationCapture::Image& img) { AUI::Shot s; s.width = img.width; s.height = img.height; s.pixels = img.pixels; return s; }
AUI::HistoryEntry HistoryRow(const BA::Entry& e, std::int64_t now) {
    AUI::HistoryEntry h; const auto& a = e.annotation;
    h.id = AH::Wide(a.id); h.site = AH::Wide(BA::SiteLabel(a.site)); h.note = AH::Wide(a.note);
    std::wstring page = AH::Wide(a.source.url); if (const auto at = page.find(L"://"); at != std::wstring::npos) page = page.substr(at + 3);
    h.page = page;
    h.status = BA::IsDone(e.state.status) ? AUI::EntryStatus::Done : e.state.status == BA::Status::Claimed ? AUI::EntryStatus::Working : AUI::EntryStatus::Pending;
    const std::string who = e.state.resolution && BA::IsDone(e.state.status) ? e.state.resolution->by : e.state.claimedBy ? e.state.claimedBy->agent : "";
    h.agent = AUI::AgentKind(AH::AgentKindOf(who)); h.agentName = who.empty() ? L"" : AH::AgentDisplay(who);
    h.age = Age(a.createdAt, now);
    std::wstring f = TRF(follow_added, When(a.createdAt, now));
    if (!a.source.profileName.empty()) f += L" · " + TRF(follow_profile, AH::Wide(a.source.profileName));
    for (const auto& ev : e.state.events) if (ev.to == "claimed") { f += L" · " + TRF(follow_claimed, When(ev.at, now)); break; }
    if (e.state.resolution && BA::IsDone(e.state.status)) f += L" · " + (e.state.status == BA::Status::NeedsInfo ? TRF(follow_question, When(e.state.resolution->at, now)) : e.state.status == BA::Status::Dismissed ? TRF(follow_dismissed, When(e.state.resolution->at, now)) : TRF(follow_done, When(e.state.resolution->at, now)));
    h.followUp = f;
    if (e.state.resolution) h.summary = AH::Wide(e.state.resolution->summary);
    if (const auto* back = GivenBack(e)) { h.unhandled = true; h.reason = AH::Wide(back->summary); h.agent = AUI::AgentKind(AH::AgentKindOf(back->actor)); h.agentName = AH::AgentDisplay(back->actor); }
    else if (e.state.status == BA::Status::Open) if (const auto r = ReadOf(a.id); r.at) { h.readBy = AH::AgentDisplay(r.label); h.readAge = AH::AgoText(std::max<std::int64_t>(0, now - r.at)); }
    h.steps = StepsOf(e, now);
    if (auto it = thumbs.find(a.id); it != thumbs.end() && it->second.updatedAt == a.updatedAt) h.thumb = it->second.shot;
    return h;
}
std::wstring CurrentSite() { auto t = Active(); return t ? AH::Wide(BA::SiteLabel(AH::OriginOf(t->uri))) : L""; }
AUI::HistoryData HistoryFrom(const std::vector<BA::Entry>& entries) {
    AUI::HistoryData d; d.currentSite = CurrentSite(); const auto now = BA::SystemNow();
    for (const auto& e : entries) d.entries.push_back(HistoryRow(e, now));
    return d;
}
void LoadHistory(bool open, bool keyboard) {
    if (Chat::CommentHistory(open, keyboard)) return;   // Mode Commentaire: the conversations whose element is gone, only
    if (historyBusy) { historyDirty = true; return; }
    historyBusy = true;
    struct Out { std::vector<BA::Entry> entries; std::map<std::string, Cached> thumbs; std::vector<BA::Reader> readers; };
    auto out = std::make_shared<Out>(); const bool purge = !purged; purged = true; const std::int64_t retention = Prefs::Retention(settings);
    std::map<std::string, std::int64_t> known; for (const auto& t : thumbs) known[t.first] = t.second.updatedAt;
    const unsigned tw = unsigned(S(44)), th = unsigned(S(32));
    Run([out, purge, retention, known, tw, th] {
            if (purge) { std::lock_guard<std::mutex> g(writeLock); Store().Purge(retention); }
            BA::ListQuery q; q.newestFirst = true; q.limit = 300; out->entries = Store().List(q).entries; out->readers = Store().ListReaders();
            int decoded = 0;
            for (const auto& e : out->entries) {
                if (!e.annotation.screenshot.present || decoded >= 80) continue;
                if (auto k = known.find(e.annotation.id); k != known.end() && k->second == e.annotation.updatedAt) continue;
                std::string png; AnnotationCapture::Image img;
                if (Store().ReadShot(e.annotation.siteKey, e.annotation.id, png).ok() && AnnotationCapture::Thumbnail(png, tw, th, img)) { out->thumbs[e.annotation.id] = {e.annotation.updatedAt, ToShot(img)}; ++decoded; }
            }
        },
        [out, open, keyboard] {
            historyBusy = false; readers = std::move(out->readers);
            for (auto& t : out->thumbs) thumbs[t.first] = std::move(t.second);
            historyEntries.clear(); for (const auto& e : out->entries) historyEntries[e.annotation.id] = e;
            ApplyStatus(out->entries);
            const auto data = HistoryFrom(out->entries);
            if (open) {
                if (launcher.ShowHistory(data, keyboard)) Log(L"ANNOTATE history opened (" + std::to_wstring(data.entries.size()) + L" annotations)");
            } else if (launcher.HistoryVisible()) launcher.UpdateHistory(data);
            if (!historyFocus.empty() && launcher.HistoryVisible()) { launcher.ShowHistoryDetail(AH::Wide(historyFocus)); historyFocus.clear(); }
            SyncWatch();
            if (historyDirty) { historyDirty = false; if (launcher.HistoryVisible()) LoadHistory(false, false); }
        });
}
void OpenHistory(bool keyboard, const std::string& focusId = "") {
    EnsureUi(); launcher.Place(PageClient(Active()));   // V3: outside the mode, the panel alone (no launcher)
    historyFocus = focusId;
    if (launcher.HistoryVisible()) { if (!focusId.empty()) { launcher.ShowHistoryDetail(AH::Wide(focusId)); historyFocus.clear(); } return; }
    LoadHistory(true, keyboard);
}
const BA::Entry* FindHistory(const std::wstring& id) {
    const std::string k = AH::Utf8(id);
    if (auto it = historyEntries.find(k); it != historyEntries.end()) return &it->second;
    auto t = trashed.find(k); return t == trashed.end() ? nullptr : &t->second;
}
void HistoryDetail(const std::wstring& id) {
    const auto* e = FindHistory(id); if (!e || !e->annotation.screenshot.present) return;
    const std::string site = e->annotation.siteKey, ann = e->annotation.id; const unsigned w = unsigned(S(334)), h = unsigned(S(150));
    auto img = std::make_shared<AnnotationCapture::Image>(); auto ok = std::make_shared<bool>(false);
    Run([site, ann, w, h, img, ok] { std::string png; *ok = Store().ReadShot(site, ann, png).ok() && AnnotationCapture::Thumbnail(png, w, h, *img, true); },   // V3: never cropped
        [id, img, ok] { if (*ok && launcher.HistoryVisible()) launcher.SetHistoryCapture(id, ToShot(*img)); });
}
// Trash / Restore / DiscardTrash on the thread pool; an agent holding a file of the entry => Busy: retried.
void HistoryStoreOp(const std::wstring& id, int op) {
    const auto* e = FindHistory(id); if (!e) { Log(L"ANNOTATE history: unknown entry " + id); return; }
    const std::string site = e->annotation.siteKey, ann = e->annotation.id;
    if (op == 0) trashed[ann] = *e;
    auto result = std::make_shared<BA::Outcome>();
    Run([site, ann, op, result] {
            std::lock_guard<std::mutex> g(writeLock);
            for (int attempt = 0; attempt < 4; ++attempt) {
                *result = op == 0 ? Store().Trash(site, ann) : op == 1 ? Store().Restore(site, ann) : Store().DiscardTrash(site, ann);
                if (result->code != BA::Code::Busy) break;
                Sleep(250);
            }
        },
        [op, ann, result] {
            static const wchar_t* names[] = {L"trash", L"restore", L"discard"};
            Log(std::wstring(L"ANNOTATE history ") + names[op] + L" " + AH::Wide(ann) + L": " + AH::Wide(BA::CodeName(result->code)) + (result->message.empty() ? L"" : L" — " + AH::Wide(result->message)));
            if (!result->ok() && !(op == 2 && result->code == BA::Code::NotFound)) {
                Announce(op == 0 ? TRF(annotation_delete_failed, AnnotationProblem::Text(*result)) : op == 1 ? TRF(annotation_restore_failed, AnnotationProblem::Text(*result)) : std::wstring());   // the store's French message is logged, never shown
                if (launcher.HistoryVisible()) LoadHistory(false, false);
            }
            if (op == 0 && result->ok() && card != Card::None && cardId == ann) { launcher.HideCard(); card = Card::None; }
            if (op != 0) trashed.erase(ann);
        });
}

// ---------------------------------------------------------------- selection, capture, write
void Exit(const wchar_t* why, bool pageChanged = false);
void Resume(bool focusPage = true) {   // focusPage false: Mode Commentaire, the keyboard goes to the bubble (no 13-21 ms MoveFocus)
    auto t = ModeTab(); if (!t) return;
    s.frozen = false; s.cap = Cap::None; s.png.clear(); s.saving = s.captureWanted = false; s.pageSeq = -1; ++serials; s.serial = serials;
    // A shot still owed to the page's frame (« Ajouter » or Échap within ~60 ms of the click): the page resumes
    // once the engine has rendered it, never before (resuming would show the page's own edges in the shot).
    if (shot.waiting || shot.inFlight) resumeOwed = true; else Cmd(t, L"resume");
    if (focusPage && t->controller.p) t->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
}
void Attach(const Late& l) {
    auto shot = l.shot; const std::string site = l.site, id = l.id; auto result = std::make_shared<BA::Outcome>();
    Run([shot, site, id, result] {
            std::lock_guard<std::mutex> g(writeLock); BA::Entry e;
            *result = Store().Read(site, id, e);
            if (!result->ok()) return;
            BA::Annotation a = e.annotation; a.screenshot.scale = shot->scale; a.screenshot.crop = shot->crop;
            *result = Store().WriteAnnotation(a, &shot->png, false);
        },
        [id, result] {
            Log(L"ANNOTATE capture joined " + AH::Wide(id) + L": " + AH::Wide(BA::CodeName(result->code)));
            if (result->ok()) { if (last && last->annotation.id == id) last->annotation.screenshot.present = true; if (launcher.HistoryVisible()) LoadHistory(false, false); }
        });
}
void OnCaptured(std::uint64_t serial, const AnnotationCapture::Result& r, double selectedAt, double lag, const std::wstring& timing = L"") {
    const double ms = NowMs() - selectedAt;
    if (r.ok) Log(L"ANNOTATE capture " + std::to_wstring(r.width) + L"x" + std::to_wstring(r.height) + L" " + std::to_wstring(r.png.size()) + L" bytes (" + AH::Wide(r.path) +
                  // i18n-exempt-next: log continuation
                  L") ready " + Ms(ms) + L" ms after the selection message" + (lag >= 0 ? L" (+" + Ms(lag) + L" ms click->host)" : L"") + (timing.empty() ? L"" : L" [" + timing + L"]") + L" utc-ms=" + std::to_wstring(UtcMs()));
    else Log(L"ANNOTATE capture unavailable (" + AH::Wide(r.path) + L"): " + AH::Wide(r.why));
    if (Chat::OnCaptured(serial, r)) return;   // an element aimed for the chat: its image joins the chip
    if (auto it = std::find_if(lates.begin(), lates.end(), [&](const Late& l) { return l.serial == serial; }); it != lates.end()) {
        if (!r.ok) { lates.erase(it); return; }
        it->shot = std::make_shared<AnnotationCapture::Result>(r);
        if (it->written) { Attach(*it); lates.erase(it); }
        return;
    }
    if (!s.active || s.serial != serial) return;   // cancelled / another selection: the image is dropped
    if (!r.ok) { s.cap = Cap::Failed; note.SetShot(AUI::CaptureState::Unavailable, nullptr); return; }
    s.png = r.png; s.shotScale = r.scale; s.shotCrop = r.crop; s.cap = Cap::Ready;
    AUI::Shot shot; shot.width = r.thumbW; shot.height = r.thumbH; shot.pixels = r.thumb; shot.mark = {r.markL, r.markT, r.markR, r.markB};
    note.SetShot(AUI::CaptureState::Ready, shot.width ? &shot : nullptr);
}
void CaptureFailed(std::uint64_t serial, const char* why) { AnnotationCapture::Result r; r.path = "plan"; r.why = why; OnCaptured(serial, r, shot.serial == serial ? shot.selectedAt : s.selectedAt, -1); }
// The engine has rendered the shot (or gave up): the page's edges come back, and it answers « shown » two frames
// later (then the native copy goes: OnShown). A resume the user asked for meanwhile is sent now.
void ShotDone(const std::shared_ptr<Tab>& t, std::uint64_t serial) {
    if (shot.serial == serial) { shot.waiting = shot.inFlight = false; }
    if (!t) { frame.Hide(); return; }
    if (frame.Visible() && frameSerial == serial) {
        Cmd(t, L"show", std::to_wstring(serial));
        if (EnsurePost()) SetTimer(post, FrameTimer, 1000, nullptr);   // a page that never answers « shown »
    } else Cmd(t, L"show");
    if (resumeOwed && !shot.waiting && !shot.inFlight) { resumeOwed = false; if (s.active && !s.frozen) Cmd(t, L"resume"); }
    if (s.accentOwed && !shot.waiting && !shot.inFlight) SendAccent();   // a colour change during the capture
}
void OnShown(std::int64_t serial) {
    if (!frame.Visible() || std::uint64_t(serial) != frameSerial) return;
    if (EnsurePost()) SetTimer(post, FrameTimer, 34, nullptr);   // two page frames + one compositor frame of margin
}
// Fallback: CapturePreview of the viewport, cropped on the thread pool.
void Preview(const std::shared_ptr<Tab>& t, std::uint64_t serial, AH::Selection sel, double selectedAt, double lag) {
    IStream* stream = nullptr;
    if (!t || !t->view.p || FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream))) { ShotDone(t, serial); CaptureFailed(serial, "stream"); return; }
    const auto rect = sel.target.rect; const double vw = sel.target.viewport.width, vh = sel.target.viewport.height;
    const unsigned tw = unsigned(S(AUI::NoteSpec::thumbW)), th = unsigned(S(AUI::NoteSpec::thumbH));
    std::weak_ptr<Tab> weak = t;
    auto cb = Callback<ICoreWebView2CapturePreviewCompletedHandler, HRESULT>([weak, stream, serial, rect, vw, vh, tw, th, selectedAt, lag](HRESULT hr) -> HRESULT {
        std::string bytes;
        if (SUCCEEDED(hr)) { HGLOBAL mem = nullptr; STATSTG st{}; if (SUCCEEDED(GetHGlobalFromStream(stream, &mem)) && SUCCEEDED(stream->Stat(&st, STATFLAG_NONAME))) { if (const void* d = GlobalLock(mem)) { bytes.assign(static_cast<const char*>(d), size_t(st.cbSize.QuadPart)); GlobalUnlock(mem); } } }
        stream->Release();
        ShotDone(weak.lock(), serial);   // the frame comes back right after the shot
        if (bytes.empty()) { AnnotationCapture::Result r; r.path = "preview"; r.why = "CapturePreview"; OnCaptured(serial, r, selectedAt, lag); return S_OK; }
        auto result = std::make_shared<AnnotationCapture::Result>(); auto input = std::make_shared<std::string>(std::move(bytes));
        Run([result, input, rect, vw, vh, tw, th] { *result = AnnotationCapture::Process(*input, rect, vw, vh, tw, th); },
            [result, serial, selectedAt, lag] { OnCaptured(serial, *result, selectedAt, lag); });
        return S_OK; });
    const HRESULT hr = t->view->CapturePreview(COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG, stream, cb); cb->Release();
    if (FAILED(hr)) { stream->Release(); ShotDone(t, serial); CaptureFailed(serial, "CapturePreview refused"); }
}
// The engine's shot of the whole viewport (Page.captureScreenshot WITHOUT a clip: a clipped shot made the engine
// re-render the view on screen for the clip, the page's text vanished for ~4 frames at every click — V3 retours §3,
// measured), cropped / cleaned / encoded on the thread pool (AnnotationCapture::ProcessViewport); any failure => Preview.
void Capture(const std::shared_ptr<Tab>& t, std::uint64_t serial) {
    if (!t || !t->view.p) { ShotDone(t, serial); CaptureFailed(serial, "no page"); return; }
    shot.waiting = false; shot.inFlight = true;
    const AH::Selection sel = shot.sel; const double selectedAt = shot.selectedAt, lag = shot.lag, engineAt = NowMs(), handoff = shot.handoffAt, frameMs = shot.frameMs;
    const auto plan = AH::PlanClip(sel.target.rect, sel.target.viewport);
    if (!plan.ok) { ShotDone(t, serial); CaptureFailed(serial, "outside the viewport"); return; }
    const std::uint32_t accent = AccentRgb(); const double viewW = sel.target.viewport.width;
    const unsigned tw = unsigned(S(AUI::NoteSpec::thumbW)), th = unsigned(S(AUI::NoteSpec::thumbH));
    std::weak_ptr<Tab> weak = t;
    auto cb = Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler, HRESULT, LPCWSTR>([weak, serial, sel, plan, viewW, tw, th, selectedAt, lag, engineAt, accent, handoff, frameMs](HRESULT hr, LPCWSTR json) -> HRESULT {
        auto tab = weak.lock(); const double engineDone = NowMs();
        if (FAILED(hr) || !json || !*json) { Log(L"ANNOTATE Page.captureScreenshot failed: falling back to CapturePreview"); if (tab) Preview(tab, serial, sel, selectedAt, lag); else { ShotDone(tab, serial); CaptureFailed(serial, "tab closed"); } return S_OK; }
        ShotDone(tab, serial);
        auto wide = std::make_shared<std::wstring>(json); auto result = std::make_shared<AnnotationCapture::Result>(); auto work = std::make_shared<double>(0);
        auto mark = std::make_shared<AH::Mark>(sel.mark);
        static const std::wstring rawDir = EnvVar(L"BROWSER_ANNOTATIONS_QA_RAW_DIR");
        Run([wide, result, plan, viewW, tw, th, work, mark, accent, serial] {
                const double w0 = NowMs(); struct Done { double w0; std::shared_ptr<double> out; ~Done() { *out = NowMs() - w0; } } done{w0, work};
                std::string png;
                if (!AH::ScreenshotData(AH::Utf8(*wide), png)) { result->path = "cdp"; result->why = "no image in the reply"; return; }
                wide->clear(); wide->shrink_to_fit();
                if (!rawDir.empty()) {   // QA only (BROWSER_ANNOTATIONS_QA_RAW_DIR): the engine's image before crop and fill removal
                    HANDLE f = CreateFileW((rawDir + L"\raw-" + std::to_wstring(serial) + L".png").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                    if (f != INVALID_HANDLE_VALUE) { DWORD n = 0; WriteFile(f, png.data(), DWORD(png.size()), &n, nullptr); CloseHandle(f); }
                }
                *result = AnnotationCapture::ProcessViewport(png, plan, viewW, tw, th, mark.get(), accent);
            },
            [result, serial, selectedAt, lag, engineAt, engineDone, work, handoff, frameMs, mark] {
                const double panelMs = shot.serial == serial ? shot.panelMs : -1;
                // i18n-exempt-begin: timing log text
                OnCaptured(serial, *result, selectedAt, lag, L"viewport crop " + std::to_wstring(result->cropX) + L"," + std::to_wstring(result->cropY) + L" " + std::to_wstring(result->cropW) + L"x" + std::to_wstring(result->cropH) +
                                                             L"; native frame " + Ms(frameMs) + L" ms, page handoff " + Ms(engineAt - handoff) + L" ms, engine asked after " + Ms(engineAt - selectedAt) +
                                                             L" ms, engine " + Ms(engineDone - engineAt) + L" ms, decode/crop/fill/encode/thumbnail " + Ms(*work) + L" ms" + (mark->present ? L" (fill removed)" : L"") + L", panel built in " + Ms(panelMs) + L" ms while the engine worked" +
            // i18n-exempt-end
                                                             L" => image in memory " + Ms(engineDone + *work - selectedAt) + L" ms after the selection; UI hand-off " + Ms(NowMs() - engineDone - *work) + L" ms");   // i18n-exempt: timing log text
            });
        return S_OK; });
    const HRESULT hr = t->view->CallDevToolsProtocolMethod(L"Page.captureScreenshot", AH::Wide(AH::ViewportShotParams()).c_str(), cb); cb->Release();
    if (FAILED(hr)) { Log(L"ANNOTATE Page.captureScreenshot refused: falling back to CapturePreview"); Preview(t, serial, sel, selectedAt, lag); }
}
void ShowPanel() {
    if (!panel.on) return; panel.on = false; if (post) KillTimer(post, PanelTimer);
    if (!s.active || s.serial != panel.serial) return;
    if (s.cap == Cap::Failed) panel.req.capture = AUI::CaptureState::Unavailable;
    const double p0 = NowMs();
    if (!note.Show(panel.req, panel.target, panel.page, panel.keyboard, Reduced())) { Log(L"ANNOTATE note panel unavailable"); return; }
    shot.panelMs = NowMs() - p0;
    if (launcher.CardVisible()) { launcher.HideCard(); card = Card::None; }
}
void OnHidden(const std::shared_ptr<Tab>& t, std::int64_t serial) {
    if (!shot.waiting || serial != shot.pageSeq) return;
    shot.hiddenAt = NowMs();
    if (post) KillTimer(post, HideTimer);
    s.captureWanted = false; Capture(t, shot.serial);
    ShowPanel();   // the engine works while the panel is built
}
// The page frame's native copy (src/annotation-frame.h), on screen when this returns true.
bool ShowFrame(const std::shared_ptr<Tab>& t, const AH::Selection& sel) {
    if (!sel.mark.present) return false;
    static const bool control = !EnvVar(L"BROWSER_ANNOTATIONS_QA_NO_NATIVE_FRAME").empty();   // QA negative control only: the V2 path (visible hide)
    if (control) return false;
    AUI::FrameMark m; m.page = PageClient(t); m.dpr = sel.target.viewport.devicePixelRatio; m.accent = AccentRgb();
    m.x = sel.mark.x; m.y = sel.mark.y; m.w = sel.mark.w; m.h = sel.mark.h; m.lx = sel.mark.lx; m.ly = sel.mark.ly; m.lw = sel.mark.lw; m.lh = sel.mark.lh; m.text = AH::Wide(sel.mark.text);
    m.rx = sel.mark.rx; m.ry = sel.mark.ry; m.rw = sel.mark.rw; m.rh = sel.mark.rh; m.rtext = AH::Wide(sel.mark.rtext);   // R14
    // V3-B: the spacing inspector (padding ring, gaps, value labels, the viewport of the hatches' phase)
    m.hasPad = sel.mark.hasPad; std::copy(sel.mark.pad, sel.mark.pad + 8, m.pad); m.radius = sel.mark.radius; m.innerRadius = sel.mark.innerRadius; m.vw = sel.mark.vw; m.vh = sel.mark.vh;
    for (const auto& b : sel.mark.bands) m.gaps.push_back({b.x, b.y, b.w, b.h});
    for (const auto& l : sel.mark.labels) m.labels.push_back({l.kind, l.x, l.y, l.w, l.h, AH::Wide(l.text)});
    if (sel.mark.HasSpace()) {   // QA evidence: what the native copy draws for the inspector
        std::wstring labels; for (const auto& l : m.labels) labels += (labels.empty() ? L"" : L",") + std::wstring(l.kind ? L"g" : L"p") + l.text;
        Log(L"ANNOTATE inspector at the selection: padding ring " + std::wstring(m.hasPad ? L"yes" : L"no") + L", " + std::to_wstring(m.gaps.size()) + L" gap rectangles, labels " + labels + L"; style " + AH::StyleSummary(sel.target));
    }
    if (post) KillTimer(post, FrameTimer);
    return frame.Show(m);
}
std::wstring Clip(std::wstring t, size_t n) { for (auto& c : t) if (c == L'\n' || c == L'\r' || c == L'\t') c = L' '; if (t.size() > n) { t.resize(n); t += L"…"; } return t; }
// The shot's start: the native copy of the frame on screen, then the page hides its own (« handoff ») — or, without the
// native layer, the page hides its whole frame (V2). Called at once, or right after OnSelect (Mode Commentaire).
void StartShot(const std::shared_ptr<Tab>& t) {
    if (!t || !shot.waiting) return;
    const auto& sel = shot.sel;
    const double f0 = NowMs(); const bool native = ShowFrame(t, sel); shot.frameMs = NowMs() - f0; shot.handoffAt = NowMs();
    if (native) { frameSerial = shot.serial; shot.pageSeq = sel.seq; Cmd(t, L"handoff", std::to_wstring(sel.seq)); }
    else { shot.pageSeq = std::int64_t(shot.serial) + (std::int64_t(1) << 41); Cmd(t, L"hide", std::to_wstring(shot.pageSeq)); Log(L"ANNOTATE native frame unavailable: the page hides its frame for the shot"); }
    if (s.serial == shot.serial) s.pageSeq = shot.pageSeq;
}
void OnSelect(const std::shared_ptr<Tab>& t, AH::Selection sel, double lag) {
    if (s.paused) return;   // V1.4: the selection is off (the page script sends none then)
    ++serials; s.serial = serials; s.frozen = true; s.cap = Cap::Pending; s.png.clear(); s.saving = false; s.sel = sel; s.pageSeq = sel.seq;
    s.selectedAt = NowMs(); s.clickLagMs = lag;
    BA::Annotation a;
    a.source.channel = SourceChannel;
#ifdef BROWSER_VERSION_TEXT
    a.source.version = BROWSER_VERSION_TEXT;
#endif
    a.source.storeTag = AH::StoreTag(DataRoot()); a.source.tabSerial = t->serial; a.source.navStamp = std::int64_t(s.navStamp);
    a.source.url = AH::Utf8(t->uri); a.source.title = AH::Utf8(t->title); a.source.frameKind = sel.frameKind;
    a.target = sel.target; a.target.viewport.zoom = s.zoom;
    BA::Sanitize(a); s.pending = a;
    // Screen geometry: CSS px x devicePixelRatio in the WebView's pixels.
    const RECT page = PageScreen(t); const double dpr = sel.target.viewport.devicePixelRatio; const auto& r = sel.target.rect;
    RECT target{page.left + LONG(std::lround(r.x * dpr)), page.top + LONG(std::lround(r.y * dpr)), page.left + LONG(std::lround((r.x + r.width) * dpr)), page.top + LONG(std::lround((r.y + r.height) * dpr))};
    // A target outside the visible page (text selection scrolled away, element off screen): the panel and its
    // pill are placed as if for the page's centre, so neither leaves the window.
    if (RECT in{}; !IntersectRect(&in, &target, &page)) { const LONG cx = (page.left + page.right) / 2, cy = (page.top + page.bottom) / 3; target = {cx - 1, cy - 1, cx + 1, cy + 1}; }
    else target = in;
    // R18 (< 100 ms from the click to the bubble): in Mode Commentaire the bubble opens FIRST; the shot (taken from the
    // page itself: the bubble is not in it) and its native frame follow.
    const bool commentFirst = Chat::CommentFirst(t) && Chat::TakeSelection(t, s.serial, !AH::PlanClip(r, sel.target.viewport).ok);
    AUI::NoteRequest req; req.draft = s.draft; req.capture = AUI::CaptureState::Pending; req.askAgent = Chat::AskAvailable(t);
    {   bool redacted = false; std::wstring shown = AH::Wide(BA::RedactUrl(a.source.url, &redacted));
        if (const auto at = shown.find(L"://"); at != std::wstring::npos) shown = shown.substr(at + 3);
        req.context.page = Clip(shown, 90);
        req.context.site = AH::Wide(BA::SiteLabel(s.origin));
        const auto& path = a.target.path; std::wstring el;
        for (size_t i = path.size() > 2 ? path.size() - 2 : 0; i < path.size(); ++i) el += (el.empty() ? L"" : L" > ") + AH::Wide(path[i]);
        if (sel.frameKind == "cross-origin") el = TRF(annotation_cross_origin_frame, el);
        req.context.element = Clip(el.empty() ? AH::Wide(a.target.tag) : el, 90);
        req.context.label = sel.frameKind == "cross-origin" ? TRF(annotation_cross_origin_frame, AH::ElementLabel(a.target)) : AH::ElementLabel(a.target);
        const std::wstring text = a.target.selectionText ? AH::Wide(*a.target.selectionText) : AH::Wide(a.target.text);
        req.context.text = text.empty() ? L"(aucun)" : L"« " + Clip(text, 80) + L" »";
        req.context.styles = AH::StylesText(a.target.styles);
        req.context.style = AH::StyleSummary(a.target);   // V3-B: « padding 20 · gap 14 (flex) » when the element has them
        const auto plan = AH::PlanClip(r, a.target.viewport);
        req.context.shotLabel = plan.ok ? TRF(annotation_shot_cropped, std::to_wstring(plan.outW), std::to_wstring(plan.outH)) : std::wstring(TR(annotation_shot_offscreen));
    }
    // V3 (retours §2): nothing changes on screen. The native layer draws the page frame's edges and label at the same
    // pixels and waits for the compositor; then the page hides its own (« handoff ») and answers « hidden » in the
    // same task; the shot requested then is rendered without them (a style change made in the task before is absent
    // from the next capture: 10/10 in V2 QA). Started BEFORE the panel is built, so the engine works while the UI
    // thread lays the panel out. Without the native layer (no geometry, no window): the V2 path, the page hides
    // its whole frame and answers two frames later (visible). The timer catches a page that never answers.
    if (!AH::PlanClip(r, a.target.viewport).ok) {   // nothing of it is on screen: « Capture impossible ici » at once
        s.cap = Cap::Failed; s.captureWanted = false; req.capture = AUI::CaptureState::Unavailable;
        Log(L"ANNOTATE capture unavailable (plan): outside the viewport");
    } else {
        shot = ShotJob{}; shot.serial = s.serial; shot.sel = sel; shot.selectedAt = s.selectedAt; shot.lag = lag; shot.waiting = true; s.captureWanted = true;
        // Mode Commentaire (H1's opening time): the bubble is already open; the native frame (screen copy + a wait for the
        // compositor, ~20 ms) and the shot start right after this message, so the UI thread goes back to the bubble now.
        // The page's commands keep their order (the chat's « follow » was sent above, the « handoff » comes after).
        if (commentFirst && EnsurePost() && PostMessageW(post, ShotStart, WPARAM(shot.serial), 0)) shot.deferred = true;
        else StartShot(t);
        if (EnsurePost()) SetTimer(post, HideTimer, 800, nullptr);
    }
    // chat intégré (V1.3): on a local site the element always becomes a chip of the chat's composer, the panel opening
    // if it is closed; the picker goes on (the shot first). Elsewhere (no chat): the note panel, as before.
    if (commentFirst || Chat::TakeSelection(t, s.serial, s.cap == Cap::Failed)) { Resume(!commentFirst); return; }
    panel.on = true; panel.serial = s.serial; panel.req = req; panel.target = target; panel.page = page; panel.keyboard = sel.keyboard;
    if (shot.waiting && shot.serial == s.serial && EnsurePost()) SetTimer(post, PanelTimer, 60, nullptr);   // shown by OnHidden, or by the timer
    else ShowPanel();
}
// #125: a private window's note is listed in its ledger BEFORE it is written (same site folder as Store::WriteAnnotation).
bool RecordPrivate(const BA::Annotation& a) {
    if (!privateLedger.Opened() && !privateLedger.Open(PrivateDir())) return false;
    const std::string siteKey = BA::SiteKey(BA::SiteOrigin(a.site.empty() ? BA::RedactUrl(a.source.url) : a.site));
    return privateLedger.Add(siteKey, a.id);
}
void OnSubmit(const AUI::NoteSubmission& sub) {
    auto t = ModeTab(); if (!s.active || !t) { note.Close(false); return; }
    if (s.saving) return;
    s.saving = true;
    BA::Annotation a = s.pending; std::wstring typed = sub.note;
    while (!typed.empty() && iswspace(typed.back())) typed.pop_back();   // a trailing Entrée/newline is not part of the note
    a.note = AH::Utf8(typed); a.id = BA::NewAnnotationId(); a.site = s.origin;
    a.source.profileId = AH::Utf8(profileId);
    if (const auto* p = CurrentProfile()) a.source.profileName = AH::Utf8(p->name); else a.source.profileName = AH::Utf8(DefaultProfileLook().name);
    BA::Sanitize(a);
    if (privateWindow && !RecordPrivate(a)) {   // #125: never a private note that its window could not erase
        s.saving = false; BA::Outcome o; o.code = BA::Code::Io; note.Failed(TRF(annotation_save_failed_reason, AnnotationProblem::Text(o)));
        Log(L"ANNOTATE private window: the ledger could not be written, nothing saved"); return;
    }
    auto png = s.cap == Cap::Ready && !s.png.empty() ? std::make_shared<std::string>(s.png) : nullptr;
    if (png) { a.screenshot.scale = s.shotScale; a.screenshot.crop = s.shotCrop; }
    // « Ajouter » never waits for the capture: it joins the annotation when it lands.
    if (!png && s.cap == Cap::Pending) {
        Late l; l.serial = s.serial; l.site = a.site; l.id = a.id; lates.push_back(l);
        // the page has not said « hidden » yet: the shot follows its pipeline and joins the annotation (OnCaptured)
    }
    struct Out { BA::Outcome result; BA::Entry entry; };
    auto out = std::make_shared<Out>(); const auto serial = s.serial; const double clicked = NowMs();
    Run([a, png, out]() mutable {
            std::lock_guard<std::mutex> g(writeLock);
            out->result = Store().WriteAnnotation(a, png.get(), false);
            if (out->result.ok()) Store().Read(a.siteKey, a.id, out->entry);
        },
        [out, serial, clicked] {
            const bool current = s.active && s.serial == serial;
            auto late = std::find_if(lates.begin(), lates.end(), [&](const Late& l) { return l.serial == serial; });
            if (!out->result.ok()) {
                if (late != lates.end()) lates.erase(late);
                Log(L"ANNOTATE write failed: " + AH::Wide(out->result.message));
                if (current) { s.saving = false; note.Failed(TRF(annotation_save_failed_reason, AnnotationProblem::Text(out->result))); }
                return;
            }
            const auto& e = out->entry; last = e; sessionIds.insert(e.annotation.id);
            Log(L"ANNOTATE written " + AH::Wide(e.annotation.siteKey + "/" + e.annotation.id) + (e.annotation.screenshot.present ? L" with shot" : late != lates.end() ? L" (shot joins when ready)" : L" without shot") +
                L" in " + Ms(NowMs() - clicked) + L" ms");
            if (late != lates.end()) { late->written = true; if (late->shot) { Attach(*late); lates.erase(late); } }
            if (!current) return;
            s.draft.clear(); note.Saved();
            card = agents.any() ? Card::Added : Card::NoAgent; cardId = e.annotation.id; cardSite = e.annotation.siteKey;
            AUI::CardContent c; c.kind = agents.any() ? AUI::CardKind::Added : AUI::CardKind::NoAgent; c.id = AH::Wide(e.annotation.id); c.note = AH::Wide(e.annotation.note);
            launcher.ShowCard(c);
            Resume();
        });
}

// ---------------------------------------------------------------- lifecycle
// #125 (variant A, src/annotation-onboarding.h): the first time the mode is turned on, its onboarding opens in the place of
// « Connecter un agent », the mode on behind it. A3 connects through ConnectApp (the same code as the modal); « Autre app
// ou réglage manuel » leaves it for the modal with the manual setup unfolded. Finished or skipped (« Plus tard », the close
// button, Échap), it never comes back (setting annotation-onboarded); « --annotation-onboarding » shows it every time (QA).
bool OnboardingForced() { static const bool forced = Onboarding::HasSwitch(GetCommandLineW(), AnnotationOnboarding::Switch); return forced; }
void OnboardingClosed(AnnotationOnboarding::Ending end, bool manual) {
    namespace AO = AnnotationOnboarding;
    if (end != AO::Ending::None) Prefs::SetOnboarded(settings);
    if (end == AO::Ending::Skipped && !manual) laterThisSession = true;   // « Plus tard »: no « Connecter un agent » modal in this session either
    Log(std::wstring(L"ANNOTATE onboarding ") + (end == AO::Ending::Finished ? L"finished" : manual ? L"left for the manual setup" : L"skipped") + L" (" + (agents.claude.connected() ? L"Claude" : L"") +
        (agents.codex.connected() ? L" Codex" : L"") + (agents.any() ? L" connected)" : L"no agent connected)") + (Prefs::HasOnboarded<BrowserSettings::State>::value ? L"" : L", not persisted: no setting key"));
    if (manual && s.active) { OpenConnect(false, true); return; }
    if (s.active) { banner.SetReminder(ReminderWanted()); FocusPage(); }
    SyncWatch();
}
// #125 v2: the tutorial's colour is the profile's, like the mode's; « Personnel » (no colour) takes the mode's blue
// #3E67FF (Val), its gradient turned to that hue.
void TintTutorial(NativeChrome::AnnotationOnboardingUI::Options& o) {
    const auto* p = CurrentProfile();
    if (p && !p->neutral && p->intensity > 0) { o.tint = *p; o.accent = 0; return; }
    int hue = 267; float chroma = ProfileColor::MaxChroma; ProfileColor::FromHex(L"#3E67FF", hue, chroma);
    o.tint.hue = hue; o.tint.intensity = chroma; o.tint.neutral = false; o.accent = 0x3E67FF;
}
bool OpenOnboarding(AnnotationOnboarding::Step start = AnnotationOnboarding::Step::What) {
    namespace OUI = NativeChrome::AnnotationOnboardingUI;
    EnsureUi();
    if (OnboardingOpen()) return true;
    if (localCard.Visible()) localCard.Close();
    OUI::Options o; o.reduced = Reduced(); o.start = start; TintTutorial(o);   // « Ton agent »: A6 / A7 read the agents' one status (no MCP rows)
    o.hooks.closed = [](AnnotationOnboarding::Ending end, bool manual) { OnboardingClosed(end, manual); };
    o.hooks.language = [](I18n::Locale l) { const bool ok = UiLanguage::Save(DataRoot(), l); Log(std::wstring(L"ANNOTATE onboarding language ") + (l == I18n::Locale::En ? L"en" : L"fr") + (ok ? L" saved" : L" NOT saved")); };
    o.hooks.announce = [](const std::wstring& t) { Announce(t); };
    onboarding = std::make_unique<OUI::Window>();
    if (!onboarding->Open(instance, window, base, ClientScreen(), std::move(o))) { onboarding.reset(); Log(L"ANNOTATE onboarding could not open: « Connecter un agent » instead"); return false; }
    if (s.active) banner.SetReminder(false, true);
    SyncWatch(); RefreshAgents();
    Log(std::wstring(L"ANNOTATE onboarding opened") + (OnboardingForced() ? L" (--annotation-onboarding)" : L" (first time)") + (ConnectRedirected() ? L", connection writes redirected to BROWSER_ANNOTATIONS_CONNECT_ROOT" : L""));
    return true;
}
void ConnectClosed(AUI::ConnectEnd end) {
    if (end == AUI::ConnectEnd::Later && connectLaunch) {
        laterThisSession = true;
        Prefs::AddDecline(settings);
        Log(L"ANNOTATE connect: « Plus tard » (" + std::to_wstring(Prefs::Declines(settings)) + L"/" + std::to_wstring(Prefs::MaxDeclines) + (Prefs::HasDeclines<BrowserSettings::State>::value ? L")" : L", not persisted: no setting key yet)"));
    }
    connectLaunch = false;
    if (s.active) { banner.SetReminder(ReminderWanted()); FocusPage(); }
    SyncWatch();
}
void EnsureUi() {
    if (uiReady) return; uiReady = true;
    AUI::BannerHooks bh; bh.exit = [] { Exit(L"quit"); };
    bh.escape = [] { if (AH::OnEscape(false, Chat::BubbleOpen()) == AH::EscapeAction::CloseBubble) Chat::CloseBubble(true); else Exit(L"escape"); };   // as the page's Escape (review M4)
    bh.connect = [] { OpenTonAgent(TonAgentFrom::Band); }; bh.announce = [](const std::wstring& t) { Announce(t); };   // « Ton agent »: the reminder opens the agents' screen
    AgentStatus::Subscribe([](const AgentStatus::Status&) { SyncReminder(); });   // the reminder follows the chat agents (an install, a sign-in, a detection)
    bh.chat = [] { Chat::SwitchLayout(false); };     // « Panneau »: the chat at the right (opens it)
    bh.comment = [] { Chat::SwitchLayout(true); };   // « Commentaire »: a bubble on the element clicked
    banner.Init(instance, window, base, bh);
    AUI::NoteHooks nh;
    nh.submit = [](const AUI::NoteSubmission& sub) { OnSubmit(sub); };
    nh.cancel = [](const std::wstring& draft, bool) { s.draft = draft; Resume(); };
    nh.ask = [](const std::wstring& draft) { Chat::AskFromNote(draft); };
    nh.announce = [](const std::wstring& t) { Announce(t); };
    note.Init(instance, window, base, nh);
    AUI::LauncherHooks lh;
    lh.toggle = [] { if (launcher.HistoryVisible()) launcher.HideHistory(); else OpenHistory(false); SyncWatch(); };
    lh.open = [] { const std::string id = cardId; launcher.HideCard(); card = Card::None; OpenHistory(false, id); };
    lh.connect = [] { launcher.HideCard(); card = Card::None; OpenConnect(false); };
    lh.reload = [] { launcher.HideCard(); card = Card::None; if (auto t = Active(); t && t->view.p) Command('R'); };
    lh.reopen = [] {
        const std::string site = cardSite, id = cardId; launcher.HideCard(); card = Card::None; seenDone.erase(id); seenWorking.erase(id);
        auto r = std::make_shared<BA::Outcome>();
        Run([site, id, r] { std::lock_guard<std::mutex> g(writeLock); *r = Store().Reopen(site, id); }, [id, r] { Log(L"ANNOTATE reopen " + AH::Wide(id) + L": " + AH::Wide(BA::CodeName(r->code)) + (r->message.empty() ? L"" : L" — " + AH::Wide(r->message))); Announce(r->ok() ? std::wstring(TR(annotation_reopened)) : TRF(annotation_reopen_failed, AnnotationProblem::Text(*r))); });
    };
    lh.dismissed = [](bool) { card = Card::None; };
    lh.focusOut = [] { FocusPage(); };
    lh.announce = [](const std::wstring& t) { Announce(t); };
    lh.history.remove = [](const std::wstring& id) { HistoryStoreOp(id, 0); };
    lh.history.undo = [](const std::wstring& id) { HistoryStoreOp(id, 1); };
    lh.history.removeFinal = [](const std::wstring& id) { HistoryStoreOp(id, 2); };
    lh.history.detail = [](const std::wstring& id) { if (Chat::OpenFromHistory(id)) return; HistoryDetail(id); };
    lh.history.copyEntry = [](const std::wstring& id) {
        const auto* e = FindHistory(id); if (!e) return;
        if (CopyToClipboard(AH::Wide(BA::ToMarkdown(*e, e->annotation.screenshot.present ? Store().ShotPath(e->annotation.siteKey, e->annotation.id) : L"")))) Log(L"ANNOTATE copied as Markdown " + id);
    };
    lh.history.copy = [](const std::wstring& md) { CopyToClipboard(md); };
    lh.history.reopen = [](const std::wstring& id) {   // V3-B (Paper 5.5): back to waiting for an agent
        const auto* e = FindHistory(id); if (!e) return;
        const std::string site = e->annotation.siteKey, ann = e->annotation.id; auto r = std::make_shared<BA::Outcome>();
        seenUnhandled.erase(ann); seenDone.erase(ann); seenWorking.erase(ann);
        Run([site, ann, r] { std::lock_guard<std::mutex> g(writeLock); *r = Store().Reopen(site, ann); },
            [ann, r] { Log(L"ANNOTATE history reopen " + AH::Wide(ann) + L": " + AH::Wide(BA::CodeName(r->code)) + (r->message.empty() ? L"" : L" — " + AH::Wide(r->message)));
                       if (!r->ok()) Announce(TRF(annotation_reopen_failed, AnnotationProblem::Text(*r)));
                       if (launcher.HistoryVisible()) LoadHistory(false, false);
                       RefreshStatus(); });
    };
    lh.history.annotate = [] { if (!s.active) AnnotationCommand(AnnotateCommand); };
    lh.history.closed = [](bool) { SyncWatch(); FocusPage(); };
    lh.history.announce = [](const std::wstring& t) { Announce(t); };
    launcher.Init(instance, window, base, lh);
    AUI::ConnectHooks ch;
    ch.connect = [](AUI::AgentKind a) { ConnectApp(a, true); };
    ch.disconnect = [](AUI::AgentKind a) { ConnectApp(a, false); };
    ch.copy = [](const std::wstring& text) { if (CopyToClipboard(text)) Log(L"ANNOTATE configuration copied"); };
    ch.closed = [](AUI::ConnectEnd end) { ConnectClosed(end); };
    ch.announce = [](const std::wstring& t) { Announce(t); };
    connect.Init(instance, window, base, ch);
    frame.Init(instance, window, base);
    veil.Init(instance, window, base);
    veil.OnEnded([](double ms, unsigned long long frames, double worst) {
        Log(L"ANNOTATE entry veil destroyed after " + Ms(ms) + L" ms (" + std::to_wstring(frames) + L" frames, worst frame " + Ms(worst) + L" ms)"); });
}
// #125 v2 E1b: the card under the greyed button (a click on it, or Ctrl+Maj+A, on a live site).
void ShowLocalCard(const std::shared_ptr<Tab>& t) {
    EnsureUi();
    if (OnboardingOpen()) return;
    if (localCard.Visible()) { localCard.Close(); return; }   // a second click puts it away
    RECT button = RegionScreen('N');
    if (IsRectEmpty(&button)) { RECT c = ClientScreen(); button = {c.right - 120, c.top + 48, c.right - 96, c.top + 72}; }
    std::wstring host = t ? AH::Wide(ChatLocal::Classify(AH::Utf8(t->uri)).label) : std::wstring();
    if (host.empty() && t) host = t->uri;
    NativeChrome::AnnotationOnboardingUI::Options tint; TintTutorial(tint);
    const auto accent = tint.accent ? tint.accent : [&] { const auto a = NativeChrome::OnboardingKit::Accent(tint.tint); return (unsigned(a.r) << 16) | (unsigned(a.g) << 8) | a.b; }();
    NativeChrome::AnnotationLocalCard::Hooks h;
    h.discover = [] { Log(L"ANNOTATE live-site card: « Découvrir en 1 minute »"); OpenOnboarding(); };
    h.announce = [](const std::wstring& text) { Announce(text); };
    // The click's own tooltip (E1a) goes at once and none shows over the card while it is open (Fix's PR #131 check: the
    // card's appearance moves the pointer « over » the button again, which brought the tooltip back on top of the title).
    // Blocked once shown: a card still fading out is finished inside Show, and its own `closed` lifts the block.
    h.closed = [] { ChromeTipBlock(false); };
    if (localCard.Show(instance, window, base, button, host, accent, Reduced(), std::move(h))) { ChromeTipBlock(true); Log(L"ANNOTATE live-site card shown (the mode is for local projects and files)"); }
}
void Activate(const Start& st) {
    auto t = Active(); std::wstring why;
    if (!Available(t, &why)) {
        if (why == TR(annotate_local_only)) { ShowLocalCard(t); return; }   // #125 v2 E1b: a live site — why, and the tutorial
        Announce(L"Annoter : " + why); return;
    }
    if (s.active) { if (ModeTab() == t) { if (st.kind) SendStart(t, st); return; } Exit(L"tab"); }
    EnsureUi(); EnsurePost(); Store();   // the store object is created here, on the UI thread, before any job uses it
    const std::wstring keepDraft = s.draft;
    s = Session{}; s.active = true; s.tab = t; s.navStamp = t->navStamp; ++serials; s.serial = serials; s.nonce = BA::NewClaimToken();
    if (s.nonce.empty()) { s.active = false; Log(L"ANNOTATE no random source"); return; }
    CtrlWatch();   // R31
    { LPWSTR src = nullptr; if (SUCCEEDED(t->view->get_Source(&src)) && src) s.source = src; CoTaskMemFree(src); }
    s.origin = AH::OriginOf(t->uri); t->controller->get_ZoomFactor(&s.zoom); s.draft = keepDraft;
    s.pageAccent = SettledAccentRgb();   // the profile's colour, even when the entry falls inside its 120 ms fade
    const std::string nonce = s.nonce;
    auto cb = Callback<ICoreWebView2ExecuteScriptCompletedHandler, HRESULT, LPCWSTR>([nonce](HRESULT hr, LPCWSTR result) -> HRESULT {
        if (!s.active || s.nonce != nonce) return S_OK;
        if (FAILED(hr) || !result || wcscmp(result, L"\"ok\"") != 0) { Log(L"ANNOTATE script injection failed"); Exit(L"injection"); Announce(TR(annotation_mode_unavailable)); }
        else { s.injected = true; Chat::ScriptReady(); }   // Mode Commentaire: the frames of the page's conversations
        return S_OK; });
    const ULONGLONG started = GetTickCount64();
    chrome.annotationBand = NativeChrome::Annotations::BannerSpec::height; SyncChrome(); Layout();   // the band first: the page is measured in its final viewport
    if (FAILED(t->view->ExecuteScript(AUI::PageScriptCall(Params(t, st)).c_str(), cb))) { cb->Release(); s.active = false; chrome.annotationBand = 0; SyncChrome(); Layout(); Log(L"ANNOTATE ExecuteScript refused"); return; }
    cb->Release();
    banner.Show(BandRect(), Reduced());
    Chat::ModeEntered();   // the band's « Demander à l'agent » button
    if (!veil.Start(PageClient(t), Reduced())) Log(L"ANNOTATE entry veil unavailable");   // V3-B: entry only
    LauncherSync(true);
    if (!purged) { purged = true; const std::int64_t retention = Prefs::Retention(settings); Run([retention] { std::lock_guard<std::mutex> g(writeLock); Store().Purge(retention); }, nullptr); }   // retention: first use of the session
    // Launch rule (« Ton agent »): the tutorial the first time; otherwise nothing opens by itself — the band's reminder only.
    const bool direct = st.kind != 0;   // started on an element from the page menu: the note comes first
    RefreshAgents([direct, nonce] {
        if (!s.active || s.nonce != nonce) return;
        AnnotationOnboarding::Launch first; first.onboarded = Prefs::Onboarded(settings); first.forced = OnboardingForced(); first.direct = direct; first.chatOpen = Chat::Open();
        first.busy = connect.Visible() || note.Visible() || OnboardingOpen();
        if (AnnotationOnboarding::ShouldShow(first) && OpenOnboarding()) return;   // #125: the first time, the onboarding (agents connected or not)
        banner.SetReminder(ReminderWanted());   // « Ton agent »: no « Connecter un agent » window by itself any more; the reminder is the chat's
    });
    RefreshStatus(); SyncWatch();
    Log(L"ANNOTATE mode on (" + std::to_wstring(GetTickCount64() - started) + L" ms to inject request, accent " + AH::Wide(AccentInts(s.pageAccent)) + L") " + t->uri);
}
// Chat intégré (V1.4): the selection — the « viseur » cursor, the hover frame, a click that picks an element — can be
// switched off while the chat panel stays open with its conversation and its chips (the panel's aim button,
// Ctrl+Maj+A or the toolbar « Annoter » button while the chat is open). The page is the page again; the band goes up
// and the toolbar button is no longer lit, as when the mode is off; the page script stays (the chat's marks, the
// element's source). On again: the band comes back, the « viseur » too. Closing the chat while it is off ends the mode.
// R25: the band slides in / out (the page and the chat panel are laid out under it at every frame: no jump). As short
// as the band's own entrance, on the panel's curve; reduced motion: at once.
inline constexpr int BandSlideInMs = 240, BandSlideOutMs = 240;   /* the entrance has more to do on its first frame (the band shown): a little longer */   // R25: ~12 frames at 60 Hz, regular steps
AUI::Ramp bandSlide; bool bandSliding = false, bandHideAtEnd = false; int bandFrames = 0;
void BandSlideApply(NativeChrome::Motion::Time now) {
    const float full = float(AUI::BannerSpec::height), cur = AUI::BandSlideHeight(bandSlide.Value(now), full, Reduced());
    chrome.annotationBand = cur; SyncChrome(); Layout();
    banner.Slide(BandRect(), full - cur);
}
struct BandSlideClient : AUI::Animator::Client {
    bool Frame(NativeChrome::Motion::Time now) override {
        if (!s.active) { bandSliding = false; return false; }
        BandSlideApply(now); ++bandFrames;
        if (bandSlide.Moving(now)) return true;
        bandSliding = false; Layout();   // R25: the one size change, on the side that is out of sight
        if (bandHideAtEnd) { bandHideAtEnd = false; banner.Hide(false); chrome.annotationBand = 0; SyncChrome(); Layout(); }
        Log(L"ANNOTATE band slide done (" + std::to_wstring(int(chrome.annotationBand)) + L" DIP, " + std::to_wstring(bandFrames) + L" frames)"); bandFrames = 0;
        return false;
    }
} bandSlideClient;
void SetSelecting(bool on) {
    if (!s.active || s.paused == !on) return;
    auto t = ModeTab();
    s.paused = !on;
    if (!on) {
        if (note.Visible()) note.Close(false);
        panel = PendingPanel{}; s.frozen = false;
        Cmd(t, L"pick", L"false");
    } else {
        Cmd(t, L"pick", L"true");
    }
    // R25: the band slides; the page and the panel follow it frame by frame (Layout at every step)
    const auto now = NativeChrome::Motion::Now(); const bool reduced = Reduced();
    if (!bandSliding) bandSlide.Snap(on ? 0.f : 1.f);
    bandSlide.Set(on ? 1.f : 0.f, now, reduced ? 0 : (on ? BandSlideInMs : BandSlideOutMs), AUI::Curves::easeOut);   // R25: no front-loaded jump (the band curve moved 22 px in its first frame)
    bandHideAtEnd = !on;
    if (!reduced) bandSliding = true;   // R25: from the first frame, the page only moves
    if (on) { banner.Show(BandRect(), reduced); Chat::ModeEntered(); }
    if (on && !reduced) { const auto at = NativeChrome::Motion::Now(); bandSlide.Snap(0.f); bandSlide.Set(1.f, at, BandSlideInMs, AUI::Curves::easeOut); }   // R25: the ramp starts after the band's first render
    BandSlideApply(NativeChrome::Motion::Now());
    if (reduced) { bandSliding = false; if (!on) { bandHideAtEnd = false; banner.Hide(false); chrome.annotationBand = 0; SyncChrome(); Layout(); } }
    else { bandSliding = true; AUI::Animator::Get().Request(&bandSlideClient); }
    Announce(on ? TR(annotation_selection_on) : TR(annotation_selection_off));
    Log(std::wstring(L"ANNOTATE selection ") + (on ? L"on" : L"off (the chat stays open)"));
    Chat::SelectionChanged();
}
void Exit(const wchar_t* why, bool pageChanged) {
    if (!s.active) return;
    auto t = ModeTab(); const bool hadPopover = note.Visible();
    Cmd(t, L"exit");
    if (post) { KillTimer(post, HideTimer); KillTimer(post, FrameTimer); KillTimer(post, PanelTimer); }
    panel = PendingPanel{};
    frame.Destroy(); shot = ShotJob{}; resumeOwed = false; frameSerial = 0;   // the page's frame is gone with its script
    const std::wstring draft = hadPopover && note.Edit() ? [] { const int n = GetWindowTextLengthW(note.Edit()); std::wstring d(size_t(n) + 1, L'\0'); GetWindowTextW(note.Edit(), d.data(), n + 1); d.resize(size_t(n)); return d; }() : s.draft;
    // A capture still owed to a written annotation is lost if its page goes away (the note stays, without image).
    lates.erase(std::remove_if(lates.begin(), lates.end(), [](const Late& l) { return l.serial == s.serial && !l.shot; }), lates.end());
    s = Session{}; s.draft = pageChanged ? std::wstring() : draft; ++serials;
    CtrlWatch();   // R31: the mode is off: no more reading, the state forgotten
    note.Close(!pageChanged); banner.Hide(); if (connect.Visible()) connect.Close(false);
    if (OnboardingOpen()) { onboarding->Dismiss(); Log(L"ANNOTATE onboarding closed by the mode's exit (not answered: shown again next time)"); }
    veil.Interrupt();   // V3-B: an exit during the entry fades the veil from its value (none at the exit otherwise)
    card = Card::None;
    chrome.annotationBand = 0; SyncChrome(); Layout();
    LauncherSync();   // the launcher goes back into its corner; its windows are destroyed once it is gone
    if (pageChanged && hadPopover) Announce(TR(annotation_page_changed));
    SyncWatch();
    Log(std::wstring(L"ANNOTATE mode off (") + why + L")");
    Chat::ModeExited();
}
// Chat intégré: the page of the mode was reloaded (by the chat after a turn, or by the dev server) while the chat panel
// is open. The mode stays: band, panel and launcher do not move; the selection state is dropped and the page script is
// injected again in the new document. Called once that document is complete.
void Rearm(const std::shared_ptr<Tab>& t, std::function<void(bool)> then = nullptr) {
    if (!s.active || ModeTab() != t || !t || !t->view.p) { if (then) then(false); return; }
    std::wstring why;
    if (!Available(t, &why)) { Exit(L"navigation", true); if (then) then(false); return; }
    if (post) { KillTimer(post, HideTimer); KillTimer(post, FrameTimer); KillTimer(post, PanelTimer); }
    panel = PendingPanel{}; frame.Destroy(); shot = ShotJob{}; resumeOwed = false; frameSerial = 0;
    if (note.Visible()) { note.Close(false); Announce(TR(annotation_page_changed)); }
    lates.erase(std::remove_if(lates.begin(), lates.end(), [](const Late& l) { return l.serial == s.serial && !l.shot; }), lates.end());
    ++serials; s.serial = serials; s.frozen = s.saving = s.captureWanted = s.injected = false; s.cap = Cap::None; s.png.clear(); s.pageSeq = -1;
    s.navStamp = t->navStamp; s.nonce = BA::NewClaimToken(); s.source.clear();
    { LPWSTR src = nullptr; if (SUCCEEDED(t->view->get_Source(&src)) && src) s.source = src; CoTaskMemFree(src); }
    s.origin = AH::OriginOf(t->uri); t->controller->get_ZoomFactor(&s.zoom); s.pageAccent = SettledAccentRgb();
    const std::string nonce = s.nonce;
    auto cb = Callback<ICoreWebView2ExecuteScriptCompletedHandler, HRESULT, LPCWSTR>([nonce, then](HRESULT hr, LPCWSTR result) -> HRESULT {
        if (!s.active || s.nonce != nonce) { if (then) then(false); return S_OK; }
        const bool ok = SUCCEEDED(hr) && result && wcscmp(result, L"\"ok\"") == 0;
        if (!ok) { Log(L"ANNOTATE script re-injection failed"); Exit(L"injection"); Announce(TR(annotation_mode_unavailable)); }
        else { s.injected = true; Log(L"ANNOTATE mode re-armed after a reload"); if (s.paused) Cmd(ModeTab(), L"pick", L"false"); Chat::ScriptReady(); }   // D1: a reload empties the page script: the chat's marks and the open bubble's element again
        if (then) then(ok);
        return S_OK; });
    if (FAILED(t->view->ExecuteScript(AUI::PageScriptCall(Params(t, {})).c_str(), cb))) { cb->Release(); Exit(L"injection"); if (then) then(false); return; }
    cb->Release();
}
LRESULT CALLBACK PostProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == JobDone) {
        std::vector<std::unique_ptr<Job>> done; { std::lock_guard<std::mutex> lock(jobsMutex); done.swap(jobsDone); }
        LogMigration(); for (auto& j : done) if (j->done && !halted) j->done(); return 0;
    }
    if (m == ShotStart) { if (s.active && shot.waiting && shot.deferred && shot.serial == std::uint64_t(w)) { shot.deferred = false; StartShot(ModeTab()); } return 0; }
    if (m == WatchChanged) {
        watchSignal = false; if (!watch.thread) return 0;
        RefreshStatus();
        if (launcher.HistoryVisible()) LoadHistory(false, false);
        if (connect.Visible() || OnboardingOpen() || pendingInstall) RefreshAgents();
        return 0;
    }
    if (m == WM_TIMER && w == HideTimer) { KillTimer(h, HideTimer); if (s.active && shot.waiting) { s.captureWanted = false; Log(L"ANNOTATE page did not confirm the hidden frame: capturing anyway"); Capture(ModeTab(), shot.serial); } ShowPanel(); return 0; }
    if (m == WM_TIMER && w == PanelTimer) { KillTimer(h, PanelTimer); ShowPanel(); return 0; }
    if (m == WM_TIMER && w == CtrlTimer) { if (!s.active || !OursInFront()) { CtrlWatch(); return 0; } CtrlArm(PointerOverPage() ? 30 : 200); CtrlPoll(); return 0; }   // R31, PERF-20
    if (m == WM_TIMER && w == FrameTimer) { KillTimer(h, FrameTimer); frame.Hide(); return 0; }
    return DefWindowProcW(h, m, w, l);
}
} // namespace Ann

// ---------------------------------------------------------------- host entry points (declared at the top of main.cpp)
// #125: at start (every window, once the profile root is known), the notes of a private window that did not close
// normally; nothing to do (one attribute read) when no private window ever annotated.
void AnnotationStartup() {
    const std::wstring dir = Ann::PrivateDir();
    if (GetFileAttributesW(dir.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    auto r = std::make_shared<AnnotationPrivate::Report>();
    Ann::Run([dir, r] { std::lock_guard<std::mutex> g(Ann::writeLock); *r = AnnotationPrivate::CleanOrphans(dir, Ann::Store()); },
             [r] { if (r->ledgers) Log(L"ANNOTATE private notes left by a closed private window erased: " + std::to_wstring(r->deleted) + L" deleted, " + std::to_wstring(r->missing) + L" already gone, " + std::to_wstring(r->failed) + L" kept for next time"); });
}
// #125: a private window closes: what it annotated goes with it (the notes still being written are waited for, 2 s at most;
// past that the ledger stays and the next start finishes the job).
void AnnotationShutdown() {
    if (!privateWindow || !Ann::privateLedger.Opened()) return;
    for (int i = 0; i < 200 && Ann::jobsInFlight.load() > 0; ++i) Sleep(10);
    const bool settled = Ann::jobsInFlight.load() == 0;
    AnnotationPrivate::Report r; { std::lock_guard<std::mutex> g(Ann::writeLock); r = Ann::privateLedger.Erase(Ann::Store(), !settled); }
    Log(L"ANNOTATE private window closed: " + std::to_wstring(r.deleted) + L" note(s) erased, " + std::to_wstring(r.missing) + L" already gone, " + std::to_wstring(r.failed) + L" left for the next start" + (settled ? L"" : L" (a write was still running)"));
}
bool AnnotationModeActive() { return Ann::s.active; }
bool AnnotationBandSliding() { return Ann::s.active && Ann::bandSliding; }   // R25: the page and the chat panel only move meanwhile
bool AnnotationSurfaceFocus() {
    if (!Ann::uiReady) return false;
    HWND f = GetFocus(); if (!f) return false;
    for (HWND w : {Ann::note.Window(), Ann::connect.Window(), Ann::onboarding ? Ann::onboarding->Handle() : HWND(nullptr), Ann::localCard.Window(), Ann::launcher.LauncherWindow(), Ann::launcher.CardWindow(), Ann::launcher.HistoryWindow(), Ann::launcher.ToastWindow(), Ann::banner.Window()})
        if (w && (f == w || IsChild(w, f))) return true;
    return false;
}
void AnnotationSyncChrome() { Ann::SyncChrome(); Ann::LauncherSync(); }
void AnnotationLayout() { Ann::Place(); }
void AnnotationWindowState() { if (Ann::uiReady) Ann::banner.WindowStateChanged(); Ann::CtrlWatch(); }   // R31: Ctrl read only in front   // V3-B: the wave's clock
void AnnotationAccentChanged() { Ann::AccentChanged(); }   // profiles-integration.h: the window's accent changed
void AnnotationNavigation(const std::shared_ptr<Tab>& t) {
    if (Ann::localCard.Visible()) Ann::localCard.Close();   // #125 v2 E1b: the card was about the page that leaves
    if (!Ann::s.active || Ann::ModeTab() != t) return;
    if (Chat::KeepsMode(t)) Chat::PageNavigating(t);   // chat intégré: the mode survives a reload of its page (Ann::Rearm)
    else Ann::Exit(L"navigation", true);
}
void AnnotationTabChanged() { if (Ann::localCard.Visible()) Ann::localCard.Close(); if (Ann::s.active && Ann::ModeTab() != Active()) Ann::Exit(L"tab change"); Ann::LauncherSync(); }   // i18n-exempt: log reason
void AnnotationTabSleeping(const std::shared_ptr<Tab>& t) { if (Ann::s.active && Ann::ModeTab() == t) Ann::Exit(L"tab asleep"); }   // i18n-exempt: log reason
bool AnnotationPageMessage(const std::shared_ptr<Tab>& t, ICoreWebView2WebMessageReceivedEventArgs* a) {
    using namespace Ann;
    if (!s.active || !t || ModeTab() != t || !a) return false;
    LPWSTR raw = nullptr;
    if (FAILED(a->get_WebMessageAsJson(&raw)) || !raw) { CoTaskMemFree(raw); return false; }
    std::wstring json = TakeString(raw);
    if (!AH::LooksLikeAnnotation(json)) { Wipe(json); return false; }
    const std::wstring source = TakeString([&] { LPWSTR x = nullptr; a->get_Source(&x); return x; }());
    const std::string utf8 = AH::Utf8(json); Wipe(json);
    if (t != Active()) { LogOnce(L"inactive tab"); return true; }   // i18n-exempt: log reason
    if (source != s.source) { LogOnce(L"source"); return true; }
    if (t->navStamp != s.navStamp) { LogOnce(L"navStamp"); return true; }
    // #125, belt and braces: whatever path brought the mode here, a page that is not a local project or file gets no
    // selection, no note and no bubble — its message ends the mode (the button then shows why it is off).
    if (!SiteAllowed(source) || !SiteAllowed(t->uri)) { Log(L"ANNOTATE message from a page that is not local: mode off"); Exit(L"page not local", true); return true; }   // i18n-exempt: log reason
    const auto c = AH::CheckMessage(utf8, s.nonce, std::int64_t(s.navStamp));
    // R45 (125 / 150 %): the page's zoom as it is NOW (Ctrl+= / Ctrl+- after the mode started): the bubble's first
    // placement (« selecting ») converts the element's CSS px with it; a stale 100 % put the bubble over the labels for
    // a moment (« element 1310x60 » then « 1638x75 »), then the full « select » moved it.
    if (c.env.op == AH::Op::Selecting || c.env.op == AH::Op::Select || c.env.op == AH::Op::Anchor) RefreshZoom(t);
    switch (c.env.op) {
    case AH::Op::Select: {
        double lag = -1;   // click -> host, from the page's Date.now() (same system clock)
        if (c.selection.at > 0) { FILETIME ft; GetSystemTimeAsFileTime(&ft); ULARGE_INTEGER u{}; u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime;
            const double now = double(u.QuadPart / 10000ULL) - 11644473600000.0; if (now - c.selection.at > -50 && now - c.selection.at < 10000) lag = std::max(0.0, now - c.selection.at); }
        const double h0 = NowMs(); OnSelect(t, c.selection, lag);
        Log(L"ANNOTATE « select » handled in " + Ms(NowMs() - h0) + L" ms on the UI thread"); break; }
    case AH::Op::Hidden: OnHidden(t, c.env.serial); break;
    case AH::Op::Shown: OnShown(c.env.serial); break;
    case AH::Op::Escape:   // review M4 (b): an open bubble closes first, the mode stays
        switch (AH::OnEscape(s.paused, Chat::BubbleOpen())) { case AH::EscapeAction::CloseBubble: Chat::CloseBubble(true); break; case AH::EscapeAction::Leave: Exit(L"escape"); break; default: break; }
        break;
    case AH::Op::NoSelection: Announce(TR(annotation_no_selection)); break;
    case AH::Op::Anchor: Chat::OnAnchor(c.env); break;
    case AH::Op::Mods: CtrlPoll(); break;   // R31: a pointer move over the page: Ctrl read now
    case AH::Op::Selecting: if (!s.paused) Chat::OpenEarly(t, c.env); break;   // R18: before the full « select » of the same seq
    default: LogOnce(AH::Wide(c.why.empty() ? std::string("invalid") : c.why)); break;
    }
    return true;
}
bool AnnotationCommand(int cmd) {
    if (cmd == 'N' || cmd == AnnotateCommand) {
        if (Ann::s.active && Chat::Open()) { Ann::SetSelecting(Ann::s.paused); return true; }   // V1.4: with the chat open, the selection only
        if (Ann::s.active) Ann::Exit(L"toggle"); else Ann::Activate({}); return true;
    }
    if (cmd == AnnotateExitCommand) { if (Chat::BubbleOpen()) { Chat::CloseBubble(true); return true; } Ann::Exit(L"escape"); return true; }   // M4 (Q's real window): Échap in the browser's window closes an open bubble first
    if (cmd == ChatCommand) { Chat::Toggle(true); return true; }   // Ctrl+Maj+D: « Demander à l'agent » (starts the mode if needed)
    if (cmd == AnnotateHistoryCommand) {
        if (Ann::uiReady && Ann::launcher.HistoryVisible()) { Ann::launcher.HideHistory(); Ann::SyncWatch(); }
        else if (!privateWindow) Ann::OpenHistory(true);
        return true;
    }
    return false;
}
bool AnnotationHistoryAvailable() { return !privateWindow && !halted; }
// ---- src/annotation-entry.h (settings section « Annotations »)
std::wstring AnnotationStoreRoot() { return Ann::Store().Root(); }
void AnnotationOpenConnect(bool manual) { if (!halted) Ann::OpenConnect(false, manual); }
void AnnotationOpenTutorial(bool fromWelcome) { if (!halted) Ann::OpenOnboarding(fromWelcome ? AnnotationOnboarding::Step::Pick : AnnotationOnboarding::Step::What); }
// « Ton agent » (src/agent-status.h) for Réglages › Agents (src/annotation-entry.h).
bool ChatAgentsAvailable() { return true; }
AnnotationSettings::ChatAgents ChatAgentsStatus() { return Ann::ChatAgentsNow(); }
void ChatAgentsWatch(std::function<void()> changed) { AgentStatus::Subscribe([changed](const AgentStatus::Status&) { if (changed) changed(); }); }
void ChatAgentsRefresh() { AgentStatus::Refresh(); }
// Called from the row's click only (the drawer's Activate): Install / Retry get their consent HERE, from that click.
void ChatAgentAct(AnnotationSettings::ChatApp app, AnnotationSettings::ChatAction action) {
    namespace AS = AnnotationSettings; const auto agent = AS::ToAgent(app); bool started = false;
    switch (action) {
    case AS::ChatAction::Install: started = AgentStatus::Install(AgentStatus::UserClicked(agent)); break;
    case AS::ChatAction::Retry: started = AgentStatus::Retry(AgentStatus::UserClicked(agent)); break;
    case AS::ChatAction::Update: started = AgentStatus::Install(AgentStatus::UserClicked(agent)); break;   // TooOld: installing again updates it
    case AS::ChatAction::SignIn: started = AgentStatus::SignIn(agent); break;
    case AS::ChatAction::Cancel:
#ifdef AGENT_STATUS_CANCEL_SIGN_IN   // worker A: closes the sign-in console, back to « Pas connecté »
        started = AgentStatus::CancelSignIn(agent);
#endif
        break;
    case AS::ChatAction::ManualInstall: {   // « Installer moi-même ↗ »: the official page (its command to paste is there)
        const auto manual = AgentStatus::ManualInstall(agent);
        started = manual.url && reinterpret_cast<INT_PTR>(ShellExecuteW(window, L"open", manual.url, nullptr, nullptr, SW_SHOWNORMAL)) > 32;
        break;
    }
    default: return;
    }
    static constexpr const wchar_t* names[] = {L"none", L"install", L"update", L"sign-in", L"cancel sign-in", L"retry", L"manual install page"};   // i18n-exempt: log words (AGENTS settings …)
    Log(std::wstring(L"AGENTS settings ") + names[static_cast<int>(action)] + L" " + AS::ChatAppName(app) + (started ? L": started" : L": refused (busy, not applicable or not available)"));
}
void AnnotationOpenFolder() {
    const std::wstring root = Ann::Store().Root();
    Ann::Run([] { Ann::Store().EnsureRoot(); }, [root] { ShellExecuteW(window, L"open", root.c_str(), nullptr, nullptr, SW_SHOWNORMAL); Log(L"ANNOTATE store folder opened"); });
}
void AnnotationCount(std::function<void(std::size_t)> done) {
    auto n = std::make_shared<std::size_t>(0);
    Ann::Run([n] { for (bool drafts : {false, true}) { BrowserAnnotations::ListQuery q; q.drafts = drafts; *n += Ann::Store().List(q).entries.size(); } }, [n, done] { if (done) done(*n); });
}
void AnnotationClearAll(std::function<void(bool, std::size_t)> done) {
    struct Out { bool ok = true; std::size_t removed = 0; };
    auto out = std::make_shared<Out>();
    Ann::Run([out] {
            std::lock_guard<std::mutex> g(Ann::writeLock); std::set<std::string> sites;
            for (bool drafts : {false, true}) { BrowserAnnotations::ListQuery q; q.drafts = drafts; for (const auto& e : Ann::Store().List(q).entries) { sites.insert(e.annotation.siteKey); ++out->removed; } }
            for (const auto& site : sites) if (!Ann::Store().DeleteSite(site).ok()) out->ok = false;
        },
        [out, done] {
            using namespace Ann;
            Log(L"ANNOTATE all annotations cleared: " + std::to_wstring(out->removed) + (out->ok ? L"" : L" (some folders could not be removed)"));
            sessionIds.clear(); seenWorking.clear(); seenDone.clear(); seenRead.clear(); seenUnhandled.clear(); last.reset(); historyEntries.clear(); trashed.clear(); thumbs.clear(); lates.clear(); claimed = 0;
            if (uiReady) { if (launcher.CardVisible()) { launcher.HideCard(); card = Card::None; } if (launcher.Visible()) ShowLauncher(); if (launcher.HistoryVisible()) LoadHistory(false, false); }
            SyncWatch();
            if (done) done(out->ok, out->removed);
        });
}
bool AnnotationsCompiled() { return true; }
void AnnotationSettingsChanged() { Ann::LauncherSync(); }
// Page menu (PageContextMenu / ShowPageMenu): « Annoter l'élément », « Annoter la sélection », Markdown.
constexpr int AnnotateElementItem = 0x10010, AnnotateSelectionItem = 0x10011, AnnotateMarkdownItem = 0x10012;
void AnnotationMenuItems(const std::shared_ptr<Tab>& tab, ICoreWebView2ContextMenuRequestedEventArgs* args, std::vector<CM::Item>& items) {
    Ann::menu = {};
    if (!Ann::Available(tab, nullptr)) return;
    Com<ICoreWebView2ContextMenuTarget> target; BOOL has = FALSE; LPWSTR text = nullptr;
    if (SUCCEEDED(args->get_ContextMenuTarget(&target.p)) && target.p && SUCCEEDED(target->get_HasSelection(&has)) && has && SUCCEEDED(target->get_SelectionText(&text)) && text) Ann::menu.text = text;
    CoTaskMemFree(text);
    POINT at{}; args->get_Location(&at);
    Ann::menu.valid = true; Ann::menu.at = at; Ann::menu.selection = !Ann::menu.text.empty();
    std::vector<CM::Item> add{CM::Separator(), CM::Command(AnnotateElementItem, TR(annotation_menu_element))};
    add.back().shortcut = TR(key_ctrl_shift_a);
    if (Ann::menu.selection) add.push_back(CM::Command(AnnotateSelectionItem, TR(annotation_menu_selection)));
    if (Ann::last) add.push_back(CM::Command(AnnotateMarkdownItem, TR(annotation_menu_copy_last)));
    auto lastGroup = std::find_if(items.rbegin(), items.rend(), [](const CM::Item& m) { return m.separator; });
    auto where = lastGroup == items.rend() ? items.end() : std::prev(lastGroup.base());
    items.insert(where, add.begin(), add.end());
}
void AnnotationMenuChoice(const std::shared_ptr<Tab>& tab, int choice) {
    if (choice != AnnotateElementItem && choice != AnnotateSelectionItem && choice != AnnotateMarkdownItem) return;
    const auto m = Ann::menu; Ann::menu = {};
    if (choice == AnnotateMarkdownItem) {
        if (!Ann::last) return;
        const auto md = AnnotationHost::Wide(BrowserAnnotations::ToMarkdown(*Ann::last, Ann::last->annotation.screenshot.present ? Ann::Store().ShotPath(Ann::last->annotation.siteKey, Ann::last->annotation.id) : L""));
        if (CopyToClipboard(md)) Ann::Announce(TR(annotation_copied_markdown));
        return;
    }
    if (!m.valid || !tab || tab != Active()) return;
    Ann::Start st; st.kind = choice == AnnotateSelectionItem ? 2 : 1; st.at = m.at; st.text = m.text;
    Ann::Activate(st);
}
#include "chat-host.h"
#ifndef BROWSER_TON_AGENT_SCREEN
// Stand-in until worker B's « Ton agent » screen lands (src/ton-agent-entry.h: B defines OpenTonAgent and
// BROWSER_TON_AGENT_SCREEN): the chat panel, whose own block shows the agents and their actions.
void OpenTonAgent(TonAgentFrom from) {
    Log(std::wstring(L"AGENTS « Ton agent » asked (") + (from == TonAgentFrom::Band ? L"band" : from == TonAgentFrom::Chat ? L"chat" : L"settings") + L"): screen not built yet, the chat panel instead");
    if (!Chat::Open()) Chat::Toggle(false);
}
#endif
#else
// Unstamped fixtures (tests that include main.cpp) and --channel release builds: no annotation mode.
float ChatInsetDip() { return 0; }
void ChatNavigationCompleted(const std::shared_ptr<Tab>&) {}
void ChatShutdown() {}
void AnnotationStartup() {}
void AnnotationShutdown() {}
bool AnnotationModeActive() { return false; }
bool AnnotationBandSliding() { return false; }
bool AnnotationSurfaceFocus() { return false; }
void AnnotationAccentChanged() {}
void AnnotationSyncChrome() {}
void AnnotationLayout() {}
void AnnotationWindowState() {}
void AnnotationNavigation(const std::shared_ptr<Tab>&) {}
void AnnotationTabChanged() {}
void AnnotationTabSleeping(const std::shared_ptr<Tab>&) {}
bool AnnotationPageMessage(const std::shared_ptr<Tab>&, ICoreWebView2WebMessageReceivedEventArgs*) { return false; }
bool AnnotationCommand(int) { return false; }
bool AnnotationHistoryAvailable() { return false; }
bool AnnotationsCompiled() { return false; }
std::wstring AnnotationStoreRoot() { return L""; }
void AnnotationOpenConnect(bool) {}
void AnnotationOpenTutorial(bool) {}
bool ChatAgentsAvailable() { return false; }
AnnotationSettings::ChatAgents ChatAgentsStatus() { return {}; }
void ChatAgentsWatch(std::function<void()>) {}
void ChatAgentsRefresh() {}
void ChatAgentAct(AnnotationSettings::ChatApp, AnnotationSettings::ChatAction) {}
void ChatAgentsStartup() {}    // « Ton agent » (worker A): no chat, no agents' status
void ChatAgentsShutdown() {}
void AnnotationOpenFolder() {}
void AnnotationCount(std::function<void(std::size_t)> done) { if (done) done(0); }
void AnnotationClearAll(std::function<void(bool, std::size_t)> done) { if (done) done(true, 0); }
void AnnotationSettingsChanged() {}
void AnnotationMenuItems(const std::shared_ptr<Tab>&, ICoreWebView2ContextMenuRequestedEventArgs*, std::vector<CM::Item>&) {}
void AnnotationMenuChoice(const std::shared_ptr<Tab>&, int) {}
#endif
