#ifndef UNICODE
#define UNICODE
#endif
#include "../src/chrome.h"
#include <cstdio>
using namespace NativeChrome;
#define Check(ok) do { if(!(ok)){std::printf("FAIL loading at line %d: %s\n",__LINE__,#ok);exit(1);} } while(false)
int wmain(int argc,wchar_t** argv){
 Check(argc==3);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
 auto at=[](int ms){return Motion::Time{}+Motion::Milliseconds(ms);};Loading::State a,b;
 Check(!a.NextWake(at(0),false).delay);a.Start(1,at(0));Check(a.NextWake(at(1),false).frames);
 Check(a.Sample(at(360)).fraction==.15f);Check(!a.NextWake(at(361),false).delay);
 const auto p1=a.Sample(at(90)).fraction,p2=a.Sample(at(180)).fraction,p3=a.Sample(at(270)).fraction,p4=a.Sample(at(360)).fraction;
 Check(p1>0&&p1<p2&&p2<p3&&p3<p4);Check(p2-p1>p3-p2&&p3-p2>p4-p3);
 a.Start(1,at(380));Check(a.Sample(at(380)).fraction==.15f);
 a.Committed(at(400));Check(a.Sample(at(400)).fraction==.15f);Check(a.Sample(at(760)).fraction==.65f);
 Check(!a.NextWake(at(761),false).delay);Check(b.Sample(at(800)).opacity==0);
 a.Start(2,at(900));a.Finish(1,at(910));Check(a.Sample(at(1260)).fraction==.15f);
 a.Finish(2,at(1300));Check(a.Sample(at(1660)).fraction==1);Check(a.Sample(at(1760)).opacity==.5f);
 Check(a.Sample(at(1860)).opacity==0&&!a.NextWake(at(1860),false).delay);
 a.Start(3,at(2000));Check(a.Sample(at(2000),true).fraction==.15f&&!a.NextWake(at(2000),true).delay);
 // A held navigation (restored tabs while the ad blocker is replaced): the bar shows at once, its navigation's own Start
 // continues it (no jump back), and that navigation's completion ends it.
 Loading::State w;w.Wait(at(0));Check(w.IsLoading()&&w.Sample(at(360)).fraction==.15f);
 w.Start(7,at(400));Check(w.Sample(at(400)).fraction==.15f);w.Committed(at(420));Check(w.Sample(at(780)).fraction==.65f);
 w.Finish(7,at(800));Check(!w.IsLoading()&&w.Sample(at(1160)).fraction==1);
 w.Clear();w.Wait(at(2000));w.Clear();Check(!w.IsLoading());w.Start(8,at(2100));Check(w.Sample(at(2100)).fraction==0);   // Clear forgets the wait
 Chrome chrome;Check(SUCCEEDED(chrome.InitOffscreen(argv[1],96,1180,100)));
 chrome.Paint({L"Loading"},0,L"https://example.test/",false,0,0);const float inField=chrome.bar.left+18; // the standalone Reload now sits at 83..115
 auto rest=chrome.SurfacePixel(inField,48)&255;
 chrome.loading={.5f,1};chrome.Paint({L"Loading"},0,L"https://example.test/",false,0,0);
 auto fill=chrome.SurfacePixel(inField,48)&255;Check(fill>=rest+16&&fill<=rest+18);
 // Sample the editable field: the star moved Reload into the old sample point.
 const float farField=chrome.edit.right-8;
 Check((chrome.SurfacePixel(farField,48)&255)==rest);
 Check(SUCCEEDED(chrome.SavePng((std::wstring(argv[2])+L"/loading-half.png").c_str())));
 chrome.loading={1,1};chrome.Paint({L"Loading"},0,L"https://example.test/",false,0,0);
 Check((chrome.SurfacePixel(farField,48)&255)==fill);
 Check(!GetModuleHandleW(L"WebView2Loader.dll"));puts("PASS loading lifecycle, stale completion, per-tab state, reduced motion, idle timers, 8% native fill pixels");return 0;
}
