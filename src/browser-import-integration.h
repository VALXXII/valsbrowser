// Réglages › Confidentialité › « Importer depuis un navigateur… » (part of main.cpp, included near its end so
// it sees every profile-scoped global). Model, readers and rules: src/browser-import.h; plan and availability:
// design/browser-data-import-plan.md.
//
// Flow
//  1. The row opens one window dialog (src/modal.h): source select (discovered profiles + « Fichier de favoris
//     HTML… »), three checks (Favoris, Historique, Cookies — the last always disabled with the reason), and the
//     destination named in the body (this window's profile). Discovery reads directory metadata only.
//  2. « Importer » (closes = false): an HTML source first asks for the file (IFileOpenDialog). The dialog goes
//     busy and a worker thread reads the source (no global touched). The result comes back to the UI thread by
//     a posted message.
//  3. On the UI thread, only if this window still shows the same profile (workspace, profile id, data folder):
//     one profile-store transaction per category (BrowserImport::CommitBookmarks / CommitHistory). If nothing
//     could be imported the dialog stays open with the reason (retry); otherwise it closes and a summary dialog
//     reports each category honestly (terminée / partielle).
// Logs carry counts and the source brand only: never a URL, a title or a path of the person's data.
#include "browser-import.h"

namespace BI = BrowserImport;
// The destination named in the dialog: pinned when the dialog opens, checked again when « Importer » is pressed and
// when the read comes back. If this window shows another profile at either moment, nothing is written.
struct ImportDestination {
    Workspace* owner = nullptr; std::wstring profile, data;
    static ImportDestination Now() { return {WorkspaceNow(), profileId, dataDir}; }
    bool Current() const { return !WorkspaceDead(owner) && WorkspaceNow() == owner && !WorkspaceParked() && profileId == profile && dataDir == data; }
};
struct ImportJob {
    BI::Source source; BI::Options options; BI::ReadResult result;
    ImportDestination destination;
};
std::shared_ptr<ImportJob> importRunning;        // the worker's job (UI thread owns the pointer)
std::shared_ptr<ImportJob> importFinished;        // handed back by the worker, under importLock
std::mutex importLock;
bool importDialogOpen = false;
std::wstring importRootsOverride;                 // --import-sources <dir>: synthetic roots (tests); never set otherwise

BI::Roots ImportRoots() {
    if (!importRootsOverride.empty()) return BI::Roots{importRootsOverride + L"\\Local", importRootsOverride + L"\\Roaming"};
    return BI::DefaultRoots();
}
std::wstring ImportProfileName() { if (const auto* p = CurrentProfile()) return p->name; return DefaultProfileLook().name; }
// The check list for a source (Favoris, Historique, Cookies), with each one's availability.
std::vector<MD::Check> ImportChecks(const BI::Source& s) {
    MD::Check bookmarks, history, cookies;
    bookmarks.label = TR(import_item_bookmarks); history.label = TR(import_item_history); cookies.label = TR(import_item_cookies);
    bookmarks.value = s.bookmarks; bookmarks.disabled = !s.bookmarks;
    bookmarks.detail = s.bookmarks ? TRF(import_bookmarks_detail, BI::FolderTitle(s)) : std::wstring(TR(import_bookmarks_missing));
    if (s.browser == BI::Browser::HtmlFile) { history.disabled = true; history.detail = TR(import_history_html); }
    else { history.value = s.history; history.disabled = !s.history; history.detail = s.history ? TR(import_history_detail) : TR(import_history_missing); }
    cookies.disabled = true; cookies.value = false; cookies.detail = TR(import_cookies_unavailable);
    return {bookmarks, history, cookies};
}
std::wstring PickBookmarksFile() {
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))) || !dialog) { Log(L"IMPORT file picker unavailable"); return L""; }
    FILEOPENDIALOGOPTIONS options = 0; dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_NODEREFERENCELINKS);
    dialog->SetTitle(TR(import_pick_title));
    const std::wstring filter = TR(import_pick_filter);
    const COMDLG_FILTERSPEC types[] = {{filter.c_str(), L"*.html;*.htm"}};
    dialog->SetFileTypes(1, types);
    std::wstring chosen; const HRESULT hr = dialog->Show(modal.Window() ? modal.Window() : window);
    if (SUCCEEDED(hr)) { IShellItem* item = nullptr; PWSTR path = nullptr; if (SUCCEEDED(dialog->GetResult(&item)) && item && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) chosen = path; CoTaskMemFree(path); if (item) item->Release(); }
    else if (hr != HRESULT_FROM_WIN32(ERROR_CANCELLED)) Log(L"IMPORT file picker failed hr=" + std::to_wstring(static_cast<unsigned long>(hr)));
    dialog->Release();
    return chosen;
}
// ---- Test seam (E2E only; compiled out of release-channel builds) ------------------------------------------------
// `--import-hold read|snapshot` (with --import-sources): the worker stops right after the read (« read ») or right
// after the private database snapshot is taken (« snapshot »), sets the named event
// Local\\browser-import-reached-<pid> and waits for Local\\browser-import-release-<pid> (60 s at most). A test can thus
// switch profile, delete one or close the window at a known point of the import. No flag: no wait, no event.
std::wstring importHold;
#ifndef BROWSER_CHANNEL_RELEASE
void ImportHoldPoint(const wchar_t* phase) {
    if (importHold != phase) return;
    const std::wstring pid = std::to_wstring(GetCurrentProcessId());
    HANDLE reached = CreateEventW(nullptr, TRUE, FALSE, (L"Local\\browser-import-reached-" + pid).c_str());
    HANDLE release = CreateEventW(nullptr, TRUE, FALSE, (L"Local\\browser-import-release-" + pid).c_str());
    if (reached) SetEvent(reached);
    if (release) WaitForSingleObject(release, 60000);
    if (reached) CloseHandle(reached);
    if (release) CloseHandle(release);
}
#else
void ImportHoldPoint(const wchar_t*) {}
#endif
DWORD WINAPI ImportWorker(LPVOID param) {
    std::unique_ptr<std::shared_ptr<ImportJob>> job(static_cast<std::shared_ptr<ImportJob>*>(param));
#ifndef BROWSER_CHANNEL_RELEASE
    if (importHold == L"snapshot") BI::SnapshotTakenForTest() = [] { ImportHoldPoint(L"snapshot"); };
#endif
    (*job)->result = BI::Read((*job)->source, (*job)->options);
    ImportHoldPoint(L"read");
    HWND target = window;
    { std::lock_guard<std::mutex> guard(importLock); importFinished = *job; }
    if (target) PostMessageW(target, ImportMessage, 0, 0);
    return 0;
}
void StartImport(const std::vector<BI::Source>& sources, const ImportDestination& destination) {
    if (importRunning || !modalOpen) return;
    if (!destination.Current()) { Log(L"IMPORT not started: the profile changed since the dialog opened"); modal.SetError(TR(import_profile_changed)); return; }
    const MD::Result now = modal.Snapshot();
    if (now.select < 0 || now.select >= static_cast<int>(sources.size())) return;
    BI::Source source = sources[static_cast<std::size_t>(now.select)];
    const bool bookmarks = now.checks.size() > 0 && now.checks[0] && source.bookmarks;
    const bool history = now.checks.size() > 1 && now.checks[1] && source.history;
    if (!bookmarks && !history) { modal.SetError(TR(import_none_selected)); return; }
    if (source.browser == BI::Browser::HtmlFile) {
        source.dir = PickBookmarksFile();
        if (source.dir.empty()) return;   // cancelled: the dialog stays as it was
        if (!destination.Current()) { modal.SetError(TR(import_profile_changed)); return; }   // switched while the file dialog was open
    }
    auto job = std::make_shared<ImportJob>();
    job->source = source; job->options.bookmarks = bookmarks; job->options.history = history;
    job->destination = destination;
    modal.SetBusy(true, TR(import_busy));
    importRunning = job;
    auto* param = new std::shared_ptr<ImportJob>(job);
    // Generous stack: the source's folders are walked recursively (bounded to 256 levels).
    HANDLE t = CreateThread(nullptr, 16u << 20, ImportWorker, param, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    if (!t) { delete param; importRunning.reset(); modal.SetBusy(false); modal.SetError(TRF(import_result_failed, TR(import_title), TR(import_failure_unreadable))); return; }
    CloseHandle(t);
    Log(std::wstring(L"IMPORT started from ") + (source.browser == BI::Browser::HtmlFile ? L"html" : BI::BrandName(source.browser)) + (bookmarks ? L" bookmarks" : L"") + (history ? L" history" : L""));
}
std::wstring ImportCountsLog(const BI::Counts& c) {
    return L"imported=" + std::to_wstring(c.imported) + L" present=" + std::to_wstring(c.present) + L" unsupported=" + std::to_wstring(c.unsupported) + L" full=" + std::to_wstring(c.full) + L" deep=" + std::to_wstring(c.deep) + L" folders=" + std::to_wstring(c.folders);
}
void ImportFinished() {
    std::shared_ptr<ImportJob> job;
    { std::lock_guard<std::mutex> guard(importLock); job = std::move(importFinished); }
    if (!job || job != importRunning) return;
    importRunning.reset();
    const bool dialog = modalOpen && importDialogOpen;
    // The destination is the profile named in the dialog; if this window shows another one now, nothing is written.
    if (!job->destination.Current()) {
        Log(L"IMPORT cancelled: the profile changed, nothing written");
        if (dialog) { modal.SetBusy(false); modal.SetError(TR(import_profile_changed)); }
        return;
    }
    if (!dialog) { Log(L"IMPORT cancelled: dialog closed, nothing written"); return; }
    const BI::Source& s = job->source; const auto now = NativeChrome::DownloadsUI::UnixNowMs();
    std::vector<std::wstring> lines; bool anySaved = false, anyFailed = false, anyLeft = false; std::wstring firstError;   // anyLeft: entries not imported (limit, depth)
    auto failed = [&](const wchar_t* category, const std::wstring& why) {
        anyFailed = true; const std::wstring line = TRF(import_result_failed, category, why); lines.push_back(line); if (firstError.empty()) firstError = line;
    };
    if (job->options.bookmarks) {
        const auto& r = job->result.bookmarks;
        if (r.failure != BI::Failure::None) { failed(TR(import_item_bookmarks), BI::FailureText(r.failure, s)); Log(L"IMPORT bookmarks read failed " + std::to_wstring(static_cast<int>(r.failure))); }
        else {
            std::map<BM::Id, BM::Id> remap;
            // Identities first, bookmarks second, one transaction (src/browser-import.h CommitBookmarks): no identity, no import.
            auto c = BI::CommitBookmarks(dataDir, BookmarksFile(), dataDir + L"\\bookmark-imports", bookmarksBase, bookmarks, s, r.roots, now, nullptr, &remap);
            if (!remap.empty()) bookmarkUndo.clear();   // as SaveBookmarks: ids taken in another window were remapped
            c.counts.unreadable = r.ignored;
            if (c.Saved()) {
                anySaved = true; bookmarksSaveFailed = false; if (c.counts.Left()) anyLeft = true;
                if (!bookmarks.Find(lastFolder)) lastFolder = BM::BarRoot;
                lines.push_back(TRF(import_result_bookmarks, BI::CountsText(c.counts)));
                Log(L"IMPORT bookmarks saved " + ImportCountsLog(c.counts) + L" ignored=" + std::to_wstring(r.ignored));
                BookmarksChanged();
            } else { failed(TR(import_item_bookmarks), c.identity == BI::Identity::Newer ? TR(import_map_newer) : TR(import_save_failed)); Log(L"IMPORT bookmarks save failed result=" + std::to_wstring(static_cast<int>(c.result)) + L" identity=" + std::to_wstring(static_cast<int>(c.identity))); }
        }
    }
    if (job->options.history) {
        const auto& r = job->result.history;
        if (r.failure != BI::Failure::None) { failed(TR(import_item_history), BI::FailureText(r.failure, s)); Log(L"IMPORT history read failed " + std::to_wstring(static_cast<int>(r.failure))); }
        else {
            auto c = BI::CommitHistory(dataDir, HistoryFile(), historyBase, history, r.visits, r.beyond, now, [](const std::wstring& u) { return SearchResultPage(u); });
            if (c.Saved()) {
                anySaved = true; c.counts.unreadable = r.ignored; if (c.counts.Left()) anyLeft = true;
                lines.push_back(TRF(import_result_history, BI::CountsText(c.counts)));
                Log(L"IMPORT history saved " + ImportCountsLog(c.counts) + L" ignored=" + std::to_wstring(r.ignored));
                if (!WorkspaceParked()) historyDrawer.Changed();
            } else { failed(TR(import_item_history), TR(import_save_failed)); Log(L"IMPORT history save failed result=" + std::to_wstring(static_cast<int>(c.result))); }
        }
    }
    if (!anySaved) {   // nothing changed anywhere: the dialog stays, with the reason, for a retry
        modal.SetBusy(false); modal.SetError(firstError);
        return;
    }
    if (s.browser != BI::Browser::HtmlFile) lines.push_back(TR(import_result_cookies));
    modal.Finish(MD::AcceptButton);
    MD::Spec r; r.kind = MD::Kind::Generic; r.scope = MD::Scope::Window; r.automationId = L"ImportResult";
    const bool partial = anyFailed || anyLeft;
    r.title = partial ? TR(import_partial_title) : TR(import_done_title);
    for (const auto& l : lines) { if (!r.body.empty()) r.body += L"\n"; r.body += l; }
    r.buttons = {MD::Make(MD::AcceptButton, TR(common_ok), MD::Role::Primary, true, true, true, L"DialogOk")};
    AskModal(std::move(r), nullptr, [](const MD::Result&) {});
    accessibility.Notify('S', (partial ? TR(import_partial_title) : TR(import_done_title)), L"Import");   // i18n-exempt: UIA activity id
}
void ShowImportDialog(bool keyboard) {
    if (smoke || privateWindow || importDialogOpen || importRunning) return;
    keyboardModality = keyboard;
    auto sources = std::make_shared<std::vector<BI::Source>>(BI::Discover(ImportRoots()));
    sources->push_back(BI::HtmlSource(L""));
    Log(L"IMPORT dialog: " + std::to_wstring(sources->size() - 1) + L" browser profiles found");
    MD::Spec s; s.kind = MD::Kind::Generic; s.scope = MD::Scope::Window; s.automationId = L"ImportBrowserData";
    s.title = TR(import_title); s.body = TRF(import_destination, ImportProfileName());
    s.hasSelect = true; s.select.label = TR(import_source_label); s.select.index = 0;
    for (const auto& src : *sources) s.select.options.push_back(BI::Label(src));
    s.select.checks = [sources](int i) { return ImportChecks((*sources)[static_cast<std::size_t>(std::clamp(i, 0, static_cast<int>(sources->size()) - 1))]); };
    s.checks = ImportChecks((*sources)[0]);
    s.buttons = {MD::Make(MD::CancelButton, TR(common_cancel), MD::Role::Secondary, false, true, false, L"DialogCancel"),
                 MD::Make(MD::AcceptButton, TR(import_action), MD::Role::Primary, true, false, true, L"DialogImport")};
    s.buttons[1].closes = false;
    s.onButton = [sources, destination = ImportDestination::Now()](int id) { if (id == MD::AcceptButton) StartImport(*sources, destination); };
    importDialogOpen = true;
    AskModal(std::move(s), nullptr, [](const MD::Result&) { importDialogOpen = false; });
}
