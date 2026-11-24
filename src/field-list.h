#pragma once
#include "i18n.h"
// #121 « Infos enregistrées · A » (Paper 1LDA-0) and « Liste réunie, comptes + infos · A » (1LTY-0), page « #121 —
// Fenêtres du moteur à notre design »: our list under a page field. Security contract (P1, P6, P7):
// design/security-review-autofill-under-field.md. An owned, never-activated popup (WS_EX_NOACTIVATE, MA_NOACTIVATE):
// the page keeps the focus (no blur), our isolated world routes ↓ ↑ Entrée Échap, this window only draws rows and
// reports pointer intent; a row is chosen by a press AND a release on it, both while the host says it is armed.
//
// Paper values (get_jsx):
//   container 320 wide, #171717 (--color-browser-canvas), border 1 #FFFFFF14, radius 10, padding 6, gap 2
//   title: padding 6 / 8 / 6 (the second title of the merged list: top 10), 14/18 Medium #FFFFFF80
//   info row: h 36, radius 6, padding left 8 right 4, gap 10; icon slot 24 (envelope 16 #FFFFFF99); text 16/20 -0.01em,
//     the typed letters SemiBold #FFFFFF, the rest Regular #FFFFFFB3, clipped (no ellipsis); « Retirer » 28×28 radius 6
//     (x 12 #FFFFFFB3) on the hovered row only, its slot kept on the others
//   account row: h 48, padding 8, gap 10; avatar 24 radius 12 #3E3E3E (--color-browser-pressed), initial 12/12
//     SemiBold #FFFFFFCC; column gap 2: username 16/20 -0.01em #FFFFFF, « •••••••••••• » 14/16 +0.08em #FFFFFF80
//   hovered row: #333333 (--color-browser-hover); separator 1 px #FFFFFF0D, 8 in from each side
//   footer: h 32, padding 8, gap 10, sliders 16 #FFFFFF99 in the 24 slot, 14/18 Regular #FFFFFFB3
// States (board « Listes sous le champ + bulle · états », 1OBF-0): the row chosen with ↓ has the hovered fill and, in
// place of the cross, a key cap « Entrée » (#FFFFFF14, radius 4, height 20, padding 6, 12/16 Medium #FFFFFFB3); Entrée
// fills it, Suppr removes an e-mail; the footer hovered takes #333333 (its text stays #FFFFFFB3); the cross hovered
// gets a #FFFFFF1A plate (pressed #FFFFFF29). Accounts without infos: the B03 title, footer « Gérer les mots de passe ».
// Extrapolated (not on the board): the cap on a chosen ACCOUNT row (8 from its right edge, like its padding).
#include <windows.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>
#include "tooltip.h"

namespace NativeChrome::FieldList {
namespace Glyph {   // Paper path data (Phosphor regular / fill, 256 view box)
inline constexpr char envelope[] = "M224,48H32a8,8,0,0,0-8,8V192a16,16,0,0,0,16,16H216a16,16,0,0,0,16-16V56A8,8,0,0,0,224,48Zm-20.57,16L128,133.15,52.57,64ZM216,192H40V74.19l82.59,75.71a8,8,0,0,0,10.82,0L216,74.19V192Z";
inline constexpr char close[] = "M205.66,194.34a8,8,0,0,1-11.32,11.32L128,139.31,61.66,205.66a8,8,0,0,1-11.32-11.32L116.69,128,50.34,61.66A8,8,0,0,1,61.66,50.34L128,116.69l66.34-66.35a8,8,0,0,1,11.32,11.32L139.31,128Z";
inline constexpr char sliders[] = "M40,88H73a32,32,0,0,0,62,0h81a8,8,0,0,0,0-16H135a32,32,0,0,0-62,0H40a8,8,0,0,0,0,16Zm64-24A16,16,0,1,1,88,80,16,16,0,0,1,104,64ZM216,168H199a32,32,0,0,0-62,0H40a8,8,0,0,0,0,16h97a32,32,0,0,0,62,0h17a8,8,0,0,0,0-16Zm-48,24a16,16,0,1,1,16-16A16,16,0,0,1,168,192Z";
}
namespace Layout {
inline constexpr float width = 320, border = 1, pad = 6, gap = 2, radius = 10;
inline constexpr float titleTop = 6, titleBottom = 6, secondTitleTop = 10, titleInset = 8, titleLine = 18, titleSize = 14;
inline constexpr float infoRow = 36, accountRow = 48, footerRow = 32, rowRadius = 6, rowLeft = 8, infoRight = 4, slot = 24, rowGap = 10;
inline constexpr float icon = 16, remove = 28, removeIcon = 12, avatar = 24, initialSize = 12;
inline constexpr float textSize = 16, textLine = 20, textTracking = -.16f, dotsSize = 14, dotsLine = 16, dotsTracking = 1.12f, columnGap = 2;
inline constexpr float separatorInset = 8, anchorGap = 8;                     // 8 DIP under the field (board: y 290 under 249.6 + 32)
inline constexpr unsigned surface = 0x171717, hover = 0x333333, avatarFill = 0x3E3E3E;
inline constexpr float borderAlpha = 0x14 / 255.f, titleAlpha = 0x80 / 255.f, iconAlpha = 0x99 / 255.f, restAlpha = 0xB3 / 255.f;
inline constexpr float initialAlpha = 0xCC / 255.f, dotsAlpha = 0x80 / 255.f, separatorAlpha = 0x0D / 255.f;
inline constexpr float removeHover = 0x1A / 255.f, removePressed = 0x29 / 255.f;
inline constexpr float capHeight = 20, capPad = 6, capRadius = 4, capSize = 12, capLine = 16, capPlate = 0x14 / 255.f, capText = 0xB3 / 255.f;
}
// What the list shows. Selectable rows, in this order: accounts, infos, the footer (the host and the isolated world
// count them the same way: Count()).
struct Content {
    std::wstring accountsTitle; std::vector<std::wstring> accounts;   // « Comptes enregistrés pour github.com »
    std::wstring infosTitle; std::vector<std::wstring> infos;          // « Informations enregistrées »
    std::size_t typed = 0;                                              // letters shown SemiBold in the infos
    std::wstring footer;                                                // « Gérer la saisie automatique » …
    std::size_t Count() const { return accounts.size() + infos.size() + 1; }
    bool operator==(const Content& o) const { return accountsTitle == o.accountsTitle && accounts == o.accounts && infosTitle == o.infosTitle && infos == o.infos && typed == o.typed && footer == o.footer; }
};
struct Item { enum class Kind { Title, Account, Info, Separator, Footer } kind; float top = 0, height = 0; int row = -1; std::size_t index = 0; bool second = false; };
// The vertical layout in DIP (y from the window's top edge, the border included).
inline std::vector<Item> Items(const Content& c) {
    using namespace Layout; std::vector<Item> out; float y = border + pad; int row = 0;
    auto add = [&](Item::Kind k, float h, int r = -1, std::size_t i = 0, bool second = false) { if (!out.empty()) y += gap; out.push_back({k, y, h, r, i, second}); y += h; };
    if (!c.accounts.empty()) { add(Item::Kind::Title, titleTop + titleLine + titleBottom); for (std::size_t i = 0; i < c.accounts.size(); ++i) add(Item::Kind::Account, accountRow, row++, i); }
    if (!c.infos.empty()) { const bool second = !c.accounts.empty(); add(Item::Kind::Title, (second ? secondTitleTop : titleTop) + titleLine + titleBottom, -1, 1, second); for (std::size_t i = 0; i < c.infos.size(); ++i) add(Item::Kind::Info, infoRow, row++, i); }
    add(Item::Kind::Separator, 1); add(Item::Kind::Footer, footerRow, row++);
    return out;
}
inline float Height(const Content& c) { const auto items = Items(c); return items.empty() ? 0 : items.back().top + items.back().height + Layout::pad + Layout::border; }
inline D2D1_RECT_F RowRect(const Item& it) { using namespace Layout; return {border + pad, it.top, width - border - pad, it.top + it.height}; }
inline D2D1_RECT_F RemoveRect(const Item& it) { using namespace Layout; const auto r = RowRect(it); const float cy = (r.top + r.bottom) / 2; return {r.right - infoRight - remove, cy - remove / 2, r.right - infoRight, cy + remove / 2}; }

struct Hooks {
    std::function<bool()> armed;           // P7: the host's arming (visible, still, foreground ≥ 500 ms)
    std::function<void(int)> pick;          // a selectable row (accounts, infos, footer), chosen with the pointer
    std::function<void(int)> remove;        // « Retirer » of an info row (its selectable index)
};

class List {
    HWND hwnd_ = nullptr; Chrome chrome_; Chrome* surface_ = &chrome_; PopupKit::GlyphCache glyphs_; Hooks hooks_;
    Content content_; bool visible_ = false, tracking_ = false;
    int hover_ = -1, selected_ = -1, pressed_ = -1; bool hoverRemove_ = false, pressRemove_ = false, pressArmed_ = false;
    UINT dpi_ = 96;
    void HitTest(float x, float y, int& row, bool& onRemove) const {
        row = -1; onRemove = false;
        for (const auto& it : Items(content_)) {
            if (it.row < 0) continue; const auto r = RowRect(it);
            if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) { row = it.row; onRemove = it.kind == Item::Kind::Info && PopupKit::Contains(RemoveRect(it), x, y); return; }
        }
    }
    // One run of text with an optional SemiBold head in another colour (the typed letters), clipped to `box`.
    void Run(Chrome& c, const std::wstring& text, std::size_t strong, D2D1_RECT_F box, const PopupKit::Type& type, D2D1_COLOR_F rest, D2D1_COLOR_F head, bool semibold) {
        auto* t = c.RenderTarget(); auto* b = c.SolidBrush(); if (!t || !b || text.empty()) return;
        auto* layout = PopupKit::MakeLayout(c, text, 8192.f, type, false); if (!layout) return;
        DWRITE_TRIMMING none{DWRITE_TRIMMING_GRANULARITY_NONE, 0, 0}; layout->SetTrimming(&none, nullptr);   // clipped, never ellipsised (board)
        if (semibold) layout->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, {0, static_cast<UINT32>(text.size())});
        ID2D1SolidColorBrush* emphasis = nullptr; strong = std::min(strong, text.size());
        if (strong && SUCCEEDED(t->CreateSolidColorBrush(head, &emphasis))) { const DWRITE_TEXT_RANGE r{0, static_cast<UINT32>(strong)}; layout->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, r); layout->SetDrawingEffect(emphasis, r); }
        t->PushAxisAlignedClip(box, D2D1_ANTIALIAS_MODE_ALIASED);
        b->SetColor(rest); t->DrawTextLayout(D2D1::Point2F(box.left, box.top), layout, b, D2D1_DRAW_TEXT_OPTIONS_NONE);
        t->PopAxisAlignedClip();
        if (emphasis) emphasis->Release();
        layout->Release();
    }
    // The key cap « Entrée » right-aligned at `right`, vertically centred on `cy`; returns its left edge.
    float Cap(Chrome& c, float right, float cy) {
        using namespace Layout; const PopupKit::Type type{capSize, capLine, 0, true}; const std::wstring label = TR(key_enter);
        const float w = std::ceil(PopupKit::Measure(c, label, 8192, type, false).width) + 2 * capPad;
        const D2D1_RECT_F box{right - w, cy - capHeight / 2, right, cy + capHeight / 2};
        PopupKit::Fill(c, box, capRadius, PopupKit::White(capPlate));
        PopupKit::DrawText(c, label, {box.left + capPad, cy - capLine / 2, box.right, cy + capLine / 2}, type, PopupKit::White(capText));
        return box.left;
    }
    void Paint() {
        Chrome& c = *surface_; using namespace Layout;
        if (!c.RenderTarget()) c.Resize();
        auto* t = c.RenderTarget(); if (!t || !c.SolidBrush()) return;
        const float h = Height(content_);
        t->BeginDraw(); t->Clear(PopupKit::Hex(surface)); PopupKit::Fill(c, {0, 0, width, h}, radius, PopupKit::Hex(surface));
        const PopupKit::Type title{titleSize, titleLine, 0, true}, text{textSize, textLine, textTracking}, dots{dotsSize, dotsLine, dotsTracking}, footer{titleSize, titleLine}, initial{initialSize, initialSize};
        for (const auto& it : Items(content_)) {
            const auto r = RowRect(it);
            const bool lit = it.row >= 0 && (it.row == hover_ || it.row == selected_);
            if (lit && it.kind != Item::Kind::Separator) PopupKit::Fill(c, r, rowRadius, PopupKit::Hex(hover));
            switch (it.kind) {
            case Item::Kind::Title: {
                const std::wstring& s = it.index == 0 ? content_.accountsTitle : content_.infosTitle;
                const float top = it.top + (it.second ? secondTitleTop : titleTop);
                PopupKit::DrawText(c, s, {r.left + titleInset, top, r.right - titleInset, top + titleLine}, title, PopupKit::White(titleAlpha));
                break; }
            case Item::Kind::Account: {
                const std::wstring& user = content_.accounts[it.index];
                const float cy = (r.top + r.bottom) / 2;
                const D2D1_RECT_F disc{r.left + rowLeft, cy - avatar / 2, r.left + rowLeft + avatar, cy + avatar / 2};
                if (auto* b = c.SolidBrush()) { b->SetColor(PopupKit::Hex(avatarFill)); t->FillEllipse(D2D1::Ellipse({(disc.left + disc.right) / 2, cy}, avatar / 2, avatar / 2), b); }
                std::wstring letter = user.empty() ? L"" : std::wstring(1, static_cast<wchar_t>(towupper(user[0])));
                if (!letter.empty()) {
                    auto* layout = PopupKit::MakeLayout(c, letter, avatar, initial, false, DWRITE_TEXT_ALIGNMENT_CENTER);
                    if (layout) { layout->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, {0, static_cast<UINT32>(letter.size())}); if (auto* b = c.SolidBrush()) { b->SetColor(PopupKit::White(initialAlpha)); t->DrawTextLayout(D2D1::Point2F(disc.left, cy - initialSize / 2), layout, b, D2D1_DRAW_TEXT_OPTIONS_NONE); } layout->Release(); }
                }
                const float textRight = it.row == selected_ ? Cap(c, r.right - rowLeft, cy) - rowGap : r.right - rowLeft;
                const float left = r.left + rowLeft + slot + rowGap, top = cy - (textLine + columnGap + dotsLine) / 2;
                Run(c, user, 0, {left, top, textRight, top + textLine}, text, PopupKit::White(1), PopupKit::White(1), false);
                Run(c, std::wstring(12, L'\x2022'), 0, {left, top + textLine + columnGap, textRight, top + textLine + columnGap + dotsLine}, dots, PopupKit::White(dotsAlpha), PopupKit::White(dotsAlpha), false);
                break; }
            case Item::Kind::Info: {
                const float cy = (r.top + r.bottom) / 2;
                glyphs_.Draw(c, Glyph::envelope, {r.left + rowLeft, cy - icon / 2, r.left + rowLeft + slot, cy + icon / 2}, icon, PopupKit::White(iconAlpha));
                const bool chosen = it.row == selected_;
                const float cap = chosen ? Cap(c, r.right - infoRight, cy) : 0;
                const float left = r.left + rowLeft + slot + rowGap, right = chosen ? cap - rowGap : r.right - infoRight - remove - rowGap;
                Run(c, content_.infos[it.index], content_.typed, {left, cy - textLine / 2, right, cy + textLine / 2}, text, PopupKit::White(restAlpha), PopupKit::White(1), false);
                if (!chosen && it.row == hover_) {
                    if (hoverRemove_) PopupKit::Fill(c, RemoveRect(it), rowRadius, PopupKit::White(pressRemove_ && pressed_ == it.row ? removePressed : removeHover));
                    glyphs_.Draw(c, Glyph::close, RemoveRect(it), removeIcon, PopupKit::White(restAlpha));
                }
                break; }
            case Item::Kind::Separator:
                PopupKit::Fill(c, {r.left + separatorInset, it.top, r.right - separatorInset, it.top + 1}, 0, PopupKit::White(separatorAlpha));
                break;
            case Item::Kind::Footer: {
                const float cy = (r.top + r.bottom) / 2;
                glyphs_.Draw(c, Glyph::sliders, {r.left + rowLeft, cy - icon / 2, r.left + rowLeft + slot, cy + icon / 2}, icon, PopupKit::White(iconAlpha));
                PopupKit::DrawText(c, content_.footer, {r.left + rowLeft + slot + rowGap, cy - titleLine / 2, r.right - rowLeft, cy + titleLine / 2}, footer, PopupKit::White(restAlpha));
                break; }
            }
        }
        PopupKit::Outline(c, width, h, radius, PopupKit::White(borderAlpha));
        if (t->EndDraw() == D2DERR_RECREATE_TARGET) c.RecreateTarget();
    }
    void Pointer(float x, float y) {
        int row; bool onRemove; HitTest(x, y, row, onRemove);
        if (row == hover_ && onRemove == hoverRemove_) return;
        hover_ = row; hoverRemove_ = onRemove; InvalidateRect(hwnd_, nullptr, FALSE);
    }
    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<List*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (msg == WM_NCCREATE) { self = static_cast<List*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); self->hwnd_ = h; }
        return self ? self->Handle(h, msg, w, l) : DefWindowProcW(h, msg, w, l);
    }
    LRESULT Handle(HWND h, UINT msg, WPARAM w, LPARAM l) {
        const float scale = 96.f / dpi_, x = GET_X_LPARAM(l) * scale, y = GET_Y_LPARAM(l) * scale;
        switch (msg) {
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;   // P6: the page keeps the focus (no blur)
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); Paint(); EndPaint(h, &ps); return 0; }
        case WM_SIZE: case WM_DPICHANGED: chrome_.Resize(); InvalidateRect(h, nullptr, FALSE); return 0;
        case WM_MOUSEMOVE:
            if (!tracking_) { TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, h, 0}; TrackMouseEvent(&t); tracking_ = true; }
            Pointer(x, y); return 0;
        case WM_MOUSELEAVE: tracking_ = false; if (GetCapture() != h) { hover_ = -1; hoverRemove_ = false; InvalidateRect(h, nullptr, FALSE); } return 0;
        case WM_LBUTTONDOWN:
            // P7: a press that began before arming never chooses anything, even if released later.
            Pointer(x, y); pressed_ = hover_; pressRemove_ = hoverRemove_; pressArmed_ = hooks_.armed && hooks_.armed();
            if (pressed_ >= 0) SetCapture(h);
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_LBUTTONUP: {
            const int was = pressed_; const bool wasRemove = pressRemove_, armedAtPress = pressArmed_;
            pressed_ = -1; pressRemove_ = pressArmed_ = false; if (GetCapture() == h) ReleaseCapture();
            Pointer(x, y);
            InvalidateRect(h, nullptr, FALSE);
            if (was >= 0 && was == hover_ && wasRemove == hoverRemove_ && armedAtPress && hooks_.armed && hooks_.armed()) {
                if (wasRemove) { if (hooks_.remove) hooks_.remove(was); } else if (hooks_.pick) hooks_.pick(was);
            }
            return 0; }
        case WM_CAPTURECHANGED: pressed_ = -1; pressRemove_ = pressArmed_ = false; return 0;
        }
        return DefWindowProcW(h, msg, w, l);
    }
public:
    ~List() { Dispose(); }
    bool Create(HINSTANCE instance, HWND owner, const std::wstring& root, Hooks hooks) {
        hooks_ = std::move(hooks);
        if (!PopupKit::Register(instance, L"BrowserFieldList", Proc)) return false;
        if (!PopupKit::CreatePopup(instance, owner, L"BrowserFieldList", TR(forms_window_name), WS_EX_NOACTIVATE, this, Layout::radius)) return false;
        chrome_.textCacheLimit = 64;
        return SUCCEEDED(chrome_.Init(hwnd_, root));
    }
    void Dispose() { glyphs_.Clear(); chrome_.Dispose(); if (hwnd_ && IsWindow(hwnd_)) DestroyWindow(hwnd_); hwnd_ = nullptr; visible_ = false; }
    HWND Window() const { return hwnd_; }
    bool Visible() const { return visible_; }
    const Content& Current() const { return content_; }
    RECT Bounds() const { RECT r{}; if (hwnd_) GetWindowRect(hwnd_, &r); return r; }
    int Selected() const { return selected_; }
    // Screen pixels at `dpi` of the list for `c` (the host places it, Forms::Place).
    static SIZE SizeFor(const Content& c, UINT dpi) { return {static_cast<LONG>(std::lround(Layout::width * dpi / 96.f)), static_cast<LONG>(std::lround(Height(c) * dpi / 96.f))}; }
    void Show(Content c, const RECT& at, UINT dpi) {
        if (!hwnd_) return;
        const bool same = visible_ && c.Count() == content_.Count();
        content_ = std::move(c); dpi_ = dpi ? dpi : 96;
        if (!same) { hover_ = selected_ = -1; hoverRemove_ = false; }
        SetWindowPos(hwnd_, HWND_TOP, at.left, at.top, at.right - at.left, at.bottom - at.top, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        PopupKit::SetAlpha(hwnd_, 1); chrome_.Resize();
        visible_ = true; InvalidateRect(hwnd_, nullptr, FALSE); UpdateWindow(hwnd_);
    }
    void Hide() {
        if (!hwnd_ || !visible_) return;
        visible_ = false; hover_ = selected_ = pressed_ = -1; hoverRemove_ = pressRemove_ = pressArmed_ = false;
        if (GetCapture() == hwnd_) ReleaseCapture();
        ShowWindow(hwnd_, SW_HIDE);
    }
    void SetSelected(int row) { if (row == selected_) return; selected_ = row; if (visible_) { InvalidateRect(hwnd_, nullptr, FALSE); UpdateWindow(hwnd_); } }
    // Tests: the same Paint() into an offscreen PNG (software Direct2D), `hover` = the row under the pointer.
    bool RenderForTest(const std::wstring& root, const Content& c, int hover, UINT dpi, const wchar_t* path) {
        content_ = c; hover_ = hover; selected_ = -1;
        return PopupKit::RenderPng(root, Layout::width, Height(c), dpi, path, surface_, [this] { Paint(); });
    }
    void HoverForTest(int row, bool onRemove) { hover_ = row; hoverRemove_ = onRemove; }
    // Tests: a state of the board « états » (selected row with ↓, the cross hovered or pressed).
    bool RenderStateForTest(const std::wstring& root, const Content& c, int hover, bool onRemove, bool pressed, int selected, UINT dpi, const wchar_t* path) {
        content_ = c; hover_ = hover; hoverRemove_ = onRemove; pressRemove_ = pressed; pressed_ = pressed ? hover : -1; selected_ = selected;
        const bool ok = PopupKit::RenderPng(root, Layout::width, Height(c), dpi, path, surface_, [this] { Paint(); });
        pressRemove_ = false; pressed_ = -1; return ok;
    }
};
} // namespace NativeChrome::FieldList
