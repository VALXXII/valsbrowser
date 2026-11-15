#pragma once
// In-place rename (bookmark folders: band item, Favoris drawer row). A borderless single-line native
// EDIT laid exactly over the label it edits, in the label's own font and colours: no frame, no popover.
// design/bookmark-inline-rename-plan.md.
//  - Starts with the whole text selected; Enter commits, Escape restores, Tab and focus loss commit
//    (Explorer's rename). During IME composition Enter / Escape belong to the IME.
//  - Every other key is the EDIT's own (caret, selection, word moves, text undo, clipboard, IME).
//  - The owner keeps the stable item id; the editor only reports text. Commit/Cancel destroy the EDIT
//    after the callback (never from inside its own WM_KILLFOCUS: deferred by a posted message).
//  - Accessible name through documented Dynamic Annotation (IAccPropServices, PROPID_ACC_NAME).
#include <windows.h>
#include <commctrl.h>
#include <oleacc.h>
#include <algorithm>
#include <functional>
#include <string>

namespace NativeUI {

class InlineRename {
public:
    struct Style { HFONT font = nullptr; COLORREF text = RGB(255, 255, 255), back = RGB(51, 51, 51); };
    // `committed`: Enter / Tab / focus loss (with the typed text) or Escape / Cancel() (empty text).
    using Done = std::function<void(unsigned long long id, bool committed, const std::wstring& text)>;
    ~InlineRename() { Cancel(); if (brush_) DeleteObject(brush_); }
    // `box`: the label's text box in `host` client pixels. Returns false if the EDIT could not be made.
    bool Begin(HWND host, unsigned long long id, RECT box, const std::wstring& text, Style style, const wchar_t* name, Done done) {
        Cancel();
        HWND edit = CreateWindowExW(0, L"EDIT", text.c_str(), WS_CHILD | ES_AUTOHSCROLL, 0, 0, 1, 1, host, nullptr, reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(host, GWLP_HINSTANCE)), nullptr);
        if (!edit) return false;
        edit_ = edit; host_ = host; id_ = id; done_ = std::move(done); style_ = style; composing_ = finishing_ = false;
        if (brush_) DeleteObject(brush_);
        brush_ = CreateSolidBrush(style.back);
        SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(style.font), FALSE);
        SendMessageW(edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
        SendMessageW(edit, EM_SETLIMITTEXT, 1024, 0);
        SetWindowSubclass(edit, Proc, subclassId, reinterpret_cast<DWORD_PTR>(this));
        Name(edit, name);
        Move(box);
        ShowWindow(edit, SW_SHOW);
        SetFocus(edit);
        SendMessageW(edit, EM_SETSEL, 0, -1);
        return true;
    }
    // The label moved (relayout, scroll): same box rules; the EDIT is vertically centred on it.
    void Move(RECT box) {
        if (!edit_) return;
        box_ = box;
        HDC dc = GetDC(edit_); HGDIOBJ old = SelectObject(dc, style_.font); TEXTMETRICW tm{}; GetTextMetricsW(dc, &tm); SelectObject(dc, old); ReleaseDC(edit_, dc);
        const int h = tm.tmHeight, cy = (box.top + box.bottom) / 2;
        SetWindowPos(edit_, HWND_TOP, box.left, cy - h / 2, (std::max)(1L, box.right - box.left), h, SWP_NOACTIVATE);
    }
    void Commit() { Finish(true); }
    void Cancel() { Finish(false); }
    bool Active() const { return edit_ != nullptr; }
    unsigned long long Id() const { return edit_ ? id_ : 0; }
    HWND Edit() const { return edit_; }
    HWND Host() const { return edit_ ? host_ : nullptr; }
    RECT Box() const { return box_; }
    bool Owns(HWND h) const { return edit_ && h == edit_; }
    // From the host's WM_CTLCOLOREDIT for Edit(): the label's colours.
    LRESULT Color(HDC dc) const { SetTextColor(dc, style_.text); SetBkColor(dc, style_.back); SetBkMode(dc, OPAQUE); return reinterpret_cast<LRESULT>(brush_); }
private:
    static constexpr UINT_PTR subclassId = 0x524E;   // "RN"
    static constexpr UINT FinishMessage = WM_APP + 0x52;
    HWND edit_ = nullptr, host_ = nullptr; unsigned long long id_ = 0; Done done_; Style style_{}; HBRUSH brush_ = nullptr; RECT box_{};
    bool composing_ = false, finishing_ = false;
    void Finish(bool commit) {
        if (!edit_ || finishing_) return;
        finishing_ = true;
        std::wstring text;
        if (commit) { const int n = GetWindowTextLengthW(edit_); text.resize(n + 1); GetWindowTextW(edit_, text.data(), n + 1); text.resize(n); }
        HWND edit = edit_; const unsigned long long id = id_; Done done = std::move(done_);
        const bool hadFocus = GetFocus() == edit;
        edit_ = nullptr; done_ = nullptr;
        RemoveWindowSubclass(edit, Proc, subclassId);
        if (hadFocus && host_) SetFocus(host_);   // focus never stays on a dying EDIT; the owner may move it on
        DestroyWindow(edit);
        finishing_ = false;
        if (done) done(id, commit, text);
    }
    static void Name(HWND edit, const wchar_t* name) {
        if (!name || !*name) return;
        // CLSID_AccPropServices / IID_IAccPropServices / PROPID_ACC_NAME (oleacc.h), defined here so no
        // extra import library is needed.
        static const CLSID clsid{0xb5f8350b, 0x0548, 0x48b1, {0xa6, 0xee, 0x88, 0xbd, 0x00, 0xb4, 0xa5, 0xe7}};
        static const IID iid{0x6e26e776, 0x04f0, 0x495d, {0x80, 0xe4, 0x33, 0x30, 0x35, 0x2e, 0x31, 0x69}};
        static const GUID nameProp{0x608d3df8, 0x8128, 0x4aa7, {0xa4, 0x28, 0xf5, 0x5e, 0x49, 0x26, 0x72, 0x91}};
        IAccPropServices* services = nullptr;
        if (SUCCEEDED(CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, iid, reinterpret_cast<void**>(&services))) && services) {
            services->SetHwndPropStr(edit, static_cast<DWORD>(OBJID_CLIENT), CHILDID_SELF, nameProp, name);
            services->Release();
        }
    }
    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
        auto* self = reinterpret_cast<InlineRename*>(data);
        switch (msg) {
        case WM_IME_STARTCOMPOSITION: self->composing_ = true; break;
        case WM_IME_ENDCOMPOSITION: self->composing_ = false; break;
        case WM_GETDLGCODE: return DefSubclassProc(h, msg, w, l) | DLGC_WANTALLKEYS | DLGC_WANTCHARS | DLGC_WANTARROWS | DLGC_WANTTAB;
        case WM_KEYDOWN:
            if (!self->composing_ && (w == VK_RETURN || w == VK_TAB)) { self->Commit(); return 0; }
            if (!self->composing_ && w == VK_ESCAPE) { self->Cancel(); return 0; }
            break;
        case WM_CHAR:   // the matching characters of Enter / Escape / Tab (no beep, no tab inserted)
            if (w == L'\r' || w == L'\n' || w == 27 || w == L'\t') return 0;
            break;
        case WM_KILLFOCUS: { const LRESULT r = DefSubclassProc(h, msg, w, l); PostMessageW(h, FinishMessage, 0, 0); return r; }
        case FinishMessage: if (GetFocus() != h) self->Commit(); return 0;   // still away: blur commits
        case WM_NCDESTROY: RemoveWindowSubclass(h, Proc, subclassId); if (self->edit_ == h) { self->edit_ = nullptr; self->done_ = nullptr; } break;
        default: break;
        }
        return DefSubclassProc(h, msg, w, l);
    }
};

} // namespace NativeUI
