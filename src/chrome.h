#pragma once
#include "i18n.h"
#include <d2d1_1.h>
#include <commctrl.h>
#include <d2d1effects.h>
#include <dwrite_1.h>
#include <wincodec.h>
#include <map>
#include <functional>
#include "motion.h"
#include "favicon.h"
#include "design-system.h"
#include "motion-presentation.h"
#include "native-lottie.h"
#include "lottie-playback.h"
#include "download-icon.h"
#include "audio-mixer.h"
#include "nav-motion.h"
#include "loading.h"
#include "tab-motion.h"
#include "address.h"
#include "bookmarks-bar.h"
#include "svg-path.h"
#include "site-request.h"
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
// Bookmarks band items: BookmarkBase + visible index (no collision with tabs, closes or the 3000+ keyboard commands).
constexpr int BookmarkBase=4000, BookmarkLimit=1000;
inline bool IsBookmark(int id){return id>=BookmarkBase&&id<BookmarkBase+BookmarkLimit;}
inline int BookmarkIndex(int id){return IsBookmark(id)?id-BookmarkBase:-1;}
inline bool IsTab(int id){return id>=TabBase&&id<TabBase+128;}
inline bool IsClose(int id){return id>=CloseBase&&id<CloseBase+128;}
inline int TabIndex(int id){return IsClose(id)?id-CloseBase:IsTab(id)?id-TabBase:-1;}
inline int SelectionAfterClose(int count,int active,int closed){if(count<=1)return -1;if(closed<active)return active-1;if(closed==active)return std::min(active,count-2);return active;}
inline Design::ButtonVariant VariantFor(int id){if(id=='m'||id=='M'||id=='X')return Design::ButtonVariant::Window;if(id=='C'||id=='K')return Design::ButtonVariant::Embedded;if(id=='B'||id=='F'||id=='R')return Design::ButtonVariant::Raised;if(IsClose(id))return Design::ButtonVariant::TabClose;return Design::ButtonVariant::Ghost;}
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
    // DIP rectangle repainted for download-icon-only frames (the 'D' button).
    // The ripple grows well past the button (x4): the rectangle covers its reach,
    // down to the chrome's lower edge (the page covers what lies below).
    D2D1_RECT_F DownloadIconDirty()const{const float cx=width-55,cy=60,r=DownloadIcon::Ripple::reach*Design::Size::icon/256+1;return {cx-r,cy-r,cx+r,std::min(cy+r,ContentTop())};}
    // Audio button (left of History, only while a site has made sound): icon-only frames.
    static constexpr float AudioRight=103,AudioLeft=127;   // from the window's right edge; the field ends 6 DIP before
    bool audioShown=false;   // tooltip resolved at use (TR(chrome_tip_audio)): `chrome` is a global built before the language is pinned
    Audio::IconModel audioIcon;
    D2D1_RECT_F AudioIconDirty()const{return {width-AudioLeft-1,47,width-AudioRight+1,73};}
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
    // Split view canvas: rails (collapsed sites), the active panel's accent ring drawn
    // 1 DIP off the page (2 DIP stroke, inside the 4 DIP half-gap) and the separator handles.
    void DrawSplit(){
        if(!split.shown)return;
        for(size_t i=0;i<split.rails.size();++i){auto r=split.rails[i];Round(r,Design::Radius::control,Design::Surface::tabHover);
            const float cx=(r.left+r.right)/2;const int tab=i<split.railTabs.size()?split.railTabs[i]:-1;
            const bool vertical=r.bottom-r.top>r.right-r.left;const float top=vertical?r.top+12:(r.top+r.bottom)/2-8;const float left=vertical?cx-8:r.left+12;
            if(tab>=0)DrawFavicon(tab,{left,top,left+16,top+16});}
        // Active panel ring (Paper 08): neutral, 1 DIP off the page, 2 DIP. After a click it is
        // discreet (white 10 %); reached by keyboard (F6, menu) it is the focus ring's 50 % (5.26:1).
        // The accent is kept for drop previews and manipulation, never a permanent frame.
        // At an outer bottom corner the page is round (6 DIP): the ring follows it (8 on its centre line).
        if(split.ring){auto a=split.active;Color(1,keyboardModality?Design::Focus::keyboardOpacity:Design::Focus::pointerOpacity);
            if(auto g=CornerBox({a.left-2,a.top-2,a.right+2,a.bottom+2},2,2,split.activeBottomRight?8.f:2.f,split.activeBottomLeft?8.f:2.f)){target->DrawGeometry(g,brush,2);g->Release();}}
        // Separator (Paper 08): at rest only the 4 DIP canvas gap, a thin neutral line. The
        // handle, 8 x 36 #2F2F2F fully rounded, appears while hovered, dragged or keyboard-focused
        // and overlaps both page edges by 2 DIP. Pages are child windows that cover the canvas,
        // so it is normally drawn by the grip window above them; this path is the fallback
        // without grips (no layered children), where only the gap part shows.
        if(split.paintHandles)for(int i=0;i<(int)split.handles.size();++i){if(i!=split.hotHandle&&i!=split.focusedHandle)continue;auto h=split.handles[i];
            const bool row=h.bottom-h.top>h.right-h.left;const float cx=(h.left+h.right)/2,cy=(h.top+h.bottom)/2;
            D2D1_RECT_F pill=row?D2D1_RECT_F{cx-4,cy-18,cx+4,cy+18}:D2D1_RECT_F{cx-18,cy-4,cx+18,cy+4};
            Color(47/255.f);target->FillRoundedRectangle(D2D1::RoundedRect(pill,4,4),brush);
            if(i==split.focusedHandle){Color(1,Design::Focus::keyboardOpacity);target->DrawRoundedRectangle(D2D1::RoundedRect({pill.left-2,pill.top-2,pill.right+2,pill.bottom+2},6,6),brush,2);}}
    }
    // Rectangle with a radius per corner (top-left, top-right, bottom-right, bottom-left).
    ID2D1PathGeometry* CornerBox(D2D1_RECT_F r,float tl,float tr,float br,float bl){
        ID2D1PathGeometry* g=nullptr;if(!factory||FAILED(factory->CreatePathGeometry(&g)))return nullptr;ID2D1GeometrySink* s=nullptr;if(FAILED(g->Open(&s))){g->Release();return nullptr;}
        auto arc=[&](D2D1_POINT_2F to,float radius){if(radius>0)s->AddArc(D2D1::ArcSegment(to,{radius,radius},0,D2D1_SWEEP_DIRECTION_CLOCKWISE,D2D1_ARC_SIZE_SMALL));else s->AddLine(to);};
        s->BeginFigure({r.left+tl,r.top},D2D1_FIGURE_BEGIN_HOLLOW);s->AddLine({r.right-tr,r.top});arc({r.right,r.top+tr},tr);s->AddLine({r.right,r.bottom-br});arc({r.right-br,r.bottom},br);
        s->AddLine({r.left+bl,r.bottom});arc({r.left,r.bottom-bl},bl);s->AddLine({r.left,r.top+tl});arc({r.left+tl,r.top},tl);s->EndFigure(D2D1_FIGURE_END_CLOSED);s->Close();s->Release();return g;
    }
    // Tab outline with different left/right corner radii (split group members).
    ID2D1PathGeometry* TabShape(D2D1_RECT_F r,float rl,float rr){
        ID2D1PathGeometry* g=nullptr;if(!factory||FAILED(factory->CreatePathGeometry(&g)))return nullptr;ID2D1GeometrySink* s=nullptr;if(FAILED(g->Open(&s))){g->Release();return nullptr;}
        auto arc=[&](D2D1_POINT_2F to,float radius){if(radius>0)s->AddArc(D2D1::ArcSegment(to,{radius,radius},0,D2D1_SWEEP_DIRECTION_CLOCKWISE,D2D1_ARC_SIZE_SMALL));else s->AddLine(to);};
        s->BeginFigure({r.left+rl,r.top},D2D1_FIGURE_BEGIN_FILLED);s->AddLine({r.right-rr,r.top});arc({r.right,r.top+rr},rr);s->AddLine({r.right,r.bottom-rr});arc({r.right-rr,r.bottom},rr);
        s->AddLine({r.left+rl,r.bottom});arc({r.left,r.bottom-rl},rl);s->AddLine({r.left,r.top+rl});arc({r.left+rl,r.top},rl);s->EndFigure(D2D1_FIGURE_END_CLOSED);s->Close();s->Release();return g;
    }
    void FillShape(D2D1_RECT_F r,float rl,float rr,float c){if(auto g=TabShape(r,rl,rr)){Color(c);target->FillGeometry(g,brush);g->Release();}}
    void StrokeShape(D2D1_RECT_F r,float rl,float rr,float width,float alpha){if(auto g=TabShape(r,rl,rr)){Color(1,alpha);target->DrawGeometry(g,brush,width);g->Release();}}
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
    // ---- Animated downloads icon (download-icon.h) ----
    // The host feeds the model; Paint draws its pose for the 'D' button.
    DownloadIcon::Model downloadIcon;
    // Back/Forward travel and the Reload <-> Stop morph (nav-motion.h); fed by the host.
    NavMotion::State nav;
    Lottie::Asset dlTrack,dlFill,dlArrow,dlCut,dlSuccess,dlSuccessCut;bool dlSuccessReady=false;int dlSuccessFrame=-2;
    ID2D1Bitmap* dlBitmap=nullptr;unsigned dlSide=0;int dlRing=-1,dlArrowFrame=-1;bool dlFailed=false;std::vector<uint32_t> dlPixels;Design::Rgb dlTint{-1,-1,-1};
    void DropDownloadIcon(){Drop(dlBitmap);dlRing=dlArrowFrame=-1;}
    // One Asset serves one pixel size (the player cannot re-target): reload on a DPI change.
    bool LoadDownloadIcon(unsigned side){
        if(dlSide==side)return !dlFailed;
        DropDownloadIcon();dlSide=side;
        dlFailed=!(dlTrack.Load(assets+L"lottie\\download-ring-track.json")&&dlFill.Load(assets+L"lottie\\download-ring-fill.json")&&dlArrow.Load(assets+L"lottie\\download-arrow.json")&&dlCut.Load(assets+L"lottie\\download-arrow-cutout.json"))
            ||dlTrack.FrameCount()!=DownloadIcon::RingFrames||dlFill.FrameCount()!=DownloadIcon::RingFrames||dlArrow.FrameCount()!=DownloadIcon::ArrowFrames||dlCut.FrameCount()!=DownloadIcon::ArrowFrames;
        // Success morph (arrow -> check-circle check + its own cut-out); optional: without it the arrow stays at rest.
        dlSuccessReady=dlSuccess.Load(assets+L"lottie\\download-success.json")&&dlSuccessCut.Load(assets+L"lottie\\download-success-cutout.json")
            &&dlSuccess.FrameCount()==DownloadIcon::SuccessFrames&&dlSuccessCut.FrameCount()==DownloadIcon::SuccessFrames;
        return !dlFailed;
    }
    bool DrawDownloadIcon(DownloadIcon::Pose pose,D2D1_RECT_F box,float size,float opacity){
        const unsigned side=std::clamp(static_cast<unsigned>(std::ceil(size*dpi/96)),1u,64u);
        if(!LoadDownloadIcon(side))return false;
        // The ring's fill follows the window's accent ink, also when it changes mid-download.
        const auto ink=Design::Accent::Ink();const bool retint=ink.r!=dlTint.r||ink.g!=dlTint.g||ink.b!=dlTint.b;
        const int success=dlSuccessReady?pose.success:-1;
        if(!dlBitmap||pose.ring!=dlRing||pose.arrow!=dlArrowFrame||success!=dlSuccessFrame||retint){
            auto at=[](Lottie::Asset& a,int frame,int frames,unsigned px){return a.Sample(double(frame)/(frames-1),px);};
            auto t=at(dlTrack,pose.ring,DownloadIcon::RingFrames,side),f=at(dlFill,pose.ring,DownloadIcon::RingFrames,side);
            // The arrow layer and its matching cut-out: the success morph while it runs, else the arrow timeline.
            auto a=success>=0?at(dlSuccess,success,DownloadIcon::SuccessFrames,side):at(dlArrow,pose.arrow,DownloadIcon::ArrowFrames,side);
            auto c=success>=0?at(dlSuccessCut,success,DownloadIcon::SuccessFrames,side):at(dlCut,pose.arrow,DownloadIcon::ArrowFrames,side);
            if(!t||!f||!a||!c)return false;
            const float tint[3]={ink.r,ink.g,ink.b};
            // The success check is drawn in the profile ink too, blended in over the first 6 frames of its morph (and out on the way back).
            const float arrowMix=success>=0?std::min(1.f,success/6.f):0.f;
            DownloadIcon::Compose(t->bgra.data(),f->bgra.data(),c->bgra.data(),a->bgra.data(),side*side,dlPixels,tint,arrowMix);dlTint=ink;
            HRESULT hr=S_OK;
            if(!dlBitmap)hr=target->CreateBitmap(D2D1::SizeU(side,side),dlPixels.data(),side*4,D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED)),&dlBitmap);
            else hr=dlBitmap->CopyFromMemory(nullptr,dlPixels.data(),side*4);
            if(FAILED(hr)||!dlBitmap){DropDownloadIcon();return false;}
            dlRing=pose.ring;dlArrowFrame=pose.arrow;dlSuccessFrame=success;
        }
        const float x=(box.left+box.right-size)/2,y=(box.top+box.bottom-size)/2;
        target->DrawBitmap(dlBitmap,{x,y,x+size,y+size},opacity);return true;
    }
    // ---- Audio button (audio-mixer.h Audio::IconModel, design/lottie/audio-meter.py) ----
    Lottie::Asset audioMeter;ID2D1Bitmap* audioBitmap=nullptr;unsigned audioSide=0;int audioFrame=-1;bool audioFailed=false;
    void DropAudioIcon(){Drop(audioBitmap);audioFrame=-1;}
    // `color` tints the white Lottie (premultiplied: each pixel's alpha x the colour), so the
    // music meter can be the accent (Paper TO2-0 / UCD-0) without a second asset.
    std::vector<uint32_t> audioPixels;uint32_t audioTint=0;
    bool DrawAudioFrame(int frame,D2D1_RECT_F box,float size,D2D1_COLOR_F color){
        const unsigned side=std::clamp(static_cast<unsigned>(std::ceil(size*dpi/96)),1u,64u);
        if(audioSide!=side){DropAudioIcon();audioSide=side;audioFailed=!audioMeter.Load(assets+L"lottie\\audio-meter.json")||audioMeter.FrameCount()!=Audio::Icon::Frames;}
        if(audioFailed)return false;
        const uint32_t tint=(uint32_t(std::lround(color.r*255))<<16)|(uint32_t(std::lround(color.g*255))<<8)|uint32_t(std::lround(color.b*255));
        if(!audioBitmap||frame!=audioFrame||tint!=audioTint){
            const auto* f=audioMeter.Sample(double(frame)/(Audio::Icon::Frames-1),side);if(!f)return false;
            audioPixels.resize(f->bgra.size());
            for(size_t i=0;i<f->bgra.size();++i){const uint32_t a=f->bgra[i]>>24;audioPixels[i]=(a<<24)|((a*((tint>>16)&255)/255)<<16)|((a*((tint>>8)&255)/255)<<8)|(a*(tint&255)/255);}
            HRESULT hr=S_OK;
            if(!audioBitmap)hr=target->CreateBitmap(D2D1::SizeU(side,side),audioPixels.data(),side*4,D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED)),&audioBitmap);
            else hr=audioBitmap->CopyFromMemory(nullptr,audioPixels.data(),side*4);
            if(FAILED(hr)||!audioBitmap){DropAudioIcon();return false;}
            audioFrame=frame;audioTint=tint;
        }
        const float x=(box.left+box.right-size)/2,y=(box.top+box.bottom-size)/2;
        target->DrawBitmap(audioBitmap,{x,y,x+size,y+size},color.a);return true;
    }
    // Ghost button like History/Downloads. Quiet (short sounds, idle): white at 50 % x the
    // pose's subdued emphasis (38 %). Music (Paper TO2-0 / UCD-0): the meter turns the accent,
    // and hover / an open panel lays the accent at 16 % (--color-browser-split-tint) under it.
    // The two blend with the emphasis, so the intro/outro also carry the colour change.
    void DrawAudioButton(D2D1_RECT_F box,Design::ButtonState state,Motion::Time now){
        const auto pose=audioIcon.At(now);
        const float music=Motion::Clamp((pose.emphasis-Audio::Icon::Subdued)/(1-Audio::Icon::Subdued));
        auto style=Design::Resolve(Design::ButtonVariant::Ghost,state);
        const auto accent=Design::Accent::Get();
        if(music>0&&(state.hover>0||state.press>0)){
            if(style.fill)Round(box,style.radius,style.surface*(1-music)+Design::Surface::canvas*music);
            FillBox(box,style.radius,D2D1::ColorF(accent.r,accent.g,accent.b,(.16f*state.hover+.08f*state.press)*music));
        }else if(style.fill)Round(box,style.radius,style.surface);
        if(style.focus)FocusRing(box,style.radius);
        const float size=style.iconSize*Motion::PressScale(state.press,motion&&motion->ReducedMotion(),!state.disabled);
        const float white=.5f*style.iconOpacity*pose.emphasis;
        const D2D1_COLOR_F ink=D2D1::ColorF(1+(accent.r-1)*music,1+(accent.g-1)*music,1+(accent.b-1)*music,(white+(1-white)*music)*pose.presence);
        if(pose.frame>=0&&DrawAudioFrame(pose.frame,box,size,ink))return;
        static constexpr char high[]="M165.27,21.22a12,12,0,0,0-12.64,1.31L83.88,76H40A20,20,0,0,0,20,96v64a20,20,0,0,0,20,20H83.88l68.75,53.47A12,12,0,0,0,172,224V32A12,12,0,0,0,165.27,21.22ZM148,199.47,95.37,158.53A12,12,0,0,0,88,156H44V100H88a12,12,0,0,0,7.37-2.53L148,56.54ZM212,104v48a12,12,0,0,1-24,0V104a12,12,0,0,1,24,0Zm36-16v80a12,12,0,0,1-24,0V88a12,12,0,0,1,24,0Z";
        static constexpr char low[]="M165.27,21.22a12,12,0,0,0-12.64,1.31L83.88,76H40A20,20,0,0,0,20,96v64a20,20,0,0,0,20,20H83.88l68.75,53.47A12,12,0,0,0,172,224V32A12,12,0,0,0,165.27,21.22ZM148,199.46,95.37,158.53A12,12,0,0,0,88,156H44V100H88a12,12,0,0,0,7.37-2.53L148,56.54ZM212,104v48a12,12,0,0,1-24,0V104a12,12,0,0,1,24,0Z";
        // A failed frame of the loop still shows music (high), the outro its end (low).
        const bool high_=pose.frame>=0?pose.frame<Audio::Icon::Loop+Audio::Icon::Out/2:pose.high;
        PathGlyph(high_?high:low,box,size,ink);
    }
    // Media artwork: `image` cropped to fill `box` (cover), corners rounded, with a 1 DIP
    // white 10 % inner outline so dark art keeps its edge on the dark canvas.
    bool DrawCover(const std::shared_ptr<const FaviconImage>& image,D2D1_RECT_F box,float radius){
        if(!image||!image->width||!image->height||!target||!factory)return false;
        auto& bitmap=faviconBitmaps[image];if(!bitmap){auto properties=D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));target->CreateBitmap(D2D1::SizeU(image->width,image->height),image->pixels.data(),image->stride,properties,&bitmap);}if(!bitmap)return false;
        const float bw=box.right-box.left,bh=box.bottom-box.top,iw=float(image->width),ih=float(image->height);
        const float k=std::max(bw/iw,bh/ih),sw=bw/k,sh=bh/k;const D2D1_RECT_F src{(iw-sw)/2,(ih-sh)/2,(iw+sw)/2,(ih+sh)/2};
        ID2D1RoundedRectangleGeometry* clip=nullptr;if(FAILED(factory->CreateRoundedRectangleGeometry(D2D1::RoundedRect(box,radius,radius),&clip)))return false;
        ID2D1Layer* layer=nullptr;if(FAILED(target->CreateLayer(nullptr,&layer))){Drop(clip);return false;}
        target->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(),clip,D2D1_ANTIALIAS_MODE_PER_PRIMITIVE),layer);
        target->DrawBitmap(bitmap,box,1,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,src);
        target->PopLayer();Drop(layer);Drop(clip);
        Color(1,.1f);target->DrawRoundedRectangle(D2D1::RoundedRect({box.left+.5f,box.top+.5f,box.right-.5f,box.bottom-.5f},radius-.5f,radius-.5f),brush,1);
        return true;
    }
    // Blue ring stroke that grows, blurs and fades. The blur is a real Direct2D
    // Gaussian blur of a small offscreen layer (allocated once, at the ripple's
    // reach) and grows with the ring like the preview's; without the effect,
    // two softer, wider passes stand in.
    ID2D1DeviceContext* rippleDc=nullptr;ID2D1Effect* rippleBlur=nullptr;ID2D1BitmapRenderTarget* rippleLayer=nullptr;float rippleExtent=0;bool rippleNoEffect=false;
    void DropRipple(){Drop(rippleLayer);Drop(rippleBlur);Drop(rippleDc);rippleExtent=0;rippleNoEffect=false;}
    void DrawDownloadRipple(float t,D2D1_RECT_F box,float size){
        if(t<0||t>=1)return;
        namespace R=DownloadIcon::Ripple;
        const float p=Motion::Eased(t,Motion::Curve::Icon),unit=size/256.f,scale=1+(R::grow-1)*p;
        const float radius=R::radius*unit*scale,width=R::stroke*unit*scale,blur=R::blur*unit*scale*p,alpha=R::opacity*(1-p);
        const D2D1_POINT_2F centre{(box.left+box.right)/2,(box.top+box.bottom)/2};
        const auto ink=Design::Accent::Ink();   // the ripple is the ring's colour: the window's accent ink
        brush->SetColor(D2D1::ColorF(ink.r,ink.g,ink.b,alpha));
        if(!rippleNoEffect&&!rippleDc){
            static const CLSID gaussian={0x1feb6d69,0x2fe6,0x4ac9,{0x8c,0x58,0x1d,0x7f,0x93,0xe7,0xa6,0xa5}};
            if(FAILED(target->QueryInterface(__uuidof(ID2D1DeviceContext),reinterpret_cast<void**>(&rippleDc)))||FAILED(rippleDc->CreateEffect(gaussian,&rippleBlur))){Drop(rippleBlur);Drop(rippleDc);rippleNoEffect=true;}
        }
        if(rippleDc&&rippleBlur&&blur>.05f){
            // Layer side: the furthest reach (ring + stroke + 3 sigma of the largest blur).
            const float extent=2*(R::radius*R::grow+R::stroke*R::grow/2+3*R::blur*R::grow)*unit+2;
            if(!rippleLayer||rippleExtent!=extent){Drop(rippleLayer);if(FAILED(target->CreateCompatibleRenderTarget(D2D1::SizeF(extent,extent),&rippleLayer))){rippleLayer=nullptr;rippleNoEffect=true;DropRipple();rippleNoEffect=true;}else rippleExtent=extent;}
            if(rippleLayer){
                rippleLayer->BeginDraw();rippleLayer->Clear(D2D1::ColorF(0,0,0,0));
                rippleLayer->DrawEllipse(D2D1::Ellipse({extent/2,extent/2},radius,radius),brush,width);
                ID2D1Bitmap* bitmap=nullptr;
                if(SUCCEEDED(rippleLayer->EndDraw())&&SUCCEEDED(rippleLayer->GetBitmap(&bitmap))&&bitmap){
                    rippleBlur->SetInput(0,bitmap);
                    const float deviation=blur;const UINT32 soft=0; // D2D1_BORDER_MODE_SOFT
                    rippleBlur->SetValue(0,D2D1_PROPERTY_TYPE_FLOAT,reinterpret_cast<const BYTE*>(&deviation),sizeof(deviation));
                    rippleBlur->SetValue(2,D2D1_PROPERTY_TYPE_ENUM,reinterpret_cast<const BYTE*>(&soft),sizeof(soft));
                    ID2D1Image* output=nullptr;rippleBlur->GetOutput(&output);
                    if(output){const D2D1_POINT_2F at{centre.x-extent/2,centre.y-extent/2};rippleDc->DrawImage(output,&at,nullptr,D2D1_INTERPOLATION_MODE_LINEAR,D2D1_COMPOSITE_MODE_SOURCE_OVER);output->Release();}
                    rippleBlur->SetInput(0,nullptr);bitmap->Release();return;
                }
                if(bitmap)bitmap->Release();
            }
        }
        const D2D1_ELLIPSE e=D2D1::Ellipse(centre,radius,radius);
        auto pass=[&](float w,float a){brush->SetColor(D2D1::ColorF(ink.r,ink.g,ink.b,a));target->DrawEllipse(e,brush,w);};
        if(blur>.05f){pass(width+blur*2,alpha*.25f);pass(width+blur,alpha*.35f);}
        pass(width,alpha*(blur>.05f?.6f:1.f));
    }
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
    IDWriteFactory* WriteFactory()const{return write;}
    IDWriteFontCollection* FontCollection()const{return fonts;}
    IWICImagingFactory* Imaging()const{return wic;}
    ID2D1RenderTarget* SwapTarget(ID2D1RenderTarget* next){auto old=target;target=next;return old;}
    void RecreateTarget(){DropGhostLayer();DropFade();ClearFaviconBitmaps();Drop(lottieBitmap);DropDownloadIcon();DropAudioIcon();DropRipple();lottieSide=0;for(auto& b:bitmaps)Drop(b.second);bitmaps.clear();Drop(brush);Drop(target);hwndTarget=nullptr;Resize();}
    float TextWidth(const std::wstring& value,float size,bool medium=false,bool tabular=false,float tracking=0){if(value.empty())return 0;IDWriteTextLayout* layout=nullptr;TextMeasure m;if(FAILED(CachedTextLayout(value,{0,0,4096,32},size,tabular,medium,tracking,&layout,m)))return 0;DWRITE_TEXT_METRICS metrics{};layout->GetMetrics(&metrics);Drop(layout);return metrics.widthIncludingTrailingWhitespace;}
    // Toolbar button drawn in its hover state while the popover it opened is shown.
    int latched=0;
    void SetMotion(Motion::State* state){motion=state;}
    bool animateSelection=false;
    std::set<int> disabled;
    bool Enabled(int id)const{return disabled.count(id)==0;}
    void SetEnabled(int id,bool enabled){if(enabled)disabled.erase(id);else disabled.insert(id);}
    int dragIndex=-1;float dragOffset=0;
    // ---- Tab open/close motion (presentation only) -------------------------------
    // The host sets `tabKeys` (stable, never-reused tab serials, in order) before
    // Build/Paint. Without matching keys (fixtures) the strip is drawn statically.
    // Hit regions follow the presented geometry of the last Build, so a click lands
    // on what is on screen; selection, focus and the page change synchronously.
    Motion::TabStripMotion tabMotion;
    std::vector<unsigned> tabKeys;
    bool animateTabLayout=false; // host: animate the next width/scroll reflow (e.g. width unlock)
    bool tabMotionStarted=false; // set when a Build started motion; the host schedules frames
    Motion::TabLayoutFrame tabFrame;
    struct TabSnapshot{std::wstring title;std::shared_ptr<const FaviconImage> favicon;float fill=Design::Surface::canvas;bool selected=false;};
    std::map<unsigned,TabSnapshot> tabSnapshots; // what a closing tab looked like, drawn while it blurs out
    bool TabKeysValid(int count)const{return motion&&(int)tabKeys.size()==count;}
    unsigned TabKey(int n)const{return n>=0&&n<(int)tabKeys.size()?tabKeys[n]:(unsigned)(n+1);}
    bool TakeTabMotionStarted(){const bool started=tabMotionStarted;tabMotionStarted=false;return started;}
    Design::Box ShownSurface(const Motion::TabItem& it)const{const float x=Design::TabStrip::Left()+it.slot-tabFrame.scroll+Design::Space::tabGap*it.presence;return {x,Design::TabStrip::top,x+tabFrame.tab*it.presence,Design::TabStrip::bottom};}
    // Content keeps the settled tab width (stable text layouts, no squashing) and is clipped to the surface.
    Design::Box ShownContent(const Motion::TabItem& it)const{auto s=ShownSurface(it);s.right=s.left+strip.tab;return s;}
    Design::Box ShownRegion(const Motion::TabItem& it)const{const float x=Design::TabStrip::Left()+it.slot-tabFrame.scroll;return {x,Design::TabStrip::regionTop,x+it.presence*(tabFrame.tab+Design::Space::tabGap),Design::TabStrip::regionBottom};}
    float ShownViewRight()const{return Design::TabStrip::Left()+tabFrame.view;}
    // Presented surface of live tab n (split separators, menu outline, drag grab), falling back to the layout.
    Design::Box ShownTab(int n)const{for(auto& it:tabFrame.items)if(it.index==n)return ShownSurface(it);return strip.Surface(n);}
    float ShownTabLeft(int n)const{return ShownTab(n).left;}
    void SyncTabMotion(int count,Motion::Time now){
        if(!TabKeysValid(count)){
            tabFrame={};tabFrame.tab=strip.tab;tabFrame.scroll=strip.scroll;tabFrame.view=strip.view;tabFrame.content=strip.Content();
            for(int i=0;i<count;i++){Motion::TabItem it;it.key=TabKey(i);it.index=i;it.slot=strip.Pitch()*i;tabFrame.items.push_back(it);}
            animateTabLayout=false;return;
        }
        if(tabMotion.ReducedMotion()!=motion->ReducedMotion())tabMotion.SetReducedMotion(motion->ReducedMotion());
        if(tabMotion.Sync(tabKeys,{strip.tab,strip.scroll,strip.view,strip.Overflow()},Design::Space::tabGap,now,animateTabLayout))tabMotionStarted=true;
        animateTabLayout=false;tabFrame=tabMotion.Sample(now);
    }
    // ---- Closing tab blur-out --------------------------------------------------------
    // The ghost is drawn at full width in its old place into a transparent layer, then
    // composited with a Gaussian blur (0 -> TabExitBlur DIP) and opacity (1 -> 0).
    // Direct2D 1.1 (ID2D1DeviceContext) is required for the blur; without it the
    // layer only fades. Resources are dropped with the render target.
    ID2D1DeviceContext* blurContext=nullptr;ID2D1Effect* blurEffect=nullptr;bool blurProbed=false;
    ID2D1BitmapRenderTarget* ghostLayer=nullptr;D2D1_SIZE_F ghostLayerSize{};
    unsigned long long ghostBlurDraws=0,ghostFadeDraws=0; // fixture evidence of the path taken
    void DropGhostLayer(){Drop(ghostLayer);ghostLayerSize={};Drop(blurEffect);Drop(blurContext);blurProbed=false;}
    D2D1_RECT_F GhostRect(const Motion::TabItem& it)const{const float x=Design::TabStrip::Left()+it.slot-tabFrame.scroll+Design::Space::tabGap;return {x,Design::TabStrip::top,x+tabFrame.tab,Design::TabStrip::bottom};}
    bool BlurReady(){
        if(!blurProbed){blurProbed=true;
            // CLSID_D2D1GaussianBlur, defined here so no dxguid import library is needed.
            static const GUID GaussianBlur={0x1feb6d69,0x2fe6,0x4ac9,{0x8c,0x58,0x1d,0x7f,0x93,0xe7,0xa6,0xa5}};
            if(SUCCEEDED(target->QueryInterface(__uuidof(ID2D1DeviceContext),(void**)&blurContext))&&blurContext){
                if(FAILED(blurContext->CreateEffect(GaussianBlur,&blurEffect)))Drop(blurEffect);}
            if(!blurEffect)Drop(blurContext);}
        return blurContext&&blurEffect;
    }
    void DrawGhost(const Motion::TabItem& item){
        auto it=tabSnapshots.find(item.key);if(it==tabSnapshots.end())return;
        const float alpha=1-std::clamp(item.exit,0.f,1.f);if(alpha<=.002f)return;
        const auto& snap=it->second;const D2D1_RECT_F rect=GhostRect(item);
        const float pad=3*Motion::TabExitBlur,w=rect.right-rect.left+2*pad,h=rect.bottom-rect.top+2*pad;
        if(!ghostLayer||ghostLayerSize.width!=w||ghostLayerSize.height!=h){Drop(ghostLayer);ghostLayerSize={};
            if(FAILED(target->CreateCompatibleRenderTarget(D2D1::SizeF(w,h),&ghostLayer))||!ghostLayer){Drop(ghostLayer);return;}
            ghostLayerSize={w,h};}
        // Paint the tab as it last looked, with the main helpers redirected to the layer.
        auto* main=SwapTarget(ghostLayer);ghostLayer->BeginDraw();ghostLayer->Clear(D2D1::ColorF(0,0,0,0));
        const D2D1_RECT_F tab{pad,pad,pad+rect.right-rect.left,pad+rect.bottom-rect.top};const Design::Box box{tab.left,tab.top,tab.right,tab.bottom};
        if(snap.fill!=Design::Surface::canvas){Color(snap.fill);target->FillRoundedRectangle(D2D1::RoundedRect(tab,Design::Radius::control,Design::Radius::control),brush);}
        auto favicon=strip.FaviconIn(box);auto title=strip.TitleIn(box);const D2D1_RECT_F slot{favicon.left,favicon.top,favicon.right,favicon.bottom};
        if(!DrawSiteImage(snap.favicon,slot))Icon(L"favicon",slot,16);
        if(strip.Titled()&&title.right-title.left>=Design::Size::icon)Text(snap.title,{title.left,title.top,title.right,title.bottom},Design::Type::label.size,snap.selected?Design::Text::secondary:Design::Text::muted,false,Design::Type::label.medium,Design::Type::label.tracking);
        HRESULT hr=ghostLayer->EndDraw();SwapTarget(main);if(FAILED(hr))return;
        ID2D1Bitmap* bitmap=nullptr;if(FAILED(ghostLayer->GetBitmap(&bitmap))||!bitmap)return;
        // Cut at the strip's left edge and its available room, not at the shrinking
        // presented view: a closing last tab must blur out whole, not be sliced.
        const float right=std::max(ShownViewRight(),std::min(rect.right+pad,Design::TabStrip::Left()+strip.room));
        target->PushAxisAlignedClip(D2D1::RectF(strip.ViewLeft(),Design::TabStrip::regionTop,right,Design::TabStrip::regionBottom),D2D1_ANTIALIAS_MODE_ALIASED);
        const D2D1_POINT_2F origin{rect.left-pad,rect.top-pad};
        if(BlurReady()){
            const float sigma=Motion::TabExitBlur*std::clamp(item.exit,0.f,1.f);
            blurEffect->SetInput(0,bitmap);blurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION,D2D1_PROPERTY_TYPE_FLOAT,(const BYTE*)&sigma,sizeof(sigma));
            blurContext->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(),nullptr,D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,D2D1::IdentityMatrix(),alpha),nullptr);
            blurContext->DrawImage(blurEffect,origin);blurContext->PopLayer();blurEffect->SetInput(0,nullptr);++ghostBlurDraws;
        }else{target->DrawBitmap(bitmap,D2D1::RectF(origin.x,origin.y,origin.x+w,origin.y+h),alpha);++ghostFadeDraws;}
        target->PopAxisAlignedClip();Drop(bitmap);
    }
    // Split view (src/split-view.h). `tabGroups` holds each tab's group (0 = none):
    // adjacent members are drawn joined. A tab lifted into the drag overlay leaves
    // an outlined slot. `split` is painted on the canvas around the page windows.
    std::vector<int> tabGroups;int liftedIndex=-1,visibleTabGroup=0;
    std::vector<int> menuTargets;  // tabs a highlighted split-menu row designates (Paper V2: 1 DIP white 50 % outline)
    struct SplitDecor{bool shown=false,ring=true;D2D1_RECT_F active{};std::vector<D2D1_RECT_F> rails;std::vector<int> railTabs;std::vector<D2D1_RECT_F> handles;int hotHandle=-1,focusedHandle=-1;bool paintHandles=true,activeBottomLeft=false,activeBottomRight=false;} split;
    UINT dpi=96;float width=1180,height=815;D2D1_RECT_F bar{},edit{};std::vector<Region> regions;
    // Fullscreen (F11 or a page's element): the page takes the whole window. Nothing of the chrome is drawn, hit,
    // focused, given a tooltip or exposed to UI Automation (all of these read `regions`); Paint only clears the canvas.
    bool contentOnly=false;
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
    void Dispose(){if(hwnd&&!followDpiGone&&IsWindow(hwnd))RemoveWindowSubclass(hwnd,FollowDpiProc,reinterpret_cast<UINT_PTR>(this));followDpiGone=true;ClearTextCache();if(eyeSlash){eyeSlash->Release();eyeSlash=nullptr;}DropGhostLayer();DropFade();Drop(lottieBitmap);DropDownloadIcon();DropAudioIcon();audioMeter.Clear();audioSide=0;DropRipple();dlTrack.Clear();dlFill.Clear();dlArrow.Clear();dlCut.Clear();dlSide=0;lottieSide=0;lottie.Clear();copyLottie.Clear();refreshLottie.Clear();ClearFaviconBitmaps();favicons.clear();for(auto& b:bitmaps)Drop(b.second);bitmaps.clear();Drop(brush);Drop(target);hwndTarget=nullptr;Drop(surface);Drop(wic);Drop(fonts);if(write&&fontLoader)write->UnregisterFontCollectionLoader(fontLoader);Drop(fontLoader);Drop(write);DropPathGlyphs();Drop(factory);}
    HRESULT InitResources(const std::wstring& root){assets=root+L"\\assets\\";InitLottie();HRESULT hr=D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,&factory);if(FAILED(hr))return hr;
        hr=DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),(IUnknown**)&write);if(FAILED(hr))return hr;
        fontLoader=new FontLoader(assets+L"fonts\\");hr=write->RegisterFontCollectionLoader(fontLoader);if(FAILED(hr))return hr;const UINT key=1;hr=write->CreateCustomFontCollection(fontLoader,&key,sizeof(key),&fonts);if(FAILED(hr))return hr;for(auto family:{L"Inter Display"}){UINT32 index;BOOL exists=FALSE;hr=fonts->FindFamilyName(family,&index,&exists);if(FAILED(hr)||!exists)return E_FAIL;}
        hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic));return hr;
    }
    HRESULT Init(HWND h,const std::wstring& root){hwnd=h;followDpiGone=false;if(h)SetWindowSubclass(h,FollowDpiProc,reinterpret_cast<UINT_PTR>(this),reinterpret_cast<DWORD_PTR>(this));HRESULT hr=InitResources(root);return FAILED(hr)?hr:Resize();}
    // The window moved to a monitor with another DPI (or its scale changed) while the render
    // target kept the old one: everything inside was drawn too large or too small (bug
    // 2026-11-17, settings, history, menus... on a 100 % monitor beside a 125 % one). A move
    // sizes the window first and changes its DPI afterwards, and a surface that keeps the
    // same pixel size gets no WM_SIZE, so every window drawn by a Chrome re-checks its DPI
    // itself: after a DPI change or a move, and before painting. Returns true when it changed.
    bool FollowDpi(){if(!hwnd||!hwndTarget)return false;const UINT now=GetDpiForWindow(hwnd);if(!now||now==dpi)return false;Resize();InvalidateRect(hwnd,nullptr,FALSE);return true;}
    static LRESULT CALLBACK FollowDpiProc(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data){
        auto* self=reinterpret_cast<Chrome*>(data);
        if(m==WM_NCDESTROY){RemoveWindowSubclass(h,FollowDpiProc,id);if(self->hwnd==h)self->followDpiGone=true;return DefSubclassProc(h,m,w,l);}
        if(m==WM_PAINT)self->FollowDpi();
        const LRESULT r=DefSubclassProc(h,m,w,l);
        if(m==WM_DPICHANGED||m==WM_WINDOWPOSCHANGED)self->FollowDpi();
        return r;
    }
    bool followDpiGone=false;
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
    // Back/Forward pair (Paper): one segment, 6 DIP outer / 3 DIP inner corners, the 1 x 10
    // hairline between the halves drawn with Back. Same Raised states as before (hover,
    // press, disabled .32 glyph); the glyph rides the travel spring, the surface never moves.
    void DrawNavButton(int id,const wchar_t* icon,D2D1_RECT_F box,Design::ButtonState state,Motion::Time now){
        namespace T=Design::Toolbar;const bool back=id=='B';
        auto style=Design::Resolve(Design::ButtonVariant::Raised,state);
        const float rl=back?Design::Radius::control:T::innerRadius,rr=back?T::innerRadius:Design::Radius::control;
        FillShape(box,rl,rr,style.surface);
        if(style.focus)StrokeShape({box.left+.5f,box.top+.5f,box.right-.5f,box.bottom-.5f},rl-.5f,rr-.5f,Design::Size::focusStroke,Design::Focus::Opacity(keyboardModality));
        if(back){const float x=T::separatorLeft,y=(box.top+box.bottom)/2;Color(1,T::separatorOpacity);target->FillRoundedRectangle(D2D1::RoundedRect({x,y-T::separatorHeight/2,x+T::separatorWidth,y+T::separatorHeight/2},.5f,.5f),brush);}
        const bool reduced=motion&&motion->ReducedMotion();
        const float dx=reduced?0.f:(back?nav.back:nav.forward).Position(now);
        Icon(icon,{box.left+dx,box.top,box.right+dx,box.bottom},style.iconSize*Motion::PressScale(state.press,reduced,!state.disabled),style.iconOpacity);
    }
    // Reload <-> Stop (nav-motion.h): both glyphs turn counter-clockwise about the slot
    // centre while they cross-scale and cross-fade. At rest this is exactly one static icon.
    void DrawStopMorph(D2D1_RECT_F box,float size,float opacity,Motion::Time now){
        const auto layers=nav.stop.Layers(now);
        D2D1_MATRIX_3X2_F base;target->GetTransform(&base);const D2D1_POINT_2F c{(box.left+box.right)/2,(box.top+box.bottom)/2};
        auto layer=[&](const wchar_t* name,Motion::IconLayer l,float angle){if(l.opacity<=.001f)return;
            if(angle!=0)target->SetTransform(D2D1::Matrix3x2F::Rotation(angle,c)*base);
            Icon(name,box,size*l.scale,opacity*l.opacity);target->SetTransform(base);};
        layer(L"reload",layers.reload,layers.reloadAngle);layer(L"tab-close",layers.stop,layers.stopAngle);
    }
    void DrawButton(const wchar_t* icon,D2D1_RECT_F box,Design::ButtonVariant variant,Design::ButtonState state){
        auto style=Design::Resolve(variant,state);
        if(style.fill){Round(box,style.radius,style.surface);if(variant==Design::ButtonVariant::Embedded)DrawLoading(box);}
        if(style.focus)FocusRing(box,style.radius);
        Icon(icon,box,style.iconSize*Motion::PressScale(state.press,motion&&motion->ReducedMotion(),!state.disabled),style.iconOpacity);
    }
    HRESULT PaintStateSheet(){if(!target)return E_UNEXPECTED;target->BeginDraw();target->Clear(D2D1::ColorF(Design::Surface::canvas,Design::Surface::canvas,Design::Surface::canvas));
        Text(L"Native components",   /* i18n-exempt: developer specimen page */{24,16,700,48},20,1,false,true);const bool modality=keyboardModality;
        // i18n-exempt-next: developer specimen page (state names)
        const wchar_t* labels[]={L"Default",L"Hover",L"Pressed",L"Disabled",L"Keyboard focus",L"Pointer focus"};
        // i18n-exempt-next: developer specimen page (variant names)
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
        Text(L"Tab surfaces / stable close slot",   /* i18n-exempt: developer specimen page */{24,384,400,416},14,.7f);
        for(int col=0;col<3;++col){float x=240.f+col*190;D2D1_RECT_F tab{x,432,x+128,464};Round(tab,Design::Radius::control,Design::TabSurface(col==0,col==1?1:0));DrawFavicon(0,{x+8,440,x+24,456});Text(L"Ggyp \u00c9\u00e7",{x+32,432,x+96,464},14,.5f,false,true);if(col!=2)Icon(L"tab-close",{x+100,436,x+124,460});if(col==2)FocusRing(tab,Design::Radius::control);}keyboardModality=modality;
        Text(L"Production Direct2D + DirectWrite / Phosphor Bold / no WebView2",   /* i18n-exempt: developer specimen page */{24,496,900,528},14,.7f);
        return target->EndDraw();
    }
    // ---- Bookmarks (star in the field, optional band under the toolbar) -------------
    // Set by the host before Build/Paint. `bandShown` false keeps the 81 DIP chrome.
    struct BandItem { unsigned id=0; std::wstring title,tip; bool folder=false,local=false; std::shared_ptr<const FaviconImage> image; };
    std::vector<BandItem> bandItems; bool bandShown=false,bandButtonForced=false;
    bool starred=false;            // the active page is bookmarked: filled star
    int bandOpen=0;                // region whose menu is open: keeps its hover fill, chevron points up
    int bandRenaming=-1;           // band index renamed in place: hover surface, its title drawn by the editor
    int bandRover=-1;              // band item that takes the band's single Tab stop (roving focus)
    int bandDrag=-1;float bandDragOffset=0; // item being dragged (visible index) and its pointer offset
    int bandDropInto=-1;           // folder item under the dragged one: drop moves it inside
    float bandInsertX=-1;          // a bookmark dragged from a menu or the drawer: x (DIP) of the insertion line, -1 none
    BookmarksBar::Result band;     // last layout (Build)
    // Annotation mode (T3, src/annotation-mode.h): the "Annoter" button ('N') left of the tool cluster, and
    // the 32 DIP "Mode Annotation" band under the toolbar while the mode is on (the page moves down).
    bool annotateShown=false,annotateActive=false; float annotationBand=0; std::wstring annotateTip;   // empty until Ann::SyncChrome: AnnotateTip() falls back to the catalog at use
    const wchar_t* AnnotateTip() const { return annotateTip.empty() ? TR(chrome_tip_annotate) : annotateTip.c_str(); }
    std::function<void(ID2D1RenderTarget*,ID2D1Factory*,ID2D1SolidColorBrush*,D2D1_RECT_F,Motion::InteractionVisual,bool focus)> paintAnnotate;
    float AnnotateRight()const{return (audioShown?AudioLeft:97.f)+6;}   // from the right edge: 6 DIP left of Audio/History
    float ContentTop()const{return Design::Vertical::ContentTop(bandShown)+annotationBand;}
    D2D1_RECT_F BandRect(int i)const{return {band.left[i],BookmarksBar::Layout::top,band.left[i]+band.width[i],BookmarksBar::Layout::bottom};}
    // The title's box inside band item i (DIP): after the 16 DIP icon and its gap, before the folder
    // chevron; the in-place rename editor sits exactly here (design/bookmark-inline-rename-plan.md).
    D2D1_RECT_F BandTitleBox(int i)const{namespace L=BookmarksBar::Layout;const auto box=BandRect(i);float right=box.right-L::itemPad;if(i<(int)bandItems.size()&&bandItems[i].folder)right-=L::chevron+L::chevronGap;return {box.left+L::itemPad+Design::Size::icon+L::iconGap,box.top,right,box.bottom};}
    float BandTitleWidth(const BandItem& item){return item.title.empty()?0:TextWidth(item.title,Design::Type::label.size,Design::Type::label.medium,false,Design::Type::label.tracking);}
    void BuildBand(){
        band={};if(!bandShown)return;
        std::vector<BookmarksBar::Item> items;items.reserve(bandItems.size());
        for(auto& item:bandItems)items.push_back({BookmarksBar::Layout::Natural(BandTitleWidth(item),item.folder)});
        band=BookmarksBar::Arrange(width,items,bandButtonForced);
        for(int i=0;i<band.visible&&i<BookmarkLimit;++i)regions.push_back({BookmarkBase+i,BandRect(i),bandItems[i].tip.c_str(),i==bandRover||(bandRover<0&&i==0)});
        if(band.overflow)regions.push_back({'O',{band.overflowLeft,BookmarksBar::Layout::top,band.overflowLeft+Design::Size::control,BookmarksBar::Layout::bottom},TR(chrome_tip_bookmarks_overflow),bandRover<0&&band.visible==0});
        if(bandItems.empty()&&!band.overflow)regions.push_back({'E',{BookmarksBar::Layout::padding,BookmarksBar::Layout::top,BookmarksBar::Layout::padding+EmptyBandWidth(),BookmarksBar::Layout::bottom},TR(chrome_tip_add_page_bookmark)});
    }
    float EmptyBandWidth(){return 8+Design::Size::icon+6+TextWidth(TR(chrome_bookmarks_add_page),Design::Type::label.size,true,false,Design::Type::label.tracking)+16+KeyCapsWidth()+8;}
    float KeyCapsWidth(){return 2*12+TextWidth(L"Ctrl",14,false,true)+TextWidth(L"D",14,false,true)+2;}
    // Fill of a band item's own surface (for the title's fade mask).
    float BandSurface(float hover,float press){return Design::Resolve(Design::ButtonVariant::Ghost,{hover,press}).fill?Design::Resolve(Design::ButtonVariant::Ghost,{hover,press}).surface:Design::Surface::canvas;}
    void DrawBandItem(int i,const BandItem& item,int hover,int focus,Motion::Time now){
        namespace L=BookmarksBar::Layout;const int id=BookmarkBase+i;
        D2D1_RECT_F box=BandRect(i);const bool dragged=i==bandDrag;
        if(dragged){box.left+=bandDragOffset;box.right+=bandDragOffset;}
        auto v=motion?motion->Visual(id,now):Motion::InteractionVisual{hover==id?1.f:0.f,0};
        if(id==bandOpen||i==bandDropInto||i==bandRenaming)v.hover=1;
        const float surface=dragged?Design::Surface::raised:BandSurface(v.hover,v.press);
        if(dragged||v.hover>0||v.press>0)Round(box,Design::Radius::control,surface);
        if(focus==id)FocusRing(box,Design::Radius::control);
        const float cy=(box.top+box.bottom)/2;const D2D1_RECT_F icon{box.left+L::itemPad,cy-8,box.left+L::itemPad+16,cy+8};
        if(item.folder)Icon(L"folder-closed",icon,16,1);
        else if(!DrawSiteImage(item.image,icon))Icon(item.local?L"file":L"favicon",icon,16,1);
        const float emphasis=std::max(v.hover,id==bandOpen?1.f:0.f);
        float right=box.right-L::itemPad;
        if(item.folder){
            // Discreet chevron: 12 DIP at 32% (secondary on hover), pointing up while its menu is open.
            const D2D1_RECT_F caret{right-L::chevron,cy-L::chevron/2,right,cy+L::chevron/2};
            D2D1_MATRIX_3X2_F base;target->GetTransform(&base);
            if(id==bandOpen)target->SetTransform(D2D1::Matrix3x2F::Rotation(180,{(caret.left+caret.right)/2,(caret.top+caret.bottom)/2})*base);
            Icon(L"caret",caret,L::chevron,id==bandOpen?1.f:.64f+.36f*v.hover);
            target->SetTransform(base);right=caret.left-L::chevronGap;
        }
        if(item.title.empty()||i==bandRenaming)return;
        const float left=icon.right+L::iconGap;if(right<=left+1)return;
        const float textWidth=BandTitleWidth(item);
        target->PushAxisAlignedClip(D2D1::RectF(left,box.top,right,box.bottom),D2D1_ANTIALIAS_MODE_ALIASED);
        Text(item.title,{left,box.top,left+textWidth+2,box.bottom},Design::Type::label.size,Design::Text::secondary+(Design::Text::primary-Design::Text::secondary)*emphasis,false,Design::Type::label.medium,Design::Type::label.tracking);
        target->PopAxisAlignedClip();
        // Truncated titles dissolve into the item's surface (never an ellipsis), like the address.
        if(left+textWidth>right+.5f){
            const float c=surface,fade=std::min(16.f,right-left);ID2D1GradientStopCollection* stops=nullptr;ID2D1LinearGradientBrush* gradient=nullptr;
            D2D1_GRADIENT_STOP g[]={{0,D2D1::ColorF(c,c,c,0)},{1,D2D1::ColorF(c,c,c,1)}};
            if(SUCCEEDED(target->CreateGradientStopCollection(g,2,&stops))&&SUCCEEDED(target->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({right-fade,0},{right,0}),stops,&gradient)))target->FillRectangle(D2D1::RectF(right-fade,box.top+1,right,box.bottom-1),gradient);
            Drop(gradient);Drop(stops);
        }
    }
    void DrawBand(int hover,int focus,Motion::Time now){
        if(!bandShown)return;
        for(int i=0;i<band.visible&&i<(int)bandItems.size();++i)if(i!=bandDrag)DrawBandItem(i,bandItems[i],hover,focus,now);
        if(bandDrag>=0&&bandDrag<band.visible&&bandDrag<(int)bandItems.size())DrawBandItem(bandDrag,bandItems[bandDrag],hover,focus,now);
        {   // Drop feedback for a bookmark dragged in from a menu or the drawer: the folder it would enter is tinted with the
            // accent; otherwise a 2 DIP accent line marks where it would land. Nothing animates at rest.
            const auto accent=Design::Accent::Get();
            if(bandDrag<0&&bandDropInto>=0&&bandDropInto<band.visible)FillBox(BandRect(bandDropInto),Design::Radius::control,D2D1::ColorF(accent.r,accent.g,accent.b,.18f));
            if(bandInsertX>=0)FillBox({bandInsertX-1,BookmarksBar::Layout::top+4,bandInsertX+1,BookmarksBar::Layout::bottom-4},1,D2D1::ColorF(accent.r,accent.g,accent.b,1));
        }
        if(band.overflow){const D2D1_RECT_F box{band.overflowLeft,BookmarksBar::Layout::top,band.overflowLeft+Design::Size::control,BookmarksBar::Layout::bottom};
            auto v=motion?motion->Visual('O',now):Motion::InteractionVisual{hover=='O'?1.f:0.f,0};if(bandOpen=='O')v.hover=1;
            DrawButton(L"overflow",box,Design::ButtonVariant::Ghost,{v.hover,v.press,false,focus=='O'});}
        if(bandItems.empty()&&!band.overflow){
            // "Toujours" with no bookmark yet: one quiet way in, never an empty strip.
            const D2D1_RECT_F box{BookmarksBar::Layout::padding,BookmarksBar::Layout::top,BookmarksBar::Layout::padding+EmptyBandWidth(),BookmarksBar::Layout::bottom};
            auto v=motion?motion->Visual('E',now):Motion::InteractionVisual{hover=='E'?1.f:0.f,0};
            const auto style=Design::Resolve(Design::ButtonVariant::Ghost,{v.hover,v.press,false,focus=='E'});
            if(style.fill)Round(box,style.radius,style.surface);
            if(style.focus)FocusRing(box,style.radius);
            const float cy=(box.top+box.bottom)/2;Icon(L"star",{box.left+8,cy-8,box.left+24,cy+8},16,1);
            const float tw=TextWidth(TR(chrome_bookmarks_add_page),Design::Type::label.size,true,false,Design::Type::label.tracking);
            Text(TR(chrome_bookmarks_add_page),{box.left+30,box.top,box.left+30+tw+2,box.bottom},Design::Type::label.size,Design::Text::secondary+.5f*v.hover,false,true,Design::Type::label.tracking);
            float x=box.left+30+tw+16;for(const wchar_t* key:{L"Ctrl",L"D"}){const float w=TextWidth(key,14,false,true)+12;FillBox({x,cy-10,x+w,cy+10},4,D2D1::ColorF(1,1,1,.06f));keycap=true;Text(key,{x+5,cy-10,x+w,cy+10},14,Design::Text::disabled,true);keycap=false;x+=w+2;}
        }
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
    // Pending permission request (Paper B01 « Puce permission »): set by the host while the
    // request's popover is open for the active tab, cleared with it. Glyphs are SiteRequest::Path
    // strings (the popover's own), the tip names the request for the tooltip and UI Automation.
    std::vector<const char*> requestGlyphs; std::wstring requestTip;
    // State and motion set by the host each frame: `requestFrom` -> `requestState` cross-fade
    // (`requestMix` 0..1), chip opacity, the URL's animated offset (-1: follow the chip), and
    // whether the chip takes clicks (not while it fades out).
    SiteRequest::State requestState=SiteRequest::State::Pending,requestFrom=SiteRequest::State::Pending;
    float requestMix=1,requestAlpha=1,requestShift=-1;bool requestLive=true;
    D2D1_RECT_F request{};
    // Chip box: 2 DIP after the security slot (or its « Non sécurisé » plate). None while editing.
    D2D1_RECT_F RequestBox(){if(requestGlyphs.empty())return {};const auto i=IndicatorBox();if(i.right<=i.left)return {};namespace L=SiteRequest::Layout;const float c=AddressCenter(),l=i.right+L::lead;return {l,c-L::height/2,l+SiteRequest::ChipWidth(requestGlyphs.size(),requestState),c+L::height/2};}
    std::map<const char*,ID2D1PathGeometry*> pathGlyphs; // factory resources: survive target loss
    void DropPathGlyphs(){for(auto& g:pathGlyphs)if(g.second)g.second->Release();pathGlyphs.clear();}
    void PathGlyph(const char* d,D2D1_RECT_F box,float size,D2D1_COLOR_F color){
        if(!target||!brush||!factory||!d)return;
        auto& g=pathGlyphs[d];
        if(!g)g=DialogKit::SvgGeometry(factory,d);
        if(!g)return;
        D2D1_MATRIX_3X2_F old;target->GetTransform(&old);const float k=size/256,x=(box.left+box.right-size)/2,y=(box.top+box.bottom-size)/2;
        target->SetTransform(D2D1::Matrix3x2F::Scale(k,k)*D2D1::Matrix3x2F::Translation(x,y)*old);brush->SetColor(color);target->FillGeometry(g,brush);target->SetTransform(old);}
    // One state of the chip at opacity `a`, left-aligned in `box` (its own width).
    void DrawChipState(SiteRequest::State st,D2D1_RECT_F box,float a){
        namespace L=SiteRequest::Layout;using SiteRequest::State;if(a<=0)return;
        box.right=box.left+SiteRequest::ChipWidth(requestGlyphs.size(),st);
        const auto accent=Design::Accent::Get();
        if(st==State::Pending){const float p=((L::plate>>16)&0xFF)/255.f;FillBox(box,L::radius,D2D1::ColorF(p,p,p,a));}
        else if(st==State::Active)FillBox(box,L::radius,D2D1::ColorF(accent.r,accent.g,accent.b,L::tint*a));
        float gx=box.left+L::padX;const float cy=(box.top+box.bottom)/2;
        if(st==State::Active){brush->SetColor(D2D1::ColorF(accent.r,accent.g,accent.b,a));target->FillEllipse(D2D1::Ellipse({gx+L::dot/2,cy},L::dot/2,L::dot/2),brush);gx+=L::dot+L::activeGap;}
        const float glyphAlpha=(st==State::Granted||st==State::Blocked?L::dimmed:1.f)*a;const float step=L::icon+(st==State::Active?L::activeGap:L::gap);
        for(auto* g:requestGlyphs){const D2D1_RECT_F gb{gx,box.top,gx+L::icon,box.bottom};PathGlyph(g,gb,L::icon,D2D1::ColorF(1,1,1,glyphAlpha));
            if(st==State::Blocked){ // Paper E5Z-0: a slash cut out in the field colour, then drawn at 70 %
                const float k=L::icon/256,x0=gb.left,y0=cy-L::icon/2;const D2D1_POINT_2F p0{x0+28*k,y0+20*k},p1{x0+236*k,y0+236*k};
                ID2D1StrokeStyle* round=nullptr;D2D1_STROKE_STYLE_PROPERTIES sp{};sp.startCap=sp.endCap=D2D1_CAP_STYLE_ROUND;factory->CreateStrokeStyle(sp,nullptr,0,&round);
                const float f=Design::Surface::raised;brush->SetColor(D2D1::ColorF(f,f,f,a));target->DrawLine(p0,p1,brush,44*k,round);
                brush->SetColor(D2D1::ColorF(1,1,1,glyphAlpha));target->DrawLine(p0,p1,brush,22*k,round);if(round)round->Release();}
            gx+=step;}}
    void DrawRequestChip(D2D1_RECT_F box){
        const float a=std::clamp(requestAlpha,0.f,1.f),m=std::clamp(requestMix,0.f,1.f);
        if(m<1&&requestFrom!=requestState)DrawChipState(requestFrom,box,a*(1-m));
        DrawChipState(requestState,box,a*(requestFrom!=requestState?m:1.f));}
    D2D1_COLOR_F DangerColor(float a=1)const{return D2D1::ColorF(Design::Danger::r,Design::Danger::g,Design::Danger::b,a);}
    // B03 native key control (no icon asset yet: a drawn key; final glyph from the Paper writer). Ghost button.
    bool vaultKey=false;static constexpr int VaultKeyId=906;   // not a character id: 'W' is Ctrl+W (close tab)
    void DrawVaultKey(D2D1_RECT_F box,float hover,bool focused){
        if(hover>0)FillBox(box,Design::Radius::button,D2D1::ColorF(1,1,1,.06f*std::clamp(hover,0.f,1.f)));
        if(focused)FocusRing(box,Design::Radius::button);
        const float cx=(box.left+box.right)/2,cy=(box.top+box.bottom)/2;brush->SetColor(D2D1::ColorF(1,1,1,.8f));
        target->DrawEllipse(D2D1::Ellipse({cx-3.5f,cy},3.5f,3.5f),brush,1.5f);
        target->DrawLine({cx,cy},{cx+7,cy},brush,1.5f);target->DrawLine({cx+5,cy},{cx+5,cy+3},brush,1.5f);target->DrawLine({cx+7,cy},{cx+7,cy+2.5f},brush,1.5f);
    }
    // Profile button (region 900, Paper p-I-0 P01): the profile's Fill glyph on a plate of its ink.
    // `open`: the profile menu is showing (pressed look held). Private windows leave it off.
    // P05: an icon change cross-fades (incoming: opacity 0 -> 1, scale .25 -> 1, blur 4 -> 0 px; outgoing the
    // reverse) in 300 ms cubic-bezier(.2,0,0,1); a colour-only change eases the ink in 120 ms. Each starts from
    // what is on screen (interruptible). Reduced motion: the icon cross-fades by opacity only (300 ms), a colour
    // is applied at once.
    struct ProfileButtonState{bool shown=false,open=false;std::wstring name;const char* glyph=nullptr;D2D1_COLOR_F ink{1,1,1,1};
        const char* fromGlyph=nullptr;D2D1_COLOR_F fromInk{1,1,1,1};Motion::Time changed{};bool iconChange=false,animating=false;
        bool privateWindow=false;   // the incognito identity (eye-slash) in a private window
    }profileButton;
    static constexpr float profileIconMs=300,profileInkMs=120;
    float ProfileButtonProgress(Motion::Time now)const{const auto& b=profileButton;if(!b.animating)return 1;const float ms=static_cast<float>(std::chrono::duration<double,std::milli>(now-b.changed).count());return Motion::Clamp(ms/(b.iconChange?profileIconMs:profileInkMs));}
    D2D1_COLOR_F ProfileInkAt(Motion::Time now)const{const auto& b=profileButton;const float t=Motion::Eased(ProfileButtonProgress(now),Motion::Curve::Icon);return D2D1::ColorF(b.fromInk.r+(b.ink.r-b.fromInk.r)*t,b.fromInk.g+(b.ink.g-b.fromInk.g)*t,b.fromInk.b+(b.ink.b-b.fromInk.b)*t,1);}
    // Host entry point: a new look for the button; `animate` false (first paint, another window) snaps.
    void SetProfileButton(const std::wstring& name,const char* glyph,D2D1_COLOR_F ink,bool animate,Motion::Time now=Motion::Now()){
        auto& b=profileButton;const bool reduced=motion&&motion->ReducedMotion();
        const bool iconChange=b.glyph&&glyph&&b.glyph!=glyph,inkChange=b.ink.r!=ink.r||b.ink.g!=ink.g||b.ink.b!=ink.b;
        if(animate&&b.shown&&(iconChange||(inkChange&&!reduced))){
            b.fromInk=ProfileInkAt(now);
            if(iconChange)b.fromGlyph=ProfileButtonProgress(now)<.5f&&b.animating&&b.iconChange?b.fromGlyph:b.glyph;   // mid swap: keep the glyph mostly on screen
            b.iconChange=iconChange;b.changed=now;b.animating=true;
        }else{b.animating=false;b.fromGlyph=nullptr;}
        b.name=name;b.glyph=glyph;b.ink=ink;b.shown=true;
    }
    bool ProfileButtonAnimating(Motion::Time now){auto& b=profileButton;if(b.animating&&ProfileButtonProgress(now)>=1){b.animating=false;b.fromGlyph=nullptr;}return b.animating;}
    // One glyph at a pose of the swap: opacity, scale about the plate centre, Gaussian blur (DIP).
    void DrawSwapGlyph(const char* d,D2D1_RECT_F plate,float size,D2D1_COLOR_F color,float opacity,float scale,float blur){
        if(!d||opacity<=.002f){return;}color.a*=opacity;const float s=size*scale;
        if(blur<.05f){PathGlyph(d,plate,s,color);return;}
        ID2D1DeviceContext* dc=nullptr;ID2D1Effect* fx=nullptr;ID2D1BitmapRenderTarget* layer=nullptr;
        static const CLSID gaussian={0x1feb6d69,0x2fe6,0x4ac9,{0x8c,0x58,0x1d,0x7f,0x93,0xe7,0xa6,0xa5}};
        const float extent=size+4*blur*2;
        if(SUCCEEDED(target->QueryInterface(__uuidof(ID2D1DeviceContext),reinterpret_cast<void**>(&dc)))&&SUCCEEDED(dc->CreateEffect(gaussian,&fx))&&SUCCEEDED(target->CreateCompatibleRenderTarget(D2D1::SizeF(extent,extent),&layer))){
            ID2D1RenderTarget* keep=target;target=layer;layer->BeginDraw();layer->Clear(D2D1::ColorF(0,0,0,0));
            ID2D1SolidColorBrush* own=nullptr;layer->CreateSolidColorBrush(color,&own);ID2D1SolidColorBrush* keepBrush=brush;if(own)brush=own;
            PathGlyph(d,{0,0,extent,extent},s,color);brush=keepBrush;if(own)own->Release();target=keep;
            ID2D1Bitmap* bitmap=nullptr;
            if(SUCCEEDED(layer->EndDraw())&&SUCCEEDED(layer->GetBitmap(&bitmap))&&bitmap){
                fx->SetInput(0,bitmap);const float deviation=blur/2;fx->SetValue(0,D2D1_PROPERTY_TYPE_FLOAT,reinterpret_cast<const BYTE*>(&deviation),sizeof(deviation));
                ID2D1Image* out=nullptr;fx->GetOutput(&out);
                if(out){const D2D1_POINT_2F at{(plate.left+plate.right-extent)/2,(plate.top+plate.bottom-extent)/2};dc->DrawImage(out,&at,nullptr,D2D1_INTERPOLATION_MODE_LINEAR,D2D1_COMPOSITE_MODE_SOURCE_OVER);out->Release();}
                fx->SetInput(0,nullptr);bitmap->Release();
            }
        }else PathGlyph(d,plate,s,color);
        if(layer){layer->Release();}if(fx){fx->Release();}if(dc){dc->Release();}
    }
    void DrawProfileButton(int hover,int focus,Motion::Time now){namespace PB=Design::ProfileButton;
        const auto v=motion?motion->Visual(900,now):Motion::InteractionVisual{hover==900?1.f:0.f,0};const bool reduced=motion&&motion->ReducedMotion();
        const float press=std::max(v.press,profileButton.open?1.f:0.f),h=Motion::Clamp(v.hover);
        const float tint=PB::rest+(PB::hover-PB::rest)*h*(1-press)+(PB::pressed-PB::rest)*press;
        const auto p=PB::Plate();const float k=1-(1-PB::pressScale)*(reduced?0.f:press),cx=(p.left+p.right)/2,cy=(p.top+p.bottom)/2,half=PB::size/2*k;
        const D2D1_RECT_F plate{cx-half,cy-half,cx+half,cy+half};const auto ink=ProfileInkAt(now);
        FillBox(plate,PB::radius*k,D2D1::ColorF(ink.r,ink.g,ink.b,tint));
        const auto& b=profileButton;
        if(b.animating&&b.iconChange&&b.fromGlyph){
            const float t=Motion::Eased(ProfileButtonProgress(now),Motion::Curve::Icon);
            if(reduced){DrawSwapGlyph(b.fromGlyph,plate,PB::glyph*k,ink,1-t,1,0);DrawSwapGlyph(b.glyph,plate,PB::glyph*k,ink,t,1,0);}
            else{DrawSwapGlyph(b.fromGlyph,plate,PB::glyph*k,ink,1-t,1-.75f*t,4*t);DrawSwapGlyph(b.glyph,plate,PB::glyph*k,ink,t,.25f+.75f*t,4*(1-t));}
        }else if(b.glyph)PathGlyph(b.glyph,plate,PB::glyph*k,ink);
        if(focus==900)FocusRing({p.left,p.top,p.right,p.bottom},PB::radius);}
    // A9 « Privée » chip: drawn only, never hovered/focused/clicked (region kept for UIA bounds).
    static constexpr int PrivateChipId=905; bool privateChip=false; ID2D1PathGeometry* eyeSlash=nullptr;
    Design::Box PrivateChipRect(){return Design::PrivateChip::Rect(TextWidth(Design::PrivateChip::label(),14,true));}
    void DrawPrivateChip(){namespace PC=Design::PrivateChip;const auto c=PrivateChipRect();
        FillBox({c.left,c.top,c.right,c.bottom},PC::radius,D2D1::ColorF(PC::tintR,PC::tintG,PC::tintB,PC::tintA));
        const float gx=c.left+PC::padLeft,gy=c.top+(PC::height-PC::glyph)/2;
        if(!eyeSlash&&factory)eyeSlash=DialogKit::SvgGeometry(factory,PC::eyeSlash);
        if(eyeSlash){D2D1_MATRIX_3X2_F old;target->GetTransform(&old);target->SetTransform(D2D1::Matrix3x2F::Scale(PC::glyph/256,PC::glyph/256)*D2D1::Matrix3x2F::Translation(gx,gy)*old);brush->SetColor(D2D1::ColorF(1,1,1,1));target->FillGeometry(eyeSlash,brush);target->SetTransform(old);}
        const float tx=gx+PC::glyph+PC::gap;Text(PC::label(),{tx,c.top,c.right,c.bottom},14,1,false,true,0);
    }
    void FillBox(D2D1_RECT_F r,float radius,D2D1_COLOR_F c){brush->SetColor(c);target->FillRoundedRectangle(D2D1::RoundedRect(r,radius,radius),brush);}
    void DrawAddress(bool editing,bool indicatorHover){
        namespace A=Design::Address;using AddressBar::Security;using AddressBar::Tone;
        const auto slot=AddressSlot();const float right=edit.right,cy=AddressCenter();
        const D2D1_RECT_F line{0,bar.top,0,bar.bottom};
        if(editing){if(!DrawSiteImage(editFavicon,slot))Icon(editIcon?editIcon:address.icon(),slot,Design::Size::icon,A::secondary);return;}
        if(address.security==Security::Empty){Icon(L"search",slot,Design::Size::icon,A::secondary);
            Text(TR(chrome_address_placeholder),{bar.left+A::textInset,line.top,right,line.bottom},Design::Type::address.size,Design::Text::address,false,Design::Type::address.medium,Design::Type::address.tracking);return;}
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
            float edge=plate.right;if(request.right>request.left){DrawRequestChip(request);edge=request.right;}
            const float d=edge+A::divider;Color(1,.2f);target->FillRectangle(D2D1::RectF(d,cy-5,d+1,cy+5),brush);
            x=d+1+A::divider;
        }else{
            if(indicatorHover)FillBox(slot,A::chipRadius,D2D1::ColorF(1,1,1,A::chipPlate));
            Icon(address.icon(),slot,Design::Size::icon,A::secondary);
            // B01: lock (50 %) stays in its slot; the request chip follows, the URL moves past it.
            if(request.right>request.left){DrawRequestChip(request);x=requestShift>=0?bar.left+A::textInset+requestShift:request.right+SiteRequest::Layout::trail+Design::Space::xs;}
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
    void Build(int count,int selected,const std::vector<std::wstring>* titles=nullptr,Motion::Time now=Motion::Now()){regions.clear();tips.clear();if(contentOnly)return;
        namespace TS=Design::TabStrip;
        // Private window: reserve the chip and its 6 DIP row gap before any tab computation (capacity,
        // scroll, drag, hit tests and focus all go through TS::Left()).
        TS::leading=privateChip?Design::PrivateChip::Width(TextWidth(Design::PrivateChip::label(),14,true))+Design::Space::tabGap:0;
        strip=TS::Layout(width,count,selected,scroll,lockedTab);scroll=strip.scroll;SyncTabMotion(count,now);
        brandTip=profileButton.shown?(profileButton.privateWindow?std::wstring(TR(chrome_tip_private_window)):TRF(chrome_tip_profile,profileButton.name)):TRF(chrome_tip_tab_list,std::to_wstring(count));
        auto brand=Design::Brand::Region();regions.push_back({900,{brand.left,brand.top,brand.right,brand.bottom},brandTip.c_str()});
        if(privateChip){const auto c=PrivateChipRect();regions.push_back({PrivateChipId,{c.left,c.top,c.right,c.bottom},nullptr});}
        // Tooltips carry the full title: compact tabs truncate or hide it.
        tips.reserve(count);for(int i=0;i<count;i++){const std::wstring title=titles&&i<(int)titles->size()?(*titles)[i]:L"";tips.push_back(title.empty()?std::wstring(TR(chrome_tab_untitled_tip)):title);}
        const float viewRight=ShownViewRight();
        for(auto& item:tabFrame.items){const int i=item.index;if(i<0||i>=count)continue;
            auto surface=ShownSurface(item);if(surface.right-surface.left<.5f||surface.right<=strip.ViewLeft()||surface.left>=viewRight)continue;
            auto close=strip.CloseIn(ShownContent(item));auto region=ShownRegion(item);
            region.left=std::max(region.left,TS::Left());region.right=std::min(region.right,viewRight);
            // Close first: its hit area wins over the containing tab, and IDs
            // never collide with tabs, keyboard commands or toolbar buttons.
            // Tabs under a fade stay clickable (selecting scrolls them clear) but
            // expose no close target and take no keyboard focus. A tab still
            // opening exposes its close target once the glyph is fully revealed.
            const bool clear=strip.Clear(i);
            if(clear&&strip.Closable(i,selected)&&close.right<=surface.right+.01f)regions.push_back({CloseBase+i,{close.left,close.top,close.right,close.bottom},TR(chrome_tip_close_tab)});
            regions.push_back({TabBase+i,{region.left,region.top,region.right,region.bottom},tips[i].c_str(),clear});
        }
        float x=viewRight+Design::Space::tabGap;regions.push_back({'T',{x,6,x+Design::Size::control,38},TR(chrome_tip_new_tab)});
        regions.push_back({'m',{width-118,4,width-86,36},TR(chrome_tip_minimize)});regions.push_back({'M',{width-78,4,width-46,36},TR(chrome_tip_maximize)});regions.push_back({'X',{width-38,4,width-6,36},TR(chrome_tip_close_window)});
        {namespace T=Design::Toolbar;regions.push_back({'B',{T::backLeft,44,T::backRight,76},TR(chrome_tip_back)});regions.push_back({'F',{T::forwardLeft,44,T::forwardRight,76},TR(chrome_tip_forward)});regions.push_back({'R',{T::reloadLeft,44,T::reloadRight,76},navigationLoading?TR(chrome_tip_stop):TR(chrome_tip_reload)});bar={T::addressLeft,44,width-(annotateShown?AnnotateRight()+24+Design::Space::controlGap:audioShown?AudioLeft+Design::Space::controlGap:AudioRight),76};}edit={bar.left+Design::Address::textInset,44,bar.right-70,76};
        indicator=IndicatorBox();if(indicator.right>indicator.left)regions.push_back({'I',indicator,address.tip(),false});
        request=RequestBox();if(request.right>request.left&&requestLive)regions.push_back({'Q',request,requestTip.c_str(),false});
        // Field actions, right to left 8 DIP apart (Paper): star, copy link. Reload/Stop lives
        // only in the standalone toolbar button; the field text runs to 10 DIP before Copy.
        // B03 (vault, review §5): « Remplir un mot de passe enregistré », left of Copy, only when the host says so.
        regions.push_back({'L',{bar.left,44,bar.right-(vaultKey?96.f:64.f),76},TR(chrome_tip_address)});
        if(vaultKey){regions.push_back({VaultKeyId,{bar.right-92,48,bar.right-68,72},TR(chrome_tip_fill_password)});}
        regions.push_back({'C',{bar.right-60,48,bar.right-36,72},TR(chrome_tip_copy_link)});
        regions.push_back({'K',{bar.right-28,48,bar.right-4,72},starred?TR(chrome_tip_edit_bookmark):TR(chrome_tip_add_bookmark)});
        if(annotateShown)regions.push_back({'N',{width-AnnotateRight()-24,48,width-AnnotateRight(),72},AnnotateTip()});
        if(audioShown)regions.push_back({'V',{width-AudioLeft,48,width-AudioRight,72},TR(chrome_tip_audio)});
        // Right cluster, 24 DIP buttons 6 apart, then the separator and Réglages. No private-window button: « Nouvelle
        // fenêtre privée » lives in the profile menu and Ctrl+Maj+N (command 'P' unchanged).
        regions.push_back({'Y',{width-97,48,width-73,72},TR(chrome_tip_history)});regions.push_back({'D',{width-67,48,width-43,72},TR(chrome_tip_downloads)});regions.push_back({'S',{width-30,48,width-6,72},TR(chrome_tip_settings)});
        BuildBand();
    }
    std::vector<int> FocusOrder()const{std::set<int> closes;for(auto& region:regions)if(IsClose(region.id))closes.insert(region.id);std::vector<int> order;for(auto& region:regions){if(IsClose(region.id)||!region.focusable)continue;if(IsBookmark(region.id)||region.id=='O'){if(region.id!=BandStop())continue;}if(Enabled(region.id))order.push_back(region.id);if(IsTab(region.id)){int close=CloseBase+TabIndex(region.id);if(closes.count(close)&&Enabled(close))order.push_back(close);}}return order;}
    // The band's single Tab stop: the last focused item, else the first (or the » button alone).
    int BandStop()const{if(!bandShown)return 0;if(bandRover>=0&&bandRover<band.visible)return BookmarkBase+bandRover;if(bandRover==-2&&band.overflow)return 'O';if(band.visible>0)return BookmarkBase;return band.overflow?'O':0;}
    // Band order for arrows: visible items, then the » button.
    std::vector<int> BandOrder()const{std::vector<int> o;for(int i=0;i<band.visible;++i)o.push_back(BookmarkBase+i);if(band.overflow)o.push_back('O');return o;}
    int Hit(float x,float y){for(auto& r:regions)if(x>=r.rect.left&&x<r.rect.right&&y>=r.rect.top&&y<r.rect.bottom)return Enabled(r.id)?r.id:0;return 0;}
    bool AssetsReady(){if(bitmaps.size()<11)return false;for(auto& b:bitmaps)if(!b.second)return false;return fonts&&fonts->GetFontFamilyCount()==1;}
    // Tabs at their presented geometry. Exactly one surface per tab (no second
    // close-button hover fill). While a tab opens its surface width follows
    // `presence` and its content, laid out at the settled width, is clipped to it
    // and fades with `opacity`; a closing tab is a ghost that blurs out beneath.
    template<class Visible> void DrawTabs(const std::vector<std::wstring>& titles,int selected,int hover,int focus,Visible visible,Motion::Time now){
        namespace TS=Design::TabStrip;const bool keyed=TabKeysValid((int)titles.size());
        auto box2rect=[](Design::Box b){return D2D1::RectF(b.left,b.top,b.right,b.bottom);};
        const D2D1_RECT_F viewport=D2D1::RectF(strip.ViewLeft(),TS::regionTop,ShownViewRight(),TS::regionBottom);
        // Ghosts first: neighbours glide over a closing tab while it blurs out.
        for(auto& item:tabFrame.items)if(item.index<0&&visible(GhostRect(item)))DrawGhost(item);
        // Split groups: a 2 x 10 separator (white 30 %) between adjacent members of one group.
        if(!tabGroups.empty()){target->PushAxisAlignedClip(viewport,D2D1_ANTIALIAS_MODE_ALIASED);
            for(int n=0;n+1<(int)tabGroups.size()&&n+1<strip.count;++n)if(tabGroups[n]&&tabGroups[n]==tabGroups[n+1]&&n!=liftedIndex&&n+1!=liftedIndex&&n!=dragIndex&&n+1!=dragIndex){
                auto a=ShownTab(n),b=ShownTab(n+1);const float x=(a.right+b.left)/2,y=(a.top+a.bottom)/2;Color(1,Design::Surface::separatorOpacity);target->FillRoundedRectangle(D2D1::RoundedRect({x-1,y-5,x+1,y+5},1,1),brush);}
            // Designated target of the split menu: one tab outlined on its shape, a group around its members (2 DIP out, radius 8).
            if(!menuTargets.empty()){D2D1_RECT_F u{1e9f,1e9f,-1e9f,-1e9f};for(int n:menuTargets)if(n>=0&&n<strip.count){auto r=ShownTab(n);u={std::min(u.left,r.left),std::min(u.top,r.top),std::max(u.right,r.right),std::max(u.bottom,r.bottom)};}
                if(u.right>u.left){const bool group=menuTargets.size()>1;if(group)u={u.left-2,u.top-2,u.right+2,u.bottom+2};const float rr=group?8.f:Design::Radius::control;
                    Color(1,Design::Focus::keyboardOpacity);target->DrawRoundedRectangle(D2D1::RoundedRect({u.left+.5f,u.top+.5f,u.right-.5f,u.bottom-.5f},rr-.5f,rr-.5f),brush,1);}}
            target->PopAxisAlignedClip();}
        auto drawOne=[&](const Motion::TabItem& item){
            const int n=item.index;
            D2D1_RECT_F tab=box2rect(ShownSurface(item)),content=box2rect(ShownContent(item));
            if(n==dragIndex){auto s=strip.Surface(n);s.left+=dragOffset;s.right+=dragOffset;tab=content={s.left,s.top,s.right,s.bottom};}
            if(tab.right-tab.left<.25f||!visible(tab))return;
            const bool partial=item.presence<1&&n!=dragIndex;
            const float radius=std::min(Design::Radius::control,(tab.right-tab.left)/2);
            // Partially scrolled tabs are cut at the strip viewport, then masked.
            target->PushAxisAlignedClip(viewport,D2D1_ANTIALIAS_MODE_ALIASED);
            if(n==liftedIndex){Color(1,.10f);target->DrawRoundedRectangle(D2D1::RoundedRect({tab.left+.5f,tab.top+.5f,tab.right-.5f,tab.bottom-.5f},5.5f,5.5f),brush,1);target->PopAxisAlignedClip();return;}
            const Design::Box box{content.left,content.top,content.right,content.bottom};
            auto close=strip.CloseIn(box);auto title=strip.TitleIn(box);auto favicon=strip.FaviconIn(box);
            const bool closable=strip.Clear(n)&&strip.Closable(n,selected)&&n!=dragIndex;
            bool tabHovered=TabIndex(hover)==n;float tabHover=motion?std::max(motion->Visual(TabBase+n,now).hover,motion->Visual(CloseBase+n,now).hover):(tabHovered?1.f:0.f);
            float emphasis=dragIndex<0&&motion&&animateSelection?motion->selection.Visual((int)TabKey(n),now):(n==selected?1.f:0.f);
            const int group=n<(int)tabGroups.size()?tabGroups[n]:0;const bool member=group!=0;
            // Split group (Paper 08): members share a neutral 1 DIP white-10 % border, outer
            // corners 6 / inner corners 2, and a 2 x 10 separator between them. In the group
            // on screen every member has a surface; the active one is the selected #292929.
            float base=member&&group==visibleTabGroup?std::max(Design::Surface::tabHover,Design::TabSurface(false,tabHover)):Design::TabSurface(false,tabHover);
            // Reduced motion: an opening tab cross-fades its opaque fill from the canvas.
            const float fill=Design::Surface::canvas+(base+(Design::Surface::selected-base)*emphasis-Design::Surface::canvas)*item.fill;
            bool filled=false;
            if(member){const bool first=n==0||tabGroups[n-1]!=group,last=n+1>=(int)tabGroups.size()||tabGroups[n+1]!=group;
                const float rl=std::min(first?Design::Radius::control:2.f,radius),rr=std::min(last?Design::Radius::control:2.f,radius);
                if(emphasis>0||tabHover>0||n==dragIndex||group==visibleTabGroup){FillShape(tab,rl,rr,fill);filled=true;}
                StrokeShape({tab.left+.5f,tab.top+.5f,tab.right-.5f,tab.bottom-.5f},std::max(0.f,rl-.5f),std::max(0.f,rr-.5f),1,Design::Focus::pointerOpacity);}
            else if(emphasis>0||tabHover>0||n==dragIndex){Color(fill);target->FillRoundedRectangle(D2D1::RoundedRect(tab,radius,radius),brush);filled=true;}
            // Sleeping (never the selected tab): favicon at 40%, title at 32% instead of muted.
            const bool asleep=n!=selected&&n<(int)sleeping.size()&&sleeping[n];
            if(keyed){auto& snap=tabSnapshots[item.key];if(n<(int)titles.size())snap.title=titles[n];snap.favicon=n<(int)favicons.size()?favicons[n]:nullptr;snap.fill=filled?fill:Design::Surface::canvas;snap.selected=n==selected;}
            if(focus==TabBase+n)FocusRing(tab,radius);
            if(partial)target->PushAxisAlignedClip(tab,D2D1_ANTIALIAS_MODE_ALIASED);
            const float a=item.opacity;
            // Narrow selected tab: the close glyph cross-fades in over the favicon it replaces.
            const float closeShown=closable?(focus==CloseBase+n?1.f:tabHover):0.f;
            DrawFavicon(n,{favicon.left,favicon.top,favicon.right,favicon.bottom},(strip.SwapsClose(n,selected)?1-closeShown:1)*(asleep?.4f:1)*a);
            if(strip.Titled()&&title.right-title.left>=Design::Size::icon)Text(titles[n],{title.left,title.top,title.right,title.bottom},Design::Type::label.size,(n==selected?Design::Text::secondary:asleep?.32f:Design::Text::muted)*a,false,Design::Type::label.medium,Design::Type::label.tracking);
            if(closable&&(tabHover>0||focus==CloseBase+n)){float press=motion?motion->Visual(CloseBase+n,now).press:0;Icon(L"tab-close",{close.left,close.top,close.right,close.bottom},Design::Size::icon*Motion::PressScale(press,motion&&motion->ReducedMotion()),closeShown*a);}
            if(closable&&focus==CloseBase+n)FocusRing({close.left,close.top,close.right,close.bottom},Design::Radius::button);
            if(partial)target->PopAxisAlignedClip();
            target->PopAxisAlignedClip();
        };
        // Paint the grabbed tab last so it stays above its neighbours.
        for(auto& item:tabFrame.items)if(item.index>=0&&item.index!=dragIndex)drawOne(item);
        for(auto& item:tabFrame.items)if(item.index>=0&&item.index==dragIndex)drawOne(item);
        if(keyed)for(auto it=tabSnapshots.begin();it!=tabSnapshots.end();){if(std::none_of(tabFrame.items.begin(),tabFrame.items.end(),[&](const Motion::TabItem& i){return i.key==it->first;}))it=tabSnapshots.erase(it);else ++it;}
    }
    // Smoke diagnostics: why AssetsReady() is false (bitmap count, keys that failed to load, font families).
    std::wstring AssetsReport(){std::wstring s=L"bitmaps="+std::to_wstring(bitmaps.size())+L" fonts="+std::to_wstring(fonts?fonts->GetFontFamilyCount():0)+L" missing=";for(auto& b:bitmaps)if(!b.second)s+=b.first+L";";return s;}
    void Paint(const std::vector<std::wstring>& titles,int selected,const std::wstring& uri,bool editing,int hover,int focus,const D2D1_RECT_F* dirty=nullptr,Motion::Time now=Motion::Now()){(void)uri;/* the field reads `address`, set by the host */if(!target||!brush)return;Build((int)titles.size(),selected,&titles,now);target->BeginDraw();target->PushAxisAlignedClip(dirty?*dirty:D2D1::RectF(0,0,width,height),D2D1_ANTIALIAS_MODE_ALIASED);target->Clear(D2D1::ColorF(Design::Surface::canvas,Design::Surface::canvas,Design::Surface::canvas));if(!contentOnly){
        Round(bar,Design::Radius::control,Design::Surface::raised);DrawLoading(bar);if(focus=='L')FocusRing(bar,Design::Radius::control);
        // Skip controls wholly outside the dirty rectangle (4 DIP margin covers focus rings).
        auto visible=[&](D2D1_RECT_F b){return !dirty||(b.right+4>dirty->left&&b.left-4<dirty->right&&b.bottom+4>dirty->top&&b.top-4<dirty->bottom);};
        DrawTabs(titles,selected,hover,focus,visible,now);
        for(auto& r:regions){int id=r.id;auto box=r.rect;if(IsClose(id)||IsTab(id)||!visible(box))continue;
            if(id==PrivateChipId){DrawPrivateChip();continue;}
            if(id==VaultKeyId){DrawVaultKey(box,motion?motion->Visual(id,now).hover:(hover==id?1.f:0.f),focus==VaultKeyId);continue;}
            if(id==900){if(profileButton.shown){DrawProfileButton(hover,focus,now);continue;}auto glyph=Design::Brand::Glyph();Icon(L"brand-flower",{glyph.left,glyph.top,glyph.right,glyph.bottom},Design::Brand::size,Design::Brand::opacity);if(focus==900)FocusRing(box,Design::Radius::control);continue;}if(id=='L'||id=='I'||id=='Q')continue;
            auto interaction=motion?motion->Visual(id,now):Motion::InteractionVisual{hover==id?1.f:0.f,0};if(id==latched)interaction.hover=1;
            if(id=='V'){DrawAudioButton(box,{interaction.hover,interaction.press,false,focus==id},now);continue;}
            if(id=='N'){if(paintAnnotate)paintAnnotate(target,factory,brush,box,interaction,focus==id);continue;}
            const wchar_t* icon=nullptr;switch(id){case 'T':icon=L"plus";break;case 'm':icon=L"minimize";break;case 'M':icon=L"maximize";break;case 'X':icon=L"close";break;case 'B':icon=L"back";break;case 'F':icon=L"forward";break;case 'R':icon=navigationLoading?L"tab-close":L"reload";break;case 'C':icon=L"copy";break;case 'K':icon=starred?L"star-fill":L"star";break;case 'Y':icon=L"history";break;case 'D':icon=L"downloads";break;case 'P':icon=L"private";break;case 'S':icon=L"settings";break;}if(icon){auto variant=VariantFor(id);Design::ButtonState state{interaction.hover,interaction.press,!Enabled(id),focus==id};
                if((id=='C'||id=='R')&&motion){
                    auto style=Design::Resolve(variant,state);if(style.fill){Round(box,style.radius,style.surface);if(variant==Design::ButtonVariant::Embedded)DrawLoading(box);}if(style.focus)FocusRing(box,style.radius);
                    float size=style.iconSize*Motion::PressScale(state.press,motion->ReducedMotion(),!state.disabled);
                    bool painted=false;
                    if(!motion->ReducedMotion()){
                        if(id=='C'&&lottie.CopyActive(now))painted=DrawLottie(copyLottie,lottie.CopyProgress(now),box,size,style.iconOpacity);
                        if(id=='R'&&lottie.RefreshActive(now))painted=DrawLottie(refreshLottie,lottie.RefreshProgress(now),box,size,style.iconOpacity);
                    }
                    // Missing/invalid assets keep the native static Phosphor endpoint.
                    if(!painted&&id=='C'&&(motion->ReducedMotion()||copyLottie.Duration()==0)){
                        float a=motion->copy.Visual(now).opacity;
                        Icon(icon,box,size,(1-a)*style.iconOpacity);Icon(L"check",box,size,a*style.iconOpacity);
                    }else if(!painted&&id=='R')DrawStopMorph(box,size,style.iconOpacity,now);
                    else if(!painted)Icon(icon,box,size,style.iconOpacity);
                }
                else if(id=='D'&&(!downloadIcon.At(now).idle||downloadIcon.At(now).ripple>=0)){
                    auto pose=downloadIcon.At(now);auto style=Design::Resolve(variant,state);
                    if(style.fill)Round(box,style.radius,style.surface);
                    if(style.focus)FocusRing(box,style.radius);
                    const float size=style.iconSize*Motion::PressScale(state.press,motion&&motion->ReducedMotion(),!state.disabled);
                    if(pose.idle||!DrawDownloadIcon(pose,box,size,style.iconOpacity))Icon(icon,box,size,style.iconOpacity);
                }
                else if(id=='X')DrawCloseWindow(box,state);
                else if(id=='B'||id=='F')DrawNavButton(id,icon,box,state,now);
                else DrawButton(icon,box,variant,state);}

        }
        // Fades follow the presented strip (scroll/visible width may be gliding).
        if(tabFrame.scroll>.5f)DrawFade(strip.ViewLeft(),strip.ViewLeft()+Design::TabStrip::fade);
        if(tabFrame.scroll<tabFrame.content-tabFrame.view-.5f)DrawFade(ShownViewRight(),ShownViewRight()-Design::TabStrip::fade);
        Color(Design::Text::primary,Design::Surface::separatorOpacity);target->FillRoundedRectangle(D2D1::RoundedRect({width-37,55,width-36,65},.5f,.5f),brush);
        DrawAddress(editing,hover=='I');
        DrawBand(hover,focus,now);
        // Chrome's lower edge: a 1 DIP #FFFFFF05 hairline, then the page (Paper: no top padding).
        Color(1,5/255.f);target->FillRectangle(D2D1::RectF(0,ContentTop()-1,width,ContentTop()),brush);
        DrawSplit();
        // Ripple last, over the neighbouring buttons it reaches, never over the page edge.
        if(const auto ripple=downloadIcon.At(now).ripple;ripple>=0){
            const auto d=std::find_if(regions.begin(),regions.end(),[](const Region& r){return r.id=='D';});
            if(d!=regions.end()){target->PushAxisAlignedClip(D2D1::RectF(0,0,width,ContentTop()-1),D2D1_ANTIALIAS_MODE_ALIASED);DrawDownloadRipple(ripple,d->rect,Design::Size::icon);target->PopAxisAlignedClip();}
        }
        }   // !contentOnly
        target->PopAxisAlignedClip();HRESULT hr=target->EndDraw();if(hr==D2DERR_RECREATE_TARGET){DropGhostLayer();DropFade();DropDownloadIcon();DropAudioIcon();DropRipple();ClearFaviconBitmaps();for(auto& b:bitmaps)Drop(b.second);bitmaps.clear();Drop(brush);Drop(target);hwndTarget=nullptr;Resize();}
    }
};
}
