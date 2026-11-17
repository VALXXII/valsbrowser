#pragma once
#include "i18n.h"
// "Favoris" drawer (Ctrl+Maj+O, "Gérer les favoris"): the History drawer's twin, docked
// to the same right edge under the toolbar, 268 DIP wide. Header + close, a search
// field (type anywhere to filter: every word in the title or the address), then the
// tree: "Barre de favoris" and "Autres favoris" and their folders, folding on their
// chevron, 16 DIP of indent per level, 48 DIP rows (32 DIP tile + title + detail).
// Hovered or focused rows show their actions ("+" new tab, "×" remove).
// It is also the complete accessible path to every folder: a UIA List whose items
// carry name, address, level, position and an expand/collapse state.
// Keyboard: Up/Down rows, Right/Left unfold/fold (or go to the parent), Enter open
// (Ctrl/Shift: new tab), Delete remove (undo toast), F2 edit, Alt+Up/Down move the
// row within its folder, Shift+F10 / Menu key: the row's menu, Escape clears the
// search then closes.
#include <windowsx.h>
#include <dwmapi.h>
#include <commctrl.h>
#include <functional>
#include <set>
#include <vector>
#include "chrome.h"
#include "native-ui.h"
#include "download-panel.h"
#include "history.h"
#include "bookmarks.h"
#include "accessibility.h"

namespace NativeChrome {
namespace BookmarksUI {
namespace Panel = Downloads::Panel;
namespace DrawerMotion = Downloads::DrawerMotion;
using Downloads::Lane;
namespace Layout {
inline constexpr float width = Panel::width, indent = 16, firstRow = HistoryView::Layout::viewTop + 12; // 96
inline constexpr float top = Downloads::DrawerLayout::top;
}
constexpr int CloseHit = 1, SearchHit = 2, ClearHit = 3, RowBase = 1000;
inline int RowHit(int i) { return RowBase + i * 3; }
inline bool IsRowPart(int hit) { return hit >= RowBase; }
inline int HitIndex(int hit) { return (hit - RowBase) / 3; }
inline int HitPart(int hit) { return (hit - RowBase) % 3; } // 0 row, 1 first action, 2 second action

struct Row {
    Bookmarks::Id id = 0; int level = 0; bool folder = false, open = false, local = false;
    std::wstring title, detail, site, url; std::size_t count = 0;
};

class Drawer {
public:
    struct Hooks {
        std::function<void(Motion::Time)> schedule;
        std::function<void(const std::wstring&, bool)> open;          // url, new tab
        std::function<void(Bookmarks::Id)> remove;                    // with undo
        std::function<void(Bookmarks::Id, RECT)> edit;                // popover anchored to the row (screen)
        std::function<void(Bookmarks::Id, int)> move;                 // -1 up, +1 down within its folder
        std::function<void(Bookmarks::Id, POINT, bool)> context;      // the row's menu at a screen point
        std::function<void(bool)> closed;
        std::function<std::shared_ptr<const FaviconImage>(const std::wstring&)> icon;
        std::function<void(const std::wstring&)> announce;
        // In-place rename of a row title (design/bookmark-inline-rename-plan.md): the owner's editor
        // colours its EDIT (0: not ours) and follows the row after every paint (scroll, rebuild, resize).
        std::function<LRESULT(HDC, HWND)> editColor;
        std::function<void()> relayout;
    };
private:
    HWND hwnd_ = nullptr, owner_ = nullptr;
    Chrome chrome_; Motion::State motion_; Hooks hooks_; Accessibility::Bridge uia_;
    const Bookmarks::Store* store_ = nullptr;
    std::vector<Row> rows_; std::set<Bookmarks::Id> open_{Bookmarks::BarRoot, Bookmarks::OtherRoot};
    std::wstring query_; Lane slide_, scroll_;
    int hover_ = 0, pressed_ = 0, focus_ = 0; bool keyboard_ = false, visible_ = false, closing_ = false, tracking_ = false;
    int appliedVisible_ = -1; RECT appliedOwner_{};
    Bookmarks::Id renaming_ = 0;   // its title is drawn by the owner's in-place editor
    float Dpi() const { return static_cast<float>(GetDpiForWindow(hwnd_ ? hwnd_ : owner_)); }
    int Px(float dip) const { return static_cast<int>(std::lround(dip * Dpi() / 96.f)); }
    int OwnerPx(float dip) const { return static_cast<int>(std::lround(dip * GetDpiForWindow(owner_) / 96.f)); }
    float Height() const { RECT r{}; if (hwnd_) GetClientRect(hwnd_, &r); return r.bottom * 96.f / Dpi(); }
    RECT OwnerClient() const { RECT r{}; GetClientRect(owner_, &r); MapWindowPoints(owner_, nullptr, reinterpret_cast<POINT*>(&r), 2); return r; }
    bool Reduced() const { return motion_.ReducedMotion(); }
    float RowTop(int i, float scroll) const { return Layout::firstRow + i * Panel::pitch - scroll; }
    float Indent(const Row& r) const { return r.level * Layout::indent; }
    float MaxScroll() const { return std::max(0.f, Layout::firstRow + rows_.size() * Panel::pitch + Panel::padding - Height()); }
    static std::wstring Path(const Bookmarks::Store& s, Bookmarks::Id id) {
        std::wstring path; const auto* n = s.Find(id); int guard = 0;
        while (n && n->parent && guard++ < 16) { const auto* p = s.Find(n->parent); if (!p) break; path = path.empty() ? p->title : p->title + L" › " + path; n = p; }
        return path;
    }
    static bool Matches(const Bookmarks::Node& n, const std::wstring& query) {
        const std::wstring q = AddressBar::Lower(AddressBar::Trim(query)); if (q.empty()) return true;
        const std::wstring title = AddressBar::Lower(Bookmarks::DisplayTitle(n)), text = AddressBar::Lower(AddressBar::MatchText(n.url));
        size_t start = 0;
        while (start < q.size()) {
            const size_t end = q.find_first_of(L" \t", start); const std::wstring term = q.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
            if (!term.empty() && title.find(term) == std::wstring::npos && text.find(term) == std::wstring::npos) return false;
            if (end == std::wstring::npos) break; start = end + 1;
        }
        return true;
    }
    Row MakeRow(const Bookmarks::Node& n, int level) const {
        Row r; r.id = n.id; r.level = level; r.folder = n.kind == Bookmarks::Kind::Folder; r.title = Bookmarks::DisplayTitle(n); r.url = n.url;
        if (r.folder) { r.count = store_->CountLinks(n.id); r.open = open_.count(n.id) > 0; r.detail = r.count == 0 ? std::wstring(TR(bookmarks_folder_empty)) : TRP(bookmarks_count, r.count); }
        else { AddressBar::Parts p; r.local = AddressBar::Split(n.url, p) && p.scheme == L"file"; r.detail = HistoryView::HostOf(n.url); r.site = AddressBar::SiteOf(n.url); }
        if (r.title.empty()) r.title = r.folder ? std::wstring(TR(bookmarks_unnamed_folder)) : n.url;
        return r;
    }
    void Rebuild() {
        const auto keep = IsRowPart(focus_) && HitIndex(focus_) < static_cast<int>(rows_.size()) ? rows_[HitIndex(focus_)].id : 0;
        rows_.clear(); if (!store_) return;
        if (!AddressBar::Trim(query_).empty()) {
            for (Bookmarks::Id root : {Bookmarks::BarRoot, Bookmarks::OtherRoot}) for (Bookmarks::Id top : store_->Root(root).children) for (Bookmarks::Id id : store_->Subtree(top)) {
                const auto* n = store_->Find(id); if (!n || !Matches(*n, query_)) continue;
                Row r = MakeRow(*n, 0); const auto path = Path(*store_, id); if (!path.empty()) r.detail = path + (r.folder ? L"" : L" · " + r.detail); r.open = false; rows_.push_back(r);
            }
        } else {
            // Explicit stack (no recursion over data read from disk), children in display order.
            for (Bookmarks::Id root : {Bookmarks::BarRoot, Bookmarks::OtherRoot}) {
                std::vector<std::pair<Bookmarks::Id, int>> stack{{root, 0}};
                while (!stack.empty()) {
                    const auto [id, level] = stack.back(); stack.pop_back();
                    const auto* n = store_->Find(id); if (!n) continue;
                    if (id == Bookmarks::OtherRoot && n->children.empty()) continue;
                    rows_.push_back(MakeRow(*n, level));
                    if (n->kind == Bookmarks::Kind::Folder && open_.count(id) && level < 32)
                        for (auto it = n->children.rbegin(); it != n->children.rend(); ++it) stack.push_back({*it, level + 1});
                }
            }
        }
        if (keep) { for (size_t i = 0; i < rows_.size(); ++i) if (rows_[i].id == keep) { focus_ = RowHit(static_cast<int>(i)) + HitPart(focus_); break; } }
        if (IsRowPart(focus_) && HitIndex(focus_) >= static_cast<int>(rows_.size())) focus_ = rows_.empty() ? SearchHit : RowHit(static_cast<int>(rows_.size()) - 1);
    }
    // ---- Geometry of a row's parts -------------------------------------------------
    D2D1_RECT_F Tile(const Row& r, float top) const { const float x = Panel::padding + Indent(r); return {x, top, x + Panel::tile, top + Panel::tile}; }
    D2D1_RECT_F Action(float top, int slot) const { auto b = Panel::ActionBox(top, slot); return {b.left, b.top, b.right, b.bottom}; }
    D2D1_RECT_F Caret(float top) const { auto b = Panel::ActionBox(top, 1); return {b.left, b.top, b.right, b.bottom}; }
    int Actions(const Row& r) const { return r.folder ? (r.level == 0 ? 0 : 1) : 2; }
    int Hit(float x, float y) const {
        auto in = [&](Design::Box b) { return x >= b.left && x < b.right && y >= b.top && y < b.bottom; };
        if (in(Panel::Close())) return CloseHit;
        if (!query_.empty() && in(HistoryView::Layout::Clear())) return ClearHit;
        if (in(HistoryView::Layout::Search())) return SearchHit;
        if (y < HistoryView::Layout::viewTop) return 0;
        const float scroll = scroll_.Value(Motion::Now());
        for (int i = 0; i < static_cast<int>(rows_.size()); ++i) {
            const float top = RowTop(i, scroll); auto band = Panel::Hover(top); if (y < band.top || y >= band.bottom) continue;
            const Row& r = rows_[i]; const int n = Actions(r);
            for (int s = 0; s < n; ++s) { const int slot = n == 1 ? 0 : s; const auto a = Action(top, slot); if (x >= a.left && x < a.right && y >= a.top && y < a.bottom) return RowHit(i) + 1 + s; }
            return RowHit(i);
        }
        return 0;
    }
    std::vector<int> Order() const { std::vector<int> o{CloseHit, SearchHit}; if (!query_.empty()) o.push_back(ClearHit); for (int i = 0; i < static_cast<int>(rows_.size()); ++i) o.push_back(RowHit(i)); return o; }
    void Interact(int hover, int pressed) { hover_ = hover; pressed_ = pressed; auto now = Motion::Now(); motion_.SetInteraction(hover_, pressed_ == hover_ ? pressed_ : 0, now); Invalidate(); if (hooks_.schedule) hooks_.schedule(now); }
    void Reveal(int hit) {
        if (!IsRowPart(hit)) return; const int i = HitIndex(hit); auto now = Motion::Now();
        const float top = Layout::firstRow + i * Panel::pitch, scroll = scroll_.Target(), viewTop = HistoryView::Layout::viewTop + 8, viewBottom = Height() - Panel::pitch;
        float target = scroll; if (top - scroll < viewTop) target = top - viewTop; else if (top + Panel::tile - scroll > viewBottom) target = top + Panel::tile - viewBottom;
        target = std::clamp(target, 0.f, MaxScroll());
        if (target != scroll) { scroll_.Set(target, now, Motion::Milliseconds(0), Reduced() ? Motion::Milliseconds(0) : DrawerMotion::scroll); if (hooks_.schedule) hooks_.schedule(now); }
    }
    std::wstring Spoken(int i) const {
        const Row& r = rows_[i]; const std::wstring pos = std::to_wstring(i + 1), total = std::to_wstring(rows_.size());
        if (!r.folder) return TRF(bookmarks_spoken_link, r.title, r.detail, pos, total);
        return r.open ? TRF(bookmarks_spoken_folder_open, r.title, r.detail, pos, total) : TRF(bookmarks_spoken_folder_closed, r.title, r.detail, pos, total);
    }
    void SetFocusHit(int hit) {
        focus_ = hit; Reveal(hit); Interact(hover_, pressed_);
        if (uia_.Listening()) uia_.Raise(Key(hit), UIA_AutomationFocusChangedEventId);
        if (!hooks_.announce || !keyboard_) return;
        if (hit == SearchHit) hooks_.announce(query_.empty() ? std::wstring(TR(bookmarks_search)) : TRF(bookmarks_search_query, query_));
        else if (hit == CloseHit) hooks_.announce(TR(bookmarks_close));
        else if (IsRowPart(hit) && HitIndex(hit) < static_cast<int>(rows_.size())) {
            const int part = HitPart(hit); const Row& r = rows_[HitIndex(hit)];
            hooks_.announce(part == 0 ? Spoken(HitIndex(hit)) : r.folder || part == 2 ? std::wstring(TR(common_delete)) : std::wstring(TR(history_open_new_tab)));
        }
    }
    void SetQuery(std::wstring q) { if (q == query_) return; query_ = std::move(q); if (IsRowPart(focus_)) focus_ = SearchHit; scroll_.Snap(0); Rebuild(); Invalidate(); if (uia_.Listening()) uia_.ChildrenChanged(ListKey); if (hooks_.announce) hooks_.announce(rows_.empty() ? EmptyText() : TRP(bookmarks_results, rows_.size())); }
    std::wstring EmptyText() const { const auto q = AddressBar::Trim(query_); return q.empty() ? std::wstring(TR(bookmarks_empty)) : TRF(history_no_results, q); }
    void Toggle(int i, int force = -1) {
        if (i < 0 || i >= static_cast<int>(rows_.size()) || !rows_[i].folder || !query_.empty()) return;
        const auto id = rows_[i].id; const bool open = force < 0 ? !open_.count(id) : force == 1;
        if (open) open_.insert(id); else open_.erase(id);
        Rebuild(); scroll_.Snap(std::clamp(scroll_.Target(), 0.f, MaxScroll())); Invalidate();
        if (uia_.Listening()) { uia_.ChildrenChanged(ListKey); uia_.Raise(Key(RowHit(i)), UIA_ExpandCollapseExpandCollapseStatePropertyId); }
        if (hooks_.announce) hooks_.announce(open ? TRF(common_section_expanded, rows_[i].title) : TRF(common_section_collapsed, rows_[i].title));
    }
    RECT RowScreen(int i) const { const float top = RowTop(i, scroll_.Value(Motion::Now())); RECT r{Px(Panel::padding - 6), Px(top - 6), Px(Layout::width - Panel::padding + 6), Px(top + Panel::tile + 6)}; MapWindowPoints(hwnd_, nullptr, reinterpret_cast<POINT*>(&r), 2); return r; }
    void Activate(int hit, bool keyboard, bool newTab = false) {
        if (hit == CloseHit) { Hide(keyboard, true); return; }
        if (hit == SearchHit) { focus_ = SearchHit; Invalidate(); return; }
        if (hit == ClearHit) { SetQuery(L""); focus_ = SearchHit; Invalidate(); return; }
        if (!IsRowPart(hit)) return;
        const int i = HitIndex(hit), part = HitPart(hit); if (i >= static_cast<int>(rows_.size())) return;
        const Row r = rows_[i];
        if (part == 0) { if (r.folder) Toggle(i); else if (hooks_.open) hooks_.open(r.url, newTab); return; }
        const bool remove = r.folder || part == 2;
        if (!remove) { if (hooks_.open) hooks_.open(r.url, true); return; }
        if (hooks_.remove) hooks_.remove(r.id);
        if (keyboard) { const int next = std::min(i, static_cast<int>(rows_.size()) - 1); focus_ = next >= 0 ? RowHit(next) : SearchHit; Reveal(focus_); }
    }
    void KeyDown(WPARAM w) {
        const bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0, alt = GetKeyState(VK_MENU) < 0;
        keyboard_ = true;
        if (w == VK_ESCAPE) { if (!query_.empty()) { SetQuery(L""); SetFocusHit(SearchHit); } else Hide(true, true); return; }
        const int i = IsRowPart(focus_) ? HitIndex(focus_) : -1;
        if (alt && (w == VK_UP || w == VK_DOWN) && i >= 0 && hooks_.move && query_.empty() && rows_[i].level > 0) { hooks_.move(rows_[i].id, w == VK_UP ? -1 : 1); return; }
        const auto order = Order();
        if (w == VK_TAB || w == VK_DOWN || w == VK_UP) {
            const bool back = w == VK_UP || (w == VK_TAB && shift); int from = IsRowPart(focus_) ? RowHit(i) : focus_;
            auto it = std::find(order.begin(), order.end(), from); int k = it == order.end() ? (back ? static_cast<int>(order.size()) - 1 : 0) : static_cast<int>(it - order.begin()) + (back ? -1 : 1);
            if (w != VK_TAB) k = std::clamp(k, 0, static_cast<int>(order.size()) - 1); else k = (k + static_cast<int>(order.size())) % static_cast<int>(order.size());
            SetFocusHit(order[k]); return;
        }
        if (w == VK_HOME && !rows_.empty() && i >= 0) { SetFocusHit(RowHit(0)); return; }
        if (w == VK_END && !rows_.empty() && i >= 0) { SetFocusHit(RowHit(static_cast<int>(rows_.size()) - 1)); return; }
        if (i >= 0 && (w == VK_RIGHT || w == VK_LEFT)) {
            const Row& r = rows_[i];
            if (r.folder && HitPart(focus_) == 0) {
                if (w == VK_RIGHT && !r.open && query_.empty()) { Toggle(i, 1); return; }
                if (w == VK_LEFT && r.open && query_.empty()) { Toggle(i, 0); return; }
            }
            if (w == VK_LEFT && HitPart(focus_) == 0 && r.level > 0) { for (int k = i - 1; k >= 0; --k) if (rows_[k].level < r.level) { SetFocusHit(RowHit(k)); return; } }
            const int n = Actions(r); const int part = std::clamp(HitPart(focus_) + (w == VK_RIGHT ? 1 : -1), 0, n); SetFocusHit(RowHit(i) + part); return;
        }
        if (w == VK_DELETE && i >= 0 && Actions(rows_[i]) > 0) { Activate(RowHit(i) + Actions(rows_[i]), true); return; }
        if (w == VK_F2 && i >= 0 && rows_[i].level > 0 && hooks_.edit) { hooks_.edit(rows_[i].id, RowScreen(i)); return; }
        if ((w == VK_APPS || (w == VK_F10 && shift)) && i >= 0 && hooks_.context) { const RECT r = RowScreen(i); hooks_.context(rows_[i].id, {r.left + Px(8), r.bottom}, true); return; }
        if (w == VK_RETURN) { if (focus_ == SearchHit) { for (int k = 0; k < static_cast<int>(rows_.size()); ++k) if (!rows_[k].folder) { Activate(RowHit(k), true, shift || ctrl); return; } return; } if (focus_) Activate(focus_, true, shift || ctrl); return; }
        if (w == VK_SPACE && focus_ && focus_ != SearchHit) { Activate(focus_, true, ctrl); return; }
        if (w == VK_BACK && focus_ == SearchHit && !query_.empty()) { std::wstring q = query_; if (ctrl) { while (!q.empty() && q.back() == L' ') q.pop_back(); while (!q.empty() && q.back() != L' ') q.pop_back(); } else q.pop_back(); SetQuery(q); }
    }
    void Char(wchar_t c) {
        if (c < 0x20 || c == 0x7f) return;
        if (c == L' ' && (focus_ != SearchHit || query_.empty())) return;
        if (query_.size() >= 256) return;
        focus_ = SearchHit; SetQuery(query_ + c);
    }
    // ---- Paint ----------------------------------------------------------------------
    void Paint() {
        if (!chrome_.RenderTarget()) chrome_.Resize();
        auto* t = chrome_.RenderTarget(); if (!t || !chrome_.SolidBrush()) return;
        const auto now = Motion::Now(); const float c = Design::Surface::canvas, h = Height(), scroll = scroll_.Value(now);
        const bool focused = GetFocus() == hwnd_;
        t->BeginDraw(); t->Clear(D2D1::ColorF(c, c, c, 1)); chrome_.keyboardModality = keyboard_;
        auto R = [](Design::Box b) { return D2D1_RECT_F{b.left, b.top, b.right, b.bottom}; };
        chrome_.Text(TR(bookmarks_title), R(Panel::Title()), Panel::line, Design::Text::muted, false, true, -.16f);
        const auto cv = motion_.Visual(CloseHit, now);
        chrome_.DrawButton(L"close", R(Panel::Close()), Design::ButtonVariant::Ghost, {cv.hover, cv.press, false, focused && focus_ == CloseHit});
        {   // Search field: the History drawer's, with the accent caret.
            const auto box = R(HistoryView::Layout::Search()); const auto v = motion_.Visual(SearchHit, now);
            chrome_.Round(box, Design::Radius::control, Design::Surface::raised + (Design::Surface::hover - Design::Surface::raised) * .5f * v.hover);
            if (focused && focus_ == SearchHit && keyboard_) chrome_.FocusRing(box, Design::Radius::control);
            chrome_.Icon(L"search", R(HistoryView::Layout::SearchIcon()), Design::Size::icon, Design::Text::secondary);
            const auto text = R(HistoryView::Layout::SearchText(!query_.empty()));
            if (query_.empty()) chrome_.Text(TR(bookmarks_search), text, Panel::line, Design::Text::muted, false, false, -.16f);
            else { const float w = chrome_.TextWidth(query_, Panel::line, false, false, -.16f), shift = std::max(0.f, w - (text.right - text.left - 2));
                t->PushAxisAlignedClip(text, D2D1_ANTIALIAS_MODE_ALIASED); chrome_.Text(query_, {text.left - shift, text.top, text.left - shift + w + 4, text.bottom}, Panel::line, .9f, false, false, -.16f); t->PopAxisAlignedClip(); }
            if (focused && focus_ == SearchHit) {
                const float x = std::round(text.left + std::min(query_.empty() ? 0.f : chrome_.TextWidth(query_, Panel::line, false, false, -.16f), text.right - text.left - 2)) + .5f;
                const auto a = Design::Accent::Get(); chrome_.SolidBrush()->SetColor(D2D1::ColorF(a.r, a.g, a.b, Design::Accent::caretOpacity));
                t->DrawLine({x, (text.top + text.bottom) / 2 - 8}, {x, (text.top + text.bottom) / 2 + 8}, chrome_.SolidBrush(), 1);
            }
            if (!query_.empty()) { const auto v2 = motion_.Visual(ClearHit, now); chrome_.DrawButton(L"close", R(HistoryView::Layout::Clear()), Design::ButtonVariant::Ghost, {v2.hover, v2.press, false, focused && focus_ == ClearHit}); }
        }
        t->PushAxisAlignedClip(D2D1::RectF(0, HistoryView::Layout::viewTop, Layout::width, h), D2D1_ANTIALIAS_MODE_ALIASED);
        if (rows_.empty()) {
            chrome_.Text(EmptyText(), {Panel::padding, Layout::firstRow, Layout::width - Panel::padding, Layout::firstRow + 20}, Panel::line, Design::Text::muted, false, false, -.16f);
            if (query_.empty()) chrome_.Text(TR(bookmarks_empty_hint), {Panel::padding, Layout::firstRow + 22, Layout::width - Panel::padding, Layout::firstRow + 42}, 14, Design::Text::disabled);
        }
        for (int i = 0; i < static_cast<int>(rows_.size()); ++i) {
            const float top = RowTop(i, scroll); if (top + Panel::tile < HistoryView::Layout::viewTop - 8 || top > h) continue;
            const Row& r = rows_[i]; const int row = RowHit(i);
            const auto v = motion_.Visual(row, now);
            const bool shown = (IsRowPart(hover_) && HitIndex(hover_) == i) || (focused && IsRowPart(focus_) && HitIndex(focus_) == i);
            const auto tile = Tile(r, top);
            if (r.level == 0 && r.folder) {
                // Root sections: a label like the drawers' period headers, folding on the caret.
                chrome_.Text(r.title, {tile.left, top + 4, Layout::width - 60, top + 24}, Panel::line, Design::Text::muted, false, true, -.16f);
            } else {
                chrome_.Round(tile, Design::Radius::control, Design::Surface::raised);
                if (r.folder) chrome_.FolderGlyph(tile, Design::Size::icon, 1, r.open ? 1.f : 0.f, Reduced());
                else if (!chrome_.DrawSiteImage(hooks_.icon && !r.site.empty() ? hooks_.icon(r.site) : nullptr, tile)) chrome_.Icon(r.local ? L"file" : L"favicon", tile, Design::Size::icon, 1);
                const float textLeft = tile.right + Design::Space::md, textRight = shown ? Action(top, Actions(r) == 2 ? 0 : (r.folder ? 0 : 1)).left - 6 : Layout::width - Panel::padding - (r.folder ? 30 : 0);
                const float titleOpacity = (Design::Text::muted + (.78f - Design::Text::muted) * std::max(v.hover, shown ? 1.f : 0.f)) * (1 - .25f * v.press);
                if (r.id != renaming_) chrome_.Text(r.title, {textLeft, top - Panel::nameRaise, textRight, top + Panel::line - Panel::nameRaise}, Panel::line, titleOpacity, false, true, -.16f);
                chrome_.Text(r.detail, {textLeft, top + Panel::line, textRight, top + 2 * Panel::line}, Panel::detailText, Design::Text::muted, true, false, -.14f);
            }
            if (r.folder) {   // chevron: points right folded, down open
                const auto box = Caret(top); D2D1_MATRIX_3X2_F base; t->GetTransform(&base);
                if (!(shown && Actions(r) == 1) || r.level == 0) { t->SetTransform(D2D1::Matrix3x2F::Rotation(r.open ? 0.f : -90.f, {(box.left + box.right) / 2, (box.top + box.bottom) / 2}) * base); chrome_.Icon(L"caret", box, 16, query_.empty() ? 1.f : .4f); t->SetTransform(base); }
            }
            if (shown) {
                const int n = Actions(r);
                for (int s = 0; s < n; ++s) {
                    const int slot = n == 1 ? 0 : s; const int hit = row + 1 + s; const auto av = motion_.Visual(hit, now);
                    chrome_.DrawButton(r.folder || s == 1 ? L"close" : L"plus", Action(top, slot), Design::ButtonVariant::Ghost, {av.hover, av.press, false, focused && focus_ == hit});
                }
            }
            if (focused && focus_ == row) chrome_.FocusRing({tile.left - 6, top - 6, Layout::width - Panel::padding + 6, top + Panel::tile + 6}, Design::Radius::control + 6);
        }
        t->PopAxisAlignedClip();
        const float px = t->GetSize().width / std::max(1.f, static_cast<float>(t->GetPixelSize().width));
        chrome_.Color(1, DownloadsUI::Popup::border); t->FillRectangle(D2D1::RectF(0, 0, px, h), chrome_.SolidBrush());
        if (t->EndDraw() == D2DERR_RECREATE_TARGET) chrome_.RecreateTarget();
    }
    void ApplyGeometry(Motion::Time now, bool force = false) {
        if (!hwnd_) return;
        const RECT owner = OwnerClient(); const int column = OwnerPx(Layout::width), top = owner.top + OwnerPx(Layout::top), height = std::max(1, static_cast<int>(owner.bottom - top));
        const int shown = static_cast<int>(std::lround(column * Motion::Clamp(slide_.Value(now))));
        if (!force && shown == appliedVisible_ && EqualRect(&owner, &appliedOwner_)) return;
        appliedVisible_ = shown; appliedOwner_ = owner;
        SetWindowPos(hwnd_, nullptr, owner.right - shown, top, column, height, SWP_NOACTIVATE | SWP_NOZORDER);
        const int r = OwnerPx(20);
        SetWindowRgn(hwnd_, shown > 0 ? (IsZoomed(owner_) ? CreateRectRgn(0, 0, shown, height) : CreateRoundRectRgn(-r * 2, -r * 2, shown + 1, height + 1, r, r)) : CreateRectRgn(0, 0, 0, 0), FALSE);
    }
    void Finish() { closing_ = false; visible_ = false; appliedVisible_ = -1; ShowWindow(hwnd_, SW_HIDE); }
    // ---- UI Automation -----------------------------------------------------------------
    static constexpr long long SearchKey = 2, CloseKey = 1, ListKey = 3;
    long long Key(int hit) const { if (hit == SearchHit) return SearchKey; if (hit == CloseHit) return CloseKey; if (hit == ClearHit) return 4; if (IsRowPart(hit) && HitIndex(hit) < static_cast<int>(rows_.size())) { const auto id = rows_[HitIndex(hit)].id; return (static_cast<long long>(1 + HitPart(hit)) << 32) | id; } return 0; }
    int HitOf(long long key) const {
        if (key == SearchKey) return SearchHit; if (key == CloseKey) return CloseHit; if (key == 4) return ClearHit;
        const int part = static_cast<int>(key >> 32) - 1; const auto id = static_cast<Bookmarks::Id>(key & 0xffffffff);
        for (int i = 0; i < static_cast<int>(rows_.size()); ++i) if (rows_[i].id == id) return RowHit(i) + std::max(0, part);
        return 0;
    }
    std::vector<Accessibility::Node> Nodes() const {
        std::vector<Accessibility::Node> nodes; if (!visible_ || closing_) return nodes;
        auto screen = [&](D2D1_RECT_F r) { RECT p{Px(r.left), Px(r.top), Px(r.right), Px(r.bottom)}; MapWindowPoints(hwnd_, nullptr, reinterpret_cast<POINT*>(&p), 2); return p; };
        auto R = [](Design::Box b) { return D2D1_RECT_F{b.left, b.top, b.right, b.bottom}; };
        Accessibility::Node close; close.key = CloseKey; close.name = TR(bookmarks_close); close.accelerator = TR(key_escape); close.automationId = L"BookmarksClose"; close.bounds = screen(R(Panel::Close())); nodes.push_back(close);
        Accessibility::Node search; search.key = SearchKey; search.role = Accessibility::Role::Edit; search.name = TR(bookmarks_search); search.value = query_; search.automationId = L"BookmarksSearch"; search.bounds = screen(R(HistoryView::Layout::Search())); nodes.push_back(search);
        Accessibility::Node list; list.key = ListKey; list.role = Accessibility::Role::List; list.name = TR(bookmarks_title); list.automationId = L"BookmarksList"; list.focusable = false; RECT w{}; GetWindowRect(hwnd_, &w); list.bounds = w; nodes.push_back(list);
        const float scroll = scroll_.Value(Motion::Now()), h = Height();
        for (int i = 0; i < static_cast<int>(rows_.size()); ++i) {
            const Row& r = rows_[i]; const float top = RowTop(i, scroll);
            Accessibility::Node n; n.key = Key(RowHit(i)); n.parent = ListKey; n.role = Accessibility::Role::ListItem; n.name = r.title; n.help = r.folder ? L"Dossier, " + r.detail : r.detail;
            n.automationId = L"Bookmark." + std::to_wstring(r.id); n.position = i + 1; n.count = static_cast<int>(rows_.size()); n.level = r.level + 1;
            n.selected = IsRowPart(focus_) && HitIndex(focus_) == i; if (r.folder && query_.empty()) n.expand = r.open ? 2 : 1;
            n.offscreen = top + Panel::tile < HistoryView::Layout::viewTop || top > h; n.bounds = screen({Panel::padding, top, Layout::width - Panel::padding, top + Panel::tile});
            nodes.push_back(n);
        }
        return nodes;
    }
    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<Drawer*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (msg == WM_NCCREATE) { self = static_cast<Drawer*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); self->hwnd_ = h; }
        return self ? self->Handle(h, msg, w, l) : DefWindowProcW(h, msg, w, l);
    }
    LRESULT Handle(HWND h, UINT msg, WPARAM w, LPARAM l) {
        const float s = 96.f / Dpi();
        switch (msg) {
        case WM_GETOBJECT: { LRESULT result; if (uia_.OnGetObject(h, w, l, result)) return result; break; }
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); Paint(); EndPaint(h, &ps); if (renaming_ && hooks_.relayout) hooks_.relayout(); return 0; }
        case WM_CTLCOLOREDIT: if (hooks_.editColor) { if (const LRESULT brush = hooks_.editColor(reinterpret_cast<HDC>(w), reinterpret_cast<HWND>(l))) return brush; } break;
        case WM_SIZE: chrome_.Resize(); Invalidate(); return 0;
        // Placed on a monitor at another DPI: WM_SIZE read the old DPI during the move; redraw at the new one.
        case WM_DPICHANGED: chrome_.Resize(); Invalidate(); return 0;
        case WM_MOUSEACTIVATE: return MA_ACTIVATE;
        case WM_ACTIVATE: if (LOWORD(w) == WA_INACTIVE && !holding_) { HWND to = reinterpret_cast<HWND>(l); if (!to || GetWindow(to, GW_OWNER) != owner_ || to == owner_) Hide(false, true); } return 0;
        case WM_SETFOCUS: case WM_KILLFOCUS: Invalidate(); return 0;
        case WM_SETCURSOR: if (LOWORD(l) == HTCLIENT) { POINT p; GetCursorPos(&p); ScreenToClient(h, &p); SetCursor(LoadCursor(nullptr, Hit(p.x * s, p.y * s) == SearchHit ? IDC_IBEAM : IDC_ARROW)); return TRUE; } break;
        case WM_MOUSEMOVE: { if (!tracking_) { TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, h, 0}; TrackMouseEvent(&t); tracking_ = true; } const int hit = Hit(GET_X_LPARAM(l) * s, GET_Y_LPARAM(l) * s); if (hit != hover_) Interact(hit, pressed_); return 0; }
        case WM_MOUSELEAVE: tracking_ = false; Interact(0, 0); return 0;
        case WM_LBUTTONDOWN: { if (renaming_) SetFocus(h); /* a press elsewhere ends the in-place rename (blur commits) */ keyboard_ = false; const int hit = Hit(GET_X_LPARAM(l) * s, GET_Y_LPARAM(l) * s); if (hit == SearchHit) focus_ = SearchHit; if (hit) { SetCapture(h); Interact(hit, hit); } return 0; }
        case WM_LBUTTONUP: { const int hit = Hit(GET_X_LPARAM(l) * s, GET_Y_LPARAM(l) * s), was = pressed_; if (GetCapture() == h) ReleaseCapture(); Interact(hit, 0); if (was && was == hit) { if (IsRowPart(hit)) focus_ = RowHit(HitIndex(hit)); Activate(hit, false, (w & (MK_CONTROL | MK_SHIFT)) != 0); } return 0; }
        case WM_MBUTTONUP: { const int hit = Hit(GET_X_LPARAM(l) * s, GET_Y_LPARAM(l) * s); if (IsRowPart(hit) && HitPart(hit) == 0 && !rows_[HitIndex(hit)].folder && hooks_.open) hooks_.open(rows_[HitIndex(hit)].url, true); return 0; }
        case WM_RBUTTONDOWN: if (renaming_) SetFocus(h); return 0;
        case WM_RBUTTONUP: { const int hit = Hit(GET_X_LPARAM(l) * s, GET_Y_LPARAM(l) * s); if (IsRowPart(hit) && hooks_.context) { const int i = HitIndex(hit); if (rows_[i].level == 0) return 0; focus_ = RowHit(i); POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)}; ClientToScreen(h, &p); hooks_.context(rows_[i].id, p, false); } return 0; }
        case WM_MOUSEWHEEL: { auto now = Motion::Now(); scroll_.Set(std::clamp(scroll_.Target() - GET_WHEEL_DELTA_WPARAM(w) / 120.f * Panel::pitch * 1.5f, 0.f, MaxScroll()), now, Motion::Milliseconds(0), Reduced() ? Motion::Milliseconds(0) : DrawerMotion::scroll); if (hooks_.schedule) hooks_.schedule(now); return 0; }
        case WM_KEYDOWN: case WM_SYSKEYDOWN: if (msg == WM_SYSKEYDOWN && w != VK_UP && w != VK_DOWN && w != VK_F10) break; KeyDown(w); return 0;
        case WM_CHAR: Char(static_cast<wchar_t>(w)); return 0;
        case WM_APP + 1: { const int hit = HitOf(static_cast<long long>(l)); if (!hit) return 0; if (w == 1) Activate(hit, true); else if (w == 2 || w == 3) { if (IsRowPart(hit)) Toggle(HitIndex(hit), w == 2 ? 1 : 0); } else if (w == 4) { keyboard_ = true; SetFocusHit(hit); } return 0; }
        }
        return DefWindowProcW(h, msg, w, l);
    }
public:
    bool holding_ = false; // a menu or the popover opened from the drawer: deactivation keeps it open
    ULONGLONG hiddenAt = 0;
    ~Drawer() { Dispose(); }
    void Dispose() { uia_.Detach(); chrome_.Dispose(); hwnd_ = nullptr; visible_ = false; }
    bool Create(HINSTANCE instance, HWND owner, const std::wstring& root, const Bookmarks::Store* store, Hooks hooks) {
        owner_ = owner; store_ = store; hooks_ = std::move(hooks); motion_.EnablePolish(); chrome_.SetMotion(&motion_); chrome_.textCacheLimit = 1024;
        WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = instance; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.lpszClassName = L"LisiereBookmarksDrawer";
        RegisterClassW(&wc);
        CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, TR(bookmarks_title), WS_POPUP, 0, 0, Px(Layout::width), Px(400), owner, nullptr, instance, this);
        if (!hwnd_) return false;
        NativeUI::ConfigureShadowlessPopup(hwnd_); // DWMWCP_DONOTROUND, no shadow: the region shapes it
        Accessibility::Host host; host.window = hwnd_;
        host.nodes = [this] { return Nodes(); };
        host.focused = [this]() -> long long { return GetFocus() == hwnd_ ? Key(IsRowPart(focus_) ? focus_ : focus_) : 0; };
        host.invoke = [this](long long key) { PostMessageW(hwnd_, WM_APP + 1, 1, static_cast<LPARAM>(key)); };
        host.select = [this](long long key) { PostMessageW(hwnd_, WM_APP + 1, 4, static_cast<LPARAM>(key)); };
        host.focus = [this](long long key) { PostMessageW(hwnd_, WM_APP + 1, 4, static_cast<LPARAM>(key)); };
        host.expand = [this](long long key, bool open) { PostMessageW(hwnd_, WM_APP + 1, open ? 2 : 3, static_cast<LPARAM>(key)); };
        host.setValue = [this](long long key, const std::wstring& value) { if (key == SearchKey) { focus_ = SearchHit; SetQuery(value.substr(0, 256)); } };
        // The in-place rename EDIT on a row (a child HWND): named like the popover's field.
        host.label = [this](HWND child, Accessibility::Node& node) { if (!child || !renaming_ || GetParent(child) != hwnd_ || child == hwnd_) return false; node.name = TR(bookmark_field_name); node.automationId = L"BookmarkRename" /* i18n-exempt: UIA id */; return true; };
        uia_.Attach(std::move(host));
        return SUCCEEDED(chrome_.Init(hwnd_, root));
    }
    HWND Window() const { return hwnd_; }
    UINT RenderDpiForTest() const { return chrome_.dpi; }   // the DPI the drawer is drawn at (tests/ui/address_popup_dpi_test.cpp)
    bool Visible() const { return visible_; }
    bool Open() const { return visible_ && !closing_; }
    bool Closing() const { return closing_; }
    const std::vector<Row>& Rows() const { return rows_; }
    const std::wstring& Query() const { return query_; }
    int Focus() const { return focus_; }
    void Invalidate() { if (hwnd_ && visible_) InvalidateRect(hwnd_, nullptr, FALSE); }
    void SetReducedMotion(bool reduced) { motion_.SetReducedMotion(reduced); Invalidate(); }
    void Show(bool keyboard) {
        if (!hwnd_) return;
        motion_.SetReducedMotion(Motion::PreferredReducedMotion()); auto now = Motion::Now();
        if (!visible_) { slide_.Snap(0); scroll_.Snap(0); query_.clear(); motion_.ClearInteractions(); hover_ = pressed_ = 0; appliedVisible_ = -1; appliedOwner_ = {}; }
        Rebuild(); focus_ = SearchHit; keyboard_ = keyboard;
        slide_.Set(1, now, Motion::Milliseconds(0), Reduced() ? Motion::Milliseconds(0) : DrawerMotion::enter, DrawerMotion::enterSpring);
        closing_ = false; visible_ = true; ApplyGeometry(now, true);
        SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        Paint(); ValidateRect(hwnd_, nullptr); SetForegroundWindow(hwnd_); SetFocus(hwnd_);
        if (uia_.Listening()) { uia_.ChildrenChanged(ListKey); uia_.Raise(SearchKey, UIA_AutomationFocusChangedEventId); }
        std::size_t links = 0; if (store_) links = store_->CountLinks(Bookmarks::BarRoot) + store_->CountLinks(Bookmarks::OtherRoot);
        if (hooks_.announce) hooks_.announce(links ? TRP(bookmarks_announce_count, links) : std::wstring(TR(bookmarks_announce_none)));
        if (hooks_.schedule) hooks_.schedule(now);
    }
    void Hide(bool keyboard, bool animate) {
        if (!visible_ || closing_) { if (visible_ && !animate) Finish(); return; }
        hiddenAt = GetTickCount64(); if (GetCapture() == hwnd_) ReleaseCapture(); motion_.ClearInteractions(); hover_ = pressed_ = 0;
        auto now = Motion::Now(); closing_ = true;
        slide_.Set(0, now, Motion::Milliseconds(0), animate && !Reduced() ? DrawerMotion::exit : Motion::Milliseconds(0), DrawerMotion::exitSpring);
        if (!slide_.Moving(now)) Finish();
        if (hooks_.closed) hooks_.closed(keyboard);
        if (visible_ && hooks_.schedule) hooks_.schedule(now);
    }
    void Reposition() { if (visible_) ApplyGeometry(Motion::Now()); }
    // The store changed (add, remove, move, rename, undo).
    // In-place rename: the title box of the (non-root) row `id`, in drawer client pixels, when the whole
    // title line is inside the list's visible area; false otherwise (folded away, filtered, scrolled out).
    bool TitleBox(Bookmarks::Id id, RECT& box) const {
        if (!hwnd_ || !visible_) return false;
        const float scroll = scroll_.Value(Motion::Now()), h = Height();
        for (int i = 0; i < static_cast<int>(rows_.size()); ++i) {
            const Row& r = rows_[i]; if (r.id != id) continue; if (r.level == 0) return false;
            const float top = RowTop(i, scroll), lineTop = top - Panel::nameRaise, lineBottom = top + Panel::line - Panel::nameRaise;
            if (lineTop < HistoryView::Layout::viewTop || lineBottom > h) return false;
            const float left = Tile(r, top).right + Design::Space::md, right = Action(top, Actions(r) == 2 ? 0 : (r.folder ? 0 : 1)).left - 6;
            if (right <= left + 8) return false;
            box = {Px(left), Px(lineTop), Px(right), Px(lineBottom)};
            return true;
        }
        return false;
    }
    void SetRenaming(Bookmarks::Id id) { if (renaming_ == id) return; renaming_ = id; Invalidate(); }
    Bookmarks::Id Renaming() const { return renaming_; }
    void Changed() { if (!hwnd_ || !visible_) return; Rebuild(); scroll_.Snap(std::clamp(scroll_.Target(), 0.f, MaxScroll())); Invalidate(); if (uia_.Listening()) uia_.ChildrenChanged(ListKey); }
    // Keeps a row focused (after a move or an edit).
    void FocusBookmark(Bookmarks::Id id) { for (int i = 0; i < static_cast<int>(rows_.size()); ++i) if (rows_[i].id == id) { keyboard_ = true; SetFocusHit(RowHit(i)); return; } }
    Motion::Wake NextWake(Motion::Time now) const {
        if (!visible_) return {};
        const bool moving = slide_.Moving(now) || scroll_.Moving(now);
        return Motion::Earlier(motion_.NextWake(now), moving ? Motion::Frame() : Motion::Wake{});
    }
    void Frame(Motion::Time now) {
        if (!visible_) return;
        ApplyGeometry(now);
        if (closing_ && !slide_.Moving(now)) { Finish(); return; }
        if (NextWake(now).delay || painted_) Invalidate();
        painted_ = NextWake(now).delay.has_value();
    }
    bool painted_ = false;
    // Test hooks (smoke run and fixtures).
    void TypeForTest(const std::wstring& text) { for (wchar_t c : text) Char(c); }
    void KeyForTest(WPARAM key) { KeyDown(key); }
    void PaintForTest() { Paint(); }
    std::vector<Accessibility::Node> AccessibleNodesForTest() const { return Nodes(); }
};
} // namespace BookmarksUI
} // namespace NativeChrome
