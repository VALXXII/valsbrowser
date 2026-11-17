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
#include <shlwapi.h>
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

class View {
    Com<ICoreWebView2Controller> controller_; Com<ICoreWebView2> view_; Com<ICoreWebView2Environment> env_;
    Hooks hooks_; std::wstring assets_; bool creating_ = false, visible_ = false; RECT bounds_{}; unsigned generation_ = 0;
    bool devtools_ = false;
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
            if (SUCCEEDED(controller_->QueryInterface(IID_ICoreWebView2Controller2, (void**)&c2.p)) && c2.p) c2->put_DefaultBackgroundColor(COREWEBVIEW2_COLOR{255, 0x17, 0x17, 0x17});
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
        creating_ = true; bounds_ = bounds; env_.set(environment); const unsigned gen = ++generation_;
        auto done = Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT, ICoreWebView2Controller*>([this, gen](HRESULT hr, ICoreWebView2Controller* c) -> HRESULT {
            if (gen != generation_) { if (c) c->Close(); return S_OK; }   // destroyed while it was being created
            creating_ = false;
            if (FAILED(hr) || !c || FAILED(c->get_CoreWebView2(&view_.p)) || !view_.p) { Log(L"CHAT panel WebView2 unavailable"); env_.set(nullptr); if (hooks_.created) hooks_.created(false); return S_OK; }
            controller_.set(c); Wire();
            controller_->put_Bounds(bounds_); controller_->put_IsVisible(visible_ ? TRUE : FALSE);
            view_->Navigate(PageUrl);
            if (hooks_.created) hooks_.created(true);
            return S_OK; });
        const HRESULT hr = environment->CreateCoreWebView2Controller(parent, done); done->Release();
        if (FAILED(hr)) { creating_ = false; env_.set(nullptr); ++generation_; return false; }
        return true;
    }
    void Place(RECT bounds) { bounds_ = bounds; if (controller_.p) controller_->put_Bounds(bounds); }
    void Show(bool on) { visible_ = on; if (controller_.p) controller_->put_IsVisible(on ? TRUE : FALSE); }
    bool Visible() const { return visible_; }
    void Focus() { if (controller_.p && visible_) controller_->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC); }
    bool Post(const std::string& json) { return view_.p && SUCCEEDED(view_->PostWebMessageAsJson(AnnotationHost::Wide(json).c_str())); }
    void OpenDevTools() { if (view_.p && devtools_) view_->OpenDevToolsWindow(); }
    ICoreWebView2* Raw() const { return view_.p; }
    // The WebView and its renderer go away; a creation under way is abandoned.
    void Destroy() {
        ++generation_; creating_ = false; visible_ = false;
        if (controller_.p) controller_->Close();
        view_.set(nullptr); controller_.set(nullptr); env_.set(nullptr);
    }
};

} // namespace ChatPanelView
