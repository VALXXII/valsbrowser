#pragma once
// Native "Download history" surfaces, painted with Direct2D through their own
// Chrome instance (shared fonts, icons, helpers):
//  - Popover: owned, DWM-rounded popup under the toolbar button; the four latest
//    downloads and a "View all" link.
//  - Drawer: docked to the window's right edge under the toolbar; every download
//    grouped by period (Today ... Last year), each section folding on its caret.
// Hovering a row reveals a name mask and two actions that slide in from the
// right while un-blurring (D2D Gaussian blur), staggered: see Downloads::RowReveal.
#include <windowsx.h>
#include <dwmapi.h>
#include <d2d1_1.h>
#include <shellapi.h>
#include <commoncontrols.h>
#include <commctrl.h>
#include <array>
#include <functional>
#include <map>
#include <vector>
#include "chrome.h"
#include "downloads.h"

namespace NativeChrome {
namespace DownloadsUI {
using namespace Downloads;
// Hit identifiers inside a surface.
constexpr int CloseHit = 1, ViewAllHit = 2, SectionBase = 10, ActionBase = 100000;
inline int SectionHit(Period period) { return SectionBase + static_cast<int>(period); }
inline bool IsSection(int hit) { return hit >= SectionBase && hit < SectionBase + PeriodCount; }
inline Period HitPeriod(int hit) { return static_cast<Period>(hit - SectionBase); }
inline int ActionHit(int item, int slot) { return ActionBase + item * 2 + slot; }
inline bool IsAction(int hit) { return hit >= ActionBase; }
inline int HitItem(int hit) { return (hit - ActionBase) / 2; }
inline int HitSlot(int hit) { return (hit - ActionBase) % 2; }
inline std::int64_t FileTimeMs(FILETIME ft) { ULARGE_INTEGER v; v.LowPart = ft.dwLowDateTime; v.HighPart = ft.dwHighDateTime; return static_cast<std::int64_t>(v.QuadPart / 10000ULL); }
inline std::int64_t UnixNowMs() { FILETIME ft; GetSystemTimeAsFileTime(&ft); return FileTimeMs(ft) - 11644473600000LL; }
// Local time minus UTC (ms), for calendar-day periods.
inline std::int64_t LocalOffsetMs() { FILETIME utc, local; GetSystemTimeAsFileTime(&utc); FileTimeToLocalFileTime(&utc, &local); return FileTimeMs(local) - FileTimeMs(utc); }

namespace Popup { inline constexpr float border = .02f; } // hairline: white 2% over the canvas (Paper)
// One laid-out element of a list. Rows of a folding section are clipped to the
// band their section still uncovers and fade with it.
enum class SlotKind { Row, Section, ViewAll, Empty };
struct Slot {
    SlotKind kind = SlotKind::Row;
    int index = -1;                 // Row: item index; Section: period
    float top = 0, opacity = 1, open = 1;
    float clipTop = -1e6f, clipBottom = 1e6f;
};
// Everything a frame depends on; the offscreen fixture builds one directly.
struct Scene {
    const std::vector<Item>* items = nullptr;
    const std::map<int, RowReveal>* reveals = nullptr;
    const Motion::State* motion = nullptr;
    int focus = 0; bool keyboard = false;
    float entrance = 1, viewTop = Panel::ViewTop(), viewBottom = 1e6f;
    float fadeTop = 0, fadeBottom = 0; // scroll edge effects (drawer)
    float fadeTopSize = DrawerLayout::topFade;
    bool edge = false;                 // drawer: hairline on its leading edge
    float originX = 0, originY = 0;    // where the 268 DIP column sits in the window (drawer morph)
    float edgeOpacity = 1;
    // Drawer morph: the popover's hairline border, carried by the drawer and
    // fading out as it docks (content coordinates).
    D2D1_RECT_F frame{}; float frameRadius = 0, frameOpacity = 0;
    std::int64_t nowMs = 0;
    Motion::Time now{};
};

// Drawer layout: sections in period order, rows clipped while folding.
// `open` holds each section's current fold value (1 = expanded).
inline std::vector<Slot> DrawerSlots(const std::vector<Item>& items, const std::array<std::vector<int>, PeriodCount>& groups,
                                     const std::array<float, PeriodCount>& open, float scroll, float* contentBottom = nullptr) {
    std::vector<Slot> slots; float y = DrawerLayout::firstSection - scroll;
    if (items.empty()) { slots.push_back({SlotKind::Empty, -1, y}); y += Panel::tile; }
    for (int p = 0; p < PeriodCount; ++p) {
        const auto& rows = groups[p]; if (rows.empty()) continue;
        const float o = Motion::Clamp(open[p]);
        slots.push_back({SlotKind::Section, p, y, 1, o});
        if (o > .001f) {
            const float clipTop = y + DrawerLayout::sectionHeight, clipBottom = clipTop + o * (DrawerLayout::toRows + rows.size() * Panel::pitch);
            for (size_t k = 0; k < rows.size(); ++k) {
                Slot row{SlotKind::Row, rows[k], clipTop + DrawerLayout::toRows + k * Panel::pitch - (1 - o) * DrawerMotion::foldTravel, o, 1};
                if (o < 1) { row.clipTop = clipTop; row.clipBottom = clipBottom; }
                if (row.top < clipBottom) slots.push_back(row);
            }
        }
        y += DrawerLayout::Block(static_cast<int>(rows.size()), o);
    }
    if (contentBottom) *contentBottom = y + scroll + Panel::padding - DrawerLayout::after;
    return slots;
}
inline std::vector<Slot> PopoverSlots(const std::vector<Item>& items) {
    std::vector<Slot> slots;
    if (items.empty()) { slots.push_back({SlotKind::Empty, -1, Panel::RowTop(0)}); return slots; }
    const int rows = std::min<int>(Panel::popoverRows, static_cast<int>(items.size()));
    for (int i = 0; i < rows; ++i) slots.push_back({SlotKind::Row, i, Panel::RowTop(i)});
    slots.push_back({SlotKind::ViewAll, -1, Panel::ViewAllTop(rows)});
    return slots;
}

class View {
    Chrome& chrome_;
    ID2D1DeviceContext* dc_ = nullptr; ID2D1Effect* blur_ = nullptr;
    ID2D1BitmapRenderTarget* layer_ = nullptr; D2D1_SIZE_F layerSize_{};
    ID2D1GradientStopCollection* stops_ = nullptr; ID2D1LinearGradientBrush* fade_ = nullptr;
    std::map<std::wstring, ID2D1Bitmap*> icons_;
    template<class T> static void Drop(T*& p) { if (p) { p->Release(); p = nullptr; } }
    void PushOpacity(float opacity) { dc_->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), opacity), nullptr); }
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
    // Shell icon lookups cost milliseconds: resolve them before a motion needs them.
    void Preload(const std::vector<Item>& items) { if (!chrome_.RenderTarget()) return; for (auto& item : items) FileIcon(item); }

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
            if (layered) PushOpacity(visual.opacity);
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
            if (visual.opacity < .998f) PushOpacity(visual.opacity);
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
    Motion::InteractionVisual Interaction(const Scene& scene, int hit) const { return scene.motion ? scene.motion->Visual(hit, scene.now) : Motion::InteractionVisual{}; }
    void ActionButton(Action action, D2D1_RECT_F box, int hit, const Scene& scene) {
        const auto* icon = IconFor(action); if (!icon) return;
        auto v = Interaction(scene, hit);
        chrome_.DrawButton(icon, box, Design::ButtonVariant::Ghost, {v.hover, v.press, false, scene.focus == hit});
    }
    void Row(const Item& item, float top, const Scene& scene) {
        auto* target = chrome_.RenderTarget(); auto* brush = chrome_.SolidBrush();
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
    // Section header: label + caret button (aligned with the close button). The
    // caret points down when open and turns -90 degrees as the section folds.
    void Section(Period period, float y, float open, const Scene& scene) {
        auto* target = chrome_.RenderTarget();
        const auto label = DrawerLayout::Label(y);
        chrome_.Text(PeriodLabel(period), {label.left, label.top, label.right, label.bottom}, Panel::line, Design::Text::muted, false, true, -.16f);
        const int hit = SectionHit(period); const auto v = Interaction(scene, hit);
        const auto c = DrawerLayout::Caret(y); const D2D1_RECT_F box{c.left, c.top, c.right, c.bottom};
        const auto style = Design::Resolve(Design::ButtonVariant::Ghost, {v.hover, v.press, false, scene.focus == hit});
        if (style.fill) chrome_.Round(box, style.radius, style.surface);
        if (style.focus) chrome_.FocusRing(box, style.radius);
        D2D1_MATRIX_3X2_F base; target->GetTransform(&base);
        const D2D1_POINT_2F center{(box.left + box.right) / 2, (box.top + box.bottom) / 2};
        target->SetTransform(D2D1::Matrix3x2F::Rotation(-90.f * (1 - Motion::Clamp(open)), center) * base);
        chrome_.Icon(L"caret", box, style.iconSize * Motion::PressScale(v.press, scene.motion && scene.motion->ReducedMotion()), style.iconOpacity);
        target->SetTransform(base);
    }
    float ViewAllWidth() { return chrome_.TextWidth(L"View all", Panel::line, false, false, -.16f); }
    void ViewAll(float top, const Scene& scene) {
        const auto v = Interaction(scene, ViewAllHit);
        const float w = ViewAllWidth();
        // Text link: brightens on hover, dims while pressed.
        const float opacity = (Design::Text::muted + (.78f - Design::Text::muted) * v.hover) * (1 - .25f * v.press);
        chrome_.Text(L"View all", {Panel::padding, top, Panel::padding + w + 2, top + Panel::line}, Panel::line, opacity, false, false, -.16f);
        if (scene.focus == ViewAllHit) { auto b = Panel::ViewAll(top, w); chrome_.FocusRing({b.left, b.top, b.right, b.bottom}, Design::Radius::inset); }
    }

    void Paint(const Scene& scene, const std::vector<Slot>& slots, float height) {
        auto* target = chrome_.RenderTarget(); auto* brush = chrome_.SolidBrush(); if (!target || !brush || !scene.items) return;
        Prepare();
        const float c = Design::Surface::canvas;
        target->Clear(D2D1::ColorF(c, c, c, 1));
        chrome_.keyboardModality = scene.keyboard;
        const float e = Motion::Clamp(scene.entrance);
        const bool entering = dc_ && e < .999f;
        const auto origin = D2D1::Matrix3x2F::Translation(scene.originX, scene.originY);
        target->SetTransform(D2D1::Matrix3x2F::Translation(0, -(1 - e) * Entrance::travel) * origin);
        if (entering) PushOpacity(e);

        // Header
        const auto title = Panel::Title();
        chrome_.Text(L"Download history", {title.left, title.top, title.right, title.bottom}, Panel::line, Design::Text::muted, false, true, -.16f);
        const auto close = Panel::Close();
        auto cv = Interaction(scene, CloseHit);
        chrome_.DrawButton(L"close", {close.left, close.top, close.right, close.bottom}, Design::ButtonVariant::Ghost, {cv.hover, cv.press, false, scene.focus == CloseHit});

        const auto& items = *scene.items;
        const float viewBottom = std::min(scene.viewBottom, height);
        target->PushAxisAlignedClip(D2D1::RectF(0, scene.viewTop, Panel::width, viewBottom), D2D1_ANTIALIAS_MODE_ALIASED);
        for (const auto& slot : slots) {
            switch (slot.kind) {
            case SlotKind::Empty:
                chrome_.Text(L"No downloads yet", {Panel::padding, slot.top, Panel::width - Panel::padding, slot.top + Panel::tile}, Panel::line, Design::Text::muted, false, false, -.16f);
                break;
            case SlotKind::Section:
            case SlotKind::ViewAll: {
                if (slot.opacity <= .002f || slot.top + DrawerLayout::sectionHeight < scene.viewTop || slot.top > viewBottom) break;
                const bool layered = dc_ && slot.opacity < .998f;
                if (layered) PushOpacity(slot.opacity);
                if (slot.kind == SlotKind::Section) Section(static_cast<Period>(slot.index), slot.top, slot.open, scene); else ViewAll(slot.top, scene);
                if (layered) dc_->PopLayer();
                break;
            }
            case SlotKind::Row: {
                if (slot.index < 0 || slot.index >= static_cast<int>(items.size())) break;
                const float top = std::max(slot.clipTop, scene.viewTop), bottom = std::min(slot.clipBottom, viewBottom);
                if (bottom <= top || slot.top + Panel::tile + Panel::rowGap < top || slot.top - Panel::rowGap > bottom) break;
                const bool clipped = slot.clipTop > -1e5f, layered = dc_ && slot.opacity < .998f;
                if (slot.opacity <= .002f) break;
                if (clipped) target->PushAxisAlignedClip(D2D1::RectF(0, top, Panel::width, bottom), D2D1_ANTIALIAS_MODE_ALIASED);
                if (layered) PushOpacity(slot.opacity);
                Row(items[slot.index], slot.top, scene);
                if (layered) dc_->PopLayer();
                if (clipped) target->PopAxisAlignedClip();
                break;
            }
            }
        }
        // Scroll edge effects: content dissolves into the canvas where more lies beyond.
        if (fade_ && scene.fadeTop > 0) {
            fade_->SetStartPoint({0, scene.viewTop + scene.fadeTopSize}); fade_->SetEndPoint({0, scene.viewTop}); fade_->SetOpacity(Motion::Clamp(scene.fadeTop));
            target->FillRectangle(D2D1::RectF(0, scene.viewTop, Panel::width, scene.viewTop + scene.fadeTopSize), fade_);
        }
        if (fade_ && scene.fadeBottom > 0) {
            fade_->SetStartPoint({0, viewBottom - DrawerLayout::bottomFade}); fade_->SetEndPoint({0, viewBottom}); fade_->SetOpacity(Motion::Clamp(scene.fadeBottom));
            target->FillRectangle(D2D1::RectF(0, viewBottom - DrawerLayout::bottomFade, Panel::width, viewBottom), fade_);
        }
        if (fade_) fade_->SetOpacity(1);
        target->PopAxisAlignedClip();
        if (entering) dc_->PopLayer();
        target->SetTransform(origin);
        if (scene.frameOpacity > 0) {
            const float px = target->GetSize().width / std::max(1.f, static_cast<float>(target->GetPixelSize().width));
            auto f = scene.frame; f.left += px / 2; f.top += px / 2; f.right -= px / 2; f.bottom -= px / 2;
            chrome_.Color(1, Popup::border * scene.frameOpacity);
            target->DrawRoundedRectangle(D2D1::RoundedRect(f, scene.frameRadius, scene.frameRadius), brush, px);
        }
        if (scene.edge && scene.edgeOpacity > 0) { // one physical pixel, a touch lighter than the canvas
            const float px = target->GetSize().width / std::max(1.f, static_cast<float>(target->GetPixelSize().width));
            chrome_.Color(1, Popup::border * scene.edgeOpacity); target->FillRectangle(D2D1::RectF(0, 0, px, height), brush);
        }
        target->SetTransform(D2D1::IdentityMatrix());
    }
};

// Window, input, reveals, tooltips and keyboard shared by both surfaces.
class Host {
public:
    struct Hooks {
        std::function<void(Motion::Time)> schedule;    // request display-paced frames
        std::function<void(int, Action)> action;       // item id, action
        std::function<void(bool)> closed;              // true when closed from the keyboard
        std::function<void()> viewAll;                 // popover link → drawer
    };
protected:
    HWND hwnd_ = nullptr, owner_ = nullptr, tip_ = nullptr;
    std::vector<UINT_PTR> tips_;
    Chrome chrome_; View view_{chrome_};
    Motion::State motion_;
    std::map<int, RowReveal> reveals_;
    Hooks hooks_;
    std::vector<Item>* items_ = nullptr;
    int hoverItem_ = -1, hover_ = 0, pressed_ = 0, focus_ = 0; // hoverItem_: item id
    bool keyboard_ = false, visible_ = false, animatingPainted_ = false, tracking_ = false;
    static constexpr UINT_PTR TickTimer = 1;
    float Dpi() const { return static_cast<float>(GetDpiForWindow(hwnd_ ? hwnd_ : owner_)); }
    int Px(float dip) const { return static_cast<int>(std::lround(dip * Dpi() / 96.f)); }
    float ClientHeight() const { RECT r{}; if (hwnd_) GetClientRect(hwnd_, &r); return r.bottom * 96.f / Dpi(); }

    virtual std::vector<Slot> Slots(Motion::Time now) const = 0;
    virtual float SurfaceHeight() const { return ClientHeight(); }
    virtual void Decorate(Scene&) const {}
    virtual bool ActivateOther(int, bool) { return false; }
    virtual void Wheel(float) {}
    virtual void Reveal(int) {}                 // keep a focused element in view
    virtual Motion::Wake ExtraWake(Motion::Time) const { return {}; }
    virtual const wchar_t* ClassName() const = 0;
    virtual void Configure() {}
    virtual D2D1_POINT_2F Origin(Motion::Time) const { return {0, 0}; } // column offset in the window (DIP)
public:
    virtual void Dismiss(bool keyboard) = 0;
protected:
    Scene MakeScene(Motion::Time now) const {
        Scene scene; scene.items = items_; scene.reveals = &reveals_; scene.motion = &motion_;
        scene.focus = GetFocus() == hwnd_ ? focus_ : 0; scene.keyboard = keyboard_;
        scene.now = now; scene.nowMs = UnixNowMs(); scene.viewBottom = SurfaceHeight();
        const auto origin = Origin(now); scene.originX = origin.x; scene.originY = origin.y;
        Decorate(scene); return scene;
    }
    int Hit(float x, float y, int& item) const {
        item = -1;
        const auto origin = Origin(Motion::Now()); x -= origin.x; y -= origin.y;
        auto close = Panel::Close();
        if (x >= close.left && x < close.right && y >= close.top && y < close.bottom) return CloseHit;
        const auto scene = MakeScene(Motion::Now());
        if (!items_ || y < scene.viewTop || y >= scene.viewBottom) return 0;
        for (const auto& slot : Slots(scene.now)) {
            if (slot.kind == SlotKind::Section) {
                auto h = DrawerLayout::Header(slot.top);
                if (y >= h.top && y < h.bottom) return SectionHit(static_cast<Period>(slot.index));
            } else if (slot.kind == SlotKind::ViewAll) {
                auto b = Panel::ViewAll(slot.top, const_cast<View&>(view_).ViewAllWidth());
                if (x >= b.left && x < b.right && y >= b.top && y < b.bottom) return ViewAllHit;
            } else if (slot.kind == SlotKind::Row && slot.opacity > .5f) {
                auto band = Panel::Hover(slot.top);
                const float top = std::max(band.top, slot.clipTop), bottom = std::min(band.bottom, slot.clipBottom);
                if (y < top || y >= bottom) continue;
                const auto& entry = (*items_)[slot.index]; item = entry.id;
                const auto actions = ActionsFor(entry);
                for (int s = 0; s < 2; ++s) {
                    if ((s == 0 ? actions.first : actions.second) == Action::None) continue;
                    auto box = Panel::ActionBox(slot.top, s);
                    if (x >= box.left && x < box.right && y >= box.top && y < box.bottom) return ActionHit(entry.id, s);
                }
                return 0;
            }
        }
        return 0;
    }
    // A row shows its actions while hovered or holding keyboard focus.
    void UpdateReveals(Motion::Time now) {
        const bool reduced = motion_.ReducedMotion();
        const int focusId = IsAction(focus_) ? HitItem(focus_) : -1;
        if (hoverItem_ >= 0) reveals_.try_emplace(hoverItem_);
        if (focusId >= 0) reveals_.try_emplace(focusId);
        for (auto it = reveals_.begin(); it != reveals_.end();) {
            const bool shown = it->first == hoverItem_ || it->first == focusId;
            it->second.Set(shown, now, reduced);
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
        const float s = 96.f / Dpi(); int item = -1, hit = 0;
        if (PtInRect(&client, p)) hit = Hit(p.x * s, p.y * s, item);
        hoverItem_ = item; Interact(hit, pressed_);
    }
    std::vector<int> FocusOrder() const {
        std::vector<int> order{CloseHit};
        if (!items_) return order;
        for (const auto& slot : Slots(Motion::Now())) {
            if (slot.kind == SlotKind::Section) order.push_back(SectionHit(static_cast<Period>(slot.index)));
            else if (slot.kind == SlotKind::ViewAll) order.push_back(ViewAllHit);
            else if (slot.kind == SlotKind::Row && slot.opacity > .5f) {
                const auto& item = (*items_)[slot.index]; auto a = ActionsFor(item);
                if (a.first != Action::None) order.push_back(ActionHit(item.id, 0));
                if (a.second != Action::None) order.push_back(ActionHit(item.id, 1));
            }
        }
        return order;
    }
    int IndexOf(int id) const { if (!items_) return -1; for (size_t i = 0; i < items_->size(); ++i) if ((*items_)[i].id == id) return static_cast<int>(i); return -1; }
    void Activate(int hit, bool keyboard) {
        if (hit == CloseHit) { Dismiss(keyboard); return; }
        if (ActivateOther(hit, keyboard)) return;
        if (!IsAction(hit) || !hooks_.action) return;
        const int index = IndexOf(HitItem(hit)); if (index < 0) return;
        const auto actions = ActionsFor((*items_)[index]);
        const Action action = HitSlot(hit) == 0 ? actions.first : actions.second;
        if (action != Action::None) hooks_.action(HitItem(hit), action);
    }
    // Icon-only buttons carry tooltips; tools follow the laid-out rows.
    void RefreshTips() {
        if (!tip_) return;
        for (auto id : tips_) { TOOLINFOW t{}; t.cbSize = sizeof(t); t.hwnd = hwnd_; t.uId = id; SendMessageW(tip_, TTM_DELTOOLW, 0, reinterpret_cast<LPARAM>(&t)); }
        tips_.clear();
        const auto origin = Origin(Motion::Now());
        auto add = [&](UINT_PTR id, Design::Box b, const wchar_t* text) {
            TOOLINFOW t{}; t.cbSize = sizeof(t); t.uFlags = TTF_SUBCLASS; t.hwnd = hwnd_; t.uId = id;
            t.rect = {Px(b.left + origin.x), Px(b.top + origin.y), Px(b.right + origin.x), Px(b.bottom + origin.y)}; t.lpszText = const_cast<wchar_t*>(text);
            SendMessageW(tip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&t)); tips_.push_back(id);
        };
        add(CloseHit, Panel::Close(), L"Close (Esc)");
        if (!items_) return;
        const auto scene = MakeScene(Motion::Now());
        for (const auto& slot : Slots(scene.now)) {
            if (slot.kind == SlotKind::Section) {
                if (slot.top >= scene.viewTop && slot.top + DrawerLayout::sectionHeight <= scene.viewBottom)
                    add(static_cast<UINT_PTR>(SectionHit(static_cast<Period>(slot.index))), DrawerLayout::Caret(slot.top), slot.open > .5f ? L"Collapse" : L"Expand");
                continue;
            }
            if (slot.kind != SlotKind::Row || slot.opacity < .99f || slot.top < scene.viewTop || slot.top + Panel::tile > scene.viewBottom) continue;
            const auto& item = (*items_)[slot.index]; const auto actions = ActionsFor(item);
            for (int s = 0; s < 2; ++s) {
                const Action action = s == 0 ? actions.first : actions.second;
                if (action != Action::None) add(static_cast<UINT_PTR>(ActionHit(item.id, s)), Panel::ActionBox(slot.top, s), TipFor(action));
            }
        }
    }
    void Ticker() {
        bool active = false;
        if (visible_ && items_) for (auto& item : *items_) if (item.state == State::InProgress && item.endTime > 0) active = true;
        if (active) SetTimer(hwnd_, TickTimer, 1000, nullptr); else KillTimer(hwnd_, TickTimer);
    }
    void PaintNow() {
        if (!chrome_.RenderTarget()) chrome_.Resize();
        auto* target = chrome_.RenderTarget(); if (!target || !items_) return;
        const auto scene = MakeScene(Motion::Now());
        target->BeginDraw();
        view_.Paint(scene, Slots(scene.now), SurfaceHeight());
        if (target->EndDraw() == D2DERR_RECREATE_TARGET) { view_.Release(); chrome_.RecreateTarget(); }
    }
    void ClearPointer() { if (GetCapture() == hwnd_) ReleaseCapture(); reveals_.clear(); motion_.ClearInteractions(); hoverItem_ = -1; hover_ = pressed_ = 0; tracking_ = false; }
    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<Host*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (msg == WM_NCCREATE) { self = static_cast<Host*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); self->hwnd_ = h; }
        return self ? self->Handle(h, msg, w, l) : DefWindowProcW(h, msg, w, l);
    }
    virtual LRESULT Handle(HWND h, UINT msg, WPARAM w, LPARAM l) {
        const float scale = 96.f / Dpi();
        switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); PaintNow(); EndPaint(h, &ps); return 0; }
        case WM_SIZE: chrome_.Resize(); Invalidate(); return 0;
        case WM_MOUSEACTIVATE: return MA_ACTIVATE;
        case WM_ACTIVATE: if (LOWORD(w) == WA_INACTIVE) Dismiss(false); return 0;
        case WM_MOUSEMOVE: {
            if (!tracking_) { TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, h, 0}; TrackMouseEvent(&track); tracking_ = true; }
            int item = -1; const int hit = Hit(GET_X_LPARAM(l) * scale, GET_Y_LPARAM(l) * scale, item);
            if (item != hoverItem_ || hit != hover_) { hoverItem_ = item; Interact(hit, pressed_); }
            return 0;
        }
        case WM_MOUSELEAVE: tracking_ = false; hoverItem_ = -1; Interact(0, 0); return 0;
        case WM_LBUTTONDOWN: { int item; const int hit = Hit(GET_X_LPARAM(l) * scale, GET_Y_LPARAM(l) * scale, item); keyboard_ = false; if (hit) { SetCapture(h); Interact(hit, hit); } return 0; }
        case WM_LBUTTONUP: { int item; const int hit = Hit(GET_X_LPARAM(l) * scale, GET_Y_LPARAM(l) * scale, item); const int was = pressed_; if (GetCapture() == h) ReleaseCapture(); hoverItem_ = item; Interact(hit, 0); if (was && was == hit) Activate(hit, false); return 0; }
        case WM_CAPTURECHANGED: if (pressed_) Interact(hover_, 0); return 0;
        case WM_MOUSEWHEEL: Wheel(GET_WHEEL_DELTA_WPARAM(w) / 120.f); return 0;
        case WM_KEYDOWN: {
            if (w == VK_ESCAPE) { Dismiss(true); return 0; }
            auto order = FocusOrder();
            if (w == VK_TAB || w == VK_DOWN || w == VK_UP) {
                keyboard_ = true; const bool back = w == VK_UP || (w == VK_TAB && GetKeyState(VK_SHIFT) < 0);
                auto it = std::find(order.begin(), order.end(), focus_);
                int index = it == order.end() ? (back ? static_cast<int>(order.size()) - 1 : 0) : static_cast<int>(it - order.begin()) + (back ? -1 : 1);
                index = (index + static_cast<int>(order.size())) % static_cast<int>(order.size());
                focus_ = order[index]; Reveal(focus_); Interact(hover_, pressed_); return 0;
            }
            if ((w == VK_RETURN || w == VK_SPACE) && focus_) { keyboard_ = true; Activate(focus_, true); return 0; }
            return 0;
        }
        case WM_TIMER: if (w == TickTimer) { Invalidate(); Ticker(); } return 0;
        case WM_DESTROY: KillTimer(h, TickTimer); return 0;
        }
        return DefWindowProcW(h, msg, w, l);
    }
    bool CreateSurface(HINSTANCE instance, HWND owner, const std::wstring& root, std::vector<Item>* items, Hooks hooks, UINT classStyle, float width, float height) {
        owner_ = owner; items_ = items; hooks_ = std::move(hooks); motion_.EnablePolish(); chrome_.SetMotion(&motion_);
        chrome_.textCacheLimit = 1024; // every row's name/detail runs + labels, at any history size
        WNDCLASSW wc{}; wc.style = classStyle; wc.lpfnWndProc = Proc; wc.hInstance = instance; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.lpszClassName = ClassName();
        RegisterClassW(&wc);
        CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"Download history", WS_POPUP, 0, 0, Px(width), Px(height), owner, nullptr, instance, this);
        if (!hwnd_) return false;
        tip_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0, 0, 0, hwnd_, nullptr, instance, nullptr);
        Configure();
        return SUCCEEDED(chrome_.Init(hwnd_, root));
    }
    void Activated() { ShowWindow(hwnd_, SW_SHOW); SetForegroundWindow(hwnd_); SetFocus(hwnd_); }
public:
    // Recently closed by deactivation: the click that caused it must not reopen it.
    ULONGLONG hiddenAt = 0;
    virtual ~Host() { Dispose(); }
    // Release Direct2D/WIC before CoUninitialize; the owner destroys the window.
    void Dispose() { view_.Release(); chrome_.Dispose(); hwnd_ = tip_ = nullptr; tips_.clear(); visible_ = false; }
    HWND Window() const { return hwnd_; }
    bool Visible() const { return visible_; }
    void Invalidate() { if (hwnd_ && visible_) InvalidateRect(hwnd_, nullptr, FALSE); }
    Motion::Wake NextWake(Motion::Time now) const {
        if (!visible_) return {};
        auto wake = Motion::Earlier(motion_.NextWake(now), ExtraWake(now));
        for (auto& item : reveals_) wake = Motion::Earlier(wake, item.second.NextWake(now));
        return wake;
    }
    // Display-paced frame from the host clock; also paints the frame after motion ends.
    virtual void Frame(Motion::Time now) {
        const bool moving = NextWake(now).delay.has_value();
        if (moving || animatingPainted_) Invalidate();
        if (!moving && animatingPainted_) RefreshTips();
        animatingPainted_ = moving;
        if (!moving) { for (auto it = reveals_.begin(); it != reveals_.end();) { if (!it->second.Shown() && it->second.Idle(now)) it = reveals_.erase(it); else ++it; } }
    }
};

class Popover final : public Host {
    Motion::Tween entrance_;
    float anchorRight_ = 0, anchorTop_ = 0;
    int Rows() const { return items_ ? std::min<int>(Panel::popoverRows, static_cast<int>(items_->size())) : 0; }
    float Height() const { return Panel::Height(Rows(), items_ && !items_->empty()); }
    std::vector<Slot> Slots(Motion::Time) const override { static const std::vector<Item> none; return PopoverSlots(items_ ? *items_ : none); }
    float SurfaceHeight() const override { return Height(); }
    void Decorate(Scene& scene) const override { scene.entrance = entrance_.Value(scene.now); scene.viewBottom = Height() - Panel::padding / 2; }
    Motion::Wake ExtraWake(Motion::Time now) const override { return entrance_.Moving(now) ? Motion::Frame() : Motion::Wake{}; }
    bool ActivateOther(int hit, bool) override { if (hit != ViewAllHit) return false; if (hooks_.viewAll) hooks_.viewAll(); return true; }
    const wchar_t* ClassName() const override { return L"LisiereDownloads"; }
    void Configure() override {
        // Windows 11: rounded, antialiased corners and a hairline border from DWM.
        const int round = 2; // DWMWCP_ROUND
        const COLORREF border = Design::NativeGray(Design::Surface::canvas + Popup::border * (1 - Design::Surface::canvas));
        if (FAILED(DwmSetWindowAttribute(hwnd_, 33, &round, sizeof(round))))
            SetWindowRgn(hwnd_, CreateRoundRectRgn(0, 0, Px(Panel::width) + 1, Px(Height()) + 1, Px(Panel::radius * 2), Px(Panel::radius * 2)), TRUE);
        DwmSetWindowAttribute(hwnd_, 34, &border, sizeof(border));
    }
    void Place() {
        if (!hwnd_) return;
        const int w = Px(Panel::width), h = Px(Height());
        SetWindowPos(hwnd_, HWND_TOP, static_cast<int>(std::lround(anchorRight_)) - w, static_cast<int>(std::lround(anchorTop_)), w, h, SWP_NOACTIVATE);
        RefreshTips();
    }
    LRESULT Handle(HWND h, UINT msg, WPARAM w, LPARAM l) override { if (msg == WM_DPICHANGED) { Place(); return 0; } return Host::Handle(h, msg, w, l); }
public:
    bool Create(HINSTANCE instance, HWND owner, const std::wstring& root, std::vector<Item>* items, Hooks hooks) {
        items_ = items; return CreateSurface(instance, owner, root, items, std::move(hooks), 0, Panel::width, Height());
    }
    // anchor: toolbar button rectangle in screen pixels; the popover's right edge aligns with it.
    void Show(RECT anchor) {
        if (!hwnd_) return;
        motion_.SetReducedMotion(Motion::SystemReducedMotion());
        anchorRight_ = static_cast<float>(anchor.right); anchorTop_ = anchor.bottom + Px(Panel::anchorGap);
        focus_ = 0; keyboard_ = false; ClearPointer();
        auto now = Motion::Now();
        entrance_.Snap(0); entrance_.Set(1, now, Entrance::duration, motion_.ReducedMotion(), Motion::Curve::Icon);
        Place(); visible_ = true;
        // Shown first without activation, painted, then activated: never a blank frame.
        SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        PaintNow(); ValidateRect(hwnd_, nullptr); Activated();
        Repoint(); Ticker(); // pointer already over the panel: reveal immediately
    }
    void Dismiss(bool keyboard) override { Hide(keyboard); }
    void Hide(bool keyboard) {
        if (!visible_) return;
        visible_ = false; hiddenAt = GetTickCount64();
        KillTimer(hwnd_, TickTimer); ClearPointer();
        ShowWindow(hwnd_, SW_HIDE);
        if (hooks_.closed) hooks_.closed(keyboard);
    }
    // The model changed (progress, state, list): resize to fit and repaint.
    void Changed() {
        if (!hwnd_ || !visible_) return;
        auto order = FocusOrder(); if (focus_ && std::find(order.begin(), order.end(), focus_) == order.end()) focus_ = 0;
        RECT r; GetWindowRect(hwnd_, &r);
        if (r.bottom - r.top != Px(Height())) Place(); else RefreshTips();
        Repoint(); Ticker(); // the row under the pointer may have changed
    }
};

// Blends the popover layout (t = 0) into the drawer layout (t = 1). The whole
// drawer list starts shifted so its rows sit exactly on the popover's rows, then
// glides into place: section headers emerge from under the title instead of
// crossing the rows. Content the popover did not show fades in over the last
// 75%; "View all" leaves within the first 30%. Pure, so the fixture renders it.
inline std::vector<Slot> MorphSlots(const std::vector<Item>& items, const std::vector<Slot>& drawer, float t) {
    const auto popover = PopoverSlots(items);
    std::map<int, float> from; // item index -> popover top
    for (auto& slot : popover) if (slot.kind == SlotKind::Row) from[slot.index] = slot.top;
    float shift = 0; // first shared row: popover top - drawer top
    for (auto& slot : drawer) if (slot.kind == SlotKind::Row && from.count(slot.index)) { shift = from[slot.index] - slot.top; break; }
    const float late = Motion::Clamp((t - DrawerMotion::morphLead) / (1 - DrawerMotion::morphLead));
    std::vector<Slot> slots;
    for (auto slot : drawer) {
        auto f = slot.kind == SlotKind::Row ? from.find(slot.index) : from.end();
        if (f != from.end()) { slot.top = f->second + (slot.top - f->second) * t; slot.clipTop = -1e6f; slot.clipBottom = 1e6f; from.erase(f); }
        else { slot.top += shift * (1 - t); slot.opacity *= late; }
        slots.push_back(slot);
    }
    for (auto& [index, top] : from) slots.push_back({SlotKind::Row, index, top, 1 - t}); // rows of folded sections
    for (auto& slot : popover) if (slot.kind == SlotKind::ViewAll) { Slot link = slot; link.opacity = 1 - Motion::Clamp(t / DrawerMotion::morphLinkOut); slots.push_back(link); }
    return slots;
}

// Docked drawer: the full history grouped by period. Two motions, both cheap:
//  - Morph (from "View all"): the window sits at its final place and size; a
//    region shaped like the popover grows into the drawer while the content is
//    re-laid out (one Direct2D frame + one GDI region per display frame).
//  - Slide (close, Escape, toolbar button): the window itself moves right,
//    clipped to the owner's client area, so DWM keeps content and position in
//    sync without repainting.
class Drawer final : public Host {
    Lane slide_, scroll_, morph_;
    std::array<Lane, PeriodCount> fold_;
    bool closing_ = false;
    RECT morphFrom_{}; float morphRadius_ = 0; // popover rectangle in window pixels
    int appliedVisible_ = -1, appliedMorph_ = -1; RECT appliedOwner_{}, appliedWindow_{};
    bool warmed_ = false; std::tuple<size_t, int, LONG, LONG, UINT> warmSignature_{};
    std::array<std::vector<int>, PeriodCount> Groups() const {
        std::array<std::vector<int>, PeriodCount> groups;
        if (!items_) return groups;
        const auto now = UnixNowMs(), offset = LocalOffsetMs();
        for (size_t i = 0; i < items_->size(); ++i) groups[static_cast<int>(PeriodOf((*items_)[i].started, now, offset))].push_back(static_cast<int>(i));
        return groups;
    }
    std::array<float, PeriodCount> Folds(Motion::Time now, bool target = false) const {
        std::array<float, PeriodCount> open{}; for (int p = 0; p < PeriodCount; ++p) open[p] = target ? fold_[p].Target() : fold_[p].Value(now); return open;
    }
    float MaxScroll(Motion::Time now, bool target) const {
        static const std::vector<Item> none; float bottom = 0;
        DrawerSlots(items_ ? *items_ : none, Groups(), Folds(now, target), 0, &bottom);
        return std::max(0.f, bottom - SurfaceHeight());
    }
    float Morph(Motion::Time now) const { return Motion::Clamp(morph_.Value(now)); }
    std::vector<Slot> Slots(Motion::Time now) const override {
        static const std::vector<Item> none; const auto& items = items_ ? *items_ : none;
        auto slots = DrawerSlots(items, Groups(), Folds(now), scroll_.Value(now));
        const float t = Morph(now);
        return t < 1 ? MorphSlots(items, slots, t) : slots;
    }
    float Scale() const { return 96.f / Dpi(); }
    // The 268 DIP column: at the popover's place when the morph starts, docked after.
    D2D1_POINT_2F Origin(Motion::Time now) const override {
        const float t = Morph(now), docked = DrawerLayout::margin;
        return {(morphFrom_.left * Scale()) + (docked - morphFrom_.left * Scale()) * t, morphFrom_.top * Scale() * (1 - t)};
    }
    void Decorate(Scene& scene) const override {
        const float t = Morph(scene.now);
        scene.viewTop = DrawerLayout::viewTop; scene.edge = true; scene.edgeOpacity = t;
        const float popoverBottom = (morphFrom_.bottom - morphFrom_.top) * Scale() - Panel::padding / 2;
        scene.viewBottom = SurfaceHeight() + (std::min(popoverBottom, SurfaceHeight()) - SurfaceHeight()) * (1 - t);
        const float scroll = scroll_.Value(scene.now), max = MaxScroll(scene.now, false);
        scene.fadeTop = Motion::Clamp(scroll / DrawerLayout::topFade);
        scene.fadeBottom = Motion::Clamp((max - scroll) / Panel::pitch) * t;
        if (t < 1) {
            // The popover's border rides the growing shape and fades as it docks.
            const float s = Scale(), w = (DrawerLayout::width + DrawerLayout::margin);
            auto lerp = [t](float a, float b) { return a + (b - a) * t; };
            scene.frame = {lerp(morphFrom_.left * s, DrawerLayout::margin) - scene.originX, lerp(morphFrom_.top * s, 0) - scene.originY,
                           lerp(morphFrom_.right * s, w) - scene.originX, lerp(morphFrom_.bottom * s, SurfaceHeight()) - scene.originY};
            scene.frameRadius = morphRadius_ * s * (1 - t); scene.frameOpacity = 1 - t;
            // Content meets the moving edge through a fade, never a hard cut.
            const float edge = 4 * t * (1 - t);
            scene.fadeBottom = std::max(scene.fadeBottom, edge); scene.fadeTop = std::max(scene.fadeTop, edge);
            scene.fadeTopSize = DrawerLayout::topFade * 2; // headers emerge from under the title through a deeper fade
            const float headerBottom = Panel::headerTop + Panel::headerHeight; // 36: content may rise to the header line while it docks
            scene.viewTop = headerBottom + (DrawerLayout::viewTop - headerBottom) * t;
        }
    }
    Motion::Wake ExtraWake(Motion::Time now) const override {
        bool moving = slide_.Moving(now) || scroll_.Moving(now) || morph_.Moving(now);
        for (auto& lane : fold_) moving = moving || lane.Moving(now);
        return moving ? Motion::Frame() : Motion::Wake{};
    }
    const wchar_t* ClassName() const override { return L"LisiereDownloadsDrawer"; }
    void Configure() override {
        const int square = 1; DwmSetWindowAttribute(hwnd_, 33, &square, sizeof(square)); // DWMWCP_DONOTROUND: the region shapes it
        const COLORREF none = 0xFFFFFFFE; DwmSetWindowAttribute(hwnd_, 34, &none, sizeof(none));
    }
    RECT OwnerClient() const { RECT r{}; GetClientRect(owner_, &r); MapWindowPoints(owner_, nullptr, reinterpret_cast<POINT*>(&r), 2); return r; }
    int OwnerPx(float dip) const { return static_cast<int>(std::lround(dip * GetDpiForWindow(owner_) / 96.f)); }
    float SurfaceHeight() const override { return ClientHeight(); }
    // Window = margin strip + 268 DIP column, docked to the owner's right edge.
    // The slide moves it right; the region clips it to the owner's client area,
    // rounds the corner it shares with the owner and, while morphing, takes the
    // shape of the popover growing into the drawer.
    void ApplyGeometry(Motion::Time now, bool force = false) {
        if (!hwnd_) return;
        const RECT owner = OwnerClient();
        const int column = OwnerPx(DrawerLayout::width), margin = OwnerPx(DrawerLayout::margin), width = column + margin;
        const int top = owner.top + OwnerPx(DrawerLayout::top), height = std::max(1, static_cast<int>(owner.bottom - top));
        const int shown = static_cast<int>(std::lround(column * Motion::Clamp(slide_.Value(now))));
        const float t = Morph(now); const int morphKey = t >= 1 ? 1000 : static_cast<int>(t * 999);
        if (!force && shown == appliedVisible_ && morphKey == appliedMorph_ && EqualRect(&owner, &appliedOwner_)) return;
        const bool resized = !EqualRect(&owner, &appliedOwner_);
        appliedVisible_ = shown; appliedMorph_ = morphKey; appliedOwner_ = owner;
        const RECT window{owner.right - width + (column - shown), top, owner.right + (column - shown), top + height};
        if (force || !EqualRect(&window, &appliedWindow_)) { SetWindowPos(hwnd_, nullptr, window.left, window.top, width, height, SWP_NOACTIVATE | SWP_NOZORDER); appliedWindow_ = window; }
        HRGN region = nullptr;
        if (shown > 0) {
            // Owner clip: everything left of the owner's right edge, rounding the corner it shares with it.
            const int right = margin + shown, r = OwnerPx(20);
            region = IsZoomed(owner_) ? CreateRectRgn(0, 0, right, height) : CreateRoundRectRgn(-r * 2, -r * 2, right + 1, height + 1, r, r);
            // Shape: the docked column [margin, width] — never the margin strip, which
            // only exists for the popover to morph from — or, while morphing, the
            // popover rectangle (rounded like DWM's popup corners) growing into it.
            HRGN shape = nullptr;
            if (t < 1) {
                auto lerp = [t](LONG a, int b) { return static_cast<int>(std::lround(a + (b - a) * t)); };
                const int radius = static_cast<int>(std::lround(morphRadius_ * (1 - t))) * 2 + 1;
                shape = CreateRoundRectRgn(lerp(morphFrom_.left, margin), lerp(morphFrom_.top, 0), lerp(morphFrom_.right, width) + 1, lerp(morphFrom_.bottom, height) + 1, radius, radius);
            } else shape = CreateRectRgn(margin, 0, width, height);
            CombineRgn(region, region, shape, RGN_AND); DeleteObject(shape);
        } else region = CreateRectRgn(0, 0, 0, 0);
        // No redraw: the content for this frame is already presented (morph frames
        // paint first) or unchanged (slide), so a region change costs no paint.
        SetWindowRgn(hwnd_, region, FALSE);
        if (resized) { scroll_.Snap(std::clamp(scroll_.Target(), 0.f, MaxScroll(now, true))); RefreshTips(); }
    }
    void ScrollTo(float target, Motion::Time now, Motion::Milliseconds duration) {
        target = std::clamp(target, 0.f, MaxScroll(now, true));
        scroll_.Set(target, now, Motion::Milliseconds(0), motion_.ReducedMotion() ? Motion::Milliseconds(0) : duration);
    }
    void Wheel(float notches) override {
        auto now = Motion::Now(); ScrollTo(scroll_.Target() - notches * Panel::pitch * 1.5f, now, DrawerMotion::scroll);
        Repoint(); if (hooks_.schedule) hooks_.schedule(now);
    }
    bool ActivateOther(int hit, bool) override {
        if (!IsSection(hit)) return false;
        auto now = Motion::Now(); auto& lane = fold_[static_cast<int>(HitPeriod(hit))];
        lane.Set(lane.Target() > .5f ? 0.f : 1.f, now, Motion::Milliseconds(0), motion_.ReducedMotion() ? Motion::Milliseconds(0) : DrawerMotion::fold, DrawerMotion::foldSpring);
        ScrollTo(scroll_.Target(), now, DrawerMotion::fold); // content got shorter: settle within bounds
        Repoint(); RefreshTips(); if (hooks_.schedule) hooks_.schedule(now);
        return true;
    }
    void Reveal(int hit) override {
        auto now = Motion::Now(); const float scroll = scroll_.Target();
        for (const auto& slot : DrawerSlots(*items_, Groups(), Folds(now, true), scroll)) {
            const bool match = (slot.kind == SlotKind::Section && SectionHit(static_cast<Period>(slot.index)) == hit) ||
                               (slot.kind == SlotKind::Row && IsAction(hit) && (*items_)[slot.index].id == HitItem(hit));
            if (!match) continue;
            const float height = slot.kind == SlotKind::Row ? Panel::tile : DrawerLayout::sectionHeight;
            const float viewTop = DrawerLayout::viewTop + DrawerLayout::topFade, viewBottom = SurfaceHeight() - Panel::pitch;
            if (slot.top < viewTop) ScrollTo(scroll - (viewTop - slot.top), now, DrawerMotion::scroll);
            else if (slot.top + height > viewBottom) ScrollTo(scroll + (slot.top + height - viewBottom), now, DrawerMotion::scroll);
            return;
        }
    }
    LRESULT Handle(HWND h, UINT msg, WPARAM w, LPARAM l) override {
        if (msg == WM_DPICHANGED) { ApplyGeometry(Motion::Now(), true); return 0; }
        return Host::Handle(h, msg, w, l);
    }
    void Finish() {
        closing_ = false; visible_ = false; appliedVisible_ = appliedMorph_ = -1; appliedWindow_ = {};
        morph_.Snap(1);
        KillTimer(hwnd_, TickTimer); ShowWindow(hwnd_, SW_HIDE);
    }
public:
    Drawer() { for (auto& lane : fold_) lane.Snap(1); morph_.Snap(1); }
    bool Create(HINSTANCE instance, HWND owner, const std::wstring& root, std::vector<Item>* items, Hooks hooks) {
        return CreateSurface(instance, owner, root, items, std::move(hooks), 0, DrawerLayout::width + DrawerLayout::margin, 400);
    }
    bool Closing() const { return closing_; }
    // Prepares everything the first drawer frames need (window size, render
    // target, shell icons, text layouts) while hidden. Called while the popover
    // is open so "View all" starts the morph immediately.
    void Warm() {
        if (!hwnd_ || visible_ || !items_) return;
        // Same history, same owner size: caches and render target are still warm.
        const RECT owner = OwnerClient();
        const auto signature = std::make_tuple(items_->size(), items_->empty() ? 0 : items_->front().id, owner.right - owner.left, owner.bottom - owner.top, GetDpiForWindow(owner_));
        if (warmed_ && signature == warmSignature_) return;
        warmed_ = true; warmSignature_ = signature;
        auto now = Motion::Now();
        appliedWindow_ = {}; appliedOwner_ = {}; ApplyGeometry(now, true);
        view_.Preload(*items_);
        const float morph = morph_.Value(now); morph_.Snap(1); PaintNow(); morph_.Snap(morph);
        appliedVisible_ = appliedMorph_ = -1;
    }
    // from: the popover's screen rectangle to morph out of, or null to slide in;
    // under: the popover window, kept above the drawer until the hand-off.
    void Show(const RECT* from = nullptr, HWND under = nullptr) {
        if (!hwnd_) return;
        motion_.SetReducedMotion(Motion::SystemReducedMotion());
        auto now = Motion::Now(); const bool reduced = motion_.ReducedMotion();
        if (!visible_) { slide_.Snap(0); scroll_.Snap(0); morph_.Snap(1); focus_ = 0; keyboard_ = false; ClearPointer(); }
        if (from && !reduced) {
            // Window-local pixels of the popover, the drawer's column at its docked place.
            const RECT owner = OwnerClient();
            const int width = OwnerPx(DrawerLayout::width + DrawerLayout::margin), left = owner.right - width, top = owner.top + OwnerPx(DrawerLayout::top);
            morphFrom_ = {from->left - left, from->top - top, from->right - left, from->bottom - top};
            morphFrom_.left = std::max(0L, morphFrom_.left); morphFrom_.top = std::max(0L, morphFrom_.top);
            morphRadius_ = static_cast<float>(OwnerPx(Panel::radius));
            slide_.Snap(1);
            if (!visible_) Warm(); // cheap when already warmed while the popover was open
            morph_.Snap(0);        // its clock starts once the hand-off is on screen (below)
        } else {
            morph_.Snap(1);
            view_.Preload(*items_);
            // Re-opening mid-exit continues from where the drawer is on screen.
            slide_.Set(1, now, Motion::Milliseconds(0), reduced ? Motion::Milliseconds(0) : DrawerMotion::enter, DrawerMotion::enterSpring);
        }
        closing_ = false; visible_ = true;
        // Hand-off without a blank frame. A window only keeps what it is painted
        // with while it is really on screen, so the drawer is shown in the
        // popover's shape *underneath* the popover (which still covers it), is
        // painted there, and DWM composes that frame. Only then is it activated,
        // which hides the popover: the drawer under it is already showing it.
        ApplyGeometry(now, true);
        SetWindowPos(hwnd_, under ? under : HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        PaintNow(); ValidateRect(hwnd_, nullptr); DwmFlush();
        if (under) SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        Activated();
        // The morph's clock starts now that its first frame is on screen: any time
        // spent above must never be skipped by the animation (that was a jump).
        now = Motion::Now();
        if (from && !reduced) morph_.Set(1, now, Motion::Milliseconds(0), DrawerMotion::morph, DrawerMotion::morphSpring);
        Repoint(); RefreshTips(); Ticker();
        if (hooks_.schedule) hooks_.schedule(now);
    }
    // Leaves along the path it came in on; `animate` false hides at once.
    void Dismiss(bool keyboard) override { Hide(keyboard, true); }
    void Hide(bool keyboard, bool animate) {
        if (!visible_ || closing_) { if (visible_ && !animate) Finish(); return; }
        hiddenAt = GetTickCount64(); ClearPointer();
        auto now = Motion::Now(); closing_ = true;
        slide_.Set(0, now, Motion::Milliseconds(0), animate && !motion_.ReducedMotion() ? DrawerMotion::exit : Motion::Milliseconds(0), DrawerMotion::exitSpring);
        if (!slide_.Moving(now)) Finish();
        if (hooks_.closed) hooks_.closed(keyboard);
        if (visible_ && hooks_.schedule) hooks_.schedule(now);
    }
    // Owner moved, resized or changed DPI.
    void Reposition() { if (visible_) ApplyGeometry(Motion::Now()); }
    void Changed() {
        if (!hwnd_ || !visible_) return;
        auto now = Motion::Now();
        auto order = FocusOrder(); if (focus_ && std::find(order.begin(), order.end(), focus_) == order.end()) focus_ = 0;
        scroll_.Snap(std::clamp(scroll_.Value(now), 0.f, MaxScroll(now, true)));
        RefreshTips(); Repoint(); Ticker();
    }
    void Frame(Motion::Time now) override {
        if (!visible_) return;
        if (morph_.Moving(now) || appliedMorph_ < 1000) {
            // Morph frame (and the one that lands it): paint the new layout first,
            // then reshape the region, so both reach the same composition. One
            // paint per frame: the pending invalidation is consumed here.
            PaintNow(); ValidateRect(hwnd_, nullptr); ApplyGeometry(now);
            if (closing_ && !slide_.Moving(now)) { Finish(); return; }
            if (!morph_.Moving(now)) RefreshTips();
            animatingPainted_ = false; return;
        }
        ApplyGeometry(now);
        if (closing_ && !slide_.Moving(now)) { Finish(); return; }
        Host::Frame(now);
    }
};
} // namespace DownloadsUI
} // namespace NativeChrome
