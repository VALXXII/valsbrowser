#pragma once
// Display-paced, on-demand frame notifications for the native chrome (no WebView2).
//
// One small worker thread blocks until the UI thread requests a frame, then waits
// for the next compositor tick and posts ONE message to the UI window. It never
// spins, never changes the global timer resolution and is fully dormant (blocked on
// an event, INFINITE) whenever no animation requests frames.
//
// Clock sources, best first (selected at runtime, with per-frame safety fallback):
//  1. DCompositionWaitForCompositorClock (dcomp.dll, Windows build 22000+): the
//     system compositor heartbeat. Follows the active display / dynamic refresh rate.
//  2. DwmGetCompositionTimingInfo + high-resolution waitable timer: sleeps until
//     the next estimated DWM vblank (re-read every frame, so rate changes apply).
//  3. If the worker cannot start, the caller keeps the legacy SetTimer path, which
//     is NOT display synchronised (USER timers are rounded to the system tick).
//
// Bounded notifications: at most one frame message is ever outstanding. Ticks that
// arrive while one is pending are coalesced (counted, never queued).
#include <windows.h>
#include <dwmapi.h>
#include <atomic>
#include <memory>

namespace NativeChrome { namespace FrameClock {
enum class Source : int { None = 0, CompositorClock = 1, DwmTiming = 2 };
inline const wchar_t* SourceName(Source s) {
    switch (s) { case Source::CompositorClock: return L"compositor-clock";
                 case Source::DwmTiming: return L"dwm-vblank-timer"; default: return L"none"; }
}
// lParam of the frame message: 0 = frame, 1 = clock stopped permanently (use fallback).
constexpr LPARAM FrameTick = 0, ClockLost = 1;

struct Counters {
    std::atomic<unsigned long long> requests{0}, wakes{0}, ticks{0}, posted{0}, coalesced{0},
        cancelled{0}, timeouts{0}, failures{0}, fallbackFrames{0};
};

class Clock {
    using WaitFn = DWORD(WINAPI*)(UINT, const HANDLE*, DWORD);
    struct Shared {
        HWND hwnd = nullptr; UINT message = 0;
        HANDLE wake = nullptr, stop = nullptr, timer = nullptr; HMODULE dcomp = nullptr;
        WaitFn wait = nullptr;
        std::atomic<bool> requested{false}, pending{false};
        std::atomic<int> source{0};
        int consecutiveFailures = 0; // worker thread only
        Counters counters;
        ~Shared() {
            if (timer) CloseHandle(timer);
            if (wake) CloseHandle(wake);
            if (stop) CloseHandle(stop);
            if (dcomp) FreeLibrary(dcomp);
        }
    };
    std::shared_ptr<Shared> shared_;
    HANDLE thread_ = nullptr;

    // Sleep until the next estimated DWM vblank; interruptible by stop. Never returns
    // in less than ~1ms after the previous call, so failures cannot spin.
    static bool WaitDwm(Shared& s) {
        LARGE_INTEGER frequency{}, now{}; QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&now);
        long long period = frequency.QuadPart / 60, target = now.QuadPart + period;
        DWM_TIMING_INFO info{}; info.cbSize = sizeof(info);
        if (SUCCEEDED(DwmGetCompositionTimingInfo(nullptr, &info)) && info.qpcRefreshPeriod > 0 &&
            (long long)info.qpcRefreshPeriod >= frequency.QuadPart / 1000 &&
            (long long)info.qpcRefreshPeriod <= frequency.QuadPart / 20) {
            period = (long long)info.qpcRefreshPeriod;
            long long vblank = (long long)info.qpcVBlank;
            if (vblank <= 0) vblank = now.QuadPart;
            // Next vblank at least half a period ahead: no double frame per vblank.
            const long long minimum = now.QuadPart + period / 2;
            if (vblank < minimum) vblank += ((minimum - vblank + period - 1) / period) * period;
            target = vblank;
        } else ++s.counters.failures;
        long long ticks100ns = (target - now.QuadPart) * 10000000 / frequency.QuadPart;
        if (ticks100ns < 10000) ticks100ns = 10000; // >=1ms: hard floor against spinning
        LARGE_INTEGER due{}; due.QuadPart = -ticks100ns;
        if (!s.timer || !SetWaitableTimer(s.timer, &due, 0, nullptr, nullptr, FALSE)) {
            ++s.counters.failures;
            return WaitForSingleObject(s.stop, 16) == WAIT_TIMEOUT;
        }
        HANDLE handles[2] = {s.stop, s.timer};
        return WaitForMultipleObjects(2, handles, FALSE, INFINITE) == WAIT_OBJECT_0 + 1;
    }
    // Returns false when stopping.
    static bool WaitFrame(Shared& s) {
        if (s.source.load() == (int)Source::CompositorClock && s.wait) {
            const DWORD r = s.wait(1, &s.stop, 250);
            // The API sample documents WAIT_OBJECT_0+count for a tick; the reference page
            // documents STATUS_SUCCESS. Disambiguate 0 by checking the stop event itself.
            const bool stopping = WaitForSingleObject(s.stop, 0) == WAIT_OBJECT_0;
            if (stopping) return false;
            if (r == WAIT_OBJECT_0 + 1 || r == WAIT_OBJECT_0) { s.consecutiveFailures = 0; ++s.counters.ticks; return true; }
            if (r == WAIT_TIMEOUT) { ++s.counters.timeouts; ++s.counters.ticks; return true; }
            // Display off / occluded (returns immediately) or other failure: pace this
            // frame from DWM timing so the loop can never spin; demote after repeats.
            ++s.counters.failures; ++s.counters.fallbackFrames;
            if (++s.consecutiveFailures >= 8) s.source = (int)Source::DwmTiming;
            const bool ok = WaitDwm(s); if (ok) ++s.counters.ticks; return ok;
        }
        const bool ok = WaitDwm(s); if (ok) ++s.counters.ticks; return ok;
    }
    static DWORD WINAPI Run(LPVOID parameter) {
        std::shared_ptr<Shared> owner(*static_cast<std::shared_ptr<Shared>*>(parameter));
        delete static_cast<std::shared_ptr<Shared>*>(parameter);
        Shared& s = *owner;
        for (;;) {
            HANDLE handles[2] = {s.stop, s.wake};
            const DWORD r = WaitForMultipleObjects(2, handles, FALSE, INFINITE); // dormant
            if (r != WAIT_OBJECT_0 + 1) break;
            ++s.counters.wakes;
            if (!s.requested.load()) { ++s.counters.cancelled; continue; }
            if (!WaitFrame(s)) break;
            if (!s.requested.exchange(false)) { ++s.counters.cancelled; continue; }
            if (s.pending.exchange(true)) { ++s.counters.coalesced; continue; } // no backlog
            if (PostMessageW(s.hwnd, s.message, 0, FrameTick)) ++s.counters.posted;
            else { s.pending = false; ++s.counters.failures; break; } // window gone
        }
        if (WaitForSingleObject(s.stop, 0) != WAIT_OBJECT_0) {
            s.source = (int)Source::None;
            PostMessageW(s.hwnd, s.message, 0, ClockLost);
        }
        return 0;
    }
public:
    Clock() = default;
    Clock(const Clock&) = delete; Clock& operator=(const Clock&) = delete;
    ~Clock() { Stop(); }
    // allowCompositorClock=false forces the DWM timing source (fixtures/harness only).
    bool Start(HWND hwnd, UINT message, bool allowCompositorClock = true) {
        if (thread_) return true;
        auto s = std::make_shared<Shared>();
        s->hwnd = hwnd; s->message = message;
        s->wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        s->stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!s->wake || !s->stop) return false;
        // High-resolution waitable timers (Windows 10 1803+) do not alter the global
        // timer resolution; older systems get a normal waitable timer.
        s->timer = CreateWaitableTimerExW(nullptr, nullptr, 0x00000002 /*CREATE_WAITABLE_TIMER_HIGH_RESOLUTION*/, TIMER_ALL_ACCESS);
        if (!s->timer) s->timer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
        s->source = (int)Source::DwmTiming;
        if (allowCompositorClock) {
            s->dcomp = LoadLibraryExW(L"dcomp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (s->dcomp) s->wait = reinterpret_cast<WaitFn>(reinterpret_cast<void*>(GetProcAddress(s->dcomp, "DCompositionWaitForCompositorClock")));
            if (s->wait) s->source = (int)Source::CompositorClock;
        }
        auto* parameter = new std::shared_ptr<Shared>(s);
        thread_ = CreateThread(nullptr, 64 * 1024, Run, parameter, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
        if (!thread_) { delete parameter; return false; }
        shared_ = std::move(s);
        return true;
    }
    bool Running() const { return thread_ && shared_ && shared_->source.load() != (int)Source::None; }
    Source CurrentSource() const { return shared_ ? (Source)shared_->source.load() : Source::None; }
    const Counters* Stats() const { return shared_ ? &shared_->counters : nullptr; }
    // UI thread: ask for one notification at the next compositor tick. Idempotent.
    void Request() {
        if (!Running()) return;
        ++shared_->counters.requests;
        if (!shared_->requested.exchange(true)) SetEvent(shared_->wake);
    }
    // UI thread: no frame wanted (idle, deadline-only dwell, minimized, halted).
    void Cancel() { if (shared_) shared_->requested = false; }
    // UI thread, on the frame message: re-allows the next notification.
    void Acknowledge() { if (shared_) shared_->pending = false; }
    // Safe shutdown: wakes the worker from any wait and joins it (bounded).
    void Stop() {
        if (!thread_) return;
        shared_->requested = false;
        SetEvent(shared_->stop);
        // The worker holds its own reference to the shared state, so a (theoretical)
        // join timeout can never free handles it still uses.
        WaitForSingleObject(thread_, 2000);
        CloseHandle(thread_); thread_ = nullptr;
    }
    bool Stopped() const { return !thread_; }
};
}} // namespace NativeChrome::FrameClock
