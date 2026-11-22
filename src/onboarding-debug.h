#pragma once
// Onboarding — review panel for its motion (« --onboarding --onboarding-debug » only; never in a normal run).
// A small dark tool window beside the onboarding window: a selector for the transition (1 → 2 … 6 → 7, then
// « 7 → navigateur »), one slider per parameter of that section (src/onboarding-tuning.h), live, then « Rejouer »
// (plays the selected transition), « Réinitialiser » (that section), « Copier » (all the values as text, to hand back
// so they become the defaults) and « Enregistrer ». The values are kept in <data>\onboarding-tuning.txt after every
// change and read again at the next debug run. Review tool: French only (as src/motion-lab.h).
#include <windows.h>
#include <windowsx.h>
#include <functional>
#include <string>
#include "chrome.h"
#include "native-ui.h"
#include "tooltip.h"
#include "profile-store.h"
#include "onboarding-kit.h"
#include "onboarding-tuning.h"

namespace NativeChrome {
namespace OnboardingDebug {

namespace K = OnboardingKit; namespace T = Onboarding::Tuning;
namespace Layout { inline constexpr float width = 320, pad = 16, title = 44, selector = 40, row = 40, button = 32, gap = 8, track = 4; }

inline std::string Load(const std::wstring& file) { std::string bytes; ProfileStore::ReadAll(file, bytes); return bytes; }
inline bool Save(const std::wstring& file, const T::Values& v) { return ProfileStore::WriteAtomic(file, T::Write(v)); }

class Panel {
    enum Button { None = 0, Replay = 100, Reset = 101, Copy = 102, Store = 103, Previous = 104, Next = 105 };
    HWND hwnd_ = nullptr, owner_ = nullptr; Chrome chrome_; K::Glyphs glyphs_; UINT dpi_ = 96; std::wstring file_, note_;
    T::Values* values_ = nullptr; std::function<void()> changed_; std::function<void(T::Section)> replay_;
    int section_ = T::T23, hover_ = None, pressed_ = None, dragging_ = -1;
    T::Section Section() const { return static_cast<T::Section>(section_); }
    int Rows() const { return T::ParamCount(Section()); }
    float& Value(int i) { return (*values_)[Section()][static_cast<std::size_t>(i)]; }
    float Scale() const { return dpi_ / 96.f; }
    int Px(float v) const { return static_cast<int>(std::lround(v * Scale())); }
    float Top() const { return Layout::title + Layout::selector; }
    float Height() const { return Top() + T::MaxParams * Layout::row + Layout::gap + 2 * (Layout::button + Layout::gap) + Layout::pad + 20; }
    D2D1_RECT_F Track(int i) const { const float y = Top() + i * Layout::row + 27; return {Layout::pad, y, Layout::width - Layout::pad, y + Layout::track}; }
    D2D1_RECT_F Lane(int i) const { const float y = Top() + i * Layout::row; return {Layout::pad, y + 17, Layout::width - Layout::pad, y + Layout::row}; }
    D2D1_RECT_F ButtonBox(int b) const {
        if (b == Previous) return {Layout::pad, Layout::title, Layout::pad + 32, Layout::title + 32};
        if (b == Next) return {Layout::width - Layout::pad - 32, Layout::title, Layout::width - Layout::pad, Layout::title + 32};
        const float top = Top() + T::MaxParams * Layout::row + Layout::gap;
        if (b == Replay) return {Layout::pad, top, Layout::width - Layout::pad, top + Layout::button};
        const float y = top + Layout::button + Layout::gap, third = (Layout::width - 2 * Layout::pad - 2 * Layout::gap) / 3, x = Layout::pad + (b - Reset) * (third + Layout::gap);
        return {x, y, x + third, y + Layout::button};
    }
    static bool In(D2D1_RECT_F r, float x, float y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }
    int HitButton(float x, float y) const { for (int b : {Replay, Reset, Copy, Store, Previous, Next}) if (In(ButtonBox(b), x, y)) return b; return None; }
    int HitSlider(float x, float y) const { for (int i = 0; i < Rows(); ++i) if (In(Lane(i), x, y)) return i; return -1; }
    void Invalidate() { if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE); }
    void Changed() { Save(file_, *values_); note_.clear(); if (changed_) changed_(); Invalidate(); }
    void Drag(int i, float x) {
        const auto t = Track(i); const T::Param& p = T::Params(Section())[i];
        const float u = std::min(1.f, std::max(0.f, (x - t.left) / (t.right - t.left))), v = T::Step(Section(), i, p.min + u * (p.max - p.min));
        if (v != Value(i)) { Value(i) = v; Changed(); }
    }
    static std::wstring Number(const T::Param& p, float v) { wchar_t b[48]; swprintf(b, 48, L"%.*f", p.decimals, static_cast<double>(v)); std::wstring s = b; for (auto& c : s) if (c == L'.') c = L','; if (*p.unit) s += std::wstring(L" ") + p.unit; return s; }
    void CopyText() {
        const std::string text = T::Write(*values_); const std::wstring wide(text.begin(), text.end());
        if (!OpenClipboard(hwnd_)) return;
        EmptyClipboard();
        if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (wide.size() + 1) * sizeof(wchar_t))) { if (void* p = GlobalLock(h)) { memcpy(p, wide.c_str(), (wide.size() + 1) * sizeof(wchar_t)); GlobalUnlock(h); SetClipboardData(CF_UNICODETEXT, h); } }
        CloseClipboard(); note_ = L"Toutes les valeurs sont copiées dans le presse-papiers.";
    }
    void Press(int b) {
        if (b == Replay) { if (replay_) replay_(Section()); }
        else if (b == Previous) { section_ = (section_ + T::Sections - 1) % T::Sections; note_.clear(); }
        else if (b == Next) { section_ = (section_ + 1) % T::Sections; note_.clear(); }
        else if (b == Reset) { (*values_)[Section()] = T::Defaults(Section()); Changed(); note_ = std::wstring(L"Valeurs d'origine rétablies pour ") + T::Label(Section()) + L"."; }
        else if (b == Copy) CopyText();
        else if (b == Store) note_ = Save(file_, *values_) ? L"Enregistré : " + file_.substr(file_.find_last_of(L'\\') + 1) : L"Enregistrement impossible.";
        Invalidate();
    }
    void Paint() {
        if (!chrome_.RenderTarget()) chrome_.Resize();
        auto* t = chrome_.RenderTarget(); if (!t) return;
        t->BeginDraw(); t->Clear(K::Col(K::Token::canvas));
        K::Text(chrome_, L"Transitions de l'accueil", {Layout::pad, 14, Layout::width, 34}, {16, 20, 600, -.16f}, K::Col(K::Token::text));
        for (int b : {Previous, Next}) {
            const auto r = ButtonBox(b); K::Fill(chrome_, r, 6, K::Col(hover_ == b ? (pressed_ == b ? K::Token::pressed : K::Token::hover) : K::Token::raised));
            D2D1_MATRIX_3X2_F base; t->GetTransform(&base);
            t->SetTransform(D2D1::Matrix3x2F::Rotation(b == Previous ? 90.f : -90.f, D2D1::Point2F((r.left + r.right) / 2, (r.top + r.bottom) / 2)) * base);
            glyphs_.Stroke(chrome_, K::Path::chevron, {r.left + 8, r.top + 8, r.right - 8, r.bottom - 8}, 24, 1.8f, K::Col(K::Token::text)); t->SetTransform(base);
        }
        K::Text(chrome_, T::Label(Section()), {Layout::pad + 32, Layout::title + 6, Layout::width - Layout::pad - 32, Layout::title + 26}, {14, 20, 500, 0}, K::Col(K::Token::text), DWRITE_TEXT_ALIGNMENT_CENTER);
        for (int i = 0; i < Rows(); ++i) {
            const T::Param& p = T::Params(Section())[i]; const float y = Top() + i * Layout::row, v = Value(i);
            K::Text(chrome_, p.label, {Layout::pad, y, Layout::width - 76, y + 20}, {13, 18, 400, 0}, K::Col(K::Token::secondary), DWRITE_TEXT_ALIGNMENT_LEADING, false, true);
            K::Text(chrome_, Number(p, v), {Layout::width - 100, y, Layout::width - Layout::pad, y + 20}, {13, 18, 500, 0}, K::Col(K::Token::text), DWRITE_TEXT_ALIGNMENT_TRAILING);
            const auto tr = Track(i); const float u = (v - p.min) / (p.max - p.min), x = tr.left + u * (tr.right - tr.left), d = tr.left + (p.def - p.min) / (p.max - p.min) * (tr.right - tr.left);
            K::Fill(chrome_, tr, 2, PopupKit::White(.12f)); K::Fill(chrome_, {tr.left, tr.top, x, tr.bottom}, 2, K::Col(K::Token::accent));
            if (v != p.def) K::Fill(chrome_, {d - .5f, tr.top - 3, d + .5f, tr.bottom + 3}, 0, PopupKit::White(.35f));   // where the default sits
            K::Fill(chrome_, {x - 2, tr.top - 6, x + 2, tr.bottom + 6}, 2, K::Col(K::Token::text));
        }
        const std::wstring replay = std::wstring(L"Rejouer ") + T::Label(Section());
        const std::wstring labels[4] = {replay, L"Réinitialiser", L"Copier", L"Enregistrer"};
        for (int b : {Replay, Reset, Copy, Store}) {
            const auto r = ButtonBox(b); const bool primary = b == Replay; const unsigned fill = primary ? K::Token::accent : hover_ == b ? (pressed_ == b ? K::Token::pressed : K::Token::hover) : K::Token::raised;
            K::Fill(chrome_, r, 6, K::Col(fill, primary && hover_ == b ? .88f : 1.f));
            K::Text(chrome_, labels[b - Replay], {r.left, r.top + 6, r.right, r.bottom}, {13, 20, 500, 0}, K::Col(K::Token::text), DWRITE_TEXT_ALIGNMENT_CENTER);
        }
        const auto last = ButtonBox(Store);
        K::Text(chrome_, note_, {Layout::pad, last.bottom + 8, Layout::width - Layout::pad, last.bottom + 26}, {12, 16, 400, 0}, PopupKit::White(.6f), DWRITE_TEXT_ALIGNMENT_LEADING, false, true);
        PopupKit::Outline(chrome_, Layout::width, Height(), 12, PopupKit::White(0x1A / 255.f));
        if (t->EndDraw() == D2DERR_RECREATE_TARGET) chrome_.RecreateTarget();
    }
    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<Panel*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (msg == WM_NCCREATE) { self = static_cast<Panel*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); }
        return self && self->hwnd_ == h ? self->Handle(h, msg, w, l) : DefWindowProcW(h, msg, w, l);
    }
    LRESULT Handle(HWND h, UINT msg, WPARAM w, LPARAM l) {
        const float s = Scale(), x = GET_X_LPARAM(l) / s, y = GET_Y_LPARAM(l) / s;
        switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); Paint(); EndPaint(h, &ps); return 0; }
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;   // the onboarding window keeps the keyboard
        case WM_NCHITTEST: { POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)}; ScreenToClient(h, &p); return p.y / s < Layout::title - 8 ? HTCAPTION : HTCLIENT; }
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT tr{sizeof(tr), TME_LEAVE, h, 0}; TrackMouseEvent(&tr);
            if (dragging_ >= 0) { Drag(dragging_, x); return 0; }
            const int b = HitButton(x, y); if (b != hover_) { hover_ = b; Invalidate(); } return 0;
        }
        case WM_MOUSELEAVE: if (hover_) { hover_ = None; Invalidate(); } return 0;
        case WM_LBUTTONDOWN: { SetCapture(h); const int i = HitSlider(x, y); if (i >= 0) { dragging_ = i; Drag(i, x); } else { pressed_ = HitButton(x, y); Invalidate(); } return 0; }
        case WM_LBUTTONUP: { const int was = pressed_; const bool slid = dragging_ >= 0; pressed_ = None; dragging_ = -1; if (GetCapture() == h) ReleaseCapture(); if (!slid && was && HitButton(x, y) == was) Press(was); Invalidate(); return 0; }
        case WM_MOUSEWHEEL: { POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)}; ScreenToClient(h, &p); const int i = HitSlider(p.x / s, p.y / s); if (i >= 0) { Value(i) = T::Step(Section(), i, Value(i) + (GET_WHEEL_DELTA_WPARAM(w) > 0 ? 1 : -1) * T::Params(Section())[i].step); Changed(); } return 0; }
        case WM_DPICHANGED: { dpi_ = HIWORD(w); const RECT* r = reinterpret_cast<const RECT*>(l); SetWindowPos(h, nullptr, r->left, r->top, Px(Layout::width), Px(Height()), SWP_NOZORDER | SWP_NOACTIVATE); NativeUI::ConfigureShadowlessPopup(h, 12); chrome_.Resize(); Invalidate(); return 0; }
        }
        return DefWindowProcW(h, msg, w, l);
    }
public:
    HWND Window() const { return hwnd_; }
    // `values`: the window's own tuning (changed in place); `file`: where they are kept between debug runs.
    bool Open(HINSTANCE instance, HWND owner, const std::wstring& root, const std::wstring& file, T::Values* values, std::function<void()> changed, std::function<void(T::Section)> replay) {
        owner_ = owner; file_ = file; values_ = values; changed_ = std::move(changed); replay_ = std::move(replay);
        WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = instance; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.lpszClassName = L"LisiereOnboardingDebug"; RegisterClassW(&wc);
        hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST, wc.lpszClassName, L"Transitions de l'accueil", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, instance, this);
        if (!hwnd_) return false;
        NativeUI::ConfigureFullClientFrame(hwnd_);
        RECT o{}; GetWindowRect(owner, &o); SetWindowPos(hwnd_, nullptr, o.right + 12, o.top, 1, 1, SWP_NOZORDER | SWP_NOACTIVATE);
        dpi_ = GetDpiForWindow(hwnd_); if (!dpi_) dpi_ = 96;
        if (FAILED(chrome_.Init(hwnd_, root))) { DestroyWindow(hwnd_); hwnd_ = nullptr; return false; }
        MONITORINFO mi{sizeof(mi)}; GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &mi);
        int x = o.right + Px(12); if (x + Px(Layout::width) > mi.rcWork.right) x = std::max<int>(mi.rcWork.left, o.left - Px(12) - Px(Layout::width));
        const int y = std::max<int>(mi.rcWork.top, std::min<int>(o.top, mi.rcWork.bottom - Px(Height())));
        SetWindowPos(hwnd_, HWND_TOPMOST, x, y, Px(Layout::width), Px(Height()), SWP_NOACTIVATE);
        NativeUI::ConfigureShadowlessPopup(hwnd_, 12); chrome_.Resize();
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE); return true;
    }
    void Close() { if (!hwnd_) return; glyphs_.Clear(); chrome_.Dispose(); DestroyWindow(hwnd_); hwnd_ = nullptr; }
};

} // namespace OnboardingDebug
} // namespace NativeChrome
