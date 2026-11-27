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
static void Relayout() { Layout(); SyncUpdateToastReserve(); LayoutUpdateToast(); Pump(700); }
static RECT Win(HWND h) { RECT r{}; GetWindowRect(h, &r); return r; }

static void CheckAtRest(const std::string& name) {
    const RECT c = ClientScreen(), card = Win(updateToast.Window());
    Expect(updateToast.Shown() && IsWindowVisible(updateToast.Window()), name + ": the card is shown");
    Expect(card.left == c.left + S(14) && card.bottom == c.bottom - S(14), name + ": bottom-left, 14 DIP from the edges " + Text(card) + " in " + Text(c));
    Expect(card.right - card.left == S(269) && std::abs((card.bottom - card.top) - (int)std::lround(209.59375f * GetDpiForWindow(window) / 96.f)) <= 1, name + ": Paper's 269 × 209.59");
    Expect(card.top >= c.top + S(ChromeBottomDip()), name + ": under the chrome (" + std::to_string(ChromeBottomDip()) + " DIP)");
}

int wmain(int argc, wchar_t** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 3) { std::fputs("usage: card_layouts_host_test <repo root> <out folder>\n", stderr); return 2; }
    base = argv[1]; const std::wstring out = argv[2]; std::filesystem::create_directories(out);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const std::wstring deskName = L"BrowserCardLayouts" + std::to_wstring(GetCurrentProcessId());
    HDESK desk = CreateDesktopW(deskName.c_str(), nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    if (!desk || !SetThreadDesktop(desk)) { std::fprintf(stderr, "FAIL: own desktop (%lu)\n", GetLastError()); return 1; }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); instance = GetModuleHandleW(nullptr);
    dataDir = out + L"\\data"; std::filesystem::create_directories(dataDir); logFile = out + L"\\card-layouts.log"; std::filesystem::remove(logFile);
    for (auto name : NativeChrome::FontFiles) AddFontResourceExW((base + L"\\assets\\fonts\\" + name).c_str(), FR_PRIVATE, nullptr);
    paper = CreateSolidBrush(BG);
    WNDCLASSW wc{}; wc.lpfnWndProc = WndProc; wc.hInstance = instance; wc.lpszClassName = L"CardLayoutsTest"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); RegisterClassW(&wc);
    const UINT sys = GetDpiForSystem();
    CreateWindowW(wc.lpszClassName, L"browser", WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU | WS_CLIPCHILDREN,
                  MulDiv(120, sys, 96), MulDiv(120, sys, 96), MulDiv(1180, sys, 96), MulDiv(815, sys, 96), nullptr, nullptr, instance, nullptr);
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
    std::printf("     window %s, dpi %u (the desktop's)\n", Text(Win(window)).c_str(), GetDpiForWindow(window));
    for (auto layout : {BrowserSettings::TabsPosition::Top, BrowserSettings::TabsPosition::Side, BrowserSettings::TabsPosition::Compact}) {
        const std::string name = layout == BrowserSettings::TabsPosition::Top ? "top bar" : layout == BrowserSettings::TabsPosition::Side ? "side complete" : "side compact";
        settings.tabsPosition = layout; ApplyLayout(); Relayout();
        Expect((layout == BrowserSettings::TabsPosition::Top) == !chrome.sidebar, name + ": layout in effect");
        CheckAtRest(name);
        // A drawer open (Historique, then Téléchargements).
        ShowHistory(false); SyncUpdateToastReserve(); LayoutUpdateToast(); Pump(800);
        RECT card = Win(updateToast.Window()), drawer = Win(historyDrawer.Window()), both{};
        if (!chrome.sidebar) Expect(updateToast.Shown() && !IntersectRect(&both, &card, &drawer), name + ", Historique open on the right: the card keeps clear of it " + Text(card) + " / " + Text(drawer));
        else Expect(updateToast.Shown() && card.left == ClientScreen().left + S(14), name + ", Historique open on the panel's side: the card keeps its corner " + Text(card));
        CloseDockedDrawers(); Pump(600); SyncUpdateToastReserve(); LayoutUpdateToast(); Pump(300);
        ShowDownloads(); SyncUpdateToastReserve(); LayoutUpdateToast(); Pump(800);
        if (downloadDrawer.Window() && downloadDrawer.Visible() && !chrome.sidebar) {
            const RECT c2 = Win(updateToast.Window()), d2 = Win(downloadDrawer.Window()); RECT overlap{};
            Expect(updateToast.Shown() && !IntersectRect(&overlap, &c2, &d2), name + ", Téléchargements open: the card keeps clear of it " + Text(c2) + " / " + Text(d2));
        }
        CloseDockedDrawers(); Pump(600); SyncUpdateToastReserve(); LayoutUpdateToast(); Pump(300);
        CheckAtRest(name + ", drawers closed again");
    }
    // The bookmarks band adds 40 DIP of chrome; a short window hides the card (state kept) and shows it again.
    settings.tabsPosition = BrowserSettings::TabsPosition::Top; ApplyLayout(); Relayout();
    SetUpdateToastTop(121); LayoutUpdateToast(); Pump(300);
    Expect(updateToast.Shown(), "the bookmarks band (121 DIP of chrome): the card still fits in 815 DIP");
    // The window's smallest height (300 DIP, WM_GETMINMAXINFO): 300 - 81 - 14 - 16 < 209.6, no room for the card.
    SetWindowPos(window, nullptr, 0, 0, S(900), S(300), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE); Relayout();
    Expect(!updateToast.Fits() && !IsWindowVisible(updateToast.Window()), "a 300 DIP window: no room under the chrome, the card hides (state kept)");
    SetWindowPos(window, nullptr, 0, 0, S(1180), S(815), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE); Relayout();
    CheckAtRest("back to 1180 × 815");
    updateToast.Close();
    std::printf("%d checks, %d failures\n", checks, failures);
    std::fflush(stdout);
    TerminateProcess(GetCurrentProcess(), failures ? 1 : 0);   // returning after including main.cpp can hang (static teardown)
    return failures ? 1 : 0;
}
