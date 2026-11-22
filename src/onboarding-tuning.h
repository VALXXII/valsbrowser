#pragma once
// Onboarding — the transition between board 2 (« Créer votre profil ») and board 3 (« Importer vos données »),
// as numbers. The window (src/onboarding.h) plays exactly this timeline; the defaults ARE the shipped motion.
// Only « --onboarding-debug » opens the panel that changes them (src/onboarding-debug.h), to let the designer
// tune the motion and hand the values back. Pure data + text (de)serialisation, no Win32: unit-tested in
// tests/onboarding/onboarding_tuning_test.cpp. Same idea as src/motion-tuning.h (the motion lab).
//
//   0 ───────────── duration ─────────────▶
//   old content fades out │ new content fades in (rising `contentRise` DIP into place)
//   ◀── outShare ──▶
//        mockDelay ▶│◀──────── mockDuration ────────▶│   the browser mock travels and scales between its two
//                                                         framings along a cubic-bezier, the veil follows it,
//                                                         and it may dim mid-way (`mockDim`).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>

namespace Onboarding {
namespace Tuning {

enum Id { Duration, OutShare, ContentRise, MockDelay, MockDuration, CurveX1, CurveY1, CurveX2, CurveY2, MockDim, Count };
struct Param { Id id; const char* key; const wchar_t* label; const wchar_t* unit; float min, max, def, step; int decimals; };
inline const Param* Params() {
    static const Param p[Count] = {
        {Duration, "transition23.duration", L"Durée du contenu", L"ms", 60, 1500, 220, 5, 0},
        {OutShare, "transition23.outShare", L"Part du fondu sortant", L"", 0, .9f, .4f, .01f, 2},
        {ContentRise, "transition23.contentRise", L"Montée du nouveau contenu", L"px", 0, 40, 0, 1, 0},
        {MockDelay, "transition23.mockDelay", L"Aperçu : délai", L"ms", 0, 800, 0, 5, 0},
        {MockDuration, "transition23.mockDuration", L"Aperçu : durée du déplacement", L"ms", 60, 2000, 220, 5, 0},
        {CurveX1, "transition23.curveX1", L"Courbe : x1", L"", 0, 1, .23f, .01f, 2},
        {CurveY1, "transition23.curveY1", L"Courbe : y1", L"", -.5f, 1.5f, 1, .01f, 2},
        {CurveX2, "transition23.curveX2", L"Courbe : x2", L"", 0, 1, .32f, .01f, 2},
        {CurveY2, "transition23.curveY2", L"Courbe : y2", L"", -.5f, 1.5f, 1, .01f, 2},
        {MockDim, "transition23.mockDim", L"Aperçu : atténuation à mi-course", L"", 0, 1, 0, .01f, 2},
    };
    return p;
}
using Values = std::array<float, Count>;
inline Values Defaults() { Values v{}; for (int i = 0; i < Count; ++i) v[static_cast<std::size_t>(i)] = Params()[i].def; return v; }
inline float Clamp(int id, float value) { const Param& p = Params()[id]; return std::isfinite(value) ? std::min(p.max, std::max(p.min, value)) : p.def; }
// The slider's value: on the parameter's step, inside its bounds.
inline float Step(int id, float value) { const Param& p = Params()[id]; return Clamp(id, std::round(value / p.step) * p.step); }

inline constexpr const char* Header = "browser-onboarding-tuning 1";
inline std::string Write(const Values& v) {
    std::string out = std::string(Header) + "\n";
    for (int i = 0; i < Count; ++i) { char line[96]; std::snprintf(line, sizeof(line), "%s=%.*f\n", Params()[i].key, Params()[i].decimals, static_cast<double>(v[static_cast<std::size_t>(i)])); out += line; }
    return out;
}
// Anything unexpected keeps the default: an old or hand-edited file never breaks the motion.
inline Values Read(const std::string& text) {
    Values v = Defaults(); std::istringstream in(text); std::string line;
    if (!std::getline(in, line)) return v;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line != Header) return v;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto eq = line.find('='); if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq), value = line.substr(eq + 1);
        for (int i = 0; i < Count; ++i) {
            if (key != Params()[i].key) continue;
            char* end = nullptr; const float f = std::strtof(value.c_str(), &end);
            if (end != value.c_str() && std::isfinite(f)) v[static_cast<std::size_t>(i)] = Clamp(i, f);
        }
    }
    return v;
}

// CSS cubic-bezier(x1, y1, x2, y2) at time t (0..1).
inline float Bezier(float x1, float y1, float x2, float y2, float t) {
    if (t <= 0) return 0;
    if (t >= 1) return 1;
    auto at = [](float a, float b, float s) { const float u = 1 - s; return 3 * u * u * s * a + 3 * u * s * s * b + s * s * s; };
    float lo = 0, hi = 1, s = t;
    for (int i = 0; i < 24; ++i) { s = (lo + hi) / 2; (at(x1, x2, s) < t ? lo : hi) = s; }
    return at(y1, y2, s);
}

struct Frame {
    bool swapped = false;        // the new board's content is the one drawn
    float oldAlpha = 1, newAlpha = 0;
    float rise = 0;              // DIP the new content still has to climb
    float mock = 0;              // 0 = board 2's framing of the browser mock, 1 = board 3's
    float dim = 0;               // how much the mock is dimmed right now (0..mockDim)
};
inline float TotalMs(const Values& v) { return std::max(v[Duration], v[MockDelay] + v[MockDuration]); }
inline bool Done(const Values& v, float ms) { return ms >= TotalMs(v); }
inline Frame At(const Values& v, float ms) {
    Frame f; const float c = std::min(1.f, std::max(0.f, ms / v[Duration])), share = v[OutShare];
    f.swapped = c >= share && ms > 0;
    if (!f.swapped) { f.oldAlpha = share > 0 ? 1 - c / share : 0; f.newAlpha = 0; }
    else { f.oldAlpha = 0; f.newAlpha = share < 1 ? (c - share) / (1 - share) : 1; f.rise = v[ContentRise] * (1 - f.newAlpha) * (1 - f.newAlpha); }
    const float m = std::min(1.f, std::max(0.f, (ms - v[MockDelay]) / v[MockDuration]));
    f.mock = Bezier(v[CurveX1], v[CurveY1], v[CurveX2], v[CurveY2], m);
    f.dim = v[MockDim] * std::sin(3.14159265f * m);
    if (f.dim < 1e-4f) f.dim = 0;
    return f;
}

} // namespace Tuning
} // namespace Onboarding
