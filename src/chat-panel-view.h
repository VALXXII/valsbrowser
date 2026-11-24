#pragma once
// Chat intégré — the panel's WebView2 (design/chat-integration.md, « Panneau »). Included by src/chat-host.h
// (main.cpp's translation unit: uses its Callback<>, Com<>, Log, window, environment, S()).
//
//   created   the FIRST time the panel is opened, never before: CreateCoreWebView2Controller on the tabs' own
//             environment (no new browser process, one renderer more while it exists), windowed, child of the
//             browser window, at the right of the page area;
//   content   only our own files: https://chat.browser.internal/chat-panel/* is answered from <app>\assets\chat-panel
//             and /fonts/* from <app>\assets\fonts by WebResourceRequested; EVERY other request gets 403, every
//             navigation elsewhere and every new window is cancelled; CSP doubled as a response header;
//   pont      window.chrome.webview.postMessage <-> PostWebMessageAsJson (contract B, design/chat-panel.md);
//   moved     put_Bounds only (the slide of the entrance / exit never resizes it: no layout per frame);
//   destroyed Close() on the controller: its renderer goes with it.
//   visual    R1 / R3 (Mode Commentaire v2): the controller is a COMPOSITION controller (visual hosting): the page is a
//             DirectComposition visual with a transparent background, so the bubble's window shows the page's own
//             antialiased 16 px corners over any site (no DWM corner, no window region), and grows transparent around
//             the surface while a menu or a tooltip goes out of it. The host forwards the mouse (SendMouseInput), the
//             cursor (CursorChanged) and UI Automation (WM_GETOBJECT); the keyboard is WebView2's own once focused.
//             Placements: the bubble's popup is its own composition target (WS_EX_NOREDIRECTIONBITMAP: what the page
//             does not paint is see-through); the panel gets a child window of the browser window, painted #171717
//             under the page. Without dcomp.dll or ICoreWebView2Environment3: windowed hosting, as before.
#include <shlwapi.h>
#include <dcomp.h>
#include <uiautomation.h>
#include <windowsx.h>
#include <vector>
#include <functional>
#include <string>

namespace ChatPanelView {

inline constexpr const wchar_t* Origin = L"https://chat.browser.internal";
inline constexpr const wchar_t* PageUrl = L"https://chat.browser.internal/chat-panel/index.html";
inline constexpr const wchar_t* Csp = L"default-src 'none'; script-src 'self'; style-src 'self'; font-src 'self'; img-src data:; connect-src 'none'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'";
inline constexpr std::size_t MaxAssetBytes = 4u << 20;

struct Hooks {
    std::function<void(const std::string& json)> message;   // one panel -> host message (UTF-8 JSON, already size-checked)
    std::function<void(bool ok)> created;                   // the controller exists (or could not be created)
    std::function<void(int vk)> accelerator;                // Ctrl+… pressed in the panel: the browser's own shortcuts
    std::function<void()> crashed;                          // the panel's renderer died
};

// What a path of our origin maps to on disk; empty = refused. Pure (tests).
// "/chat-panel/panel.js" -> <assets>\chat-panel\panel.js ; "/fonts/X.ttf" -> <assets>\fonts\X.ttf. One folder level,
// file names [A-Za-z0-9._-] only, no dev host files (dev.html, dev-host.js: QA only, never served).
inline std::wstring AssetFor(const std::wstring& assetsDir, const std::wstring& urlPath) {
    std::wstring p = urlPath; if (const auto q = p.find_first_of(L"?#"); q != std::wstring::npos) p = p.substr(0, q);
    const wchar_t* roots[] = {L"/chat-panel/", L"/fonts/"}; const wchar_t* dirs[] = {L"\\chat-panel\\", L"\\fonts\\"};
    for (int i = 0; i < 2; ++i) {
        const std::wstring root = roots[i]; if (p.rfind(root, 0) != 0) continue;
        const std::wstring name = p.substr(root.size());
        if (name.empty() || name.size() > 80 || name[0] == L'.') return L"";
        for (wchar_t c : name) if (!((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || c == L'.' || c == L'_' || c == L'-')) return L"";
        if (name.find(L"..") != std::wstring::npos || name == L"dev.html" || name == L"dev-host.js") return L"";
        return assetsDir + dirs[i] + name;
    }
    return L"";
}
inline const wchar_t* MimeFor(const std::wstring& file) {
    const auto dot = file.find_last_of(L'.'); const std::wstring ext = dot == std::wstring::npos ? L"" : file.substr(dot);
    if (ext == L".html") return L"text/html; charset=utf-8";
    if (ext == L".css") return L"text/css; charset=utf-8";
    if (ext == L".js") return L"text/javascript; charset=utf-8";
    if (ext == L".ttf") return L"font/ttf";
    if (ext == L".woff2") return L"font/woff2";
    if (ext == L".svg") return L"image/svg+xml";
    if (ext == L".png") return L"image/png";
    if (ext == L".json") return L"application/json; charset=utf-8";
    return L"";
}

// The colour behind the page: transparent when the page is a visual, or sits in the bubble's see-through window (the
// page draws its own 16 px surface, R1); else the canvas #171717 (the panel: no white flash while it loads).
inline COREWEBVIEW2_COLOR Background(bool visual, bool inBubble) { return ChatBubble::TransparentBackground(visual, inBubble) ? COREWEBVIEW2_COLOR{0, 0, 0, 0} : COREWEBVIEW2_COLOR{255, 0x17, 0x17, 0x17}; }

class View {
    Com<ICoreWebView2Controller> controller_; Com<ICoreWebView2> view_; Com<ICoreWebView2Environment> env_;
    Hooks hooks_; std::wstring assets_; bool creating_ = false, visible_ = false; RECT bounds_{}; unsigned generation_ = 0;
    HWND parent_ = nullptr, created_ = nullptr;   // where it should be / where the controller is
    bool devtools_ = false;
    // ---- visual hosting
    Com<ICoreWebView2CompositionController> comp_; bool visual_ = false;
    HMODULE dcompModule_ = nullptr; IDCompositionDevice* dc_ = nullptr; IDCompositionVisual* webVisual_ = nullptr;
    struct Target { HWND hwnd = nullptr; IDCompositionTarget* target = nullptr; IDCompositionVisual* root = nullptr; };
    std::vector<Target> targets_; IDCompositionVisual* attachedTo_ = nullptr;
    HWND panelHost_ = nullptr, direct_ = nullptr, host_ = nullptr;   // the panel's child host / the popup that hosts itself / current
    RECT hostBounds_{}; bool tracking_ = false; HCURSOR cursor_ = nullptr;
    static View*& Self() { static View* self = nullptr; return self; }
    // Windowed hosting is the default: screen readers read the chat page natively (visual hosting gave UI Automation an
    // empty page and cut the main window's tree, measured 19 Nov 2026). BROWSER_CHAT_VISUAL=1 brings visual hosting back.
    static bool VisualWanted() { wchar_t v[8] = L""; GetEnvironmentVariableW(L"BROWSER_CHAT_VISUAL", v, 8); return ChatBubble::VisualHosting(v); }
    void ApplyBackground() {
        if (!controller_.p) return;
        Com<ICoreWebView2Controller2> c2;
        if (SUCCEEDED(controller_->QueryInterface(IID_ICoreWebView2Controller2, (void**)&c2.p)) && c2.p) c2->put_DefaultBackgroundColor(Background(visual_, parent_ == direct_ && direct_));
    }
    bool EnsureDComp() {
        if (dc_) return true;
        if (!dcompModule_) dcompModule_ = LoadLibraryExW(L"dcomp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        using Create = HRESULT(WINAPI*)(IDXGIDevice*, REFIID, void**);
        auto create = dcompModule_ ? reinterpret_cast<Create>(GetProcAddress(dcompModule_, "DCompositionCreateDevice")) : nullptr;
        if (!create || FAILED(create(nullptr, __uuidof(IDCompositionDevice), reinterpret_cast<void**>(&dc_))) || !dc_) { dc_ = nullptr; return false; }
        if (FAILED(dc_->CreateVisual(&webVisual_)) || !webVisual_) { dc_->Release(); dc_ = nullptr; return false; }
        return true;
    }
    IDCompositionVisual* RootOf(HWND h) {
        for (auto& t : targets_) if (t.hwnd == h) return t.root;
        Target t; t.hwnd = h;
        if (!dc_ || FAILED(dc_->CreateTargetForHwnd(h, TRUE, &t.target)) || FAILED(dc_->CreateVisual(&t.root)) || FAILED(t.target->SetRoot(t.root))) {
            if (t.root) t.root->Release();
            if (t.target) t.target->Release();
            return nullptr;
        }
        targets_.push_back(t); return t.root;
    }
    static LRESULT CALLBACK PanelHostProc(HWND h, UINT m, WPARAM w, LPARAM l) {
        LRESULT r = 0; if (Self() && Self()->HostMessage(h, m, w, l, r)) return r;
        if (m == WM_ERASEBKGND) { RECT c; GetClientRect(h, &c); HBRUSH b = CreateSolidBrush(RGB(0x17, 0x17, 0x17)); FillRect(reinterpret_cast<HDC>(w), &c, b); DeleteObject(b); return 1; }
        return DefWindowProcW(h, m, w, l);
    }
    HWND PanelHost(HWND parent) {
        if (panelHost_ && GetParent(panelHost_) == parent) return panelHost_;
        if (panelHost_) { DestroyWindow(panelHost_); panelHost_ = nullptr; }
        WNDCLASSEXW wc{sizeof(wc)}; wc.lpfnWndProc = PanelHostProc; wc.hInstance = GetModuleHandleW(nullptr); wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.lpszClassName = L"BrowserChatPanelHost"; RegisterClassExW(&wc);
        panelHost_ = CreateWindowExW(0, wc.lpszClassName, L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 1, 1, parent, nullptr, wc.hInstance, nullptr);
        return panelHost_;
    }
    // #127 side panel layouts: the panel host window rounded (`radius` px) and `border` px inside its bounds, the chrome
    // painting the border ring around it (Chat::SyncCardFrame). 0, 0: square, edge to edge (the top bar).
    int cornerRadius_ = 0, cornerBorder_ = 0; SIZE regionFor_{};
    void Corner(int radius, int border) { if (radius == cornerRadius_ && border == cornerBorder_) return; cornerRadius_ = radius; cornerBorder_ = border; regionFor_ = {}; if (panelHost_) ShapeHost(); }
    void ShapeHost() {
        if (!panelHost_) return; RECT r{}; GetClientRect(panelHost_, &r); const SIZE sz{r.right, r.bottom};
        if (sz.cx == regionFor_.cx && sz.cy == regionFor_.cy) return; regionFor_ = sz;
        if (cornerRadius_ <= 0) { SetWindowRgn(panelHost_, nullptr, TRUE); return; }
        const int b = cornerBorder_, k = 2 * std::max(1, cornerRadius_ - b);
        SetWindowRgn(panelHost_, CreateRoundRectRgn(b, b, sz.cx - b + 1, sz.cy - b + 1, k, k), TRUE);
    }
    // Where the controller sits: its host window, and the WebView's bounds in that window's client pixels.
    void Seat(HWND parent, RECT bounds) {
        HWND host = parent; RECT inHost = bounds;
        if (parent != direct_) {   // the panel: a child window at `bounds`, the WebView filling it
            host = PanelHost(parent); if (!host) return;
            SetWindowPos(host, HWND_TOP, bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top, SWP_NOACTIVATE | (visible_ ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
            inHost = {0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top};
            ShapeHost();
        } else if (panelHost_) ShowWindow(panelHost_, SW_HIDE);
        hostBounds_ = inHost;
        if (host != host_) {
            if (IDCompositionVisual* root = RootOf(host)) {
                if (attachedTo_) attachedTo_->RemoveVisual(webVisual_);
                root->AddVisual(webVisual_, FALSE, nullptr); attachedTo_ = root;
            }
            if (controller_.p) controller_->put_ParentWindow(host);
            host_ = host; created_ = parent;
        }
        if (controller_.p) controller_->put_Bounds(inHost);
        if (dc_) dc_->Commit();
    }
    static bool ReadAsset(const std::wstring& file, std::string& out) {
        HANDLE f = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER size{}; bool ok = GetFileSizeEx(f, &size) && size.QuadPart >= 0 && std::size_t(size.QuadPart) <= MaxAssetBytes;
        if (ok) { out.resize(size_t(size.QuadPart)); DWORD got = 0; ok = out.empty() || (ReadFile(f, out.data(), DWORD(out.size()), &got, nullptr) && got == out.size()); }
        CloseHandle(f); return ok;
    }
    void Respond(ICoreWebView2WebResourceRequestedEventArgs* a, int status, const wchar_t* reason, const std::string& body, const wchar_t* mime) {
        IStream* stream = body.empty() ? nullptr : SHCreateMemStream(reinterpret_cast<const BYTE*>(body.data()), UINT(body.size()));
        std::wstring headers = L"Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nContent-Security-Policy: " + std::wstring(Csp);   // i18n-exempt: HTTP response headers
        if (mime && *mime) headers += L"\r\nContent-Type: " + std::wstring(mime);
        Com<ICoreWebView2WebResourceResponse> response;
        if (env_.p && SUCCEEDED(env_->CreateWebResourceResponse(stream, status, reason, headers.c_str(), &response.p)) && response.p) a->put_Response(response.p);
        if (stream) stream->Release();
    }
    void Wire() {
        EventRegistrationToken token{};
        Com<ICoreWebView2Settings> st;
        if (SUCCEEDED(view_->get_Settings(&st.p)) && st.p) {
            st->put_AreDevToolsEnabled(devtools_ ? TRUE : FALSE); st->put_AreDefaultContextMenusEnabled(devtools_ ? TRUE : FALSE); st->put_IsStatusBarEnabled(FALSE);
            st->put_IsZoomControlEnabled(FALSE); st->put_AreDefaultScriptDialogsEnabled(FALSE); st->put_IsBuiltInErrorPageEnabled(FALSE);
            st->put_IsWebMessageEnabled(TRUE); st->put_AreHostObjectsAllowed(FALSE);
        }
        {   // the canvas colour behind the page: no white flash while it loads (design/chat-panel.md §1)
            Com<ICoreWebView2Controller2> c2;
            if (SUCCEEDED(controller_->QueryInterface(IID_ICoreWebView2Controller2, (void**)&c2.p)) && c2.p) c2->put_DefaultBackgroundColor(Background(visual_, parent_ == direct_ && direct_));
        }
        view_->AddWebResourceRequestedFilter(L"*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
        const unsigned gen = generation_;
        auto resource = Callback<ICoreWebView2WebResourceRequestedEventHandler, ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs*>([this, gen](ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* a) -> HRESULT {
            if (gen != generation_) return S_OK;
            Com<ICoreWebView2WebResourceRequest> request; LPWSTR raw = nullptr, method = nullptr;
            if (FAILED(a->get_Request(&request.p)) || !request.p || FAILED(request->get_Uri(&raw)) || !raw) { CoTaskMemFree(raw); Respond(a, 403, L"Forbidden", "", L""); return S_OK; }   // i18n-exempt: HTTP reason phrase
            const std::wstring uri = raw; CoTaskMemFree(raw);
            request->get_Method(&method); const std::wstring verb = method ? method : L""; CoTaskMemFree(method);
            const std::wstring origin = Origin; std::wstring file;
            if (verb == L"GET" && uri.rfind(origin + L"/", 0) == 0) file = AssetFor(assets_, uri.substr(origin.size()));
            std::string body; const wchar_t* mime = file.empty() ? L"" : MimeFor(file);
            if (file.empty() || !*mime || !ReadAsset(file, body)) { Log(L"CHAT panel request refused: " + uri.substr(0, 200)); Respond(a, file.empty() ? 403 : 404, file.empty() ? L"Forbidden" : L"Not Found", "", L""); return S_OK; }   // i18n-exempt: HTTP reason phrases
            Respond(a, 200, L"OK", body, mime);
            return S_OK; });
        view_->add_WebResourceRequested(resource, &token); resource->Release();
        auto starting = Callback<ICoreWebView2NavigationStartingEventHandler, ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs*>([](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* a) -> HRESULT {
            LPWSTR raw = nullptr; a->get_Uri(&raw); const std::wstring uri = raw ? raw : L""; CoTaskMemFree(raw);
            if (uri != PageUrl) { a->put_Cancel(TRUE); Log(L"CHAT panel navigation refused: " + uri.substr(0, 200)); }
            return S_OK; });
        view_->add_NavigationStarting(starting, &token); starting->Release();
        auto popup = Callback<ICoreWebView2NewWindowRequestedEventHandler, ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs*>([](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* a) -> HRESULT { a->put_Handled(TRUE); return S_OK; });
        view_->add_NewWindowRequested(popup, &token); popup->Release();
        auto permission = Callback<ICoreWebView2PermissionRequestedEventHandler, ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs*>([](ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs* a) -> HRESULT { a->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY); return S_OK; });
        view_->add_PermissionRequested(permission, &token); permission->Release();
        auto message = Callback<ICoreWebView2WebMessageReceivedEventHandler, ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs*>([this, gen](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* a) -> HRESULT {
            if (gen != generation_) return S_OK;
            LPWSTR src = nullptr, raw = nullptr; a->get_Source(&src); const std::wstring source = src ? src : L""; CoTaskMemFree(src);
            if (source != PageUrl) { Log(L"CHAT panel message from another document ignored"); return S_OK; }
            if (FAILED(a->get_WebMessageAsJson(&raw)) || !raw) { CoTaskMemFree(raw); return S_OK; }
            const size_t chars = wcslen(raw);
            if (chars > (13u << 20)) { CoTaskMemFree(raw); Log(L"CHAT panel message too large: ignored"); return S_OK; }
            std::string json = Utf8(std::wstring(raw, chars)); CoTaskMemFree(raw);
            if (hooks_.message) hooks_.message(json);
            return S_OK; });
        view_->add_WebMessageReceived(message, &token); message->Release();
        auto key = Callback<ICoreWebView2AcceleratorKeyPressedEventHandler, ICoreWebView2Controller*, ICoreWebView2AcceleratorKeyPressedEventArgs*>([this](ICoreWebView2Controller*, ICoreWebView2AcceleratorKeyPressedEventArgs* a) -> HRESULT {
            COREWEBVIEW2_KEY_EVENT_KIND kind; a->get_KeyEventKind(&kind); UINT vk = 0; a->get_VirtualKey(&vk);
            if (kind != COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN && kind != COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN) return S_OK;
            // Only Ctrl combinations leave the panel (browser shortcuts); Échap, Tab, Entrée, arrows and Ctrl+/ are its own.
            if (GetKeyState(VK_CONTROL) >= 0 || vk == VK_OEM_2 || vk == VK_CONTROL || vk == VK_SHIFT || vk == VK_MENU) return S_OK;
            if (hooks_.accelerator) { const int before = handled_; hooks_.accelerator(int(vk)); if (handled_ != before) a->put_Handled(TRUE); }
            return S_OK; });
        controller_->add_AcceleratorKeyPressed(key, &token); key->Release();
        auto failed = Callback<ICoreWebView2ProcessFailedEventHandler, ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs*>([this, gen](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs*) -> HRESULT {
            if (gen == generation_ && hooks_.crashed) hooks_.crashed(); return S_OK; });
        view_->add_ProcessFailed(failed, &token); failed->Release();
    }
    int handled_ = 0;
public:
    void Init(Hooks hooks, std::wstring assetsDir, bool devtools) { hooks_ = std::move(hooks); assets_ = std::move(assetsDir); devtools_ = devtools; }
    void AcceleratorHandled() { ++handled_; }   // called by hooks.accelerator when it took the key
    bool Exists() const { return controller_.p != nullptr; }
    bool Creating() const { return creating_; }
    bool Usable(ICoreWebView2Environment* current) const { return controller_.p && env_.p == current; }
    // Creates the controller (asynchronous). `bounds`: where it will sit, in the browser window's client pixels.
    bool Create(ICoreWebView2Environment* environment, HWND parent, RECT bounds) {
        if (controller_.p || creating_ || !environment) return controller_.p != nullptr;
        creating_ = true; bounds_ = bounds; parent_ = created_ = parent; env_.set(environment); const unsigned gen = ++generation_;
        auto done = Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT, ICoreWebView2Controller*>([this, gen](HRESULT hr, ICoreWebView2Controller* c) -> HRESULT {
            if (gen != generation_) { if (c) c->Close(); return S_OK; }   // destroyed while it was being created
            creating_ = false;
            if (FAILED(hr) || !c || FAILED(c->get_CoreWebView2(&view_.p)) || !view_.p) { Log(L"CHAT panel WebView2 unavailable"); env_.set(nullptr); if (hooks_.created) hooks_.created(false); return S_OK; }
            controller_.set(c); Wire();
            if (parent_ != created_) { controller_->put_ParentWindow(parent_); created_ = parent_; ApplyBackground(); }   // moved while it was being created
            controller_->put_Bounds(bounds_); controller_->put_IsVisible(visible_ ? TRUE : FALSE);
            view_->Navigate(PageUrl);
            if (hooks_.created) hooks_.created(true);
            return S_OK; });
        Com<ICoreWebView2Environment3> env3;
        if (VisualWanted() && EnsureDComp() && SUCCEEDED(environment->QueryInterface(IID_ICoreWebView2Environment3, reinterpret_cast<void**>(&env3.p))) && env3.p) {
            done->Release(); visual_ = true; Self() = this;
            HWND host = parent == direct_ ? parent : PanelHost(parent);
            auto made = Callback<ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandler, HRESULT, ICoreWebView2CompositionController*>([this, gen](HRESULT hr, ICoreWebView2CompositionController* cc) -> HRESULT {
                if (gen != generation_) { Com<ICoreWebView2Controller> c; if (cc && SUCCEEDED(cc->QueryInterface(IID_ICoreWebView2Controller, reinterpret_cast<void**>(&c.p))) && c.p) c->Close(); return S_OK; }
                creating_ = false;
                Com<ICoreWebView2Controller> c;
                if (FAILED(hr) || !cc || FAILED(cc->QueryInterface(IID_ICoreWebView2Controller, reinterpret_cast<void**>(&c.p))) || !c.p || FAILED(c->get_CoreWebView2(&view_.p)) || !view_.p) { Log(L"CHAT panel WebView2 unavailable (visual hosting)"); env_.set(nullptr); if (hooks_.created) hooks_.created(false); return S_OK; }
                comp_.set(cc); controller_.set(c.p); Wire();
                comp_->put_RootVisualTarget(webVisual_);
                {   EventRegistrationToken token{};
                    auto cursor = Callback<ICoreWebView2CursorChangedEventHandler, ICoreWebView2CompositionController*, IUnknown*>([this](ICoreWebView2CompositionController* s, IUnknown*) -> HRESULT {
                        HCURSOR h = nullptr; if (SUCCEEDED(s->get_Cursor(&h))) { cursor_ = h; POINT p; GetCursorPos(&p); RECT r{}; if (host_ && GetWindowRect(host_, &r) && PtInRect(&r, p)) SetCursor(h); }
                        return S_OK; });
                    comp_->add_CursorChanged(cursor, &token); cursor->Release(); }
                host_ = nullptr; const RECT b = bounds_; Seat(parent_, b);
                controller_->put_IsVisible(visible_ ? TRUE : FALSE);
                view_->Navigate(PageUrl);
                Log(L"CHAT panel WebView2: visual hosting (transparent)");
                if (hooks_.created) hooks_.created(true);
                return S_OK; });
            const HRESULT hr = host ? env3->CreateCoreWebView2CompositionController(host, made) : E_FAIL; made->Release();
            if (FAILED(hr)) { creating_ = false; env_.set(nullptr); ++generation_; visual_ = false; return false; }
            return true;
        }
        const HRESULT hr = environment->CreateCoreWebView2Controller(parent, done); done->Release();
        if (FAILED(hr)) { creating_ = false; env_.set(nullptr); ++generation_; return false; }
        return true;
    }
    void Place(RECT bounds) {
        bounds_ = bounds;
        if (visual_) { if (controller_.p) Seat(parent_, bounds); return; }
        if (controller_.p) controller_->put_Bounds(bounds);
    }
    // Mode Commentaire: the same WebView (same page, same conversation) goes into another window — the browser window
    // for the panel, the bubble's own window. `bounds` in that window's client pixels.
    void Reparent(HWND parent, RECT bounds) {
        parent_ = parent; bounds_ = bounds;
        if (visual_) { if (controller_.p) Seat(parent, bounds); return; }
        if (controller_.p && created_ != parent) { controller_->put_ParentWindow(parent); created_ = parent; ApplyBackground(); }
        if (controller_.p) controller_->put_Bounds(bounds);
    }
    HWND Parent() const { return parent_; }
    void Show(bool on) {
        visible_ = on; if (controller_.p) controller_->put_IsVisible(on ? TRUE : FALSE);
        if (visual_ && panelHost_ && host_ == panelHost_) ShowWindow(panelHost_, on ? SW_SHOWNA : SW_HIDE);
    }
    // Mode Commentaire: this popup is its own composition target (the bubble), not a parent for the panel's child host.
    void HostDirectly(HWND popup) { direct_ = popup; }
    bool Visual() const { return visual_; }   // transparent page possible (R1 / R3); false = windowed hosting
    // Visual hosting: the host windows' input. Returns true when the message was taken (`result` set).
    bool HostMessage(HWND h, UINT m, WPARAM w, LPARAM l, LRESULT& result) {
        if (!visual_ || !comp_.p || h != host_) return false;
        if (m == WM_SETCURSOR && LOWORD(l) == HTCLIENT) { HCURSOR c = cursor_; if (!c) comp_->get_Cursor(&c); if (c) { SetCursor(c); result = TRUE; return true; } return false; }
        if (m == WM_GETOBJECT && static_cast<long>(l) == static_cast<long>(UiaRootObjectId)) {
            Com<ICoreWebView2CompositionController2> c2; Com<IUnknown> provider; Com<IRawElementProviderSimple> simple;
            static auto fn = reinterpret_cast<decltype(&::UiaReturnRawElementProvider)>(reinterpret_cast<void*>(GetProcAddress(LoadLibraryExW(L"uiautomationcore.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32), "UiaReturnRawElementProvider")));
            const HRESULT q = comp_->QueryInterface(IID_ICoreWebView2CompositionController2, reinterpret_cast<void**>(&c2.p));
            const HRESULT a = SUCCEEDED(q) && c2.p ? c2->get_AutomationProvider(&provider.p) : E_NOINTERFACE;
            const HRESULT s = SUCCEEDED(a) && provider.p ? provider->QueryInterface(__uuidof(IRawElementProviderSimple), reinterpret_cast<void**>(&simple.p)) : E_NOINTERFACE;
            static int said = 0;
            if (said < 3) {
                ++said; HRESULT nav = E_NOINTERFACE, host = E_NOINTERFACE; int child = -1, hasHost = -1; ProviderOptions opt{};
                if (simple.p) {
                    simple->get_ProviderOptions(&opt);
                    Com<IRawElementProviderSimple> hp; host = simple->get_HostRawElementProvider(&hp.p); hasHost = hp.p ? 1 : 0;
                    Com<IRawElementProviderFragment> fr; if (SUCCEEDED(simple->QueryInterface(__uuidof(IRawElementProviderFragment), reinterpret_cast<void**>(&fr.p))) && fr.p) { Com<IRawElementProviderFragment> c; nav = fr->Navigate(NavigateDirection_FirstChild, &c.p); child = c.p ? 1 : 0; }
                }
                wchar_t b[260]; swprintf(b, 260, L"CHAT UI Automation of the chat page: controller2 %08lX, provider %08lX, simple %08lX, options %d, host %08lX/%d, first child %08lX/%d", static_cast<unsigned long>(q), static_cast<unsigned long>(a), static_cast<unsigned long>(s), int(opt), static_cast<unsigned long>(host), hasHost, static_cast<unsigned long>(nav), child); Log(b);
            }
            if (fn && simple.p) { result = fn(h, w, l, simple.p); return true; }
            return false;
        }
        if (m == WM_MOUSELEAVE) { tracking_ = false; comp_->SendMouseInput(COREWEBVIEW2_MOUSE_EVENT_KIND_LEAVE, COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_NONE, 0, POINT{0, 0}); result = 0; return true; }
        if (!(m >= WM_MOUSEFIRST && m <= WM_MOUSELAST)) return false;
        const bool wheel = m == WM_MOUSEWHEEL || m == WM_MOUSEHWHEEL;
        POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)}; if (wheel) ScreenToClient(h, &p);
        if (!PtInRect(&hostBounds_, p) && GetCapture() != h) return false;
        if (m == WM_LBUTTONDOWN || m == WM_RBUTTONDOWN || m == WM_MBUTTONDOWN || m == WM_XBUTTONDOWN) {
            SetCapture(h);
            const HWND f = GetFocus(); if (!f || (f != h && !IsChild(h, f))) Focus();   // the keyboard follows a click in the page
        }
        if ((m == WM_LBUTTONUP || m == WM_RBUTTONUP || m == WM_MBUTTONUP || m == WM_XBUTTONUP) && GetCapture() == h) ReleaseCapture();
        UINT32 data = 0;
        if (wheel) data = UINT32(GET_WHEEL_DELTA_WPARAM(w)); else if (m == WM_XBUTTONDOWN || m == WM_XBUTTONUP || m == WM_XBUTTONDBLCLK) data = GET_XBUTTON_WPARAM(w);
        p.x -= hostBounds_.left; p.y -= hostBounds_.top;
        comp_->SendMouseInput(static_cast<COREWEBVIEW2_MOUSE_EVENT_KIND>(m), static_cast<COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS>(GET_KEYSTATE_WPARAM(w)), data, p);
        if (m == WM_MOUSEMOVE && !tracking_) { TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, h, 0}; tracking_ = TrackMouseEvent(&t) != FALSE; }
        result = 0; return true;
    }
    bool Visible() const { return visible_; }
    void Focus() { if (controller_.p && visible_) controller_->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC); }
    bool Post(const std::string& json) { return view_.p && SUCCEEDED(view_->PostWebMessageAsJson(AnnotationHost::Wide(json).c_str())); }
    void OpenDevTools() { if (view_.p && devtools_) view_->OpenDevToolsWindow(); }
    ICoreWebView2* Raw() const { return view_.p; }
    // The WebView and its renderer go away; a creation under way is abandoned.
    void Destroy() {
        ++generation_; creating_ = false; visible_ = false;
        if (controller_.p) controller_->Close();
        view_.set(nullptr); controller_.set(nullptr); comp_.set(nullptr); env_.set(nullptr);
        if (attachedTo_ && webVisual_) { attachedTo_->RemoveVisual(webVisual_); attachedTo_ = nullptr; if (dc_) dc_->Commit(); }
        host_ = nullptr; created_ = nullptr; visual_ = false; tracking_ = false; cursor_ = nullptr;
        if (panelHost_) ShowWindow(panelHost_, SW_HIDE);
    }
};

} // namespace ChatPanelView
