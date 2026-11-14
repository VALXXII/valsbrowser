#pragma once
// Launcher (40 DIP, bottom-right of the page area) and its card.
// Paper « MCP - Design UI UX »:
//   Launcher at rest WO9-0: 40x40 at right 16 / bottom 16 of the page, canvas #171717,
//     border 1 #FFFFFF1F, radius 10, mark 18x17 #FFFFFFCC. UIA name « Annotations ».
//   Launcher running WOD-0: border #3E67FF66, mark 14x13 white, ring 28 accent (turning),
//     badge: pill h 18 min 18, padding 5, border 2 #F4F4F3, radius 9, accent, at
//     (right -6, top -6), « 1 » 11/14 semibold tabular white.
//   Card WOL-0 (agent working) / WVJ-0 (no agent) at right 16 / bottom 64: 320 wide, raised
//     #292929, border 1 #FFFFFF0F, radius 10, padding 12, gap 8 (WVJ: 10).
//     WOL: ring 16 accent + « Prise par » [avatar 16] « Opus 5.5 » 14/18 medium #FFFFFFE6,
//          « Depuis 2 min » 14/18 #FFFFFF99, close 24 (glyph 14 at 70 %); note 14/20;
//          « Boutique » 12/16 #FFFFFF99 + « Ouvrir » (h 32, padding 12, #FFFFFF0F, 14/16 medium).
//     WVJ: check 16 + « Annotation ajoutée » 13/18 medium #FFFFFFE6 « · Boutique » #FFFFFF99,
//          close; « Aucun agent ne la lit encore… » 14/20; « Plus tard » (#333333, flex 1) and
//          « Connecter un agent » (accent, flex 1.4), h 32 radius 6 14/18 medium.
//     « Annotation ajoutée » (agent connected): the WVJ header row alone; it leaves by itself
//          after 4 s (one-shot timer, paused while hovered or focused; it has no action).
// Both are CHILD layered windows of the browser window, above the WebView: per-pixel alpha,
// so the transparent corners and the space around the badge never take a click.
// Paper's drop shadows (0 4 12 #0000002E, 0 8 24 #00000040) are not drawn: repository rule
// « zero drop shadow » for our popups (src/native-ui.h).
// Motion: the card is born from the launcher (origin = the launcher's centre line at the
// card's bottom edge, scale 0.96 -> 1, 10 DIP lift from below, opacity), leaves the same way
// in 150 ms; the launcher fades/scales in from its centre. Press 0.96. Reduced motion:
// opacity only; the running ring stops turning (static arc + badge stay as cues).
#include "annotation-kit.h"

namespace NativeChrome { namespace Annotations {
enum class LauncherState { Rest, Working };
enum class CardKind { None, Working, NoAgent, Added, Resolved };   // Resolved (T3): « Résolue par … » + summary
struct CardContent {
    CardKind kind = CardKind::None;
    std::wstring agent, since, note, project;   // « Opus 5.5 », « Depuis 2 min », note text, « Boutique »
    std::wstring monogram;                       // avatar fallback (« C »); `avatar` pixels win when given
    std::wstring headline;                       // Resolved: « Résolue par » / « Question de » / « Écartée par »
    Shot avatar;
};
enum CardHit { CardNone = 0, CardClose = 1, CardOpen = 2, CardLater = 3, CardConnect = 4 };
namespace LauncherSpec {
inline constexpr float size = 40, radius = 10, inset = 16, badgeOut = 6, cardW = 320, cardBottom = 64, cardRadius = 10, pad = 12;
inline constexpr std::uint32_t badgeRing = 0xF4F4F3FF;   // Paper: the page colour behind the badge
}
struct LauncherVisual { LauncherState state = LauncherState::Rest; int count = 0; Motion::InteractionVisual iv; bool focus = false, keyboard = false, reduced = false, expanded = false; float spinner = 0; std::uint32_t ring = LauncherSpec::badgeRing; };
// Launcher window: 46x46 DIP; the button occupies (0, 6, 40, 46); the badge overhangs.
inline D2D1_RECT_F LauncherBox() { return {0, LauncherSpec::badgeOut, LauncherSpec::size, LauncherSpec::badgeOut + LauncherSpec::size}; }
inline void PaintLauncher(Ink& ink, const LauncherVisual& v, float opacity = 1, float scale = 1) {
    using namespace LauncherSpec; const auto b = LauncherBox(); auto* t = ink.Target();
    D2D1_MATRIX_3X2_F base; t->GetTransform(&base);
    const float s = scale * Motion::PressScale(v.iv.press, v.reduced);
    t->SetTransform(D2D1::Matrix3x2F::Scale(s, s, {(b.left + b.right) / 2, (b.top + b.bottom) / 2}) * base);
    ID2D1Layer* group = nullptr; const bool pushed = ink.PushOpacity(opacity, group);
    const float c = Design::Surface::canvas + (Design::Surface::tabHover - Design::Surface::canvas) * v.iv.hover;
    ink.Fill(b, radius, {c, c, c, 1});
    const bool working = v.state == LauncherState::Working;
    ink.Stroke(b, radius, working ? Hex(0x3E67FF66) : Hex(0xFFFFFF1F), 1);
    const float cx = (b.left + b.right) / 2, cy = (b.top + b.bottom) / 2;
    if (working) {
        ink.Icon(Glyphs::mark, {cx - 7, cy - 6.5f, cx + 7, cy + 6.5f}, {1, 1, 1, 1});
        ink.Icon(Glyphs::ring, {cx - 14, cy - 14, cx + 14, cy + 14}, Accent(), v.spinner);
    } else ink.Icon(Glyphs::mark, {cx - 9, cy - 8.5f, cx + 9, cy + 8.5f}, Hex(0xFFFFFFCC));
    if (v.focus) FocusRing(ink, b, radius, v.keyboard);
    if (working && v.count > 0) {
        const std::wstring n = v.count > 99 ? L"99+" : std::to_wstring(v.count);
        Font f = F(11, 14, 600); f.tabular = true;
        const float w = std::max(18.f, ink.Width(n, f) + 14), right = b.right + badgeOut, top = b.top - badgeOut;
        const D2D1_RECT_F pill{right - w, top, right, top + 18};
        ink.Fill(pill, 9, Hex(v.ring)); ink.Fill(Inflate(pill, -2), 7, Accent());
        ink.Text(n, f, pill, {1, 1, 1, 1}, false, Align::Center, true);
    }
    ink.PopOpacity(group); (void)pushed;
    t->SetTransform(base);
}
struct CardLayout { float height = 0; D2D1_RECT_F header{}, close{}, body{}, footer{}, open{}, later{}, connect{}, projectLabel{}; };
inline const wchar_t* NoAgentBody() { return L"Aucun agent ne la lit encore. Connectez Claude, Codex ou un autre client MCP en une minute."; }
inline CardLayout LayoutCard(Ink& ink, const CardContent& c) {
    using namespace LauncherSpec; CardLayout l; const float x = 1 + pad, r = cardW - 1 - pad, gap = (c.kind == CardKind::Working || c.kind == CardKind::Resolved) ? 8.f : 10.f;
    float y = 1 + pad;
    l.header = {x, y, r, y + 24}; l.close = {r - 24, y, r, y + 24}; y += 24;
    if (c.kind == CardKind::Working || c.kind == CardKind::Resolved) {
        const float h = std::min(4, ink.Lines(c.note, F(14, 20), r - x)) * 20.f;
        y += gap; l.body = {x, y, r, y + h}; y += h + gap;
        l.footer = {x, y, r, y + 32};
        const float ow = 12 + ink.Width(L"Ouvrir", F(14, 16, 500)) + 12; l.open = {r - ow, y, r, y + 32};
        l.projectLabel = {x, y, l.open.left - 8, y + 32}; y += 32;
    } else if (c.kind == CardKind::NoAgent) {
        const float h = ink.Height(NoAgentBody(), F(14, 20), r - x);
        y += gap; l.body = {x, y, r, y + h}; y += h + gap;
        const float avail = r - x - 8, a = avail / 2.4f;
        l.later = {x, y, x + a, y + 32}; l.connect = {l.later.right + 8, y, r, y + 32}; y += 32;
    }
    l.height = y + pad + 1; return l;
}
inline int CardHitAt(const CardLayout& l, const CardContent& c, float x, float y) {
    if (Inside(l.close, x, y)) return CardClose;
    if ((c.kind == CardKind::Working || c.kind == CardKind::Resolved) && Inside(l.open, x, y)) return CardOpen;
    if (c.kind == CardKind::NoAgent && Inside(l.later, x, y)) return CardLater;
    if (c.kind == CardKind::NoAgent && Inside(l.connect, x, y)) return CardConnect;
    return CardNone;
}
struct CardVisual { std::map<int, Motion::InteractionVisual> iv; int focus = 0; bool keyboard = false, reduced = false; float spinner = 0;
    Motion::InteractionVisual V(int id) const { auto it = iv.find(id); return it == iv.end() ? Motion::InteractionVisual{} : it->second; } };
inline void PaintCard(Ink& ink, const CardContent& c, const CardLayout& l, const CardVisual& v) {
    using namespace LauncherSpec; const D2D1_RECT_F all{0, 0, cardW, l.height};
    ink.Fill(all, cardRadius, Hex(Ink_::raised));
    const float cy = (l.header.top + l.header.bottom) / 2;
    float x = l.header.left;
    if (c.kind == CardKind::Working || c.kind == CardKind::Resolved) {
        if (c.kind == CardKind::Resolved) ink.Icon(Glyphs::check, {x, cy - 8, x + 16, cy + 8}, Accent()); else ink.Icon(Glyphs::ring, {x, cy - 8, x + 16, cy + 8}, Accent(), v.spinner);
        x += 16 + 8;
        const std::wstring lead = c.kind == CardKind::Resolved ? (c.headline.empty() ? std::wstring(L"Résolue par") : c.headline) : std::wstring(L"Prise par");
        const Font f = F(14, 18, 500); const float sinceW = ink.Width(c.since, F(14, 18)), right = l.close.left - 8;
        const float pw = ink.Width(lead, f);
        ink.Text(lead, f, {x, l.header.top, x + pw + 1, l.header.bottom}, Hex(0xFFFFFFE6), false, Align::Leading, true); x += pw + 4;
        if (c.avatar.width) { ink.Thumbnail(&c.avatar, {x, cy - 8, x + 16, cy + 8}, 3); x += 16 + 4; }
        else if (!c.monogram.empty()) { ink.Fill({x, cy - 8, x + 16, cy + 8}, 4, Hex(0xFFFFFF14)); ink.Text(c.monogram, F(10, 12, 500), {x, cy - 8, x + 16, cy + 8}, {1, 1, 1, 1}, false, Align::Center, true); x += 16 + 4; }
        ink.Text(c.agent, f, {x, l.header.top, right - sinceW - 8, l.header.bottom}, Hex(0xFFFFFFE6), false, Align::Leading, true);
        ink.Text(c.since, F(14, 18), {right - sinceW, l.header.top, right, l.header.bottom}, Hex(0xFFFFFF99), false, Align::Trailing, true);
        auto* t = ink.Target(); t->PushAxisAlignedClip(l.body, D2D1_ANTIALIAS_MODE_ALIASED);
        ink.Text(c.note, F(14, 20), l.body, Hex(0xFFFFFFE6), true); t->PopAxisAlignedClip();
        ink.Text(c.project, F(12, 16), l.projectLabel, Hex(0xFFFFFF99), false, Align::Leading, true);
        const auto iv = v.V(CardOpen);
        { ScaleScope s(ink.Target(), l.open, Motion::PressScale(iv.press, v.reduced));
          ink.Fill(l.open, 6, {1, 1, 1, (15 / 255.f) + .05f * iv.hover + .04f * iv.press});
          ink.Text(L"Ouvrir", F(14, 16, 500), l.open, {1, 1, 1, 1}, false, Align::Center, true);
          if (v.focus == CardOpen) FocusRing(ink, l.open, 6, v.keyboard); }
    } else {
        ink.Icon(Glyphs::check, {x, cy - 8, x + 16, cy + 8}, {1, 1, 1, 1}); x += 16 + 8;
        ink.Spans({{L"Annotation ajoutée", Hex(0xFFFFFFE6), 500}, {c.project.empty() ? L"" : L" · " + c.project, Hex(0xFFFFFF99), 400}}, F(13, 18), {x, l.header.top, l.close.left - 8, l.header.bottom});
        if (c.kind == CardKind::NoAgent) {
            ink.Text(NoAgentBody(), F(14, 20), l.body, Hex(0xFFFFFFE6), true);
            const auto lv = v.V(CardLater), cv = v.V(CardConnect);
            { ScaleScope s(ink.Target(), l.later, Motion::PressScale(lv.press, v.reduced));
              ink.Fill(l.later, 6, Hex(Ink_::hover)); ink.Fill(l.later, 6, {1, 1, 1, .05f * lv.hover + .04f * lv.press});
              ink.Text(L"Plus tard", F(14, 18, 500), l.later, {1, 1, 1, 1}, false, Align::Center, true);
              if (v.focus == CardLater) FocusRing(ink, l.later, 6, v.keyboard); }
            { ScaleScope s(ink.Target(), l.connect, Motion::PressScale(cv.press, v.reduced));
              ink.Fill(l.connect, 6, Accent()); ink.Fill(l.connect, 6, {1, 1, 1, .08f * cv.hover + .06f * cv.press});
              ink.Text(L"Connecter un agent", F(14, 18, 500), l.connect, {1, 1, 1, 1}, false, Align::Center, true);
              if (v.focus == CardConnect) FocusRing(ink, l.connect, 6, v.keyboard); }
        }
    }
    {   const auto iv = v.V(CardClose);
        ScaleScope s(ink.Target(), l.close, Motion::PressScale(iv.press, v.reduced));
        if (iv.hover > 0) ink.Fill(l.close, 4, {1, 1, 1, .08f * iv.hover});
        const float ccx = (l.close.left + l.close.right) / 2, ccy = (l.close.top + l.close.bottom) / 2;
        ink.Icon(Glyphs::close, {ccx - 7, ccy - 7, ccx + 7, ccy + 7}, {1, 1, 1, .7f + .3f * iv.hover});
        if (v.focus == CardClose) FocusRing(ink, l.close, 4, v.keyboard); }
    ink.Stroke(all, cardRadius, Hex(0xFFFFFF0F), 1);
}
inline std::wstring CardAnnouncement(const CardContent& c) {
    switch (c.kind) {
    case CardKind::Working: return L"Annotation prise par " + c.agent;
    case CardKind::NoAgent: return L"Annotation ajoutée" + (c.project.empty() ? std::wstring() : L" · " + c.project) + L". Aucun agent connecté.";
    case CardKind::Added: return L"Annotation ajoutée" + (c.project.empty() ? std::wstring() : L" · " + c.project);
    case CardKind::Resolved: return (c.headline.empty() ? std::wstring(L"Résolue par") : c.headline) + L" " + c.agent + L" : " + c.note;
    default: return {};
    }
}

struct LauncherHooks {
    std::function<void()> toggle;      // launcher clicked / invoked
    std::function<void()> open;        // « Ouvrir » (working card)
    std::function<void()> later;       // « Plus tard »
    std::function<void()> connect;     // « Connecter un agent » (the host opens the onboarding)
    std::function<void(bool keyboard)> dismissed; // card closed by the user or by its own dwell
    std::function<void()> focusOut;    // Échap/Tab leaves the launcher: the host takes focus back
    std::function<void(const std::wstring&)> announce;
};
class Launcher : public Animator::Client {
    static constexpr long long ButtonKey = 1, CardKey = 10, CardBodyKey = 11;
    static constexpr UINT_PTR DwellTimer = 7;
    HINSTANCE instance_ = nullptr; HWND parent_ = nullptr; std::wstring root_; LauncherHooks hooks_;
    Layer button_, card_; Presence buttonPresence_, cardPresence_; Motion::State motion_;
    RECT page_{}; LauncherState state_ = LauncherState::Rest; int count_ = 0; bool reduced_ = false;
    CardContent content_; CardLayout cardLayout_;
    bool buttonHover_ = false, buttonPressed_ = false, buttonFocus_ = false, keyboard_ = false;
    int cardHover_ = 0, cardPressed_ = 0, cardFocus_ = 0; bool dwellArmed_ = false;
    std::uint32_t ring_ = LauncherSpec::badgeRing;

    float Scale() const { const UINT d = GetDpiForWindow(parent_); return (d ? d : 96) / 96.f; }
    int Px(float dip) const { return int(std::lround(dip * Scale())); }
    RECT ButtonRect() const {   // parent-client px of the 46x46 window
        const int r = page_.right - Px(LauncherSpec::inset), b = page_.bottom - Px(LauncherSpec::inset);
        return {r - Px(LauncherSpec::size), b - Px(LauncherSpec::size + LauncherSpec::badgeOut), r + Px(LauncherSpec::badgeOut), b};
    }
    RECT CardRect() const {     // card + the lift travel below it
        const int r = page_.right - Px(LauncherSpec::inset), b = page_.bottom - Px(LauncherSpec::cardBottom);
        return {r - Px(LauncherSpec::cardW), b - Px(cardLayout_.height), r, b + Px(Spec::panelLift)};
    }
    void RenderButton(Motion::Time now) {
        if (!button_.Window()) return;
        LauncherVisual v; v.state = state_; v.count = count_; v.iv = motion_.Visual(ButtonKey, now); v.focus = buttonFocus_; v.keyboard = keyboard_;
        v.reduced = reduced_; v.expanded = cardPresence_.IsOpen(); v.spinner = SpinnerAngle(now, reduced_); v.ring = ring_;
        const Pose p = buttonPresence_.Sample(now, reduced_);
        button_.paint = [v, p](Ink& ink, float, float) { PaintLauncher(ink, v, p.opacity, p.scale); };
        button_.Present(ButtonRect(), true);
    }
    CardVisual Visual(Motion::Time now) const {
        CardVisual v; for (int id = CardClose; id <= CardConnect; ++id) v.iv[id] = motion_.Visual(100 + id, now);
        v.focus = cardFocus_; v.keyboard = keyboard_; v.reduced = reduced_; v.spinner = SpinnerAngle(now, reduced_); return v;
    }
    void RenderCard(Motion::Time now) {
        if (!card_.Window()) return;
        const Pose p = cardPresence_.Sample(now, reduced_, 0, Spec::panelLift);
        const CardContent c = content_; const CardLayout l = cardLayout_; const CardVisual v = Visual(now);
        // Origin: the launcher's centre line on the card's bottom edge.
        const D2D1_POINT_2F origin{LauncherSpec::cardW - LauncherSpec::size / 2, l.height};
        card_.paint = [c, l, v, p, origin](Ink& ink, float, float) {
            auto* t = ink.Target(); D2D1_MATRIX_3X2_F base; t->GetTransform(&base);
            t->SetTransform(D2D1::Matrix3x2F::Scale(p.scale, p.scale, origin) * D2D1::Matrix3x2F::Translation(p.dx, p.dy) * base);
            ID2D1Layer* group = nullptr; const bool pushed = ink.PushOpacity(p.opacity, group);
            PaintCard(ink, c, l, v); if (pushed) ink.PopOpacity(group);
            t->SetTransform(base);
        };
        card_.Present(CardRect(), true);
    }
    void Interact() {
        const auto now = Motion::Now();
        // Interaction ids: ButtonKey for the launcher, 100 + CardHit for card buttons.
        const int hover = buttonHover_ ? int(ButtonKey) : cardHover_ ? 100 + cardHover_ : 0;
        const int press = buttonPressed_ && buttonHover_ ? int(ButtonKey) : (cardPressed_ && cardPressed_ == cardHover_) ? 100 + cardPressed_ : 0;
        motion_.SetInteraction(hover, press, now);
        RenderButton(now); if (cardPresence_.Visible(now)) RenderCard(now);
        Animator::Get().Request(this);
    }
    void Announce(const std::wstring& text) { if (text.empty()) return; if (hooks_.announce) hooks_.announce(text); else card_.uia.Notify(CardKey, text.c_str(), L"AnnotationCard"); }
    void ArmDwell() {
        if (!card_.Window()) return; KillTimer(card_.Window(), DwellTimer); dwellArmed_ = false;
        if (content_.kind == CardKind::Added && cardPresence_.IsOpen() && !cardHover_ && !cardFocus_) { SetTimer(card_.Window(), DwellTimer, Spec::addedDwellMs, nullptr); dwellArmed_ = true; }
    }
    void ActivateCard(int hit, bool keyboard) {
        switch (hit) {
        case CardClose: HideCard(); if (hooks_.dismissed) hooks_.dismissed(keyboard); if (keyboard) FocusLauncher(); break;
        case CardOpen: if (hooks_.open) hooks_.open(); break;
        case CardLater: HideCard(); if (hooks_.later) hooks_.later(); if (keyboard) FocusLauncher(); break;
        case CardConnect: if (hooks_.connect) hooks_.connect(); break;
        }
    }
    std::vector<int> CardOrder() const {
        std::vector<int> o;
        if (content_.kind == CardKind::Working || content_.kind == CardKind::Resolved) o = {CardOpen, CardClose};
        else if (content_.kind == CardKind::NoAgent) o = {CardLater, CardConnect, CardClose};
        else o = {CardClose};
        return o;
    }
    bool ButtonHandle(UINT m, WPARAM w, LPARAM l, LRESULT& r) {
        const float s = 1 / button_.Scale(); const float x = GET_X_LPARAM(l) * s, y = GET_Y_LPARAM(l) * s;
        const bool in = Inside(LauncherBox(), x, y);
        switch (m) {
        case WM_MOUSEMOVE: { TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, button_.Window(), 0}; TrackMouseEvent(&t); if (in != buttonHover_) { buttonHover_ = in; Interact(); } r = 0; return true; }
        case WM_MOUSELEAVE: buttonHover_ = false; Interact(); r = 0; return true;
        case WM_LBUTTONDOWN: if (in) { keyboard_ = false; buttonPressed_ = true; SetCapture(button_.Window()); Interact(); } r = 0; return true;
        case WM_LBUTTONUP: { const bool was = buttonPressed_; buttonPressed_ = false; if (GetCapture() == button_.Window()) ReleaseCapture(); Interact(); if (was && in && hooks_.toggle) hooks_.toggle(); r = 0; return true; }
        case WM_SETFOCUS: buttonFocus_ = true; RenderButton(Motion::Now()); return false;
        case WM_KILLFOCUS: buttonFocus_ = false; RenderButton(Motion::Now()); return false;
        case WM_GETDLGCODE: r = DLGC_WANTALLKEYS; return true;
        case WM_KEYDOWN:
            keyboard_ = true;
            if (w == VK_RETURN || w == VK_SPACE) { if (hooks_.toggle) hooks_.toggle(); r = 0; return true; }
            if (w == VK_TAB && cardPresence_.IsOpen()) { FocusCard(); r = 0; return true; }
            if (w == VK_ESCAPE || w == VK_TAB) { if (cardPresence_.IsOpen() && w == VK_ESCAPE) { HideCard(); if (hooks_.dismissed) hooks_.dismissed(true); } else if (hooks_.focusOut) hooks_.focusOut(); r = 0; return true; }
            return false;
        case WM_APP + 1: if (hooks_.toggle) hooks_.toggle(); r = 0; return true;
        }
        return false;
    }
    bool CardHandle(UINT m, WPARAM w, LPARAM l, LRESULT& r) {
        const float s = 1 / card_.Scale(); const float x = GET_X_LPARAM(l) * s, y = GET_Y_LPARAM(l) * s;
        switch (m) {
        case WM_MOUSEMOVE: { TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, card_.Window(), 0}; TrackMouseEvent(&t);
            const int hit = cardPresence_.IsOpen() ? CardHitAt(cardLayout_, content_, x, y) : 0;
            if (dwellArmed_) { KillTimer(card_.Window(), DwellTimer); dwellArmed_ = false; }
            if (hit != cardHover_) { cardHover_ = hit; Interact(); } if (!cardHover_) cardHover_ = 0;
            r = 0; return true; }
        case WM_MOUSELEAVE: cardHover_ = 0; Interact(); ArmDwell(); r = 0; return true;
        case WM_LBUTTONDOWN: { keyboard_ = false; cardPressed_ = CardHitAt(cardLayout_, content_, x, y); if (cardPressed_) SetCapture(card_.Window()); Interact(); r = 0; return true; }
        case WM_LBUTTONUP: { const int hit = CardHitAt(cardLayout_, content_, x, y), was = cardPressed_; cardPressed_ = 0; if (GetCapture() == card_.Window()) ReleaseCapture(); Interact(); if (was && was == hit) ActivateCard(hit, false); r = 0; return true; }
        case WM_SETFOCUS: if (!cardFocus_) { auto o = CardOrder(); cardFocus_ = o.front(); } KillTimer(card_.Window(), DwellTimer); RenderCard(Motion::Now()); return false;
        case WM_KILLFOCUS: cardFocus_ = 0; RenderCard(Motion::Now()); ArmDwell(); return false;
        case WM_GETDLGCODE: r = DLGC_WANTALLKEYS; return true;
        case WM_KEYDOWN: {
            keyboard_ = true; auto o = CardOrder();
            if (w == VK_TAB) { auto it = std::find(o.begin(), o.end(), cardFocus_); int i = it == o.end() ? 0 : int(it - o.begin()) + (GetKeyState(VK_SHIFT) < 0 ? -1 : 1);
                if (i < 0 || i >= int(o.size())) { FocusLauncher(); } else { cardFocus_ = o[size_t(i)]; RenderCard(Motion::Now()); } r = 0; return true; }
            if (w == VK_ESCAPE) { HideCard(); if (hooks_.dismissed) hooks_.dismissed(true); FocusLauncher(); r = 0; return true; }
            if ((w == VK_RETURN || w == VK_SPACE) && cardFocus_) { ActivateCard(cardFocus_, true); r = 0; return true; }
            return false; }
        case WM_TIMER: if (w == DwellTimer) { KillTimer(card_.Window(), DwellTimer); dwellArmed_ = false; if (content_.kind == CardKind::Added) { HideCard(); if (hooks_.dismissed) hooks_.dismissed(false); } r = 0; return true; } return false;
        case WM_APP + 2: ActivateCard(int(w), true); r = 0; return true;
        }
        return false;
    }
    std::vector<Accessibility::Node> ButtonNodes() {
        std::vector<Accessibility::Node> n; if (!buttonPresence_.IsOpen()) return n;
        Accessibility::Node b; b.key = ButtonKey; b.name = L"Annotations"; b.automationId = L"AnnotationLauncher";
        b.help = state_ == LauncherState::Working ? (count_ == 1 ? L"1 annotation en cours" : std::to_wstring(count_) + L" annotations en cours") : L"Suivi des annotations";
        b.expand = cardPresence_.IsOpen() ? 2 : 1; b.bounds = button_.Screen(LauncherBox()); n.push_back(b); return n;
    }
    std::vector<Accessibility::Node> CardNodes() {
        std::vector<Accessibility::Node> n; if (!cardPresence_.IsOpen()) return n;
        Accessibility::Node g; g.key = CardKey; g.role = Accessibility::Role::Group; g.focusable = false; g.automationId = L"AnnotationCard";
        g.name = content_.kind == CardKind::Working ? L"Annotation en cours" : content_.kind == CardKind::Resolved ? L"Annotation traitée" : L"Annotation ajoutée"; GetWindowRect(card_.Window(), &g.bounds); n.push_back(g);
        Accessibility::Node t; t.key = CardBodyKey; t.parent = CardKey; t.role = Accessibility::Role::Text; t.focusable = false; t.name = CardAnnouncement(content_);
        t.value = (content_.kind == CardKind::Working || content_.kind == CardKind::Resolved) ? content_.note : content_.kind == CardKind::NoAgent ? NoAgentBody() : L""; t.bounds = card_.Screen(cardLayout_.header); n.push_back(t);
        auto add = [&](int hit, const wchar_t* name, D2D1_RECT_F r) { Accessibility::Node b; b.key = 100 + hit; b.parent = CardKey; b.name = name; b.bounds = card_.Screen(r); b.automationId = name; n.push_back(b); };
        if (content_.kind == CardKind::Working || content_.kind == CardKind::Resolved) add(CardOpen, L"Ouvrir", cardLayout_.open);
        if (content_.kind == CardKind::NoAgent) { add(CardLater, L"Plus tard", cardLayout_.later); add(CardConnect, L"Connecter un agent", cardLayout_.connect); }
        add(CardClose, L"Fermer", cardLayout_.close);
        return n;
    }
    bool Ensure() {
        if (button_.Window()) return true;
        if (!Ink::Shared(root_)) return false;
        Layer::Options b; b.child = true; b.tabStop = true; b.title = L"Annotations";
        Layer::Options c; c.child = true; c.tabStop = true; c.title = L"Annotation";
        if (!button_.Create(instance_, parent_, b) || !card_.Create(instance_, parent_, c)) return false;
        button_.handle = [this](UINT m, WPARAM w, LPARAM l, LRESULT& r) { return ButtonHandle(m, w, l, r); };
        card_.handle = [this](UINT m, WPARAM w, LPARAM l, LRESULT& r) { return CardHandle(m, w, l, r); };
        Accessibility::Host bh; bh.window = button_.Window(); bh.nodes = [this] { return ButtonNodes(); };
        bh.focused = [this]() -> long long { return buttonFocus_ ? ButtonKey : 0; };
        bh.invoke = [this](long long) { PostMessageW(button_.Window(), WM_APP + 1, 0, 0); };
        bh.expand = [this](long long, bool) { PostMessageW(button_.Window(), WM_APP + 1, 0, 0); };
        button_.uia.Attach(std::move(bh));
        Accessibility::Host ch; ch.window = card_.Window(); ch.nodes = [this] { return CardNodes(); };
        ch.focused = [this]() -> long long { return cardFocus_ ? 100 + cardFocus_ : 0; };
        ch.invoke = [this](long long key) { PostMessageW(card_.Window(), WM_APP + 2, WPARAM(key - 100), 0); };
        card_.uia.Attach(std::move(ch));
        motion_.EnablePolish();
        return true;
    }
public:
    ~Launcher() { if (Animator::Exists()) Animator::Get().Forget(this); }
    void Init(HINSTANCE instance, HWND parent, std::wstring root, LauncherHooks hooks) { instance_ = instance; parent_ = parent; root_ = std::move(root); hooks_ = std::move(hooks); }
    bool Created() const { return button_.Window() != nullptr; }
    bool Visible() const { return buttonPresence_.IsOpen(); }
    bool CardVisible() const { return cardPresence_.IsOpen(); }
    HWND LauncherWindow() const { return button_.Window(); }
    HWND CardWindow() const { return card_.Window(); }
    // Page area (the WebView bounds) in the parent's client pixels; re-anchors both surfaces.
    void Place(RECT page) { page_ = page; const auto now = Motion::Now(); if (buttonPresence_.Visible(now)) RenderButton(now); if (cardPresence_.Visible(now)) RenderCard(now); }
    // The page colour behind the badge (Paper #F4F4F3); optional.
    void SetBadgeRing(std::uint32_t rgba) { ring_ = rgba; }
    bool Show(LauncherState state, int count, bool reduced) {
        if (!Ensure()) return false;
        const auto now = Motion::Now(); state_ = state; count_ = count; reduced_ = reduced; motion_.SetReducedMotion(reduced);
        buttonPresence_.Show(now); RenderButton(now); Animator::Get().Request(this);
        if (button_.uia.Listening()) button_.uia.ChildrenChanged(ButtonKey);
        return true;
    }
    void Hide(bool animate = true) {
        if (!button_.Window()) return; const auto now = Motion::Now();
        HideCard(animate); buttonPresence_.Hide(now, animate); buttonHover_ = buttonPressed_ = false;
        if (!buttonPresence_.Visible(now)) button_.Hide(); else Animator::Get().Request(this);
    }
    void ShowCard(const CardContent& content) {
        if (!Ensure() || content.kind == CardKind::None) return;
        const auto now = Motion::Now(); const bool same = cardPresence_.IsOpen() && content.kind == content_.kind;
        content_ = content; cardLayout_ = LayoutCard(*Ink::Instance(), content_); cardHover_ = cardPressed_ = 0;
        cardPresence_.Show(now); RenderCard(now); RenderButton(now); Animator::Get().Request(this);
        ArmDwell();
        if (!same || content.kind == CardKind::Added) Announce(CardAnnouncement(content_));
        if (card_.uia.Listening()) card_.uia.ChildrenChanged(CardKey);
    }
    void HideCard(bool animate = true) {
        if (!card_.Window() || !cardPresence_.IsOpen()) return;
        const auto now = Motion::Now(); KillTimer(card_.Window(), DwellTimer); dwellArmed_ = false;
        cardPresence_.Hide(now, animate); cardFocus_ = cardHover_ = cardPressed_ = 0;
        if (GetFocus() == card_.Window()) FocusLauncher();
        if (!cardPresence_.Visible(now)) card_.Hide(); else Animator::Get().Request(this);
        RenderButton(now);
    }
    void FocusLauncher() { if (button_.Window() && buttonPresence_.IsOpen()) { keyboard_ = true; SetFocus(button_.Window()); } }
    void FocusCard() { if (card_.Window() && cardPresence_.IsOpen()) { keyboard_ = true; cardFocus_ = CardOrder().front(); SetFocus(card_.Window()); RenderCard(Motion::Now()); } }
    bool Frame(Motion::Time now) override {
        bool more = false;
        if (buttonPresence_.Visible(now)) {
            RenderButton(now);
            more |= buttonPresence_.Moving(now) || (state_ == LauncherState::Working && !reduced_);
        } else button_.Hide();
        if (cardPresence_.Visible(now)) {
            RenderCard(now);
            more |= cardPresence_.Moving(now) || (cardPresence_.IsOpen() && content_.kind == CardKind::Working && !reduced_);
        } else card_.Hide();
        more |= motion_.NextWake(now).delay.has_value();
        return more;
    }
    // ---- tests / QA
    Layer& ButtonLayerForTest() { return button_; }
    Layer& CardLayerForTest() { return card_; }
    Presence& CardPresenceForTest() { return cardPresence_; }
    const CardLayout& CardLayoutForTest() const { return cardLayout_; }
    bool DwellArmedForTest() const { return dwellArmed_; }
    std::vector<Accessibility::Node> ButtonNodesForTest() { return ButtonNodes(); }
    std::vector<Accessibility::Node> CardNodesForTest() { return CardNodes(); }
    void SetStateForTest(LauncherState s, int count) { state_ = s; count_ = count; RenderButton(Motion::Now()); }
};
}} // namespace NativeChrome::Annotations
