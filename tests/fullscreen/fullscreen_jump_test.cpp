// Fullscreen in ONE step (user report 25/11: entering or leaving a video's fullscreen, the window « jumps » several
// times before it settles; same on exit). Drives src/main.cpp's own window procedure, ApplyFullscreen, SetPageFullscreen
// and ApplyPages on a window with the browser's styles (WS_CAPTION | WS_THICKFRAME, client = whole window), windowed
// and maximized, and records:
//   * every window size change (WM_SIZE, with its show state: a show-state change is a restore / maximize animation);
//   * every page bounds or visibility change (a stand-in WebView2 controller records put_Bounds / put_IsVisible).
// A clean switch is ONE window size change with no show-state change and ONE bounds change per page on screen; leaving
// gives back the exact window rectangle and show state, again in one step. Cases: windowed / maximized x F11 / a page's
// fullscreen, a split view (the page alone), Mode Focus, the side layout (#143) with and without Mode Focus, a minimize
// during fullscreen (and leaving while minimized), Agrandir / Restaurer refused while fullscreen. The trace lines
// « FULLSCREEN step » written to the log (read by the real-window checks, tests/e2e/fullscreen_e2e.py) must agree
// with what this test counted.
// Screen-free: the window lives on a desktop of its own (CreateDesktop) that is never displayed. Shown, maximized,
// minimized and resized there, it never reaches the user's screen, mouse, keyboard or foreground. No WebView2, no
// network, no profile (smoke: nothing read or written but the log in <out>).
//   python -I tests/fullscreen/run.py      (or: fullscreen_jump_test.exe <repo root> <out folder>)
#include "../../src/main.cpp"
#include <cstdio>
#include <fstream>

static int failures = 0, checks = 0;
static void Expect(bool ok, const std::string& what) { ++checks; std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) ++failures; }
static std::string Text(const RECT& r) { char b[80]; std::snprintf(b, sizeof b, "(%ld,%ld %ldx%ld)", r.left, r.top, r.right - r.left, r.bottom - r.top); return b; }
static bool Same(const RECT& a, const RECT& b) { return EqualRect(&a, &b) != FALSE; }

// ---- what changed --------------------------------------------------------------------------------------------------
struct PageEvent { unsigned serial = 0; bool bounds = false; RECT r{}; BOOL visible = FALSE; };
static std::vector<PageEvent> pageEvents;
static std::vector<std::pair<WPARAM, RECT>> sizeEvents;   // WM_SIZE: show state, window rectangle

// A WebView2 controller stand-in: bounds and visibility are recorded, everything else answers S_OK / E_NOTIMPL.
struct FakeController final : ICoreWebView2Controller {
    LONG refs = 1; unsigned serial = 0; RECT bounds{}; BOOL visible = FALSE;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override { if (!out) return E_POINTER; if (id == IID_IUnknown) { *out = static_cast<IUnknown*>(this); AddRef(); return S_OK; } *out = nullptr; return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override { const LONG n = InterlockedDecrement(&refs); if (!n) delete this; return (ULONG)n; }
    HRESULT STDMETHODCALLTYPE get_IsVisible(BOOL* v) override { if (v) *v = visible; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_IsVisible(BOOL v) override { visible = v; PageEvent e; e.serial = serial; e.visible = v; pageEvents.push_back(e); return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Bounds(RECT* r) override { if (r) *r = bounds; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_Bounds(RECT r) override { bounds = r; PageEvent e; e.serial = serial; e.bounds = true; e.r = r; pageEvents.push_back(e); return S_OK; }
    HRESULT STDMETHODCALLTYPE get_ZoomFactor(double* z) override { if (z) *z = 1; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_ZoomFactor(double) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE add_ZoomFactorChanged(ICoreWebView2ZoomFactorChangedEventHandler*, EventRegistrationToken*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE remove_ZoomFactorChanged(EventRegistrationToken) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetBoundsAndZoomFactor(RECT r, double) override { return put_Bounds(r); }
    HRESULT STDMETHODCALLTYPE MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE add_MoveFocusRequested(ICoreWebView2MoveFocusRequestedEventHandler*, EventRegistrationToken*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE remove_MoveFocusRequested(EventRegistrationToken) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE add_GotFocus(ICoreWebView2FocusChangedEventHandler*, EventRegistrationToken*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE remove_GotFocus(EventRegistrationToken) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE add_LostFocus(ICoreWebView2FocusChangedEventHandler*, EventRegistrationToken*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE remove_LostFocus(EventRegistrationToken) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE add_AcceleratorKeyPressed(ICoreWebView2AcceleratorKeyPressedEventHandler*, EventRegistrationToken*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE remove_AcceleratorKeyPressed(EventRegistrationToken) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_ParentWindow(HWND* h) override { if (h) *h = window; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_ParentWindow(HWND) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE NotifyParentWindowPositionChanged() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Close() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_CoreWebView2(ICoreWebView2** v) override { if (v) *v = nullptr; return E_NOTIMPL; }
};

LRESULT CALLBACK TestProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_SIZE && window && h == window) { RECT r{}; GetWindowRect(h, &r); sizeEvents.push_back({w, r}); }
    return WndProc(h, m, w, l);
}
static void Pump(int ms = 150) {
    const ULONGLONG end = GetTickCount64() + ms; MSG m;
    do { while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); } Sleep(5); } while (GetTickCount64() < end);
}
static RECT WinRect() { RECT r{}; GetWindowRect(window, &r); return r; }
static MONITORINFO Monitor() { MONITORINFO mi{sizeof(mi)}; GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &mi); return mi; }
static RECT WholeClient() { const RECT m = Monitor().rcMonitor; return {0, 0, m.right - m.left, m.bottom - m.top}; }

static size_t LogLineCount() { std::ifstream in{std::filesystem::path(logFile)}; size_t n = 0; std::string s; while (std::getline(in, s)) ++n; return n; }
static std::vector<std::string> LogLinesFrom(size_t first) {
    std::ifstream in{std::filesystem::path(logFile)}; std::vector<std::string> out; std::string s; size_t i = 0;
    while (std::getline(in, s)) if (i++ >= first) out.push_back(s);
    return out;
}

struct Switch {
    std::vector<std::pair<WPARAM, RECT>> sizes; std::vector<PageEvent> pages; size_t traceWindow = 0, tracePage = 0;
    std::vector<RECT> Bounds(const std::shared_ptr<Tab>& t) const { std::vector<RECT> v; for (auto& e : pages) if (e.serial == t->serial && e.bounds) v.push_back(e.r); return v; }
    std::vector<BOOL> Shows(const std::shared_ptr<Tab>& t) const { std::vector<BOOL> v; for (auto& e : pages) if (e.serial == t->serial && !e.bounds) v.push_back(e.visible); return v; }
    bool StatesAll(WPARAM state) const { for (auto& s : sizes) if (s.first != state) return false; return true; }
    // The first change this page went through (the order matters: hidden before resized, resized before shown).
    const PageEvent* First(const std::shared_ptr<Tab>& t) const { for (auto& e : pages) if (e.serial == t->serial) return &e; return nullptr; }
    std::string Sizes() const { std::string o; for (auto& s : sizes) o += std::string(o.empty() ? "" : " → ") + (s.first == SIZE_MAXIMIZED ? "max " : s.first == SIZE_MINIMIZED ? "min " : "normal ") + Text(s.second); return o.empty() ? "none" : o; }
};
template<class F> static Switch Measure(F act) {
    Pump(60); sizeEvents.clear(); pageEvents.clear(); const size_t mark = LogLineCount();
    act(); Pump();
    Switch s; s.sizes = sizeEvents; s.pages = pageEvents;
    for (auto& line : LogLinesFrom(mark)) { if (line.find("FULLSCREEN step") == std::string::npos) continue; if (line.find(" ms window ") != std::string::npos) ++s.traceWindow; if (line.find(" ms page ") != std::string::npos) ++s.tracePage; }
    return s;
}
// One switch, in or out: one window size change at most (none when the size is already right), no show-state change,
// the window at `want`, each listed page bounds-changed once at most and settled where expected, the trace agreeing.
static void ExpectStep(const Switch& s, const std::string& what, WPARAM state, const RECT& want, bool mustMove,
                       const std::vector<std::pair<std::shared_ptr<Tab>, RECT>>& shown) {
    Expect(s.sizes.size() <= 1 && (!mustMove || s.sizes.size() == 1) && s.StatesAll(state),
           what + ": ONE window size change, show state kept (" + s.Sizes() + ")");
    Expect(Same(WinRect(), want), what + ": window " + Text(WinRect()) + " == " + Text(want));
    for (auto& [t, r] : shown) {
        const auto b = s.Bounds(t);
        Expect(b.size() <= 1 && Same(t->placed, r) && t->placedVisible == 1, what + ": page " + ::Utf8(t->title) + " resized once at most (" + std::to_string(b.size()) + "), at " + Text(t->placed) + " == " + Text(r));
    }
    Expect(s.traceWindow == s.sizes.size() && s.tracePage == s.pages.size(),
           what + ": the log's FULLSCREEN step lines match (window " + std::to_string(s.traceWindow) + "/" + std::to_string(s.sizes.size()) + ", page " + std::to_string(s.tracePage) + "/" + std::to_string(s.pages.size()) + ")");
}

static std::shared_ptr<Tab> MakeTab(const wchar_t* title) {
    auto t = std::make_shared<Tab>(); t->title = title; t->home = false; t->uri = std::wstring(L"https://") + title + L".test/"; t->revealed = true;
    auto* c = new FakeController(); c->serial = t->serial; t->controller.p = c; return t;
}
static void Relayout() { Layout(); Pump(); }

// Windowed or maximized: F11 then a page's fullscreen, in and out, each in one step.
static void BothKinds(const std::shared_ptr<Tab>& a, const std::string& where) {
    const bool zoomed = IsZoomed(window) != FALSE; const WPARAM state = zoomed ? SIZE_MAXIMIZED : SIZE_RESTORED;
    const RECT before = WinRect(), page = a->placed, whole = WholeClient(), mon = Monitor().rcMonitor;
    // A size change reaches the window as WM_SIZE only when its CLIENT changes: maximized on a desktop without a
    // taskbar (this test's own desktop) the client is already the whole monitor, only the frame overhang goes.
    const bool moves = !Same(ClientScreen(), mon);
    for (int kind = 0; kind < 2; ++kind) {
        const std::string k = where + (kind ? ", page fullscreen" : ", F11");
        auto in = Measure([&] { if (kind) { a->pageFullscreen = true; SetPageFullscreen(a, true); } else ToggleBrowserFullscreen(); });
        ExpectStep(in, k + " in", state, mon, moves, {{a, whole}});
        Expect(Same(ClientScreen(), mon) && (IsZoomed(window) != FALSE) == zoomed, k + ": the client is the whole monitor " + Text(ClientScreen()) + ", still " + (zoomed ? "maximized" : "windowed"));
        auto out = Measure([&] { if (kind) { a->pageFullscreen = false; SetPageFullscreen(a, false); } else ToggleBrowserFullscreen(); });
        ExpectStep(out, k + " out", state, before, moves, {{a, page}});
        Expect((IsZoomed(window) != FALSE) == zoomed && !InFullscreen(), k + " out: " + (zoomed ? "maximized" : "windowed") + " again");
    }
}

int wmain(int argc, wchar_t** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 3) { std::fputs("usage: fullscreen_jump_test <repo root> <out folder>\n", stderr); return 2; }
    base = argv[1]; const std::wstring out = argv[2]; std::filesystem::create_directories(out);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    // A desktop of our own, never displayed: set before this thread creates any window.
    const std::wstring deskName = L"BrowserFullscreenJump" + std::to_wstring(GetCurrentProcessId());
    HDESK desk = CreateDesktopW(deskName.c_str(), nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    if (!desk || !SetThreadDesktop(desk)) { std::fprintf(stderr, "FAIL: own desktop (%lu)\n", GetLastError()); return 1; }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); instance = GetModuleHandleW(nullptr);
    // Not a smoke run (the side layout is off in one): every file the window writes goes to <out>\data.
    dataDir = out + L"\\data"; std::filesystem::create_directories(dataDir); logFile = out + L"\\fullscreen.log"; std::filesystem::remove(logFile);
    for (auto name : NativeChrome::FontFiles) AddFontResourceExW((base + L"\\assets\\fonts\\" + name).c_str(), FR_PRIVATE, nullptr);
    paper = CreateSolidBrush(BG);
    WNDCLASSW wc{}; wc.lpfnWndProc = TestProc; wc.hInstance = instance; wc.lpszClassName = L"FullscreenJumpTest"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); RegisterClassW(&wc);
    const UINT sys = GetDpiForSystem();
    CreateWindowW(wc.lpszClassName, L"browser", WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU | WS_CLIPCHILDREN,
                  MulDiv(160, sys, 96), MulDiv(240, sys, 96), MulDiv(1180, sys, 96), MulDiv(815, sys, 96), nullptr, nullptr, instance, nullptr);
    if (!window) { std::fputs("FAIL: window\n", stderr); return 1; }
    NativeUI::ConfigureFullClientFrame(window); NativeUI::RoundNatively(window);
    if (FAILED(chrome.Init(window, base))) { std::fputs("FAIL: chrome\n", stderr); return 1; }
    motion.SetReducedMotion(true); chrome.SetMotion(&motion); UpdateFont();
    auto a = MakeTab(L"Video"), b = MakeTab(L"Notes");
    pages = {a, b}; current = 0; a->usedAt = ++useClock;
    ShowWindow(window, SW_SHOWNORMAL); Relayout();
    {   const auto m = Monitor(); std::printf("     monitor %s, work area %s, window %s, dpi %u\n", Text(m.rcMonitor).c_str(), Text(m.rcWork).c_str(), Text(WinRect()).c_str(), GetDpiForWindow(window)); }
    Expect(a->placedVisible == 1 && !IsRectEmpty(&a->placed) && b->placedVisible != 1, "setup: the active page is placed and shown, the other one hidden");

    // 1. Windowed.
    BothKinds(a, "windowed");
    // 2. Maximized: stays maximized all along (Windows' restore / maximize animations were the jumps).
    ShowWindow(window, SW_MAXIMIZE); Relayout();
    Expect(IsZoomed(window) != FALSE, "maximized for the next cases " + Text(WinRect()));
    BothKinds(a, "maximized");
    // 3. Agrandir / Restaurer while fullscreen: refused (the window would leave its monitor with no chrome).
    {   const RECT maxed = WinRect(); ToggleBrowserFullscreen(); Pump();
        SendMessageW(window, WM_SYSCOMMAND, SC_RESTORE, 0); Pump(); Command('M'); Pump();
        Expect(IsZoomed(window) && Same(WinRect(), Monitor().rcMonitor), "maximized fullscreen: Restaurer (system menu) and Agrandir/Restaurer (command) change nothing");
        // Minimized then restored from the taskbar: back over the whole monitor, still maximized.
        ShowWindow(window, SW_MINIMIZE); Pump(); ShowWindow(window, SW_RESTORE); Pump();
        Expect(IsZoomed(window) && Same(ClientScreen(), Monitor().rcMonitor) && Same(a->placed, WholeClient()), "maximized fullscreen, minimized and restored: the whole monitor again " + Text(ClientScreen()));
        // Left while minimized: restoring then brings the maximized window back, chrome and all.
        ShowWindow(window, SW_MINIMIZE); Pump(); ToggleBrowserFullscreen(); Pump();
        Expect(IsIconic(window) && !InFullscreen(), "F11 off while minimized: still minimized");
        ShowWindow(window, SW_RESTORE); Pump();
        Expect(IsZoomed(window) && Same(WinRect(), maxed) && Same(ClientScreen(), Monitor().rcWork), "restored after: maximized in the work area " + Text(ClientScreen()));
        ShowWindow(window, SW_RESTORE); Relayout();
        Expect(!IsZoomed(window), "windowed again"); const RECT windowed = WinRect();
        ToggleBrowserFullscreen(); Pump(); SendMessageW(window, WM_SYSCOMMAND, SC_MAXIMIZE, 0); Pump(); Command('M'); Pump();
        Expect(!IsZoomed(window) && Same(WinRect(), Monitor().rcMonitor), "windowed fullscreen: Agrandir changes nothing");
        ShowWindow(window, SW_MINIMIZE); Pump(); ToggleBrowserFullscreen(); Pump(); ShowWindow(window, SW_RESTORE); Pump();
        Expect(!IsZoomed(window) && Same(WinRect(), windowed), "windowed: F11 off while minimized, restored to its own rectangle " + Text(WinRect())); }
    // 4. Split view (maximized): the page that asked is alone at the whole monitor, the other panel hidden; both back.
    {   ShowWindow(window, SW_MAXIMIZE); Relayout();
        splitGroups[1] = SV::Orientation::Row; a->group = b->group = 1; b->usedAt = ++useClock; current = 0; Relayout();
        const RECT pa = a->placed, pb = b->placed, maxed = WinRect();
        Expect(a->placedVisible == 1 && b->placedVisible == 1 && !Same(pa, pb), "split setup: both panels shown " + Text(pa) + " " + Text(pb));
        auto in = Measure([&] { a->pageFullscreen = true; SetPageFullscreen(a, true); });
        ExpectStep(in, "split, page fullscreen in", SIZE_MAXIMIZED, Monitor().rcMonitor, false, {{a, WholeClient()}});
        { const auto* first = in.First(b);
          Expect(b->placedVisible == 0 && in.Shows(b).size() == 1 && in.Bounds(b).size() <= 1 && first && !first->bounds && !first->visible,
                 "split, page fullscreen in: the other panel hidden once, BEFORE any resize (" + std::to_string(in.Bounds(b).size()) + " bounds change after)"); }
        auto out = Measure([&] { a->pageFullscreen = false; SetPageFullscreen(a, false); });
        ExpectStep(out, "split, page fullscreen out", SIZE_MAXIMIZED, maxed, false, {{a, pa}, {b, pb}});
        Expect(b->placedVisible == 1 && out.Shows(b).size() == 1 && Same(b->placed, pb), "split, page fullscreen out: the other panel back, as it was");
        a->group = b->group = 0; splitGroups.clear(); Relayout(); }
    // 5. Mode Focus (maximized then windowed): the floating panel leaves and comes back, the page in one step.
    {   settings.focusMode = true; Relayout(); Expect(chrome.floating, "Mode Focus on");
        BothKinds(a, "Mode Focus, maximized"); Expect(chrome.floating, "Mode Focus back after fullscreen");
        ShowWindow(window, SW_RESTORE); Relayout(); BothKinds(a, "Mode Focus, windowed");
        settings.focusMode = false; Relayout(); }
    // 6. Side layout (#143), with and without Mode Focus.
    {   settings.tabsPosition = BrowserSettings::TabsPosition::Side; ApplyLayout(); Relayout(); Expect(chrome.sidebar, "side layout on");
        BothKinds(a, "side, windowed"); ShowWindow(window, SW_MAXIMIZE); Relayout(); BothKinds(a, "side, maximized");
        Expect(chrome.sidebar && !chrome.contentOnly, "side layout back after fullscreen");
        settings.focusMode = true; Relayout(); BothKinds(a, "side + Mode Focus, maximized"); settings.focusMode = false; Relayout(); }

    std::printf("%s: %d checks, %d failures\n", failures ? "FAIL" : "PASS", checks, failures);
    // The window and its desktop end with the process at once (no WM_DESTROY path, no static teardown of the browser's
    // globals: returning from wmain hung there, 25/11).
    std::fflush(stdout); TerminateProcess(GetCurrentProcess(), failures ? 1 : 0); return failures ? 1 : 0;
}
