// Val 2026-11-26 (part A feedback, point 2): changing the layout from Réglages (the root's layout switch, and Réglages ›
// Onglets' own control) keeps the drawer OPEN: same view, focus kept on the chosen segment, placed where drawers go in
// the new layout (Fix's #143: the top bar's on the right, the side panel's against the column on the left), painted at
// its new place in the same turn (no slide, no frame at the old place), however many times in a row. The other drawers
// (Historique, Favoris, Téléchargements) still close on a layout change, as before.
// Drives src/main.cpp itself (its window procedure, ApplyLayout, Layout, the real Réglages drawer and its hooks) on a
// window with the browser's styles, on a Windows desktop of its own that is never displayed: nothing reaches the
// user's screen, mouse, keyboard or foreground. A stand-in WebView2 controller: no WebView2, no network, no profile
// (every file goes to <out>). Pictures: the drawer as painted in each layout, and the rectangles for
// tests/atelier-a/compose.py (layout-keep.json).
// Usage: settings_layout_keep_test.exe <repo root> <out folder>
#include "../../src/main.cpp"
#include <cstdio>
#include <fstream>

static int failures = 0, checks = 0;
static void Expect(bool ok, const std::string& what) { ++checks; std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) ++failures; }
static std::string Text(const RECT& r) { char b[80]; std::snprintf(b, sizeof b, "(%ld,%ld %ldx%ld)", r.left, r.top, r.right - r.left, r.bottom - r.top); return b; }
static bool Near(float a, float b, float e = .51f) { return std::fabs(a - b) <= e; }

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
    do { while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); } Sleep(5); } while (GetTickCount64() < end);
}
static RECT Win(HWND h) { RECT r{}; GetWindowRect(h, &r); return r; }
static const char* Name(BrowserSettings::TabsPosition p) { return p == BrowserSettings::TabsPosition::Top ? "top bar" : p == BrowserSettings::TabsPosition::Side ? "side complete" : "side compact"; }
// The drawer docked where the current layout puts drawers (Fix's #143 rule, as tests/drawer-side checks it).
static bool Docked(const std::string& what) {
    namespace DL = NativeChrome::Downloads::DrawerLayout;
    const RECT c = ClientScreen(), w = Win(settingsDrawer.Window()); const int width = S(NativeChrome::SettingsUI::Layout::width);
    bool ok = IsWindowVisible(settingsDrawer.Window()) && w.right - w.left == width && w.top == c.top + S(DL::liveTop) && w.bottom == c.bottom;
    if (!chrome.sidebar) ok = ok && !DL::liveLeft && w.right == c.right && Near(ContentBox().right, chrome.width - NativeChrome::SettingsUI::Layout::width - 4, 1);
    else ok = ok && DL::liveLeft && w.left == c.left + S(chrome.side.drawerDock) && Near(ContentBox().left, chrome.side.drawerDock + NativeChrome::SettingsUI::Layout::width + 4, 1);
    Expect(ok, what + ": docked " + (chrome.sidebar ? "on the LEFT against the column" : "on the RIGHT") + " " + Text(w) + ", the page beside it");
    return ok;
}

int wmain(int argc, wchar_t** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 3) { std::fputs("usage: settings_layout_keep_test <repo root> <out folder>\n", stderr); return 2; }
    base = argv[1]; const std::wstring out = argv[2]; std::filesystem::create_directories(out);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const std::wstring deskName = L"BrowserLayoutKeep" + std::to_wstring(GetCurrentProcessId());
    HDESK desk = CreateDesktopW(deskName.c_str(), nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    if (!desk || !SetThreadDesktop(desk)) { std::fprintf(stderr, "FAIL: own desktop (%lu)\n", GetLastError()); return 1; }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); instance = GetModuleHandleW(nullptr);
    dataDir = out + L"\\data"; std::filesystem::create_directories(dataDir); logFile = out + L"\\layout-keep.log"; std::filesystem::remove(logFile);
    for (auto name : NativeChrome::FontFiles) AddFontResourceExW((base + L"\\assets\\fonts\\" + name).c_str(), FR_PRIVATE, nullptr);
    paper = CreateSolidBrush(BG);
    WNDCLASSW wc{}; wc.lpfnWndProc = WndProc; wc.hInstance = instance; wc.lpszClassName = L"LayoutKeepTest"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); RegisterClassW(&wc);
    const UINT sys = GetDpiForSystem();
    CreateWindowW(wc.lpszClassName, L"browser", WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU | WS_CLIPCHILDREN,
                  MulDiv(120, sys, 96), MulDiv(120, sys, 96), MulDiv(1180, sys, 96), MulDiv(740, sys, 96), nullptr, nullptr, instance, nullptr);
    if (!window) { std::fputs("FAIL: window\n", stderr); return 1; }
    NativeUI::ConfigureFullClientFrame(window); NativeUI::RoundNatively(window);
    if (FAILED(chrome.Init(window, base))) { std::fputs("FAIL: chrome\n", stderr); return 1; }
    motion.SetReducedMotion(true); chrome.SetMotion(&motion); UpdateFont();
    CreateDownloadPanel(); CreateSettingsPanel(); CreateHistoryPanel(); CreateBookmarkSurfaces();
    auto a = std::make_shared<Tab>(); a->title = L"Planete"; a->home = false; a->uri = L"https://planete.test/"; a->revealed = true; a->controller.p = new FakeController();
    pages = {a}; current = 0; a->usedAt = ++useClock;
    settings.tabsPosition = BrowserSettings::TabsPosition::Top; ApplyLayout();
    ShowWindow(window, SW_SHOWNORMAL); Layout(); Pump();
    std::printf("     window %s, dpi %u (its own desktop's)\n", Text(Win(window)).c_str(), GetDpiForWindow(window));

    using BrowserSettings::TabsPosition; using NativeChrome::SettingsUI::LayoutSwitchBase;
    std::ofstream rects((out + L"\\layout-keep.json").c_str()); rects << "[\n"; bool first = true;
    ShowSettings(); Pump(700);
    Expect(settingsDrawer.Open() && settings.tabsPosition == TabsPosition::Top, "Réglages open in the top bar");
    Docked("top bar, opened");
    // The switch, several times in a row, every transition between the three layouts (and back).
    const TabsPosition seq[] = {TabsPosition::Side, TabsPosition::Compact, TabsPosition::Top, TabsPosition::Compact, TabsPosition::Side, TabsPosition::Top, TabsPosition::Side};
    int step = 0;
    for (auto target : seq) {
        const std::string name = std::string("switch #") + std::to_string(++step) + " to « " + Name(target) + " »";
        const int hit = LayoutSwitchBase + static_cast<int>(target);
        settingsDrawer.FocusForTest(hit, true);   // the keyboard on that segment (Tab / arrows), then Entrée
        settingsDrawer.ActivateForTest(hit, true);
        // Same turn, no message pumped yet: already open, at rest (no slide), at its new place.
        Expect(settings.tabsPosition == target && chrome.sidebar == BrowserSettings::SidePanel(target), name + ": the layout changed");
        Expect(settingsDrawer.Open() && Near(settingsDrawer.SlideForTest(NativeChrome::Motion::Now()), 1, .001f), name + ": the drawer is still open, at rest (no slide in again)");
        Expect(settingsDrawer.CurrentViewForTest() == NativeChrome::SettingsUI::View::Main && settingsDrawer.DepthForTest() == 0, name + ": same view (the root)");
        Expect(settingsDrawer.FocusForTest() == hit, name + ": the focus stays on the chosen segment");
        Docked(name + ", in the same turn");
        Pump(400);
        Expect(settingsDrawer.Open() && settingsDrawer.FocusForTest() == hit, name + ": still open and focused after the next frames");
        Docked(name + ", settled");
        const RECT c = ClientScreen(), w = Win(settingsDrawer.Window()); const auto page = ContentBox(); const float k = GetDpiForWindow(window) / 96.f;
        const std::wstring png = out + L"\\drawer-" + std::to_wstring(step) + L".png";
        settingsDrawer.RenderForTest(png, 96, (w.bottom - w.top) / k, base);
        char line[512]; std::snprintf(line, sizeof line, "%s  {\"step\": %d, \"layout\": \"%s\", \"client\": [%.1f, %.1f], \"drawer\": [%.1f, %.1f, %.1f, %.1f], \"page\": [%.1f, %.1f, %.1f, %.1f], \"column\": %.1f, \"png\": \"drawer-%d.png\"}",
                                     first ? "" : ",\n", step, Name(target), (c.right - c.left) / k, (c.bottom - c.top) / k, (w.left - c.left) / k, (w.top - c.top) / k, (w.right - c.left) / k, (w.bottom - c.top) / k,
                                     page.left, page.top, page.right, page.bottom, chrome.sidebar ? chrome.side.drawerDock : 0.f, step);
        rects << line; first = false;
    }
    rects << "\n]\n"; rects.close();
    // Réglages › Onglets' own control: the same rule (the drawer on its sub-view, the focus on its segment).
    settingsDrawer.ActivateForTest(NativeChrome::SettingsUI::CatTabs, true); Pump(700);
    Expect(settingsDrawer.CurrentViewForTest() == NativeChrome::SettingsUI::View::Tabs, "Réglages › Onglets open");
    {   const int hit = NativeChrome::SettingsUI::TabsLayoutBase + static_cast<int>(TabsPosition::Top);
        settingsDrawer.FocusForTest(hit, true); settingsDrawer.ActivateForTest(hit, true); Pump(400);
        Expect(settings.tabsPosition == TabsPosition::Top && !chrome.sidebar && settingsDrawer.Open() && settingsDrawer.CurrentViewForTest() == NativeChrome::SettingsUI::View::Tabs && settingsDrawer.FocusForTest() == hit,
               "Réglages › Onglets « En haut »: the drawer stays on Onglets, focused, in the top bar");
        Docked("Réglages › Onglets, top bar"); }
    // The other drawers still close on a layout change (the rule is Réglages' own).
    settingsDrawer.Hide(false, false); Pump(300); ShowHistory(false); Pump(700);
    SetTabsPosition(TabsPosition::Side); Pump(500);
    Expect(chrome.sidebar && !historyDrawer.Open(), "Historique open, the layout changed: it closes as before");
    std::printf("%s: %d checks, %d failures\n", failures ? "FAIL" : "PASS", checks, failures);
    std::fflush(stdout); TerminateProcess(GetCurrentProcess(), failures ? 1 : 0); return failures ? 1 : 0;
}
