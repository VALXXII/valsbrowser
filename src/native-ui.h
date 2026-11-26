#pragma once
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>
#include <string>
#include <dwmapi.h>
#include <commctrl.h>
#include <functional>
namespace NativeUI {
// Typing flicker (« quand on écrit […] le texte clignote un peu », tests/ui/edit_flicker_test.cpp).
// A native EDIT draws straight on the screen, in steps a compositor can show: its background erased
// before the text comes back (WM_ERASEBKGND then WM_PAINT; a multi-line EDIT erases visibly), the
// text typed before its parent answers EN_CHANGE (an inline completion appended a moment later),
// then a placeholder its owner paints over it afterwards through GetDC. EditBuffer makes every draw
// of the field one finished image:
//   - the parent's WM_CTLCOLOREDIT calls Clip(): the client area is excluded from the DC the EDIT is
//     about to draw with, so none of its own drawing reaches the screen;
//   - once the outermost message the EDIT is handling returns (a keystroke with everything its
//     EN_CHANGE caused, a selection drag step, a WM_SETTEXT...), or in its WM_PAINT, the EDIT draws
//     itself again into a memory bitmap (WM_PRINTCLIENT, erase included), the overlay (placeholder)
//     is drawn on it, and the result is copied to the screen in one BitBlt, caret hidden meanwhile.
// Nothing about the EDIT's look changes: same pixels, its own rendering, colours from the same
// WM_CTLCOLOREDIT. Text, caret, selection, IME, undo, clipboard and UIA stay the EDIT's own.
// With an overlay, the field is also recomposed when it becomes empty or stops being empty.
// Flush(): paint a pending update now (the parent has just repainted the field around it: the EDIT's
// colour must not lag behind it until the next turn of the message loop).
class EditBuffer {
public:
    using Overlay = std::function<void(HWND edit, HDC dc, const RECT& client)>;
    static bool Attach(HWND edit, Overlay overlay = nullptr) {
        if (!edit || Get(edit)) return false;
        auto* s = new State; s->overlay = std::move(overlay); s->empty = GetWindowTextLengthW(edit) == 0;
        if (!SetPropW(edit, prop, s)) { delete s; return false; }
        if (!SetWindowSubclass(edit, Proc, subclassId, reinterpret_cast<DWORD_PTR>(s))) { RemovePropW(edit, prop); delete s; return false; }
        return true;
    }
    static bool Attached(HWND edit) { return Get(edit) != nullptr; }
    // From the parent's WM_CTLCOLOREDIT (any EDIT: those not attached are left alone).
    static void Clip(HWND edit, HDC dc) {
        State* s = Get(edit);
        if (!s || s->composing || s->bypass) return;
        RECT client{}; GetClientRect(edit, &client);
        ExcludeClipRect(dc, client.left, client.top, client.right, client.bottom);
        s->drew = true;
    }
    static void Flush(HWND edit) { if (edit && Get(edit)) RedrawWindow(edit, nullptr, nullptr, RDW_UPDATENOW); }
    // The field needs a new image (its colours changed): composed now, no erase on the screen.
    static void Redraw(HWND edit) { if (!edit) return; InvalidateRect(edit, nullptr, Get(edit) ? FALSE : TRUE); Flush(edit); }
private:
    static constexpr UINT_PTR subclassId = 0xEDB0;
    static constexpr wchar_t prop[] = L"NativeUI.EditBuffer";
    struct State { Overlay overlay; bool composing = false, drew = false, bypass = false, empty = true; int depth = 0; };
    static State* Get(HWND edit) { return edit ? static_cast<State*>(GetPropW(edit, prop)) : nullptr; }
    // Asked as a nested message (depth > 0), so the question itself never triggers another check.
    static bool Empty(HWND h, State& s) { ++s.depth; const bool empty = GetWindowTextLengthW(h) == 0; --s.depth; return empty; }
    static bool Compose(HWND h, State& s, HDC dc) {
        RECT client{}; GetClientRect(h, &client);
        const int w = client.right, ht = client.bottom;
        if (w <= 0 || ht <= 0) return true;
        BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(bi.bmiHeader); bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -ht; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
        void* bits = nullptr;
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bitmap = mem ? CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
        if (!bitmap) {   // no memory: the EDIT repaints itself natively once rather than leave a stale field
            if (mem) DeleteDC(mem);
            s.bypass = true; InvalidateRect(h, nullptr, TRUE); return false;
        }
        HGDIOBJ old = SelectObject(mem, bitmap);
        s.composing = true;
        SendMessageW(h, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(mem), PRF_CLIENT | PRF_ERASEBKGND);
        if (s.overlay) s.overlay(h, mem, client);
        s.composing = false;
        GdiFlush();
        BitBlt(dc, 0, 0, w, ht, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old); DeleteObject(bitmap); DeleteDC(mem);
        return true;
    }
    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
        auto* s = reinterpret_cast<State*>(data);
        if (msg == WM_NCDESTROY) { RemoveWindowSubclass(h, Proc, subclassId); RemovePropW(h, prop); delete s; return DefSubclassProc(h, msg, w, l); }
        if (s->composing) return DefSubclassProc(h, msg, w, l);   // our own WM_PRINTCLIENT pass, into memory
        if (s->bypass) {   // one native paint after an allocation failure
            if (msg == WM_PAINT) { const LRESULT r = DefSubclassProc(h, msg, w, l); s->bypass = false; return r; }
            return DefSubclassProc(h, msg, w, l);
        }
        if (msg == WM_ERASEBKGND) return 1;   // the composed image carries the erase
        if (msg == WM_PAINT && !w) {
            // Our BeginPaint (caret hidden until EndPaint). The EDIT runs its own paint on a DC that
            // shows nothing (its state stays its own), then the composed field is copied in one go.
            ++s->depth;
            PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
            const int saved = SaveDC(dc);
            RECT client{}; GetClientRect(h, &client); ExcludeClipRect(dc, client.left, client.top, client.right, client.bottom);
            const LRESULT r = DefSubclassProc(h, msg, reinterpret_cast<WPARAM>(dc), l);
            RestoreDC(dc, saved);
            --s->depth;
            s->drew = false; s->empty = Empty(h, *s);
            if (!Compose(h, *s, dc)) { EndPaint(h, &ps); return r; }
            EndPaint(h, &ps);
            return r;
        }
        const bool print = msg == WM_PRINTCLIENT || msg == WM_PRINT;
        const int saved = print && w ? SaveDC(reinterpret_cast<HDC>(w)) : 0;
        ++s->depth;
        const LRESULT r = DefSubclassProc(h, msg, w, l);
        --s->depth;
        if (print) { if (saved) RestoreDC(reinterpret_cast<HDC>(w), saved); if (s->drew) { s->drew = false; Compose(h, *s, reinterpret_cast<HDC>(w)); } return r; }
        if (s->depth == 0 && IsWindow(h)) {
            if (s->overlay) { const bool empty = Empty(h, *s); if (empty != s->empty) { s->empty = empty; if (IsWindowVisible(h)) s->drew = true; } }
            if (s->drew) {
                s->drew = false;
                const bool caret = GetFocus() == h; if (caret) HideCaret(h);
                HDC dc = GetDC(h); Compose(h, *s, dc); ReleaseDC(h, dc);
                if (caret) ShowCaret(h);
            }
        }
        return r;
    }
};
// EDIT controls paint their own background. Match the parent's loading fill in
// client pixels, including when the moving edge passes through the text field.
class AddressBackground {
    HBRUSH brush_=nullptr;
    int width_=0,edge_=-1; COLORREF base_=0,fill_=0;
public:
    ~AddressBackground(){if(brush_)DeleteObject(brush_);}
    HBRUSH Brush(int width,int edge,COLORREF base,COLORREF fill){
        width=width>0?width:1;edge=edge<0?0:edge>width?width:edge;
        if(brush_&&width==width_&&edge==edge_&&base==base_&&fill==fill_)return brush_;
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=1;
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
        void* pixels=nullptr;HBITMAP bitmap=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
        if(!bitmap)return (HBRUSH)GetStockObject(DC_BRUSH);
        auto rgb=[](COLORREF c){return DWORD(GetBValue(c))|(DWORD(GetGValue(c))<<8)|(DWORD(GetRValue(c))<<16);};
        for(int x=0;x<width;++x)((DWORD*)pixels)[x]=rgb(x<edge?fill:base);
        HBRUSH next=CreatePatternBrush(bitmap);DeleteObject(bitmap);
        if(!next)return (HBRUSH)GetStockObject(DC_BRUSH);
        if(brush_)DeleteObject(brush_);
        brush_=next;width_=width;edge_=edge;base_=base;fill_=fill;
        return brush_;
    }
};

// Coloured text caret for a native EDIT (Paper: 1 DIP, accent #3E67FF at 90%, danger
// while a field is invalid). Windows inverts the caret bitmap over the field (XOR), so
// over the field's known flat surface `background` the visible colour is exactly `color`.
// The EDIT recreates its caret on focus: call this after WM_SETFOCUS and whenever the
// colour changes (validity). One bitmap per call; the previous one is released.
class AccentCaret {
    HBITMAP bitmap_=nullptr;
public:
    ~AccentCaret(){if(bitmap_)DeleteObject(bitmap_);}
    static COLORREF Mix(COLORREF over,COLORREF color,float alpha){
        auto c=[alpha](int b,int f){return (int)(b+(f-b)*alpha+.5f);};
        return RGB(c(GetRValue(over),GetRValue(color)),c(GetGValue(over),GetGValue(color)),c(GetBValue(over),GetBValue(color)));
    }
    void Apply(HWND edit,COLORREF background,COLORREF color,int width,int height){
        if(!edit||GetFocus()!=edit)return;
        width=width<1?1:width;height=height<1?1:height;
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
        void* pixels=nullptr;HDC dc=GetDC(edit);HBITMAP next=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);ReleaseDC(edit,dc);
        if(!next||!pixels)return;
        const COLORREF x=color^background;const DWORD bgr=DWORD(GetBValue(x))|(DWORD(GetGValue(x))<<8)|(DWORD(GetRValue(x))<<16);
        for(int i=0;i<width*height;++i)((DWORD*)pixels)[i]=bgr;
        DestroyCaret();
        if(!CreateCaret(edit,next,width,height)){DeleteObject(next);return;}
        if(bitmap_)DeleteObject(bitmap_);
        bitmap_=next;
        // The EDIT placed its own caret already: keep its position, show ours.
        ShowCaret(edit);
        DWORD start=0,end=0;SendMessageW(edit,EM_GETSEL,(WPARAM)&start,(LPARAM)&end);SendMessageW(edit,EM_SETSEL,start,end);
    }
};
// Selection highlight of a native single-line EDIT in a colour of ours (the address field follows
// the window's profile). The EDIT has no per-control selection colour: it paints the selected text
// in the system COLOR_HIGHLIGHT / COLOR_HIGHLIGHTTEXT, during WM_PAINT and also directly (EM_SETSEL,
// mouse drag, double-click), and asks its parent for colours (WM_CTLCOLOREDIT, same HDC) before
// EVERY draw. Measured (design/profile-address-selection-report.md): its highlight geometry is its
// own Uniscribe layout (bidi runs reordered visually, fallback fonts, scroll offset, one inclusive
// column) and cannot be recomputed exactly outside it; EM_POSFROMCHAR also wraps at 16 bits on long
// scrolled text. So the geometry is never recomputed here — the EDIT draws it:
//   1. Clip(), from the parent's WM_CTLCOLOREDIT while the field has the focus: excludes the whole
//      client area from that DC, so nothing the EDIT draws there reaches the screen;
//   2. once the EDIT's draw is over, Compose() asks the EDIT to draw itself again (WM_PRINTCLIENT,
//      documented) into a memory DIB of the client size — exactly what it would have shown, native
//      shaping, bidi, scroll and highlight included — then remaps ONLY the highlight pixels and copies
//      the result to the screen DC (WM_PAINT: our BeginPaint DC; direct draws: GetDC, caret hidden).
// Remap: a highlight pixel is COLOR_HIGHLIGHT (the cell) or an antialiased blend toward
// COLOR_HIGHLIGHTTEXT (a glyph inside it), i.e. p = H + a (T - H) channel by channel. It becomes
// fill + a (text - fill): the same coverage, our colours. Only columns that contain the exact cell
// colour H (plus columns entirely made of such blends between them) are touched. Nothing is read from
// the screen.
// That classification is only sound if no ORDINARY pixel of the field can look like a highlight
// pixel. Clip() records the field's ordinary palette from the parent's DC (its text colour, its
// background colour, plus any other surface the parent's brush paints, passed by the caller); the
// remap runs only if neither those colours nor any antialiased blend between the text and a surface
// lies within tolerance of the H -> T line (Separable). Otherwise — e.g. a legal black / white system
// pair against a grey field — the EDIT keeps its native highlight, untouched (independent review
// 2026-11-15).
// Selection range, caret, text, IME, undo, clipboard and UIA stay the EDIT's own.
// High contrast (forced colours), or a system pair the remap cannot separate: native highlight.
class EditSelection {
public:
    struct Colors { COLORREF fill = 0, text = 0; };
    // The colours the EDIT itself paints when nothing is selected.
    struct Ordinary { COLORREF ink = CLR_INVALID; COLORREF surfaces[2] = {CLR_INVALID, CLR_INVALID}; };
    struct Pair { COLORREF highlight = 0, highlightText = 0; };
    using ColorSource = Colors (*)();
    using Policy = bool (*)();   // true: forced colours, leave the native highlight
    using PairSource = Pair (*)();   // the system selection pair (injectable for tests; never set by us)
    static bool SystemForcedColors() {
        HIGHCONTRASTW hc{}; hc.cbSize = sizeof(hc);
        return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) && (hc.dwFlags & HCF_HIGHCONTRASTON);
    }
    static Pair SystemPair() { return {GetSysColor(COLOR_HIGHLIGHT), GetSysColor(COLOR_HIGHLIGHTTEXT)}; }
    bool Attach(HWND edit, ColorSource colors, Policy forced = SystemForcedColors, PairSource pair = SystemPair) {
        if (!edit || !colors) return false;
        edit_ = edit; colors_ = colors; forced_ = forced ? forced : SystemForcedColors; pair_ = pair ? pair : SystemPair;
        return SetWindowSubclass(edit, Proc, subclassId, reinterpret_cast<DWORD_PTR>(this)) != FALSE;
    }
    // From the parent's WM_CTLCOLOREDIT for this edit, AFTER it set the text and background colours
    // on `dc`; `otherSurface`: a second colour its returned brush paints (the loading fill), if any.
    void Clip(HWND edit, HDC dc, COLORREF otherSurface = CLR_INVALID) {
        if (edit != edit_) return;
        ordinary_.ink = GetTextColor(dc); ordinary_.surfaces[0] = GetBkColor(dc); ordinary_.surfaces[1] = otherSurface;
        // Every draw of the focused field is composed, not only those showing a selection: a keystroke
        // that replaces the selected inline completion must not show the typed text alone before the
        // new completion comes back (EditBuffer, the same double buffering). The remap itself still
        // runs only on a visible selection the remap can tell apart (Compose).
        if (composing_ || GetFocus() != edit_) return;
        if (bypass_) return;   // until the next WM_PAINT is over (Compose could not allocate)
        RECT client{}; GetClientRect(edit_, &client);
        ExcludeClipRect(dc, client.left, client.top, client.right, client.bottom);
        drew_ = true;
    }
    // The colour changed (profile switch, a step of the profile fade): repaint now.
    void Refresh() { if (edit_ && Active() && HasSelection()) InvalidateRect(edit_, nullptr, FALSE); }
    // Draws the field (EDIT's own rendering, selection in our colours) on `dc`, client coordinates.
    // Called whenever Clip() kept the EDIT's own draw off the screen: the field is always redrawn here,
    // even if the selection is gone by now (Ctrl+Shift+Left collapsing it while the EDIT was drawing).
    bool Compose(HDC dc) {
        if (!edit_) return false;
        RECT client{}; GetClientRect(edit_, &client);
        const int w = client.right, h = client.bottom;
        if (w <= 0 || h <= 0) return false;
        BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(bi.bmiHeader); bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
        void* bits = nullptr;
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bitmap = mem ? CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
        if (!bitmap || !bits) {   // no memory: let the EDIT repaint itself natively once (system highlight) rather than leave a stale frame
            if (bitmap) DeleteObject(bitmap);
            if (mem) DeleteDC(mem);
            bypass_ = true; InvalidateRect(edit_, nullptr, TRUE); return false;
        }
        HGDIOBJ old = SelectObject(mem, bitmap);
        composing_ = true;
        SendMessageW(edit_, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(mem), PRF_CLIENT | PRF_ERASEBKGND);
        composing_ = false;
        GdiFlush();
        if (Active() && HasSelection()) Remap(static_cast<DWORD*>(bits), w, h, colors_(), pair_(), ordinary_);
        BitBlt(dc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old); DeleteObject(bitmap); DeleteDC(mem);
        return true;
    }
    bool Active() const { return edit_ && GetFocus() == edit_ && !forced_() && Separable(pair_(), ordinary_); }
    // True when highlight pixels can be told apart from every ordinary pixel of the field: H and T far
    // enough apart to measure coverage, and neither the ink, a surface nor any antialiased blend of the
    // ink over a surface within a wide tolerance of the H -> T line. Unknown palette: false.
    static bool Separable(Pair pair, const Ordinary& o) {
        if (Spread(pair.highlight, pair.highlightText) < minSpread || o.ink == CLR_INVALID || o.surfaces[0] == CLR_INVALID) return false;
        for (COLORREF s : o.surfaces) {
            if (s == CLR_INVALID) continue;
            for (int step = 0; step <= 64; ++step) {
                const double t = step / 64.0;
                auto mix = [t](int a, int b) { return static_cast<int>(std::lround(a + (b - a) * t)); };
                const COLORREF p = RGB(mix(GetRValue(s), GetRValue(o.ink)), mix(GetGValue(s), GetGValue(o.ink)), mix(GetBValue(s), GetBValue(o.ink)));
                if (Coverage(pair, p, ambiguityTolerance) >= 0) return false;
            }
        }
        return true;
    }
    // Coverage of T over H in pixel `p` (0..1), or -1 when `p` is not such a blend within `tolerance`.
    static double Coverage(Pair pair, COLORREF p, double tolerance = 2.5) {
        const int h[3] = {GetRValue(pair.highlight), GetGValue(pair.highlight), GetBValue(pair.highlight)};
        const int t[3] = {GetRValue(pair.highlightText), GetGValue(pair.highlightText), GetBValue(pair.highlightText)};
        const int c[3] = {GetRValue(p), GetGValue(p), GetBValue(p)};
        int k = 0; for (int i = 1; i < 3; ++i) if (std::abs(t[i] - h[i]) > std::abs(t[k] - h[k])) k = i;   // the channel that separates H from T best
        if (t[k] == h[k]) return -1;
        const double a = static_cast<double>(c[k] - h[k]) / (t[k] - h[k]);
        const double slack = tolerance / std::abs(t[k] - h[k]);
        if (a < -slack || a > 1 + slack) return -1;
        for (int i = 0; i < 3; ++i) if (std::abs(h[i] + a * (t[i] - h[i]) - c[i]) > tolerance) return -1;
        return std::clamp(a, 0.0, 1.0);
    }
    // The remap, exposed for tests: `px` is a top-down 0x00RRGGBB client image of the EDIT drawn with
    // the selection pair `pair`. Does nothing (false) unless Separable(pair, ordinary).
    static bool Remap(DWORD* px, int w, int h, Colors c, Pair pair, const Ordinary& ordinary) {
        if (!Separable(pair, ordinary)) return false;
        const int fc[3] = {GetRValue(c.fill), GetGValue(c.fill), GetBValue(c.fill)}, xc[3] = {GetRValue(c.text), GetGValue(c.text), GetBValue(c.text)};
        auto pixel = [](DWORD v) { return RGB((v >> 16) & 255, (v >> 8) & 255, v & 255); };
        // Columns: 2 = holds the cell colour, 1 = only blends (a glyph column inside the cell), 0 = other.
        std::vector<int> kind(w, 0);
        for (int x = 0; x < w; ++x) {
            bool cell = false, any = false;
            for (int y = 0; y < h; ++y) {
                const COLORREF v = pixel(px[y * w + x]);
                if (v == pair.highlight) cell = true;
                any = any || Coverage(pair, v) >= 0;
            }
            kind[x] = cell ? 2 : any ? 1 : 0;
        }
        // Selected columns: runs of cell / blend columns that contain at least one cell column.
        std::vector<bool> selected(w, false);
        for (int x = 0; x < w;) {
            if (!kind[x]) { ++x; continue; }
            int end = x; bool cell = false;
            while (end < w && kind[end]) { cell = cell || kind[end] == 2; ++end; }
            if (cell) for (int i = x; i < end; ++i) selected[i] = true;
            x = end;
        }
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            if (!selected[x]) continue;
            DWORD& v = px[y * w + x];
            const double a = Coverage(pair, pixel(v));
            if (a < 0) continue;
            int o[3]; for (int i = 0; i < 3; ++i) o[i] = static_cast<int>(std::lround(fc[i] + a * (xc[i] - fc[i])));
            v = (static_cast<DWORD>(o[0]) << 16) | (static_cast<DWORD>(o[1]) << 8) | static_cast<DWORD>(o[2]);
        }
        return true;
    }
private:
    static constexpr UINT_PTR subclassId = 0x5E1C;
    static constexpr int minSpread = 96;                  // H and T must differ enough on one channel to measure coverage
    static constexpr double ambiguityTolerance = 6;       // > the 2.5 classification tolerance: margin for GDI rounding
    HWND edit_ = nullptr; ColorSource colors_ = nullptr; Policy forced_ = SystemForcedColors; PairSource pair_ = SystemPair;
    Ordinary ordinary_{};
    bool drew_ = false, composing_ = false, bypass_ = false; int depth_ = 0;
    static int Spread(COLORREF a, COLORREF b) { return (std::max)({std::abs(GetRValue(a) - GetRValue(b)), std::abs(GetGValue(a) - GetGValue(b)), std::abs(GetBValue(a) - GetBValue(b))}); }
    bool HasSelection() const { DWORD start = 0, end = 0; SendMessageW(edit_, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end)); return start != end; }
    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
        auto* self = reinterpret_cast<EditSelection*>(data);
        if (msg == WM_NCDESTROY) { RemoveWindowSubclass(h, Proc, subclassId); self->edit_ = nullptr; return DefSubclassProc(h, msg, w, l); }
        if (self->composing_) return DefSubclassProc(h, msg, w, l);   // our own WM_PRINTCLIENT pass
        if (msg == WM_PAINT && !w) {
            // Our own BeginPaint (it hides the caret until EndPaint): the EDIT paints into this DC
            // (clipped out by Clip() while a selection is visible), then the composed field.
            ++self->depth_;
            PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
            const int saved = SaveDC(dc);
            const LRESULT r = DefSubclassProc(h, msg, reinterpret_cast<WPARAM>(dc), l);
            RestoreDC(dc, saved);
            --self->depth_;
            if (self->drew_) { self->drew_ = false; self->Compose(dc); }
            self->bypass_ = false;
            EndPaint(h, &ps);
            return r;
        }
        const bool print = msg == WM_PRINTCLIENT || msg == WM_PRINT;
        const int saved = print && w ? SaveDC(reinterpret_cast<HDC>(w)) : 0;
        ++self->depth_;
        const LRESULT r = DefSubclassProc(h, msg, w, l);
        --self->depth_;
        if (print) { if (saved) RestoreDC(reinterpret_cast<HDC>(w), saved); if (self->drew_) { self->drew_ = false; self->Compose(reinterpret_cast<HDC>(w)); } return r; }
        if (self->depth_ == 0 && self->drew_ && IsWindow(h)) {
            self->drew_ = false;
            HideCaret(h); HDC dc = GetDC(h); self->Compose(dc); ReleaseDC(h, dc); ShowCaret(h);
        }
        return r;
    }
};
// Ctrl+A selects the whole text of a Win32 EDIT, which has no such shortcut of its own (it ignored the
// keystroke, then the WM_CHAR 0x01 that TranslateMessage makes of it). Ctrl+Shift+A and Ctrl+Alt+A
// (AltGr) stay free for their own shortcuts. Any number of other subclasses can sit on the same EDIT.
namespace SelectAllDetail {
inline constexpr UINT_PTR subclassId=0x5E1A;   // "select all"
inline LRESULT CALLBACK Proc(HWND edit,UINT message,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR){
    switch(message){
    case WM_KEYDOWN:if(w=='A'&&GetKeyState(VK_CONTROL)<0&&GetKeyState(VK_SHIFT)>=0&&GetKeyState(VK_MENU)>=0){SendMessageW(edit,EM_SETSEL,0,-1);return 0;}break;
    case WM_CHAR:if(w==1)return 0;   // Ctrl+A's control character: never typed, no beep
        break;
    case WM_NCDESTROY:RemoveWindowSubclass(edit,Proc,subclassId);break;
    }
    return DefSubclassProc(edit,message,w,l);
}
}
inline bool SelectAllOnCtrlA(HWND edit){return edit&&SetWindowSubclass(edit,SelectAllDetail::Proc,SelectAllDetail::subclassId,0)!=FALSE;}
inline std::wstring EscapeMenuText(const std::wstring& text){
    std::wstring label;label.reserve(text.size());
    for(wchar_t c:text){label+=c;if(c==L'&')label+=c;}
    return label;
}
// TranslateMessage queues WM_CHAR even when the subsequent keydown is handled.
// Swallow only characters belonging to an address action; preserve IME input.
struct AddressKeyState {
    bool composing=false,returnHandled=false,escapeHandled=false,tabHandled=false;
    void SetComposing(bool value){composing=value;if(value)returnHandled=escapeHandled=tabHandled=false;}
    bool HandleKey(UINT key){if(composing)return false;if(key==VK_RETURN){returnHandled=true;return true;}if(key==VK_ESCAPE){escapeHandled=true;return true;}if(key==VK_TAB){tabHandled=true;return true;}return false;}
    bool ConsumeChar(WPARAM character){if(composing)return false;if(character==L'\r'&&returnHandled){returnHandled=false;return true;}if(character==VK_ESCAPE&&escapeHandled){escapeHandled=false;return true;}if(character==L'\t'&&tabHandled){tabHandled=false;return true;}return false;}
};
// All pixels are client-drawn; retain resize/Snap styles and hit testing while
// suppressing standard activated nonclient frame painting.
inline void ConfigureFullClientFrame(HWND window){DWMNCRENDERINGPOLICY policy=DWMNCRP_DISABLED;DwmSetWindowAttribute(window,DWMWA_NCRENDERING_POLICY,&policy,sizeof(policy));const DWORD noBorder=0xfffffffe;DwmSetWindowAttribute(window,34,&noBorder,sizeof(noBorder));}
// MAIN WINDOW ONLY. DWM rounds the top-level browser window (antialiased) and gives it
// a top-level window's frame shadow. Popups must NOT use this: DWM rounding always
// brings a shadow (measurements below). nativeCornerRadius stays the default corner of
// popup surfaces, now cut by ConfigureShadowlessPopup's window region at that radius.
inline constexpr float nativeCornerRadius=8;
inline void RoundNatively(HWND window){if(!window)return;SetWindowRgn(window,nullptr,FALSE);const int round=2/*DWMWCP_ROUND*/;DwmSetWindowAttribute(window,33,&round,sizeof(round));const COLORREF none=0xFFFFFFFE;DwmSetWindowAttribute(window,34,&none,sizeof(none));}
// Docked drawers (Réglages, Historique, Favoris, Téléchargements) slide without moving or re-cutting their window.
// Moving a window and changing its region both reach the screen a composition after the frame presented with them
// (measured: one frame in three showed the drawer cut, the page through its edge, or its content cut on the left).
// So the window stays on its docked column with a fixed shape; each slide step is one present: the content drawn
// `lead` pixels right of its place and, left of it, the key colour, which the layered window shows as transparent
// (and click-through). The region only rounds the corner shared with the owner (`square`: maximized, no corner),
// or hides everything while nothing has entered (before the first present of a reopened drawer).
inline constexpr COLORREF drawerKey=RGB(255,0,255);
inline void KeyedDrawer(HWND window){
    if(!window)return;
    SetWindowLongPtrW(window,GWL_EXSTYLE,GetWindowLongPtrW(window,GWL_EXSTYLE)|WS_EX_LAYERED);
    SetLayeredWindowAttributes(window,drawerKey,0,LWA_COLORKEY);
}
// A region mirrored in a `width` px wide window (pixel x <-> width - 1 - x), exactly: GDI's own round rectangles are not
// symmetric pixel for pixel, so a mirrored corner is the other one reflected, never drawn again.
inline HRGN MirroredRegion(HRGN source,int width){
    HRGN out=CreateRectRgn(0,0,0,0);const DWORD size=source?GetRegionData(source,0,nullptr):0;if(size<sizeof(RGNDATAHEADER))return out;
    std::vector<BYTE> data(size);auto* rd=reinterpret_cast<RGNDATA*>(data.data());if(GetRegionData(source,size,rd)!=size)return out;
    const RECT* r=reinterpret_cast<const RECT*>(rd->Buffer);
    for(DWORD i=0;i<rd->rdh.nCount;++i){HRGN one=CreateRectRgn(width-r[i].right,r[i].top,width-r[i].left,r[i].bottom);CombineRgn(out,out,one,RGN_OR);DeleteObject(one);}
    return out;
}
// `left` (#143): docked on the left (the side panel's layouts), mirrored: the bottom-LEFT corner is the one rounded.
inline HRGN DrawerRegion(bool entered,int right,int height,int radius,bool square,bool left=false){
    if(!entered)return CreateRectRgn(0,0,0,0);
    if(square)return CreateRectRgn(0,0,right,height);
    HRGN region=CreateRoundRectRgn(-radius*2,-radius*2,right+1,height+1,radius,radius),clip=CreateRectRgn(0,0,right+1,height+1);
    CombineRgn(region,region,clip,RGN_AND);DeleteObject(clip);
    if(left){HRGN mirrored=MirroredRegion(region,right);DeleteObject(region);return mirrored;}
    return region;
}
// #143 (owner 2026-11-25): the drawers dock on the tab panel's side. Their window rectangle (screen px), `width` wide from
// `top`, `height` high, in the owner's client `owner` (screen px): on the right `right` px in from its right edge (the top
// bar; Mode Focus's floating panel edge), or on the LEFT `dock` px in from its left edge (the side panel's right edge).
inline RECT DockedDrawerRect(const RECT& owner,int width,int top,int height,bool left,int dock,int right){
    return left?RECT{owner.left+dock,top,owner.left+dock+width,top+height}:RECT{owner.right-right-width,top,owner.right-right,top+height};}
// Where a sliding drawer's content is drawn (px, signed): `lead` px toward the edge it enters from (right: +, left: -).
inline int DrawerSlideOffset(int lead,bool left){return left?-lead:lead;}
// The column of the window its content covers while it slides ([left, right) window px): the strip it has not entered
// yet, `lead` px on its entering side, is keyed out.
inline void DrawerContentColumn(int lead,int width,bool left,int& from,int& to){from=left?0:lead;to=left?width-lead:width;}
// Whether a drawer docked there sits in the window's rounded bottom corner (its region rounds that corner): on the right
// against the window's edge, on the left against it too (dock 0); against the panel only when Side::roundAgainstPanel.
inline bool DrawerOnWindowCorner(bool left,int dock,int right,bool roundAgainstPanel){return left?(dock<=0||roundAgainstPanel):right<=0;}
// The main window (WS_CAPTION for Windows' minimize / maximize animations, its client covering the whole window):
// DefWindowProc paints a classic caption over the client when the title or icon changes (WM_SETTEXT / WM_SETICON).
// Windows draws that caption only for a full WS_CAPTION (WS_BORDER | WS_DLGFRAME), so the title and icon are stored
// with WS_DLGFRAME briefly cleared: the frame Windows computed stays as it is (cached until SWP_FRAMECHANGED, never
// sent here) and the window keeps WS_VISIBLE. Never by clearing WS_VISIBLE instead: that hides the window's children,
// the WebView2 pages, for that instant, and a frame DWM composes then shows the canvas in place of the page (the brief
// flash seen on clicks: every title, address or history change of the page sent a WM_SETTEXT).
inline LRESULT DefWindowProcNoCaption(HWND window,UINT message,WPARAM w,LPARAM l){
    const LONG_PTR style=GetWindowLongPtrW(window,GWL_STYLE);if((style&WS_CAPTION)!=WS_CAPTION)return DefWindowProcW(window,message,w,l);
    SetWindowLongPtrW(window,GWL_STYLE,style&~static_cast<LONG_PTR>(WS_DLGFRAME));const LRESULT r=DefWindowProcW(window,message,w,l);
    SetWindowLongPtrW(window,GWL_STYLE,GetWindowLongPtrW(window,GWL_STYLE)|WS_DLGFRAME);return r;}
// A window's text set only when it really changes: no WM_SETTEXT (nor its caption, accessibility and taskbar work)
// for the same title on each of the page's address, history or loading events.
inline bool SetTextIfChanged(HWND window,const std::wstring& text){
    if(!window)return false;
    const int n=GetWindowTextLengthW(window);
    std::wstring now(n>0?n:0,L'\0');
    if(n>0)now.resize(std::max(0,GetWindowTextW(window,now.data(),n+1)));
    if(now==text)return false;
    return SetWindowTextW(window,text.c_str())!=FALSE;}
inline bool HandleFrame(HWND window,UINT message,WPARAM w,LPARAM,LRESULT& result){switch(message){case WM_NCCALCSIZE:result=0;return true;case WM_NCPAINT:result=0;return true;case WM_NCACTIVATE:result=DefWindowProcW(window,message,w,-1);InvalidateRect(window,nullptr,FALSE);return true;}return false;}
// ---------------------------------------------------------------------------------
// Zero drop shadow (user decision, 2026-11-09). The earlier acceptance of DWM's
// standard popup shadow is REVOKED: none of our popup windows may cast a shadow,
// DWM-drawn or painted. Measured on Windows 11 10.0.26200 (tests/popup_shadow_test.cpp
// and the probes in design/missing-elements-A.md: desktop pixels captured with BitBlt
// around a #171717 popup over a white backdrop):
//   * plain WS_POPUP, DWMWCP_DEFAULT ............ no shadow, square corners
//   * DWMWCP_ROUND / ROUNDSMALL ................ shadow (2 px below: #CBCBCB / #E6E6E6)
//     whatever else is set: NCRENDERING_POLICY=DISABLED, WS_EX_LAYERED (LWA_ALPHA or
//     colour key), WS_EX_NOREDIRECTIONBITMAP, DwmExtendFrameIntoClientArea, even
//     UpdateLayeredWindow per-pixel alpha. DWM's antialiased corner and its shadow
//     cannot be separated through public API.
//   * CS_DROPSHADOW class ..................... small shadow (2 px below: #D4D4D4)
//   * WS_THICKFRAME ............................ frame shadow
//   * DWMWCP_DONOTROUND + round-rect window region .. no shadow, rounded 1-bit corner
//   * UpdateLayeredWindow per-pixel alpha ...... no shadow and antialiasing possible,
//     but child HWNDs (native EDIT fields) are not composed, and every Direct2D
//     surface would need a new renderer: rejected (fields, scope).
// Chosen compromise: DWMWCP_DONOTROUND + a GDI round-rect window region kept in sync
// with the window size and DPI. Cost: the corner silhouette is a 1-bit mask (no
// alpha). Mitigation: surfaces paint their own antialiased rounded fill and hairline
// and clear to the canvas (#171717) they sit on, so the stair pixels are mostly
// canvas over canvas. Gain: the exact Paper radius per surface (no longer DWM's fixed
// 8 DIP) and no shadow. DWM stays enabled; no system setting (SPI_SETDROPSHADOW…) is
// touched; only our own windows and classes in this process change. System windows
// (file pickers, print, Windows Hello, WebView2 engine menus) keep their system look.
inline constexpr float keepRegion=-1;   // radius sentinel: the window manages its own region
namespace ShadowDetail {
inline constexpr UINT_PTR subclassId=0x5A0D;   // "zero drop"
// State lives in a window property: GetWindowSubclass is exported only by ordinal from the
// comctl32 v5 this app loads (no v6 manifest), so importing it by name breaks process load.
inline constexpr wchar_t stateProp[]=L"NativeUI.Shadowless";
struct State { float radius=keepRegion; int width=-1,height=-1; UINT dpi=0; };
inline void Attributes(HWND window){
    const DWMNCRENDERINGPOLICY policy=DWMNCRP_DISABLED;DwmSetWindowAttribute(window,DWMWA_NCRENDERING_POLICY,&policy,sizeof(policy));
    const int corner=1/*DWMWCP_DONOTROUND: any DWM rounding brings the shadow*/;DwmSetWindowAttribute(window,33/*DWMWA_WINDOW_CORNER_PREFERENCE*/,&corner,sizeof(corner));
    const COLORREF none=0xFFFFFFFE/*DWMWA_COLOR_NONE*/;DwmSetWindowAttribute(window,34/*DWMWA_BORDER_COLOR*/,&none,sizeof(none));
}
inline void StripClass(HWND window){
    const ULONG_PTR style=GetClassLongPtrW(window,GCL_STYLE);
    if(style&CS_DROPSHADOW)SetClassLongPtrW(window,GCL_STYLE,static_cast<LONG_PTR>(style&~static_cast<ULONG_PTR>(CS_DROPSHADOW)));
}
inline void StripFrame(HWND window){
    const LONG_PTR style=GetWindowLongPtrW(window,GWL_STYLE);
    // WS_CAPTION = WS_BORDER|WS_DLGFRAME: a 1 px WS_BORDER alone casts nothing (comctl tooltips).
    if(style&(WS_DLGFRAME|WS_THICKFRAME)){SetWindowLongPtrW(window,GWL_STYLE,style&~static_cast<LONG_PTR>(WS_DLGFRAME|WS_THICKFRAME));SetWindowPos(window,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);}
}
inline int RadiusPx(float radius,UINT dpi){return static_cast<int>(radius*(dpi?dpi:96)/96.f+.5f);}
inline void Region(HWND window,State& state,bool force){
    if(state.radius<0)return;
    RECT r{};if(!GetWindowRect(window,&r))return;
    const int w=r.right-r.left,h=r.bottom-r.top;const UINT dpi=GetDpiForWindow(window);
    if(!force&&w==state.width&&h==state.height&&dpi==state.dpi)return;
    state.width=w;state.height=h;state.dpi=dpi;
    const int d=2*RadiusPx(state.radius,dpi);
    SetWindowRgn(window,d>0&&w>0&&h>0?CreateRoundRectRgn(0,0,w+1,h+1,d,d):nullptr,FALSE);
}
inline LRESULT CALLBACK Proc(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data){
    auto* state=reinterpret_cast<State*>(data);
    switch(message){
    case WM_NCDESTROY:{RemoveWindowSubclass(window,Proc,subclassId);RemovePropW(window,stateProp);delete state;return DefSubclassProc(window,message,w,l);}
    case WM_WINDOWPOSCHANGED:{const LRESULT result=DefSubclassProc(window,message,w,l);Region(window,*state,false);return result;}
    case WM_DPICHANGED:{const LRESULT result=DefSubclassProc(window,message,w,l);Attributes(window);Region(window,*state,true);return result;}
    case WM_NCACTIVATE:case WM_SHOWWINDOW:case WM_THEMECHANGED:case WM_DWMCOMPOSITIONCHANGED:{
        const LRESULT result=DefSubclassProc(window,message,w,l);
        if(message!=WM_SHOWWINDOW||w)Attributes(window);
        return result;}
    }
    return DefSubclassProc(window,message,w,l);
}
}
// Call at creation of every popup window of ours (and freely again on WM_DPICHANGED /
// WM_NCACTIVATE / WM_SHOWWINDOW: idempotent, cheap). `radius` (DIP):
//   keepRegion (default) — the window keeps/manages its own region (docked panels);
//   0 — square; > 0 — rounded region at that radius, kept in sync with size and DPI.
// Installs one comctl subclass that re-asserts the attributes on activation, show,
// theme and DPI changes and re-cuts the region on resize: callers need nothing else.
inline void ConfigureShadowlessPopup(HWND window,float radius=keepRegion){
    if(!window||!IsWindow(window))return;
    ShadowDetail::StripClass(window);ShadowDetail::StripFrame(window);ShadowDetail::Attributes(window);
    auto* state=static_cast<ShadowDetail::State*>(GetPropW(window,ShadowDetail::stateProp));
    if(!state){
        state=new ShadowDetail::State{};
        if(!SetWindowSubclass(window,ShadowDetail::Proc,ShadowDetail::subclassId,reinterpret_cast<DWORD_PTR>(state))){delete state;return;}
        SetPropW(window,ShadowDetail::stateProp,state);
    }
    const bool changed=state->radius!=radius;state->radius=radius;
    ShadowDetail::Region(window,*state,changed);
}
// Verification (tests/QA): true when nothing can draw a shadow for this window — no
// CS_DROPSHADOW on its class, no caption/dialog/thick frame, DWM non-client rendering
// off and no DWM rounding (ROUND/ROUNDSMALL always bring the shadow).
inline bool IsShadowless(HWND window){
    if(!window||!IsWindow(window))return false;
    if(GetClassLongPtrW(window,GCL_STYLE)&CS_DROPSHADOW)return false;
    if(GetWindowLongPtrW(window,GWL_STYLE)&(WS_DLGFRAME|WS_THICKFRAME))return false;
    BOOL nc=FALSE;if(SUCCEEDED(DwmGetWindowAttribute(window,DWMWA_NCRENDERING_ENABLED,&nc,sizeof(nc)))&&nc)return false;
    int corner=0;
    if(SUCCEEDED(DwmGetWindowAttribute(window,33,&corner,sizeof(corner)))&&(corner==2||corner==3))return false;
    return true;
}
// comctl tooltips (tooltips_class32, registered per process): drop CS_DROPSHADOW from
// the class for THIS process only (the comctl v5 class used here carries none; v6
// does) and keep DWM from rounding (= shadowing) the tip. Call after creating it.
inline void StripTooltipShadow(HWND tooltip){
    if(!tooltip||!IsWindow(tooltip))return;
    ShadowDetail::StripClass(tooltip);ShadowDetail::Attributes(tooltip);
}
}
