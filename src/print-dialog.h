#pragma once
#include "i18n.h"
// Our print window (#121). Paper file « Browser Design », page p-V-0 « #121 — Fenêtres du moteur à notre design »:
//   « Proposition — Impression · A (avec aperçu) » (1LPR-0): the window;
//   « Proposition — Impression · états » (1NHB-0): its states, validated by the owner (issue #121, label
//   « maquette prête »): 1 page choice by ticking thumbnails, 2 range outside the document, 3 a printer in
//   landscape, 4 the printer list without any printer, 5 preview being prepared, 6 preview unavailable,
//   7 / 8 printing and print error (C8 toasts, src/print-integration.h), 9 hover and keyboard focus, 10 narrow
//   window. Values read with get_jsx (never from a capture) and noted beside each constant.
//
// The sheet on the left is the real document as it will print (src/print-preview.h rasterizes the engine's PDF);
// the settings on the right are Imprimante, Pages (Toutes / Choisir), Orientation (Portrait / Paysage),
// « Couleurs et images de fond », then « Annuler » and « Enregistrer » (« Imprimer » for a printer).
// What the boards do not show (listed in design/print-dialog.md): scrolling of a long thumbnail grid and of a long
// printer list, keyboard focus of a thumbnail, landscape thumbnails, the printer list with printers installed.
//
// Window model (as Modal::Dialog, src/modal.h): two owned shadowless popups — the scrim (per-pixel alpha so its
// bottom corners follow the page's rounded corners) swallowing pointer input on the page area, and the panel; the
// printer list is drawn inside the panel (state 4 fits it there). Show returns at once; exactly one of
// Hooks::accept / Hooks::cancel runs per Show. Échap = Annuler (or closes the list), Entrée = the focused control
// (the primary button by default), Tab / Maj+Tab cycle, ← → move a segmented choice or the preview page, ↑ ↓ Alt+↓
// F4 open the printer list, arrows + Espace tick thumbnails. Motion: 150 ms opacity (+4 DIP rise) in, 120 ms out;
// reduced motion: opacity only.
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <d2d1.h>
#include <dwrite.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "modal.h"

namespace NativeChrome {
namespace PrintDialog {
namespace Paper {
// Window (1LS7-0): 720 wide, 1 px #FFFFFF14 border (border-box), radius 10, #171717, 480 inside the border.
inline constexpr float width = 720, height = 482, border = 1, radius = 10, borderInk = 0x14 / 255.f;
inline constexpr float scrim = 0x80 / 255.f;                   // « Voile » #00000080
inline constexpr float pageTop = 140 - 81;                     // panel top under the page area's top
// Preview column: 360, #0F0F0F, padding 24, gap 12, content centred; sheet white radius 2; pager 24 high.
inline constexpr float previewWidth = 360, previewPad = 24, previewGap = 12;
inline constexpr unsigned long previewFill = 0x0F0F0F;
inline constexpr float slotWidth = previewWidth - 2 * previewPad, slotHeight = 396, sheetRadius = 2;
inline constexpr float pager = 24, pagerGap = 8, pagerGlyph = 14, pagerOn = 0xB3 / 255.f, pagerOff = 0x52 / 255.f, pagerQuiet = 0x80 / 255.f;
inline constexpr float arrowHover = 0x14 / 255.f;              // state 9: arrow hover #FFFFFF14, glyph white
// Settings column: padding 16, gap 16; label 14/20 #FFFFFF80; controls 32 radius 8 #FFFFFF14 (hover #FFFFFF1F).
inline constexpr float pad = 16, gap = 16, headGap = 4, fieldGap = 8;
inline constexpr float control = 32, controlRadius = 8, controlFill = 0x14 / 255.f, controlHover = 0x1F / 255.f;
inline constexpr float selectPad = 10, selectGap = 8, caret = 12, caretInk = 0x99 / 255.f;
inline constexpr float segmentPad = 2, segmentGap = 2, segmentOnRadius = 6, segmentOffRadius = 4, segmentOffInk = 0xB3 / 255.f, segmentHover = 0x0A / 255.f;
inline constexpr float check = 16, checkRadius = 4, checkGlyph = 10, checkGap = 8, checkRowHover = 0x0A / 255.f, checkRowInset = 6, checkRowRadius = 6;
inline constexpr float actionsTop = 8, buttonGap = 8, disabledPrimary = .4f;   // state 2: the disabled action at 40 %
inline constexpr float label = 0x80 / 255.f, subtitle = 0xB3 / 255.f;
inline constexpr float focusRing = 0x80 / 255.f;                // state 9: 1 px white 50 %, outside the control
// Range field (states 1, 2): 8 under the choice; help / error row 6 under it, 13/18.
inline constexpr float helpGap = 6, helpLine = 18, errorGlyph = 14, errorGap = 6, caretHeight = 16, dangerFill = .08f;
// Thumbnails (state 1): grid 272 wide, columns 128 (gap 16), rows gap 12; sheet 120 wide radius 2; number 13/16
// Medium 8 under it (#FFFFFFE6 ticked, #FFFFFF80 not); ticked: outline 2 accent, offset 2; not ticked: 60 %;
// box 18 at 6 / 6 from the top-right: accent radius 4 with an 11 check, or white with a 1.5 #00000066 border.
inline constexpr float gridWidth = 272, thumbItem = 128, thumbGap = 16, thumbRowGap = 12, thumbWidth = 120, thumbNumberGap = 8, thumbNumber = 16;
inline constexpr float thumbOutline = 2, thumbOffset = 2, thumbOff = .6f, thumbBox = 18, thumbBoxInset = 6, thumbBoxRadius = 4, thumbGlyph = 11, thumbBoxRing = 1.5f;
// Loading (state 5): skeleton on the sheet (padding 24 / 22, gap 10).
inline constexpr float skeletonPadY = 24, skeletonPadX = 22, skeletonGap = 10;
// Unavailable (state 6): dashed 1 px #FFFFFF26 radius 4, padding 24, gap 8, icon 40; title 14/20 Medium #FFFFFFE6;
// text 13/18 #FFFFFF80, 249 wide, centred.
inline constexpr float dashedInk = 0x26 / 255.f, dashedRadius = 4, unavailableIcon = 40, unavailableGap = 8, unavailableText = 249;
// Printer list (state 4): #171717, 1 px #FFFFFF14, radius 10, padding 6, gap 2; rows 32 radius 6 padding 8 gap 10,
// 14/18; highlight #333333; separator 1 px #FFFFFF0D, 4 / 8 margins; note padding 6 / 8 #FFFFFF80.
inline constexpr float listTop = 3, listPad = 6, listGap = 2, listRow = 32, listRowRadius = 6, listRowPad = 8, listRowGap = 10;
inline constexpr float listSeparatorInk = 0x0D / 255.f, listSeparatorY = 4, listSeparatorX = 8, listNotePadY = 6, listCheck = 14, listExternal = 12;
// Narrow window (state 10): under 760 of page area, 400 wide, one column; header: title, gap 12, count, and the
// « Aperçu » chip at the right (#FFFFFF0A radius 8, padding 4 4 4 8, gap 8, sheet 32 x 44 radius 2, 13/16 Medium).
inline constexpr float narrowBelow = 760, narrowWidth = 400, narrowHeadGap = 12, chipFill = 0x0A / 255.f, chipRadius = 8, chipPad = 4, chipPadRight = 8, chipGap = 8, chipSheetW = 32, chipSheetH = 44, narrowActionsTop = 16;
inline constexpr float enterMs = 150, exitMs = 120, enterRise = 4;
inline constexpr DialogKit::Style segment{14, 18, true, 0}, help{13, 18}, thumbNumberStyle{13, 16, true}, listText{14, 18}, unavailableTitle{14, 20, true}, chipText{13, 16, true};
}
namespace Glyph {   // the boards' SVG exports (Phosphor, viewBox 0 0 256 256)
inline constexpr const char* previous = "M168.49,199.51a12,12,0,0,1-17,17l-80-80a12,12,0,0,1,0-17l80-80a12,12,0,0,1,17,17L97,128Z";
inline constexpr const char* next = DialogKit::Glyph::caretRight;
inline constexpr const char* caret = DialogKit::Glyph::caretDown;
inline constexpr const char* check = DialogKit::Glyph::check;
inline constexpr const char* warning = "M128,24A104,104,0,1,0,232,128,104.11,104.11,0,0,0,128,24Zm-8,56a8,8,0,0,1,16,0v56a8,8,0,0,1-16,0Zm8,104a12,12,0,1,1,12-12A12,12,0,0,1,128,184Z";
inline constexpr const char* external = "M224,104a8,8,0,0,1-16,0V59.32l-66.33,66.34a8,8,0,0,1-11.32-11.32L196.68,48H152a8,8,0,0,1,0-16h64a8,8,0,0,1,8,8Zm-40,24a8,8,0,0,0-8,8v72H48V80h72a8,8,0,0,0,0-16H48A16,16,0,0,0,32,80V208a16,16,0,0,0,16,16H176a16,16,0,0,0,16-16V136A8,8,0,0,0,184,128Z";
// Jiggicons document-dismiss, glass, grey (state 6, viewBox 0 0 24 24, fill-rule evenodd): body #3A3A3D, shine white
// 35 % (y 2 -> 14.4), fold and badge #A3A3A8 -> #6E6E73 (y 2.5 -> 22.5), cross white. A paid icon, on one line: the
// public copy shows a free substitute there (tools/public-mirror/paid-icons, document-dismiss).
namespace Dismiss { inline constexpr const char *body = "", *shine = "", *fold = "M20.296 7.455l-5.25 -5.25A1.125 1.125 0 0 0 14.25 1.875H5.25A1.875 1.875 0 0 0 3.375 3.75V20.25a1.875 1.875 0 0 0 1.875 1.875H18.75a1.875 1.875 0 0 0 1.875 -1.875V8.25A1.125 1.125 0 0 0 20.296 7.455ZM17.156 7.5H15V5.344ZM5.625 19.875V4.125h7.125V8.625a1.125 1.125 0 0 0 1.125 1.125h4.5V19.875Zm9.421 -7.079L13.594 14.25l1.455 1.454a1.125 1.125 0 0 1 -1.594 1.594L12 15.844l-1.454 1.455a1.125 1.125 0 0 1 -1.594 -1.594L10.406 14.25 8.954 12.796a1.125 1.125 0 1 1 1.594 -1.594L12 12.656l1.454 -1.455a1.125 1.125 0 1 1 1.594 1.594Z", *badge = "", *cross = ""; }   // Phosphor file-x (bold, 2.1.1, MIT), scaled to 24
}

// Page ranges as the engine reads them (ICoreWebView2PrintSettings2::PageRanges): items separated
// by commas, each « n », « a-b », « -b » (from 1), « a- » (to the end) or « - »; spaces ignored;
// pages from 1 to `total`, a ≤ b. `pages` receives the 0-based pages, ascending, without repeats.
// `beyond` (optional) tells that the text is well formed but names a page after the last one.
inline bool ParseRanges(const std::wstring& text, int total, std::vector<int>* pages = nullptr, bool* beyond = nullptr) {
    if (beyond) *beyond = false;
    if (total < 1) return false;
    std::vector<bool> on(static_cast<size_t>(total), false); bool any = false, past = false;
    size_t i = 0; const size_t n = text.size();
    auto space = [&] { while (i < n && (text[i] == L' ' || text[i] == L'\t' || text[i] == 0xA0 || text[i] == 0x202F)) ++i; };
    auto dash = [](wchar_t c) { return c == L'-' || c == 0x2010 || c == 0x2011 || c == 0x2012 || c == 0x2013 || c == 0x2014 || c == 0x2212; };
    auto number = [&](long long& v) { const size_t from = i; v = 0; while (i < n && text[i] >= L'0' && text[i] <= L'9') { v = std::min<long long>(v * 10 + (text[i] - L'0'), 1000000000LL); ++i; } return i > from; };
    for (;;) {
        space(); long long a = 0, b = 0; const bool hasA = number(a); space();
        if (i < n && dash(text[i])) { ++i; space(); const bool hasB = number(b); if (!hasA) a = 1; if (!hasB) b = total; }
        else { if (!hasA) return false; b = a; }
        if (a < 1 || b < a) return false;
        if (a > total || b > total) past = true;
        else for (long long p = a; p <= b; ++p) on[static_cast<size_t>(p - 1)] = true;
        any = true; space();
        if (i == n) break;
        if (text[i] != L',') return false;
        ++i;
    }
    if (!any) return false;
    if (past) { if (beyond) *beyond = true; return false; }
    if (pages) { pages->clear(); for (int p = 0; p < total; ++p) if (on[static_cast<size_t>(p)]) pages->push_back(p); }
    return true;
}
// The field's text for ticked pages (0-based): runs of 3 or more become « a-b », pairs stay « a, b » (« 1-3, 5 »).
inline std::wstring FormatRanges(const std::vector<int>& pages) {
    std::wstring out; size_t i = 0;
    while (i < pages.size()) {
        size_t j = i; while (j + 1 < pages.size() && pages[j + 1] == pages[j] + 1) ++j;
        auto add = [&](const std::wstring& s) { if (!out.empty()) out += L", "; out += s; };
        if (j - i >= 2) add(std::to_wstring(pages[i] + 1) + L"-" + std::to_wstring(pages[j] + 1));
        else for (size_t k = i; k <= j; ++k) add(std::to_wstring(pages[k] + 1));
        i = j + 1;
    }
    return out;
}
// window.print() of a page's top document opens our window: a script added to every document replaces it by a
// message to the browser (src/print-integration.h). Frames keep the engine's own print (the engine prints the
// top document only). The function keeps its name and length so pages see a function named print.
inline constexpr wchar_t PageScript[] = LR"JS((()=>{try{if(window!==window.top)return;const w=window.chrome&&window.chrome.webview;if(!w||typeof w.postMessage!=='function')return;const post=w.postMessage.bind(w);
const print=function print(){try{post('lisiere-print');}catch(_){}};Object.defineProperty(window,'print',{value:print,writable:true,configurable:true,enumerable:true});}catch(_){}})())JS";
inline constexpr wchar_t PageMessage[] = L"lisiere-print";
// The text handed to the engine: typographic dashes become '-', no-break spaces become spaces.
inline std::wstring EngineRanges(const std::wstring& text) {
    std::wstring out; out.reserve(text.size());
    for (wchar_t c : text) out += (c == 0x2010 || c == 0x2011 || c == 0x2012 || c == 0x2013 || c == 0x2014 || c == 0x2212) ? L'-' : (c == 0xA0 || c == 0x202F || c == L'\t') ? L' ' : c;
    return out;
}
// The sheet in its slot: paper `w` × `h` (any unit, portrait), turned for landscape, as large as the
// slot allows, centred (Paper: A4 portrait fills the 396 height, 280 wide; landscape 312 × 221).
inline D2D1_RECT_F SheetIn(D2D1_RECT_F slot, float w, float h, bool landscape) {
    if (w <= 0 || h <= 0) { w = 210; h = 297; }
    if (landscape) std::swap(w, h);
    const float sw = slot.right - slot.left, sh = slot.bottom - slot.top, k = std::min(sw / w, sh / h);
    const float rw = std::round(w * k), rh = std::round(h * k);
    const float x = slot.left + std::round((sw - rw) / 2), y = slot.top + std::round((sh - rh) / 2);
    return {x, y, x + rw, y + rh};
}

struct State {
    std::vector<std::wstring> printers;   // shown names, in list order (« Enregistrer en PDF » first)
    int pdf = 0;                          // the entry that saves a PDF (« Enregistrer en PDF »)
    int printer = 0;
    bool chooseRange = false; std::wstring ranges;
    bool landscape = false, backgrounds = true;
    float paperWidth = 210, paperHeight = 297;   // the chosen destination's paper, portrait (mm)
    bool SavesPdf() const { return printer == pdf; }
    bool NoPrinter() const { return printers.size() <= 1; }
};

class Dialog {
public:
    struct Hooks {
        std::function<void()> wake;                                        // the host clock: a frame is wanted
        std::function<void(const State&)> changed;                        // printer, orientation or backgrounds changed: a new document
        std::function<void(int page, UINT width, UINT height)> want;       // a page (0-based) is needed at this pixel size
        std::function<void(const State&)> accept;                          // « Enregistrer » / « Imprimer »
        std::function<void()> cancel;                                      // « Annuler », Échap, Abort
        std::function<void()> addPrinter;                                  // « Ajouter une imprimante… » (state 4)
        std::function<bool()> keyboard;                                    // was the host's last input the keyboard?
    };
    // Focus / hit ids (stable; tests and UIA).
    enum Hit { None = 0, PreviousHit = 1, NextHit, PrinterHit, AllPagesHit = 10, ChoosePagesHit, PortraitHit = 20, LandscapeHit, BackgroundsHit = 30,
               CancelHit = 40, AcceptHit, RangeStop = 50, GridStop = 60, ColumnHit = 70, ThumbBase = 10000, ListBase = 20000 };
    static int ThumbHit(int page) { return ThumbBase + page; }
    static bool IsThumb(int hit) { return hit >= ThumbBase && hit < ListBase; }
    static int ListHit(size_t row) { return ListBase + static_cast<int>(row); }
    static bool IsList(int hit) { return hit >= ListBase; }
    enum class Mode { Sheet, Grid, Unavailable };
    // Offscreen render options (tests and the Designer bot's references).
    struct Render { bool narrow = false, unavailable = false, listOpen = false, rangeFocus = false; int hover = 0, focus = 0, listHighlight = -1; float scroll = 0; };
private:
    using Kit = DialogKit::Kit;
    struct Box { int hit = 0; D2D1_RECT_F r{}; };
    struct Page { UINT width = 0, height = 0; std::vector<BYTE> pixels; ID2D1Bitmap* bitmap = nullptr; ID2D1RenderTarget* owner = nullptr; };
    enum class RowKind { Choice, Separator, Note, Add };
    struct ListRow { RowKind kind = RowKind::Choice; int printer = -1; std::wstring text; D2D1_RECT_F r{}; };
    HWND hwnd_ = nullptr, scrim_ = nullptr, owner_ = nullptr, restore_ = nullptr, range_ = nullptr; HINSTANCE instance_ = nullptr;
    Chrome chrome_; Kit kit_; Motion::State motion_; Hooks hooks_; Accessibility::Bridge uia_;
    Chrome* pc_ = &chrome_; UINT offscreenDpi_ = 0; std::wstring root_;
    State state_; RECT area_{}; float corner_ = 0;
    bool open_ = false, closing_ = false, keyboard_ = true, reduced_ = false, rangeFocus_ = false, headless_ = false;   // headless_: tests drive the logic without showing or focusing anything
    bool narrow_ = false, unavailable_ = false, listOpen_ = false;
    int hover_ = 0, pressed_ = 0, focus_ = 0, listHighlight_ = -1, gridFocus_ = 0;
    Motion::Tween fade_;
    int total_ = -1, index_ = 0; std::vector<int> visible_; std::map<int, Page> pages_;
    std::vector<Box> boxes_; std::vector<ListRow> rows_; float scroll_ = 0, gridHeight_ = 0, listScroll_ = 0, listContent_ = 0;
    // Geometry of the current layout (DIP, panel-local, border included).
    float width_ = Paper::width, height_ = Paper::height, setLeft_ = 0, setWidth_ = 0;
    float titleTop_ = 0, printerTop_ = 0, pagesTop_ = 0, orientationTop_ = 0, checkTop_ = 0, actionsTop_ = 0;
    D2D1_RECT_F sheet_{}, chip_{}, list_{};
    DialogKit::EditField field_; HFONT font_ = nullptr;
    std::map<const char*, ID2D1PathGeometry*> evenOdd_;   // the dismiss icon's paths (factory resources)
    // Scrim surface (premultiplied black, rounded bottom corners), uploaded with UpdateLayeredWindow.
    HDC scrimDc_ = nullptr; HBITMAP scrimBitmap_ = nullptr; HGDIOBJ scrimOld_ = nullptr; int scrimW_ = 0, scrimH_ = 0, scrimR_ = -1;
    // Fade bookkeeping. `settled_`: a frame has landed the fade's final value (full opacity, final place). Until then
    // NextWake keeps asking for frames, so a frame that comes after the fade's end still lands it (night 24/11: the
    // first frames came late while the window opened and the panel and the scrim stayed at the first frame's 15 %).
    bool settled_ = true; BYTE alpha_ = 0, scrimAlpha_ = 0;
    // UIA keys of the nodes that are not hit ids. Never a Hit value: PreviousHit is 1, and a group keyed 1 made the tree
    // loop (window -> sheet -> window...: a UIA client walked ~1000 nodes in 9 s, release 25/11).
    static constexpr long long GroupKey = 90, PagerTextKey = 7, SheetKey = 8, ChipKey = 9;

    UINT Dpi() const { return offscreenDpi_ ? offscreenDpi_ : GetDpiForWindow(hwnd_ ? hwnd_ : owner_); }
    int Px(float dip) const { return static_cast<int>(std::lround(dip * Dpi() / 96.f)); }
    float Dip(int px) const { return px * 96.f / Dpi(); }
    static constexpr float B = Paper::border;
    // ---- state ---------------------------------------------------------------------------------------
    bool Loading() const { return total_ < 1 && !unavailable_; }
    bool RangeBeyond() const { bool beyond = false; return state_.chooseRange && total_ >= 1 && !state_.ranges.empty() && !ParseRanges(state_.ranges, total_, nullptr, &beyond) && beyond; }
    bool RangeError() const { return state_.chooseRange && total_ >= 1 && !state_.ranges.empty() && !ParseRanges(state_.ranges, total_); }
    bool RangeValid() const { return !state_.chooseRange || (total_ >= 1 && ParseRanges(state_.ranges, total_)) || (unavailable_ && !state_.ranges.empty()); }
    std::vector<int> Ticked() const { std::vector<int> p; if (total_ >= 1 && state_.chooseRange) ParseRanges(state_.ranges, total_, &p); return p; }
    Mode PreviewMode() const { if (unavailable_) return Mode::Unavailable; if (state_.chooseRange && total_ >= 1 && !RangeError()) return Mode::Grid; return Mode::Sheet; }
    int Sheets() const { if (total_ < 1) return -1; if (state_.chooseRange && !RangeError()) return static_cast<int>(Ticked().size()); return total_; }
    std::wstring PrimaryLabel() const { return state_.SavesPdf() ? TR(print_save) : TR(print_print); }
    std::wstring PagerText() const {
        if (Loading()) return TR(print_preparing);
        return total_ >= 1 && !visible_.empty() ? TRF(print_page_of, std::to_wstring(index_ + 1), std::to_wstring(visible_.size())) : L"";
    }
    std::wstring Subtitle() const {
        if (unavailable_) return L"";
        if (Loading()) return TR(print_counting);
        const int n = Sheets(); return TRP(print_sheets, n, std::to_wstring(n));
    }
    bool HasSubtitle() const { return !unavailable_; }
    bool Enabled(int hit) const {
        if (!hit) return false;
        if (hit == PreviousHit) return !narrow_ && PreviewMode() == Mode::Sheet && total_ >= 1 && index_ > 0;
        if (hit == NextHit) return !narrow_ && PreviewMode() == Mode::Sheet && total_ >= 1 && index_ + 1 < static_cast<int>(visible_.size());
        if (hit == AcceptHit) return RangeValid();
        if (hit == GridStop || IsThumb(hit)) return !narrow_ && PreviewMode() == Mode::Grid;
        if (IsList(hit)) { const size_t i = static_cast<size_t>(hit - ListBase); return listOpen_ && i < rows_.size() && (rows_[i].kind == RowKind::Choice || rows_[i].kind == RowKind::Add); }
        return true;
    }
    D2D1_RECT_F Find(int hit) const { for (auto& b : boxes_) if (b.hit == hit) return b.r; return {}; }
    int HitAt(float x, float y) const {
        if (listOpen_) { for (size_t i = 0; i < rows_.size(); ++i) if (Enabled(ListHit(i)) && DialogKit::Inside(rows_[i].r, x, y) && y >= list_.top && y < list_.bottom) return ListHit(i); return DialogKit::Inside(list_, x, y) ? -1 : -2; }
        for (auto it = boxes_.rbegin(); it != boxes_.rend(); ++it) if (DialogKit::Inside(it->r, x, y)) return it->hit;
        return 0;
    }
    // ---- geometry --------------------------------------------------------------------------------------
    float ThumbHeight() const { float w = state_.paperWidth > 0 ? state_.paperWidth : 210, h = state_.paperHeight > 0 ? state_.paperHeight : 297; if (state_.landscape) std::swap(w, h); return std::round(Paper::thumbWidth * h / w); }
    float ThumbItemHeight() const { return ThumbHeight() + Paper::thumbNumberGap + Paper::thumbNumber; }
    D2D1_RECT_F ColumnInner() const { return {B + Paper::previewPad, B + Paper::previewPad, B + Paper::previewWidth - Paper::previewPad, height_ - B - Paper::previewPad}; }
    D2D1_RECT_F RangeBox() const { const auto s = Find(AllPagesHit); const float top = s.bottom + Paper::segmentPad + Paper::fieldGap; return {setLeft_, top, setLeft_ + setWidth_, top + Paper::control}; }
    D2D1_RECT_F ControlBox(float labelTop) const { const float top = labelTop + 20 + Paper::fieldGap; return {setLeft_, top, setLeft_ + setWidth_, top + Paper::control}; }
    void Layout() {
        boxes_.clear();
        width_ = narrow_ ? Paper::narrowWidth : Paper::width;
        setLeft_ = narrow_ ? B + Paper::pad : B + Paper::previewWidth + Paper::pad; setWidth_ = width_ - B - Paper::pad - setLeft_;
        // Header: wide « Imprimer » over the count (gap 4); narrow: one 52 row (the chip's 44 + 8) with the chip at the right.
        titleTop_ = B + Paper::pad;
        float y = titleTop_ + (narrow_ ? Paper::chipSheetH + 2 * Paper::chipPad : HasSubtitle() ? 20 + Paper::headGap + 20 : 20) + Paper::gap;
        if (narrow_) {
            const float cw = Paper::chipPad + Paper::chipSheetW + Paper::chipGap + kit_.Width(TR(print_preview_chip), Paper::chipText) + Paper::chipPadRight;
            chip_ = {setLeft_ + setWidth_ - cw, titleTop_, setLeft_ + setWidth_, titleTop_ + Paper::chipSheetH + 2 * Paper::chipPad};
        } else chip_ = {};
        printerTop_ = y; boxes_.push_back({PrinterHit, ControlBox(printerTop_)}); y += 20 + Paper::fieldGap + Paper::control + Paper::gap;
        pagesTop_ = y;
        auto segments = [&](float labelTop, int first) {
            const auto c = ControlBox(labelTop); const float w = (c.right - c.left - 2 * Paper::segmentPad - Paper::segmentGap) / 2;
            for (int k = 0; k < 2; ++k) { const float l = c.left + Paper::segmentPad + k * (w + Paper::segmentGap); boxes_.push_back({first + k, {l, c.top + Paper::segmentPad, l + w, c.bottom - Paper::segmentPad}}); }
        };
        segments(pagesTop_, AllPagesHit); y += 20 + Paper::fieldGap + Paper::control;
        if (state_.chooseRange) y += Paper::fieldGap + Paper::control + Paper::helpGap + Paper::helpLine;
        y += Paper::gap; orientationTop_ = y; segments(orientationTop_, PortraitHit); y += 20 + Paper::fieldGap + Paper::control + Paper::gap;
        checkTop_ = y;
        const float lw = kit_.Width(TR(print_backgrounds), DialogKit::Type::field);
        boxes_.push_back({BackgroundsHit, {setLeft_, checkTop_, std::min(setLeft_ + setWidth_, setLeft_ + Paper::check + Paper::checkGap + lw), checkTop_ + Paper::control}});
        if (narrow_) { actionsTop_ = checkTop_ + Paper::control + Paper::gap + Paper::narrowActionsTop + Paper::actionsTop; height_ = actionsTop_ + Paper::control + Paper::pad + B; }
        else { height_ = Paper::height; actionsTop_ = height_ - B - Paper::pad - Paper::control; }
        const float right = setLeft_ + setWidth_, aw = kit_.ButtonWidth(PrimaryLabel()), cw = kit_.ButtonWidth(TR(common_cancel));
        boxes_.push_back({AcceptHit, {right - aw, actionsTop_, right, actionsTop_ + Paper::control}});
        boxes_.push_back({CancelHit, {right - aw - Paper::buttonGap - cw, actionsTop_, right - aw - Paper::buttonGap, actionsTop_ + Paper::control}});
        // Preview column (wide only).
        sheet_ = {};
        if (!narrow_) {
            const auto inner = ColumnInner(); const float innerH = inner.bottom - inner.top;
            boxes_.push_back({ColumnHit, {B, B, B + Paper::previewWidth, height_ - B}});
            switch (PreviewMode()) {
            case Mode::Unavailable: sheet_ = {B + std::round((Paper::previewWidth - 280) / 2), inner.top, B + std::round((Paper::previewWidth - 280) / 2) + 280, inner.top + Paper::slotHeight}; break;
            case Mode::Grid: {
                const int rows = (total_ + 1) / 2; const float ih = ThumbItemHeight();
                gridHeight_ = rows * ih + std::max(0, rows - 1) * Paper::thumbRowGap;
                scroll_ = std::clamp(scroll_, 0.f, std::max(0.f, gridHeight_ - innerH));
                const float top = gridHeight_ <= innerH ? inner.top + std::round((innerH - gridHeight_) / 2) : inner.top - scroll_;
                const float x0 = B + std::round((Paper::previewWidth - Paper::gridWidth) / 2);
                boxes_.push_back({GridStop, {x0, inner.top, x0 + Paper::gridWidth, inner.bottom}});
                for (int p = 0; p < total_; ++p) {
                    const float x = x0 + (p % 2) * (Paper::thumbItem + Paper::thumbGap) + (Paper::thumbItem - Paper::thumbWidth) / 2, ty = top + (p / 2) * (ih + Paper::thumbRowGap);
                    const D2D1_RECT_F r{x, ty, x + Paper::thumbWidth, ty + ThumbHeight()};
                    if (r.bottom > B && r.top < height_ - B) boxes_.push_back({ThumbHit(p), r});
                }
                break;
            }
            case Mode::Sheet: {
                const auto s = SheetIn({inner.left, inner.top, inner.right, inner.top + Paper::slotHeight}, state_.paperWidth, state_.paperHeight, state_.landscape);
                const float sw = s.right - s.left, sh = s.bottom - s.top, group = sh + Paper::previewGap + Paper::pager;
                const float top = inner.top + std::round((innerH - group) / 2), left = B + std::round((Paper::previewWidth - sw) / 2);
                sheet_ = {left, top, left + sw, top + sh};
                const float tw = kit_.Width(PagerText(), DialogKit::Type::body), w = Paper::pager * 2 + Paper::pagerGap * 2 + tw;
                const float px = B + std::round((Paper::previewWidth - w) / 2), py = sheet_.bottom + Paper::previewGap;
                boxes_.push_back({PreviousHit, {px, py, px + Paper::pager, py + Paper::pager}});
                boxes_.push_back({NextHit, {px + w - Paper::pager, py, px + w, py + Paper::pager}});
                break;
            }
            }
        }
        LayoutList();
    }
    // ---- printer list (state 4) ---------------------------------------------------------------------------
    void BuildRows() {
        rows_.clear();
        ListRow pdf; pdf.printer = state_.pdf; pdf.text = state_.pdf >= 0 && state_.pdf < static_cast<int>(state_.printers.size()) ? state_.printers[static_cast<size_t>(state_.pdf)] : L""; rows_.push_back(pdf);
        ListRow sep; sep.kind = RowKind::Separator; rows_.push_back(sep);
        if (state_.NoPrinter()) {
            ListRow note; note.kind = RowKind::Note; note.text = TR(print_no_printer); rows_.push_back(note);
            ListRow add; add.kind = RowKind::Add; add.text = TR(print_add_printer); rows_.push_back(add);
        } else for (size_t i = 0; i < state_.printers.size(); ++i) if (static_cast<int>(i) != state_.pdf) { ListRow r; r.printer = static_cast<int>(i); r.text = state_.printers[i]; rows_.push_back(r); }
    }
    float RowHeight(const ListRow& r) const { return r.kind == RowKind::Separator ? 2 * Paper::listSeparatorY + 1 : r.kind == RowKind::Note ? 2 * Paper::listNotePadY + Paper::listText.line : Paper::listRow; }
    void LayoutList() {
        if (!listOpen_) { list_ = {}; return; }
        const auto sel = Find(PrinterHit); float content = 0;
        for (size_t i = 0; i < rows_.size(); ++i) content += (i ? Paper::listGap : 0) + RowHeight(rows_[i]);
        listContent_ = content;
        const float top = sel.bottom + Paper::listTop, full = 2 * (B + Paper::listPad) + content, room = height_ - B - 8 - top;   // a long list scrolls inside the panel (not on the board)
        const float h = std::min(full, std::max(Paper::listRow + 2 * (B + Paper::listPad), room));
        list_ = {sel.left - B, top, sel.right + B, top + h};
        listScroll_ = std::clamp(listScroll_, 0.f, std::max(0.f, full - h));
        float y = list_.top + B + Paper::listPad - listScroll_;
        for (auto& r : rows_) { const float rh = RowHeight(r); r.r = {list_.left + B + Paper::listPad, y, list_.right - B - Paper::listPad, y + rh}; y += rh + Paper::listGap; }
    }
    void OpenList(bool keyboard) {
        if (listOpen_ || !open_) return;
        BuildRows(); listOpen_ = true; listScroll_ = 0; keyboard_ = keyboard; focus_ = PrinterHit; Layout();
        listHighlight_ = -1; for (size_t i = 0; i < rows_.size(); ++i) if (rows_[i].kind == RowKind::Choice && rows_[i].printer == state_.printer) listHighlight_ = static_cast<int>(i);
        if (listHighlight_ < 0) listHighlight_ = 0;
        InvalidateRect(hwnd_, nullptr, FALSE); if (uia_.Listening()) uia_.ChildrenChanged(GroupKey);
    }
    void CloseList() { if (!listOpen_) return; listOpen_ = false; listHighlight_ = -1; Layout(); InvalidateRect(hwnd_, nullptr, FALSE); if (uia_.Listening()) uia_.ChildrenChanged(GroupKey); }
    void MoveHighlight(int step) {
        if (!listOpen_ || rows_.empty()) return; int i = listHighlight_;
        for (size_t guard = 0; guard < rows_.size(); ++guard) { i = std::clamp(i + step, 0, static_cast<int>(rows_.size()) - 1); if (Enabled(ListHit(static_cast<size_t>(i)))) { listHighlight_ = i; break; } if (i == 0 || i == static_cast<int>(rows_.size()) - 1) break; }
        // keep the highlighted row in view
        if (listHighlight_ >= 0) { const auto r = rows_[static_cast<size_t>(listHighlight_)].r; const float top = list_.top + B + Paper::listPad, bottom = list_.bottom - B - Paper::listPad;
            if (r.top < top) listScroll_ -= top - r.top; else if (r.bottom > bottom) listScroll_ += r.bottom - bottom; LayoutList(); }
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    void ChooseRow(int row) {
        if (row < 0 || row >= static_cast<int>(rows_.size())) return;
        const ListRow r = rows_[static_cast<size_t>(row)];
        CloseList(); FocusStop(PrinterHit);
        if (r.kind == RowKind::Add) { if (hooks_.addPrinter) hooks_.addPrinter(); return; }
        if (r.kind != RowKind::Choice || r.printer == state_.printer) return;
        state_.printer = r.printer; Changed(true);   // the host answers with the destination's paper (SetPaper) and a new document
    }
    // ---- preview pages -------------------------------------------------------------------------------
    void Recompute() {
        visible_.clear();
        if (total_ >= 1 && !(state_.chooseRange && ParseRanges(state_.ranges, total_, &visible_))) { visible_.clear(); for (int p = 0; p < total_; ++p) visible_.push_back(p); }
        if (state_.chooseRange && total_ >= 1 && state_.ranges.empty()) { visible_.clear(); for (int p = 0; p < total_; ++p) visible_.push_back(p); }
        index_ = std::clamp(index_, 0, std::max(0, static_cast<int>(visible_.size()) - 1));
        gridFocus_ = std::clamp(gridFocus_, 0, std::max(0, total_ - 1));
    }
    void ReleasePage(Page& p) { if (p.bitmap) p.bitmap->Release(); if (p.owner) p.owner->Release(); p.bitmap = nullptr; p.owner = nullptr; }
    void ClearPages() { for (auto& p : pages_) ReleasePage(p.second); pages_.clear(); }
    UINT ToPx(float dip) const { return static_cast<UINT>(std::max(1L, std::lround(dip * Dpi() / 96.f))); }
    void Ask(int page, UINT w, UINT h) { if (page < 0 || page >= total_ || !hooks_.want) return; auto it = pages_.find(page); if (it == pages_.end() || it->second.width != w || it->second.height != h) hooks_.want(page, w, h); }
    void RequestPages() {
        if (!open_ || total_ < 1 || !hooks_.want || unavailable_) return;
        Layout();
        if (narrow_) { if (!visible_.empty()) Ask(visible_.front(), ToPx(Paper::chipSheetW), ToPx(Paper::chipSheetH)); }
        else if (PreviewMode() == Mode::Grid) {
            const UINT w = ToPx(Paper::thumbWidth), h = ToPx(ThumbHeight());
            for (auto it = boxes_.rbegin(); it != boxes_.rend(); ++it) if (IsThumb(it->hit)) Ask(it->hit - ThumbBase, w, h);   // the last asked is served first
        } else if (!visible_.empty()) {
            const UINT w = ToPx(sheet_.right - sheet_.left), h = ToPx(sheet_.bottom - sheet_.top);
            auto at = [&](int i) { if (i >= 0 && i < static_cast<int>(visible_.size())) Ask(visible_[static_cast<size_t>(i)], w, h); };
            at(index_ + 1); at(index_ - 1); at(index_);   // the host serves the newest wish first: the page on screen
        }
        // Bounded cache: the pages farthest from the one on screen go first.
        const size_t limit = PreviewMode() == Mode::Grid ? 24 : 12;
        while (pages_.size() > limit) {
            const int here = visible_.empty() ? 0 : visible_[static_cast<size_t>(index_)]; auto farthest = pages_.begin();
            for (auto it = pages_.begin(); it != pages_.end(); ++it) if (std::abs(it->first - here) > std::abs(farthest->first - here)) farthest = it;
            ReleasePage(farthest->second); pages_.erase(farthest);
        }
    }
    ID2D1Bitmap* BitmapOf(Page& p) {
        auto* t = pc_->RenderTarget(); if (!t || p.pixels.empty()) return nullptr;
        if (p.bitmap && p.owner == t) return p.bitmap;
        ReleasePage(p);
        if (FAILED(t->CreateBitmap(D2D1::SizeU(p.width, p.height), p.pixels.data(), p.width * 4, D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)), &p.bitmap))) { p.bitmap = nullptr; return nullptr; }
        p.owner = t; t->AddRef(); return p.bitmap;
    }
    void DrawPage(int page, D2D1_RECT_F s, float radius) {
        auto* t = pc_->RenderTarget(); auto it = pages_.find(page); if (it == pages_.end()) return;
        auto* bitmap = BitmapOf(it->second); if (!bitmap) return;
        ID2D1BitmapBrush* brush = nullptr;
        if (FAILED(t->CreateBitmapBrush(bitmap, D2D1::BitmapBrushProperties(D2D1_EXTEND_MODE_CLAMP, D2D1_EXTEND_MODE_CLAMP, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR), &brush)) || !brush) return;
        const auto size = bitmap->GetSize();
        brush->SetTransform(D2D1::Matrix3x2F::Scale((s.right - s.left) / std::max(1.f, size.width), (s.bottom - s.top) / std::max(1.f, size.height)) * D2D1::Matrix3x2F::Translation(s.left, s.top));
        t->FillRoundedRectangle(D2D1::RoundedRect(s, radius, radius), brush); brush->Release();
    }
    // ---- paint helpers -------------------------------------------------------------------------------------
    bool Focused(int hit) const { return keyboard_ && focus_ == hit && (offscreenDpi_ || headless_ || GetFocus() == hwnd_); }
    float HoverOf(int hit, Motion::Time now) const { return std::max(motion_.Visual(hit, now).hover, offscreenDpi_ && hover_ == hit ? 1.f : 0.f); }
    // CSS outline: outside the box, 1 device pixel.
    void OuterRing(D2D1_RECT_F r, float radius, D2D1_COLOR_F c) { const float px = kit_.Pixel(); kit_.Ring({r.left - px, r.top - px, r.right + px, r.bottom + px}, radius + px, c); }
    void Opacity(float a, const std::function<void()>& draw) {
        auto* t = pc_->RenderTarget(); ID2D1Layer* layer = nullptr;
        if (a < 1 && SUCCEEDED(t->CreateLayer(nullptr, &layer))) t->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), a), layer);
        draw();
        if (layer) { t->PopLayer(); layer->Release(); }
    }
    void Centred(const std::wstring& text, DialogKit::Style s, float cx, float top, D2D1_COLOR_F c) { const float w = kit_.Width(text, s); kit_.Text(text, s, std::round(cx - w / 2), top, w + 2, c); }
    ID2D1PathGeometry* EvenOdd(const char* d) { auto& g = evenOdd_[d]; if (!g && pc_->Factory()) g = DialogKit::SvgGeometry(pc_->Factory(), d, true); return g; }
    void ReleaseGeometries() { for (auto& g : evenOdd_) if (g.second) g.second->Release(); evenOdd_.clear(); }
    // Jiggicons document-dismiss (glass, grey) at `box` (40 x 40), from the board's export.
    void DrawDismiss(D2D1_RECT_F box) {
        auto* t = pc_->RenderTarget(); auto* b = pc_->SolidBrush(); if (!t || !b) return;
        D2D1_MATRIX_3X2_F old; t->GetTransform(&old); const float k = (box.right - box.left) / 24;
        t->SetTransform(D2D1::Matrix3x2F::Scale(k, k) * D2D1::Matrix3x2F::Translation(box.left, box.top) * old);
        auto gradient = [&](float y1, float y2, D2D1_COLOR_F a, D2D1_COLOR_F z) -> ID2D1LinearGradientBrush* {
            const D2D1_GRADIENT_STOP stops[2] = {{0, a}, {1, z}}; ID2D1GradientStopCollection* c = nullptr; ID2D1LinearGradientBrush* g = nullptr;
            if (SUCCEEDED(t->CreateGradientStopCollection(stops, 2, &c))) { t->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({0, y1}, {0, y2}), c, &g); c->Release(); }
            return g;
        };
        if (auto* g = EvenOdd(Glyph::Dismiss::body)) { b->SetColor(DialogKit::Rgb(0x3A3A3D)); t->FillGeometry(g, b); }
        if (auto* shine = gradient(2, 14.4f, DialogKit::White(1), DialogKit::White(0))) { shine->SetOpacity(.35f); if (auto* g = EvenOdd(Glyph::Dismiss::shine)) t->FillGeometry(g, shine); shine->Release(); }
        if (auto* grey = gradient(2.5f, 22.5f, DialogKit::Rgb(0xA3A3A8), DialogKit::Rgb(0x6E6E73))) { for (auto d : {Glyph::Dismiss::fold, Glyph::Dismiss::badge}) if (auto* g = EvenOdd(d)) t->FillGeometry(g, grey); grey->Release(); }
        if (auto* g = EvenOdd(Glyph::Dismiss::cross)) { b->SetColor(DialogKit::White(1)); t->FillGeometry(g, b); }
        t->SetTransform(old);
    }
    // ---- paint ----------------------------------------------------------------------------------------------
    void PaintSkeleton(D2D1_RECT_F s) {   // state 5: the sheet's grey shapes
        auto* t = pc_->RenderTarget(); t->PushAxisAlignedClip(s, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        const float l = s.left + Paper::skeletonPadX, w = s.right - s.left - 2 * Paper::skeletonPadX; float y = s.top + Paper::skeletonPadY;
        auto bar = [&](float bw, float bh, unsigned long rgb, float r) { kit_.Fill({l, y, l + bw, y + bh}, r, DialogKit::Rgb(rgb)); y += bh + Paper::skeletonGap; };
        bar(180, 12, 0xECECEC, 2); bar(120, 12, 0xECECEC, 2); bar(w, 110, 0xF2F2F2, 2); bar(w, 5, 0xF0F0F0, 1); bar(w, 5, 0xF0F0F0, 1); bar(std::round(w * .7f), 5, 0xF0F0F0, 1);
        t->PopAxisAlignedClip();
    }
    void PaintSheetMode(Motion::Time now) {
        kit_.Fill(sheet_, Paper::sheetRadius, DialogKit::White(1));
        if (Loading()) PaintSkeleton(sheet_);
        else if (!visible_.empty()) DrawPage(visible_[static_cast<size_t>(index_)], sheet_, Paper::sheetRadius);
        // Pager: arrows hidden while the preview is prepared (their place kept).
        for (int hit : {static_cast<int>(PreviousHit), static_cast<int>(NextHit)}) {
            if (Loading()) break;
            const auto r = Find(hit); const bool on = Enabled(hit); const float h = on ? HoverOf(hit, now) : 0, focus = on && Focused(hit) ? 1.f : 0;
            if (h > 0) kit_.Fill(r, DialogKit::Paper::buttonRadius, DialogKit::White(Paper::arrowHover * h));
            const float ink = on ? Paper::pagerOn + (1 - Paper::pagerOn) * std::max(h, focus) : Paper::pagerOff;
            kit_.DrawGlyph(hit == PreviousHit ? Glyph::previous : Glyph::next, r, Paper::pagerGlyph, DialogKit::White(ink));
            if (focus > 0) OuterRing(r, DialogKit::Paper::buttonRadius, DialogKit::White(Paper::focusRing));
        }
        const auto p = Find(PreviousHit); const std::wstring text = PagerText();
        if (!text.empty()) kit_.Text(text, DialogKit::Type::body, p.right + Paper::pagerGap, p.top + (Paper::pager - DialogKit::Type::body.line) / 2, kit_.Width(text, DialogKit::Type::body) + 2, DialogKit::White(Loading() ? Paper::pagerQuiet : Paper::subtitle));
    }
    void PaintGrid() {
        auto* t = pc_->RenderTarget(); const auto ticked = Ticked(); const std::set<int> on(ticked.begin(), ticked.end());
        t->PushAxisAlignedClip({B, B, B + Paper::previewWidth, height_ - B}, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        for (const auto& box : boxes_) {
            if (!IsThumb(box.hit)) continue;
            const int page = box.hit - ThumbBase; const bool yes = on.count(page) > 0; const auto r = box.r;
            Opacity(yes ? 1 : Paper::thumbOff, [&] {
                kit_.Fill(r, Paper::sheetRadius, DialogKit::White(1)); DrawPage(page, r, Paper::sheetRadius);
                const D2D1_RECT_F cb{r.right - Paper::thumbBoxInset - Paper::thumbBox, r.top + Paper::thumbBoxInset, r.right - Paper::thumbBoxInset, r.top + Paper::thumbBoxInset + Paper::thumbBox};
                if (yes) { kit_.Fill(cb, Paper::thumbBoxRadius, DialogKit::AccentColor()); kit_.DrawGlyph(Glyph::check, cb, Paper::thumbGlyph, DialogKit::White(1)); }
                else { kit_.Fill(cb, Paper::thumbBoxRadius, DialogKit::White(1)); auto* br = pc_->SolidBrush(); br->SetColor(D2D1::ColorF(0, 0, 0, 0x66 / 255.f)); const float h = Paper::thumbBoxRing / 2;
                       t->DrawRoundedRectangle(D2D1::RoundedRect({cb.left + h, cb.top + h, cb.right - h, cb.bottom - h}, Paper::thumbBoxRadius - h, Paper::thumbBoxRadius - h), br, Paper::thumbBoxRing); }
            });
            if (yes) {   // outline 2 px accent, 2 px away (CSS outline follows the radius: 2 + 2 + 2 outside)
                auto* br = pc_->SolidBrush(); br->SetColor(DialogKit::AccentColor()); const float o = Paper::thumbOffset + Paper::thumbOutline / 2;
                t->DrawRoundedRectangle(D2D1::RoundedRect({r.left - o, r.top - o, r.right + o, r.bottom + o}, Paper::sheetRadius + o, Paper::sheetRadius + o), br, Paper::thumbOutline);
            }
            if (Focused(GridStop) && page == gridFocus_) OuterRing({r.left - Paper::thumbOffset - Paper::thumbOutline - 2, r.top - Paper::thumbOffset - Paper::thumbOutline - 2, r.right + Paper::thumbOffset + Paper::thumbOutline + 2, r.bottom + Paper::thumbOffset + Paper::thumbOutline + 2}, 8, DialogKit::White(Paper::focusRing));   // not on the board
            Centred(std::to_wstring(page + 1), Paper::thumbNumberStyle, (r.left + r.right) / 2, r.bottom + Paper::thumbNumberGap, DialogKit::White(yes ? 0xE6 / 255.f : Paper::label));
        }
        t->PopAxisAlignedClip();
    }
    void PaintUnavailable() {   // state 6
        auto* t = pc_->RenderTarget(); auto* br = pc_->SolidBrush(); const auto s = sheet_;
        ID2D1StrokeStyle* dash = nullptr; const float dashes[2] = {3, 3};
        pc_->Factory()->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT, D2D1_LINE_JOIN_MITER, 10, D2D1_DASH_STYLE_CUSTOM, 0), dashes, 2, &dash);
        br->SetColor(DialogKit::White(Paper::dashedInk)); const float px = kit_.Pixel();
        t->DrawRoundedRectangle(D2D1::RoundedRect({s.left + px / 2, s.top + px / 2, s.right - px / 2, s.bottom - px / 2}, Paper::dashedRadius - px / 2, Paper::dashedRadius - px / 2), br, px, dash);
        if (dash) dash->Release();
        const std::wstring text = TR(print_unavailable_text); const auto cut = text.find(L'\n');
        const std::wstring a = text.substr(0, cut), b = cut == std::wstring::npos ? L"" : text.substr(cut + 1);
        const float content = Paper::unavailableIcon + Paper::unavailableGap + Paper::unavailableTitle.line + Paper::unavailableGap + Paper::help.line * (b.empty() ? 1 : 2);
        float y = s.top + std::round((s.bottom - s.top - content) / 2); const float cx = (s.left + s.right) / 2;
        DrawDismiss({cx - Paper::unavailableIcon / 2, y, cx + Paper::unavailableIcon / 2, y + Paper::unavailableIcon}); y += Paper::unavailableIcon + Paper::unavailableGap;
        Centred(TR(print_unavailable_title), Paper::unavailableTitle, cx, y, DialogKit::White(0xE6 / 255.f)); y += Paper::unavailableTitle.line + Paper::unavailableGap;
        Centred(a, Paper::help, cx, y, DialogKit::White(Paper::label)); if (!b.empty()) Centred(b, Paper::help, cx, y + Paper::help.line, DialogKit::White(Paper::label));
    }
    void PaintChip() {   // state 10: the preview as a thumbnail in the header
        kit_.Fill(chip_, Paper::chipRadius, DialogKit::White(Paper::chipFill));
        const D2D1_RECT_F s{chip_.left + Paper::chipPad, chip_.top + Paper::chipPad, chip_.left + Paper::chipPad + Paper::chipSheetW, chip_.top + Paper::chipPad + Paper::chipSheetH};
        if (!unavailable_) { kit_.Fill(s, Paper::sheetRadius, DialogKit::White(1)); if (!visible_.empty()) DrawPage(visible_.front(), s, Paper::sheetRadius); }
        else { auto* br = pc_->SolidBrush(); br->SetColor(DialogKit::White(Paper::dashedInk)); const float px = kit_.Pixel(); pc_->RenderTarget()->DrawRoundedRectangle(D2D1::RoundedRect({s.left + px / 2, s.top + px / 2, s.right - px / 2, s.bottom - px / 2}, Paper::sheetRadius, Paper::sheetRadius), br, px); }   // not on the board
        kit_.Text(TR(print_preview_chip), Paper::chipText, s.right + Paper::chipGap, (chip_.top + chip_.bottom - Paper::chipText.line) / 2, chip_.right - s.right, DialogKit::White(Paper::subtitle));
    }
    void PaintSegments(int first, const std::wstring& a, const std::wstring& b, bool second, Motion::Time now) {
        const auto r0 = Find(first), r1 = Find(first + 1);
        kit_.Fill({r0.left - Paper::segmentPad, r0.top - Paper::segmentPad, r1.right + Paper::segmentPad, r1.bottom + Paper::segmentPad}, Paper::controlRadius, DialogKit::White(Paper::controlFill));
        for (int k = 0; k < 2; ++k) {
            const int hit = first + k; const auto r = k ? r1 : r0; const bool on = (k == 1) == second; const float h = on ? 0 : HoverOf(hit, now);
            if (on) kit_.Fill(r, Paper::segmentOnRadius, DialogKit::Gray(Design::Surface::pressed));
            else if (h > 0) kit_.Fill(r, Paper::segmentOffRadius, DialogKit::White(Paper::segmentHover * h));   // state 9: #FFFFFF0A, label white
            const std::wstring& label = k ? b : a; const float w = kit_.Width(label, Paper::segment);
            kit_.Text(label, Paper::segment, std::round((r.left + r.right - w) / 2), r.top + (r.bottom - r.top - Paper::segment.line) / 2, w + 2, DialogKit::White(on ? 1.f : Paper::segmentOffInk + (1 - Paper::segmentOffInk) * h));
            if (on && (Focused(first) || Focused(first + 1))) OuterRing(r, Paper::segmentOnRadius, DialogKit::White(Paper::focusRing));
        }
    }
    COLORREF FieldColor() const {
        if (RangeError()) return DialogKit::OverCanvas(DialogKit::Danger(), Paper::dangerFill);
        const float h = motion_.Visual(RangeStop, Motion::Now()).hover;
        return DialogKit::OverCanvas(DialogKit::White(1), Paper::controlFill + (Paper::controlHover - Paper::controlFill) * h);
    }
    bool RangeHasFocus() const { return range_ ? (headless_ ? rangeFocus_ : GetFocus() == range_) : rangeFocus_; }
    void PaintRange() {   // states 1 and 2
        const auto r = RangeBox(); const COLORREF c = FieldColor();
        kit_.Fill(r, Paper::controlRadius, D2D1::ColorF(GetRValue(c) / 255.f, GetGValue(c) / 255.f, GetBValue(c) / 255.f, 1));
        if (RangeError()) OuterRing(r, Paper::controlRadius, DialogKit::Danger());
        else if (RangeHasFocus()) OuterRing(r, Paper::controlRadius, DialogKit::White(Paper::focusRing));
        if (!range_ && !state_.ranges.empty()) {   // offscreen render: the EDIT child is not part of the image
            kit_.Text(state_.ranges, DialogKit::Type::field, r.left + Paper::selectPad, r.top + 6, r.right - r.left - 2 * Paper::selectPad, DialogKit::White(1));
            if (rangeFocus_) { const float x = r.left + Paper::selectPad + kit_.Width(state_.ranges, DialogKit::Type::field) + 1; kit_.Fill({x, r.top + (Paper::control - Paper::caretHeight) / 2, x + 1, r.top + (Paper::control + Paper::caretHeight) / 2}, 0, DialogKit::White(1)); }
        }
        const float y = r.bottom + Paper::helpGap;
        if (RangeBeyond()) {
            kit_.DrawGlyph(Glyph::warning, {r.left, y + (Paper::helpLine - Paper::errorGlyph) / 2, r.left + Paper::errorGlyph, y + (Paper::helpLine + Paper::errorGlyph) / 2}, Paper::errorGlyph, DialogKit::Danger());
            kit_.Text(TRP(print_range_beyond, total_, std::to_wstring(total_)), Paper::help, r.left + Paper::errorGlyph + Paper::errorGap, y, setWidth_ - Paper::errorGlyph - Paper::errorGap, DialogKit::Danger());
        } else kit_.Text(TR(print_range_help), Paper::help, r.left, y, setWidth_, DialogKit::White(Paper::label));
    }
    void PaintSettings(Motion::Time now) {
        const float left = setLeft_, w = setWidth_;
        if (narrow_) {
            const float ty = titleTop_ + (chip_.bottom - chip_.top - 20) / 2, tw = kit_.Width(TR(print_title), DialogKit::Type::title);
            kit_.Text(TR(print_title), DialogKit::Type::title, left, ty, tw + 2, DialogKit::White(1));
            kit_.Text(Subtitle(), DialogKit::Type::body, left + tw + Paper::narrowHeadGap, ty, chip_.left - left - tw - 2 * Paper::narrowHeadGap, DialogKit::White(Paper::subtitle));
            PaintChip();
        } else {
            kit_.Text(TR(print_title), DialogKit::Type::title, left, titleTop_, w, DialogKit::White(1));
            if (HasSubtitle()) kit_.Text(Subtitle(), DialogKit::Type::body, left, titleTop_ + 20 + Paper::headGap, w, DialogKit::White(Paper::subtitle));
        }
        kit_.Text(TR(print_printer), DialogKit::Type::body, left, printerTop_, w, DialogKit::White(Paper::label));
        kit_.Text(TR(print_pages), DialogKit::Type::body, left, pagesTop_, w, DialogKit::White(Paper::label));
        kit_.Text(TR(print_orientation), DialogKit::Type::body, left, orientationTop_, w, DialogKit::White(Paper::label));
        {   // Imprimante: select (hover #FFFFFF1F, focus outline)
            const auto r = Find(PrinterHit); const float h = std::max(HoverOf(PrinterHit, now), motion_.Visual(PrinterHit, now).press);
            kit_.Fill(r, Paper::controlRadius, DialogKit::White(Paper::controlFill + (Paper::controlHover - Paper::controlFill) * h));
            const std::wstring name = state_.printer >= 0 && state_.printer < static_cast<int>(state_.printers.size()) ? state_.printers[static_cast<size_t>(state_.printer)] : L"";
            const float tl = r.left + Paper::selectPad, tr = r.right - Paper::selectPad - Paper::caret - Paper::selectGap;
            kit_.Text(name, DialogKit::Type::field, tl, r.top + (Paper::control - DialogKit::Type::field.line) / 2, tr - tl, DialogKit::White(1));
            kit_.DrawGlyph(Glyph::caret, {r.right - Paper::selectPad - Paper::caret, r.top, r.right - Paper::selectPad, r.bottom}, Paper::caret, DialogKit::White(Paper::caretInk));
            if (Focused(PrinterHit) && !listOpen_) OuterRing(r, Paper::controlRadius, DialogKit::White(Paper::focusRing));
        }
        PaintSegments(AllPagesHit, TR(print_pages_all), state_.chooseRange ? TR(print_pages_choose_on) : TR(print_pages_choose), state_.chooseRange, now);
        if (state_.chooseRange) PaintRange();
        PaintSegments(PortraitHit, TR(print_portrait), TR(print_landscape), state_.landscape, now);
        {   // « Couleurs et images de fond »: hover / focus box 6 beyond the row on both sides (state 9)
            const auto row = D2D1_RECT_F{left, checkTop_, left + w, checkTop_ + Paper::control}; const float h = HoverOf(BackgroundsHit, now);
            const D2D1_RECT_F around{row.left - Paper::checkRowInset, row.top, row.right + Paper::checkRowInset, row.bottom};
            if (h > 0) kit_.Fill(around, Paper::checkRowRadius, DialogKit::White(Paper::checkRowHover * h));
            const D2D1_RECT_F box{row.left, row.top + (Paper::control - Paper::check) / 2, row.left + Paper::check, row.top + (Paper::control + Paper::check) / 2};
            if (state_.backgrounds) { kit_.Fill(box, Paper::checkRadius, DialogKit::AccentColor()); kit_.DrawGlyph(Glyph::check, box, Paper::checkGlyph, DialogKit::White(1)); }
            else { auto* t = pc_->RenderTarget(); auto* br = pc_->SolidBrush(); br->SetColor(DialogKit::White(0x66 / 255.f)); t->DrawRoundedRectangle(D2D1::RoundedRect({box.left + .75f, box.top + .75f, box.right - .75f, box.bottom - .75f}, Paper::checkRadius - .75f, Paper::checkRadius - .75f), br, 1.5f); }
            kit_.Text(TR(print_backgrounds), DialogKit::Type::field, box.right + Paper::checkGap, row.top + (Paper::control - DialogKit::Type::field.line) / 2, row.right - box.right - Paper::checkGap, DialogKit::White(1));
            if (Focused(BackgroundsHit)) OuterRing(around, Paper::checkRowRadius, DialogKit::White(Paper::focusRing));
        }
        {   // Buttons: A1 rules; the disabled action at 40 % (state 2).
            const auto cv = motion_.Visual(CancelHit, now);
            kit_.Button(Find(CancelHit), TR(common_cancel), Kit::Tone::Secondary, std::max(cv.hover, offscreenDpi_ && hover_ == CancelHit ? 1.f : 0.f), cv.press, Focused(CancelHit), false);
            const bool on = Enabled(AcceptHit); const auto av = motion_.Visual(AcceptHit, now);
            Opacity(on ? 1 : Paper::disabledPrimary, [&] { kit_.Button(Find(AcceptHit), PrimaryLabel(), Kit::Tone::Primary, on ? std::max(av.hover, offscreenDpi_ && hover_ == AcceptHit ? 1.f : 0.f) : 0, on ? av.press : 0, on && Focused(AcceptHit), false); });
        }
    }
    void PaintList() {   // state 4
        if (!listOpen_) return;
        auto* t = pc_->RenderTarget();
        kit_.Fill(list_, Paper::radius, DialogKit::Gray(Design::Surface::canvas));
        t->PushAxisAlignedClip({list_.left + B, list_.top + B, list_.right - B, list_.bottom - B}, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        for (size_t i = 0; i < rows_.size(); ++i) {
            const auto& r = rows_[i];
            switch (r.kind) {
            case RowKind::Separator: kit_.Fill({r.r.left + Paper::listSeparatorX, r.r.top + Paper::listSeparatorY, r.r.right - Paper::listSeparatorX, r.r.top + Paper::listSeparatorY + 1}, 0, DialogKit::White(Paper::listSeparatorInk)); break;
            case RowKind::Note: kit_.Text(r.text, Paper::listText, r.r.left + Paper::listRowPad, r.r.top + Paper::listNotePadY, r.r.right - r.r.left - 2 * Paper::listRowPad, DialogKit::White(Paper::label)); break;
            case RowKind::Choice: case RowKind::Add: {
                if (static_cast<int>(i) == listHighlight_) kit_.Fill(r.r, Paper::listRowRadius, DialogKit::Gray(Design::Surface::hover));
                const bool add = r.kind == RowKind::Add, checked = !add && r.printer == state_.printer;
                const float trail = add ? Paper::listExternal : Paper::listCheck, tr = r.r.right - Paper::listRowPad - ((add || checked) ? trail + Paper::listRowGap : 0);
                kit_.Text(r.text, Paper::listText, r.r.left + Paper::listRowPad, r.r.top + (Paper::listRow - Paper::listText.line) / 2, tr - r.r.left - Paper::listRowPad, DialogKit::White(add ? Paper::subtitle : 1));
                if (checked) kit_.DrawGlyph(Glyph::check, {r.r.right - Paper::listRowPad - trail, r.r.top, r.r.right - Paper::listRowPad, r.r.bottom}, trail, DialogKit::White(1));
                if (add) kit_.DrawGlyph(Glyph::external, {r.r.right - Paper::listRowPad - trail, r.r.top, r.r.right - Paper::listRowPad, r.r.bottom}, trail, DialogKit::White(Paper::label));
                break;
            }
            }
        }
        t->PopAxisAlignedClip();
        kit_.Ring(list_, Paper::radius, DialogKit::White(Paper::borderInk));
    }
    void Paint() {
        if (pc_ == &chrome_ && !chrome_.RenderTarget()) chrome_.Resize();
        auto* t = pc_->RenderTarget(); if (!t || !pc_->SolidBrush()) return;
        const auto now = Motion::Now(); const float c = Design::Surface::canvas;
        Layout();
        t->BeginDraw(); t->Clear(D2D1::ColorF(c, c, c, 1));
        if (!narrow_) {
            // Preview column: #0F0F0F inside the border, clipped by the panel's inner corner (radius − border).
            t->PushAxisAlignedClip({B, B, B + Paper::previewWidth, height_ - B}, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            kit_.Fill({B, B, width_ - B, height_ - B}, Paper::radius - B, DialogKit::Rgb(Paper::previewFill));
            t->PopAxisAlignedClip();
            switch (PreviewMode()) { case Mode::Sheet: PaintSheetMode(now); break; case Mode::Grid: PaintGrid(); break; case Mode::Unavailable: PaintUnavailable(); break; }
        }
        PaintSettings(now); PaintList();
        kit_.Ring({0, 0, width_, height_}, Paper::radius, DialogKit::White(Paper::borderInk));
        if (t->EndDraw() == D2DERR_RECREATE_TARGET && pc_ == &chrome_) { kit_.Release(); for (auto& p : pages_) ReleasePage(p.second); chrome_.RecreateTarget(); }
        if (range_) NativeUI::EditBuffer::Flush(range_);
    }
    // ---- scrim -----------------------------------------------------------------------------------------
    void DropScrimSurface() { if (scrimDc_) { if (scrimOld_) SelectObject(scrimDc_, scrimOld_); DeleteDC(scrimDc_); } if (scrimBitmap_) DeleteObject(scrimBitmap_); scrimDc_ = nullptr; scrimBitmap_ = nullptr; scrimOld_ = nullptr; scrimW_ = scrimH_ = 0; scrimR_ = -1; }
    void UploadScrim(float alpha) {
        if (!scrim_) return;
        const int w = std::max<int>(1, area_.right - area_.left), h = std::max<int>(1, area_.bottom - area_.top), r = std::min({Px(corner_), w / 2, h});
        if (!scrimDc_ || w != scrimW_ || h != scrimH_ || r != scrimR_) {
            DropScrimSurface();
            BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(bi.bmiHeader); bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
            void* bits = nullptr; scrimDc_ = CreateCompatibleDC(nullptr); scrimBitmap_ = scrimDc_ ? CreateDIBSection(scrimDc_, &bi, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
            if (!scrimBitmap_ || !bits) { DropScrimSurface(); return; }
            scrimOld_ = SelectObject(scrimDc_, scrimBitmap_);
            auto* px = static_cast<UINT32*>(bits); std::fill(px, px + static_cast<size_t>(w) * h, 0xFF000000u);   // opaque black: the window's constant alpha sets the 50 %
            // Bottom corners: the page's rounded corners (radius r), antialiased by 4 × 4 supersampling.
            for (int y = h - r; y < h; ++y) for (int k = 0; k < r; ++k) {
                int inside = 0;
                for (int sy = 0; sy < 4; ++sy) for (int sx = 0; sx < 4; ++sx) {
                    const float dx = r - (k + (sx + .5f) / 4), dy = (y + (sy + .5f) / 4) - (h - r);
                    if (dx <= 0 || dy <= 0 || dx * dx + dy * dy <= static_cast<float>(r) * r) ++inside;
                }
                const UINT32 a = static_cast<UINT32>(std::lround(255.f * inside / 16)) << 24;
                px[static_cast<size_t>(y) * w + k] = a; px[static_cast<size_t>(y) * w + (w - 1 - k)] = a;
            }
            scrimW_ = w; scrimH_ = h; scrimR_ = r;
        }
        BLENDFUNCTION blend{AC_SRC_OVER, 0, static_cast<BYTE>(std::lround(255 * Paper::scrim * std::clamp(alpha, 0.f, 1.f))), AC_SRC_ALPHA}; scrimAlpha_ = blend.SourceConstantAlpha;
        POINT at{area_.left, area_.top}, zero{0, 0}; SIZE size{w, h};
        UpdateLayeredWindow(scrim_, nullptr, &at, &size, scrimDc_, &zero, 0, &blend, ULW_ALPHA);
    }
    // ---- window ------------------------------------------------------------------------------------------
    // The fade is read at the frame's own time (Frame(now)), never at a later clock reading.
    float Fade(Motion::Time now) const { return fade_.Value(now); }
    void Place(bool entering, Motion::Time now = Motion::Now()) {
        narrow_ = Dip(area_.right - area_.left) < Paper::narrowBelow; Layout();
        const float rise = entering && !reduced_ ? Paper::enterRise * (1 - Fade(now)) : 0;
        const int w = Px(width_), h = Px(height_), aw = area_.right - area_.left, ah = area_.bottom - area_.top;
        const int x = area_.left + (aw - w) / 2;
        int y = area_.top + Px(Paper::pageTop);
        if (y + h > area_.bottom - Px(Paper::pad)) y = area_.top + std::max(0, (ah - h) / 2);   // short page area: centred instead
        SetWindowPos(hwnd_, HWND_TOP, x, y + Px(rise), w, h, SWP_NOACTIVATE);
        chrome_.Resize(); PlaceRange(); InvalidateRect(hwnd_, nullptr, FALSE);
    }
    void ApplyAlpha(Motion::Time now = Motion::Now()) {
        const float a = Fade(now); UploadScrim(a); alpha_ = static_cast<BYTE>(std::lround(255 * a));
        SetLayeredWindowAttributes(hwnd_, 0, alpha_, LWA_ALPHA);
    }
    // ---- range field (EDIT child) ------------------------------------------------------------------------
    void MakeFont() {
        if (font_) DeleteObject(font_);
        font_ = CreateFontW(-Px(16), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Inter Display");
        if (range_) SendMessageW(range_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    }
    void PlaceRange() {
        if (!range_) return;
        ShowWindow(range_, state_.chooseRange ? SW_SHOWNA : SW_HIDE);
        if (!state_.chooseRange || !font_) return;
        HDC dc = GetDC(range_); auto old = SelectObject(dc, font_); TEXTMETRICW m{}; GetTextMetricsW(dc, &m); SelectObject(dc, old); ReleaseDC(range_, dc);
        const int lineH = m.tmHeight + m.tmExternalLeading; const auto r = RangeBox();
        MoveWindow(range_, Px(r.left + Paper::selectPad), Px(r.top) + (Px(Paper::control) - lineH) / 2, Px(r.right - r.left - 2 * Paper::selectPad), lineH, TRUE);
        field_.SetColor(FieldColor());
    }
    void CreateRange() {
        DestroyRange();
        range_ = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 1, 1, hwnd_, nullptr, instance_, nullptr);
        if (!range_) return;
        NativeUI::SelectAllOnCtrlA(range_);
        SendMessageW(range_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0); SendMessageW(range_, EM_SETLIMITTEXT, 200, 0);
        SetWindowTextW(range_, state_.ranges.c_str());
        field_.edit = range_; field_.placeholder.clear(); field_.password = false;   // no placeholder: the help line under the field says what to type
        DialogKit::EditField* f = &field_;
        NativeUI::EditBuffer::Attach(range_, [f](HWND h, HDC dc, const RECT& client) { DialogKit::DrawPlaceholder(*f, h, dc, client); });
        SetWindowSubclass(range_, EditProc, 0x5044, reinterpret_cast<DWORD_PTR>(this));
        MakeFont(); field_.SetColor(FieldColor());
    }
    void DestroyRange() { if (range_ && IsWindow(range_)) DestroyWindow(range_); range_ = nullptr; field_.edit = nullptr; if (field_.brush) DeleteObject(field_.brush); field_.brush = nullptr; rangeFocus_ = false; }
    void RefreshCaret() {   // state 1: a 1 x 16 white caret
        if (!range_ || GetFocus() != range_) return;
        field_.caret.Apply(range_, field_.color, RGB(255, 255, 255), std::max(1, Px(1)), Px(Paper::caretHeight));
    }
    void SetRangeText(const std::wstring& s) {   // thumbnails -> field (the field's EN_CHANGE updates the state)
        if (range_) { SetWindowTextW(range_, s.c_str()); SendMessageW(range_, EM_SETSEL, s.size(), s.size()); }
        else RangeChanged(s);
    }
    void RangeEdited() {
        if (!range_) return;
        const int n = GetWindowTextLengthW(range_); std::wstring s(static_cast<size_t>(n) + 1, L'\0'); GetWindowTextW(range_, s.data(), n + 1); s.resize(static_cast<size_t>(n));
        RangeChanged(s);
    }
    void RangeChanged(const std::wstring& s) {
        if (s == state_.ranges) return;
        state_.ranges = s; const int before = visible_.empty() ? -1 : visible_[static_cast<size_t>(index_)];
        Recompute(); auto at = std::find(visible_.begin(), visible_.end(), before); index_ = at == visible_.end() ? 0 : static_cast<int>(at - visible_.begin());
        field_.SetColor(FieldColor()); RefreshCaret(); Changed(false); RequestPages();
    }
    void Toggle(int page) {   // state 1: a thumbnail's box ticks the page, the field follows
        if (page < 0 || page >= total_ || PreviewMode() != Mode::Grid) return;
        auto ticked = Ticked(); auto it = std::find(ticked.begin(), ticked.end(), page);
        if (it == ticked.end()) { ticked.push_back(page); std::sort(ticked.begin(), ticked.end()); } else ticked.erase(it);
        SetRangeText(FormatRanges(ticked));
        if (uia_.Listening()) uia_.Raise(ThumbHit(page), UIA_ToggleToggleStatePropertyId);
    }
    // ---- focus / keys --------------------------------------------------------------------------------------
    std::vector<int> Order() const {
        std::vector<int> o;
        if (Enabled(PreviousHit)) o.push_back(PreviousHit);
        if (Enabled(NextHit)) o.push_back(NextHit);
        if (Enabled(GridStop)) o.push_back(GridStop);
        o.push_back(PrinterHit); o.push_back(state_.chooseRange ? ChoosePagesHit : AllPagesHit);
        if (state_.chooseRange && range_) o.push_back(RangeStop);
        o.push_back(state_.landscape ? LandscapeHit : PortraitHit); o.push_back(BackgroundsHit); o.push_back(CancelHit);
        if (Enabled(AcceptHit)) o.push_back(AcceptHit);
        return o;
    }
    int CurrentStop() const { return RangeHasFocus() && range_ ? RangeStop : focus_; }
    void FocusStop(int stop) {
        if (stop == RangeStop && range_) { focus_ = 0; if (headless_) rangeFocus_ = true; else { SetFocus(range_); SendMessageW(range_, EM_SETSEL, 0, -1); RefreshCaret(); } }
        else { focus_ = stop; if (headless_) rangeFocus_ = false; else if (hwnd_) SetFocus(hwnd_); }
        InvalidateRect(hwnd_, nullptr, FALSE);
        if (uia_.Listening() && stop > 0 && stop != RangeStop) uia_.Raise(stop == GridStop ? ThumbHit(gridFocus_) : stop, UIA_AutomationFocusChangedEventId);
    }
    void Cycle(bool back) {
        CloseList();
        const auto o = Order(); if (o.empty()) return;
        const auto it = std::find(o.begin(), o.end(), CurrentStop());
        int i = it == o.end() ? (back ? -1 : 0) : static_cast<int>(it - o.begin()) + (back ? -1 : 1);
        i = (i + static_cast<int>(o.size())) % static_cast<int>(o.size());
        keyboard_ = true; FocusStop(o[static_cast<size_t>(i)]);
    }
    int InitialStop() const { return Enabled(AcceptHit) ? AcceptHit : CancelHit; }
    void Interact() { motion_.SetInteraction(hover_, pressed_ == hover_ ? pressed_ : 0, Motion::Now()); field_.SetColor(FieldColor()); InvalidateRect(hwnd_, nullptr, FALSE); if (hooks_.wake) hooks_.wake(); }
    void Changed(bool document) {
        const bool wasNarrow = narrow_; const float wasHeight = height_;
        Layout(); if (hwnd_ && open_ && (narrow_ != wasNarrow || height_ != wasHeight)) Place(false);
        PlaceRange(); InvalidateRect(hwnd_, nullptr, FALSE);
        if (document && hooks_.changed) hooks_.changed(state_);
        if (uia_.Listening()) uia_.ChildrenChanged(GroupKey);
    }
    void Turn(int step) {
        const int next = std::clamp(index_ + step, 0, std::max(0, static_cast<int>(visible_.size()) - 1));
        if (next == index_) return;
        index_ = next; RequestPages(); InvalidateRect(hwnd_, nullptr, FALSE);
        if (!Enabled(focus_) && (focus_ == PreviousHit || focus_ == NextHit)) FocusStop(Enabled(PreviousHit) ? PreviousHit : NextHit);
        if (uia_.Listening()) { uia_.ChildrenChanged(GroupKey); uia_.Notify(GroupKey, PagerText().c_str(), L"PrintPreviewPage"); }
    }
    void ScrollGrid(float dy) {
        if (PreviewMode() != Mode::Grid) return;
        const auto inner = ColumnInner(); const float max = std::max(0.f, gridHeight_ - (inner.bottom - inner.top));
        const float s = std::clamp(scroll_ + dy, 0.f, max); if (s == scroll_) return;
        scroll_ = s; Layout(); RequestPages(); InvalidateRect(hwnd_, nullptr, FALSE);
    }
    void ShowGridFocus() {   // keep the focused thumbnail in view
        if (PreviewMode() != Mode::Grid) return; const auto inner = ColumnInner(); const float ih = ThumbItemHeight();
        const float rows = static_cast<float>(gridFocus_ / 2), top = rows * (ih + Paper::thumbRowGap), bottom = top + ih;
        if (gridHeight_ <= inner.bottom - inner.top) return;
        if (top < scroll_) ScrollGrid(top - scroll_); else if (bottom > scroll_ + (inner.bottom - inner.top)) ScrollGrid(bottom - scroll_ - (inner.bottom - inner.top));
    }
    void SetSegment(int hit) {
        switch (hit) {
        case AllPagesHit: case ChoosePagesHit: {
            const bool choose = hit == ChoosePagesHit; if (choose == state_.chooseRange) return;
            state_.chooseRange = choose; scroll_ = 0; Recompute(); focus_ = hit; Changed(false); RequestPages();
            if (choose && range_) { keyboard_ = true; FocusStop(RangeStop); }   // the field is there to be typed in
            break;
        }
        case PortraitHit: case LandscapeHit: { const bool l = hit == LandscapeHit; if (l == state_.landscape) return; state_.landscape = l; focus_ = hit; ClearPages(); total_ = -1; unavailable_ = false; Recompute(); Changed(true); break; }
        }
    }
    void Activate(int hit, bool keyboard) {
        keyboard_ = keyboard;
        if (!open_ || !Enabled(hit)) return;
        if (IsList(hit)) { ChooseRow(hit - ListBase); return; }
        if (IsThumb(hit)) { gridFocus_ = hit - ThumbBase; focus_ = GridStop; Toggle(hit - ThumbBase); return; }
        switch (hit) {
        case PreviousHit: Turn(-1); break;
        case NextHit: Turn(1); break;
        case PrinterHit: if (listOpen_) CloseList(); else OpenList(keyboard); break;
        case AllPagesHit: case ChoosePagesHit: case PortraitHit: case LandscapeHit: SetSegment(hit); break;
        case BackgroundsHit: state_.backgrounds = !state_.backgrounds; focus_ = hit; ClearPages(); total_ = -1; unavailable_ = false; Recompute(); Changed(true); if (uia_.Listening()) uia_.Raise(hit, UIA_ToggleToggleStatePropertyId); break;
        case GridStop: Toggle(gridFocus_); break;
        case CancelHit: Close(false); break;
        case AcceptHit: Close(true); break;
        }
    }
    bool ListKey(WPARAM key) {
        switch (key) {
        case VK_ESCAPE: case VK_TAB: CloseList(); FocusStop(PrinterHit); if (key == VK_TAB) Cycle(GetKeyState(VK_SHIFT) < 0); return true;
        case VK_DOWN: MoveHighlight(1); return true;
        case VK_UP: MoveHighlight(-1); return true;
        case VK_HOME: listHighlight_ = 0; MoveHighlight(0); return true;
        case VK_END: listHighlight_ = static_cast<int>(rows_.size()) - 1; MoveHighlight(0); if (!Enabled(ListHit(static_cast<size_t>(listHighlight_)))) MoveHighlight(-1); return true;
        case VK_RETURN: case VK_SPACE: case VK_F4: ChooseRow(listHighlight_); return true;
        }
        return true;   // the list keeps every key while open
    }
    bool Key(WPARAM key) {
        if (!open_) return false;
        if (listOpen_) return ListKey(key);
        const bool inEdit = RangeHasFocus() && range_;
        switch (key) {
        case VK_ESCAPE: Close(false); return true;
        case VK_TAB: Cycle(GetKeyState(VK_SHIFT) < 0); return true;
        case VK_RETURN: keyboard_ = true; if (inEdit || !focus_ || focus_ == BackgroundsHit || focus_ == GridStop) { if (focus_ == GridStop && !inEdit) Toggle(gridFocus_); else if (Enabled(AcceptHit)) Close(true); } else Activate(focus_, true); return true;
        case VK_SPACE: if (!inEdit && focus_) { Activate(focus_, true); return true; } return false;
        case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: {
            if (inEdit) return false;
            const bool forward = key == VK_RIGHT || key == VK_DOWN;
            if (focus_ == GridStop) { const int step = (key == VK_UP || key == VK_DOWN) ? 2 : 1; gridFocus_ = std::clamp(gridFocus_ + (forward ? step : -step), 0, std::max(0, total_ - 1)); keyboard_ = true; ShowGridFocus(); InvalidateRect(hwnd_, nullptr, FALSE); if (uia_.Listening()) uia_.Raise(ThumbHit(gridFocus_), UIA_AutomationFocusChangedEventId); return true; }
            if (focus_ == PrinterHit && (key == VK_DOWN || key == VK_UP)) { Activate(PrinterHit, true); return true; }
            if (key == VK_UP || key == VK_DOWN) return false;
            if (focus_ == AllPagesHit || focus_ == ChoosePagesHit) { keyboard_ = true; SetSegment(forward ? ChoosePagesHit : AllPagesHit); if (!state_.chooseRange) FocusStop(AllPagesHit); return true; }
            if (focus_ == PortraitHit || focus_ == LandscapeHit) { keyboard_ = true; SetSegment(forward ? LandscapeHit : PortraitHit); FocusStop(state_.landscape ? LandscapeHit : PortraitHit); return true; }
            if (focus_ == PreviousHit || focus_ == NextHit) { keyboard_ = true; Turn(forward ? 1 : -1); return true; }
            return false;
        }
        case VK_PRIOR: case VK_NEXT: if (inEdit) return false; keyboard_ = true; if (PreviewMode() == Mode::Grid) ScrollGrid((key == VK_NEXT ? 1 : -1) * (ColumnInner().bottom - ColumnInner().top)); else Turn(key == VK_NEXT ? 1 : -1); return true;
        case VK_F4: if (!inEdit && focus_ == PrinterHit) { Activate(PrinterHit, true); return true; } return false;
        }
        return false;
    }
    void Close(bool accepted) {
        if (!open_) return;
        listOpen_ = false;
        open_ = false;
        if (uia_.Listening()) uia_.Raise(GroupKey, UIA_Window_WindowClosedEventId);
        closing_ = true; settled_ = false; fade_.Set(0, Motion::Now(), Motion::Milliseconds(static_cast<int>(Paper::exitMs)), false, Motion::Curve::EaseOut);
        EnableWindow(hwnd_, FALSE);
        HWND back = restore_ && IsWindow(restore_) ? restore_ : owner_;
        if (!headless_) { if (owner_) SetForegroundWindow(owner_); if (back) SetFocus(back); }
        if (hooks_.wake) hooks_.wake();
        const State answer = state_;
        if (accepted) { if (hooks_.accept) hooks_.accept(answer); } else if (hooks_.cancel) hooks_.cancel();
    }
    void FinishClosing() {
        closing_ = false; fade_.Snap(0); settled_ = true;
        if (hwnd_) ShowWindow(hwnd_, SW_HIDE);
        if (scrim_) ShowWindow(scrim_, SW_HIDE);
        if (hwnd_) EnableWindow(hwnd_, TRUE);
        DestroyRange(); ClearPages(); DropScrimSurface();
    }
    // ---- accessibility ----------------------------------------------------------------------------------------
    RECT ScreenOf(D2D1_RECT_F r) const { RECT s{Px(r.left), Px(r.top), Px(r.right), Px(r.bottom)}; MapWindowPoints(hwnd_, nullptr, reinterpret_cast<POINT*>(&s), 2); return s; }
    std::vector<Accessibility::Node> Nodes() {
        std::vector<Accessibility::Node> nodes; if (!open_) return nodes;
        Layout();
        Accessibility::Node g; g.key = GroupKey; g.role = Accessibility::Role::Group; g.name = TR(print_title); g.help = Subtitle(); g.focusable = false; g.automationId = L"PrintDialog";
        RECT all{}; GetWindowRect(hwnd_, &all); g.bounds = all; nodes.push_back(g);
        auto add = [&](int key, const std::wstring& name, D2D1_RECT_F r, const wchar_t* id) { Accessibility::Node n; n.key = key; n.parent = GroupKey; n.name = name; n.bounds = ScreenOf(r); n.enabled = Enabled(key); n.automationId = id; nodes.push_back(n); return nodes.size() - 1; };
        auto text = [&](long long key, const std::wstring& name, D2D1_RECT_F r, const wchar_t* id, const std::wstring& help = L"") { Accessibility::Node s; s.key = key; s.parent = GroupKey; s.role = Accessibility::Role::Text; s.focusable = false; s.name = name; s.help = help; s.bounds = ScreenOf(r); s.automationId = id; nodes.push_back(s); };
        if (narrow_) text(ChipKey, TR(print_preview), chip_, L"PrintPreviewChip");
        else switch (PreviewMode()) {
        case Mode::Unavailable: text(SheetKey, TR(print_unavailable_title), sheet_, L"PrintPreviewSheet", TR(print_unavailable_text)); break;
        case Mode::Sheet:
            text(SheetKey, Loading() ? TR(print_preparing) : TR(print_preview), sheet_, L"PrintPreviewSheet");
            add(PreviousHit, TR(print_previous_page), Find(PreviousHit), L"PrintPreviousPage");
            { const auto p = Find(PreviousHit), q = Find(NextHit); text(PagerTextKey, PagerText(), {p.right, p.top, q.left, q.bottom}, L"PrintPageNumber"); }
            add(NextHit, TR(print_next_page), Find(NextHit), L"PrintNextPage");
            break;
        case Mode::Grid: {
            const auto ticked = Ticked(); int position = 0;
            for (const auto& b : boxes_) if (IsThumb(b.hit)) { const int page = b.hit - ThumbBase; const auto at = add(b.hit, TRF(print_page_n, std::to_wstring(page + 1)), b.r, L"PrintThumbnail");
                nodes[at].role = Accessibility::Role::ListItem; nodes[at].toggle = std::find(ticked.begin(), ticked.end(), page) != ticked.end() ? 2 : 1; nodes[at].position = ++position; nodes[at].count = total_; }
            break;
        }
        }
        { const auto at = add(PrinterHit, TR(print_printer), Find(PrinterHit), L"PrintPrinter"); nodes[at].expand = listOpen_ ? 2 : 1; if (state_.printer >= 0 && state_.printer < static_cast<int>(state_.printers.size())) nodes[at].value = state_.printers[static_cast<size_t>(state_.printer)]; }
        if (listOpen_) for (size_t i = 0; i < rows_.size(); ++i) {
            const auto& r = rows_[i]; if (r.kind == RowKind::Separator) continue;
            if (r.kind == RowKind::Note) { text(30000 + static_cast<long long>(i), r.text, r.r, L"PrintNoPrinter"); continue; }
            const auto at = add(ListHit(i), r.text, r.r, r.kind == RowKind::Add ? L"PrintAddPrinter" : L"PrintPrinterChoice"); nodes[at].role = Accessibility::Role::ListItem; nodes[at].selected = r.kind == RowKind::Choice && r.printer == state_.printer;
        }
        auto segment = [&](int hit, const std::wstring& name, bool on, int position, const wchar_t* id, const std::wstring& group) { const auto at = add(hit, name, Find(hit), id); nodes[at].role = Accessibility::Role::ListItem; nodes[at].selected = on; nodes[at].position = position; nodes[at].count = 2; nodes[at].help = group; };
        segment(AllPagesHit, TR(print_pages_all), !state_.chooseRange, 1, L"PrintPagesAll", TR(print_pages));
        segment(ChoosePagesHit, TR(print_pages_choose_on), state_.chooseRange, 2, L"PrintPagesChoose", TR(print_pages));
        if (state_.chooseRange) { const auto r = RangeBox(); text(31000, RangeBeyond() ? TRP(print_range_beyond, total_, std::to_wstring(total_)) : TR(print_range_help), {r.left, r.bottom + Paper::helpGap, r.right, r.bottom + Paper::helpGap + Paper::helpLine}, RangeBeyond() ? L"PrintRangeError" : L"PrintRangeHelp"); }
        segment(PortraitHit, TR(print_portrait), !state_.landscape, 1, L"PrintPortrait", TR(print_orientation));
        segment(LandscapeHit, TR(print_landscape), state_.landscape, 2, L"PrintLandscape", TR(print_orientation));
        { const auto at = add(BackgroundsHit, TR(print_backgrounds), Find(BackgroundsHit), L"PrintBackgrounds"); nodes[at].toggle = state_.backgrounds ? 2 : 1; }
        add(CancelHit, TR(common_cancel), Find(CancelHit), L"PrintCancel");
        add(AcceptHit, PrimaryLabel(), Find(AcceptHit), L"PrintAccept");
        return nodes;
    }
    // ---- window procedures ------------------------------------------------------------------------------------
    static LRESULT CALLBACK EditProc(HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
        auto* self = reinterpret_cast<Dialog*>(data);
        switch (msg) {
        case WM_NCDESTROY: RemoveWindowSubclass(h, EditProc, 0x5044); break;
        case WM_GETDLGCODE: return DefSubclassProc(h, msg, w, l) | DLGC_WANTALLKEYS;
        case WM_KEYDOWN: if (w == VK_RETURN && (l & (1 << 30))) return 0; if ((w == VK_ESCAPE || w == VK_TAB || w == VK_RETURN) && self->Key(w)) return 0; break;
        case WM_CHAR: if (w == L'\t' || w == L'\r' || w == 27) return 0; break;
        case WM_SETFOCUS: { const LRESULT r = DefSubclassProc(h, msg, w, l); self->CloseList(); self->focus_ = 0; self->rangeFocus_ = true; self->RefreshCaret(); InvalidateRect(self->hwnd_, nullptr, FALSE); return r; }
        case WM_KILLFOCUS: self->rangeFocus_ = false; InvalidateRect(self->hwnd_, nullptr, FALSE); break;
        case WM_LBUTTONDOWN: self->keyboard_ = false; break;
        case WM_MOUSEMOVE: if (self->hover_ != RangeStop) { self->hover_ = RangeStop; TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, h, 0}; TrackMouseEvent(&t); self->Interact(); } break;
        case WM_MOUSELEAVE: self->hover_ = 0; self->Interact(); break;
        }
        return DefSubclassProc(h, msg, w, l);
    }
    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<Dialog*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (msg == WM_NCCREATE) { self = static_cast<Dialog*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); self->hwnd_ = h; }
        return self ? self->Handle(h, msg, w, l) : DefWindowProcW(h, msg, w, l);
    }
    static LRESULT CALLBACK ScrimProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<Dialog*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (msg == WM_NCCREATE) { self = static_cast<Dialog*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); }
        switch (msg) {
        case WM_MOUSEACTIVATE: return MA_NOACTIVATEANDEAT;
        case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN:
            if (self && self->open_) { self->CloseList(); SetForegroundWindow(self->hwnd_); if (!self->focus_ && !(self->range_ && GetFocus() == self->range_)) self->FocusStop(self->InitialStop()); }
            return 0;
        case WM_SETCURSOR: SetCursor(LoadCursor(nullptr, IDC_ARROW)); return TRUE;
        }
        return DefWindowProcW(h, msg, w, l);
    }
    LRESULT Handle(HWND h, UINT msg, WPARAM w, LPARAM l) {
        const float scale = 96.f / Dpi();
        switch (msg) {
        case WM_GETOBJECT: { LRESULT result; if (uia_.OnGetObject(h, w, l, result)) return result; break; }
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); Paint(); EndPaint(h, &ps); return 0; }
        case WM_CTLCOLOREDIT: {
            if (reinterpret_cast<HWND>(l) != range_ || !range_) break;
            HDC dc = reinterpret_cast<HDC>(w); NativeUI::EditBuffer::Clip(range_, dc);
            SetTextColor(dc, RGB(255, 255, 255)); SetBkColor(dc, field_.color); SetBkMode(dc, OPAQUE);
            return reinterpret_cast<LRESULT>(field_.brush);
        }
        case WM_COMMAND: if (reinterpret_cast<HWND>(l) == range_ && HIWORD(w) == EN_CHANGE) RangeEdited(); return 0;
        case WM_MOUSEACTIVATE: return MA_ACTIVATE;
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, h, 0}; TrackMouseEvent(&t); int hit = HitAt(GET_X_LPARAM(l) * scale, GET_Y_LPARAM(l) * scale);
            if (listOpen_) { if (IsList(hit) && hit - ListBase != listHighlight_) { listHighlight_ = hit - ListBase; InvalidateRect(h, nullptr, FALSE); } return 0; }
            if (!Enabled(hit) || hit == ColumnHit || hit == GridStop) hit = 0; if (hit != hover_) { hover_ = hit; Interact(); } return 0;
        }
        case WM_MOUSELEAVE: hover_ = 0; Interact(); return 0;
        case WM_MOUSEWHEEL: { const float dy = -static_cast<float>(GET_WHEEL_DELTA_WPARAM(w)) / WHEEL_DELTA * 3 * 20;
            if (listOpen_) { listScroll_ += dy; LayoutList(); InvalidateRect(h, nullptr, FALSE); } else ScrollGrid(dy); return 0; }
        case WM_LBUTTONDOWN: {
            keyboard_ = false; int hit = HitAt(GET_X_LPARAM(l) * scale, GET_Y_LPARAM(l) * scale);
            if (listOpen_) { if (IsList(hit)) ChooseRow(hit - ListBase); else if (hit == -2) { CloseList(); FocusStop(PrinterHit); } return 0; }   // a click outside the list only closes it
            if (!Enabled(hit) || hit == ColumnHit || hit == GridStop) hit = 0; pressed_ = hit; if (hit) SetCapture(h);
            if (!hit && range_ && GetFocus() == range_) { focus_ = 0; SetFocus(h); } Interact(); return 0;
        }
        case WM_LBUTTONUP: { if (listOpen_) return 0; const int hit = HitAt(GET_X_LPARAM(l) * scale, GET_Y_LPARAM(l) * scale); const int was = pressed_; pressed_ = 0; if (GetCapture() == h) ReleaseCapture(); Interact(); if (was && was == hit) Activate(hit, false); return 0; }
        case WM_KEYDOWN: if ((l & (1 << 30)) && (w == VK_RETURN || w == VK_SPACE)) return 0; if (Key(w)) return 0; break;
        case WM_SYSKEYDOWN: if ((w == VK_DOWN || w == VK_UP) && focus_ == PrinterHit && open_) { if (!listOpen_) Activate(PrinterHit, true); return 0; } break;
        case WM_ACTIVATE: if (LOWORD(w) == WA_INACTIVE && listOpen_) CloseList(); break;
        case WM_SETFOCUS: InvalidateRect(h, nullptr, FALSE); return 0;
        case WM_DPICHANGED: if (open_) { MakeFont(); ClearPages(); Place(false); RequestPages(); } return 0;
        case WM_APP + 1: Activate(static_cast<int>(w), true); return 0;
        case WM_DESTROY: if (open_) { open_ = false; if (hooks_.cancel) hooks_.cancel(); } uia_.Detach(); return 0;
        }
        return DefWindowProcW(h, msg, w, l);
    }
public:
    ~Dialog() { Dispose(); }
    bool Create(HINSTANCE instance, HWND owner, const std::wstring& root, Hooks hooks) {
        instance_ = instance; owner_ = owner; hooks_ = std::move(hooks); root_ = root; motion_.EnablePolish(); chrome_.SetMotion(&motion_); kit_.Bind(&chrome_);
        reduced_ = Motion::PreferredReducedMotion(); motion_.SetReducedMotion(reduced_);
        WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = instance; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.lpszClassName = L"BrowserPrintDialog"; RegisterClassW(&wc);
        WNDCLASSW sc{}; sc.lpfnWndProc = ScrimProc; sc.hInstance = instance; sc.hCursor = LoadCursor(nullptr, IDC_ARROW); sc.lpszClassName = L"BrowserPrintScrim"; RegisterClassW(&sc);
        scrim_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE, sc.lpszClassName, L"", WS_POPUP, 0, 0, 1, 1, owner, nullptr, instance, this);
        CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED, wc.lpszClassName, TR(print_title), WS_POPUP | WS_CLIPCHILDREN, 0, 0, 1, 1, owner, nullptr, instance, this);
        if (!hwnd_ || !scrim_) return false;
        SetLayeredWindowAttributes(hwnd_, 0, 0, LWA_ALPHA);
        NativeUI::ConfigureShadowlessPopup(scrim_, 0);
        NativeUI::ConfigureShadowlessPopup(hwnd_, Paper::radius);
        Accessibility::Host host; host.window = hwnd_;
        host.nodes = [this] { return Nodes(); };
        host.focused = [this]() -> long long { return GetFocus() == hwnd_ ? (focus_ == GridStop ? ThumbHit(gridFocus_) : listOpen_ && listHighlight_ >= 0 ? ListHit(static_cast<size_t>(listHighlight_)) : focus_) : 0; };
        host.invoke = [this](long long key) { PostMessageW(hwnd_, WM_APP + 1, static_cast<WPARAM>(key), 0); };
        host.toggle = host.invoke; host.select = host.invoke;
        host.expand = [this](long long key, bool) { PostMessageW(hwnd_, WM_APP + 1, static_cast<WPARAM>(key), 0); };
        host.label = [this](HWND child, Accessibility::Node& n) { if (child != range_ || !range_) return false; n.name = TR(print_pages); n.help = TR(print_range_help); n.automationId = L"PrintPageRanges"; return true; };
        uia_.Attach(std::move(host));
        return SUCCEEDED(chrome_.Init(hwnd_, root));
    }
    void Dispose() {
        if (open_) { open_ = false; if (hooks_.cancel) hooks_.cancel(); }
        uia_.Detach(); DestroyRange(); ClearPages(); ReleaseGeometries(); kit_.Release(); chrome_.Dispose(); DropScrimSurface();
        if (font_) DeleteObject(font_);
        font_ = nullptr;
        if (hwnd_ && IsWindow(hwnd_)) DestroyWindow(hwnd_);
        if (scrim_ && IsWindow(scrim_)) DestroyWindow(scrim_);
        hwnd_ = scrim_ = nullptr; closing_ = false;
    }
    // `area`: the page area (screen pixels); `cornerDip`: its bottom corners' radius (0 when maximised).
    bool Show(State state, RECT area, float cornerDip) {
        if (!hwnd_ || open_) return false;
        if (closing_) FinishClosing();
        state_ = std::move(state); area_ = area; corner_ = cornerDip;
        total_ = -1; index_ = 0; gridFocus_ = 0; scroll_ = 0; visible_.clear(); ClearPages(); unavailable_ = false; listOpen_ = false;
        hover_ = pressed_ = focus_ = 0; motion_.ClearInteractions();
        keyboard_ = hooks_.keyboard ? hooks_.keyboard() : true; open_ = true;
        HWND f = GetFocus(); restore_ = f && f != hwnd_ && !IsChild(hwnd_, f) ? f : nullptr;
        EnableWindow(hwnd_, TRUE);
        CreateRange();
        const auto start = Motion::Now(); settled_ = false;
        fade_.Snap(0); fade_.Set(1, start, Motion::Milliseconds(static_cast<int>(Paper::enterMs)), reduced_, Motion::Curve::EaseOut);
        Place(true, start); ApplyAlpha(start);
        ShowWindow(scrim_, SW_SHOWNOACTIVATE);
        ShowWindow(hwnd_, SW_SHOW); SetForegroundWindow(hwnd_);
        FocusStop(InitialStop());
        if (uia_.Listening()) { uia_.ChildrenChanged(GroupKey); uia_.Raise(GroupKey, UIA_Window_WindowOpenedEventId); }
        if (hooks_.wake) hooks_.wake();
        return true;
    }
    void Abort() { if (open_) Close(false); }
    void Reposition(RECT area, float cornerDip) { area_ = area; corner_ = cornerDip; if (open_) { Place(false); ApplyAlpha(); RequestPages(); } }
    // The new document's page count (≥ 1), or -1 while it is being made (state 5: the sheet's grey shapes).
    void SetDocument(int total) {
        if (!open_) return;
        unavailable_ = false;
        if (total < 1) { ClearPages(); total_ = -1; }
        else { const bool fresh = total_ < 1; total_ = total; if (fresh) { index_ = 0; scroll_ = 0; } }
        Recompute(); field_.SetColor(FieldColor()); RefreshCaret(); Changed(false); RequestPages();
    }
    // State 6: the engine made no PDF of this document; it still prints.
    void SetUnavailable() { if (!open_) return; unavailable_ = true; total_ = -1; ClearPages(); Recompute(); field_.SetColor(FieldColor()); Changed(false); }
    // The destination's paper (portrait, mm): a new sheet shape.
    void SetPaper(float widthMm, float heightMm) { if (!open_ || (widthMm == state_.paperWidth && heightMm == state_.paperHeight)) return; state_.paperWidth = widthMm; state_.paperHeight = heightMm; ClearPages(); Layout(); InvalidateRect(hwnd_, nullptr, FALSE); }
    // One rendered page (0-based, premultiplied BGRA).
    void SetPage(int page, UINT width, UINT height, std::vector<BYTE> pixels) {
        if (!open_ || page < 0 || page >= total_ || !width || !height || pixels.size() < static_cast<size_t>(width) * height * 4) return;
        auto& p = pages_[page]; ReleasePage(p); p.width = width; p.height = height; p.pixels = std::move(pixels);
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    bool Visible() const { return open_; }
    bool Closing() const { return closing_; }
    HWND Window() const { return hwnd_; }
    HWND Scrim() const { return scrim_; }
    const State& Current() const { return state_; }
    int Total() const { return total_; }
    bool Unavailable() const { return unavailable_; }
    Motion::Wake NextWake(Motion::Time now) const {
        if (!open_ && !closing_) return {};
        Motion::Wake w = motion_.NextWake(now);
        if (fade_.Moving(now) || closing_ || !settled_) w = Motion::Earlier(w, Motion::Frame());
        return w;
    }
    // Every frame lands the fade at `now`, the settled value included: as Modal::Dialog, which applies its alpha on
    // every frame. Applying it only while the fade moved left the window at the last moving frame's opacity whenever
    // the next frame came after the fade's end.
    void Frame(Motion::Time now) {
        if (!hwnd_) return;
        if (closing_ && !fade_.Moving(now)) { FinishClosing(); return; }
        if (!open_ && !closing_) return;
        const bool moving = fade_.Moving(now);
        if (moving || !settled_) { Place(moving, now); ApplyAlpha(now); settled_ = !moving; }
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    void SetReducedMotion(bool reduced) { reduced_ = reduced; motion_.SetReducedMotion(reduced); if (open_) InvalidateRect(hwnd_, nullptr, FALSE); }
    // ---- tests (native harness; no window needed for the offscreen render) ----
    // Show's logic on the created, never shown windows: no ShowWindow, no focus or foreground change.
    bool OpenHeadlessForTest(State state, RECT area, float cornerDip) {
        if (!hwnd_ || open_) return false;
        headless_ = true; state_ = std::move(state); area_ = area; corner_ = cornerDip;
        total_ = -1; index_ = 0; gridFocus_ = 0; scroll_ = 0; visible_.clear(); ClearPages(); unavailable_ = false; listOpen_ = false; hover_ = pressed_ = focus_ = 0; motion_.ClearInteractions();
        keyboard_ = true; open_ = true; restore_ = nullptr; CreateRange(); fade_.Snap(1);
        narrow_ = Dip(area_.right - area_.left) < Paper::narrowBelow; Layout(); PlaceRange(); FocusStop(InitialStop());
        return true;
    }
    // Show's entrance (fade from 0 at `start`) on the hidden windows of OpenHeadlessForTest, then Frame(now) with the
    // test's own times: the panel's and the scrim's alphas and the panel's place are read back. As Show, it ends by
    // asking the host for frames (the wake hook: the browser's motion loop, tests/print/print_motion_test.cpp).
    void StartEntranceForTest(Motion::Time start) {
        settled_ = false; fade_.Snap(0); fade_.Set(1, start, Motion::Milliseconds(static_cast<int>(Paper::enterMs)), false, Motion::Curve::EaseOut);
        Place(true, start); ApplyAlpha(start);
        if (hooks_.wake) hooks_.wake();
    }
    BYTE AlphaForTest() const { BYTE a = 0; DWORD flags = 0; COLORREF key = 0; return hwnd_ && GetLayeredWindowAttributes(hwnd_, &key, &a, &flags) ? a : alpha_; }
    BYTE ScrimAlphaForTest() const { return scrimAlpha_; }
    RECT PanelRectForTest() const { RECT r{}; if (hwnd_) GetWindowRect(hwnd_, &r); return r; }
    bool SettledForTest() const { return settled_; }
    void FinishAnimationsForTest() { fade_.Settle(); if (closing_) FinishClosing(); else if (open_) { Place(false); ApplyAlpha(); } }
    void KeyForTest(WPARAM key) { Key(key); }
    void ActivateForTest(int hit, bool keyboard = false) { Activate(hit, keyboard); }
    void SetRangeForTest(const std::wstring& s) { if (range_) SetWindowTextW(range_, s.c_str()); else RangeChanged(s); }
    void WheelForTest(float dy) { if (listOpen_) { listScroll_ += dy; LayoutList(); } else ScrollGrid(dy); }
    int FocusForTest() const { return CurrentStop(); }
    int PageIndexForTest() const { return index_; }
    int GridFocusForTest() const { return gridFocus_; }
    std::vector<int> VisibleForTest() const { return visible_; }
    std::vector<int> TickedForTest() const { return Ticked(); }
    bool EnabledForTest(int hit) const { return Enabled(hit); }
    D2D1_RECT_F BoxForTest(int hit) { Layout(); if (IsList(hit)) { const size_t i = static_cast<size_t>(hit - ListBase); return i < rows_.size() ? rows_[i].r : D2D1_RECT_F{}; } return Find(hit); }
    D2D1_RECT_F SheetForTest() { Layout(); return sheet_; }
    D2D1_RECT_F RangeBoxForTest() { Layout(); return RangeBox(); }
    D2D1_RECT_F ListForTest() { Layout(); return list_; }
    D2D1_RECT_F ChipForTest() { Layout(); return chip_; }
    SIZE SizeForTest() { Layout(); return {static_cast<LONG>(width_), static_cast<LONG>(height_)}; }
    Mode ModeForTest() const { return PreviewMode(); }
    bool NarrowForTest() const { return narrow_; }
    bool ListOpenForTest() const { return listOpen_; }
    std::vector<std::wstring> ListTextsForTest() const { std::vector<std::wstring> v; for (auto& r : rows_) if (r.kind != RowKind::Separator) v.push_back(r.text); return v; }
    std::wstring SubtitleForTest() const { return Subtitle(); }
    std::wstring PagerForTest() const { return PagerText(); }
    std::wstring PrimaryForTest() const { return PrimaryLabel(); }
    bool RangeErrorForTest() const { return RangeError(); }
    bool RangeBeyondForTest() const { return RangeBeyond(); }
    std::vector<Accessibility::Node> NodesForTest() { return Nodes(); }
    // The panel as it would paint for `state`, `total` pages (-1: being prepared) and the page images given, into a
    // PNG at `dpi` (offscreen Direct2D: no window, works on a locked desktop).
    bool RenderForTest(const std::wstring& root, const std::wstring& png, UINT dpi, State state, int total, int index,
                       const std::map<int, std::pair<SIZE, std::vector<BYTE>>>& images = {}, int focus = 0) { Render r; r.focus = focus; return RenderForTest(root, png, dpi, std::move(state), total, index, images, r); }
    bool RenderForTest(const std::wstring& root, const std::wstring& png, UINT dpi, State state, int total, int index,
                       const std::map<int, std::pair<SIZE, std::vector<BYTE>>>& images, const Render& o) {
        const State saved = state_; const int savedTotal = total_, savedIndex = index_, savedFocus = focus_, savedHover = hover_, savedHighlight = listHighlight_;
        const bool savedOpen = open_, savedKeyboard = keyboard_, savedNarrow = narrow_, savedUnavailable = unavailable_, savedList = listOpen_, savedRangeFocus = rangeFocus_; const float savedScroll = scroll_;
        HWND savedRange = range_; range_ = nullptr;
        offscreenDpi_ = dpi; state_ = std::move(state); total_ = total; index_ = index; narrow_ = o.narrow; unavailable_ = o.unavailable; open_ = true; keyboard_ = o.focus != 0; focus_ = o.focus; hover_ = o.hover; rangeFocus_ = o.rangeFocus; scroll_ = o.scroll;
        Recompute(); if (o.listOpen) { BuildRows(); listOpen_ = true; listScroll_ = 0; } else listOpen_ = false;
        Layout(); listHighlight_ = o.listHighlight;
        if (listOpen_ && listHighlight_ < 0) for (size_t i = 0; i < rows_.size(); ++i) if (rows_[i].kind == RowKind::Choice && rows_[i].printer == state_.printer) listHighlight_ = static_cast<int>(i);
        Chrome off; bool ok = false;
        if (SUCCEEDED(off.InitOffscreen(root, dpi, width_, height_))) {
            kit_.Release(); ReleaseGeometries(); kit_.Bind(&off); pc_ = &off;
            for (const auto& im : images) { auto& p = pages_[im.first]; ReleasePage(p); p.width = static_cast<UINT>(im.second.first.cx); p.height = static_cast<UINT>(im.second.first.cy); p.pixels = im.second.second; }
            Paint();
            ok = SUCCEEDED(off.SavePng(png.c_str()));
            ClearPages(); kit_.Release(); ReleaseGeometries(); kit_.Bind(&chrome_); pc_ = &chrome_; off.Dispose();
        }
        offscreenDpi_ = 0; range_ = savedRange;
        state_ = saved; total_ = savedTotal; index_ = savedIndex; open_ = savedOpen; keyboard_ = savedKeyboard; focus_ = savedFocus; hover_ = savedHover; narrow_ = savedNarrow; unavailable_ = savedUnavailable; listOpen_ = savedList; listHighlight_ = savedHighlight; rangeFocus_ = savedRangeFocus; scroll_ = savedScroll;
        Recompute(); if (open_) Layout();
        return ok;
    }
};
}   // namespace PrintDialog
}   // namespace NativeChrome
