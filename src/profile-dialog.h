#pragma once
#include "i18n.h"
// « Nouveau profil » (Paper p-I-0 P03, Modale A1 400 wide, radius 16): title, one line of body,
// the editor block (profile-editor.h), « Annuler » / « Créer le profil » (the draft profile's accent fill, live).
// Modal: Run() disables the owner and runs its own loop until the dialog ends; the duplicate check
// happens at submit (« Doublon · vérifié à l'envoi, bouton actif »). `submit` does the real work
// (Profiles::Create + open its window) and returns an error to show, or "" to close.
//
// Motion (P05): 150 ms opacity (+4 DIP rise) in, 120 ms out; reduced motion: opacity only.
// Keyboard: focus starts in Nom; Tab / Maj+Tab cycle Nom → Icône → Teinte → Intensité → Hex →
// Annuler → Créer; Entrée in Nom or on Créer submits; Échap cancels.
#include <windows.h>
#include <windowsx.h>
#include <functional>
#include <string>
#include "chrome.h"
#include "native-ui.h"
#include "accessibility.h"
#include "tooltip.h"
#include "profile-editor.h"

namespace NativeChrome {
namespace ProfileDialog {

namespace Layout {
inline constexpr float width = 400, pad = 16, gap = 16, radius = 16, title = 20, bodyLine = 20, buttonH = 32, buttonPad = 12, buttonMin = 64, actionsTop = 8, buttonGap = 8;
}
struct Spec {
    std::wstring title = TR(profile_new_name);
    std::wstring body = TR(profile_dialog_body);
    std::wstring accept = TR(profile_dialog_create);
    Profiles::Profile seed;                                         // default name/icon/colour
    D2D1_COLOR_F accent{0x3E / 255.f, 0x67 / 255.f, 1, 1};           // the opener's accent (not used for the CTA: it previews the draft)
    std::function<Profiles::NameError(const std::wstring&)> validateName;
    // Returns "" on success (the dialog closes), else an error line; NameError sends it to the field.
    std::function<std::wstring(const Profiles::Profile&, Profiles::NameError&)> submit;
    bool keyboard = false, reduced = false;
};

class Dialog {
    enum Button { None = 0, BtnCancel = 10, BtnAccept = 11 };
    HINSTANCE instance_ = nullptr; HWND owner_ = nullptr, hwnd_ = nullptr; std::wstring root_;
    Chrome chrome_; Accessibility::Bridge uia_; ProfileEditor::Editor editor_;
    Spec spec_; float height_ = 0, bodyH_ = 0; UINT dpi_ = 96; bool done_ = false, accepted_ = false, keyboard_ = false;
    int focus_ = None, hover_ = None, pressed_ = None; std::wstring error_;
    Motion::Time opened_{}; bool entering_ = false, leaving_ = false; Motion::Time left_{};
    static constexpr UINT_PTR FrameTimer = 1;
    float Scale() const { return dpi_ / 96.f; }
    int Px(float dip) const { return static_cast<int>(std::lround(dip * Scale())); }
    float EditorTop() const { return Layout::pad + Layout::title + 4 + bodyH_ + Layout::gap; }
    float ErrorH() const { return error_.empty() ? 0.f : 20.f + Layout::gap; }
    D2D1_RECT_F ButtonBox(int b) const {
        const float y = height_ - Layout::pad - Layout::buttonH; const float right = Layout::width - Layout::pad;
        const float acceptW = std::max(Layout::buttonMin, const_cast<Chrome&>(chrome_).TextWidth(spec_.accept, 14, true) + 2 * Layout::buttonPad);
        const float cancelW = std::max(Layout::buttonMin, const_cast<Chrome&>(chrome_).TextWidth(TR(common_cancel), 14, true) + 2 * Layout::buttonPad);
        if (b == BtnAccept) return {right - acceptW, y, right, y + Layout::buttonH};
        return {right - acceptW - Layout::buttonGap - cancelW, y, right - acceptW - Layout::buttonGap, y + Layout::buttonH};
    }
    void Measure() {
        bodyH_ = PopupKit::Measure(chrome_, spec_.body, Layout::width - 2 * Layout::pad, PopupKit::Type{14, 20}, true).height;
        editor_.SetWidth(Layout::width - 2 * Layout::pad);
        height_ = EditorTop() + editor_.Height() + Layout::gap + ErrorH() + Layout::actionsTop + Layout::buttonH + Layout::pad;
    }
    void Invalidate() { if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE); }
    void PlaceEditor() { editor_.Place({Layout::pad, EditorTop()}, Scale(), !leaving_); }
    void Resize() {
        RECT o{}; GetWindowRect(owner_, &o); const int w = Px(Layout::width), h = Px(height_);
        const int x = o.left + (o.right - o.left - w) / 2, y = o.top + std::max<int>(Px(80), (o.bottom - o.top - h) / 3);
        restX_ = x; restY_ = y;
        SetWindowPos(hwnd_, HWND_TOP, x, y, w, h, SWP_NOACTIVATE); NativeUI::ConfigureShadowlessPopup(hwnd_, Layout::radius); chrome_.Resize(); PlaceEditor();
    }
    // P05 « Modales »: opacity + 4 DIP rise in 150 ms (ease-out), opacity out 120 ms; reduced: opacity only.
    // No scrim: Paper P03/P04 draw none (the #0E0E0E behind them is the specimen panel).
    int restX_ = 0, restY_ = 0;
    void Present(float a, float riseU) {
        SetLayeredWindowAttributes(hwnd_, 0, static_cast<BYTE>(std::lround(255 * a)), LWA_ALPHA);
        const float e = 1 - (1 - riseU) * (1 - riseU) * (1 - riseU); const int rise = spec_.reduced ? 0 : Px(4 * (1 - e));
        SetWindowPos(hwnd_, nullptr, restX_, restY_ + rise, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void Submit() {
        if (!editor_.CommitName(true)) { editor_.SetFocusStop(ProfileEditor::Name, keyboard_); return; }
        Profiles::NameError ne = Profiles::NameError::None;
        const std::wstring err = spec_.submit ? spec_.submit(editor_.Value(), ne) : L"";
        if (ne != Profiles::NameError::None) { editor_.ShowNameError(ne); editor_.SetFocusStop(ProfileEditor::Name, keyboard_); return; }
        if (!err.empty()) { error_ = err; Measure(); Resize(); Invalidate(); return; }
        accepted_ = true; Close();
    }
    void Close() { if (leaving_) return; leaving_ = true; left_ = Motion::Now(); editor_.Hide(); SetTimer(hwnd_, FrameTimer, 10, nullptr); }
    void Activate(int b) { if (b == BtnCancel) { accepted_ = false; Close(); } else if (b == BtnAccept) Submit(); }
    void Focus(int b, bool keyboard) { keyboard_ = keyboard; if (b != None) editor_.Blur(); focus_ = b; if (b != None && hwnd_) SetFocus(hwnd_); Invalidate(); }
    void Paint() {
        if (!chrome_.RenderTarget()) chrome_.Resize();
        auto* t = chrome_.RenderTarget(); if (!t) return; const float k = Design::Surface::canvas;
        t->BeginDraw(); t->Clear(D2D1::ColorF(k, k, k, 1));
        PopupKit::DrawText(chrome_, spec_.title, {Layout::pad, Layout::pad, Layout::width - Layout::pad, Layout::pad + 20}, PopupKit::Type{16, 20, -.16f, true}, PopupKit::White(1));
        PopupKit::DrawText(chrome_, spec_.body, {Layout::pad, Layout::pad + 24, Layout::width - Layout::pad, Layout::pad + 24 + bodyH_}, PopupKit::Type{14, 20}, PopupKit::White(.7f), true);
        editor_.Paint(chrome_, {Layout::pad, EditorTop()});
        if (!error_.empty()) { const float y = EditorTop() + editor_.Height() + Layout::gap; PopupKit::DrawText(chrome_, error_, {Layout::pad, y, Layout::width - Layout::pad, y + 20}, PopupKit::Type{14, 20}, D2D1::ColorF(Design::Danger::r, Design::Danger::g, Design::Danger::b, 1), true); }
        for (int b : {BtnCancel, BtnAccept}) {
            const auto box = ButtonBox(b); const bool accept = b == BtnAccept; const float lift = (hover_ == b ? .06f : 0) + (pressed_ == b ? .04f : 0);
            // User correction (2026-11-14): « Créer le profil » previews the DRAFT profile's accent fill
            // (solved for white text ≥ 4.6:1), live with every hue / intensity / hex edit; a neutral draft
            // takes the reference blue. The opener window's accent is never touched.
            // Hover / press deepen the draft fill (Design::Accent::StatesOf): its hue, white text >= 4.5:1 in every state.
            if (accept) { const auto a = DraftAccent(); const auto c = Design::Accent::StateColor({a.r, a.g, a.b}, hover_ == b ? 1.f : 0.f, pressed_ == b ? 1.f : 0.f); PopupKit::Fill(chrome_, box, 8, D2D1::ColorF(c.r, c.g, c.b, 1)); }
            else { const float r = Design::Surface::raised + lift; PopupKit::Fill(chrome_, box, 8, D2D1::ColorF(r, r, r, 1)); }
            if (focus_ == b && keyboard_) { auto* br = chrome_.SolidBrush(); br->SetColor(PopupKit::White(.5f)); t->DrawRoundedRectangle(D2D1::RoundedRect({box.left + .5f, box.top + .5f, box.right - .5f, box.bottom - .5f}, 7.5f, 7.5f), br, 1); }   // keyboard only
            PopupKit::DrawText(chrome_, accept ? spec_.accept : std::wstring(TR(common_cancel)), {box.left, box.top + 7, box.right, box.bottom}, PopupKit::Type{14, 18, -.14f, true}, PopupKit::White(1), false, DWRITE_TEXT_ALIGNMENT_CENTER);
        }
        PopupKit::Outline(chrome_, Layout::width, height_, Layout::radius, PopupKit::White(0x14 / 255.f));
        if (t->EndDraw() == D2DERR_RECREATE_TARGET) chrome_.RecreateTarget();
    }
    D2D1_COLOR_F DraftAccent() const {
        const auto& v = editor_.Value();
        if (v.neutral) { const unsigned long r = Design::Accent::defaultRgb; return D2D1::ColorF(((r >> 16) & 255) / 255.f, ((r >> 8) & 255) / 255.f, (r & 255) / 255.f, 1); }
        return ProfileEditor::Color(ProfileColor::AccentFill(v.hue, v.intensity));
    }
public:
    D2D1_COLOR_F DraftAccentForTest() const { return DraftAccent(); }
    void SetDraftForTest(const Profiles::Profile& p) { editor_.SetValue(p); }
private:
    float Alpha(Motion::Time now) {
        const auto ms = [&](Motion::Time a) { return static_cast<float>(std::chrono::duration<double, std::milli>(now - a).count()); };
        if (leaving_) return 1 - Motion::Clamp(ms(left_) / 120.f);
        if (entering_) { const float u = Motion::Clamp(ms(opened_) / 150.f); if (u >= 1) entering_ = false; return u; }
        return 1;
    }
    void Step() {
        const auto now = Motion::Now(); const float a = Alpha(now);
        Present(a, entering_ || leaving_ ? (leaving_ ? 1.f : Motion::Clamp(static_cast<float>(std::chrono::duration<double, std::milli>(now - opened_).count()) / 150.f)) : 1.f);
        if (leaving_ && a <= 0) { KillTimer(hwnd_, FrameTimer); done_ = true; PostMessageW(owner_, WM_NULL, 0, 0); }
        else if (!entering_ && !leaving_) KillTimer(hwnd_, FrameTimer);
    }
    int Hit(float x, float y) const { for (int b : {BtnCancel, BtnAccept}) { const auto r = ButtonBox(b); if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return b; } return None; }
    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<Dialog*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (msg == WM_NCCREATE) { self = static_cast<Dialog*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); }
        return self ? self->Handle(h, msg, w, l) : DefWindowProcW(h, msg, w, l);
    }
    LRESULT Handle(HWND h, UINT msg, WPARAM w, LPARAM l) {
        const float s = Scale();
        switch (msg) {
        case WM_GETOBJECT: { LRESULT r; if (uia_.OnGetObject(h, w, l, r)) return r; break; }
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); Paint(); EndPaint(h, &ps); return 0; }
        case WM_CTLCOLOREDIT: { LRESULT r; if (editor_.CtlColor(reinterpret_cast<HWND>(l), reinterpret_cast<HDC>(w), r)) return r; break; }
        case WM_TIMER: if (w == FrameTimer) Step(); return 0;
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT tr{sizeof(tr), TME_LEAVE, h, 0}; TrackMouseEvent(&tr);
            const float x = GET_X_LPARAM(l) / s, y = GET_Y_LPARAM(l) / s;
            editor_.PointerMove(x - Layout::pad, y - EditorTop());
            const int b = Hit(x, y); if (b != hover_) { hover_ = b; Invalidate(); } return 0;
        }
        case WM_MOUSELEAVE: editor_.PointerLeave(); if (hover_) { hover_ = None; Invalidate(); } return 0;
        case WM_LBUTTONDOWN: {
            const float x = GET_X_LPARAM(l) / s, y = GET_Y_LPARAM(l) / s; SetCapture(h);
            if (editor_.PointerDown(x - Layout::pad, y - EditorTop())) { focus_ = None; Invalidate(); return 0; }
            pressed_ = Hit(x, y); if (pressed_) Focus(pressed_, false); Invalidate(); return 0;
        }
        case WM_LBUTTONUP: {
            ReleaseCapture(); const float x = GET_X_LPARAM(l) / s, y = GET_Y_LPARAM(l) / s;
            if (editor_.Dragging()) { editor_.PointerUp(); return 0; }
            const int b = Hit(x, y), was = pressed_; pressed_ = None; Invalidate(); if (b && b == was) Activate(b); return 0;
        }
        case WM_KEYDOWN: {
            keyboard_ = true; const bool shift = GetKeyState(VK_SHIFT) < 0;
            if (w == VK_ESCAPE) { Activate(BtnCancel); return 0; }
            if (w == VK_TAB) {
                if (focus_ == None && editor_.Focus() != ProfileEditor::None) { editor_.Tab(shift); return 0; }
                if (focus_ == BtnCancel) { if (shift) { focus_ = None; editor_.SetFocusStop(ProfileEditor::Hex, true); } else Focus(BtnAccept, true); return 0; }
                if (focus_ == BtnAccept) { if (shift) Focus(BtnCancel, true); else { focus_ = None; editor_.SetFocusStop(ProfileEditor::Name, true); } return 0; }
                editor_.SetFocusStop(ProfileEditor::Name, true); return 0;
            }
            if ((w == VK_RETURN || w == VK_SPACE) && focus_ != None) { Activate(focus_); return 0; }
            if (w == VK_RETURN && focus_ == None) { Submit(); return 0; }
            if (focus_ == None && editor_.Key(w)) return 0;
            return 0;
        }
        case WM_ACTIVATE: return 0;
        case WM_CLOSE: Activate(BtnCancel); return 0;
        }
        return DefWindowProcW(h, msg, w, l);
    }
    void AttachUia() {
        Accessibility::Host host; host.window = hwnd_;
        host.nodes = [this] {
            std::vector<Accessibility::Node> nodes; RECT win{}; GetWindowRect(hwnd_, &win);
            Accessibility::Node g; g.key = 1; g.role = Accessibility::Role::Group; g.name = spec_.title; g.help = spec_.body; g.automationId = L"ProfileDialog"; g.focusable = false; g.bounds = win; nodes.push_back(g);
            auto ed = editor_.Nodes(1000, 1, win); nodes.insert(nodes.end(), ed.begin(), ed.end());
            for (int b : {BtnCancel, BtnAccept}) {
                const auto r = ButtonBox(b); Accessibility::Node n; n.key = b; n.parent = 1; n.role = Accessibility::Role::Button; n.name = b == BtnAccept ? spec_.accept : std::wstring(TR(common_cancel)); n.automationId = b == BtnAccept ? L"ProfileDialogAccept" : L"ProfileDialogCancel";
                n.bounds = {win.left + Px(r.left), win.top + Px(r.top), win.left + Px(r.right), win.top + Px(r.bottom)}; nodes.push_back(n);
            }
            if (!error_.empty()) { Accessibility::Node e; e.key = 2; e.parent = 1; e.role = Accessibility::Role::Text; e.name = error_; e.focusable = false; nodes.push_back(e); }
            return nodes;
        };
        host.focused = [this]() -> long long { if (focus_) return focus_; const int f = editor_.Focus(); return f ? 1000 + f : 0; };
        host.invoke = [this](long long key) { if (key == BtnCancel || key == BtnAccept) PostMessageW(hwnd_, WM_KEYDOWN, VK_RETURN, 0), focus_ = static_cast<int>(key); else editor_.Accessible(1000, key, 0, 0); };
        host.select = [this](long long key) { editor_.Accessible(1000, key, 1, 0); };
        host.setRange = [this](long long key, double v) { editor_.Accessible(1000, key, 2, v); };
        host.setValue = [this](long long key, const std::wstring& v) { editor_.AccessibleValue(1000, key, v); };
        host.focus = [this](long long key) { if (key == BtnCancel || key == BtnAccept) Focus(static_cast<int>(key), true); else editor_.Accessible(1000, key, 0, 0); };
        uia_.Attach(std::move(host));
    }
public:
    void Init(HINSTANCE instance, HWND owner, std::wstring root) {
        instance_ = instance; owner_ = owner; root_ = std::move(root);
        WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = instance; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.lpszClassName = L"LisiereProfileDialog"; RegisterClassW(&wc);
    }
    bool Open() const { return hwnd_ != nullptr; }
    HWND Window() const { return hwnd_; }
    // Modal. Returns true when `submit` succeeded.
    bool Run(Spec spec) {
        if (!owner_ || hwnd_) return false;
        spec_ = std::move(spec); done_ = accepted_ = false; error_.clear(); focus_ = hover_ = pressed_ = None; keyboard_ = spec_.keyboard;
        hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED, L"LisiereProfileDialog", spec_.title.c_str(), WS_POPUP, 0, 0, 1, 1, owner_, nullptr, instance_, this);
        if (!hwnd_) return false;
        SetLayeredWindowAttributes(hwnd_, 0, 0, LWA_ALPHA);
        dpi_ = GetDpiForWindow(owner_); chrome_.Init(hwnd_, root_);
        ProfileEditor::Editor::Hooks hooks;
        hooks.invalidate = [this] { Invalidate(); };
        hooks.validateName = spec_.validateName;
        hooks.submit = [this] { Submit(); };
        hooks.focusChanged = [this](ProfileEditor::Stop s) { if (s != ProfileEditor::None && focus_ != None) { focus_ = None; Invalidate(); } };
        hooks.tabOut = [this](int dir) { Focus(dir > 0 ? BtnCancel : BtnAccept, true); };
        editor_.Attach(hwnd_, hooks, ProfileEditor::Style::Dialog); editor_.SetValue(spec_.seed);
        Measure(); Resize(); AttachUia();
        opened_ = Motion::Now(); entering_ = true; leaving_ = false;
        EnableWindow(owner_, FALSE);
        ShowWindow(hwnd_, SW_SHOW); SetForegroundWindow(hwnd_); editor_.SetFocusStop(ProfileEditor::Name, keyboard_);
        SetTimer(hwnd_, FrameTimer, 10, nullptr);
        MSG msg;
        while (!done_ && GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if (msg.message == WM_KEYDOWN && (msg.hwnd == hwnd_ || IsChild(hwnd_, msg.hwnd))) {
                // Edits keep their own keys (they forward Tab / Entrée / Échap themselves).
                if (msg.hwnd == hwnd_) { Handle(hwnd_, WM_KEYDOWN, msg.wParam, msg.lParam); continue; }
            }
            TranslateMessage(&msg); DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) { PostQuitMessage(static_cast<int>(msg.wParam)); break; }
        }
        EnableWindow(owner_, TRUE); SetForegroundWindow(owner_);
        uia_.Detach(); editor_.Detach(); chrome_.Dispose(); DestroyWindow(hwnd_); hwnd_ = nullptr;
        return accepted_;
    }
    void Cancel() { if (hwnd_) { accepted_ = false; done_ = true; PostMessageW(owner_, WM_NULL, 0, 0); } }
};

} // namespace ProfileDialog
} // namespace NativeChrome
