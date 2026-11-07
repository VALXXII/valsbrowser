// Interactive native preview of the update toast. MOCK flow only: no network, no
// installer, no WebView2, no profile. A plain child window stands in for page content
// so the overlay is shown above a child HWND exactly as in the browser.
#include "update-toast.h"
#include <cstdio>
#include <string>

using namespace BrowserUpdate;

static HWND window = nullptr, page = nullptr, status = nullptr;
static Toast toast;
static View view;
static bool light = false, reduced = false, drawer = false;
static std::wstring lastAction = L"none";
static const std::uint64_t MockTotal = 200ull * 1048576; // mock size, labelled MOCK in the window
enum : int { IdAvailable = 100, IdDownloading, IdUnknown, IdReady, IdInstalling, IdInstalled, IdError, IdHide,
             IdReduced, IdLight, IdDrawer, IdFocus };
static const UINT_PTR MockTimer = 7, InstallTimer = 8;

static float Scale() { return GetDpiForWindow(window) / 96.f; }
static int Px(float dip) { return (int)(dip * Scale() + .5f); }
static const wchar_t* Name(Action a) {
    switch (a) { case Action::Download: return L"Download"; case Action::Cancel: return L"Cancel";
    case Action::RestartInstall: return L"RestartInstall"; case Action::Retry: return L"Retry"; case Action::Dismiss: return L"Dismiss"; }
    return L"?";
}
static void Status() {
    wchar_t text[256];
    std::swprintf(text, 256, L"MOCK PREVIEW (no network / installer / WebView2) — last toast action: %ls — F6 focuses the toast, Esc returns", lastAction.c_str());
    SetWindowTextW(status, text);
}
static void Show(View v) {
    view = v;
    KillTimer(window, InstallTimer);
    if (v.phase != Phase::Downloading) KillTimer(window, MockTimer);
    toast.SetView(view);
}
static void StartMockDownload(bool unknown) {
    View v; v.phase = Phase::Downloading; v.received = 0; v.total = unknown ? 0 : MockTotal; v.secondsRemaining = -1;
    Show(v);
    SetTimer(window, MockTimer, 100, nullptr);
}
static void OnAction(Action a) {
    lastAction = Name(a); Status();
    switch (a) {
    case Action::Download: case Action::Retry: StartMockDownload(false); break;
    case Action::Cancel: { View v; v.phase = Phase::Available; Show(v); break; }
    case Action::RestartInstall: { View v; v.phase = Phase::Installing; Show(v); SetTimer(window, InstallTimer, 1500, nullptr); break; }
    case Action::Dismiss: { View v; Show(v); break; }
    }
}
static void LayoutChildren() {
    RECT r; GetClientRect(window, &r);
    const int x0 = Px(8), h = Px(26), gap = Px(6);
    int x = x0, y = Px(8);
    for (int id = IdAvailable; id <= IdFocus; ++id) {
        HWND b = GetDlgItem(window, id);
        const int w = Px(id >= IdReduced && id <= IdDrawer ? 112.f : 80.f);
        if (x + w > r.right - x0 && x > x0) { x = x0; y += h + gap; }
        SetWindowPos(b, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
        x += w + gap;
    }
    SetWindowPos(status, nullptr, x0, y + h + Px(6), r.right - 2 * x0, Px(18), SWP_NOZORDER | SWP_NOACTIVATE);
    // Page content area: same insets as the browser's WebView2 bounds {4,84,w-4,h-4}.
    SetWindowPos(page, nullptr, Px(4), Px(84), r.right - Px(8), r.bottom - Px(88), SWP_NOZORDER | SWP_NOACTIVATE);
    toast.SetReservedRight(drawer ? 270.f : 0.f);
    toast.Layout();
}
static LRESULT CALLBACK PageProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_PAINT) {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT r; GetClientRect(h, &r);
        HBRUSH bg = CreateSolidBrush(light ? RGB(245, 245, 245) : RGB(23, 23, 23)); FillRect(dc, &r, bg); DeleteObject(bg);
        HBRUSH ink = CreateSolidBrush(light ? RGB(60, 60, 60) : RGB(48, 48, 48));
        const float s = GetDpiForWindow(h) / 96.f;
        for (int y = (int)(24 * s), row = 0; y < r.bottom; y += (int)(22 * s), ++row)
            for (int x = (int)(24 * s), n = 0; x < r.right - (int)(24 * s); ++n) {
                const int w2 = (int)((30 + (row * 13 + n * 29) % 70) * s);
                RECT word{x, y, x + w2, y + (int)(9 * s)}; FillRect(dc, &word, ink); x += w2 + (int)(8 * s);
            }
        if (drawer) { RECT d{r.right - (int)(270 * s), 0, r.right, r.bottom}; HBRUSH db = CreateSolidBrush(RGB(23, 23, 23)); FillRect(dc, &d, db); DeleteObject(db); }
        DeleteObject(ink);
        EndPaint(h, &ps); return 0;
    }
    if (m == WM_LBUTTONDOWN) { SetFocus(h); return 0; }
    if (m == WM_KEYDOWN && w == VK_F6) { toast.Focus(); return 0; }
    return DefWindowProcW(h, m, w, l);
}
static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        window = h;
        const wchar_t* labels[] = {L"Available", L"Downloading", L"Unknown size", L"Ready", L"Installing", L"Installed", L"Error", L"Hide",
                                   L"Reduced motion", L"Light page", L"Drawer open", L"Focus (F6)"};
        for (int i = 0; i < 12; ++i) {
            const int id = IdAvailable + i;
            const DWORD style = WS_CHILD | WS_VISIBLE | (id >= IdReduced && id <= IdDrawer ? BS_AUTOCHECKBOX : BS_PUSHBUTTON);
            CreateWindowW(L"BUTTON", labels[i], style, 0, 0, 10, 10, h, (HMENU)(INT_PTR)id, nullptr, nullptr);
        }
        status = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, h, nullptr, nullptr, nullptr);
        page = CreateWindowW(L"ToastPreviewPage", L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 10, 10, h, nullptr, nullptr, nullptr);
        return 0;
    }
    case WM_COMMAND: {
        View v;
        switch (LOWORD(w)) {
        case IdAvailable: v.phase = Phase::Available; Show(v); break;
        case IdDownloading: StartMockDownload(false); break;
        case IdUnknown: StartMockDownload(true); break;
        case IdReady: v.phase = Phase::Ready; Show(v); break;
        case IdInstalling: v.phase = Phase::Installing; Show(v); break;
        case IdInstalled: v.phase = Phase::Installed; Show(v); break; // mock success event
        case IdError: v.phase = Phase::Error; v.error = L"MOCK: le serveur de mises à jour est injoignable."; Show(v); break;
        case IdHide: Show(v); break;
        case IdReduced: {
            reduced = SendMessageW((HWND)l, BM_GETCHECK, 0, 0) == BST_CHECKED;
            ToastOptions o; o.forceReducedMotion = reduced ? 1 : 0;
            wchar_t path[MAX_PATH]; GetModuleFileNameW(nullptr, path, MAX_PATH);
            std::wstring root = path; root = root.substr(0, root.find_last_of(L'\\'));
            std::wstring assets = root + L"\\..\\..\\assets";
            toast.Init(h, assets, OnAction, o);
            toast.SetFocusReleaseHandler([] { SetFocus(page); });
            LayoutChildren(); toast.SetView(view);
            break;
        }
        case IdLight: light = SendMessageW((HWND)l, BM_GETCHECK, 0, 0) == BST_CHECKED; InvalidateRect(page, nullptr, TRUE); break;
        case IdDrawer: drawer = SendMessageW((HWND)l, BM_GETCHECK, 0, 0) == BST_CHECKED; InvalidateRect(page, nullptr, TRUE); LayoutChildren(); break;
        case IdFocus: toast.Focus(); break;
        }
        if (LOWORD(w) != IdFocus) SetFocus(page);
        return 0;
    }
    case WM_TIMER:
        if (w == MockTimer && view.phase == Phase::Downloading) {
            View v = view;
            v.received += 2621440; // MOCK: 2.5 MB per 100 ms
            if (v.total && v.received >= v.total) { View r; r.phase = Phase::Ready; Show(r); return 0; }
            if (!v.total && v.received >= 40ull * 1048576) { View r; r.phase = Phase::Ready; Show(r); return 0; }
            v.secondsRemaining = v.total ? (double)(v.total - v.received) / (2621440.0 * 10) : -1;
            view = v; toast.SetView(view);
            return 0;
        }
        if (w == InstallTimer) { KillTimer(h, InstallTimer); View v; v.phase = Phase::Installed; Show(v); return 0; } // MOCK success
        return 0;
    case WM_SIZE:
        if (w == SIZE_MINIMIZED) { toast.SetOwnerVisible(false); return 0; }
        toast.SetOwnerVisible(true); LayoutChildren(); return 0;
    case WM_MOVE: case WM_EXITSIZEMOVE: toast.Layout(); return 0;
    case WM_DPICHANGED: {
        const RECT* r = (const RECT*)l;
        SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        LayoutChildren(); return 0;
    }
    case WM_CTLCOLORSTATIC: { HDC dc = (HDC)w; SetTextColor(dc, RGB(200, 200, 200)); SetBkColor(dc, RGB(23, 23, 23)); return (LRESULT)GetStockObject(BLACK_BRUSH); }
    case WM_ERASEBKGND: { RECT r; GetClientRect(h, &r); HBRUSH b = CreateSolidBrush(RGB(23, 23, 23)); FillRect((HDC)w, &r, b); DeleteObject(b); return 1; }
    case WM_DESTROY: toast.Close(); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;
    WNDCLASSW pc{}; pc.lpfnWndProc = PageProc; pc.hInstance = instance; pc.lpszClassName = L"ToastPreviewPage"; pc.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
    RegisterClassW(&pc);
    WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = instance; wc.lpszClassName = L"ToastPreview"; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    const UINT dpi = GetDpiForSystem();
    HWND h = CreateWindowW(wc.lpszClassName, L"Update toast preview — MOCK flow (no network, no installer)",
                           WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, MulDiv(1180, dpi, 96), MulDiv(815, dpi, 96),
                           nullptr, nullptr, instance, nullptr);
    if (!h) return 2;
    wchar_t path[MAX_PATH]; GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring root = path; root = root.substr(0, root.find_last_of(L'\\'));
    // build/update-toast/preview.exe -> repo assets; a copy next to browser.exe can pass its own folder.
    std::wstring assets = root + L"\\..\\..\\assets";
    if (GetFileAttributesW((assets + L"\\fonts\\InterDisplay-Medium.ttf").c_str()) == INVALID_FILE_ATTRIBUTES) assets = root + L"\\assets";
    if (!toast.Init(h, assets, OnAction)) { MessageBoxW(h, L"Toast init failed (fonts not found).", L"preview", MB_ICONERROR); return 3; }
    toast.SetFocusReleaseHandler([] { SetFocus(page); });
    Status(); LayoutChildren();
    ShowWindow(h, show); UpdateWindow(h);
    View v; v.phase = Phase::Available; Show(v);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    CoUninitialize();
    return 0;
}
