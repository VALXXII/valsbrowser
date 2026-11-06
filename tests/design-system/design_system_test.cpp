#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "../../src/chrome.h"
using namespace NativeChrome;
void Require(bool condition,const char* what){if(!condition){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);}}
int wmain(int argc,wchar_t** argv){
    Require(argc==3,"root and output arguments");Require(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"native COM initialization");
    Require(!GetModuleHandleW(L"WebView2Loader.dll"),"no WebView2 loader");
    using namespace Design;
    for(auto variant:{ButtonVariant::Ghost,ButtonVariant::Raised,ButtonVariant::Embedded,ButtonVariant::Window,ButtonVariant::TabClose}){
        auto idle=Resolve(variant),hover=Resolve(variant,{1,0}),press=Resolve(variant,{1,1}),disabled=Resolve(variant,{1,1,true,true}),focus=Resolve(variant,{0,0,false,true});
        Require(disabled.surface==idle.surface&&!disabled.focus&&Motion::PressScale(1,false,false)==1,"disabled ignores hover, press and focus");
        Require(disabled.iconOpacity<idle.iconOpacity,"disabled is visibly unavailable");
        Require(focus.focus&&focus.surface==idle.surface,"keyboard focus independent of fill");
        Require(std::abs(Motion::PressScale(1)-.96f)<.0001f,"pressed scale endpoint");
        if(variant!=ButtonVariant::TabClose)Require(press.surface>=hover.surface&&hover.surface>=idle.surface,"monotonic opaque interaction fills");
    }
    Require(VariantFor('X')==VariantFor('m')&&VariantFor('M')==VariantFor('m'),"identical window control variant");
    for(UINT dpi:{96,144,192}){
        Chrome chrome;Require(SUCCEEDED(chrome.InitOffscreen(argv[1],dpi,1180,560)),"production offscreen renderer");
        Require(SUCCEEDED(chrome.PaintStateSheet()),"production state sheet render");
        Require(SUCCEEDED(chrome.SavePng((std::wstring(argv[2])+L"/components-"+std::to_wstring(dpi)+L".png").c_str())),"state sheet PNG export");
        chrome.Paint({L"Google",L"Ggyp \u00c9\u00e7"},0,L"https://example.test",false,CloseBase,0);
        auto selected=chrome.SurfacePixel(183,16);
        chrome.Paint({L"Google",L"Ggyp \u00c9\u00e7"},0,L"https://example.test",false,TabBase,0);
        Require(selected==chrome.SurfacePixel(183,16),"close hover has single tab fill");
        chrome.SetEnabled('R',false);chrome.Build(2,0);Require(chrome.Hit(chrome.bar.right-57,60)==0,"disabled control excluded from pointer activation");
        auto order=chrome.FocusOrder();Require(std::find(order.begin(),order.end(),'R')==order.end(),"disabled excluded from keyboard order");auto tab=std::find(order.begin(),order.end(),TabBase);Require(tab!=order.end()&&tab+1!=order.end()&&*(tab+1)==CloseBase,"tab body precedes its reserved close action in keyboard order");
        chrome.SetEnabled('R',true);Require(chrome.Hit(chrome.bar.right-57,60)=='R',"enabled control restores same geometry");
        auto m=chrome.MeasureText(L"Ggyp \u00c9\u00e7",{0,0,200,32},Type::label.size,true);Require(m.inkTop>=0&&m.inkBottom<=32,"complete glyph ink retained");
        chrome.Paint({L"Google",L"Ggyp \u00c9\u00e7"},0,L"https://example.test",false,0,'R');
        Require(SUCCEEDED(chrome.SavePng((std::wstring(argv[2])+L"/chrome-focus-"+std::to_wstring(dpi)+L".png").c_str())),"production chrome focus snapshot");
    }
    Chrome animated;Require(SUCCEEDED(animated.InitOffscreen(argv[1],96,1180,84)),"animated production fixture");
    Motion::State motion(false);motion.EnablePolish();animated.SetMotion(&motion);animated.animateSelection=true;
    const auto start=Motion::Time{};motion.SelectTab(1,start);motion.SelectTab(2,start+Motion::Milliseconds(10));
    int before=0,after=0;for(int ms:{10,70,130}){animated.Paint({L"First",L"Second"},1,L"",false,0,0,nullptr,start+Motion::Milliseconds(ms));int old=animated.SurfacePixel(183,16)&255,newer=animated.SurfacePixel(323,16)&255;if(ms==10){before=old;Require(newer==23,"new selection starts at existing canvas without entrance jump");}if(ms==130){after=old;Require(newer==41,"selected surface reaches semantic selected endpoint");}Require(old>=23&&old<=41&&newer>=23&&newer<=41,"selection interpolation bounded to opaque surface tokens");}
    Require(before==41&&after==23,"old tab selection surface fades to canvas");
    motion.SetReducedMotion(true);motion.SelectTab(1,start+Motion::Milliseconds(140));animated.Paint({L"First",L"Second"},0,L"",false,0,0,nullptr,start+Motion::Milliseconds(140));Require((animated.SurfacePixel(183,16)&255)==41,"reduced motion selection updates immediately");
    animated.Dispose();Require(!GetModuleHandleW(L"WebView2Loader.dll"),"native fixture never loaded engine");
    std::puts("PASS: typed variants, disabled/focus isolation, opaque state fills, native state sheets at 96/144/192 DPI, stable close hover and glyph ink.");CoUninitialize();return 0;
}
