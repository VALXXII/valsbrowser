#define UNICODE
#define _UNICODE
#include "../src/native-ui.h"
#include <cstdio>
#include <initializer_list>
#include <cstdlib>
NativeUI::AddressBackground background;
int edge=120;
LRESULT CALLBACK Parent(HWND h,UINT msg,WPARAM w,LPARAM l){
    if(msg==WM_CTLCOLOREDIT){
        HDC dc=(HDC)w;SetBkMode(dc,TRANSPARENT);SetBkColor(dc,RGB(38,38,38));
        SetTextColor(dc,RGB(180,180,180));SetBrushOrgEx(dc,0,0,nullptr);
        return (LRESULT)background.Brush(240,edge,RGB(38,38,38),RGB(55,55,55));
    }
    return DefWindowProcW(h,msg,w,l);
}
void Check(bool ok,const char* label){if(!ok){std::printf("FAIL %s\n",label);std::exit(1);}}
int main(){
    WNDCLASSW wc{};wc.lpfnWndProc=Parent;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"AddressBackgroundTest";RegisterClassW(&wc);
    HWND parent=CreateWindowW(wc.lpszClassName,L"",WS_POPUP,-10000,-10000,260,50,nullptr,nullptr,wc.hInstance,nullptr);
    HWND edit=CreateWindowW(L"EDIT",L"A          Z",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,0,0,240,32,parent,nullptr,wc.hInstance,nullptr);
    ShowWindow(parent,SW_SHOWNOACTIVATE);UpdateWindow(parent);
    HDC dc=CreateCompatibleDC(nullptr);HDC screen=GetDC(nullptr);HBITMAP bitmap=CreateCompatibleBitmap(screen,240,32);ReleaseDC(nullptr,screen);
    HGDIOBJ old=SelectObject(dc,bitmap);
    for(int boundary:{0,120,240}){
        edge=boundary;SendMessageW(edit,WM_PRINT,(WPARAM)dc,PRF_CLIENT|PRF_ERASEBKGND);
        for(int x:{4,80,119,120,180,235})Check(GetPixel(dc,x,24)==(x<edge?RGB(55,55,55):RGB(38,38,38)),"continuous edit background at loading edge");
        // Inside the text's line box: opaque glyph backgrounds must not erase the fill.
        Check(GetPixel(dc,35,5)==(35<edge?RGB(55,55,55):RGB(38,38,38)),"text line preserves fill");
    }
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);DestroyWindow(parent);
    puts("PASS native EDIT loading background: empty, partial, full and text line");
}

