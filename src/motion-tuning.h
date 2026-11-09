#pragma once
// Motion lab parameters: every knob maps to one real input of a native animation model
// (nav-motion.h, lottie-playback.h, download-icon.h). Bounded, stepped, defaulting to the
// shipped values. Pure data + text (de)serialisation; no Win32 here, so it is unit tested.
// The normal product never changes these: only `--motion-lab` opens the panel that does.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>

namespace NativeChrome { namespace Tuning {
enum Id {
    BackPeak, BackResponse, BackDamping,
    ForwardPeak, ForwardResponse, ForwardDamping,
    RefreshTurn, StopMorph, StopTurn, RefreshPreview,
    CopySpeed, CopyDwell,
    DownloadSpeed, DownloadRipple,
    Count
};
enum Group { Back, Forward, Refresh, Copy, Download, Groups };
struct Param {
    Id id; Group group; const char* key; const wchar_t* label; const wchar_t* unit;
    float min, max, def, step; int decimals;
};
inline const Param* Params() {
    static const Param p[Count] = {
        {BackPeak, Back, "back.peak", L"Amplitude", L"DIP", 0, 8, 3, .25f, 2},
        {BackResponse, Back, "back.response", L"Réponse", L"s", .15f, 1, .4f, .01f, 2},
        {BackDamping, Back, "back.damping", L"Amortissement", L"", .5f, 1, 1, .01f, 2},
        {ForwardPeak, Forward, "forward.peak", L"Amplitude", L"DIP", 0, 8, 3, .25f, 2},
        {ForwardResponse, Forward, "forward.response", L"Réponse", L"s", .15f, 1, .4f, .01f, 2},
        {ForwardDamping, Forward, "forward.damping", L"Amortissement", L"", .5f, 1, 1, .01f, 2},
        {RefreshTurn, Refresh, "refresh.turn_ms", L"Durée du tour", L"ms", 250, 1500, 500, 10, 0},
        {StopMorph, Refresh, "refresh.morph_ms", L"Morph vers la croix", L"ms", 0, 600, 240, 10, 0},
        {StopTurn, Refresh, "refresh.morph_deg", L"Rotation du morph", L"°", 0, 180, 90, 5, 0},
        {RefreshPreview, Refresh, "refresh.preview_ms", L"Chargement simulé (aperçu)", L"ms", 300, 4000, 1200, 50, 0},
        {CopySpeed, Copy, "copy.speed", L"Vitesse du morph", L"×", .5f, 2, 1, .05f, 2},
        {CopyDwell, Copy, "copy.dwell_ms", L"Maintien de la coche", L"ms", 300, 3000, 1500, 50, 0},
        {DownloadSpeed, Download, "download.speed", L"Vitesse", L"×", .5f, 2, 1, .05f, 2},
        {DownloadRipple, Download, "download.ripple_ms", L"Durée de l'onde", L"ms", 200, 1500, 600, 25, 0},
    };
    return p;
}
inline const Param& Spec(Id id) { return Params()[id]; }
inline const wchar_t* GroupName(Group g) {
    switch (g) { case Back: return L"Précédent"; case Forward: return L"Suivant"; case Refresh: return L"Actualiser / Arrêter";
    case Copy: return L"Copier le lien"; case Download: return L"Téléchargements"; default: return L""; }
}
inline float Snap(Id id, float v) {
    const auto& p = Spec(id);
    if (!std::isfinite(v)) v = p.def;
    v = std::clamp(v, p.min, p.max);
    const float steps = std::round((v - p.min) / p.step);
    return std::clamp(p.min + steps * p.step, p.min, p.max);
}
class Values {
    float v_[Count];
public:
    Values() { Reset(); }
    void Reset() { for (int i = 0; i < Count; ++i) v_[i] = Params()[i].def; }
    float operator[](Id id) const { return v_[id]; }
    void Set(Id id, float v) { v_[id] = Snap(id, v); }
    bool IsDefault() const { for (int i = 0; i < Count; ++i) if (v_[i] != Params()[i].def) return false; return true; }
    // `key=value` lines, '#' comments. Unknown keys and bad numbers are ignored; values clamp.
    std::string Serialize() const {
        std::ostringstream out; out << "# browser motion lab (--motion-lab)\n";
        for (int i = 0; i < Count; ++i) { char n[32]; std::snprintf(n, sizeof n, "%.*f", Params()[i].decimals, v_[i]); out << Params()[i].key << '=' << n << '\n'; }
        return out.str();
    }
    int Parse(const std::string& text) {
        int applied = 0; std::istringstream in(text); std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            const auto eq = line.find('='); if (eq == std::string::npos) continue;
            const std::string key = line.substr(0, eq), value = line.substr(eq + 1);
            char* end = nullptr; const float f = std::strtof(value.c_str(), &end);
            if (end == value.c_str()) continue;
            for (int i = 0; i < Count; ++i) if (key == Params()[i].key) { Set(static_cast<Id>(i), f); ++applied; }
        }
        return applied;
    }
};
// Process-wide, like Motion::appReducedMotion: the models read it when a segment starts.
inline Values& Current() { static Values values; return values; }
}} // namespace NativeChrome::Tuning
