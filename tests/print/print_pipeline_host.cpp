// tests/print: the real engine behind our print window (#121), in a WebView2 host whose window is never shown (no
// ShowWindow, tool window, off every monitor, never activated), on local pages only, driven by tests/print/run.py.
//
//   print_pipeline_host.exe --base http://127.0.0.1:<port> --udf <temp profile> --out <folder>
//
// It does what src/print-integration.h does, with the same settings and the same pieces (PrintDialog::PageScript,
// ParseRanges / EngineRanges, PrintPreview::Renderer), and checks:
//   * PrintToPdfStream of a very long page, portrait A4 with backgrounds: a multi-page PDF, page count from the
//     preview worker, the time it takes, and that the UI thread kept turning (no gap > 250 ms between its ticks);
//   * landscape: another page count (the sheet turned), rendered pages are wider than tall;
//   * page ranges « 2-3 » and « 1, 3 »: two sheets; « 9999 »: refused by the engine (E_INVALIDARG);
//   * backgrounds off: the dark band of the page prints white;
//   * window.print() of the top document reaches the host as the message « lisiere-print » (our window opens on it in
//     the browser), while the frame's window.print stays the engine's own function.
// Nothing is printed: there is no printer in this test.
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <WebView2.h>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "../../src/chrome.h"
#include "../../src/print-dialog.h"
#include "../../src/print-preview.h"

namespace {
template<class T> const wchar_t* GuidText();
#define GUID_TEXT(T,S) template<> const wchar_t* GuidText<T>() { return L##S; }
GUID_TEXT(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler,"{4e8a3389-c9d8-4bd2-b6b5-124fee6cc14d}")
GUID_TEXT(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler,"{6c4819f3-c9b7-4260-8127-c9f5bde7f68c}")
GUID_TEXT(ICoreWebView2NavigationCompletedEventHandler,"{d33a35bf-1c49-4f98-93ab-006e0533fe1c}")
GUID_TEXT(ICoreWebView2ExecuteScriptCompletedHandler,"{49511172-cc67-4bca-9923-137112f4c4cc}")
GUID_TEXT(ICoreWebView2PrintToPdfStreamCompletedHandler,"{4c9f8229-8f93-444f-a711-2c0dfd6359d5}")
GUID_TEXT(ICoreWebView2WebMessageReceivedEventHandler,"{57213f19-00e6-49fa-8e07-898ea01ecbd2}")

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

std::wstring base, udf, outDir;
ICoreWebView2Environment* env = nullptr; ICoreWebView2Controller* controller = nullptr; ICoreWebView2* view = nullptr;
PrintPreview::Renderer renderer; unsigned generation = 0;
int failures = 0, checks = 0; bool finished = false; std::wstring message;
ULONGLONG lastTick = 0, worstGap = 0; bool measuring = false;

void Say(const char* s) { std::printf("%s\n", s); std::fflush(stdout); }
void Check(bool ok, const char* what) { ++checks; if (!ok) ++failures; std::printf("%s %s\n", ok ? "  ok:" : "FAILED:", what); std::fflush(stdout); }
void Finish() { if (!finished) { finished = true; PostQuitMessage(0); } }
std::map<UINT_PTR, std::function<void()>>& Jobs() { static std::map<UINT_PTR, std::function<void()>> jobs; return jobs; }
void After(UINT ms, std::function<void()> f) {   // a thread timer (no window): its id comes back from SetTimer
    const UINT_PTR id = SetTimer(nullptr, 0, ms, [](HWND, UINT, UINT_PTR t, DWORD) {
        KillTimer(nullptr, t); auto it = Jobs().find(t); if (it == Jobs().end()) return;
        auto run = std::move(it->second); Jobs().erase(it); run();
    });
    Jobs()[id] = std::move(f);
}
// The UI thread's heartbeat: a timer every 16 ms; the longest gap between two ticks while a document is being made.
void CALLBACK Beat(HWND, UINT, UINT_PTR, DWORD) { const ULONGLONG now = GetTickCount64(); if (measuring && lastTick) worstGap = std::max(worstGap, now - lastTick); lastTick = now; }

struct Settings { bool landscape = false, backgrounds = true; std::wstring ranges; };
ICoreWebView2PrintSettings* Make(const Settings& s) {   // as PrintSettingsFor (src/print-integration.h), A4 paper
    ICoreWebView2Environment6* e6 = nullptr; ICoreWebView2PrintSettings* ps = nullptr;
    if (FAILED(env->QueryInterface(IID_ICoreWebView2Environment6, (void**)&e6)) || !e6) return nullptr;
    e6->CreatePrintSettings(&ps); e6->Release(); if (!ps) return nullptr;
    ps->put_Orientation(s.landscape ? COREWEBVIEW2_PRINT_ORIENTATION_LANDSCAPE : COREWEBVIEW2_PRINT_ORIENTATION_PORTRAIT);
    ps->put_PageWidth(210 / 25.4); ps->put_PageHeight(297 / 25.4); ps->put_ShouldPrintBackgrounds(s.backgrounds ? TRUE : FALSE);
    ICoreWebView2PrintSettings2* p2 = nullptr;
    if (!s.ranges.empty() && SUCCEEDED(ps->QueryInterface(IID_ICoreWebView2PrintSettings2, (void**)&p2)) && p2) { p2->put_PageRanges(NativeChrome::PrintDialog::EngineRanges(s.ranges).c_str()); p2->Release(); }
    return ps;
}
struct Made { HRESULT hr = E_FAIL; std::shared_ptr<std::vector<BYTE>> pdf; double ms = 0, copyMs = 0; };
void MakePdf(const Settings& s, std::function<void(Made)> done) {
    ICoreWebView2_16* v16 = nullptr; auto* ps = Make(s);
    if (FAILED(view->QueryInterface(IID_ICoreWebView2_16, (void**)&v16)) || !v16 || !ps) { Made m; done(m); return; }
    const auto start = std::chrono::steady_clock::now(); worstGap = 0; lastTick = GetTickCount64(); measuring = true;
    auto h = Callback<ICoreWebView2PrintToPdfStreamCompletedHandler, HRESULT, IStream*>([done, start](HRESULT hr, IStream* stream) -> HRESULT {
        measuring = false; Made m; m.hr = hr; m.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        m.pdf = std::make_shared<std::vector<BYTE>>();
        if (SUCCEEDED(hr) && stream) {   // the browser's PrintBytes: the stream copied on the UI thread
            const auto c0 = std::chrono::steady_clock::now(); LARGE_INTEGER zero{}; stream->Seek(zero, STREAM_SEEK_SET, nullptr);
            BYTE chunk[1 << 16]; ULONG got = 0; while (SUCCEEDED(stream->Read(chunk, sizeof chunk, &got)) && got) m.pdf->insert(m.pdf->end(), chunk, chunk + got);
            m.copyMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - c0).count();
        }
        done(m); return S_OK; });
    const HRESULT hr = v16->PrintToPdfStream(ps, h); h->Release(); ps->Release(); v16->Release();
    if (FAILED(hr)) { measuring = false; Made m; m.hr = hr; done(m); }
}
// The preview worker on the engine's PDF: page count, then pages drawn.
void Count(const Made& m, std::function<void(int)> done) {
    const unsigned gen = ++generation; renderer.Load(gen, m.pdf);
    auto poll = std::make_shared<std::function<void(int)>>();
    *poll = [gen, done, poll](int left) {
        for (auto& r : renderer.Take()) if (r.generation == gen) { done(r.kind == PrintPreview::Result::Count ? r.count : -1); return; }
        if (left <= 0) { done(-1); return; }
        After(10, [poll, left] { (*poll)(left - 1); });
    };
    (*poll)(2000);
}
void Page(int page, UINT w, UINT h, std::function<void(PrintPreview::Result)> done) {
    const unsigned gen = generation; renderer.Want(gen, page, w, h);
    auto poll = std::make_shared<std::function<void(int)>>();
    *poll = [gen, page, done, poll](int left) {
        for (auto& r : renderer.Take()) if (r.generation == gen && r.page == page) { done(std::move(r)); return; }
        if (left <= 0) { done(PrintPreview::Result{}); return; }
        After(10, [poll, left] { (*poll)(left - 1); });
    };
    (*poll)(1000);
}
void Save(const Made& m, const wchar_t* name) { std::ofstream((outDir + L"\\" + name).c_str(), std::ios::binary).write(reinterpret_cast<const char*>(m.pdf->data()), (std::streamsize)m.pdf->size()); }
// Mean brightness of a horizontal strip (fractions of the page).
double Bright(const PrintPreview::Result& r, float y0, float y1) {
    if (r.pixels.empty()) return -1; double sum = 0; size_t n = 0;
    for (UINT y = (UINT)(y0 * r.height); y < (UINT)(y1 * r.height) && y < r.height; ++y) for (UINT x = r.width / 10; x < r.width * 9 / 10; ++x) { const BYTE* p = &r.pixels[(static_cast<size_t>(y) * r.width + x) * 4]; sum += (p[0] + p[1] + p[2]) / 3.0; ++n; }
    return n ? sum / n : -1;
}

void Steps() {
    char line[256];
    MakePdf({}, [](Made portrait) {
        char l[256];
        Check(SUCCEEDED(portrait.hr) && portrait.pdf->size() > 1000, "long page, portrait A4, backgrounds: the engine made a PDF");
        std::snprintf(l, sizeof l, "  PDF %zu KB in %.0f ms (stream copied on the UI thread in %.1f ms), longest UI-thread gap %llu ms", portrait.pdf->size() / 1024, portrait.ms, portrait.copyMs, worstGap); Say(l);
        Check(worstGap < 250, "the UI thread kept turning while the engine made the document");
        Save(portrait, L"engine-portrait.pdf");
        Count(portrait, [portrait](int pages) {
            char l2[256]; std::snprintf(l2, sizeof l2, "  portrait: %d pages", pages); Say(l2);
            Check(pages >= 10, "preview worker: the long page prints on many sheets");
            Page(0, 280, 396, [pages](PrintPreview::Result first) {
                Check(first.kind == PrintPreview::Result::Page && first.width == 280 && first.height == 396, "page 1 drawn at the sheet's size");
                const double band = Bright(first, .06f, .12f);
                char l3[128]; std::snprintf(l3, sizeof l3, "  backgrounds on: band brightness %.0f", band); Say(l3);
                Check(band >= 0 && band < 120, "backgrounds on: the page's dark band prints dark");
                MakePdf({true, true, L""}, [pages](Made landscape) {
                    Check(SUCCEEDED(landscape.hr), "landscape: the engine made a PDF"); Save(landscape, L"engine-landscape.pdf");
                    Count(landscape, [pages](int lpages) {
                        char l4[128]; std::snprintf(l4, sizeof l4, "  landscape: %d pages (portrait %d)", lpages, pages); Say(l4);
                        Check(lpages > 0 && lpages != pages, "landscape: another page count (the sheet turned)");
                        MakePdf({false, true, L"2-3"}, [](Made ranged) {
                            Check(SUCCEEDED(ranged.hr), "range 2-3: the engine made a PDF"); Save(ranged, L"engine-range-2-3.pdf");
                            Count(ranged, [](int n) {
                                Check(n == 2, "range 2-3: two sheets");
                                MakePdf({false, true, L"1, 3"}, [](Made two) {
                                    Count(two, [](int n2) {
                                        Check(n2 == 2, "range « 1, 3 »: two sheets");
                                        MakePdf({false, true, L"9999"}, [](Made bad) {
                                            { char lb[128]; std::snprintf(lb, sizeof lb, "  range 9999: engine answered hr 0x%08lX, %zu bytes", static_cast<unsigned long>(bad.hr), bad.pdf ? bad.pdf->size() : 0); Say(lb); }
                                            Check(!NativeChrome::PrintDialog::ParseRanges(L"9999", 43) && (FAILED(bad.hr) || !bad.pdf || bad.pdf->size() < 2000), "range 9999: refused by our check before the engine; the engine makes no document of it either");
                                            MakePdf({false, false, L""}, [](Made plain) {
                                                Check(SUCCEEDED(plain.hr), "backgrounds off: the engine made a PDF");
                                                Count(plain, [](int) {
                                                    Page(0, 280, 396, [](PrintPreview::Result first) {
                                                        const double band = Bright(first, .06f, .12f);
                                                        char l5[128]; std::snprintf(l5, sizeof l5, "  backgrounds off: band brightness %.0f", band); Say(l5);
                                                        Check(band > 200, "backgrounds off: the dark band prints white");
                                                        // window.print(): top document -> our message; the frame keeps the engine's.
                                                        view->ExecuteScript(L"JSON.stringify({top:String(window.print).includes('native code'),frame:String(document.getElementById('frame').contentWindow.print).includes('native code')})",
                                                            Callback<ICoreWebView2ExecuteScriptCompletedHandler, HRESULT, LPCWSTR>([](HRESULT, LPCWSTR r) -> HRESULT {
                                                                const std::wstring s = r ? r : L"";
                                                                Check(s.find(L"top\\\":false") != std::wstring::npos && s.find(L"frame\\\":true") != std::wstring::npos, "window.print: replaced in the top document only (the frame keeps the engine's)");
                                                                view->ExecuteScript(L"window.print(); 'called'", nullptr);
                                                                After(1500, [] {
                                                                    Check(message == NativeChrome::PrintDialog::PageMessage, "window.print() of the top document reaches the browser as « lisiere-print »");
                                                                    Finish(); });
                                                                return S_OK; }));
                                                    });
                                                });
                                            });
                                        });
                                    });
                                });
                            });
                        });
                    });
                });
            });
        });
    });
    (void)line;
}
LRESULT CALLBACK WindowProc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }
} // namespace

int wmain(int argc, wchar_t** argv) {
    for (int i = 1; i + 1 < argc; i += 2) { const std::wstring k = argv[i], v = argv[i + 1]; if (k == L"--base") base = v; else if (k == L"--udf") udf = v; else if (k == L"--out") outDir = v; }
    if (base.empty() || udf.empty() || outDir.empty()) { Say("usage: --base URL --udf DIR --out DIR"); return 2; }
    // Per-monitor DPI aware, as the browser (its manifest): runtime 158 refuses a controller in a DPI-unaware process
    // (CreateCoreWebView2Controller: 0x8007139F, ERROR_INVALID_STATE; 24/11 afternoon).
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (!PrintPreview::Renderer::Available() || !renderer.Start(nullptr, 0)) { Say("FAILED: preview worker unavailable"); return 1; }
    WNDCLASSW wc{}; wc.lpfnWndProc = WindowProc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"PrintPipelineHost"; RegisterClassW(&wc);
    // Never shown: the page renders, nothing appears on screen and the focus is never taken.
    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"print pipeline", WS_POPUP, -32000, -32000, 1024, 768, nullptr, nullptr, wc.hInstance, nullptr);
    SetTimer(nullptr, 0, 16, Beat);
    HMODULE loader = LoadLibraryW(L"WebView2Loader.dll");
    using CreateFn = HRESULT(STDAPICALLTYPE*)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*, ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);
    auto create = loader ? reinterpret_cast<CreateFn>(reinterpret_cast<void*>(GetProcAddress(loader, "CreateCoreWebView2EnvironmentWithOptions"))) : nullptr;
    if (!create) { Say("FAILED: WebView2Loader.dll missing"); return 2; }
    auto created = Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT, ICoreWebView2Environment*>([hwnd](HRESULT hr, ICoreWebView2Environment* e) -> HRESULT {
        if (FAILED(hr) || !e) { std::printf("FAILED: engine environment hr=0x%08lX\n", static_cast<unsigned long>(hr)); ++failures; Finish(); return S_OK; }
        env = e; env->AddRef();
        auto made = Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT, ICoreWebView2Controller*>([](HRESULT hr2, ICoreWebView2Controller* c) -> HRESULT {
            if (FAILED(hr2) || !c) { std::printf("FAILED: engine controller hr=0x%08lX\n", static_cast<unsigned long>(hr2)); ++failures; Finish(); return S_OK; }
            controller = c; c->AddRef(); RECT r{0, 0, 1024, 768}; c->put_Bounds(r); c->put_IsVisible(TRUE);
            if (FAILED(c->get_CoreWebView2(&view)) || !view) { Say("FAILED: engine"); ++failures; Finish(); return S_OK; }
            EventRegistrationToken token{};
            auto msg = Callback<ICoreWebView2WebMessageReceivedEventHandler, ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs*>([](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* a) -> HRESULT {
                LPWSTR s = nullptr; if (SUCCEEDED(a->TryGetWebMessageAsString(&s)) && s) { message = s; CoTaskMemFree(s); } return S_OK; });
            view->add_WebMessageReceived(msg, &token); msg->Release();
            view->AddScriptToExecuteOnDocumentCreated(NativeChrome::PrintDialog::PageScript, nullptr);   // as RegisterPrintScript
            auto done = Callback<ICoreWebView2NavigationCompletedEventHandler, ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*>([](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* a) -> HRESULT {
                static bool once = false; if (once) return S_OK; once = true;
                BOOL ok = FALSE; a->get_IsSuccess(&ok); Check(ok != FALSE, "local long page loaded");
                if (!ok) { Finish(); return S_OK; }
                After(500, [] { Steps(); });   // the frame settles
                return S_OK; });
            view->add_NavigationCompleted(done, &token); done->Release();
            view->Navigate((base + L"/long.html").c_str());
            return S_OK; });
        env->CreateCoreWebView2Controller(hwnd, made); made->Release(); return S_OK; });
    const HRESULT hr = create(nullptr, udf.c_str(), nullptr, created); created->Release();
    if (FAILED(hr)) { Say("FAILED: engine"); return 2; }
    SetTimer(nullptr, 0, 180000, [](HWND, UINT, UINT_PTR, DWORD) { Say("FAILED: time out"); ++failures; Finish(); });
    MSG msg; while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    renderer.Stop();
    if (view) view->Release();
    if (controller) { controller->Close(); controller->Release(); }
    if (env) env->Release();
    std::printf("print pipeline: %d checks, %d failed\n", checks, failures); std::fflush(stdout);
    CoUninitialize();
    return failures ? 1 : 0;
}
