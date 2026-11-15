#pragma once
#include "i18n.h"
// « Supprimer le profil « X » ? » (Paper p-I-0 P04, current edits: Y6V-0 default, Y7G-0 open windows,
// Y81-0 in progress, YRQ-0 failure). A dedicated surface: the generic A1 modal does not have its
// structure (identity glyph in the title, consequences block between hairlines, busy row).
//
//   400 wide · canvas · 1 px #FFFFFF14 · radius 16 · padding 16 · gap 16
//   title: Fill glyph 16 in the profile ink · gap 8 · 16/20 Medium −0.01em white
//   « Conséquences »: hairlines #FFFFFF0D top and bottom, padding 12 / 0, gap 12; two items
//     (label 14/20 white, detail 14/18 #FFFFFF80, gap 2)
//   failure: warning-circle 16 danger (2 down) · gap 6 · 14/20 danger text
//   actions (padding-top 8, gap 8, right): « Annuler » raised #292929 · « Supprimer le profil »
//     plate #FF7B6E24 (hover #FF7B6E3D) danger text; both radius 8, h 32, min 64, padding 12, 14/18 Medium
//     focus: inset 1 px #FFFFFF80. Default focus « Annuler »; after a failure « Réessayer ».
//   in progress: circle-notch 16 #FFFFFFB3 (turning; still in reduced motion) + « Suppression… »
//     14/18 Medium #FFFFFFB3 on the left; both buttons at 32 %, inert; Échap inert.
// Échap = Annuler. Entrée/Espace activate the focused button only (never a destructive default).
#include <windows.h>
#include <windowsx.h>
#include <functional>
#include <string>
#include "chrome.h"
#include "native-ui.h"
#include "accessibility.h"
#include "tooltip.h"

namespace NativeChrome {
namespace ProfileDeleteDialog {

namespace Glyph {
inline constexpr const char* warningCircle = "M128,20A108,108,0,1,0,236,128,108.12,108.12,0,0,0,128,20Zm0,192a84,84,0,1,1,84-84A84.09,84.09,0,0,1,128,212Zm-12-80V80a12,12,0,0,1,24,0v52a12,12,0,0,1-24,0Zm28,40a16,16,0,1,1-16-16A16,16,0,0,1,144,172Z";
inline constexpr const char* circleNotch = "M236,128a108,108,0,0,1-216,0c0-42.52,24.73-81.34,63-98.9A12,12,0,1,1,93,50.91C63.24,64.57,44,94.83,44,128a84,84,0,0,0,168,0c0-33.17-19.24-63.43-49-77.09A12,12,0,1,1,173,29.1C211.27,46.66,236,85.48,236,128Z";
}
namespace Layout {
inline constexpr float width = 400, pad = 16, gap = 16, radius = 16, inner = width - 2 * pad;
inline constexpr float button = 32, buttonPad = 12, buttonMin = 64, actionsTop = 8, buttonGap = 8;
inline constexpr unsigned border = 0x14, hairline = 0x0D;
inline constexpr float plate = 0x24 / 255.f, plateHover = 0x3D / 255.f, disabled = .32f;
}
struct Spec {
    std::wstring name; const char* glyph = nullptr; D2D1_COLOR_F ink{1, 1, 1, 1};
    bool keyboard = false, reduced = false;
    std::wstring note;               // a truthful extra line when it applies (downloads in progress are cancelled)
    std::function<void()> confirm;   // starts the delete; the host ends it with Finish() or Fail()
};

class Dialog {
    enum Button { None = 0, BtnCancel = 10, BtnDelete = 11 };
    HINSTANCE instance_ = nullptr; HWND owner_ = nullptr, hwnd_ = nullptr; std::wstring root_;
    Chrome chrome_; Accessibility::Bridge uia_; PopupKit::GlyphCache glyphs_;
    Spec spec_; UINT dpi_ = 96; float height_ = 0, erasedH_ = 0, keptH_ = 0, errorH_ = 0;
    bool done_ = false, deleted_ = false, busy_ = false, keyboard_ = false, leaving_ = false, entering_ = false;
    int focus_ = BtnCancel, hover_ = None, pressed_ = None; std::wstring error_;
    Motion::Time opened_{}, left_{}, busySince_{};
    static constexpr UINT_PTR FrameTimer = 1;
    static const wchar_t* erasedLabel() { return TR(profile_delete_erased); }
    static const wchar_t* erasedText() { return TR(profile_delete_erased_text); }
    static const wchar_t* keptLabel() { return TR(profile_delete_kept); }
    static const wchar_t* keptText() { return TR(profile_delete_kept_text); }
    float Scale() const { return dpi_ / 96.f; }
    int Px(float dip) const { return static_cast<int>(std::lround(dip * Scale())); }
    std::wstring Title() const { return TRF(profile_delete_title, spec_.name); }
    std::wstring DeleteLabel() const { return error_.empty() ? TR(profile_delete_action) : TR(downloads_action_retry); }
    // Geometry (DIP).
    float BlockTop() const { return Layout::pad + 20 + Layout::gap; }
    float BlockH() const { return 1 + 12 + 20 + 2 + erasedH_ + 12 + 20 + 2 + keptH_ + 12 + 1; }
    float noteH_ = 0;
    float NoteTop() const { return BlockTop() + BlockH() + Layout::gap; }
    float ErrorTop() const { return NoteTop() + (spec_.note.empty() ? 0 : noteH_ + Layout::gap); }
    float ActionsTop() const { return ErrorTop() + (error_.empty() ? 0 : errorH_ + Layout::gap) + Layout::actionsTop; }
    float ButtonW(int b) { return std::max(Layout::buttonMin, std::ceil(chrome_.TextWidth(b == BtnDelete ? DeleteLabel() : std::wstring(TR(common_cancel)), 14, true, false, -.14f)) + 2 * Layout::buttonPad); }
    D2D1_RECT_F Box(int b) {
        const float y = ActionsTop(), right = Layout::width - Layout::pad, dw = ButtonW(BtnDelete), cw = ButtonW(BtnCancel);
        if (b == BtnDelete) return {right - dw, y, right, y + Layout::button};
        return {right - dw - Layout::buttonGap - cw, y, right - dw - Layout::buttonGap, y + Layout::button};
    }
    void Measure() {
        PopupKit::Type detail{14, 18}, err{14, 20};
        erasedH_ = PopupKit::Measure(chrome_, erasedText(), Layout::inner, detail, true).height;
        keptH_ = PopupKit::Measure(chrome_, keptText(), Layout::inner, detail, true).height;
        errorH_ = error_.empty() ? 0 : PopupKit::Measure(chrome_, error_, Layout::inner - 22, err, true).height;
        noteH_ = spec_.note.empty() ? 0 : PopupKit::Measure(chrome_, spec_.note, Layout::inner, err, true).height;
        height_ = ActionsTop() + Layout::button + Layout::pad;
    }
    void Place() {
        RECT o{}; GetWindowRect(owner_, &o); const int w = Px(Layout::width), h = Px(height_);
        const int x = o.left + (o.right - o.left - w) / 2, y = o.top + std::max<int>(Px(80), (o.bottom - o.top - h) / 3);
        restX_ = x; restY_ = y;
        SetWindowPos(hwnd_, HWND_TOP, x, y, w, h, SWP_NOACTIVATE); NativeUI::ConfigureShadowlessPopup(hwnd_, Layout::radius); chrome_.Resize();
    }
    // P05 « Modales »: opacity + 4 DIP rise in 150 ms (ease-out), opacity out 120 ms; reduced: opacity only.
    // No scrim: Paper P03/P04 draw none (the #0E0E0E behind them is the specimen panel).
    int restX_ = 0, restY_ = 0;
    void Present(float a, float riseU) {
        SetLayeredWindowAttributes(hwnd_, 0, static_cast<BYTE>(std::lround(255 * a)), LWA_ALPHA);
        const float e = 1 - (1 - riseU) * (1 - riseU) * (1 - riseU); const int rise = spec_.reduced ? 0 : Px(4 * (1 - e));
        SetWindowPos(hwnd_, nullptr, restX_, restY_ + rise, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void Invalidate() { if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE); }
    void Activate(int b) {
        if (busy_ || leaving_) return;
        if (b == BtnCancel) { deleted_ = false; Close(); return; }
        if (b == BtnDelete) { busy_ = true; busySince_ = Motion::Now(); hover_ = None; Invalidate(); SetTimer(hwnd_, FrameTimer, 16, nullptr); if (uia_.Listening()) uia_.ChildrenChanged(1); if (spec_.confirm) spec_.confirm(); }
    }
    void Close() { if (leaving_) return; leaving_ = true; left_ = Motion::Now(); SetTimer(hwnd_, FrameTimer, 10, nullptr); }
    void Button(int b, float dim) {
        const auto box = Box(b); const bool del = b == BtnDelete; const bool hover = hover_ == b && !busy_;
        if (del) PopupKit::Fill(chrome_, box, 8, D2D1::ColorF(Design::Danger::r, Design::Danger::g, Design::Danger::b, (hover ? Layout::plateHover : Layout::plate) * dim));
        else { const float r = Design::Surface::raised + (hover ? .04f : 0); PopupKit::Fill(chrome_, box, 8, D2D1::ColorF(r, r, r, dim)); }
        if (focus_ == b && !busy_ && keyboard_) { auto* br = chrome_.SolidBrush(); br->SetColor(PopupKit::White(.5f)); chrome_.RenderTarget()->DrawRoundedRectangle(D2D1::RoundedRect({box.left + .5f, box.top + .5f, box.right - .5f, box.bottom - .5f}, 7.5f, 7.5f), br, 1); }
        const D2D1_COLOR_F ink = del ? D2D1::ColorF(Design::Danger::r, Design::Danger::g, Design::Danger::b, dim) : PopupKit::White(dim);
        PopupKit::DrawText(chrome_, del ? DeleteLabel() : std::wstring(TR(common_cancel)), {box.left, box.top + 7, box.right, box.bottom}, PopupKit::Type{14, 18, -.14f, true}, ink, false, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    void Paint() {
        if (!chrome_.RenderTarget()) chrome_.Resize();
        auto* t = chrome_.RenderTarget(); if (!t) return; const float k = Design::Surface::canvas;
        t->BeginDraw(); t->Clear(D2D1::ColorF(k, k, k, 1));
        const float L = Layout::pad, R = Layout::width - Layout::pad;
        // Title: identity glyph in the profile ink, then the question.
        if (spec_.glyph) glyphs_.Draw(chrome_, spec_.glyph, {L, L + 2, L + 16, L + 18}, 16, spec_.ink);
        PopupKit::DrawText(chrome_, Title(), {L + 24, L, R, L + 20}, PopupKit::Type{16, 20, -.16f, true}, PopupKit::White(1));
        // Consequences, between hairlines.
        float y = BlockTop(); const auto hair = PopupKit::White(Layout::hairline / 255.f);
        PopupKit::Fill(chrome_, {L, y, R, y + 1}, 0, hair); y += 1 + 12;
        PopupKit::DrawText(chrome_, erasedLabel(), {L, y, R, y + 20}, PopupKit::Type{14, 20}, PopupKit::White(1)); y += 22;
        PopupKit::DrawText(chrome_, erasedText(), {L, y, R, y + erasedH_}, PopupKit::Type{14, 18}, PopupKit::White(.5f), true); y += erasedH_ + 12;
        PopupKit::DrawText(chrome_, keptLabel(), {L, y, R, y + 20}, PopupKit::Type{14, 20}, PopupKit::White(1)); y += 22;
        PopupKit::DrawText(chrome_, keptText(), {L, y, R, y + keptH_}, PopupKit::Type{14, 18}, PopupKit::White(.5f), true); y += keptH_ + 12;
        PopupKit::Fill(chrome_, {L, y, R, y + 1}, 0, hair);
        if (!spec_.note.empty()) PopupKit::DrawText(chrome_, spec_.note, {L, NoteTop(), R, NoteTop() + noteH_}, PopupKit::Type{14, 20}, PopupKit::White(.7f), true);
        // Failure, in line.
        if (!error_.empty()) {
            const float ey = ErrorTop(); const D2D1_COLOR_F danger = D2D1::ColorF(Design::Danger::r, Design::Danger::g, Design::Danger::b, 1);
            glyphs_.Draw(chrome_, Glyph::warningCircle, {L, ey + 2, L + 16, ey + 18}, 16, danger);
            PopupKit::DrawText(chrome_, error_, {L + 22, ey, R, ey + errorH_}, PopupKit::Type{14, 20}, danger, true);
        }
        // Busy row, then the actions.
        const float ay = ActionsTop();
        if (busy_) {
            const float turn = spec_.reduced ? 0.f : static_cast<float>(std::fmod(std::chrono::duration<double>(Motion::Now() - busySince_).count() * 360.0, 360.0));
            D2D1_MATRIX_3X2_F base; t->GetTransform(&base);
            t->SetTransform(D2D1::Matrix3x2F::Rotation(turn, {L + 8, ay + 16}) * base);
            glyphs_.Draw(chrome_, Glyph::circleNotch, {L, ay + 8, L + 16, ay + 24}, 16, PopupKit::White(.7f));
            t->SetTransform(base);
            PopupKit::DrawText(chrome_, TR(profile_deleting), {L + 24, ay + 7, R, ay + 25}, PopupKit::Type{14, 18, -.14f, true}, PopupKit::White(.7f));
        }
        const float dim = busy_ ? Layout::disabled : 1.f;
        Button(BtnCancel, dim); Button(BtnDelete, dim);
        PopupKit::Outline(chrome_, Layout::width, height_, Layout::radius, PopupKit::White(Layout::border / 255.f));
        if (t->EndDraw() == D2DERR_RECREATE_TARGET) chrome_.RecreateTarget();
    }
    void Step() {
        const auto now = Motion::Now(); const auto ms = [&](Motion::Time a) { return static_cast<float>(std::chrono::duration<double, std::milli>(now - a).count()); };
        float a = 1;
        if (leaving_) a = 1 - Motion::Clamp(ms(left_) / 120.f);
        else if (entering_) { a = Motion::Clamp(ms(opened_) / 150.f); if (a >= 1) entering_ = false; }
        Present(a, leaving_ || !entering_ ? 1.f : Motion::Clamp(ms(opened_) / 150.f));
        if (busy_ && !spec_.reduced) Invalidate();
        if (leaving_ && a <= 0) { KillTimer(hwnd_, FrameTimer); done_ = true; PostMessageW(owner_, WM_NULL, 0, 0); }
        else if (!entering_ && !leaving_ && !busy_) KillTimer(hwnd_, FrameTimer);
    }
    int Hit(float x, float y) { for (int b : {BtnCancel, BtnDelete}) { const auto r = Box(b); if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return b; } return None; }
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
        case WM_TIMER: if (w == FrameTimer) Step(); return 0;
        case WM_MOUSEMOVE: { TRACKMOUSEEVENT tr{sizeof(tr), TME_LEAVE, h, 0}; TrackMouseEvent(&tr); const int b = Hit(GET_X_LPARAM(l) / s, GET_Y_LPARAM(l) / s); if (b != hover_) { hover_ = b; Invalidate(); } return 0; }
        case WM_MOUSELEAVE: if (hover_) { hover_ = None; Invalidate(); } return 0;
        case WM_LBUTTONDOWN: pressed_ = Hit(GET_X_LPARAM(l) / s, GET_Y_LPARAM(l) / s); if (pressed_ && !busy_) { focus_ = pressed_; keyboard_ = false; Invalidate(); } return 0;
        case WM_LBUTTONUP: { const int b = Hit(GET_X_LPARAM(l) / s, GET_Y_LPARAM(l) / s); if (b && b == pressed_) Activate(b); pressed_ = None; return 0; }
        case WM_KEYDOWN:
            keyboard_ = true;
            if (w == VK_ESCAPE) { if (!busy_) Activate(BtnCancel); return 0; }
            if (w == VK_TAB || w == VK_LEFT || w == VK_RIGHT) { if (!busy_) { focus_ = focus_ == BtnCancel ? BtnDelete : BtnCancel; Invalidate(); if (uia_.Listening()) uia_.Raise(focus_, UIA_AutomationFocusChangedEventId); } return 0; }
            if (w == VK_RETURN || w == VK_SPACE) { Activate(focus_); return 0; }
            return 0;
        case WM_CLOSE: if (!busy_) Activate(BtnCancel); return 0;
        }
        return DefWindowProcW(h, msg, w, l);
    }
    void AttachUia() {
        Accessibility::Host host; host.window = hwnd_;
        host.nodes = [this] {
            std::vector<Accessibility::Node> nodes; RECT win{}; GetWindowRect(hwnd_, &win);
            Accessibility::Node g; g.key = 1; g.role = Accessibility::Role::Group; g.name = Title(); g.automationId = L"ProfileDeleteDialog"; g.focusable = false; g.bounds = win;
            g.help = TRF(profile_delete_help, erasedLabel(), erasedText(), keptLabel(), keptText()); if (busy_) g.status = TR(profile_deleting); nodes.push_back(g);
            if (!error_.empty()) { Accessibility::Node e; e.key = 2; e.parent = 1; e.role = Accessibility::Role::Text; e.name = error_; e.automationId = L"ProfileDeleteError"; e.focusable = false; nodes.push_back(e); }
            for (int b : {BtnCancel, BtnDelete}) {
                const auto r = Box(b); Accessibility::Node n; n.key = b; n.parent = 1; n.role = Accessibility::Role::Button; n.enabled = !busy_;
                n.name = b == BtnDelete ? DeleteLabel() : std::wstring(TR(common_cancel)); n.automationId = b == BtnDelete ? L"ProfileDeleteConfirm" : L"ProfileDeleteCancel";
                n.bounds = {win.left + Px(r.left), win.top + Px(r.top), win.left + Px(r.right), win.top + Px(r.bottom)}; nodes.push_back(n);
            }
            return nodes;
        };
        host.focused = [this]() -> long long { return busy_ ? 1 : focus_; };
        host.invoke = [this](long long key) { if (key == BtnCancel || key == BtnDelete) { const int b = static_cast<int>(key); PostMessageW(hwnd_, WM_APP + 1, b, 0); } };
        host.focus = [this](long long key) { if (key == BtnCancel || key == BtnDelete) { focus_ = static_cast<int>(key); keyboard_ = true; Invalidate(); } };
        uia_.Attach(std::move(host));
    }
public:
    void Init(HINSTANCE instance, HWND owner, std::wstring root) {
        instance_ = instance; owner_ = owner; root_ = std::move(root);
        WNDCLASSW wc{}; wc.lpfnWndProc = [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT { if (m == WM_APP + 1) { auto* self = reinterpret_cast<Dialog*>(GetWindowLongPtrW(h, GWLP_USERDATA)); if (self) self->Activate(static_cast<int>(w)); return 0; } return Proc(h, m, w, l); };
        wc.hInstance = instance; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.lpszClassName = L"LisiereProfileDeleteDialog"; RegisterClassW(&wc);
    }
    bool Open() const { return hwnd_ != nullptr; }
    HWND Window() const { return hwnd_; }
    // The host's answer to `confirm`.
    void Finish() { if (!hwnd_) return; busy_ = false; deleted_ = true; KillTimer(hwnd_, FrameTimer); Close(); }
    void Fail(const std::wstring& error) {
        if (!hwnd_) return; busy_ = false; error_ = error; focus_ = BtnDelete; Measure();   // the ring follows the modality that confirmed Place(); Invalidate();
        if (uia_.Listening()) { uia_.ChildrenChanged(1); uia_.Raise(BtnDelete, UIA_AutomationFocusChangedEventId); }
    }
    bool Busy() const { return busy_; }
    // Modal. True when the profile was deleted (Finish), false when cancelled.
    bool Run(Spec spec) {
        if (!owner_ || hwnd_) return false;
        spec_ = std::move(spec); done_ = deleted_ = busy_ = leaving_ = false; error_.clear(); focus_ = BtnCancel; hover_ = pressed_ = None; keyboard_ = spec_.keyboard;
        hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED, L"LisiereProfileDeleteDialog", Title().c_str(), WS_POPUP, 0, 0, 1, 1, owner_, nullptr, instance_, this);
        if (!hwnd_) return false;
        SetLayeredWindowAttributes(hwnd_, 0, 0, LWA_ALPHA); dpi_ = GetDpiForWindow(owner_); chrome_.Init(hwnd_, root_);
        Measure(); Place(); AttachUia();
        opened_ = Motion::Now(); entering_ = true; EnableWindow(owner_, FALSE);
        ShowWindow(hwnd_, SW_SHOW); SetForegroundWindow(hwnd_); SetFocus(hwnd_); SetTimer(hwnd_, FrameTimer, 10, nullptr);
        MSG msg;
        while (!done_ && GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if (msg.message == WM_KEYDOWN && msg.hwnd != hwnd_ && GetForegroundWindow() == hwnd_) { Handle(hwnd_, WM_KEYDOWN, msg.wParam, msg.lParam); continue; }
            TranslateMessage(&msg); DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) { PostQuitMessage(static_cast<int>(msg.wParam)); break; }
        }
        EnableWindow(owner_, TRUE); SetForegroundWindow(owner_);
        uia_.Detach(); glyphs_.Clear(); chrome_.Dispose(); DestroyWindow(hwnd_); hwnd_ = nullptr;
        return deleted_;
    }
    // Tests: render a state off screen (same Paint path).
    bool RenderForTest(const std::wstring& root, Spec spec, int state, const wchar_t* png) {
        spec_ = std::move(spec); error_.clear(); busy_ = false; focus_ = BtnCancel; keyboard_ = true;
        if (state == 1) { busy_ = true; busySince_ = Motion::Now(); spec_.reduced = true; }
        if (state == 2) { error_ = TR(profile_delete_in_use); focus_ = BtnDelete; }
        // Measure on a provisional surface, then paint at the final height (same Paint path).
        if (FAILED(chrome_.InitOffscreen(root, 96, Layout::width, 600))) return false;
        Measure(); chrome_.Dispose();
        if (FAILED(chrome_.InitOffscreen(root, 96, Layout::width, height_))) return false;
        Measure(); Paint(); const bool ok = SUCCEEDED(chrome_.SavePng(png)); chrome_.Dispose(); glyphs_.Clear(); return ok;
    }
    float HeightForTest() const { return height_; }
};

} // namespace ProfileDeleteDialog
} // namespace NativeChrome
