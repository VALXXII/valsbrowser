// Review renders for the download success morph (design/lottie/download-success).
// Composes EXACTLY like the toolbar (src/download-icon.h DownloadIcon::Compose): ring track + fill
// held at the completed frame (FillEnd = 54), fill tinted, then the success cut and arrow channels,
// over the canvas #171717. Small sizes use the production player (src/native-lottie.cpp, <= 64 px);
// the large sheet renders the same Lottie files with ThorVG at 256 px.
// Writes PNG files into the folder given as argv[1].
#define NOMINMAX
#include "../../../src/native-lottie.h"
#include <thorvg.h>
#include <windows.h>
#include <wincodec.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace L = NativeChrome::Lottie;
// Mirror of src/download-icon.h (frame layout + Compose with the accent tint, as in the lead's working
// copy), kept here so this review tool does not depend on a file being edited by the integration.
namespace DI {
constexpr int Morph = 14, Steps = 40, Out = 27, FillEnd = Morph + Steps, RingFrames = FillEnd + Out + 1, ArrowFrames = Morph + 78 + Out + 1;
constexpr float Ink = .5f;
inline void Compose(const uint32_t* track, const uint32_t* fill, const uint32_t* cut, const uint32_t* arrow, size_t count, std::vector<uint32_t>& out, const float* tint) {
    out.resize(count);
    for (size_t i = 0; i < count; ++i) {
        const float ta = (track[i] >> 24) / 255.f * Ink, aa = (arrow[i] >> 24) / 255.f * Ink, keep = 1 - (cut[i] >> 24) / 255.f, fa = (fill[i] >> 24) / 255.f;
        float c[3], a;
        for (int ch = 0; ch < 3; ++ch) c[ch] = tint[2 - ch] * fa + ta * (1 - fa);
        a = fa + ta * (1 - fa);
        for (int ch = 0; ch < 3; ++ch) c[ch] = aa + c[ch] * keep * (1 - aa);
        a = aa + a * keep * (1 - aa);
        uint32_t px = static_cast<uint32_t>(std::lround(std::clamp(a, 0.f, 1.f) * 255)) << 24;
        for (int ch = 0; ch < 3; ++ch) px |= static_cast<uint32_t>(std::lround(std::clamp(c[ch], 0.f, 1.f) * 255)) << (8 * ch);
        out[i] = px;
    }
}
}
static std::wstring assets = L"assets\\lottie\\";

struct Image { unsigned w = 0, h = 0; std::vector<uint32_t> px; };   // BGRA, opaque
static bool SavePng(const Image& img, const std::wstring& path) {
    IWICImagingFactory* f = nullptr; IWICBitmapEncoder* e = nullptr; IWICBitmapFrameEncode* fr = nullptr; IWICStream* s = nullptr;
    bool ok = SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f))) && SUCCEEDED(f->CreateStream(&s))
              && SUCCEEDED(s->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) && SUCCEEDED(f->CreateEncoder(GUID_ContainerFormatPng, nullptr, &e))
              && SUCCEEDED(e->Initialize(s, WICBitmapEncoderNoCache)) && SUCCEEDED(e->CreateNewFrame(&fr, nullptr)) && SUCCEEDED(fr->Initialize(nullptr))
              && SUCCEEDED(fr->SetSize(img.w, img.h));
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
    ok = ok && SUCCEEDED(fr->SetPixelFormat(&fmt)) && SUCCEEDED(fr->WritePixels(img.h, img.w * 4, img.w * img.h * 4, reinterpret_cast<BYTE*>(const_cast<uint32_t*>(img.px.data()))))
         && SUCCEEDED(fr->Commit()) && SUCCEEDED(e->Commit());
    if (fr) fr->Release(); if (e) e->Release(); if (s) s->Release(); if (f) f->Release();
    return ok;
}
// Premultiplied BGRA over the canvas colour.
static uint32_t Over(uint32_t p, uint32_t bg = 0xFF171717) {
    const float a = (p >> 24) / 255.f; uint32_t out = 0xFF000000;
    for (int ch = 0; ch < 3; ++ch) { const float c = ((p >> (8 * ch)) & 255) / 255.f + ((bg >> (8 * ch)) & 255) / 255.f * (1 - a); out |= static_cast<uint32_t>(std::lround(std::min(1.f, c) * 255)) << (8 * ch); }
    return out;
}
// Large renders: the same Lottie file through ThorVG at any size (ARGB premultiplied).
struct Big {
    tvg::Animation* anim = nullptr; tvg::SwCanvas* canvas = nullptr; std::vector<uint32_t> buf; unsigned side = 0;
    bool Load(const std::wstring& path, unsigned s) {
        std::ifstream in(std::filesystem::path(path), std::ios::binary); std::string json((std::istreambuf_iterator<char>(in)), {});
        anim = tvg::Animation::gen(); canvas = tvg::SwCanvas::gen(); side = s; buf.assign(s * s, 0);
        return anim && canvas && anim->picture()->load(json.data(), static_cast<uint32_t>(json.size()), "lottie", nullptr, true) == tvg::Result::Success
               && canvas->target(buf.data(), s, s, s, tvg::ColorSpace::ARGB8888) == tvg::Result::Success && anim->picture()->size(float(s), float(s)) == tvg::Result::Success
               && canvas->add(anim->picture()) == tvg::Result::Success;
    }
    const std::vector<uint32_t>& Frame(float f) { anim->frame(f); canvas->update(); canvas->draw(true); canvas->sync(); return buf; }
};

static const float Green[3] = {0x6F / 255.f, 0xD0 / 255.f, 0x87 / 255.f}, Blue[3] = {0x3E / 255.f, 0x67 / 255.f, 0xFF / 255.f};

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) return 2;
    const std::wstring out = argv[1];
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    tvg::Initializer::init(0);
    const int frames[] = {0, 6, 12, 18, 24};   // start, quarter, half, three-quarter, end (400 ms morph)
    const float* tints[] = {Blue, Green};
    const wchar_t* tintNames[] = {L"blue", L"green"};
    int failures = 0;
    // ---- Production pipeline at 16 / 24 / 32 px (x1 DPI sizes), each frame upscaled 8x for review ----
    for (unsigned side : {16u, 24u, 32u}) {
        L::Asset track, fill, cut, arrow, oldArrow, oldCut;
        if (!track.Load(assets + L"download-ring-track.json") || !fill.Load(assets + L"download-ring-fill.json") || !cut.Load(assets + L"download-success-cutout.json")
            || !arrow.Load(assets + L"download-success.json") || !oldArrow.Load(assets + L"download-arrow.json") || !oldCut.Load(assets + L"download-arrow-cutout.json")) { std::printf("load failed\n"); return 1; }
        if (arrow.FrameCount() != 25 || cut.FrameCount() != 25) { std::printf("FAIL frame count %u/%u\n", arrow.FrameCount(), cut.FrameCount()); ++failures; }
        const auto* t = track.Sample(double(DI::FillEnd) / (DI::RingFrames - 1), side); const auto t0 = t->bgra;
        const auto* fl = fill.Sample(double(DI::FillEnd) / (DI::RingFrames - 1), side); const auto f0 = fl->bgra;
        const unsigned scale = 8, cols = 6, cell = side * scale + 8;
        for (int ti = 0; ti < 2; ++ti) {
            Image sheet; sheet.w = cell * cols; sheet.h = cell; sheet.px.assign(sheet.w * sheet.h, 0xFF171717);
            auto blit = [&](const std::vector<uint32_t>& px, unsigned col) {
                for (unsigned y = 0; y < side * scale; ++y) for (unsigned x = 0; x < side * scale; ++x)
                    sheet.px[(4 + y) * sheet.w + col * cell + 4 + x] = Over(px[(y / scale) * side + x / scale]);
            };
            std::vector<uint32_t> composed;
            // Column 0: the existing icon at the loop boundary (old arrow frame 14) = continuity reference.
            { const auto a = oldArrow.Sample(14.0 / (DI::ArrowFrames - 1), side)->bgra; const auto c = oldCut.Sample(14.0 / (DI::ArrowFrames - 1), side)->bgra;
              DI::Compose(t0.data(), f0.data(), c.data(), a.data(), side * side, composed, tints[ti]); blit(composed, 0);
              std::vector<uint32_t> first; const auto a1 = arrow.Sample(0, side)->bgra; const auto c1 = cut.Sample(0, side)->bgra;
              DI::Compose(t0.data(), f0.data(), c1.data(), a1.data(), side * side, first, tints[ti]);
              if (first != composed) { std::printf("FAIL %upx: success frame 0 differs from the existing arrow at its loop boundary\n", side); ++failures; }
              else std::printf("ok   %upx: success frame 0 == existing icon at the loop boundary (pixel-identical)\n", side); }
            for (int i = 0; i < 5; ++i) {
                const auto a = arrow.Sample(frames[i] / 24.0, side)->bgra; const auto c = cut.Sample(frames[i] / 24.0, side)->bgra;
                DI::Compose(t0.data(), f0.data(), c.data(), a.data(), side * side, composed, tints[ti]); blit(composed, i + 1);
            }
            SavePng(sheet, out + L"\\toolbar-" + std::to_wstring(side) + L"px-" + tintNames[ti] + L".png");
        }
    }
    // ---- Large review sheet: 256 px, every 3rd frame + the five key frames, composited the same way ----
    Big track, fill, cut, arrow;
    if (!track.Load(assets + L"download-ring-track.json", 256) || !fill.Load(assets + L"download-ring-fill.json", 256) || !cut.Load(assets + L"download-success-cutout.json", 256) || !arrow.Load(assets + L"download-success.json", 256)) { std::printf("big load failed\n"); return 1; }
    const auto t = track.Frame(float(DI::FillEnd)); const auto fl = fill.Frame(float(DI::FillEnd));
    const unsigned cols = 13, rows = 2, cell = 264;
    Image sheet; sheet.w = cell * cols; sheet.h = cell * rows; sheet.px.assign(sheet.w * sheet.h, 0xFF171717);
    int all[25]; for (int i = 0; i < 25; ++i) all[i] = i;   // every frame of the 400 ms morph
    for (int k = 0; k < 25; ++k) {
        const auto a = arrow.Frame(float(all[k])); const auto c = cut.Frame(float(all[k]));
        std::vector<uint32_t> composed; DI::Compose(t.data(), fl.data(), c.data(), a.data(), 256 * 256, composed, Green);
        Image one; one.w = one.h = 256; one.px.resize(256 * 256);
        for (unsigned i = 0; i < 256 * 256; ++i) one.px[i] = Over(composed[i]);
        for (unsigned y = 0; y < 256; ++y) for (unsigned x = 0; x < 256; ++x) sheet.px[((k / cols) * cell + 4 + y) * sheet.w + (k % cols) * cell + 4 + x] = one.px[y * 256 + x];
        if (all[k] % 6 == 0) SavePng(one, out + L"\\frame-" + std::to_wstring(all[k]) + L"-256.png");
    }
    SavePng(sheet, out + L"\\sheet-256-green.png");
    // Arrow channel alone at 256 (white on canvas): shows the stem folding onto the long arm with no blob.
    Image ch; ch.w = cell * cols; ch.h = cell * rows; ch.px.assign(ch.w * ch.h, 0xFF171717);
    for (int k = 0; k < 25; ++k) { const auto a = arrow.Frame(float(all[k])); for (unsigned y = 0; y < 256; ++y) for (unsigned x = 0; x < 256; ++x) ch.px[((k / cols) * cell + 4 + y) * ch.w + (k % cols) * cell + 4 + x] = Over(a[y * 256 + x]); }
    SavePng(ch, out + L"\\arrow-channel-256.png");
    // The last frame is the official check: pixel-identical to the stem-less reference at every size.
    for (unsigned side : {16u, 24u, 32u, 64u}) {
        L::Asset a, ref;
        if (!a.Load(assets + L"download-success.json") || !ref.Load(out + L"\\reference-check.json")) { std::printf("FAIL reference load\n"); ++failures; break; }
        const auto x = a.Sample(1.0, side)->bgra; const auto y = ref.Sample(0.0, side)->bgra;
        int worst = 0, differing = 0;
        for (size_t i = 0; i < x.size(); ++i) {
            const int d = std::abs(int(x[i] >> 24) - int(y[i] >> 24)); worst = std::max(worst, d); differing += d > 0;
        }
        // Frame-to-frame motion around the stem/head hand-over (frame 9) and at the end: the hand-over
        // step must not stand out from the normal motion of its neighbours.
        for (int f : {8, 9, 10, 23, 24}) {
            const auto prev = a.Sample((f - 1) / 24.0, side)->bgra; const auto cur = a.Sample(f / 24.0, side)->bgra; int moved = 0, step = 0;
            for (size_t i = 0; i < cur.size(); ++i) { const int d = std::abs(int(cur[i] >> 24) - int(prev[i] >> 24)); moved += d > 0; step = std::max(step, d); }
            std::printf("     %upx: frame %d -> %d changes %d px (max alpha step %d/255)\n", side, f - 1, f, moved, step);
        }
        if (x != y) { std::printf("FAIL %upx: last frame differs from the official check (%d px, max alpha diff %d/255)\n", side, differing, worst); ++failures; }
        else std::printf("ok   %upx: last frame == official Phosphor Bold check (pixel-identical)\n", side);
    }
    std::printf(failures ? "%d FAIL\n" : "review renders written\n", failures);
    return failures ? 1 : 0;
}
