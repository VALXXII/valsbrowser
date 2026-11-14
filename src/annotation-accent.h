#pragma once
// Annotation mode — the window's profile accent carried to every Paper tone of the mode (pure: no Win32, no Direct2D).
//
// Every accent colour of the mode in Paper (p-H-0, section 5) is a tone of the reference accent #3E67FF: the band's
// gradient #233FD6, its label #5B84FF, the « BETA » label #6A8FFF, the mark's ramp, the wave's bright dots #7FA0FF…
// The window's accent is the profile's FILL (src/profiles-integration.h: OKLCH, L solved for white >= 4.6:1; the
// neutral « Personnel » keeps #3E67FF, Paper P07). Each Paper tone is carried to the fill by the same OKLCH move that
// takes #3E67FF to it: lightness + dL, chroma x (C tone / C reference), hue + dh, chroma then fitted into sRGB (hue and
// lightness kept). The reference fill gives the Paper value back EXACTLY (no rounding drift for « Personnel »).
// A tone used for text gets a minimal contrast floor on its real backdrop: its lightness is raised in small steps
// (hue kept) until the WCAG ratio is met on EVERY composited backdrop row its text box covers (the band's gradient
// lightens towards the bottom: review of e204fd6, BETA was floored on the chip's middle row only and fell to 4.32:1
// lower down); a tone that already passes everywhere is returned unchanged — the Personnel shortcut never skips it.
// Colours are 0xRRGGBB (no alpha) unless a name says RGBA.
#include <cstdint>
#include <vector>
#include "profile-color.h"

namespace NativeChrome { namespace Annotations { namespace AccentTone {
inline constexpr std::uint32_t reference = 0x3E67FF;   // Paper --color-browser-accent (Design::Accent::defaultRgb)
inline constexpr std::uint32_t canvas = 0x171717;

inline ProfileColor::Rgb8 Rgb8Of(std::uint32_t c) { return {std::uint8_t((c >> 16) & 255), std::uint8_t((c >> 8) & 255), std::uint8_t(c & 255)}; }
inline std::uint32_t Pack(ProfileColor::Rgb8 c) { return (std::uint32_t(c.r) << 16) | (std::uint32_t(c.g) << 8) | std::uint32_t(c.b); }
inline double Contrast(std::uint32_t a, std::uint32_t b) { return ProfileColor::Contrast(Rgb8Of(a), Rgb8Of(b)); }
// `top` at `alpha` over `under`, as rendered (sRGB blend, rounded to bytes).
inline std::uint32_t Over(std::uint32_t top, double alpha, std::uint32_t under) { return Pack(ProfileColor::Over(Rgb8Of(top), alpha, Rgb8Of(under))); }

struct Lch { double L = 0, C = 0, h = 0; };
inline Lch ToOklch(std::uint32_t rgb) {
    namespace D = ProfileColor::Detail;
    const double r = D::ToLinear(((rgb >> 16) & 255) / 255.0), g = D::ToLinear(((rgb >> 8) & 255) / 255.0), b = D::ToLinear((rgb & 255) / 255.0);
    const double l = std::cbrt(.4122214708 * r + .5363325363 * g + .0514459929 * b);
    const double m = std::cbrt(.2119034982 * r + .6806995451 * g + .1073969566 * b);
    const double s = std::cbrt(.0883024619 * r + .2817188376 * g + .6299787005 * b);
    const double L = .2104542553 * l + .7936177850 * m - .0040720468 * s;
    const double A = 1.9779984951 * l - 2.4285922050 * m + .4505937099 * s, B = .0259040371 * l + .7827717662 * m - .8086757660 * s;
    double h = std::atan2(B, A) * 180 / D::Pi; if (h < 0) h += 360;
    return {L, std::sqrt(A * A + B * B), h};
}
inline std::uint32_t FromOklch(Lch c) {
    namespace D = ProfileColor::Detail;
    const double L = std::clamp(c.L, 0.0, 1.0), h = std::fmod(std::fmod(c.h, 360.0) + 360.0, 360.0);
    return Pack(D::Pack(D::FromOklch(L, D::FitChroma(L, std::max(0.0, c.C), h), h)));
}
// The Paper tone `paper` (a tone of #3E67FF) for the accent `fill`.
inline std::uint32_t Relative(std::uint32_t fill, std::uint32_t paper) {
    fill &= 0xFFFFFF; paper &= 0xFFFFFF;
    if (fill == reference) return paper;
    const Lch f = ToOklch(fill), r = ToOklch(reference), t = ToOklch(paper);
    return FromOklch({f.L + (t.L - r.L), f.C * (r.C > 0 ? t.C / r.C : 1), f.h + (t.h - r.h)});
}
// `fg` with its lightness raised (hue and chroma kept, fitted into sRGB) until it reaches `minimum`:1 on `bg`.
inline std::uint32_t Readable(std::uint32_t fg, std::uint32_t bg, double minimum) {
    if (Contrast(fg, bg) >= minimum) return fg;
    Lch c = ToOklch(fg); std::uint32_t out = fg;
    for (int i = 0; i < 200 && c.L < 1; ++i) { c.L = std::min(1.0, c.L + .0025); out = FromOklch(c); if (Contrast(out, bg) >= minimum) break; }
    return out;
}
// The same on several backdrops: the floor is the worst of them (the lightest backdrop, for a light text).
template <class Backdrops> inline double WorstContrast(std::uint32_t fg, const Backdrops& bgs) {
    double worst = 99; for (std::uint32_t bg : bgs) worst = std::min(worst, Contrast(fg, bg)); return worst;
}
template <class Backdrops> inline std::uint32_t ReadableOnAll(std::uint32_t fg, const Backdrops& bgs, double minimum) {
    if (WorstContrast(fg, bgs) >= minimum) return fg;
    Lch c = ToOklch(fg); std::uint32_t out = fg;
    for (int i = 0; i < 200 && c.L < 1; ++i) { c.L = std::min(1.0, c.L + .0025); out = FromOklch(c); if (WorstContrast(out, bgs) >= minimum) break; }
    return out;
}
inline std::uint32_t Rgba(std::uint32_t rgb, std::uint32_t alphaByte) { return ((rgb & 0xFFFFFF) << 8) | (alphaByte & 255); }

// ---------------------------------------------------------------- the band (Paper 1.1 / 5.2)
namespace Paper {   // the reference values (fill #3E67FF)
inline constexpr std::uint32_t gradientEnd = 0x233FD6, label = 0x5B84FF, beta = 0x6A8FFF;
inline constexpr std::uint32_t markLight = 0xD1DFFF, markMid = 0x3296FF, markDeep = 0x2F63E7, waveBright = 0x7FA0FF;
inline constexpr std::uint32_t borderAlpha = 0x3D, waveDimAlpha = 0x96, waveBrightAlpha = 0xDC;
inline constexpr float gradientAlpha = .32f, betaChip = .16f;
inline constexpr double textMinimum = 4.5;   // 1.4.3 AA: « Mode Annotation » 14 px, « BETA » 12 px
// Text boxes in the 32 DIP band (annotation-banner.h LayoutBanner): « Mode Annotation » 6..26, the « BETA » chip
// (h 18, centred) 7..25. The glyphs sit inside; the floors cover the whole box (conservative bound).
inline constexpr float bandHeight = 32, labelTop = 6, labelBottom = 26, betaTop = 7, betaBottom = 25;
}
struct Band {
    std::uint32_t fill = reference;
    std::uint32_t gradientEnd = Paper::gradientEnd;             // RGB, drawn at 0 -> 32 % top to bottom
    std::uint32_t border = Rgba(reference, Paper::borderAlpha);  // RGBA
    std::uint32_t label = Paper::label, beta = Paper::beta;      // RGB, text
    std::uint32_t markLight = Paper::markLight, markMid = Paper::markMid, markDeep = Paper::markDeep;
    std::uint32_t waveDim = Rgba(reference, Paper::waveDimAlpha), waveBright = Rgba(Paper::waveBright, Paper::waveBrightAlpha);   // RGBA
};
// The band's background at y (DIP, 0..32) as composed natively: the canvas, then the vertical gradient (gradient end at
// 0 -> 32 % from the top to the bottom, annotation-banner.h PaintBanner). The texture's dots are not included (their
// own, existing limit). Lighter downwards: the gradient end is always lighter than the canvas.
inline std::uint32_t BandAt(std::uint32_t gradientEnd, double y) {
    return Over(gradientEnd, Paper::gradientAlpha * std::clamp(y / Paper::bandHeight, 0.0, 1.0), canvas);
}
// Every backdrop row behind a text box, top to bottom in 0.25 DIP steps (finer than a device pixel at 400 %).
inline std::vector<std::uint32_t> LabelBackdrops(std::uint32_t gradientEnd) {
    std::vector<std::uint32_t> out; for (double y = Paper::labelTop; y <= Paper::labelBottom + 1e-9; y += .25) out.push_back(BandAt(gradientEnd, y)); return out;
}
// The « BETA » chip: the accent at 16 % over the band, rows 7..25.
inline std::vector<std::uint32_t> BetaBackdrops(std::uint32_t fill, std::uint32_t gradientEnd) {
    std::vector<std::uint32_t> out; for (double y = Paper::betaTop; y <= Paper::betaBottom + 1e-9; y += .25) out.push_back(Over(fill, Paper::betaChip, BandAt(gradientEnd, y))); return out;
}
// The lightest of them (the box's bottom row) — kept for reports.
inline std::uint32_t LabelBackdrop(std::uint32_t gradientEnd) { return BandAt(gradientEnd, Paper::labelBottom); }
inline std::uint32_t BetaBackdrop(std::uint32_t fill, std::uint32_t gradientEnd) { return Over(fill, Paper::betaChip, BandAt(gradientEnd, Paper::betaBottom)); }
inline Band BandFor(std::uint32_t fill) {
    fill &= 0xFFFFFF; Band b; b.fill = fill;
    // Non-text tones: the reference fill gives the Paper values back exactly (Relative's own shortcut). Text tones are
    // always floored: Paper's « BETA » #6A8FFF itself measures 4.34:1 on the chip's bottom row.
    b.gradientEnd = Relative(fill, Paper::gradientEnd);
    b.border = Rgba(fill, Paper::borderAlpha);
    b.label = ReadableOnAll(Relative(fill, Paper::label), LabelBackdrops(b.gradientEnd), Paper::textMinimum);
    b.beta = ReadableOnAll(Relative(fill, Paper::beta), BetaBackdrops(fill, b.gradientEnd), Paper::textMinimum);
    b.markLight = Relative(fill, Paper::markLight); b.markMid = Relative(fill, Paper::markMid); b.markDeep = Relative(fill, Paper::markDeep);
    b.waveDim = Rgba(fill, Paper::waveDimAlpha); b.waveBright = Rgba(Relative(fill, Paper::waveBright), Paper::waveBrightAlpha);
    return b;
}
// Painted per frame (the wave runs at 30 frames/s): the last fill's tones are kept.
inline const Band& BandCached(std::uint32_t fill) {
    static thread_local Band last; static thread_local bool ready = false;
    if (!ready || last.fill != (fill & 0xFFFFFF)) { last = BandFor(fill); ready = true; }
    return last;
}
}}} // namespace NativeChrome::Annotations::AccentTone
