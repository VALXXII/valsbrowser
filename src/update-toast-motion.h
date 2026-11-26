#pragma once
#include "i18n.h"
// Pure model of the native update card: Paper geometry, texts, formatting, click arbitration and presentation-value
// motion. No HWND, Direct2D or clock access, so everything here is deterministic and unit-tested (tests/update-toast).
//
// The card is Val's « Mise à jour » card (Paper « Browser Design », page « Atelier de Val », boards New update download /
// downloading / completed, validated 2026-11-26 on the preview design/update-card-preview): a centred card with the
// GrainGradient background, a 53 DIP icon, a title, a text or the progress line, and one or two buttons. While it
// downloads, the icon IS the Downloads drawer's animated icon (src/download-icon.h, same model and frames, white at 85 %).
// States without a board (Installing, Installed, Error) keep their texts, buttons and rules, drawn in the same layout.
#include "update-types.h"
#include "download-icon.h"
#include "profile-color.h"
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
namespace DI = NativeChrome::DownloadIcon;

// Exact Paper values (get_jsx / get_computed_styles of 277H-0, 2784-0, 278R-0). DIPs, border-box coordinates.
namespace Paper {
inline constexpr float width = 269, border = 1, radius = 8 /* DWM-free region corner, as before */, padTop = 86, padX = 16, padBottom = 16;
// The boards are 209.59375 / 187.59375 high: their content overflows the fixed board by this much at the bottom.
inline constexpr float textTrim = .40625f;
inline constexpr float icon = 53, iconTop = 20, iconOpacity = .85f;
// Icon placement inside its 53 box (Paper SVGs): the download icon's viewBox starts at 1.6 3.2 of 424 (0.2 / 0.4 DIP up
// and left), the check's at 0 0 with top 19.8 (0.2 DIP up).
inline constexpr float trayDx = -.2f, trayDy = -.4f, checkDx = 0, checkDy = -.2f;
inline constexpr float titleSize = 16, titleLine = 14, titleTracking = -.01f, titleOpacity = 230 / 255.f;   // #FFFFFFE6
inline constexpr float bodySize = 14, bodyLine = 18, bodyTracking = .01f, bodyOpacity = 191 / 255.f;       // #FFFFFFBF
inline constexpr float textGap = 8, actionsGap = 16;
inline constexpr float statIcon = 14, statGap = 4, statSize = 14, statLine = 14, statTracking = -.01f, statOpacity = 191 / 255.f;
inline constexpr float buttonHeight = 32, buttonRadius = 6, buttonGap = 8, primaryPadX = 8, secondaryPadX = 16;
inline constexpr float buttonSize = 14, buttonLine = 14, buttonTracking = -.01f, primaryLabel = .8f, secondaryLabel = .5f;
inline constexpr float surface = 23 / 255.f, borderOpacity = 13 / 255.f;                                  // #171717, #FFFFFF0D
inline constexpr float grainWidth = 268, grainHeight = 202;                                                // GrainGradient node
inline constexpr float fadeWidth = 269, fadeHeight = 347, fadeStop = .7964f;                               // #17171700 -> #171717
struct Fade { float left, top; };
inline constexpr Fade fadeOffer{-1, -73}, fadeDownload{-2, -100};
inline constexpr float edgeLeft = 14, edgeBottom = 14.447f;
inline constexpr float available = 209.59375f, downloading = 187.59375f, ready = 209.59375f;
}
// Solid card, as before: no acrylic, blur or page transparency.
inline constexpr float MaterialOpacity = 1.f;
// Card snapped to whole DIPs at the bottom edge; Paper's 14.447 is a canvas artefact.
inline constexpr float EdgeBottom = 14;
// Narrowest card that keeps every title (« Téléchargement en cours… » is 190.1 DIP) and both offer buttons on one line.
inline constexpr float MinWidth = 226;

enum class Kind { None, Available, Downloading, Ready, Installing, Installed, Error };
// Tray: Phosphor download-simple (Disponible, Installing); Check: check-circle (Prête, Installed); Info (Error);
// Drawer: the Downloads drawer's animated icon (En cours).
enum class Glyph { Tray, Check, Info, Drawer };
struct Button { std::wstring label; Action action; };
struct Content {
    Kind kind = Kind::None;
    Glyph icon = Glyph::Tray;
    std::wstring title, body;
    bool stats = false;           // En cours: size + remaining time
    std::wstring size, eta;       // eta empty = unknown (« --:-- »)
    float progress = -1;          // -1 = unknown total
    std::vector<Button> buttons;  // [0] the primary (takes the row), [1] the secondary (its own width)
};

// One decimal in the interface language: « 50.4 » in English, « 50,4 » in French.
inline std::wstring OneDecimal(double value) { return I18n::Decimal(value, 1); }
// Binary megabytes, as Windows Explorer labels them, always one decimal. Units switch to GB from 1000 MB on the total.
inline std::wstring FormatSize(std::uint64_t received, std::uint64_t total) {
    const double mb = 1024.0 * 1024.0, gb = mb * 1024.0;
    const bool giga = (total ? total : received) >= 1000 * mb;
    const double unit = giga ? gb : mb;
    // English keeps « 50.4/200.0MB »; French writes « 50,4/200,0 Mo » (Paper).
    const std::wstring suffix = std::wstring(I18n::Current() == I18n::Locale::Fr ? L" " : L"") + (giga ? TR(size_unit_gb) : TR(size_unit_mb));
    if (!total) return OneDecimal(received / unit) + suffix;
    if (received > total) received = total;
    return OneDecimal(received / unit) + L"/" + OneDecimal(total / unit) + suffix;
}
// « 00:14 » (Paper), whole seconds rounded up; h:mm:ss from one hour. Unknown (negative / NaN) => empty, never invented.
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
        c = {Kind::Available, Glyph::Tray, TR(update_toast_available), TR(update_toast_available_body)};
        c.buttons = {{TR(updates_download), Action::Download}, {TR(common_later), Action::Later}}; return c;
    case Phase::Downloading:
        c = {Kind::Downloading, Glyph::Drawer, TR(update_toast_downloading), L""};
        c.stats = true; c.size = FormatSize(v.received, v.total); c.eta = FormatEta(v.secondsRemaining);
        c.progress = v.total ? (float)std::min(1.0, (double)v.received / (double)v.total) : -1.f;
        c.buttons = {{TR(common_cancel), Action::Cancel}}; return c;
    case Phase::Ready:
        c = {Kind::Ready, Glyph::Check, TR(update_toast_ready), TR(update_toast_ready_body)};
        c.buttons = {{TR(update_toast_restart_install), Action::RestartInstall}}; return c;
    case Phase::Installing:  // no board: the offer's layout, no action
        c = {Kind::Installing, Glyph::Tray, TR(update_toast_installing), TR(update_toast_installing_body)}; return c;
    case Phase::Installed:   // no board: the check, no action, finite dwell (unchanged)
        c = {Kind::Installed, Glyph::Check, TR(update_toast_installed), TR(update_toast_installed_body)}; return c;
    case Phase::Error:       // no board: Réessayer as the primary, Ignorer as the secondary (the offer's two buttons)
        c = {Kind::Error, Glyph::Info, TR(update_toast_failed),
             v.error.empty() ? std::wstring(TR(update_toast_failed_body)) : ClipError(v.error)};
        c.buttons = {{TR(downloads_action_retry), Action::Retry}, {TR(update_toast_dismiss), Action::Dismiss}}; return c;
    }
    return c;
}

struct Rect { float left = 0, top = 0, right = 0, bottom = 0;
    float Width() const { return right - left; } float Height() const { return bottom - top; }
    bool Contains(float x, float y) const { return x >= left && x < right && y >= top && y < bottom; } };
// Card-local geometry, y down from the card's top edge (border box).
struct Geometry {
    float width = Paper::width, height = 0;
    Rect icon, title, body, stats;
    std::vector<Rect> buttons;   // [0] primary, [1] secondary
    int bodyLines = 0;
};
inline float ContentWidth(float width) { return width - 2 * (Paper::border + Paper::padX); }
// bodyLines: the renderer's balanced DirectWrite layout at ContentWidth(); secondaryWidth: the secondary button's own
// width (its label + 2 × 16).
inline Geometry Layout(const Content& c, float width, int bodyLines, float secondaryWidth = 0) {
    Geometry g; g.width = width; g.bodyLines = bodyLines;
    const float x = Paper::border + Paper::padX, r = width - x;
    g.icon = {width / 2 - Paper::icon / 2, Paper::border + Paper::iconTop, width / 2 + Paper::icon / 2, Paper::border + Paper::iconTop + Paper::icon};
    float y = Paper::border + Paper::padTop;
    g.title = {x, y, r, y + Paper::titleLine};
    y = g.title.bottom;
    if (!c.body.empty() && bodyLines > 0) { y += Paper::textGap; g.body = {x, y, r, y + bodyLines * Paper::bodyLine}; y = g.body.bottom; }
    else if (c.stats) { y += Paper::textGap; g.stats = {x, y, r, y + Paper::statLine}; y = g.stats.bottom; }
    y += Paper::actionsGap;
    if (!c.buttons.empty()) {
        const bool two = c.buttons.size() > 1;
        const float second = two ? std::min(secondaryWidth, (r - x - Paper::buttonGap) / 2) : 0;
        const float split = two ? r - second - Paper::buttonGap : r;
        g.buttons.push_back({x, y, split, y + Paper::buttonHeight});
        if (two) g.buttons.push_back({r - second, y, r, y + Paper::buttonHeight});
        y += Paper::buttonHeight + Paper::padBottom;
    }
    g.height = y + Paper::border - Paper::textTrim;
    return g;
}
// Card width for an owner client (DIP); 0 = does not fit (card hidden, state kept).
inline float FitWidth(float clientWidth, float edgeLeft, float reservedRight) {
    const float room = clientWidth - 2 * edgeLeft - reservedRight;
    if (room < MinWidth) return 0;
    return std::floor(std::min(Paper::width, room));
}
inline bool FitsHeight(float clientHeight, float topReserve, float cardHeight) {
    return clientHeight - topReserve - EdgeBottom - Paper::actionsGap >= cardHeight;
}

// ---- The background's colours follow the profile (Val, 2026-11-26) ----------------
// Val drew the GrainGradient's three colours for the violet profile (ink #B49BFF, Paper --color-profile-violet).
// For another profile ink, each colour keeps, in OKLCH: its own lightness, its hue offset from the violet ink and its
// chroma RATIO to the violet ink's (#8A66FF: −4.75°, ×1.52; #ECAEFF: +23.9°, ×0.90; #F4F4FF: −8.1°, ×0.10). A colour
// out of sRGB loses chroma only (lightness and hue kept). The violet ink gives exactly Val's three colours; a grey ink
// (chroma under 0.02, the « Personnel » profile) gives neutral greys at the same lightnesses.
namespace Grain {
struct Lch { double L, C, h; };
inline Lch ToLch(ProfileColor::Rgb8 c) {
    using namespace ProfileColor::Detail;
    const double r = ToLinear(c.r / 255.0), g = ToLinear(c.g / 255.0), b = ToLinear(c.b / 255.0);
    const double l = std::cbrt(.4122214708 * r + .5363325363 * g + .0514459929 * b), m = std::cbrt(.2119034982 * r + .6806995451 * g + .1073969566 * b);
    const double s = std::cbrt(.0883024619 * r + .2817188376 * g + .6299787005 * b);
    const double L = .2104542553 * l + .7936177850 * m - .0040720468 * s, A = 1.9779984951 * l - 2.4285922050 * m + .4505937099 * s;
    const double B = .0259040371 * l + .7827717662 * m - .8086757660 * s;
    double h = std::atan2(B, A) * 180 / Pi; if (h < 0) h += 360;
    return {L, std::sqrt(A * A + B * B), h};
}
inline constexpr ProfileColor::Rgb8 VioletInk{0xB4, 0x9B, 0xFF};
inline constexpr ProfileColor::Rgb8 Board[3] = {{0x8A, 0x66, 0xFF}, {0xEC, 0xAE, 0xFF}, {0xF4, 0xF4, 0xFF}};
inline constexpr double GreyChroma = .02;   // as ProfileColor::FromHex: below it, a grey
inline std::array<ProfileColor::Rgb8, 3> Colors(ProfileColor::Rgb8 ink) {
    std::array<ProfileColor::Rgb8, 3> out{};
    const Lch v = ToLch(VioletInk), p = ToLch(ink);
    const double ratio = p.C < GreyChroma ? 0 : p.C / v.C, turn = p.h - v.h;
    for (int i = 0; i < 3; ++i) {
        if (ink == VioletInk) { out[i] = Board[i]; continue; }   // exactly Val's (the formula gives them too)
        const Lch b = ToLch(Board[i]);
        const double h = std::fmod(b.h + turn + 720, 360);
        const double C = ProfileColor::Detail::FitChroma(b.L, b.C * ratio, h);
        out[i] = ProfileColor::Detail::Pack(ProfileColor::Detail::FromOklch(b.L, C, h));
    }
    return out;
}
} // namespace Grain

// ---- Motion ---------------------------------------------------------------------
// cubic-bezier(x1, y1, x2, y2) by bisection on x (the Lottie curves of design/lottie/download-progress.py).
inline float Bezier(float x1, float y1, float x2, float y2, float x) {
    if (x <= 0) return 0;
    if (x >= 1) return 1;
    float lo = 0, hi = 1, s = .5f;
    for (int n = 0; n < 32; ++n) { s = (lo + hi) / 2; const float u = 1 - s, xs = 3 * u * u * s * x1 + 3 * u * s * s * x2 + s * s * s; if (xs < x) lo = s; else hi = s; }
    s = (lo + hi) / 2; const float u = 1 - s;
    return 3 * u * u * s * y1 + 3 * u * s * s * y2 + s * s * s;
}
inline float Settle(float x) { return Bezier(.45f, 0, .15f, 1, x); }   // the drawer's SETTLE

// One scalar animated either by a critically damped spring (interruptible, carries velocity, no overshoot) or a fixed
// tween. Always retargeted from the presentation value and velocity at `now`.
class Channel {
public:
    enum class Curve { EaseOut, Settle };
private:
    enum class Mode { Rest, Spring, Tween } mode_ = Mode::Rest;
    float from_ = 0, velocity_ = 0, target_ = 0, omega_ = 0;
    double duration_ = 0;
    Curve curve_ = Curve::EaseOut;
    Time begin_{};
    static double Seconds(Time a, Time b) { return std::chrono::duration<double>(b - a).count(); }
    float Ease(float p) const { return curve_ == Curve::Settle ? ToastModel::Settle(p) : NativeChrome::Motion::Eased(p, NativeChrome::Motion::Curve::EaseOut); }
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
            const float p = (float)(t / duration_), e = Ease(p);
            if (v) { const float h = .001f, e2 = Ease(std::min(1.f, p + h)); *v = (float)((e2 - e) / h / duration_ * (target_ - from_)); }
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
    void Tween(float target, Time now, Ms duration, Curve curve = Curve::EaseOut) {
        const float x = Raw(now, nullptr);
        if (duration.count() <= 0 || x == target) { Snap(target); return; }
        mode_ = Mode::Tween; from_ = x; velocity_ = 0; target_ = target; begin_ = now; curve_ = curve;
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

// Designer's values (2026-11-26) where Paper gives none; the drawer's own timings for its icon.
namespace Timing {
inline constexpr double response = .3;             // no-bounce spring 1.0 / 0.3 s: height, « Plus tard », fade, entrance
inline constexpr Ms crossfade{120};                // texts, labels, icons (also every change under reduced motion)
inline constexpr Ms exit{160};                     // disappearance: a fade, no movement
inline constexpr Ms settle{450};                   // ring 88 -> 96 and the fill back to the ink (the drawer's « done », 27 frames)
inline constexpr Ms press{150};
inline constexpr float enterOffset = 8;            // DIP below the anchor: the card rises into place
inline constexpr Ms installedDwell{6000}, dwellAfterHover{2000};
}

// Track dimming of the white progress (coordinator, 2026-11-26): the track sits at 50 % of the ink while the fill means
// progress, following the ring's own frames so nothing jumps (tray -> ring morph, then the « done » fade).
inline float TrackDim(int ringFrame) {
    if (ringFrame <= DI::Morph) return Settle(ringFrame / (float)DI::Morph);
    if (ringFrame <= DI::FillEnd) return 1;
    return 1 - Settle(std::min(1.f, (ringFrame - DI::FillEnd) / (float)DI::Out));
}

struct Layer { Content content; Geometry geometry; Channel opacity; unsigned id = 0; };
struct LayerFrame { const Content* content = nullptr; const Geometry* geometry = nullptr; float opacity = 0; };
// What the 53 DIP icon box shows.
struct IconFrame {
    enum class Mode { None, Static, Drawer, Check } mode = Mode::None;
    Glyph glyph = Glyph::Tray;   // Static
    DI::Pose pose;               // Drawer: the drawer's frames
    float dim = 0;               // Drawer: track dimming
    float settle = 0;            // Check: 0 = the drawer's ring (r 88) with its check, 1 = Val's check-circle (r 96)
    float opacity = 0;
    bool operator==(const IconFrame& o) const { return mode == o.mode && glyph == o.glyph && pose == o.pose && dim == o.dim && settle == o.settle && opacity == o.opacity; }
};
struct Frame {
    bool visible = false, exiting = false;
    float opacity = 0, offset = 0, height = 0;
    float fadeLeft = Paper::fadeOffer.left, fadeTop = Paper::fadeOffer.top;
    float secondary = 0;          // the secondary button's share of its slot (« Plus tard » folds 1 -> 0)
    float secondaryWidth = 0;     // its own width (label + 32)
    float actions = 0;            // the button row's presence (0 for Installing / Installed)
    std::array<LayerFrame, 2> layers{};   // [0] outgoing (may be empty), [1] current: texts and button labels
    IconFrame icon, ghost;        // ghost: the outgoing icon during a cross-fade
    float ripple = -1;            // 0..1: the drawer's end ripple
};

inline Glyph StaticGlyph(Kind k) {
    switch (k) {
    case Kind::Ready: case Kind::Installed: return Glyph::Check;
    case Kind::Error: return Glyph::Info;
    default: return Glyph::Tray;
    }
}

class Presenter {
    std::array<Layer, 2> layers_{}; // [1] = current target content, [0] = outgoing
    bool hasOut_ = false, shown_ = false, exiting_ = false, reduced_ = false;
    Channel opacity_, offset_, height_, fadeLeft_, fadeTop_, secondary_, actions_, check_, ghost_, main_;
    float secondaryWidth_ = 0;
    unsigned nextId_ = 1;
    // The drawer's icon while downloading, and its end.
    DI::Model icon_;
    bool drawer_ = false;           // the icon box follows icon_ (download, its end, a cancel's reversal)
    bool checkLatched_ = false;     // a completed download's check is drawn: the card keeps it (no 3 s hold, no morph back)
    Glyph glyph_ = Glyph::Tray;     // the static icon when the drawer's is not shown
    IconFrame ghostFrame_;
    // Downloading -> Ready waits for the arrow to come back to rest (the drawer's loop boundary): texts, button and
    // height change when the check starts to draw.
    bool pending_ = false; Content pendingContent_; Geometry pendingGeometry_;

    static float Fraction(const Content& c) { return c.progress > 0 ? c.progress : 0.f; }
    IconFrame IconAt(Time now) const {
        IconFrame f; f.opacity = 1;
        if (drawer_) {
            const DI::Pose p = icon_.At(now);
            if (checkLatched_ || (p.success >= DI::SuccessMorph && icon_.Succeeded())) {
                f.mode = IconFrame::Mode::Check; f.settle = checkLatched_ ? NativeChrome::Motion::Clamp(check_.Value(now)) : 0; return f;
            }
            if (!p.idle) { f.mode = IconFrame::Mode::Drawer; f.pose = p; f.dim = TrackDim(p.ring); return f; }
        }
        f.mode = IconFrame::Mode::Static; f.glyph = glyph_;
        return f;
    }
    void Crossfade(Time now) {
        ghostFrame_ = IconAt(now);
        ghost_.Snap(NativeChrome::Motion::Clamp(main_.Value(now))); ghost_.Tween(0, now, Timing::crossfade);
        main_.Snap(0); main_.Tween(1, now, Timing::crossfade);
    }
    void Move(Channel& c, float target, Time now) { if (reduced_) c.Snap(target); else c.Spring(target, now, Timing::response); }
    void Geometry_(const Content& c, const Geometry& g, Time now) {
        Move(height_, g.height, now);
        const auto fade = c.kind == Kind::Downloading ? Paper::fadeDownload : Paper::fadeOffer;
        Move(fadeLeft_, fade.left, now); Move(fadeTop_, fade.top, now);
        if (c.buttons.size() > 1 && g.buttons.size() > 1) secondaryWidth_ = g.buttons[1].Width();
        Move(secondary_, c.buttons.size() > 1 ? 1.f : 0.f, now);
        Move(actions_, c.buttons.empty() ? 0.f : 1.f, now);
    }
    // Text layers: the outgoing fades in 120 ms, the incoming fades in 120 ms (both drawn squared: never two legible).
    void Swap(const Content& c, const Geometry& g, Time now) {
        Layer& keep = (!hasOut_ || layers_[1].opacity.Value(now) >= layers_[0].opacity.Value(now)) ? layers_[1] : layers_[0];
        Layer out = keep;
        out.opacity.Tween(0, now, Timing::crossfade);
        layers_[0] = out; hasOut_ = true;
        layers_[1] = {c, g, {}, nextId_++};
        layers_[1].opacity.Snap(0); layers_[1].opacity.Tween(1, now, Timing::crossfade);
        Geometry_(c, g, now);
    }
    // The drawer's model, fed like the toolbar's (src/main.cpp UpdateDownloadIcon): active + combined fraction.
    void Feed(bool active, float fraction, bool completed, Time now) { icon_.Update(active, fraction, completed, now, reduced_); }
    void ShowReady(const Content& c, const Geometry& g, Time now) {
        pending_ = false;
        Swap(c, g, now);
        glyph_ = Glyph::Check;
        if (reduced_ || !drawer_) { Crossfade(now); drawer_ = false; check_.Snap(1); }
    }
public:
    Presenter() { height_.epsilon = .05f; offset_.epsilon = .05f; fadeTop_.epsilon = .05f; fadeLeft_.epsilon = .01f; }
    bool Shown() const { return shown_; }
    bool Exiting() const { return exiting_; }
    bool Reduced() const { return reduced_; }
    const Content& Current() const { return layers_[1].content; }
    const Geometry& CurrentGeometry() const { return layers_[1].geometry; }
    unsigned Generation() const { return layers_[1].id; }
    bool PendingReady() const { return pending_; }
    void SetReduced(bool reduced, Time now) {
        if (reduced == reduced_) return;
        reduced_ = reduced;
        if (reduced) { // settle movement now; keep opacity fades
            for (Channel* c : {&offset_, &height_, &fadeLeft_, &fadeTop_, &secondary_, &actions_, &check_}) c->Snap(c->Target());
            (void)now;
        }
    }
    void Set(const Content& c, const Geometry& g, Time now) {
        Advance(now);
        if (c.kind == Kind::None) {
            pending_ = false;
            if (!shown_ || exiting_) return;
            exiting_ = true;
            opacity_.Tween(0, now, Timing::exit);
            return;
        }
        if (!shown_) {
            hasOut_ = pending_ = checkLatched_ = false;
            layers_[1] = {c, g, {}, nextId_++};
            layers_[1].opacity.Snap(1);
            opacity_.Snap(0); height_.Snap(g.height);
            const auto fade = c.kind == Kind::Downloading ? Paper::fadeDownload : Paper::fadeOffer;
            fadeLeft_.Snap(fade.left); fadeTop_.Snap(fade.top);
            if (c.buttons.size() > 1 && g.buttons.size() > 1) secondaryWidth_ = g.buttons[1].Width();
            secondary_.Snap(c.buttons.size() > 1 ? 1.f : 0.f); actions_.Snap(c.buttons.empty() ? 0.f : 1.f);
            if (reduced_) { offset_.Snap(0); opacity_.Tween(1, now, Timing::crossfade); }
            else { offset_.Snap(Timing::enterOffset); offset_.Spring(0, now, Timing::response); opacity_.Spring(1, now, Timing::response); }
            icon_ = DI::Model{}; drawer_ = c.kind == Kind::Downloading; glyph_ = StaticGlyph(c.kind);
            if (drawer_) Feed(true, Fraction(c), false, now);
            check_.Snap(glyph_ == Glyph::Check ? 1.f : 0.f);
            ghost_.Snap(0); main_.Snap(1);
            shown_ = true; exiting_ = false;
            return;
        }
        if (exiting_) { // interrupted exit: come back from the presentation values
            exiting_ = false;
            if (reduced_) opacity_.Tween(1, now, Timing::crossfade); else opacity_.Spring(1, now, Timing::response);
        }
        const Kind was = pending_ ? Kind::Downloading : layers_[1].content.kind;
        if (pending_ && c.kind == Kind::Ready) { pendingContent_ = c; pendingGeometry_ = g; return; }
        if (pending_) pending_ = false;    // something else replaced the coming « Prête »: handled below from En cours
        if (was == c.kind && !pending_) {
            layers_[1].content = c; layers_[1].geometry = g;
            if (c.kind == Kind::Downloading) Feed(true, Fraction(c), false, now);
            Geometry_(c, g, now);
            return;
        }
        if (was == Kind::Downloading && c.kind == Kind::Ready && drawer_ && !reduced_) {
            // The download ends: the drawer's ripple at once, the check when the arrow is back at rest.
            Feed(false, 1, true, now);
            pending_ = true; pendingContent_ = c; pendingGeometry_ = g;
            Advance(now);
            return;
        }
        if (was == Kind::Downloading && c.kind == Kind::Ready) {   // reduced motion: the drawer's static check, cross-faded
            Feed(false, 1, true, now);
            Swap(c, g, now); Crossfade(now); drawer_ = false; glyph_ = Glyph::Check; check_.Snap(1);
            return;
        }
        if (c.kind == Kind::Downloading) {
            // Disponible -> En cours: the drawer's start (tray curls into the ring, arrow drops in), as in the toolbar.
            const bool continuous = !reduced_ && (!drawer_ ? glyph_ == Glyph::Tray : true) && !checkLatched_;
            if (!continuous) { Crossfade(now); icon_ = DI::Model{}; }
            drawer_ = true; checkLatched_ = false; check_.Snap(0);
            Feed(true, Fraction(c), false, now);
            Swap(c, g, now);
            return;
        }
        if (was == Kind::Downloading && c.kind == Kind::Available && drawer_ && !reduced_) {
            // Annuler: the drawer's cancel, the arrow finishes its loop then the ring unrolls into the tray.
            Feed(false, 0, false, now);
            glyph_ = Glyph::Tray;
            Swap(c, g, now);
            return;
        }
        // Every other change (no drawn path between those icons, or reduced motion): a cross-fade.
        const Glyph next = StaticGlyph(c.kind);
        if (drawer_ || next != glyph_) Crossfade(now);
        if (drawer_ && was == Kind::Downloading) Feed(false, 0, false, now);
        drawer_ = false; checkLatched_ = false; glyph_ = next;
        check_.Snap(next == Glyph::Check ? 1.f : 0.f);
        Swap(c, g, now);
    }
    // Applies what is due at `now` (the coming « Prête », the check's settle). Call before Sample, once per frame.
    void Advance(Time now) {
        if (!shown_) return;
        if (pending_ && drawer_ && now >= icon_.EndStart()) ShowReady(pendingContent_, pendingGeometry_, std::max(icon_.EndStart(), now - Ms(17)));
        if (drawer_ && !checkLatched_ && icon_.Succeeded() && icon_.Ending() && !pending_ && now >= icon_.CheckDrawn()) {
            checkLatched_ = true; check_.Snap(0); check_.Tween(1, icon_.CheckDrawn(), Timing::settle, Channel::Curve::Settle);
        }
    }
    // Instantly removes the card (owner closed / does not fit without motion).
    void Clear() { shown_ = exiting_ = hasOut_ = pending_ = false; opacity_.Snap(0); }
    Frame Sample(Time now) const {
        Frame f;
        if (!shown_) return f;
        f.opacity = NativeChrome::Motion::Clamp(opacity_.Value(now));
        if (exiting_ && !opacity_.Moving(now)) return f; // exit finished
        f.visible = true; f.exiting = exiting_;
        f.offset = offset_.Value(now);
        f.height = height_.Value(now);
        f.fadeLeft = fadeLeft_.Value(now); f.fadeTop = fadeTop_.Value(now);
        f.secondary = NativeChrome::Motion::Clamp(secondary_.Value(now)); f.secondaryWidth = secondaryWidth_;
        f.actions = NativeChrome::Motion::Clamp(actions_.Value(now));
        auto frame = [&](const Layer& l) {
            const float v = NativeChrome::Motion::Clamp(l.opacity.Value(now));
            return LayerFrame{&l.content, &l.geometry, v * v};
        };
        if (hasOut_ && layers_[0].opacity.Moving(now)) f.layers[0] = frame(layers_[0]);
        f.layers[1] = frame(layers_[1]);
        f.icon = IconAt(now);
        const float m = NativeChrome::Motion::Clamp(main_.Value(now)), gh = NativeChrome::Motion::Clamp(ghost_.Value(now));
        f.icon.opacity = m * m;
        if (gh > 0 && ghost_.Moving(now)) { f.ghost = ghostFrame_; f.ghost.opacity = gh * gh; }
        if (drawer_ && !reduced_) f.ripple = icon_.At(now).ripple;
        return f;
    }
    bool IconMoving(Time now) const {
        if (!drawer_ || checkLatched_) return false;
        if (icon_.At(now).ripple >= 0) return true;
        const auto wake = icon_.NextWake(now);
        return wake.frames || (pending_ && wake.delay.has_value());
    }
    bool Moving(Time now) const {
        if (!shown_) return false;
        for (const Channel* c : {&opacity_, &offset_, &height_, &fadeLeft_, &fadeTop_, &secondary_, &actions_, &check_, &ghost_, &main_})
            if (c->Moving(now)) return true;
        return layers_[1].opacity.Moving(now) || (hasOut_ && layers_[0].opacity.Moving(now)) || IconMoving(now) || pending_;
    }
    // Call each frame: applies due changes, finalises an exit and drops finished segments.
    void Settle(Time now) {
        Advance(now);
        if (shown_ && exiting_ && !opacity_.Moving(now)) { shown_ = exiting_ = hasOut_ = pending_ = false; icon_ = DI::Model{}; drawer_ = false; return; }
        for (Channel* c : {&opacity_, &offset_, &height_, &fadeLeft_, &fadeTop_, &secondary_, &actions_, &check_, &ghost_, &main_}) c->Settle(now);
        layers_[1].opacity.Settle(now);
        if (hasOut_ && !layers_[0].opacity.Moving(now)) hasOut_ = false;
        icon_.Settle(now);
        if (drawer_ && !icon_.Active() && !checkLatched_) drawer_ = false;   // a cancel's reversal ended on the tray
        if (checkLatched_ && !check_.Moving(now)) { drawer_ = false; checkLatched_ = false; glyph_ = Glyph::Check; }
    }
    bool Interactive() const { return shown_ && !exiting_; }
};

// Pointer arbitration: an action is bound at pointer-down and fires on pointer-up only if the same content generation
// and button are still under the pointer.
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

// White card icon (« white at 85 % », coordinator 2026-11-26): the drawer's four layers composed like
// DownloadIcon::Compose (src/download-icon.h), all in white: track at the ink (dimmed to half while the fill means
// progress), the fill at the ink over it, the arrow's band cut out of both, the arrow at the ink on top. Inputs are the
// ThorVG frames (premultiplied ARGB, only their coverage is read); the output is premultiplied BGRA white.
inline void ComposeWhite(const uint32_t* track, const uint32_t* fill, const uint32_t* cut, const uint32_t* arrow, size_t count,
                         std::vector<uint32_t>& out, float ink, float dim) {
    out.resize(count);
    const float trackInk = 1 - .5f * NativeChrome::Motion::Clamp(dim);
    for (size_t i = 0; i < count; ++i) {
        const float t = (track[i] >> 24) / 255.f, f = (fill[i] >> 24) / 255.f, c = (cut[i] >> 24) / 255.f, a = (arrow[i] >> 24) / 255.f * ink;
        const float inner = (f + t * trackInk * (1 - f)) * ink * (1 - c);
        const float alpha = std::clamp(a + inner * (1 - a), 0.f, 1.f);
        const uint32_t v = static_cast<uint32_t>(std::lround(alpha * 255));
        out[i] = (v << 24) | (v << 16) | (v << 8) | v;
    }
}
}} // namespace BrowserUpdate::ToastModel
