#pragma once
#include "i18n.h"
// C1/C2 · Rechercher dans la page (Paper « Browser Design », p-D-0: contexte C1 DF0-0 / barre
// DNM-0, états C2 DR6-0: vide DUL-0, résultat DW5-0, aucun résultat DX1-0, focus clavier E11-0).
// Native bar anchored 8 DIP inside the top-right corner of the page rectangle. It owns a native
// EDIT (IME, selection, clipboard) and draws everything else with Direct2D. It never talks to
// WebView2: main wires ICoreWebView2Find to the hooks and reports back through SetResult().
//
// Paper values (get_jsx / get_computed_styles):
//   bar 420x40, raised #292929, border 1 px #FFFFFF0F, radius 6, padding 4, gap 4
//   field flex, h 32, radius 3, #FFFFFF0A (no match: #FF7B6E14), padding-inline 8, gap 8:
//     search glyph 16 @50 %, text 14/18 #FFFFFFE6, caret 1x16 #3E67FFE6, placeholder
//     « Rechercher dans la page » #FFFFFF66, counter 14/18 tabular #FFFFFF99 (no match: danger)
//   options 32x32 radius 3: « Aa » 14 medium -0.02em (#FFFFFF80 off, #FFFFFFE6 focused, white on),
//     « ab » 13/12 medium -0.02em over a 16x4 bracket (2 px, radius 1), 50 % when off;
//     on = #3E3E3E plate (--color-browser-pressed)
//   separator 1x16 #FFFFFF1F + 4 right; previous / next / close 32x32 radius 3, glyph 16 @70 %,
//     hover #333333, disabled 32 %; keyboard focus: inset 1 px #FFFFFF80 ring
//   tooltip (C5) under the hovered/focused button: « Occurrence suivante · Entrée »…
// Behaviour (C2 notes E4D-0 / E69-0): incremental from the first letter (no UI debounce: main
// may coalesce engine requests), counter « n sur m » (« 1 sur 999+ » beyond 999), arrows wrap,
// Entrée / Maj+Entrée = next / previous, F3 / Maj+F3 too, Alt+C / Alt+W toggle the options,
// Échap or ✕ close the bar and give focus back, Ctrl+F re-selects the field.
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include "native-ui.h"
#include <functional>
#include <string>
#include <vector>
#include "tooltip.h"

namespace NativeChrome {
namespace FindBar {
namespace Layout {
inline constexpr float width = 420, height = 40, radius = 6, inset = 8;          // C1 DNM-0: top 8, right 8
inline constexpr float fieldLeft = 5, fieldRight = 242, fieldTop = 4, fieldBottom = 36, fieldRadius = 3, fieldPad = 8, fieldGap = 8;
inline constexpr float button = 32, buttonRadius = 3, icon = 16;
inline constexpr float caseLeft = 246, wordLeft = 278, separatorX = 314, prevLeft = 319, nextLeft = 351, closeLeft = 383;
inline constexpr float textLine = 18, textSize = 14, enterMs = 160, slide = 8;
inline constexpr unsigned surface = 0x292929, hover = 0x333333, pressed = 0x3E3E3E;
inline constexpr float border = 0x0F / 255.f, field = 0x0A / 255.f, danger = 0x14 / 255.f, text = 0xE6 / 255.f, counter = 0x99 / 255.f;
inline constexpr float placeholder = 0x66 / 255.f, off = 0x80 / 255.f, separator = 0x1F / 255.f, glyph = .7f, disabled = .32f;
inline constexpr float tooltipMargin = 16; // C1 DQF-0: tooltip 16 DIP inside the page's right edge
static_assert(fieldLeft + (fieldRight - fieldLeft) + 4 + 64 + 4 + 1 + 4 + 96 + 4 + 1 == width, "Paper bar: field 237 + options 64 + nav 101");
}
struct Options { bool matchCase = false, wholeWord = false; bool operator==(const Options& o) const { return matchCase == o.matchCase && wholeWord == o.wholeWord; } };
enum class State { Idle, Searching, Found, NoMatch, Unavailable };
struct Hooks {
    std::function<void(const std::wstring&, const Options&)> query; // every edit / option change, "" = stop
    std::function<void()> next, previous, close, wake;              // close: Échap / ✕ (bar already hidden)
};
// Counter copy (Paper C2): empty while idle or searching.
inline std::wstring CounterText(int active, int count, State state) {
    switch (state) {
    case State::NoMatch: return TR(find_no_result);
    case State::Unavailable: return TR(find_unavailable);
    case State::Found: { if (count <= 0) return TR(find_no_result); const std::wstring total = count > 999 ? L"999+" : std::to_wstring(count);
        return TRF(find_counter, std::to_wstring(active < 1 ? 1 : active), total); }
    default: return L"";
    }
}
// UI Automation announcement (aria-live polite in Paper).
inline std::wstring Announcement(int active, int count, State state) { auto t = CounterText(active, count, state); return t; }

class Bar {
public:
    enum Hit { None = 0, CaseHit = 1, WordHit = 2, PrevHit = 3, NextHit = 4, CloseHit = 5, FieldHit = 6 };
private:
    HWND hwnd_ = nullptr, owner_ = nullptr, edit_ = nullptr, restore_ = nullptr; HINSTANCE instance_ = nullptr;
    Chrome chrome_; Chrome* surface_ = &chrome_; Motion::State motion_; PopupKit::Pump pump_; PopupKit::GlyphCache glyphs_; Accessibility::Bridge uia_;
    Tooltip::Tip tip_; Hooks hooks_; Options options_; State state_ = State::Idle; int active_ = 0, count_ = 0;
    RECT page_{}; bool visible_ = false, reduced_ = false, keyboard_ = false, silent_ = false;
    int hover_ = 0, pressed_ = 0, focus_ = 0; Motion::Tween enter_; HFONT font_ = nullptr; HBRUSH fieldBrush_ = nullptr; COLORREF fieldColor_ = 0;
    NativeUI::AccentCaret caret_; std::wstring announced_;

    int Px(float dip) const { return PopupKit::Px(owner_, dip); }
    float Dip(int px) const { return px * 96.f / PopupKit::Dpi(owner_); }
    static D2D1_RECT_F Box(float left) { return {left, Layout::fieldTop, left + Layout::button, Layout::fieldBottom}; }
    D2D1_RECT_F HitBox(int hit) const {
        switch (hit) { case CaseHit: return Box(Layout::caseLeft); case WordHit: return Box(Layout::wordLeft); case PrevHit: return Box(Layout::prevLeft);
        case NextHit: return Box(Layout::nextLeft); case CloseHit: return Box(Layout::closeLeft); case FieldHit: return {Layout::fieldLeft, Layout::fieldTop, Layout::fieldRight, Layout::fieldBottom}; }
        return {};
    }
    bool Enabled(int hit) const { return (hit != PrevHit && hit != NextHit) || (state_ == State::Found && count_ > 0); }
    int HitAt(float x, float y) const { for (int h : {CaseHit, WordHit, PrevHit, NextHit, CloseHit, FieldHit}) if (PopupKit::Contains(HitBox(h), x, y)) return h; return None; }
    static const wchar_t* Name(int hit) {
        switch (hit) { case CaseHit: return TR(find_match_case); case WordHit: return TR(find_whole_word); case PrevHit: return TR(find_previous); case NextHit: return TR(find_next); case CloseHit: return TR(common_close); }
        return L"";
    }
    static const wchar_t* Keys(int hit) {
        switch (hit) { case CaseHit: return L"Alt+C"; case WordHit: return L"Alt+W"; case PrevHit: return TR(key_shift_enter); case NextHit: return TR(key_enter); case CloseHit: return TR(key_escape); }
        return L"";
    }
    std::wstring Counter() const { return CounterText(active_, count_, state_); }
    float CounterWidth() { const auto c = Counter(); return c.empty() ? 0 : std::ceil(PopupKit::Measure(chrome_, c, 8192, {Layout::textSize, Layout::textLine, 0, false, true}, false).width); }
    COLORREF FieldColor() const {
        auto mix = [](int base, int over, float a) { return static_cast<int>(std::lround(base + (over - base) * a)); };
        if (state_ == State::NoMatch) return RGB(mix(0x29, 0xFF, Layout::danger), mix(0x29, 0x7B, Layout::danger), mix(0x29, 0x6E, Layout::danger));
        const int g = mix(0x29, 0xFF, Layout::field); return RGB(g, g, g);
    }
    COLORREF TextColor(float alpha) const { auto f = FieldColor(); auto c = [&](int b) { return static_cast<int>(std::lround(b + (255 - b) * alpha)); }; return RGB(c(GetRValue(f)), c(GetGValue(f)), c(GetBValue(f))); }
    void RefreshField() {
        const COLORREF color = FieldColor();
        if (!fieldBrush_ || color != fieldColor_) { if (fieldBrush_) DeleteObject(fieldBrush_); fieldBrush_ = CreateSolidBrush(color); fieldColor_ = color; if (edit_) InvalidateRect(edit_, nullptr, TRUE); }
        RefreshCaret();
    }
    void RefreshCaret() {
        if (!edit_ || GetFocus() != edit_) return;
        const auto a = Design::Accent::Get();
        caret_.Apply(edit_, FieldColor(), NativeUI::AccentCaret::Mix(FieldColor(), RGB(std::lround(a.r * 255), std::lround(a.g * 255), std::lround(a.b * 255)), Design::Accent::caretOpacity), std::max(1, Px(1)), Px(16));
    }
    void PlaceEdit() {
        if (!edit_) return;
        const float left = Layout::fieldLeft + Layout::fieldPad + Layout::icon + Layout::fieldGap, cw = CounterWidth();
        const float right = Layout::fieldRight - Layout::fieldPad - (cw ? cw + Layout::fieldGap : 0);
        const int top = Px(20 - Layout::textLine / 2), h = Px(Layout::textLine);
        SetWindowPos(edit_, nullptr, Px(left), top, std::max(1, Px(right) - Px(left)), h, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    void Paint() {
        Chrome& chrome_ = *surface_; // offscreen in RenderForTest
        using namespace Layout;
        if (!chrome_.RenderTarget()) chrome_.Resize();
        auto* t = chrome_.RenderTarget(); if (!t || !chrome_.SolidBrush()) return;
        const auto now = Motion::Now(); chrome_.keyboardModality = keyboard_;
        t->BeginDraw(); t->Clear(PopupKit::Gray(0x17));
        PopupKit::Fill(chrome_, {0, 0, Layout::width, Layout::height}, radius, PopupKit::Hex(surface));
        // Field.
        const auto f = HitBox(FieldHit);
        PopupKit::Fill(chrome_, f, fieldRadius, state_ == State::NoMatch ? PopupKit::Danger(danger) : PopupKit::White(field));
        glyphs_.Draw(chrome_, PopupKit::Glyphs::search, {f.left + fieldPad, f.top, f.left + fieldPad + icon, f.bottom}, icon, PopupKit::White(.5f));
        const auto c = Counter();
        if (!c.empty()) { const float cw = CounterWidth(); PopupKit::DrawText(chrome_, c, {f.right - fieldPad - cw, 20 - textLine / 2, f.right - fieldPad + 1, 20 + textLine / 2}, {textSize, textLine, 0, false, true},
            state_ == State::NoMatch ? PopupKit::Danger() : PopupKit::White(counter)); }
        // Options.
        for (int h : {CaseHit, WordHit}) {
            const auto b = HitBox(h); const auto v = motion_.Visual(h, now); const bool on = h == CaseHit ? options_.matchCase : options_.wholeWord;
            const float plate = on ? 1.f : std::max(v.hover, v.press);
            if (plate > 0) PopupKit::Fill(chrome_, b, buttonRadius, PopupKit::Hex(on || v.press > v.hover ? pressed : hover, on ? 1.f : plate));
            const bool focused = focus_ == h && GetFocus() == hwnd_;
            if (h == CaseHit) {
                const PopupKit::Type type{14, 16, -.28f, true}; const float w = PopupKit::Measure(chrome_, L"Aa", 8192, type, false).width;
                PopupKit::DrawText(chrome_, L"Aa", {(b.left + b.right - w) / 2, 12, (b.left + b.right + w) / 2 + 1, 28}, type, PopupKit::White(on ? 1.f : focused ? text : off));
            } else {
                const float o = on ? 1.f : .5f, cx = (b.left + b.right) / 2; const PopupKit::Type type{13, 12, -.26f, true};
                const float w = PopupKit::Measure(chrome_, L"ab", 8192, type, false).width, top = 20 - 17 / 2.f;
                PopupKit::DrawText(chrome_, L"ab", {cx - w / 2, top, cx + w / 2 + 1, top + 12}, type, PopupKit::White(o));
                const float y = top + 13; const auto col = PopupKit::White(o);
                PopupKit::Fill(chrome_, {cx - 8, y, cx - 6, y + 4}, 0, col); PopupKit::Fill(chrome_, {cx + 6, y, cx + 8, y + 4}, 0, col); PopupKit::Fill(chrome_, {cx - 8, y + 2, cx + 8, y + 4}, 1, col);
            }
            if (focused) PopupKit::InsetRing(chrome_, b, buttonRadius);
        }
        PopupKit::Fill(chrome_, {separatorX, 12, separatorX + 1, 28}, 0, PopupKit::White(separator));
        for (int h : {PrevHit, NextHit, CloseHit}) {
            const auto b = HitBox(h); const bool enabled = Enabled(h); const auto v = enabled ? motion_.Visual(h, now) : Motion::InteractionVisual{};
            const float plate = std::max(v.hover, v.press);
            if (plate > 0) PopupKit::Fill(chrome_, b, buttonRadius, PopupKit::Hex(v.press > v.hover ? pressed : hover, plate));
            const char* path = h == PrevHit ? PopupKit::Glyphs::caretUp : h == NextHit ? PopupKit::Glyphs::caretDown : PopupKit::Glyphs::close;
            const float size = icon * Motion::PressScale(v.press, reduced_, enabled);
            glyphs_.Draw(chrome_, path, b, size, PopupKit::White(glyph * (enabled ? 1.f : disabled)));
            if (focus_ == h && GetFocus() == hwnd_) PopupKit::InsetRing(chrome_, b, buttonRadius);
        }
        PopupKit::Outline(chrome_, Layout::width, Layout::height, radius, PopupKit::White(border));
        if (t->EndDraw() == D2DERR_RECREATE_TARGET) chrome_.RecreateTarget();
    }
    POINT Rest() const { return {page_.right - Px(Layout::inset) - Px(Layout::width), page_.top + Px(Layout::inset)}; }
    void Place(float progress) {
        const POINT p = Rest(); const float slide = reduced_ ? 0 : Layout::slide * (1 - progress);
        SetWindowPos(hwnd_, HWND_TOP, p.x, p.y - Px(slide), Px(Layout::width), Px(Layout::height), SWP_NOACTIVATE);
        PopupKit::SetAlpha(hwnd_, progress);
    }
    RECT ScreenBox(int hit) const { const auto b = HitBox(hit); RECT r{Px(b.left), Px(b.top), Px(b.right), Px(b.bottom)}; MapWindowPoints(hwnd_, nullptr, reinterpret_cast<POINT*>(&r), 2); return r; }
    RECT TipBounds() const { RECT r = page_; r.right -= Px(Layout::tooltipMargin); r.left += Px(Layout::tooltipMargin); return r; }
    void TipFor(int hit, bool immediate) {
        if (!hit || hit == FieldHit) { tip_.Hide(); return; }
        Tooltip::Content c{Name(hit), Keys(hit), L""}; const RECT target = ScreenBox(hit);
        const RECT bounds = TipBounds();
        if (immediate) tip_.Show(c, target, &bounds); else tip_.Hover(c, target, &bounds);
    }
    void Interact() { motion_.SetInteraction(Enabled(hover_) ? hover_ : 0, pressed_ == hover_ && Enabled(pressed_) ? pressed_ : 0, Motion::Now()); Repaint(); Wake(); }
    void Repaint() { if (!hwnd_ || !visible_) return; Paint(); ValidateRect(hwnd_, nullptr); }
    void Wake() { pump_.Request(NextWake(Motion::Now())); }
    void Query() { if (silent_ || !hooks_.query) return; const auto text = Text(); state_ = text.empty() ? State::Idle : State::Searching; if (text.empty()) active_ = count_ = 0; RefreshField(); PlaceEdit(); Repaint(); hooks_.query(text, options_); }
    void Toggle(int hit) { if (hit == CaseHit) options_.matchCase = !options_.matchCase; else options_.wholeWord = !options_.wholeWord; Repaint(); if (uia_.Listening()) uia_.Raise(hit, UIA_ToggleToggleStatePropertyId); Query(); }
    void Activate(int hit) {
        if (!Enabled(hit)) return;
        switch (hit) {
        case CaseHit: case WordHit: Toggle(hit); break;
        case PrevHit: if (hooks_.previous) hooks_.previous(); break;
        case NextHit: if (hooks_.next) hooks_.next(); break;
        case CloseHit: Close(); break;
        case FieldHit: FocusField(); break;
        }
    }
    void Step(bool back) { if (state_ != State::Found || count_ <= 0) return; if (back) { if (hooks_.previous) hooks_.previous(); } else if (hooks_.next) hooks_.next(); }
    void Close() { Hide(); if (hooks_.close) hooks_.close(); }
    void FocusField() { focus_ = 0; tip_.Hide(); if (edit_) { SetFocus(edit_); SendMessageW(edit_, EM_SETSEL, 0, -1); } Repaint(); }
    std::vector<int> Order() const { std::vector<int> o{0, CaseHit, WordHit}; if (Enabled(PrevHit)) { o.push_back(PrevHit); o.push_back(NextHit); } o.push_back(CloseHit); return o; }
    void Cycle(bool back) {
        keyboard_ = true; const auto o = Order(); const int at = GetFocus() == edit_ ? 0 : focus_;
        int i = 0; for (size_t k = 0; k < o.size(); ++k) if (o[k] == at) i = static_cast<int>(k);
        i = (i + (back ? -1 : 1) + static_cast<int>(o.size())) % static_cast<int>(o.size());
        if (!o[i]) { FocusField(); return; }
        focus_ = o[i]; SetFocus(hwnd_); Repaint(); TipFor(focus_, true);
        if (uia_.Listening()) uia_.Raise(focus_, UIA_AutomationFocusChangedEventId);
    }
    // Keys shared by the EDIT and the drawn buttons. True when handled.
    bool Key(WPARAM key, bool alt) {
        const bool shift = GetKeyState(VK_SHIFT) < 0, ctrl = GetKeyState(VK_CONTROL) < 0;
        if (alt && (key == 'C' || key == 'W')) { keyboard_ = true; Toggle(key == 'C' ? CaseHit : WordHit); return true; }
        if (alt) return false;
        if (key == VK_ESCAPE) { if (tip_.Visible()) tip_.HideNow(); Close(); return true; }
        if (key == VK_TAB) { Cycle(shift); return true; }
        if (key == VK_F3) { Step(shift); return true; }
        if (ctrl && key == 'F') { FocusField(); return true; }
        if (key == VK_RETURN || key == VK_SPACE) {
            if (GetFocus() == hwnd_ && focus_) { Activate(focus_); return true; }
            if (key == VK_RETURN) { Step(shift); return true; }
        }
        return false;
    }
    static LRESULT CALLBACK EditProc(HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
        auto* self = reinterpret_cast<Bar*>(data);
        switch (msg) {
        case WM_GETDLGCODE: return DefSubclassProc(h, msg, w, l) | DLGC_WANTALLKEYS;
        case WM_KEYDOWN: if (self->Key(w, false)) return 0; break;
        case WM_SYSKEYDOWN: if (self->Key(w, true)) return 0; break;
        case WM_CHAR: if (w == L'\t' || w == L'\r' || w == 27) return 0; if (w == 6 /* Ctrl+F */) return 0; break;
        case WM_SYSCHAR: if (w == L'c' || w == L'C' || w == L'w' || w == L'W') return 0; break;
        case WM_SETFOCUS: { const LRESULT r = DefSubclassProc(h, msg, w, l); self->focus_ = 0; self->RefreshCaret(); self->Repaint(); return r; }
        case WM_LBUTTONDOWN: self->keyboard_ = false; self->tip_.Hide(); break;
        case WM_PAINT: {
            const LRESULT r = DefSubclassProc(h, msg, w, l);
            if (GetWindowTextLengthW(h) == 0) { // Paper placeholder (#FFFFFF66), drawn after the caret position 0
                HDC dc = GetDC(h); RECT rc{}; GetClientRect(h, &rc); rc.left += self->Px(2);
                HGDIOBJ old = SelectObject(dc, self->font_); SetBkMode(dc, TRANSPARENT); SetTextColor(dc, self->TextColor(Layout::placeholder));
                DrawTextW(dc, TR(find_title), -1, &rc, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
                SelectObject(dc, old); ReleaseDC(h, dc);
            }
            return r;
        }
        }
        return DefSubclassProc(h, msg, w, l);
    }
    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<Bar*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (msg == WM_NCCREATE) { self = static_cast<Bar*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); self->hwnd_ = h; }
        return self ? self->Handle(h, msg, w, l) : DefWindowProcW(h, msg, w, l);
    }
    LRESULT Handle(HWND h, UINT msg, WPARAM w, LPARAM l) {
        const float s = 96.f / PopupKit::Dpi(h);
        switch (msg) {
        case WM_GETOBJECT: { LRESULT r; if (uia_.OnGetObject(h, w, l, r)) return r; break; }
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); Paint(); EndPaint(h, &ps); return 0; }
        case WM_CTLCOLOREDIT: { HDC dc = reinterpret_cast<HDC>(w); SetTextColor(dc, TextColor(Layout::text)); SetBkColor(dc, FieldColor()); SetBkMode(dc, OPAQUE); return reinterpret_cast<LRESULT>(fieldBrush_); }
        case WM_COMMAND: if (reinterpret_cast<HWND>(l) == edit_ && HIWORD(w) == EN_CHANGE) Query(); return 0;
        case WM_MOUSEACTIVATE: return MA_ACTIVATE;
        case WM_TIMER: if (w == PopupKit::Pump::TimerId) { KillTimer(h, w); const auto now = Motion::Now(); Frame(now); pump_.Request(NextWake(now)); } return 0;
        case WM_SETCURSOR: { POINT p; GetCursorPos(&p); ScreenToClient(h, &p); if (HitAt(p.x * s, p.y * s) == FieldHit) { SetCursor(LoadCursor(nullptr, IDC_IBEAM)); return TRUE; } break; }
        case WM_MOUSEMOVE: { TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, h, 0}; TrackMouseEvent(&t); const int hit = HitAt(GET_X_LPARAM(l) * s, GET_Y_LPARAM(l) * s); if (hit != hover_) { hover_ = hit; Interact(); if (!pressed_) TipFor(hit, false); } return 0; }
        case WM_MOUSELEAVE: hover_ = 0; Interact(); tip_.Hide(); return 0;
        case WM_LBUTTONDOWN: { keyboard_ = false; tip_.HideNow(); const int hit = HitAt(GET_X_LPARAM(l) * s, GET_Y_LPARAM(l) * s);
            if (hit == FieldHit) { FocusField(); return 0; }
            pressed_ = Enabled(hit) ? hit : 0; if (pressed_) SetCapture(h); Interact(); return 0; }  // feedback at pointer-down
        case WM_LBUTTONUP: { const int hit = HitAt(GET_X_LPARAM(l) * s, GET_Y_LPARAM(l) * s), was = pressed_; pressed_ = 0; if (GetCapture() == h) ReleaseCapture(); Interact(); if (was && was == hit) Activate(hit); return 0; }
        case WM_KEYDOWN: if (Key(w, false)) return 0; break;
        case WM_SYSKEYDOWN: if (Key(w, true)) return 0; break;
        case WM_SYSCHAR: if (w == L'c' || w == L'C' || w == L'w' || w == L'W') return 0; break;
        case WM_SETFOCUS: if (!focus_) focus_ = CaseHit; Repaint(); return 0;
        case WM_KILLFOCUS: tip_.Hide(); Repaint(); return 0;
        case WM_APP + 1: keyboard_ = true; Activate(static_cast<int>(w)); return 0;
        case WM_DPICHANGED: if (visible_) Reposition(page_); return 0;
        case WM_NCDESTROY: uia_.Detach(); hwnd_ = edit_ = nullptr; visible_ = false; break;
        }
        return DefWindowProcW(h, msg, w, l);
    }
    std::vector<Accessibility::Node> Nodes() {
        std::vector<Accessibility::Node> nodes; if (!visible_) return nodes;
        Accessibility::Node g; g.key = 100; g.role = Accessibility::Role::Group; g.name = TR(find_title); g.automationId = L"FindBar"; g.focusable = false; g.status = Counter(); GetWindowRect(hwnd_, &g.bounds); nodes.push_back(g);
        for (int hit : {CaseHit, WordHit, PrevHit, NextHit, CloseHit}) {
            Accessibility::Node n; n.key = hit; n.parent = 100; n.name = Name(hit); n.accelerator = Keys(hit); n.bounds = ScreenBox(hit); n.enabled = Enabled(hit);
            static const wchar_t* ids[] = {L"", L"FindMatchCase", L"FindWholeWord", L"FindPrevious", L"FindNext", L"FindClose"}; n.automationId = ids[hit];
            if (hit == CaseHit) n.toggle = options_.matchCase ? 2 : 1; if (hit == WordHit) n.toggle = options_.wholeWord ? 2 : 1;
            nodes.push_back(n);
        }
        return nodes;
    }
public:
    ~Bar() { Dispose(); }
    bool Create(HINSTANCE instance, HWND owner, const std::wstring& root, Hooks hooks) {
        instance_ = instance; owner_ = owner; hooks_ = std::move(hooks); motion_.EnablePolish(); chrome_.SetMotion(&motion_);
        PopupKit::Register(instance, L"LisiereFindBar", Proc);
        if (!PopupKit::CreatePopup(instance, owner, L"LisiereFindBar", TR(find_title), 0, this, Layout::radius, WS_POPUP | WS_CLIPCHILDREN)) return false;
        pump_.Bind(hwnd_, hooks_.wake);
        edit_ = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 1, 1, hwnd_, nullptr, instance, nullptr); NativeUI::SelectAllOnCtrlA(edit_);
        if (!edit_) return false;
        SendMessageW(edit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0); SendMessageW(edit_, EM_SETLIMITTEXT, 1024, 0);
        SetWindowSubclass(edit_, EditProc, 0xF1, reinterpret_cast<DWORD_PTR>(this));
        RefreshField();
        Accessibility::Host host; host.window = hwnd_;
        host.nodes = [this] { return Nodes(); };
        host.focused = [this]() -> long long { return GetFocus() == hwnd_ ? focus_ : 0; };
        host.invoke = [this](long long key) { PostMessageW(hwnd_, WM_APP + 1, static_cast<WPARAM>(key), 0); };
        host.toggle = [this](long long key) { PostMessageW(hwnd_, WM_APP + 1, static_cast<WPARAM>(key), 0); };
        host.label = [this](HWND child, Accessibility::Node& n) { if (child != edit_) return false; n.name = TR(find_title); n.automationId = L"FindField"; n.status = Counter(); return true; };
        uia_.Attach(std::move(host));
        Tooltip::Hooks tipHooks; tipHooks.wake = [this] { Wake(); };
        tip_.Create(instance, hwnd_, root, tipHooks);
        return SUCCEEDED(chrome_.Init(hwnd_, root));
    }
    void Dispose() {
        pump_.Stop(); tip_.Dispose(); uia_.Detach(); chrome_.Dispose(); glyphs_.Clear();
        if (hwnd_ && IsWindow(hwnd_)) DestroyWindow(hwnd_);
        if (font_) DeleteObject(font_); if (fieldBrush_) DeleteObject(fieldBrush_);
        font_ = nullptr; fieldBrush_ = nullptr; hwnd_ = edit_ = nullptr; visible_ = false;
    }
    // Opens (or re-focuses) the bar over `pageScreen` (screen px of the page area). `seed`
    // (selection or the tab's last search) replaces the text, selected; empty keeps it.
    void Show(RECT pageScreen, const std::wstring& seed) {
        if (!hwnd_) return;
        page_ = pageScreen; const bool was = visible_;
        if (!was) { HWND f = GetFocus(); restore_ = f && f != edit_ && f != hwnd_ ? f : nullptr; }
        if (font_) DeleteObject(font_);
        font_ = CreateFontW(-Px(Layout::textSize), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Inter Display");
        SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), FALSE);
        visible_ = true; chrome_.Resize();
        if (!was) { enter_.Snap(reduced_ ? 1.f : 0.f); enter_.Set(1, Motion::Now(), Motion::Milliseconds(static_cast<int>(Layout::enterMs)), false, Motion::Curve::EaseOut); Place(enter_.Value(Motion::Now())); }
        else Place(1);
        if (!seed.empty() && seed != Text()) SetWindowTextW(edit_, seed.c_str()); // EN_CHANGE → query
        PlaceEdit(); ShowWindow(hwnd_, SW_SHOW); chrome_.Resize(); Paint(); ValidateRect(hwnd_, nullptr);
        SetForegroundWindow(hwnd_); FocusField();
        if (uia_.Listening()) { uia_.ChildrenChanged(100); uia_.Raise(100, UIA_Window_WindowOpenedEventId); }
        Wake();
    }
    // Hides without notifying (main's tab switch, page navigation). Focus goes back to what had
    // it at Show() when it still exists; main then moves focus into the page.
    void Hide() {
        if (!hwnd_ || !visible_) return;
        const bool had = GetFocus() == edit_ || GetFocus() == hwnd_;
        visible_ = false; tip_.HideNow(); hover_ = pressed_ = focus_ = 0; motion_.ClearInteractions(); ShowWindow(hwnd_, SW_HIDE);
        if (had) { if (restore_ && IsWindow(restore_)) SetFocus(restore_); else if (owner_) SetForegroundWindow(owner_); }
        restore_ = nullptr; pump_.Request({});
    }
    void Focus() { if (visible_) { SetForegroundWindow(hwnd_); FocusField(); } }
    bool HasFocus() const { HWND f = GetFocus(); return hwnd_ && (f == hwnd_ || f == edit_); }
    // Engine report: `active` 1-based (0 = none), `count` matches, `state` as below.
    void SetResult(int active, int count, State state) {
        active_ = std::max(0, active); count_ = std::max(0, count); state_ = state;
        if (state_ == State::Found && count_ == 0) state_ = State::NoMatch;
        if (Text().empty() && state_ != State::Unavailable) { state_ = State::Idle; active_ = count_ = 0; }
        RefreshField(); PlaceEdit(); motion_.SetInteraction(Enabled(hover_) ? hover_ : 0, 0, Motion::Now()); Repaint();
        if (GetFocus() == hwnd_ && !Enabled(focus_)) { focus_ = CloseHit; Repaint(); }
        const auto spoken = Announcement(active_, count_, state_);
        if (spoken != announced_ && !spoken.empty() && uia_.Listening()) uia_.Notify(100, spoken.c_str(), L"FindResult");
        announced_ = spoken;
    }
    void Reposition(RECT pageScreen) { page_ = pageScreen; if (visible_) { Place(enter_.Value(Motion::Now())); chrome_.Resize(); PlaceEdit(); Repaint(); } }
    std::wstring Text() const { if (!edit_) return L""; std::wstring s(static_cast<size_t>(GetWindowTextLengthW(edit_)) + 1, L'\0'); s.resize(static_cast<size_t>(GetWindowTextW(edit_, s.data(), static_cast<int>(s.size())))); return s; }
    // Replaces the text without a query (restoring a tab's bar state).
    void SetText(const std::wstring& text) { silent_ = true; if (edit_) SetWindowTextW(edit_, text.c_str()); silent_ = false; PlaceEdit(); Repaint(); }
    const Options& CurrentOptions() const { return options_; }
    void SetOptions(const Options& o) { options_ = o; Repaint(); }
    State CurrentState() const { return state_; }
    std::wstring CounterLabel() const { return Counter(); }
    bool Visible() const { return visible_; }
    HWND Window() const { return hwnd_; }
    HWND Edit() const { return edit_; }
    Tooltip::Tip& TooltipForTest() { return tip_; }
    void SetReducedMotion(bool reduced) { reduced_ = reduced; motion_.SetReducedMotion(reduced); tip_.SetReducedMotion(reduced); if (reduced) enter_.Settle(); }
    Motion::Wake NextWake(Motion::Time now) const {
        auto w = Motion::Earlier(motion_.NextWake(now), tip_.NextWake(now));
        if (visible_ && enter_.Moving(now)) w = Motion::Earlier(w, Motion::Frame());
        return w;
    }
    void Frame(Motion::Time now) {
        tip_.Frame(now);
        if (!visible_) return;
        Place(enter_.Value(now)); Paint(); ValidateRect(hwnd_, nullptr);
    }
    // Tests drive the keyboard paths directly (no input injection).
    bool KeyForTest(WPARAM key, bool alt = false) { return Key(key, alt); }
    void ActivateForTest(int hit) { Activate(hit); }
    void PaintForTest() { Paint(); }
    bool RenderForTest(const std::wstring& root, const wchar_t* png, UINT dpi = 96) { return PopupKit::RenderPng(root, Layout::width, Layout::height, dpi, png, surface_, [this] { Paint(); }); }
};
} // namespace FindBar
} // namespace NativeChrome
