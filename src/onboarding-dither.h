#pragma once
// Onboarding board 6 « Mode Annotation » — the dither over its gradient (the designer's request: « de manière
// subtile, mais là »). It is the annotation band's own texture, not a lookalike: the « vague tramée » of
// src/annotation-dither.h (Paper « MCP - Design UI UX » 5.2 « Bandeau vivant »), same 4 x 4 Bayer ordered
// dithering, same one-dot-per-cell look with the last pixel row and column left empty, same 2 DIP cell snapped
// to device pixels, same field (a light base that grows downwards + the travelling wave), same two dot tones
// (dim below 0.75, bright above, in the band's 150 : 220 alpha ratio).
// What changes is what the band's field is laid on: the gradient is 725 x 407 where the band is a strip, so the
// band's height is stretched over the gradient's (the crest still swings every 210 DIP along x and the envelope
// still travels one full width per loop, seamlessly), and the ink is white — lighter dots over the dark teal,
// a faint veil over the light part — at an opacity of its own. Pure: no HWND, no Direct2D, no clock.
#include "annotation-dither.h"

namespace Onboarding {
namespace Trame {

namespace D = NativeChrome::Annotations::Dither;
struct Look {
    float opacity = .22f;     // of the whole texture (the band's is 0.85 on its own accent)
    float cell = D::Wave::cell;   // DIP, the band's pitch
    float base = .18f;        // the band's base density (0.18 y / H)
    float wave = 1;           // the wave's amplitude (the band's is 1 once risen)
};
inline constexpr float bandHeight = 40;   // the strip the band's formulas were written for
// The band's field, stretched over a `w` x `h` DIP area.
inline float Field(float x, float y, float w, float h, double phase, const Look& look) noexcept {
    const float base = look.base * D::Clamp01(y / std::max(h, 1.f));
    return look.wave > 0 ? base + D::WaveBand(x, y * bandHeight / std::max(h, 1.f), w, bandHeight, phase) * look.wave : base;
}
// Premultiplied BGRA dots over a transparent raster (`r` is cleared). Returns the number of dots.
inline int Raster(const D::Raster& r, float scale, double phase, const Look& look) {
    const int cp = D::CellPx(look.cell, scale), dot = cp - D::GapPx(scale); int dots = 0;
    const float w = r.w / scale, h = r.h / scale;
    const std::uint32_t dim = D::Premul(0xFFFFFF96, look.opacity), bright = D::Premul(0xFFFFFFDC, look.opacity);   // white at the band's 150 / 220 alphas
    for (int y = 0; y < r.h; ++y) std::fill_n(r.px + std::size_t(y) * r.stride, r.w, 0u);
    if (look.opacity <= 0) return 0;
    // Field(), cell by cell, without paying its sine and its two exponentials for each of the ~70 000 cells:
    // in D::WaveBand the crest and the envelope only depend on x (one value per column), and the band is zero
    // four widths away from its crest. Same numbers as Field() (checked in the render test).
    static thread_local std::vector<float> crest, envelope;
    const int columns = (r.w + cp - 1) / cp; crest.assign(std::size_t(columns), 0); envelope.assign(std::size_t(columns), 0);
    for (int cx = 0; cx < columns && look.wave > 0; ++cx) {
        const float x = (cx + .5f) * cp / scale;
        crest[std::size_t(cx)] = bandHeight * (.55f + .30f * float(std::sin(6.283185307179586 * (x / 210.0 - phase))));
        double d = x / std::max(w, 1.f) - (.15 + phase); d -= std::floor(d + .5);
        const float e = float(d / .22); envelope[std::size_t(cx)] = .85f * (.35f + .65f * std::exp(-e * e)) * look.wave;
    }
    for (int cy = 0; cy * cp < r.h; ++cy) for (int cx = 0; cx < columns; ++cx) {
        const float y = (cy + .5f) * cp / scale, b = (y * bandHeight / std::max(h, 1.f) - crest[std::size_t(cx)]) / 5.5f;
        float v = look.base * D::Clamp01(y / std::max(h, 1.f));
        if (look.wave > 0 && b > -4 && b < 4) v += std::exp(-b * b) * envelope[std::size_t(cx)];
        if (v <= D::Threshold(cx, cy)) continue;
        const std::uint32_t c = v < .75f ? dim : bright; ++dots;
        for (int py = cy * cp; py < std::min(r.h, cy * cp + dot); ++py) std::fill_n(r.px + std::size_t(py) * r.stride + cx * cp, std::min(dot, r.w - cx * cp), c);
    }
    return dots;
}

} // namespace Trame
} // namespace Onboarding
