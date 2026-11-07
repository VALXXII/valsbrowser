#pragma once
// Native "Download history" popover: an owned, DWM-rounded popup painted with
// Direct2D through its own Chrome instance (shared fonts, icons, helpers).
// Hovering a row reveals a name mask and two actions that slide in from the
// right while un-blurring (D2D Gaussian blur), staggered: see Downloads::RowReveal.
#include <windowsx.h>
#include <dwmapi.h>
#include <d2d1_1.h>
#include <shellapi.h>
#include <commoncontrols.h>
#include <commctrl.h>
#include <functional>
#include <map>
#include <vector>
#include "chrome.h"
#include "downloads.h"

namespace NativeChrome {
namespace DownloadsUI {
using namespace Downloads;
// Hit identifiers inside the popover.
constexpr int CloseHit = 1, ActionBase = 100000;
inline int ActionHit(int item, int slot) { return ActionBase + item * 2 + slot; }
inline bool IsAction(int hit) { return hit >= ActionBase; }
inline int HitItem(int hit) { return (hit - ActionBase) / 2; }
inline int HitSlot(int hit) { return (hit - ActionBase) % 2; }
inline std::int64_t UnixNowMs() {
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER v; v.LowPart = ft.dwLowDateTime; v.HighPart = ft.dwHighDateTime;
    return static_cast<std::int64_t>((v.QuadPart - 116444736000000000ULL) / 10000ULL);
}
// Everything a frame depends on; the offscreen fixture builds one directly.
struct Scene {
    const std::vector<Item>* items = nullptr;
    const std::map<int, RowReveal>* reveals = nullptr;
    const Motion::State* motion = nullptr;
    int focus = 0; bool keyboard = false;
    float scroll = 0, entrance = 1;
    std::int64_t nowMs = 0;
    Motion::Time now{};
};

class View {
    Chrome& chrome_;
    ID2D1DeviceContext* dc_ = nullptr; ID2D1Effect* blur_ = nullptr;
    ID2D1BitmapRenderTarget* layer_ = nullptr; D2D1_SIZE_F layerSize_{};
    ID2D1GradientStopCollection* stops_ = nullptr; ID2D1LinearGradientBrush* fade_ = nullptr;
    std::map<std::wstring, ID2D1Bitmap*> icons_;
    template<class T> static void Drop(T*& p) { if (p) { p->Release(); p = nullptr; } }
public:
    explicit View(Chrome& chrome) : chrome_(chrome) {}
    ~View() { Release(); }
    bool blurAvailable = true;
    void Release() {
        Drop(layer_); layerSize_ = {}; Drop(fade_); Drop(stops_); Drop(blur_); Drop(dc_);
        for (auto& icon : icons_) Drop(icon.second);
        icons_.clear();
    }
    // Device context + blur effect are optional: without them reveals still slide and fade.
    void Prepare() {
        auto* target = chrome_.RenderTarget(); if (!target) return;
        if (!dc_ && SUCCEEDED(target->QueryInterface(__uuidof(ID2D1DeviceContext), reinterpret_cast<void**>(&dc_))) && blurAvailable) {
            static const CLSID gaussian = {0x1feb6d69, 0x2fe6, 0x4ac9, {0x8c, 0x58, 0x1d, 0x7f, 0x93, 0xe7, 0xa6, 0xa5}};
            if (FAILED(dc_->CreateEffect(gaussian, &blur_))) blur_ = nullptr;
        }
        if (!fade_) {
            const float c = Design::Surface::canvas;
            D2D1_GRADIENT_STOP stops[] = {{0, D2D1::ColorF(c, c, c, 0)}, {1, D2D1::ColorF(c, c, c, 1)}};
            if (FAILED(target->CreateGradientStopCollection(stops, 2, &stops_)) ||
                FAILED(target->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({0, 0}, {1, 0}), stops_, &fade_))) { Drop(fade_); Drop(stops_); }
        }
    }
    // Shell icon for the file: the real file for executables/shortcuts once
    // complete (so an installer shows its own icon), otherwise its extension.
    ID2D1Bitmap* FileIcon(const Item& item) {
        auto* target = chrome_.RenderTarget(); auto* wic = chrome_.Imaging(); if (!target || !wic) return nullptr;
        std::wstring name = FileName(item.path), ext;
        const auto dot = name.find_last_of(L'.');
        if (dot != std::wstring::npos) ext = name.substr(dot);
        for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));
        const bool own = item.state == State::Completed && !item.missing && (ext == L".exe" || ext == L".lnk" || ext == L".ico" || ext == L".msi");
        const std::wstring generic = ext.empty() ? L".file" : ext, key = own ? item.path : generic;
        auto found = icons_.find(key); if (found != icons_.end()) return found->second;
        ID2D1Bitmap* bitmap = nullptr;
        SHFILEINFOW info{};
        if (SHGetFileInfoW(key.c_str(), FILE_ATTRIBUTE_NORMAL, &info, sizeof(info), SHGFI_SYSICONINDEX | (own ? 0 : SHGFI_USEFILEATTRIBUTES)) ||
            (own && SHGetFileInfoW(generic.c_str(), FILE_ATTRIBUTE_NORMAL, &info, sizeof(info), SHGFI_SYSICONINDEX | SHGFI_USEFILEATTRIBUTES))) {
            static const IID imageList = {0x46eb5926, 0x582e, 0x4017, {0x9f, 0xdf, 0xe8, 0x99, 0x8d, 0xaa, 0x09, 0x50}};
            const float px = 24.f * target->GetPixelSize().width / std::max(1.f, target->GetSize().width);
            IImageList* list = nullptr; HICON icon = nullptr;
            if (SUCCEEDED(SHGetImageList(px > 48 ? SHIL_JUMBO : SHIL_EXTRALARGE, imageList, reinterpret_cast<void**>(&list))) && list) {
                list->GetIcon(info.iIcon, ILD_TRANSPARENT, &icon); list->Release();
            }
            if (icon) {
                IWICBitmap* source = nullptr; IWICFormatConverter* converter = nullptr;
                HRESULT hr = wic->CreateBitmapFromHICON(icon, &source);
                if (SUCCEEDED(hr)) hr = wic->CreateFormatConverter(&converter);
                if (SUCCEEDED(hr)) hr = converter->Initialize(source, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom);
                if (SUCCEEDED(hr)) target->CreateBitmapFromWicBitmap(converter, nullptr, &bitmap);
                Drop(converter); Drop(source); DestroyIcon(icon);
            }
        }
        icons_[key] = bitmap;
        return bitmap;
    }
    void ForgetIcons() { for (auto& icon : icons_) Drop(icon.second); icons_.clear(); }

    // Draws `paint` (panel coordinates) shifted by visual.offset, faded and, when
    // the effect exists, Gaussian-blurred over `bounds` via an offscreen layer.
    template<class F> void Element(D2D1_RECT_F bounds, LayerVisual visual, F paint, bool hardEdges = false) {
        if (visual.opacity <= .002f) return;
        auto* target = chrome_.RenderTarget();
        D2D1_MATRIX_3X2_F base; target->GetTransform(&base);
        const bool blurred = dc_ && blur_ && visual.blur > .05f;
        if (!blurred) {
            target->SetTransform(D2D1::Matrix3x2F::Translation(visual.offset, 0) * base);
            const bool layered = dc_ && visual.opacity < .998f;
            if (layered) dc_->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), visual.opacity), nullptr);
            paint();
            if (layered) dc_->PopLayer();
            target->SetTransform(base);
            return;
        }
        const D2D1_SIZE_F size{bounds.right - bounds.left, bounds.bottom - bounds.top};
        if (!layer_ || layerSize_.width != size.width || layerSize_.height != size.height) {
            Drop(layer_);
            if (FAILED(target->CreateCompatibleRenderTarget(size, &layer_))) { layer_ = nullptr; blurAvailable = false; Drop(blur_); Element(bounds, visual, paint, hardEdges); return; }
            layerSize_ = size; layer_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        }
        layer_->BeginDraw();
        layer_->Clear(D2D1::ColorF(0, 0, 0, 0));
        layer_->SetTransform(D2D1::Matrix3x2F::Translation(-bounds.left, -bounds.top));
        auto* previous = chrome_.SwapTarget(layer_);
        paint();
        chrome_.SwapTarget(previous);
        layer_->SetTransform(D2D1::IdentityMatrix());
        if (FAILED(layer_->EndDraw())) return;
        ID2D1Bitmap* bitmap = nullptr; if (FAILED(layer_->GetBitmap(&bitmap)) || !bitmap) return;
        blur_->SetInput(0, bitmap);
        const float deviation = visual.blur; const UINT32 border = hardEdges ? 1u : 0u; // D2D1_BORDER_MODE_HARD / SOFT
        blur_->SetValue(0, D2D1_PROPERTY_TYPE_FLOAT, reinterpret_cast<const BYTE*>(&deviation), sizeof(deviation));
        blur_->SetValue(2, D2D1_PROPERTY_TYPE_ENUM, reinterpret_cast<const BYTE*>(&border), sizeof(border));
        ID2D1Image* output = nullptr; blur_->GetOutput(&output);
        if (output) {
            if (visual.opacity < .998f) dc_->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), visual.opacity), nullptr);
            const D2D1_POINT_2F at{bounds.left + visual.offset, bounds.top};
            dc_->DrawImage(output, &at, nullptr, D2D1_INTERPOLATION_MODE_LINEAR, D2D1_COMPOSITE_MODE_SOURCE_OVER);
            if (visual.opacity < .998f) dc_->PopLayer();
            output->Release();
        }
        blur_->SetInput(0, nullptr); bitmap->Release();
        // The layer is reused by the next element: execute this draw first.
        target->Flush();
    }

    // Icon + text runs on the detail line; returns the x after the run.
    float Run(const wchar_t* icon, const std::wstring& text, float x, float top, float opacity) {
        if (icon) { chrome_.Icon(icon, {x, top, x + Panel::detailIcon, top + Panel::line}, Panel::detailIcon); x += Panel::detailIcon + Panel::iconGap; }
        if (!text.empty()) {
            const float w = chrome_.TextWidth(text, Panel::detailText, false, true, -.14f);
            chrome_.Text(text, {x, top, std::min(x + w + 2, Panel::width - Panel::padding), top + Panel::line}, Panel::detailText, opacity, true, false, -.14f);
            x += w;
        }
        return x;
    }
    void Detail(const Item& item, float top, std::int64_t nowMs) {
        const float y = top + Panel::line, muted = Design::Text::muted;
        float x = Panel::textLeft;
        switch (item.state) {
        case State::InProgress: {
            x = Run(L"arrow-down", Bytes(item.received, item.total), x, y, muted);
            if (item.endTime > 0) {
                const auto left = std::max<std::int64_t>(0, (item.endTime - nowMs + 999) / 1000);
                Run(L"timer", Remaining(left), x + Panel::groupGap, y, muted);
            }
            break;
        }
        case State::Paused: x = Run(L"detail-pause", Bytes(item.received, item.total), x, y, muted); Run(nullptr, L"Paused", x + Panel::groupGap, y, muted); break;
        case State::Completed: Run(item.missing ? L"warning" : L"check-circle", item.missing ? std::wstring(L"File deleted") : FolderLabel(item.path), x, y, muted); break;
        case State::Failed: Run(L"warning", FailureText(item.reason), x, y, muted); break;
        case State::Cancelled: Run(L"detail-close", L"Canceled", x, y, muted); break;
        }
    }
    void ActionButton(Action action, D2D1_RECT_F box, int hit, const Scene& scene) {
        const auto* icon = IconFor(action); if (!icon) return;
        auto v = scene.motion ? scene.motion->Visual(hit, scene.now) : Motion::InteractionVisual{};
        chrome_.DrawButton(icon, box, Design::ButtonVariant::Ghost, {v.hover, v.press, false, scene.focus == hit});
    }

    void Paint(const Scene& scene, float height) {
        auto* target = chrome_.RenderTarget(); auto* brush = chrome_.SolidBrush(); if (!target || !brush) return;
        Prepare();
        const float c = Design::Surface::canvas;
        target->Clear(D2D1::ColorF(c, c, c, 1));
        chrome_.keyboardModality = scene.keyboard;
        const float e = Motion::Clamp(scene.entrance);
        const bool entering = dc_ && e < .999f;
        target->SetTransform(D2D1::Matrix3x2F::Translation(0, -(1 - e) * Entrance::travel));
        if (entering) dc_->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), e), nullptr);

        // Header
        const auto title = Panel::Title();
        chrome_.Text(L"Download history", {title.left, title.top, title.right, title.bottom}, Panel::line, Design::Text::muted, false, true, -.16f);
        const auto close = Panel::Close();
        auto cv = scene.motion ? scene.motion->Visual(CloseHit, scene.now) : Motion::InteractionVisual{};
        chrome_.DrawButton(L"close", {close.left, close.top, close.right, close.bottom}, Design::ButtonVariant::Ghost, {cv.hover, cv.press, false, scene.focus == CloseHit});

        const auto& items = *scene.items;
        const float viewBottom = height - Panel::padding / 2;
        target->PushAxisAlignedClip(D2D1::RectF(0, Panel::ViewTop(), Panel::width, viewBottom), D2D1_ANTIALIAS_MODE_ALIASED);
        if (items.empty()) {
            const float top = Panel::RowTop(0);
            chrome_.Text(L"No downloads yet", {Panel::padding, top, Panel::width - Panel::padding, top + Panel::tile}, Panel::line, Design::Text::muted, false, false, -.16f);
        }
        for (size_t i = 0; i < items.size(); ++i) {
            const auto& item = items[i];
            const float top = Panel::RowTop(static_cast<int>(i), scene.scroll);
            if (top + Panel::tile < Panel::ViewTop() - Panel::rowGap || top > viewBottom + Panel::rowGap) continue;
            const auto tile = Panel::Tile(top);
            chrome_.Round({tile.left, tile.top, tile.right, tile.bottom}, Design::Radius::control, Design::Surface::raised);
            if (auto* icon = FileIcon(item)) {
                const float s = 24, x = (tile.left + tile.right - s) / 2, y = (tile.top + tile.bottom - s) / 2;
                const float fade = item.state == State::Completed && !item.missing ? 1.f : .5f;
                if (dc_) dc_->DrawBitmap(icon, D2D1::RectF(x, y, x + s, y + s), fade, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, nullptr, nullptr);
                else target->DrawBitmap(icon, D2D1::RectF(x, y, x + s, y + s), fade);
            }
            const auto name = Panel::Name(top);
            chrome_.Text(FileName(item.path), {name.left, name.top, name.right, name.bottom}, Panel::line, Design::Text::muted, false, true, -.16f);
            Detail(item, top, scene.nowMs);

            // Reveal: mask, then left action, then right action.
            RowReveal still; const RowReveal* reveal = &still;
            if (scene.reveals) { auto found = scene.reveals->find(item.id); if (found != scene.reveals->end()) reveal = &found->second; }
            const auto actions = ActionsFor(item);
            const float bandTop = top - Panel::rowGap / 2, bandBottom = top + Panel::tile + Panel::rowGap / 2;
            Element(D2D1::RectF(Panel::MaskStart() - 8, bandTop, Panel::width, bandBottom), reveal->Layer(0, scene.now), [&] {
                auto* t = chrome_.RenderTarget();
                if (fade_) {
                    fade_->SetStartPoint({Panel::MaskStart(), 0}); fade_->SetEndPoint({Panel::MaskSolid(), 0});
                    t->FillRectangle(D2D1::RectF(Panel::MaskStart(), bandTop, Panel::MaskSolid(), bandBottom), fade_);
                }
                chrome_.Color(Design::Surface::canvas);
                t->FillRectangle(D2D1::RectF(Panel::MaskSolid() - .5f, bandTop, Panel::width + Reveal::maskTravel, bandBottom), brush);
            }, true);
            for (int slot = 0; slot < 2; ++slot) {
                const Action action = slot == 0 ? actions.first : actions.second;
                if (action == Action::None) continue;
                const auto box = Panel::ActionBox(top, slot);
                const D2D1_RECT_F rect{box.left, box.top, box.right, box.bottom};
                const int hit = ActionHit(item.id, slot);
                Element(D2D1::RectF(rect.left - 12, rect.top - 12, rect.right + 12, rect.bottom + 12), reveal->Layer(1 + slot, scene.now), [&] { ActionButton(action, rect, hit, scene); });
            }
        }
        target->PopAxisAlignedClip();
        if (entering) dc_->PopLayer();
        target->SetTransform(D2D1::IdentityMatrix());
    }
};

class Popover {
public:
    struct Hooks {
        std::function<void(Motion::Time)> schedule;    // request display-paced frames
        std::function<void(int, Action)> action;       // item id, action
        std::function<void(bool)> closed;              // true when closed from the keyboard
    };
private:
    HWND hwnd_ = nullptr, owner_ = nullptr, tip_ = nullptr;
    std::vector<UINT_PTR> tips_;
    Chrome chrome_; View view_{chrome_};
    Motion::State motion_;
    std::map<int, RowReveal> reveals_;
    Motion::Tween entrance_;
    Hooks hooks_;
    std::vector<Item>* items_ = nullptr;
    int hoverItem_ = -1, hover_ = 0, pressed_ = 0, focus_ = 0, focusItem_ = -1;
    bool keyboard_ = false, visible_ = false, animatingPainted_ = false, tracking_ = false;
    float scroll_ = 0, anchorRight_ = 0, anchorTop_ = 0;
    static constexpr UINT_PTR TickTimer = 1;
    float Dpi() const { return static_cast<float>(GetDpiForWindow(hwnd_ ? hwnd_ : owner_)); }
    int Px(float dip) const { return static_cast<int>(std::lround(dip * Dpi() / 96.f)); }
    float Height() const { return Panel::Height(items_ ? static_cast<int>(items_->size()) : 0); }
    float MaxScroll() const { const int n = items_ ? static_cast<int>(items_->size()) : 0; return std::max(0.f, (n - Panel::maxVisibleRows) * Panel::pitch); }
    int RowAt(float y) const {
        if (!items_ || y < Panel::ViewTop() || y >= Height() - Panel::padding / 2) return -1;
        for (size_t i = 0; i < items_->size(); ++i) { auto band = Panel::Hover(Panel::RowTop(static_cast<int>(i), scroll_)); if (y >= band.top && y < band.bottom) return static_cast<int>(i); }
        return -1;
    }
    int Hit(float x, float y, int& row) const {
        row = RowAt(y);
        auto close = Panel::Close();
        if (x >= close.left && x < close.right && y >= close.top && y < close.bottom) return CloseHit;
        if (row < 0) return 0;
        const auto& item = (*items_)[row]; const auto actions = ActionsFor(item);
        for (int slot = 0; slot < 2; ++slot) {
            if ((slot == 0 ? actions.first : actions.second) == Action::None) continue;
            auto box = Panel::ActionBox(Panel::RowTop(row, scroll_), slot);
            if (x >= box.left && x < box.right && y >= box.top && y < box.bottom) return ActionHit(item.id, slot);
        }
        return 0;
    }
    int IndexOf(int id) const { if (!items_) return -1; for (size_t i = 0; i < items_->size(); ++i) if ((*items_)[i].id == id) return static_cast<int>(i); return -1; }
    // A row shows its actions while hovered or holding keyboard focus.
    void UpdateReveals(Motion::Time now) {
        const bool reduced = motion_.ReducedMotion();
        const int hoverId = hoverItem_ >= 0 && items_ && hoverItem_ < static_cast<int>(items_->size()) ? (*items_)[hoverItem_].id : -1;
        const int focusId = IsAction(focus_) ? HitItem(focus_) : -1;
        if (hoverId >= 0) reveals_.try_emplace(hoverId);
        if (focusId >= 0) reveals_.try_emplace(focusId);
        for (auto it = reveals_.begin(); it != reveals_.end();) {
            const bool shown = it->first == hoverId || it->first == focusId;
            it->second.Set(shown, now, reduced);
            if (!shown && it->second.Idle(now)) it = reveals_.erase(it); else ++it;
        }
    }
    void Interact(int hover, int pressed) {
        hover_ = hover; pressed_ = pressed; auto now = Motion::Now();
        motion_.SetInteraction(hover_, pressed_ == hover_ ? pressed_ : 0, now);
        UpdateReveals(now); Invalidate(); if (hooks_.schedule) hooks_.schedule(now);
    }
    std::vector<int> FocusOrder() const {
        std::vector<int> order{CloseHit};
        if (!items_) return order;
        for (auto& item : *items_) { auto a = ActionsFor(item); if (a.first != Action::None) order.push_back(ActionHit(item.id, 0)); if (a.second != Action::None) order.push_back(ActionHit(item.id, 1)); }
        return order;
    }
    void EnsureVisible(int hit) {
        if (!IsAction(hit)) return;
        const int index = IndexOf(HitItem(hit));
        if (index < 0) return;
        const float top = Panel::RowTop(index), viewTop = Panel::firstRow, viewBottom = Height() - Panel::padding;
        if (top - scroll_ < viewTop) scroll_ = top - viewTop;
        if (top + Panel::tile - scroll_ > viewBottom) scroll_ = top + Panel::tile - viewBottom;
        scroll_ = std::clamp(scroll_, 0.f, MaxScroll());
        RefreshTips();
    }
    void Activate(int hit, bool keyboard) {
        if (hit == CloseHit) { Hide(keyboard); return; }
        if (!IsAction(hit) || !hooks_.action) return;
        const int index = IndexOf(HitItem(hit)); if (index < 0) return;
        const auto actions = ActionsFor((*items_)[index]);
        const Action action = HitSlot(hit) == 0 ? actions.first : actions.second;
        if (action != Action::None) hooks_.action(HitItem(hit), action);
    }
    // Icon-only buttons carry tooltips; tools follow the visible rows.
    void RefreshTips() {
        if (!tip_) return;
        for (auto id : tips_) { TOOLINFOW t{}; t.cbSize = sizeof(t); t.hwnd = hwnd_; t.uId = id; SendMessageW(tip_, TTM_DELTOOLW, 0, reinterpret_cast<LPARAM>(&t)); }
        tips_.clear();
        auto add = [&](UINT_PTR id, Design::Box b, const wchar_t* text) {
            TOOLINFOW t{}; t.cbSize = sizeof(t); t.uFlags = TTF_SUBCLASS; t.hwnd = hwnd_; t.uId = id;
            t.rect = {Px(b.left), Px(b.top), Px(b.right), Px(b.bottom)}; t.lpszText = const_cast<wchar_t*>(text);
            SendMessageW(tip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&t)); tips_.push_back(id);
        };
        add(CloseHit, Panel::Close(), L"Close (Esc)");
        if (!items_) return;
        const float bottom = Height() - Panel::padding / 2;
        for (size_t i = 0; i < items_->size(); ++i) {
            const float top = Panel::RowTop(static_cast<int>(i), scroll_);
            if (top < Panel::ViewTop() || top + Panel::tile > bottom) continue;
            const auto actions = ActionsFor((*items_)[i]);
            for (int slot = 0; slot < 2; ++slot) {
                const Action action = slot == 0 ? actions.first : actions.second;
                if (action != Action::None) add(static_cast<UINT_PTR>(ActionHit((*items_)[i].id, slot)), Panel::ActionBox(top, slot), TipFor(action));
            }
        }
    }
    void Place() {
        if (!hwnd_) return;
        const int w = Px(Panel::width), h = Px(Height());
        SetWindowPos(hwnd_, HWND_TOP, static_cast<int>(std::lround(anchorRight_)) - w, static_cast<int>(std::lround(anchorTop_)), w, h, SWP_NOACTIVATE);
        RefreshTips();
    }
    void Ticker() {
        bool active = false;
        if (visible_ && items_) for (auto& item : *items_) if (item.state == State::InProgress && item.endTime > 0) active = true;
        if (active) SetTimer(hwnd_, TickTimer, 1000, nullptr); else KillTimer(hwnd_, TickTimer);
    }
    void PaintNow() {
        if (!chrome_.RenderTarget()) chrome_.Resize();
        auto* target = chrome_.RenderTarget(); if (!target || !items_) return;
        Scene scene; scene.items = items_; scene.reveals = &reveals_; scene.motion = &motion_;
        scene.focus = GetFocus() == hwnd_ ? focus_ : 0; scene.keyboard = keyboard_; scene.scroll = scroll_;
        scene.now = Motion::Now(); scene.nowMs = UnixNowMs(); scene.entrance = entrance_.Value(scene.now);
        target->BeginDraw();
        view_.Paint(scene, Height());
        if (target->EndDraw() == D2DERR_RECREATE_TARGET) { view_.Release(); chrome_.RecreateTarget(); }
    }
    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<Popover*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (msg == WM_NCCREATE) { self = static_cast<Popover*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); self->hwnd_ = h; }
        return self ? self->Handle(h, msg, w, l) : DefWindowProcW(h, msg, w, l);
    }
    LRESULT Handle(HWND h, UINT msg, WPARAM w, LPARAM l) {
        const float scale = 96.f / Dpi();
        switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); PaintNow(); EndPaint(h, &ps); return 0; }
        case WM_SIZE: chrome_.Resize(); view_.ForgetIcons(); return 0;
        case WM_DPICHANGED: Place(); return 0;
        case WM_MOUSEACTIVATE: return MA_ACTIVATE;
        case WM_ACTIVATE: if (LOWORD(w) == WA_INACTIVE) Hide(false); return 0;
        case WM_MOUSEMOVE: {
            if (!tracking_) { TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, h, 0}; TrackMouseEvent(&track); tracking_ = true; }
            int row = -1; const int hit = Hit(GET_X_LPARAM(l) * scale, GET_Y_LPARAM(l) * scale, row);
            if (row != hoverItem_ || hit != hover_) { hoverItem_ = row; Interact(hit, pressed_); }
            return 0;
        }
        case WM_MOUSELEAVE: tracking_ = false; hoverItem_ = -1; Interact(0, 0); return 0;
        case WM_LBUTTONDOWN: { int row; const int hit = Hit(GET_X_LPARAM(l) * scale, GET_Y_LPARAM(l) * scale, row); keyboard_ = false; if (hit) { SetCapture(h); Interact(hit, hit); } return 0; }
        case WM_LBUTTONUP: { int row; const int hit = Hit(GET_X_LPARAM(l) * scale, GET_Y_LPARAM(l) * scale, row); const int was = pressed_; if (GetCapture() == h) ReleaseCapture(); hoverItem_ = row; Interact(hit, 0); if (was && was == hit) Activate(hit, false); return 0; }
        case WM_CAPTURECHANGED: if (pressed_) Interact(hover_, 0); return 0;
        case WM_MOUSEWHEEL: {
            const float before = scroll_; scroll_ = std::clamp(scroll_ - GET_WHEEL_DELTA_WPARAM(w) / 120.f * Panel::pitch, 0.f, MaxScroll());
            if (scroll_ != before) { RefreshTips(); POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)}; ScreenToClient(h, &p); int row; const int hit = Hit(p.x * scale, p.y * scale, row); hoverItem_ = row; Interact(hit, pressed_); }
            return 0;
        }
        case WM_KEYDOWN: {
            if (w == VK_ESCAPE) { Hide(true); return 0; }
            auto order = FocusOrder();
            if (w == VK_TAB || w == VK_DOWN || w == VK_UP) {
                keyboard_ = true; const bool back = w == VK_UP || (w == VK_TAB && GetKeyState(VK_SHIFT) < 0);
                auto it = std::find(order.begin(), order.end(), focus_);
                int index = it == order.end() ? (back ? static_cast<int>(order.size()) - 1 : 0) : static_cast<int>(it - order.begin()) + (back ? -1 : 1);
                index = (index + static_cast<int>(order.size())) % static_cast<int>(order.size());
                focus_ = order[index]; EnsureVisible(focus_); Interact(hover_, pressed_); return 0;
            }
            if ((w == VK_RETURN || w == VK_SPACE) && focus_) { keyboard_ = true; Activate(focus_, true); return 0; }
            return 0;
        }
        case WM_TIMER: if (w == TickTimer) { Invalidate(); Ticker(); } return 0;
        case WM_DESTROY: KillTimer(h, TickTimer); return 0;
        }
        return DefWindowProcW(h, msg, w, l);
    }
public:
    // Recently closed by deactivation: the click that caused it must not reopen it.
    ULONGLONG hiddenAt = 0;
    ~Popover() { Dispose(); }
    // Release Direct2D/WIC before CoUninitialize; the owner destroys the window.
    void Dispose() { view_.Release(); chrome_.Dispose(); hwnd_ = tip_ = nullptr; tips_.clear(); visible_ = false; }
    bool Create(HINSTANCE instance, HWND owner, const std::wstring& root, std::vector<Item>* items, Hooks hooks) {
        owner_ = owner; items_ = items; hooks_ = std::move(hooks); motion_.EnablePolish(); chrome_.SetMotion(&motion_);
        WNDCLASSW wc{}; wc.style = CS_DROPSHADOW; wc.lpfnWndProc = Proc; wc.hInstance = instance; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.lpszClassName = L"LisiereDownloads";
        RegisterClassW(&wc);
        CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"Download history", WS_POPUP, 0, 0, Px(Panel::width), Px(Height()), owner, nullptr, instance, this);
        if (!hwnd_) return false;
        tip_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0, 0, 0, hwnd_, nullptr, instance, nullptr);
        // Windows 11: rounded, antialiased corners and a hairline border from DWM.
        const int round = 2; // DWMWCP_ROUND
        const COLORREF border = Design::NativeGray(Design::Surface::canvas + .04f);
        if (FAILED(DwmSetWindowAttribute(hwnd_, 33, &round, sizeof(round))))
            SetWindowRgn(hwnd_, CreateRoundRectRgn(0, 0, Px(Panel::width) + 1, Px(Height()) + 1, Px(Panel::radius * 2), Px(Panel::radius * 2)), TRUE);
        DwmSetWindowAttribute(hwnd_, 34, &border, sizeof(border));
        return SUCCEEDED(chrome_.Init(hwnd_, root));
    }
    HWND Window() const { return hwnd_; }
    bool Visible() const { return visible_; }
    // anchor: toolbar button rectangle in screen pixels; the popover's right edge aligns with it.
    void Show(RECT anchor) {
        if (!hwnd_) return;
        motion_.SetReducedMotion(Motion::SystemReducedMotion());
        anchorRight_ = static_cast<float>(anchor.right); anchorTop_ = anchor.bottom + Px(Panel::anchorGap);
        scroll_ = 0; hoverItem_ = -1; hover_ = pressed_ = 0; focus_ = 0; keyboard_ = false;
        motion_.ClearInteractions(); reveals_.clear();
        auto now = Motion::Now();
        entrance_.Snap(0); entrance_.Set(1, now, Entrance::duration, motion_.ReducedMotion(), Motion::Curve::Icon);
        Place(); visible_ = true;
        ShowWindow(hwnd_, SW_SHOW); SetForegroundWindow(hwnd_); SetFocus(hwnd_);
        // Pointer already over the panel (opened under the cursor): reveal immediately.
        POINT p; GetCursorPos(&p); ScreenToClient(hwnd_, &p); const float s = 96.f / Dpi(); int row; const int hit = Hit(p.x * s, p.y * s, row);
        RECT client; GetClientRect(hwnd_, &client);
        if (PtInRect(&client, p)) { hoverItem_ = row; hover_ = hit; }
        Interact(hover_, 0); Ticker();
    }
    void Hide(bool keyboard) {
        if (!visible_) return;
        visible_ = false; hiddenAt = GetTickCount64();
        KillTimer(hwnd_, TickTimer); if (GetCapture() == hwnd_) ReleaseCapture();
        reveals_.clear(); motion_.ClearInteractions(); hoverItem_ = -1; hover_ = pressed_ = 0; tracking_ = false;
        ShowWindow(hwnd_, SW_HIDE);
        if (hooks_.closed) hooks_.closed(keyboard);
    }
    // The model changed (progress, state, list): resize to fit and repaint.
    void Changed() {
        if (!hwnd_ || !visible_) return;
        scroll_ = std::clamp(scroll_, 0.f, MaxScroll());
        if (hoverItem_ >= static_cast<int>(items_->size())) hoverItem_ = -1;
        auto order = FocusOrder(); if (focus_ && std::find(order.begin(), order.end(), focus_) == order.end()) focus_ = 0;
        RECT r; GetWindowRect(hwnd_, &r);
        if (r.bottom - r.top != Px(Height())) Place(); else RefreshTips();
        // The row under the pointer may have changed (removal shifts rows up).
        POINT p; GetCursorPos(&p); ScreenToClient(hwnd_, &p); const float s = 96.f / Dpi(); int row = -1; int hit = 0;
        RECT client; GetClientRect(hwnd_, &client); if (PtInRect(&client, p)) hit = Hit(p.x * s, p.y * s, row);
        hoverItem_ = row; Interact(hit, pressed_); Ticker();
    }
    void Invalidate() { if (hwnd_ && visible_) InvalidateRect(hwnd_, nullptr, FALSE); }
    Motion::Wake NextWake(Motion::Time now) const {
        if (!visible_) return {};
        auto wake = Motion::Earlier(motion_.NextWake(now), entrance_.Moving(now) ? Motion::Frame() : Motion::Wake{});
        for (auto& item : reveals_) wake = Motion::Earlier(wake, item.second.NextWake(now));
        return wake;
    }
    // Display-paced frame from the host clock; also paints the frame after motion ends.
    void Frame(Motion::Time now) {
        const bool moving = NextWake(now).delay.has_value();
        if (moving || animatingPainted_) Invalidate();
        animatingPainted_ = moving;
        if (!moving) { for (auto it = reveals_.begin(); it != reveals_.end();) { if (!it->second.Shown() && it->second.Idle(now)) it = reveals_.erase(it); else ++it; } }
    }
};
} // namespace DownloadsUI
} // namespace NativeChrome
