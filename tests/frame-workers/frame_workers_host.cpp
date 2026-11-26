// tests/frame-workers: a WebView2 host whose window is never shown (no ShowWindow, tool window, off every
// monitor, never activated), driven by run.py on local pages only. It attaches to the page exactly like the
// password vault's capture (src/elements-integration.h VaultSessionSetup / Target.attachedToTarget): same
// DevTools calls, same Vault:: parameters and parser, frames set up the same way, every other target resumed.
// Then it reports which workers of the page and of its cross-site frame answered.
//
//   frame_workers_host.exe --url <top page> --udf <temp profile> --attach none|vault|iframe-only [--seconds N]
//
// --attach iframe-only is the vault's filter before the fix (iframes only): the reference that reproduces the
// bug (Cloudflare Turnstile's « Verification failed » on polar.sh).
#ifndef UNICODE
#define UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <WebView2.h>
#include <cstdio>
#include <functional>
#include <string>
#include "password-vault.h"

namespace {
template<class T> const wchar_t* GuidText();
#define GUID_TEXT(T,S) template<> const wchar_t* GuidText<T>() { return L##S; }
GUID_TEXT(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler,"{4e8a3389-c9d8-4bd2-b6b5-124fee6cc14d}")
GUID_TEXT(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler,"{6c4819f3-c9b7-4260-8127-c9f5bde7f68c}")
GUID_TEXT(ICoreWebView2NavigationCompletedEventHandler,"{d33a35bf-1c49-4f98-93ab-006e0533fe1c}")
GUID_TEXT(ICoreWebView2ExecuteScriptCompletedHandler,"{49511172-cc67-4bca-9923-137112f4c4cc}")
GUID_TEXT(ICoreWebView2CallDevToolsProtocolMethodCompletedHandler,"{5c4889f0-5ef6-4c5a-952c-d8f1b92d0574}")
GUID_TEXT(ICoreWebView2DevToolsProtocolEventReceivedEventHandler,"{e2fda4be-5456-406c-a261-3d452138362c}")

template<class I, class... A> class Handler final : public I {
    LONG refs = 1; IID iid{}; std::function<HRESULT(A...)> fn;
public:
    explicit Handler(std::function<HRESULT(A...)> f) : fn(std::move(f)) { CLSIDFromString(GuidText<I>(), &iid); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (id == IID_IUnknown || id == iid) { *out = static_cast<I*>(this); AddRef(); return S_OK; }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override { ULONG n = InterlockedDecrement(&refs); if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE Invoke(A... args) override { return fn(args...); }
};
template<class I, class... A, class F> I* Callback(F f) { return new Handler<I, A...>(std::move(f)); }
IID Iid(const wchar_t* s) { IID i{}; CLSIDFromString(s, &i); return i; }
const IID IID_View11 = Iid(L"{0be78e56-c193-4051-b943-23b460c08bdb}");      // ICoreWebView2_11
const IID IID_EventArgs2 = Iid(L"{2dc4959d-1494-4393-95ba-bea4cb9ebd1b}");  // ICoreWebView2DevToolsProtocolEventReceivedEventArgs2

std::wstring url, udf, attach = L"vault";
int seconds = 12, polls = 0;
ICoreWebView2Controller* controller = nullptr;
ICoreWebView2* view = nullptr;
bool finished = false;

std::string Utf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0'); WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr); return s;
}
void Say(const std::wstring& s) { printf("%s\n", Utf8(s).c_str()); fflush(stdout); }
void Finish() { if (!finished) { finished = true; PostQuitMessage(0); } }
void After(UINT ms, TIMERPROC f) { SetTimer(nullptr, 0, ms, f); }

std::wstring AttachParams() {
    if (attach == L"iframe-only")   // the vault's filter before the fix
        return L"{\"autoAttach\":true,\"waitForDebuggerOnStart\":true,\"flatten\":true,\"filter\":[{\"type\":\"iframe\",\"exclude\":false}]}";
    return Vault::AutoAttachParams();
}
void Call(const std::wstring& session, const wchar_t* method, const std::wstring& params) {
    auto done = Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler, HRESULT, LPCWSTR>([m = std::wstring(method)](HRESULT hr, LPCWSTR) -> HRESULT {
        if (FAILED(hr)) Say(L"DEVTOOLS FAILED " + m);
        return S_OK; });
    if (session.empty()) view->CallDevToolsProtocolMethod(method, params.c_str(), done);
    else { ICoreWebView2_11* v11 = nullptr; if (SUCCEEDED(view->QueryInterface(IID_View11, (void**)&v11)) && v11) { v11->CallDevToolsProtocolMethodForSession(session.c_str(), method, params.c_str(), done); v11->Release(); } }
    done->Release();
}
// VaultSessionSetup: world, binding, auto-attach (nested frames), then a frame that waits for us is resumed.
void SessionSetup(const std::wstring& session) {
    Call(session, L"Page.enable", L"{}");
    Call(session, L"Runtime.addBinding", Vault::CaptureBindingParams());
    Call(session, L"Page.addScriptToEvaluateOnNewDocument", Vault::CaptureScriptParams());
    Call(session, L"Target.setAutoAttach", AttachParams());
    if (!session.empty()) Call(session, L"Runtime.runIfWaitingForDebugger", L"{}");
}
void Receive(const wchar_t* event, std::function<void(const std::wstring&)> f) {
    ICoreWebView2DevToolsProtocolEventReceiver* r = nullptr;
    if (FAILED(view->GetDevToolsProtocolEventReceiver(event, &r)) || !r) { Say(L"no receiver"); return; }
    EventRegistrationToken token{};
    auto h = Callback<ICoreWebView2DevToolsProtocolEventReceivedEventHandler, ICoreWebView2*, ICoreWebView2DevToolsProtocolEventReceivedEventArgs*>(
        [f](ICoreWebView2*, ICoreWebView2DevToolsProtocolEventReceivedEventArgs* a) -> HRESULT {
            LPWSTR j = nullptr; if (FAILED(a->get_ParameterObjectAsJson(&j)) || !j) return S_OK;
            const std::wstring json = j; CoTaskMemFree(j); f(json); return S_OK; });
    r->add_DevToolsProtocolEventReceived(h, &token); h->Release(); r->Release();
}
void InstallVaultAttach() {
    // Target.attachedToTarget, as in elements-integration.h: an iframe gets the capture setup, any other target is
    // only resumed (VaultResume).
    Receive(L"Target.attachedToTarget", [](const std::wstring& json) {
        Vault::AttachedTarget a;
        if (!Vault::ParseAttachedToTarget(json, a)) { Say(L"ATTACHED unparsed"); return; }
        Say(L"ATTACHED " + a.type + (a.waiting ? L" (waiting)" : L""));
        if (a.type == L"iframe") SessionSetup(a.sessionId); else Call(a.sessionId, L"Runtime.runIfWaitingForDebugger", L"{}");
    });
    SessionSetup(L"");
    Call(L"", L"Runtime.runIfWaitingForDebugger", L"{}");
}
void Poll() {
    if (finished) return;
    view->ExecuteScript(L"JSON.stringify(window.results||{})", Callback<ICoreWebView2ExecuteScriptCompletedHandler, HRESULT, LPCWSTR>([](HRESULT, LPCWSTR r) -> HRESULT {
        const std::wstring s = r ? r : L"";
        // 6 checks in the page + 6 in the frame, each true or false once it answered.
        size_t answered = 0;
        for (const wchar_t* word : {L"true", L"false"}) for (size_t at = s.find(word); at != std::wstring::npos; at = s.find(word, at + 1)) ++answered;
        if (answered >= 12 || ++polls > seconds * 4) { Say(L"RESULTS " + s); Finish(); return S_OK; }
        After(250, [](HWND, UINT, UINT_PTR id, DWORD) { KillTimer(nullptr, id); Poll(); });
        return S_OK; }));
}
void OnView() {
    if (attach != L"none") InstallVaultAttach();
    EventRegistrationToken token{};
    auto completed = Callback<ICoreWebView2NavigationCompletedEventHandler, ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*>(
        [](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* a) -> HRESULT {
            BOOL ok = FALSE; a->get_IsSuccess(&ok); Say(ok ? L"LOADED" : L"LOAD FAILED");
            if (!ok) { Finish(); return S_OK; }
            Poll(); return S_OK; });
    view->add_NavigationCompleted(completed, &token); completed->Release();
    view->Navigate(url.c_str());
}
LRESULT CALLBACK WindowProc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }
} // namespace

int wmain(int argc, wchar_t** argv) {
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::wstring k = argv[i], v = argv[i + 1];
        if (k == L"--url") url = v; else if (k == L"--udf") udf = v; else if (k == L"--attach") attach = v; else if (k == L"--seconds") seconds = _wtoi(v.c_str());
    }
    if (url.empty() || udf.empty() || (attach != L"none" && attach != L"vault" && attach != L"iframe-only")) { Say(L"usage: --url U --udf DIR --attach none|vault|iframe-only"); return 2; }
    Vault::ActiveFlag() = true;
    // WebView2 158 refuses a controller in a process that is not DPI-aware (0x8007139F, ERROR_INVALID_STATE): the browser
    // declares it in its manifest, this host does it here, before the environment.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    WNDCLASSW wc{}; wc.lpfnWndProc = WindowProc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"FrameWorkersHost"; RegisterClassW(&wc);
    // Never shown: the page renders, nothing appears on screen and the focus is never taken.
    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"frame workers", WS_POPUP, -32000, -32000, 1024, 768, nullptr, nullptr, wc.hInstance, nullptr);
    HMODULE loader = LoadLibraryW(L"WebView2Loader.dll");
    using CreateFn = HRESULT(STDAPICALLTYPE*)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*, ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);
    auto create = loader ? reinterpret_cast<CreateFn>(reinterpret_cast<void*>(GetProcAddress(loader, "CreateCoreWebView2EnvironmentWithOptions"))) : nullptr;
    if (!create) { Say(L"WebView2Loader.dll missing"); return 2; }
    auto created = Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT, ICoreWebView2Environment*>([hwnd](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
        if (FAILED(hr) || !env) { Say(L"ENGINE FAILED"); Finish(); return S_OK; }
        auto made = Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT, ICoreWebView2Controller*>([](HRESULT hr2, ICoreWebView2Controller* c) -> HRESULT {
            if (FAILED(hr2) || !c) { Say(L"ENGINE FAILED"); Finish(); return S_OK; }
            controller = c; c->AddRef(); RECT r{0, 0, 1024, 768}; c->put_Bounds(r); c->put_IsVisible(TRUE);
            if (FAILED(c->get_CoreWebView2(&view)) || !view) { Say(L"ENGINE FAILED"); Finish(); return S_OK; }
            OnView(); return S_OK; });
        env->CreateCoreWebView2Controller(hwnd, made); made->Release(); return S_OK; });
    const HRESULT hr = create(nullptr, udf.c_str(), nullptr, created); created->Release();
    if (FAILED(hr)) { Say(L"ENGINE FAILED"); return 2; }
    After((UINT)(seconds + 30) * 1000, [](HWND, UINT, UINT_PTR, DWORD) { Say(L"RESULTS {}"); Finish(); });
    MSG msg; while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    if (view) view->Release();
    if (controller) { controller->Close(); controller->Release(); }
    return 0;
}
