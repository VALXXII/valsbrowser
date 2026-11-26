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
#include "local-pill.h"
#include "bookmarks-bar.h"
#include "svg-path.h"
#include "site-request.h"
#include "sidebar.h"
#include "emoji-text.h"
#include "focus-mode.h"
#include <set>
#include <tuple>
#include <cstring>

namespace NativeChrome {
template<class T> void Drop(T*& p){if(p){p->Release();p=nullptr;}}
// SemiBold: the onboarding boards (Paper p-R-0) set their titles and buttons in weight 600.
const wchar_t* FontFiles[]={L"InterDisplay-Regular.ttf",L"InterDisplay-Medium.ttf",L"InterDisplay-SemiBold.ttf"};
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
struct TextMeasure {float lineHeight=0,baseline=0,originY=0,inkTop=0,inkBottom=0;bool trimmed=false;EmojiText::Style emoji;};
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
    struct TextEntry{IDWriteTextLayout* layout=nullptr;TextMeasure measure;unsigned long long used=0;};
    std::map<TextKey,TextEntry> textCache;
    // Paint() counts its passes: when the cache is full, only layouts the current pass has not used
    // are evicted, so a pass that needs more than the limit never empties the cache under itself.
    unsigned long long textPass=0,textEvictedPass=~0ull;
public:
    // Present without blocking the UI thread on vblank (pacing comes from FrameClock)
    // and keep the back buffer so partial invalidation stays correct.
    D2D1_PRESENT_OPTIONS presentOptions=D2D1_PRESENT_OPTIONS(D2D1_PRESENT_OPTIONS_RETAIN_CONTENTS|D2D1_PRESENT_OPTIONS_IMMEDIATELY);
    bool cacheText=true; // false only for the pacing harness baseline
    // Main chrome keeps 96 (pacing-tested); the downloads surfaces need room for
    // every visible row so animation frames never rebuild layouts.
    size_t textCacheLimit=96;
    unsigned long long textLayoutsCreated=0;
    unsigned long long faviconUploads=0; // site images uploaded as GPU bitmaps (tests measure re-uploads)
    void ClearTextCache(){for(auto& item:textCache)Drop(item.second.layout);textCache.clear();}
    Loading::Visual loading;
    bool navigationLoading=false;
    // DIP rectangle repainted for loading-only frames (URL surface incl. embedded buttons).
    // DIP rectangle repainted for download-icon-only frames (the 'D' button).
    // The ripple grows well past the button (x4): the rectangle covers its reach,
    // down to the chrome's lower edge (the page covers what lies below).
    D2D1_RECT_F RegionRect(int id)const{for(auto& r:regions)if(r.id==id)return r.rect;return {};}
    D2D1_RECT_F DownloadIconDirty()const{if(sidebar){auto b=RegionRect('D');const float cx=(b.left+b.right)/2,cy=(b.top+b.bottom)/2,r=DownloadIcon::Ripple::reach*Design::Size::icon/256+1;return {cx-r,cy-r,cx+r,cy+r};}const float cx=width-55,cy=60,r=DownloadIcon::Ripple::reach*Design::Size::icon/256+1;return {cx-r,cy-r,cx+r,std::min(cy+r,ContentTop())};}
    // Audio button (left of History, only while a site has made sound): icon-only frames.
    static constexpr float AudioRight=103,AudioLeft=127;   // from the window's right edge; the field ends 6 DIP before
    bool audioShown=false;   // tooltip resolved at use (TR(chrome_tip_audio)): `chrome` is a global built before the language is pinned
    Audio::IconModel audioIcon;
    D2D1_RECT_F AudioIconDirty()const{if(sidebar){auto b=RegionRect('V');return {b.left-1,b.top-1,b.right+1,b.bottom+1};}return {width-AudioLeft-1,47,width-AudioRight+1,73};}
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
        // At an outer bottom corner the page is round (6 DIP): the ring follows it (8 on its centre line). Mode Focus: the
        // outer top corners too (the page's 6 DIP corners, 8 on the ring's centre line, 2 DIP off the page).
        if(split.ring){auto a=split.active;Color(1,keyboardModality?Design::Focus::keyboardOpacity:Design::Focus::pointerOpacity);
            if(auto g=CornerBox({a.left-2,a.top-2,a.right+2,a.bottom+2},split.activeTopLeft?8.f:2.f,split.activeTopRight?8.f:2.f,split.activeBottomRight?8.f:2.f,split.activeBottomLeft?8.f:2.f)){target->DrawGeometry(g,brush,2);g->Release();}}
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
    ID2D1PathGeometry* CornerBox(D2D1_RECT_F r,float tl,float tr,float br,float bl,bool filled=false){
        ID2D1PathGeometry* g=nullptr;if(!factory||FAILED(factory->CreatePathGeometry(&g)))return nullptr;ID2D1GeometrySink* s=nullptr;if(FAILED(g->Open(&s))){g->Release();return nullptr;}
        auto arc=[&](D2D1_POINT_2F to,float radius){if(radius>0)s->AddArc(D2D1::ArcSegment(to,{radius,radius},0,D2D1_SWEEP_DIRECTION_CLOCKWISE,D2D1_ARC_SIZE_SMALL));else s->AddLine(to);};
        s->BeginFigure({r.left+tl,r.top},filled?D2D1_FIGURE_BEGIN_FILLED:D2D1_FIGURE_BEGIN_HOLLOW);s->AddLine({r.right-tr,r.top});arc({r.right,r.top+tr},tr);s->AddLine({r.right,r.bottom-br});arc({r.right-br,r.bottom},br);
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
        auto& bitmap=faviconBitmaps[image];if(!bitmap){++faviconUploads;auto properties=D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));target->CreateBitmap(D2D1::SizeU(image->width,image->height),image->pixels.data(),image->stride,properties,&bitmap);}if(!bitmap)return false;
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
    // Keeps the GPU copy of every image this chrome still draws (tabs, closing-tab ghosts, bookmarks bar,
    // the field's suggestion icon): each is uploaded once per target, not on every paint.
    void SetFavicons(std::vector<std::shared_ptr<const FaviconImage>> images){favicons=std::move(images);
        auto kept=[&](const std::shared_ptr<const FaviconImage>& image){if(image==editFavicon||std::find(favicons.begin(),favicons.end(),image)!=favicons.end())return true;
            for(auto& item:bandItems){if(item.image==image)return true;}
            for(auto& snap:tabSnapshots){if(snap.second.favicon==image)return true;}
            return false;};
        for(auto it=faviconBitmaps.begin();it!=faviconBitmaps.end();){if(!kept(it->first)){Drop(it->second);it=faviconBitmaps.erase(it);}else ++it;}}
    // Missing/empty/invalid site images are normal favicon absence, not engine errors.
    std::shared_ptr<const FaviconImage> DecodeFavicon(IStream* stream){if(!wic||!stream)return {};STATSTG stat{};if(FAILED(stream->Stat(&stat,STATFLAG_NONAME))||stat.cbSize.QuadPart==0||stat.cbSize.QuadPart>4*1024*1024)return {};LARGE_INTEGER zero{};if(FAILED(stream->Seek(zero,STREAM_SEEK_SET,nullptr)))return {};IWICBitmapDecoder* decoder=nullptr;IWICBitmapFrameDecode* frame=nullptr;IWICFormatConverter* converter=nullptr;UINT w=0,h=0;HRESULT hr=wic->CreateDecoderFromStream(stream,nullptr,WICDecodeMetadataCacheOnLoad,&decoder);if(SUCCEEDED(hr))hr=decoder->GetFrame(0,&frame);if(SUCCEEDED(hr))hr=frame->GetSize(&w,&h);if(SUCCEEDED(hr)&&(!w||!h||w>1024||h>1024))hr=E_INVALIDARG;if(SUCCEEDED(hr))hr=wic->CreateFormatConverter(&converter);if(SUCCEEDED(hr))hr=converter->Initialize(frame,GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom);std::shared_ptr<FaviconImage> image;if(SUCCEEDED(hr)){image=std::make_shared<FaviconImage>();image->width=w;image->height=h;image->stride=w*4;image->pixels.resize(image->stride*h);hr=converter->CopyPixels(nullptr,image->stride,(UINT)image->pixels.size(),image->pixels.data());}Drop(converter);Drop(frame);Drop(decoder);return SUCCEEDED(hr)?image:nullptr;}
    // A site image outside the tab list (suggestions, the field while editing), 16 DIP
    // centred in `box`. Bitmaps share the tab favicon cache (pruned with it).
    bool DrawSiteImage(const std::shared_ptr<const FaviconImage>& image,D2D1_RECT_F box,float opacity=1){if(!image||!image->width||!image->height)return false;
        auto& bitmap=faviconBitmaps[image];if(!bitmap){++faviconUploads;auto properties=D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));target->CreateBitmap(D2D1::SizeU(image->width,image->height),image->pixels.data(),image->stride,properties,&bitmap);}if(!bitmap)return false;
        const float side=Design::Size::icon,factor=std::min(side/image->width,side/image->height),w=image->width*factor,h=image->height*factor,x=(box.left+box.right-w)/2,y=(box.top+box.bottom-h)/2;
        target->DrawBitmap(bitmap,{x,y,x+w,y+h},opacity);return true;}
    void DrawFavicon(int index,D2D1_RECT_F slot,float opacity=1){if(opacity<=0)return;auto image=index<(int)favicons.size()?favicons[index]:nullptr;if(!image){Icon(L"favicon",slot,16,opacity);return;}auto& bitmap=faviconBitmaps[image];if(!bitmap){++faviconUploads;auto properties=D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));target->CreateBitmap(D2D1::SizeU(image->width,image->height),image->pixels.data(),image->stride,properties,&bitmap);}if(bitmap){float factor=std::min((slot.right-slot.left)/image->width,(slot.bottom-slot.top)/image->height),w=image->width*factor,h=image->height*factor;float x=(slot.left+slot.right-w)/2,y=(slot.top+slot.bottom-h)/2;target->DrawBitmap(bitmap,{x,y,x+w,y+h},opacity);}else Icon(L"favicon",slot,16,opacity);}
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
    struct TabSnapshot{std::wstring title;std::shared_ptr<const FaviconImage> favicon;float fill=Design::Surface::canvas;bool selected=false;float veil=0;};
    // Paper v2 tab title: a `TabStrip::titleFade` gradient into surface `c` ending at the close,
    // then solid surface under it, both at opacity `o` (the close's own reveal).
    void TitleVeil(Design::Box close,float titleRight,float c,float o){
        const float edge=close.left,start=edge-Design::TabStrip::titleFade;
        ID2D1GradientStopCollection* stops=nullptr;ID2D1LinearGradientBrush* gradient=nullptr;
        D2D1_GRADIENT_STOP g[]={{0,D2D1::ColorF(c,c,c,0)},{1,D2D1::ColorF(c,c,c,o)}};
        if(SUCCEEDED(target->CreateGradientStopCollection(g,2,&stops))&&SUCCEEDED(target->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({start,0},{edge,0}),stops,&gradient)))target->FillRectangle(D2D1::RectF(start,close.top,edge,close.bottom),gradient);
        Drop(gradient);Drop(stops);
        brush->SetColor(D2D1::ColorF(c,c,c,o));target->FillRectangle(D2D1::RectF(edge,close.top,titleRight,close.bottom),brush);}
    std::map<unsigned,TabSnapshot> tabSnapshots; // what a closing tab looked like, drawn while it blurs out
    // #127 parity 3: the column's rows run the same model on the vertical axis (Sidebar::ShownRow); its own instance, its
    // own snapshots for the ghosts. The host asks both for frames (TabsNextWake) and settles / resets both.
    Motion::TabStripMotion sideMotion;Motion::TabLayoutFrame sideFrame;std::map<unsigned,TabSnapshot> sideSnapshots;
    // #127 motion spec (Designer) §1: opening at .25 s, the content fading in over 120 ms from 40 ms; the reflow after
    // closing on the bar at .3 s; reduced motion fades in 120 ms. The column also closes in two beats (content out in
    // 80 ms, then the row folds at .22 s). The top bar keeps its ghost blur on close (validated look).
    static Motion::TabMotionParams TopBarTabMotion(){Motion::TabMotionParams p;p.openResponse=.25f;p.reflowResponse=.30f;p.contentDelay=Motion::Milliseconds(40);p.contentFade=Motion::Milliseconds(120);p.reducedFade=Motion::Milliseconds(120);return p;}
    static Motion::TabMotionParams ColumnTabMotion(){auto p=TopBarTabMotion();p.exit=Motion::Milliseconds(80);p.exitThenCollapse=true;return p;}
    // The selected row's background is ONE object sliding between rows (spec §1 « Sélection »: position and size,
    // spring 1.0 / .25 s), never off on one row and on at the other; the text's colour and weight fade (120 ms).
    Motion::Spring selTop{.05f},selBottom{.05f},selLeft{.05f},selRight{.05f};unsigned selPlateKey=0;
    // Motion spec §2: while the column folds or unfolds, its pieces (bookmark tiles, « » », tools, profile, fold button,
    // Back / Forward / Reload, the address) glide from where they are to the new layout's places: a morph of position
    // and size on the fold's spring (.35 s), retargeted from the presented box. Outside a fold they sit on the layout
    // (snapped); reduced motion: at once. Hit regions always follow the layout. Host: sideMorphing during the fold.
    // Motion spec §1 « Réordonner »: while a row is lifted it grows to x1.02 (1.0 / .15 s) on #2A2A2A and follows the
    // pointer 1:1 from where it was grabbed; the other rows step aside (1.0 / .2 s) as soon as its centre passes their
    // middle (no insertion line); dropped, it goes to its place with the pointer's speed (.85 / .3 s, a slight bounce).
    // Per tab key, a vertical offset added to the laid-out row. Reduced motion: the drag stays 1:1, the rest jumps.
    static constexpr float liftScale=1.02f,liftResponse=.15f,stepResponse=.20f,dropResponse=.30f,dropDamping=.85f;
    std::map<unsigned,Motion::Spring> rowShift;Motion::Spring liftGrow{.0005f};int liftedRow=-1;
    struct RowDropState{unsigned key=0;float top=0,velocity=0;bool pending=false;} rowDrop;
    float RowShift(unsigned key,Motion::Time now)const{auto it=rowShift.find(key);return it==rowShift.end()?0.f:it->second.Value(now);}
    bool ReorderMoving(Motion::Time now)const{if(liftGrow.Moving(now))return true;for(auto& e:rowShift)if(e.second.Moving(now))return true;return false;}
    // The host, at the drop (before the list is reordered): the dropped row's last top and the pointer's vertical speed.
    void RowDropped(unsigned key,float top,float velocity,Motion::Time now){rowDrop={key,top,velocity,true};
        // The rows that stepped aside are re-laid out by the move: their offsets keep only what was still on its way.
        for(auto& e:rowShift){if(e.first==key)continue;const float v=e.second.Value(now),t=e.second.Target();e.second.Snap(v-t);e.second.Set(0,now,stepResponse,!motion||motion->ReducedMotion());}}
    struct MorphBox{Motion::Spring l{.05f},t{.05f},r{.05f},b{.05f};bool known=false;};std::map<int,MorphBox> sideMorph;bool sideMorphing=false;
    D2D1_RECT_F Morph(int key,const Sidebar::Box& to,Motion::Time now){auto& m=sideMorph[key];const bool reduced=!motion||motion->ReducedMotion();
        if(!m.known||!sideMorphing||reduced){m.l.Snap(to.left);m.t.Snap(to.top);m.r.Snap(to.right);m.b.Snap(to.bottom);m.known=true;}
        else{const float k=(float)Sidebar::Timing::foldResponse;m.l.Set(to.left,now,k);m.t.Set(to.top,now,k);m.r.Set(to.right,now,k);m.b.Set(to.bottom,now,k);}
        return {m.l.Value(now),m.t.Value(now),m.r.Value(now),m.b.Value(now)};}
    bool MorphMoving(Motion::Time now)const{for(auto& e:sideMorph){const auto& m=e.second;if(m.l.Moving(now)||m.t.Moving(now)||m.r.Moving(now)||m.b.Moving(now))return true;}return false;}
    // Motion spec §4 « Repère de vue partagée » (253L-0): a reorder outside a drag (a split view made from the menu or by a
    // row dropped on the page, the keyboard moves) slides the moved rows from their former slots (1.0 / .3 s, rowShift);
    // a new pair's frame grows from the active row to the block (size morph, .3 s), the 10 x 1 line fades in (120 ms)
    // once they touch; separation: the line fades out, the frame shrinks back to that row. The split menu's outline
    // fades in / out (100 ms). Reduced motion: the geometry snaps, the fades stay.
    static constexpr float splitResponse=.30f;static constexpr int splitLineMs=120,menuOutlineMs=100;
    struct SplitMark{Motion::Spring l{.05f},t{.05f},r{.05f},b{.05f};Motion::Tween line;unsigned anchor=0;float full=0,row=0;bool born=false,leaving=false;};
    std::map<int,SplitMark> splitMarks;bool splitMarksPrimed=false;std::map<unsigned,Motion::Tween> menuOutlines;std::vector<unsigned> sideKeysSeen;
    bool SplitMarksMoving(Motion::Time now)const{for(auto& e:splitMarks){const auto& m=e.second;if(m.l.Moving(now)||m.t.Moving(now)||m.r.Moving(now)||m.b.Moving(now)||m.line.Moving(now))return true;}
        for(auto& e:menuOutlines){if(e.second.Moving(now))return true;}return false;}
    static constexpr float selectionResponse=.25f;
    bool PlateMoving(Motion::Time now)const{return selTop.Moving(now)||selBottom.Moving(now)||selLeft.Moving(now)||selRight.Moving(now);}
    Motion::Wake TabsNextWake(Motion::Time now)const{auto w=Motion::Earlier(tabMotion.NextWake(now),sideMotion.NextWake(now));return PlateMoving(now)||ReorderMoving(now)||SplitMarksMoving(now)?Motion::Earlier(w,Motion::Frame()):w;}
    void SettleTabs(){tabMotion.Settle();sideMotion.Settle();selTop.Settle();selBottom.Settle();selLeft.Settle();selRight.Settle();}
    void SetTabsReducedMotion(bool reduced){tabMotion.SetReducedMotion(reduced);sideMotion.SetReducedMotion(reduced);}
    void ResetTabMotion(){tabMotion.Reset();sideMotion.Reset();sideFrame={};sideSnapshots.clear();selPlateKey=0;splitMarks.clear();splitMarksPrimed=false;menuOutlines.clear();sideKeysSeen.clear();}   // a layout switch: no stale open / close
    void SyncSideMotion(int count,Motion::Time now){
        const auto m=Sidebar::Rows(side.mode);const float pitch=m.row+m.gap;
        // The model's content is the rows alone: its view leaves out the lead and « + Nouvel onglet », so its scroll range
        // is the column's (Layout::maxScroll).
        const float view=side.tabsView.Height()-m.lead-m.row;
        if(!TabKeysValid(count)){sideFrame={};sideFrame.tab=m.row;sideFrame.scroll=side.scroll;sideFrame.view=view;for(int i=0;i<count;i++){Motion::TabItem it;it.key=TabKey(i);it.index=i;it.slot=pitch*i;sideFrame.items.push_back(it);}sideFrame.content=pitch*count;return;}
        sideMotion.SetParams(ColumnTabMotion());
        if(sideMotion.ReducedMotion()!=motion->ReducedMotion())sideMotion.SetReducedMotion(motion->ReducedMotion());
        if(sideMotion.Sync(tabKeys,{m.row,side.scroll,view,true},m.gap,now,false))   /* overflow: the model honours the list's own view */tabMotionStarted=true;
        sideFrame=sideMotion.Sample(now);
        // §4: the tabs present before and after in another order (not a drop: RowDropped throws that row itself) start from
        // their former slot (opening / closing shift indices, not this order: the model slides those).
        if(!sideKeysSeen.empty()&&sideKeysSeen!=tabKeys&&!rowDrop.pending&&sidebarDrag<0){
            const std::set<unsigned> fresh(tabKeys.begin(),tabKeys.end()),old(sideKeysSeen.begin(),sideKeysSeen.end());
            std::vector<unsigned> a,b;for(unsigned k:sideKeysSeen)if(fresh.count(k))a.push_back(k);for(unsigned k:tabKeys)if(old.count(k))b.push_back(k);
            if(a!=b){std::map<unsigned,int> rank;for(int i=0;i<(int)a.size();++i)rank[a[i]]=i;const bool reduced=motion->ReducedMotion();
                for(int i=0;i<(int)b.size();++i){const int d=rank[b[i]]-i;if(!d||reduced)continue;auto& sp=rowShift[b[i]];const float v=sp.Value(now);sp.Snap(v+d*pitch);sp.Set(0,now,splitResponse,false);tabMotionStarted=true;}}}
        sideKeysSeen=tabKeys;
    }
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
        tabMotion.SetParams(TopBarTabMotion());
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
        if(snap.veil>0&&strip.Titled()&&strip.HasClose())TitleVeil(strip.CloseIn(box),title.right,snap.fill,snap.veil);
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
    int menuSource=-1;             // #127: the right-clicked tab while a split-menu row designates a target (the column outlines both)
    struct SplitDecor{bool shown=false,ring=true;D2D1_RECT_F active{};std::vector<D2D1_RECT_F> rails;std::vector<int> railTabs;std::vector<D2D1_RECT_F> handles;int hotHandle=-1,focusedHandle=-1;bool paintHandles=true,activeBottomLeft=false,activeBottomRight=false,activeTopLeft=false,activeTopRight=false;} split;
    UINT dpi=96;float width=1180,height=815;D2D1_RECT_F bar{},edit{};std::vector<Region> regions;
    // Fullscreen (F11 or a page's element): the page takes the whole window. Nothing of the chrome is drawn, hit,
    // focused, given a tooltip or exposed to UI Automation (all of these read `regions`); Paint only clears the canvas.
    bool contentOnly=false;
    // Mode Focus (src/focus-mode.h, Paper p-U-0): the chrome floats in a panel over the top of the page. `width` is then
    // the chrome's layout width (the client's minus FocusMode::layoutInset) and `clientWidth` the window's; `origin` is
    // where chrome-local (0, 0) lands in client DIP and `panel` the panel's client rectangle at its slide position
    // (bottom <= 0: off screen). Regions, `bar`, `edit` and every Design constant stay chrome-local: the host maps them
    // with ToClient(), and Hit() / Local() take client DIP. Without `floating` the origin is (0, 0) and nothing changes.
    bool floating=false;D2D1_POINT_2F origin{0,0};D2D1_RECT_F panel{};float clientWidth=0;
    // The window paints the panel itself only while it is « live » (at rest, in the page windows' hole); while it slides,
    // the host shows a picture of it (RenderPanel) in a layered window above the page instead.
    bool panelLive=true;
    // With the annotation mode (src/focus-mode.h « With the annotation mode »): the banner above the panel and the chat
    // card's strip on its right (the chrome's layout width shrinks by it). Set by the host before Resize().
    FocusMode::Above panelAbove{};
    float ClientWidth()const{return floating?clientWidth:width;}
    void SetFloating(bool on){if(on==floating)return;if(on){clientWidth=width;width=std::max(0.f,clientWidth-FocusMode::layoutInset-panelAbove.reserve);}else{width=clientWidth;origin={0,0};panel={};}floating=on;}
    void SetPanelAbove(FocusMode::Above a){panelAbove=a;if(floating)width=std::max(0.f,clientWidth-FocusMode::layoutInset-a.reserve);}
    // The panel's part inside the window (nothing above the client's top edge, nor over the annotation banner); empty when
    // off screen.
    D2D1_RECT_F PanelVisible()const{const float top=std::max(0.f,panelAbove.banner);if(!floating||panel.bottom<=top||panel.right<=panel.left)return {};return {panel.left,std::max(top,panel.top),panel.right,panel.bottom};}
    bool PanelShown()const{const auto v=PanelVisible();return v.bottom>v.top;}
    // Client DIP -> chrome-local; false where no chrome is on screen (floating: outside the panel's visible part).
    bool Local(float& x,float& y)const{if(!floating)return true;const auto v=PanelVisible();if(!(x>=v.left&&x<v.right&&y>=v.top&&y<v.bottom))return false;x-=origin.x;y-=origin.y;return true;}
    D2D1_RECT_F ToClient(D2D1_RECT_F r)const{return {r.left+origin.x,r.top+origin.y,r.right+origin.x,r.bottom+origin.y};}
    // The panel at slide `position` (0 hidden .. 1 at rest), its edges and the content's origin on whole device pixels:
    // the same rounding as the page windows, so the hole made in them and the painted panel meet exactly, and the
    // chrome inside rasterises like the normal one (an integer pixel offset).
    struct PanelPlace{D2D1_RECT_F panel;D2D1_POINT_2F origin;};
    PanelPlace PanelAt(float position)const{namespace FM=FocusMode;const float s=dpi/96.f;const auto r=FM::Panel(ClientWidth(),bandShown,position,panelAbove);
        const float top=FM::Snap(r.top,s),h=FM::Snap(FM::PanelHeight(bandShown),s);
        return {{FM::Snap(r.left,s),top,FM::Snap(r.right,s),top+h},{FM::Snap(r.left+FM::border,s),top+FM::Snap(FM::border,s)}};}
    void PlacePanel(float position){if(!floating)return;const auto p=PanelAt(position);panel=p.panel;origin=p.origin;}
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
    // i18n-exempt-begin: start-up step names, written to the log only (startup_graphics_failed)
    // The step InitResources / Resize last attempted (the host logs it with the HRESULT when the start fails).
    const wchar_t* initStep=L"";
    HRESULT InitResources(const std::wstring& root){assets=root+L"\\assets\\";InitLottie();initStep=L"Direct2D factory";HRESULT hr=D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,&factory);if(FAILED(hr))return hr;
        initStep=L"DirectWrite factory";hr=DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),(IUnknown**)&write);if(FAILED(hr))return hr;
        initStep=L"font loader";fontLoader=new FontLoader(assets+L"fonts\\");hr=write->RegisterFontCollectionLoader(fontLoader);if(FAILED(hr))return hr;const UINT key=1;initStep=L"font collection (assets\\fonts)";hr=write->CreateCustomFontCollection(fontLoader,&key,sizeof(key),&fonts);if(FAILED(hr))return hr;
        initStep=L"Inter Display font";for(auto family:{L"Inter Display"}){UINT32 index;BOOL exists=FALSE;hr=fonts->FindFamilyName(family,&index,&exists);if(FAILED(hr)||!exists)return FAILED(hr)?hr:E_FAIL;}
        initStep=L"WIC imaging factory (COM)";hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic));return hr;
    }
    // i18n-exempt-end
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
        if(floating){clientWidth=width;width=std::max(0.f,clientWidth-FocusMode::layoutInset-panelAbove.reserve);}
        if(target){target->SetDpi((float)dpi,(float)dpi);return hwndTarget->Resize(D2D1::SizeU(r.right,r.bottom));}
        initStep=L"window render target" /* i18n-exempt: log only */;HRESULT hr=factory->CreateHwndRenderTarget(D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,D2D1::PixelFormat(),(float)dpi,(float)dpi),D2D1::HwndRenderTargetProperties(hwnd,D2D1::SizeU(r.right,r.bottom),presentOptions),&hwndTarget);if(FAILED(hr))return hr;target=hwndTarget;target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);initStep=L"brush";return target->CreateSolidColorBrush(D2D1::ColorF(1,1,1),&brush);
    }
    void Color(float c,float alpha=1){brush->SetColor(D2D1::ColorF(c,c,c,alpha));}
    // Docked drawers (NativeUI::KeyedDrawer): the strip a sliding drawer has not entered yet, [0, lead) DIP, in the
    // colour its layered window keys out. Aliased and pixel-exact (lead is a whole number of pixels).
    void KeyOut(float lead){if(lead<=0||!target||!brush)return;D2D1_MATRIX_3X2_F m;target->GetTransform(&m);target->SetTransform(D2D1::Matrix3x2F::Identity());
        const auto mode=target->GetAntialiasMode();target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);brush->SetColor(D2D1::ColorF(1,0,1,1));
        target->FillRectangle(D2D1::RectF(0,0,lead,target->GetSize().height+1),brush);target->SetAntialiasMode(mode);target->SetTransform(m);}
    // The same key colour over a GDI region (target pixels), rectangle by rectangle, aliased: the keyed-out silhouette is
    // exactly the region's, pixel for pixel (the downloads drawer's morph: what its growing shape does not cover yet).
    void KeyOutRegion(HRGN region){if(!region||!target||!brush)return;const DWORD size=GetRegionData(region,0,nullptr);if(size<sizeof(RGNDATAHEADER))return;
        std::vector<BYTE> data(size);auto* rgn=reinterpret_cast<RGNDATA*>(data.data());if(GetRegionData(region,size,rgn)!=size)return;
        float dx=96,dy=96;target->GetDpi(&dx,&dy);const float kx=96.f/dx,ky=96.f/dy;
        D2D1_MATRIX_3X2_F m;target->GetTransform(&m);target->SetTransform(D2D1::Matrix3x2F::Identity());const auto mode=target->GetAntialiasMode();target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);brush->SetColor(D2D1::ColorF(1,0,1,1));
        const RECT* r=reinterpret_cast<const RECT*>(rgn->Buffer);for(DWORD i=0;i<rgn->rdh.nCount;++i)target->FillRectangle(D2D1::RectF(r[i].left*kx,r[i].top*ky,r[i].right*kx,r[i].bottom*ky),brush);
        target->SetAntialiasMode(mode);target->SetTransform(m);}
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
        // Emoji: system colour font per cluster, Inter's line box kept (src/emoji-text.h).
        placement.emoji=EmojiText::Prepare(write,layout,value,fonts,L"Inter Display");
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
            if(textCache.size()>=textCacheLimit){
                // Never painted through Paint(), or a runaway (a pass measures at most every bar item + the rest): as before.
                if(!textPass||textCache.size()>=4*textCacheLimit+2*BookmarkLimit)ClearTextCache();
                else if(textEvictedPass!=textPass){textEvictedPass=textPass;for(auto e=textCache.begin();e!=textCache.end();){if(e->second.used!=textPass){Drop(e->second.layout);e=textCache.erase(e);}else ++e;}}
            }
            TextEntry entry;entry.used=textPass;const D2D1_RECT_F origin{0,0,slot.right-slot.left,slot.bottom-slot.top};
            HRESULT hr=MakeTextLayout(value,origin,size,tabular,medium,tracking,&entry.layout,entry.measure);if(FAILED(hr))return hr;
            it=textCache.emplace(std::move(key),entry).first;
        }
        it->second.used=textPass;
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
        target->PushAxisAlignedClip(clip,D2D1_ANTIALIAS_MODE_ALIASED);
        EmojiText::Draw(target,D2D1::Point2F(slot.left,m.originY),layout,brush,D2D1::ColorF(ink.r,ink.g,ink.b,opacity),m.emoji);
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
    // The address field's outline while it has the focus (Val #5, Paper « #FFFFFF0D, 1 px inside »): white 5 % after a
    // click, in the three layouts; reached from the keyboard (Ctrl+L, F6, Tab) it keeps the focus ring's 50 % (WCAG 2.4.7).
    static constexpr float addressRing=0x0D/255.f;
    void AddressRing(D2D1_RECT_F box,float radius){const float a=keyboardModality?Design::Focus::keyboardOpacity:addressRing;Color(1,a);
        target->DrawRoundedRectangle(D2D1::RoundedRect({box.left+.5f,box.top+.5f,box.right-.5f,box.bottom-.5f},radius-.5f,radius-.5f),brush,1);}
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
        if(back){const float x=box.right+T::pairGap,y=(box.top+box.bottom)/2;Color(1,T::separatorOpacity);target->FillRoundedRectangle(D2D1::RoundedRect({x,y-T::separatorHeight/2,x+T::separatorWidth,y+T::separatorHeight/2},.5f,.5f),brush);}
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
    bool annotateDimmed=false;   // #127 column: the mode cannot start on this page (greyed, still shown, its tooltip says why; #125: live sites)
    const wchar_t* AnnotateTip() const { return annotateTip.empty() ? TR(chrome_tip_annotate) : annotateTip.c_str(); }
    // #125 E1a: on a live site the button is greyed but stays enabled (its click opens the E1b card): `annotateOff`.
    bool annotateOff=false;
    // UIA HelpText of 'N': how to use the mode when it can start, else why not (the tooltip, E1a's sentence on a live site).
    std::wstring AnnotateHelp() const { return Enabled('N')&&!annotateOff ? std::wstring(TR(a11y_annotate_help)) : std::wstring(AnnotateTip()); }
    std::function<void(ID2D1RenderTarget*,ID2D1Factory*,ID2D1SolidColorBrush*,D2D1_RECT_F,Motion::InteractionVisual,bool focus)> paintAnnotate;
    float AnnotateRight()const{return (audioShown?AudioLeft:97.f)+6;}   // from the right edge: 6 DIP left of Audio/History
    float ContentTop()const{if(sidebar&&!contentOnly)return side.bar+annotationBand;   /* #127: 40 over the complete column, 32 over the compact one */return Design::Vertical::ContentTop(bandShown)+annotationBand;}
    D2D1_RECT_F BandRect(int i)const{if(sidebar){if(sidebarPopover&&i>=0&&i<(int)sidePop.grid.tiles.size())return R(sidePop.grid.tiles[i]);const auto& L=SideColumn();if(i>=0&&i<SideBookmarkTiles(L))return R(SideBookmarkTile(L,i));return {};}if(i<0||i>=(int)band.left.size())return {};return {band.left[i],BookmarksBar::Layout::top,band.left[i]+band.width[i],BookmarksBar::Layout::bottom};}
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
    D2D1_RECT_F AddressSlot()const{const float c=AddressCenter(),l=bar.left+addressSlotInset;return {l,c-addressSlotSize/2,l+addressSlotSize,c+addressSlotSize/2};}
    float ChipWidth(const wchar_t* label){namespace A=Design::Address;if(sideChip)return 2*SideChip::pad+SideChip::icon+SideChip::gap+TextWidth(label,SideChip::font,false,false,0);return A::chipPadLeft+Design::Size::icon+Design::Space::xs+TextWidth(label,Design::Type::label.size,true,false,Design::Type::label.tracking)+A::chipPadRight;}
    // Hit/tooltip box of the indicator: the icon slot, or the whole chip. None while editing.
    D2D1_RECT_F IndicatorBox(){if(addressEditing||address.security==AddressBar::Security::Empty)return {};if(address.security==AddressBar::Security::Local)return LocalPillBox();auto box=AddressSlot();const wchar_t* chip=address.chip();
        if(*chip&&sideChip){const float cy=AddressCenter(),l=bar.left+sideChipInset;return {l,cy-SideChip::height/2,l+ChipWidth(chip),cy+SideChip::height/2};}
        if(*chip){box.right=box.left+ChipWidth(chip);}return box;}
    // #127 side panel layouts (Paper « Annotation · 1 à 7 (validées) » 1ZTU-0 / 1ZU8-0): the « Non sécurisé » pill is 20 high,
    // padding 5, gap 4, padlock 14 at 50 %, label 13/16 Regular white 70 %, #FFFFFF0F radius 4; 6 DIP in from the column's
    // field (10 in the compact bar's), then 6, the 1 x 10 divider, 6, the address.
    struct SideChip{static constexpr float height=20,pad=5,icon=14,gap=4,font=13,text=.7f,divider=6,columnInset=6;};
    bool sideChip=false;float sideChipInset=0;
    // #127 « Local » pill (Design::Address::LocalPill, AddressBar::Security::Local): its width and its box in the field
    // (6 after the field's edge, 20 high, centred).
    float LocalPillWidth(){namespace A=Design::Address;return A::localPad+A::localIcon+A::localGap+std::ceil(TextWidth(TR(address_local),A::localText))+A::localPad;}   // whole DIP, as Paper's 59
    D2D1_RECT_F LocalPillBox(){namespace A=Design::Address;const float c=AddressCenter(),l=bar.left+(sideChip?sideChipInset:A::localLead);return {l,c-A::localHeight/2,l+LocalPillWidth(),c+A::localHeight/2};}
    // The profile's ink, as the profile button shows it (its target colour); without a profile button, the neutral grey.
    ProfileColor::Rgb8 LocalInk()const{const auto& b=profileButton;if(!b.shown)return ProfileColor::Neutral();
        auto byte=[](float v){return static_cast<std::uint8_t>(std::lround(std::clamp(v,0.f,1.f)*255));};return {byte(b.ink.r),byte(b.ink.g),byte(b.ink.b)};}
    static D2D1_COLOR_F Solid(ProfileColor::Rgb8 c){return D2D1::ColorF(c.r/255.f,c.g/255.f,c.b/255.f,1);}
    // The pill in `box` with one colour set: LocalPill::For(LocalInk(),true) in the top bar's #292929 field (DrawAddress);
    // #127's compact bar (no field, on #171717) calls it with LocalPill::For(LocalInk(),false) and draws its divider in
    // that set's `divider`.
    void DrawLocalPill(D2D1_RECT_F box,bool hover,const LocalPill::Colors& c){
        namespace A=Design::Address;const float cy=(box.top+box.bottom)/2;
        FillBox(box,A::localRadius,Solid(hover?c.hover:c.plate));
        const D2D1_RECT_F glyph{box.left+A::localPad,cy-A::localIcon/2,box.left+A::localPad+A::localIcon,cy+A::localIcon/2};
        PathGlyph(A::monitorPath,glyph,A::localIcon,Solid(c.ink));
        ink=Solid(c.ink);Text(TR(address_local),{glyph.right+A::localGap,cy-16,box.right,cy+16},A::localText,1);ink=D2D1::ColorF(1,1,1,1);
    }
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
    D2D1_RECT_F RequestBox(){if(requestGlyphs.empty())return {};const auto i=IndicatorBox();if(i.right<=i.left)return {};namespace L=SiteRequest::Layout;const auto pill=VaultPillBox();const float c=AddressCenter(),l=pill.right>pill.left?pill.right+VaultPillLayout::requestGap:i.right+L::lead;return {l,c-L::height/2,l+SiteRequest::ChipWidth(requestGlyphs.size(),requestState),c+L::height/2};}
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
    // B03 native key control, #121 point 4 variant C (Paper « Clé dans la barre d'adresse · A / B / C », C chosen by the
    // owner): a pill right after the padlock with the number of saved accounts, the B01 pill's look (get_jsx 1MI5-0):
    // #FFFFFF14, radius 4, height 24, padding 6 / 7, gap 4, key 14 #FFFFFFCC, count Inter 13/16 Medium #FFFFFFCC; 2 DIP
    // after the padlock slot, the URL 8 DIP after it (4 group padding + 4 gap). It is chrome (a page cannot draw here,
    // review P15); what it opens and its arming are unchanged (review §5). States (board « Listes sous le champ + bulle ·
    // états », 1OE3-0): hovered, #FFFFFF24 with the key and the number in white (the same outline key, Phosphor Bold:
    // owner decision 24 November); with a B01 request pill,
    // the key first, the request pill 4 DIP after it (margin 2 + gap 2).
    bool vaultKey=false;int vaultKeyCount=0;static constexpr int VaultKeyId=906;   // not a character id: 'W' is Ctrl+W (close tab)
    static constexpr const char* VaultKeyGlyph="M160,12A84.09,84.09,0,0,0,79.43,119.53L23.51,175.51A12,12,0,0,0,20,184v40a12,12,0,0,0,12,12H72a12,12,0,0,0,12-12V212H96a12,12,0,0,0,12-12V188h12a12,12,0,0,0,8.49-3.51l7.93-7.94A84,84,0,1,0,160,12Zm0,144a59.66,59.66,0,0,1-24.43-5.18,12,12,0,0,0-13.48,2.45L115,164H96a12,12,0,0,0-12,12v12H72a12,12,0,0,0-12,12v12H44V189l58.73-58.73a12,12,0,0,0,2.45-13.48A60,60,0,1,1,160,156Zm36-76a16,16,0,1,1-16-16A16,16,0,0,1,196,80Z";
    struct VaultPillLayout{static constexpr float lead=2,height=24,radius=4,padLeft=6,padRight=7,icon=14,gap=4,textSize=13,trail=8,requestGap=4,plate=0x14/255.f,hoverPlate=0x24/255.f,ink=0xCC/255.f,hoverInk=1;};
    std::wstring VaultKeyCountText()const{return std::to_wstring(std::max(1,vaultKeyCount));}
    // Shown whenever the host says so (VaultKeyVisible), as the key was before #121: also after a leading chip (« Non
    // sécurisé » on a page whose certificate error was accepted, #127 « Local » on a project of this computer), right after
    // its plate (2 DIP, as after the padlock; not drawn by a board next to « Local »). Hidden only while the address is
    // edited (the EDIT covers that lane; the indicator is gone too).
    D2D1_RECT_F VaultPillBox(){using P=VaultPillLayout;if(!vaultKey||addressEditing||address.security==AddressBar::Security::Empty)return {};const auto i=IndicatorBox();if(i.right<=i.left)return {};
        const float c=AddressCenter(),l=i.right+P::lead,w=P::padLeft+P::icon+P::gap+TextWidth(VaultKeyCountText(),P::textSize,true)+P::padRight;return {l,c-P::height/2,l+w,c+P::height/2};}
    void DrawVaultKey(D2D1_RECT_F box,float hover,bool focused){
        using P=VaultPillLayout;const float h=std::clamp(hover,0.f,1.f),ink=P::ink+(P::hoverInk-P::ink)*h;
        FillBox(box,P::radius,D2D1::ColorF(1,1,1,P::plate+(P::hoverPlate-P::plate)*h));
        if(focused)FocusRing(box,P::radius);
        const float cy=(box.top+box.bottom)/2,gx=box.left+P::padLeft;
        PathGlyph(VaultKeyGlyph,{gx,cy-P::icon/2,gx+P::icon,cy+P::icon/2},P::icon,D2D1::ColorF(1,1,1,ink));
        Text(VaultKeyCountText(),{gx+P::icon+P::gap,box.top,box.right,box.bottom},P::textSize,ink,false,true,0);
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
    float profileRest=Design::ProfileButton::rest;
    void DrawProfileButton(int hover,int focus,Motion::Time now,const D2D1_RECT_F* at=nullptr){namespace PB=Design::ProfileButton;
        const auto v=motion?motion->Visual(900,now):Motion::InteractionVisual{hover==900?1.f:0.f,0};const bool reduced=motion&&motion->ReducedMotion();
        const float press=std::max(v.press,profileButton.open?1.f:0.f),h=Motion::Clamp(v.hover);
        const float tint=profileRest+(PB::hover-profileRest)*h*(1-press)+(PB::pressed-profileRest)*press;   // #127 compact: no plate at rest
        const auto p=at?Design::Box{at->left,at->top,at->right,at->bottom}:PB::Plate();const float k=1-(1-PB::pressScale)*(reduced?0.f:press),cx=(p.left+p.right)/2,cy=(p.top+p.bottom)/2,half=PB::size/2*k;
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
        const auto slot=AddressSlot();const float right=std::min(edit.right,addressTextRight),cy=AddressCenter();
        const D2D1_RECT_F line{0,bar.top,0,bar.bottom};
        if(editing){if(!DrawSiteImage(editFavicon,slot)){if(!editIcon&&address.security==Security::Local)PathGlyph(A::monitorPath,slot,sidebar?addressGlyph:A::localIcon,D2D1::ColorF(1,1,1,addressGlyphOpacity));else Icon(editIcon?editIcon:address.icon(),slot,addressGlyph,addressGlyphOpacity);}return;}
        if(address.security==Security::Empty){Icon(L"search",slot,addressGlyph,addressGlyphOpacity);
            Text(TR(chrome_address_placeholder),{bar.left+addressTextInset,line.top,right,line.bottom},addressFont,Design::Text::address,false,Design::Type::address.medium,AddressTracking());return;}
        const bool danger=address.security==Security::CertificateError;
        float x=bar.left+addressTextInset;
        if(address.security==Security::Local){
            // Paper (« Barre du haut »; #127 « Adresse · pastille « Local » » 1ZWP-0: the column's field the same, the compact bar
            // on #171717 its « no field » colours): pill in the profile's colours, 6, the 1 × 10 divider, 6, the address.
            const auto pill=LocalPillBox();const auto c=LocalPill::For(LocalInk(),!sidebar||addressHostOnly);DrawLocalPill(pill,indicatorHover,c);
            float edge=pill.right;{const auto key=VaultPillBox();if(key.right>key.left)edge=std::ceil(key.right);}   // #121: the key pill follows « Local » (drawn with the regions), the divider after it, on a whole DIP as after « Local »
            if(request.right>request.left){DrawRequestChip(request);edge=request.right;}
            const float d=edge+A::localDivider;brush->SetColor(Solid(c.divider));target->FillRectangle(D2D1::RectF(d,cy-5,d+1,cy+5),brush);
            x=d+1+A::localDivider;
        }else if(const wchar_t* chip=address.chip();*chip){
            D2D1_RECT_F plate{slot.left,slot.top,slot.left+ChipWidth(chip),slot.bottom};if(sideChip)plate=IndicatorBox();
            const float lift=indicatorHover?.04f:0;
            FillBox(plate,A::chipRadius,danger?DangerColor(Design::Danger::plate+lift):D2D1::ColorF(1,1,1,A::chipPlate+lift));
            D2D1_RECT_F glyph{plate.left+A::chipPadLeft,cy-Design::Size::icon/2,plate.left+A::chipPadLeft+Design::Size::icon,cy+Design::Size::icon/2};
            if(sideChip)glyph={plate.left+SideChip::pad,cy-SideChip::icon/2,plate.left+SideChip::pad+SideChip::icon,cy+SideChip::icon/2};
            if(danger)TintedIcon(address.icon(),glyph,A::chipIcon,DangerColor());else Icon(address.icon(),glyph,A::chipIcon,A::secondary);
            if(danger)ink=DangerColor();
            if(sideChip)Text(chip,{glyph.right+SideChip::gap,line.top,plate.right,line.bottom},SideChip::font,danger?1.f:SideChip::text,false,false,0);
            else Text(chip,{glyph.right+Design::Space::xs,line.top,plate.right,line.bottom},Design::Type::label.size,danger?1.f:A::secondary,false,true,Design::Type::label.tracking);
            ink=D2D1::ColorF(1,1,1,1);
            float edge=plate.right;{const auto pill=VaultPillBox();if(pill.right>pill.left)edge=std::ceil(pill.right);}   // #121: the key pill follows the chip (drawn with the regions); divider on a whole DIP
            if(request.right>request.left){DrawRequestChip(request);edge=request.right;}
            const float gap=sideChip?SideChip::divider:A::divider,d=edge+gap;Color(1,.2f);target->FillRectangle(D2D1::RectF(d,cy-5,d+1,cy+5),brush);
            x=d+1+gap;
        }else{
            if(indicatorHover)FillBox(slot,A::chipRadius,D2D1::ColorF(1,1,1,A::chipPlate));
            Icon(address.icon(),slot,addressGlyph,addressGlyphOpacity);
            // B01: lock (50 %) stays in its slot; the request chip follows, the URL moves past it.
            // #121 C: the key pill comes first (drawn with the regions), the request chip after it.
            const auto pill=VaultPillBox();const float pillShift=pill.right>pill.left?pill.right-pill.left+VaultPillLayout::requestGap:0;
            if(pill.right>pill.left)x=pill.right+VaultPillLayout::trail;
            if(request.right>request.left){DrawRequestChip(request);x=requestShift>=0?bar.left+addressTextInset+pillShift+requestShift:request.right+SiteRequest::Layout::trail+Design::Space::xs;}
        }
        // Runs share one baseline; the overflowing tail is clipped under a fade, never ellipsised.
        target->PushAxisAlignedClip(D2D1::RectF(x,line.top,right,line.bottom),D2D1_ANTIALIAS_MODE_ALIASED);
        const float start=x;
        bool hostSeen=false;
        for(auto& run:address.runs){if(x>=right)break;if(addressHostOnly){if(run.tone==Tone::Primary)hostSeen=true;else if(hostSeen)break;}
            const float w=TextWidth(run.text,addressFont,Design::Type::address.medium,false,AddressTracking());
            const float opacity=run.tone==Tone::Primary?A::primary:run.tone==Tone::Danger?1.f:A::secondary;
            if(run.tone==Tone::Danger)ink=DangerColor();
            Text(run.text,{x,line.top,x+w+1,line.bottom},addressFont,opacity,false,Design::Type::address.medium,AddressTracking());
            if(run.strike){auto m=MeasureText(L"x",{x,line.top,x+w+1,line.bottom},addressFont);const float y=std::round(m.originY+m.baseline-addressFont*.27f);
                brush->SetColor(ink);target->FillRectangle(D2D1::RectF(x,y,x+w,y+1),brush);}
            ink=D2D1::ColorF(1,1,1,1);x+=w;
        }
        target->PopAxisAlignedClip();
        if(x>right&&start<right-A::fade){
            if(!addressFadeBrush){const float c=addressFadeSurface;D2D1_GRADIENT_STOP stops[]={{0,D2D1::ColorF(c,c,c,0)},{1,D2D1::ColorF(c,c,c,1)}};
                if(FAILED(target->CreateGradientStopCollection(stops,2,&addressFadeStops))||FAILED(target->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({0,0},{1,0}),addressFadeStops,&addressFadeBrush)))DropAddressFade();}
            if(addressFadeBrush){addressFadeBrush->SetStartPoint({right-A::fade,0});addressFadeBrush->SetEndPoint({right,0});target->FillRectangle(D2D1::RectF(right-A::fade,bar.top+1,right,bar.bottom-1),addressFadeBrush);}
        }
    }
    // ---- Panneau latéral (#127, variante A finale; geometry and Paper values in src/sidebar.h) --------------------
    // One column on the left replaces the tab row and the toolbar. Regions keep the toolbar's command ids ('B' 'F' 'R'
    // 'L' 'C' 'K' 'N' 'V' 'Y' 'D' 'S', 900, tabs, closes, bookmarks, 'O' 'E' 'T', the window buttons), so the host's
    // commands, menus, focus order and UI Automation work unchanged; only the places change.
    bool sidebar=false;                                  // variant A: the browser turns it on (src/main.cpp, wWinMain)
    Sidebar::Mode sidebarMode=Sidebar::Mode::Complete;   // effective mode (a narrow window is compact)
    bool sidebarOverlay=false;                           // compact: the complete column open over the page (03)
    float sidebarColumn=Sidebar::Complete::width;        // the column's drawn width (fold animation)
    float sidebarWidth=Sidebar::Complete::width;         // the complete column's chosen width (Resize: 220 .. 360)
    // The column's edge (Sidebar::Resize): 0 rest, 1 hovered, 2 dragged (the line along the page's left edge); the handle
    // exists while the host allows it (docked, settled, the window wide enough for the chosen layout).
    int sidebarEdge=0;bool sidebarResizable=false;static constexpr int SidebarEdgeId=909;std::wstring edgeTip;
    // Motion spec §5: the hover line fades in over 120 ms once the pointer has rested 150 ms (dragging: at once).
    Motion::Time edgeHoverAt{};
    float EdgeAlpha(Motion::Time now)const{if(sidebarEdge==2)return 1;if(sidebarEdge!=1)return 0;const double ms=std::chrono::duration<double,std::milli>(now-edgeHoverAt).count()-150;return (float)std::clamp(ms/120,0.0,1.0);}
    float sidebarTitles=1;                               // titles and labels (fold animation)
    float sidebarScroll=0;                               // tab list scroll
    int sidebarDrag=-1;float sidebarDragY=0,sidebarDragTopAtDrop=0;int sidebarInsert=-1;   // tab dragged in the list: its row, pointer top, drop slot
    std::vector<std::wstring> sideDetails;               // host: per tab, the tooltip's second line (domain)
    std::vector<bool> sideHome;                          // host: per tab, the new-tab page (globe + « Nouvel onglet »)
    Sidebar::Layout side,sideOver;                       // last layouts: the docked column, the column over the page
    std::vector<int> sideTools;                          // footer buttons, left to right / top to bottom
    static constexpr int SidebarToggleId=907;
    bool SidebarOn()const{return sidebar&&!contentOnly;}
    std::wstring gridToggleTip;   // « Tous les favoris (N) » / « Réduire » (UI Automation name; the row shows it)
    const Sidebar::Layout& SideColumn()const{return sidebarOverlay?sideOver:side;}
    static D2D1_RECT_F R(const Sidebar::Box& b){return {b.left,b.top,b.right,b.bottom};}
    // The page zone's left edge while the column docks (the fold animation moves it).
    float SidebarZoneLeft()const{return sideHidden?Sidebar::Zone::inset:sidebarColumn;}
    // Mode Focus in the side layouts (src/sidebar.h FocusSide): the column is hidden (the page takes the window); it comes
    // back over the page, `sideShown` while it is there at rest (painted by the window, hit-tested; during its slide the
    // host shows a picture of it instead, RenderSideColumn).
    bool sideHidden=false,sideShown=false;
    // Bookmarks grid, variant B (Paper « Favoris · … (validée) »; the « » » + menu variant A is archived): the complete column
    // folds to two rows with « Tous les favoris (N) », unfolds to five rows then scrolls; the compact one keeps four and
    // « » », which opens every bookmark in a popover at its right. Host state: unfolded, both scrolls, the popover.
    bool sidebarGridOpen=false,sidebarPopover=false;float sidebarGridScroll=0,sidebarPopoverScroll=0;float sidebarReserve=0;
    Sidebar::PopoverLayout sidePop;
    static constexpr int SidebarGridToggleId=908,SidebarMoreId='O',SidebarManageId=910;   // 910: « Gérer les favoris » (grid or popover foot)   // « » » keeps the band overflow's id (its tile, its keyboard stop)
    Sidebar::Input SideInput(Sidebar::Mode m,int count,float zoneLeft=-1,bool compactZone=false)const{Sidebar::Input in;in.mode=m;in.zoneLeft=zoneLeft;in.width=width;in.height=height;
        if(compactZone){in.zoneSet=true;in.zoneMode=Sidebar::Mode::Compact;}
        in.grid=bandShown;in.bookmarks=(int)bandItems.size();in.gridOpen=sidebarGridOpen;in.gridScroll=sidebarGridScroll;in.tabs=count;in.tools=(int)std::max<size_t>(1,sideTools.size());in.scroll=sidebarScroll;in.band=annotationBand;in.reserve=sidebarReserve;in.hidden=sideHidden;in.top=sideHidden?std::max(0.f,annotationBand):0.f;in.panel=sidebarWidth;in.groups=tabGroups;return in;}
    // Visible bookmark tiles of the column (index i = bandItems[i]): complete, the grid's at its scroll; compact, the first
    // four (five when they all fit).
    int SideBookmarkTiles(const Sidebar::Layout& L)const{if(L.emptyGrid)return 0;if(L.mode==Sidebar::Mode::Complete)return (int)L.grid.tiles.size();return L.more?(int)L.tiles.size()-1:(int)L.tiles.size();}
    Sidebar::Box SideBookmarkTile(const Sidebar::Layout& L,int i)const{return L.mode==Sidebar::Mode::Complete?L.grid.tiles[i]:L.tiles[i];}
    // #127: the folder tile under (x, y) a dragged bookmark `dragged` could be filed in (grid, compact column, « » » popover), else -1.
    int SideFolderAt(float x,float y,unsigned dragged){if(!sidebar)return -1;const int hit=Hit(x,y);if(!IsBookmark(hit))return -1;const int i=BookmarkIndex(hit);
        return i<(int)bandItems.size()&&bandItems[i].folder&&bandItems[i].id!=dragged?i:-1;}
    void BuildSidebar(int count,int selected,const std::vector<std::wstring>* titles,Motion::Time now=Motion::Now()){
        namespace SB=Sidebar;
        sideTools.clear();if(annotateShown)sideTools.push_back('N');if(audioShown)sideTools.push_back('V');sideTools.push_back('Y');sideTools.push_back('D');sideTools.push_back('S');
        side=SB::Compute(SideInput(sidebarMode,count,sidebarColumn));sidebarScroll=side.scroll;   /* the page's edge is always the presented width (the host's ContentBox): one value */if(side.gridOpen||!side.grid.Scrolls())sidebarGridScroll=side.grid.scroll;SyncSideMotion(count,now);
        if(sidebarOverlay){sideOver=SB::Compute(SideInput(SB::Mode::Complete,count,side.column,true));sideOver.scroll=side.scroll;}
        // The compact « » » popover: all the bookmarks, its tiles first (it covers the column's right and the page).
        if(sidebarPopover&&!sidebarOverlay&&side.more&&side.mode==SB::Mode::Compact&&!side.tiles.empty()){sidePop=SB::ComputePopover(side.column,side.tiles.back(),(int)bandItems.size(),sidebarPopoverScroll,height);sidebarPopoverScroll=sidePop.grid.scroll;
            for(int i=0;i<(int)sidePop.grid.tiles.size()&&i<(int)bandItems.size()&&i<BookmarkLimit;++i){auto t=sidePop.grid.tiles[i];if(t.bottom<=sidePop.grid.view.top||t.top>=sidePop.grid.view.bottom)continue;t.top=std::max(t.top,sidePop.grid.view.top);t.bottom=std::min(t.bottom,sidePop.grid.view.bottom);regions.push_back({BookmarkBase+i,R(t),bandItems[i].tip.c_str(),false});}
            regions.push_back({SidebarManageId,R(sidePop.manage),nullptr});}
        else{sidebarPopover=false;sidePop={};}
        const auto& C=SideColumn();const bool full=C.mode==SB::Mode::Complete;
        tips.reserve(count);for(int i=0;i<count;i++){const std::wstring title=titles&&i<(int)titles->size()?(*titles)[i]:L"";std::wstring t=title.empty()?std::wstring(i<(int)sideHome.size()&&sideHome[i]?TR(tab_new):TR(chrome_tab_untitled_tip)):title;
            const std::wstring d=i<(int)sideDetails.size()?sideDetails[i]:L"";if(!full)t+=L"\n"+(d.empty()?std::wstring(TR(sidebar_middle_click_close)):TRF(sidebar_tab_detail,d));tips.push_back(t);}
        // Column (the one over the page first: it wins the hit test where it covers the compact column).
        brandTip=profileButton.shown?(profileButton.privateWindow?std::wstring(TR(chrome_tip_private_window)):TRF(chrome_tip_profile,profileButton.name)):TRF(chrome_tip_tab_list,std::to_wstring(count));
        regions.push_back({900,R(C.profile),brandTip.c_str()});
        if(full){regions.push_back({'B',R(C.back),TR(chrome_tip_back)});regions.push_back({'F',R(C.forward),TR(chrome_tip_forward)});regions.push_back({'R',R(C.reload),navigationLoading?TR(chrome_tip_stop):TR(chrome_tip_reload)});}
        regions.push_back({SidebarToggleId,R(C.toggle),sidebarMode==SB::Mode::Complete&&!sidebarOverlay?TR(sidebar_tip_collapse):TR(sidebar_tip_expand)});
        const auto& A=C.addressInColumn?C:side;   // the field: in the column (complete or over the page), else the window bar
        if(!C.addressInColumn){regions.push_back({'B',R(side.back),TR(chrome_tip_back)});regions.push_back({'F',R(side.forward),TR(chrome_tip_forward)});regions.push_back({'R',R(side.reload),navigationLoading?TR(chrome_tip_stop):TR(chrome_tip_reload)});}
        UseAddress(A);
        indicator=IndicatorBox();if(indicator.right>indicator.left)regions.push_back({'I',indicator,address.tip(),false});
        request=RequestBox();if(request.right>request.left&&requestLive)regions.push_back({'Q',request,requestTip.c_str(),false});
        const bool actions=A.addressInColumn&&!addressEditing;
        regions.push_back({'L',actions?D2D1_RECT_F{A.address.left,A.address.top,A.copy.left-SB::Complete::addressGap/2-(vaultKey?20.f:0.f),A.address.bottom}:R(A.address),TR(chrome_tip_address)});
        if(actions){const float cy=(A.address.top+A.address.bottom)/2,h=Design::Size::compact/2;
            if(vaultKey){const float x=A.copy.left-SB::Complete::addressGap-A.addressGlyph/2;regions.push_back({VaultKeyId,{x-h,cy-h,x+h,cy+h},TR(chrome_tip_fill_password)});}
            const float split=(A.copy.right+A.star.left)/2;
            regions.push_back({'C',{split-Design::Size::compact,cy-h,split,cy+h},TR(chrome_tip_copy_link)});
            regions.push_back({'K',{split,cy-h,split+Design::Size::compact,cy+h},starred?TR(chrome_tip_edit_bookmark):TR(chrome_tip_add_bookmark)});}
        // Bookmarks grid B (the band's items, ids and tips; BandRect reads the tiles): the complete grid's tiles clipped to its
        // viewport and its toggle row; the compact tiles and « » ». The band's keyboard stop is the first tile.
        {band={};const int shown=SideBookmarkTiles(C);band.visible=std::min(shown,(int)bandItems.size());
            const Sidebar::Box clip=full?C.grid.view:C.gridBlock;
            for(int i=0;i<band.visible&&i<BookmarkLimit;++i){if(sidebarPopover&&!full)break;auto t=SideBookmarkTile(C,i);if(t.bottom<=clip.top||t.top>=clip.bottom)continue;t.top=std::max(t.top,clip.top);t.bottom=std::min(t.bottom,clip.bottom);
                regions.push_back({BookmarkBase+i,R(t),bandItems[i].tip.c_str(),i==bandRover||(bandRover<0&&i==0)});}
            if(!full&&C.more){band.overflow=true;band.overflowLeft=C.tiles.back().left;regions.push_back({SidebarMoreId,R(C.tiles.back()),TR(sidebar_all_bookmarks_tip),bandRover<0&&band.visible==0});}
            if(full&&!C.manage.Empty())regions.push_back({SidebarManageId,R(C.manage),nullptr});   // the row says what it does: no tooltip
            if(full&&!C.gridToggle.Empty()){gridToggleTip=C.gridOpen?std::wstring(TR(sidebar_collapse_grid)):TRF(sidebar_all_bookmarks,std::to_wstring(bandItems.size()));regions.push_back({SidebarGridToggleId,R(C.gridToggle),nullptr});}
            if(C.emptyGrid&&!C.tiles.empty())regions.push_back({'E',R(C.tiles.front()),TR(chrome_tip_add_page_bookmark)});}
        // Tabs: close targets first (they win over their row), rows clipped to the list's view.
        for(int i=0;i<count&&i<(int)C.tabs.size();++i){auto row=C.tabs[i];if(row.bottom<=C.tabsView.top||row.top>=C.tabsView.bottom)continue;
            row.top=std::max(row.top,C.tabsView.top);row.bottom=std::min(row.bottom,C.tabsView.bottom);const bool clear=C.tabs[i].top>=C.tabsView.top-.01f&&C.tabs[i].bottom<=C.tabsView.bottom+.01f;
            if(full&&clear)regions.push_back({CloseBase+i,R(SB::CloseIn(C.tabs[i])),TR(chrome_tip_close_tab)});
            regions.push_back({TabBase+i,R(row),tips[i].c_str(),clear});}
        if(C.newTab.top<C.tabsView.bottom&&C.newTab.bottom>C.tabsView.top){auto r=C.newTab;r.top=std::max(r.top,C.tabsView.top);r.bottom=std::min(r.bottom,C.tabsView.bottom);regions.push_back({'T',R(r),TR(chrome_tip_new_tab)});}
        for(size_t i=0;i<sideTools.size()&&i<C.tools.size();++i){const int id=sideTools[i];const wchar_t* tip=id=='N'?AnnotateTip():id=='V'?TR(chrome_tip_audio):id=='Y'?TR(chrome_tip_history):id=='D'?TR(chrome_tip_downloads):TR(chrome_tip_settings);regions.push_back({id,R(C.tools[i]),tip});}
        // The compact column under the one over the page: only what the overlay leaves visible (nothing, it covers it).
        regions.push_back({'m',R(side.minimize),TR(chrome_tip_minimize)});regions.push_back({'M',R(side.maximize),TR(chrome_tip_maximize)});regions.push_back({'X',R(side.close),TR(chrome_tip_close_window)});
        // « Redimensionner »: the column's right edge along the page (keyboard: ← → ±16, Maj ±64, Début / Fin, Entrée 248).
        if(sidebarResizable&&!sidebarOverlay&&!side.page.Empty()){edgeTip=TR(sidebar_tip_resize);regions.push_back({SidebarEdgeId,R(SB::Resize::Handle(side.column,side.page)),edgeTip.c_str()});}
        if(sideHidden){if(!sideShown)regions.clear();else regions.erase(std::remove_if(regions.begin(),regions.end(),[](const Region& r){return r.rect.right<=r.rect.left||r.rect.bottom<=r.rect.top;}),regions.end());}   // Mode Focus: no window buttons; the column away, nothing to hit
        (void)selected;
    }
    // The field at a layout's place, with Paper's metrics: in the column the domain alone (01 1Q29-0 « lemonde.fr »),
    // in the window bar the whole address (02 1Q3H-0 « lemonde.fr/planete »).
    void UseAddress(const Sidebar::Layout& A){bar=R(A.address);addressGlyphOpacity=1;addressFont=A.addressFont;addressGlyph=A.addressGlyph;addressTextInset=A.text.left-A.address.left;
        addressSlotInset=A.lock.left-A.address.left;addressSlotSize=A.addressGlyph;addressHostOnly=A.addressInColumn;
        const float pad=A.addressInColumn?Sidebar::Complete::addressPad:Sidebar::Compact::addressPad;
        // In the column the text stops before Copy / the star (and the vault key): a long address or a « Non sécurisé » pill
        // fades out there (24 DIP), never under them; while typing the field has its whole width (no actions).
        edit={A.text.left,A.address.top,std::max(A.text.left,A.address.right-pad),A.address.bottom};   // the native EDIT (typing: no actions)
        addressTextRight=A.addressInColumn?std::max(A.text.left,A.text.right-(vaultKey?20.f:0.f)):1e9f;
        sideChip=true;sideChipInset=A.addressInColumn?SideChip::columnInset:A.lock.left-A.address.left;
        const float fade=A.addressInColumn||A.addressPlate?Design::Surface::raised:Design::Surface::canvas;if(fade!=addressFadeSurface){addressFadeSurface=fade;DropAddressFade();}}   // the compact bar's field has no plate: the canvas
    bool addressHostOnly=false;float addressFadeSurface=Design::Surface::raised;float addressTextRight=1e9f;   // the drawn address at rest stops here (the column: before Copy)
    // The field's colour under the native EDIT while typing (WM_CTLCOLOREDIT): the top bar's #292929; the column's and the
    // compact bar's typing field, white 4 % over the canvas (Paper 06 1QH8-0) — one colour across the field (Val, 24 nov.).
    float EditSurface()const{if(!sidebar||contentOnly)return Design::Surface::raised;const float c=Design::Surface::canvas;return c+(1-c)*Sidebar::Look::fieldEditing;}
   // the fade's colour: the field's (the compact bar's: the canvas)
    // Address metrics (the toolbar's by default; the column sets Paper's: 14 / 12 DIP glyphs, text 28 in).
    float AddressTracking()const{return Design::Type::address.tracking*addressFont/Design::Type::address.size;}   // -0.01 em
    float addressGlyphOpacity=Design::Address::secondary;   // the column's glyphs are Paper's #FFFFFF80 (the 50 % bitmap at 1)
    float addressFont=Design::Type::address.size,addressGlyph=Design::Size::icon,addressTextInset=Design::Address::textInset,addressSlotInset=Design::Address::slotInset,addressSlotSize=Design::Address::slot;
    // Fills of the column (white over the canvas), glyphs from Paper paths.
    void White(D2D1_RECT_F r,float radius,float alpha){FillBox(r,radius,D2D1::ColorF(1,1,1,alpha));}
    void SideGlyph(const char* d,D2D1_RECT_F box,float size,float alpha){PathGlyph(d,box,size,D2D1::ColorF(1,1,1,alpha));}
    float Hovered(int id,int hover,Motion::Time now){return motion?motion->Visual(id,now).hover:(hover==id?1.f:0.f);}
    // Window buttons (01 1Q00-0): glyph 12, stroke 1.8 / 24, white 70 %; hover keeps the app's plates (close warms red).
    void DrawWindowGlyph(int id,D2D1_RECT_F box,int hover,int focus,Motion::Time now){
        namespace Z=Sidebar::Zone;auto v=motion?motion->Visual(id,now):Motion::InteractionVisual{hover==id?1.f:0.f,0};
        const float h=std::clamp(v.hover,0.f,1.f),p=std::clamp(v.press,0.f,1.f);
        D2D1_COLOR_F ink=D2D1::ColorF(1,1,1,Z::glyphAlpha);
        if(id=='X'){if(h>0||p>0)FillBox(box,Design::Radius::control,D2D1::ColorF(250/255.f,106/255.f,89/255.f,.30f*h+.12f*p));ink=D2D1::ColorF(1,1+(208/255.f-1)*h,1+(200/255.f-1)*h,Z::glyphAlpha+(.95f-Z::glyphAlpha)*h);}
        else if(h>0||p>0)White(box,Design::Radius::control,Sidebar::Look::toolHover*(h+p));
        if(focus==id)FocusRing(box,Design::Radius::control);
        const float cx=(box.left+box.right)/2,cy=(box.top+box.bottom)/2,s=Z::glyph/24,w=Z::glyphStroke;   // 24-unit glyph scaled to 12
        brush->SetColor(ink);ID2D1StrokeStyle* round=nullptr;D2D1_STROKE_STYLE_PROPERTIES sp{};sp.startCap=sp.endCap=D2D1_CAP_STYLE_ROUND;factory->CreateStrokeStyle(sp,nullptr,0,&round);
        auto at=[&](float x,float y){return D2D1::Point2F(cx+(x-12)*s,cy+(y-12)*s);};
        if(id=='m')target->DrawLine(at(4,12),at(20,12),brush,w,round);
        else if(id=='M'){const auto a=at(5,5),b=at(19,19);target->DrawRoundedRectangle(D2D1::RoundedRect({a.x,a.y,b.x,b.y},1.5f*s,1.5f*s),brush,w);}
        else{target->DrawLine(at(6,6),at(18,18),brush,w,round);target->DrawLine(at(18,6),at(6,18),brush,w,round);}
        if(round)round->Release();}
    // One footer tool: hover white 4 %, its drawer open white 10 % (07 1QMH-0); glyph 16 white 70 % (settings 50 %).
    void DrawSideTool(int id,D2D1_RECT_F box,int hover,int focus,Motion::Time now){
        namespace K=Sidebar::Look;auto v=motion?motion->Visual(id,now):Motion::InteractionVisual{hover==id?1.f:0.f,0};
        if(id=='N'){DrawSideAnnotate(box,v,focus==id);return;}
        if(id=='V'){DrawAudioButton(box,{v.hover,v.press,false,focus==id},now);return;}
        const float fill=id==latched?K::toolOpen:K::toolHover*std::clamp(v.hover+v.press,0.f,1.f);
        if(fill>0)White(box,Design::Radius::control,fill);
        if(focus==id)FocusRing(box,Design::Radius::control);
        const float size=Design::Size::icon*Motion::PressScale(v.press,motion&&motion->ReducedMotion());
        if(id=='D'){const auto pose=downloadIcon.At(now);if(!pose.idle&&DrawDownloadIcon(pose,box,size,1))return;}
        SideGlyph(id=='Y'?Sidebar::Glyph::history:id=='D'?Sidebar::Glyph::downloads:Sidebar::Glyph::settings,box,size,id=='S'?K::settingsGlyph:K::glyph);}
    // Annoter in the tools (01 1TLQ-0 / compact 1TMQ-0): Paper's mark 15 DIP white 70 %; greyed (Design::Text::disabled,
    // like the toolbar's #125 « off » state) when the mode cannot start here, never hidden; while the mode is on, the
    // app's active look (accent 16 % plate, accent mark, 10 x 2 accent bar under it; Designer, answer 6).
    void DrawSideAnnotate(D2D1_RECT_F box,Motion::InteractionVisual v,bool focused){
        namespace G=Sidebar::Glyph;const auto a=Design::Accent::Get();const bool reduced=motion&&motion->ReducedMotion();
        if(annotateActive)FillBox(box,Design::Radius::control,D2D1::ColorF(a.r,a.g,a.b,.16f));
        else{const float fill=Sidebar::Look::toolHover*std::clamp(v.hover+v.press,0.f,1.f)*(Enabled('N')?1.f:0.f);if(fill>0)White(box,Design::Radius::control,fill);}
        if(focused)FocusRing(box,Design::Radius::control);
        auto& g=pathGlyphs[G::annotate];if(!g&&factory)g=DialogKit::SvgGeometry(factory,G::annotate);
        if(g){const float k=std::min(G::annotateBox/G::annotateW,G::annotateBox/G::annotateH)*Motion::PressScale(v.press,reduced),w=G::annotateW*k,h=G::annotateH*k;
            const float x=(box.left+box.right-w)/2,y=(box.top+box.bottom-h)/2;D2D1_MATRIX_3X2_F old;target->GetTransform(&old);
            target->SetTransform(D2D1::Matrix3x2F::Translation(-G::annotateX,-G::annotateY)*D2D1::Matrix3x2F::Scale(k,k)*D2D1::Matrix3x2F::Translation(x,y)*old);
                        // Active (Paper « Annotation · panneau » 1YHH-0): the profile's ink (#6FD087 for Travail) on the tinted plate, no underline.
            const auto ink=Design::Accent::Ink();brush->SetColor(annotateActive?D2D1::ColorF(ink.r,ink.g,ink.b,1):D2D1::ColorF(1,1,1,annotateDimmed||!Enabled('N')?Design::Text::disabled:Sidebar::Look::glyph));target->FillGeometry(g,brush);target->SetTransform(old);}
    }
    // Tab rows (01 1Q1K-0 / 1Q1C-0 / 1Q17-0; compact 1Q4Z-0): active #3E3E3E, hover white 4 %, rest none.
    // `row`: the full row (content keeps it, as the strip's content keeps the settled width); `shown`: the presented
    // surface while the row opens or the list moves (null: the row); `content` / `fillFactor`: the strip's TabItem
    // opacity and reduced-motion fill. The selection fades like the strip's (Motion::TabSelection, 120 ms ease-out):
    // the surface goes from the hover white to #3E3E3E, the title to full white. Returns the surface grey (ghosts).
    float DrawSideTab(const Sidebar::Layout& L,int i,const std::wstring& title,int selected,int hover,int focus,Motion::Time now,D2D1_RECT_F row,float alpha,float labels,const D2D1_RECT_F* shown=nullptr,float content=1,float fillFactor=1,bool plate=false){
        namespace K=Sidebar::Look;const bool full=L.mode==Sidebar::Mode::Complete;
        const bool member=i<(int)L.split.size()&&L.split[i].Member(),pair=member&&PairShown(L,i,selected);
        const float emphasis=pair?1.f:member?0.f:sidebarDrag<0&&motion&&animateSelection&&i<(int)tabKeys.size()?Motion::Clamp(motion->selection.Visual((int)TabKey(i),now)):(i==selected?1.f:0.f);
        const bool active=emphasis>.5f;
        const float h=std::max(Hovered(TabBase+i,hover,now),Hovered(CloseBase+i,hover,now));
        const D2D1_RECT_F surface=shown?*shown:row;const float c=Design::Surface::canvas,base=c+(1-c)*K::tabHover*std::clamp(h,0.f,1.f);
        const float fill=pair?c:member?c+(base-c)*fillFactor:plate?(i==selected?c:c+(base-c)*fillFactor):c+((base+(K::tabActive-base)*emphasis)-c)*fillFactor;   // plate: the selection is the sliding object; a pair: its #262626 (PaintSideSplit)
        if(fill>c+.001f&&surface.bottom-surface.top>.5f)Round(surface,std::min(Design::Radius::control,(surface.bottom-surface.top)/2),fill);
        if(shown)target->PushAxisAlignedClip(surface,D2D1_ANTIALIAS_MODE_ALIASED);
        alpha*=content;
        const Sidebar::Box rb{row.left,row.top,row.right,row.bottom};
        const auto fav=Sidebar::FaviconIn(rb,L.mode);
        const bool asleep=!active&&i<(int)sleeping.size()&&sleeping[i];
        DrawFavicon(i,R(fav),(asleep?.4f:1.f)*alpha);
        if(focus==TabBase+i)FocusRing(row,Design::Radius::control);
        if(!full){if(shown)target->PopAxisAlignedClip();return fill;}   // compact: the icon alone
        const bool closeShown=h>0||focus==CloseBase+i;
        const auto t=Sidebar::TitleIn(rb,closeShown);
        const float rest=h>0?K::titleRest+(K::titleHover-K::titleRest)*h:asleep?.32f:K::titleRest;
        const float o=(rest+(K::titleActive-rest)*emphasis)*labels*alpha;
        FadedText(title,R(t),Design::Type::label.size,o,false,active,0);
        if(closeShown){const auto cb=R(Sidebar::CloseIn(rb));const float press=motion?motion->Visual(CloseBase+i,now).press:0;
            SideGlyph(Sidebar::Glyph::close,cb,Sidebar::Complete::closeGlyph*Motion::PressScale(press,motion&&motion->ReducedMotion()),K::glyph*std::max(h,focus==CloseBase+i?1.f:0.f)*labels*alpha);
            if(focus==CloseBase+i)FocusRing(cb,Design::Radius::button);}
        if(shown)target->PopAxisAlignedClip();
        return pair?Sidebar::Split::fill:plate&&i==selected?K::tabActive:fill;   // the ghost keeps what the row looked like
    }
    // The selection plate at the selected row's presented box: a new selection slides it there from where it is (spring);
    // the same row moving with the list keeps it glued (snapped) unless a slide is under way (retargeted from the presented
    // value). Reduced motion: it jumps (the text's fade keeps the link).
    void PaintSelectionPlate(D2D1_RECT_F row,unsigned key,Motion::Time now){
        const bool reduced=motion&&motion->ReducedMotion();
        auto to=[&](Motion::Spring& s,float v){s.Set(v,now,selectionResponse,reduced);};
        if(!selPlateKey)selTop.Snap(row.top),selBottom.Snap(row.bottom),selLeft.Snap(row.left),selRight.Snap(row.right);
        else if(key!=selPlateKey||PlateMoving(now)){to(selTop,row.top);to(selBottom,row.bottom);to(selLeft,row.left);to(selRight,row.right);}
        else{selTop.Snap(row.top);selBottom.Snap(row.bottom);selLeft.Snap(row.left);selRight.Snap(row.right);}
        selPlateKey=key;
        const D2D1_RECT_F p{selLeft.Value(now),selTop.Value(now),selRight.Value(now),selBottom.Value(now)};
        if(p.bottom-p.top>.5f&&p.right-p.left>.5f)Round(p,std::min(Design::Radius::control,(p.bottom-p.top)/2),Sidebar::Look::tabActive);
    }
    // A closing row's ghost (the strip's DrawGhost on a row): drawn as it last looked, at full height in its old place,
    // into the shared ghost layer, then composited with the blur (0 -> 4 DIP) and opacity (1 -> 0) of the exit.
    void DrawSideGhost(const Sidebar::Layout& L,const Motion::TabItem& item,float labels){
        auto it=sideSnapshots.find(item.key);if(it==sideSnapshots.end())return;
        const float alpha=1-std::clamp(item.exit,0.f,1.f);if(alpha<=.002f)return;
        const auto& snap=it->second;const auto rect=R(Sidebar::ShownRow(L,item.slot,1,sideFrame.scroll));
        const float pad=3*Motion::TabExitBlur,w=rect.right-rect.left+2*pad,h=rect.bottom-rect.top+2*pad;
        if(!ghostLayer||ghostLayerSize.width!=w||ghostLayerSize.height!=h){Drop(ghostLayer);ghostLayerSize={};
            if(FAILED(target->CreateCompatibleRenderTarget(D2D1::SizeF(w,h),&ghostLayer))||!ghostLayer){Drop(ghostLayer);return;}
            ghostLayerSize={w,h};}
        auto* main=SwapTarget(ghostLayer);ghostLayer->BeginDraw();ghostLayer->Clear(D2D1::ColorF(0,0,0,0));
        const Sidebar::Box box{pad,pad,pad+rect.right-rect.left,pad+rect.bottom-rect.top};
        if(snap.fill>Design::Surface::canvas+.001f)Round(R(box),Design::Radius::control,snap.fill);
        const auto fav=R(Sidebar::FaviconIn(box,L.mode));if(!DrawSiteImage(snap.favicon,fav))Icon(L"favicon",fav,16);
        if(L.mode==Sidebar::Mode::Complete)Text(snap.title,R(Sidebar::TitleIn(box,false)),Design::Type::label.size,(snap.selected?Sidebar::Look::titleActive:Sidebar::Look::titleRest)*labels,false,snap.selected,0);
        HRESULT hr=ghostLayer->EndDraw();SwapTarget(main);if(FAILED(hr))return;
        ID2D1Bitmap* bitmap=nullptr;if(FAILED(ghostLayer->GetBitmap(&bitmap))||!bitmap)return;
        target->PushAxisAlignedClip(R(L.tabsView),D2D1_ANTIALIAS_MODE_ALIASED);
        const D2D1_POINT_2F origin{rect.left-pad,rect.top-pad};   // the spec animates opacity only: a plain fade (80 ms), no blur
        target->DrawBitmap(bitmap,D2D1::RectF(origin.x,origin.y,origin.x+w,origin.y+h),alpha);++ghostFadeDraws;
        target->PopAxisAlignedClip();Drop(bitmap);
    }
    // Vue partagée dans la colonne (Sidebar::Split; Paper 2473-0 « 1 · Repos » 247H-0 / 248B-0): each block's shared background
    // #262626 (radius 8) under its rows, the 10 x 1 #545454 line between joined rows. The rows paint over it (DrawSideTab).
    // Frozen look (2473-0, Val's retouch): every block is a 1 DIP #262626 outline (no fill, radius 8) around its rows;
    // the displayed pair's rows take #262626 with corners 6 outside / 3 inside; the 10 x 1 #545454 line on the favicons'
    // axis between joined rows.
    static bool PairShown(const Sidebar::Layout& L,int i,int selected){if(i<0||i>=(int)L.split.size()||!L.split[i].Member()||selected<0)return false;const auto b=Sidebar::BlockOf(L.split,i);return selected>=b.first&&selected<b.first+b.count;}
    // `off`: per row, what its presented place adds to the laid-out one (slot motion, step aside, §4 slide); `animated`: the
    // docked column (the one over the page in a narrow window is drawn as laid out).
    void PaintSideSplit(const Sidebar::Layout& L,int selected,Motion::Time now,const std::vector<float>& off,bool animated){namespace S=Sidebar::Split;
        const int n=std::min((int)L.tabs.size(),(int)L.split.size());const bool reduced=!motion||motion->ReducedMotion();
        auto rowAt=[&](int i){auto b=L.tabs[i];const float o=i<(int)off.size()?off[i]:0.f;b.top+=o;b.bottom+=o;return b;};
        auto padded=[](Sidebar::Box b){return Sidebar::Box{b.left-S::pad,b.top-S::pad,b.right+S::pad,b.bottom+S::pad};};
        for(int i=0;i<n;++i){if(!PairShown(L,i,selected))continue;const auto b=Sidebar::BlockOf(L.split,i);const bool first=i==b.first,last=i==b.first+b.count-1;
            if(auto g=CornerBox(R(rowAt(i)),first?S::outer:S::inner,first?S::outer:S::inner,last?S::outer:S::inner,last?S::outer:S::inner,true)){Color(S::fill);target->FillGeometry(g,brush);g->Release();}}
        // The outline: outside the padding in the panel (Paper's CSS outline), inside the 40 in compact (the frame spans the column).
        const float k=(L.mode==Sidebar::Mode::Complete?1:-1)*S::frameW/2;
        auto frame=[&](Sidebar::Box b,float alpha){if(alpha<=0)return;Color(S::frame,alpha);target->DrawRoundedRectangle(D2D1::RoundedRect({b.left-k,b.top-k,b.right+k,b.bottom+k},S::radius+k,S::radius+k),brush,S::frameW);};
        auto joint=[&](int i,float alpha){if(alpha<=0)return;const auto row=rowAt(i);const auto f=Sidebar::FaviconIn(row,L.mode);const float cx=(f.left+f.right)/2;Color(S::line,alpha);target->FillRectangle(D2D1::RectF(cx-S::lineW/2,row.bottom,cx+S::lineW/2,row.bottom+S::lineH),brush);};
        if(!animated){for(const auto& b:L.blocks)frame(b,1);for(int i=0;i<n;++i)if(L.split[i].JoinsNext()&&i+1<n)joint(i,1);return;}
        std::set<int> live;size_t blockIndex=0;
        for(int i=0;i<n;++i){const auto& sr=L.split[i];if(!sr.Member()||sr.JoinsPrevious())continue;if(blockIndex>=L.blocks.size())break;
            const auto blk=Sidebar::BlockOf(L.split,i);const auto B=L.blocks[blockIndex++];const int g=sr.group;live.insert(g);
            auto it=splitMarks.find(g);const bool fresh=it==splitMarks.end();auto& m=splitMarks[g];
            if(fresh||m.leaving){const int a=selected>=blk.first&&selected<blk.first+blk.count?selected:blk.first;
                if(fresh){const auto A=padded(rowAt(a));if(!splitMarksPrimed||reduced){m.l.Snap(B.left);m.t.Snap(B.top);m.r.Snap(B.right);m.b.Snap(B.bottom);m.line.Snap(splitMarksPrimed?0.f:1.f);}   /* reduced motion: in place, the line still fades */
                    else{m.l.Snap(A.left);m.t.Snap(A.top);m.r.Snap(A.right);m.b.Snap(A.bottom);m.line.Snap(0);m.born=true;}}
                m.anchor=TabKey(a);m.leaving=false;}
            m.l.Set(B.left,now,splitResponse,reduced);m.t.Set(B.top,now,splitResponse,reduced);m.r.Set(B.right,now,splitResponse,reduced);m.b.Set(B.bottom,now,splitResponse,reduced);
            m.full=B.Height();m.row=padded(rowAt(blk.first)).Height();
            bool rowsMoving=false;for(int r=blk.first;r<blk.first+blk.count&&r<n;++r){auto sp=rowShift.find(TabKey(r));if(sp!=rowShift.end()&&sp->second.Moving(now))rowsMoving=true;}
            const bool settled=!m.l.Moving(now)&&!m.t.Moving(now)&&!m.r.Moving(now)&&!m.b.Moving(now)&&!rowsMoving;
            if(settled){m.born=false;m.line.Set(1,now,std::chrono::milliseconds(splitLineMs),false,Motion::Curve::SmoothStep);}}
        // Separated: the line out, then the frame back to the row it came from.
        for(auto& e:splitMarks){if(live.count(e.first))continue;auto& m=e.second;
            if(!m.leaving){m.leaving=true;m.born=false;m.full=m.b.Value(now)-m.t.Value(now);m.line.Set(0,now,std::chrono::milliseconds(splitLineMs),false,Motion::Curve::SmoothStep);}
            int a=-1;for(int r=0;r<n;++r)if(TabKey(r)==m.anchor)a=r;
            if(a>=0){const auto A=padded(rowAt(a));m.row=A.Height();m.l.Set(A.left,now,splitResponse,reduced);m.t.Set(A.top,now,splitResponse,reduced);m.r.Set(A.right,now,splitResponse,reduced);m.b.Set(A.bottom,now,splitResponse,reduced);}
            else{m.l.Snap(m.l.Value(now));m.t.Snap(m.t.Value(now));m.r.Snap(m.r.Value(now));m.b.Snap(m.b.Value(now));m.row=m.full;}}
        for(auto e=splitMarks.begin();e!=splitMarks.end();){auto& m=e->second;const bool still=!m.l.Moving(now)&&!m.t.Moving(now)&&!m.r.Moving(now)&&!m.b.Moving(now);
            if(m.leaving&&(still||reduced)&&!m.line.Moving(now)){e=splitMarks.erase(e);continue;}++e;}
        splitMarksPrimed=true;
        // Drawn: each frame at its presented box (its opacity follows its size while it grows from / shrinks to a row), the
        // lines with their group's fade.
        for(auto& e:splitMarks){const auto& m=e.second;const Sidebar::Box b{m.l.Value(now),m.t.Value(now),m.r.Value(now),m.b.Value(now)};
            const float span=m.full-m.row;const float alpha=(m.born||m.leaving)&&span>1?std::clamp((b.Height()-m.row)/span,0.f,1.f):1.f;frame(b,alpha);}
        for(int i=0;i<n;++i){if(!L.split[i].JoinsNext()||i+1>=n)continue;auto it=splitMarks.find(L.split[i].group);joint(i,it==splitMarks.end()?1.f:it->second.line.Value(now));}}
    // « 2 · Survol du menu » (2492-0 / 24AJ-0): the tabs a highlighted split-menu row designates and the right-clicked tab,
    // a 1 DIP inner #8C8C8C stroke on each row (its radius 6), over the row; §4: it fades in / out (100 ms).
    void PaintSideMenuTargets(const Sidebar::Layout& L,Motion::Time now,const std::vector<float>& off,bool animated){namespace S=Sidebar::Split;const float k=S::outlineW/2,r=Design::Radius::control-k;
        auto draw=[&](int i,float alpha){if(alpha<=0||i<0||i>=(int)L.tabs.size())return;auto b=R(L.tabs[i]);const float o=i<(int)off.size()?off[i]:0.f;b.top+=o;b.bottom+=o;Color(S::outline,alpha);
            target->DrawRoundedRectangle(D2D1::RoundedRect({b.left+k,b.top+k,b.right-k,b.bottom-k},r,r),brush,S::outlineW);};
        const auto designated=Sidebar::Designated(menuTargets,menuSource,(int)L.tabs.size());
        if(!animated){for(int i:designated)draw(i,1);return;}
        std::set<unsigned> on;for(int i:designated)on.insert(TabKey(i));
        for(unsigned key:on)menuOutlines[key].Set(1,now,std::chrono::milliseconds(menuOutlineMs),false,Motion::Curve::SmoothStep);
        for(auto e=menuOutlines.begin();e!=menuOutlines.end();){if(!on.count(e->first)){e->second.Set(0,now,std::chrono::milliseconds(menuOutlineMs),false,Motion::Curve::SmoothStep);if(!e->second.Moving(now)){e=menuOutlines.erase(e);continue;}}++e;}
        for(int i=0;i<(int)L.tabs.size();++i){auto e=menuOutlines.find(TabKey(i));if(e!=menuOutlines.end())draw(i,e->second.Value(now));}}
    // The column's own fills and glyphs (the shared toolbar buttons are painted by PaintSideButton).
    void PaintSideColumn(const Sidebar::Layout& L,const std::vector<std::wstring>& titles,int selected,bool editing,int hover,int focus,Motion::Time now,bool overlay){
        namespace SB=Sidebar;namespace K=SB::Look;const bool full=L.mode==SB::Mode::Complete;
        const float col=overlay?L.column:sidebarColumn;
        if(overlay&&L.hidden){const float g=((SB::FocusSide::borderRgb>>16)&255)/255.f;Color(Design::Surface::canvas);target->FillRectangle(D2D1::RectF(0,L.top,L.column,height),brush);
            brush->SetColor(D2D1::ColorF(g,g,g,1));target->FillRectangle(D2D1::RectF(L.column,L.top,L.column+SB::FocusSide::border,height),brush);}   // Mode Focus (2091-0 / 20EJ-0)
        else if(overlay){if(auto g=TabShape({0,0,L.column+SB::Overlay::border,height},0,SB::Overlay::radius)){Color(Design::Surface::canvas);target->FillGeometry(g,brush);g->Release();}
            if(auto g=CornerBox({.5f-SB::Overlay::radius,-.5f-SB::Overlay::radius,L.column+.5f,height+.5f},0,SB::Overlay::radius,SB::Overlay::radius,0)){Color(1,SB::Overlay::borderAlpha);target->DrawGeometry(g,brush,SB::Overlay::border);g->Release();}}
        target->PushAxisAlignedClip(D2D1::RectF(0,L.top,col,height),D2D1_ANTIALIAS_MODE_ALIASED);
        const float labels=overlay?1.f:sidebarTitles;
        // Top: profile, fold button (+ Back / Forward / Reload in the complete column).
        {const auto plate=overlay?R(L.profile):Morph(900,L.profile,now);profileRest=L.profilePlate?Design::ProfileButton::rest:0;DrawProfileButton(hover,focus,now,&plate);profileRest=Design::ProfileButton::rest;}
        {const auto tg=overlay?R(L.toggle):Morph(SidebarToggleId,L.toggle,now);const float hv=Hovered(SidebarToggleId,hover,now);if(hv>0)White(tg,Design::Radius::control,K::toolHover*hv);if(focus==SidebarToggleId)FocusRing(tg,Design::Radius::control);SideGlyph(SB::Glyph::fold,tg,Design::Size::icon,K::foldGlyph);}   /* #6: 50 % like Back / Forward / Reload */
        if(full)for(int id:{'B','F','R'}){const auto& b=id=='B'?L.back:id=='F'?L.forward:L.reload;PaintSideButton(id,overlay?R(b):Morph(id,b,now),hover,focus,now);}
        // Address in the column.
        if(full)PaintSideAddress(L,editing,hover,focus,now,labels);
        // Grid B.
        PaintSideGrid(L,hover,focus,now,labels);
        // Tabs.
        Color(1,K::line);target->FillRectangle(R(L.line),brush);
        target->PushAxisAlignedClip({L.tabsView.left-Sidebar::Split::frameW,L.tabsView.top,L.tabsView.right+Sidebar::Split::frameW,L.tabsView.bottom},D2D1_ANTIALIAS_MODE_ALIASED);   // the split frame's outline sits 1 DIP outside the rows
        const int n=std::min((int)titles.size(),(int)L.tabs.size());
        const bool moving=!overlay&&sidebarDrag<0&&&L==&side&&!sideFrame.items.empty();
        // No drag: every row's offset heads home (a drag cancelled by Échap, or dropped on the grid, leaves its stepped-aside
        // rows at their step otherwise); a drop and a §4 slide already aim at 0.
        if(moving){const bool reduced=!motion||motion->ReducedMotion();for(auto& e:rowShift)if(e.second.Target()!=0)e.second.Set(0,now,stepResponse,reduced);}
        std::vector<float> rowOff(L.tabs.size(),0.f);   // what each row's presented place adds to its laid-out one
        if(moving){const float pitch=SB::Rows(L.mode).row+SB::Rows(L.mode).gap;for(auto& it:sideFrame.items)if(it.index>=0&&it.index<(int)rowOff.size())rowOff[it.index]=(it.slot-it.index*pitch)-(sideFrame.scroll-L.scroll)+RowShift(it.key,now);}
        else for(int i=0;i<(int)rowOff.size();++i)rowOff[i]=RowShift(TabKey(i),now);
        PaintSideSplit(L,selected,now,rowOff,&L==&side);
        auto emptySlot=[&](D2D1_RECT_F r){Color(K::lifted);target->DrawRoundedRectangle(D2D1::RoundedRect({r.left+.5f,r.top+.5f,r.right-.5f,r.bottom-.5f},Design::Radius::control-.5f,Design::Radius::control-.5f),brush,1);};   // Dragged onto the page (split view, « Vue partagée dans la colonne » 3): its place stays empty, a 1 DIP inner outline.
        auto titleOf=[&](int i){return titles[i].empty()?std::wstring(i<(int)sideHome.size()&&sideHome[i]?TR(tab_new):TR(chrome_tab_untitled_tip)):titles[i];};
        auto newRow=L.newTab;
        if(moving){   // the strip's model on rows: ghosts under, live rows at their presented slots, « + » after them
            const float pitch=SB::Rows(L.mode).row+SB::Rows(L.mode).gap;
            for(auto& it:sideFrame.items)if(it.index<0)DrawSideGhost(L,it,labels);
            for(auto& it:sideFrame.items){const int i=it.index;if(i!=selected||i<0||i>=n||i==liftedIndex||PairShown(L,i,selected))continue;   // the plate first, under every row (not a displayed pair: its own #262626)
                const float dy=(it.slot-i*pitch)-(sideFrame.scroll-L.scroll)+RowShift(it.key,now);auto r=L.tabs[i];r.top+=dy;r.bottom=r.top+L.tabs[i].Height()*std::clamp(it.presence,0.f,1.f);PaintSelectionPlate(R(r),it.key,now);}
            for(auto& it:sideFrame.items){const int i=it.index;if(i<0||i>=n)continue;
                // The laid-out row (split blocks keep their offsets and insets), moved by what the motion adds: its slot's
                // distance from rest and the presented scroll; height scaled by its presence.
                if(rowDrop.pending&&it.key==rowDrop.key){auto& sp=rowShift[it.key];sp.Snap(rowDrop.top-L.tabs[i].top);sp.Throw(0,now,dropResponse,dropDamping,rowDrop.velocity,!motion||motion->ReducedMotion());rowDrop.pending=false;}
                const float dy=(it.slot-i*pitch)-(sideFrame.scroll-L.scroll)+RowShift(it.key,now);auto rowFull=L.tabs[i];rowFull.top+=dy;rowFull.bottom+=dy;
                auto rowShown=rowFull;rowShown.bottom=rowShown.top+rowFull.Height()*std::clamp(it.presence,0.f,1.f);
                if(rowFull.bottom<=L.tabsView.top||rowFull.top>=L.tabsView.bottom)continue;
                if(i==liftedIndex){emptySlot(R(rowFull));continue;}
                const auto title=titleOf(i);const auto shown=R(rowShown);
                const float fill=DrawSideTab(L,i,title,selected,hover,focus,now,R(rowFull),1.f,labels,&shown,it.opacity,it.fill,true);
                auto& snap=sideSnapshots[it.key];snap.title=title;snap.favicon=i<(int)favicons.size()?favicons[i]:nullptr;snap.fill=fill;snap.selected=i==selected;}
            for(auto s=sideSnapshots.begin();s!=sideSnapshots.end();){if(std::none_of(sideFrame.items.begin(),sideFrame.items.end(),[&](const Motion::TabItem& i){return i.key==s->first;}))s=sideSnapshots.erase(s);else ++s;}
            const float dn=(sideFrame.content-n*pitch)-(sideFrame.scroll-L.scroll);newRow.top+=dn;newRow.bottom+=dn;}
        else{const bool reduced=!motion||motion->ReducedMotion();
            const float step=sidebarDrag>=0&&sidebarDrag<n?L.tabs[sidebarDrag].Height()+SB::Rows(L.mode).gap:0.f;
            for(int i=0;i<n;++i){
                // §1: the rows between the lifted row's place and where it would land step aside by its pitch.
                float target=0;if(sidebarDrag>=0&&sidebarInsert>=0&&i!=sidebarDrag){if(sidebarInsert<sidebarDrag&&i>=sidebarInsert&&i<sidebarDrag)target=step;else if(sidebarInsert>sidebarDrag&&i>sidebarDrag&&i<=sidebarInsert)target=-step;}
                auto& sp=rowShift[TabKey(i)];sp.Set(target,now,stepResponse,reduced);
                auto row=L.tabs[i];const float o=sp.Value(now);row.top+=o;row.bottom+=o;
                if(row.bottom<=L.tabsView.top||row.top>=L.tabsView.bottom)continue;
                if(i==liftedIndex){emptySlot(R(row));continue;}
                if(i==sidebarDrag)continue;   // it is the lifted row, drawn over the list below
                DrawSideTab(L,i,titleOf(i),selected,hover,focus,now,R(row),1.f,labels);}}
        PaintSideMenuTargets(L,now,rowOff,&L==&side);
        {const auto r=R(newRow);const float hv=Hovered('T',hover,now);if(hv>0)White(r,Design::Radius::control,K::tabHover*hv);if(focus=='T')FocusRing(r,Design::Radius::control);
            const auto f=SB::FaviconIn(newRow,L.mode);SideGlyph(SB::Glyph::plus,R(f),14,K::newTab);
            if(full)Text(TR(tab_new),{f.right+SB::Complete::tabIconGap,r.top,r.right-SB::Complete::tabPad,r.bottom},Design::Type::label.size,K::newTab*labels,false,false,0);}
        // Drag (05): the place it leaves stays hollow (30 %), an accent line marks where it lands, the row follows the pointer.
        if(sidebarDrag>=0&&sidebarDrag<n){
            if(liftedRow!=sidebarDrag){liftedRow=sidebarDrag;liftGrow.Snap(1);liftGrow.Set(liftScale,now,liftResponse,!motion||motion->ReducedMotion());}
            const auto& o=L.tabs[sidebarDrag];const float k=liftGrow.Value(now),cx=(o.left+o.right)/2,cy=sidebarDragY+o.Height()/2;
            D2D1_MATRIX_3X2_F keep;target->GetTransform(&keep);target->SetTransform(D2D1::Matrix3x2F::Scale(k,k,{cx,cy})*keep);
            const D2D1_RECT_F d{o.left,sidebarDragY,o.right,sidebarDragY+o.Height()};
            Round(d,Design::Radius::control,K::dragged);Color(1,K::draggedBorder);target->DrawRoundedRectangle(D2D1::RoundedRect({d.left+.5f,d.top+.5f,d.right-.5f,d.bottom-.5f},5.5f,5.5f),brush,1);
            const SB::Box db{d.left,d.top,d.right,d.bottom};DrawFavicon(sidebarDrag,R(SB::FaviconIn(db,L.mode)));
            if(full)Text(titles[sidebarDrag].empty()?std::wstring(TR(tab_new)):titles[sidebarDrag],R(SB::TitleIn(db,false)),Design::Type::label.size,labels,false,false,0);
            target->SetTransform(keep);}
        else liftedRow=-1;
        target->PopAxisAlignedClip();
        // List fades (answer 4): 24 DIP of canvas over rows scrolled out at the top / bottom.
        if(L.scroll>.5f)DrawVerticalFade(L.tabsView.left,L.tabsView.top,L.tabsView.right,L.tabsView.top+Design::TabStrip::fade,overlay);
        if(L.scroll<L.maxScroll-.5f)DrawVerticalFade(L.tabsView.left,L.tabsView.bottom,L.tabsView.right,L.tabsView.bottom-Design::TabStrip::fade,overlay);
        // Tools.
        Color(1,K::line);target->FillRectangle(R(L.footerLine),brush);
        for(size_t i=0;i<sideTools.size()&&i<L.tools.size();++i)DrawSideTool(sideTools[i],overlay?R(L.tools[i]):Morph(sideTools[i],L.tools[i],now),hover,focus,now);
        target->PopAxisAlignedClip();
    }
    // One bookmark tile (01 1Q23-0 / grid B 1WOO-0 / compact 1WDV-0): fill, hover, focus, favicon 18 or the folder glyph.
    void DrawBookmarkTile(int i,D2D1_RECT_F t,float rest,int hover,int focus,Motion::Time now){namespace K=Sidebar::Look;const int id=BookmarkBase+i;
        float hv=Hovered(id,hover,now);if(id==bandOpen)hv=1;White(t,Design::Radius::r8,rest+(K::tileHover-rest)*std::clamp(hv,0.f,1.f));if(focus==id)FocusRing(t,Design::Radius::r8);
        if(i<0||i>=(int)bandItems.size()){return;}const auto& item=bandItems[i];const float cx=(t.left+t.right)/2,cy=(t.top+t.bottom)/2;
        if(i==bandDropInto){const auto a=Design::Accent::Get();FillBox(t,Design::Radius::r8,D2D1::ColorF(a.r,a.g,a.b,.18f));}   // a drag would file it here (the bar's look)
        if(item.folder)SideGlyph(Sidebar::Glyph::folder,t,Design::Size::icon,K::glyph);
        else if(!DrawSiteImage(item.image,{cx-9,cy-9,cx+9,cy+9}))Icon(item.local?L"file":L"favicon",t,Design::Size::icon,1);
        if(i==bandDrag){const float c=Design::Surface::canvas;FillBox(t,Design::Radius::r8,D2D1::ColorF(c,c,c,1-K::origin));}}   // the dragged tile stays, dimmed (the tab drag's origin look)
    // A grid in its viewport: tiles clipped, the canvas fade at the bottom while more is below, the thin bar (1WRB-0, 1WRC-0).
    void DrawGridView(const Sidebar::Grid& g,float rest,int hover,int focus,Motion::Time now){namespace C=Sidebar::Complete;
        target->PushAxisAlignedClip(R(g.view),D2D1_ANTIALIAS_MODE_ALIASED);
        for(int i=0;i<(int)g.tiles.size();++i){const auto& t=g.tiles[i];if(t.bottom<=g.view.top||t.top>=g.view.bottom)continue;DrawBookmarkTile(i,Morph(BookmarkBase+i,t,now),rest,hover,focus,now);}
        if(g.Scrolls()&&g.scroll<g.maxScroll-.5f)DrawVerticalFade(g.view.left,g.view.bottom,g.view.right,g.view.bottom-C::fade,false);
        target->PopAxisAlignedClip();
        if(!g.bar.Empty())FillBox(R(g.bar),2,D2D1::ColorF(1,1,1,C::barAlpha));}
    void PaintSideGrid(const Sidebar::Layout& L,int hover,int focus,Motion::Time now,float labels){
        namespace SB=Sidebar;namespace K=SB::Look;namespace C=SB::Complete;const bool full=L.mode==SB::Mode::Complete;
        if(L.gridBlock.Empty())return;
        if(L.emptyGrid){const auto box=R(L.tiles.front());const float hv=Hovered('E',hover,now);White(box,Design::Radius::r8,(full?K::tileRest:K::compactTile)+(K::tileHover-(full?K::tileRest:K::compactTile))*hv);if(focus=='E')FocusRing(box,Design::Radius::r8);
            const float cy=(box.top+box.bottom)/2;if(!full){Icon(L"star",box,16,1);return;}
            // « Toujours » with no bookmark: the bar's empty way in, full width (answer 3).
            Icon(L"star",{box.left+8,cy-8,box.left+24,cy+8},16,1);
            const float tw=TextWidth(TR(chrome_bookmarks_add_page),Design::Type::label.size,true,false,Design::Type::label.tracking);
            Text(TR(chrome_bookmarks_add_page),{box.left+30,box.top,std::min(box.right,box.left+30+tw+2),box.bottom},Design::Type::label.size,(Design::Text::secondary+.5f*hv)*labels,false,true,Design::Type::label.tracking);
            float x=box.left+30+tw+16;for(const wchar_t* key:{L"Ctrl",L"D"}){const float w=TextWidth(key,14,false,true)+12;if(x+w>box.right-4)break;FillBox({x,cy-10,x+w,cy+10},4,D2D1::ColorF(1,1,1,.06f*labels));keycap=true;Text(key,{x+5,cy-10,x+w,cy+10},14,Design::Text::disabled*labels,true);keycap=false;x+=w+2;}
            return;}
        if(full){DrawGridView(L.grid,K::tileRest,hover,focus,now);
            if(!L.gridToggle.Empty()){   // « Tous les favoris (N) » ⌄ / « Réduire » ⌃ (1WKW-0 / 1WRD-0)
                const auto r=R(L.gridToggle);const float hv=Hovered(SidebarGridToggleId,hover,now);if(hv>0)White(r,Design::Radius::control,K::toolHover*hv);if(focus==SidebarGridToggleId)FocusRing(r,Design::Radius::control);
                const D2D1_RECT_F caret{r.right-C::togglePad-C::toggleGlyph,r.top,r.right-C::togglePad,r.bottom};
                FadedText(gridToggleTip,{r.left+C::togglePad,r.top,caret.left-C::togglePad,r.bottom},C::toggleFont,K::toggleText*labels,false,false,0);
                SideGlyph(L.gridOpen?SB::Glyph::caretUp:SB::Glyph::caretDown,caret,C::toggleGlyph,K::toggleText*labels);}
            if(!L.manage.Empty())PaintManageRow(R(L.manage),false,hover,focus,now,labels);
            return;}
        // Compact: the first tiles, then « » » (#FFFFFF0A, caret-double-right 70 %; open: #FFFFFF1F, white).
        const int shown=SideBookmarkTiles(L);for(int i=0;i<shown;++i)DrawBookmarkTile(i,Morph(BookmarkBase+i,L.tiles[i],now),K::compactTile,sidebarPopover?0:hover,focus,now);
        if(L.more){const auto t=R(L.tiles.back());const float hv=Hovered(SidebarMoreId,hover,now);const bool open=sidebarPopover;
            White(t,Design::Radius::r8,open?K::moreOpen:K::moreRest+(K::tileHover-K::moreRest)*hv);if(focus==SidebarMoreId)FocusRing(t,Design::Radius::r8);SideGlyph(SB::Glyph::more,t,Design::Size::icon,open?1.f:K::glyph);}}
    // The compact popover (« compact, 40 » 1WSC-0): canvas card, 1 DIP #FFFFFF0F border, radius 8, the unfolded grid.
    void PaintSidePopover(int hover,int focus,Motion::Time now){namespace P=Sidebar::Popover;if(!sidebarPopover||sidePop.box.Empty())return;
        const auto b=R(sidePop.box);Round(b,P::radius,Design::Surface::canvas);Color(1,P::borderAlpha);target->DrawRoundedRectangle(D2D1::RoundedRect({b.left+.5f,b.top+.5f,b.right-.5f,b.bottom-.5f},P::radius-.5f,P::radius-.5f),brush,P::border);
        DrawGridView(sidePop.grid,Sidebar::Look::tileRest,hover,focus,now);
        FillBox(R(sidePop.divider),0,D2D1::ColorF(0x26/255.f,0x26/255.f,0x26/255.f,1));PaintManageRow(R(sidePop.manage),true,hover,focus,now,1);}
    // « Gérer les favoris  Ctrl+Maj+O » (1Z8Y-0 under the unfolded grid; 1Z2S-0 at the popover's foot): #242424, radius 6,
    // folder #B3B3B3, label #E6E6E6 (popover: white), the shortcut #808080 on the right; solid Paper colours over #242424.
    // Hover: the tools' white 4 % (Paper draws no hover for it).
    void PaintManageRow(D2D1_RECT_F r,bool popover,int hover,int focus,Motion::Time now,float labels){namespace C=Sidebar::Complete;namespace P=Sidebar::Popover;
        const float bg=0x24/255.f;auto on=[bg](unsigned c){return (c/255.f-bg)/(1-bg);};   // an opacity giving that grey over #242424
        FillBox(r,Design::Radius::control,D2D1::ColorF(bg,bg,bg,1));const float hv=Hovered(SidebarManageId,hover,now);if(hv>0)White(r,Design::Radius::control,Sidebar::Look::toolHover*hv);
        if(focus==SidebarManageId)FocusRing(r,Design::Radius::control);
        const float padL=popover?P::managePadL:C::managePad,padR=popover?P::managePadR:C::managePad,gap=popover?P::manageGap:C::manageGap,icon=popover?P::manageIcon:C::manageIcon,cy=(r.top+r.bottom)/2;
        SideGlyph(Sidebar::Glyph::folder,{r.left+padL,cy-icon/2,r.left+padL+icon,cy+icon/2},icon,on(0xB3)*labels);
        const std::wstring key=TR(key_ctrl_shift_o);const float kw=TextWidth(key,C::manageKeyFont,false,false,0);
        Text(key,{r.right-padR-kw-1,r.top,r.right-padR+1,r.bottom},C::manageKeyFont,on(0x80)*labels);
        FadedText(TR(bookmarks_manage),{r.left+padL+icon+gap,r.top,r.right-padR-kw-gap,r.bottom},C::manageFont,(popover?1.f:on(0xE6))*labels);}
    // The docked column's soft edge at the page (x = `edge`): the canvas from 0 to 1 over its last edgeFade DIP.
    void DrawEdgeFade(float edge){const float c=Design::Surface::canvas,from=edge-Sidebar::Complete::edgeFade;if(edge<=0)return;ID2D1GradientStopCollection* stops=nullptr;ID2D1LinearGradientBrush* g=nullptr;
        D2D1_GRADIENT_STOP s[]={{0,D2D1::ColorF(c,c,c,0)},{1,D2D1::ColorF(c,c,c,1)}};
        if(SUCCEEDED(target->CreateGradientStopCollection(s,2,&stops))&&SUCCEEDED(target->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({from,0},{edge,0}),stops,&g)))target->FillRectangle(D2D1::RectF(std::max(0.f,from),0,edge,height),g);
        Drop(g);Drop(stops);}
    // A column text cut by its slot (Sidebar::Complete::textFade): its glyphs fade over the slot's last 12 DIP (an opacity
    // layer, whatever the row's fill under it); a text that fits is drawn as is.
    void FadedText(const std::wstring& value,D2D1_RECT_F slot,float size,float opacity,bool tabular=false,bool medium=false,float tracking=0){
        const float fade=Sidebar::Complete::textFade;const float full=value.empty()?0.f:TextWidth(value,size,medium,tabular,tracking);
        if(value.empty()||slot.right-slot.left<=fade||full<=slot.right-slot.left+.5f){Text(value,slot,size,opacity,tabular,medium,tracking);return;}
        ID2D1GradientStopCollection* stops=nullptr;ID2D1LinearGradientBrush* g=nullptr;ID2D1Layer* layer=nullptr;
        D2D1_GRADIENT_STOP s[]={{0,D2D1::ColorF(0,0,0,1)},{1,D2D1::ColorF(0,0,0,0)}};
        const D2D1_RECT_F whole{slot.left,slot.top,slot.left+full+2,slot.bottom};   // laid out whole (never an ellipsis), cut at the slot under the fade
        if(SUCCEEDED(target->CreateGradientStopCollection(s,2,&stops))&&SUCCEEDED(target->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({slot.right-fade,0},{slot.right,0}),stops,&g))&&SUCCEEDED(target->CreateLayer(nullptr,&layer))){
            target->PushLayer(D2D1::LayerParameters({slot.left,slot.top-size,slot.right,slot.bottom+size},nullptr,D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,D2D1::IdentityMatrix(),1.f,g,D2D1_LAYER_OPTIONS_NONE),layer);
            Text(value,whole,size,opacity,tabular,medium,tracking);target->PopLayer();}
        else Text(value,slot,size,opacity,tabular,medium,tracking);
        Drop(layer);Drop(g);Drop(stops);}
    void DrawVerticalFade(float left,float from,float right,float to,bool overlay){(void)overlay;const float c=Design::Surface::canvas;ID2D1GradientStopCollection* stops=nullptr;ID2D1LinearGradientBrush* g=nullptr;
        D2D1_GRADIENT_STOP s[]={{0,D2D1::ColorF(c,c,c,1)},{1,D2D1::ColorF(c,c,c,0)}};
        if(SUCCEEDED(target->CreateGradientStopCollection(s,2,&stops))&&SUCCEEDED(target->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({0,from},{0,to}),stops,&g)))target->FillRectangle(D2D1::RectF(left,std::min(from,to),right,std::max(from,to)),g);
        Drop(g);Drop(stops);}
    // The address field (01 1Q26-0, editing 06 1QH8-0, compact 1Q3G-0).
    void PaintSideAddress(const Sidebar::Layout& L,bool editing,int hover,int focus,Motion::Time now,float labels){
        // Motion spec §2: the field glides between the column and the window bar (translated with its morphed box).
        D2D1_MATRIX_3X2_F keep;target->GetTransform(&keep);const bool glide=sideMorphing&&!editing;
        if(glide){const auto m=Morph('L',L.address,now);target->SetTransform(D2D1::Matrix3x2F::Translation(m.left-L.address.left,m.top-L.address.top)*keep);}
        PaintSideAddressAt(L,editing,hover,focus,now,labels);
        if(glide)target->SetTransform(keep);else Morph('L',L.address,now);
    }
    void PaintSideAddressAt(const Sidebar::Layout& L,bool editing,int hover,int focus,Motion::Time now,float labels){
        namespace K=Sidebar::Look;const auto box=R(L.address);
        if(L.addressPlate||editing){White(box,Design::Radius::control,editing?K::fieldEditing:K::field);}DrawLoading(box);   // the compact bar's field has no background (Val's retouch 1WOG-0)
        if(editing||focus=='L')AddressRing(box,Design::Radius::control);   // #5: 5 % after a click, 50 % from the keyboard
        const auto keepBar=bar,keepEdit=edit;const float k1=addressFont,k2=addressGlyph,k3=addressTextInset,k4=addressSlotInset,k5=addressSlotSize,k7=addressFadeSurface,k9=sideChipInset,k10=addressTextRight;const bool k6=addressHostOnly,k8=sideChip;
        const bool own=std::fabs(bar.left-L.address.left)<.01f&&std::fabs(bar.top-L.address.top)<.01f;if(!own)UseAddress(L);
        const auto keepRequest=request;if(!own)request=D2D1_RECT_F{};
        DrawAddress(editing,own&&hover=='I');
        if(!own){bar=keepBar;edit=keepEdit;addressFont=k1;addressGlyph=k2;addressTextInset=k3;addressSlotInset=k4;addressSlotSize=k5;addressHostOnly=k6;request=keepRequest;sideChip=k8;sideChipInset=k9;addressTextRight=k10;if(addressFadeSurface!=k7){addressFadeSurface=k7;DropAddressFade();}}
        if(L.addressInColumn&&!editing){
            for(int id:{'C','K'}){const auto it=std::find_if(regions.begin(),regions.end(),[id](const Region& r){return r.id==id;});if(it==regions.end())continue;PaintSideButton(id,it->rect,hover,focus,now,labels);}
            if(vaultKey){const auto it=std::find_if(regions.begin(),regions.end(),[](const Region& r){return r.id==VaultKeyId;});if(it!=regions.end())DrawVaultKey(it->rect,Hovered(VaultKeyId,hover,now),focus==VaultKeyId);}}
    }
    // Toolbar buttons at their column / window-bar places (same motion, Lottie and states as the toolbar).
    void PaintSideButton(int id,D2D1_RECT_F box,int hover,int focus,Motion::Time now,float alpha=1,bool plates=true){
        auto interaction=motion?motion->Visual(id,now):Motion::InteractionVisual{hover==id?1.f:0.f,0};if(id==latched)interaction.hover=1;
        Design::ButtonState state{interaction.hover,interaction.press,!Enabled(id),focus==id};
        if(!plates&&(id=='B'||id=='F'||id=='R')){   // the compact bar (Val's retouch 1WO6-0 / 1WOD-0): no plate; a quiet one under the pointer
            const bool reduced=motion&&motion->ReducedMotion();const float a=std::clamp(state.hover+state.press,0.f,1.f)*(state.disabled?0.f:1.f);
            if(id=='R'){if(a>0)White(box,Design::Radius::control,Sidebar::Look::toolHover*a);}
            else{const bool back=id=='B';namespace T=Design::Toolbar;const float rl=back?Design::Radius::control:T::innerRadius,rr=back?T::innerRadius:Design::Radius::control;
                if(a>0)if(auto g=TabShape(box,rl,rr)){brush->SetColor(D2D1::ColorF(1,1,1,Sidebar::Look::toolHover*a));target->FillGeometry(g,brush);g->Release();}
                if(back){const float x=box.right+T::pairGap,y=(box.top+box.bottom)/2;Color(1,T::separatorOpacity);target->FillRoundedRectangle(D2D1::RoundedRect({x,y-T::separatorHeight/2,x+T::separatorWidth,y+T::separatorHeight/2},.5f,.5f),brush);}}
            if(state.focus)FocusRing(box,Design::Radius::control);
            const float size=Design::Size::icon*Motion::PressScale(state.press,reduced,!state.disabled),o=state.disabled?Design::Text::disabled/Design::Text::secondary:1.f;
            if(id=='R'){bool painted=false;if(motion&&!reduced&&lottie.RefreshActive(now))painted=DrawLottie(refreshLottie,lottie.RefreshProgress(now),box,size,o);if(!painted)DrawStopMorph(box,size,o,now);return;}
            const float dx=reduced?0.f:(id=='B'?nav.back:nav.forward).Position(now);Icon(id=='B'?L"back":L"forward",{box.left+dx,box.top,box.right+dx,box.bottom},size,o);return;}
        if(id=='B'||id=='F'){DrawNavButton(id,id=='B'?L"back":L"forward",box,state,now);return;}
        if(id=='R'){auto style=Design::Resolve(Design::ButtonVariant::Raised,state);Round(box,style.radius,style.surface);if(style.focus)FocusRing(box,style.radius);
            const float size=style.iconSize*Motion::PressScale(state.press,motion&&motion->ReducedMotion(),!state.disabled);bool painted=false;
            if(motion&&!motion->ReducedMotion()&&lottie.RefreshActive(now))painted=DrawLottie(refreshLottie,lottie.RefreshProgress(now),box,size,style.iconOpacity);
            if(!painted)DrawStopMorph(box,size,style.iconOpacity,now);
            return;}
        // Copy / star in the column's field: glyph 14 white 50 %, a quiet hover plate, Copy's link -> check morph kept.
        const float g=Sidebar::Complete::addressGlyph*Motion::PressScale(state.press,motion&&motion->ReducedMotion(),!state.disabled);
        if(state.hover>0)White(box,Design::Radius::button,.06f*state.hover*alpha);
        if(state.focus)FocusRing(box,Design::Radius::button);
        const float o=Sidebar::Look::glyphMuted*alpha*(state.disabled?Design::Text::disabled/Design::Text::secondary:1.f);
        if(id=='C'){if(motion&&!motion->ReducedMotion()&&lottie.CopyActive(now)&&DrawLottie(copyLottie,lottie.CopyProgress(now),box,g,alpha))return;
            const float a=motion?motion->copy.Visual(now).opacity:0;SideGlyph(Sidebar::Glyph::copy,box,g,o*(1-a));if(a>0)Icon(L"check",box,g,a*alpha);return;}
        if(id=='K'){if(starred)Icon(L"star-fill",box,g,alpha);else SideGlyph(Sidebar::Glyph::star,box,g,o);}
    }
    // The edge line (Sidebar::Resize; Paper 1Y92-0 / 1Y95-0 / 1Y98-0 / 1Y9B-0): 2 DIP, round caps, just outside the page's
    // left edge, following its rounded corners on a radius-9 arc; a vertical gradient fades it out over 12 DIP at both
    // ends. The page's corner masks draw the part of the arcs inside the page's box (src/main.cpp RenderCorner).
    static D2D1_COLOR_F EdgeColor(unsigned c,float a=1){return D2D1::ColorF(((c>>16)&255)/255.f,((c>>8)&255)/255.f,(c&255)/255.f,a);}
    unsigned EdgeLineColor(bool focused)const{if(!sidebar||sidebarOverlay||side.page.Empty())return 0;if(sidebarEdge==2||focused)return 0x1000000|Sidebar::Resize::drag;if(sidebarEdge==1)return 0x1000000|Sidebar::Resize::hover;return 0;}
    void PaintSideEdge(bool focused){const unsigned c=EdgeLineColor(focused);if(!c||!factory||!target)return;namespace RZ=Sidebar::Resize;const float a=focused?1.f:EdgeAlpha(Motion::Now());if(a<=0)return;
        const auto& P=side.page;const float x=P.left-RZ::line/2,r=RZ::arc,k=Sidebar::Zone::pageRadius,top=P.top-1,bottom=P.bottom+1;
        ID2D1PathGeometry* g=nullptr;if(FAILED(factory->CreatePathGeometry(&g))||!g)return;ID2D1GeometrySink* s=nullptr;
        if(SUCCEEDED(g->Open(&s))&&s){s->BeginFigure({P.left+k,top},D2D1_FIGURE_BEGIN_HOLLOW);
            s->AddArc(D2D1::ArcSegment({x,P.top+k},D2D1::SizeF(r,r),0,D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,D2D1_ARC_SIZE_SMALL));
            s->AddLine({x,P.bottom-k});
            s->AddArc(D2D1::ArcSegment({P.left+k,bottom},D2D1::SizeF(r,r),0,D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,D2D1_ARC_SIZE_SMALL));
            s->EndFigure(D2D1_FIGURE_END_OPEN);s->Close();s->Release();}
        const float span=std::max(1.f,bottom-top),f=std::min(.5f,RZ::fade/span);const unsigned rgb=c&0xFFFFFF;
        D2D1_GRADIENT_STOP stops[4]={{0,EdgeColor(rgb,0)},{f,EdgeColor(rgb,a)},{1-f,EdgeColor(rgb,a)},{1,EdgeColor(rgb,0)}};
        ID2D1GradientStopCollection* col=nullptr;ID2D1LinearGradientBrush* gb=nullptr;ID2D1StrokeStyle* cap=nullptr;
        factory->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND,D2D1_CAP_STYLE_ROUND,D2D1_CAP_STYLE_ROUND),nullptr,0,&cap);
        if(SUCCEEDED(target->CreateGradientStopCollection(stops,4,&col))&&col&&SUCCEEDED(target->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({0,top},{0,bottom}),col,&gb))&&gb)target->DrawGeometry(g,gb,RZ::line,cap);
        if(gb){gb->Release();}if(col){col->Release();}if(cap){cap->Release();}g->Release();}
    void PaintSidebar(const std::vector<std::wstring>& titles,int selected,bool editing,int hover,int focus,Motion::Time now){
        namespace SB=Sidebar;
        if(sideHidden){if(sideShown)PaintSideColumn(side,titles,selected,editing,hover,focus,now,true);
            PaintSidePopover(hover,focus,now);DrawChatFrame();DrawSplit();return;}   // Mode Focus (src/sidebar.h FocusSide)
        // Window bar: compact navigation + address (02 1Q3F-0), window buttons.
        if(!side.addressInColumn){for(int id:{'B','F','R'}){const auto& b=id=='B'?side.back:id=='F'?side.forward:side.reload;PaintSideButton(id,Morph(id,b,now),sidebarOverlay?0:hover,sidebarOverlay?0:focus,now,1,side.navPlates);}PaintSideAddress(side,editing&&!sidebarOverlay,hover,focus,now,1);}
        for(int id:{'m','M','X'})DrawWindowGlyph(id,id=='m'?R(side.minimize):id=='M'?R(side.maximize):R(side.close),hover,focus,now);
        // Under the page windows: the canvas around the page's rounded corners is the window's own. The docked column's paint
        // stops at the page's edge (Sidebar::Complete::minimumLayout): laid out wider than shown (it folds / unfolds) or its
        // pieces gliding, its last 12 DIP fade into the canvas instead of being cut by the page (Val 25 nov., #143).
        {const float edge=side.zone.left;target->PushAxisAlignedClip({0,0,std::max(0.f,edge),height},D2D1_ANTIALIAS_MODE_ALIASED);
            PaintSideColumn(side,titles,selected,editing&&!sidebarOverlay,sidebarOverlay?0:hover,sidebarOverlay?0:focus,now,false);
            target->PopAxisAlignedClip();
            if(side.column>edge+.5f||sideMorphing)DrawEdgeFade(edge);}
        if(sidebarOverlay)PaintSideColumn(sideOver,titles,selected,editing,hover,focus,now,true);
        PaintSidePopover(hover,focus,now);
        PaintSideEdge(focus==SidebarEdgeId&&keyboardModality);   // keyboard focus: the dragging colour
        DrawChatFrame();   // Paper 1YJQ-0
        DrawSplit();
        if(const auto ripple=downloadIcon.At(now).ripple;ripple>=0){const auto d=std::find_if(regions.begin(),regions.end(),[](const Region& r){return r.id=='D';});
            if(d!=regions.end())DrawDownloadRipple(ripple,d->rect,Design::Size::icon);}
    }
    void Build(int count,int selected,const std::vector<std::wstring>* titles=nullptr,Motion::Time now=Motion::Now()){regions.clear();tips.clear();if(contentOnly)return;
        if(!sidebar){sideChip=false;addressTextRight=1e9f;}
        if(!sidebar&&addressFadeSurface!=Design::Surface::raised){addressFadeSurface=Design::Surface::raised;DropAddressFade();}   // the top bar's field
        if(sidebar){BuildSidebar(count,selected,titles,now);return;}
        addressHostOnly=false;addressGlyphOpacity=Design::Address::secondary;addressFont=Design::Type::address.size;addressGlyph=Design::Size::icon;addressTextInset=Design::Address::textInset;addressSlotInset=Design::Address::slotInset;addressSlotSize=Design::Address::slot;
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
        // B03 (vault, review §5), #121 C: the key pill right after the padlock, only when the host says so.
        {const auto pill=VaultPillBox();if(pill.right>pill.left)regions.push_back({VaultKeyId,pill,TR(chrome_tip_fill_password)});}
        request=RequestBox();if(request.right>request.left&&requestLive)regions.push_back({'Q',request,requestTip.c_str(),false});
        // Field actions, right to left 8 DIP apart (Paper): star, copy link. Reload/Stop lives
        // only in the standalone toolbar button; the field text runs to 10 DIP before Copy.
        regions.push_back({'L',{bar.left,44,bar.right-64.f,76},TR(chrome_tip_address)});
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
    // Client DIP (the floating panel's offset is taken off here; outside its visible part nothing is hit).
    int Hit(float x,float y){if(!Local(x,y))return 0;for(auto& r:regions)if(r.id==SidebarEdgeId&&x>=r.rect.left&&x<r.rect.right&&y>=r.rect.top&&y<r.rect.bottom)return r.id;   // #127: the edge wins over the tiles it overlaps
        for(auto& r:regions){if(x>=r.rect.left&&x<r.rect.right&&y>=r.rect.top&&y<r.rect.bottom)return Enabled(r.id)?r.id:0;}return 0;}
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
            // Paper v2: with the close showing, the title dissolves into the tab surface over 20 DIP
            // and the close sits on solid surface; at rest the title keeps the full width and its « … ».
            const float titleVeil=strip.Titled()&&strip.HasClose()&&closable?std::min(1.f,closeShown):0.f;
            if(keyed)tabSnapshots[item.key].veil=titleVeil;
            if(titleVeil>0)TitleVeil(close,title.right,filled?fill:Design::Surface::canvas,titleVeil);
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
    // `dirty`: client DIP.
    void Paint(const std::vector<std::wstring>& titles,int selected,const std::wstring& uri,bool editing,int hover,int focus,const D2D1_RECT_F* dirty=nullptr,Motion::Time now=Motion::Now()){(void)uri;/* the field reads `address`, set by the host */if(!target||!brush)return;++textPass;Build((int)titles.size(),selected,&titles,now);target->BeginDraw();target->PushAxisAlignedClip(dirty?*dirty:D2D1::RectF(0,0,ClientWidth(),height),D2D1_ANTIALIAS_MODE_ALIASED);target->Clear(D2D1::ColorF(Design::Surface::canvas,Design::Surface::canvas,Design::Surface::canvas));
        if(!contentOnly){if(floating){DrawSplit();DrawChatFrame();if(panelLive)DrawPanel(titles,selected,editing,hover,focus,dirty,now);}else if(sidebar)PaintSidebar(titles,selected,editing,hover,focus,now);else DrawChrome(titles,selected,editing,hover,focus,dirty,now,true);}
        target->PopAxisAlignedClip();HRESULT hr=target->EndDraw();if(hr==D2DERR_RECREATE_TARGET)RecreateTarget(); /* every target resource, the Lottie bitmap included */
    }
    // The chat card's 1 DIP #262626 ring (Mode Focus, Paper 203J-0: radius 6; the side panel layouts, Paper 1YJQ-0: 8), on
    // the canvas around the chat's WebView (placed 1 DIP inside `chatFrame`, client DIP; its rounded corners are the host's
    // layered masks). Empty: nothing. One card for both (src/chat-host-ui.h CardOn): the host sets the radius.
    D2D1_RECT_F chatFrame{};float chatRadius=FocusMode::chatRadius;
    void DrawChatFrame(){if(chatFrame.right<=chatFrame.left||chatFrame.bottom<=chatFrame.top)return;namespace FM=FocusMode;const float g=((FM::chatBorderRgb>>16)&255)/255.f,b=FM::chatBorder/2;
        brush->SetColor(D2D1::ColorF(g,g,g,1));target->DrawRoundedRectangle(D2D1::RoundedRect({chatFrame.left+b,chatFrame.top+b,chatFrame.right-b,chatFrame.bottom-b},chatRadius-b,chatRadius-b),brush,FM::chatBorder);}
    // Mode Focus, « Hover en haut » (Paper 1JBQ-0): the panel (#171717, a 1 DIP #FFFFFF14 border, radius 4, no shadow)
    // at its slide position, then the chrome inside it, translated to `origin` and clipped to the inside of the border.
    // The normal chrome's 1 DIP lower hairline is not drawn: the border replaces it.
    void DrawPanel(const std::vector<std::wstring>& titles,int selected,bool editing,int hover,int focus,const D2D1_RECT_F* dirty,Motion::Time now){
        if(!PanelShown())return;
        const D2D1_RECT_F p=panel;namespace FM=FocusMode;
        Round(p,FM::panelRadius,Design::Surface::canvas);
        Color(1,FM::borderAlpha);const float half=FM::border/2;
        target->DrawRoundedRectangle(D2D1::RoundedRect({p.left+half,p.top+half,p.right-half,p.bottom-half},FM::panelRadius-half,FM::panelRadius-half),brush,FM::border);
        target->PushAxisAlignedClip({p.left+FM::border,p.top+FM::border,p.right-FM::border,p.bottom-FM::border},D2D1_ANTIALIAS_MODE_ALIASED);
        D2D1_MATRIX_3X2_F base;target->GetTransform(&base);target->SetTransform(D2D1::Matrix3x2F::Translation(origin.x,origin.y)*base);
        D2D1_RECT_F local{};if(dirty)local={dirty->left-origin.x,dirty->top-origin.y,dirty->right-origin.x,dirty->bottom-origin.y};
        DrawChrome(titles,selected,editing,hover,focus,dirty?&local:nullptr,now,false);
        target->SetTransform(base);target->PopAxisAlignedClip();
    }
    // The panel at rest drawn alone into premultiplied BGRA pixels, outside its rounded shape transparent: the picture the
    // host slides above the page during the slide (src/main.cpp « Mode Focus »: the page windows' region reaches the
    // screen a frame after the window's paint, a moving hole would lag the panel). Drawn on a render target compatible
    // with this one (same brushes, bitmaps and text), read back through Direct2D 1.1; false without it.
    bool RenderPanel(const std::vector<std::wstring>& titles,int selected,bool editing,int hover,int focus,Motion::Time now,std::vector<uint32_t>& out,UINT& w,UINT& h){
        if(!floating||!target||!brush||contentOnly)return false;
        ++textPass;Build((int)titles.size(),selected,&titles,now);
        const auto savedPanel=panel;const auto savedOrigin=origin;const auto rest=PanelAt(1);panel=rest.panel;origin=rest.origin;
        const float s=dpi/96.f;w=(UINT)std::lround((panel.right-panel.left)*s);h=(UINT)std::lround((panel.bottom-panel.top)*s);
        bool ok=false;ID2D1BitmapRenderTarget* bt=nullptr;
        if(w&&h&&SUCCEEDED(target->CreateCompatibleRenderTarget(D2D1::SizeF(w/s,h/s),D2D1::SizeU(w,h),D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS_NONE,&bt))&&bt){
            bt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
            auto* main=SwapTarget(bt);bt->BeginDraw();bt->Clear(D2D1::ColorF(0,0,0,0));bt->SetTransform(D2D1::Matrix3x2F::Translation(-panel.left,-panel.top));
            DrawPanel(titles,selected,editing,hover,focus,nullptr,now);
            bt->SetTransform(D2D1::Matrix3x2F::Identity());const HRESULT hr=bt->EndDraw();SwapTarget(main);
            ID2D1Bitmap* bitmap=nullptr;ID2D1DeviceContext* dc=nullptr;ID2D1Bitmap1* cpu=nullptr;
            if(SUCCEEDED(hr)&&SUCCEEDED(bt->GetBitmap(&bitmap))&&bitmap&&SUCCEEDED(bt->QueryInterface(__uuidof(ID2D1DeviceContext),reinterpret_cast<void**>(&dc)))&&dc){
                const D2D1_BITMAP_PROPERTIES1 props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),(float)dpi,(float)dpi);
                if(SUCCEEDED(dc->CreateBitmap(D2D1::SizeU(w,h),nullptr,0,&props,&cpu))&&cpu&&SUCCEEDED(cpu->CopyFromBitmap(nullptr,bitmap,nullptr))){
                    D2D1_MAPPED_RECT m{};if(SUCCEEDED(cpu->Map(D2D1_MAP_OPTIONS_READ,&m))){out.resize((size_t)w*h);for(UINT y=0;y<h;++y)memcpy(&out[(size_t)y*w],m.bits+(size_t)y*m.pitch,(size_t)w*4);cpu->Unmap();ok=true;}}
            }
            Drop(cpu);Drop(dc);Drop(bitmap);Drop(bt);
        }
        panel=savedPanel;origin=savedOrigin;return ok;
    }
    // Mode Focus in the side layouts: the column at rest drawn alone into premultiplied BGRA pixels (its width + the 1 DIP
    // border, from its top to the window's bottom): the picture the host slides over the page (src/main.cpp « #127 ×
    // Mode Focus »; a moving hole in the page windows would lag a frame). False without Direct2D 1.1.
    bool RenderSideColumn(const std::vector<std::wstring>& titles,int selected,bool editing,int hover,int focus,Motion::Time now,std::vector<uint32_t>& out,UINT& w,UINT& h){
        if(!sidebar||!sideHidden||!target||!brush||contentOnly)return false;
        ++textPass;Build((int)titles.size(),selected,&titles,now);
        const float s=dpi/96.f,top=side.top,cw=side.column+Sidebar::FocusSide::border;w=(UINT)std::lround(cw*s);h=(UINT)std::lround(std::max(1.f,height-top)*s);
        bool ok=false;ID2D1BitmapRenderTarget* bt=nullptr;
        if(w&&h&&SUCCEEDED(target->CreateCompatibleRenderTarget(D2D1::SizeF(w/s,h/s),D2D1::SizeU(w,h),D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS_NONE,&bt))&&bt){
            bt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
            auto* main=SwapTarget(bt);bt->BeginDraw();bt->Clear(D2D1::ColorF(0,0,0,0));bt->SetTransform(D2D1::Matrix3x2F::Translation(0,-top));
            PaintSideColumn(side,titles,selected,editing,hover,focus,now,true);
            bt->SetTransform(D2D1::Matrix3x2F::Identity());const HRESULT hr=bt->EndDraw();SwapTarget(main);
            ID2D1Bitmap* bitmap=nullptr;ID2D1DeviceContext* dc=nullptr;ID2D1Bitmap1* cpu=nullptr;
            if(SUCCEEDED(hr)&&SUCCEEDED(bt->GetBitmap(&bitmap))&&bitmap&&SUCCEEDED(bt->QueryInterface(__uuidof(ID2D1DeviceContext),reinterpret_cast<void**>(&dc)))&&dc){
                const D2D1_BITMAP_PROPERTIES1 props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),(float)dpi,(float)dpi);
                if(SUCCEEDED(dc->CreateBitmap(D2D1::SizeU(w,h),nullptr,0,&props,&cpu))&&cpu&&SUCCEEDED(cpu->CopyFromBitmap(nullptr,bitmap,nullptr))){
                    D2D1_MAPPED_RECT m{};if(SUCCEEDED(cpu->Map(D2D1_MAP_OPTIONS_READ,&m))){out.resize((size_t)w*h);for(UINT y=0;y<h;++y)memcpy(&out[(size_t)y*w],m.bits+(size_t)y*m.pitch,(size_t)w*4);cpu->Unmap();ok=true;}}
            }
            Drop(cpu);Drop(dc);Drop(bitmap);Drop(bt);
        }
        return ok;
    }
    // The tab row, the toolbar and the band in chrome-local DIP (`dirty` too). `lowerEdge`: the normal chrome's hairline
    // and split canvas under it (the floating panel has neither: DrawSplit runs on the canvas before the panel).
    void DrawChrome(const std::vector<std::wstring>& titles,int selected,bool editing,int hover,int focus,const D2D1_RECT_F* dirty,Motion::Time now,bool lowerEdge){
        Round(bar,Design::Radius::control,Design::Surface::raised);DrawLoading(bar);if(focus=='L')AddressRing(bar,Design::Radius::control);   /* #5 */
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
        if(lowerEdge){Color(1,5/255.f);target->FillRectangle(D2D1::RectF(0,ContentTop()-1,width,ContentTop()),brush);
            DrawSplit();}
        // Ripple last, over the neighbouring buttons it reaches, never over the page edge.
        if(const auto ripple=downloadIcon.At(now).ripple;ripple>=0){
            const auto d=std::find_if(regions.begin(),regions.end(),[](const Region& r){return r.id=='D';});
            if(d!=regions.end()){target->PushAxisAlignedClip(D2D1::RectF(0,0,width,ContentTop()-1),D2D1_ANTIALIAS_MODE_ALIASED);DrawDownloadRipple(ripple,d->rect,Design::Size::icon);target->PopAxisAlignedClip();}
        }
    }
};
}
