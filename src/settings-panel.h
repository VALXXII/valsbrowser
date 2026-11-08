#pragma once
// Native "Réglages" drawer, painted with Direct2D through its own Chrome
// instance (shared fonts, icons, helpers). It docks to the window's right edge
// under the toolbar exactly like the download history drawer, but always
// arrives with a slide (never a morph: there is no popover to grow from) and
// leaves the same way. Layout from the Paper frame "Réglages — panneau".
//
// Motion (Apple / Emil): every lane retargets from the value on screen, so a
// toggle flicked twice or a drawer reopened mid-exit never jumps.
//  - Drawer: critically damped springs, exit snappier than enter.
//  - Engine choice: the selection plate and its check glide to the new row.
//  - Switches: the knob slides on a short spring and stretches while pressed.
//  - "Voir plus…": the extra engines unfold like a download section.
// Reduced motion keeps every state change and drops the travel.
#include <windowsx.h>
#include <dwmapi.h>
#include <commctrl.h>
#include <array>
#include <functional>
#include <vector>
#include "chrome.h"
#include "downloads.h"
#include "settings.h"

namespace NativeChrome {
namespace SettingsUI {
using Downloads::Lane; using Downloads::Spring; using Downloads::Easing; using Downloads::Settle;
using BrowserSettings::Engine;

// ---- Hit identifiers ----------------------------------------------------------
enum Hit : int {
    None = 0, Close = 1, EngineBase = 10, More = 20,
    RestoreTabs = 30, Folder = 31, AskWhere = 32, ReduceMotion = 33,
    ClearData = 40, CheckUpdates = 41,
};
inline int EngineHit(int index) { return EngineBase + index; }
inline bool IsEngine(int hit) { return hit >= EngineBase && hit < EngineBase + BrowserSettings::EngineCount; }

// ---- Geometry (DIP, panel-local; x = 0 is the drawer's leading edge) -----------
namespace Layout {
using Design::Box;
inline constexpr float width = Downloads::DrawerLayout::width, top = Downloads::DrawerLayout::top; // 268, under the toolbar
inline constexpr float inset = 12, rowLeft = inset, rowRight = width - inset;                    // rows: 12..256
inline constexpr float textLeft = rowLeft + 8;                                                    // 20: labels, names
inline constexpr float slotRight = rowRight - 4, slot = Design::Size::compact;                   // trailing 24 DIP slot: 228..252
inline constexpr float iconLeft = textLeft, iconSize = Design::Size::icon;                        // favicon / caret: 20..36
inline constexpr float nameLeft = iconLeft + iconSize + Design::Space::md;                        // 44
inline constexpr float padTop = 16, headerHeight = 24, padBottom = 16, groupGap = 20;
inline constexpr float viewTop = padTop + headerHeight + groupGap;                                 // 60: content scrolls under the header
inline constexpr float label = 24, labelGap = 2, row = 32, rowGap = 2, pitch = row + rowGap;
inline constexpr float folderRow = 54, downloadsGap = 6;
// Footer: hairline, 12, "Effacer les données…" (32), 8, version (24), 16.
inline constexpr float footer = 1 + 12 + row + 8 + slot + padBottom;                              // 93
inline constexpr float switchWidth = 28, switchHeight = 16, knob = 12, knobInset = 2, knobStretch = 3;
inline constexpr float topFade = 16, bottomFade = 48;
inline constexpr Box Title() { return {textLeft, padTop, rowRight - slot - 8, padTop + headerHeight}; }
inline constexpr Box CloseButton() { return {rowRight - slot, padTop, rowRight, padTop + headerHeight}; }
inline constexpr Box Row(float y, float h = row) { return {rowLeft, y, rowRight, y + h}; }
inline constexpr Box Trailing(float y, float h = row) { return {slotRight - slot, y + (h - slot) / 2, slotRight, y + (h + slot) / 2}; }
inline constexpr Box Switch(float y) { return {slotRight - switchWidth, y + (row - switchHeight) / 2, slotRight, y + (row + switchHeight) / 2}; }
inline constexpr Box Icon(float y) { return {iconLeft, y + (row - iconSize) / 2, iconLeft + iconSize, y + (row + iconSize) / 2}; }
inline constexpr float FooterTop(float height) { return height - footer; }
inline constexpr float ClearTop(float height) { return FooterTop(height) + 1 + 12; }
inline constexpr float VersionTop(float height) { return height - padBottom - slot; }
}
static_assert(Layout::Trailing(0).left == 228 && Layout::Switch(0).left == 224, "trailing controls share the 252 DIP lane");
static_assert(Layout::viewTop == 60, "content starts 20 DIP under the 24 DIP header");

namespace Timing {
inline constexpr Spring selectSpring{.26f}, switchSpring{.2f};
inline constexpr Motion::Milliseconds select = Settle(.26f), toggle = Settle(.2f), press{120}, spin{900};
}

// One laid-out element of the scrolling list (content coordinates).
enum class Kind { Label, Engine, More, Switch, Folder };
struct Slot {
    Kind kind = Kind::Label; int hit = 0, index = -1;
    float top = 0, height = Layout::row, opacity = 1;
    float clipTop = -1e6f, clipBottom = 1e6f;
    const wchar_t* text = nullptr;
};

// Pure layout: `more` is the fold value of the extra engines (1 = shown).
inline std::vector<Slot> ContentSlots(float more, float scroll, float* contentBottom = nullptr) {
    using namespace Layout;
    std::vector<Slot> slots; float y = viewTop - scroll;
    auto labelAt = [&](const wchar_t* text) { slots.push_back({Kind::Label, 0, -1, y, label, 1, -1e6f, 1e6f, text}); y += label; };
    // Moteur de recherche
    labelAt(L"Moteur de recherche"); y += labelGap;
    for (int i = 0; i < BrowserSettings::PrimaryEngines; ++i) { slots.push_back({Kind::Engine, EngineHit(i), i, y}); y += pitch; }
    const float m = Motion::Clamp(more), extra = (BrowserSettings::EngineCount - BrowserSettings::PrimaryEngines) * pitch;
    if (m > .001f) {
        const float clipTop = y, clipBottom = y + m * extra;
        for (int i = BrowserSettings::PrimaryEngines; i < BrowserSettings::EngineCount; ++i) {
            Slot s{Kind::Engine, EngineHit(i), i, y + (i - BrowserSettings::PrimaryEngines) * pitch - (1 - m) * 8, row, m};
            if (m < 1) { s.clipTop = clipTop; s.clipBottom = clipBottom; }
            if (s.top < clipBottom) slots.push_back(s);
        }
    }
    y += m * extra;
    slots.push_back({Kind::More, More, -1, y}); y += row + groupGap;
    // Démarrage
    labelAt(L"Démarrage"); y += labelGap;
    slots.push_back({Kind::Switch, RestoreTabs, -1, y, row, 1, -1e6f, 1e6f, L"Restaurer les onglets"}); y += row + groupGap;
    // Téléchargements
    labelAt(L"Téléchargements"); y += downloadsGap;
    slots.push_back({Kind::Folder, Folder, -1, y, folderRow}); y += folderRow + downloadsGap;
    slots.push_back({Kind::Switch, AskWhere, -1, y, row, 1, -1e6f, 1e6f, L"Demander où enregistrer"}); y += row + groupGap;
    // Affichage
    labelAt(L"Affichage"); y += labelGap;
    slots.push_back({Kind::Switch, ReduceMotion, -1, y, row, 1, -1e6f, 1e6f, L"Réduire les animations"}); y += row;
    if (contentBottom) *contentBottom = y + scroll;
    return slots;
}

// "…\Downloads": the folder's own name, the rest elided; a drive root stays whole.
inline std::wstring FolderText(const std::wstring& path) {
    std::wstring p = path; while (p.size() > 3 && (p.back() == L'\\' || p.back() == L'/')) p.pop_back();
    const auto slash = p.find_last_of(L"\\/");
    if (p.empty()) return L"Téléchargements";
    if (slash == std::wstring::npos || slash + 1 >= p.size()) return p;
    if (slash <= 2) return p; // "C:\Users" stays readable
    return L"…\\" + p.substr(slash + 1);
}

class Drawer {
public:
    struct Version { std::wstring text; bool checking = false, canCheck = false; };
    struct Hooks {
        std::function<void(Motion::Time)> schedule;                 // display-paced frames
        std::function<void(bool)> closed;                           // true when closed from the keyboard
        std::function<void(BrowserSettings::Change)> changed;       // the state changed: apply + save
        std::function<void()> pickFolder;                           // modal folder picker (may change the state)
        std::function<void()> clearData;                            // modal confirmation + clear
        std::function<void()> checkUpdates;
        std::function<Version()> version;                           // "browser 0.3.x · À jour"
        std::function<std::wstring()> downloadFolder;               // effective folder (default resolved)
        std::function<bool()> reducedMotion;                        // system || setting
    };
private:
    HWND hwnd_ = nullptr, owner_ = nullptr, tip_ = nullptr;
    std::vector<UINT_PTR> tips_; std::vector<std::wstring> tipTexts_;
    Chrome chrome_; Motion::State motion_;
    BrowserSettings::State* state_ = nullptr; Hooks hooks_;
    ID2D1GradientStopCollection* stops_ = nullptr; ID2D1LinearGradientBrush* fade_ = nullptr;
    Lane slide_, scroll_, more_, plate_, spin_;
    std::array<Lane, 3> switches_; // RestoreTabs, AskWhere, ReduceMotion
    std::array<Lane, 3> knobPress_;
    int hover_ = 0, pressed_ = 0, focus_ = 0;
    bool keyboard_ = false, visible_ = false, closing_ = false, tracking_ = false, modal_ = false, animatingPainted_ = false, spinning_ = false;
    int appliedVisible_ = -1; RECT appliedOwner_{}, appliedWindow_{};
    Motion::Time spinStart_{}, landStart_{};
    // Update spinner: a constant counter-clockwise turn while a check runs. When
    // it ends, the glyph keeps its speed and eases into the next full turn (cubic
    // Hermite: the spin's velocity in, zero out), so it settles on its rest pose.
    bool landing_ = false; float landFrom_ = 0, landDistance_ = 0, landSeconds_ = 0;
    static constexpr float spinSpeed = 360.f * 1000 / Timing::spin.count(); // degrees per second
    float Seconds(Motion::Time from, Motion::Time to) const { return static_cast<float>(std::chrono::duration<double>(to - from).count()); }
    bool Landing(Motion::Time now) const { return landing_ && Seconds(landStart_, now) < landSeconds_; }
    bool Spinning(Motion::Time now) const { return !Reduced() && (spinning_ || Landing(now)); }
    float SpinDegrees(Motion::Time now) const {
        if (spinning_) return std::fmod(Seconds(spinStart_, now) * spinSpeed, 360.f);
        if (!Landing(now)) return 0;
        const float u = Motion::Clamp(Seconds(landStart_, now) / landSeconds_), u2 = u * u, u3 = u2 * u;
        return std::fmod(landFrom_ + (3 * u2 - 2 * u3) * landDistance_ + (u - 2 * u2 + u3) * spinSpeed * landSeconds_, 360.f);
    }
    void SetChecking(bool checking, Motion::Time now) {
        if (checking && !spinning_) { spinStart_ = now - std::chrono::duration_cast<Motion::Clock::duration>(std::chrono::duration<double>(SpinDegrees(now) / spinSpeed)); landing_ = false; }
        else if (!checking && spinning_ && !Reduced()) {
            landFrom_ = SpinDegrees(now); landDistance_ = 360 - landFrom_;
            if (landDistance_ < 60) landDistance_ += 360; // too close to rest to brake smoothly: one more turn
            landSeconds_ = std::clamp(2 * landDistance_ / spinSpeed, .25f, .7f); landStart_ = now; landing_ = true;
        }
        spinning_ = checking;
    }
    static int SwitchIndex(int hit) { return hit == RestoreTabs ? 0 : hit == AskWhere ? 1 : hit == ReduceMotion ? 2 : -1; }
    bool SwitchValue(int hit) const {
        if (!state_) return false;
        return hit == RestoreTabs ? state_->restoreTabs : hit == AskWhere ? state_->askWhereToSave : hit == ReduceMotion && state_->reduceMotion;
    }
    bool Reduced() const { return motion_.ReducedMotion(); }
    float Dpi() const { return static_cast<float>(GetDpiForWindow(hwnd_ ? hwnd_ : owner_)); }
    int Px(float dip) const { return static_cast<int>(std::lround(dip * Dpi() / 96.f)); }
    int OwnerPx(float dip) const { return static_cast<int>(std::lround(dip * GetDpiForWindow(owner_) / 96.f)); }
    float Height() const { RECT r{}; if (hwnd_) GetClientRect(hwnd_, &r); return r.bottom * 96.f / Dpi(); }
    float ViewBottom() const { return Layout::FooterTop(Height()); }
    float MaxScroll(Motion::Time now, bool target) const {
        float bottom = 0; ContentSlots(target ? more_.Target() : more_.Value(now), 0, &bottom);
        return std::max(0.f, bottom + Layout::groupGap - ViewBottom());
    }
    std::vector<Slot> Slots(Motion::Time now) const { return ContentSlots(more_.Value(now), scroll_.Value(now)); }
    int SelectedIndex() const { return state_ ? static_cast<int>(state_->engine) : 0; }
    // Selection plate: content y of the selected row (animated), unscrolled.
    float EngineTop(int index, float more) const {
        for (const auto& s : ContentSlots(more, 0)) if (s.kind == Kind::Engine && s.index == index) return s.top;
        return Layout::viewTop + Layout::label + Layout::labelGap; // hidden engine: tucked under the first row
    }
    template<class T> static void Drop(T*& p) { if (p) { p->Release(); p = nullptr; } }
    void PushOpacity(float opacity) {
        ID2D1Layer* layer = nullptr; auto* t = chrome_.RenderTarget();
        if (FAILED(t->CreateLayer(&layer))) return;
        t->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), opacity), layer);
        layer->Release();
    }
    void Fill(Design::Box b, float radius, float r, float g, float bl, float a) {
        auto* brush = chrome_.SolidBrush(); brush->SetColor(D2D1::ColorF(r, g, bl, a));
        chrome_.RenderTarget()->FillRoundedRectangle(D2D1::RoundedRect({b.left, b.top, b.right, b.bottom}, radius, radius), brush);
    }
    void White(Design::Box b, float radius, float a) { if (a > .001f) Fill(b, radius, 1, 1, 1, a); }
    static D2D1_RECT_F R(Design::Box b) { return {b.left, b.top, b.right, b.bottom}; }
    Motion::InteractionVisual Interaction(int hit, Motion::Time now) const { return motion_.Visual(hit, now); }

    // Ghost icon button (14 DIP glyph at 70%, shared button radius): fills and brightens on hover.
    void IconButton(const wchar_t* icon, Design::Box box, int hit, Motion::Time now, float rotation = 0, bool enabled = true) {
        auto* t = chrome_.RenderTarget(); const auto v = Interaction(hit, now);
        const float h = enabled ? v.hover : 0, p = enabled ? v.press : 0;
        if (h > 0 || p > 0) {
            const float c = Design::Surface::canvas, hovered = c + (Design::Surface::hover - c) * h, fill = hovered + (Design::Surface::pressed - hovered) * p;
            chrome_.Round(R(box), Design::Radius::button, fill);
        }
        if (focus_ == hit) chrome_.FocusRing(R(box), Design::Radius::button);
        D2D1_MATRIX_3X2_F base; t->GetTransform(&base);
        if (rotation != 0) t->SetTransform(D2D1::Matrix3x2F::Rotation(rotation, {(box.left + box.right) / 2, (box.top + box.bottom) / 2}) * base);
        const float size = 14 * Motion::PressScale(p, Reduced(), enabled);
        chrome_.Icon(icon, R(box), size, enabled ? .7f + .3f * h : Design::Text::disabled);
        t->SetTransform(base);
    }
    // Opaque equivalent of `amount` white over the canvas: row fills never stack
    // translucent layers where the selection plate crosses a hovered row.
    static float Tint(float amount) { const float c = Design::Surface::canvas; return c + (1 - c) * amount; }
    static constexpr float hoverTint = .04f, pressTint = .03f, plateTint = .04f;
    // Row surface (first pass, under the plate): hover tint, deeper while pressed.
    void RowSurface(Design::Box box, int hit, Motion::Time now) {
        const auto v = Interaction(hit, now); const float amount = hoverTint * v.hover + pressTint * v.press;
        if (amount > .0005f) chrome_.Round(R(box), Design::Radius::control, Tint(amount));
    }
    void RowFocus(Design::Box box, int hit) { if (focus_ == hit) chrome_.FocusRing(R(box), Design::Radius::control); }
    void LabelText(const Slot& s) {
        chrome_.Text(s.text, {Layout::textLeft, s.top, Layout::rowRight, s.top + 20}, 14, .5f, false, true, -.14f);
    }
    // The engine's own favicon (assets\engines\<key>.png), dimmed like its name
    // until selected; a rounded tile with its initial if the file is missing.
    void EngineTile(int index, float y, float strength) {
        const auto box = Layout::Icon(y);
        const std::string key = BrowserSettings::Info(static_cast<Engine>(index)).key;
        if (auto* favicon = chrome_.AssetBitmap(L"engines\\" + std::wstring(key.begin(), key.end()) + L".png")) {
            chrome_.RenderTarget()->DrawBitmap(favicon, R(box), .75f + .25f * strength, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            return;
        }
        White(box, 4, .7f + .3f * strength);
        const std::wstring letter(1, BrowserSettings::Info(static_cast<Engine>(index)).name[0]);
        const float c = Design::Surface::canvas; chrome_.ink = D2D1::ColorF(c, c, c, 1);
        const float w = chrome_.TextWidth(letter, 11, true);
        chrome_.Text(letter, {(box.left + box.right - w) / 2 - .5f, box.top, box.right + 2, box.bottom}, 11, 1, false, true);
        chrome_.ink = D2D1::ColorF(1, 1, 1, 1);
    }
    void EngineRow(const Slot& s, Motion::Time now) {
        RowFocus(Layout::Row(s.top), s.hit);
        // The plate's proximity brightens the row it lands on.
        const float plateTop = PlateTop(now), closeness = Motion::Clamp(1 - std::abs(plateTop - (s.top + scroll_.Value(now))) / Layout::pitch);
        EngineTile(s.index, s.top, closeness);
        chrome_.Text(BrowserSettings::Info(static_cast<Engine>(s.index)).name, {Layout::nameLeft, s.top, Layout::slotRight - Layout::slot - 4, s.top + Layout::row}, 16, .7f + .3f * closeness, false, false, -.16f);
    }
    void MoreRow(const Slot& s, Motion::Time now) {
        RowFocus(Layout::Row(s.top), More);
        auto* t = chrome_.RenderTarget(); const auto icon = Layout::Icon(s.top);
        D2D1_MATRIX_3X2_F base; t->GetTransform(&base);
        t->SetTransform(D2D1::Matrix3x2F::Rotation(180.f * Motion::Clamp(more_.Value(now)), {(icon.left + icon.right) / 2, (icon.top + icon.bottom) / 2}) * base);
        // "Voir moins" cannot fold away the selected engine: it rests dimmed then.
        const float opacity = more_.Target() > .5f && state_ && !BrowserSettings::Primary(state_->engine) ? Design::Text::disabled : .7f;
        chrome_.Icon(L"caret", R(icon), 16, opacity);
        t->SetTransform(base);
        chrome_.Text(more_.Target() > .5f ? L"Voir moins" : L"Voir plus…", {Layout::nameLeft, s.top, Layout::slotRight, s.top + Layout::row}, 16, opacity, false, false, -.16f);
    }
    void SwitchRow(const Slot& s, Motion::Time now) {
        RowFocus(Layout::Row(s.top), s.hit);
        const int i = SwitchIndex(s.hit); const float on = Motion::Clamp(switches_[i].Value(now));
        chrome_.Text(s.text, {Layout::textLeft, s.top, Layout::Switch(s.top).left - 8, s.top + Layout::row}, 16, .7f + .3f * on, false, false, -.16f);
        // Track: 10% white off, 70% on. Knob: 70% white off, the canvas on.
        const auto v = Interaction(s.hit, now); const auto track = Layout::Switch(s.top);
        White(track, Layout::switchHeight / 2, .10f + .60f * on + .05f * v.hover * (1 - on));
        const float c = Design::Surface::canvas, offTrack = c + (1 - c) * .10f, offKnob = offTrack + (1 - offTrack) * .7f;
        const float knobGray = offKnob + (c - offKnob) * on;
        const float stretch = Reduced() ? 0 : Layout::knobStretch * Motion::Clamp(knobPress_[i].Value(now));
        const float travel = Layout::switchWidth - 2 * Layout::knobInset - Layout::knob;
        float left = track.left + Layout::knobInset + travel * on - stretch * on;
        const Design::Box knob{left, track.top + Layout::knobInset, left + Layout::knob + stretch, track.top + Layout::knobInset + Layout::knob};
        Fill(knob, Layout::knob / 2, knobGray, knobGray, knobGray, 1);
    }
    void FolderRow(const Slot& s, Motion::Time now) {
        RowFocus(Layout::Row(s.top, Layout::folderRow), Folder);
        chrome_.Text(L"Enregistrer dans", {Layout::textLeft, s.top + 8, Layout::Trailing(s.top).left - 8, s.top + 28}, 16, .7f, false, false, -.16f);
        const std::wstring folder = hooks_.downloadFolder ? hooks_.downloadFolder() : (state_ ? state_->downloadFolder : L"");
        chrome_.Text(FolderText(folder), {Layout::textLeft, s.top + 30, Layout::Trailing(s.top).left - 8, s.top + 46}, 14, .5f, false, false, -.14f);
        // The folder glyph answers the whole row (the row tint is the only fill): closed at
        // rest, it opens on hover. Cross-fade on the row's own hover tween (no extra frames),
        // the open folder tipping in from its bottom-left hinge (-10 deg, 90%) as it appears.
        const auto v = Interaction(Folder, now); const auto box = Layout::Trailing(s.top, Layout::folderRow);
        const float h = Motion::Clamp(v.hover);
        chrome_.FolderGlyph(R(box), 14 * Motion::PressScale(v.press, Reduced()), .7f + .3f * h, h, Reduced());
    }
    float PlateTop(Motion::Time now) const { return plate_.Value(now); }
    void Plate(Motion::Time now, float scroll) {
        // One plate for the selection: it glides between rows, the check rides on it.
        const float y = PlateTop(now) - scroll;
        chrome_.Round(R(Layout::Row(y)), Design::Radius::control, Tint(plateTint)); // opaque: covers a hover fill instead of adding to it
        chrome_.Icon(L"check", R(Layout::Trailing(y)), 14, .9f);
    }
    void Footer(Motion::Time now) {
        const float h = Height(), top = Layout::FooterTop(h);
        auto* t = chrome_.RenderTarget(); auto* brush = chrome_.SolidBrush();
        const float px = t->GetSize().width / std::max(1.f, static_cast<float>(t->GetPixelSize().width));
        chrome_.Color(1, .04f); t->FillRectangle(D2D1::RectF(Layout::rowLeft, top, Layout::rowRight, top + px), brush);
        // "Effacer les données…": the danger color, its own warm tint on hover.
        const float clear = Layout::ClearTop(h); const auto v = Interaction(ClearData, now); const auto row = Layout::Row(clear);
        if (v.hover > 0 || v.press > 0) Fill(row, Design::Radius::control, 1, .482f, .431f, .08f * v.hover + .05f * v.press);
        if (focus_ == ClearData) chrome_.FocusRing(R(row), Design::Radius::control);
        chrome_.ink = D2D1::ColorF(1, .482f, .431f, 1);
        chrome_.Text(L"Effacer les données…", {Layout::textLeft, clear, Layout::rowRight, clear + Layout::row}, 16, 1, false, false, -.16f);
        chrome_.ink = D2D1::ColorF(1, 1, 1, 1);
        // Version + check for updates. The glyph spins while a check runs.
        const auto version = hooks_.version ? hooks_.version() : Version{};
        const float y = Layout::VersionTop(h);
        chrome_.Text(version.text, {Layout::textLeft, y, Layout::Trailing(y, Layout::slot).left - 4, y + Layout::slot}, 14, .5f, true, false, -.14f);
        float turn = 0;
        // Counter-clockwise, the way the glyph's arrow points (like the toolbar's refresh turn).
        if (Spinning(now)) turn = -SpinDegrees(now);
        IconButton(L"reload", Layout::Trailing(y, Layout::slot), CheckUpdates, now, turn, version.canCheck && !version.checking);
    }
    void Paint(Motion::Time now) {
        auto* target = chrome_.RenderTarget(); auto* brush = chrome_.SolidBrush(); if (!target || !brush) return;
        if (!fade_) {
            const float c = Design::Surface::canvas;
            D2D1_GRADIENT_STOP stops[] = {{0, D2D1::ColorF(c, c, c, 0)}, {1, D2D1::ColorF(c, c, c, 1)}};
            if (FAILED(target->CreateGradientStopCollection(stops, 2, &stops_)) ||
                FAILED(target->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({0, 0}, {0, 1}), stops_, &fade_))) { Drop(fade_); Drop(stops_); }
        }
        const float c = Design::Surface::canvas, h = Height();
        target->Clear(D2D1::ColorF(c, c, c, 1));
        chrome_.keyboardModality = keyboard_;
        // Header
        chrome_.Text(L"Réglages", R(Layout::Title()), 16, .9f, false, true, -.16f);
        IconButton(L"close", Layout::CloseButton(), Close, now);
        // Content, clipped between the header and the footer.
        const float viewTop = Layout::viewTop - Layout::groupGap / 2, viewBottom = Layout::FooterTop(h);
        const float scroll = scroll_.Value(now);
        target->PushAxisAlignedClip(D2D1::RectF(0, viewTop, Layout::width, viewBottom), D2D1_ANTIALIAS_MODE_ALIASED);
        const auto slots = Slots(now);
        // Three passes: row fills, then the plate over them, then row contents.
        auto pass = [&](bool surfaces) { for (const auto& s : slots) {
            if (s.top + s.height < viewTop || s.top > viewBottom || s.opacity <= .002f || (surfaces && s.kind == Kind::Label)) continue;
            const bool clipped = s.clipTop > -1e5f, layered = s.opacity < .998f;
            if (clipped) target->PushAxisAlignedClip(D2D1::RectF(0, std::max(s.clipTop, viewTop), Layout::width, std::min(s.clipBottom, viewBottom)), D2D1_ANTIALIAS_MODE_ALIASED);
            if (layered) PushOpacity(s.opacity);
            if (surfaces) RowSurface(Layout::Row(s.top, s.height), s.hit, now);
            else switch (s.kind) {
            case Kind::Label: LabelText(s); break;
            case Kind::Engine: EngineRow(s, now); break;
            case Kind::More: MoreRow(s, now); break;
            case Kind::Switch: SwitchRow(s, now); break;
            case Kind::Folder: FolderRow(s, now); break;
            }
            if (layered) target->PopLayer();
            if (clipped) target->PopAxisAlignedClip();
        } };
        pass(true); Plate(now, scroll); pass(false);
        // Scroll edge effects: content dissolves into the canvas where more lies beyond.
        const float max = MaxScroll(now, false);
        if (fade_ && scroll > 0) {
            fade_->SetStartPoint({0, viewTop + Layout::topFade}); fade_->SetEndPoint({0, viewTop}); fade_->SetOpacity(Motion::Clamp(scroll / Layout::topFade));
            target->FillRectangle(D2D1::RectF(0, viewTop, Layout::width, viewTop + Layout::topFade), fade_);
        }
        if (fade_ && max - scroll > 0) {
            fade_->SetStartPoint({0, viewBottom - Layout::bottomFade}); fade_->SetEndPoint({0, viewBottom}); fade_->SetOpacity(Motion::Clamp((max - scroll) / Layout::topFade));
            target->FillRectangle(D2D1::RectF(0, viewBottom - Layout::bottomFade, Layout::width, viewBottom), fade_);
        }
        if (fade_) fade_->SetOpacity(1);
        target->PopAxisAlignedClip();
        Footer(now);
        // Leading edge: one physical pixel, a touch lighter than the canvas (as the download drawer).
        const float px = target->GetSize().width / std::max(1.f, static_cast<float>(target->GetPixelSize().width));
        chrome_.Color(1, .02f); target->FillRectangle(D2D1::RectF(0, 0, px, h), brush);
    }
    void PaintNow() {
        if (!chrome_.RenderTarget()) chrome_.Resize();
        auto* target = chrome_.RenderTarget(); if (!target) return;
        target->BeginDraw(); Paint(Motion::Now());
        if (target->EndDraw() == D2DERR_RECREATE_TARGET) { Drop(fade_); Drop(stops_); chrome_.RecreateTarget(); }
    }

    // ---- Input -----------------------------------------------------------------
    int HitAt(float x, float y) const {
        const float h = Height(); auto in = [x, y](Design::Box b) { return x >= b.left && x < b.right && y >= b.top && y < b.bottom; };
        if (in(Layout::CloseButton())) return Close;
        if (in(Layout::Trailing(Layout::VersionTop(h), Layout::slot))) return CheckUpdates;
        if (in(Layout::Row(Layout::ClearTop(h)))) return ClearData;
        if (y < Layout::viewTop - Layout::groupGap / 2 || y >= Layout::FooterTop(h)) return None;
        for (const auto& s : Slots(Motion::Now())) {
            if (s.kind == Kind::Label || s.opacity < .5f) continue;
            const auto row = Layout::Row(s.top, s.height);
            if (in(row) && y >= s.clipTop && y < s.clipBottom) return s.hit;
        }
        return None;
    }
    std::vector<int> FocusOrder() const {
        std::vector<int> order{Close};
        for (const auto& s : Slots(Motion::Now())) if (s.kind != Kind::Label && s.opacity > .5f) order.push_back(s.hit);
        order.push_back(ClearData);
        if (hooks_.version && hooks_.version().canCheck) order.push_back(CheckUpdates);
        return order;
    }
    void Interact(int hover, int pressed) {
        hover_ = hover; pressed_ = pressed; const auto now = Motion::Now();
        motion_.SetInteraction(hover_, pressed_ == hover_ ? pressed_ : 0, now);
        for (int i = 0; i < 3; ++i) {
            const int hit = i == 0 ? RestoreTabs : i == 1 ? AskWhere : ReduceMotion;
            knobPress_[i].Set(pressed_ == hit && hover_ == hit ? 1.f : 0.f, now, Motion::Milliseconds(0), Reduced() ? Motion::Milliseconds(0) : Timing::press, Downloads::Curves::emphasized);
        }
        Invalidate(); Schedule(now);
    }
    void Repoint() {
        if (!hwnd_ || !visible_) return;
        POINT p; GetCursorPos(&p); ScreenToClient(hwnd_, &p); RECT client; GetClientRect(hwnd_, &client);
        const float s = 96.f / Dpi(); Interact(PtInRect(&client, p) ? HitAt(p.x * s, p.y * s) : 0, pressed_);
    }
    void Schedule(Motion::Time now) { if (hooks_.schedule) hooks_.schedule(now); }
    void ScrollTo(float target, Motion::Time now) {
        target = std::clamp(target, 0.f, MaxScroll(now, true));
        scroll_.Set(target, now, Motion::Milliseconds(0), Reduced() ? Motion::Milliseconds(0) : Downloads::DrawerMotion::scroll);
    }
    void Reveal(int hit) {
        const auto now = Motion::Now(); const float scroll = scroll_.Target();
        for (const auto& s : ContentSlots(more_.Target(), scroll)) {
            if (s.hit != hit || s.kind == Kind::Label) continue;
            const float top = Layout::viewTop, bottom = ViewBottom() - 8;
            if (s.top < top) ScrollTo(scroll - (top - s.top), now);
            else if (s.top + s.height > bottom) ScrollTo(scroll + (s.top + s.height - bottom), now);
            return;
        }
    }
    // Modal work (folder picker, confirmation) must not close the drawer it came from.
    template<class F> void Modal(F f) { modal_ = true; f(); modal_ = false; if (visible_ && !closing_) { Activated(); Changed(); } }
    void Activate(int hit, bool keyboard) {
        if (!state_) return;
        const auto now = Motion::Now();
        if (hit == Close) { Hide(keyboard, true); return; }
        if (IsEngine(hit)) {
            const auto engine = static_cast<Engine>(hit - EngineBase);
            if (state_->engine != engine) {
                state_->engine = engine;
                plate_.Set(EngineTop(static_cast<int>(engine), more_.Target()), now, Motion::Milliseconds(0), Reduced() ? Motion::Milliseconds(0) : Timing::select, Timing::selectSpring);
                if (hooks_.changed) hooks_.changed(BrowserSettings::Change::Engine);
            }
        } else if (hit == More) {
            const float target = more_.Target() > .5f ? 0.f : 1.f;
            // Folding away the selected engine would hide the plate: keep its row.
            if (target == 0 && !BrowserSettings::Primary(state_->engine)) return;
            more_.Set(target, now, Motion::Milliseconds(0), Reduced() ? Motion::Milliseconds(0) : Downloads::DrawerMotion::fold, Downloads::DrawerMotion::foldSpring);
            plate_.Set(EngineTop(SelectedIndex(), target), now, Motion::Milliseconds(0), Reduced() ? Motion::Milliseconds(0) : Downloads::DrawerMotion::fold, Downloads::DrawerMotion::foldSpring);
            ScrollTo(scroll_.Target(), now);
        } else if (SwitchIndex(hit) >= 0) {
            bool& value = hit == RestoreTabs ? state_->restoreTabs : hit == AskWhere ? state_->askWhereToSave : state_->reduceMotion;
            value = !value;
            switches_[SwitchIndex(hit)].Set(value ? 1.f : 0.f, now, Motion::Milliseconds(0), Reduced() ? Motion::Milliseconds(0) : Timing::toggle, Timing::switchSpring);
            if (hooks_.changed) hooks_.changed(hit == RestoreTabs ? BrowserSettings::Change::RestoreTabs : hit == AskWhere ? BrowserSettings::Change::AskWhereToSave : BrowserSettings::Change::ReduceMotion);
            if (hit == ReduceMotion) SyncReducedMotion();
        } else if (hit == Folder) { if (hooks_.pickFolder) Modal([&] { hooks_.pickFolder(); }); }
        else if (hit == ClearData) { if (hooks_.clearData) Modal([&] { hooks_.clearData(); }); }
        else if (hit == CheckUpdates) { if (hooks_.version && hooks_.version().canCheck && hooks_.checkUpdates) hooks_.checkUpdates(); Changed(); }
        Repoint(); RefreshTips(); Schedule(Motion::Now());
    }
    void RefreshTips() {
        if (!tip_) return;
        for (auto id : tips_) { TOOLINFOW t{}; t.cbSize = sizeof(t); t.hwnd = hwnd_; t.uId = id; SendMessageW(tip_, TTM_DELTOOLW, 0, reinterpret_cast<LPARAM>(&t)); }
        tips_.clear(); tipTexts_.clear(); tipTexts_.reserve(4);
        auto add = [&](UINT_PTR id, Design::Box b, std::wstring text) {
            tipTexts_.push_back(std::move(text));
            TOOLINFOW t{}; t.cbSize = sizeof(t); t.uFlags = TTF_SUBCLASS; t.hwnd = hwnd_; t.uId = id;
            t.rect = {Px(b.left), Px(b.top), Px(b.right), Px(b.bottom)}; t.lpszText = const_cast<wchar_t*>(tipTexts_.back().c_str());
            SendMessageW(tip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&t)); tips_.push_back(id);
        };
        add(Close, Layout::CloseButton(), L"Fermer (Échap)");
        add(CheckUpdates, Layout::Trailing(Layout::VersionTop(Height()), Layout::slot), L"Rechercher une mise à jour");
        const auto now = Motion::Now();
        for (const auto& s : Slots(now)) if (s.kind == Kind::Folder && s.top >= Layout::viewTop - 1 && s.top + s.height <= ViewBottom())
            add(Folder, Layout::Row(s.top, s.height), hooks_.downloadFolder ? hooks_.downloadFolder() : L"Changer de dossier");
    }
    void Invalidate() { if (hwnd_ && visible_) InvalidateRect(hwnd_, nullptr, FALSE); }
    void Activated() { ShowWindow(hwnd_, SW_SHOW); SetForegroundWindow(hwnd_); SetFocus(hwnd_); }
    void SyncReducedMotion() { motion_.SetReducedMotion(hooks_.reducedMotion ? hooks_.reducedMotion() : Motion::PreferredReducedMotion()); }

    // ---- Window ----------------------------------------------------------------
    RECT OwnerClient() const { RECT r{}; GetClientRect(owner_, &r); MapWindowPoints(owner_, nullptr, reinterpret_cast<POINT*>(&r), 2); return r; }
    // The 268 DIP column docked to the owner's right edge under the toolbar. The
    // slide moves the window itself; the region clips it to the owner's client
    // area and rounds the corner it shares with it. DWM keeps content and
    // position in sync without a repaint.
    void ApplyGeometry(Motion::Time now, bool force = false) {
        if (!hwnd_) return;
        const RECT owner = OwnerClient();
        const int column = OwnerPx(Layout::width), top = owner.top + OwnerPx(Layout::top), height = std::max(1, static_cast<int>(owner.bottom - top));
        const int shown = static_cast<int>(std::lround(column * Motion::Clamp(slide_.Value(now))));
        if (!force && shown == appliedVisible_ && EqualRect(&owner, &appliedOwner_)) return;
        const bool resized = !EqualRect(&owner, &appliedOwner_);
        appliedVisible_ = shown; appliedOwner_ = owner;
        const RECT window{owner.right - shown, top, owner.right - shown + column, top + height};
        if (force || !EqualRect(&window, &appliedWindow_)) { SetWindowPos(hwnd_, nullptr, window.left, window.top, column, height, SWP_NOACTIVATE | SWP_NOZORDER); appliedWindow_ = window; }
        HRGN region = nullptr;
        if (shown > 0) {
            const int r = OwnerPx(20);
            region = IsZoomed(owner_) ? CreateRectRgn(0, 0, shown, height) : CreateRoundRectRgn(-r * 2, -r * 2, shown + 1, height + 1, r, r);
        } else region = CreateRectRgn(0, 0, 0, 0);
        SetWindowRgn(hwnd_, region, FALSE);
        if (resized) { scroll_.Snap(std::clamp(scroll_.Target(), 0.f, MaxScroll(now, true))); RefreshTips(); Invalidate(); }
    }
    void Finish() {
        closing_ = false; visible_ = false; appliedVisible_ = -1; appliedWindow_ = {};
        ShowWindow(hwnd_, SW_HIDE);
    }
    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<Drawer*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (msg == WM_NCCREATE) { self = static_cast<Drawer*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); self->hwnd_ = h; }
        return self ? self->Handle(h, msg, w, l) : DefWindowProcW(h, msg, w, l);
    }
    LRESULT Handle(HWND h, UINT msg, WPARAM w, LPARAM l) {
        const float scale = 96.f / Dpi();
        switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); PaintNow(); EndPaint(h, &ps); return 0; }
        case WM_SIZE: chrome_.Resize(); Invalidate(); return 0;
        case WM_DPICHANGED: ApplyGeometry(Motion::Now(), true); return 0;
        case WM_MOUSEACTIVATE: return MA_ACTIVATE;
        case WM_ACTIVATE: if (LOWORD(w) == WA_INACTIVE && !modal_) Hide(false, true); return 0;
        case WM_MOUSEMOVE: {
            if (!tracking_) { TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, h, 0}; TrackMouseEvent(&track); tracking_ = true; }
            const int hit = HitAt(GET_X_LPARAM(l) * scale, GET_Y_LPARAM(l) * scale);
            if (hit != hover_) Interact(hit, pressed_);
            return 0;
        }
        case WM_MOUSELEAVE: tracking_ = false; Interact(0, 0); return 0;
        case WM_LBUTTONDOWN: { const int hit = HitAt(GET_X_LPARAM(l) * scale, GET_Y_LPARAM(l) * scale); keyboard_ = false; if (hit) { SetCapture(h); Interact(hit, hit); } return 0; }
        case WM_LBUTTONUP: {
            const int hit = HitAt(GET_X_LPARAM(l) * scale, GET_Y_LPARAM(l) * scale), was = pressed_;
            if (GetCapture() == h) ReleaseCapture();
            Interact(hit, 0); if (was && was == hit) Activate(hit, false);
            return 0;
        }
        case WM_CAPTURECHANGED: if (pressed_) Interact(hover_, 0); return 0;
        case WM_MOUSEWHEEL: { const auto now = Motion::Now(); ScrollTo(scroll_.Target() - GET_WHEEL_DELTA_WPARAM(w) / 120.f * Layout::pitch * 1.5f, now); Repoint(); Schedule(now); return 0; }
        case WM_KEYDOWN: {
            if (w == VK_ESCAPE) { Hide(true, true); return 0; }
            if (w == VK_TAB || w == VK_DOWN || w == VK_UP) {
                keyboard_ = true; const auto order = FocusOrder();
                const bool back = w == VK_UP || (w == VK_TAB && GetKeyState(VK_SHIFT) < 0);
                auto it = std::find(order.begin(), order.end(), focus_);
                int index = it == order.end() ? (back ? static_cast<int>(order.size()) - 1 : 0) : static_cast<int>(it - order.begin()) + (back ? -1 : 1);
                index = (index + static_cast<int>(order.size())) % static_cast<int>(order.size());
                focus_ = order[index]; Reveal(focus_); Interact(hover_, pressed_); return 0;
            }
            if ((w == VK_RETURN || w == VK_SPACE) && focus_) { keyboard_ = true; Activate(focus_, true); return 0; }
            return 0;
        }
        }
        return DefWindowProcW(h, msg, w, l);
    }
public:
    // Recently closed by deactivation: the click that caused it must not reopen it.
    ULONGLONG hiddenAt = 0;
    Drawer() { slide_.Snap(0); more_.Snap(0); }
    ~Drawer() { Dispose(); }
    bool Create(HINSTANCE instance, HWND owner, const std::wstring& root, BrowserSettings::State* state, Hooks hooks) {
        owner_ = owner; state_ = state; hooks_ = std::move(hooks);
        motion_.EnablePolish(); chrome_.SetMotion(&motion_); chrome_.textCacheLimit = 256;
        WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = instance; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.lpszClassName = L"LisiereSettingsDrawer";
        RegisterClassW(&wc);
        CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"Réglages", WS_POPUP, 0, 0, Px(Layout::width), Px(400), owner, nullptr, instance, this);
        if (!hwnd_) return false;
        tip_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0, 0, 0, hwnd_, nullptr, instance, nullptr);
        const int square = 1; DwmSetWindowAttribute(hwnd_, 33, &square, sizeof(square)); // DWMWCP_DONOTROUND: the region shapes it
        const COLORREF none = 0xFFFFFFFE; DwmSetWindowAttribute(hwnd_, 34, &none, sizeof(none));
        return SUCCEEDED(chrome_.Init(hwnd_, root));
    }
    // Release Direct2D/WIC before CoUninitialize; the owner destroys the window.
    void Dispose() { Drop(fade_); Drop(stops_); chrome_.Dispose(); hwnd_ = tip_ = nullptr; tips_.clear(); visible_ = false; }
    HWND Window() const { return hwnd_; }
    bool Visible() const { return visible_; }
    bool Closing() const { return closing_; }
    bool Open() const { return visible_ && !closing_; }
    // Slides in from the window's right edge; reopening mid-exit continues from
    // where the drawer is on screen.
    void Show() {
        if (!hwnd_ || !state_) return;
        SyncReducedMotion();
        auto now = Motion::Now(); const bool reduced = Reduced();
        if (!visible_) {
            slide_.Snap(0); scroll_.Snap(0); focus_ = 0; keyboard_ = false; hover_ = pressed_ = 0; motion_.ClearInteractions();
            // The selected engine is always on screen: unfold when it sits behind "Voir plus…".
            more_.Snap(BrowserSettings::Primary(state_->engine) ? 0.f : 1.f);
            plate_.Snap(EngineTop(SelectedIndex(), more_.Target()));
            for (int i = 0; i < 3; ++i) { switches_[i].Snap(SwitchValue(i == 0 ? RestoreTabs : i == 1 ? AskWhere : ReduceMotion) ? 1.f : 0.f); knobPress_[i].Snap(0); }
        }
        { const auto version = hooks_.version ? hooks_.version() : Version{}; SetChecking(version.checking, now); }
        closing_ = false; visible_ = true;
        // Shown at its current extent (nothing yet on a fresh open), painted, then
        // repainted by Frame() while the slide uncovers it.
        ApplyGeometry(now, true);
        SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        PaintNow(); ValidateRect(hwnd_, nullptr); Activated();
        // The slide's clock starts once the painted frame is on screen. Reopening
        // mid-exit retargets from where the drawer is.
        now = Motion::Now();
        slide_.Set(1, now, Motion::Milliseconds(0), reduced ? Motion::Milliseconds(0) : Downloads::DrawerMotion::enter, Downloads::DrawerMotion::enterSpring);
        ApplyGeometry(now); Invalidate();
        Repoint(); RefreshTips(); Schedule(now);
    }
    // Leaves along the path it came in on; `animate` false hides at once.
    void Hide(bool keyboard, bool animate) {
        if (!visible_ || closing_) { if (visible_ && !animate) Finish(); return; }
        hiddenAt = GetTickCount64();
        if (GetCapture() == hwnd_) ReleaseCapture();
        motion_.ClearInteractions(); hover_ = pressed_ = 0; tracking_ = false;
        auto now = Motion::Now(); closing_ = true;
        slide_.Set(0, now, Motion::Milliseconds(0), animate && !Reduced() ? Downloads::DrawerMotion::exit : Motion::Milliseconds(0), Downloads::DrawerMotion::exitSpring);
        if (!slide_.Moving(now)) Finish();
        if (hooks_.closed) hooks_.closed(keyboard);
        if (visible_) Schedule(now);
    }
    void Toggle() { if (Open()) Hide(false, true); else Show(); }
    // Owner moved, resized or changed DPI.
    void Reposition() { if (visible_) ApplyGeometry(Motion::Now()); }
    // The state or the version status changed outside the drawer: follow it.
    void Changed() {
        if (!hwnd_ || !visible_ || !state_) return;
        const auto now = Motion::Now(); const bool reduced = Reduced();
        for (int i = 0; i < 3; ++i) switches_[i].Set(SwitchValue(i == 0 ? RestoreTabs : i == 1 ? AskWhere : ReduceMotion) ? 1.f : 0.f, now, Motion::Milliseconds(0), reduced ? Motion::Milliseconds(0) : Timing::toggle, Timing::switchSpring);
        if (!BrowserSettings::Primary(state_->engine) && more_.Target() < .5f) more_.Set(1, now, Motion::Milliseconds(0), reduced ? Motion::Milliseconds(0) : Downloads::DrawerMotion::fold, Downloads::DrawerMotion::foldSpring);
        plate_.Set(EngineTop(SelectedIndex(), more_.Target()), now, Motion::Milliseconds(0), reduced ? Motion::Milliseconds(0) : Timing::select, Timing::selectSpring);
        const bool checking = hooks_.version && hooks_.version().checking;
        SetChecking(checking, now);
        auto order = FocusOrder(); if (focus_ && std::find(order.begin(), order.end(), focus_) == order.end()) focus_ = 0;
        RefreshTips(); Invalidate(); Schedule(now);
    }
    void SetReducedMotion() { SyncReducedMotion(); Invalidate(); }
    Motion::Wake NextWake(Motion::Time now) const {
        if (!visible_) return {};
        bool moving = slide_.Moving(now) || scroll_.Moving(now) || more_.Moving(now) || plate_.Moving(now) || Spinning(now);
        for (auto& lane : switches_) moving = moving || lane.Moving(now);
        for (auto& lane : knobPress_) moving = moving || lane.Moving(now);
        return Motion::Earlier(motion_.NextWake(now), moving ? Motion::Frame() : Motion::Wake{});
    }
    // Display-paced frame from the host clock; also paints the frame after motion ends.
    void Frame(Motion::Time now) {
        if (!visible_) return;
        ApplyGeometry(now);
        if (closing_ && !slide_.Moving(now)) { Finish(); return; }
        // A window keeps only what it is painted with while its region shows it:
        // the entering slide repaints as it uncovers; the exit moves finished pixels.
        bool content = (!closing_ && slide_.Moving(now)) || scroll_.Moving(now) || more_.Moving(now) || plate_.Moving(now) || Spinning(now) || motion_.NextWake(now).delay.has_value();
        for (auto& lane : switches_) content = content || lane.Moving(now);
        for (auto& lane : knobPress_) content = content || lane.Moving(now);
        if (content || animatingPainted_) Invalidate();
        // Rows moved under a still pointer (fold, scroll): hover follows the layout.
        if (!content && animatingPainted_) { RefreshTips(); Repoint(); }
        animatingPainted_ = content;
    }
};
} // namespace SettingsUI
} // namespace NativeChrome
