#pragma once
#include "i18n.h"
// Onboarding window — drawing kit (Paper p-R-0, boards 1 … 7): the colours the boards derive from the profile,
// Inter Display text with CSS line boxes in three weights, the boards' glyphs (path data copied from get_jsx),
// « in oklab » gradients and the blurred ticks. Values and their Paper nodes: design/onboarding-values.md.
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>
#include "chrome.h"
#include "tooltip.h"          // PopupKit (path reader, CSS baseline)
#include "profile-color.h"
#include "profiles.h"

namespace NativeChrome {
namespace OnboardingKit {

using Rgb8 = ProfileColor::Rgb8;
inline constexpr Rgb8 FromHex(unsigned v) { return {static_cast<std::uint8_t>((v >> 16) & 255), static_cast<std::uint8_t>((v >> 8) & 255), static_cast<std::uint8_t>(v & 255)}; }
inline D2D1_COLOR_F Col(Rgb8 c, float a = 1) { return D2D1::ColorF(c.r / 255.f, c.g / 255.f, c.b / 255.f, a); }
inline D2D1_COLOR_F Col(unsigned rgb, float a = 1) { return Col(FromHex(rgb), a); }
inline Rgb8 Lerp(Rgb8 a, Rgb8 b, float t) {
    auto m = [t](std::uint8_t x, std::uint8_t y) { return static_cast<std::uint8_t>(std::lround(x + (y - x) * t)); };
    return {m(a.r, b.r), m(a.g, b.g), m(a.b, b.b)};
}

// ---- Paper tokens used by the boards ----------------------------------------------------------------
namespace Token {
inline constexpr unsigned canvas = 0x171717, raised = 0x292929, hover = 0x333333, pressed = 0x3E3E3E;   // --color-browser-*
inline constexpr unsigned text = 0xFFFFFF, secondary = 0xB3B3B3, danger = 0xFF7B6E, accent = 0x3E67FF;
inline constexpr float disabled = .32f, focus = .10f;                                                    // --opacity-browser-*
}

// ---- OKLab (CSS « in oklab » interpolation, hue moves) -----------------------------------------------
struct Lab { double L = 0, a = 0, b = 0; };
inline Lab ToLab(Rgb8 c) {
    namespace D = ProfileColor::Detail;
    const double r = D::ToLinear(c.r / 255.0), g = D::ToLinear(c.g / 255.0), b = D::ToLinear(c.b / 255.0);
    const double l = std::cbrt(.4122214708 * r + .5363325363 * g + .0514459929 * b), m = std::cbrt(.2119034982 * r + .6806995451 * g + .1073969566 * b), s = std::cbrt(.0883024619 * r + .2817188376 * g + .6299787005 * b);
    return {.2104542553 * l + .7936177850 * m - .0040720468 * s, 1.9779984951 * l - 2.4285922050 * m + .4505937099 * s, .0259040371 * l + .7827717662 * m - .8086757660 * s};
}
inline Rgb8 FromLab(Lab v) {
    const double l_ = v.L + .3963377774 * v.a + .2158037573 * v.b, m_ = v.L - .1055613458 * v.a - .0638541728 * v.b, s_ = v.L - .0894841775 * v.a - 1.2914855480 * v.b;
    const double l = l_ * l_ * l_, m = m_ * m_ * m_, s = s_ * s_ * s_;
    return ProfileColor::Detail::Pack({4.0767416621 * l - 3.3077115913 * m + .2309699292 * s, -1.2684380046 * l + 2.6097574011 * m - .3413193965 * s, -.0041960863 * l - .7034186147 * m + 1.7076147010 * s});
}
inline Rgb8 MixOklab(Rgb8 a, Rgb8 b, double t) { const Lab x = ToLab(a), y = ToLab(b); return FromLab({x.L + (y.L - x.L) * t, x.a + (y.a - x.a) * t, x.b + (y.b - x.b) * t}); }

// ---- Colours the boards derive from the profile --------------------------------------------------------
// The boards are drawn for the sample profile: hue 200°, intensity 88 %.
inline constexpr int PaperHue = 200;
inline constexpr float PaperChroma = .88f * ProfileColor::MaxChroma;
inline bool IsNeutral(const Profiles::Profile& p) { return p.neutral || p.intensity <= 0; }
inline bool IsPaperColour(const Profiles::Profile& p) { return !IsNeutral(p) && p.hue == PaperHue && std::fabs(p.intensity - PaperChroma) < .0006f; }
// Identity colour (selected icon, ticks, check): the profile ink, #00D1DA on the boards.
inline Rgb8 Ink(const Profiles::Profile& p) { return IsNeutral(p) ? ProfileColor::Neutral() : ProfileColor::Ink(p.hue, p.intensity); }
// Filled controls of the boards (« Continuer », checkbox, annotation frame): #0099A0 = OKLCH L .62 at the
// profile's hue and chroma (the boards' own accent, lighter than the chrome's AccentFill).
inline Rgb8 Accent(const Profiles::Profile& p) {
    if (IsNeutral(p)) return ProfileColor::NeutralFill();
    if (IsPaperColour(p)) return FromHex(0x0099A0);
    namespace D = ProfileColor::Detail;
    const double h = D::NormalizeHue(p.hue), c = D::ClampChroma(p.intensity);
    return D::Pack(D::FromOklch(.62, D::FitChroma(.62, c, h), h));
}
// A colour of the boards (drawn at 200° / 88 %) moved to the profile's hue and intensity, lightness kept.
inline Rgb8 Rehue(unsigned paper, const Profiles::Profile& p) {
    if (IsPaperColour(p)) return FromHex(paper);
    namespace D = ProfileColor::Detail;
    const Lab v = ToLab(FromHex(paper));
    const double c = std::sqrt(v.a * v.a + v.b * v.b);
    double h = std::atan2(v.b, v.a) * 180 / D::Pi + (p.hue - PaperHue);
    h = std::fmod(std::fmod(h, 360.0) + 360.0, 360.0);
    const double k = IsNeutral(p) ? 0 : std::min(1.0, D::ClampChroma(p.intensity) / PaperChroma);
    const double L = std::clamp(v.L, 0.0, 1.0);
    return D::Pack(D::FromOklch(L, D::FitChroma(L, c * k, h), h));
}
// « Intensité » track end: #00E1EA on the boards (OKLCH L .827, the hue's largest chroma).
inline Rgb8 IntensityEnd(const Profiles::Profile& p) {
    if (p.hue == PaperHue) return FromHex(0x00E1EA);
    namespace D = ProfileColor::Detail; const double h = D::NormalizeHue(p.hue);
    return D::Pack(D::FromOklch(.827, D::FitChroma(.827, ProfileColor::MaxChroma, h), h));
}
inline int IntensityPercent(const Profiles::Profile& p) { return IsNeutral(p) ? 0 : static_cast<int>(std::lround(std::clamp(p.intensity, 0.f, ProfileColor::MaxChroma) / ProfileColor::MaxChroma * 100)); }
inline void SetIntensityPercent(Profiles::Profile& p, int percent) { percent = std::clamp(percent, 0, 100); p.intensity = percent / 100.f * ProfileColor::MaxChroma; p.neutral = percent == 0; }

// ---- Text: Inter Display, CSS line boxes, weights 400 / 500 / 600 -----------------------------------
struct Font { float size = 14, line = 20; int weight = 400; float tracking = 0; };   // tracking in DIP (em × size)
// Where the web engine that drew the boards puts the baseline in a line box: Inter's ascent and descent rounded
// to whole pixels, the leading split in two, the result on a whole pixel (half up). 14/20 gives 16, 32/38 gives
// 31, 16/16 gives 14 — one pixel lower than the unrounded CSS formula for most of the boards' sizes
// (measured against the Paper screenshots, tests/onboarding/compare.py).
inline float Baseline(Font f) {
    const float ascent = std::round(.96875f * f.size), descent = std::round(.2412f * f.size);
    return std::floor(ascent + (f.line - ascent - descent) / 2 + .5f);
}
inline IDWriteTextLayout* MakeLayout(Chrome& c, const std::wstring& text, float width, Font f, bool wrap, DWRITE_TEXT_ALIGNMENT align, bool trim) {
    auto* write = c.WriteFactory(); if (!write || text.empty()) return nullptr;
    IDWriteTextFormat* format = nullptr;
    if (FAILED(write->CreateTextFormat(L"Inter Display", c.FontCollection(), static_cast<DWRITE_FONT_WEIGHT>(f.weight), DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, f.size, L"fr-FR", &format))) return nullptr;
    format->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    format->SetTextAlignment(align);
    format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, f.line, Baseline(f));
    IDWriteTextLayout* layout = nullptr;
    write->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format, std::max(1.f, width), 4096, &layout); format->Release();
    if (!layout) return nullptr;
    IDWriteTextLayout1* l1 = nullptr;
    if (f.tracking != 0 && SUCCEEDED(layout->QueryInterface(__uuidof(IDWriteTextLayout1), reinterpret_cast<void**>(&l1)))) { l1->SetCharacterSpacing(0, f.tracking, 0, {0, static_cast<UINT32>(text.size())}); l1->Release(); }   // CSS letter-spacing: after each character
    if (trim && !wrap) { DWRITE_TRIMMING t{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0}; IDWriteInlineObject* ellipsis = nullptr; write->CreateEllipsisTrimmingSign(layout, &ellipsis); layout->SetTrimming(&t, ellipsis); if (ellipsis) ellipsis->Release(); }
    return layout;
}
// Width of one line as the boards lay it out: whole pixels, rounded up (every text node of the boards has an
// integer width — « Passer et naviguer » 115, « Commencer » 78 — and the boxes that hug them follow).
inline float Width(Chrome& c, const std::wstring& text, Font f) {
    auto* layout = MakeLayout(c, text, 8192, f, false, DWRITE_TEXT_ALIGNMENT_LEADING, false); if (!layout) return 0;
    DWRITE_TEXT_METRICS m{}; layout->GetMetrics(&m); layout->Release(); return std::ceil(m.widthIncludingTrailingWhitespace - .01f);
}
// `box.top` is the top of the first line box; one line unless `wrap`.
inline void Text(Chrome& c, const std::wstring& text, D2D1_RECT_F box, Font f, D2D1_COLOR_F color, DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING, bool wrap = false, bool trim = false) {
    auto* t = c.RenderTarget(); auto* b = c.SolidBrush(); if (!t || !b || text.empty() || color.a <= 0) return;
    auto* layout = MakeLayout(c, text, box.right - box.left, f, wrap, align, trim); if (!layout) return;
    b->SetColor(color); t->DrawTextLayout(D2D1::Point2F(box.left, box.top), layout, b, D2D1_DRAW_TEXT_OPTIONS_NONE); layout->Release();
}

// Runs of one text in their own colour (and weight): CSS « <span> »s. One layout, so every run starts at the exact
// advance of the previous one (kerning included). `weight` 0: the font's.
struct Span { std::wstring text; D2D1_COLOR_F color; int weight = 0; };
inline void Spans(Chrome& c, const std::vector<Span>& runs, D2D1_RECT_F box, Font f, DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING, bool wrap = false, bool trim = false) {
    auto* t = c.RenderTarget(); auto* b = c.SolidBrush(); if (!t || !b || runs.empty()) return;
    std::wstring all; for (const auto& r : runs) all += r.text;
    auto* layout = MakeLayout(c, all, box.right - box.left, f, wrap, align, trim); if (!layout) return;
    std::vector<ID2D1SolidColorBrush*> brushes; UINT32 at = 0;
    for (std::size_t i = 0; i < runs.size(); ++i) {
        const DWRITE_TEXT_RANGE range{at, static_cast<UINT32>(runs[i].text.size())}; at += range.length;
        if (runs[i].weight) layout->SetFontWeight(static_cast<DWRITE_FONT_WEIGHT>(runs[i].weight), range);
        if (i == 0) continue;
        ID2D1SolidColorBrush* own = nullptr;
        if (SUCCEEDED(t->CreateSolidColorBrush(runs[i].color, &own)) && own) { layout->SetDrawingEffect(own, range); brushes.push_back(own); }
    }
    b->SetColor(runs[0].color); t->DrawTextLayout(D2D1::Point2F(box.left, box.top), layout, b, D2D1_DRAW_TEXT_OPTIONS_NONE);
    for (auto* own : brushes) own->Release();
    layout->Release();
}
// Height of `text` wrapped in `width` (whole line boxes).
inline float WrappedHeight(Chrome& c, const std::wstring& text, float width, Font f) {
    auto* layout = MakeLayout(c, text, width, f, true, DWRITE_TEXT_ALIGNMENT_LEADING, false); if (!layout) return f.line;
    DWRITE_TEXT_METRICS m{}; layout->GetMetrics(&m); layout->Release(); return std::max(f.line, static_cast<float>(m.lineCount) * f.line);
}

// ---- Shapes ---------------------------------------------------------------------------------------------
inline void Fill(Chrome& c, D2D1_RECT_F r, float radius, D2D1_COLOR_F color) {
    auto* t = c.RenderTarget(); auto* b = c.SolidBrush(); if (!t || !b || color.a <= 0) return;
    b->SetColor(color); if (radius > 0) t->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), b); else t->FillRectangle(r, b);
}
// A box-sizing: border-box border (inside the rectangle).
inline void Border(Chrome& c, D2D1_RECT_F r, float radius, float width, D2D1_COLOR_F color) {
    auto* t = c.RenderTarget(); auto* b = c.SolidBrush(); if (!t || !b || color.a <= 0) return;
    const float h = width / 2; b->SetColor(color);
    const D2D1_RECT_F in{r.left + h, r.top + h, r.right - h, r.bottom - h};
    if (radius > 0) t->DrawRoundedRectangle(D2D1::RoundedRect(in, std::max(0.f, radius - h), std::max(0.f, radius - h)), b, width); else t->DrawRectangle(in, b, width);
}
struct Stop { float at; Rgb8 color; float alpha; };
// Linear gradient between two points, sRGB stops as given.
inline void Gradient(Chrome& c, D2D1_RECT_F r, float radius, D2D1_POINT_2F from, D2D1_POINT_2F to, const std::vector<Stop>& stops, float opacity = 1) {
    auto* t = c.RenderTarget(); if (!t || stops.empty() || opacity <= 0) return;
    std::vector<D2D1_GRADIENT_STOP> s; for (const auto& x : stops) s.push_back({x.at, Col(x.color, x.alpha * opacity)});
    ID2D1GradientStopCollection* collection = nullptr; ID2D1LinearGradientBrush* brush = nullptr;
    if (SUCCEEDED(t->CreateGradientStopCollection(s.data(), static_cast<UINT32>(s.size()), &collection)) && SUCCEEDED(t->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(from, to), collection, &brush))) {
        if (radius > 0) t->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), brush); else t->FillRectangle(r, brush);
    }
    if (brush) brush->Release();
    if (collection) collection->Release();
}
// CSS « linear-gradient(in oklab …) » between opaque stops: sRGB sub-stops sampled along the OKLab line.
inline std::vector<Stop> OklabStops(const std::vector<std::pair<float, Rgb8>>& stops, int steps = 6) {
    std::vector<Stop> out;
    for (std::size_t i = 0; i + 1 < stops.size(); ++i)
        for (int k = 0; k < steps; ++k) { const float u = k / static_cast<float>(steps); out.push_back({stops[i].first + (stops[i + 1].first - stops[i].first) * u, MixOklab(stops[i].second, stops[i + 1].second, u), 1}); }
    if (!stops.empty()) out.push_back({stops.back().first, stops.back().second, 1});
    return out;
}
inline void DrawBitmap(ID2D1RenderTarget* t, ID2D1Bitmap* bitmap, D2D1_RECT_F to, float opacity, const D2D1_RECT_F* from = nullptr) {
    if (!t || !bitmap || opacity <= 0) return;
    ID2D1DeviceContext* dc = nullptr;
    if (SUCCEEDED(t->QueryInterface(__uuidof(ID2D1DeviceContext), reinterpret_cast<void**>(&dc))) && dc) { dc->DrawBitmap(bitmap, &to, opacity, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, from); dc->Release(); }
    else t->DrawBitmap(bitmap, to, opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, from);
}

// ---- Glyphs (path data from the boards; device independent geometry, cached per factory) ---------------
namespace Path {
// Stroked, 24 view box, stroke 1.8 round.
inline constexpr char chevron[] = "m7 10 5 5 5-5";
inline constexpr char close[] = "m7 7 10 10m0-10L7 17";
inline constexpr char check[] = "m5 12 4 4L19 6";
// Filled.
inline constexpr char user[] = "M122.682 176.875a4.25 4.25 0 0 1-3.682 2.125H17a4.25 4.25 0 0 1-3.676-6.375c8.091-13.988 20.559-24.018 35.11-28.773a38.25 38.25 0 1 1 39.132 0c14.551 4.755 27.019 14.785 35.11 28.773A4.25 4.25 0 0 1 122.682 176.875Z";   // view box 4 61.6 128 128
inline constexpr char cloudDown[] = "M123.965 62.26C123.055 38.77 103.535 20 80.03 20A44.05 44.05 0 0 0 40.645 44.335h0A43.74 43.74 0 0 0 36 63.865 4.09 4.09 0 0 1 32.285 68 4 4 0 0 1 28 64a51.83 51.83 0 0 1 2.67-16.46 2 2 0 0 0-2.375-2.59A32.045 32.045 0 0 0 4 76c0 17.595 14.875 32 32.5 32H80A44.045 44.045 0 0 0 123.965 62.26Zm-25.135 12.57l-16 16a4 4 0 0 1-5.66 0l-16-16a4 4 0 0 1 5.66-5.66L76 78.345V48a4 4 0 0 1 8 0v30.345l9.17-9.175a4 4 0 0 1 5.66 5.66Z";   // 128
inline constexpr char checkBold[] = "M232.49,80.49l-128,128a12,12,0,0,1-17,0l-56-56a12,12,0,1,1,17-17L96,183,215.51,63.51a12,12,0,0,1,17,17Z";   // 256
inline constexpr char warning[] = "M240.26,186.1,152.81,34.23h0a28.74,28.74,0,0,0-49.62,0L15.74,186.1a27.45,27.45,0,0,0,0,27.71A28.31,28.31,0,0,0,40.55,228h174.9a28.31,28.31,0,0,0,24.79-14.19A27.45,27.45,0,0,0,240.26,186.1Zm-20.8,15.7a4.46,4.46,0,0,1-4,2.2H40.55a4.46,4.46,0,0,1-4-2.2,3.56,3.56,0,0,1,0-3.73L124,46.2a4.77,4.77,0,0,1,8,0l87.44,151.87A3.56,3.56,0,0,1,219.46,201.8ZM116,136V104a12,12,0,0,1,24,0v32a12,12,0,0,1-24,0Zm28,40a16,16,0,1,1-16-16A16,16,0,0,1,144,176Z";   // 256
inline constexpr char info[] = "M47.25 36.751a7 7 0 1 1 7 7A7 7 0 0 1 47.25 36.751Zm56 19.25A47.25 47.25 0 1 1 56 8.751 47.302 47.302 90 0 1 103.25 56.001Zm-10.5 0a36.75 36.75 0 1 0-36.75 36.75A36.789 36.789 90 0 0 92.75 56.001Zm-31.5 16.047V57.751a8.75 8.75 0 0 0-8.75-8.75 5.25 5.25 0 0 0-1.75 10.202V73.501a8.75 8.75 0 0 0 8.75 8.75 5.25 5.25 0 0 0 1.75-10.203Z";   // 112
inline constexpr char plus[] = "M228,128a12,12,0,0,1-12,12H140v76a12,12,0,0,1-24,0V140H40a12,12,0,0,1,0-24h76V40a12,12,0,0,1,24,0v76h76A12,12,0,0,1,228,128Z";   // 256
inline constexpr char shield[] = "M208,36H48A20,20,0,0,0,28,56v56c0,54.29,26.32,87.22,48.4,105.29,23.71,19.39,47.44,26,48.44,26.29a12.1,12.1,0,0,0,6.32,0c1-.28,24.73-6.9,48.44-26.29,22.08-18.07,48.4-51,48.4-105.29V56A20,20,0,0,0,208,36Zm-4,76c0,35.71-13.09,64.69-38.91,86.15A126.28,126.28,0,0,1,128,219.38a126.14,126.14,0,0,1-37.09-21.23C65.09,176.69,52,147.71,52,112V60H204Z";   // 256
inline constexpr char arrowUp[] = "M208.49,120.49a12,12,0,0,1-17,0L140,69V216a12,12,0,0,1-24,0V69L64.49,120.49a12,12,0,0,1-17-17l72-72a12,12,0,0,1,17,0l72,72A12,12,0,0,1,208.49,120.49Z";   // 256
inline constexpr char claude[] = "M3.979 13.968l3.898-2.188 0.066-0.19-0.066-0.105h-0.191l-0.652-0.041-2.228-0.06-1.931-0.08-1.872-0.1-0.472-0.101-0.441-0.582 0.045-0.291 0.397-0.266 0.567 0.051 1.254 0.085 1.882 0.131 1.364 0.079 2.022 0.211h0.322l0.045-0.13-0.11-0.081-0.086-0.079L5.846 8.91l-2.108-1.394-1.104-0.804-0.597-0.406-0.301-0.381-0.13-0.833 0.541-0.597 0.728 0.502 0.186 0.501 0.738 0.567 1.575 1.219 2.057 1.515 0.301 0.251 0.12-0.085 0.015-0.061-0.135-0.225-1.119-2.023-1.195-2.056-0.531-0.853-0.141-0.512c-0.05-0.211-0.085-0.386-0.085-0.602L5.279 0.893 5.62 0.783l0.823 0.11 0.346 0.301 0.512 1.169 0.828 1.842 1.284 2.503 0.377 0.743 0.2 0.688 0.075 0.211h0.131v-0.121l0.106-1.41 0.195-1.731 0.19-2.228 0.066-0.628 0.311-0.752 0.617-0.406 0.482 0.231 0.396 0.567-0.552 0.366-0.235 1.53-0.462 2.398-0.301 1.606h0.175l0.201-0.201 0.813-1.078 1.364-1.706 0.602-0.677 0.702-0.748 0.452-0.356h0.852l0.628 0.933-0.281 0.964-0.879 1.114-0.727 0.942-1.044 1.405-0.653 1.124 0.061 0.091 0.156-0.016 2.357-0.501 1.275-0.231 1.52-0.261 0.687 0.321 0.753 0.327-0.271 0.666-1.626 0.402-1.906 0.381-2.84 0.672-0.351 0.026 0.402 0.05 1.279 0.12 0.547 0.03h1.339l2.495 0.186 0.652 0.431 0.391 0.528-0.653 0.401-1.003 0.512-1.354-0.321-3.161-0.753-1.084-0.271h-0.15v0.09l0.903 0.884 1.655 1.495 2.072 1.926 0.105 0.477-0.266 0.376-0.281-0.041-1.82-1.369-0.703-0.617-1.591-1.339h-0.106v0.14l0.367 0.538 1.937 2.909 0.1 0.893-0.14 0.291-0.502 0.176-0.552-0.101-1.134-1.591L11.967 15.623l-0.944-1.605-0.114 0.065-0.558 5.995-0.26 0.306-0.602 0.23-0.503-0.38-0.266-0.618 0.267-1.219 0.321-1.591 0.261-1.264 0.236-1.57 0.14-0.522-0.01-0.035-0.116 0.015-1.183 1.626-1.802 2.433-1.425 1.526-0.341 0.136-0.592-0.307 0.055-0.547 0.332-0.486 1.972-2.51 1.189-1.554 0.767-0.899-0.005-0.13h-0.045l-5.237 3.401-0.934 0.12-0.401-0.376 0.05-0.617 0.192-0.201 1.575-1.083Z";   // view box 0.667 1.6 18.666 18.666
inline constexpr char cross[] = "M16 4 V12 M16 20 V28 M4 16 H12 M20 16 H28";   // 32, stroked
}
class Glyphs {
    std::map<const char*, ID2D1PathGeometry*> paths_; ID2D1Factory* factory_ = nullptr; ID2D1StrokeStyle* round_ = nullptr;
    ID2D1PathGeometry* Get(Chrome& c, const char* d) {
        if (c.Factory() != factory_) { Clear(); factory_ = c.Factory(); }
        if (!factory_) return nullptr;
        auto& g = paths_[d]; if (!g) g = PopupKit::ParsePath(factory_, d);
        return g;
    }
public:
    ~Glyphs() { Clear(); }
    void Clear() { for (auto& p : paths_) if (p.second) p.second->Release(); paths_.clear(); if (round_) { round_->Release(); round_ = nullptr; } factory_ = nullptr; }
    // `d` in a square view box (origin vx, vy, side) mapped onto `box`.
    void Fill(Chrome& c, const char* d, D2D1_RECT_F box, float side, D2D1_COLOR_F color, float vx = 0, float vy = 0) {
        auto* t = c.RenderTarget(); auto* b = c.SolidBrush(); auto* g = Get(c, d); if (!t || !b || !g || color.a <= 0) return;
        const float k = (box.right - box.left) / side; D2D1_MATRIX_3X2_F base; t->GetTransform(&base);
        t->SetTransform(D2D1::Matrix3x2F::Translation(-vx, -vy) * D2D1::Matrix3x2F::Scale(k, k) * D2D1::Matrix3x2F::Translation(box.left, box.top) * base);
        b->SetColor(color); t->FillGeometry(g, b); t->SetTransform(base);
    }
    // Round caps and joins; `width` in view box units.
    void Stroke(Chrome& c, const char* d, D2D1_RECT_F box, float side, float width, D2D1_COLOR_F color) {
        auto* t = c.RenderTarget(); auto* b = c.SolidBrush(); auto* g = Get(c, d); if (!t || !b || !g || color.a <= 0) return;
        if (!round_) factory_->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND), nullptr, 0, &round_);
        const float k = (box.right - box.left) / side; D2D1_MATRIX_3X2_F base; t->GetTransform(&base);
        t->SetTransform(D2D1::Matrix3x2F::Scale(k, k) * D2D1::Matrix3x2F::Translation(box.left, box.top) * base);
        b->SetColor(color); t->DrawGeometry(g, b, width, round_); t->SetTransform(base);
    }
};

// CSS « filter: blur(4px); opacity: .2 » on a row of bars: one soft pass over all of them (the blur is
// linear, so the sum of blurred bars is the blur of the sum). Without the effect: the bars, fainter.
inline void BlurredBars(Chrome& c, const std::vector<D2D1_RECT_F>& bars, float radius, D2D1_COLOR_F color, float deviation) {
    auto* t = c.RenderTarget(); auto* b = c.SolidBrush(); if (!t || !b || bars.empty() || color.a <= 0) return;
    D2D1_RECT_F all = bars.front(); for (const auto& r : bars) { all.left = std::min(all.left, r.left); all.top = std::min(all.top, r.top); all.right = std::max(all.right, r.right); all.bottom = std::max(all.bottom, r.bottom); }
    const float pad = std::ceil(deviation * 3) + 1, w = all.right - all.left + 2 * pad, h = all.bottom - all.top + 2 * pad;
    static const CLSID gaussian = {0x1feb6d69, 0x2fe6, 0x4ac9, {0x8c, 0x58, 0x1d, 0x7f, 0x93, 0xe7, 0xa6, 0xa5}};
    ID2D1DeviceContext* dc = nullptr; ID2D1Effect* blur = nullptr; ID2D1BitmapRenderTarget* layer = nullptr; bool done = false;
    if (SUCCEEDED(t->QueryInterface(__uuidof(ID2D1DeviceContext), reinterpret_cast<void**>(&dc))) && dc && SUCCEEDED(dc->CreateEffect(gaussian, &blur)) && blur && SUCCEEDED(t->CreateCompatibleRenderTarget(D2D1::SizeF(w, h), &layer)) && layer) {
        ID2D1SolidColorBrush* lb = nullptr; layer->BeginDraw(); layer->Clear(D2D1::ColorF(0, 0, 0, 0));
        if (SUCCEEDED(layer->CreateSolidColorBrush(color, &lb)) && lb) { for (const auto& r : bars) layer->FillRoundedRectangle(D2D1::RoundedRect({r.left - all.left + pad, r.top - all.top + pad, r.right - all.left + pad, r.bottom - all.top + pad}, radius, radius), lb); lb->Release(); }
        ID2D1Bitmap* bitmap = nullptr;
        if (SUCCEEDED(layer->EndDraw()) && SUCCEEDED(layer->GetBitmap(&bitmap)) && bitmap) {
            blur->SetInput(0, bitmap); const UINT32 soft = 0;
            blur->SetValue(0, D2D1_PROPERTY_TYPE_FLOAT, reinterpret_cast<const BYTE*>(&deviation), sizeof(deviation));
            blur->SetValue(2, D2D1_PROPERTY_TYPE_ENUM, reinterpret_cast<const BYTE*>(&soft), sizeof(soft));
            ID2D1Image* output = nullptr; blur->GetOutput(&output);
            if (output) { const D2D1_POINT_2F at{all.left - pad, all.top - pad}; dc->DrawImage(output, &at, nullptr, D2D1_INTERPOLATION_MODE_LINEAR, D2D1_COMPOSITE_MODE_SOURCE_OVER); output->Release(); done = true; }
            blur->SetInput(0, nullptr);
        }
        if (bitmap) bitmap->Release();
    }
    if (layer) layer->Release();
    if (blur) blur->Release();
    if (dc) dc->Release();
    if (!done) { b->SetColor(D2D1::ColorF(color.r, color.g, color.b, color.a * .35f)); for (const auto& r : bars) t->FillRoundedRectangle(D2D1::RoundedRect({r.left - 3, r.top - 3, r.right + 3, r.bottom + 3}, radius + 3, radius + 3), b); }
}

} // namespace OnboardingKit
} // namespace NativeChrome
