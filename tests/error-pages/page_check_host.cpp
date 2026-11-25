// tests/error-pages: a WebView2 host whose window is never shown (no ShowWindow, tool window, off every monitor,
// never activated), driven by run.py. It shows each generated error page with NavigateToString (as the browser
// does), at 1x (DevTools Emulation, 1180x700 CSS px), waits for its fonts, runs the case's prepare script (open
// the technical details, hover a button through DevTools input: no real mouse), then prints the measurements of
// measure.js and writes a 1x PNG of the viewport (DevTools Page.captureScreenshot, no screen involved).
//
//   page_check_host.exe --pages <dir> --names a,b,c --udf <temp profile> --out <dir>
//
// <dir>/<name>.html (gen_pages.exe), <dir>/measure.js, optional <dir>/<name>.prepare.js returning "x,y" (hover).
#ifndef UNICODE
#define UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
#include <WebView2.h>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iterator>
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

std::wstring pagesDir, udf, outDir;
std::vector<std::wstring> names;
size_t current = 0;
int fontPolls = 0;
ICoreWebView2Controller* controller = nullptr;
ICoreWebView2* view = nullptr;
bool finished = false;

std::string Utf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0'); WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr); return s;
}
std::wstring Wide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0'); MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n); return w;
}
std::string ReadFile(const std::wstring& path) { std::ifstream in(path.c_str(), std::ios::binary); return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()); }
void Say(const std::wstring& s) { printf("%s\n", Utf8(s).c_str()); fflush(stdout); }
void Finish() { if (!finished) { finished = true; PostQuitMessage(0); } }
void After(UINT ms, std::function<void()> f) {
    static std::function<void()> pending; pending = std::move(f);
    SetTimer(nullptr, 0, ms, [](HWND, UINT, UINT_PTR id, DWORD) { KillTimer(nullptr, id); auto run = std::move(pending); run(); });
}
void Cdp(const wchar_t* method, const std::wstring& params, std::function<void(HRESULT, const std::wstring&)> done) {
    auto h = Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler, HRESULT, LPCWSTR>([done](HRESULT hr, LPCWSTR json) -> HRESULT {
        done(hr, json ? json : L""); return S_OK; });
    view->CallDevToolsProtocolMethod(method, params.c_str(), h); h->Release();
}
void Script(const std::wstring& js, std::function<void(const std::wstring&)> done) {
    auto h = Callback<ICoreWebView2ExecuteScriptCompletedHandler, HRESULT, LPCWSTR>([done](HRESULT, LPCWSTR r) -> HRESULT {
        done(r ? r : L""); return S_OK; });
    view->ExecuteScript(js.c_str(), h); h->Release();
}
// ExecuteScript's result is JSON: a string comes back quoted.
std::wstring Unquote(const std::wstring& json) {
    if (json.size() < 2 || json.front() != L'"') return json;
    std::wstring r;
    for (size_t i = 1; i + 1 < json.size(); ++i) { if (json[i] == L'\\' && i + 2 < json.size()) { ++i; r += json[i] == L'n' ? L'\n' : json[i]; } else r += json[i]; }
    return r;
}
void Next();
void Capture() {
    Cdp(L"Page.captureScreenshot", L"{\"format\":\"png\",\"fromSurface\":true}", [](HRESULT hr, const std::wstring& json) {
        const std::wstring name = names[current];
        const auto at = json.find(L"\"data\":\""); const auto end = at == std::wstring::npos ? at : json.find(L'"', at + 8);
        if (FAILED(hr) || end == std::wstring::npos) { Say(L"CAPTURE FAILED " + name); ++current; Next(); return; }
        const std::wstring b64 = json.substr(at + 8, end - at - 8);
        DWORD size = 0; CryptStringToBinaryW(b64.c_str(), (DWORD)b64.size(), CRYPT_STRING_BASE64, nullptr, &size, nullptr, nullptr);
        std::string bytes(size, '\0');
        if (!size || !CryptStringToBinaryW(b64.c_str(), (DWORD)b64.size(), CRYPT_STRING_BASE64, (BYTE*)bytes.data(), &size, nullptr, nullptr)) { Say(L"CAPTURE FAILED " + name); ++current; Next(); return; }
        std::ofstream((outDir + L"\\" + name + L".png").c_str(), std::ios::binary) << bytes;
        Say(L"CAPTURED " + name);
        ++current; Next();
    });
}
void Measure() {
    Script(Wide(ReadFile(pagesDir + L"\\measure.js")), [](const std::wstring& r) {
        Say(L"MEASURE " + names[current] + L" " + r);
        After(100, [] { Capture(); });
    });
}
void Prepare() {
    const std::string js = ReadFile(pagesDir + L"\\" + names[current] + L".prepare.js");
    if (js.empty()) { After(150, [] { Measure(); }); return; }
    // The script returns what to do next: "x,y" or "hover:x,y" (DevTools mouse move), "press:x,y" (move, then the
    // button held down), "post:<text>" (the host posts <text> to the page, as main does), "wait:<ms>", or nothing.
    Script(Wide(js), [](const std::wstring& r) {
        std::wstring cmd = Unquote(r), arg = cmd; const auto colon = cmd.find(L':');
        if (colon != std::wstring::npos) { arg = cmd.substr(colon + 1); cmd = cmd.substr(0, colon); } else cmd = L"hover";
        if (cmd == L"post") { view->PostWebMessageAsString(arg.c_str()); After(300, [] { Measure(); }); return; }
        if (cmd == L"wait") { After((UINT)_wtoi(arg.c_str()), [] { Measure(); }); return; }
        const auto comma = arg.find(L',');
        if (comma == std::wstring::npos) { After(250, [] { Measure(); }); return; }
        const std::wstring at = L"\"x\":" + arg.substr(0, comma) + L",\"y\":" + arg.substr(comma + 1);
        const bool press = cmd == L"press";
        Cdp(L"Input.dispatchMouseEvent", L"{\"type\":\"mouseMoved\"," + at + L"}", [press, at](HRESULT, const std::wstring&) {
            if (!press) { After(900, [] { Measure(); }); return; }   // 600 ms tooltip delay + 120 ms fade
            Cdp(L"Input.dispatchMouseEvent", L"{\"type\":\"mousePressed\",\"button\":\"left\",\"clickCount\":1," + at + L"}", [](HRESULT, const std::wstring&) { After(250, [] { Measure(); }); });
        });
    });
}
void WaitFonts() {
    Script(L"document.fonts.status", [](const std::wstring& r) {
        if (r == L"\"loaded\"" || ++fontPolls > 50) { if (fontPolls > 50) Say(L"FONTS NOT LOADED " + names[current]); Prepare(); return; }
        After(100, [] { WaitFonts(); });
    });
}
void Next() {
    if (current >= names.size()) { Finish(); return; }
    Cdp(L"Input.dispatchMouseEvent", L"{\"type\":\"mouseReleased\",\"button\":\"left\",\"clickCount\":1,\"x\":1,\"y\":1}", [](HRESULT, const std::wstring&) {});
    Cdp(L"Input.dispatchMouseEvent", L"{\"type\":\"mouseMoved\",\"x\":1,\"y\":1}", [](HRESULT, const std::wstring&) {});
    fontPolls = 0;
    const std::wstring html = Wide(ReadFile(pagesDir + L"\\" + names[current] + L".html"));
    const HRESULT hr = html.empty() ? E_INVALIDARG : view->NavigateToString(html.c_str());
    Say(L"NAVIGATE " + names[current] + L" hr=" + std::to_wstring((long)hr) + L" bytes=" + std::to_wstring(Utf8(html).size()));
    if (FAILED(hr)) { ++current; Next(); }
}
void OnView() {
    EventRegistrationToken token{};
    auto completed = Callback<ICoreWebView2NavigationCompletedEventHandler, ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*>(
        [](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* a) -> HRESULT {
            BOOL ok = FALSE; a->get_IsSuccess(&ok);
            if (!ok) { Say(L"LOAD FAILED " + names[current]); ++current; Next(); return S_OK; }
            WaitFonts(); return S_OK; });
    view->add_NavigationCompleted(completed, &token); completed->Release();
    Cdp(L"Emulation.setDeviceMetricsOverride", L"{\"width\":1180,\"height\":700,\"deviceScaleFactor\":1,\"mobile\":false}", [](HRESULT hr, const std::wstring&) {
        if (FAILED(hr)) Say(L"EMULATION FAILED");
        Next(); });
}
LRESULT CALLBACK WindowProc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }
} // namespace

int wmain(int argc, wchar_t** argv) {
    std::wstring list;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::wstring k = argv[i], v = argv[i + 1];
        if (k == L"--pages") pagesDir = v; else if (k == L"--names") list = v; else if (k == L"--udf") udf = v; else if (k == L"--out") outDir = v;
    }
    for (size_t s = 0; s <= list.size();) { const size_t e = list.find(L',', s); const std::wstring n = list.substr(s, e == std::wstring::npos ? std::wstring::npos : e - s); if (!n.empty()) names.push_back(n); if (e == std::wstring::npos) break; s = e + 1; }
    if (pagesDir.empty() || udf.empty() || outDir.empty() || names.empty()) { Say(L"usage: --pages DIR --names a,b --udf DIR --out DIR"); return 2; }
    // WebView2 158 refuses a controller in a process that is not DPI-aware (0x8007139F, ERROR_INVALID_STATE): the browser
    // declares it in its manifest, this host does it here, before the environment.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    WNDCLASSW wc{}; wc.lpfnWndProc = WindowProc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"ErrorPagesCheckHost"; RegisterClassW(&wc);
    // Never shown: the page renders, nothing appears on screen and the focus is never taken.
    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"error pages check", WS_POPUP, -32000, -32000, 1180, 700, nullptr, nullptr, wc.hInstance, nullptr);
    HMODULE loader = LoadLibraryW(L"WebView2Loader.dll");
    using CreateFn = HRESULT(STDAPICALLTYPE*)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*, ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);
    auto create = loader ? reinterpret_cast<CreateFn>(reinterpret_cast<void*>(GetProcAddress(loader, "CreateCoreWebView2EnvironmentWithOptions"))) : nullptr;
    if (!create) { Say(L"WebView2Loader.dll missing"); return 2; }
    auto created = Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT, ICoreWebView2Environment*>([hwnd](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
        if (FAILED(hr) || !env) { Say(L"ENGINE FAILED"); Finish(); return S_OK; }
        auto made = Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT, ICoreWebView2Controller*>([](HRESULT hr2, ICoreWebView2Controller* c) -> HRESULT {
            if (FAILED(hr2) || !c) { Say(L"ENGINE FAILED"); Finish(); return S_OK; }
            controller = c; c->AddRef(); RECT r{0, 0, 1180, 700}; c->put_Bounds(r); c->put_IsVisible(TRUE);
            if (FAILED(c->get_CoreWebView2(&view)) || !view) { Say(L"ENGINE FAILED"); Finish(); return S_OK; }
            OnView(); return S_OK; });
        env->CreateCoreWebView2Controller(hwnd, made); made->Release(); return S_OK; });
    const HRESULT hr = create(nullptr, udf.c_str(), nullptr, created); created->Release();
    if (FAILED(hr)) { Say(L"ENGINE FAILED"); return 2; }
    SetTimer(nullptr, 0, 120000, [](HWND, UINT, UINT_PTR, DWORD) { Say(L"TIMEOUT"); Finish(); });
    MSG msg; while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    if (view) view->Release();
    if (controller) { controller->Close(); controller->Release(); }
    return 0;
}
