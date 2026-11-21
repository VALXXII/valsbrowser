#pragma once
#include "i18n.h"
// "Favori ajouté" / "Modifier le favori" popover (Paper "Favoris — flow", artboards 02,
// 07 and 12 as modified by the user on 2026-11-08): a 320 DIP owned popup, radius 8,
// 1 DIP #FFFFFF0F border, padding 16, gap 16; no title (the window's accessible name
// carries it); a 24x24 close button pinned 8/8 to the top-right corner.
//  - Nom: native EDIT, text selected and focused at opening.
//  - Adresse (editing a link): native EDIT. Validated at "Terminé"; once an error is
//    shown it is re-checked on every keystroke. While invalid the text, the caret and a
//    1 DIP inset outline turn danger (#FF7B6E, 90%/50%) and a neutral help line names the
//    fix. Valid again: white text, accent caret, focus outline, no message.
//  - Dossier: a row that opens the folder list (native menu, accessible).
//  - Supprimer (danger text, left) and Terminé (white fill, right).
// The bookmark already exists when the popover opens (Ctrl+D saves at once): Escape and
// the close button keep it and drop only the popover's edits; Enter, Terminé and a
// click elsewhere apply them. Entrance 180 ms fade + 6 DIP slide from the anchor;
// reduced motion: immediate.
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <commctrl.h>
#include <algorithm>
#include <tuple>
#include <functional>
#include <string>
#include <vector>
#include "chrome.h"
#include "native-ui.h"
#include "accessibility.h"
#include "context-menu.h"

namespace NativeChrome {
namespace BookmarkPopover {
namespace Layout {
inline constexpr float width = 320, padding = 16, gap = 16, radius = NativeUI::nativeCornerRadius;
inline constexpr float label = 18, labelGap = 6, field = 32, fieldPad = 10, help = 36, helpGap = 6;
inline constexpr float actionsTop = 4, button = 32, close = 24, closeInset = 8;
inline constexpr float caretHeight = 18, enterMs = 180, enterSlide = 6;
inline float FieldBlock(bool error) { return label + labelGap + field + (error ? helpGap + help : 0); }
inline float Height(bool address, bool folder, bool error) {
    float h = padding + FieldBlock(false);
    if (address) h += gap + FieldBlock(error);
    if (folder) h += gap + FieldBlock(false);
    return h + gap + actionsTop + button + padding;
}
}
// What the host shows and receives. `folders`: (id, depth, name) in tree order.
struct Request {
    unsigned id = 0; bool folder = false, added = false;
    std::wstring title, url;
    unsigned parent = 0;
    std::vector<std::tuple<unsigned, int, std::wstring>> folders;
};
struct Hooks {
    std::function<void(unsigned id, const std::wstring& title, const std::wstring& url, unsigned parent)> commit; // url already normalized
    std::function<void(unsigned id)> remove;
    std::function<unsigned(unsigned parent)> newFolder; // creates "Nouveau dossier" in parent, returns it (0 = refused)
    std::function<std::vector<std::tuple<unsigned, int, std::wstring>>(unsigned id)> folders; // refreshed list (excluding id's subtree)
    std::function<int(const std::vector<ContextMenu::Item>&, RECT, bool)> choose; // native menu under a screen rect
    std::function<void(bool keyboard)> closed;
    std::function<void(const std::wstring&)> announce;
};

class Popover {
    enum Hit { None = 0, CloseHit = 1, FolderHit = 2, DeleteHit = 3, DoneHit = 4 };
    HWND hwnd_ = nullptr, owner_ = nullptr, name_ = nullptr, url_ = nullptr; HINSTANCE instance_ = nullptr;
    Chrome chrome_; Motion::State motion_; Hooks hooks_; Request request_;
    HFONT font_ = nullptr; HBRUSH fieldBrush_ = nullptr;
    NativeUI::AccentCaret caret_;
    Accessibility::Bridge uia_;
    unsigned parent_ = 0; std::wstring parentName_;
    bool error_ = false, shownError_ = false, visible_ = false, keyboard_ = false, committing_ = false, entering_ = false;
    int hover_ = 0, pressed_ = 0, focus_ = 0; // focus_: drawn control holding the keyboard (0 = an EDIT)
    POINT final_{}; Motion::Time opened_{};
    static constexpr UINT_PTR FrameTimer = 1;

    float Dpi() const { return static_cast<float>(GetDpiForWindow(hwnd_ ? hwnd_ : owner_)); }
    int Px(float dip) const { return static_cast<int>(std::lround(dip * Dpi() / 96.f)); }
    bool HasAddress() const { return !request_.folder; }
    bool HasFolder() const { return true; }
    float Height() const { return Layout::Height(HasAddress(), HasFolder(), shownError_); }
    // Vertical layout (DIP, client).
    float NameField() const { return Layout::padding + Layout::label + Layout::labelGap; }
    float UrlField() const { return Layout::padding + Layout::FieldBlock(false) + Layout::gap + Layout::label + Layout::labelGap; }
    float FolderField() const { return HasAddress() ? UrlField() + Layout::field + (shownError_ ? Layout::helpGap + Layout::help : 0) + Layout::gap + Layout::label + Layout::labelGap : NameField() + Layout::field + Layout::gap + Layout::label + Layout::labelGap; }
    float ActionsTop() const { return FolderField() + Layout::field + Layout::gap + Layout::actionsTop; }
    D2D1_RECT_F Field(float top) const { return {Layout::padding, top, Layout::width - Layout::padding, top + Layout::field}; }
    D2D1_RECT_F CloseBox() const { return {Layout::width - Layout::closeInset - Layout::close, Layout::closeInset, Layout::width - Layout::closeInset, Layout::closeInset + Layout::close}; }
    D2D1_RECT_F DeleteBox() { const float w = chrome_.TextWidth(TR(common_delete), 14, true, false, -.28f) + 20; const float t = ActionsTop(); return {Layout::padding, t, Layout::padding + w, t + Layout::button}; }
    D2D1_RECT_F DoneBox() { const float w = chrome_.TextWidth(TR(common_done), 14, true, false, -.28f) + 28; const float t = ActionsTop(); return {Layout::width - Layout::padding - w, t, Layout::width - Layout::padding, t + Layout::button}; }
    static bool Inside(D2D1_RECT_F r, float x, float y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }
    int HitAt(float x, float y) {
        if (Inside(CloseBox(), x, y)) return CloseHit;
        if (Inside(Field(FolderField()), x, y)) return FolderHit;
        if (Inside(DeleteBox(), x, y)) return DeleteHit;
        if (Inside(DoneBox(), x, y)) return DoneHit;
        return None;
    }
    std::wstring Text(HWND edit) const { const int n = GetWindowTextLengthW(edit); std::wstring s(n + 1, 0); GetWindowTextW(edit, s.data(), n + 1); s.resize(n); return s; }
    // Validity of what the Adresse field holds now (empty result = invalid).
    std::wstring NormalizedUrl() const { return HasAddress() ? AddressBar::Normalize(Text(url_)) : std::wstring(); }
    COLORREF FieldGray() const { return Design::NativeGray(Design::Surface::raised); }
    COLORREF CaretColor(bool danger) const {
        const auto c = danger ? Design::DangerRgb() : Design::Accent::Get();
        return NativeUI::AccentCaret::Mix(FieldGray(), RGB(std::lround(c.r * 255), std::lround(c.g * 255), std::lround(c.b * 255)), Design::Accent::caretOpacity);
    }
    void RefreshCaret() {
        HWND f = GetFocus(); if (f != name_ && f != url_) return;
        caret_.Apply(f, FieldGray(), CaretColor(f == url_ && error_ && shownError_), std::max(1, Px(1)), Px(Layout::caretHeight));
    }
    void PlaceEdits() {
        if (!hwnd_) return;
        auto place = [&](HWND edit, float top) {
            if (!edit) return;
            HDC dc = GetDC(edit); auto old = SelectObject(dc, font_); TEXTMETRICW m{}; GetTextMetricsW(dc, &m); SelectObject(dc, old); ReleaseDC(edit, dc);
            const int h = m.tmHeight + m.tmExternalLeading; const int y = Px(top) + (Px(Layout::field) - h) / 2;
            MoveWindow(edit, Px(Layout::padding + Layout::fieldPad), y, Px(Layout::width - 2 * Layout::padding - 2 * Layout::fieldPad), h, TRUE);
        };
        place(name_, NameField());
        if (url_) { ShowWindow(url_, HasAddress() ? SW_SHOW : SW_HIDE); if (HasAddress()) place(url_, UrlField()); }
    }
    void Resize() {
        RECT r{}; GetWindowRect(hwnd_, &r);
        const int w = Px(Layout::width), h = Px(Height());
        SetWindowPos(hwnd_, nullptr, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        chrome_.Resize(); PlaceEdits(); InvalidateRect(hwnd_, nullptr, FALSE);
        if (uia_.Listening()) uia_.ChildrenChanged(GroupKey);
    }
    void SetError(bool error) {
        error_ = error;
        const bool show = error && (shownError_ || committing_);
        if (show != shownError_) { shownError_ = show; Resize(); }
        InvalidateRect(url_, nullptr, TRUE); InvalidateRect(hwnd_, nullptr, FALSE); RefreshCaret();
    }
    void Validate() { if (shownError_) SetError(NormalizedUrl().empty()); }
    void Commit() {
        if (!visible_) return;
        std::wstring url;
        if (HasAddress()) {
            url = NormalizedUrl();
            if (url.empty()) {
                committing_ = true; SetError(true); committing_ = false;
                SetFocus(url_); SendMessageW(url_, EM_SETSEL, 0, -1);
                if (hooks_.announce) hooks_.announce(TR(bookmark_invalid_address_full));
                return;
            }
        }
        const unsigned id = request_.id; const std::wstring title = Text(name_); const unsigned parent = parent_;
        Close(false);
        if (hooks_.commit) hooks_.commit(id, title, url, parent);
    }
    void Close(bool keyboard) {
        if (!visible_) return;
        visible_ = false; KillTimer(hwnd_, FrameTimer);
        if (uia_.Listening()) uia_.Raise(GroupKey, UIA_Window_WindowClosedEventId);
        ShowWindow(hwnd_, SW_HIDE);
        if (hooks_.closed) hooks_.closed(keyboard);
    }
    void ChooseFolder(bool keyboard) {
        if (!hooks_.choose) return;
        choosing_ = true;
        auto folders = hooks_.folders ? hooks_.folders(request_.id) : request_.folders;
        std::vector<ContextMenu::Item> items;
        for (size_t i = 0; i < folders.size(); ++i) {
            const auto& [id, depth, name] = folders[i];
            auto item = ContextMenu::Command(static_cast<int>(i) + 1, std::wstring(static_cast<size_t>(std::max(0, depth)) * 2, L' ') + NativeUI::EscapeMenuText(name), L"", true, id == parent_);
            item.icon = L"folder-closed"; items.push_back(item);
        }
        items.push_back(ContextMenu::Separator());
        auto add = ContextMenu::Command(100000, TR(bookmark_new_folder_menu)); add.icon = L"folder-plus"; items.push_back(add);
        RECT r{}; const auto f = Field(FolderField()); r = {Px(f.left), Px(f.top), Px(f.right), Px(f.bottom)}; MapWindowPoints(hwnd_, nullptr, reinterpret_cast<POINT*>(&r), 2);
        const int chosen = hooks_.choose(items, r, keyboard);
        choosing_ = false;
        if (chosen == 100000 && hooks_.newFolder) {
            if (const unsigned id = hooks_.newFolder(parent_)) { parent_ = id; parentName_ = TR(bookmark_new_folder_name); if (hooks_.announce) hooks_.announce(TRF(bookmark_folder_created, TR(bookmark_new_folder_name))); }
        } else if (chosen > 0 && chosen <= static_cast<int>(folders.size())) {
            parent_ = std::get<0>(folders[chosen - 1]); parentName_ = std::get<2>(folders[chosen - 1]);
        }
        for (const auto& f2 : (hooks_.folders ? hooks_.folders(request_.id) : folders)) if (std::get<0>(f2) == parent_) parentName_ = std::get<2>(f2);
        if (IsWindowVisible(hwnd_)) { SetForegroundWindow(hwnd_); SetFocus(keyboard ? hwnd_ : name_); focus_ = keyboard ? FolderHit : 0; }
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    void Activate(int hit, bool keyboard) {
        switch (hit) {
        case CloseHit: Close(keyboard); break;
        case FolderHit: ChooseFolder(keyboard); break;
        case DeleteHit: { const unsigned id = request_.id; Close(keyboard); if (hooks_.remove) hooks_.remove(id); break; }
        case DoneHit: Commit(); break;
        default: break;
        }
    }
    // Tab order: Nom, Adresse, Dossier, Supprimer, Terminé, close.
    std::vector<int> Order() const { std::vector<int> o{-1}; if (HasAddress()) o.push_back(-2); o.push_back(FolderHit); o.push_back(DeleteHit); o.push_back(DoneHit); o.push_back(CloseHit); return o; }
    int Current() const { HWND f = GetFocus(); return f == name_ ? -1 : f == url_ ? -2 : focus_; }
    void FocusStop(int stop) {
        keyboard_ = true;
        if (stop == -1) { focus_ = 0; SetFocus(name_); SendMessageW(name_, EM_SETSEL, 0, -1); }
        else if (stop == -2) { focus_ = 0; SetFocus(url_); SendMessageW(url_, EM_SETSEL, 0, -1); }
        else { focus_ = stop; SetFocus(hwnd_); }
        InvalidateRect(hwnd_, nullptr, FALSE); if (uia_.Listening()) uia_.Raise(focus_ ? focus_ : 0, UIA_AutomationFocusChangedEventId);
    }
    void Cycle(bool back) {
        const auto o = Order(); const int at = Current(); auto it = std::find(o.begin(), o.end(), at);
        int i = it == o.end() ? 0 : static_cast<int>(it - o.begin()) + (back ? -1 : 1);
        i = (i + static_cast<int>(o.size())) % static_cast<int>(o.size()); FocusStop(o[i]);
    }
    // Keys shared by the EDITs and the drawn controls. True when handled.
    bool Key(WPARAM key) {
        if (key == VK_ESCAPE) { Close(true); return true; }
        if (key == VK_TAB) { Cycle(GetKeyState(VK_SHIFT) < 0); return true; }
        if (key == VK_RETURN) { if (focus_ && GetFocus() == hwnd_) Activate(focus_, true); else Commit(); return true; }
        if (key == VK_SPACE && focus_ && GetFocus() == hwnd_) { Activate(focus_, true); return true; }
        if ((key == VK_DOWN || key == VK_F4) && focus_ == FolderHit && GetFocus() == hwnd_) { ChooseFolder(true); return true; }
        return false;
    }
    static LRESULT CALLBACK EditProc(HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
        auto* self = reinterpret_cast<Popover*>(data);
        switch (msg) {
        case WM_GETDLGCODE: return DefSubclassProc(h, msg, w, l) | DLGC_WANTALLKEYS;
        case WM_KEYDOWN: if (self->Key(w)) return 0; break;
        case WM_CHAR: if (w == L'\t' || w == L'\r' || w == 27) return 0; break;
        case WM_SETFOCUS: { const LRESULT r = DefSubclassProc(h, msg, w, l); self->focus_ = 0; self->RefreshCaret(); InvalidateRect(self->hwnd_, nullptr, FALSE); return r; }
        case WM_KILLFOCUS: InvalidateRect(self->hwnd_, nullptr, FALSE); break;
        case WM_LBUTTONDOWN: self->keyboard_ = false; break;
        }
        return DefSubclassProc(h, msg, w, l);
    }
    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<Popover*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (msg == WM_NCCREATE) { self = static_cast<Popover*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); self->hwnd_ = h; }
        return self ? self->Handle(h, msg, w, l) : DefWindowProcW(h, msg, w, l);
    }
    LRESULT Handle(HWND h, UINT msg, WPARAM w, LPARAM l) {
        const float scale = 96.f / Dpi();
        switch (msg) {
        case WM_GETOBJECT: { LRESULT result; if (uia_.OnGetObject(h, w, l, result)) return result; break; }
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); Paint(); EndPaint(h, &ps); NativeUI::EditBuffer::Flush(name_); NativeUI::EditBuffer::Flush(url_); return 0; }
        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(w); const bool danger = reinterpret_cast<HWND>(l) == url_ && error_ && shownError_;
            NativeUI::EditBuffer::Clip(reinterpret_cast<HWND>(l), dc);
            SetTextColor(dc, danger ? RGB(0xFF, 0x7B, 0x6E) : RGB(255, 255, 255)); SetBkColor(dc, FieldGray()); SetBkMode(dc, OPAQUE);
            return reinterpret_cast<LRESULT>(fieldBrush_);
        }
        case WM_COMMAND: if (reinterpret_cast<HWND>(l) == url_ && HIWORD(w) == EN_CHANGE) Validate(); if (HIWORD(w) == EN_SETFOCUS || HIWORD(w) == EN_KILLFOCUS) InvalidateRect(h, nullptr, FALSE); return 0;
        case WM_ACTIVATE:
            // A click elsewhere applies the edits (the menu it opens itself is not "elsewhere").
            if (LOWORD(w) == WA_INACTIVE && visible_ && !choosing_) { HWND to = reinterpret_cast<HWND>(l); if (!to || (GetWindow(to, GW_OWNER) != hwnd_)) PostMessageW(h, WM_APP + 1, 0, 0); }
            return 0;
        case WM_APP + 1: if (visible_ && GetForegroundWindow() != hwnd_ && !choosing_) { if (HasAddress() && NormalizedUrl().empty()) Close(false); else Commit(); } return 0;
        case WM_MOUSEACTIVATE: return MA_ACTIVATE;
        case WM_APP + 2: keyboard_ = true; Activate(static_cast<int>(w), true); return 0;
        case WM_TIMER: if (w == FrameTimer) Step(); return 0;
        case WM_MOUSEMOVE: { TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, h, 0}; TrackMouseEvent(&t); const int hit = HitAt(GET_X_LPARAM(l) * scale, GET_Y_LPARAM(l) * scale); if (hit != hover_) { hover_ = hit; Interact(); } return 0; }
        case WM_MOUSELEAVE: hover_ = 0; Interact(); return 0;
        case WM_LBUTTONDOWN: { keyboard_ = false; const int hit = HitAt(GET_X_LPARAM(l) * scale, GET_Y_LPARAM(l) * scale); pressed_ = hit; if (hit) SetCapture(h); Interact(); return 0; }
        case WM_LBUTTONUP: { const int hit = HitAt(GET_X_LPARAM(l) * scale, GET_Y_LPARAM(l) * scale); const int was = pressed_; pressed_ = 0; if (GetCapture() == h) ReleaseCapture(); Interact(); if (was && was == hit) Activate(hit, false); return 0; }
        case WM_KEYDOWN: if (Key(w)) return 0; break;
        case WM_SETFOCUS: InvalidateRect(h, nullptr, FALSE); return 0;
        case WM_DPICHANGED: if (visible_) Resize(); return 0;
        }
        return DefWindowProcW(h, msg, w, l);
    }
    bool choosing_ = false;
    void Interact() { motion_.SetInteraction(hover_, pressed_ == hover_ ? pressed_ : 0, Motion::Now()); InvalidateRect(hwnd_, nullptr, FALSE); if (motion_.NextWake(Motion::Now()).delay) SetTimer(hwnd_, FrameTimer, 10, nullptr); }
    void Step() {
        const auto now = Motion::Now();
        if (entering_) {
            const float u = Motion::Clamp(static_cast<float>(std::chrono::duration<double, std::milli>(now - opened_).count()) / Layout::enterMs);
            const float e = 1 - (1 - u) * (1 - u) * (1 - u);
            SetLayeredWindowAttributes(hwnd_, 0, static_cast<BYTE>(std::lround(255 * Motion::Clamp(e * 1.4f))), LWA_ALPHA);
            SetWindowPos(hwnd_, nullptr, final_.x, final_.y - Px(Layout::enterSlide * (1 - e)), 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            if (u >= 1) entering_ = false;
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        if (!entering_ && !motion_.NextWake(now).delay) KillTimer(hwnd_, FrameTimer);
    }
    void FieldFrame(D2D1_RECT_F box, HWND edit, bool danger) {
        chrome_.Round(box, Design::Radius::control, Design::Surface::raised);
        auto* t = chrome_.RenderTarget(); auto* b = chrome_.SolidBrush(); const float inset = .5f;
        const D2D1_RECT_F ring{box.left + inset, box.top + inset, box.right - inset, box.bottom - inset};
        if (danger) { b->SetColor(D2D1::ColorF(Design::Danger::r, Design::Danger::g, Design::Danger::b, .5f)); t->DrawRoundedRectangle(D2D1::RoundedRect(ring, Design::Radius::control, Design::Radius::control), b, 1); }
        else if (edit && GetFocus() == edit) { b->SetColor(D2D1::ColorF(1, 1, 1, Design::Focus::pointerOpacity)); t->DrawRoundedRectangle(D2D1::RoundedRect(ring, Design::Radius::control, Design::Radius::control), b, 1); }
    }
    void Label(const wchar_t* text, float fieldTop) { chrome_.Text(text, {Layout::padding, fieldTop - Layout::labelGap - Layout::label, Layout::width - 40, fieldTop - Layout::labelGap}, 14, Design::Text::secondary, false, true, -.28f); }
    void Paint() {
        if (!chrome_.RenderTarget()) chrome_.Resize();
        auto* t = chrome_.RenderTarget(); if (!t || !chrome_.SolidBrush()) return;
        const auto now = Motion::Now(); const float c = Design::Surface::canvas, h = Height();
        t->BeginDraw(); t->Clear(D2D1::ColorF(c, c, c, 1));
        chrome_.keyboardModality = keyboard_;
        Label(TR(bookmark_field_name), NameField()); FieldFrame(Field(NameField()), name_, false);
        if (HasAddress()) {
            const bool danger = error_ && shownError_;
            Label(TR(bookmark_field_address), UrlField()); FieldFrame(Field(UrlField()), url_, danger);
            if (danger) {
                const float y = UrlField() + Layout::field + Layout::helpGap;
                const D2D1_RECT_F icon{Layout::padding, y + 2, Layout::padding + 14, y + 16};
                chrome_.Icon(L"danger", icon, 14, 1); chrome_.Icon(L"danger", icon, 14, .8f); // 70%
                chrome_.Text(TR(bookmark_invalid_address), {Layout::padding + 20, y, Layout::width - Layout::padding, y + 18}, 14, .7f);
                chrome_.Text(TR(bookmark_address_example), {Layout::padding + 20, y + 18, Layout::width - Layout::padding, y + Layout::help}, 14, .7f);
            }
        }
        {   // Dossier: icon, name, caret; a drawn control (focus ring when it holds the keyboard).
            const auto box = Field(FolderField()); Label(TR(bookmark_field_folder), FolderField());
            const auto v = motion_.Visual(FolderHit, now);
            chrome_.Round(box, Design::Radius::control, Design::Surface::raised + (Design::Surface::hover - Design::Surface::raised) * v.hover);
            if (focus_ == FolderHit && GetFocus() == hwnd_) chrome_.FocusRing(box, Design::Radius::control);
            const float cy = (box.top + box.bottom) / 2;
            chrome_.Icon(L"folder-closed", {box.left + 10, cy - 8, box.left + 26, cy + 8}, 16, 1);
            chrome_.Text(parentName_, {box.left + 34, box.top, box.right - 34, box.bottom}, 14, 1, false, true, -.28f);
            chrome_.Icon(L"caret", {box.right - 26, cy - 8, box.right - 10, cy + 8}, 16, 1);
        }
        {   // Supprimer (danger text) and Terminé (white fill, canvas text).
            const auto del = DeleteBox(); const auto dv = motion_.Visual(DeleteHit, now);
            if (dv.hover > 0 || dv.press > 0) chrome_.FillBox(del, Design::Radius::control, D2D1::ColorF(Design::Danger::r, Design::Danger::g, Design::Danger::b, .10f * dv.hover + .06f * dv.press));
            if (focus_ == DeleteHit && GetFocus() == hwnd_) chrome_.FocusRing(del, Design::Radius::control);
            chrome_.ink = chrome_.DangerColor(); chrome_.Text(TR(common_delete), {del.left + 10, del.top, del.right, del.bottom}, 14, 1, false, true, -.28f); chrome_.ink = D2D1::ColorF(1, 1, 1, 1);
            const auto done = DoneBox(); const auto ov = motion_.Visual(DoneHit, now);
            chrome_.FillBox(done, Design::Radius::control, D2D1::ColorF(1 - .08f * ov.hover - .08f * ov.press, 1 - .08f * ov.hover - .08f * ov.press, 1 - .08f * ov.hover - .08f * ov.press, 1));
            if (focus_ == DoneHit && GetFocus() == hwnd_) { auto* b = chrome_.SolidBrush(); b->SetColor(D2D1::ColorF(1, 1, 1, Design::Focus::Opacity(keyboard_))); t->DrawRoundedRectangle(D2D1::RoundedRect({done.left - 2.5f, done.top - 2.5f, done.right + 2.5f, done.bottom + 2.5f}, Design::Radius::control + 2, Design::Radius::control + 2), b, 1); }
            chrome_.ink = D2D1::ColorF(c, c, c, 1); chrome_.Text(TR(common_done), {done.left + 14, done.top, done.right, done.bottom}, 14, 1, false, true, -.28f); chrome_.ink = D2D1::ColorF(1, 1, 1, 1);
        }
        {   // Close: 24x24 at 8/8, 14 DIP glyph at 50%.
            const auto v = motion_.Visual(CloseHit, now);
            chrome_.DrawButton(L"close", CloseBox(), Design::ButtonVariant::Ghost, {v.hover, v.press, false, focus_ == CloseHit && GetFocus() == hwnd_});
        }
        // Hairline border (Paper #FFFFFF0F) on the rounded outline.
        const float px = t->GetSize().width / std::max(1.f, static_cast<float>(t->GetPixelSize().width));
        auto* b = chrome_.SolidBrush(); b->SetColor(D2D1::ColorF(1, 1, 1, 15 / 255.f));
        t->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(px / 2, px / 2, Layout::width - px / 2, h - px / 2), Layout::radius, Layout::radius), b, px);
        if (t->EndDraw() == D2DERR_RECREATE_TARGET) chrome_.RecreateTarget();
    }
    // ---- UI Automation: a named group (the window), its drawn buttons; the EDITs are named through `label`.
    static constexpr long long GroupKey = 10;
    std::vector<Accessibility::Node> Nodes() {
        std::vector<Accessibility::Node> nodes; if (!visible_) return nodes;
        auto screen = [&](D2D1_RECT_F r) { RECT p{Px(r.left), Px(r.top), Px(r.right), Px(r.bottom)}; MapWindowPoints(hwnd_, nullptr, reinterpret_cast<POINT*>(&p), 2); return p; };
        Accessibility::Node g; g.key = GroupKey; g.role = Accessibility::Role::Group; g.name = Title(); g.automationId = L"BookmarkPopover"; g.focusable = false; RECT w{}; GetWindowRect(hwnd_, &w); g.bounds = w; nodes.push_back(g);
        auto button = [&](int key, const wchar_t* name, D2D1_RECT_F r, const std::wstring& value = L"", const wchar_t* id = L"") {
            Accessibility::Node n; n.key = key; n.parent = GroupKey; n.name = name; n.help = value; n.automationId = id; n.bounds = screen(r); nodes.push_back(n);
        };
        button(FolderHit, TR(bookmark_field_folder), Field(FolderField()), parentName_, L"BookmarkFolder");
        button(DeleteHit, TR(common_delete), DeleteBox(), L"", L"BookmarkDelete");
        button(DoneHit, TR(common_done), DoneBox(), L"", L"BookmarkDone");
        button(CloseHit, TR(common_close), CloseBox(), L"", L"BookmarkClose");
        return nodes;
    }
public:
    ~Popover() { Dispose(); }
    void Dispose() { uia_.Detach(); chrome_.Dispose(); if (font_) DeleteObject(font_); font_ = nullptr; if (fieldBrush_) DeleteObject(fieldBrush_); fieldBrush_ = nullptr; hwnd_ = name_ = url_ = nullptr; visible_ = false; }
    bool Create(HINSTANCE instance, HWND owner, const std::wstring& root, Hooks hooks) {
        instance_ = instance; owner_ = owner; hooks_ = std::move(hooks); motion_.EnablePolish(); chrome_.SetMotion(&motion_);
        WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = instance; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.lpszClassName = L"LisiereBookmarkPopover";
        RegisterClassW(&wc);
        CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED, wc.lpszClassName, TR(bookmark_window_name), WS_POPUP | WS_CLIPCHILDREN, 0, 0, 1, 1, owner, nullptr, instance, this);
        if (!hwnd_) return false;
        SetLayeredWindowAttributes(hwnd_, 0, 255, LWA_ALPHA);
        NativeUI::ConfigureShadowlessPopup(hwnd_, Layout::radius); // no shadow: region-rounded, never DWM-rounded
        name_ = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 1, 1, hwnd_, nullptr, instance, nullptr);
        url_ = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 1, 1, hwnd_, nullptr, instance, nullptr);
        for (HWND e : {name_, url_}) { SendMessageW(e, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0); SetWindowSubclass(e, EditProc, 7, reinterpret_cast<DWORD_PTR>(this)); NativeUI::EditBuffer::Attach(e); }
        SendMessageW(name_, EM_SETLIMITTEXT, 1024, 0); SendMessageW(url_, EM_SETLIMITTEXT, 4095, 0);
        fieldBrush_ = CreateSolidBrush(FieldGray());
        Accessibility::Host host; host.window = hwnd_;
        host.nodes = [this] { return Nodes(); };
        host.focused = [this]() -> long long { return GetFocus() == hwnd_ ? focus_ : 0; };
        host.invoke = [this](long long key) { PostMessageW(hwnd_, WM_APP + 2, static_cast<WPARAM>(key), 0); };
        host.label = [this](HWND child, Accessibility::Node& n) {
            if (child == name_) { n.name = TR(bookmark_field_name); n.automationId = L"BookmarkName"; return true; }
            if (child == url_) { n.name = TR(bookmark_field_address); n.automationId = L"BookmarkAddress"; if (error_ && shownError_) n.help = TR(bookmark_invalid_address_full); return true; }
            return false;
        };
        uia_.Attach(std::move(host));
        return SUCCEEDED(chrome_.Init(hwnd_, root));
    }
    HWND Window() const { return hwnd_; }
    HWND NameEdit() const { return name_; }
    HWND UrlEdit() const { return url_; }
    bool Visible() const { return visible_; }
    bool ShowsError() const { return error_ && shownError_; }
    unsigned Editing() const { return visible_ ? request_.id : 0; }
    std::wstring Title() const { return request_.folder ? TR(bookmark_title_rename_folder) : request_.added ? TR(bookmark_title_added) : TR(bookmark_title_edit); }
    // Opens under `anchor` (screen pixels), right edges aligned when `alignRight`
    // (the star), left edges otherwise (a band item); kept 6 DIP inside `bounds`.
    void Show(const Request& request, RECT anchor, RECT bounds, bool alignRight, bool keyboard, bool reduced) {
        if (!hwnd_) return;
        if (font_) DeleteObject(font_);
        font_ = CreateFontW(-Px(16), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Inter Display");
        for (HWND e : {name_, url_}) SendMessageW(e, WM_SETFONT, reinterpret_cast<WPARAM>(font_), FALSE);
        request_ = request; parent_ = request.parent; parentName_.clear();
        for (const auto& f : (hooks_.folders ? hooks_.folders(request.id) : request.folders)) if (std::get<0>(f) == parent_) parentName_ = std::get<2>(f);
        error_ = shownError_ = false; focus_ = 0; keyboard_ = keyboard; hover_ = pressed_ = 0; motion_.ClearInteractions(); motion_.SetReducedMotion(reduced);
        SetWindowTextW(name_, request.title.c_str()); SetWindowTextW(url_, request.url.c_str());
        SetWindowTextW(hwnd_, Title().c_str());
        const int w = Px(Layout::width), h = Px(Height()), margin = Px(Design::Space::sm);
        int x = alignRight ? anchor.right - w : anchor.left, y = anchor.bottom + Px(Design::Space::md);
        x = std::max<int>(bounds.left + margin, std::min<int>(x, bounds.right - margin - w));
        if (y + h > bounds.bottom - margin) y = std::max<int>(bounds.top + margin, anchor.top - Px(Design::Space::md) - h);
        final_ = {x, y}; visible_ = true; opened_ = Motion::Now(); entering_ = !reduced;
        SetWindowPos(hwnd_, HWND_TOP, x, entering_ ? y - Px(Layout::enterSlide) : y, w, h, SWP_NOACTIVATE);
        SetLayeredWindowAttributes(hwnd_, 0, entering_ ? 0 : 255, LWA_ALPHA);
        chrome_.Resize(); PlaceEdits();
        ShowWindow(hwnd_, SW_SHOW); Paint(); ValidateRect(hwnd_, nullptr);
        SetForegroundWindow(hwnd_); SetFocus(name_); SendMessageW(name_, EM_SETSEL, 0, -1);
        if (entering_) SetTimer(hwnd_, FrameTimer, 10, nullptr);
        if (uia_.Listening()) { uia_.ChildrenChanged(GroupKey); uia_.Raise(GroupKey, UIA_Window_WindowOpenedEventId); }
    }
    void Hide() { Close(false); }
    // Test hooks (smoke run): type an address, press a key in a field.
    void SetUrlForTest(const std::wstring& text) { SetWindowTextW(url_, text.c_str()); Validate(); }
    void SetNameForTest(const std::wstring& text) { SetWindowTextW(name_, text.c_str()); }
    void CommitForTest() { Commit(); }
    void KeyForTest(WPARAM key) { Key(key); }
    unsigned ParentForTest() const { return parent_; }
    void PaintForTest() { Paint(); }
};
} // namespace BookmarkPopover
} // namespace NativeChrome
