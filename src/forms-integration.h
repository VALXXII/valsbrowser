// #121 host side (part of main.cpp, included right after elements-integration.h): our list under a page field and our
// validation bubble, in place of the engine's « Informations enregistrées » list and « Insérez un symbole @ » bubble.
// Contract P1–P16: design/security-review-autofill-under-field.md. Pure parts: src/form-memory.h (tested in
// tests/form_memory_test.cpp); surfaces: src/field-list.h, src/validation-bubble.h.
//
// The world « browser-forms » (Forms::Script) is installed in the tab's OWN DevTools session (top document only,
// never a frame session), in every window (the bubble), and reports through a binding that only exists there.
// What the host does with a report:
//   type    → rows (vault accounts of the EXACT origin, then the e-mails the person sent), placed under the field inside
//             the page area, armed 500 ms; nothing reaches the page;
//   sel     → the highlighted row; pick → the fill, after every check of P9 (same tab, document, origin, armed);
//   save    → the e-mails typed and sent (P12), normal windows only, « Saisie automatique » on;
//   invalid → our bubble with our words (P14), any window.
#include "form-memory.h"
#include "field-list.h"
#include "validation-bubble.h"

NativeChrome::FieldList::List fieldList; NativeChrome::ValidationBubble::Bubble validationBubble;
// The e-mails the person sent (P12), read again only when the file changed (path and stamp of the last read).
Forms::Store formEntries, formEntriesBase; bool formEntriesUsable = false, formEntriesRead = false; std::wstring formEntriesPath; Vault::FileStamp formEntriesStamp;
std::wstring FormEntriesFile() { return dataDir + L"\\form-entries"; }
// P11: no list under the field (and nothing stored) in a private window; never while halted or in the smoke test.
bool FormsListWanted() { return !privateWindow && !smoke && !halted; }
bool FormsWanted() { return !smoke && !halted; }   // the bubble: every window
// Fresh read under the profile lock (lockMs 0 = never wait: the list keeps what it had). Damaged or unknown: unusable,
// never taken as empty, never rewritten (the commit refuses it as well).
bool FormsRead(DWORD lockMs) {
    if (!FormsListWanted() || dataDir.empty()) { formEntries = formEntriesBase = Forms::Store{}; formEntriesUsable = false; return false; }
    std::string bytes; bool read = false, exists = false; Vault::FileStamp stamp;
    { ProfileStore::Lock lock(dataDir, lockMs); if (!lock.Held()) return formEntriesUsable && formEntriesPath == FormEntriesFile();
      stamp = Vault::StampOf(FormEntriesFile()); exists = stamp.exists;
      read = stamp.size <= Forms::Store::MaxFileBytes && ProfileStore::ReadAll(FormEntriesFile(), bytes); }
    formEntriesRead = read; formEntriesPath = FormEntriesFile(); formEntriesStamp = stamp;
    bool damaged = false; std::istringstream in(bytes); Forms::Store s = Forms::Store::Read(in, &damaged);
    if (!read || damaged || (exists && bytes.empty())) { static bool said = false; if (!said) { said = true; Log(L"FORMS typed-info file unreadable or damaged: not used, never rewritten"); } formEntries = formEntriesBase = Forms::Store{}; formEntriesUsable = false; return false; }
    formEntries = s; formEntriesBase = std::move(s); formEntriesUsable = true; return true;
}
bool FormsCached() {
    const std::wstring path = FormEntriesFile();
    if (Vault::CacheStale(formEntriesRead, formEntriesPath, formEntriesStamp, path, Vault::StampOf(path))) FormsRead(0);
    return formEntriesUsable && formEntriesPath == path;
}
// A change of the e-mails (sent, picked, removed): applied on a fresh read, then the transaction (merge, fail closed).
bool FormsChange(const std::function<bool(Forms::Store&)>& change) {
    if (!FormsListWanted() || !FormsRead(1000)) return false;
    if (!change(formEntries)) return false;
    const auto r = Forms::Commit(dataDir, FormEntriesFile(), formEntriesBase, formEntries);
    const bool ok = r == ProfileStore::Result::Written || r == ProfileStore::Result::Unchanged;
    if (!ok) { formEntries = formEntriesBase; Log(L"FORMS typed info NOT saved (busy, damaged or unknown file)"); }
    formEntriesRead = false;   // the stamp changed: read again next time
    return ok;
}
// Clear on exit / full clear (with the engine's own form data, P13).
void FormsWipe() { if (!FormsListWanted() || dataDir.empty()) return; if (!Forms::Wipe(dataDir, FormEntriesFile(), formEntriesBase, formEntries)) Log(L"FORMS typed info NOT wiped (profile busy)"); formEntriesRead = false; }
// P13, once per profile: the e-mail values of the engine's « autofill » table, read from a private copy of « Web Data »
// before the engine starts (it holds the file while it runs: Busy, retried at the next start). Nothing is deleted.
void FormsEngineImport() {
    if (!FormsListWanted() || dataDir.empty() || profileDir.empty()) return;
    const std::wstring marker = dataDir + L"\\form-entries-engine";
    if (GetFileAttributesW(marker.c_str()) != INVALID_FILE_ATTRIBUTES) return;
    std::vector<Forms::EngineEmail> found;
    const auto r = Forms::ReadEngineEmails(profileDir + L"\\EBWebView\\Default\\Web Data", found);
    if (r == Forms::EngineImport::Busy) { Log(L"FORMS engine typed info: the engine holds its file, retried at the next start"); return; }
    if (r == Forms::EngineImport::Failed) { Log(L"FORMS engine typed info: unreadable, retried at the next start"); return; }
    if (r == Forms::EngineImport::Copied && !FormsChange([&](Forms::Store& s) { bool any = false; for (const auto& e : found) any = s.Add(e.value, e.usedMs) || any; return any; })) return;
    if (ProfileStore::WriteAtomic(marker, "done\n")) Log(L"FORMS engine typed info copied once (" + std::to_wstring(found.size()) + L" e-mails); the engine's own data is kept");
}

// ---- The world in each tab ------------------------------------------------------------------------------------------
std::set<unsigned> formsInstalled;
void FormsEvent(const std::shared_ptr<Tab>& t, std::wstring json, const std::wstring& session);
void FormsCall(const std::shared_ptr<Tab>& t, const wchar_t* method, const std::wstring& params, std::shared_ptr<bool> ok = nullptr) {
    auto done = Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler, HRESULT, LPCWSTR>([m = std::wstring(method), ok](HRESULT hr, LPCWSTR) -> HRESULT {
        if (FAILED(hr)) { if (ok) *ok = false; Log(L"FORMS setup failed: " + m + L" " + HrText(hr)); } return S_OK; });
    const HRESULT hr = t->view->CallDevToolsProtocolMethod(method, params.c_str(), done); done->Release();
    if (FAILED(hr)) { if (ok) *ok = false; Log(std::wstring(L"FORMS setup call failed: ") + method + L" " + HrText(hr)); }
}
// P16: its own world and binding, the tab's own session only (no frame auto-attach: the vault capture's stays as it is).
void FormsInstall(const std::shared_ptr<Tab>& t, const PopupHost::RequestPtr& popup) {
    if (!FormsWanted() || !t || !t->view.p || formsInstalled.count(t->serial)) return;
    std::weak_ptr<Tab> weak = t;
    auto receive = [&](const wchar_t* event, std::function<void(const std::shared_ptr<Tab>&, std::wstring, std::wstring)> f) {
        Com<ICoreWebView2DevToolsProtocolEventReceiver> r; if (FAILED(t->view->GetDevToolsProtocolEventReceiver(event, &r.p)) || !r.p) return false;
        auto h = Callback<ICoreWebView2DevToolsProtocolEventReceivedEventHandler, ICoreWebView2*, ICoreWebView2DevToolsProtocolEventReceivedEventArgs*>([weak, f](ICoreWebView2*, ICoreWebView2DevToolsProtocolEventReceivedEventArgs* a) -> HRESULT {
            auto tab = weak.lock(); if (halted || !tab || tab->closed || !a) return S_OK; LPWSTR j = nullptr; if (FAILED(a->get_ParameterObjectAsJson(&j)) || !j) return S_OK;
            std::wstring json = j; CoTaskMemFree(j);
            std::wstring session; { Com<ICoreWebView2DevToolsProtocolEventReceivedEventArgs2> a2; LPWSTR sid = nullptr; if (SUCCEEDED(a->QueryInterface(IID_ICoreWebView2DevToolsProtocolEventReceivedEventArgs2, (void**)&a2.p)) && a2.p && SUCCEEDED(a2->get_SessionId(&sid)) && sid) { session = sid; CoTaskMemFree(sid); } }
            f(tab, std::move(json), session); return S_OK; });
        EventRegistrationToken token{}; const bool ok = SUCCEEDED(r->add_DevToolsProtocolEventReceived(h, &token)); h->Release(); return ok; };
    // The binding is added again to the new document's world (the Runtime domain stays off, as the vault's, review V-5).
    auto rebind = [](const std::shared_ptr<Tab>& tab, std::wstring, std::wstring session) { if (session.empty() && tab->view.p && formsInstalled.count(tab->serial)) FormsCall(tab, L"Runtime.addBinding", Forms::BindingParams()); };
    const bool events = receive(L"Runtime.bindingCalled", [](const std::shared_ptr<Tab>& tab, std::wstring json, std::wstring session) { FormsEvent(tab, std::move(json), session); })
        && receive(L"Page.frameNavigated", rebind) && receive(L"Page.domContentEventFired", rebind);
    if (!events) { Log(L"FORMS world NOT installed: DevTools events unavailable (the engine's bubble stays)"); return; }
    formsInstalled.insert(t->serial);
    auto ok = std::make_shared<bool>(true);
    FormsCall(t, L"Page.enable", L"{}", ok);   // new-document scripts run only while the Page domain is enabled
    FormsCall(t, L"Runtime.addBinding", Forms::BindingParams(), ok);
    FormsCall(t, L"Page.addScriptToEvaluateOnNewDocument", Forms::ScriptParams(), ok);
    // A window.open target loads only once the script is in place (popup readiness barrier).
    ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler* barrier = PopupScriptBarrier(popup, L"forms");
    auto ready = Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler, HRESULT, LPCWSTR>([barrier, ok](HRESULT hr, LPCWSTR) -> HRESULT {
        if (SUCCEEDED(hr) && !*ok) hr = E_FAIL;
        Log(SUCCEEDED(hr) ? L"FORMS world installed (list under the field, validation bubble)" : L"FORMS world NOT installed: " + HrText(hr));
        if (barrier) { barrier->Invoke(SUCCEEDED(hr) ? S_OK : hr, L""); barrier->Release(); } return S_OK; });
    const HRESULT hr = t->view->CallDevToolsProtocolMethod(L"Runtime.runIfWaitingForDebugger", L"{}", ready); ready->Release();   // ordered after the setup calls
    if (FAILED(hr) && barrier) { popups.Finish(popup, false); barrier->Release(); }
}
// The host's calls into the world: Runtime.evaluate in the context id the ENGINE gave with the report.
void FormsEvaluate(const std::shared_ptr<Tab>& t, long long context, const std::wstring& expression, std::function<void(HRESULT, const std::wstring&)> done = nullptr) {
    if (!t || !t->view.p || context <= 0) { if (done) done(E_FAIL, L""); return; }
    auto h = Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler, HRESULT, LPCWSTR>([done](HRESULT hr, LPCWSTR json) -> HRESULT { if (done) done(hr, json ? json : L""); return S_OK; });
    const HRESULT hr = t->view->CallDevToolsProtocolMethod(L"Runtime.evaluate", Forms::EvaluateParams(context, expression).c_str(), h); h->Release();
    if (FAILED(hr) && done) done(hr, L"");
}

// ---- The list under the field ---------------------------------------------------------------------------------------
struct FieldListState {
    bool open = false; unsigned serial = 0; std::uint64_t nav = 0; long long context = 0; int seq = 0;
    std::wstring origin, typed; bool password = false, acOff = false; Forms::Rows rows; RECT at{}, view{}, field{}; ULONGLONG shownAt = 0; Vault::Target atAction;
} fieldState;
RECT TabScreenRect(const std::shared_ptr<Tab>& t) { RECT r = t ? t->placed : RECT{}; if (window) MapWindowPoints(window, nullptr, reinterpret_cast<POINT*>(&r), 2); return r; }
bool FormsForeground() {
    bool fg = GetForegroundWindow() == window;
#ifdef BROWSER_E2E_VAULT_HOST
    fg = true;   // TEST HOST ONLY (as VaultKeyArmed): the 500 ms arming still applies
#endif
    return fg;
}
bool FormsArmed() { return fieldState.open && Forms::Armed(fieldState.shownAt, GetTickCount64(), FormsForeground()); }
void FormsHideList(bool tellWorld) {
    if (!fieldState.open) return;
    fieldState.open = false; fieldList.Hide();
    if (tellWorld) { auto t = Active(); if (t && t->serial == fieldState.serial && t->navStamp == fieldState.nav) FormsEvaluate(t, fieldState.context, Forms::ClosedCall(fieldState.seq)); }
}
struct BubbleState { bool open = false; unsigned serial = 0; std::uint64_t nav = 0; RECT view{}; } bubbleState;
void FormsHideBubble() { if (!bubbleState.open) return; bubbleState.open = false; validationBubble.Hide(); }
void FormsHideAll() { FormsHideList(true); FormsHideBubble(); }
// From BuildChrome (tab switch, navigation, layout): P8, the list and the bubble belong to one tab, one document, one place.
void FormsSync() {
    if (!fieldState.open && !bubbleState.open) return;
    auto t = Active(); const RECT view = TabScreenRect(t);
    if (fieldState.open && (WorkspaceParked() || !t || t->serial != fieldState.serial || t->navStamp != fieldState.nav || !EqualRect(&fieldState.view, &view))) FormsHideList(false);
    if (bubbleState.open && (WorkspaceParked() || !t || t->serial != bubbleState.serial || t->navStamp != bubbleState.nav || !EqualRect(&bubbleState.view, &view))) FormsHideBubble();
}
NativeChrome::FieldList::Content FormsContent(const Forms::Rows& rows, const std::wstring& origin) {
    NativeChrome::FieldList::Content c;
    if (!rows.accounts.empty()) { c.accountsTitle = TRF(password_accounts, origin.substr(origin.find(L"://") + 3)); for (const auto& a : rows.accounts) c.accounts.push_back(a.user); }
    if (!rows.infos.empty()) { c.infosTitle = TR(forms_infos_title); c.infos = rows.infos; c.typed = rows.typed; }
    c.footer = !rows.accounts.empty() && !rows.infos.empty() ? TR(forms_manage_both) : !rows.accounts.empty() ? TR(settings_manage_passwords) : TR(forms_manage_autofill);
    return c;
}
// Shows (or refreshes) the list for the current field state; false when it fits nowhere (P2).
bool FormsPlaceList(const std::shared_ptr<Tab>& t, const RECT& field) {
    const auto content = FormsContent(fieldState.rows, fieldState.origin);
    const UINT dpi = GetDpiForWindow(window); const SIZE size = NativeChrome::FieldList::List::SizeFor(content, dpi);
    const RECT view = TabScreenRect(t); RECT at{};
    if (!Forms::Place(field, view, size.cx, size.cy, static_cast<int>(std::lround(NativeChrome::FieldList::Layout::anchorGap * dpi / 96.f)), at)) return false;
    fieldState.field = field;
    const bool moved = !fieldState.open || !EqualRect(&at, &fieldState.at);
    fieldState.at = at; fieldState.view = view; if (moved) fieldState.shownAt = GetTickCount64();   // P7: re-placed, re-armed
    fieldState.open = true; fieldList.Show(content, at, dpi);
    FormsEvaluate(t, fieldState.context, Forms::ShownCall(fieldState.seq, content.Count()));
    return true;
}
void FormsTyped(const std::shared_ptr<Tab>& t, const Forms::Report& r, long long context) {
    auto refuse = [&](const wchar_t* why) { if (fieldState.open) FormsHideList(false); FormsEvaluate(t, context, Forms::ClosedCall(r.seq)); if (why) Log(std::wstring(L"FORMS list not shown: ") + why); };
    if (!FormsListWanted()) return refuse(nullptr);
    if (t != Active() || t->closed || !t->view.p || t->home || !t->errorPage.shown.empty() || !t->https.shown.empty() || IsSandboxUri(t->uri)) return refuse(L"not the active page");   // i18n-exempt: log line only
    const std::wstring origin = Vault::CanonicalOrigin(EngineSource(t));
    if (origin.empty() || r.origin != origin) return refuse(nullptr);   // P4: a top-level https document of the tab's engine source
    std::vector<Forms::AccountRow> site; const bool passwords = settings.passwords && Vault::Active();
    if (passwords && VaultCached().first) for (const auto& e : vault.For(origin)) site.push_back({e.id, e.user});   // P11: no secret, the exact origin
    const bool infosOn = settings.autofill && !r.acOff;
    const auto rows = Forms::BuildRows(site, infosOn && FormsCached() ? &formEntries : nullptr, r.value, passwords, infosOn);
    if (rows.Empty()) return refuse(nullptr);
    const bool sameDocument = fieldState.open && fieldState.serial == t->serial && fieldState.nav == t->navStamp && fieldState.context == context;
    fieldState.serial = t->serial; fieldState.nav = t->navStamp; fieldState.context = context; fieldState.seq = r.seq; fieldState.origin = origin; fieldState.typed = r.value;
    fieldState.password = r.password; fieldState.acOff = r.acOff; fieldState.rows = rows;
    if (!sameDocument) fieldState.atAction = VaultTargetNow(t, true);   // typing is the person's action; checked again at the pick (Vault::CanFill)
    if (!FormsPlaceList(t, Forms::FieldScreen(r, TabScreenRect(t)))) return refuse(L"no room inside the page");   // i18n-exempt: log line only
}
void ShowSettingsAt(NativeChrome::SettingsUI::View v);
// The footer: « Gérer la saisie automatique » / « … et la saisie » → Réglages › Confidentialité; accounts only → B05.
void FormsManage(bool keyboard) {
    const bool accountsOnly = !fieldState.rows.accounts.empty() && fieldState.rows.infos.empty();
    FormsHideList(true);
    if (accountsOnly) ShowVaultManager(keyboard); else ShowSettingsAt(NativeChrome::SettingsUI::View::Privacy);
}
// P9: the value goes to the SAME element of the SAME document (the world checks it), or nothing happens.
void FormsFillValue(const std::shared_ptr<Tab>& t, const std::wstring& value, bool info) {
    const int seq = fieldState.seq; const long long context = fieldState.context; std::weak_ptr<Tab> weak = t; Workspace* owner = WorkspaceNow();
    fieldState.open = false; fieldList.Hide();
    FormsEvaluate(t, context, Forms::FillCall(seq, value), [weak, value, info, owner, seq, context](HRESULT hr, const std::wstring& json) {
        const auto a = Forms::ParseFillAnswer(hr, json.c_str());
        if (a != Forms::FillAnswer::Filled) { Log(L"FORMS fill not done: the field changed or moved"); if (auto tab = weak.lock()) FormsEvaluate(tab, context, Forms::ClosedCall(seq)); return; }
        Log(info ? L"FORMS filled: a saved e-mail (the person's choice)" : L"FORMS filled: an account's username (identifier step, no secret)");
        if (info && !WorkspaceDead(owner)) { WorkspaceScope scope(owner); FormsChange([&](Forms::Store& s) { return s.Touch(value, NativeChrome::DownloadsUI::UnixNowMs()); }); }
    });
}
void FormsPick(int row, bool keyboard) {
    auto t = Active();
    if (!FormsArmed()) { Log(L"FORMS pick ignored: the list is not armed (P7)"); return; }
    if (!t || t->closed || !t->view.p || t->serial != fieldState.serial || t->navStamp != fieldState.nav || !t->errorPage.shown.empty() || !t->https.shown.empty()
        || Vault::CanonicalOrigin(EngineSource(t)) != fieldState.origin) { Log(L"FORMS pick refused: the page changed (P9)"); FormsHideList(false); return; }
    const int accounts = static_cast<int>(fieldState.rows.accounts.size()), infos = static_cast<int>(fieldState.rows.infos.size());
    if (row < 0 || row > accounts + infos) return;
    if (row == accounts + infos) { FormsManage(keyboard); return; }
    if (row < accounts) {
        const auto account = fieldState.rows.accounts[static_cast<size_t>(row)];
        if (fieldState.password) { const auto origin = fieldState.origin; const auto atAction = fieldState.atAction; FormsHideList(true); VaultFillChosen(t, account.id, origin, atAction); return; }   // the B03 fill, unchanged
        FormsFillValue(t, account.user, false);   // P10: identifier-only step, no secret
        return;
    }
    FormsFillValue(t, fieldState.rows.infos[static_cast<size_t>(row - accounts)], true);
}
void FormsRemove(int row) {
    if (!FormsArmed()) return;
    const int accounts = static_cast<int>(fieldState.rows.accounts.size()), i = row - accounts;
    if (i < 0 || i >= static_cast<int>(fieldState.rows.infos.size())) return;
    const std::wstring value = fieldState.rows.infos[static_cast<size_t>(i)];
    if (!FormsChange([&](Forms::Store& s) { return s.Remove(value); })) return;
    Log(L"FORMS typed info removed (« retirer »)");
    auto t = Active(); if (!t) return;
    auto& infos = fieldState.rows.infos; infos.erase(std::remove(infos.begin(), infos.end(), value), infos.end());
    if (fieldState.rows.Empty() || t->serial != fieldState.serial || !FormsPlaceList(t, fieldState.field)) FormsHideList(true);   // same field, the list re-placed (and re-armed)
}
// P12: what the person typed and sent; normal windows, « Saisie automatique » on, the tab's own https document.
void FormsSaved(const std::shared_ptr<Tab>& t, const Forms::Report& r) {
    if (!FormsListWanted() || !settings.autofill || !t || t->closed) return;
    const std::wstring origin = Vault::CanonicalOrigin(EngineSource(t)); if (origin.empty() || r.origin != origin) return;
    static std::map<unsigned, ULONGLONG> last; const ULONGLONG now = GetTickCount64(); auto& at = last[t->serial]; if (at && now - at < 1000) return; at = now;
    std::vector<std::wstring> values; for (const auto& v : r.values) if (Forms::EmailShaped(v)) values.push_back(v);
    if (values.empty()) return;
    if (FormsChange([&](Forms::Store& s) { bool any = false; for (const auto& v : values) any = s.Add(v, NativeChrome::DownloadsUI::UnixNowMs()) || any; return any; })) Log(L"FORMS typed e-mail kept (" + std::to_wstring(values.size()) + L")");
}
// P14: our bubble under the first invalid field, any window, the tab's own top-level document.
void FormsInvalid(const std::shared_ptr<Tab>& t, const Forms::Report& r) {
    if (!FormsWanted() || t != Active() || t->closed || !t->view.p || WorkspaceParked()) return;
    if (Forms::OriginOf(EngineSource(t)) != r.origin || r.origin.empty()) return;
    const std::wstring text = Forms::InvalidText(r.message, r.argA, r.argB); if (text.empty()) { FormsHideBubble(); return; }
    const UINT dpi = GetDpiForWindow(window); const SIZE size = validationBubble.SizeFor(text, dpi);
    const RECT view = TabScreenRect(t), field = Forms::FieldScreen(r, view); RECT at{};
    if (!Forms::Place(field, view, size.cx, size.cy, static_cast<int>(std::lround(NativeChrome::ValidationBubble::Layout::anchorGap * dpi / 96.f)), at)) { FormsHideBubble(); return; }
    bubbleState = {true, t->serial, t->navStamp, view};
    validationBubble.Show(text, at);
    Log(L"FORMS validation bubble: " + r.message);
}
void FormsEvent(const std::shared_ptr<Tab>& t, std::wstring json, const std::wstring& session) {
    if (!session.empty() || !t || t->closed || WorkspaceParked()) return;   // P4: the tab's own session only (never a frame's)
    Forms::Report r; long long context = 0;
    if (!Forms::ParseReport(json, r, context)) return;   // another binding (the vault's), or not ours
    switch (r.kind) {
    case Forms::Report::Kind::Type: FormsTyped(t, r, context); break;
    case Forms::Report::Kind::Select:
        if (fieldState.open && t->serial == fieldState.serial && context == fieldState.context && r.seq == fieldState.seq) fieldList.SetSelected(r.index);
        break;
    case Forms::Report::Kind::Pick:
        if (fieldState.open && t->serial == fieldState.serial && context == fieldState.context && r.seq == fieldState.seq) FormsPick(r.index, true);
        break;
    case Forms::Report::Kind::Remove:   // Suppr on the row chosen with ↓ (board « états »): an e-mail only, armed (FormsRemove)
        if (fieldState.open && t->serial == fieldState.serial && context == fieldState.context && r.seq == fieldState.seq) FormsRemove(r.index);
        break;
    case Forms::Report::Kind::Close:
        if (fieldState.open && t->serial == fieldState.serial && context == fieldState.context && r.seq == fieldState.seq) FormsHideList(false);
        break;
    case Forms::Report::Kind::Save: FormsSaved(t, r); break;
    case Forms::Report::Kind::Invalid: FormsInvalid(t, r); break;
    case Forms::Report::Kind::Unbubble: if (bubbleState.open && t->serial == bubbleState.serial) FormsHideBubble(); break;
    }
}
void FormsTabClosed(const std::shared_ptr<Tab>& t) { if (!t) return; formsInstalled.erase(t->serial); if (fieldState.open && fieldState.serial == t->serial) FormsHideList(false); if (bubbleState.open && bubbleState.serial == t->serial) FormsHideBubble(); }
void CreateFormsSurfaces() {
    NativeChrome::FieldList::Hooks h;
    h.armed = [] { return FormsArmed(); };
    h.pick = [](int row) { FormsPick(row, false); };
    h.remove = [](int row) { FormsRemove(row); };
    if (!fieldList.Create(instance, window, base, std::move(h))) Log(L"FORMS list unavailable");
    if (!validationBubble.Create(instance, window, base)) Log(L"FORMS bubble unavailable");
}
