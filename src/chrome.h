#pragma once
#include <d2d1.h>
#include <dwrite_1.h>
#include <wincodec.h>
#include <map>
#include "motion.h"
#include "favicon.h"
#include "design-system.h"
#include "motion-presentation.h"
#include "native-lottie.h"
#include "lottie-playback.h"
#include "loading.h"
#include "address.h"
#include <set>
#include <tuple>
#include <cstring>

namespace NativeChrome {
template<class T> void Drop(T*& p){if(p){p->Release();p=nullptr;}}
const wchar_t* FontFiles[]={L"InterDisplay-Regular.ttf",L"InterDisplay-Medium.ttf"};
struct FontEnumerator final:IDWriteFontFileEnumerator {
    LONG refs=1; IDWriteFactory* factory; std::wstring folder; int index=-1; IDWriteFontFile* file=nullptr;
    FontEnumerator(IDWriteFactory* f,std::wstring p):factory(f),folder(std::move(p)){factory->AddRef();}
    ~FontEnumerator(){Drop(file);Drop(factory);}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** p)override{if(!p)return E_POINTER;*p=nullptr;if(iid==IID_IUnknown||iid==__uuidof(IDWriteFontFileEnumerator)){*p=this;AddRef();return S_OK;}return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return InterlockedIncrement(&refs);}
    ULONG STDMETHODCALLTYPE Release()override{auto n=InterlockedDecrement(&refs);if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE MoveNext(BOOL* more)override{Drop(file);*more=++index<(int)(sizeof(FontFiles)/sizeof(FontFiles[0]));if(!*more)return S_OK;return factory->CreateFontFileReference((folder+FontFiles[index]).c_str(),nullptr,&file);}
    HRESULT STDMETHODCALLTYPE GetCurrentFontFile(IDWriteFontFile** out)override{if(!file)return E_FAIL;*out=file;file->AddRef();return S_OK;}
};
struct FontLoader final:IDWriteFontCollectionLoader {
    LONG refs=1;std::wstring folder;explicit FontLoader(std::wstring p):folder(std::move(p)){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** p)override{if(!p)return E_POINTER;*p=nullptr;if(iid==IID_IUnknown||iid==__uuidof(IDWriteFontCollectionLoader)){*p=this;AddRef();return S_OK;}return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return InterlockedIncrement(&refs);}
    ULONG STDMETHODCALLTYPE Release()override{auto n=InterlockedDecrement(&refs);if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE CreateEnumeratorFromKey(IDWriteFactory* f,const void*,UINT32,IDWriteFontFileEnumerator** out)override{*out=new FontEnumerator(f,folder);return S_OK;}
};
constexpr int TabBase=1000, CloseBase=2000;
inline bool IsTab(int id){return id>=TabBase&&id<TabBase+128;}
inline bool IsClose(int id){return id>=CloseBase&&id<CloseBase+128;}
inline int TabIndex(int id){return IsClose(id)?id-CloseBase:IsTab(id)?id-TabBase:-1;}
inline int SelectionAfterClose(int count,int active,int closed){if(count<=1)return -1;if(closed<active)return active-1;if(closed==active)return std::min(active,count-2);return active;}
inline Design::ButtonVariant VariantFor(int id){if(id=='m'||id=='M'||id=='X')return Design::ButtonVariant::Window;if(id=='R'||id=='C')return Design::ButtonVariant::Embedded;if(id=='B'||id=='F')return Design::ButtonVariant::Raised;if(IsClose(id))return Design::ButtonVariant::TabClose;return Design::ButtonVariant::Ghost;}
inline float ControlSurface(int id,float amount){return Design::Resolve(VariantFor(id),{amount,0}).surface;}
struct TextMeasure {float lineHeight=0,baseline=0,originY=0,inkTop=0,inkBottom=0;bool trimmed=false;};
struct Region{int id;D2D1_RECT_F rect;const wchar_t* tip;bool focusable=true;};
class Chrome {
    HWND hwnd=nullptr;std::wstring assets;ID2D1Factory* factory=nullptr;ID2D1RenderTarget* target=nullptr;ID2D1HwndRenderTarget* hwndTarget=nullptr;IWICBitmap* surface=nullptr;ID2D1SolidColorBrush* brush=nullptr;
    IDWriteFactory* write=nullptr;FontLoader* fontLoader=nullptr;IDWriteFontCollection* fonts=nullptr;IWICImagingFactory* wic=nullptr;
    std::map<std::wstring,ID2D1Bitmap*> bitmaps;
    Motion::State* motion=nullptr;
    std::vector<std::shared_ptr<const FaviconImage>> favicons;
    std::map<std::shared_ptr<const FaviconImage>,ID2D1Bitmap*,std::owner_less<std::shared_ptr<const FaviconImage>>> faviconBitmaps;
    // Device-independent DirectWrite layouts reused across frames: animation frames
    // must not rebuild text formats/typography for unchanged labels. Bounded.
    struct TextKey{std::wstring value;float width,height,size,tracking;bool tabular,medium,keycap;
        bool operator<(const TextKey& o)const{return std::tie(value,width,height,size,tracking,tabular,medium,keycap)<std::tie(o.value,o.width,o.height,o.size,o.tracking,o.tabular,o.medium,o.keycap);}};
    struct TextEntry{IDWriteTextLayout* layout=nullptr;TextMeasure measure;};
    std::map<TextKey,TextEntry> textCache;
public:
    // Present without blocking the UI thread on vblank (pacing comes from FrameClock)
    // and keep the back buffer so partial invalidation stays correct.
    D2D1_PRESENT_OPTIONS presentOptions=D2D1_PRESENT_OPTIONS(D2D1_PRESENT_OPTIONS_RETAIN_CONTENTS|D2D1_PRESENT_OPTIONS_IMMEDIATELY);
    bool cacheText=true; // false only for the pacing harness baseline
    // Main chrome keeps 96 (pacing-tested); the downloads surfaces need room for
    // every visible row so animation frames never rebuild layouts.
    size_t textCacheLimit=96;
    unsigned long long textLayoutsCreated=0;
    void ClearTextCache(){for(auto& item:textCache)Drop(item.second.layout);textCache.clear();}
    Loading::Visual loading;
    bool navigationLoading=false;
    // DIP rectangle repainted for loading-only frames (URL surface incl. embedded buttons).
    D2D1_RECT_F LoadingDirty()const{return {bar.left-1,bar.top-1,bar.right+1,bar.bottom+1};}
    // Quantised on-screen identity of the loading fill: equal signatures render
    // identical pixels, so a loading-only frame with an unchanged signature is skipped.
    // The edge is an aliased (pixel-snapped) clip: quarter-pixel buckets are aligned to
    // both integer and half-pixel coverage boundaries, and an edge within 1/256 px of a
    // boundary gets its own signature, so float rounding on either side can never
    // merge a real coverage change. Opacity is not quantised (D2D alpha rounding is
    // not specified): fade frames always repaint.
    long long LoadingSignature(Loading::Visual v)const{
        if(v.fraction<=0||v.opacity<=0)return -1;
        const double x=(bar.left+(bar.right-bar.left)*std::min(v.fraction,1.f))*dpi/96.0*4;
        const long long lo=(long long)std::floor(x-1/64.),hi=(long long)std::floor(x+1/64.);
        uint32_t bits=0;const float opacity=std::min(v.opacity,1.f);memcpy(&bits,&opacity,sizeof(bits));
        return ((lo*2+(hi-lo))<<32)|bits;
    }
    void DrawLoading(D2D1_RECT_F clip){
        if(loading.fraction<=0||loading.opacity<=0)return;
        clip.left=std::max(clip.left,bar.left);clip.top=std::max(clip.top,bar.top);
        clip.right=std::min(clip.right,bar.left+(bar.right-bar.left)*loading.fraction);clip.bottom=std::min(clip.bottom,bar.bottom);
        if(clip.right<=clip.left||clip.bottom<=clip.top)return;
        target->PushAxisAlignedClip(clip,D2D1_ANTIALIAS_MODE_ALIASED);
        Color(1,Design::AddressLoading::opacity*loading.opacity);target->FillRoundedRectangle(D2D1::RoundedRect(bar,Design::Radius::control,Design::Radius::control),brush);
        target->PopAxisAlignedClip();
    }
    Lottie::Asset copyLottie,refreshLottie;
    Lottie::Playback lottie;
    ID2D1Bitmap* lottieBitmap=nullptr;
    unsigned lottieSide=0;
    void InitLottie(){copyLottie.Load(assets+L"lottie\\copy-link.json");refreshLottie.Load(assets+L"lottie\\refresh.json");lottie.Configure(copyLottie.Duration(),refreshLottie.Duration(),copyLottie.FrameCount());}
    bool DrawLottie(Lottie::Asset& asset,double progress,D2D1_RECT_F box,float size,float opacity){
        const unsigned side=std::clamp(static_cast<unsigned>(std::ceil(16.f*dpi/96)),1u,64u);
        auto frame=asset.Sample(progress,side);if(!frame)return false;
        if(lottieSide!=side){Drop(lottieBitmap);lottieSide=side;}
        HRESULT hr=S_OK;
        if(!lottieBitmap)hr=target->CreateBitmap(D2D1::SizeU(side,side),frame->bgra.data(),side*4,D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED)),&lottieBitmap);
        else hr=lottieBitmap->CopyFromMemory(nullptr,frame->bgra.data(),side*4);
        if(FAILED(hr)||!lottieBitmap)return false;
        float x=(box.left+box.right-size)/2,y=(box.top+box.bottom-size)/2;
        target->DrawBitmap(lottieBitmap,{x,y,x+size,y+size},.5f*opacity);return true;
    }
    void ClearFaviconBitmaps(){for(auto& item:faviconBitmaps)Drop(item.second);faviconBitmaps.clear();}
    std::vector<bool> sleeping; // per tab (set with the favicons): suspended background tabs draw dimmed
    void SetFavicons(std::vector<std::shared_ptr<const FaviconImage>> images){favicons=std::move(images);for(auto it=faviconBitmaps.begin();it!=faviconBitmaps.end();){if(std::find(favicons.begin(),favicons.end(),it->first)==favicons.end()){Drop(it->second);it=faviconBitmaps.erase(it);}else ++it;}}
    // Missing/empty/invalid site images are normal favicon absence, not engine errors.
    std::shared_ptr<const FaviconImage> DecodeFavicon(IStream* stream){if(!wic||!stream)return {};STATSTG stat{};if(FAILED(stream->Stat(&stat,STATFLAG_NONAME))||stat.cbSize.QuadPart==0||stat.cbSize.QuadPart>4*1024*1024)return {};LARGE_INTEGER zero{};if(FAILED(stream->Seek(zero,STREAM_SEEK_SET,nullptr)))return {};IWICBitmapDecoder* decoder=nullptr;IWICBitmapFrameDecode* frame=nullptr;IWICFormatConverter* converter=nullptr;UINT w=0,h=0;HRESULT hr=wic->CreateDecoderFromStream(stream,nullptr,WICDecodeMetadataCacheOnLoad,&decoder);if(SUCCEEDED(hr))hr=decoder->GetFrame(0,&frame);if(SUCCEEDED(hr))hr=frame->GetSize(&w,&h);if(SUCCEEDED(hr)&&(!w||!h||w>1024||h>1024))hr=E_INVALIDARG;if(SUCCEEDED(hr))hr=wic->CreateFormatConverter(&converter);if(SUCCEEDED(hr))hr=converter->Initialize(frame,GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom);std::shared_ptr<FaviconImage> image;if(SUCCEEDED(hr)){image=std::make_shared<FaviconImage>();image->width=w;image->height=h;image->stride=w*4;image->pixels.resize(image->stride*h);hr=converter->CopyPixels(nullptr,image->stride,(UINT)image->pixels.size(),image->pixels.data());}Drop(converter);Drop(frame);Drop(decoder);return SUCCEEDED(hr)?image:nullptr;}
    // A site image outside the tab list (suggestions, the field while editing), 16 DIP
    // centred in `box`. Bitmaps share the tab favicon cache (pruned with it).
    bool DrawSiteImage(const std::shared_ptr<const FaviconImage>& image,D2D1_RECT_F box,float opacity=1){if(!image||!image->width||!image->height)return false;
        auto& bitmap=faviconBitmaps[image];if(!bitmap){auto properties=D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));target->CreateBitmap(D2D1::SizeU(image->width,image->height),image->pixels.data(),image->stride,properties,&bitmap);}if(!bitmap)return false;
        const float side=Design::Size::icon,factor=std::min(side/image->width,side/image->height),w=image->width*factor,h=image->height*factor,x=(box.left+box.right-w)/2,y=(box.top+box.bottom-h)/2;
        target->DrawBitmap(bitmap,{x,y,x+w,y+h},opacity);return true;}
    void DrawFavicon(int index,D2D1_RECT_F slot,float opacity=1){if(opacity<=0)return;auto image=index<(int)favicons.size()?favicons[index]:nullptr;if(!image){Icon(L"favicon",slot,16,opacity);return;}auto& bitmap=faviconBitmaps[image];if(!bitmap){auto properties=D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));target->CreateBitmap(D2D1::SizeU(image->width,image->height),image->pixels.data(),image->stride,properties,&bitmap);}if(bitmap){float factor=std::min((slot.right-slot.left)/image->width,(slot.bottom-slot.top)/image->height),w=image->width*factor,h=image->height*factor;float x=(slot.left+slot.right-w)/2,y=(slot.top+slot.bottom-h)/2;target->DrawBitmap(bitmap,{x,y,x+w,y+h},opacity);}else Icon(L"favicon",slot,16,opacity);}
    // Hosts that paint their own surfaces (the downloads popover) reuse the fonts,
    // icons and helpers through these; SwapTarget redirects them to an offscreen layer.
    ID2D1RenderTarget* RenderTarget()const{return target;}
    // Draws into `t` (a compatible render target: it shares this one's brushes and bitmaps) until restored.
    void Redirect(ID2D1RenderTarget* t){target=t;}
    ID2D1SolidColorBrush* SolidBrush()const{return brush;}
    ID2D1Factory* Factory()const{return factory;}
    IWICImagingFactory* Imaging()const{return wic;}
    ID2D1RenderTarget* SwapTarget(ID2D1RenderTarget* next){auto old=target;target=next;return old;}
    void RecreateTarget(){DropFade();ClearFaviconBitmaps();Drop(lottieBitmap);lottieSide=0;for(auto& b:bitmaps)Drop(b.second);bitmaps.clear();Drop(brush);Drop(target);hwndTarget=nullptr;Resize();}
    float TextWidth(const std::wstring& value,float size,bool medium=false,bool tabular=false,float tracking=0){if(value.empty())return 0;IDWriteTextLayout* layout=nullptr;TextMeasure m;if(FAILED(CachedTextLayout(value,{0,0,4096,32},size,tabular,medium,tracking,&layout,m)))return 0;DWRITE_TEXT_METRICS metrics{};layout->GetMetrics(&metrics);Drop(layout);return metrics.widthIncludingTrailingWhitespace;}
    // Toolbar button drawn in its hover state while the popover it opened is shown.
    int latched=0;
    void SetMotion(Motion::State* state){motion=state;}
    bool animateSelection=false;
    std::set<int> disabled;
    bool Enabled(int id)const{return disabled.count(id)==0;}
    void SetEnabled(int id,bool enabled){if(enabled)disabled.erase(id);else disabled.insert(id);}
    int dragIndex=-1;float dragOffset=0;
    UINT dpi=96;float width=1180,height=815;D2D1_RECT_F bar{},edit{};std::vector<Region> regions;
    // Tab strip: adaptive width + horizontal scroll (DIP). `lockedTab` (0 = off) is set by
    // the host after a pointer close and cleared when the pointer leaves the strip.
    float scroll=0,lockedTab=0;Design::TabStrip::Strip strip;
    std::vector<std::wstring> tips;std::wstring brandTip; // owned tooltip text for `regions`
    ID2D1GradientStopCollection* fadeStops=nullptr;ID2D1LinearGradientBrush* fadeBrush=nullptr;
    void DropFade(){Drop(fadeBrush);Drop(fadeStops);DropAddressFade();}
    // Canvas-coloured linear mask over the strip edge that still has hidden tabs:
    // opaque at the clip edge, transparent `fade` DIP inward.
    void DrawFade(float edge,float inner){
        if(!fadeBrush){const float c=Design::Surface::canvas;D2D1_GRADIENT_STOP stops[]={{0,D2D1::ColorF(c,c,c,1)},{1,D2D1::ColorF(c,c,c,0)}};
            if(FAILED(target->CreateGradientStopCollection(stops,2,&fadeStops))||FAILED(target->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({0,0},{1,0}),fadeStops,&fadeBrush))){DropFade();return;}}
        fadeBrush->SetStartPoint({edge,0});fadeBrush->SetEndPoint({inner,0});
        target->FillRectangle(D2D1::RectF(std::min(edge,inner),Design::TabStrip::regionTop,std::max(edge,inner),Design::TabStrip::regionBottom),fadeBrush);
    }
    ~Chrome(){Dispose();}
    void Dispose(){ClearTextCache();DropFade();Drop(lottieBitmap);lottieSide=0;lottie.Clear();copyLottie.Clear();refreshLottie.Clear();ClearFaviconBitmaps();favicons.clear();for(auto& b:bitmaps)Drop(b.second);bitmaps.clear();Drop(brush);Drop(target);hwndTarget=nullptr;Drop(surface);Drop(wic);Drop(fonts);if(write&&fontLoader)write->UnregisterFontCollectionLoader(fontLoader);Drop(fontLoader);Drop(write);Drop(factory);}
    HRESULT InitResources(const std::wstring& root){assets=root+L"\\assets\\";InitLottie();HRESULT hr=D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,&factory);if(FAILED(hr))return hr;
        hr=DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),(IUnknown**)&write);if(FAILED(hr))return hr;
        fontLoader=new FontLoader(assets+L"fonts\\");hr=write->RegisterFontCollectionLoader(fontLoader);if(FAILED(hr))return hr;const UINT key=1;hr=write->CreateCustomFontCollection(fontLoader,&key,sizeof(key),&fonts);if(FAILED(hr))return hr;for(auto family:{L"Inter Display"}){UINT32 index;BOOL exists=FALSE;hr=fonts->FindFamilyName(family,&index,&exists);if(FAILED(hr)||!exists)return E_FAIL;}
        hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic));return hr;
    }
    HRESULT Init(HWND h,const std::wstring& root){hwnd=h;HRESULT hr=InitResources(root);return FAILED(hr)?hr:Resize();}
    HRESULT InitOffscreen(const std::wstring& root,UINT dpiValue,float w,float h){dpi=dpiValue;width=w;height=h;HRESULT hr=InitResources(root);if(FAILED(hr))return hr;hr=wic->CreateBitmap((UINT)(w*dpi/96),(UINT)(h*dpi/96),GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,&surface);if(FAILED(hr))return hr;hr=factory->CreateWicBitmapRenderTarget(surface,D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,D2D1::PixelFormat(),(float)dpi,(float)dpi),&target);if(FAILED(hr))return hr;target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);return target->CreateSolidColorBrush(D2D1::ColorF(1,1,1),&brush);}
    HRESULT SavePng(const wchar_t* path){if(!surface)return E_FAIL;IWICStream* stream=nullptr;IWICBitmapEncoder* encoder=nullptr;IWICBitmapFrameEncode* frame=nullptr;HRESULT hr=wic->CreateStream(&stream);if(SUCCEEDED(hr))hr=stream->InitializeFromFilename(path,GENERIC_WRITE);if(SUCCEEDED(hr))hr=wic->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder);if(SUCCEEDED(hr))hr=encoder->Initialize(stream,WICBitmapEncoderNoCache);if(SUCCEEDED(hr))hr=encoder->CreateNewFrame(&frame,nullptr);if(SUCCEEDED(hr))hr=frame->Initialize(nullptr);if(SUCCEEDED(hr))hr=frame->WriteSource(surface,nullptr);if(SUCCEEDED(hr))hr=frame->Commit();if(SUCCEEDED(hr))hr=encoder->Commit();Drop(frame);Drop(encoder);Drop(stream);return hr;}
    UINT32 SurfacePixel(float x,float y){UINT32 pixel=0;WICRect r{(INT)(x*dpi/96),(INT)(y*dpi/96),1,1};if(surface)surface->CopyPixels(&r,4,4,(BYTE*)&pixel);return pixel;}
    HRESULT Resize(){if(!factory||!hwnd)return S_OK;RECT r;GetClientRect(hwnd,&r);dpi=GetDpiForWindow(hwnd);width=(float)r.right*96/dpi;height=(float)r.bottom*96/dpi;
        if(target){target->SetDpi((float)dpi,(float)dpi);return hwndTarget->Resize(D2D1::SizeU(r.right,r.bottom));}
        HRESULT hr=factory->CreateHwndRenderTarget(D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,D2D1::PixelFormat(),(float)dpi,(float)dpi),D2D1::HwndRenderTargetProperties(hwnd,D2D1::SizeU(r.right,r.bottom),presentOptions),&hwndTarget);if(FAILED(hr))return hr;target=hwndTarget;target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);return target->CreateSolidColorBrush(D2D1::ColorF(1,1,1),&brush);
    }
    void Color(float c,float alpha=1){brush->SetColor(D2D1::ColorF(c,c,c,alpha));}
    void Round(D2D1_RECT_F r,float radius,float c){Color(c);target->FillRoundedRectangle(D2D1::RoundedRect(r,radius,radius),brush);}
    // Enable tabular figures explicitly for counters; titles and URLs keep proportional digits.
    HRESULT MakeTextLayout(const std::wstring& value,D2D1_RECT_F slot,float size,bool tabular,bool medium,float tracking,IDWriteTextLayout** output,TextMeasure& placement){
        if(!write||!fonts||!output)return E_UNEXPECTED;
        IDWriteTextFormat* format=nullptr;IDWriteTextLayout* layout=nullptr;
        HRESULT hr=write->CreateTextFormat(L"Inter Display",fonts,medium?DWRITE_FONT_WEIGHT_MEDIUM:DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,size,L"fr-FR",&format);
        if(FAILED(hr))return hr;
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        const float available=std::max(1.f,slot.right-slot.left);
        hr=write->CreateTextLayout(value.c_str(),(UINT32)value.size(),format,available,4096,&layout);Drop(format);if(FAILED(hr))return hr;++textLayoutsCreated;
        IDWriteTextLayout1* layout1=nullptr;
        if(SUCCEEDED(layout->QueryInterface(__uuidof(IDWriteTextLayout1),(void**)&layout1))){layout1->SetCharacterSpacing(tracking,0,0,{0,(UINT32)value.size()});Drop(layout1);}
        IDWriteTypography* typo=nullptr;write->CreateTypography(&typo);
        // Inter Display variants of the Paper type ramp: cv01-cv04 + cv13; key caps take cv11 instead of cv13.
        if(typo){for(auto tag:{DWRITE_MAKE_OPENTYPE_TAG('c','v','0','1'),DWRITE_MAKE_OPENTYPE_TAG('c','v','0','2'),DWRITE_MAKE_OPENTYPE_TAG('c','v','0','3'),DWRITE_MAKE_OPENTYPE_TAG('c','v','0','4'),keycap?DWRITE_MAKE_OPENTYPE_TAG('c','v','1','1'):DWRITE_MAKE_OPENTYPE_TAG('c','v','1','3')})typo->AddFontFeature({(DWRITE_FONT_FEATURE_TAG)tag,1});if(tabular)typo->AddFontFeature({DWRITE_FONT_FEATURE_TAG_TABULAR_FIGURES,1});layout->SetTypography(typo,{0,(UINT32)value.size()});Drop(typo);}
        DWRITE_TEXT_METRICS metrics{};layout->GetMetrics(&metrics);
        DWRITE_LINE_METRICS line{};UINT32 count=0;layout->GetLineMetrics(&line,1,&count);
        placement.lineHeight=std::max(metrics.height,line.height);placement.baseline=line.baseline;
        // A natural line box, not CSS's tight 14px label bounds. The old short
        // box made DirectWrite trim vertically and CLIP cut G/accents/descenders.
        layout->SetMaxHeight(placement.lineHeight+1);
        placement.trimmed=metrics.widthIncludingTrailingWhitespace>available;
        if(placement.trimmed){DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER,0,0};IDWriteInlineObject* ellipsis=nullptr;write->CreateEllipsisTrimmingSign(layout,&ellipsis);layout->SetTrimming(&trim,ellipsis);Drop(ellipsis);}
        DWRITE_OVERHANG_METRICS overhang{};layout->GetOverhangMetrics(&overhang);
        placement.originY=slot.top+(slot.bottom-slot.top-placement.lineHeight)/2;
        placement.inkTop=placement.originY-overhang.top;
        placement.inkBottom=placement.originY+layout->GetMaxHeight()+overhang.bottom;
        *output=layout;return S_OK;
    }
    TextMeasure MeasureText(const std::wstring& value,D2D1_RECT_F slot,float size,bool medium=false,bool tabular=false,float tracking=0){TextMeasure m;IDWriteTextLayout* layout=nullptr;MakeTextLayout(value,slot,size,tabular,medium,tracking,&layout,m);Drop(layout);return m;}
    // Returns an AddRef'd layout; placement is translated to this slot.
    HRESULT CachedTextLayout(const std::wstring& value,D2D1_RECT_F slot,float size,bool tabular,bool medium,float tracking,IDWriteTextLayout** output,TextMeasure& placement){
        if(!cacheText)return MakeTextLayout(value,slot,size,tabular,medium,tracking,output,placement);
        TextKey key{value,slot.right-slot.left,slot.bottom-slot.top,size,tracking,tabular,medium,keycap};
        auto it=textCache.find(key);
        if(it==textCache.end()){
            if(textCache.size()>=textCacheLimit)ClearTextCache();
            TextEntry entry;const D2D1_RECT_F origin{0,0,slot.right-slot.left,slot.bottom-slot.top};
            HRESULT hr=MakeTextLayout(value,origin,size,tabular,medium,tracking,&entry.layout,entry.measure);if(FAILED(hr))return hr;
            it=textCache.emplace(std::move(key),entry).first;
        }
        placement=it->second.measure;placement.originY+=slot.top;placement.inkTop+=slot.top;placement.inkBottom+=slot.top;
        *output=it->second.layout;(*output)->AddRef();return S_OK;
    }
    // Text colour (alpha unused: Text's opacity applies). White everywhere except
    // where a surface sets it around a call (danger text, dark monograms).
    D2D1_COLOR_F ink{1,1,1,1};
    // Keyboard key caps (menus) use their own variant set; set around the call like `ink`.
    bool keycap=false;
    void Text(const std::wstring& value,D2D1_RECT_F slot,float size,float opacity,bool tabular=false,bool medium=false,float tracking=0){if(value.empty())return;IDWriteTextLayout* layout=nullptr;TextMeasure m;if(FAILED(CachedTextLayout(value,slot,size,tabular,medium,tracking,&layout,m)))return;
        // Restrict horizontal overflow to its slot, but retain all measured
        // vertical glyph ink. All labels use the full 32 DIP control height.
        auto clip=slot;clip.top=std::min(slot.top,m.inkTop-96.f/dpi);clip.bottom=std::max(slot.bottom,m.inkBottom+96.f/dpi);
        target->PushAxisAlignedClip(clip,D2D1_ANTIALIAS_MODE_ALIASED);brush->SetColor(D2D1::ColorF(ink.r,ink.g,ink.b,opacity));
        target->DrawTextLayout(D2D1::Point2F(slot.left,m.originY),layout,brush,D2D1_DRAW_TEXT_OPTIONS_NONE);
        target->PopAxisAlignedClip();Drop(layout);
    }
    ID2D1Bitmap* IconBitmap(const wchar_t* name){int imageDpi=384;for(int d:{96,120,144,168,192,240,288,384})if(d>=int(dpi)){imageDpi=d;break;}std::wstring key=std::wstring(name)+L"-"+std::to_wstring(imageDpi)+L".png";auto& bitmap=bitmaps[key];
        if(!bitmap){IWICBitmapDecoder* dec=nullptr;IWICBitmapFrameDecode* frame=nullptr;IWICFormatConverter* conv=nullptr;HRESULT hr=wic->CreateDecoderFromFilename((assets+L"icons\\"+key).c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&dec);if(SUCCEEDED(hr))hr=dec->GetFrame(0,&frame);if(SUCCEEDED(hr))hr=wic->CreateFormatConverter(&conv);if(SUCCEEDED(hr))hr=conv->Initialize(frame,GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom);if(SUCCEEDED(hr))target->CreateBitmapFromWicBitmap(conv,nullptr,&bitmap);Drop(conv);Drop(frame);Drop(dec);}
        return bitmap;
    }
    // Any bundled image under assets\ (e.g. L"engines\\google.png"), cached like the icons; null when missing.
    ID2D1Bitmap* AssetBitmap(const std::wstring& relative){auto& bitmap=bitmaps[L"asset:"+relative];
        if(!bitmap){IWICBitmapDecoder* dec=nullptr;IWICBitmapFrameDecode* frame=nullptr;IWICFormatConverter* conv=nullptr;HRESULT hr=wic->CreateDecoderFromFilename((assets+relative).c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&dec);if(SUCCEEDED(hr))hr=dec->GetFrame(0,&frame);if(SUCCEEDED(hr))hr=wic->CreateFormatConverter(&conv);if(SUCCEEDED(hr))hr=conv->Initialize(frame,GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom);if(SUCCEEDED(hr))target->CreateBitmapFromWicBitmap(conv,nullptr,&bitmap);Drop(conv);Drop(frame);Drop(dec);}
        return bitmap;
    }
    void Icon(const wchar_t* name,D2D1_RECT_F rect,float size=16,float opacity=1){if(opacity<=0)return;
        if(auto* bitmap=IconBitmap(name)){float x=(rect.left+rect.right-size)/2,y=(rect.top+rect.bottom-size)/2;target->DrawBitmap(bitmap,D2D1::RectF(x,y,x+size,y+size),opacity);}
    }
    // The white glyph used as a mask for a colour (icons ship white only).
    void TintedIcon(const wchar_t* name,D2D1_RECT_F rect,float size,D2D1_COLOR_F color){if(color.a<=0)return;auto* bitmap=IconBitmap(name);if(!bitmap)return;
        const float x=(rect.left+rect.right-size)/2,y=(rect.top+rect.bottom-size)/2;const auto mode=target->GetAntialiasMode();target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
        brush->SetColor(color);const auto source=bitmap->GetSize();target->FillOpacityMask(bitmap,brush,D2D1_OPACITY_MASK_CONTENT_GRAPHICS,D2D1::RectF(x,y,x+size,y+size),D2D1::RectF(0,0,source.width,source.height));target->SetAntialiasMode(mode);
    }
    // Window close (Paper): hover warms the plate to #FA6A59 at 30% and the glyph to #FFD0C8 at 80%;
    // pressing deepens the plate. Other window controls keep the neutral Window variant.
    void DrawCloseWindow(D2D1_RECT_F box,Design::ButtonState state){
        const float h=state.disabled?0:std::clamp(state.hover,0.f,1.f),p=state.disabled?0:std::clamp(state.press,0.f,1.f);
        if(h>0||p>0){brush->SetColor(D2D1::ColorF(250/255.f,106/255.f,89/255.f,.30f*h+.12f*p));target->FillRoundedRectangle(D2D1::RoundedRect(box,Design::Radius::control,Design::Radius::control),brush);}
        if(state.focus&&!state.disabled)FocusRing(box,Design::Radius::control);
        const float size=Design::Size::windowIcon*Motion::PressScale(p,motion&&motion->ReducedMotion(),!state.disabled);
        // close-mask is the glyph at full opacity (the regular icons bake in 50%): at rest it matches
        // the neighbouring window icons (50% white); hovered it reaches #FFD0C8 at 95%, clearly legible.
        const float opacity=state.disabled?Design::Text::disabled*.5f:.5f+.45f*h;
        TintedIcon(L"close-mask",box,size,D2D1::ColorF(1,1+(208/255.f-1)*h,1+(200/255.f-1)*h,opacity));
    }
    // Folder glyph shared by the settings and download surfaces: closed at rest, it opens with
    // `open` (the owner's hover tween, so no extra frames): cross-fade, the open folder tipping in
    // from its bottom-left hinge (-10 deg, 90%). Reduced motion keeps only the cross-fade.
    void FolderGlyph(D2D1_RECT_F box,float size,float opacity,float open,bool reduced){
        const float h=std::clamp(open,0.f,1.f);
        Icon(L"folder-closed",box,size,opacity*(1-h));
        if(h<=.001f)return;
        D2D1_MATRIX_3X2_F base;target->GetTransform(&base);
        const D2D1_POINT_2F hinge{(box.left+box.right-size)/2,(box.top+box.bottom+size)/2};
        if(!reduced)target->SetTransform(D2D1::Matrix3x2F::Scale(.9f+.1f*h,.9f+.1f*h,hinge)*D2D1::Matrix3x2F::Rotation(-10.f*(1-h),hinge)*base);
        Icon(L"folder",box,size,opacity*h);
        target->SetTransform(base);
    }
    // True when focus last moved by keyboard (Tab, arrows, shortcuts); a pointer press clears it.
    bool keyboardModality=true;
    void FocusRing(D2D1_RECT_F box,float radius){const float inset=Design::Size::focusStroke/2;box.left+=inset;box.top+=inset;box.right-=inset;box.bottom-=inset;Color(Design::Focus::color,Design::Focus::Opacity(keyboardModality));target->DrawRoundedRectangle(D2D1::RoundedRect(box,radius,radius),brush,Design::Size::focusStroke);}
    void DrawButton(const wchar_t* icon,D2D1_RECT_F box,Design::ButtonVariant variant,Design::ButtonState state){
        auto style=Design::Resolve(variant,state);
        if(style.fill){Round(box,style.radius,style.surface);if(variant==Design::ButtonVariant::Embedded)DrawLoading(box);}
        if(style.focus)FocusRing(box,style.radius);
        Icon(icon,box,style.iconSize*Motion::PressScale(state.press,motion&&motion->ReducedMotion(),!state.disabled),style.iconOpacity);
    }
    HRESULT PaintStateSheet(){if(!target)return E_UNEXPECTED;target->BeginDraw();target->Clear(D2D1::ColorF(Design::Surface::canvas,Design::Surface::canvas,Design::Surface::canvas));
        Text(L"Native components",{24,16,700,48},20,1,false,true);const bool modality=keyboardModality;
        const wchar_t* labels[]={L"Default",L"Hover",L"Pressed",L"Disabled",L"Keyboard focus",L"Pointer focus"};
        const wchar_t* rows[]={L"Ghost / new tab",L"Raised / navigation",L"Embedded / reload",L"Window controls"};
        Design::ButtonVariant variants[]={Design::ButtonVariant::Ghost,Design::ButtonVariant::Raised,Design::ButtonVariant::Embedded,Design::ButtonVariant::Window};
        for(int col=0;col<6;++col)Text(labels[col],{240.f+col*128,56,364.f+col*128,88},14,.7f);
        for(int row=0;row<4;++row){float y=104.f+row*64;Text(rows[row],{24,y,230,y+32},14,.7f);
            for(int col=0;col<6;++col){float x=240.f+col*128;Design::ButtonState state{col==1?1.f:0.f,col==2?1.f:0.f,col==3,col>=4};keyboardModality=col!=5;
                if(row==2)Round({x-4,y-4,x+100,y+36},Design::Radius::control,Design::Surface::raised);
                DrawButton(row==0?L"plus":row==1?L"back":row==2?L"reload":L"minimize",{x,y,x+32,y+32},variants[row],state);
                if(row==3){DrawButton(L"maximize",{x+36,y,x+68,y+32},variants[row],state);DrawButton(L"close",{x+72,y,x+104,y+32},variants[row],state);}
            }
        }
        keyboardModality=true;
        Text(L"Tab surfaces / stable close slot",{24,384,400,416},14,.7f);
        for(int col=0;col<3;++col){float x=240.f+col*190;D2D1_RECT_F tab{x,432,x+128,464};Round(tab,Design::Radius::control,Design::TabSurface(col==0,col==1?1:0));DrawFavicon(0,{x+8,440,x+24,456});Text(L"Ggyp \u00c9\u00e7",{x+32,432,x+96,464},14,.5f,false,true);if(col!=2)Icon(L"tab-close",{x+100,436,x+124,460});if(col==2)FocusRing(tab,Design::Radius::control);}keyboardModality=modality;
        Text(L"Production Direct2D + DirectWrite / Phosphor Bold / no WebView2",{24,496,900,528},14,.7f);
        return target->EndDraw();
    }
    // ---- Address field (issue #20) -------------------------------------------------
    // Set by the host before Build/Paint: what the bar reads outside editing, and the
    // leading glyph while the EDIT is open (null: the security icon).
    AddressBar::Display address;
    const wchar_t* editIcon=nullptr;
    std::shared_ptr<const FaviconImage> editFavicon; // the active suggestion's remembered site icon
    bool addressEditing=false;
    D2D1_RECT_F indicator{};
    ID2D1GradientStopCollection* addressFadeStops=nullptr;ID2D1LinearGradientBrush* addressFadeBrush=nullptr;
    void DropAddressFade(){Drop(addressFadeBrush);Drop(addressFadeStops);}
    float AddressCenter()const{return (bar.top+bar.bottom)/2;}
    D2D1_RECT_F AddressSlot()const{const float c=AddressCenter(),l=bar.left+Design::Address::slotInset;return {l,c-Design::Address::slot/2,l+Design::Address::slot,c+Design::Address::slot/2};}
    float ChipWidth(const wchar_t* label){namespace A=Design::Address;return A::chipPadLeft+Design::Size::icon+Design::Space::xs+TextWidth(label,Design::Type::label.size,true,false,Design::Type::label.tracking)+A::chipPadRight;}
    // Hit/tooltip box of the indicator: the icon slot, or the whole chip. None while editing.
    D2D1_RECT_F IndicatorBox(){if(addressEditing||address.security==AddressBar::Security::Empty)return {};auto box=AddressSlot();const wchar_t* chip=address.chip();if(*chip)box.right=box.left+ChipWidth(chip);return box;}
    D2D1_COLOR_F DangerColor(float a=1)const{return D2D1::ColorF(Design::Danger::r,Design::Danger::g,Design::Danger::b,a);}
    void FillBox(D2D1_RECT_F r,float radius,D2D1_COLOR_F c){brush->SetColor(c);target->FillRoundedRectangle(D2D1::RoundedRect(r,radius,radius),brush);}
    void DrawAddress(bool editing,bool indicatorHover){
        namespace A=Design::Address;using AddressBar::Security;using AddressBar::Tone;
        const auto slot=AddressSlot();const float right=edit.right,cy=AddressCenter();
        const D2D1_RECT_F line{0,bar.top,0,bar.bottom};
        if(editing){if(!DrawSiteImage(editFavicon,slot))Icon(editIcon?editIcon:address.icon(),slot,Design::Size::icon,A::secondary);return;}
        if(address.security==Security::Empty){Icon(L"search",slot,Design::Size::icon,A::secondary);
            Text(L"Search, browse, whatever",{bar.left+A::textInset,line.top,right,line.bottom},Design::Type::address.size,Design::Text::address,false,Design::Type::address.medium,Design::Type::address.tracking);return;}
        const bool danger=address.security==Security::CertificateError;
        float x=bar.left+A::textInset;
        if(const wchar_t* chip=address.chip();*chip){
            D2D1_RECT_F plate{slot.left,slot.top,slot.left+ChipWidth(chip),slot.bottom};
            const float lift=indicatorHover?.04f:0;
            FillBox(plate,A::chipRadius,danger?DangerColor(Design::Danger::plate+lift):D2D1::ColorF(1,1,1,A::chipPlate+lift));
            D2D1_RECT_F glyph{plate.left+A::chipPadLeft,cy-Design::Size::icon/2,plate.left+A::chipPadLeft+Design::Size::icon,cy+Design::Size::icon/2};
            if(danger)TintedIcon(address.icon(),glyph,A::chipIcon,DangerColor());else Icon(address.icon(),glyph,A::chipIcon,A::secondary);
            if(danger)ink=DangerColor();
            Text(chip,{glyph.right+Design::Space::xs,line.top,plate.right,line.bottom},Design::Type::label.size,danger?1.f:A::secondary,false,true,Design::Type::label.tracking);
            ink=D2D1::ColorF(1,1,1,1);
            const float d=plate.right+A::divider;Color(1,.2f);target->FillRectangle(D2D1::RectF(d,cy-5,d+1,cy+5),brush);
            x=d+1+A::divider;
        }else{
            if(indicatorHover)FillBox(slot,A::chipRadius,D2D1::ColorF(1,1,1,A::chipPlate));
            Icon(address.icon(),slot,Design::Size::icon,A::secondary);
        }
        // Runs share one baseline; the overflowing tail is clipped under a fade, never ellipsised.
        target->PushAxisAlignedClip(D2D1::RectF(x,line.top,right,line.bottom),D2D1_ANTIALIAS_MODE_ALIASED);
        const float start=x;
        for(auto& run:address.runs){if(x>=right)break;
            const float w=TextWidth(run.text,Design::Type::address.size,Design::Type::address.medium,false,Design::Type::address.tracking);
            const float opacity=run.tone==Tone::Primary?A::primary:run.tone==Tone::Danger?1.f:A::secondary;
            if(run.tone==Tone::Danger)ink=DangerColor();
            Text(run.text,{x,line.top,x+w+1,line.bottom},Design::Type::address.size,opacity,false,Design::Type::address.medium,Design::Type::address.tracking);
            if(run.strike){auto m=MeasureText(L"x",{x,line.top,x+w+1,line.bottom},Design::Type::address.size);const float y=std::round(m.originY+m.baseline-Design::Type::address.size*.27f);
                brush->SetColor(ink);target->FillRectangle(D2D1::RectF(x,y,x+w,y+1),brush);}
            ink=D2D1::ColorF(1,1,1,1);x+=w;
        }
        target->PopAxisAlignedClip();
        if(x>right&&start<right-A::fade){
            if(!addressFadeBrush){const float c=Design::Surface::raised;D2D1_GRADIENT_STOP stops[]={{0,D2D1::ColorF(c,c,c,0)},{1,D2D1::ColorF(c,c,c,1)}};
                if(FAILED(target->CreateGradientStopCollection(stops,2,&addressFadeStops))||FAILED(target->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({0,0},{1,0}),addressFadeStops,&addressFadeBrush)))DropAddressFade();}
            if(addressFadeBrush){addressFadeBrush->SetStartPoint({right-A::fade,0});addressFadeBrush->SetEndPoint({right,0});target->FillRectangle(D2D1::RectF(right-A::fade,bar.top+1,right,bar.bottom-1),addressFadeBrush);}
        }
    }
    void Build(int count,int selected,const std::vector<std::wstring>* titles=nullptr){regions.clear();tips.clear();
        namespace TS=Design::TabStrip;strip=TS::Layout(width,count,selected,scroll,lockedTab);scroll=strip.scroll;
        brandTip=L"Liste des onglets ("+std::to_wstring(count)+L") · molette pour changer";
        auto brand=Design::Brand::Region();regions.push_back({900,{brand.left,brand.top,brand.right,brand.bottom},brandTip.c_str()});
        // Tooltips carry the full title: compact tabs truncate or hide it.
        tips.reserve(count);for(int i=0;i<count;i++){const std::wstring title=titles&&i<(int)titles->size()?(*titles)[i]:L"";tips.push_back((title.empty()?L"Onglet":title)+L" - Ctrl+1...8 / Ctrl+9 (dernier) - Ctrl+Maj+PgPréc/PgSuiv pour déplacer - clic milieu pour fermer");}
        for(int i=0;i<count;i++){if(!strip.Visible(i))continue;
            auto close=strip.CloseIn(strip.Surface(i));auto region=strip.Region(i);
            region.left=std::max(region.left,TS::left);region.right=std::min(region.right,strip.ViewRight());
            // Close first: its hit area wins over the containing tab, and IDs
            // never collide with tabs, keyboard commands or toolbar buttons.
            // Tabs under a fade stay clickable (selecting scrolls them clear) but
            // expose no close target and take no keyboard focus.
            const bool clear=strip.Clear(i);
            if(clear&&strip.Closable(i,selected))regions.push_back({CloseBase+i,{close.left,close.top,close.right,close.bottom},L"Fermer cet onglet"});
            regions.push_back({TabBase+i,{region.left,region.top,region.right,region.bottom},tips[i].c_str(),clear});
        }
        float x=strip.NewTabLeft();regions.push_back({'T',{x,6,x+Design::Size::control,38},L"Nouvel onglet (Ctrl+T)"});
        regions.push_back({'m',{width-118,4,width-86,36},L"Réduire"});regions.push_back({'M',{width-78,4,width-46,36},L"Agrandir / restaurer"});regions.push_back({'X',{width-38,4,width-6,36},L"Fermer la fenêtre"});
        regions.push_back({'B',{6,44,38,76},L"Précédent (Alt+←)"});regions.push_back({'F',{44,44,76,76},L"Suivant (Alt+→)"});bar={82,44,width-133,76};edit={bar.left+Design::Address::textInset,44,bar.right-79,76};
        indicator=IndicatorBox();if(indicator.right>indicator.left)regions.push_back({'I',indicator,address.tip(),false});
        regions.push_back({'L',{bar.left,44,bar.right-73,76},L"Adresse ou recherche (Ctrl+L / Alt+D)"});regions.push_back({'R',{bar.right-69,48,bar.right-69+Design::Size::compact,72},navigationLoading?L"Arr\u00eater le chargement (Ctrl+R)":L"Actualiser (Ctrl+R)"});regions.push_back({'C',{bar.right-28,48,bar.right-4,72},L"Copier le lien"});
        regions.push_back({'Y',{width-127,48,width-103,72},L"Historique (Ctrl+H)"});regions.push_back({'D',{width-97,48,width-73,72},L"Téléchargements"});regions.push_back({'P',{width-67,48,width-43,72},L"Navigation privée : disponibilité"});regions.push_back({'S',{width-30,48,width-6,72},L"Réglages"});
    }
    std::vector<int> FocusOrder()const{std::set<int> closes;for(auto& region:regions)if(IsClose(region.id))closes.insert(region.id);std::vector<int> order;for(auto& region:regions){if(IsClose(region.id)||!region.focusable)continue;if(Enabled(region.id))order.push_back(region.id);if(IsTab(region.id)){int close=CloseBase+TabIndex(region.id);if(closes.count(close)&&Enabled(close))order.push_back(close);}}return order;}
    int Hit(float x,float y){for(auto& r:regions)if(x>=r.rect.left&&x<r.rect.right&&y>=r.rect.top&&y<r.rect.bottom)return Enabled(r.id)?r.id:0;return 0;}
    bool AssetsReady(){if(bitmaps.size()<11)return false;for(auto& b:bitmaps)if(!b.second)return false;return fonts&&fonts->GetFontFamilyCount()==1;}
    // Smoke diagnostics: why AssetsReady() is false (bitmap count, keys that failed to load, font families).
    std::wstring AssetsReport(){std::wstring s=L"bitmaps="+std::to_wstring(bitmaps.size())+L" fonts="+std::to_wstring(fonts?fonts->GetFontFamilyCount():0)+L" missing=";for(auto& b:bitmaps)if(!b.second)s+=b.first+L";";return s;}
    void Paint(const std::vector<std::wstring>& titles,int selected,const std::wstring& uri,bool editing,int hover,int focus,const D2D1_RECT_F* dirty=nullptr,Motion::Time now=Motion::Now()){(void)uri;/* the field reads `address`, set by the host */if(!target||!brush)return;Build((int)titles.size(),selected,&titles);target->BeginDraw();target->PushAxisAlignedClip(dirty?*dirty:D2D1::RectF(0,0,width,height),D2D1_ANTIALIAS_MODE_ALIASED);target->Clear(D2D1::ColorF(Design::Surface::canvas,Design::Surface::canvas,Design::Surface::canvas));
        Round(bar,Design::Radius::control,Design::Surface::raised);DrawLoading(bar);if(focus=='L')FocusRing(bar,Design::Radius::control);
        // Skip controls wholly outside the dirty rectangle (4 DIP margin covers focus rings).
        auto visible=[&](D2D1_RECT_F b){return !dirty||(b.right+4>dirty->left&&b.left-4<dirty->right&&b.bottom+4>dirty->top&&b.top-4<dirty->bottom);};
        auto paintRegions=regions;
        // Paint the grabbed tab last so it stays above its neighbours.
        if(dragIndex>=0){auto it=std::find_if(paintRegions.begin(),paintRegions.end(),[&](const Region& r){return r.id==TabBase+dragIndex;});if(it!=paintRegions.end()){auto r=*it;paintRegions.erase(it);paintRegions.push_back(r);}}
        for(auto& r:paintRegions){int id=r.id;auto box=r.rect;if(IsClose(id)||!visible(box))continue;if(IsTab(id)){int n=TabIndex(id);auto surface=strip.Surface(n);if(n==dragIndex){surface.left+=dragOffset;surface.right+=dragOffset;}D2D1_RECT_F tab{surface.left,surface.top,surface.right,surface.bottom};auto close=strip.CloseIn(surface);auto title=strip.TitleIn(surface);auto favicon=strip.FaviconIn(surface);const bool closable=strip.Clear(n)&&strip.Closable(n,selected)&&n!=dragIndex;
                // Partially scrolled tabs are cut at the strip viewport, then masked.
                target->PushAxisAlignedClip(D2D1::RectF(strip.ViewLeft(),Design::TabStrip::regionTop,strip.ViewRight(),Design::TabStrip::regionBottom),D2D1_ANTIALIAS_MODE_ALIASED);bool tabHovered=TabIndex(hover)==n;float tabHover=motion?std::max(motion->Visual(TabBase+n,now).hover,motion->Visual(CloseBase+n,now).hover):(tabHovered?1.f:0.f);
                // Exactly one opaque surface: no second close-button hover fill.
                float emphasis=dragIndex<0&&motion&&animateSelection?motion->selection.Visual(n+1,now):(n==selected?1.f:0.f);
                float base=Design::TabSurface(false,tabHover);
                if(emphasis>0||tabHover>0||n==dragIndex){Round(tab,Design::Radius::control,base+(Design::Surface::selected-base)*emphasis);}
                if(focus==TabBase+n)FocusRing(tab,Design::Radius::control);
                // Narrow selected tab: the close glyph cross-fades in over the favicon it replaces.
                const float closeShown=closable?(focus==CloseBase+n?1.f:tabHover):0.f;
                // Sleeping (never the selected tab): favicon at 40%, title at 32% instead of muted.
                const bool asleep=n!=selected&&n<(int)sleeping.size()&&sleeping[n];
                DrawFavicon(n,{favicon.left,favicon.top,favicon.right,favicon.bottom},(strip.SwapsClose(n,selected)?1-closeShown:1)*(asleep?.4f:1));
                if(strip.Titled()&&title.right-title.left>=Design::Size::icon)Text(titles[n],{title.left,title.top,title.right,title.bottom},Design::Type::label.size,n==selected?Design::Text::secondary:asleep?.32f:Design::Text::muted,false,Design::Type::label.medium,Design::Type::label.tracking);
                if(closable&&(tabHover>0||focus==CloseBase+n)){float press=motion?motion->Visual(CloseBase+n,now).press:0;Icon(L"tab-close",{close.left,close.top,close.right,close.bottom},Design::Size::icon*Motion::PressScale(press,motion&&motion->ReducedMotion()),closeShown);}
                if(closable&&focus==CloseBase+n)FocusRing({close.left,close.top,close.right,close.bottom},Design::Radius::button);
                target->PopAxisAlignedClip();continue;}
            if(id==900){auto glyph=Design::Brand::Glyph();Icon(L"brand-flower",{glyph.left,glyph.top,glyph.right,glyph.bottom},Design::Brand::size,Design::Brand::opacity);if(focus==900)FocusRing(box,Design::Radius::control);continue;}if(id=='L'||id=='I')continue;
            auto interaction=motion?motion->Visual(id,now):Motion::InteractionVisual{hover==id?1.f:0.f,0};if(id==latched)interaction.hover=1;
            const wchar_t* icon=nullptr;switch(id){case 'T':icon=L"plus";break;case 'm':icon=L"minimize";break;case 'M':icon=L"maximize";break;case 'X':icon=L"close";break;case 'B':icon=L"back";break;case 'F':icon=L"forward";break;case 'R':icon=navigationLoading?L"tab-close":L"reload";break;case 'C':icon=L"copy";break;case 'Y':icon=L"history";break;case 'D':icon=L"downloads";break;case 'P':icon=L"private";break;case 'S':icon=L"settings";break;}if(icon){auto variant=VariantFor(id);Design::ButtonState state{interaction.hover,interaction.press,!Enabled(id),focus==id};
                if((id=='C'||id=='R')&&motion){
                    auto style=Design::Resolve(variant,state);if(style.fill){Round(box,style.radius,style.surface);if(variant==Design::ButtonVariant::Embedded)DrawLoading(box);}if(style.focus)FocusRing(box,style.radius);
                    float size=style.iconSize*Motion::PressScale(state.press,motion->ReducedMotion(),!state.disabled);
                    bool painted=false;
                    if(!motion->ReducedMotion()){
                        if(id=='C'&&lottie.CopyActive(now))painted=DrawLottie(copyLottie,lottie.CopyProgress(now),box,size,style.iconOpacity);
                        if(id=='R'&&!navigationLoading&&lottie.RefreshActive(now))painted=DrawLottie(refreshLottie,lottie.RefreshProgress(now),box,size,style.iconOpacity);
                    }
                    // Missing/invalid assets keep the native static Phosphor endpoint.
                    if(!painted&&id=='C'&&(motion->ReducedMotion()||copyLottie.Duration()==0)){
                        float a=motion->copy.Visual(now).opacity;
                        Icon(icon,box,size,(1-a)*style.iconOpacity);Icon(L"check",box,size,a*style.iconOpacity);
                    }else if(!painted)Icon(icon,box,size,style.iconOpacity);
                }
                else if(id=='X')DrawCloseWindow(box,state);
                else DrawButton(icon,box,variant,state);}

        }
        if(strip.HiddenBefore())DrawFade(strip.ViewLeft(),strip.ViewLeft()+Design::TabStrip::fade);
        if(strip.HiddenAfter())DrawFade(strip.ViewRight(),strip.ViewRight()-Design::TabStrip::fade);
        Color(Design::Text::primary,Design::Surface::separatorOpacity);target->FillRoundedRectangle(D2D1::RoundedRect({bar.right-37,55,bar.right-36,65},.5f,.5f),brush);target->FillRoundedRectangle(D2D1::RoundedRect({width-37,55,width-36,65},.5f,.5f),brush);
        DrawAddress(editing,hover=='I');
        target->PopAxisAlignedClip();HRESULT hr=target->EndDraw();if(hr==D2DERR_RECREATE_TARGET){DropFade();ClearFaviconBitmaps();for(auto& b:bitmaps)Drop(b.second);bitmaps.clear();Drop(brush);Drop(target);hwndTarget=nullptr;Resize();}
    }
};
}
