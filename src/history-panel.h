#pragma once
#include "i18n.h"
// Native "History" drawer, opened by the toolbar's History button (or Ctrl+H),
// beside Downloads. It is the download drawer's twin, docked to the same right
// edge under the toolbar, below the button that opens it: same 268 DIP column, header, period sections folding on
// their caret, 48 DIP rows whose actions slide in un-blurring on hover, scroll
// edge fades, spring slide in/out along one path, nothing awake at rest. It adds
// a search field (type anywhere to filter) and opens a page on row click.
//  - View: paints one frame from a Scene (also used by tests/history fixture).
//  - Drawer: window, geometry, motion, pointer, keyboard, tooltips.
#include <windowsx.h>
#include <dwmapi.h>
#include "drawer-corners.h"   // Mode Focus: the docked drawer's antialiased top corners
#include "resize-border.h"     // the window's resize border wins over the drawer on the window's edges
#include <d2d1_1.h>
#include <commctrl.h>
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <vector>
#include "chrome.h"
#include "download-panel.h" // RowReveal, Lane, springs, the reveal blur (DownloadsUI::View::Element)
#include "history.h"

namespace NativeChrome {
namespace HistoryUI {
using namespace HistoryView;
using Downloads::Panel::pitch;
namespace Panel = Downloads::Panel;
namespace DrawerLayout = Downloads::DrawerLayout;
namespace DrawerMotion = Downloads::DrawerMotion;
using Downloads::RowReveal;
using Downloads::Lane;

// Hit identifiers. Rows: RowHit(i) is the row itself (opens the page), the two
// actions follow (0 = open in a new tab, 1 = remove from history).
constexpr int CloseHit = 1, SearchHit = 2, ClearHit = 3, SectionBase = 10, RowBase = 1000;
inline int SectionHit(Period p) { return SectionBase + static_cast<int>(p); }
inline bool IsSection(int hit) { return hit >= SectionBase && hit < SectionBase + PeriodCount; }
inline Period HitPeriod(int hit) { return static_cast<Period>(hit - SectionBase); }
inline int RowHit(int index) { return RowBase + index * 3; }
inline int ActionHit(int index, int slot) { return RowBase + index * 3 + 1 + slot; }
inline bool IsRowPart(int hit) { return hit >= RowBase; }
inline int HitIndex(int hit) { return (hit - RowBase) / 3; }
inline int HitPart(int hit) { return (hit - RowBase) % 3; } // 0 row, 1 open in new tab, 2 remove
inline const wchar_t* ActionIcon(int slot) { return slot == 0 ? L"plus" : L"close"; } // "plus": the toolbar's new tab glyph
inline const wchar_t* ActionTip(int slot) { return slot == 0 ? TR(history_open_new_tab) : TR(downloads_action_remove); }

// Everything a frame depends on; the offscreen fixture builds one directly.
struct Scene {
    const std::vector<Entry>* entries = nullptr;
    const std::vector<Slot>* slots = nullptr;
    const std::map<int, RowReveal>* reveals = nullptr; // entry index -> reveal
    const Motion::State* motion = nullptr;
    std::function<std::shared_ptr<const FaviconImage>(const std::wstring&)> icon;
    std::wstring query;
    int focus = 0; bool keyboard = false, caret = false;
    float height = 600, fadeTop = 0, fadeBottom = 0, edgeOpacity = 1, originX = 0; // originX: slide offset (DIP)
    std::int64_t nowMs = 0, offsetMs = 0;
    Motion::Time now{};
};

class View {
    Chrome& chrome_;
    DownloadsUI::View reveal_{chrome_}; // reuses the download rows' blurred reveal layer
    ID2D1GradientStopCollection* stops_ = nullptr; ID2D1LinearGradientBrush* fade_ = nullptr;
    template<class T> static void Drop(T*& p) { if (p) { p->Release(); p = nullptr; } }
    Motion::InteractionVisual Interaction(const Scene& s, int hit) const { return s.motion ? s.motion->Visual(hit, s.now) : Motion::InteractionVisual{}; }
    bool Reduced(const Scene& s) const { return s.motion && s.motion->ReducedMotion(); }
    static D2D1_RECT_F R(Design::Box b) { return {b.left, b.top, b.right, b.bottom}; }
    void Prepare() {
        reveal_.Prepare();
        auto* target = chrome_.RenderTarget();
        if (!fade_ && target) {
            const float c = Design::Surface::canvas;
            D2D1_GRADIENT_STOP stops[] = {{0, D2D1::ColorF(c, c, c, 0)}, {1, D2D1::ColorF(c, c, c, 1)}};
            if (FAILED(target->CreateGradientStopCollection(stops, 2, &stops_)) ||
                FAILED(target->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({0, 0}, {1, 0}), stops_, &fade_))) { Drop(fade_); Drop(stops_); }
        }
    }
    void Search(const Scene& s) {
        auto* target = chrome_.RenderTarget();
        const auto box = R(Layout::Search());
        const auto v = Interaction(s, SearchHit);
        const bool focused = s.focus == SearchHit;
        // Raised like the address field; a touch lighter under the pointer.
        chrome_.Round(box, Design::Radius::control, Design::Surface::raised + (Design::Surface::hover - Design::Surface::raised) * .5f * v.hover);
        if (focused && s.keyboard) chrome_.FocusRing(box, Design::Radius::control);
        chrome_.Icon(L"search", R(Layout::SearchIcon()), Design::Size::icon, Design::Text::secondary);
        const bool clear = !s.query.empty();
        const auto text = R(Layout::SearchText(clear));
        if (s.query.empty()) chrome_.Text(TR(history_search_placeholder), text, Panel::line, Design::Text::muted, false, false, -.16f);
        else {
            // The end of a long query stays in view, like an edit field scrolled to its caret.
            const float w = chrome_.TextWidth(s.query, Panel::line, false, false, -.16f), room = text.right - text.left - 2;
            const float shift = std::max(0.f, w - room);
            target->PushAxisAlignedClip(text, D2D1_ANTIALIAS_MODE_ALIASED);
            chrome_.Text(s.query, {text.left - shift, text.top, text.left - shift + w + 4, text.bottom}, Panel::line, .9f, false, false, -.16f);
            target->PopAxisAlignedClip();
        }
        if (s.caret && focused) { // static caret: no blink timer at rest
            const float x = std::round(text.left + std::min(s.query.empty() ? 0.f : chrome_.TextWidth(s.query, Panel::line, false, false, -.16f), text.right - text.left - 2)) + .5f;
            const auto a = Design::Accent::Get(); chrome_.SolidBrush()->SetColor(D2D1::ColorF(a.r, a.g, a.b, Design::Accent::caretOpacity));
            target->DrawLine({x, text.top - 1}, {x, text.bottom + 1}, chrome_.SolidBrush(), 1);
        }
        if (clear) {
            const auto c = Interaction(s, ClearHit);
            chrome_.DrawButton(L"close", R(Layout::Clear()), Design::ButtonVariant::Ghost, {c.hover, c.press, false, s.focus == ClearHit});
        }
    }
    void Section(Period period, float y, float open, const Scene& s) {
        auto* target = chrome_.RenderTarget();
        chrome_.Text(Downloads::PeriodLabel(period), R(DrawerLayout::Label(y)), Panel::line, Design::Text::muted, false, true, -.16f);
        const int hit = SectionHit(period); const auto v = Interaction(s, hit);
        const auto box = R(DrawerLayout::Caret(y));
        const auto style = Design::Resolve(Design::ButtonVariant::Ghost, {v.hover, v.press, false, s.focus == hit});
        if (style.fill) chrome_.Round(box, style.radius, style.surface);
        if (style.focus) chrome_.FocusRing(box, style.radius);
        D2D1_MATRIX_3X2_F base; target->GetTransform(&base);
        const D2D1_POINT_2F center{(box.left + box.right) / 2, (box.top + box.bottom) / 2};
        target->SetTransform(D2D1::Matrix3x2F::Rotation(-90.f * (1 - Motion::Clamp(open)), center) * base);
        chrome_.Icon(L"caret", box, style.iconSize * Motion::PressScale(v.press, Reduced(s)), style.iconOpacity);
        target->SetTransform(base);
    }
    void Row(int index, float top, const Scene& s) {
        const auto& e = (*s.entries)[index];
        auto* target = chrome_.RenderTarget(); auto* brush = chrome_.SolidBrush();
        const auto tile = R(Panel::Tile(top));
        chrome_.Round(tile, Design::Radius::control, Design::Surface::raised);
        std::shared_ptr<const FaviconImage> image; if (s.icon && !e.site.empty()) image = s.icon(e.site);
        if (!chrome_.DrawSiteImage(image, tile)) chrome_.Icon(e.local ? L"file" : L"favicon", tile, Design::Size::icon, Design::Text::secondary);

        RowReveal still; const RowReveal* reveal = &still;
        if (s.reveals) { auto found = s.reveals->find(index); if (found != s.reveals->end()) reveal = &found->second; }
        // The title brightens with the row's reveal (pointer or keyboard), dims while pressed.
        const float shown = reveal->Layer(0, s.now).opacity, press = Interaction(s, RowHit(index)).press;
        const float titleOpacity = (Design::Text::muted + (.78f - Design::Text::muted) * shown) * (1 - .25f * press);
        chrome_.Text(e.title, R(Panel::Name(top)), Panel::line, titleOpacity, false, true, -.16f);
        chrome_.Text(Detail(e, s.nowMs, s.offsetMs), R(Panel::Detail(top)), Panel::detailText, Design::Text::muted, true, false, -.14f);

        // Reveal: the name mask, then "open in new tab", then "remove" (download rows).
        const float bandTop = top - Panel::rowGap / 2, bandBottom = top + Panel::tile + Panel::rowGap / 2;
        reveal_.Element(D2D1::RectF(Panel::MaskStart() - 8, bandTop, Panel::width, bandBottom), reveal->Layer(0, s.now), [&] {
            auto* t = chrome_.RenderTarget();
            if (fade_) {
                fade_->SetStartPoint({Panel::MaskStart(), 0}); fade_->SetEndPoint({Panel::MaskSolid(), 0});
                t->FillRectangle(D2D1::RectF(Panel::MaskStart(), bandTop, Panel::MaskSolid(), bandBottom), fade_);
            }
            chrome_.Color(Design::Surface::canvas);
            t->FillRectangle(D2D1::RectF(Panel::MaskSolid() - .5f, bandTop, Panel::width + Downloads::Reveal::maskTravel, bandBottom), brush);
        }, true);
        for (int slot = 0; slot < 2; ++slot) {
            const auto rect = R(Panel::ActionBox(top, slot));
            const int hit = ActionHit(index, slot); const auto v = Interaction(s, hit);
            reveal_.Element(D2D1::RectF(rect.left - 12, rect.top - 12, rect.right + 12, rect.bottom + 12), reveal->Layer(1 + slot, s.now), [&] {
                chrome_.DrawButton(ActionIcon(slot), rect, Design::ButtonVariant::Ghost, {v.hover, v.press, false, s.focus == hit});
            });
        }
        // Last, so the reveal mask never covers it.
        if (s.focus == RowHit(index)) chrome_.FocusRing(R(Layout::RowRing(top)), Layout::rowRingRadius);
        (void)target;
    }
public:
    explicit View(Chrome& chrome) : chrome_(chrome) {}
    ~View() { Release(); }
    void Release() { reveal_.Release(); Drop(fade_); Drop(stops_); }
    void Paint(const Scene& s) {
        auto* target = chrome_.RenderTarget(); auto* brush = chrome_.SolidBrush(); if (!target || !brush || !s.entries || !s.slots) return;
        Prepare();
        const float c = Design::Surface::canvas, width = Panel::width, height = s.height;
        target->Clear(D2D1::ColorF(c, c, c, 1));
        target->SetTransform(D2D1::Matrix3x2F::Translation(s.originX, 0));
        chrome_.keyboardModality = s.keyboard;

        chrome_.Text(TR(history_title), R(Panel::Title()), Panel::line, Design::Text::muted, false, true, -.16f);
        const auto cv = Interaction(s, CloseHit);
        chrome_.DrawButton(L"close", R(Panel::Close()), Design::ButtonVariant::Ghost, {cv.hover, cv.press, false, s.focus == CloseHit});
        Search(s);

        target->PushAxisAlignedClip(D2D1::RectF(0, Layout::viewTop, width, height), D2D1_ANTIALIAS_MODE_ALIASED);
        for (const auto& slot : *s.slots) {
            switch (slot.kind) {
            case SlotKind::Empty:
                chrome_.Text(EmptyText(s.query), {Panel::padding, slot.top, width - Panel::padding, slot.top + Panel::tile}, Panel::line, Design::Text::muted, false, false, -.16f);
                break;
            case SlotKind::Section:
                if (slot.top + DrawerLayout::sectionHeight < Layout::viewTop || slot.top > height) break;
                Section(static_cast<Period>(slot.index), slot.top, slot.open, s);
                break;
            case SlotKind::Row: {
                if (slot.index < 0 || slot.index >= static_cast<int>(s.entries->size()) || slot.opacity <= .002f) break;
                const float top = std::max(slot.clipTop, Layout::viewTop), bottom = std::min(slot.clipBottom, height);
                if (bottom <= top || slot.top + Panel::tile + Panel::rowGap < top || slot.top - Panel::rowGap > bottom) break;
                const bool clipped = slot.clipTop > -1e5f;
                const bool layered = slot.opacity < .998f;
                if (clipped) target->PushAxisAlignedClip(D2D1::RectF(0, top, width, bottom), D2D1_ANTIALIAS_MODE_ALIASED);
                if (layered) target->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), slot.opacity), nullptr);
                Row(slot.index, slot.top, s);
                if (layered) target->PopLayer();
                if (clipped) target->PopAxisAlignedClip();
                break;
            }
            }
        }
        // Scroll edge effects: content dissolves into the canvas where more lies beyond.
        if (fade_ && s.fadeTop > 0) {
            fade_->SetStartPoint({0, Layout::viewTop + DrawerLayout::topFade}); fade_->SetEndPoint({0, Layout::viewTop}); fade_->SetOpacity(Motion::Clamp(s.fadeTop));
            target->FillRectangle(D2D1::RectF(0, Layout::viewTop, width, Layout::viewTop + DrawerLayout::topFade), fade_);
        }
        if (fade_ && s.fadeBottom > 0) {
            fade_->SetStartPoint({0, height - DrawerLayout::bottomFade}); fade_->SetEndPoint({0, height}); fade_->SetOpacity(Motion::Clamp(s.fadeBottom));
            target->FillRectangle(D2D1::RectF(0, height - DrawerLayout::bottomFade, width, height), fade_);
        }
        if (fade_) fade_->SetOpacity(1);
        target->PopAxisAlignedClip();
        // Leading hairline toward the page: one physical pixel, a touch lighter than the canvas.
        if (s.edgeOpacity > 0) {
            const float px = target->GetSize().width / std::max(1.f, static_cast<float>(target->GetPixelSize().width));
            chrome_.Color(1, DownloadsUI::Popup::border * s.edgeOpacity); target->FillRectangle(D2D1::RectF(0, 0, px, height), brush);
        }
    }
};

class Drawer {
public:
    struct Hooks {
        std::function<void(Motion::Time)> schedule;                       // display-paced frames
        std::function<void(const std::wstring&, bool)> open;              // url, new tab
        std::function<void(const std::wstring&)> remove;                  // url
        std::function<void(bool)> closed;                                 // true when closed from the keyboard
        std::function<std::shared_ptr<const FaviconImage>(const std::wstring&)> icon; // site -> remembered favicon
        std::function<void(const std::wstring&)> announce;                // screen reader notification
    };
private:
    HWND hwnd_ = nullptr, owner_ = nullptr;
    Tooltip::Tools tip_;
    Chrome chrome_; View view_{chrome_};
    Motion::State motion_;
    Hooks hooks_;
    const AddressBar::History* history_ = nullptr;
    std::vector<Entry> entries_;
    std::array<std::vector<int>, PeriodCount> groups_;
    std::wstring query_;
    std::map<int, RowReveal> reveals_;
    Lane slide_, scroll_;
    std::array<Lane, PeriodCount> fold_;
    int hover_ = 0, pressed_ = 0, focus_ = 0, hoverRow_ = -1;
    bool keyboard_ = false, visible_ = false, closing_ = false, tracking_ = false, animatingPainted_ = false;
    int appliedVisible_ = -1, appliedEntered_ = -1, lead_ = 0; RECT appliedOwner_{}, appliedWindow_{};

    float Dpi() const { return static_cast<float>(GetDpiForWindow(hwnd_ ? hwnd_ : owner_)); }
    int Px(float dip) const { return static_cast<int>(std::lround(dip * Dpi() / 96.f)); }
    int OwnerPx(float dip) const { return static_cast<int>(std::lround(dip * GetDpiForWindow(owner_) / 96.f)); }
    float Height() const { RECT r{}; if (hwnd_) GetClientRect(hwnd_, &r); return r.bottom * 96.f / Dpi(); }
    RECT OwnerClient() const { RECT r{}; GetClientRect(owner_, &r); MapWindowPoints(owner_, nullptr, reinterpret_cast<POINT*>(&r), 2); return r; }
    bool Reduced() const { return motion_.ReducedMotion(); }

    std::array<float, PeriodCount> Folds(Motion::Time now, bool target = false) const {
        std::array<float, PeriodCount> open{}; for (int p = 0; p < PeriodCount; ++p) open[p] = target ? fold_[p].Target() : fold_[p].Value(now); return open;
    }
    std::vector<Slot> SlotsAt(Motion::Time now, bool target = false, float* bottom = nullptr) const {
        return Slots(entries_, groups_, Folds(now, target), target ? scroll_.Target() : scroll_.Value(now), bottom);
    }
    float MaxScroll(Motion::Time now, bool target) const {
        float bottom = 0; Slots(entries_, groups_, Folds(now, target), 0, &bottom);
        return std::max(0.f, bottom - Height());
    }
    void Rebuild() {
        if (history_) entries_ = Entries(*history_, query_); else entries_.clear();
        groups_ = Group(entries_, DownloadsUI::UnixNowMs(), DownloadsUI::LocalOffsetMs());
        reveals_.clear(); hoverRow_ = -1;
    }
    // ---- Input ---------------------------------------------------------------
    int Hit(float x, float y, int& row) const {
        row = -1; x -= LeadDip();   // window DIP -> content DIP while it slides
        auto inside = [&](Design::Box b) { return x >= b.left && x < b.right && y >= b.top && y < b.bottom; };
        if (inside(Panel::Close())) return CloseHit;
        if (!query_.empty() && inside(Layout::Clear())) return ClearHit;
        if (inside(Layout::Search())) return SearchHit;
        if (y < Layout::viewTop || y >= Height()) return 0;
        for (const auto& slot : SlotsAt(Motion::Now())) {
            if (slot.kind == SlotKind::Section) {
                auto h = DrawerLayout::Header(slot.top);
                if (y >= h.top && y < h.bottom) return SectionHit(static_cast<Period>(slot.index));
            } else if (slot.kind == SlotKind::Row && slot.opacity > .5f) {
                auto band = Panel::Hover(slot.top);
                const float top = std::max(band.top, slot.clipTop), bottom = std::min(band.bottom, slot.clipBottom);
                if (y < top || y >= bottom) continue;
                row = slot.index;
                for (int s = 0; s < 2; ++s) if (inside(Panel::ActionBox(slot.top, s))) return ActionHit(slot.index, s);
                return RowHit(slot.index);
            }
        }
        return 0;
    }
    // A row shows its actions while hovered or holding keyboard focus.
    void UpdateReveals(Motion::Time now) {
        const int focusRow = IsRowPart(focus_) ? HitIndex(focus_) : -1;
        if (hoverRow_ >= 0) reveals_.try_emplace(hoverRow_);
        if (focusRow >= 0) reveals_.try_emplace(focusRow);
        for (auto it = reveals_.begin(); it != reveals_.end();) {
            const bool shown = it->first == hoverRow_ || it->first == focusRow;
            it->second.Set(shown, now, Reduced());
            if (!shown && it->second.Idle(now)) it = reveals_.erase(it); else ++it;
        }
    }
    void Interact(int hover, int pressed) {
        hover_ = hover; pressed_ = pressed; auto now = Motion::Now();
        motion_.SetInteraction(hover_, pressed_ == hover_ ? pressed_ : 0, now);
        UpdateReveals(now); Invalidate(); if (hooks_.schedule) hooks_.schedule(now);
    }
    void Repoint() {
        if (!hwnd_ || !visible_) return;
        POINT p; GetCursorPos(&p); ScreenToClient(hwnd_, &p); RECT client; GetClientRect(hwnd_, &client);
        const float s = 96.f / Dpi(); int row = -1, hit = 0;
        if (PtInRect(&client, p)) hit = Hit(p.x * s, p.y * s, row);
        hoverRow_ = row; Interact(hit, pressed_);
    }
    // Tab / arrows: close, search (and its clear button), then sections and rows
    // as laid out. A row's two actions are reached with Right / Left from it.
    std::vector<int> FocusOrder() const {
        std::vector<int> order{CloseHit, SearchHit};
        if (!query_.empty()) order.push_back(ClearHit);
        for (const auto& slot : SlotsAt(Motion::Now(), true)) {
            if (slot.kind == SlotKind::Section) order.push_back(SectionHit(static_cast<Period>(slot.index)));
            else if (slot.kind == SlotKind::Row && slot.opacity > .5f) order.push_back(RowHit(slot.index));
        }
        return order;
    }
    void Announce(int hit) {
        if (!hooks_.announce || !keyboard_) return;
        if (hit == CloseHit) hooks_.announce(TR(history_announce_close));
        else if (hit == SearchHit) hooks_.announce(query_.empty() ? std::wstring(TR(history_announce_search)) : TRF(history_announce_search_query, query_));
        else if (hit == ClearHit) hooks_.announce(TR(history_announce_clear_search));
        else if (IsSection(hit)) hooks_.announce(fold_[static_cast<int>(HitPeriod(hit))].Target() > .5f ? TRF(common_section_expanded, Downloads::PeriodLabel(HitPeriod(hit))) : TRF(common_section_collapsed, Downloads::PeriodLabel(HitPeriod(hit))));
        else if (IsRowPart(hit) && HitIndex(hit) < static_cast<int>(entries_.size())) {
            const auto& e = entries_[HitIndex(hit)];
            if (HitPart(hit) == 0) hooks_.announce(e.title + L", " + Detail(e, DownloadsUI::UnixNowMs(), DownloadsUI::LocalOffsetMs()));
            else hooks_.announce(HitPart(hit) == 1 ? TR(history_open_new_tab) : TR(history_announce_delete));
        }
    }
    void SetFocusHit(int hit) { focus_ = hit; Reveal(hit); Interact(hover_, pressed_); Announce(hit); }
    void ScrollTo(float target, Motion::Time now, Motion::Milliseconds duration) {
        target = std::clamp(target, 0.f, MaxScroll(now, true));
        scroll_.Set(target, now, Motion::Milliseconds(0), Reduced() ? Motion::Milliseconds(0) : duration);
    }
    void Wheel(float notches) {
        auto now = Motion::Now(); ScrollTo(scroll_.Target() - notches * pitch * 1.5f, now, DrawerMotion::scroll);
        Repoint(); if (hooks_.schedule) hooks_.schedule(now);
    }
    // Keeps a focused section or row in view.
    void Reveal(int hit) {
        if (!IsSection(hit) && !IsRowPart(hit)) return;
        auto now = Motion::Now(); const float scroll = scroll_.Target();
        for (const auto& slot : SlotsAt(now, true)) {
            const bool match = (slot.kind == SlotKind::Section && SectionHit(static_cast<Period>(slot.index)) == hit) ||
                               (slot.kind == SlotKind::Row && IsRowPart(hit) && slot.index == HitIndex(hit));
            if (!match) continue;
            const float height = slot.kind == SlotKind::Row ? Panel::tile : DrawerLayout::sectionHeight;
            const float top = Layout::viewTop + DrawerLayout::topFade, bottom = Height() - pitch;
            if (slot.top < top) ScrollTo(scroll - (top - slot.top), now, DrawerMotion::scroll);
            else if (slot.top + height > bottom) ScrollTo(scroll + (slot.top + height - bottom), now, DrawerMotion::scroll);
            if (hooks_.schedule) hooks_.schedule(now);
            return;
        }
    }
    void Fold(Period period) {
        auto now = Motion::Now(); auto& lane = fold_[static_cast<int>(period)];
        lane.Set(lane.Target() > .5f ? 0.f : 1.f, now, Motion::Milliseconds(0), Reduced() ? Motion::Milliseconds(0) : DrawerMotion::fold, DrawerMotion::foldSpring);
        ScrollTo(scroll_.Target(), now, DrawerMotion::fold); // content got shorter: settle within bounds
        Repoint(); RefreshTips(); if (hooks_.schedule) hooks_.schedule(now);
        Announce(SectionHit(period));
    }
    void SetQuery(std::wstring query) {
        if (query == query_) return;
        query_ = std::move(query);
        Rebuild();
        for (auto& lane : fold_) lane.Snap(1); // every match visible
        scroll_.Snap(0);
        if (IsRowPart(focus_) || IsSection(focus_) || (focus_ == ClearHit && query_.empty())) focus_ = SearchHit;
        Repoint(); RefreshTips(); Invalidate();
        if (hooks_.announce) hooks_.announce(entries_.empty() ? EmptyText(query_) : TRP(history_pages_count, entries_.size()));
    }
    void Open(int index, bool newTab) {
        if (index < 0 || index >= static_cast<int>(entries_.size()) || !hooks_.open) return;
        const auto url = entries_[index].url; hooks_.open(url, newTab);
    }
    void Activate(int hit, bool keyboard, bool newTab = false) {
        if (hit == CloseHit) { Hide(keyboard, true); return; }
        if (hit == SearchHit) { focus_ = SearchHit; Invalidate(); return; }
        if (hit == ClearHit) { SetQuery(L""); focus_ = SearchHit; Invalidate(); return; }
        if (IsSection(hit)) { Fold(HitPeriod(hit)); return; }
        if (!IsRowPart(hit)) return;
        const int index = HitIndex(hit), part = HitPart(hit);
        if (part == 0) Open(index, newTab);
        else if (part == 1) Open(index, true);
        else if (index < static_cast<int>(entries_.size()) && hooks_.remove) {
            const auto url = entries_[index].url;
            hooks_.remove(url); // the host updates the history, then calls Changed()
            if (keyboard) { // keep the keyboard on the row that took its place
                const int next = std::min(index, static_cast<int>(entries_.size()) - 1);
                focus_ = next >= 0 ? RowHit(next) : SearchHit; Reveal(focus_); Interact(hover_, pressed_);
            }
            if (hooks_.announce) hooks_.announce(TR(history_announce_deleted));
        }
    }
    void RefreshTips() {
        if (!tip_) return;
        tip_.Clear();
        auto add = [&](UINT_PTR id, Design::Box b, const wchar_t* text) { tip_.Add(id, {Px(b.left), Px(b.top), Px(b.right), Px(b.bottom)}, text); };
        add(CloseHit, Panel::Close(), TR(common_close_tip));
        if (!query_.empty()) add(ClearHit, Layout::Clear(), TR(history_clear_search));
        const float height = Height();
        for (const auto& slot : SlotsAt(Motion::Now(), true)) {
            if (slot.kind == SlotKind::Section) {
                if (slot.top >= Layout::viewTop && slot.top + DrawerLayout::sectionHeight <= height)
                    add(static_cast<UINT_PTR>(SectionHit(static_cast<Period>(slot.index))), DrawerLayout::Caret(slot.top), slot.open > .5f ? TR(common_collapse) : TR(common_expand));
                continue;
            }
            if (slot.kind != SlotKind::Row || slot.opacity < .99f || slot.top < Layout::viewTop || slot.top + Panel::tile > height) continue;
            for (int s = 0; s < 2; ++s) add(static_cast<UINT_PTR>(ActionHit(slot.index, s)), Panel::ActionBox(slot.top, s), ActionTip(s));
        }
    }
    Scene MakeScene(Motion::Time now) const {
        Scene s; s.entries = &entries_; s.reveals = &reveals_; s.motion = &motion_; s.icon = hooks_.icon; s.query = query_;
        const bool focused = GetFocus() == hwnd_;
        s.focus = focused ? focus_ : 0; s.keyboard = keyboard_; s.caret = focused;
        s.now = now; s.nowMs = DownloadsUI::UnixNowMs(); s.offsetMs = DownloadsUI::LocalOffsetMs(); s.height = Height();
        const float scroll = scroll_.Value(now), max = MaxScroll(now, false);
        s.fadeTop = Motion::Clamp(scroll / DrawerLayout::topFade);
        s.fadeBottom = Motion::Clamp((max - scroll) / pitch);
        return s;
    }
    void PaintNow() {
        if (!chrome_.RenderTarget()) chrome_.Resize();
        auto* target = chrome_.RenderTarget(); if (!target) return;
        const auto now = Motion::Now();
        auto scene = MakeScene(now); const auto slots = SlotsAt(now); scene.slots = &slots; scene.originX = LeadDip();
        target->BeginDraw();
        view_.Paint(scene); chrome_.KeyOut(scene.originX); KeyCorners();
        if (target->EndDraw() == D2DERR_RECREATE_TARGET) { view_.Release(); chrome_.RecreateTarget(); }
    }
    void ClearPointer() { if (GetCapture() == hwnd_) ReleaseCapture(); reveals_.clear(); motion_.ClearInteractions(); hoverRow_ = -1; hover_ = pressed_ = 0; tracking_ = false; }
    // Window = the 268 DIP column docked to the owner's right edge under the toolbar,
    // where the download and settings drawers dock. The window never moves while
    // sliding (NativeUI::KeyedDrawer): a slide step is one present, the content drawn
    // `lead_` pixels right of its place and the strip it has not entered keyed out.
    // The region rounds the corner shared with the owner (empty while nothing has
    // entered). Returns true when it painted.
    bool ApplyGeometry(Motion::Time now, bool force = false) {
        if (!hwnd_) return false;
        const RECT owner = OwnerClient();
        const int column = OwnerPx(Layout::width), top = owner.top + OwnerPx(Downloads::DrawerLayout::liveTop), height = std::max(1, static_cast<int>(owner.bottom - OwnerPx(Downloads::DrawerLayout::liveBottom) - top));
        const int shown = static_cast<int>(std::lround(column * Motion::Clamp(slide_.Value(now))));
        const int inset = OwnerPx(DrawerLayout::liveRight); const RECT window{owner.right - inset - column, top, owner.right - inset, top + height};   // Mode Focus: the floating panel's right edge (0 otherwise)
        const float radius = DrawerLayout::liveRadius * GetDpiForWindow(owner_) / 96.f;   // Mode Focus: round top corners (src/drawer-corners.h)
        const bool restyled = !EqualRect(&window, &appliedWindow_) || radius != corners_.Radius();
        if (!force && !restyled && shown == appliedVisible_ && EqualRect(&owner, &appliedOwner_)) return false;
        const bool resized = !EqualRect(&owner, &appliedOwner_) || !EqualRect(&window, &appliedWindow_), slid = shown != appliedVisible_;
        appliedVisible_ = shown; appliedOwner_ = owner; lead_ = column - shown; corners_.SetRadius(radius);
        if (force || !EqualRect(&window, &appliedWindow_)) { SetWindowPos(hwnd_, nullptr, window.left, window.top, column, height, SWP_NOACTIVATE | SWP_NOZORDER); appliedWindow_ = window; }
        const int entered = shown > 0;
        if (force || resized || entered != appliedEntered_) { SetWindowRgn(hwnd_, NativeUI::DrawerRegion(entered, column, height, OwnerPx(20), IsZoomed(owner_) || inset > 0), FALSE); appliedEntered_ = entered; }   // Mode Focus: off the window's corner, square
        if (lead_ > 0) corners_.HideLeading();   // Mode Focus: it moves with the content: back at rest
        if (restyled && !force) InvalidateRect(hwnd_, nullptr, FALSE);   // the corners keyed out (or no longer) by the next paint
        if (resized) { scroll_.Snap(std::clamp(scroll_.Target(), 0.f, MaxScroll(now, true))); RefreshTips(); }
        if (force || !slid || shown == 0 || !IsWindowVisible(hwnd_)) { SyncCorners(); return false; }
        PaintNow(); ValidateRect(hwnd_, nullptr); SyncCorners();
        return true;
    }
    // Slide offset in DIP (content drawn right of its docked place while it enters or leaves).
    float LeadDip() const { return lead_ * 96.f / std::max(1.f, Dpi()); }
    // Mode Focus: the antialiased top corners as presented (src/drawer-corners.h): the trailing one whenever the column is
    // in, the leading one at rest only (it moves with the content while sliding). KeyCorners: the paint's part of them.
    void SyncCorners() { if (!visible_ || appliedVisible_ <= 0 || !IsWindowVisible(hwnd_)) { corners_.Hide(); return; } const float c = Design::Surface::canvas; corners_.Place(hwnd_, appliedWindow_, lead_, appliedWindow_.right - appliedWindow_.left, c + (1 - c) * DownloadsUI::Popup::border, c, lead_ == 0); }
    void KeyCorners() { if (corners_.Radius() <= 0) return; HRGN cut = corners_.CutRegion(lead_, appliedWindow_.right - appliedWindow_.left); chrome_.KeyOutRegion(cut); DeleteObject(cut); }
    void Finish() {
        closing_ = false; visible_ = false; appliedVisible_ = -1; appliedWindow_ = {};
        corners_.Hide(); ShowWindow(hwnd_, SW_HIDE);
    }
    DrawerCorners::Pair corners_;   // Mode Focus: the antialiased top corners
    void Key(WPARAM w) {
        const bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
        if (w == VK_ESCAPE) { if (!query_.empty()) { SetQuery(L""); focus_ = SearchHit; Invalidate(); } else Hide(true, true); return; }
        const auto order = FocusOrder();
        if (w == VK_TAB || w == VK_DOWN || w == VK_UP) {
            keyboard_ = true; const bool back = w == VK_UP || (w == VK_TAB && shift);
            int from = focus_; if (IsRowPart(from)) from = RowHit(HitIndex(from));
            auto it = std::find(order.begin(), order.end(), from);
            int index = it == order.end() ? (back ? static_cast<int>(order.size()) - 1 : 0) : static_cast<int>(it - order.begin()) + (back ? -1 : 1);
            index = (index + static_cast<int>(order.size())) % static_cast<int>(order.size());
            SetFocusHit(order[index]); return;
        }
        if (IsRowPart(focus_) && (w == VK_RIGHT || w == VK_LEFT)) {
            keyboard_ = true; const int part = std::clamp(HitPart(focus_) + (w == VK_RIGHT ? 1 : -1), 0, 2);
            SetFocusHit(RowBase + HitIndex(focus_) * 3 + part); return;
        }
        if (w == VK_DELETE && IsRowPart(focus_)) { keyboard_ = true; Activate(ActionHit(HitIndex(focus_), 1), true); return; }
        if (w == VK_RETURN) {
            keyboard_ = true;
            if (focus_ == SearchHit) { if (!entries_.empty()) Open(FirstShown(), shift || ctrl); return; }
            if (focus_) Activate(focus_, true, shift || ctrl);
            return;
        }
        if (w == VK_SPACE && focus_ && focus_ != SearchHit) { keyboard_ = true; Activate(focus_, true, ctrl); return; }
        if (w == VK_BACK && focus_ == SearchHit && !query_.empty()) {
            std::wstring q = query_;
            if (ctrl) { while (!q.empty() && q.back() == L' ') q.pop_back(); while (!q.empty() && q.back() != L' ') q.pop_back(); }
            else { q.pop_back(); if (!q.empty() && q.back() >= 0xD800 && q.back() <= 0xDBFF) q.pop_back(); }
            SetQuery(q); return;
        }
    }
    // The first entry in display order (top of the first non-empty period).
    int FirstShown() const { for (const auto& g : groups_) if (!g.empty()) return g.front(); return 0; }
    void Char(wchar_t c) {
        if (c < 0x20 || c == 0x7f) return; // control characters: Backspace, Enter, Escape, Ctrl+letter
        if (c == L' ' && (focus_ != SearchHit || query_.empty())) return; // Space activates the focused control; no leading blank
        if (query_.size() >= 256) return;
        if (focus_ != SearchHit) focus_ = SearchHit;
        SetQuery(query_ + c);
    }
    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<Drawer*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (msg == WM_NCCREATE) { self = static_cast<Drawer*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); self->hwnd_ = h; }
        { LRESULT border = 0; if (NativeChrome::ResizeBorder::Claim(h, msg, w, l, border)) return border; }   // flush with the window's right edge and bottom: its resize border wins there (src/resize-border.h)
        return self ? self->Handle(h, msg, w, l) : DefWindowProcW(h, msg, w, l);
    }
    LRESULT Handle(HWND h, UINT msg, WPARAM w, LPARAM l) {
        const float scale = 96.f / Dpi();
        switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); PaintNow(); EndPaint(h, &ps); return 0; }
        case WM_SIZE: chrome_.Resize(); Invalidate(); return 0;
        case WM_DPICHANGED: ApplyGeometry(Motion::Now(), true); return 0;
        case WM_MOUSEACTIVATE: return MA_ACTIVATE;
        case WM_ACTIVATE: if (LOWORD(w) == WA_INACTIVE) Hide(false, true); return 0;
        case WM_SETFOCUS: case WM_KILLFOCUS: Invalidate(); return 0;
        case WM_SETCURSOR:
            if (LOWORD(l) == HTCLIENT) { POINT p; GetCursorPos(&p); ScreenToClient(h, &p); int row; const int hit = Hit(p.x * scale, p.y * scale, row);
                SetCursor(LoadCursor(nullptr, hit == SearchHit ? IDC_IBEAM : IDC_ARROW)); return TRUE; }
            break;
        case WM_MOUSEMOVE: {
            if (!tracking_) { TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, h, 0}; TrackMouseEvent(&track); tracking_ = true; }
            int row = -1; const int hit = Hit(GET_X_LPARAM(l) * scale, GET_Y_LPARAM(l) * scale, row);
            if (row != hoverRow_ || hit != hover_) { hoverRow_ = row; Interact(hit, pressed_); }
            return 0;
        }
        case WM_MOUSELEAVE: tracking_ = false; hoverRow_ = -1; Interact(0, 0); return 0;
        case WM_LBUTTONDOWN: { int row; const int hit = Hit(GET_X_LPARAM(l) * scale, GET_Y_LPARAM(l) * scale, row); keyboard_ = false;
            if (hit == SearchHit) focus_ = SearchHit;
            if (hit) { SetCapture(h); Interact(hit, hit); } return 0; }
        case WM_LBUTTONUP: { int row; const int hit = Hit(GET_X_LPARAM(l) * scale, GET_Y_LPARAM(l) * scale, row); const int was = pressed_;
            if (GetCapture() == h) ReleaseCapture();
            hoverRow_ = row; Interact(hit, 0);
            if (was && was == hit) Activate(hit, false, (w & MK_CONTROL) != 0);
            return 0; }
        case WM_MBUTTONUP: { int row; const int hit = Hit(GET_X_LPARAM(l) * scale, GET_Y_LPARAM(l) * scale, row); if (IsRowPart(hit) && HitPart(hit) == 0) Open(HitIndex(hit), true); return 0; }
        case WM_CAPTURECHANGED: if (pressed_) Interact(hover_, 0); return 0;
        case WM_MOUSEWHEEL: Wheel(GET_WHEEL_DELTA_WPARAM(w) / 120.f); return 0;
        case WM_KEYDOWN: Key(w); return 0;
        case WM_CHAR: Char(static_cast<wchar_t>(w)); return 0;
        case WM_DESTROY: return 0;
        }
        return DefWindowProcW(h, msg, w, l);
    }
public:
    Drawer() { for (auto& lane : fold_) lane.Snap(1); }
    ~Drawer() { Dispose(); }
    // Recently closed by deactivation: the click that caused it must not reopen it.
    ULONGLONG hiddenAt = 0;
    bool Create(HINSTANCE instance, HWND owner, const std::wstring& root, const AddressBar::History* history, Hooks hooks) {
        owner_ = owner; history_ = history; hooks_ = std::move(hooks); motion_.EnablePolish(); chrome_.SetMotion(&motion_);
        chrome_.textCacheLimit = 1024; // titles + detail runs of every row at any history size
        WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = instance; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.lpszClassName = L"LisiereHistoryDrawer";
        RegisterClassW(&wc);
        CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, TR(history_title), WS_POPUP, 0, 0, Px(Layout::width), Px(400), owner, nullptr, instance, this);
        if (!hwnd_) return false;
        tip_.Create(instance, hwnd_, root);
        NativeUI::ConfigureShadowlessPopup(hwnd_); // DWMWCP_DONOTROUND, no shadow: the region shapes it
        NativeUI::KeyedDrawer(hwnd_);               // the slide's not-yet-entered strip is keyed out
        return SUCCEEDED(chrome_.Init(hwnd_, root));
    }
    // Release Direct2D/WIC before CoUninitialize; the owner destroys the window.
    void Dispose() { view_.Release(); chrome_.Dispose(); tip_.Dispose(); hwnd_ = nullptr; visible_ = false; }
    HWND Window() const { return hwnd_; }
    UINT RenderDpiForTest() const { return chrome_.dpi; }   // the DPI the drawer is drawn at (tests/ui/address_popup_dpi_test.cpp)
    bool Visible() const { return visible_; }
    bool Closing() const { return closing_; }
    bool Open() const { return visible_ && !closing_; }
    const std::wstring& Query() const { return query_; }
    const std::vector<Entry>& Shown() const { return entries_; }
    void Invalidate() { if (hwnd_ && visible_) InvalidateRect(hwnd_, nullptr, FALSE); }
    void SetReducedMotion(bool reduced) { motion_.SetReducedMotion(reduced); Invalidate(); }
    // Slides in from the right edge (re-opening mid-exit continues from where it is).
    // The search field takes the keyboard: typing filters at once.
    void Show(bool keyboard = false) {
        if (!hwnd_) return;
        motion_.SetReducedMotion(Motion::PreferredReducedMotion());
        auto now = Motion::Now();
        if (!visible_) {
            slide_.Snap(0); scroll_.Snap(0); query_.clear(); for (auto& lane : fold_) lane.Snap(1);
            ClearPointer(); appliedVisible_ = -1; appliedWindow_ = {}; appliedOwner_ = {};
        }
        Rebuild();
        focus_ = SearchHit; keyboard_ = keyboard;
        slide_.Set(1, now, Motion::Milliseconds(0), Reduced() ? Motion::Milliseconds(0) : DrawerMotion::enter, DrawerMotion::enterSpring);
        closing_ = false; visible_ = true;
        ApplyGeometry(now, true);
        // Painted while shown without activation, then activated: never a blank frame.
        SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        PaintNow(); ValidateRect(hwnd_, nullptr); SyncCorners();
        SetForegroundWindow(hwnd_); SetFocus(hwnd_);
        Repoint(); RefreshTips();
        if (hooks_.announce) hooks_.announce(entries_.empty() ? std::wstring(TR(history_announce_empty)) : TRP(history_announce_pages, entries_.size()));
        if (hooks_.schedule) hooks_.schedule(Motion::Now());
    }
    // Leaves along the path it came in on; `animate` false hides at once.
    void Hide(bool keyboard, bool animate) {
        if (!visible_ || closing_) { if (visible_ && !animate) Finish(); return; }
        hiddenAt = GetTickCount64(); ClearPointer();
        auto now = Motion::Now(); closing_ = true;
        slide_.Set(0, now, Motion::Milliseconds(0), animate && !Reduced() ? DrawerMotion::exit : Motion::Milliseconds(0), DrawerMotion::exitSpring);
        if (!slide_.Moving(now)) Finish();
        if (hooks_.closed) hooks_.closed(keyboard);
        if (visible_ && hooks_.schedule) hooks_.schedule(now);
    }
    void Toggle(bool keyboard) { if (Open()) Hide(keyboard, true); else Show(keyboard); }
    // Owner moved, resized or changed DPI.
    void Reposition() { if (visible_) ApplyGeometry(Motion::Now()); }   // Mode Focus: a new live top / right / radius counts as a move (ApplyGeometry)
    // The history changed (a visit, a title, a removal, cleared data).
    void Changed() {
        if (!hwnd_ || !visible_) return;
        auto now = Motion::Now();
        Rebuild();
        auto order = FocusOrder(); if (focus_ && std::find(order.begin(), order.end(), IsRowPart(focus_) ? RowHit(HitIndex(focus_)) : focus_) == order.end()) focus_ = SearchHit;
        scroll_.Snap(std::clamp(scroll_.Value(now), 0.f, MaxScroll(now, true)));
        RefreshTips(); Repoint(); Invalidate();
    }
    Motion::Wake NextWake(Motion::Time now) const {
        if (!visible_) return {};
        bool moving = slide_.Moving(now) || scroll_.Moving(now);
        for (auto& lane : fold_) moving = moving || lane.Moving(now);
        auto wake = Motion::Earlier(motion_.NextWake(now), moving ? Motion::Frame() : Motion::Wake{});
        for (auto& item : reveals_) wake = Motion::Earlier(wake, item.second.NextWake(now));
        return wake;
    }
    // Display-paced frame from the host clock; also paints the frame after motion ends.
    void Frame(Motion::Time now) {
        if (!visible_) return;
        const bool painted = ApplyGeometry(now);
        if (closing_ && !slide_.Moving(now)) { Finish(); return; }
        const bool moving = NextWake(now).delay.has_value();
        if (moving || animatingPainted_) Invalidate();
        if (painted) ValidateRect(hwnd_, nullptr);   // a slide step: already presented with its region
        if (!moving && animatingPainted_) RefreshTips();
        animatingPainted_ = moving;
        if (!moving) for (auto it = reveals_.begin(); it != reveals_.end();) { if (!it->second.Shown() && it->second.Idle(now)) it = reveals_.erase(it); else ++it; }
    }
    // Test hooks (smoke run): type into the search field / activate as the keyboard would.
    void TypeForTest(const std::wstring& text) { for (wchar_t c : text) Char(c); }
    void KeyForTest(WPARAM key) { Key(key); }
};
} // namespace HistoryUI
} // namespace NativeChrome
