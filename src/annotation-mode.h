#pragma once
// Annotation mode — browser integration (T3, V1). Included once by src/main.cpp, after the
// page menu (it uses the host's globals: tabs, chrome, accessibility, Log, Layout…).
// Contract: plan-annotations-mcp-local/design/annotations-v1-brief.md (+ plan §1.3, §4, §6, §8).
//
// Budget « mode jamais activé = zéro coût »: until the first activation nothing here exists —
// no script, listener, window, thread, timer, folder watch or file access. The toolbar button
// is painted by the chrome like its neighbours; Ctrl+Maj+A, the button, the page menu and the
// launcher are the only entries. At the first activation: one message-only window (to receive
// thread-pool results), the store object, and a one-off purge + projects read off the UI thread.
//
//   activation   ExecuteScript(page script) in the ACTIVE tab only (src/annotation-script.h);
//                the page moves down 32 DIP under the « Mode Annotation » band; launcher shown.
//   selection    one page message, validated (tab active + mode active + Source + navStamp +
//                nonce + <= 64 KiB + schema: src/annotation-host.h) -> native popover at once;
//                the frame is hidden, CapturePreview(PNG) taken, then cropped/reduced/encoded on
//                the thread pool (src/annotation-capture.h) -> thumbnail in the popover.
//   submit       Store::WriteAnnotation on the thread pool (never on the UI thread) -> card.
//   exit         Échap, « Quitter », the button, NavigationStarting/SourceChanged, a tab change,
//                the tab put to sleep: the script removes itself (listeners, shadow root, state).
//   agents       readers\ and states are read only when the card or the onboarding is open;
//                ReadDirectoryChangesW runs only while one of them is open.
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <set>

#ifdef BROWSER_ANNOTATIONS
#include "annotations.h"
#include "annotation-ui.h"
#include "annotation-script.h"
#include "annotation-host.h"
#include "annotation-capture.h"

namespace Ann {
namespace BA = BrowserAnnotations; namespace AUI = NativeChrome::Annotations; namespace AH = AnnotationHost;

struct Start { int kind = 0; POINT at{}; std::wstring text; };   // 0 none, 1 point (WebView px), 2 text selection
enum class Cap { None, Pending, Ready, Failed };
struct Session {
    bool active = false, injected = false, frozen = false, waitingSubmit = false, saving = false, captureWanted = false;
    std::weak_ptr<Tab> tab; std::uint64_t navStamp = 0; std::wstring source; std::string nonce; double zoom = 1;
    std::uint64_t serial = 0;                 // one per selection: late async results are dropped
    AH::Selection sel; BA::Annotation pending; std::string origin;
    Cap cap = Cap::None; std::string png; double shotScale = 1; std::string shotCrop;
    AUI::NoteSubmission submission; std::string projectId;
    std::wstring draft;                       // kept when the popover is cancelled (« Reprendre la note »)
} s;
std::uint64_t serials = 0;                    // never reset: a new session never matches an old result
AUI::Banner banner; AUI::NotePopover note; AUI::Launcher launcher; AUI::Onboarding onboarding;
bool uiReady = false, prepared = false, projectsLoaded = false;
std::unique_ptr<BA::Store> store; std::mutex writeLock;
std::vector<BA::Project> projects, proposed;  // proposed: new projects for an origin, saved with the first annotation
std::optional<BA::Entry> last;                // last annotation added in this session (« Copier en Markdown »)
std::set<std::string> seen;                   // resolutions already shown as a card
std::int64_t startedAt = 0;
AH::AgentStatus status; std::vector<BA::Reader> readers; std::size_t openCount = 0;
bool statusBusy = false, statusDirty = false, statusForToggle = false;
enum class Card { None, Working, NoAgent, Added, Resolved } card = Card::None; BA::Entry cardEntry;
bool verifying = false; std::int64_t verifyFrom = 0;
std::wstring loggedOnce;
HWND post = nullptr; constexpr UINT JobDone = WM_APP + 1, WatchChanged = WM_APP + 2; constexpr UINT_PTR HideTimer = 1;
struct Watch { HANDLE thread = nullptr, stop = nullptr; std::wstring root; } watch; std::atomic<bool> watchSignal{false};
struct Menu { bool valid = false, selection = false; POINT at{}; std::wstring text; } menu;

bool Reduced() { return motion.ReducedMotion(); }
void Announce(const std::wstring& text) { accessibility.Notify('N', text.c_str(), L"Annotation"); Log(L"ANNOTATE " + text); }
void LogOnce(const std::wstring& what) { if (loggedOnce.find(what) != std::wstring::npos) return; loggedOnce += what + L";"; Log(L"ANNOTATE ignored page message: " + what); }
BA::Store& Store() { if (!store) store = std::make_unique<BA::Store>(BA::StoreRootForDataDir(dataDir)); return *store; }
std::string ChannelName() {
#ifdef BROWSER_CHANNEL_RELEASE
    return "live";
#else
    return "preview";
#endif
}

// ---------------------------------------------------------------- thread pool -> UI thread
struct Job { std::function<void()> work, done; };
void Run(std::function<void()> work, std::function<void()> done);
void RefreshStatus(bool forToggle);
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
    try { if (j->work) j->work(); } catch (...) {}
    if (!post || !PostMessageW(post, JobDone, 0, reinterpret_cast<LPARAM>(j))) delete j;
}
void Run(std::function<void()> work, std::function<void()> done) {
    auto* j = new Job{std::move(work), std::move(done)};
    if (!EnsurePost() || !TrySubmitThreadpoolCallback(RunJob, j, nullptr)) { Log(L"ANNOTATE thread pool unavailable"); delete j; }
}

// ---------------------------------------------------------------- availability, chrome
std::shared_ptr<Tab> ModeTab() { return s.active ? s.tab.lock() : nullptr; }
bool Available(const std::shared_ptr<Tab>& t, std::wstring* why) {
    auto no = [&](const wchar_t* w) { if (why) *why = w; return false; };
    if (privateWindow) return no(L"indisponible en navigation privée");
    if (halted || !t || t->closed || !t->view.p || !t->controller.p) return no(L"aucune page");
    if (t->home) return no(L"ouvrez une page Web pour l'annoter");
    if (!t->https.shown.empty() || !t->errorPage.shown.empty() || t->sandbox || IsSandboxUri(t->uri)) return no(L"indisponible sur une page interne");
    if (t->uri.rfind(L"http://", 0) != 0 && t->uri.rfind(L"https://", 0) != 0) return no(L"seules les pages http et https s'annotent");
    if (t->pageFullscreen) return no(L"indisponible en plein écran");
    return true;
}
void SyncChrome() {
    chrome.annotateShown = true; chrome.annotateActive = s.active;
    std::wstring why; const bool ok = Available(Active(), &why);
    chrome.SetEnabled('N', ok || s.active);
    chrome.annotateTip = s.active ? L"Quitter le mode Annotation (Ctrl+Maj+A)" : ok ? L"Annoter (Ctrl+Maj+A)" : L"Annoter — " + why;
    if (!chrome.paintAnnotate) chrome.paintAnnotate = [](ID2D1RenderTarget* t, ID2D1Factory* f, ID2D1SolidColorBrush* b, D2D1_RECT_F box, NativeChrome::Motion::InteractionVisual iv, bool focus) {
        AUI::ToolbarVisual v; v.active = s.active; v.hover = chrome.Enabled('N') ? iv.hover : 0; v.press = iv.press; v.focus = focus; v.keyboard = keyboardModality; v.reduced = Reduced();
        AUI::PaintToolbarButton(t, f, b, box, v);
    };
}
RECT BandRect() { RECT c{}; GetClientRect(window, &c); return {0, S(chrome.ContentTop() - chrome.annotationBand), c.right, S(chrome.ContentTop())}; }
RECT PageClient(const std::shared_ptr<Tab>& t) {
    if (t && !IsRectEmpty(&t->placed)) return t->placed;
    RECT c{}; GetClientRect(window, &c); c.top = S(chrome.ContentTop()); return c;
}
RECT PageScreen(const std::shared_ptr<Tab>& t) { RECT r = PageClient(t); MapWindowPoints(window, nullptr, reinterpret_cast<POINT*>(&r), 2); return r; }
RECT ClientScreen() { RECT r{}; GetClientRect(window, &r); MapWindowPoints(window, nullptr, reinterpret_cast<POINT*>(&r), 2); return r; }
void Place() {
    if (!s.active) return;
    banner.Move(BandRect());
    launcher.Place(PageClient(ModeTab()));
}

// ---------------------------------------------------------------- page script
void Cmd(const std::shared_ptr<Tab>& t, const std::wstring& op, const std::wstring& arg = L"null") {
    if (!t || !t->view.p || t->closed || halted) return;
    auto cb = Callback<ICoreWebView2ExecuteScriptCompletedHandler, HRESULT, LPCWSTR>([](HRESULT, LPCWSTR) -> HRESULT { return S_OK; });
    t->view->ExecuteScript(AUI::PageCommand(AH::Wide(s.nonce.empty() ? std::string("-") : s.nonce), op, arg).c_str(), cb); cb->Release();
}
std::wstring Params(const std::shared_ptr<Tab>& t, const Start& st) {
    namespace SJ = StrictJson; SJ::Value p = SJ::Value::Obj();
    p.Set("nonce", SJ::Value::Str(s.nonce)); p.Set("navStamp", SJ::Value::Int(std::int64_t(t->navStamp)));
    SJ::Value styles = SJ::Value::Arr(); for (const auto& n : BA::AllowedStyleProperties()) styles.Push(SJ::Value::Str(n));
    SJ::Value attrs = SJ::Value::Arr(); for (const auto& n : BA::AllowedAttributes()) attrs.Push(SJ::Value::Str(n));
    p.Set("styles", std::move(styles)); p.Set("attrs", std::move(attrs)); p.Set("reduced", SJ::Value::Bool(Reduced()));
    const auto a = NativeChrome::Design::Accent::Get();
    p.Set("accent", SJ::Value::Str(std::to_string(int(std::lround(a.r * 255))) + "," + std::to_string(int(std::lround(a.g * 255))) + "," + std::to_string(int(std::lround(a.b * 255)))));
    SJ::Value start = SJ::Value::Obj();
    start.Set("kind", SJ::Value::Str(st.kind == 1 ? "point" : st.kind == 2 ? "selection" : "none"));
    start.Set("x", SJ::Value::Int(st.at.x)); start.Set("y", SJ::Value::Int(st.at.y));
    start.Set("text", SJ::Value::Str(BA::CleanText(AH::Utf8(st.text), BA::MaxTextChars, true)));
    p.Set("start", std::move(start));
    return AH::Wide(SJ::Serialize(p));
}
void SendStart(const std::shared_ptr<Tab>& t, const Start& st) {
    if (st.kind == 1) Cmd(t, L"point", L"{\"x\":" + std::to_wstring(st.at.x) + L",\"y\":" + std::to_wstring(st.at.y) + L"}");
    else if (st.kind == 2) Cmd(t, L"selection", AH::Wide(StrictJson::Serialize(StrictJson::Value::Str(BA::CleanText(AH::Utf8(st.text), BA::MaxTextChars, true)))));
}

// ---------------------------------------------------------------- folder watch (card / onboarding open only)
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
void StartWatch() {
    if (watch.thread || !EnsurePost()) return;
    watch.root = Store().Root(); watch.stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    watch.thread = CreateThread(nullptr, 0, WatchMain, &watch, 0, nullptr);
    if (!watch.thread) { CloseHandle(watch.stop); watch.stop = nullptr; return; }
    Log(L"ANNOTATE watching the store (card/onboarding open)");
}
void StopWatch() {
    if (!watch.thread) return;
    SetEvent(watch.stop); WaitForSingleObject(watch.thread, 2000);
    CloseHandle(watch.thread); CloseHandle(watch.stop); watch.thread = watch.stop = nullptr;
    Log(L"ANNOTATE store watch stopped");
}
bool CardOpen() { return launcher.CardVisible() && (card == Card::Working || card == Card::Resolved || card == Card::NoAgent); }
void StopWatchIfIdle() { if (!onboarding.Visible() && !CardOpen()) StopWatch(); }

// ---------------------------------------------------------------- projects
std::vector<AUI::Project> UiProjects() {
    std::vector<AUI::Project> out;
    for (const auto* list : {&projects, &proposed}) for (const auto& p : *list) out.push_back({AH::Wide(p.id), AH::Wide(p.label), p.origins.empty() ? L"" : AH::Wide(p.origins.front())});
    return out;
}
const BA::Project* FindProject(const std::string& id) {
    for (const auto* list : {&projects, &proposed}) for (const auto& p : *list) if (p.id == id) return &p;
    return nullptr;
}
// The project of the current origin (an existing rule), else a proposed one (saved with the first write).
std::string ProjectFor(const std::string& origin, const std::wstring& title) {
    if (const int i = AH::ProjectForOrigin(projects, origin); i >= 0) return projects[size_t(i)].id;
    if (const int i = AH::ProjectForOrigin(proposed, origin); i >= 0) return proposed[size_t(i)].id;
    std::vector<BA::Project> all = projects; all.insert(all.end(), proposed.begin(), proposed.end());
    proposed.push_back(AH::NewProjectFor(origin, title, all));
    return proposed.back().id;
}
void Prepare() {
    if (prepared) return; prepared = true; startedAt = BA::SystemNow();
    // Retention at the first use of the session (never on a timer, nothing before the first activation).
    auto loaded = std::make_shared<std::vector<BA::Project>>();
    Run([loaded] { std::lock_guard<std::mutex> g(writeLock); const auto r = Store().Purge(); (void)r; *loaded = Store().LoadProjects(); },
        [loaded] {
            projects = *loaded; projectsLoaded = true;
            proposed.erase(std::remove_if(proposed.begin(), proposed.end(), [](const BA::Project& p) { return AH::ProjectForOrigin(projects, p.origins.empty() ? "" : p.origins.front()) >= 0; }), proposed.end());
            if (s.active && note.Visible()) { const auto id = ProjectFor(s.origin, ModeTab() ? ModeTab()->title : L""); if (s.projectId.empty() || !FindProject(s.projectId)) s.projectId = id; note.SetProjects(UiProjects(), AH::Wide(s.projectId)); }
        });
}

// ---------------------------------------------------------------- cards, onboarding, status
std::wstring Label(const std::string& projectId) { const auto* p = FindProject(projectId); return AH::Wide(p ? p->label : projectId); }
AUI::CardContent CardFor(const BA::Entry& e, Card kind) {
    AUI::CardContent c; const auto now = BA::SystemNow();
    c.project = Label(e.annotation.project);
    if (kind == Card::Working) {
        c.kind = AUI::CardKind::Working; c.agent = e.state.claimedBy ? AH::Wide(e.state.claimedBy->agent) : L"un agent";
        c.since = AH::SinceText(now - AH::ClaimTime(e)); c.note = AH::Wide(e.annotation.note);
    } else {
        c.kind = AUI::CardKind::Resolved; c.headline = AH::OutcomeHeadline(e);
        c.agent = e.state.resolution ? AH::Wide(e.state.resolution->by) : L"un agent";
        c.since = e.state.resolution ? AH::AgoShort(now - e.state.resolution->at) : L"";
        c.note = e.state.resolution ? AH::Wide(e.state.resolution->summary) : AH::Wide(e.annotation.note);
    }
    c.monogram = AH::Monogram(c.agent);
    return c;
}
void ShowEntryCard(const BA::Entry& e, Card kind) {
    card = kind; cardEntry = e; launcher.ShowCard(CardFor(e, kind));
    if (kind == Card::Resolved) seen.insert(e.annotation.id);
    StartWatch();
}
AUI::VerifyStatus VerifyFrom() {
    AUI::VerifyStatus v; const auto now = BA::SystemNow();
    std::vector<BA::Reader> sorted = readers;
    std::sort(sorted.begin(), sorted.end(), [](const BA::Reader& a, const BA::Reader& b) { return a.lastReadAt > b.lastReadAt; });
    for (const auto& r : sorted) {
        if (!v.read && r.lastReadAt >= verifyFrom - 5) { v.read = true; v.reader = AH::Wide(r.label.empty() ? r.client : r.label); v.when = AH::AgoText(now - r.lastReadAt);
            v.detail = openCount == 1 ? L"1 ouverte" : std::to_wstring(openCount) + L" ouvertes"; }
        if (v.agents.size() < 4) v.agents.push_back({AH::Monogram(AH::Wide(r.label.empty() ? r.client : r.label)), AH::Wide(r.label.empty() ? r.client : r.label),
                                                    L"Dernière lecture " + AH::AgoText(now - r.lastReadAt), now - r.lastReadAt < 7 * 86400});
    }
    return v;
}
std::wstring BridgePath() { return base + L"\\browser-annotations-mcp.exe"; }
void OpenOnboarding() {
    auto t = ModeTab() ? ModeTab() : Active();
    const std::string origin = t ? AH::OriginOf(t->uri) : std::string();
    std::string pid = s.projectId; if (pid.empty() && !origin.empty()) pid = ProjectFor(origin, t ? t->title : L"");
    AUI::OnboardingSetup setup; setup.projectId = AH::Wide(pid); setup.projectLabel = Label(pid);
    setup.bridgePath = BridgePath(); setup.mcpbName.clear(); setup.mcpbSize.clear();   // no .mcpb in V1: manual configuration
    setup.serverName = ChannelName() == "live" ? L"browser-annotations" : L"browser-annotations-preview";
    if (GetFileAttributesW(setup.bridgePath.c_str()) == INVALID_FILE_ATTRIBUTES) Log(L"ANNOTATE bridge not found next to browser.exe: " + setup.bridgePath);
    verifyFrom = BA::SystemNow();
    if (onboarding.Show(setup, ClientScreen(), Reduced())) { StartWatch(); RefreshStatus(false); }
}
void ApplyStatus() {
    const bool forToggle = statusForToggle; statusForToggle = false;
    if (launcher.Visible()) launcher.Show(status.claimed ? AUI::LauncherState::Working : AUI::LauncherState::Rest, status.claimed, Reduced());
    if (onboarding.Visible()) onboarding.SetStatus(VerifyFrom());
    if (launcher.CardVisible() && (card == Card::Working || card == Card::Resolved)) {
        // Live while the card is open: « Prise par » -> « Résolue par » as soon as the agent resolves.
        if (status.working) { if (card != Card::Working || cardEntry.annotation.id != status.working->annotation.id) ShowEntryCard(*status.working, Card::Working); }
        else if (status.resolved) { if (card != Card::Resolved || cardEntry.annotation.id != status.resolved->annotation.id) ShowEntryCard(*status.resolved, Card::Resolved); }
    }
    if (forToggle) {
        if (status.working) ShowEntryCard(*status.working, Card::Working);
        else if (status.resolved) ShowEntryCard(*status.resolved, Card::Resolved);
        else OpenOnboarding();
    } else if (s.active && !launcher.CardVisible() && status.resolved && !seen.count(status.resolved->annotation.id) && !note.Visible()) {
        ShowEntryCard(*status.resolved, Card::Resolved);   // a resolution not yet seen in this session
    }
}
void RefreshStatus(bool forToggle) {
    if (forToggle) statusForToggle = true;
    if (statusBusy) { statusDirty = true; return; }
    statusBusy = true;
    struct Out { std::vector<BA::Entry> entries; std::vector<BA::Reader> readers; };
    auto out = std::make_shared<Out>();
    Run([out] { BA::ListQuery q; q.newestFirst = true; q.limit = 500; out->entries = Store().List(q).entries; out->readers = Store().ListReaders(); },
        [out] {
            statusBusy = false;
            status = AH::Summarize(out->entries, startedAt - 24 * 3600); readers = out->readers;
            openCount = size_t(std::count_if(out->entries.begin(), out->entries.end(), [](const BA::Entry& e) { return e.state.status == BA::Status::Open; }));
            ApplyStatus();
            if (statusDirty) { statusDirty = false; RefreshStatus(false); }
        });
}

// ---------------------------------------------------------------- selection, capture, write
void Exit(const wchar_t* why, bool pageChanged = false);
void Resume() {
    auto t = ModeTab(); if (!t) return;
    s.frozen = false; s.cap = Cap::None; s.png.clear(); s.waitingSubmit = s.saving = false; ++serials; s.serial = serials;
    Cmd(t, L"resume");
    if (t->controller.p) t->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
}
void Capture(const std::shared_ptr<Tab>& t, std::uint64_t serial);
void DoSubmit() {
    auto t = ModeTab(); if (!t) return;
    s.waitingSubmit = false; s.saving = true;
    BA::Annotation a = s.pending; std::wstring typed = s.submission.note;
    while (!typed.empty() && iswspace(typed.back())) typed.pop_back();   // a trailing Entrée/newline is not part of the note
    a.note = AH::Utf8(typed); a.project = AH::Utf8(s.submission.projectId);
    std::optional<BA::Project> newProject; std::string origin = s.origin;
    for (const auto& p : proposed) if (p.id == a.project) newProject = p;
    auto png = (s.submission.includeShot && s.cap == Cap::Ready && !s.png.empty()) ? std::make_shared<std::string>(s.png) : nullptr;
    if (png) { a.screenshot.scale = s.shotScale; a.screenshot.crop = s.shotCrop; }
    struct Out { BA::Outcome result; BA::Entry entry; std::vector<BA::Reader> readers; std::vector<BA::Project> projects; };
    auto out = std::make_shared<Out>(); const auto serial = s.serial;
    Run([a, png, newProject, origin, out]() mutable {
            std::lock_guard<std::mutex> g(writeLock);
            auto list = Store().LoadProjects();
            bool changed = false;
            auto it = std::find_if(list.begin(), list.end(), [&](const BA::Project& p) { return p.id == a.project; });
            if (it == list.end() && newProject && list.size() < BA::MaxProjects) { list.push_back(*newProject); it = list.end() - 1; changed = true; }
            if (it != list.end() && !origin.empty() && std::find(it->origins.begin(), it->origins.end(), origin) == it->origins.end() && it->origins.size() < BA::MaxOriginsPerProject) { it->origins.push_back(origin); changed = true; }
            if (changed) { const auto saved = Store().SaveProjects(list); if (!saved.ok()) { out->result = saved; return; } }
            out->projects = list;
            out->result = Store().WriteAnnotation(a, png.get(), false);
            if (out->result.ok()) { Store().Read(a.project, a.id, out->entry); out->readers = Store().ListReaders(); }
        },
        [out, serial] {
            if (!out->projects.empty()) { projects = out->projects; projectsLoaded = true;
                proposed.erase(std::remove_if(proposed.begin(), proposed.end(), [](const BA::Project& p) { return std::any_of(projects.begin(), projects.end(), [&](const BA::Project& q) { return q.id == p.id; }); }), proposed.end()); }
            const bool current = s.active && s.serial == serial;
            if (!out->result.ok()) {
                Log(L"ANNOTATE write failed: " + AH::Wide(out->result.message));
                if (current) { s.saving = false; note.Failed(L"Impossible d'enregistrer l'annotation (" + AH::Wide(out->result.message) + L"). Votre note est conservée ; réessayez."); }
                return;
            }
            last = out->entry; readers = out->readers;
            Log(L"ANNOTATE written " + AH::Wide(out->entry.annotation.project + "/" + out->entry.annotation.id) + (out->entry.annotation.screenshot.present ? L" with shot" : L" without shot"));
            if (!current) return;
            s.draft.clear(); note.Saved();
            const bool agent = std::any_of(readers.begin(), readers.end(), [&](const BA::Reader& r) { return r.project.empty() || r.project == out->entry.annotation.project; });
            AUI::CardContent c; c.kind = agent ? AUI::CardKind::Added : AUI::CardKind::NoAgent; c.project = Label(out->entry.annotation.project);
            card = agent ? Card::Added : Card::NoAgent; cardEntry = out->entry; launcher.ShowCard(c);
            if (!agent) StartWatch();
            Resume();
        });
}
void OnHidden(const std::shared_ptr<Tab>& t, std::int64_t serial) {
    if (!s.captureWanted || serial != std::int64_t(s.serial)) return;
    if (post) KillTimer(post, HideTimer);
    s.captureWanted = false; Capture(t, s.serial);
}
void Capture(const std::shared_ptr<Tab>& t, std::uint64_t serial) {
    IStream* stream = nullptr;
    auto fail = [serial](const wchar_t* why) {
        if (s.active && s.serial == serial) { s.cap = Cap::Failed; note.SetShot(AUI::CaptureState::Unavailable, nullptr); Log(std::wstring(L"ANNOTATE capture unavailable: ") + why); if (s.waitingSubmit) DoSubmit(); }
    };
    if (!t || !t->view.p || FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream))) { fail(L"stream"); Cmd(t, L"show"); return; }
    const auto rect = s.sel.target.rect; const double vw = s.sel.target.viewport.width, vh = s.sel.target.viewport.height;
    const unsigned tw = unsigned(S(AUI::NoteSpec::thumbW)), th = unsigned(S(AUI::NoteSpec::thumbH));
    std::weak_ptr<Tab> weak = t; const ULONGLONG started = GetTickCount64();
    auto cb = Callback<ICoreWebView2CapturePreviewCompletedHandler, HRESULT>([weak, stream, serial, rect, vw, vh, tw, th, started, fail](HRESULT hr) -> HRESULT {
        auto tab = weak.lock(); std::string bytes;
        if (SUCCEEDED(hr)) { HGLOBAL mem = nullptr; STATSTG st{}; if (SUCCEEDED(GetHGlobalFromStream(stream, &mem)) && SUCCEEDED(stream->Stat(&st, STATFLAG_NONAME))) { if (const void* d = GlobalLock(mem)) { bytes.assign(static_cast<const char*>(d), size_t(st.cbSize.QuadPart)); GlobalUnlock(mem); } } }
        stream->Release();
        Cmd(tab, L"show");   // the frozen frame comes back right after the shot
        if (!s.active || s.serial != serial) return S_OK;
        if (bytes.empty()) { fail(L"CapturePreview"); return S_OK; }
        auto result = std::make_shared<AnnotationCapture::Result>(); auto input = std::make_shared<std::string>(std::move(bytes));
        Run([result, input, rect, vw, vh, tw, th] { *result = AnnotationCapture::Process(*input, rect, vw, vh, tw, th); },
            [result, serial, started, fail] {
                if (!s.active || s.serial != serial) return;
                if (!result->ok) { fail(AH::Wide(result->why).c_str()); return; }
                s.png = result->png; s.shotScale = result->scale; s.shotCrop = result->crop; s.cap = Cap::Ready;
                AUI::Shot shot; shot.width = result->thumbW; shot.height = result->thumbH; shot.pixels = result->thumb; shot.mark = {result->markL, result->markT, result->markR, result->markB};
                note.SetShot(AUI::CaptureState::Ready, shot.width ? &shot : nullptr);
                Log(L"ANNOTATE capture " + std::to_wstring(result->width) + L"x" + std::to_wstring(result->height) + L" " + std::to_wstring(result->png.size()) + L" bytes in " + std::to_wstring(GetTickCount64() - started) + L" ms");
                if (s.waitingSubmit) DoSubmit();
            });
        return S_OK; });
    const HRESULT hr = t->view->CapturePreview(COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG, stream, cb); cb->Release();
    if (FAILED(hr)) { stream->Release(); Cmd(t, L"show"); fail(L"CapturePreview refused"); }
}
std::wstring Clip(std::wstring t, size_t n) { for (auto& c : t) if (c == L'\n' || c == L'\r' || c == L'\t') c = L' '; if (t.size() > n) { t.resize(n); t += L"…"; } return t; }
void OnSelect(const std::shared_ptr<Tab>& t, AH::Selection sel) {
    ++serials; s.serial = serials; s.frozen = true; s.cap = Cap::Pending; s.png.clear(); s.waitingSubmit = s.saving = false; s.sel = sel;
    BA::Annotation a;
    a.source.channel = ChannelName();
#ifdef BROWSER_VERSION_TEXT
    a.source.version = BROWSER_VERSION_TEXT;
#endif
    a.source.storeTag = AH::StoreTag(dataDir); a.source.tabSerial = t->serial; a.source.navStamp = std::int64_t(s.navStamp);
    a.source.url = AH::Utf8(t->uri); a.source.title = AH::Utf8(t->title); a.source.frameKind = sel.frameKind;
    a.target = sel.target; a.target.viewport.zoom = s.zoom;
    BA::Sanitize(a); s.pending = a;
    if (s.projectId.empty() || !FindProject(s.projectId)) s.projectId = ProjectFor(s.origin, t->title);
    // Screen geometry: CSS px x devicePixelRatio in the WebView's pixels.
    const RECT page = PageScreen(t); const double dpr = sel.target.viewport.devicePixelRatio; const auto& r = sel.target.rect;
    RECT target{page.left + LONG(std::lround(r.x * dpr)), page.top + LONG(std::lround(r.y * dpr)), page.left + LONG(std::lround((r.x + r.width) * dpr)), page.top + LONG(std::lround((r.y + r.height) * dpr))};
    AUI::NoteRequest req; req.draft = s.draft; req.projects = UiProjects(); req.projectId = AH::Wide(s.projectId); req.capture = AUI::CaptureState::Pending;
    {   bool redacted = false; std::wstring shown = AH::Wide(BA::RedactUrl(a.source.url, &redacted));
        if (const auto at = shown.find(L"://"); at != std::wstring::npos) shown = shown.substr(at + 3);
        req.context.page = Clip(shown, 90);
        const auto& path = a.target.path; std::wstring el;
        for (size_t i = path.size() > 2 ? path.size() - 2 : 0; i < path.size(); ++i) el += (el.empty() ? L"" : L" > ") + AH::Wide(path[i]);
        if (sel.frameKind == "cross-origin") el += L" (iframe d'une autre origine)";
        req.context.element = Clip(el.empty() ? AH::Wide(a.target.tag) : el, 90);
        const std::wstring text = a.target.selectionText ? AH::Wide(*a.target.selectionText) : AH::Wide(a.target.text);
        req.context.text = text.empty() ? L"(aucun)" : L"« " + Clip(text, 80) + L" »";
        req.context.styles = AH::StylesText(a.target.styles);
        const auto plan = AH::PlanCrop(r, a.target.viewport.width, a.target.viewport.height, unsigned(std::lround(a.target.viewport.width * dpr)), unsigned(std::lround(a.target.viewport.height * dpr)));
        req.context.shotLabel = plan.ok ? L"Recadrée · " + std::to_wstring(plan.outW) + L"×" + std::to_wstring(plan.outH) : L"Hors de la zone visible";
    }
    if (!note.Show(req, target, page, sel.keyboard, Reduced())) { Log(L"ANNOTATE popover unavailable"); return; }
    // Hide the frame, let the page paint, then CapturePreview (the host's timer catches a page that never answers).
    s.captureWanted = true; Cmd(t, L"hide", std::to_wstring(s.serial));
    if (EnsurePost()) SetTimer(post, HideTimer, 800, nullptr);
}
void OnSubmit(const AUI::NoteSubmission& sub) {
    if (!s.active) { note.Close(false); return; }
    s.submission = sub; s.projectId = AH::Utf8(sub.projectId);
    if (sub.includeShot && s.cap == Cap::Pending) { s.waitingSubmit = true; return; }   // written when the capture lands
    DoSubmit();
}

// ---------------------------------------------------------------- lifecycle
void EnsureUi() {
    if (uiReady) return; uiReady = true;
    AUI::BannerHooks bh; bh.exit = [] { Exit(L"quit"); }; bh.announce = [](const std::wstring& t) { Announce(t); };
    banner.Init(instance, window, base, bh);
    AUI::NoteHooks nh;
    nh.submit = [](const AUI::NoteSubmission& sub) { OnSubmit(sub); };
    nh.cancel = [](const std::wstring& draft, bool) { s.draft = draft; Resume(); };
    nh.newProject = [] {
        auto t = ModeTab(); if (!t) return;
        std::vector<BA::Project> all = projects; all.insert(all.end(), proposed.begin(), proposed.end());
        proposed.push_back(AH::NewProjectFor(s.origin, t->title, all, true)); s.projectId = proposed.back().id;
        note.SetProjects(UiProjects(), AH::Wide(s.projectId)); Announce(L"Projet " + AH::Wide(proposed.back().label) + L" créé pour cette origine");
    };
    nh.projectChanged = [](const std::wstring& id) { s.projectId = AH::Utf8(id); };
    nh.announce = [](const std::wstring& t) { Announce(t); };
    note.Init(instance, window, base, nh);
    AUI::LauncherHooks lh;
    lh.toggle = [] { if (launcher.CardVisible()) { launcher.HideCard(); card = Card::None; StopWatchIfIdle(); } else RefreshStatus(true); };
    lh.open = [] {
        if (!cardEntry.annotation.id.empty()) { const auto dir = Store().Root() + L"\\" + AH::Wide(cardEntry.annotation.project) + L"\\" + AH::Wide(cardEntry.annotation.id);
            ShellExecuteW(window, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL); }
    };
    lh.later = [] { card = Card::None; StopWatchIfIdle(); };
    lh.connect = [] { launcher.HideCard(); card = Card::None; OpenOnboarding(); };
    lh.dismissed = [](bool) { card = Card::None; StopWatchIfIdle(); };
    lh.focusOut = [] { FocusPage(); };
    lh.announce = [](const std::wstring& t) { Announce(t); };
    launcher.Init(instance, window, base, lh);
    AUI::OnboardingHooks oh;
    oh.copy = [](const std::wstring& text) { if (CopyToClipboard(text)) Log(L"ANNOTATE configuration copied"); };
    oh.verifyStarted = [](AUI::AgentApp) { verifying = true; verifyFrom = BA::SystemNow(); StartWatch(); RefreshStatus(false); };
    oh.verifyStopped = [](AUI::AgentApp) { verifying = false; };
    oh.help = [](AUI::AgentApp) {
        CopyToClipboard(L"Connecteur : " + BridgePath() + L"\r\nMagasin : " + Store().Root() + L"\r\nJournal de Claude Desktop : %APPDATA%\\Claude\\logs\\mcp-server-<nom>.log");
        Announce(L"Diagnostic copié : chemin du connecteur et du magasin");
    };
    oh.agentAction = [](int) { Announce(L"La connexion se gère dans l'app de l'agent"); };
    oh.closed = [](bool) { verifying = false; StopWatchIfIdle(); if (s.active) FocusPage(); };
    oh.announce = [](const std::wstring& t) { Announce(t); };
    onboarding.Init(instance, window, base, oh);
}
void Activate(const Start& st) {
    auto t = Active(); std::wstring why;
    if (!Available(t, &why)) { Announce(L"Annoter : " + why); return; }
    if (s.active) { if (ModeTab() == t) { if (st.kind) SendStart(t, st); return; } Exit(L"tab"); }
    EnsureUi(); EnsurePost(); Store();   // the store object is created here, on the UI thread, before any job uses it
    const std::wstring keepDraft = s.draft;
    s = Session{}; s.active = true; s.tab = t; s.navStamp = t->navStamp; ++serials; s.serial = serials; s.nonce = BA::NewClaimToken();
    if (s.nonce.empty()) { s.active = false; Log(L"ANNOTATE no random source"); return; }
    { LPWSTR src = nullptr; if (SUCCEEDED(t->view->get_Source(&src)) && src) s.source = src; CoTaskMemFree(src); }
    s.origin = AH::OriginOf(t->uri); t->controller->get_ZoomFactor(&s.zoom); s.draft = keepDraft;
    Prepare();
    const std::string nonce = s.nonce;
    auto cb = Callback<ICoreWebView2ExecuteScriptCompletedHandler, HRESULT, LPCWSTR>([nonce](HRESULT hr, LPCWSTR result) -> HRESULT {
        if (!s.active || s.nonce != nonce) return S_OK;
        if (FAILED(hr) || !result || wcscmp(result, L"\"ok\"") != 0) { Log(L"ANNOTATE script injection failed"); Exit(L"injection"); Announce(L"Impossible d'activer le mode Annotation sur cette page"); }
        else s.injected = true;
        return S_OK; });
    const ULONGLONG started = GetTickCount64();
    chrome.annotationBand = NativeChrome::Annotations::BannerSpec::height; SyncChrome(); Layout();   // the band first: the page is measured in its final viewport
    if (FAILED(t->view->ExecuteScript(AUI::PageScriptCall(Params(t, st)).c_str(), cb))) { cb->Release(); s.active = false; chrome.annotationBand = 0; SyncChrome(); Layout(); Log(L"ANNOTATE ExecuteScript refused"); return; }
    cb->Release();
    banner.Show(BandRect(), Reduced());
    launcher.Place(PageClient(t)); launcher.Show(status.claimed ? AUI::LauncherState::Working : AUI::LauncherState::Rest, status.claimed, Reduced());
    RefreshStatus(false);
    Log(L"ANNOTATE mode on (" + std::to_wstring(GetTickCount64() - started) + L" ms to inject request) " + t->uri);
}
void Exit(const wchar_t* why, bool pageChanged) {
    if (!s.active) return;
    auto t = ModeTab(); const bool hadPopover = note.Visible();
    Cmd(t, L"exit");
    if (post) KillTimer(post, HideTimer);
    const std::wstring draft = hadPopover && note.Edit() ? [] { const int n = GetWindowTextLengthW(note.Edit()); std::wstring d(size_t(n) + 1, L'\0'); GetWindowTextW(note.Edit(), d.data(), n + 1); d.resize(size_t(n)); return d; }() : s.draft;
    s = Session{}; s.draft = pageChanged ? std::wstring() : draft; ++serials;
    note.Close(!pageChanged); banner.Hide(); launcher.Hide(); card = Card::None; StopWatchIfIdle();
    chrome.annotationBand = 0; SyncChrome(); Layout();
    if (pageChanged && hadPopover) Announce(L"La page a changé, sélection annulée");
    Log(std::wstring(L"ANNOTATE mode off (") + why + L")");
}
LRESULT CALLBACK PostProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == JobDone) { std::unique_ptr<Job> j(reinterpret_cast<Job*>(l)); if (j->done && !halted) j->done(); return 0; }
    if (m == WatchChanged) { watchSignal = false; if (watch.thread) RefreshStatus(false); return 0; }
    if (m == WM_TIMER && w == HideTimer) { KillTimer(h, HideTimer); if (s.active && s.captureWanted) { s.captureWanted = false; Log(L"ANNOTATE page did not confirm the hidden frame: capturing anyway"); Capture(ModeTab(), s.serial); } return 0; }
    return DefWindowProcW(h, m, w, l);
}
} // namespace Ann

// ---------------------------------------------------------------- host entry points (declared at the top of main.cpp)
bool AnnotationModeActive() { return Ann::s.active; }
bool AnnotationSurfaceFocus() {
    if (!Ann::uiReady) return false;
    HWND f = GetFocus(); if (!f) return false;
    for (HWND w : {Ann::note.Window(), Ann::onboarding.Window(), Ann::launcher.LauncherWindow(), Ann::launcher.CardWindow(), Ann::banner.Window()})
        if (w && (f == w || IsChild(w, f))) return true;
    return false;
}
void AnnotationSyncChrome() { Ann::SyncChrome(); }
void AnnotationLayout() { Ann::Place(); }
void AnnotationNavigation(const std::shared_ptr<Tab>& t) { if (Ann::s.active && Ann::ModeTab() == t) Ann::Exit(L"navigation", true); }
void AnnotationTabChanged() { if (Ann::s.active && Ann::ModeTab() != Active()) Ann::Exit(L"tab change"); }
void AnnotationTabSleeping(const std::shared_ptr<Tab>& t) { if (Ann::s.active && Ann::ModeTab() == t) Ann::Exit(L"tab asleep"); }
bool AnnotationPageMessage(const std::shared_ptr<Tab>& t, ICoreWebView2WebMessageReceivedEventArgs* a) {
    using namespace Ann;
    if (!s.active || !t || ModeTab() != t || !a) return false;
    LPWSTR raw = nullptr;
    if (FAILED(a->get_WebMessageAsJson(&raw)) || !raw) { CoTaskMemFree(raw); return false; }
    std::wstring json = TakeString(raw);
    if (!AH::LooksLikeAnnotation(json)) { Wipe(json); return false; }
    const std::wstring source = TakeString([&] { LPWSTR x = nullptr; a->get_Source(&x); return x; }());
    const std::string utf8 = AH::Utf8(json); Wipe(json);
    if (t != Active()) { LogOnce(L"inactive tab"); return true; }
    if (source != s.source) { LogOnce(L"source"); return true; }
    if (t->navStamp != s.navStamp) { LogOnce(L"navStamp"); return true; }
    const auto c = AH::CheckMessage(utf8, s.nonce, std::int64_t(s.navStamp));
    switch (c.env.op) {
    case AH::Op::Select: OnSelect(t, c.selection); break;
    case AH::Op::Hidden: OnHidden(t, c.env.serial); break;
    case AH::Op::Escape: Exit(L"escape"); break;
    case AH::Op::NoSelection: Announce(L"Aucun texte sélectionné à annoter"); break;
    default: LogOnce(AH::Wide(c.why.empty() ? std::string("invalid") : c.why)); break;
    }
    return true;
}
bool AnnotationCommand(int cmd) {
    if (cmd == 'N' || cmd == AnnotateCommand) { if (Ann::s.active) Ann::Exit(L"toggle"); else Ann::Activate({}); return true; }
    if (cmd == AnnotateExitCommand) { Ann::Exit(L"escape"); return true; }
    return false;
}
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
    std::vector<CM::Item> add{CM::Separator(), CM::Command(AnnotateElementItem, L"Annoter l'élément")};
    add.back().shortcut = L"Ctrl+Maj+A";
    if (Ann::menu.selection) add.push_back(CM::Command(AnnotateSelectionItem, L"Annoter la sélection"));
    if (Ann::last) add.push_back(CM::Command(AnnotateMarkdownItem, L"Copier la dernière annotation (Markdown)"));
    auto lastGroup = std::find_if(items.rbegin(), items.rend(), [](const CM::Item& m) { return m.separator; });
    auto where = lastGroup == items.rend() ? items.end() : std::prev(lastGroup.base());
    items.insert(where, add.begin(), add.end());
}
void AnnotationMenuChoice(const std::shared_ptr<Tab>& tab, int choice) {
    if (choice != AnnotateElementItem && choice != AnnotateSelectionItem && choice != AnnotateMarkdownItem) return;
    const auto m = Ann::menu; Ann::menu = {};
    if (choice == AnnotateMarkdownItem) {
        if (!Ann::last) return;
        const auto md = AnnotationHost::Wide(BrowserAnnotations::ToMarkdown(*Ann::last, Ann::Store().ShotPath(Ann::last->annotation.project, Ann::last->annotation.id)));
        if (CopyToClipboard(md)) Ann::Announce(L"Annotation copiée en Markdown");
        return;
    }
    if (!m.valid || !tab || tab != Active()) return;
    Ann::Start st; st.kind = choice == AnnotateSelectionItem ? 2 : 1; st.at = m.at; st.text = m.text;
    Ann::Activate(st);
}
#else
// Unstamped fixtures (tests that include main.cpp) and --channel release builds: no annotation mode.
bool AnnotationModeActive() { return false; }
bool AnnotationSurfaceFocus() { return false; }
void AnnotationSyncChrome() {}
void AnnotationLayout() {}
void AnnotationNavigation(const std::shared_ptr<Tab>&) {}
void AnnotationTabChanged() {}
void AnnotationTabSleeping(const std::shared_ptr<Tab>&) {}
bool AnnotationPageMessage(const std::shared_ptr<Tab>&, ICoreWebView2WebMessageReceivedEventArgs*) { return false; }
bool AnnotationCommand(int) { return false; }
void AnnotationMenuItems(const std::shared_ptr<Tab>&, ICoreWebView2ContextMenuRequestedEventArgs*, std::vector<CM::Item>&) {}
void AnnotationMenuChoice(const std::shared_ptr<Tab>&, int) {}
#endif
