#pragma once
// « Mode Annotation » band under the toolbar + the toolbar « Annoter » button state.
// Paper (file 01M6Q7YT14ZKMGET54ASPNGQEC, p-H-0, « Bandeau — Mode Annotation », 1.1 / 1.8):
//   height 32; background linear-gradient(180deg, #3E67FF00 -> #233FD6 at 32 %) over the
//   canvas #171717; bottom border 1 #3E67FF3D; padding 0 8 0 12; gap 12.
//   Left: the mark (Paper renders it through a « Heatmap » shader 31x29: natively the same
//   glyph, 18x17, filled with the shader's #C0DAFF -> #2F63E7 ramp) + 6 + « Mode Annotation »
//   Inter Display 14/20 #5B84FF + 6 + « BETA » chip (h 18, radius 3, accent 16 %, padding 5,
//   12/20 medium #5B84FF -> #6A8FFF for 4.5:1, see BannerSpec::betaLabel).
//   Right: [1.8 reminder: h 22, radius 6, #FFFFFF14, padding 8, gap 6: « Aucun agent
//   connecté » 12/16 #FFFFFFCC + « Connecter » 12/16 medium white, underline 1 px offset 3]
//   + 12 + « Quitter » button h 24, radius 3, padding 8/4, gap 8: 13/16 medium #FFFFFFE6 and
//   key cap « Échap » (h 18, radius 3, padding 5, #FFFFFF14, 11/14 regular #FFFFFFB3).
// Toolbar « Annoter » (Paper W09-0, active): 24x24 radius 6, split tint (accent 16 %), mark
// 13x12.25 accent, an accent bar 10x2 radius 1 at x+7, 2 DIP under the button.
// The band is a child layered window of the browser window (above the WebView), created
// at the first Show; it fades in/out in 120 ms (opacity only: the page moves by 32 DIP at once).
#include "annotation-kit.h"

namespace NativeChrome { namespace Annotations {
namespace BannerSpec {
inline constexpr float height = 32, padLeft = 12, padRight = 8, logoW = 31, logoH = 29, logoGap = 6, gap = 12;
inline constexpr float markW = 18, markH = 17;          // Paper WO9-0 mark size inside the 31x29 slot
inline constexpr float quitH = 24, quitPadL = 8, quitPadR = 4, quitGap = 8, quitRadius = 3;
inline constexpr float remindH = 22, remindPad = 8, remindGap = 6, remindRadius = 6;   // Paper 1.8 (10SY-0)
inline constexpr std::uint32_t label = 0x5B84FFFF, border = 0x3E67FF3D, gradientEnd = 0x233FD6FF;
// « BETA » on its accent-16 % chip: Paper #5B84FF measures 4.14:1 there (12 px needs 4.5:1), so it is
// lifted to #6A8FFF (4.66:1); « Mode Annotation » keeps #5B84FF on the band (4.87:1).
inline constexpr std::uint32_t betaLabel = 0x6A8FFFFF;
inline constexpr float gradientAlpha = .32f;
}
enum BannerHit { BannerNone = 0, BannerRemind = 1, BannerQuit = 2 };
struct BannerVisual {
    Motion::InteractionVisual quit, remind; bool focus = false, keyboard = false, reduced = false;
    int focusItem = BannerQuit; bool beta = true; float reminder = 0;   // reminder: 0 hidden .. 1 shown (fade)
};
struct BannerLayout { D2D1_RECT_F logo{}, label{}, beta{}, quit{}, remind{}; float keycapX = 0; };
inline BannerLayout LayoutBanner(Ink& ink, float w, bool beta = true) {
    using namespace BannerSpec; BannerLayout l;
    l.logo = {padLeft, (height - logoH) / 2, padLeft + logoW, (height + logoH) / 2};
    l.label = {l.logo.right + logoGap, 6, l.logo.right + logoGap + ink.Width(L"Mode Annotation", F(14, 20)), 26};
    if (beta) { const float bw = ink.Width(L"BETA", F(12, 20, 500)) + 10; l.beta = {l.label.right + logoGap, (height - 18) / 2, l.label.right + logoGap + bw, (height + 18) / 2}; }
    const float quitW = quitPadL + ink.Width(L"Quitter", F(13, 16, 500)) + quitGap + KeycapWidth(ink, L"Échap", 400) + quitPadR;
    l.quit = {w - padRight - quitW, (height - quitH) / 2, w - padRight, (height + quitH) / 2};
    l.keycapX = l.quit.right - quitPadR - KeycapWidth(ink, L"Échap", 400);
    const float rw = remindPad + ink.Width(L"Aucun agent connecté", F(12, 16)) + remindGap + ink.Width(L"Connecter", F(12, 16, 500)) + remindPad;
    l.remind = {l.quit.left - gap - rw, (height - remindH) / 2, l.quit.left - gap, (height + remindH) / 2};
    return l;
}
inline int BannerHitAt(const BannerLayout& l, bool reminder, float x, float y) {
    if (Inside(l.quit, x, y)) return BannerQuit;
    if (reminder && Inside(l.remind, x, y)) return BannerRemind;
    return BannerNone;
}
inline void PaintBanner(Ink& ink, float w, float h, const BannerVisual& v) {
    using namespace BannerSpec;
    auto* t = ink.Target(); const auto l = LayoutBanner(ink, w, v.beta);
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
    if (v.beta) { ink.Fill(l.beta, 3, Accent(.16f)); ink.Text(L"BETA", F(12, 20, 500), l.beta, Hex(betaLabel), false, Align::Center, true); }
    if (v.reminder > 0) {   // « Aucun agent connecté · Connecter » (1.8): fades in, press 0.96
        ID2D1Layer* group = nullptr; const bool pushed = ink.PushOpacity(v.reminder, group);
        {
            ScaleScope scale(t, l.remind, Motion::PressScale(v.remind.press, v.reduced));
            ink.Fill(l.remind, remindRadius, {1, 1, 1, (20 / 255.f) + .04f * v.remind.hover + .03f * v.remind.press});
            Piece connect = PText(L"Connecter", F(12, 16, 500), {1, 1, 1, 1}); connect.underline = true;
            Pieces(ink, {PText(L"Aucun agent connecté", F(12, 16), Hex(0xFFFFFFCC), remindGap), connect}, l.remind);
            if (v.focus && v.focusItem == BannerRemind) FocusRing(ink, l.remind, remindRadius, v.keyboard);
        }
        if (pushed) ink.PopOpacity(group);
    }
    {   // Quitter + Échap: hover plate, press 0.96.
        ScaleScope scale(t, l.quit, Motion::PressScale(v.quit.press, v.reduced));
        const float plate = .08f * v.quit.hover + .04f * v.quit.press;
        ink.Fill(l.quit, quitRadius, {1, 1, 1, plate});
        ink.Text(L"Quitter", F(13, 16, 500), {l.quit.left + quitPadL, l.quit.top, l.keycapX, l.quit.bottom}, Hex(0xFFFFFFE6), false, Align::Leading, true);
        Keycap(ink, L"Échap", l.keycapX, (l.quit.top + l.quit.bottom) / 2, Hex(0xFFFFFF14), Hex(0xFFFFFFB3), 400);
        if (v.focus && v.focusItem == BannerQuit) FocusRing(ink, l.quit, quitRadius, v.keyboard);
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
    std::function<void()> connect;                            // V2: « Aucun agent connecté · Connecter » (1.8)
    std::function<void(const std::wstring&)> announce;        // optional: the host's announcer
};
inline Timing BannerTiming() { Timing t; t.enter = t.exit = t.reducedEnter = t.reducedExit = Spec::bannerFade; t.from = t.to = 1; return t; }
class Banner : public Animator::Client {
    static constexpr long long RootKey = 1, QuitKey = 2, RemindKey = 3;
    HINSTANCE instance_ = nullptr; HWND parent_ = nullptr; std::wstring root_; BannerHooks hooks_;
    Layer layer_; Presence presence_{BannerTiming()};
    Motion::State motion_; Ramp reminder_; bool reminderOn_ = false, beta_ = true;
    RECT rect_{}; int hover_ = 0, pressed_ = 0, focusItem_ = BannerQuit; bool focus_ = false, keyboard_ = false, reduced_ = false;
    float Width() const { return (rect_.right - rect_.left) / layer_.Scale(); }
    BannerLayout Layout() { auto* ink = Ink::Instance(); return ink ? LayoutBanner(*ink, Width(), beta_) : BannerLayout{}; }
    void Render(Motion::Time now) {
        if (!layer_.Window()) return;
        const float opacity = presence_.Progress(now);
        BannerVisual v; v.quit = motion_.Visual(BannerQuit, now); v.remind = motion_.Visual(BannerRemind, now); v.focus = focus_; v.keyboard = keyboard_;
        v.reduced = reduced_; v.focusItem = focusItem_; v.beta = beta_; v.reminder = Motion::Clamp(reminder_.Value(now));
        layer_.paint = [v, opacity](Ink& ink, float w, float h) {
            ID2D1Layer* group = nullptr; const bool pushed = ink.PushOpacity(opacity, group);
            PaintBanner(ink, w, h, v); if (pushed) ink.PopOpacity(group);
        };
        layer_.Present(rect_, true);
    }
    void Interact() { const auto now = Motion::Now(); motion_.SetInteraction(hover_, pressed_ == hover_ ? pressed_ : 0, now); Render(now); Animator::Get().Request(this); }
    void Activate(int item) {
        if (item == BannerQuit) { if (hooks_.exit) hooks_.exit(); }
        else if (item == BannerRemind && reminderOn_ && hooks_.connect) hooks_.connect();
    }
    bool Handle(UINT m, WPARAM w, LPARAM l, LRESULT& r) {
        const float s = 1 / layer_.Scale(); const float x = GET_X_LPARAM(l) * s, y = GET_Y_LPARAM(l) * s;
        switch (m) {
        case WM_MOUSEMOVE: { TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, layer_.Window(), 0}; TrackMouseEvent(&t);
            const int hit = BannerHitAt(Layout(), reminderOn_, x, y); if (hit != hover_) { hover_ = hit; Interact(); } r = 0; return true; }
        case WM_MOUSELEAVE: hover_ = 0; Interact(); r = 0; return true;
        case WM_LBUTTONDOWN: pressed_ = BannerHitAt(Layout(), reminderOn_, x, y); if (pressed_) { SetCapture(layer_.Window()); keyboard_ = false; } Interact(); r = 0; return true;
        case WM_LBUTTONUP: { const int was = pressed_, hit = BannerHitAt(Layout(), reminderOn_, x, y); pressed_ = 0; if (GetCapture() == layer_.Window()) ReleaseCapture();
            hover_ = hit; Interact(); if (was && was == hit) Activate(hit); r = 0; return true; }
        case WM_SETFOCUS: focus_ = true; Render(Motion::Now()); return false;
        case WM_KILLFOCUS: focus_ = false; Render(Motion::Now()); return false;
        case WM_GETDLGCODE: r = DLGC_WANTALLKEYS; return true;
        case WM_KEYDOWN:
            keyboard_ = true;
            if (w == VK_TAB && reminderOn_) {
                focusItem_ = focusItem_ == BannerQuit ? BannerRemind : BannerQuit; Render(Motion::Now());
                if (layer_.uia.Listening()) layer_.uia.Raise(focusItem_ == BannerQuit ? QuitKey : RemindKey, UIA_AutomationFocusChangedEventId);
                r = 0; return true;
            }
            if (w == VK_RETURN || w == VK_SPACE) { Activate(focusItem_); r = 0; return true; }
            if (w == VK_ESCAPE) { if (hooks_.exit) hooks_.exit(); r = 0; return true; }
            return false;
        case WM_APP + 1: Activate(int(w)); r = 0; return true;
        }
        return false;
    }
    std::vector<Accessibility::Node> Nodes() {
        std::vector<Accessibility::Node> n; if (!presence_.IsOpen()) return n;
        Accessibility::Node g; g.key = RootKey; g.role = Accessibility::Role::Group; g.name = L"Mode Annotation"; g.status = reminderOn_ ? L"Activé · aucun agent connecté" : L"Activé";
        g.help = L"Cliquez un élément de la page pour l'annoter."; g.automationId = L"AnnotationBanner"; g.focusable = false;
        GetWindowRect(layer_.Window(), &g.bounds); n.push_back(g);
        const auto l = Layout();
        if (reminderOn_) {
            Accessibility::Node c; c.key = RemindKey; c.parent = RootKey; c.name = L"Connecter un agent"; c.help = L"Aucun agent connecté";
            c.automationId = L"AnnotationConnectReminder"; c.bounds = layer_.Screen(l.remind); n.push_back(c);
        }
        Accessibility::Node q; q.key = QuitKey; q.parent = RootKey; q.name = L"Quitter le mode Annotation"; q.accelerator = L"Échap";
        q.automationId = L"AnnotationQuit"; q.bounds = layer_.Screen(l.quit); n.push_back(q);
        return n;
    }
public:
    ~Banner() { if (Animator::Exists()) Animator::Get().Forget(this); }
    // Stores the context only: no window, no thread, no allocation of D2D resources.
    void Init(HINSTANCE instance, HWND parent, std::wstring root, BannerHooks hooks) { instance_ = instance; parent_ = parent; root_ = std::move(root); hooks_ = std::move(hooks); }
    bool Created() const { return layer_.Window() != nullptr; }
    HWND Window() const { return layer_.Window(); }
    bool Visible() const { return presence_.IsOpen(); }
    bool ReminderVisible() const { return reminderOn_; }
    // rect: the band in the parent's client pixels (full width, 32 DIP high, under the toolbar).
    bool Show(RECT rect, bool reduced) {
        if (!Ink::Shared(root_)) return false;
        if (!layer_.Window()) {
            Layer::Options o; o.child = true; o.title = L"Mode Annotation"; o.tabStop = true;
            if (!layer_.Create(instance_, parent_, o)) return false;
            layer_.handle = [this](UINT m, WPARAM w, LPARAM l, LRESULT& r) { return Handle(m, w, l, r); };
            Accessibility::Host host; host.window = layer_.Window();
            host.nodes = [this] { return Nodes(); };
            host.focused = [this]() -> long long { return focus_ ? (focusItem_ == BannerQuit ? QuitKey : RemindKey) : 0; };
            host.invoke = [this](long long key) { PostMessageW(layer_.Window(), WM_APP + 1, key == QuitKey ? BannerQuit : BannerRemind, 0); };
            layer_.uia.Attach(std::move(host));
            motion_.EnablePolish();
        }
        rect_ = rect; reduced_ = reduced; motion_.SetReducedMotion(reduced); const auto now = Motion::Now();
        const bool was = presence_.IsOpen();
        presence_.Show(now, true, reduced); Render(now); Animator::Get().Request(this);
        if (!was) {
            if (hooks_.announce) hooks_.announce(L"Mode Annotation activé");
            else layer_.uia.Notify(RootKey, L"Mode Annotation activé", L"AnnotationModeOn");
        }
        return true;
    }
    void Move(RECT rect) { rect_ = rect; if (presence_.Visible(Motion::Now())) Render(Motion::Now()); }
    // V2 (1.8): « Aucun agent connecté · Connecter » after « Plus tard »; fades in/out in 120 ms.
    void SetReminder(bool on, bool animate = true) {
        if (on == reminderOn_) return;
        reminderOn_ = on; const auto now = Motion::Now();
        reminder_.Set(on ? 1.f : 0.f, now, animate ? Spec::bannerFade : 0, Curves::out);
        if (!on && focusItem_ == BannerRemind) focusItem_ = BannerQuit;
        if (layer_.Window()) { Render(now); Animator::Get().Request(this); if (layer_.uia.Listening()) layer_.uia.ChildrenChanged(RootKey); }
    }
    void SetBeta(bool beta) { beta_ = beta; if (layer_.Window() && presence_.Visible(Motion::Now())) Render(Motion::Now()); }
    void Hide(bool animate = true) {
        if (!layer_.Window()) return;
        const auto now = Motion::Now();
        presence_.Hide(now, animate, reduced_); hover_ = pressed_ = 0; focus_ = false; motion_.ClearInteractions();
        if (!presence_.Visible(now)) layer_.Hide(); else Animator::Get().Request(this);
    }
    bool Frame(Motion::Time now) override {
        if (!presence_.Visible(now)) { layer_.Hide(); return false; }
        Render(now);
        return presence_.Moving(now) || reminder_.Moving(now) || motion_.NextWake(now).delay.has_value();
    }
    // Tests.
    Layer& LayerForTest() { return layer_; }
    std::vector<Accessibility::Node> NodesForTest() { return Nodes(); }
    bool KeyForTest(WPARAM key) { LRESULT r; return Handle(WM_KEYDOWN, key, 0, r); }
    int FocusItemForTest() const { return focusItem_; }
};
}} // namespace NativeChrome::Annotations
