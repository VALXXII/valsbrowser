// Focused fixture: compile the real production window and EDIT procedures,
// but never call the browser entry point or create/load a web engine.
#define wWinMain BrowserEntryNeverCalled
#include "../../src/main.cpp"
#undef wWinMain
#include <cstdio>

void PumpNativeMessages(){MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){if(msg.message==WM_QUIT)continue;if(msg.message==WM_KEYDOWN||msg.message==WM_SYSKEYDOWN){int cmd=Key((UINT)msg.wParam);if(cmd){Command(cmd);continue;}}TranslateMessage(&msg);DispatchMessageW(&msg);}}
LRESULT CALLBACK TraceEdit(HWND h,UINT message,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR){
    bool relevant=message==WM_KEYDOWN||message==WM_CHAR||message==WM_KILLFOCUS||message==WM_IME_STARTCOMPOSITION||message==WM_IME_ENDCOMPOSITION;
    if(relevant)std::printf("EDIT before msg=0x%04x key=%llu handledReturn=%d composing=%d visible=%d focused=%d\n",message,(unsigned long long)w,addressKeys.returnHandled,addressKeys.composing,IsWindowVisible(h),GetFocus()==h);
    auto result=DefSubclassProc(h,message,w,l);
    if(relevant)std::printf("EDIT after  msg=0x%04x handledReturn=%d composing=%d visible=%d focused=%d result=%lld\n",message,addressKeys.returnHandled,addressKeys.composing,IsWindowVisible(h),GetFocus()==h,(long long)result);
    return result;
}
COLORREF ReadOwnPixel(int x,int y){DwmFlush();HDC dc=GetDC(window);COLORREF pixel=GetPixel(dc,x,y);ReleaseDC(window,dc);return pixel;}
int wmain(int argc,wchar_t** argv){
    if(argc!=2)return 1;SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if(FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)))return 2;
    instance=GetModuleHandleW(nullptr);base=argv[1];
    INITCOMMONCONTROLSEX cc{sizeof(cc),ICC_STANDARD_CLASSES|ICC_TAB_CLASSES};InitCommonControlsEx(&cc);
    for(auto name:NativeChrome::FontFiles)std::printf("Private font loaded=%d\n",AddFontResourceExW((base+L"\\assets\\fonts\\"+name).c_str(),FR_PRIVATE,nullptr));
    paper=CreateSolidBrush(BG);WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=WndProc;wc.lpszClassName=L"BrowserNativeRuntimeProbe";wc.hbrBackground=paper;wc.hCursor=LoadCursor(nullptr,IDC_ARROW);RegisterClassW(&wc);
    window=CreateWindowW(wc.lpszClassName,L"Native runtime probe - NO WEB ENGINE",WS_POPUP|WS_THICKFRAME|WS_MINIMIZEBOX|WS_MAXIMIZEBOX|WS_SYSMENU|WS_CLIPCHILDREN,100,100,1180,500,nullptr,nullptr,instance,nullptr);
    NativeUI::ConfigureFullClientFrame(window);if(FAILED(chrome.Init(window,base)))return 3;chrome.SetMotion(&motion);motion.SetReducedMotion(true);UpdateFont();
    auto first=std::make_shared<Tab>();first->title=L"Google";pages.push_back(first);auto second=std::make_shared<Tab>();second->title=L"Second tab";pages.push_back(second);current=0;
    Layout();ShowWindow(window,SW_SHOW);UpdateWindow(window);PumpNativeMessages();
    // Viewport host (Paper V3 Outline > Viewbox): placed 2 DIP inside left/right/bottom,
    // bottom-only 8 DIP corners restored, square maximised, restored again after.
    // DirectComposition: the corners are the GPU clip (soft, antialiased border) with
    // the exact fractional radius and no window region; otherwise the aliased region.
    auto viewportShape=[](const char* label,bool maximized){RECT client{},host{},box{};GetClientRect(window,&client);GetWindowRect(viewport,&host);MapWindowPoints(nullptr,window,(POINT*)&host,2);HRGN rgn=CreateRectRgn(0,0,0,0);int kind=GetWindowRgn(viewport,rgn);GetRgnBox(rgn,&box);
        const int w=host.right-host.left,hh=host.bottom-host.top;const float expectedRadius=maximized?0.f:8.f*GetDpiForWindow(window)/96.f;
        bool rounded,topSquare;
        if(compositor.Ready()){
            // The clip covers the host; a bottom-corner pixel is outside the shape, a top-corner pixel inside.
            rounded=kind==ERROR&&compositor.Masked()==!maximized&&compositor.Width()==w&&compositor.Height()==hh&&std::fabs(compositor.Radius()-expectedRadius)<.001f&&(maximized?compositor.Contains(0,(float)hh-1):!compositor.Contains(0,(float)hh-1));
            topSquare=compositor.Contains(0,0)&&compositor.Contains((float)w-1,0);
        }else{rounded=kind!=ERROR&&!PtInRegion(rgn,0,hh-1);topSquare=kind==ERROR||PtInRegion(rgn,0,0);if(maximized)rounded=kind==ERROR;}
        DeleteObject(rgn);
        std::printf("VIEWPORT %s dpi=%u composition=%d client=%ldx%ld host=(%ld,%ld,%ld,%ld) expected=(%d,%d,%d,%d) clip=%.0fx%.0f radius=%.2f expectedRadius=%.2f windowRegion=%d shapeOk=%d topSquare=%d\n",label,GetDpiForWindow(window),compositor.Ready(),client.right,client.bottom,host.left,host.top,host.right,host.bottom,S(2),S(84),(int)client.right-S(2),(int)client.bottom-S(2),compositor.Width(),compositor.Height(),compositor.Radius(),expectedRadius,kind,rounded,topSquare);
        return std::make_tuple(host.left==S(2)&&host.top==S(84)&&host.right==client.right-S(2)&&host.bottom==client.bottom-S(2)&&w>0&&hh>0,rounded,topSquare);};
    {auto [placed,rounded,square]=viewportShape("restored",false);if(!placed||!rounded||!square){std::printf("FAIL viewport restored\n");return 4;}
     ShowWindow(window,SW_MAXIMIZE);PumpNativeMessages();auto [mp,mr,ms]=viewportShape("maximized",true);if(!mp||!mr||!ms){std::printf("FAIL viewport maximized\n");return 4;}
     SetWindowPos(window,nullptr,0,0,900,600,SWP_NOMOVE|SWP_NOZORDER);ShowWindow(window,SW_RESTORE);PumpNativeMessages();SetWindowPos(window,nullptr,0,0,900,600,SWP_NOMOVE|SWP_NOZORDER);PumpNativeMessages();auto [rp,rr,rs]=viewportShape("resized",false);if(!rp||!rr||!rs){std::printf("FAIL viewport resized\n");return 4;}}
    // Corner mask coverage (8x8 per pixel): opaque canvas outside the arc, clear inside,
    // partial alpha along it, mirrored exactly between the two corners, at 100/125/175 %.
    for(float r:{8.f,10.f,14.f}){const int n=(int)std::ceil(r);int partial=0;bool ok=NativeViewport::MaskAlpha(0,n-1,n,r,false)>.99f&&NativeViewport::MaskAlpha(n-1,0,n,r,false)==0.f;
     for(int j=0;j<n;++j)for(int i=0;i<n;++i){const float a=NativeViewport::MaskAlpha(i,j,n,r,false);if(a>0&&a<1)++partial;if(a!=NativeViewport::MaskAlpha(n-1-i,j,n,r,true))ok=false;if(i+1<n&&NativeViewport::MaskAlpha(i+1,j,n,r,false)>a)ok=false;}
     std::printf("VIEWPORT mask radius=%.0f px tile=%d partialPixels=%d ok=%d\n",r,n,partial,ok&&partial>=n);if(!ok||partial<n){std::printf("FAIL viewport mask\n");return 4;}}
    // Overlay scrollbar layer (Paper V3 "Scrollbar minimale"): with composition, both
    // handle visuals exist above the pages and accept a raster; hidden (opacity 0) at rest.
    if(compositor.Ready()){const float sc=GetDpiForWindow(window)/96.f;auto tile=NativeChrome::Scrollbar::Raster(compositor.Width()-2*sc,(int)std::lround(120*sc),sc,0,true);
     const bool uploaded=compositor.Bars()&&compositor.SetBarContent(0,tile.pixels.data(),tile.across,tile.along);compositor.HideBar(0);compositor.Commit();
     std::printf("SCROLLBAR layer bars=%d uploaded=%d tile=%dx%d\n",compositor.Bars(),uploaded,tile.across,tile.along);if(!uploaded){std::printf("FAIL scrollbar layer\n");return 4;}}
    SetWindowSubclass(address,TraceEdit,99,0);FocusAddress();SetWindowTextW(address,L"https://example.com/");PumpNativeMessages();
    wchar_t face[128]{};HDC dc=GetDC(address);auto old=SelectObject(dc,font);GetTextFaceW(dc,128,face);TEXTMETRICW tm{};GetTextMetricsW(dc,&tm);SelectObject(dc,old);ReleaseDC(address,dc);RECT format{};SendMessageW(address,EM_GETRECT,0,(LPARAM)&format);RECT bounds{};GetWindowRect(address,&bounds);MapWindowPoints(nullptr,window,(POINT*)&bounds,2);
    std::printf("REAL EDIT: face=%s dpi=%u fontHeight=%ld ascent=%ld window=(%ld,%ld,%ld,%ld) format=(%ld,%ld,%ld,%ld)\n",Utf8(face).c_str(),GetDpiForWindow(window),tm.tmHeight,tm.tmAscent,bounds.left,bounds.top,bounds.right,bounds.bottom,format.left,format.top,format.right,format.bottom);
    PostMessageW(address,WM_KEYDOWN,VK_RETURN,1);PumpNativeMessages();
    std::printf("ENTER result: returnHandled=%d composing=%d editVisible=%d focusParent=%d noWebView=%d\n",addressKeys.returnHandled,addressKeys.composing,IsWindowVisible(address),GetFocus()==window,GetModuleHandleW(L"WebView2Loader.dll")==nullptr);
    for(int i=0;i<6;i++){UpdateInteraction(i%2?'R':'C',0);PumpNativeMessages();std::printf("HWND partial frame %d bodyPixel=%06lx controlPixel=%06lx\n",i,(unsigned long)ReadOwnPixel(S(300),S(200)),(unsigned long)ReadOwnPixel(S(500),S(50)));}
    std::printf("FINAL noWebView=%d\n",GetModuleHandleW(L"WebView2Loader.dll")==nullptr);
    DestroyWindow(window);PumpNativeMessages();chrome.Dispose();DeleteObject(font);DeleteObject(paper);for(auto name:NativeChrome::FontFiles)RemoveFontResourceExW((base+L"\\assets\\fonts\\"+name).c_str(),FR_PRIVATE,nullptr);CoUninitialize();return 0;
}
