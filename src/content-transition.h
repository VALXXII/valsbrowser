#pragma once
// Tab / profile switch transition of the web viewport (user request 2026-11-14: « petit blur out / blur in »).
//
// The tabs are WINDOWED WebView2 controllers (child HWNDs): a live page cannot carry a composition effect (that
// needs CompositionController visual hosting, which this build does not use; see viewport-composition.h). The
// transition therefore works on at most TWO transient snapshots of our OWN webview windows (PrintWindow of the
// visible WebView child windows only: never the desktop, never another application), composed by DirectComposition
// on a target of the browser window that sits above its child windows (CreateTargetForHwnd topmost = above THIS
// window's children only: the overlay is part of our window, stays behind other applications and our own popups,
// menus and modals, and takes no input).
//
//   Outgoing  Begin(), before the outgoing page is hidden: snapshot 1 (outgoing page) is shown; its GPU Gaussian
//             blur grows at once while the real switch runs underneath (no input lock, no artificial wait).
//   Waiting   Placed(), when the switch returned: the caller polls the incoming page's readiness asynchronously,
//             bounded by WaitCapMs (<= 150 ms, cold profiles included): the outgoing snapshot is never held
//             opaque longer while the live incoming page already receives input.
//   Incoming  Incoming(): snapshot 2 (the incoming page, as placed) appears UNDER the blurred outgoing one at the
//             blur on screen; the outgoing one fades quickly, the incoming one sharpens to 0, then the overlay
//             dissolves into the live page (identical pixels by then).
//   FadingOut FadeOut(): timeout, nothing to capture (native home canvas), reduced motion, capture budget exceeded
//             or a failed capture: the outgoing snapshot fades over whatever is live (the loading canvas); an old
//             profile's capture is never held over the new profile beyond the timeout.
//
//  * Every animation is an IDCompositionAnimation run by DWM: no frame timer, no per-frame CPU work.
//  * Generation: every Begin (new or retarget) bumps it; the caller's async steps (readiness poll, end timer) carry
//    the generation they were started for and are ignored when it changed (an old capture never resurfaces).
//  * Retarget (a switch during a transition): what is on screen becomes the outgoing layer as is (no recapture);
//    the newest target's incoming phase follows. At most two surfaces alive at any time.
//  * Capture budget: one capture blocks the UI thread (measured). Past captures adapt the next transitions: a
//    capture slower than SlowCaptureMs skips the incoming capture (fade only); slower than SkipCaptureMs, or a
//    viewport above MaxPixels, makes the next switches instant. This cannot undo a capture already taken.
//  * Input: the caller cancels the overlay on the first input to a page (WM_PARENTNOTIFY button-down, sent
//    synchronously before the page handles the click; a page key-down). Cancel bumps the generation: a late
//    readiness / blur-in never brings a capture back after an interaction. Nothing is consumed or delayed.
//  * Capture allowlist: the caller passes the host windows of the CURRENTLY shown pages (one per tab, split
//    panes included); any other window (the previous tab's, overlapping or not) is never captured.
//  * Reduced motion: no blur effect, no incoming capture: the outgoing snapshot only fades.
//  * Cancel (resize, move, minimise, DPI, lost foreground, close): removed at once; a viewport shift inside the same
//    window (bookmarks bar) re-anchors the overlay to the new viewport instead (offset + clip, never stretched).
//  * Both surfaces are released immediately at the end or on cancel. Nothing persisted.
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dcomp.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cwchar>
#include <vector>

namespace ContentTransition {

enum class Kind { Tab, Profile };
enum class Phase { Idle, Outgoing, Waiting, Incoming, FadingOut };
inline constexpr float SlowCaptureMs = 35, SkipCaptureMs = 70;
inline constexpr unsigned WaitCapMs = 120;   // longest opaque hold after the switch returned (then a fade)
inline constexpr long long MaxPixels = 3840LL * 2160;
// Durations (each fading phase) and blur peak (DIP), Motion tokens: tabs <= 150 ms, profiles ~210 ms; blur 3 / 4 DIP.
struct Timing { float durationMs, blurDip; };
inline Timing TimingOf(Kind k, bool reduced) {
    if (reduced) return {k == Kind::Profile ? 140.f : 100.f, 0.f};
    return k == Kind::Profile ? Timing{210.f, 4.f} : Timing{140.f, 3.f};
}
// Pure curves (seconds from the segment start), shared by the DComp animations and the tests.
inline float Ease3(float u) { u = std::clamp(u, 0.f, 1.f); return 1 - (1 - u) * (1 - u) * (1 - u); }
// Outgoing blur: b0 -> peak (ease-out cubic over 60 % of d), then held.
inline float BlurAt(float t, float d, float b0, float peak) { return b0 + (peak - b0) * Ease3(t / (.6f * d)); }
// Fallback fade (FadingOut): o0 -> 0 (ease-in quad over d).
inline float OpacityAt(float t, float d, float o0) { const float u = std::clamp(t / d, 0.f, 1.f); return o0 * (1 - u * u); }
// Incoming phase: the incoming blur b0 -> 0 (ease-out cubic over d); the outgoing layer o0 -> 0 over the first
// 40 % (ease-out quad); the whole overlay holds until 70 %, then dissolves (smoothstep) to 0 at d.
inline float SharpenAt(float t, float d, float b0) { return b0 * (1 - Ease3(t / d)); }
inline float OldFadeAt(float t, float d, float o0) { const float u = std::clamp(t / (.4f * d), 0.f, 1.f); return o0 * (1 - u) * (1 - u); }
inline float DissolveAt(float t, float d, float o0) { if (t <= .7f * d) return o0; const float u = std::clamp((t - .7f * d) / (.3f * d), 0.f, 1.f); return o0 * (1 - u * u * (3 - 2 * u)); }
// The adaptive budget (pure): what the next transition may do after a capture that took `lastMs`.
enum class Budget { Full, OutgoingOnly, None };
inline Budget BudgetFor(float lastMs, long long pixels) { if (pixels > MaxPixels || lastMs > SkipCaptureMs) return Budget::None; return lastMs > SlowCaptureMs ? Budget::OutgoingOnly : Budget::Full; }

class Overlay {
public:
    struct Values { float oldBlur, oldOpacity, outer, newBlur; };
    ~Overlay() { Destroy(); }
    // Starts (or retargets) a transition over `viewport` (client pixels of `window`). False: no overlay (instant).
    bool Begin(HWND window, RECT viewport, Kind kind, bool reduced, float dpiScale, const std::vector<HWND>& pages, COLORREF canvas = RGB(0x17, 0x17, 0x17)) {
        const auto now = Clock::now(); const Timing timing = TimingOf(kind, reduced);
        const int w = viewport.right - viewport.left, h = viewport.bottom - viewport.top;
        if (active_) {   // retarget: what is on screen becomes the outgoing layer (re-anchored if the viewport moved)
            const Values v = Now(now);
            if (phase_ == Phase::Incoming && newS_) {   // the incoming snapshot (sharpening) is the visible one
                oldV_->SetContent(newS_); Release(oldS_); oldS_ = newS_; newS_ = nullptr; newV_->SetContent(nullptr); newV_->SetEffect(nullptr);
                v0_ = {v.newBlur, 1.f, v.outer, 0.f};
            } else v0_ = {v.oldBlur, v.oldOpacity, v.outer, 0.f};
            Release(newS_); newV_->SetContent(nullptr);
            if (!EqualRect(&viewport, &rect_)) Place(viewport);
            ++retargets_; ++generation_; kind_ = kind; reduced_ = reduced; duration_ = timing.durationMs / 1000.f; peak_ = reduced ? 0.f : std::max(v0_.oldBlur, timing.blurDip * dpiScale);
            return AnimateOutgoing(now);
        }
        Cancel();
        if (w <= 0 || h <= 0) return false;
        budget_ = BudgetFor(lastCaptureMs_, static_cast<long long>(w) * h);
        if (budget_ == Budget::None) { lastCaptureMs_ *= .5f; return false; }   // skipped (decays: a later switch retries)
        if (!Ensure(window)) return false;
        canvas_ = canvas;
        const auto c0 = Clock::now();
        if (!Capture(window, viewport, pages, oldS_)) return false;
        captureMs_ = lastCaptureMs_ = Seconds(Clock::now() - c0) * 1000.f; captures_ = 1; incomingCaptureMs_ = 0;
        Place(viewport);
        oldV_->SetContent(oldS_); newV_->SetContent(nullptr);
        active_ = true; ++generation_; kind_ = kind; reduced_ = reduced; duration_ = timing.durationMs / 1000.f; peak_ = reduced ? 0.f : timing.blurDip * dpiScale;
        v0_ = {0.f, 1.f, 1.f, 0.f};
        return AnimateOutgoing(Clock::now());   // the clock starts once the snapshot is up (capture excluded)
    }
    // The viewport moved / resized inside the same window (bookmarks bar, chrome layout): the overlay follows its
    // top-left and clips to the new size (the snapshot keeps its pixels: never stretched).
    void Reanchor(RECT viewport) { if (!active_ || EqualRect(&viewport, &rect_)) return; Place(viewport); device_->Commit(); }
    // The switch returned: the incoming page is being placed. Returns the generation the caller's poll carries.
    unsigned Placed() { if (active_ && phase_ == Phase::Outgoing) phase_ = Phase::Waiting; return generation_; }
    // The incoming page is ready (placed, painted): snapshot 2 and the blur-in / dissolve. False if stale.
    bool Incoming(HWND window, unsigned generation, const std::vector<HWND>& pages) {
        if (!active_ || generation != generation_ || phase_ != Phase::Waiting) return false;
        if (reduced_ || budget_ != Budget::Full) return FadeOut(generation);
        const auto c0 = Clock::now();
        if (!Capture(window, rect_, pages, newS_)) return FadeOut(generation);
        incomingCaptureMs_ = Seconds(Clock::now() - c0) * 1000.f; lastCaptureMs_ = std::max(lastCaptureMs_, incomingCaptureMs_); ++captures_;
        const auto now = Clock::now(); const Values v = Now(now);
        newV_->SetContent(newS_);
        v0_ = {v.oldBlur, v.oldOpacity, v.outer, peak_ > 0 ? std::max(v.oldBlur, .5f * peak_) : 0.f};   // incoming starts at the blur on screen
        phase_ = Phase::Incoming; start_ = now;
        const float d = duration_;
        IDCompositionAnimation *oldFade = nullptr, *dissolve = nullptr, *sharpen = nullptr;
        bool ok = SUCCEEDED(device_->CreateAnimation(&oldFade)) && SUCCEEDED(device_->CreateAnimation(&dissolve));
        if (ok) {
            const float s = .4f * d, o = v0_.oldOpacity;   // o (1 - u)², u = t / s
            oldFade->AddCubic(0.0, o, -2 * o / s, o / (s * s), 0.f); oldFade->End(s, 0.f);
            const float hs = .7f * d, ds = .3f * d, q = v0_.outer;   // hold, then smoothstep down: q (1 - 3u² + 2u³)
            dissolve->AddCubic(0.0, q, 0, 0, 0); dissolve->AddCubic(hs, q, 0, -3 * q / (ds * ds), 2 * q / (ds * ds * ds)); dissolve->End(d, 0.f);
            ok = SUCCEEDED(oldGroup_->SetOpacity(oldFade)) && SUCCEEDED(outerGroup_->SetOpacity(dissolve));
        }
        newBlurOk_ = false;
        if (ok && v0_.newBlur > 0 && SUCCEEDED(device_->CreateAnimation(&sharpen))) {
            const float k = -v0_.newBlur;   // b0 + k (3u - 3u² + u³), u = t / d
            sharpen->AddCubic(0.0, v0_.newBlur, 3 * k / d, -3 * k / (d * d), k / (d * d * d)); sharpen->End(d, 0.f);
            newBlurOk_ = SUCCEEDED(blurNew_->SetStandardDeviation(sharpen)) && SUCCEEDED(newV_->SetEffect(blurNew_));
            if (!newBlurOk_) newV_->SetEffect(nullptr);
        } else newV_->SetEffect(nullptr);
        ok = ok && SUCCEEDED(device_->Commit());
        Release(oldFade); Release(dissolve); Release(sharpen);
        if (!ok) { Cancel(); return false; }
        return true;
    }
    // Timeout, nothing to capture, reduced motion, budget: the outgoing snapshot fades over what is live.
    bool FadeOut(unsigned generation) {
        if (!active_ || generation != generation_ || (phase_ != Phase::Waiting && phase_ != Phase::Outgoing)) return false;
        const auto now = Clock::now(); const Values v = Now(now);
        Release(newS_); newV_->SetContent(nullptr); newV_->SetEffect(nullptr);
        v0_ = {v.oldBlur, v.oldOpacity, v.outer, 0.f}; phase_ = Phase::FadingOut; start_ = now;
        IDCompositionAnimation* fade = nullptr;
        bool ok = SUCCEEDED(device_->CreateAnimation(&fade));
        if (ok) { const float d = duration_, o = v0_.oldOpacity; fade->AddCubic(0.0, o, 0.f, -o / (d * d), 0.f); fade->End(d, 0.f); ok = SUCCEEDED(oldGroup_->SetOpacity(fade)) && SUCCEEDED(outerGroup_->SetOpacity(v0_.outer)); }
        ok = ok && SUCCEEDED(device_->Commit()); Release(fade);
        if (!ok) { Cancel(); return false; }
        return true;
    }
    // The end timer: releases everything if no newer transition replaced this one.
    void Finish(unsigned generation) { if (active_ && generation == generation_ && (phase_ == Phase::Incoming || phase_ == Phase::FadingOut)) Cancel(); }
    // Removes the overlay at once and releases both snapshots.
    void Cancel() {
        if (!active_) return;
        active_ = false; phase_ = Phase::Idle; ++generation_;
        if (oldV_) { oldV_->SetContent(nullptr); oldV_->SetEffect(nullptr); }
        if (newV_) { newV_->SetContent(nullptr); newV_->SetEffect(nullptr); }
        Release(oldS_); Release(newS_); if (device_) device_->Commit();
    }
    bool Active() const { return active_; }
    Phase CurrentPhase() const { return phase_; }
    bool Holding() const { return active_ && phase_ == Phase::Outgoing; }
    bool Waiting() const { return active_ && phase_ == Phase::Waiting; }
    RECT Rect() const { return rect_; }
    Kind CurrentKind() const { return kind_; }
    unsigned Generation() const { return generation_; }
    Budget CurrentBudget() const { return budget_; }
    // Time left in the current fading phase (Incoming / FadingOut); the caller schedules Finish then.
    float RemainingMs() const { return (phase_ == Phase::Incoming || phase_ == Phase::FadingOut) ? std::max(0.f, (duration_ - Seconds(Clock::now() - start_)) * 1000.f) : 0.f; }
    float CaptureMs() const { return captureMs_; }
    float IncomingCaptureMs() const { return incomingCaptureMs_; }
    void SetLastCaptureMsForTest(float ms) { lastCaptureMs_ = ms; }
    int Captures() const { return captures_; }
    UINT32 LastSampleForTest() const { return lastSample_; }
    bool LastCaptureOccluded() const { return occluded_; }
    // Tests on plain GDI windows: WM_PRINT (0) is deterministic there; WebView hosts need PW_RENDERFULLCONTENT.
    void SetPrintFlagsForTest(UINT flags) { printFlags_ = flags; }
    int Surfaces() const { return (oldS_ ? 1 : 0) + (newS_ ? 1 : 0); }
    int Retargets() const { return retargets_; }
    bool Available() const { return ok_; }
    // A second layer above this transition's, for the Mode Focus entry / exit (src/main.cpp « Mode Focus transition »):
    // a window has ONE topmost composition target, so it shares this device and target. Null when unavailable.
    IDCompositionVisual2* ExtraLayer(HWND window) { return Ensure(window) ? extra_ : nullptr; }
    IDCompositionDesktopDevice* Device() const { return ok_ ? device_ : nullptr; }
    // Premultiplied BGRA pixels into a new surface of this device (its previous one released).
    bool UploadSurface(const UINT32* pixels, int w, int h, IDCompositionSurface*& surface) { return ok_ && Upload(pixels, w, h, surface); }
    bool Blurring() const { return active_ && !reduced_ && blurOk_; }
    bool IncomingBlurring() const { return active_ && phase_ == Phase::Incoming && newBlurOk_; }
    // The values on screen now (tests and logs).
    Values Now() const { return Now(Clock::now()); }
    void Destroy() {
        Cancel();
        if (target_) target_->SetRoot(nullptr);
        if (device_) device_->Commit();
        Release(blurOld_); Release(blurNew_); Release(oldGroup_); Release(outerGroup_); Release(clip_); Release(extra_); Release(root_);
        Release(newV_); Release(oldV_); Release(oldC_); Release(outer_); Release(factory_); Release(target_); Release(device3_); Release(device_); Release(d3d_); Release(context_);
        if (dcomp_) { FreeLibrary(dcomp_); dcomp_ = nullptr; } if (d3dModule_) { FreeLibrary(d3dModule_); d3dModule_ = nullptr; }
        window_ = nullptr; tried_ = ok_ = false;
    }
private:
    using Clock = std::chrono::steady_clock;
    template<class T> static void Release(T*& p) { if (p) { p->Release(); p = nullptr; } }
    static float Seconds(Clock::duration d) { return std::chrono::duration<float>(d).count(); }
    void Place(RECT viewport) {
        rect_ = viewport;
        clip_->SetLeft(0.f); clip_->SetTop(0.f); clip_->SetRight(static_cast<float>(viewport.right - viewport.left)); clip_->SetBottom(static_cast<float>(viewport.bottom - viewport.top));
        outer_->SetOffsetX(static_cast<float>(viewport.left)); outer_->SetOffsetY(static_cast<float>(viewport.top));
    }
    Values Now(Clock::time_point now) const {
        if (!active_) return {0, 0, 0, 0};
        const float t = Seconds(now - start_), d = duration_;
        switch (phase_) {
        case Phase::Outgoing: case Phase::Waiting: return {reduced_ ? 0.f : BlurAt(t, d, v0_.oldBlur, peak_), v0_.oldOpacity, v0_.outer, 0.f};
        case Phase::Incoming: return {v0_.oldBlur, OldFadeAt(t, d, v0_.oldOpacity), DissolveAt(t, d, v0_.outer), SharpenAt(t, d, v0_.newBlur)};
        case Phase::FadingOut: return {v0_.oldBlur, OpacityAt(t, d, v0_.oldOpacity), v0_.outer, 0.f};
        default: return {0, 0, 0, 0};
        }
    }
    // Outgoing: the blur grows from its current value to the peak (held), opacities held while the switch runs.
    bool AnimateOutgoing(Clock::time_point now) {
        phase_ = Phase::Outgoing; start_ = now; const float d = duration_;
        bool ok = SUCCEEDED(oldGroup_->SetOpacity(v0_.oldOpacity)) && SUCCEEDED(outerGroup_->SetOpacity(v0_.outer));
        IDCompositionAnimation* grow = nullptr; blurOk_ = false;
        if (ok && !reduced_ && peak_ > 0 && SUCCEEDED(device_->CreateAnimation(&grow))) {
            const float s = .6f * d, k = peak_ - v0_.oldBlur;
            grow->AddCubic(0.0, v0_.oldBlur, 3 * k / s, -3 * k / (s * s), k / (s * s * s)); grow->AddCubic(s, peak_, 0, 0, 0); grow->End(3600.0, peak_);
            blurOk_ = SUCCEEDED(blurOld_->SetStandardDeviation(grow)) && SUCCEEDED(oldV_->SetEffect(blurOld_));
            if (!blurOk_) oldV_->SetEffect(nullptr);   // no blur on this system: an opacity-only transition, never a fake
        } else oldV_->SetEffect(nullptr);
        ok = ok && SUCCEEDED(device_->Commit()); Release(grow);
        if (!ok) { Cancel(); return false; }
        return true;
    }
    bool Ensure(HWND window) {
        if (ok_ && window == window_) return true;
        if (tried_ && window == window_) return false;
        Destroy(); tried_ = true; window_ = window;
        d3dModule_ = LoadLibraryExW(L"d3d11.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        dcomp_ = LoadLibraryExW(L"dcomp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        auto createD3D = d3dModule_ ? reinterpret_cast<PFN_D3D11_CREATE_DEVICE>(reinterpret_cast<void*>(GetProcAddress(d3dModule_, "D3D11CreateDevice"))) : nullptr;
        using Create3 = HRESULT(WINAPI*)(IUnknown*, REFIID, void**);
        auto create3 = dcomp_ ? reinterpret_cast<Create3>(reinterpret_cast<void*>(GetProcAddress(dcomp_, "DCompositionCreateDevice3"))) : nullptr;
        if (!createD3D || !create3) return false;
        HRESULT hr = E_FAIL;
        for (D3D_DRIVER_TYPE type : {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP}) { hr = createD3D(nullptr, type, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &d3d_, nullptr, &context_); if (SUCCEEDED(hr)) break; }
        IDXGIDevice* dxgi = nullptr;
        if (FAILED(hr) || FAILED(d3d_->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgi)))) return false;
        hr = create3(dxgi, __uuidof(IDCompositionDesktopDevice), reinterpret_cast<void**>(&device_)); dxgi->Release();
        if (FAILED(hr) || !device_ || FAILED(device_->QueryInterface(__uuidof(IDCompositionDevice3), reinterpret_cast<void**>(&device3_)))) return false;
        // outer (clip, offset, overall opacity) -> [ incoming (blur) ] below [ oldC (opacity) -> outgoing (blur) ]
        if (FAILED(device_->CreateTargetForHwnd(window, TRUE, &target_)) || FAILED(device_->CreateVisual(&outer_)) || FAILED(device_->CreateVisual(&newV_)) ||
            FAILED(device_->CreateVisual(&oldC_)) || FAILED(device_->CreateVisual(&oldV_)) ||
            FAILED(outer_->AddVisual(newV_, FALSE, nullptr)) || FAILED(outer_->AddVisual(oldC_, TRUE, newV_)) || FAILED(oldC_->AddVisual(oldV_, FALSE, nullptr)) ||
            FAILED(device_->CreateEffectGroup(&outerGroup_)) || FAILED(outer_->SetEffect(outerGroup_)) || FAILED(device_->CreateEffectGroup(&oldGroup_)) || FAILED(oldC_->SetEffect(oldGroup_)) ||
            FAILED(device_->CreateRectangleClip(&clip_)) || FAILED(outer_->SetClip(clip_)) ||
            FAILED(device3_->CreateGaussianBlurEffect(&blurOld_)) || FAILED(device3_->CreateGaussianBlurEffect(&blurNew_)) || FAILED(device_->CreateSurfaceFactory(d3d_, &factory_)) ||
            FAILED(device_->CreateVisual(&root_)) || FAILED(device_->CreateVisual(&extra_)) || FAILED(root_->AddVisual(outer_, FALSE, nullptr)) || FAILED(root_->AddVisual(extra_, TRUE, outer_)) ||
            FAILED(target_->SetRoot(root_)) || FAILED(device_->Commit())) return false;   // root -> [ outer (this transition), extra (Mode Focus) above ]
        blurOld_->SetBorderMode(D2D1_BORDER_MODE_HARD); blurNew_->SetBorderMode(D2D1_BORDER_MODE_HARD);   // blurred edges never show the canvas through
        ok_ = true; return true;
    }
    // Our OWN webview windows only, and only the ALLOWLISTED ones: the host windows of the pages shown now (one per
    // tab; two in split view), each verified to be a visible child of the browser window. Each is rendered with
    // PrintWindow (PW_RENDERFULLCONTENT) at its place over the canvas colour; never the desktop, never our overlay,
    // never another window (the previous tab's included, even overlapping). CPU buffers freed at once.
    bool Capture(HWND window, RECT viewport, const std::vector<HWND>& allow, IDCompositionSurface*& surface) {
        const int w = viewport.right - viewport.left, h = viewport.bottom - viewport.top;
        if (w <= 0 || h <= 0) return false;
        struct Page { HWND hwnd; RECT rect; }; std::vector<Page> pages; occluded_ = false;
        for (HWND c : allow) {
            if (!c || !IsWindow(c) || GetParent(c) != window || !IsWindowVisible(c)) continue;
            RECT r{}; GetWindowRect(c, &r); MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&r), 2);
            RECT x{}; if (!IntersectRect(&x, &r, &viewport)) continue;
            // PW_RENDERFULLCONTENT returns what DWM composed at this rectangle: if another (non-allowlisted) visible
            // sibling above it overlaps it (a previous tab's window not hidden yet), its pixels could be captured.
            // Refused: the caller fades instead (never another page's pixels).
            for (HWND z = GetWindow(c, GW_HWNDPREV); z; z = GetWindow(z, GW_HWNDPREV)) {
                wchar_t zc[64]{};   // only another page's host window matters (not our scrollbars / grips / bubbles)
                if (!IsWindowVisible(z) || std::find(allow.begin(), allow.end(), z) != allow.end() || !GetClassNameW(z, zc, 64) || std::wcsncmp(zc, L"Chrome_WidgetWin", 16) != 0) continue;
                RECT zr{}, o{}; GetWindowRect(z, &zr); MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&zr), 2);
                if (IntersectRect(&o, &zr, &r)) { occluded_ = true; return false; }
            }
            pages.push_back({c, r});
        }
        if (pages.empty()) return false;   // nothing of ours to show (native canvas only): the caller fades instead
        const UINT32 canvas = 0xFF000000u | (GetRValue(canvas_) << 16) | (GetGValue(canvas_) << 8) | GetBValue(canvas_);
        std::vector<UINT32> pixels(static_cast<size_t>(w) * h, canvas);
        bool any = false;
        HDC screen = GetDC(window); HDC mem = CreateCompatibleDC(screen);
        for (const auto& p : pages) {
            const int cw = p.rect.right - p.rect.left, ch = p.rect.bottom - p.rect.top; if (cw <= 0 || ch <= 0) continue;
            BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(bi.bmiHeader); bi.bmiHeader.biWidth = cw; bi.bmiHeader.biHeight = -ch; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
            void* bits = nullptr; HBITMAP dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0); if (!dib || !bits) { if (dib) DeleteObject(dib); continue; }
            HGDIOBJ old = SelectObject(mem, dib);
            const bool ok = PrintWindow(p.hwnd, mem, printFlags_) != FALSE;   // PW_RENDERFULLCONTENT: the WebView host's own GPU content
            SelectObject(mem, old);
            if (ok) {
                const auto* src = static_cast<const UINT32*>(bits); any = true;
                for (int y = 0; y < ch; ++y) { const int vy = p.rect.top + y - viewport.top; if (vy < 0 || vy >= h) continue;
                    for (int x = 0; x < cw; ++x) { const int vx = p.rect.left + x - viewport.left; if (vx < 0 || vx >= w) continue; pixels[static_cast<size_t>(vy) * w + vx] = src[static_cast<size_t>(y) * cw + x] | 0xFF000000u; } }
            }
            DeleteObject(dib);
        }
        DeleteDC(mem); ReleaseDC(window, screen);
        if (!any) return false;
        lastSample_ = pixels[static_cast<size_t>(h / 2) * w + w / 2];   // tests: what the snapshot holds (centre)
        return Upload(pixels.data(), w, h, surface);
    }
    bool Upload(const UINT32* pixels, int w, int h, IDCompositionSurface*& surface) {
        Release(surface);
        if (FAILED(factory_->CreateSurface(w, h, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, &surface))) return false;
        POINT at{}; IDXGISurface* dxgi = nullptr; ID3D11Texture2D* texture = nullptr;
        if (FAILED(surface->BeginDraw(nullptr, __uuidof(IDXGISurface), reinterpret_cast<void**>(&dxgi), &at))) { Release(surface); return false; }
        const bool ok = SUCCEEDED(dxgi->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture)));
        if (ok) { const D3D11_BOX box{static_cast<UINT>(at.x), static_cast<UINT>(at.y), 0, static_cast<UINT>(at.x + w), static_cast<UINT>(at.y + h), 1}; context_->UpdateSubresource(texture, 0, &box, pixels, w * 4, 0); texture->Release(); }
        dxgi->Release();
        if (FAILED(surface->EndDraw()) || !ok) { Release(surface); return false; }
        return true;
    }
    HWND window_ = nullptr; HMODULE dcomp_ = nullptr, d3dModule_ = nullptr; bool tried_ = false, ok_ = false;
    ID3D11Device* d3d_ = nullptr; ID3D11DeviceContext* context_ = nullptr;
    IDCompositionDesktopDevice* device_ = nullptr; IDCompositionDevice3* device3_ = nullptr; IDCompositionTarget* target_ = nullptr;
    IDCompositionVisual2 *outer_ = nullptr, *newV_ = nullptr, *oldC_ = nullptr, *oldV_ = nullptr, *root_ = nullptr, *extra_ = nullptr;
    IDCompositionEffectGroup *outerGroup_ = nullptr, *oldGroup_ = nullptr; IDCompositionRectangleClip* clip_ = nullptr;
    IDCompositionGaussianBlurEffect *blurOld_ = nullptr, *blurNew_ = nullptr; IDCompositionSurfaceFactory* factory_ = nullptr;
    IDCompositionSurface *oldS_ = nullptr, *newS_ = nullptr; COLORREF canvas_ = RGB(0x17, 0x17, 0x17);
    bool active_ = false, reduced_ = false, blurOk_ = false, newBlurOk_ = false; Phase phase_ = Phase::Idle; Kind kind_ = Kind::Tab; Budget budget_ = Budget::Full;
    RECT rect_{}; unsigned generation_ = 0; int retargets_ = 0, captures_ = 0; UINT32 lastSample_ = 0; bool occluded_ = false; UINT printFlags_ = 0x2 /* PW_RENDERFULLCONTENT */;
    Clock::time_point start_{}; float duration_ = 0, peak_ = 0, captureMs_ = 0, incomingCaptureMs_ = 0, lastCaptureMs_ = 0;
    Values v0_{0, 1, 1, 0};
};

} // namespace ContentTransition
