// Compile the actual command handler, but replace clipboard OS calls so the
// harness never changes the user's clipboard and never enters browser main.
#include <windows.h>
#include <cstdlib>
BOOL WINAPI FixtureGetClientRect(HWND,LPRECT rect){*rect={0,0,1180,815};return TRUE;}
BOOL WINAPI FixtureInvalidateRect(HWND,const RECT*,BOOL){return TRUE;}
bool commitSucceeds=false, canOpen=true, wasCommitted=false;
BOOL WINAPI FixtureOpenClipboard(HWND){return canOpen;}
BOOL WINAPI FixtureEmptyClipboard(){return TRUE;}
BOOL WINAPI FixtureCloseClipboard(){return TRUE;}
HANDLE WINAPI FixtureSetClipboardData(UINT format,HANDLE memory){
    if(!commitSucceeds)return nullptr;
    if(format!=CF_UNICODETEXT)std::abort();
    wasCommitted=true;GlobalFree(memory);return reinterpret_cast<HANDLE>(1);
}
#define OpenClipboard FixtureOpenClipboard
#define EmptyClipboard FixtureEmptyClipboard
#define CloseClipboard FixtureCloseClipboard
#define SetClipboardData FixtureSetClipboardData
#define GetClientRect FixtureGetClientRect
#define InvalidateRect FixtureInvalidateRect
#define wWinMain BrowserEntryNotCalled
#include "../../src/main.cpp"
#undef wWinMain
#include <cstdio>
void Require(bool ok,const char* what){if(!ok){std::fprintf(stderr,"FAIL %s\n",what);std::exit(1);}}
int wmain(){
    auto tab=std::make_shared<Tab>();tab->home=false;tab->uri=L"https://example.test/";pages.push_back(tab);current=0;
    motion.SetReducedMotion(false);chrome.lottie.Configure(2./3,.5);
    // Avoid creating any window/timer: this exercises the production command,
    // with ScheduleMotion's normal minimized gate killing any pending timer.
    motionMinimized=true;
    ExtraCommand('C');Require(!chrome.lottie.CopyActive(NativeChrome::Motion::Now()),"failed SetClipboardData does not animate");
    Require(motion.copy.Visual(NativeChrome::Motion::Now()).phase==NativeChrome::Motion::SuccessPhase::Idle,"failed clipboard has no success cue");
    commitSucceeds=true;canOpen=false;ExtraCommand('C');Require(!wasCommitted&&!chrome.lottie.CopyActive(NativeChrome::Motion::Now()),"clipboard open failure does not animate");
    canOpen=true;ExtraCommand('C');Require(wasCommitted&&chrome.lottie.CopyActive(NativeChrome::Motion::Now()),"successful SetClipboardData drives Lottie");
    Require(!GetModuleHandleW(L"WebView2Loader.dll"),"no browser runtime loaded");
    pages.clear();std::puts("PASS production clipboard success gate (OS clipboard calls replaced; user clipboard untouched).");return 0;
}
