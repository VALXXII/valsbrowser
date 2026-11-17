#pragma once
// Chat intégré — the panel (open / close / layout), the picker, the folder, the history and the panel's messages
// (included by src/chat-host.h; see its header for the whole picture).
namespace Chat {

void OpenPanel(); void ClosePanel(); void NewConversation(); void MaybeStartSession(); void SendHistory(const std::string& query);
// Keyboard focus back to the page of the chat. Never through the host's FocusPage(): its Check() treats a refusal of
// MoveFocus as fatal (it stops the whole browser), and WebView2 does refuse while a modal owns the window.
void FocusChatPage() {
    auto t = ChatTab(); if (!t || t->closed || !t->controller.p || halted || t != Active() || !PageShown(*t)) return;
    if (GetForegroundWindow() != window || !IsWindowEnabled(window)) return;
    SetFocus(window); t->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);   // result ignored on purpose
}

// ---------------------------------------------------------------- images
// A PNG data: URL of at most maxW x maxH from any image WIC can read (the chip's thumbnail; contract B: <= 256 KiB).
std::string ThumbDataUrl(const std::string& bytes, unsigned maxW = 192, unsigned maxH = 128) {
    namespace D = AnnotationCapture::detail; D::Com com; D::Ptr<IWICImagingFactory> wic; if (!D::Factory(wic)) return "";
    D::Ptr<IStream> in; in.p = SHCreateMemStream(reinterpret_cast<const BYTE*>(bytes.data()), UINT(bytes.size())); if (!in.p) return "";
    D::Ptr<IWICBitmapDecoder> dec; D::Ptr<IWICBitmapFrameDecode> frame; UINT w = 0, h = 0;
    if (FAILED(wic->CreateDecoderFromStream(in.p, nullptr, WICDecodeMetadataCacheOnDemand, &dec.p)) || FAILED(dec->GetFrame(0, &frame.p)) || FAILED(frame->GetSize(&w, &h)) || !w || !h || w > 20000 || h > 20000) return "";
    const double k = std::min(1.0, std::min(double(maxW) / w, double(maxH) / h)); const UINT tw = std::max<UINT>(1, UINT(std::lround(w * k))), th = std::max<UINT>(1, UINT(std::lround(h * k)));
    D::Ptr<IWICFormatConverter> conv; if (FAILED(wic->CreateFormatConverter(&conv.p)) || FAILED(conv->Initialize(frame.p, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return "";
    D::Ptr<IWICBitmapScaler> scaler; if (FAILED(wic->CreateBitmapScaler(&scaler.p)) || FAILED(scaler->Initialize(conv.p, tw, th, WICBitmapInterpolationModeFant))) return "";
    std::string png; if (!D::Encode(wic.p, scaler.p, png) || png.size() > 180 * 1024) return "";
    return "data:image/png;base64," + Base64(png);
}

// ---------------------------------------------------------------- the page (queries through the mode's script)
void Query(const std::shared_ptr<Tab>& t, const std::wstring& what, const std::wstring& arg, std::function<void(const std::string&)> done) {
    if (!t || !t->view.p || t->closed || !Ann::s.active || Ann::ModeTab() != t || !Ann::s.injected) { if (done) done(""); return; }
    auto cb = Callback<ICoreWebView2ExecuteScriptCompletedHandler, HRESULT, LPCWSTR>([done](HRESULT hr, LPCWSTR result) -> HRESULT {
        std::string out; if (SUCCEEDED(hr) && result) { SJ::Value v; if (SJ::Parse(U8(result), v) && v.IsString()) out = v.string; }
        if (done) done(out); return S_OK; });
    if (FAILED(t->view->ExecuteScript(AUI::PageQuery(W(Ann::s.nonce), what, arg).c_str(), cb)) && done) done("");
    cb->Release();
}
// state: "working" (Paper 6.10: « ⟳ titre - modèle » above the element), "done" (6.11: frame + check), "clear".
void ApplyMark(const char* state, const std::string& selector) {
    auto t = ChatTab(); if (!t || t != Ann::ModeTab()) return;
    SJ::Value a = SJ::Value::Obj(); a.Set("state", Str(std::string(state))); a.Set("selector", Str(selector));
    a.Set("title", Str(ChatPrompt::OneLine(convo.data.head.title.empty() ? T8(I18n::Id::chat_title_new) : convo.data.head.title, 32))); a.Set("sub", Str(LabelOf(Acp::FindModelOption(agentInfo[agentId].options))));
    const std::string what = state;
    Query(t, L"mark", W(SJ::Serialize(a)), [what](const std::string& r) { if (what != "clear") Diag(L"mark « " + W(what) + L" » on the element: " + (r == "true" ? L"found" : L"not found (no mark)")); });
}
// After a turn that changed files: the page must show them. A dev server that reloads by itself (HMR client in the
// page, or a navigation that already happened during the turn) is left alone; otherwise the chat reloads the page.
void AfterTurnPage(bool changed) {
    auto t = ChatTab(); if (!t || t->closed || !t->view.p) return;
    if (!changed) { ApplyMark("clear", ""); return; }
    if (ChatLocal::Classify(U8(t->uri)).origin != site.origin) return;   // the tab went elsewhere: nothing to reload
    if (t->navStamp != turnNavStamp || rearmOwed) { Diag(L"page already reloaded during the turn (dev server): no reload"); if (!rearmOwed && !doneMarkSelector.empty()) ApplyMark("done", doneMarkSelector); return; }
    std::weak_ptr<Tab> weak = t;
    auto cb = Callback<ICoreWebView2ExecuteScriptCompletedHandler, HRESULT, LPCWSTR>([weak](HRESULT hr, LPCWSTR result) -> HRESULT {
        auto tab = weak.lock(); if (!tab || tab->closed || !tab->view.p) return S_OK;
        const bool hmr = SUCCEEDED(hr) && result && wcscmp(result, L"true") == 0;
        if (hmr) { Diag(L"dev server with hot reload detected: no reload"); if (post) SetTimer(post, MarkTimer, 700, nullptr); }
        else { Diag(L"files changed and no dev server reload: reloading the page"); tab->view->Reload(); }
        return S_OK; });
    const wchar_t* probe = L"(function(){try{return !!(document.querySelector('script[src*=\"/@vite/client\"],script[src*=\"webpack-dev-server\"],script[src*=\"/_next/static/chunks/webpack\"],astro-dev-toolbar')"
                           L"||window.__vite_plugin_react_preamble_installed__||window.webpackHotUpdate||window.__webpack_hash__||window.__NEXT_DATA__||window.$RefreshReg$)}catch(e){return false}})()";
    if (FAILED(t->view->ExecuteScript(probe, cb))) t->view->Reload();
    cb->Release();
}
void DoRearm() {
    if (post) KillTimer(post, RearmTimer);
    if (!rearmOwed) return; rearmOwed = false; rearmTries = 0;
    auto t = ChatTab(); if (!t || t != Ann::ModeTab() || !Ann::s.active) return;
    if (ChatLocal::Classify(U8(t->uri)).origin != site.origin) { Ann::Exit(L"navigation", true); return; }   // another site: the mode (and the panel) end, as before
    Ann::Rearm(t, [](bool ok) {
        if (!ok) return;
        if (turn.active && !turn.markSelector.empty()) ApplyMark("working", turn.markSelector);
        else if (!doneMarkSelector.empty()) ApplyMark("done", doneMarkSelector);
    });
}

// ---------------------------------------------------------------- aiming from the chat
Element ElementFromMode(std::uint64_t serial) {
    Element e; e.id = "el" + std::to_string(++elementSeq); e.serial = serial; e.sel = Ann::s.sel; e.pending = Ann::s.pending; const auto& t = e.pending.target;
    e.label = t.path.empty() ? t.tag : t.path.back(); e.selector = t.selector;
    std::string type; for (const auto& a : t.attributes) if (a.name == "type") type = a.value;
    e.kind = ChatPrompt::KindOf(t.tag, t.role, type);
    e.text = ChatPrompt::OneLine(t.selectionText ? *t.selectionText : (t.text.empty() ? t.accessibleName : t.text), 120); e.hint = ChatPrompt::OneLine(e.text, 40);
    return e;
}
void ResolveElementSource(Element& e) {
    if (folder.empty()) return;
    e.nearest = {};
    e.source = ChatPrompt::ResolveSource(e.rawSource, U8(folder), [](const std::string& abs) { const DWORD a = GetFileAttributesW(W(abs).c_str()); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY); }, &e.nearest);
}
void MaybeAttach(std::uint64_t serial) {
    auto it = aiming.find(serial); if (it == aiming.end() || !it->second.captureDone || !it->second.sourceDone) return;
    Element e = std::move(it->second); aiming.erase(it); const std::string id = e.id;
    Diag(L"element aimed: " + W(e.label) + (e.source.known ? L" — source " + W(e.source.Full()) + L" (" + W(e.source.from) + L")" : e.nearest.known ? L" — no source of its own (nearest ancestor " + W(e.nearest.Full()) + L")" : L" — no source") + (e.png.empty() ? L", no image" : L", image " + std::to_wstring(e.png.size()) + L" bytes"));
    if (convo.data.head.source.empty() && e.source.known && convo.data.items.empty()) { convo.data.head.source = e.source.Display(); SendState(); }
    SJ::Value m = SJ::Value::Obj(); m.Set("element", ElementRef(e)); composerElements[id] = std::move(e); Send("element.attached", std::move(m));
    {   SJ::Value p = SJ::Value::Obj(); p.Set("active", SJ::Value::Bool(picking && !turn.active)); Send("pick.state", std::move(p)); }
    if (!pendingAsk.empty() && Usable() && !turn.active) { const std::string text = U8(pendingAsk); pendingAsk.clear(); StartTurn(text, {id}, {}); }
}
void QuerySource(const std::shared_ptr<Tab>& t, std::uint64_t serial) {
    Query(t, L"source", L"null", [serial](const std::string& json) {
        auto it = aiming.find(serial); if (it == aiming.end()) return;
        auto& e = it->second;
        if (!json.empty() && ChatPrompt::ParsePageSource(json, e.rawSource)) { e.components = e.rawSource.components; ResolveElementSource(e); }
        e.sourceDone = true; MaybeAttach(serial);
    });
}
void TakeImage(std::uint64_t serial, const std::string& png, double scale, const std::string& crop) {
    auto it = aiming.find(serial); if (it == aiming.end()) return;
    it->second.png = png; it->second.shotScale = scale; it->second.shotCrop = crop;
    auto thumb = std::make_shared<std::string>(); auto copy = std::make_shared<std::string>(png);
    Ann::Run([thumb, copy] { *thumb = ThumbDataUrl(*copy); }, [thumb, serial] { if (auto e = aiming.find(serial); e != aiming.end()) { e->second.thumb = *thumb; e->second.captureDone = true; MaybeAttach(serial); } });
}
bool TakesSelection() { return Open() && panelReady && picking && site.local && !site.file && !turn.active && composerElements.size() + aiming.size() < 10; }
void OnSelection(const std::shared_ptr<Tab>& t, std::uint64_t serial, bool captureImpossible) {
    Element e = ElementFromMode(serial); e.captureDone = captureImpossible; aiming[serial] = std::move(e);
    if (!doneMarkSelector.empty()) { doneMarkSelector.clear(); ApplyMark("clear", ""); }
    QuerySource(t, serial);
}
bool OnCaptured(std::uint64_t serial, const AnnotationCapture::Result& r) {
    auto it = aiming.find(serial); if (it == aiming.end()) return false;
    if (r.ok && !r.png.empty()) TakeImage(serial, r.png, r.scale, r.crop); else { it->second.captureDone = true; MaybeAttach(serial); }
    return true;
}
bool AskAvailable(const std::shared_ptr<Tab>& t) { return t && !privateWindow && ChatLocal::IsLocal(U8(t->uri)) && !ChatLocal::Classify(U8(t->uri)).file; }
// « Demander à l'agent » in the note panel: the selection (and its image, ready or still coming) moves to the chat.
void AskFromNote(const std::wstring& draft) {
    auto t = Ann::ModeTab(); if (!t || !Ann::s.active) return;
    const std::uint64_t serial = Ann::s.serial; const Ann::Cap cap = Ann::s.cap;
    Element e = ElementFromMode(serial); e.captureDone = cap != Ann::Cap::Pending && cap != Ann::Cap::Ready;
    const std::string png = cap == Ann::Cap::Ready ? Ann::s.png : std::string(); const double scale = Ann::s.shotScale; const std::string crop = Ann::s.shotCrop;
    aiming[serial] = std::move(e);
    QuerySource(t, serial);
    if (!png.empty()) TakeImage(serial, png, scale, crop);
    std::wstring text = draft; while (!text.empty() && iswspace(text.back())) text.pop_back(); while (!text.empty() && iswspace(text.front())) text.erase(0, 1);
    pendingAsk = text;
    Ann::s.draft.clear(); Ann::note.Close(true); Ann::Resume();
    if (!Open()) OpenPanel(); else { SJ::Value f = SJ::Value::Obj(); f.Set("target", Str(std::string("composer"))); Send("focus", std::move(f)); view.Focus(); }
    Diag(L"« Demander à l'agent » from the note panel" + std::wstring(text.empty() ? L" (no text: the element only)" : L" (with its text)"));
}

// ---------------------------------------------------------------- site -> folder
void LoadProjects() { if (!projectsLoaded) { projects = ChatStore::Projects::Load(ProjectsFile()); projectsLoaded = true; } }
void SaveProjects() {
    auto copy = std::make_shared<ChatStore::Projects>(projects); const std::wstring file = ProjectsFile();
    Ann::Run([copy, file] { ChatStore::EnsureDir(file.substr(0, file.find_last_of(L'\\'))); copy->Save(file); }, nullptr);
}
void RememberChoices() {
    if (site.origin.empty() || folder.empty()) return;
    auto& p = projects.sites[site.origin]; p.folder = folder; p.agentId = agentId; p.usedAt = BA::SystemNow(); SaveProjects();
}
void SetFolder(const std::wstring& path) {
    const std::wstring dir = ChatFolder::detail::Trim(path);
    if (dir.empty() || !ChatFolder::detail::IsDir(dir) || ChatFolder::detail::Refused(dir)) { Problem p; p.title = TR(chat_err_folder_title); p.message = TR(chat_err_folder); p.retry = false; SendError(p, "", "composer", false); return; }
    if (dir != folder) DisposeSession(L"project folder changed");   // i18n-exempt: log reason
    folder = dir; RememberChoices();
    for (auto& e : composerElements) ResolveElementSource(e.second);
    convo.data.head.folder = folder;
    Diag(L"site " + W(site.origin) + L" linked to " + folder);
    SendState(); MaybeStartSession();
    if (!pendingAsk.empty() && !composerElements.empty() && !turn.active) { const std::string text = U8(pendingAsk); pendingAsk.clear(); std::vector<std::string> ids; for (const auto& e : composerElements) ids.push_back(e.first); StartTurn(text, ids, {}); }
}
void DetectFolder() {
    if (suggestionAsked || !site.local || site.file || !folder.empty()) return;
    suggestionAsked = true; auto out = std::make_shared<ChatFolder::Guess>(); const int port = site.port;
    Ann::Run([out, port] { *out = ChatFolder::Detect(port); },
             [out] { suggestion = *out;
                     Diag(suggestion.folder.empty() ? L"no folder suggestion: " + suggestion.why : L"folder suggestion from the process listening on the site's port (pid " + std::to_wstring(suggestion.pid) + L"): " + suggestion.folder);
                     SendState(); });
}
int CountPending() { return pendingAnnotations; }
void RefreshPending() {
    if (!site.local || site.origin.empty()) return;
    auto n = std::make_shared<int>(0); const std::string origin = site.origin;
    Ann::Run([n, origin] { BA::ListQuery q; q.site = origin; q.status = BA::Status::Open; *n = int(Ann::Store().List(q).entries.size()); }, [n] { if (*n != pendingAnnotations) { pendingAnnotations = *n; SendState(); } });
}

// ---------------------------------------------------------------- conversations
void RebuildTurnMaps() {
    checkpointOfTurn.clear(); snapshotOfTurn.clear(); convo.turnSeq = 0;
    for (const auto& item : convo.data.items) {
        const auto* ty = item.Get("type"); const auto* id = item.Get("turnId"); if (!ty || !id || !id->IsString()) continue;
        if (id->string.size() > 1 && id->string[0] == 't') convo.turnSeq = std::max(convo.turnSeq, atoi(id->string.c_str() + 1));
        if (ty->string != "turn.end") continue;
        if (const auto* c = item.Get("checkpointId"); c && c->IsString()) checkpointOfTurn[id->string] = c->string;
        if (const auto* s = item.Get("snapshotTurn"); s && s->IsString()) snapshotOfTurn[id->string] = s->string;
    }
}
void NewConversation() {
    DisposeSession(L"new conversation");   // i18n-exempt: log reason
    convo = Convo{}; auto& h = convo.data.head; h.id = NewId("c_"); h.origin = site.origin; h.siteLabel = site.label; h.agentId = agentId; h.folder = folder; h.createdAt = h.updatedAt = BA::SystemNow();
    queue.clear(); pendingAsk.clear(); RebuildTurnMaps(); doneMarkSelector.clear();   // the composer (text, chips, images) is the user's draft: it stays
    ClearStrip("auth"); ClearStrip("undo"); waitingPermissions.clear();
    SendState(); SendThread(); SendQueue();
}
void SendHistory(const std::string& query) {
    auto out = std::make_shared<std::vector<ChatStore::Header>>();
    Ann::Run([out] { *out = History().List(); },
             [out, query] {
                 std::wstring needle = W(query); for (auto& c : needle) c = towlower(c);
                 SJ::Value items = SJ::Value::Arr();
                 for (const auto& h : *out) {
                     if (h.origin != site.origin) continue;   // V1: the conversations of this site
                     std::wstring title = W(h.title); for (auto& c : title) c = towlower(c);
                     if (!needle.empty() && title.find(needle) == std::wstring::npos) continue;
                     SJ::Value o = SJ::Value::Obj(); o.Set("id", Str(h.id)); o.Set("title", Str(h.title.empty() ? T8(I18n::Id::chat_title_new) : h.title));
                     if (!h.source.empty()) o.Set("subtitle", Str(h.source));
                     o.Set("siteLabel", Str(h.siteLabel)); o.Set("thisSite", SJ::Value::Bool(true)); o.Set("agentIcon", Str(std::string(AgentOf(h.agentId) ? AgentOf(h.agentId)->icon : "generic")));
                     o.Set("updatedAt", Str(Iso(h.updatedAt))); o.Set("running", SJ::Value::Bool(turn.active && h.id == convo.data.head.id)); o.Set("current", SJ::Value::Bool(h.id == convo.data.head.id));
                     items.Push(std::move(o));
                 }
                 SJ::Value m = SJ::Value::Obj(); m.Set("items", std::move(items)); if (!query.empty()) m.Set("query", Str(query)); Send("history", std::move(m));
             });
}
void SelectHistory(const std::string& id) {
    if (id == convo.data.head.id) { SendState(); SendThread(); return; }
    auto out = std::make_shared<ChatStore::Conversation>(); auto ok = std::make_shared<bool>(false);
    Ann::Run([out, ok, id] { *ok = History().Load(id, *out); },
             [out, ok] {
                 if (!*ok || out->head.origin != site.origin || turn.active) return;
                 DisposeSession(L"another conversation"); convo = Convo{}; convo.data = std::move(*out); convo.preambleSent = convo.data.head.promptVersion == ChatPrompt::Version;   // i18n-exempt: log reason
                 if (AgentOf(convo.data.head.agentId)) agentId = convo.data.head.agentId;
                 queue.clear(); RebuildTurnMaps();
                 SendState(); SendThread(); SendQueue(); MaybeStartSession();
                 Diag(L"conversation reopened: " + W(convo.data.head.id) + L" (" + std::to_wstring(convo.data.items.size()) + L" items)");
             });
}
void MaybeStartSession() {
    const auto& a = agentInfo[agentId];
    if (Usable() && Open() && !session && !sessionStarting && !preparing && a.known && a.cli && a.prepared) StartSession();
}
void SwitchAgent(const std::string& id, const std::string& model) {
    if (!AgentOf(id)) return;
    auto go = [id, model] {
        agentId = id; RememberChoices();
        if (!convo.data.items.empty()) NewConversation(); else { DisposeSession(L"agent changed"); convo.data.head.agentId = id; convo.data.head.sessionId.clear(); }   // i18n-exempt: log reason
        pendingModel = model; SendState(); DetectAgents([] { MaybeStartSession(); });
    };
    if (turn.active) { queue.clear(); afterTurn = go; StopTurn(); } else go();
}
void Connect(const std::string& id, const std::string& methodId) {
    const auto* def = AgentOf(id); if (!def) return;
    auto& info = agentInfo[id]; const Acp::AuthMethod* method = nullptr;
    for (const auto& m : info.auth) if (!methodId.empty() && m.id == methodId) method = &m;
    if (!method) method = AgentRuntime::PreferredSignIn(info.auth);
    if (!method) { if (id == agentId) { session.reset(); MaybeStartSession(); if (!session) StartSession(); } return; }   // no method known yet: a new start tells
    if (!method->terminal) {
        if (session && id == agentId && session->Authenticate(method->id)) { sessionStarting = true; SetAgentStatus(id, "starting", TR(chat_agent_starting)); }
        return;
    }
    // a terminal method: the CLI's own sign-in in a visible console; the session starts again once it closes
    auto signIn = std::make_shared<AgentRuntime::SignIn>(); auto error = std::make_shared<std::string>(); const Acp::AuthMethod copy = *method; const auto rt = def->rt;
    SetAgentStatus(id, "starting", TR(chat_agent_starting));
    Ann::Run([signIn, error, copy, rt] { *signIn = AgentRuntime::RunTerminalSignIn(RtEnv(), rt, copy, error.get()); if (signIn->Started()) signIn->Wait(15 * 60 * 1000); },
             [signIn, error, id] { Diag(L"terminal sign-in " + W(id) + (signIn->Started() ? L": console closed" : L": could not start — " + W(*error)));
                                   signIn->Close(); if (id == agentId && !sessionReady) { session.reset(); sessionStarting = false; if (Usable()) StartSession(); else DetectAgents(); } });
}

// ---------------------------------------------------------------- panel window, layout, motion
LRESULT CALLBACK PostProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == AcpWake) { PumpEvents(); return 0; }
    if (m == WM_APP + 22) { PrepareProgress(); return 0; }
    if (m == WM_TIMER) {
        if (w == DestroyTimer) { KillTimer(h, DestroyTimer); if (panelState == PanelState::Closed && view.Exists()) { view.Destroy(); panelReady = false; Diag(L"panel WebView destroyed (closed for " + std::to_wstring(PanelIdleMs()) + L" ms)"); } return 0; }
        if (w == AgentIdleTimer) { KillTimer(h, AgentIdleTimer); if (!turn.active && session) DisposeSession(L"idle"); return 0; }   // i18n-exempt: log reason
        if (w == RearmTimer) { auto t = ChatTab(); if (!rearmOwed || !t) { KillTimer(h, RearmTimer); rearmOwed = false; } else if (t->navInFlight == 0 || ++rearmTries > 60) DoRearm(); return 0; }
        if (w == MarkTimer) { KillTimer(h, MarkTimer); if (!doneMarkSelector.empty()) ApplyMark("done", doneMarkSelector); return 0; }
    }
    return DefWindowProcW(h, m, w, l);
}
bool EnsurePost() {
    if (post) return true;
    WNDCLASSEXW wc{sizeof(wc)}; wc.lpfnWndProc = PostProc; wc.hInstance = instance; wc.lpszClassName = L"BrowserChatPost"; RegisterClassExW(&wc);
    post = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, nullptr);
    return post != nullptr;
}
RECT PanelRect(float shown) {   // client pixels; shown 0 (off the right edge) .. 1 (in place)
    RECT c{}; GetClientRect(window, &c); const int w = S(PanelDip()); const int left = c.right - w + int(std::lround((1 - shown) * w));
    return {left, S(chrome.ContentTop()), left + w, c.bottom};
}
void FinishClose();
struct SlideClient : AUI::Animator::Client {
    bool Frame(NativeChrome::Motion::Time now) override {
        if (!Shown()) return false;
        view.Place(PanelRect(slide.Value(now)));
        if (slide.Moving(now)) return true;
        if (panelState == PanelState::Opening) { panelState = PanelState::Open; Diag(L"panel in place " + Ann::Ms(Ann::NowMs() - stats.requestedAt) + L" ms after the request"); }
        else if (panelState == PanelState::Closing) FinishClose();
        return false;
    }
} slideClient;
void Layout() { if (Shown() && view.Exists()) view.Place(PanelRect(slide.Value(NativeChrome::Motion::Now()))); }
void SyncChromeBits() {
    if (!Ann::uiReady) return;
    Ann::banner.SetChat(Ann::s.active, Open());
    if (Ann::s.active && Ann::banner.Visible() && !Ann::connect.Visible()) Ann::banner.SetReminder(!Ann::agents.any() && !Open());   // the MCP reminder steps back while the chat is open
}
void OnPanelMessage(const std::string& json);
void EnsureInit() {
    if (uiInit) return; uiInit = true; EnsurePost();
    ChatPanelView::Hooks h;
    h.message = [](const std::string& json) { OnPanelMessage(json); };
    h.created = [](bool ok) { stats.createdAt = Ann::NowMs(); Diag(ok ? L"panel WebView created in " + Ann::Ms(stats.createdAt - stats.requestedAt) + L" ms" : L"panel WebView could not be created"); if (!ok) ClosePanel(); else Layout(); };
    h.accelerator = [](int vk) { const int cmd = Key(UINT(vk)); if (cmd) { view.AcceleratorHandled(); PostMessageW(window, Shortcut, WPARAM(cmd), 0); } };
    h.crashed = [] { Diag(L"panel renderer failed: the panel is rebuilt at the next opening"); panelReady = false; if (Open()) ClosePanel(); view.Destroy(); };
    std::wstring exe = Ann::BrowserExe(); const auto slash = exe.find_last_of(L'\\');
    view.Init(std::move(h), (slash == std::wstring::npos ? std::wstring(L".") : exe.substr(0, slash)) + L"\\assets", !Ann::EnvVar(L"BROWSER_CHAT_DEVTOOLS").empty() && TestMode());
}
void OpenPanel() {
    auto t = Active(); if (!t || !Ann::s.active || Ann::ModeTab() != t || halted || privateWindow) return;
    if (Open()) return;
    EnsureInit(); stats = OpenStats{}; stats.requestedAt = Ann::NowMs();
    const bool sameTab = ChatTab() == t; chatTab = t;
    const auto now = ChatLocal::Classify(U8(t->uri));
    if (now.origin != site.origin || !sameTab) { suggestion = {}; suggestionAsked = false; }
    site = now; LoadProjects(); folder.clear();
    if (site.local && !site.file) if (auto it = projects.sites.find(site.origin); it != projects.sites.end()) {
        if (ChatFolder::detail::IsDir(it->second.folder)) folder = it->second.folder;
        if (!turn.active && convo.data.items.empty() && AgentOf(it->second.agentId)) agentId = it->second.agentId;
        pendingMode.clear();
    }
    if (!turn.active && (convo.data.head.id.empty() || convo.data.head.origin != site.origin)) NewConversation();
    if (post) KillTimer(post, DestroyTimer);
    if (Ann::connect.Visible()) Ann::connect.Close(false);   // the chat was asked for: the « Connecter un agent » modal (MCP) steps back
    const bool reduced = Ann::Reduced(); const auto at = NativeChrome::Motion::Now();
    panelState = PanelState::Opening; slide.Snap(reduced ? 1.f : 0.f); slide.Set(1.f, at, reduced ? 0 : SlideInMs, AUI::Curves::band);
    if (!view.Usable(environment.p)) { view.Destroy(); panelReady = false; if (!view.Create(environment.p, window, PanelRect(slide.Value(at)))) { panelState = PanelState::Closed; Diag(L"panel WebView creation refused"); return; } }
    view.Show(true);
    ::Layout();   // the page shrinks at once (one layout); the panel then slides over the canvas
    picking = site.local && !site.file;
    Ann::LauncherSync(); SyncChromeBits();
    if (reduced) panelState = PanelState::Open; else AUI::Animator::Get().Request(&slideClient);
    DetectAgents([] { MaybeStartSession(); }); DetectFolder(); RefreshPending();
    if (panelReady) { SendState(); SJ::Value p = SJ::Value::Obj(); p.Set("active", SJ::Value::Bool(picking && !turn.active)); Send("pick.state", std::move(p)); SJ::Value f = SJ::Value::Obj(); f.Set("target", Str(std::string("composer"))); Send("focus", std::move(f)); view.Focus(); }
    Ann::Announce(site.local && !site.file ? std::wstring(TR(chat_announce_open)) : TRF(chat_announce_unavailable, site.file ? TR(chat_disabled_file) : TR(chat_disabled_not_local)));
    Diag(L"panel opened on " + W(site.origin) + (site.local ? L"" : L" (not local: disabled)") + (folder.empty() ? L", no folder yet" : L", folder " + folder));
}
void FinishClose() {
    panelState = PanelState::Closed; view.Show(false);
    ::Layout(); Ann::LauncherSync(); SyncChromeBits();
    if (post) SetTimer(post, DestroyTimer, PanelIdleMs(), nullptr);
    ArmAgentIdle();
}
void ClosePanel() {
    if (!Open()) return;
    const bool reduced = Ann::Reduced(); picking = false; aiming.clear();
    panelState = PanelState::Closing; slide.Set(0.f, NativeChrome::Motion::Now(), reduced ? 0 : SlideOutMs, AUI::Curves::band);
    if (reduced || !view.Exists()) FinishClose(); else AUI::Animator::Get().Request(&slideClient);
    Ann::Announce(TR(chat_announce_closed)); Diag(L"panel closed");
    if (Ann::s.active) FocusChatPage();
}
void Toggle(bool keyboard) {
    (void)keyboard;
    if (Open()) { ClosePanel(); return; }
    if (!Ann::s.active) { AnnotationCommand(AnnotateCommand); if (!Ann::s.active) return; }
    else if (Ann::ModeTab() != Active()) return;
    OpenPanel();
}
void ModeEntered() { SyncChromeBits(); }
void ModeExited() { rearmOwed = false; if (post) { KillTimer(post, RearmTimer); KillTimer(post, MarkTimer); } if (Open()) ClosePanel(); if (Ann::uiReady) Ann::banner.SetChat(false, false); }
bool KeepsMode(const std::shared_ptr<Tab>& t) { return Open() && t && ChatTab() == t; }
void PageNavigating(const std::shared_ptr<Tab>&) { rearmOwed = true; rearmTries = 0; aiming.clear(); if (EnsurePost()) SetTimer(post, RearmTimer, 250, nullptr); }

// ---------------------------------------------------------------- panel -> host (contract B §5)
struct Msg {
    const SJ::Value& v;
    std::string Text(const char* key, std::size_t max) const { const auto* p = v.Get(key); return p && p->IsString() && p->string.size() <= max ? p->string : std::string(); }
    std::string Id(const char* key) const { const std::string s = Text(key, 128); return ChatStore::ValidId(s) ? s : std::string(); }
    bool Has(const char* key) const { return v.Get(key) != nullptr; }
    bool Flag(const char* key) const { const auto* p = v.Get(key); return p && p->IsBool() && p->boolean; }
    std::vector<std::string> Ids(const char* key, std::size_t max) const { std::vector<std::string> out; if (const auto* p = v.Get(key); p && p->IsArray()) for (const auto& i : p->items) if (i.IsString() && ChatStore::ValidId(i.string) && out.size() < max) out.push_back(i.string); return out; }
};
std::string LastUserText() { for (auto it = convo.data.items.rbegin(); it != convo.data.items.rend(); ++it) { const auto* ty = it->Get("type"); const auto* tx = it->Get("text"); if (ty && ty->string == "message.user" && tx && tx->IsString()) return tx->string; } return ""; }
void SendOrQueue(const std::string& text, const std::vector<std::string>& els, const std::vector<std::string>& atts) {
    if (text.find_first_not_of(" \t\r\n") == std::string::npos && els.empty() && atts.empty()) return;
    if (!Usable()) return;
    if (turn.active) { queue.push_back({NewId("q_"), text, els, atts}); SendQueue(); } else StartTurn(text, els, atts);
}
void AddAttachment(const std::string& name, const std::string& mime, std::string base64) {
    static const char* allowed[] = {"image/png", "image/jpeg", "image/webp", "image/gif"}; bool ok = false; for (auto* m : allowed) ok = ok || mime == m;
    auto bytes = std::make_shared<std::string>(); auto thumb = std::make_shared<std::string>(); auto data = std::make_shared<std::string>(std::move(base64));
    auto refuse = [] { Problem p; p.title = TR(chat_err_image_title); p.message = TR(chat_err_image); p.retry = false; SendError(p, "", "composer", false); };
    if (!ok || data->empty() || data->size() > (12u << 20) || composerAttachments.size() >= 10) { refuse(); return; }
    Ann::Run([bytes, thumb, data] { if (AH::Base64Decode(*data, *bytes)) *thumb = ThumbDataUrl(*bytes, 160, 160); },
             [thumb, data, name, mime, refuse] {
                 if (thumb->empty()) { refuse(); return; }
                 Attachment a; a.id = NewId("at_"); a.name = ChatPrompt::OneLine(name.empty() ? "image" : name, 120); a.mime = mime; a.base64 = std::move(*data); a.thumb = *thumb;
                 SJ::Value m = SJ::Value::Obj(); m.Set("attachment", AttachmentRef(a)); composerAttachments[a.id] = std::move(a); Send("attachment.added", std::move(m));
             });
}
void PickImage() {
    IFileOpenDialog* dlg = nullptr; std::wstring path;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg))) || !dlg) return;
    const std::wstring label = TR(chat_image_pick_title); COMDLG_FILTERSPEC spec[] = {{label.c_str(), L"*.png;*.jpg;*.jpeg;*.webp;*.gif"}};
    dlg->SetFileTypes(1, spec); dlg->SetTitle(label.c_str());
    DWORD opts = 0; dlg->GetOptions(&opts); dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_NOCHANGEDIR);
    if (SUCCEEDED(dlg->Show(window))) { IShellItem* item = nullptr; if (SUCCEEDED(dlg->GetResult(&item)) && item) { PWSTR p = nullptr; if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p)) && p) { path = p; CoTaskMemFree(p); } item->Release(); } }
    dlg->Release(); if (path.empty()) return;
    std::string bytes; if (!ChatStore::ReadFile(path, bytes, 9u << 20)) { Problem p; p.title = TR(chat_err_image_title); p.message = TR(chat_err_image); p.retry = false; SendError(p, "", "composer", false); return; }
    std::wstring ext = path.substr(path.find_last_of(L'.') + 1); for (auto& c : ext) c = towlower(c);
    AddAttachment(U8(ChatFolder::Name(path)), ext == L"png" ? "image/png" : ext == L"gif" ? "image/gif" : ext == L"webp" ? "image/webp" : "image/jpeg", Base64(bytes));
}
void Mentions(const std::string& query) {
    struct Out { std::vector<std::pair<std::string, std::string>> files; }; auto out = std::make_shared<Out>(); const std::wstring dir = folder;
    std::wstring needle = W(query); for (auto& c : needle) c = towlower(c);
    SJ::Value els = SJ::Value::Arr(); for (const auto& e : composerElements) els.Push(ElementRef(e.second));
    auto reply = [query, out](SJ::Value elements) {
        SJ::Value files = SJ::Value::Arr(); for (const auto& f : out->files) { SJ::Value o = SJ::Value::Obj(); o.Set("path", Str(f.first)); o.Set("dir", Str(f.second)); files.Push(std::move(o)); }
        SJ::Value m = SJ::Value::Obj(); m.Set("query", Str(query)); m.Set("elements", std::move(elements)); m.Set("files", std::move(files)); Send("mentions", std::move(m)); };
    if (dir.empty()) { reply(std::move(els)); return; }
    auto shared = std::make_shared<SJ::Value>(std::move(els));
    Ann::Run([out, dir, needle] {   // bounded walk of the project folder: 6 levels, 6000 entries, the snapshot store's excluded folders
            const auto excluded = CS::DefaultExcludedDirectories(); int visited = 0;
            std::function<void(const std::wstring&, const std::wstring&, int)> walk = [&](const std::wstring& abs, const std::wstring& rel, int depth) {
                WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((abs + L"\\*").c_str(), &fd); if (h == INVALID_HANDLE_VALUE) return;
                do {
                    const std::wstring name = fd.cFileName; if (name == L"." || name == L".." || ++visited > 6000 || out->files.size() >= 20) continue;
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
                    const std::wstring r = rel.empty() ? name : rel + L"/" + name;
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                        bool skip = false; std::string low = U8(name); for (auto& c : low) c = char(std::tolower(static_cast<unsigned char>(c))); for (const auto& x : excluded) skip = skip || low == x;
                        if (!skip && depth < 6) walk(abs + L"\\" + name, r, depth + 1);
                    } else { std::wstring low = r; for (auto& c : low) c = towlower(c); if (needle.empty() || low.find(needle) != std::wstring::npos) out->files.emplace_back(U8(r), U8(rel)); }
                } while (FindNextFileW(h, &fd));
                FindClose(h); };
            walk(dir, L"", 0);
        },
        [reply, shared] { reply(std::move(*shared)); });
}
void EditMessage(const std::string& messageId, const std::string& text, bool hasRestore, bool restore) {
    if (turn.active || messageId.size() < 2 || messageId[0] != 'u') return;
    const std::string turnId = messageId.substr(1); std::size_t index = convo.data.items.size(); std::vector<std::string> files;
    for (std::size_t i = 0; i < convo.data.items.size(); ++i) {
        const auto& item = convo.data.items[i]; const auto* ty = item.Get("type"); if (!ty) continue;
        if (ty->string == "message.user") { const auto* id = item.Get("id"); if (id && id->string == messageId) index = i; }
        if (index < convo.data.items.size() && ty->string == "turn.end") { const auto* un = item.Get("undone"); const auto* ch = item.Get("changes");
            if (!(un && un->IsBool() && un->boolean) && ch && ch->IsArray()) for (const auto& c : ch->items) if (const auto* p = c.Get("path"); p && p->IsString() && files.size() < 40 && std::find(files.begin(), files.end(), p->string) == files.end()) files.push_back(p->string); }
    }
    if (index >= convo.data.items.size()) return;
    if (!hasRestore && !files.empty()) { SJ::Value m = SJ::Value::Obj(); m.Set("messageId", Str(messageId)); SJ::Value fs = SJ::Value::Arr(); for (const auto& f : files) fs.Push(Str(f)); m.Set("files", std::move(fs)); Send("edit.confirm", std::move(m)); return; }
    auto resend = [index, text] {
        if (index < convo.data.items.size()) convo.data.items.erase(convo.data.items.begin() + std::ptrdiff_t(index), convo.data.items.end());
        RebuildTurnMaps(); SendThread(); StartTurn(text, {}, {});
    };
    if (restore && snapshotOfTurn.count(turnId)) { afterUndo = [resend](bool ok) { if (ok) resend(); }; Undo(turnId, ""); } else resend();
}
void OnPanelMessage(const std::string& json) {
    SJ::Limits lim; lim.maxBytes = 13u << 20; lim.maxStringBytes = 13u << 20; lim.maxDepth = 12; lim.maxItems = 4096;
    SJ::Value v; if (!SJ::Parse(json, v, nullptr, lim) || !v.IsObject()) { Diag(L"panel message unreadable: ignored"); return; }
    const Msg m{v}; const std::string type = m.Text("type", 64);
    const auto* ver = v.Get("v"); if (!ver || !ver->IsNumber() || !ver->integral || ver->integer != 1) { Diag(L"panel message with another protocol version: ignored"); return; }
    if (json.size() > (256u << 10) && type != "attachImage") { Diag(L"panel message too large (" + W(type) + L"): ignored"); return; }
    if (type == "ready") {
        panelReady = true; stats.readyAt = Ann::NowMs();
        Diag(L"panel ready " + Ann::Ms(stats.readyAt - stats.requestedAt) + L" ms after the request (build " + W(m.Text("build", 64)) + L")");
        SJ::Value hello = SJ::Value::Obj(); hello.Set("locale", Str(std::string(I18n::Current() == I18n::Locale::En ? "en" : "fr"))); hello.Set("platform", Str(std::string("windows"))); hello.Set("reducedMotion", SJ::Value::Bool(Ann::Reduced()));
        Send("hello", std::move(hello)); SendState(); SendThread(); SendCommands(); SendQueue();
        for (const auto& e : composerElements) { SJ::Value a = SJ::Value::Obj(); a.Set("element", ElementRef(e.second)); Send("element.attached", std::move(a)); }
        for (const auto& a : composerAttachments) { SJ::Value x = SJ::Value::Obj(); x.Set("attachment", AttachmentRef(a.second)); Send("attachment.added", std::move(x)); }
        for (const auto& s : activeStrips) Send("strip", s.second);
        for (const auto& w : waitingPermissions) Send("permission.request", w.second);   // a card that was waiting before this WebView existed
        {   SJ::Value p = SJ::Value::Obj(); p.Set("active", SJ::Value::Bool(picking && !turn.active)); Send("pick.state", std::move(p)); }
        if (Open()) { SJ::Value f = SJ::Value::Obj(); f.Set("target", Str(std::string("composer"))); Send("focus", std::move(f)); view.Focus(); }
        return;
    }
    if (!panelReady) return;
    if (type == "send") { SendOrQueue(m.Text("text", 400000), m.Ids("elements", 10), m.Ids("attachments", 10)); return; }
    if (type == "stop") { StopTurn(); return; }
    if (type == "queue.sendNow") { const std::string id = m.Id("id"); for (auto it = queue.begin(); it != queue.end(); ++it) if (it->id == id) { Queued q = std::move(*it); queue.erase(it); queue.push_front(std::move(q)); break; } if (turn.active) StopTurn(); else ProcessQueue(); SendQueue(); return; }
    if (type == "queue.remove") { const std::string id = m.Id("id"); queue.erase(std::remove_if(queue.begin(), queue.end(), [&](const Queued& q) { return q.id == id; }), queue.end()); SendQueue(); return; }
    if (type == "permission.respond") {
        const std::string request = m.Id("requestId"), option = m.Id("optionId"); if (request.empty() || !session) return;
        session->RespondPermission(request, option); waitingPermissions.erase(request); Diag(L"permission answered: " + W(option));
        if (const std::string text = m.Text("message", 400000); text.find_first_not_of(" \t\r\n") != std::string::npos) { queue.push_front({NewId("q_"), text, {}, {}}); SendQueue(); }
        return;
    }
    if (type == "setModel") {
        const std::string id = m.Id("id"), agent = m.Id("agentId"); if (id.empty()) return;
        if (!agent.empty() && agent != agentId) { SwitchAgent(agent, id); return; }
        const auto* o = Acp::FindModelOption(agentInfo[agentId].options);
        if (session && sessionReady && o) session->SetConfigOption(o->id, id); else pendingModel = id;
        if (!site.origin.empty() && !folder.empty()) { projects.sites[site.origin].model = id; RememberChoices(); }
        return;
    }
    if (type == "setEffort") { const std::string id = m.Id("id"); const auto* o = Acp::FindEffortOption(agentInfo[agentId].options); if (id.empty()) return; if (session && sessionReady && o) session->SetConfigOption(o->id, id); else pendingEffort = id; return; }
    if (type == "setMode") {
        const std::string id = m.Id("id"); if (id.empty()) return; const auto mv = ModesOf(agentId, agentInfo[agentId]);
        if (!mv.list.empty() && KindOfMode(mv, id) == "other" && std::none_of(mv.list.begin(), mv.list.end(), [&](const ModeEntry& e) { return e.id == id; })) return;   // not a mode the agent offers
        SetAgentMode(id);   // for this session only: a new session always starts in « Demander » (decision 4), whatever was chosen before
        return;
    }
    if (type == "setAgent") { SwitchAgent(m.Id("id"), ""); return; }
    if (type == "agent.connect") { Connect(m.Id("agentId").empty() ? agentId : m.Id("agentId"), m.Id("methodId")); return; }
    if (type == "agent.prepare") { const std::string id = m.Id("agentId").empty() ? agentId : m.Id("agentId"); PrepareAgent(id, [](bool ok) { if (ok) MaybeStartSession(); }); return; }
    if (type == "pickElement") { picking = m.Flag("active") && site.local && !site.file; SJ::Value p = SJ::Value::Obj(); p.Set("active", SJ::Value::Bool(picking && !turn.active)); Send("pick.state", std::move(p)); if (picking) FocusChatPage(); return; }
    if (type == "element.remove") { composerElements.erase(m.Id("id")); return; }
    if (type == "attachImage") { if (m.Text("source", 16) == "picker") PickImage(); else AddAttachment(m.Text("name", 300), m.Text("mime", 40), m.Text("data", 13u << 20)); return; }
    if (type == "attachment.remove") { composerAttachments.erase(m.Id("id")); return; }
    if (type == "mention.query") { Mentions(m.Text("query", 200)); return; }
    if (type == "folder.use") { const std::wstring path = W(m.Text("path", 2048)); if (!suggestion.folder.empty() && path == suggestion.folder) SetFolder(path); else Diag(L"folder.use with a path that was not proposed: ignored"); return; }
    if (type == "folder.choose") { const std::wstring picked = ChatFolder::PickFolder(window, TR(chat_folder_pick_title), !folder.empty() ? folder : !suggestion.folder.empty() ? suggestion.folder : projects.Recent()); if (!picked.empty()) SetFolder(picked); if (Open()) view.Focus(); return; }
    if (type == "undo.turn") { Undo(m.Id("turnId"), ""); return; }
    if (type == "checkpoint.restore") { const std::string cp = m.Id("checkpointId"); std::string t; for (const auto& p : checkpointOfTurn) if (p.second == cp) t = p.first; if (!cp.empty()) Undo(t, cp); return; }
    if (type == "checkpoint.viewChanges") { ViewChanges(m.Id("turnId"), m.Text("path", 1024)); return; }
    if (type == "plan.run") {
        if (turn.active && !turn.planPermission.empty() && session) { std::string allow; for (const auto& o : turn.planOptions) if (allow.empty() && o.kind.rfind("allow", 0) == 0) allow = o.id; if (allow.empty() && !turn.planOptions.empty()) allow = turn.planOptions.back().id; session->RespondPermission(turn.planPermission, allow); turn.planPermission.clear(); return; }
        // a plan given as plain updates (no gate): leave Plan mode for the first « ask » mode, then say go
        const auto mv = ModesOf(agentId, agentInfo[agentId]); std::string next; for (const auto& md : mv.list) if (next.empty() && md.kind == "ask") next = md.id; for (const auto& md : mv.list) if (next.empty() && md.kind != "plan") next = md.id;
        if (!next.empty() && session && sessionReady) SetAgentMode(next);
        SendOrQueue(T8(I18n::Id::chat_prompt_run_plan), {}, {}); return;
    }
    if (type == "plan.edit") { if (turn.active && !turn.planPermission.empty() && session) { std::string reject; for (const auto& o : turn.planOptions) if (reject.empty() && o.kind.rfind("reject", 0) == 0) reject = o.id; session->RespondPermission(turn.planPermission, reject); turn.planPermission.clear(); } return; }
    if (type == "history.open") { SendHistory(""); return; }
    if (type == "history.search") { SendHistory(m.Text("query", 200)); return; }
    if (type == "history.select") { const std::string id = m.Id("id"); if (id.empty()) return; if (turn.active) { queue.clear(); afterTurn = [id] { SelectHistory(id); }; StopTurn(); } else SelectHistory(id); return; }
    if (type == "newChat") { if (turn.active) { queue.clear(); afterTurn = [] { NewConversation(); MaybeStartSession(); }; StopTurn(); } else { NewConversation(); MaybeStartSession(); } return; }
    if (type == "message.edit") { EditMessage(m.Id("messageId"), m.Text("text", 400000), m.Has("restore"), m.Flag("restore")); return; }
    if (type == "retry") { const std::string t = m.Id("turnId"); std::string text; for (const auto& item : convo.data.items) { const auto* ty = item.Get("type"); const auto* id = item.Get("id"); const auto* tx = item.Get("text"); if (ty && ty->string == "message.user" && id && id->string == "u" + t && tx && tx->IsString()) text = tx->string; } if (text.empty()) text = LastUserText(); if (!text.empty()) SendOrQueue(text, {}, {}); return; }
    if (type == "error.action" || type == "strip.action") {
        const std::string action = m.Id("actionId");
        if (action == "retry") { const std::string text = LastUserText(); if (!text.empty()) SendOrQueue(text, {}, {}); else MaybeStartSession(); }
        else if (action == "connect") Connect(agentId, "");
        else if (action == "newChat") { NewConversation(); MaybeStartSession(); }
        return;
    }
    if (type == "suggestion.run") { const std::string id = m.Id("id"); SendOrQueue(T8(id == "annotations" ? I18n::Id::chat_prompt_annotations : id == "mobile" ? I18n::Id::chat_prompt_mobile : I18n::Id::chat_prompt_contrast), {}, {}); return; }
    if (type == "copy") { const bool ok = CopyToClipboard(W(m.Text("text", 400000))); SJ::Value r = SJ::Value::Obj(); r.Set("ok", SJ::Value::Bool(ok)); Send("copy.done", std::move(r)); return; }
    if (type == "link.open") { const std::string url = m.Text("url", 2000); if (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0) CreateTab(W(url), -1, TR(tab_new), nullptr); else Diag(L"link refused: " + W(ClipUtf8(url, 120))); return; }
    if (type == "file.open") {   // shown in the Explorer (never executed): a file of the project folder only
        std::string rel = m.Text("path", 1024), abs; if (rel.empty() || folder.empty()) return;
        std::string root = U8(folder); for (auto& c : root) if (c == '\\') c = '/'; for (auto& c : rel) if (c == '\\') c = '/';
        if (!ChatPrompt::detail::Normalize(ChatPrompt::detail::Absolute(rel) ? rel : root + "/" + rel, abs) || !ChatPrompt::detail::StartsWithFolded(abs, root + "/")) return;
        std::wstring w = W(abs); for (auto& c : w) if (c == L'/') c = L'\\';
        if (GetFileAttributesW(w.c_str()) != INVALID_FILE_ATTRIBUTES) ShellExecuteW(window, L"open", L"explorer.exe", (L"/select,\"" + w + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
        return;
    }
    if (type == "close") { ClosePanel(); return; }
    if (type == "panel.log") { Diag(L"panel " + W(m.Text("level", 8)) + L": " + W(ClipUtf8(m.Text("message", 2048), 600))); return; }
    Diag(L"panel message of an unknown type ignored: " + W(ClipUtf8(type, 60)));
}

void Shutdown() {
    if (prepareCancel) *prepareCancel = true;
    if (session) { session->Dispose(); disposed.push_back(std::move(session)); session.reset(); }
    for (auto& s : disposed) s->WaitStopped(3000);
    disposed.clear(); view.Destroy(); panelReady = false; panelState = PanelState::Closed;
    if (post) { DestroyWindow(post); post = nullptr; }
}
} // namespace Chat

// ---------------------------------------------------------------- host entry points (declared at the top of main.cpp)
float ChatInsetDip() { return Chat::InsetDip(); }
void ChatNavigationCompleted(const std::shared_ptr<Tab>& t) { if (Chat::rearmOwed && t && Chat::ChatTab() == t) Chat::DoRearm(); }
void ChatShutdown() { Chat::Shutdown(); }
