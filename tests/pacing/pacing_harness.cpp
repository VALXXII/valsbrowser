// DISPLAY PACING HARNESS — native-only, purpose-built. Never loads WebView2 or the
// browser; opens one visible window titled "Display pacing harness (native only, no
// WebView2)", renders the production NativeChrome::Chrome header with a synthetic
// Loading::State cycle, measures, and destroys only its own window.
//
// Usage: pacing-harness.exe <repo-root> <out-dir> <paced|paced-dwm|timer|legacy> [monitor-index] [probe|active-seconds]
//   paced  : production path — FrameClock + loading-only invalidation + pixel-identical
//            frame gating + text-layout cache + RETAIN_CONTENTS|IMMEDIATELY present.
//   paced-dwm : paced, forcing the DWM-timing fallback clock source.
//   timer  : SetTimer(16) with the same render optimisations (attribution only).
//   legacy : previous behaviour — SetTimer(16), full-header invalidation, text layouts
//            rebuilt every paint, D2D1_PRESENT_OPTIONS_NONE.
//   probe  : synchronous Chrome::Paint cost per dirty-rect size, no pacing.
// Measured: notification arrivals (QPC at frame/timer message), UI-thread paint
// completion (QPC right after Chrome::Paint's EndDraw returns in WM_PAINT), paint
// duration, worker counters, process CPU time from GetProcessTimes (user+kernel, the
// OS accounting; its granularity makes short phases imprecise, so use long phases),
// raw QueryProcessCycleTime cycles (NOT converted to time: the docs say cycle counts
// must not be converted to elapsed time), and dwm.exe CPU when readable. DWM
// scan-out itself is NOT observed.
#ifndef UNICODE
#define UNICODE
#endif
#include "../../src/chrome.h"
#include "../../src/frame-clock.h"
#include <tlhelp32.h>
#include <intrin.h>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>
using namespace NativeChrome;
enum class Mode { Paced, Timer, Legacy } mode = Mode::Paced;
constexpr UINT FrameMsg = WM_APP + 8; constexpr UINT_PTR MotionTimer = 72, EventTimer = 5, PhaseTimer = 6;
Chrome chrome; FrameClock::Clock clockSource; Loading::State loading; HWND window; HANDLE dwm = nullptr;
bool minimized = false, clockReady = false; uint64_t navigationId = 0; int cycleStep = 0;
enum class Phase { Warmup, Active, Idle, Minimized, Restore, Done } phase = Phase::Warmup;
struct Sample { double t, paintMs; bool moving; };
std::vector<Sample> samples; std::vector<double> arrivals;
unsigned long long frameMessages = 0, timerMessages = 0, paints = 0, skipped = 0; long long painted = -2;
struct Counts { double wall = 0, cpu = 0, user = 0, kernel = 0, dwmCpu = 0; bool cpuOk = true, cyclesOk = true; unsigned long long cycles = 0, paints = 0, frames = 0, timers = 0, skipped = 0, wakes = 0, ticks = 0, coalesced = 0, layouts = 0; };
Counts active, idle, hidden, mark;
const std::vector<std::wstring> titles = {L"Example Domain", L"Documentation — Microsoft Learn", L"Recherche DuckDuckGo", L"Nouvel onglet", L"Issue #4821 · GitHub", L"Gelasio & Inter Display specimen"};
const std::wstring uri = L"https://learn.microsoft.com/en-us/windows/win32/directcomp/compositor-clock/compositor-clock";
double Now() { LARGE_INTEGER f, c; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c); return c.QuadPart * 1000.0 / f.QuadPart; }
double Ms(FILETIME t) { return ((unsigned long long)t.dwHighDateTime << 32 | t.dwLowDateTime) / 10000.0; }
double CpuOf(HANDLE p) { FILETIME a, b, k, u; return p && GetProcessTimes(p, &a, &b, &k, &u) ? Ms(k) + Ms(u) : -1; }
unsigned activeMs = 6000;
Counts Snapshot() {
    Counts c; c.wall = Now(); FILETIME a, b, k, u;
    if (GetProcessTimes(GetCurrentProcess(), &a, &b, &k, &u)) { c.kernel = Ms(k); c.user = Ms(u); c.cpu = c.kernel + c.user; } else { c.cpuOk = false; printf("GetProcessTimes failed err=%lu\n", GetLastError()); }
    c.dwmCpu = CpuOf(dwm); ULONG64 cy = 0; if (QueryProcessCycleTime(GetCurrentProcess(), &cy)) c.cycles = cy; else { c.cyclesOk = false; printf("QueryProcessCycleTime failed err=%lu\n", GetLastError()); }
    c.paints = paints; c.frames = frameMessages; c.timers = timerMessages; c.skipped = skipped; c.layouts = chrome.textLayoutsCreated;
    if (auto s = clockSource.Stats()) { c.wakes = s->wakes; c.ticks = s->ticks; c.coalesced = s->coalesced; } return c;
}
Counts Delta(const Counts& a, const Counts& b) {
    Counts d; d.wall = b.wall - a.wall; d.cpu = b.cpu - a.cpu; d.user = b.user - a.user; d.kernel = b.kernel - a.kernel; d.cpuOk = a.cpuOk && b.cpuOk; d.cyclesOk = a.cyclesOk && b.cyclesOk; d.dwmCpu = a.dwmCpu < 0 || b.dwmCpu < 0 ? -1 : b.dwmCpu - a.dwmCpu; d.cycles = b.cycles - a.cycles;
    d.paints = b.paints - a.paints; d.frames = b.frames - a.frames; d.timers = b.timers - a.timers; d.skipped = b.skipped - a.skipped; d.layouts = b.layouts - a.layouts;
    d.wakes = b.wakes - a.wakes; d.ticks = b.ticks - a.ticks; d.coalesced = b.coalesced - a.coalesced; return d;
}
int S(float v) { return (int)std::lround(v * GetDpiForWindow(window) / 96.f); }
void InvalidateHeader() { RECT r; GetClientRect(window, &r); r.bottom = std::min(r.bottom, (LONG)S(84)); InvalidateRect(window, &r, FALSE); }
void InvalidateFrame() {
    if (mode == Mode::Legacy) { InvalidateHeader(); return; }
    if (chrome.LoadingSignature(loading.Sample(Motion::Now())) == painted) { ++skipped; return; } // pixel-identical
    auto d = chrome.LoadingDirty(); RECT r{S(d.left), S(d.top), S(d.right), S(d.bottom)}; InvalidateRect(window, &r, FALSE);
}
void Schedule() {
    const auto now = Motion::Now(); auto wake = loading.NextWake(now, false);
    if (minimized || !wake.delay) { KillTimer(window, MotionTimer); clockSource.Cancel(); return; }
    if (mode == Mode::Paced && clockReady) { KillTimer(window, MotionTimer); clockSource.Request(); return; }
    SetTimer(window, MotionTimer, (UINT)wake.delay->count(), nullptr); // legacy: re-armed per frame
}
// Synthetic navigation: start(15%) at 0, commit(65%) at 400, complete at 800, next at 1500 ms.
void NavigationEvent() {
    const auto now = Motion::Now(); KillTimer(window, EventTimer); UINT next = 0;
    switch (cycleStep % 3) { case 0: loading.Start(++navigationId, now); next = 400; break;
                             case 1: loading.Committed(now); next = 400; break;
                             default: loading.Finish(navigationId, now); next = 700; break; }
    ++cycleStep; arrivals.push_back(NAN); samples.push_back({NAN, 0, true}); InvalidateFrame(); Schedule(); // segment separators
    if (phase == Phase::Active || phase == Phase::Warmup) SetTimer(window, EventTimer, next, nullptr);
}
void NextPhase() {
    KillTimer(window, PhaseTimer);
    switch (phase) {
    case Phase::Warmup: phase = Phase::Active; samples.clear(); arrivals.clear(); mark = Snapshot(); SetTimer(window, PhaseTimer, activeMs, nullptr); break;
    case Phase::Active: active = Delta(mark, Snapshot()); KillTimer(window, EventTimer); phase = Phase::Idle; loading.Clear(); InvalidateHeader(); Schedule(); mark = Snapshot(); SetTimer(window, PhaseTimer, 5000, nullptr); break;
    case Phase::Idle: idle = Delta(mark, Snapshot()); phase = Phase::Minimized;
        loading.Start(++navigationId, Motion::Now()); Schedule(); ShowWindow(window, SW_MINIMIZE);
        mark = Snapshot(); SetTimer(window, PhaseTimer, 3000, nullptr); break;
    case Phase::Minimized: hidden = Delta(mark, Snapshot()); phase = Phase::Restore; ShowWindow(window, SW_RESTORE); SetTimer(window, PhaseTimer, 600, nullptr); break;
    case Phase::Restore: phase = Phase::Done; DestroyWindow(window); break;
    default: break;
    }
}
void Arrival() { if (phase == Phase::Active && loading.NextWake(Motion::Now(), false).frames) arrivals.push_back(Now()); }
LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_SIZE: minimized = w == SIZE_MINIMIZED; if (!minimized) { chrome.Resize(); InvalidateRect(h, nullptr, FALSE); } Schedule(); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; BeginPaint(h, &ps); const float k = 96.f / GetDpiForWindow(h);
        D2D1_RECT_F dirty{ps.rcPaint.left * k, ps.rcPaint.top * k, ps.rcPaint.right * k, ps.rcPaint.bottom * k};
        const double t0 = Now(); const auto now = Motion::Now(); chrome.loading = loading.Sample(now);
        const bool moving = loading.NextWake(now, false).frames; painted = chrome.LoadingSignature(chrome.loading);
        chrome.Paint(titles, 0, uri, false, 0, 0, &dirty, now); const double t1 = Now();
        EndPaint(h, &ps); ++paints;
        if (phase == Phase::Active) samples.push_back({t1, t1 - t0, moving});
        return 0; }
    case FrameMsg:
        if (l == FrameClock::ClockLost) { clockSource.Stop(); clockReady = false; Schedule(); return 0; }
        ++frameMessages; Arrival(); clockSource.Acknowledge(); if (!minimized) InvalidateFrame(); Schedule(); return 0;
    case WM_TIMER:
        if (w == MotionTimer) { ++timerMessages; Arrival(); KillTimer(h, MotionTimer); if (!minimized) InvalidateFrame(); Schedule(); }
        else if (w == EventTimer) NavigationEvent();
        else if (w == PhaseTimer) NextPhase();
        return 0;
    case WM_DESTROY: KillTimer(h, MotionTimer); KillTimer(h, EventTimer); KillTimer(h, PhaseTimer); clockSource.Stop(); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
struct MonitorPick { int want, index = 0; HMONITOR found = nullptr; };
BOOL CALLBACK EnumMonitor(HMONITOR m, HDC, LPRECT, LPARAM p) { auto* pick = (MonitorPick*)p; if (pick->index++ == pick->want) { pick->found = m; return FALSE; } return TRUE; }
double Percentile(std::vector<double> v, double p) { if (v.empty()) return 0; std::sort(v.begin(), v.end()); size_t i = (size_t)std::min<double>(v.size() - 1, std::floor(p * (v.size() - 1) + .5)); return v[i]; }
std::vector<double> Gaps(const std::vector<double>& t) { std::vector<double> g; for (size_t i = 1; i < t.size(); ++i) if (!std::isnan(t[i]) && !std::isnan(t[i - 1])) g.push_back(t[i] - t[i - 1]); return g; }
HANDLE OpenDwm() { // read-only CPU accounting for context; failure is reported, not worked around
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0); if (snap == INVALID_HANDLE_VALUE) return nullptr;
    PROCESSENTRY32W e{}; e.dwSize = sizeof(e); HANDLE h = nullptr;
    for (BOOL ok = Process32FirstW(snap, &e); ok; ok = Process32NextW(snap, &e)) if (!_wcsicmp(e.szExeFile, L"dwm.exe")) { h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, e.th32ProcessID); break; }
    CloseHandle(snap); return h;
}
int wmain(int argc, wchar_t** argv) {
    if (argc < 4) { puts("usage: pacing-harness <root> <out> <paced|timer|legacy> [monitor] [probe]"); return 2; }
    std::wstring m = argv[3]; mode = m == L"legacy" ? Mode::Legacy : m == L"timer" ? Mode::Timer : Mode::Paced; const bool forceDwm = m == L"paced-dwm";
    const int monitorIndex = argc > 4 ? _wtoi(argv[4]) : 0; const bool probe = argc > 5 && std::wstring(argv[5]) == L"probe";
    if (argc > 5 && !probe && _wtoi(argv[5]) > 0) activeMs = 1000u * (unsigned)_wtoi(argv[5]);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); dwm = OpenDwm();
    MonitorPick pick{monitorIndex}; EnumDisplayMonitors(nullptr, nullptr, EnumMonitor, (LPARAM)&pick);
    if (!pick.found) { printf("monitor %d not present\n", monitorIndex); return 3; }
    MONITORINFOEXW info{}; info.cbSize = sizeof(info); GetMonitorInfoW(pick.found, &info);
    DEVMODEW dm{}; dm.dmSize = sizeof(dm); EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &dm);
    WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"DisplayPacingHarness"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    UINT dpi = 96; { UINT x, y; using Fn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*); HMODULE shcore = LoadLibraryExW(L"shcore.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32); auto fn = shcore ? (Fn)(void*)GetProcAddress(shcore, "GetDpiForMonitor") : nullptr; if (fn && SUCCEEDED(fn(pick.found, 0, &x, &y))) dpi = x; }
    RECT frame{0, 0, MulDiv(1180, dpi, 96), MulDiv(110, dpi, 96)}; AdjustWindowRectExForDpi(&frame, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi);
    window = CreateWindowW(wc.lpszClassName, L"Display pacing harness (native only, no WebView2)", WS_OVERLAPPEDWINDOW,
                           info.rcWork.left + 40, info.rcWork.top + 40, frame.right - frame.left, frame.bottom - frame.top, nullptr, nullptr, wc.hInstance, nullptr);
    if (mode == Mode::Legacy) { chrome.presentOptions = D2D1_PRESENT_OPTIONS_NONE; chrome.cacheText = false; }
    if (!window || FAILED(chrome.Init(window, argv[1]))) { puts("FAIL harness init"); return 4; }
    if (probe) { // synchronous paint-cost breakdown, no pacing
        ShowWindow(window, SW_SHOWNORMAL); UpdateWindow(window); chrome.loading = {.5f, 1};
        auto bar = chrome.LoadingDirty(); D2D1_RECT_F pixel{bar.left, bar.top, bar.left + 1, bar.top + 1}, header{0, 0, chrome.width, 84};
        struct V { const char* name; D2D1_RECT_F* dirty; } variants[] = {{"1-DIP dirty", &pixel}, {"URL-bar dirty", &bar}, {"full header", &header}};
        for (auto& v : variants) { std::vector<double> t; for (int i = 0; i < 300; ++i) { const double a = Now(); chrome.Paint(titles, 0, uri, false, 0, 0, v.dirty); t.push_back(Now() - a); }
            printf("probe mode=%ls %s: Chrome::Paint median=%.3fms p95=%.3fms\n", argv[3], v.name, Percentile(t, .5), Percentile(t, .95)); }
        DestroyWindow(window); MSG pm; while (PeekMessageW(&pm, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&pm); chrome.Dispose(); return 0;
    }
    if (mode == Mode::Paced) clockReady = clockSource.Start(window, FrameMsg, !forceDwm);
    ShowWindow(window, SW_SHOWNORMAL); UpdateWindow(window); SetForegroundWindow(window);
    SetTimer(window, PhaseTimer, 800, nullptr); NavigationEvent(); // warmup includes a first cycle
    MSG msg; while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    // Whole-process totals (creation to now), for cross-checking phase aggregation.
    FILETIME created, exited, kTotal, uTotal, nowFt; double processWall = -1, processUser = -1, processKernel = -1;
    if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kTotal, &uTotal)) { GetSystemTimePreciseAsFileTime(&nowFt); processWall = Ms(nowFt) - Ms(created); processUser = Ms(uTotal); processKernel = Ms(kTotal); }
    ULONG64 processCycles = 0; const bool processCyclesOk = QueryProcessCycleTime(GetCurrentProcess(), &processCycles) != 0;
    auto Cpu = [](const Counts& w, char* buffer, size_t n) {
        if (!w.cpuOk) snprintf(buffer, n, "GetProcessTimes FAILED");
        else snprintf(buffer, n, "GetProcessTimes user+kernel=%.1fms (user %.1f, kernel %.1f) over wall %.0fms = %.2f%% of one logical core", w.cpu, w.user, w.kernel, w.wall, 100 * w.cpu / w.wall);
        return buffer; };
    char cpuText[256];
    DWM_TIMING_INFO timing{}; timing.cbSize = sizeof(timing); DwmGetCompositionTimingInfo(nullptr, &timing);
    std::vector<double> paintTimes, paintMs; for (auto& s : samples) if (s.moving) { paintTimes.push_back(s.t); if (!std::isnan(s.t)) paintMs.push_back(s.paintMs); }
    const auto wakeGaps = Gaps(arrivals), paintGaps = Gaps(paintTimes);
    double moving = 0; for (double g : wakeGaps) moving += g;
    const auto* st = clockSource.Stats();
    wchar_t path[1024]; swprintf(path, 1024, L"%ls\\pacing-%ls-monitor%d-%us.txt", argv[2], argv[3], monitorIndex, activeMs / 1000);
    FILE* out = _wfopen(path, L"w");
    for (FILE* f : {stdout, out}) {
        if (!f) continue;
        fprintf(f, "mode=%ls monitor=%d device=%ls EnumDisplaySettingsHz=%lu dpi=%u DwmGetCompositionTimingInfo.rateRefresh=%u/%u source=%ls\n", argv[3], monitorIndex, info.szDevice, dm.dmDisplayFrequency, dpi,
                timing.rateRefresh.uiNumerator, timing.rateRefresh.uiDenominator, mode == Mode::Paced && st ? FrameClock::SourceName(clockSource.CurrentSource()) : L"settimer");
        fprintf(f, "active %.0fms: notification intervals while moving n=%zu median=%.2fms p95=%.2fms p99=%.2fms max=%.2fms (%.1f/s)\n",
                active.wall, wakeGaps.size(), Percentile(wakeGaps, .5), Percentile(wakeGaps, .95), Percentile(wakeGaps, .99), Percentile(wakeGaps, 1), moving > 0 ? wakeGaps.size() * 1000 / moving : 0.0);
        fprintf(f, "active: painted intervals while moving n=%zu median=%.2fms p95=%.2fms max=%.2fms; skipped pixel-identical frames=%llu\n",
                paintGaps.size(), Percentile(paintGaps, .5), Percentile(paintGaps, .95), Percentile(paintGaps, 1), active.skipped);
        fprintf(f, "active: Chrome::Paint incl. EndDraw median=%.3fms p95=%.3fms max=%.3fms paints=%llu frameMsgs=%llu timerMsgs=%llu workerWakes=%llu ticks=%llu coalesced=%llu textLayoutsCreated=%llu\n",
                Percentile(paintMs, .5), Percentile(paintMs, .95), Percentile(paintMs, 1), active.paints, active.frames, active.timers, active.wakes, active.ticks, active.coalesced, active.layouts);
        fprintf(f, "active CPU: %s; per paint %.3fms\n", Cpu(active, cpuText, sizeof cpuText), active.cpuOk && active.paints ? active.cpu / active.paints : 0.0);
        fprintf(f, "active raw cycles: %s %llu process cycles total, %.0f cycles/paint (raw hardware cycle counts; not converted to time)\n", active.cyclesOk ? "QueryProcessCycleTime" : "QueryProcessCycleTime FAILED", active.cycles, active.paints ? (double)active.cycles / active.paints : 0.0);
        fprintf(f, "idle CPU: %s; raw cycles=%llu; paints=%llu frameMsgs=%llu timerMsgs=%llu workerWakes=%llu ticks=%llu\n", Cpu(idle, cpuText, sizeof cpuText), idle.cycles, idle.paints, idle.frames, idle.timers, idle.wakes, idle.ticks);
        fprintf(f, "minimized (loading started then minimized) CPU: %s; raw cycles=%llu; paints=%llu frameMsgs=%llu timerMsgs=%llu workerWakes=%llu ticks=%llu\n", Cpu(hidden, cpuText, sizeof cpuText), hidden.cycles, hidden.paints, hidden.frames, hidden.timers, hidden.wakes, hidden.ticks);
        fprintf(f, "whole process (creation..end, includes startup/D2D/font init/warmup/restore): wall=%.0fms user=%.1fms kernel=%.1fms cycles=%s%llu; dwm.exe=%s\n", processWall, processUser, processKernel, processCyclesOk ? "" : "FAILED ", (unsigned long long)processCycles, dwm ? "readable" : "not readable (access denied), not measured");
        if (st) fprintf(f, "worker totals: requests=%llu wakes=%llu ticks=%llu posted=%llu coalesced=%llu cancelled=%llu timeouts=%llu failures=%llu fallbackFrames=%llu\n",
                        st->requests.load(), st->wakes.load(), st->ticks.load(), st->posted.load(), st->coalesced.load(), st->cancelled.load(), st->timeouts.load(), st->failures.load(), st->fallbackFrames.load());
        fprintf(f, "webview2Loaded=%d dwmReadable=%d\n", GetModuleHandleW(L"WebView2Loader.dll") ? 1 : 0, dwm ? 1 : 0);
    }
    if (out) fclose(out);
    if (dwm) CloseHandle(dwm);
    chrome.Dispose(); CoUninitialize();
    return GetModuleHandleW(L"WebView2Loader.dll") ? 5 : 0;
}
