// Colour emoji in the real bookmark surfaces (design/emoji-rendering-report.md): the production
// window procedure, band, native folder menus (nested), the" overflow menu at 560 DIP and the Favoris
// drawer, on this thread with synthetic titles only (no profile is read or written). Each window is
// captured on its own (PrintWindow, PW_RENDERFULLCONTENT): no screen capture, no input injection, no
// foreground change. A capture that comes back black is reported UNAVAILABLE, never compared.
// Pixel rule: a "colour" pixel has a chroma (max - min channel) above 32; the chrome is greyscale
// everywhere else (white text and icons on #171717 / #292929), so only colour glyphs produce them.
// Test-unit shims (production code unchanged, as in bookmark_rename_test.cpp): the production drawer
// asks for the foreground, recorded and dropped here; GetDpiForWindow answers fixtureDpi so the same
// window is checked at 96 / 144 / 192 DPI without touching any display setting.
// Usage: bookmark_emoji_ui_test.exe <repo root> <output dir>
#include <windows.h>
static int foregroundRequests = 0;
static BOOL FixtureSetForegroundWindow(HWND) { ++foregroundRequests; return TRUE; }
static UINT fixtureDpi = 0;
static UINT FixtureGetDpiForWindow(HWND h) { return fixtureDpi ? fixtureDpi : GetDpiForWindow(h); }
#define SetForegroundWindow FixtureSetForegroundWindow
#define GetDpiForWindow FixtureGetDpiForWindow
#include "../../src/main.cpp"
#undef SetForegroundWindow
#undef GetDpiForWindow
#include <cstdio>
#include <thread>

static int failures = 0, unavailable = 0;
static void Expect(bool ok, const std::string& what) { std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) ++failures; }
constexpr UINT RunMessage = WM_APP + 60, PostRunMessage = WM_APP + 61;
static HWND helper = nullptr; static std::wstring out;
static LRESULT CALLBACK HelperProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    if (msg == RunMessage) { (*reinterpret_cast<std::function<void()>*>(l))(); return 0; }
    if (msg == PostRunMessage) { auto* f = reinterpret_cast<std::function<void()>*>(l); (*f)(); delete f; return 0; }
    return DefWindowProcW(h, msg, w, l);
}
static void OnUi(std::function<void()> f) { SendMessageW(helper, RunMessage, 0, (LPARAM)&f); }
static void Later(std::function<void()> f) { PostMessageW(helper, PostRunMessage, 0, (LPARAM) new std::function<void()>(std::move(f))); }
template <class F> static bool WaitFor(F condition, int ms = 4000) { for (int waited = 0; waited < ms; waited += 20) { if (condition()) return true; Sleep(20); } return condition(); }
static int Depth() { int d = 0; OnUi([&] { d = contextMenu.Depth(); }); return d; }

struct Shot { bool ok = false, black = true; int colored = 0, ink = 0; };
// Own-window capture -> PNG + pixel statistics (UI thread).
static Shot Capture(HWND h, const std::wstring& name) {
    Shot s; Sleep(700);
    bookmarksDrawer.Frame(NativeChrome::Motion::Now());
    wchar_t cls[80]{}; GetClassNameW(h, cls, 80);
    if (std::wstring(cls) == L"LisiereContextMenu") SendMessageW(h, WM_TIMER, 1, 0);
    RedrawWindow(h, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    RECT r{}; if (!h || !GetWindowRect(h, &r)) return s;
    const int w = r.right - r.left, hh = r.bottom - r.top; if (w <= 0 || hh <= 0) return s;
    HDC screen = GetDC(nullptr), dc = CreateCompatibleDC(screen); BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(info.bmiHeader); info.bmiHeader.biWidth = w; info.bmiHeader.biHeight = -hh; info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    void* bits = nullptr; HBITMAP bmp = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0); auto old = SelectObject(dc, bmp);
    s.ok = PrintWindow(h, dc, 2 /*PW_RENDERFULLCONTENT*/) != FALSE; SelectObject(dc, old);
    if (s.ok && bits) {
        const auto* p = static_cast<const BYTE*>(bits);
        for (int i = 0; i < w * hh; ++i) { const int b = p[4 * i], g = p[4 * i + 1], rr = p[4 * i + 2], mx = std::max({rr, g, b}), mn = std::min({rr, g, b}); if (mx > 0) s.black = false; if (mx - mn > 32) ++s.colored; if (mx > 60) ++s.ink; }
        IWICImagingFactory* wic = nullptr; IWICBitmap* bitmap = nullptr; IWICStream* stream = nullptr; IWICBitmapEncoder* enc = nullptr; IWICBitmapFrameEncode* frame = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) && SUCCEEDED(wic->CreateBitmapFromHBITMAP(bmp, nullptr, WICBitmapIgnoreAlpha, &bitmap)) &&
            SUCCEEDED(wic->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromFilename((out + L"\\" + name + L".png").c_str(), GENERIC_WRITE)) && SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) &&
            SUCCEEDED(enc->Initialize(stream, WICBitmapEncoderNoCache)) && SUCCEEDED(enc->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) && SUCCEEDED(frame->WriteSource(bitmap, nullptr))) { frame->Commit(); enc->Commit(); }
        if (frame) frame->Release(); if (enc) enc->Release(); if (stream) stream->Release(); if (bitmap) bitmap->Release(); if (wic) wic->Release();
    }
    DeleteObject(bmp); DeleteDC(dc); ReleaseDC(nullptr, screen);
    return s;
}
// A surface must show colour emoji; a black or failed capture is UNAVAILABLE (not a pass, not a fail).
static void ExpectColour(const Shot& s, const char* surface) {
    char line[200];
    if (!s.ok || s.black) { std::printf("UNAVAILABLE %s: own-window capture %s, not compared\n", surface, s.ok ? "black" : "failed"); ++unavailable; return; }
    std::snprintf(line, sizeof line, "%s: %d colour px (emoji in colour), %d ink px", surface, s.colored, s.ink);
    Expect(s.colored >= 150 && s.ink > 0, line);   // pre-fix: 0 (the drawer's blue caret alone: ~40)
}

static BM::Id travail, projets;
static void Seed() {
    bookmarks = BM::Store{}; const std::int64_t t = 1794100000000;
    // Synthetic titles: the user's screenshot (sun FE0F, bare sun, tools), sequences, plain text.
    bookmarks.Add(BM::BarRoot, BM::End, L"Accueil \u2600\uFE0F \u00E9t\u00E9", L"https://example.com/accueil", t);
    travail = bookmarks.AddFolder(BM::BarRoot, BM::End, L"Travail \U0001F6E0\uFE0F", t);
    bookmarks.Add(travail, BM::End, L"Accueil \u2600 \u00E9t\u00E9 (texte)", L"https://example.com/texte", t);
    bookmarks.Add(travail, BM::End, L"Notes \U0001F4DD", L"https://example.com/notes", t);
    bookmarks.Add(travail, BM::End, L"\u00C9quipe \U0001F469\u200D\U0001F4BB\U0001F44D\U0001F3FD", L"https://example.com/equipe", t);
    projets = bookmarks.AddFolder(travail, BM::End, L"Projets \U0001F680", t);
    bookmarks.Add(projets, BM::End, L"Lancement \U0001F680 #\uFE0F\u20E3", L"https://example.com/lancement", t);
    bookmarks.Add(BM::BarRoot, BM::End, L"Perso", L"https://example.com/perso", t);
    for (auto* n : {L"Lecture \U0001F4DA", L"Musique \U0001F3B5", L"Sport \u26BD", L"Cuisine \U0001F373", L"Voyage \u2708\uFE0F", L"Jardin \U0001F33B", L"Cin\u00E9ma \U0001F3AC"})
        bookmarks.Add(BM::BarRoot, BM::End, n, L"https://example.com/" + std::to_wstring(std::wcslen(n)), t);
    bookmarks.Add(BM::OtherRoot, BM::End, L"Autre \u2B50", L"https://example.com/autre", t);
}
static void Resize(int dip) { SetWindowPos(window, nullptr, 0, 0, S((float)dip), S(815), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE); Layout(); }

static void Surfaces(UINT dpi) {
    const std::wstring at = L"-" + std::to_wstring(dpi); const std::string tag = " @" + std::to_string(dpi) + " DPI";
    OnUi([dpi] { fixtureDpi = dpi; UpdateFont(); chrome.RecreateTarget(); Resize(1180); });
    OnUi([&] { ExpectColour(Capture(window, L"window-band-1180" + at), ("window band (Chrome::Text, production paint)" + tag).c_str()); });
    // Folder menu "Travail" (band index 1), then its nested "Projets" submenu.
    int index = -1; OnUi([&] { for (int i = 0; i < (int)chrome.bandItems.size(); ++i) if (chrome.bandItems[i].id == travail) index = i; });
    Expect(index >= 0, "Travail folder is on the band");
    Later([index] { FolderMenu(index, true); });
    if (WaitFor([] { return Depth() == 1; })) {
        HWND menu = nullptr; OnUi([&] { menu = contextMenu.LevelWindow(0); ExpectColour(Capture(menu, L"menu-folder" + at), ("folder menu rows (Chrome::Text)" + tag).c_str()); });
        PostMessageW(menu, WM_KEYDOWN, VK_UP, 0); PostMessageW(menu, WM_KEYDOWN, VK_RIGHT, 0);   // last row: the subfolder
        if (WaitFor([] { return Depth() == 2; })) { OnUi([&] { ExpectColour(Capture(contextMenu.LevelWindow(1), L"menu-submenu" + at), ("nested submenu rows" + tag).c_str()); }); PostMessageW(contextMenu.LevelWindow(1), WM_KEYDOWN, VK_ESCAPE, 0); WaitFor([] { return Depth() == 1; }); }
        else Expect(false, "keyboard opens the nested submenu");
        PostMessageW(menu, WM_KEYDOWN, VK_ESCAPE, 0); Expect(WaitFor([] { return Depth() == 0; }), "Escape closes the folder menu");
    } else Expect(false, "folder menu opens");
    //" overflow at 560 DIP: the emoji items that no longer fit.
    OnUi([] { Resize(560); });
    Later([] { OverflowMenu(true); });
    if (WaitFor([] { return Depth() == 1; })) {
        HWND menu = nullptr; OnUi([&] { menu = contextMenu.LevelWindow(0); ExpectColour(Capture(menu, L"menu-overflow-560" + at), ("overflow menu at 560 DIP" + tag).c_str()); });
        PostMessageW(menu, WM_KEYDOWN, VK_ESCAPE, 0); WaitFor([] { return Depth() == 0; });
    } else Expect(false, "overflow menu opens");
    OnUi([] { Resize(1180); });
    // Favoris drawer.
    OnUi([&] { ShowBookmarksDrawer(true); ExpectColour(Capture(bookmarksDrawer.Window(), L"drawer" + at), ("Favoris drawer rows (Chrome::Text)" + tag).c_str()); bookmarksDrawer.Hide(false, false); });
}
static void Client() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    for (UINT dpi : {96u, 144u, 192u}) Surfaces(dpi);
    OnUi([] { fixtureDpi = 0; });
    std::printf("     production foreground requests recorded and dropped: %d\n", foregroundRequests);
    // Stored titles are never rewritten by drawing.
    OnUi([] { const auto* n = bookmarks.Find(travail); Expect(n && n->title == L"Travail \U0001F6E0\uFE0F", "stored title unchanged (UTF-16 with its FE0F)"); });
    PostThreadMessageW(GetWindowThreadProcessId(helper, nullptr), WM_QUIT, 0, 0);
    CoUninitialize();
}

int wmain(int argc, wchar_t** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 3) { std::fputs("usage: bookmark_emoji_ui_test <root> <out>\n", stderr); return 2; }
    base = argv[1]; out = argv[2]; dataDir = out + L"\\profile-unused";
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2); CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); instance = GetModuleHandleW(nullptr);
    smoke = true;   // no profile, settings or session file is read or written
    for (auto name : NativeChrome::FontFiles) AddFontResourceExW((base + L"\\assets\\fonts\\" + name).c_str(), FR_PRIVATE, nullptr);
    WNDCLASSW hc{}; hc.lpfnWndProc = HelperProc; hc.hInstance = instance; hc.lpszClassName = L"BookmarkEmojiTestHelper"; RegisterClassW(&hc);
    helper = CreateWindowW(hc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, nullptr);
    WNDCLASSW wc{}; wc.lpfnWndProc = WndProc; wc.hInstance = instance; wc.lpszClassName = L"BookmarkEmojiTest"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); RegisterClassW(&wc);
    CreateWindowExW(WS_EX_NOACTIVATE, wc.lpszClassName, L"browser", WS_POPUP | WS_CLIPCHILDREN, 40, 40, MulDiv(1180, GetDpiForSystem(), 96), MulDiv(815, GetDpiForSystem(), 96), nullptr, nullptr, instance, nullptr);
    if (!window || FAILED(chrome.Init(window, base))) { std::fputs("FAIL: window\n", stderr); return 1; }
    motion.SetReducedMotion(true); chrome.SetMotion(&motion); contextMenu.Init(instance, window, base); UpdateFont(); CreateBookmarkSurfaces();
    Seed();
    auto t = std::make_shared<Tab>(); t->title = L"Accueil \u2600\uFE0F"; t->uri = L"https://example.com/"; t->home = false; pages = {t}; current = 0;
    ShowWindow(window, SW_SHOWNOACTIVATE); Layout(); Update(); UpdateWindow(window);
    std::thread client(Client);
    MSG msg; while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    client.join();
    bookmarksDrawer.Hide(false, false); DestroyWindow(window); DestroyWindow(helper);
    std::printf("%d failure(s), %d surface(s) UNAVAILABLE\n", failures, unavailable);
    return failures ? 1 : 0;
}
