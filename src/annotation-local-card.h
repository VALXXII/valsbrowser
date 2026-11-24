#pragma once
#include "i18n.h"
// « Annoter » on a live site (#125 v2 E1b, Paper « #125 v2 — Tutoriel annotation (validé) » 1STD-0, card 1STE-0): the
// toolbar button is greyed (E1a's tooltip « Mode Annotation - seulement sur vos projets et fichiers locaux ») and a click
// on it — or Ctrl+Maj+A — opens this card under it.
//   316 wide, #171717, border 1 #FFFFFF14, radius 12, padding 16, gap 12. Title 16/20 600 −0.01 em white « Annoter marche
//   sur vos projets et fichiers locaux »; gap 4; body 14/20 #B3B3B3 « <host> est en ligne. Ouvrez votre projet en local
//   (par exemple localhost:5173) ou un fichier pour l'annoter. », the host 500 white. Buttons, right-aligned, gap 8:
//   « Compris » raised #292929 and « Découvrir en 1 minute » in the accent, both h 28, radius 6, padding-inline 12,
//   13/16 500 white; hover / press as the onboarding's buttons.
//   Place: 8 below the button; its right edge 5 left of the button's centre (Paper: card 744 … 1060 at y 80 for the button
//   1053 … 1077, 48 … 72, in the 1180-wide frame).
// Closes with « Compris », Échap, a click elsewhere (it loses the activation), or when the host says the page changed;
// « Découvrir en 1 minute » closes it and asks the host for the tutorial. It comes back at every click (Val's default #2).
// Motion: in 150 ms (opacity), out 120 ms; reduced motion the same (opacity only).
#include <windows.h>
#include <windowsx.h>
#include <functional>
#include <map>
#include <string>
#include "chrome.h"
#include "tooltip.h"
#include "accessibility.h"
#include "frame-clock.h"
#include "native-ui.h"
#include "onboarding-parts.h"

namespace NativeChrome {
namespace AnnotationLocalCard {

namespace K = OnboardingKit; namespace P = OnboardingParts;

inline constexpr float Width = 316, Pad = 16, Radius = 12, ButtonH = 28, Gap = 12, ButtonGap = 8, Below = 8, FromCentre = 5;
struct Hooks {
    std::function<void()> discover;                           // « Découvrir en 1 minute »: the tutorial
    std::function<void()> closed;                             // the card is gone (any way)
    std::function<void(const std::wstring&)> announce;
};

class Card {
public:
    enum Id : int { None = 0, Ok = 1, Discover = 2 };
    static constexpr UINT FrameMessage = WM_APP + 0x4E1;
private:
    HINSTANCE instance_ = nullptr; HWND hwnd_ = nullptr, owner_ = nullptr; std::wstring root_;
    Chrome chrome_; Chrome* c_ = &chrome_; Accessibility::Bridge uia_; FrameClock::Clock clock_; bool clockOk_ = false;
    Hooks hooks_; std::wstring host_; unsigned accent_ = 0x0099A0; bool reduced_ = false, offscreen_ = false, open_ = false, closing_ = false, keyboard_ = false;
    int hover_ = None, pressed_ = None, focus_ = Discover; std::map<int, float> glow_;
    UINT dpi_ = 96; Motion::Time opened_{}, left_{}, lastStep_{};

    float Scale() const { return dpi_ / 96.f; }
    int Px(float dip) const { return static_cast<int>(std::lround(dip * Scale())); }
    static float Ms(Motion::Time a, Motion::Time b) { return static_cast<float>(std::chrono::duration<double, std::milli>(b - a).count()); }
    static bool In(D2D1_RECT_F r, float x, float y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }
    static K::Font TitleFont() { return {16, 20, 600, -.16f}; }
    static K::Font BodyFont() { return {14, 20, 400, 0}; }
    static K::Font ButtonFont() { return {13, 16, 500, 0}; }
    std::wstring Body() const { return TRF(annotation_local_card_body, host_); }
    float Inner() const { return Width - 2 - 2 * Pad; }
    float TitleH() { return K::WrappedHeight(*c_, TR(annotation_local_card_title), Inner(), TitleFont()); }
    float BodyH() { return K::WrappedHeight(*c_, Body(), Inner(), BodyFont()); }
    float Height() { return 1 + Pad + TitleH() + 4 + BodyH() + Gap + ButtonH + Pad + 1; }
    D2D1_RECT_F ButtonBox(int id) {
        const float wd = K::Width(*c_, TR(annotation_local_card_discover), ButtonFont()) + 24, wo = K::Width(*c_, TR(annotation_local_card_ok), ButtonFont()) + 24;
        const float top = Height() - 1 - Pad - ButtonH, right = Width - 1 - Pad;
        return id == Discover ? D2D1_RECT_F{right - wd, top, right, top + ButtonH} : D2D1_RECT_F{right - wd - ButtonGap - wo, top, right - wd - ButtonGap, top + ButtonH};
    }
    int Hit(float x, float y) { for (int id : {Ok, Discover}) if (In(ButtonBox(id), x, y)) return id; return None; }
    void Invalidate() { if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE); }
    void RequestFrame() { if (!hwnd_ || offscreen_) return; if (clockOk_) clock_.Request(); else SetTimer(hwnd_, 1, 16, nullptr); }
    bool Animating() const {
        const auto now = Motion::Now();
        if (open_ && !closing_ && Ms(opened_, now) < 150) return true;
        if (closing_) return true;
        for (const auto& g : glow_) { const float want = g.first == hover_ ? 1.f : 0.f; if (std::fabs(g.second - want) > .001f) return true; }
        return false;
    }
    void Step() {
        const auto now = Motion::Now(); const float dt = lastStep_ == Motion::Time{} ? 16 : std::min(100.f, Ms(lastStep_, now)); lastStep_ = now;
        if (closing_) { const float u = Motion::Clamp(Ms(left_, now) / 120.f); PopupKit::SetAlpha(hwnd_, 1 - u); if (u >= 1) { Finish(); return; } }
        else PopupKit::SetAlpha(hwnd_, Motion::Clamp(Ms(opened_, now) / 150.f));
        for (auto& g : glow_) { const float want = g.first == hover_ ? 1.f : 0.f, step = dt / 120.f; g.second = want > g.second ? std::min(want, g.second + step) : std::max(want, g.second - step); }
        Invalidate();
        if (Animating()) RequestFrame(); else if (!clockOk_ && hwnd_) KillTimer(hwnd_, 1);
    }
    void Draw() {
        auto* t = c_->RenderTarget(); if (!t) return;
        P::TextLook(*c_, t); t->Clear(D2D1::ColorF(0, 0, 0, 0));
        const float h = Height(); const D2D1_RECT_F box{0, 0, Width, h};
        K::Fill(*c_, box, Radius, K::Col(K::Token::canvas)); K::Border(*c_, box, Radius, 1, PopupKit::White(0x14 / 255.f));
        const float x = 1 + Pad, titleH = TitleH(); float y = 1 + Pad;
        K::Text(*c_, TR(annotation_local_card_title), {x, y, x + Inner(), y + titleH}, TitleFont(), K::Col(K::Token::text), DWRITE_TEXT_ALIGNMENT_LEADING, true);
        y += titleH + 4;
        // The host in white 500, the rest #B3B3B3: the sentence is split around « {0} ».
        const std::wstring body = Body(); const auto at = body.find(host_);
        std::vector<K::Span> runs;
        if (at == std::wstring::npos || host_.empty()) runs.push_back({body, K::Col(K::Token::secondary)});
        else {
            if (at > 0) runs.push_back({body.substr(0, at), K::Col(K::Token::secondary)});
            runs.push_back({host_, K::Col(K::Token::text), 500});
            runs.push_back({body.substr(at + host_.size()), K::Col(K::Token::secondary)});
        }
        K::Spans(*c_, runs, {x, y, x + Inner(), y + BodyH()}, BodyFont(), DWRITE_TEXT_ALIGNMENT_LEADING, true);
        for (int id : {Ok, Discover}) {
            const auto r = ButtonBox(id); const float glow = glow_[id], press = pressed_ == id && hover_ == id ? 1.f : 0.f;
            if (id == Discover) { const auto base = K::FromHex(accent_); const auto col = Design::Accent::StateColor({base.r / 255.f, base.g / 255.f, base.b / 255.f}, glow, press); K::Fill(*c_, r, 6, D2D1::ColorF(col.r, col.g, col.b, 1)); }
            else K::Fill(*c_, r, 6, K::Col(K::Lerp(K::Lerp(K::FromHex(K::Token::raised), K::FromHex(K::Token::hover), glow), K::FromHex(K::Token::pressed), press)));
            K::Text(*c_, id == Discover ? TR(annotation_local_card_discover) : TR(annotation_local_card_ok), {r.left, r.top + 6, r.right, r.bottom}, ButtonFont(), K::Col(K::Token::text), DWRITE_TEXT_ALIGNMENT_CENTER);
            if (focus_ == id && keyboard_) PopupKit::InsetRing(*c_, r, 6);
        }
    }
    void Paint() {
        if (!chrome_.RenderTarget()) chrome_.Resize();
        auto* t = chrome_.RenderTarget(); if (!t) return;
        t->BeginDraw(); Draw(); if (t->EndDraw() == D2DERR_RECREATE_TARGET) chrome_.RecreateTarget();
    }
    void Activate(int id) {
        if (id == Discover) { Close(); if (hooks_.discover) hooks_.discover(); }
        else if (id == Ok) Close();
    }
    void Finish() {
        if (!hwnd_) return;
        if (uia_.Attached()) uia_.Detach();
        clock_.Stop(); KillTimer(hwnd_, 1); chrome_.Dispose(); DestroyWindow(hwnd_); hwnd_ = nullptr; open_ = closing_ = false;
        if (hooks_.closed) hooks_.closed();
    }
    void AttachUia() {
        Accessibility::Host host; host.window = hwnd_;
        host.nodes = [this] {
            std::vector<Accessibility::Node> nodes; RECT win{}; GetWindowRect(hwnd_, &win);
            Accessibility::Node g; g.key = 500; g.role = Accessibility::Role::Group; g.focusable = false; g.bounds = win; g.automationId = L"AnnotationLocalCard"; g.name = TR(annotation_local_card_title); g.help = Body(); nodes.push_back(g);
            for (int id : {Ok, Discover}) {
                const auto r = ButtonBox(id); Accessibility::Node n; n.key = id; n.parent = 500; n.role = Accessibility::Role::Button; n.name = id == Ok ? TR(annotation_local_card_ok) : TR(annotation_local_card_discover);
                n.automationId = id == Ok ? L"AnnotationLocalCardOk" : L"AnnotationLocalCardDiscover"; n.bounds = {win.left + Px(r.left), win.top + Px(r.top), win.left + Px(r.right), win.top + Px(r.bottom)}; nodes.push_back(n);
            }
            return nodes;
        };
        host.focused = [this]() -> long long { return focus_; };
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
        case WM_TIMER: if (w == 1) Step(); return 0;
        case WM_MOUSEMOVE: { TRACKMOUSEEVENT tr{sizeof(tr), TME_LEAVE, h, 0}; TrackMouseEvent(&tr); const int id = Hit(GET_X_LPARAM(l) / s, GET_Y_LPARAM(l) / s); if (id != hover_) { hover_ = id; if (id) glow_[id]; RequestFrame(); } return 0; }
        case WM_MOUSELEAVE: if (hover_) { hover_ = None; RequestFrame(); } return 0;
        case WM_LBUTTONDOWN: pressed_ = Hit(GET_X_LPARAM(l) / s, GET_Y_LPARAM(l) / s); keyboard_ = false; if (pressed_) SetCapture(h); Invalidate(); return 0;
        case WM_LBUTTONUP: { const int was = pressed_; pressed_ = None; if (GetCapture() == h) ReleaseCapture(); const int id = Hit(GET_X_LPARAM(l) / s, GET_Y_LPARAM(l) / s); Invalidate(); if (id && id == was) Activate(id); return 0; }
        case WM_KEYDOWN:
            keyboard_ = true;
            if (w == VK_ESCAPE) { Close(); return 0; }
            if (w == VK_TAB || w == VK_LEFT || w == VK_RIGHT) { focus_ = focus_ == Ok ? Discover : Ok; Invalidate(); if (uia_.Attached()) uia_.Raise(focus_, UIA_AutomationFocusChangedEventId); return 0; }
            if (w == VK_RETURN || w == VK_SPACE) { Activate(focus_); return 0; }
            return 0;
        case WM_APP + 1: Activate(static_cast<int>(w)); return 0;
        case WM_ACTIVATE: if (LOWORD(w) == WA_INACTIVE && open_ && !closing_) Close(); return 0;   // a click elsewhere
        case WM_MOUSEACTIVATE: return MA_ACTIVATE;
        }
        return DefWindowProcW(h, msg, w, l);
    }
public:
    Card() = default; Card(const Card&) = delete; Card& operator=(const Card&) = delete;
    ~Card() { if (hwnd_) { hooks_.closed = nullptr; Finish(); } }
    bool Visible() const { return open_ && !closing_; }
    HWND Window() const { return hwnd_; }
    // `button`: the toolbar button in screen pixels; `host`: the live site's name (« boutique-sable.fr »); `accent`: 0xRRGGBB.
    bool Show(HINSTANCE instance, HWND owner, const std::wstring& root, RECT button, std::wstring host, unsigned accent, bool reduced, Hooks hooks) {
        if (hwnd_) { if (closing_) Finish(); else return true; }
        instance_ = instance; owner_ = owner; root_ = root; host_ = std::move(host); accent_ = accent; reduced_ = reduced; hooks_ = std::move(hooks);
        hover_ = pressed_ = None; focus_ = Discover; keyboard_ = false; glow_.clear(); offscreen_ = false;
        if (!PopupKit::Register(instance, L"BrowserAnnotationLocalCard", Proc)) return false;
        hwnd_ = PopupKit::CreatePopup(instance, owner, L"BrowserAnnotationLocalCard", TR(annotation_local_card_title), 0, this, Radius);
        if (!hwnd_) return false;
        dpi_ = GetDpiForWindow(owner ? owner : hwnd_); if (!dpi_) dpi_ = 96;
        if (FAILED(chrome_.Init(hwnd_, root_))) { DestroyWindow(hwnd_); hwnd_ = nullptr; return false; }
        const int w = Px(Width), hgt = Px(Height()), centre = (button.left + button.right) / 2;
        SetWindowPos(hwnd_, HWND_TOP, centre - Px(FromCentre) - w, button.bottom + Px(Below), w, hgt, SWP_NOACTIVATE);
        NativeUI::ConfigureShadowlessPopup(hwnd_, Radius); chrome_.Resize();
        clockOk_ = clock_.Start(hwnd_, FrameMessage); AttachUia();
        open_ = true; closing_ = false; opened_ = Motion::Now(); lastStep_ = {};
        Paint(); ShowWindow(hwnd_, SW_SHOW); SetForegroundWindow(hwnd_); SetFocus(hwnd_); RequestFrame();
        if (uia_.Attached()) uia_.Raise(500, UIA_Window_WindowOpenedEventId);
        if (hooks_.announce) hooks_.announce(std::wstring(TR(annotation_local_card_title)) + L". " + Body());
        return true;
    }
    void Close() { if (!hwnd_ || closing_) return; closing_ = true; left_ = Motion::Now(); if (owner_ && GetForegroundWindow() == hwnd_) SetForegroundWindow(owner_); RequestFrame(); }

    // ---- tests ----
    void SetupForTest(const std::wstring& root, std::wstring host, unsigned accent, Hooks hooks) { root_ = root; host_ = std::move(host); accent_ = accent; hooks_ = std::move(hooks); offscreen_ = true; open_ = true; focus_ = Discover; }
    void ActivateForTest(int id) { if (id == Discover) { if (hooks_.discover) hooks_.discover(); } open_ = false; if (hooks_.closed) hooks_.closed(); }
    float HeightForTest(Chrome& c) { Chrome* keep = c_; c_ = &c; const float h = Height(); c_ = keep; return h; }
    bool RenderForTest(UINT dpi, const wchar_t* path, Chrome& measure) {
        dpi_ = dpi; const float h = HeightForTest(measure);
        return PopupKit::RenderPng(root_, Width, h, dpi, path, c_, [this] { auto* t = c_->RenderTarget(); if (!t) return; t->BeginDraw(); Draw(); t->EndDraw(); });
    }
};

} // namespace AnnotationLocalCard
} // namespace NativeChrome
