// Host integration: the production main.cpp startup path (StartEngine, recovery dialog, installer
// worker, session restore, close) against a FAKE WebView2Loader.dll (fake_loader.cpp) and FAKE
// install effects. No real runtime, no network, no installer, no user profile.
//   host_test.exe <base dir with fake loader + assets> <data dir> <scenario>
// Scenarios: recover, loader-missing, denied, close-during-install, relaunch, not-detected.
#define wWinMain BrowserEntryNeverCalled
#include "../../src/main.cpp"
#undef wWinMain
#include <cstdio>

static int failures = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++failures; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static std::wstring data;
static bool Pump(const std::function<bool()>& until, DWORD ms) {
    const ULONGLONG end = GetTickCount64() + ms;
    for (;;) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { if (msg.message == WM_QUIT) return until(); TranslateMessage(&msg); DispatchMessageW(&msg); }
        if (until()) return true;
        if (GetTickCount64() > end) return false;
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
    }
}
static void Mode(const char* m) { std::ofstream(std::filesystem::path(base + L"\\fake-loader-mode.txt")) << m; }
static int Calls(const char* what) {
    std::ifstream in(std::filesystem::path(base + L"\\fake-loader-calls.txt")); std::string line; int n = 0;
    while (std::getline(in, line)) if (line == what) ++n;
    return n;
}
static std::string SessionBytes() { std::ifstream in(std::filesystem::path(sessionFile), std::ios::binary); std::stringstream s; s << in.rdbuf(); return s.str(); }
static bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

struct FakeInstall { std::atomic<int> starts{0}; std::function<void()> onLaunch; bool waitForCancel = false; };
static FakeInstall fake;
class OkFetch : public WI::Fetch {
public:
    WI::FetchResult Get(const std::wstring&, std::uint64_t, const std::function<bool(const void*, std::size_t)>& sink,
                        const std::function<void(std::uint64_t, std::uint64_t)>& progress, const std::atomic<bool>&) override {
        static const char body[] = "MZ fake bootstrapper bytes";
        sink(body, sizeof(body)); progress(sizeof(body), sizeof(body));
        WI::FetchResult r; r.status = WI::FetchResult::Ok; r.code = 200; return r;
    }
};
static WI::Effects FakeEffects() {
    ++fake.starts;
    WI::Effects e; e.fetch = std::make_shared<OkFetch>(); e.tempParent = data;
    e.verify = [](HANDLE, const std::wstring&) { WI::TrustResult t; t.status = WI::TrustResult::Ok; t.signer = L"Microsoft Corporation"; return t; };
    e.launch = [](const std::wstring&, const std::wstring&, HANDLE cancel, DWORD) {
        WI::LaunchResult l;
        if (fake.waitForCancel) { l.status = WaitForSingleObject(cancel, 10000) == WAIT_OBJECT_0 ? WI::LaunchResult::Cancelled : WI::LaunchResult::Exited; return l; }
        Sleep(50);
        if (fake.onLaunch) fake.onLaunch();
        l.status = WI::LaunchResult::Exited; l.code = 0; return l;
    };
    return e;
}

// Evidence: the dialog window's own rendering (PrintWindow), saved as a 32-bit BMP in the data dir.
static void Shot(const wchar_t* name) {
    HWND h = modal.Window(); if (!h || !IsWindowVisible(h)) return;
    Pump([] { return false; }, 400);   // let the 150 ms entrance finish
    RedrawWindow(h, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW); DwmFlush();
    RECT r; GetWindowRect(h, &r); const int w = r.right - r.left, hh = r.bottom - r.top;
    BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(bi.bmiHeader); bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = hh; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr; HDC screen = GetDC(nullptr), mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0); auto old = SelectObject(mem, bmp);
    PrintWindow(h, mem, PW_RENDERFULLCONTENT); GdiFlush();
    BITMAPFILEHEADER fh{}; fh.bfType = 0x4D42; fh.bfOffBits = sizeof(fh) + sizeof(bi.bmiHeader); fh.bfSize = fh.bfOffBits + DWORD(w) * hh * 4;
    std::ofstream out(std::filesystem::path(data + L"\\" + name + L".bmp"), std::ios::binary);
    out.write(reinterpret_cast<const char*>(&fh), sizeof(fh)); out.write(reinterpret_cast<const char*>(&bi.bmiHeader), sizeof(bi.bmiHeader));
    out.write(static_cast<const char*>(bits), std::streamsize(w) * hh * 4);
    SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem); ReleaseDC(nullptr, screen);
}
static bool WindowAlive() { return window && IsWindow(window); }
static WR::Phase Phase() { return engineStart ? engineStart->phase() : WR::Phase::Closed; }

int wmain(int argc, wchar_t** argv) {
    if (wcsstr(GetCommandLineW(), L"--webview2-recovered")) {   // the relaunched instance: just prove it started
        if (argc > 2) std::ofstream(std::filesystem::path(std::wstring(argv[2]) + L"\\relaunched.txt")) << "relaunched";
        return 0;
    }
    if (argc != 4) { std::printf("usage: host_test <base> <data> <scenario>\n"); return 2; }
    const std::wstring scenario = argv[3];
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 3;
    instance = GetModuleHandleW(nullptr); base = argv[1]; data = argv[2];
    dataDir = data; profileDir = data + L"\\profile"; sessionFile = data + L"\\session"; downloadsFile = data + L"\\downloads";
    settingsFile = data + L"\\settings"; logFile = data + L"\\browser.log";
    launchArgs = L"\"" + base + L"\" \"" + data + L"\" child";
    DeleteFileW((base + L"\\fake-loader-calls.txt").c_str());
    // A saved session that must survive every failed start untouched.
    { BrowserSession::State s; s.urls = {"https://example.com/", "https://example.org/a"}; s.active = 1; std::ofstream out(std::filesystem::path(sessionFile), std::ios::binary); BrowserSession::Write(out, s); }
    const std::string sessionBefore = SessionBytes();
    engineInstallEffects = FakeEffects;

    INITCOMMONCONTROLSEX cc{sizeof(cc), ICC_STANDARD_CLASSES | ICC_TAB_CLASSES}; InitCommonControlsEx(&cc);
    for (auto name : NativeChrome::FontFiles) AddFontResourceExW((base + L"\\assets\\fonts\\" + name).c_str(), FR_PRIVATE, nullptr);
    paper = CreateSolidBrush(BG);
    WNDCLASSW wc{}; wc.hInstance = instance; wc.lpfnWndProc = WndProc; wc.lpszClassName = L"BrowserWebView2RecoveryHost"; wc.hbrBackground = paper; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    window = CreateWindowW(wc.lpszClassName, L"WebView2 recovery host test - FAKE LOADER", WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU | WS_CLIPCHILDREN,
                           80, 80, 1180, 760, nullptr, nullptr, instance, nullptr);
    NativeUI::ConfigureFullClientFrame(window);
    if (FAILED(chrome.Init(window, base))) return 4;
    contextMenu.Init(instance, window, base);
    CreateElementSurfaces();
    frameClockReady = frameClock.Start(window, FrameMessage);
    chrome.SetMotion(&motion); motion.SetReducedMotion(true); UpdateFont(); Layout();
    ShowWindow(window, SW_SHOWNOACTIVATE); UpdateWindow(window);

    if (scenario == L"recover" || scenario == L"close-during-install" || scenario == L"relaunch" || scenario == L"not-detected") Mode("missing");
    else if (scenario == L"denied") Mode("denied");
    else if (scenario == L"loader-missing") DeleteFileW((base + L"\\WebView2Loader.dll").c_str());
    else { std::printf("unknown scenario\n"); return 2; }

    StartEngine();
    Pump([] { return Phase() != WR::Phase::Creating; }, 3000);
    const auto& v = engineStart->Current();
    std::printf("[%ls] startup: phase=%d cause=%ls install=%d dialog=%d probes=%d creates=%d\n", scenario.c_str(), int(v.phase),
                WR::CauseName(v.diagnosis.cause), v.canInstall, modal.Visible(), Calls("probe"), Calls("create"));
    CHECK(v.phase == WR::Phase::Problem && engineDialogOpen && modal.Visible() && !halted);
    CHECK(!sessionReady && pages.empty() && SessionBytes() == sessionBefore);

    if (scenario == L"recover") {
        CHECK(v.diagnosis.cause == WR::Cause::RuntimeMissing && v.canInstall && Calls("probe") == 1 && Calls("create") == 0);
        Shot(L"1-runtime-missing");
        fake.onLaunch = [] { Mode("present"); Sleep(600); };
        OnEngineButton(EngineInstallButton);
        OnEngineButton(EngineInstallButton);   // double click
        Pump([] { return Phase() == WR::Phase::Installing; }, 3000);
        Shot(L"2-installing");
        CHECK(Pump([] { return Phase() == WR::Phase::Recovered; }, 8000));
        Pump([] { return false; }, 300);
        std::printf("[recover] after: phase=%d starts=%d creates=%d pages=%zu ready=%d dialog=%d\n", int(Phase()), fake.starts.load(), Calls("create"), pages.size(), engineStart->ReadyFired(), modal.Visible());
        CHECK(fake.starts == 1 && Calls("create") == 1 && engineStart->ReadyFired() && sessionReady);
        CHECK(pages.size() == 2 && environment.p != nullptr && !modal.Visible() && !engineDialogOpen);
        SendMessageW(window, WM_CLOSE, 0, 0);
        Pump([] { return !WindowAlive(); }, 3000);
        std::ifstream in(std::filesystem::path(sessionFile), std::ios::binary);
        auto after = BrowserSession::Read(in);
        CHECK(after.urls.size() == 2 && after.urls[0] == "https://example.com/" && after.urls[1] == "https://example.org/a");
    } else if (scenario == L"not-detected") {
        fake.onLaunch = [] {};   // « exit 0 » but nothing appears
        OnEngineButton(EngineInstallButton);
        CHECK(Pump([] { return Phase() == WR::Phase::Problem && fake.starts == 1 && !engineInstaller.Running(); }, 8000));
        Pump([] { return false; }, 200);
        const auto& w = engineStart->Current();
        CHECK(!w.canInstall && w.note.find(L"introuvable") != std::wstring::npos && modal.Visible() && Calls("create") == 0);
        Shot(L"3-installed-not-detected");
        OnEngineButton(EngineInstallButton);   // refused: no loop
        Pump([] { return false; }, 200);
        CHECK(fake.starts == 1);
        modal.Finish(EngineQuitButton);
        Pump([] { return !WindowAlive(); }, 3000);
        CHECK(!WindowAlive() && SessionBytes() == sessionBefore);
    } else if (scenario == L"loader-missing") {
        CHECK(v.diagnosis.cause == WR::Cause::LoaderMissing && !v.canInstall && v.canRetry);
        Shot(L"4-loader-missing");
        OnEngineButton(EngineInstallButton);
        Pump([] { return false; }, 200);
        CHECK(fake.starts == 0);
        modal.Finish(EngineQuitButton);
        Pump([] { return !WindowAlive(); }, 3000);
        CHECK(!WindowAlive() && SessionBytes() == sessionBefore);
    } else if (scenario == L"denied") {
        CHECK(v.diagnosis.cause == WR::Cause::ProfileUnavailable && !v.canInstall && Calls("create") == 1);
        CHECK(v.diagnosis.technical.find(L"0x80070005") != std::wstring::npos);
        OnEngineButton(EngineRetryButton);
        Pump([] { return false; }, 200);
        CHECK(Calls("create") == 2 && Phase() == WR::Phase::Problem && fake.starts == 0);
        modal.Finish(EngineQuitButton);
        Pump([] { return !WindowAlive(); }, 3000);
        CHECK(!WindowAlive() && SessionBytes() == sessionBefore && !Exists(profileDir));
    } else if (scenario == L"close-during-install") {
        fake.waitForCancel = true;
        OnEngineButton(EngineInstallButton);
        CHECK(Pump([] { return Phase() == WR::Phase::Installing; }, 5000));
        const ULONGLONG t0 = GetTickCount64();
        PostMessageW(window, WM_CLOSE, 0, 0);
        Pump([] { return !WindowAlive(); }, 5000);
        std::printf("[close-during-install] closed in %llu ms phase=%d\n", GetTickCount64() - t0, int(Phase()));
        CHECK(!WindowAlive() && GetTickCount64() - t0 < 3000 && Phase() == WR::Phase::Closed && !engineInstaller.Running());
        CHECK(SessionBytes() == sessionBefore && !sessionReady);
    } else if (scenario == L"relaunch") {
        fake.onLaunch = [] { Mode("uninstalled"); };   // found by the probe, but creation says uninstalled
        DeleteFileW((data + L"\\relaunched.txt").c_str());
        OnEngineButton(EngineInstallButton);
        CHECK(Pump([] { return Phase() == WR::Phase::Problem && fake.starts == 1 && !engineInstaller.Running(); }, 8000));
        Pump([] { return false; }, 200);
        const auto& w = engineStart->Current();
        CHECK(w.canRelaunch && !w.canInstall && w.diagnosis.cause == WR::Cause::RuntimeInconsistent && !engineStart->ReadyFired());
        Shot(L"5-relaunch");
        OnEngineButton(EngineRelaunchButton);
        Pump([] { return !WindowAlive(); }, 3000);
        Pump([] { return false; }, 1500);
        CHECK(!WindowAlive() && Exists(data + L"\\relaunched.txt") && SessionBytes() == sessionBefore);
    }
    std::ifstream log{std::filesystem::path(logFile)}; std::string line; int lines = 0;
    while (std::getline(log, line)) { if (line.find("WEBVIEW2") != std::string::npos) { std::printf("  log: %s\n", line.c_str()); ++lines; } CHECK(line.find("example.") == std::string::npos); }
    CHECK(lines > 0);
    if (WindowAlive()) DestroyWindow(window);
    std::printf("host_test %ls: %d checks, %d failures\n", scenario.c_str(), checks, failures);
    std::fflush(stdout);
    ExitProcess(failures ? 1 : 0);   // skip global teardown of the fixture's half-initialised surfaces
}
