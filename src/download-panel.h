#pragma once
#include "i18n.h"
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
#include "drawer-corners.h"   // Mode Focus: the docked drawer's antialiased top corners
#include "resize-border.h"     // the window's resize border wins over the drawer on the window's edges
#include <d2d1_1.h>
#include <shellapi.h>
#include <commoncontrols.h>
#include <commctrl.h>
#include <array>
#include <functional>
#include <map>
#include <vector>
#include "chrome.h"
#include "native-ui.h"
#include "tooltip.h"
#include "downloads.h"
#include "download-thumbnails.h"   // image rows: the picture itself (src/download-file-kind.h)
#include "accessibility.h"

namespace NativeChrome {
namespace DownloadsUI {
using namespace Downloads;
// Hit identifiers inside a surface.
// No CloseHit: the surfaces have no header (Échap, a click outside or the toolbar button close them).
constexpr int ViewAllHit = 2, SectionBase = 10, ActionBase = 100000;
inline int SectionHit(Period period) { return SectionBase + static_cast<int>(period); }
inline bool IsSection(int hit) { return hit >= SectionBase && hit < SectionBase + PeriodCount; }
inline Period HitPeriod(int hit) { return static_cast<Period>(hit - SectionBase); }
// Per row three targets: its two action buttons (slots 0, 1) and the row itself (slot 2: a finished
// download's row opens the file, like its Open action). IsAction: any of them (the row's reveal follows).
inline int ActionHit(int item, int slot) { return ActionBase + item * 3 + slot; }
inline int RowHit(int item) { return ActionBase + item * 3 + 2; }
inline bool IsAction(int hit) { return hit >= ActionBase; }
inline int HitItem(int hit) { return (hit - ActionBase) / 3; }
inline int HitSlot(int hit) { return (hit - ActionBase) % 3; }
inline bool IsRow(int hit) { return IsAction(hit) && HitSlot(hit) == 2; }
// A row click (or Entrée on the focused row, or UIA Invoke) opens the file only once it is downloaded
// and still there; rows in progress, paused, failed, blocked or cancelled do nothing.
inline bool Openable(const Item& item) { return item.state == State::Completed && !item.missing; }
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
    float height = 0;               // Row: a held download's card height in the popover (0: a plain row)
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
    float edgeRight = -1;              // #143, docked on the left: the column's right edge (content DIP), the hairline left of it
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
    if (items.empty()) { slots.push_back({SlotKind::Empty, -1, Panel::firstRow - scroll}); y += Panel::tile; }   // where the popover shows it
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
// A held download shows as its card (HeldCard) when the caller measured one (cardHeight > 0).
inline bool ShowsCard(const Item& item, float cardHeight) { return cardHeight > 0 && item.held && item.state == State::InProgress; }
inline std::vector<Slot> PopoverSlots(const std::vector<Item>& items, float cardHeight = 0) {
    std::vector<Slot> slots;
    if (items.empty()) { slots.push_back({SlotKind::Empty, -1, Panel::RowTop(0)}); return slots; }
    const int rows = std::min<int>(Panel::popoverRows, static_cast<int>(items.size()));
    float top = Panel::firstRow, bottom = 0; bool previousCard = false;
    for (int i = 0; i < rows; ++i) {
        const bool card = ShowsCard(items[i], cardHeight);
        // Rows 16 apart, a card 20 from a row's tile (12 + the row's 8 inset in Paper), 12 between two cards.
        if (i > 0) top = bottom + (card && previousCard ? HeldCard::gap : card || previousCard ? HeldCard::around : Panel::rowGap);
        Slot slot{SlotKind::Row, i, top}; slot.height = card ? cardHeight : 0; slots.push_back(slot);
        bottom = top + (card ? cardHeight : Panel::tile); previousCard = card;
    }
    slots.push_back({SlotKind::ViewAll, -1, bottom + Panel::rowGap});
    return slots;
}
// Popover height for its slots: « View all » + its line + the bottom padding.
inline float PopoverHeight(const std::vector<Slot>& slots) {
    for (const auto& slot : slots) if (slot.kind == SlotKind::ViewAll) return slot.top + Panel::line + Panel::padding;
    return Panel::Height(1);
}

class View {
    Chrome& chrome_;
    ID2D1DeviceContext* dc_ = nullptr; ID2D1Effect* blur_ = nullptr;
    ID2D1BitmapRenderTarget* layer_ = nullptr; D2D1_SIZE_F layerSize_{};
    ID2D1GradientStopCollection* stops_ = nullptr; ID2D1LinearGradientBrush* fade_ = nullptr;
    std::map<std::wstring, ID2D1Bitmap*> icons_;
    // Image rows: each decoded thumbnail as a bitmap brush of this target (rebuilt when the cache's entry changes).
    struct ThumbBrush { std::shared_ptr<const Downloads::Thumbnails::Thumb> thumb; ID2D1Bitmap* bitmap = nullptr; ID2D1BitmapBrush* brush = nullptr; };
    std::map<Downloads::Thumbnails::Key, ThumbBrush> thumbs_;
    // Image / SVG rows: the file-type glyphs' geometries (factory resources).
    struct GlyphGeometry { bool parsed = false; GlyphShape shape; std::vector<ID2D1PathGeometry*> paths; };
    std::map<const char*, GlyphGeometry> glyphs_; ID2D1StrokeStyle* round_ = nullptr;
    template<class T> static void Drop(T*& p) { if (p) { p->Release(); p = nullptr; } }
    void DropThumbs() { for (auto& t : thumbs_) { Drop(t.second.brush); Drop(t.second.bitmap); } thumbs_.clear(); }
    void PushOpacity(float opacity) { dc_->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), opacity), nullptr); }
public:
    explicit View(Chrome& chrome) : chrome_(chrome) {}
    ~View() { Release(); }
    bool blurAvailable = true;
    void Release() {
        Drop(bodyLayout_); bodyText_.clear(); bodyHeight_ = 0; Drop(warning_);
        Drop(layer_); layerSize_ = {}; Drop(fade_); Drop(stops_); Drop(blur_); Drop(dc_);
        for (auto& icon : icons_) Drop(icon.second);
        icons_.clear(); DropThumbs();
        for (auto& g : glyphs_) for (auto*& p : g.second.paths) Drop(p);
        glyphs_.clear(); Drop(round_);
    }
    // The surface that receives ThumbnailMessage when a thumbnail it asked for is ready (none offscreen).
    HWND notify = nullptr;
    static constexpr UINT ThumbnailMessage = WM_APP + 62;
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
    // Shell icon for the file (FileKind::Other only: an image or an SVG shows its picture or glyph, never the
    // icon of the application associated with its type): the real file for executables/shortcuts once
    // complete (so an installer shows its own icon), otherwise its extension.
    ID2D1Bitmap* FileIcon(const Item& item) {
        auto* target = chrome_.RenderTarget(); auto* wic = chrome_.Imaging(); if (!target || !wic) return nullptr;
        std::wstring name = FileName(item.path), ext;
        const auto dot = name.find_last_of(L'.');
        if (dot != std::wstring::npos) ext = name.substr(dot);
        for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));
        const bool own = item.state == State::Completed && !item.missing && (ext == L".exe" || ext == L".lnk" || ext == L".msi");
        const std::wstring generic = ext.empty() ? L".file" : ext, key = own ? item.path : generic;
        auto found = icons_.find(key); if (found != icons_.end()) return found->second;
        ID2D1Bitmap* bitmap = nullptr;
        SHFILEINFOW info{};
        if (SHGetFileInfoW(key.c_str(), FILE_ATTRIBUTE_NORMAL, &info, sizeof(info), SHGFI_SYSICONINDEX | (own ? 0 : SHGFI_USEFILEATTRIBUTES)) ||
            (own && SHGetFileInfoW(generic.c_str(), FILE_ATTRIBUTE_NORMAL, &info, sizeof(info), SHGFI_SYSICONINDEX | SHGFI_USEFILEATTRIBUTES))) {
            static const IID imageList = {0x46eb5926, 0x582e, 0x4017, {0x9f, 0xdf, 0xe8, 0x99, 0x8d, 0xaa, 0x09, 0x50}};
            const float px = Panel::fileIcon * target->GetPixelSize().width / std::max(1.f, target->GetSize().width);
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
    void ForgetIcons() { for (auto& icon : icons_) Drop(icon.second); icons_.clear(); DropThumbs(); }
    // Shell icon lookups cost milliseconds: resolve them before a motion needs them. Image rows: the files
    // already decoded are checked again (changed on disk: decoded again), the first rows' are asked for now.
    void Preload(const std::vector<Item>& items) {
        if (!chrome_.RenderTarget()) return;
        int asked = 0;
        for (auto& item : items) {
            const auto kind = KindOf(item.path, item.mime);
            if (kind == FileKind::Other) { FileIcon(item); continue; }
            if (kind != FileKind::Image || !Openable(item)) continue;
            Downloads::Thumbnails::Service::Get().Recheck(item.path, notify, ThumbnailMessage);
            if (asked++ < 16) Thumbnail(item);
        }
    }
    // Pixels of the 24 DIP file icon on this target.
    int IconPixels() const {
        auto* target = chrome_.RenderTarget(); if (!target) return 0;
        return std::clamp(static_cast<int>(std::lround(Panel::fileIcon * target->GetPixelSize().width / std::max(1.f, target->GetSize().width))), 1, Downloads::Thumbnails::maxSide);
    }
    // A downloaded image's thumbnail (FileKind::Image, Openable); null while it is decoded or when it cannot be.
    ID2D1BitmapBrush* Thumbnail(const Item& item) {
        auto* target = chrome_.RenderTarget(); const int side = IconPixels(); if (!target || side <= 0) return nullptr;
        auto thumb = Downloads::Thumbnails::Service::Get().Lookup(item.path, side, notify, ThumbnailMessage);
        if (!thumb || !thumb->ok || thumb->pixels.side != side) return nullptr;
        auto& cached = thumbs_[{item.path, side}];
        if (cached.thumb != thumb) {
            Drop(cached.brush); Drop(cached.bitmap); cached.thumb = thumb;
            const auto props = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
            if (SUCCEEDED(target->CreateBitmap(D2D1::SizeU(side, side), thumb->pixels.bgra.data(), static_cast<UINT32>(side * 4), props, &cached.bitmap)) &&
                FAILED(target->CreateBitmapBrush(cached.bitmap, &cached.brush))) cached.brush = nullptr;
        }
        return cached.brush;
    }
    // Designer (2026-11-26): glyphs in solid #949494; thumbnails ringed inside by 1 px #FFFFFF14, so a dark
    // picture does not melt into the tile.
    static constexpr float fileGlyph = 0x94 / 255.f, thumbnailRing = 0x14 / 255.f;
    // The thumbnail in the file icon's box, its corners rounded like an inset element, its inner ring on the
    // physical pixel grid.
    void DrawThumbnail(ID2D1BitmapBrush* brush, D2D1_RECT_F box, float opacity) {
        auto* target = chrome_.RenderTarget(); if (!target || !brush) return;
        ID2D1Bitmap* bitmap = nullptr; brush->GetBitmap(&bitmap); if (!bitmap) return;
        const float k = (box.right - box.left) / std::max(1.f, bitmap->GetSize().width); bitmap->Release();   // DIP size (96 DPI bitmap: its pixels)
        brush->SetTransform(D2D1::Matrix3x2F::Scale(k, k) * D2D1::Matrix3x2F::Translation(box.left, box.top));
        brush->SetOpacity(opacity); brush->SetInterpolationMode(D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        target->FillRoundedRectangle(D2D1::RoundedRect(box, Design::Radius::inset, Design::Radius::inset), brush);
        auto* ring = chrome_.SolidBrush(); if (!ring) return;
        const float px = target->GetSize().width / std::max(1.f, static_cast<float>(target->GetPixelSize().width)), r = Design::Radius::inset - px / 2;
        ring->SetColor(D2D1::ColorF(1, 1, 1, thumbnailRing * opacity));
        target->DrawRoundedRectangle(D2D1::RoundedRect({box.left + px / 2, box.top + px / 2, box.right - px / 2, box.bottom - px / 2}, r, r), ring, px);
    }
    // A file-type glyph (src/download-file-kind.h) in `color`, `size` DIP centred in `box`.
    void Glyph(const char* svg, D2D1_RECT_F box, float size, D2D1_COLOR_F color) {
        auto* target = chrome_.RenderTarget(); auto* factory = chrome_.Factory(); auto* brush = chrome_.SolidBrush();
        if (!target || !factory || !brush || !svg || color.a <= 0) return;
        auto& g = glyphs_[svg];
        if (!g.parsed) { g.parsed = true; g.shape = ParseGlyph(svg); for (auto& p : g.shape.paths) g.paths.push_back(DialogKit::SvgGeometry(factory, p.d.c_str(), p.evenOdd)); }
        if (g.shape.view <= 0) return;
        if (!round_) factory->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND), nullptr, 0, &round_);
        D2D1_MATRIX_3X2_F base; target->GetTransform(&base);
        const float k = size / g.shape.view, x = (box.left + box.right - size) / 2, y = (box.top + box.bottom - size) / 2;
        target->SetTransform(D2D1::Matrix3x2F::Scale(k, k) * D2D1::Matrix3x2F::Translation(x, y) * base);
        brush->SetColor(color);
        for (size_t i = 0; i < g.paths.size(); ++i) {
            if (!g.paths[i]) continue;
            if (g.shape.paths[i].fill) target->FillGeometry(g.paths[i], brush);
            if (g.shape.paths[i].stroke) target->DrawGeometry(g.paths[i], brush, g.shape.strokeWidth, round_);
        }
        target->SetTransform(base);
    }

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
    // ---- Held download (Paper B07b, variant A) ----
    static constexpr D2D1_COLOR_F Amber(float alpha = 1) { return {0xE6 / 255.f, 0xAC / 255.f, 0x3D / 255.f, alpha}; }   // --color-profile-ambre
    IDWriteTextLayout* bodyLayout_ = nullptr; std::wstring bodyText_; float bodyHeight_ = 0;
    ID2D1PathGeometry* warning_ = nullptr;
    // Phosphor « Warning » (bold), the Paper frame's own path: drawn as a vector so the amber is full strength (the
    // bundled glyph bitmaps carry the toolbar's 50 % alpha).
    void Warning(D2D1_RECT_F box, float size, D2D1_COLOR_F color) {
        static constexpr const char* path = "M240.26,186.1,152.81,34.23h0a28.74,28.74,0,0,0-49.62,0L15.74,186.1a27.45,27.45,0,0,0,0,27.71A28.31,28.31,0,0,0,40.55,228h174.9a28.31,28.31,0,0,0,24.79-14.19A27.45,27.45,0,0,0,240.26,186.1Zm-20.8,15.7a4.46,4.46,0,0,1-4,2.2H40.55a4.46,4.46,0,0,1-4-2.2,3.56,3.56,0,0,1,0-3.73L124,46.2a4.77,4.77,0,0,1,8,0l87.44,151.87A3.56,3.56,0,0,1,219.46,201.8ZM116,136V104a12,12,0,0,1,24,0v32a12,12,0,0,1-24,0Zm28,40a16,16,0,1,1-16-16A16,16,0,0,1,144,176Z";
        if (!warning_) warning_ = DialogKit::SvgGeometry(chrome_.Factory(), path);
        auto* t = chrome_.RenderTarget(); if (!warning_ || !t) return;
        D2D1_MATRIX_3X2_F base; t->GetTransform(&base);
        const float x = (box.left + box.right - size) / 2, y = (box.top + box.bottom - size) / 2;
        t->SetTransform(D2D1::Matrix3x2F::Scale(size / 256.f, size / 256.f) * D2D1::Matrix3x2F::Translation(x, y) * base);
        chrome_.SolidBrush()->SetColor(color); t->FillGeometry(warning_, chrome_.SolidBrush());
        t->SetTransform(base);
    }
    // The body paragraph wrapped in the card's inner width, on CSS 18 DIP lines (half-leading above each line).
    IDWriteTextLayout* Body() {
        const std::wstring text = TR(downloads_held_body);
        if (bodyLayout_ && text == bodyText_) return bodyLayout_;
        Drop(bodyLayout_); bodyText_ = text; bodyHeight_ = 0;
        TextMeasure m; IDWriteTextLayout* layout = nullptr;
        if (FAILED(chrome_.MakeTextLayout(text, {0, 0, HeldCard::innerWidth, HeldCard::bodyLine}, Panel::detailText, false, false, 0, &layout, m)) || !layout) return nullptr;
        DWRITE_TRIMMING none{DWRITE_TRIMMING_GRANULARITY_NONE, 0, 0}; layout->SetTrimming(&none, nullptr);
        layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP); layout->SetMaxWidth(HeldCard::innerWidth); layout->SetMaxHeight(4096);
        DWRITE_LINE_METRICS natural{}; UINT32 count = 0; layout->GetLineMetrics(&natural, 1, &count);
        layout->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, HeldCard::bodyLine, (HeldCard::bodyLine - natural.height) / 2 + natural.baseline);
        DWRITE_TEXT_METRICS metrics{}; layout->GetMetrics(&metrics);
        bodyHeight_ = metrics.lineCount * HeldCard::bodyLine; bodyLayout_ = layout; return layout;
    }
    float CardHeight() { return Body() ? HeldCard::Height(bodyHeight_) : 0; }
    void CardButton(int slot, Design::Box b, int hit, const Scene& scene) {
        const auto v = Interaction(scene, hit); const D2D1_RECT_F r{b.left, b.top, b.right, b.bottom};
        auto* t = chrome_.RenderTarget(); auto* brush = chrome_.SolidBrush();
        // « Supprimer »: filled #FFFFFF1A; « Conserver »: ghost. Hover lifts the plate, press dims it.
        const float plate = (slot == 0 ? .102f : 0) + .06f * v.hover - .03f * v.press;
        if (plate > 0) { chrome_.Color(1, plate); t->FillRoundedRectangle(D2D1::RoundedRect(r, HeldCard::radius, HeldCard::radius), brush); }
        const float label = slot == 0 ? 1.f : .6f + .2f * v.hover;
        const std::wstring text = slot == 0 ? TR(downloads_action_discard) : TR(downloads_action_keep);
        const float w = std::min(r.right - r.left, chrome_.TextWidth(text, Panel::detailText, true, false, 0)), x = (r.left + r.right - w) / 2;   // centred
        chrome_.Text(text, {x, r.top, x + w + 1, r.bottom}, Panel::detailText, label * (1 - .15f * v.press), false, true, 0);
        if (scene.focus == hit) chrome_.FocusRing(r, HeldCard::radius);
    }
    void Card(const Item& item, float top, float height, const Scene& scene) {
        auto* t = chrome_.RenderTarget(); auto* brush = chrome_.SolidBrush();
        const auto c = HeldCard::Card(top, height); const D2D1_RECT_F card{c.left, c.top, c.right, c.bottom};
        brush->SetColor(Amber(0x14 / 255.f)); t->FillRoundedRectangle(D2D1::RoundedRect(card, HeldCard::radius, HeldCard::radius), brush);
        // 1 px inset ring (box-shadow inset), on the physical pixel grid.
        const float px = t->GetSize().width / std::max(1.f, static_cast<float>(t->GetPixelSize().width));
        brush->SetColor(Amber(0x33 / 255.f)); t->DrawRoundedRectangle(D2D1::RoundedRect({card.left + px / 2, card.top + px / 2, card.right - px / 2, card.bottom - px / 2}, HeldCard::radius - px / 2, HeldCard::radius - px / 2), brush, px);
        const auto tb = HeldCard::Tile(top); const D2D1_RECT_F tile{tb.left, tb.top, tb.right, tb.bottom};
        brush->SetColor(Amber(0x26 / 255.f)); t->FillRoundedRectangle(D2D1::RoundedRect(tile, HeldCard::radius, HeldCard::radius), brush);
        Warning(tile, HeldCard::glyph, Amber());
        const auto n = HeldCard::Name(top); chrome_.Text(FileName(item.path), {n.left, n.top, n.right, n.bottom}, 16, 1, false, true, -.16f);
        const auto st = HeldCard::Status(top); const auto ink = chrome_.ink; chrome_.ink = Amber();
        chrome_.Text(TRF(downloads_held_status, Bytes(item.total > 0 ? item.total : item.received, 0)), {st.left, st.top, st.right, st.bottom}, Panel::detailText, 1, false, false, 0);
        chrome_.ink = ink;
        if (auto* body = Body()) { brush->SetColor(D2D1::ColorF(1, 1, 1, 0xB3 / 255.f)); t->DrawTextLayout({HeldCard::left + HeldCard::inset, HeldCard::BodyTop(top)}, body, brush, D2D1_DRAW_TEXT_OPTIONS_NONE); }
        for (int slot = 0; slot < 2; ++slot) CardButton(slot, HeldCard::Button(top, height, slot), ActionHit(item.id, slot), scene);
    }
    void Detail(const Item& item, float top, std::int64_t nowMs) {
        const float y = top + Panel::line, muted = Design::Text::muted;
        float x = Panel::textLeft;
        switch (item.state) {
        case State::InProgress: {
            if (item.held) {   // DL-V5: warning glyph + « À confirmer », amber at full opacity
                Warning({x, y, x + Panel::detailIcon, y + Panel::line}, Panel::detailIcon, Amber());
                x += Panel::detailIcon + Panel::iconGap; const auto ink = chrome_.ink; chrome_.ink = Amber();
                const std::wstring text = TR(downloads_state_held); const float w = chrome_.TextWidth(text, Panel::detailText, false, true, -.14f);
                chrome_.Text(text, {x, y, std::min(x + w + 2, Panel::width - Panel::padding), y + Panel::line}, Panel::detailText, 1, true, false, -.14f);
                chrome_.ink = ink; break;
            }
            x = Run(L"arrow-down", Bytes(item.received, item.total), x, y, muted);
            if (item.endTime > 0) {
                const auto left = std::max<std::int64_t>(0, (item.endTime - nowMs + 999) / 1000);
                Run(L"timer", Remaining(left), x + Panel::groupGap, y, muted);
            }
            break;
        }
        case State::Paused: x = Run(L"detail-pause", Bytes(item.received, item.total), x, y, muted); Run(nullptr, TR(downloads_state_paused), x + Panel::groupGap, y, muted); break;
        case State::Completed: Run(item.missing ? L"warning" : L"check-circle", item.missing ? std::wstring(TR(downloads_state_file_deleted)) : FolderLabel(item.path), x, y, muted); break;
        case State::Failed: Run(L"warning", FailureText(item.reason), x, y, muted); break;
        case State::Cancelled: Run(L"detail-close", TR(downloads_state_canceled), x, y, muted); break;
        }
    }
    Motion::InteractionVisual Interaction(const Scene& scene, int hit) const { return scene.motion ? scene.motion->Visual(hit, scene.now) : Motion::InteractionVisual{}; }
    void ActionButton(Action action, D2D1_RECT_F box, int hit, const Scene& scene) {
        const auto* icon = IconFor(action); if (!icon) return;
        auto v = Interaction(scene, hit);
        if (action == Action::ShowInFolder) { // closed folder that opens under the pointer (or keyboard focus)
            const auto style = Design::Resolve(Design::ButtonVariant::Ghost, {v.hover, v.press, false, scene.focus == hit});
            if (style.fill) chrome_.Round(box, style.radius, style.surface);
            if (style.focus) chrome_.FocusRing(box, style.radius);
            const bool reduced = scene.motion && scene.motion->ReducedMotion();
            chrome_.FolderGlyph(box, style.iconSize * Motion::PressScale(v.press, reduced), style.iconOpacity, scene.focus == hit ? 1.f : v.hover, reduced);
            return;
        }
        chrome_.DrawButton(icon, box, Design::ButtonVariant::Ghost, {v.hover, v.press, false, scene.focus == hit});
    }
    void Row(const Item& item, float top, const Scene& scene) {
        auto* target = chrome_.RenderTarget(); auto* brush = chrome_.SolidBrush();
        const auto tile = Panel::Tile(top);
        chrome_.Round({tile.left, tile.top, tile.right, tile.bottom}, Design::Radius::control, Design::Surface::raised);
        {
            const float s = Panel::fileIcon, x = (tile.left + tile.right - s) / 2, y = (tile.top + tile.bottom - s) / 2;
            const D2D1_RECT_F box = D2D1::RectF(x, y, x + s, y + s);
            const float fade = item.state == State::Completed && !item.missing ? 1.f : item.held ? .8f : .5f;
            const auto kind = KindOf(item.path, item.mime);
            if (kind == FileKind::Other) {
                if (auto* icon = FileIcon(item)) {
                    if (dc_) dc_->DrawBitmap(icon, box, fade, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, nullptr, nullptr);
                    else target->DrawBitmap(icon, box, fade);
                }
            } else if (auto* picture = kind == FileKind::Image && Openable(item) ? Thumbnail(item) : nullptr) DrawThumbnail(picture, box, fade);
            else Glyph(GlyphFor(kind), box, s, D2D1::ColorF(fileGlyph, fileGlyph, fileGlyph, fade));   // solid #949494 (faded like the row)
        }
        const auto name = Panel::Name(top);
        // A held row keeps its name readable (#FFFFFFE6 at the rows' .9): it waits for a decision.
        chrome_.Text(FileName(item.path), {name.left, name.top, name.right, name.bottom}, Panel::line, item.held ? .9f * .9f : Design::Text::muted, false, true, -.16f);
        // Keyboard focus on a finished row (Entrée opens it): the ring around the tile + text band, 6 DIP out.
        if (scene.focus == RowHit(item.id)) chrome_.FocusRing({Panel::padding - 6, top - 6, Panel::width - Panel::padding + 6, top + Panel::tile + 6}, Design::Radius::control + 6);
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
    float ViewAllWidth() { return chrome_.TextWidth(TR(downloads_view_all), Panel::line, false, false, -.16f); }
    void ViewAll(float top, const Scene& scene) {
        const auto v = Interaction(scene, ViewAllHit);
        const float w = ViewAllWidth();
        // Text link: brightens on hover, dims while pressed.
        const float opacity = (Design::Text::muted + (.78f - Design::Text::muted) * v.hover) * (1 - .25f * v.press);
        chrome_.Text(TR(downloads_view_all), {Panel::padding, top, Panel::padding + w + 2, top + Panel::line}, Panel::line, opacity, false, false, -.16f);
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

        const auto& items = *scene.items;
        const float viewBottom = std::min(scene.viewBottom, height);
        target->PushAxisAlignedClip(D2D1::RectF(0, scene.viewTop, Panel::width, viewBottom), D2D1_ANTIALIAS_MODE_ALIASED);
        for (const auto& slot : slots) {
            switch (slot.kind) {
            case SlotKind::Empty:
                chrome_.Text(TR(downloads_empty), {Panel::padding, slot.top, Panel::width - Panel::padding, slot.top + Panel::tile}, Panel::line, Design::Text::muted, false, false, -.16f);
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
                if (slot.height > 0) Card(items[slot.index], slot.top, slot.height, scene); else Row(items[slot.index], slot.top, scene);
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
            const float x = scene.edgeRight >= 0 ? scene.edgeRight - px : 0.f;   // the page's side
            chrome_.Color(1, Popup::border * scene.edgeOpacity); target->FillRectangle(D2D1::RectF(x, 0, x + px, height), brush);
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
    HWND hwnd_ = nullptr, owner_ = nullptr;
    Tooltip::Tools tip_;
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
    virtual void KeyOut(Motion::Time) {}          // paints what the frame must not show in the key colour (the drawer, NativeUI::KeyedDrawer)
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
        const auto scene = MakeScene(Motion::Now());
        if (!items_ || y < scene.viewTop || y >= scene.viewBottom) return 0;
        for (const auto& slot : Slots(scene.now)) {
            if (slot.kind == SlotKind::Section) {
                auto h = DrawerLayout::Header(slot.top);
                if (y >= h.top && y < h.bottom) return SectionHit(static_cast<Period>(slot.index));
            } else if (slot.kind == SlotKind::ViewAll) {
                auto b = Panel::ViewAll(slot.top, const_cast<View&>(view_).ViewAllWidth());
                if (x >= b.left && x < b.right && y >= b.top && y < b.bottom) return ViewAllHit;
            } else if (slot.kind == SlotKind::Row && slot.opacity > .5f && slot.height > 0) {
                const auto card = HeldCard::Card(slot.top, slot.height);
                if (y < card.top || y >= card.bottom || x < card.left || x >= card.right) continue;
                const auto& entry = (*items_)[slot.index]; item = entry.id;
                for (int s = 0; s < 2; ++s) { auto box = HeldCard::Button(slot.top, slot.height, s); if (x >= box.left && x < box.right && y >= box.top && y < box.bottom) return ActionHit(entry.id, s); }
                return 0;
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
                return Openable(entry) ? RowHit(entry.id) : 0;   // anywhere else on a finished row: open it
            }
        }
        return 0;
    }
    // A row shows its actions while hovered or holding keyboard focus. A download in progress or paused shows its
    // Pause / Resume and Cancel at all times (user report 2026-11-14: an active download looked un-cancellable while
    // the pointer was elsewhere); the name keeps its mask, the hit lanes are the same buttons.
    static bool Active(const Item& item) { return (item.state == State::InProgress && !item.held) || item.state == State::Paused; }   // a held row (DL-V5) reveals on hover
    bool ActiveId(int id) const { if (!items_) return false; for (const auto& i : *items_) if (i.id == id) return Active(i); return false; }
    void UpdateReveals(Motion::Time now) {
        const bool reduced = motion_.ReducedMotion();
        const int focusId = IsAction(focus_) ? HitItem(focus_) : -1;
        if (hoverItem_ >= 0) reveals_.try_emplace(hoverItem_);
        if (focusId >= 0) reveals_.try_emplace(focusId);
        if (items_) for (const auto& i : *items_) if (Active(i)) reveals_.try_emplace(i.id);
        for (auto it = reveals_.begin(); it != reveals_.end();) {
            const bool shown = it->first == hoverItem_ || it->first == focusId || ActiveId(it->first);
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
        std::vector<int> order;
        if (!items_) return order;
        for (const auto& slot : Slots(Motion::Now())) {
            if (slot.kind == SlotKind::Section) order.push_back(SectionHit(static_cast<Period>(slot.index)));
            else if (slot.kind == SlotKind::ViewAll) order.push_back(ViewAllHit);
            else if (slot.kind == SlotKind::Row && slot.opacity > .5f) {
                const auto& item = (*items_)[slot.index]; auto a = ActionsFor(item);
                if (Openable(item)) order.push_back(RowHit(item.id));
                if (a.first != Action::None) order.push_back(ActionHit(item.id, 0));
                if (a.second != Action::None) order.push_back(ActionHit(item.id, 1));
            }
        }
        return order;
    }
    int IndexOf(int id) const { if (!items_) return -1; for (size_t i = 0; i < items_->size(); ++i) if ((*items_)[i].id == id) return static_cast<int>(i); return -1; }
    void Activate(int hit, bool keyboard) {
        if (ActivateOther(hit, keyboard)) return;
        if (!IsAction(hit) || !hooks_.action) return;
        const int index = IndexOf(HitItem(hit)); if (index < 0) return;
        if (IsRow(hit)) { if (Openable((*items_)[index])) hooks_.action(HitItem(hit), Action::Open); return; }
        const auto actions = ActionsFor((*items_)[index]);
        const Action action = HitSlot(hit) == 0 ? actions.first : actions.second;
        if (action != Action::None) hooks_.action(HitItem(hit), action);
    }
    // Icon-only buttons carry tooltips; tools follow the laid-out rows.
    void RefreshTips() {
        if (!tip_) return;
        tip_.Clear();
        const auto origin = Origin(Motion::Now());
        auto add = [&](UINT_PTR id, Design::Box b, const wchar_t* text) {
            tip_.Add(id, {Px(b.left + origin.x), Px(b.top + origin.y), Px(b.right + origin.x), Px(b.bottom + origin.y)}, text);
        };
        if (!items_) return;
        const auto scene = MakeScene(Motion::Now());
        for (const auto& slot : Slots(scene.now)) {
            if (slot.kind == SlotKind::Section) {
                if (slot.top >= scene.viewTop && slot.top + DrawerLayout::sectionHeight <= scene.viewBottom)
                    add(static_cast<UINT_PTR>(SectionHit(static_cast<Period>(slot.index))), DrawerLayout::Caret(slot.top), slot.open > .5f ? TR(common_collapse) : TR(common_expand));
                continue;
            }
            if (slot.kind != SlotKind::Row || slot.height > 0 || slot.opacity < .99f || slot.top < scene.viewTop || slot.top + Panel::tile > scene.viewBottom) continue;   // a card's buttons carry their label
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
        view_.Paint(scene, Slots(scene.now), SurfaceHeight()); KeyOut(scene.now);
        if (target->EndDraw() == D2DERR_RECREATE_TARGET) { view_.Release(); chrome_.RecreateTarget(); }
    }
    // ---- UI Automation (the actions were reachable by pointer / keyboard only) ----------------------------------
    static constexpr UINT InvokeMessage = WM_APP + 61;
    static constexpr long long RowKeyBase = 1000000000LL;
    static std::wstring StatusText(const Item& item) {
        switch (item.state) {
        case State::InProgress: return item.held ? TRF(downloads_held_status, Bytes(item.total > 0 ? item.total : item.received, 0)) + L". " + TR(downloads_held_body) : Bytes(item.received, item.total);
        case State::Paused: return TR(downloads_state_paused);
        case State::Completed: return item.missing ? TR(downloads_state_file_deleted) : TR(downloads_state_completed);
        case State::Failed: return FailureText(item.reason);
        case State::Cancelled: return TR(downloads_state_canceled);
        }
        return L"";
    }
    static const wchar_t* ActionId(Action a) {
        switch (a) { case Action::Pause: return L"DownloadPause"; case Action::Resume: return L"DownloadResume"; case Action::Cancel: return L"DownloadCancel";
        case Action::Retry: return L"DownloadRetry"; case Action::Remove: return L"DownloadRemove"; case Action::ShowInFolder: return L"DownloadShowInFolder"; case Action::Open: return L"DownloadOpen";
        case Action::Discard: return L"DownloadDiscard"; case Action::Keep: return L"DownloadKeep"; default: return L""; }
    }
public:
    // The accessible tree of the surface (screen pixels), in reading order. Public: tests read it.
    std::vector<Accessibility::Node> AccessibleNodes() const {
        std::vector<Accessibility::Node> out; if (!hwnd_ || !items_) return out;
        RECT w{}; GetWindowRect(hwnd_, &w); const float k = Dpi() / 96.f; const auto origin = Origin(Motion::Now());
        auto screen = [&](Design::Box b) { return RECT{w.left + static_cast<LONG>((b.left + origin.x) * k), w.top + static_cast<LONG>((b.top + origin.y) * k), w.left + static_cast<LONG>((b.right + origin.x) * k), w.top + static_cast<LONG>((b.bottom + origin.y) * k)}; };
        Accessibility::Node list; list.key = 3; list.role = Accessibility::Role::List; list.name = TR(downloads_history_title); list.automationId = L"DownloadList"; list.focusable = false; list.bounds = w; out.push_back(list);
        const auto scene = MakeScene(Motion::Now()); int position = 0, count = 0;
        const auto slots = Slots(scene.now);
        for (const auto& slot : slots) if (slot.kind == SlotKind::Row && slot.opacity > .5f) ++count;
        for (const auto& slot : slots) {
            if (slot.kind == SlotKind::ViewAll) { Accessibility::Node v; v.key = ViewAllHit; v.role = Accessibility::Role::Button; v.name = TR(downloads_view_all); v.automationId = L"DownloadsViewAll"; v.bounds = screen(Panel::ViewAll(slot.top, const_cast<View&>(view_).ViewAllWidth())); out.push_back(v); continue; }
            if (slot.kind != SlotKind::Row || slot.opacity <= .5f || slot.index < 0 || slot.index >= static_cast<int>(items_->size())) continue;
            const auto& item = (*items_)[slot.index];
            Accessibility::Node row; row.key = RowKeyBase + item.id; row.parent = 3; row.role = Accessibility::Role::ListItem; row.name = FileName(item.path); row.status = StatusText(item);
            row.automationId = L"DownloadItem"; row.position = ++position; row.count = count; row.focusable = Openable(item); row.bounds = screen(slot.height > 0 ? HeldCard::Card(slot.top, slot.height) : Panel::Hover(slot.top));
            row.offscreen = slot.top + Panel::tile < scene.viewTop || slot.top > scene.viewBottom; out.push_back(row);
            const auto actions = ActionsFor(item);
            for (int sl = 0; sl < 2; ++sl) {
                const Action a = sl == 0 ? actions.first : actions.second; if (a == Action::None) continue;
                Accessibility::Node b; b.key = ActionHit(item.id, sl); b.parent = row.key; b.role = Accessibility::Role::Button; b.name = TipFor(a); b.help = FileName(item.path);
                b.automationId = ActionId(a); b.bounds = screen(slot.height > 0 ? HeldCard::Button(slot.top, slot.height, sl) : Panel::ActionBox(slot.top, sl)); b.offscreen = row.offscreen; out.push_back(b);
            }
        }
        return out;
    }
protected:
    Accessibility::Bridge uia_;
    void AttachAccessibility() {
        Accessibility::Host host; host.window = hwnd_;
        host.nodes = [this] { return AccessibleNodes(); };
        host.focused = [this]() -> long long { return visible_ && GetFocus() == hwnd_ && keyboard_ ? UiaKey(focus_) : 0; };
        host.invoke = [this](long long key) { if (const int hit = HitOfKey(key)) PostMessageW(hwnd_, InvokeMessage, static_cast<WPARAM>(hit), 0); };
        host.focus = [this](long long key) { if (const int hit = HitOfKey(key)) PostMessageW(hwnd_, InvokeMessage, static_cast<WPARAM>(hit), 1); };
        uia_.Attach(std::move(host));
    }
    // A row's node key is RowKeyBase + id; its hit is RowHit(id). Other nodes use their hit as key.
    static long long UiaKey(int hit) { return IsRow(hit) ? RowKeyBase + HitItem(hit) : hit; }
    static int HitOfKey(long long key) { return key >= RowKeyBase ? RowHit(static_cast<int>(key - RowKeyBase)) : key > 0 ? static_cast<int>(key) : 0; }
    void ClearPointer() { if (GetCapture() == hwnd_) ReleaseCapture(); reveals_.clear(); motion_.ClearInteractions(); hoverItem_ = -1; hover_ = pressed_ = 0; tracking_ = false; }
    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<Host*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (msg == WM_NCCREATE) { self = static_cast<Host*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); self->hwnd_ = h; }
        { LRESULT border = 0; if (NativeChrome::ResizeBorder::Claim(h, msg, w, l, border)) return border; }   // flush with the window's right edge and bottom: its resize border wins there (src/resize-border.h)
        return self ? self->Handle(h, msg, w, l) : DefWindowProcW(h, msg, w, l);
    }
    virtual LRESULT Handle(HWND h, UINT msg, WPARAM w, LPARAM l) {
        const float scale = 96.f / Dpi();
        switch (msg) {
        case WM_ERASEBKGND: return 1;
        case View::ThumbnailMessage: Invalidate(); return 0;   // an image row's thumbnail is ready
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
            if ((w == VK_TAB || w == VK_DOWN || w == VK_UP) && order.empty()) return 0;
            if (w == VK_TAB || w == VK_DOWN || w == VK_UP) {
                keyboard_ = true; const bool back = w == VK_UP || (w == VK_TAB && GetKeyState(VK_SHIFT) < 0);
                auto it = std::find(order.begin(), order.end(), focus_);
                int index = it == order.end() ? (back ? static_cast<int>(order.size()) - 1 : 0) : static_cast<int>(it - order.begin()) + (back ? -1 : 1);
                index = (index + static_cast<int>(order.size())) % static_cast<int>(order.size());
                focus_ = order[index]; Reveal(focus_); Interact(hover_, pressed_); if (uia_.Listening()) uia_.Raise(UiaKey(focus_), UIA_AutomationFocusChangedEventId); return 0;
            }
            if ((w == VK_RETURN || w == VK_SPACE) && focus_) { keyboard_ = true; Activate(focus_, true); return 0; }
            return 0;
        }
        case WM_TIMER: if (w == TickTimer) { Invalidate(); Ticker(); } return 0;
        case WM_GETOBJECT: { LRESULT result; if (uia_.OnGetObject(h, w, l, result)) return result; break; }
        case InvokeMessage: {   // UIA Invoke (lParam 0) or SetFocus (1): the same paths as Entrée / Tab
            const int hit = static_cast<int>(w); auto order = FocusOrder();
            if (std::find(order.begin(), order.end(), hit) == order.end()) return 0;
            keyboard_ = true; focus_ = hit; Reveal(focus_); Interact(hover_, pressed_);
            if (l == 0) Activate(hit, true);
            return 0;
        }
        case WM_DESTROY: KillTimer(h, TickTimer); return 0;
        }
        return DefWindowProcW(h, msg, w, l);
    }
    bool CreateSurface(HINSTANCE instance, HWND owner, const std::wstring& root, std::vector<Item>* items, Hooks hooks, UINT classStyle, float width, float height) {
        owner_ = owner; items_ = items; hooks_ = std::move(hooks); motion_.EnablePolish(); chrome_.SetMotion(&motion_);
        chrome_.textCacheLimit = 1024; // every row's name/detail runs + labels, at any history size
        WNDCLASSW wc{}; wc.style = classStyle; wc.lpfnWndProc = Proc; wc.hInstance = instance; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.lpszClassName = ClassName();
        RegisterClassW(&wc);
        CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, TR(downloads_history_title), WS_POPUP, 0, 0, Px(width), Px(height), owner, nullptr, instance, this);
        if (!hwnd_) return false;
        view_.notify = hwnd_;
        tip_.Create(instance, hwnd_, root);
        Configure();
        AttachAccessibility();
        return SUCCEEDED(chrome_.Init(hwnd_, root));
    }
    void Activated() { ShowWindow(hwnd_, SW_SHOW); SetForegroundWindow(hwnd_); SetFocus(hwnd_); }
public:
    // Recently closed by deactivation: the click that caused it must not reopen it.
    ULONGLONG hiddenAt = 0;
    // Tests: shown far off screen WITHOUT activation (never takes the user's focus), reveals settled.
    void ShowForTest(float width, float height) {
        if (!hwnd_) {return;} visible_ = true; motion_.SetReducedMotion(true);
        SetWindowPos(hwnd_, HWND_BOTTOM, -12000, -12000, Px(width), Px(height), SWP_NOACTIVATE | SWP_SHOWWINDOW);
        chrome_.Resize(); UpdateReveals(Motion::Now()); RefreshTips();
    }
    bool RevealShownForTest(int id) const { auto it = reveals_.find(id); return it != reveals_.end() && it->second.Shown(); }
    void ModelChangedForTest() { UpdateReveals(Motion::Now()); }
    static constexpr UINT InvokeMessageForTest() { return InvokeMessage; }
    static constexpr long long RowKeyBaseForTest() { return RowKeyBase; }
    std::vector<int> FocusOrderForTest() const { return FocusOrder(); }
    int FocusForTest() const { return focus_; }
    // UIA Invoke / SetFocus on a node, as the provider calls them (posted, then pumped by the caller).
    void InvokeNodeForTest(long long key) { if (const int hit = HitOfKey(key)) SendMessageW(hwnd_, InvokeMessage, static_cast<WPARAM>(hit), 0); }
    virtual ~Host() { Dispose(); }
    // Release Direct2D/WIC before CoUninitialize; the owner destroys the window.
    void Dispose() { uia_.Detach(); view_.Release(); chrome_.Dispose(); tip_.Dispose(); hwnd_ = nullptr; visible_ = false; }
    HWND Window() const { return hwnd_; }
    bool Visible() const { return visible_; }
    void Invalidate() { if (hwnd_ && visible_) InvalidateRect(hwnd_, nullptr, FALSE); }
    // Live preference change while open (Show() re-reads it otherwise).
    void SetReducedMotion(bool reduced) { motion_.SetReducedMotion(reduced); Invalidate(); }
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
    float anchorLeft_ = -1, anchorBottom_ = 0;   // #127: opened from the column, left / bottom edges (screen px)
    int Rows() const { return items_ ? std::min<int>(Panel::popoverRows, static_cast<int>(items_->size())) : 0; }
    float CardHeight() const { return const_cast<View&>(view_).CardHeight(); }
    float Height() const { if (!items_ || items_->empty()) return Panel::Height(Rows(), false); return PopoverHeight(Slots(Motion::Time{})); }
    std::vector<Slot> Slots(Motion::Time) const override { static const std::vector<Item> none; return PopoverSlots(items_ ? *items_ : none, CardHeight()); }
    float SurfaceHeight() const override { return Height(); }
    void Decorate(Scene& scene) const override {
        scene.entrance = entrance_.Value(scene.now); scene.viewBottom = Height() - Panel::padding / 2;
        // The hairline DWM used to draw, on the region's rounded outline.
        scene.frame = {0, 0, Panel::width, Height()}; scene.frameRadius = Panel::radius; scene.frameOpacity = 1;
    }
    Motion::Wake ExtraWake(Motion::Time now) const override { return entrance_.Moving(now) ? Motion::Frame() : Motion::Wake{}; }
    bool ActivateOther(int hit, bool) override { if (hit != ViewAllHit) return false; if (hooks_.viewAll) hooks_.viewAll(); return true; }
    const wchar_t* ClassName() const override { return L"LisiereDownloads"; }
    void Configure() override {
        // A region rounds the corners (no DWM rounding: it always casts a shadow); the hairline is painted (Decorate).
        NativeUI::ConfigureShadowlessPopup(hwnd_, Panel::radius);
    }
    void Place() {
        if (!hwnd_) return;
        const int w = Px(Panel::width), h = Px(Height());
        const int x = anchorLeft_ >= 0 ? static_cast<int>(std::lround(anchorLeft_)) : static_cast<int>(std::lround(anchorRight_)) - w;
        const int y = anchorLeft_ >= 0 ? static_cast<int>(std::lround(anchorBottom_)) - h : static_cast<int>(std::lround(anchorTop_));
        SetWindowPos(hwnd_, HWND_TOP, x, y, w, h, SWP_NOACTIVATE);
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
        motion_.SetReducedMotion(Motion::PreferredReducedMotion());
        anchorRight_ = static_cast<float>(anchor.right); anchorTop_ = anchor.bottom + Px(Panel::anchorGap); anchorLeft_ = -1;
        Open();
    }
    // From the column (#127): left edge and bottom edge in screen pixels (above the button, or right of the column).
    void ShowFrom(float left, float bottom) {
        if (!hwnd_) return;
        anchorLeft_ = left; anchorBottom_ = bottom;
        Open();
    }
    void Open() {
        motion_.SetReducedMotion(Motion::PreferredReducedMotion());
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
inline std::vector<Slot> MorphSlots(const std::vector<Item>& items, const std::vector<Slot>& drawer, float t, float cardHeight = 0) {
    const auto popover = PopoverSlots(items, cardHeight);
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

// Docked drawer: the full history grouped by period. Two motions, both cheap,
// both one Direct2D present per display frame on a window that neither moves
// nor changes its region while they run (NativeUI::KeyedDrawer):
//  - Morph (from "View all"): the window sits at its final place and size; the
//    popover's shape grows into the drawer while the content is re-laid out.
//    What the shape does not cover yet is painted in the key colour (KeyOut).
//  - Slide (close, Escape, toolbar button): the content is drawn further right
//    each frame, the strip it has not entered keyed out.
// Why the morph no longer grows a window region (user report 2026-11-23: small
// flashes at the edges while the popover grows): a present into this layered
// window only reaches the pixels inside the region in force when it presents.
// Painting first and growing the region after left, every frame, the strip just
// added holding what the window showed there before (black on a new surface,
// earlier drawer pixels) until the next frame painted it; DWM showed it in between.
// #143: « Tout afficher » morphs the popover into the drawer only when the popover lies over the drawer's window (`local`:
// its rectangle in that window's pixels, `width` the window's): at least 90 % of its width inside. Elsewhere (the side
// panel's complete column, whose popover sits over the column; the top-right one in side Focus) the drawer slides in.
inline bool MorphFits(const RECT& local, LONG width) {
    const LONG w = local.right - local.left; if (w <= 0 || width <= 0) return false;
    const LONG inside = std::min(local.right, width) - std::max(local.left, 0L);
    return inside * 10 >= w * 9;
}
class Drawer final : public Host {
    Lane slide_, scroll_, morph_;
    std::array<Lane, PeriodCount> fold_;
    bool closing_ = false;
    RECT morphFrom_{}; float morphRadius_ = 0; // popover rectangle in window pixels
    int appliedVisible_ = -1, appliedMorph_ = -1, lead_ = 0; RECT appliedOwner_{}, appliedWindow_{};   // lead_: slide offset (window px)
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
        return t < 1 ? MorphSlots(items, slots, t, const_cast<View&>(view_).CardHeight()) : slots;
    }
    float Scale() const { return 96.f / Dpi(); }
    // The 268 DIP column: at the popover's place when the morph starts, docked after
    // (and `lead_` further toward its entering edge while it slides, see ApplyGeometry).
    // #143, docked on the left: the column is the window's left part [0, column), the
    // margin strip its right one, and the slide enters from the left.
    D2D1_POINT_2F Origin(Motion::Time now) const override {
        const float t = Morph(now), docked = left_ ? -lead_ * Scale() : DrawerLayout::margin + lead_ * Scale();
        return {(morphFrom_.left * Scale()) + (docked - morphFrom_.left * Scale()) * t, morphFrom_.top * Scale() * (1 - t)};
    }
    void Decorate(Scene& scene) const override {
        const float t = Morph(scene.now);
        scene.viewTop = DrawerLayout::viewTop; scene.edge = true; scene.edgeOpacity = t;
        scene.edgeRight = left_ ? OwnerPx(DrawerLayout::width) * Scale() : -1.f;   // #143: the hairline on the page's side
        const float popoverBottom = (morphFrom_.bottom - morphFrom_.top) * Scale() - Panel::padding / 2;
        scene.viewBottom = SurfaceHeight() + (std::min(popoverBottom, SurfaceHeight()) - SurfaceHeight()) * (1 - t);
        const float scroll = scroll_.Value(scene.now), max = MaxScroll(scene.now, false);
        scene.fadeTop = Motion::Clamp(scroll / DrawerLayout::topFade);
        scene.fadeBottom = Motion::Clamp((max - scroll) / Panel::pitch) * t;
        if (t < 1) {
            // The popover's border rides the growing shape and fades as it docks.
            const float s = Scale(), w = (DrawerLayout::width + DrawerLayout::margin);
            const float dockLeft = left_ ? 0.f : DrawerLayout::margin, dockRight = left_ ? DrawerLayout::width : w;   // #143: mirrored
            auto lerp = [t](float a, float b) { return a + (b - a) * t; };
            scene.frame = {lerp(morphFrom_.left * s, dockLeft) - scene.originX, lerp(morphFrom_.top * s, 0) - scene.originY,
                           lerp(morphFrom_.right * s, dockRight) - scene.originX, lerp(morphFrom_.bottom * s, SurfaceHeight()) - scene.originY};
            scene.frameRadius = morphRadius_ * s * (1 - t); scene.frameOpacity = 1 - t;
            // Content meets the moving edge through a fade, never a hard cut.
            const float edge = 4 * t * (1 - t);
            scene.fadeBottom = std::max(scene.fadeBottom, edge); scene.fadeTop = std::max(scene.fadeTop, edge);
            scene.fadeTopSize = DrawerLayout::topFade * 2; // section headers emerge from the top edge through a deeper fade
            scene.viewTop = Panel::ViewTop() + (DrawerLayout::viewTop - Panel::ViewTop()) * t;   // the popover's view (8) opens to the drawer's (0)
        }
    }
    Motion::Wake ExtraWake(Motion::Time now) const override {
        bool moving = slide_.Moving(now) || scroll_.Moving(now) || morph_.Moving(now);
        for (auto& lane : fold_) moving = moving || lane.Moving(now);
        return moving ? Motion::Frame() : Motion::Wake{};
    }
    const wchar_t* ClassName() const override { return L"LisiereDownloadsDrawer"; }
    void Configure() override {
        NativeUI::ConfigureShadowlessPopup(hwnd_); // DWMWCP_DONOTROUND, no shadow: the region shapes it
        NativeUI::KeyedDrawer(hwnd_);               // the slide's not-yet-entered strip is keyed out
    }
    RECT OwnerClient() const { RECT r{}; GetClientRect(owner_, &r); MapWindowPoints(owner_, nullptr, reinterpret_cast<POINT*>(&r), 2); return r; }
    int OwnerPx(float dip) const { return static_cast<int>(std::lround(dip * GetDpiForWindow(owner_) / 96.f)); }
    float SurfaceHeight() const override { return ClientHeight(); }
    // The popover's rectangle (rounded like DWM's popup corners) growing into the
    // docked column [dockLeft, dockRight) ([margin, width) on the right, [0, column)
    // docked left), in window pixels: a GDI region, so its corner is the 1-bit one
    // the popover's own region has.
    HRGN MorphShape(float t, int dockLeft, int dockRight, int height) const {
        auto lerp = [t](LONG a, int b) { return static_cast<int>(std::lround(a + (b - a) * t)); };
        const int radius = static_cast<int>(std::lround(morphRadius_ * (1 - t))) * 2 + 1;
        return CreateRoundRectRgn(lerp(morphFrom_.left, dockLeft), lerp(morphFrom_.top, 0), lerp(morphFrom_.right, dockRight) + 1, lerp(morphFrom_.bottom, height) + 1, radius, radius);
    }
    // The docked column in window pixels: [margin, width) on the right, [0, column) docked left (#143).
    void DockColumn(int& from, int& to) const {
        RECT client{}; GetClientRect(hwnd_, &client); const int margin = OwnerPx(DrawerLayout::margin);
        from = left_ ? 0 : margin; to = left_ ? std::max(0, static_cast<int>(client.right) - margin) : static_cast<int>(client.right);
    }
    // Key colour (NativeUI::KeyedDrawer: transparent, click-through) over what this
    // frame must not show: while morphing, everything outside the growing shape;
    // otherwise the margin strip (outside the docked column); and, while sliding
    // (a close may start mid-morph), the strip the column has not entered (`lead_` px).
    void KeyOut(Motion::Time now) override {
        const float t = Morph(now);
        if (t < 1) {
            RECT client{}; GetClientRect(hwnd_, &client); int from = 0, to = 0; DockColumn(from, to);
            HRGN outside = CreateRectRgn(0, 0, client.right, client.bottom), shape = MorphShape(t, from, to, client.bottom);
            CombineRgn(outside, outside, shape, RGN_DIFF); chrome_.KeyOutRegion(outside);
            DeleteObject(shape); DeleteObject(outside);
            if (lead_ > 0) chrome_.KeyOut(KeyLead(), left_);
            return;
        }
        chrome_.KeyOut(KeyLead(), left_); KeyCorners();   // #143: docked left, the margin strip and the lead on the right
    }
    // The keyed strip's width (DIP): the margin strip and the lead (docked left: in whole pixels from the right edge).
    float KeyLead() const { return left_ ? (OwnerPx(DrawerLayout::margin) + lead_) * Scale() : DrawerLayout::margin + lead_ * Scale(); }
    // Window = margin strip + 268 DIP column, docked on the tab panel's side (#143):
    // to the owner's right edge in the top bar (margin strip on its left), to the
    // side panel's right edge in its layouts (mirrored: column, then the margin strip).
    // It never moves while sliding (NativeUI::KeyedDrawer): a slide step is one present,
    // the column drawn `lead_` pixels toward its entering edge (Origin) and the strip it
    // has not entered keyed out (KeyOut). The region rounds the corner shared with the
    // owner (empty while nothing has entered) and keeps the docked column, or the
    // whole window while morphing: it changes only when a morph starts or lands,
    // never during one. Returns true when it painted.
    bool ApplyGeometry(Motion::Time now, bool force = false) {
        if (!hwnd_) return false;
        const RECT owner = OwnerClient();
        const int column = OwnerPx(DrawerLayout::width), margin = OwnerPx(DrawerLayout::margin), width = column + margin;
        const int top = owner.top + OwnerPx(DrawerLayout::liveTop), height = std::max(1, static_cast<int>(owner.bottom - OwnerPx(DrawerLayout::liveBottom) - top));
        const int shown = static_cast<int>(std::lround(column * Motion::Clamp(slide_.Value(now))));
        const float t = Morph(now); const int morphKey = t >= 1 ? 1000 : static_cast<int>(t * 999);
        const bool left = DrawerLayout::liveLeft; const int inset = left ? 0 : OwnerPx(DrawerLayout::liveRight), dock = left ? OwnerPx(DrawerLayout::liveDock) : 0;   // Mode Focus: the floating panel's right edge (0 otherwise)
        const RECT window = NativeUI::DockedDrawerRect(owner, width, top, height, left, dock, inset);
        const float radius = left ? 0.f : DrawerLayout::liveRadius * GetDpiForWindow(owner_) / 96.f;   // Mode Focus: round top corners (src/drawer-corners.h; the top bar's floating panel only), never while morphing
        const bool restyled = !EqualRect(&window, &appliedWindow_) || radius != corners_.Radius() || left != left_;
        if (!force && !restyled && shown == appliedVisible_ && morphKey == appliedMorph_ && EqualRect(&owner, &appliedOwner_)) return false;
        const bool resized = !EqualRect(&owner, &appliedOwner_) || restyled, slid = shown != appliedVisible_;
        const bool reshape = force || resized || (morphKey < 1000) != (appliedMorph_ < 1000) || (shown > 0) != (appliedVisible_ > 0);
        appliedVisible_ = shown; appliedMorph_ = morphKey; appliedOwner_ = owner; lead_ = column - shown; left_ = left; corners_.SetRadius(radius);
        if (lead_ > 0 || morphKey < 1000) corners_.HideLeading();   // Mode Focus: it moves with the content: back at rest
        if (restyled && !force) InvalidateRect(hwnd_, nullptr, FALSE);   // the corners keyed out (or no longer) by the next paint
        if (force || !EqualRect(&window, &appliedWindow_)) { SetWindowPos(hwnd_, nullptr, window.left, window.top, width, height, SWP_NOACTIVATE | SWP_NOZORDER); appliedWindow_ = window; }
        HRGN region = nullptr;
        if (!reshape) {}
        else if (shown > 0) {
            // Owner clip: everything left of the owner's right edge, rounding the corner it shares with it
            // at 10 DIP, inside the owner's antialiased 8 DIP DWM corner: canvas over canvas, so the
            // visible outline stays the owner's smooth one.
            // #143: docked left, the bottom-left corner, and only on the window's own corner.
            const int r = OwnerPx(20);
            region = NativeUI::DrawerRegion(true, width, height, r, IsZoomed(owner_) || !NativeUI::DrawerOnWindowCorner(left, dock, inset, DrawerLayout::Side::roundAgainstPanel), left);   // Mode Focus / against the panel: off the window's corner, square
            // Docked: the column [margin, width] ([0, column] docked left) — never the
            // margin strip, which only exists for the popover to morph from. Morphing:
            // the whole window, the same for the whole morph; the popover's shape growing
            // into the column is what KeyOut leaves unkeyed, so shape and content reach
            // DWM in one present.
            HRGN shape = t < 1 ? CreateRectRgn(0, 0, width, height) : left ? CreateRectRgn(0, 0, column, height) : CreateRectRgn(margin, 0, width, height);
            CombineRgn(region, region, shape, RGN_AND); DeleteObject(shape);
        } else region = CreateRectRgn(0, 0, 0, 0);
        // No redraw here: a morph's region is set before its first frame (Enter) and
        // shrinks to the column once that strip is keyed; a slide step presents right after.
        if (region) SetWindowRgn(hwnd_, region, FALSE);
        if (resized) { scroll_.Snap(std::clamp(scroll_.Target(), 0.f, MaxScroll(now, true))); RefreshTips(); }
        if (force || !slid || shown == 0 || t < 1 || !IsWindowVisible(hwnd_)) { SyncCorners(); return false; }
        PaintNow(); ValidateRect(hwnd_, nullptr); SyncCorners();
        return true;
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
        KillTimer(hwnd_, TickTimer); corners_.Hide(); ShowWindow(hwnd_, SW_HIDE);
    }
    // Mode Focus: the antialiased top corners as presented (src/drawer-corners.h), never while morphing: the trailing one
    // whenever the column is in, the leading one at rest only (it moves with the content while sliding). KeyCorners: the
    // paint's part of them (KeyOut).
    DrawerCorners::Pair corners_;
    void SyncCorners() {
        if (!visible_ || appliedVisible_ <= 0 || appliedMorph_ < 1000 || !IsWindowVisible(hwnd_)) { corners_.Hide(); return; }
        int from = 0, to = 0; ContentColumn(from, to);
        const float c = Design::Surface::canvas; corners_.Place(hwnd_, appliedWindow_, from, to, c + (1 - c) * Popup::border, c, lead_ == 0);
    }
    void KeyCorners() { if (corners_.Radius() <= 0) return; int from = 0, to = 0; ContentColumn(from, to); HRGN cut = corners_.CutRegion(from, to); chrome_.KeyOutRegion(cut); DeleteObject(cut); }
    // The window pixels the sliding column covers now: [margin + lead, width) on the right, [0, column - lead) docked
    // left (#143; there the radius is 0: the corners belong to the top bar's floating panel).
    void ContentColumn(int& from, int& to) const {
        const int width = appliedWindow_.right - appliedWindow_.left, margin = OwnerPx(DrawerLayout::margin);
        from = left_ ? 0 : margin + lead_; to = left_ ? std::max(0, width - margin - lead_) : width;
    }
    bool left_ = false;   // #143: docked on the left (the side panel's layouts), as last placed by ApplyGeometry
    // Layered alpha 0, the colour key kept: nothing of the window reaches the
    // screen, while what it presents still lands in its surface.
    void Veil(bool veiled) { SetLayeredWindowAttributes(hwnd_, NativeUI::drawerKey, 0, veiled ? LWA_COLORKEY | LWA_ALPHA : LWA_COLORKEY); }
    // Show(); `test`: never activated (the user's focus stays where it is) and never reduced motion.
    void Enter(const RECT* from, HWND under, bool test) {
        if (!hwnd_) return;
        motion_.SetReducedMotion(!test && Motion::PreferredReducedMotion());
        auto now = Motion::Now(); const bool reduced = motion_.ReducedMotion();
        if (!visible_) { slide_.Snap(0); scroll_.Snap(0); morph_.Snap(1); focus_ = 0; keyboard_ = false; ClearPointer(); }
        // Window-local pixels of the popover, the drawer's column at its docked place. #143: the popover morphs only
        // when it lies over the drawer's window (the top bar; the compact column's popover beside the left dock); away
        // from it (the complete column's popover over the column, or the top-right one in side Focus) the drawer slides in.
        RECT local{};
        if (from) {
            const RECT owner = OwnerClient(); namespace DL = DrawerLayout;
            const int width = OwnerPx(DL::width + DL::margin), top = owner.top + OwnerPx(DL::liveTop);
            const RECT docked = NativeUI::DockedDrawerRect(owner, width, top, 1, DL::liveLeft, DL::liveLeft ? OwnerPx(DL::liveDock) : 0, DL::liveLeft ? 0 : OwnerPx(DL::liveRight));
            local = {from->left - docked.left, from->top - top, from->right - docked.left, from->bottom - top};
        }
        const bool morph = from && !reduced && MorphFits(local, OwnerPx(DrawerLayout::width + DrawerLayout::margin));
        if (morph) {
            morphFrom_ = local;
            morphFrom_.left = std::max(0L, morphFrom_.left); morphFrom_.top = std::max(0L, morphFrom_.top);
            morphFrom_.right = std::min<LONG>(morphFrom_.right, OwnerPx(DrawerLayout::width + DrawerLayout::margin));
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
        // with while it is really on screen, so the drawer is shown *underneath*
        // the popover (which still covers it), is painted there in the popover's
        // shape, and DWM composes that frame. Only then is it activated, which
        // hides the popover: the drawer under it is already showing it.
        // A morph's region is the whole window from this first frame on (it no
        // longer grows), so what an earlier session left in the window's surface
        // must never reach the screen: the window stays veiled until this frame has
        // filled every pixel of it, keyed out around the popover's shape.
        if (morph) Veil(true);
        ApplyGeometry(now, true);
        SetWindowPos(hwnd_, under ? under : HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        PaintNow(); ValidateRect(hwnd_, nullptr);
        if (morph) { Veil(false); DwmFlush(); }   // one more composition: the unveiled frame is on screen before the popover goes
        DwmFlush();
        if (under) SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        SyncCorners();   // Mode Focus: a slide-in without morph (or reduced motion) may have no later frame
        if (!test) Activated();
        // The morph's clock starts now that its first frame is on screen: any time
        // spent above must never be skipped by the animation (that was a jump).
        now = Motion::Now();
        if (morph) morph_.Set(1, now, Motion::Milliseconds(0), DrawerMotion::morph, DrawerMotion::morphSpring);
        Repoint(); RefreshTips(); Ticker();
        if (hooks_.schedule) hooks_.schedule(now);
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
    void Show(const RECT* from = nullptr, HWND under = nullptr) { Enter(from, under, false); }
    // Tests: the morph out of `from` (screen pixels), never activated, on a window kept off screen;
    // MorphingForTest: true while its frames (Frame, called as the host clock does) still have work.
    void MorphForTest(const RECT& from) { Enter(&from, nullptr, true); }
    bool MorphingForTest() const { return visible_ && (morph_.Moving(Motion::Now()) || appliedMorph_ < 1000); }
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
    void Reposition() { if (visible_) ApplyGeometry(Motion::Now()); }   // Mode Focus: a new live top / right / radius counts as a move (ApplyGeometry)
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
            // Morph frame (and the one that lands it): one present carries the new
            // layout and, keyed around it, the new shape; the region stays (the
            // landing frame keys the margin strip, then the region drops it). One
            // paint per frame: the pending invalidation is consumed here.
            PaintNow(); ValidateRect(hwnd_, nullptr); ApplyGeometry(now);
            if (closing_ && !slide_.Moving(now)) { Finish(); return; }
            if (!morph_.Moving(now)) RefreshTips();
            animatingPainted_ = false; return;
        }
        const bool painted = ApplyGeometry(now);
        if (closing_ && !slide_.Moving(now)) { Finish(); return; }
        Host::Frame(now);
        if (painted) ValidateRect(hwnd_, nullptr);   // a slide step: already presented with its region
    }
};
} // namespace DownloadsUI
} // namespace NativeChrome
