#pragma once
#include <windows.h>
#include <string>
#include <dwmapi.h>
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
inline bool HandleFrame(HWND window,UINT message,WPARAM w,LPARAM,LRESULT& result){switch(message){case WM_NCCALCSIZE:result=0;return true;case WM_NCPAINT:result=0;return true;case WM_NCACTIVATE:result=DefWindowProcW(window,message,w,-1);InvalidateRect(window,nullptr,FALSE);return true;}return false;}
}
