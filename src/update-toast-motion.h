#pragma once
// Pure model of the native update toast: Paper geometry, texts, formatting, click
// arbitration and presentation-value motion. No HWND, Direct2D or clock access, so
// everything here is deterministic and unit-tested (tests/update-toast).
#include "update-types.h"
#include "motion.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace BrowserUpdate { namespace ToastModel {
using Time = NativeChrome::Motion::Time;
using Ms = std::chrono::milliseconds;

// Exact Paper values (file "Browser Design", artboards New update download /
// downloading / install / installed). DIPs; opacities 0..1.
namespace Paper {
inline constexpr float width = 269, border = 1, radius = 10, padX = 16, padY = 16, gap = 16;
// Paper text frames are 0.40625 DIP shorter than their line box; card heights below depend on it.
inline constexpr float textTrim = .40625f;
inline constexpr float titleRow = 16, titleIcon = 16, titleGap = 6, titleSize = 16, titleLine = 14, titleTracking = -.01f;
inline constexpr float bodySize = 16, bodyLine = 20, bodyTracking = .01f;
inline constexpr float textOpacity = (128 / 255.f) * .9f;  // #FFFFFF80 at opacity .9
inline constexpr float iconOpacity = .5f;                   // --opacity-browser-secondary
inline constexpr float buttonHeight = 32, buttonRadius = 6, buttonPadX = 8, buttonGap = 8;
inline constexpr float buttonSize = 14, buttonTracking = -.01f, buttonTextOpacity = .5f;
inline constexpr float buttonSurface = 41 / 255.f;          // #292929
inline constexpr float statIcon = 14, statGap = 4, statSize = 14, statTracking = -.01f;
inline constexpr float statsRow = 14 - textTrim, statsGap = 8, track = 4;
inline constexpr float trackOpacity = .10f, fillOpacity = .5f; // --opacity-browser-focus / -secondary
inline constexpr float surface = 23 / 255.f, surfaceOpacity = .5f, borderOpacity = 13 / 255.f; // #171717 50%, #FFFFFF0D
inline constexpr float edgeLeft = 14, edgeBottom = 14.447f;
inline constexpr float available = 153.59375f, downloading = 139.59375f, ready = 153.59375f, installed = 105.59375f;
}
// Opaque fallback when desktop acrylic is unavailable or transparency is disabled.
// The live acrylic surface uses Paper::surfaceOpacity over the compositor's blur.
inline constexpr float MaterialOpacity = .98f;
// Card snapped to whole DIPs at the bottom edge; Paper's 14.447 is a canvas artefact.
inline constexpr float EdgeBottom = 14;
// Narrowest card that keeps every title on one line (measured: "New update available").
inline constexpr float MinWidth = 224;

enum class Kind { None, Available, Downloading, Ready, Installing, Installed, Error };
enum class Glyph { Info, Download, CheckCircle, ArrowDown, Timer };
struct Button { std::wstring label; Action action; };
struct Content {
    Kind kind = Kind::None;
    Glyph icon = Glyph::Info;
    std::wstring title, body;
    bool stats = false;           // Downloading row: sizes, ETA and track
    std::wstring size, eta;       // eta empty = unknown
    float progress = -1;          // -1 = unknown total (no fill)
    std::vector<Button> buttons;
};

inline std::wstring Trim0(double value) {
    wchar_t text[32];
    std::swprintf(text, 32, L"%.1f", value);
    std::wstring s = text;
    if (s.size() > 2 && s.compare(s.size() - 2, 2, L".0") == 0) s.resize(s.size() - 2);
    return s;
}
// "50.4/200MB" (Paper). Binary megabytes, as Windows Explorer labels them. Units
// switch to GB from 1000 MB on the total so the row stays short.
inline std::wstring FormatSize(std::uint64_t received, std::uint64_t total) {
    const double mb = 1024.0 * 1024.0, gb = mb * 1024.0;
    const bool giga = (total ? total : received) >= 1000 * mb;
    const double unit = giga ? gb : mb;
    const wchar_t* suffix = giga ? L"GB" : L"MB";
    if (!total) return Trim0(received / unit) + suffix;
    if (received > total) received = total;
    return Trim0(received / unit) + L"/" + Trim0(total / unit) + suffix;
}
// "00:14" (Paper). Unknown (negative / NaN) => empty, never invented.
inline std::wstring FormatEta(double seconds) {
    if (!(seconds >= 0)) return {};
    long long s = (long long)std::ceil(seconds);
    if (s > 99 * 3600) s = 99 * 3600;
    wchar_t text[32];
    if (s >= 3600) std::swprintf(text, 32, L"%lld:%02lld:%02lld", s / 3600, (s / 60) % 60, s % 60);
    else std::swprintf(text, 32, L"%02lld:%02lld", s / 60, s % 60);
    return text;
}
inline std::wstring ClipError(std::wstring text) {
    for (auto& c : text) if (c == L'\r' || c == L'\n' || c == L'\t') c = L' ';
    if (text.size() > 140) { text.resize(139); text += L"…"; }
    return text;
}
inline Content Describe(const View& v) {
    Content c;
    switch (v.phase) {
    case Phase::Hidden: case Phase::Checking: return c;
    case Phase::Available:
        c = {Kind::Available, Glyph::Info, L"New update available", L"A new update is ready to be downloaded."};
        c.buttons = {{L"Download", Action::Download}}; return c;
    case Phase::Downloading:
        c = {Kind::Downloading, Glyph::Download, L"Downloading...", L""};
        c.stats = true; c.size = FormatSize(v.received, v.total); c.eta = FormatEta(v.secondsRemaining);
        c.progress = v.total ? (float)std::min(1.0, (double)v.received / (double)v.total) : -1.f;
        c.buttons = {{L"Cancel", Action::Cancel}}; return c;
    case Phase::Ready:
        c = {Kind::Ready, Glyph::Info, L"Ready for install", L"The update is ready to be installed."};
        c.buttons = {{L"Restart and install", Action::RestartInstall}}; return c;
    case Phase::Installing:  // extension: no Paper board; Installed layout, no action.
        c = {Kind::Installing, Glyph::Download, L"Installing...", L"The update is being installed."}; return c;
    case Phase::Installed:
        c = {Kind::Installed, Glyph::CheckCircle, L"Update installed", L"The new update has been successfully installed."}; return c;
    case Phase::Error:       // extension: Paper visual language, two-button row (Paper row gap 8).
        c = {Kind::Error, Glyph::Info, L"Update failed",
             v.error.empty() ? std::wstring(L"The update could not be completed.") : ClipError(v.error)};
        c.buttons = {{L"Dismiss", Action::Dismiss}, {L"Retry", Action::Retry}}; return c;
    }
    return c;
}

struct Rect { float left = 0, top = 0, right = 0, bottom = 0;
    float Width() const { return right - left; } float Height() const { return bottom - top; }
    bool Contains(float x, float y) const { return x >= left && x < right && y >= top && y < bottom; } };
// Card-local geometry, y down from the card's top edge (border box).
struct Geometry {
    float width = Paper::width, height = 0;
    Rect title, icon, body, stats, track;
    std::vector<Rect> buttons;
};
inline float BodyHeight(int lines) { return lines > 0 ? lines * Paper::bodyLine - Paper::textTrim : 0; }
inline float ContentWidth(float width) { return width - 2 * (Paper::border + Paper::padX); }
// bodyLines comes from the renderer's balanced DirectWrite layout at ContentWidth().
inline Geometry Layout(const Content& c, float width, int bodyLines) {
    Geometry g; g.width = width;
    const float x = Paper::border + Paper::padX, r = width - x;
    float y = Paper::border + Paper::padY;
    g.icon = {x, y, x + Paper::titleIcon, y + Paper::titleRow};
    g.title = {x + Paper::titleIcon + Paper::titleGap, y, r, y + Paper::titleRow};
    y += Paper::titleRow;
    if (!c.body.empty() && bodyLines > 0) { y += Paper::gap; g.body = {x, y, r, y + BodyHeight(bodyLines)}; y = g.body.bottom; }
    if (c.stats) {
        y += Paper::gap; g.stats = {x, y, r, y + Paper::statsRow};
        g.track = {x, g.stats.bottom + Paper::statsGap, r, g.stats.bottom + Paper::statsGap + Paper::track};
        y = g.track.bottom;
    }
    if (!c.buttons.empty()) {
        y += Paper::gap;
        const float n = (float)c.buttons.size(), w = (r - x - Paper::buttonGap * (n - 1)) / n;
        for (size_t i = 0; i < c.buttons.size(); ++i) {
            const float l = x + i * (w + Paper::buttonGap);
            g.buttons.push_back({l, y, l + w, y + Paper::buttonHeight});
        }
        y += Paper::buttonHeight;
    }
    g.height = y + Paper::padY + Paper::border;
    return g;
}
// Card width for an owner client (DIP); 0 = does not fit (card hidden, state kept).
inline float FitWidth(float clientWidth, float edgeLeft, float reservedRight) {
    const float room = clientWidth - 2 * edgeLeft - reservedRight;
    if (room < MinWidth) return 0;
    return std::floor(std::min(Paper::width, room));
}
inline bool FitsHeight(float clientHeight, float topReserve, float cardHeight) {
    return clientHeight - topReserve - EdgeBottom - Paper::gap >= cardHeight;
}

// ---- Motion ---------------------------------------------------------------------
// One scalar animated either by a critically damped spring (enter, state swap:
// interruptible, carries velocity, no overshoot) or a fixed ease-out tween (exit).
// Always retargeted from the presentation value and velocity at `now`.
class Channel {
    enum class Mode { Rest, Spring, Tween } mode_ = Mode::Rest;
    float from_ = 0, velocity_ = 0, target_ = 0, omega_ = 0;
    double duration_ = 0;
    Time begin_{};
    static double Seconds(Time a, Time b) { return std::chrono::duration<double>(b - a).count(); }
    float Raw(Time now, float* v) const {
        const double t = std::max(0.0, Seconds(begin_, now));
        if (mode_ == Mode::Spring) {
            const double c1 = from_ - target_, c2 = velocity_ + omega_ * c1, e = std::exp(-omega_ * t);
            double x = target_ + (c1 + c2 * t) * e, dv = (c2 - omega_ * (c1 + c2 * t)) * e;
            // No bounce: a retarget with inherited velocity may never cross the target.
            if (c1 == 0 || (x - target_) * c1 < 0) { x = target_; dv = 0; }
            if (v) *v = (float)dv;
            return (float)x;
        }
        if (mode_ == Mode::Tween) {
            if (t >= duration_) { if (v) *v = 0; return target_; }
            const float p = (float)(t / duration_);
            const float e = NativeChrome::Motion::Eased(p, NativeChrome::Motion::Curve::EaseOut);
            if (v) {
                const float h = .001f, e2 = NativeChrome::Motion::Eased(std::min(1.f, p + h), NativeChrome::Motion::Curve::EaseOut);
                *v = (float)((e2 - e) / h / duration_ * (target_ - from_));
            }
            return from_ + (target_ - from_) * e;
        }
        if (v) *v = 0;
        return target_;
    }
public:
    float epsilon = .002f;  // settle threshold in channel units
    float Value(Time now) const { return Raw(now, nullptr); }
    float Velocity(Time now) const { float v = 0; Raw(now, &v); return v; }
    float Target() const { return target_; }
    void Snap(float value) { mode_ = Mode::Rest; from_ = target_ = value; velocity_ = 0; }
    // response: seconds (Apple "response"); damping fixed at 1.0.
    void Spring(float target, Time now, double response) {
        float v = 0; const float x = Raw(now, &v);
        if (mode_ == Mode::Rest && x == target) { Snap(target); return; }
        mode_ = Mode::Spring; from_ = x; velocity_ = v; target_ = target; begin_ = now;
        omega_ = (float)(2 * 3.14159265358979 / response);
    }
    void Tween(float target, Time now, Ms duration) {
        const float x = Raw(now, nullptr);
        if (duration.count() <= 0 || x == target) { Snap(target); return; }
        mode_ = Mode::Tween; from_ = x; velocity_ = 0; target_ = target; begin_ = now;
        duration_ = duration.count() / 1000.0;
    }
    bool Moving(Time now) const {
        if (mode_ == Mode::Rest) return false;
        if (mode_ == Mode::Tween) return Seconds(begin_, now) < duration_;
        float v = 0; const float x = Raw(now, &v);
        return std::fabs(x - target_) > epsilon || std::fabs(v) > epsilon * omega_;
    }
    // Ends finished segments so idle checks are O(1) and exact.
    void Settle(Time now) { if (mode_ != Mode::Rest && !Moving(now)) Snap(target_); }
};

namespace Timing {
inline constexpr double response = .25;          // spring: ~99.5% at 300 ms, settled < 370 ms
inline constexpr Ms exit{150}, reducedFade{200}, outgoing{100}, press{150};
inline constexpr float enterOffset = 12, exitOffset = 12; // DIP below the anchor (enter and leave by the same path)
inline constexpr Ms installedDwell{6000}, dwellAfterHover{2000};
}

struct Layer { Content content; Geometry geometry; Channel opacity; unsigned id = 0; };
struct LayerFrame { const Content* content = nullptr; const Geometry* geometry = nullptr; float opacity = 0, iconScale = 1; };
struct Frame {
    bool visible = false, exiting = false;
    float opacity = 0, offset = 0, height = 0, progress = -1;
    std::array<LayerFrame, 2> layers{}; // [0] outgoing (may be empty), [1] current
};

class Presenter {
    std::array<Layer, 2> layers_{}; // [1] = current target content, [0] = outgoing
    bool hasOut_ = false, shown_ = false, exiting_ = false;
    Channel opacity_, offset_, height_, progress_;
    unsigned nextId_ = 1;
    bool reduced_ = false;
public:
    Presenter() { height_.epsilon = .05f; offset_.epsilon = .05f; progress_.epsilon = .001f; }
    bool Shown() const { return shown_; }
    bool Exiting() const { return exiting_; }
    bool Reduced() const { return reduced_; }
    const Content& Current() const { return layers_[1].content; }
    const Geometry& CurrentGeometry() const { return layers_[1].geometry; }
    unsigned Generation() const { return layers_[1].id; }
    void SetReduced(bool reduced, Time now) {
        if (reduced == reduced_) return;
        reduced_ = reduced;
        if (reduced) { // settle movement now; keep opacity fades
            offset_.Snap(offset_.Target()); height_.Snap(height_.Target()); progress_.Snap(progress_.Target());
            (void)now;
        }
    }
    void Set(const Content& c, const Geometry& g, Time now) {
        if (c.kind == Kind::None) {
            if (!shown_ || exiting_) return;
            exiting_ = true;
            opacity_.Tween(0, now, Timing::exit);
            if (!reduced_) offset_.Tween(Timing::exitOffset, now, Timing::exit);
            return;
        }
        const bool fresh = !shown_;
        const bool sameKind = shown_ && layers_[1].content.kind == c.kind;
        if (fresh) {
            hasOut_ = false;
            layers_[1] = {c, g, {}, nextId_++};
            layers_[1].opacity.Snap(1);
            opacity_.Snap(0); height_.Snap(g.height);
            if (reduced_) { offset_.Snap(0); opacity_.Tween(1, now, Timing::reducedFade); }
            else { offset_.Snap(Timing::enterOffset); offset_.Spring(0, now, Timing::response); opacity_.Spring(1, now, Timing::response); }
            progress_.Snap(c.progress);
            shown_ = true; exiting_ = false;
            return;
        }
        if (exiting_) { // interrupted exit: come back from the presentation values
            exiting_ = false;
            if (reduced_) opacity_.Tween(1, now, Timing::reducedFade); else opacity_.Spring(1, now, Timing::response);
            if (reduced_) offset_.Snap(0); else offset_.Spring(0, now, Timing::response);
        }
        if (sameKind) {
            layers_[1].content = c; layers_[1].geometry = g;
        } else {
            // Keep the more visible of the two existing layers as the outgoing one.
            Layer& keep = (!hasOut_ || layers_[1].opacity.Value(now) >= layers_[0].opacity.Value(now)) ? layers_[1] : layers_[0];
            Layer out = keep;
            out.opacity.Tween(0, now, reduced_ ? Timing::reducedFade : Timing::outgoing);
            layers_[0] = out; hasOut_ = true;
            layers_[1] = {c, g, {}, nextId_++};
            layers_[1].opacity.Snap(0);
            if (reduced_) layers_[1].opacity.Tween(1, now, Timing::reducedFade);
            else layers_[1].opacity.Spring(1, now, Timing::response);
        }
        if (reduced_) height_.Snap(g.height); else height_.Spring(g.height, now, Timing::response);
        // Progress: real values only; decreases (new download) snap, increases glide.
        if (c.progress < 0 || progress_.Target() < 0 || c.progress < progress_.Value(now) || reduced_) progress_.Snap(c.progress);
        else progress_.Spring(c.progress, now, Timing::response);
    }
    // Instantly removes the card (owner closed / does not fit without motion).
    void Clear() { shown_ = exiting_ = hasOut_ = false; opacity_.Snap(0); }
    Frame Sample(Time now) const {
        Frame f;
        if (!shown_) return f;
        f.opacity = NativeChrome::Motion::Clamp(opacity_.Value(now));
        if (exiting_ && !opacity_.Moving(now)) return f; // exit finished
        f.visible = true; f.exiting = exiting_;
        f.offset = offset_.Value(now);
        f.height = height_.Value(now);
        const float p = progress_.Value(now), target = progress_.Target();
        f.progress = target < 0 ? -1 : std::min(std::max(p, 0.f), target);
        auto frame = [&](const Layer& l) {
            // Content opacity is the square of its channel: the incoming layer becomes legible
            // after the outgoing one has mostly gone, so two texts never read on top of each
            // other. Monotonic mapping, so interruptions stay continuous.
            const float v = NativeChrome::Motion::Clamp(l.opacity.Value(now));
            LayerFrame lf{&l.content, &l.geometry, v * v, 1};
            lf.iconScale = reduced_ ? 1 : .25f + .75f * v; // better-ui icon swap
            return lf;
        };
        if (hasOut_ && layers_[0].opacity.Moving(now)) f.layers[0] = frame(layers_[0]);
        f.layers[1] = frame(layers_[1]);
        if (!hasOut_) f.layers[1].iconScale = 1;
        return f;
    }
    bool Moving(Time now) const {
        if (!shown_) return false;
        return opacity_.Moving(now) || offset_.Moving(now) || height_.Moving(now) || progress_.Moving(now) ||
               layers_[1].opacity.Moving(now) || (hasOut_ && layers_[0].opacity.Moving(now));
    }
    // Call each frame: finalises an exit and drops finished segments.
    void Settle(Time now) {
        if (shown_ && exiting_ && !opacity_.Moving(now)) { shown_ = exiting_ = hasOut_ = false; return; }
        for (Channel* c : {&opacity_, &offset_, &height_, &progress_}) c->Settle(now);
        layers_[1].opacity.Settle(now);
        if (hasOut_ && !layers_[0].opacity.Moving(now)) hasOut_ = false;
    }
    bool Interactive() const { return shown_ && !exiting_; }
};

// Pointer arbitration: an action is bound at pointer-down and fires on pointer-up
// only if the same content generation and button are still under the pointer.
class Clicks {
    int down_ = -1; unsigned downGen_ = 0;
    unsigned firedGen_ = 0; std::optional<Action> fired_;
public:
    void Down(int button, unsigned generation) { down_ = button; downGen_ = generation; }
    int Pressed(unsigned generation) const { return downGen_ == generation ? down_ : -1; }
    void Cancel() { down_ = -1; }
    std::optional<Action> Up(int button, unsigned generation, const Content& c) {
        const int down = down_; const unsigned gen = downGen_; down_ = -1;
        if (button < 0 || button != down || gen != generation || button >= (int)c.buttons.size()) return std::nullopt;
        return Activate(button, generation, c);
    }
    // Keyboard and pointer share the repeat guard: one activation per action per generation.
    std::optional<Action> Activate(int button, unsigned generation, const Content& c) {
        if (button < 0 || button >= (int)c.buttons.size()) return std::nullopt;
        const Action a = c.buttons[button].action;
        if (firedGen_ == generation && fired_ && *fired_ == a) return std::nullopt;
        firedGen_ = generation; fired_ = a;
        return a;
    }
};

// Installed: finite dwell, never for states that need an action. Hover/focus pause it.
class Dwell {
    std::optional<Time> deadline_; bool held_ = false;
public:
    void Start(Time now) { deadline_ = now + Timing::installedDwell; }
    void Stop() { deadline_.reset(); }
    void Hold(bool held, Time now) {
        if (held_ && !held && deadline_) deadline_ = std::max(*deadline_, now + Timing::dwellAfterHover);
        held_ = held;
    }
    bool Active() const { return deadline_.has_value(); }
    bool Expired(Time now) const { return deadline_ && !held_ && now >= *deadline_; }
    std::optional<Time> Deadline() const { return held_ ? std::nullopt : deadline_; }
};
}} // namespace BrowserUpdate::ToastModel
