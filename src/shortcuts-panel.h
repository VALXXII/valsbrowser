#pragma once
#include "i18n.h"
// C5 · Raccourcis clavier — Ctrl+/ (Paper « Browser Design », p-D-0, artboard EP1-0, panneau
// EWC-0). Native popover listing the browser's real shortcuts (those main's Key() maps), with a
// native EDIT filter. Opens centred over the page, focus in the filter; Échap closes and gives
// focus back; a click elsewhere closes. No shadow (NativeUI::ConfigureShadowlessPopup).
//
// Paper values (get_jsx EWC-0): 560 wide, #171717 (--color-browser-canvas, user request
//   2026-11-09, was #292929), border 1 px #FFFFFF0F, radius 6
//   header padding 12 / 16 left / 12 right: title 16/20 medium #FFFFFFE6, close 24x24 radius 3,
//     glyph 14 #FFFFFFB3 (hover #333333)
//   filter: padding 0 12 8; field h 32 #FFFFFF0A, border 1 px #FFFFFF1A, radius 6, padding 8,
//     gap 8, search glyph 14 #FFFFFFB3, text 14/18, placeholder « Filtrer les raccourcis » #FFFFFF66
//   section: padding-top 8, inline 12 (last: bottom 12); heading padding 8/8/4, 14/20 #FFFFFF80
//   row h 32, padding 8, radius 6, gap 8: label 14/20 #FFFFFFE6; keys gap 4, cap h 24, min 24,
//     padding 6, radius 3, #FFFFFF14, 13/16 medium #FFFFFFCC; hovered row #333333.
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include "native-ui.h"
#include <functional>
#include <string>
#include <vector>
#include "tooltip.h"

namespace NativeChrome {
namespace ShortcutsPanel {
namespace Layout {
inline constexpr float width = 560, radius = 6, header = 48, filter = 40, close = 24, closeGlyph = 14;
inline constexpr float sectionTop = 8, heading = 32, row = 32, rowRadius = 6, padX = 12, rowPad = 8, keyGap = 4, keyHeight = 24, keyPad = 6, keyMin = 24, bottom = 12;
inline constexpr float pageMargin = 24, enterMs = 160, slide = 8;
inline constexpr unsigned Surface = 0x17;   // --color-browser-canvas (Design::Surface::canvas = 23/255)
}
struct Shortcut { const wchar_t* label; std::vector<const wchar_t*> keys; };
struct Section { const wchar_t* title; std::vector<Shortcut> rows; };
// The shortcuts main's Key() actually handles (src/main.cpp), in Paper's sections. One list per language,
// built on first use in that language (labels and key names are catalog text); the panel keeps pointers
// into it, so each list lives as long as the process (UI thread only).
inline std::vector<Section> BuildSections() {
    const wchar_t* shift = TR(key_shift);
    return {
        {TR(settings_tabs), {
            {TR(tab_new), {L"Ctrl", L"T"}}, {TR(cmd_reopen_tab), {L"Ctrl", shift, L"T"}}, {TR(cmd_close_tab), {L"Ctrl", L"W"}},
            {TR(shortcut_next_tab), {L"Ctrl", L"Tab"}}, {TR(shortcut_previous_tab), {L"Ctrl", shift, L"Tab"}}, {TR(shortcut_go_to_tab), {L"Ctrl", L"1…9"}},
            {TR(shortcut_move_tab_left), {L"Ctrl", shift, TR(key_page_up)}}, {TR(shortcut_move_tab_right), {L"Ctrl", shift, TR(key_page_down)}}}},
        {TR(a11y_toolbar), {
            {TR(shortcut_go_to_address), {L"Ctrl", L"L"}}, {TR(shortcut_go_to_address), {L"Alt", L"D"}}, {TR(shortcut_content_to_chrome), {L"F6"}},
            {TR(shortcut_reload), {L"Ctrl", L"R"}}, {TR(shortcut_reload), {L"F5"}}, {TR(a11y_back), {L"Alt", L"←"}}, {TR(a11y_forward), {L"Alt", L"→"}}}},
        {TR(shortcut_section_page), {
            {TR(find_title), {L"Ctrl", L"F"}}, {TR(find_next), {L"F3"}}, {TR(find_previous), {shift, L"F3"}},
            {TR(shortcut_print), {L"Ctrl", L"P"}}, {TR(fullscreen_title), {L"F11"}}}},
        {TR(shortcut_section_bookmarks), {
            {TR(a11y_add_bookmark), {L"Ctrl", L"D"}}, {TR(bookmarks_bar_show), {L"Ctrl", shift, L"B"}}, {TR(shortcut_open_bookmarks), {L"Ctrl", shift, L"O"}},
            {TR(shortcut_undo_bookmark), {L"Ctrl", L"Z"}}, {TR(history_title), {L"Ctrl", L"H"}}}},
        {TR(shortcut_section_help), {{TR(shortcuts_title), {L"Ctrl", L"/"}}, {TR(shortcut_sandbox), {L"Ctrl", shift, L"F12"}}}},
    };
}
inline const std::vector<Section>& Sections() {
    static std::vector<Section> lists[I18n::LocaleCount];
    auto& list = lists[static_cast<int>(I18n::Current())];
    if (list.empty()) list = BuildSections();
    return list;
}
// Accent- and case-insensitive containment (« echap » finds « Échap »).
inline bool Matches(const std::wstring& text, const std::wstring& query) {
    if (query.empty()) return true;
    return FindNLSStringEx(LOCALE_NAME_USER_DEFAULT, FIND_FROMSTART | LINGUISTIC_IGNORECASE | LINGUISTIC_IGNOREDIACRITIC, text.c_str(), static_cast<int>(text.size()), query.c_str(), static_cast<int>(query.size()), nullptr, nullptr, nullptr, 0) >= 0;
}
inline bool RowMatches(const Shortcut& s, const std::wstring& query) {
    std::wstring keys; for (auto* k : s.keys) keys += std::wstring(keys.empty() ? L"" : L"+") + k;
    return Matches(s.label, query) || Matches(keys, query);
}

class Panel {
    struct Line { bool heading; const wchar_t* text; const Shortcut* shortcut; float top; };
    enum Hit { None = 0, CloseHit = 1 };
    HWND hwnd_ = nullptr, owner_ = nullptr, edit_ = nullptr, restore_ = nullptr; Chrome chrome_; Chrome* surface_ = &chrome_; Motion::State motion_; PopupKit::Pump pump_; PopupKit::GlyphCache glyphs_; Accessibility::Bridge uia_;
    std::function<void()> closed_; RECT page_{}; bool visible_ = false, reduced_ = false, keyboard_ = false, closeFocus_ = false;
    std::vector<Line> lines_; float content_ = 0, scroll_ = 0, height_ = 0; int hover_ = 0, hoverRow_ = -1, pressed_ = 0;
    Motion::Tween enter_; HFONT font_ = nullptr; HBRUSH fieldBrush_ = nullptr;
    int Px(float dip) const { return PopupKit::Px(owner_, dip); }
    float ListTop() const { return Layout::header + Layout::filter; }
    float ListHeight() const { return height_ - ListTop() - 1; }
    D2D1_RECT_F CloseBox() const { const float r = Layout::width - 1 - Layout::padX; return {r - Layout::close, 12, r, 36}; }
    D2D1_RECT_F Field() const { return {1 + Layout::padX, Layout::header, Layout::width - 1 - Layout::padX, Layout::header + 32}; }
    static COLORREF FieldGray() { const int g = static_cast<int>(std::lround(Layout::Surface + (255 - static_cast<int>(Layout::Surface)) * (0x0A / 255.f))); return RGB(g, g, g); }   // #FFFFFF0A over the panel surface
    static COLORREF Ink(float a) { const int b = GetRValue(FieldGray()); const int c = static_cast<int>(std::lround(b + (255 - b) * a)); return RGB(c, c, c); }
    std::wstring Query() const { if (!edit_) return L""; std::wstring s(static_cast<size_t>(GetWindowTextLengthW(edit_)) + 1, L'\0'); s.resize(static_cast<size_t>(GetWindowTextW(edit_, s.data(), static_cast<int>(s.size())))); return s; }
    void Build() {
        lines_.clear(); const auto q = Query(); float y = 0;
        for (auto& sec : Sections()) {
            std::vector<const Shortcut*> rows; for (auto& r : sec.rows) if (RowMatches(r, q) || Matches(sec.title, q)) rows.push_back(&r);
            if (rows.empty()) continue;
            y += Layout::sectionTop; lines_.push_back({true, sec.title, nullptr, y}); y += Layout::heading;
            for (auto* r : rows) { lines_.push_back({false, r->label, r, y}); y += Layout::row; }
        }
        if (lines_.empty()) { y += Layout::sectionTop; lines_.push_back({true, TR(shortcuts_no_match), nullptr, y}); y += Layout::heading; }
        content_ = y + Layout::bottom; scroll_ = std::clamp(scroll_, 0.f, std::max(0.f, content_ - ListHeight()));
    }
    float NaturalHeight() const { float y = 0; for (auto& sec : Sections()) y += Layout::sectionTop + Layout::heading + Layout::row * sec.rows.size(); return 1 + ListTop() + y + Layout::bottom; }
    void Paint() {
        Chrome& chrome_ = *surface_; // offscreen in RenderForTest
        using namespace Layout;
        if (!chrome_.RenderTarget()) chrome_.Resize();
        auto* t = chrome_.RenderTarget(); if (!t || !chrome_.SolidBrush()) return;
        const auto now = Motion::Now(); chrome_.keyboardModality = keyboard_;
        t->BeginDraw(); t->Clear(PopupKit::Gray(0x17));
        PopupKit::Fill(chrome_, {0, 0, width, height_}, radius, PopupKit::Gray(Surface));
        PopupKit::DrawText(chrome_, TR(shortcuts_title), {1 + 16, 13, width - 60, 33}, {16, 20, 0, true}, PopupKit::White(.9f));
        const auto cb = CloseBox(); const auto cv = motion_.Visual(CloseHit, now); const float lit = std::max(cv.hover, cv.press);
        if (lit > 0) PopupKit::Fill(chrome_, cb, 3, PopupKit::Gray(cv.press > cv.hover ? 0x3E : 0x33, lit));
        glyphs_.Draw(chrome_, PopupKit::Glyphs::close, cb, closeGlyph, PopupKit::White(0xB3 / 255.f));
        if (closeFocus_ && GetFocus() == hwnd_) PopupKit::InsetRing(chrome_, cb, 3);
        const auto f = Field();
        PopupKit::Fill(chrome_, f, 6, PopupKit::White(0x0A / 255.f));
        { auto* b = chrome_.SolidBrush(); b->SetColor(PopupKit::White(0x1A / 255.f)); t->DrawRoundedRectangle(D2D1::RoundedRect({f.left + .5f, f.top + .5f, f.right - .5f, f.bottom - .5f}, 5.5f, 5.5f), b, 1); }
        if (GetFocus() == edit_ && keyboard_) PopupKit::InsetRing(chrome_, f, 6);
        glyphs_.Draw(chrome_, PopupKit::Glyphs::search, {f.left + 8, f.top, f.left + 22, f.bottom}, 14, PopupKit::White(0xB3 / 255.f));
        t->PushAxisAlignedClip({0, ListTop(), width, height_ - 1}, D2D1_ANTIALIAS_MODE_ALIASED);
        for (size_t i = 0; i < lines_.size(); ++i) {
            const auto& l = lines_[i]; const float y = ListTop() + l.top - scroll_; if (y > height_ || y + row < ListTop()) continue;
            if (l.heading) { PopupKit::DrawText(chrome_, l.text, {1 + padX + 8, y + 8, width - padX, y + 28}, {14, 20}, PopupKit::White(.5f)); continue; }
            const D2D1_RECT_F r{1 + padX, y, width - 1 - padX, y + row};
            if (static_cast<int>(i) == hoverRow_) PopupKit::Fill(chrome_, r, rowRadius, PopupKit::Gray(0x33));
            float x = r.right - rowPad; const PopupKit::Type cap{13, 16, 0, true};
            for (auto it = l.shortcut->keys.rbegin(); it != l.shortcut->keys.rend(); ++it) {
                const float w = std::max(keyMin, std::ceil(PopupKit::Measure(chrome_, *it, 8192, cap, false).width) + 2 * keyPad);
                const D2D1_RECT_F k{x - w, y + 4, x, y + 28}; PopupKit::Fill(chrome_, k, 3, PopupKit::White(0x14 / 255.f));
                const float tw = PopupKit::Measure(chrome_, *it, 8192, cap, false).width;
                PopupKit::DrawText(chrome_, *it, {(k.left + k.right - tw) / 2, y + 8, (k.left + k.right + tw) / 2 + 1, y + 24}, cap, PopupKit::White(0xCC / 255.f));
                x = k.left - keyGap;
            }
            PopupKit::DrawText(chrome_, l.text, {r.left + rowPad, y + 6, x - 8, y + 26}, {14, 20}, PopupKit::White(.9f));
        }
        t->PopAxisAlignedClip();
        PopupKit::Outline(chrome_, width, height_, radius, PopupKit::White(0x0F / 255.f));
        if (t->EndDraw() == D2DERR_RECREATE_TARGET) chrome_.RecreateTarget();
    }
    POINT Rest() const { return {(page_.left + page_.right - Px(Layout::width)) / 2, page_.top + Px(Layout::pageMargin)}; }
    void Place(float progress) {
        const POINT p = Rest(); SetWindowPos(hwnd_, HWND_TOP, p.x, p.y - Px(reduced_ ? 0 : Layout::slide * (1 - progress)), Px(Layout::width), Px(height_), SWP_NOACTIVATE);
        PopupKit::SetAlpha(hwnd_, progress);
    }
    void PlaceEdit() { const auto f = Field(); SetWindowPos(edit_, nullptr, Px(f.left + 30), Px(f.top + 7), Px(f.right - f.left - 38), Px(18), SWP_NOZORDER | SWP_NOACTIVATE); }
    void Repaint() { if (visible_) { Paint(); ValidateRect(hwnd_, nullptr); } }
    void Wake() { pump_.Request(NextWake(Motion::Now())); }
    void Scroll(float dy) { const float before = scroll_; scroll_ = std::clamp(scroll_ + dy, 0.f, std::max(0.f, content_ - ListHeight())); if (scroll_ != before) Repaint(); }
    void Close() { const bool was = visible_; Hide(); if (was && closed_) closed_(); }
    int RowAt(float x, float y) const { if (y < ListTop() || x < 1 + Layout::padX || x > Layout::width - 1 - Layout::padX) return -1; for (size_t i = 0; i < lines_.size(); ++i) { const float top = ListTop() + lines_[i].top - scroll_; if (!lines_[i].heading && y >= top && y < top + Layout::row) return static_cast<int>(i); } return -1; }
    bool Key(WPARAM key) {
        if (key == VK_ESCAPE) { Close(); return true; }
        if (key == VK_TAB) { keyboard_ = true; if (GetFocus() == edit_) { closeFocus_ = true; SetFocus(hwnd_); } else { closeFocus_ = false; SetFocus(edit_); } Repaint(); return true; }
        if ((key == VK_RETURN || key == VK_SPACE) && GetFocus() == hwnd_ && closeFocus_) { Close(); return true; }
        if (key == VK_DOWN) { Scroll(Layout::row); return true; } if (key == VK_UP) { Scroll(-Layout::row); return true; }
        if (key == VK_NEXT) { Scroll(ListHeight() - Layout::row); return true; } if (key == VK_PRIOR) { Scroll(-(ListHeight() - Layout::row)); return true; }
        return false;
    }
    static LRESULT CALLBACK EditProc(HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
        auto* self = reinterpret_cast<Panel*>(data);
        switch (msg) {
        case WM_GETDLGCODE: return DefSubclassProc(h, msg, w, l) | DLGC_WANTALLKEYS;
        case WM_KEYDOWN: if (self->Key(w)) return 0; break;
        case WM_CHAR: if (w == L'\t' || w == L'\r' || w == 27) return 0; break;
        case WM_MOUSEWHEEL: SendMessageW(self->hwnd_, msg, w, l); return 0;
        case WM_PAINT: {
            const LRESULT r = DefSubclassProc(h, msg, w, l);
            if (GetWindowTextLengthW(h) == 0) { HDC dc = GetDC(h); RECT rc{}; GetClientRect(h, &rc); HGDIOBJ old = SelectObject(dc, self->font_); SetBkMode(dc, TRANSPARENT); SetTextColor(dc, Ink(.4f)); DrawTextW(dc, TR(shortcuts_filter), -1, &rc, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS); SelectObject(dc, old); ReleaseDC(h, dc); }
            return r;
        }
        }
        return DefSubclassProc(h, msg, w, l);
    }
    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<Panel*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (msg == WM_NCCREATE) { self = static_cast<Panel*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); self->hwnd_ = h; }
        return self ? self->Handle(h, msg, w, l) : DefWindowProcW(h, msg, w, l);
    }
    LRESULT Handle(HWND h, UINT msg, WPARAM w, LPARAM l) {
        const float s = 96.f / PopupKit::Dpi(h);
        switch (msg) {
        case WM_GETOBJECT: { LRESULT r; if (uia_.OnGetObject(h, w, l, r)) return r; break; }
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); Paint(); EndPaint(h, &ps); return 0; }
        case WM_CTLCOLOREDIT: { HDC dc = reinterpret_cast<HDC>(w); SetTextColor(dc, Ink(.9f)); SetBkColor(dc, FieldGray()); return reinterpret_cast<LRESULT>(fieldBrush_); }
        case WM_COMMAND: if (reinterpret_cast<HWND>(l) == edit_ && HIWORD(w) == EN_CHANGE) { scroll_ = 0; Build(); Repaint(); } return 0;
        case WM_MOUSEACTIVATE: return MA_ACTIVATE;
        case WM_ACTIVATE: if (LOWORD(w) == WA_INACTIVE && visible_) PostMessageW(h, WM_APP + 2, 0, 0); return 0;
        case WM_APP + 2: if (visible_ && GetForegroundWindow() != h) Close(); return 0;  // a click elsewhere
        case WM_TIMER: if (w == PopupKit::Pump::TimerId) { KillTimer(h, w); const auto now = Motion::Now(); Frame(now); pump_.Request(NextWake(now)); } return 0;
        case WM_MOUSEWHEEL: Scroll(-GET_WHEEL_DELTA_WPARAM(w) / 120.f * Layout::row * 3); return 0;
        case WM_MOUSEMOVE: { TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, h, 0}; TrackMouseEvent(&t); const float x = GET_X_LPARAM(l) * s, y = GET_Y_LPARAM(l) * s;
            const int hit = PopupKit::Contains(CloseBox(), x, y) ? CloseHit : None, row = RowAt(x, y);
            if (hit != hover_ || row != hoverRow_) { hover_ = hit; hoverRow_ = row; motion_.SetInteraction(hover_, pressed_ == hover_ ? pressed_ : 0, Motion::Now()); Repaint(); Wake(); } return 0; }
        case WM_MOUSELEAVE: hover_ = 0; hoverRow_ = -1; motion_.SetInteraction(0, 0, Motion::Now()); Repaint(); Wake(); return 0;
        case WM_LBUTTONDOWN: { keyboard_ = false; const float x = GET_X_LPARAM(l) * s, y = GET_Y_LPARAM(l) * s; pressed_ = PopupKit::Contains(CloseBox(), x, y) ? CloseHit : None;
            if (pressed_) { SetCapture(h); motion_.SetInteraction(hover_, pressed_, Motion::Now()); Repaint(); Wake(); } else if (PopupKit::Contains(Field(), x, y)) SetFocus(edit_); return 0; }
        case WM_LBUTTONUP: { const float x = GET_X_LPARAM(l) * s, y = GET_Y_LPARAM(l) * s; const int was = pressed_; pressed_ = 0; if (GetCapture() == h) ReleaseCapture(); motion_.SetInteraction(hover_, 0, Motion::Now()); Repaint(); Wake(); if (was == CloseHit && PopupKit::Contains(CloseBox(), x, y)) Close(); return 0; }
        case WM_KEYDOWN: if (Key(w)) return 0; break;
        case WM_SETFOCUS: closeFocus_ = true; Repaint(); return 0;
        case WM_KILLFOCUS: Repaint(); return 0;
        case WM_APP + 1: Close(); return 0;
        case WM_DPICHANGED: if (visible_) Show(page_); return 0;
        case WM_NCDESTROY: uia_.Detach(); hwnd_ = edit_ = nullptr; visible_ = false; break;
        }
        return DefWindowProcW(h, msg, w, l);
    }
public:
    ~Panel() { Dispose(); }
    bool Create(HINSTANCE instance, HWND owner, const std::wstring& root, std::function<void()> wake = nullptr) {
        owner_ = owner; motion_.EnablePolish(); chrome_.SetMotion(&motion_);
        PopupKit::Register(instance, L"LisiereShortcutsPanel", Proc);
        if (!PopupKit::CreatePopup(instance, owner, L"LisiereShortcutsPanel", TR(shortcuts_title), 0, this, Layout::radius, WS_POPUP | WS_CLIPCHILDREN)) return false;
        pump_.Bind(hwnd_, std::move(wake));
        edit_ = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 1, 1, hwnd_, nullptr, instance, nullptr); NativeUI::SelectAllOnCtrlA(edit_);
        if (!edit_) return false;
        SendMessageW(edit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0); SendMessageW(edit_, EM_SETLIMITTEXT, 128, 0);
        SetWindowSubclass(edit_, EditProc, 0xC5, reinterpret_cast<DWORD_PTR>(this));
        fieldBrush_ = CreateSolidBrush(FieldGray());
        Accessibility::Host host; host.window = hwnd_;
        host.nodes = [this] {
            std::vector<Accessibility::Node> nodes; if (!visible_) return nodes;
            Accessibility::Node g; g.key = 100; g.role = Accessibility::Role::List; g.name = TR(shortcuts_title); g.automationId = L"ShortcutsPanel"; g.focusable = false; GetWindowRect(hwnd_, &g.bounds); nodes.push_back(g);
            Accessibility::Node c; c.key = CloseHit; c.parent = 100; c.name = TR(common_close); c.accelerator = TR(key_escape); c.automationId = L"ShortcutsClose"; const auto b = CloseBox(); RECT r{Px(b.left), Px(b.top), Px(b.right), Px(b.bottom)}; MapWindowPoints(hwnd_, nullptr, reinterpret_cast<POINT*>(&r), 2); c.bounds = r; nodes.push_back(c);
            long long key = 1000; int pos = 0, count = 0; for (auto& l : lines_) if (!l.heading) ++count;
            for (auto& l : lines_) { if (l.heading) continue; Accessibility::Node n; n.key = ++key; n.parent = 100; n.role = Accessibility::Role::ListItem; n.name = l.text; std::wstring k; for (auto* p : l.shortcut->keys) k += std::wstring(k.empty() ? L"" : L"+") + p; n.accelerator = k; n.focusable = false; n.position = ++pos; n.count = count; GetWindowRect(hwnd_, &n.bounds); n.offscreen = true; nodes.push_back(n); }
            return nodes; };
        host.focused = [this]() -> long long { return GetFocus() == hwnd_ && closeFocus_ ? CloseHit : 0; };
        host.invoke = [this](long long key) { if (key == CloseHit) PostMessageW(hwnd_, WM_APP + 1, 0, 0); };
        host.label = [this](HWND child, Accessibility::Node& n) { if (child != edit_) return false; n.name = TR(shortcuts_filter); n.automationId = L"ShortcutsFilter"; return true; };
        uia_.Attach(std::move(host));
        return SUCCEEDED(chrome_.Init(hwnd_, root));
    }
    void Dispose() { pump_.Stop(); uia_.Detach(); chrome_.Dispose(); glyphs_.Clear(); if (hwnd_ && IsWindow(hwnd_)) DestroyWindow(hwnd_); if (font_) DeleteObject(font_); if (fieldBrush_) DeleteObject(fieldBrush_); font_ = nullptr; fieldBrush_ = nullptr; hwnd_ = edit_ = nullptr; visible_ = false; }
    // Called after Échap / ✕ / click elsewhere closed the panel (not after Hide()).
    void SetClosed(std::function<void()> closed) { closed_ = std::move(closed); }
    void Show(RECT pageScreen) {
        if (!hwnd_) return;
        const bool was = visible_; page_ = pageScreen;
        if (!was) { HWND f = GetFocus(); restore_ = f != edit_ && f != hwnd_ ? f : nullptr; SetWindowTextW(edit_, L""); scroll_ = 0; }
        if (font_) DeleteObject(font_);
        font_ = CreateFontW(-Px(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Inter Display");
        SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), FALSE);
        const float room = static_cast<float>(pageScreen.bottom - pageScreen.top) * 96.f / PopupKit::Dpi(owner_) - 2 * Layout::pageMargin;
        height_ = std::max(ListTop() + 2 * Layout::row, std::min(NaturalHeight(), room));
        visible_ = true; Build();
        if (!was) { enter_.Snap(reduced_ ? 1.f : 0.f); enter_.Set(1, Motion::Now(), Motion::Milliseconds(static_cast<int>(Layout::enterMs)), false, Motion::Curve::EaseOut); }
        Place(was ? 1.f : enter_.Value(Motion::Now())); PlaceEdit(); chrome_.Resize();
        ShowWindow(hwnd_, SW_SHOW); Paint(); ValidateRect(hwnd_, nullptr);
        SetForegroundWindow(hwnd_); SetFocus(edit_); closeFocus_ = false;
        if (uia_.Listening()) { uia_.ChildrenChanged(100); uia_.Raise(100, UIA_Window_WindowOpenedEventId); }
        Wake();
    }
    void Hide() {
        if (!hwnd_ || !visible_) return;
        const bool had = GetFocus() == edit_ || GetFocus() == hwnd_; visible_ = false; ShowWindow(hwnd_, SW_HIDE);
        if (had) { if (restore_ && IsWindow(restore_)) SetFocus(restore_); else if (owner_) SetForegroundWindow(owner_); }
        restore_ = nullptr; pump_.Request({});
    }
    bool Visible() const { return visible_; }
    HWND Window() const { return hwnd_; }
    HWND FilterEdit() const { return edit_; }
    int VisibleRowCount() const { int n = 0; for (auto& l : lines_) if (!l.heading) ++n; return n; }
    void SetReducedMotion(bool reduced) { reduced_ = reduced; motion_.SetReducedMotion(reduced); if (reduced) enter_.Settle(); }
    Motion::Wake NextWake(Motion::Time now) const { auto w = motion_.NextWake(now); if (visible_ && enter_.Moving(now)) w = Motion::Earlier(w, Motion::Frame()); return w; }
    void Frame(Motion::Time now) { if (!visible_) return; Place(enter_.Value(now)); Paint(); ValidateRect(hwnd_, nullptr); }
    bool KeyForTest(WPARAM key) { return Key(key); }
    void PaintForTest() { Paint(); }
    bool RenderForTest(const std::wstring& root, const wchar_t* png, UINT dpi = 96) { return PopupKit::RenderPng(root, Layout::width, height_, dpi, png, surface_, [this] { Paint(); }); }
};
} // namespace ShortcutsPanel
} // namespace NativeChrome
