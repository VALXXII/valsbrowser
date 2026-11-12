// Read-only evidence on THIS machine with REAL WebView2Loader.dll files: what the browser's startup
// detection decides. Calls only GetAvailableCoreWebView2BrowserVersionString[WithOptions]; the
// environment creation hook refuses, so no runtime process, window, profile or install ever starts.
//   loader_probe.exe <WebView2Loader.dll> [...]
#include "../../src/webview2-install.h"
#include <WebView2.h>

#include <cstdio>

namespace WR = WebView2Runtime;

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) { std::printf("usage: loader_probe <WebView2Loader.dll>...\n"); return 2; }
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 3;
    for (int i = 1; i < argc; ++i) {
        const std::wstring path = argv[i];
        std::printf("== %ls\n", path.c_str());
        HMODULE loader = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        using Plain = HRESULT(STDAPICALLTYPE*)(PCWSTR, LPWSTR*);
        using WithOptions = HRESULT(STDAPICALLTYPE*)(PCWSTR, ICoreWebView2EnvironmentOptions*, LPWSTR*);
        Plain plain = nullptr; WithOptions with = nullptr;
        if (loader) { WebView2Install::Resolve(loader, "GetAvailableCoreWebView2BrowserVersionString", plain); WebView2Install::Resolve(loader, "GetAvailableCoreWebView2BrowserVersionStringWithOptions", with); }
        if (plain) { LPWSTR v = nullptr; HRESULT hr = plain(nullptr, &v); std::printf("GetAvailableCoreWebView2BrowserVersionString(null) = 0x%08lX version=%ls\n", (unsigned long)hr, v ? v : L"(null)"); if (v) CoTaskMemFree(v); }
        if (with) { LPWSTR v = nullptr; HRESULT hr = with(nullptr, nullptr, &v); std::printf("...WithOptions(null, null)          = 0x%08lX version=%ls\n", (unsigned long)hr, v ? v : L"(null)"); if (v) CoTaskMemFree(v); }
        int creates = 0;
        WR::Hooks h;
        h.prepare = [&](WR::Stage& s) -> HRESULT { if (!loader) { s = WR::Stage::Loader; return HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND); } return S_OK; };
        h.probe = [&] { WR::Probe p; if (!with) return p; p.attempted = true; LPWSTR v = nullptr; p.hr = with(nullptr, nullptr, &v); if (v) { p.version = v; CoTaskMemFree(v); } return p; };
        h.create = [&](std::uint64_t) { ++creates; return E_ABORT; };   // never creates an environment here
        h.overrides = [] { return WR::ReadOverrides(L"browser.exe"); };
        h.show = [](const WR::View& v) {
            std::printf("decision: cause=%ls install=%s\n  %ls\n  %ls\n", WR::CauseName(v.diagnosis.cause), v.canInstall ? "offered" : "no",
                        v.diagnosis.title.c_str(), v.diagnosis.technical.c_str());
        };
        WR::Controller c(h);
        c.Attempt();
        if (creates) std::printf("decision: runtime detected, the browser would create its environment (not done by this probe)\n");
        if (loader) FreeLibrary(loader);
    }
    CoUninitialize();
    return 0;
}
