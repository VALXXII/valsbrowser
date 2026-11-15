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
    // B01 request chip (Paper DJ4-0/DJ8-0): lock slot unchanged, chip 2 DIP after it, #3E3E3E plate,
    // URL pushed past it; never while editing; after the « Non sécurisé » plate on HTTP. 96/144/192 DPI.
    for(UINT dpi:{96u,144u,192u}){
        Chrome r;Check(SUCCEEDED(r.InitOffscreen(root,dpi,1180,84)),"chip renderer");
        r.address=AddressBar::Describe(L"https://meet.exemple.test/xmc-pqrt-bvn",false);
        r.requestGlyphs={SiteRequest::Path::camera,SiteRequest::Path::microphone};r.requestTip=L"meet.exemple.test demande : Caméra et micro";
        r.Paint({L"Meet"},0,L"",false,0,0);
        const auto slot=r.AddressSlot();const auto q=r.request;
        Check(q.left==slot.right+2&&q.right-q.left==42&&q.bottom-q.top==24,"chip: 2 DIP after the lock slot, 42 x 24 for two glyphs");
        Check((r.SurfacePixel(q.left+2,q.top+12)&0xffffff)==0x3E3E3E,"chip: --color-browser-pressed plate");
        bool ink=false;for(float x=q.left+5;x<q.left+19&&!ink;x+=.5f)for(float y=q.top+6;y<q.bottom-6&&!ink;y+=.5f)ink=(r.SurfacePixel(x,y)&0xff)>0xC0;
        Check(ink,"chip: white camera glyph drawn");
        bool hasRegion=false;for(auto& g:r.regions)if(g.id=='Q'&&g.tip&&std::wstring(g.tip)==r.requestTip)hasRegion=true;
        Check(hasRegion&&r.Hit((q.left+q.right)/2,(q.top+q.bottom)/2)=='Q',"chip: own hit region with its tooltip");
        Check((r.SurfacePixel(slot.right+1,q.top+12)&0xffffff)!=0x3E3E3E,"chip: never overlaps the security slot");
        Check(SUCCEEDED(r.SavePng((output+L"\\request-chip-"+std::to_wstring(dpi)+L".png").c_str())),"chip sample exported");
        r.addressEditing=true;r.Paint({L"Meet"},0,L"",true,0,0);
        Check(r.request.right<=r.request.left&&r.Hit(q.left+4,q.top+12)!='Q',"chip: hidden while the address is edited");
        r.addressEditing=false;r.address=AddressBar::Describe(L"http://meet.exemple.test/",false);r.requestGlyphs={SiteRequest::Path::position};
        r.Paint({L"Meet"},0,L"",false,0,0);
        Check(r.request.left==r.indicator.right+2&&r.request.right-r.request.left==24,"chip: after the « Non sécurisé » plate, one glyph 24 wide");
        Check(SUCCEEDED(r.SavePng((output+L"\\request-chip-http-"+std::to_wstring(dpi)+L".png").c_str())),"chip http sample exported");
        // B02 after the decision: granted (no plate), active (tint + blue dot), blocked (slash).
        r.address=AddressBar::Describe(L"https://meet.exemple.test/xmc-pqrt-bvn",false);r.requestGlyphs={SiteRequest::Path::camera,SiteRequest::Path::microphone};
        r.requestState=r.requestFrom=SiteRequest::State::Granted;r.Paint({L"Meet"},0,L"",false,0,0);
        Check((r.SurfacePixel(r.request.left+2,r.request.top+12)&0xffffff)!=0x3E3E3E,"granted: no pending plate");
        Check(SUCCEEDED(r.SavePng((output+L"\\request-chip-granted-"+std::to_wstring(dpi)+L".png").c_str())),"granted sample");
        r.requestState=r.requestFrom=SiteRequest::State::Active;r.Paint({L"Meet"},0,L"",false,0,0);
        {const auto px=r.SurfacePixel(r.request.left+5+3,(r.request.top+r.request.bottom)/2);const int b=px&0xff,rr=(px>>16)&0xff;
         Check(b>rr+60,"active: blue dot (accent)");}
        Check(r.request.right-r.request.left==5+6+6+14+6+14+5,"active: Paper E4Q-0 width");
        Check(SUCCEEDED(r.SavePng((output+L"\\request-chip-active-"+std::to_wstring(dpi)+L".png").c_str())),"active sample");
        r.requestState=r.requestFrom=SiteRequest::State::Blocked;r.requestGlyphs={SiteRequest::Path::camera};r.Paint({L"Meet"},0,L"",false,0,0);
        Check(SUCCEEDED(r.SavePng((output+L"\\request-chip-blocked-"+std::to_wstring(dpi)+L".png").c_str())),"blocked sample");
        r.requestState=r.requestFrom=SiteRequest::State::Pending;
        r.requestGlyphs.clear();r.Paint({L"Meet"},0,L"",false,0,0);
        Check(r.request.right<=r.request.left,"chip: gone with its request");
    }
    // Toolbar without the private-window button (« Nouvelle fenêtre privée » is in the profile menu and Ctrl+Maj+N):
    // the right cluster is compacted (24 DIP buttons 6 apart, separator, Réglages), the address field takes the 30 DIP
    // freed, no hole, no dead zone, no overlap. Normal / private window, audio and annotation on / off, 560 and 1180
    // DIP, 96 and 144 DPI. The command 'P' itself survives (Ctrl+Maj+N, profile menu).
    for(UINT dpi:{96u,144u})for(float w:{560.f,1180.f})for(bool priv:{false,true})for(bool audio:{false,true})for(bool annotate:{false,true}){
        Chrome r;Check(SUCCEEDED(r.InitOffscreen(root,dpi,w,84)),"toolbar renderer");
        if(priv){r.profileButton.shown=true;r.profileButton.privateWindow=true;r.profileButton.name=L"Fenêtre privée";}r.audioShown=audio;r.annotateShown=annotate;
        r.address=AddressBar::Describe(L"https://exemple.test/page",false);r.Paint({L"Exemple"},0,L"",false,0,0);
        auto at=[&](int id)->const D2D1_RECT_F*{for(auto& g:r.regions)if(g.id==id)return &g.rect;return nullptr;};
        auto same=[](const D2D1_RECT_F* a,D2D1_RECT_F b){return a&&a->left==b.left&&a->top==b.top&&a->right==b.right&&a->bottom==b.bottom;};
        Check(!at('P'),"toolbar: no private-window button region");
        Check(r.Enabled('P'),"toolbar: the private-window command stays enabled (shortcut, profile menu)");
        Check(same(at('S'),{w-30,48,w-6,72}),"toolbar: Réglages unchanged at the right edge");
        Check(same(at('D'),{w-67,48,w-43,72}),"toolbar: Téléchargements takes the freed slot");
        Check(same(at('Y'),{w-97,48,w-73,72}),"toolbar: Historique 6 DIP left of Téléchargements");
        Check(audio==(at('V')!=nullptr)&&(!audio||same(at('V'),{w-127,48,w-103,72})),"toolbar: Son des sites 6 DIP left of Historique");
        Check(annotate==(at('N')!=nullptr),"toolbar: Annoter only when shown");
        std::vector<int> cluster;if(annotate)cluster.push_back('N');if(audio)cluster.push_back('V');cluster.push_back('Y');cluster.push_back('D');
        for(size_t i=0;i+1<cluster.size();++i){const auto* a=at(cluster[i]);const auto* b=at(cluster[i+1]);
            Check(a&&b&&a->right-a->left==24&&a->bottom-a->top==24&&b->left-a->right==Design::Space::controlGap,"toolbar: 24 DIP buttons, constant 6 DIP gaps");}
        Check(at('S')->left-at('D')->right==13&&w-37>=at('D')->right&&w-36<=at('S')->left,"toolbar: the separator stays between Téléchargements and Réglages");
        const float first=at(cluster.front())->left;
        Check(r.bar.right==first-Design::Space::controlGap,"address field ends 6 DIP before the first toolbar button (no hole)");
        const float before=annotate?(audio?157.f:127.f)+6+24+Design::Space::controlGap:audio?157.f+Design::Space::controlGap:133.f;
        Check(r.bar.right==w-before+30,"address field gains exactly the 30 DIP freed by the private button");
        Check(r.bar.right-r.bar.left>=200,"address field stays usable at the minimum width");
        Check(!priv||(r.Hit(Design::ProfileButton::Plate().left+4,20)==900&&r.brandTip.rfind(L"Fenêtre privée",0)==0),"private window: the incognito profile slot (the persistent private indication) is intact");
        for(int id:cluster)Check(r.Hit((at(id)->left+at(id)->right)/2,60)==id,"toolbar: each button hit at its centre");
        Check(r.Hit(w-55,60)=='D'&&r.Hit(w-85,60)=='Y',"toolbar: the former private / downloads centres hit their new owners");
        Check(r.Hit(r.bar.right-16,60)=='K',"toolbar: the star sits at the field's new end (no dead zone)");
        for(auto& g:r.regions)if(g.rect.top>=44&&g.rect.bottom<=76&&g.id!='S'&&std::find(cluster.begin(),cluster.end(),g.id)==cluster.end())Check(g.rect.right<=first,"toolbar: nothing of the field overlaps a toolbar button");
        auto order=r.FocusOrder();Check(std::find(order.begin(),order.end(),'P')==order.end(),"focus order: no private button");
        std::vector<int> tail(order.end()-std::min(order.size(),cluster.size()+1),order.end());auto want=cluster;want.push_back('S');
        Check(tail==want,"focus order: the toolbar ends N?, V?, Historique, Téléchargements, Réglages");
        if(w==1180||audio==annotate){std::wstring name=output+L"\\toolbar-"+std::to_wstring(dpi)+L"-"+std::to_wstring((int)w)+(priv?L"-private":L"-normal")+(audio?L"-audio":L"")+(annotate?L"-annotate":L"")+L".png";
            Check(SUCCEEDED(r.SavePng(name.c_str())),"toolbar sample exported");}
    }
    std::puts("toolbar without the private button: geometry, hit, focus at 560/1180 DIP, 96/144 DPI, normal/private, audio/annotation");
    // Fullscreen (contentOnly): the page alone. No region (hit, tooltip, focus, UI Automation all read them), nothing
    // drawn but the canvas; leaving it brings every control back. 96 / 144 DPI, normal / private, 1180 and 560 DIP.
    for(UINT dpi:{96u,144u})for(float w:{560.f,1180.f})for(bool priv:{false,true}){
        Chrome r;Check(SUCCEEDED(r.InitOffscreen(root,dpi,w,300)),"fullscreen renderer");
        if(priv){r.profileButton.shown=true;r.profileButton.privateWindow=true;r.profileButton.name=L"Fenêtre privée";}
        r.annotateShown=true;r.audioShown=true;r.address=AddressBar::Describe(L"https://exemple.test/page",false);
        r.Paint({L"Exemple",L"Autre"},0,L"",false,0,0);const size_t normal=r.regions.size();
        const UINT32 tabPixel=r.SurfacePixel(Design::TabStrip::Surface(0).left+8,16)&0xffffff,fieldPixel=r.SurfacePixel(r.bar.left+4,60)&0xffffff;
        Check(normal>10&&r.Hit(w-18,60)=='S'&&!r.FocusOrder().empty(),"fullscreen setup: the chrome has its controls");
        r.contentOnly=true;r.Paint({L"Exemple",L"Autre"},0,L"",false,0,0);
        Check(r.regions.empty()&&r.FocusOrder().empty(),"fullscreen: no chrome region, nothing to focus or expose");
        bool hit=false;for(float y:{4.f,20.f,44.f,60.f,76.f,100.f})for(float x=0;x<w;x+=12)if(r.Hit(x,y))hit=true;
        Check(!hit,"fullscreen: no chrome hit anywhere in the former chrome rows");
        const UINT32 canvas=0x171717;bool clean=true;for(float y:{4.f,16.f,30.f,48.f,60.f,72.f,90.f})for(float x=2;x<w;x+=17)if((r.SurfacePixel(x,y)&0xffffff)!=canvas)clean=false;
        Check(clean&&tabPixel!=canvas&&fieldPixel!=canvas,"fullscreen: only the canvas is painted (no tab, field or button pixel)");
        if(dpi==144&&w==1180){Check(SUCCEEDED(r.SavePng((output+L"\\fullscreen-content-only"+(priv?L"-private":L"")+L".png").c_str())),"fullscreen sample exported");}
        r.contentOnly=false;r.Paint({L"Exemple",L"Autre"},0,L"",false,0,0);
        Check(r.regions.size()==normal&&r.Hit(w-18,60)=='S'&&(r.SurfacePixel(Design::TabStrip::Surface(0).left+8,16)&0xffffff)==tabPixel,"fullscreen left: every control and pixel is back");
    }
    std::puts("fullscreen content only: no region, hit, focus or chrome pixel; restored after (96/144 DPI, 560/1180 DIP, normal/private)");
    DestroyWindow(h);for(auto name:FontFiles)RemoveFontResourceExW((root+L"\\assets\\fonts\\"+name).c_str(),FR_PRIVATE,nullptr);CoUninitialize();
    Check(!GetModuleHandleW(L"WebView2Loader.dll"),"native test completed without loading WebView2");
    std::printf("PASS: %d native UI checks; no browser/WebView2 loaded or launched.\n",checks);return 0;
}
