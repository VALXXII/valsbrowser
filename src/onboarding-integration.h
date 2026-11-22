// First-run onboarding, host side (part of main.cpp, included after browser-import-integration.h so it sees the
// profile, import and annotation globals). Window: src/onboarding.h; flow and first-run rule: src/onboarding-flow.h.
//
//  * When: a fresh install (installed layout, no profile registry, no data of an older build) or the
//    « --onboarding » switch (QA, design preview; works in a portable folder too). Never in a smoke run, a private
//    window or the motion lab; BROWSER_NO_ONBOARDING=1 keeps automated installs out of it. The window runs before
//    the browser window exists: closing it, Échap and « Passer et naviguer » go straight to the normal start.
//  * Profile: the window's draft is saved into the profile this start opens (the default « Personnel » on a first
//    run) with Profiles::Update — same validation and three-way registry write as Réglages › Profils. Nothing is
//    written before « Continuer ».
//  * Import: the same catalog, readers and transactions as Réglages › « Importer depuis un navigateur… »
//    (src/browser-import.h, src/browser-catalog.h): discovery reads directory metadata only, the read runs on a
//    worker thread and touches no global, the commit happens on the UI thread, one profile-store transaction per
//    category, into the profile's own files (the browser window then loads them as on any start).
//    « --import-sources <dir> » (tests) applies here as it does there.
//  * Language: the picker pins the process language at once (no window exists yet, nothing is half translated)
//    and writes the browser-wide preference (src/ui-language.h).
//  * « Activer » on the annotation board: once the browser window is up, the « Connecter un agent » dialog opens
//    (the mode itself needs a web page; connecting an agent is what turns it on for the person).
// Logs carry counts and the source brand only: never a URL, a title or a path of the person's data.
#include "onboarding.h"

namespace OB = NativeChrome::OnboardingUI;
bool onboardingAnnotate = false;
constexpr UINT_PTR OnboardingAnnotateTimerId = 0x4F42;
struct OnboardingJob { unsigned token = 0; BI::Source source; BI::Options options; BI::ReadResult result; std::wstring name; };
OB::Window* onboardingUi = nullptr; HWND onboardingWindow = nullptr;
std::shared_ptr<OnboardingJob> onboardingFinished;          // handed back by the worker, under importLock
std::vector<std::vector<BI::Source>> onboardingSources;     // [browser][profile], as the window lists them
std::vector<std::wstring> onboardingNames;

// The names people know the browsers by (the boards write « Google Chrome »).
std::wstring OnboardingBrowserName(BI::Browser b) {
    switch (b) {
    case BI::Browser::Chrome: return L"Google Chrome";     // i18n-exempt: product name
    case BI::Browser::Edge: return L"Microsoft Edge";      // i18n-exempt: product name
    default: return BI::BrandName(b);
    }
}
std::wstring OnboardingSentence(std::wstring s) { if (!s.empty()) s[0] = static_cast<wchar_t>(towupper(s[0])); return s; }
// « Profil 1 » for Chromium's « Profile 1 » folder, « Profil principal » for « Default », else the profile's own name.
std::wstring OnboardingProfileLabel(const BI::Source& s) {
    if (BI::Chromium(s.browser) && s.isDefault) return OnboardingSentence(TR(import_profile_default));
    static const std::wstring prefix = L"Profile ";   // i18n-exempt: Chromium's folder name
    if (BI::Chromium(s.browser) && s.profile.rfind(prefix, 0) == 0 && s.profile.size() > prefix.size() && s.profile.find_first_not_of(L"0123456789", prefix.size()) == std::wstring::npos) return TRF(onboarding_import_profile_n, s.profile.substr(prefix.size()));
    return s.profile;
}
std::vector<OB::BrowserSource> OnboardingSources() {
    const std::vector<BI::Row> rows = BI::Catalog(ImportRegistered(), BI::Discover(ImportRoots()), OwnExecutable());
    std::vector<OB::BrowserSource> out; onboardingSources.clear(); onboardingNames.clear();
    size_t ready = 0, unavailable = 0;
    for (const auto& r : rows) {
        if (r.state == BI::RowState::Html) { OB::BrowserSource f; f.name = TR(import_source_html); f.file = true; out.push_back(f); onboardingSources.push_back({r.source}); onboardingNames.push_back(TR(import_source_file_short)); continue; }
        if (!r.Available()) { OB::BrowserSource u; u.name = r.state == BI::RowState::NoData ? OnboardingBrowserName(r.source.browser) : r.name; u.detail = BI::RowDetail(r); u.available = false; u.logo = r.icon.empty() ? nullptr : BI::LoadLogo(r.icon, r.iconIndex, 96); out.push_back(u); onboardingSources.push_back({}); onboardingNames.push_back(u.name); ++unavailable; continue; }
        const std::wstring name = OnboardingBrowserName(r.source.browser);
        std::size_t at = 0; for (; at < out.size(); ++at) if (out[at].available && !out[at].file && out[at].name == name) break;
        if (at == out.size()) { OB::BrowserSource b; b.name = name; b.detail = BI::RowDetail(r); b.logo = r.icon.empty() ? nullptr : BI::LoadLogo(r.icon, r.iconIndex, 96); out.push_back(b); onboardingSources.push_back({}); onboardingNames.push_back(name); }
        out[at].profiles.push_back({OnboardingProfileLabel(r.source), r.source.bookmarks, r.source.history}); onboardingSources[at].push_back(r.source); ++ready;
    }
    Log(L"ONBOARDING import sources: " + std::to_wstring(ready) + L" profiles, " + std::to_wstring(unavailable) + L" browsers unavailable");
    return out;
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
    if (pick.browser < 0 || pick.browser >= static_cast<int>(onboardingSources.size())) return false;
    const auto& profiles = onboardingSources[static_cast<std::size_t>(pick.browser)];
    if (pick.profile < 0 || pick.profile >= static_cast<int>(profiles.size())) return false;
    BI::Source source = profiles[static_cast<std::size_t>(pick.profile)];
    if (source.browser == BI::Browser::HtmlFile) { source.dir = PickBookmarksFile(onboardingWindow); if (source.dir.empty()) return false; history = false; }
    auto job = std::make_shared<OnboardingJob>();
    job->token = token; job->source = source; job->options.bookmarks = bookmarks && (source.bookmarks || source.browser == BI::Browser::HtmlFile); job->options.history = history && source.history;
    job->name = onboardingNames[static_cast<std::size_t>(pick.browser)];
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
    OB::ImportOutcome outcome; const BI::Source& s = job->source; const auto now = NativeChrome::DownloadsUI::UnixNowMs();
    auto why = [&](BI::Failure f) { return f == BI::Failure::Busy && s.browser != BI::Browser::HtmlFile ? TRF(onboarding_import_open, job->name) : OnboardingSentence(BI::FailureText(f, s)); };
    auto failed = [&](const std::wstring& reason) { if (outcome.detail.empty()) outcome.detail = reason; };
    if (job->options.bookmarks) {
        const auto& r = job->result.bookmarks;
        if (r.failure != BI::Failure::None) { failed(why(r.failure)); Log(L"ONBOARDING bookmarks read failed " + std::to_wstring(static_cast<int>(r.failure))); }
        else {
            BM::Store mine; { ProfileStore::Lock lock(dataDir); std::string bytes; ProfileStore::ReadAll(BookmarksFile(), bytes); std::istringstream in(bytes); bool damaged = false; mine = BM::Store::Read(in, &damaged); }
            BM::Store was = mine;
            auto c = BI::CommitBookmarks(dataDir, BookmarksFile(), dataDir + L"\\bookmark-imports", was, mine, s, r.roots, now, nullptr, nullptr);
            if (c.Saved()) { outcome.bookmarksSaved = true; Log(L"ONBOARDING bookmarks saved " + ImportCountsLog(c.counts) + L" ignored=" + std::to_wstring(r.ignored)); }
            else { failed(OnboardingSentence(c.identity == BI::Identity::Newer ? TR(import_map_newer) : c.identity == BI::Identity::Damaged ? TR(import_map_damaged) : c.identity == BI::Identity::Full ? TR(import_map_full) : TR(import_save_failed))); Log(L"ONBOARDING bookmarks save failed result=" + std::to_wstring(static_cast<int>(c.result))); }
        }
    }
    if (job->options.history) {
        const auto& r = job->result.history;
        if (r.failure != BI::Failure::None) { failed(why(r.failure)); Log(L"ONBOARDING history read failed " + std::to_wstring(static_cast<int>(r.failure))); }
        else {
            AddressBar::History mine; { ProfileStore::Lock lock(dataDir); std::string bytes; ProfileStore::ReadAll(HistoryFile(), bytes); std::istringstream in(bytes); mine = AddressBar::History::Read(in); }
            AddressBar::History was = mine;
            auto c = BI::CommitHistory(dataDir, HistoryFile(), was, mine, r.visits, r.beyond, now, [](const std::wstring& u) { return SearchResultPage(u); });
            if (c.Saved()) { outcome.historySaved = true; Log(L"ONBOARDING history saved " + ImportCountsLog(c.counts) + L" ignored=" + std::to_wstring(r.ignored)); }
            else { failed(OnboardingSentence(TR(import_save_failed))); Log(L"ONBOARDING history save failed result=" + std::to_wstring(static_cast<int>(c.result))); }
        }
    }
    onboardingUi->ImportFinished(job->token, outcome);
}
// Read before SelectProfile (which creates the registry): is this a first run, or was the window asked for?
Onboarding::Reason OnboardingReason(const wchar_t* args) {
    Onboarding::Launch l; namespace fs = std::filesystem; std::error_code ec;
    l.requested = Onboarding::HasSwitch(args); l.smoke = smoke; l.privateWindow = privateWindow; l.motionLab = motionLabMode;
    l.installed = dataDir != base;   // installed layout: the data lives in <root>\data, not beside the executable
    l.hasRegistry = fs::exists(fs::path(dataDir) / L"profiles.txt", ec);
    l.hasData = fs::exists(fs::path(settingsFile), ec) || fs::exists(fs::path(sessionFile), ec) || fs::exists(fs::path(profileDir), ec) || fs::exists(fs::path(dataDir) / L"profiles", ec);
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
    OB::Window ui; onboardingUi = &ui;
    const OB::Result result = ui.Run(instance, base, std::move(o));
    onboardingUi = nullptr; onboardingWindow = nullptr;
    { std::lock_guard<std::mutex> guard(importLock); onboardingFinished.reset(); }
    onboardingAnnotate = result.annotate;
    Log(std::wstring(L"ONBOARDING ended: ") + (result.ending == Onboarding::Ending::Completed ? L"completed" : L"skipped") + (result.profileSaved ? L", profile step done" : L"") + (result.annotate ? L", annotation asked" : L""));
}
// Once the browser window is shown.
void OnboardingAfterStart() {
    if (!onboardingAnnotate || !window) return;
    onboardingAnnotate = false;
    SetTimer(window, OnboardingAnnotateTimerId, 900, [](HWND h, UINT, UINT_PTR id, DWORD) { KillTimer(h, id); if (!halted && !privateWindow) { Log(L"ONBOARDING opens the agent connection"); AnnotationOpenConnect(false); } });
}
