// tests/error-pages: which WebErrorStatus the engine reports for each kind of failure (no window shown: off every
// monitor, never activated). Driven by status_probe.py on local addresses only.
//
//   status_probe_host.exe --udf <temp profile> --url U1 [--fail REASON] --url U2 [--fail REASON] ...
//
// For a --url followed by --fail, DevTools Fetch fails that navigation's request locally with the CDP error REASON
// (what tests/error-pages/window_qa.py does in the real window); otherwise the request really goes out (to a
// closed local port, for instance). Prints « STATUS <url> success=<0|1> web=<WebErrorStatus> http=<code> ».
#ifndef UNICODE
#define UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <WebView2.h>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

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

struct Step { std::wstring url, fail; };
std::vector<Step> steps;
size_t current = 0;
std::wstring udf, inspect;
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
void Cdp(const wchar_t* method, const std::wstring& params) {
    auto h = Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler, HRESULT, LPCWSTR>([m = std::wstring(method)](HRESULT hr, LPCWSTR) -> HRESULT {
        if (FAILED(hr)) Say(L"DEVTOOLS FAILED " + m);
        return S_OK; });
    view->CallDevToolsProtocolMethod(method, params.c_str(), h); h->Release();
}
void Next() {
    if (current >= steps.size()) { Finish(); return; }
    view->Navigate(steps[current].url.c_str());
}
void OnView() {
    // Fetch: every request is paused; a step with --fail is failed with its reason, anything else goes on.
    ICoreWebView2DevToolsProtocolEventReceiver* r = nullptr;
    if (SUCCEEDED(view->GetDevToolsProtocolEventReceiver(L"Fetch.requestPaused", &r)) && r) {
        EventRegistrationToken t{};
        auto h = Callback<ICoreWebView2DevToolsProtocolEventReceivedEventHandler, ICoreWebView2*, ICoreWebView2DevToolsProtocolEventReceivedEventArgs*>(
            [](ICoreWebView2*, ICoreWebView2DevToolsProtocolEventReceivedEventArgs* a) -> HRESULT {
                LPWSTR j = nullptr; if (FAILED(a->get_ParameterObjectAsJson(&j)) || !j) return S_OK;
                const std::wstring json = j; CoTaskMemFree(j);
                const auto at = json.find(L"\"requestId\":\""); if (at == std::wstring::npos) return S_OK;
                const std::wstring id = json.substr(at + 13, json.find(L'"', at + 13) - at - 13);
                const std::wstring fail = current < steps.size() ? steps[current].fail : L"";
                if (!fail.empty()) Cdp(L"Fetch.failRequest", L"{\"requestId\":\"" + id + L"\",\"errorReason\":\"" + fail + L"\"}");
                else Cdp(L"Fetch.continueRequest", L"{\"requestId\":\"" + id + L"\"}");
                return S_OK; });
        r->add_DevToolsProtocolEventReceived(h, &t); h->Release(); r->Release();
    }
    Cdp(L"Fetch.enable", L"{\"patterns\":[{\"urlPattern\":\"*\",\"requestStage\":\"Request\"}]}");
    EventRegistrationToken token{};
    auto completed = Callback<ICoreWebView2NavigationCompletedEventHandler, ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*>(
        [](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* a) -> HRESULT {
            BOOL ok = FALSE; a->get_IsSuccess(&ok);
            COREWEBVIEW2_WEB_ERROR_STATUS web = COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN; a->get_WebErrorStatus(&web);
            int http = 0; ICoreWebView2NavigationCompletedEventArgs2* a2 = nullptr;
            if (SUCCEEDED(a->QueryInterface(IID_ICoreWebView2NavigationCompletedEventArgs2, (void**)&a2)) && a2) { a2->get_HttpStatusCode(&http); a2->Release(); }
            Say(L"STATUS " + steps[current].url + L" fail=" + (steps[current].fail.empty() ? L"-" : steps[current].fail) + L" success=" + std::to_wstring(ok ? 1 : 0) + L" web=" + std::to_wstring((int)web) + L" http=" + std::to_wstring(http));
            if (ok || inspect.empty()) { ++current; Next(); return S_OK; }
            // What the engine's own error document says (chrome-error:, before our page would replace it).
            auto done = Callback<ICoreWebView2ExecuteScriptCompletedHandler, HRESULT, LPCWSTR>([](HRESULT, LPCWSTR r) -> HRESULT {
                Say(L"PAGE " + steps[current].url + L" " + (r ? r : L"")); ++current; Next(); return S_OK; });
            view->ExecuteScript(inspect.c_str(), done); done->Release();
            return S_OK; });
    view->add_NavigationCompleted(completed, &token); completed->Release();
    Next();
}
LRESULT CALLBACK WindowProc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }
} // namespace

int wmain(int argc, wchar_t** argv) {
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::wstring k = argv[i], v = argv[i + 1];
        if (k == L"--udf") udf = v; else if (k == L"--inspect") inspect = v; else if (k == L"--url") steps.push_back({v, L""}); else if (k == L"--fail" && !steps.empty()) steps.back().fail = v;
    }
    if (udf.empty() || steps.empty()) { Say(L"usage: --udf DIR --url U [--fail REASON] ..."); return 2; }
    // WebView2 158 refuses a controller in a process that is not DPI-aware (0x8007139F, ERROR_INVALID_STATE): the browser
    // declares it in its manifest, this host does it here, before the environment.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    WNDCLASSW wc{}; wc.lpfnWndProc = WindowProc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"ErrorStatusProbe"; RegisterClassW(&wc);
    // Never shown: nothing appears on screen and the focus is never taken.
    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"status probe", WS_POPUP, -32000, -32000, 800, 600, nullptr, nullptr, wc.hInstance, nullptr);
    HMODULE loader = LoadLibraryW(L"WebView2Loader.dll");
    using CreateFn = HRESULT(STDAPICALLTYPE*)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*, ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);
    auto create = loader ? reinterpret_cast<CreateFn>(reinterpret_cast<void*>(GetProcAddress(loader, "CreateCoreWebView2EnvironmentWithOptions"))) : nullptr;
    if (!create) { Say(L"WebView2Loader.dll missing"); return 2; }
    auto created = Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT, ICoreWebView2Environment*>([hwnd](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
        if (FAILED(hr) || !env) { Say(L"ENGINE FAILED hr=" + std::to_wstring((long)hr)); Finish(); return S_OK; }
        auto made = Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT, ICoreWebView2Controller*>([](HRESULT hr2, ICoreWebView2Controller* c) -> HRESULT {
            if (FAILED(hr2) || !c) { Say(L"ENGINE FAILED controller hr=" + std::to_wstring((long)hr2)); Finish(); return S_OK; }
            controller = c; c->AddRef(); RECT r{0, 0, 800, 600}; c->put_Bounds(r); c->put_IsVisible(TRUE);
            if (FAILED(c->get_CoreWebView2(&view)) || !view) { Say(L"ENGINE FAILED"); Finish(); return S_OK; }
            OnView(); return S_OK; });
        env->CreateCoreWebView2Controller(hwnd, made); made->Release(); return S_OK; });
    const HRESULT hr = create(nullptr, udf.c_str(), nullptr, created); created->Release();
    if (FAILED(hr)) { Say(L"ENGINE FAILED create hr=" + std::to_wstring((long)hr)); return 2; }
    SetTimer(nullptr, 0, 90000, [](HWND, UINT, UINT_PTR, DWORD) { Say(L"TIMEOUT"); Finish(); });
    MSG msg; while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    if (view) view->Release();
    if (controller) { controller->Close(); controller->Release(); }
    return 0;
}
