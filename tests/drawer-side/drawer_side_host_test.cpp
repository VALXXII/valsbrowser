// #143 (owner 2026-11-25): the drawers open on the tab panel's side, and a button's tooltip never shows over the menu it
// opened. Drives src/main.cpp itself (its window procedure, Layout, the real Historique / Réglages / Favoris /
// Téléchargements drawers, the side Focus column, the narrow window's column over the page, the toolbar tooltip) on a
// window with the browser's styles, and checks in screen pixels at the desktop's DPI:
//   * the top bar: the drawers on the right as before (the page stops 4 DIP before them);
//   * the side panel (complete, compact) and its Mode Focus: on the LEFT, docked against the panel (in Focus just after
//     the shown column's 1 DIP border), the page starting 4 DIP after them, the corners square (no window corner), the
//     Téléchargements window's margin strip on the page's side;
//   * side Focus (Designer's Q2): a drawer brings the column at rest and keeps it; Échap closes the drawer first; the
//     column leaves with Ctrl+Maj+, taking the drawer; Focus switched on with a drawer open brings the column;
//   * a window narrower than the complete column (Q4): its column over the page and a drawer, one at a time;
//   * the tooltip: muted over its own open menu (Téléchargements, and every other button with a menu), back only once
//     the pointer has left the button and come back; other buttons untouched; the keyboard's focus tips too.
// Screen-free: the window lives on a desktop of its own (CreateDesktop), never displayed: nothing reaches the user's
// screen, mouse, keyboard or foreground. No WebView2 (a stand-in controller), no network, no profile: every file goes to
// <out>. Run it through tests/drawer-side/run.py (job object).
#include "../../src/main.cpp"
#include <cstdio>

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
static void Relayout() { Layout(); Pump(); }
static std::shared_ptr<Tab> MakeTab(const wchar_t* title) {
    auto t = std::make_shared<Tab>(); t->title = title; t->home = false; t->uri = std::wstring(L"https://") + title + L".test/"; t->revealed = true;
    t->controller.p = new FakeController(); return t;
}
static RECT Win(HWND h) { RECT r{}; GetWindowRect(h, &r); return r; }
static int Px(float dip) { return S(dip); }
// The region the window keeps (window pixels): box and whether its bottom-left / bottom-right pixels are in it.
struct Shape { RECT box{}; bool bottomLeft = false, bottomRight = false; };
static Shape ShapeOf(HWND h) {
    Shape s; HRGN r = CreateRectRgn(0, 0, 0, 0); const RECT w = Win(h);
    if (GetWindowRgn(h, r) != ERROR) { GetRgnBox(r, &s.box); s.bottomLeft = PtInRegion(r, s.box.left, s.box.bottom - 1) != FALSE; s.bottomRight = PtInRegion(r, s.box.right - 1, s.box.bottom - 1) != FALSE; }
    else { s.box = {0, 0, w.right - w.left, w.bottom - w.top}; s.bottomLeft = s.bottomRight = true; }
    DeleteObject(r); return s;
}
static void CloseAll() { CloseDockedDrawers(); Pump(500); }

// One drawer, open at rest: its window docked where the layout says, the page after (or before) it.
static void CheckDocked(const std::string& what, HWND drawer, float widthDip, bool margin) {
    const RECT c = ClientScreen(), w = Win(drawer); const auto box = ContentBox(); const Shape shape = ShapeOf(drawer);
    const int width = Px(widthDip) + (margin ? Px(NativeChrome::Downloads::DrawerLayout::margin) : 0);
    const float top = NativeChrome::Downloads::DrawerLayout::liveTop;
    Expect(IsWindowVisible(drawer) != FALSE, what + ": shown");
    Expect(w.top == c.top + Px(top) && w.bottom == c.bottom && w.right - w.left == width, what + ": " + Text(w) + " from the drawers' top, down to the window's bottom, " + std::to_string(width) + " px wide");
    if (!chrome.sidebar) {
        Expect(!NativeChrome::Downloads::DrawerLayout::liveLeft && w.right == c.right, what + ": top bar, on the RIGHT, flush with the window's edge");
        Expect(Near(box.right, chrome.width - widthDip - 4, 1), what + ": the page stops 4 DIP before it (" + std::to_string(box.right) + ")");
        Expect(shape.bottomLeft && !shape.bottomRight, what + ": its bottom-right corner rounded on the window's");
        return;
    }
    const float dock = chrome.side.drawerDock;
    Expect(NativeChrome::Downloads::DrawerLayout::liveLeft && w.left == c.left + Px(dock), what + ": on the LEFT, docked against the panel at " + std::to_string(dock) + " DIP (" + std::to_string(w.left - c.left) + " px)");
    Expect(Near(box.left, dock + widthDip + 4, 1), what + ": the page starts 4 DIP after it (" + std::to_string(box.left) + ")");
    Expect(shape.bottomLeft && shape.bottomRight, what + ": square corners (no window corner against the panel)");
    if (margin) Expect(shape.box.left == 0 && shape.box.right == Px(widthDip), what + ": the margin strip on the page's side, outside its region " + Text(shape.box));
    if (!pages.empty() && pages[current]->placedVisible == 1) Expect(pages[current]->placed.left >= Px(dock + widthDip), what + ": the page window right of it " + Text(pages[current]->placed));
}

static RECT RegionRect(int id) { for (auto& r : chrome.regions) if (r.id == id) return ChromePx(r.rect); return {}; }
static void HoverOver(int id) { const RECT r = RegionRect(id); ChromeTipHover((r.left + r.right) / 2, (r.top + r.bottom) / 2); }
static void HoverNowhere() { const RECT c = {0, 0, 0, 0}; (void)c; ChromeTipHover(S(chrome.width / 2), S(chrome.height - 20)); }
static bool TipComing() { return chromeTip.Pending() || chromeTip.Visible(); }
// The Réglages drawer rendered offscreen (its own Paint at rest): the grey of its first and last pixel columns, mid-height.
static bool EdgeGreys(const std::wstring& png, int& leftGrey, int& rightGrey) {
    if (!settingsDrawer.RenderForTest(png, GetDpiForWindow(window), 400, base)) return false;
    IWICImagingFactory* wic = nullptr; IWICBitmapDecoder* dec = nullptr; IWICBitmapFrameDecode* frame = nullptr; IWICFormatConverter* conv = nullptr; bool ok = false;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) && SUCCEEDED(wic->CreateDecoderFromFilename(png.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &dec)) &&
        SUCCEEDED(dec->GetFrame(0, &frame)) && SUCCEEDED(wic->CreateFormatConverter(&conv)) && SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) {
        UINT w = 0, h = 0; conv->GetSize(&w, &h); BYTE px[4]{};
        WICRect a{0, (INT)h / 2, 1, 1}, b{(INT)w - 1, (INT)h / 2, 1, 1};
        if (SUCCEEDED(conv->CopyPixels(&a, 4, 4, px))) { leftGrey = px[1]; if (SUCCEEDED(conv->CopyPixels(&b, 4, 4, px))) { rightGrey = px[1]; ok = true; } }
    }
    if (conv) conv->Release(); if (frame) frame->Release(); if (dec) dec->Release(); if (wic) wic->Release();
    return ok;
}

int wmain(int argc, wchar_t** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 3) { std::fputs("usage: drawer_side_host_test <repo root> <out folder>\n", stderr); return 2; }
    base = argv[1]; const std::wstring out = argv[2]; std::filesystem::create_directories(out);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const std::wstring deskName = L"BrowserDrawerSide" + std::to_wstring(GetCurrentProcessId());
    HDESK desk = CreateDesktopW(deskName.c_str(), nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    if (!desk || !SetThreadDesktop(desk)) { std::fprintf(stderr, "FAIL: own desktop (%lu)\n", GetLastError()); return 1; }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); instance = GetModuleHandleW(nullptr);
    dataDir = out + L"\\data"; std::filesystem::create_directories(dataDir); logFile = out + L"\\drawer-side.log"; std::filesystem::remove(logFile);
    for (auto name : NativeChrome::FontFiles) AddFontResourceExW((base + L"\\assets\\fonts\\" + name).c_str(), FR_PRIVATE, nullptr);
    paper = CreateSolidBrush(BG);
    WNDCLASSW wc{}; wc.lpfnWndProc = WndProc; wc.hInstance = instance; wc.lpszClassName = L"DrawerSideTest"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); RegisterClassW(&wc);
    const UINT sys = GetDpiForSystem();
    CreateWindowW(wc.lpszClassName, L"browser", WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU | WS_CLIPCHILDREN,
                  MulDiv(120, sys, 96), MulDiv(120, sys, 96), MulDiv(1180, sys, 96), MulDiv(740, sys, 96), nullptr, nullptr, instance, nullptr);
    if (!window) { std::fputs("FAIL: window\n", stderr); return 1; }
    NativeUI::ConfigureFullClientFrame(window); NativeUI::RoundNatively(window);
    if (FAILED(chrome.Init(window, base))) { std::fputs("FAIL: chrome\n", stderr); return 1; }
    motion.SetReducedMotion(true); chrome.SetMotion(&motion); UpdateFont();
    CreateDownloadPanel(); CreateSettingsPanel(); CreateHistoryPanel(); CreateBookmarkSurfaces();
    { NativeChrome::Tooltip::Hooks th; th.wake = [] {}; chromeTip.Create(instance, window, base, std::move(th)); }
    auto a = MakeTab(L"Planete"); pages = {a}; current = 0; a->usedAt = ++useClock;
    ShowWindow(window, SW_SHOWNORMAL); Relayout();
    std::printf("     window %s, dpi %u (the desktop's)\n", Text(Win(window)).c_str(), GetDpiForWindow(window));

    // ---- 1. Top bar: on the right, unchanged ----
    ShowHistory(false); Pump(700); CheckDocked("top bar, Historique", historyDrawer.Window(), NativeChrome::Downloads::DrawerLayout::width, false); CloseAll();
    ShowSettings(); Pump(700); CheckDocked("top bar, Réglages", settingsDrawer.Window(), NativeChrome::SettingsUI::Layout::width, false);
    { int l = 0, r = 0; const bool drawn = EdgeGreys(out + L"\\settings-top-bar.png", l, r);
      Expect(drawn && l > r && r == 0x17, "top bar, Réglages: its hairline on the LEFT edge, toward the page (" + std::to_string(l) + " | " + std::to_string(r) + ")"); }
    CloseAll();

    // ---- 2. Side panel, complete then compact: on the left, against the panel ----
    for (auto layout : {BrowserSettings::TabsPosition::Side, BrowserSettings::TabsPosition::Compact}) {
        settings.tabsPosition = layout; ApplyLayout(); Relayout();
        const std::string name = layout == BrowserSettings::TabsPosition::Side ? "side complete" : "side compact";
        Expect(chrome.sidebar && Near(chrome.side.drawerDock, layout == BrowserSettings::TabsPosition::Side ? 248.f : 40.f, .01f), name + ": the dock on the column's edge (" + std::to_string(chrome.side.drawerDock) + ")");
        ShowHistory(false); Pump(700); CheckDocked(name + ", Historique", historyDrawer.Window(), NativeChrome::Downloads::DrawerLayout::width, false); CloseAll();
        ShowSettings(); Pump(700); CheckDocked(name + ", Réglages", settingsDrawer.Window(), NativeChrome::SettingsUI::Layout::width, false);
        { int l = 0, r = 0; const bool drawn = EdgeGreys(out + L"\\settings-" + std::wstring(name.begin(), name.end()) + L".png", l, r);
          Expect(drawn && r > l && l == 0x17, name + ", Réglages: its hairline mirrored to the RIGHT edge, toward the page (" + std::to_string(l) + " | " + std::to_string(r) + ")"); }
        CloseAll();
        ShowBookmarksDrawer(false); Pump(700); CheckDocked(name + ", Favoris", bookmarksDrawer.Window(), NativeChrome::Downloads::DrawerLayout::width, false); CloseAll();
        ShowDownloads(); Pump(300); ShowDownloadDrawer(); Pump(900); CheckDocked(name + ", Téléchargements (Tout afficher)", downloadDrawer.Window(), NativeChrome::Downloads::DrawerLayout::width, true); CloseAll();
        Expect(Near(ContentBox().left, chrome.SidebarZoneLeft(), .01f), name + ": every drawer closed, the page back against the column");
    }
    // The column's edge cannot be dragged while a drawer is docked against it (its line follows the page's left edge).
    settings.tabsPosition = BrowserSettings::TabsPosition::Side; ApplyLayout(); Relayout();
    Expect(chrome.sidebarResizable, "side complete: the column's edge can be dragged");
    ShowHistory(false); Pump(700); BuildChrome(); Expect(!chrome.sidebarResizable, "side complete, a drawer open: no edge handle"); CloseAll(); BuildChrome();
    Expect(chrome.sidebarResizable, "side complete, the drawer closed: the edge handle back");

    // ---- 3. Side Mode Focus (Designer's Q2 / Q3) ----
    settings.focusMode = true; Relayout();
    Expect(chrome.sideHidden && !sideReveal.open, "side Focus: the column hidden");
    ShowHistory(true); Pump(700);
    Expect(sideReveal.open && sideLive && sideDrawerHold, "side Focus, Historique by the keyboard: the column at rest, held while the drawer is open");
    Expect(Near(chrome.side.drawerDock, chrome.side.column + 1, .01f), "side Focus: the dock just after the column's 1 DIP border (" + std::to_string(chrome.side.drawerDock) + ")");
    CheckDocked("side Focus, Historique", historyDrawer.Window(), NativeChrome::Downloads::DrawerLayout::width, false);
    SideFocusEvaluate(); Pump(100);
    Expect(sideReveal.open, "side Focus: the pointer away, the column stays while the drawer is open");
    Expect(SideFocusEscape() && !historyDrawer.Open() && sideReveal.open, "side Focus, Échap: the drawer first, the column still there");
    keyboardFocus = 0; editing = false; Pump(500); SideFocusEvaluate(); Pump(500);   // nothing of the chrome keeps it (no keyboard stop in it)
    Expect(!sideReveal.open, "side Focus: the drawer closed and the pointer away, the column leaves again");
    ShowSettings(); Pump(700);
    Expect(sideReveal.open && settingsDrawer.Open(), "side Focus, Réglages: the column back with it");
    ToggleSidebar(); Pump(700);
    Expect(!sideReveal.open && !settingsDrawer.Open(), "side Focus, Ctrl+Maj+, : the column leaves and takes the drawer docked against it");
    settings.focusMode = false; Relayout(); ShowHistory(false); Pump(700);
    Expect(historyDrawer.Open() && !chrome.sideHidden, "side, Historique open before Focus");
    settings.focusMode = true; Relayout(); Pump(700);
    Expect(chrome.sideHidden && sideReveal.open && historyDrawer.Open(), "Focus switched on with a drawer open: the column comes to it");
    CheckDocked("Focus switched on, Historique", historyDrawer.Window(), NativeChrome::Downloads::DrawerLayout::width, false);
    CloseAll(); settings.focusMode = false; Relayout();

    // ---- 4. A narrow window (Q4): the column over the page and a drawer, one at a time ----
    {   const RECT before = Win(window); SetWindowPos(window, nullptr, 0, 0, S(700), before.bottom - before.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE); Relayout();
        Expect(chrome.sidebar && chrome.width < SBar::autoCompactBelow && Near(chrome.side.drawerDock, 40, .01f), "narrow window: compact by itself, the dock at 40");
        ShowHistory(false); Pump(700); CheckDocked("narrow, Historique", historyDrawer.Window(), NativeChrome::Downloads::DrawerLayout::width, false);
        ToggleSidebar(); Pump(700);
        Expect(sidebarHover.open && !historyDrawer.Open(), "narrow: the column over the page opens, the drawer closes");
        ShowSettings(); Pump(700);
        Expect(!sidebarHover.open && settingsDrawer.Open(), "narrow: a drawer opens, the column over the page closes");
        CheckDocked("narrow, Réglages", settingsDrawer.Window(), NativeChrome::SettingsUI::Layout::width, false);
        CloseAll(); SetWindowPos(window, nullptr, 0, 0, before.right - before.left, before.bottom - before.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE); Relayout(); }

    // ---- 5. The tooltip never over its own open menu ----
    {   namespace TT = NativeChrome::Tooltip;
        TT::MenuMute m;   // the state machine
        Expect(m.Opened('D') && !m.Opened('D'), "mute: a new menu is reported once");
        Expect(m.Muted('D', true) && !m.Muted('S', true) && m.Muted('D', true), "mute: its button muted while open, another one not");
        Expect(m.Muted('D', false), "mute: closed, the pointer never left: still muted");
        Expect(!m.Muted(0, false) && !m.Muted('D', false) && m.id == 0, "mute: left then back: its tip again");
        m.Opened('V'); m.Muted(0, true); Expect(m.Muted('V', true), "mute: left and back while still open: muted");
        Expect(m.Muted('V', false), "mute: closed while on it: muted until it leaves");
        m.Muted('Y', false); Expect(!m.Muted('V', false), "mute: re-entered after closing: shown");
        for (auto layout : {BrowserSettings::TabsPosition::Top, BrowserSettings::TabsPosition::Compact}) {
            settings.tabsPosition = layout; ApplyLayout(); Relayout(); chromeTip.HideNow(); chromeTipMute.Reset();
            const std::string name = layout == BrowserSettings::TabsPosition::Top ? "top bar" : "compact column";
            HoverOver('D'); Expect(TipComing(), name + ": « Téléchargements » tooltip on the button, menu closed");
            HoverNowhere(); chromeTip.HideNow();
            ShowDownloads(); Pump(100);
            Expect(downloadPanel.Visible() && chrome.latched == 'D', name + ": the downloads menu open");
            Expect(!TipComing(), name + ": its tooltip gone at once");
            for (int i = 0; i < 3; ++i) { HoverOver('D'); Pump(30); }
            Pump(800);
            Expect(!TipComing(), name + ": the pointer moving on the button: no tooltip over its open menu (owner's capture)");
            HoverOver('S'); Expect(TipComing(), name + ": another button's tooltip still shows");
            HoverOver('D'); Pump(700); Expect(!TipComing(), name + ": back on « Téléchargements » while its menu is open: none");
            downloadPanel.Hide(false); Pump(100); SyncLatch();
            HoverOver('D'); Pump(700); Expect(!TipComing(), name + ": menu closed, the pointer still on the button: none until it leaves");
            HoverNowhere(); HoverOver('D'); Expect(TipComing(), name + ": the pointer left and came back: the tooltip again (usual delay)");
            HoverNowhere(); chromeTip.HideNow();
            // Leaving the window (into the popover) counts as leaving the button.
            ShowDownloads(); Pump(100); HoverOver('D'); ChromeTipLeft(); downloadPanel.Hide(false); SyncLatch(); HoverOver('D');
            Expect(TipComing(), name + ": left into the popover, back after it closed: the tooltip again");
            HoverNowhere(); chromeTip.HideNow();
        }
        // The keyboard (compact column: the focused control's tooltip): not while its menu is open.
        settings.tabsPosition = BrowserSettings::TabsPosition::Compact; ApplyLayout(); Relayout(); chromeTipMute.Reset();
        ChromeTipFocus('D'); Expect(chromeTip.Visible(), "compact, keyboard focus on Téléchargements: its tooltip");
        ShowDownloads(); Pump(100); ChromeTipFocus('D'); Expect(!TipComing(), "compact, keyboard: its menu open, no tooltip");
        downloadPanel.Hide(false); SyncLatch(); ChromeTipFocus('Y'); chromeTip.HideNow(); ChromeTipFocus('D'); Expect(chromeTip.Visible(), "compact, keyboard: focus moved away and back after it closed: the tooltip again");
        chromeTip.HideNow();
        // Every other button with a menu.
        chrome.latched = 'V'; Expect(ChromeTipMenuOpen('V'), "Son: muted while its popover / drawer is open"); chrome.latched = 'S'; Expect(ChromeTipMenuOpen('S'), "Réglages: muted while its drawer is open");
        chrome.latched = 'Y'; Expect(ChromeTipMenuOpen('Y') && !ChromeTipMenuOpen('D'), "Historique: muted while its drawer is open, not the others"); chrome.latched = 0;
        chrome.bandOpen = 'O'; Expect(ChromeTipMenuOpen('O'), "« » » / a band folder: muted while its menu is open"); chrome.bandOpen = 0;
        chrome.profileButton.open = true; Expect(ChromeTipMenuOpen(900), "profile: muted while its menu is open"); chrome.profileButton.open = false;
        chrome.sidebarPopover = true; Expect(ChromeTipMenuOpen(NativeChrome::Chrome::SidebarMoreId), "compact « » »: muted while its popover is open"); chrome.sidebarPopover = false;
        ShowBookmarksDrawer(false); Pump(300); Expect(ChromeTipMenuOpen(NativeChrome::Chrome::SidebarManageId), "« Gérer les favoris »: muted while the Favoris drawer is open"); CloseAll();
        Expect(!ChromeTipMenuOpen('D') && !ChromeTipMenuOpen(900) && !ChromeTipMenuOpen('B'), "nothing open: nothing muted");
    }
    // ---- 7. Infobulle B (#143): the side column's tabs and favourites ----
    {   namespace ST = NativeChrome::SideTip;
        sideTip.Create(instance, window, base); sideTip.SetReducedMotion(false);
        bookmarks.Add(BM::BarRoot, 0, L"Le Monde", L"https://www.lemonde.fr/planete/", 0); auto b2 = MakeTab(L"Notes"); pages.push_back(b2); b2->uri = L"https://notes.example.org/x";
        const float s = GetDpiForWindow(window) / 96.f; POINT origin{0, 0}; ClientToScreen(window, &origin);
        auto regionOf = [](int id) { for (auto& r : chrome.regions) if (r.id == id) return r; return NativeChrome::Region{0, {}, nullptr}; };
        for (auto layout : {BrowserSettings::TabsPosition::Compact, BrowserSettings::TabsPosition::Side}) {
            settings.tabsPosition = layout; ApplyLayout(); BookmarksChanged(); Relayout(); sideTip.HideNow(); chromeTip.HideNow(); chromeTipMute.Reset();
            const bool compact = layout == BrowserSettings::TabsPosition::Compact; const std::string name = compact ? "Infobulle B, compact" : "Infobulle B, complete";
            const int tab0 = NativeChrome::TabBase, tab1 = NativeChrome::TabBase + 1, tile = NativeChrome::BookmarkBase;
            const auto r0 = regionOf(tab0);
            HoverOver(tab0);
            if (compact) Expect(sideTip.Shown() && sideTip.Item() == tab0, name + ": a tab's tip at once (owner, 2026-11-26)");
            else { Expect(!sideTip.Shown() && sideTip.Waiting(), name + ": nothing before 500 ms of rest"); Pump(350); Expect(!sideTip.Shown(), name + ": still nothing at 350 ms"); Pump(300); Expect(sideTip.Shown() && sideTip.Item() == tab0, name + ": the tip after 500 ms"); }
            Expect(!TipComing(), name + ": not the toolbar's tooltip");
            const auto box = sideTip.Target();
            Expect(Near(box.left, r0.rect.right + 8, .01f) && Near((box.top + box.bottom) / 2, (r0.rect.top + r0.rect.bottom) / 2, .51f), name + ": 8 DIP right of the hovered " + (compact ? "plate" : "row") + ", centred on it (" + std::to_string(box.left) + ")");
            Expect(Near(box.Height(), 56, .01f) && sideTip.Current().title == L"Planete" && sideTip.Current().address == L"planete.test", name + ": title, then icon + address (56 high)");
            Pump(500);
            {   const RECT pl = sideTip.Placed(); const float m = ST::Window::Margin();
                Expect(sideTip.WindowShown() && sideTip.Presents() > 0 && pl.left == origin.x + (LONG)std::lround((box.left - m) * s) && pl.top == origin.y + (LONG)std::lround((box.top - m) * s),
                       name + ": on screen at the box's place in pixels (" + std::to_string(GetDpiForWindow(window)) + " dpi) " + Text(pl)); }
            sideTip.SaveForTest((out + (compact ? L"\\side-tip-compact.png" : L"\\side-tip-complete.png")).c_str());
            HoverOver(tab1);
            Expect(sideTip.Shown() && sideTip.Item() == tab1 && sideTip.Current().title == L"Notes", name + ": the next tab at once, the box gliding there");
            Expect(sideTip.MotionForTest().At(NativeChrome::Motion::Now()).opacity > .99f, name + ": no frame without the tip between two tabs");
            Pump(400);
            const auto rt = regionOf(tile);
            if (rt.id) { HoverOver(tile); Expect(sideTip.Shown() && sideTip.Item() == tile && sideTip.Current().title == L"Le Monde" && sideTip.Current().address == L"lemonde.fr", name + ": a favourite's tip (title, then its address)");
                         Expect(Near(sideTip.Target().left, rt.rect.right + 8, .01f), name + ": 8 DIP right of the favourite's tile"); }
            else Expect(false, name + ": the favourite's tile is laid out");
            ChromeTipHide();   // a click
            Expect(!sideTip.Shown(), name + ": a click: it goes");
            HoverOver(tile); Expect(!sideTip.Shown(), name + ": not again on the clicked item");
            HoverOver(tab0); Expect(compact ? sideTip.Shown() : sideTip.Waiting() || sideTip.Shown(), name + ": another item brings it back");
            Pump(600); chrome.bandOpen = tile; InvalidateChrome();
            Expect(!sideTip.Shown(), name + ": a menu opening: it goes");
            HoverOver(tile); Pump(600); Expect(!sideTip.Shown(), name + ": not over the item whose menu is open"); chrome.bandOpen = 0; InvalidateChrome();
            HoverOver(tab0); Pump(600); HoverNowhere(); Expect(!sideTip.Shown(), name + ": the pointer leaves the column: it fades");
            Pump(250); Expect(!sideTip.WindowShown(), name + ": gone after its 100 ms fade");
            HoverOver(NativeChrome::Chrome::SidebarToggleId); Expect(!sideTip.Shown(), name + ": another control keeps its own tooltip");
            HoverNowhere(); chromeTip.HideNow();
        }
        // The keyboard in the compact column: at once.
        settings.tabsPosition = BrowserSettings::TabsPosition::Compact; ApplyLayout(); Relayout(); sideTip.HideNow(); chromeTip.HideNow();
        ChromeTipFocus(NativeChrome::TabBase + 1); Expect(sideTip.Shown() && sideTip.Item() == NativeChrome::TabBase + 1 && !chromeTip.Visible(), "Infobulle B, compact, keyboard focus on a tab: its tip at once");
        ChromeTipFocus(0); Expect(!sideTip.Shown(), "Infobulle B, keyboard focus gone: it goes");
        // Reduced motion: a fade only (no slide, no scale).
        sideTip.HideNow(); sideTip.SetReducedMotion(true); HoverOver(NativeChrome::TabBase);
        { const auto f = sideTip.MotionForTest().At(NativeChrome::Motion::Now()); Expect(sideTip.Shown() && Near(f.dx, 0) && Near(f.scale, 1), "Infobulle B, reduced motion: no slide, no scale"); }
        sideTip.SetReducedMotion(false); HoverNowhere(); Pump(300); sideTip.HideNow();
        pages.pop_back(); settings.tabsPosition = BrowserSettings::TabsPosition::Top; ApplyLayout(); Relayout();
    }
    // ---- 8. Side Mode Focus: the toolbar menus opened by their shortcut come from the column (owner, 2026-11-26: Ctrl+J
    //      opened the Downloads menu top-right) ----
    {   CreateAudioPanel();
        auto hideColumn = [] { CloseAll(); keyboardFocus = 0; editing = false; SideFocusReset(); Relayout(); };
        for (auto layout : {BrowserSettings::TabsPosition::Compact, BrowserSettings::TabsPosition::Side}) {
            settings.tabsPosition = layout; settings.focusMode = true; ApplyLayout(); Relayout(); hideColumn();
            const std::string name = layout == BrowserSettings::TabsPosition::Compact ? "Focus compact" : "Focus complete";
            const RECT c = ClientScreen(); const LONG middle = (c.left + c.right) / 2;
            Expect(chrome.sideHidden && !sideReveal.open && !sideLive, name + ": the column hidden");
            // Téléchargements: Ctrl+J, as the page's accelerator handler sends it (Key, then the Shortcut message)
            { BYTE state[256]{}; state[VK_CONTROL] = 0x80; SetKeyboardState(state); const int cmd = Key('J'); state[VK_CONTROL] = 0; SetKeyboardState(state);
              Expect(cmd == 'D', name + ", Ctrl+J: the browser's Downloads command (not left to the engine's flyout)"); keyboardModality = true; Command(cmd); }
            Pump(300);
            Expect(downloadPanel.Visible() && sideReveal.open && sideLive, name + ", Ctrl+J: the column back at rest, the Downloads menu open");
            {   POINT at{}; const bool column = ColumnPopoverAt('D', NativeChrome::Downloads::Panel::anchorGap, at); const RECT p = Win(downloadPanel.Window());
                Expect(column && p.left == at.x && p.bottom == at.y, name + ", Ctrl+J: the menu hangs from the column's Downloads button " + Text(p));
                Expect(p.right < middle && p.top > c.top + Px(60), name + ", Ctrl+J: on the panel's side, not top-right " + Text(p)); }
            SideFocusEvaluate(); Pump(100);
            Expect(sideReveal.open, name + ", Ctrl+J: the column stays while the menu is open");
            downloadPanel.Hide(false); SyncLatch(); keyboardFocus = 0; Pump(100); SideFocusEvaluate(); Pump(500);
            Expect(!sideReveal.open, name + ", Ctrl+J: the menu closed and the pointer away, the column leaves");
            ShowDownloads(); Pump(300); ShowDownloadDrawer(); Pump(900);
            CheckDocked(name + ", Ctrl+J then « Tout afficher »", downloadDrawer.Window(), NativeChrome::Downloads::DrawerLayout::width, true);
            hideColumn();
            // Son (its shortcut / F6 then Entrée on the button)
            chrome.audioShown = true; Relayout(); hideColumn(); chrome.audioShown = true; Relayout();
            ShowAudio(true); Pump(300);
            {   const RECT a = Win(audioPanel.Window());
                Expect(audioPanel.Visible() && sideReveal.open && chrome.latched == 'V', name + ", Son by the keyboard: the column back, the menu open (it did not open at all)");
                Expect(a.right < middle, name + ", Son: on the panel's side " + Text(a)); }
            SideFocusEvaluate(); Pump(100); Expect(sideReveal.open, name + ", Son: the column stays while the menu is open");
            audioPanel.Close(false); Pump(300); chrome.audioShown = false; hideColumn();
            // The profile menu (Ctrl+Maj+M): anchored at the column's profile button
            {   const RECT pr = ProfileButtonScreen(); RECT reg{}; for (auto& r : chrome.regions) if (r.id == 900) { reg = ChromePx(r.rect); MapWindowPoints(window, nullptr, reinterpret_cast<POINT*>(&reg), 2); }
                Expect(sideReveal.open && EqualRect(&pr, &reg) && pr.left < c.left + Px(chrome.side.column), name + ", profile menu: the column back, anchored at its profile button " + Text(pr)); }
            hideColumn();
            // The star (Ctrl+D): the bookmark popover on the panel's side (complete: from the column's star, the column back)
            keyboardModality = true; Command('K'); Pump(400);
            {   RECT bp{}; GetWindowRect(bookmarkPopover.Window(), &bp);
                Expect(bookmarkPopover.Visible() && bp.right < middle && sideReveal.open, name + ", Ctrl+D: the column back, the bookmark popover on the panel's side " + Text(bp));
                if (layout == BrowserSettings::TabsPosition::Compact) Expect(bp.top >= c.top && bp.left >= c.left + Px(chrome.SideColumn().column), name + ", Ctrl+D: inside the window, beside the column (compact: no star; it went above the window) " + Text(bp));
                else { RECT k{}; for (auto& r : chrome.regions) if (r.id == 'K') { k = ChromePx(r.rect); MapWindowPoints(window, nullptr, reinterpret_cast<POINT*>(&k), 2); }
                       Expect(k.right > k.left && bp.top > k.bottom, name + ", Ctrl+D: under the column's star, as outside Focus " + Text(bp)); } }
            bookmarkPopover.Hide(); Pump(200); for (auto id : bookmarks.Matching(pages[current]->uri)) bookmarks.Remove(id); BookmarksChanged(); hideColumn();
            // Historique / Réglages by the keyboard (drawers, rule 2): the column back with them
            ShowHistory(true); Pump(600); Expect(sideReveal.open && historyDrawer.Open(), name + ", Ctrl+H: the column back with the drawer"); hideColumn();
            ShowSettings(); Pump(600); Expect(sideReveal.open && settingsDrawer.Open(), name + ", Réglages: the column back with the drawer"); hideColumn();
        }
        // The top bar's Focus is unchanged: its floating panel, the menu under it on the right.
        settings.tabsPosition = BrowserSettings::TabsPosition::Top; ApplyLayout(); Relayout();
        Expect(chrome.floating, "top bar Focus on");
        ShowDownloads(); Pump(300);
        {   const RECT p = Win(downloadPanel.Window()), c = ClientScreen(); Expect(downloadPanel.Visible() && p.right > (c.left + c.right) / 2, "top bar Focus, Ctrl+J: the menu on the right, as before " + Text(p)); }
        CloseAll(); settings.focusMode = false; Relayout();
    }
    // ---- 9. Ctrl+J written where Ctrl+H is (Designer, 2026-11-26: board 04's format, name then the key) ----
    for (auto locale : {I18n::Locale::Fr, I18n::Locale::En}) {
        I18n::SetForProcess(locale); const bool fr = locale == I18n::Locale::Fr; const std::string lang = fr ? "FR" : "EN";
        const auto tip = TipContent(TR(chrome_tip_downloads)), history = TipContent(TR(chrome_tip_history));
        Expect(tip.label == (fr ? L"Téléchargements" : L"Downloads") && tip.shortcut == L"Ctrl+J" && history.shortcut == L"Ctrl+H",
               lang + ": the Downloads tooltip is its name then the « Ctrl+J » key, as Historique's « Ctrl+H »");
        const auto label = AccessibleButton('D');
        Expect(label.name == (fr ? L"Téléchargements" : L"Downloads") && label.accelerator == L"Ctrl+J", lang + ": its accessible name stays the bare name, Ctrl+J as its accelerator");
        const NativeChrome::ShortcutsPanel::Shortcut* h = nullptr; const NativeChrome::ShortcutsPanel::Shortcut* next = nullptr;
        for (const auto& s : NativeChrome::ShortcutsPanel::Sections()) for (size_t i = 0; i < s.rows.size(); ++i) if (std::wstring(s.rows[i].label) == TR(history_title)) { h = &s.rows[i]; next = i + 1 < s.rows.size() ? &s.rows[i + 1] : nullptr; }
        Expect(h && next && std::wstring(next->label) == (fr ? L"Téléchargements" : L"Downloads") && next->keys.size() == 2 && std::wstring(next->keys[0]) == L"Ctrl" && std::wstring(next->keys[1]) == L"J",
               lang + ": Ctrl+/ panel: « " + (fr ? std::string("Téléchargements") : std::string("Downloads")) + " » Ctrl J right after « Historique » Ctrl H");
        // The tooltip as drawn (offscreen, the desktop's DPI): evidence for the Designer.
        RECT target{100, 100, 132, 132}; chromeTip.Show(tip, target);
        Expect(chromeTip.RenderForTest(base, (out + (fr ? L"\\tip-downloads-fr.png" : L"\\tip-downloads-en.png")).c_str(), GetDpiForWindow(window)), lang + ": the Downloads tooltip rendered");
        chromeTip.HideNow();
    }
    I18n::SetForProcess(I18n::Locale::Fr);
    // ---- 6. Morph rule of « Tout afficher » (pure) ----
    {   namespace DU = NativeChrome::DownloadsUI;
        Expect(DU::MorphFits({6, 0, 274, 300}, 274), "the top bar's popover over the drawer's window: it morphs");
        Expect(DU::MorphFits({8, 500, 276, 700}, 274), "the compact column's popover beside the left dock (2 px over): it morphs");
        Expect(!DU::MorphFits({-240, 400, 28, 700}, 274), "the complete column's popover over the column: the drawer slides in");
        Expect(!DU::MorphFits({900, 0, 1168, 300}, 274) && !DU::MorphFits({0, 0, 0, 0}, 274), "a popover elsewhere (side Focus's top-right one) or none: no morph");
    }
    std::printf("%s: %d checks, %d failures\n", failures ? "FAIL" : "PASS", checks, failures);
    std::fflush(stdout); TerminateProcess(GetCurrentProcess(), failures ? 1 : 0); return failures ? 1 : 0;
}
