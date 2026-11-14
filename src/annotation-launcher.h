#pragma once
// Launcher (40 DIP, bottom-right of the page area), its cards and its history panel (V2).
// Paper « MCP - Design UI UX »: 3.1 10GJ-0 (ajoutée, aucun agent), 3.2 10H4-0 (l'agent s'en occupe),
// 3.3 10HO-0 (l'agent a terminé), 4.1 10Z6-0 (historique ouvert); « Spéc · Historique » 1167-0.
//   Launcher 40x40 at right 16 / bottom 16, canvas #171717, radius 10, border 1: rest #FFFFFF1F,
//     an agent working #3E67FF66, history open #3E67FF99. Icon: the tray (« tiroir », stroked
//     outline 18, #FFFFFFE6); while an agent works: the ring 24 accent, turning. Badge at
//     (right -6, top -6): h 18, min 18, padding 5, radius 6, 11/14 600 tabular white; accent
//     while an agent works, raised #292929 otherwise. UIA « Annotations » (+ « n en cours »).
//   Cards 320 at right 16 / bottom 64 (8 above the launcher): canvas, border 1 #FFFFFF14,
//     radius 12. Header padding 14/10/4/14, gap 8: 3.1 accent disc 18 + check 11 white, 3.2/3.3
//     the agent's logo 16; title 14/20 600 white (« · depuis 2 min » 400 #FFFFFF99); close 26
//     (glyph 14 at 70 %). Body padding 0/14/12/40: 13/19 #FFFFFFB3, two lines at most (« … »).
//     Actions padding 0/10/10, gap 8: raised h 32 radius 6, 13/16 medium white (3.1 tree 16 at
//     60 % + « Connecter un agent »; 3.2 « Voir l'annotation »; 3.3 « Recharger la page » +
//     « Rouvrir »). « Annotation ajoutée » with an agent connected: the header alone, leaves by
//     itself after 4 s (one-shot timer, paused under the pointer / focus).
// All CHILD layered windows of the browser window, above the WebView: per-pixel alpha, so the
// transparent corners and the space around the badge never take a click. Paper's drop shadows
// are not drawn (repository rule « zero drop shadow », src/native-ui.h).
// Motion: cards and the history panel are born from the launcher (origin bottom-right: the
// launcher's centre), 0.96 -> 1 + opacity in 200 ms cubic-bezier(.23,1,.32,1), back in 150 ms
// by the same path; a card that changes state (en cours -> terminé) cross-fades its content
// in place (0.3 s critical spring) while its height follows. Press 0.96. Reduced motion:
// opacity only; the ring stops turning (the badge stays as the cue).
#include "annotation-kit.h"
#include "annotation-history.h"

namespace NativeChrome { namespace Annotations {
enum class LauncherState { Rest, Working };
// V2 cards: NoAgent (3.1), Added (header only, agent connected), Working (3.2), Done (3.3).
// Resolved is the V1 name of Done (kept so older callers compile).
// V3 (Paper 5.5): Read (« Lue par Claude », no claim yet) and Unhandled (given back with a reason).
enum class CardKind { None, Working, NoAgent, Added, Done, Read, Unhandled, Resolved = Done };
struct CardContent {
    CardKind kind = CardKind::None;
    std::wstring agent;        // « Claude » (display name)
    AgentKind agentKind = AgentKind::Claude;
    std::wstring since;        // « depuis 2 min » (3.2)
    std::wstring note;         // 3.2: the note (quoted); 3.3: the summary when `summary` is empty
    std::wstring summary;      // 3.3: what the agent did; V3 Unhandled: the agent's reason
    std::wstring id;           // the annotation (for the hooks)
    std::wstring project, monogram, headline; Shot avatar;   // V1, unused in V2
};
enum CardHit { CardNone = 0, CardClose = 1, CardOpen = 2, CardLater = 3, CardConnect = 4, CardReload = 5, CardReopen = 6 };
namespace LauncherSpec {
inline constexpr float size = 40, radius = 10, inset = 16, badgeOut = 6, cardW = 320, cardBottom = 64, cardRadius = 12;
inline constexpr std::uint32_t badgeRing = 0xF4F4F3FF;   // V1 (no ring around the badge in V2)
}
struct LauncherVisual {
    LauncherState state = LauncherState::Rest; int count = 0; Motion::InteractionVisual iv; bool focus = false, keyboard = false, reduced = false, expanded = false;
    float spinner = 0; std::uint32_t ring = LauncherSpec::badgeRing;
};
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
    ink.Stroke(b, radius, v.expanded ? Hex(0x3E67FF99) : working ? Hex(0x3E67FF66) : Hex(0xFFFFFF1F), 1);
    const float cx = (b.left + b.right) / 2, cy = (b.top + b.bottom) / 2;
    if (working) ink.Icon(Glyphs::ring, {cx - 12, cy - 12, cx + 12, cy + 12}, Accent(), v.spinner);
    else ink.IconStroke(Glyphs::tray, {cx - 9, cy - 9, cx + 9, cy + 9}, Hex(0xFFFFFFE6), 2);
    if (v.focus) FocusRing(ink, b, radius, v.keyboard);
    if (v.count > 0) {
        const std::wstring n = v.count > 99 ? L"99+" : std::to_wstring(v.count);
        Font f = F(11, 14, 600); f.tabular = true;
        const float w = std::max(18.f, ink.Width(n, f) + 10), right = b.right + badgeOut, top = b.top - badgeOut;
        const D2D1_RECT_F pill{right - w, top, right, top + 18};
        ink.Fill(pill, 6, working ? Accent() : Hex(Ink_::raised));
        ink.Text(n, f, pill, {1, 1, 1, 1}, false, Align::Center, true);
    }
    ink.PopOpacity(group); (void)pushed;
    t->SetTransform(base);
}
struct CardLayout { float height = 0; D2D1_RECT_F header{}, close{}, body{}, actions{}, primary{}, secondary{}; std::wstring bodyText; };
inline const wchar_t* NoAgentBody() { return L"Aucun agent ne la lit encore."; }
inline std::wstring CardTitle(const CardContent& c) {
    const std::wstring a = c.agent.empty() ? std::wstring(AgentName(c.agentKind)) : c.agent;
    switch (c.kind) { case CardKind::Working: return a + L" s'en occupe"; case CardKind::Done: return a + L" a terminé"; case CardKind::Read: return L"Lue par " + a;
                      case CardKind::Unhandled: return a + L" n'a pas pu la traiter"; default: return L"Annotation ajoutée"; }
}
inline CardLayout LayoutCard(Ink& ink, const CardContent& c) {
    using namespace LauncherSpec; CardLayout l; const float x0 = 1, inner = cardW - 2;
    float y = 1;
    l.header = {x0 + 14, y + 14, x0 + inner - 10, y + 14 + 26}; l.close = {l.header.right - 26, l.header.top, l.header.right, l.header.bottom};
    y += 14 + 26 + (c.kind == CardKind::Added ? 14 : 4);
    if (c.kind == CardKind::Added) { l.height = y + 1; return l; }
    const float bw = inner - 40 - 14; const Font f = F(13, 19);
    if (c.kind == CardKind::NoAgent) l.bodyText = NoAgentBody();
    else if (c.kind == CardKind::Working || c.kind == CardKind::Read) l.bodyText = ink.Clamp(c.note, f, bw, 2, L"« ", L" »");
    else if (c.kind == CardKind::Unhandled) l.bodyText = ink.Clamp(c.summary.empty() ? c.note : c.summary, f, bw, 2, L"« ", L" »");
    else l.bodyText = ink.Clamp(c.summary.empty() ? c.note : c.summary, f, bw, 2);
    const float bh = ink.Height(l.bodyText, f, bw);
    l.body = {x0 + 40, y, x0 + 40 + bw, y + bh}; y += bh + 12;
    l.actions = {x0 + 10, y, x0 + inner - 10, y + 32};
    if (c.kind == CardKind::Done) { const float w = (l.actions.right - l.actions.left - 8) / 2; l.primary = {l.actions.left, y, l.actions.left + w, y + 32}; l.secondary = {l.primary.right + 8, y, l.actions.right, y + 32}; }
    else l.primary = l.actions;
    y += 32 + 10;
    l.height = y + 1; return l;
}
inline int CardHitAt(const CardLayout& l, const CardContent& c, float x, float y) {
    if (Inside(l.close, x, y)) return CardClose;
    if (c.kind == CardKind::Added) return CardNone;
    if (Inside(l.primary, x, y)) return c.kind == CardKind::NoAgent ? CardConnect : c.kind == CardKind::Done ? CardReload : CardOpen;
    if (c.kind == CardKind::Done && Inside(l.secondary, x, y)) return CardReopen;
    return CardNone;
}
struct CardVisual { std::map<int, Motion::InteractionVisual> iv; int focus = 0; bool keyboard = false, reduced = false;
    Motion::InteractionVisual V(int id) const { auto it = iv.find(id); return it == iv.end() ? Motion::InteractionVisual{} : it->second; } };
inline void CardButton(Ink& ink, const CardVisual& v, int id, D2D1_RECT_F r, std::vector<Piece> content) {
    const auto iv = v.V(id); ScaleScope s(ink.Target(), r, Motion::PressScale(iv.press, v.reduced));
    ink.Fill(r, 6, RaisedFill(iv)); Pieces(ink, content, r);
    if (v.focus == id) FocusRing(ink, r, 6, v.keyboard);
}
// The card's content (no surface): cross-faded on a state change.
inline void PaintCardContent(Ink& ink, const CardContent& c, const CardLayout& l, const CardVisual& v) {
    const float cy = (l.header.top + l.header.bottom) / 2; float x = l.header.left;
    const bool agent = c.kind == CardKind::Working || c.kind == CardKind::Done || c.kind == CardKind::Read || c.kind == CardKind::Unhandled;
    if (c.kind == CardKind::Unhandled) ink.Icon(Glyphs::warningCircle, {x, cy - 8, x + 16, cy + 8}, Hex(Ink_::amber));
    else if (agent) Logo(ink, c.agentKind, {x, cy - 8, x + 16, cy + 8});
    else { ink.Disc(x + 9, cy, 9, Accent()); ink.Icon(Glyphs::check, {x + 3.5f, cy - 5.5f, x + 14.5f, cy + 5.5f}, {1, 1, 1, 1}); }
    x += (agent ? 16 : 18) + 8;
    const D2D1_RECT_F title{x, l.header.top, l.close.left - 8, l.header.bottom};
    if (c.kind == CardKind::Working && !c.since.empty()) ink.Spans({{CardTitle(c), {1, 1, 1, 1}, 600}, {L" · " + c.since, Hex(0xFFFFFF99), 400}}, F(14, 20), title);
    else ink.Text(CardTitle(c), F(14, 20, 600), title, {1, 1, 1, 1}, false, Align::Leading, true);
    CloseButton(ink, l.close, 14, v.V(CardClose), v.focus == CardClose, v.keyboard, v.reduced);
    if (c.kind == CardKind::Added) return;
    ink.Text(l.bodyText, F(13, 19), l.body, Hex(0xFFFFFFB3), true);
    if (c.kind == CardKind::NoAgent) CardButton(ink, v, CardConnect, l.primary, {PIcon(Glyphs::tree, 16, {1, 1, 1, .6f}), PText(L"Connecter un agent", F(13, 16, 500), {1, 1, 1, 1})});
    else if (c.kind == CardKind::Working || c.kind == CardKind::Read || c.kind == CardKind::Unhandled) CardButton(ink, v, CardOpen, l.primary, {PText(L"Voir l'annotation", F(13, 16, 500), {1, 1, 1, 1})});
    else { CardButton(ink, v, CardReload, l.primary, {PText(L"Recharger la page", F(13, 16, 500), {1, 1, 1, 1})}); CardButton(ink, v, CardReopen, l.secondary, {PText(L"Rouvrir", F(13, 16, 500), {1, 1, 1, 1})}); }
}
inline void PaintCard(Ink& ink, const CardContent& c, const CardLayout& l, const CardVisual& v) {
    const D2D1_RECT_F all{0, 0, LauncherSpec::cardW, l.height};
    ink.Fill(all, LauncherSpec::cardRadius, Hex(Ink_::canvas));
    PaintCardContent(ink, c, l, v);
    ink.Stroke(all, LauncherSpec::cardRadius, Hex(0xFFFFFF14), 1);
}
inline std::wstring CardAnnouncement(const CardContent& c) {
    switch (c.kind) {
    case CardKind::Working: return CardTitle(c) + (c.since.empty() ? L"" : L", " + c.since);
    case CardKind::NoAgent: return std::wstring(L"Annotation ajoutée. ") + NoAgentBody();
    case CardKind::Added: return L"Annotation ajoutée";
    case CardKind::Done: return CardTitle(c) + L" : " + (c.summary.empty() ? c.note : c.summary);
    case CardKind::Read: return CardTitle(c);
    case CardKind::Unhandled: return CardTitle(c) + (c.summary.empty() ? L"" : L" : " + c.summary);
    default: return {};
    }
}

struct LauncherHooks {
    std::function<void()> toggle;      // launcher clicked / invoked: the host reads its store and calls ShowHistory (or HideHistory)
    std::function<void()> open;        // 3.2 « Voir l'annotation » (V1 « Ouvrir »)
    std::function<void()> later;       // V1 « Plus tard » (no longer on a card)
    std::function<void()> connect;     // 3.1 « Connecter un agent » (the host opens ConnectModal)
    std::function<void()> reload;      // 3.3 « Recharger la page »
    std::function<void()> reopen;      // 3.3 « Rouvrir »
    std::function<void(bool keyboard)> dismissed; // card closed by the user or by its own dwell
    std::function<void()> focusOut;    // Échap/Tab leaves the launcher: the host takes focus back
    std::function<void(const std::wstring&)> announce;
    HistoryHooks history;              // the history panel (4.1 – 4.3)
};
// « Spéc · Entrée du mode » (Paper 5.2): the launcher appears 0.9 -> 1 + opacity in 200 ms; it goes back into its
// corner by the same path in 150 ms. Reduced motion: opacity only.
inline Timing LauncherTiming() { Timing t; t.enter = 200; t.exit = Spec::panelOut; t.from = .9f; t.to = .9f; return t; }
class Launcher : public Animator::Client {
    static constexpr long long ButtonKey = 1, CardKey = 10, CardBodyKey = 11;
    static constexpr UINT_PTR DwellTimer = 7;
    HINSTANCE instance_ = nullptr; HWND parent_ = nullptr; std::wstring root_; LauncherHooks hooks_;
    Layer button_, card_; HistoryPanel history_;
    Presence buttonPresence_{LauncherTiming()}, cardPresence_{PanelTiming()}; Motion::State motion_;
    bool dormant_ = false;   // V3: the mode is off => every launcher window is destroyed as soon as nothing shows
    RECT page_{}; LauncherState state_ = LauncherState::Rest; int count_ = 0; bool reduced_ = false;
    CardContent content_, before_; CardLayout cardLayout_, beforeLayout_; Crossfade cardFade_; Ramp cardHeight_; int cardStamp_ = 0;
    bool buttonHover_ = false, buttonPressed_ = false, buttonFocus_ = false, keyboard_ = false;
    int cardHover_ = 0, cardPressed_ = 0, cardFocus_ = 0; bool dwellArmed_ = false;
    std::uint32_t ring_ = LauncherSpec::badgeRing;

    float Scale() const { const UINT d = GetDpiForWindow(parent_); return (d ? d : 96) / 96.f; }
    int Px(float dip) const { return int(std::lround(dip * Scale())); }
    RECT ButtonRect() const {   // parent-client px of the 46x46 window
        const int r = page_.right - Px(LauncherSpec::inset), b = page_.bottom - Px(LauncherSpec::inset);
        return {r - Px(LauncherSpec::size), b - Px(LauncherSpec::size + LauncherSpec::badgeOut), r + Px(LauncherSpec::badgeOut), b};
    }
    float CardH(Motion::Time now) const { return cardHeight_.Value(now); }
    RECT CardRect(Motion::Time now) const {   // the card + 4 DIP of room below for the scale origin
        const int r = page_.right - Px(LauncherSpec::inset), b = page_.bottom - Px(LauncherSpec::cardBottom);
        return {r - Px(LauncherSpec::cardW), b - Px(std::max(CardH(now), cardLayout_.height)), r, b + Px(4)};
    }
    void RenderButton(Motion::Time now) {
        if (!button_.Window()) return;
        LauncherVisual v; v.state = state_; v.count = count_; v.iv = motion_.Visual(ButtonKey, now); v.focus = buttonFocus_; v.keyboard = keyboard_;
        v.reduced = reduced_; v.expanded = history_.Visible(); v.spinner = SpinnerAngle(now, reduced_); v.ring = ring_;
        const Pose p = buttonPresence_.Sample(now, reduced_);
        button_.paint = [v, p](Ink& ink, float, float) { PaintLauncher(ink, v, p.opacity, p.scale); };
        button_.Present(ButtonRect(), true);
    }
    CardVisual Visual(Motion::Time now) const {
        CardVisual v; for (int id = CardClose; id <= CardReopen; ++id) v.iv[id] = motion_.Visual(100 + id, now);
        v.focus = cardFocus_; v.keyboard = keyboard_; v.reduced = reduced_; return v;
    }
    void RenderCard(Motion::Time now) {
        if (!card_.Window()) return;
        const Pose p = cardPresence_.Sample(now, reduced_);
        const RECT rect = CardRect(now); const float winH = (rect.bottom - rect.top) / card_.Scale(), h = CardH(now), top = winH - 4 - h;
        const CardContent c = content_, b = before_; const CardLayout l = cardLayout_, bl = beforeLayout_; const CardVisual v = Visual(now);
        const bool fading = cardFade_.Moving(now) && cardFade_.Previous() >= 0; const float in = cardFade_.Incoming(now), out = cardFade_.Outgoing(now);
        // Origin: the launcher's centre (bottom-right, below the card).
        const D2D1_POINT_2F origin{LauncherSpec::cardW - LauncherSpec::size / 2, h + 8 + LauncherSpec::size / 2};
        card_.paint = [=](Ink& ink, float, float) {
            auto* t = ink.Target(); D2D1_MATRIX_3X2_F base; t->GetTransform(&base);
            t->SetTransform(D2D1::Matrix3x2F::Translation(0, top) * base);
            Posed(ink, p, origin, [&] {
                const D2D1_RECT_F all{0, 0, LauncherSpec::cardW, h};
                ink.Fill(all, LauncherSpec::cardRadius, Hex(Ink_::canvas));
                ID2D1Layer* layer = nullptr; ID2D1Geometry* clip = nullptr; PushRoundClip(ink, all, LauncherSpec::cardRadius, layer, clip);
                if (fading && out > 0) { ID2D1Layer* g = nullptr; const bool pushed = ink.PushOpacity(out, g); PaintCardContent(ink, b, bl, CardVisual{}); if (pushed) ink.PopOpacity(g); }
                { ID2D1Layer* g = nullptr; const bool pushed = ink.PushOpacity(fading ? in : 1.f, g); PaintCardContent(ink, c, l, v); if (pushed) ink.PopOpacity(g); }
                PopClip(ink, layer, clip);
                ink.Stroke(all, LauncherSpec::cardRadius, Hex(0xFFFFFF14), 1);
            });
            t->SetTransform(base);
        };
        card_.Present(rect, true);
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
        case CardReload: if (hooks_.reload) hooks_.reload(); break;
        case CardReopen: if (hooks_.reopen) hooks_.reopen(); break;
        }
    }
    std::vector<int> CardOrder() const {
        switch (content_.kind) {
        case CardKind::NoAgent: return {CardConnect, CardClose};
        case CardKind::Working: case CardKind::Read: case CardKind::Unhandled: return {CardOpen, CardClose};
        case CardKind::Done: return {CardReload, CardReopen, CardClose};
        default: return {CardClose};
        }
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
            if (w == VK_TAB && history_.Visible()) { history_.Focus(); r = 0; return true; }
            if (w == VK_TAB && cardPresence_.IsOpen()) { FocusCard(); r = 0; return true; }
            if (w == VK_ESCAPE && history_.Visible()) { HideHistory(); r = 0; return true; }
            if (w == VK_ESCAPE || w == VK_TAB) { if (cardPresence_.IsOpen() && w == VK_ESCAPE) { HideCard(); if (hooks_.dismissed) hooks_.dismissed(true); } else if (hooks_.focusOut) hooks_.focusOut(); r = 0; return true; }
            return false;
        case WM_APP + 1: if (hooks_.toggle) hooks_.toggle(); r = 0; return true;
        }
        return false;
    }
    bool CardHandle(UINT m, WPARAM w, LPARAM l, LRESULT& r) {
        const auto now = Motion::Now(); const RECT rect = CardRect(now); const float s = 1 / card_.Scale();
        const float top = (rect.bottom - rect.top) * s - 4 - CardH(now);
        const float x = GET_X_LPARAM(l) * s, y = GET_Y_LPARAM(l) * s - top;
        switch (m) {
        case WM_MOUSEMOVE: { TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, card_.Window(), 0}; TrackMouseEvent(&t);
            const int hit = cardPresence_.IsOpen() ? CardHitAt(cardLayout_, content_, x, y) : 0;
            if (dwellArmed_) { KillTimer(card_.Window(), DwellTimer); dwellArmed_ = false; }
            if (hit != cardHover_) { cardHover_ = hit; Interact(); }
            r = 0; return true; }
        case WM_MOUSELEAVE: cardHover_ = 0; Interact(); ArmDwell(); r = 0; return true;
        case WM_LBUTTONDOWN: { keyboard_ = false; cardPressed_ = CardHitAt(cardLayout_, content_, x, y); if (cardPressed_) SetCapture(card_.Window()); Interact(); r = 0; return true; }
        case WM_LBUTTONUP: { const int hit = CardHitAt(cardLayout_, content_, x, y), was = cardPressed_; cardPressed_ = 0; if (GetCapture() == card_.Window()) ReleaseCapture(); Interact(); if (was && was == hit) ActivateCard(hit, false); r = 0; return true; }
        case WM_SETFOCUS: if (!cardFocus_) cardFocus_ = CardOrder().front(); KillTimer(card_.Window(), DwellTimer); dwellArmed_ = false; RenderCard(now); return false;
        case WM_KILLFOCUS: cardFocus_ = 0; RenderCard(now); ArmDwell(); return false;
        case WM_GETDLGCODE: r = DLGC_WANTALLKEYS; return true;
        case WM_KEYDOWN: {
            keyboard_ = true; auto o = CardOrder();
            if (w == VK_TAB) { auto it = std::find(o.begin(), o.end(), cardFocus_); int i = it == o.end() ? 0 : int(it - o.begin()) + (GetKeyState(VK_SHIFT) < 0 ? -1 : 1);
                if (i < 0 || i >= int(o.size())) FocusLauncher(); else { cardFocus_ = o[size_t(i)]; RenderCard(now); if (card_.uia.Listening()) card_.uia.Raise(100 + cardFocus_, UIA_AutomationFocusChangedEventId); } r = 0; return true; }
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
        Accessibility::Node b; b.key = ButtonKey; b.name = state_ == LauncherState::Working && count_ > 0 ? L"Annotations · " + std::to_wstring(count_) + L" en cours" : std::wstring(L"Annotations");
        b.automationId = L"AnnotationLauncher"; b.accelerator = L"Ctrl+Maj+H"; b.help = L"Historique des annotations";
        b.expand = history_.Visible() ? 2 : 1; b.bounds = button_.Screen(LauncherBox()); n.push_back(b); return n;
    }
    std::vector<Accessibility::Node> CardNodes() {
        std::vector<Accessibility::Node> n; if (!cardPresence_.IsOpen()) return n;
        const auto now = Motion::Now(); const RECT rect = CardRect(now); const float top = (rect.bottom - rect.top) / card_.Scale() - 4 - CardH(now);
        auto at = [&](D2D1_RECT_F r) { r.top += top; r.bottom += top; return card_.Screen(r); };
        Accessibility::Node g; g.key = CardKey; g.role = Accessibility::Role::Group; g.focusable = false; g.automationId = L"AnnotationCard";
        g.name = CardTitle(content_); GetWindowRect(card_.Window(), &g.bounds); n.push_back(g);
        Accessibility::Node t; t.key = CardBodyKey; t.parent = CardKey; t.role = Accessibility::Role::Text; t.focusable = false; t.name = CardAnnouncement(content_);
        t.value = content_.kind == CardKind::Working || content_.kind == CardKind::Read ? content_.note : content_.kind == CardKind::Done || content_.kind == CardKind::Unhandled ? (content_.summary.empty() ? content_.note : content_.summary) : content_.kind == CardKind::NoAgent ? NoAgentBody() : L"";
        t.bounds = at(cardLayout_.header); n.push_back(t);
        auto add = [&](int hit, const wchar_t* name, D2D1_RECT_F r) { Accessibility::Node b; b.key = 100 + hit; b.parent = CardKey; b.name = name; b.bounds = at(r); b.automationId = name; n.push_back(b); };
        if (content_.kind == CardKind::NoAgent) add(CardConnect, L"Connecter un agent", cardLayout_.primary);
        if (content_.kind == CardKind::Working || content_.kind == CardKind::Read || content_.kind == CardKind::Unhandled) add(CardOpen, L"Voir l'annotation", cardLayout_.primary);
        if (content_.kind == CardKind::Done) { add(CardReload, L"Recharger la page", cardLayout_.primary); add(CardReopen, L"Rouvrir", cardLayout_.secondary); }
        add(CardClose, L"Fermer", cardLayout_.close);
        return n;
    }
    // History alone (Ctrl+Maj+H outside the mode): no launcher window.
    bool EnsureHistory() {
        if (!Ink::Shared(root_)) return false;
        if (historyReady_) return true; historyReady_ = true;
        HistoryHooks hh = hooks_.history;
        auto closed = hh.closed; hh.closed = [this, closed](bool keyboard) { RenderButton(Motion::Now()); if (keyboard) FocusLauncher(); if (closed) closed(keyboard); ReleaseIfDormant(); };
        if (!hh.announce) hh.announce = hooks_.announce;
        history_.Init(instance_, parent_, std::move(hh));
        history_.idle = [this] { ReleaseIfDormant(); };
        return true;
    }
    bool historyReady_ = false;
    bool Ensure() {
        if (!EnsureHistory()) return false;
        if (button_.Window()) return true;
        Layer::Options b; b.child = true; b.tabStop = true; b.title = L"Annotations";
        Layer::Options c; c.child = true; c.tabStop = true; c.title = L"Annotation";
        if (!card_.Create(instance_, parent_, c) || !button_.Create(instance_, parent_, b)) return false;
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
    bool HistoryVisible() const { return history_.Visible(); }
    HWND LauncherWindow() const { return button_.Window(); }
    HWND CardWindow() const { return card_.Window(); }
    HWND HistoryWindow() const { return history_.Window(); }
    HWND ToastWindow() const { return history_.ToastWindow(); }
    // Page area (the WebView bounds) in the parent's client pixels; re-anchors every surface (and raises them above the WebView).
    void Place(RECT page) {
        page_ = page; const auto now = Motion::Now();
        if (buttonPresence_.Visible(now)) RenderButton(now);
        if (cardPresence_.Visible(now)) RenderCard(now);
        history_.Place(page);
    }
    void SetBadgeRing(std::uint32_t rgba) { ring_ = rgba; }   // V1 (no ring in V2)
    // count: Working = annotations an agent is processing (accent badge); Rest = new annotations no agent read yet (raised badge, 3.1); 0 = no badge.
    // delayMs: « Spéc · Entrée du mode » — the launcher appears 120 ms after the band (0.9 -> 1 + opacity, 200 ms).
    bool Show(LauncherState state, int count, bool reduced, int delayMs = 0) {
        if (!Ensure()) return false;
        const auto now = Motion::Now(); state_ = state; count_ = count; reduced_ = reduced; motion_.SetReducedMotion(reduced);
        dormant_ = false; history_.SetLowAnchor(false);
        if (!buttonPresence_.IsOpen()) { Timing t = LauncherTiming(); t.delay = delayMs; buttonPresence_.Configure(t); }
        buttonPresence_.Show(now, true, reduced); RenderButton(now); Animator::Get().Request(this);
        if (button_.uia.Listening()) button_.uia.ChildrenChanged(ButtonKey);
        return true;
    }
    // The launcher and its card leave (into their corner, 150 ms). The history is the host's call (it may stay
    // open outside the mode: it is then re-anchored 16 DIP from the bottom).
    void Hide(bool animate = true) {
        if (!button_.Window()) return; const auto now = Motion::Now();
        HideCard(animate); buttonPresence_.Hide(now, animate, reduced_); buttonHover_ = buttonPressed_ = false;
        if (GetFocus() == button_.Window()) SetFocus(parent_);
        if (!buttonPresence_.Visible(now)) button_.Hide(); else Animator::Get().Request(this);
        if (button_.uia.Listening()) button_.uia.ChildrenChanged(ButtonKey);
    }
    // V3: outside the mode. Once nothing is on screen, every window is destroyed (zero launcher window at rest);
    // the next Show / ShowHistory creates them again.
    void SetDormant(bool dormant) { dormant_ = dormant; if (dormant) { history_.SetLowAnchor(true); ReleaseIfDormant(); } }
    bool Dormant() const { return dormant_; }
    void ReleaseIfDormant() {
        if (!dormant_) return; const auto now = Motion::Now();
        if (buttonPresence_.Visible(now) || cardPresence_.Visible(now)) return;
        if (button_.Window() || card_.Window()) {
            if (card_.Window()) KillTimer(card_.Window(), DwellTimer);
            dwellArmed_ = false; buttonFocus_ = false; cardFocus_ = cardHover_ = cardPressed_ = 0; buttonHover_ = buttonPressed_ = false;
            button_.Destroy(); card_.Destroy(); motion_.ClearInteractions();
        }
        if (history_.ReleaseWindows() && Animator::Exists()) Animator::Get().Forget(this);
    }
    // Live windows (QA « au repos » count).
    int WindowsForTest() const { return (button_.Window() ? 1 : 0) + (card_.Window() ? 1 : 0) + (history_.Window() ? 1 : 0) + (history_.ToastWindow() ? 1 : 0); }
    void ShowCard(const CardContent& content) {
        if (!Ensure() || content.kind == CardKind::None) return;
        const auto now = Motion::Now(); const bool open = cardPresence_.IsOpen();
        const bool same = open && content.kind == content_.kind && content.id == content_.id && content.note == content_.note && content.summary == content_.summary;
        const float h = open ? CardH(now) : 0.f;
        before_ = content_; beforeLayout_ = cardLayout_;
        content_ = content; cardLayout_ = LayoutCard(*Ink::Instance(), content_); cardHover_ = cardPressed_ = 0;
        if (open && !same) { cardFade_.Go(++cardStamp_, now); cardHeight_.Snap(h); cardHeight_.Set(cardLayout_.height, now, reduced_ ? 0 : Spec::panelIn, Curves::out); }
        else { cardFade_.Reset(cardStamp_); cardHeight_.Snap(cardLayout_.height); }
        if (history_.Visible()) history_.Hide(true);
        cardPresence_.Show(now, true, reduced_); RenderCard(now); RenderButton(now); Animator::Get().Request(this);
        ArmDwell();
        if (!same || content.kind == CardKind::Added) Announce(CardAnnouncement(content_));
        if (card_.uia.Listening()) card_.uia.ChildrenChanged(CardKey);
    }
    void HideCard(bool animate = true) {
        if (!card_.Window() || !cardPresence_.IsOpen()) return;
        const auto now = Motion::Now(); KillTimer(card_.Window(), DwellTimer); dwellArmed_ = false;
        cardPresence_.Hide(now, animate, reduced_); cardFocus_ = cardHover_ = cardPressed_ = 0;
        if (GetFocus() == card_.Window()) FocusLauncher();
        if (!cardPresence_.Visible(now)) card_.Hide(); else Animator::Get().Request(this);
        RenderButton(now);
    }
    // History (4.1 – 4.3): born from the launcher; the card steps aside while it is open.
    bool ShowHistory(const HistoryData& data, bool keyboard = false) {
        if (!EnsureHistory()) return false;
        HideCard(true); history_.SetLowAnchor(dormant_ || !buttonPresence_.IsOpen()); history_.Place(page_);
        const bool ok = history_.Show(data, keyboard, reduced_);
        RenderButton(Motion::Now()); if (button_.uia.Listening()) button_.uia.ChildrenChanged(ButtonKey);
        return ok;
    }
    void UpdateHistory(const HistoryData& data) { history_.Update(data); }
    void ShowHistoryDetail(const std::wstring& id) { history_.ShowDetail(id); }
    const std::wstring& HistoryDetailId() const { return history_.DetailId(); }
    void SetHistoryCapture(const std::wstring& id, const Shot& capture) { history_.SetEntryCapture(id, capture); }
    void HideHistory(bool animate = true) { history_.Hide(animate); RenderButton(Motion::Now()); if (!animate) ReleaseIfDormant(); }
    void FocusLauncher() { if (button_.Window() && buttonPresence_.IsOpen()) { keyboard_ = true; SetFocus(button_.Window()); } }
    void FocusCard() { if (card_.Window() && cardPresence_.IsOpen()) { keyboard_ = true; cardFocus_ = CardOrder().front(); SetFocus(card_.Window()); RenderCard(Motion::Now()); } }
    bool Frame(Motion::Time now) override {
        bool more = false;
        if (buttonPresence_.Visible(now)) {
            RenderButton(now);
            more |= buttonPresence_.Moving(now) || (state_ == LauncherState::Working && !reduced_);
        } else button_.Hide();
        if (cardPresence_.Visible(now)) { RenderCard(now); more |= cardPresence_.Moving(now) || cardFade_.Moving(now) || cardHeight_.Moving(now); }
        else card_.Hide();
        more |= motion_.NextWake(now).delay.has_value();
        if (!more && dormant_) ReleaseIfDormant();
        return more;
    }
    // ---- tests / QA
    Layer& ButtonLayerForTest() { return button_; }
    Layer& CardLayerForTest() { return card_; }
    Presence& CardPresenceForTest() { return cardPresence_; }
    const CardLayout& CardLayoutForTest() const { return cardLayout_; }
    HistoryPanel& HistoryForTest() { return history_; }
    bool DwellArmedForTest() const { return dwellArmed_; }
    std::vector<Accessibility::Node> ButtonNodesForTest() { return ButtonNodes(); }
    std::vector<Accessibility::Node> CardNodesForTest() { return CardNodes(); }
    void SetStateForTest(LauncherState s, int count) { state_ = s; count_ = count; RenderButton(Motion::Now()); }
};
}} // namespace NativeChrome::Annotations
