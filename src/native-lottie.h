#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace NativeChrome::Lottie {
struct Frame { unsigned side=0; std::vector<uint32_t> bgra; };
// UI-thread owned, synchronous ThorVG software renderer. No WebView or JS.
// At most 120 frames, one DPI resolution per asset, at most 64x64 pixels.
class Asset {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    Asset(); ~Asset();
    Asset(const Asset&)=delete; Asset& operator=(const Asset&)=delete;
    bool Load(const std::wstring& path);
    void Clear();
    double Duration()const;
    unsigned FrameCount()const;
    size_t CacheBytes()const;
    const Frame* Sample(double progress,unsigned side);
};
}
