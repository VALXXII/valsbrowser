#pragma once
#include "i18n.h"
// Per-site audio: what plays, how loud, and the toolbar icon that says so.
//
// Pure model (no WebView2, no window): the host feeds it the engine's audible flag
// (ICoreWebView2_8::IsDocumentPlayingAudio) and the reports of the injected page script
// (audio-script.h), and reads back rows for the panel, the toolbar icon state and the
// per-site gains to send to pages.
//
// Gain. WebView2 exposes no per-tab volume (only IsMuted), so the volume is applied
// inside the page by the injected script, as a browser-side multiplier the site cannot
// see: an <audio>/<video> element plays at (its own volume x the site gain) while its
// `volume` property keeps reporting the site's own value, and every Web Audio graph is
// routed through one hidden GainNode per AudioContext. 0..100 %, no amplification.
//
// Music vs short sounds. A site counts as playing music (animated icon) only with
// evidence: a playing media element that is long (>= 20 s) or live, a Media Session
// title, or sound sustained for 5 s (a game's music through Web Audio). Anything else
// audible is a short sound (notification, click, game effect): it reveals the static,
// subdued icon and never animates it. The icon keeps playing through gaps between
// tracks (1.5 s hold) and stays available, static, after the sound ends, so the
// site's volume remains one click away.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <istream>
#include <map>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>
#include "motion.h"
#include "address.h"

namespace NativeChrome { namespace Audio {
using Motion::Time;
using Motion::Milliseconds;

// ---- Thresholds (ms) --------------------------------------------------------------------
inline constexpr double MediaSeconds = 20;          // a playing element at least this long is music/video
inline constexpr long long SustainedMs = 5000;      // continuous sound this long counts as music
inline constexpr long long MusicHoldMs = 1500;      // the icon keeps playing through a gap this short
inline constexpr long long ForgetMs = 10 * 60000;   // a silent short-sound entry at default volume is dropped
inline constexpr int DefaultPercent = 100, Step = 1, WheelStep = 5, PageStep = 10; // arrows 1 %: the value is continuous, the ticks only picture it

inline int ClampPercent(int v) { return std::clamp(v, 0, 100); }
inline long long Ms(Time from, Time to) { return std::chrono::duration_cast<Milliseconds>(to - from).count(); }

// ---- Per-site volumes (persisted beside `session`) ------------------------------------------
// One line per site that is not at 100 %: "<site>\t<percent>\n" (UTF-8). Unknown or
// malformed lines are skipped; at most 500 sites are kept (newest writes win).
class Volumes {
    std::map<std::wstring, int> percent_;
public:
    static constexpr size_t Limit = 500;
    int Get(const std::wstring& site) const { auto it = percent_.find(site); return it == percent_.end() ? DefaultPercent : it->second; }
    float Gain(const std::wstring& site) const { return Get(site) / 100.f; }
    // True when the stored value changed.
    bool Set(const std::wstring& site, int value) {
        if (site.empty()) return false;
        value = ClampPercent(value);
        const int before = Get(site);
        if (value == DefaultPercent) percent_.erase(site); else if (percent_.size() < Limit || percent_.count(site)) percent_[site] = value; else return false;
        return before != value;
    }
    bool Custom(const std::wstring& site) const { return percent_.count(site) != 0; }
    size_t Size() const { return percent_.size(); }
    const std::map<std::wstring, int>& All() const { return percent_; }
    void Write(std::ostream& out) const { for (auto& [site, v] : percent_) out << Downloads::ToUtf8(site) << '\t' << v << '\n'; }
    void Read(std::istream& in) {
        percent_.clear(); std::string line;
        while (std::getline(in, line) && percent_.size() < Limit) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const auto tab = line.find('\t'); if (tab == std::string::npos || tab == 0) continue;
            std::wstring site; if (!Downloads::FromUtf8(line.substr(0, tab), site)) continue;
            if (site.find_first_of(L"\"\\'<> \t") != std::wstring::npos || site != AddressBar::Lower(site)) continue;
            char* end = nullptr; const long v = std::strtol(line.c_str() + tab + 1, &end, 10);
            if (end == line.c_str() + tab + 1 || *end) continue;
            if (v >= 0 && v < DefaultPercent) percent_[site] = static_cast<int>(v);
        }
    }
};

// Site keys are AddressBar::SiteOf: lower-case host without "www." (the script uses the same rule).
inline std::wstring SiteKey(const std::wstring& uri) { return AddressBar::SiteOf(uri); }

// ---- Page reports -----------------------------------------------------------------------
// The injected script posts a flat JSON object, e.g.
//   {"lisiereAudio":1,"p":1,"d":215.3,"l":0,"w":0,"s":2,"t":"Title","a":"Artist","b":"Album","i":"https://…/art.jpg","v":0}
// p: media elements playing (any frame) · d: longest playing duration (s; -1 unknown)
// l: a playing element is live (infinite duration) · w: running AudioContexts
// s: Media Session (0 none, 1 paused, 2 playing) · t/a/b: metadata · i: artwork URL
// v: playing elements that have video.
struct Report {
    int playing = 0, webAudio = 0, video = 0, session = 0;
    double longest = -1; bool live = false;
    std::wstring title, artist, album, artwork;
    bool operator==(const Report& o) const { return playing == o.playing && webAudio == o.webAudio && video == o.video && session == o.session && longest == o.longest && live == o.live && title == o.title && artist == o.artist && album == o.album && artwork == o.artwork; }
    bool operator!=(const Report& o) const { return !(*this == o); }
};
namespace Detail {
inline void Skip(const std::wstring& s, size_t& i) { while (i < s.size() && (s[i] == L' ' || s[i] == L'\t' || s[i] == L'\r' || s[i] == L'\n')) ++i; }
inline bool String(const std::wstring& s, size_t& i, std::wstring& out) {
    out.clear(); if (i >= s.size() || s[i] != L'"') return false; ++i;
    while (i < s.size()) {
        wchar_t c = s[i++];
        if (c == L'"') return true;
        if (c != L'\\') { out += c; continue; }
        if (i >= s.size()) return false;
        c = s[i++];
        switch (c) {
        case L'"': case L'\\': case L'/': out += c; break;
        case L'b': out += L'\b'; break; case L'f': out += L'\f'; break; case L'n': out += L'\n'; break; case L'r': out += L'\r'; break; case L't': out += L'\t'; break;
        case L'u': { if (i + 4 > s.size()) return false; unsigned v = 0; for (int k = 0; k < 4; ++k) { const wchar_t h = s[i++]; v <<= 4; if (h >= L'0' && h <= L'9') v |= h - L'0'; else if (h >= L'a' && h <= L'f') v |= h - L'a' + 10; else if (h >= L'A' && h <= L'F') v |= h - L'A' + 10; else return false; } out += static_cast<wchar_t>(v); break; }
        default: return false;
        }
    }
    return false;
}
// Display text: control characters become spaces, runs collapse, ends trimmed, capped.
inline std::wstring Clean(const std::wstring& s, size_t limit = 200) {
    std::wstring out; bool space = false;
    for (wchar_t c : s) { if (c < 32 || c == 0x7f || c == 0x2028 || c == 0x2029) c = L' '; if (c == L' ') { space = !out.empty(); continue; } if (space) out += L' '; space = false; out += c; if (out.size() >= limit) break; }
    return out;
}
}
inline bool IsReport(const std::wstring& message) { return message.rfind(L"{\"lisiereAudio\":", 0) == 0; }
inline bool ParseReport(const std::wstring& m, Report& r) {
    if (!IsReport(m) || m.size() > 16384) return false;
    r = Report{}; size_t i = 1;
    while (true) {
        Detail::Skip(m, i); std::wstring key; if (!Detail::String(m, i, key)) return false;
        Detail::Skip(m, i); if (i >= m.size() || m[i] != L':') return false; ++i; Detail::Skip(m, i);
        if (i < m.size() && m[i] == L'"') {
            std::wstring value; if (!Detail::String(m, i, value)) return false;
            if (key == L"t") r.title = Detail::Clean(value); else if (key == L"a") r.artist = Detail::Clean(value); else if (key == L"b") r.album = Detail::Clean(value);
            else if (key == L"i") { const auto url = Detail::Clean(value, 2048); if (url.rfind(L"https://", 0) == 0 && url.find_first_of(L" \"<>\\") == std::wstring::npos) r.artwork = url; }
        } else {
            const wchar_t* start = m.c_str() + i; wchar_t* end = nullptr; const double v = std::wcstod(start, &end);
            if (end == start) { if (m.compare(i, 4, L"null") == 0) { i += 4; } else return false; }
            else {
                i += static_cast<size_t>(end - start);
                const bool finite = std::isfinite(v);
                auto count = [&](int& out) { out = finite ? static_cast<int>(std::clamp(v, 0.0, 1000.0)) : 0; };
                if (key == L"p") count(r.playing); else if (key == L"w") count(r.webAudio); else if (key == L"v") count(r.video);
                else if (key == L"s") r.session = finite ? std::clamp(static_cast<int>(v), 0, 2) : 0;
                else if (key == L"l") r.live = finite && v != 0;
                else if (key == L"d") r.longest = finite && v >= 0 ? std::min(v, 1e7) : -1;
            }
        }
        Detail::Skip(m, i); if (i >= m.size()) return false;
        if (m[i] == L'}') return true;
        if (m[i] != L',') return false;
        ++i;
    }
}

// ---- One tab's audio --------------------------------------------------------------------
enum class Kind { Silent, Effect, Media };
struct TabAudio {
    std::wstring site;
    bool audible = false, muted = false, heard = false; // heard: produced sound since it reached this site
    Time audibleSince{}, lastAudible{}, firstHeard{};
    Report report;
    // Evidence of music/video regardless of whether sound is coming out right now.
    bool MediaEvidence() const { return (report.playing > 0 && (report.live || report.longest >= MediaSeconds)) || report.session == 2 || (report.playing > 0 && report.video > 0 && report.longest < 0); }
    bool Sustained(Time now) const { return audible && Ms(audibleSince, now) >= SustainedMs; }
    Kind At(Time now) const { if (!audible) return Kind::Silent; return MediaEvidence() || Sustained(now) ? Kind::Media : Kind::Effect; }
    // A short-sound entry nobody touched fades from the toolbar after a long silence.
    bool Forgettable(Time now, bool customVolume) const { return !audible && !customVolume && !MediaEvidence() && report.session == 0 && Ms(lastAudible, now) >= ForgetMs; }
    // Engine flag changed. Returns true when the visible state may change.
    bool SetAudible(bool value, Time now) {
        if (value == audible) return false;
        audible = value;
        if (value) { audibleSince = now; if (!heard) { heard = true; firstHeard = now; } }
        lastAudible = now; return true;
    }
    // The tab left for another site (or a non-web page): start over.
    void SiteChanged(const std::wstring& next, Time now) {
        if (next == site) return;
        site = next; report = Report{};
        heard = audible; if (audible) { firstHeard = audibleSince = now; } lastAudible = now;
    }
    // Next instant At() can change on its own (the sustained rule), for the host's timer.
    Motion::Wake NextWake(Time now) const {
        if (audible && !MediaEvidence() && Ms(audibleSince, now) < SustainedMs) return Motion::Deadline(now, audibleSince + Milliseconds(SustainedMs));
        return {};
    }
};

// ---- Toolbar state (all tabs) ---------------------------------------------------------------
// `music` is debounced: on as soon as a tab plays music, off only after MusicHoldMs of none.
class Toolbar {
    bool music_ = false, raw_ = false; Time lastMusic_{};
public:
    bool Music() const { return music_; }
    void Update(bool anyMusic, Time now) {
        if (!anyMusic && raw_) lastMusic_ = now; // music lasted until this update
        raw_ = anyMusic;
        if (anyMusic) { music_ = true; lastMusic_ = now; }
        else if (music_ && Ms(lastMusic_, now) >= MusicHoldMs) music_ = false;
    }
    Motion::Wake NextWake(Time now) const { return music_ && !raw_ ? Motion::Deadline(now, lastMusic_ + Milliseconds(MusicHoldMs)) : Motion::Wake{}; }
};

// ---- Panel rows -------------------------------------------------------------------------
// One row per site, in the order sites were first heard (stable: rows never jump while
// the panel is open). The row speaks for the site's loudest evidence: a tab playing
// music wins over one that only clicked.
struct Row {
    std::wstring site, title, detail, artist, artwork, status;
    int percent = DefaultPercent, tabs = 0, tab = -1; // tab: index of the tab the row opens
    Kind kind = Kind::Silent; bool playingMedia = false, muted = false, video = false;
};
struct TabView { const TabAudio* audio = nullptr; std::wstring title; bool active = false; };
inline std::wstring StatusText(Kind kind, bool media, bool muted) {
    if (muted) return TR(audio_status_muted_background);
    if (kind == Kind::Media) return L"";
    if (kind == Kind::Effect) return TR(audio_status_short_sounds);
    return media ? TR(audio_status_paused) : TR(audio_status_silent);
}
inline std::vector<Row> Rows(const std::vector<TabView>& tabs, const Volumes& volumes, Time now) {
    struct Pick { int tab; int score; Time first; };
    std::map<std::wstring, Pick> best; std::map<std::wstring, int> counts;
    for (int i = 0; i < static_cast<int>(tabs.size()); ++i) {
        const auto* a = tabs[i].audio; if (!a || !a->heard || a->site.empty()) continue;
        const Kind k = a->At(now);
        const int score = (k == Kind::Media ? 40 : k == Kind::Effect ? 30 : 0) + (a->MediaEvidence() || a->report.session ? 10 : 0) + (tabs[i].active ? 1 : 0);
        ++counts[a->site];
        auto it = best.find(a->site);
        if (it == best.end()) best[a->site] = {i, score, a->firstHeard};
        else { if (score > it->second.score) { it->second.tab = i; it->second.score = score; } it->second.first = std::min(it->second.first, a->firstHeard); }
    }
    std::vector<std::pair<Time, Row>> ordered;
    for (auto& [site, pick] : best) {
        const auto& t = tabs[pick.tab]; const auto& a = *t.audio; const auto& r = a.report;
        Row row; row.site = site; row.tab = pick.tab; row.tabs = counts[site]; row.percent = volumes.Get(site);
        row.kind = a.At(now); row.playingMedia = r.playing > 0 || r.session == 2; row.muted = a.muted; row.video = r.video > 0;
        row.title = !r.title.empty() ? r.title : !t.title.empty() ? Detail::Clean(t.title) : site;
        row.artwork = r.artwork;
        std::wstring detail = r.artist; if (!r.title.empty() && detail.empty() && !r.album.empty()) detail = r.album;
        row.artist = detail;
        row.detail = detail.empty() ? site : detail + L" · " + site;
        row.status = StatusText(row.kind, a.MediaEvidence() || r.session != 0, a.muted && a.audible);
        if (row.tabs > 1) row.detail += L" · " + TRP(audio_tabs_count, row.tabs);
        ordered.push_back({pick.first, row});
    }
    std::stable_sort(ordered.begin(), ordered.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
    std::vector<Row> rows; for (auto& o : ordered) rows.push_back(std::move(o.second));
    return rows;
}
inline std::wstring PercentText(int percent) { return TRF(audio_percent, std::to_wstring(ClampPercent(percent))); }
// Keyboard: arrows 1 %, Page keys 10 %, Home/End 0 and 100 %. Returns the new value.
inline int KeyStep(int percent, int key /* VK_* */) {
    switch (key) {
    case 0x25: case 0x28: return ClampPercent(percent - Step);      // Left, Down
    case 0x27: case 0x26: return ClampPercent(percent + Step);      // Right, Up
    case 0x22: return ClampPercent(percent - PageStep);             // Page Down
    case 0x21: return ClampPercent(percent + PageStep);             // Page Up
    case 0x24: return 0;                                            // Home
    case 0x23: return 100;                                          // End
    }
    return percent;
}
// Mute toggles to 0 and back to what it was (or 100 % when it was already 0).
inline int ToggleMute(int percent, int remembered) { return percent > 0 ? 0 : (remembered > 0 ? remembered : DefaultPercent); }

// Accessible label of the toolbar button: what plays, never only "audio".
inline std::wstring ButtonName(const std::vector<Row>& rows, bool music) {
    if (rows.empty()) return TR(chrome_tip_audio);
    int playing = 0; for (auto& r : rows) if (r.kind != Kind::Silent) ++playing;
    if (music) { for (auto& r : rows) if (r.kind == Kind::Media) return TRF(audio_button_music, r.title, r.site); }
    if (playing) return TRP(audio_button_short_sounds, playing);
    return TRP(audio_button_recent_silent, rows.size());
}

// ---- Toolbar icon motion ------------------------------------------------------------------
// One Lottie (design/lottie/audio-meter.py): 0..72 the meter loop (frame 0 = Phosphor
// speaker-simple-high), 72..96 the outro to speaker-simple-low; backwards it is the intro.
namespace Icon {
inline constexpr int Loop = 72, Out = 24, Frames = Loop + Out + 1; // 97
inline constexpr double Fps = 60;
// The loop's 72 frames play over LoopMs (a playback speed, not part of the file): 5 s, one
// frame every ~69 ms. First version: 1.2 s (user feedback: too strong and fast). Values chosen
// by the user in the preview's debug mode (2026-11-12): this, plus the asset's amplitude 0.3,
// 5 beats, outer bar 70 % of half a beat late, ease 0.4 (design/lottie/audio-meter.py).
inline constexpr long long LoopMs = 5000;
inline constexpr float Subdued = .76f;            // x the toolbar ink (white 50 %) = 38 %: 3.6:1 on the canvas
inline constexpr long long OutMs = static_cast<long long>(Out * 1000 / Fps);     // 400
inline constexpr long long FadeMs = 150;          // the button's first appearance
}
struct IconPose {
    int frame = -1;         // Lottie frame, -1 = static glyph
    bool high = false;      // static glyph: speaker-simple-high (music, reduced motion) or -low
    float emphasis = Icon::Subdued; // multiplier of the toolbar ink
    float presence = 1;     // first appearance fade
};
class IconModel {
    enum class Mode { Rest, Morph, Loop, Finish };
    Mode mode_ = Mode::Rest;
    bool music_ = false, reduced_ = false, shown_ = false;
    float from_ = 1; int target_ = 1;   // outro position 0 (frame 72, high) .. 1 (frame 96, low)
    Time morphStart_{}, loopStart_{}, shownAt_{};
    float MorphAt(Time now) const {
        const float d = static_cast<float>(std::chrono::duration<double, std::milli>(now - morphStart_).count() / Icon::OutMs);
        return target_ ? std::min(1.f, from_ + d) : std::max(0.f, from_ - d);
    }
    Time MorphEnd() const { return morphStart_ + Milliseconds(static_cast<long long>(std::ceil((target_ ? 1 - from_ : from_) * Icon::OutMs))); }
    Time CycleEnd(Time now) const { const auto k = Ms(loopStart_, now) / Icon::LoopMs + 1; return loopStart_ + Milliseconds(k * Icon::LoopMs); }
    void Morph(float from, int target, Time now) { mode_ = Mode::Morph; from_ = from; target_ = target; morphStart_ = now; }
public:
    bool Music() const { return music_; }
    bool Animating() const { return mode_ != Mode::Rest; }
    void Show(bool shown, Time now) { if (shown && !shown_) shownAt_ = now; shown_ = shown; }
    // Transitions that only time drives (morph done, cycle end).
    void Settle(Time now) {
        if (mode_ == Mode::Morph && now >= MorphEnd()) { if (target_) mode_ = Mode::Rest; else { mode_ = Mode::Loop; loopStart_ = MorphEnd(); } }
        if (mode_ == Mode::Finish && now >= finishAt_) { Morph(0, 1, finishAt_); Settle(now); }
    }
    void Update(bool music, Time now, bool reduced) {
        Settle(now);
        if (reduced != reduced_) { reduced_ = reduced; music_ = music; mode_ = !reduced && music ? Mode::Loop : Mode::Rest; loopStart_ = now; return; }
        if (music == music_) return;
        music_ = music;
        if (reduced_) { mode_ = Mode::Rest; return; }
        if (music) {
            if (mode_ == Mode::Finish) mode_ = Mode::Loop;                       // still on the loop: nothing to show
            else if (mode_ == Mode::Morph) Morph(MorphAt(now), 0, now);          // reverse from where it is
            else Morph(1, 0, now);                                               // intro from the static glyph
        } else {
            if (mode_ == Mode::Loop) { mode_ = Mode::Finish; finishAt_ = CycleEnd(now); } // end the cycle at rest
            else if (mode_ == Mode::Morph) Morph(MorphAt(now), 1, now);
        }
    }
    IconPose At(Time now) const {
        IconPose p; p.presence = shown_ ? (reduced_ ? 1.f : Motion::Clamp(static_cast<float>(Ms(shownAt_, now)) / Icon::FadeMs)) : 0.f;
        if (reduced_) { p.high = music_; p.emphasis = music_ ? 1.f : Icon::Subdued; return p; }
        auto loopFrame = [&](Time t) { const long long ms = std::max(0LL, Ms(loopStart_, t)) % Icon::LoopMs; return static_cast<int>(ms * Icon::Loop / Icon::LoopMs); };
        auto morph = [&](float u) { p.frame = Icon::Loop + static_cast<int>(std::lround(u * Icon::Out)); p.emphasis = 1 - (1 - Icon::Subdued) * u; };
        switch (mode_) {
        case Mode::Rest: p.high = false; p.emphasis = Icon::Subdued; break;
        case Mode::Morph: { if (now >= MorphEnd()) { if (target_) { p.emphasis = Icon::Subdued; break; } p.frame = 0; p.emphasis = 1; break; } morph(MorphAt(now)); break; }
        case Mode::Loop: p.frame = loopFrame(now); p.emphasis = 1; break;
        case Mode::Finish: if (now >= finishAt_) { const float u = std::min(1.f, static_cast<float>(Ms(finishAt_, now)) / Icon::OutMs); if (u >= 1) { p.emphasis = Icon::Subdued; break; } morph(u); } else { p.frame = loopFrame(now); p.emphasis = 1; } break;
        }
        return p;
    }
    Motion::Wake NextWake(Time now) const {
        Motion::Wake w;
        if (shown_ && !reduced_ && Ms(shownAt_, now) < Icon::FadeMs) w = Motion::Frame();
        if (mode_ == Mode::Morph) w = Motion::Earlier(w, Motion::Frame());
        else if (mode_ == Mode::Loop || mode_ == Mode::Finish) {
            // Next 30 fps step of the loop (or the outro, at frame rate).
            if (mode_ == Mode::Finish && now >= finishAt_) w = Motion::Earlier(w, Motion::Frame());
            else { // next frame of the loop: frame f starts at f * LoopMs / Loop
                const long long ms = std::max(0LL, Ms(loopStart_, now)), cycle = ms / Icon::LoopMs * Icon::LoopMs, in = ms - cycle;
                const long long f = in * Icon::Loop / Icon::LoopMs + 1, next = cycle + (f * Icon::LoopMs + Icon::Loop - 1) / Icon::Loop;
                w = Motion::Earlier(w, Motion::Wake{Milliseconds(std::max(1LL, next - ms)), true}); }
        }
        return w;
    }
private:
    Time finishAt_{};
};
}} // namespace NativeChrome::Audio
