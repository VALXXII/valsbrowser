#pragma once
// Pending site requests in the address bar (Paper p-D-0, B01 « Indicateurs — sécurité +
// permission », B02 « Après la décision »). Pure C++17: tests/site_request_test.cpp uses it.
//
// While a permission popover is open, the field shows the security slot (lock / warning,
// unchanged) followed by a « Puce permission » chip: #3E3E3E plate (--color-browser-pressed),
// height 24, radius 4 (--radius-browser-3), padding 0 5, glyphs 14 white, 4 apart; 2 after the
// security slot and 4 before the URL's own 4 gap. One glyph per requested kind, camera before
// microphone (B01 groups both). The chip lives exactly as long as the request is pending: no
// state is kept after the answer, because WebView2 exposes no verified capture state (« en
// cours » with the accent dot would need one), and a granted permission is not a capture.
#include <algorithm>
#include <cstddef>
#include <vector>

namespace SiteRequest {
// Phosphor glyphs exactly as exported from Paper (viewBox 0 0 256 256), shared with the popovers
// (modal.h DialogKit::Glyph aliases them: the chip and the popover draw the same shapes).
namespace Path {
inline constexpr const char* camera = "M251.77,73a12,12,0,0,0-12.33.32L204,95.58V72a20,20,0,0,0-20-20H32A20,20,0,0,0,12,72V184a20,20,0,0,0,20,20H184a20,20,0,0,0,20-20V160.42l35.44,22.15A12,12,0,0,0,256,172.4V83.6A12,12,0,0,0,251.77,73ZM180,180H36V76H180Zm52-29.65-28-17.5V123.15l28-17.5Z";
inline constexpr const char* microphone = "M128,180a52.06,52.06,0,0,0,52-52V64A52,52,0,0,0,76,64v64A52.06,52.06,0,0,0,128,180ZM100,64a28,28,0,0,1,56,0v64a28,28,0,0,1-56,0Zm40,155.22V240a12,12,0,0,1-24,0V219.22A92.14,92.14,0,0,1,36,128a12,12,0,0,1,24,0,68,68,0,0,0,136,0,12,12,0,0,1,24,0A92.14,92.14,0,0,1,140,219.22Z";
inline constexpr const char* position = "M128,60a44,44,0,1,0,44,44A44.05,44.05,0,0,0,128,60Zm0,64a20,20,0,1,1,20-20A20,20,0,0,1,128,124Zm0-112a92.1,92.1,0,0,0-92,92c0,77.36,81.64,135.4,85.12,137.83a12,12,0,0,0,13.76,0,259,259,0,0,0,42.18-39C205.15,170.57,220,136.37,220,104A92.1,92.1,0,0,0,128,12Zm31.3,174.71A249.35,249.35,0,0,1,128,216.89a249.35,249.35,0,0,1-31.3-30.18C80,167.37,60,137.31,60,104a68,68,0,0,1,136,0C196,137.31,176,167.37,159.3,186.71Z";
inline constexpr const char* bell = "M225.29,165.93C216.61,151,212,129.57,212,104a84,84,0,0,0-168,0c0,25.58-4.59,47-13.27,61.93A20.08,20.08,0,0,0,30.66,186,19.77,19.77,0,0,0,48,196H84.18a44,44,0,0,0,87.64,0H208a19.77,19.77,0,0,0,17.31-10A20.08,20.08,0,0,0,225.29,165.93ZM128,212a20,20,0,0,1-19.6-16h39.2A20,20,0,0,1,128,212ZM54.66,172C63.51,154,68,131.14,68,104a60,60,0,0,1,120,0c0,27.13,4.48,50,13.33,68Z";
inline constexpr const char* clipboard = "M200,28H163.94A51.89,51.89,0,0,0,92.06,28H56A20,20,0,0,0,36,48V216a20,20,0,0,0,20,20H200a20,20,0,0,0,20-20V48A20,20,0,0,0,200,28Zm-72,0a28,28,0,0,1,27.71,24H100.29A28,28,0,0,1,128,28Zm68,184H60V52H76.4A12,12,0,0,0,76,56V64a12,12,0,0,0,12,12h80a12,12,0,0,0,12-12V56a12,12,0,0,0-.4-4H196Z";
inline constexpr const char* info = "M108,84a16,16,0,1,1,16,16A16,16,0,0,1,108,84Zm128,44A108,108,0,1,1,128,20,108.12,108.12,0,0,1,236,128Zm-24,0a84,84,0,1,0-84,84A84.09,84.09,0,0,0,212,128Zm-72,36.68V132a20,20,0,0,0-20-20,12,12,0,0,0-4,23.32V168a20,20,0,0,0,20,20,12,12,0,0,0,4-23.32Z";
inline constexpr const char* file = "M216.49,79.52l-56-56A12,12,0,0,0,152,20H56A20,20,0,0,0,36,40V216a20,20,0,0,0,20,20H200a20,20,0,0,0,20-20V88A12,12,0,0,0,216.49,79.52ZM160,57l23,23H160ZM60,212V44h76V92a12,12,0,0,0,12,12h48V212Z";
inline constexpr const char* screen = "M208,36H48A28,28,0,0,0,20,64V176a28,28,0,0,0,28,28h68v16H96a12,12,0,0,0,0,24h64a12,12,0,0,0,0-24H140V204h68a28,28,0,0,0,28-28V64A28,28,0,0,0,208,36Zm4,140a4,4,0,0,1-4,4H48a4,4,0,0,1-4-4V64a4,4,0,0,1,4-4H208a4,4,0,0,1,4,4Z";
// Phosphor « database » (bold): persistent storage (COREWEBVIEW2_PERMISSION_KIND_PERSISTENT_STORAGE).
inline constexpr const char* storage = "M196,35.52C177.62,25.51,153.48,20,128,20S78.38,25.51,60,35.52C39.37,46.79,28,62.58,28,80v96c0,17.42,11.37,33.21,32,44.48,18.35,10,42.49,15.52,68,15.52s49.62-5.51,68-15.52c20.66-11.27,32-27.06,32-44.48V80C228,62.58,216.63,46.79,196,35.52ZM204,128c0,17-31.21,36-76,36s-76-19-76-36v-8.46a88.9,88.9,0,0,0,8,4.94c18.35,10,42.49,15.52,68,15.52s49.62-5.51,68-15.52a88.9,88.9,0,0,0,8-4.94ZM128,44c44.79,0,76,19,76,36s-31.21,36-76,36S52,97,52,80,83.21,44,128,44Zm0,168c-44.79,0-76-19-76-36v-8.46a88.9,88.9,0,0,0,8,4.94c18.35,10,42.49,15.52,68,15.52s49.62-5.51,68-15.52a88.9,88.9,0,0,0,8-4.94V176C204,193,172.79,212,128,212Z";
inline constexpr const char* download = "M228,144v64a12,12,0,0,1-12,12H40a12,12,0,0,1-12-12V144a12,12,0,0,1,24,0v52H204V144a12,12,0,0,1,24,0Zm-108.49,8.49a12,12,0,0,0,17,0l40-40a12,12,0,0,0-17-17L140,115V32a12,12,0,0,0-24,0v83L96.49,95.51a12,12,0,0,0-17,17Z";
}
// COREWEBVIEW2_PERMISSION_KIND values (no WebView2 header needed).
namespace Kind { inline constexpr int Microphone = 1, Camera = 2, Geolocation = 3, Notifications = 4, ClipboardRead = 6, MultipleDownloads = 7, FileReadWrite = 8, WindowManagement = 12, PersistentStorage = 13; }
// Same mapping as OriginPrompt::Text::Glyph (the popover's row / header glyph).
inline const char* GlyphOf(int kind) {
    switch (kind) {
    case Kind::Camera: return Path::camera; case Kind::Microphone: return Path::microphone;
    case Kind::Geolocation: return Path::position; case Kind::Notifications: return Path::bell;
    case Kind::ClipboardRead: return Path::clipboard; case Kind::MultipleDownloads: return Path::download;
    case Kind::FileReadWrite: return Path::file; case Kind::WindowManagement: return Path::screen;
    case Kind::PersistentStorage: return Path::storage;
    default: return Path::info;
    }
}
inline constexpr std::size_t maxGlyphs = 3;
// Glyphs of the chip: camera, then microphone, then the other kinds in request order; one per
// distinct glyph, at most maxGlyphs (a fourth kind is still named by the tooltip).
inline std::vector<const char*> Glyphs(const std::vector<int>& kinds) {
    std::vector<int> order;
    for (int k : kinds) if (k == Kind::Camera) order.push_back(k);
    for (int k : kinds) if (k == Kind::Microphone) order.push_back(k);
    for (int k : kinds) if (k != Kind::Camera && k != Kind::Microphone) order.push_back(k);
    std::vector<const char*> out;
    for (int k : order) { const char* g = GlyphOf(k); if (std::find(out.begin(), out.end(), g) == out.end() && out.size() < maxGlyphs) out.push_back(g); }
    return out;
}
// What the chip says (Paper B01 « Puce permission » and B02 « Après la décision »):
//   Pending  a popover is open for this request: #3E3E3E plate, white glyphs (DJ8-0);
//   Granted  the site may use it (verified: our own answer or the engine's saved setting), not
//            known to be in use: no plate, glyphs 70 %;
//   Active   in use right now (E4Q-0): split tint plate (accent 16 %), 6 px accent dot, white
//            glyphs 6 apart. Only ever set from a verified source: WebView2 has no camera /
//            microphone in-use signal, so in this build only the sandbox demonstration sets it;
//   Blocked  refused for this page (E5Z-0): glyph 70 % with a slash.
enum class State { Pending, Granted, Active, Blocked };
// Paper B01/B02 values (get_computed_styles DJ4-0 / DJ8-0 / E4Q-0), DIP.
namespace Layout {
inline constexpr float lead = 2, padX = 5, gap = 4, activeGap = 6, dot = 6, icon = 14, height = 24, radius = 4, trail = 4;
inline constexpr unsigned plate = 0x3E3E3E;   // --color-browser-pressed
inline constexpr float tint = .16f, dimmed = .7f;   // split tint over the field; Granted / Blocked glyphs
inline constexpr int enterMs = 150, exitMs = 120, stateMs = 120;   // motion (Paper popover timings)
}
inline float ChipWidth(std::size_t glyphs, State s = State::Pending) {
    if (!glyphs) return 0;
    const bool active = s == State::Active; const float g = active ? Layout::activeGap : Layout::gap;
    return 2 * Layout::padX + (active ? Layout::dot + g : 0) + glyphs * Layout::icon + (glyphs - 1) * g;
}
// How far the URL moves right when the chip shows (lead gap + chip + trailing padding).
inline float TextShift(std::size_t glyphs, State s = State::Pending) { return glyphs ? Layout::lead + ChipWidth(glyphs, s) + Layout::trail : 0; }
// Accessible / tooltip wording of a state.
inline const wchar_t* StateLabel(State s) {
    switch (s) { case State::Pending: return L"demande en attente de votre réponse"; case State::Granted: return L"autorisé";
    case State::Active: return L"en cours d'utilisation"; default: return L"bloqué"; }
}
// Which state the bar shows for the active tab: an open request wins; then what the page may
// use (Active only when a verified in-use source says so), then what it was refused.
struct Grants { std::vector<int> granted, active, blocked; };
inline bool Has(const std::vector<int>& v, int k) { return std::find(v.begin(), v.end(), k) != v.end(); }
inline void Add(std::vector<int>& v, int k) { if (!Has(v, k)) v.push_back(k); }
inline void Remove(std::vector<int>& v, int k) { v.erase(std::remove(v.begin(), v.end(), k), v.end()); }
// After an answer: granted kinds leave `blocked` and vice versa; nothing becomes Active here.
inline void Record(Grants& g, const std::vector<int>& kinds, bool allowed) {
    for (int k : kinds) { if (allowed) { Add(g.granted, k); Remove(g.blocked, k); } else { Add(g.blocked, k); Remove(g.granted, k); Remove(g.active, k); } }
}
// Active is only accepted for a kind that is granted (a verified use of a permission).
inline bool SetActive(Grants& g, int k, bool on) { if (on && !Has(g.granted, k)) return false; if (on) Add(g.active, k); else Remove(g.active, k); return true; }
inline void Revoke(Grants& g, int k) { Remove(g.granted, k); Remove(g.active, k); }
struct Shown { State state = State::Granted; std::vector<int> kinds; };
inline Shown Resolve(const Grants& g) {
    if (!g.active.empty()) return {State::Active, g.granted};
    if (!g.granted.empty()) return {State::Granted, g.granted};
    if (!g.blocked.empty()) return {State::Blocked, g.blocked};
    return {State::Granted, {}};
}
}
