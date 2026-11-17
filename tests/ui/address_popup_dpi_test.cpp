// The address suggestions popup and the Favoris drawer draw at the DPI of the monitor they are shown on.
// Bug (2026-11-17): created on the primary monitor (125 %) and first shown on a 100 % monitor, it kept drawing at
// 125 %: rows and text 25 % too large there, until it happened to be resized. Real monitors only: with fewer than two
// monitors at different DPIs this prints SKIP and exits 0. Usage: address_popup_dpi_test.exe <repo root>
#include <windows.h>
#include <shellscalingapi.h>
#include <cstdio>
#include <string>
#include <vector>
#include "../../src/chrome.h"
#include "../../src/address-popup.h"
#include "../../src/bookmarks-panel.h"

using namespace NativeChrome;
struct Monitor { RECT work; UINT dpi; };
static BOOL CALLBACK Collect(HMONITOR m, HDC, LPRECT, LPARAM out) {
    MONITORINFO mi{sizeof(mi)}; UINT x = 96, y = 96;
    if (GetMonitorInfoW(m, &mi) && SUCCEEDED(GetDpiForMonitor(m, MDT_EFFECTIVE_DPI, &x, &y)))
        reinterpret_cast<std::vector<Monitor>*>(out)->push_back({mi.rcWork, x});
    return TRUE;
}
static void Pump(DWORD ms) {
    const DWORD end = GetTickCount() + ms; MSG msg;
    while (GetTickCount() < end) { while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); } Sleep(10); }
}
static int failures = 0;
static void Check(bool ok, const std::string& what) { if (!ok) ++failures; std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str()); }

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) { std::printf("usage: address_popup_dpi_test <repo root>\n"); return 2; }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::vector<Monitor> monitors; EnumDisplayMonitors(nullptr, nullptr, Collect, reinterpret_cast<LPARAM>(&monitors));
    const Monitor* a = nullptr; const Monitor* b = nullptr;
    for (auto& m : monitors) { if (!a) a = &m; else if (!b && m.dpi != a->dpi) b = &m; }
    if (!a || !b) { std::printf("SKIP: needs two monitors at different DPIs (found %zu)\n", monitors.size()); CoUninitialize(); return 0; }
    // Start where the popup is created (the primary monitor), then the other one, then back.
    for (auto& m : monitors) if (m.work.left <= 0 && m.work.right > 0 && m.work.top <= 0 && m.work.bottom > 0) { if (&m != a) { const Monitor* t = a; a = &m; if (b == &m) b = t; } }
    if (a->dpi == b->dpi) for (auto& m : monitors) if (m.dpi != a->dpi) b = &m;
    std::printf("monitors: A dpi %u, B dpi %u\n", a->dpi, b->dpi);

    HINSTANCE hi = GetModuleHandleW(nullptr);
    WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = hi; wc.lpszClassName = L"AddressPopupDpiOwner"; RegisterClassW(&wc);
    HWND owner = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"owner", WS_POPUP, a->work.left + 10, a->work.top + 10, 10, 10, nullptr, nullptr, hi, nullptr);
    AddressUI::Suggestions s;
    Check(s.Create(hi, owner, argv[1], {}), "suggestions popup created");
    AddressBar::Suggestions data;
    AddressBar::Suggestion row; row.kind = AddressBar::Kind::Search; row.text = L"t.co"; data.rows = {row, row, row};

    auto showOn = [&](const Monitor& m, const char* name) {
        const int left = m.work.left + 40, top = m.work.top + 120, right = left + 600;
        s.Show(data, 0, L"Google", left, top, right, m.dpi);
        Pump(250);
        const UINT window = GetDpiForWindow(s.Window()), drawn = s.RenderDpiForTest();
        Check(window == m.dpi, std::string("shown on ") + name + ": the popup window is on that monitor (dpi " + std::to_string(window) + ")");
        Check(drawn == m.dpi, std::string("shown on ") + name + ": rows drawn at its dpi " + std::to_string(m.dpi) + " (drawn at " + std::to_string(drawn) + ")");
        s.Hide(); Pump(50);
    };
    showOn(*a, "A");
    showOn(*b, "B (after A: the reported bug)");
    showOn(*a, "A again");
    showOn(*b, "B again");

    // The Favoris drawer: created on the primary monitor, it slides out of the window it belongs to.
    WNDCLASSW bw{}; bw.lpfnWndProc = DefWindowProcW; bw.hInstance = hi; bw.lpszClassName = L"AddressPopupDpiBrowser"; RegisterClassW(&bw);
    Bookmarks::Store store;
    auto drawerOn = [&](const Monitor& m, const char* name) {
        HWND browser = CreateWindowExW(0, bw.lpszClassName, L"browser", WS_POPUP | WS_VISIBLE, m.work.left + 40, m.work.top + 40, 900, 700, nullptr, nullptr, hi, nullptr);
        BookmarksUI::Drawer d; Check(d.Create(hi, browser, argv[1], &store, {}), std::string("Favoris drawer created for a window on ") + name);
        d.Show(false); Pump(600);
        const UINT window = GetDpiForWindow(d.Window()), drawn = d.RenderDpiForTest();
        Check(window == m.dpi, std::string("Favoris drawer on ") + name + ": on that monitor (dpi " + std::to_string(window) + ")");
        Check(drawn == m.dpi, std::string("Favoris drawer on ") + name + ": drawn at its dpi " + std::to_string(m.dpi) + " (drawn at " + std::to_string(drawn) + ")");
        d.Hide(false, false); d.Dispose(); DestroyWindow(browser); Pump(50);
    };
    drawerOn(*b, "B");
    drawerOn(*a, "A");

    s.Dispose(); DestroyWindow(owner); CoUninitialize();
    std::printf("\n%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
