// First-run onboarding, host side (part of main.cpp, included after browser-import-integration.h so it sees the
// profile, import and annotation globals). Window: src/onboarding.h; flow and first-run rule: src/onboarding-flow.h;
// import view and commit: src/onboarding-import.h.
//
//  * When: a fresh install (installed layout, no profile registry, no data of an older build) or the
//    « --onboarding » switch (QA, design preview; works in a portable folder too). Never in a smoke run, a private
//    window or the motion lab; BROWSER_NO_ONBOARDING=1 keeps automated installs out of it. The window runs before
//    the browser window exists: closing it, Échap and « Passer et naviguer » go straight to the normal start.
//  * Profile: the window's draft is saved into the profile this start opens (the default « Personnel » on a first
//    run) with Profiles::Update — same validation and three-way registry write as Réglages › Profils. Nothing is
//    written before « Continuer ».
//  * Import: the same catalog, readers and transactions as Réglages › « Importer depuis un navigateur… »:
//    discovery reads directory metadata only, the read runs on a worker thread and touches no global, the commit
//    happens on the UI thread, one profile-store transaction per category, into the profile's own files (the
//    browser window then loads them as on any start). « --import-sources <dir> » (tests) applies here as there.
//  * Language: the picker pins the process language at once (no window exists yet, nothing is half translated)
//    and writes the browser-wide preference (src/ui-language.h).
//  * The end: when the flow is completed the onboarding window leaves on a stage (src/onboarding-handoff.h) and
//    the browser window comes in on it: created and painted cloaked, photographed, shown scaled-in as a picture,
//    then uncovered under the picture's last frame. Skipping (Échap, close, « Passer et naviguer ») starts the
//    browser the plain way.
//  * « Activer » on the annotation board: once the browser window is up, the « Connecter un agent » dialog opens
//    (the mode itself needs a web page; connecting an agent is what turns it on for the person).
// Logs carry counts and the source brand only: never a URL, a title or a path of the person's data.
#include "onboarding.h"
#include "onboarding-import.h"

namespace OB = NativeChrome::OnboardingUI; namespace OI = OnboardingImport;
bool onboardingAnnotate = false, onboardingDebug = false;   // debug: « --onboarding-debug », the 2 → 3 tuning panel (review only)
constexpr UINT_PTR OnboardingAnnotateTimerId = 0x4F42;
struct OnboardingJob { unsigned token = 0; BI::Source source; BI::Options options; BI::ReadResult result; std::wstring name; };
OB::Window* onboardingUi = nullptr; HWND onboardingWindow = nullptr;
std::shared_ptr<OnboardingJob> onboardingFinished;          // handed back by the worker, under importLock
struct OnboardingEntry { std::vector<BI::Source> profiles; std::wstring name; };   // behind one entry of board 3's list
OnboardingHandoff::Stage onboardingStage;                   // the window's exit and the browser's entrance, after a completed flow
constexpr UINT_PTR OnboardingRevealTimerId = 0x4F43;

// Worker thread (the window asks for it in the background): registry, folder metadata and logos only, no global.
// Each entry carries its own sources, so a pick always resolves against the list the window is showing.
std::vector<OB::BrowserSource> OnboardingSources() {
    const ULONGLONG start = GetTickCount64();
    OI::Listing listing = OI::List(BI::Catalog(ImportRegistered(), BI::Discover(ImportRoots()), OwnExecutable()), [](const BI::Row& r) { return BI::LoadLogo(r.icon, r.iconIndex, 96); });
    for (std::size_t i = 0; i < listing.shown.size(); ++i) listing.shown[i].tag = std::make_shared<const OnboardingEntry>(OnboardingEntry{listing.sources[i], listing.names[i]});
    Log(L"ONBOARDING import sources: " + std::to_wstring(listing.profiles) + L" profiles, " + std::to_wstring(listing.unavailable) + L" browsers unavailable, listed in " + std::to_wstring(GetTickCount64() - start) + L" ms off the UI thread");
    return listing.shown;
}
DWORD WINAPI OnboardingWorker(LPVOID param) {
    std::unique_ptr<std::shared_ptr<OnboardingJob>> job(static_cast<std::shared_ptr<OnboardingJob>*>(param));
    (*job)->result = BI::Read((*job)->source, (*job)->options);
    ImportHoldPoint(L"read");
    const HWND target = onboardingWindow; const unsigned token = (*job)->token;
    { std::lock_guard<std::mutex> guard(importLock); onboardingFinished = *job; }
    if (target) PostMessageW(target, OB::Window::HostMessage, token, 0);
    return 0;
}
bool OnboardingStartImport(unsigned token, const OB::ImportPick& pick, bool bookmarks, bool history) {
    const OB::BrowserSource* shown = onboardingUi ? onboardingUi->PickedSource() : nullptr;
    const auto* entry = shown ? static_cast<const OnboardingEntry*>(shown->tag.get()) : nullptr;
    if (!entry || pick.profile < 0 || pick.profile >= static_cast<int>(entry->profiles.size())) return false;
    BI::Source source = entry->profiles[static_cast<std::size_t>(pick.profile)];
    if (source.browser == BI::Browser::HtmlFile) { source.dir = PickBookmarksFile(onboardingWindow); if (source.dir.empty()) return false; history = false; }
    auto job = std::make_shared<OnboardingJob>();
    job->token = token; job->source = source; job->name = entry->name;
    job->options.bookmarks = bookmarks && (source.bookmarks || source.browser == BI::Browser::HtmlFile); job->options.history = history && source.history;
    if (!job->options.bookmarks && !job->options.history) return false;
    auto* param = new std::shared_ptr<OnboardingJob>(job);
    // Generous stack: the source's folders are walked recursively (bounded to 256 levels).
    HANDLE t = CreateThread(nullptr, 16u << 20, OnboardingWorker, param, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    if (!t) { delete param; return false; }
    CloseHandle(t);
    Log(std::wstring(L"ONBOARDING import started from ") + (source.browser == BI::Browser::HtmlFile ? L"html" : BI::BrandName(source.browser)) + (job->options.bookmarks ? L" bookmarks" : L"") + (job->options.history ? L" history" : L""));
    return true;
}
// The read is back (UI thread): one transaction per category into the profile's files, then the window is told.
void OnboardingImportRead(WPARAM token) {
    std::shared_ptr<OnboardingJob> job;
    { std::lock_guard<std::mutex> guard(importLock); job = std::move(onboardingFinished); }
    if (!job || job->token != static_cast<unsigned>(token) || !onboardingUi) return;
    // A result for an import the person cancelled, left or replaced by another one: nothing is written.
    if (onboardingUi->ImportToken() != job->token) { Log(L"ONBOARDING import dropped: no longer wanted, nothing written"); return; }
    const OI::Files files{dataDir, BookmarksFile(), HistoryFile(), dataDir + L"\\bookmark-imports"};
    const OB::ImportOutcome outcome = OI::Commit(files, job->source, job->name, job->options, job->result, NativeChrome::DownloadsUI::UnixNowMs(),
                                                 [](const std::wstring& u) { return SearchResultPage(u); }, [](const std::wstring& line) { Log(line); });
    onboardingUi->ImportFinished(job->token, outcome);
}
// Read before SelectProfile (which creates the registry): is this a first run, or was the window asked for?
Onboarding::Reason OnboardingReason(const wchar_t* args) {
    Onboarding::Launch l; namespace fs = std::filesystem; std::error_code ec;
    l.requested = Onboarding::Asked(args); l.smoke = smoke; l.privateWindow = privateWindow; l.motionLab = motionLabMode;
    l.installed = dataDir != base;   // installed layout: the data lives in <root>\data, not beside the executable
    l.hasRegistry = fs::exists(fs::path(dataDir) / L"profiles.txt", ec);
    l.hasData = fs::exists(fs::path(settingsFile), ec) || fs::exists(fs::path(sessionFile), ec) || fs::exists(fs::path(profileDir), ec) || fs::exists(fs::path(dataDir) / L"profiles", ec);
    onboardingDebug = Onboarding::HasSwitch(args, Onboarding::DebugSwitch);
    wchar_t off[8]{}; l.optOut = GetEnvironmentVariableW(L"BROWSER_NO_ONBOARDING", off, 8) > 0 && off[0] != L'0';
    return Onboarding::ShouldShow(l);
}
// After SelectProfile and the fonts, before the browser window. Returns when the person is done.
void RunOnboarding(Onboarding::Reason reason) {
    if (reason == Onboarding::Reason::None || !profilesEnabled || privateWindow || smoke) return;
    Log(std::wstring(L"ONBOARDING shown (") + (reason == Onboarding::Reason::FirstRun ? L"first run" : L"requested") + L")");
    OB::Options o; o.annotation = AnnotationsCompiled(); o.reduced = ReducedMotionPreferred();
    const Profiles::Profile current = CurrentProfile() ? *CurrentProfile() : DefaultProfileLook();
    // An untouched default profile takes the boards' sample look; a profile the person already shaped keeps its own.
    const bool untouched = current.id == Profiles::DefaultId && current.neutral && current.icon == Profiles::DefaultIcon;
    o.seed = untouched ? OB::PaperSeed(current.id, current.name) : current;
    o.hooks.saveProfile = [](const Profiles::Profile& draft) {
        Profiles::Profile next = draft; next.id = profileId; Profiles::NameError error = Profiles::NameError::None;
        if (Profiles::Update(profileRoot, profileRegistry, next, &error)) { Log(L"ONBOARDING profile saved"); BroadcastProfilesChanged(); return Profiles::NameError::None; }
        if (error != Profiles::NameError::None) return error;
        Log(L"ONBOARDING profile not saved (registry " + (profileRegistry.readOnly ? profileRegistry.error : std::wstring(L"busy")) + L")");   // the flow goes on: the person can redo it in Réglages
        return Profiles::NameError::None;
    };
    o.hooks.sources = [] { return OnboardingSources(); };
    o.hooks.startImport = [](unsigned token, const OB::ImportPick& pick, bool bookmarks, bool history) { return OnboardingStartImport(token, pick, bookmarks, history); };
    o.hooks.message = [](WPARAM token, LPARAM) { OnboardingImportRead(token); };
    o.hooks.language = [](I18n::Locale l) { if (!UiLanguage::Save(profileRoot, l)) Log(L"ONBOARDING language not saved"); else Log(std::wstring(L"ONBOARDING language ") + (l == I18n::Locale::En ? L"en" : L"fr")); };
    o.hooks.created = [](HWND h) { onboardingWindow = h; };   // where the worker posts its answer
    if (onboardingDebug) { o.debug = true; o.tuningFile = profileRoot + L"\\onboarding-tuning.txt"; Log(L"ONBOARDING debug panel (2 -> 3 transition)"); }
    o.stage = &onboardingStage;
    OB::Window ui; onboardingUi = &ui;
    const OB::Result result = ui.Run(instance, base, std::move(o));
    Log(L"ONBOARDING slowest board change held the UI thread " + std::to_wstring(static_cast<int>(std::lround(ui.SlowestChangeMs()))) + L" ms, slowest frame " + std::to_wstring(static_cast<int>(std::lround(ui.SlowestFrameMs()))) + L" ms");
    onboardingUi = nullptr; onboardingWindow = nullptr;
    { std::lock_guard<std::mutex> guard(importLock); onboardingFinished.reset(); }
    onboardingAnnotate = result.annotate;
    Log(std::wstring(L"ONBOARDING ended: ") + (result.ending == Onboarding::Ending::Completed ? L"completed" : L"skipped") + (result.profileSaved ? L", profile step done" : L"") + (result.annotate ? L", annotation asked" : L"") + (result.handedOff ? L", hand-off" : L""));
}
// In place of ShowWindow + UpdateWindow at the end of the start. After a completed onboarding the window is
// shown cloaked (composed by DWM, not visible), painted, photographed and handed to the stage, which plays its
// scale-in and uncovers the real window under the last frame. Anything that goes wrong shows it the plain way.
void OnboardingShowBrowser(int show) {
    if (!onboardingStage.Active()) { ShowWindow(window, show); UpdateWindow(window); return; }
    OnboardingHandoff::Cloak(window, true);
    ShowWindow(window, show); UpdateWindow(window);
    RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN); DwmFlush(); DwmFlush();
    auto shot = OnboardingHandoff::Capture(window, IsZoomed(window) ? 0.f : NativeUI::nativeCornerRadius * GetDpiForWindow(window) / 96.f);
    if (shot.Valid() && onboardingStage.Enter(std::move(shot), window)) {
        Log(L"ONBOARDING hand-off: browser painted under cover, scale-in on its picture");
        // Whatever happens to the stage, the window never stays hidden.
        SetTimer(window, OnboardingRevealTimerId, 5000, [](HWND h, UINT, UINT_PTR id, DWORD) { KillTimer(h, id); OnboardingHandoff::Cloak(h, false); });
    } else { OnboardingHandoff::Cloak(window, false); Log(L"ONBOARDING hand-off: no picture of the browser, shown directly"); }
}
// Once the browser window is shown.
void OnboardingAfterStart() {
    if (!onboardingAnnotate || !window) return;
    onboardingAnnotate = false;
    SetTimer(window, OnboardingAnnotateTimerId, 900, [](HWND h, UINT, UINT_PTR id, DWORD) { KillTimer(h, id); if (!halted && !privateWindow) { Log(L"ONBOARDING opens the agent connection"); AnnotationOpenConnect(false); } });
}
