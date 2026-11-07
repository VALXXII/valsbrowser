// Records the real popover -> drawer morph as Windows composes it.
// Usage: morph_capture.exe <browser folder> <output folder>
// Launches browser.exe (its folder needs `session` on a white page and a
// `downloads` history), opens the popover and clicks "View all" with posted
// messages (the user's pointer never moves), then grabs the composed desktop
// around the window as fast as possible for 700 ms and reports every frame
// where the page shows through where the panel should be.
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <wincodec.h>
#include <cstdio>
#include <string>
#include <vector>
#include <cstdlib>

struct Shot { double ms; int w, h; std::vector<unsigned char> bgra; };

static HWND FindByClass(const wchar_t* cls, DWORD pid) {
    struct Ctx { const wchar_t* cls; DWORD pid; HWND found; } ctx{cls, pid, nullptr};
    EnumWindows([](HWND h, LPARAM p) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(p); wchar_t name[64]; DWORD owner = 0;
        GetWindowThreadProcessId(h, &owner); GetClassNameW(h, name, 64);
        if (owner == c->pid && wcscmp(name, c->cls) == 0) { c->found = h; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
    return ctx.found;
}
static void Click(HWND h, int x, int y) {
    const LPARAM at = MAKELPARAM(x, y);
    PostMessageW(h, WM_MOUSEMOVE, 0, at); PostMessageW(h, WM_LBUTTONDOWN, MK_LBUTTON, at); PostMessageW(h, WM_LBUTTONUP, 0, at);
}
static bool SavePng(const std::wstring& path, const Shot& s) {
    IWICImagingFactory* wic = nullptr; IWICBitmapEncoder* enc = nullptr; IWICBitmapFrameEncode* frame = nullptr; IWICStream* stream = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
    if (SUCCEEDED(hr)) hr = wic->CreateStream(&stream);
    if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
    if (SUCCEEDED(hr)) hr = wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
    if (SUCCEEDED(hr)) hr = enc->Initialize(stream, WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr)) hr = enc->CreateNewFrame(&frame, nullptr);
    if (SUCCEEDED(hr)) hr = frame->Initialize(nullptr);
    if (SUCCEEDED(hr)) hr = frame->SetSize(s.w, s.h);
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&format);
    if (SUCCEEDED(hr)) hr = frame->WritePixels(s.h, s.w * 4, static_cast<UINT>(s.bgra.size()), const_cast<BYTE*>(s.bgra.data()));
    if (SUCCEEDED(hr)) hr = frame->Commit();
    if (SUCCEEDED(hr)) hr = enc->Commit();
    if (frame) frame->Release(); if (enc) enc->Release(); if (stream) stream->Release(); if (wic) wic->Release();
    return SUCCEEDED(hr);
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) return 2;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::wstring folder = argv[1], out = argv[2], exe = folder + L"\\browser.exe";
    STARTUPINFOW si{sizeof(si)}; PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), nullptr, nullptr, nullptr, FALSE, 0, nullptr, folder.c_str(), &si, &pi)) { std::fprintf(stderr, "launch failed\n"); return 3; }
    AllowSetForegroundWindow(pi.dwProcessId);
    HWND main = nullptr;
    for (int i = 0; i < 100 && !main; ++i) { Sleep(100); main = FindByClass(L"LisiereBrowser", pi.dwProcessId); }
    if (!main) { std::fprintf(stderr, "no window\n"); TerminateProcess(pi.hProcess, 1); return 4; }
    SetForegroundWindow(main);
    Sleep(3500); // WebView2 + white page
    const float scale = GetDpiForWindow(main) / 96.f;
    RECT client; GetClientRect(main, &client);
    auto px = [scale](float dip) { return static_cast<int>(dip * scale + .5f); };
    const float width = client.right / scale;
    Click(main, px(width - 85), px(60)); // downloads button
    Sleep(500);
    HWND popover = FindByClass(L"LisiereDownloads", pi.dwProcessId);
    if (!popover || !IsWindowVisible(popover)) { std::fprintf(stderr, "popover did not open\n"); PostMessageW(main, WM_CLOSE, 0, 0); return 5; }
    RECT pr; GetWindowRect(popover, &pr);
    // Capture region: the right 300 DIP of the client, from the toolbar down.
    POINT origin{client.right - px(300), px(76)}; ClientToScreen(main, &origin);
    const int w = px(300), h = std::min<int>(px(600), client.bottom - px(76));
    HDC screen = GetDC(nullptr), mem = CreateCompatibleDC(screen);
    BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(bi.bmiHeader); bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr; HBITMAP dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0); SelectObject(mem, dib);
    LARGE_INTEGER freq, t0, t; QueryPerformanceFrequency(&freq);
    std::vector<Shot> shots;
    auto grab = [&] {
        BitBlt(mem, 0, 0, w, h, screen, origin.x, origin.y, SRCCOPY | CAPTUREBLT); GdiFlush();
        QueryPerformanceCounter(&t);
        Shot s{(t.QuadPart - t0.QuadPart) * 1000.0 / freq.QuadPart, w, h, std::vector<unsigned char>(static_cast<unsigned char*>(bits), static_cast<unsigned char*>(bits) + w * h * 4)};
        for (size_t i = 3; i < s.bgra.size(); i += 4) s.bgra[i] = 255;
        shots.push_back(std::move(s));
    };
    QueryPerformanceCounter(&t0); grab();
    // "View all": popover-local DIP (16 + 4, 244 + 8)
    Click(popover, px(24), (pr.bottom - pr.top) - px(24)); // "View all" sits 16 + 8 DIP above the bottom
    do { grab(); } while (shots.back().ms < 700);
    // Panel area (popover rectangle in capture coordinates): count "page" pixels
    // (the white page is > 200) inside it on every frame.
    // Probe strip: popover-local DIP x 230..262 (right of every glyph, canvas only).
    const int left = pr.left - origin.x + px(230), top = pr.top - origin.y + px(40), right = pr.left - origin.x + px(262), bottom = pr.bottom - origin.y - px(4);
    int bad = 0, unique = 0; std::vector<unsigned char> previous;
    for (size_t i = 0; i < shots.size(); ++i) {
        auto& s = shots[i];
        if (s.bgra == previous) continue; // only composed frames that changed
        previous = s.bgra; ++unique;
        int page = 0;
        for (int y = std::max(0, top); y < std::min(h, bottom); ++y)
            for (int x = std::max(0, left); x < std::min(w, right); ++x) { const unsigned char* p = &s.bgra[(y * w + x) * 4]; if (std::abs(p[0] - 34) <= 2 && std::abs(p[1] - 34) <= 2 && std::abs(p[2] - 34) <= 2) ++page; /* example.com (dark) shows through */ }
        wchar_t name[128]; std::swprintf(name, 128, L"%ls\\cap-%03d-%06.1fms%ls.png", out.c_str(), unique, s.ms, page > 1000 ? L"-HOLE" : L"");
        if (!SavePng(name, s)) std::wprintf(L"save failed %ls\n", name);
        if (page > 1000) { ++bad; std::wprintf(L"frame %d at %.1f ms: %d page pixels inside the panel area\n", unique, s.ms, page); }
    }
    HWND drawer = FindByClass(L"LisiereDownloadsDrawer", pi.dwProcessId);
    if (drawer) {
        RECT wr; GetWindowRect(drawer, &wr); HRGN rgn = CreateRectRgn(0, 0, 0, 0); RECT box{};
        const int type = GetWindowRgn(drawer, rgn); GetRgnBox(rgn, &box); DeleteObject(rgn);
        RECT mr; GetWindowRect(main, &mr);
        std::wprintf(L"drawer window %ld,%ld %ldx%ld | region type %d box %ld,%ld-%ld,%ld | main %ld,%ld-%ld,%ld | capture origin %ld,%ld\n",
            wr.left, wr.top, wr.right - wr.left, wr.bottom - wr.top, type, box.left, box.top, box.right, box.bottom, mr.left, mr.top, mr.right, mr.bottom, origin.x, origin.y);
    }
    std::wprintf(L"popover visible %d, drawer visible %d, foreground is browser %d\n", IsWindowVisible(popover), drawer && IsWindowVisible(drawer), GetForegroundWindow() == drawer || GetForegroundWindow() == main);
    std::wprintf(L"%zu grabs, %d distinct frames, %d with the page showing through\n", shots.size(), unique, bad);
    PostMessageW(main, WM_CLOSE, 0, 0);
    WaitForSingleObject(pi.hProcess, 5000);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return bad ? 1 : 0;
}
