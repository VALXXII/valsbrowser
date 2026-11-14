#pragma once
// Annotation mode V3-B — the entry veil (Paper 5.1 « Entrée du mode — pic du dégradé », track « Dégradé du viewport »
// of 5.2, validated). At the entry only (none at the exit: « seule l'entrée se célèbre »):
//   a child layered window over the page area, click-through (WS_EX_TRANSPARENT) and never activated, carrying a
//   wash in the window accent (Paper #3E67FF) that falls 160 DIP from the band, an inset rim (~48 DIP) on the three other sides, and the dithered
//   texture rippling (src/annotation-dither.h). Up in 140 ms, held 80 ms, out by 500 ms, then the window is DESTROYED.
//   Interruptible: leaving the mode during the entry fades it from the value on screen (160 ms).
//   Reduced motion: the wash without the texture, 50 %, 150 ms in and 150 ms out.
// The pixels are written straight into the layered surface (no Direct2D): one full-page frame costs a few ms.
// Created at the entry, destroyed at the end: nothing is left between two entries.
#include "annotation-kit.h"
#include "annotation-dither.h"

namespace NativeChrome { namespace Annotations {
class EntryVeil : public Animator::Client {
    Layer layer_; HINSTANCE instance_ = nullptr; HWND parent_ = nullptr; std::wstring root_;
    Dither::VeilTimeline timeline_; Motion::Time start_{}; RECT rect_{}; bool running_ = false;
    unsigned long long frames_ = 0; float peak_ = 0, last_ = 0; double lastMs_ = 0, endedAfter_ = -1, frameCost_ = 0, worstFrame_ = 0, rasterCost_ = 0;
    std::function<void(double ms, unsigned long long frames, double worstFrameMs)> ended_;
    double Ms(Motion::Time now) const { return std::chrono::duration<double, std::milli>(now - start_).count(); }
    void Render(Motion::Time now) {
        const double ms = Ms(now); const float k = timeline_.Value(ms); const bool texture = !timeline_.Reduced();
        const std::uint32_t accent = AccentRgb24();   // the window accent as presented, read per frame
        const float scale = layer_.Scale(); const double seconds = ms / 1000.0;
        LARGE_INTEGER f{}, a{}, b{}; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&a);
        layer_.PresentPixels(rect_, [&](std::uint32_t* px, int stride, int w, int h) {
            LARGE_INTEGER r0{}, r1{}; QueryPerformanceCounter(&r0);
            Dither::RasterVeil({px, stride, w, h}, scale, seconds, k, texture, accent);
            QueryPerformanceCounter(&r1); rasterCost_ += double(r1.QuadPart - r0.QuadPart) * 1000.0 / double(f.QuadPart); }, frames_ == 0);
        QueryPerformanceCounter(&b); const double cost = double(b.QuadPart - a.QuadPart) * 1000.0 / double(f.QuadPart);
        frameCost_ += cost; worstFrame_ = std::max(worstFrame_, cost);
        ++frames_; last_ = k; peak_ = std::max(peak_, k); lastMs_ = ms;
    }
public:
    ~EntryVeil() { if (Animator::Exists()) Animator::Get().Forget(this); }
    void Init(HINSTANCE instance, HWND parent, std::wstring root) { instance_ = instance; parent_ = parent; root_ = std::move(root); }
    // page: the page area in the parent's client pixels. False: nothing shown (no window).
    bool Start(RECT page, bool reduced, Motion::Time now = Motion::Now()) {
        if (IsRectEmpty(&page) || !Ink::Shared(root_)) return false;
        if (running_) Destroy();
        Layer::Options o; o.child = true; o.clickThrough = true; o.title = L"";
        if (!layer_.Create(instance_, parent_, o)) return false;
        rect_ = page; start_ = now; timeline_.Start(reduced); running_ = true; frames_ = 0; peak_ = last_ = 0; lastMs_ = 0; endedAfter_ = -1; frameCost_ = worstFrame_ = rasterCost_ = 0;
        Animator::Get().Request(this);
        return true;
    }
    void Move(RECT page) { if (running_ && !IsRectEmpty(&page)) rect_ = page; }
    // The mode ended during the entry: out from the presented value.
    void Interrupt(Motion::Time now = Motion::Now()) { if (running_) { timeline_.Interrupt(Ms(now)); Animator::Get().Request(this); } }
    bool Frame(Motion::Time now) override {
        if (!running_) return false;
        const double ms = Ms(now);
        if (timeline_.Done(ms)) { endedAfter_ = ms; Destroy(); return false; }
        Render(now);
        return true;
    }
    void Destroy() {
        const bool was = running_; running_ = false; layer_.Destroy();
        if (Animator::Exists()) Animator::Get().Forget(this);
        if (was && ended_) ended_(endedAfter_, frames_, worstFrame_);
    }
    void OnEnded(std::function<void(double, unsigned long long, double)> f) { ended_ = std::move(f); }
    bool Running() const { return running_; }
    HWND Window() const { return layer_.Window(); }
    // Tests / measurements.
    Layer& LayerForTest() { return layer_; }
    const Dither::VeilTimeline& Timeline() const { return timeline_; }
    void RenderForTest(Motion::Time now) { Render(now); }
    unsigned long long Frames() const { return frames_; }
    float Peak() const { return peak_; }
    float LastValue() const { return last_; }
    double LastMs() const { return lastMs_; }
    double EndedAfter() const { return endedAfter_; }
    double AverageFrameMs() const { return frames_ ? frameCost_ / double(frames_) : 0; }
    double WorstFrameMs() const { return worstFrame_; }
    double AverageRasterMs() const { return frames_ ? rasterCost_ / double(frames_) : 0; }
};
}} // namespace NativeChrome::Annotations
