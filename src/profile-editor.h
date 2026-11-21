#pragma once
#include "i18n.h"
// Profile editor block (Paper p-I-0: P03 « Modale nouveau profil » Y0D-0, P02 detail XWS-0, P06):
// name field, 12-icon radio grid, colour (hue + intensity sliders, live preview, free hex).
// One component, two hosts: the « Nouveau profil » dialog (profile-dialog.h) and Réglages ›
// Profils › <profil> (settings-panel.h). The host owns the window, paints the block where it wants
// it (Paint(origin)) and forwards pointer, keys and WM_CTLCOLOREDIT; the block owns two native
// EDIT children of the host (name, hex) and its UI Automation nodes.
//
//   label 14/20 · gap 8 · field h32 r8 #FFFFFF14 pad 10 (16/20 text) · gap 8 · help 14/18 #FFFFFF80
//   icon grid 6 × 2 cells 32 r8 gap 4 (212 wide); glyph 16 #FFFFFFB3; selected: ink 16 % + inset
//     1.5 px white, glyph in ink; keyboard focus: + 2 px #FFFFFF80 outside
//   colour rows h24 gap 8: track (flex) 12 high r6 at y 6 + value lane 36 (13/16 Medium tnum 50 %)
//     thumb: vertical bar 4 × 20 r2 white, 1 px black 32 % edge; dragging 6 × 24 + shadow;
//     keyboard focus: a 3 px #FFFFFF80 ring
//   preview row (pt 4, gap 8): plate 32 ink 10 % + glyph · « Aa » 40 × 32 on the accent fill ·
//     hex field 96 × 32 #FFFFFF14 r8 pad 10, 14/20 tnum
// Keys: icons ← → ↑ ↓ / Début / Fin (radio group); sliders ← → 1, Maj 10, Page↑↓ 30, Début/Fin,
// the hue wraps 359 ↔ 0 from the keyboard; full-lane 24 DIP hit area, jump on press, x-only 1:1 drag.
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>
#include "chrome.h"
#include "accessibility.h"
#include "tooltip.h"            // PopupKit
#include "profile-icons.h"
#include "profile-color.h"
#include "profiles.h"

namespace NativeChrome {
namespace ProfileEditor {

namespace Layout {
inline constexpr float label = 20, labelGap = 8, field = 32, help = 18, fieldPad = 10;
inline constexpr float cell = 32, cellGap = 4, cellRadius = 8, glyph = 16; inline constexpr int columns = 6;
inline constexpr float sliderRow = 24, rowGap = 8, track = 12, trackRadius = 6, valueLane = 36, laneGap = 8;
inline constexpr float thumbW = 4, thumbH = 20, dragW = 6, dragH = 24, focusRing = 3;
inline constexpr float previewTop = 4, plate = 32, chipW = 40, hexW = 96;
inline constexpr float GridWidth() { return columns * cell + (columns - 1) * cellGap; } // 212
}
static_assert(Layout::GridWidth() == 212, "Paper icon grid: 6 × 32 + 5 × 4");

inline D2D1_COLOR_F Color(ProfileColor::Rgb8 c, float a = 1) { return D2D1::ColorF(c.r / 255.f, c.g / 255.f, c.b / 255.f, a); }
inline ProfileColor::Rgb8 InkOf(const Profiles::Profile& p) { return p.neutral ? ProfileColor::Neutral() : ProfileColor::Ink(p.hue, p.intensity); }
inline ProfileColor::Rgb8 FillOf(const Profiles::Profile& p) { return p.neutral ? ProfileColor::NeutralFill() : ProfileColor::AccentFill(p.hue, p.intensity); }
inline int IntensityPercent(const Profiles::Profile& p) { return p.neutral ? 0 : static_cast<int>(std::lround(std::clamp(p.intensity, 0.f, ProfileColor::MaxChroma) / ProfileColor::MaxChroma * 100)); }
inline void SetIntensityPercent(Profiles::Profile& p, int percent) {
    percent = std::clamp(percent, 0, 100); p.intensity = percent / 100.f * ProfileColor::MaxChroma; p.neutral = percent == 0;
}
inline std::wstring NameErrorText(Profiles::NameError e) {
    switch (e) {
    case Profiles::NameError::Empty: return TR(profile_name_empty);
    case Profiles::NameError::TooLong: return TR(profile_name_max);
    case Profiles::NameError::Duplicate: return TR(profile_name_duplicate);
    default: return L"";
    }
}

enum class Style { Dialog, Drawer };   // Dialog: « Nom » 14/20 #FFFFFFB3; Drawer: 14/20 Medium #FFFFFF80 + longer help
enum Stop : int { None = 0, Name = 1, Icons = 2, Hue = 3, Intensity = 4, Hex = 5 };

class Editor {
public:
    struct Hooks {
        std::function<void()> invalidate;
        std::function<void(const Profiles::Profile&, Stop what)> changed;     // icon/colour live; name only once committed (drawer)
        std::function<Profiles::NameError(const std::wstring&)> validateName; // against the registry
        std::function<void(Stop)> focusChanged;                                // a stop took the focus (host focus bookkeeping)
        std::function<void(int)> tabOut;                                       // Tab past the last / before the first stop (+1 / -1)
        std::function<void()> submit;                                          // Entrée in the name field (dialog: create)
    };
private:
    HWND host_ = nullptr; Hooks hooks_; Style style_ = Style::Dialog;
    Profiles::Profile value_; std::wstring savedName_; Profiles::NameError error_ = Profiles::NameError::None; bool showError_ = false;
    int focus_ = None; bool keyboard_ = false; int hover_ = None, hoverCell_ = -1; int dragging_ = None;
    HWND name_ = nullptr, hex_ = nullptr; HFONT nameFont_ = nullptr, hexFont_ = nullptr; HBRUSH editBrush_ = nullptr; UINT fontDpi_ = 0;
    D2D1_POINT_2F origin_{}; float width_ = 368; float scale_ = 1;
    PopupKit::GlyphCache glyphs_;
    bool syncing_ = false;

    // The drawer's help wraps to two lines (« … Enregistré avec Entrée ou en quittant le champ. »).
    float HelpHeight() const { return style_ == Style::Drawer ? 2 * Layout::help : Layout::help; }
    float NameBlock() const { return Layout::label + Layout::labelGap + Layout::field + Layout::labelGap + HelpHeight(); }
    float IconBlock() const { return Layout::label + Layout::labelGap + 2 * Layout::cell + Layout::cellGap; }
    float ColorBlock() const { return 2 * Layout::sliderRow + 2 * Layout::rowGap + Layout::previewTop + Layout::plate; }
    float Gap() const { return style_ == Style::Drawer ? 20.f : 16.f; }
public:
    // ---- geometry (block-local DIP; origin added by the host) ----
    float Height() const { return NameBlock() + Gap() + IconBlock() + Gap() + ColorBlock(); }
    D2D1_RECT_F NameField() const { const float y = Layout::label + Layout::labelGap; return {0, y, width_, y + Layout::field}; }
    float IconsTop() const { return NameBlock() + Gap() + Layout::label + Layout::labelGap; }
    D2D1_RECT_F Cell(int i) const { const int c = i % Layout::columns, r = i / Layout::columns; const float x = c * (Layout::cell + Layout::cellGap), y = IconsTop() + r * (Layout::cell + Layout::cellGap); return {x, y, x + Layout::cell, y + Layout::cell}; }
    float ColorTop() const { return NameBlock() + Gap() + IconBlock() + Gap(); }
    D2D1_RECT_F Lane(int which) const { const float y = ColorTop() + (which == Hue ? 0 : Layout::sliderRow + Layout::rowGap); return {0, y, width_ - Layout::valueLane - Layout::laneGap, y + Layout::sliderRow}; }
    D2D1_RECT_F Track(int which) const { auto l = Lane(which); const float y = l.top + (Layout::sliderRow - Layout::track) / 2; return {l.left, y, l.right, y + Layout::track}; }
    float PreviewTop() const { return ColorTop() + 2 * (Layout::sliderRow + Layout::rowGap) + Layout::previewTop; }
    D2D1_RECT_F Plate() const { const float y = PreviewTop(); return {0, y, Layout::plate, y + Layout::plate}; }
    D2D1_RECT_F Chip() const { const float y = PreviewTop(), x = Layout::plate + 8; return {x, y, x + Layout::chipW, y + Layout::plate}; }
    D2D1_RECT_F HexField() const { const float y = PreviewTop(), x = Layout::plate + 8 + Layout::chipW + 8; return {x, y, x + Layout::hexW, y + Layout::plate}; }
    static bool In(D2D1_RECT_F r, float x, float y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }
    // Fraction along a track for a value, and back (thumb centred on the value).
    float Fraction(int which) const { return which == Hue ? value_.hue / 359.f : IntensityPercent(value_) / 100.f; }
    float ThumbX(int which) const { const auto t = Track(which); return t.left + Layout::thumbW / 2 + Fraction(which) * (t.right - t.left - Layout::thumbW); }

    // ---- state ----
    const Profiles::Profile& Value() const { return value_; }
    std::wstring NameText() const { if (!name_) return value_.name; const int n = GetWindowTextLengthW(name_); std::wstring s(static_cast<size_t>(n) + 1, L'\0'); GetWindowTextW(name_, s.data(), n + 1); s.resize(static_cast<size_t>(n)); return s; }
    void SetValue(const Profiles::Profile& p) {
        value_ = p; savedName_ = p.name; error_ = Profiles::NameError::None; showError_ = false;
        if (!Profiles::IconKnown(value_.icon)) value_.icon = Profiles::DefaultIcon;
        syncing_ = true; if (name_) SetWindowTextW(name_, p.name.c_str()); SyncHex(); syncing_ = false; Invalidate();
    }
    int Focus() const { return focus_; }
    bool Dragging() const { return dragging_ != None; }
    Profiles::NameError Error() const { return showError_ ? error_ : Profiles::NameError::None; }
    void ShowNameError(Profiles::NameError e) { error_ = e; showError_ = e != Profiles::NameError::None; Invalidate(); }
    std::vector<int> Stops() const { return {Name, Icons, Hue, Intensity, Hex}; }
    void SetStyle(Style s) { style_ = s; }
    void SetWidth(float w) { width_ = w; }

    // ---- life cycle ----
    void Attach(HWND host, Hooks hooks, Style style) {
        host_ = host; hooks_ = std::move(hooks); style_ = style;
        name_ = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 10, 10, host, nullptr, GetModuleHandleW(nullptr), nullptr);
        hex_ = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 10, 10, host, nullptr, GetModuleHandleW(nullptr), nullptr);
        for (HWND e : {name_, hex_}) if (e) { SetWindowSubclass(e, EditProc, 0, reinterpret_cast<DWORD_PTR>(this)); SendMessageW(e, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0); NativeUI::EditBuffer::Attach(e); }
        if (name_) SendMessageW(name_, EM_LIMITTEXT, 2 * Profiles::MaxNameLength, 0);   // UTF-16 units; the 32 characters are enforced in WM_CHAR
        if (hex_) SendMessageW(hex_, EM_LIMITTEXT, 7, 0);
        editBrush_ = CreateSolidBrush(RGB(0x2C, 0x2C, 0x2C)); // #FFFFFF14 over #171717
    }
    void Detach() {
        for (HWND* e : {&name_, &hex_}) if (*e) { RemoveWindowSubclass(*e, EditProc, 0); DestroyWindow(*e); *e = nullptr; }
        if (nameFont_) { DeleteObject(nameFont_); nameFont_ = nullptr; } if (hexFont_) { DeleteObject(hexFont_); hexFont_ = nullptr; }
        if (editBrush_) { DeleteObject(editBrush_); editBrush_ = nullptr; }
        glyphs_.Clear(); host_ = nullptr;
    }
    ~Editor() { Detach(); }
    // The host answers WM_CTLCOLOREDIT with this for our two edits (white text on the field fill).
    bool CtlColor(HWND edit, HDC dc, LRESULT& result) {
        if (edit != name_ && edit != hex_) return false;
        NativeUI::EditBuffer::Clip(edit, dc);
        SetTextColor(dc, RGB(255, 255, 255)); SetBkColor(dc, RGB(0x2C, 0x2C, 0x2C)); result = reinterpret_cast<LRESULT>(editBrush_); return true;
    }
    bool Owns(HWND h) const { return h && (h == name_ || h == hex_); }
    // From the host's WM_PAINT, once the block is painted: the two fields follow in the same turn.
    void Flush() const { NativeUI::EditBuffer::Flush(name_); NativeUI::EditBuffer::Flush(hex_); }
    // Places the native edits over the painted fields (`origin` in host client DIP, `scale` px/DIP).
    // `visible`: hidden while the host scrolls/animates the block out of view.
    void Place(D2D1_POINT_2F origin, float scale, bool visible) {
        origin_ = origin; scale_ = scale; if (!host_) return;
        const UINT dpi = static_cast<UINT>(std::lround(96 * scale));
        if (dpi != fontDpi_) {
            if (nameFont_) DeleteObject(nameFont_); if (hexFont_) DeleteObject(hexFont_);
            nameFont_ = CreateFontW(-MulDiv(16, dpi, 72) * 72 / 96, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Inter Display");
            hexFont_ = CreateFontW(-MulDiv(14, dpi, 72) * 72 / 96, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Inter Display");
            if (name_) SendMessageW(name_, WM_SETFONT, reinterpret_cast<WPARAM>(nameFont_), TRUE); if (hex_) SendMessageW(hex_, WM_SETFONT, reinterpret_cast<WPARAM>(hexFont_), TRUE);
            fontDpi_ = dpi;
        }
        auto put = [&](HWND e, D2D1_RECT_F r, float textH) {
            if (!e) return; const float y = r.top + (r.bottom - r.top - textH) / 2;
            const int x0 = static_cast<int>(std::lround((origin.x + r.left + Layout::fieldPad) * scale)), y0 = static_cast<int>(std::lround((origin.y + y) * scale));
            const int x1 = static_cast<int>(std::lround((origin.x + r.right - Layout::fieldPad) * scale)), y1 = static_cast<int>(std::lround((origin.y + y + textH) * scale));
            SetWindowPos(e, HWND_TOP, x0, y0, x1 - x0, y1 - y0, SWP_NOACTIVATE | (visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
        };
        put(name_, NameField(), 20); put(hex_, HexField(), 20);
    }
    void Hide() { if (name_) ShowWindow(name_, SW_HIDE); if (hex_) ShowWindow(hex_, SW_HIDE); }

    // ---- focus ----
    void SetFocusStop(int stop, bool keyboard) {
        keyboard_ = keyboard; const int before = focus_; focus_ = stop;
        if (stop == Name && name_) { SetFocus(name_); SendMessageW(name_, EM_SETSEL, 0, -1); }
        else if (stop == Hex && hex_) { SetFocus(hex_); SendMessageW(hex_, EM_SETSEL, 0, -1); }
        else if (host_ && (GetFocus() == name_ || GetFocus() == hex_)) SetFocus(host_);
        if (before != stop && hooks_.focusChanged) hooks_.focusChanged(static_cast<Stop>(stop));
        Invalidate();
    }
    void Blur() { if (focus_ == Name) CommitName(false); if (focus_ == Hex) CommitHex(); focus_ = None; if (host_ && (GetFocus() == name_ || GetFocus() == hex_)) SetFocus(host_); Invalidate(); }
    // Tab inside the block: true when it moved to another stop of the block.
    bool Tab(bool back) {
        const auto s = Stops(); auto it = std::find(s.begin(), s.end(), focus_);
        if (it == s.end()) { SetFocusStop(back ? s.back() : s.front(), true); return true; }
        if (focus_ == Name) CommitName(false); if (focus_ == Hex) CommitHex();
        if (back ? it == s.begin() : it + 1 == s.end()) { focus_ = None; Invalidate(); if (hooks_.tabOut) hooks_.tabOut(back ? -1 : 1); return false; }
        SetFocusStop(back ? *(it - 1) : *(it + 1), true); return true;
    }

    // ---- input (block-local DIP) ----
    int HitStop(float x, float y, int* cellOut = nullptr) const {
        if (In(NameField(), x, y)) return Name;
        for (int i = 0; i < ProfileIcons::count; ++i) if (In(Cell(i), x, y)) { if (cellOut) *cellOut = i; return Icons; }
        if (In(Lane(Hue), x, y)) return Hue;
        if (In(Lane(Intensity), x, y)) return Intensity;
        if (In(HexField(), x, y)) return Hex;
        return None;
    }
    bool PointerMove(float x, float y) {
        if (dragging_ != None) { DragTo(dragging_, x); return true; }
        int cell = -1; const int stop = HitStop(x, y, &cell);
        if (stop != hover_ || cell != hoverCell_) { hover_ = stop; hoverCell_ = stop == Icons ? cell : -1; Invalidate(); }
        return stop != None;
    }
    void PointerLeave() { if (hover_ != None || hoverCell_ >= 0) { hover_ = None; hoverCell_ = -1; Invalidate(); } }
    // Returns true when the press belongs to the block (the host captures the pointer for a drag).
    bool PointerDown(float x, float y) {
        int cell = -1; const int stop = HitStop(x, y, &cell);
        if (stop == None) return false;
        keyboard_ = false;
        if (stop == Icons) { if (focus_ != Icons) SetFocusStop(Icons, false); SelectIcon(cell); return true; }
        if (stop == Hue || stop == Intensity) { if (focus_ != stop) SetFocusStop(stop, false); dragging_ = stop; DragTo(stop, x); return true; }
        SetFocusStop(stop, false); return true;
    }
    void PointerUp() { if (dragging_ != None) { const int d = dragging_; dragging_ = None; Invalidate(); if (hooks_.changed) hooks_.changed(value_, static_cast<Stop>(d)); } }
    void DragTo(int which, float x) {
        const auto t = Track(which); const float u = std::clamp((x - t.left - Layout::thumbW / 2) / std::max(1.f, t.right - t.left - Layout::thumbW), 0.f, 1.f);
        if (which == Hue) SetHue(static_cast<int>(std::lround(u * 359)), false); else SetIntensity(static_cast<int>(std::lround(u * 100)), false);
    }
    void SetHue(int h, bool commit = true) {
        h = ((h % 360) + 360) % 360; if (h == value_.hue && !value_.neutral) return;
        value_.hue = h; if (value_.neutral) SetIntensityPercent(value_, 88);   // a hue on a grey profile brings its colour back
        (void)commit; SyncHex(); Invalidate(); if (hooks_.changed) hooks_.changed(value_, Hue);
    }
    void SetIntensity(int percent, bool commit = true) {
        (void)commit; percent = std::clamp(percent, 0, 100); if (percent == IntensityPercent(value_)) return;
        SetIntensityPercent(value_, percent); SyncHex(); Invalidate(); if (hooks_.changed) hooks_.changed(value_, Intensity);
    }
    void SelectIcon(int i) {
        if (i < 0 || i >= ProfileIcons::count) return; const std::wstring key = ProfileIcons::all[i].key;
        if (key == value_.icon) { Invalidate(); return; }
        value_.icon = key; Invalidate(); if (hooks_.changed) hooks_.changed(value_, Icons);
    }
    int IconIndex() const { return ProfileIcons::IndexOf(value_.icon.c_str()); }
    // Keys while a painted stop (icons, sliders) has the focus; the edits handle their own.
    bool Key(WPARAM key) {
        const bool shift = GetKeyState(VK_SHIFT) < 0; keyboard_ = true;
        if (key == VK_TAB) { Tab(shift); return true; }
        if (focus_ == Icons) {
            int i = IconIndex(); const int n = ProfileIcons::count, c = Layout::columns;
            switch (key) {
            case VK_LEFT: i = (i + n - 1) % n; break; case VK_RIGHT: i = (i + 1) % n; break;
            case VK_UP: i = (i + n - c) % n; break; case VK_DOWN: i = (i + c) % n; break;
            case VK_HOME: i = 0; break; case VK_END: i = n - 1; break;
            default: return false;
            }
            SelectIcon(i); return true;
        }
        if (focus_ == Hue || focus_ == Intensity) {
            const int step = shift ? 10 : 1; const bool hue = focus_ == Hue;
            const int v = hue ? value_.hue : IntensityPercent(value_); int next = v;
            switch (key) {
            case VK_LEFT: case VK_DOWN: next = v - step; break; case VK_RIGHT: case VK_UP: next = v + step; break;
            case VK_PRIOR: next = v + 30; break; case VK_NEXT: next = v - 30; break;
            case VK_HOME: next = 0; break; case VK_END: next = hue ? 359 : 100; break;
            default: return false;
            }
            if (hue) SetHue(((next % 360) + 360) % 360); else SetIntensity(std::clamp(next, 0, 100));
            return true;
        }
        return false;
    }

    // ---- name / hex commits ----
    // Entrée (stay) on an invalid name: error under the field, focus kept. Leaving the field: back to
    // the last valid name, silently (Paper P02 rules). Returns true when the name is valid.
    bool CommitName(bool stay) {
        const std::wstring text = Profiles::TrimName(NameText());
        const auto e = hooks_.validateName ? hooks_.validateName(text) : (text.empty() ? Profiles::NameError::Empty : Profiles::NameError::None);
        if (e != Profiles::NameError::None) {
            if (stay) { ShowNameError(e); return false; }
            if (style_ == Style::Drawer) { syncing_ = true; if (name_) SetWindowTextW(name_, savedName_.c_str()); syncing_ = false; value_.name = savedName_; showError_ = false; Invalidate(); }
            return false;
        }
        showError_ = false; value_.name = text;
        if (text != savedName_) { savedName_ = text; if (hooks_.changed) hooks_.changed(value_, Name); }
        syncing_ = true; if (name_ && NameText() != text) SetWindowTextW(name_, text.c_str()); syncing_ = false;
        Invalidate(); return true;
    }
    void CommitHex() {
        if (!hex_) return; int n = GetWindowTextLengthW(hex_); std::wstring s(static_cast<size_t>(n) + 1, L'\0'); GetWindowTextW(hex_, s.data(), n + 1); s.resize(static_cast<size_t>(n));
        int hue = value_.hue; float chroma = value_.intensity;
        if (ProfileColor::FromHex(s, hue, chroma)) {
            value_.hue = hue; value_.intensity = chroma; value_.neutral = chroma <= 0;
            if (value_.neutral) value_.intensity = 0;
            if (hooks_.changed) hooks_.changed(value_, Hex);
        }
        SyncHex(); Invalidate();
    }
    void SyncHex() { if (!hex_) return; syncing_ = true; const auto t = ProfileColor::Hex(InkOf(value_)); SetWindowTextW(hex_, t.c_str()); syncing_ = false; }

    // ---- paint ----
    void Invalidate() { if (hooks_.invalidate) hooks_.invalidate(); }
    void Paint(Chrome& c, D2D1_POINT_2F origin, float opacity = 1) {
        auto* t = c.RenderTarget(); if (!t) return;
        D2D1_MATRIX_3X2_F base; t->GetTransform(&base); t->SetTransform(D2D1::Matrix3x2F::Translation(origin.x, origin.y) * base);
        const auto ink = InkOf(value_);
        const bool drawer = style_ == Style::Drawer;
        PopupKit::Type labelType{14, 20, 0, drawer}, field{16, 20, -.16f}, help{14, 18}, value{13, 16, 0, true, true};
        // Name
        PopupKit::DrawText(c, TR(bookmark_field_name), {0, 0, width_, Layout::label}, labelType, PopupKit::White((drawer ? .5f : .7f) * opacity));
        const auto nf = NameField();
        PopupKit::Fill(c, nf, 8, PopupKit::White((.08f + (hover_ == Name && focus_ != Name ? .02f : 0)) * opacity));
        // Focused field: keyboard = the 1 px #FFFFFF80 inset ring (Paper P03 « Prêt »); pointer = a discreet
        // 1 px #FFFFFF33 edge — never stacked with another ring (user correction 2026-11-14).
        if (focus_ == Name) Ring(c, nf, 8, (keyboard_ ? .5f : .2f) * opacity, 1, true);
        if (!name_ || !IsWindowVisible(name_)) PopupKit::DrawText(c, value_.name, {nf.left + Layout::fieldPad, nf.top + 6, nf.right - Layout::fieldPad, nf.bottom}, field, PopupKit::White(opacity));
        const float hy = nf.bottom + Layout::labelGap;
        if (showError_) PopupKit::DrawText(c, NameErrorText(error_), {0, hy, width_, hy + HelpHeight()}, help, D2D1::ColorF(Design::Danger::r, Design::Danger::g, Design::Danger::b, opacity), true);
        else PopupKit::DrawText(c, drawer ? TR(profile_name_max_drawer) : TR(profile_name_max), {0, hy, width_, hy + HelpHeight()}, help, PopupKit::White(.5f * opacity), drawer);
        // Icons
        const float iy = NameBlock() + Gap();
        PopupKit::DrawText(c, TR(profile_icon), {0, iy, width_, iy + Layout::label}, PopupKit::Type{14, 20}, PopupKit::White(.7f * opacity));
        const int selected = IconIndex();
        for (int i = 0; i < ProfileIcons::count; ++i) {
            const auto r = Cell(i); const bool on = i == selected;
            if (on) { PopupKit::Fill(c, r, Layout::cellRadius, Color(ink, .16f * opacity)); Ring(c, r, Layout::cellRadius, opacity, 1.5f, true); }
            else if (hover_ == Icons && hoverCell_ == i) PopupKit::Fill(c, r, Layout::cellRadius, PopupKit::White(.06f * opacity));
            if (on && focus_ == Icons && keyboard_) Ring(c, {r.left - 2, r.top - 2, r.right + 2, r.bottom + 2}, Layout::cellRadius + 2, .5f * opacity, 2, false);
            glyphs_.Draw(c, ProfileIcons::all[i].fill, r, Layout::glyph, on ? Color(ink, opacity) : PopupKit::White(.7f * opacity));
        }
        // Colour
        Slider(c, Hue, opacity); Slider(c, Intensity, opacity);
        for (int which : {Hue, Intensity}) {
            const auto l = Lane(which); const std::wstring v = which == Hue ? std::to_wstring(value_.hue) + L"°" : std::to_wstring(IntensityPercent(value_)) + L" %";
            PopupKit::DrawText(c, v, {l.right + Layout::laneGap, l.top + 4, width_, l.bottom}, value, PopupKit::White(.5f * opacity), false, DWRITE_TEXT_ALIGNMENT_CENTER);
        }
        const auto plate = Plate(), chip = Chip(), hex = HexField();
        PopupKit::Fill(c, plate, 8, Color(ink, .10f * opacity));
        glyphs_.Draw(c, ProfileIcons::Fill(value_.icon.c_str()), plate, Layout::glyph, Color(ink, opacity));
        PopupKit::Fill(c, chip, 8, Color(FillOf(value_), opacity));
        PopupKit::DrawText(c, L"Aa", {chip.left, chip.top + 7, chip.right, chip.bottom}, PopupKit::Type{14, 18, 0, true}, PopupKit::White(opacity), false, DWRITE_TEXT_ALIGNMENT_CENTER);
        PopupKit::Fill(c, hex, 8, PopupKit::White((.08f + (hover_ == Hex && focus_ != Hex ? .02f : 0)) * opacity));
        if (focus_ == Hex) Ring(c, hex, 8, (keyboard_ ? .5f : .2f) * opacity, 1, true);
        if (!hex_ || !IsWindowVisible(hex_)) PopupKit::DrawText(c, ProfileColor::Hex(ink), {hex.left + Layout::fieldPad, hex.top + 6, hex.right - 4, hex.bottom}, PopupKit::Type{14, 20, 0, false, true}, PopupKit::White(opacity));
        t->SetTransform(base);
    }
private:
    void Ring(Chrome& c, D2D1_RECT_F r, float radius, float alpha, float stroke, bool inset) {
        auto* t = c.RenderTarget(); auto* b = c.SolidBrush(); b->SetColor(PopupKit::White(alpha)); const float k = inset ? stroke / 2 : -stroke / 2;
        t->DrawRoundedRectangle(D2D1::RoundedRect({r.left + k, r.top + k, r.right - k, r.bottom - k}, std::max(0.f, radius - k), std::max(0.f, radius - k)), b, stroke);
    }
    void Slider(Chrome& c, int which, float opacity) {
        auto* t = c.RenderTarget(); const auto tr = Track(which);
        // Continuous gradient (sRGB stops sampled every 30° / every 25 % of the OKLCH ramp).
        std::vector<D2D1_GRADIENT_STOP> stops;
        if (which == Hue) for (int k = 0; k <= 12; ++k) stops.push_back({k / 12.f, Color(ProfileColor::Ink((k * 30) % 360, .14f), opacity)});
        else for (int k = 0; k <= 4; ++k) stops.push_back({k / 4.f, Color(k == 0 ? ProfileColor::Neutral() : ProfileColor::Ink(value_.hue, ProfileColor::MaxChroma * k / 4.f), opacity)});
        ID2D1GradientStopCollection* collection = nullptr; ID2D1LinearGradientBrush* brush = nullptr;
        if (SUCCEEDED(t->CreateGradientStopCollection(stops.data(), static_cast<UINT32>(stops.size()), &collection)) &&
            SUCCEEDED(t->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({tr.left, 0}, {tr.right, 0}), collection, &brush)))
            t->FillRoundedRectangle(D2D1::RoundedRect(tr, Layout::trackRadius, Layout::trackRadius), brush);
        if (brush) brush->Release(); if (collection) collection->Release();
        const float cx = ThumbX(which), cy = (tr.top + tr.bottom) / 2; const bool drag = dragging_ == which;
        const float w = drag ? Layout::dragW : Layout::thumbW, h = drag ? Layout::dragH : Layout::thumbH;
        const D2D1_RECT_F thumb{cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2};
        auto* b = c.SolidBrush();
        if (drag) { b->SetColor(D2D1::ColorF(0, 0, 0, .3f * opacity)); t->FillRoundedRectangle(D2D1::RoundedRect({thumb.left - 1, thumb.top + 1, thumb.right + 1, thumb.bottom + 2}, 3, 3), b); }
        b->SetColor(D2D1::ColorF(0, 0, 0, .32f * opacity)); t->FillRoundedRectangle(D2D1::RoundedRect({thumb.left - 1, thumb.top - 1, thumb.right + 1, thumb.bottom + 1}, 3, 3), b);
        b->SetColor(PopupKit::White(opacity)); t->FillRoundedRectangle(D2D1::RoundedRect(thumb, 2, 2), b);
        if (focus_ == which && keyboard_) Ring(c, {thumb.left - Layout::focusRing - 1, thumb.top - Layout::focusRing - 1, thumb.right + Layout::focusRing + 1, thumb.bottom + Layout::focusRing + 1}, 2 + Layout::focusRing, .5f * opacity, 1.5f, true);
    }
    static LRESULT CALLBACK EditProc(HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
        auto* self = reinterpret_cast<Editor*>(data); const bool isName = h == self->name_;
        switch (msg) {
        case WM_GETDLGCODE: return DLGC_WANTALLKEYS | DefSubclassProc(h, msg, w, l);
        case WM_KEYDOWN:
            if (w == VK_TAB) { self->keyboard_ = true; self->Tab(GetKeyState(VK_SHIFT) < 0); return 0; }
            if (w == VK_RETURN) { if (isName) { if (self->CommitName(true) && self->hooks_.submit) self->hooks_.submit(); } else self->CommitHex(); return 0; }
            if (w == VK_ESCAPE) {
                if (isName && self->style_ == Style::Drawer) { self->syncing_ = true; SetWindowTextW(h, self->savedName_.c_str()); self->syncing_ = false; self->showError_ = false; self->Invalidate(); }
                if (!isName) self->SyncHex();
                // Échap also reaches the host (dialog: Annuler; drawer: back / close).
                SendMessageW(GetParent(h), WM_KEYDOWN, VK_ESCAPE, 0); return 0;
            }
            break;
        case WM_CHAR: if (w == L'\r' || w == 27 || w == L'\t') return 0;
            // Name: typing stops at 32 user-perceived characters (a surrogate pair counts once).
            if (isName && w >= 0x20 && !IS_LOW_SURROGATE(static_cast<wchar_t>(w))) { DWORD a = 0, b = 0; SendMessageW(h, EM_GETSEL, reinterpret_cast<WPARAM>(&a), reinterpret_cast<LPARAM>(&b)); if (a == b && Profiles::NameLength(self->NameText()) >= Profiles::MaxNameLength) { MessageBeep(MB_OK); return 0; } }
            { const LRESULT r = DefSubclassProc(h, msg, w, l); if (isName && self->showError_) { self->showError_ = false; self->Invalidate(); } if (isName && self->style_ == Style::Dialog) { self->value_.name = Profiles::TrimName(self->NameText()); self->Invalidate(); } return r; }
        case WM_SETFOCUS: { const LRESULT r = DefSubclassProc(h, msg, w, l); const int stop = isName ? Name : Hex; if (self->focus_ != stop) { self->focus_ = stop; if (self->hooks_.focusChanged) self->hooks_.focusChanged(static_cast<Stop>(stop)); self->Invalidate(); } return r; }
        case WM_KILLFOCUS: { const LRESULT r = DefSubclassProc(h, msg, w, l); if (isName) self->CommitName(false); else self->CommitHex(); return r; }
        case WM_NCDESTROY: RemoveWindowSubclass(h, EditProc, 0); break;
        }
        return DefSubclassProc(h, msg, w, l);
    }
public:
    // ---- UI Automation (keys `base + stop`, cells `base + 100 + i`) ----
    std::vector<Accessibility::Node> Nodes(long long base, long long parent, RECT hostScreen) const {
        std::vector<Accessibility::Node> out;
        auto bounds = [&](D2D1_RECT_F r) { return RECT{hostScreen.left + static_cast<LONG>((origin_.x + r.left) * scale_), hostScreen.top + static_cast<LONG>((origin_.y + r.top) * scale_), hostScreen.left + static_cast<LONG>((origin_.x + r.right) * scale_), hostScreen.top + static_cast<LONG>((origin_.y + r.bottom) * scale_)}; };
        Accessibility::Node name; name.key = base + Name; name.parent = parent; name.role = Accessibility::Role::Edit; name.name = TR(bookmark_field_name); name.value = value_.name; name.help = showError_ ? NameErrorText(error_) : TR(profile_name_max); name.automationId = L"ProfileName"; name.bounds = bounds(NameField()); out.push_back(name);
        Accessibility::Node grid; grid.key = base + Icons; grid.parent = parent; grid.role = Accessibility::Role::List; grid.name = TR(profile_icon); grid.automationId = L"ProfileIcons"; grid.focusable = false;
        grid.bounds = bounds({0, IconsTop(), Layout::GridWidth(), IconsTop() + 2 * Layout::cell + Layout::cellGap}); out.push_back(grid);
        const int sel = IconIndex();
        for (int i = 0; i < ProfileIcons::count; ++i) {
            Accessibility::Node n; n.key = base + 100 + i; n.parent = base + Icons; n.role = Accessibility::Role::ListItem; n.name = I18n::T(ProfileIcons::all[i].label.id); n.selected = i == sel; n.position = i + 1; n.count = ProfileIcons::count; n.automationId = L"ProfileIcon"; n.bounds = bounds(Cell(i)); out.push_back(n);
        }
        for (int which : {Hue, Intensity}) {
            Accessibility::Node s; s.key = base + which; s.parent = parent; s.role = Accessibility::Role::Slider; s.automationId = which == Hue ? L"ProfileHue" : L"ProfileIntensity";
            s.name = which == Hue ? TR(profile_hue) : TR(profile_intensity); s.rangeMin = 0; s.rangeMax = which == Hue ? 359 : 100; s.smallChange = 1; s.largeChange = 10;
            s.rangeValue = which == Hue ? value_.hue : IntensityPercent(value_);
            s.value = which == Hue ? std::to_wstring(value_.hue) + L"°, " + ProfileColor::HueName(value_.hue) : std::to_wstring(IntensityPercent(value_)) + L" %";
            s.bounds = bounds(Lane(which)); out.push_back(s);
        }
        Accessibility::Node hex; hex.key = base + Hex; hex.parent = parent; hex.role = Accessibility::Role::Edit; hex.name = TR(profile_hex); hex.value = ProfileColor::Hex(InkOf(value_)); hex.automationId = L"ProfileHex"; hex.bounds = bounds(HexField()); out.push_back(hex);
        return out;
    }
    // UIA actions routed by the host for keys of this block.
    bool Accessible(long long base, long long key, int action, double range) {
        const long long k = key - base;
        if (k >= 100 && k < 100 + ProfileIcons::count) { SetFocusStop(Icons, true); SelectIcon(static_cast<int>(k - 100)); return true; }
        if (k == Hue && action == 2) { SetHue(static_cast<int>(std::lround(range))); return true; }
        if (k == Intensity && action == 2) { SetIntensity(static_cast<int>(std::lround(range))); return true; }
        if (k >= Name && k <= Hex) { SetFocusStop(static_cast<int>(k), true); return true; }
        return false;
    }
    // UIA ValuePattern on Nom / Couleur hexadécimale: the text is set as if typed, then committed.
    bool AccessibleValue(long long base, long long key, const std::wstring& text) {
        const long long k = key - base;
        if (k == Name) { syncing_ = true; if (name_) SetWindowTextW(name_, text.c_str()); else value_.name = text; syncing_ = false; if (style_ == Style::Dialog) value_.name = Profiles::TrimName(text); CommitName(style_ == Style::Dialog); Invalidate(); return true; }
        if (k == Hex) { if (hex_) SetWindowTextW(hex_, text.c_str()); CommitHex(); return true; }
        return false;
    }
    // Test hooks.
    void TestSetName(const std::wstring& s) { if (name_) SetWindowTextW(name_, s.c_str()); else value_.name = s; }
    void TestFocus(int stop) { focus_ = stop; keyboard_ = true; }
};

} // namespace ProfileEditor
} // namespace NativeChrome
