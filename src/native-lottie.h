#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace NativeChrome::Lottie {
struct Frame { unsigned side=0; std::vector<uint32_t> bgra; };
// Load limits. The defaults protect every existing asset (256 KiB, 120 frames, 10 s, every frame
// cached). An asset that needs more opts in explicitly (the « Nouveautés » logo, src/modal.h): its
// own byte/frame ceilings, and `cacheFrames = false` renders each sampled frame on demand into one
// reusable buffer instead of keeping every frame (memory stays one frame, whatever the length).
struct Limits {
    size_t maxBytes = 256 * 1024;
    double maxFrames = 120, maxSeconds = 10;
    bool cacheFrames = true;
    // Largest frame side in pixels. The toolbar's icons stay at the historical 64; the update card's 53 DIP icon
    // (the Downloads drawer's frames, src/update-toast.cpp) asks for up to 256 (200 % and beyond) with cacheFrames off.
    unsigned maxSide = 64;
};
// UI-thread owned, synchronous ThorVG software renderer. No WebView or JS.
// At most Limits::maxFrames frames, one DPI resolution per asset, at most Limits::maxSide pixels a side (64 by default).
class Asset {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    Asset(); ~Asset();
    Asset(const Asset&)=delete; Asset& operator=(const Asset&)=delete;
    bool Load(const std::wstring& path, const Limits& limits = {});
    void Clear();
    double Duration()const;
    unsigned FrameCount()const;
    size_t CacheBytes()const;
    const Frame* Sample(double progress,unsigned side);
};
}
