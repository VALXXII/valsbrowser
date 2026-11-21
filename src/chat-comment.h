#pragma once
// Chat intégré — Mode Commentaire (design/chat-integration.md « Mode Commentaire », contract B 1.8 §19, Paper 18NS-0 …
// 18IJ-0). Included by src/chat-host.h after the panel, the turn and the batch: same engine, other layout.
//
//   switch    the band's two icons (Paper 1AYP-0): Commentaire (a bubble on the element clicked) | Panneau (the chat at
//             the right, as before). Remembered for all sites (projects.json « layout »).
//   bubble    a native popup window owned by the browser window that hosts THE chat WebView (moved there with
//             put_ParentWindow; the panel gets it back the same way): 320 CSS px wide (R20, the user: Paper said 305), as high as the page says
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
inline constexpr int BubbleWidthCss = 320,   /* R20: the user's decision (Paper: 305) */ BubbleMaxHeightCss = 520, BubbleStartHeightCss = 104, BubbleGapCss = 12, BubbleLeadCss = 4, BubbleMarginCss = 12;   // Paper: 12 px from the element, 4 px above it

struct BubbleState {
    HWND hwnd = nullptr; bool open = false; std::string conv;      // the conversation it shows
    std::string selector, label;                                     // the element it follows ("" + current: the element selected now)
    RECT element{}; bool known = false;                              // the element on screen (px), once the page said where
    int w = 0, h = 0; ChatBubble::Side side = ChatBubble::Side::Right;
    double closedAt = -1e9;                                          // NowMs() of the last close (a click that closed it is not a new comment)
    double openedAt = -1e9;                                          // NowMs() of the last opening: the click that opened it gives the page the focus back first
    bool modal = false;                                              // a Windows dialog of ours is up: its deactivation does not close the bubble
    ChatBubble::Edges over, inset;                                   // R3: what the page asks around its surface / where the surface is in the window (CSS px)
    RECT win{}; RECT region{};                                       // R19: the window as placed, its region (window px): changed only when needed
    bool waitingAck = false;                                         // R18: placed and sized, shown once the page acknowledged the opening layout
    bool away = false;                                               // open, hidden while another application is in front; shown again when the browser comes back
    ChatBubble::Point click;                                         // R44: where the click was (screen px): the corner of a big element nearest to it
    bool fromBottom = false; ChatBubble::Rect surface;               // R44: the edge that stays; where the surface is (screen px)
    bool hadKeyboard = false;                                        // M4 (a): the bubble had the keyboard when another application came in front
    int roomAreaH = 0;                                               // M15: the page area's height the offered room was measured with
    bool frameKept = false; ChatBubble::Rect frameRel;              // E4: the window of this opening, relative to the surface's fixed corner
    bool noFollow = false;                                           // C3: opened from the history (its element is gone): no selector followed
    bool lost = false;                                               // C3: its element is not found while its conversation works: kept at its last place
    bool measured = false;                                           // R44: the page measured the element since this opening (the click's rectangle was only a first guess)
    bool warm = false;                                               // R18: the WebView is in the window, rendering, the window shown with an empty region
    bool followCurrent = false;                                      // the deferred work of the opening (FinishOpening)
    ChatBubble::Anchor anchor;                                       // R29: frozen once placed (offset from the element, fixed edge)
    int roomCss = 0;                                                 // R38: the height offered to the page (room below / above its fixed edge)
    bool resized = false; int baseHCss = 0;                          // R40: a size the user chose with the handle (diff open) / the height before it
    ChatBubble::ResizeMax resizeMax;                                 // R40: sent with every layout
    bool endAsked = false;                                           // R29: the document's end asked once per opening
    int sizeW = -1, sizeH = -1;                                      // the last bubble.size (a repeat gets no answer: contract §20)
} bubble;
std::map<std::string, Element> anchorElements;   // conversation id -> its element (image, source): joins every message of its bubble
std::map<std::string, Convo> parked;             // conversations with a message waiting, neither the engine's nor shown
std::vector<ChatStore::Header> anchored;         // this site's conversations that belong to an element (index + memory)
std::set<std::string> onPage;                    // the ones whose element is in the page now
bool anchoredLoading = false;

bool BubbleOpen() { return bubble.open; }
bool BubbleWarm() { return bubble.warm; }
std::string BubbleSelector() { return bubble.selector; }
void ForgetPage() { onPage.clear(); }
void SyncMarks();
void FollowBubbleElement(bool current);
// The page's script is (again) in the page — after a reload (the agent's edit reloads the page) its state is new: the
// marks, and the open bubble's element (D1: its frame + labels, no hover frame), are given to it again.
void WarmBubble(); void CoolBubble();
const ChatStore::Header* AnchoredOf(const std::string& id);
void ScriptReady() {   // at the first injection AND after every reload (annotation-mode.h: both paths call it)
    const auto r = ChatBubble::OnScriptIn(CommentLayout(), bubble.open);
    if (r.marks && !bubble.open) WarmBubble();   // R18: the bubble ready before the first click
    if (r.marks) SyncMarks();
    if (bubble.open && !bubble.noFollow) if (auto t = ChatTab()) {   // F8 (review R): another page of the site now — the bubble's conversation belongs to its own page
        std::string page; if (const auto* h = AnchoredOf(bubble.conv)) page = h->anchorPage;
        else if (convo.data.head.id == bubble.conv) page = convo.data.head.anchorPage; else if (aside.on && aside.c.data.head.id == bubble.conv) page = aside.c.data.head.anchorPage;
        if (!page.empty() && page != ChatBubble::PageKey(U8(t->uri))) { Diag(L"another page is shown: the bubble of the previous one closes"); CloseBubble(false); return; }   // i18n-exempt: log detail
    }
    if (r.follow) { Diag(L"the page script came in again: the bubble's element is given back"); FollowBubbleElement(false); }
}
std::string ShownId() { return aside.on ? aside.c.data.head.id : convo.data.head.id; }
const ChatStore::Header* AnchoredOf(const std::string& id) { for (const auto& h : anchored) if (h.id == id) return &h; return nullptr; }
std::string ModeOrigin();
bool HasQueued(const std::string& id);
// R2: something left this conversation's bubble (a message sent or waiting, an annotation kept).
bool SentConvo(const Convo& c) {
    std::vector<std::string> types; for (const auto& i : c.data.items) if (const auto* t = i.Get("type"); t && t->IsString()) types.push_back(t->string);
    return ChatBubble::Sent(types, HasQueued(c.data.head.id));
}
// A conversation's header joins the list (or is updated) when it is saved — once something was sent from it (R2: a click
// on an element, then closing its bubble, leaves no frame + icon).
void NoteAnchored(const ChatStore::Header& h) {
    if (!h.Anchored() || !h.sent || h.origin != ModeOrigin()) return;   // Q 6: an element's conversation kept by key too
    if (h.id == convo.data.head.id && !SentConvo(convo)) return;
    for (auto& a : anchored) if (a.id == h.id) { a = h; return; }
    anchored.insert(anchored.begin(), h);
    SyncMarks();   // a new conversation of an element: its frame + icon now
}
void SyncMarks(); void LauncherRefresh(); std::string MarkState(const std::string& id);
std::string ShownSelector(const std::string& id) { for (const auto& h : anchored) if (h.id == id) return h.anchorSelector; return ""; }
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
                 for (const auto& h : *out) if (h.origin == origin && h.Anchored() && h.sent) keep.push_back(h);   // R32: never one that sent nothing
                 for (const Convo* mem : {&convo, aside.on ? &aside.c : nullptr}) if (mem && mem->data.head.Anchored() && SentConvo(*mem)) { bool in = false; for (const auto& k : keep) in = in || k.id == mem->data.head.id; if (!in && !mem->data.head.id.empty()) keep.insert(keep.begin(), mem->data.head); }   // R2: only once sent
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
    if (ChatBubble::BubbleLayout(CommentLayout(), Shown(), bubble.open, bubble.warm)) {   // R34: one rule
        m.Set("mode", Str(std::string("bubble"))); m.Set("maxWidth", SJ::Value::Int(BubbleWidthCss)); m.Set("maxHeight", SJ::Value::Int(bubble.roomCss > 0 ? bubble.roomCss : std::min(BubbleMaxHeightCss, FreeHeightCss())));   // R38
        m.Set("side", Str(std::string(SideName(bubble.side)))); if (!bubble.label.empty()) m.Set("element", Str(bubble.label)); m.Set("open", SJ::Value::Bool(openNow));
        {   // F6 (P's contract): the element's stable key — the page keeps a bubble's draft per element until its first send
            std::string key = bubble.selector; if (key.empty()) if (const auto* h = AnchoredOf(bubble.conv)) key = h->anchorSelector.empty() ? h->anchorSource : h->anchorSelector;
            if (!key.empty()) m.Set("elementKey", Str(ClipUtf8(key, 1000)));
        }
        if (bubble.resizeMax.width > 0) {   // R40: the handle's bounds (the anchored corner stays where it is)
            SJ::Value mx = SJ::Value::Obj(); mx.Set("width", SJ::Value::Int(std::max(BubbleWidthCss, bubble.resizeMax.width))); mx.Set("height", SJ::Value::Int(bubble.resizeMax.height)); m.Set("resizeMax", std::move(mx));
            SJ::Value mn = SJ::Value::Obj(); mn.Set("width", SJ::Value::Int(BubbleWidthCss)); mn.Set("height", SJ::Value::Int(bubble.baseHCss > 0 ? bubble.baseHCss : bubble.sizeH)); m.Set("resizeMin", std::move(mn));
        }
        if (bubble.surface.W() > 0) {   // E4 (b): where a menu may really go out of the surface — the page area's edges, not only the window's
            const RECT pa = PageArea(); const float kk = Dpi(); auto css = [&](long v) { return SJ::Value::Int(std::max(0, int(v / kk))); };
            SJ::Value r = SJ::Value::Obj(); r.Set("top", css(bubble.surface.top - pa.top)); r.Set("right", css(pa.right - bubble.surface.right));
            r.Set("bottom", css(pa.bottom - bubble.surface.bottom)); r.Set("left", css(bubble.surface.left - pa.left)); m.Set("room", std::move(r));
        }
        {   SJ::Value in = SJ::Value::Obj(); in.Set("top", SJ::Value::Int(bubble.inset.top)); in.Set("right", SJ::Value::Int(bubble.inset.right)); in.Set("bottom", SJ::Value::Int(bubble.inset.bottom)); in.Set("left", SJ::Value::Int(bubble.inset.left));
            m.Set("inset", std::move(in)); }   // R3: where the surface sits in the window (0 at rest: Paper's bubble has no shadow)
    } else m.Set("mode", Str(std::string("panel")));
    Send("layout", std::move(m));
}
void PlaceBubble(bool scrolling);
// R29: near the end of the document (the page cannot scroll further), a bubble that would be cut moves up once.
void AskDocumentEnd() {
    bubble.endAsked = true; auto t = ChatTab(); if (!t) t = Active(); if (!t || !t->view.p) return;
    const std::string conv = bubble.conv;
    auto cb = Callback<ICoreWebView2ExecuteScriptCompletedHandler, HRESULT, LPCWSTR>([conv](HRESULT hr, LPCWSTR result) -> HRESULT {
        if (!bubble.open || bubble.conv != conv || FAILED(hr) || !result || wcscmp(result, L"true") != 0) return S_OK;
        const RECT a = PageArea(); const float k = Dpi(); const int margin = int(std::lround(BubbleMarginCss * k));
        const int h = bubble.h ? bubble.h : int(std::lround(BubbleStartHeightCss * k));
        const auto r = ChatBubble::Anchored(bubble.anchor, {bubble.element.left, bubble.element.top, bubble.element.right, bubble.element.bottom}, 1, h);
        const int shift = ChatBubble::BottomShift(r, {a.left, a.top, a.right, a.bottom}, margin, true);
        if (shift > 0) { bubble.anchor.dy -= shift; Diag(L"the bubble at the end of the document moves up once by " + std::to_wstring(shift) + L" px"); PlaceBubble(); }
        return S_OK; });
    if (FAILED(t->view->ExecuteScript(L"(function(){var s=document.scrollingElement||document.documentElement;return s.scrollHeight-innerHeight-scrollY<4})()", cb))) {}
    cb->Release();
}
void PlaceBubble(bool scrolling) {   // scrolling (R39): the element moved — the window only follows it, nothing else
    if (!bubble.hwnd || !bubble.open) return;
    const RECT a = PageArea(); const float k = Dpi();
    int w = bubble.w ? bubble.w : int(std::lround(BubbleWidthCss * k)), h = bubble.h ? bubble.h : int(std::lround(BubbleStartHeightCss * k));
    const ChatBubble::Rect area{a.left, a.top, a.right, a.bottom}; const int margin = int(std::lround(BubbleMarginCss * k)), gap = int(std::lround(BubbleGapCss * k));
    if (!bubble.resized) ChatBubble::Fit(w, h, area, margin, int(std::lround(BubbleWidthCss * k)), int(std::lround(BubbleMaxHeightCss * k)));   // R40: a size chosen with the handle is kept
    // no element known yet (opened from the history, or waiting for the page): the bottom right corner of the page
    const RECT e = bubble.known ? bubble.element : RECT{a.right - margin, a.bottom - margin, a.right - margin, a.bottom - margin};
    const ChatBubble::Rect er{e.left, e.top, e.right, e.bottom};
    ChatBubble::Placement p; bool placedNow = false;
    if (!bubble.anchor.set) {   // R29: the first placement chooses the side and the anchor; then they never change
        // R44: the side is chosen for the height the bubble may take (520), so that it is whole in the visible area
        p = ChatBubble::Place(er, w, h, area, gap, margin, bubble.side, int(std::lround(BubbleLeadCss * k)),
                              int(std::lround(BubbleMaxHeightCss * k)), bubble.click, int(std::lround(BubbleGapCss * k)));
        if (bubble.known) {
            bubble.anchor = ChatBubble::AnchorOf(p, er); placedNow = true;
            Diag(std::wstring(L"bubble placed ") + W(std::string(p.why)) + L" (element " + std::to_wstring(er.W()) + L"x" + std::to_wstring(er.H()) + L", visible area " + std::to_wstring(area.W()) + L"x" + std::to_wstring(area.H()) + L")");   // i18n-exempt: log detail
        }
    } else { p.side = bubble.anchor.side; p.fromRight = bubble.anchor.fromRight; p.fromBottom = bubble.anchor.fromBottom; p.rect = ChatBubble::Anchored(bubble.anchor, er, w, h); }
    const bool sideChanged = p.side != bubble.side; bubble.side = p.side; bubble.fromBottom = p.fromBottom; bubble.surface = p.rect;
    if (!scrolling) {   // R38 / R40: the room and the handle's bounds are told on a new size, never at each scroll step (R39)
        const bool areaResized = area.H() != bubble.roomAreaH; bubble.roomAreaH = area.H();   // M15: a new page area size measures it again
        const int room = ChatBubble::OfferedRoomCss(placedNow || !bubble.anchor.set || areaResized, bubble.roomCss, p.rect, p.fromBottom, area, margin, k, BubbleMaxHeightCss);   // R47: kept with the anchor
        const auto rm = ChatBubble::ResizeRoom(p.rect, p.fromRight, p.fromBottom, area, margin, k);
        if (room != bubble.roomCss || rm.width != bubble.resizeMax.width || rm.height != bubble.resizeMax.height) {
            bubble.roomCss = room; bubble.resizeMax = rm; if (panelReady && bubble.open && !bubble.waitingAck) SendLayout(false);
        }
    }
    if (!scrolling && bubble.anchor.set && !bubble.endAsked && p.rect.bottom > area.bottom - margin && !p.fromBottom) AskDocumentEnd();   // R29: the one exception
    // R19: the window is the surface + a fixed transparent margin, as tall as the surface may grow; a menu or a tooltip
    // out of the surface only widens the window's region (nothing moves: same image before and after, to the pixel)
    MONITORINFO mi{sizeof(mi)}; GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &mi);
    // R39: a scroll step carries the same window (size, inset): only a move. Re-framed to the screen (a new inset told to
    // the page, a new WebView size) at the next change of size — never at each image of a scroll.
    // E4 (review R): not only a scroll step — every placement of this opening carries the window chosen at its opening
    // (relative to the surface's fixed corner): framed again only when the surface or its overflow does not fit any more
    if (placedNow) bubble.frameKept = false;
    ChatBubble::Kept kept; if (bubble.frameKept) kept = ChatBubble::FrameKept(p.rect, p.fromRight, p.fromBottom, bubble.frameRel, k, bubble.over);
    if (!kept.ok && scrolling && bubble.win.right > bubble.win.left && !bubble.waitingAck) {   // R39: a scroll never frames again
        kept.ok = true; kept.f = ChatBubble::Reframe(p.rect, bubble.inset, bubble.win.right - bubble.win.left, bubble.win.bottom - bubble.win.top, k, bubble.over);
    }
    const auto f = kept.ok ? kept.f
                           : ChatBubble::FrameBubble(p.rect, int(std::lround(std::min(BubbleMaxHeightCss, FreeHeightCss()) * k)), p.fromBottom,
                                                     {mi.rcWork.left, mi.rcWork.top, mi.rcWork.right, mi.rcWork.bottom}, k, bubble.over);
    if (!kept.ok) { bubble.frameKept = true; bubble.frameRel = ChatBubble::RelativeTo(f.window, ChatBubble::CornerOf(p.rect, p.fromRight, p.fromBottom)); }
    const bool insetChanged = !(f.inset == bubble.inset); bubble.inset = f.inset;
    {   // Q's invariant (region == surface): where the page will draw the surface (its CAP on the inset) is where the host shows it
        auto px = [&](int c) { return int(c * k + .5f); };
        const int dx = f.window.left + px(std::min(f.inset.left, ChatBubble::PageInsetCapCss)) - p.rect.left, dy = f.window.top + px(std::min(f.inset.top, ChatBubble::PageInsetCapCss)) - p.rect.top;
        if (std::abs(dx) > 1 || std::abs(dy) > 1) Diag(L"bubble drawn away from its region (" + std::to_wstring(dx) + L", " + std::to_wstring(dy) + L" px): inset " + std::to_wstring(f.inset.left) + L"," + std::to_wstring(f.inset.top));   // i18n-exempt: log detail
    }
    const RECT wr{f.window.left, f.window.top, f.window.right, f.window.bottom};
    const auto vis = ChatBubble::Clip(f.region, {a.left - f.window.left, a.top - f.window.top, a.right - f.window.left, a.bottom - f.window.top});   // R29: never over the bars
    const RECT rg{vis.left, vis.top, vis.right, vis.bottom};
    const bool regionChanges = !bubble.waitingAck && !EqualRect(&rg, &bubble.region);
    if (!EqualRect(&wr, &bubble.win)) {
        const bool sized = wr.right - wr.left != bubble.win.right - bubble.win.left || wr.bottom - wr.top != bubble.win.bottom - bubble.win.top;
        bubble.win = wr;
        // R39b: the window moves AND its region changes (the element passes under an edge of the page area). Between the two
        // calls the old region would ride with the window over the browser's bars for an image: first the part both regions
        // share (inside the page area at the old place and at the new one), then the move, then the new region.
        if (regionChanges && !bubble.away) {
            RECT both{}; if (!IntersectRect(&both, &bubble.region, &rg)) both = RECT{};
            SetWindowRgn(bubble.hwnd, CreateRectRgn(both.left, both.top, both.right, both.bottom), FALSE);
        }
        // R39: a move (scroll) is only a move — no z-order, no copy of the old pixels, no WebView bounds
        SetWindowPos(bubble.hwnd, nullptr, wr.left, wr.top, wr.right - wr.left, wr.bottom - wr.top, SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER | (sized ? 0 : SWP_NOSIZE));
        if (sized) view.Place({0, 0, wr.right - wr.left, wr.bottom - wr.top});
    }
    if (bubble.waitingAck) bubble.region = rg;   // R18: kept for the reveal; the window shows nothing until then
    else if (regionChanges) { bubble.region = rg; if (!bubble.away) SetWindowRgn(bubble.hwnd, CreateRectRgn(rg.left, rg.top, rg.right, rg.bottom), FALSE); }   // R41: no repaint for a region (no flash); the system owns it now
    if (!scrolling && (sideChanged || insetChanged) && panelReady) SendLayout(false);
}
// R39 (measured frame by frame): the page posts « anchor » from its scroll listener, before the compositor's image of
// that scroll reaches the screen; a window moved at once lands one screen image AHEAD of the content. The move waits
// for the next composition (DwmFlush, on a small thread of its own), then lands with the page's image. Only the latest
// position counts: anchors that arrive meanwhile only update the element. BROWSER_CHAT_FOLLOW_FRAMES=0 moves at once.
struct Follower { HANDLE wake = nullptr; std::atomic<HWND> to{nullptr}; std::atomic<bool> pending{false}; std::atomic<unsigned> gen{0}; } follower;
// F11 (review R): the positions the page sent, by generation — each wait places the one it waited for, a newer one waits for its own
std::deque<std::pair<unsigned, RECT>> followPositions;
int FollowFrames() {
    static const int n = [] { wchar_t v[8] = L""; return GetEnvironmentVariableW(L"BROWSER_CHAT_FOLLOW_FRAMES", v, 8) ? std::clamp(_wtoi(v), 0, 3) : 1; }();
    return n;
}
void FollowLater() {
    if (!follower.wake) {
        follower.wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!follower.wake) { PlaceBubble(true); return; }
        std::thread([] {
            for (;;) {
                WaitForSingleObject(follower.wake, INFINITE);
                const unsigned gen = follower.gen.load();   // F11: the positions sent so far are on their way to the screen
                for (int i = 0; i < FollowFrames(); ++i) DwmFlush();
                if (const HWND h = follower.to.load()) PostMessageW(h, WM_APP + 4, WPARAM(gen), 0);
            }
        }).detach();
    }
    static double askedAt = 0;   // a move asked but never delivered (the window went away meanwhile) does not block the next ones
    if (follower.to.load() != bubble.hwnd || Ann::NowMs() - askedAt > 200) follower.pending = false;
    follower.to = bubble.hwnd;
    if (!follower.pending.exchange(true)) { askedAt = Ann::NowMs(); SetEvent(follower.wake); }
}
RECT BubbleScreenRect() { RECT r{}; if (bubble.hwnd) GetWindowRect(bubble.hwnd, &r); return r; }
// M10: what of the bubble is on screen and takes the clicks (its region: surface + overflow asked), screen px
RECT BubbleRegionScreen() { const RECT w = BubbleScreenRect(); return RECT{w.left + bubble.region.left, w.top + bubble.region.top, w.left + bubble.region.right, w.top + bubble.region.bottom}; }
// Another application in front (test réel: a window of another program took the foreground): the bubble is not closed
// any more — it is hidden, still open (its conversation, its draft, its place), and shown again as it was as soon as a
// window of this browser is in front. One state: open + away = hidden; every opening, closing and click clears it.
void BubbleAway(const wchar_t* who) {
    if (!bubble.open || bubble.away || !bubble.hwnd) return;
    bubble.away = true; SetWindowRgn(bubble.hwnd, CreateRectRgn(0, 0, 0, 0), TRUE);   // an empty region, not SW_HIDE: hiding the active window would give the activation back to the browser (« back » at once)
    Diag(std::wstring(L"the bubble lost the foreground to another application (") + who + L"): hidden, shown again when the browser is back in front");
}
void BubbleBack(const wchar_t* why) {
    if (!bubble.open || !bubble.away || !bubble.hwnd) return;
    bubble.away = false;
    PlaceBubble();   // the page may have moved meanwhile
    if (!bubble.waitingAck) { const RECT rg = bubble.region; SetWindowRgn(bubble.hwnd, CreateRectRgn(rg.left, rg.top, rg.right, rg.bottom), TRUE); ShowWindow(bubble.hwnd, SW_SHOWNA); }
    if (bubble.hadKeyboard && !bubble.waitingAck) { SetForegroundWindow(bubble.hwnd); view.Focus(); }   // M4 (a): it had the keyboard: it gets it back
    Diag(std::wstring(L"the bubble is shown again (") + why + L")");
}
void CALLBACK ForegroundChanged(HWINEVENTHOOK, DWORD, HWND fg, LONG, LONG, DWORD, DWORD) {
    if (!bubble.open || !fg || !bubble.hwnd) return;
    DWORD pid = 0; GetWindowThreadProcessId(fg, &pid);
    if (pid == GetCurrentProcessId()) {
        if (bubble.away) BubbleBack(L"the browser is back in front");   // i18n-exempt: log detail
        // Q pass 1, defect 4 (M4): the bubble had the keyboard when another application came — the browser back in front gives
        // it back, also when the bubble was not hidden (a short visit) — else the keys and Échap went to the browser's band
        else if (bubble.hadKeyboard && fg != bubble.hwnd && !bubble.waitingAck) { Diag(L"the browser is back in front: the keyboard goes back to the bubble"); PostMessageW(bubble.hwnd, WM_APP + 2, 0, 0); }   // i18n-exempt: log detail
        return;
    }
    // another application in front while the browser's window (not the bubble) had it: the same check as WM_ACTIVATE's
    if (!bubble.away && !bubble.modal) PostMessageW(bubble.hwnd, WM_APP + 1, 0, 0);
}
void BubbleModal(bool on) { bubble.modal = on; }
void CloseBubble(bool focusPage); void RevealBubble(const wchar_t* why); void FinishOpening();
LRESULT CALLBACK BubbleProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    // A click elsewhere — the page, the browser's chrome, another window — takes the activation from the bubble: it closes
    // (its agent goes on). Not while one of our own dialogs is up (image picker, folder picker).
    // Another application takes the foreground: the bubble closes. The browser's own windows do not count: a click in the
    // page is a selection (TakeCommentSelection closes the bubble, or opens the element's), and the mode's capture and
    // focus moves activate the browser window without any click.
    if (m == WM_ACTIVATE && (LOWORD(w) == WA_ACTIVE || LOWORD(w) == WA_CLICKACTIVE) && bubble.open) { bubble.hadKeyboard = true; view.Focus(); }   // M4 (a): activated — its page gets the keyboard
    if (m == WM_ACTIVATE && LOWORD(w) == WA_INACTIVE && bubble.open) {   // M4 (a): to a window of this browser, the keyboard is the browser's
        DWORD p0 = 0; HWND to0 = reinterpret_cast<HWND>(l); if (to0) GetWindowThreadProcessId(to0, &p0); if (to0 && p0 == GetCurrentProcessId()) bubble.hadKeyboard = false;
    }
    if (m == WM_ACTIVATE && LOWORD(w) == WA_INACTIVE && bubble.open && !bubble.modal) {
        DWORD pid = 0; HWND to = reinterpret_cast<HWND>(l); if (to) GetWindowThreadProcessId(to, &pid);
        if (!to || pid != GetCurrentProcessId()) PostMessageW(h, WM_APP + 1, 0, 0);
        return 0;
    }
    // the selection that opened it ends by giving the page the keyboard (Ann::Resume): not a click elsewhere
    // R9: the foreground may be nobody for a moment (a process starting, the agent's own helpers): looked at again before
    // closing, and a window of this browser keeps the bubble. The log says who took it (a bubble closed right after a
    // message was sent was the start of the user's stuck turn).
    if (m == WM_APP + 1 || (m == WM_TIMER && w == 0xB1)) {
        if (m == WM_TIMER) KillTimer(h, 0xB1);
        if (!bubble.open || bubble.modal) return 0;
        if (const double since = Ann::NowMs() - bubble.openedAt; since <= 500) { SetTimer(h, 0xB1, UINT(520 - since), nullptr); return 0; }   // just opened: looked at again after, not dropped
        const HWND fg = GetForegroundWindow(); DWORD pid = 0; if (fg) GetWindowThreadProcessId(fg, &pid);
        if (!fg) { if (m == WM_APP + 1) SetTimer(h, 0xB1, 250, nullptr); else BubbleAway(L"nobody"); return 0; }
        if (fg == h || pid == GetCurrentProcessId()) return 0;
        wchar_t cls[64] = L""; GetClassNameW(fg, cls, 64);
        BubbleAway((std::wstring(cls) + L", pid " + std::to_wstring(pid)).c_str()); return 0;
    }
    if (m == WM_APP + 2) { if (bubble.open && !bubble.away) { SetForegroundWindow(h); view.Focus(); } return 0; }   // the keyboard to the bubble, once the selection is over
    if (m == WM_APP + 3) { FinishOpening(); return 0; }   // R18: after the opening layout
    if (m == WM_APP + 4) {   // R39: the page's image is on screen — F11: the position this wait was for, not a newer one
        follower.pending = false; const unsigned gen = unsigned(w); bool any = false; RECT at{};
        while (!followPositions.empty() && followPositions.front().first <= gen) { at = followPositions.front().second; followPositions.pop_front(); any = true; }
        if (any) { bubble.element = at; if (bubble.open && bubble.known && bubble.anchor.set) PlaceBubble(true); }
        if (!followPositions.empty()) FollowLater();
        return 0;
    }
    if (m == WM_TIMER && w == 0xB2) { KillTimer(h, 0xB2); RevealBubble(L"without acknowledgement (safety delay)"); return 0; }   // i18n-exempt: log detail
    if (m == WM_CLOSE) { CloseBubble(true); return 0; }
    {   LRESULT r = 0; if (view.HostMessage(h, m, w, l, r)) return r; }   // R1: the page is a visual of this window: mouse, cursor, UI Automation
    if (m == WM_ERASEBKGND) return 1;   // no redirection surface: what the page does not paint is see-through
    return DefWindowProcW(h, m, w, l);
}
bool EnsureBubbleWindow() {
    if (bubble.hwnd) return true;
    WNDCLASSEXW wc{sizeof(wc)}; wc.lpfnWndProc = BubbleProc; wc.hInstance = instance; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.lpszClassName = L"BrowserChatBubble"; RegisterClassExW(&wc);
    // R1: no redirection bitmap — the window shows only its DirectComposition content (the page, transparent around its
    // 16 px antialiased corners); no DWM rounding (it would bring a shadow Paper does not draw, and its own 8 px radius),
    // no border, no region (stair-stepped).
    bubble.hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP, wc.lpszClassName, TR(chat_bubble_title), WS_POPUP | WS_CLIPCHILDREN, 0, 0, 10, 10, window, nullptr, instance, nullptr);
    if (!bubble.hwnd) return false;
    {   const int corner = 1 /*DWMWCP_DONOTROUND*/; DwmSetWindowAttribute(bubble.hwnd, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &corner, sizeof(corner));
        const COLORREF none = 0xFFFFFFFE /*DWMWA_COLOR_NONE*/; DwmSetWindowAttribute(bubble.hwnd, 34 /*DWMWA_BORDER_COLOR*/, &none, sizeof(none)); }
    view.HostDirectly(bubble.hwnd);
    static HWINEVENTHOOK foreground = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, ForegroundChanged, 0, 0, WINEVENT_OUTOFCONTEXT);   // the bubble hidden by another application comes back with the browser
    (void)foreground;
    return true;
}
// R18 (< 100 ms from the click to the slide): while Mode Commentaire is on, the bubble's WebView is created and loaded
// in advance, in the bubble's window, shown with an EMPTY region (nothing on screen, nothing clickable, the page renders).
// A click then only places the window, sends the opening layout and reveals the surface on the page's acknowledgement.
void WarmBubble() {
    auto t = Active(); if (!t || !Ann::s.active || Ann::ModeTab() != t || halted || privateWindow || !CommentLayout() || Shown() || bubble.open) return;   // R34: not while the panel slides out
    if (WarmHeldBack()) return;   // QA only
    EnsureInit(); if (!EnsureBubbleWindow()) return;
    if (post) KillTimer(post, DestroyTimer);
    const RECT a = PageArea(); const float k = Dpi(); const int w = int(std::lround((BubbleWidthCss + 2 * BubbleMarginCss) * k)), h = int(std::lround(BubbleMaxHeightCss * k));
    SetWindowRgn(bubble.hwnd, CreateRectRgn(0, 0, 0, 0), FALSE);
    SetWindowPos(bubble.hwnd, HWND_TOP, a.left, a.top, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    bubble.win = RECT{}; bubble.region = RECT{};
    if (!view.Usable(environment.p)) { view.Destroy(); panelReady = false; if (!view.Create(environment.p, bubble.hwnd, {0, 0, w, h})) return; Diag(L"bubble WebView created in advance (Mode Commentaire)"); }
    else if (view.Parent() != bubble.hwnd) view.Reparent(bubble.hwnd, {0, 0, w, h});
    else view.Place({0, 0, w, h});
    view.Show(true);
    if (!bubble.warm) { bubble.warm = true; if (panelReady) SendLayout(false); }
}
void CoolBubble() {   // Mode Commentaire left (or the panel chosen): the window goes away; the WebView is destroyed when idle, as before
    if (!bubble.warm || bubble.open) return;
    bubble.warm = false; if (bubble.hwnd) ShowWindow(bubble.hwnd, SW_HIDE);
    if (!Open()) { view.Show(false); if (post) SetTimer(post, DestroyTimer, PanelIdleMs(), nullptr); }
}
void FinishOpening();
// R44: a click on the element whose bubble is open never does nothing: the bubble is brought to the front, and placed
// again (R44 rules) when it is not whole in the visible area.
void EnsureOnScreen(const wchar_t* why) {
    if (!bubble.open || !bubble.hwnd || bubble.waitingAck) return;
    const RECT a = PageArea(); const ChatBubble::Rect area{a.left, a.top, a.right, a.bottom};
    if (!ChatBubble::FullyVisible(bubble.surface, area) || !IsWindowVisible(bubble.hwnd) || IsRectEmpty(&bubble.region)) {
        Diag(std::wstring(L"the bubble was not whole on screen (") + why + L"): placed again");   // i18n-exempt: log detail
        POINT c{}; bubble.click = GetCursorPos(&c) && PtInRect(&bubble.element, c) ? ChatBubble::Point{c.x, c.y, true} : ChatBubble::Point{};
        bubble.anchor = ChatBubble::Anchor{}; bubble.side = ChatBubble::Side::Right;
        PlaceBubble();
        const RECT rg = bubble.region; SetWindowRgn(bubble.hwnd, CreateRectRgn(rg.left, rg.top, rg.right, rg.bottom), TRUE); ShowWindow(bubble.hwnd, SW_SHOWNA);
    }
    SetForegroundWindow(bubble.hwnd); PostMessageW(bubble.hwnd, WM_APP + 2, 0, 0);
}
void OpenBubble(const std::string& conv, const RECT* element, const std::string& selector, bool followCurrent, const std::string& label) {
    auto t = Active(); if (!t || !Ann::s.active || Ann::ModeTab() != t || halted || privateWindow) return;
    if (Open()) ClosePanel();
    if (panelState == PanelState::Closing) FinishClose();   // M5 (review R): the panel's slide ends now — it would move and hide the view the bubble takes
    EnsureInit(); if (!EnsureBubbleWindow()) return;
    PrepareChat(t);   // once per page (cheap again)
    if (post) KillTimer(post, DestroyTimer);
    bubble.followCurrent = followCurrent;
    // Bug « clic, pas de bulle » (test réel, 0.3.473): the bubble of A was open, a click on B (its own conversation) came
    // here with `again` and kept A's anchor, side and region — for a wide B the bubble landed out of the page area, clipped
    // to nothing, and no reveal was asked. Only the SAME element (the click's full « select » after its « selecting »)
    // keeps the open bubble; another element is a new opening (placement, region and reveal from scratch).
    const bool again = bubble.open && ChatBubble::SameBubble(bubble.conv, bubble.selector, conv, selector);
    if (bubble.open && !again) Diag(L"the bubble moves to another element: opened again from scratch");
    if (bubble.away) BubbleBack(L"a click on an element");   // i18n-exempt: log detail
    bubble.open = true; bubble.conv = conv; bubble.selector = selector; bubble.label = label;
    bubble.known = element != nullptr; if (element) bubble.element = *element;
    if (!again) {   // R44: the pointer is still where the user clicked
        POINT c{}; bubble.click = ChatBubble::Point{};
        if (element && GetCursorPos(&c) && PtInRect(element, c)) bubble.click = ChatBubble::Point{c.x, c.y, true};
        bubble.fromBottom = false; bubble.surface = ChatBubble::Rect{}; bubble.measured = false; bubble.noFollow = false; bubble.lost = false; bubble.frameKept = false;
    }
    if (!again) { bubble.side = ChatBubble::Side::Right; bubble.h = 0; bubble.over = bubble.inset = ChatBubble::Edges{}; bubble.sizeW = bubble.sizeH = -1; bubble.anchor = ChatBubble::Anchor{}; bubble.endAsked = false; bubble.roomCss = 0; bubble.resized = false; bubble.baseHCss = 0; bubble.resizeMax = ChatBubble::ResizeMax{}; }
    stats = OpenStats{}; stats.requestedAt = Ann::NowMs();
    if (!view.Usable(environment.p)) { view.Destroy(); panelReady = false; if (!view.Create(environment.p, bubble.hwnd, {0, 0, 10, 10})) { bubble.open = false; Diag(L"bubble WebView creation refused"); return; } }
    else view.Reparent(bubble.hwnd, {0, 0, 10, 10});
    if (!again) { bubble.win = RECT{}; bubble.region = RECT{}; }
    PlaceBubble();
    bubble.openedAt = Ann::NowMs();
    view.Show(true);   // the page renders and measures while the window shows nothing
    if (again && IsWindowVisible(bubble.hwnd) && !bubble.waitingAck) { EnsureOnScreen(L"a click on its element"); }   // i18n-exempt: log detail
    else {   // R18: an empty region (the page renders and measures), the surface revealed at its place once it says its size
        bubble.waitingAck = true; SetWindowRgn(bubble.hwnd, CreateRectRgn(0, 0, 0, 0), FALSE);
        ShowWindow(bubble.hwnd, SW_SHOWNA); SetTimer(bubble.hwnd, 0xB2, panelReady ? 300 : 2000, nullptr);   // a page still being created answers later
    }
    // R18: the opening layout leaves first; everything else waits for the next turn of the message loop
    if (panelReady) {
        SendLayout(true);
        // M2 (review R, P's contract): the region is shown at the page's acknowledgement of this layout (its bubble.size),
        // never before — revealed at once, the page still showed the previous conversation for a few images
        SendState(); SendThread(); SendQueue();
        SJ::Value f = SJ::Value::Obj(); f.Set("target", Str(std::string("composer"))); Send("focus", std::move(f));
    }
    PostMessageW(bubble.hwnd, WM_APP + 3, 0, 0);
    Ann::Announce(TRF(chat_bubble_announce, W(label.empty() ? std::string("—") : label)));
    Diag(L"bubble opened on " + W(label.empty() ? std::string("(no element)") : label) + L" for conversation " + W(conv) + (again ? L" (was open)" : L""));
}
// R18: what the opening does not need before the surface shows (the agent's start, the element's frame, the marks)
// One truth: an open bubble that is neither hidden (another application) nor waiting for its page IS on screen — window
// visible, region not empty. Checked after every opening; a mismatch is repaired (placed again from scratch) and logged.
void CheckBubbleShown() {
    if (!bubble.open || bubble.away || bubble.waitingAck || !bubble.hwnd) return;
    const bool visible = IsWindowVisible(bubble.hwnd) != FALSE, empty = IsRectEmpty(&bubble.region) != FALSE;
    const RECT a = PageArea(); const bool whole = !bubble.known || ChatBubble::FullyVisible(bubble.surface, ChatBubble::Rect{a.left, a.top, a.right, a.bottom});   // R44
    if (visible && !empty && whole) return;
    Diag(std::wstring(L"the bubble was open but not on screen (") + (!visible ? L"window hidden" : empty ? L"empty region" : L"not whole in the visible area") + L"): placed again");   // i18n-exempt: log detail
    bubble.anchor = ChatBubble::Anchor{}; bubble.win = RECT{}; bubble.region = RECT{}; bubble.side = ChatBubble::Side::Right;
    PlaceBubble(); ShowWindow(bubble.hwnd, SW_SHOWNA);
}
void FinishOpening() {
    if (!bubble.open) return;
    CheckBubbleShown();
    if (panelReady) view.Focus();
    ArmAgentIdle(); Ann::LauncherSync(); SyncChromeBits();
    FollowBubbleElement(bubble.followCurrent);   // the page follows the element (its moves become `anchor` messages)
    SyncMarks();
    MaybeStartSession();   // R13: its agent starts now (model list, model · effort), as the panel's does
}
void FollowBubbleElement(bool current) {
    auto t = ChatTab(); if (!t) t = Active(); if (!t || !bubble.open) return;
    if (bubble.noFollow) { Diag(L"the bubble was opened from the history: no element followed"); return; }   // C3   // i18n-exempt: log detail
    std::string sel = bubble.selector;
    if (sel.empty()) if (const auto* h = AnchoredOf(bubble.conv)) sel = h->anchorSelector;
    if (sel.empty() && convo.data.head.id == bubble.conv) sel = convo.data.head.anchorSelector;
    SJ::Value f = SJ::Value::Obj(); if (current) f.Set("current", SJ::Value::Bool(true)); else f.Set("selector", Str(sel));
    SJ::Value d = SJ::Value::Obj(); d.Set("label", Str(bubble.label)); f.Set("draw", std::move(d));   // D1: the element's frame + its two labels
    if (!current && sel.empty()) { Diag(L"the bubble's element has no selector: not followed"); return; }
    Query(t, L"follow", W(SJ::Serialize(f)), [current](const std::string& r) { if (!current) Diag(L"the bubble's element followed by its selector: " + std::wstring(r == "true" ? L"found" : L"not found (" + W(r.substr(0, 40)) + L")")); });
}
// R18: the window appears once, already at its place and size (the page plays its entrance slide).
void RevealBubble(const wchar_t* why) {
    if (!bubble.open || !bubble.waitingAck || !bubble.hwnd) return;
    bubble.waitingAck = false; KillTimer(bubble.hwnd, 0xB2);
    if (bubble.away) { Diag(L"bubble ready at its place, hidden while another application is in front"); return; }   // BubbleBack shows it
    const RECT rg = bubble.region; SetWindowRgn(bubble.hwnd, CreateRectRgn(rg.left, rg.top, rg.right, rg.bottom), TRUE);   // the surface appears, at its place
    ShowWindow(bubble.hwnd, SW_SHOWNA); SetForegroundWindow(bubble.hwnd);
    PostMessageW(bubble.hwnd, WM_APP + 2, 0, 0);   // the caret to the bubble
    Diag(std::wstring(L"bubble shown at its place ") + why + L" after " + Ann::Ms(Ann::NowMs() - bubble.openedAt) + L" ms");
}
// M6 / F5 (review R): a bubble closed with nothing sent (or the panel chosen) leaves no conversation tied to its element —
// the panel showed it titled with the element, and a first message from the panel marked the element; its image is freed
void ForgetUnsent(const std::string& conv) {
    if (conv.empty()) return;
    Convo* c = convo.data.head.id == conv ? &convo : (aside.on && aside.c.data.head.id == conv ? &aside.c : nullptr);
    if (!c || SentConvo(*c) || HasQueued(conv) || (turn.active && c == &convo)) return;
    anchorElements.erase(conv); parked.erase(conv);
    if (c->data.head.anchorSelector.empty() && c->data.head.anchorSource.empty()) return;
    auto& h = c->data.head; h.anchorSelector.clear(); h.anchorLabel.clear(); h.anchorPage.clear(); h.anchorSource.clear(); h.title.clear();
    Diag(L"nothing was sent from that bubble: its conversation is no longer tied to the element");   // i18n-exempt: log detail
}
void CloseBubble(bool focusPage) {
    if (!bubble.open) return;
    ForgetUnsent(bubble.conv);   // M6 / F5
    const bool wasAway = bubble.away;
    bubble.open = false; bubble.away = false; bubble.closedAt = Ann::NowMs(); bubble.waitingAck = false; if (bubble.hwnd) KillTimer(bubble.hwnd, 0xB2);
    const bool keepWarm = CommentLayout() && Ann::s.active && !privateWindow;   // R18: ready for the next click
    if (bubble.hwnd) { if (keepWarm) { SetWindowRgn(bubble.hwnd, CreateRectRgn(0, 0, 0, 0), TRUE); if (wasAway) ShowWindow(bubble.hwnd, SW_SHOWNA); bubble.win = RECT{}; bubble.region = RECT{}; bubble.warm = true; } else ShowWindow(bubble.hwnd, SW_HIDE); }   // warm: shown, empty region
    if (!keepWarm) { view.Show(false); bubble.warm = false; if (post) SetTimer(post, DestroyTimer, PanelIdleMs(), nullptr); }
    if (auto t = ChatTab()) Query(t, L"follow", L"null", nullptr);
    if (panelReady) Send("bubble.hidden", SJ::Value::Obj());   // M2 / F4 (P's contract): invisible, inert, aria-hidden, thread emptied — never the old one shown again
    ArmAgentIdle(); SyncMarks();
    Diag(L"bubble closed (the agent goes on)");
    if (focusPage && Ann::s.active) { SetForegroundWindow(window); FocusChatPage(); }
}
void OnBubbleSize(int wCss, int hCss, ChatBubble::Edges over, bool resized) {
    if (!bubble.open) return;
    if (!resized && bubble.resized) Diag(L"the bubble is back to its size (diff closed)");
    bubble.resized = resized;   // R40
    over = ChatBubble::Bounded(over);
    if (wCss == bubble.sizeW && hCss == bubble.sizeH && over == bubble.over) { RevealBubble(L"on the acknowledgement"); return; }   // contract §20: a repeat gets no answer   // i18n-exempt: log detail
    bubble.sizeW = wCss; bubble.sizeH = hCss; bubble.over = over;
    const float k = Dpi();
    if (resized) {   // R40: the handle — width 320 .. its room, height the size before the diff .. its room; the anchored corner stays
        const int maxW = std::max(BubbleWidthCss, bubble.resizeMax.width), maxH = std::max(bubble.baseHCss, bubble.resizeMax.height);
        bubble.w = int(std::lround(std::clamp(wCss, BubbleWidthCss, std::max(BubbleWidthCss, maxW)) * k)); bubble.h = int(std::lround(std::clamp(hCss, std::max(40, bubble.baseHCss), std::max(40, maxH)) * k));
    } else {
        bubble.baseHCss = std::clamp(hCss, 40, BubbleMaxHeightCss);
        bubble.w = int(std::lround(std::clamp(wCss, 200, BubbleWidthCss) * k)); bubble.h = int(std::lround(std::clamp(hCss, 40, BubbleMaxHeightCss) * k));
    }
    const auto before = bubble.inset; PlaceBubble();
    if (bubble.inset == before && panelReady) SendLayout(false);   // R3: every new bubble.size gets its inset (PlaceBubble sent it if it changed)
    RevealBubble(L"on the acknowledgement");   // R18   // i18n-exempt: log detail
}
// The element the bubble follows moved (scroll, layout, reload) or is gone.
// C3: the bubble's conversation works or waits — a turn of it runs, an authorization of it waits, or a message of it is queued
bool BubbleBusy() {
    const bool engine = !aside.on && bubble.conv == convo.data.head.id;
    return (engine && turn.active) || (engine && !waitingPermissions.empty()) || HasQueued(bubble.conv);
}
void OnAnchor(const AH::Envelope& e) {
    if (!bubble.open) return;
    if (e.gone) {   // C3 (review R): never closed while its conversation works or waits — kept at its last place
        if (BubbleBusy()) { if (!bubble.lost) { bubble.lost = true; Diag(L"the bubble's element is not found while its conversation works: the bubble stays at its last place"); } return; }   // i18n-exempt: log detail
        Diag(L"the bubble's element is no longer in the page, nothing runs: closed (its conversation is in the history)"); CloseBubble(false); return;   // i18n-exempt: log detail
    }
    if (bubble.lost) {   // C3: found again — followed and placed again from where it is now
        bubble.lost = false; bubble.anchor = ChatBubble::Anchor{}; bubble.side = ChatBubble::Side::Right; bubble.measured = true;
        Diag(L"the bubble's element is found again: followed");   // i18n-exempt: log detail
    }
    if (!bubble.known) bubble.anchor = ChatBubble::Anchor{};   // R29: the first real position of the element decides the anchor
    const RECT measured = ToScreen(e.x, e.y, e.w, e.h, e.vw);
    if (!bubble.measured) {   // R44: the first measure of the page is the truth; the anchor taken from the click's rectangle is only kept if it is right
        bubble.measured = true;
        const RECT& was = bubble.element;
        if (bubble.known && bubble.anchor.set && (std::abs(was.left - measured.left) > 2 || std::abs(was.top - measured.top) > 2 || std::abs(was.right - measured.right) > 2 || std::abs(was.bottom - measured.bottom) > 2)) {
            Diag(L"the element is not where the click said (" + std::to_wstring(was.left) + L"," + std::to_wstring(was.top) + L" " + std::to_wstring(was.right - was.left) + L"x" + std::to_wstring(was.bottom - was.top) +
                 L" -> " + std::to_wstring(measured.left) + L"," + std::to_wstring(measured.top) + L" " + std::to_wstring(measured.right - measured.left) + L"x" + std::to_wstring(measured.bottom - measured.top) + L"): placed again");   // i18n-exempt: log detail
            bubble.anchor = ChatBubble::Anchor{}; bubble.side = ChatBubble::Side::Right;
        }
    }
    const bool scrolling = bubble.known && bubble.anchor.set;   // R39: a known element that moved: the window follows, nothing else
    bubble.known = true;
    if (scrolling && FollowFrames() > 0) {   // F11: placed when the page's image of THIS position is on screen
        followPositions.push_back({++follower.gen, measured}); while (followPositions.size() > 8) followPositions.pop_front();
        FollowLater();
    } else { followPositions.clear(); bubble.element = measured; PlaceBubble(scrolling); }
    if (scrolling && e.t > 0) {   // R39: the page's scroll (its clock) -> here, summed over 30 steps   // i18n-exempt: log detail
        static int n = 0; static double sum = 0, worst = 0;
        FILETIME ft; GetSystemTimePreciseAsFileTime(&ft); ULARGE_INTEGER u{}; u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime;
        const double lag = std::max(0.0, double(u.QuadPart) / 10000.0 - 11644473600000.0 - e.t);
        ++n; sum += lag; worst = std::max(worst, lag);
        if (n == 30) { Diag(L"anchor: the page's scroll reached the host " + Ann::Ms(sum / n) + L" ms after it on average, " + Ann::Ms(worst) + L" ms at worst (30 steps); the window moves " + std::to_wstring(FollowFrames()) + L" screen image(s) later"); n = 0; sum = worst = 0; }
    }
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
// E5 (review R): the click's early « selecting » and its full « select » both asked to read the same conversation from the
// disk: two readings, two openings, the agent started then stopped. One reading per conversation; the latest « then » wins.
std::string loadingConv; std::function<void()> loadingThen, loadingMissing;
void ShowConversation(const std::string& id, std::function<void()> then, std::function<void()> missing = nullptr) {
    if (id == convo.data.head.id) { if (aside.on) { ParkAside(); SendState(); SendThread(); SendQueue(); RefreshWaitStrip(); } if (then) then(); return; }
    if (aside.on && aside.c.data.head.id == id) { if (then) then(); return; }
    if (auto it = parked.find(id); it != parked.end()) {
        Convo c = std::move(it->second); parked.erase(it);
        if (turn.active) { ShowAside(std::move(c)); if (then) then(); return; }
        ParkAside(); DisposeSession(L"another element");   // i18n-exempt: log reason
        convo = std::move(c); RebuildTurnMaps(); SendState(); SendThread(); SendQueue(); RefreshWaitStrip(); MaybeStartSession();
        if (then) then(); if (HasQueued(convo.data.head.id)) ProcessQueue(); return;
    }
    if (id == loadingConv) { loadingThen = std::move(then); loadingMissing = std::move(missing); Diag(L"conversation already being read: one reading (" + W(id) + L")"); return; }   // E5   // i18n-exempt: log detail
    loadingConv = id; loadingThen = std::move(then); loadingMissing = std::move(missing);
    auto out = std::make_shared<ChatStore::Conversation>(); auto ok = std::make_shared<bool>(false);
    Ann::Run([out, ok, id] { *ok = History().Load(id, *out); },
             [out, ok, id] {
                 if (loadingConv != id) return;   // replaced by another reading meanwhile
                 auto then = std::move(loadingThen); auto missing = std::move(loadingMissing); loadingConv.clear(); loadingThen = nullptr; loadingMissing = nullptr;
                 if (!*ok || out->head.origin != site.origin) {
                     Diag(L"conversation not found: " + W(id));
                     // R6: it is forgotten (its element must not keep a frame that opens nothing)
                     anchored.erase(std::remove_if(anchored.begin(), anchored.end(), [&](const ChatStore::Header& h) { return h.id == id; }), anchored.end());
                     SyncMarks(); if (missing) missing(); return;
                 }
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
std::string NewAnchoredConversation(const std::string& selector, const std::string& label, const std::string& page, const std::string& source, const std::string& key = "") {
    auto setAnchor = [&](ChatStore::Header& h) { h.anchorSelector = selector; h.anchorKey = selector.empty() ? key : std::string(); h.anchorLabel = label; h.anchorPage = page; h.anchorSource = source; if (!label.empty() && h.title.empty()) h.title = ChatPrompt::OneLine(label, 44); };
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
// C2 (review R): a conversation shown « aside » (another element's bubble opened while a turn ran) stayed aside after
// that turn: what was sent from it went into the engine's conversation. When nothing runs, it becomes the engine's —
// the same path as a queued message of another element (SwitchEngineTo).
bool TakeShownAside() {
    if (!aside.on || turn.active || aside.c.data.head.id.empty()) return false;
    const std::string id = aside.c.data.head.id;
    Diag(L"nothing runs: the conversation shown (" + W(id) + L") becomes the engine's");   // i18n-exempt: log detail
    return SwitchEngineTo(id);
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
    auto t = ChatTab(); if (t && !sel.empty() && CommentLayout()) Query(t, L"peek", W(AnnotationHost::PeekArg(sel, true)), nullptr);   // F9: the element on screen first (soft scroll)
    ShowConversation(id, [id, sel, label, t] { if (CommentLayout()) OpenBubble(id, nullptr, sel, false, label); if (t && !sel.empty()) Query(t, L"peek", W(AnnotationHost::PeekArg("", false)), nullptr); });
}

// ---------------------------------------------------------------- the frames + icons of the page (Paper 18IJ-0)
std::string MarkState(const std::string& id) {
    if (turn.active && convo.data.head.id == id) return "working";
    if (HasQueued(id)) return "waiting";
    return "idle";
}
void SyncMarks() {
    auto t = Ann::ModeTab(); if (!t || !Ann::s.active || !Ann::s.injected) return;
    SJ::Value list = SJ::Value::Arr(); std::set<std::string> found0;   // found0: on the page, drawn by the bubble's frame
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
            if (bubble.open && bubble.conv == h.id) { found0.insert(h.id); continue; }   // D1: its bubble is open: the frame + labels, not the icon
            if (h.anchorSelector.empty()) { found0.insert(h.id); continue; }   // Q 6: an element in a frame / web component: no mark drawn by selector
            SJ::Value o = SJ::Value::Obj(); o.Set("id", Str(h.id)); o.Set("selector", Str(h.anchorSelector)); o.Set("state", Str(MarkState(h.id)));
            if (MarkState(h.id) == "waiting") o.Set("waitLabel", Str(T8(I18n::Id::chat_mark_waiting)));
            list.Push(std::move(o));
        }
    }
    Query(t, L"cmarks", W(SJ::Serialize(list)), [found0](const std::string& json) {
        std::set<std::string> found = found0; SJ::Value v; if (SJ::Parse(json, v) && v.IsArray()) for (const auto& i : v.items) if (i.IsString()) found.insert(i.string);
        const bool changed = found != onPage; onPage = std::move(found);
        if (changed && Ann::uiReady && Ann::launcher.HistoryVisible()) Ann::LoadHistory(false, false);
        LauncherRefresh();   // D3: shown, and counting, only for the conversations whose element is gone
    });
}

// ---------------------------------------------------------------- the launcher in this layout (D3, brief rule 6)
// D3: the mode's page decides (not `site`, which the chat only sets at its first opening: before it, the mode's own
// launcher came back in Commentaire).
bool ModeSiteLocal() { auto t = Ann::ModeTab(); if (!t) return false; const auto c = ChatLocal::Classify(U8(t->uri)); return c.local && !c.file; }
std::vector<std::string> GoneIds() {
    std::vector<std::string> ids; const std::string origin = ModeOrigin(); if (!CommentLayout() || !ModeSiteLocal() || origin.empty()) return ids;
    if (anchoredOrigin != origin) return ids;   // not read yet: nothing is gone
    for (const auto& h : anchored) if (h.origin == origin && !onPage.count(h.id)) ids.push_back(h.id);
    return ids;
}
// -1: not in this layout (the launcher is the mode's own); else the conversations whose element is gone.
int CommentLauncherCount(bool workingOnly) {
    if (!CommentLayout() || !ModeSiteLocal()) return -1;
    int n = 0; for (const auto& id : GoneIds()) if (!workingOnly || MarkState(id) == "working") ++n;
    return n;
}
void LauncherRefresh() {
    if (!Ann::uiReady) return;
    Ann::LauncherSync();
    if (Ann::launcher.Visible()) Ann::ShowLauncher();
}

// ---------------------------------------------------------------- a click in the page, in this layout
// R18 (< 100 ms from the click): the page posts a light « selecting » (seq, selector, rect) before its full « select »,
// whose description takes longer. The bubble opens on it at once — on the element's conversation, or a new one — and the
// « select » that follows (same seq) only completes it (label, image, source). A « select » that never comes leaves a
// working bubble, anchored by the selector.
struct EarlyOpen { bool on = false; std::int64_t seq = -1; std::string conv; double at = 0; } early;
std::string LabelOfSelector(const std::string& s) {
    std::string last = s; if (const auto p = s.find_last_of(" >"); p != std::string::npos) last = s.substr(p + 1);
    if (const auto c = last.find(':'); c != std::string::npos) last = last.substr(0, c);
    return ChatPrompt::OneLine(last, 60);
}
bool OpenEarly(const std::shared_ptr<Tab>& t, const AnnotationHost::Envelope& env) {
    if (!t || !CommentFirst(t) || env.selector.empty() || bubble.open || Ann::NowMs() - bubble.closedAt < 400) return false;
    PrepareChat(t);
    const float k = Dpi() * float(Ann::s.zoom > 0 ? Ann::s.zoom : 1.0); const RECT pa = Ann::PageScreen(t);
    const RECT element{pa.left + LONG(std::lround(env.x * k)), pa.top + LONG(std::lround(env.y * k)), pa.left + LONG(std::lround((env.x + env.w) * k)), pa.top + LONG(std::lround((env.y + env.h) * k))};
    const std::string page = ChatBubble::PageKey(U8(t->uri)), label = LabelOfSelector(env.selector);
    early = EarlyOpen{true, env.serial, "", Ann::NowMs()};
    if (env.at > 0) { FILETIME ft; GetSystemTimeAsFileTime(&ft); ULARGE_INTEGER u{}; u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime;   // R18: the click (page clock) -> here
        const double now = double(u.QuadPart / 10000ULL) - 11644473600000.0; Diag(L"the click's « selecting » reached the host " + Ann::Ms(std::max(0.0, now - env.at)) + L" ms after the click"); }
    for (const auto& h : anchored) if (h.origin == site.origin && h.anchorPage == page && h.anchorSelector == env.selector) {   // its conversation
        const std::string id = h.id, l = h.anchorLabel.empty() ? label : h.anchorLabel; early.conv = id;
        ShowConversation(id, [id, element, l] { OpenBubble(id, &element, ShownSelector(id), false, l); });
        Diag(L"bubble opened on the click (selecting " + std::to_wstring(env.serial) + L"), the element's conversation");
        return true;
    }
    const std::string id = NewAnchoredConversation(env.selector, label, page, ""); early.conv = id;
    OpenBubble(id, &element, env.selector, false, label);
    Diag(L"bubble opened on the click (selecting " + std::to_wstring(env.serial) + L")");
    return true;
}
bool TakeCommentSelection(const std::shared_ptr<Tab>& t, std::uint64_t serial, bool captureImpossible) {
    PrepareChat(t);
    if (early.on && early.seq == Ann::s.sel.seq && bubble.open && bubble.conv == early.conv) {   // R18: the bubble is already open: completed
        early.on = false;
        {   // E1 (review R): the early opening followed the click's selector; the full selection says whether it is a safe one
            const auto& tq = Ann::s.pending.target;
            const std::string safe = Ann::s.sel.frameKind == "main" && tq.shadowPath.empty() && tq.selectorUnique ? tq.selector : std::string();
            if (safe != bubble.selector) {
                Diag(safe.empty() ? std::wstring(L"the click's selector is not a safe one (frame, shadow or not unique): the bubble follows the element itself") : L"the bubble follows the full selection's selector");   // i18n-exempt: log detail
                bubble.selector = safe; FollowBubbleElement(safe.empty());
            }
        }
        const auto& tg = Ann::s.pending.target; const std::string label = tg.path.empty() ? tg.tag : tg.path.back();
        if (!label.empty() && label != bubble.label) {
            bubble.label = label;
            for (auto* h : {&convo.data.head, aside.on ? &aside.c.data.head : nullptr}) if (h && h->id == early.conv) { h->anchorLabel = label; if (!SentConvo(h == &convo.data.head ? convo : aside.c)) h->title = ChatPrompt::OneLine(label, 44); }
            if (panelReady) SendLayout(false);
        }
        if (anchorElements.find(early.conv) == anchorElements.end()) { Element e = ElementFromMode(serial); e.captureDone = captureImpossible; e.anchorFor = early.conv; aiming[serial] = std::move(e); QuerySource(t, serial); }
        Diag(L"the click's full selection completes the bubble (" + Ann::Ms(Ann::NowMs() - early.at) + L" ms after it opened)");
        return true;
    }
    early.on = false;
    const auto& tgt = Ann::s.pending.target;
    const std::string sel = Ann::s.sel.frameKind == "main" && tgt.shadowPath.empty() && tgt.selectorUnique ? tgt.selector : std::string();
    const std::string page = ChatBubble::PageKey(U8(t->uri));
    const std::string label = tgt.path.empty() ? tgt.tag : tgt.path.back();
    const RECT element = ToScreen(Ann::s.sel.target.rect.x, Ann::s.sel.target.rect.y, Ann::s.sel.target.rect.width, Ann::s.sel.target.rect.height, Ann::s.sel.target.viewport.width);
    const ChatStore::Header* existing = nullptr;
    if (!sel.empty()) for (const auto& h : anchored) if (h.origin == site.origin && h.anchorPage == page && h.anchorSelector == sel) { existing = &h; break; }
    // Q 6 (E1): no safe selector (iframe, web component): its conversation is found again by its key
    const std::string key = sel.empty() ? ChatBubble::UnsafeAnchorKey(Ann::s.sel.frameKind, tgt.selector, tgt.shadowPath) : std::string();
    if (!existing && !key.empty()) for (const auto& h : anchored) if (h.origin == site.origin && h.anchorPage == page && h.anchorKey == key) { existing = &h; break; }
    for (const Convo* mem : {&convo, aside.on ? &aside.c : nullptr}) if (!existing && mem && !key.empty() && mem->data.head.anchorKey == key && mem->data.head.anchorPage == page && SentConvo(*mem)) existing = &mem->data.head;
    if (existing) {   // its conversation: the bubble opens on it again
        const std::string id = existing->id, s = existing->anchorSelector, l = existing->anchorLabel;
        if (bubble.open && bubble.conv == id) { BubbleBack(L"a click on its element"); EnsureOnScreen(L"a click on its element"); return true; }   // already open: shown (hidden by another application meanwhile)   // i18n-exempt: log detail
        if (anchorElements.find(id) == anchorElements.end()) { Element e = ElementFromMode(serial); e.captureDone = captureImpossible; e.anchorFor = id; aiming[serial] = std::move(e); QuerySource(t, serial); }
        const std::string page2 = page;
        ShowConversation(id, [id, element, s, l] { OpenBubble(id, &element, s, s.empty(), l); },   // Q 6: no selector — the element itself is followed
                         [id, element, s, l, page2, serial] {   // R6: unreadable — a new conversation for the element, in its bubble
                             const std::string fresh = NewAnchoredConversation(s, l, page2, "");
                             for (auto& a : aiming) if (a.first == serial && a.second.anchorFor == id) a.second.anchorFor = fresh;
                             if (auto it = anchorElements.find(id); it != anchorElements.end()) { anchorElements[fresh] = it->second; anchorElements.erase(it); }
                             OpenBubble(fresh, &element, s, s.empty(), l);
                         });
        return true;
    }
    // Q pass 1, defect 1 (the user's journey): a click on another element while a bubble is open opens THAT element's bubble
    // (before: it only closed the open one, 3 times out of 4 nothing opened). Only a click right after a close does nothing.
    if (!bubble.open && Ann::NowMs() - bubble.closedAt < 400) { Diag(L"a click right after the bubble closed: nothing opened"); return true; }   // i18n-exempt: log detail
    if (aiming.size() >= 10) return false;   // R35: the panel's chips are not this bubble's
    if (bubble.open) { Diag(L"a click on another element: its bubble opens"); ForgetUnsent(bubble.conv); }   // i18n-exempt: log detail
    const std::string id = NewAnchoredConversation(sel, label, page, "", key);
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
    if (held.on) ReleaseHeld();   // M14: a second send — the first one leaves now (it was erased)
    held = HeldSend{true, annotate, bubble.conv, text, els, atts, Ann::NowMs()};
    if (post) SetTimer(post, HeldTimer, 2000, nullptr);   // M14: never forever — 2 s, then without the element's image
    Diag(L"message held until the bubble's element is ready");
    return true;
}
void ReleaseHeld() {
    if (post) KillTimer(post, HeldTimer);
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
    if (!comment) ForgetUnsent(convo.data.head.id);   // M6: the panel never shows an element's unsent conversation
    if (comment != was) { projects.layout = comment ? "comment" : "panel"; SaveProjects(); Diag(comment ? L"layout: Commentaire (a bubble on the element)" : L"layout: Panneau (the chat at the right)"); }
    if (comment) {
        if (Ann::s.paused) Ann::SetSelecting(true);   // the switch of V1.4 is a panel thing: the selection is how comments start
        if (Open()) ClosePanel();
        Ann::Announce(TR(chat_layout_comment_on));
    } else {
        CloseBubble(false); CoolBubble();   // R34: the bubble is gone (open or warm) before the panel takes the page
        if (!Open()) OpenPanel(); else if (panelReady) { SendLayout(false); SendThread(); }
    }
    SyncChromeBits(); SyncMarks();
    if (comment) WarmBubble();   // R18 (after the panel's slide: FinishClose warms it if the panel is still on screen)
    LauncherRefresh();   // D3: in Commentaire, only for the conversations whose element is gone
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
    ShowConversation(conv, [conv, label] { if (CommentLayout()) { OpenBubble(conv, nullptr, "", false, label); bubble.noFollow = true; } else OpenPanel(); });   // C3: no element to follow (it is gone)
    return true;
}
} // namespace Chat
