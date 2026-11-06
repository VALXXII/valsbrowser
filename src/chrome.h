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
#include <set>

namespace NativeChrome {
template<class T> void Drop(T*& p){if(p){p->Release();p=nullptr;}}
const wchar_t* FontFiles[]={L"InterDisplay-Regular.ttf",L"InterDisplay-Medium.ttf",L"Gelasio-Regular.ttf"};
struct FontEnumerator final:IDWriteFontFileEnumerator {
    LONG refs=1; IDWriteFactory* factory; std::wstring folder; int index=-1; IDWriteFontFile* file=nullptr;
    FontEnumerator(IDWriteFactory* f,std::wstring p):factory(f),folder(std::move(p)){factory->AddRef();}
    ~FontEnumerator(){Drop(file);Drop(factory);}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** p)override{if(!p)return E_POINTER;*p=nullptr;if(iid==IID_IUnknown||iid==__uuidof(IDWriteFontFileEnumerator)){*p=this;AddRef();return S_OK;}return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return InterlockedIncrement(&refs);}
    ULONG STDMETHODCALLTYPE Release()override{auto n=InterlockedDecrement(&refs);if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE MoveNext(BOOL* more)override{Drop(file);*more=++index<3;if(!*more)return S_OK;return factory->CreateFontFileReference((folder+FontFiles[index]).c_str(),nullptr,&file);}
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
struct Region{int id;D2D1_RECT_F rect;const wchar_t* tip;};
class Chrome {
    HWND hwnd=nullptr;std::wstring assets;ID2D1Factory* factory=nullptr;ID2D1RenderTarget* target=nullptr;ID2D1HwndRenderTarget* hwndTarget=nullptr;IWICBitmap* surface=nullptr;ID2D1SolidColorBrush* brush=nullptr;
    IDWriteFactory* write=nullptr;FontLoader* fontLoader=nullptr;IDWriteFontCollection* fonts=nullptr;IWICImagingFactory* wic=nullptr;
    std::map<std::wstring,ID2D1Bitmap*> bitmaps;
    Motion::State* motion=nullptr;
    std::vector<std::shared_ptr<const FaviconImage>> favicons;
    std::map<std::shared_ptr<const FaviconImage>,ID2D1Bitmap*,std::owner_less<std::shared_ptr<const FaviconImage>>> faviconBitmaps;
public:
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
    void SetFavicons(std::vector<std::shared_ptr<const FaviconImage>> images){favicons=std::move(images);for(auto it=faviconBitmaps.begin();it!=faviconBitmaps.end();){if(std::find(favicons.begin(),favicons.end(),it->first)==favicons.end()){Drop(it->second);it=faviconBitmaps.erase(it);}else ++it;}}
    // Missing/empty/invalid site images are normal favicon absence, not engine errors.
    std::shared_ptr<const FaviconImage> DecodeFavicon(IStream* stream){if(!wic||!stream)return {};STATSTG stat{};if(FAILED(stream->Stat(&stat,STATFLAG_NONAME))||stat.cbSize.QuadPart==0||stat.cbSize.QuadPart>4*1024*1024)return {};LARGE_INTEGER zero{};if(FAILED(stream->Seek(zero,STREAM_SEEK_SET,nullptr)))return {};IWICBitmapDecoder* decoder=nullptr;IWICBitmapFrameDecode* frame=nullptr;IWICFormatConverter* converter=nullptr;UINT w=0,h=0;HRESULT hr=wic->CreateDecoderFromStream(stream,nullptr,WICDecodeMetadataCacheOnLoad,&decoder);if(SUCCEEDED(hr))hr=decoder->GetFrame(0,&frame);if(SUCCEEDED(hr))hr=frame->GetSize(&w,&h);if(SUCCEEDED(hr)&&(!w||!h||w>1024||h>1024))hr=E_INVALIDARG;if(SUCCEEDED(hr))hr=wic->CreateFormatConverter(&converter);if(SUCCEEDED(hr))hr=converter->Initialize(frame,GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom);std::shared_ptr<FaviconImage> image;if(SUCCEEDED(hr)){image=std::make_shared<FaviconImage>();image->width=w;image->height=h;image->stride=w*4;image->pixels.resize(image->stride*h);hr=converter->CopyPixels(nullptr,image->stride,(UINT)image->pixels.size(),image->pixels.data());}Drop(converter);Drop(frame);Drop(decoder);return SUCCEEDED(hr)?image:nullptr;}
    void DrawFavicon(int index,D2D1_RECT_F slot){auto image=index<(int)favicons.size()?favicons[index]:nullptr;if(!image){Icon(L"favicon",slot);return;}auto& bitmap=faviconBitmaps[image];if(!bitmap){auto properties=D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));target->CreateBitmap(D2D1::SizeU(image->width,image->height),image->pixels.data(),image->stride,properties,&bitmap);}if(bitmap){float factor=std::min((slot.right-slot.left)/image->width,(slot.bottom-slot.top)/image->height),w=image->width*factor,h=image->height*factor;float x=(slot.left+slot.right-w)/2,y=(slot.top+slot.bottom-h)/2;target->DrawBitmap(bitmap,{x,y,x+w,y+h});}else Icon(L"favicon",slot);}
    void SetMotion(Motion::State* state){motion=state;}
    bool animateSelection=false;
    std::set<int> disabled;
    bool Enabled(int id)const{return disabled.count(id)==0;}
    void SetEnabled(int id,bool enabled){if(enabled)disabled.erase(id);else disabled.insert(id);}
    UINT dpi=96;float width=1180,height=815;int first=0;D2D1_RECT_F bar{},edit{};std::vector<Region> regions;
    ~Chrome(){Dispose();}
    void Dispose(){Drop(lottieBitmap);lottieSide=0;lottie.Clear();copyLottie.Clear();refreshLottie.Clear();ClearFaviconBitmaps();favicons.clear();for(auto& b:bitmaps)Drop(b.second);bitmaps.clear();Drop(brush);Drop(target);hwndTarget=nullptr;Drop(surface);Drop(wic);Drop(fonts);if(write&&fontLoader)write->UnregisterFontCollectionLoader(fontLoader);Drop(fontLoader);Drop(write);Drop(factory);}
    HRESULT InitResources(const std::wstring& root){assets=root+L"\\assets\\";InitLottie();HRESULT hr=D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,&factory);if(FAILED(hr))return hr;
        hr=DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),(IUnknown**)&write);if(FAILED(hr))return hr;
        fontLoader=new FontLoader(assets+L"fonts\\");hr=write->RegisterFontCollectionLoader(fontLoader);if(FAILED(hr))return hr;const UINT key=1;hr=write->CreateCustomFontCollection(fontLoader,&key,sizeof(key),&fonts);if(FAILED(hr))return hr;for(auto family:{L"Inter Display",L"Gelasio"}){UINT32 index;BOOL exists=FALSE;hr=fonts->FindFamilyName(family,&index,&exists);if(FAILED(hr)||!exists)return E_FAIL;}
        hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic));return hr;
    }
    HRESULT Init(HWND h,const std::wstring& root){hwnd=h;HRESULT hr=InitResources(root);return FAILED(hr)?hr:Resize();}
    HRESULT InitOffscreen(const std::wstring& root,UINT dpiValue,float w,float h){dpi=dpiValue;width=w;height=h;HRESULT hr=InitResources(root);if(FAILED(hr))return hr;hr=wic->CreateBitmap((UINT)(w*dpi/96),(UINT)(h*dpi/96),GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,&surface);if(FAILED(hr))return hr;hr=factory->CreateWicBitmapRenderTarget(surface,D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,D2D1::PixelFormat(),(float)dpi,(float)dpi),&target);if(FAILED(hr))return hr;target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);return target->CreateSolidColorBrush(D2D1::ColorF(1,1,1),&brush);}
    HRESULT SavePng(const wchar_t* path){if(!surface)return E_FAIL;IWICStream* stream=nullptr;IWICBitmapEncoder* encoder=nullptr;IWICBitmapFrameEncode* frame=nullptr;HRESULT hr=wic->CreateStream(&stream);if(SUCCEEDED(hr))hr=stream->InitializeFromFilename(path,GENERIC_WRITE);if(SUCCEEDED(hr))hr=wic->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder);if(SUCCEEDED(hr))hr=encoder->Initialize(stream,WICBitmapEncoderNoCache);if(SUCCEEDED(hr))hr=encoder->CreateNewFrame(&frame,nullptr);if(SUCCEEDED(hr))hr=frame->Initialize(nullptr);if(SUCCEEDED(hr))hr=frame->WriteSource(surface,nullptr);if(SUCCEEDED(hr))hr=frame->Commit();if(SUCCEEDED(hr))hr=encoder->Commit();Drop(frame);Drop(encoder);Drop(stream);return hr;}
    UINT32 SurfacePixel(float x,float y){UINT32 pixel=0;WICRect r{(INT)(x*dpi/96),(INT)(y*dpi/96),1,1};if(surface)surface->CopyPixels(&r,4,4,(BYTE*)&pixel);return pixel;}
    HRESULT Resize(){if(!factory||!hwnd)return S_OK;RECT r;GetClientRect(hwnd,&r);dpi=GetDpiForWindow(hwnd);width=(float)r.right*96/dpi;height=(float)r.bottom*96/dpi;
        if(target){target->SetDpi((float)dpi,(float)dpi);return hwndTarget->Resize(D2D1::SizeU(r.right,r.bottom));}
        HRESULT hr=factory->CreateHwndRenderTarget(D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,D2D1::PixelFormat(),(float)dpi,(float)dpi),D2D1::HwndRenderTargetProperties(hwnd,D2D1::SizeU(r.right,r.bottom)),&hwndTarget);if(FAILED(hr))return hr;target=hwndTarget;target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);return target->CreateSolidColorBrush(D2D1::ColorF(1,1,1),&brush);
    }
    void Color(float c,float alpha=1){brush->SetColor(D2D1::ColorF(c,c,c,alpha));}
    void Round(D2D1_RECT_F r,float radius,float c){Color(c);target->FillRoundedRectangle(D2D1::RoundedRect(r,radius,radius),brush);}
    HRESULT MakeTextLayout(const std::wstring& value,D2D1_RECT_F slot,float size,bool brand,bool medium,float tracking,IDWriteTextLayout** output,TextMeasure& placement){
        if(!write||!fonts||!output)return E_UNEXPECTED;
        IDWriteTextFormat* format=nullptr;IDWriteTextLayout* layout=nullptr;
        HRESULT hr=write->CreateTextFormat(brand?L"Gelasio":L"Inter Display",fonts,medium?DWRITE_FONT_WEIGHT_MEDIUM:DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,size,L"fr-FR",&format);
        if(FAILED(hr))return hr;
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        const float available=std::max(1.f,slot.right-slot.left);
        hr=write->CreateTextLayout(value.c_str(),(UINT32)value.size(),format,available,4096,&layout);Drop(format);if(FAILED(hr))return hr;
        IDWriteTextLayout1* layout1=nullptr;
        if(SUCCEEDED(layout->QueryInterface(__uuidof(IDWriteTextLayout1),(void**)&layout1))){layout1->SetCharacterSpacing(tracking,0,0,{0,(UINT32)value.size()});Drop(layout1);}
        IDWriteTypography* typo=nullptr;write->CreateTypography(&typo);
        if(typo){for(auto tag:{DWRITE_MAKE_OPENTYPE_TAG('c','p','s','p'),DWRITE_MAKE_OPENTYPE_TAG('c','v','1','2'),DWRITE_MAKE_OPENTYPE_TAG('t','n','u','m')})typo->AddFontFeature({(DWRITE_FONT_FEATURE_TAG)tag,1});layout->SetTypography(typo,{0,(UINT32)value.size()});Drop(typo);}
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
    TextMeasure MeasureText(const std::wstring& value,D2D1_RECT_F slot,float size,bool medium=false,bool brand=false,float tracking=0){TextMeasure m;IDWriteTextLayout* layout=nullptr;MakeTextLayout(value,slot,size,brand,medium,tracking,&layout,m);Drop(layout);return m;}
    void Text(const std::wstring& value,D2D1_RECT_F slot,float size,float opacity,bool brand=false,bool medium=false,float tracking=0){if(value.empty())return;IDWriteTextLayout* layout=nullptr;TextMeasure m;if(FAILED(MakeTextLayout(value,slot,size,brand,medium,tracking,&layout,m)))return;
        // Restrict horizontal overflow to its slot, but retain all measured
        // vertical glyph ink. All labels use the full 32 DIP control height.
        auto clip=slot;clip.top=std::min(slot.top,m.inkTop-96.f/dpi);clip.bottom=std::max(slot.bottom,m.inkBottom+96.f/dpi);
        target->PushAxisAlignedClip(clip,D2D1_ANTIALIAS_MODE_ALIASED);Color(1,opacity);
        target->DrawTextLayout(D2D1::Point2F(slot.left,m.originY),layout,brush,D2D1_DRAW_TEXT_OPTIONS_NONE);
        target->PopAxisAlignedClip();Drop(layout);
    }
    void Icon(const wchar_t* name,D2D1_RECT_F rect,float size=16,float opacity=1){if(opacity<=0)return;int imageDpi=384;for(int d:{96,120,144,168,192,240,288,384})if(d>=int(dpi)){imageDpi=d;break;}std::wstring key=std::wstring(name)+L"-"+std::to_wstring(imageDpi)+L".png";auto& bitmap=bitmaps[key];
        if(!bitmap){IWICBitmapDecoder* dec=nullptr;IWICBitmapFrameDecode* frame=nullptr;IWICFormatConverter* conv=nullptr;HRESULT hr=wic->CreateDecoderFromFilename((assets+L"icons\\"+key).c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&dec);if(SUCCEEDED(hr))hr=dec->GetFrame(0,&frame);if(SUCCEEDED(hr))hr=wic->CreateFormatConverter(&conv);if(SUCCEEDED(hr))hr=conv->Initialize(frame,GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom);if(SUCCEEDED(hr))target->CreateBitmapFromWicBitmap(conv,nullptr,&bitmap);Drop(conv);Drop(frame);Drop(dec);}
        if(bitmap){float x=(rect.left+rect.right-size)/2,y=(rect.top+rect.bottom-size)/2;target->DrawBitmap(bitmap,D2D1::RectF(x,y,x+size,y+size),opacity);}
    }
    void FocusRing(D2D1_RECT_F box,float radius){const float inset=Design::Size::focusStroke/2;box.left+=inset;box.top+=inset;box.right-=inset;box.bottom-=inset;Color(Design::Focus::color,Design::Focus::opacity);target->DrawRoundedRectangle(D2D1::RoundedRect(box,radius,radius),brush,Design::Size::focusStroke);}
    void DrawButton(const wchar_t* icon,D2D1_RECT_F box,Design::ButtonVariant variant,Design::ButtonState state){
        auto style=Design::Resolve(variant,state);
        if(style.fill)Round(box,style.radius,style.surface);
        if(style.focus)FocusRing(box,style.radius);
        Icon(icon,box,style.iconSize*Motion::PressScale(state.press,motion&&motion->ReducedMotion(),!state.disabled),style.iconOpacity);
    }
    HRESULT PaintStateSheet(){if(!target)return E_UNEXPECTED;target->BeginDraw();target->Clear(D2D1::ColorF(Design::Surface::canvas,Design::Surface::canvas,Design::Surface::canvas));
        Text(L"Native components",{24,16,700,48},20,1,false,true);
        const wchar_t* labels[]={L"Default",L"Hover",L"Pressed",L"Disabled",L"Keyboard focus"};
        const wchar_t* rows[]={L"Ghost / new tab",L"Raised / navigation",L"Embedded / reload",L"Window controls"};
        Design::ButtonVariant variants[]={Design::ButtonVariant::Ghost,Design::ButtonVariant::Raised,Design::ButtonVariant::Embedded,Design::ButtonVariant::Window};
        for(int col=0;col<5;++col)Text(labels[col],{240.f+col*128,56,364.f+col*128,88},14,.7f);
        for(int row=0;row<4;++row){float y=104.f+row*64;Text(rows[row],{24,y,230,y+32},14,.7f);
            for(int col=0;col<5;++col){float x=240.f+col*128;Design::ButtonState state{col==1?1.f:0.f,col==2?1.f:0.f,col==3,col==4};
                if(row==2)Round({x-4,y-4,x+100,y+36},Design::Radius::control,Design::Surface::raised);
                DrawButton(row==0?L"plus":row==1?L"back":row==2?L"reload":L"minimize",{x,y,x+32,y+32},variants[row],state);
                if(row==3){DrawButton(L"maximize",{x+36,y,x+68,y+32},variants[row],state);DrawButton(L"close",{x+72,y,x+104,y+32},variants[row],state);}
            }
        }
        Text(L"Tab surfaces / stable close slot",{24,384,400,416},14,.7f);
        for(int col=0;col<3;++col){float x=240.f+col*190;D2D1_RECT_F tab{x,432,x+128,464};Round(tab,Design::Radius::control,Design::TabSurface(col==0,col==1?1:0));DrawFavicon(0,{x+8,440,x+24,456});Text(L"Ggyp \u00c9\u00e7",{x+32,432,x+96,464},14,.5f,false,true);if(col!=2)Icon(L"tab-close",{x+100,436,x+124,460});if(col==2)FocusRing(tab,Design::Radius::control);}
        Text(L"Production Direct2D + DirectWrite / Phosphor Bold / no WebView2",{24,496,900,528},14,.7f);
        return target->EndDraw();
    }
    void Build(int count,int selected){regions.clear();int capacity=Design::TabStrip::Capacity(width);if(selected<first)first=selected;if(selected>=first+capacity)first=selected-capacity+1;first=std::max(0,first);int shown=std::min(capacity,count-first);
        regions.push_back({900,{0,2,51,38},L"Liste des onglets · molette pour changer"});
        for(int i=0;i<shown;i++){
            auto close=Design::TabStrip::CloseSlot(Design::TabStrip::Surface(i));auto region=Design::TabStrip::Region(i);
            // Close first: its hit area wins over the containing tab, and IDs
            // never collide with tabs, keyboard commands or toolbar buttons.
            regions.push_back({CloseBase+first+i,{close.left,close.top,close.right,close.bottom},L"Fermer cet onglet"});
            regions.push_back({TabBase+first+i,{region.left,region.top,region.right,region.bottom},L"Onglet · clic milieu pour fermer"});
        }
        float x=Design::TabStrip::NewTabLeft(shown);regions.push_back({'T',{x,6,x+Design::Size::control,38},L"Nouvel onglet (Ctrl+T)"});
        regions.push_back({'m',{width-118,4,width-86,36},L"Réduire"});regions.push_back({'M',{width-78,4,width-46,36},L"Agrandir / restaurer"});regions.push_back({'X',{width-38,4,width-6,36},L"Fermer la fenêtre"});
        regions.push_back({'B',{6,44,38,76},L"Précédent (Alt+←)"});regions.push_back({'F',{44,44,76,76},L"Suivant (Alt+→)"});bar={82,44,width-103,76};edit={92,44,bar.right-79,76};
        regions.push_back({'L',{bar.left,44,bar.right-73,76},L"Adresse ou recherche (Ctrl+L)"});regions.push_back({'R',{bar.right-69,48,bar.right-69+Design::Size::compact,72},L"Actualiser (Ctrl+R)"});regions.push_back({'C',{bar.right-28,48,bar.right-4,72},L"Copier le lien"});
        regions.push_back({'D',{width-97,48,width-73,72},L"Téléchargements"});regions.push_back({'P',{width-67,48,width-43,72},L"Navigation privée : disponibilité"});regions.push_back({'S',{width-30,48,width-6,72},L"Réglages"});
    }
    std::vector<int> FocusOrder()const{std::vector<int> order;for(auto& region:regions){if(IsClose(region.id))continue;if(Enabled(region.id))order.push_back(region.id);if(IsTab(region.id)&&Enabled(CloseBase+TabIndex(region.id)))order.push_back(CloseBase+TabIndex(region.id));}return order;}
    int Hit(float x,float y){for(auto& r:regions)if(x>=r.rect.left&&x<r.rect.right&&y>=r.rect.top&&y<r.rect.bottom)return Enabled(r.id)?r.id:0;return 0;}
    bool AssetsReady(){if(bitmaps.size()<11)return false;for(auto& b:bitmaps)if(!b.second)return false;return fonts&&fonts->GetFontFamilyCount()==2;}
    void Paint(const std::vector<std::wstring>& titles,int selected,const std::wstring& uri,bool editing,int hover,int focus,const D2D1_RECT_F* dirty=nullptr,Motion::Time now=Motion::Now()){if(!target||!brush)return;Build((int)titles.size(),selected);target->BeginDraw();target->PushAxisAlignedClip(dirty?*dirty:D2D1::RectF(0,0,width,height),D2D1_ANTIALIAS_MODE_ALIASED);target->Clear(D2D1::ColorF(Design::Surface::canvas,Design::Surface::canvas,Design::Surface::canvas));
        Round(bar,Design::Radius::control,Design::Surface::raised);if(focus=='L')FocusRing(bar,Design::Radius::control);
        for(auto& r:regions){int id=r.id;auto box=r.rect;if(IsClose(id))continue;if(IsTab(id)){int n=TabIndex(id);auto surface=Design::TabStrip::Surface(n-first);D2D1_RECT_F tab{surface.left,surface.top,surface.right,surface.bottom};auto close=Design::TabStrip::CloseSlot(surface);auto title=Design::TabStrip::TitleSlot(surface);auto favicon=Design::TabStrip::FaviconSlot(surface);bool tabHovered=TabIndex(hover)==n;float tabHover=motion?std::max(motion->Visual(TabBase+n,now).hover,motion->Visual(CloseBase+n,now).hover):(tabHovered?1.f:0.f);
                // Exactly one opaque surface: no second close-button hover fill.
                float emphasis=motion&&animateSelection?motion->selection.Visual(n+1,now):(n==selected?1.f:0.f);
                float base=Design::TabSurface(false,tabHover);
                if(emphasis>0||tabHover>0){Round(tab,Design::Radius::control,base+(Design::Surface::selected-base)*emphasis);}
                if(focus==TabBase+n)FocusRing(tab,Design::Radius::control);
                DrawFavicon(n,{favicon.left,favicon.top,favicon.right,favicon.bottom});
                Text(titles[n],{title.left,title.top,title.right,title.bottom},Design::Type::label.size,n==selected?Design::Text::secondary:Design::Text::muted,false,Design::Type::label.medium,Design::Type::label.tracking);
                if(tabHover>0||focus==CloseBase+n){float press=motion?motion->Visual(CloseBase+n,now).press:0;Icon(L"tab-close",{close.left,close.top,close.right,close.bottom},Design::Size::icon*Motion::PressScale(press,motion&&motion->ReducedMotion()),focus==CloseBase+n?1:tabHover);}
                if(focus==CloseBase+n)FocusRing({close.left,close.top,close.right,close.bottom},Design::Radius::inset);
                continue;}
            if(id==900){Icon(L"brand-flower",box,16);if(focus==900)FocusRing(box,Design::Radius::control);continue;}if(id=='L')continue;
            auto interaction=motion?motion->Visual(id,now):Motion::InteractionVisual{hover==id?1.f:0.f,0};
            const wchar_t* icon=nullptr;switch(id){case 'T':icon=L"plus";break;case 'm':icon=L"minimize";break;case 'M':icon=L"maximize";break;case 'X':icon=L"close";break;case 'B':icon=L"back";break;case 'F':icon=L"forward";break;case 'R':icon=L"reload";break;case 'C':icon=L"copy";break;case 'D':icon=L"downloads";break;case 'P':icon=L"private";break;case 'S':icon=L"settings";break;}if(icon){auto variant=VariantFor(id);Design::ButtonState state{interaction.hover,interaction.press,!Enabled(id),focus==id};
                if((id=='C'||id=='R')&&motion){
                    auto style=Design::Resolve(variant,state);if(style.fill)Round(box,style.radius,style.surface);if(style.focus)FocusRing(box,style.radius);
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
                    }else if(!painted)Icon(icon,box,size,style.iconOpacity);
                }
                else DrawButton(icon,box,variant,state);}

        }
        Color(Design::Text::primary,Design::Surface::separatorOpacity);target->FillRoundedRectangle(D2D1::RoundedRect({bar.right-37,55,bar.right-36,65},.5f,.5f),brush);target->FillRoundedRectangle(D2D1::RoundedRect({width-37,55,width-36,65},.5f,.5f),brush);
        if(!editing)Text(uri.empty()?L"Search, browse, whatever":uri,{92,44,bar.right-79,76},Design::Type::address.size,Design::Text::address,false,Design::Type::address.medium,Design::Type::address.tracking);
        target->PopAxisAlignedClip();HRESULT hr=target->EndDraw();if(hr==D2DERR_RECREATE_TARGET){ClearFaviconBitmaps();for(auto& b:bitmaps)Drop(b.second);bitmaps.clear();Drop(brush);Drop(target);hwndTarget=nullptr;Resize();}
    }
};
}
