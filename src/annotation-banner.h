#pragma once
// « Mode Annotation » band under the toolbar + the toolbar « Annoter » button state.
// Paper (file 01M6Q7YT14ZKMGET54ASPNGQEC, p-H-0, « Bandeau — Mode Annotation » VZZ-0):
//   height 32; background linear-gradient(180deg, #3E67FF00 -> #233FD6 at 32 %) over the
//   canvas #171717; bottom border 1 #3E67FF3D; padding 0 8 0 12; gap 12.
//   Left: the mark (Paper renders it through a « Heatmap » shader 31x29: natively the same
//   glyph, 18x17, filled with the shader's #C0DAFF -> #2F63E7 ramp) + 6 + « Mode Annotation »
//   Inter Display 14/20 #5B84FF.
//   Right: « Quitter » button h 24, radius 3, padding 8/4, gap 8: 13/16 medium #FFFFFFE6 and
//   key cap « Échap » (h 18, radius 3, padding 5, #FFFFFF14, 11/14 regular #FFFFFFB3).
// Toolbar « Annoter » (Paper W09-0, active): 24x24 radius 6, split tint (accent 16 %), mark
// 13x12.25 accent, an accent bar 10x2 radius 1 at x+7, 2 DIP under the button.
// The band is a child layered window of the browser window (above the WebView), created
// at the first Show; it fades in/out (opacity only: the page moves by 32 DIP at once).
#include "annotation-kit.h"

namespace NativeChrome { namespace Annotations {
namespace BannerSpec {
inline constexpr float height = 32, padLeft = 12, padRight = 8, logoW = 31, logoH = 29, logoGap = 6;
inline constexpr float markW = 18, markH = 17;          // Paper WO9-0 mark size inside the 31x29 slot
inline constexpr float quitH = 24, quitPadL = 8, quitPadR = 4, quitGap = 8, quitRadius = 3;
inline constexpr std::uint32_t label = 0x5B84FFFF, border = 0x3E67FF3D, gradientEnd = 0x233FD6FF;
inline constexpr float gradientAlpha = .32f;
}
struct BannerVisual { Motion::InteractionVisual quit; bool focus = false, keyboard = false, reduced = false; };
struct BannerLayout { D2D1_RECT_F logo, label, quit; float keycapX; };
inline BannerLayout LayoutBanner(Ink& ink, float w) {
    using namespace BannerSpec; BannerLayout l;
    l.logo = {padLeft, (height - logoH) / 2, padLeft + logoW, (height + logoH) / 2};
    l.label = {l.logo.right + logoGap, 6, l.logo.right + logoGap + ink.Width(L"Mode Annotation", F(14, 20)) + 1, 26};
    const float quitW = quitPadL + ink.Width(L"Quitter", F(13, 16, 500)) + quitGap + KeycapWidth(ink, L"Échap", 400) + quitPadR;
    l.quit = {w - padRight - quitW, (height - quitH) / 2, w - padRight, (height + quitH) / 2};
    l.keycapX = l.quit.right - quitPadR - KeycapWidth(ink, L"Échap", 400);
    return l;
}
inline void PaintBanner(Ink& ink, float w, float h, const BannerVisual& v) {
    using namespace BannerSpec;
    auto* t = ink.Target(); const auto l = LayoutBanner(ink, w);
    ink.Fill({0, 0, w, h}, 0, Hex(Ink_::canvas));
    const auto end = Hex(gradientEnd);
    ID2D1GradientStopCollection* stops = nullptr; ID2D1LinearGradientBrush* gradient = nullptr;
    const D2D1_GRADIENT_STOP s[2] = {{0, {end.r, end.g, end.b, 0}}, {1, {end.r, end.g, end.b, gradientAlpha}}};
    if (SUCCEEDED(t->CreateGradientStopCollection(s, 2, &stops)) && SUCCEEDED(t->CreateLinearGradientBrush({{0, 0}, {0, h}}, stops, &gradient))) t->FillRectangle({0, 0, w, h}, gradient);
    Release(gradient); Release(stops);
    ink.Fill({0, h - 1, w, h}, 0, Hex(border));
    {   // Mark with the shader's ramp.
        const D2D1_RECT_F m{(l.logo.left + l.logo.right - markW) / 2, (l.logo.top + l.logo.bottom - markH) / 2, (l.logo.left + l.logo.right + markW) / 2, (l.logo.top + l.logo.bottom + markH) / 2};
        const D2D1_GRADIENT_STOP ramp[3] = {{0, D2D1::ColorF(0xD1DFFF)}, {.55f, D2D1::ColorF(0x3296FF)}, {1, D2D1::ColorF(0x2F63E7)}};
        if (SUCCEEDED(t->CreateGradientStopCollection(ramp, 3, &stops)) && SUCCEEDED(t->CreateLinearGradientBrush({{m.left, m.top}, {m.right, m.bottom}}, stops, &gradient))) ink.IconBrush(Glyphs::mark, m, gradient);
        Release(gradient); Release(stops);
    }
    ink.Text(L"Mode Annotation", F(14, 20), {l.label.left, 0, l.label.right + 40, h}, Hex(label), false, Align::Leading, true);
    {   // Quitter + Échap: hover plate, press 0.96.
        ScaleScope scale(t, l.quit, Motion::PressScale(v.quit.press, v.reduced));
        const float plate = .08f * v.quit.hover + .04f * v.quit.press;
        ink.Fill(l.quit, quitRadius, {1, 1, 1, plate});
        ink.Text(L"Quitter", F(13, 16, 500), {l.quit.left + quitPadL, l.quit.top, l.keycapX, l.quit.bottom}, Hex(0xFFFFFFE6), false, Align::Leading, true);
        Keycap(ink, L"Échap", l.keycapX, (l.quit.top + l.quit.bottom) / 2, Hex(0xFFFFFF14), Hex(0xFFFFFFB3), 400);
        if (v.focus) FocusRing(ink, l.quit, quitRadius, v.keyboard);
    }
}

// Toolbar « Annoter » for the browser's own chrome renderer (T3 calls it from its paint pass
// with the Chrome's target, factory and brush). box = the 24x24 button. No allocation per
// frame after the first (geometry cached per factory).
struct ToolbarVisual { bool active = false; float hover = 0, press = 0; bool focus = false, keyboard = false, reduced = false; };
inline void PaintToolbarButton(ID2D1RenderTarget* t, ID2D1Factory* factory, ID2D1SolidColorBrush* brush, D2D1_RECT_F box, const ToolbarVisual& v) {
    if (!t || !factory || !brush) return;
    static std::map<ID2D1Factory*, ID2D1PathGeometry*> cache;
    auto& g = cache[factory]; if (!g) g = DialogKit::SvgGeometry(factory, Glyphs::mark.d);
    D2D1_MATRIX_3X2_F base; t->GetTransform(&base);
    const float s = Motion::PressScale(v.press, v.reduced);
    if (s != 1) t->SetTransform(D2D1::Matrix3x2F::Scale(s, s, {(box.left + box.right) / 2, (box.top + box.bottom) / 2}) * base);
    const auto a = Design::Accent::Get();
    if (v.active) { brush->SetColor(D2D1::ColorF(a.r, a.g, a.b, .16f)); t->FillRoundedRectangle(D2D1::RoundedRect(box, 6, 6), brush); }
    else if (v.hover > 0 || v.press > 0) { brush->SetColor(D2D1::ColorF(1, 1, 1, (20 / 255.f) * std::max(v.hover, v.press))); t->FillRoundedRectangle(D2D1::RoundedRect(box, 6, 6), brush); }
    if (g) {
        const float w = 13, h = 12.25f, x = (box.left + box.right - w) / 2, y = (box.top + box.bottom - h) / 2;
        D2D1_MATRIX_3X2_F now; t->GetTransform(&now);
        t->SetTransform(D2D1::Matrix3x2F::Translation(-Glyphs::mark.x, -Glyphs::mark.y) * D2D1::Matrix3x2F::Scale(w / Glyphs::mark.w, h / Glyphs::mark.h) * D2D1::Matrix3x2F::Translation(x, y) * now);
        brush->SetColor(v.active ? D2D1::ColorF(a.r, a.g, a.b, 1) : D2D1::ColorF(1, 1, 1, .5f));
        t->FillGeometry(g, brush); t->SetTransform(now);
    }
    if (v.active) { brush->SetColor(D2D1::ColorF(a.r, a.g, a.b, 1)); t->FillRoundedRectangle(D2D1::RoundedRect({box.left + 7, box.bottom + 2, box.left + 17, box.bottom + 4}, 1, 1), brush); }
    if (v.focus) { brush->SetColor(D2D1::ColorF(1, 1, 1, Design::Focus::Opacity(v.keyboard))); t->DrawRoundedRectangle(D2D1::RoundedRect({box.left + .5f, box.top + .5f, box.right - .5f, box.bottom - .5f}, 5.5f, 5.5f), brush, 1); }
    t->SetTransform(base);
}
// UIA for the toolbar button (T3 adds it to its toolbar nodes).
inline Accessibility::Node ToolbarNode(long long key, long long parent, RECT bounds, bool active) {
    Accessibility::Node n; n.key = key; n.parent = parent; n.role = Accessibility::Role::Button; n.name = L"Annoter";
    n.accelerator = L"Ctrl+Maj+A"; n.toggle = active ? 2 : 1; n.automationId = L"AnnotateButton"; n.bounds = bounds; return n;
}

struct BannerHooks {
    std::function<void()> exit;                               // « Quitter » / Échap via UIA
    std::function<void(const std::wstring&)> announce;        // optional: the host's announcer
};
class Banner : public Animator::Client {
    static constexpr long long RootKey = 1, QuitKey = 2;
    HINSTANCE instance_ = nullptr; HWND parent_ = nullptr; std::wstring root_; BannerHooks hooks_;
    Layer layer_; Presence presence_; Motion::Interaction quit_;
    RECT rect_{}; bool hover_ = false, pressed_ = false, focus_ = false, keyboard_ = false, reduced_ = false;
    float Width() const { return (rect_.right - rect_.left) / layer_.Scale(); }
    D2D1_RECT_F QuitBox() { auto* ink = Ink::Instance(); return ink ? LayoutBanner(*ink, Width()).quit : D2D1_RECT_F{}; }
    void Render(Motion::Time now) {
        if (!layer_.Window()) return;
        const float opacity = presence_.Progress(now);
        BannerVisual v; v.quit = quit_.Visual(now); v.focus = focus_; v.keyboard = keyboard_; v.reduced = reduced_;
        layer_.paint = [this, v, opacity](Ink& ink, float w, float h) {
            ID2D1Layer* group = nullptr; const bool pushed = ink.PushOpacity(opacity, group);
            PaintBanner(ink, w, h, v); if (pushed) ink.PopOpacity(group);
        };
        layer_.Present(rect_, true);
    }
    void SetInteraction(bool hover, bool pressed) {
        hover_ = hover; pressed_ = pressed; const auto now = Motion::Now();
        quit_.Set(hover_, pressed_ && hover_, now, reduced_); Render(now); Animator::Get().Request(this);
    }
    bool Handle(UINT m, WPARAM w, LPARAM l, LRESULT& r) {
        const float s = 1 / layer_.Scale();
        switch (m) {
        case WM_MOUSEMOVE: { TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, layer_.Window(), 0}; TrackMouseEvent(&t);
            const bool in = Inside(QuitBox(), GET_X_LPARAM(l) * s, GET_Y_LPARAM(l) * s); if (in != hover_) SetInteraction(in, pressed_); r = 0; return true; }
        case WM_MOUSELEAVE: SetInteraction(false, false); r = 0; return true;
        case WM_LBUTTONDOWN: if (Inside(QuitBox(), GET_X_LPARAM(l) * s, GET_Y_LPARAM(l) * s)) { SetCapture(layer_.Window()); keyboard_ = false; SetInteraction(true, true); } r = 0; return true;
        case WM_LBUTTONUP: { const bool was = pressed_; if (GetCapture() == layer_.Window()) ReleaseCapture();
            const bool in = Inside(QuitBox(), GET_X_LPARAM(l) * s, GET_Y_LPARAM(l) * s); SetInteraction(in, false); if (was && in && hooks_.exit) hooks_.exit(); r = 0; return true; }
        case WM_SETFOCUS: focus_ = true; Render(Motion::Now()); return false;
        case WM_KILLFOCUS: focus_ = false; Render(Motion::Now()); return false;
        case WM_KEYDOWN: if (w == VK_RETURN || w == VK_SPACE || w == VK_ESCAPE) { keyboard_ = true; if (hooks_.exit) hooks_.exit(); r = 0; return true; } return false;
        case WM_APP + 1: if (hooks_.exit) hooks_.exit(); r = 0; return true;
        }
        return false;
    }
    std::vector<Accessibility::Node> Nodes() {
        std::vector<Accessibility::Node> n; if (!presence_.IsOpen()) return n;
        Accessibility::Node g; g.key = RootKey; g.role = Accessibility::Role::Group; g.name = L"Mode Annotation"; g.status = L"Activé";
        g.help = L"Cliquez un élément de la page pour l'annoter."; g.automationId = L"AnnotationBanner"; g.focusable = false;
        GetWindowRect(layer_.Window(), &g.bounds); n.push_back(g);
        Accessibility::Node q; q.key = QuitKey; q.parent = RootKey; q.name = L"Quitter le mode Annotation"; q.accelerator = L"Échap";
        q.automationId = L"AnnotationQuit"; q.bounds = layer_.Screen(QuitBox()); n.push_back(q);
        return n;
    }
public:
    ~Banner() { Animator::Exists() ? Animator::Get().Forget(this) : void(); }
    // Stores the context only: no window, no thread, no allocation of D2D resources.
    void Init(HINSTANCE instance, HWND parent, std::wstring root, BannerHooks hooks) { instance_ = instance; parent_ = parent; root_ = std::move(root); hooks_ = std::move(hooks); }
    bool Created() const { return layer_.Window() != nullptr; }
    HWND Window() const { return layer_.Window(); }
    bool Visible() const { return presence_.IsOpen(); }
    // rect: the band in the parent's client pixels (full width, 32 DIP high, under the toolbar).
    bool Show(RECT rect, bool reduced) {
        if (!Ink::Shared(root_)) return false;
        if (!layer_.Window()) {
            Layer::Options o; o.child = true; o.title = L"Mode Annotation";
            if (!layer_.Create(instance_, parent_, o)) return false;
            layer_.handle = [this](UINT m, WPARAM w, LPARAM l, LRESULT& r) { return Handle(m, w, l, r); };
            Accessibility::Host host; host.window = layer_.Window();
            host.nodes = [this] { return Nodes(); };
            host.focused = [this]() -> long long { return focus_ ? QuitKey : 0; };
            host.invoke = [this](long long key) { if (key == QuitKey) PostMessageW(layer_.Window(), WM_APP + 1, 0, 0); };
            layer_.uia.Attach(std::move(host));
        }
        rect_ = rect; reduced_ = reduced; quit_.EnablePolish(); const auto now = Motion::Now();
        const bool was = presence_.IsOpen();
        presence_.Show(now); Render(now); Animator::Get().Request(this);
        if (!was) {
            if (hooks_.announce) hooks_.announce(L"Mode Annotation activé");
            else layer_.uia.Notify(RootKey, L"Mode Annotation activé", L"AnnotationModeOn");
        }
        return true;
    }
    void Move(RECT rect) { rect_ = rect; if (presence_.Visible(Motion::Now())) Render(Motion::Now()); }
    void Hide(bool animate = true) {
        if (!layer_.Window()) return; const auto now = Motion::Now();
        presence_.Hide(now, animate); hover_ = pressed_ = focus_ = false; quit_.Settle();
        if (!presence_.Visible(now)) layer_.Hide(); else Animator::Get().Request(this);
    }
    bool Frame(Motion::Time now) override {
        if (!presence_.Visible(now)) { layer_.Hide(); return false; }
        Render(now);
        return presence_.Moving(now) || quit_.NextWake(now).delay.has_value();
    }
    // Tests.
    Layer& LayerForTest() { return layer_; }
    std::vector<Accessibility::Node> NodesForTest() { return Nodes(); }
};
}} // namespace NativeChrome::Annotations
