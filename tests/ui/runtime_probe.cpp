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
    SetWindowSubclass(address,TraceEdit,99,0);FocusAddress();SetWindowTextW(address,L"https://example.com/");PumpNativeMessages();
    wchar_t face[128]{};HDC dc=GetDC(address);auto old=SelectObject(dc,font);GetTextFaceW(dc,128,face);TEXTMETRICW tm{};GetTextMetricsW(dc,&tm);SelectObject(dc,old);ReleaseDC(address,dc);RECT format{};SendMessageW(address,EM_GETRECT,0,(LPARAM)&format);RECT bounds{};GetWindowRect(address,&bounds);MapWindowPoints(nullptr,window,(POINT*)&bounds,2);
    std::printf("REAL EDIT: face=%s dpi=%u fontHeight=%ld ascent=%ld window=(%ld,%ld,%ld,%ld) format=(%ld,%ld,%ld,%ld)\n",Utf8(face).c_str(),GetDpiForWindow(window),tm.tmHeight,tm.tmAscent,bounds.left,bounds.top,bounds.right,bounds.bottom,format.left,format.top,format.right,format.bottom);
    PostMessageW(address,WM_KEYDOWN,VK_RETURN,1);PumpNativeMessages();
    std::printf("ENTER result: returnHandled=%d composing=%d editVisible=%d focusParent=%d noWebView=%d\n",addressKeys.returnHandled,addressKeys.composing,IsWindowVisible(address),GetFocus()==window,GetModuleHandleW(L"WebView2Loader.dll")==nullptr);
    for(int i=0;i<6;i++){UpdateInteraction(i%2?'R':'C',0);PumpNativeMessages();std::printf("HWND partial frame %d bodyPixel=%06lx controlPixel=%06lx\n",i,(unsigned long)ReadOwnPixel(S(300),S(200)),(unsigned long)ReadOwnPixel(S(500),S(50)));}
    std::printf("FINAL noWebView=%d\n",GetModuleHandleW(L"WebView2Loader.dll")==nullptr);
    DestroyWindow(window);PumpNativeMessages();chrome.Dispose();DeleteObject(font);DeleteObject(paper);for(auto name:NativeChrome::FontFiles)RemoveFontResourceExW((base+L"\\assets\\fonts\\"+name).c_str(),FR_PRIVATE,nullptr);CoUninitialize();return 0;
}
