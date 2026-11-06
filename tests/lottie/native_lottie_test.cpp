#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include "../../src/chrome.h"
using namespace NativeChrome;
void Require(bool ok,const char* message){if(!ok){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
int wmain(int argc,wchar_t** argv){
    Require(argc==3,"root and output required");std::wstring root=argv[1],out=argv[2];
    Require(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"COM");
    Require(!GetModuleHandleW(L"WebView2Loader.dll"),"no engine loaded");
    const auto start=Motion::Time{};auto at=[&](int ms){return start+Motion::Milliseconds(ms);};
    Lottie::Playback playback;playback.Configure(2./3,.5);
    Require(!playback.NextWake(start,false).delay,"idle has no timer");
    playback.CopySucceeded(start);Require(playback.NextWake(start,false).frames,"copy begins frames");
    Require(playback.CopyProgress(at(600))==10./19&&!playback.NextWake(at(600),false).frames,"check dwell uses deadline, no frame ticks");
    playback.CopySucceeded(at(600));Require(playback.CopyProgress(at(600))==10./19,"repeat retains check");
    Require(!playback.NextWake(at(4000),false).delay,"copy stops at rest");
    playback.Clear();playback.RefreshBegin(start);Require(playback.RefreshActive(at(100)),"actual refresh starts");
    Require(!playback.NextWake(at(100),true).delay,"reduced motion has no lottie ticks");
    playback.RefreshEnd(at(100));Require(playback.RefreshActive(at(499))&&!playback.RefreshActive(at(500)),"early navigation completion preserves exactly one 500ms turn");
    playback.RefreshBegin(start);auto half=playback.RefreshProgress(at(250));playback.RefreshBegin(at(250));
    Require(half==.5&&playback.RefreshProgress(at(250))==half,"repeated activation cannot rewind the current turn");
    Require(!playback.RefreshActive(at(500))&&!playback.NextWake(at(500),false).delay,"long load cannot loop or leave a frame timer");
    playback.RefreshEnd(at(2000));Require(!playback.RefreshActive(at(2000))&&playback.RefreshProgress(at(2000))==1,"late completion cannot restart animation");
    playback.RefreshBegin(at(2100));Require(playback.RefreshActive(at(2200))&&!playback.RefreshActive(at(2600)),"new activation after rest gets one fresh turn");
    playback.Clear();Require(!playback.NextWake(start,false).delay,"teardown idle");
    Lottie::Asset missing;Require(!missing.Load(root+L"/assets/lottie/missing.json"),"missing asset falls back");
    {std::ofstream f(std::filesystem::path(out+L"/invalid.json"));f<<"{invalid json}";}
    Require(!missing.Load(out+L"/invalid.json"),"invalid asset falls back");
    for(const auto* name:{L"refresh",L"copy-link"}){
        Lottie::Asset asset;Require(asset.Load(root+L"/assets/lottie/"+name+L".json"),"real JSON load");
        auto t0=std::chrono::steady_clock::now();
        auto first=asset.Sample(0,64);Require(first,"first frame");auto initial=first->bgra;
        auto middle=asset.Sample(.5,64);Require(middle&&middle->bgra!=initial,"JSON keyframes change pixels");
        unsigned clear=0,ink=0,partial=0;
        for(auto pixel:middle->bgra){auto a=pixel>>24;clear+=a==0;ink+=a>0;partial+=a>0&&a<255;Require((pixel&255)<=a&&((pixel>>8)&255)<=a&&((pixel>>16)&255)<=a,"premultiplied alpha");}
        Require(clear>0&&ink>0&&partial>0,"transparent antialiased paths");
        for(unsigned i=0;i<asset.FrameCount();++i)Require(asset.Sample(double(i)/(asset.FrameCount()-1),64),"all frames render");
        if(std::wstring(name)==L"refresh")Require(asset.Sample(1,64)->bgra==initial,"single CCW turn ends pixel-identical to rest");
        Require(asset.CacheBytes()<=(asset.FrameCount()+1)*64*64*4,"bounded single-DPI cache");
        auto fullBytes=asset.CacheBytes();Require(asset.Sample(.5,16),"DPI replacement");
        Require(asset.CacheBytes()<fullBytes,"DPI releases old cache");Require(!asset.Sample(0,65),"oversize rejected");
        Require(asset.Sample(.5,32)&&asset.Sample(.5,64)&&asset.Sample(.5,16),"16 -> 32 -> 64 -> 16 DPI regression");
        std::printf("asset frames=%u duration=%.6f full64Cache=%zu renderAllMs=%.3f\n",asset.FrameCount(),asset.Duration(),fullBytes,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count());
        asset.Clear();Require(asset.CacheBytes()==0,"release cache");
    }
    for(UINT dpi:{96,144,192}){
        Chrome chrome;Require(SUCCEEDED(chrome.InitOffscreen(root,dpi,1180,140)),"production offscreen renderer");
        Motion::State motion(false);motion.EnablePolish();motion.SetInteraction(TabBase,CloseBase,start);chrome.SetMotion(&motion);
        chrome.Paint({L"Google"},0,L"https://example.test/",false,0,0,nullptr,start);
        std::vector<unsigned> rest;
        for(int x=0;x<16;++x)for(int y=0;y<16;++y)rest.push_back(chrome.SurfacePixel(chrome.bar.right-65+x,52+y)&255);
        chrome.lottie.RefreshBegin(start);
        chrome.Paint({L"Google"},0,L"https://example.test/",false,0,0,nullptr,start);
        double difference=0;unsigned n=0;
        for(int x=0;x<16;++x)for(int y=0;y<16;++y)difference+=std::abs(int(rest[n++])-int(chrome.SurfacePixel(chrome.bar.right-65+x,52+y)&255));
        Require(difference/(256*255)<.04,"animated rest brightness matches baked 50-percent Phosphor PNG");
        for(int ms:{0,150,333,800,1900,2200}){
            chrome.lottie.Clear();chrome.lottie.CopySucceeded(start);chrome.lottie.RefreshBegin(start);
            chrome.Paint({L"Google",L"Ggyp \u00c9\u00e7 long deliberately ellipsized title"},0,L"https://example.test/",false,NativeChrome::CloseBase,'R',nullptr,at(ms));
            Require(SUCCEEDED(chrome.SavePng((out+L"/keyframe-"+std::to_wstring(dpi)+L"-"+std::to_wstring(ms)+L".png").c_str())),"keyframe png");
        }
        chrome.Build(2,0);auto close=Design::TabStrip::CloseSlot(Design::TabStrip::Surface(0));
        Require(chrome.Hit(Design::Brand::centerX,Design::Brand::centerY)==900,"brand retains tab-list action at approved position");
        Require(chrome.Hit(Design::TabStrip::Surface(0).left+1,22)==TabBase,"brand hit region cannot intercept the first tab");
        Require(chrome.Hit((close.left+close.right)/2,(close.top+close.bottom)/2)==CloseBase,"centered close hit geometry");
        auto newTab=Design::TabStrip::NewTabLeft(2);Require(newTab-Design::TabStrip::Surface(1).right==6,"last plus gap");
        motion.SetReducedMotion(true);motion.CopySucceeded(start);chrome.lottie.CopySucceeded(start);
        chrome.Paint({L"Google"},0,L"https://example.test/",false,0,0,nullptr,at(300));
        Require(SUCCEEDED(chrome.SavePng((out+L"/reduced-"+std::to_wstring(dpi)+L".png").c_str())),"reduced static success");
        Require(!chrome.lottie.NextWake(at(300),true).delay,"reduced production schedule");
    }
    Require(!GetModuleHandleW(L"WebView2Loader.dll"),"never loaded WebView2");
    CoUninitialize();std::puts("PASS native Lottie pixels, alpha, DPI, resource bounds, lifecycle, stopped timers, reduced motion, geometry.");return 0;
}
