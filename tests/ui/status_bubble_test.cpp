#ifndef UNICODE
#define UNICODE
#endif
// Link preview bubble (src/status-bubble.h), Paper "Aperçu URL — contexte bas gauche":
// geometry (23 high, hugs text + 6/6 padding, capped at 393 and at the page width), the
// #171717 surface with only its top-right corner rounded, 50 % white Inter text,
// click-through window styles, and hide on empty text. No WebView2.
#include <windows.h>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>
#include "../../src/chrome.h"
#include "../../src/status-bubble.h"
using namespace NativeChrome;
void Require(bool good,const char* message){if(!good){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
struct Canvas{HDC dc=nullptr;HBITMAP bmp=nullptr;HGDIOBJ old=nullptr;std::uint32_t* px=nullptr;int w=0,h=0;
    Canvas(int W,int H):w(W),h(H){BITMAPINFO bi{};bi.bmiHeader.biSize=sizeof(bi.bmiHeader);bi.bmiHeader.biWidth=w;bi.bmiHeader.biHeight=-h;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;
        HDC screen=GetDC(nullptr);dc=CreateCompatibleDC(screen);ReleaseDC(nullptr,screen);void* bits=nullptr;bmp=CreateDIBSection(dc,&bi,DIB_RGB_COLORS,&bits,nullptr,0);old=SelectObject(dc,bmp);px=(std::uint32_t*)bits;}
    ~Canvas(){SelectObject(dc,old);DeleteObject(bmp);DeleteDC(dc);}
    void Fill(std::uint32_t c){for(int i=0;i<w*h;++i)px[i]=c;}
    int Gray(int x,int y)const{return (int)(px[y*w+x]&255);}
    void Save(const std::wstring& path)const{BITMAPFILEHEADER f{};BITMAPINFOHEADER i{};i.biSize=sizeof(i);i.biWidth=w;i.biHeight=-h;i.biPlanes=1;i.biBitCount=32;i.biCompression=BI_RGB;
        f.bfType=0x4D42;f.bfOffBits=sizeof(f)+sizeof(i);f.bfSize=f.bfOffBits+w*h*4;std::ofstream o(path.c_str(),std::ios::binary);o.write((char*)&f,sizeof(f));o.write((char*)&i,sizeof(i));o.write((char*)px,w*h*4);}
};
int wmain(int argc,wchar_t** argv){
    Require(argc==3,"root and output path required");SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    Require(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"COM");std::wstring root=argv[1],out=argv[2];
    for(auto name:FontFiles)AddFontResourceExW((root+L"\\assets\\fonts\\"+name).c_str(),FR_PRIVATE,nullptr);
    Chrome chrome;Require(SUCCEEDED(chrome.InitOffscreen(root,96,64,64)),"native renderer");
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"StatusBubbleTestOwner";RegisterClassW(&wc);
    HWND owner=CreateWindowExW(WS_EX_TOOLWINDOW,wc.lpszClassName,L"",WS_POPUP,-32000,-32000,1180,815,nullptr,nullptr,wc.hInstance,nullptr);Require(owner,"owner window");
    const float scale=GetDpiForWindow(owner)/96.f;auto Px=[&](float dip){return (int)std::lround(dip*scale);};
    StatusBubble::Bubble bubble;Require(bubble.Init(wc.hInstance,owner,chrome.Factory(),chrome.WriteFactory(),chrome.FontCollection()),"bubble window");
    const LONG_PTR ex=GetWindowLongPtrW(bubble.Window(),GWL_EXSTYLE);
    Require((ex&WS_EX_TRANSPARENT)&&(ex&WS_EX_LAYERED)&&(ex&WS_EX_NOACTIVATE),"click-through, never activated, layered");
    const RECT page{Px(4),Px(84),Px(1176),Px(811)};
    const std::wstring longUrl=L"https://artificialanalysis.ai/methodology/coding-agents-benchmarking#reward-hacking",shortUrl=L"https://exemple.fr/";
    auto check=[&](const std::wstring& url,RECT pg,int expectW,const wchar_t* file){
        bubble.Show(url,pg);Require(bubble.Visible(),"shown");RECT r=bubble.ScreenRect();POINT o{pg.left,pg.bottom};ClientToScreen(owner,&o);
        const int w=r.right-r.left,h=r.bottom-r.top;std::printf("%ls -> %dx%d px\n",url.c_str(),w,h);
        Require(h==Px(23),"height 23 DIP");Require(r.left==o.x&&r.bottom==o.y,"flush with the page's bottom-left corner");
        if(expectW>0)Require(std::abs(w-expectW)<=1,"expected width");
        Canvas c(w,h);c.Fill(0xFFFFFFFF);POINT client{0,0};ScreenToClient(owner,&client);(void)client;
        POINT topLeft{r.left,r.top};ScreenToClient(owner,&topLeft);bubble.BlendInto(c.dc,{-topLeft.x,-topLeft.y});
        Require(c.Gray(0,h-1)==23&&c.Gray(0,0)==23&&c.Gray(w-1,h-1)==23,"#171717 surface, square corners on three sides");
        Require(c.Gray(w-1,0)>200,"top-right corner rounded (page shows through)");
        int peak=0;for(int y=0;y<h;++y)for(int x=Px(6);x<w-Px(6);++x)peak=std::max(peak,c.Gray(x,y));
        std::printf("  text peak gray %d\n",peak);Require(peak>110&&peak<160,"text is white at 50 % over #171717");
        int left=0;for(int y=0;y<h;++y)for(int x=0;x<Px(6)-1;++x)left=std::max(left,c.Gray(x,y));Require(left==23,"6 DIP left padding stays clear");
        c.Save(out+L"\\"+file);return w;};
    const int shortW=check(shortUrl,page,0,L"status-short.bmp");
    Require(shortW<Px(393)&&shortW==Px(bubble.WidthFor(shortUrl,4096)),"short URL: the bubble hugs its text");
    check(longUrl,page,Px(393),L"status-long.bmp");
    check(longUrl,{Px(4),Px(84),Px(254),Px(811)},Px(250),L"status-narrow-page.bmp");
    bubble.Show(L"",page);Require(!bubble.Visible(),"empty status hides the bubble");
    bubble.Show(longUrl,{0,0,0,0});Require(!bubble.Visible(),"no page, no bubble");
    bubble.Dispose();Require(!bubble.Window(),"disposed");DestroyWindow(owner);
    std::printf("Status bubble tests passed.\n");return 0;
}
