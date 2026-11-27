// The update card's placement in the browser itself (Val's card, 2026-11-26): src/main.cpp's window procedure, Layout and
// its real drawers, with the card the browser creates (updateToast) showing « Mise à jour disponible », in the three tab
// layouts (top bar, side panel complete, side panel compact). Checked in screen pixels at the desktop's DPI:
//   * at rest: bottom-left, 14 DIP from the client's left and bottom edges, Paper's 269 × 209.59, under the chrome;
//   * a drawer open: on the top bar the card keeps clear of it (SetReservedRight); on the side layouts the drawers dock
//     on the panel's side (#143) and the card keeps its corner, as before this card;
//   * the bookmarks band (121 DIP of chrome) and a short window: the card hides when it no longer fits, its state kept.
// Nothing here changed with the new card (same Toast API and host calls); the test pins it for the taller card.
// Screen-free: a desktop of its own (CreateDesktop), never displayed. No WebView2 (a stand-in controller), no network,
// no update engine (the card is driven directly), no profile: every file goes to <out>.
#include "../../src/main.cpp"
#include <cstdio>

static int failures = 0, checks = 0;
static void Expect(bool ok, const std::string& what) { ++checks; std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) ++failures; }
static std::string Text(const RECT& r) { char b[80]; std::snprintf(b, sizeof b, "(%ld,%ld %ldx%ld)", r.left, r.top, r.right - r.left, r.bottom - r.top); return b; }

struct FakeController final : ICoreWebView2Controller {
    LONG refs = 1; RECT bounds{}; BOOL visible = FALSE;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override { if (!out) return E_POINTER; if (id == IID_IUnknown) { *out = static_cast<IUnknown*>(this); AddRef(); return S_OK; } *out = nullptr; return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override { const LONG n = InterlockedDecrement(&refs); if (!n) delete this; return (ULONG)n; }
    HRESULT STDMETHODCALLTYPE get_IsVisible(BOOL* v) override { if (v) *v = visible; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_IsVisible(BOOL v) override { visible = v; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Bounds(RECT* r) override { if (r) *r = bounds; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_Bounds(RECT r) override { bounds = r; return S_OK; }
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

static void Pump(int ms = 150) {
    const ULONGLONG end = GetTickCount64() + ms; MSG m;
    do { while (GetTickCount64() < end && PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); } Sleep(5); } while (GetTickCount64() < end);   // bounded
}
static RECT Win(HWND h) { RECT r{}; GetWindowRect(h, &r); return r; }
// The window's monitor's work area, and the part of the client that can be seen there: the card measures its corner from
// that part (ToastModel::VisibleClient), so a window larger than its screen (GitHub's 1024 × 768 runners) keeps it visible.
static RECT Work() { MONITORINFO mi{sizeof(mi)}; return GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &mi) ? mi.rcWork : ClientScreen(); }
static RECT Seen() { const RECT c = ClientScreen(), w = Work(); RECT v{}; return IntersectRect(&v, &c, &w) ? v : c; }
// The card at rest in its corner (its motion runs on the real clock: waited for, up to a generous bound, not assumed).
static bool AtRest() {
    const RECT v = Seen(), card = Win(updateToast.Window());
    return updateToast.Shown() && IsWindowVisible(updateToast.Window()) && card.left == v.left + S(14) && card.bottom == v.bottom - S(14) &&
           std::abs((card.bottom - card.top) - (int)std::lround(209.59375f * GetDpiForWindow(window) / 96.f)) <= 1;
}
template <class F> static bool PumpUntil(F done, int ms, int maxMs = 5000) { const ULONGLONG end = GetTickCount64() + maxMs; Pump(ms); while (!done() && GetTickCount64() < end) Pump(50); return done(); }
static void Settle(int ms = 700) { const ULONGLONG end = GetTickCount64() + 5000; Pump(ms); while (!AtRest() && GetTickCount64() < end) Pump(50); }
static void Relayout() { Layout(); SyncUpdateToastReserve(); LayoutUpdateToast(); Settle(); }

static void CheckAtRest(const std::string& name) {
    const RECT c = ClientScreen(), v = Seen(), work = Work(), card = Win(updateToast.Window());
    Expect(updateToast.Shown() && IsWindowVisible(updateToast.Window()), name + ": the card is shown");
    Expect(card.left == v.left + S(14) && card.bottom == v.bottom - S(14), name + ": bottom-left, 14 DIP from the visible edges " + Text(card) + " in " + Text(v) +
           (EqualRect(&v, &c) ? std::string() : " (client " + Text(c) + ", work area " + Text(work) + ")"));
    Expect(card.left >= work.left && card.top >= work.top && card.right <= work.right && card.bottom <= work.bottom, name + ": on the screen's work area " + Text(work));
    Expect(card.right - card.left == S(269) && std::abs((card.bottom - card.top) - (int)std::lround(209.59375f * GetDpiForWindow(window) / 96.f)) <= 1, name + ": Paper's 269 × 209.59");
    Expect(card.top >= c.top + S(ChromeBottomDip()), name + ": under the chrome (" + std::to_string(ChromeBottomDip()) + " DIP)");
}

int wmain(int argc, wchar_t** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 3) { std::fputs("usage: card_layouts_host_test <repo root> <out folder>\n", stderr); return 2; }
    base = argv[1]; const std::wstring out = argv[2]; std::filesystem::create_directories(out);
    // --dpi-unaware: every window at 96 dpi (GitHub's runners), whatever this screen's scale.
    const bool unaware = argc > 3 && std::wstring(argv[3]) == L"--dpi-unaware";
    SetProcessDpiAwarenessContext(unaware ? DPI_AWARENESS_CONTEXT_UNAWARE : DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const std::wstring deskName = L"BrowserCardLayouts" + std::to_wstring(GetCurrentProcessId());
    HDESK desk = CreateDesktopW(deskName.c_str(), nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    if (!desk || !SetThreadDesktop(desk)) { std::fprintf(stderr, "FAIL: own desktop (%lu)\n", GetLastError()); return 1; }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); instance = GetModuleHandleW(nullptr);
    dataDir = out + L"\\data"; std::filesystem::create_directories(dataDir); logFile = out + L"\\card-layouts.log"; std::filesystem::remove(logFile);
    for (auto name : NativeChrome::FontFiles) AddFontResourceExW((base + L"\\assets\\fonts\\" + name).c_str(), FR_PRIVATE, nullptr);
    paper = CreateSolidBrush(BG);
    WNDCLASSW wc{}; wc.lpfnWndProc = WndProc; wc.hInstance = instance; wc.lpszClassName = L"CardLayoutsTest"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); RegisterClassW(&wc);
    // The window asked at 1180 × 815 DIP from (120, 120), inside its monitor's work area: on a smaller screen (GitHub's
    // runners: 1024 × 768) it is made to fit, as a user's window would be. A window larger than its screen is checked
    // on its own below.
    const UINT sys = GetDpiForSystem();
    MONITORINFO primary{sizeof(primary)}; RECT work{0, 0, MulDiv(1024, sys, 96), MulDiv(728, sys, 96)};
    if (GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &primary)) work = primary.rcWork;
    const int edge = MulDiv(8, sys, 96), workW = work.right - work.left, workH = work.bottom - work.top;
    const int winW = std::min(MulDiv(1180, sys, 96), workW - 2 * edge), winH = std::min(MulDiv(815, sys, 96), workH - 2 * edge);
    const int winX = work.left + std::max(edge, std::min(MulDiv(120, sys, 96), workW - winW - edge));
    const int winY = work.top + std::max(edge, std::min(MulDiv(120, sys, 96), workH - winH - edge));
    CreateWindowW(wc.lpszClassName, L"browser", WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU | WS_CLIPCHILDREN,
                  winX, winY, winW, winH, nullptr, nullptr, instance, nullptr);
    if (!window) { std::fputs("FAIL: window\n", stderr); return 1; }
    NativeUI::ConfigureFullClientFrame(window); NativeUI::RoundNatively(window);
    if (FAILED(chrome.Init(window, base))) { std::fputs("FAIL: chrome\n", stderr); return 1; }
    motion.SetReducedMotion(true); chrome.SetMotion(&motion); UpdateFont();
    CreateDownloadPanel(); CreateSettingsPanel(); CreateHistoryPanel(); CreateBookmarkSurfaces();
    auto a = std::make_shared<Tab>(); a->title = L"Planete"; a->home = false; a->uri = L"https://planete.test/"; a->revealed = true; a->controller.p = new FakeController();
    pages = {a}; current = 0; a->usedAt = ++useClock;
    ShowWindow(window, SW_SHOWNORMAL);
    // The card the browser makes (StartUpdates without the engine: no network); « Mise à jour disponible ».
    Expect(updateToast.Init(window, base + L"\\assets", UpdateAction), "the browser's card created");
    BrowserUpdate::View offer; offer.phase = BrowserUpdate::Phase::Available; offer.version = L"0.3.999"; offer.total = 209715200;
    updateToast.SetView(offer);
    Relayout();
    std::printf("     window %s, dpi %u (%s), work area %s\n", Text(Win(window)).c_str(), GetDpiForWindow(window), unaware ? "DPI-unaware run" : "the desktop's", Text(Work()).c_str());
    for (auto layout : {BrowserSettings::TabsPosition::Top, BrowserSettings::TabsPosition::Side, BrowserSettings::TabsPosition::Compact}) {
        const std::string name = layout == BrowserSettings::TabsPosition::Top ? "top bar" : layout == BrowserSettings::TabsPosition::Side ? "side complete" : "side compact";
        settings.tabsPosition = layout; ApplyLayout(); Relayout();
        Expect((layout == BrowserSettings::TabsPosition::Top) == !chrome.sidebar, name + ": layout in effect");
        CheckAtRest(name);
        // A drawer open (Historique, then Téléchargements).
        ShowHistory(false); SyncUpdateToastReserve(); LayoutUpdateToast();
        RECT card{}, drawer{}, both{};
        auto clearOf = [&](HWND d) { card = Win(updateToast.Window()); drawer = Win(d); return updateToast.Shown() && !IntersectRect(&both, &card, &drawer); };
        if (!chrome.sidebar) {
            const bool ok = PumpUntil([&] { return clearOf(historyDrawer.Window()); }, 800);
            Expect(ok, name + ", Historique open on the right: the card keeps clear of it " + Text(card) + " / " + Text(drawer));
        } else {
            const bool ok = PumpUntil([&] { card = Win(updateToast.Window()); return updateToast.Shown() && card.left == Seen().left + S(14); }, 800);
            Expect(ok, name + ", Historique open on the panel's side: the card keeps its corner " + Text(card));
        }
        CloseDockedDrawers(); Pump(600); SyncUpdateToastReserve(); LayoutUpdateToast(); Settle(300);
        ShowDownloads(); SyncUpdateToastReserve(); LayoutUpdateToast(); Pump(800);
        if (downloadDrawer.Window() && downloadDrawer.Visible() && !chrome.sidebar) {
            const bool ok = PumpUntil([&] { return clearOf(downloadDrawer.Window()); }, 0);
            Expect(ok, name + ", Téléchargements open: the card keeps clear of it " + Text(card) + " / " + Text(drawer));
        }
        CloseDockedDrawers(); Pump(600); SyncUpdateToastReserve(); LayoutUpdateToast(); Settle(300);
        CheckAtRest(name + ", drawers closed again");
    }
    // The bookmarks band adds 40 DIP of chrome; a short window hides the card (state kept) and shows it again.
    settings.tabsPosition = BrowserSettings::TabsPosition::Top; ApplyLayout(); Relayout();
    SetUpdateToastTop(121); LayoutUpdateToast(); Settle(300);
    Expect(updateToast.Shown(), "the bookmarks band (121 DIP of chrome): the card still fits in " + std::to_string((Seen().bottom - Seen().top) * 96 / (int)GetDpiForWindow(window)) + " DIP");
    // The window's smallest height (300 DIP, WM_GETMINMAXINFO): 300 - 81 - 14 - 16 < 209.6, no room for the card.
    SetWindowPos(window, nullptr, 0, 0, std::min(S(900), winW), S(300), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    Layout(); SyncUpdateToastReserve(); LayoutUpdateToast();
    const bool hidden = PumpUntil([] { return !updateToast.Fits() && !IsWindowVisible(updateToast.Window()); }, 700);
    Expect(hidden, "a 300 DIP window: no room under the chrome, the card hides (state kept)");
    SetWindowPos(window, nullptr, 0, 0, winW, winH, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE); Relayout();
    CheckAtRest("back to its first size");
    // A window larger than its screen (the release run 45936128773: asked at 1180 × 815 from (120, 120) on GitHub's
    // 1024 × 768 runner, shrunk to 1044 × 788, its bottom 180 px under the work area): the card stays on what can be seen,
    // 14 DIP above the work area's bottom, never under the taskbar or off the screen.
    {
        const RECT was = Win(window), at = Work();
        SetWindowPos(window, nullptr, was.left, at.bottom - (was.bottom - was.top) + S(180), 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE); Relayout();
        const RECT c = ClientScreen(), card = Win(updateToast.Window());
        Expect(c.bottom > at.bottom && card.bottom == at.bottom - S(14), "a window past the bottom of its screen (client " + Text(c) + ", work area " + Text(at) +
               "): the card 14 DIP above the work area's bottom " + Text(card));
        CheckAtRest("a window past the bottom of its screen");
        for (auto layout : {BrowserSettings::TabsPosition::Side, BrowserSettings::TabsPosition::Compact, BrowserSettings::TabsPosition::Top}) {
            settings.tabsPosition = layout; ApplyLayout(); Relayout();
            CheckAtRest(std::string("past the bottom of its screen, ") + (layout == BrowserSettings::TabsPosition::Top ? "top bar" : layout == BrowserSettings::TabsPosition::Side ? "side complete" : "side compact"));
        }
        SetWindowPos(window, nullptr, was.left, was.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE); Relayout();
        CheckAtRest("moved back onto its screen");
    }
    updateToast.Close();
    std::printf("%d checks, %d failures\n", checks, failures);
    std::fflush(stdout);
    TerminateProcess(GetCurrentProcess(), failures ? 1 : 0);   // returning after including main.cpp can hang (static teardown)
    return failures ? 1 : 0;
}
