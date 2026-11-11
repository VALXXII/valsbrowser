// FAKE WebView2Loader.dll for tests/webview2-runtime/host_test only (never packaged): exports the
// two loader functions the browser uses, driven by "fake-loader-mode.txt" next to this DLL, and
// appends every call to "fake-loader-calls.txt". It starts no runtime and creates no browser process.
//   missing      probe → 0x80070002 / null version; creation → 0x80070002 (the reported machine)
//   present      probe → 999.0.0.0; creation succeeds asynchronously with a stub environment
//   uninstalled  probe → 999.0.0.0; creation callback → ERROR_PRODUCT_UNINSTALLED
//   denied       probe → 999.0.0.0; creation fails immediately with E_ACCESSDENIED
#include <windows.h>
#include <WebView2.h>

#include <fstream>
#include <string>
#include <vector>

static HMODULE self;
static std::wstring Here(const wchar_t* name) {
    wchar_t path[MAX_PATH]; GetModuleFileNameW(self, path, MAX_PATH);
    std::wstring p = path; return p.substr(0, p.find_last_of(L'\\') + 1) + name;
}
static std::string Mode() {
    std::ifstream in(Here(L"fake-loader-mode.txt").c_str()); std::string m; in >> m; return m.empty() ? "missing" : m;
}
static void Record(const char* what) { std::ofstream out(Here(L"fake-loader-calls.txt").c_str(), std::ios::app); out << what << "\n"; }
static LPWSTR Copy(const wchar_t* s) { size_t n = (wcslen(s) + 1) * sizeof(wchar_t); auto p = static_cast<LPWSTR>(CoTaskMemAlloc(n)); if (p) memcpy(p, s, n); return p; }

class StubEnvironment final : public ICoreWebView2Environment {
    LONG refs = 1;
    std::vector<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler*> held;   // never completed
public:
    ~StubEnvironment() { for (auto* h : held) h->Release(); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (IsEqualIID(id, IID_IUnknown)) { *out = static_cast<IUnknown*>(this); AddRef(); return S_OK; }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override { ULONG n = InterlockedDecrement(&refs); if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE CreateCoreWebView2Controller(HWND, ICoreWebView2CreateCoreWebView2ControllerCompletedHandler* h) override {
        Record("controller"); if (h) { h->AddRef(); held.push_back(h); } return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateWebResourceResponse(IStream*, int, LPCWSTR, LPCWSTR, ICoreWebView2WebResourceResponse**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_BrowserVersionString(LPWSTR* v) override { if (!v) return E_POINTER; *v = Copy(L"999.0.0.0-fake"); return S_OK; }
    HRESULT STDMETHODCALLTYPE add_NewBrowserVersionAvailable(ICoreWebView2NewBrowserVersionAvailableEventHandler*, EventRegistrationToken* t) override { if (t) t->value = 1; return S_OK; }
    HRESULT STDMETHODCALLTYPE remove_NewBrowserVersionAvailable(EventRegistrationToken) override { return S_OK; }
};

struct Pending { ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* handler; HRESULT hr; };
static std::vector<Pending> pending;
static void CALLBACK Complete(HWND, UINT, UINT_PTR id, DWORD) {
    KillTimer(nullptr, id);
    auto list = std::move(pending); pending.clear();
    for (auto& p : list) {
        if (SUCCEEDED(p.hr)) { auto* env = new StubEnvironment(); p.handler->Invoke(S_OK, env); env->Release(); }
        else p.handler->Invoke(p.hr, nullptr);
        p.handler->Release();
    }
}

extern "C" __declspec(dllexport) HRESULT STDAPICALLTYPE GetAvailableCoreWebView2BrowserVersionStringWithOptions(PCWSTR, ICoreWebView2EnvironmentOptions*, LPWSTR* version) {
    Record("probe");
    if (!version) return E_POINTER;
    *version = nullptr;
    if (Mode() == "missing") return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    *version = Copy(L"999.0.0.0");
    return S_OK;
}
extern "C" __declspec(dllexport) HRESULT STDAPICALLTYPE CreateCoreWebView2EnvironmentWithOptions(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*, ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* handler) {
    Record("create");
    const std::string m = Mode();
    if (m == "missing") return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    if (m == "denied") return E_ACCESSDENIED;
    if (!handler) return E_POINTER;
    handler->AddRef();
    pending.push_back({handler, m == "uninstalled" ? HRESULT_FROM_WIN32(ERROR_PRODUCT_UNINSTALLED) : S_OK});
    SetTimer(nullptr, 0, 10, Complete);   // asynchronous, on the caller's message loop like the real loader
    return S_OK;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) { if (reason == DLL_PROCESS_ATTACH) self = h; return TRUE; }
