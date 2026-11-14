#pragma once
// A7 + C8 + C9 · Toasts contextuels (Paper « Browser Design », p-D-0).
//   A7 « Toast — limite atteinte » FD6-0 (+ états FGV/FH8/FHK/FHW-0): card layout, 269 DIP.
//   C8 « Toasts · 360 px » FLE-0 (réussite FLK, échec FLY, en cours FMD, bref FMS, focus FN7-0)
//   C9 « Zone toasts — 14 px bas-gauche » GCG-0: anchored 14 DIP from the page's bottom-left.
// One toast at a time: Show() replaces the previous one (its callback gets `false`). Non-
// activating: showing never steals focus; main may move keyboard focus here with Focus() (F6).
// The callback runs exactly once: true when the action button was used, false for ✕, Échap,
// time-out, replacement, Hide(), Dispose() or the owner going away.
//
// Paper values (get_jsx / get_computed_styles):
//   Card (A7): width 269, #171717, border 1 px #FFFFFF0D, radius 10, padding 16, gap 16
//     header: info glyph 16 @50 %, gap 6, title 16/20 medium -0.01em #FFFFFFE6
//     body 16/20 regular +0.01em #FFFFFFB3 ; button full width h 32 radius 6 #292929,
//     14/18 medium -0.01em #FFFFFFCC (hover #333333 + white text, focus inset #FFFFFF80 + white)
//   Status (C8): width 360, #292929, border 1 px #FFFFFF0D, radius 10, padding 8 / 12 left / 8
//     right, gap 10; glyph 16 (réussite check-circle #7D98FF, échec warning-circle #FF7B6E,
//     en cours ring 1.5 #FFFFFF26 + white arc); title 14/20 medium #FFFFFFE6, detail 14/20
//     #FFFFFF80 one line; action h 32 padding 10 radius 6 #FFFFFF14 14 medium #FFFFFFE6;
//     close 24x24 radius 3, glyph 14 #FFFFFF80, hover #333333; focus inset 1 px #FFFFFF80
//   Brief (C8 « Lien copié »): fit width, h 40, padding 8 / 12 / 16, gap 10, glyph #FFFFFFB3,
//     text 14/20 regular #FFFFFFE6
//   Timing: brief 3 s, success 6 s (A7 6 s), failure and in-progress stay; hover/focus pause the
//   timer. Entry: status 8 DIP slide + 160 ms fade (C8 FVX-0), card 12 DIP + 240 ms like the
//   update toast (A7 FIS-0); exit 150 ms fade; reduced motion: 100 ms fades only.
#include <windows.h>
#include <windowsx.h>
#include <functional>
#include <string>
#include <vector>
#include "tooltip.h"

namespace NativeChrome {
namespace ContextToast {
enum class Kind { Status, Card, Brief };
enum class Icon { None, Info, Success, Error, Progress, Link };
struct Spec {
    std::wstring automationId, message, detail, action; // message = title (Card/Status) or text (Brief)
    Icon icon = Icon::None; bool error = false;          // error: announced as an alert
    int durationMs = 4000;                               // 0 = stays until acted on
    Kind kind = Kind::Status; bool closable = true;      // ✕ (Status only)
    float progress = -1;                                 // Icon::Progress: 0..1
    // Profiles (Paper p-I-0 P04 Y8Y-0 / Y9A-0): 320 wide, radius 16; header check-circle at 50 % + title;
    // `inlineAction`: the detail and the action share one row (« Effacement définitif dans 7 s. » | Annuler),
    // else the body wraps and the action sits right-aligned under it at its own width.
    bool profileCard = false, inlineAction = false;
};
namespace Layout {
inline constexpr float inset = 14, radius = 10, statusWidth = 360, cardWidth = 269;
inline constexpr float cardPad = 16, cardGap = 16, headerGap = 6, cardButton = 32, buttonRadius = 6;
inline constexpr float padY = 8, padLeft = 12, padRight = 8, briefRight = 16, gap = 10, action = 32, actionPad = 10, close = 24, closeRadius = 3, closeGlyph = 14;
inline constexpr float statusRise = 8, cardRise = 12, statusEnterMs = 160, cardEnterMs = 240, exitMs = 150, reducedMs = 100;
inline constexpr float border = 0x0D / 255.f;
}
// Presets (Paper copy).
inline Spec TabLimitSpec(int limit) {
    Spec s; s.kind = Kind::Card; s.automationId = L"TabLimitToast"; s.icon = Icon::Info; s.durationMs = 6000;
    s.message = std::to_wstring(limit) + L" onglets ouverts";
    s.detail = L"C'est le maximum pour une fenêtre. Fermez-en ou continuez dans une nouvelle fenêtre.";
    s.action = L"Nouvelle fenêtre"; return s;
}
inline Spec PartialRestoreSpec(int notOpened, int limit) {
    Spec s = TabLimitSpec(limit); s.automationId = L"TabLimitRestoreToast";
    s.message = std::to_wstring(notOpened) + (notOpened > 1 ? L" onglets non ouverts" : L" onglet non ouvert");
    s.detail = L"La fenêtre a atteint " + std::to_wstring(limit) + L" onglets pendant la restauration de la session.";
    s.action = L"Ouvrir dans une nouvelle fenêtre"; return s;
}
inline Spec PageSavedSpec(const std::wstring& fileName) { Spec s; s.automationId = L"PageSavedToast"; s.icon = Icon::Success; s.message = L"Page enregistrée"; s.detail = fileName; s.action = L"Afficher"; s.durationMs = 6000; return s; }
inline Spec SaveFailedSpec(const std::wstring& reason) { Spec s; s.automationId = L"PageSaveFailedToast"; s.icon = Icon::Error; s.error = true; s.message = L"Échec de l'enregistrement"; s.detail = reason; s.action = L"Réessayer"; s.durationMs = 0; return s; }
inline Spec SavingSpec(float progress, const std::wstring& what = L"Page complète") {
    Spec s; s.automationId = L"PageSavingToast"; s.icon = Icon::Progress; s.progress = progress; s.message = L"Enregistrement de la page…";
    s.detail = what + L" · " + std::to_wstring(static_cast<int>(std::lround(std::clamp(progress, 0.f, 1.f) * 100))) + L" %"; s.action = L"Annuler"; s.durationMs = 0; s.closable = false; return s;
}
inline Spec BriefSpec(const std::wstring& text, Icon icon = Icon::None) { Spec s; s.kind = Kind::Brief; s.automationId = L"BriefToast"; s.message = text; s.icon = icon; s.durationMs = 3000; s.closable = false; return s; }

class Toast {
public:
    enum Hit { None = 0, ActionHit = 1, CloseHit = 2 };
private:
    HWND hwnd_ = nullptr, owner_ = nullptr, restore_ = nullptr; Chrome chrome_; Chrome* surface_ = &chrome_; Motion::State motion_; PopupKit::Pump pump_; PopupKit::GlyphCache glyphs_; Accessibility::Bridge uia_;
    Spec spec_; std::function<void(bool)> done_; RECT page_{}; float bottomReserve_ = 0;
    bool visible_ = false, leaving_ = false, reduced_ = false, hovered_ = false, keyboard_ = false;
    int hover_ = 0, pressed_ = 0, focus_ = 0; Motion::Time phase_{}; float width_ = 0, height_ = 0;
    ULONGLONG remaining_ = 0, resumed_ = 0; Motion::Time deadline_{}; bool timing_ = false;

    int Px(float dip) const { return PopupKit::Px(owner_, dip); }
    bool HasAction() const { return !spec_.action.empty() && spec_.kind != Kind::Brief; }
    bool HasClose() const { return spec_.kind == Kind::Status && spec_.closable; }
    PopupKit::Type Title() const { return spec_.kind == Kind::Card ? PopupKit::Type{16, 20, -.16f, true} : PopupKit::Type{14, 20, 0, spec_.kind == Kind::Status}; }
    float ActionWidth() { return std::ceil(PopupKit::Measure(chrome_, spec_.action, 8192, {14, 20, 0, true}, false).width) + 2 * Layout::actionPad; }
    float BodyHeight() { return PopupKit::Measure(chrome_, spec_.detail, Layout::cardWidth - 2 - 2 * Layout::cardPad, {16, 20, .16f}, true).height; }
    // P04 profile card geometry.
    static constexpr float profileWidth = 320, profileRadius = 16;
    float Radius() const { return spec_.profileCard ? profileRadius : Layout::radius; }
    float ProfileBodyHeight() { return PopupKit::Measure(chrome_, spec_.detail, profileWidth - 2 - 2 * Layout::cardPad, {14, 20}, true).height; }
    float ProfileActionWidth() { return std::max(64.f, std::ceil(PopupKit::Measure(chrome_, spec_.action, 8192, {14, 18, -.14f, true}, false).width) + 24); }
    void Measure() {
        using namespace Layout;
        if (spec_.profileCard) {
            width_ = profileWidth; const float head = 2 + 2 * cardPad + 20;
            if (spec_.inlineAction) { height_ = head + cardGap + cardButton; return; }
            height_ = head + (spec_.detail.empty() ? 0 : cardGap + ProfileBodyHeight()) + (HasAction() ? cardGap + cardButton : 0); return;
        }
        if (spec_.kind == Kind::Card) { width_ = cardWidth; height_ = 2 + 2 * cardPad + 20 + (spec_.detail.empty() ? 0 : cardGap + BodyHeight()) + (HasAction() ? cardGap + cardButton : 0); return; }
        if (spec_.kind == Kind::Brief) { width_ = std::ceil(2 + padLeft + (spec_.icon != Icon::None ? 16 + gap : 0) + PopupKit::Measure(chrome_, spec_.message, 8192, Title(), false).width + briefRight); height_ = 40; return; }
        width_ = statusWidth; height_ = 2 + 2 * padY + std::max(spec_.detail.empty() ? 20.f : 40.f, HasAction() ? action : 24.f);
    }
    D2D1_RECT_F ActionBox() {
        using namespace Layout;
        if (!HasAction()) return {};
        if (spec_.profileCard) { const float y = height_ - 1 - cardPad - cardButton, r = width_ - 1 - cardPad; return {r - ProfileActionWidth(), y, r, y + cardButton}; }
        if (spec_.kind == Kind::Card) { const float y = height_ - 1 - cardPad - cardButton; return {1 + cardPad, y, width_ - 1 - cardPad, y + cardButton}; }
        const float right = width_ - 1 - padRight - (HasClose() ? close + gap : 0), cy = height_ / 2;
        return {right - ActionWidth(), cy - action / 2, right, cy + action / 2};
    }
    D2D1_RECT_F CloseBox() const { if (!HasClose()) return {}; const float r = width_ - 1 - Layout::padRight, cy = height_ / 2; return {r - Layout::close, cy - Layout::close / 2, r, cy + Layout::close / 2}; }
    int HitAt(float x, float y) { if (HasAction() && PopupKit::Contains(ActionBox(), x, y)) return ActionHit; if (HasClose() && PopupKit::Contains(CloseBox(), x, y)) return CloseHit; return None; }
    void DrawIcon(D2D1_RECT_F box) {
        Chrome& chrome_ = *surface_;
        switch (spec_.icon) {
        case Icon::Info: glyphs_.Draw(chrome_, PopupKit::Glyphs::info, box, 16, PopupKit::White(.5f)); break;
        case Icon::Success: { const auto k = Design::Accent::Ink(); glyphs_.Draw(chrome_, PopupKit::Glyphs::checkCircle, box, 16, D2D1::ColorF(k.r, k.g, k.b, 1)); break; }   // the window's profile ink (was the legacy #7D98FF)
        case Icon::Error: glyphs_.Draw(chrome_, PopupKit::Glyphs::warningCircle, box, 16, PopupKit::Danger()); break;
        case Icon::Link: glyphs_.Draw(chrome_, PopupKit::Glyphs::link, box, 16, PopupKit::White(0xB3 / 255.f)); break;
        case Icon::Progress: {
            auto* t = chrome_.RenderTarget(); auto* b = chrome_.SolidBrush(); const float cx = (box.left + box.right) / 2, cy = (box.top + box.bottom) / 2, r = 6.25f;
            b->SetColor(PopupKit::White(0x26 / 255.f)); t->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), b, 1.5f);
            const float p = std::clamp(spec_.progress < 0 ? .25f : spec_.progress, 0.f, 1.f); if (p <= 0) break;
            ID2D1PathGeometry* g = nullptr; if (FAILED(chrome_.Factory()->CreatePathGeometry(&g))) break; ID2D1GeometrySink* s = nullptr;
            if (SUCCEEDED(g->Open(&s))) {
                const float a = p * 6.2831853f - 1.5707963f;
                s->BeginFigure(D2D1::Point2F(cx, cy - r), D2D1_FIGURE_BEGIN_HOLLOW);
                if (p >= .999f) { s->AddArc(D2D1::ArcSegment(D2D1::Point2F(cx, cy + r), D2D1::SizeF(r, r), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL)); s->AddArc(D2D1::ArcSegment(D2D1::Point2F(cx, cy - r), D2D1::SizeF(r, r), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL)); }
                else s->AddArc(D2D1::ArcSegment(D2D1::Point2F(cx + r * std::cos(a), cy + r * std::sin(a)), D2D1::SizeF(r, r), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, p > .5f ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
                s->EndFigure(D2D1_FIGURE_END_OPEN); s->Close(); s->Release();
                ID2D1StrokeStyle* round = nullptr; chrome_.Factory()->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND), nullptr, 0, &round);
                b->SetColor(PopupKit::White(1)); t->DrawGeometry(g, b, 1.5f, round); if (round) round->Release();
            }
            g->Release(); break;
        }
        default: break;
        }
    }
    void Paint() {
        Chrome& chrome_ = *surface_; // offscreen in RenderForTest
        using namespace Layout;
        if (!chrome_.RenderTarget()) chrome_.Resize();
        auto* t = chrome_.RenderTarget(); if (!t || !chrome_.SolidBrush()) return;
        const auto now = Motion::Now(); chrome_.keyboardModality = keyboard_;
        t->BeginDraw(); t->Clear(PopupKit::Gray(0x17));
        PopupKit::Fill(chrome_, {0, 0, width_, height_}, Radius(), spec_.kind == Kind::Card ? PopupKit::Gray(0x17) : PopupKit::Gray(0x29));
        const bool focusShown = GetFocus() == hwnd_;
        if (spec_.profileCard) {
            // Header: check-circle (bold) white 50 %, gap 6, title 16/20 Medium #FFFFFFE6.
            const float x = 1 + cardPad, y = 1 + cardPad;
            glyphs_.Draw(chrome_, PopupKit::Glyphs::checkCircle, {x, y + 2, x + 16, y + 18}, 16, PopupKit::White(.5f));
            PopupKit::DrawText(chrome_, spec_.message, {x + 22, y, width_ - x, y + 20}, {16, 20, -.16f, true}, PopupKit::White(.9f));
            if (spec_.inlineAction) {
                const auto b = ActionBox(); const float cy = (b.top + b.bottom) / 2;
                PopupKit::DrawText(chrome_, spec_.detail, {x, cy - 10, b.left - 8, cy + 10}, {14, 20, 0, false, true}, PopupKit::White(0xB3 / 255.f));
            } else if (!spec_.detail.empty()) PopupKit::DrawText(chrome_, spec_.detail, {x, y + 20 + cardGap, width_ - x, height_}, {14, 20}, PopupKit::White(0xB3 / 255.f), true);
            if (HasAction()) {   // raised #292929, radius 8, 14/18 Medium white; focus: inset #FFFFFF80 (keyboard)
                const auto b = ActionBox(); const auto v = motion_.Visual(ActionHit, now); const bool focused = focusShown && focus_ == ActionHit && keyboard_;
                const float lit = std::max(v.hover, v.press);
                PopupKit::Fill(chrome_, b, 8, PopupKit::Gray(static_cast<int>(std::lround(0x29 + (0x33 - 0x29) * lit + (0x3E - 0x33) * v.press))));
                PopupKit::DrawText(chrome_, spec_.action, {b.left, b.top + 7, b.right, b.bottom}, {14, 18, -.14f, true}, PopupKit::White(1), false, DWRITE_TEXT_ALIGNMENT_CENTER);
                if (focused) PopupKit::InsetRing(chrome_, b, 8);
            }
        } else if (spec_.kind == Kind::Card) {
            const float x = 1 + cardPad, y = 1 + cardPad;
            float textX = x; if (spec_.icon != Icon::None) { DrawIcon({x, y + 2, x + 16, y + 18}); textX += 16 + headerGap; }
            PopupKit::DrawText(chrome_, spec_.message, {textX, y, width_ - x, y + 20}, Title(), PopupKit::White(.9f));
            if (!spec_.detail.empty()) PopupKit::DrawText(chrome_, spec_.detail, {x, y + 20 + cardGap, width_ - x, height_}, {16, 20, .16f}, PopupKit::White(0xB3 / 255.f), true);
            if (HasAction()) {
                const auto b = ActionBox(); const auto v = motion_.Visual(ActionHit, now); const bool focused = focusShown && focus_ == ActionHit;
                const float lit = std::max(v.hover, v.press);
                PopupKit::Fill(chrome_, b, buttonRadius, PopupKit::Gray(static_cast<int>(std::lround(0x29 + (0x33 - 0x29) * lit + (0x3E - 0x33) * v.press))));
                const PopupKit::Type type{14, 18, -.14f, true}; const float w = PopupKit::Measure(chrome_, spec_.action, 8192, type, false).width;
                PopupKit::DrawText(chrome_, spec_.action, {(b.left + b.right - w) / 2, b.top + 7, (b.left + b.right + w) / 2 + 1, b.bottom}, type, PopupKit::White(focused || lit > .5f ? 1.f : 0xCC / 255.f));
                if (focused) PopupKit::InsetRing(chrome_, b, buttonRadius);
            }
        } else if (spec_.kind == Kind::Brief) {
            float x = 1 + padLeft; if (spec_.icon != Icon::None) { DrawIcon({x, 12, x + 16, 28}); x += 16 + gap; }
            PopupKit::DrawText(chrome_, spec_.message, {x, 10, width_, 30}, Title(), PopupKit::White(.9f));
        } else {
            float x = 1 + padLeft; const float cy = height_ / 2;
            if (spec_.icon != Icon::None) { DrawIcon({x, cy - 8, x + 16, cy + 8}); x += 16 + gap; }
            const float right = (HasAction() ? ActionBox().left : HasClose() ? CloseBox().left : width_ - 1 - padRight) - (HasAction() || HasClose() ? gap : 0);
            const float top = spec_.detail.empty() ? cy - 10 : cy - 20;
            PopupKit::DrawText(chrome_, spec_.message, {x, top, right, top + 20}, Title(), PopupKit::White(.9f));
            if (!spec_.detail.empty()) PopupKit::DrawText(chrome_, spec_.detail, {x, top + 20, right, top + 40}, {14, 20}, PopupKit::White(.5f));
            if (HasAction()) {
                const auto b = ActionBox(); const auto v = motion_.Visual(ActionHit, now);
                PopupKit::Fill(chrome_, b, buttonRadius, PopupKit::White(0x14 / 255.f + .06f * v.hover + .06f * v.press));
                PopupKit::DrawText(chrome_, spec_.action, {b.left + actionPad, b.top + 6, b.right, b.bottom}, {14, 20, 0, true}, PopupKit::White(.9f));
                if (focusShown && focus_ == ActionHit) PopupKit::InsetRing(chrome_, b, buttonRadius);
            }
            if (HasClose()) {
                const auto b = CloseBox(); const auto v = motion_.Visual(CloseHit, now); const float lit = std::max(v.hover, v.press);
                if (lit > 0) PopupKit::Fill(chrome_, b, closeRadius, PopupKit::Gray(v.press > v.hover ? 0x3E : 0x33, lit));
                glyphs_.Draw(chrome_, PopupKit::Glyphs::close, b, closeGlyph * Motion::PressScale(v.press, reduced_, true), PopupKit::White(.5f));
                if (focusShown && focus_ == CloseHit) PopupKit::InsetRing(chrome_, b, closeRadius);
            }
        }
        PopupKit::Outline(chrome_, width_, height_, Radius(), PopupKit::White(border));
        if (t->EndDraw() == D2DERR_RECREATE_TARGET) chrome_.RecreateTarget();
    }
    POINT Rest() const { return {page_.left + Px(Layout::inset), page_.bottom - Px(Layout::inset + bottomReserve_) - Px(height_)}; }
    float EnterMs() const { return reduced_ ? Layout::reducedMs : spec_.kind == Kind::Card ? Layout::cardEnterMs : Layout::statusEnterMs; }
    float Rise() const { return reduced_ ? 0 : spec_.kind == Kind::Card ? Layout::cardRise : Layout::statusRise; }
    float Elapsed(Motion::Time now) const { return static_cast<float>(std::chrono::duration<double, std::milli>(now - phase_).count()); }
    void Place(float progress, bool entering) {
        const POINT p = Rest(); const int rise = entering ? Px(Rise() * (1 - progress)) : 0;
        SetWindowPos(hwnd_, HWND_TOP, p.x, p.y + rise, Px(width_), Px(height_), SWP_NOACTIVATE);
        PopupKit::SetAlpha(hwnd_, progress);
    }
    void Wake() { pump_.Request(NextWake(Motion::Now())); }
    // Life timer: runs only while neither hovered nor focused.
    void Pause() { if (!timing_) return; const auto now = Motion::Now(); remaining_ = deadline_ > now ? static_cast<ULONGLONG>(std::chrono::duration_cast<Motion::Milliseconds>(deadline_ - now).count()) + 1 : 1; timing_ = false; }
    void Resume() { if (!visible_ || leaving_ || timing_ || !remaining_ || hovered_ || GetFocus() == hwnd_) return; deadline_ = Motion::Now() + Motion::Milliseconds(remaining_); timing_ = true; Wake(); }
    void Finish(bool actioned) {
        visible_ = leaving_ = timing_ = false; remaining_ = 0; ShowWindow(hwnd_, SW_HIDE);
        auto done = std::move(done_); done_ = nullptr; if (done) done(actioned);
    }
    void Leave(bool actioned) {
        if (!visible_ || leaving_) return;
        const bool focused = GetFocus() == hwnd_;
        timing_ = false; remaining_ = 0; leaving_ = true; phase_ = Motion::Now();
        auto done = std::move(done_); done_ = nullptr;      // exactly once, at the decision
        if (focused) { if (restore_ && IsWindow(restore_)) SetFocus(restore_); else if (owner_) SetFocus(owner_); }
        if (done) done(actioned);
        Wake();
    }
    void Activate(int hit) { if (hit == ActionHit && HasAction()) Leave(true); else if (hit == CloseHit && HasClose()) Leave(false); }
    void Interact() { motion_.SetInteraction(hover_, pressed_ == hover_ ? pressed_ : 0, Motion::Now()); Paint(); ValidateRect(hwnd_, nullptr); Wake(); }
    std::vector<int> Order() const { std::vector<int> o; if (HasAction()) o.push_back(ActionHit); if (HasClose()) o.push_back(CloseHit); return o; }
    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<Toast*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (msg == WM_NCCREATE) { self = static_cast<Toast*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); self->hwnd_ = h; }
        return self ? self->Handle(h, msg, w, l) : DefWindowProcW(h, msg, w, l);
    }
    LRESULT Handle(HWND h, UINT msg, WPARAM w, LPARAM l) {
        const float s = 96.f / PopupKit::Dpi(h);
        switch (msg) {
        case WM_GETOBJECT: { LRESULT r; if (uia_.OnGetObject(h, w, l, r)) return r; break; }
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); Paint(); EndPaint(h, &ps); return 0; }
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_TIMER: if (w == PopupKit::Pump::TimerId) { KillTimer(h, w); const auto now = Motion::Now(); Frame(now); pump_.Request(NextWake(now)); } return 0;
        case WM_MOUSEMOVE: { if (!hovered_) { hovered_ = true; Pause(); TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, h, 0}; TrackMouseEvent(&t); } const int hit = HitAt(GET_X_LPARAM(l) * s, GET_Y_LPARAM(l) * s); if (hit != hover_) { hover_ = hit; Interact(); } return 0; }
        case WM_MOUSELEAVE: hovered_ = false; hover_ = 0; Interact(); Resume(); return 0;
        case WM_LBUTTONDOWN: { keyboard_ = false; pressed_ = HitAt(GET_X_LPARAM(l) * s, GET_Y_LPARAM(l) * s); if (pressed_) SetCapture(h); Interact(); return 0; }
        case WM_LBUTTONUP: { const int hit = HitAt(GET_X_LPARAM(l) * s, GET_Y_LPARAM(l) * s), was = pressed_; pressed_ = 0; if (GetCapture() == h) ReleaseCapture(); Interact(); if (was && was == hit) Activate(hit); return 0; }
        case WM_KEYDOWN: {
            const auto o = Order();
            if ((w == VK_TAB || w == VK_LEFT || w == VK_RIGHT) && !o.empty()) { keyboard_ = true; auto it = std::find(o.begin(), o.end(), focus_); size_t i = it == o.end() ? 0 : (static_cast<size_t>(it - o.begin()) + 1) % o.size(); focus_ = o[i]; Paint(); ValidateRect(h, nullptr); if (uia_.Listening()) uia_.Raise(focus_, UIA_AutomationFocusChangedEventId); return 0; }
            if ((w == VK_RETURN || w == VK_SPACE) && focus_) { Activate(focus_); return 0; }
            if (w == VK_ESCAPE) { Leave(false); return 0; }                                  // Paper: Échap ferme le toast focalisé
            if (w == VK_F6) { if (restore_ && IsWindow(restore_)) SetFocus(restore_); else if (owner_) SetFocus(owner_); return 0; }
            break;
        }
        case WM_SETFOCUS: Pause(); { const auto o = Order(); if (!focus_ && !o.empty()) focus_ = o.front(); } Paint(); ValidateRect(h, nullptr); return 0;
        case WM_KILLFOCUS: focus_ = 0; Paint(); ValidateRect(h, nullptr); Resume(); return 0;
        case WM_APP + 1: keyboard_ = true; Activate(static_cast<int>(w)); return 0;
        case WM_DPICHANGED: if (visible_ && !leaving_) Place(1, false); return 0;
        case WM_NCDESTROY: { uia_.Detach(); hwnd_ = nullptr; auto done = std::move(done_); done_ = nullptr; visible_ = false; if (done) done(false); break; }
        }
        return DefWindowProcW(h, msg, w, l);
    }
public:
    ~Toast() { Dispose(); }
    bool Create(HINSTANCE instance, HWND owner, const std::wstring& root, std::function<void()> wake = nullptr) {
        owner_ = owner; motion_.EnablePolish(); chrome_.SetMotion(&motion_);
        PopupKit::Register(instance, L"LisiereContextToast", Proc);
        if (!PopupKit::CreatePopup(instance, owner, L"LisiereContextToast", L"Notification", WS_EX_NOACTIVATE, this, Layout::radius)) return false;
        pump_.Bind(hwnd_, std::move(wake));
        Accessibility::Host host; host.window = hwnd_;
        host.nodes = [this] {
            std::vector<Accessibility::Node> nodes; if (!visible_ || leaving_) return nodes;
            auto screen = [&](D2D1_RECT_F r) { RECT p{Px(r.left), Px(r.top), Px(r.right), Px(r.bottom)}; MapWindowPoints(hwnd_, nullptr, reinterpret_cast<POINT*>(&p), 2); return p; };
            Accessibility::Node g; g.key = 10; g.role = Accessibility::Role::Group; g.name = spec_.message; g.help = spec_.detail; g.status = spec_.error ? L"Erreur" : L"";
            g.automationId = spec_.automationId; g.focusable = false; GetWindowRect(hwnd_, &g.bounds); nodes.push_back(g);
            if (HasAction()) { Accessibility::Node a; a.key = ActionHit; a.parent = 10; a.name = spec_.action; a.automationId = spec_.automationId + L"Action"; a.bounds = screen(ActionBox()); nodes.push_back(a); }
            if (HasClose()) { Accessibility::Node c; c.key = CloseHit; c.parent = 10; c.name = L"Fermer"; c.accelerator = L"Échap"; c.automationId = spec_.automationId + L"Close"; c.bounds = screen(CloseBox()); nodes.push_back(c); }
            return nodes; };
        host.focused = [this]() -> long long { return GetFocus() == hwnd_ ? focus_ : 0; };
        host.invoke = [this](long long key) { PostMessageW(hwnd_, WM_APP + 1, static_cast<WPARAM>(key), 0); };
        uia_.Attach(std::move(host));
        return SUCCEEDED(chrome_.Init(hwnd_, root));
    }
    void Dispose() {
        pump_.Stop(); auto done = std::move(done_); done_ = nullptr; visible_ = false;
        uia_.Detach(); chrome_.Dispose(); glyphs_.Clear();
        if (hwnd_ && IsWindow(hwnd_)) DestroyWindow(hwnd_); hwnd_ = nullptr;
        if (done) done(false);
    }
    // Shows `spec` 14 DIP inside the bottom-left corner of `pageScreen` (screen px). Replaces a
    // visible toast (its callback receives false first). Returns false without a window.
    bool Show(Spec spec, RECT pageScreen, std::function<void(bool actioned)> done) {
        if (!hwnd_) { if (done) done(false); return false; }
        if (auto previous = std::move(done_)) { done_ = nullptr; previous(false); }
        const bool was = visible_ && !leaving_;
        spec_ = std::move(spec); done_ = std::move(done); page_ = pageScreen; leaving_ = false; focus_ = 0; hover_ = pressed_ = 0; motion_.ClearInteractions();
        visible_ = true; phase_ = Motion::Now(); Measure(); NativeUI::ConfigureShadowlessPopup(hwnd_, Radius()); chrome_.Resize();
        if (was) Place(1, false); else Place(0, true);
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE); chrome_.Resize(); Paint(); ValidateRect(hwnd_, nullptr);
        if (was) phase_ = Motion::Now() - Motion::Milliseconds(static_cast<int>(EnterMs())); // no second entrance
        timing_ = false; remaining_ = spec_.durationMs > 0 ? static_cast<ULONGLONG>(spec_.durationMs) : 0; Resume();
        if (uia_.Listening()) { uia_.ChildrenChanged(10); const std::wstring said = spec_.message + (spec_.detail.empty() ? L"" : L". " + spec_.detail); uia_.Notify(10, said.c_str(), spec_.error ? L"ToastAlert" : L"ToastStatus"); }
        Wake(); return true;
    }
    // Same toast, new content (progress, « en cours » → réussite): keeps the callback and timer state.
    void Update(Spec spec) {
        if (!visible_ || leaving_) return; const bool restart = spec.durationMs != spec_.durationMs;
        spec_ = std::move(spec); Measure(); NativeUI::ConfigureShadowlessPopup(hwnd_, Radius()); Place(1, false); chrome_.Resize(); Paint(); ValidateRect(hwnd_, nullptr);
        if (restart) { timing_ = false; remaining_ = spec_.durationMs > 0 ? static_cast<ULONGLONG>(spec_.durationMs) : 0; Resume(); }
    }
    void Hide() { Leave(false); }
    void HideNow() { if (!visible_) return; if (!leaving_) { auto done = std::move(done_); done_ = nullptr; if (done) done(false); } Finish(false); }
    void Reposition(RECT pageScreen) { page_ = pageScreen; if (visible_ && !leaving_) Place(1, false); }
    void SetBottomReserve(float dip) { bottomReserve_ = dip; if (visible_ && !leaving_) Place(1, false); }
    // F6 from the page: keyboard focus to the toast (timer paused); Échap / F6 give it back.
    bool Focus() { if (!visible_ || leaving_ || Order().empty()) return false; HWND f = GetFocus(); if (f != hwnd_) restore_ = f; keyboard_ = true; focus_ = Order().front(); SetFocus(hwnd_); Paint(); ValidateRect(hwnd_, nullptr); return true; }
    bool HasFocus() const { return hwnd_ && GetFocus() == hwnd_; }
    bool Visible() const { return visible_ && !leaving_; }
    HWND Window() const { return hwnd_; }
    const Spec& Current() const { return spec_; }
    SIZE SizeDip() const { return {static_cast<LONG>(width_), static_cast<LONG>(height_)}; }
    void SetReducedMotion(bool reduced) { reduced_ = reduced; motion_.SetReducedMotion(reduced); }
    Motion::Wake NextWake(Motion::Time now) const {
        auto w = motion_.NextWake(now);
        if (visible_ && (leaving_ || Elapsed(now) < EnterMs())) w = Motion::Earlier(w, Motion::Frame());
        if (visible_ && timing_ && !leaving_) { auto d = Motion::Deadline(now, deadline_); if (!d.delay) d = Motion::Frame(); w = Motion::Earlier(w, d); }
        return w;
    }
    void Frame(Motion::Time now) {
        if (!hwnd_ || !visible_) return;
        if (leaving_) { const float u = Motion::Clamp(Elapsed(now) / (reduced_ ? Layout::reducedMs : Layout::exitMs)); Place(1 - u, false); if (u >= 1) Finish(false); return; }
        if (timing_ && now >= deadline_) { timing_ = false; remaining_ = 0; Leave(false); return; }
        const float u = Motion::Clamp(Elapsed(now) / EnterMs());
        Place(1 - (1 - u) * (1 - u) * (1 - u), true);
        Paint(); ValidateRect(hwnd_, nullptr);
    }
    // Tests: drive paths without input injection.
    void ActivateForTest(int hit) { Activate(hit); }
    void KeyForTest(WPARAM key) { SendMessageW(hwnd_, WM_KEYDOWN, key, 0); }
    void PaintForTest() { Paint(); }
    bool RenderForTest(const std::wstring& root, const wchar_t* png, UINT dpi = 96) { return PopupKit::RenderPng(root, width_, height_, dpi, png, surface_, [this] { Paint(); }); }
    void ExpireForTest() { if (visible_ && !leaving_) Leave(false); }
};
} // namespace ContextToast
} // namespace NativeChrome
