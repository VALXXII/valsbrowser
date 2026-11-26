#pragma once
#include "i18n.h"
// Annotation mode, iframes case 2 — an iframe of ANOTHER site under the pointer (Paper « Browser Design », page « #127 —
// Panneau latéral (A finale validée) », board « Iframes · cas 2 · iframe d'un autre site · v2 carte + En savoir plus
// (maquette prête) » 26DE-0, Designer's v2 validated by Val: it replaces v1's tooltip). The page script frames the whole
// iframe (label « iframe · autre site » inside it, src/annotation-script.h) and tells the host once when the hover enters
// it and once when it leaves it (« frameHover », src/annotation-host.h); src/annotation-mode.h places these surfaces.
//
//   Card (26GX-0), under the frame (PlaceFrameCard: 12 below the iframe, its left on the iframe's left; above it when it
//   is not whole under it): 300 wide, #242424, border 1 #3A3A3A, radius 12, padding 12, gap 8. Title 13/18 600 white
//   « Ce cadre vient d'un autre site »; two texts 12/17 #BFBFBF; then (4 more above, gap 8) two buttons 28 high, radius 6,
//   padding-inline 10, 12/16 500 white: « Annoter le cadre » in the profile's accent, « En savoir plus » #333333.
//   Help panel (26HA-0), opened by « En savoir plus » at the drawers' place and width (332, docked under the toolbar on the tab
//   panel's side, as Réglages: the window's right edge in the top bar, the side panel's edge in its layouts, #143): #171717, padding 16, gap 16. Header: title 15/20 600 white « Les cadres d'autres
//   sites », « × » 16/16 #8C8C8C at its right. The intro 13/19 #BFBFBF; four sections (gap 4): subtitle 13/18 600 white, text
//   13/19 #BFBFBF (the last one three lines « • … », pre-wrap).
//
// Not in the board — the app's existing values (listed in the PR as gaps, never a new look):
//   card    shown 120 ms after the hover entered the frame (the spacing inspector's delay: nothing during a fast
//           movement); fades in 150 ms and out 120 ms, opacity only, reduced motion the same (the live-site card E1b's);
//           stays 300 ms after the hover left the frame so the pointer can reach it (Mode Focus's bar grace); goes at once
//           when the page scrolls under a still pointer, a selection starts, the mode ends; follows the window when it
//           moves. It never takes the keyboard (the page keeps it: WS_EX_NOACTIVATE): its buttons are for the pointer and
//           UI Automation (Invoke). 8 px from the page area's edges (the chat bubble's margin); no room under nor above
//           (an iframe taller than the visible page): inside it, at the bottom of what is visible of it (the bubble's
//           rule 10). Buttons: « Annoter le cadre » hover / pressed = the accent's (Design::Accent::StateColor, every accent
//           button's); « En savoir plus » #333333 → #3E3E3E under the pointer and pressed (the next step of the tokens'
//           ladder #292929 / #333333 / #3E3E3E).
//   panel   the drawers' frame and motion: docked column (Mode Focus's top / right / bottom and corners), white 2 % leading
//           hairline, DrawerMotion's enter / exit springs on the keyed strip (reduced motion: at once); closes on « × »,
//           Échap, a click elsewhere (it loses the activation, as every drawer) or the mode's end. The header stays; the
//           rest scrolls with the wheel and the keys under it, through Réglages' 16 px top fade. « × » = the board's glyph
//           on the drawers' 24 px ghost button (hover plate, tooltip « Fermer (Échap) »), keyboard ring when tabbed to.
#include <windows.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "chrome.h"
#include "tooltip.h"
#include "accessibility.h"
#include "frame-clock.h"
#include "native-ui.h"
#include "onboarding-parts.h"
#include "downloads.h"         // Lane, the drawers' place (DrawerLayout) and motion (DrawerMotion)
#include "drawer-corners.h"    // Mode Focus: the docked drawer's antialiased top corners
#include "resize-border.h"     // the window's resize border wins over the panel on the window's edges

namespace NativeChrome {
namespace AnnotationFrameCard {

namespace K = OnboardingKit; namespace P = OnboardingParts;

// ---------------------------------------------------------------- the card (26GX-0)
namespace CardSpec {
inline constexpr float width = 300, pad = 12, radius = 12, gap = 8, rowTop = 4, buttonH = 28, buttonRadius = 6, buttonPadX = 10, buttonGap = 8, border = 1;
inline constexpr float below = 12, margin = 8;   // Paper: the card's top 318 for the iframe's bottom 306; the page area's margin (the bubble's)
inline constexpr unsigned surface = 0x242424, edge = 0x3A3A3A, body = 0xBFBFBF, neutral = 0x333333, neutralHover = 0x3E3E3E;
inline constexpr int showDelayMs = 120, fadeInMs = 150, fadeOutMs = 120, leaveGraceMs = 300;
inline K::Font Title() { return {13, 18, 600, 0}; }
inline K::Font Body() { return {12, 17, 400, 0}; }
inline K::Font Button() { return {12, 16, 500, 0}; }
}
struct CardHooks {
    std::function<void()> annotate, more;   // « Annoter le cadre », « En savoir plus »
    std::function<void()> entered, left;    // the pointer came onto the card / left it
};

class Card {
public:
    enum Id : int { None = 0, Annotate = 1, More = 2 };
    static constexpr UINT FrameMessage = WM_APP + 0x4E2;
private:
    HWND hwnd_ = nullptr, owner_ = nullptr; std::wstring root_;
    Chrome chrome_; Chrome* c_ = &chrome_; Accessibility::Bridge uia_; FrameClock::Clock clock_; bool clockOk_ = false;
    CardHooks hooks_; unsigned accent_ = 0x3E67FF; bool open_ = false, closing_ = false, inside_ = false, offscreen_ = false;
    int hover_ = None, pressed_ = None; std::map<int, float> glow_;
    UINT dpi_ = 96; Motion::Time fadeAt_{}, lastStep_{}; float fadeFrom_ = 0, fadeTo_ = 0, fadeMs_ = 1;

    float Scale() const { return dpi_ / 96.f; }
    int Px(float dip) const { return static_cast<int>(std::lround(dip * Scale())); }
    static float Ms(Motion::Time a, Motion::Time b) { return static_cast<float>(std::chrono::duration<double, std::milli>(b - a).count()); }
    static bool In(D2D1_RECT_F r, float x, float y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }
    static float Inner() { return CardSpec::width - 2 * CardSpec::border - 2 * CardSpec::pad; }   // 274
    float TitleH() { return K::WrappedHeight(*c_, TR(annotation_frame_card_title), Inner(), CardSpec::Title()); }
    float WhyH() { return K::WrappedHeight(*c_, TR(annotation_frame_card_why), Inner(), CardSpec::Body()); }
    float StillH() { return K::WrappedHeight(*c_, TR(annotation_frame_card_still), Inner(), CardSpec::Body()); }
    float RowTop() { return CardSpec::border + CardSpec::pad + TitleH() + CardSpec::gap + WhyH() + CardSpec::gap + StillH() + CardSpec::gap + CardSpec::rowTop; }
    float Height() { return RowTop() + CardSpec::buttonH + CardSpec::pad + CardSpec::border; }
    static const wchar_t* Label(int id) { return id == Annotate ? TR(annotation_frame_card_annotate) : TR(annotation_frame_card_more); }
    D2D1_RECT_F ButtonBox(int id) {
        const float top = RowTop(), left = CardSpec::border + CardSpec::pad;
        const float wa = K::Width(*c_, Label(Annotate), CardSpec::Button()) + 2 * CardSpec::buttonPadX;
        if (id == Annotate) return {left, top, left + wa, top + CardSpec::buttonH};
        const float wm = K::Width(*c_, Label(More), CardSpec::Button()) + 2 * CardSpec::buttonPadX, x = left + wa + CardSpec::buttonGap;
        return {x, top, x + wm, top + CardSpec::buttonH};
    }
    int Hit(float x, float y) { for (int id : {Annotate, More}) if (In(ButtonBox(id), x, y)) return id; return None; }
    void Invalidate() { if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE); }
    void RequestFrame() { if (!hwnd_ || offscreen_) return; if (clockOk_) clock_.Request(); else SetTimer(hwnd_, 1, 16, nullptr); }
    float Alpha(Motion::Time now) const { return fadeFrom_ + (fadeTo_ - fadeFrom_) * Motion::Clamp(Ms(fadeAt_, now) / fadeMs_); }
    // The fade runs on the frame clock; its end is also settled by a plain timer (fade + 40 ms), so a stalled clock (a busy
    // UI thread, a desktop not composed) can never leave the card half shown, or shown after it was asked to go.
    static constexpr UINT_PTR SettleTimer = 2;
    void Fade(float to, int ms) {
        const auto now = Motion::Now(); fadeFrom_ = Alpha(now); fadeTo_ = to; fadeAt_ = now; fadeMs_ = static_cast<float>(std::max(1, ms));
        if (hwnd_ && !offscreen_) SetTimer(hwnd_, SettleTimer, static_cast<UINT>(std::max(1, ms) + 40), nullptr);
    }
    bool Animating() const {
        const auto now = Motion::Now();
        if (Ms(fadeAt_, now) < fadeMs_) return true;
        for (const auto& g : glow_) { const float want = g.first == hover_ ? 1.f : 0.f; if (std::fabs(g.second - want) > .001f) return true; }
        return false;
    }
    void Step() {
        const auto now = Motion::Now(); const float dt = lastStep_ == Motion::Time{} ? 16 : std::min(100.f, Ms(lastStep_, now)); lastStep_ = now;
        PopupKit::SetAlpha(hwnd_, Alpha(now));
        if (closing_ && Ms(fadeAt_, now) >= fadeMs_) { Finish(); return; }
        for (auto& g : glow_) { const float want = g.first == hover_ ? 1.f : 0.f, step = dt / 120.f; g.second = want > g.second ? std::min(want, g.second + step) : std::max(want, g.second - step); }
        Invalidate();
        if (Animating()) RequestFrame(); else if (!clockOk_ && hwnd_) KillTimer(hwnd_, 1);
    }
    void Draw() {
        auto* t = c_->RenderTarget(); if (!t) return;
        P::TextLook(*c_, t); t->Clear(D2D1::ColorF(0, 0, 0, 0));
        const D2D1_RECT_F box{0, 0, CardSpec::width, Height()};
        K::Fill(*c_, box, CardSpec::radius, K::Col(CardSpec::surface)); K::Border(*c_, box, CardSpec::radius, CardSpec::border, K::Col(CardSpec::edge));
        const float x = CardSpec::border + CardSpec::pad; float y = CardSpec::border + CardSpec::pad;
        const float th = TitleH(), wh = WhyH(), sh = StillH();
        K::Text(*c_, TR(annotation_frame_card_title), {x, y, x + Inner(), y + th}, CardSpec::Title(), K::Col(K::Token::text), DWRITE_TEXT_ALIGNMENT_LEADING, true); y += th + CardSpec::gap;
        K::Text(*c_, TR(annotation_frame_card_why), {x, y, x + Inner(), y + wh}, CardSpec::Body(), K::Col(CardSpec::body), DWRITE_TEXT_ALIGNMENT_LEADING, true); y += wh + CardSpec::gap;
        K::Text(*c_, TR(annotation_frame_card_still), {x, y, x + Inner(), y + sh}, CardSpec::Body(), K::Col(CardSpec::body), DWRITE_TEXT_ALIGNMENT_LEADING, true);
        for (int id : {Annotate, More}) {
            const auto r = ButtonBox(id); const float glow = glow_.count(id) ? glow_[id] : 0.f, press = pressed_ == id && hover_ == id ? 1.f : 0.f;
            if (id == Annotate) { const auto base = K::FromHex(accent_); const auto col = Design::Accent::StateColor({base.r / 255.f, base.g / 255.f, base.b / 255.f}, glow, press); K::Fill(*c_, r, CardSpec::buttonRadius, D2D1::ColorF(col.r, col.g, col.b, 1)); }
            else K::Fill(*c_, r, CardSpec::buttonRadius, K::Col(K::Lerp(K::Lerp(K::FromHex(CardSpec::neutral), K::FromHex(CardSpec::neutralHover), glow), K::FromHex(CardSpec::neutralHover), press)));
            const float ty = r.top + (CardSpec::buttonH - CardSpec::Button().line) / 2;
            K::Text(*c_, Label(id), {r.left, ty, r.right, ty + CardSpec::Button().line}, CardSpec::Button(), K::Col(K::Token::text), DWRITE_TEXT_ALIGNMENT_CENTER);
        }
    }
    void Paint() {
        if (!chrome_.RenderTarget()) chrome_.Resize();
        auto* t = chrome_.RenderTarget(); if (!t) return;
        t->BeginDraw(); Draw(); if (t->EndDraw() == D2DERR_RECREATE_TARGET) chrome_.RecreateTarget();
    }
    void Activate(int id) {
        if (!open_ || closing_) return;
        if (id == Annotate && hooks_.annotate) hooks_.annotate();
        else if (id == More && hooks_.more) hooks_.more();
    }
    void Finish() {
        if (!hwnd_) return;
        if (GetCapture() == hwnd_) ReleaseCapture();
        KillTimer(hwnd_, SettleTimer); PopupKit::SetAlpha(hwnd_, 0); ShowWindow(hwnd_, SW_HIDE);
        open_ = closing_ = inside_ = false; hover_ = pressed_ = None; glow_.clear(); fadeFrom_ = fadeTo_ = 0;
        if (!clockOk_) KillTimer(hwnd_, 1);
    }
    void AttachUia() {
        Accessibility::Host host; host.window = hwnd_;
        host.nodes = [this] {
            std::vector<Accessibility::Node> nodes; RECT win{}; GetWindowRect(hwnd_, &win);
            Accessibility::Node g; g.key = 500; g.role = Accessibility::Role::Group; g.focusable = false; g.bounds = win; g.automationId = L"AnnotationFrameCard";
            g.name = TR(annotation_frame_card_title); g.help = std::wstring(TR(annotation_frame_card_why)) + L" " + TR(annotation_frame_card_still); nodes.push_back(g);
            for (int id : {Annotate, More}) {
                const auto r = ButtonBox(id); Accessibility::Node n; n.key = id; n.parent = 500; n.role = Accessibility::Role::Button; n.name = Label(id);
                n.automationId = id == Annotate ? L"AnnotationFrameCardAnnotate" : L"AnnotationFrameCardMore";
                n.bounds = {win.left + Px(r.left), win.top + Px(r.top), win.left + Px(r.right), win.top + Px(r.bottom)}; nodes.push_back(n);
            }
            return nodes;
        };
        host.focused = []() -> long long { return 0; };   // never the keyboard (the page keeps it)
        host.invoke = [this](long long key) { PostMessageW(hwnd_, WM_APP + 1, WPARAM(key), 0); };
        uia_.Attach(std::move(host));
    }
    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<Card*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (msg == WM_NCCREATE) { self = static_cast<Card*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); }
        return self && self->hwnd_ == h ? self->Handle(h, msg, w, l) : DefWindowProcW(h, msg, w, l);
    }
    LRESULT Handle(HWND h, UINT msg, WPARAM w, LPARAM l) {
        const float s = Scale();
        switch (msg) {
        case WM_GETOBJECT: { LRESULT r; if (uia_.OnGetObject(h, w, l, r)) return r; break; }
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); Paint(); EndPaint(h, &ps); return 0; }
        case FrameMessage: if (l == FrameClock::ClockLost) { clock_.Stop(); clockOk_ = false; SetTimer(h, 1, 16, nullptr); return 0; } clock_.Acknowledge(); Step(); return 0;
        case WM_TIMER:
            if (w == 1) Step();
            else if (w == SettleTimer) { KillTimer(h, SettleTimer); if (closing_) Finish(); else if (open_) PopupKit::SetAlpha(h, Alpha(Motion::Now())); }   // the fade's end, whatever the clock did
            return 0;
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;   // the page keeps the keyboard
        case WM_MOUSEMOVE: {
            if (!inside_) { inside_ = true; TRACKMOUSEEVENT tr{sizeof(tr), TME_LEAVE, h, 0}; TrackMouseEvent(&tr); if (hooks_.entered) hooks_.entered(); }
            const int id = Hit(GET_X_LPARAM(l) / s, GET_Y_LPARAM(l) / s); if (id != hover_) { hover_ = id; if (id) glow_[id]; RequestFrame(); }
            return 0;
        }
        case WM_MOUSELEAVE: inside_ = false; if (hover_) { hover_ = None; RequestFrame(); } if (hooks_.left) hooks_.left(); return 0;
        case WM_LBUTTONDOWN: pressed_ = Hit(GET_X_LPARAM(l) / s, GET_Y_LPARAM(l) / s); if (pressed_) SetCapture(h); Invalidate(); return 0;
        case WM_LBUTTONUP: { const int was = pressed_; pressed_ = None; if (GetCapture() == h) ReleaseCapture(); const int id = Hit(GET_X_LPARAM(l) / s, GET_Y_LPARAM(l) / s); Invalidate(); if (id && id == was) Activate(id); return 0; }
        case WM_CAPTURECHANGED: if (pressed_) { pressed_ = None; Invalidate(); } return 0;
        case WM_APP + 1: Activate(static_cast<int>(w)); return 0;
        }
        return DefWindowProcW(h, msg, w, l);
    }
public:
    Card() = default; Card(const Card&) = delete; Card& operator=(const Card&) = delete;
    ~Card() { Destroy(); }
    bool Visible() const { return open_ && !closing_; }
    HWND Window() const { return hwnd_; }
    // The pointer is on the card now (screen test: the page's « leave » came because the pointer went onto it).
    bool PointerInside() const {
        if (!hwnd_ || !open_ || !IsWindowVisible(hwnd_)) return false;
        POINT p{}; RECT r{}; return GetCursorPos(&p) && GetWindowRect(hwnd_, &r) && PtInRect(&r, p);
    }
    // The window (made once, kept hidden between hovers; destroyed by Destroy at the mode's end).
    bool Ensure(HINSTANCE instance, HWND owner, const std::wstring& root) {
        if (hwnd_) return true;
        owner_ = owner; root_ = root;
        if (!PopupKit::Register(instance, L"BrowserAnnotationFrameCard", Proc)) return false;
        hwnd_ = PopupKit::CreatePopup(instance, owner, L"BrowserAnnotationFrameCard", TR(annotation_frame_card_title), WS_EX_NOACTIVATE, this, CardSpec::radius);
        if (!hwnd_) return false;
        dpi_ = GetDpiForWindow(owner ? owner : hwnd_); if (!dpi_) dpi_ = 96;
        if (FAILED(chrome_.Init(hwnd_, root_))) { DestroyWindow(hwnd_); hwnd_ = nullptr; return false; }
        clockOk_ = clock_.Start(hwnd_, FrameMessage); AttachUia();
        return true;
    }
    // The card's size in screen pixels at the owner's DPI (Ensure first).
    SIZE SizePx() { if (owner_) { const UINT d = GetDpiForWindow(owner_); if (d) dpi_ = d; } return {Px(CardSpec::width), Px(Height())}; }
    // Shown (or moved, when already shown) with its top-left corner at `at` (screen pixels); fades in from what is on screen.
    bool Show(POINT at, unsigned accent, CardHooks hooks) {
        if (!hwnd_) return false;
        accent_ = accent; hooks_ = std::move(hooks);
        const SIZE sz = SizePx();
        SetWindowPos(hwnd_, HWND_TOP, at.x, at.y, sz.cx, sz.cy, SWP_NOACTIVATE);
        NativeUI::ConfigureShadowlessPopup(hwnd_, CardSpec::radius); chrome_.Resize();
        const bool was = open_ && !closing_;
        open_ = true; closing_ = false; lastStep_ = {};
        if (!was) { hover_ = pressed_ = None; glow_.clear(); inside_ = false; Fade(1, CardSpec::fadeInMs); }
        Paint(); PopupKit::SetAlpha(hwnd_, Alpha(Motion::Now())); ShowWindow(hwnd_, SW_SHOWNOACTIVATE); RequestFrame();
        if (!was && uia_.Attached()) uia_.Raise(500, UIA_Window_WindowOpenedEventId);
        return true;
    }
    // Fades out (120 ms), then hidden.
    void Close() { if (!hwnd_ || !open_ || closing_) return; closing_ = true; Fade(0, CardSpec::fadeOutMs); RequestFrame(); }
    // Hidden at once (no fade).
    void Hide() { if (hwnd_ && open_) Finish(); }
    void Destroy() {
        if (!hwnd_) return;
        if (uia_.Attached()) uia_.Detach();
        clock_.Stop(); KillTimer(hwnd_, 1); chrome_.Dispose(); DestroyWindow(hwnd_); hwnd_ = nullptr; open_ = closing_ = inside_ = false;
    }

    // ---- tests (no window: offscreen renders, geometry) ----
    void SetupForTest(const std::wstring& root, unsigned accent, CardHooks hooks) { root_ = root; accent_ = accent; hooks_ = std::move(hooks); offscreen_ = true; open_ = true; closing_ = false; }
    float HeightForTest(Chrome& c) { Chrome* keep = c_; c_ = &c; const float h = Height(); c_ = keep; return h; }
    D2D1_RECT_F ButtonForTest(int id, Chrome& c) { Chrome* keep = c_; c_ = &c; const auto r = ButtonBox(id); c_ = keep; return r; }
    void HoverForTest(int id) { hover_ = id; glow_.clear(); if (id) glow_[id] = 1; }
    void ActivateForTest(int id) { Activate(id); }
    bool RenderForTest(UINT dpi, const wchar_t* path, Chrome& measure) {
        dpi_ = dpi; const float h = HeightForTest(measure);
        return PopupKit::RenderPng(root_, CardSpec::width, h, dpi, path, c_, [this] { auto* t = c_->RenderTarget(); if (!t) return; t->BeginDraw(); Draw(); t->EndDraw(); });
    }
};

// ---------------------------------------------------------------- the help panel (26HA-0)
namespace HelpSpec {
inline constexpr float width = 332;   // the drawers' column (Réglages: SettingsUI::Layout::width), docked like them
inline constexpr float pad = 16, gap = 16, sectionGap = 4, header = 20, closeBox = 24, topFade = 16;
inline constexpr float viewTop = pad + header;            // 36: the header stays, the rest scrolls under it
inline constexpr float bodyTop = viewTop + gap;           // 52: the intro, at rest
inline constexpr unsigned body = 0xBFBFBF, closeGlyph = 0x8C8C8C;
inline constexpr float hairline = .02f;                   // the drawers' leading edge: white 2 % over the canvas (DownloadsUI::Popup::border)
inline K::Font Title() { return {15, 20, 600, 0}; }
inline K::Font Close() { return {16, 16, 400, 0}; }
inline K::Font Sub() { return {13, 18, 600, 0}; }
inline K::Font Text() { return {13, 19, 400, 0}; }
inline const wchar_t* Glyph() { return L"×"; }   // i18n-exempt: the board's « × » glyph (no words)
}
struct HelpHooks {
    std::function<void(bool keyboard)> closed;            // gone (any way); keyboard: Échap / Entrée on « × »
    std::function<void(const std::wstring&)> announce;   // screen readers
};

class Help {
public:
    static constexpr UINT FrameMessage = WM_APP + 0x4E3;
    enum Hit : int { None = 0, CloseHit = 1 };
private:
    HWND hwnd_ = nullptr, owner_ = nullptr; std::wstring root_;
    Chrome chrome_; Chrome* c_ = &chrome_; Tooltip::Tools tip_; Accessibility::Bridge uia_; FrameClock::Clock clock_; bool clockOk_ = false;
    HelpHooks hooks_; Downloads::Lane slide_, scroll_, glow_;
    int hover_ = None, pressed_ = None, focus_ = None; bool keyboard_ = false, visible_ = false, closing_ = false, tracking_ = false, reduced_ = false, offscreen_ = false;
    int appliedVisible_ = -1, appliedEntered_ = -1, lead_ = 0; RECT appliedOwner_{}, appliedWindow_{};
    float testHeight_ = 0;
    DrawerCorners::Pair corners_;   // Mode Focus: the antialiased top corners

    float Dpi() const { return static_cast<float>(GetDpiForWindow(hwnd_ ? hwnd_ : owner_)); }
    int Px(float dip) const { return static_cast<int>(std::lround(dip * Dpi() / 96.f)); }
    int OwnerPx(float dip) const { return static_cast<int>(std::lround(dip * GetDpiForWindow(owner_) / 96.f)); }
    float Height() const { if (offscreen_) return testHeight_; RECT r{}; if (hwnd_) GetClientRect(hwnd_, &r); return r.bottom * 96.f / std::max(1.f, Dpi()); }
    RECT OwnerClient() const { RECT r{}; GetClientRect(owner_, &r); MapWindowPoints(owner_, nullptr, reinterpret_cast<POINT*>(&r), 2); return r; }
    float LeadDip() const { return offscreen_ ? 0.f : lead_ * 96.f / std::max(1.f, Dpi()); }
    // The content's offset in DIP while it slides: right of its place docked on the right, left of it docked on the left (#143).
    float OffsetDip() const { return offscreen_ ? 0.f : NativeUI::DrawerSlideOffset(lead_, left_) * 96.f / std::max(1.f, Dpi()); }
    bool left_ = false;   // #143: docked on the left (the side panel's layouts), as last placed by ApplyGeometry
    static float TextW() { return HelpSpec::width - 2 * HelpSpec::pad; }   // 300
    struct Section { const wchar_t* title; const wchar_t* text; };
    static std::vector<Section> Sections() {
        return {{nullptr, TR(annotation_frame_help_intro)}, {TR(annotation_frame_help_sees_title), TR(annotation_frame_help_sees)},
                {TR(annotation_frame_help_hidden_title), TR(annotation_frame_help_hidden)}, {TR(annotation_frame_help_why_title), TR(annotation_frame_help_why)},
                {TR(annotation_frame_help_do_title), TR(annotation_frame_help_do)}};
    }
    // The body's bottom (DIP, unscrolled), its last line included; the panel's padding comes under it.
    float BodyBottom() {
        float y = HelpSpec::bodyTop; bool first = true;
        for (const auto& s : Sections()) {
            if (!first) y += HelpSpec::gap; first = false;
            if (s.title) y += HelpSpec::Sub().line + HelpSpec::sectionGap;
            y += K::WrappedHeight(*c_, s.text, TextW(), HelpSpec::Text());
        }
        return y;
    }
    float MaxScroll() { return std::max(0.f, BodyBottom() + HelpSpec::pad - Height()); }
    float GlyphW() { return K::Width(*c_, HelpSpec::Glyph(), HelpSpec::Close()); }
    // « × » (Paper 26HD-0: 16/16 at the header row's right, centred on its 20 px line) and its 24 px button around it.
    D2D1_RECT_F GlyphBox() { const float w = GlyphW(), x = HelpSpec::width - HelpSpec::pad - w, y = HelpSpec::pad + (HelpSpec::header - HelpSpec::Close().line) / 2; return {x, y, x + w, y + HelpSpec::Close().line}; }
    D2D1_RECT_F CloseBox() { const auto g = GlyphBox(); const float cx = (g.left + g.right) / 2, cy = (g.top + g.bottom) / 2, h = HelpSpec::closeBox / 2; return {cx - h, cy - h, cx + h, cy + h}; }
    int HitAt(float x, float y) { x -= OffsetDip(); const auto b = CloseBox(); return x >= b.left && x < b.right && y >= b.top && y < b.bottom ? CloseHit : None; }
    void Invalidate() { if (hwnd_ && visible_) InvalidateRect(hwnd_, nullptr, FALSE); }
    void RequestFrame() { if (!hwnd_ || offscreen_) return; if (clockOk_) clock_.Request(); else SetTimer(hwnd_, 1, 16, nullptr); }
    bool Moving(Motion::Time now) const { return slide_.Moving(now) || scroll_.Moving(now) || glow_.Moving(now); }
    void SetHover(int hit) {
        if (hit == hover_) return; hover_ = hit; const auto now = Motion::Now();
        glow_.Set(hit == CloseHit ? 1.f : 0.f, now, Motion::Milliseconds(0), reduced_ ? Motion::Milliseconds(0) : Motion::Milliseconds(120)); Invalidate(); RequestFrame();
    }
    void ScrollTo(float target) {
        const auto now = Motion::Now(); target = std::clamp(target, 0.f, MaxScroll());
        scroll_.Set(target, now, Motion::Milliseconds(0), reduced_ ? Motion::Milliseconds(0) : Downloads::DrawerMotion::scroll); Invalidate(); RequestFrame();
    }
    void Draw(Motion::Time now) {
        auto* t = c_->RenderTarget(); if (!t) return;
        P::TextLook(*c_, t);
        const float canvas = Design::Surface::canvas, h = Height(), lead = LeadDip();
        t->Clear(D2D1::ColorF(canvas, canvas, canvas, 1));
        t->SetTransform(D2D1::Matrix3x2F::Translation(OffsetDip(), 0));
        // the body, scrolled under the header
        const float scroll = offscreen_ ? scroll_.Target() : scroll_.Value(now);
        t->PushAxisAlignedClip(D2D1::RectF(0, HelpSpec::viewTop, HelpSpec::width, h), D2D1_ANTIALIAS_MODE_ALIASED);
        float y = HelpSpec::bodyTop - scroll; bool first = true;
        for (const auto& s : Sections()) {
            if (!first) y += HelpSpec::gap; first = false;
            if (s.title) { K::Text(*c_, s.title, {HelpSpec::pad, y, HelpSpec::pad + TextW(), y + HelpSpec::Sub().line}, HelpSpec::Sub(), K::Col(K::Token::text)); y += HelpSpec::Sub().line + HelpSpec::sectionGap; }
            const float th = K::WrappedHeight(*c_, s.text, TextW(), HelpSpec::Text());
            if (y + th > HelpSpec::viewTop - 1 && y < h + 1) K::Text(*c_, s.text, {HelpSpec::pad, y, HelpSpec::pad + TextW(), y + th}, HelpSpec::Text(), K::Col(HelpSpec::body), DWRITE_TEXT_ALIGNMENT_LEADING, true);
            y += th;
        }
        if (scroll > 0) K::Gradient(*c_, {0, HelpSpec::viewTop, HelpSpec::width, HelpSpec::viewTop + HelpSpec::topFade}, 0, {0, HelpSpec::viewTop}, {0, HelpSpec::viewTop + HelpSpec::topFade},
                                    {{0, K::FromHex(K::Token::canvas), 1}, {1, K::FromHex(K::Token::canvas), 0}}, Motion::Clamp(scroll / HelpSpec::topFade));   // Réglages' fade
        t->PopAxisAlignedClip();
        // the header
        K::Text(*c_, TR(annotation_frame_help_title), {HelpSpec::pad, HelpSpec::pad, CloseBox().left - 4, HelpSpec::pad + HelpSpec::header}, HelpSpec::Title(), K::Col(K::Token::text), DWRITE_TEXT_ALIGNMENT_LEADING, false, true);
        const float glow = offscreen_ ? (hover_ == CloseHit ? 1.f : 0.f) : glow_.Value(now), press = pressed_ == CloseHit && hover_ == CloseHit ? 1.f : 0.f;
        const auto style = Design::Resolve(Design::ButtonVariant::Ghost, {glow, press, false, focus_ == CloseHit && keyboard_});
        if (style.fill) c_->Round(CloseBox(), style.radius, style.surface);
        if (style.focus) c_->FocusRing(CloseBox(), style.radius);
        K::Text(*c_, HelpSpec::Glyph(), GlyphBox(), HelpSpec::Close(), K::Col(HelpSpec::closeGlyph));
        // the drawers' hairline toward the page: one physical pixel (on the right edge when docked on the left, #143)
        const float px = t->GetSize().width / std::max(1.f, static_cast<float>(t->GetPixelSize().width)), edge = left_ ? HelpSpec::width - px : 0.f;
        K::Fill(*c_, {edge, 0, edge + px, h}, 0, PopupKit::White(HelpSpec::hairline));
        t->SetTransform(D2D1::Matrix3x2F::Identity());
        if (!offscreen_) { chrome_.KeyOut(lead, left_); KeyCorners(); }
    }
    void PaintNow() {
        if (!chrome_.RenderTarget()) chrome_.Resize();
        auto* t = chrome_.RenderTarget(); if (!t) return;
        t->BeginDraw(); Draw(Motion::Now()); if (t->EndDraw() == D2DERR_RECREATE_TARGET) chrome_.RecreateTarget();
    }
    void RefreshTips() {
        if (!tip_) return;
        tip_.Clear(); const auto b = CloseBox(); tip_.Add(CloseHit, {Px(b.left), Px(b.top), Px(b.right), Px(b.bottom)}, TR(common_close_tip));
    }
    // The column docked on the tab panel's side under the toolbar, where the drawers dock (#143: the window's right edge in
    // the top bar, the side panel's edge in its layouts; src/history-panel.h's ApplyGeometry): the window never moves while
    // sliding, the content drawn toward its entering edge and the strip it has not entered keyed out.
    bool ApplyGeometry(Motion::Time now, bool force = false) {
        if (!hwnd_) return false;
        namespace DL = Downloads::DrawerLayout;
        const RECT owner = OwnerClient();
        const int column = OwnerPx(HelpSpec::width), top = owner.top + OwnerPx(DL::liveTop), height = std::max(1, static_cast<int>(owner.bottom - OwnerPx(DL::liveBottom) - top));
        const int shown = static_cast<int>(std::lround(column * Motion::Clamp(slide_.Value(now))));
        const bool left = DL::liveLeft; const int inset = left ? 0 : OwnerPx(DL::liveRight), dock = left ? OwnerPx(DL::liveDock) : 0;   // Mode Focus: the floating panel's right edge (0 otherwise)
        const RECT window = NativeUI::DockedDrawerRect(owner, column, top, height, left, dock, inset);
        const float radius = left ? 0.f : DL::liveRadius * GetDpiForWindow(owner_) / 96.f;   // Mode Focus: round top corners (the top bar's floating panel only)
        const bool restyled = !EqualRect(&window, &appliedWindow_) || radius != corners_.Radius() || left != left_;
        if (!force && !restyled && shown == appliedVisible_ && EqualRect(&owner, &appliedOwner_)) return false;
        const bool resized = !EqualRect(&owner, &appliedOwner_) || !EqualRect(&window, &appliedWindow_) || left != left_, slid = shown != appliedVisible_;
        appliedVisible_ = shown; appliedOwner_ = owner; lead_ = column - shown; left_ = left; corners_.SetRadius(radius);
        if (force || !EqualRect(&window, &appliedWindow_)) { SetWindowPos(hwnd_, nullptr, window.left, window.top, column, height, SWP_NOACTIVATE | SWP_NOZORDER); appliedWindow_ = window; }
        const int entered = shown > 0;
        if (force || resized || entered != appliedEntered_) { SetWindowRgn(hwnd_, NativeUI::DrawerRegion(entered, column, height, OwnerPx(20), IsZoomed(owner_) || !NativeUI::DrawerOnWindowCorner(left, dock, inset, DL::Side::roundAgainstPanel), left), FALSE); appliedEntered_ = entered; }
        if (lead_ > 0) corners_.HideLeading();
        if (restyled && !force) InvalidateRect(hwnd_, nullptr, FALSE);
        if (resized) { scroll_.Snap(std::clamp(scroll_.Target(), 0.f, MaxScroll())); RefreshTips(); }
        if (force || !slid || shown == 0 || !IsWindowVisible(hwnd_)) { SyncCorners(); return false; }
        PaintNow(); ValidateRect(hwnd_, nullptr); SyncCorners();
        return true;
    }
    void SyncCorners() { if (!visible_ || appliedVisible_ <= 0 || !IsWindowVisible(hwnd_)) { corners_.Hide(); return; } int from = 0, to = 0; NativeUI::DrawerContentColumn(lead_, appliedWindow_.right - appliedWindow_.left, left_, from, to); const float c = Design::Surface::canvas; corners_.Place(hwnd_, appliedWindow_, from, to, c + (1 - c) * HelpSpec::hairline, c, lead_ == 0); }
    void KeyCorners() { if (corners_.Radius() <= 0) return; int from = 0, to = 0; NativeUI::DrawerContentColumn(lead_, appliedWindow_.right - appliedWindow_.left, left_, from, to); HRGN cut = corners_.CutRegion(from, to); chrome_.KeyOutRegion(cut); DeleteObject(cut); }
    void Finish() {
        closing_ = false; visible_ = false; appliedVisible_ = -1; appliedWindow_ = {};
        corners_.Hide(); if (hwnd_) ShowWindow(hwnd_, SW_HIDE);
    }
    void Step() {
        if (!visible_) return;
        const auto now = Motion::Now();
        const bool painted = ApplyGeometry(now);
        if (closing_ && !slide_.Moving(now)) { Finish(); return; }
        if (!painted) Invalidate();
        if (Moving(now)) RequestFrame(); else if (!clockOk_ && hwnd_) KillTimer(hwnd_, 1);
    }
    void Key(WPARAM w) {
        if (w == VK_ESCAPE) { Hide(true, true); return; }
        if (w == VK_TAB) { keyboard_ = true; focus_ = focus_ == CloseHit ? None : CloseHit; Invalidate(); if (focus_ && uia_.Attached()) uia_.Raise(CloseHit, UIA_AutomationFocusChangedEventId); return; }
        if ((w == VK_RETURN || w == VK_SPACE) && focus_ == CloseHit) { Hide(true, true); return; }
        const float page = std::max(40.f, Height() - HelpSpec::viewTop - 40);
        if (w == VK_DOWN) ScrollTo(scroll_.Target() + 40); else if (w == VK_UP) ScrollTo(scroll_.Target() - 40);
        else if (w == VK_NEXT) ScrollTo(scroll_.Target() + page); else if (w == VK_PRIOR) ScrollTo(scroll_.Target() - page);
        else if (w == VK_HOME) ScrollTo(0); else if (w == VK_END) ScrollTo(MaxScroll());
    }
    std::wstring Whole() const {
        std::wstring all = TR(annotation_frame_help_title);
        for (const auto& s : Sections()) { all += L"\n"; if (s.title) { all += s.title; all += L"\n"; } all += s.text; }
        return all;
    }
    void AttachUia() {
        Accessibility::Host host; host.window = hwnd_;
        host.nodes = [this] {
            std::vector<Accessibility::Node> nodes; RECT win{}; GetWindowRect(hwnd_, &win);
            Accessibility::Node g; g.key = 500; g.role = Accessibility::Role::Group; g.focusable = false; g.bounds = win; g.automationId = L"AnnotationFrameHelp";
            g.name = TR(annotation_frame_help_title); g.help = Whole(); nodes.push_back(g);
            const auto b = CloseBox(); Accessibility::Node n; n.key = CloseHit; n.parent = 500; n.role = Accessibility::Role::Button; n.name = TR(common_close); n.accelerator = L"Esc";   // i18n-exempt: key name (UIA accelerator)
            n.automationId = L"AnnotationFrameHelpClose"; n.bounds = {win.left + Px(b.left + OffsetDip()), win.top + Px(b.top), win.left + Px(b.right + OffsetDip()), win.top + Px(b.bottom)}; nodes.push_back(n);
            return nodes;
        };
        host.focused = [this]() -> long long { return focus_; };
        host.invoke = [this](long long key) { if (key == CloseHit) PostMessageW(hwnd_, WM_APP + 1, 0, 0); };
        uia_.Attach(std::move(host));
    }
    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<Help*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (msg == WM_NCCREATE) { self = static_cast<Help*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); self->hwnd_ = h; }
        { LRESULT border = 0; if (NativeChrome::ResizeBorder::Claim(h, msg, w, l, border)) return border; }   // flush with the window's right edge and bottom: its resize border wins there
        return self ? self->Handle(h, msg, w, l) : DefWindowProcW(h, msg, w, l);
    }
    LRESULT Handle(HWND h, UINT msg, WPARAM w, LPARAM l) {
        const float scale = 96.f / std::max(1.f, Dpi());
        switch (msg) {
        case WM_GETOBJECT: { LRESULT r; if (uia_.OnGetObject(h, w, l, r)) return r; break; }
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); PaintNow(); EndPaint(h, &ps); return 0; }
        case WM_SIZE: chrome_.Resize(); Invalidate(); return 0;
        case WM_DPICHANGED: ApplyGeometry(Motion::Now(), true); return 0;
        case FrameMessage: if (l == FrameClock::ClockLost) { clock_.Stop(); clockOk_ = false; SetTimer(h, 1, 16, nullptr); return 0; } clock_.Acknowledge(); Step(); return 0;
        case WM_TIMER: if (w == 1) Step(); return 0;
        case WM_MOUSEACTIVATE: return MA_ACTIVATE;
        case WM_ACTIVATE: if (LOWORD(w) == WA_INACTIVE) Hide(false, true); return 0;   // a click elsewhere, as every drawer
        case WM_SETFOCUS: case WM_KILLFOCUS: Invalidate(); return 0;
        case WM_MOUSEMOVE: {
            if (!tracking_) { TRACKMOUSEEVENT tr{sizeof(tr), TME_LEAVE, h, 0}; TrackMouseEvent(&tr); tracking_ = true; }
            SetHover(HitAt(GET_X_LPARAM(l) * scale, GET_Y_LPARAM(l) * scale)); return 0;
        }
        case WM_MOUSELEAVE: tracking_ = false; SetHover(None); return 0;
        case WM_LBUTTONDOWN: pressed_ = HitAt(GET_X_LPARAM(l) * scale, GET_Y_LPARAM(l) * scale); keyboard_ = false; if (pressed_) SetCapture(h); Invalidate(); return 0;
        case WM_LBUTTONUP: { const int was = pressed_, hit = HitAt(GET_X_LPARAM(l) * scale, GET_Y_LPARAM(l) * scale); pressed_ = None; if (GetCapture() == h) ReleaseCapture(); Invalidate(); if (was == CloseHit && hit == CloseHit) Hide(false, true); return 0; }
        case WM_CAPTURECHANGED: if (pressed_) { pressed_ = None; Invalidate(); } return 0;
        case WM_MOUSEWHEEL: ScrollTo(scroll_.Target() - GET_WHEEL_DELTA_WPARAM(w) / 120.f * 40 * 1.5f); return 0;
        case WM_KEYDOWN: Key(w); return 0;
        case WM_APP + 1: Hide(false, true); return 0;   // UIA Invoke on « × »
        }
        return DefWindowProcW(h, msg, w, l);
    }
public:
    Help() { slide_.Snap(0); scroll_.Snap(0); glow_.Snap(0); }
    Help(const Help&) = delete; Help& operator=(const Help&) = delete;
    ~Help() { Destroy(); }
    bool Visible() const { return visible_; }
    bool Open() const { return visible_ && !closing_; }
    bool Closing() const { return closing_; }
    HWND Window() const { return hwnd_; }
    bool Create(HINSTANCE instance, HWND owner, const std::wstring& root) {
        if (hwnd_) return true;
        owner_ = owner; root_ = root;
        WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = instance; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.lpszClassName = L"BrowserAnnotationFrameHelp";
        if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, TR(annotation_frame_help_title), WS_POPUP, 0, 0, OwnerPx(HelpSpec::width), OwnerPx(400), owner, nullptr, instance, this);
        if (!hwnd_) return false;
        tip_.Create(instance, hwnd_, root);
        NativeUI::ConfigureShadowlessPopup(hwnd_);   // DWMWCP_DONOTROUND, no shadow: the region shapes it
        NativeUI::KeyedDrawer(hwnd_);                // the slide's not-yet-entered strip is keyed out
        if (FAILED(chrome_.Init(hwnd_, root))) { DestroyWindow(hwnd_); hwnd_ = nullptr; return false; }
        clockOk_ = clock_.Start(hwnd_, FrameMessage); AttachUia();
        return true;
    }
    // Slides in from its edge (the drawers' side, #143; reopened while leaving: from where it is) and takes the keyboard.
    void Show(bool reduced, HelpHooks hooks, bool keyboard = false) {
        if (!hwnd_) return;
        hooks_ = std::move(hooks); reduced_ = reduced;
        auto now = Motion::Now();
        if (!visible_) { slide_.Snap(0); scroll_.Snap(0); glow_.Snap(0); hover_ = pressed_ = None; tracking_ = false; appliedVisible_ = -1; appliedWindow_ = {}; appliedOwner_ = {}; }
        focus_ = keyboard ? CloseHit : None; keyboard_ = keyboard;
        slide_.Set(1, now, Motion::Milliseconds(0), reduced_ ? Motion::Milliseconds(0) : Downloads::DrawerMotion::enter, Downloads::DrawerMotion::enterSpring);
        closing_ = false; visible_ = true;
        ApplyGeometry(now, true);
        SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);   // painted while shown without activation, then activated: never a blank frame
        PaintNow(); ValidateRect(hwnd_, nullptr); SyncCorners();
        SetForegroundWindow(hwnd_); SetFocus(hwnd_);
        RefreshTips(); RequestFrame();
        if (uia_.Attached()) uia_.Raise(500, UIA_Window_WindowOpenedEventId);
        if (hooks_.announce) hooks_.announce(TR(annotation_frame_help_title));
    }
    // Leaves along the path it came in on; `animate` false hides at once.
    void Hide(bool keyboard, bool animate) {
        if (!visible_ || closing_) { if (visible_ && !animate) Finish(); return; }
        if (GetCapture() == hwnd_) ReleaseCapture();
        hover_ = pressed_ = None; tracking_ = false;
        auto now = Motion::Now(); closing_ = true;
        slide_.Set(0, now, Motion::Milliseconds(0), animate && !reduced_ ? Downloads::DrawerMotion::exit : Motion::Milliseconds(0), Downloads::DrawerMotion::exitSpring);
        if (!slide_.Moving(now)) Finish(); else RequestFrame();
        if (hooks_.closed) { auto closed = hooks_.closed; closed(keyboard); }
    }
    // The owner moved, was resized or changed DPI (or Mode Focus moved the drawers' place).
    void Reposition() { if (visible_) ApplyGeometry(Motion::Now()); }
    void Destroy() {
        if (!hwnd_) return;
        if (uia_.Attached()) uia_.Detach();
        corners_.Hide(); clock_.Stop(); KillTimer(hwnd_, 1); tip_.Dispose(); chrome_.Dispose(); DestroyWindow(hwnd_); hwnd_ = nullptr; visible_ = closing_ = false;
    }

    // ---- tests ----
    void KeyForTest(WPARAM key) { Key(key); }
    void ClickCloseForTest() { Hide(false, true); }
    float SlideForTest() const { return slide_.Value(Motion::Now()); }
    void SettleForTest() { slide_.Snap(slide_.Target()); scroll_.Snap(scroll_.Target()); glow_.Snap(glow_.Target()); Step(); }
    RECT WindowRectForTest() const { RECT r{}; if (hwnd_) GetWindowRect(hwnd_, &r); return r; }
    float BodyBottomForTest(Chrome& c) { Chrome* keep = c_; c_ = &c; const float b = BodyBottom(); c_ = keep; return b; }
    D2D1_RECT_F CloseBoxForTest(Chrome& c) { Chrome* keep = c_; c_ = &c; const auto b = CloseBox(); c_ = keep; return b; }
    D2D1_RECT_F GlyphBoxForTest(Chrome& c) { Chrome* keep = c_; c_ = &c; const auto b = GlyphBox(); c_ = keep; return b; }
    // An offscreen render of the panel `height` DIP high, scrolled by `scroll` (clamped), « × » hovered or not.
    // `left`: docked on the left (the side panel's layouts, #143): the hairline on its right edge, toward the page.
    bool RenderForTest(const std::wstring& root, UINT dpi, float height, float scroll, bool hoverClose, const wchar_t* path, Chrome& measure, bool left = false) {
        const bool keepOff = offscreen_, keepLeft = left_; const float keepH = testHeight_; const int keepHover = hover_;
        offscreen_ = true; left_ = left; testHeight_ = height; hover_ = hoverClose ? CloseHit : None;
        Chrome* keep = c_; c_ = &measure; const float s = std::clamp(scroll, 0.f, MaxScroll()); c_ = keep;
        scroll_.Snap(s);
        const bool ok = PopupKit::RenderPng(root, HelpSpec::width, height, dpi, path, c_, [this] { auto* t = c_->RenderTarget(); if (!t) return; t->BeginDraw(); Draw(Motion::Now()); t->EndDraw(); });
        offscreen_ = keepOff; left_ = keepLeft; testHeight_ = keepH; hover_ = keepHover; scroll_.Snap(0);
        return ok;
    }
};

} // namespace AnnotationFrameCard
} // namespace NativeChrome
