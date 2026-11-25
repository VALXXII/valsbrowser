// tests/crash-screenshare: engine facts behind the crashed-tab page and the screen-share confirmation (#121), measured
// on a WebView2 host whose window is never shown (no ShowWindow, tool window, off every monitor, never activated),
// driven by engine_probe.py on local pages only, one fresh temporary profile per run. Nothing appears on screen.
//
//   engine_probe_host.exe --url <page A> --url2 <page B> --udf <temp profile> --mode <mode> [--fonts <dir>] [--shot <png>]
//
// Modes (each prints FACT lines read by engine_probe.py):
//   crash-builtin   A then B, renderer crashed (DevTools Page.crash): ProcessFailed kind/reason, whether the engine's
//                   own page answers scripts, then Reload(): does B come back?
//   crash-ours      same crash, OUR page (src/crash-page.h, real fonts) shown at once with NavigateToString, as
//                   src/main.cpp does: its history entry, a capture of it (--shot), Reload() on it, then « Recharger »
//                   as GoBack() (back to the crashed entry), then a link from there (our entry is pruned).
//   crash-off       put_IsBuiltInErrorPageEnabled(FALSE), crash: is there a live document to write into?
//   crash-kill      renderer processes terminated from outside (task manager, low memory): kind/reason, then ours.
//   share           ScreenCaptureStarting: getDisplayMedia() with and without a user gesture, twice from the page and
//                   once from a cross-site frame; the host takes the deferral and answers Cancel (« Bloquer ») 600 ms
//                   later: what the page gets. The engine's picker is never allowed to open (Cancel only).
//   hang            the page's main thread loops forever: does RENDER_PROCESS_UNRESPONSIVE come, and can the tab still
//                   show a document (NavigateToString) or go back to the site?
//   picker          RUN ON A HIDDEN DESKTOP ONLY (engine_probe.py does; refused on the desktop on screen): « Choisir… » as
//                   src/elements-integration.h answers it (deferral, Cancel FALSE) lets the engine show its own picker:
//                   which windows appear (none: it is drawn inside the engine's window), a capture of it, Échap posted
//                   to that window (no effect without the focus), then a click on its « Cancel »: what the page gets.
//   hang-again      frozen for good; after the first report, new DevTools input: is the page reported again (what
//                   « Attendre » waits for)?
// The process is per-monitor DPI aware (WebView2 158 refuses a controller in a process that is not: 0x8007139F).
#ifndef UNICODE
#define UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <WebView2.h>
#include <cstdio>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include "crash-page.h"

namespace {
template<class T> const wchar_t* GuidText();
#define GUID_TEXT(T,S) template<> const wchar_t* GuidText<T>() { return L##S; }
GUID_TEXT(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler,"{4e8a3389-c9d8-4bd2-b6b5-124fee6cc14d}")
GUID_TEXT(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler,"{6c4819f3-c9b7-4260-8127-c9f5bde7f68c}")
GUID_TEXT(ICoreWebView2NavigationCompletedEventHandler,"{d33a35bf-1c49-4f98-93ab-006e0533fe1c}")
GUID_TEXT(ICoreWebView2NavigationStartingEventHandler,"{9adbe429-f36d-432b-9ddc-f8881fbd76e3}")
GUID_TEXT(ICoreWebView2ExecuteScriptCompletedHandler,"{49511172-cc67-4bca-9923-137112f4c4cc}")
GUID_TEXT(ICoreWebView2CallDevToolsProtocolMethodCompletedHandler,"{5c4889f0-5ef6-4c5a-952c-d8f1b92d0574}")
GUID_TEXT(ICoreWebView2ProcessFailedEventHandler,"{79e0aea4-990b-42d9-aa1d-0fcc2e5bc7f1}")
GUID_TEXT(ICoreWebView2ScreenCaptureStartingEventHandler,"{e24ff05a-1db5-59d9-89f3-3c864268db4a}")
GUID_TEXT(ICoreWebView2PermissionRequestedEventHandler,"{15e1c6a3-c72a-4df3-91d7-d097fbec6bfd}")
GUID_TEXT(ICoreWebView2GetProcessExtendedInfosCompletedHandler,"{f45e55aa-3bc2-11ee-be56-0242ac120002}")

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

std::wstring url, url2, udf, mode, fonts, shot;
ICoreWebView2Environment* environment = nullptr;
ICoreWebView2Controller* controller = nullptr;
ICoreWebView2* view = nullptr;
bool finished = false;
std::function<void(bool)> onCompleted;   // the next NavigationCompleted (success flag)

std::string Utf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0'); WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr); return s;
}
void Say(const std::wstring& s) { printf("%s\n", Utf8(s).c_str()); fflush(stdout); }
void Finish() { if (!finished) { finished = true; Say(L"DONE"); PostQuitMessage(0); } }
// One-shot timers carrying a std::function (thread timers: no window needed).
std::function<void()>& Pending(UINT_PTR id) { static std::function<void()> slots[256]; return slots[id % 256]; }
void After(UINT ms, std::function<void()> f) {
    const UINT_PTR id = SetTimer(nullptr, 0, ms, [](HWND, UINT, UINT_PTR i, DWORD) { KillTimer(nullptr, i); auto g = std::move(Pending(i)); Pending(i) = nullptr; if (g && !finished) g(); });
    Pending(id) = std::move(f);
}
std::wstring Short(const std::wstring& s) { return s.size() > 72 ? s.substr(0, 72) + L"…" : s; }
std::wstring Source() { LPWSTR s = nullptr; std::wstring r; if (SUCCEEDED(view->get_Source(&s)) && s) { r = s; CoTaskMemFree(s); } return r; }
bool CanGoBack() { BOOL b = FALSE; view->get_CanGoBack(&b); return b != FALSE; }
bool CanGoForward() { BOOL b = FALSE; view->get_CanGoForward(&b); return b != FALSE; }
// A script with a deadline: `then` runs once, with the answer or « <no answer within N ms> ».
void Script(const std::wstring& js, std::function<void(const std::wstring&)> then, UINT deadline = 4000) {
    auto once = std::make_shared<std::function<void(const std::wstring&)>>(std::move(then));
    auto h = Callback<ICoreWebView2ExecuteScriptCompletedHandler, HRESULT, LPCWSTR>([once](HRESULT hr, LPCWSTR r) -> HRESULT { auto f = std::move(*once); *once = nullptr; if (f) f(FAILED(hr) ? L"<script failed>" : r ? r : L"null"); return S_OK; });
    if (FAILED(view->ExecuteScript(js.c_str(), h))) { auto f = std::move(*once); *once = nullptr; if (f) f(L"<script refused>"); }
    h->Release();
    After(deadline, [once, deadline] { auto f = std::move(*once); *once = nullptr; if (f) f(L"<no answer within " + std::to_wstring(deadline) + L" ms>"); });
}
void DevTools(const wchar_t* method, const std::wstring& params, std::function<void(HRESULT, const std::wstring&)> then, UINT deadline = 4000) {
    auto once = std::make_shared<std::function<void(HRESULT, const std::wstring&)>>(std::move(then));
    auto h = Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler, HRESULT, LPCWSTR>([once](HRESULT hr, LPCWSTR r) -> HRESULT { auto f = std::move(*once); *once = nullptr; if (f) f(hr, r ? r : L""); return S_OK; });
    if (FAILED(view->CallDevToolsProtocolMethod(method, params.c_str(), h))) { auto f = std::move(*once); *once = nullptr; if (f) f(E_FAIL, L""); }
    h->Release();
    if (deadline) After(deadline, [once] { auto f = std::move(*once); *once = nullptr; if (f) f(E_PENDING, L"<no answer>"); });
}
void Navigate(const std::wstring& u, std::function<void(bool)> then) { onCompleted = std::move(then); view->Navigate(u.c_str()); }
void History(const std::wstring& label, std::function<void()> then) {
    DevTools(L"Page.getNavigationHistory", L"{}", [label, then](HRESULT hr, const std::wstring& r) {
        // Only the shape: index and the entries' urls (data: urls shortened).
        std::wstring out = FAILED(hr) ? std::wstring(L"<unavailable>") : r;
        if (SUCCEEDED(hr)) {
            std::wstring compact; size_t at = out.find(L"\"currentIndex\":"); if (at != std::wstring::npos) compact = L"index=" + out.substr(at + 15, out.find(L',', at) - at - 15);
            for (size_t u = out.find(L"\"url\":\""); u != std::wstring::npos; u = out.find(L"\"url\":\"", u + 7)) { const size_t e = out.find(L'"', u + 7); compact += L" | " + Short(out.substr(u + 7, e - u - 7)); }
            out = compact;
        }
        Say(L"FACT " + label + L".history " + out); if (then) then(); });
}
void State(const std::wstring& label, std::function<void()> then) {
    Say(L"FACT " + label + L".source " + Short(Source()));
    Say(std::wstring(L"FACT ") + label + L".nav back=" + (CanGoBack() ? L"yes" : L"no") + L" forward=" + (CanGoForward() ? L"yes" : L"no"));
    Script(L"({href:location.href.slice(0,60),title:document.title,h1:(document.querySelector('h1')||{}).textContent})", [label, then](const std::wstring& r) { Say(L"FACT " + label + L".document " + r); History(label, then); });
}
std::wstring DataUri(const std::wstring& path) {
    std::ifstream in(path.c_str(), std::ios::binary); if (!in) return L"";
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    static const char* digits = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::wstring r = L"data:font/woff2;base64,";
    for (size_t i = 0; i < bytes.size(); i += 3) {
        const unsigned n = ((unsigned char)bytes[i] << 16) | (i + 1 < bytes.size() ? (unsigned char)bytes[i + 1] << 8 : 0) | (i + 2 < bytes.size() ? (unsigned char)bytes[i + 2] : 0);
        r += (wchar_t)digits[(n >> 18) & 63]; r += (wchar_t)digits[(n >> 12) & 63]; r += i + 1 < bytes.size() ? (wchar_t)digits[(n >> 6) & 63] : L'='; r += i + 2 < bytes.size() ? (wchar_t)digits[n & 63] : L'=';
    }
    return r;
}
// Our real page, as the browser builds it (fonts from --fonts).
std::wstring OurPage(const std::wstring& address = url2) {
    NativeChrome::CrashPage::Extras x;
    if (!fonts.empty()) { x.fontRegular = DataUri(fonts + L"\\InterDisplay-Regular.woff2"); x.fontMedium = DataUri(fonts + L"\\InterDisplay-Medium.woff2"); x.fontSemiBold = DataUri(fonts + L"\\InterDisplay-SemiBold.woff2"); }
    x.reducedMotion = true;   // the capture shows the settled page
    const std::wstring html = NativeChrome::CrashPage::Html(address, L"{probe-token}", x);
    Say(L"FACT ours.size chars=" + std::to_wstring(html.size()) + L" utf8=" + std::to_wstring(Utf8(html).size()));
    return html;
}
std::string Base64Decode(const std::wstring& s) {
    auto v = [](wchar_t c) -> int { if (c >= L'A' && c <= L'Z') return c - L'A'; if (c >= L'a' && c <= L'z') return c - L'a' + 26; if (c >= L'0' && c <= L'9') return c - L'0' + 52; if (c == L'+') return 62; if (c == L'/') return 63; return -1; };
    std::string out; unsigned acc = 0; int bits = 0;
    for (wchar_t c : s) { const int d = v(c); if (d < 0) continue; acc = (acc << 6) | d; bits += 6; if (bits >= 8) { bits -= 8; out += (char)((acc >> bits) & 255); } }
    return out;
}
void Capture(std::function<void()> then) {
    if (shot.empty()) { then(); return; }
    DevTools(L"Page.captureScreenshot", L"{\"format\":\"png\"}", [then](HRESULT hr, const std::wstring& r) {
        const size_t at = r.find(L"\"data\":\"");
        if (FAILED(hr) || at == std::wstring::npos) { Say(L"FACT ours.capture failed hr=" + std::to_wstring((long)hr)); then(); return; }
        const std::string png = Base64Decode(r.substr(at + 8, r.find(L'"', at + 8) - at - 8));
        std::ofstream(shot.c_str(), std::ios::binary).write(png.data(), (std::streamsize)png.size());
        Say(L"FACT ours.capture bytes=" + std::to_wstring(png.size())); then(); }, 10000);
}

// ---- crash modes -------------------------------------------------------------------------------------------------
bool crashed = false;
void KillRenderers() {
    ICoreWebView2Environment8* e8 = nullptr; ICoreWebView2ProcessInfoCollection* list = nullptr; UINT n = 0, killed = 0;
    if (FAILED(environment->QueryInterface(IID_ICoreWebView2Environment8, (void**)&e8)) || !e8) { Say(L"FACT kill <no Environment8>"); return; }
    if (SUCCEEDED(e8->GetProcessInfos(&list)) && list && SUCCEEDED(list->get_Count(&n))) {
        for (UINT i = 0; i < n; ++i) {
            ICoreWebView2ProcessInfo* p = nullptr; COREWEBVIEW2_PROCESS_KIND kind{}; INT32 pid = 0;
            if (SUCCEEDED(list->GetValueAtIndex(i, &p)) && p && SUCCEEDED(p->get_Kind(&kind)) && kind == COREWEBVIEW2_PROCESS_KIND_RENDERER && SUCCEEDED(p->get_ProcessId(&pid))) {
                HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, (DWORD)pid);
                if (h) { if (TerminateProcess(h, 1)) ++killed; CloseHandle(h); }
            }
            if (p) p->Release();
        }
    }
    if (list) list->Release();
    e8->Release();
    Say(L"FACT kill renderers=" + std::to_wstring(killed));
}
void Crash() { if (mode == L"crash-kill") KillRenderers(); else DevTools(L"Page.crash", L"{}", nullptr, 0); }
void AfterOurs() {
    State(L"ours", [] { Capture([] {
        // A plain Reload() of our page: what comes back?
        onCompleted = [](bool ok) {
            Say(std::wstring(L"FACT ours.reload ") + (ok ? L"ok" : L"failed"));
            State(L"ours.reload", [] {
                // « Recharger » as GoBack(): back to the crashed entry (B), our entry left in front.
                onCompleted = [](bool ok2) {
                    Say(std::wstring(L"FACT ours.recharger ") + (ok2 ? L"ok" : L"failed"));
                    State(L"ours.recharger", [] {
                        // Forward again: our page comes back from history (what NavigationStarting reports).
                        onCompleted = [](bool ok3) {
                            Say(std::wstring(L"FACT ours.forward ") + (ok3 ? L"ok" : L"failed"));
                            State(L"ours.forward", [] {
                                onCompleted = [](bool ok4) {
                                    Say(std::wstring(L"FACT ours.back2 ") + (ok4 ? L"ok" : L"failed"));
                                    // Then a link from B: our entry in front is pruned.
                                    State(L"ours.back2", [] { Navigate(url, [](bool ok5) { Say(std::wstring(L"FACT ours.next ") + (ok5 ? L"ok" : L"failed")); State(L"ours.next", [] { Finish(); }); }); });
                                };
                                Say(L"FACT ours.back2 hr=" + std::to_wstring((long)view->GoBack()));
                            });
                        };
                        Say(L"FACT ours.forward hr=" + std::to_wstring((long)view->GoForward()));
                    });
                };
                Say(L"FACT ours.recharger hr=" + std::to_wstring((long)view->GoBack()));
            });
        };
        Say(L"FACT ours.reload hr=" + std::to_wstring((long)view->Reload()));
    }); });
}
// ---- frozen page (#121 « Page qui ne répond plus »): cadence, recovery, reload, the tab's own renderer ------------
ULONGLONG hangArmed = 0; int hangEvents = 0; bool hangSettled = false;
// The renderer process hosting this view's main frame (ICoreWebView2_20 FrameId among the renderers' frames).
void TabRenderer(std::function<void(DWORD)> then) {
    UINT32 mainFrame = 0; ICoreWebView2_20* v20 = nullptr;
    if (SUCCEEDED(view->QueryInterface(IID_ICoreWebView2_20, (void**)&v20)) && v20) { v20->get_FrameId(&mainFrame); v20->Release(); }
    ICoreWebView2Environment13* e13 = nullptr;
    if (FAILED(environment->QueryInterface(IID_ICoreWebView2Environment13, (void**)&e13)) || !e13) { Say(L"FACT renderer <no Environment13>"); then(0); return; }
    auto h = Callback<ICoreWebView2GetProcessExtendedInfosCompletedHandler, HRESULT, ICoreWebView2ProcessExtendedInfoCollection*>([mainFrame, then](HRESULT hr, ICoreWebView2ProcessExtendedInfoCollection* list) -> HRESULT {
        DWORD found = 0; UINT n = 0;
        if (SUCCEEDED(hr) && list && SUCCEEDED(list->get_Count(&n)))
            for (UINT i = 0; i < n && !found; ++i) {
                ICoreWebView2ProcessExtendedInfo* x = nullptr; ICoreWebView2ProcessInfo* p = nullptr; ICoreWebView2FrameInfoCollection* frames = nullptr; ICoreWebView2FrameInfoCollectionIterator* it = nullptr;
                if (SUCCEEDED(list->GetValueAtIndex(i, &x)) && x && SUCCEEDED(x->get_ProcessInfo(&p)) && p && SUCCEEDED(x->get_AssociatedFrameInfos(&frames)) && frames && SUCCEEDED(frames->GetIterator(&it)) && it) {
                    BOOL more = FALSE; it->get_HasCurrent(&more);
                    while (more && !found) {
                        ICoreWebView2FrameInfo* f = nullptr; ICoreWebView2FrameInfo2* f2 = nullptr; UINT32 id = 0;
                        if (SUCCEEDED(it->GetCurrent(&f)) && f && SUCCEEDED(f->QueryInterface(IID_ICoreWebView2FrameInfo2, (void**)&f2)) && f2 && SUCCEEDED(f2->get_FrameId(&id)) && id == mainFrame) { INT32 pid = 0; p->get_ProcessId(&pid); found = (DWORD)pid; }
                        if (f2) f2->Release();
                        if (f) f->Release();
                        if (FAILED(it->MoveNext(&more))) more = FALSE;
                    }
                }
                if (it) it->Release();
                if (frames) frames->Release();
                if (p) p->Release();
                if (x) x->Release();
            }
        Say(L"FACT renderer main frame " + std::to_wstring(mainFrame) + L" -> pid " + std::to_wstring(found));
        then(found); return S_OK; });
    if (FAILED(e13->GetProcessExtendedInfos(h))) then(0);
    h->Release(); e13->Release();
}
void HangUnresponsive() {
    ++hangEvents;
    const ULONGLONG at = GetTickCount64() - hangArmed;
    Say(L"FACT hang.unresponsive #" + std::to_wstring(hangEvents) + L" at " + std::to_wstring(at) + L" ms");
    if (mode == L"hang-again" && hangEvents == 1)   // « Attendre »: the window closed; new input 3 s later
        After(3000, [] { Say(L"FACT hang.again new input"); for (int i = 0; i < 3; ++i) DevTools(L"Input.dispatchKeyEvent", L"{\"type\":\"keyDown\",\"key\":\"b\",\"text\":\"b\"}", nullptr, 0); });
    if (hangEvents != 1) return;
    if (mode == L"hang-recover") {   // a script on a frozen page answers only once it runs again: the recovery signal
        Script(L"1", [](const std::wstring& r) { Say(L"FACT hang.script answered " + r + L" at " + std::to_wstring(GetTickCount64() - hangArmed) + L" ms (events so far " + std::to_wstring(hangEvents) + L")");
            hangSettled = true; After(10000, [] { Say(L"FACT hang.events after recovery: total " + std::to_wstring(hangEvents)); Finish(); }); }, 60000);
        return;
    }
    if (mode == L"hang-reload") {   // « Recharger » on a frozen page: does Reload() alone go through?
        const ULONGLONG t0 = GetTickCount64();
        onCompleted = [t0](bool ok) { Say(std::wstring(L"FACT hang.reload ") + (ok ? L"ok" : L"failed") + L" after " + std::to_wstring(GetTickCount64() - t0) + L" ms"); hangSettled = true; State(L"hang.reload", [] { Finish(); }); };
        Say(L"FACT hang.reload hr=" + std::to_wstring((long)view->Reload()));
        After(12000, [] {
            if (hangSettled) return;
            Say(L"FACT hang.reload no NavigationCompleted within 12 s: ending the tab's renderer");
            TabRenderer([](DWORD pid) {
                if (!pid) { Finish(); return; }
                HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid); if (h) { Say(L"FACT hang.kill " + std::to_wstring(TerminateProcess(h, 1))); CloseHandle(h); }
            });
        });
    }
}
void OnProcessFailed(COREWEBVIEW2_PROCESS_FAILED_KIND kind, COREWEBVIEW2_PROCESS_FAILED_REASON reason, int exitCode) {
    Say(L"FACT failed kind=" + std::to_wstring((int)kind) + L" reason=" + std::to_wstring((int)reason) + L" exit=" + std::to_wstring(exitCode));
    if (mode == L"hang-recover" || mode == L"hang-reload" || mode == L"hang-cadence" || mode == L"hang-again") {
        if (kind == COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_UNRESPONSIVE) HangUnresponsive();
        else if (kind == COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED && mode == L"hang-reload") {
            const ULONGLONG t0 = GetTickCount64();
            onCompleted = [t0](bool ok) { Say(std::wstring(L"FACT hang.reloadAfterKill ") + (ok ? L"ok" : L"failed") + L" after " + std::to_wstring(GetTickCount64() - t0) + L" ms"); hangSettled = true; State(L"hang.afterKill", [] { Finish(); }); };
            Say(L"FACT hang.reloadAfterKill hr=" + std::to_wstring((long)view->Reload()));
        }
        return;
    }
    if (mode == L"hang") {
        if (kind != COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_UNRESPONSIVE || crashed) return;
        crashed = true;
        const ULONGLONG t0 = GetTickCount64();
        onCompleted = [t0](bool ok) {
            Say(std::wstring(L"FACT hang.ours ") + (ok ? L"shown" : L"failed") + L" after " + std::to_wstring(GetTickCount64() - t0) + L" ms");
            State(L"hang.ours", [] { Finish(); });
        };
        Say(std::wstring(L"FACT hang.navigateToString hr=") + std::to_wstring((long)view->NavigateToString(L"<!doctype html><title>ours</title>ours")));
        After(15000, [] { Say(L"FACT hang.ours no NavigationCompleted within 15 s"); Finish(); });
        return;
    }
    if (kind != COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED) return;
    // crash-twice: our page is on screen and ITS renderer dies too (night 24/11, a background tab crashed again).
    // Reload() of our entry, as src/elements-integration.h ShowCrashPage now does: our page must come back, not B.
    static int exits = 0;
    if (mode == L"crash-twice" && ++exits == 2) {
        onCompleted = [](bool ok) { Say(std::wstring(L"FACT twice.reload ") + (ok ? L"ok" : L"failed")); State(L"twice", [] { Finish(); }); };
        Say(L"FACT twice.reload hr=" + std::to_wstring((long)view->Reload()));
        return;
    }
    if (crashed) return;
    crashed = true;
    if (mode == L"crash-twice") {
        onCompleted = [](bool ok) { Say(std::wstring(L"FACT ours.shown ") + (ok ? L"ok" : L"failed")); State(L"ours", [] { KillRenderers(); }); };
        Say(L"FACT ours.navigateToString hr=" + std::to_wstring((long)view->NavigateToString(OurPage().c_str())));
        return;
    }
    if (mode == L"crash-builtin" || mode == L"crash-off") {
        After(1500, [] {
            Say(L"FACT engine.source " + Short(Source()));
            Say(std::wstring(L"FACT engine.nav back=") + (CanGoBack() ? L"yes" : L"no") + L" forward=" + (CanGoForward() ? L"yes" : L"no"));
            Script(L"location.href", [](const std::wstring& r) {
                Say(L"FACT engine.script " + r);
                DevTools(L"Page.getFrameTree", L"{}", [](HRESULT hr, const std::wstring& r2) {
                    Say(L"FACT engine.frameTree hr=" + std::to_wstring((long)hr) + L" " + Short(r2));
                    onCompleted = [](bool ok) { Say(std::wstring(L"FACT engine.reload ") + (ok ? L"ok" : L"failed")); State(L"engine.after", [] { Finish(); }); };
                    Say(L"FACT engine.reload hr=" + std::to_wstring((long)view->Reload()));
                    After(15000, [] { Say(L"FACT engine.reload no NavigationCompleted within 15 s"); Finish(); });
                });
            }, 3000);
        });
        return;
    }
    // crash-ours / crash-kill: what src/main.cpp does, at once in the handler.
    onCompleted = [](bool ok) { Say(std::wstring(L"FACT ours.shown ") + (ok ? L"ok" : L"failed")); AfterOurs(); };
    Say(L"FACT ours.navigateToString hr=" + std::to_wstring((long)view->NavigateToString(OurPage().c_str())));
}

// ---- share mode ---------------------------------------------------------------------------------------------------
int shareEvents = 0;
const wchar_t* GetDisplay = L"navigator.mediaDevices.getDisplayMedia({video:true}).then(s=>{s.getTracks().forEach(t=>t.stop());return 'granted'},e=>e.name+': '+e.message)";
void Evaluate(const std::wstring& label, const std::wstring& expr, bool gesture, std::function<void()> then) {
    const ULONGLONG t0 = GetTickCount64();
    std::wstring quoted; for (wchar_t c : expr) { if (c == L'"' || c == L'\\') quoted += L'\\'; quoted += c; }
    DevTools(L"Runtime.evaluate", L"{\"expression\":\"" + quoted + L"\",\"awaitPromise\":true,\"userGesture\":" + (gesture ? L"true" : L"false") + L"}",
             [label, t0, then](HRESULT hr, const std::wstring& r) {
                 Say(L"FACT " + label + L" hr=" + std::to_wstring((long)hr) + L" after " + std::to_wstring(GetTickCount64() - t0) + L" ms " + r + L" events=" + std::to_wstring(shareEvents));
                 then(); }, 10000);
}
void Share() {
    ICoreWebView2_27* v27 = nullptr;
    if (FAILED(view->QueryInterface(IID_ICoreWebView2_27, (void**)&v27)) || !v27) { Say(L"FACT share.api unavailable (no ICoreWebView2_27)"); Finish(); return; }
    Say(L"FACT share.api ICoreWebView2_27 available");
    EventRegistrationToken token{};
    auto h = Callback<ICoreWebView2ScreenCaptureStartingEventHandler, ICoreWebView2*, ICoreWebView2ScreenCaptureStartingEventArgs*>(
        [](ICoreWebView2*, ICoreWebView2ScreenCaptureStartingEventArgs* a) -> HRESULT {
            ++shareEvents;
            std::wstring source = L"?";
            ICoreWebView2FrameInfo* f = nullptr; LPWSTR s = nullptr;
            if (SUCCEEDED(a->get_OriginalSourceFrameInfo(&f)) && f) { if (SUCCEEDED(f->get_Source(&s)) && s) { source = s; CoTaskMemFree(s); } f->Release(); }
            BOOL cancel = TRUE, handled = TRUE; a->get_Cancel(&cancel); a->get_Handled(&handled);
            Say(L"FACT share.event source=" + source + L" defaultCancel=" + std::to_wstring(cancel) + L" defaultHandled=" + std::to_wstring(handled));
            ICoreWebView2Deferral* d = nullptr;
            a->put_Handled(TRUE);
            if (FAILED(a->GetDeferral(&d)) || !d) { a->put_Cancel(TRUE); Say(L"FACT share.deferral unavailable"); return S_OK; }
            a->AddRef();
            After(600, [a, d] { a->put_Cancel(TRUE); d->Complete(); d->Release(); a->Release(); });   // « Bloquer »
            return S_OK; });
    v27->add_ScreenCaptureStarting(h, &token); h->Release(); v27->Release();
    auto p = Callback<ICoreWebView2PermissionRequestedEventHandler, ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs*>(
        [](ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs* a) -> HRESULT {
            COREWEBVIEW2_PERMISSION_KIND k{}; a->get_PermissionKind(&k); Say(L"FACT share.permissionRequested kind=" + std::to_wstring((int)k));
            a->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY); return S_OK; });
    view->add_PermissionRequested(p, &token); p->Release();
    Evaluate(L"share.page.gesture", GetDisplay, true, [] {
        Evaluate(L"share.page.again", GetDisplay, true, [] {
            Evaluate(L"share.page.nogesture", GetDisplay, false, [] {
                Evaluate(L"share.frame.gesture", L"new Promise(r=>{addEventListener('message',e=>r(String(e.data)),{once:true});document.querySelector('iframe').contentWindow.postMessage('share','*')})", true, [] { Finish(); });
            });
        });
    });
}

// ---- hang mode ----------------------------------------------------------------------------------------------------
void Hang() {
    Script(L"setTimeout(()=>{for(;;){}},100);'armed'", [](const std::wstring& r) {
        Say(L"FACT hang.armed " + r);
        After(600, [] {   // input the hung page cannot answer: the engine's hang monitor needs an unanswered event
            for (int i = 0; i < 3; ++i) DevTools(L"Input.dispatchKeyEvent", L"{\"type\":\"keyDown\",\"key\":\"a\",\"text\":\"a\"}", nullptr, 0);
            After(45000, [] { if (!crashed) Say(L"FACT hang.unresponsive none within 45 s"); Finish(); });
        });
    });
}
// hang-recover: frozen 30 s, then running again; hang-reload: frozen for good.
void Hang2() {
    const std::wstring js = mode == L"hang-recover" ? L"setTimeout(()=>{const end=Date.now()+30000;while(Date.now()<end){}},100);'armed'" : L"setTimeout(()=>{for(;;){}},100);'armed'";
    Script(js, [](const std::wstring& r) {
        hangArmed = GetTickCount64(); Say(L"FACT hang.armed " + r);
        After(600, [] {
            for (int i = 0; i < 3; ++i) DevTools(L"Input.dispatchKeyEvent", L"{\"type\":\"keyDown\",\"key\":\"a\",\"text\":\"a\"}", nullptr, 0);
            After(70000, [] { if (!hangSettled) Say(L"FACT hang.unsettled after 70 s, events " + std::to_wstring(hangEvents)); Finish(); });
        });
    });
}

// ---- picker mode ----------------------------------------------------------------------------------------------------
HWND probeWindow = nullptr;
std::wstring ClassOf(HWND h) { wchar_t c[128]{}; GetClassNameW(h, c, 128); return c; }
// Visible top-level windows of other processes on this desktop (the engine's), as « [class pid WxH] ».
std::wstring EngineWindows() {
    struct Ctx { std::wstring out; DWORD self; } ctx{L"", GetCurrentProcessId()};
    EnumWindows([](HWND h, LPARAM p) -> BOOL {
        auto& c = *reinterpret_cast<Ctx*>(p); DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
        if (pid != c.self && IsWindowVisible(h)) {
            wchar_t t[96]{}; GetWindowTextW(h, t, 96); RECT r{}; GetWindowRect(h, &r);
            c.out += L" [" + ClassOf(h) + L" pid " + std::to_wstring(pid) + L" '" + t + L"' " + std::to_wstring(r.right - r.left) + L"x" + std::to_wstring(r.bottom - r.top) + L"]";
        }
        return TRUE; }, reinterpret_cast<LPARAM>(&ctx));
    return ctx.out.empty() ? L" none" : ctx.out;
}
// Descendants of our window, with their process (ours or the engine's).
std::wstring EngineChildren() {
    std::wstring out;
    EnumChildWindows(probeWindow, [](HWND c, LPARAM p) -> BOOL {
        DWORD pid = 0; GetWindowThreadProcessId(c, &pid); RECT r{}; GetWindowRect(c, &r);
        *reinterpret_cast<std::wstring*>(p) += L" [" + ClassOf(c) + L" pid " + std::to_wstring(pid) + (pid == GetCurrentProcessId() ? L" (ours)" : L"") + L" "
            + std::to_wstring(r.right - r.left) + L"x" + std::to_wstring(r.bottom - r.top) + (IsWindowVisible(c) ? L"" : L" hidden") + L"]";
        return TRUE; }, reinterpret_cast<LPARAM>(&out));
    return out.empty() ? L" none" : out;
}
// The engine's own window inside ours (its Views root): a dialog the engine draws inside it gets that window's keys.
HWND EngineRoot() {
    HWND found = nullptr;
    EnumChildWindows(probeWindow, [](HWND c, LPARAM p) -> BOOL {
        DWORD pid = 0; GetWindowThreadProcessId(c, &pid);
        if (pid != GetCurrentProcessId() && ClassOf(c).rfind(L"Chrome_WidgetWin", 0) == 0) { *reinterpret_cast<HWND*>(p) = c; return FALSE; }
        return TRUE; }, reinterpret_cast<LPARAM>(&found));
    return found;
}
// The window holding the keyboard in the engine's UI thread.
HWND EngineKeyboard() {
    HWND root = EngineRoot(); if (!root) return nullptr;
    GUITHREADINFO g{}; g.cbSize = sizeof(g);
    if (!GetGUIThreadInfo(GetWindowThreadProcessId(root, nullptr), &g)) return nullptr;
    return g.hwndFocus ? g.hwndFocus : g.hwndActive;
}
// PrintWindow of our window (with the engine's children) into a 32-bit BMP next to --shot: what the picker looks like.
void Snapshot(const std::wstring& name) {
    if (shot.empty()) return;
    RECT r{}; GetWindowRect(probeWindow, &r); const int w = r.right - r.left, h = r.bottom - r.top;
    BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(bi.bmiHeader); bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr; HDC screen = GetDC(nullptr); HDC dc = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0); HGDIOBJ old = SelectObject(dc, bmp);
    const BOOL ok = PrintWindow(probeWindow, dc, 2 /* PW_RENDERFULLCONTENT */);
    const std::wstring path = shot.substr(0, shot.find_last_of(L"\\/") + 1) + name;
    std::ofstream f(path.c_str(), std::ios::binary);
    BITMAPFILEHEADER fh{}; fh.bfType = 0x4D42; fh.bfOffBits = sizeof(fh) + sizeof(bi.bmiHeader); fh.bfSize = fh.bfOffBits + w * h * 4;
    f.write(reinterpret_cast<const char*>(&fh), sizeof(fh)); f.write(reinterpret_cast<const char*>(&bi.bmiHeader), sizeof(bi.bmiHeader)); f.write(static_cast<const char*>(bits), w * h * 4);
    SelectObject(dc, old); DeleteObject(bmp); DeleteDC(dc); ReleaseDC(nullptr, screen);
    Say(L"FACT picker.snapshot " + name + (ok ? L"" : L" (PrintWindow failed)"));
}
void Picker() {
    ICoreWebView2_27* v27 = nullptr;
    if (FAILED(view->QueryInterface(IID_ICoreWebView2_27, (void**)&v27)) || !v27) { Say(L"FACT picker.api unavailable"); Finish(); return; }
    EventRegistrationToken token{};
    auto h = Callback<ICoreWebView2ScreenCaptureStartingEventHandler, ICoreWebView2*, ICoreWebView2ScreenCaptureStartingEventArgs*>(
        [](ICoreWebView2*, ICoreWebView2ScreenCaptureStartingEventArgs* a) -> HRESULT {
            ++shareEvents; a->put_Cancel(TRUE);   // as Browser: refused unless « Choisir… »
            ICoreWebView2Deferral* d = nullptr;
            if (FAILED(a->GetDeferral(&d)) || !d) { Say(L"FACT picker.deferral unavailable"); return S_OK; }
            a->AddRef();
            After(400, [a, d] { a->put_Cancel(FALSE); d->Complete(); d->Release(); a->Release(); Say(L"FACT picker.choose (Cancel FALSE, deferral completed)"); });
            return S_OK; });
    v27->add_ScreenCaptureStarting(h, &token); h->Release(); v27->Release();
    Say(L"FACT picker.before windows:" + EngineWindows());
    After(3500, [] {
        Say(L"FACT picker.open windows:" + EngineWindows());
        Say(L"FACT picker.open children:" + EngineChildren());
        Snapshot(L"picker-open.bmp");
        HWND k = EngineKeyboard(); DWORD pid = 0; if (k) GetWindowThreadProcessId(k, &pid);
        Say(L"FACT picker.keyboard " + (k ? ClassOf(k) + L" pid " + std::to_wstring(pid) : std::wstring(L"none (no window of this desktop is active)")));
        HWND target = k && pid != GetCurrentProcessId() ? k : EngineRoot();
        if (target) { PostMessageW(target, WM_KEYDOWN, VK_ESCAPE, 0x00010001); PostMessageW(target, WM_KEYUP, VK_ESCAPE, 0xC0010001); Say(L"FACT picker.escape posted to the engine's window " + ClassOf(target)); }
        After(2500, [] {
            Say(L"FACT picker.after-escape windows:" + EngineWindows()); Snapshot(L"picker-after-escape.bmp");
            // The picker is centred at the top of the page (1172 x 730 here and in the night QA): its « Cancel » button
            // at 908 x 523 of the engine's window. A click posted there, as a user's.
            if (HWND root = EngineRoot()) {
                const LPARAM at = MAKELPARAM(908, 523);
                PostMessageW(root, WM_MOUSEMOVE, 0, at); PostMessageW(root, WM_LBUTTONDOWN, MK_LBUTTON, at); PostMessageW(root, WM_LBUTTONUP, 0, at);
                Say(L"FACT picker.click posted on « Cancel » (908, 523) of the engine's window");
            }
            After(2500, [] { Snapshot(L"picker-after-click.bmp"); });
        });
    });
    const ULONGLONG t0 = GetTickCount64();
    std::wstring quoted; for (wchar_t c : std::wstring(GetDisplay)) { if (c == L'"' || c == L'\\') quoted += L'\\'; quoted += c; }
    DevTools(L"Runtime.evaluate", L"{\"expression\":\"" + quoted + L"\",\"awaitPromise\":true,\"userGesture\":true}", [t0](HRESULT hr, const std::wstring& r) {
        Say(L"FACT picker.page hr=" + std::to_wstring((long)hr) + L" after " + std::to_wstring(GetTickCount64() - t0) + L" ms " + r + L" events=" + std::to_wstring(shareEvents));
        After(500, [] { Finish(); }); }, 20000);
}
// hang-again: the first report, then new input 3 s later (HangUnresponsive): reported again?
void HangAgain() {
    Script(L"setTimeout(()=>{for(;;){}},100);'armed'", [](const std::wstring& r) {
        hangArmed = GetTickCount64(); Say(L"FACT hang.armed " + r);
        After(600, [] {
            for (int i = 0; i < 3; ++i) DevTools(L"Input.dispatchKeyEvent", L"{\"type\":\"keyDown\",\"key\":\"a\",\"text\":\"a\"}", nullptr, 0);
            After(80000, [] { Say(L"FACT hang.again total reports " + std::to_wstring(hangEvents)); Finish(); });
        });
    });
}

// ---- limit mode: the largest document NavigateToString accepts (ASCII; the result is the same for any page) ------
void Limit() {
    size_t ok = 0, refused = 0;
    for (size_t chars : {1000000u, 1100000u, 1200000u, 1300000u, 1400000u, 1500000u, 1550000u, 1572000u, 1573000u, 1600000u, 2000000u}) {
        const std::wstring html = L"<!doctype html><title>x</title><!--" + std::wstring(chars - 40, L'a') + L"-->";
        const HRESULT hr = view->NavigateToString(html.c_str());
        Say(L"FACT limit " + std::to_wstring(html.size()) + L" chars hr=" + std::to_wstring((long)hr));
        if (SUCCEEDED(hr)) ok = html.size(); else if (!refused) refused = html.size();
    }
    Say(L"FACT limit accepted up to " + std::to_wstring(ok) + L" chars, refused from " + std::to_wstring(refused));
    Finish();
}

void OnView() {
    EventRegistrationToken token{};
    if (mode == L"limit") { Limit(); return; }
    auto starting = Callback<ICoreWebView2NavigationStartingEventHandler, ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs*>(
        [](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* a) -> HRESULT {
            LPWSTR u = nullptr; std::wstring s; if (SUCCEEDED(a->get_Uri(&u)) && u) { s = u; CoTaskMemFree(u); }
            BOOL user = FALSE, redirect = FALSE; a->get_IsUserInitiated(&user); a->get_IsRedirected(&redirect);
            Say(L"FACT nav.starting " + Short(s) + L" (" + std::to_wstring(s.size()) + L" chars)"); return S_OK; });
    view->add_NavigationStarting(starting, &token); starting->Release();
    auto completed = Callback<ICoreWebView2NavigationCompletedEventHandler, ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*>(
        [](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* a) -> HRESULT {
            BOOL ok = FALSE; a->get_IsSuccess(&ok);
            auto f = std::move(onCompleted); onCompleted = nullptr;
            if (f) f(ok != FALSE); else Say(std::wstring(L"FACT unexpected.completed ") + (ok ? L"ok " : L"failed ") + Short(Source()));
            return S_OK; });
    view->add_NavigationCompleted(completed, &token); completed->Release();
    auto failed = Callback<ICoreWebView2ProcessFailedEventHandler, ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs*>(
        [](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs* a) -> HRESULT {
            COREWEBVIEW2_PROCESS_FAILED_KIND kind{}; a->get_ProcessFailedKind(&kind);
            COREWEBVIEW2_PROCESS_FAILED_REASON reason{}; int exitCode = 0;
            ICoreWebView2ProcessFailedEventArgs2* a2 = nullptr;
            if (SUCCEEDED(a->QueryInterface(IID_ICoreWebView2ProcessFailedEventArgs2, (void**)&a2)) && a2) { a2->get_Reason(&reason); a2->get_ExitCode(&exitCode); a2->Release(); }
            OnProcessFailed(kind, reason, exitCode); return S_OK; });
    view->add_ProcessFailed(failed, &token); failed->Release();
    if (mode == L"crash-off") {
        ICoreWebView2Settings* s = nullptr;
        if (SUCCEEDED(view->get_Settings(&s)) && s) { Say(L"FACT builtinErrorPage off hr=" + std::to_wstring((long)s->put_IsBuiltInErrorPageEnabled(FALSE))); s->Release(); }
    }
    if (mode == L"share") { Navigate(url, [](bool ok) { Say(std::wstring(L"FACT A ") + (ok ? L"ok" : L"failed")); After(500, [] { Share(); }); }); return; }
    if (mode == L"picker") { Navigate(url, [](bool ok) { Say(std::wstring(L"FACT A ") + (ok ? L"ok" : L"failed")); After(500, [] { Picker(); }); }); return; }
    if (mode == L"hang-again") { Navigate(url, [](bool ok) { Say(std::wstring(L"FACT A ") + (ok ? L"ok" : L"failed")); HangAgain(); }); return; }
    if (mode == L"hang") { Navigate(url, [](bool ok) { Say(std::wstring(L"FACT A ") + (ok ? L"ok" : L"failed")); Hang(); }); return; }
    if (mode == L"hang-recover" || mode == L"hang-reload" || mode == L"hang-cadence") { Navigate(url, [](bool ok) { Say(std::wstring(L"FACT A ") + (ok ? L"ok" : L"failed")); Hang2(); }); return; }
    if (mode == L"render") {   // the board's own address (Paper 1LK6-0 shows lemonde.fr), for the pixel comparison
        onCompleted = [](bool ok) { Say(std::wstring(L"FACT render ") + (ok ? L"ok" : L"failed")); After(800, [] { Capture([] { Finish(); }); }); };
        Say(L"FACT render.navigateToString hr=" + std::to_wstring((long)view->NavigateToString(OurPage(L"https://www.lemonde.fr/planete/").c_str())));
        return;
    }
    Navigate(url, [](bool ok) {
        Say(std::wstring(L"FACT A ") + (ok ? L"ok" : L"failed"));
        Navigate(url2, [](bool ok2) { Say(std::wstring(L"FACT B ") + (ok2 ? L"ok" : L"failed")); State(L"before", [] { Crash(); }); });
    });
}
LRESULT CALLBACK WindowProc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }
} // namespace

int wmain(int argc, wchar_t** argv) {
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::wstring k = argv[i], v = argv[i + 1];
        if (k == L"--url") url = v; else if (k == L"--url2") url2 = v; else if (k == L"--udf") udf = v; else if (k == L"--mode") mode = v;
        else if (k == L"--fonts") fonts = v; else if (k == L"--shot") shot = v;
    }
    if (url.empty() || url2.empty() || udf.empty() || mode.empty()) { Say(L"usage: --url A --url2 B --udf DIR --mode M [--fonts DIR] [--shot PNG]"); return 2; }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);   // WebView2 158: no controller otherwise (0x8007139F)
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    WNDCLASSW wc{}; wc.lpfnWndProc = WindowProc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"CrashShareProbe"; RegisterClassW(&wc);
    // Never shown: the page renders, nothing appears on screen and the focus is never taken. 1172 x 730 DIP: the
    // board's page area (Paper 1LK6-0).
    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"engine probe", WS_POPUP, -32000, -32000, 1172, 730, nullptr, nullptr, wc.hInstance, nullptr);
    probeWindow = hwnd;
    if (mode == L"picker") {
        // The engine draws its picker inside the page's window: that window must be shown for the picker to exist. Only
        // ever on a desktop that is never displayed (engine_probe.py starts this mode there): refused on the one on screen.
        wchar_t mine[256]{}, input[256]{}; HDESK in = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
        GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()), UOI_NAME, mine, sizeof(mine), nullptr);
        if (in) { GetUserObjectInformationW(in, UOI_NAME, input, sizeof(input), nullptr); CloseDesktop(in); }
        if (!in || std::wstring(mine) == input) { Say(L"FACT picker refused: this desktop (" + std::wstring(mine) + L") is the one on screen"); return 3; }
        SetWindowPos(hwnd, nullptr, 0, 0, 1172, 730, SWP_NOZORDER | SWP_NOACTIVATE); ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        Say(L"FACT picker desktop " + std::wstring(mine) + L" (never displayed): window shown there");
    }
    HMODULE loader = LoadLibraryW(L"WebView2Loader.dll");
    using CreateFn = HRESULT(STDAPICALLTYPE*)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*, ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);
    auto create = loader ? reinterpret_cast<CreateFn>(reinterpret_cast<void*>(GetProcAddress(loader, "CreateCoreWebView2EnvironmentWithOptions"))) : nullptr;
    if (!create) { Say(L"WebView2Loader.dll missing"); return 2; }
    auto created = Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT, ICoreWebView2Environment*>([hwnd](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
        if (FAILED(hr) || !env) { Say(L"ENGINE FAILED environment hr=" + std::to_wstring((long)hr)); Finish(); return S_OK; }
        environment = env; env->AddRef();
        auto made = Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT, ICoreWebView2Controller*>([hwnd](HRESULT hr2, ICoreWebView2Controller* c) -> HRESULT {
            if (FAILED(hr2) || !c) { Say(L"ENGINE FAILED controller hr=" + std::to_wstring((long)hr2)); Finish(); return S_OK; }
            controller = c; c->AddRef(); RECT r{}; GetClientRect(hwnd, &r); c->put_Bounds(r); c->put_IsVisible(TRUE);
            if (FAILED(c->get_CoreWebView2(&view)) || !view) { Say(L"ENGINE FAILED"); Finish(); return S_OK; }
            OnView(); return S_OK; });
        env->CreateCoreWebView2Controller(hwnd, made); made->Release(); return S_OK; });
    const HRESULT hr = create(nullptr, udf.c_str(), nullptr, created); created->Release();
    if (FAILED(hr)) { Say(L"ENGINE FAILED"); return 2; }
    After(80000, [] { Say(L"FACT timeout"); Finish(); });
    MSG msg; while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    if (view) view->Release();
    if (controller) { controller->Close(); controller->Release(); }
    if (environment) environment->Release();
    return 0;
}
