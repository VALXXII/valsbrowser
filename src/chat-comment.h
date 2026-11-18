#pragma once
// Chat intégré — Mode Commentaire (design/chat-integration.md « Mode Commentaire », contract B 1.8 §19, Paper 18NS-0 …
// 18IJ-0). Included by src/chat-host.h after the panel, the turn and the batch: same engine, other layout.
//
//   switch    the band's two icons (Paper 1AYP-0): Commentaire (a bubble on the element clicked) | Panneau (the chat at
//             the right, as before). Remembered for all sites (projects.json « layout »).
//   bubble    a native popup window owned by the browser window that hosts THE chat WebView (moved there with
//             put_ParentWindow; the panel gets it back the same way): 305 CSS px wide, as high as the page says
//             (`bubble.size`, at most min(520, the free space)), in the free space next to the element (right, left,
//             below, above — src/chat-bubble.h), never out of the page area; it follows the element (the page posts
//             `anchor` when the element moves). Échap (`close`) or a click elsewhere closes it; the agent goes on.
//   one conversation per element   the conversation's header keeps the element (selector, label, page, source). Every
//             element of the page that has one carries a frame + the mark (Paper 18IJ-0), during and after the work; a
//             click on it opens its bubble again. Found again after a reload by its selector.
//   gone      an element that is no longer in the page: its conversation is listed in the launcher's history (bottom
//             right), which in this mode lists only those; a click there opens it in a bubble at the bottom right.
//   one turn at a time   while the agent works on element A, the bubble of element B can be opened and written in: the
//             message waits in the queue (« Commencera après le travail en cours sur A ») and starts by itself when A's
//             turn ends. The conversation shown (B, « aside ») is then not the engine's (A): A's thread messages are not
//             sent to the panel (Send() filters them, chat-host.h); a permission A asks for is said by a strip.
namespace Chat {

bool CommentLayout() { LoadProjects(); return projects.layout == "comment"; }
float Dpi() { return GetDpiForWindow(window) / 96.f; }
inline constexpr int BubbleWidthCss = 305, BubbleMaxHeightCss = 520, BubbleStartHeightCss = 104, BubbleGapCss = 8, BubbleMarginCss = 12;

struct BubbleState {
    HWND hwnd = nullptr; bool open = false; std::string conv;      // the conversation it shows
    std::string selector, label;                                     // the element it follows ("" + current: the element selected now)
    RECT element{}; bool known = false;                              // the element on screen (px), once the page said where
    int w = 0, h = 0; ChatBubble::Side side = ChatBubble::Side::Right;
    double closedAt = -1e9;                                          // NowMs() of the last close (a click that closed it is not a new comment)
    double openedAt = -1e9;                                          // NowMs() of the last opening: the click that opened it gives the page the focus back first
    bool modal = false;                                              // a Windows dialog of ours is up: its deactivation does not close the bubble
} bubble;
std::map<std::string, Element> anchorElements;   // conversation id -> its element (image, source): joins every message of its bubble
std::map<std::string, Convo> parked;             // conversations with a message waiting, neither the engine's nor shown
std::vector<ChatStore::Header> anchored;         // this site's conversations that belong to an element (index + memory)
std::set<std::string> onPage;                    // the ones whose element is in the page now
bool anchoredLoading = false;

bool BubbleOpen() { return bubble.open; }
std::string BubbleSelector() { return bubble.selector; }
void ForgetPage() { onPage.clear(); }
void SyncMarks();
void ScriptReady() { if (CommentLayout()) SyncMarks(); }
std::string ShownId() { return aside.on ? aside.c.data.head.id : convo.data.head.id; }
const ChatStore::Header* AnchoredOf(const std::string& id) { for (const auto& h : anchored) if (h.id == id) return &h; return nullptr; }
std::string ModeOrigin();
// A conversation's header joins the list (or is updated) when it is saved, or created anchored.
void NoteAnchored(const ChatStore::Header& h) {
    if (!h.Anchored() || h.origin != ModeOrigin()) return;
    for (auto& a : anchored) if (a.id == h.id) { a = h; return; }
    anchored.insert(anchored.begin(), h);
    SyncMarks();   // a new conversation of an element: its frame + icon now
}
void SyncMarks();
std::string ModeOrigin() { auto t = Ann::ModeTab(); return t ? ChatLocal::Classify(U8(t->uri)).origin : std::string(); }
std::string anchoredOrigin;   // the site `anchored` was read for
void RefreshAnchored() {
    const std::string origin = ModeOrigin();
    if (anchoredLoading || origin.empty()) return;
    anchoredLoading = true; auto out = std::make_shared<std::vector<ChatStore::Header>>();
    Ann::Run([out] { *out = History().List(); },
             [out, origin] {
                 anchoredLoading = false; if (origin != ModeOrigin()) return;
                 anchoredOrigin = origin;
                 std::vector<ChatStore::Header> keep;
                 for (const auto& h : *out) if (h.origin == origin && h.Anchored()) keep.push_back(h);
                 for (const auto* mem : {&convo.data.head, aside.on ? &aside.c.data.head : nullptr}) if (mem && mem->Anchored()) { bool in = false; for (const auto& k : keep) in = in || k.id == mem->id; if (!in && !mem->id.empty()) keep.insert(keep.begin(), *mem); }
                 anchored = std::move(keep); SyncMarks();
             });
}

// ---------------------------------------------------------------- where things are on screen
RECT PageArea() { auto t = ChatTab(); if (!t) t = Active(); return Ann::PageScreen(t); }
RECT ToScreen(double x, double y, double w, double h, double vw) {
    const RECT p = PageArea(); const double k = vw > 0 ? double(p.right - p.left) / vw : 1;
    return {p.left + LONG(std::lround(x * k)), p.top + LONG(std::lround(y * k)), p.left + LONG(std::lround((x + w) * k)), p.top + LONG(std::lround((y + h) * k))};
}
int FreeHeightCss() { const RECT a = PageArea(); return std::max(120, int((a.bottom - a.top) / Dpi()) - 2 * BubbleMarginCss); }
const char* SideName(ChatBubble::Side s) { switch (s) { case ChatBubble::Side::Left: return "left"; case ChatBubble::Side::Below: return "below"; case ChatBubble::Side::Above: return "above"; case ChatBubble::Side::Inside: return "inside"; default: return "right"; } }

// contract B 1.8 §19.1: the page's layout, sent at every opening of the bubble, every switch, and new bounds (open false)
void SendLayout(bool openNow) {
    SJ::Value m = SJ::Value::Obj();
    if (bubble.open) {
        m.Set("mode", Str(std::string("bubble"))); m.Set("maxWidth", SJ::Value::Int(BubbleWidthCss)); m.Set("maxHeight", SJ::Value::Int(std::min(BubbleMaxHeightCss, FreeHeightCss())));
        m.Set("side", Str(std::string(SideName(bubble.side)))); if (!bubble.label.empty()) m.Set("element", Str(bubble.label)); m.Set("open", SJ::Value::Bool(openNow));
    } else m.Set("mode", Str(std::string("panel")));
    Send("layout", std::move(m));
}
void PlaceBubble() {
    if (!bubble.hwnd || !bubble.open) return;
    const RECT a = PageArea(); const float k = Dpi();
    int w = bubble.w ? bubble.w : int(std::lround(BubbleWidthCss * k)), h = bubble.h ? bubble.h : int(std::lround(BubbleStartHeightCss * k));
    const ChatBubble::Rect area{a.left, a.top, a.right, a.bottom}; const int margin = int(std::lround(BubbleMarginCss * k)), gap = int(std::lround(BubbleGapCss * k));
    ChatBubble::Fit(w, h, area, margin, int(std::lround(BubbleWidthCss * k)), int(std::lround(BubbleMaxHeightCss * k)));
    // no element known yet (opened from the history, or waiting for the page): the bottom right corner of the page
    const RECT e = bubble.known ? bubble.element : RECT{a.right - margin, a.bottom - margin, a.right - margin, a.bottom - margin};
    const auto p = ChatBubble::Place({e.left, e.top, e.right, e.bottom}, w, h, area, gap, margin, bubble.side);
    const bool sideChanged = p.side != bubble.side; bubble.side = p.side;
    SetWindowPos(bubble.hwnd, HWND_TOP, p.rect.left, p.rect.top, p.rect.W(), p.rect.H(), SWP_NOACTIVATE);
    view.Place({0, 0, p.rect.W(), p.rect.H()});
    if (sideChanged && panelReady) SendLayout(false);
}
RECT BubbleScreenRect() { RECT r{}; if (bubble.hwnd) GetWindowRect(bubble.hwnd, &r); return r; }
void BubbleModal(bool on) { bubble.modal = on; }
void CloseBubble(bool focusPage);
LRESULT CALLBACK BubbleProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    // A click elsewhere — the page, the browser's chrome, another window — takes the activation from the bubble: it closes
    // (its agent goes on). Not while one of our own dialogs is up (image picker, folder picker).
    // Another application takes the foreground: the bubble closes. The browser's own windows do not count: a click in the
    // page is a selection (TakeCommentSelection closes the bubble, or opens the element's), and the mode's capture and
    // focus moves activate the browser window without any click.
    if (m == WM_ACTIVATE && LOWORD(w) == WA_INACTIVE && bubble.open && !bubble.modal) {
        DWORD pid = 0; HWND to = reinterpret_cast<HWND>(l); if (to) GetWindowThreadProcessId(to, &pid);
        if (!to || pid != GetCurrentProcessId()) PostMessageW(h, WM_APP + 1, 0, 0);
        return 0;
    }
    // the selection that opened it ends by giving the page the keyboard (Ann::Resume): not a click elsewhere
    if (m == WM_APP + 1) { if (bubble.open && !bubble.modal && GetForegroundWindow() != h && Ann::NowMs() - bubble.openedAt > 500) CloseBubble(false); return 0; }
    if (m == WM_APP + 2) { if (bubble.open) { SetForegroundWindow(h); view.Focus(); } return 0; }   // the keyboard to the bubble, once the selection is over
    if (m == WM_CLOSE) { CloseBubble(true); return 0; }
    if (m == WM_ERASEBKGND) { RECT c; GetClientRect(h, &c); HBRUSH b = CreateSolidBrush(RGB(0x17, 0x17, 0x17)); FillRect(reinterpret_cast<HDC>(w), &c, b); DeleteObject(b); return 1; }
    return DefWindowProcW(h, m, w, l);
}
bool EnsureBubbleWindow() {
    if (bubble.hwnd) return true;
    WNDCLASSEXW wc{sizeof(wc)}; wc.lpfnWndProc = BubbleProc; wc.hInstance = instance; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.lpszClassName = L"BrowserChatBubble"; RegisterClassExW(&wc);
    bubble.hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, TR(chat_bubble_title), WS_POPUP | WS_CLIPCHILDREN, 0, 0, 10, 10, window, nullptr, instance, nullptr);
    if (!bubble.hwnd) return false;
    NativeUI::RoundNatively(bubble.hwnd);   // DWM's own rounded corners and shadow (antialiased; a region would be stair-stepped)
    return true;
}
void OpenBubble(const std::string& conv, const RECT* element, const std::string& selector, bool followCurrent, const std::string& label) {
    auto t = Active(); if (!t || !Ann::s.active || Ann::ModeTab() != t || halted || privateWindow) return;
    if (Open()) ClosePanel();
    EnsureInit(); if (!EnsureBubbleWindow()) return;
    PrepareChat(t);
    if (post) KillTimer(post, DestroyTimer);
    const bool again = bubble.open;
    bubble.open = true; bubble.conv = conv; bubble.selector = selector; bubble.label = label;
    bubble.known = element != nullptr; if (element) bubble.element = *element;
    if (!again) { bubble.side = ChatBubble::Side::Right; bubble.h = 0; }
    stats = OpenStats{}; stats.requestedAt = Ann::NowMs();
    if (!view.Usable(environment.p)) { view.Destroy(); panelReady = false; if (!view.Create(environment.p, bubble.hwnd, {0, 0, 10, 10})) { bubble.open = false; Diag(L"bubble WebView creation refused"); return; } }
    else view.Reparent(bubble.hwnd, {0, 0, 10, 10});
    PlaceBubble();
    bubble.openedAt = Ann::NowMs();
    ShowWindow(bubble.hwnd, SW_SHOW); SetForegroundWindow(bubble.hwnd); view.Show(true);
    PostMessageW(bubble.hwnd, WM_APP + 2, 0, 0);   // after Ann::Resume (it focuses the page): the caret goes to the bubble
    ArmAgentIdle(); Ann::LauncherSync(); SyncChromeBits();
    // the page follows the element (its moves become `anchor` messages)
    {   SJ::Value f = SJ::Value::Obj(); if (followCurrent) f.Set("current", SJ::Value::Bool(true)); else f.Set("selector", Str(selector));
        if (followCurrent || !selector.empty()) Query(t, L"follow", W(SJ::Serialize(f)), nullptr); }
    if (panelReady) {
        SendLayout(true); SendState(); SendThread(); SendQueue();
        SJ::Value f = SJ::Value::Obj(); f.Set("target", Str(std::string("composer"))); Send("focus", std::move(f));
        view.Focus();
    }
    SyncMarks();
    Ann::Announce(TRF(chat_bubble_announce, W(label.empty() ? std::string("—") : label)));
    Diag(L"bubble opened on " + W(label.empty() ? std::string("(no element)") : label) + L" for conversation " + W(conv) + (again ? L" (was open)" : L""));
}
void CloseBubble(bool focusPage) {
    if (!bubble.open) return;
    bubble.open = false; bubble.closedAt = Ann::NowMs();
    if (bubble.hwnd) ShowWindow(bubble.hwnd, SW_HIDE);
    view.Show(false);
    if (auto t = ChatTab()) Query(t, L"follow", L"null", nullptr);
    if (post) SetTimer(post, DestroyTimer, PanelIdleMs(), nullptr);
    ArmAgentIdle(); SyncMarks();
    Diag(L"bubble closed (the agent goes on)");
    if (focusPage && Ann::s.active) { SetForegroundWindow(window); FocusChatPage(); }
}
void OnBubbleSize(int wCss, int hCss) {
    if (!bubble.open) return;
    const float k = Dpi();
    bubble.w = int(std::lround(std::clamp(wCss, 200, BubbleWidthCss) * k)); bubble.h = int(std::lround(std::clamp(hCss, 40, BubbleMaxHeightCss) * k));
    PlaceBubble();
}
// The element the bubble follows moved (scroll, layout, reload) or is gone.
void OnAnchor(const AH::Envelope& e) {
    if (!bubble.open) return;
    if (e.gone) { Diag(L"the bubble's element is no longer in the page"); CloseBubble(false); return; }
    bubble.element = ToScreen(e.x, e.y, e.w, e.h, e.vw); bubble.known = true;
    PlaceBubble();
}

// ---------------------------------------------------------------- which conversation is shown
void SendQueue();
void RefreshWaitStrip() {
    std::size_t waiting = 0; for (const auto& q : queue) if (aside.on && q.convId == aside.c.data.head.id) ++waiting;
    if (aside.on && waiting) {
        const std::string label = convo.data.head.anchorLabel.empty() ? ChatPrompt::OneLine(convo.data.head.title, 40) : convo.data.head.anchorLabel;
        Strip("wait", "neutral", TRF(chat_wait_after, W(label)));
    } else if (activeStrips.count("wait")) ClearStrip("wait");
    if (!aside.on && activeStrips.count("waitwork")) ClearStrip("waitwork");
}
bool HasQueued(const std::string& id) { for (const auto& q : queue) if ((q.convId.empty() ? convo.data.head.id : q.convId) == id) return true; return false; }
// The conversation shown leaves the screen: kept in memory while a message of it waits, else simply dropped (it is saved).
void ParkAside() {
    if (!aside.on) return;
    const std::string id = aside.c.data.head.id;
    if (HasQueued(id) || aside.c.data.items.empty()) parked[id] = std::move(aside.c);
    aside = Aside{};
}
void ShowAside(Convo c) {
    ParkAside(); aside.c = std::move(c); aside.on = true;
    SendState(); SendThread(); SendQueue(); RefreshWaitStrip();
}
// Shows a conversation (the bubble's): the engine's (A) when it is that one; else, while A works, B « aside »; when nothing
// runs, B becomes the engine's (the agent starts for it). `then` runs once it is in place.
void ShowConversation(const std::string& id, std::function<void()> then) {
    if (id == convo.data.head.id) { if (aside.on) { ParkAside(); SendState(); SendThread(); SendQueue(); RefreshWaitStrip(); } if (then) then(); return; }
    if (aside.on && aside.c.data.head.id == id) { if (then) then(); return; }
    if (auto it = parked.find(id); it != parked.end()) {
        Convo c = std::move(it->second); parked.erase(it);
        if (turn.active) { ShowAside(std::move(c)); if (then) then(); return; }
        ParkAside(); DisposeSession(L"another element");   // i18n-exempt: log reason
        convo = std::move(c); RebuildTurnMaps(); SendState(); SendThread(); SendQueue(); RefreshWaitStrip(); MaybeStartSession();
        if (then) then(); if (HasQueued(convo.data.head.id)) ProcessQueue(); return;
    }
    auto out = std::make_shared<ChatStore::Conversation>(); auto ok = std::make_shared<bool>(false);
    Ann::Run([out, ok, id] { *ok = History().Load(id, *out); },
             [out, ok, then] {
                 if (!*ok || out->head.origin != site.origin) { Diag(L"conversation not found: " + W(out->head.id)); return; }
                 Convo c; c.data = std::move(*out); c.preambleSent = c.data.head.promptVersion == ChatPrompt::Version;
                 if (turn.active) { ShowAside(std::move(c)); if (then) then(); return; }
                 ParkAside(); DisposeSession(L"another element"); convo = std::move(c);   // i18n-exempt: log reason
                 if (AgentOf(convo.data.head.agentId)) agentId = convo.data.head.agentId;
                 RebuildTurnMaps(); SendState(); SendThread(); SendQueue(); RefreshWaitStrip(); MaybeStartSession();
                 Diag(L"conversation of an element reopened: " + W(convo.data.head.id));
                 if (then) then();
             });
}
// A new conversation for an element (the engine's when nothing runs, else aside: its first message will wait).
std::string NewAnchoredConversation(const std::string& selector, const std::string& label, const std::string& page, const std::string& source) {
    auto setAnchor = [&](ChatStore::Header& h) { h.anchorSelector = selector; h.anchorLabel = label; h.anchorPage = page; h.anchorSource = source; if (!label.empty() && h.title.empty()) h.title = ChatPrompt::OneLine(label, 44); };
    if (!turn.active) {
        ParkAside(); NewConversation(); setAnchor(convo.data.head); SendState(); return convo.data.head.id;
    }
    Convo c; auto& h = c.data.head; h.id = NewId("c_"); h.origin = site.origin; h.siteLabel = site.label; h.agentId = agentId; h.folder = folder; h.createdAt = h.updatedAt = BA::SystemNow();
    setAnchor(h); const std::string id = h.id; ShowAside(std::move(c));
    return id;
}
// Called by ProcessQueue: the next message waiting belongs to another conversation — it becomes the engine's.
bool SwitchEngineTo(const std::string& id) {
    Convo next; bool fromAside = false;
    if (aside.on && aside.c.data.head.id == id) { next = std::move(aside.c); aside = Aside{}; fromAside = true; }
    else if (auto it = parked.find(id); it != parked.end()) { next = std::move(it->second); parked.erase(it); }
    else return false;
    DisposeSession(L"the next element");   // i18n-exempt: log reason
    Convo old = std::move(convo); convo = std::move(next);
    if (AgentOf(convo.data.head.agentId)) agentId = convo.data.head.agentId;
    RebuildTurnMaps();
    // the screen keeps showing what it showed: the old engine's conversation stays shown « aside » if it was the one shown
    if (!fromAside && !old.data.head.id.empty() && (bubble.open ? bubble.conv == old.data.head.id : Open())) { aside.c = std::move(old); aside.on = true; }
    Diag(L"the next element's turn: conversation " + W(convo.data.head.id));
    SendState(); SendThread(); SendQueue(); RefreshWaitStrip();
    return true;
}
// The bubble's element joins every message of its bubble (contract B 1.8 §19.4: the page does not list it).
void AddAnchorElement(std::vector<std::string>& ids) {
    if (!bubble.open) return;
    auto it = anchorElements.find(bubble.conv); if (it == anchorElements.end()) return;
    Element e = it->second; e.id = "el" + std::to_string(++elementSeq); e.annId.clear(); e.annSite.clear(); e.token.clear();
    ids.insert(ids.begin(), e.id); composerElements[e.id] = std::move(e);
}
// A permission the engine's turn waits for while another conversation is shown: said by a strip, « Voir » shows it.
void OnHiddenPermission() {
    const std::string label = convo.data.head.anchorLabel.empty() ? ChatPrompt::OneLine(convo.data.head.title, 40) : convo.data.head.anchorLabel;
    Strip("waitwork", "info", TRF(chat_wait_permission, W(Agent().name), W(label)), "showWork", TR(chat_action_show));
}
void ShowEngineWork() {
    const std::string id = convo.data.head.id, sel = convo.data.head.anchorSelector, label = convo.data.head.anchorLabel;
    ShowConversation(id, [id, sel, label] { if (CommentLayout()) OpenBubble(id, nullptr, sel, false, label); });
}

// ---------------------------------------------------------------- the frames + icons of the page (Paper 18IJ-0)
std::string MarkState(const std::string& id) {
    if (turn.active && convo.data.head.id == id) return "working";
    if (HasQueued(id)) return "waiting";
    return "idle";
}
void SyncMarks() {
    auto t = Ann::ModeTab(); if (!t || !Ann::s.active || !Ann::s.injected) return;
    SJ::Value list = SJ::Value::Arr();
    const auto here = ChatLocal::Classify(U8(t->uri));
    if (CommentLayout() && here.local && !here.file) {
        if (anchoredOrigin != here.origin) { RefreshAnchored(); return; }   // read first, then drawn
        const std::string page = ChatBubble::PageKey(U8(t->uri));
        std::vector<const ChatStore::Header*> all; for (const auto& h : anchored) all.push_back(&h);
        auto add = [&](const ChatStore::Header& h) { if (!h.Anchored() || !HasQueued(h.id)) return; for (auto* a : all) if (a->id == h.id) return; all.push_back(&h); };   // waiting, not saved yet
        if (aside.on) add(aside.c.data.head);
        for (const auto& p : parked) add(p.second.data.head);
        for (const auto* hp : all) { const auto& h = *hp;
            if (h.origin != here.origin || h.anchorPage != page) continue;
            SJ::Value o = SJ::Value::Obj(); o.Set("id", Str(h.id)); o.Set("selector", Str(h.anchorSelector)); o.Set("state", Str(MarkState(h.id)));
            if (MarkState(h.id) == "waiting") o.Set("waitLabel", Str(T8(I18n::Id::chat_mark_waiting)));
            list.Push(std::move(o));
        }
    }
    Query(t, L"cmarks", W(SJ::Serialize(list)), [](const std::string& json) {
        std::set<std::string> found; SJ::Value v; if (SJ::Parse(json, v) && v.IsArray()) for (const auto& i : v.items) if (i.IsString()) found.insert(i.string);
        const bool changed = found != onPage; onPage = std::move(found);
        if (changed && Ann::uiReady && Ann::launcher.HistoryVisible()) Ann::LoadHistory(false, false);
    });
}

// ---------------------------------------------------------------- a click in the page, in this layout
bool TakeCommentSelection(const std::shared_ptr<Tab>& t, std::uint64_t serial, bool captureImpossible) {
    PrepareChat(t);
    const auto& tgt = Ann::s.pending.target;
    const std::string sel = Ann::s.sel.frameKind == "main" && tgt.shadowPath.empty() && tgt.selectorUnique ? tgt.selector : std::string();
    const std::string page = ChatBubble::PageKey(U8(t->uri));
    const std::string label = tgt.path.empty() ? tgt.tag : tgt.path.back();
    const RECT element = ToScreen(Ann::s.sel.target.rect.x, Ann::s.sel.target.rect.y, Ann::s.sel.target.rect.width, Ann::s.sel.target.rect.height, Ann::s.sel.target.viewport.width);
    const ChatStore::Header* existing = nullptr;
    if (!sel.empty()) for (const auto& h : anchored) if (h.origin == site.origin && h.anchorPage == page && h.anchorSelector == sel) { existing = &h; break; }
    if (existing) {   // its conversation: the bubble opens on it again
        const std::string id = existing->id, s = existing->anchorSelector, l = existing->anchorLabel;
        if (bubble.open && bubble.conv == id) return true;
        if (anchorElements.find(id) == anchorElements.end()) { Element e = ElementFromMode(serial); e.captureDone = captureImpossible; e.anchorFor = id; aiming[serial] = std::move(e); QuerySource(t, serial); }
        ShowConversation(id, [id, element, s, l] { OpenBubble(id, &element, s, false, l); });
        return true;
    }
    // a click elsewhere while a bubble is up (or that just closed it): it only closes the bubble
    if (bubble.open || Ann::NowMs() - bubble.closedAt < 400) { if (bubble.open) CloseBubble(true); Diag(L"a click elsewhere closed the bubble"); return true; }
    if (composerElements.size() + aiming.size() >= 10) return false;
    const std::string id = NewAnchoredConversation(sel, label, page, "");
    Element e = ElementFromMode(serial); e.captureDone = captureImpossible; e.anchorFor = id; aiming[serial] = std::move(e); QuerySource(t, serial);
    OpenBubble(id, &element, sel, sel.empty(), label);
    return true;
}
// MaybeAttach: the bubble's own element (image, source) is kept for its messages, never a chip of the composer.
// A message sent from the bubble before its element was ready (image, source still coming): it leaves once it is.
struct HeldSend { bool on = false, annotate = false; std::string conv, text; std::vector<std::string> els, atts; double at = 0; } held;
void SendOrQueue(const std::string& text, const std::vector<std::string>& els, const std::vector<std::string>& atts);
void KeepAnnotation(const std::string& text, const std::vector<std::string>& ids);
bool HoldForAnchor(const std::string& text, const std::vector<std::string>& els, const std::vector<std::string>& atts, bool annotate) {
    if (!bubble.open || anchorElements.count(bubble.conv)) return false;
    bool aimingIt = false; for (const auto& a : aiming) aimingIt = aimingIt || a.second.anchorFor == bubble.conv;
    if (!aimingIt) return false;
    held = HeldSend{true, annotate, bubble.conv, text, els, atts, Ann::NowMs()};
    Diag(L"message held until the bubble's element is ready");
    return true;
}
void ReleaseHeld() {
    if (!held.on) return;
    HeldSend h = std::move(held); held = HeldSend{};
    if (auto it = anchorElements.find(h.conv); it != anchorElements.end()) {
        Element e = it->second; e.id = "el" + std::to_string(++elementSeq); h.els.insert(h.els.begin(), e.id); composerElements[e.id] = std::move(e);
    }
    if (h.annotate) KeepAnnotation(h.text, h.els); else SendOrQueue(h.text, h.els, h.atts);
}
bool KeepAnchorElement(Element& e) {
    if (e.anchorFor.empty()) return false;
    const std::string id = e.anchorFor;
    for (auto* h : {&convo.data.head, aside.on ? &aside.c.data.head : nullptr}) if (h && h->id == id && h->anchorSource.empty() && e.source.known) { h->anchorSource = e.source.Display(); if (h->source.empty()) h->source = e.source.Display(); }
    Diag(L"the bubble's element is ready: " + W(e.label) + (e.png.empty() ? L", no image" : L", image " + std::to_wstring(e.png.size()) + L" bytes"));
    anchorElements[id] = std::move(e);
    if (held.on && held.conv == id) ReleaseHeld();
    return true;
}

// ---------------------------------------------------------------- the layout switch of the band (Paper 1AYP-0)
void SwitchLayout(bool comment) {
    const bool was = CommentLayout();
    if (comment != was) { projects.layout = comment ? "comment" : "panel"; SaveProjects(); Diag(comment ? L"layout: Commentaire (a bubble on the element)" : L"layout: Panneau (the chat at the right)"); }
    if (comment) {
        if (Ann::s.paused) Ann::SetSelecting(true);   // the switch of V1.4 is a panel thing: the selection is how comments start
        if (Open()) ClosePanel();
        Ann::Announce(TR(chat_layout_comment_on));
    } else {
        const bool had = bubble.open; CloseBubble(false);
        if (!Open()) OpenPanel(); else if (panelReady) { SendLayout(false); SendThread(); }
        (void)had;
    }
    SyncChromeBits(); SyncMarks();
}

// ---------------------------------------------------------------- the launcher's history: conversations whose element is gone
std::wstring PageShort(const std::string& url) { std::wstring w = W(url); if (const auto p = w.find(L"://"); p != std::wstring::npos) w = w.substr(p + 3); return w.size() > 90 ? w.substr(0, 89) + L"…" : w; }
bool CommentHistory(bool open, bool keyboard) {
    if (!Ann::s.active || !CommentLayout() || !site.local || site.origin.empty()) return false;
    AUI::HistoryData d; d.currentSite = W(site.label); const auto now = BA::SystemNow();
    for (const auto& h : anchored) {
        if (h.origin != site.origin || onPage.count(h.id)) continue;
        AUI::HistoryEntry e; e.id = L"conv:" + W(h.id); e.site = W(h.siteLabel.empty() ? site.label : h.siteLabel); e.page = PageShort(h.anchorPage);
        e.note = W(h.title.empty() ? h.anchorLabel : h.title); e.status = MarkState(h.id) == "working" ? AUI::EntryStatus::Working : AUI::EntryStatus::Done;
        e.agent = h.agentId == "codex" ? AUI::AgentKind::Codex : AUI::AgentKind::Claude; e.agentName = W(std::string(AgentOf(h.agentId) ? AgentOf(h.agentId)->name : Agent().name));
        e.age = Ann::Age(h.updatedAt, now); e.followUp = TRF(chat_gone_followup, W(h.anchorLabel));
        d.entries.push_back(std::move(e));
    }
    if (open) { if (Ann::launcher.ShowHistory(d, keyboard)) Diag(L"history of the conversations whose element is gone: " + std::to_wstring(d.entries.size())); }
    else if (Ann::launcher.HistoryVisible()) Ann::launcher.UpdateHistory(d);
    return true;
}
bool OpenFromHistory(const std::wstring& id) {
    if (id.rfind(L"conv:", 0) != 0) return false;
    Ann::launcher.HideHistory();
    const std::string conv = U8(id.substr(5)); const auto* h = AnchoredOf(conv); const std::string label = h ? h->anchorLabel : std::string();
    ShowConversation(conv, [conv, label] { if (CommentLayout()) OpenBubble(conv, nullptr, "", false, label); else OpenPanel(); });
    return true;
}
} // namespace Chat
