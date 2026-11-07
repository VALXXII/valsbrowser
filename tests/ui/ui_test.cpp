#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <commctrl.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <vector>
#include "../../src/chrome.h"
#include "../../src/native-ui.h"

using namespace NativeChrome;
int checks=0;
void Check(bool condition,const char* label){++checks;if(!condition){std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}}
NativeUI::AddressKeyState keys;
int consumedReturn=0;
LRESULT CALLBACK TestEdit(HWND h,UINT message,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR){
    if(message==WM_KEYDOWN&&w==VK_RETURN){keys.HandleKey(VK_RETURN);return 0;}
    if(message==WM_CHAR&&keys.ConsumeChar(w)){++consumedReturn;return 0;}
    return DefSubclassProc(h,message,w,l);
}
int wmain(int argc,wchar_t** argv){
    Check(argc==3,"root and output arguments");std::wstring root=argv[1],output=argv[2];
    Check(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"COM native rendering initialized");
    Check(!GetModuleHandleW(L"WebView2Loader.dll"),"no WebView2 loader in native test process");
    for(auto name:FontFiles)Check(AddFontResourceExW((root+L"\\assets\\fonts\\"+name).c_str(),FR_PRIVATE,nullptr)>0,"private test font loaded");
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"BrowserNativeUITest";RegisterClassW(&wc);
    HWND h=CreateWindowW(wc.lpszClassName,L"native-only hidden fixture",WS_POPUP|WS_THICKFRAME|WS_MINIMIZEBOX|WS_MAXIMIZEBOX|WS_SYSMENU,0,0,1180,815,nullptr,nullptr,wc.hInstance,nullptr);
    Check(h!=nullptr,"hidden native fixture created");NativeUI::ConfigureFullClientFrame(h);
    const auto styles=GetWindowLongPtrW(h,GWL_STYLE);Check((styles&WS_THICKFRAME)&&(styles&WS_MAXIMIZEBOX)&&(styles&WS_MINIMIZEBOX),"frame preserves resize and Snap styles");
    LRESULT result=99;Check(NativeUI::HandleFrame(h,WM_NCCALCSIZE,FALSE,0,result)&&result==0,"initial nonclient calculation fully client");
    Check(NativeUI::HandleFrame(h,WM_NCCALCSIZE,TRUE,0,result)&&result==0,"resize nonclient calculation fully client");
    Check(NativeUI::HandleFrame(h,WM_NCPAINT,0,0,result)&&result==0,"standard nonclient paint suppressed");
    Check(NativeUI::HandleFrame(h,WM_NCACTIVATE,TRUE,0,result),"activation uses repaint suppression path");
    HWND edit=CreateWindowExW(0,L"EDIT",L"Google",WS_CHILD|ES_AUTOHSCROLL,0,0,500,32,h,nullptr,wc.hInstance,nullptr);
    SetWindowSubclass(edit,TestEdit,1,0);PostMessageW(edit,WM_KEYDOWN,VK_RETURN,1);
    MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
    Check(consumedReturn==1,"TranslateMessage generated Enter WM_CHAR consumed exactly once");
    Check(!keys.ConsumeChar(L'G'),"ordinary glyphs preserved");keys.composing=true;Check(!keys.HandleKey(VK_RETURN)&&!keys.ConsumeChar(L'\r'),"IME confirmation not consumed");keys.composing=false;
    Check(keys.HandleKey(VK_ESCAPE)&&keys.ConsumeChar(VK_ESCAPE)&&!keys.ConsumeChar(VK_ESCAPE),"only handled Escape consumed once");
    Check(keys.HandleKey(VK_TAB)&&keys.ConsumeChar(L'\t'),"handled Tab has no leftover edit character");
    for(int count=1;count<=128;++count)for(int active=0;active<count;++active)for(int closed=0;closed<count;++closed){
        int next=SelectionAfterClose(count,active,closed);
        if(count==1)Check(next==-1,"last close requests fresh tab");
        else if(active!=closed){int survivingIdentity=next>=closed?next+1:next;Check(survivingIdentity==active,"inactive close preserves active identity");}
        else Check(next>=0&&next<count-1,"active close selects surviving neighbor");
    }
    const std::vector<std::wstring> titles{L"Google",L"Ggyp ÉÀç",L"Superwhisper — AI Voice Dictation"};
    for(UINT dpi:{96,120,144,168,192,240,288,384}){
        Chrome chrome;Check(SUCCEEDED(chrome.InitOffscreen(root,dpi,1180,84)),"offscreen Direct2D/WIC setup");
        const D2D1_RECT_F line{0,0,500,32};
        for(float size:{14.f,16.f})for(const auto& sample:{L"G",L"Google",L"Ggyp",L"\u00e9\u00e8\u00e0\u00f9 \u00c9\u00c8\u00c0\u00d9",L"e\u0301 e\u0300 a\u0300",L"ÉÀç",L"ÀÉÎÔÜ gypqj"}){
            auto m=chrome.MeasureText(sample,line,size,size==14);
            Check(m.lineHeight>size,"real font line height exceeds old clipped label box");
            Check(m.inkTop>=-.01f&&m.inkBottom<=32.01f,"all measured vertical glyph ink fits complete control");
            Check(!m.trimmed,"short labels never vertically ellipsized");
        }
        auto shortTitle=chrome.MeasureText(L"Google",{89,6,157,38},14,true,false,-.28f);
        auto longTitle=chrome.MeasureText(titles[2],{89,6,157,38},14,true,false,-.28f);
        Check(!shortTitle.trimmed&&longTitle.trimmed,"ellipsis restricted to real horizontal overflow");
        HFONT font=CreateFontW(-MulDiv(16,dpi,96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,L"Inter Display");SendMessageW(edit,WM_SETFONT,(WPARAM)font,0);
        HDC dc=GetDC(edit);auto old=SelectObject(dc,font);TEXTMETRICW tm{};GetTextMetricsW(dc,&tm);SelectObject(dc,old);ReleaseDC(edit,dc);
        auto address=chrome.MeasureText(L"Ag",{92,44,998,76},16,false,false,-.16f);int baseline=(int)std::lround((address.originY+address.baseline)*dpi/96);
        int height=std::max(MulDiv(22,dpi,96),(int)(tm.tmHeight+tm.tmExternalLeading+2));MoveWindow(edit,0,baseline-tm.tmAscent,500,height,FALSE);
        RECT format{};SendMessageW(edit,EM_GETRECT,0,(LPARAM)&format);
        int top=baseline-tm.tmAscent-format.top;
        Check(height>=tm.tmHeight+tm.tmExternalLeading,"native edit full line height available");
        Check(top+format.top+tm.tmAscent==baseline,"native edit baseline matches DirectWrite rounded baseline");DeleteObject(font);
        chrome.Paint(titles,0,L"Google — Ggyp ÉÀç",false,TabBase,0);UINT32 body=chrome.SurfacePixel(Design::TabStrip::Surface(0).right-2,16);
        Check(SUCCEEDED(chrome.SavePng((output+L"\\hover-tab-"+std::to_wstring(dpi)+L".png").c_str())),"native tab snapshot exported");
        chrome.Paint(titles,0,L"Google — Ggyp ÉÀç",false,CloseBase,0);UINT32 close=chrome.SurfacePixel(Design::TabStrip::Surface(0).right-2,16);
        Check(body==close&&(close&0xffffff)==0x292929,"close hover never doubles selected surface brightness");
        Check(SUCCEEDED(chrome.SavePng((output+L"\\hover-close-"+std::to_wstring(dpi)+L".png").c_str())),"native close snapshot exported");
        chrome.Paint(titles,0,L"Google — Ggyp ÉÀç",false,CloseBase+1,0);Check((chrome.SurfacePixel(Design::TabStrip::Surface(1).right-2,16)&0xffffff)==0x222222,"inactive close uses one opaque hover surface");
        Motion::State feedback(false);auto when=Motion::Now()-Motion::Milliseconds(250);feedback.SetInteraction(TabBase,CloseBase,when);feedback.CopySucceeded(when);chrome.SetMotion(&feedback);
        chrome.Paint(titles,0,L"Google — Ggyp ÉÀç",false,CloseBase,0);
        Check((chrome.SurfacePixel(Design::TabStrip::Surface(0).right-2,16)&0xffffff)==0x292929,"animated close also keeps one selected surface");
        Check(feedback.copy.Visual(Motion::Now()).opacity==1,"rendered copy-success dwell uses truthful explicit trigger");
        if(dpi==96){Check(SUCCEEDED(chrome.SavePng((output+L"\\copy-check-96.png").c_str())),"Phosphor copy-check native snapshot exported");}
        chrome.SetMotion(nullptr);
        for(float width:{560.f,1180.f,1920.f})for(int selected:{0,1,57,127}){
            chrome.width=width;chrome.Build(128,selected);std::set<int> ids;
            for(auto& r:chrome.regions){Check(ids.insert(r.id).second,"hit IDs unique at overflow widths");if(IsClose(r.id)){Check(chrome.Hit((r.rect.left+r.rect.right)/2,(r.rect.top+r.rect.bottom)/2)==r.id,"close hit takes precedence over tab");Check(r.rect.right-r.rect.left==24&&r.rect.bottom-r.rect.top==24,"stable 24 DIP right close target");Check(TabIndex(r.id)>=0&&TabIndex(r.id)<128,"close target maps to correct underlying tab");}}
        }
        std::printf("DPI %u: line %.3f DIP; address baseline %dpx; glyph/ellipsis/close/frame checks passed\n",dpi,address.lineHeight,baseline);
    }
    DestroyWindow(h);for(auto name:FontFiles)RemoveFontResourceExW((root+L"\\assets\\fonts\\"+name).c_str(),FR_PRIVATE,nullptr);CoUninitialize();
    Check(!GetModuleHandleW(L"WebView2Loader.dll"),"native test completed without loading WebView2");
    std::printf("PASS: %d native UI checks; no browser/WebView2 loaded or launched.\n",checks);return 0;
}
