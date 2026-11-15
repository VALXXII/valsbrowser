#pragma once
#include <windows.h>
#include <algorithm>
#include <string>
#include <dwmapi.h>
#include <commctrl.h>
namespace NativeUI {
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
// the window's profile). The EDIT has no per-control selection colour: it paints the selected run
// in the system COLOR_HIGHLIGHT / COLOR_HIGHLIGHTTEXT, during WM_PAINT and also directly (EM_SETSEL,
// mouse drag, double-click: measured, design/profile-address-selection-report.md). Before EVERY
// draw it asks its parent for colours (WM_CTLCOLOREDIT, same HDC). So:
//   1. Clip(), from the parent's WM_CTLCOLOREDIT: excludes the selected run's cell from that DC,
//      the system highlight never reaches the screen;
//   2. the subclass installed by Attach() paints that cell itself once the EDIT's draw is over
//      (WM_PAINT into our BeginPaint DC; any other message on a fresh DC with the caret hidden).
// Nothing else changes: selection range, caret, text, IME, undo, clipboard and UIA stay the
// EDIT's own. The run is measured like the EDIT draws it: start = EM_POSFROMCHAR, width = the
// run's extent in the field's font, one line high, clipped to the formatting rectangle.
// High contrast (forced colours): Clip/Paint do nothing, Windows keeps the system highlight.
class EditSelection {
public:
    struct Colors { COLORREF fill = 0, text = 0; };
    using ColorSource = Colors (*)();
    using Policy = bool (*)();   // true: forced colours, leave the native highlight
    static bool SystemForcedColors() {
        HIGHCONTRASTW hc{}; hc.cbSize = sizeof(hc);
        return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) && (hc.dwFlags & HCF_HIGHCONTRASTON);
    }
    bool Attach(HWND edit, ColorSource colors, Policy forced = SystemForcedColors) {
        if (!edit || !colors) return false;
        edit_ = edit; colors_ = colors; forced_ = forced ? forced : SystemForcedColors;
        return SetWindowSubclass(edit, Proc, subclassId, reinterpret_cast<DWORD_PTR>(this)) != FALSE;
    }
    // From the parent's WM_CTLCOLOREDIT for this edit.
    void Clip(HWND edit, HDC dc) {
        if (edit != edit_ || !Active()) return;
        RECT run{};
        if (!Run(dc, run, nullptr)) return;
        ExcludeClipRect(dc, run.left, run.top, run.right, run.bottom);
        drew_ = true;
    }
    // The colour changed (profile switch, a step of the profile fade): repaint the run now.
    void Refresh() { if (edit_ && Active()) { RECT run{}; HDC dc = GetDC(edit_); const bool has = Run(dc, run, nullptr); ReleaseDC(edit_, dc); if (has) InvalidateRect(edit_, &run, FALSE); } }
    // Paints the selected run on `dc` (the edit's client coordinates). Exposed for tests (WM_PRINT).
    bool Paint(HDC dc) {
        if (!Active()) return false;
        std::wstring text;
        RECT run{};
        if (!Run(dc, run, &text)) return false;
        DWORD start = 0, end = 0;
        SendMessageW(edit_, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
        RECT format{};
        SendMessageW(edit_, EM_GETRECT, 0, reinterpret_cast<LPARAM>(&format));
        const Colors c = colors_();
        const int saved = SaveDC(dc);
        IntersectClipRect(dc, format.left, format.top, format.right, format.bottom);
        if (HFONT font = reinterpret_cast<HFONT>(SendMessageW(edit_, WM_GETFONT, 0, 0))) SelectObject(dc, font);
        SetBkColor(dc, c.fill); SetTextColor(dc, c.text); SetBkMode(dc, OPAQUE);
        ExtTextOutW(dc, run.left, run.top, ETO_OPAQUE | ETO_CLIPPED, &run, text.c_str() + start, end - start, nullptr);
        RestoreDC(dc, saved);
        return true;
    }
    bool Active() const { return edit_ && GetFocus() == edit_ && !forced_(); }
private:
    static constexpr UINT_PTR subclassId = 0x5E1C;
    HWND edit_ = nullptr; ColorSource colors_ = nullptr; Policy forced_ = SystemForcedColors;
    bool drew_ = false; int depth_ = 0;
    // The selected run's cell, in client pixels; false when nothing is selected.
    bool Run(HDC dc, RECT& run, std::wstring* out) const {
        DWORD start = 0, end = 0;
        SendMessageW(edit_, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
        const int length = GetWindowTextLengthW(edit_);
        if (start >= end || static_cast<int>(end) > length) return false;
        std::wstring text(length + 1, L'\0');
        GetWindowTextW(edit_, text.data(), length + 1); text.resize(length);
        const LRESULT pos = SendMessageW(edit_, EM_POSFROMCHAR, start, 0);
        if (pos == -1) return false;
        HDC measure = dc ? dc : GetDC(edit_);
        HGDIOBJ old = nullptr;
        if (HFONT font = reinterpret_cast<HFONT>(SendMessageW(edit_, WM_GETFONT, 0, 0))) old = SelectObject(measure, font);
        TEXTMETRICW tm{}; GetTextMetricsW(measure, &tm);
        SIZE size{}; GetTextExtentPoint32W(measure, text.c_str() + start, static_cast<int>(end - start), &size);
        if (old) SelectObject(measure, old);
        if (!dc) ReleaseDC(edit_, measure);
        const int x = static_cast<short>(LOWORD(pos)), y = static_cast<short>(HIWORD(pos));
        run = {x, y, x + size.cx, y + tm.tmHeight};
        // The EDIT's own highlight reaches the next character's position inclusive (measured: one
        // pixel past the run's extent where the font's rounding differs, e.g. ":/" in Inter).
        if (static_cast<int>(end) < length) {
            const LRESULT next = SendMessageW(edit_, EM_POSFROMCHAR, end, 0);
            if (next != -1) run.right = (std::max)(run.right, static_cast<LONG>(static_cast<short>(LOWORD(next)) + 1));
        }
        if (out) *out = std::move(text);
        return true;
    }
    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
        auto* self = reinterpret_cast<EditSelection*>(data);
        if (msg == WM_NCDESTROY) { RemoveWindowSubclass(h, Proc, subclassId); self->edit_ = nullptr; return DefSubclassProc(h, msg, w, l); }
        if (msg == WM_PAINT && !w) {
            // Our own BeginPaint: the EDIT paints into this DC (wParam), the run follows in the same
            // frame; BeginPaint/EndPaint hide and restore the caret around both.
            // Saved around the EDIT's paint: Clip()'s exclusion must not outlive it on this DC.
            // BeginPaint's erase (WM_ERASEBKGND) counts as part of this paint too.
            ++self->depth_;
            PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
            const int saved = SaveDC(dc);
            const LRESULT r = DefSubclassProc(h, msg, reinterpret_cast<WPARAM>(dc), l);
            RestoreDC(dc, saved);
            --self->depth_;
            self->drew_ = false;
            self->Paint(dc);
            EndPaint(h, &ps);
            return r;
        }
        const bool print = msg == WM_PRINTCLIENT || msg == WM_PRINT;
        const int saved = print && w ? SaveDC(reinterpret_cast<HDC>(w)) : 0;
        ++self->depth_;
        const LRESULT r = DefSubclassProc(h, msg, w, l);
        --self->depth_;
        if (print) { if (saved) RestoreDC(reinterpret_cast<HDC>(w), saved); if (self->drew_) { self->drew_ = false; self->Paint(reinterpret_cast<HDC>(w)); } return r; }
        if (self->depth_ == 0 && self->drew_ && IsWindow(h)) {
            self->drew_ = false;
            HideCaret(h); HDC dc = GetDC(h); self->Paint(dc); ReleaseDC(h, dc); ShowCaret(h);
        }
        return r;
    }
};
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
