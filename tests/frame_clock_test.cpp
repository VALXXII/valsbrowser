// Native-only FrameClock lifecycle fixture: message-only window, never loads WebView2.
#ifndef UNICODE
#define UNICODE
#endif
#include "../src/chrome.h"
#include "../src/frame-clock.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace NativeChrome;
constexpr UINT Msg = WM_APP + 8;
static int frames = 0, lost = 0;
static std::vector<double> arrivals;
static double Ms() { LARGE_INTEGER f, c; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c); return c.QuadPart * 1000.0 / f.QuadPart; }
static void Check(bool ok, const char* what) { if (!ok) { printf("FAIL frame clock: %s\n", what); exit(1); } }
static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == Msg) { if (l == FrameClock::ClockLost) ++lost; else { ++frames; arrivals.push_back(Ms()); } return 0; }
    return DefWindowProcW(h, m, w, l);
}
static void Pump(double ms) {
    const double end = Ms() + ms; MSG msg;
    while (Ms() < end) {
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 2, QS_ALLINPUT);
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
    }
}
static double Median(std::vector<double> v) { std::sort(v.begin(), v.end()); return v.empty() ? 0 : v[v.size() / 2]; }
static void Exercise(HWND hwnd, bool compositor) {
    FrameClock::Clock clock; frames = lost = 0; arrivals.clear();
    // Windows Server 2022 (the CI host) predates the compositor-clock API.
    // The production clock intentionally uses DWM timing on those systems.
    HMODULE dcomp = compositor ? LoadLibraryExW(L"dcomp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32) : nullptr;
    const bool compositorAvailable = dcomp && GetProcAddress(dcomp, "DCompositionWaitForCompositorClock");
    if (dcomp) FreeLibrary(dcomp);
    const auto expectedSource = compositorAvailable ? FrameClock::Source::CompositorClock : FrameClock::Source::DwmTiming;
    Check(clock.Start(hwnd, Msg, compositor), "start");
    Check(clock.CurrentSource() == expectedSource, "expected source for available Windows APIs");
    Pump(250);
    Check(frames == 0 && clock.Stats()->wakes == 0 && clock.Stats()->ticks == 0, "idle: no wakes, ticks or messages without a request");
    for (int i = 0; i < 5; ++i) clock.Request();
    Pump(120);
    Check(frames == 1, "five requests coalesce into exactly one frame message");
    // rAF-style loop: acknowledge, request again from the frame handler.
    frames = 0; arrivals.clear();
    for (int n = 0; n < 60; ++n) { clock.Acknowledge(); clock.Request(); const int before = frames; const double end = Ms() + 200; while (frames == before && Ms() < end) Pump(1); }
    Check(frames == 60, "paced loop delivers one message per request");
    std::vector<double> gaps; for (size_t i = 1; i < arrivals.size(); ++i) gaps.push_back(arrivals[i] - arrivals[i - 1]);
    const double median = Median(gaps);
    Check(median > 2.0 && median < 34.0, "frame interval within display cadence range (2..34 ms)");
    // Backlog bound: never acknowledge, keep requesting; at most one message arrives.
    clock.Acknowledge(); frames = 0; clock.Request(); Pump(60); const int first = frames;
    const auto coalescedBefore = clock.Stats()->coalesced.load();
    for (int i = 0; i < 40; ++i) { clock.Request(); Pump(5); }
    Check(first == 1 && frames == 1, "unacknowledged frame never queues another notification");
    Check(clock.Stats()->coalesced.load() > coalescedBefore, "ticks during a pending frame are coalesced (counted, not queued)");
    // Cancel before the tick: nothing is delivered and the worker returns dormant.
    clock.Acknowledge(); frames = 0; clock.Request(); clock.Cancel(); Pump(100);
    Check(frames == 0, "cancelled request delivers no frame");
    const auto wakes = clock.Stats()->wakes.load(); Pump(200);
    Check(clock.Stats()->wakes.load() == wakes, "dormant after cancel: no further worker wakes");
    // Shutdown while waiting for a tick is prompt and final.
    clock.Request(); const double t0 = Ms(); clock.Stop(); const double stopMs = Ms() - t0;
    Check(stopMs < 100, "stop joins the worker promptly");
    frames = 0; clock.Request(); Pump(80);
    Check(frames == 0 && lost == 0 && !clock.Running(), "no frames or lost-clock notices after stop");
    printf("  source=%ls median=%.2fms frames=60 stop=%.2fms ticks=%llu coalesced=%llu timeouts=%llu failures=%llu\n",
           FrameClock::SourceName(expectedSource),
           median, stopMs, clock.Stats() ? clock.Stats()->ticks.load() : 0ULL, clock.Stats() ? clock.Stats()->coalesced.load() : 0ULL,
           clock.Stats() ? clock.Stats()->timeouts.load() : 0ULL, clock.Stats() ? clock.Stats()->failures.load() : 0ULL);
}
// Gating proof: equal LoadingSignature => identical rendered pixels around the fill
// edge and inside it, across a dense sweep of width and fade values, at two DPIs.
static void SignatureIsPixelExact(const wchar_t* root, UINT dpi) {
    Chrome chrome; Check(SUCCEEDED(chrome.InitOffscreen(root, dpi, 1180, 84)), "offscreen chrome");
    chrome.Paint({L"Loading"}, 0, L"https://example.test/", false, 0, 0);
    const float step = 96.f / dpi; long long previousSig = -3; Loading::Visual previousVisual; int equalPairs = 0, distinct = 0;
    // Sample fixed absolute device pixels (around the current edge, plus the interior).
    auto render = [&](Loading::Visual v, float edge) {
        chrome.loading = v; chrome.Paint({L"Loading"}, 0, L"https://example.test/", false, 0, 0);
        std::vector<UINT32> px; const float base = std::floor(edge / step) * step;
        for (int i = -4; i <= 4; ++i) px.push_back(chrome.SurfacePixel(base + i * step + step / 2, 60));
        px.push_back(chrome.SurfacePixel(chrome.bar.left + 40, 60)); return px; };
    auto visit = [&](Loading::Visual v) {
        const long long sig = chrome.LoadingSignature(v);
        if (sig == previousSig) {
            ++equalPairs; const float edge = chrome.bar.left + (chrome.bar.right - chrome.bar.left) * v.fraction;
            Check(render(previousVisual, edge) == render(v, edge), "equal signature must mean identical pixels (edge window + interior)");
        } else ++distinct;
        previousSig = sig; previousVisual = v; };    // Width sweep (0.00005 steps ~ 0.05-0.1 device px) — equal signatures must match exactly.
    for (int i = 0; i <= 3000; ++i) visit({.15f + i * .00005f, 1});
    // Fade sweep at full width — interior alpha.
    previousSig = -3; for (int i = 0; i <= 400; ++i) visit({1, 1 - i / 400.f});
    Check(equalPairs > 100 && distinct > 50, "sweep exercised both skipped and painted frames");
    printf("  signature pixel-exact at %u DPI: %d skippable pairs verified identical, %d distinct\n", dpi, equalPairs, distinct);
}
int wmain(int argc, wchar_t** argv) {
    Check(argc >= 2, "usage: frame-clock-test <repo-root> [evidence]");
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    SignatureIsPixelExact(argv[1], 96); SignatureIsPixelExact(argv[1], 144);
    WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"FrameClockFixture";
    Check(RegisterClassW(&wc) != 0, "register");
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    Check(hwnd != nullptr, "message window");
    Exercise(hwnd, true);
    Exercise(hwnd, false);
    // A clock whose window disappears exits and cannot post into a dead HWND forever.
    { FrameClock::Clock clock; HWND temp = CreateWindowW(wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
      Check(clock.Start(temp, Msg), "start temp"); DestroyWindow(temp); clock.Request(); Pump(100);
      Check(!clock.Running(), "worker stops when its window is gone"); clock.Stop(); }
    // Loading semantics unchanged: frames only while width/fade interpolate; dwells idle.
    Loading::State l; const auto at = [](int ms) { return Motion::Time{} + Motion::Milliseconds(ms); };
    l.Start(1, at(0)); Check(l.NextWake(at(10), false).frames && !l.NextWake(at(370), false).delay, "loading frames only while moving");
    l.Finish(1, at(400)); Check(l.NextWake(at(900), false).frames && !l.NextWake(at(961), false).delay, "fill+fade ends at 560ms");
    DestroyWindow(hwnd);
    Check(!GetModuleHandleW(L"WebView2Loader.dll"), "no WebView2");
    puts("PASS frame clock: pixel-exact loading-frame gating (96/144 DPI), idle dormant, request coalescing, paced loop, no backlog, cancel, prompt stop, dead-window exit, available sources");
    return 0;
}
