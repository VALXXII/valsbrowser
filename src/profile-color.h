#pragma once
// Profile colours (Paper p-I-0, P06/P07): one hue + one intensity (OKLCH chroma) per profile.
//  - Ink: identity glyph, caret, link, check, progress. OKLCH L 0.78, so every hue reads on the
//    canvas #171717 (>= 8:1) and on its own 10 % plate (>= 7:1).
//  - AccentFill: filled controls (primary button, checkbox, switch) under WHITE text. L is solved
//    per hue as the lightest value that keeps white >= 4.6:1 (WCAG AA 4.5 with margin).
//  - Neutral: intensity 0, the default « Personnel » grey (#B3B3B3 ink, #747474 fill).
// Pure C++17, no Win32: unit-tested in tests/profile_color_test.cpp.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace ProfileColor {

struct Rgb8 {
    std::uint8_t r = 0, g = 0, b = 0;
    bool operator==(const Rgb8& o) const { return r == o.r && g == o.g && b == o.b; }
    bool operator!=(const Rgb8& o) const { return !(*this == o); }
};

inline constexpr float MaxChroma = .16f;     // slider « Intensité » 100 %
inline constexpr float InkLightness = .78f;
inline constexpr double FillWhiteContrast = 4.6;
inline constexpr Rgb8 Canvas{0x17, 0x17, 0x17};

namespace Detail {
inline constexpr double Pi = 3.14159265358979323846;
inline double ToLinear(double v) { return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4); }
inline double ToGamma(double v) { return v <= .0031308 ? 12.92 * v : 1.055 * std::pow(v, 1 / 2.4) - .055; }
struct Linear { double r, g, b; };
inline Linear FromOklch(double L, double C, double hDeg) {
    const double h = hDeg * Pi / 180, a = C * std::cos(h), b = C * std::sin(h);
    const double l_ = L + .3963377774 * a + .2158037573 * b, m_ = L - .1055613458 * a - .0638541728 * b, s_ = L - .0894841775 * a - 1.2914855480 * b;
    const double l = l_ * l_ * l_, m = m_ * m_ * m_, s = s_ * s_ * s_;
    return {4.0767416621 * l - 3.3077115913 * m + .2309699292 * s, -1.2684380046 * l + 2.6097574011 * m - .3413193965 * s, -.0041960863 * l - .7034186147 * m + 1.7076147010 * s};
}
inline bool InGamut(const Linear& c) { const double e = 1e-6; return c.r >= -e && c.r <= 1 + e && c.g >= -e && c.g <= 1 + e && c.b >= -e && c.b <= 1 + e; }
// Largest chroma <= C that fits sRGB at this L and hue (the hue never changes).
inline double FitChroma(double L, double C, double h) {
    if (InGamut(FromOklch(L, C, h))) return C;
    double lo = 0, hi = C;
    for (int i = 0; i < 40; ++i) { const double mid = (lo + hi) / 2; (InGamut(FromOklch(L, mid, h)) ? lo : hi) = mid; }
    return lo;
}
inline std::uint8_t Byte(double linear) { const double v = ToGamma(std::clamp(linear, 0.0, 1.0)); return static_cast<std::uint8_t>(std::lround(v * 255)); }
inline Rgb8 Pack(const Linear& c) { return {Byte(c.r), Byte(c.g), Byte(c.b)}; }
inline double Luminance(Rgb8 c) { return .2126 * ToLinear(c.r / 255.0) + .7152 * ToLinear(c.g / 255.0) + .0722 * ToLinear(c.b / 255.0); }
inline int NormalizeHue(int hue) { hue %= 360; return hue < 0 ? hue + 360 : hue; }
inline double ClampChroma(float chroma) { return std::isfinite(chroma) ? std::clamp(static_cast<double>(chroma), 0.0, static_cast<double>(MaxChroma)) : 0.0; }
} // namespace Detail

// WCAG 2.x contrast ratio, 1..21.
inline double Contrast(Rgb8 a, Rgb8 b) {
    double x = Detail::Luminance(a), y = Detail::Luminance(b); if (x < y) std::swap(x, y);
    return (x + .05) / (y + .05);
}
inline Rgb8 Neutral() { return {0xB3, 0xB3, 0xB3}; }       // --color-profile-gris
inline Rgb8 NeutralFill() { return {0x74, 0x74, 0x74}; }   // Paper P07 « Neutre #747474 » (white 4.67:1)

// Identity / text colour: OKLCH L 0.78 at this hue, chroma clamped into sRGB. Chroma 0 = Neutral().
inline Rgb8 Ink(int hue, float chroma) {
    const double c = Detail::ClampChroma(chroma);
    if (c <= 0) return Neutral();
    const double h = Detail::NormalizeHue(hue);
    return Detail::Pack(Detail::FromOklch(InkLightness, Detail::FitChroma(InkLightness, c, h), h));
}

// Filled accent under white text: the lightest L (searched down from 0.62) where white >= 4.6:1.
inline Rgb8 AccentFill(int hue, float chroma) {
    const double c = Detail::ClampChroma(chroma);
    if (c <= 0) return NeutralFill();
    const double h = Detail::NormalizeHue(hue);
    const Rgb8 white{255, 255, 255};
    Rgb8 best = Detail::Pack(Detail::FromOklch(.40, Detail::FitChroma(.40, c, h), h));
    for (int step = 0; step <= 220; ++step) {   // L 0.62 → 0.40 in 0.001 steps: first that passes is the lightest
        const double L = .62 - step * .001;
        const Rgb8 candidate = Detail::Pack(Detail::FromOklch(L, Detail::FitChroma(L, c, h), h));
        if (Contrast(white, candidate) >= FillWhiteContrast) { best = candidate; break; }
    }
    return best;
}

// Plate: `ink` at `alpha` (0..1) over the canvas, as rendered (sRGB blend, like the Paper color-mix).
inline Rgb8 Over(Rgb8 top, double alpha, Rgb8 under = Canvas) {
    alpha = std::clamp(alpha, 0.0, 1.0);
    auto mix = [alpha](std::uint8_t a, std::uint8_t b) { return static_cast<std::uint8_t>(std::lround(b + (a - b) * alpha)); };
    return {mix(top.r, under.r), mix(top.g, under.g), mix(top.b, under.b)};
}

inline std::wstring Hex(Rgb8 c) { wchar_t t[8]; swprintf(t, 8, L"#%02X%02X%02X", c.r, c.g, c.b); return t; }

// « #RRGGBB », « RRGGBB » or « #RGB ». Keeps the hue and the chroma (clamped to MaxChroma);
// the lightness is NOT kept: the profile always displays at L 0.78 (Paper P06 « Hex sombre »).
// A near-grey value gives chroma 0 (neutral). False on anything else; outputs untouched.
inline bool FromHex(const std::wstring& text, int& hue, float& chroma) {
    std::wstring s = text;
    while (!s.empty() && iswspace(s.front())) s.erase(s.begin());
    while (!s.empty() && iswspace(s.back())) s.pop_back();
    if (!s.empty() && s[0] == L'#') s.erase(0, 1);
    if (s.size() == 3) s = std::wstring{s[0], s[0], s[1], s[1], s[2], s[2]};
    if (s.size() != 6) return false;
    int v[6];
    for (int i = 0; i < 6; ++i) {
        const wchar_t ch = s[i];
        if (ch >= L'0' && ch <= L'9') v[i] = ch - L'0';
        else if (ch >= L'a' && ch <= L'f') v[i] = ch - L'a' + 10;
        else if (ch >= L'A' && ch <= L'F') v[i] = ch - L'A' + 10;
        else return false;
    }
    const double r = Detail::ToLinear((v[0] * 16 + v[1]) / 255.0), g = Detail::ToLinear((v[2] * 16 + v[3]) / 255.0), b = Detail::ToLinear((v[4] * 16 + v[5]) / 255.0);
    const double l = std::cbrt(.4122214708 * r + .5363325363 * g + .0514459929 * b);
    const double m = std::cbrt(.2119034982 * r + .6806995451 * g + .1073969566 * b);
    const double s2 = std::cbrt(.0883024619 * r + .2817188376 * g + .6299787005 * b);
    const double A = 1.9779984951 * l - 2.4285922050 * m + .4505937099 * s2;
    const double B = .0259040371 * l + .7827717662 * m - .8086757660 * s2;
    const double C = std::sqrt(A * A + B * B);
    if (C < .02) { chroma = 0; return true; }   // greys (and near-greys) are the neutral profile colour; hue kept
    double h = std::atan2(B, A) * 180 / Detail::Pi; if (h < 0) h += 360;
    hue = Detail::NormalizeHue(static_cast<int>(std::lround(h)));
    chroma = static_cast<float>(std::min(C, static_cast<double>(MaxChroma)));
    return true;
}

// Spoken / shown hue name, 30° buckets (Paper P06 caption).
inline const wchar_t* HueName(int hue) {
    static const wchar_t* names[12] = {L"rouge", L"orange", L"jaune", L"citron", L"vert", L"émeraude", L"cyan", L"azur", L"bleu", L"violet", L"magenta", L"rose"};
    return names[Detail::NormalizeHue(hue) / 30];
}

} // namespace ProfileColor
