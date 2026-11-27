#pragma once
// « À propos de Browser » B (Paper « Browser — référence unifiée », 257B-0 « À propos — B (validée) »): the four tile
// icons and the header logo.
//
// Tile icons: Jiggicons GLASS, 26 DIP, viewBox 24, path data verbatim from the icon sources (the Paper board's SVGs are
// the same paths, recoloured). A glass icon is drawn in four passes, exactly like its SVG:
//   1. `under` layers; a `masked` one is cut out of the glass shape (the SVG « out » mask);
//   2. the masked layers again, blurred (feGaussianBlur stdDeviation 1.6 in icon units) and clipped to the glass shape
//      (the SVG « in » mask): what is seen through the frosted glass;
//   3. the glass shape, then its rim highlight (white to transparent, the original highlight kept);
//   4. `over` layers.
// Colours follow the window's profile (Design::Accent::Ink(), the OKLCH L .78 ink): Paper gives them for Travail
// (ink #6FD087): body #3E8A52 -> #173D23, glass #B9EEC6 -> #6FD087 at 70 %, light parts #D8F6DF -> #9FE3B1 at 96 %.
// Each stop is that ink moved in OKLCH by the ratios measured on those values (lightness x, chroma x, hue +):
// Travail gives back the board's hex values exactly; every other profile keeps the same relation to its own ink, and the
// neutral grey profile (chroma 0) gets greys. tests/about_window_test.cpp checks both.
//
// Paid icons: terminal (glass) and compass (glass) are paid Jiggicons. Each is ONE line below, declared in
// tools/public-mirror/paid-icons (the public copy replaces it with the free laptop / globe, same identifier and shape).
#include <windows.h>
#include <d2d1.h>
#include <d2d1_1.h>
#include <d2d1effects.h>
#include <wincodec.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include "svg-path.h"
#include "profile-color.h"

namespace NativeChrome {
namespace AboutIcons {

enum class Fill : unsigned char { Body, Light, Glass, Rim };
// One filled path in icon units; its vertical gradient runs from y1 (top stop) to y2 (bottom stop), userSpaceOnUse.
struct Layer { const char* d = nullptr; Fill fill = Fill::Body; float y1 = 0, y2 = 0; bool evenOdd = false, masked = false; };
struct Icon { Layer under[10]; Layer glass, rim; Layer over[3]; };

// « Nouveautés »: gift (glass, free).
inline constexpr Icon gift{{{"M18.75 10.5L5.25 10.5A1.24 1.24 0 0 0 4 11.75L4 18.5A3.49 3.49 0 0 0 7.5 22L16.5 22A3.49 3.49 0 0 0 20 18.5L20 11.75A1.24 1.24 0 0 0 18.75 10.5Z", Fill::Body, 10.5f, 22, true, true}, {"M12.75 12.5L12.75 21.25A0.75 0.75 0 0 1 11.25 21.25L11.25 12.5A0.75 0.75 0 0 1 12.75 12.5Z", Fill::Light, 11.75f, 22, false, true}, {"M10.38 7.47Q11.1 7.5 12 7.5Q12.9 7.5 13.62 7.47Q14.44 7.43 15.08 7.34Q15.81 7.25 16.36 7.09Q16.99 6.91 17.42 6.62A0.75 0.75 0 0 0 17.48 6.57Q17.81 6.29 18.03 5.94Q18.24 5.6 18.34 5.21Q18.43 4.84 18.42 4.45Q18.4 4.07 18.27 3.71Q18.14 3.35 17.92 3.05A2.51 2.51 0 0 0 16.65 2.13Q16.25 2 15.8 2Q15.1 2 14.45 2.29Q13.8 2.58 13.22 3.15Q12.55 3.82 12 4.85Q11.45 3.82 10.78 3.15Q10.2 2.58 9.55 2.29Q8.9 2 8.2 2Q7.75 2 7.35 2.13A2.51 2.51 0 0 0 6.08 3.05Q5.86 3.35 5.73 3.71Q5.6 4.07 5.58 4.45Q5.57 4.84 5.66 5.21Q5.76 5.6 5.97 5.94Q6.19 6.29 6.52 6.57A0.75 0.75 0 0 0 6.58 6.62Q7.01 6.91 7.64 7.09Q8.19 7.25 8.92 7.34Q9.56 7.43 10.38 7.47Z", Fill::Body, 2, 7.5f, true, true}}, {"M19.65 6L4.35 6A2.34 2.34 0 0 0 2 8.35L2 9.65A2.34 2.34 0 0 0 4.35 12L19.65 12A2.34 2.34 0 0 0 22 9.65L22 8.35A2.34 2.34 0 0 0 19.65 6Z", Fill::Glass, 6, 12, true}, {"M4.35 6L19.65 6A2.34 2.34 0 0 1 22 8.35L22 9.65A2.34 2.34 0 0 1 19.65 12L4.35 12A2.34 2.34 0 0 1 2 9.65L2 8.35A2.34 2.34 0 0 1 4.35 6ZM4.35 6.75L19.65 6.75A1.6 1.6 0 0 1 21.25 8.35L21.25 9.65A1.6 1.6 0 0 1 19.65 11.25L4.35 11.25A1.6 1.6 0 0 1 2.75 9.65L2.75 8.35A1.6 1.6 0 0 1 4.35 6.75Z", Fill::Rim, 6, 9.72f, true}, {}};
// « Code source »: terminal (glass, PAID, one line: tools/public-mirror/paid-icons/terminal.glass.paid.svg).
inline constexpr Icon terminal{{{"M17.5 3L6.5 3A3 3 0 0 0 3.5 6L3.5 14.25A1.74 1.74 0 0 0 5.25 16L18.75 16A1.74 1.74 0 0 0 20.5 14.25L20.5 6A3 3 0 0 0 17.5 3Z", Fill::Body, 3, 16, true, true}}, {"M17.25 4.5L6.75 4.5A1.74 1.74 0 0 0 5 6.25L5 12.75A1.74 1.74 0 0 0 6.75 14.5L17.25 14.5A1.74 1.74 0 0 0 19 12.75L19 6.25A1.74 1.74 0 0 0 17.25 4.5Z", Fill::Glass, 4.5f, 14.5f, true}, {"M6.75 4.5L17.25 4.5A1.74 1.74 0 0 1 19 6.25L19 12.75A1.74 1.74 0 0 1 17.25 14.5L6.75 14.5A1.74 1.74 0 0 1 5 12.75L5 6.25A1.74 1.74 0 0 1 6.75 4.5ZM6.75 5.25L17.25 5.25A0.99 0.99 0 0 1 18.25 6.25L18.25 12.75A0.99 0.99 0 0 1 17.25 13.75L6.75 13.75A0.99 0.99 0 0 1 5.75 12.75L5.75 6.25A0.99 0.99 0 0 1 6.75 5.25Z", Fill::Rim, 4.5f, 10.7f, true}, {{"M20.5 17.25L3.5 17.25A1.5 1.5 0 0 0 2.95 20.14Q3.21 20.25 3.5 20.25L20.5 20.25A1.5 1.5 0 0 0 21.05 17.36Q20.79 17.25 20.5 17.25Z", Fill::Body, 17.25f, 20.25f, true}}};
// « Site web »: compass (glass, PAID, one line: tools/public-mirror/paid-icons/compass.glass.paid.svg).
inline constexpr Icon compass{{{"M2.17 10.18A9.99 9.99 0 0 0 13.82 21.83A9.99 9.99 0 0 0 21.3 8.32A9.98 9.98 0 0 0 8.32 2.7A9.98 9.98 0 0 0 2.17 10.18Z", Fill::Body, 2, 22, true, true}}, {"M6.83 10.25A19.51 19.51 0 0 0 6.83 13.75Q6.91 14.67 7.08 15.55Q7.26 16.46 7.52 17.29Q7.79 18.14 8.14 18.87Q8.5 19.61 8.94 20.19Q9.37 20.78 9.87 21.18Q10.37 21.59 10.91 21.8A3.03 3.03 0 0 0 13.09 21.8Q13.63 21.59 14.13 21.18Q14.63 20.78 15.06 20.19Q15.5 19.61 15.86 18.87Q16.21 18.14 16.48 17.29Q16.74 16.46 16.92 15.55Q17.09 14.67 17.17 13.75A19.51 19.51 0 0 0 17.17 10.25Q17.09 9.33 16.92 8.45Q16.74 7.54 16.48 6.71Q16.21 5.86 15.86 5.13Q15.5 4.39 15.06 3.81Q14.63 3.22 14.13 2.82Q13.63 2.41 13.09 2.2A3.03 3.03 0 0 0 10.91 2.2Q10.37 2.41 9.87 2.82Q9.37 3.22 8.94 3.81Q8.5 4.39 8.14 5.13Q7.79 5.86 7.52 6.71Q7.26 7.54 7.08 8.45Q6.91 9.33 6.83 10.25Z", Fill::Glass, 2, 22, true}, {"M6.75 12Q6.75 11.12 6.83 10.25Q6.91 9.33 7.08 8.45Q7.26 7.54 7.52 6.71Q7.79 5.86 8.14 5.13Q8.5 4.39 8.94 3.81Q9.37 3.22 9.87 2.82Q10.37 2.41 10.91 2.2A3.03 3.03 0 0 1 13.09 2.2Q13.63 2.41 14.13 2.82Q14.63 3.22 15.06 3.81Q15.5 4.39 15.86 5.13Q16.21 5.86 16.48 6.71Q16.74 7.54 16.92 8.45Q17.09 9.33 17.17 10.25A19.51 19.51 0 0 1 17.17 13.75Q17.09 14.67 16.92 15.55Q16.74 16.46 16.48 17.29Q16.21 18.14 15.86 18.87Q15.5 19.61 15.06 20.19Q14.63 20.78 14.13 21.18Q13.63 21.59 13.09 21.8A3.03 3.03 0 0 1 10.91 21.8Q10.37 21.59 9.87 21.18Q9.37 20.78 8.94 20.19Q8.5 19.61 8.14 18.87Q7.79 18.14 7.52 17.29Q7.26 16.46 7.08 15.55Q6.91 14.67 6.83 13.75Q6.75 12.88 6.75 12ZM7.5 12Q7.5 11.15 7.58 10.32Q7.65 9.43 7.82 8.6Q7.98 7.72 8.23 6.94Q8.49 6.14 8.82 5.46Q9.15 4.78 9.54 4.26Q9.92 3.74 10.34 3.4Q10.75 3.07 11.18 2.9A2.28 2.28 0 0 1 12.82 2.9Q13.25 3.07 13.66 3.4Q14.08 3.74 14.46 4.26Q14.85 4.78 15.18 5.46Q15.51 6.14 15.77 6.94Q16.02 7.72 16.18 8.6Q16.35 9.43 16.42 10.32A18.76 18.76 0 0 1 16.42 13.68Q16.35 14.57 16.18 15.4Q16.02 16.28 15.77 17.06Q15.51 17.86 15.18 18.54Q14.85 19.22 14.46 19.74Q14.08 20.26 13.66 20.6Q13.25 20.93 12.82 21.1A2.28 2.28 0 0 1 11.18 21.1Q10.75 20.93 10.34 20.6Q9.92 20.26 9.54 19.74Q9.15 19.22 8.82 18.54Q8.49 17.86 8.23 17.06Q7.98 16.28 7.82 15.4Q7.65 14.57 7.58 13.68Q7.5 12.85 7.5 12Z", Fill::Rim, 2, 14.4f, true}, {{"M21.74 11.25L2.26 11.25A0.25 0.25 0 0 0 2.01 11.49Q1.99 12 2.01 12.51A0.25 0.25 0 0 0 2.26 12.75L21.74 12.75A0.25 0.25 0 0 0 21.99 12.51Q22.01 12 21.99 11.49A0.25 0.25 0 0 0 21.74 11.25Z", Fill::Light, 11.25f, 12.75f, true}}};
// « Signaler un problème »: bug (glass, free).
inline constexpr Icon bug{{{"M9.26 6.86Q9.17 6.27 9.01 5.81Q8.85 5.36 8.61 5.02Q8.37 4.68 8.03 4.42Q7.68 4.14 7.2 3.94A0.75 0.75 0 0 1 7.8 2.56Q8.45 2.84 8.95 3.23Q9.46 3.63 9.83 4.15Q10.19 4.67 10.42 5.3Q10.64 5.91 10.74 6.64A0.75 0.75 0 0 1 9.26 6.86Z", Fill::Body, 2.5f, 7.5f}, {"M13.26 6.64Q13.36 5.91 13.58 5.3Q13.81 4.67 14.17 4.15Q14.54 3.63 15.05 3.23Q15.55 2.84 16.2 2.56A0.75 0.75 0 0 1 16.8 3.94Q16.32 4.14 15.97 4.42Q15.63 4.68 15.39 5.02Q15.15 5.36 14.99 5.81Q14.83 6.27 14.74 6.86A0.75 0.75 0 0 1 13.26 6.64Z", Fill::Body, 2.5f, 7.5f}, {"M7.01 10.64L3.36 8.39A0.75 0.75 0 0 1 3.11 7.36A0.75 0.75 0 0 1 4.14 7.11L7.79 9.36A0.75 0.75 0 0 1 7.01 10.64Z", Fill::Body, 7, 10.75f, false, true}, {"M7.25 14.75L3.25 14.75A0.75 0.75 0 0 1 3.25 13.25L7.25 13.25A0.75 0.75 0 0 1 7.25 14.75Z", Fill::Body, 13.25f, 14.75f, false, true}, {"M7.98 18.4L4.13 20.65A0.75 0.75 0 0 1 3.37 19.35L7.22 17.1A0.75 0.75 0 0 1 7.98 18.4Z", Fill::Body, 17, 20.75f, false, true}, {"M16.21 9.36L19.86 7.11A0.75 0.75 0 0 1 20.89 7.36A0.75 0.75 0 0 1 20.64 8.39L16.99 10.64A0.75 0.75 0 0 1 15.96 10.39A0.75 0.75 0 0 1 16.21 9.36Z", Fill::Body, 7, 10.75f, false, true}, {"M16.75 13.25L20.75 13.25A0.75 0.75 0 0 1 20.75 14.75L16.75 14.75A0.75 0.75 0 0 1 16.75 13.25Z", Fill::Body, 13.25f, 14.75f, false, true}, {"M16.78 17.1L20.63 19.35A0.75 0.75 0 0 1 19.87 20.65L16.02 18.4A0.75 0.75 0 0 1 16.78 17.1Z", Fill::Body, 17, 20.75f, false, true}, {"M12.75 5.5L11.25 5.5A4.74 4.74 0 0 0 6.5 10.25L6.5 14.25A0.75 0.75 0 0 0 7.25 15L16.75 15A0.75 0.75 0 0 0 17.5 14.25L17.5 10.25A4.74 4.74 0 0 0 12.75 5.5Z", Fill::Body, 5.5f, 15, true, true}}, {"M15.25 9.75L8.75 9.75A2.24 2.24 0 0 0 6.5 12L6.5 16A5.5 5.5 0 0 0 17.41 17Q17.5 16.5 17.5 16L17.5 12A2.24 2.24 0 0 0 15.25 9.75Z", Fill::Glass, 9.75f, 21.5f, true}, {"M8.75 9.75L15.25 9.75A2.24 2.24 0 0 1 17.5 12L17.5 16A5.5 5.5 0 0 1 6.59 17Q6.5 16.5 6.5 16L6.5 12A2.24 2.24 0 0 1 8.75 9.75ZM8.75 10.5L15.25 10.5A1.5 1.5 0 0 1 16.75 12L16.75 16A4.75 4.75 0 0 1 7.33 16.86Q7.25 16.44 7.25 16L7.25 12A1.5 1.5 0 0 1 8.75 10.5Z", Fill::Rim, 9.75f, 17.04f, true}, {{"M12.75 13L12.75 19.25A0.75 0.75 0 0 1 11.25 19.25L11.25 13A0.75 0.75 0 0 1 12.75 13Z", Fill::Body, 12.25f, 20}}};

inline constexpr float blur = 1.6f;   // feGaussianBlur stdDeviation, icon units
inline constexpr float glassAlpha = .7f, lightAlpha = .96f;

// ---- colours ----------------------------------------------------------------------------------------------------
struct Stop { double l, c, h; };   // lightness x, chroma x, hue + (degrees), against the ink
// Measured on the board (Travail, ink #6FD087): see the file header.
inline constexpr Stop bodyTop{.7317, .8225, -.17}, bodyBottom{.4161, .4559, 1.48}, glassTop{1.1569, .5528, 2.01}, lightTop{1.2125, .3151, 2.31}, lightBottom{1.0996, .7011, 2.08};
struct Palette { D2D1_COLOR_F body[2], light[2], glass[2]; };
struct Oklch { double l = 0, c = 0, h = 0; };
inline Oklch ToOklch(float r8, float g8, float b8) {   // 0..1 sRGB
    using ProfileColor::Detail::ToLinear;
    const double r = ToLinear(r8), g = ToLinear(g8), b = ToLinear(b8);
    const double l = std::cbrt(.4122214708 * r + .5363325363 * g + .0514459929 * b);
    const double m = std::cbrt(.2119034982 * r + .6806995451 * g + .1073969566 * b);
    const double s = std::cbrt(.0883024619 * r + .2817188376 * g + .6299787005 * b);
    const double L = .2104542553 * l + .7936177850 * m - .0040720468 * s, A = 1.9779984951 * l - 2.4285922050 * m + .4505937099 * s, B = .0259040371 * l + .7827717662 * m - .8086757660 * s;
    double h = std::atan2(B, A) * 180 / ProfileColor::Detail::Pi; if (h < 0) h += 360;
    return {L, std::sqrt(A * A + B * B), h};
}
inline ProfileColor::Rgb8 Moved(const Oklch& ink, Stop s) {
    const double L = std::clamp(ink.l * s.l, 0.0, 1.0), h = std::fmod(ink.h + s.h + 360, 360.0);
    return ProfileColor::Detail::Pack(ProfileColor::Detail::FromOklch(L, ProfileColor::Detail::FitChroma(L, ink.c * s.c, h), h));
}
inline D2D1_COLOR_F Color(ProfileColor::Rgb8 c, float a) { return D2D1::ColorF(c.r / 255.f, c.g / 255.f, c.b / 255.f, a); }
inline Palette PaletteFor(float r, float g, float b) {
    const auto k = ToOklch(r, g, b);
    const ProfileColor::Rgb8 ink{static_cast<std::uint8_t>(std::lround(r * 255)), static_cast<std::uint8_t>(std::lround(g * 255)), static_cast<std::uint8_t>(std::lround(b * 255))};
    Palette p;
    p.body[0] = Color(Moved(k, bodyTop), 1); p.body[1] = Color(Moved(k, bodyBottom), 1);
    p.light[0] = Color(Moved(k, lightTop), lightAlpha); p.light[1] = Color(Moved(k, lightBottom), lightAlpha);
    p.glass[0] = Color(Moved(k, glassTop), glassAlpha); p.glass[1] = Color(ink, glassAlpha);
    return p;
}

// ---- painter ----------------------------------------------------------------------------------------------------
// Device resources live with one render target (geometries need its factory); a new target drops them. The frosted
// layer of each icon (blurred, before its glass clip) is kept as a bitmap per colour and DPI: repaints (hover, the
// spinner) never blur again.
class Painter {
    template <class T> static void Drop(T*& p) { if (p) p->Release(); p = nullptr; }
    ID2D1RenderTarget* target_ = nullptr; ID2D1Factory* factory_ = nullptr;
    std::map<std::pair<const char*, bool>, ID2D1PathGeometry*> paths_;
    std::map<const Icon*, ID2D1PathGeometry*> outside_;
    struct Frost { const Icon* icon = nullptr; float size = 0, dpi = 0; unsigned key = 0; ID2D1Bitmap* bitmap = nullptr; };
    std::vector<Frost> frost_;
    bool frosted_ = false;   // the last icon drawn used a real Gaussian blur (else the unblurred fallback)
    static constexpr float margin = 6;   // icon units around the icon in the frost bitmap (> 3 sigma)
    static unsigned Key(const D2D1_COLOR_F& c) { return (static_cast<unsigned>(std::lround(c.r * 255)) << 16) | (static_cast<unsigned>(std::lround(c.g * 255)) << 8) | static_cast<unsigned>(std::lround(c.b * 255)); }
    void Bind(ID2D1RenderTarget* t) {
        if (t == target_) return;
        Release(); target_ = t; target_->AddRef(); t->GetFactory(&factory_);
    }
    ID2D1PathGeometry* Path(const char* d, bool evenOdd) {
        if (!d || !factory_) return nullptr;
        auto& g = paths_[{d, evenOdd}]; if (!g) g = DialogKit::SvgGeometry(factory_, d, evenOdd);
        return g;
    }
    ID2D1PathGeometry* Outside(const Icon& icon) {   // everything but the glass shape (the « out » mask)
        auto& g = outside_[&icon]; if (g || !factory_) return g;
        auto* glass = Path(icon.glass.d, icon.glass.evenOdd); if (!glass) return nullptr;
        ID2D1RectangleGeometry* all = nullptr; ID2D1GeometrySink* sink = nullptr;
        if (SUCCEEDED(factory_->CreateRectangleGeometry({-12, -12, 36, 36}, &all)) && SUCCEEDED(factory_->CreatePathGeometry(&g)) && SUCCEEDED(g->Open(&sink))) {
            if (FAILED(all->CombineWithGeometry(glass, D2D1_COMBINE_MODE_EXCLUDE, nullptr, sink))) { sink->Close(); Drop(sink); Drop(g); }
            else { sink->Close(); Drop(sink); }
        } else Drop(g);
        Drop(all);
        return g;
    }
    static void Colors(const Layer& l, const Palette& p, D2D1_COLOR_F& top, D2D1_COLOR_F& bottom) {
        switch (l.fill) {
        case Fill::Body: top = p.body[0]; bottom = p.body[1]; break;
        case Fill::Light: top = p.light[0]; bottom = p.light[1]; break;
        case Fill::Glass: top = p.glass[0]; bottom = p.glass[1]; break;
        case Fill::Rim: top = D2D1::ColorF(1, 1, 1, 1); bottom = D2D1::ColorF(1, 1, 1, 0); break;
        }
    }
    void FillLayer(ID2D1RenderTarget* t, const Layer& l, const Palette& p) {
        auto* g = Path(l.d, l.evenOdd); if (!g) return;
        D2D1_COLOR_F top{}, bottom{}; Colors(l, p, top, bottom);
        const D2D1_GRADIENT_STOP s[2] = {{0.f, top}, {1.f, bottom}};
        ID2D1GradientStopCollection* stops = nullptr; ID2D1LinearGradientBrush* brush = nullptr;
        if (SUCCEEDED(t->CreateGradientStopCollection(s, 2, &stops)) && SUCCEEDED(t->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({0, l.y1}, {0, l.y2}), stops, &brush))) t->FillGeometry(g, brush);
        Drop(brush); Drop(stops);
    }
    template <class F> void Masked(ID2D1RenderTarget* t, ID2D1Geometry* mask, F&& draw) {
        ID2D1Layer* layer = nullptr;
        if (!mask || FAILED(t->CreateLayer(nullptr, &layer))) { draw(); return; }
        t->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), mask, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE), layer);
        draw(); t->PopLayer(); Drop(layer);
    }
    // The masked layers blurred, icon box + margin, at `size` DIP for the icon (nullptr: no Direct2D 1.1 effects here).
    ID2D1Bitmap* Frosted(ID2D1RenderTarget* t, const Icon& icon, const Palette& p, float size) {
        float dx = 0, dy = 0; t->GetDpi(&dx, &dy); const unsigned key = Key(p.glass[1]);
        for (auto& f : frost_) if (f.icon == &icon && f.size == size && f.dpi == dx && f.key == key) return f.bitmap;
        const float k = size / 24, extent = (24 + 2 * margin) * k;
        ID2D1BitmapRenderTarget* sharp = nullptr; ID2D1BitmapRenderTarget* soft = nullptr; ID2D1Bitmap* source = nullptr; ID2D1Bitmap* out = nullptr;
        ID2D1DeviceContext* dc = nullptr; ID2D1Effect* fx = nullptr;
        static const GUID gaussian{0x1feb6d69, 0x2fe6, 0x4ac9, {0x8c, 0x58, 0x1d, 0x7f, 0x93, 0xe7, 0xa6, 0xa5}};   // CLSID_D2D1GaussianBlur (no dxguid)
        if (SUCCEEDED(t->CreateCompatibleRenderTarget(D2D1::SizeF(extent, extent), &sharp))) {
            sharp->BeginDraw(); sharp->Clear(D2D1::ColorF(0, 0, 0, 0));
            sharp->SetTransform(D2D1::Matrix3x2F::Translation(margin, margin) * D2D1::Matrix3x2F::Scale(k, k));
            for (const auto& l : icon.under) { if (!l.d) break; if (l.masked) FillLayer(sharp, l, p); }
            if (SUCCEEDED(sharp->EndDraw()) && SUCCEEDED(sharp->GetBitmap(&source)) && SUCCEEDED(t->CreateCompatibleRenderTarget(D2D1::SizeF(extent, extent), &soft))
                && SUCCEEDED(soft->QueryInterface(__uuidof(ID2D1DeviceContext), reinterpret_cast<void**>(&dc))) && SUCCEEDED(dc->CreateEffect(gaussian, &fx))) {
                const float deviation = blur * k;   // DIP
                fx->SetInput(0, source);
                fx->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, reinterpret_cast<const BYTE*>(&deviation), sizeof(deviation));
                soft->BeginDraw(); soft->Clear(D2D1::ColorF(0, 0, 0, 0)); dc->DrawImage(fx); fx->SetInput(0, nullptr);
                if (SUCCEEDED(soft->EndDraw())) soft->GetBitmap(&out);
            }
        }
        Drop(fx); Drop(dc); Drop(source); Drop(soft); Drop(sharp);
        // One colour at a time (a profile's colour fade never piles bitmaps up): older colours and DPIs go.
        for (auto it = frost_.begin(); it != frost_.end();) { if (it->key != key || it->dpi != dx) { Drop(it->bitmap); it = frost_.erase(it); } else ++it; }
        if (out) frost_.push_back({&icon, size, dx, key, out});
        return out;
    }
public:
    ~Painter() { Release(); }
    Painter() = default;
    Painter(const Painter&) = delete; Painter& operator=(const Painter&) = delete;
    void Release() {
        for (auto& f : frost_) Drop(f.bitmap);
        frost_.clear();
        for (auto& g : paths_) Drop(g.second);
        paths_.clear();
        for (auto& g : outside_) Drop(g.second);
        outside_.clear();
        Drop(factory_); Drop(target_);
    }
    bool FrostedForTest() const { return frosted_; }
    // Draws `icon` with its top-left at (left, top), `size` DIP square, in the colours of `ink` (0..1 sRGB).
    void Draw(ID2D1RenderTarget* t, const Icon& icon, float left, float top, float size, float r, float g, float b) {
        if (!t) return;
        Bind(t);
        const Palette p = PaletteFor(r, g, b);
        D2D1_MATRIX_3X2_F old; t->GetTransform(&old);
        const float k = size / 24;
        const D2D1_MATRIX_3X2_F space = D2D1::Matrix3x2F::Scale(k, k) * D2D1::Matrix3x2F::Translation(left, top) * old;
        t->SetTransform(space);
        bool masked = false; for (const auto& l : icon.under) { if (!l.d) break; masked = masked || l.masked; }
        ID2D1Geometry* glass = Path(icon.glass.d, icon.glass.evenOdd);
        ID2D1Geometry* outside = masked ? Outside(icon) : nullptr;
        for (const auto& l : icon.under) {   // 1. under layers, the masked ones cut out of the glass
            if (!l.d) break;
            if (l.masked && outside) Masked(t, outside, [&] { FillLayer(t, l, p); });
            else FillLayer(t, l, p);
        }
        frosted_ = false;
        if (masked && glass) {   // 2. seen through the frosted glass
            ID2D1Bitmap* frost = Frosted(t, icon, p, size);
            frosted_ = frost != nullptr;
            Masked(t, glass, [&] {
                if (frost) { t->SetTransform(old); const float m = margin * k, e = (24 + 2 * margin) * k; t->DrawBitmap(frost, {left - m, top - m, left - m + e, top - m + e}, 1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR); t->SetTransform(space); }
                else for (const auto& l : icon.under) { if (!l.d) break; if (l.masked) FillLayer(t, l, p); }
            });
        }
        FillLayer(t, icon.glass, p); FillLayer(t, icon.rim, p);   // 3. glass and its highlight
        for (const auto& l : icon.over) { if (!l.d) break; FillLayer(t, l, p); }   // 4. over the glass
        t->SetTransform(old);
    }
};

// ---- header logo --------------------------------------------------------------------------------------------------
// The application's own icon (resource 101: the live icon in a release build, the preview icon otherwise — Paper shows
// the live one), drawn at the device pixel size of its 52 DIP slot, clipped to radius 14 over its #262626 plate.
// `file`: an .ico to read instead (test hosts have no icon resource).
class Logo {
    ID2D1RenderTarget* target_ = nullptr; ID2D1Bitmap* bitmap_ = nullptr; UINT side_ = 0; std::wstring file_; bool drawn_ = false;
    template <class T> static void Drop(T*& p) { if (p) p->Release(); p = nullptr; }
    ID2D1Bitmap* Make(ID2D1RenderTarget* t, UINT side) {
        HICON icon = file_.empty() ? static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101), IMAGE_ICON, static_cast<int>(side), static_cast<int>(side), LR_DEFAULTCOLOR))
                                   : static_cast<HICON>(LoadImageW(nullptr, file_.c_str(), IMAGE_ICON, static_cast<int>(side), static_cast<int>(side), LR_LOADFROMFILE));
        if (!icon) return nullptr;
        IWICImagingFactory* wic = nullptr; IWICBitmap* source = nullptr; IWICFormatConverter* pbgra = nullptr; ID2D1Bitmap* out = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) && SUCCEEDED(wic->CreateBitmapFromHICON(icon, &source))
            && SUCCEEDED(wic->CreateFormatConverter(&pbgra)) && SUCCEEDED(pbgra->Initialize(source, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)))
            t->CreateBitmapFromWicBitmap(pbgra, nullptr, &out);
        Drop(pbgra); Drop(source); Drop(wic); DestroyIcon(icon);
        return out;
    }
public:
    ~Logo() { Release(); }
    Logo() = default;
    Logo(const Logo&) = delete; Logo& operator=(const Logo&) = delete;
    void Release() { Drop(bitmap_); Drop(target_); side_ = 0; }
    void SetFile(const std::wstring& file) { if (file != file_) { Release(); file_ = file; } }
    bool Drawn() const { return drawn_; }   // the last Draw found an icon (tests; kept after Release)
    // Draws into `box` (DIP); false when no icon could be read (the plate alone stays).
    bool Draw(ID2D1RenderTarget* t, ID2D1Factory* factory, D2D1_RECT_F box, float radius) {
        if (!t || !factory) return false;
        float dx = 0, dy = 0; t->GetDpi(&dx, &dy);
        const UINT side = static_cast<UINT>(std::max(1L, std::lround((box.right - box.left) * dx / 96.f)));
        if (t != target_ || side != side_) { Release(); target_ = t; t->AddRef(); side_ = side; bitmap_ = Make(t, side); }
        drawn_ = bitmap_ != nullptr;
        if (!bitmap_) return false;
        ID2D1RoundedRectangleGeometry* clip = nullptr; ID2D1Layer* layer = nullptr;
        if (SUCCEEDED(factory->CreateRoundedRectangleGeometry(D2D1::RoundedRect(box, radius, radius), &clip)) && SUCCEEDED(t->CreateLayer(nullptr, &layer))) {
            t->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), clip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE), layer);
            t->DrawBitmap(bitmap_, box, 1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            t->PopLayer();
        }
        Drop(layer); Drop(clip);
        return true;
    }
};

} // namespace AboutIcons
} // namespace NativeChrome
