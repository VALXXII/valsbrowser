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
void Require(bool good,const char* message){if(!good){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
Motion::Time T(int ms){return Motion::Time{}+Motion::Milliseconds(ms);}
int Gray(UINT32 pixel){return (int)(pixel&255);}
IStream* TestPng(){
    IStream* stream=nullptr;Require(SUCCEEDED(CreateStreamOnHGlobal(nullptr,TRUE,&stream)),"memory PNG stream");
    IWICImagingFactory* factory=nullptr;IWICBitmapEncoder* encoder=nullptr;IWICBitmapFrameEncode* frame=nullptr;
    Require(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))),"WIC test factory");
    Require(SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder)),"PNG encoder");Require(SUCCEEDED(encoder->Initialize(stream,WICBitmapEncoderNoCache)),"PNG memory output");
    Require(SUCCEEDED(encoder->CreateNewFrame(&frame,nullptr)),"PNG frame");Require(SUCCEEDED(frame->Initialize(nullptr)),"PNG init");Require(SUCCEEDED(frame->SetSize(2,2)),"PNG size");WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;Require(SUCCEEDED(frame->SetPixelFormat(&format)),"PNG pixel format");
    BYTE bytes[]={0,0,0,0,0,0,0,0,0,0,255,255,0,0,255,255};
    Require(SUCCEEDED(frame->WritePixels(2,8,sizeof(bytes),bytes)),"PNG transparent and red pixels");Require(SUCCEEDED(frame->Commit())&&SUCCEEDED(encoder->Commit()),"PNG commit");Drop(frame);Drop(encoder);Drop(factory);return stream;
}
int wmain(int argc,wchar_t** argv){
    Require(argc==3,"root and output path required");Require(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"native COM initialization");
    Require(!GetModuleHandleW(L"WebView2Loader.dll"),"no WebView2 loaded");std::wstring root=argv[1],out=argv[2];
    Chrome chrome;Require(SUCCEEDED(chrome.InitOffscreen(root,96,1180,84)),"native offscreen renderer");
    for(int id:{'R','C'}){
        Motion::State state(false);state.SetInteraction(id,0,T(0));chrome.SetMotion(&state);int previous=41;
        const float x=id=='R'?1010.f:1051.f;
        for(int ms:{0,1,60,120}){chrome.Paint({L"Google"},0,L"https://example.test",false,id,0,nullptr,T(ms));int color=Gray(chrome.SurfacePixel(x,60));Require(color>=41&&color>=previous,"address hover cannot flash darker than its parent");previous=color;std::printf("%c enter t=%d fill=%d\n",id,ms,color);}
        Require(previous==51,"address hover exact shared target");state.SetInteraction(0,0,T(120));previous=51;
        for(int ms:{121,180,240}){chrome.Paint({L"Google"},0,L"https://example.test",false,0,0,nullptr,T(ms));int color=Gray(chrome.SurfacePixel(x,60));Require(color>=41&&color<=previous,"address leave monotonic toward parent without dark flash");previous=color;std::printf("%c leave t=%d fill=%d\n",id,ms,color);}
        Require(previous==41,"address leave returns exact parent surface");
    }
    for(int ms:{0,1,60,120}){int expected=-1;for(int id:{'m','M','X'}){Motion::State state(false);state.SetInteraction(id,0,T(0));chrome.SetMotion(&state);chrome.Paint({L"Google"},0,L"",false,id,0,nullptr,T(ms));float x=id=='m'?1064.f:id=='M'?1104.f:1144.f;int color=Gray(chrome.SurfacePixel(x,20));if(expected<0)expected=color;Require(color==expected,"window close/minimize/maximize hover fills equal at every phase");}std::printf("window controls t=%d equal fill=%d\n",ms,expected);}
    chrome.SetMotion(nullptr);
    IStream* png=TestPng();auto image=chrome.DecodeFavicon(png);Drop(png);Require(image&&image->width==2&&image->height==2&&image->pixels[3]==0&&image->pixels[11]==255,"WIC decodes memory PNG and preserves alpha");
    IStream* empty=nullptr;CreateStreamOnHGlobal(nullptr,TRUE,&empty);Require(!chrome.DecodeFavicon(empty)&&!chrome.DecodeFavicon(nullptr),"null and empty favicon streams are normal absence");BYTE invalid[]={1,2,3};ULONG wrote;empty->Write(invalid,3,&wrote);Require(!chrome.DecodeFavicon(empty),"malformed site image falls back neutrally");Drop(empty);
    FaviconState tab;tab.Navigation(L"document-a");auto a=tab.Request(L"a.ico");tab.Navigation(L"document-b");Require(!tab.Complete(a,image),"old-navigation image callback rejected");auto b=tab.Request(L"b.ico");auto newer=tab.Request(L"b.ico");Require(!tab.Complete(b,image)&&tab.Complete(newer,image),"same-URI favicon changes reject older request");tab.Request(L"");Require(!tab.image,"favicon absence clears previous site image");auto closing=tab.Request(L"last.ico");tab.Close();Require(!tab.Complete(closing,image),"closed tab callback rejected");
    for(UINT dpi:{96,144,192}){Chrome renderer;Require(SUCCEEDED(renderer.InitOffscreen(root,dpi,1180,84)),"favicon DPI renderer");renderer.SetFavicons({image});renderer.Paint({L"Site favicon"},0,L"",false,0,0);auto slot=Design::TabStrip::FaviconSlot(Design::TabStrip::Surface(0));Require((renderer.SurfacePixel(slot.left+8,slot.top+2)&0xffffff)==0x292929,"transparent favicon has no white backing");Require((renderer.SurfacePixel(slot.left+8,slot.top+14)&0xffffff)==0xff0000,"favicon decoded color scales in original slot");Require(SUCCEEDED(renderer.SavePng((out+L"\\favicon-alpha-"+std::to_wstring(dpi)+L".png").c_str())),"native favicon sample exported");renderer.SetFavicons({});renderer.Paint({L"No favicon"},0,L"",false,0,0);Require((renderer.SurfacePixel(slot.left,slot.top)&0xffffff)==0x292929,"neutral fallback has no solid white rectangle");}
    Require(!GetModuleHandleW(L"WebView2Loader.dll"),"finished without loading WebView2");std::puts("PASS: focused hover colors, reverse transitions, memory PNG/alpha, favicon stale-request and close guards; no browser launch.");chrome.Dispose();CoUninitialize();return 0;
}
