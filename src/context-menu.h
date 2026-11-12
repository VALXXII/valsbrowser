#pragma once
// Native context menu: one component for every right click (web pages through
// WebView2's ContextMenuRequested, tabs, the tab list, the address field),
// painted with Direct2D from the Paper frame "Clic droit menu — poli": 14 DIP
// text, 32 DIP rows, 9 DIP separators, a 4 DIP inset shell rounded by DWM (antialiased,
// 8 = the rows' 4 + 4, concentric; Paper's 10 = 6 + 4) and a white 6% hairline. No shadow:
// the shell is cut by NativeUI::ConfigureShadowlessPopup's window region (zero-shadow rule).
//
// Track() is modal like TrackPopupMenu: it runs its own message loop and
// returns the chosen id (0 = dismissed). It opens instantly (a right click is a
// high-frequency gesture: no entrance motion), hover fills fade in 120 ms on the
// shared interaction tweens, and it closes at once. Submenus open after a short
// hover, at once from Right / Enter.
#include <d2d1_1.h>
#include <d2d1effects.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "chrome.h"
#include "native-ui.h"
#include "accessibility.h"
#include <functional>

namespace NativeChrome {
namespace ContextMenu {

// Layout glyph drawn before a label (split view block, Paper p-7-0 "V2"): an 18 x 14
// outline with the part the dragged site will take filled in.
enum class Glyph { None, Blank, Right, Left, Top, Bottom, StartRow, EndRow, StartColumn, EndColumn };
enum class Kind { Command, Header, Note };  // Header / Note: read-only rows, never focused
struct Item {
    int id = 0;                       // returned by Track(); 0 is never returned for an item
    std::wstring description;         // optional second line (choice menus: what the option does)
    std::wstring label, shortcut;     // label may carry '&' mnemonics ("&&" is a literal '&')
    bool enabled = true, checked = false, separator = false;
    bool danger = false;              // destructive command (Paper: #FF7B6E label); its label always says what it does
    std::wstring icon;                // optional 16 DIP glyph (assets/icons name) in a leading column, white 70%
    std::shared_ptr<const FaviconImage> image; // optional site icon in the same column (wins over `icon`)
    std::wstring help;                // UI Automation help text (e.g. a bookmark's address)
    std::vector<Item> children;       // non-empty: a submenu
    Kind kind = Kind::Command;
    Glyph glyph = Glyph::None;
    std::wstring second;              // a quieter word (50 %) after the label (homonyms: "GitHub  Issues"); header: "· affiché"
    std::vector<std::shared_ptr<const FaviconImage>> icons;  // site icons: one (16, header 14) or a group's three (12)
    bool siteIcon = false;            // reserve the icon slot even without an image (generic globe)
    std::wstring accessible;          // read by assistive tech when the row is highlighted ("À droite, Avec YouTube")
    int tag = 0;                      // caller data for the highlight callback (e.g. which tab it designates)
};
// How a menu ended: the chosen id plus the gesture, so a bookmark row can open in a
// new tab (Ctrl / Shift) or ask for its own context menu (right click, Menu key).
// `keyboard`: chosen with Entrée / Espace / a mnemonic / UIA Invoke (not a pointer): the focus ring
// of whatever the choice opens follows it.
struct Choice { int id = 0; bool ctrl = false, shift = false, context = false, keyboard = false; };
inline Item Separator() { Item item; item.separator = true; return item; }
inline Item Header(std::wstring label, std::wstring second, std::vector<std::shared_ptr<const FaviconImage>> icons, int tag = 0) {
    Item item; item.kind = Kind::Header; item.label = std::move(label); item.second = std::move(second); item.icons = std::move(icons); item.siteIcon = true; item.enabled = false; item.tag = tag; return item;
}
inline Item Note(std::wstring text, bool indent = true) { Item item; item.kind = Kind::Note; item.label = std::move(text); item.enabled = false; item.glyph = indent ? Glyph::Blank : Glyph::None; return item; }
inline Item Command(int id, std::wstring label, std::wstring shortcut = L"", bool enabled = true, bool checked = false) {
    Item item; item.id = id; item.label = std::move(label); item.shortcut = std::move(shortcut); item.enabled = enabled; item.checked = checked; return item;
}
// Drops mnemonic markers ("&Back" -> "Back", "R&&D" -> "R&D") and reports the access key.
inline std::wstring Plain(const std::wstring& label, wchar_t* mnemonic = nullptr) {
    std::wstring out; out.reserve(label.size()); if (mnemonic) *mnemonic = 0;
    for (size_t i = 0; i < label.size(); ++i) {
        if (label[i] == L'&' && i + 1 < label.size()) {
            ++i; if (label[i] != L'&' && mnemonic && !*mnemonic) *mnemonic = static_cast<wchar_t>(towlower(label[i]));
        }
        out += label[i];
    }
    return out;
}

namespace Layout {
inline constexpr float pad = 4, row = 32, separator = 9, padLeft = 8, padRight = 10, shortcutGap = 24;
inline constexpr float radius = NativeUI::nativeCornerRadius, rowRadius = 4, text = 14, line = 20, trailing = 16, minWidth = 200, maxWidth = 440;
inline constexpr float submenuDelayMs = 200;
// Leading icon column (only when a row of the level has one): 16 DIP glyph, 8 DIP to the label (Paper c-99tvvr).
inline constexpr float icon = 16, iconGap = 8, iconOpacity = .7f;
// Key caps (Paper): one pill per key, 6 DIP sides, 2 DIP tall padding, radius 4, 2 DIP apart.
inline constexpr float keyPadX = 6, keyHeight = 20, keyRadius = 4, keyGap = 2;
// Described choices (Paper "menu décrit"): title line + wrapped description, 7/8 DIP padding.
inline constexpr float describedWidth = 288, describedTop = 7, describedBottom = 8, descriptionLine = 18;
// Opening: fades in, slides 6 DIP away from its anchor and comes into focus (blur 3 DIP to 0), 180 ms ease-out.
inline constexpr float enterSlide = 6, enterBlur = 3, enterMs = 180;
// Split block (Paper "V2"): 28 DIP header and reason rows in 13 DIP text, glyph 18 x 14 then 10 DIP to the label.
inline constexpr float header = 28, small = 13, glyphW = 18, glyphH = 14, glyphGap = 10, headerIcon = 14, miniIcon = 12, headerGap = 8;
// Glyph ink, opaque grey on the #171717 menu (Paper 9Z2-0: #8A8A8A outline and fill, the same on
// the highlighted row). Disabled keeps the former 32 % white over #171717 (0x61).
inline constexpr unsigned glyphInk = 0x8A, glyphInkDisabled = 0x61;
}
// CLSID_D2D1GaussianBlur (d2d1effects.h declares it extern; dxguid is not linked).
inline constexpr GUID BlurEffect{0x1feb6d69, 0x2fe6, 0x4ac9, {0x8c, 0x58, 0x1d, 0x7f, 0x93, 0xe7, 0xa6, 0xa5}};
static_assert(Layout::radius == Layout::rowRadius + Layout::pad, "concentric shell: row radius + inset");

// 18 x 14, radius 2, 1.5 DIP inset outline, the destination half (or a 6 DIP end) filled.
// One opaque ink for the outline and the fill (Paper 9Z2-0, user clean-up for c-bu5dzt):
// the fill overlaps the outline on three sides, so a translucent ink doubled its alpha there.
inline void PaintLayoutGlyph(ID2D1RenderTarget* t, ID2D1SolidColorBrush* brush, Glyph g, float x, float cy, bool enabled) {
    if (g == Glyph::None || g == Glyph::Blank) return;
    const float ink = (enabled ? Layout::glyphInk : Layout::glyphInkDisabled) / 255.f;
    brush->SetColor(D2D1::ColorF(ink, ink, ink, 1));
    const D2D1_RECT_F box{x, cy - Layout::glyphH / 2, x + Layout::glyphW, cy + Layout::glyphH / 2};
    t->DrawRoundedRectangle(D2D1::RoundedRect({box.left + .75f, box.top + .75f, box.right - .75f, box.bottom - .75f}, 1.25f, 1.25f), brush, 1.5f);
    const float w = Layout::glyphW, h = Layout::glyphH; D2D1_RECT_F fill = box;
    switch (g) {
    case Glyph::Right: fill.left = box.right - w / 2; break;
    case Glyph::Left: fill.right = box.left + w / 2; break;
    case Glyph::Top: fill.bottom = box.top + h / 2; break;
    case Glyph::Bottom: fill.top = box.bottom - h / 2; break;
    case Glyph::StartRow: fill.right = box.left + 6; break;
    case Glyph::EndRow: fill.left = box.right - 6; break;
    case Glyph::StartColumn: fill.bottom = box.top + 5; break;
    case Glyph::EndColumn: fill.top = box.bottom - 5; break;
    default: break;
    }
    t->PushAxisAlignedClip(box, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    t->FillRoundedRectangle(D2D1::RoundedRect(fill, 2, 2), brush); t->PopAxisAlignedClip();
}

class Menu {
    struct Level {
        HWND hwnd = nullptr; Chrome chrome; Motion::State motion;
        const std::vector<Item>* items = nullptr;
        std::vector<float> tops, heights; std::vector<std::vector<std::wstring>> descriptions; // wrapped lines
        float width = Layout::minWidth, height = 0;
        int hover = -1, pressed = -1, child = -1; // child: index whose submenu is open
        bool keyboard = false;
        UINT dpi = 96;
        // Opening motion: final window position and the direction it arrives from.
        bool entering = false; Motion::Time opened{}; POINT final{}; int fromX = 0, fromY = 0;
        ID2D1Effect* blur = nullptr;
        bool icons = false;               // the level reserves the icon column
        Accessibility::Bridge uia;        // Menu + MenuItem tree for screen readers
        int announced = -2;               // last row reported as focused
    };
    HINSTANCE instance_ = nullptr; HWND owner_ = nullptr; std::wstring root_;
    const Item* highlighted_ = nullptr;
    std::vector<std::unique_ptr<Level>> levels_; int depth_ = 0; // open levels
    int result_ = 0; bool done_ = false, reduced_ = false; Choice choice_; bool keyedChoice_ = false;   // the input that drives the current pick (keyboard vs pointer)
    std::function<void(int)> background_; // middle click on a row: open in the background, menu stays
    bool contextRows_ = false;            // right click on a row returns it with context = true
    static constexpr UINT InvokeMessage = WM_APP + 40; // UIA actions are posted, never run inside the provider call
    static constexpr UINT_PTR FrameTimer = 1, SubmenuTimer = 2;

    static bool Selectable(const Item& item) { return !item.separator && item.enabled && item.kind == Kind::Command; }
    Level* LevelOf(HWND hwnd) { for (int i = 0; i < depth_; ++i) if (levels_[i]->hwnd == hwnd) return levels_[i].get(); return nullptr; }
    int IndexOf(HWND hwnd) const { for (int i = 0; i < depth_; ++i) if (levels_[i]->hwnd == hwnd) return i; return -1; }
    float Scale(const Level& l) const { return l.dpi / 96.f; }
    int Px(const Level& l, float dip) const { return static_cast<int>(std::lround(dip * Scale(l))); }

    Level& Ensure(int index) {
        while (static_cast<int>(levels_.size()) <= index) {
            auto level = std::make_unique<Level>();
            level->hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED, L"LisiereContextMenu", L"", WS_POPUP, 0, 0, 1, 1, owner_, nullptr, instance_, this);
            if (level->hwnd) {
                SetLayeredWindowAttributes(level->hwnd, 0, 255, LWA_ALPHA); // whole-window alpha for the opening fade
                NativeUI::ConfigureShadowlessPopup(level->hwnd, Layout::radius); // region-rounded, no shadow
                level->motion.EnablePolish(); level->chrome.SetMotion(&level->motion); level->chrome.textCacheLimit = 192;
                level->chrome.Init(level->hwnd, root_);
                Attach(*level, static_cast<int>(levels_.size()));
            }
            levels_.push_back(std::move(level));
        }
        return *levels_[index];
    }
    // "Ctrl+Maj+T" -> {Ctrl, Maj, T}; the engine's arrow names become arrows.
    static std::vector<std::wstring> Keys(const std::wstring& shortcut) {
        std::vector<std::wstring> keys; size_t start = 0;
        while (start <= shortcut.size()) {
            size_t plus = shortcut.find(L'+', start);
            if (plus == start && plus + 1 == shortcut.size()) plus = std::wstring::npos; // a literal "+" key
            std::wstring key = shortcut.substr(start, plus == std::wstring::npos ? std::wstring::npos : plus - start);
            std::wstring low = key; for (auto& ch : low) ch = static_cast<wchar_t>(towlower(ch));
            if (low == L"left" || low == L"arrowleft" || low == L"gauche") key = L"\u2190";
            else if (low == L"right" || low == L"arrowright" || low == L"droite") key = L"\u2192";
            else if (low == L"up" || low == L"arrowup" || low == L"haut") key = L"\u2191";
            else if (low == L"down" || low == L"arrowdown" || low == L"bas") key = L"\u2193";
            else if (low == L"shift") key = L"Maj";
            if (!key.empty()) keys.push_back(key);
            if (plus == std::wstring::npos) break;
            start = plus + 1;
        }
        return keys;
    }
    float KeyWidth(Level& l, const std::wstring& key) { l.chrome.keycap = true; const float w = l.chrome.TextWidth(key, Layout::text, false, true); l.chrome.keycap = false; return std::ceil(w) + 2 * Layout::keyPadX; }
    float KeysWidth(Level& l, const std::wstring& shortcut) {
        float w = 0; for (const auto& key : Keys(shortcut)) w += (w > 0 ? Layout::keyGap : 0) + KeyWidth(l, key); return w;
    }
    // Draws the caps right-aligned to `right`, vertically centred on `cy`; returns their left edge.
    float DrawKeys(Level& l, const std::wstring& shortcut, float right, float cy, float dim) {
        auto* t = l.chrome.RenderTarget(); auto* brush = l.chrome.SolidBrush(); const auto keys = Keys(shortcut);
        float x = right;
        for (size_t k = keys.size(); k-- > 0;) {
            const float w = KeyWidth(l, keys[k]); const D2D1_RECT_F cap{x - w, cy - Layout::keyHeight / 2, x, cy + Layout::keyHeight / 2};
            brush->SetColor(D2D1::ColorF(1, 1, 1, .06f * dim)); t->FillRoundedRectangle(D2D1::RoundedRect(cap, Layout::keyRadius, Layout::keyRadius), brush);
            l.chrome.keycap = true; l.chrome.Text(keys[k], {cap.left + Layout::keyPadX - 1, cap.top, cap.right, cap.bottom}, Layout::text, Design::Text::disabled * dim, true); l.chrome.keycap = false;
            x = cap.left - Layout::keyGap;
        }
        return x + Layout::keyGap;
    }
    // Balanced wrap (CSS text-wrap: balance): the greedy line count at the narrowest width keeping it.
    std::vector<std::wstring> Wrap(Level& l, const std::wstring& text, float width) {
        auto lines = Greedy(l, text, width);
        if (lines.size() < 2) return lines;
        float lo = width / lines.size(), hi = width;
        for (int k = 0; k < 10 && hi - lo > .5f; ++k) { const float mid = (lo + hi) / 2; if (Greedy(l, text, mid).size() == lines.size()) hi = mid; else lo = mid; }
        return Greedy(l, text, hi);
    }
    std::vector<std::wstring> Greedy(Level& l, const std::wstring& text, float width) {
        std::vector<std::wstring> lines; std::wstring line, word;
        auto flush = [&] { if (!line.empty()) lines.push_back(line); line.clear(); };
        for (size_t i = 0; i <= text.size(); ++i) {
            if (i < text.size() && text[i] != L' ') { word += text[i]; continue; }
            if (word.empty()) continue;
            const std::wstring candidate = line.empty() ? word : line + L" " + word;
            if (!line.empty() && l.chrome.TextWidth(candidate, Layout::text) > width) { flush(); line = word; } else line = candidate;
            word.clear();
        }
        flush(); return lines;
    }
    // Content-driven width: the widest label, plus the shortcut column or a trailing glyph.
    // A menu with described choices takes the fixed Paper width and wraps descriptions.
    void Measure(Level& l) {
        float label = 0, shortcut = 0; bool glyph = false, described = false; l.icons = false;
        for (const auto& item : *l.items) {
            if (item.separator) continue;
            const float size = item.kind == Kind::Command ? Layout::text : Layout::small;
            float w = l.chrome.TextWidth(Plain(item.label), size) + Lead(item);
            if (!item.second.empty()) w += l.chrome.TextWidth(L"  " + item.second, Layout::small);
            label = std::max(label, w);
            if (!item.shortcut.empty()) shortcut = std::max(shortcut, KeysWidth(l, item.shortcut));
            glyph = glyph || item.checked || !item.children.empty();
            described = described || !item.description.empty();
            l.icons = l.icons || !item.icon.empty() || item.image != nullptr;
        }
        if (l.icons) label += Layout::icon + Layout::iconGap;
        float tail = shortcut > 0 ? Layout::shortcutGap + shortcut : 0;
        if (glyph) tail = std::max(tail, Layout::shortcutGap + Layout::trailing);
        l.width = std::clamp(std::ceil(2 * Layout::pad + 2 + Layout::padLeft + label + tail + Layout::padRight), Layout::minWidth, Layout::maxWidth);
        if (described) l.width = std::max(l.width, Layout::describedWidth);
        const float textWidth = l.width - 2 * Layout::pad - 2 - Layout::padLeft - Layout::padRight - Layout::trailing - 10;
        l.tops.clear(); l.heights.clear(); l.descriptions.clear(); float y = Layout::pad + 1;
        for (const auto& item : *l.items) {
            auto lines = item.description.empty() ? std::vector<std::wstring>{} : Wrap(l, item.description, textWidth);
            const float h = item.separator ? Layout::separator : item.kind != Kind::Command ? Layout::header : lines.empty() ? Layout::row : Layout::describedTop + Layout::line + 1 + lines.size() * Layout::descriptionLine + Layout::describedBottom;
            l.tops.push_back(y); l.heights.push_back(h); l.descriptions.push_back(std::move(lines)); y += h;
        }
        l.height = y + Layout::pad + 1;
    }
    static RECT WorkArea(POINT p) { MONITORINFO info{sizeof(info)}; GetMonitorInfoW(MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST), &info); return info.rcWork; }
    // Opens `items` as level `index`. at: screen point for the top-left corner;
    // flipX: the x the menu ends at when it must open leftward instead.
    void Open(int index, const std::vector<Item>& items, POINT at, int flipX, int flipY, bool activate, bool fromRight = false) {
        Level& l = Ensure(index); if (!l.hwnd) return;
        l.items = &items; l.hover = l.pressed = l.child = -1; l.keyboard = false; l.motion.ClearInteractions(); l.motion.SetReducedMotion(reduced_);
        l.dpi = GetDpiForWindow(owner_); Measure(l);
        const int w = Px(l, l.width), h = Px(l, l.height); const RECT work = WorkArea(at);
        int x = fromRight ? at.x - w : at.x, y = at.y;
        if (x + w > work.right) x = std::max<int>(work.left, flipX - w);
        if (y + h > work.bottom) y = std::max<int>(work.top, std::min<int>(flipY, work.bottom) - h);
        x = std::max<int>(x, work.left); y = std::max<int>(y, work.top);
        // It arrives from its anchor: downward under it, upward when flipped above, sideways for a submenu.
        l.final = {x, y}; l.fromX = index > 0 ? (x < at.x ? 1 : -1) : 0; l.fromY = index > 0 ? 0 : (y < at.y ? 1 : -1);
        l.entering = !reduced_; l.opened = Motion::Now();
        SetWindowPos(l.hwnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
        Step(l, l.opened);
        l.chrome.Resize();
        depth_ = index + 1;
        ShowWindow(l.hwnd, activate ? SW_SHOW : SW_SHOWNOACTIVATE);
        Paint(l); ValidateRect(l.hwnd, nullptr);
        if (l.entering) SetTimer(l.hwnd, FrameTimer, 10, nullptr);
        if (activate) { SetForegroundWindow(l.hwnd); SetFocus(l.hwnd); }
        l.announced = -2;
        if (l.uia.Listening()) { l.uia.ChildrenChanged(MenuKey); l.uia.Raise(MenuKey, UIA_MenuOpenedEventId); }
    }
    void CloseFrom(int index) {
        for (int i = index; i < depth_; ++i) { if (levels_[i]->uia.Listening()) levels_[i]->uia.Raise(MenuKey, UIA_MenuClosedEventId); KillTimer(levels_[i]->hwnd, FrameTimer); KillTimer(levels_[i]->hwnd, SubmenuTimer); ShowWindow(levels_[i]->hwnd, SW_HIDE); levels_[i]->entering = false; }
        if (index < depth_) depth_ = index;
        if (index > 0 && index - 1 < depth_) { levels_[index - 1]->child = -1; Invalidate(*levels_[index - 1]); }
        Highlight();
    }
    void OpenChild(int index) {
        Level& l = *levels_[index]; const int i = l.hover;
        if (i < 0 || (*l.items)[i].children.empty() || !(*l.items)[i].enabled) return;
        // (submenus never carry descriptions; their rows are Layout::row tall)
        if (l.child == i && depth_ > index + 1) return;
        CloseFrom(index + 1); l.child = i;
        RECT r; GetWindowRect(l.hwnd, &r); OffsetRect(&r, l.final.x - r.left, l.final.y - r.top); // its resting place, even mid-entrance
        const POINT at{r.right - Px(l, Layout::pad), r.top + Px(l, l.tops[i]) - Px(l, Layout::pad + 1)};
        Open(index + 1, (*l.items)[i].children, at, r.left + Px(l, Layout::pad), r.top + Px(l, l.tops[i] + Layout::row) + Px(l, Layout::pad + 1), false);
        Invalidate(l);
    }
    void Back(int index) { const int row = levels_[index - 1]->child; CloseFrom(index); if (row >= 0) SetHover(index - 1, row, true); }
    void Finish(int id) { result_ = id; choice_.id = id; choice_.keyboard = id && keyedChoice_; done_ = true; PostMessageW(owner_, WM_NULL, 0, 0); }
    void Activate(int index, int i, bool ctrl = false, bool shift = false, bool context = false) {
        Level& l = *levels_[index]; if (i < 0 || i >= static_cast<int>(l.items->size())) return;
        const Item& item = (*l.items)[i]; if (!Selectable(item)) return;
        if (context && (!contextRows_ || !item.id)) return;
        if (!item.children.empty() && !context) { l.hover = i; OpenChild(index); if (depth_ > index + 1) Move(index + 1, 1, true); return; }
        choice_ = {}; choice_.ctrl = ctrl; choice_.shift = shift; choice_.context = context;
        Finish(item.id);
    }
    void SetHover(int index, int i, bool keyboard) {
        Level& l = *levels_[index]; l.keyboard = keyboard;
        if (l.hover == i) return;
        l.hover = i; Interact(l); Highlight();
        if (keyboard && i >= 0 && l.uia.Listening() && l.announced != i) { l.announced = i; l.uia.Raise(RowKey(i), UIA_AutomationFocusChangedEventId); }
        // Hovering another row of a parent closes its open submenu after the same delay.
        KillTimer(l.hwnd, SubmenuTimer);
        if (!keyboard && (i != l.child)) SetTimer(l.hwnd, SubmenuTimer, static_cast<UINT>(Layout::submenuDelayMs), nullptr);
    }
    // The row fill is the hover tween alone (pointer over the row, or keyboard focus): an open
    // branch never holds it. Leaving the row retargets the tween from its current value at once,
    // even mid-entrance of the submenu, which stays open for the trajectory (SubmenuTimer).
    static float RowFill(const Motion::InteractionVisual& v) { return v.hover; }
    void PointerMove(int index, float y) {
        Level& l = *levels_[index]; const int row = HitRow(l, y);
        SetHover(index, row >= 0 && !(*l.items)[row].enabled ? -1 : row, false);
    }
    // Leaving a level (to its submenu, another level or outside) drops the pointer hover; the
    // submenu stays open. Keyboard focus is not the pointer's to clear.
    void PointerLeave(int index) { Level& l = *levels_[index]; if (!l.keyboard) SetHover(index, -1, false); }
    void Interact(Level& l) {
        l.motion.SetInteraction(l.hover + 1, l.pressed >= 0 && l.pressed == l.hover ? l.pressed + 1 : 0, Motion::Now());
        Invalidate(l);
    }
    void Move(int index, int step, bool fromStart = false) {
        Level& l = *levels_[index]; const int n = static_cast<int>(l.items->size()); if (!n) return;
        int i = fromStart ? (step > 0 ? -1 : n) : l.hover;
        for (int k = 0; k < n; ++k) { i = (i + step + n) % n; if (Selectable((*l.items)[i])) { SetHover(index, i, true); return; } }
    }
    int HitRow(const Level& l, float y) const {
        for (size_t i = 0; i < l.tops.size(); ++i) if (y >= l.tops[i] && y < l.tops[i] + l.heights[i]) return (*l.items)[i].separator || (*l.items)[i].kind != Kind::Command ? -1 : static_cast<int>(i);
        return -1;
    }
    // Space before the label: a layout glyph, or a site icon (one, or a group's three).
    static float Lead(const Item& item) {
        if (item.kind == Kind::Header) return (item.icons.size() > 1 ? 3 * Layout::miniIcon + 4 : Layout::headerIcon) + Layout::headerGap;
        if (item.glyph != Glyph::None) return Layout::glyphW + Layout::glyphGap;
        if (item.icons.size() > 1) return 3 * Layout::miniIcon + 4 + Layout::glyphGap;
        if (item.siteIcon || !item.icons.empty()) return Layout::icon + Layout::glyphGap;
        return 0;
    }
    void DrawIcons(Level& l, const Item& item, float x, float cy, float single, float alpha) {
        if (item.icons.size() > 1) {
            for (size_t k = 0; k < item.icons.size() && k < 3; ++k) { const float left = x + k * (Layout::miniIcon + 2); const D2D1_RECT_F box{left, cy - Layout::miniIcon / 2, left + Layout::miniIcon, cy + Layout::miniIcon / 2};
                if (!l.chrome.DrawSiteImage(item.icons[k], box, alpha)) l.chrome.Icon(L"favicon", box, 16, alpha); }
            return;
        }
        const D2D1_RECT_F box{x, cy - single / 2, x + single, cy + single / 2};
        if (item.icons.empty() || !l.chrome.DrawSiteImage(item.icons[0], box, alpha)) l.chrome.Icon(L"favicon", box, 16, alpha);
    }
    void DrawGlyph(Level& l, Glyph g, float x, float cy, bool enabled) { PaintLayoutGlyph(l.chrome.RenderTarget(), l.chrome.SolidBrush(), g, x, cy, enabled); }
    // The highlighted command (hover or keyboard) across every open level, for the owner.
    void Highlight() {
        const Item* now = nullptr;
        for (int i = depth_ - 1; i >= 0 && !now; --i) { auto& l = *levels_[i]; if (l.hover >= 0 && l.hover < static_cast<int>(l.items->size()) && Selectable((*l.items)[l.hover])) now = &(*l.items)[l.hover]; }
        if (now == highlighted_) return; highlighted_ = now; if (onHighlight) onHighlight(now);
    }
    void Invalidate(Level& l) {
        if (!l.hwnd) return; InvalidateRect(l.hwnd, nullptr, FALSE);
        if (l.motion.NextWake(Motion::Now()).delay) SetTimer(l.hwnd, FrameTimer, 10, nullptr);
    }
    static float EnterProgress(const Level& l, Motion::Time now) {
        const float u = Motion::Clamp(static_cast<float>(std::chrono::duration<double, std::milli>(now - l.opened).count()) / Layout::enterMs);
        return 1 - (1 - u) * (1 - u) * (1 - u) * (1 - u); // ease-out quart: quick to arrive, soft to land
    }
    // Window part of the entrance: alpha and position (cheap: DWM composes both).
    void Step(Level& l, Motion::Time now) {
        if (!l.hwnd) return;
        const float e = l.entering ? EnterProgress(l, now) : 1;
        if (e >= 1) l.entering = false;
        const float fade = l.entering ? Motion::Clamp(e * 1.4f) : 1;
        SetLayeredWindowAttributes(l.hwnd, 0, static_cast<BYTE>(std::lround(255 * fade)), LWA_ALPHA);
        const int offset = Px(l, Layout::enterSlide * (1 - e));
        SetWindowPos(l.hwnd, nullptr, l.final.x + l.fromX * offset, l.final.y + l.fromY * offset, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    void Paint(Level& l) {
        if (!l.chrome.RenderTarget()) l.chrome.Resize();
        auto* target = l.chrome.RenderTarget(); if (!target || !l.chrome.SolidBrush() || !l.items) return;
        const auto now = Motion::Now(); const float c = Design::Surface::canvas;
        target->BeginDraw();
        // Coming into focus: the frame is drawn off screen and blurred while it enters.
        const float blur = l.entering ? Layout::enterBlur * (1 - EnterProgress(l, now)) : 0;
        ID2D1DeviceContext* dc = nullptr; ID2D1BitmapRenderTarget* layer = nullptr; ID2D1Bitmap* image = nullptr; bool drawn = false;
        if (blur > .05f && SUCCEEDED(target->QueryInterface(__uuidof(ID2D1DeviceContext), reinterpret_cast<void**>(&dc))) &&
            (l.blur || SUCCEEDED(dc->CreateEffect(BlurEffect, &l.blur))) && SUCCEEDED(target->CreateCompatibleRenderTarget(&layer))) {
            l.chrome.Redirect(layer); layer->BeginDraw(); PaintContent(l, now); const HRESULT hr = layer->EndDraw(); l.chrome.Redirect(target);
            if (SUCCEEDED(hr) && SUCCEEDED(layer->GetBitmap(&image))) {
                const D2D1_BORDER_MODE border = D2D1_BORDER_MODE_HARD; // the canvas keeps its edge: no dark halo
                l.blur->SetInput(0, image);
                l.blur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, reinterpret_cast<const BYTE*>(&blur), sizeof(blur));
                l.blur->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, reinterpret_cast<const BYTE*>(&border), sizeof(border));
                target->Clear(D2D1::ColorF(c, c, c, 1)); dc->DrawImage(l.blur); l.blur->SetInput(0, nullptr); drawn = true;
            }
        }
        if (!drawn) PaintContent(l, now);
        if (image) image->Release();
        if (layer) layer->Release();
        if (dc) dc->Release();
        if (target->EndDraw() == D2DERR_RECREATE_TARGET) { if (l.blur) { l.blur->Release(); l.blur = nullptr; } l.chrome.RecreateTarget(); }
        if (!l.motion.NextWake(now).delay && !l.entering) KillTimer(l.hwnd, FrameTimer);
    }
    void PaintContent(Level& l, Motion::Time now) {
        auto* t = l.chrome.RenderTarget(); auto* brush = l.chrome.SolidBrush(); const float c = Design::Surface::canvas;
        t->Clear(D2D1::ColorF(c, c, c, 1));
        l.chrome.keyboardModality = l.keyboard;
        const float left = Layout::pad + 1, right = l.width - Layout::pad - 1;
        for (size_t i = 0; i < l.items->size(); ++i) {
            const Item& item = (*l.items)[i]; const float top = l.tops[i];
            if (item.separator) { brush->SetColor(D2D1::ColorF(1, 1, 1, .06f)); const float y = top + Layout::separator / 2 - .5f; t->FillRectangle(D2D1::RectF(left + Layout::padLeft, y, right - Layout::padLeft, y + 1), brush); continue; }
            if (item.kind != Kind::Command) {  // header (#FFFFFF99, its tail 35 %) or reason (#FFFFFFB3), 13 DIP, not focusable
                const float cy = top + Layout::header / 2, y = top + (Layout::header - Layout::line) / 2; float x = left + Layout::padLeft;
                if (item.kind == Kind::Header) { DrawIcons(l, item, x, cy, Layout::headerIcon, 1); x += Lead(item); }
                else if (item.glyph != Glyph::None) x += Layout::glyphW + Layout::glyphGap;
                const std::wstring text = Plain(item.label); const float alpha = item.kind == Kind::Header ? .6f : .7f;
                l.chrome.Text(text, {x, y, right - Layout::padRight, y + Layout::line}, Layout::small, alpha);
                if (!item.second.empty()) l.chrome.Text(L" " + item.second, {x + l.chrome.TextWidth(text, Layout::small), y, right - Layout::padRight, y + Layout::line}, Layout::small, .5f);  // 5.26:1
                continue;
            }
            const auto v = l.motion.Visual(static_cast<int>(i) + 1, now);
            const float h = item.enabled ? RowFill(v) : 0;
            const auto& lines = l.descriptions[i]; const bool described = !lines.empty();
            const D2D1_RECT_F row{left, top, right, top + l.heights[i]};
            const float titleTop = described ? top + Layout::describedTop : top + (Layout::row - Layout::line) / 2; // glyphs align with the title line
            if (h > 0 || v.press > 0) { brush->SetColor(D2D1::ColorF(1, 1, 1, .08f * h + .04f * v.press)); t->FillRoundedRectangle(D2D1::RoundedRect(row, Layout::rowRadius, Layout::rowRadius), brush); }
            // Keyboard focus: the same fill plus a 1 DIP inner ring at 50 %; the parent of an open submenu keeps the fill only.
            if (l.keyboard && static_cast<int>(i) == l.hover && static_cast<int>(i) != l.child && item.enabled) {
                brush->SetColor(D2D1::ColorF(1, 1, 1, Design::Focus::keyboardOpacity));
                t->DrawRoundedRectangle(D2D1::RoundedRect({row.left + .5f, row.top + .5f, row.right - .5f, row.bottom - .5f}, Layout::rowRadius - .5f, Layout::rowRadius - .5f), brush, 1);
            }
            const float dim = item.enabled ? 1.f : Design::Text::disabled;
            float tail = right - Layout::padRight;
            if (!item.children.empty()) {
                const D2D1_RECT_F box{tail - Layout::trailing, titleTop + (Layout::line - Layout::trailing) / 2, tail, titleTop + (Layout::line + Layout::trailing) / 2};
                D2D1_MATRIX_3X2_F base; t->GetTransform(&base);
                t->SetTransform(D2D1::Matrix3x2F::Rotation(-90, {(box.left + box.right) / 2, (box.top + box.bottom) / 2}) * base);
                l.chrome.Icon(L"caret", box, 16, .7f * dim); t->SetTransform(base); tail -= Layout::trailing + Layout::shortcutGap;
            } else if (item.checked) {
                const D2D1_RECT_F box{tail - Layout::trailing, titleTop + (Layout::line - Layout::trailing) / 2, tail, titleTop + (Layout::line + Layout::trailing) / 2};
                // Icons ship at 50%: two passes give the check its ~75% weight.
                l.chrome.Icon(L"check", box, 14, dim); l.chrome.Icon(L"check", box, 14, dim); tail -= Layout::trailing + Layout::shortcutGap;
            }
            if (!item.shortcut.empty()) tail = DrawKeys(l, item.shortcut, tail, titleTop + Layout::line / 2, dim) - Layout::shortcutGap;
            float textLeft = left + Layout::padLeft; const float cy = titleTop + Layout::line / 2;
            if (l.icons) {
                const D2D1_RECT_F box{textLeft, titleTop + (Layout::line - Layout::icon) / 2, textLeft + Layout::icon, titleTop + (Layout::line + Layout::icon) / 2};
                if (!(item.image && l.chrome.DrawSiteImage(item.image, box, dim)) && !item.icon.empty()) {
                    // Glyphs ship at 50%: a second pass at 80% lands on Paper's 70% (.5 + .8 x .5 x .5).
                    l.chrome.Icon(item.icon.c_str(), box, Layout::icon, dim); l.chrome.Icon(item.icon.c_str(), box, Layout::icon, .8f * dim);
                }
                textLeft += Layout::icon + Layout::iconGap;
            }
            if (item.glyph != Glyph::None) { DrawGlyph(l, item.glyph, textLeft, cy, item.enabled); textLeft += Lead(item); }
            else if (item.siteIcon || !item.icons.empty()) { DrawIcons(l, item, textLeft, cy, Layout::icon, dim); textLeft += Lead(item); }
            const float labelRight = std::max(textLeft + 8, std::min(tail + Layout::shortcutGap - 8, right - Layout::padRight));
            const bool danger = item.danger && item.enabled;
            if (danger) l.chrome.ink = D2D1::ColorF(Design::Danger::r, Design::Danger::g, Design::Danger::b, 1);
            const std::wstring plain = Plain(item.label);
            l.chrome.Text(plain, {textLeft, titleTop, labelRight, titleTop + Layout::line}, Layout::text, danger ? 1.f : (.9f + .1f * h) * dim);
            l.chrome.ink = D2D1::ColorF(1, 1, 1, 1);
            if (!item.second.empty()) { const float x = textLeft + l.chrome.TextWidth(plain + L"  ", Layout::text); if (x < labelRight) l.chrome.Text(item.second, {x, titleTop, labelRight, titleTop + Layout::line}, Layout::small, (.5f + .1f * h) * dim); }  // 50 % (5.26:1), quieter than the 90-100 % name
            // Description: 50% at rest, a touch brighter on the hovered choice.
            for (size_t k = 0; k < lines.size(); ++k) {
                const float y = titleTop + Layout::line + 1 + k * Layout::descriptionLine;
                l.chrome.Text(lines[k], {textLeft, y, right - Layout::padRight - Layout::trailing - 10, y + Layout::descriptionLine}, Layout::text, (.5f + .1f * h) * dim);
            }
        }
        // Hairline on the region's rounded outline (DWM no longer draws one).
        const float px = t->GetSize().width / std::max(1.f, static_cast<float>(t->GetPixelSize().width));
        brush->SetColor(D2D1::ColorF(1, 1, 1, .06f));
        t->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(px / 2, px / 2, l.width - px / 2, l.height - px / 2), Layout::radius, Layout::radius), brush, px);
    }
    // Keyboard for the whole stack: the deepest open level owns it.
    bool Key(WPARAM key) {
        keyedChoice_ = true;
        if (!depth_) return false;
        const int index = depth_ - 1; Level& l = *levels_[index];
        switch (key) {
        case VK_DOWN: Move(index, 1); return true;
        case VK_UP: Move(index, -1); return true;
        case VK_HOME: Move(index, 1, true); return true;
        case VK_END: Move(index, -1, true); return true;
        case VK_RIGHT: if (l.hover >= 0 && !(*l.items)[l.hover].children.empty()) Activate(index, l.hover); return true;
        case VK_LEFT: if (index > 0) Back(index); return true;
        case VK_ESCAPE: if (index > 0) Back(index); else Finish(0); return true;
        case VK_RETURN: case VK_SPACE: if (l.hover >= 0) Activate(index, l.hover, GetKeyState(VK_CONTROL) < 0, GetKeyState(VK_SHIFT) < 0); return true;
        case VK_APPS: if (l.hover >= 0) Activate(index, l.hover, false, false, true); return true;
        case VK_TAB: Move(index, GetKeyState(VK_SHIFT) < 0 ? -1 : 1); return true;
        case VK_F10: if (GetKeyState(VK_SHIFT) < 0) { if (l.hover >= 0) Activate(index, l.hover, false, false, true); return true; } Finish(0); return true;
        case VK_MENU: Finish(0); return true;
        default: break;
        }
        // Access keys: a unique match runs, several cycle through.
        const UINT ch = MapVirtualKeyW(static_cast<UINT>(key), MAPVK_VK_TO_CHAR); if (!ch) return true;
        const wchar_t letter = static_cast<wchar_t>(towlower(static_cast<wchar_t>(ch)));
        std::vector<int> matches;
        for (size_t i = 0; i < l.items->size(); ++i) { wchar_t m = 0; Plain((*l.items)[i].label, &m); if (m == letter && Selectable((*l.items)[i])) matches.push_back(static_cast<int>(i)); }
        if (matches.size() == 1) Activate(index, matches[0]);
        else if (!matches.empty()) { auto it = std::upper_bound(matches.begin(), matches.end(), l.hover); SetHover(index, it == matches.end() ? matches[0] : *it, true); }
        return true;
    }
    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM w, LPARAM lp) {
        auto* self = reinterpret_cast<Menu*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (msg == WM_NCCREATE) { self = static_cast<Menu*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams); SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); }
        return self ? self->Handle(h, msg, w, lp) : DefWindowProcW(h, msg, w, lp);
    }
    LRESULT Handle(HWND h, UINT msg, WPARAM w, LPARAM lp) {
        const int index = IndexOf(h); Level* l = index >= 0 ? levels_[index].get() : nullptr;
        switch (msg) {
        case WM_GETOBJECT: if (l) { LRESULT result; if (l->uia.OnGetObject(h, w, lp, result)) return result; } break;
        case InvokeMessage: keyedChoice_ = true; if (l && index < depth_ && static_cast<int>(w) < static_cast<int>(l->items->size())) { if (lp == 1) { l->hover = static_cast<int>(w); OpenChild(index); } else if (lp == 2) CloseFrom(index + 1); else Activate(index, static_cast<int>(w)); } return 0;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); if (l) Paint(*l); EndPaint(h, &ps); return 0; }
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_ACTIVATE:
            // Another window took activation (Alt+Tab, a click elsewhere): dismiss.
            if (LOWORD(w) == WA_INACTIVE && !done_ && depth_ && !LevelOf(reinterpret_cast<HWND>(lp))) Finish(0);
            return 0;
        case WM_TIMER:
            if (!l) return 0;
            if (w == FrameTimer) { if (l->entering) Step(*l, Motion::Now()); InvalidateRect(h, nullptr, FALSE); return 0; }
            if (w == SubmenuTimer) { KillTimer(h, SubmenuTimer); if (l->hover >= 0 && !(*l->items)[l->hover].children.empty()) OpenChild(index); else if (l->hover >= 0 && l->child >= 0 && l->hover != l->child) CloseFrom(index + 1); }
            return 0;
        case WM_MOUSEMOVE: {
            if (!l || ignorePointer) return 0;
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, h, 0}; TrackMouseEvent(&track);
            PointerMove(index, GET_Y_LPARAM(lp) / Scale(*l)); return 0;
        }
        case WM_MOUSELEAVE: if (l && !ignorePointer) PointerLeave(index); return 0;
        case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN: if (l) { l->pressed = HitRow(*l, GET_Y_LPARAM(lp) / Scale(*l)); Interact(*l); } return 0;
        case WM_LBUTTONUP: case WM_RBUTTONUP: case WM_MBUTTONUP: {
            if (!l) return 0;
            const int row = HitRow(*l, GET_Y_LPARAM(lp) / Scale(*l)); l->pressed = -1; Interact(*l);
            if (row < 0) return 0;
            const Item& item = (*l->items)[row];
            // Middle click opens a link row in the background and keeps the menu open, as in every browser.
            if (msg == WM_MBUTTONUP) { if (background_ && Selectable(item) && item.children.empty() && item.id) background_(item.id); return 0; }
            // Right click on a row of a bookmark menu asks for that row's own menu.
            if (msg == WM_RBUTTONUP && contextRows_ && item.id) { Activate(index, row, false, false, true); return 0; }
            keyedChoice_ = false; Activate(index, row, (w & MK_CONTROL) != 0, (w & MK_SHIFT) != 0);
            return 0;
        }
        }
        return DefWindowProcW(h, msg, w, lp);
    }
    // ---- UI Automation: each level is a Menu, each row a MenuItem ------------------
    static constexpr long long MenuKey = 1;
    static long long RowKey(int i) { return 100 + i; }
    void Attach(Level& level, int index) {
        Accessibility::Host host; host.window = level.hwnd;
        host.nodes = [this, index] { return Nodes(index); };
        host.focused = [this, index]() -> long long { if (index >= depth_) return 0; const auto& l = *levels_[index]; return index == depth_ - 1 && l.hover >= 0 ? RowKey(l.hover) : 0; };
        host.invoke = [this, index](long long key) { if (index < depth_) PostMessageW(levels_[index]->hwnd, InvokeMessage, static_cast<WPARAM>(key - 100), 0); };
        host.expand = [this, index](long long key, bool open) { if (index < depth_) PostMessageW(levels_[index]->hwnd, InvokeMessage, static_cast<WPARAM>(key - 100), open ? 1 : 2); };
        host.focus = [this, index](long long key) { if (index < depth_ && key >= 100) SetHover(index, static_cast<int>(key - 100), true); };
        level.uia.Attach(std::move(host));
    }
    std::vector<Accessibility::Node> Nodes(int index) const {
        std::vector<Accessibility::Node> nodes; if (index >= depth_ || index >= static_cast<int>(levels_.size())) return nodes;
        const Level& l = *levels_[index]; if (!l.items || !l.hwnd) return nodes;
        RECT window{}; GetWindowRect(l.hwnd, &window); const float k = l.dpi / 96.f;
        Accessibility::Node menu; menu.key = MenuKey; menu.role = Accessibility::Role::Menu; menu.name = index == 0 ? L"Menu" : L"Sous-menu"; menu.automationId = L"ContextMenu"; menu.focusable = false; menu.bounds = window; nodes.push_back(menu);
        int count = 0; for (const auto& item : *l.items) if (!item.separator) ++count;
        int position = 0;
        for (size_t i = 0; i < l.items->size() && i < l.tops.size(); ++i) {
            const Item& item = (*l.items)[i]; if (item.separator) continue;
            Accessibility::Node n; n.key = RowKey(static_cast<int>(i)); n.parent = MenuKey; n.role = Accessibility::Role::MenuItem;
            n.name = Plain(item.label); n.accelerator = item.shortcut; n.help = !item.help.empty() ? item.help : item.description; n.enabled = item.enabled;
            if (item.checked) n.status = L"Sélectionné";
            if (!item.children.empty()) n.expand = l.child == static_cast<int>(i) && depth_ > index + 1 ? 2 : 1;
            n.position = ++position; n.count = count; n.automationId = L"MenuItem";
            n.bounds = {window.left + static_cast<LONG>(Layout::pad * k), window.top + static_cast<LONG>(l.tops[i] * k), window.right - static_cast<LONG>(Layout::pad * k), window.top + static_cast<LONG>((l.tops[i] + l.heights[i]) * k)};
            nodes.push_back(std::move(n));
        }
        return nodes;
    }
public:
    // Called when the highlighted command changes (hover or keyboard, any level); nullptr when none.
    std::function<void(const Item*)> onHighlight;
    bool ignorePointer = false;  // native QA: the real pointer, wherever it is, must not move the highlight
    ~Menu() { Dispose(); }
    void Init(HINSTANCE instance, HWND owner, std::wstring root) {
        instance_ = instance; owner_ = owner; root_ = std::move(root);
        WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = instance; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.lpszClassName = L"LisiereContextMenu";
        RegisterClassW(&wc);
    }
    // Release Direct2D/WIC before CoUninitialize; the owner destroys the windows.
    // The windows belong to the owner and may outlive this object: they forget it here.
    void Dispose() { for (auto& l : levels_) { if (l->hwnd && IsWindow(l->hwnd)) SetWindowLongPtrW(l->hwnd, GWLP_USERDATA, 0); l->uia.Detach(); if (l->blur) { l->blur->Release(); l->blur = nullptr; } l->chrome.Dispose(); } levels_.clear(); depth_ = 0; }
    bool Open() const { return depth_ > 0; }
    // Dismisses an open Track() from outside (its surface closed meanwhile): it returns 0.
    void Cancel() { if (depth_ && !done_) Finish(0); }
    // Native QA (--split-test): open levels, and highlighting a row by its label as the
    // pointer (fill) or the keyboard (fill + ring) would. Disabled and read-only rows refuse.
    int Depth() const { return depth_; }
    HWND LevelWindow(int i) const { return i >= 0 && i < depth_ ? levels_[i]->hwnd : nullptr; }
    bool TestHover(int level, const std::wstring& label, bool keyboard) {
        if (level < 0 || level >= depth_) return false; auto& l = *levels_[level];
        for (size_t i = 0; i < l.items->size(); ++i) if (Plain((*l.items)[i].label) == label && Selectable((*l.items)[i])) { SetHover(level, static_cast<int>(i), keyboard); return true; }
        return false;
    }
    // Fixture hooks: the pointer as the window procedure sees it, and what a row paints now.
    void TestPointerMove(int level, const std::wstring& label) {
        if (level < 0 || level >= depth_) return; auto& l = *levels_[level];
        for (size_t i = 0; i < l.items->size(); ++i) if (Plain((*l.items)[i].label) == label) { PointerMove(level, l.tops[i] + 1); return; }
    }
    void TestPointerLeave(int level) { if (level >= 0 && level < depth_) PointerLeave(level); }
    bool TestKey(WPARAM key) { return Key(key); }
    float TestRowFill(int level, const std::wstring& label, Motion::Time now) const {
        if (level < 0 || level >= depth_) return -1; const auto& l = *levels_[level];
        for (size_t i = 0; i < l.items->size(); ++i) if (Plain((*l.items)[i].label) == label) return (*l.items)[i].enabled ? RowFill(l.motion.Visual(static_cast<int>(i) + 1, now)) : 0;
        return -1;
    }
    bool TestEntering(int level) const { return level >= 0 && level < depth_ && levels_[level]->entering; }
    int TestHoverRow(int level) const { return level >= 0 && level < depth_ ? levels_[level]->hover : -2; }
    // Modal: returns the chosen item's id, 0 when dismissed. `at` is a screen
    // point; `exclude` (keyboard-opened menus) is the control to drop below.
    // alignRight: a keyboard-opened menu ends at the control's right edge (gear-side controls).
    // Bookmark menus: the whole gesture. `background` runs on a middle click without closing;
    // a right click (or the Menu key) on a row with an id returns {id, context = true}.
    bool LastChoiceKeyboard() const { return choice_.keyboard; }   // after Track(): was the pick made from the keyboard?
    Choice TrackChoice(const std::vector<Item>& items, POINT at, const RECT* exclude, bool keyboard, bool reduced, bool alignRight, std::function<void(int)> background) {
        background_ = std::move(background); contextRows_ = true;
        const int id = Track(items, at, exclude, keyboard, reduced, alignRight);
        background_ = nullptr; contextRows_ = false;
        Choice c = choice_; c.id = id; if (!id) c = {};
        return c;
    }
    // Test and host hooks: the UIA snapshot of an open level, its window, the open depth.
    std::vector<Accessibility::Node> AccessibleNodes(int index) const { return Nodes(index); }
    // fromChecked: a keyboard-opened list (a select's options) starts on its checked row, not the first.
    int Track(const std::vector<Item>& items, POINT at, const RECT* exclude = nullptr, bool keyboard = false, bool reduced = false, bool alignRight = false, bool fromChecked = false) {
        if (!owner_ || items.empty() || depth_) return 0;
        result_ = 0; done_ = false; reduced_ = reduced; choice_ = {}; keyedChoice_ = keyboard;
        int flipX = at.x, flipY = at.y;
        const bool fromRight = exclude && alignRight;
        if (exclude) { at = {fromRight ? exclude->right : exclude->left, exclude->bottom}; flipX = fromRight ? exclude->right : exclude->right; flipY = exclude->top; }
        Open(0, items, at, flipX, flipY, true, fromRight);
        if (!depth_) return 0;
        if (keyboard) {
            int start = -1;
            if (fromChecked) for (size_t i = 0; i < items.size(); ++i) if (items[i].checked && Selectable(items[i])) { start = static_cast<int>(i); break; }
            if (start >= 0) SetHover(0, start, true); else Move(0, 1, true);
        }
        MSG msg;
        while (!done_ && depth_ && GetMessageW(&msg, nullptr, 0, 0) > 0) {
            // A press outside every level dismisses the menu and is swallowed, like Windows menus.
            const bool press = msg.message == WM_LBUTTONDOWN || msg.message == WM_RBUTTONDOWN || msg.message == WM_MBUTTONDOWN ||
                               msg.message == WM_NCLBUTTONDOWN || msg.message == WM_NCRBUTTONDOWN || msg.message == WM_NCMBUTTONDOWN;
            if (press && !LevelOf(msg.hwnd)) { Finish(0); break; }
            if (msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN) { if (Key(msg.wParam)) continue; }
            if (msg.message == WM_CHAR || msg.message == WM_SYSCHAR || msg.message == WM_KEYUP || msg.message == WM_SYSKEYUP) continue;
            TranslateMessage(&msg); DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) { PostQuitMessage(static_cast<int>(msg.wParam)); break; }
        }
        CloseFrom(0); if (highlighted_ && onHighlight) onHighlight(nullptr); highlighted_ = nullptr;
        return result_;
    }
};
} // namespace ContextMenu
} // namespace NativeChrome
