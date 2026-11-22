#pragma once
// Onboarding — its motion as numbers. Every screen-to-screen transition and the hand-off to the browser are
// timelines played from the values below; the defaults ARE the shipped motion (the designer's own settings, tuned
// with the review panel of « --onboarding-debug », src/onboarding-debug.h). Pure data + text (de)serialisation, no
// Win32: unit-tested in tests/onboarding/onboarding_tuning_test.cpp. Same idea as src/motion-tuning.h.
//
// A transition between two boards (sections 1 → 2 … 6 → 7; the way back plays the same section):
//   0 ───────────── duration ─────────────▶
//   old content fades out │ new content fades in (rising `contentRise` DIP into place)
//   ◀── outShare ──▶
//        moveDelay ▶│◀──────── moveDuration ────────▶│   the main movement: the backdrop goes from one board's
//                                                         to the other's (the browser mock travels and scales,
//                                                         shaders and the bottom veil cross-fade) along a
//                                                         cubic-bezier; it may dim mid-way (`moveDim`).
// The hand-off (section « 7 → navigateur »), from the moment board 7 is in place:
//   hold │ logo shrinks and fades (the dots go on) │ window shrinks and fades │ browser grows in and fades in
//                              ◀ windowOverlap ▶│             ◀ browserOverlap ▶│
// Reduced motion plays none of this: short fades, nothing scales or travels (ExitPlanFor, src/onboarding-flow.h).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include "onboarding-flow.h"

namespace Onboarding {
namespace Tuning {

enum Section { T12, T23, T34, T45, T56, T67, Exit, Trame, Sections };
// Parameters of a board-to-board section.
enum Id { Duration, OutShare, ContentRise, MockDelay, MockDuration, CurveX1, CurveY1, CurveX2, CurveY2, MockDim, Count };
// Parameters of the hand-off.
enum ExitId { Hold, LogoDuration, LogoScale, WindowOverlap, WindowDuration, WindowScale, BrowserOverlap, BrowserDuration, BrowserScale, ExitX1, ExitY1, ExitX2, ExitY2, ExitCount };
// Parameters of board 6's dither (src/onboarding-dither.h).
enum TrameId { TrameOpacity, TramePitch, TramePeriod, TrameBase, TrameWave, TrameCount };
inline constexpr int MaxParams = 13;
struct Param { const char* key; const wchar_t* label; const wchar_t* unit; float min, max, def, step; int decimals; };

inline const char* Prefix(Section s) { static const char* p[Sections] = {"transition12", "transition23", "transition34", "transition45", "transition56", "transition67", "exit", "trame"}; return p[s]; }
inline const wchar_t* Label(Section s) { static const wchar_t* l[Sections] = {L"1 → 2", L"2 → 3", L"3 → 4", L"4 → 5", L"5 → 6", L"6 → 7", L"7 → navigateur", L"6 · Trame"}; return l[s]; }
inline int ParamCount(Section s) { return s == Exit ? static_cast<int>(ExitCount) : s == Trame ? static_cast<int>(TrameCount) : static_cast<int>(Count); }
// The designer's pace (tuned on 2 → 3, asked for everywhere): a short cross-fade of the content that leads
// into a 740 ms main movement on cubic-bezier(0.33, 0.99, 0.42, 0.96).
inline const Param* Params(Section s) {
    static const Param move[Count] = {
        {"duration", L"Durée du contenu", L"ms", 60, 1500, 380, 5, 0},
        {"outShare", L"Part du fondu sortant", L"", 0, .9f, .23f, .01f, 2},
        {"contentRise", L"Montée du nouveau contenu", L"px", 0, 40, 0, 1, 0},
        {"mockDelay", L"Mouvement : délai", L"ms", 0, 800, 0, 5, 0},
        {"mockDuration", L"Mouvement : durée", L"ms", 60, 2000, 740, 5, 0},
        {"curveX1", L"Courbe : x1", L"", 0, 1, .33f, .01f, 2},
        {"curveY1", L"Courbe : y1", L"", -.5f, 1.5f, .99f, .01f, 2},
        {"curveX2", L"Courbe : x2", L"", 0, 1, .42f, .01f, 2},
        {"curveY2", L"Courbe : y2", L"", -.5f, 1.5f, .96f, .01f, 2},
        {"mockDim", L"Mouvement : atténuation à mi-course", L"", 0, 1, 0, .01f, 2},
    };
    static const Param exit[ExitCount] = {
        {"hold", L"Logo seul : pause", L"ms", 0, 3000, 500, 10, 0},
        {"logoDuration", L"Logo : durée de sortie", L"ms", 60, 2000, 740, 5, 0},
        {"logoScale", L"Logo : échelle finale", L"", .5f, 1, .92f, .01f, 2},
        {"windowOverlap", L"Fenêtre : avance sur la fin du logo", L"ms", 0, 1000, 200, 5, 0},
        {"windowDuration", L"Fenêtre : durée de sortie", L"ms", 60, 2000, 740, 5, 0},
        {"windowScale", L"Fenêtre : échelle finale", L"", .5f, 1, .92f, .01f, 2},
        {"browserOverlap", L"Navigateur : avance sur la fin de la fenêtre", L"ms", 0, 1000, 300, 5, 0},
        {"browserDuration", L"Navigateur : durée d'entrée", L"ms", 60, 2000, 740, 5, 0},
        {"browserScale", L"Navigateur : échelle de départ", L"", .5f, 1, .96f, .01f, 2},
        {"curveX1", L"Courbe : x1", L"", 0, 1, .33f, .01f, 2},
        {"curveY1", L"Courbe : y1", L"", -.5f, 1.5f, .99f, .01f, 2},
        {"curveX2", L"Courbe : x2", L"", 0, 1, .42f, .01f, 2},
        {"curveY2", L"Courbe : y2", L"", -.5f, 1.5f, .96f, .01f, 2},
    };
    // Board 6's dither: the annotation band's texture (pitch 2 DIP, base 0.18, wave 1), white, subtle.
    static const Param trame[TrameCount] = {
        {"opacity", L"Opacité", L"", 0, 1, .22f, .01f, 2},
        {"pitch", L"Pas des points", L"px", 2, 6, 2, 1, 0},
        {"period", L"Durée d'un passage de la vague", L"s", 2, 30, 8, .5f, 1},
        {"base", L"Densité du fond", L"", 0, .6f, .18f, .01f, 2},
        {"wave", L"Amplitude de la vague", L"", 0, 1.5f, 1, .05f, 2},
    };
    return s == Exit ? exit : s == Trame ? trame : move;
}
using Row = std::array<float, MaxParams>;
struct Values {
    std::array<Row, Sections> v{};
    Row& operator[](Section s) { return v[static_cast<std::size_t>(s)]; }
    const Row& operator[](Section s) const { return v[static_cast<std::size_t>(s)]; }
    bool operator==(const Values& o) const { return v == o.v; }
    bool operator!=(const Values& o) const { return !(*this == o); }
};
inline Row Defaults(Section s) { Row r{}; for (int i = 0; i < ParamCount(s); ++i) r[static_cast<std::size_t>(i)] = Params(s)[i].def; return r; }
inline Values Defaults() { Values v; for (int s = 0; s < Sections; ++s) v[static_cast<Section>(s)] = Defaults(static_cast<Section>(s)); return v; }
inline float Clamp(Section s, int id, float value) { const Param& p = Params(s)[id]; return std::isfinite(value) ? std::min(p.max, std::max(p.min, value)) : p.def; }
// The slider's value: on the parameter's step, inside its bounds.
// It is the number the file would hold (same decimals), so what is shown, saved and read back are one value.
inline float Step(Section s, int id, float value) {
    const Param& p = Params(s)[id]; char text[48];
    std::snprintf(text, sizeof(text), "%.*f", p.decimals, static_cast<double>(Clamp(s, id, std::round(value / p.step) * p.step)));
    return std::strtof(text, nullptr);
}

// The section a change of board plays (either direction).
inline Section Between(Screen a, Screen b) {
    auto pair = [&](Screen x, Screen y) { return (a == x && b == y) || (a == y && b == x); };
    if (a == Screen::Outro || b == Screen::Outro) return T67;
    if (a == Screen::Annotation || b == Screen::Annotation) return T56;
    if (pair(Screen::Welcome, Screen::Profile)) return T12;
    if (pair(Screen::Profile, Screen::ImportPick)) return T23;
    if (pair(Screen::ImportRunning, Screen::ImportDone) || pair(Screen::ImportRunning, Screen::ImportError)) return a == Screen::ImportRunning ? T45 : T34;   // « Réessayer » starts an import again
    if (pair(Screen::ImportPick, Screen::ImportRunning)) return T34;
    return T23;
}

inline constexpr const char* Header = "browser-onboarding-tuning 1";
// One « <section>.<parameter>=<value> » per line.
inline std::string Write(const Values& v) {
    std::string out = std::string(Header) + "\n";
    for (int s = 0; s < Sections; ++s) for (int i = 0; i < ParamCount(static_cast<Section>(s)); ++i) {
        const Param& p = Params(static_cast<Section>(s))[i]; char line[112];
        std::snprintf(line, sizeof(line), "%s.%s=%.*f\n", Prefix(static_cast<Section>(s)), p.key, p.decimals, static_cast<double>(v[static_cast<Section>(s)][static_cast<std::size_t>(i)]));
        out += line;
    }
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
        const auto eq = line.find('='), dot = line.find('.'); if (eq == std::string::npos || dot == std::string::npos || dot > eq) continue;
        const std::string section = line.substr(0, dot), key = line.substr(dot + 1, eq - dot - 1), value = line.substr(eq + 1);
        for (int s = 0; s < Sections; ++s) {
            if (section != Prefix(static_cast<Section>(s))) continue;
            for (int i = 0; i < ParamCount(static_cast<Section>(s)); ++i) {
                if (key != Params(static_cast<Section>(s))[i].key) continue;
                char* end = nullptr; const float f = std::strtof(value.c_str(), &end);
                if (end != value.c_str() && std::isfinite(f)) v[static_cast<Section>(s)][static_cast<std::size_t>(i)] = Clamp(static_cast<Section>(s), i, f);
            }
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

// ---- a board-to-board transition ----------------------------------------------------------------------
struct Frame {
    bool swapped = false;        // the new board's content is the one drawn
    float oldAlpha = 1, newAlpha = 0;
    float rise = 0;              // DIP the new content still has to climb
    float mock = 0;              // the main movement: 0 = the old board's backdrop, 1 = the new one's
    float dim = 0;               // how much the backdrop is dimmed right now (0..moveDim)
};
inline float TotalMs(const Row& v) { return std::max(v[Duration], v[MockDelay] + v[MockDuration]); }
inline bool Done(const Row& v, float ms) { return ms >= TotalMs(v); }
inline Frame At(const Row& v, float ms) {
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

// ---- the hand-off to the browser ------------------------------------------------------------------------
// What is played once board 7 is in place. Times in ms from that moment; the window's and the browser's parts
// are played by src/onboarding-handoff.h from `windowStart` on.
struct ExitPlan {
    float logoStart = 0, logoMs = 0, logoScale = 1;
    float windowStart = 0, windowMs = 0, windowScale = 1;
    float browserStart = 0, browserMs = 0, browserScale = 1;
    float x1 = 0, y1 = 0, x2 = 1, y2 = 1;
    float End() const { return browserStart + browserMs; }
};
inline ExitPlan ExitPlanFor(const Row& v, bool reducedMotion) {
    ExitPlan p;
    if (reducedMotion) {   // short fades one after the other, nothing scales
        p.logoStart = static_cast<float>(OutroMs(true)); p.logoMs = 120; p.windowStart = p.logoStart + p.logoMs; p.windowMs = 120; p.browserStart = p.windowStart + p.windowMs; p.browserMs = 120;
        return p;
    }
    p.x1 = v[ExitX1]; p.y1 = v[ExitY1]; p.x2 = v[ExitX2]; p.y2 = v[ExitY2];
    p.logoStart = v[Hold]; p.logoMs = v[LogoDuration]; p.logoScale = v[LogoScale];
    p.windowStart = std::max(p.logoStart, p.logoStart + p.logoMs - v[WindowOverlap]); p.windowMs = v[WindowDuration]; p.windowScale = v[WindowScale];
    p.browserStart = std::max(p.windowStart, p.windowStart + p.windowMs - v[BrowserOverlap]); p.browserMs = v[BrowserDuration]; p.browserScale = v[BrowserScale];
    return p;
}
struct ExitFrame {
    float logoScale = 1, logoAlpha = 1;          // the logo inside the dot field (the dots stay)
    float windowScale = 1, windowAlpha = 1;      // the onboarding window
    float browserScale = 1, browserAlpha = 0;    // the browser window
    bool windowLeaving = false, browserEntering = false, done = false;
};
inline ExitFrame ExitAt(const ExitPlan& p, float ms) {
    ExitFrame f;
    auto ease = [&](float start, float length) { return Bezier(p.x1, p.y1, p.x2, p.y2, length > 0 ? std::min(1.f, std::max(0.f, (ms - start) / length)) : (ms >= start ? 1.f : 0.f)); };
    const float l = ease(p.logoStart, p.logoMs), w = ease(p.windowStart, p.windowMs), b = ease(p.browserStart, p.browserMs);
    f.logoScale = 1 + (p.logoScale - 1) * l; f.logoAlpha = 1 - l;
    f.windowLeaving = ms >= p.windowStart; f.windowScale = 1 + (p.windowScale - 1) * w; f.windowAlpha = 1 - w;
    f.browserEntering = ms >= p.browserStart; f.browserScale = p.browserScale + (1 - p.browserScale) * b; f.browserAlpha = b;
    f.done = ms >= p.End();
    return f;
}

} // namespace Tuning
} // namespace Onboarding
